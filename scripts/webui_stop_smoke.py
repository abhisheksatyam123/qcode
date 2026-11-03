#!/usr/bin/env python3
"""Headless browser check for the web Subagents panel's Stop button.

Starts `qcode serve` against the scripted stub provider (the same one the
orchestrator smoke uses), with a lead turn that spawns a background subagent
and waits for it. While that subagent runs, scripts/webui_stop_smoke.js opens
the session in headless Chromium, clicks Stop on the running row, and waits
for the row to show killed. This script then checks the server API: the
subagent is killed and the lead still finishes.

Exit codes: 0 pass, 1 fail, 77 skipped (Playwright or Chromium missing).

  scripts/webui_stop_smoke.py --binary build/host-debug/apps/qcode/qcode
"""

import argparse
import json
import os
import shutil
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

HERE = Path(__file__).resolve().parent
LEAD_PROMPT = "SMOKE TUI stop button"
CHROME = "/opt/pw-browsers/chromium"


def post_json(url: str, payload: dict, timeout: float = 20) -> tuple[int, str]:
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
    ap.add_argument("--startup-deadline", type=float, default=20.0)
    args = ap.parse_args()

    if shutil.which("node") is None:
        print("SKIP: node is not installed")
        return 77
    chrome = CHROME if os.path.exists(CHROME) else None
    if chrome is None:
        print("SKIP: Chromium not found at " + CHROME)
        return 77

    stub = Stub()
    stub.start()
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    with tempfile.TemporaryDirectory(prefix="qcode-webui-stop-") as tmp:
        home = Path(tmp) / "home"
        logs = Path(tmp) / "logs"
        home.mkdir()
        logs.mkdir()
        env = base_env(home, write_config(home, stub.port))
        proc = subprocess.Popen([str(args.binary), "serve", "--port", str(port), "--log-dir", str(logs)],
                                env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            wait_ready(base, proc, args.startup_deadline)
            status, body = post_json(base + "/sessions", {
                "workspace": str(home), "provider": "openai", "model": MODEL,
                "agent_mode": "orchestrator"})
            if status >= 300:
                fail(f"could not create a session: {status} {body[:200]}")
            sid = json.loads(body)["id"]

            lead: dict = {}

            def run_lead() -> None:
                status_, text = post_json(base + "/generate", {"session_id": sid, "text": LEAD_PROMPT},
                                          timeout=180)
                lead["status"], lead["text"] = status_, text

            thread = threading.Thread(target=run_lead, daemon=True)
            thread.start()

            # Wait until the background subagent is shown as running.
            deadline = time.monotonic() + 30
            running = False
            while time.monotonic() < deadline and not running:
                tasks = get_json(base, "/tasks?parent_session_id=" + urllib.parse.quote(sid))["metadata"]["tasks"]
                running = any(t.get("status") == "running" for t in tasks)
                if not running:
                    time.sleep(0.2)
            if not running:
                fail("no background subagent reached running")

            env_js = dict(os.environ, QCODE_BASE=base, QCODE_SID=sid, CHROME_PATH=chrome,
                          )
            browser = subprocess.run(["node", str(HERE / "webui_stop_smoke.js")], env=env_js,
                                     timeout=120, capture_output=True, text=True)
            sys.stdout.write(browser.stdout)
            sys.stderr.write(browser.stderr)
            if browser.returncode == 77:
                print("SKIP: Playwright is not installed")
                return 77
            if browser.returncode != 0:
                now = get_json(base, "/tasks?parent_session_id=" + urllib.parse.quote(sid))["metadata"]["tasks"]
                print("server task statuses: " + str([t.get("status") for t in now]))
                fail("the browser check failed")

            thread.join(timeout=120)
            if lead.get("status") != 200 or "SMOKE DONE" not in lead.get("text", ""):
                fail("the lead did not finish with SMOKE DONE after the stop")

            tasks = get_json(base, "/tasks?parent_session_id=" + urllib.parse.quote(sid))["metadata"]["tasks"]
            statuses = [t.get("status") for t in tasks]
            if "killed" not in statuses or "running" in statuses:
                fail(f"after Stop the subagent should be killed, got {statuses}")

            print("OK: web Stop button killed the running subagent; the lead finished.")
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
