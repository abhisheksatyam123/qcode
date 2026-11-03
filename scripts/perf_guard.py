#!/usr/bin/env python3
"""Headless perf guard for qcode.

Runs against an isolated HOME with a local stub OpenAI-compatible provider, so
no network is needed and generation is real end to end:

  serve phase
    * startup: ms from spawn until GET /health answers,
    * idle RSS after startup settles,
    * N generation turns: POST /sessions, then POST /generate streamed to the
      stub. Each turn must reach the stub and produce the canned marker.
    * loaded RSS after the turns.

  tui phase (--tui, Linux only)
    * `qcode tui` under a pseudo-terminal: ms to first output, and RSS after it
      has idled.

The guard fails on any 5xx, on a turn that never reaches the stub, or when a
gated metric exceeds the baseline by --tolerance (default 1.5x). Metrics are
machine-specific, so regenerate the baseline on the box that runs the guard:

  scripts/perf_guard.py --binary build/rel/apps/qcode/qcode --tui --write-baseline scripts/perf_baseline.json
  scripts/perf_guard.py --binary build/rel/apps/qcode/qcode --tui --baseline scripts/perf_baseline.json
"""

import argparse
import fcntl
import json
import os
import pty
import select
import socket
import struct
import subprocess
import sys
import tempfile
import termios
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

MARKER = "PERF_GUARD_OK"
CANNED_CHUNKS = [f"{MARKER} token{i} " for i in range(24)]
MODEL = "gpt-4o-mini"


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def rss_mb(pid: int) -> float:
    try:
        with open(f"/proc/{pid}/status", encoding="utf-8") as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1]) / 1024.0
    except OSError:
        pass
    raise RuntimeError(f"cannot read RSS for pid {pid} (Linux /proc required)")


class StubProvider:
    """Serves canned streamed completions on /v1/chat/completions and /v1/responses."""

    def __init__(self) -> None:
        self.hits = {"chat": 0, "responses": 0}
        self._lock = threading.Lock()
        outer = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_args) -> None:  # keep test output quiet
                pass

            def do_POST(self) -> None:  # noqa: N802
                length = int(self.headers.get("Content-Length", "0") or 0)
                self.rfile.read(length)
                if self.path.endswith("/chat/completions"):
                    kind = "chat"
                elif self.path.endswith("/responses"):
                    kind = "responses"
                else:
                    self.send_response(404)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                with outer._lock:
                    outer.hits[kind] += 1
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Connection", "close")
                self.end_headers()
                for chunk in CANNED_CHUNKS:
                    if kind == "chat":
                        event = {"choices": [{"index": 0, "delta": {"content": chunk}}]}
                        payload = f"data: {json.dumps(event)}\n\n"
                    else:
                        event = {"type": "response.output_text.delta", "delta": chunk}
                        payload = f"event: response.output_text.delta\ndata: {json.dumps(event)}\n\n"
                    self.wfile.write(payload.encode())
                if kind == "chat":
                    self.wfile.write(b"data: [DONE]\n\n")
                else:
                    done = {"type": "response.completed", "response": {"id": "resp_perf", "status": "completed"}}
                    self.wfile.write(f"event: response.completed\ndata: {json.dumps(done)}\n\n".encode())
                self.wfile.flush()

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.server.daemon_threads = True
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    def start(self) -> None:
        self.thread.start()

    def stop(self) -> None:
        self.server.shutdown()
        self.server.server_close()

    def total_hits(self) -> int:
        with self._lock:
            return sum(self.hits.values())


def request(base: str, method: str, path: str, body: bytes | None = None) -> int:
    req = urllib.request.Request(base + path, data=body, method=method)
    if body is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=20) as resp:
            resp.read()
            return resp.status
    except urllib.error.HTTPError as e:
        e.read()
        return e.code


