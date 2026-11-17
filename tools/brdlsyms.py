#!/usr/bin/env python3
"""Export primary Browser offsets referenced by embedded Browser parts.

A part is assembled on its own, so every `br_*` name it uses and does not
define is an offset into the PRIMARY's segment, taken from browser.map. That
is right for DATA - the part runs with DS = the primary - and wrong for CODE:
a near `call` or `jmp` to an imported label assembles cleanly and lands at
that offset in the PART's code segment. So a near transfer to an imported
name is refused here. A part reaches primary code through a far vector
(brdownload.inc's br_dl_r_* table, the `RCALL` macro), never directly.
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path


NAME = re.compile(r"\bbr_[A-Za-z0-9_]+\b")
LOCAL = re.compile(r"^\s*(br_[A-Za-z0-9_]+)\s*:", re.M)
NEAR = re.compile(r"^\s*(?:[.\w]+:\s*)?(call|j[a-z]+)\s+(?:near\s+|short\s+)?"
                  r"(br_[A-Za-z0-9_]+|net_[A-Za-z0-9_]+)\b")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("map", type=Path)
    ap.add_argument("source", type=Path, nargs="+")
    ap.add_argument("-o", "--output", required=True, type=Path)
    ap.add_argument("--also", nargs="*", default=[],
                    help="primary names to import that are not br_* "
                         "(a constant, or a byte a macro names - NET_CLASS "
                         "is [net_cls]); the map must have each")
    args = ap.parse_args()
    also = set(args.also)

    symbols: dict[str, int] = {}
    for line in args.map.read_text(encoding="utf-8", errors="replace").splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[0].removeprefix("0x").isalnum():
            try:
                value = int(fields[0], 16)
            except ValueError:
                continue
            name = fields[-1]
            if name.startswith("br_") or name in also:
                symbols[name] = value

    texts = {p: p.read_text(encoding="utf-8") for p in args.source}
    source = "\n".join(texts.values())
    code = "\n".join(line.split(";", 1)[0] for line in source.splitlines())
    locals_ = set(LOCAL.findall(source))
    needed = ({n for n in NAME.findall(code)
               if not n.endswith("_")} - locals_) | also
    missing = sorted(needed - symbols.keys())
    if missing:
        ap.error("symbols missing from Browser map: " + ", ".join(missing))

    near = []
    for path, text in texts.items():
        for no, line in enumerate(text.splitlines(), 1):
            m = NEAR.match(line.split(";", 1)[0])
            if m and m.group(2) not in locals_:
                near.append(f"{path}:{no}: {m.group(1)} {m.group(2)}")
    if near:
        ap.error("a near call or jump from a part to primary code lands in the "
                 "part's own segment; go through a far vector instead:\n  "
                 + "\n  ".join(near))

    lines = ["; Generated from build/browser.map by tools/brdlsyms.py."]
    lines.extend(f"{name} equ 0x{symbols[name]:04X}" for name in sorted(needed))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="ascii")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
