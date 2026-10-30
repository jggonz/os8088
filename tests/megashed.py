#!/usr/bin/env python3
"""kern_mega: a kernel cache ABOVE 1MB is purgeable (SPEC.md 50.6.8,
docs/plans/completed/KERN-MEGA-MEMORY-PLAN.md MC1).

    python3 tests/megashed.py            the row
    python3 tests/megashed.py --keep     ...and leave the boot's output

Boots build/mega.img under Bochs (16MB) with build/megaapps.img in B:, opens
B:'s Disk window and the About box so the kernel's caches stand in the pool
(the icon store, B:'s FAT window, raise caches, and the directory read-ahead
store the boot took), then COVERS EVERY FREE KB OF THE POOL with pinned
mx_tab records written through the debugger (owner FEh with MX_FPIN, which
KERN-MEGA-MEMORY-PLAN MC3's pack leaves in place) - a machine whose extended memory
a program holds - and opens the Control Panel. CTRL.DRV is loaded low and
moved above 1MB by mod_hi (mega_hiclaim2, the top rank), which with the pool
full can only succeed by SHEDDING caches, cheapest first:

  1. `kern_mega: above 1MB: CTRL.DRV` is on the debug port - the module went
     up, so a claim the full pool could not hold was satisfied;
  2. at least one TRIV cache (level FBh) that stood before is gone;
  3. every cache of level FDh and up (the FAT window, the read-ahead store)
     that stood before still stands - the shed took the cheapest it could,
     and stopped when the claim fitted.

THE FAILING CASE (docs/WRITING-TESTS.md 1): take `call mx_shed` out of
mx_alloc's `.shed` arm (kernel/mega.inc) and CTRL.DRV stays in conventional
memory: no line (1), and nothing shed (2). Verified when the row was written.

It reads build/ (wants=) and writes only under build/bochs-megashed/.
"""
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs as ob  # noqa: E402

MX_BLKSZ, MX_NBLK = 10, 96          # kernel/mega.inc
MX_FPURGE = 4
LVL_TRIV, LVL_MED = 0xFB, 0xFD


def w16(b, i):
    return b[i] | b[i + 1] << 8


def table(box):
    X, K = box.reads([("mx_tab", MX_BLKSZ * MX_NBLK), ("xm_kb", 2)])
    rows = []
    for k in range(MX_NBLK):
        e = X[k * MX_BLKSZ:(k + 1) * MX_BLKSZ]
        rows.append({"k": k, "off": w16(e, 0), "kb": w16(e, 2), "own": e[4],
                     "fl": e[5], "aux": w16(e, 8)})
    return rows, w16(K, 0)


def caches(rows):
    return {(r["off"], r["aux"]) for r in rows
            if r["kb"] and r["own"] == 0xFE and r["fl"] & MX_FPURGE}


def menu(box, dy):
    """System menu, then DY guest pixels down: one guest pixel is two X
    pixels (docs/BOCHS.md)."""
    box.goto(10, 5)
    box.xd("mousedown", 1)
    left = dy * 2
    while left > 30:
        box.xd("mousemove_relative", "--", 0, 30)
        time.sleep(0.04)
        left -= 30
    box.xd("mousemove_relative", "--", 0, left)
    time.sleep(0.5)
    box.xd("mouseup", 1)


def main():
    keep = "--keep" in sys.argv
    out = os.path.join(ROOT, "build", "bochs-megashed")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    fails = []
    box = ob.Box(out, [], bimg=os.path.join(ROOT, "build", "megaapps.img"),
                 btype="1_44")
    try:
        box.up()
        box.capture()
        box.dbl(591, 105, "B:'s Disk window")
        menu(box, 19)                   # About os8088...
        box.ui_done("the About box")
        rows, xkb = table(box)
        before = caches(rows)
        print("megashed: pool %d KB, caches above 1MB before: %s" % (
            xkb, sorted("%dK@%d %04X" % (r["kb"], r["off"], r["aux"])
                        for r in rows if (r["off"], r["aux"]) in before
                        and r["kb"])))
        if not any(a >> 8 == LVL_TRIV for _, a in before):
            fails.append("no TRIV cache stood above 1MB to shed - the "
                         "setup did not make one (or MX_FPURGE is not set)")
        # cover every free KB: a program holding the rest of extended memory
        used = sorted((r["off"], r["off"] + r["kb"]) for r in rows if r["kb"])
        gaps, at = [], 0
        for a, b in used:
            if a > at:
                gaps.append((at, a - at))
            at = max(at, b)
        if at < xkb:
            gaps.append((at, xkb - at))
        free = [r["k"] for r in rows if not r["kb"]]
        mxa = box.sym("mx_tab")
        cmds = []
        for (o, kb), k in zip(gaps, free):
            e = mxa + k * MX_BLKSZ
            cmds += ["setpmem 0x%x 2 0x%x" % (e, o),
                     "setpmem 0x%x 2 0x%x" % (e + 4, 0x08FE),
                     "setpmem 0x%x 2 0" % (e + 6),
                     "setpmem 0x%x 2 0" % (e + 8),
                     "setpmem 0x%x 2 0x%x" % (e + 2, kb)]
        box.dbg(cmds)
        print("megashed: covered %d free run(s), %d KB" %
              (len(gaps), sum(g[1] for g in gaps)))
        n0 = len(box.lines())
        menu(box, 35)                   # Control Panel
        box.ui_done("the Control Panel", secs=120)
        box.shot(os.path.join(out, "cp.png"))
        lines = box.lines()[n0:]
        rows2, _ = table(box)
        after = caches(rows2)
        if not any("above 1MB: CTRL.DRV" in l for l in lines):
            fails.append("CTRL.DRV did not go above 1MB with the pool full "
                         "of caches - nothing was shed for it")
        gone = before - after
        print("megashed: shed %s" % sorted("%04X@%d" % (a, o)
                                           for o, a in gone))
        if not any(a >> 8 == LVL_TRIV for _, a in gone):
            fails.append("no TRIV cache was shed")
        for o, a in before:
            if a >> 8 >= LVL_MED and (o, a) not in after:
                fails.append("a level %02Xh cache (%04X) was shed while "
                             "cheaper ones were enough" % (a >> 8, a))
        for l in box.lines():
            if l.startswith("kern_mega FAULT"):
                fails.append(l)
    finally:
        box.stop()
    if not keep:
        shutil.rmtree(out, ignore_errors=True)
    for f in fails:
        print("FAIL:", f)
    if not fails:
        print("megashed: ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
