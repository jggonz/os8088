#!/usr/bin/env python3
"""SPEC.md 19.9.1: what reading and rewriting a file in SYSTEM\\APPDATA COSTS.

apps/os88data.inc is the visit five packages share - Cyclone, Tank Attack,
Dot Delirium, Clear Skies and PIXELSTEIN 3D - and its whole subject is a
cost, so this row asserts the cost rather than the behaviour (tests/pxsstate.py
already reads the files back across a restart). PIXELSTEIN is the instrument
because it touches TWO files at entry (PXSTEIN.CFG, PXSTEIN.HS) and rewrites
one from a menu pick, the same eight bytes long every time:

  (a) THE LOADS walk with OSAPI_FILE_GOTO_QM and never OSAPI_FILE_GOTO - the
      display remount, which is what made Tank's save about six seconds
      (docs/plans/NAV-COST-PLAN.md). Counted by kernel breakpoints armed
      before the launch: zero FILE_GOTO, and at most two floppy reads for
      both files together.
  (b) EVERY SAVE is free of FILE_GOTO too, and the ones AFTER THE FIRST are
      ONE SECTOR WRITTEN: OSAPI_FILE_WRITE_AT's INSIDE arm (SPEC.md 18.4.7),
      no FAT and no entry. The first is a full OSAPI_FILE_WRITE by design
      (os88data.inc's compression-hint rule) and is held to at most four.
  (c) ...and the bytes landed: PXSTEIN.CFG off the live floppy, by
      tools/os88flush.py's own FAT12 walker, carries the Sound byte the
      last pick left in the game.

Each save is bracketed by breakpoints at px_set_save and its .gone (both arms
end there), with the floppy controller's own counters reset at the entry and
read at the exit, after the motor has been left to stop - so every save starts
where a player's would, and pays the quiet mount's boot-sector read.

Broken on purpose: od_write with its in-place arm cut (`jne .full` made a
`jmp .full`) takes (b) red at write_sectors 6 on saves 2-4 (measured); the walker put
back on OSAPI_FILE_GOTO takes (a) and (b) red on the FILE_GOTO counts.
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88flush      # noqa: E402
import os88marty      # noqa: E402
import os88mouse      # noqa: E402
import os88ui         # noqa: E402
import pxslib         # noqa: E402

FAIL = []
GOTO, QM = "osapi_file_goto", "osapi_file_goto_qm"
HZ = 4772727.0


def check(ok, what):
    print("   %-72s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def cfg_bytes(m):
    try:
        return os88flush.Flush(marty=m).volume(1).read("SYSTEM/APPDATA/PXSTEIN.CFG")
    except os88flush.FlushError as e:
        print("   (%s)" % e)
        return b""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--saves", type=int, default=4)
    a = ap.parse_args()
    os.chdir(ROOT)
    with os88marty.launch("build/os8088-360.img", apps="build/games360.img",
                          machine=a.machine) as m:
        os88marty.settle(m)
        os88marty.no_saver(m)
        # --- (a) the two loads at entry -------------------------------------
        st = {}

        def hit_load(mm, rec):
            if "d0" not in st:
                mm.disk(reset=True)
                st["d0"] = True
            st["d"] = mm.disk()
        with os88marty.bp_trace(m, GOTO, QM, on_hit=hit_load) as tr:
            g = pxslib.open_game(m, play=False)
        hs = tr.hits
        ng, nq = tr.count(GOTO), tr.count(QM)
        d = st.get("d", {})
        span = 1000.0 * (hs[-1]["cycles"] - hs[0]["cycles"]) / HZ if hs else 0.0
        print("   loads: FILE_GOTO %d, GOTO_QM %d, %.1f ms, %d reads / %d sectors"
              % (ng, nq, span, d.get("reads", -1), d.get("read_sectors", -1)))
        check(ng == 0 and nq >= 4, "(a) the loads walk with GOTO_QM and never FILE_GOTO "
              "(%d FILE_GOTO, %d GOTO_QM)" % (ng, nq))
        check(0 <= d.get("reads", 99) <= 2, "(a) ...and read the floppy at most twice for "
              "both files (%d)" % d.get("reads", -1))
        # --- (b) the saves --------------------------------------------------
        mo = os88mouse.Mouse(marty=m)
        ui = os88ui.UI(m, mouse=mo, verbose=False)
        a0, a1 = g.addr("px_set_save"), g.addr("px_set_save.gone")
        for i in range(a.saves):
            os88marty.guest_sleep(m, 4.0)       # the motor stops (18.9.1.1)
            res = {}

            def hit(mm, rec):
                if rec["addr"] == a0:
                    mm.disk(reset=True)
                elif rec["addr"] == a1:
                    res["d"] = mm.disk()
            with os88marty.bp_trace(m, a0, a1, GOTO, QM, on_hit=hit) as tr:
                ui.menu_pick("Game", "Sound")
                tr.until(lambda: "d" in res, "save %d to finish" % (i + 1), limit=120.0)
            h0 = next(h for h in tr.hits if h["addr"] == a0)
            h1 = next(h for h in tr.hits if h["addr"] == a1)
            d = res["d"]
            ng = tr.count(GOTO)
            print("   save %d: %7.1f ms  FILE_GOTO %d  GOTO_QM %d  reads %d  writes %d "
                  "(%d sectors)" % (i + 1, 1000.0 * (h1["cycles"] - h0["cycles"]) / HZ,
                                    ng, tr.count(QM), d["reads"], d["writes"],
                                    d["write_sectors"]))
            check(ng == 0, "(b) save %d: no FILE_GOTO" % (i + 1))
            if i == 0:
                check(1 <= d["write_sectors"] <= 4,
                      "(b) save 1 is a full write, at most four sectors (%d)"
                      % d["write_sectors"])
            else:
                check(d["write_sectors"] == 1,
                      "(b) save %d is ONE SECTOR, in place (%d written)"
                      % (i + 1, d["write_sectors"]))
        # --- (c) the bytes landed -------------------------------------------
        snd = g.byte("px_sound")
        cfg = cfg_bytes(m)
        check(len(cfg) == 12 and cfg[:4] == b"PXC\x02" and cfg[9] == snd,
              "(c) PXSTEIN.CFG on the floppy carries Sound %d (%r)" % (snd, cfg))
    print("appdata: %s" % ("ok" if not FAIL else "FAIL (%d)" % len(FAIL)))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
