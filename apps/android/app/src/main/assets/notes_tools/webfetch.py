#!/usr/bin/env python3
"""Fetch a URL and print readable text (stdlib only — works on Android QCode).

Usage:
  webfetch.py <url> [--max-chars N] [--json]

Prints extracted text to stdout. Use for docs, articles, and reference pages.
"""
from __future__ import annotations

import argparse
import json
import re
import ssl
import sys
import urllib.error
import urllib.request
from html.parser import HTMLParser
from typing import List
from urllib.parse import urlparse


DEFAULT_MAX_CHARS = 50_000
USER_AGENT = (
    "Mozilla/5.0 (compatible; QCodeWebFetch/1.0; +https://github.com/qcode)"
)


class _TextExtractor(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self._skip_depth = 0
        self._parts: List[str] = []
        self.title = ""
        self._in_title = False

    def handle_starttag(self, tag: str, attrs) -> None:  # type: ignore[override]
        t = tag.lower()
        if t in {"script", "style", "noscript", "svg", "iframe"}:
            self._skip_depth += 1
            return
        if self._skip_depth:
            return
        if t == "title":
            self._in_title = True
        if t in {"p", "div", "br", "li", "tr", "h1", "h2", "h3", "h4", "h5", "h6"}:
            self._parts.append("\n")

    def handle_endtag(self, tag: str) -> None:  # type: ignore[override]
        t = tag.lower()
        if t in {"script", "style", "noscript", "svg", "iframe"} and self._skip_depth:
            self._skip_depth -= 1
            return
        if self._skip_depth:
            return
        if t == "title":
            self._in_title = False

    def handle_data(self, data: str) -> None:  # type: ignore[override]
        if self._skip_depth:
            return
        text = data.strip()
        if not text:
            return
        if self._in_title:
            self.title += text + " "
            return
        self._parts.append(text + " ")

    def text(self) -> str:
        raw = "".join(self._parts)
        raw = re.sub(r"[ \t]+", " ", raw)
        raw = re.sub(r"\n{3,}", "\n\n", raw)
        return raw.strip()


def _ssl_context() -> ssl.SSLContext:
    ctx = ssl.create_default_context()
    return ctx


def fetch_bytes(url: str, timeout: float = 30.0) -> tuple[bytes, str]:
    parsed = urlparse(url)
    if parsed.scheme not in {"http", "https"}:
        raise SystemExit(f"unsupported scheme: {parsed.scheme!r}")
    req = urllib.request.Request(
        url,
        headers={
            "User-Agent": USER_AGENT,
            "Accept": "text/html,application/xhtml+xml,text/plain,application/json;q=0.9,*/*;q=0.8",
        },
        method="GET",
    )
    with urllib.request.urlopen(req, timeout=timeout, context=_ssl_context()) as resp:
        ctype = (resp.headers.get("Content-Type") or "").split(";")[0].strip().lower()
        data = resp.read(2_000_000)  # hard cap 2MB
        return data, ctype


def extract_text(data: bytes, content_type: str, url: str) -> dict:
    charset = "utf-8"
    try:
        text = data.decode(charset, errors="replace")
    except Exception:
        text = data.decode("latin-1", errors="replace")

    if content_type.startswith("text/plain") or content_type in {
        "application/json",
        "application/javascript",
        "text/css",
        "text/markdown",
        "text/x-markdown",
    }:
        return {"url": url, "title": "", "content_type": content_type, "text": text}

    if "html" in content_type or text.lstrip().lower().startswith("<!doctype html") or "<html" in text[:500].lower():
        parser = _TextExtractor()
        try:
            parser.feed(text)
            parser.close()
        except Exception:
            pass
        return {
            "url": url,
            "title": parser.title.strip(),
            "content_type": content_type or "text/html",
            "text": parser.text(),
        }

    return {"url": url, "title": "", "content_type": content_type, "text": text}


def main() -> None:
    ap = argparse.ArgumentParser(description="Fetch URL as readable text")
    ap.add_argument("url")
    ap.add_argument("--max-chars", type=int, default=DEFAULT_MAX_CHARS)
    ap.add_argument("--json", action="store_true", help="Emit JSON instead of plain text")
    ap.add_argument("--timeout", type=float, default=30.0)
    args = ap.parse_args()

    try:
        data, ctype = fetch_bytes(args.url, timeout=args.timeout)
    except urllib.error.HTTPError as e:
        print(f"HTTP {e.code}: {e.reason}", file=sys.stderr)
        raise SystemExit(1) from e
    except Exception as e:
        print(f"fetch failed: {e}", file=sys.stderr)
        raise SystemExit(1) from e

    out = extract_text(data, ctype, args.url)
    body = out["text"]
    if args.max_chars > 0 and len(body) > args.max_chars:
        body = body[: args.max_chars] + "\n\n[truncated]"
        out["text"] = body
        out["truncated"] = True

    if args.json:
        print(json.dumps(out, ensure_ascii=False))
    else:
        if out.get("title"):
            print(f"# {out['title']}\n")
        print(f"Source: {out['url']}\n")
        print(body)


if __name__ == "__main__":
    main()
