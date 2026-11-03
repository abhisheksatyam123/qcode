#!/usr/bin/env python3
"""End-to-end smoke test for orchestrator mode, over the server API.

A scripted stub OpenAI-compatible provider plays both roles:

  lead (the turn that contains "SMOKE LEAD")
    1. spawns three background subagents, A, B and C,
    2. kills C,
    3. waits for A,
    4. resumes A with a follow-up,
    5. checks the team with the status action,
    6. answers "SMOKE DONE".

  subagents (every request without "SMOKE LEAD")
    * A and B reply at once,
    * C holds its reply until the lead's kill has been handed back, so it
      ends as killed. Subagents make non-streaming calls, so a killed one has
      no partial text to keep; its stored row says so,
    * A's follow-up gets a reply of its own.

The script then checks the server API: the three tasks, their statuses and
the per-status counts, C's stored "[killed]" row, and that the lead finished.
Run it against a built binary:

  scripts/orchestrator_smoke.py --binary build/host-debug/apps/qcode/qcode
"""

import argparse
import json
import re
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import subprocess

sys.path.insert(0, str(Path(__file__).resolve().parent))
from perf_guard import MODEL, base_env, free_port, request, wait_ready, write_config  # noqa: E402

LEAD_PROMPT = "SMOKE LEAD"
SPAWN_NAMES = ["A", "B", "C"]
ID_RE = re.compile(r"(ses_[0-9a-z]+)")


class Stub:
    """Scripted provider. Lead and subagent requests are told apart by the prompt."""

    def __init__(self) -> None:
        self.kill_seen = threading.Event()
        self.lead_ids: dict[str, str] = {}
        self.subagent_hits = Counter()
        self.lock = threading.Lock()
        outer = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_args) -> None:
                pass

            def do_POST(self) -> None:  # noqa: N802
                body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", "0") or 0)) or b"{}")
                if not self.path.endswith("/chat/completions"):
                    self.send_response(404)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                if not body.get("stream"):
                    # Non-streaming callers (subagents) expect one JSON object.
                    reply = outer.respond_json(body)
                    data = json.dumps(reply).encode()
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(data)))
                    self.end_headers()
                    self.wfile.write(data)
                    return
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Connection", "close")
                self.end_headers()
                outer.respond(body, self)

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.server.daemon_threads = True
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    def start(self) -> None:
        self.thread.start()

    def stop(self) -> None:
        self.server.shutdown()
        self.server.server_close()

    # ── SSE helpers ──
    @staticmethod
    def send(handler, delta: dict, finish: str | None = None) -> None:
        event = {"choices": [{"index": 0, "delta": delta, "finish_reason": finish}]}
        handler.wfile.write(f"data: {json.dumps(event)}\n\n".encode())
        handler.wfile.flush()

    @staticmethod
    def finish(handler) -> None:
        handler.wfile.write(b"data: [DONE]\n\n")
        handler.wfile.flush()

    def respond(self, body: dict, handler) -> None:
        text = json.dumps(body.get("messages", []))
        if LEAD_PROMPT in text or "SMOKE TUI" in text:
            self.respond_lead(body, handler)
        else:
            self.respond_subagent(body, handler)

    def respond_json(self, body: dict) -> dict:
        """Non-streaming subagent reply: the same decisions as the SSE path."""
        name = self.subagent_name(body)
        with self.lock:
            self.subagent_hits[name] += 1
        content = f"report {name}"
        if name == "C":
            self.kill_seen.wait(timeout=30)
        return {"id": "smoke", "object": "chat.completion", "model": MODEL,
                "choices": [{"index": 0, "finish_reason": "stop",
                             "message": {"role": "assistant", "content": content}}]}

    @staticmethod
    def subagent_name(body: dict) -> str:
        messages = body.get("messages", [])
        last_user = next((json.dumps(m.get("content")) for m in reversed(messages)
                          if m.get("role") == "user"), "")
        if "follow up A" in last_user:
            return "A-resumed"
        return next((n for n in SPAWN_NAMES if f"job {n}" in last_user), "unknown")

    def respond_subagent(self, body: dict, handler) -> None:
        messages = body.get("messages", [])
        last_user = next((json.dumps(m.get("content")) for m in reversed(messages)
                          if m.get("role") == "user"), "")
        if "follow up A" in last_user:
            name = "A-resumed"
        else:
            name = next((n for n in SPAWN_NAMES if f"job {n}" in last_user), "unknown")
        with self.lock:
            self.subagent_hits[name] += 1
        self.send(handler, {"role": "assistant", "content": f"report {name}"})
        if name == "C":
            self.kill_seen.wait(timeout=30)
        self.send(handler, {}, finish="stop")
        self.finish(handler)

    def respond_lead(self, body: dict, handler) -> None:
        messages = body.get("messages", [])
        results = [m for m in messages if m.get("role") == "tool"]
        n = len(results)
        if "SMOKE TUI" in json.dumps(messages):
            # TUI scenario: A finishes, C runs until someone stops it with k.
            if n == 0:
                self.tool_calls(handler, [
                    ("task", {"prompt": "job A", "description": "A", "background": True}),
                    ("task", {"prompt": "job C", "description": "C", "background": True})])
            elif n == 2:
                c_id = self.id_from(results[1])
                self.lead_ids["C"] = c_id
                self.tool_calls(handler, [("task", {"action": "wait", "task_id": c_id,
                                                    "timeout_s": 60})])
            else:
                self.send(handler, {"role": "assistant", "content": "SMOKE DONE"}, finish="stop")
                self.finish(handler)
            return
        if n >= 4:
            self.kill_seen.set()  # the kill has come back to the lead
        if n == 0:
            calls = [("task", {"prompt": f"job {x}", "description": x, "background": True})
                     for x in SPAWN_NAMES]
            self.tool_calls(handler, calls)
        elif n == 3:
            c_id = self.id_from(results[2])
            self.lead_ids["C"] = c_id
            self.tool_calls(handler, [("task", {"action": "kill", "task_id": c_id})])
        elif n == 4:
            a_id = self.id_from(results[0])
            self.lead_ids["A"] = a_id
            self.tool_calls(handler, [("task", {"action": "wait", "task_id": a_id, "timeout_s": 60})])
        elif n == 5:
            self.tool_calls(handler, [("task", {"task_id": self.lead_ids["A"],
                                                "prompt": "follow up A"})])
        elif n == 6:
            self.tool_calls(handler, [("task", {"action": "status"})])
        else:
            self.send(handler, {"role": "assistant", "content": "SMOKE DONE"}, finish="stop")
            self.finish(handler)
            return
        self.finish(handler)

    def tool_calls(self, handler, calls: list[tuple[str, dict]]) -> None:
        for index, (name, args) in enumerate(calls):
            delta = {"tool_calls": [{"index": index, "id": f"call_{index}_{time.monotonic_ns()}",
                                     "type": "function",
                                     "function": {"name": name, "arguments": json.dumps(args)}}]}
            self.send(handler, delta)
        self.send(handler, {}, finish="tool_calls")
        self.finish(handler)

    @staticmethod
    def id_from(message: dict) -> str:
        found = ID_RE.search(str(message.get("content", "")))
        if not found:
            raise RuntimeError(f"no task id in tool result: {message.get('content')!r}")
        return found.group(1)


