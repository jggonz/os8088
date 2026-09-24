#!/usr/bin/env python3
"""PIXELSTEIN 3D's art pipeline holds its rules (SPEC.md 97.4).

    python3 tests/unit/t_pxsart.py

Host-side, soak (`soak -k 't_pxs*'`, one package beside a change to it): the
fifteen committed masters read back as 32 x 32 of the sixteen colours with
no key and no alpha; the losable criterion passes on the CGA4 and Hercules
sets (brick against grey stone); the byte-texture set is the 30,720 bytes
part 3 claims, laid out as px_bt_build lays it; no non-black index maps to
an all-black texel byte IN EITHER SHADE on CGA4, Hercules or C160 (the
blue-stone wall that vanished into the ceiling on the first CGA screendump
was the lit shade; its dark face was a 97% black silhouette on Hercules and
C160 one round later - the first assertion here checked the lit shade
alone); no dark shade is its lit one; the odd-row phase is the generator's;
and the three NEGATIVE CONTROLS - a master with the key in it, one of the
wrong size, and an RGB master with a tRNS colour - are refused in words.
Since wave 4, the HUD's twenty-one one-bit masters too (97.4's contract):
their sizes, their rows as the include packs them, ink in every one, and a
wrong-sized one refused naming the size it must be.
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, eq, done                         # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import pxsart                                               # noqa: E402
import pxslevel                                             # noqa: E402


def main():
    ms = pxsart.masters()
    eq(len(ms), pxsart.NWALL, "fifteen wall masters (pxslevel's materials 1..15)")
    eq(pxsart.NWALL, len(pxslevel.MAT_NAMES), "one master a material name")
    for i, m in enumerate(ms):
        eq((len(m), len(m[0])), (32, 32), "master %d is 32 x 32" % (i + 1))
        flat = [v for row in m for v in row]
        check(all(0 <= v < 16 for v in flat), "master %d is in material indices" % (i + 1))
        check(pxsart.KEY not in flat, "master %d holds no key (index 5)" % (i + 1))
    ok, lines = pxsart.criterion(ms)
    for ln in lines:
        print("  t_pxsart: " + ln)
    check(ok, "the losable criterion: brick vs grey stone on CGA4 and Hercules")

    it = pxsart.ink_tables()
    for be in pxsart.RULED:
        lit, dark = it[be]
        eq((len(lit), len(dark)), (16, 16), "%s: sixteen inks a shade" % be)
        check(lit[0] == 0 and dark[0] == 0, "%s: black is black" % be)
        check(all(v != 0 for v in lit[1:]), "%s: no lit colour maps to all-black" % be)
        # ...AND NO DARK ONE (97.4): the first cut of the rule below stepped
        # the dark shade down "the all-black one included", and blue stone -
        # 79% index 1 over black mortar - was a 97% black silhouette in
        # shadow on Hercules and C160 (review, wave 2)
        check(all(v != 0 for v in dark[1:]), "%s: no dark shade maps to all-black" % be)
        # THE DARK SHADE IS NEVER THE LIT ONE (97.4): a colour the palette
        # has not got (blue on CGA palette 0) matched the same sparse
        # pattern at 100% and at 55%, so a corner between two faces of it
        # carried no shading cue - the dark steps one pattern or level
        # down, or the lit one UP when it is already at the floor
        same = [i for i in range(1, 16) if dark[i] == lit[i]]
        check(not same, "%s: no dark shade is its lit one (%s)" % (be, same or "none"))
        check(dark[1] != lit[1] and lit[1] != 0 and dark[1] != 0,
              "%s: blue (index 1) lights %02X and darkens to %02X, neither black"
              % (be, lit[1], dark[1]))
    eq((it["herc"][0][1], it["herc"][1][1]), (0x11, 0x01),
       "herc: blue is at the ladder's floor, so its LIT steps up (11) and its dark keeps 01")
    rok, rlines = pxsart.ink_rules()
    for ln in rlines:
        print("  t_pxsart: " + ln)
    check(rok, "ink_rules(): what --check asserts, the same three rules")
    lit, dark = it["modex"]
    eq(lit, list(range(16)), "modex: the lit index is the DAC entry")
    eq(dark, list(range(16, 32)), "modex: the dark index is sixteen along")
    lit, dark = it["c160"]
    eq(lit, list(range(16)), "c160: the lit nibble is the colour")
    check(all(dark[i] != lit[i] for i in range(1, 16)), "c160: every dark twin differs from its lit")
    check(all(pxsart.luma(pxsart.PALETTE[dark[i]]) < pxsart.luma(pxsart.PALETTE[i])
              for i in range(2, 16)),
          "c160: every dark twin is darker but blue's (nothing darker than blue is not black)")
    eq((dark[1], dark[8]), (8, 1),
       "c160: blue's shadow is dark grey and dark grey's is blue (the Flat tone table's idiom)")

    for be in ("cga4", "herc", "c160", "modex"):
        bt = pxsart.bt_set(ms, be)
        eq(len(bt), pxsart.BT_SIZE, "%s: the set is %d bytes" % (be, pxsart.BT_SIZE))
    bt = pxsart.bt_set(ms, "cga4")
    # the layout: material 6 (brick), lit, column 3, texel 7
    m, col, v = 6, 3, 7
    want = it["cga4"][0][ms[m - 1][v][col]]
    eq(bt[(m - 1) * pxsart.MATSZ + col * 32 + v], want,
       "cga4: [mat-1][shade][column][v] is the layout px_bt_build writes")
    eq(pxsart.texel(bt, "cga4", m, 0, col << 3, v, 0), want, "texel(): even row, the byte")
    eq(pxsart.texel(bt, "cga4", m, 0, col << 3, v, 1), ((want >> 2) | (want << 6)) & 255,
       "texel(): odd row, ror al, 1 twice (CGA4's phase)")
    bth = pxsart.bt_set(ms, "herc")
    w = bth[(m - 1) * pxsart.MATSZ + 1024 + col * 32 + v]
    eq(pxsart.texel(bth, "herc", m, 1, col << 3, v, 3), ((w >> 3) | (w << 5)) & 255,
       "texel(): the dark shade, ror al, cl (3) on Hercules")
    btc = pxsart.bt_set(ms, "c160")
    pair = 5
    want = (it["c160"][0][ms[m - 1][v][2 * pair]] << 4) | it["c160"][0][ms[m - 1][v][2 * pair + 1]]
    eq(btc[(m - 1) * pxsart.MATSZ + pair * 32 + v], want,
       "c160: a column holds texels u and u+1 of one row, the left in the high nibble")
    eq(pxsart.texel(btc, "c160", m, 0, (2 * pair) << 3, v, 1), want,
       "texel(): C160 takes u >> 4 and has no phase")
    eq(pxsart.phase("modex", 0x2B, 1), 0x2B, "modex has no phase")

    blob = pxsart.art_blob(ms)
    eq(len(blob), pxsart.NWALL * pxsart.WALLSZ, "the art claim is 15 x 512 bytes")
    eq(blob[0] >> 4, ms[0][0][0], "the master's first byte: texel 0 in the high nibble")
    eq(blob[0] & 15, ms[0][0][1], "...and texel 1 in the low")
    z = pxsart.stream(ms)
    whole = len(blob) + len(pxsart.sprite_blob(pxsart.sprites(), pxsart.weapons()))
    check(0 < len(z) < whole, "the LZ4 stream is smaller than the masters it carries - the "
          "walls AND the sprites (%d < %d; wave 6's dog took the stream past the walls "
          "alone, which the first cut compared it with)" % (len(z), whole))

    # the negative controls
    with tempfile.TemporaryDirectory() as d:
        real = pxsart.master_path
        try:
            bad = [[7] * 32 for _ in range(32)]
            bad[3][4] = pxsart.KEY
            p = os.path.join(d, "key.png")
            pxsart.write_png_indexed(p, 32, 32, bad)
            pxsart.master_path = lambda mat: p
            try:
                pxsart.load_master(1)
                check(False, "a master with the key in it is refused")
            except ValueError as e:
                check("KEY" in str(e), "a master with the key in it is refused in words (%s)"
                      % str(e)[:60])
            p2 = os.path.join(d, "size.png")
            pxsart.write_png_indexed(p2, 33, 32, [[7] * 33 for _ in range(32)])
            pxsart.master_path = lambda mat: p2
            try:
                pxsart.load_master(1)
                check(False, "a 33 x 32 master is refused")
            except ValueError as e:
                check("33x32" in str(e), "a 33 x 32 master is refused in words")
            # an RGB master whose tRNS chunk names its background: what an
            # image model hands back, and what read_png read as opaque wall
            # art before the alpha reached the guard (review, wave 2)
            p3 = os.path.join(d, "trns.png")
            rows = [[pxsart.PALETTE[7]] * 32 for _ in range(32)]
            rows[2][3] = pxsart.PALETTE[0]
            raw = b"".join(b"\x00" + bytes(v for px in row for v in px) for row in rows)
            with open(p3, "wb") as f:
                f.write(b"\x89PNG\r\n\x1a\n")
                f.write(pxsart._chunk(b"IHDR", pxsart.struct.pack(">IIBBBBB", 32, 32, 8, 2, 0, 0, 0)))
                f.write(pxsart._chunk(b"tRNS", pxsart.struct.pack(">HHH", 0, 0, 0)))
                f.write(pxsart._chunk(b"IDAT", pxsart.zlib.compress(raw, 9)))
                f.write(pxsart._chunk(b"IEND", b""))
            pxsart.master_path = lambda mat: p3
            try:
                pxsart.load_master(1)
                check(False, "an RGB master with a tRNS colour in it is refused")
            except ValueError as e:
                check("alpha" in str(e) and "(3,2)" in str(e),
                      "an RGB master with a tRNS colour is refused in words, at the pixel (%s)"
                      % str(e)[-60:])
        finally:
            pxsart.master_path = real
    # --- the sprites (wave 3, 97.4, 97.6) ------------------------------------
    sp = pxsart.sprites()
    wp = pxsart.weapons()
    eq(len(sp), pxsart.NSPR, "42 sprite frames: 17 of the guard, 6 decorations, 8 pickups, "
       "11 of the dog (wave 6)")
    eq(pxsart.NSPR, 42, "...forty-two")
    eq((pxsart.D_WALK0, pxsart.D_BITE, pxsart.D_DIE, pxsart.D_DEAD), (31, 39, 40, 41),
       "the dog's frames after the pickups: four facings x two walk phases, the bite, the "
       "fall, the corpse")
    eq(len(pxsart.D_FACING), 8, "the dog's eight facings map onto its four masters")
    check(all(0 <= mm < pxsart.D_NFACE for mm, _ in pxsart.D_FACING),
          "...every facing names one of the four")
    # ...and the ENGINE's copy agrees (review, wave 6): pxact.inc's px_dogfac
    # is the same table by hand, master | 0x80 when mirrored, and nothing else
    # compared them - a regenerated dog with a new D_FACING would disagree
    # with the engine silently
    import re
    src = open(os.path.join(ROOT, "apps", "pixelstein", "pxact.inc")).read()
    mm = re.search(r"^px_dogfac:\s*db\s+([^;\n]+)", src, re.M)
    check(mm is not None, "pxact.inc carries px_dogfac")
    if mm:
        eng = [int(v.strip(), 0) for v in mm.group(1).split(",")]
        eq(eng, [m | (0x80 if f else 0) for m, f in pxsart.D_FACING],
           "px_dogfac (pxact.inc) is D_FACING (pxsart.py), master | 0x80 mirrored")
    eq(len(wp), pxsart.NWPN, "nine weapon frames: three weapons x three")
    eq(len(pxsart.sprite_names()), 42, "one file stem a frame")
    for i, (idx, alpha) in enumerate(sp):
        flat = [v for row in idx for v in row]
        check(pxsart.KEY not in [v for row, ar in zip(idx, alpha) for v, a in zip(row, ar) if a],
              "frame %d holds no key inside an opaque texel" % i) if i % 5 == 0 else None
        runs = pxsart.frame_runs(alpha)
        check(all(len(r) <= pxsart.MAXRUNS for r in runs),
              "frame %d has at most %d spans a column" % (i, pxsart.MAXRUNS)) if i % 5 == 0 else None
    idx, alpha = sp[pxsart.PICK0]
    check(all(alpha[v][c] == 0 for v in range(16) for c in range(32)),
          "a pickup sits in the frame's lower half (rows 16..31)")
    idx, alpha = wp[3]
    eq(len(idx), 32, "a weapon frame is padded to 32 texel rows")
    sok, slines = pxsart.spr_criterion(sp)
    for ln in slines:
        print("  t_pxsart: " + ln)
    check(sok, "the guard's front and side are distinguishable at twelve columns (CGA4, Hercules)")
    for be in ("cga4", "herc", "c160", "modex"):
        st = pxsart.spr_set(sp, wp, be)
        eq(len(st), pxsart.NSPR * pxsart.FRSZ + pxsart.NWPN * pxsart.WFRSZ,
           "%s: the sprite set is %d bytes (part 4)" % (be, len(st)))
    fr = pxsart.spr_frame(sp[0][0], sp[0][1], "cga4")
    runs = pxsart.frame_runs(sp[0][1])
    c = 16
    eq(fr[pxsart.SPR * pxsart.SPR + c * pxsart.RUNSZ], len(runs[c]),
       "a column's run table begins with its count")
    if runs[c]:
        eq((fr[pxsart.SPR * pxsart.SPR + c * pxsart.RUNSZ + 1], fr[pxsart.SPR * pxsart.SPR + c * pxsart.RUNSZ + 2]),
           runs[c][0], "...then (v0, v1), v1 exclusive")
    lit = pxsart.ink_tables()["cga4"][0]
    v = runs[c][0][0] if runs[c] else 0
    eq(fr[c * 32 + v], lit[sp[0][0][v][c]], "a frame's texel bytes are column-major through the LIT table")
    frc = pxsart.spr_frame(sp[0][0], sp[0][1], "c160")
    eq(frc[(c // 2) * 32 + v] >> 4, pxsart.ink_tables()["c160"][0][sp[0][0][v][c & ~1]],
       "c160: a column holds a texel pair, the left in the high nibble")
    # the sprite negative controls: a half-alpha pixel, and the key in an opaque one
    with tempfile.TemporaryDirectory() as d:
        real = pxsart.sprite_path
        try:
            fi, fa = [[7] * 32 for _ in range(32)], [[1] * 32 for _ in range(32)]
            p = os.path.join(d, "half.png")
            raw = b"".join(b"\x00" + b"".join(bytes(pxsart.PALETTE[7]) + (b"\x80" if (x, y) == (3, 4) else b"\xff")
                                              for x in range(32)) for y in range(32))
            with open(p, "wb") as f:
                f.write(b"\x89PNG\r\n\x1a\n")
                f.write(pxsart._chunk(b"IHDR", pxsart.struct.pack(">IIBBBBB", 32, 32, 8, 6, 0, 0, 0)))
                f.write(pxsart._chunk(b"IDAT", pxsart.zlib.compress(raw, 9)))
                f.write(pxsart._chunk(b"IEND", b""))
            pxsart.sprite_path = lambda stem: p
            try:
                pxsart.load_sprite("x", 32, 32)
                check(False, "a sprite with alpha 128 is refused")
            except ValueError as e:
                check("alpha 128" in str(e) and "(3,4)" in str(e),
                      "a sprite with alpha 128 is refused in words, at the pixel")
            fi[5][6] = pxsart.KEY
            p2 = os.path.join(d, "key.png")
            pxsart.write_png_rgba(p2, 32, 32, fi, fa)
            pxsart.sprite_path = lambda stem: p2
            try:
                pxsart.load_sprite("x", 32, 32)
                check(False, "a sprite with the key inside an opaque pixel is refused")
            except ValueError as e:
                check("KEY" in str(e), "a sprite with the key inside an opaque pixel is refused in words")
            fa2 = [[0] * 32 for _ in range(32)]
            for v in range(32):
                fa2[v][8] = v & 1               # sixteen one-texel spans
            fi2 = [[7] * 32 for _ in range(32)]
            try:
                pxsart.frame_runs(fa2)
                check(False, "a column of sixteen spans is refused")
            except ValueError as e:
                check("spans" in str(e) and "column 8" in str(e),
                      "a column of sixteen spans is refused in words, naming the column")
        finally:
            pxsart.sprite_path = real
    # THE HUD MASTERS (wave 4, 97.4's contract, 97.13): twenty-one one-bit
    # masks at their sizes, the include's bytes their rows, and a wrong-sized
    # master refused in words naming the size it must be
    hs = pxsart.huds()
    eq(len(hs), 21, "twenty-one HUD masters: ten digits, six faces, two keys, three weapons")
    for stem, (w, h), bits in hs:
        eq((len(bits[0]), len(bits)), (w, h), "%s is %dx%d" % (stem, w, h))
    dig = dict((stem, b) for stem, _z, b in hs)
    eq(pxsart.hud_bytes(dig["h_digit8"])[:2], bytes(
        [int("".join(str(v) for v in dig["h_digit8"][r]), 2) for r in range(2)]),
       "a HUD row is w / 8 bytes, bit 7 the leftmost pixel")
    check(all(any(any(r) for r in b) for _s, _z, b in hs), "every HUD master has ink in it")
    with tempfile.TemporaryDirectory() as d:
        real = pxsart.hud_path
        try:
            p = os.path.join(d, "wide.png")
            pxsart.write_png_indexed(p, 9, 16, [[15] * 9 for _ in range(16)])
            pxsart.hud_path = lambda stem: p
            try:
                pxsart.load_hud("h_digit0", pxsart.HUD_DIGIT)
                check(False, "a 9 x 16 digit is refused")
            except ValueError as e:
                check("8x16" in str(e), "a 9 x 16 digit is refused in words, naming 8x16")
            # THE GAP (97.4): a digit that fills its cell fuses with the next
            pxsart.write_png_indexed(p, 8, 16, [[15] * 8 for _ in range(16)])
            try:
                pxsart.load_hud("h_digit0", pxsart.HUD_DIGIT)
                check(False, "a digit with ink in its gap column is refused")
            except ValueError as e:
                check("rightmost column" in str(e),
                      "a digit inked in column 7 is refused in words, naming the gap")
            pxsart.write_png_indexed(p, 8, 16, [[15] * 7 + [0] for _ in range(16)])
            try:
                pxsart.load_hud("h_digit0", pxsart.HUD_DIGIT)
                check(False, "a digit with ink in its bottom row is refused")
            except ValueError as e:
                check("bottom row" in str(e), "a digit inked in row 15 is refused in words")
            pxsart.write_png_indexed(p, 8, 16, [[15] * 7 + [0] for _ in range(15)] + [[0] * 8])
            check(len(pxsart.load_hud("h_digit0", pxsart.HUD_DIGIT)) == 16,
                  "a digit whose column 7 and row 15 are ground loads")
            # THE THRESHOLD (97.4: luminance >= 64 of 255): a near-black
            # ground is ground and a mid grey is ink - review r2 found the
            # threshold scaled by 255 twice, so (32,32,32) read as 64 of 64 ink
            pxsart.write_png_rgb(p, 8, 16, [[(32, 32, 32)] * 8 for _ in range(16)])
            eq(sum(map(sum, pxsart.load_hud("h_face0", pxsart.HUD_DIGIT))), 0,
               "an opaque (32,32,32) master is all ground (luminance 32 < 64)")
            pxsart.write_png_rgb(p, 8, 16, [[(80, 80, 80)] * 8 for _ in range(16)])
            eq(sum(map(sum, pxsart.load_hud("h_face0", pxsart.HUD_DIGIT))), 128,
               "an opaque (80,80,80) master is all ink (luminance 80 >= 64)")
        finally:
            pxsart.hud_path = real
    done("t_pxsart")


if __name__ == "__main__":
    main()
