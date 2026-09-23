#!/usr/bin/env python3
"""The mount-owned window is the bottom of `.lowbss` (SPEC.md 2.1.2).

    python3 tests/unit/t_lowwin.py

`dsk_secbuf` comes alive at `drv_boot`'s first mount and is untouched before
it - the same moment, and the same silence, as the FAT window under it.
Adjacent, the two are one contiguous region that is dead for the whole of
`kmain`, which is what the boot overlay is meant to land in and spill through
(docs/plans/completed/BOOT-LADDER-PLAN.md stage B).

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

**THE WINDOW IS ONE BUFFER NOW** (SPEC.md 22.6.3).  It was three, then two;
`disk_icons` left for the machine-wide icon store (SPEC.md 25.9), and then
`disk_dir` and `dsk_icoix` left with the floor listing itself - a listing is
written into the store its CALLER supplied, so there is no shared snapshot to
have a home.  What is left either side is `dsk_secbuf`, and the arms are the
same shape again: 512 bytes, the one `int 13h` TARGET here, taking the rung's
512-aligned base because it is the rung's first bytes.

WHAT STILL DIFFERS IS THE FAT RUNG UNDER IT - SPEC.md 51.0.0's two sectors
against nine - so the tables below stay stated per arm, which is also what
keeps them able to disagree: a single table that fits both is a table that has
stopped asserting anything about either.

**AND THE REGION IS DERIVED FROM THE MEASURED WINDOW, not from `WANT`.**  It
used to be summed out of `WANT` and compared with a second hand-written
table, so the two agreed with each other and read nothing out of the kernel at
all: both arms' region checks PASSED on the build where `disk_dir` had been
deleted and the window was a quarter of the size this file claimed.  A check
that compares two constants in the same file is the green row that tests
nothing (docs/WRITING-TESTS.md 1).
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
# kern_small's DSK_FAT_SECS to 2 against kern_big's 9.  Stated rather than
# derived, for this file's own reason: a table read out of the kernel agrees
# with the kernel by construction and would have noticed none of the changes
# below.
#
# The window is `dsk_secbuf` and nothing else on BOTH arms (SPEC.md 22.6.3).
# `dsk_ovlpad` is NOT a row here on purpose: `DSK_OVLPAD` is 0 today - six
# boot-only bodies moved into the blob half through SPEC.md 2.5.3.2's OVBCALL
# set and `.ovlw` fell 1,900 -> 1,342 - so the label emits nothing and never
# reaches the listing.  Give it a row here if it is ever non-zero again.
WANT = {
    "kern_big":   [("dsk_secbuf", 512)],
    "kern_small": [("dsk_secbuf", 512)],
}
FAT_BYTES = {"kern_big": 4608, "kern_small": 1024}   # DSK_FAT_SECS * 512
# ...and what the two make between them: the region the boot overlay spills
# through, and the part of it a whole-sector int 13h read can actually reach.
#
# BOTH ARE WHOLE SECTORS AGAIN, which they had stopped being: 4,608 + 512 is
# 5,120 exactly and 1,024 + 512 is 1,536, so the readable ceiling IS the
# region on both arms.  While the window carried a listing the region was
# 13.125 sectors and the last fraction of a sector was unreachable, which is
# why `kernel.asm`'s `%if` rounds `OVLW_SIZE` UP before comparing.  That
# rounding is still what makes the guard correct and must not be taken out
# because the numbers happen to divide today.
REGION = {"kern_big": (5120, 5120), "kern_small": (1536, 1536)}
SECTOR = 512

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


def window_bytes(rows, names):
    """How many bytes of the rung the window actually occupies.

    `dsk_win_base` and `dsk_win_end` are bare labels that emit nothing, so
    they carry no address column in the listing and cannot be read off it.
    The window is defined as the rung's FIRST bytes, so the number is the
    offset of the first labelled `.lowbss` row the window does not own.

    That is an independent measurement rather than `sum(WANT)` restated, and
    it catches two things the per-label loop above cannot: a foreign block
    landing INSIDE the window (the offset comes back short) and the whole
    window sliding down the rung (it comes back 0, because the foreign label
    is now first).
    """
    for a, _sz, l in rows:
        if l and l not in names:
            return a
    return -1


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

    # THE MEASURED WINDOW, off the listing - dsk_win_base..dsk_win_end as the
    # kernel actually laid it out, and NOT `sum(WANT)`.  Summing the table
    # made the two checks below compare two constants written in this file,
    # so they agreed with each other while disagreeing with the kernel by
    # 1,600 bytes.
    measured = window_bytes(rows, {n for n, _ in want})
    check(measured == total,
          "%s: dsk_win_base..dsk_win_end measures %d" % (label, total),
          "the region below is computed from THIS number, so if it is not "
          "the window the table describes, everything after it is arithmetic "
          "about a kernel that does not exist",
          got=measured, want=total)
    w_region, w_read = REGION[label]
    region = FAT_BYTES[label] + measured
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
