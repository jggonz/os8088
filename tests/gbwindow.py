#!/usr/bin/env python3
"""REALDMG.O88 in a window, on the glass, against dmg-acid2's reference.

    python3 tests/gbwindow.py [--only vga|herc|cga]

docs/plans/REALDMG-PLAN.md wave 3's gate. Boots os8088 on MartyPC with a B:
disk holding REALDMG.O88 and dmg-acid2 (tools/getgbtest.py's, as ACID2.GB),
opens the ROM from the Disk window - the association launches the package
with it - and checks, on each of the three machines (a CGA's desktop is
monochrome, so its window takes the Hercules path):

  * the game RUNS: the frame counter in the package's view (S_FRAMES, read
    through rdg_viewseg off the package's listing) advances;
  * the PICTURE is the reference's, pixel for pixel, in the window's
    content: on VGA the desktop's four greys through OSAPI_GFX_BLITP, on a
    Hercules the reference through the same 2x2 ordered dither the package
    uses (shade 0 lit, 1 a quarter dark, 2 a checkerboard, 3 dark), through
    OSAPI_GFX_BLIT1.

Each machine then takes Machine > Full Screen (docs/REALDMG-SPEC.md
section 15) - the gbfsx gate of docs/plans/REALDMG-PLAN.md wave 4. Each
checks the picture against the SAME reference, so every full-screen writer
is pixel-identical to the windowed one.
- On VGA, the bracket's mode 0Dh frame, with the picture in the middle of
  it.
- On a Hercules, the 720x348 page with the picture at 2x2, each shade a 2x2
  dither pattern.
- On a CGA, 320x200 in four colours with the picture at 2x1. Both of these
  are read out of the card's memory (section 15.1, drawn by damage).

Each also checks that the frame counter advances in there, and that Esc
goes back to a desktop whose window shows the picture again.

THE NEGATIVE CONTROL is the comparator's: the reference mirrored left to
right must NOT match what is on the glass.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests", "rdgcore"))
import os88marty as M  # noqa: E402
import os88ui  # noqa: E402
import rdgacid  # noqa: E402

MACHINES = {
    "vga": "os8088_xt_vga_720b",
    "herc": "os8088_5150_herc_sb_720_gla",
    "cga": "os8088_5150_cga_720b_gla",
}
S_FRAMES = 0x4000 + 0x3E
S_VIEW = 0x4000 + 0x12


def label_off(lst, name):
    for line in open(lst, errors="replace"):
        m = re.match(r"\s*\d+\s+([0-9A-F]{8})\s+\S+\s+(?:<\d+>\s+)?%s\s" % name, line)
        if m:
            return int(m.group(1), 16)
    sys.exit("gbwindow: %s not in %s" % (name, lst))


def find_seg(m, name, viewoff):
    """The running package: a header match whose rdg_viewseg names a view
    whose own S_VIEW agrees - a copy of the file in a disk buffer matches the
    header too, with any word at all where rdg_viewseg would be."""
    buf = m.read(0x10000, 0x90000)
    for o in range(0, len(buf) - 32, 16):
        if buf[o:o + 2] == b"O8" and buf[o + 16:o + 16 + len(name)] == name:
            v = int.from_bytes(m.read(((0x10000 + o) & ~15) + viewoff, 2), "little")
            if v and v < 0xF000 and int.from_bytes(m.read(v * 16 + S_VIEW, 2), "little") == v:
                return (0x10000 + o) >> 4      # its view names itself back
    return None


def shades_vga(px):
    """RGB -> shade by brightness: white 0 ... black 3."""
    levels = sorted(set(px), key=lambda c: -sum(c))
    assert len(levels) <= 4, levels
    return [levels.index(c) for c in px], levels


def dither(want):
    out = []
    for y in range(144):
        for x in range(160):
            s = want[y * 160 + x]
            even_c, even_r = x % 2 == 0, y % 2 == 0
            dark = (s == 3 or (s == 2 and (even_c == even_r)) or (s == 1 and even_c and even_r))
            out.append(0 if dark else 1)
    return out


def frames(m, view):
    return int.from_bytes(m.read(view * 16 + S_FRAMES, 2), "little")


def compare(kind, got, want, label, rows=144):
    """`rows`: a CGA's 200-line desktop cuts the window to fit above the
    dock, and the rows it cut are not on the glass to compare."""
    if kind in ("herc", "cga"):
        exp = dither(want)
        mir = dither([want[y * 160 + 159 - x] for y in range(144) for x in range(160)])
    else:
        exp, mir = want, [want[y * 160 + 159 - x] for y in range(144) for x in range(160)]
    n = rows * 160
    got, exp, mir = got[:n], exp[:n], mir[:n]
    bad = sum(1 for a, b in zip(got, exp) if a != b)
    badm = sum(1 for a, b in zip(got, mir) if a != b)
    print("gbwindow %s: %d of %d pixels differ; mirrored, %d" % (label, bad, n, badm))
    if badm == 0:
        return "%s: the comparator cannot go red: the mirror matches too" % label
    return None if bad == 0 else "%s: %d pixels differ" % (label, bad)


def window_px(ui, m, kind):
    ui.mo.to(600, 120)                      # the pointer off the picture
    M.guest_sleep(m, 1)
    w = ui.window("RealDMG")                # where it is NOW: SNAP may move it
    W, H, data = m.fbuf(0)
    cx, cy = w.x + 1, w.y + os88ui.geom.TITLE_H   # (measured: the frame's
                                            #   top row is the title's first)
    if kind in ("herc", "cga"):             # THE CARD'S OWN BYTES: MartyPC's
        vw, vh, rows = m.vram(kind)         #   rendered Hercules frame is
        return [rows[cy + y][cx + x]        #   cropped and offset, and the
                for y in range(144) for x in range(160)], W, H, data  # VRAM route is exact
    px = [tuple(data[((cy + y) * W + cx + x) * 3:((cy + y) * W + cx + x) * 3 + 3])
          for y in range(144) for x in range(160)]
    return shades_vga(px)[0], W, H, data


def fullscreen(ui, m, view, want):
    """Machine > Full Screen: mode 0Dh with its display cut to the picture
    (REALDMG-SPEC 15): the picture at (0, 0) of the raster as MartyPC draws
    it, from the display's start - the centring is the sync's, which a
    frame buffer cannot show."""
    ui.menu_pick("Machine", "Full Screen")
    M.guest_sleep(m, 3)
    W, H, data = m.fbuf(0)
    if W < 320 or H < 200 or W >= 640 and H >= 480:
        return "no full-screen mode: the frame is %dx%d" % (W, H)
    if os.environ.get("GBWIN_SAVE"):
        M.write_png_rgb(os.environ["GBWIN_SAVE"] + "-fsx-glass.png", W, H, data)
    sx, sy = W // 320, H // 200             # MartyPC may double the mode
    px = [tuple(data[((y * sy) * W + x * sx) * 3:
                     ((y * sy) * W + x * sx) * 3 + 3])
          for y in range(144) for x in range(160)]
    if len(set(px)) > 4:                    # the border's colour is in there
        return "full screen: the picture is not at (0, 0) - %d colours in its rect" % len(set(px))
    got = shades_vga(px)[0]
    err = compare("vga", got, want, "vga full screen (%dx%d)" % (W, H))
    if os.environ.get("GBWIN_SAVE"):
        M.write_png_rgb(os.environ["GBWIN_SAVE"] + "-fsx-glass.png", W, H, data)
    f0 = frames(m, view)
    M.guest_sleep(m, 2)
    f1 = frames(m, view)
    if not err and f1 <= f0:
        err = "full screen: frames did not advance (%d, %d)" % (f0, f1)
    # F1's speed line, under the picture on the border (REALDMG-SPEC 15): it
    # is the bracket's one write that is not the picture's, and it once went
    # to whatever ES a writer before it left - the package's own memory
    m.key("F1", up=False)
    M.guest_sleep(m, 1)
    m.key("F1", down=False)
    M.guest_sleep(m, 2)
    W, H, data = m.fbuf(0)
    if os.environ.get("GBWIN_SAVE"):
        M.write_png_rgb(os.environ["GBWIN_SAVE"] + "-fsx-f1.png", W, H, data)
    band = [tuple(data[((y * sy) * W + x * sx) * 3:((y * sy) * W + x * sx) * 3 + 3])
            for y in range(146, 154) for x in range(160)]
    ground = max(set(band), key=band.count)
    lit = sum(1 for c in band if c != ground)
    print("gbwindow vga full screen: F1's line, %d of %d pixels lit" % (lit, len(band)))
    if not err and lit < 100:
        err = "full screen: F1 drew no speed line (%d pixels off the border's colour)" % lit
    m.key("F1", up=False)                   # ...and off again: the window's
    M.guest_sleep(m, 1)                     #   picture is compared after
    m.key("F1", down=False)
    M.guest_sleep(m, 1)
    m.key("Escape", up=False)               # held, so the loop's poll sees it
    M.guest_sleep(m, 1)
    m.key("Escape", down=False)
    M.guest_sleep(m, 3)
    # What says the desktop is back is the window's picture, which the caller
    # compares again: the frame's size cannot, as MartyPC goes on reporting
    # the bracket's 400 scan lines after the BIOS's mode 12h.
    return err


def fs_mono(ui, m, view, want, kind):
    """Machine > Full Screen on a Hercules (720x348, the picture at 2x2 from
    (200, 26), four shades as 2x2 dither patterns) or a CGA (320x200 in four
    colours, the picture at 2x1 from (0, 28)): REALDMG-SPEC section 15.1's
    writers, read out of the card's memory."""
    ui.menu_pick("Machine", "Full Screen")
    M.guest_sleep(m, 4)
    vw, vh, rows = m.vram(kind)
    got, odd = [], 0
    for y in range(144):
        for x in range(160):
            if kind == "herc":
                c = (rows[26 + 2 * y][200 + 2 * x] + rows[26 + 2 * y][201 + 2 * x] +
                     rows[27 + 2 * y][200 + 2 * x] + rows[27 + 2 * y][201 + 2 * x])
                s = {4: 0, 3: 1, 2: 2, 0: 3}.get(c)
            else:
                r = rows[28 + y]
                c0 = r[4 * x] * 2 + r[4 * x + 1]
                c1 = r[4 * x + 2] * 2 + r[4 * x + 3]
                s = {3: 0, 1: 1, 2: 2, 0: 3}.get(c0) if c0 == c1 else None
            if s is None:
                odd += 1
                s = 0
            got.append(s)
    if odd:
        return "%s full screen: %d pixels are no shade's pattern" % (kind, odd)
    exp = want
    mir = [want[y * 160 + 159 - x] for y in range(144) for x in range(160)]
    bad = sum(1 for a, b in zip(got, exp) if a != b)
    badm = sum(1 for a, b in zip(got, mir) if a != b)
    print("gbwindow %s full screen: %d of 23,040 pixels differ; mirrored, %d" % (kind, bad, badm))
    err = None
    if badm == 0:
        err = "%s full screen: the comparator cannot go red" % kind
    elif bad:
        err = "%s full screen: %d pixels differ" % (kind, bad)
    f0 = frames(m, view)
    M.guest_sleep(m, 2)
    f1 = frames(m, view)
    if not err and f1 <= f0:
        err = "%s full screen: frames did not advance (%d, %d)" % (kind, f0, f1)
    m.key("Escape", up=False)
    M.guest_sleep(m, 1)
    m.key("Escape", down=False)
    M.guest_sleep(m, 4)
    return err


