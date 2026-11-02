#!/usr/bin/env python3
"""kern_mega: hibernate and resume IN PROTECTED MODE, with the trap census
(docs/plans/KERN-MEGA-PLAN.md 14.5, 14.46; SPEC.md 87.9).

    python3 tests/megahiber.py            the row
    python3 tests/megahiber.py --keep     ...and leave the boots' directory

14.5's programs' sum and KERN-MEGA-SEGMENTS-PLAN 8.6's `.mega` split had
never been through a hibernate on kern_mega in protected mode. This boots
`make megatrap`'s kernel (a private tree) under Bochs off C:'s boot partition
with the system floppy's root files beside it, so HIBER.DRV loads from the
volume it writes to (tools/os88bochs.py --csys --bootc --noa), starts MEGAHIB
(tests/megahib: 64KB of XMS patterns held by a window) and Mines from C:\\RUN,
hibernates through the System menu, powers the machine off, boots the same
C: again and resumes. MEGAHIB must then read its 64KB back (`MEGAHIB: ok`),
and the kernel must say `kern_mega: resumed`.

AND THE HIBERNATE MUST NOT LEAN ON A FIXUP: the conventional picture was one
write from segment 0, the NULL selector, so HDD.DRV's PIO loop took a
mega_null0 fixup for every word of the first 64KB - 32,512 of them, two
exceptions each, every hibernate. hbm_xwconv takes only the first CLUSTER
through segment 0 now (1,024 at this C:'s 2KB), so the row allows LIMIT.

`--full` (the row `megahibfull`) is KERN-MEGA-MEMORY-PLAN MC4.1 (b)'s case:
a 2MB machine, and before the hibernate every free KB of the pool is covered
by PINNED mx_tab records written through the debugger (megashed's shape) -
a machine whose extended memory is all in use. The resume used to stage the
whole file in one free block beside the image's runs and so REFUSED here
(hbm_xplan at the write, and the menu greyed it); it streams now, every run
straight to its place, so the same assertions hold: `kern_mega: resumed`,
MEGAHIB's 64KB back, and every filler record standing where it was.

THE FAILING CASE (docs/WRITING-TESTS.md 1): put hbm_wrimg's one
dwf_dskw_write from segment 0 back in place of `call hbm_xwconv` (with
hbm_xlow at 0xFFFF around it, as it was) and the hibernate takes 32,512
traps - red on the census.
"""
import os
import shutil
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs as ob  # noqa: E402
import os88build  # noqa: E402

LIMIT = 2048                    # trap lines a hibernate may take


MX_BLKSZ, MX_NBLK = 10, 96          # kernel/mega.inc
FULL = "--full" in sys.argv


def traps(lines):
    return [l for l in lines if l.startswith("T ")]


def pool(box):
    """[(entry, off, kb, own)] of mx_tab and the pool's KB"""
    X, K = box.reads([("mx_tab", MX_BLKSZ * MX_NBLK), ("xm_kb", 2)])
    rows = [(k,) + struct.unpack_from("<HHB", X, k * MX_BLKSZ)
            for k in range(MX_NBLK)]
    return rows, struct.unpack_from("<H", K, 0)[0]


def fill(box):
    """every free KB of the pool under a PINNED record (owner FEh, MX_FPIN):
    -> the records written, as [(entry, off, kb)]"""
    rows, xkb = pool(box)
    used = sorted((o, o + kb) for _, o, kb, _ in rows if kb)
    gaps, at = [], 0
    for a, b in used:
        if a > at:
            gaps.append((at, a - at))
        at = max(at, b)
    if at < xkb:
        gaps.append((at, xkb - at))
    free = [k for k, _, kb, _ in rows if not kb]
    base = box.sym("mx_tab")
    made, cmds = [], []
    for (o, kb), k in zip(gaps, free):
        e = base + k * MX_BLKSZ
        cmds += ["setpmem 0x%x 2 0x%x" % (e, o),
                 "setpmem 0x%x 2 0x08FE" % (e + 4),
                 "setpmem 0x%x 2 0" % (e + 6),
                 "setpmem 0x%x 2 0" % (e + 8),
                 "setpmem 0x%x 2 0x%x" % (e + 2, kb)]
        made.append((k, o, kb))
    box.dbg(cmds, timeout=60)
    return made, xkb


