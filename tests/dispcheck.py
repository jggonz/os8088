#!/usr/bin/env python3
"""Did os8088 bring the SECOND card up, can it DRAW on it, and does the
   POINTER cross? (SPEC.md 39.13/39.14/39.15)

    python3 tests/dispcheck.py                        # the machine's own answer
    python3 tests/dispcheck.py --primary herc         # ...with make VIDEO=herc

tests/dualcheck.py is the gate on the EMULATOR - two cards that are genuinely
two cards, with no kernel involved. This is the gate on the KERNEL: given such
a machine, does `vid_disp_init` programme the one it is not running on,
without touching the one it is?

BLACK IS NOT EVIDENCE, and that is the whole reason this is longer than three
lines. `vid_setmode` clears the framebuffer it programmes, so a correctly
brought-up secondary and a card nobody has touched both render an empty
screen, and "both monitors lit" as an eyeball test cannot tell them apart. So
three assertions instead:

  1. THE PRIMARY IS UNCHANGED - its rendered framebuffer hashed and compared
     against a build without this work in it. That comparison is the caller's
     to make (`--sha`), because the reference is a kernel this script cannot
     build; without one it still checks the primary is drawn and the
     secondary carries a desktop and nothing else.
  2. THE RECORDS DESCRIBE THE RIGHT CARDS - display 0 the primary at the
     virtual origin, display 1 the other one immediately to its right, each
     with its own segment, stride and extent, and the live block still being
     display 0's.
  3. THE SECONDARY IS A REAL BANKED FRAMEBUFFER. One 0xFF per bank is written
     from the HOST and the card is asked what it rasterised: SPEC.md 39.3 says
     bank b is row b, so four banks must appear as four CONSECUTIVE rows at
     x offsets 0, 80, 160, 240. That is what separates a card in a graphics
     mode from a card in a text mode - both have memory at the same aperture,
     and only the raster can say which. It is checked as SHAPE rather than as
     absolute coordinates, because MartyPC's MDA aperture in Hercules graphics
     is offset by (-16, +2) from the guest's own origin (docs/MARTYPC-DEBUG.md).
  4. A WHOLE SHAPE LANDS ON IT (SPEC.md 39.14.2). The drive column's x is one
     word - [vid_desk_zx], with its erase band either side - so moving it into
     the second display's half of the virtual desktop and posting [cp_dirty]
     makes wm_paint_all draw the volume ICONS and their LABELS there. The
     measurement is the longest horizontal run of lit pixels: a 50% dither
     alternates, so its longest run is ONE, and an icon's white label box is
     tens. Nothing else in this file can tell a glyph from a fill, and this
     does not have to - what it asserts is that whole shapes reached the
     second card at all.
  5. THE POINTER CROSSES, AND LEAVES NOTHING BEHIND (SPEC.md 39.15). Driven
     with real Microsoft packets through the real UART, out onto the second
     display and back: `cur_disp` must follow, and the ROUND TRIP must leave
     both framebuffers exactly as they were - a save-under put back in the
     wrong place smears, and a smear is permanent. Then the pointer is PUSHED
     AT TWO WALLS, because the clamp is the part a rectangle gets wrong: left
     from below the primary must stop at the second display's left edge rather
     than walking into the dead zone, and the far corner must be the union's.
     That second pair is what caught vid_disp_of testing a corrupted x.

WHICH CARD IS PRIMARY IS THE KERNEL'S ANSWER, never MartyPC's `primary` flag.
A `make VIDEO=herc` kernel on os8088_5150_both_gla draws on the Hercules while
the config's first [[machine.video]] is the CGA - so `--primary herc` also
tells the boot gate which card to watch, or `launch` waits 120 s for a menu
bar on a card nothing is drawing on and reports a machine that booted fine as
one that never booted.
"""
import argparse
import hashlib
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402

