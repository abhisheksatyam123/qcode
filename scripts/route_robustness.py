#!/usr/bin/env python3
"""Robustness pass over the qcode serve HTTP routes, with exact expectations.

Starts `qcode serve` against the stub provider from perf_guard.py and sends
hostile and valid requests to every route. Each request has an exact expected
status (see EXPECT below); any other status is a finding. In particular:

  * a malformed session id (bad characters, traversal, too long) is 400,
  * an unknown session id is 404, whatever the body,
  * a body that is not a JSON object, or has a wrong-typed field, is 400
    before any work starts,
  * a 5xx, a dropped connection, or a crashed server is always a finding.

Usage:
  scripts/route_robustness.py --binary build/rel/apps/qcode/qcode
"""

import argparse
import concurrent.futures
import http.client
import json
import socket
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from perf_guard import MODEL, StubProvider, base_env, free_port, wait_ready, write_config  # noqa: E402

OVERSIZE = b'{"text": "' + b"x" * (20 * 1024 * 1024) + b'"}'

# Body classes, in the order the matrix sends them. Each name is a key in EXPECT.
BODY_CASES = [
    ("empty", b""),
    ("malformed", b"{not json"),
    ("array", b"[]"),
    ("number", b"42"),
    ("null", b"null"),
    ("string", b'"just a string"'),
    ("wrong-types", b'{"session_id": 5, "text": 7, "provider": [], "model": {}}'),
    ("null-fields", b'{"session_id": null, "workspace": 1.5, "title": ["x"]}'),
    ("binary", b"\xff\xfe\x00garbage"),
    ("no-body", None),
]

# Session ids: (class, raw id as put in the path). "malformed" must be 400 and
# "unknown" must be 404 on every session route, whatever the method or body.
ID_CASES = [
    ("malformed", "..%2F..%2Fetc"),
    ("malformed", "%zz"),
    ("malformed", "%00"),
    ("malformed", "a" * 4096),
    ("unknown", "does-not-exist"),
]

# (method, path template). {sid} is replaced with a session id.
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

# Expected status for a valid, existing session, per route and body class.
# A plain int applies to every body. A dict lists the exceptions; a missing
# name falls back to "default". Each entry says why it is that code.
EXPECT_VALID = {
    # Routes that ignore the body.
    ("GET", "/session/{sid}"): 200,
    ("GET", "/session/{sid}/messages"): 200,
    ("GET", "/session/{sid}/stats"): 200,
    ("GET", "/session/{sid}/files"): 200,
    ("GET", "/session/{sid}/logs"): 200,
    ("POST", "/session/{sid}/cancel"): 200,      # idle session: nothing to stop
    ("POST", "/session/{sid}/abort"): 200,
    ("POST", "/session/{sid}/clear"): 200,
    ("DELETE", "/session/{sid}"): 200,           # body ignored; see DELETE below
    # Filesystem routes: traversal and absolute paths are 400, a missing file 404.
    ("GET", "/session/{sid}/fs/list?path=../../.."): 400,
    ("GET", "/session/{sid}/fs/read?path=../../../etc/passwd"): 400,
    ("GET", "/session/{sid}/fs/exists?path=/etc/passwd"): 400,
    ("GET", "/session/{sid}/fs/raw?path=%zz"): 404,
    ("PUT", "/session/{sid}/fs/write"): 400,     # no path given
    # Body-reading routes: an invalid body is 400.
    ("POST", "/session/{sid}/generate"): 400,    # no text given
    ("POST", "/session/{sid}/compact"): 400,     # a fresh session has too little history
    ("POST", "/session/{sid}/mode"): {"default": 400, "wrong-types": 200, "null-fields": 200},
    ("DELETE", "/session/{sid}/queue"): {"default": 400, "empty": 200, "null-fields": 200, "no-body": 200},
}

# Expected status for the routes that take no session id in the path.
EXPECT_NO_SID = {
    ("POST", "/generate"): 400,                  # session_id required
    ("POST", "/sessions"): 400,                  # provider/model/workspace checked
    ("POST", "/rename"): 400,                    # session_id required
    ("POST", "/session/cancel"): 400,            # session_id required
    ("POST", "/api/models/feedback"): 400,       # model_id required
}


def expected_valid(method: str, template: str, body_name: str) -> int:
    rule = EXPECT_VALID[(method, template)]
    if isinstance(rule, int):
        return rule
    return rule.get(body_name, rule["default"])


