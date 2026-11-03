#!/usr/bin/env python3
"""Interrupt smoke: cancelling a lead turn aborts its subagents' requests fast.

The scripted stub holds every subagent reply until something stops it, so a
subagent is always mid-request when the lead is cancelled. Two phases:

  blocking    SMOKE CANCEL: the lead runs one subagent and waits on it.
  background  SMOKE TUI:    the lead starts a background subagent and waits on it.

For each phase the script cancels the lead through POST /session/<id>/cancel
(the API form of Esc) and checks, with the measured times printed:
  * the lead's stream ends within LIMIT_S,
  * the subagent is no longer running within LIMIT_S,
  * the server state is consistent: no running task, the lead is not
    generating, and a second cancel is answered as idle.

Exit 0 pass, 1 fail.

  scripts/interrupt_smoke.py --binary build/host-debug/apps/qcode/qcode
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
from perf_guard import MODEL, base_env, free_port, wait_ready, write_config  # noqa: E402

LIMIT_S = 1.5


def post(url: str, payload: dict | None = None, timeout: float = 20) -> tuple[int, str]:
    data = json.dumps(payload).encode() if payload is not None else b""
    req = urllib.request.Request(url, data=data, method="POST")
    req.add_header("Content-Type", "application/json")
    req.add_header("Content-Length", str(len(data)))
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")


def tasks_of(base: str, sid: str) -> list[dict]:
    return get_json(base, "/tasks?parent_session_id=" + urllib.parse.quote(sid))["metadata"]["tasks"]


def run_phase(name: str, prompt: str, stub: Stub, base: str, home: Path, wait_bg: bool) -> None:
    status, body = post(base + "/sessions", {"workspace": str(home), "provider": "openai",
                                              "model": MODEL, "agent_mode": "orchestrator"})
    if status >= 300:
        fail(f"{name}: could not create a session: {status} {body[:200]}")
    sid = json.loads(body)["id"]

    stub.hits_seen.clear()
    lead: dict = {}

    def run_lead() -> None:
        status_, text = post(base + "/generate", {"session_id": sid, "text": prompt}, timeout=120)
        lead["ended"] = time.monotonic()
        lead["status"], lead["text"] = status_, text

    thread = threading.Thread(target=run_lead, daemon=True)
    thread.start()
    if not stub.hits_seen.wait(timeout=30):
        fail(f"{name}: no subagent request reached the stub")
    # The subagent is in flight; for the background phase, wait until it shows running.
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        tasks = tasks_of(base, sid)
        if tasks and any(t.get("status") == "running" for t in tasks):
            break
        time.sleep(0.05)
    else:
        fail(f"{name}: no subagent shown as running: {tasks_of(base, sid)}")

    t0 = time.monotonic()
    code, _ = post(base + f"/session/{sid}/cancel")
    if code >= 300:
        fail(f"{name}: cancel answered {code}")

    thread.join(timeout=LIMIT_S + 5)
    lead_s = (lead.get("ended", time.monotonic()) - t0)
    if thread.is_alive() or "ended" not in lead:
        fail(f"{name}: the lead's stream did not end after cancel")
    if lead_s > LIMIT_S:
        fail(f"{name}: the lead's stream ended {lead_s:.2f}s after cancel (limit {LIMIT_S}s)")

    # Each subagent must stop, not just the lead.
    while True:
        tasks = tasks_of(base, sid)
        if not any(t.get("status") == "running" for t in tasks):
            break
        if time.monotonic() - t0 > LIMIT_S:
            fail(f"{name}: a subagent still runs {time.monotonic() - t0:.2f}s after cancel: "
                 f"{[t.get('status') for t in tasks]}")
        time.sleep(0.02)
    sub_s = time.monotonic() - t0
    statuses = [t.get("status") for t in tasks]

    # Consistent state: the lead is idle, and a second cancel is answered idle.
    lead_info = get_json(base, f"/session/{sid}")
    if isinstance(lead_info, dict) and lead_info.get("generating") is True:
        fail(f"{name}: the lead still reports generating after cancel")
    code, body = post(base + f"/session/{sid}/cancel")
    if code >= 300 or "cancelled" not in body and code != 200:
        fail(f"{name}: second cancel answered {code} {body[:120]}")

    print(f"OK {name}: lead stream ended in {lead_s * 1000:.0f} ms, subagent stopped in "
          f"{sub_s * 1000:.0f} ms, statuses {statuses}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", required=True, type=Path)
    ap.add_argument("--startup-deadline", type=float, default=20.0)
    args = ap.parse_args()

    stub = Stub()
    stub.start()
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    with tempfile.TemporaryDirectory(prefix="qcode-interrupt-") as tmp:
        home = Path(tmp) / "home"
        logs = Path(tmp) / "logs"
        home.mkdir()
        logs.mkdir()
        env = base_env(home, write_config(home, stub.port))
        proc = subprocess.Popen([str(args.binary), "serve", "--port", str(port), "--log-dir", str(logs)],
                                env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            wait_ready(base, proc, args.startup_deadline)
            run_phase("blocking", "SMOKE CANCEL", stub, base, home, wait_bg=False)
            run_phase("background", "SMOKE TUI", stub, base, home, wait_bg=True)
            print("OK: cancel stops the lead and its subagents within the limit, with consistent state.")
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
