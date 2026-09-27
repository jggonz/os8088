#!/usr/bin/env python3
"""VIDEO.O88 plays CGA IN COLOUR - 320 x 200 x 4 in mode 4, and 160 x 100 x
16 in the text hack - SPEC.md 98.1.3.3, 98.3.12, 98.4.6.

    make && python3 tests/vidcga.py --fmt cga4|c160|c512|text
                                    [--screen cga|vga|herc] [--pal HEX]
                                    [--mono]

ONE CLIP, made here: coloured boxes that move over a coloured ground, and
bursts of noise in every value the format has - 40 frames, a keyframe a
second. CGA4 carries the palette byte `--pal` (default 51h: mode 5's cyan,
red and white, bright, on blue - the one set the BIOS has no call for). On
the CGA 5150 or MartyPC's VGA XT:

1. IS THE FILE TAKEN AS IT IS? Format, layout, mode (4 / text), and C160
   through the shadow while CGA4 is native.
2. IS THE POSTER THE KEY'S GREY? The Preview's claim holds cga4_mono /
   c160_mono of the poster key, byte for byte - the luma of each colour
   against the 4 x 4 Bayer cell.
3. NO WINDOW: Play goes to the full screen, which is these formats' only
   place (98.3.12).
4. EVERY HELD FRAME, IN MEMORY AND ON THE GLASS: held before four frames,
   the screen's memory against the decode - CGA4 the banked mode 4 image,
   C160 each ATTRIBUTE at its odd address with 0DEh in every character -
   and every pixel's RENDERED colour against the colour the file means,
   which is what proves the palette byte and the CRTC retime.
5. IT ENDS: the play finishes with every frame drawn and no error.

Broken on purpose - vp_cgaset's palette calls skipped, or the C160 retime
- the colours on the glass are wrong and it FAILS 4.

C512 (98.1.3.5, 98.3.12.2) is the same five on a CGA only, NATIVE (no
shadow): its canvas is the text screen as it is, so 4's memory is every
character and attribute at its own address, and the glass - MartyPC's
headless capture is RGB, not composite - shows each cell's PATTERN: its
first dot the foreground under 55h and the background under 13h, its
second the foreground and its fourth the background under both. That is
the two glyph rows 0 and 1 the retime shows, the two characters, and the
attribute's two nibbles, in three dots a cell. The card byte (2, both)
must come through as the player's [vp_cgapal].

TEXT (98.1.3.6, 98.3.16) is the 80 x 25 text screen NOT retimed, on any
adapter - the CGA 5150, MartyPC's VGA XT and, `--mono`, the Hercules 5150 -
played NATIVE. 4's memory is every character and attribute at its own
address (B800h, or B000h on the Hercules; not on the VGA, as above), and the
glass is sampled at the centre of each QUARTER of every cell whose glyph no
ROM draws differently - a space, the full block and the four half blocks -
each the attribute's foreground where the glyph is lit and its background
where not: that is the cells, blink off (a background of 8..15 is not a
blinking 0..7), and the attributes, on the card's own font. The canvas is
the whole screen, and its cell (0, 0) - where a mode set leaves the cursor,
which a card draws in the cell's foreground - is a space on a colour: every
dot of it must be its ground at four moments a blink's quarter apart, which
is the cursor off. 2's poster
is text_mono's, taken with the MACHINE's glyphs, which the player names.
A colour file on the Hercules must be refused; a mono one plays there.
Broken on purpose (the blink or the cursor left on) it FAILS 4: the blink
by a quarter of every background of 8..15, the cursor by its two lines.
"""
import argparse
import os
import random
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88build, os88vid as vid, os88geom as geom  # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402

NF, FPS = 40, 15.0
STOPS = (1, 12, 27, NF)
MACHINE = {"cga": "os8088_5150_cga_gla", "vga": "os8088_xt_vga",
           "herc": "os8088_5150_herc_gla"}