def main():
    keep = "--keep" in sys.argv
    t = os88build.tree("MEGATRAP=1", targets=(
        "mega.img", "megarun.o88", "mbr.bin", "megahib.o88", "mines.o88"))
    out = os.path.join(t.dir, "megahiber")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    t0 = time.time()
    fails = []
    # --- 1. hibernate -----------------------------------------------------
    megs = 2 if FULL else None
    made = []
    box = ob.Box(out, [t.img("megahib.o88"), t.img("mines.o88")],
                 build=t.dir, bootc=True, noa=True, csys=True, megs=megs)
    try:
        if not box.wait_pm(300):
            print("FAIL: kern_mega never reached protected mode")
            return 1
        # every wait below is on the guest's state (os88bochs.Box) where
        # 20 + 10 + 8 + 20 + 3 + 20 + 10 s were slept, and the hibernate's
        # end is the sentence it puts on the TEXT screen, not 30 s of E9
        # silence
        box.up()
        box.capture()
        box.dbl(591, 160, "C:'s Disk window")
        box.dbl(150, 123, "RUN's listing")
        box.goto(160, 155)              # MEGAHIB
        box.dblclick()
        if not box.wait_for("MEGAHIB: held", 60):
            fails.append("MEGAHIB never held its block")
        box.ui_done("MEGAHIB's window")
        if FULL:
            made, xkb = fill(box)
            print("megahiber: the pool's %dK, %d free run(s) filled: %s"
                  % (xkb, len(made), made))
            if not made:
                fails.append("--full: nothing to fill")
        box.click(300, 84, "RUN to the front")  # RUN's title
        box.dbl(160, 171, "Mines' window")      # Mines
        before = len(traps(box.lines()))
        box.goto(10, 5)                 # System menu > Hibernate...
        box.xd("mousedown", 1)
        for _ in range(5):              # 83 guest pixels, two X pixels each
            box.xd("mousemove_relative", "--", 0, 30)
            time.sleep(0.04)
        box.xd("mousemove_relative", "--", 0, 16)
        box.guest_sleep(0.5)    # the hold, in GUEST time
        box.xd("mouseup", 1)
        box.ui_done("the Hibernate dialog")
        box.goto(225, 225)              # Hibernate
        box.xd("click", 1)

        def off():
            t = box.read(0xB8000, 160 * 6)[0::2].decode("latin-1")
            return "hibernated" in t
        try:
            box.until(off, "the hibernate's `os8088 has hibernated`", 400,
                      poll=1.0)
        except RuntimeError as e:
            fails.append(str(e))
        box.shot(os.path.join(out, "hibernated.png"))
        lines = box.lines()
    finally:
        box.stop()
    hib = len(traps(lines)) - before
    print("megahiber: the hibernate took %d #GP trap lines" % hib)
    for l in lines:
        if l.startswith("kern_mega FAULT") or l.startswith(
                "kern_mega CLOSED"):
            fails.append("hibernate: " + l)
    if hib > LIMIT:
        fails.append("the hibernate took %d #GP fixups, over %d - the "
                     "conventional picture written through segment 0 again? "
                     "(tools/megatraps.py %s)" % (hib, LIMIT, box.e9))
    shutil.copy(box.e9, os.path.join(out, "e9-hibernate.out"))
    # --- 2. resume --------------------------------------------------------
    box = ob.Box(out, [], build=t.dir, cmode="keep", bootc=True, noa=True,
                 megs=megs)
    try:
        if not box.wait_pm(300):
            fails.append("the second boot never reached protected mode")
        else:
            box.up()                    # the desktop and its Resume card
            box.capture()
            box.goto(225, 225)          # Resume
            box.xd("click", 1)
            if not box.wait_resumed(120):
                fails.append("no `kern_mega: resumed`")
            box.ui_done("the resumed desktop")
            box.shot(os.path.join(out, "resumed.png"))
            box.click(140, 330, "MEGAHIB to the front")  # ...and a key
            box.xd("key", "a")
            if not box.wait_for("MEGAHIB: ok", 30):
                fails.append("MEGAHIB did not read its 64KB back: %s" % [
                    l for l in box.lines() if l.startswith("MEGAHIB")])
            if made:
                rows, _ = pool(box)
                gone = [(k, o, kb) for k, o, kb in made
                        if (rows[k][1], rows[k][2]) != (o, kb)]
                if gone:
                    fails.append("--full: filler records not back as they "
                                 "were: %s" % gone)
        lines = box.lines()
    finally:
        box.stop()
    for l in lines:
        if l.startswith("kern_mega FAULT") or l.startswith(
                "kern_mega CLOSED"):
            fails.append("resume: " + l)
    print("megahiber: %.0fs under Bochs" % (time.time() - t0))
    for f in fails:
        print("FAIL: " + f)
    if keep or fails:
        print("megahiber: the boots are in " + out)
    else:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
