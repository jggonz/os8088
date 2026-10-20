#!/usr/bin/env python3
"""AN EXACTLY HORIZONTAL SEGMENT LANDS WHERE IT WAS ASKED (SPEC.md 88.4.6.1).

    python3 tests/skiesflat.py [--clobber-flat]

`CS_SLICE` is the fast path for a near-horizontal line on the two backends
that pack pixels into bytes without planes - CGA and the 160x100 hack. It
splits the line into one horizontal RUN a row and hands each to the
backend's run proc, which is much cheaper than plotting pixel by pixel.

Its `%%flat` arm - dy = 0 exactly, one run of every pixel - jumped into the
row loop **without loading DX**, which is where that loop takes the run's
first x. DX arrived holding `3 x BP` from the caller's own slice test, and
for a flat line BP is 0: **every exactly-horizontal segment in the program
was drawn at x = 0**, the view's left edge.

It hid for the program's whole life because of what it takes to make one.
The angle has to be EXACT - a roll of one unit is enough to tilt it - so it
wants a model edge between two vertices of the same height, seen with the
wings level. The Eiffel Tower's platform bar (88.5.4.5) is the first such
edge in any world, and even then the ink went somewhere nothing MARKED, so
it never reached the glass: it sat in the shadow and showed up as a "stale
pixel" in `skiescga`'s dirty-row check, at the other end of the screen from
the tower.

So the row asks the direct question instead. Stood on the Issy runway
looking at the tower, the bar is a run of about nine pixels at the tower's
own x, on one row above the flare - and there is nothing at the left edge.

The reading is taken at a breakpoint on `cs_blit` - the copy to the device,
so the one instruction at which the shadow holds a WHOLE frame. A frame
count lands inside `cs_scene` instead, on a picture with the ground painted
and the tower not reached yet, and which half you get depends on the host
(docs/WRITING-TESTS.md 13 entry 44).

--clobber-flat is the red run (docs/WRITING-TESTS.md 1): it NOPs the four
bytes of the `mov dx, [cs_slx]` this section is about, in the CGA walk
alone, and the bar moves to x = 0.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88ui                                               # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEG = 65536.0 / 360.0
CSA_OBJS, CSA_NOBJ = 18, 20        # the location's object table
CSO_SKIP, CSO_SIZE = 18, 20        # ...and one object's deferral
CSO_FLAGS = 16                     # ...and its flags word
CSO_SEEN, CSO_BOXED = 0x8000, 0x2000
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def runs_of(row_bytes, least=6):
    """The runs of lit pixels in one 2bpp CGA row, as (first x, last x)."""
    lit = []
    for b, v in enumerate(row_bytes):
        for k in range(4):
            if (v >> (6 - 2 * k)) & 3:
                lit.append(b * 4 + k)
    out, cur = [], []
    for x in lit:
        if cur and x == cur[-1] + 1:
            cur.append(x)
        else:
            if len(cur) >= least:
                out.append((cur[0], cur[-1]))
            cur = [x]
    if len(cur) >= least:
        out.append((cur[0], cur[-1]))
    return out


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--clobber-flat", action="store_true",
                    help="the flat run forgets its x again: red")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    mp = dispapps._map("skies")

    def off(n):
        return dispapps.bss_off("skies", n)

    with os88ui.boot(a.image, apps=a.apps, machine=a.machine) as ui:
        m = ui.m
        ui.path("B:/GAMES/SKIES.O88")
        slot, seg = dispapps.pkg_seg(m, 0)
        lin = seg << 4
        base = int.from_bytes(m.readseg(seg, 8, 2), "little")

        def uw(n):
            return int.from_bytes(m.readseg(seg, base + off(n), 2), "little")

        def poke(n, d):
            m.write(lin + base + off(n), d)

        m.advance(frames=30)
        m.run()
        if a.clobber_flat:
            # `mov dx, [cs_slx]` = 8B 16 <off16>, in the CGA walk alone - the
            # 160x100 one is the same four bytes from the same macro, and
            # patching both would be patching a backend this row never enters.
            # The SLOPED arm loads the same word at %%go, so the anchor is the
            # flat arm's own preamble: mov ax, cx / mov word [cs_slcnt], 0
            lo, hi = mp["cs_line_cga_sh"], mp["cs_line_cga_st"]
            code = m.read(lin + lo, hi - lo)
            pat = (b"\x89\xC8\xC7\x06"
                   + (base + off("cs_slcnt")).to_bytes(2, "little")
                   + b"\x00\x00\x8B\x16"
                   + (base + off("cs_slx")).to_bytes(2, "little"))
            i = code.find(pat)
            if i < 0 or code.find(pat, i + 1) >= 0:
                sys.exit("skiesflat: the CGA walk does not load the flat run's "
                         "x the way this patch expects (%d matches)"
                         % code.count(pat))
            m.pause()
            m.write(lin + lo + i + 8, b"\x90\x90\x90\x90")
            m.run()
            print("  (the flat run forgets where x is: this run must fail)")

        m.type_text("f")
        m.advance(frames=150)
        m.run()
        check(uw("cs_back") != 0, "the bracket took a mode")

        # ON THE RUNWAY, looking at the tower. Pinned rather than flown: the
        # bar is horizontal only with the wings EXACTLY level, which is the
        # whole reason this defect needed a particular picture to show it.
        m.pause()
        poke("cs_pause", b"\x01")
        m.run()
        m.advance(frames=2)
        m.pause()
        for nm, v in (("cs_px", -2600), ("cs_py", 2), ("cs_pz", -2300)):
            m.write(lin + base + off(nm),
                    ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
        poke("cs_hdg", (int(49 * DEG) & 0xFFFF).to_bytes(2, "little"))
        poke("cs_pitch", b"\x00\x00")
        poke("cs_roll", b"\x00\x00")
        poke("cs_state", b"\x00")
        m.run()
        m.advance(frames=200)
        m.pause()

        # EVERY OBJECT LOOKED AT AGAIN, because poking the pose TELEPORTED
        # the aeroplane and 88.5.2 says a teleport clears the table: CSO_SKIP
        # is the tick the cull next looks at this object, and SEEN/BOXED are
        # what it decided about the place we are no longer at.
        ap = int.from_bytes(m.read(lin + base + off("cs_airport"), 2), "little")
        objs = int.from_bytes(m.read(lin + ap + CSA_OBJS, 2), "little")
        nobj = int.from_bytes(m.read(lin + ap + CSA_NOBJ, 2), "little")
        for o in range(objs, objs + nobj * CSO_SIZE, CSO_SIZE):
            m.write(lin + o + CSO_SKIP, b"\x00\x00")
            f = int.from_bytes(m.read(lin + o + CSO_FLAGS, 2), "little")
            m.write(lin + o + CSO_FLAGS,
                    (f & ~(CSO_SEEN | CSO_BOXED)).to_bytes(2, "little"))
        m.write(lin + mp["cs_rwobj"] + CSO_SKIP, b"\x00\x00")

        # ...AND STOP WHERE THE FRAME IS WHOLE. `advance` is exact in guest
        # time and lands at an arbitrary point INSIDE cs_scene, so the shadow
        # it stops on is half a picture: the ground painted and the tower not
        # reached yet. Which half depends on the free-run phase, which depends
        # on the HOST - so this row passed alone and failed three-way
        # concurrent, on the same bytes and the same pose. cs_blit is the
        # copy to the device, so its entry is the one instruction at which
        # the shadow is a complete frame.
        m.bp_exec(lin + mp["cs_blit"])
        m.run()
        for _ in range(4):                   # a few whole frames after the
            if m.wait_stop(60) is None:      # clear, so the cull has looked
                sys.exit("skiesflat: the guest never reached cs_blit")
            m.run()
        if m.wait_stop(60) is None:
            sys.exit("skiesflat: the guest never reached cs_blit")
        m.bp_exec()

        got = [(int.from_bytes(m.readseg(seg, base + off("cs_roll"), 2),
                               "little"))]
        check(got[0] == 0, "the wings are EXACTLY level, which is what makes "
                           "the bar horizontal at all (roll %d)" % got[0])

        # THE SHADOW and not the glass: the misdrawn run lands where nothing
        # marked, so it never reaches the device at all - which is how this
        # spent a whole commit being read as a dirty-row defect (88.4.6.1)
        sh = uw("cs_shseg")
        wh = uw("cs_wh")
        print("      shadow %04x, view %dx%d, pose %d,%d,%d hdg %d" % (
            sh, uw("cs_ww"), wh,
            int.from_bytes(m.readseg(seg, base + off("cs_px") + 1, 2), "little", signed=True),
            int.from_bytes(m.readseg(seg, base + off("cs_py") + 1, 2), "little", signed=True),
            int.from_bytes(m.readseg(seg, base + off("cs_pz") + 1, 2), "little", signed=True),
            uw("cs_hdg")))
        found, left = [], []
        for row in range(20, 54):            # above the flare, below the apex
            for r in runs_of(m.read((sh << 4) + row * 80, 80)):
                (found if r[0] > 100 else left).append((row, r))
        print("      runs above the flare: at the tower %s, at the left %s"
              % (found[:3], left[:3]))
        if not found:
            # A red run must say WHERE the ink went, or the next reader is
            # back to guessing between "drawn at x = 0" and "not drawn"
            for row in range(wh):
                r = runs_of(m.read((sh << 4) + row * 80, 80), 3)
                if r:
                    print("        census row %d %s" % (row, r))
        check(bool(found),
              "the platform bar is drawn AT THE TOWER (%s)"
              % (found[:2] if found else "nothing over x=100"))
        check(not left,
              "...and nothing is drawn at the view's left edge (%s)"
              % (left[:2] if left else "clean"))
        if found:
            w = found[0][1][1] - found[0][1][0] + 1
            check(w >= 6,
                  "...and it is the bar's own width, not a stray pixel (%d)" % w)
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


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
