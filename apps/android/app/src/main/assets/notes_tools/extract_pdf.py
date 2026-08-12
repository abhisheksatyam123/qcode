#!/usr/bin/env python3
"""Extract text from a PDF. Usage: extract_pdf.py <pdf> [out.md]"""
from __future__ import annotations

import sys
from pathlib import Path


def extract(path: Path) -> str:
    try:
        from pypdf import PdfReader  # type: ignore
    except Exception:
        try:
            from PyPDF2 import PdfReader  # type: ignore
        except Exception as e:
            raise SystemExit(f"pypdf/PyPDF2 not available: {e}") from e
    reader = PdfReader(str(path))
    parts = []
    for page in reader.pages:
        parts.append(page.extract_text() or "")
    return "\n\n".join(parts).strip()


def main() -> None:
    if len(sys.argv) < 2:
        print("usage: extract_pdf.py <file.pdf> [out.md]", file=sys.stderr)
        raise SystemExit(2)
    src = Path(sys.argv[1])
    text = extract(src)
    if len(sys.argv) >= 3:
        out = Path(sys.argv[2])
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(f"# {src.stem}\n\n{text}\n", encoding="utf-8")
        print(out)
    else:
        print(text)


if __name__ == "__main__":
    main()
