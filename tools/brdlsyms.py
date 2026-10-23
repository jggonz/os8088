#!/usr/bin/env python3
"""Export primary Browser offsets referenced by embedded Browser parts."""
from __future__ import annotations

import argparse
import re
from pathlib import Path


NAME = re.compile(r"\bbr_[A-Za-z0-9_]+\b")
LOCAL = re.compile(r"^\s*(br_[A-Za-z0-9_]+)\s*:", re.M)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("map", type=Path)
    ap.add_argument("source", type=Path, nargs="+")
    ap.add_argument("-o", "--output", required=True, type=Path)
    args = ap.parse_args()

    symbols: dict[str, int] = {}
    for line in args.map.read_text(encoding="utf-8", errors="replace").splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[0].removeprefix("0x").isalnum():
            try:
                value = int(fields[0], 16)
            except ValueError:
                continue
            name = fields[-1]
            if name.startswith("br_"):
                symbols[name] = value

    source = "\n".join(p.read_text(encoding="utf-8") for p in args.source)
    locals_ = set(LOCAL.findall(source))
    needed = set(NAME.findall(source)) - locals_
    missing = sorted(needed - symbols.keys())
    if missing:
        ap.error("symbols missing from Browser map: " + ", ".join(missing))

    lines = ["; Generated from build/browser.map by tools/brdlsyms.py."]
    lines.extend(f"{name} equ 0x{symbols[name]:04X}" for name in sorted(needed))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="ascii")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