def send(base: str, method: str, path: str, body: bytes | None = None,
         content_type: str = "application/json") -> int:
    """Returns the HTTP status, or -1 (refused / reset) or -2 (timeout).

    The server may answer before a large upload is finished (it rejects the
    request early and closes the socket). That is a valid answer, so a send
    error is not fatal: the response is still read if it can be.
    """
    host, port = base.rsplit("//", 1)[1].split(":")
    conn = http.client.HTTPConnection(host, int(port), timeout=30)
    headers = {}
    if body is not None:
        headers["Content-Type"] = content_type
    try:
        try:
            conn.request(method, path, body=body, headers=headers)
        except (BrokenPipeError, ConnectionResetError):
            pass  # answered early; fall through and read the response
        resp = conn.getresponse()
        resp.read()
        return resp.status
    except ConnectionRefusedError:
        return -1
    except (socket.timeout, TimeoutError):
        return -2
    except (ConnectionError, http.client.HTTPException, OSError):
        return -1
    finally:
        conn.close()


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
    ap.add_argument("--dump", type=Path, help="write every (label, status, expected) triple as JSON")
    args = ap.parse_args()
    if not args.binary.exists():
        print(f"binary not found: {args.binary}", file=sys.stderr)
        return 2

    stub = StubProvider()
    stub.start()
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    findings: list[str] = []
    results: list[tuple[str, int, str]] = []

    def check(label: str, status: int, expected) -> None:
        """expected is an int, or a set of ints when timing decides the answer."""
        ok_set = {expected} if isinstance(expected, int) else set(expected)
        results.append((label, status, str(expected)))
        if status not in ok_set:
            findings.append(f"{label} -> {status}, expected {expected}")

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

            # 1. Every route, every id class, every body class. Each route gets
            #    its own session so one route's side effects (DELETE) cannot hide
            #    another route's answer. DELETE gets a fresh session per request
            #    because its first call removes the session.
            for method, template in ROUTES:
                for id_class, raw_id in ([("valid", None)] + ID_CASES if "{sid}" in template else [("-", None)]):
                    for body_name, body in BODY_CASES:
                        if "{sid}" not in template:
                            path = template
                            expect = EXPECT_NO_SID[(method, template)]
                        else:
                            if id_class == "valid":
                                ident = make_session(base, str(home)) if method == "DELETE" else sid
                                expect = expected_valid(method, template, body_name)
                            else:
                                ident = raw_id
                                expect = 400 if id_class == "malformed" else 404
                            path = template.replace("{sid}", urllib.parse.quote(ident, safe=""))
                            if id_class == "valid" and method == "DELETE":
                                path = template.replace("{sid}", ident)
                        status = send(base, method, path, body)
                        check(f"{method} {path[:60]} id={id_class} body={body_name}", status, expect)

            # 2. Oversized bodies (20 MB). The body is valid JSON, so the answer
            #    depends on whether the route has what it needs.
            check("POST /generate oversize", send(base, "POST", "/generate", OVERSIZE), 400)
            check("POST /rename oversize", send(base, "POST", "/rename", OVERSIZE), 400)
            check("POST /session/{sid}/mode oversize",
                  send(base, "POST", f"/session/{sid}/mode", OVERSIZE), 200)
            # Accepted: the text is a valid prompt. This starts a turn, which the
            # concurrency step below then runs into.
            check("POST /session/{sid}/generate oversize",
                  send(base, "POST", f"/session/{sid}/generate", OVERSIZE), 200)

            # 3. Concurrency on one session: mixed reads and writes at once.
            jobs = []
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.concurrency) as pool:
                for i in range(args.concurrency * 4):
                    kind = i % 4
                    if kind == 0:
                        jobs.append((200, pool.submit(send, base, "GET", f"/session/{sid}/messages")))
                    elif kind == 1:
                        jobs.append((200, pool.submit(send, base, "POST", f"/session/{sid}/mode",
                                                      b'{"agent_mode": "normal"}')))
                    elif kind == 2:
                        jobs.append((200, pool.submit(send, base, "POST", f"/session/{sid}/clear", b"{}")))
                    else:
                        jobs.append((200, pool.submit(send, base, "POST", f"/session/{sid}/cancel", b"{}")))
                for i, (expect, fut) in enumerate(jobs):
                    check(f"concurrent #{i}", fut.result(), expect)

            # 4. Concurrent generations on one session. Each one either starts a
            #    turn (200), is steered into the running turn (202), or is refused
            #    while a stopped turn is still ending (409). Nothing else is allowed.
            gen_body = json.dumps({"session_id": sid, "text": "robustness"}).encode()
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.concurrency) as pool:
                futs = [pool.submit(send, base, "POST", "/generate", gen_body) for _ in range(args.concurrency)]
                for i, fut in enumerate(futs):
                    check(f"concurrent generate #{i}", fut.result(), {200, 202, 409})

            # 5. Positive controls: valid requests get their exact success code, so
            #    a 400 that is too eager cannot pass silently.
            check("control GET /session/{sid}", send(base, "GET", f"/session/{sid}"), 200)
            check("control POST /session/{sid}/cancel no body",
                  send(base, "POST", f"/session/{sid}/cancel"), 200)
            check("control POST /rename valid",
                  send(base, "POST", "/rename",
                       json.dumps({"session_id": sid, "title": "robustness"}).encode()), 200)
            check("control POST /api/models/feedback valid",
                  send(base, "POST", "/api/models/feedback",
                       json.dumps({"model_id": MODEL, "rating": "up"}).encode()), 200)
            victim = make_session(base, str(home))
            check("control DELETE /session/{sid}", send(base, "DELETE", f"/session/{victim}"), 200)
            check("control GET deleted session -> 404", send(base, "GET", f"/session/{victim}"), 404)

            # 6. The server must still answer after all of the above.
            check("post-run /health", send(base, "GET", "/health"), 200)
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

    if args.dump:
        args.dump.write_text(json.dumps(results, indent=1), encoding="utf-8")
    print(f"checked {len(results)} requests")
    if findings:
        for f in findings[:200]:
            print(f"FINDING: {f}", file=sys.stderr)
        if len(findings) > 200:
            print(f"... and {len(findings) - 200} more", file=sys.stderr)
        return 1
    print("route robustness: every request got its expected status")
    return 0


if __name__ == "__main__":
    sys.exit(main())
