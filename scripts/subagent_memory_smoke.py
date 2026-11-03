#!/usr/bin/env python3
"""Server memory with ten concurrent subagents, over several rounds.

Each round is a fresh orchestrator session whose lead starts ten subagents in
one message. Each subagent's reply is ~100 KB and takes ~1 s, so all ten are
in flight at once. The server's RSS is sampled through the round and once
more after the round settles.

A leak shows up as growth from round to round after the first (warm-up)
round. The script fails when the settled RSS of the last round is more than
LIMIT_MB above the settled RSS of the round before the last one.

  scripts/subagent_memory_smoke.py --binary build/host-debug/apps/qcode/qcode [--rounds 5]
"""

import argparse
import json
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from orchestrator_smoke import Stub, fail, get_json  # noqa: E402
from perf_guard import MODEL, base_env, free_port, rss_mb, wait_ready, write_config  # noqa: E402

LIMIT_MB = 8.0
SETTLE_S = 2.0


def post(url: str, payload: dict, timeout: float = 120) -> tuple[int, str]:
    req = urllib.request.Request(url, data=json.dumps(payload).encode(), method="POST")
    req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", required=True, type=Path)
    ap.add_argument("--rounds", type=int, default=5)
    ap.add_argument("--background", action="store_true",
                    help="start the ten subagents in the background and wait on each")
    ap.add_argument("--startup-deadline", type=float, default=20.0)
    args = ap.parse_args()

    stub = Stub()
    stub.start()
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    with tempfile.TemporaryDirectory(prefix="qcode-memory-") as tmp:
        home = Path(tmp) / "home"
        logs = Path(tmp) / "logs"
        home.mkdir()
        logs.mkdir()
        env = base_env(home, write_config(home, stub.port))
        proc = subprocess.Popen([str(args.binary), "serve", "--port", str(port), "--log-dir", str(logs)],
                                env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            wait_ready(base, proc, args.startup_deadline)
            settled = []
            for rnd in range(1, args.rounds + 1):
                status, body = post(base + "/sessions", {"workspace": str(home), "provider": "openai",
                                                          "model": MODEL, "agent_mode": "orchestrator"})
                if status >= 300:
                    fail(f"round {rnd}: could not create a session")
                sid = json.loads(body)["id"]
                peak = [rss_mb(proc.pid)]
                stop = threading.Event()

                def sample() -> None:
                    while not stop.is_set():
                        peak[0] = max(peak[0], rss_mb(proc.pid))
                        time.sleep(0.05)

                sampler = threading.Thread(target=sample, daemon=True)
                sampler.start()
                status, text = post(base + "/generate", {"session_id": sid,
                                                         "text": f"SMOKE MEM{' bg' if args.background else ''} round {rnd}"})
                stop.set()
                sampler.join()
                if status != 200 or "SMOKE DONE" not in text:
                    fail(f"round {rnd}: the lead did not finish")
                tasks = get_json(base, "/tasks?parent_session_id=" + urllib.parse.quote(sid))["metadata"]["tasks"]
                done = sum(1 for t in tasks if t.get("status") == "done")
                if len(tasks) != 10 or done != 10:
                    fail(f"round {rnd}: expected 10 done subagents, got {[t.get('status') for t in tasks]}")
                time.sleep(SETTLE_S)
                after = rss_mb(proc.pid)
                settled.append(after)
                print(f"round {rnd}: peak {peak[0]:.1f} MB, settled {after:.1f} MB")

            if len(settled) < 3:
                print("OK: too few rounds to judge growth")
                return 0
            growth = settled[-1] - settled[1]
            print(f"settled growth from round 2 to round {len(settled)}: {growth:+.1f} MB "
                  f"(limit {LIMIT_MB:.0f} MB)")
            if growth > LIMIT_MB:
                fail(f"settled RSS grew {growth:.1f} MB over {len(settled) - 2} rounds of ten subagents")
            print("OK: no per-subagent growth beyond the limit.")
            return 0
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()


if __name__ == "__main__":
    sys.exit(main())
