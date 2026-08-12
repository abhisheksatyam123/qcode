#!/usr/bin/env python3
"""Render quiz.json -> quiz.html for Study Buddy.

Usage: render_quiz_html.py <quiz.json> [quiz.html]
"""
from __future__ import annotations

import html
import json
import sys
from pathlib import Path


def render(quiz) -> str:
    blocks = []
    for i, q in enumerate(quiz if isinstance(quiz, list) else [], start=1):
        qid = html.escape(str(q.get("id", f"q{i}")))
        qtype = html.escape(str(q.get("type", "short")))
        topic = html.escape(str(q.get("topic", "")))
        prompt = q.get("prompt_html") or html.escape(str(q.get("prompt", "")))
        widget = ""
        if qtype == "mcq":
            choices = q.get("choices") or []
            parts = []
            for j, c in enumerate(choices):
                cid = f"{qid}_{j}"
                label = html.escape(str(c))
                parts.append(
                    f'<label class="choice"><input type="radio" name="{qid}" '
                    f'value="{label}"> <span>{label}</span></label>'
                )
            widget = "\n".join(parts)
        elif qtype == "long":
            widget = f'<textarea name="{qid}" rows="5" placeholder="Write your answer…"></textarea>'
        else:
            widget = f'<input type="text" name="{qid}" placeholder="Short answer">'
        blocks.append(
            f'<section class="question" data-qid="{qid}" data-type="{qtype}">'
            f'<div class="meta">Q{i} · {qtype}'
            + (f" · {topic}" if topic else "")
            + f"</div><div class=\"prompt\">{prompt}</div>"
            f'<div class="answer">{widget}</div></section>'
        )
    body = "\n".join(blocks)
    return f"""<!DOCTYPE html>
<html lang="en"><head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>Quiz</title>
<style>
body {{ font-family: system-ui, sans-serif; margin: 16px; color: #e2e8f0; background: #0b1220; }}
.question {{ margin-bottom: 18px; padding-bottom: 12px; border-bottom: 1px solid #1e293b; }}
.meta {{ font-size: 12px; color: #64748b; margin-bottom: 6px; }}
.prompt {{ line-height: 1.45; margin-bottom: 10px; }}
.choice {{ display: flex; gap: 8px; margin: 6px 0; }}
input[type=text], textarea {{ width: 100%; background: #0f172a; border: 1px solid #334155;
  color: #e2e8f0; border-radius: 8px; padding: 10px; }}
button {{ background: #2563eb; color: white; border: 0; border-radius: 8px; padding: 10px 14px; }}
</style></head><body>
<form id="quiz">{body}
<p><button type="submit">Submit</button></p>
</form>
</body></html>
"""


def main() -> None:
    if len(sys.argv) < 2:
        print("usage: render_quiz_html.py <quiz.json> [quiz.html]", file=sys.stderr)
        raise SystemExit(2)
    src = Path(sys.argv[1])
    quiz = json.loads(src.read_text(encoding="utf-8"))
    html_out = render(quiz)
    out = Path(sys.argv[2]) if len(sys.argv) >= 3 else src.with_suffix(".html")
    out.write_text(html_out, encoding="utf-8")
    print(out)


if __name__ == "__main__":
    main()
