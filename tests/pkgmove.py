#!/usr/bin/env python3
"""A package's declared claims MOVE, every word follows, nothing changes (NO-PINNING).

    make && python3 tests/pkgmove.py --app texpad --open B:/MEDIA/PAPER.TEX \\
        --word tp_srcseg

ONE GATE FOR THE MANY SMALL ITEMS OF docs/plans/NO-PINNING-PLAN.md (A3 and
after): a package declares a data claim movable with a relocation proc that
updates the words naming it. This row opens a document by double-click (its
association launches the package), records the claim each named WORD holds
and that claim's bytes, then posts a compaction from OUTSIDE at the TRIVIAL
purge rank - `[mem_cpq]` + `[mem_cpq_lvl]`, the door OSAPI_MEM_COMPACT posts
through (SPEC.md 66.4.3) - which drops the raise caches and the icon store and
so opens holes the claims slide into.

FIVE ASSERTIONS:
  1. every named claim is declared movable (MC_RLOC read from OUTSIDE - a
     refused declaration looks identical from inside, SPEC.md 66.5.6.2);
  2. at least one MOVED (else the run proved nothing - say so, do not pass);
  3. every word names a live claim afterwards;
  4. every moved claim's bytes are the same, hashed at both addresses;
  5. the window REDRAWS the same picture: the guest repaints everything
     (`[cp_dirty]`, tests/dispcalc.py's poke) and the content band is compared
     with the one before the move.

Each registration names its own red run: the package's proc made a bare
`ret`, which leaves the word naming the old base and fails 3.

FOR A PACKAGE WHOSE CLAIMS ARE NOT ALL OF THE STORY (PiXEL, SPEC.md 106.5.1):
  --scratch DIR:FILE[=NAME]  the B: floppy built with build/<app>.o88 and these
  --idle WORD                a byte of the package's that reads 0 when nothing
                             runs - a quiet claim table is not a finished job
  --busy WORD                ...and one that must read non-zero at the plan
  --still WORD               a claim the package STANDS IN (a code part its
                             parked worker is inside): must read pinned at
                             every plan made with the worker PARKED and every
                             --busy byte non-zero, and unmoved - the half that
                             catches a CS on a stack
  --tuck                     minimize the --first window, when it covers the
                             drive's zone
A --word may be an expression over the map's labels (`op_table+32`: part 2's
row segment), and --word is optional when --still is given.
"""
import argparse
import hashlib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
import dispapps                                         # noqa: E402
import dispcp                                           # noqa: E402
import os88build                                        # noqa: E402
import os88geom                                         # noqa: E402
import os88marty                                        # noqa: E402
import os88mouse                                        # noqa: E402
import os88sym                                          # noqa: E402

