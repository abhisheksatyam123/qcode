#!/usr/bin/env python3
"""Extract PDF text and emit a chapter study pack (content.md + interactive QnA HTML).

Usage:
  python3 chapter_qna_scaffold.py <chapter.pdf> <out_dir> [--title "Chapter title"]

Requires: pypdf (or pdftotext on PATH).
"""
from __future__ import annotations

import argparse
import html
import re
import shutil
import subprocess
import sys
from pathlib import Path


def extract_text(pdf: Path) -> str:
    # Prefer pdftotext for layout-preserving NCERT PDFs.
    if shutil.which("pdftotext"):
        try:
            out = subprocess.check_output(
                ["pdftotext", "-layout", str(pdf), "-"],
                stderr=subprocess.DEVNULL,
            )
            text = out.decode("utf-8", errors="replace")
            if text.strip():
                return text
        except Exception:
            pass
    try:
        from pypdf import PdfReader
    except Exception:
        from PyPDF2 import PdfReader  # type: ignore
    reader = PdfReader(str(pdf))
    parts = []
    for page in reader.pages:
        parts.append(page.extract_text() or "")
    return "\n\n".join(parts)


def clean_text(text: str) -> str:
    # Drop InDesign footer noise like "Chapter 1.indd ..."
    lines = []
    for line in text.splitlines():
        if re.search(r"\.indd\s+\d+", line):
            continue
        if re.match(r"^\d{1,2}/\d{1,2}/\d{4}", line.strip()):
            continue
        lines.append(line.rstrip())
    # Collapse huge blank runs
    out = "\n".join(lines)
    out = re.sub(r"\n{3,}", "\n\n", out)
    return out.strip() + "\n"


def guess_title(text: str, fallback: str) -> str:
    for line in text.splitlines():
        s = line.strip()
        if len(s) >= 8 and not s.startswith("Fig.") and "Ganita" not in s:
            if re.match(r"^\d+\.\d+", s):
                continue
            return s
    return fallback


def section_headings(text: str) -> list[str]:
    heads = []
    for line in text.splitlines():
        s = line.strip()
        if re.match(r"^\d+\.\d+\s+\S+", s) or s in {"Chapter Summary", "End-of-Chapter Exercises"}:
            heads.append(s)
    return heads[:20]