# vid_kind -> the MartyPC card type that kind IS, and what that card's
# framebuffer looks like: segment, stride, banks, extent.
KIND = {
    1: ("mda", 0xB000, 90, 4, 720, 348),        # VID_HERC
    2: ("cga", 0xB800, 80, 2, 640, 200),        # VID_CGA
}


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def longest_run(px, w, h):
    """The longest horizontal run of lit pixels. A 50% desktop dither
    answers 1 by construction; anything drawn answers more."""
    best = 0
    for y in range(h):
        run = 0
        base = y * w * 3
        for x in range(w):
            if px[base + x * 3]:
                run += 1
                if run > best:
                    best = run
            else:
                run = 0
    return best


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_both_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--primary", choices=("auto", "herc", "cga"),
                    default="auto",
                    help="which card the KERNEL drives; anything but auto "
                         "means a VIDEO= build, and picks the boot gate's card")
    ap.add_argument("--sha", default=None,
                    help="the primary's expected framebuffer sha256 prefix, "
                         "from a build without step 3")
    a = ap.parse_args(argv)

    defs = ()
    gate_card = None
    if a.primary != "auto":
        want = 2 if a.primary == "herc" else 3        # VID_FORCE, viddet.inc
        defs = ("VID_FORCE=%d" % want,)

    fail = []
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        cards = m.cards()
        if len(cards) != 2:
            sys.exit("dispcheck: %s has %d video card(s), not 2 - and a "
                     "one-card machine cannot answer this question at all"
                     % (a.machine, len(cards)))
        if a.primary != "auto":
            kind = 1 if a.primary == "herc" else 2
            gate_card = [c for c in cards if c["type"] == KIND[kind][0]][0]["idx"]
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up, card=gate_card)

        say = lambda s: print("  " + s)
        cards = m.cards()           # ...AFTER the boot: `frames` is 0 on every
                                    # card before the machine has run, so the
                                    # never-scanned test asked above would fire
                                    # on a perfectly good pair
        for c in cards:
            say("card %d %-4s field %dx%d frames=%d"
                % (c["idx"], c["type"], c["field_w"], c["field_h"],
                   c["frames"]))
            if c["frames"] == 0:
                fail.append("card %d (%s) has never scanned a frame - it is "
                            "not being clocked at all"
                            % (c["idx"], c["type"]))

        S = lambda n: os88sym.linear(n, defs)
        try:
            kind = m.read(S("vid_kind"), 1)[0]
            avail = m.read(S("vid_avail"), 1)[0]
            ndisp = m.read(S("vid_ndisp"), 1)[0]
            cur = m.read(S("vid_cur"), 1)[0]
        except KeyError as e:
            sys.exit("dispcheck: no symbol %s - a kern_small kernel has none "
                     "of this, by design (docs/KERN-SPLIT-PLAN.md)" % e)
        say("vid_kind=%d vid_avail=%02X vid_ndisp=%d vid_cur=%d"
            % (kind, avail, ndisp, cur))
        if avail != 0x06:
            fail.append("vid_avail is %02X, not HERC|CGA" % avail)
        if ndisp != 2:
            fail.append("vid_ndisp is %d, not 2" % ndisp)
        if cur != 0:
            fail.append("vid_cur is %d - the live block is not display 0's"
                        % cur)

        raw = m.read(S("vid_ctx"), 80)
        ctx = [[u16(raw, d * 40 + i * 2) for i in range(20)] for d in (0, 1)]
        for d in (0, 1):
            w = ctx[d]
            say("ctx[%d] seg=%04X stride=%2d cw=%3d ch=%3d rseg=%04X "
                "origin=(%d,%d)"
                % (d, w[0], w[1], w[7], w[8], w[11], w[18], w[19]))
        other = 1 if kind == 2 else 2
        for d, k in ((0, kind), (1, other)):
            _, seg, stride, _, cw, ch = KIND[k]
            got = (ctx[d][0], ctx[d][1], ctx[d][7], ctx[d][8])
            if got != (seg, stride, cw, ch):
                fail.append("ctx[%d] is %s, wanted %s"
                            % (d, got, (seg, stride, cw, ch)))
            if ctx[d][11] != seg:
                fail.append("ctx[%d] renders into %04X, not its own "
                            "framebuffer" % (d, ctx[d][11]))
        if (ctx[0][18], ctx[0][19]) != (0, 0):
            fail.append("display 0 is not at the virtual origin")
        if (ctx[1][18], ctx[1][19]) != (ctx[0][7], 0):
            fail.append("display 1 is not immediately right of display 0")
        live = [u16(m.read(S("vid_seg"), 36), i * 2) for i in range(18)]
        if live != ctx[0][:18]:
            fail.append("the live block is not display 0's record")
        dw, dh = u16(m.read(S("vid_w"), 2)), u16(m.read(S("vid_h"), 2))
        say("desktop %dx%d" % (dw, dh))
        if (dw, dh) != (ctx[0][7], ctx[0][8]):
            fail.append("the desktop is not display 0's extent - step 3 draws "
                        "nothing on the second card and must not claim to")

        # --- the picture on each -------------------------------------------
        pri = [c for c in cards if c["type"] == KIND[kind][0]][0]
        sec = [c for c in cards if c is not pri][0]
        say("os8088 primary = card %d (%s), secondary = card %d (%s)"
            % (pri["idx"], pri["type"], sec["idx"], sec["type"]))
        shas, lits = {}, {}
        for c in (pri, sec):
            w, h, px = m.fbuf(card=c["idx"])
            lits[c["idx"]] = sum(1 for i in range(0, len(px), 3) if px[i])
            shas[c["idx"]] = hashlib.sha256(px).hexdigest()
            say("card %d %-4s %dx%d %6d lit (%5.1f%%) sha=%s"
                % (c["idx"], c["type"], w, h, lits[c["idx"]],
                   100.0 * lits[c["idx"]] / (w * h), shas[c["idx"]][:16]))
        if not lits[pri["idx"]]:
            fail.append("the primary is blank")
        # SPEC.md 39.14.4: wm_paint_all dithers every display, so the
        # secondary carries the 50% desktop and nothing else. A tolerance
        # rather than exactly half because `fbuf` is an APERTURE - 720x350
        # over a 720x348 framebuffer, offset (-16, +2) - so its top and
        # bottom rows are not the kernel's.
        secw, sech, secpx = m.fbuf(card=sec["idx"])
        frac = 100.0 * lits[sec["idx"]] / (secw * sech)
        before = longest_run(secpx, secw, sech)
        say("secondary: %.1f%% lit, longest horizontal run %d px"
            % (frac, before))
        if not 40.0 <= frac <= 60.0:
            fail.append("the secondary is %.1f%% lit, not a desktop dither"
                        % frac)
        if before > 2:
            fail.append("the secondary's longest lit run is %d - a 50%% "
                        "dither alternates, so nothing is drawn there and "
                        "this is something else" % before)
        if a.sha and not shas[pri["idx"]].startswith(a.sha):
            fail.append("the primary's framebuffer is %s, wanted %s"
                        % (shas[pri["idx"]][:16], a.sha))

        # --- is the secondary REALLY a banked graphics framebuffer? ---------
        # A DIFF, not a census: since SPEC.md 39.14.4 the card carries a
        # desktop dither, so "every lit pixel" is half the screen. What the
        # writes change is what they wrote - and on a dithered background that
        # is the DARK half of each byte, which is why the left edge is allowed
        # to be a pixel out either way.
        _, base, stride, banks, _, _ = KIND[other]
        for b in range(banks):
            m.write((base << 4) + b * 0x2000 + b * 10 + 4, bytes([0xFF]))
        m.advance(frames=2, card=sec["idx"])
        m.run()                     # advance() leaves it PAUSED
        w, h, px = m.fbuf(card=sec["idx"])
        pts = [(y, x) for y in range(h) for x in range(w)
               if px[(y * w + x) * 3] != secpx[(y * w + x) * 3]]
        rows = sorted({y for y, _ in pts})
        if len(rows) != banks:
            fail.append("%d banks changed %d row(s) on the secondary - it is "
                        "not in a graphics mode" % (banks, len(rows)))
        else:
            lo = [min(x for y, x in pts if y == r) for r in rows]
            hi = [max(x for y, x in pts if y == r) for r in rows]
            dy = [r - rows[0] for r in rows]
            dx = [x - lo[0] for x in lo]
            say("secondary banked layout: rows %s, x %s (spans %s)"
                % (dy, dx, [hi[i] - lo[i] for i in range(banks)]))
            if dy != list(range(banks)):
                fail.append("banks landed on rows %s, not %s"
                            % (dy, list(range(banks))))
            for b in range(banks):
                if abs(dx[b] - b * 80) > 1:
                    fail.append("bank %d landed at x %+d, not %+d"
                                % (b, dx[b], b * 80))
                if hi[b] - lo[b] > 7:
                    fail.append("bank %d's byte spans %d px, not 8"
                                % (b, hi[b] - lo[b] + 1))
        if hashlib.sha256(m.fbuf(card=pri["idx"])[2]).hexdigest() \
                != shas[pri["idx"]]:
            fail.append("writing the secondary's memory changed the PRIMARY - "
                        "these are one card wearing two addresses")
        else:
            say("primary unchanged by the secondary's writes")

        # --- 4: does the POINTER cross, and does it leave nothing behind?
        # (SPEC.md 39.15). os88mouse drives real Microsoft packets through the
        # real UART and closes the loop on the kernel's own published
        # mouse_x - which is VIRTUAL, so a target on the second display is
        # simply a bigger x, and reaching one at all is the assertion.
        #
        # The strong half is the ROUND TRIP: out onto the second display and
        # back to where it started must leave BOTH framebuffers exactly as
        # they were. A save-under that put a byte back in the wrong place
        # smears, and a smear is permanent - which is the whole reason 7.1.2's
        # move is written the way it is, and the reason 39.15.3 makes the
        # crossing a jump rather than a straddle.
        mo = os88mouse.Mouse(marty=m)
        home = (ctx[0][7] // 4, ctx[0][8] // 2)     # NOT the boot position,
                                                    # or `to` is a no-op and
                                                    # proves nothing
        mo.to(*home)
        m.advance(frames=4, card=pri["idx"])
        m.run()                     # advance() leaves it PAUSED
        pri_before = m.fbuf(card=pri["idx"])[2]
        sec_before = m.fbuf(card=sec["idx"])[2]
        cd0 = m.read(S("cur_disp"), 1)[0]

        away = (ctx[1][18] + ctx[1][7] // 2, ctx[1][8] // 2)
        mo.to(*away)
        m.advance(frames=4, card=sec["idx"])
        m.run()                     # advance() leaves it PAUSED
        cd1 = m.read(S("cur_disp"), 1)[0]
        mx = u16(m.read(S("mouse_x"), 2))
        my = u16(m.read(S("mouse_y"), 2))
        say("pointer %s -> %s, cur_disp %d -> %d, mouse now (%d,%d)"
            % (home, away, cd0, cd1, mx, my))
        if (mx, my) != away:
            fail.append("the pointer would not go to %s - it is at (%d,%d), "
                        "so the clamp is still a rectangle" % (away, mx, my))
        if cd0 != 0 or cd1 != 1:
            fail.append("cur_disp went %d -> %d, wanted 0 -> 1" % (cd0, cd1))
        pri_away = m.fbuf(card=pri["idx"])[2]
        sec_away = m.fbuf(card=sec["idx"])[2]
        if pri_away == pri_before:
            fail.append("the primary is unchanged with the pointer away - "
                        "the arrow never left it")
        if sec_away == sec_before:
            fail.append("the secondary is unchanged with the pointer on it - "
                        "the arrow never arrived")

        mo.to(*home)
        m.advance(frames=4, card=pri["idx"])
        m.run()                     # advance() leaves it PAUSED
        pri_back = m.fbuf(card=pri["idx"])[2]
        sec_back = m.fbuf(card=sec["idx"])[2]
        cd2 = m.read(S("cur_disp"), 1)[0]
        if cd2 != 0:
            fail.append("cur_disp is %d after coming home, wanted 0" % cd2)
        # The primary's menu bar carries a CLOCK, which changes on a schedule
        # of its own; everything below MBAR_H is ours.
        skip = os88marty.MBAR_H * m.fbuf(card=pri["idx"])[0] * 3
        dpri = sum(1 for i in range(skip, len(pri_back), 3)
                   if pri_back[i] != pri_before[i])
        dsec = sum(1 for i in range(0, len(sec_back), 3)
                   if sec_back[i] != sec_before[i])
        say("round trip: %d differing px on the primary (below the bar), "
            "%d on the secondary" % (dpri, dsec))
        if dpri or dsec:
            fail.append("the round trip left %d/%d differing pixels - a "
                        "save-under went back in the wrong place" % (dpri, dsec))

        # --- the clamp is not a rectangle (SPEC.md 39.15.4) ----------------
        # Two cases a per-axis clamp gets wrong, driven the only way that
        # means anything: push the pointer at a wall and see where it stops.
        def push(dx, dy, n=14):
            for _ in range(n):
                mo.m.mouse(dx, dy)
                time.sleep(0.12)
            time.sleep(0.4)
            return mo.where()[:2]

        # The dead zone is the rows the TALLER display has and the shorter
        # one does not, so which display to stand on and which way to push
        # depends on which is taller - it is the Hercules either way, and it
        # is the primary or the secondary depending on the machine.
        tall, short = (1, 0) if ctx[1][8] > ctx[0][8] else (0, 1)
        y = ctx[short][8] + 40                  # inside `tall`, outside `short`
        mo.to(ctx[tall][18] + 40 if tall else ctx[0][7] - 40, y)
        dx, edge = ((-100, ctx[1][18]) if tall == 1
                    else (100, ctx[0][7] - 1))
        got = push(dx, 0)
        say("pushed %s along the row only display %d has: stopped at %s"
            % ("LEFT" if dx < 0 else "RIGHT", tall, got))
        if got[0] != edge:
            fail.append("pushed at the dead zone the pointer reached x=%d, "
                        "wanted %d - it walked into the gap"
                        % (got[0], edge))
        far = (ctx[1][18] + ctx[1][7] - 1, ctx[1][19] + ctx[1][8] - 1)
        mo.to(ctx[1][18] + 40, ctx[1][19] + 40)     # ...on display 1, then out
        got = push(100, 100)
        say("pushed to the OUTER corner: stopped at %s, display ends %s"
            % (got, far))
        if got != far:
            fail.append("the outer corner clamp answered %s, wanted %s"
                        % (got, far))

        # --- 5: can the KERNEL draw a whole shape there? (SPEC.md 39.14.2) --
        # Move the drive column into the second display's half of the virtual
        # desktop and post [cp_dirty]; ui_task's step 3 is a wm_paint_all, and
        # desk_paint then draws every volume's icon and label at the new x.
        zx = ctx[0][7] + ctx[1][7] // 2         # display 1, half way across
        m.write(S("vid_desk_zx"), bytes([zx & 255, zx >> 8]))
        m.write(S("vid_desk_zl"), bytes([(zx - 2) & 255, (zx - 2) >> 8]))
        m.write(S("vid_desk_zr"), bytes([(zx + 50) & 255, (zx + 50) >> 8]))
        m.write(S("cp_dirty"), bytes([1]))
        m.advance(frames=90, card=pri["idx"])
        m.run()                     # advance() leaves it PAUSED
        os88marty.settle(m, gate=os88marty.desktop_up, card=gate_card)
        w, h, px = m.fbuf(card=sec["idx"])
        after = longest_run(px, w, h)
        lit = sum(1 for i in range(0, len(px), 3) if px[i])
        say("secondary after the drive column moved onto it: longest run %d "
            "px, %d lit (%.1f%%)" % (after, lit, 100.0 * lit / (w * h)))
        if after < 16:
            fail.append("nothing whole reached the second display: its "
                        "longest lit run is still %d px" % after)

    print()
    for f in fail:
        print("dispcheck: FAIL: %s" % f)
    if fail:
        return 1
    print("dispcheck: %s brought both cards up, %s primary - PASS"
          % (a.machine, KIND[kind][0]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
