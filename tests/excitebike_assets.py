#!/usr/bin/env python3
"""EXCITEBIKE asset compiler: an independent reader of every file it writes (SPEC.md 102.2).

    python3 tests/excitebike_assets.py

Host only, no emulator, nothing external.  `tools/excitebike_assets.py
--selfcheck` says the BUDGETS hold; this says the BYTES mean what the format
says, by reading them back with code that shares nothing with the writer but
the format's own description (docs, SPEC.md 102.2):

  1. two compiles of the committed sources, contact sheets included, are
     byte-identical file for file (the `diff -r` of the acceptance);
  2. 8BBV.GFX / 8BBC.GFX: magic, length, 16-bit checksum, a monotonic record
     directory, then every record decoded back to what the SOURCE text says -
     each tile's 32 (VGA, plane-major) or 16 (CGA, 2bpp) bytes to its 8x8 slot
     grid, the band dictionary, collision rows and class table, the top
     picture, every pose's four layer masks to its 24x24 ink grid, the font;
  3. 8BITBIKE.VGA/.HRC/.CGA: the EXF1 rows decoded packet by packet and
     compared with the rows the layer renderer produces;
  4. the generated NASM: every constant equals the file it describes, and each
     track's run-length stream expands to the piece list it came from.
"""
import filecmp
import os
import re
import struct
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import excitebike_assets as X  # noqa: E402

FAILED = []


def check(cond, what):
    if not cond:
        FAILED.append(what)
        print("FAIL:", what)
    return cond


def parse_packets(blob, off, width_bytes):
    """One EXF1 row: -> bytes.  1..127 repeat, 128..255 literals, 0 ends."""
    out = bytearray()
    while True:
        n = blob[off]
        off += 1
        if n == 0:
            break
        if n < 128:
            out += bytes([blob[off]]) * n
            off += 1
        else:
            n -= 127
            out += blob[off:off + n]
            off += n
    return bytes(out)


def vga_tile_rows(b):
    return [[sum((((b[p * 8 + y] >> (7 - x)) & 1) << p) for p in range(4)) for x in range(8)]
            for y in range(8)]


