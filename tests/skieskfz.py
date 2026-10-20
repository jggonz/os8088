#!/usr/bin/env python3
"""THE KERNEL HEARTBEAT AND THE PACKAGE WATCHDOG, TOGETHER (SPEC.md 8.9.1).

    make skiesdiag && python3 tests/skieskfz.py

Two instruments have to be readable at once on a machine that has hard
frozen: `KFZ=1`'s kernel heartbeat (SPEC.md 8.9), which says whether IRQ0
was masked, whether an EOI went missing and which side of the BIOS chain
the machine died on; and Clear Skies' own watchdog (SPEC.md 88.14), which
says where in a frame it stopped. They are painted by different code into
the same framebuffer, so nothing but a row that runs both says they fit.

Three claims, and the second is the one the field found:

  1. the heartbeat keeps painting INSIDE an fsx bracket - it is drawn from
     IRQ0, so a bracket that owns the video mode cannot silence it;
  2. the 30-second stuck report NEVER ARMS in one. `ui_task` does not run
     inside a bracket at all (SPEC.md 53.1), so "no pass in 30 seconds" is
     the defined state there - and the report forces the gfx lock open and
     draws into the KERNEL's framebuffer while the app owns the mode;
  3. the watchdog's own blocks stay CLEAN - three identical rows and a
     blank fourth - which is SPEC.md 88.14.3's move above the view holding
     against both the sim's blit and the heartbeat.

--clobber-stuck is the red run (docs/WRITING-TESTS.md 1): it pokes
[fsx_task] out of the ISR's sight by NOPping the compare, so the counter
climbs again and claim 2 fails at the threshold.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSD_ROWS, CSD_BLKS = 4, 9
KHB_STUCK = 546                 # kernel/sched.inc, ticks (30s)
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--apps", default="build/skiesdiag/apps360.img")
    ap.add_argument("--clobber-stuck", action="store_true",
                    help="the bracket test comes out of the ISR: red")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    if not os.path.exists(a.apps):
        print("  SKIP: %s - run `make skiesdiag` first" % a.apps)
        return 2
    # THE KERNEL IS A KNOB BUILD, so it goes in a private tree and never in
    # build/ (docs/plans/SOAK-PARALLEL.md 8): a KFZ kernel left in build/ makes
    # every other emulator row die saying "the map describes a DIFFERENT
    # kernel", which points at the kernel rather than at what you did.
    import os88build                                        # noqa: E402
    t = os88build.tree("KFZ=1").apply()      # ...and the symbol reader with it
    tdir = t.dir
    import os88marty                                        # noqa: E402
    import os88ui                                           # noqa: E402
    import dispapps                                         # noqa: E402
    import skiesdiag                                        # noqa: E402
    sym = skiesdiag.diagmap()
    print("    kernel %s, defines %s"
          % (os.path.relpath(tdir, ROOT), " ".join(t.defines)))

    def strip(m):
        return b"".join(m.read(0xB0000 + b, 80)
                        for b in (0, 0x2000, 0x4000, 0x6000))

    with os88marty.launch(os.path.join(tdir, "os8088-360.img"), apps=a.apps,
                          machine=a.machine, boot=False) as m:
        m.run()
        ui = os88ui.UI(m)
        # THE PICTURE GATE CANNOT BE USED: KHB_X is 0, so the strip is painted
        # ON the menu bar and `settle` reads "menu field 13% lit" against a
        # desktop's 93. That is the instrument working, not the machine
        # failing - so this takes the WORD gate and skips the picture one.
        ui.up(limit=180.0)
        os88marty.no_saver(m)
        if a.clobber_stuck:
            # `cmp byte [fsx_task], 0xFF / jne` - 8.9.1's whole gate
            lo = m.sym("sch_isr")
            code = m.read(lo, 0x400)
            j = code.find(b"\x80\x3E")
            hit = -1
            while j >= 0:
                if code[j + 4:j + 6] == b"\xFF\x75":
                    hit = j
                    break
                j = code.find(b"\x80\x3E", j + 1)
            if hit < 0:
                sys.exit("skieskfz: sch_isr does not gate the stuck report "
                         "the way this patch expects")
            m.pause()
            m.write(lo + hit, b"\x90" * 6)
            m.run()
            print("  (the bracket test is gone: must fail)")
        m.advance(frames=60)
        m.run()
        ui.path("B:/GAMES/SKIES.O88")
        slot, seg = dispapps.pkg_seg(m, 0)
        m.advance(frames=60)
        m.run()
        m.type_text("f")
        m.advance(frames=200)
        m.run()
        doff = [int.from_bytes(m.readseg(seg, sym["cs_doff"] + 2 * r, 2),
                               "little")
                for r in range(CSD_BLKS * CSD_ROWS)]

        def rows_of(b):
            return [(lambda x: (x[0] << 8) | x[1])(
                        m.read(0xB0000 + doff[b * CSD_ROWS + r], 2))
                    for r in range(CSD_ROWS)]

        def tick():
            return int.from_bytes(m.readseg(seg, sym["cs_dtick"], 2), "little")

        def kw(n):
            return int.from_bytes(m.read(m.sym(n), 2), "little")

        t0, s0 = tick(), strip(m)
        worst, mixed = 0, 0
        while tick() - t0 < KHB_STUCK + 150:
            m.advance(frames=120)
            m.run()
            m.pause()               # ...or a read races the next paint and
            worst = max(worst, kw("khb_stuck"))     # invents a mixture
            # A PAUSE CAN LAND INSIDE cs_diag_paint, which writes a block ROW
            # BY ROW - so one mixed reading is the sampler catching the ISR
            # mid-stride and not the blit. It resolves within a tick, and a
            # persistent one does not, so a suspect block is re-read once.
            # (Measured: 1 in ~50 samples on an idle lane, and the row failed
            # on exactly that under `os88test soak`.)
            suspect = [b for b in range(CSD_BLKS)
                       if not (lambda r: r[0] == r[1] == r[2] and r[3] == 0)
                       (rows_of(b))]
            if suspect:
                m.run()
                m.advance(frames=8)
                m.pause()
                for b in suspect:
                    r = rows_of(b)
                    if not (r[0] == r[1] == r[2] and r[3] == 0):
                        mixed += 1
            m.run()
        m.pause()
        s1 = strip(m)
        m.run()
        ticks = tick() - t0
        print("      %d ticks in the bracket, threshold %d; khb_stuck high "
              "water %d; %d mixed block readings" % (ticks, KHB_STUCK, worst,
                                                     mixed))
        check(ticks > KHB_STUCK,
              "the bracket outlived the report's threshold (%d > %d)"
              % (ticks, KHB_STUCK))
        check(s0 != s1,
              "the heartbeat is still painting inside the bracket")
        check(worst == 0,
              "...and the 30-second report NEVER ARMED in one (%d)" % worst)
        check(mixed == 0,
              "...and the watchdog's blocks are clean (%d mixed)" % mixed)
    if bad:
        for b in bad:
            print("FAIL: " + b)
        return 1
    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
