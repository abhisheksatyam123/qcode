#!/usr/bin/env python3
"""Search the web and print results (stdlib only — works on Android QCode).

Usage:
  websearch.py <query> [--num N] [--json]

Backends (first non-empty wins):
  1. DuckDuckGo Lite (HTML form POST)
  2. Wikipedia OpenSearch
  3. DuckDuckGo Instant Answer JSON

Optional env:
  WEBSEARCH_BACKEND=lite|wiki|ddg   force a backend
"""
from __future__ import annotations

import argparse
import json
import os
import re
import ssl
import sys
import urllib.error
import urllib.parse
import urllib.request
from html.parser import HTMLParser
from typing import Any, Dict, List


USER_AGENT = (
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36"
)
MAX_RESULTS = 10


def _ssl_context() -> ssl.SSLContext:
    return ssl.create_default_context()


def _request(
    url: str,
    *,
    data: bytes | None = None,
    timeout: float = 25.0,
    accept: str = "text/html,application/json;q=0.9,*/*;q=0.8",
) -> bytes:
    headers = {
        "User-Agent": USER_AGENT,
        "Accept": accept,
    }
    if data is not None:
        headers["Content-Type"] = "application/x-www-form-urlencoded"
    req = urllib.request.Request(url, data=data, headers=headers, method="POST" if data else "GET")
    with urllib.request.urlopen(req, timeout=timeout, context=_ssl_context()) as resp:
        return resp.read(1_500_000)


class _LiteParser(HTMLParser):
    """Parse DuckDuckGo Lite result rows (result-link / result-snippet)."""

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.results: List[Dict[str, str]] = []
        self._in_link = False
        self._in_snippet = False
        self._cur: Dict[str, str] = {}
        self._buf: List[str] = []

    def handle_starttag(self, tag: str, attrs) -> None:  # type: ignore[override]
        attrs_d = dict(attrs)
        cls = attrs_d.get("class", "")
        if tag == "a" and "result-link" in cls.split():
            self._in_link = True
            self._buf = []
            self._cur = {"title": "", "url": attrs_d.get("href") or "", "snippet": ""}
            return
        if tag == "td" and "result-snippet" in cls.split():
            self._in_snippet = True
            self._buf = []

    def handle_endtag(self, tag: str) -> None:  # type: ignore[override]
        if self._in_link and tag == "a":
            self._cur["title"] = "".join(self._buf).strip()
            self._in_link = False
            self._buf = []
            if self._cur.get("url") and self._cur.get("title"):
                self.results.append(dict(self._cur))
            return
        if self._in_snippet and tag == "td":
            snip = "".join(self._buf).strip()
            self._in_snippet = False
            self._buf = []
            if self.results and not self.results[-1].get("snippet"):
                self.results[-1]["snippet"] = snip

    def handle_data(self, data: str) -> None:  # type: ignore[override]
        if self._in_link or self._in_snippet:
            self._buf.append(data)


def search_ddg_lite(query: str, num: int) -> List[Dict[str, Any]]:
    body = urllib.parse.urlencode({"q": query}).encode()
    raw = _request("https://lite.duckduckgo.com/lite/", data=body)
    html = raw.decode("utf-8", errors="replace")
    parser = _LiteParser()
    try:
        parser.feed(html)
        parser.close()
    except Exception:
        pass
    out: List[Dict[str, Any]] = []
    for r in parser.results:
        if len(out) >= num:
            break
        url = (r.get("url") or "").strip()
        title = (r.get("title") or "").strip()
        if not url.startswith("http"):
            continue
        # Skip DDG ad redirectors / help chrome.
        if "duckduckgo.com/y.js" in url or "/duckduckgo-help-pages/" in url:
            continue
        if title.lower() in {"more info", "ad", "ads"}:
            continue
        out.append(
            {
                "title": title or url,
                "url": url,
                "snippet": r.get("snippet") or "",
            }
        )
    return out


