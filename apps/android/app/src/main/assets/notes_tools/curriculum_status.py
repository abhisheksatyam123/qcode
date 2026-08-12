#!/usr/bin/env python3
"""Walk classes/ and report curriculum gaps / progress.

Usage:
  curriculum_status.py [vault_root] [--json] [--write]

Defaults vault_root to cwd (session workspace should be the notes vault).
With --write, refreshes rollups in _curriculum.json / _class.json / _subject.json.
"""
from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional


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


def progress_mastery(prog: Dict[str, Any]) -> float:
    attempts = int(prog.get("attempts") or 0)
    correct = int(prog.get("correct") or 0)
    if attempts <= 0:
        return 0.0
    return correct / attempts


def derive_status(prog: Dict[str, Any]) -> str:
    attempts = int(prog.get("attempts") or 0)
    if attempts <= 0 and not prog.get("questions_attempted"):
        return "not_started"
    mastery = progress_mastery(prog)
    if attempts >= MASTERED_MIN_ATTEMPTS and mastery >= MASTERED_MASTERY:
        return "mastered"
    return "in_progress"


def normalize_progress(prog: Optional[Dict[str, Any]], questions_total: int) -> Dict[str, Any]:
    p = dict(prog or {})
    p.setdefault("version", 1)
    p.setdefault("attempts", 0)
    p.setdefault("correct", 0)
    p.setdefault("questions_attempted", [])
    p.setdefault("by_topic", {})
    p.setdefault("last_attempt_at", "")
    p["questions_total"] = questions_total or int(p.get("questions_total") or 0)
    p["status"] = derive_status(p)
    return p


def is_chapter_dir(path: Path) -> bool:
    name = path.name
    if name.startswith("_") or name in {"quizzes", "tools"}:
        return False
    return (path / "_meta.json").is_file() or (path / "quiz.json").is_file() or (path / "content.md").is_file()