RGB16 = [tuple((v * 255 + 31) // 63 for v in vid.STD16[3 * i:3 * i + 3])
         for i in range(16)]


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def clip(tmp, fmt, pal):
    """(path, pixel width, rows): the canvases as colour indexes, packed"""
    rnd = random.Random(8088)
    if fmt == "c512":
        return clip512(tmp)
    if fmt == "text":
        return cliptext(tmp, pal)
    if fmt == "cga4":
        W, H, nv, ppb = 160, 100, 4, 4
        g = vid.Geom(vid.LAY_CGA, W // 4, H)
    else:
        W, H, nv, ppb = 120, 80, 16, 2
        g = vid.Geom(vid.LAY_C160, W // 2, H)
    bits = 8 // ppb
    cvs = []
    px = [0] * (W * H)
    for f in range(NF):
        k = f % 13
        if k == 0:
            px = [(f // 13 + 1) % nv] * (W * H)
        elif k in (5, 6):
            for _ in range(150):
                a = rnd.randrange(W * H - 8)
                for i in range(8):
                    px[a + i] = rnd.randrange(nv)
        else:
            x0, y0 = (f * 7) % (W - 32), (f * 3) % (H - 24)
            for y in range(24):
                for x in range(32):
                    px[(y0 + y) * W + x0 + x] = (f + y // 3) % nv
        cv = bytearray(g.wb * H)
        for y in range(H):
            for x in range(W):
                cv[y * g.wb + x // ppb] |= px[y * W + x] << \
                    (8 - bits * (x % ppb + 1))
        cvs.append(bytes(cv))
    out = os.path.join(tmp, "COLOUR.V88")
    vid.encode_canvases(cvs, g, out, FPS,
                        vid.PF_CGA4 if fmt == "cga4" else vid.PF_C160,
                        title="vidcga clip", keysecs=1.0, poster=1,
                        cgapal=pal if fmt == "cga4" else None)
    vid.verify_v88(out)
    return out, W, H


def clip512(tmp):
    """(path, cells, rows): C512 canvases, each cell one of the 512 codes"""
    rnd = random.Random(512)
    W, H = 60, 80
    g = vid.Geom(vid.LAY_TXT, W * 2, H)
    code = [0] * (W * H)
    cvs = []
    for f in range(NF):
        k = f % 13
        if k == 0:
            code = [(f * 37 + 5) % 512] * (W * H)
        elif k in (5, 6):
            for _ in range(150):
                a = rnd.randrange(W * H - 8)
                for i in range(8):
                    code[a + i] = rnd.randrange(512)
        else:
            x0, y0 = (f * 3) % (W - 16), (f * 3) % (H - 24)
            for y in range(24):
                for x in range(16):
                    code[(y0 + y) * W + x0 + x] = (f * 29 + y * 17) % 512
        cv = bytearray(g.wb * H)
        for i, c in enumerate(code):
            y, x = divmod(i, W)
            cv[y * g.wb + 2 * x] = 0x55 if c >= 256 else 0x13
            cv[y * g.wb + 2 * x + 1] = c & 255
        cvs.append(bytes(cv))
    out = os.path.join(tmp, "COLOUR.V88")
    vid.encode_canvases(cvs, g, out, FPS, vid.PF_C512, title="vidcga clip",
                        keysecs=1.0, poster=1, cgapal=vid.CARD_BOTH)
    vid.verify_v88(out)
    return out, W, H


# TEXT's glyphs whose quarters every ROM agrees on: (TL, TR, BL, BR) lit
TQ = {0x00: (0, 0, 0, 0), 0x20: (0, 0, 0, 0), 0xDB: (1, 1, 1, 1),
      0xDC: (0, 0, 1, 1), 0xDF: (1, 1, 0, 0), 0xDD: (1, 0, 1, 0),
      0xDE: (0, 1, 0, 1)}
TQC = sorted(TQ)
# the cell a mode set leaves the cursor in, (0, 0): a SPACE whose foreground
# is not its background, so a cursor left on - drawn in the foreground - shows
CURSOR_CELL = {1: (0x20, 0x1E), 0: (0x20, 0x07)}


def cliptext(tmp, colour):
    """(path, cells, rows): TEXT canvases - mostly TQ's glyphs, some
    letters, in any attribute (colour) or 07h, 0Fh and 70h (mono)"""
    rnd = random.Random(80 * 25)
    W, H = 80, 25           # the WHOLE screen: the cursor's cell is in it
    g = vid.Geom(vid.LAY_TEXT, W * 2, H)

    def code():
        c = rnd.choice(TQC) if rnd.random() < 0.85 else \
            rnd.randrange(0x21, 0x7F)
        a = rnd.randrange(256) if colour else \
            rnd.choice(vid.TEXT_MONO_ATTRS)
        return c, a
    cell = [(0x20, 0x07)] * (W * H)
    cvs = []
    for f in range(NF):
        k = f % 13
        if k == 0:
            cell = [code()] * (W * H)
        elif k in (5, 6):
            for _ in range(60):
                a = rnd.randrange(W * H - 8)
                for i in range(8):
                    cell[a + i] = code()
        else:
            x0, y0 = (f * 3) % (W - 16), (f * 2) % (H - 8)
            c = code()
            for y in range(8):
                for x in range(16):
                    cell[(y0 + y) * W + x0 + x] = c if (x + y) % 3 else \
                        code()
        cell[0] = CURSOR_CELL[colour]   # where the BIOS leaves the cursor
        cv = bytearray(g.wb * H)
        for i, (c, a) in enumerate(cell):
            y, x = divmod(i, W)
            cv[y * g.wb + 2 * x] = c
            cv[y * g.wb + 2 * x + 1] = a
        cvs.append(bytes(cv))
    out = os.path.join(tmp, "COLOUR.V88")
    vid.encode_canvases(cvs, g, out, FPS, vid.PF_TEXT, title="vidcga text",
                        keysecs=1.0, poster=1,
                        cgapal=vid.TEXT_COLOUR if colour else vid.TEXT_MONO)
    vid.verify_v88(out)
    return out, W, H


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fmt", choices=("cga4", "c160", "c512", "text"),
                    default="cga4")
    ap.add_argument("--mono", action="store_true",
                    help="text: a MONO file (07h, 0Fh, 70h), else colour")
    ap.add_argument("--screen", choices=sorted(MACHINE), default="cga")
    ap.add_argument("--pal", default="51")
    a = ap.parse_args()
    pal = int(a.pal, 16)
    if a.fmt == "text":
        pal = 0 if a.mono else 1
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    bad = []
    if a.fmt == "c512" and a.screen != "cga":
        sys.exit("vidcga: C512 is a CGA's alone (98.3.12.2)")
    if a.screen == "herc" and a.fmt != "text":
        sys.exit("vidcga: the Hercules plays TEXT alone of these")
    txt = a.fmt == "text"
    cols = vid.cga4_colours(pal) if a.fmt == "cga4" else list(range(16))
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88, W, H = clip(tmp, a.fmt, pal)
        r = vid.Reader(v88)
        g = r.g
        disk = os.path.join(tmp, "vidcga.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, v88], check=True,
                       capture_output=True)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=MACHINE[a.screen]) as ui:
            m = ui.m
            w = ui.path("B:/COLOUR.V88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4

            def rw(n):
                return u16(m.read(base + syms[n], 2))

            def rb(n):
                return m.read(base + syms[n], 1)[0]

            def ww(n, v):
                m.write(base + syms[n], struct.pack("<H", v))

            def until(cond, what, guest=90.0):
                os88marty.until(m, cond, what, poll=0.2, limit=600.0,
                                guest=guest)

            def value(cv, x, y):
                if a.fmt == "cga4":
                    return (cv[y * g.wb + x // 4] >> (6 - 2 * (x & 3))) & 3
                b = cv[y * g.wb + x // 2]
                return b >> 4 if not x & 1 else b & 15

            def heldtext(n, what):
                """TEXT: the screen's memory, the glass at every known
                cell's four quarters, and nothing round the canvas"""
                want = vid.decode_at(r, n)
                ty0, tx0 = rw("vp_ty0"), rw("vp_tx0")
                dm = rest = 0
                if a.screen != "vga":
                    vram = bytes(m.read(0xB0000 if a.screen == "herc"
                                        else 0xB8000, 4000))
                    got = b"".join(vram[(ty0 + y) * 160 + tx0:
                                        (ty0 + y) * 160 + tx0 + g.wb]
                                   for y in range(H))
                    dm = sum(1 for x, y in zip(got, want) if x != y)
                    rest = sum(1 for i in range(4000)
                               if vram[i] and not (
                                   ty0 <= i // 160 < ty0 + H and
                                   tx0 <= i % 160 < tx0 + g.wb))
                fw, fh, rgb = m.fbuf(0)
                cw, chh = fw / 80.0, fh / 25.0
                wrong, seen = 0, {}
                for y in range(H):
                    for x in range(W):
                        ch = want[y * g.wb + 2 * x]
                        at = want[y * g.wb + 2 * x + 1]
                        if ch not in TQ:
                            continue
                        for qi, (qx, qy) in enumerate(
                                ((0, 0), (1, 0), (0, 1), (1, 1))):
                            ci = at & 15 if TQ[ch][qi] else at >> 4
                            fx = int((tx0 // 2 + x) * cw +
                                     cw * (0.25 + 0.5 * qx))
                            fy = int((ty0 + y) * chh +
                                     chh * (0.25 + 0.5 * qy))
                            o = 3 * (fy * fw + fx)
                            px = rgb[o:o + 3]
                            if a.screen == "herc":  # (MartyPC hands an
                                # MDA's levels on as palette indexes, normal
                                # 2 and bright 3 - so the SUM orders them)
                                seen.setdefault(ci, []).append(sum(px))
                            elif max(abs(px[j] - RGB16[ci][j])
                                     for j in range(3)) > 8 and not (
                                         ci == 6 and a.screen == "vga" and
                                         tuple(px) == (170, 0, 0)):
                                if wrong < 3:
                                    print("   (cell %d,%d: %02Xh on %02Xh, "
                                          "quarter %d wants colour %d %r, "
                                          "the glass %r)" % (
                                              x, y, ch, at, qi, ci,
                                              RGB16[ci], tuple(px)))
                                wrong += 1
                if seen:                # MONO: black < normal < bright
                    lo = {c: min(v) for c, v in seen.items()}
                    hi = {c: max(v) for c, v in seen.items()}
                    order = [c for c in (0, 7, 15) if c in seen]
                    for c1, c2 in zip(order, order[1:]):
                        if hi[c1] >= lo[c2]:
                            wrong += sum(1 for v in seen[c1] if v >= lo[c2])
                    if 0 in seen and hi[0] > 40:
                        wrong += sum(1 for v in seen[0] if v > 40)
                    print("   the glass's levels: %s" % ", ".join(
                        "%d %d..%d" % (c, lo[c], hi[c]) for c in order))
                # THE CURSOR: cell (0, 0) is a space on a colour, so every
                # dot of it is its background - at four moments a blink's
                # quarter apart, so a cursor left on is seen whichever half
                # of its blink it is in
                bgc = CURSOR_CELL[pal][1] >> 4
                lit = 0
                for look in range(4):
                    if look:
                        os88marty.pace(m, 0.07)
                        fw, fh, rgb = m.fbuf(0)
                    for fy in range(int(chh)):
                        for fx in range(int(cw)):
                            o = 3 * (fy * fw + fx)
                            px = rgb[o:o + 3]
                            if (sum(px) > 0) if a.screen == "herc" else \
                                    max(abs(px[j] - RGB16[bgc][j])
                                        for j in range(3)) > 8:
                                lit += 1
                print("   %s, frame %d: %d of %d bytes in memory differ, %d "
                      "bytes round the canvas not 0, %d quarters on the "
                      "glass not their colour, %d dots of the cursor's "
                      "cell not its ground (%d x %d)" % (what, n, dm, len(want), rest,
                                            wrong, lit, fw, fh))
                if dm or rest or wrong or lit:
                    bad.append("%s: frame %d wrong (%d bytes, %d round, %d "
                               "quarters, %d cursor dots)"
                               % (what, n, dm, rest, wrong, lit))

            def held(n, what):
                if txt:
                    return heldtext(n, what)
                want = vid.decode_at(r, n)
                ty0, tx0 = rw("vp_ty0"), rw("vp_tx0")
                vram = bytes(m.read(0xB8000, 16384))
                if a.fmt == "c512":     # the screen IS the canvas
                    got = b"".join(vram[(ty0 + y) * 160 + tx0:
                                        (ty0 + y) * 160 + tx0 + g.wb]
                                   for y in range(H))
                    chars = 0
                elif a.fmt == "cga4":
                    row = [((ty0 + y) % 2) * 8192 + (ty0 + y) // 2 * 80 +
                           tx0 for y in range(H)]
                    got = b"".join(vram[b:b + g.wb] for b in row)
                    chars = 0
                else:
                    got = bytes(vram[(ty0 + y) * 160 + (tx0 + x) * 2 + 1]
                                for y in range(H) for x in range(g.wb))
                    chars = sum(1 for i in range(0, 16000, 2)
                                if vram[i] != 0xDE) \
                        if a.screen == "cga" else 0
                # (the memory only on a CGA: a VGA keeps B800h's bytes in
                # its planes, odd/even, where the debugger's read of the
                # address does not reach them - the glass is its proof)
                dm = sum(1 for x, y in zip(got, want) if x != y) \
                    if a.screen == "cga" else 0
                # the glass: every pixel's colour at its centre. (MartyPC's
                # VGA draws attribute 6 in text mode as red, 170,0,0, where a
                # VGA's is brown and its own CGA's is brown - the same file
                # - so on that screen alone either is taken for colour 6)
                vgatext = a.fmt == "c160" and a.screen == "vga"
                fw, fh, rgb = m.fbuf(0)
                wrong = 0
                for y in range(H if a.fmt == "c512" else 0):
                    for x in range(W):
                        ch = want[y * g.wb + 2 * x]
                        at = want[y * g.wb + 2 * x + 1]
                        fg, bg = at & 15, at >> 4
                        cell = tx0 // 2 + x
                        fy = ((ty0 + y) * 2) * fh // 200
                        for dot, ci in ((0, fg if ch == 0x55 else bg),
                                        (1, fg), (3, bg)):
                            fx = (cell * 8 + dot) * fw // 640
                            o = 3 * (fy * fw + fx)
                            if max(abs(rgb[o + j] - RGB16[ci][j])
                                   for j in range(3)) > 8:
                                wrong += 1
                for y in range(0 if a.fmt == "c512" else H):
                    for x in range(W):
                        if a.fmt == "cga4":
                            sx, sy = (tx0 * 4 + x) * 2 + 1, ty0 + y
                            fx, fy = sx * fw // 640, sy * fh // 200
                        elif a.screen == "cga":
                            fx, fy = (tx0 * 2 + x) * 4 + 2, (ty0 + y) * 2
                        else:               # 9-dot cells, rows of four
                            cell = tx0 + x // 2
                            fx = cell * 9 + (1 if not x & 1 else 6)
                            fy = (ty0 + y) * 4 + 1
                        o = 3 * (fy * fw + fx)
                        ci = cols[value(want, x, y)]
                        got = rgb[o:o + 3]
                        if max(abs(got[j] - RGB16[ci][j])
                               for j in range(3)) > 8 and not (
                                   ci == 6 and vgatext and
                                   tuple(got) == (170, 0, 0)):
                            wrong += 1
                print("   %s, frame %d: %d of %d bytes in memory differ, %d "
                      "characters not 0DEh, %d of %d pixels on the glass "
                      "not their colour" % (what, n, dm, len(want), chars,
                                            wrong, W * H))
                if dm or chars or wrong:
                    bad.append("%s: frame %d wrong (%d bytes, %d chars, %d "
                               "pixels)" % (what, n, dm, chars, wrong))
            try:
                until(lambda mm: rb("vp_loaded") == 1, "the header", 30.0)
                until(lambda mm: rw("vp_ploads") >= 1, "the poster", 60.0)
                # --- 1
                got1 = (rb("vp_pixfmt") + 1, rb("vp_layout") + 1,
                        rb("vp_mode"), rb("vp_shadow"), rb("vp_ok"))
                refuse = txt and a.screen == "herc" and not a.mono
                want1 = (vid.PF_CGA4, vid.LAY_CGA, 2, 0, 1) \
                    if a.fmt == "cga4" else (vid.PF_C512, vid.LAY_TXT, 0,
                                             0, 1) if a.fmt == "c512" else \
                    (vid.PF_TEXT, vid.LAY_TEXT, 0, 0, 0 if refuse else 1) \
                    if txt else (vid.PF_C160, vid.LAY_C160, 0, 1, 1)
                if refuse:              # (vp_ok 0: no mode taken)
                    got1 = got1[:2] + (0, 0) + got1[4:]
                print("   format %d, layout %d, mode %d, shadow %d, ok %d"
                      % got1)
                if got1 != want1:
                    bad.append("the player read the file as %r, not %r"
                               % (got1, want1))
                if a.fmt == "c512" and rb("vp_cgapal") != vid.CARD_BOTH:
                    bad.append("the card byte came through as %d, not %d"
                               % (rb("vp_cgapal"), vid.CARD_BOTH))
                if txt and rb("vp_cgapal") != pal:
                    bad.append("the colour byte came through as %d, not %d"
                               % (rb("vp_cgapal"), pal))
                if refuse:              # COLOUR on a mono card: refused,
                    msg = bytes(m.read(base + rw("vp_msg"), 40)).split(
                        b"\0")[0]      # in its words
                    print("   refused: %r" % msg.decode("latin-1"))
                    if rw("vp_msg") != syms["vp_s_notxt"]:
                        bad.append("a colour TEXT file on the Hercules was "
                                   "not refused as one: %r" % msg)
                    raise StopIteration
                # --- 2: the poster, key 1
                k = r.key(1)
                surf = g.surface()
                r.apply(surf, k[1], key=True)
                cv = g.canvas(surf)
                if txt:                 # the MACHINE's glyphs, 32..126
                    gf, gl = rb("vp_tqf"), rb("vp_tql")
                    gb = bytes(m.read((rw("vp_tqg") << 4) + rw("vp_tqo"),
                                      (gl - gf + 1) * 8))
                    font = {gf + i: list(gb[i * 8:i * 8 + 8])
                            for i in range(gl - gf + 1)}
                    if rb("vp_tqok") != 1 or gf > 32 or gl < 126:
                        bad.append("the player had no glyphs 32..126 (%d, "
                                   "%d..%d)" % (rb("vp_tqok"), gf, gl))
                wantp = vid.cga4_mono(cv, g.wb, H, pal) \
                    if a.fmt == "cga4" else vid.c512_mono(cv, g.wb, H) \
                    if a.fmt == "c512" else vid.text_mono(
                        cv, g.wb, H, font) if txt else \
                    vid.c160_mono(cv, g.wb, H)
                ps = rw("vp_ps")
                gotp = bytes(m.read(rw("vp_pseg") << 4, len(wantp)))
                dp = sum(1 for x, y in zip(gotp, wantp) if x != y)
                print("   the poster: scale %d, %d of %d bytes differ from "
                      "the host's grey" % (ps, dp, len(wantp)))
                if ps != 1 or dp:
                    bad.append("the poster (scale %d) differs in %d bytes"
                               % (ps, dp))
                # --- 3, 4
                ui.mo.to(8, 190)
                ww("vp_stopat", STOPS[0])
                m.write(base + syms["vp_played"], b"\0")
                m.type_text("p")
                until(lambda mm: rb("vp_ready") == 1, "the play to start")
                if rb("vp_winm"):
                    bad.append("the play went into the window")
                for i, n in enumerate(STOPS):
                    until(lambda mm: rb("vp_held") == 1 and
                          rw("vp_done") == n, "the hold before frame %d" % n)
                    os88marty.pace(m, 0.2)
                    held(n - 1, "hold %d" % i)
                    ww("vp_stopat", STOPS[i + 1] if i + 1 < len(STOPS)
                       else 0xFFFF)
                    m.write(base + syms["vp_held"], b"\0")
                # --- 5
                until(lambda mm: rb("vp_played") == 1, "the play to end")
                res = (rw("vp_done"), rb("vp_err"), rw("vp_stall"))
                print("   the play: drew %d, error %d, stalls %d" % res)
                if res != (NF, 0, 0):
                    bad.append("the play ended as %r" % (res,))
            except StopIteration:
                pass
            except os88marty.MartyError as e:
                bad.append(str(e).split(".")[0])
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
