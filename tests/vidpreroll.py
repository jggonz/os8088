#!/usr/bin/env python3
"""A ONE-BIT play from the start begins at KEY 0 when the encoder held a
PRE-ROLL - SPEC.md 98.2.9 and 98.3.5.

    make && python3 tests/vidpreroll.py [--machine NAME]

First, on the host: the ENCODER'S PREVIEW (tools/os88vencgui.py's
preview_frames, through os88vid.first_shown) must start at the same key 0
with the picture whole - it is the window's promise of what a play shows.

The owner's Hercules encode of a clip whose first frame the budgets could not
pay for in one period opened on that frame PAINTED IN: the encoder holds the
first picture over silence and starts the keyframe grid at the first whole
frame (98.2.9), and a colour play starts at that key - but a one-bit play
started at the stream's first record and showed the paint. Two clips on one
scratch floppy, played full screen on MartyPC's Hercules 5150:

1. PRE.V88: noise over every pixel, the tight profile tests/videnc.py's leg
   3 uses, so frame 0 is cut and key 0 is frames in. A play from the start
   must be BASED at key 0's frame + 1, from the key's record, and the hold
   before its first streamed frame must show key 0's picture - the source's
   first frame whole. Then it plays to the end.
2. PLAIN.V88: tests/vidplay.py's clip, whose key 0 IS frame 0 (and whose
   poster is key 1, so the start had to read key 0's entry to know). Its
   play still starts at the first record: based at 0, the hold before frame
   1 showing frame 0.

Broken on purpose (vp_spos's one-bit test taken out, so a one-bit play from
the start goes to the first record again) leg 1 FAILS: based at 0, no key.
With os88vid.first_shown back on colour alone the preview FAILS: it starts
at frame 0 with 3,703 bytes of the picture missing.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88build, os88vid as vid, os88venc as venc  # noqa
import os88geom as geom                                       # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402
import vidplay                                                # noqa: E402

TIGHT = ("--preset", "herc", "--disk", "20000", "--avg", "0.30", "--peak",
         "0.50", "--owe", "0", "--rate", "5512", "--profile", "floppy",
         "--reserve", "16")


class Stop(Exception):
    """a wait that never came true: reported as a FAIL, not a traceback"""


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default=vidplay.MACHINE["herc"])
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",), ())
    pkg = os88build.at("build/video.o88")
    if not os.path.exists(pkg):
        sys.exit("vidpreroll: no build/video.o88 - run `make`")
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        src = os.path.join(tmp, "noise.mkv")
        subprocess.run(
            ["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
             "testsrc2=size=320x200:rate=30:duration=2,noise=alls=100",
             "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=22050",
             "-t", "2", "-c:v", "ffv1", "-c:a", "pcm_s16le", src],
            check=True)
        pre = os.path.join(tmp, "PRE.V88")
        keep = []
        venc.encode(venc.parser().parse_args(
            [src, pre, "--quiet"] + list(TIGHT)), keep)
        vid.verify_v88(pre)
        rp = vid.Reader(pre)
        k0 = rp.keys[0][0]
        whole = vid.decode_at(rp, k0)
        print("   PRE.V88: %d frames on %s, key 0 at frame %d, %s the "
              "first picture; frame 0 %d bytes short of it"
              % (rp.frames, rp.g.name, k0,
                 "exactly" if whole == keep[0].tobytes() else "NOT",
                 sum(1 for x, y in zip(vid.decode_at(rp, 0), whole)
                     if x != y)))
        if not k0 or whole != keep[0].tobytes() or rp.g.name != "herc":
            sys.exit("vidpreroll: the clip has no pre-roll - it tests "
                     "nothing")
        # THE ENCODER'S PREVIEW shows what the player does (98.2.9.3):
        # its first frame key 0's, and the picture whole - on the host
        import os88vencgui as G
        pr, pfr = G.preview_frames(pre)
        pf0 = pfr[0][0]
        surf0 = None
        for f, surf, rec, at, i in vid.v88_frames(vid.Reader(pre)):
            if f == pf0:
                surf0 = bytes(surf)
                break
        pdiff = sum(1 for x, y in zip(rp.g.canvas(surf0), whole) if x != y)
        print("   the encoder's preview starts at frame %d (want %d), %d "
              "bytes off the first picture" % (pf0, k0, pdiff))
        if pf0 != k0 or pdiff:
            bad.append("the encoder's preview starts at frame %d, %d bytes "
                       "off the first picture - not key 0's" % (pf0, pdiff))
        plain = os.path.join(tmp, "PLAIN.V88")
        os.rename(vidplay.clip(tmp, "herc"), plain)
        rq = vid.Reader(plain)
        if rq.keys[0][0] != 0 or rq.poster != 1:
            sys.exit("vidpreroll: PLAIN.V88 is not the clip this row was "
                     "written against")
        disk = os.path.join(tmp, "vidpre.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, pre, plain], check=True,
                       capture_output=True)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=a.machine, quick=True) as ui:
            m = ui.m

            def leg(name, r, base0):
                g = r.g
                tg = vid.Geom(vid.LAYOUT_BY_NAME["herc"], g.wb,
                              vidplay.ROWS["herc"])
                ty0 = (vidplay.ROWS["herc"] - g.h) // 2 // tg.banks * tg.banks
                tx0 = (tg.stride - g.wb) // 2
                rows_at = [tg.base[y + ty0] + tx0 for y in range(g.h)]
                w = ui.path("B:/" + name)
                rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                             geom.WIN_SIZE)
                base = u16(rec, geom.W_SEG) << 4

                def rw(n):
                    return u16(m.read(base + syms[n], 2))

                def rb(n):
                    return m.read(base + syms[n], 1)[0]

                def wait(cond, what, guest=120.0):
                    try:
                        os88marty.until(m, cond, what, poll=0.05,
                                        limit=600.0, guest=guest)
                    except os88marty.MartyError as e:
                        raise Stop("%s: %s never happened (%s)" % (
                            name, what, str(e).split(".")[0]))

                wait(lambda mm: rw("vp_ploads") >= 1, "the poster")
                m.write(base + syms["vp_nowin"], b"\1")
                if rb("vp_ok") != 1 or rw("vp_sel") != 0:
                    sys.exit("vidpreroll: %s will not play from the start "
                             "here" % name)
                stop = base0 + 1
                m.write(base + syms["vp_stopat"], struct.pack("<H", stop))
                m.write(base + syms["vp_played"], b"\0")
                m.type_text("p")
                wait(lambda mm: rb("vp_held") == 1, "the first hold")
                done, pbase, krec = rw("vp_done"), rw("vp_base"), \
                    rw("vp_krec")
                seg = bytes(m.read(vidplay.VSEG["herc"],
                                   vidplay.VSIZE["herc"]))
                got = b"".join(seg[b:b + g.wb] for b in rows_at)
                want = vid.decode_at(r, base0)
                diff = sum(1 for x, y in zip(got, want) if x != y)
                print("   %s: based at %d (want %d), %s; held before frame "
                      "%d, %d bytes of %d differ from frame %d"
                      % (name, pbase, base0 + (1 if base0 else 0),
                         "from key 0's record" if krec != 0xFFFF else
                         "from the first record", done, diff, len(got),
                         base0))
                wantb = base0 + 1 if base0 else 0
                if pbase != wantb or (krec != 0xFFFF) != bool(base0):
                    bad.append("%s: the play from the start is based at %d "
                               "%s, not %d %s" % (
                                   name, pbase, "from a key" if krec !=
                                   0xFFFF else "from the first record",
                                   wantb, "from key 0" if base0 else
                                   "from the first record"))
                if done != stop or diff:
                    bad.append("%s: the first hold was before frame %d with "
                               "%d bytes off frame %d's picture"
                               % (name, done, diff, base0))
                m.write(base + syms["vp_stopat"], b"\xff\xff")
                m.write(base + syms["vp_held"], b"\0")
                wait(lambda mm: rb("vp_played") == 1, "the play to end")
                if rw("vp_done") != r.frames or rb("vp_err"):
                    bad.append("%s: the play ended at %d of %d, error %d"
                               % (name, rw("vp_done"), r.frames,
                                  rb("vp_err")))

            try:
                leg("PRE.V88", rp, k0)
                leg("PLAIN.V88", rq, 0)
            except Stop as e:
                bad.append(str(e))
    if bad:
        for b in bad:
            print("FAIL: " + b)
        sys.exit(1)
    print("vidpreroll: ok")


if __name__ == "__main__":
    main()
