#!/usr/bin/env python3
"""Tracker asks for the room before it refuses (SPEC.md 66.4.3, 45.3.1).

    make && make build/trackmove360.img && python3 tests/trkcompact.py

THE REFUSAL IS THE SUBJECT, not the load. Tracker's requirement is EXACT -
this module or no module - and `trk_fdone` asks `OSAPI_MEM_AVAIL` before it
stops the music, frees the old blob or turns the drive, precisely so that a
"Too big for free memory" costs nothing. What that refusal could not say
until SPEC.md 66.4.3 is whether the machine could have FOUND the room: a
package may not compact its own region from inside its own callback, so the
largest run it is quoted is the largest run with itself standing in the
middle of the heap.

So the scenario builds exactly that heap, and it builds it out of TRACKER
ITSELF - instances stacked down from the ceiling until the floor run is
smaller than the module, then the TOPMOST one closed so the survivor above
has a hole it could move into and nothing like enough underneath it.

IT COUNTS RATHER THAN ASSUMES, which is what makes the arithmetic hold on a
machine this was not measured on: each instance takes one region off the
floor run, so the first N whose floor run is under the module's size is also
guaranteed to be within one region OF it - which is exactly the window the
feature lives in, and the loop's stopping rule proves it rather than hoping
for it. A package region is a top-down claim (SPEC.md 50.3.2), so the stack
grows DOWN from the ceiling and the one hole in the arena is the floor.

Four assertions, and the first is the one that makes the other three mean
something:

  1. TRACKER POSTED. `[trk_cpq]` is seen non-zero, which only `trk_cpq_try`
     writes and only after plain `OSAPI_MEM_AVAIL` has said no and
     `OSAPI_MEM_COMPACT`'s what-if has said "not with you where you are". Without
     this the run proves nothing: a load that simply fitted would take the
     same path to the same green.
  2. The status line said so - `[tui_msgp]` is `trk_s_cpq` - because this
     callback returns INTO a compaction that freezes the machine, and a
     sentence that arrives after the freeze is a sentence nobody reads.
  3. THE MODULE LOADED. Same file, same heap, and the answer went from a
     refusal to a playing module.
  4. Nothing was destroyed on the way: every other instance is still there,
     and the attempt closed its own flag so the next load may ask again.
"""
import sys, os, time, argparse
# THIS TREE'S root, DERIVED - never a hard-coded path. A literal is right in the
# checkout it was written in and wrong in a git worktree, which is how parallel
# work is done here: os88sym re-assembles ROOT/kernel/kernel.asm and compares it
# against ROOT/build/kernel.bin, so a literal ROOT answers about a DIFFERENT
# kernel from the image being booted.
_OS88_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tools"))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tests"))
import os88fixture                                       # noqa: E402
import os88ui, os88geom, os88marty, dispcp               # noqa: E402
from trackmove import pkg_syms, u16                      # noqa: E402
from heapcheck import claims                             # noqa: E402

