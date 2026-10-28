#!/usr/bin/env python3
"""What a compaction COSTS while a Sound Blaster plays (NO-PINNING G1).

    make && make build/trackmove360.img && python3 tests/cpcost.py

WHY. Every claim NO-PINNING makes movable is one more block a compaction may
COPY, at ~13.3 cycles a byte on a 4.77 MHz 8088 (PERFORMANCE.md Set 117.2),
with `[sch_lock]` raised across the walk - so no other task runs while it
copies, and a Sound Blaster stream's refill task is a task. The plan's C3
(64 KB emulator RAMs) and C4 (LEMMINGS' ~330 KB) make bigger blocks movable,
and the adversarial review asked what that does to a stream that is playing
(docs/plans/NO-PINNING-PLAN.md 5, finding 7). This row MEASURES it on the
scenario the tree already has - tests/trackmove.py's: HEAPFRAG owns the floor,
Tracker opens BEVERLY.MOD above it, HEAPFRAG closes and runs again, and its
big claim compacts Tracker's 116 KB module down - but on a machine WITH a card
and with the music PLAYING through the compaction, where trackmove stops it.

WHAT IT REPORTS, per `mem_compact` call: the guest cycles from entry to
return and what that is in milliseconds on the 5150, and across the whole
claim: the driver's underrun count (`sbl_under`) before and after, and
whether the replayer is still running.

TWO ASSERTIONS, and the numbers are the point:
  1. the module moved (else the row measured nothing);
  2. the music is still playing afterwards (an underrun is a PAUSE the refill
     task resumes - SPEC.md 34.5's contract - and must not be a stop).
The underrun count is REPORTED, not asserted: whether a compaction may
interrupt the music for its own length is the owner's call, and this is the
number it is made on.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import trackmove as T                                   # noqa: E402
import dispcp                                           # noqa: E402
import os88drvsym                                       # noqa: E402
import os88fixture                                      # noqa: E402
import os88geom                                         # noqa: E402
import os88marty                                        # noqa: E402
import os88mouse                                        # noqa: E402
import os88sym                                          # noqa: E402

HZ = 4772727


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_sb_gla")
    a = ap.parse_args()
    S = os88sym.linear
    P = T.pkg_syms("apps/tracker/tracker.asm")
    D = os88drvsym.symbols("sound.bin")
    os88fixture.need("build/trackmove360.img")
    claims, u16 = T.claims, T.u16

    with os88marty.launch("build/os8088-360.img",
                          apps="build/trackmove360.img",
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        os88marty.no_saver(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        dslot = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
        mo.drag(wx + 60, wy + 9, wx + 60 + 215, wy + 9)
        os88marty.settle(m)
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)

        def raise_disk():
            dw = [w for w in os88geom.windows(m, S) if w.i == dslot][0]
            mo.click(*T.uncovered(m, S, dw))
            os88marty.until(m, lambda _: os88geom.top(m, S)
                            == os88geom.winptr(m, dslot, S),
                            "the Disk window to come to the front", limit=20)

        def heap_quiet():
            os88marty.quiesce(m, lambda: (m.disk().get("reads"),
                                          sorted(claims(m, S))),
                              guest=2.0, budget=120.0, what="heap")

        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, T.PKG_HEAPFRAG)
        heap_quiet()
        os88marty.settle(m)
        _, hf_win = T.find_win(m, S, "Heap")

        raise_disk()
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, T.PKG_MOD)
        os88marty.until(m, lambda _: (T.find_win(m, S, "Tracker")[0] or 0)
                        and u16(m.read(T.find_win(m, S, "Tracker")[0] * 16
                                       + P["mp_loaded"], 2)),
                        "Tracker to load the module", poll=0.5, limit=60)
        tk_seg, _ = T.find_win(m, S, "Tracker")

        def tbyte(name, seg=None):
            return m.read((seg or tk_seg) * 16 + P[name], 1)[0]

        os88marty.until(m, lambda _: tbyte("mp_playing"),
                        "the card to start playing", limit=60)
        dseg = next(c[0] for c in claims(m, S) if c[2] == T.MEM_K_DRV)

        def dword(name):
            return u16(m.read(dseg * 16 + D[name], 2))
        base0 = u16(m.read(tk_seg * 16 + P["trk_modseg"], 2))
        print("Tracker at %04x, module %04x, playing; stream %s"
              % (tk_seg, base0, "DIRECT" if m.read(dseg * 16 +
                                                  D["sbl_direct"], 1)[0]
                 else "double-buffered"))

        # close heapfrag through its dock tile, then run it again
        mo.click(*os88geom.tile_xy(m, hf_win, S))
        os88marty.until(m, lambda _: os88geom.top(m, S)
                        == os88geom.winptr(m, hf_win.i, S),
                        "heapfrag fronted", limit=20)
        mo.click(hf_win.x + 8, hf_win.y + 9)
        os88marty.until(m, lambda _: not any(
            w.i == hf_win.i and w.visible for w in os88geom.windows(m, S)),
            "heapfrag closed", limit=20)

        under0 = dword("sbl_under")
        raise_disk()
        runs = []
        m.bp_exec("mem_compact")
        dispcp.open_named(m, mo, S, lambda *_: None, wx, wy, T.PKG_HEAPFRAG)
        for _ in range(20):
            if m.wait_stop(60) is None:
                break
            rg = m.regs()
            if rg["ip"] != os88sym.syms()["mem_compact"]:
                break
            c0 = int(m.status().get("cycles", 0))
            ret = u16(m.read((rg["ss"] << 4) + rg["sp"], 2))
            m.bp_exec(rg["cs"] * 16 + ret)
            m.go()
            m.wait_stop(60)
            runs.append(int(m.status().get("cycles", 0)) - c0)
            m.bp_exec("mem_compact")
            m.go()
        m.bp_exec()
        m.go()
        heap_quiet()
        os88marty.pace(m, 3.0) if hasattr(os88marty, "pace") else None

        tk_new, _ = T.find_win(m, S, "Tracker")
        base1 = u16(m.read(tk_new * 16 + P["trk_modseg"], 2))
        under1 = dword("sbl_under")
        playing = tbyte("mp_playing", tk_new)
        bad = 0
        for i, c in enumerate(runs):
            print("  mem_compact #%d: %9d cycles = %7.1f ms on a 4.77 MHz "
                  "8088" % (i + 1, c, c * 1000.0 / HZ))
        print("  underruns across the claim: %d (sbl_under %d -> %d)"
              % (under1 - under0, under0, under1))
        ok = base1 != base0
        print("  1 module moved        %s" % ("%04x -> %04x" % (base0, base1)
                                             if ok else "NO"))
        bad += not ok
        print("  2 still playing       %s" % ("yes" if playing else "NO"))
        bad += not playing
    print("cpcost: %s" % ("FAIL (%d)" % bad if bad else "2 checks passed"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