def build_html(title: str, sections: list[str], content_preview: str) -> str:
    """Scaffold interactive QnA; customize prompts per chapter after review."""
    section_opts = sections[:4] or [
        "Introduction",
        "Core idea",
        "Worked examples",
        "Summary",
    ]
    qs = [
        {
            "id": "q1",
            "type": "mcq",
            "topic": section_opts[0],
            "prompt": f"What is the main theme of <em>{html.escape(title)}</em>?",
            "choices": section_opts[:4]
            if len(section_opts) >= 4
            else section_opts + ["Review"] * (4 - len(section_opts)),
            "answer": section_opts[0],
        },
        {
            "id": "q2",
            "type": "short",
            "topic": "Key term",
            "prompt": "Name one key term introduced in this chapter.",
            "answer": "",
        },
        {
            "id": "q3",
            "type": "long",
            "topic": "Explain",
            "prompt": "In your own words, explain the central idea of this chapter to a classmate.",
            "answer": "",
        },
    ]
    # Hand-authored overrides happen in dedicated HTML files; scaffold stays generic.

    blocks = []
    for i, q in enumerate(qs, start=1):
        qid = html.escape(q["id"])
        qtype = html.escape(q["type"])
        topic = html.escape(q.get("topic", ""))
        prompt = q["prompt"]
        if qtype == "mcq":
            widget = "".join(
                f'<label class="choice"><input type="radio" name="{qid}" value="{html.escape(c)}">'
                f" <span>{html.escape(c)}</span></label>"
                for c in q.get("choices", [])
            )
        elif qtype == "long":
            widget = f'<textarea name="{qid}" rows="5" placeholder="Write your answer…"></textarea>'
        else:
            widget = f'<input type="text" name="{qid}" placeholder="Short answer">'
        ans = html.escape(str(q.get("answer", "")))
        blocks.append(
            f'<section class="question" data-qid="{qid}" data-type="{qtype}" data-answer="{ans}">'
            f'<div class="meta">Q{i} · {qtype}'
            + (f" · {topic}" if topic else "")
            + f'</div><div class="prompt">{prompt}</div>'
            f'<div class="answer">{widget}</div>'
            f'<div class="feedback" hidden></div></section>'
        )

    body = "\n".join(blocks)
    preview = html.escape(content_preview[:1200])
    section_list = "".join(f"<li>{html.escape(s)}</li>" for s in sections[:12])
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>{html.escape(title)} · QnA</title>
<style>
:root {{
  --bg: #0b1220; --panel: #111827; --text: #e5e7eb; --muted: #94a3b8;
  --accent: #38bdf8; --ok: #34d399; --bad: #f87171; --line: #1f2937;
}}
* {{ box-sizing: border-box; }}
body {{
  margin: 0; font-family: "Segoe UI", system-ui, sans-serif;
  background: linear-gradient(160deg, #0b1220, #111827 55%, #0f172a);
  color: var(--text); line-height: 1.5;
}}
header {{
  padding: 20px 18px 8px; border-bottom: 1px solid var(--line);
}}
header h1 {{ margin: 0 0 6px; font-size: 1.35rem; }}
header p {{ margin: 0; color: var(--muted); font-size: .95rem; }}
main {{ padding: 16px 18px 40px; max-width: 820px; margin: 0 auto; }}
.card {{
  background: rgba(17,24,39,.85); border: 1px solid var(--line);
  border-radius: 14px; padding: 14px 16px; margin: 14px 0;
}}
.card h2 {{ margin: 0 0 8px; font-size: 1.05rem; color: var(--accent); }}
.question {{ margin: 0 0 16px; padding-bottom: 14px; border-bottom: 1px solid var(--line); }}
.meta {{ font-size: 12px; color: var(--muted); margin-bottom: 6px; }}
.prompt {{ margin-bottom: 10px; }}
.choice {{ display: flex; gap: 8px; margin: 6px 0; align-items: flex-start; }}
input[type=text], textarea {{
  width: 100%; background: #0b1220; border: 1px solid #334155; color: var(--text);
  border-radius: 10px; padding: 10px 12px; font: inherit;
}}
button {{
  background: #2563eb; color: white; border: 0; border-radius: 10px;
  padding: 10px 14px; font-weight: 600; cursor: pointer;
}}
.feedback {{ margin-top: 8px; font-size: .92rem; }}
.feedback.ok {{ color: var(--ok); }}
.feedback.bad {{ color: var(--bad); }}
pre {{
  white-space: pre-wrap; background: #0b1220; border-radius: 10px;
  padding: 12px; border: 1px solid var(--line); color: #cbd5e1; font-size: .85rem;
}}
ul {{ margin: 8px 0 0 18px; color: #cbd5e1; }}
</style>
</head>
<body>
<header>
  <h1>{html.escape(title)}</h1>
  <p>Interactive chapter QnA · open in Files preview · answer &amp; check</p>
</header>
<main>
  <div class="card">
    <h2>Sections found in the PDF</h2>
    <ul>{section_list or "<li>(no numbered sections detected)</li>"}</ul>
  </div>
  <div class="card">
    <h2>Quiz</h2>
    <form id="quiz">{body}
      <p><button type="submit">Check answers</button></p>
    </form>
  </div>
  <div class="card">
    <h2>Text preview (from PDF extract)</h2>
    <pre>{preview}</pre>
  </div>
</main>
<script>
function norm(s) {{
  return (s || '').toLowerCase().replace(/\\s+/g, ' ').trim();
}}
document.getElementById('quiz').addEventListener('submit', (e) => {{
  e.preventDefault();
  document.querySelectorAll('.question').forEach((block) => {{
    const type = block.dataset.type;
    const expected = norm(block.dataset.answer || '');
    let got = '';
    if (type === 'mcq') {{
      const sel = block.querySelector('input[type=radio]:checked');
      got = sel ? sel.value : '';
    }} else {{
      const input = block.querySelector('textarea, input[type=text]');
      got = input ? input.value : '';
    }}
    const fb = block.querySelector('.feedback');
    fb.hidden = false;
    if (!expected) {{
      fb.className = 'feedback ok';
      fb.textContent = got.trim() ? 'Answer recorded. (open-ended — review with teacher/notes)' : 'Please write an answer.';
      return;
    }}
    const ok = norm(got) === expected || norm(got).includes(expected);
    fb.className = 'feedback ' + (ok ? 'ok' : 'bad');
    fb.textContent = ok ? 'Correct.' : ('Not quite. Expected something like: ' + block.dataset.answer);
  }});
}});
</script>
</body>
</html>
"""


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("pdf")
    ap.add_argument("out_dir")
    ap.add_argument("--title", default="")
    args = ap.parse_args()
    pdf = Path(args.pdf)
    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)

    raw = extract_text(pdf)
    if not raw.strip():
        print("ERROR: empty PDF extract", file=sys.stderr)
        raise SystemExit(2)
    text = clean_text(raw)
    title = args.title or guess_title(text, pdf.stem)
    sections = section_headings(text)

    (out / "content.md").write_text(
        f"# {title}\n\n_Source: `{pdf.name}`_\n\n{text}", encoding="utf-8"
    )
    html_path = out / "quiz.html"
    html_path.write_text(build_html(title, sections, text), encoding="utf-8")
    print(html_path)
    print(f"sections: {len(sections)}  chars: {len(text)}")


if __name__ == "__main__":
    main()
