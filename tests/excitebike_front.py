#!/usr/bin/env python3
"""EXCITEBIKE wave 1: desktop splash and help, loading screen, adapter art,
the first race frames, Alt+Enter, Esc, the refusals and the heap (SPEC.md 102.6).

    make excitebikedisk && python3 tests/excitebike_front.py [--arm vga|cga|herc|ega|all]

Everything is asserted against pixels the COMPILER computed from the committed
sources (tools/excitebike_assets.py), never against pixels read back from the
guest's own art file: the expected splash is re-rendered from splash.json, the
expected loading screen from font.txt, the first race frames from the reference
renderer (tools/exbsim.py: the tile, column and track sources).  Nothing external is read: no ROM, no disassembly.

Per adapter
  vga, cga, herc   the splash pixels (all but the menu box), the help page, the
             window-shade reveal below one XT tick a band, the loading screen
             (a breakpoint just after it is drawn and before the first disk
             read), the first race frames pixel for pixel, `C` cycling the
             palette, Esc back to a pixel-identical splash with the heap claim
             map EXACTLY as it was, Alt+Enter in and out again, and the window
             closing back to the heap the desktop had before it opened.
  herc       the same as cga on the 1-bit desktop: the splash and help, the loading
             screen (720x348, two card pixels a game pixel, 16 pixels in from the
             left), the first race frames, Esc, Alt+Enter, the heap (wave 6).
  ega        Enter REFUSED with the sentence on the glass (no mode change, no claim),
             on a VIDEO=ega kernel (a private build tree; it
             is a VGA card the kernel believes is an EGA, SPEC.md 39.24).

The guest numbers it prints are MartyPC's 4.77 MHz XT: the reveal step, the
load-and-verify, the engine start.
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88marty as M          # noqa: E402
import os88ui                  # noqa: E402
import os88geom as G           # noqa: E402
import os88build               # noqa: E402
import excitebike_assets as X  # noqa: E402
import excitebike_video as V   # noqa: E402
import exbsim                  # noqa: E402

MACHINE = {"vga": "os8088_xt_vga", "cga": "os8088_5150_cga_gla",
           "herc": "os8088_5150_herc_gla", "ega": "os8088_xt_vga"}
OUT = os.path.join(ROOT, "build", "excitebike-proof")


def at(p):
    return os88build.at(p)


# ---------------------------------------------------------------------------
# the guest's symbols: assemble the package with a table of `dw label` on the end
# ---------------------------------------------------------------------------
def symbols():
    d = os.path.join(ROOT, "apps", "excitebike")
    src = open(os.path.join(d, "excitebike.asm")).read()
    names = re.findall(r"^VAR (x[a-z]_\w+),", src, re.M)
    for f in ("excitebike.asm", "front.inc", "video.inc", "world.inc", "game.inc", "vga.inc",
              "cga.inc", "herc.inc", "sprite.inc", "flow.inc", "ai.inc", "audio.inc"):
        names += re.findall(r"^(x[a-z]_\w+):", open(os.path.join(d, f)).read(), re.M)
    names = sorted(set(names))
    with tempfile.TemporaryDirectory() as td:
        p = os.path.join(td, "probe.asm")
        b = os.path.join(td, "probe.bin")
        open(p, "w").write(src + "\n" + "\n".join("dw " + n for n in names))
        subprocess.run(["nasm", "-f", "bin", "-I", os.path.join(ROOT, "apps") + "/",
                        "-I", d + "/", "-I", at("build/excitebike-art") + "/",
                        "-o", b, p], cwd=ROOT, check=True)
        raw = open(b, "rb").read()
        return dict(zip(names, struct.unpack("<%dH" % len(names), raw[-2 * len(names):])))


class Probe:
    def __init__(self, ui, sym):
        self.ui, self.m, self.sym = ui, ui.m, sym
        w = ui.window("8BitBike")
        raw = self.m.read(self.m.sym("wm_wins"), G.MAX_WIN * G.WIN_SIZE)
        self.base = struct.unpack_from("<H", raw, w.i * G.WIN_SIZE + G.W_SEG)[0] << 4

    def addr(self, n):
        return self.base + self.sym[n if re.match(r"x[a-z]_", n) else "xb_" + n]

    def data(self, n, size=1):
        return self.m.read(self.addr(n), size)

    def b(self, n):
        return self.data(n)[0]

    def w(self, n):
        return struct.unpack("<H", self.data(n, 2))[0]

    def put(self, n, data):
        self.m.write(self.addr(n), bytes([data]) if isinstance(data, int) else bytes(data))

    def call(self, n, **args):
        """Run a guest routine to its return; guest milliseconds it took."""
        m = self.m
        saved = m.regs()
        regs = ("ax", "bx", "cx", "dx", "si", "di", "bp", "sp", "ss", "ds", "es", "flags")
        m.cmd(cmd="park", cs=self.base >> 4, ip=self.sym["xb_" + n])
        for r in regs:
            m.setreg(r, args.get(r, saved[r]))
        sp = (saved["sp"] - 2) & 65535
        m.setreg("sp", sp)
        m.write((saved["ss"] << 4) + sp, struct.pack("<H", saved["ip"]))
        start = m.status()["cycles"]
        m.bp_exec((saved["cs"] << 4) + saved["ip"])
        m.run()
        assert m.wait_stop(30) == "breakpoint", n + " never returned"
        ms = (m.status()["cycles"] - start) / M.GUEST_HZ * 1000
        for r in regs:
            m.setreg(r, saved[r])
        return ms


def key(ui, name, done=True):
    ui.m.key(name, up=False)
    M.pace(ui.m, .04)
    ui.m.key(name, down=False)
    M.pace(ui.m, .15)
    if done:
        M.ui_done(ui.m)


def claims(m):
    raw = m.read(m.sym("mem_tab"), G.MEM_MAX * G.MC_SIZE)
    out = []
    for i in range(G.MEM_MAX):
        r = raw[i * G.MC_SIZE:(i + 1) * G.MC_SIZE]
        if struct.unpack_from("<H", r, 0)[0]:
            out.append(struct.unpack_from("<HHHHH", r, 0))
    return sorted(out)


def paras(cl):
    return sum(c[1] for c in cl)


# ---------------------------------------------------------------------------
# captures
# ---------------------------------------------------------------------------
def nearest(palette):
    cache = {}

    def f(rgb):
        if rgb not in cache:
            cache[rgb] = min(range(len(palette)), key=lambda i: sum(
                (a - b) ** 2 for a, b in zip(palette[i], rgb)))
        return cache[rgb]
    return f


def desktop_vga(m):
    w, h, px = m.fbuf(0)
    return w, h, px


def save_png_rgb(name, w, h, px):
    os.makedirs(OUT, exist_ok=True)
    M.write_png_rgb(os.path.join(OUT, name), w, h, px)


def save_png_mono(name, w, h, rows):
    os.makedirs(OUT, exist_ok=True)
    M.write_png(os.path.join(OUT, name), w, h, rows)


def shot(ui, tag, name):
    """The desktop as pixels: EGA indices for VGA, 0/1 rows for the 1-bit desktops."""
    ui.mo.to(2, 2)
    ui.settle()
    return grab(ui.m, tag, name)


def grab(m, tag, name):
    if tag in ("vga", "ega"):
        w, h, px = desktop_vga(m)
        save_png_rgb("%s-%s.png" % (tag, name), w, h, px)
        f = nearest(X.EGA_RGB)
        return w, h, [[f(tuple(px[(y * w + x) * 3:(y * w + x) * 3 + 3])) for x in range(w)]
                      for y in range(h)]
    w, h, rows = m.vram()
    save_png_mono("%s-%s.png" % (tag, name), w, h, rows)
    return w, h, [list(r) for r in rows]


def expected_front(art, sp, tag, scale, help_page):
    """The compiler's own splash for this desktop, as pixels the screen can be
    compared with: EGA indices (vga) or 0/1 (mono)."""
    h = {"vga": 264, "ega": 264, "herc": 264, "cga": 132}[tag]
    g = sp.compose(h, help_page)
    if tag in ("vga", "ega"):
        return g
    out = []
    for y, row in enumerate(g):
        r = []
        for x, v in enumerate(row):
            lvl = X.MONO_LEVEL[v]
            r.append(1 if lvl == 2 or (lvl == 1 and (x + y) & 1 == 0) else 0)
        out.append(r)
    return out


def compare_front(shot_, exp, fx, fy, exclude, tag, what):
    """All pixels of the art at content origin (fx, fy), but the rectangles in
    `exclude` (in the art's own pixel units, text drawn by the OS font)."""
    w, h, pix = shot_
    eh = len(exp)
    bad = 0
    first = None
    for y in range(eh):
        row = pix[fy + y]
        for x in range(432):
            if any(x0 <= x < x1 and y0 <= y < y1 for x0, y0, x1, y1 in exclude):
                continue
            if row[fx + 8 + x] != exp[y][x]:
                bad += 1
                if first is None:
                    first = (x, y, row[fx + 8 + x], exp[y][x])
    assert not bad, "%s %s: %d pixels differ from the compiled art, first at %s" % (
        tag, what, bad, first)


def menu_box(art, tag):
    x0, y0, x1, y1 = art.splash["reserve"]
    if tag == "cga":
        return (x0, y0 // 2, x1, (y1 + 1) // 2)
    return (x0, y0, x1, y1)


def help_text_box(tag):
    if tag == "cga":
        return (10, 4, 314, 130)
    return (10, 9, 315, 259)


def revealed(ui, p):
    M.until(ui.m, lambda _: p.w("reveal") >= p.w("frontheight") and p.w("frontheight") > 0,
            "the window shade finishes", guest=20)
    ui.settle()


# ---------------------------------------------------------------------------
# expected fullscreen images
# ---------------------------------------------------------------------------
def scene_tiles(art, x0=30):
    plain = ["plain_a", "plain_b"]
    ids = [plain[i % 2] for i in range(X.RUNWAY)]
    t = art.tracks[0]
    for pl in (t["pass1"], t["pass2"]):
        ids += X.lap_columns(pl, art.pieces_by_name)[0]
    grid = [[None] * 40 for _ in range(25)]
    for r in range(8):
        for c in range(40):
            grid[r][c] = art.top[r][(x0 + c) % 64]
    for c in range(40):
        rows = X.col_rows(art, ids[x0 + c])
        for r in range(16):
            grid[8 + r][c] = rows[r]
    return grid


def scene_slots(art, x0=30):
    """320x200 palette-slot image of the placeholder (HUD row 24 included)."""
    grid = scene_tiles(art, x0)
    img = [[0] * 320 for _ in range(200)]
    for r in range(24):
        for c in range(40):
            rows = art.tiles[grid[r][c]][1]
            for y in range(8):
                img[r * 8 + y][c * 8:c * 8 + 8] = rows[y]
    text = "WAVE 1 PLACEHOLDER  C:COLOUR ESC:EXIT"
    for i, ch in enumerate(text):
        g = art.font[ord(ch.upper()) - 32]
        for y in range(8):
            for x in range(8):
                if g[y] & (0x80 >> x):
                    img[192 + y][i * 8 + x] = 15
    return img


def hud_text_rows(art, text, row, gs):
    """The glyph pixels of a centred (or placed) text, as a set of (x, y)."""
    pts = set()
    for i, ch in enumerate(text):
        code = ord(ch.upper())
        if ch == " ":
            continue
        g = art.font[code - 32]
        for y in range(8):
            for x in range(8):
                if g[y] & (0x80 >> x):
                    pts.add((gs + i * 8 + x, row * 8 + y))
    return pts


def loading_points(art):
    pts = set()
    for line, row in ((X.LOAD_LINES[0], 10), (X.LOAD_LINES[1], 12)):
        col = (40 - len(line)) // 2
        pts |= hud_text_rows(art, line, row, col * 8)
    return pts


def fb_320x200(m):
    """The 0Dh screen as rows of RGB tuples (the card's own rendering is
    line- and pixel-doubled)."""
    w, h, px = m.fbuf(0)
    assert (w, h) == (640, 400), ("0Dh framebuffer is 640x400 doubled", w, h)
    return [[tuple(px[((2 * y) * w + 2 * x) * 3:((2 * y) * w + 2 * x) * 3 + 3])
             for x in range(320)] for y in range(200)]


def cga_pixels(m):
    raw = m.read(0xB8000, 16384)
    img = []
    for y in range(200):
        base = (y & 1) * 0x2000 + (y >> 1) * 80
        row = []
        for x in range(80):
            b = raw[base + x]
            row += [(b >> 6) & 3, (b >> 4) & 3, (b >> 2) & 3, b & 3]
        img.append(row)
    return img


def theme_rgb(art, theme):
    return X.slot_rgb(art, theme)


def check_scene_vga(m, art, theme, tag):
    img = fb_320x200(m)
    pal = theme_rgb(art, theme)
    f = nearest(pal)
    exp = scene_slots(art)
    bad = first = None
    bad = 0
    for y in range(200):
        for x in range(320):
            got = f(img[y][x])
            if got != exp[y][x]:
                bad += 1
                if first is None:
                    first = (x, y, got, exp[y][x])
    assert not bad, "%s placeholder: %d pixels differ, first at %s (rgb %s)" % (
        tag, bad, first, img[first[1]][first[0]] if first else None)
    # the palette really is the theme's: every used slot's colour is within
    # DAC rounding of the compiled 6-bit value
    for slot in {v for row in exp for v in row}:
        got = next(img[y][x] for y in range(200) for x in range(320) if exp[y][x] == slot)
        want = pal[slot]
        assert all(abs(a - b) <= 6 for a, b in zip(got, want)), (
            "slot %d colour %s is not the theme's %s" % (slot, got, want))


def check_scene_cga(m, art, tag):
    got = cga_pixels(m)
    ink = art.palette["cga"]["p0_high"]
    exp = [[ink[v] for v in row] for row in scene_slots(art)]
    # the HUD text pixels are ink 3, not slot 15's mapping - both are 3 here
    bad = [(x, y) for y in range(200) for x in range(320) if got[y][x] != exp[y][x]]
    assert not bad, "%s placeholder: %d pixels differ, first at %s" % (tag, len(bad), bad[:1])


def check_loading(m, art, tag):
    """At the breakpoint on xb_gfx_load - the loading screen is drawn, the disk
    has not been touched.  CGA: the whole 4-colour screen.  VGA: plane 0 of
    A000:0 (the debugger's peeks read one plane; the text is written to all
    four at once, so any plane shows all of it); the coloured picture is
    checked once the loop has run a frame (check_loading_glass)."""
    pts = loading_points(art)
    if tag == "vga":
        raw = m.read(0xA0000, 8000)
        lit = {(x, y) for y in range(200) for x in range(320)
               if raw[y * 40 + x // 8] & (0x80 >> (x % 8))}
    elif tag == "herc":
        # 360 game pixels of pairs, the 40-cell text grid 16 pixels in from the left (SPEC.md 102.7)
        raw = m.read(0xB0000, 0x8000)
        img = []
        for y in range(200):
            base = (y & 3) * 0x2000 + (y >> 2) * 90
            row = []
            for c in range(90):
                b = raw[base + c]
                row += [(b >> 6) & 3, (b >> 4) & 3, (b >> 2) & 3, b & 3]
            img.append(row)
        lit = {(x - 16, y) for y in range(200) for x in range(360) if img[y][x] != 0}
        assert all(img[y][x + 16] == 3 for x, y in pts), "loading text is not the white pair"
    else:
        img = cga_pixels(m)
        lit = {(x, y) for y in range(200) for x in range(320) if img[y][x] != 0}
        assert all(img[y][x] == 3 for x, y in pts), "loading text is not ink 3"
    assert lit == pts, ("the loading screen differs from the two resident lines",
                        len(lit ^ pts))


def check_loading_glass(m, art):
    """VGA only: the card's own rendering of the same screen, in white."""
    pts = loading_points(art)
    img = fb_320x200(m)
    lit = {(x, y) for y in range(200) for x in range(320) if img[y][x] != (0, 0, 0)}
    assert lit == pts, "the loading screen on the glass differs"
    assert {img[y][x] for x, y in pts} == {(255, 255, 255)} or min(
        next(iter({img[y][x] for x, y in pts}))) >= 240, "loading text is not white"


# ---------------------------------------------------------------------------
# one adapter
# ---------------------------------------------------------------------------
def arm(tag, sym, art, sp):
    machine = MACHINE[tag]
    tree = None
    system = at("build/os8088-360.img")
    if tag == "ega":
        tree = os88build.tree("VIDEO=ega").apply()
        system = tree.img("os8088-360.img")
    disk = at("build/excitebike360.img")
    with os88ui.boot(system, apps=disk, machine=machine) as ui:
        m = ui.m
        ui.open_drive("B")
        ui.settle()
        c_before = claims(m)
        ui.open("8BITBIKE.O88")
        ui.settle()
        p = Probe(ui, sym)
        revealed(ui, p)
        assert p.w("artseg"), "the splash art did not load"
        assert not p.b("fs") and not p.b("help") and not p.b("error")
        color = tag in ("vga", "ega")
        assert bool(p.b("frontcolor")) == color
        assert bool(p.b("frontplay")) == (tag in ("vga", "cga", "herc")), (
            "playable on VGA, CGA and Hercules", tag, p.b("frontplay"))
        scale = 1 if tag == "cga" else 2
        assert p.b("frontscale") == scale
        c_open = claims(m)
        assert paras(c_open) > paras(c_before), "opening the package claimed nothing"
        fx, fy = p.w("frontx"), p.w("fronty")

        # ---- the splash, pixel for pixel
        s0 = shot(ui, tag, "splash")
        fx, fy = p.w("frontx"), p.w("fronty")
        compare_front(s0, expected_front(art, sp, tag, scale, False), fx, fy,
                      [menu_box(art, tag)], tag, "splash")
        print(tag, "splash pixels: identical to the compiled art (menu box excluded)", flush=True)

        # ---- the reveal: one band a timer tick, each below one XT tick
        m.pause()
        m.bp_exec(p.addr("fronttick"))
        m.key("KeyH")
        m.run()
        for _ in range(10):
            assert m.wait_stop(30) == "breakpoint"
            if p.b("help"):
                break
            m.run()
        else:
            raise AssertionError("H did not open help")
        assert p.w("reveal") == 0, "help did not restart the reveal"
        times = []
        while p.w("reveal") < p.w("frontheight"):
            times.append(p.call("fronttick"))
        print("%s help reveal: %d steps, worst %.2f ms, total %.0f ms" %
              (tag, len(times), max(times), sum(times)), flush=True)
        assert max(times) < 55, ("a reveal step delays the next timer tick", max(times))
        m.bp_exec()
        m.run()
        revealed(ui, p)
        s1 = shot(ui, tag, "help")
        exp = expected_front(art, sp, tag, scale, True)
        fx, fy = p.w("frontx"), p.w("fronty")
        compare_front(s1, exp, fx, fy, [help_text_box(tag)], tag, "help page")
        assert s1[2] != s0[2], "help and splash are the same picture"
        print(tag, "help page pixels: identical to the compiled art", flush=True)
        key(ui, "Escape")
        revealed(ui, p)
        assert not p.b("help")
        s2 = shot(ui, tag, "splash-again")
        compare_front(s2, expected_front(art, sp, tag, scale, False), fx, fy,
                      [menu_box(art, tag)], tag, "splash after help")

        if tag == "ega":
            # the refusal: the sentence is on the glass and Enter does nothing
            before = s2[2]
            box = menu_box(art, tag)
            key(ui, "Enter")
            M.pace(m, 1)
            ui.settle()
            assert not p.b("fs") and not p.b("error"), "a refused adapter entered fullscreen"
            assert p.w("gfxseg") == 0
            s3 = shot(ui, tag, "refused")
            # 'VGA, CGA OR HERC ONLY' replaced 'ENTER  START RACE': the sentence is
            # on the glass, and its 18th cell (an 'O') is lit where the 17
            # characters of the playable label leave the cell blank
            y0 = fy + 85 * scale
            x0 = fx + 156
            lit = sum(1 for y in range(y0, y0 + 8 * scale) for x in range(x0, x0 + 144)
                      if s3[2][y][x] != 0)
            assert lit > 60, ("the refusal sentence is not on the glass", lit)
            last = sum(1 for y in range(y0, y0 + 8 * scale) for x in range(x0 + 136, x0 + 144)
                       if s3[2][y][x] != 0)
            assert last > 0, "the refusal label is the 17-character playable one"
            ui.close(ui.window("8BitBike"))
            ui.settle()
            assert claims(m) == c_before or paras(claims(m)) == paras(c_before), (
                "the heap after closing is not the heap before opening",
                paras(claims(m)), paras(c_before))
            print(tag, "refusal: sentence on the glass, Enter refused, heap back: PASS", flush=True)
            return

        # ---- a drag: the click target and the art move together
        ui.move_window(ui.window("8BitBike"), 127, 21 if tag == "cga" else 28)
        ui.settle()
        revealed(ui, p)
        w = ui.window("8BitBike")
        fx, fy = w.x + 1, w.y + G.TITLE_H
        s5 = shot(ui, tag, "moved")
        compare_front(s5, expected_front(art, sp, tag, scale, False), fx, fy,
                      [menu_box(art, tag)], tag, "moved splash")
        c_pre = claims(m)

        # ---- fullscreen: the loading screen, the load, the scene
        def launch(via):
            p.put("noflow", 1)                            # the raw race loop: this is the wave-1 front end's gate
            if via == "click":
                ui.mo.to(fx + 160, fy + 92 * scale)       # START, in the moved window
            m.pause()
            m.bp_exec(p.addr("gfx_load"))
            if via == "enter":
                m.key("Enter")
            elif via == "alt":
                m.alt("Enter", hold=.15)
            else:
                m.mouse(l=True)
            m.run()
            assert m.wait_stop(60) == "breakpoint", "never reached the art load"
            if via == "click":
                m.mouse(l=False)
        launch("enter")
        assert p.b("fs") and not p.b("cga") == (tag == "vga")
        check_loading(m, art, tag)
        print(tag, "loading screen: two resident lines, nothing else, before the first read", flush=True)
        c0 = m.status()["cycles"]
        m.bp_exec(p.addr("font_copy"))
        m.run()
        assert m.wait_stop(120) == "breakpoint"
        c1 = m.status()["cycles"]
        if tag == "vga":
            # the loading screen stayed up through the whole disk read
            m.bp_exec(p.addr("palette"))
            m.run()
            assert m.wait_stop(60) == "breakpoint"
            check_loading_glass(m, art)
            m.bp_exec(p.addr("palette"))
        assert p.w("gfxseg"), "art claim missing"
        m.bp_exec(p.addr("race"))
        m.run()
        assert m.wait_stop(120) == "breakpoint"
        c2 = m.status()["cycles"]
        ms = lambda a, b: (b - a) / M.GUEST_HZ * 1000
        print("%s guest time: load+verify %.0f ms (disk incl.), font/track/palette/engine start %.0f ms" %
              (tag, ms(c0, c1), ms(c1, c2)), flush=True)
        recs = struct.unpack("<10H", p.data("recs", 20))
        assert recs == tuple(sorted(recs)) and recs[0] == 12 + 40, ("record offsets", recs)
        cl_fs = claims(m)
        assert paras(cl_fs) > paras(c_pre), "fullscreen claimed no art memory"
        m.bp_exec()
        m.run()
        # the scroll engine is running: hold it after a few frames and compare the
        # card's picture with the reference renderer (tests/excitebike_video.py is
        # the full gate; this is the front end's own look at the first frame)
        g = V.Game(ui, tag, V.symbols(), exbsim.Ref())
        g.cids = g.ref.course_cids(0)
        M.until(m, lambda _: g.w("xb_frames") > 4, "the race loop renders", guest=30)
        # hold through the game's own frame protocol (xb_tframes = 1): poking xb_tpause
        # mid-frame can land between the simulation step and the render, leaving state
        # one frame ahead of the glass (an intermittent 16-pixel wheel difference on CGA)
        m.pause()                       # every write below lands on a stopped machine
        g.go(1)
        V.compare(g, "%s first race frames" % tag)
        print(tag, "first race frames: pixel-identical to the reference renderer", flush=True)
        # C: the next theme (VGA) or the other CGA profile
        m.run()
        key(ui, "KeyC", False)
        M.pace(m, 1)
        if tag == "vga":
            assert p.b("theme") == 1
            m.pause()
            V.compare(g, "vga theme 1")
            m.run()
        else:
            assert p.b("profile") == 1
            print(tag, "C: profile toggled", flush=True)
        # Esc leaves; the desktop is whole and the heap is where it was
        key(ui, "Escape", False)
        M.until(m, lambda _: p.b("fs") == 0, "the bracket ends", guest=30)
        ui.settle()
        assert not p.b("error")
        assert p.w("gfxseg") == 0
        c_post = claims(m)
        assert c_post == c_pre, ("the claim map after Esc differs", c_pre, c_post)
        revealed(ui, p)
        w = ui.window("8BitBike")
        fx, fy = w.x + 1, w.y + G.TITLE_H
        s6 = shot(ui, tag, "restored")
        compare_front(s6, expected_front(art, sp, tag, scale, False), fx, fy,
                      [menu_box(art, tag)], tag, "splash after fullscreen")
        print(tag, "Esc: splash pixel-identical, claim map unchanged", flush=True)
        # Alt+Enter in and out again (a second visit: no stale state)
        launch("alt")
        m.bp_exec(p.addr("race"))
        m.run()
        assert m.wait_stop(120) == "breakpoint"
        m.bp_exec()
        m.run()
        M.pace(m, .3)
        m.alt("Enter", hold=.15)
        M.until(m, lambda _: p.b("fs") == 0, "Alt+Enter leaves", guest=30)
        ui.settle()
        assert claims(m) == c_pre, "second visit leaked"
        assert not (m.read(0x417, 1)[0] & 8), "Alt release lost across the mode switch"
        print(tag, "Alt+Enter in and out: PASS", flush=True)
        # a click on START enters too (translated against the moved window)
        launch("click")
        m.bp_exec(p.addr("race"))
        m.run()
        assert m.wait_stop(120) == "breakpoint"
        m.bp_exec()
        m.run()
        key(ui, "Escape", False)
        M.until(m, lambda _: p.b("fs") == 0, "Esc after a click launch", guest=30)
        ui.settle()
        # close: the heap returns to what the desktop had before the package
        ui.close(ui.window("8BitBike"))
        ui.settle()
        c_end = claims(m)
        assert paras(c_end) == paras(c_before), (
            "closing the window left the heap changed", paras(c_end), paras(c_before))
        print(tag, "closed: heap back to the desktop's own (%d paragraphs): PASS" %
              paras(c_end), flush=True)
    if tree is not None:
        os88build.plain().apply()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", choices=("vga", "cga", "herc", "ega", "all"), default="all")
    a = ap.parse_args()
    art = X.Art()
    sp = X.Splash(art)
    sym = symbols()
    arms = ("vga", "cga", "herc") if a.arm == "all" else (a.arm,)
    for tag in arms:
        arm(tag, sym, art, sp)
    print("excitebike_front: PASS")


if __name__ == "__main__":
    main()
