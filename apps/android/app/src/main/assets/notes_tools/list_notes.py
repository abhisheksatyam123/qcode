#!/usr/bin/env python3
"""List study-worthy notes under a vault (skip .git/.obsidian/tools/study).

Usage: list_notes.py <vault_root>
Prints JSON array of {path, title, kind} relative to vault.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

SKIP_DIRS = {".git", ".obsidian", ".trash", "tools", "study", "__pycache__"}
NOTE_EXT = {".md", ".txt", ".markdown", ".pdf"}


def main() -> None:
    if len(sys.argv) < 2:
        print("usage: list_notes.py <vault_root>", file=sys.stderr)
        raise SystemExit(2)
    root = Path(sys.argv[1]).resolve()
    out = []
    if not root.is_dir():
        print(json.dumps(out))
        return
    for p in sorted(root.rglob("*")):
        if not p.is_file():
            continue
        if p.suffix.lower() not in NOTE_EXT:
            continue
        rel_parts = p.relative_to(root).parts
        if any(part in SKIP_DIRS for part in rel_parts):
            continue
        out.append(
            {
                "path": str(p.relative_to(root)).replace("\\", "/"),
                "title": p.stem,
                "kind": p.suffix.lower().lstrip("."),
                "abs": str(p),
            }
        )
    print(json.dumps(out, ensure_ascii=False))


if __name__ == "__main__":
    main()
