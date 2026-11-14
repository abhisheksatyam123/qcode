#!/usr/bin/env python3
"""Local Cactus Mirage endpoint for the Spiddy hero (vision).

Mirage is the Cactus vision model: scene understanding, regions, OCR text, relations.
It does NOT do text sentiment (see docs/cactus-spiddy.md); that stays with the rule
classifier or the text bridge.

Start it with the Cactus runtime installed (see docs/cactus-spiddy.md):

    python3 tools/cactus_mirage_bridge.py --port 9102 --venv ~/tmp/cactus/venv

POST /describe
  {"image": "data:image/png;base64,..."  |  "<base64>", "prompt": "optional"}
  -> {"ok": true, "model": "mirage", "took_ms": 11023,
      "summary": "computer screen · dark gray · window, arrow · text: ...",
      "result": {scene, objects, regions, text, relations, events, reasoning}}

GET /health -> {"ok": true, "mirage": "ready"|"cold", "device": ...}
"""
from __future__ import annotations

import argparse
import base64
import binascii
import json
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

MAX_BYTES = 8 * 1024 * 1024
_model = None


def load_model():
    """Import cactus lazily so /health works even without the runtime."""
    global _model
    if _model is None:
        import cactus  # noqa: F401
        _model = cactus.Mirage()
    return _model


def decode_image(value: str) -> bytes:
    if not isinstance(value, str) or not value:
        raise ValueError("image is required")
    match = re.match(r"^data:image/[a-z0-9.+-]+;base64,(.*)$", value, re.I | re.S)
    payload = match.group(1) if match else value
    try:
        data = base64.b64decode(payload, validate=True)
    except (binascii.Error, ValueError) as exc:
        raise ValueError("image must be base64 or a data URL") from exc
    if not data:
        raise ValueError("image is empty")
    if len(data) > MAX_BYTES:
        raise ValueError(f"image is larger than {MAX_BYTES // (1024 * 1024)} MB")
    return data


def _clip(text, limit=160):
    text = " ".join(str(text or "").split())
    return text if len(text) <= limit else text[: limit - 1] + "…"


def summarize(result) -> str:
    """One readable line for the chat UI. Keeps the important bits, drops boxes/masks."""
    if isinstance(result, str):
        return _clip(result)
    if not isinstance(result, dict):
        return ""
    parts = []
    scene = result.get("scene") or {}
    if isinstance(scene, dict):
        for key in ("setting", "background", "style"):
            if scene.get(key):
                parts.append(str(scene[key]))
    regions = result.get("regions") or []
    if isinstance(regions, list):
        names = [str(r.get("name")) for r in regions[:5] if isinstance(r, dict) and r.get("name")]
        if names:
            parts.append(", ".join(names))
    texts = result.get("text") or []
    if isinstance(texts, list):
        strings = [str(t.get("string")) for t in texts[:3] if isinstance(t, dict) and t.get("string")]
        if strings:
            parts.append("text: " + " / ".join(_clip(s, 60) for s in strings))
    reasoning = result.get("reasoning")
    if reasoning:
        parts.append(_clip(reasoning, 220))
    return " · ".join(parts) or _clip(json.dumps(result, ensure_ascii=False), 220)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "spiddy-mirage/1.0"

    def _cors(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS")

    def _json(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self._cors()
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(204)
        self._cors()
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self):
        if self.path in ("/health", "/"):
            self._json(200, {"ok": True, "mirage": "ready" if _model is not None else "cold",
                             "model": "Cactus-Compute/mirage"})
        else:
            self._json(404, {"error": "not found"})

    def do_POST(self):
        if self.path not in ("/describe", "/v1/describe"):
            self._json(404, {"error": "not found"})
            return
        try:
            length = int(self.headers.get("Content-Length") or 0)
            if length > MAX_BYTES * 2:
                self._json(413, {"ok": False, "error": "request too large"})
                return
            payload = json.loads(self.rfile.read(length) or b"{}")
        except Exception:
            self._json(400, {"ok": False, "error": "invalid json"})
            return
        try:
            image = decode_image(payload.get("image"))
        except ValueError as exc:
            self._json(400, {"ok": False, "error": str(exc)})
            return
        try:
            started = time.time()
            result = load_model().describe(image)
            took_ms = int((time.time() - started) * 1000)
            self._json(200, {
                "ok": True,
                "model": "mirage",
                "took_ms": took_ms,
                "summary": summarize(result),
                "result": result,
            })
        except Exception as exc:  # model/runtime failure: report, never crash the server
            self._json(500, {"ok": False, "error": str(exc)})

    def log_message(self, fmt, *args):
        sys.stderr.write("[mirage] " + (fmt % args) + "\n")


def main() -> int:
    ap = argparse.ArgumentParser(description="Cactus Mirage bridge for Spiddy")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9102)
    ap.add_argument("--warm", action="store_true", help="load the model before serving")
    args = ap.parse_args()

    if args.warm:
        print("loading Mirage…", file=sys.stderr)
        load_model()
        print("Mirage ready", file=sys.stderr)

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"mirage bridge on http://{args.host}:{args.port}/describe", file=sys.stderr)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
