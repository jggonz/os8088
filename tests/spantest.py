#!/usr/bin/env python3
"""DOES gfx_spans DRAW WHAT A FILL A ROW DRAWS? (SPEC.md 5.10)

    make && make spantest && python3 tests/spantest.py [--machine ...]

SPEC.md 5.10.3 says a REFUSED gfx_spans is answered by one OSAPI_GFX_FILL per
span out of the same list, "which draws the identical pixels". That sentence
is the whole assertion here, and it is a good one to hang a gate on: the
fallback loop is not a second opinion about the shape, it is the SAME span
list through machinery that predates the primitive by years.

**WHY THIS EXISTS AND apps/paint DOES NOT COVER IT.** tests/paintundo.py is a
strong gate on the primitive - its redo compares a span-drawn screen against a
canvas the untouched walk wrote - but it only asks for the shapes a brush chord
makes. Four things in 5.10's contract had no test at all: an EMPTY row
(x1 > x2), the vertical clip (a run starting above the screen or ending past
the bottom), a middle grey's per-row dither alternation (SPEC.md 39.4), and
the refusal itself.

Nine cases, each drawn twice over an identically erased ground, compared as a
region hash:

    0 a swept parallelogram - Paint's own shape
    1 x1 and x2 CONVERGING, so the last rows are empty
    2 wide enough for a rep stosb interior
    3 one pixel wide - both edge masks inside one byte, the merge path
    4 off both side edges at once
    5 starting ABOVE the screen: leading records dropped
    6 running off the BOTTOM: the run truncated
    7 a middle grey, which dithers by row parity on a 1bpp adapter
    8 one whole byte, byte-aligned - lmask and rmask both 0xFF

...then the REFUSAL, by poking [wm_clip_n] non-zero across one call and
asserting CF=1 with nothing drawn. That one is worth the poke: a refusal
trigger that fired too rarely would let the primitive draw through an armed
clip region, which is a corrupted repaint and not a slow one.
"""
import argparse
import hashlib
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88fixture                                       # noqa: E402
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(HERE)
S = os88sym.linear
NCASE = 9
NAMES = ["a swept parallelogram", "converging -> empty rows",
         "wide: a rep stosb interior", "one pixel wide, masks merged",
         "off both side edges", "starting above the screen",
         "running off the bottom", "a middle grey (dither)",
         "one whole byte, aligned"]
_MAP = {}


