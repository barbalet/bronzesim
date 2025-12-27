#!/usr/bin/env python3
"""
Extract the authoritative BRONZESIM DSL grammar from src/brz_dsl.c and write it
to docs/grammar.ebnf.

The grammar lives between:
  DSL_GRAMMAR_BEGIN
  DSL_GRAMMAR_END

This tool is intended to be called by the Makefile target: `make docs`.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src" / "brz_dsl.c"
OUT = ROOT / "docs" / "grammar.ebnf"

def main() -> int:
    text = SRC.read_text(encoding="utf-8", errors="replace")

    m = re.search(r"DSL_GRAMMAR_BEGIN(.*?)DSL_GRAMMAR_END", text, flags=re.S)
    if not m:
        print(f"[extract_dsl_grammar] ERROR: No grammar block found in {SRC}", file=sys.stderr)
        return 2

    grammar = m.group(1).strip()

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(grammar + "\n", encoding="utf-8")

    print(f"[extract_dsl_grammar] Wrote {OUT}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
