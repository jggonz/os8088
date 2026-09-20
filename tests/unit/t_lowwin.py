#!/usr/bin/env python3
"""The mount-owned window is the bottom of `.lowbss` (SPEC.md 2.1.2).

    python3 tests/unit/t_lowwin.py

`disk_dir`, `dsk_icoix` and `dsk_secbuf` come alive at `drv_boot`'s first
mount and are untouched before it - the same moment, and the same silence, as
the FAT window under them.  Adjacent, the two are one contiguous region that is
dead for the whole of `kmain`, which is what the boot overlay is meant to land
in and spill through (docs/plans/completed/BOOT-LADDER-PLAN.md stage B).

THIS ROW EXISTS BECAUSE NOTHING ELSE WOULD NOTICE.  The placement is bought by
one line - `kernel/dskwin.inc` being the FIRST file `kernel.asm` includes,
because `-f bin` lays a section out in the order its contributions appear and
`.lowbss` has twelve contributors.  Put a new include above it, or move a
`.lowbss` block into a file that sorts earlier, and the window slides into the
middle of the rung.  The kernel still assembles.  It still boots.  Every test
in this tree still passes, because no byte of RAM has moved and no address any
code names has changed - `.lowbss` is `nobits` and reached through SS either
way.  The only thing that breaks is stage C, later, in a build nobody has run
yet, and the symptom there is the overlay writing over `vid_rowtab`.

So the invariant is checked where it can still be read: the offsets, off the
same NASM listing the layout comes from.

BOTH ARMS.  Their windows were DIFFERENT SHAPES and are the same one again:
SPEC.md 25.9 took the icon BODIES out into the machine-wide store, so
kern_small's 16-body pool and kern_big's 2,048-byte array both went and what
is left either side is a reference byte per entry.  What still differs is the
FAT rung UNDER them - SPEC.md 51.0.0's two sectors against nine - so the
tables below stay stated per arm, which is also what keeps them able to
disagree: a single table that fits both is a table that has stopped asserting
anything about either.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402

# PER ARM, because the two sit over different FAT rungs - SPEC.md 51.0.0 cut
# kern_small's DSK_FAT_SECS to 2 against kern_big's 9.  Stated twice rather
# than derived, for this file's own reason: a table read out of the kernel
# agrees with the kernel by construction and would have noticed none of the
# changes below.
#
# **AND THE TWO ARMS ARE THE SAME SHAPE AGAIN**, which is the change SPEC.md
# 25.9 made: `disk_icons` is GONE - the 16x16 bodies are in the machine-wide
# icon store now, and what the window keeps is `dsk_icoix`, one reference byte
# per entry. So the 2,048-byte half of kern_big and the 1,024-byte pool of
# kern_small both went, and both arms are secbuf + listing + index. The other
# move in the same direction was `DSK_NENT` 32 -> 64 (SPEC.md 22.6.2's
# sixty-four-entry listing), which takes `disk_dir` 768 -> 1,536 and
# `DSK_ICOIX_N` with it to 64 - that constant was `DSK_VENT` until SPEC.md
# 22.6 retired the second cap and is `DSK_NENT` on both kernels now. Net: the
# window is 2,112 where it was 3,328.
#
# `dsk_ovlpad` is NOT in this table on purpose: `DSK_OVLPAD` is 0 today
# (dskwin.inc says "AND IT IS ZERO AGAIN"), so the label emits nothing and
# never reaches the listing. Give it a row here if it is ever non-zero again.
#
# **AND THE ARMS ARE DIFFERENT AGAIN** (SPEC.md 22.6.2): `DSK_NENT` is 64 on
# kern_big and 32 on kern_small, so `disk_dir` is 1,536 there and 768 here and
# `dsk_icoix` follows it.  Sixty-four was taken for the DOS box and kern_small
# has none; what makes the cut LANDABLE is SPEC.md 2.5.3.2, which moved the
# serial mouse probe and the adapter probe into `.ovl` so that `.ovlw` rounds
# to 2,048 and fits the smaller region.  The two are one change: the listing
# cannot shrink under an overlay that needs the room, and the overlay moving
# on its own moves HEAP_SEG by nothing.
WANT = {
    "kern_big":   [("dsk_secbuf", 512), ("disk_dir", 1536),
                   ("dsk_icoix", 64)],
    "kern_small": [("dsk_secbuf", 512), ("disk_dir", 768),
                   ("dsk_icoix", 32)],
}
FAT_BYTES = {"kern_big": 4608, "kern_small": 1024}   # DSK_FAT_SECS * 512
# ...and what the two make between them: the region the boot overlay spills
# through, and the part of it a whole-sector int 13h read can actually reach.
REGION = {"kern_big": (6720, 6656), "kern_small": (2336, 2048)}
SECTOR = 512
# `disk_dir` is DSK_NENT * DSK_DE_STRIDE and DSK_DE_STRIDE is 24, not
# DSK_DE_SIZE's 32 (SPEC.md 19.1): a staged listing does not carry the
# record's zero tail.  With DSK_NENT at 64 that is 1,536, and the window is
# 2,112 - so the region is 6,720 on kern_big, of which **6,656 is readable**,
# and 3,136 on kern_small, of which **3,072 is**.  Neither is a whole number
# of sectors, and the readable ceiling is what the boot overlay gets, so the
# numbers are asserted here rather than left to be discovered when `.ovlw`
# next grows.  `kernel.asm`'s own `%if` rounds OVLW_SIZE UP to a sector for
# exactly this reason.
#
# THE WINDOW GOT SMALLER AND THAT COST THE OVERLAY 1,216 BYTES: 3,328 -> 2,112
# when 25.9 moved the icon bodies out, against +768 from DSK_NENT.  It is a
# straight win in RAM - the bodies are shared now instead of copied per
# listing - and a straight loss to `.ovlw`'s ceiling, which is the trade to
# know about before that section next needs room.


def lowbss(defines=()):
    """[(offset, size, label)] for `.lowbss`, in address order."""
    out = os.path.join(ROOT, "build", "t_lowwin.lst")
    binout = os.path.join(ROOT, "build", "t_lowwin.bin")
    cmd = [os.environ.get("NASM", "nasm"), "-f", "bin", "-w+error",
           "-I", "kernel/", "-I", "apps/", "-I", "build/"] + list(defines) + \
          ["-l", out, "-o", binout, "kernel/kernel.asm"]
    subprocess.run(cmd, check=True, cwd=ROOT, stdout=subprocess.DEVNULL)
    sec, rows = None, []
    for ln in open(out, errors="replace"):
        m = re.match(r'^\s*(\d+) ([0-9A-F]{8})? *(<res ([0-9A-Fa-f]+)h>)?'
                     r' *(?:<\d+>)? ?(.*)$', ln)
        if not m:
            continue
        addr, res, src = m.group(2), m.group(4), (m.group(5) or "")
        s = src.strip()
        sm = re.match(r'^section\s+(\.[A-Za-z0-9_]+)', s)
        if sm:
            sec = sm.group(1)
            continue
        if sec != ".lowbss" or not addr:
            continue
        lab = re.match(r'^([A-Za-z_][A-Za-z0-9_]*)', s)
        rows.append((int(addr, 16), int(res, 16) if res else 0,
                     lab.group(1) if lab else ""))
    for p in (out, binout):
        if os.path.exists(p):
            os.remove(p)
    rows.sort()
    return rows


for label, defines in (("kern_big", ("-DKERN_BIG",)),
                       ("kern_small", ("-DKERN_SMALL",))):
    rows = lowbss(defines)
    at = {l: (a, sz) for a, sz, l in rows if l}
    want = WANT[label]

    want_off = 0
    for name, size in want:
        if name not in at:
            check(False, "%s: %s is in .lowbss" % (label, name),
                  "the window is what stage B put at the bottom of the rung",
                  got="absent", want="present")
            continue
        off, sz = at[name]
        check(off == want_off, "%s: %s at .lowbss+%d" % (label, name, want_off),
              "the three have to be the rung's FIRST bytes, so that they and "
              "the FAT window under them are one dead region - see this "
              "file's header for why nothing else would catch a slide",
              got=off, want=want_off)
        check(sz == size, "%s: %s is %d bytes" % (label, name, size),
              "the window's size is what SPEC.md 2.1.2's 8,192 is computed "
              "from; a resize moves the total and stage C's headroom with it",
              got=sz, want=size)
        want_off += size
    if "dsk_secbuf" in at:
        check(at["dsk_secbuf"][0] % 512 == 0,
              "%s: dsk_secbuf is 512-byte aligned in LOW_SEG" % label,
              "it is an int 13h TRANSFER BASE, and only an aligned base cannot "
              "straddle a 64KB DMA page (SPEC.md 2.1.1) - it sat at +2,816 once",
              got=at["dsk_secbuf"][0] % 512, want=0)

    total = sum(s for _, s in want)
    check(want_off == total, "%s: the window is contiguous, %d bytes"
          % (label, total),
          "a gap between them is a gap in the region the overlay spills "
          "through", got=want_off, want=total)
    w_region, w_read = REGION[label]
    region = FAT_BYTES[label] + total
    check(region == w_region, "%s: the overlay's window half is %d bytes"
          % (label, w_region),
          "SPEC.md 2.1.2 and 2.5.3 both quote this number and kernel.asm's "
          "%if is against it; it moved when the staged listing narrowed",
          got=region, want=w_region)
    check((region // SECTOR) * SECTOR == w_read,
          "%s: ...of which %d is READABLE" % (label, w_read),
          "the overlay arrives on the kernel's own int 13h read, so the "
          "usable ceiling is the region rounded DOWN to a whole sector - it "
          "was the same number as the region while the window was 7x512 and "
          "is not any more (SPEC.md 2.1.1)",
          got=(region // SECTOR) * SECTOR, want=w_read)

done("lowwin")
