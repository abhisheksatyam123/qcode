#!/usr/bin/env python3
"""Record a quiz attempt into chapter (or overall quiz) _progress.json.

Usage:
  record_attempt.py --path classes/class-9/mathematics/ch01-coordinates \\
      --question-id q1 --correct 1 --topic core-idea [--vault .] [--refresh]
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List


MASTERED_MIN_ATTEMPTS = 3
MASTERED_MASTERY = 0.8


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def read_json(path: Path, default: Any = None) -> Any:
    if not path.is_file():
        return default
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return default


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def count_questions(quiz_path: Path) -> int:
    data = read_json(quiz_path, [])
    if isinstance(data, list):
        return len(data)
    if isinstance(data, dict) and isinstance(data.get("questions"), list):
        return len(data["questions"])
    return 0


def derive_status(prog: Dict[str, Any]) -> str:
    attempts = int(prog.get("attempts") or 0)
    if attempts <= 0 and not prog.get("questions_attempted"):
        return "not_started"
    mastery = (int(prog.get("correct") or 0) / attempts) if attempts else 0.0
    if attempts >= MASTERED_MIN_ATTEMPTS and mastery >= MASTERED_MASTERY:
        return "mastered"
    return "in_progress"


def main() -> None:
    ap = argparse.ArgumentParser(description="Record quiz attempt into _progress.json")
    ap.add_argument("--vault", default=".")
    ap.add_argument(
        "--path",
        required=True,
        help="Relative path to chapter or overall quiz folder",
    )
    ap.add_argument("--question-id", required=True)
    ap.add_argument("--correct", type=int, required=True, choices=[0, 1])
    ap.add_argument("--topic", default="")
    ap.add_argument(
        "--refresh",
        action="store_true",
        help="Also run curriculum_status.py --write to refresh rollups",
    )
    args = ap.parse_args()

    vault = Path(args.vault).resolve()
    target = (vault / args.path).resolve()
    if not str(target).startswith(str(vault)):
        print("path escapes vault", file=sys.stderr)
        raise SystemExit(2)
    if not target.is_dir():
        print(f"not a directory: {args.path}", file=sys.stderr)
        raise SystemExit(2)

    progress_path = target / "_progress.json"
    q_total = count_questions(target / "quiz.json")
    prog = read_json(progress_path, {}) or {}
    prog.setdefault("version", 1)
    prog["attempts"] = int(prog.get("attempts") or 0) + 1
    prog["correct"] = int(prog.get("correct") or 0) + (1 if args.correct else 0)
    attempted: List[str] = list(prog.get("questions_attempted") or [])
    if args.question_id not in attempted:
        attempted.append(args.question_id)
    prog["questions_attempted"] = attempted
    prog["questions_total"] = q_total or int(prog.get("questions_total") or 0)
    prog["last_attempt_at"] = utc_now()

    by_topic: Dict[str, Any] = dict(prog.get("by_topic") or {})
    topic = (args.topic or "general").strip() or "general"
    t = dict(by_topic.get(topic) or {})
    t["attempts"] = int(t.get("attempts") or 0) + 1
    t["correct"] = int(t.get("correct") or 0) + (1 if args.correct else 0)
    t_attempts = t["attempts"]
    t["mastery"] = round((t["correct"] / t_attempts) if t_attempts else 0.0, 3)
    by_topic[topic] = t
    prog["by_topic"] = by_topic
    prog["status"] = derive_status(prog)

    write_json(progress_path, prog)
    print(json.dumps(prog, ensure_ascii=False, indent=2))

    if args.refresh:
        tool = Path(__file__).resolve().parent / "curriculum_status.py"
        if tool.is_file():
            subprocess.run(
                [sys.executable, str(tool), str(vault), "--write"],
                check=False,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )


if __name__ == "__main__":
    main()
