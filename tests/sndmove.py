#!/usr/bin/env python3
"""The Sound Blaster driver's IMAGE moves while the card is idle (SPEC.md 66.6.3).

    make && make build/sndmove360.img && python3 tests/sndmove.py

SOUND.DRV is the only driver that hooks an interrupt vector, so its image is
the one a compaction has to follow into the IVT (SPEC.md 66.6.3.1) - and an
idle card is the state that has to be safe, because the vector stays hooked
from the first stream open until `snd_unhook`.

THIS ROW USED TO BE ABOUT THE RING. An 8KB `MC_DMA` claim was taken at attach
and held for the session, and the row moved it and checked `[sbl_seg]`,
`[sbl_dmaoff]` and `[sbl_page]` followed. SPEC.md 34.5.2 retired that claim: a
ring stream the card can reach is played straight out of the staging pool, and
the double buffer is claimed per stream and freed with it. So an idle card
holds NOTHING but its image, and assertion 1 says exactly that - a claim still
owned by the driver after the stream closed is a leak. The refusal arm (the
pool does not move while the card is playing out of it) needs a playing stream
and a compaction under it, which is `tests/trackmove.py` checks 8 and 9.

IT WANTS A SOUND BLASTER, which in a container means `os8088_5150_sb_gla`;
MartyPC models one, so this is not on CLAUDE.md's QEMU list.

THE ARENA IS BUILT, and it has to be. Loaded at boot the image sits at the
ceiling, which is where it belongs - so a correct compaction moves nothing and
a run would prove nothing. So the row drops the sound driver, opens SBTEST
into the ceiling it left (a package region is claimed top-down), brings sound
back UNDERNEATH it and closes SBTEST again, which is
docs/plans/HEAP-UNPIN-PLAN.md 2.0's own defect performed on purpose. It was
the RAM disk until the 8KB ring stopped being claimed at attach: the image
alone leaves a 7KB hole, and the RAM disk's 9KB image then lands lower:

  [ low claims ][ filler's fill ][ img ][ hole ][ FILLER ]

...and a forcing ask can then be answered only by moving the image. The
filler is pressed with 'S', which asks WITHOUT filling first: its fill is first
fit ascending, so it would land in that hole and pin it shut.

FIVE ASSERTIONS:

  1. an idle card holds no claim but its image - the lazy buffer and the pool
     both went with the stream and the app that held them (SPEC.md 34.5.2);
  3. `[drv_wcnt]` is 0 - no worker, so nothing pins the image;
  4b. THE IMAGE MOVED, so `drv_tab` followed;
  5b. ...and no interrupt vector still names the old image, with as many
     naming the new one as named the old. A vector nobody patched is fine
     until the card raises its IRQ, and then the CPU is running bytes the
     compactor has re-let;
  6. the machine still draws afterwards.

The numbering keeps the old row's, so a log from either reads the same.

AND IT HAS TO EARN ITS VECTOR. SOUND.DRV hooks its IRQ at the FIRST STREAM
OPEN and not at attach (`sbl_f_irqdisc`) - so on a machine that has never made
a sound nothing points into the image and 5b would be vacuous. `SBTEST.O88`
rides on the disk to open and close one stream, AFTER the driver shuffle, an
unmount being what unhooks.

WHAT FORCES THE MOVE is the filler's own fill: `fl_fill` claims
`OSAPI_MEM_AVAIL`, `mem_avail` plans both passes (SPEC.md 66.4.3.1) and
`mem_claim` delivers both, so the pack has usually happened before the first
key. That is why `vec0` is read before the filler launches, paired with
`sndseg`, the pre-move base - read after, it names a segment nothing points
into any more and 5b reports a pass that proves nothing.
"""
import argparse
import os
import sys

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
        # the image sits at the ceiling: nothing is out of place, so a
        # compaction correctly moves nothing and the run would prove nothing.
        # So put something ABOVE it - drop the sound driver, open SBTEST into
        # the ceiling it left (a package REGION is claimed top-down), bring
        # sound back underneath, then close SBTEST:
        #
        #   ceiling [ SND img ] ...........   as it boots
        #   ceiling [ SBTEST  ][ SND img ]    after the shuffle
        #   ceiling [  hole   ][ SND img ]    ...and the hole to close
        #
        # It was the RAM disk, and that stopped working the day the driver's
        # 8KB ring stopped being claimed at attach (SPEC.md 34.5.2): the image
        # alone leaves a 7KB hole, the RAM disk's image is 9KB and lands lower,
        # and sound then goes straight back to the ceiling it came from.
        # SBTEST fits, and it is on the disk anyway to hook the vector.
        def panel():
            mo.menu(8, 8, 8, 40)                # chip menu -> Control Panel
            heaphi.quiet(m)
            cps = [w for w in heaphi.wins(m) if w[3] >= 280 and w[4] >= 100]
            if not cps:
                print("FAIL: no Control Panel")
                raise SystemExit(1)
            cp = cps[-1]
            x0, y0 = cp[1] + 1, cp[2] + 18
            mo.click(x0 + 40,
                     y0 + heaphi.CP_I0Y + heaphi.CP_IDRV * heaphi.CP_IROWH + 7)
            heaphi.quiet(m)
            return cp, x0, y0

        def drvrow(r):
            cp, x0, y0 = panel()
            was = seg(r)
            mo.click(x0 + heaphi.CP_RX + 40,
                     y0 + heaphi.CP_DBY1 + r * heaphi.CP_DROWH
                     + heaphi.CP_DROWH // 2)
            try:                                # a load is a floppy read,
                M.until(m, lambda _: seg(r) != was,     # which a screen
                        "driver row %d to (un)mount" % r,   # settle takes
                        poll=0.25, limit=60)                # for "done"
            except M.MartyError:
                pass                            # ...judged by the caller
            heaphi.quiet(m)
            try:
                mo.click(cp[1] + 8, cp[2] + 9)  # close the panel: CTRL.DRV is
            except M.MartyError:                # a module and would be a wall
                # A pointer that stops taking packets here has been seen once
                # in a soak (right after the re-mount) and not in 14 runs
                # since, so say what the machine was doing rather than only
                # that the arrow did not move: an IMR with bit 4 set is the
                # serial mouse's IRQ left masked by the driver's attach.
                st = m.status()
                print("  pointer stuck: %04X:%04X ui_idle=%s imr=%02X "
                      "lock=%d evq=%d btn=%d ticks=%d xy=%s sound at %04x"
                      % (st["cs"], st["ip"], M.ui_idle(m), m.inb(0x21),
                         m.read(S("gfx_lock_flag"), 1)[0],
                         m.read(S("evq_count"), 1)[0],
                         m.read(S("mouse_btn"), 1)[0], M._ktick(m),
                         mo.where(), seg(r)), flush=True)
                raise
            heaphi.quiet(m)

        drvrow(SND_ROW)                         # unmount sound
        if seg(SND_ROW):
            print("FAIL: the sound driver did not unmount")
            return 1

        # --- SBTEST into the ceiling -----------------------------------------
        dispcp.open_drive(m, mo, S, M.settle, "B")
        dslot = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
        mo.drag(wx + 60, wy + 9, wx + 60 + 215, wy + 9)
        M.settle(m)
        disk = dispcp.win_rect(m, S, dslot)[:2]

        def open_named(name, secs):
            before = set(w.i for w in os88geom.windows(m, S) if w.visible)
            dispcp.open_named(m, mo, S, M.settle, *disk, name=name)
            try:
                M.until(m, lambda _: any(w.visible and w.i not in before
                                         for w in os88geom.windows(m, S)),
                        "%s's window" % name, poll=0.25, limit=secs * 10)
            except M.MartyError:
                pass                            # ...reported just below
            M.settle(m)
            new = [w for w in os88geom.windows(m, S)
                   if w.visible and w.i not in before]
            if not new:
                print("FAIL: %s never opened a window" % name)
                raise SystemExit(1)
            return new[0]

        sbw = open_named("SBTEST.O88", 8)
        drvrow(SND_ROW)                         # ...and sound comes back under
        if not seg(SND_ROW):
            print("FAIL: the sound driver did not come back")
            return 1
        print("re-mounted under SBTEST: sound at %04x" % seg(SND_ROW))

        # --- ONE STREAM, OPENED AND CLOSED (for assertion 5b) ----------------
        # SOUND.DRV hooks its IRQ at the FIRST STREAM OPEN and not at attach
        # (sbl_f_irqdisc), and it stays hooked until snd_unhook - so a machine
        # that has never made a sound has NO vector into the image and the
        # vector check below would be vacuous on it. This is the state the IVT
        # patch actually exists for: a card that has played and is now idle.
        # It has to come AFTER the driver shuffle, because an unmount unhooks.
        def snd_held():
            """Heap claims the sound driver owns - its ring, its grant."""
            MC_SIZE, MEM_MAX = os88geom.MC_SIZE, os88geom.MEM_MAX
            raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
            return [i for i in range(MEM_MAX)
                    if u16(raw, i * MC_SIZE) and
                    u16(raw, i * MC_SIZE + 4) == seg(SND_ROW)]

        mo.click(sbw.x + sbw.w // 2, sbw.y + sbw.h - 20)     # open the stream
        try:        # the open hooks the card's vector (sbl_f_irqdisc)...
            M.until(m, lambda _: ivt_names(seg(SND_ROW)),
                    "the stream to open", poll=0.1, limit=30)
        except M.MartyError:
            pass                        # ...5b below says so
        M.guest_sleep(m, 2.5)           # ...and SBTEST's tone is 2 s: played
        mo.click(sbw.x + sbw.w // 2, sbw.y + sbw.h - 20)     # ...and close it
        try:        # the close frees the grant and the ring, and the stream's
            M.until(m, lambda _: not snd_held()          # task goes (1 and 3
                    and not m.read(S("drv_wcnt"), 1)[0],  # below read them;
                    "the stream to close", poll=0.1,     # an idle box's
                    guest=4 * M.GUEST_PACE)              # pause is the bound)
        except M.MartyError:
            pass
        mo.click(sbw.x + 8, sbw.y + 9)                       # ...and the app:
        M.settle(m)                                          # the hole opens
        sndseg = seg(SND_ROW)                   # a claim of sbtest's could
        if not sndseg:                          # have moved the image already
            print("FAIL: the sound driver is gone after SBTEST")
            return 1
        # THE VECTORS, BEFORE - AND HERE, not after the filler launches.
        # SOUND.DRV is the only driver in the tree that hooks one and it hooks
        # up to five (SPEC.md 66.6.3.1), so the count is read rather than
        # assumed, and a zero makes 5b vacuous.
        #
        # IT USED TO BE READ AFTER THE FILLER AND THAT STOPPED WORKING, for a
        # reason that is the kernel telling the truth rather than a defect
        # (SPEC.md 66.4.3.1): fl_fill claims OSAPI_MEM_AVAIL, mem_avail plans
        # BOTH passes now, and mem_claim delivers both - so the filler's own
        # fill is what packs the ceiling and moves the image, before this row
        # ever presses a key. A vec0 taken after it names a segment nothing
        # points into any more and 5b reported "nothing pointed into the
        # image" while every other check passed. Taking it here keeps it
        # paired with `sndseg`, which has been the pre-move base all along -
        # re-reading THAT instead is the fix that looks right and is not, and
        # it moves the failure to 4b.
        vec0 = ivt_names(sndseg)
        print("a stream has been opened and closed: %d vector(s) into %04x"
              % (len(vec0), sndseg))

        bad = 0
        # --- 1: AN IDLE CARD HOLDS NOTHING BUT ITS IMAGE (SPEC.md 34.5.2) ----
        # This assertion used to be "the ring is there": an 8KB MC_DMA claim
        # taken at attach and held for the session, and the row moved it. The
        # ring is claimed per double-buffered stream now and freed with it, and
        # a ring stream the card can reach is played straight out of the pool -
        # so a card that has played and gone quiet holds NO claim at all, and
        # a stray one here is the lazy buffer or the pool leaking past a close.
        MC_SIZE, MEM_MAX = os88geom.MC_SIZE, os88geom.MEM_MAX
        raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
        held = []
        for i in range(MEM_MAX):
            r = raw[i * MC_SIZE:(i + 1) * MC_SIZE]
            base, para, own = u16(r, 0), u16(r, 2), u16(r, 4)
            if base and own == sndseg:
                held.append((base, para))
        print("  1 idle, nothing held       %s"
              % ("OK" if not held else
                 ", ".join("%04x %dKB" % (b_, p_ // 64) for b_, p_ in held)
                 + "  <-- still owned by the driver after the stream closed"))
        bad += bool(held)

        wcnt = m.read(S("drv_wcnt"), 1)[0]
        print("  3 the chip is idle         %s"
              % ("drv_wcnt=0" if wcnt == 0 else
                 "drv_wcnt=%d <-- a worker is alive, so the image is PINNED "
                 "and assertion 4b would prove nothing" % wcnt))
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

        dump("after the filler's fill, which is now the forcing event")
        # ...AND THE KEYS ARE A FOLLOW-UP rather than the force. The filler's
        # own fill asks for mem_avail and mem_avail plans both passes, so the
        # pack has usually happened by the time this loop starts; the loop
        # stays because it costs one read when it has, and because a machine
        # whose fill did NOT reach the ceiling still gets its force.
        for _ in range(6):
            # 'S' AND NOT 'A': ask, do not fill. The fill is first fit
            # ascending and the hole this row opened above the image is the
            # lowest run big enough, so a fill would seal it - pinned, and
            # against the very block the ask needs moved (tests/filler).
            m.key("KeyS")
            try:
                M.until(m, lambda _: seg(SND_ROW) not in (sndseg, 0),
                        "the sound image to move", poll=0.25,
                        guest=6 * M.GUEST_PACE)
            except M.MartyError:
                pass                            # ...ask again
            M.settle(m)
            if seg(SND_ROW) not in (sndseg, 0):
                break
        dump("after")

        img = seg(SND_ROW)
        imoved = img and img != sndseg
        print("  4b the IMAGE moved         %s"
              % ("%04x -> %04x, so the IVT patch ran and drv_tab followed"
                 % (sndseg, img) if imoved else
                 "NO (%s) <-- the run proves nothing"
                 % ("%04x" % img if img else "GONE")))
        bad += not imoved

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
