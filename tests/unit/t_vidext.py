#!/usr/bin/env python3
"""The external renderer hook's inventory (SPEC.md 120.3.1).

Every planar drawing body in the kernel dispatches on [vid_mono] (or on
[vid_planes]), and on a packed display every one of those bodies must either
hand the work to VESA.DRV or refuse it on purpose. This row keeps the list
honest in three ways:

  1. the DISPATCH SITES are a registry, tests/vidext.txt: one line per
     `cmp byte [vid_mono], 0` / `mov r8, [vid_mono]` / `cmp byte [vid_planes],
     1` in the kernel, by file and count, each hooked or exempt with a reason.
     A new site fails `make` until it is written down - a new planar body
     that forgot its VIDEXT would otherwise program the VGA's ports on a
     packed mode and draw nothing anyone can diagnose;
  2. every selector kernel/vxabi.inc names is EMITTED exactly once in the
     kernel (`VIDEXT n`, `VIDEXTC n, exit`, a `db n` beside vx_callf1), so
     the driver's table and the kernel's hooks cannot drift;
  3. the driver's jump table has one body per selector and none of them is
     the refusal stub, so a selector the driver stopped answering is a build
     failure and not a blank area on the glass.

Host-side, no emulator, part of the fast tier.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402

REG = os.path.join(ROOT, "tests", "vidext.txt")
SITE = re.compile(r"^\s*(cmp byte \[vid_mono\], 0|mov [a-d][lh], \[vid_mono\]"
                  r"|cmp byte \[vid_planes\], 1)(\s|$)")
HOOK = re.compile(r"^\s*(VIDEXT|VIDEXTC|VIDEXTR)\s+(\S+)")
SEL = re.compile(r"^(VX_[A-Z0-9]+)\s+equ\s+(\d+)")


def code(line):
    return line.split(";", 1)[0]


def main():
    # --- 1. the dispatch sites, by file, against the registry -------------
    reg = {}
    for line in open(REG):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        f, n, status = line.split(None, 2)
        reg[f] = (int(n), status)
    found = {}
    for f in sorted(os.listdir(os.path.join(ROOT, "kernel"))):
        if not f.endswith(".inc"):
            continue
        n = 0
        with open(os.path.join(ROOT, "kernel", f), errors="replace") as fh:
            for line in fh:
                if SITE.match(code(line)):
                    n += 1
        if n:
            found[f] = n
    for f, n in sorted(found.items()):
        want = reg.get(f)
        check(want is not None and want[0] == n,
              "%s: %d dispatch site(s) on [vid_mono]/[vid_planes], registered"
              % (f, n),
              "a NEW planar dispatch is a body VESA.DRV does not draw: hook it "
              "(VIDEXT/VIDEXTC/VIDEXTR) or write it into tests/vidext.txt with "
              "the reason it needs no hook - registered %r"
              % (want,))
    for f in reg:
        check(f in found, "%s: the registry's file still has sites" % f,
              "a registry row for a file with no site left is stale: delete it")

    # --- 2. every selector emitted exactly once in the kernel --------------
    sels = {}
    for line in open(os.path.join(ROOT, "kernel", "vxabi.inc")):
        m = SEL.match(line)
        if m and m.group(1) != "VX_NSEL":
            sels[m.group(1)] = int(m.group(2))
    nsel = None
    for line in open(os.path.join(ROOT, "kernel", "vxabi.inc")):
        m = re.match(r"^VX_NSEL\s+equ\s+(\d+)", line)
        if m:
            nsel = int(m.group(1))
    check(nsel is not None and len(sels) == nsel and
          sorted(sels.values()) == list(range(nsel)),
          "vxabi.inc: VX_NSEL selectors, numbered 0..%s" % (nsel,),
          "the selectors index the driver's jump table")
    emitted = {s: 0 for s in sels}
    for f in sorted(os.listdir(os.path.join(ROOT, "kernel"))):
        if not f.endswith(".inc"):
            continue
        with open(os.path.join(ROOT, "kernel", f), errors="replace") as fh:
            for line in fh:
                c = code(line)
                m = HOOK.match(c)
                if m and m.group(1) != "VIDEXTR":
                    emitted[m.group(2).rstrip(",")] = \
                        emitted.get(m.group(2).rstrip(","), 0) + 1
                    continue
                m = re.match(r"^\s*(?:\w+\s+)?db\s+(VX_[A-Z0-9]+)\s*$", c)
                if m:
                    emitted[m.group(1)] = emitted.get(m.group(1), 0) + 1
    for s in sorted(sels, key=sels.get):
        check(emitted.get(s, 0) == 1,
              "%s is hooked exactly once in the kernel (%d)" % (s, emitted.get(s, 0)),
              "a selector with no hook is a driver body nothing reaches; one "
              "with two is two bodies claiming one register contract")

    # --- 3. the driver answers every selector with a body -----------------
    drv = open(os.path.join(ROOT, "drivers", "vesa", "vesa.asm"),
               errors="replace").read()
    m = re.search(r"^vx_jt:\n((?:\s*dw\s+\S+.*\n)+)", drv, re.M)
    rows = re.findall(r"^\s*dw\s+(\S+)", m.group(1), re.M) if m else []
    check(len(rows) == nsel, "vesa.asm: vx_jt has VX_NSEL (%s) rows (%d)"
          % (nsel, len(rows)), "the kernel's selector would index past it")
    check(rows and all(r != "vxp_refuse" for r in rows),
          "vesa.asm: no selector is answered by the refusal stub",
          "a refused primitive is a blank area on the glass: either give it "
          "a body or take its hook out and register the site as exempt - "
          "%r" % ([r for r in rows if r == "vxp_refuse"],))
    done("t_vidext")


if __name__ == "__main__":
    main()
