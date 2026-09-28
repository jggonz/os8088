#!/usr/bin/env python3
"""PIXELSTEIN 3D's region MOVES, and the game is still playing afterwards
(SPEC.md 97.9, 66.6.1.2, 66.6.2).

    make pxsmove && python3 tests/pxsmove.py [--machine os8088_5150_cga_gla]

Part 0 is a RE-HOMED program (SPEC.md 20.12.10): its region is the parts
carve, and the carve holds two more parts - the scratch the compiled scalers are
generated into and the byte textures - that the loader's handoff named by
ABSOLUTE SEGMENT (the level stream, a lazy part since wave 4, is a claim of
its own and must NOT move with it). So `OS88_REGION_MOVABLE`'s bare
`ret` would be silent corruption here (66.6.1's own sentence), and the
package carries `px_reloc`, which moves every word that names its own region;
and it hires a worker, which `OS88_WORKER_RESTARTABLE px_worker_rs` hands back
(66.6.2), or task_spawn's frame would pin the region however it declared.

tests/rehomemove.py's shape, one package along: PXSTEIN opens first with its
worker running, Textured pinned so the frame goes through everything the
proc fixes - the far call into part 2's driver (px_drvp), the bodies'
segment (px_bseg), the wall pass's ES that part 2 carries a copy of
(PXG_QTEX) - and the level stream read through PXH_LEV; FILLER opens under
it, takes the arena down and forces the compaction. THE ASSERTIONS:

  1. the carve moved at all - without it the run proves nothing;
  2. px_reloc was CALLED ([px_moved] > 0) - 66.2's "declared and forgot";
  3. every handoff segment that names a part inside the carve moved BY THE
     DELTA and lies inside the carve's NEW extent (the address, not the
     bytes: a compaction does not scrub what it copied from, so a stale
     vector still reads the right bytes off the old copy - rehomemove's
     finding); PXH_LEV, PXH_ART and PXH_SPR, claims of their own, did
     NOT move;
  4. the derived words followed: px_bseg, px_drvp's segment, and the copy of
     PXH_BT inside part 2 at PXG_QTEX;
  5. the level stream reads 'PXL',1 through the fixed PXH_LEV;
  6. THE GAME IS STILL DRAWING and the worker is alive: the same pose forced
     whole after the move composes THE SAME SHADOW, byte for byte, as before
     it - a stale px_drvp would far-call the old copy of the driver and a
     stale QTEX would texture from the old copy of part 3, both of which
     still read plausibly until the arena is reused;
  7. the window is findable by title (W_SEG followed).

Broken on purpose with px_reloc's table emptied it reports the handoff
outside the new extent and [px_moved] still counting - the proc ran and
fixed nothing.
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))     # LAST, so it wins (pxslib)
import os88marty                                                # noqa: E402
import pxslib                                                   # noqa: E402
import os88geom                                                 # noqa: E402
import os88ui                                                   # noqa: E402
sys.path.insert(0, os.path.join(ROOT, "tools"))     # tools/heapmap.py (the
import heapmap                                      # reader), not tests/'s

FAIL = []


def check(ok, what):
    print("   %-70s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def claims(m, S):
    return heapmap.Map(m, {n: S(n) for n in
                           ("mem_base", "mem_top", "spl_live", "mem_tab")})


def filler(m, S):
    """FILLER's (fl_done, fl_nask) - or None - read through its window, so a
    compaction that moves it is followed, and only while the gfx lock is FREE:
    its fill runs inside its first W_PAINT and an ask round inside W_ONKEY,
    both with the lock held. tests/rehomemove.py's reader, whose FILLER this
    is; the offsets are filler.asm's bss table."""
    if m.read(S("gfx_lock_flag"), 1)[0]:
        return None
    for w in os88geom.windows(m, S):
        if w.title.startswith("Filler"):
            seg = struct.unpack_from("<H", m.read(os88geom.winptr(m, w.i, S)
                                                  + os88geom.W_SEG, 2))[0]
            if not seg:
                return None
            img = struct.unpack_from("<H", m.read(seg << 4, 32), 8)[0]
            b = m.read((seg << 4) + img, 22)
            return b[20], struct.unpack_from("<H", b, 16)[0]  # done, nask
    return None


