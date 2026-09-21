#!/usr/bin/env python3
"""PIXELSTEIN 3D's art pipeline holds its rules (SPEC.md 96.4).

    python3 tests/unit/t_pxsart.py

Host-side, soak (`soak -k 'pxs*'`, one package beside a change to it): the
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
        # ...AND NO DARK ONE (96.4): the first cut of the rule below stepped
        # the dark shade down "the all-black one included", and blue stone -
        # 79% index 1 over black mortar - was a 97% black silhouette in
        # shadow on Hercules and C160 (review, wave 2)
        check(all(v != 0 for v in dark[1:]), "%s: no dark shade maps to all-black" % be)
        # THE DARK SHADE IS NEVER THE LIT ONE (96.4): a colour the palette
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
    check(0 < len(z) < len(blob), "the LZ4 stream is smaller than the masters (%d < %d)"
          % (len(z), len(blob)))

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
    done("t_pxsart")


if __name__ == "__main__":
    main()
