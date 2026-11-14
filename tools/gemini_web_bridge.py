#!/usr/bin/env python3
"""OpenAI-compatible HTTP bridge in front of the Gemini web app (Hydrogen / Argon).

The Gemini 4 web modes have no public API and are not served by the Antigravity /
CloudCode endpoint, so this drives the real gemini.google.com UI over a logged-in
Firefox profile (see gemini_web_task.py).

  python3 tools/gemini_web_bridge.py --port 9099
  curl -s localhost:9099/v1/models
  curl -s localhost:9099/v1/chat/completions -H 'content-type: application/json' \
       -d '{"model":"gemini-4-hydrogen","messages":[{"role":"user","content":"hi"}]}'

Supports stream=true (SSE with immediate headers + 10s keep-alives) and stream=false.
Requests are serialised: one Firefox instance at a time on one profile.
"""
from __future__ import annotations

import argparse
import json
import queue
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import gemini_web_task as task

MODEL_TO_MODE = {
    "gemini-4-hydrogen": "hydrogen",
    "gemini-4-argon": "argon",
    "gemini-4-carbon": "carbon",
    "hydrogen": "hydrogen",
    "argon": "argon",
}
LOCK = threading.Lock()
ARGS = None
SESSION = None


def session():
    """One warm Firefox instance shared by all requests."""
    global SESSION
    if SESSION is None:
        SESSION = task.WebSession(headed=False, profile=ARGS.profile, driver_path=ARGS.driver)
    return SESSION


def run_task(body: dict):
    model = body.get("model") or "gemini-4-hydrogen"
    mode = MODEL_TO_MODE.get(model)
    if not mode:
        raise ValueError(f"unknown model '{model}'; use {sorted(MODEL_TO_MODE)}")
    parts = []
    for m in body.get("messages") or []:
        content = m.get("content")
        if isinstance(content, list):
            content = "".join(c.get("text", "") for c in content if isinstance(c, dict))
        if content:
            parts.append(f"{m.get('role', 'user')}: {content}")
    prompt = "\n\n".join(parts).strip()
    if not prompt:
        raise ValueError("empty messages")
    print(f"[bridge] {model} mode={mode} prompt_chars={len(prompt)}", flush=True)
    with LOCK:
        try:
            result = session().run(prompt, mode=mode, timeout=int(body.get("timeout", 180)))
        except BaseException:
            session().close()  # next request starts from a clean browser
            raise
    print(f"[bridge] {model} reply_chars={len(result['reply'])} {result['seconds']}s", flush=True)
    return model, prompt, result


def completion(model: str, prompt: str, result: dict) -> dict:
    return {
        "id": "chatcmpl-" + uuid.uuid4().hex[:16],
        "object": "chat.completion",
        "created": int(time.time()),
        "model": model,
        "choices": [{"index": 0,
                     "message": {"role": "assistant", "content": result["reply"]},
                     "finish_reason": "stop"}],
        "usage": {"prompt_tokens": len(prompt) // 4,
                  "completion_tokens": len(result["reply"]) // 4,
                  "total_tokens": (len(prompt) + len(result["reply"])) // 4},
        "gemini_mode": result["mode"],
        "elapsed_seconds": result["seconds"],
    }


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def _json(self, code: int, payload: dict) -> None:
        raw = json.dumps(payload).encode()
        self.send_response(code)
        self.send_header("content-type", "application/json")
        self.send_header("content-length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):  # noqa: N802
        path = self.path.rstrip("/")
        if path in ("/v1/models", "/models"):
            self._json(200, {"object": "list", "data": [
                {"id": m, "object": "model", "owned_by": "google-web"}
                for m in ("gemini-4-hydrogen", "gemini-4-argon")]})
        elif path in ("/health", "/healthz"):
            self._json(200, {"ok": True})
        else:
            self._json(404, {"error": {"message": "not found"}})

    def do_POST(self):  # noqa: N802
        if self.path.rstrip("/") != "/v1/chat/completions":
            self._json(404, {"error": {"message": "not found"}})
            return
        try:
            length = int(self.headers.get("content-length") or 0)
            body = json.loads(self.rfile.read(length) or b"{}")
        except Exception as exc:  # noqa: BLE001
            self._json(400, {"error": {"message": str(exc)}})
            return
        if body.get("stream"):
            self._stream(body)
            return
        try:
            model, prompt, result = run_task(body)
            self._json(200, completion(model, prompt, result))
        except Exception as exc:  # noqa: BLE001
            self._json(400, {"error": {"message": str(exc)}})

    def _stream(self, body: dict) -> None:
        cid = "chatcmpl-" + uuid.uuid4().hex[:16]
        created = int(time.time())
        model = body.get("model") or "gemini-4-hydrogen"

        def frame(delta: dict, finish=None) -> bytes:
            payload = {"id": cid, "object": "chat.completion.chunk", "created": created,
                       "model": model,
                       "choices": [{"index": 0, "delta": delta, "finish_reason": finish}]}
            return f"data: {json.dumps(payload)}\n\n".encode()

        self.send_response(200)
        self.send_header("content-type", "text/event-stream")
        self.send_header("cache-control", "no-cache")
        self.send_header("connection", "close")
        self.end_headers()
        try:
            self.wfile.write(frame({"role": "assistant"}))
            self.wfile.flush()
        except Exception:  # noqa: BLE001
            return

        box: queue.Queue = queue.Queue()

        def work() -> None:
            try:
                box.put(("ok", run_task(body)))
            except BaseException as exc:  # noqa: BLE001
                box.put(("err", exc))

        threading.Thread(target=work, daemon=True).start()
        outcome = None
        while outcome is None:
            try:
                outcome = box.get(timeout=10)
            except queue.Empty:
                try:
                    self.wfile.write(b": keep-alive\n\n")
                    self.wfile.flush()
                except Exception:  # client went away
                    return
        try:
            if outcome[0] == "err":
                self.wfile.write(frame({"content": f"[bridge error] {outcome[1]}"}, "stop"))
            else:
                _, prompt, result = outcome[1]
                self.wfile.write(frame({"content": result["reply"]}, "stop"))
            self.wfile.write(b"data: [DONE]\n\n")
            self.wfile.flush()
        except Exception:  # noqa: BLE001
            pass

    def log_message(self, fmt, *args):  # quiet
        pass


def main() -> None:
    global ARGS
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=9099)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--profile", default=task.DEFAULT_PROFILE)
    ap.add_argument("--driver", default=task.DEFAULT_DRIVER)
    ARGS = ap.parse_args()
    srv = ThreadingHTTPServer((ARGS.host, ARGS.port), Handler)
    print(f"gemini web bridge on http://{ARGS.host}:{ARGS.port} "
          f"(models: gemini-4-hydrogen, gemini-4-argon)")
    srv.serve_forever()


if __name__ == "__main__":
    main()
