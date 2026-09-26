#!/usr/bin/env python3
"""DOT DELIRIUM: no wall pixel is EVER on the glass in an actor's colour.

SPEC.md 93.5.19. A band goes down in one pen, so a wall pixel inside an
actor's band used to come out in the actor's colour - and SPEC.md 93.2.3's
concave corner block sits INSIDE the corridor tile an actor turns on, so every
turn lit one. The repair queue put it back a frame later; the field saw the
frame.  The planar band gives the walls a plane of their own, so the block is
written once, blue.

WHAT THIS READS, AND WHEN.  Every pixel the board picture (`dd_bdseg`) says is
ink - walls, the ghost-house door and every corner block - against the glass,
at the ENTRY of every `dd_blit` / `dd_blitp` of a playing frame.  That is the
state after every write the program has made before it, so a pixel written
wrong and put back a few milliseconds later is caught between the two, which a
reading at a frame boundary (tests/dotdel.py leg H) cannot see by
construction.

A pixel an actor's SPRITE covers is excused, and the cover is the sprite's own
bits rather than its box: the actor in front of the wall is the right picture,
and the box excuse is exactly what hid this defect - the corner is inside the
box of the actor standing on it.  Each actor is excused at every position the
glass may still hold it at mid-frame (where it is, where it was last drawn,
and the split's old box), which can only make this more lenient.

A reading is WRONG when a picture pixel is lit and not blue (an actor's pen),
and MISSING when it is black; both are counted, and the row fails on either.

BREAK IT ON PURPOSE: `--nopok` finds every `mov byte [dd_pok], 1` store in
the running image - the frame's, the window's and the W_PAINT's - and makes
them store 0, which is the one-pen build on the
same machine and the same scene - the A/B this row exists to show.

VGA ONLY: one plane has no pen at all (SPEC.md 5.4.2.2).
"""
import argparse
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))

import dotdel as D                                          # noqa: E402
import os88ui                                               # noqa: E402

KEYS = ("ArrowUp", "ArrowLeft", "ArrowDown", "ArrowRight")

# How far back the glass can be, in guest cycles: two 60 Hz refreshes of a
# 4.77 MHz 8088 is ~160,000, and this is ~125 ms - over two DOT DELIRIUM
# frames, so every position an actor held on a picture the card may still be
# showing is excused. Still sprite BITS, so a corner under a band and under no
# sprite stays a failure: --nopok is what says so.
WINDOW = 600000


