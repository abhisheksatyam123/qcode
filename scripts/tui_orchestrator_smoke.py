#!/usr/bin/env python3
"""Orchestrator mode in the TUI, under a pseudo-terminal, against the smoke stub.

Drives `qcode tui` with keystrokes and reads the screen through a terminal
emulator (pyte), so it checks what a user sees:

  1. Shift+Tab switches the mode to Orchestrator (footer).
  2. A turn spawns subagent A (finishes) and C (keeps running).
  3. The Subagents tab shows C as running while it runs, live.
  4. Pressing k on C (empty prompt, Subagents tab) stops it: C shows killed,
     the header counts say so, and A stays done.
  5. A second turn starts a background subagent C2 that runs. Esc interrupts
     the turn: C2 shows killed within ESC_LIMIT_S, and the lead stops.

Needs the `pyte` package (pip install pyte). Without it the test exits 77,
which CTest treats as skipped.

  scripts/tui_orchestrator_smoke.py --binary build/host-debug/apps/qcode/qcode
"""

import argparse
import fcntl
import os
import pty
import select
import struct
import subprocess
import sys
import tempfile
import termios
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from orchestrator_smoke import Stub  # noqa: E402
from perf_guard import base_env, write_config  # noqa: E402

try:
    import pyte
except ImportError:  # pragma: no cover - environment dependent
    print("SKIP: pyte is not installed")
    sys.exit(77)

ESC_LIMIT_S = 2.0  # the interrupt should land in about 1 s; this leaves room for the pty
SHIFT_TAB = b"\x1b[Z"
ESC = b"\x1b"
DOWN = b"\x1b[B"
UP = b"\x1b[A"
TAB = b"\t"


class Terminal:
    def __init__(self, binary: Path, env: dict) -> None:
        self.screen = pyte.Screen(160, 50)
        self.stream = pyte.ByteStream(self.screen)
        self.master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 50, 160, 0, 0))
        self.proc = subprocess.Popen([str(binary), "tui"], env=env, stdin=slave, stdout=slave,
                                     stderr=slave, start_new_session=True, close_fds=True)
        os.close(slave)

    def pump(self, seconds: float) -> None:
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            ready, _, _ = select.select([self.master], [], [], 0.05)
            if ready:
                try:
                    self.stream.feed(os.read(self.master, 65536))
                except OSError:
                    return

    def send(self, data: bytes, settle: float = 0.5) -> None:
        os.write(self.master, data)
        self.pump(settle)

    def text(self) -> str:
        return "\n".join(line.rstrip() for line in self.screen.display)

    def wait_for(self, needle: str, timeout: float) -> bool:
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if needle in self.text():
                return True
            self.pump(0.2)
        return False

    def close(self) -> None:
        self.proc.terminate()
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        os.close(self.master)


def fail(msg: str, term: Terminal) -> None:
    print(f"FAIL: {msg}")
    print(term.text()[-2000:])
    sys.exit(1)  # main's finally closes the terminal


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", required=True, type=Path)
    args = ap.parse_args()

    stub = Stub()
    stub.start()
    with tempfile.TemporaryDirectory(prefix="qcode-tui-smoke-") as tmp:
        home = Path(tmp) / "home"
        home.mkdir()
        env = base_env(home, write_config(home, stub.port))
        env["TERM"] = "xterm-256color"
        term = Terminal(args.binary, env)
        try:
            term.pump(4)
            term.send(SHIFT_TAB)
            if "Orchestrator" not in term.text():
                fail("Shift+Tab did not switch the footer to Orchestrator", term)

            term.send(DOWN)  # focus the prompt
            for ch in "SMOKE TUI":
                term.send(ch.encode(), settle=0.02)
            term.send(b"\r", settle=0.5)

            term.send(UP)  # focus the tab bar
            for _ in range(3):  # Chat -> Files -> Stats -> Subagents
                term.send(TAB, settle=0.3)
            if "SUBAGENTS (" not in term.text() and "No subagents" not in term.text():
                fail("the Subagents tab did not open", term)

            # Live: C shows as running while its run is open.
            if not term.wait_for("[running]", 30):
                fail("the Subagents tab never showed a running subagent", term)

            # Kill C from the tab with an empty prompt.
            term.send(b"k", settle=0.5)
            if not term.wait_for("[killed]", 20):
                fail("pressing k did not stop C (no [killed] row)", term)
            if "1 killed" not in term.text():
                fail("the header counts do not show 1 killed", term)
            if "[done]" not in term.text():
                fail("A should still show done", term)

            # Esc: a second turn starts a background subagent; Esc interrupts it.
            term.send(DOWN)  # focus the prompt; the Subagents tab stays open
            for ch in "SMOKE ESC":
                term.send(ch.encode(), settle=0.02)
            term.send(b"\r", settle=0.5)
            if not term.wait_for("[running]", 30):
                fail("the second turn's subagent never showed as running", term)
            # Esc means "interrupt" once the lead is inside its turn, waiting on C.
            term.pump(1.5)
            t0 = time.monotonic()
            term.send(ESC, settle=0.0)
            if not term.wait_for("2 killed", ESC_LIMIT_S + 3):
                fail("Esc did not stop the running subagent (header lacks 2 killed)", term)
            esc_s = time.monotonic() - t0
            if esc_s > ESC_LIMIT_S:
                fail(f"Esc took {esc_s:.2f}s to stop the subagent (limit {ESC_LIMIT_S}s)", term)
            if "[running]" in term.text():
                fail("a subagent still shows running after Esc", term)

            print(f"OK: mode switched to Orchestrator; k stopped C; Esc stopped the second subagent in "
                  f"{esc_s * 1000:.0f} ms; A stayed done; header counts updated.")
            return 0
        finally:
            term.close()
            stub.stop()


if __name__ == "__main__":
    sys.exit(main())