def search_wikipedia(query: str, num: int) -> List[Dict[str, Any]]:
    params = urllib.parse.urlencode(
        {
            "action": "opensearch",
            "search": query,
            "limit": str(num),
            "namespace": "0",
            "format": "json",
        }
    )
    raw = _request(
        f"https://en.wikipedia.org/w/api.php?{params}",
        accept="application/json",
    )
    data = json.loads(raw.decode("utf-8", errors="replace"))
    # [query, titles[], descriptions[], urls[]]
    if not isinstance(data, list) or len(data) < 4:
        return []
    titles, descs, urls = data[1], data[2], data[3]
    out: List[Dict[str, Any]] = []
    for i, title in enumerate(titles):
        if len(out) >= num:
            break
        url = urls[i] if i < len(urls) else ""
        if not url:
            continue
        out.append(
            {
                "title": title,
                "url": url,
                "snippet": descs[i] if i < len(descs) else "",
            }
        )
    return out


def search_ddg_instant(query: str, num: int) -> List[Dict[str, Any]]:
    params = urllib.parse.urlencode(
        {
            "q": query,
            "format": "json",
            "no_html": "1",
            "skip_disambig": "1",
        }
    )
    raw = _request(f"https://api.duckduckgo.com/?{params}", accept="application/json")
    data = json.loads(raw.decode("utf-8", errors="replace"))
    out: List[Dict[str, Any]] = []

    def add(title: str, url: str, snippet: str) -> None:
        if not url or len(out) >= num:
            return
        out.append(
            {
                "title": (title or url).strip(),
                "url": url.strip(),
                "snippet": re.sub(r"<.*?>", "", snippet or "").strip(),
            }
        )

    if data.get("AbstractURL"):
        add(
            data.get("Heading") or data.get("AbstractSource") or "",
            data["AbstractURL"],
            data.get("Abstract") or "",
        )
    for topic in data.get("RelatedTopics") or []:
        if len(out) >= num:
            break
        if isinstance(topic, dict) and topic.get("FirstURL"):
            add(topic.get("Text") or "", topic["FirstURL"], topic.get("Text") or "")
        elif isinstance(topic, dict):
            for sub in topic.get("Topics") or []:
                if isinstance(sub, dict) and sub.get("FirstURL"):
                    add(sub.get("Text") or "", sub["FirstURL"], sub.get("Text") or "")
                if len(out) >= num:
                    break
    return out


def search(query: str, num: int) -> List[Dict[str, Any]]:
    backend = (os.environ.get("WEBSEARCH_BACKEND") or "").strip().lower()
    order = ["lite", "wiki", "ddg"]
    if backend in {"lite", "wiki", "ddg"}:
        order = [backend]
    errors: List[str] = []
    for name in order:
        try:
            if name == "lite":
                hits = search_ddg_lite(query, num)
            elif name == "wiki":
                hits = search_wikipedia(query, num)
            else:
                hits = search_ddg_instant(query, num)
            if hits:
                return hits
        except Exception as e:
            errors.append(f"{name}: {e}")
    if errors:
        print("websearch backends failed:\n- " + "\n- ".join(errors), file=sys.stderr)
    return []


def main() -> None:
    ap = argparse.ArgumentParser(description="Search the web")
    ap.add_argument("query", nargs="+")
    ap.add_argument("--num", type=int, default=5)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()
    query = " ".join(args.query).strip()
    if not query:
        print("usage: websearch.py <query> [--num N]", file=sys.stderr)
        raise SystemExit(2)
    num = max(1, min(args.num, MAX_RESULTS))
    hits = search(query, num)
    payload = {"query": query, "results": hits}
    if args.json:
        print(json.dumps(payload, ensure_ascii=False, indent=2))
        return
    if not hits:
        print("No search results found. Try a different query.")
        raise SystemExit(1)
    print(f"Query: {query}\n")
    for i, hit in enumerate(hits, 1):
        print(f"{i}. {hit['title']}")
        print(f"   {hit['url']}")
        if hit.get("snippet"):
            snip = re.sub(r"\s+", " ", hit["snippet"]).strip()
            print(f"   {snip}")
        print()


if __name__ == "__main__":
    main()
