#!/usr/bin/env python3
"""Headless perf guard for `qcode serve`.

Starts the server with an isolated HOME and config, then measures:
  * startup: milliseconds from spawn until the HTTP API answers,
  * idle RSS after startup settles,
  * RSS after N session round trips (create, read, list, messages).

Exits non-zero when startup or RSS exceeds its threshold, or when any request
returns a 5xx. Thresholds come from --baseline (a JSON file written with
--write-baseline) scaled by --tolerance, or from explicit --max-* flags.

Usage:
  scripts/perf_guard.py --binary build/rel/apps/qcode/qcode --turns 50
  scripts/perf_guard.py --binary ... --write-baseline scripts/perf_baseline.json
  scripts/perf_guard.py --binary ... --baseline scripts/perf_baseline.json
"""

import argparse
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path


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


def request(base: str, method: str, path: str, body: bytes | None = None) -> int:
    req = urllib.request.Request(base + path, data=body, method=method)
    if body is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
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


def measure(binary: Path, turns: int, startup_deadline_s: float) -> dict:
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    with tempfile.TemporaryDirectory(prefix="qcode-perf-") as tmp:
        home = Path(tmp) / "home"
        logs = Path(tmp) / "logs"
        home.mkdir()
        logs.mkdir()
        # One dummy provider so the server starts. No request reaches it: the
        # baseURL points at a closed port, and turns are not generated here.
        config = home / "opencode.json"
        config.write_text(json.dumps({"provider": {"openai": {"options": {
            "apiKey": "perf-guard-dummy",
            "baseURL": "http://127.0.0.1:9/v1",
        }}}}), encoding="utf-8")
        env = dict(os.environ)
        env.update({
            "HOME": str(home),
            "XDG_CONFIG_HOME": str(home / ".config"),
            "OPENCODE_CONFIG": str(config),
        })
        cmd = [str(binary), "serve", "--port", str(port), "--log-dir", str(logs)]
        proc = subprocess.Popen(cmd, env=env, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        try:
            startup_ms = wait_ready(base, proc, startup_deadline_s)  # GET /health
            time.sleep(0.5)  # let startup allocations settle
            idle_rss = rss_mb(proc.pid)

            errors = 0
            ids: list[str] = []
            for _ in range(turns):
                body = json.dumps({"workspace": str(home), "provider": "openai", "model": "gpt-4o-mini"}).encode()
                req = urllib.request.Request(base + "/sessions", data=body, method="POST")
                req.add_header("Content-Type", "application/json")
                try:
                    with urllib.request.urlopen(req, timeout=10) as resp:
                        payload = json.loads(resp.read() or b"{}")
                        sid = payload.get("id") or payload.get("session_id")
                        if sid:
                            ids.append(str(sid))
                        status = resp.status
                except urllib.error.HTTPError as e:
                    e.read()
                    status = e.code
                if status >= 500:
                    errors += 1
                for sid in ids[-1:]:
                    for path in (f"/session/{sid}", f"/session/{sid}/messages", "/sessions"):
                        if request(base, "GET", path) >= 500:
                            errors += 1
            loaded_rss = rss_mb(proc.pid)
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()

    return {
        "startup_ms": round(startup_ms, 1),
        "idle_rss_mb": round(idle_rss, 1),
        "loaded_rss_mb": round(loaded_rss, 1),
        "turns": turns,
        "server_errors": errors,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", required=True, type=Path)
    ap.add_argument("--turns", type=int, default=50)
    ap.add_argument("--startup-deadline", type=float, default=20.0)
    ap.add_argument("--baseline", type=Path, help="JSON from --write-baseline")
    ap.add_argument("--write-baseline", type=Path, help="write measured numbers here and exit 0")
    ap.add_argument("--tolerance", type=float, default=1.25,
                    help="allowed multiple of the baseline (default 1.25)")
    ap.add_argument("--max-startup-ms", type=float)
    ap.add_argument("--max-rss-mb", type=float)
    args = ap.parse_args()

    if not args.binary.exists():
        print(f"binary not found: {args.binary}", file=sys.stderr)
        return 2

    result = measure(args.binary, args.turns, args.startup_deadline)
    print(json.dumps(result, indent=2))

    if args.write_baseline:
        args.write_baseline.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(f"baseline written to {args.write_baseline}")
        return 0

    max_startup = args.max_startup_ms
    max_rss = args.max_rss_mb
    if args.baseline:
        base = json.loads(args.baseline.read_text(encoding="utf-8"))
        max_startup = max_startup or base["startup_ms"] * args.tolerance
        max_rss = max_rss or base["loaded_rss_mb"] * args.tolerance

    failures = []
    if result["server_errors"]:
        failures.append(f"{result['server_errors']} request(s) returned 5xx")
    if max_startup is not None and result["startup_ms"] > max_startup:
        failures.append(f"startup {result['startup_ms']} ms > limit {max_startup:.1f} ms")
    if max_rss is not None and result["loaded_rss_mb"] > max_rss:
        failures.append(f"loaded RSS {result['loaded_rss_mb']} MB > limit {max_rss:.1f} MB")

    if failures:
        for f in failures:
            print(f"PERF REGRESSION: {f}", file=sys.stderr)
        return 1
    print("perf guard: within limits")
    return 0


if __name__ == "__main__":
    sys.exit(main())