def wait_ready(base: str, proc: subprocess.Popen, deadline_s: float) -> float:
    start = time.monotonic()
    while time.monotonic() - start < deadline_s:
        if proc.poll() is not None:
            raise RuntimeError(f"server exited early with code {proc.returncode}")
        try:
            if request(base, "GET", "/health") < 500:
                return (time.monotonic() - start) * 1000.0
        except (urllib.error.URLError, ConnectionError, OSError):
            pass
        time.sleep(0.02)
    raise RuntimeError(f"server did not answer within {deadline_s:.0f}s")


def write_config(home: Path, stub_port: int) -> Path:
    config = home / "opencode.json"
    config.write_text(json.dumps({"provider": {"openai": {"options": {
        "apiKey": "perf-guard-dummy",
        "baseURL": f"http://127.0.0.1:{stub_port}/v1",
    }, "models": {MODEL: {"name": "Perf guard stub"}}}}}), encoding="utf-8")
    return config


def base_env(home: Path, config: Path) -> dict:
    env = dict(os.environ)
    env.update({
        "HOME": str(home),
        "XDG_CONFIG_HOME": str(home / ".config"),
        "OPENCODE_CONFIG": str(config),
    })
    return env


def run_turn(base: str, workspace: str) -> tuple[bool, float, int]:
    """One session plus one generation. Returns (marker seen, ms, 5xx count)."""
    errors = 0
    body = json.dumps({"workspace": workspace, "provider": "openai", "model": MODEL}).encode()
    req = urllib.request.Request(base + "/sessions", data=body, method="POST")
    req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=20) as resp:
            session_id = json.loads(resp.read() or b"{}").get("id", "")
    except urllib.error.HTTPError as e:
        e.read()
        return False, 0.0, 1 if e.code >= 500 else 0
    if not session_id:
        return False, 0.0, 0

    start = time.monotonic()
    gen = json.dumps({"session_id": session_id, "text": "perf guard prompt"}).encode()
    req = urllib.request.Request(base + "/generate", data=gen, method="POST")
    req.add_header("Content-Type", "application/json")
    seen = False
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            seen = MARKER in resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        e.read()
        errors += 1 if e.code >= 500 else 0
    ms = (time.monotonic() - start) * 1000.0

    if request(base, "GET", f"/session/{session_id}/messages") >= 500:
        errors += 1
    return seen, ms, errors


def measure_serve(binary: Path, turns: int, startup_deadline_s: float) -> dict:
    stub = StubProvider()
    stub.start()
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    with tempfile.TemporaryDirectory(prefix="qcode-perf-") as tmp:
        home = Path(tmp) / "home"
        logs = Path(tmp) / "logs"
        home.mkdir()
        logs.mkdir()
        env = base_env(home, write_config(home, stub.port))
        cmd = [str(binary), "serve", "--port", str(port), "--log-dir", str(logs)]
        proc = subprocess.Popen(cmd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            startup_ms = wait_ready(base, proc, startup_deadline_s)
            time.sleep(0.5)
            idle_rss = rss_mb(proc.pid)

            server_errors = 0
            generated = 0
            turn_ms: list[float] = []
            for _ in range(turns):
                seen, ms, errs = run_turn(base, str(home))
                server_errors += errs
                if seen:
                    generated += 1
                    turn_ms.append(ms)
            loaded_rss = rss_mb(proc.pid)
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
    stub_hits = stub.total_hits()
    stub.stop()

    turn_ms.sort()
    p50 = turn_ms[len(turn_ms) // 2] if turn_ms else 0.0
    return {
        "startup_ms": round(startup_ms, 1),
        "idle_rss_mb": round(idle_rss, 1),
        "loaded_rss_mb": round(loaded_rss, 1),
        "turns": turns,
        "turns_generated": generated,
        "stub_hits": stub_hits,
        "turn_p50_ms": round(p50, 1),
        "server_errors": server_errors,
    }


def measure_tui(binary: Path, idle_s: float, first_paint_deadline_s: float) -> dict:
    stub = StubProvider()
    stub.start()
    with tempfile.TemporaryDirectory(prefix="qcode-perf-tui-") as tmp:
        home = Path(tmp) / "home"
        home.mkdir()
        env = base_env(home, write_config(home, stub.port))
        env["TERM"] = "xterm-256color"
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 120, 0, 0))
        start = time.monotonic()
        proc = subprocess.Popen([str(binary), "tui"], env=env, stdin=slave, stdout=slave,
                                stderr=slave, start_new_session=True, close_fds=True)
        os.close(slave)
        tui_rss = 0.0
        first_paint_ms = None
        try:
            deadline = start + first_paint_deadline_s
            while time.monotonic() < deadline and first_paint_ms is None:
                if proc.poll() is not None:
                    raise RuntimeError(f"tui exited early with code {proc.returncode}")
                ready, _, _ = select.select([master], [], [], 0.05)
                if ready:
                    try:
                        if os.read(master, 65536):
                            first_paint_ms = (time.monotonic() - start) * 1000.0
                    except OSError:
                        break
            if first_paint_ms is None:
                raise RuntimeError(f"tui produced no output within {first_paint_deadline_s:.0f}s")

            # Drain output while idling so the pty buffer never blocks the process.
            idle_until = time.monotonic() + idle_s
            while time.monotonic() < idle_until:
                ready, _, _ = select.select([master], [], [], 0.1)
                if ready:
                    try:
                        os.read(master, 65536)
                    except OSError:
                        break
            tui_rss = rss_mb(proc.pid)
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            os.close(master)
            stub.stop()
    return {
        "tui_first_paint_ms": round(first_paint_ms or 0.0, 1),
        "tui_rss_mb": round(tui_rss, 1),
    }


