#!/usr/bin/env python3
"""Create class/subject/chapter skeleton under classes/.

Usage:
  scaffold_chapter.py --class 9 --subject mathematics \\
      --slug ch01-number-systems --title "Number Systems" [--vault .] [--order 1]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any, Dict, List


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def read_json(path: Path, default: Any = None) -> Any:
    if not path.is_file():
        return default
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return default


def slugify(s: str) -> str:
    s = s.strip().lower().replace(" ", "-")
    s = re.sub(r"[^a-z0-9._-]+", "-", s)
    return re.sub(r"-{2,}", "-", s).strip("-") or "item"


def class_id_from_arg(raw: str) -> str:
    raw = raw.strip()
    if raw.startswith("class-"):
        return slugify(raw)
    if raw.isdigit():
        return f"class-{raw}"
    return slugify(raw)


def ensure_list_entry(items: List[Dict[str, Any]], entry: Dict[str, Any], key: str = "id") -> List[Dict[str, Any]]:
    out = list(items or [])
    for i, it in enumerate(out):
        if it.get(key) == entry.get(key):
            out[i] = {**it, **entry}
            return out
    out.append(entry)
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description="Scaffold curriculum chapter folders")
    ap.add_argument("--vault", default=".")
    ap.add_argument("--class", dest="class_arg", required=True, help="9 or class-9")
    ap.add_argument("--subject", required=True, help="mathematics")
    ap.add_argument("--slug", required=True, help="ch01-number-systems")
    ap.add_argument("--title", required=True)
    ap.add_argument("--order", type=int, default=0)
    ap.add_argument("--topics", default="", help="comma-separated topic names")
    args = ap.parse_args()

    vault = Path(args.vault).resolve()
    cid = class_id_from_arg(args.class_arg)
    sid = slugify(args.subject)
    chapter_slug = slugify(args.slug)
    topics = [t.strip() for t in args.topics.split(",") if t.strip()]

    class_dir = vault / "classes" / cid
    subject_dir = class_dir / sid
    chapter_dir = subject_dir / chapter_slug
    quizzes_dir = subject_dir / "quizzes"
    chapter_dir.mkdir(parents=True, exist_ok=True)
    quizzes_dir.mkdir(parents=True, exist_ok=True)

    # Chapter files
    meta_path = chapter_dir / "_meta.json"
    if not meta_path.exists():
        write_json(
            meta_path,
            {
                "version": 1,
                "id": chapter_slug,
                "title": args.title,
                "order": args.order or 0,
                "class_id": cid,
                "subject_id": sid,
                "topics": topics,
            },
        )
    prog_path = chapter_dir / "_progress.json"
    if not prog_path.exists():
        write_json(
            prog_path,
            {
                "version": 1,
                "attempts": 0,
                "correct": 0,
                "questions_total": 0,
                "questions_attempted": [],
                "by_topic": {},
                "last_attempt_at": "",
                "status": "not_started",
            },
        )
    quiz_path = chapter_dir / "quiz.json"
    if not quiz_path.exists():
        write_json(quiz_path, [])
    content_path = chapter_dir / "content.md"
    if not content_path.exists():
        content_path.write_text(
            f"# {args.title}\n\n_Add chapter notes here._\n", encoding="utf-8"
        )

    # Subject index
    subject_json_path = subject_dir / "_subject.json"
    subject_json = read_json(subject_json_path, {}) or {}
    subject_json["version"] = 1
    subject_json["id"] = sid
    subject_json["title"] = subject_json.get("title") or sid.replace("-", " ").title()
    subject_json["class_id"] = cid
    chapters = ensure_list_entry(
        subject_json.get("chapters") or [],
        {
            "id": chapter_slug,
            "title": args.title,
            "order": args.order or 0,
            "path": str(chapter_dir.relative_to(vault)).replace("\\", "/"),
        },
    )
    subject_json["chapters"] = chapters
    subject_json.setdefault("overall_quizzes", subject_json.get("overall_quizzes") or [])
    subject_json.setdefault(
        "rollup",
        {
            "chapters_total": len(chapters),
            "chapters_mastered": 0,
            "chapters_in_progress": 0,
            "chapters_not_started": len(chapters),
            "chapters_remaining": len(chapters),
            "attempts_total": 0,
            "weak_chapters": [chapter_slug],
            "updated_at": "",
        },
    )
    write_json(subject_json_path, subject_json)

    # Class index
    class_json_path = class_dir / "_class.json"
    class_json = read_json(class_json_path, {}) or {}
    class_json["version"] = 1
    class_json["id"] = cid
    class_json["title"] = class_json.get("title") or cid.replace("-", " ").title()
    class_json["subjects"] = ensure_list_entry(
        class_json.get("subjects") or [],
        {
            "id": sid,
            "title": subject_json["title"],
            "path": str(subject_dir.relative_to(vault)).replace("\\", "/"),
        },
    )
    write_json(class_json_path, class_json)

    # Curriculum index
    cur_path = vault / "classes" / "_curriculum.json"
    cur = read_json(cur_path, {}) or {}
    cur["version"] = 1
    cur["title"] = cur.get("title") or "Curriculum"
    cur["classes"] = ensure_list_entry(
        cur.get("classes") or [],
        {
            "id": cid,
            "title": class_json["title"],
            "path": str(class_dir.relative_to(vault)).replace("\\", "/"),
        },
    )
    write_json(cur_path, cur)

    print(str(chapter_dir.relative_to(vault)).replace("\\", "/"))


if __name__ == "__main__":
    main()
