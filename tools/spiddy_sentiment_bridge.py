#!/usr/bin/env python3
"""Local sentiment endpoint for the Spiddy hero.

The WebUI picks Spiddy's animation from an 864-row catalog (pose x emotion x intensity x
motion). Its rule-based classifier always runs; this bridge lets a *small text LLM* refine
the label instead.

POST /classify  {"text": "...", "labels": [...], "intensities": [...]}
  -> {"emotion": "goofy", "intensity": "mid", "confidence": 0.8, "model": "...", "source": "llm"}

Backends
  mock         keyword rules, no dependencies (default when nothing else is installed)
  llama        llama-cpp-python with a small GGUF instruct model
  transformers zero-shot-classification pipeline (NLI model)

NOTE: Cactus-Compute/mirage is a *vision* model (image-text-to-text: detection, OCR,
segmentation). It cannot classify text sentiment, so it is not a backend here.

Usage
  python3 tools/spiddy_sentiment_bridge.py --backend mock --port 9101
  python3 tools/spiddy_sentiment_bridge.py --backend llama --model ./qwen2.5-0.5b-instruct-q4_k_m.gguf

Then in the WebUI console:
  qcodeSpiddy.setSentimentEndpoint('http://127.0.0.1:9101/classify')
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

EMOTIONS = ["happy", "surprised", "worried", "focused", "proud", "sad",
            "angry", "sleepy", "excited", "confused", "goofy", "dizzy"]
INTENSITIES = ["low", "mid", "high"]

PROMPT = (
    "You label the emotional tone of a coding assistant's message so an animated character "
    "can react. Choose exactly one emotion from: " + ", ".join(EMOTIONS) + ". "
    "Then choose an intensity from: low, mid, high. "
    "Answer with two words only, emotion then intensity. No punctuation, no explanation.\n"
    "Message: \"\"\"{text}\"\"\"\nAnswer:"
)

# Mock rules: ordered, tool outcome handled by the WebUI already.
MOCK_RULES = [
    ("angry", re.compile(r"\b(blocked|forbidden|denied|critical|furious|unacceptable)\b", re.I)),
    ("goofy", re.compile(r"\b(oops|whoops|my bad|silly|haha|lol)\b", re.I)),
    ("dizzy", re.compile(r"\b(timed? out|timeout|retrying|spinning)\b", re.I)),
    ("worried", re.compile(r"\b(sorry|fail(ed|ure|s)?|error|broken|bug|unable|crash|exception)\b", re.I)),
    ("sad", re.compile(r"\b(unfortunately|sadly|lost|missing|no results?|not found)\b", re.I)),
    ("focused", re.compile(r"\b(let me|checking|looking|reading|searching|analyz|investigat)\b", re.I)),
    ("confused", re.compile(r"\b(unclear|not sure|which one|confus|ambiguous)\b", re.I)),
    ("happy", re.compile(r"\b(done|fixed|works?|success|great|nice|perfect|passed|ready)\b", re.I)),
]


def intensity_for(text: str) -> str:
    if text.count("!") >= 2:
        return "high"
    if text.isupper() and len(text) > 8:
        return "high"
    if len(text.strip()) < 12:
        return "low"
    return "mid"


def mock_classify(text: str) -> dict:
    for emotion, rx in MOCK_RULES:
        if rx.search(text):
            return {"emotion": emotion, "intensity": intensity_for(text), "confidence": 0.5}
    return {"emotion": "focused", "intensity": "low", "confidence": 0.2}


class LlamaBackend:
    source = "llm"

    def __init__(self, model_path: str):
        from llama_cpp import Llama  # noqa: F401  (import error is the useful failure)
        self.llm = Llama(model_path=model_path, n_ctx=1024, verbose=False)
        self.model = model_path

    def classify(self, text: str) -> dict:
        out = self.llm(PROMPT.format(text=text[:2000]), max_tokens=6, temperature=0.0, stop=["\n"])
        raw = out["choices"][0]["text"]
        return parse_pair(raw)


class TransformersBackend:
    source = "llm"

    def __init__(self, model: str):
        from transformers import pipeline  # noqa: F401
        self.pipe = pipeline("zero-shot-classification", model=model)
        self.model = model

    def classify(self, text: str) -> dict:
        res = self.pipe(text[:2000], candidate_labels=EMOTIONS, multi_label=False)
        return {
            "emotion": res["labels"][0],
            "intensity": intensity_for(text),
            "confidence": round(float(res["scores"][0]), 3),
        }


def parse_pair(raw: str) -> dict:
    """Extract (emotion, intensity) from a model reply, tolerating decorations."""
    tokens = re.findall(r"[a-z]+", (raw or "").lower())
    emotion = next((t for t in tokens if t in EMOTIONS), None)
    if not emotion:
        return {"emotion": "focused", "intensity": "mid", "confidence": 0.0}
    intensity = next((t for t in tokens if t in INTENSITIES), "mid")
    return {"emotion": emotion, "intensity": intensity, "confidence": 0.8}


def build_backend(name: str, model: str | None):
    if name in ("auto", "llama"):
        try:
            return LlamaBackend(model or ""), "llama"
        except Exception as exc:  # missing package, missing model, bad file
            if name == "llama":
                raise SystemExit(f"llama backend unavailable: {exc}")
    if name in ("auto", "transformers"):
        try:
            return TransformersBackend(model or "cross-encoder/nli-deberta-v3-xsmall"), "transformers"
        except Exception as exc:
            if name == "transformers":
                raise SystemExit(f"transformers backend unavailable: {exc}")
    print("no model backend available; using keyword mock (wiring test only)", file=sys.stderr)
    return None, "mock"


def make_handler(backend, backend_name: str):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def _cors(self):
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")
            self.send_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS")

        def _json(self, status: int, payload: dict):
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
                self._json(200, {"ok": True, "backend": backend_name,
                                 "model": getattr(backend, "model", None)})
            else:
                self._json(404, {"error": "not found"})

        def do_POST(self):
            if self.path not in ("/classify", "/v1/classify"):
                self._json(404, {"error": "not found"})
                return
            try:
                length = int(self.headers.get("Content-Length") or 0)
                payload = json.loads(self.rfile.read(length) or b"{}")
            except Exception:
                self._json(400, {"error": "invalid json"})
                return
            text = str(payload.get("text") or "")[:4000]
            if not text.strip():
                self._json(400, {"error": "text is required"})
                return
            try:
                result = backend.classify(text) if backend else mock_classify(text)
            except Exception as exc:
                self._json(500, {"error": str(exc)})
                return
            if result.get("emotion") not in EMOTIONS:
                result["emotion"] = "focused"
            if result.get("intensity") not in INTENSITIES:
                result["intensity"] = "mid"
            result.setdefault("model", getattr(backend, "model", "mock"))
            result["source"] = "llm" if backend else "mock"
            self._json(200, result)

        def log_message(self, fmt, *args):
            sys.stderr.write("[spiddy] " + (fmt % args) + "\n")

    return Handler


def main() -> int:
    ap = argparse.ArgumentParser(description="Sentiment endpoint for the Spiddy hero")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9101)
    ap.add_argument("--backend", choices=["auto", "llama", "transformers", "mock"], default="auto")
    ap.add_argument("--model", default=None, help="GGUF path (llama) or HF id (transformers)")
    args = ap.parse_args()

    backend, name = build_backend(args.backend, args.model)
    server = ThreadingHTTPServer((args.host, args.port), make_handler(backend, name))
    print(f"spiddy sentiment bridge on http://{args.host}:{args.port}/classify (backend={name})",
          file=sys.stderr)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