GATED = {
    "startup_ms": "serve startup",
    "loaded_rss_mb": "serve RSS after turns",
    "tui_first_paint_ms": "TUI first paint",
    "tui_rss_mb": "TUI RSS",
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", required=True, type=Path)
    ap.add_argument("--turns", type=int, default=30)
    ap.add_argument("--startup-deadline", type=float, default=20.0)
    ap.add_argument("--tui", action="store_true", help="also measure the TUI under a pty (Linux)")
    ap.add_argument("--tui-idle", type=float, default=2.0,
                    help="seconds the TUI idles before its RSS is read")
    ap.add_argument("--baseline", type=Path, help="JSON from --write-baseline")
    ap.add_argument("--write-baseline", type=Path, help="write measured numbers here and exit 0")
    ap.add_argument("--tolerance", type=float, default=1.5, help="allowed multiple of the baseline")
    args = ap.parse_args()

    if not args.binary.exists():
        print(f"binary not found: {args.binary}", file=sys.stderr)
        return 2

    result = measure_serve(args.binary, args.turns, args.startup_deadline)
    if args.tui:
        result.update(measure_tui(args.binary, args.tui_idle, args.startup_deadline))
    print(json.dumps(result, indent=2))

    if args.write_baseline:
        args.write_baseline.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(f"baseline written to {args.write_baseline}")
        return 0

    failures = []
    if result["server_errors"]:
        failures.append(f"{result['server_errors']} request(s) returned 5xx")
    if result["turns_generated"] != result["turns"]:
        failures.append(f"only {result['turns_generated']}/{result['turns']} turns produced the marker")
    if result["stub_hits"] < result["turns"]:
        failures.append(f"stub saw {result['stub_hits']} requests for {result['turns']} turns")

    if args.baseline:
        base = json.loads(args.baseline.read_text(encoding="utf-8"))
        for key, label in GATED.items():
            if key not in result or key not in base:
                continue
            limit = base[key] * args.tolerance
            if result[key] > limit:
                failures.append(f"{label} {result[key]} > limit {limit:.1f} ({key})")

    if failures:
        for f in failures:
            print(f"PERF REGRESSION: {f}", file=sys.stderr)
        return 1
    print("perf guard: within limits")
    return 0


if __name__ == "__main__":
    sys.exit(main())
