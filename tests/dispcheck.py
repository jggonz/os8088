#!/usr/bin/env python3
"""Did os8088 bring the SECOND card up? (SPEC.md 39.13)

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
     against a build without step 3 in it. That comparison is the caller's to
     make (`--sha`), because the reference is a kernel this script cannot
     build; without one it still checks the primary is drawn and the secondary
     is not.
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

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402

# vid_kind -> the MartyPC card type that kind IS, and what that card's
# framebuffer looks like: segment, stride, banks, extent.
KIND = {
    1: ("mda", 0xB000, 90, 4, 720, 348),        # VID_HERC
    2: ("cga", 0xB800, 80, 2, 640, 200),        # VID_CGA
}


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


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
        os88marty.settle(m, gate=lambda mm: os88marty.bar_up(mm, gate_card),
                         card=gate_card)

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
        if lits[sec["idx"]]:
            fail.append("the secondary is not blank - step 3 draws nothing "
                        "on it, so anything there came from somewhere else")
        if a.sha and not shas[pri["idx"]].startswith(a.sha):
            fail.append("the primary's framebuffer is %s, wanted %s"
                        % (shas[pri["idx"]][:16], a.sha))

        # --- is the secondary REALLY a banked graphics framebuffer? ---------
        _, base, stride, banks, _, _ = KIND[other]
        for b in range(banks):
            m.write((base << 4) + b * 0x2000 + b * 10 + 4, bytes([0xFF]))
        m.advance(frames=2, card=sec["idx"])
        w, h, px = m.fbuf(card=sec["idx"])
        pts = [(y, x) for y in range(h) for x in range(w) if px[(y * w + x) * 3]]
        rows = sorted({y for y, _ in pts})
        if len(rows) != banks:
            fail.append("%d banks lit %d row(s) on the secondary - it is not "
                        "in a graphics mode" % (banks, len(rows)))
        else:
            xs = [min(x for y, x in pts if y == r) for r in rows]
            dy = [r - rows[0] for r in rows]
            dx = [x - xs[0] for x in xs]
            say("secondary banked layout: rows %s, x %s" % (dy, dx))
            if dy != list(range(banks)):
                fail.append("banks landed on rows %s, not %s"
                            % (dy, list(range(banks))))
            if dx != [b * 80 for b in range(banks)]:
                fail.append("banks landed at x %s, not %s"
                            % (dx, [b * 80 for b in range(banks)]))
        if hashlib.sha256(m.fbuf(card=pri["idx"])[2]).hexdigest() \
                != shas[pri["idx"]]:
            fail.append("writing the secondary's memory changed the PRIMARY - "
                        "these are one card wearing two addresses")
        else:
            say("primary unchanged by the secondary's writes")

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