def census(tag, ui, p, say, samples, nopok=False):
    m = ui.m
    fail = []
    if not D.settle_playing(m, p):
        return ["%s: never reached a playing state" % tag]
    m.pause()
    tw, th = p.w("dd_tw"), p.w("dd_th")
    bdx, bdy = p.w("dd_bdx"), p.w("dd_bdy")
    sb, bdseg, mh, mw = p.w("dd_sb"), p.w("dd_bdseg"), p.w("dd_mh"), p.w("dd_mw")
    pic = bytes(m.read(bdseg << 4, sb * mh))
    ink = [(x, y) for y in range(mh) for x in range(mw)
           if (pic[y * sb + (x >> 3)] >> (7 - (x & 7))) & 1]
    sprsz = 2 * 16
    spr_at = (p.seg << 4) + p.names["dd_spr"]
    if nopok:
        # mov byte [dd_pok], 1 is C6 06 <lo> <hi> 01: make both store 0
        at = p.names["dd_pok"]
        pat = bytes([0xC6, 0x06, at & 0xFF, at >> 8, 0x01])
        img = bytes(m.read(p.seg << 4, at))
        hits = [i for i in range(len(img) - 4) if img[i:i + 5] == pat]
        if len(hits) not in (0, 3):     # 0: the windowed pass patched them
            return ["%s: --nopok found %d dd_pok stores, want 3" % (tag,
                                                                  len(hits))]
        for i in hits:
            m.write((p.seg << 4) + i + 4, b"\x00")
    bps = [{"type": "execseg", "seg": p.seg, "off": D.codeoff("dd_blit")}]
    m.breakpoints(bps)
    wrong, missing, took, looked = {}, {}, 0, 0
    nopl, bad = 0, []                   # readings with the planes OFF, and
                                        # what each wrong one looked like
    kidx = 0
    hist = []                           # (guest cycle, {actor: (x, y, img)})
                                        # for every reading in WINDOW: see
                                        # the note at `spots`
    for i in range(samples):
        if i % 25 == 0:
            m.key(KEYS[kidx % 4], down=True, up=False)
            if kidx:
                m.key(KEYS[(kidx - 1) % 4], down=False, up=True)
            kidx += 1
        m.go()
        if not m.wait_stop(20.0):
            break
        if p.b("dd_state") != 2:            # only a PLAYING frame's writes
            continue
        m.write((p.seg << 4) + p.names["dd_lives"], bytes([99]))
        cover = set()
        now = {}
        cyc = m.status()["cycles"]
        for a in range(5):
            img, limg = p.b("dd_img", a), p.b("dd_limg", a)
            now[a] = [(p.w("dd_x", a) >> 4, p.w("dd_y", a) >> 4, img),
                      (p.w("dd_ox", a), p.w("dd_oy", a), limg),
                      (p.w("dd_pbx", a), p.w("dd_pby", a), limg)]
        hist = [(c, s) for c, s in hist if cyc - c <= WINDOW] + [(cyc, now)]
        for a in range(5):
            if not p.b("dd_shown", a) and not p.b("dd_alive", a):
                continue
            # WHERE THE GLASS MAY STILL HOLD IT: where it is, where it was
            # drawn, the split's old box - at THIS reading and at every one in
            # the last WINDOW of guest time. The glass is the last frame the
            # card FINISHED (MartyPC's fbuf; a VGA's planes cannot be read
            # as memory at all), which is up to two refreshes older than the
            # breakpoint - so a sprite a band has already taken up can still
            # be there, and dd_ox no longer names where. Two readings found
            # that before this did: a ghost's skirt three rows under the box
            # it had already been redrawn in, over a corner block.
            spots, imgs = set(), set()
            for _, snap in hist:
                for x, y, im in snap[a]:
                    spots.add((x, y))
                    imgs.add(im)
            for limg in imgs:
                bits = bytes(m.read(spr_at + limg * sprsz, sprsz))
                for sx, sy in spots:
                    for r in range(th):
                        word = (bits[2 * r] << 8) | bits[2 * r + 1]
                        for c in range(tw):
                            if (word >> (15 - c)) & 1:
                                cover.add((sx + c, sy + r))
        w, h, d = D.screen(m)
        per = len(d) // (w * h)
        took += 1
        pok = p.b("dd_pok")
        nopl += not pok
        before = sum(wrong.values()) + sum(missing.values())
        for x, y in ink:
            if (x, y) in cover:
                continue
            o = ((bdy + y) * w + (bdx + x)) * per
            if o + per > len(d):
                continue
            q = d[o:o + per]
            looked += 1
            if not any(q):
                missing[(x, y)] = missing.get((x, y), 0) + 1
            elif not (q[2] > q[0] and q[2] > q[1]):
                wrong[(x, y)] = wrong.get((x, y), 0) + 1
        if sum(wrong.values()) + sum(missing.values()) != before and \
                len(bad) < 4:
            px = [(xy, tuple(d[((bdy + xy[1]) * w + bdx + xy[0]) * per:
                               ((bdy + xy[1]) * w + bdx + xy[0]) * per + per]))
                  for xy in list(wrong)[-2:]]
            bad.append("reading %d: dd_pok %d, the band being blitted at "
                       "(%d,%d) %dx%d ground %d, pixels %s, actors "
                       "(x,y ox,oy img limg) %s" % (
                           i, pok, p.w("dd_bx0"), p.w("dd_by0"), p.w("dd_bw"),
                           p.w("dd_bh"), p.b("dd_bgnd"), px,
                           [(p.w("dd_x", a) >> 4, p.w("dd_y", a) >> 4,
                             p.w("dd_ox", a), p.w("dd_oy", a),
                             p.b("dd_img", a), p.b("dd_limg", a))
                            for a in range(5)]))
    for k in KEYS:
        m.key(k, down=False, up=True)
    m.breakpoints([])
    m.go()
    nw, nm = sum(wrong.values()), sum(missing.values())
    worst = sorted(((n, t) for t, n in wrong.items()), reverse=True)[:6]
    if took < samples // 4:
        fail.append("%s: only %d readings of a playing frame - this read "
                    "nothing" % (tag, took))
    elif nw or nm:
        fail.append("%s: %d wall-pixel readings in an actor's pen and %d "
                    "black, over %d mid-frame readings (%d pixels each) - a "
                    "wall went down in the wrong pen (SPEC.md 93.5.19). "
                    "Worst (count, (x, y)): %s. %d readings had the planes "
                    "OFF; the first wrong ones: %s"
                    % (tag, nw, nm, took, len(ink), worst, nopl, bad))
    else:
        say("%s: %d mid-frame readings, %d wall pixels looked at, 0 wrong, "
            "0 missing (%d readings with the planes off)"
            % (tag, took, looked, nopl))
    return fail


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--samples", type=int, default=240)
    ap.add_argument("--nopok", action="store_true",
                    help="hold dd_pok at 0: the one-pen build, for the A/B")
    ap.add_argument("--fullscreen-only", action="store_true")
    a = ap.parse_args(argv)
    say = lambda s: print("  " + s)
    fail = []
    names = D.bss()
    with os88ui.boot(a.image, apps=a.apps, machine="os8088_xt_vga") as ui:
        ui.path(D.PKG)
        time.sleep(2)
        p = D.Probe(ui, names)
        ui.m.key("Enter")
        time.sleep(3)
        for what in ("windowed", "fullscreen"):
            if what == "fullscreen":
                ui.m.key("KeyF")
                time.sleep(3)
            elif a.fullscreen_only:
                continue
            fail += census("vga " + what, ui, p, say, a.samples, a.nopok)
    if fail:
        print("ddcorner: %d FAILED" % len(fail))
        for f in fail:
            print("    FAIL: %s" % f)
        return 1
    print("ddcorner: passed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