def scan_vault(vault: Path) -> Dict[str, Any]:
    classes_root = vault / "classes"
    out: Dict[str, Any] = {
        "vault": str(vault),
        "classes_root": str(classes_root),
        "classes": [],
        "summary": {
            "classes_total": 0,
            "subjects_total": 0,
            "chapters_total": 0,
            "chapters_mastered": 0,
            "chapters_in_progress": 0,
            "chapters_not_started": 0,
            "attempts_total": 0,
            "weak_chapters": [],
            "remaining_chapters": [],
            "remaining_subjects": [],
        },
    }
    if not classes_root.is_dir():
        out["error"] = "classes/ not found — run scaffold_chapter.py or seed the curriculum sample"
        return out

    for class_dir in sorted(p for p in classes_root.iterdir() if p.is_dir() and not p.name.startswith("_")):
        class_meta = read_json(class_dir / "_class.json", {}) or {}
        class_entry: Dict[str, Any] = {
            "id": class_meta.get("id") or class_dir.name,
            "title": class_meta.get("title") or class_dir.name,
            "path": str(class_dir.relative_to(vault)).replace("\\", "/"),
            "subjects": [],
        }
        subjects_remaining = 0
        class_chapters = 0
        class_mastered = 0
        class_in_progress = 0
        class_not_started = 0
        class_attempts = 0

        for subject_dir in sorted(
            p for p in class_dir.iterdir() if p.is_dir() and not p.name.startswith("_")
        ):
            subject_meta = read_json(subject_dir / "_subject.json", {}) or {}
            subject_entry: Dict[str, Any] = {
                "id": subject_meta.get("id") or subject_dir.name,
                "title": subject_meta.get("title") or subject_dir.name.replace("-", " ").title(),
                "path": str(subject_dir.relative_to(vault)).replace("\\", "/"),
                "chapters": [],
                "overall_quizzes": [],
            }

            # Chapter dirs (exclude quizzes/)
            chapter_dirs = [
                p
                for p in subject_dir.iterdir()
                if p.is_dir() and p.name != "quizzes" and is_chapter_dir(p)
            ]
            for chapter_dir in sorted(chapter_dirs, key=lambda p: (
                (read_json(p / "_meta.json", {}) or {}).get("order", 9999),
                p.name,
            )):
                meta = read_json(chapter_dir / "_meta.json", {}) or {}
                q_total = count_questions(chapter_dir / "quiz.json")
                prog = normalize_progress(read_json(chapter_dir / "_progress.json", {}), q_total)
                status = prog["status"]
                attempted = len(prog.get("questions_attempted") or [])
                mastery = progress_mastery(prog)
                ch = {
                    "id": meta.get("id") or chapter_dir.name,
                    "title": meta.get("title") or chapter_dir.name,
                    "order": meta.get("order", 0),
                    "path": str(chapter_dir.relative_to(vault)).replace("\\", "/"),
                    "status": status,
                    "attempts": prog.get("attempts", 0),
                    "correct": prog.get("correct", 0),
                    "mastery": round(mastery, 3),
                    "questions_total": q_total,
                    "questions_attempted_count": attempted,
                    "topics": meta.get("topics") or [],
                    "by_topic": prog.get("by_topic") or {},
                }
                subject_entry["chapters"].append(ch)
                class_chapters += 1
                class_attempts += int(prog.get("attempts") or 0)
                if status == "mastered":
                    class_mastered += 1
                elif status == "in_progress":
                    class_in_progress += 1
                else:
                    class_not_started += 1
                if status != "mastered":
                    rem = {
                        "class_id": class_entry["id"],
                        "subject_id": subject_entry["id"],
                        "chapter_id": ch["id"],
                        "path": ch["path"],
                        "status": status,
                        "mastery": ch["mastery"],
                    }
                    out["summary"]["remaining_chapters"].append(rem)
                    if status == "not_started" or mastery < 0.6:
                        out["summary"]["weak_chapters"].append(rem)

            # Subject-wide quizzes/
            quizzes_dir = subject_dir / "quizzes"
            if quizzes_dir.is_dir():
                for quiz_dir in sorted(p for p in quizzes_dir.iterdir() if p.is_dir()):
                    qmeta = read_json(quiz_dir / "_meta.json", {}) or {}
                    q_total = count_questions(quiz_dir / "quiz.json")
                    prog = normalize_progress(read_json(quiz_dir / "_progress.json", {}), q_total)
                    subject_entry["overall_quizzes"].append(
                        {
                            "id": qmeta.get("id") or quiz_dir.name,
                            "title": qmeta.get("title") or quiz_dir.name,
                            "path": str(quiz_dir.relative_to(vault)).replace("\\", "/"),
                            "status": prog["status"],
                            "attempts": prog.get("attempts", 0),
                            "questions_total": q_total,
                        }
                    )

            chapters_remaining = sum(
                1 for c in subject_entry["chapters"] if c["status"] != "mastered"
            )
            subject_entry["rollup"] = {
                "chapters_total": len(subject_entry["chapters"]),
                "chapters_mastered": sum(
                    1 for c in subject_entry["chapters"] if c["status"] == "mastered"
                ),
                "chapters_in_progress": sum(
                    1 for c in subject_entry["chapters"] if c["status"] == "in_progress"
                ),
                "chapters_not_started": sum(
                    1 for c in subject_entry["chapters"] if c["status"] == "not_started"
                ),
                "chapters_remaining": chapters_remaining,
                "attempts_total": sum(int(c["attempts"]) for c in subject_entry["chapters"]),
                "weak_chapters": [
                    c["id"]
                    for c in subject_entry["chapters"]
                    if c["status"] == "not_started" or c["mastery"] < 0.6
                ],
                "updated_at": utc_now(),
            }
            if chapters_remaining > 0:
                subjects_remaining += 1
                out["summary"]["remaining_subjects"].append(
                    {
                        "class_id": class_entry["id"],
                        "subject_id": subject_entry["id"],
                        "path": subject_entry["path"],
                        "chapters_remaining": chapters_remaining,
                    }
                )

            class_entry["subjects"].append(subject_entry)

        class_entry["rollup"] = {
            "subjects_total": len(class_entry["subjects"]),
            "chapters_total": class_chapters,
            "chapters_mastered": class_mastered,
            "chapters_in_progress": class_in_progress,
            "chapters_not_started": class_not_started,
            "subjects_remaining": subjects_remaining,
            "attempts_total": class_attempts,
            "updated_at": utc_now(),
        }
        out["classes"].append(class_entry)
        out["summary"]["classes_total"] += 1
        out["summary"]["subjects_total"] += len(class_entry["subjects"])
        out["summary"]["chapters_total"] += class_chapters
        out["summary"]["chapters_mastered"] += class_mastered
        out["summary"]["chapters_in_progress"] += class_in_progress
        out["summary"]["chapters_not_started"] += class_not_started
        out["summary"]["attempts_total"] += class_attempts

    return out


