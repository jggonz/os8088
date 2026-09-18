#!/usr/bin/env python3
"""THE DOS CORE'S bss IS AT THE SAME OFFSETS IN EVERY HOST (SPEC.md 96.44.2).

    python3 tests/unit/t_dosbss.py

docs/plans/KERN-DOS-PLAN.md §4.1.3 puts the INT 21h core in a part both the
box and `kern_dos` join, which means the core is assembled ONCE.  Its state is
reached DS-relative at `os88_image_end + DOS_B_*`, so every one of those
offsets has to come out the same in both builds - and `DOS_B_*` is a running
sum over the `DBSS` rows, so ONE conditional row moves every cell after it.

That is not hypothetical: §96.43.2 gated twenty-nine window rows out of
`kern_dos`, and before this rule existed a thirtieth (`DOS_B_PKTRAW`) sat
inside the packet driver's own `%ifndef KD_BACKEND` - which would have put the
whole tail of the core's state at two different offsets in the two hosts.

FOUR RULES, and every one is a hard zero:

  1. **No `DBSS` row is conditional.**  A row that only some builds emit
     belongs to a host, and a host's rows are `HBSS` - a second accumulator
     based above the core's block, where a host may have as many or as few as
     it likes.
  2. **No `HBSS` row is named from inside the core.**  `%ifndef DOS_EXTCORE`
     marks the core's spans (§96.44); a core routine that reads a host cell
     would resolve it through `dos_hbss`, which is only where it is in THIS
     host.
  3. **No host-varying arm inside the core.**  The core is assembled once, so
     a `%ifdef KD_BACKEND` in it is one host's code in both hosts' binary.
  4. **Every `DBSS` row comes out at the SAME offset in all four builds** -
     and this one reads the ASSEMBLER rather than the source.

**RULE 4 EXISTS BECAUSE RULES 1 TO 3 WERE GREEN WHILE THE HALVES DISAGREED**
(SPEC.md 96.44.2.1).  They read the source and ask whether a ROW is
conditional; the offender was a row's SIZE - `DBSS DOS_B_DVCWD, 2 * DVOL_MAX`,
where `DVOL_MAX` is `%ifndef KD_BACKEND` and came out 6 in the window and 8 in
the core.  Every core cell after that table was four bytes apart in the two
halves, and what it cost was the whole console launcher: the window set
`[dsh_exec]` and `dsh_run` read its own copy, so the core printed `Bad command
or file name` for every program typed at the prompt, and the window's
`[dsh_why]` never said `DSHW_NOCMD` so `dos_con_prog` was never called at all.

A source rule cannot see that, and no cleverer source rule should be attempted:
the assembler already knows the answer, so rule 4 ASKS IT.  Four builds, one
`[map all]` each, every `DBSS` name compared.

VERIFIED TO FAIL: turning any `HBSS` row back into a `DBSS` one inside the
packet driver takes rule 1 red naming the row; reading `[dos_win]` from a core
proc takes rule 2 red naming the proc; putting `DOS_B_DVCWD` back on
`2 * DVOL_MAX` takes rule 4 red naming that row and the two offsets.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                               # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
SRC = os.path.join(ROOT, "apps", "dos", "dos.asm")
OPEN = re.compile(r"^%(?:if|ifdef|ifndef)\b")

# --- rule 4's four builds ---------------------------------------------------
# The same `DBSS` table is assembled into all of them and every one of them is
# something that SHIPS, which is the point: a pair that agrees is not a rule.
#
#   inline   build/dos.o88     - the box with the core inside it
#   parted   build/dosp.bin    - the box that CALLS a core part (-DDOS_EXTCORE)
#   root     build/doscore.bin - that core part, assembled on its own
#   kerndos  build/kerndos.bin - the same core over the KERNEL's disk layer,
#                                which is where DVOL_MAX comes from somebody
#                                else entirely (kernel/disk.inc: 4 or 8)
#
# The `-I` sets are the Makefile's own, per recipe.
BUILDS = (
    ("inline",  ["apps/dos/dos.asm"],   [],
     ["apps/", "apps/dos/", "drivers/net/"]),
    ("parted",  ["apps/dos/dos.asm"],   ["DOSKPART", "DOS_EXTCORE"],
     ["apps/", "apps/dos/", "drivers/net/", "kerndos/"]),
    ("root",    ["apps/dos/doscore.asm"], [],
     ["apps/", "apps/dos/", "drivers/net/", "kerndos/"]),
    ("kerndos", ["kerndos/kdos.asm"],   ["DOS_EXTCORE"],
     ["kernel/", "kerndos/", "apps/", "apps/dos/", "drivers/net/"]),
)


def bss_of(tag, src, defines, incs, d):
    """{DOS_B_x: value} out of nasm's own map - the assembler's answer.

    **THE FILE NAMES MAY NOT CARRY THE DEFINES**, which is not fussiness: the
    `[map all <path>]` line goes through the PREPROCESSOR, so a path with
    `DOSKPART` in it comes back as the empty string that `-DDOSKPART` makes it
    and nasm writes the map somewhere else entirely - with rc 0 and no
    complaint.  The build name is lower case and collides with nothing.
    """
    a = os.path.join(d, tag + ".asm")
    mp = os.path.join(d, tag + ".map")
    open(a, "w").write(open(os.path.join(ROOT, src)).read() +
                       "\n[map all %s]\n" % mp)
    args = ["nasm", "-f", "bin", "-w+error"]
    for i in incs:
        args += ["-I", os.path.join(ROOT, i)]
    args += ["-D" + x for x in defines]
    args += ["-o", os.path.join(d, tag + ".bin"), a]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode:
        return None, r.stderr.strip().split("\n")[-1][:200]
    out, nos = {}, False
    for line in open(mp):
        if line.startswith("---- "):
            nos = line.startswith("---- No Section")
            continue
        f = line.split()
        if nos and len(f) == 2 and f[1].startswith("DOS_B_") \
                and re.fullmatch(r"[0-9A-Fa-f]+", f[0]):
            out[f[1]] = int(f[0], 16)
    return out, None


def regions(lines, opener):
    """(line index -> True) for every line inside a block `opener` matches."""
    inside, d = [], 0
    for l in lines:
        s = l.strip()
        if d == 0 and opener(s):
            d = 1
            inside.append(False)
            continue
        if d:
            if OPEN.match(s):
                d += 1
            elif s.startswith("%endif"):
                d -= 1
        inside.append(d > 0)
    return inside


def main():
    L = open(SRC).read().split("\n")

    # --- rule 1 -------------------------------------------------------------
    host = regions(L, lambda s: re.match(
        r"^%(ifdef|ifndef)\s+(KD_BACKEND|DOSKPART|DOSTRACE|DOSNET_CARD)\b", s))
    bad = [(i + 1, L[i].strip()) for i in range(len(L))
           if host[i] and re.match(r"^\s*DBSS\s", L[i])]
    check(not bad, "no core DBSS row is conditional",
          "\n".join("  dos.asm:%d  %s" % r for r in bad[:8]) +
          "\n  a row only some builds emit moves every cell after it: make it "
          "HBSS (SPEC.md 96.44.2)")

    # --- rule 2 -------------------------------------------------------------
    hb = {m.group(1) for l in L
          for m in [re.match(r"^([a-z_][a-z0-9_]*)\s+equ\s+dos_hbss\s*\+", l)] if m}
    # ...and anything derived from one
    for l in L:
        m = re.match(r"^([a-z_][a-z0-9_]*)\s+equ\s+([a-z_][a-z0-9_]*)\s*\+", l)
        if m and m.group(2) in hb:
            hb.add(m.group(1))
    core = regions(L, lambda s: s.startswith("%ifndef DOS_EXTCORE"))
    hits, proc = [], "?"
    for i, l in enumerate(L):
        m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*):", l)
        if m:
            proc = m.group(1)
        if not core[i] or re.match(r"^\s*(DBSS|HBSS)\s", l):
            continue
        for n in re.findall(r"\b([a-z_][a-z0-9_]*)\b", l.split(";")[0]):
            if n in hb:
                hits.append((i + 1, proc, n))
    check(not hits, "no core proc names a host bss cell",
          "\n".join("  dos.asm:%d  %s reads %s" % h for h in hits[:8]) +
          "\n  a host cell is only where it is in THIS host (SPEC.md 96.44.2)")

    # --- rule 3 -------------------------------------------------------------
    # The core is assembled ONCE, so a build-time arm inside it is the box's or
    # `kern_dos`'s and never both.  Five of these stood at W9b - the last
    # screen, the packet driver's poll, the console and the mouse - and each
    # became a host HOOK (SPEC.md 96.44.3).  DOSTRACE and DOSNET_CARD are not
    # on this list: they are build knobs, the same in every host.
    d, arms, kind = 0, [], None
    for i, l in enumerate(L):
        s = l.strip()
        m = re.match(r"^%(ifdef|ifndef)\s+(KD_BACKEND|DOSKPART)\b", s)
        if m:
            if d == 0:
                kind, start = m.group(0), i
            d += 1
            continue
        if d:
            if OPEN.match(s):
                d += 1
            elif s.startswith("%endif"):
                d -= 1
                if d == 0 and core[start]:
                    arms.append((start + 1, kind))
    check(not arms, "no host-varying arm inside the core",
          "\n".join("  dos.asm:%d  %s" % a for a in arms[:8]) +
          "\n  the core is assembled once: make it a DHK_* hook the host "
          "fills (SPEC.md 96.44.3)")

    # --- rule 4 -------------------------------------------------------------
    # The rows to compare are the DBSS ones, read off the source; their VALUES
    # come from four assemblies of it.  An HBSS row is expected to differ - a
    # host's block is the host's - so it is not in this set.
    rows = [m.group(1) for l in L
            for m in [re.match(r"^\s*DBSS\s+([A-Z_][A-Z0-9_]*)\s*,", l)] if m]
    d = tempfile.mkdtemp(prefix="t_dosbss")
    try:
        maps, broke = {}, []
        for name, src, defs, incs in BUILDS:
            got, err = bss_of(name, src[0], defs, incs, d)
            if err:
                broke.append("  %-8s %s" % (name, err))
            else:
                maps[name] = got
        check(not broke, "all four hosts assemble",
              "\n".join(broke) + "\n  rule 4 cannot answer without them")
        if not broke:
            off = []
            for r in rows:
                seen = {n: m[r] for n, m in maps.items() if r in m}
                if len(set(seen.values())) > 1:
                    off.append("  %-22s %s" % (r, "  ".join(
                        "%s=%d" % (n, v) for n, v in sorted(seen.items()))))
            check(not off, "every DBSS row is at one offset in all four hosts",
                  "\n".join(off[:8]) +
                  "\n  the core is assembled ONCE and joined to either host, so "
                  "a cell at two offsets is silent state corruption in whichever "
                  "host the core was not built against. It is usually a row's "
                  "SIZE naming a per-host constant - DVOL_MAX was the first "
                  "(SPEC.md 96.44.2.1)")
    finally:
        shutil.rmtree(d, ignore_errors=True)
    done("t_dosbss")


if __name__ == "__main__":
    sys.exit(main())
