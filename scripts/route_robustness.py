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
import os
import socket
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from perf_guard import MODEL, StubProvider, base_env, free_port, wait_ready, write_config  # noqa: E402

# The API key perf_guard writes into the generated config. It must never reach a
# response body or a log line.
SECRET = b"perf-guard-dummy"
SEEN_BODIES: list[bytes] = []

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


def request_raw(port: int, method: str, path: str, body: bytes | None = None,
                headers: dict | None = None) -> tuple[int, dict, bytes]:
    """One request. Returns (status, response headers, body). Status is -1 on
    refused/reset and -2 on timeout.

    The server may answer before a large upload is finished (it rejects the
    request early and closes the socket). That is a valid answer, so a send
    error is not fatal: the response is still read if it can be.
    """
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=30)
    hdrs = {}
    if body is not None:
        hdrs["Content-Type"] = "application/json"
    hdrs.update(headers or {})
    try:
        try:
            conn.request(method, path, body=body, headers=hdrs)
        except (BrokenPipeError, ConnectionResetError):
            pass  # answered early; fall through and read the response
        resp = conn.getresponse()
        data = resp.read()
        SEEN_BODIES.append(data[: 1 << 20])
        return resp.status, {k.lower(): v for k, v in resp.getheaders()}, data
    except ConnectionRefusedError:
        return -1, {}, b""
    except (socket.timeout, TimeoutError):
        return -2, {}, b""
    except (ConnectionError, http.client.HTTPException, OSError):
        return -1, {}, b""
    finally:
        conn.close()


def send(base: str, method: str, path: str, body: bytes | None = None) -> int:
    return request_raw(int(base.rsplit(":", 1)[1]), method, path, body)[0]


def security_checks(port: int, sid: str, home: Path, check, ensure) -> None:
    """Browser and network exposure: Host, Origin, CORS, static and symlink paths."""
    def req(method, path, body=None, headers=None):
        return request_raw(port, method, path, body, headers)

    rename_body = json.dumps({"session_id": sid, "title": "robustness"}).encode()
    evil = {"Origin": "http://evil.example"}

    # DNS rebinding: a name other than loopback must not reach the API.
    check("sec Host evil.example GET /health",
          req("GET", "/health", headers={"Host": "evil.example"})[0], 403)
    check("sec Host 127.0.0.1 GET /health",
          req("GET", "/health", headers={"Host": f"127.0.0.1:{port}"})[0], 200)

    # Cross-site browser requests: Origin must be loopback or the same host.
    check("sec Origin evil GET /health", req("GET", "/health", headers=evil)[0], 403)
    check("sec Origin evil GET session", req("GET", f"/session/{sid}", headers=evil)[0], 403)
    check("sec Origin evil POST /rename", req("POST", "/rename", rename_body, evil)[0], 403)
    check("sec Origin evil preflight",
          req("OPTIONS", "/rename", headers={**evil, "Access-Control-Request-Method": "POST"})[0], 403)
    st, h, _ = req("OPTIONS", "/rename", headers={"Origin": "http://localhost:5173",
                                                  "Access-Control-Request-Method": "POST"})
    check("sec preflight from localhost dev server", st, 204)
    ensure("sec preflight echoes only the allowed origin",
           h.get("access-control-allow-origin") == "http://localhost:5173")

    # Requests without Origin (curl, the CLI) are not cross-site and stay open.
    st, h, _ = req("GET", "/health")
    check("sec no Origin GET /health", st, 200)
    ensure("sec no wildcard CORS on same-origin responses",
           h.get("access-control-allow-origin") != "*")
    st, h, _ = req("GET", "/health", headers={"Origin": "http://localhost:5173"})
    check("sec localhost origin GET /health", st, 200)
    ensure("sec ACAO echoes the allowed origin, never *",
           h.get("access-control-allow-origin") == "http://localhost:5173")

    # Static paths cannot leave the web UI root.
    check("sec static encoded ../", req("GET", "/%2e%2e/%2e%2e/etc/passwd")[0], 404)
    check("sec static ..%2F", req("GET", "/..%2F..%2Fetc%2Fpasswd")[0], 404)

    # A symlink inside the workspace that points outside it must not be followed.
    os.symlink("/etc", home / "escape")
    check("sec symlink fs/read escape/passwd",
          req("GET", f"/session/{sid}/fs/read?path=escape/passwd")[0], 400)
    check("sec symlink fs/list escape",
          req("GET", f"/session/{sid}/fs/list?path=escape")[0], 400)
    check("sec symlink fs/write escape/probe",
          req("PUT", f"/session/{sid}/fs/write",
              json.dumps({"path": "escape/qcode-robust-probe", "content": "x"}).encode())[0], 400)
    ensure("sec symlink write did not reach /etc", not Path("/etc/qcode-robust-probe").exists())


