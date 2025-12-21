#!/usr/bin/env python3
"""
Update DSL_MANUAL.md by injecting the auto-generated grammar section from
docs/grammar.ebnf.

Injection markers inside DSL_MANUAL.md:
  <!-- AUTO-GENERATED-GRAMMAR-BEGIN -->
  <!-- AUTO-GENERATED-GRAMMAR-END -->
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GRAMMAR = ROOT / "docs" / "grammar.ebnf"
MANUAL = ROOT / "DSL_MANUAL.md"

BEGIN = "<!-- AUTO-GENERATED-GRAMMAR-BEGIN -->"
END   = "<!-- AUTO-GENERATED-GRAMMAR-END -->"

def main() -> int:
    if not GRAMMAR.exists():
        print(f"[update_docs] ERROR: Missing {GRAMMAR}. Run extract_dsl_grammar.py first.", file=sys.stderr)
        return 2

    grammar = GRAMMAR.read_text(encoding="utf-8", errors="replace").rstrip() + "\n"
    md = MANUAL.read_text(encoding="utf-8", errors="replace")

    pattern = re.compile(
        re.escape(BEGIN) + r".*?" + re.escape(END),
        flags=re.S
    )

    replacement = (
        f"{BEGIN}\n\n"
        "```ebnf\n"
        f"{grammar}"
        "```\n\n"
        f"{END}"
    )

    if not pattern.search(md):
        print(f"[update_docs] ERROR: Injection markers not found in {MANUAL}", file=sys.stderr)
        return 3

    md2 = pattern.sub(replacement, md, count=1)
    MANUAL.write_text(md2, encoding="utf-8")

    print(f"[update_docs] Updated {MANUAL}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