def moff(name):
    """Every equate in the package, from nasm's own [map]."""
    if not _MAP:
        src = os.path.join(ROOT, "tests", "spantest", "spantest.asm")
        tmp, mp = "/tmp/os88_spantest_map.asm", "/tmp/os88_spantest.map"
        open(tmp, "w").write(open(src).read() + "\n[map all %s]\n" % mp)
        subprocess.check_call(["nasm", "-f", "bin", "-I",
                               os.path.join(ROOT, "apps") + os.sep,
                               "-o", "/dev/null", tmp])
        for ln in open(mp):                 # "<vaddr> <raddr> <name>", HEX
            f = ln.split()
            if len(f) == 3:
                try:
                    _MAP[f[2]] = int(f[0], 16)
                except ValueError:
                    pass
    if name not in _MAP:
        sys.exit("spantest: no symbol %s" % name)
    return _MAP[name]


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/spantest.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    if a.apps == "build/spantest.img":
        # On demand, like fmtest: nothing in `all` builds a package that ships

        # on no disk. os88fixture.need is the same `make`, and it does NOTHING
        # when the runner has
        # already built the artefact (Row(wants=...) and os88test's prebuild).
        # That is what lets this row drop builds=True and share the lane.
        os88fixture.need(a.apps)
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        os88marty.settle(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        rows = [r[0] for r in dispcp.listing(m, S)]
        row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy,
                               rows.index("SPANTEST.O88"))
        x, y = dispcp.row_xy(wx, wy, row)
        mo.dblclick(x, y)
        m.advance(frames=250)
        m.run()
        got = dispapps.pkg_seg(m, 0)
        if got is None:
            sys.exit("spantest: SPANTEST.O88 did not open")
        seg = got[1]
        base = seg << 4

        def rd(name, n=2):
            return int.from_bytes(m.read(base + moff(name), n), "little")

        def wr(name, v):
            m.write(base + moff(name), bytes([v]))

        def draw(case, mode):
            """One case, one way. Returns (hash, ink, box, refused)."""
            was = rd("st_done", 1)
            wr("st_case", case)
            wr("st_mode", mode)
            m.advance(frames=4)
            m.run()
            m.key("KeyG")
            for _ in range(60):
                m.advance(frames=6)
                m.run()
                if rd("st_done", 1) != was:
                    break
            else:
                sys.exit("spantest: case %d mode %d never ran" % (case, mode))
            box = (rd("st_bx1"), rd("st_by1"), rd("st_bx2"), rd("st_by2"))
            box = tuple(v - 65536 if v > 32767 else v for v in box)
            fw, fh, fb = m.fbuf(card=0)
            # THE FULL RGB TRIPLE, not a black/white threshold. The ground is
            # CWHITE and case 7's ink is CLGRAY, which on a 1bpp adapter is
            # SPEC.md 39.4's dither and on VGA is a light grey - lighter than
            # any threshold that calls the other cases' black "ink". Thresholded,
            # that case read as zero pixels on VGA and tripped the "this case
            # proves nothing" guard below on a build that was correct. It also
            # makes the hash sensitive to the wrong COLOUR, which one bit is not.
            out, ink = bytearray(), 0
            for yy in range(max(0, box[1]), min(box[3] + 1, fh)):
                for xx in range(max(0, box[0]), min(box[2] + 1, fw)):
                    o = (yy * fw + xx) * 3
                    px = fb[o:o + 3]
                    out += px
                    if min(px) < 250:           # anything but the erased ground
                        ink += 1
            return (hashlib.sha256(bytes(out)).hexdigest()[:16], ink,
                    box, rd("st_cf", 1))

        print()
        bad = []
        for c in range(NCASE):
            hs, ns, box, cf = draw(c, 0)
            hf, nf, _, _ = draw(c, 1)
            ok = (hs == hf) and not cf
            note = "" if ok else ("   <-- REFUSED" if cf else "   <-- DIFFER")
            print("   %d %-30s spans %s %5d | fills %s %5d%s"
                  % (c, NAMES[c], hs, ns, hf, nf, note))
            if not ok:
                bad.append(c)
            if ns == 0 and nf == 0 and c != 1:
                print("       (both drew NOTHING - this case proves nothing)")
                bad.append(c)

        # --- the refusal. A region is armed by hand for one call and put
        # back. ONE 1x1 RECT AT THE ORIGIN, not just a count: everything the
        # package draws in that window goes through GFXCLIP too, its erase
        # included, so a bogus table would decide by luck what survives. With
        # a real rect far from the case's box, a correct kernel leaves that
        # box EXACTLY as it was - which is the assertion, rather than an ink
        # count that the erase could have produced.
        clipn, tab = S("wm_clip_n"), S("wm_clip_tab")
        keepn, keept = m.read(clipn, 2), m.read(tab, 8)
        before = draw(0, 1)                     # a known ground, unpoked
        m.write(tab, b"".join(v.to_bytes(2, "little") for v in (0, 0, 0, 0)))
        m.write(clipn, (1).to_bytes(2, "little"))
        hs, ns, _, cf = draw(0, 0)
        m.write(clipn, keepn)
        m.write(tab, keept)
        refok = bool(cf) and hs == before[0]
        print("\n   an armed clip region: CF=%d, the box %s%s"
              % (cf, "unchanged" if hs == before[0] else "WAS DRAWN INTO",
                 "" if refok else "   <-- SHOULD REFUSE, DRAWING NOTHING"))

        m.advance(frames=10)
        m.run()

    print()
    if bad or not refok:
        print("spantest: FAIL - %s%s"
              % ("cases %s do not match a GFX_FILL a row" % sorted(set(bad))
                 if bad else "",
                 "" if refok else
                 ("; " if bad else "") + "the refusal did not fire"))
        return 1
    print("spantest: PASS - %d cases, gfx_spans and a GFX_FILL a row draw the\n"
          "  same pixels out of the same list, and an armed clip region "
          "refuses" % NCASE)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
