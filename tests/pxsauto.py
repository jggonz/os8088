#!/usr/bin/env python3
"""PIXELSTEIN 3D's detail selector, every movement (SPEC.md 97.8, 97.10;
docs/plans/PIXELSTEIN-PLAN.md 14's row).

    python3 tests/pxsauto.py [--machine os8088_5150_cga_gla] [--no-slow]

MartyPC's 5150 is cycle-exact, so its frame is what it is: the only way to
make the selector see a slow frame or a fast one is to POKE THE CLOCK - a
breakpoint on px_auto_frame, and px_ftime (the frame the selector is about
to judge, 838 ns units) written at every stop before the compare reads it.
Windowed, with the turn key held so a frame is drawn every tick - HELD
BEFORE THE FIRST WAIT, because a still window composes nothing by
construction (97.8's idle predicate) and the first cut waited for a frame
that could never come. The legs, in the order that proves each on its own:

  (l) THE LINES, read out of part 0: an 8086 steps UP only under the rung
      above's own line (px_auplo/px_auphi, by the landing position) -
      111.1 ms onto Textured Low res (PX_BUDGET / 1.125, the largest
      Textured Low res / Flat Low res ratio measured) and 77.4 ms onto a
      Full res position (PX_BUDGET / 1.614) - where half the budget, a
      286's line, was under every frame an 8086 draws and made a step
      down permanent (wave 6's close; SPEC.md 97.8);
  (a) Auto on the 8086 STARTS a WINDOW at position 3 - Flat Low res, one
      rung under the bracket's Textured Low res, Flat Full between them
      being the DEARER picture on an 8086 (PLAN 15; the ladder since wave
      2 is Textured Full, Textured Low res, Flat Full, Flat Low res) - and
      the line says "Detail: Flat  Size 64  Low res";
  (c) seventy frames poked FAST (1,000 units, far under any line) do not
      climb: px_apos stays at the tier's start, which is its ceiling. The
      first cut climbed to Flat Full on the 64th, a rung whose full repaint
      is 6.86 fps (the review's blocker);
  (r) Detail > Full res under Auto RE-SEATS the ladder within its rung -
      position (apos & ~1) | res, so 2 here - and banks it as the ceiling
      (px_oncmd's arm, 97.8); seventy fast frames at 2 stay there;
  (b0) THE NEGATIVE CONTROL: the turn key released and px_force poked at
      every stop instead - the machine's OWN frame on a still eye, the cast
      and a compose that writes nothing, ~65 ms at Flat Full - nine of them
      with NOTHING poked into px_ftime do not step down, and every one
      reads under the budget (printed against it). It is a still eye and
      not the turning frame because the 5150's own windowed Flat Full turn
      at Size 64 reads 120-137 ms across the 125.0 ms line (116.4 / 112.9
      at the default of 48 since wave 3's review, 97.8) - a control that
      the machine can fail on its own is no control. Without this leg the step
      down below could be passing on the 5150's own frame rather than on
      the poke. The frames from here on are these forced still-eye frames;
      px_ftime is what is judged, so what the frame drew no longer matters;
  (b) eight frames poked SLOW (200,000 units, over the 149,165 budget) step
      DOWN exactly once, to position 3 - Flat Low res, the floor; the step
      banked a hold-down of PX_AHOLD ticks (read back against the tick);
      the line reads "Detail: Flat  Low res" on the next drawn frame and is
      not redrawn after (announced ONCE); twelve more slow frames leave the
      ladder on its floor - there is no Wire under it (97.8);
  (d) the hold-down, both edges, with px_ahold poked so that neither what
      a frame costs the guest clock nor what a stop does decides it:
      STRETCHED to 600 ticks ahead, sixty-four fast frames and a
      sixty-fifth - the one whose compare rolls px_ahit over at 64 with the
      hold live - do NOT step up; then COLLAPSED to a tick ago, THE LINE'S
      EDGE: 65 frames one unit OVER the Full res line (77.4 ms) do not
      step up, and 65 frames ON it do - the step up comes at the rollover,
      lands at position 2 (the ceiling the re-seat banked) and nowhere
      else; and seventy more fast frames stay there.
  (f) THE BRACKET, NOTHING POKED INTO THE CLOCK: two guards at the elbow
      and the whole view repainted every frame (the machine's own ~150 ms,
      over the budget) step Auto DOWN from Textured Low res STRAIGHT to Flat
      Low res - never Flat Full; then the guards gone and the same forced
      repaint in the empty hall (~105 ms: under the 111.1 line, over a
      286's 62.5 - so the old line never climbed) step it back UP to
      Textured Low res once the 10 s hold-down has passed and 64 frames in a
      row have read under the line; seventy more such frames there stay.
      The clock is the KERNEL's tick word (pxslib.kticks: what
      OSAPI_GET_TICKS answers and px_ahold is written in), ~200 ticks
      behind the BIOS count; a first cut read the BIOS one and saw a hold
      that had "already passed" while the package was honouring it.

--no-slow is the by-hand proof that leg (b) bites: the SLOW pokes are
skipped (nothing written, the still-eye frames judged as they are), and
the row must then FAIL at (b).

Every leg reads the package's bss through pxslib and never the glass.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))     # LAST, so it wins (pxslib)
import os88marty                                                # noqa: E402
import pxslib                                                    # noqa: E402

STAND, CHASE, PXAF_ATTACK = 1, 4, 2     # (tests/pxsact.py's)
FAST = 1000                     # 0.8 ms: under half the budget by any margin
SLOW = 200000                   # 167.6 ms: over the 125.0 ms budget
BUDGET = 149165                 # PX_BUDGET (pxgame.asm), 838 ns units
AHOLD = 182                     # PX_AHOLD: ticks of hold-down after a step down
# the ladder since wave 2 (SPEC.md 97.8): 0 Textured Full, 1 Textured Low res,
# 2 Flat Full, 3 Flat Low res. An 8086 WINDOW starts at 3 since wave 6's close
# (Flat Full, the position between, is dearer than Textured Low res on an
# 8086), and Detail > Full res re-seats it at 2 - the ceiling legs (r)..(d)
# step down from and back up to
POS_TOP, POS_FLOOR = 2, 3
POS_START = 3                   # an 8086's WINDOW (wave 6's close)
POS_BRACKET = 1                 # ...and its bracket: Textured Low res
AUP1 = 132591                   # PX_AUP1: 111.1 ms, BUDGET / 1.125
AUPF = 92419                    # PX_AUPF: 77.4 ms, BUDGET / 1.614
HALF = 74582                    # PX_BUDGET50: a 286's step-up line
FAIL = []


def check(ok, what):
    print("   %-66s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def us(ftime):
    return ftime * 0.838 / 1000.0


def line(g):
    b = g.bytes_("px_lbuf", 96)
    return b.split(b"\0", 1)[0].decode("ascii", "replace").rstrip()


def frames(g, n, ftime, watch=None, real=None, force=False):
    """n stops at px_auto_frame, px_ftime poked to `ftime` at each (None:
    nothing poked - the machine's own frame is judged); the real px_ftime
    is appended to `real` before the poke; `force` pokes px_force at every
    stop so the next frame draws on a still eye (the turn key released) -
    or, "all", owes the WHOLE view (pxslib.force_all_poke: a full repaint);
    watch(g) is called at every stop and its False ends the run early.
    Returns the number of stops taken."""
    m = g.m
    for i in range(n):
        m.run()
        if m.wait_stop(30) is None:
            sys.exit("pxsauto: no frame in 30 s - is the turn key held / px_force poked?")
        if real is not None:
            real.append(g.dword("px_ftime"))
        if ftime is not None:
            g.poke("px_ftime", [ftime & 255, (ftime >> 8) & 255,
                                (ftime >> 16) & 255, (ftime >> 24) & 255])
        if force == "all":
            g.force_all_poke()          # the WHOLE view owed: a full repaint
        elif force:
            g.poke_byte("px_force", 1)
        if watch is not None and not watch(g):
            return i + 1
    return n


def reseat_full(g):
    """Detail > Full res under Auto: px_oncmd's arm, poked the way
    pxslib.pin pokes a Detail pick (97.8: applied between frames through
    px_pend) - the row's byte, the position, the ceiling, the counters."""
    g.m.pause()
    g.poke_byte("px_res", 0)
    g.poke_byte("px_apos", POS_TOP)
    g.poke_byte("px_astart", POS_TOP)
    g.poke_byte("px_amiss", 0)
    g.poke_byte("px_ahit", 0)
    g.poke_byte("px_pend", 1)
    g.m.run()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/games360.img")
    ap.add_argument("--no-slow", action="store_true",
                    help="skip the SLOW pokes: leg (b) must then FAIL (the proof it bites)")
    a = ap.parse_args()
    os.chdir(ROOT)
    slow = None if a.no_slow else SLOW
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        g = pxslib.open_game(m)
        g.sim(False)                    # the world frozen and the player safe
        g.god(True)                     # (wave 3): the selector is what is
                                        # under test, not a guard's tick
        st = g.state()
        print("   PXSTEIN.O88: window %d, part 0 at %04x, tier %d" % (g.win, g.seg, st["tier"]))
        check(st["tier"] == 0, "the guest is an 8086 (tier %d)" % st["tier"])

        # --- (a) the start: read BEFORE any frame is driven ------------------
        # the start is a launch-time fact (px_astart, the tier); the turn
        # frames the legs below drive read 120-140 ms in this window on the
        # 5150 (97.1's reported number), across the 125 ms line, and wave 3's
        # weapon put them over it often enough for Auto to have stepped once
        # before the first cut of this leg looked
        check(st["detail"] == pxslib.PXD["auto"], "Detail is Auto (%d)" % st["detail"])
        # --- (l) the lines, out of part 0 ----------------------------------
        lo = [g.m.read(g.base + g.s["px_auplo"] + 2 * i, 2) for i in range(3)]
        hi = [g.m.read(g.base + g.s["px_auphi"] + 2 * i, 2) for i in range(3)]
        lines = [int.from_bytes(l, "little") | int.from_bytes(h, "little") << 16
                 for l, h in zip(lo, hi)]
        print("   the 8086's step-up lines by landing position: %s = %s ms (a 286's: %.1f)"
              % (lines, ["%.1f" % us(v) for v in lines], us(HALF)))
        check(lines == [AUPF, AUP1, AUPF],
              "onto Textured Full / Textured Low res / Flat Full: 77.4 / 111.1 / 77.4 ms")
        check(round(BUDGET / 1.125) == AUP1 and round(BUDGET / 1.614) == AUPF,
              "...each the budget over the largest measured ratio (1.125, 1.614)")
        check(st["apos"] == POS_START and st["rung"] == pxslib.PXR["flat"] and st["lowres"] == 1,
              "Auto starts the 8086's window at position 3, Flat Low res (pos %d, rung %d, lowres %d)"
              % (st["apos"], st["rung"], st["lowres"]))
        check(g.byte("px_astart") == POS_START, "px_astart banks the start (%d)" % g.byte("px_astart"))
        m.pause()
        t = line(g)
        m.run()
        print("   the line: %r" % t)
        check(t.startswith("Detail: Flat  Size 64  Low res"), "the text line says Flat Low res")

        # a frame every tick from here: the breakpoint ARMED, then the turn
        # key held, so every frame from the first is judged on a POKED clock
        # - the machine's own windowed Flat Full turn frame on the 5150 at
        # Size 64 is 120-140 ms (97.1), across the 125 ms line, and wave 3's
        # weapon and sprite pass put every one of them over it: eight
        # unpoked frames here stepped Auto down before leg (c) looked (the
        # first cut of this wave; wave 2's frames straddled the line and
        # never made eight). 97.8 records the fact, and the ordering here
        # stays a convenience
        m.bp_exec(g.addr("px_auto_frame"))
        m.key("ArrowRight", down=True, up=False)
        frames(g, 3, FAST)

        # --- (c) the ceiling: fast frames do not climb past the start -----------
        moved = []
        frames(g, 70, FAST, watch=lambda gg: moved.append(gg.byte("px_apos")) or True)
        check(all(p == POS_START for p in moved),
              "70 fast frames never left position 3, the start and ceiling (saw %s)"
              % sorted(set(moved)))
        check(g.byte("px_ahit") < 64, "px_ahit counts nothing at the ceiling (%d)" % g.byte("px_ahit"))

        # --- (r) Detail > Full res under Auto re-seats the ladder at the top ------
        reseat_full(g)
        for _ in range(3):                  # applied at the next px_frame_begin
            frames(g, 1, FAST)
            if g.byte("px_cols") == 64:
                break
        st = g.state()
        check(st["apos"] == POS_TOP and st["rung"] == pxslib.PXR["flat"] and st["lowres"] == 0
              and st["cols"] == 64 and st["detail"] == pxslib.PXD["auto"],
              "Detail > Full res under Auto re-seated position 2, Flat Full, 64 cols "
              "(pos %d, rung %d, lowres %d, cols %d)" % (st["apos"], st["rung"], st["lowres"], st["cols"]))
        check(g.byte("px_astart") == POS_TOP, "...and banked it as the ceiling (%d)" % g.byte("px_astart"))
        t = line(g)
        print("   the line: %r" % t)
        check(t.startswith("Detail: Flat  Size 64  Full res"), "the text line says Flat Full res")
        top0 = []
        frames(g, 70, FAST, watch=lambda gg: top0.append(gg.byte("px_apos")) or True)
        check(all(p == POS_TOP for p in top0),
              "70 fast frames at the top stay at position 2 (saw %s)" % sorted(set(top0)))

        # --- (b0) the negative control: a still eye, nothing poked, nothing steps
        # the key up, the eye still: from here every frame is owed by a poked
        # px_force at the stop before it. The first three stops absorb the
        # turn the released key's last scancode (px_tap) still spends
        m.key("ArrowRight", down=False, up=True)
        g.poke_byte("px_force", 1)
        frames(g, 3, None, force=True)
        real, ctl = [], []
        frames(g, 9, None, watch=lambda gg: ctl.append(gg.byte("px_apos")) or True,
               real=real, force=True)
        print("   the machine's own Flat Full frame on a still eye (windowed): %.1f..%.1f ms "
              "against the %.1f ms budget" % (us(min(real)), us(max(real)), us(BUDGET)))
        check(all(p == POS_TOP for p in ctl),
              "nine unpoked frames did not step down (saw %s)" % sorted(set(ctl)))
        check(max(real) < BUDGET, "...and every one of them read under the budget")
        check(g.byte("px_amiss") == 0, "px_amiss is 0 after them (%d)" % g.byte("px_amiss"))

        # --- (b) the step down: eight slow frames, once, announced once ---------
        seen = []
        frames(g, 8, slow, watch=lambda gg: seen.append(gg.byte("px_apos")) or True, force=True)
        # the eighth stop is BEFORE its compare runs: one more frame lands it
        frames(g, 1, slow, force=True)
        st = g.state()
        check(st["apos"] == POS_FLOOR and st["rung"] == pxslib.PXR["flat"] and st["lowres"] == 1,
              "eight slow frames stepped down once, to Flat Low res (pos %d, rung %d, lowres %d)"
              % (st["apos"], st["rung"], st["lowres"]))
        check(g.byte("px_amiss") == 0, "px_amiss cleared by the step (%d)" % g.byte("px_amiss"))
        check(seen[:7] == [POS_TOP] * 7, "the first seven misses moved nothing (%s)" % seen)
        hold = (g.word("px_ahold") - g.kticks()) & 0xFFFF
        print("   the step banked a hold-down: px_ahold is %d ticks ahead of the clock" % hold)
        check(AHOLD - 8 <= hold <= AHOLD, "...of PX_AHOLD = %d ticks, less what the stop spent" % AHOLD)
        # announced once: the NEXT drawn frame carries the line and clears
        # px_lined; the frames after it do not set it again
        frames(g, 1, slow, force=True)
        t = line(g)
        print("   the line: %r" % t)
        check(t.startswith("Detail: Flat  Size 64  Low res"), "the line announced Flat Low res")
        relit = []
        frames(g, 12, slow, watch=lambda gg: relit.append(gg.byte("px_lined")) or True, force=True)
        check(not any(relit), "the announcement was made once (px_lined %s)" % relit)
        check(g.byte("px_apos") == POS_FLOOR, "twelve more slow frames stay on the floor (%d) "
              "- the floor, no Wire under it" % g.byte("px_apos"))

        # --- (d) the hold-down, both edges; the step back; the ceiling again ------
        # a stop costs the guest clock (78 stops spent 321 ticks in the first
        # cut, and the 10 s hold had passed by the 64th "inside" frame), so
        # the hold is POKED far and then near and each edge is proved alone
        g.poke_word("px_ahold", (g.kticks() + 600) & 0xFFFF)
        held = []
        frames(g, 65, FAST, watch=lambda gg: held.append(gg.byte("px_apos")) or True, force=True)
        left = (g.word("px_ahold") - g.kticks()) & 0xFFFF
        print("   at the 65th fast frame the hold-down has %d ticks left, px_ahit %d"
              % (left, g.byte("px_ahit")))
        check(all(p == POS_FLOOR for p in held) and left < 600,
              "65 fast frames inside the hold-down - px_ahit rolled over at 64 with the hold "
              "live - did not step up (saw %s)" % sorted(set(held)))
        g.poke_word("px_ahold", (g.kticks() - 1) & 0xFFFF)
        over = []
        frames(g, 65, AUPF + 1, force=True, watch=lambda gg: over.append(gg.byte("px_apos")) or True)
        check(all(p == POS_FLOOR for p in over) and g.byte("px_ahit") == 0,
              "the hold collapsed: 65 frames ONE UNIT OVER the Full res line (%.2f ms) do not "
              "step up (saw %s, px_ahit %d)" % (us(AUPF + 1), sorted(set(over)), g.byte("px_ahit")))
        path = []
        n = frames(g, 130, AUPF, force=True,
                   watch=lambda gg: path.append(gg.byte("px_apos")) or path[-1] != POS_TOP)
        check(n <= 65 and path[-1] == POS_TOP,
              "...and frames ON it do: the step up came at the rollover (after %d)" % n)
        check(set(path) <= {POS_TOP, POS_FLOOR},
              "...and touched no position outside the ladder (%s)" % sorted(set(path)))
        st = g.state()
        check(st["rung"] == pxslib.PXR["flat"] and st["lowres"] == 0,
              "the rung in force is Flat Full again (rung %d, lowres %d)"
              % (st["rung"], st["lowres"]))
        top = []
        frames(g, 70, FAST, watch=lambda gg: top.append(gg.byte("px_apos")) or True, force=True)
        check(all(p == POS_TOP for p in top),
              "70 more fast frames stay at position 2 (saw %s)" % sorted(set(top)))
        m.bp_exec()
        m.run()
        m.key("ArrowRight", down=False, up=True)
        pxslib.release_held(m, ("ArrowLeft", "ArrowRight"), "leaving the window")

        # --- (f) the bracket, the machine's own clock: a fight, then the hall -----
        g.enter_fsx()
        st = g.state()
        check(st["apos"] == POS_BRACKET and g.byte("px_astart") == POS_BRACKET
              and st["rung"] == pxslib.PXR["tex"] and st["lowres"] == 1,
              "the bracket re-seats Auto at position 1, Textured Low res, its ceiling "
              "(pos %d, start %d, rung %d, lowres %d)"
              % (st["apos"], g.byte("px_astart"), st["rung"], st["lowres"]))
        px, py, head = g.scene("a")
        g.wait_frames(1)
        m.pause()
        g.actor_poke(0, x=px + 256, y=py, state=CHASE, hp=25, ang=2048, dir=2, flags=PXAF_ATTACK)
        g.actor_poke(1, x=px + 512, y=py + 64, state=CHASE, hp=25, ang=2048, dir=2,
                     flags=PXAF_ATTACK)
        g.poke_byte("px_amiss", 0)
        g.poke_byte("px_ahit", 0)
        g.force_all_poke()
        m.run()
        m.bp_exec(g.addr("px_auto_frame"))
        fight, fpath = [], [POS_BRACKET]

        def down(gg):
            p = gg.byte("px_apos")
            if p != fpath[-1]:
                fpath.append(p)
            return p == POS_BRACKET
        n = frames(g, 40, None, real=fight, force="all", watch=down)
        print("   TWO GUARDS AT THE ELBOW, the whole view every frame: %s ms; Auto %s after %d"
              % (["%.1f" % us(v) for v in fight], fpath, n))
        check(fpath == [POS_BRACKET, POS_FLOOR],
              "the fight stepped Auto DOWN from Textured Low res STRAIGHT to Flat Low res, "
              "never Flat Full (path %s)" % fpath)
        # (a stop reads the frame ABOUT to be judged, so the eight that
        # stepped are the eight before the last read)
        check(len(fight) >= 9 and min(fight[-9:-1]) > BUDGET,
              "...on eight frames of its own over the budget")
        k_down = g.kticks()
        m.pause()
        for i in range(g.byte("px_nact")):
            g.actor_poke(i, state=0)            # the hall emptied: out of the world
        g.force_all_poke()
        m.run()
        hall, hpath = [], [POS_FLOOR]

        def up(gg):
            p = gg.byte("px_apos")
            if p != hpath[-1]:
                hpath.append(p)
            return p == POS_FLOOR
        n = frames(g, 260, None, real=hall, force="all", watch=up)
        waited = (g.kticks() - k_down) & 0xFFFF
        floor_ms = hall[:-1]
        print("   THE EMPTY HALL at Flat Low res, the whole view every frame: %.1f..%.1f ms, "
              "median %.1f, over %d frames (the first %s; line %.1f, a 286's %.1f); Auto %s "
              "after %d ticks" % (us(min(floor_ms)), us(max(floor_ms)),
                                  us(pxslib.median(floor_ms)), len(floor_ms),
                                  " ".join("%.1f" % us(v) for v in floor_ms[:4]), us(AUP1),
                                  us(HALF), hpath, waited))
        check(hpath == [POS_FLOOR, POS_BRACKET],
              "the empty hall stepped Auto back UP to Textured Low res, past Flat Full (path %s)"
              % hpath)
        check(waited >= AHOLD and n <= 3 * 64 + 8,
              "...once the %d-tick hold-down had passed (%d ticks) and a run of 64 under the "
              "line had followed it (%d frames in all)" % (AHOLD, waited, n))
        check(max(floor_ms[-64:]) <= AUP1 and min(floor_ms) > HALF,
              "...on frames under the 111.1 ms line and every one over a 286's 62.5 - the "
              "half-budget line never climbed from here")
        stay, top = [], []
        frames(g, 70, None, real=stay, force="all",
               watch=lambda gg: top.append(gg.byte("px_apos")) or True)
        print("   ...and Textured Low res in the same hall: %.1f..%.1f ms, median %.1f (%s)"
              % (us(min(stay)), us(max(stay)), us(pxslib.median(stay)),
                 " ".join("%.1f" % us(v) for v in stay[:12])))
        check(all(p == POS_BRACKET for p in top),
              "70 more such frames stay at Textured Low res (saw %s)" % sorted(set(top)))
        m.bp_exec()
        m.run()

    if FAIL:
        print("pxsauto: FAIL (%d)" % len(FAIL))
        for f in FAIL:
            print("  -", f)
        return 1
    print("pxsauto: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
