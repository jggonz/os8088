#!/usr/bin/env python3
"""PIXELSTEIN 3D's photographs, regenerated (SPEC.md 97.10, 97.6, 97.15) -
AN INSTRUMENT.

    python3 tests/pxsshots.py [--machine M ...] [--montage-only] [--out DIR]

SPEC.md 97.15 and PIXELSTEIN-PLAN 17.2 cite two photographs as the wave-6
evidence, and build/ is untracked and emptied by `make clean`, so the script
that takes them lives here and not beside a report (review, wave 6's close).
On MartyPC, one launch per machine - os8088_5150_cga_gla, os8088_5150_herc_gla,
os8088_xt_vga - at Textured Low res 64, the world frozen and the player
invulnerable:

**The corridor and the dog.** Scene A's corridor, then the same pose with a
dog (actor kind 1, 97.4) standing two tiles ahead, on every backend: WIN1
on CGA, CGA4 and CGA16 (C160) in the bracket on the CGA 5150; WIN1 on
Hercules and the Hercules bracket on the Hercules one; WIN4 (tier poked to
286) and Mode X on the XT-VGA. Written as
build/pxs-shots/wave6f-<tag>-{corridor,dog}.png, and assembled with
tools/pxssim.py's Textured CGA4 reference on top into
**build/pxs-shots/wave6f-montage-corridor-dog.png**.

**The Tab map (97.6).** In each bracket, Tab: the map of the seen cells drawn
into the view (wave6f-<tag>-map.png); then Esc, which leaves the map up in
the window WITH its bar line (97.13; pxsact leg r4 is the gate, this is the
photograph: wave6f-<wtag>-map-esc.png). Assembled into
**build/pxs-shots/wave6f-montage-map.png**.

Its assertions are the photographs' own preconditions, never a number: the
dog is a sprite candidate and its frame differs from the corridor's, the
map is up (px_mapon, px_mapd drained) in the bracket and still up with
PXM_MAP on the bar after Esc. A shot that fails one is not written as
evidence - the row FAILS. LOOK at the PNGs; nothing here judges a picture.
"""
import argparse
import hashlib
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))     # LAST, so it wins (pxslib)
import os88marty                                                # noqa: E402
import pxslib                                                    # noqa: E402

MACHINES = ("os8088_5150_cga_gla", "os8088_5150_herc_gla", "os8088_xt_vga")
WTAG = {"os8088_5150_cga_gla": "win1cga", "os8088_5150_herc_gla": "win1herc",
        "os8088_xt_vga": "win4"}
FTAG = {"os8088_5150_cga_gla": "cga4", "os8088_5150_herc_gla": "herc",
        "os8088_xt_vga": "modex"}
# the montage's rows, top to bottom (the reference is added above them)
ROWS = (("cga4", "CGA4 bracket"), ("c160", "CGA16 (C160) bracket"),
        ("herc", "Hercules bracket"), ("modex", "Mode X bracket"),
        ("win1cga", "WIN1 on CGA"), ("win1herc", "WIN1 on Hercules"),
        ("win4", "WIN4 (VGA, tier 286)"))
MAPROWS = (("cga4", "win1cga", "CGA4"), ("c160", "win1cga", "CGA16 (C160)"),
           ("herc", "win1herc", "Hercules"), ("modex", "win4", "Mode X / WIN4"))
PXM_MAP = 4                         # pxhud.inc: the bar's "MAP - TAB TO PLAY"
DOG = 1                             # the actor kind (97.4)
FAIL = []