def write_rollups(vault: Path, report: Dict[str, Any]) -> None:
    classes_root = vault / "classes"
    curriculum = read_json(classes_root / "_curriculum.json", {}) or {}
    curriculum["version"] = 1
    curriculum["title"] = curriculum.get("title") or "Curriculum"
    curriculum["classes"] = [
        {"id": c["id"], "title": c["title"], "path": c["path"]} for c in report["classes"]
    ]
    curriculum["rollup"] = {
        **report["summary"],
        "updated_at": utc_now(),
    }
    # Drop large lists from curriculum rollup file
    curriculum["rollup"].pop("weak_chapters", None)
    curriculum["rollup"].pop("remaining_chapters", None)
    curriculum["rollup"].pop("remaining_subjects", None)
    write_json(classes_root / "_curriculum.json", curriculum)

    for class_entry in report["classes"]:
        class_path = vault / class_entry["path"]
        class_json = read_json(class_path / "_class.json", {}) or {}
        class_json["version"] = 1
        class_json["id"] = class_entry["id"]
        class_json["title"] = class_entry["title"]
        class_json["subjects"] = [
            {"id": s["id"], "title": s["title"], "path": s["path"]}
            for s in class_entry["subjects"]
        ]
        class_json["rollup"] = class_entry["rollup"]
        write_json(class_path / "_class.json", class_json)

        for subject in class_entry["subjects"]:
            subject_path = vault / subject["path"]
            subject_json = read_json(subject_path / "_subject.json", {}) or {}
            subject_json["version"] = 1
            subject_json["id"] = subject["id"]
            subject_json["title"] = subject["title"]
            subject_json["class_id"] = class_entry["id"]
            subject_json["chapters"] = [
                {
                    "id": c["id"],
                    "title": c["title"],
                    "order": c.get("order", 0),
                    "path": c["path"],
                }
                for c in subject["chapters"]
            ]
            subject_json["overall_quizzes"] = [
                {"id": q["id"], "title": q["title"], "path": q["path"]}
                for q in subject.get("overall_quizzes") or []
            ]
            subject_json["rollup"] = subject["rollup"]
            write_json(subject_path / "_subject.json", subject_json)

            # Persist normalized chapter progress status
            for ch in subject["chapters"]:
                ch_path = vault / ch["path"]
                prog = normalize_progress(
                    read_json(ch_path / "_progress.json", {}),
                    ch["questions_total"],
                )
                write_json(ch_path / "_progress.json", prog)


def format_text(report: Dict[str, Any]) -> str:
    lines: List[str] = []
    s = report["summary"]
    if report.get("error"):
        lines.append("ERROR: " + report["error"])
        return "\n".join(lines)
    lines.append("Curriculum status")
    lines.append(
        f"  classes={s['classes_total']} subjects={s['subjects_total']} "
        f"chapters={s['chapters_total']} "
        f"(mastered={s['chapters_mastered']} in_progress={s['chapters_in_progress']} "
        f"not_started={s['chapters_not_started']}) attempts={s['attempts_total']}"
    )
    lines.append("")
    for cls in report["classes"]:
        lines.append(f"Class: {cls['title']} ({cls['id']})")
        for sub in cls["subjects"]:
            r = sub["rollup"]
            lines.append(
                f"  Subject: {sub['title']} — "
                f"{r['chapters_mastered']}/{r['chapters_total']} chapters mastered, "
                f"{r['chapters_remaining']} remaining"
            )
            for ch in sub["chapters"]:
                lines.append(
                    f"    [{ch['status']}] {ch['id']}: "
                    f"attempts={ch['attempts']} correct={ch['correct']} "
                    f"mastery={ch['mastery']:.0%} "
                    f"q={ch['questions_attempted_count']}/{ch['questions_total']} "
                    f"({ch['path']})"
                )
            for q in sub.get("overall_quizzes") or []:
                lines.append(
                    f"    [overall:{q['status']}] {q['id']} attempts={q['attempts']} "
                    f"q_total={q['questions_total']} ({q['path']})"
                )
        lines.append("")
    weak = report["summary"].get("weak_chapters") or []
    if weak:
        lines.append("Weak / gap chapters:")
        for w in weak[:20]:
            lines.append(
                f"  - {w['class_id']}/{w['subject_id']}/{w['chapter_id']} "
                f"[{w['status']}] mastery={w['mastery']}"
            )
    rem_sub = report["summary"].get("remaining_subjects") or []
    if rem_sub:
        lines.append("Subjects with remaining chapters:")
        for r in rem_sub:
            lines.append(
                f"  - {r['class_id']}/{r['subject_id']}: "
                f"{r['chapters_remaining']} chapter(s) left"
            )
    return "\n".join(lines).rstrip() + "\n"


def main() -> None:
    ap = argparse.ArgumentParser(description="Curriculum status for classes/")
    ap.add_argument("vault", nargs="?", default=".")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--write", action="store_true", help="Refresh rollup JSON files")
    args = ap.parse_args()
    vault = Path(args.vault).resolve()
    report = scan_vault(vault)
    if args.write and not report.get("error"):
        write_rollups(vault, report)
        report = scan_vault(vault)
    if args.json:
        print(json.dumps(report, ensure_ascii=False, indent=2))
    else:
        sys.stdout.write(format_text(report))


if __name__ == "__main__":
    main()
