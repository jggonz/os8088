#!/usr/bin/env python3
"""THE GROUND'S TEXTURE and ULTRA'S FIELDS (SPEC.md 88.13.1.5), on MartyPC.

    python3 tests/skiesultra.py [--machine os8088_5150_cga_gla]
                                [--no-sync | --no-refill | --no-latch]

Neither shows in a byte count. The texture is a box of its own (Texture, F7 -
88.13.1.5.2) and the fields are Ultra's, and both are read off the picture:

ON CGA (the default machine) the SHADOW, where both are byte patterns:
  1. Texture off, at High: the ground rows are plain green, and no field;
  2. Texture on: the live ink table's GROUND row is the dotted one and the
     ground ROWS on the glass carry it - phase 1 and 3 rows are MOSTLY their
     brown dots, an object or a line crossing a row composing any byte at its
     edges - which is the half that can go quietly wrong: a row of the
     ground's kind is "as it was" and is never laid again (88.3.1), so a
     table swap without the rows marked unknown changes nothing on the glass.
     Still no field: the fields are Ultra's and not the box's;
  3. at Ultra fields lie on it: tiles drawn out of cs_gtobj, and RUNS of the
     field inks' bytes in the view, four alike (no edge composes that);
  4. and with the box off at High, all of it goes again.

ON VGA (--machine os8088_xt_vga) the RENDERED GLASS, because Mode X's
checker is written through the card's LATCHES (cs_mxlon) and is nowhere in
memory the package can read: with the box off no ground pixel is the second
green; on, half of them are, in all four row phases, and no row of the ground
is that green across (which is what a fill that lost the latches looks like);
and with the haze on the HAZE'S GROUND BAND is a checker of its own two
shades - the field: "we do need to also dither the ground haze band, or else
it stands out weirdly" - and plain again with the box off.

--no-sync NOPs cs_render's call to cs_grsync (CGA check 2 red); --no-refill
NOPs only the rows' marking, so the table reads right and the glass stays
plain (CGA check 2's second half red on its own); --no-latch makes cs_mxlon a
`ret`, so Mode X stores the marker bytes as colours (VGA red); --no-band puts
the haze band's row back to plain, and only the band's check goes red.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import dispapps                                             # noqa: E402
import skies                                                # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSB_MODEX, CSB_CGA, CSBL_HIGH, CSBL_ULTRA = 1, 2, 4, 5
CSI_GROUND, CSI_HZG1, CSI_GRALT, CSI_HZGALT = 1, 20, 23, 24
CSFL_ALL, CSFL_HAZE, CSFL_GTEX = 3, 4, 8
CSO_SIZE, CS_GTF = 20, 16
PLAIN = bytes([0x55] * 4)
DOTS = bytes([0x55, 0xD5, 0x55, 0x5D])
FIELDS = {0x5F, 0xF5, 0x66, 0x99}       # CSI_FIELD and CSI_FIELD2 on CGA
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--no-sync", action="store_true",
                    help="cs_render never calls cs_grsync")
    ap.add_argument("--no-refill", action="store_true",
                    help="cs_grsync swaps the table and marks no row")
    ap.add_argument("--no-latch", action="store_true",
                    help="cs_mxlon arms nothing (Mode X)")
    ap.add_argument("--no-band", action="store_true",
                    help="the haze band's latched row is its plain one again")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    mp = dispapps._map("skies")

    def off(n):
        return dispapps.bss_off("skies", n)

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        slot, seg, base = skies.open_game(m)
        lin = seg << 4

        def w(n):
            return int.from_bytes(m.readseg(seg, base + off(n), 2), "little")

        def byte(n):
            return m.readseg(seg, base + off(n), 1)[0]

        def poke(n, d):
            m.write(lin + base + off(n), d)

        def patch(at, data, why):
            m.pause()
            m.write(lin + at, data)
            m.run()
            print("  (%s: this run must fail)" % why)

        if a.no_sync:
            r0 = mp["cs_render"]
            code = m.readseg(seg, r0, 96)
            site = None
            for i in range(len(code) - 2):
                if code[i] == 0xE8 and (r0 + i + 3 + int.from_bytes(
                        code[i + 1:i + 3], "little")) & 0xFFFF == mp["cs_grsync"]:
                    site = r0 + i
                    break
            if site is None:
                sys.exit("skiesultra: no call to cs_grsync in cs_render")
            patch(site, b"\x90\x90\x90", "cs_grsync is never called")
        if a.no_refill:
            g0 = mp["cs_grsync"]
            at = m.readseg(seg, g0, 120).find(b"\xF3\xAA")  # its rep stosb
            if at < 0:
                sys.exit("skiesultra: no rep stosb in cs_grsync")
            patch(g0 + at, b"\x90\x90", "the rows are never marked unknown")
        if a.no_latch:
            patch(mp["cs_mxlon"], b"\xC3", "cs_mxlon arms no latch")
        if a.no_band:
            patch(mp["cs_mxhzp"] + 4, bytes([CSI_HZG1] * 4),
                  "the haze band is never latched")

        m.pause()
        poke("cs_inited", b"\x00")
        m.run()
        m.type_text("f")
        for _ in range(60):
            m.advance(frames=20)
            m.run()
            if byte("cs_back") and byte("cs_inited"):
                break
        m.advance(frames=10)
        m.run()
        back = byte("cs_back")
        check(back in (CSB_CGA, CSB_MODEX), "a backend whose ground the box "
              "changes: CGA's bytes or Mode X's latches (%d)" % back)
        if back not in (CSB_CGA, CSB_MODEX):
            return 1
        DEG = 65536 // 360

        def pin(px, py, pz, hdg, pitch):
            m.pause()
            for n, v in (("cs_px", px), ("cs_py", py), ("cs_pz", pz)):
                poke(n, ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_hdg", ((hdg * DEG) & 0xFFFF).to_bytes(2, "little"))
            poke("cs_pitch", ((pitch * DEG) & 0xFFFF).to_bytes(2, "little"))
            poke("cs_roll", b"\x00\x00")
            poke("cs_state", b"\x01")
            poke("cs_pause", b"\x01")           # ...and the world held still
            m.run()

        def frames(n, bld, fill):
            m.pause()
            poke("cs_setbld", bytes([bld]))
            poke("cs_setfill", bytes([fill]))
            m.run()
            m.bp_exec(lin + mp["cs_render"])
            for _ in range(n + 1):
                m.run()
                if m.wait_stop(30) is None:
                    sys.exit("skiesultra: cs_render never ran")
            m.bp_exec()
            m.pause()

        if back == CSB_MODEX:
            vga(m, seg, w, pin, frames)
        else:
            cga(m, seg, base, off, w, pin, frames)
        m.run()
        m.type_text("f")
        m.advance(frames=40)
        m.run()

    if bad:
        for b in bad:
            print("FAIL: " + b)
        return 1
    print("  ok")
    return 0


def cga(m, seg, base, off, w, pin, frames):
    # 1,500 m over the edge of tile (1, 0)'s field and looking nearly
    # straight down: (1 + 3 x 0) mod 8 is cs_gtmod's row 1, a field, so the
    # view is that field and the bare ground beside it
    pin(3500, 1500, 1024, 0, -80)
    wh, wb0, wbn = w("cs_wh"), w("cs_wb0"), w("cs_wbn")
    shseg = w("cs_shseg")

    def look():
        """The live ground row, the ground rows' MOSTLY byte by phase, the
        field bytes in the view and the tiles drawn"""
        tab = w("cs_inktab")
        row = m.readseg(seg, tab + CSI_GROUND * 4, 4)
        kinds = m.readseg(seg, base + off("cs_rowkind"), wh)
        sh = m.readseg(shseg, 0, 80 * wh)
        # the dominant byte and not the set: a building or a line crossing a
        # row composes any byte at all at its edges
        hist = [{}, {}, {}, {}]
        fields = 0
        for r in range(wh):
            b = sh[80 * r + wb0:80 * r + wb0 + wbn]
            if kinds[r] == CSI_GROUND:
                h = hist[r & 3]
                for x in b:
                    if x not in FIELDS:         # the ground, not a field
                        h[x] = h.get(x, 0) + 1
            # ...and a field is a RUN of its own byte: an edge never composes
            # four identical bytes in a row
            for i in range(len(b) - 3):
                if b[i] in FIELDS and b[i] == b[i + 1] == b[i + 2] == b[i + 3]:
                    fields += 1
        phase = [max(h, key=h.get) if sum(h.values()) > 200 else None
                 for h in hist]
        drawn = 0
        gto = base + off("cs_gtobj")
        for i in range(CS_GTF):
            o = m.readseg(seg, gto + i * CSO_SIZE + 16, 2)
            if int.from_bytes(o, "little") & 0x8000:        # CSO_SEEN
                drawn += 1
        m.run()
        return row, phase, fields, drawn

    frames(3, CSBL_HIGH, CSFL_ALL)
    row, ph, fl, _ = look()
    check(row == PLAIN and ph == [0x55] * 4,
          "Texture off: the ground is plain green (table %s, rows mostly %s)"
          % (row.hex(), ph))
    check(fl < 16, "...and no field is in the view at High (%d bytes)" % fl)
    frames(3, CSBL_HIGH, CSFL_ALL | CSFL_GTEX)
    row, ph, fl, _ = look()
    check(row == DOTS, "Texture on: the live ground row is the dotted one (%s)"
          % row.hex())
    check(ph == list(DOTS),
          "...and the ground ROWS on the glass carry it, phase by phase "
          "(mostly %s)" % ph)
    check(fl < 16, "...and still no field: those are Ultra's (%d bytes)" % fl)
    frames(3, CSBL_ULTRA, CSFL_ALL | CSFL_GTEX)
    row, ph, fl, drawn = look()
    check(drawn > 0 and fl > 1000,
          "at Ultra fields lie on it (%d tiles drawn, %d field bytes in the "
          "view)" % (drawn, fl))
    frames(3, CSBL_HIGH, CSFL_ALL)
    row, ph, fl, _ = look()
    check(row == PLAIN and ph == [0x55] * 4 and fl < 16,
          "and Texture off at High, all of it goes again (table %s, rows "
          "mostly %s, %d field bytes)" % (row.hex(), ph, fl))


def vga(m, seg, w, pin, frames):
    # level-ish over the tower, the ground filling the lower view
    pin(-640, 300, -640, 45, -8)
    dac = m.readseg(seg, dispapps._map("skies")["cs_dactab"], 3 * 25)

    def rgb(i):
        return tuple(round(c * 255 / 63) for c in dac[3 * i:3 * i + 3])
    g0, g1 = rgb(CSI_GROUND), rgb(CSI_GRALT)

    def near(p, q):
        return all(abs(x - y) <= 6 for x, y in zip(p, q))

    def look():
        wd, hd, d = m.fbuf(0)
        m.run()
        # the screen rows of the view's lower half, below the horizon and the
        # haze: a screen row is one Mode X row doubled, so take every other
        nb = n1 = 0
        lines = 0
        rows1 = set()
        for y in range(hd // 2, int(hd * 0.70), 2):
            a = b = 0
            for x in range(0, wd, 2):
                o = 3 * (y * wd + x)
                p = tuple(d[o:o + 3])
                if near(p, g0):
                    a += 1
                elif near(p, g1):
                    b += 1
            nb += a
            n1 += b
            if b:
                rows1.add((y // 2) & 3)
            if b > 3 * a and b > 20:            # a row the second green across
                lines += 1
        return nb, n1, lines, rows1

    frames(3, CSBL_HIGH, CSFL_ALL)
    nb, n1, lines, _ = look()
    check(nb > 2000 and n1 < nb // 100,
          "Texture off: the ground is the one green (%d px, %d of the second)"
          % (nb, n1))
    frames(3, CSBL_HIGH, CSFL_ALL | CSFL_GTEX)
    nb, n1, lines, rows1 = look()
    share = n1 / max(1, nb + n1)
    check(0.40 <= share <= 0.60,
          "Texture on: half the ground is the second green - the checker, "
          "written through the latches (%.3f of %d)" % (share, nb + n1))
    check(lines == 0, "...and no row of the ground is that green across, "
          "which is a fill that lost its latches (%d rows)" % lines)
    check(len(rows1) == 4, "...and the checker is in all four row phases "
          "(%s)" % sorted(rows1))

    def band():
        """The haze's ground band: its two shades' pixels over the view"""
        wd, hd, d = m.fbuf(0)
        m.run()
        h0, h1 = rgb(CSI_HZG1), rgb(CSI_HZGALT)
        a = b = 0
        for y in range(0, int(hd * 0.70), 2):
            for x in range(0, wd, 2):
                o = 3 * (y * wd + x)
                p = tuple(d[o:o + 3])
                if near(p, h0):
                    a += 1
                elif near(p, h1):
                    b += 1
        return a, b
    frames(3, CSBL_HIGH, CSFL_ALL | CSFL_GTEX | CSFL_HAZE)
    a0, a1 = band()
    share = a1 / max(1, a0 + a1)
    check(a0 + a1 > 150 and 0.35 <= share <= 0.65,
          "...and with the haze on, its GROUND BAND is a checker of its own "
          "two shades (%.3f of %d px)" % (share, a0 + a1))
    frames(3, CSBL_HIGH, CSFL_ALL | CSFL_HAZE)
    a0, a1 = band()
    check(a0 > 150 and a1 < a0 // 50,
          "...and plain with the box off (%d px, %d of the second shade)"
          % (a0, a1))
    frames(3, CSBL_HIGH, CSFL_ALL)
    nb, n1, lines, _ = look()
    check(n1 < nb // 100,
          "and Texture off, the one green again (%d of %d)" % (n1, nb))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