def check(ok, what):
    print("   %-66s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def ticks(g, n, limit=120.0):
    t0 = g.ticks()
    os88marty.until(g.m, lambda mm: (g.ticks() - t0) & 0xFFFFFF >= n,
                    "%d ticks" % n, poll=0.1, limit=limit)


def grab(m, g, path, windowed):
    """The glass, whole (the bracket) or cropped to the window (WIN1/WIN4)."""
    from PIL import Image
    m.pause()
    w, h, pix = m.fbuf(0)
    bx, by = g.word("px_bx"), g.word("px_by")
    m.run()
    os88marty.write_png_rgb(path, w, h, pix)
    if windowed:
        Image.open(path).crop((bx - 8, by - 20, bx - 8 + 528, by - 20 + 150)).save(path)
    return hashlib.md5(open(path, "rb").read()).hexdigest()[:8]


def pair(m, g, out, tag, windowed):
    """Scene A's corridor, then the same pose with a dog two tiles ahead."""
    px, py, head = g.scene("a")
    g.wait_frames(1, limit=300.0)
    os88marty.settle(m)
    c = grab(m, g, os.path.join(out, "wave6f-%s-corridor.png" % tag), windowed)
    m.pause()
    g._mark()
    for i in range(g.byte("px_nact")):
        g.actor_poke(i, state=0)
    g.actor_poke(3, x=px + 2 * 256, y=py, kind=DOG, state=1, hp=1, ang=2048, dir=2,
                 flags=0, frame=0, timer=0)
    g.force_all_poke()
    m.run()
    g.wait_frames(1, limit=300.0)
    cand = g.candidates()
    m.pause()
    g.force_all_poke()
    m.run()
    g.wait_frames(1, limit=300.0)
    os88marty.settle(m)
    d = grab(m, g, os.path.join(out, "wave6f-%s-dog.png" % tag), windowed)
    print("   %s: back %s rung %d detail %d; corridor %s dog %s; candidates %s"
          % (tag, pxslib.PXB.get(g.byte("px_back")), g.byte("px_rung"),
             g.byte("px_detail"), c, d, cand))
    check(any(a == 3 for _, _, a in cand), "%s: the dog is a sprite candidate" % tag)
    check(c != d, "%s: the dog's frame differs from the corridor's" % tag)
    m.pause()
    g.actor_poke(3, state=0)
    g.force_all_poke()
    m.run()
    g.wait_frames(1, limit=300.0)


def map_pair(m, g, out, ftag, wtag):
    """Tab in the bracket (the map), then Esc: the window keeps it and its bar."""
    px, py, head = g.scene("a")
    g.wait_frames(1, limit=300.0)
    for h in range(0, 4096, 256):       # a turn on the spot, so the map has
        m.pause()                       # more than one corridor's cells
        g.eye_poke(px, py, h)
        g.force_all_poke()
        m.run()
        g.wait_frames(1, limit=300.0)
    f0 = g.word("px_frames")
    m.type_text("\t")
    os88marty.until(m, lambda mm: g.byte("px_mapon") == 1 and g.byte("px_mapd") == 0
                    and g.word("px_frames") != f0, "the map", poll=0.1, limit=120.0)
    ticks(g, 3)
    os88marty.settle(m)
    grab(m, g, os.path.join(out, "wave6f-%s-map.png" % ftag), False)
    check(g.byte("px_mapon") == 1, "%s: Tab put the map up in the bracket" % ftag)
    g.leave_fsx(limit=300.0)
    ticks(g, 6)
    os88marty.settle(m)
    mo, hm = g.byte("px_mapon"), g.byte("px_hmsg")
    grab(m, g, os.path.join(out, "wave6f-%s-map-esc.png" % ftag), True)
    check(mo == 1 and hm == PXM_MAP, "%s: after Esc the window keeps the map and its bar "
          "line (px_mapon %d, px_hmsg %d)" % (ftag, mo, hm))
    m.type_text("\t")
    os88marty.until(m, lambda mm: g.byte("px_mapon") == 0, "the map down", poll=0.1,
                    limit=120.0)
    g.wait_frames(1, limit=300.0)


def shoot(machine, out):
    with os88marty.launch("build/os8088-360.img", apps="build/games360.img",
                          machine=machine) as m:
        g = pxslib.open_game(m)
        g.sim(False)
        g.god(True)
        if machine == "os8088_xt_vga":          # WIN4 wants a 286's tier
            m.pause()
            g.poke_byte("px_tier", 1)
            g.poke_byte("px_colour", 1)
            m.run()
        g.pin(rung="tex", lowres=True, size=64)
        g.wait_frames(1, limit=300.0)
        pair(m, g, out, WTAG[machine], True)
        brackets = [(FTAG[machine], None)]
        if machine == "os8088_5150_cga_gla":
            brackets.append(("c160", 2))
        for ftag, mode in brackets:
            if mode is not None:
                m.pause()
                g.poke_byte("px_mode", mode)        # PXB_C160
                g.poke_byte("px_fsxm", 0)           # FSXM_TEXT80
                m.run()
            g.enter_fsx(limit=300.0)
            g.pin(rung="tex", lowres=True, size=64)
            g.wait_frames(1, limit=300.0)
            pair(m, g, out, ftag, False)
            map_pair(m, g, out, ftag, WTAG[machine])    # leaves the bracket


def montage(out):
    from PIL import Image, ImageDraw
    ref = os.path.join(out, "wave6f-reference-tex-cga4.png")
    subprocess.run([sys.executable, "tools/pxssim.py", "--scene", "a", "--rung", "tex",
                    "--backend", "cga4", "--res", "low", "--size", "64", "--png", ref],
                   check=True, cwd=ROOT, capture_output=True)
    W, GAP, LAB = 720, 8, 18        # the widest glass (Hercules), unscaled:
                                    # a 1bpp picture resized drops columns

    def fit(p):
        im = Image.open(p).convert("RGB")
        if im.width > W:
            im = im.resize((W, im.height * W // im.width), Image.NEAREST)
        return im

    def build(rows, path):
        h = sum(LAB + max(i.height for i in ims) for _, ims in rows)
        can = Image.new("RGB", (2 * W + GAP, h), (40, 40, 40))
        d = ImageDraw.Draw(can)
        y = 0
        for label, ims in rows:
            d.text((4, y + 3), label, fill=(255, 255, 255))
            y += LAB
            for k, im in enumerate(ims):
                can.paste(im, (k * (W + GAP), y))
            y += max(i.height for i in ims)
        can.save(path)
        print("   wrote %s (%dx%d)" % (path, can.width, can.height))

    rows = [("reference (pxssim, tex, CGA4)", [fit(ref)])]
    for tag, label in ROWS:
        c = os.path.join(out, "wave6f-%s-corridor.png" % tag)
        d = os.path.join(out, "wave6f-%s-dog.png" % tag)
        if os.path.exists(c) and os.path.exists(d):
            rows.append(("%s  (left the corridor, scene A; right the low placeholder "
                         "dog 2 tiles ahead)" % label, [fit(c), fit(d)]))
    build(rows, os.path.join(out, "wave6f-montage-corridor-dog.png"))
    rows = []
    for ftag, wtag, label in MAPROWS:
        a = os.path.join(out, "wave6f-%s-map.png" % ftag)
        b = os.path.join(out, "wave6f-%s-map-esc.png" % ftag)
        if os.path.exists(a) and os.path.exists(b):
            rows.append(("%s  (left Tab in the bracket; right after Esc: the window "
                         "keeps the map and its bar line)" % label, [fit(a), fit(b)]))
    if rows:
        build(rows, os.path.join(out, "wave6f-montage-map.png"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", action="append", choices=MACHINES,
                    help="one machine (repeatable); all three by default")
    ap.add_argument("--montage-only", action="store_true",
                    help="assemble the montages from the shots already taken")
    ap.add_argument("--out", default="build/pxs-shots")
    a = ap.parse_args()
    os.chdir(ROOT)
    os.makedirs(a.out, exist_ok=True)
    if not a.montage_only:
        for machine in a.machine or MACHINES:
            print("pxsshots: %s" % machine)
            shoot(machine, a.out)
    montage(a.out)
    if FAIL:
        print("pxsshots: FAIL (%d)" % len(FAIL))
        for f in FAIL:
            print("  -", f)
        return 1
    print("pxsshots: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