def carve_of(m, S, seg):
    """The claim the program runs in: the one whose extent holds `seg`."""
    for c in claims(m, S).claims:
        if c.seg <= seg < c.end:
            return c
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/pxsmove360.img")
    a = ap.parse_args()
    os.chdir(ROOT)
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        S = m.sym
        g = pxslib.open_game(m)
        ui = os88ui.UI(m, verbose=False, sym=S)
        seg0 = g.seg
        h0 = g.handoff()
        c0 = carve_of(m, S, seg0)
        print("   PXSTEIN.O88: part 0 at %04x, carve %04x..%04x rloc=%d; handoff %s"
              % (seg0, c0.seg, c0.end, c0.rloc,
                 dict((k, "%04x" % v) for k, v in h0.items() if k in
                      ("lev", "gen", "bt", "art", "spr"))))
        check(c0.rloc != 0, "the carve is declared movable (MC_RLOC %d)" % c0.rloc)
        check(g.byte("px_moved") == 0, "px_reloc has not run yet (%d)" % g.byte("px_moved"))
        check(g.byte("px_wrs") == 0, "the worker has not been restarted yet (%d)"
              % g.byte("px_wrs"))
        # --- the frame through everything the proc fixes: Textured, pose A ---
        g.pin(rung="tex", lowres=True, size=64)
        g.wait_frames(1, limit=180.0)
        g.sim(False)
        g.god(True)
        g.scene("a")
        g.wait_frames(1, limit=60.0)
        st = g.state()
        check(st["rung"] == 2, "the window draws Textured (rung %d, texok %d)"
              % (st["rung"], st["texok"]))
        shadow0 = g.shadow()
        L = pxslib.layout()
        qtex0 = pxslib.u16(m.read((h0["gen"] << 4) + L["QTEX"], 2)) if h0["gen"] else 0

        # --- FILLER, and the forcing asks (tests/rehomemove.py's idiom) ------
        # EVERY WAIT IS ON FILLER'S OWN COUNTERS (tests/rehomemove.py's
        # idiom): the fill done, then each round of asks answered. It was
        # `time.sleep(8)` and six-second sleeps around a settle, which give a
        # loaded box a third of the machine an idle one gets
        ui.open("FILLER.O88")               # the Disk window, raised first
        try:
            os88marty.until(m, lambda _: (filler(m, S) or (0,))[0],
                            "the filler's fill", poll=0.3, guest=180.0)
        except os88marty.MartyError as e:
            print("   (%s)" % str(e).split("\n")[0])
        moved = False
        for _ in range(8):
            was = (filler(m, S) or (0, None))[1]
            m.key("KeyA")
            try:                            # one round of asks, and the
                os88marty.until(            # compaction a grant made
                    m, lambda _: (filler(m, S) or (0, was))[1] != was,
                    "the filler's asks", poll=0.3, guest=90.0)
            except os88marty.MartyError as e:
                print("   (%s)" % str(e).split("\n")[0])
            got = pxslib.find(m, S)
            if got and got[1] != seg0:
                moved = True
                break
        got = pxslib.find(m, S, guest=30.0)
        check(got is not None, "the game's window is still findable by title (W_SEG)")
        if not got:
            return report()
        win, seg1 = got
        check(moved and seg1 != seg0, "1. the carve MOVED (%04x -> %04x)" % (seg0, seg1))
        if seg1 == seg0:
            return report()
        delta = (seg1 - seg0) & 0xFFFF
        g = pxslib.Game(m, win, seg1)
        c1 = carve_of(m, S, seg1)
        print("   the carve now %04x..%04x, delta %+d paragraphs"
              % (c1.seg, c1.end, seg1 - seg0))
        check(g.byte("px_moved") > 0, "2. px_reloc was CALLED ([px_moved] = %d)"
              % g.byte("px_moved"))
        # the worker's half of the declaration OBSERVED (review, wave 4): the
        # kernel moves a region with a restartable worker only while that
        # worker is parked, and re-enters it at px_worker_rs - which counts
        check(g.byte("px_wrs") > 0, "2b. the worker was RE-ENTERED at px_worker_rs "
              "([px_wrs] = %d)" % g.byte("px_wrs"))
        h1 = g.handoff()
        for k in ("gen", "bt"):
            if not h0[k]:
                continue
            check((h1[k] - h0[k]) & 0xFFFF == delta and c1.seg <= h1[k] < c1.end,
                  "3. PXH_%s followed: %04x -> %04x, inside the new extent"
                  % (k.upper(), h0[k], h1[k]))
        for k in ("lev", "art", "spr"):
            check(h1[k] == h0[k], "3. PXH_%s, a claim of its own, stayed at %04x (%04x)"
                  % (k.upper(), h0[k], h1[k]))
        # against the OLD words plus the delta, never against the new handoff:
        # a proc that fixed nothing leaves both stale and EQUAL (the break-it
        # run with the table cut to one row read these as ok)
        want = ((h0["gen"] or seg0) + delta) & 0xFFFF
        check(g.word("px_bseg") == want,
              "4. px_bseg names the moved bodies (%04x, want %04x)" % (g.word("px_bseg"), want))
        drvp = g.bytes_("px_drvp", 4)
        if h0["gen"]:
            check(pxslib.u16(drvp, 2) == want, "4. px_drvp's segment followed (%04x)"
                  % pxslib.u16(drvp, 2))
        if h1["gen"]:
            qtex1 = pxslib.u16(m.read((h1["gen"] << 4) + L["QTEX"], 2))
            check(qtex1 == (h0["bt"] + delta) & 0xFFFF and qtex0 == h0["bt"],
                  "4. part 1's copy of the byte textures' segment followed "
                  "(PXG_QTEX %04x -> %04x)" % (qtex0, qtex1))
        sig = m.read(h1["lev"] << 4, 4)
        check(sig == b"PXL\x01", "5. the level stream reads 'PXL',1 through the fixed "
              "PXH_LEV (%r)" % sig)
        # --- 6. still drawing: the same pose, forced whole, the same shadow ---
        # FILLER is on top and has the focus, so the game paused itself
        # (97.8's sticky pause) - a forced frame draws all the same
        g.scene("a")
        g.wait_frames(1, limit=60.0)
        shadow1 = g.shadow()
        diff = sum(1 for x, y in zip(shadow0, shadow1) if x != y)
        check(diff == 0, "6. the game still draws: pose A forced whole after the move "
              "composes the same shadow (%d bytes differ of %d)" % (diff, len(shadow0)))
        f0 = g.word("px_frames")
        g.force()
        g.wait_frames(1, limit=60.0)
        check(g.word("px_frames") != f0, "6. ...and the worker is alive (px_frames %d -> %d)"
              % (f0, g.word("px_frames")))
    return report()


def report():
    if FAIL:
        print("pxsmove: FAIL (%d)" % len(FAIL))
        for f in FAIL:
            print("  -", f)
        return 1
    print("pxsmove: PIXELSTEIN's carve packed under the compactor, its own "
          "segment words followed, and the game drew the same picture after. PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