def get_json(base: str, path: str):
    with urllib.request.urlopen(base + path, timeout=20) as resp:
        return json.loads(resp.read() or b"null")


def fail(msg: str) -> None:
    print(f"FAIL: {msg}")
    sys.exit(1)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", required=True, type=Path)
    ap.add_argument("--startup-deadline", type=float, default=20.0)
    args = ap.parse_args()

    stub = Stub()
    stub.start()
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    with tempfile.TemporaryDirectory(prefix="qcode-smoke-") as tmp:
        home = Path(tmp) / "home"
        logs = Path(tmp) / "logs"
        home.mkdir()
        logs.mkdir()
        env = base_env(home, write_config(home, stub.port))
        proc = subprocess.Popen([str(args.binary), "serve", "--port", str(port), "--log-dir", str(logs)],
                                env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            wait_ready(base, proc, args.startup_deadline)
            body = json.dumps({"workspace": str(home), "provider": "openai", "model": MODEL,
                               "agent_mode": "orchestrator"}).encode()
            req = urllib.request.Request(base + "/sessions", data=body, method="POST")
            req.add_header("Content-Type", "application/json")
            with urllib.request.urlopen(req, timeout=20) as resp:
                sid = json.loads(resp.read())["id"]

            gen = json.dumps({"session_id": sid, "text": LEAD_PROMPT}).encode()
            req = urllib.request.Request(base + "/generate", data=gen, method="POST")
            req.add_header("Content-Type", "application/json")
            with urllib.request.urlopen(req, timeout=180) as resp:
                stream = resp.read().decode("utf-8", "replace")
            if "SMOKE DONE" not in stream:
                fail("the lead did not finish with SMOKE DONE")

            tasks = get_json(base, "/tasks?parent_session_id=" + urllib.parse.quote(sid))["metadata"]["tasks"]
            statuses = {t["task_id"]: t["status"] for t in tasks}
            counts = Counter(statuses.values())
            a_id, c_id = stub.lead_ids.get("A", ""), stub.lead_ids.get("C", "")
            if len(tasks) != 3:
                fail(f"expected 3 tasks, got {len(tasks)}: {statuses}")
            if statuses.get(a_id) != "done":
                rows = get_json(base, f"/session/{a_id}/messages")
                rows = rows if isinstance(rows, list) else rows.get("messages", [])
                fail(f"A should be done after its resume, got {statuses.get(a_id)!r}; "
                     f"A's rows: {[(m.get('sender', m.get('role')), str(m.get('content'))[:200]) for m in rows]}")
            if statuses.get(c_id) != "killed":
                fail(f"C should be killed, got {statuses.get(c_id)!r}")
            done_others = [s for tid, s in statuses.items() if tid not in (a_id, c_id)]
            if done_others != ["done"]:
                fail(f"B should be done, got {done_others}")
            if counts != Counter({"done": 2, "killed": 1}):
                fail(f"per-status counts wrong: {dict(counts)}")

            messages = get_json(base, f"/session/{c_id}/messages")
            rows = messages if isinstance(messages, list) else messages.get("messages", [])
            assistant = [m.get("content", "") for m in rows if m.get("sender", m.get("role")) == "Assistant"]
            if not any(text.startswith("[killed]") for text in assistant):
                fail(f"C's stored row lacks the killed marker: {assistant}")

            children = get_json(base, "/sessions?include_subagents=1&parent_session_id=" + urllib.parse.quote(sid))
            if sum(1 for s in children if s.get("parent_session_id") == sid) < 3:
                fail("the three child sessions are not listed under the lead")

            print("OK: lead spawned 3 subagents; C was killed; A was resumed and is done.")
            print(f"    statuses: {dict(counts)}; stub hits: {dict(stub.subagent_hits)}")
            return 0
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            stub.stop()


if __name__ == "__main__":
    sys.exit(main())
