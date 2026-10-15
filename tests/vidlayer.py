#!/usr/bin/env python3
"""THE LAYER (SPEC.md 98.1.9), host-side: a file made for a slow disk with a
second stream on top of it for a faster one.

    python3 tests/vidlayer.py

ffmpeg makes six seconds of a Mandelbrot zoom - every pixel moving every
frame, so a 60 KB/s disk cuts most of them - and each arm encodes it twice for the 286-fast profile
at that disk: once plain, once with `--layer-disk` for 240 KB/s. What it
asserts, per arm:

  - THE BASE IS UNTOUCHED: the layered file's frame records and keyframe
    records are the plain one's byte for byte (only the options block,
    which records the layer's options, and the header's layer words may
    differ). Every player made before plays exactly what it played;
  - verify_v88 passes it, which walks the layer by the writer's rules
    (verify_layer: a record a frame, each super-packet naming its first
    frame, the key table naming each key's next frame);
  - THE DECODE IS THE MODEL: base then layer, frame by frame, ends on the
    exact screen the encoder's layer model ended on - and with ONE layer
    record left out - the last, which nothing after it writes over - it
    does not (the negative control: the check bites);
  - the layer buys something: records in most frames (or fewer, where it
    takes the error under a fifth of the base's), and the enhanced play's
    error as seen below the base's.

Arms: vga8 (LIN320, one plane) and modex (four planes, sub-records), both
for the 286 at 60 KB/s; and xt286, a CGA file for a 5150 at 20 KB/s whose
layer is for the 286 (--layer-profile 286-fast): the layer's machine is
ANOTHER CPU, pricing the base's records at its own speed; and bank486,
BANKED AND LAYERED - a base for the 286 with a 512 KB bank prefilled, its
layer for the 486 - where the yardstick (key 0's decode modelled on each,
SPEC.md 98.1.9) must say the 486 is the faster.

--scenes is A PALETTE A SCENE WITH A LAYER (SPEC.md 98.1.3.2.3.1) alone: a
clip of three shots (a Mandelbrot, testsrc2, Life) encoded --pixfmt vga4
--palette colour16 --scene-palettes for the 286 at 60 KB/s, plain and with
a layer for 240. Over the four assertions above it asks that the layered
file's palette table is the plain one's, and that THE LAYER TOOK UP EACH
SCENE'S SIXTEEN AT THE TABLE'S FRAME - broken on purpose (LayerEnc.push
handed no palette, which is how it shipped) the layer's switches are [] and
it FAILS; and no frame of either encoder begins with a cached target or
screen made through another scene's palette - red with set_pal's cache
reset taken out, at the first frames of every scene (98.1.3.2.3.2). What
being in step BUYS is not asserted, being small: measured on
a six-second clip, each frame's error in its own colours 3.19% in step
against 3.22% in scene 0's - the layer mostly picks the same pixels either
way (SPEC.md 98.1.3.2.3.1).
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))
import os88venc as venc                                       # noqa: E402
import os88vid as vid                                         # noqa: E402

SKIP = 77
BASE_DISK, LAYER_DISK = 61440, 245760


def scenes(bad):
    """--scenes: a palette a scene with a layer (98.1.3.2.3.1)"""
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "scn.mkv")
        # (Life SEEDED: unseeded, ffmpeg starts it from a random board, so
        # every run encoded a different third shot - and whether a scene's
        # first frames had been weighed ahead, which is what the cache
        # check needs, came and went with it)
        subprocess.run(
            ["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
             "mandelbrot=size=320x240:rate=12,trim=duration=1.5",
             "-f", "lavfi", "-i", "testsrc2=size=320x240:rate=12,"
             "trim=duration=1.5", "-f", "lavfi", "-i",
             "life=size=320x240:rate=12:mold=10:seed=88:life_color=red:"
             "death_color=blue,trim=duration=1.5", "-filter_complex",
             "[0][1][2]concat=n=3:v=1:a=0,format=yuv420p", "-c:v", "ffv1",
             src], check=True)
        bopt = ["--preset", "vga4", "--palette", "colour16",
                "--scene-palettes", "--profile", "286-fast", "--fps", "12",
                "--disk", str(BASE_DISK)]
        lopt = ["--layer-disk", str(LAYER_DISK)]

        def run(name, args):
            a = venc.parser().parse_args(
                [src, os.path.join(tmp, name + ".V88"), "--quiet",
                 "--audio", "none", "--page-ahead", "off"] + args)
            return a.out, venc.encode(a)

        # THE CACHES IN THE SCENE'S COLOURS (98.1.3.2.3.2): before every
        # frame of every encoder - the base's and the layer's - each cached
        # target's pixels and the screen's must be the ones its palette
        # makes NOW, or that frame is weighed in another scene's colours
        np = venc.np
        stale = []
        wrapped = {}
        for cls in (c for c in vars(venc).values() if isinstance(c, type)
                    and "frame" in vars(c) and hasattr(c, "tpix")):
            def frame(self, target, *x, _o=cls.frame, **k):
                if self.dmode in ("pal", "idx") and self.dpal is not None:
                    n = sum(1 for c in self.tcache.values()
                            if not np.array_equal(c[1], self.pixels(c[0])))
                    sc = self.scache
                    if sc is not None and np.array_equal(sc[0], self.screen) \
                            and not np.array_equal(sc[1],
                                                   self.pixels(self.screen)):
                        n += 1
                    if n:
                        stale.append(n)
                return _o(self, target, *x, **k)
            wrapped[cls] = cls.frame
            cls.frame = frame

        print("\n== arm scenes")
        try:
            plain, rp = run("scnp", bopt)
            lay, rl = run("scnl", bopt + lopt)
        finally:
            for cls, f in wrapped.items():
                cls.frame = f
        print("   frames weighed with a cache in another scene's colours: %d"
              % len(stale))
        if stale:
            bad.append("scenes: %d frames began with %d cached pixels made "
                       "in another palette" % (len(stale), sum(stale)))
        ra, rb = vid.Reader(plain), vid.Reader(lay)
        same = [x[0] for x in ra.records()] == \
            [x[0] for x in rb.records()] and \
            [ra.key(i)[:2] for i in range(ra.nkeys)] == \
            [rb.key(i)[:2] for i in range(rb.nkeys)] and ra.pev == rb.pev
        print("   the base and its palette table untouched: %s" % same)
        if not same:
            bad.append("scenes: the layered file's base or table differs "
                       "from the plain encode")
        vid.verify_v88(lay)
        L = rl["layer"]
        tab = [f for f, _ in rb.pev]
        print("   palettes at %s; the layer took them up at %s; scene "
              "changes it left cut: %s" % (tab, L["pevs"], L["pev_cut"]))
        if len(tab) < 3:
            bad.append("scenes: %d palettes for three shots" % len(tab))
        if L["pevs"] != tab:
            bad.append("scenes: the layer's palettes at %s, the table's at "
                       "%s" % (L["pevs"], tab))
        last = None
        for f, surf in vid.v88_layered(rb):
            last = rb.g.canvas(surf)
        print("   the decode ends on the model's screen: %s"
              % (last == L["final"]))
        if last != L["final"]:
            bad.append("scenes: base then layer does not end on the "
                       "encoder's screen")
        print("   error as seen %.2f%% -> %.2f%% with the layer"
              % (100 * rl["q_vis"], 100 * L["q_vis"]))
        if not L["q_vis"] < rl["q_vis"]:
            bad.append("scenes: the layered play is no better")


def main():
    if venc.np is None or not shutil.which("ffmpeg"):
        print("   SKIP: needs ffmpeg and numpy")
        return SKIP
    bad = []
    if "--scenes" in sys.argv[1:]:
        scenes(bad)
        print("\nvidlayer: %s" % ("FAIL" if bad else "ok"))
        for x in bad:
            print("  - " + x)
        return 1 if bad else 0
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "src.mkv")
        subprocess.run(
            ["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
             "mandelbrot=size=320x180:rate=24,trim=duration=6", "-c:v", "ffv1",
             src], check=True)

        # (page486's own: three scenes in motion with hard cuts between,
        # so the 60 KB/s disk cuts each scene change and page-ahead builds
        # it - the zoom above has no cut to build, and stills leave the
        # disk idle and cut nothing)
        cuts = os.path.join(tmp, "cuts.mkv")
        subprocess.run(
            ["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
             "testsrc2=size=320x180:rate=24:duration=1.5", "-f", "lavfi",
             "-i", "mandelbrot=size=320x180:rate=24,trim=duration=1.5",
             "-f", "lavfi", "-i", "life=size=320x180:rate=24:mold=10:"
             "ratio=0.3:seed=7,trim=duration=1.5", "-filter_complex",
             "[0:v]format=yuv420p[a];[1:v]format=yuv420p[b];[2:v]format="
             "yuv420p[c];[a][b][c]concat=n=3:v=1:a=0[v]", "-map", "[v]",
             "-c:v", "ffv1", cuts], check=True)
        srcs = {"page486": cuts}

        def run(name, *args):
            a = venc.parser().parse_args(
                [srcs.get(name[:-1], src), os.path.join(tmp, name + ".V88"),
                 "--quiet", "--audio", "none", "--page-ahead", "off"] +
                list(args))
            return a.out, venc.encode(a)    # (the plain base against the
                                            # layered one: no second page)

        # (arm, the base's options, the layer's)
        arms = (("vga8", ["--preset", "vga8", "--profile", "286-fast",
                          "--disk", str(BASE_DISK)],
                 ["--layer-disk", str(LAYER_DISK)]),
                ("modex", ["--preset", "modex", "--profile", "286-fast",
                           "--disk", str(BASE_DISK)],
                 ["--layer-disk", str(LAYER_DISK)]),
                # A FILE FOR A 5150, ITS LAYER FOR A 286 (--layer-profile):
                # the base's records repriced on the 286's CPU
                ("xt286", ["--preset", "cga", "--profile", "5150-st225",
                           "--disk", "20480"],
                 ["--layer-profile", "286-fast", "--layer-disk",
                  str(LAYER_DISK)]),
                # BANKED AND LAYERED: the base for a 286 with an XMS bank,
                # its layer for a 486 - which the player tells apart by the
                # yardstick (key 0's decode on each), read off the header
                ("bank486", ["--preset", "modex", "--profile", "286-fast",
                             "--disk", str(BASE_DISK), "--bank", "512",
                             "--prefill", "all"],
                 ["--layer-profile", "486-dx2-66", "--layer-disk",
                  str(LAYER_DISK)]),
                # PAGE-AHEAD AND A LAYER (98.2.1.8.3): the base built ahead
                # on two pages, the layer for the 486 drawn on the page
                # shown - whose model must keep both pages, or the decode
                # ends elsewhere
                ("page486", ["--preset", "modex", "--profile", "286-fast",
                             "--disk", str(BASE_DISK), "--page-ahead",
                             "on"],
                 ["--layer-profile", "486-dx2-66", "--layer-disk",
                  str(LAYER_DISK)]))
        for arm, bopt, lopt in arms:
            print("\n== arm %s" % arm)
            plain, rp = run(arm + "p", *bopt)
            lay, rl = run(arm + "l", *(bopt + lopt))
            ra, rb = vid.Reader(plain), vid.Reader(lay)
            same = [x[0] for x in ra.records()] == \
                [x[0] for x in rb.records()] and \
                [ra.key(i)[:2] for i in range(ra.nkeys)] == \
                [rb.key(i)[:2] for i in range(rb.nkeys)]
            print("   the base: %d bytes, the layered file %d; its records "
                  "and keys untouched: %s"
                  % (len(ra.d), len(rb.d), same))
            if not same:
                bad.append("%s: the layered file's base differs from the "
                           "plain encode" % arm)
            vid.verify_v88(lay)
            r = vid.Reader(lay)
            print("   the yardstick: key 0 in %.2f ms on the layer's machine, "
                  "%.2f on the base's" % (r.ltl / 100.0, r.ltb / 100.0))
            if not r.ltl or not r.ltb or \
                    (arm in ("xt286", "bank486", "page486")) != \
                    (r.ltl < r.ltb * 0.8):
                bad.append("%s: the yardstick %d / %d does not tell the two "
                           "machines apart as it should" % (arm, r.ltl, r.ltb))
            if arm == "page486":
                fl = (rl.get("page") or {}).get("flips", 0)
                print("   page-ahead: %s, %d flips" % (r.page, fl))
                if not r.page or not fl:
                    bad.append("page486: the layered file is not page-ahead "
                               "with a flip (%s, %d)" % (r.page, fl))
            if "--layer-profile" in lopt and "--layer-disk" in lopt:
                # THE DISK TEST'S IDLE FIGURE (98.1.9.6) is the disk GIVEN,
                # on the layer profile's curve scaled to it: a 486 told its
                # disk is 240 KB/s idles near that, not at 1.89 times it
                lp = venc.PROFILES[lopt[lopt.index("--layer-profile") + 1]]
                ld = float(lopt[lopt.index("--layer-disk") + 1])
                r0 = lp["disk_at"][0][1]        # (its idle ratio, scaled
                want = max(1, int(round(ld * (1.0 + (r0 - 1.0) * min(  # by
                    1.0, ld / lp["disk"])) / 1024.0)))  # the disk given)
                raw = int(round(ld * lp["disk_at"][0][1] / 1024.0))
                print("   the disk test's idle: %d KB/s (the profile's own "
                      "curve would say %d)" % (r.lyidl, raw))
                if r.lyidl != want:
                    bad.append("%s: the layer's idle disk %d KB/s, not %d - "
                               "the profile's curve unscaled to the disk "
                               "given" % (arm, r.lyidl, want))
            if arm == "bank486" and (r.xbank, r.xpre) != (512, 0xFFFF):
                bad.append("bank486: the base's bank %d KB, prefill %x"
                           % (r.xbank, r.xpre))
            L = rl["layer"]
            print("   layer: %.1f KB/s, %d of %d frames; error as seen "
                  "%.2f%% -> %.2f%%"
                  % (L["kbs"], L["recs"], r.frames, 100 * rl["q_vis"],
                     100 * L["q_vis"]))
            if L["recs"] < r.frames // 2 and \
                    L["q_vis"] > rl["q_vis"] / 5:
                bad.append("%s: the layer has records in %d of %d frames"
                           % (arm, L["recs"], r.frames))
            if not L["q_vis"] < rl["q_vis"]:
                bad.append("%s: the layered play %.4f is no better than the "
                           "base's %.4f" % (arm, L["q_vis"], rl["q_vis"]))
            last = None
            for f, surf in vid.v88_layered(r):
                last = r.g.canvas(surf)
            print("   the decode ends on the model's screen: %s"
                  % (last == L["final"]))
            # (FRAME BY FRAME: a page-ahead file's flips are mid-clip, and
            # its last scene converges whichever page the model kept)
            import zlib
            off = [f for f, surf in vid.v88_layered(r)
                   if zlib.crc32(bytes(r.g.canvas(surf))) != L["sums"][f]]
            print("   ...and every frame on the model's: %d of %d differ%s"
                  % (len(off), r.frames, " (%s...)" % off[:6] if off else ""))
            if off:
                bad.append("%s: base then layer leaves the model's screen at "
                           "frames %s" % (arm, off[:6]))
            if last != L["final"]:
                bad.append("%s: base then layer does not end on the "
                           "encoder's screen" % arm)
            drop = max(f for f, rec, *_ in r.layer_records()
                       if rec is not None)  # (the last: nothing after it
                                            # writes over what it wrote)
            for f, surf in vid.v88_layered(r, drop=lambda f: f == drop):
                last = r.g.canvas(surf)
            print("   ...with frame %d's layer record left out: %s"
                  % (drop, last == L["final"]))
            if last == L["final"]:
                bad.append("%s: leaving a layer record out changed nothing "
                           "- the decode check cannot see a layer" % arm)
    if bad:
        print("\nvidlayer: FAIL")
        for x in bad:
            print("  - " + x)
        return 1
    print("\nvidlayer: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
