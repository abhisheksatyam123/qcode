#!/usr/bin/env python3
"""Run one task on the Gemini web app (gemini.google.com) via a logged-in Firefox profile.

No official API exists for the Gemini 4 web modes (Hydrogen / Argon), so this drives
the real UI with Selenium + geckodriver.

Usage:
  python3 tools/gemini_web_task.py "summarise this: ..."            # Hydrogen (default)
  python3 tools/gemini_web_task.py --mode argon "write a regex ..."
  echo "long prompt" | python3 tools/gemini_web_task.py -           # prompt from stdin
  python3 tools/gemini_web_task.py --json "..."                     # JSON out

Exit codes: 0 ok, 2 not logged in, 3 mode gated/locked, 4 timeout/no reply.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
import time

MODES = {
    "hydrogen": "Everyday help",   # 4 Hydrogen
    "argon": "Code & analysis",    # 4 Argon
    "carbon": "Most complex work", # 4 Carbon (Ultra-gated on Pro)
    "flash-lite": "Quick answers",
    "pro": "Detailed research",
}
DEFAULT_PROFILE = os.path.expanduser("~/qcode-browser/ff-profile")
DEFAULT_DRIVER = os.path.expanduser("~/qcode-browser/geckodriver")


def build_driver(profile: str, headed: bool, driver_path: str):
    from selenium import webdriver
    from selenium.webdriver.firefox.options import Options
    from selenium.webdriver.firefox.service import Service

    if not shutil.which("firefox") and not os.path.exists("/snap/bin/firefox"):
        raise SystemExit("firefox not found")
    if not os.path.exists(driver_path):
        raise SystemExit(
            f"geckodriver not found at {driver_path}. Download it from "
            "https://github.com/mozilla/geckodriver/releases and pass --driver."
        )
    opts = Options()
    opts.binary_location = "/snap/bin/firefox" if os.path.exists("/snap/bin/firefox") else "firefox"
    opts.add_argument("-profile")
    opts.add_argument(profile)
    opts.add_argument("-no-remote")
    if not headed:
        opts.add_argument("-headless")
    service = Service(driver_path, log_output=os.path.join(os.path.dirname(profile), "task-geckodriver.log"))
    return webdriver.Firefox(service=service, options=opts)


def picker_label(driver) -> str:
    from selenium.webdriver.common.by import By

    for b in driver.find_elements(By.CSS_SELECTOR, "button"):
        label = b.get_attribute("aria-label") or ""
        if "mode picker" in label.lower():
            return label.split("currently")[-1].strip()
    return ""


def select_mode(driver, mode: str) -> None:
    from selenium.webdriver.common.by import By
    from selenium.webdriver.common.keys import Keys

    subtitle = MODES[mode]
    for b in driver.find_elements(By.CSS_SELECTOR, "button"):
        if "mode picker" in (b.get_attribute("aria-label") or "").lower():
            driver.execute_script("arguments[0].click()", b)
            break
    else:
        raise SystemExit("mode picker not found (not logged in?)")
    time.sleep(2)
    picked = False
    for item in driver.find_elements(By.CSS_SELECTOR, "[role=menuitem],[role=menuitemradio]"):
        if subtitle in (item.text or ""):
            driver.execute_script("arguments[0].click()", item)
            picked = True
            break
    time.sleep(2)
    body = driver.find_element(By.TAG_NAME, "body").text
    if "Unlock" in body and mode == "carbon":
        driver.save_screenshot(os.path.join(os.path.dirname(DEFAULT_PROFILE), "carbon-gate.png"))
        raise SystemExit(3)
    if not picked:
        raise SystemExit(f"mode '{mode}' ({subtitle}) not offered in the picker")
    label = picker_label(driver)
    if mode.split("-")[0] not in label.lower() and mode not in ("flash-lite", "pro"):
        driver.find_element(By.TAG_NAME, "body").send_keys(Keys.ESCAPE)
        time.sleep(1)
        raise SystemExit(f"mode switch failed: picker shows '{label}'")
    driver.find_element(By.TAG_NAME, "body").send_keys(Keys.ESCAPE)
    time.sleep(1)


def send_prompt(driver, prompt: str) -> None:
    from selenium.webdriver.common.by import By
    from selenium.webdriver.common.keys import Keys

    editors = [c for c in driver.find_elements(By.CSS_SELECTOR, "div[contenteditable=true]") if c.is_displayed()]
    if not editors:
        raise SystemExit("prompt editor not found (not logged in?)")
    editor = editors[0]
    driver.execute_script("arguments[0].focus()", editor)
    editor.send_keys(Keys.CONTROL, "a")
    editor.send_keys(Keys.DELETE)
    time.sleep(0.3)
    editor.send_keys(prompt)
    time.sleep(0.5)
    editor.send_keys(Keys.ENTER)


def wait_reply(driver, timeout: int) -> str:
    from selenium.webdriver.common.by import By

    deadline = time.time() + timeout
    last, stable_since = "", 0.0
    while time.time() < deadline:
        time.sleep(2)
        blocks = driver.find_elements(By.CSS_SELECTOR, "model-response-content")
        text = (blocks[-1].text or "").strip() if blocks else ""
        if text and text == last:
            if time.time() - stable_since >= 4:
                return text
        else:
            last, stable_since = text, time.time()
    return last


class WebSession:
    """Reusable browser session: keeps Firefox warm, opens a fresh chat per task."""

    def __init__(self, headed: bool = False, profile: str = DEFAULT_PROFILE,
                 driver_path: str = DEFAULT_DRIVER):
        self.headed = headed
        self.profile = profile
        self.driver_path = driver_path
        self.driver = None

    def _ensure(self):
        if self.driver is not None:
            try:
                _ = self.driver.current_url
                return self.driver
            except Exception:  # noqa: BLE001 - browser died, recreate
                try:
                    self.driver.quit()
                except Exception:  # noqa: BLE001
                    pass
                self.driver = None
        self.driver = build_driver(self.profile, self.headed, self.driver_path)
        self.driver.set_page_load_timeout(60)
        return self.driver

    def new_chat(self) -> None:
        self._ensure().get("https://gemini.google.com/app")
        from selenium.webdriver.common.by import By  # noqa: F401 - keep selenium warm

        for _ in range(15):
            if picker_label(self.driver):
                return
            time.sleep(2)
        raise SystemExit(2)

    def run(self, prompt: str, mode: str = "hydrogen", timeout: int = 180) -> dict:
        from selenium.webdriver.common.by import By  # noqa: F401

        started = time.time()
        self.new_chat()
        select_mode(self.driver, mode)
        used_mode = picker_label(self.driver)
        send_prompt(self.driver, prompt)
        reply = wait_reply(self.driver, timeout)
        return {"mode": used_mode, "prompt": prompt, "reply": reply,
                "seconds": round(time.time() - started, 1), "ok": bool(reply)}

    def close(self) -> None:
        if self.driver is not None:
            try:
                self.driver.quit()
            except Exception:  # noqa: BLE001
                pass
            self.driver = None


def run(prompt: str, mode: str = "hydrogen", timeout: int = 180, headed: bool = False,
        profile: str = DEFAULT_PROFILE, driver_path: str = DEFAULT_DRIVER) -> dict:
    session = WebSession(headed=headed, profile=profile, driver_path=driver_path)
    try:
        return session.run(prompt, mode=mode, timeout=timeout)
    finally:
        session.close()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("prompt", nargs="*")
    ap.add_argument("--mode", default="hydrogen", choices=sorted(MODES))
    ap.add_argument("--timeout", type=int, default=180)
    ap.add_argument("--headed", action="store_true")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--profile", default=DEFAULT_PROFILE)
    ap.add_argument("--driver", default=DEFAULT_DRIVER)
    args = ap.parse_args()

    prompt = " ".join(args.prompt).strip()
    if prompt in ("", "-"):
        prompt = sys.stdin.read().strip()
    if not prompt:
        ap.error("empty prompt")

    try:
        result = run(prompt, args.mode, args.timeout, args.headed, args.profile, args.driver)
    except SystemExit as exc:
        code = exc.code if isinstance(exc.code, int) else 1
        if code == 3:
            print("error: that mode is locked (Gemini 4 Carbon needs Google AI Ultra)", file=sys.stderr)
        elif code == 2:
            print("error: not logged in — refresh ~/qcode-browser/ff-profile cookies from Firefox", file=sys.stderr)
        else:
            print(f"error: {exc.code}", file=sys.stderr)
        return code or 1

    if args.json:
        print(json.dumps(result, indent=1))
    else:
        print(result["reply"])
    return 0 if result["ok"] else 4


if __name__ == "__main__":
    raise SystemExit(main())