def cga_tile_rows(b):
    return [[(b[y * 2 + x // 4] >> (6 - 2 * (x % 4))) & 3 for x in range(8)] for y in range(8)]


def mask_grid(masks, w, h):
    bw = (w + 7) // 8
    g = []
    for y in range(h):
        row = ""
        for x in range(w):
            bit = 0x80 >> (x % 8)
            if not masks[0][y * bw + x // 8] & bit:
                row += "."
            else:
                ink = "0"
                for k in (1, 2, 3):
                    if masks[k][y * bw + x // 8] & bit:
                        ink = str(k)
                row += ink
        g.append(row)
    return g


def records(blob, magic, adapter):
    check(blob[:4] == magic, "%s magic" % magic)
    total, stored = struct.unpack_from("<HH", blob, 4)
    check(total == len(blob), "%s length word" % magic)
    z = bytearray(blob)
    z[6:8] = b"\0\0"
    check(sum(z) & 0xFFFF == stored, "%s checksum" % magic)
    check(blob[8] == 1 and blob[9] == adapter, "%s format/adapter" % magic)
    n = struct.unpack_from("<H", blob, 10)[0]
    check(n == 10, "%s has 10 records" % magic)
    d = [struct.unpack_from("<HH", blob, 12 + 4 * i) for i in range(n)]
    pos = 12 + 4 * n
    for i, (o, l) in enumerate(d):
        check(o == pos, "%s record %d contiguous" % (magic, i))
        pos += l
    check(pos == len(blob), "%s records fill the file" % magic)
    return [blob[o:o + l] for o, l in d]


def main():
    with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
        art, files, rep = X.compile_all(a)
        X.compile_all(b)
        na, nb = sorted(os.listdir(a)), sorted(os.listdir(b))
        check(na == nb, "the two output trees list the same files")
        diff = [f for f in na if not filecmp.cmp(os.path.join(a, f), os.path.join(b, f), shallow=False)]
        check(not diff, "two compiles are byte-identical (contact sheets included): %s" % diff)
        for need in ("tiles.png", "poses.png", "pieces.png", "top.png", "scene-vga-a.png", "scene-herc-a.png",
                     "splash-vga.png", ".exb-art"):
            check(need in na, "%s is emitted" % need)
        n_out = len(na)

        # ---- GFX, VGA
        rv = records(files["8BBV.GFX"], b"EXBV", 0)
        rc = records(files["8BBC.GFX"], b"EXBC", 1)
        rh = records(files["8BBH.GFX"], b"EXBH", 2)
        nt = len(art.tiles)
        check(len(rv[0]) == nt * 32 and len(rc[0]) == nt * 16 and len(rh[0]) == nt * 16, "tile record sizes")
        # Hercules (SPEC.md 102.7.1): a tile row is 16 card pixels, a PAIR to a game pixel - 00 black, 11 white,
        # and 10 / 01 by (row + column) parity for the mid level; read back here from the bytes alone
        hl = art.palette["herc"]
        for i, (name, rows, cls) in enumerate(art.tiles):
            rows = art.rows(i, "herc")
            got = []
            for y in range(8):
                row = []
                for h in (0, 1):
                    b = rh[0][i * 16 + y * 2 + h]
                    row += [(b >> 6) & 3, (b >> 4) & 3, (b >> 2) & 3, b & 3]
                got.append(row)
            want = [[(0, 2, 3)[hl[v]] if hl[v] != 1 else (2 if (y + x) % 2 == 0 else 1)
                     for x, v in enumerate(r)] for y, r in enumerate(rows)]
            check(got == want, "Hercules tile %s decodes as pairs" % name)
        ink = art.palette["cga"]["p0_high"]
        for i, (name, rows, cls) in enumerate(art.tiles):
            check(vga_tile_rows(rv[0][i * 32:i * 32 + 32]) == rows, "VGA tile %s decodes" % name)
            check(cga_tile_rows(rc[0][i * 16:i * 16 + 16]) == [[ink[v] for v in r] for r in art.rows(i, "cga")],
                  "CGA tile %s decodes through the ink map" % name)
        for recs in (rv, rc, rh):
            band = recs[1]
            check(len(band) == len(art.col_order) * 16, "band record size")
            for i, cn in enumerate(art.col_order):
                t14, hedge = art.cols[cn]
                check(list(band[i * 16:i * 16 + 16]) == t14 + hedge, "band column %s" % cn)
                check(list(recs[2][i * 6:i * 6 + 6]) == [t14[13 - k] for k in range(6)],
                      "collision rows of %s (tile rows 21..16)" % cn)
            check(len(recs[3]) == 256, "class table is 256 bytes")
            for i, (name, rows, cls) in enumerate(art.tiles):
                check(recs[3][i] == X.CLASSES.index(cls), "class of tile %s" % name)
            check(list(recs[4]) == [v for row in art.top for v in row], "top picture")
            pos = recs[5]
            check(pos[0] == len(art.poses), "pose count")
            for i, (name, body) in enumerate(art.poses):
                base = 1 + i * 288
                masks = [pos[base + k * 72:base + (k + 1) * 72] for k in range(4)]
                check(mask_grid(masks, 24, 24) == body, "pose %s masks round-trip" % name)
            sp = recs[6]
            check(sp[0] == len(art.sprites), "sprite count")
            off = 1
            for name, w, h, body in art.sprites:
                check(sp[off] == w and sp[off + 1] == h, "sprite %s size" % name)
                bw = (w + 7) // 8
                ml = bw * h
                masks = [sp[off + 2 + k * ml:off + 2 + (k + 1) * ml] for k in range(4)]
                check(mask_grid(masks, w, h) == body, "sprite %s masks round-trip" % name)
                off += 2 + 4 * ml
            check(off == len(sp), "sprite record has no slack")
        # fonts
        check(len(rv[7]) == 512 and len(rc[7]) == 1024, "font record sizes")
        for g in range(64):
            check(list(rv[7][g * 8:g * 8 + 8]) == art.font[g], "VGA glyph %d" % g)
            for y in range(8):
                px = "".join("%d%d" % (((rc[7][g * 16 + y * 2 + h] >> (6 - 2 * k)) & 3) >> 1,
                                       (rc[7][g * 16 + y * 2 + h] >> (6 - 2 * k)) & 1)
                             for h in (0, 1) for k in range(4))
                want = "".join("11" if art.font[g][y] & (0x80 >> x) else "00" for x in range(8))
                check(px == want, "CGA glyph %d row %d" % (g, y))
        check(rh[7] == rc[7], "the Hercules font is the CGA's: 11 for a lit pixel")
        # palettes: five themes x 48 DAC bytes + the four ink slots
        pal = rv[8]
        check(len(pal) == 5 * 48 + 4, "VGA palette record size")
        for t, th in enumerate(art.palette["themes"]):
            slots = [tuple(s[1]) for s in art.palette["slots"]]
            for k, v in th["slots"].items():
                slots[int(k)] = tuple(v)
            want = b"".join(bytes(min(63, (c + 2) * 63 // 255) for c in rgb) for rgb in slots)
            check(pal[t * 48:t * 48 + 48] == want, "theme %d DAC values" % t)
        check(list(pal[-4:]) == X.INK_SLOT, "pose ink slots")
        cp = rc[8]
        check(list(cp[:16]) == art.palette["cga"]["p0_high"], "CGA high ink map")
        check(list(cp[32:34]) == [16, 0], "CGA 3D9 values")
        check(list(cp[16:32]) == art.palette["cga"]["p0_low"], "CGA low ink map")
        check(list(cp[34:38]) == X.INK_SLOT, "CGA ink slots")
        hp = rh[8]
        want_ink = [(0, 2, 3)[hl[sl]] for sl in range(16)]
        check(len(hp) == 38 and list(hp[:16]) == want_ink and list(hp[16:32]) == want_ink,
              "Hercules ink maps: the pair a slot's level is")
        check(list(hp[32:34]) == [0, 0] and list(hp[34:38]) == X.INK_SLOT, "Hercules: no 3D9h value, the ink slots")

        # ---- EXF1
        sp = X.Splash(art)
        for tag in ("VGA", "HRC", "CGA"):
            blob = files["8BITBIKE." + tag]
            check(blob[:4] == b"EXF1", "EXF1 magic %s" % tag)
            w, h, depth = struct.unpack_from("<HHH", blob, 4)
            check((w, h, depth) == (432, 132 if tag == "CGA" else 264, 4 if tag == "VGA" else 1),
                  "EXF1 header %s" % tag)
            dirs = struct.unpack_from("<%dH" % (2 * h), blob, 10)
            mode = "vga" if tag == "VGA" else "mono"
            exp = sp.native_rows(sp.compose(h, False), mode) + sp.native_rows(sp.compose(h, True), mode)
            check(len(dirs) == len(exp), "EXF1 %s row count" % tag)
            ok = all(parse_packets(blob, dirs[i], 0) == exp[i] for i in range(len(exp)))
            check(ok, "EXF1 %s rows decode to the rendered layers" % tag)
            check(min(dirs) >= 10 + 4 * h and max(dirs) < len(blob), "EXF1 %s offsets in range" % tag)

        # ---- the generated NASM
        tab = open(os.path.join(a, "exbtables.inc")).read()
        def equ(name):
            return int(re.search(r"^%s equ (\d+)" % name, tab, re.M).group(1))
        check(re.search(r"^EXB_LAP_MAX equ %d" % X.LAP_MAX, open(os.path.join(a, "exbtracks.inc")).read(), re.M)
              and re.search(r"^EXB_RUNWAY equ %d" % X.RUNWAY, open(os.path.join(a, "exbtracks.inc")).read(), re.M),
              "runway and lap budget constants (XB_CID_MAX's inputs)")
        check(equ("EXB_GFX_VGA_SIZE") == len(files["8BBV.GFX"]), "VGA size constant")
        check(equ("EXB_GFX_CGA_SIZE") == len(files["8BBC.GFX"]), "CGA size constant")
        check(equ("EXB_GFX_HRC_SIZE") == len(files["8BBH.GFX"]), "Hercules size constant")
        check(equ("EXB_GFX_HRC_SUM") == struct.unpack_from("<H", files["8BBH.GFX"], 6)[0], "Hercules sum constant")
        check(equ("EXB_GFX_VGA_SUM") == struct.unpack_from("<H", files["8BBV.GFX"], 6)[0], "VGA sum constant")
        check(equ("EXB_GFX_CGA_SUM") == struct.unpack_from("<H", files["8BBC.GFX"], 6)[0], "CGA sum constant")
        for t in ("VGA", "CGA", "HRC"):
            check(equ("EXB_FRONT_%s_SIZE" % t) == len(files["8BITBIKE." + t]), "front %s size" % t)
        for i, cn in enumerate(art.col_order):
            check(equ("EXBCOL_" + cn.upper()) == i, "column id %s" % cn)
        trk = open(os.path.join(a, "exbtracks.inc")).read()
        for ti, t in enumerate(art.tracks, 1):
            for lap, plist in ((1, t["pass1"]), (2, t["pass2"])):
                m = re.search(r"^exb_t%d_s%d:\n    db ([0-9,]+)" % (ti, lap), trk, re.M)
                nums = [int(v) for v in m.group(1).split(",")]
                check(nums[-2:] == [0, 0], "track %d lap %d stream terminates" % (ti, lap))
                ids = []
                for k in range(0, len(nums) - 2, 2):
                    ids += [nums[k]] * nums[k + 1]
                want = [art.col_ids[c] for c in X.lap_columns(plist, art.pieces_by_name)[0]]
                check(ids == want, "track %d lap %d stream expands to its pieces" % (ti, lap))
                # triggers: piece starts derived from the .trk text, read back
                # from the generated _gN list (lap-relative columns)
                sids = {n: i + 1 for i, n in enumerate(art.scripts)}
                starts = X.lap_columns(plist, art.pieces_by_name)[1]
                wt = []
                for x, pn in starts:
                    sn = art.pieces_by_name[pn]["script"]
                    if sn != "-":
                        wt += [x, sids[sn]]
                g = re.search(r"^exb_t%d_g%d:\n    dw ([0-9A-Fh,]+)" % (ti, lap), trk, re.M)
                gv = [int(v[:-1], 16) if v.endswith("h") else int(v) for v in g.group(1).split(",")]
                check(gv[-1] == 0xFFFF and gv[:-1] == wt, "track %d lap %d trigger list" % (ti, lap))
            hd = re.search(r"^exb_t%d: db (\d+),(\d+)\n    dw (\d+),(\d+),(\d+)\n    dw ([^\n]+)\n"
                           r"exb_t%d_name: db '([^']*)',0" % (ti, ti), trk, re.M)
            check(hd is not None, "track %d header present" % ti)
            if hd:
                l1 = len(X.lap_columns(t["pass1"], art.pieces_by_name)[0])
                l2 = len(X.lap_columns(t["pass2"], art.pieces_by_name)[0])
                check((int(hd.group(1)), int(hd.group(2)), int(hd.group(3)), int(hd.group(4)), int(hd.group(5)))
                      == (t["theme"], t["laps"], t["par"], l1, l2), "track %d header words" % ti)
                tg = "exb_t%d" % ti
                check(hd.group(6).replace(" ", "") == "%s_s1,%s_s2,%s_g1,%s_g2,%s_name,%d" % ((tg,) * 5 + (t["par2"],)),
                      "track %d pointers and the second pass's par" % ti)
                check(hd.group(7) == t["title"].upper(), "track %d name" % ti)
        scr = open(os.path.join(a, "exbscripts.inc")).read()
        kinds = {"ANG": 0, "DEFER": 1, "H": 2}
        for n in art.scripts:
            want = []
            for x, k, v in art.scripts[n]:
                want += [x, (kinds[k] << 6) | v]
            want.append(0xFF)
            m = re.search(r"^exb_script_%s: db ([0-9,]+)" % re.escape(n), scr, re.M)
            check(m is not None and [int(v) for v in m.group(1).split(",")] == want, "script %s bytes" % n)
    if FAILED:
        print("excitebike_assets: %d FAILED" % len(FAILED))
        return 1
    print("excitebike_assets: PASS (%d output files, both GFX files, three splash files (row-decoded against the compiler's own composer: art is RLE-checked only) and the "
          "generated NASM read back)" % n_out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
