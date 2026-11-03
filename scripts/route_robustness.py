#!/usr/bin/env python3
"""Robustness pass over the qcode serve HTTP routes.

Starts `qcode serve` against the stub provider from perf_guard.py and throws
hostile input at it:

  * malformed, empty, non-object, and wrongly typed JSON bodies on every route
    that reads a body,
  * oversized bodies,
  * unknown, malformed, and path-traversal session ids on every session route,
  * concurrent requests against one session.

A request that gets a 5xx, or a server that exits, is a finding. Client errors
(4xx) and 2xx are expected. Usage:

  scripts/route_robustness.py --binary build/rel/apps/qcode/qcode
"""

import argparse
import concurrent.futures
import json
import os
import socket
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from perf_guard import MODEL, StubProvider, base_env, free_port, wait_ready, write_config  # noqa: E402

BODY_BAD = [
    b"",
    b"{not json",
    b"[]",
    b"42",
    b"null",
    b'"just a string"',
    b'{"session_id": 5, "text": 7, "provider": [], "model": {}}',
    b'{"session_id": null, "workspace": 1.5, "title": ["x"]}',
    b"\xff\xfe\x00garbage",
]
OVERSIZE = b'{"text": "' + b"x" * (20 * 1024 * 1024) + b'"}'

# (method, path template). {sid} is replaced with each candidate session id.
ROUTES = [
    ("POST", "/generate"),
    ("POST", "/sessions"),
    ("POST", "/rename"),
    ("POST", "/session/cancel"),
    ("POST", "/api/models/feedback"),
    ("POST", "/session/{sid}/generate"),
    ("POST", "/session/{sid}/cancel"),
    ("POST", "/session/{sid}/abort"),
    ("POST", "/session/{sid}/mode"),
    ("POST", "/session/{sid}/clear"),
    ("POST", "/session/{sid}/compact"),
    ("PUT", "/session/{sid}/fs/write"),
    ("DELETE", "/session/{sid}"),
    ("DELETE", "/session/{sid}/queue"),
    ("GET", "/session/{sid}"),
    ("GET", "/session/{sid}/messages"),
    ("GET", "/session/{sid}/stats"),
    ("GET", "/session/{sid}/files"),
    ("GET", "/session/{sid}/fs/list?path=../../.."),
    ("GET", "/session/{sid}/fs/read?path=../../../etc/passwd"),
    ("GET", "/session/{sid}/fs/raw?path=%zz"),
    ("GET", "/session/{sid}/fs/exists?path=/etc/passwd"),
    ("GET", "/session/{sid}/logs"),
]

BAD_IDS = [
    "does-not-exist",
    "..%2F..%2Fetc",
    "%zz",
    "a" * 4096,
    "%00",
]


def send(base: str, method: str, path: str, body: bytes | None = None,
         content_type: str = "application/json") -> int:
    url = base + path
    req = urllib.request.Request(url, data=body, method=method)
    if body is not None:
        req.add_header("Content-Type", content_type)
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            resp.read()
            return resp.status
    except urllib.error.HTTPError as e:
        e.read()
        return e.code
    except (urllib.error.URLError, socket.timeout, ConnectionError) as e:
        return -1 if "refused" in str(e) or isinstance(e, ConnectionError) else -2


def make_session(base: str, workspace: str) -> str:
    body = json.dumps({"workspace": workspace, "provider": "openai", "model": MODEL}).encode()
    req = urllib.request.Request(base + "/sessions", data=body, method="POST")
    req.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(req, timeout=20) as resp:
        return json.loads(resp.read())["id"]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", required=True, type=Path)
    ap.add_argument("--concurrency", type=int, default=16)
    args = ap.parse_args()
    if not args.binary.exists():
        print(f"binary not found: {args.binary}", file=sys.stderr)
        return 2

    stub = StubProvider()
    stub.start()
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    findings: list[str] = []
    checked = 0

    def check(label: str, status: int) -> None:
        nonlocal checked
        checked += 1
        if status >= 500 or status == -1 or status == -2:
            findings.append(f"{label} -> {status}")

    with tempfile.TemporaryDirectory(prefix="qcode-robust-") as tmp:
        home = Path(tmp) / "home"
        logs = Path(tmp) / "logs"
        home.mkdir()
        logs.mkdir()
        env = base_env(home, write_config(home, stub.port))
        proc = subprocess.Popen([str(args.binary), "serve", "--port", str(port), "--log-dir", str(logs)],
                                env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            wait_ready(base, proc, 20.0)
            sid = make_session(base, str(home))

            # 1. Hostile bodies on every route that reads one.
            for method, template in ROUTES:
                for ident in [sid] + BAD_IDS:
                    path = template.replace("{sid}", urllib.parse.quote(ident, safe=""))
                    if "{sid}" in template and ident == sid and method == "GET":
                        pass
                    for body in BODY_BAD:
                        check(f"{method} {path[:60]} body={body[:20]!r}",
                              send(base, method, path, body))
                    check(f"{method} {path[:60]} no-body", send(base, method, path, None))

            # 2. Oversized body on the body-reading POST routes.
            for path in ["/generate", "/rename", f"/session/{sid}/mode", f"/session/{sid}/generate"]:
                check(f"POST {path} oversize", send(base, "POST", path, OVERSIZE))

            # 3. Concurrency on one session: mixed reads and writes at once.
            jobs = []
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.concurrency) as pool:
                for i in range(args.concurrency * 4):
                    kind = i % 4
                    if kind == 0:
                        jobs.append(pool.submit(send, base, "GET", f"/session/{sid}/messages"))
                    elif kind == 1:
                        jobs.append(pool.submit(send, base, "POST", f"/session/{sid}/mode",
                                                b'{"mode": "orchestrator"}'))
                    elif kind == 2:
                        jobs.append(pool.submit(send, base, "POST", f"/session/{sid}/clear", b"{}"))
                    else:
                        jobs.append(pool.submit(send, base, "POST", f"/session/{sid}/cancel", b"{}"))
                for i, fut in enumerate(jobs):
                    check(f"concurrent #{i}", fut.result())

            # 4. Concurrent generations on one session. Each must finish or
            #    be refused with 4xx; none may 5xx.
            gen_body = json.dumps({"session_id": sid, "text": "robustness"}).encode()
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.concurrency) as pool:
                futs = [pool.submit(send, base, "POST", "/generate", gen_body) for _ in range(args.concurrency)]
                for i, fut in enumerate(futs):
                    check(f"concurrent generate #{i}", fut.result())

            # 5. The server must still answer after all of the above.
            check("post-run /health", send(base, "GET", "/health"))
            if proc.poll() is not None:
                findings.append(f"server exited during the run with code {proc.returncode}")
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
            stub.stop()

    print(f"checked {checked} requests")
    if findings:
        for f in findings[:200]:
            print(f"FINDING: {f}", file=sys.stderr)
        if len(findings) > 200:
            print(f"... and {len(findings) - 200} more", file=sys.stderr)
        return 1
    print("route robustness: no 5xx, no crash")
    return 0


if __name__ == "__main__":
    sys.exit(main())