S = os88sym.linear
MC_SIZE, MEM_MAX = os88geom.MC_SIZE, os88geom.MEM_MAX


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def claims(m):
    raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
    return {u16(raw, i * MC_SIZE): (u16(raw, i * MC_SIZE + 2),
                                    u16(raw, i * MC_SIZE + 8))
            for i in range(MEM_MAX) if u16(raw, i * MC_SIZE)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", required=True)
    ap.add_argument("--open", required=True,
                    help="D:/FOLDER/.../FILE to double-click")
    ap.add_argument("--word", action="append", default=[],
                    help="a word naming a declared claim, which must MOVE "
                    "(at least one of them) - optional with --still")
    ap.add_argument("--disk", default="build/apps360.img",
                    help="the B: floppy; a bare name (games360.img) is "
                    "build/'s, which is how a suite row names one without "
                    "a literal build/ path (tests/unit/t_artpath.py) - "
                    "launch() resolves it against the run's tree either way")
    ap.add_argument("--first", help="D:/.../FILE opened BEFORE the target and "
                    "closed after it, so its claims leave a hole UNDER the "
                    "target's (a claim already on the floor cannot move)")
    ap.add_argument("--scratch", action="append", default=[],
                    help="DIR:FILE[=NAME] onto a SCRATCH B: floppy built with the "
                    "package's own build/<app>.o88 at its root (PiXEL's "
                    "pictures ride no shipped disk) - the --disk is then "
                    "that image's name")
    ap.add_argument("--idle", action="append", default=[],
                    help="a byte of the package's that reads 0 when nothing "
                    "is running - PiXEL's px_busy: its claims are all made "
                    "before a decode starts, so a quiet claim table is not "
                    "a finished picture")
    ap.add_argument("--still", action="append", default=[],
                    help="a word naming a claim the package is STANDING IN "
                    "when the compaction runs - a code part its parked "
                    "worker is inside: it must read pinned at the compactor's "
                    "plan, and unmoved")
    ap.add_argument("--busy", action="append", default=[],
                    help="a byte of the package's that must read non-zero "
                    "at the compactor's plan, with the worker parked, for "
                    "the --still check to apply there - PiXEL's px_anjob "
                    "(a pass running) and px_pwm (the worker's part mask)")
    ap.add_argument("--tuck", action="store_true",
                    help="minimize the --first window to its dock tile "
                    "before the target is opened: one that covers the "
                    "drive's desktop zone leaves it nothing to click")
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--keys", help="MartyKey names, comma-separated, typed "
                    "at the target once it is in front - a game is started "
                    "and PAUSED with them, so its worker parks within "
                    "INST_PARKW and its window holds still for check 5")
    ap.add_argument("--hide", action="store_true",
                    help="minimize the target through its dock tile for the "
                    "compaction and raise it after: a game whose worker "
                    "draws flat out reaches OSAPI_TASK_ALIVE too rarely to "
                    "park inside INST_PARKW, and a hidden window draws "
                    "nothing (DotDel measured: its worker is sch_cur for "
                    "the whole park window and never parked)")
    ap.add_argument("--animated", action="store_true",
                    help="the window animates on its own (a game's worker), "
                    "so check 5 cannot compare two pictures: it requires the "
                    "window to KEEP CHANGING after the move instead, which a "
                    "worker that faulted on a stale word stops doing")
    ap.add_argument("--clock", type=int, default=0, metavar="SECS",
                    help="the window draws the guest's CLOCK, whose picture "
                    "steps every SECS guest seconds (the Desk Clock's dial: "
                    "30, the minute hand stepping at :00 and :30, SPEC.md "
                    "110.14.2): check 5's two pictures are taken inside ONE "
                    "such step - the clock is put at its start first - and "
                    "a pair that still straddles one is reported as that, "
                    "not as a redraw that differs")
    a = ap.parse_args()
    if "/" not in a.disk:
        a.disk = os.path.join("build", a.disk)
    if a.scratch:
        files = []
        for spec in a.scratch:          # DIR:path=NAME puts it on as NAME:
            if "=" in spec:             # a GIF's association is Paint's, so
                spec, name = spec.split("=", 1)     # PiXEL is handed one as a
                d, path = spec.split(":", 1)        # .PNG - it SNIFFS what it
                lit = os.path.join("build", "pkgmove-scratch", name)  # reads
                dst = os88build.at(lit)         # the RUN'S tree, which is
                os.makedirs(os.path.dirname(dst), exist_ok=True)  # where
                if not os.path.exists(dst) or open(dst, "rb").read() != \
                        open(path, "rb").read():    # scratch_disk will look
                    open(dst, "wb").write(open(path, "rb").read())
                spec = d + ":" + lit
            files.append(spec)
        a.disk = os88marty.scratch_disk(a.disk, "build/%s.o88" % a.app,
                                        *files)
    MAP = dispapps._map(a.app)

    def addr(w):
        # A WORD is a name in the package's map, or an EXPRESSION over its
        # labels - `op_table+32` is part 2's row's segment word once it is
        # fetched (OP_T_ROWS 10 + 2 * OP_ROW 8 + OP_R_ZKB 6, os88parts.inc),
        # which no label names. The map holds labels, not constants
        return MAP[w] if w in MAP else int(eval(w, {"__builtins__": {}},
                                                 dict(MAP)))
    fails = []
    done = lambda mm, **k: os88marty.ui_done(mm)

    def front(m):
        # THE FRONTMOST window, not the last slot: open_drive FRONTS a Disk
        # window that is already open (a second file at the same volume's
        # root - office360's CHART.O88 after ARTFUL.O88), and a launched
        # package's window may take a lower free slot than the last one
        t = os88geom.top(m, S)
        return [i for i in dispcp.win_list(m, S)
                if os88geom.winptr(m, i, S) == t][0]

    def walk(m, mo, spec):
        drive, path = spec.split(":/", 1)
        parts = path.split("/")
        dispcp.open_drive(m, mo, S, done, drive)
        for name in parts[:-1]:
            wx, wy, _, _ = dispcp.win_rect(m, S, front(m))
            dispcp.open_named(m, mo, S, done, wx, wy, name)
        wx, wy, _, _ = dispcp.win_rect(m, S, front(m))
        before = dispcp.win_list(m, S)
        dispcp.open_named(m, mo, S, done, wx, wy, parts[-1])
        os88marty.until(m, lambda _: len(dispcp.win_list(m, S)) > len(before),
                        "%s's window" % parts[-1], poll=0.3, limit=60)
        os88marty.quiesce(m, lambda: sorted(claims(m)), guest=2.0,
                          what="the claims")
        return [i for i in dispcp.win_list(m, S) if i not in before][0]

    with os88marty.launch("build/os8088-360.img", apps=a.disk,
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.kernel_up(m)
        os88marty.no_saver(m)
        mega = getattr(m, "bochs", False)

        def lin(sg, off=0):
            """SEG:OFF linear: an LDT selector's base on kern_mega."""
            return m.lin(sg, off) if mega else sg * 16 + off

        def key(c):
            """The claim a WORD names, as mem_tab keys it: the word itself on
            a real-mode kernel; on kern_mega (MC7.4) the word is a SELECTOR
            that a move rebases and never changes, and mem_tab keeps the
            PARAGRAPH under it - so the claim is the selector's base, and
            "it moved" is that paragraph changing"""
            return (m.lin(c) >> 4) if mega and c else c
        mo = os88mouse.Mouse(marty=m)
        first = walk(m, mo, a.first) if a.first else None
        if first is not None and a.tuck:   # a window as big as PiXEL's covers
            fw = [o for o in os88geom.windows(m, S) if o.i == first][0]
            mo.click(*os88geom.tile_xy(m, fw, S))   # the drive's zone: it is
            os88marty.until(m, lambda _: not any(    # minimized to its tile
                o.i == first and o.visible for o in os88geom.windows(m, S)),
                "the first window tucked away", limit=20)
        win = walk(m, mo, a.open)
        if first is not None:            # its claims under ours become a hole
            fw = [o for o in os88geom.windows(m, S) if o.i == first][0]
            mo.click(*os88geom.tile_xy(m, fw, S))
            os88marty.until(m, lambda _: os88geom.top(m, S)
                            == os88geom.winptr(m, first, S),
                            "the first window fronted", limit=20)
            mo.click(fw.x + 8, fw.y + 9)
            os88marty.until(m, lambda _: not any(
                o.i == first and o.visible for o in os88geom.windows(m, S)),
                "the first window closed", limit=20)

        for k in (a.keys.split(",") if a.keys else ()):
            m.key(k)
            os88marty.pace(m, 1.0)

        def pseg():
            rec = m.read(S("wm_wins") + win * dispcp.WIN_SIZE,
                         dispcp.WIN_SIZE)
            return u16(rec, 22)
        for z in a.idle:               # the package's own "nothing running"
            os88marty.until(m, lambda _: m.read(lin(pseg(), addr(z)), 1)[0]
                            == 0, "%s = 0" % z, poll=0.3, limit=120)
        os88marty.quiesce(m, lambda: sorted(claims(m).items()), guest=2.0,
                          what="the package's claims")
        os88marty.ui_done(m)

        def words(seg):
            return {w: key(u16(m.read(lin(seg, addr(w)), 2))) for w in a.word}

        def stills(seg):
            return {w: key(u16(m.read(lin(seg, addr(w)), 2))) for w in a.still}

        seg0 = pseg()
        w0 = words(seg0)
        s0 = stills(seg0)
        held = claims(m)
        print("%s at %04x: %s" % (a.app, seg0, "  ".join(
            "%s=%04x" % kv for kv in w0.items())))
        hashes = {}
        for w, c in w0.items():
            if c not in held:
                fails.append("%s = %04x names no claim before the move"
                             % (w, c))
                continue
            hashes[w] = hashlib.md5(m.read(c * 16, held[c][0] * 16)).digest()
        decl = [w for w, c in w0.items() if c in held and held[c][1]]
        print("  1 declared movable   %d of %d" % (len(decl), len(w0)))
        if len(decl) != len(w0):
            fails.append("not declared: %s" % sorted(set(w0) - set(decl)))
        x, y, w, h = dispcp.win_rect(m, S, win)

        # THE POINTER LEAVES THE WINDOW FIRST. It is a software cursor drawn
        # into the same framebuffer check 5 compares, and a gesture above can
        # leave it inside the target - closing --first's window puts it on
        # that window's close box, which is over the Desk Clock - where a
        # repaint may catch it lit in one picture and hidden in the other:
        # "redraws the same NO" on a window that redrew perfectly
        # (23 differing pixels, all of them the arrow).
        W, H = m.fbuf()[:2]
        for px, py in ((W - 2, H - 2), (1, H - 2), (W - 2, 12), (1, 12)):
            if not (x - 16 <= px < x + w and y - 16 <= py < y + h):
                mo.to(px, py)
                break

        def clock():
            c = m.read(S("clk_sec"), 3)
            return c[2], c[1], c[0] // a.clock
        if a.clock:
            # ...AND A CLOCK'S PICTURE STEPS ON ITS OWN. The two pictures
            # were 4 guest seconds apart (00:01:01 and 00:01:05 measured), so
            # wherever the session's own pace put them, one run in a few
            # straddled a half minute and the dial's minute hand moved
            # between them. The clock is put at the start of its step instead:
            # [clk_sec] alone, which only the UI task writes (kernel/clock.inc)
            # and the Desk Clock reads at its next beat and at the repaint.
            m.write(S("clk_sec"), bytes([m.read(S("clk_sec"), 1)[0]
                                         // a.clock * a.clock]))

        def repaint():
            m.write(S("cp_dirty"), b"\x01")
            os88marty.ui_done(m)
            if a.animated:              # a picture that never holds still
                os88marty.pace(m, 1.0)  # cannot settle: check 5 compares
            else:                       # two moments instead
                os88marty.settle(m)
            return m.fbuf()

        shot0 = repaint()
        tick0 = clock() if a.clock else None
        wptr = os88geom.winptr(m, win, S)

        def shown():
            return any(o.i == win and o.visible
                       for o in os88geom.windows(m, S))

        if a.hide:
            mo.click(*os88geom.tile_xy(m, win, S))
            os88marty.until(m, lambda _: not shown(), "the target hidden",
                            limit=20)
            os88marty.pace(m, 1.0)
        def post():
            m.write(S("mem_cpq_lvl"), bytes([0xFC]))
            m.write(S("mem_cpq"), bytes([wptr & 0xFF, wptr >> 8]))
            os88marty.until(m, lambda _: u16(m.read(S("mem_cpq"), 2)) == 0,
                            "the posted compaction", poll=0.2, limit=30)
            os88marty.ui_done(m)

        # --still IS JUDGED AT THE PLAN, NOT AFTER IT. What pins a code part
        # is a task STANDING in it (px_pin/px_unpin), and that changes while
        # the package runs: PiXEL's worker is inside the GIF part for a whole
        # pass and outside it between two, so a read of MC_RLOC after the
        # compaction sampled the animation's phase at a HOST-timed moment -
        # "op_table+24 stood still 2be0 -> 2be0 NOT PINNED" from a part that
        # never moved, read in the gap after a pass. So mem_compact's .plan,
        # where mem_cp_plan decides what may move with sch_lock held, is
        # broken on, and there the package's worker is asked whether it is
        # PARKED (sch_parked: it can only park at OSAPI_TASK_ALIVE, which a
        # GIF pass calls inside the part) with every --busy byte non-zero -
        # PiXEL's px_pwm being the part mask px_pin sets. At such a plan every
        # --still claim must be pinned, and across that compaction unmoved. A
        # compaction that never found the package standing in it proves
        # nothing either way, so it is posted again, a few times, and then
        # that is the failure.
        def at_plan(mm, rec):
            seg = pseg()
            inst = mm.read(S("wm_owner") + win, 1)[0]
            task = 0xFF if inst == 0xFF else mm.read(
                S("inst_tab") + inst * os88geom.I_RECSZ + os88geom.I_TASK,
                1)[0]
            parked = task != 0xFF and mm.read(S("sch_parked") + task, 1)[0]
            busy = {z: mm.read(lin(seg, addr(z)), 1)[0] for z in a.busy}
            hd = claims(mm)
            st = {}
            for k in a.still:
                c = key(u16(mm.read(lin(seg, addr(k)), 2)))
                st[k] = (c, c in hd and not hd[c][1])
            return {"task": task, "parked": bool(parked), "busy": busy,
                    "st": st}

        standing = []
        if not a.still:
            post()
        for attempt in range(1, 6 if a.still else 1):
            s0 = stills(pseg())
            for z in a.busy:
                print("  0 %s = %d as compaction %d is posted" % (
                    z, m.read(lin(pseg(), addr(z)), 1)[0], attempt))
            with os88marty.bp_trace(m, "mem_compact.plan",
                                    on_hit=at_plan) as tr:
                post()
            for p in [h["hit"] for h in tr.hits if "hit" in h]:
                stand = p["parked"] and all(p["busy"].values())
                print("    plan: worker task %s %s, %s; %s" % (
                    "%d" % p["task"] if p["task"] != 0xFF else "none",
                    "PARKED" if p["parked"] else "running",
                    " ".join("%s=%02x" % kv for kv in p["busy"].items()),
                    "  ".join("%s %04x %s" % (k, c, "pinned" if pin
                                              else "movable")
                              for k, (c, pin) in p["st"].items())))
                if stand:
                    standing.append(p)
            if standing:
                break
            print("  0 compaction %d never found the worker standing in "
                  "the package%s" % (attempt, ": posted again" if attempt < 5
                                     else ""))
        if a.still and not standing:
            fails.append("no compaction in 5 found the package standing in "
                         "%s (parked, %s non-zero): the --still check proves "
                         "nothing" % ("/".join(a.still), "/".join(a.busy)))
        for k in a.still:
            loose = [p["st"][k][0] for p in standing if not p["st"][k][1]]
            if loose:
                fails.append("%s = %04x was MOVABLE at %d of %d plans made "
                             "with the package standing in it"
                             % (k, loose[0], len(loose), len(standing)))
        if a.hide:
            mo.click(*os88geom.tile_xy(m, win, S))
            os88marty.until(m, lambda _: shown(), "the target raised",
                            limit=20)
            os88marty.ui_done(m)

        seg1 = pseg()
        w1 = words(seg1)
        held = claims(m)
        s1 = stills(seg1)               # ...and what must NOT have moved: a
        for k in a.still:               # claim the package stood in (pinned
            pins = [p["st"][k][1] for p in standing]    # at every such plan)
            ok = s1[k] == s0[k] and s1[k] in held and pins and all(pins)
            print("  0 %s stood still  %04x -> %04x %s (%s now)" % (
                k, s0[k], s1[k], "pinned at %d plan(s)" % len(pins) if ok
                else "NOT PINNED / MOVED", "movable" if s1[k] in held and
                held[s1[k]][1] else "pinned"))  # (the phase: not asserted)
            if not ok:
                fails.append("%s moved or was movable while the package "
                             "stood in it" % k)
        print("%s at %04x: %s" % (a.app, seg1, "  ".join(
            "%s=%04x" % kv for kv in w1.items())))
        moved = [k for k in w1 if w1[k] != w0[k]]
        print("  2 moved              %s" % (", ".join(moved) or "NOTHING"))
        if not moved and a.word:
            fails.append("nothing moved: the run proves nothing")
            raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)     # ...and WHY:
            for i in sorted(range(MEM_MAX),                   # the heap map,
                            key=lambda i: u16(raw, i * MC_SIZE)):  # base order
                b = u16(raw, i * MC_SIZE)
                if b:
                    print("     %04x %3dKB own %04x %s"
                          % (b, u16(raw, i * MC_SIZE + 2) // 64,
                             u16(raw, i * MC_SIZE + 4),
                             "MOVABLE" if u16(raw, i * MC_SIZE + 8) else ""))
        stale = [k for k, c in w1.items() if c not in held]
        print("  3 every word live    %s" % ("yes" if not stale else
                                             "STALE: %s" % stale))
        if stale:
            fails.append("stale words: %s" % stale)
        bad = [k for k in moved if k in hashes and w1[k] in held and
               hashlib.md5(m.read(w1[k] * 16, held[w1[k]][0] * 16)).digest()
               != hashes[k]]
        print("  4 contents survived  %s" % ("yes" if not bad else
                                             "CHANGED: %s" % bad))
        if bad:
            fails.append("contents changed: %s" % bad)
        shot1 = repaint()
        if a.clock and clock() != tick0:
            fails.append("the clock stepped between the two pictures "
                         "(%02d:%02d step %d -> %02d:%02d step %d): check 5 "
                         "compared two times, not two redraws"
                         % (tick0 + clock()))
        W = shot0[0]
        # ...and ONLY the target's own pixels: a window ABOVE it in the
        # z-order repaints its own way (a Disk window's selection over a Desk
        # Clock, which sits on the desk layer under every window), so those
        # rects are masked out of both pictures
        zn = m.read(S("wm_zn"), 1)[0]
        z = list(m.read(S("wm_zord"), zn))
        above = [o for o in os88geom.windows(m, S) if o.visible and o.i in z
                 and win in z and z.index(o.i) > z.index(win)]

        def band(f):
            out = bytearray()
            for yy in range(y, y + h):
                row = bytearray(f[2][(yy * W + x) * 3:(yy * W + x + w) * 3])
                for o in above:
                    if o.y <= yy < o.y + o.h:
                        a0 = max(o.x, x) - x
                        a1 = min(o.x + o.w, x + w) - x
                        if a1 > a0:
                            row[a0 * 3:a1 * 3] = bytes((a1 - a0) * 3)
                out += row
            return bytes(out)
        if a.animated:
            # SEVERAL moments and not one: an animation whose period divides
            # the gap shows the same frame at both ends (BOUNCE.GIF at 10
            # frames a second read frame 1, 7, 1, 7 at half-second steps)
            live = False
            for gap in (0.3, 0.4, 0.5, 0.7):
                os88marty.pace(m, gap)
                live = live or band(shot1) != band(m.fbuf())
            print("  5 still animating    %s" % ("yes" if live else "NO"))
            if not live:
                fails.append("the window stopped changing after the move")
        else:
            same = band(shot0) == band(shot1)
            print("  5 redraws the same   %s" % ("yes" if same else "NO"))
            if not same:
                fails.append("the window does not redraw identically")

    print("pkgmove %s: %s" % (a.app, "FAIL: " + "; ".join(fails) if fails
                              else "5 checks passed"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
