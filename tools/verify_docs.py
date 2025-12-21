#!/usr/bin/env python3
"""
Verify that documentation derived from the DSL grammar is up-to-date.

This script regenerates docs/grammar.ebnf and the injected grammar section in
DSL_MANUAL.md, then exits non-zero if the working tree changed.

Usage:
  python3 tools/verify_docs.py

Typical CI use:
  make -C src verify-docs
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def run(cmd: list[str]) -> None:
    p = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    sys.stdout.write(p.stdout)
    if p.returncode != 0:
        raise SystemExit(p.returncode)

def main() -> int:
    # Regenerate derived docs
    run([sys.executable, "tools/extract_dsl_grammar.py"])
    run([sys.executable, "tools/update_docs.py"])

    # Check whether regeneration changed anything
    # Works in git checkouts; if git is absent, just succeed.
    try:
        p = subprocess.run(["git", "diff", "--name-only"], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        if p.returncode != 0:
            return 0
        changed = [ln.strip() for ln in p.stdout.splitlines() if ln.strip()]
        if changed:
            sys.stderr.write("[verify_docs] ERROR: Derived docs are out of date. Run: make -C src docs\n")
            sys.stderr.write("[verify_docs] Changed files:\n  " + "\n  ".join(changed) + "\n")
            return 3
    except FileNotFoundError:
        return 0

    print("[verify_docs] OK")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
