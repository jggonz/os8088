#!/usr/bin/env python3
"""The whole dance: a 397KB module opens where the same heap refused it.

    make && make build/trkbig.img && python3 tests/trkbigmod.py

THIS IS THE SCENARIO THE FEATURE WAS ASKED FOR, driven step for step, and it
is here beside `tests/trkcompact.py` rather than instead of it because the
two answer different questions. That one builds its heap out of Tracker
instances, which is what a gate can build repeatably and which nobody would
ever do. This one is a machine somebody sits at:

  boot 640K Hercules with SOUND.DRV NOT mounted -> Sheet -> Paint -> Clear
  Skies -> mount SOUND.DRV from the Control Panel -> Tracker -> close the
  three -> open a 397KB .MOD

Every step is a thing a user does, and what it produces is the heap SPEC.md
66.4.3 exists for:

    [ 365 KB free ][ Tracker 49 ][ sound pool 8 ][ SOUND.DRV 6 ][ 92 KB free ]

Two free runs with THE ASKER'S OWN REGION between them. Plain
`OSAPI_MEM_AVAIL` may not move that region (SPEC.md 66.6.1 - a frame is
standing in it), so it reports 365 against a 397 KB requirement and the load
is refused; `OSAPI_MEM_COMPACT`'s what-if reports what the machine could have had,
Tracker posts, and the descending pass packs all three top-down claims into
the ceiling hole - **by exactly the same 92 KB each** - leaving one 457 KB
run for the claim. That is HEAP-UNPIN-PLAN 2.0's mount-mid-session wall and
66.4.3's pin measured as one thing.

Five assertions:

  1. The machine really does boot with SOUND.DRV UNMOUNTED. It is the step
     the machine list had to be extended for - the boot overlay sniffs 388h
     and wants the sound row when an OPL2 answers, so a machine with an
     AdLib in it has the driver up before the first paint and step 1 is not
     a state it has.
  2. It MOUNTS mid-session, under three regions that already hold the
     ceiling. Without this the driver is not a wall and the scenario is a
     different one.
  3. THE PRECONDITION: after the three close, the largest run is under the
     module's size and the top-down claims above it are worth more than the
     difference. Both halves fail loudly, because a run that simply fitted
     would reach the same green having tested nothing.
  4. TRACKER POSTED - `[trk_cpq]` seen set, which only `trk_cpq_try` writes
     and only after plain `OSAPI_MEM_AVAIL` has already said no. This is what
     says the pre-feature build refused: that arm's code is the same up to
     the call.
  5. The module LOADED and is playing.

The module is `tools/os88mkmod.py`'s, generated at an exact length: the sizes
that show this are hundreds of KB and every real module is somebody's file.
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
import os88build                                         # noqa: E402
import os88ui, os88geom, os88marty as M, dispcp          # noqa: E402
from trackmove import pkg_syms, u16                      # noqa: E402
from heapcheck import claims                             # noqa: E402

DISK = "build/trkbig.img"
MOD = "BIGMOD.MOD"
MEM_PG_MIN, MEM_PG_MAX = 0xFB, 0xFE     # the purge tiers (SPEC.md 50.6.4)
MC_DMA_HI = 0x8000                      # ...and the top-down door (50.3.2)
# ctrl.inc's item list and the Drivers pane, as tests/drvup.py reads them
CP_I0Y, CP_IROWH, CP_IDRV, CP_RX, TITLE_H = 6, 14, 2, 96, 18
CP_DBY1, CP_DROWH = 20, 26
DRVR_SZ, DRVR_SEG = 16, 2
SND_ROW = 0                             # drv_tab row 0 is the sound driver
APPS = ("SHEET.O88", "PAINT.O88", "SKIES.O88")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_sb_gla_144")
    a = ap.parse_args()

    P = pkg_syms("apps/tracker/tracker.asm")
    os88fixture.need(DISK)
    # **THROUGH `os88build.at()` AND NOT AS A LITERAL** (docs/WRITING-TESTS.md
    # 70). A soak reads a FROZEN TREE, and `os88fixture.need` above has just
    # made the disk THERE - so a raw `build/` path reads the operator's
    # directory, which on a machine that has never built this fixture by hand
    # does not have the file at all. `os88ui.boot` resolves its own arguments;
    # this was the one path the row opened itself.
    modsz = os.path.getsize(os88build.at("build/bigmod.mod"))
    needk = (modsz + 1023) // 1024
    print("== %s is %d bytes = %d KB ==" % (MOD, modsz, needk))

    # **`boot=` AND NOT A SETTLE** (docs/WRITING-TESTS.md 59). This row's apps
    # disk carries a 397KB module, and the desktop enumerates it on the way up
    # - so `settle`'s "the screen stopped changing" is asked of a machine that
    # is legitimately still working, and it spent 724 GUEST seconds failing to
    # see stillness on a machine that had reached a desktop long before.
    # MEASURED: the desktop is up and byte-identical across four 3-second
    # rounds at 45 guest seconds, on this machine with this disk.
    with os88ui.boot("build/os8088.img", apps=DISK, machine=a.machine,
                     boot=45) as ui:
        m, mo, S = ui.m, ui.mo, ui.sym
        bad = 0

        def layout(tag):
            """The arena, PURGEABLE CLAIMS COUNTED AS FREE - which is what a
            package is quoted, and what the first shape of tests/trkcompact.py
            got wrong.

            Answers (largest run, free KB ABOVE it), and the second is the
            exact gain rather than a proxy for it: the descending pass packs
            every movable top-down claim onto the ceiling, so whatever is free
            above the run ends up joined to it whatever the claims between
            weigh. Counting the CLAIMS instead - the obvious reading of "what
            could move" - is right here by luck and over-reports the moment
            the movers weigh more than the hole they have to move into."""
            base, top = (u16(m.read(S("mem_base"), 2)),
                         u16(m.read(S("mem_top"), 2)))
            print("  -- %s --" % tag)
            fill, big, bigend = base, 0, base
            free_at = {}
            for bs, pa, ow, dma, rl in claims(m, S):
                if MEM_PG_MIN <= (ow >> 8) <= MEM_PG_MAX:
                    print("    %04x %5d KB cache %02x (counts as free)"
                          % (bs, pa // 64, ow >> 8))
                    continue
                if rl and not (dma & MC_DMA_HI):
                    # A MOVABLE FLOOR CLAIM PACKS, and modelling it as a
                    # barrier is the error tests/trkcompact.py's second shape
                    # made: plain OSAPI_MEM_AVAIL plans the ascending pass
                    # (SPEC.md 66.10.3), so the two 3 KB claims above the
                    # floor caches slide down over them and the run below
                    # the regions is 132 KB where a barrier model reads 95
                    print("    %04x %5d KB owner %04x     MOVABLE  (packs to %04x)"
                          % (bs, pa // 64, ow, fill))
                    fill += pa
                    continue
                if bs > fill:
                    free_at[fill] = bs - fill
                    if bs - fill > big:
                        big, bigend = bs - fill, fill
                    print("         %5d KB FREE" % ((bs - fill) // 64))
                print("    %04x %5d KB owner %04x%s%s"
                      % (bs, pa // 64, ow, " HI" if dma & MC_DMA_HI else "   ",
                         "  MOVABLE" if rl else ""))
                fill = max(fill, bs + pa)
            if top > fill:
                free_at[fill] = top - fill
                if top - fill > big:
                    big, bigend = top - fill, fill
                print("         %5d KB FREE (to the top)" % ((top - fill) // 64))
            above = sum(n for at, n in free_at.items() if at > bigend)
            print("     arena %d KB, largest run %d KB, %d KB free above it,"
                  " module wants %d KB"
                  % ((top - base) // 64, big // 64, above // 64, needk))
            return big // 64, above // 64

        def snd_seg():
            return u16(m.read(S("drv_tab") + SND_ROW * DRVR_SZ + DRVR_SEG, 2))

        def wins(title):
            return [w for w in os88geom.windows(m, S)
                    if w.visible and w.title.startswith(title)]

        def check(n, what, ok, note=""):
            nonlocal bad
            print("  %d %-28s %s%s" % (n, what, "OK" if ok else "FAILED",
                                       ("  " + note) if note else ""))
            bad += not ok

        # --- 1. it boots with the sound driver DOWN -------------------------
        check(1, "SOUND.DRV down at boot", snd_seg() == 0,
              "(drv_tab row 0 seg %04x)" % snd_seg())
        if snd_seg():
            print("     this machine has an OPL2 at 388h, so drvp_sniff wanted"
                  " the row before the first paint - see the machine's own"
                  " note in os8088_machines.toml")
            return 1
        layout("bare desktop")

        # --- 2/3/4. the three programs that take the ceiling ----------------
        ui.open_drive("B")
        for name in APPS:
            ui.open(name)
            layout("...%s open" % name)

        # --- 5. mount SOUND.DRV, MID-SESSION --------------------------------
        mo.menu(8, 8, 8, 40)                    # the apple menu's first item
        M.settle(m, limit=60)
        want = m.sym("cp_ttl") - (M.KERNEL_SEG << 4)
        pan = next((w for w in os88geom.windows(m, S) if w.visible
                    and u16(m.read(os88geom.winptr(m, w.i, S) + 10, 2)) == want),
                   None)
        if pan is None:
            print("FAIL: the Control Panel did not open")
            return 1
        cx, cy = pan.x + 1, pan.y + TITLE_H
        mo.click(cx + 8, cy + CP_I0Y + CP_IDRV * CP_IROWH + 6)      # Drivers
        M.settle(m, limit=60)
        mo.click(cx + CP_RX + 8, cy + CP_DBY1 + CP_DROWH // 2)      # row 0
        M.settle(m, limit=90)
        check(2, "...and mounts mid-session", snd_seg() != 0,
              "(image at %04x)" % snd_seg())
        if not snd_seg():
            print("     ATTACH refuses when neither an OPL nor a DSP answers")
            return 1
        ui.close(pan)
        layout("SOUND.DRV mounted mid-session")

        # --- 6. Tracker, UNDER the driver -----------------------------------
        ui.raise_window(ui.disk_window())
        ui.open("TRACKER.O88")
        tw = wins("Tracker")[0]
        layout("Tracker open")

        # --- 7. close the three ---------------------------------------------
        for t in ("Sheet", "Paint", "Clear Skies"):
            for w in wins(t):
                ui.close(w)
        M.settle(m, limit=90)
        run, above = layout("Sheet / Paint / Clear Skies closed")
        check(3, "the heap is the shape", run < needk and run + above >= needk,
              "(%d KB in one run, %d more above it, wants %d)"
              % (run, above, needk))
        if bad:
            print("     a run that simply fitted would reach the same green"
                  " having tested nothing (docs/WRITING-TESTS.md 1)")
            return 1

        def wseg():
            """Tracker's segment, RE-READ: its region is what the posted
            compaction moves, and a byte polled at the base it had before the
            post is read out of whatever landed there."""
            return u16(m.read(os88geom.winptr(m, tw.i, S)
                              + os88geom.W_SEG, 2))

        seg0, snd0 = wseg(), snd_seg()

        # --- 8. open the 397KB module ---------------------------------------
        ui.raise_window(tw)
        m.key("KeyL")                           # Tracker's own Load... key
        M.settle(m, limit=120)
        if not [w for w in os88geom.windows(m, S)
                if w.visible and w.title in ("Open", "Save As")]:
            print("FAIL: 'L' put no file dialog up")
            return 1
        rows = [r[0] for r in dispcp.snapshot(m, S)]
        if MOD not in rows:
            print("FAIL: %s is not listed - %r" % (MOD, rows))
            return 1
        for _ in range(rows.index(MOD) + 1):
            m.key("ArrowDown")
            M.pace(m, 0.2)
        got = u16(m.read(S("fdlg_sel"), 2))
        if got != rows.index(MOD):
            print("FAIL: dialog selected row %d, wanted %d"
                  % (got, rows.index(MOD)))
            return 1
        m.key("Enter")

        seen = {"posted": False, "said": False}

        def both(mm):                           # the flag is up for the whole
            at = wseg()                         # pass AND the 397KB read
            if mm.read(at * 16 + P["trk_cpq"], 1)[0]:
                seen["posted"] = True
            if u16(mm.read(at * 16 + P["tui_msgp"], 2)) == P["trk_s_cpq"]:
                seen["said"] = True
            return seen["posted"] and seen["said"]
        try:
            M.until(m, both, "Tracker to post and say so", poll=0.05,
                    limit=30.0)
        except M.MartyError:
            pass                                # ...judged by check 4
        posted, said = seen["posted"], seen["said"]
        # **THE BYTE AND NOT THE SCREEN** (docs/plans/SOAK-PARALLEL.md 11,
        # docs/WRITING-TESTS.md 11). This waited for the screen to stop
        # changing and then polled `mp_loaded` anyway - so the settle was
        # asked of a machine that is legitimately busy for the whole of a
        # compaction and a 397KB read, and it spent 723 GUEST seconds never
        # seeing stillness. The row already knows the exact byte that means
        # "loaded"; `until` waits on it, on the guest's clock, and says which
        # of the two ways a wait fails happened.
        M.until(m, lambda mm: mm.read(wseg() * 16 + P["mp_loaded"], 1)[0],
                "the %dKB module to load" % needk, limit=300.0)
        layout("after the load")

        seg2 = wseg()
        tw2 = lambda n: u16(m.read(seg2 * 16 + P[n], 2))          # noqa: E731
        tb2 = lambda n: m.read(seg2 * 16 + P[n], 1)[0]            # noqa: E731
        check(4, "Tracker posted, and said so", posted and said,
              "(post %s, 'Making room...' %s)" % (posted, said))
        loaded = tb2("mp_loaded") != 0 and tw2("trk_modseg") != 0
        msg = tw2("tui_msgp")
        named = next((k for k, v in P.items() if v == msg
                      and k.startswith(("trk_s_", "mp_title"))), "+%04x" % msg)
        check(5, "the module loaded", loaded,
              "(claim %04x, %d KB, status %s)"
              % (tw2("trk_modseg"), tw2("trk_capk"), named))
        print("     Tracker's region %04x -> %04x, SOUND.DRV %04x -> %04x"
              % (seg0, seg2, snd0, snd_seg()))
        print("VERDICT:", "OK" if not bad else "%d PROBLEM(S)" % bad)
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
