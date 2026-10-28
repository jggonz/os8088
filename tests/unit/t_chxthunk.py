#!/usr/bin/env python3
"""No RESIDENT code in SHEET calls through a module vector.

    python3 tests/unit/t_chxthunk.py

apps/os88chartovl.inc's ch_xthunk is how the chart module and the macro module reach
resident code: `call far [sh_v_*]` (SHOUT) lands in a CHX shim, and the
thunk swaps the stacked CS for a TAG naming the calling module, so the far
return goes to wherever that module has moved to (NO-PINNING P1b). It has
ONE compare: a CS that is not the chart module's is taken as the macro module's, on the
promise that "every vector is called from module code".

A vector called from `.text` breaks that promise silently. The region's CS
is tagged as the macro module, and the far return lands in the macro module's segment at
a `.text` offset - past the module's end, in bytes that execute as an
endless push loop. `sh_new` did exactly that with `SHOUT sh_colw_clear`,
and File > New swept task 0's stack and stopped the machine on the death
panel (tests/sheettitle.py, the row that found it).

THE CHECK. Sheet is assembled with a map, so every label's section is
nasm's answer and not a walk's guess (os88ovlchk's package fold calls every
section but `.modc` `.text`, which is why it could not see this). Each
source line belongs to the nearest label above it; a SHOUT or a far call
through `sh_v_`/`ch_v_` whose label is in `.text` fails. Macro bodies are
skipped: SHOUT's own definition is not a call site.
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, done                           # noqa: E402
import os88ovlchk                                         # noqa: E402

SRC = "apps/sheet/sheet.asm"
VEC = re.compile(r'^\s*(?:SHOUT\b|call\s+far\s+\[(?:sh|ch)_v_)')


def sections(mapfile):
    """label -> section, from the map's per-section symbol tables."""
    out, cur = {}, None
    for line in open(mapfile):
        m = re.match(r'^---- Section (\S+)', line)
        if m:
            cur = m.group(1)
            continue
        f = line.split()
        if (cur and len(f) == 3 and '.' not in f[2]
                and re.fullmatch(r'[0-9A-F]+', f[0])
                and re.fullmatch(r'[0-9A-F]+', f[1])):
            out[f[2]] = cur
    return out


def main():
    os.chdir(ROOT)
    with tempfile.TemporaryDirectory() as d:
        asm, mp = os.path.join(d, "s.asm"), os.path.join(d, "s.map")
        open(asm, "w").write(open(SRC).read()
                             + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                        "-o", os.path.join(d, "s.bin"), asm], check=True)
        sec = sections(mp)
    check(sec.get("sh_new") == ".text" and sec.get("sh_modm0") == ".modm",
          "the map names Sheet's sections", "the parse found nothing",
          got=(sec.get("sh_new"), sec.get("sh_modm0")))
    bad, owner, inmac = [], None, False
    for f, n, raw in os88ovlchk.expand(SRC, frozenset()):
        line = raw.split(';')[0]
        if re.match(r'\s*%macro\b', line):
            inmac = True
        elif re.match(r'\s*%endmacro\b', line):
            inmac = False
            continue
        if inmac:
            continue
        m = re.match(r'^([A-Za-z_]\w*):', line)
        if m and m.group(1) in sec:
            owner = m.group(1)
        if owner and sec[owner] == ".text" and VEC.match(line):
            bad.append("%s:%d (%s) %s" % (f, n, owner, line.strip()))
    for b in bad:
        print("   " + b)
    check(not bad, "no .text code calls through a module vector",
          "ch_xthunk would tag the region's CS as the macro module's and the far "
          "return would land in that module - make it a near call",
          got=len(bad), want=0)
    done("chxthunk")


if __name__ == "__main__":
    main()
