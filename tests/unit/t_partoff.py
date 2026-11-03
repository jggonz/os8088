#!/usr/bin/env python3
"""No part offset is converted with a SECTOR shift outside the standard.

    python3 tests/unit/t_partoff.py

A PART STARTS ON A PARAGRAPH (SPEC.md 20.12.2): a row's `off` counts 16-byte
units, and it counted 512-byte sectors until SPEC.md 20.12.2.1. The standard
converts it with `OP_GSH` (apps/os88parts.inc) - but six reader sites lived
OUTSIDE the standard (RealDMG three, MIDIRack, Excitebike, Clear Skies's
program, docs/plans/completed/PART-PACKING-PLAN.md 3.4), each doing its own
`mov cl, 9 / shl ax, cl / mov cl, 7 / shr dx, cl`. A site that is missed when
the unit moves ASSEMBLES CLEANLY and reads the wrong bytes - 32 times too far
into the file - and fails only on the one machine that reaches it, as a
corrupt picture or a refused part with the wrong reason.

So the gate is the SHAPE, wherever it is:

  * assembly: a line that loads `OP_R_OFF` (or `KDH_POFF`/`KDH_COFF`/
    `KDL_L3OFF`/`CSH_PARA`, the hand-off copies of one), followed within a
    few instructions by `mov cl, 9` or `mov cl, 7` - the 512 pair - or by a
    `mov cl, 5` SHL, a sector turned into paragraphs. A reader converts with
    `OP_GSH` and `16 - OP_GSH`, or (a hand-off) names its unit in a comment
    the standard's way;
  * Python: a row's `off` multiplied by a literal 512 - `["off"] * 512` -
    where os88parts.byte_off()/os88pkg.part_table() say the granule.

apps/os88parts.inc, os88partsbody.inc and os88pself.inc ARE the standard and
are not scanned; neither are the two host readers that keep decoding an old
'O88PARTS' table on purpose (tools/os88parts.py, tools/os88pkg.py).

VERIFIED TO FAIL: putting back realdmg.asm's rw_jpart conversion as it was
(`mov cl, 9` / `shl ax, cl` / `mov cl, 7` / `shr dx, cl` after its OP_R_OFF
load) reports that file and line, and so does Clear Skies' program with its
old `mov cl, 7 / shr dx, cl` on the hand-off's CSH_PARA. A later shift on an
unrelated value - kd_tolow's `mov cl, 5 / shl` of a SECTOR COUNT, a few lines
after it loads KDL_L3OFF - is not one: the gate follows the loaded register.
"""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                           # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))

STANDARD = {"apps/os88parts.inc", "apps/os88partsbody.inc",
            "apps/os88pself.inc"}
OLD_READERS = {"tools/os88parts.py", "tools/os88pkg.py"}

LOAD = re.compile(r"^\s*mov\s+\w+\s*,\s*\[[^\]]*\b(OP_R_OFF|KDH_POFF|KDH_COFF|"
                  r"KDL_L3OFF|CSH_PARA)\b", re.I)
CL = re.compile(r"^\s*mov\s+cl\s*,\s*(\d+)\s*(?:;|$)", re.I)
SHIFT = re.compile(r"^\s*(shl|shr)\s+\w+\s*,\s*cl\b", re.I)
WINDOW = 10                 # instructions after the load
PYMUL = re.compile(r"""\[\s*["']off["']\s*\]\s*\*\s*512\b""")


def tracked():
    out = subprocess.run(["git", "ls-files"], cwd=ROOT, capture_output=True,
                         text=True).stdout.split()
    return out


def code(line):
    return line.split(";", 1)[0].strip()


MOVRR = re.compile(r"^\s*(?:mov|xchg)\s+(\w+)\s*,\s*(\w+)\s*$", re.I)
DEST = re.compile(r"^\s*\w+\s+(\w+)\s*,", re.I)


def asm_hits(path, lines):
    """A load of a part offset, then - on THAT value (the register it went
    into, or one it was copied to) - `mov cl, 9|7` and a shift, or
    `mov cl, 5` and a SHL. Tracking the value is what keeps a later,
    unrelated `mov cl, 5 / shl` (a sector count turned into paragraphs, as
    kerndos/kdxpack.inc's kd_tolow does for its WINDOW) out of it."""
    hits = []
    for i, ln in enumerate(lines):
        m = LOAD.match(ln)
        if not m:
            continue
        held = {ln.split(",")[0].split()[-1].lower()}
        seen, j, cl = 0, i + 1, None
        while j < len(lines) and seen < WINDOW and held:
            c = code(lines[j])
            j += 1
            if not c:
                continue
            seen += 1
            if LOAD.match(lines[j - 1]):
                break               # a second load starts its own window
            cm = CL.match(c)
            if cm:
                cl = int(cm.group(1))
                continue
            sm = SHIFT.match(c)
            if sm:
                reg = c.split()[1].rstrip(",").lower()
                if reg in held and cl is not None and (
                        cl in (9, 7) or (cl == 5 and sm.group(1).lower() ==
                                         "shl")):
                    hits.append("%s:%d: mov cl, %d / %s after %s on line %d"
                                % (path, j, cl, c, m.group(1), i + 1))
                    break
                continue
            mm = MOVRR.match(c)
            if mm:
                d, src = mm.group(1).lower(), mm.group(2).lower()
                if src in held:
                    held.add(d)
                    continue
            dm = DEST.match(c)
            if dm and not c.lower().startswith(("cmp", "test", "push")):
                held.discard(dm.group(1).lower())
    return hits


def main():
    files = tracked()
    asm = [f for f in files if f.endswith((".asm", ".inc"))
           and f not in STANDARD]
    pys = [f for f in files if f.endswith(".py") and f not in OLD_READERS
           and f != "tests/unit/t_partoff.py"]
    check(len(asm) > 200 and len(pys) > 200, "the tree's sources were found",
          why="an empty list passes every check below",
          got=(len(asm), len(pys)), want="> 200 each")
    hits, readers = [], 0
    for f in asm:
        try:
            lines = open(os.path.join(ROOT, f), errors="replace").read() \
                .split("\n")
        except OSError:
            continue
        if any(LOAD.match(ln) for ln in lines):
            readers += 1
        hits += asm_hits(f, lines)
    for f in pys:
        try:
            s = open(os.path.join(ROOT, f), errors="replace").read()
        except OSError:
            continue
        for m in PYMUL.finditer(s):
            hits.append("%s:%d: %s" % (f, s.count("\n", 0, m.start()) + 1,
                                       m.group(0)))
    check(readers >= 6, "the part-offset readers outside the standard were "
          "scanned", why="the LOAD pattern finding nothing would pass "
          "everything", got=readers, want=">= 6")
    check(not hits, "no part offset is converted with a sector shift outside "
          "the standard",
          why="a part starts on a PARAGRAPH (SPEC.md 20.12.2): `off` x 512 "
              "reads 32 times too far into the file and assembles cleanly. "
              "Convert with OP_GSH / 16 - OP_GSH (apps/os88parts.inc), or in "
              "Python with os88parts.byte_off() / os88pkg.part_table()",
          got=hits[:8], want="[]")
    done("t_partoff")


if __name__ == "__main__":
    main()