def run(kind, want, disk, viewoff):
    img = os.path.join(ROOT, "build", "os8088-720.img")
    with os88ui.boot(img, apps=disk, machine=MACHINES[kind]) as ui:
        m = ui.m
        w = ui.path("B:/ACID2.GB")
        seg, view = None, 0
        for _ in range(60):
            M.guest_sleep(m, 2)
            seg = seg or find_seg(m, b"REALDMG", viewoff)
            if seg:
                view = int.from_bytes(m.read(seg * 16 + viewoff, 2), "little")
            if view:
                f = int.from_bytes(m.read(view * 16 + S_FRAMES, 2), "little")
                if f >= 30:
                    break
        if not view:
            return "the package never started a game"
        f0 = int.from_bytes(m.read(view * 16 + S_FRAMES, 2), "little")
        M.guest_sleep(m, 4)
        f1 = int.from_bytes(m.read(view * 16 + S_FRAMES, 2), "little")
        if f1 <= f0:
            return "frames did not advance (%d, %d)" % (f0, f1)
        got, W, H, data = window_px(ui, m, kind)
        if os.environ.get("GBWIN_SAVE"):
            sv = os.environ["GBWIN_SAVE"]
            M.write_png_rgb(sv + "-%s-glass.png" % kind, W, H, data)
            with open(sv + "-%s-got.pgm" % kind, "wb") as f:
                f.write(b"P5 160 144 255\n" + bytes(255 - 85 * g if kind == "vga" else 255 * g for g in got))
        print("gbwindow %s: frames %d -> %d" % (kind, f0, f1))
        rows = 144
        if kind == "cga":                   # a 200-line desktop cuts the window
            wnow = ui.window("RealDMG")     #   to the band above the dock: the
            rows = min(144, wnow.h - os88ui.geom.TITLE_H - 2)   # rows it kept
            print("gbwindow cga: the window is %d tall, %d rows of the picture" % (wnow.h, rows))
        err = compare(kind, got, want, kind, rows)
        if err:
            return err
        if kind == "vga":                   # Machine > Interlace (REALDMG-SPEC
            for item, label in (("Interlace: Off", "On"),   # 13.2): each draw
                                ("Interlace: On", "Scrolling"),   # defers a
                                ("Interlace: Scrolling", "Doubled")):
                ui.menu_pick("Machine", item)   # parity, and the two together
                M.guest_sleep(m, 2)             #   must be the whole picture
                got, W, H, data = window_px(ui, m, kind)
                err = compare(kind, got, want, "vga window, Interlace: " + label, rows)
                if err:
                    return err
            ui.menu_pick("Machine", "Interlace: Doubled")     # (Off again)
        err = fullscreen(ui, m, view, want) if kind == "vga" else fs_mono(ui, m, view, want, kind)
        if err:
            return err
        got, W, H, data = window_px(ui, m, kind)
        return compare(kind, got, want, "%s window, after the bracket" % kind, rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--only", choices=sorted(MACHINES))
    a = ap.parse_args()
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools/getgbtest.py")],
                          stdout=subprocess.DEVNULL)
    g = lambda n: subprocess.check_output([sys.executable, os.path.join(ROOT, "tools/getgbtest.py"),
                                           "--path", n], text=True).strip()
    w, h, ref = rdgacid.png_grey(g("dmg-acid2-dmg.png"))
    greys = sorted(set(ref), key=lambda c: -sum(c))
    want = [greys.index(c) for c in ref]
    viewoff = label_off(os.path.join(ROOT, "build", "realdmg.lst"), "rdg_viewseg")
    tmp = tempfile.mkdtemp(prefix="gbwin-")
    rom = os.path.join(tmp, "ACID2.GB")
    with open(g("dmg-acid2.gb"), "rb") as i, open(rom, "wb") as o:
        o.write(i.read())
    disk = os.path.join(tmp, "apps.img")
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools/os88disk.py"), "-o", disk,
                           "--size", "720", os.path.join(ROOT, "build", "realdmg.o88"), rom],
                          stdout=subprocess.DEVNULL)
    fails = []
    for kind in sorted(MACHINES):
        if a.only and kind != a.only:
            continue
        err = run(kind, want, disk, viewoff)
        if err:
            fails.append("%s: %s" % (kind, err))
    for f in fails:
        print("FAIL gbwindow " + f)
    print("gbwindow: %s" % ("PASS" if not fails else "FAIL"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
