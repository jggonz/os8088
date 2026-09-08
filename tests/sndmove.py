#!/usr/bin/env python3
"""The Sound Blaster's DMA RING moves while the chip is idle (SPEC.md 66.6.4).

    make && make build/sndmove360.img && python3 tests/sndmove.py

THE LAST PINNED CLAIM. §66.4.2 established that `MC_DMA` says where a
claim may LAND and not whether it may move, and three of the four such claims
in the tree have no bus master on them at all. The fourth does: the 8KB ring is
programmed into the 8237's page and offset registers, and no relocation proc
can rewrite those mid-transfer.

`[drv_wcnt]` is the kernel's handle on that and it is exact in the direction
that matters - a stream lives only while its refill or drain task does
(§34.5), the chip is armed only while a stream is open, so zero means nothing
is armed. `mem_can_move` refuses any `MC_DMA` claim while it is non-zero.

WHAT THE DRIVER HAD TO ADD IS THREE WORDS AND THEY WERE ALREADY WRITTEN.
`sbl_ring_reloc` stores the new base and falls through into `sbl_dma_derive` -
the tail of `sbl_dma_map`, factored rather than copied - which recomputes
`[sbl_dmaoff]` and `[sbl_page]`, both a function of the base.

IT WANTS A SOUND BLASTER, which in a container means `os8088_5150_sb_gla`;
MartyPC models one, so this is not on CLAUDE.md's QEMU list.

THE ARENA IS BUILT OUT OF THE DRIVERS, and it has to be. Loaded at boot the
image sits at the ceiling with the ring packed under it, which is where it
belongs - so a correct compaction moves neither and a run would prove nothing.
So the row drops the sound driver, mounts the RAM disk into the ceiling,
brings sound back UNDERNEATH it and drops the RAM disk again, which is
docs/plans/HEAP-UNPIN-PLAN.md 2.0's own defect performed on purpose:

  [ low claims ][ filler's fill ][ 8K ][ ring ][ img ][ 21K hole ][ FILLER ]

...and the ring and image are then the ONLY thing between two free runs, so a
forcing ask bigger than either one and smaller than their sum can be answered
only by moving them. NO SPACER PACKAGE: the first version opened Paint the way
tests/regmove.py does, and Paint's region is claimed top-down, so it landed in
that same ceiling run and walled the ring off from the low arena - measured,
with a correct kernel moving nothing. And the filler is pressed with 'S',
which asks WITHOUT filling first: its fill is first fit ascending, so it lands
in that hole and pins it shut against the very block the ask needs moved.

SIX ASSERTIONS:

  1. the ring is there at all - one claim, owned by the driver's own segment,
     carrying a non-zero `MC_DMA` head;
  2. it is DECLARED movable - `MC_RLOC` out of the kernel's table, because a
     declaration the owner fence refused is silent from inside the driver;
  3. `[drv_wcnt]` is 0, so the chip is idle and the kernel may act on it - this
     is stated rather than assumed, because if a worker were live the refusal
     below would be the right answer and assertion 4 would prove nothing;
  4. THE RING MOVED, and 4b the IMAGE under it moved too - which is the IVT
     patch and `drv_tab` following, since the ring cannot outrun the image
     whose segment owns it;
  5. ...and `[sbl_seg]`, `[sbl_dmaoff]` and `[sbl_page]` inside the image
     agree with the base the kernel granted. THIS IS THE ONE THAT TESTS THE
     DRIVER: without `sbl_ring_reloc` every check above still passes and the
     next Play programs the 8237 from a stale page. Measured, with the proc
     storing the old base: 1 to 4b green, 5 red;
  5b. ...and no interrupt vector still names the old image, with as many
     naming the new one as named the old. That is the kernel's half of the
     same silence - a vector nobody patched is fine until the card raises its
     IRQ, and then the CPU is running bytes the compactor has re-let;
  6. the machine still draws afterwards.

AND IT HAS TO EARN ITS VECTOR. SOUND.DRV hooks its IRQ at the FIRST STREAM
OPEN and not at attach (`sbl_f_irqdisc`), and it stays hooked until
`snd_unhook` - so on a machine that has never made a sound nothing points into
the image and 5b would be vacuous. `SBTEST.O88` rides on the disk to open and
close one stream, AFTER the driver shuffle, an unmount being what unhooks.

WHAT IT DOES NOT COVER, said plainly: the refusal arm - that the ring does NOT
move while a stream is playing - needs a playing stream, which is Tracker's
harness (`tests/trkrate.py`) and not this one's.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88fixture                                      # noqa: E402
import os88geom                                         # noqa: E402
import os88marty as M                                   # noqa: E402
import os88sym                                          # noqa: E402
from os88mouse import Mouse                             # noqa: E402
import dispcp                                           # noqa: E402
import heaphi                                           # noqa: E402
import sheetmove                                        # noqa: E402
import trackmove                                        # noqa: E402

u16, claims, uncovered = sheetmove.u16, sheetmove.claims, sheetmove.uncovered
DISK = "build/sndmove360.img"
SND_ROW = 0                     # driver.inc: drv_tab row 0 is the sound driver


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_sb_gla")
    a = ap.parse_args()
    S = os88sym.linear
    os88fixture.need(DISK)

    with M.launch("build/os8088-360.img", apps=DISK,
                  machine=a.machine, boot=False) as m:
        m.run()
        M.settle(m, gate=M.desktop_up)
        M.no_saver(m)
        mo = Mouse(marty=m)

        def seg(r):
            return u16(m.read(S("drv_tab") + r * heaphi.DRVR_SZ
                              + heaphi.DRVR_SEG, 2))

        # THE HARDWARE IRQ SLOTS AND NOT ALL 256, because that is exactly what
        # the kernel promises to patch (SPEC.md 66.6.3.1) and the difference
        # is a machine: int C1h and int C3h on os8088_xt_hdd are SCRATCH WORDS
        # the XT-IDE option ROM keeps in unused vectors, one of them held a
        # value that was also a heap base, and a 256-entry patch rewrote it and
        # left the machine with no hard disk. A test that scanned all 256 here
        # would be asserting a promise the kernel must not make.
        IRQVEC = list(range(0x08, 0x10)) + list(range(0x70, 0x78))

        def ivt_names(want):
            t = m.read(0, 1024)
            return [v for v in IRQVEC if u16(t, v * 4 + 2) == want]

        # The sound driver is ALREADY UP on a machine with a card - measured:
        # drv_tab[0] reads 0x9E80 at the first desktop frame - so the first
        # version of this row went to the Drivers page and clicked row 0,
        # which UNLOADED it.
        if not seg(SND_ROW):
            print("FAIL: the sound driver is not loaded - no card on this "
                  "machine, or drv_tab's row order moved")
            return 1
        print("sound driver at %04x (at the CEILING, where it boots)"
              % seg(SND_ROW))

        # ...AND THAT IS WHY THE ROW HAS TO REBUILD THE ARENA. Loaded at boot,
        # the image sits at the ceiling with the ring packed immediately under
        # it: nothing is out of place, so a compaction correctly moves neither
        # and the run would prove nothing. So put something ABOVE them - drop
        # the sound driver, mount the RAM disk into the ceiling, bring sound
        # back underneath it, then drop the RAM disk again:
        #
        #   ceiling [ SND img ][ ring ] ...........   as it boots
        #   ceiling [ RAMDISK ][ SND img ][ ring ]    after the shuffle
        #   ceiling [  hole   ][ SND img ][ ring ]    ...and the hole to close
        mo.menu(8, 8, 8, 40)                    # chip menu -> Control Panel
        heaphi.quiet(m)
        cp = [w for w in heaphi.wins(m) if w[3] >= 280 and w[4] >= 100]
        if not cp:
            print("FAIL: no Control Panel")
            return 1
        cp = cp[-1]
        x0, y0 = cp[1] + 1, cp[2] + 18
        mo.click(x0 + 40,
                 y0 + heaphi.CP_I0Y + heaphi.CP_IDRV * heaphi.CP_IROWH + 7)
        heaphi.quiet(m)

        def drvrow(r):
            mo.click(x0 + heaphi.CP_RX + 40,
                     y0 + heaphi.CP_DBY1 + r * heaphi.CP_DROWH
                     + heaphi.CP_DROWH // 2)
            time.sleep(8)
            heaphi.quiet(m)

        drvrow(SND_ROW)                         # unmount sound
        if seg(SND_ROW):
            print("FAIL: the sound driver did not unmount")
            return 1
        drvrow(heaphi.RD_ROW)                   # the RAM disk takes the ceiling
        if not seg(heaphi.RD_ROW):
            print("FAIL: the RAM disk did not mount")
            return 1
        drvrow(SND_ROW)                         # ...and sound comes back under
        sndseg = seg(SND_ROW)
        if not sndseg:
            print("FAIL: the sound driver did not come back")
            return 1
        drvrow(heaphi.RD_ROW)                   # ...and the hole opens above it
        if seg(heaphi.RD_ROW):
            print("FAIL: the RAM disk did not unmount, so no hole opened")
            return 1
        mo.click(cp[1] + 8, cp[2] + 9)          # close the panel: CTRL.DRV is
        heaphi.quiet(m)                         # a module and would be a wall
        print("re-mounted under the RAM disk, which is now gone: sound at %04x"
              % sndseg)

        # --- force ------------------------------------------------------------
        dispcp.open_drive(m, mo, S, M.settle, "B")
        dslot = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
        mo.drag(wx + 60, wy + 9, wx + 60 + 215, wy + 9)
        M.settle(m)
        disk = dispcp.win_rect(m, S, dslot)[:2]

        def open_named(name, secs):
            before = set(w.i for w in os88geom.windows(m, S) if w.visible)
            dispcp.open_named(m, mo, S, M.settle, *disk, name=name)
            time.sleep(secs)
            M.settle(m)
            new = [w for w in os88geom.windows(m, S)
                   if w.visible and w.i not in before]
            if not new:
                print("FAIL: %s never opened a window" % name)
                raise SystemExit(1)
            return new[0]

        # --- ONE STREAM, OPENED AND CLOSED (for assertion 5b) ----------------
        # SOUND.DRV hooks its IRQ at the FIRST STREAM OPEN and not at attach
        # (sbl_f_irqdisc), and it stays hooked until snd_unhook - so a machine
        # that has never made a sound has NO vector into the image and the
        # vector check below would be vacuous on it. This is the state the IVT
        # patch actually exists for: a card that has played and is now idle.
        # It has to come AFTER the driver shuffle, because an unmount unhooks.
        sbw = open_named("SBTEST.O88", 8)
        mo.click(sbw.x + sbw.w // 2, sbw.y + sbw.h - 20)     # open the stream
        time.sleep(6)
        M.settle(m)
        mo.click(sbw.x + sbw.w // 2, sbw.y + sbw.h - 20)     # ...and close it
        time.sleep(4)
        M.settle(m)
        mo.click(sbw.x + 8, sbw.y + 9)                       # ...and the app
        M.settle(m)
        sndseg = seg(SND_ROW)                   # a claim of sbtest's could
        if not sndseg:                          # have moved the image already
            print("FAIL: the sound driver is gone after SBTEST")
            return 1
        print("a stream has been opened and closed: %d vector(s) into %04x"
              % (len(ivt_names(sndseg)), sndseg))

        bad = 0
        # sheetmove.claims() gives (base, para, own, rloc) and the DMA head is
        # what tells the ring from the staging pool, so the record is read here
        # rather than through it.
        MC_SIZE, MEM_MAX = os88geom.MC_SIZE, os88geom.MEM_MAX
        MC_DMA = os88geom.MC_DMA
        raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
        rings = []
        for i in range(MEM_MAX):
            r = raw[i * MC_SIZE:(i + 1) * MC_SIZE]
            base, para, own = u16(r, 0), u16(r, 2), u16(r, 4)
            if base and own == sndseg and u16(r, MC_DMA):
                rings.append((base, para, u16(r, 8), u16(r, MC_DMA)))
        print("  1 the ring is there        %s"
              % (", ".join("%04x %dKB dma-head %d para" % (b, p // 64, d)
                           for b, p, _, d in rings) if rings else
                 "NO claim owned by %04x carries an MC_DMA head" % sndseg))
        bad += not rings
        if not rings:
            print("VERDICT: 1 PROBLEM(S)")
            return 1
        base0, _, rloc, _ = rings[0]

        print("  2 declared movable         %s"
              % ("MC_RLOC=%04x" % rloc if rloc else
                 "NO  <-- OSAPI_MEM_MOVABLE was refused and the driver "
                 "cannot tell (SPEC.md 66.5.6.2)"))
        bad += not rloc

        wcnt = m.read(S("drv_wcnt"), 1)[0]
        print("  3 the chip is idle         %s"
              % ("drv_wcnt=0" if wcnt == 0 else
                 "drv_wcnt=%d <-- a stream is open, so a REFUSAL is the right "
                 "answer and assertion 4 would prove nothing" % wcnt))
        bad += wcnt != 0

        # NO SPACER PACKAGE, and that is the correction rather than an
        # omission. The first version of this row opened Paint the way
        # tests/regmove.py does - but Paint's REGION is claimed top-down, so
        # on this arena it landed in the largest ceiling run there was, which
        # is the one DIRECTLY UNDER the ring, and 33KB of pinned region then
        # stood between the ring and the low arena. Measured: the ring had
        # free space above it and a wall below, so packing it up merged
        # nothing and a correct compaction moved it nowhere.
        #
        #   [ low claims ][ fill ][ 8K ][ ring ][ img ][ hole ][ FILLER ]
        #
        # The filler alone gives that shape: its own claims cap the ceiling
        # hole, its fill takes the low arena down to FL_LEAVE, and the ring
        # and image are the only thing between the two runs.
        fl = open_named("FILLER.O88", 8)
        pt = uncovered(m, S, fl, prefer_title=True)
        if pt is None:
            print("FAIL: the filler's title bar is wholly covered")
            return 1
        mo.click(*pt)
        M.settle(m)

        # THE RING IS FOUND BY THE IMAGE'S CURRENT SEGMENT, never by the one
        # it booted at, and that is a real property rather than a convenience:
        # a claim's MC_OWN is the holder's segment, so when the image moves
        # `mem_region_reloc` rewrites the owner of every claim it held - the
        # ring included. Looking it up by the boot segment reported the ring
        # GONE on the run that first moved it.
        def ringbase(own):
            raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
            for i in range(MEM_MAX):
                r = raw[i * MC_SIZE:(i + 1) * MC_SIZE]
                if u16(r, 0) and u16(r, 4) == own and u16(r, MC_DMA):
                    return u16(r, 0)
            return None

        def dump(tag):
            base = u16(m.read(S("mem_base"), 2))
            top = u16(m.read(S("mem_top"), 2))
            raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
            rows = sorted((u16(raw, i * MC_SIZE), u16(raw, i * MC_SIZE + 2),
                           u16(raw, i * MC_SIZE + 4), u16(raw, i * MC_SIZE + 8),
                           raw[i * MC_SIZE + 10])
                          for i in range(MEM_MAX) if u16(raw, i * MC_SIZE))
            print("   -- %s" % tag)
            at = base
            for b, p_, o, r, hi in rows:
                if b > at:
                    print("      %6.1fK HOLE" % ((b - at) / 64.0))
                print("      %04x %5.1fK own %04x %s %s"
                      % (b, p_ / 64.0, o, "MOV" if r else "PIN",
                         "hi" if hi else "lo"))
                at = max(at, b + p_)
            if top > at:
                print("      %6.1fK HOLE (top)" % ((top - at) / 64.0))

        # THE VECTORS, BEFORE. SOUND.DRV is the only driver in the tree that
        # hooks one and it hooks up to five (SPEC.md 66.6.3.1), so the count is
        # read rather than assumed - and a zero here would make 5b vacuous.
        vec0 = ivt_names(sndseg)
        dump("before the forcing asks")
        for _ in range(6):
            # 'S' AND NOT 'A': ask, do not fill. The fill is first fit
            # ascending and the hole this row opened above the image is the
            # lowest run big enough, so a fill would seal it - pinned, and
            # against the very block the ask needs moved (tests/filler).
            m.key("KeyS")
            time.sleep(6)
            M.settle(m)
            if ringbase(seg(SND_ROW)) not in (base0, None):
                break
        dump("after")

        img = seg(SND_ROW)
        now = ringbase(img)
        moved = now is not None and now != base0
        print("  4 the ring MOVED           %s"
              % ("%04x -> %04x" % (base0, now) if moved else
                 "NO (%s) <-- the run proves nothing"
                 % ("%04x" % now if now else "no claim owned by %04x carries "
                    "an MC_DMA head" % img)))
        bad += not moved

        imoved = img and img != sndseg
        print("  4b ...and so did the IMAGE %s"
              % ("%04x -> %04x, so the IVT patch ran and drv_tab followed"
                 % (sndseg, img) if imoved else
                 "NO (%s) <-- the ring cannot outrun the image it sits under"
                 % ("%04x" % img if img else "GONE")))
        bad += not imoved

        # --- 5: THE DRIVER'S OWN THREE WORDS --------------------------------
        # The kernel moving the bytes is half the job; the other half is
        # `sbl_ring_reloc`, and nothing above would notice its absence - the
        # ring would sit at its new base with [sbl_seg] naming the old one,
        # silent until the next Play programmed the 8237 with a stale page and
        # offset and the card DMA'd out of somebody else's claim. So the three
        # words are read out of the image and checked against the base the
        # kernel actually granted, by the same arithmetic sbl_dma_derive does.
        D = trackmove.pkg_syms("drivers/sound/sound.asm",
                               ("drivers/sound/", "drivers/", "apps/"))

        def dw(n):
            return u16(m.read(img * 16 + D[n], 2))

        want = (now, (now & 0x0FFF) << 4, now >> 12) if now else None
        got = (dw("sbl_seg"), dw("sbl_dmaoff"),
               m.read(img * 16 + D["sbl_page"], 1)[0]) if img else None
        print("  5 the 8237's words followed %s"
              % ("sbl_seg=%04x dmaoff=%04x page=%02x" % got if got == want else
                 "STALE: %s, want %s  <-- sbl_ring_reloc did not run, and the "
                 "next Play would DMA out of somebody else's claim"
                 % (["%04x" % v for v in got] if got else got,
                    ["%04x" % v for v in want] if want else want)))
        bad += got != want

        # --- 5b: THE INTERRUPT VECTOR TABLE ---------------------------------
        # The fourth address space, and the only one outside the kernel. It is
        # asserted here rather than trusted because a stale vector is silent
        # until the card raises its IRQ - and then the CPU is executing bytes
        # the compactor has already handed to somebody else.
        vec1 = ivt_names(img) if img else []
        stale = ivt_names(sndseg)
        ok5b = bool(vec0) and not stale and len(vec1) == len(vec0)
        print("  5b the vectors followed    %s"
              % ("%s -> %s" % (["%02x" % v for v in vec0],
                               ["%02x" % v for v in vec1]) if ok5b else
                 "vectors naming the OLD image %s, the new %s (before: %s)"
                 % (["%02x" % v for v in stale], ["%02x" % v for v in vec1],
                    ["%02x" % v for v in vec0])
                 + ("  <-- nothing pointed into the image, so this proves "
                    "nothing" if not vec0 else "")))
        bad += not ok5b

        alive = True
        try:
            M.settle(m)
        except M.MartyError:
            alive = False
        print("  6 the machine still draws  %s" % ("OK" if alive else "NO"))
        bad += not alive

        print("VERDICT:", "OK" if not bad else "%d PROBLEM(S)" % bad)
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