MOD, PKG = "BEVERLY.MOD", "TRACKER.O88"
MAXTRK = 10                     # INST_MAX is 12 and the Disk window owns one
MEM_PG_MIN, MEM_PG_MAX = 0xFB, 0xFE     # the purge tiers (SPEC.md 50.6.4)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    a = ap.parse_args()

    P = pkg_syms("apps/tracker/tracker.asm")
    os88fixture.need("build/trackmove360.img")
    modsz = os.path.getsize("apps/tracker/beverly.mod")
    needk = (modsz + 1023) // 1024

    with os88ui.boot("build/os8088-360.img", apps="build/trackmove360.img",
                     machine=a.machine) as ui:
        m = ui.m
        S = ui.sym                      # os88ui's own resolver, so a row that
                                        # names a kernel word and one that
                                        # clicks a window agree about which
                                        # kernel they are talking to

        def trackers():
            """Every live Tracker as (W_SEG, window), ceiling first."""
            out = []
            for w in os88geom.windows(m, S):
                if not w.visible or not w.title.startswith("Tracker"):
                    continue
                seg = u16(m.read(os88geom.winptr(m, w.i, S)
                                 + os88geom.W_SEG, 2))
                out.append((seg, w))
            return sorted(out, reverse=True)

        def layout(tag):
            """Print the arena and answer the largest run in KB, PURGEABLE
            CLAIMS COUNTED AS FREE - which is the number a package is quoted
            (SPEC.md 50.6.4: an ordinary claim outranks every cache and
            mem_claim sheds them all on its way down), and it is the reason
            the first shape of this row read a 106 KB floor and watched the
            load simply fit: 74 KB of disk cache sat in it.

            The REGIONS are deliberately not modelled as movers. Every one
            belongs to an instance with a LIVE worker, so mem_frameless pins
            it until a compaction parks it - a bare mem_avail moves none of
            them - and the one that does move is the asker's own, which is
            what the precondition below adds by hand.

            A movable BOTTOM-UP claim is another matter, and the second shape
            of this row got it wrong the way the first got the caches wrong:
            the two 3 KB floor claims (the Disk window's and a kernel tag's)
            sit above 37 KB of purgeable cache and PACK DOWN over it, which
            plain OSAPI_MEM_AVAIL plans (SPEC.md 66.10.3) and mem_claim then
            does. Modelled as barriers they read a 95 KB floor where the
            kernel - and the machine, which loaded the 114 KB module INTO
            that floor with no region moving - read 132. So they pack here
            too, and the stopping rule stops one instance later."""
            base = u16(m.read(S("mem_base"), 2))
            top = u16(m.read(S("mem_top"), 2))
            print("  -- %s --" % tag)
            fill, big = base, 0

            def gap(to):
                nonlocal big
                if to > fill:
                    big = max(big, to - fill)
                    print("       %5d KB FREE" % ((to - fill) // 64))
            for bs, pa, ow, dma, rl in claims(m, S):
                if MEM_PG_MIN <= (ow >> 8) <= MEM_PG_MAX:
                    print("  %04x %5d KB cache rank %02x  (counts as free)"
                          % (bs, pa // 64, ow >> 8))
                    continue                # shed by any ordinary claim
                if rl and not (dma & 0x8000):
                    print("  %04x %5d KB owner %04x     MOVABLE  (packs to %04x)"
                          % (bs, pa // 64, ow, fill))
                    fill += pa              # a floor mover: the ascending
                    continue                # pass slides it onto the fill
                                            # point, so it opens no gap
                gap(bs)
                print("  %04x %5d KB owner %04x%s%s"
                      % (bs, pa // 64, ow, " HI" if dma & 0x8000 else "   ",
                         "  MOVABLE" if rl else ""))
                fill = max(fill, bs + pa)
            gap(top)
            print("     largest run %d KB, module wants %d KB"
                  % (big // 64, needk))
            return big // 64

        ui.open_drive("B")
        run = None
        for i in range(MAXTRK):
            ui.open(PKG)
            run = layout("%d instance(s)" % (i + 1))
            if run < needk:
                break
        else:
            print("FAIL: %d instances and the floor run is still %d KB - this"
                  " machine has more heap than the test can fill" % (MAXTRK,
                                                                     run))
            return 1
        # --- ONE MORE, and then TWO come off the ceiling ---------------------
        # One region of room is not enough once the floor is modelled right.
        # With the two floor claims packing, eight instances leave 132 KB and
        # the 114 KB module simply fits (no post to observe); nine leave 83,
        # and the posted pass then has EIGHT workers to stand up inside
        # INST_PARKW's four ticks - which this machine does not manage
        # (measured: four of the eight parked, the pass delivered 83 KB, and
        # Tracker refused the load it had promised itself). So the stack goes
        # one deeper and TWO regions come off the ceiling: the asker drags
        # SEVEN workers behind it, the count the row has always passed with,
        # into 98 KB of room.
        if i + 1 >= MAXTRK:
            print("FAIL: %d instances is the row's limit and the floor only"
                  " went under %d KB at the last one" % (MAXTRK, needk))
            return 1
        ui.open(PKG)
        layout("%d instance(s), one past the floor" % (i + 2))
        tk = trackers()
        nopen = len(tk)
        print("  trackers at %s" % " ".join("%04x" % s for s, _ in tk))

        rgnkb = sum(c[1] // 64 for c in claims(m, S)
                    if c[0] in (tk[0][0], tk[1][0]))
        ui.close(tk[0][1])
        ui.close(tk[1][1])
        ui.settle()
        run2 = layout("...and the top two closed")
        if run2 >= needk:
            print("FAIL: the floor run is %d KB and the module wants %d - the"
                  " load would simply fit" % (run2, needk))
            return 1
        if run2 + rgnkb < needk:
            print("FAIL: %d KB of floor plus %d KB of regions is still under the"
                  " module's %d - no compaction could fund this load"
                  % (run2, rgnkb, needk))
            return 1
        print("     PRECONDITION: %d KB free, %d KB if the asker moves too,"
              " module wants %d" % (run2, run2 + rgnkb, needk))

        tk = trackers()
        seg0, win = tk[0]
        ui.raise_window(win)

        def wseg():
            """THE LOADING INSTANCE'S SEGMENT, RE-READ EVERY TIME. Its region
            is what the posted compaction moves, so a byte polled at the base
            it had before the post is read out of whatever landed there - and
            the failure that makes is a FALSE PASS, not a miss. One word off
            its own window record, because this is on a 50ms poll."""
            return u16(m.read(os88geom.winptr(m, win.i, S)
                              + os88geom.W_SEG, 2))

        # --- File > Load, by the keyboard Tracker publishes for it ----------
        m.key("KeyL")
        os88marty.settle(m, limit=120)
        dlg = next((x for x in reversed(
            [w for w in os88geom.windows(m, S) if w.visible])
            if x.title in ("Open", "Save As")), None)
        if dlg is None:
            print("FAIL: 'L' put no file dialog up")
            return 1
        rows = [r[0] for r in dispcp.snapshot(m, S)]
        if MOD not in rows:
            print("FAIL: %s is not listed - %r" % (MOD, rows))
            return 1
        for _ in range(rows.index(MOD) + 1):
            m.key("ArrowDown")
            time.sleep(0.2)
        got = u16(m.read(S("fdlg_sel"), 2))
        if got != rows.index(MOD):
            print("FAIL: dialog selected row %d, wanted %d"
                  % (got, rows.index(MOD)))
            return 1
        m.key("Enter")

        # --- and now WATCH: the post is up for the whole compaction and the
        # 114KB floppy read behind it, so this cannot miss it by being slow --
        posted = said = False
        for _ in range(400):            # 20s, where the byte is up for the
            at = wseg()                 # whole pass AND the 114KB floppy read
            if m.read(at * 16 + P["trk_cpq"], 1)[0]:
                posted = True
            if u16(m.read(at * 16 + P["tui_msgp"], 2)) == P["trk_s_cpq"]:
                said = True
            if posted and said:
                break
            time.sleep(0.05)
        os88marty.settle(m, limit=180)
        for _ in range(20):             # belt only: the settle above already
            if m.read(wseg() * 16 + P["mp_loaded"], 1)[0]:
                break                   # covers the pass and the 114KB read,
            time.sleep(0.5)             # both of which repaint
        layout("after the load")

        seg2 = wseg()                   # ...and every read below is through
        if not seg2:                    # the region where it ended up
            print("FAIL: the loading Tracker's window vanished")
            return 1

        def tw(name):
            return u16(m.read(seg2 * 16 + P[name], 2))

        def tb(name):
            return m.read(seg2 * 16 + P[name], 1)[0]

        bad = 0
        print("  1 Tracker posted      %s  (region %04x -> %04x)"
              % ("OK" if posted else "NO - it never asked", seg0, seg2))
        bad += not posted
        print("  2 it said so          %s" % ("OK" if said else
                                              "NO - the freeze was silent"))
        bad += not said
        loaded = tb("mp_loaded") != 0 and tw("trk_modseg") != 0
        msg = tw("tui_msgp")
        said_what = next((k for k, v in P.items()
                          if v == msg and k.startswith("trk_s_")), "+%04x" % msg)
        print("  3 module loaded       %s  (modseg %04x, status %s)"
              % ("OK" if loaded else "REFUSED", tw("trk_modseg"), said_what))
        bad += not loaded
        left = len(trackers())
        clear = tb("trk_cpq") == 0
        print("  4 survivors / flag    %d instance(s), [trk_cpq]=%d"
              % (left, tb("trk_cpq")))
        bad += left != nopen - 2
        bad += not clear
        print("VERDICT:", "OK" if not bad else "%d PROBLEM(S)" % bad)
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