def non_loopback_checks(binary: Path, stub, check, ensure) -> None:
    """Binding beyond loopback must need a token, and the token must gate the API."""
    token = "robust-token-123"
    with tempfile.TemporaryDirectory(prefix="qcode-robust-remote-") as tmp:
        home = Path(tmp) / "home"
        logs = Path(tmp) / "logs"
        home.mkdir()
        logs.mkdir()
        env = base_env(home, write_config(home, stub.port))
        env.pop("QCODE_SERVER_TOKEN", None)
        port = free_port()
        serve = [str(binary), "serve", "--host", "0.0.0.0", "--port", str(port), "--log-dir", str(logs)]

        p = subprocess.Popen(serve, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            _, err = p.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            p.kill()
            _, err = p.communicate()
        check("sec 0.0.0.0 without a token refuses to start", p.returncode, 2)
        ensure("sec refusal names QCODE_SERVER_TOKEN", b"QCODE_SERVER_TOKEN" in err)

        stdout_path = Path(tmp) / "stdout.txt"
        with open(stdout_path, "w") as out:
            proc = subprocess.Popen(serve, env={**env, "QCODE_SERVER_TOKEN": token},
                                    stdout=out, stderr=subprocess.DEVNULL)
        try:
            try:
                wait_ready(f"http://127.0.0.1:{port}", proc, 20.0)
            except RuntimeError as e:
                ensure(f"sec token run starts: {e}", False)
                return
            check("sec token: /health open", request_raw(port, "GET", "/health")[0], 200)
            check("sec token: /api/version without token",
                  request_raw(port, "GET", "/api/version")[0], 401)
            check("sec token: wrong token",
                  request_raw(port, "GET", "/api/version",
                              headers={"Authorization": "Bearer nope"})[0], 401)
            check("sec token: right token",
                  request_raw(port, "GET", "/api/version",
                              headers={"Authorization": f"Bearer {token}"})[0], 200)

            # Browser sign-in. A ?token= visit trades the token for an HttpOnly,
            # SameSite=Strict cookie and redirects without the token in the URL.
            st, h, _ = request_raw(port, "GET", f"/api/version?token={token}")
            check("sec login: ?token= visit redirects", st, 302)
            ensure("sec login: redirect drops the token from the URL",
                   h.get("location") == "/api/version")
            set_cookie = h.get("set-cookie", "")
            ensure("sec login: cookie is HttpOnly and SameSite=Strict",
                   "HttpOnly" in set_cookie and "SameSite=Strict" in set_cookie)
            cookie_value = set_cookie.split(";", 1)[0].partition("=")[2]
            ensure("sec login: cookie value is not the token", cookie_value not in ("", token))
            check("sec login: cookie authorizes the API",
                  request_raw(port, "GET", "/api/version",
                              headers={"Cookie": f"qcode_auth={cookie_value}"})[0], 200)
            check("sec login: wrong cookie refused",
                  request_raw(port, "GET", "/api/version",
                              headers={"Cookie": "qcode_auth=nope"})[0], 401)
            check("sec login: wrong ?token= refused",
                  request_raw(port, "GET", "/api/version?token=nope")[0], 401)
            check("sec login: /login page served without auth",
                  request_raw(port, "GET", "/login")[0], 200)
            ensure("sec login: page has the token form",
                   b'name="token"' in request_raw(port, "GET", "/login")[2])
            st, h, _ = request_raw(port, "GET", "/", headers={"Accept": "text/html"})
            check("sec login: browser without cookie goes to /login", st, 302)
            ensure("sec login: redirect target is /login", h.get("location") == "/login")
            form = {"Content-Type": "application/x-www-form-urlencoded"}
            check("sec login: form with wrong token refused",
                  request_raw(port, "POST", "/login", body=b"token=nope", headers=form)[0], 401)
            st, h, _ = request_raw(port, "POST", "/login", body=f"token={token}".encode(), headers=form)
            check("sec login: form with right token signs in", st, 303)
            ensure("sec login: form sign-in sets the cookie", "qcode_auth=" in h.get("set-cookie", ""))
            ensure("sec login: token is not in any response body",
                   not any(token.encode() in b for b in SEEN_BODIES))
        finally:
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=5)
        ensure("sec token run: printed a sign-in URL with the token",
               f"/?token={token}".encode() in stdout_path.read_bytes())
        ensure("sec token run: API key not in logs", not secret_in_dir(logs))
        ensure("sec token run: token not in logs", not secret_in_dir(logs, token.encode()))


def secret_in_dir(d: Path, needle: bytes = SECRET) -> bool:
    return any(f.is_file() and needle in f.read_bytes() for f in d.rglob("*"))


def secret_scan(ensure, log_dir: Path) -> None:
    """The API key must not appear in any response body or server log."""
    ensure("API key absent from every response body",
           not any(SECRET in b for b in SEEN_BODIES))
    ensure("API key absent from server logs", not secret_in_dir(log_dir))



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

    def ensure(label: str, cond: bool) -> None:
        results.append((label, int(cond), "true"))
        if not cond:
            findings.append(f"{label} -> FAILED")

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

            # 6. Security contract. Browsers and other hosts must not be able to
            #    drive the API, and paths must stay inside their roots.
            security_checks(port, sid, home, check, ensure)
            non_loopback_checks(args.binary, stub, check, ensure)

            # 7. The server must still answer after all of the above.
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
        secret_scan(ensure, logs)

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
