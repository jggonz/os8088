#!/usr/bin/env python3
"""The encoder front end and its budgets - SPEC.md 98.2.1, VIDEO-PLAN W8.

    python3 tests/videnc.py

Host-side. ffmpeg makes a source - five seconds of testsrc2, a moving
colour pattern, frozen for its last two, 16:9, with a tone - and
tools/os88venc.py encodes it three ways. Four questions:

1. IS THE CANVAS THE SOURCE'S SHAPE? (and --poster-at 2.1 s names the
   keyframe nearest it, 1 of the ones every 2 s) 16:9 in the Hercules preset's
   400 x 200 box, whose pixels are 29:45, is 400 x 145 - worked here from
   the aspect, not read back from the tool.
2. WITH NO LIMITS, IS EVERY FRAME THE TARGET? The lossless profile's file,
   decoded frame by frame, must equal the dithered target of every frame,
   exactly - the stream is the target when nothing stops it.
3. UNDER TIGHT LIMITS, ARE THEY KEPT? A 20 KB/s disk with a 16 KB reserve
   and a 30%/50% CPU
   must CUT frames (or this tests nothing), and then every record, priced
   by the wave 0 model on its actual bytes, is under the per-frame ceiling,
   and a replay of both buckets never goes below empty. And the FIRST
   KEYFRAME is a whole picture, exactly its target (SPEC.md 98.2.9's
   pre-roll): a half-painted key 0 is where a colour play starts.
   3b. OWED TIME (98.2.1.1): a cut every fifth frame, a 30% ceiling and
   `--owe 1.6`. Frames must run past their period (or this tests nothing),
   and the silent hook's schedule replayed from the file never finds three
   frames due and is behind no two calls running. Broken on purpose - the
   two frames after an overrun given the overrun's ceiling instead of one
   steady one between them - it drops 11 frames and is 42 calls behind.
   3c. WHAT A CUT FRAME SPENDS ON (98.2.1.2): the same clip at 12 KB/s with
   the look-ahead and the error as seen (the defaults) must flicker back at
   most half as much as with neither, and be no worse as seen; and the
   header names the ring its reserve banks in - 3 slots at 16 KB, 8 at the
   default 192 (98.1.1). 3d. --aim size (98.2.1.4) at the default budget
   must be 5% smaller than as asked, and still converge on the still.
4. DOES IT CONVERGE? Two seconds of a still picture after the motion: the
   last frame on screen must be the target, the errors the budget left all
   fixed.
6. IS THE ADPCM4 SEARCH EXACT? The Viterbi encoder must beat the greedy
   one by 3 dB or more (it is ~7 on real sound), the segments it stitches
   on four cores must be BYTE-IDENTICAL to one whole search, and every
   keyframe's scale is 0 in all of them - with no lead too, which is the
   path where a segment is searched again from where the last one ended.
7. IS COMPOSITE COLOUR ITS OWN NIBBLE? The palette - reenigne's model,
   what MartyPC and 86Box show - is held to the classic 16 colours, and
   cells alternating each colour with its complement must come back as
   exactly those nibbles, the left cell in the high half. Broken on purpose (the nibbles packed
   low-first) it FAILS naming the colours that came back wrong.
9. IS 256 COLOURS A FILE? --preset vga8-small with no limits: VGA8 on
   LIN320, 15 fps by default, true black at index 0 (what the screen round
   the canvas and a keyframe start from, though this source holds no
   black), and every frame its target - and the same in Mode X, whose
   frames are sub-records under a Map Mask; and at --detail 2x2 in Mode X,
   half the rows shown twice and every store a pair or a group - and CGA
   in colour, mode 4 with the palette byte the overrides name (39h) and
   the 160 x 100 text hack, every frame its target.
8. DOES COMPOSITE DIFFUSION SEE THE MODEL? A picture rendered through the
   model from known nibbles, in runs of 2 to 6 cells, must come back as 90%
   or more of them (94% measured). Without the lookahead - a cell judged as
   if its colour ran on to the right - it is 85%, and FAILS.
5. IS FLAT FLAT? A black a few levels off black, and a white a few off
   white, with noise - what an MP4 delivers - must dither solid. Spread over
   the whole 0..255 the threshold map lit one dot in every 8 x 8 tile of
   Bad Apple's black and white (the owner's report; --clip 0 is that).
   5c.1. IS IT STILL FLAT ONCE A GRADIENT GOES? (98.2.3) A VGA8 field that
   fades into white, and one into black, by less than --vga8-stable must
   be solid on the second frame; with the dead band holding past the clip,
   as it did, every dot of the first frame stays (the owner's Bad Carrot).
   5f. IS A MEASURED MACHINE PRICED AS IT WAS MEASURED? (98.2.3.3) Each
   of VIDBENCH's synthetic frames, priced by `286-vga`'s and `486`'s own
   tables, must come back within 1% of the microseconds the owner's 86Box
   machines decoded it in - and the wave 0 model scaled by `speed` must be
   10% or more under on a frame of runs (24% on the 286, 18% on the 486),
   which is the mispricing the
   tables exist for (with profile_table ignoring `cyc_us`, it FAILS on
   every row of runs).
   5g. DOES A FLIPPED FILE KEEP UP? (98.2.1.1.1) A noisy pan flipped on
   the 486 at 90% / 140%, replayed as the player runs it - a call every
   half period off the card, one frame a call - must leave no frame a
   whole frame behind. Without the flip schedule (the ceiling the peak
   alone) 68 are.

Broken on purpose - the measured retry in Encoder.frame skipped, so a
frame is chosen by the per-span estimate alone - question 3 FAILS naming
the frame over its ceiling. The whole row needs ffmpeg and numpy, and
SKIPS without them: that is the box declining to answer, not a pass.
"""
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88vid as vid                                        # noqa: E402
import os88venc as venc                                      # noqa: E402

SKIP = 77


def hook_schedule(r, period, over):
    """The SILENT hook's schedule (SPEC.md 98.3), replayed from the file:
    a call a period, at most two frames a call, a third due dropped. ->
    frames past their period, frames dropped, and the most calls in a row
    that found two due"""
    free, pair, ov, drops, t0 = 0.0, False, 0, 0, 0.0
    behind = best = 0
    for i, (rec, a_, i_) in enumerate(r.records()):
        c = vid.cycles_of(rec, layout=r.g.layout) + over
        alone = False
        if pair:
            start, pair = free, False
        else:
            k = max(i, -int(-(free - t0) // period))
            n = k - i + 1
            if n > 2:
                drops += n - 2
                t0 += (n - 2) * period
            start = t0 + k * period
            pair, alone = n >= 2, n == 1
            behind = behind + 1 if pair else 0
            best = max(best, behind)
        free = start + c
        if alone and free > t0 + (i + 1) * period:
            ov += 1
    return ov, drops, 0, best


def main():
    if venc.np is None or not shutil.which("ffmpeg"):
        print("   SKIP: needs ffmpeg and numpy")
        return SKIP
    np = venc.np
    bad = []
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "src.mkv")
        subprocess.run(
            ["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
             "testsrc2=size=320x180:rate=30:duration=3,tpad=stop_mode=clone:"
             "stop_duration=2", "-f", "lavfi", "-i",
             "sine=frequency=440:sample_rate=22050", "-t", "5",
             "-c:v", "ffv1", "-c:a", "pcm_s16le", src], check=True)

        def run(name, *args):
            a = venc.parser().parse_args(
                [src, os.path.join(tmp, name + ".V88"), "--quiet"] +
                list(args))
            keep = []
            res = venc.encode(a, keep)
            return a.out, res, keep

        # --- 1: the canvas's shape
        path, res, keep = run("lossless", "--preset", "herc",
                              "--profile", "lossless", "--rate", "11025",
                              "--poster-at", "2.1")
        want_h = round(400 * 29 / 45 / (16 / 9))
        print("   16:9 in Hercules' 400 x 200: %d x %d (want 400 x %d)"
              % (res["w"], res["h"], want_h))
        if (res["w"], res["h"]) != (400, want_h):
            bad.append("the canvas is %d x %d, not 400 x %d"
                       % (res["w"], res["h"], want_h))
        # --- 2: no limits, every frame the target
        vid.verify_v88(path)
        r = vid.Reader(path)
        print("   --poster-at 2.1: poster keyframe %d (want 1, frame %d)"
              % (r.poster, r.keys[1][0]))
        if r.poster != 1:
            bad.append("--poster-at 2.1 made keyframe %d the poster, not 1"
                       % r.poster)
        diff = 0
        for f, surf, rec, at, i in vid.v88_frames(r):
            if r.g.canvas(surf) != keep[f].tobytes():
                diff += 1
        print("   lossless: %d frames, %d differ from their target"
              % (r.frames, diff))
        if diff or r.frames != len(keep):
            bad.append("lossless: %d of %d frames are not their target"
                       % (diff, r.frames))
        # --- 9: VGA8, 256 colours (VIDEO-PLAN W11a): with no limits every
        # frame is its target, at 15 fps by default, TRUE black at index 0 - the
        # screen round the canvas - though testsrc2 holds none
        path, res, keep = run("vga8", "--preset", "vga8-small",
                              "--profile", "lossless", "--audio", "none")
        vid.verify_v88(path)
        r = vid.Reader(path)
        diff = sum(1 for f, surf, rec, at, i in vid.v88_frames(r)
                   if r.g.canvas(surf) != keep[f].tobytes())
        print("   vga8: %s %d x %d at %.1f fps, index 0 %s, %d of %d frames "
              "differ from their target"
              % (vid.PF_NAMES[r.pixfmt], r.g.wb, r.g.h, r.fps,
                 tuple(r.palette[:3]), diff, r.frames))
        if (r.pixfmt, r.g.layout) != (vid.PF_VGA8, vid.LAY_LIN320) or \
                abs(r.fps - 15.0) > 0.01 or diff or \
                tuple(r.palette[:3]) != (0, 0, 0) or r.frames != len(keep):
            bad.append("vga8: format %d layout %d at %.2f fps, index 0 %s, "
                       "%d frames differ"
                       % (r.pixfmt, r.g.layout, r.fps,
                          tuple(r.palette[:3]), diff))
        # ...and in Mode X, where a frame is sub-records (98.1.3.1)
        path, res, keep = run("modex", "--preset", "modex-small",
                              "--profile", "lossless", "--audio", "none")
        vid.verify_v88(path)
        r = vid.Reader(path)
        diff = sum(1 for f, surf, rec, at, i in vid.v88_frames(r)
                   if r.g.canvas(surf) != keep[f].tobytes())
        print("   modex: %s %d x %d, %d of %d frames differ from their "
              "target" % (vid.PF_NAMES[r.pixfmt], r.g.w, r.g.h, diff,
                          r.frames))
        if r.g.layout != vid.LAY_MODEX or diff or r.frames != len(keep):
            bad.append("modex: layout %d, %d frames differ"
                       % (r.g.layout, diff))
        # ...and in sixteen colours on mode 12h's planes (98.1.3.2)
        path, res, keep = run("vga4", "--preset", "vga4", "--pixfmt",
                              "vga4", "--profile", "lossless", "--audio",
                              "none")
        vid.verify_v88(path)
        r = vid.Reader(path)
        diff = sum(1 for f, surf, rec, at, i in vid.v88_frames(r)
                   if r.g.canvas(surf) != keep[f].tobytes())
        print("   vga4: %s %d x %d, %d of %d frames differ from their "
              "target" % (vid.PF_NAMES[r.pixfmt], r.g.w, r.g.h, diff,
                          r.frames))
        if r.pixfmt != vid.PF_VGA4 or not r.g.bitplanes or diff or \
                r.frames != len(keep):
            bad.append("vga4: format %d, %d frames differ"
                       % (r.pixfmt, diff))
        # ...and CGA IN COLOUR (98.1.3.3): mode 4 with its palette byte as
        # the overrides fixed it, and the 160 x 100 text hack, every frame
        # its target
        for name, extra, pf, lay in (
                ("cga4", ("--cga-palette", "1", "--cga-bright", "1",
                          "--cga-bg", "9"), vid.PF_CGA4, vid.LAY_CGA),
                ("c160", (), vid.PF_C160, vid.LAY_C160),
                ("c512", (), vid.PF_C512, vid.LAY_TXT)):
            path, res, keep = run(name, "--preset", name, "--profile",
                                  "lossless", "--audio", "none", *extra)
            vid.verify_v88(path)
            r = vid.Reader(path)
            diff = sum(1 for f, surf, rec, at, i in vid.v88_frames(r)
                       if r.g.canvas(surf) != keep[f].tobytes())
            print("   %s: %s %d x %d bytes, palette byte %02Xh, %d of %d "
                  "frames differ from their target"
                  % (name, vid.PF_NAMES[r.pixfmt], r.g.wb, r.g.h, r.cgapal,
                     diff, r.frames))
            if (r.pixfmt, r.g.layout) != (pf, lay) or diff or \
                    r.frames != len(keep) or \
                    r.cgapal != {vid.PF_CGA4: 0x39,
                                 vid.PF_C512: vid.CARD_BOTH}.get(pf, 0):
                bad.append("%s: format %d layout %d palette %02Xh, %d frames "
                           "differ" % (name, r.pixfmt, r.g.layout, r.cgapal,
                                       diff))
        # ...and at a DETAIL of 2 x 2 (98.2.4): half the rows, shown twice,
        # and every pixel a pair - so no plane byte stands alone
        path, res, keep = run("modexd", "--preset", "modex-small",
                              "--profile", "lossless", "--audio", "none",
                              "--detail", "2x2")
        vid.verify_v88(path)
        r = vid.Reader(path)
        diff = sum(1 for f, surf, rec, at, i in vid.v88_frames(r)
                   if r.g.canvas(surf) != keep[f].tobytes())
        masks = set()
        for rec, _, _ in r.records():
            si = 6
            while rec[si]:
                masks.add(rec[si])
                si = vid.walk_lists(bytearray(65536), rec, si + 1)
        print("   modex 2x2: %d x %d shown x%d, Map Masks %s, %d of %d "
              "frames differ" % (r.g.w, r.g.h, r.rowscale, sorted(masks),
                                 diff, r.frames))
        if r.rowscale != 2 or diff or masks & {1, 2, 4, 8} or \
                r.frames != len(keep):
            bad.append("modex 2x2: row scale %d, masks %s, %d frames "
                       "differ" % (r.rowscale, sorted(masks), diff))
        # --- 3: tight limits kept
        path, res, keep = run("tight", "--preset", "herc", "--disk", "20000",
                              "--avg", "0.30", "--peak", "0.50", "--owe",
                              "0", "--rate", "5512", "--profile", "floppy",
                              "--reserve", "16")
        vid.verify_v88(path)
        r = vid.Reader(path)
        period, acyc, abps = res["period"], res["audio_cyc"], res["audio_bps"]
        fps = res["fps"]
        peak = 0.50 * period
        cpu_per = 0.30 * period - acyc
        dsk_per = (20000 * 0.99 - abps) / fps
        cpu = venc.Budget(cpu_per, cpu_per * fps)
        dsk = venc.Budget(dsk_per, 16 * 1024)   # (floppy's disk is flat)
        over, cut, low = [], 0, [0.0, 0.0]
        screens = []
        for f, surf, rec, at, i in vid.v88_frames(r):
            cpu.tick()
            dsk.tick()
            c = vid.cycles_of(rec)
            n = len(rec) - r.abytes
            cpu.spend(c)
            dsk.spend(n)
            low = [min(low[0], cpu.level), min(low[1], dsk.level)]
            if c + acyc > peak + 1:
                over.append((f, c + acyc))
            cv = r.g.canvas(surf)
            if cv != keep[f].tobytes():
                cut += 1
            screens.append(cv)
        print("   20 KB/s, 30%%/50%%: %d of %d frames short of their "
              "target; worst frame %.1f%%; lowest buckets: CPU %.0f "
              "cycles, disk %.0f bytes"
              % (cut, r.frames, 100 * max(vid.cycles_of(rec) + acyc
                                         for rec, a_, i_ in r.records())
                 / period, low[0], low[1]))
        if not cut:
            bad.append("the tight profile cut nothing: it tests nothing")
        # ...and its FIRST PICTURE is whole before the keyframes start (the
        # pre-roll, SPEC.md 98.2.9): key 0 is exactly its frame's target -
        # a half-painted one is where a colour play starts
        k0 = r.keys[0][0]
        kpic = vid.decode_at(r, k0)
        print("   the first keyframe at frame %d, %s its target"
              % (k0, "exactly" if kpic == keep[k0].tobytes() else "NOT"))
        if not k0 or kpic != keep[k0].tobytes():
            bad.append("the first keyframe (frame %d) is not a whole "
                       "picture: no pre-roll" % k0)
        for f, c in over[:3]:
            bad.append("frame %d costs %.1f%% of its period, over the 50%% "
                       "ceiling" % (f, 100 * c / period))
        if low[0] < -1 or low[1] < -1:
            bad.append("a bucket went below empty (CPU %.0f, disk %.0f)"
                       % tuple(low))
        # --- 3b: OWED TIME (98.2.1.1) - a frame may run past its period,
        # and the player's own schedule, replayed here from the file, never
        # finds three frames due (a silent play would drop one) and is on
        # time again the call after
        cuts = os.path.join(tmp, "cuts.mkv")    # the picture inverted
        subprocess.run(                         # every fifth frame: a cut
            ["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
             "testsrc2=size=320x180:rate=30:duration=3,"
             "negate=enable=lt(mod(n\\,10)\\,5)", "-f", "lavfi", "-i",
             "sine=frequency=440:sample_rate=22050", "-t", "3",
             "-c:v", "ffv1", "-c:a", "pcm_s16le", cuts], check=True)
        a = venc.parser().parse_args(
            [cuts, os.path.join(tmp, "owed.V88"), "--quiet", "--preset",
             "herc-full", "--disk", "200000", "--avg", "0.50", "--peak",
             "0.30", "--owe", "1.6", "--rate", "5512"])
        res = venc.encode(a, [])
        r = vid.Reader(a.out)
        ov, drops, behind, run2 = hook_schedule(
            r, res["period"], res["audio_cyc"] + venc.HOOK_CYC)
        worst = max(vid.cycles_of(rec) + res["audio_cyc"]
                    for rec, a_, i_ in r.records()) / res["period"]
        print("   owed time at 30%%/1.6, a cut every 5 frames: %d frames past their period (worst "
              "%.0f%%), %d dropped, longest run of calls behind %d"
              % (ov, 100 * worst, drops, run2))
        if not ov:
            bad.append("owed time: no frame ran past its period - the leg "
                       "tests nothing")
        if drops or run2 > 1:
            bad.append("owed time: the hook's schedule dropped %d frames and "
                       "was behind %d calls running" % (drops, run2))
        # --- 3c: WHAT A CUT FRAME SPENDS ON (98.2.1.2): at 12 KB/s the
        # look-ahead and the error as seen (the defaults) against neither -
        # the picture as played must flicker back at most half as much and
        # be no worse as seen
        def picq(name, *args):
            p, rs, k = run(name, "--preset", "herc", "--audio", "none",
                           *args)
            return rs, vid.Reader(p), k
        q0, r0, k0 = picq("plain", "--disk", "12000", "--reserve", "16",
                          "--lookahead", "0", "--error", "bits")
        q1, r1, k1 = picq("ahead", "--disk", "12000", "--reserve", "16")
        print("   at 12 KB/s, as before / the defaults: flicker %.1f / %.1f "
              "pixels a frame, error as seen %.2f%% / %.2f%%"
              % (q0["q_flick"], q1["q_flick"], 100 * q0["q_vis"],
                 100 * q1["q_vis"]))
        if not q0["cutf"]:
            bad.append("the look-ahead leg cut nothing: it tests nothing")
        if q1["q_flick"] > q0["q_flick"] / 2 or q1["q_vis"] > q0["q_vis"]:
            bad.append("the look-ahead flickered %.1f against %.1f, error "
                       "%.4f against %.4f" % (q1["q_flick"], q0["q_flick"],
                                              q1["q_vis"], q0["q_vis"]))
        # ...and the ring the stream assumes, in its header (98.1.1): the
        # slots less TWO hold the reserve (98.2.1.3), so a 16 KB reserve
        # wants 4 - a 2-slot ring banks nothing - and the default 192 KB 8
        q2, r2, k2 = picq("asked")
        print("   the ring the header asks: %d slots at 16 KB, %d at the "
              "default reserve" % (r1.ring, r2.ring))
        if (r1.ring, r2.ring) != (3, 8):
            bad.append("rings of %d and %d slots, not 3 and 8"
                       % (r1.ring, r2.ring))
        # --- 3d: --aim size (98.2.1.4): smaller than asked where the budget
        # is not what binds, and the still after the motion still converges
        q3, r3, k3 = picq("size", "--aim", "size")
        left = vid.decode_at(r3, r3.frames - 1) != k3[-1].tobytes()
        print("   --aim size: %d bytes against %d asked, the still %s"
              % (q3["bytes"], q2["bytes"],
                 "NOT converged" if left else "converged"))
        if q3["bytes"] > q2["bytes"] * 0.95 or left:
            bad.append("--aim size: %d bytes against %d, the still %s"
                       % (q3["bytes"], q2["bytes"],
                          "not converged" if left else "converged"))
        # --- 4: converged on the still
        last = screens[-1] == keep[-1].tobytes()
        still = sum(1 for f in range(len(keep)) if np.array_equal(
            keep[f], keep[-1]))
        print("   the still's %d frames: the last is its target: %s"
              % (still, "yes" if last else "NO"))
        if not last:
            bad.append("after %d frames of a still picture the screen is "
                       "not its target" % still)
        # --- 5: a flat black and a flat white, as a lossy source delivers
        # them - a few levels off, and noisy - dither SOLID, not to a grid
        for name, grey, want in (("black", 4, 0), ("white", 251, 255)):
            flat = os.path.join(tmp, name + ".mkv")
            subprocess.run(
                ["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
                 "color=c=0x%02x%02x%02x:size=320x240:rate=30:duration=1,"
                 "noise=alls=3:allf=t" % (grey, grey, grey), "-c:v", "ffv1",
                 flat], check=True)
            a = venc.parser().parse_args(
                [flat, os.path.join(tmp, name + ".V88"), "--quiet",
                 "--preset", "cga", "--audio", "none", "--levels", "none"])
            keep = []
            venc.encode(a, keep)
            dots = sum(int((np.unpackbits(k) != (want & 1)).sum())
                       for k in keep)
            print("   flat %s from grey %d: %d dots in %d frames"
                  % (name, grey, dots, len(keep)))
            if dots:
                bad.append("a flat %s (grey %d) dithers to %d dots, not "
                           "solid" % (name, grey, dots))
    # --- 5b: ...and the COLOUR formats' solid colours (--clip, SPEC.md
    # 98.2.3, 98.2.5): a noisy flat field that is nearly a palette colour -
    # the black level, and the 16's red - stays that colour, where the
    # pattern and the ordered dither left an even grid of dots. --clip 0
    # is the old dither, and must show the dots, or the leg tests nothing
    rnd = np.random.default_rng(5)
    ramp = bytes(np.repeat(np.arange(256) // 4, 3).astype(np.uint8))
    for name, base, mk in (
            ("vga4 black", (6, 5, 8),
             lambda c: venc.KnollDitherer(160, 120, vid.STD16, 0, c)),
            ("vga4 red", (165, 6, 6),
             lambda c: venc.KnollDitherer(160, 120, vid.STD16, 0, c)),
            ("vga8 black", (5, 5, 5),
             lambda c: venc.Vga8Ditherer(160, 120, ramp, 24, 0, c))):
        f = np.clip(np.array(base, np.float32) + rnd.normal(
            0, 3, (120, 160, 3)), 0, 255).astype(np.uint8)
        stray = []
        for clip in (0, 16):
            d = mk(clip)
            idx = d(f)                  # (a first frame: nothing held)
            vals, cnt = np.unique(idx, return_counts=True)
            stray.append(int(idx.size - cnt.max()))
        print("   %s: %d stray dots with --clip 0, %d with 16 (of %d)"
              % (name, stray[0], stray[1], f.shape[0] * f.shape[1]))
        if stray[0] < 200:
            bad.append("%s: --clip 0 left %d dots - the leg tests nothing"
                       % (name, stray[0]))
        if stray[1] * 10 > stray[0] or stray[1] > 0.01 * f.size / 3:
            bad.append("%s: --clip 16 still leaves %d stray dots"
                       % (name, stray[1]))
    # --- 5c: ...and a colour held by the PATTERN'S stability (SPEC.md
    # 98.2.5) is one the pixel's new plan still mixes. A dim red field
    # puts red in a few cells of each tile; when it sinks to near-black by
    # less than --vga4-stable, black's plan is black and the red goes - it
    # once stayed until the scene changed, a trail behind every line that
    # crossed a dark picture. The field must show red first, or the leg
    # tests nothing
    dim = np.full((120, 160, 3), (30, 6, 6), np.uint8)
    dark = np.full((120, 160, 3), (12, 6, 6), np.uint8)
    for name, mk, red in (
            ("vga4", lambda: venc.KnollDitherer(160, 120, vid.STD16, 24.0,
                                                16.0), lambda i: i != 0),
            ("c512 pattern", lambda: venc.C512Ditherer(
                160, 120, vid.CARD_NEW, 0, 0, mix=4, mstable=24.0),
             None)):
        d = mk()
        a0 = d(dim)
        a1 = d(dark)
        if red is None:                 # C512: no code of black's plan
            fresh = mk()(dark)
            red = lambda i, f=fresh: i != f
            held = int(red(a1).sum())
            first = int((a0 != fresh).sum())
        else:
            held, first = int(red(a1).sum()), int(red(a0).sum())
        print("   %s: %d dots in a dim red field, %d left when it goes "
              "near-black" % (name, first, held))
        if first < 100:
            bad.append("%s: the dim field put %d dots down - the leg tests "
                       "nothing" % (name, first))
        if held:
            bad.append("%s: %d dots outlived the dim field the pattern put "
                       "them down for" % (name, held))
    # --- 5c.1: ...and VGA8's dead band never holds a pixel the clip makes
    # SOLID (98.2.3). A grey field that fades up into white, and a glow
    # that fades out into black, each by less than --vga8-stable: the
    # dither's colours on the first frame were held on the second, a grey
    # speck standing still in a white or a black for as long as it stayed
    # flat (the owner's Bad Carrot, 2026-10-07). The first frame must put
    # down colours the second would hold, or the leg tests nothing
    for name, was, now, end in (("white", 236, 248, 255),
                                ("black", 20, 8, 0)):
        d = venc.Vga8Ditherer(160, 120, ramp, 24, 18.0, 16)
        f0 = np.full((120, 160, 3), was, np.uint8)
        f1 = np.full((120, 160, 3), now, np.uint8)
        a0 = d(f0)
        first = int((np.abs(d.pal[a0] - now).max(-1) * np.sqrt(3) <
                     18.0).sum() - (np.abs(d.pal[a0] - end).max(-1) <
                                    1).sum())
        a1 = d(f1)
        held = int((np.abs(d.pal[a1] - end).max(-1) >= 1).sum())
        print("   vga8 %s: %d dots the dead band could hold, %d held when "
              "the field goes solid" % (name, first, held))
        if first < 100:
            bad.append("vga8 %s: the first field put %d holdable dots down "
                       "- the leg tests nothing" % (name, first))
        if held:
            bad.append("vga8 %s: %d dots held in a field the clip makes "
                       "solid" % (name, held))
    # --- 5c.2: --palette greyN (98.2.3.7): a ramp of N greys. A flat grey
    # ON a level is that level alone - Vga8Ditherer spreads one over three
    # of its 32 - a horizontal ramp reaches every level, the ends are solid,
    # and a Mode X file made with it uses its N indices and no other. The
    # control is the 256-colour ditherer over the same 64-grey ramp: it
    # must spread the flat level over several, or the leg tests nothing
    for n in (2, 4, 64):
        lv = [(v * 255 + 31) // 63 for v in venc.grey_levels(n)]
        mid = lv[len(lv) // 2] if n > 2 else 128
        flat = venc.GreyDitherer(160, 120, n, "bayer", 0.0, 16)(
            np.full((120, 160, 3), mid, np.uint8))
        nflat = len(np.unique(flat))
        ramp_in = np.tile(np.arange(256, dtype=np.uint8)[None, :, None],
                          (16, 1, 3))
        reach = len(np.unique(venc.GreyDitherer(256, 16, n, "bayer", 0.0,
                                                0)(ramp_in)))
        got = venc.GreyDitherer(256, 16, n, "bayer", 0.0, 16)(ramp_in)
        ends = (int(got[:, :17].max()), int(got[:, -17:].min()))
        print("   grey%d: a flat level is %d level(s), a ramp reaches %d of "
              "%d, its ends %d and %d" % (n, nflat, reach, n, ends[0],
                                          ends[1]))
        if n == 64:
            ctl = len(np.unique(venc.Vga8Ditherer(
                160, 120, venc.grey_palette(64), 24, 0, 16)(
                np.full((120, 160, 3), mid, np.uint8))))
            print("   (control: the 256-colour ditherer makes that flat "
                  "level %d levels)" % ctl)
            if ctl < 2:
                bad.append("grey64's control made one level - the leg "
                           "tests nothing")
        if (n > 2 and nflat != 1) or reach != n or ends != (0, n - 1):
            bad.append("grey%d: a flat level made %d levels, a ramp %d "
                       "of %d, ends %s" % (n, nflat, reach, n, ends))
    gtmp = tempfile.mkdtemp()
    gsrc, g4 = os.path.join(gtmp, "g.mkv"), os.path.join(gtmp, "g4.V88")
    subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
                    "testsrc2=size=320x240:rate=15:duration=1", "-c:v",
                    "ffv1", gsrc], check=True)
    venc.encode(venc.parser().parse_args(
        [gsrc, g4, "--quiet", "--preset", "modex-small", "--profile",
         "lossless", "--audio", "none", "--palette", "grey4"]), None)
    r = vid.Reader(g4)
    surf = r.g.surface()
    used = set()
    for rec, _, _ in r.records():
        r.apply(surf, rec)
        used |= set(r.g.canvas(surf))
    want = bytes(v for v in venc.grey_levels(4) for _ in range(3))
    print("   --palette grey4: indices %s, the palette's first four %s"
          % (sorted(used), "the ramp" if r.palette[:12] == want else
             "NOT the ramp"))
    if not used <= {0, 1, 2, 3} or r.palette[:12] != want:
        bad.append("--palette grey4 used %s, palette %s"
                   % (sorted(used), r.palette[:12].hex()))
    # --- 5c.3: --screen (98.2.5.1): sixteen colours on a screen of its
    # own, flipped, in a ramp of its own - the screen and the pages in the
    # header, the canvas at the screen's shape, and N colours written as
    # PLANE_CODES' values with the palette's entries at them. A file whose
    # 2 greys came out as 0 and 1 would cost four stores a byte, not one
    for scrn, pal, n, box in (("640x400", "grey4", 4, (640, 400)),
                              ("320x240", "grey2", 2, (320, 240))):
        sv = os.path.join(gtmp, "s%s.V88" % scrn)
        venc.encode(venc.parser().parse_args(
            [gsrc, sv, "--quiet", "--preset", "vga4-full", "--profile",
             "lossless", "--audio", "none", "--screen", scrn, "--palette",
             pal, "--flip"]), None)
        r = vid.Reader(sv)
        surf = r.g.surface()
        used = set()
        for rec, _, _ in r.records():
            r.apply(surf, rec)
            used |= set(r.g.canvas(surf))
        codes = vid.PLANE_CODES[n]
        lv = venc.grey_levels(n)
        palok = all(tuple(r.palette[3 * c:3 * c + 3]) == (v, v, v)
                    for c, v in zip(codes, lv)) and not any(r.palette[48:])
        print("   --screen %s --palette %s: screen %d, flip %s, canvas %d "
              "x %d, values %s, palette %s" % (
                  scrn, pal, r.screen, r.flip, r.g.w, r.g.h,
                  " ".join("%X" % v for v in sorted(used)),
                  "at them" if palok else "WRONG"))
        if r.screen != vid.SCREEN_BY_NAME[scrn] or not r.flip or \
                (r.g.w, r.g.h) != box or not used <= set(codes) or \
                not palok:
            bad.append("--screen %s --palette %s: screen %d flip %s canvas "
                       "%dx%d values %s palette %s"
                       % (scrn, pal, r.screen, r.flip, r.g.w, r.g.h,
                          sorted(used), palok))
    # --- 5d: --cut (98.2.1.2.1), Mode X pans lossless, so the 30 KB a
    # record holds is the only thing that cuts them; the decoded file is
    # held to the targets frame for frame. A FLAT picture flipping to its
    # negative each frame is about half the size the per-span estimate
    # says: RANK cuts frames that would have fitted, and FILL grows the
    # estimate until the record fills its room - here, the whole frame.
    # A NOISY one overruns the record whatever is done: TEAR keeps whole
    # rows in screen order, so a frame's stale rows are one band (and its
    # wrap) where RANK scatters them
    def cut_pan(name, vf, modes):
        ctmp = tempfile.mkdtemp()
        pan = os.path.join(ctmp, "pan.mkv")
        subprocess.run(
            ["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
             "testsrc2=size=960x240:rate=24:duration=1", "-vf",
             "crop=320:240:'mod(n*24,640)':0," + vf, "-c:v", "ffv1", pan],
            check=True)
        res = {}
        for mode in modes:
            out = os.path.join(ctmp, "pan_%s.V88" % mode)
            keep = []
            venc.encode(venc.parser().parse_args(
                [pan, out, "--quiet", "--preset", "modex", "--profile",
                 "lossless", "--audio", "none", "--cut", mode]), keep)
            r = vid.Reader(out)
            surf = r.g.surface()
            wrong, runs = 0, []
            for f, (rec, _, _) in enumerate(r.records()):
                r.apply(surf, rec)
                cv = np.frombuffer(bytes(r.g.canvas(surf)), np.uint8) \
                    .reshape(r.g.h, r.g.w)
                w = cv != keep[f]
                wrong += int(w.sum())
                st = w.mean(1) > 0.02           # a row 2% wrong is stale
                runs.append(int(st[0]) + int((st[1:] & ~st[:-1]).sum()))
            res[mode] = (wrong, max(runs), sum(x > 0 for x in runs))
            print("   --cut %s, %s pan: %d pixels wrong in %d frames, at "
                  "most %d bands of stale rows in one" % (
                      mode, name, wrong, len(runs), res[mode][1]))
        shutil.rmtree(ctmp, ignore_errors=True)
        return res
    fl = cut_pan("flat", "negate=enable='mod(n\\,2)'", ("rank", "fill"))
    if fl["rank"][0] < 10000:
        bad.append("--cut: rank left %d pixels wrong on the flat pan - the "
                   "leg tests nothing" % fl["rank"][0])
    if fl["fill"][0] * 10 > fl["rank"][0]:
        bad.append("--cut fill left %d pixels wrong on the flat pan, rank "
                   "%d: the record was not grown into its room"
                   % (fl["fill"][0], fl["rank"][0]))
    nz = cut_pan("noisy", "noise=alls=30:allf=t", ("rank", "tear"))
    if nz["rank"][2] < 10 or nz["rank"][1] <= 3:
        bad.append("--cut: rank's noisy pan cut %d frames, %d bands - the "
                   "leg tests nothing" % (nz["rank"][2], nz["rank"][1]))
    if nz["tear"][1] > 3:
        bad.append("--cut tear left %d bands of stale rows in a frame - a "
                   "tear is its line, its wrap and a half-done row"
                   % nz["tear"][1])
    # --- 5e: --bands (98.2.1.2.2): a Mode X frame drawn in bands of rows,
    # every plane of a band before the next - so the rows its writes land
    # on, in the order the player makes them, only ever move DOWN a band,
    # where plane by plane they climb back to the top at every plane
    ctmp = tempfile.mkdtemp()
    pan = os.path.join(ctmp, "pan.mkv")
    subprocess.run(
        ["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
         "testsrc2=size=960x240:rate=24:duration=0.5", "-vf",
         "crop=320:240:'mod(n*24,640)':0,noise=alls=30:allf=t",
         "-c:v", "ffv1", pan], check=True)
    climbs = {}
    for nb in (0, 8):
        out = os.path.join(ctmp, "b%d.V88" % nb)
        venc.encode(venc.parser().parse_args(
            [pan, out, "--quiet", "--preset", "modex", "--profile",
             "lossless", "--audio", "none", "--bands", str(nb)]))
        vid.verify_v88(out)
        r = vid.Reader(out)
        up = 0
        for rec, _, _ in r.records():
            rows, si = [], vid.REC_HDR
            buf = bytearray(vid.PLANE * 4)
            while rec[si]:
                mask = rec[si]
                si += 1
                for p in range(4):
                    if mask >> p & 1:
                        end = vid.walk_lists(
                            memoryview(buf)[p * vid.PLANE:], rec, si,
                            lambda k, di, m: rows.append(r.g.rowof[di]))
                si = end
            bands = [y * 8 // r.g.h for y in rows]     # (eighths, both)
            up += sum(1 for a, b in zip(bands, bands[1:]) if b < a)
        climbs[nb] = up
        print("   --bands %d: the writes went back up a band %d times over "
              "%d frames" % (nb, up, r.frames))
    # ...and at 63.5 KB in 12 bands a whole noisy 320 x 240 frame - 75 KB
    # of changes - is CUT to its room: the estimate left the bands' own
    # bytes out and built it 2.6 KB past the record's length word, which
    # ended the encode (the owner's report, 2026-10-07)
    out = os.path.join(ctmp, "big.V88")
    try:
        venc.encode(venc.parser().parse_args(
            [pan, out, "--quiet", "--preset", "modex", "--profile",
             "lossless", "--audio", "none", "--bands", "12", "--frame-cap",
             "63.5", "--detail", "1x1", "--fit", "fill"]))
        vid.verify_v88(out)
        big = max(len(rec) for rec, _, _ in vid.Reader(out).records())
        print("   --bands 12 at 63.5 KB: the largest record %d bytes" % big)
        if big > 127 * vid.SECTOR - 4:
            bad.append("--bands 12 at 63.5 KB: a record of %d bytes" % big)
    except vid.V88Error as e:
        bad.append("--bands 12 at 63.5 KB: %s" % e)
    shutil.rmtree(ctmp, ignore_errors=True)
    if climbs[0] == 0:
        bad.append("--bands: plane by plane never climbed - the leg tests "
                   "nothing")
    if climbs[8]:
        bad.append("--bands 8: the writes climbed back %d times"
                   % climbs[8])
    # --- 5f: a profile's measured decode (98.2.3.3): VIDBENCH's frames, as
    # the owner's 86Box machines decoded them (microseconds, the report's
    # rows), priced back by each profile's own table
    meas = {"286-vga": (28.1, 900.1, 92.6, 1199.9, 1291.3, 1516.3, 1427.1,
                        2606.7, 2376.8, 2662.8, 2379.6, 246.4),
            "486": (2.5, 232.8, 19.7, 394.7, 428.5, 557.8, 545.6, 1055.4,
                    1010.3, 1035.5, 977.4, 60.1)}
    for name, us in meas.items():
        P = venc.PROFILES[name]
        k = vid.HZ / 1e6 * P["speed"]
        t, sub = venc.profile_table(P, vid.LAY_MODEX)
        worst, under = 0.0, 1.0
        for (label, ops), m in zip(vid.synth_frames(), us):
            rec = bytes(vid.REC_HDR) + vid.to_lists(ops)[0]
            got = vid.cycles_of(rec, table=t) / k
            worst = max(worst, abs(got / m - 1))
            if label.startswith("S RUN"):
                under = min(under, vid.cycles_of(rec) / k / m)
        print("   5f: %s priced within %.2f%% of its bench; the wave 0 model "
              "puts its runs at %.0f%% of what they took"
              % (name, 100 * worst, 100 * under))
        if worst > 0.01:
            bad.append("%s: a VIDBENCH frame priced %.1f%% off what the "
                       "machine took" % (name, 100 * worst))
        if under > 0.9:
            bad.append("%s: the wave 0 model prices runs at %.0f%% of what "
                       "they took - the leg tests nothing" % (name,
                                                             100 * under))
    # --- 5g: THE FLIP SCHEDULE (98.2.1.1.1): a flipped file whose frames
    # may run over their period is replayed as the player runs it - a call
    # every half period off the card, one frame a call, a frame late when a
    # call finds a whole one more due - and no frame may be late. The
    # replay prices each frame as charge() does (its record, and the back
    # page brought up by the copy or the last record again), and nothing
    # of the encoder's own bookkeeping
    ftmp = tempfile.mkdtemp()
    noisy = os.path.join(ftmp, "noisy.mkv")
    subprocess.run(
        ["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
         "testsrc2=size=960x240:rate=24:duration=3", "-f", "lavfi", "-i",
         "sine=frequency=440:sample_rate=22050:duration=3", "-vf",
         "crop=320:240:'mod(n*24,640)':0,noise=alls=40:allf=t",
         "-c:v", "ffv1", "-c:a", "pcm_s16le", noisy], check=True)
    P = venc.PROFILES["486"]
    t, sub = venc.profile_table(P, vid.LAY_MODEX)
    kk = P["lcopy_us"] * vid.HZ / 1e6 * P["speed"]
    for sched in (True, False):
        out = os.path.join(ftmp, "f%d.V88" % sched)
        args = venc.parser().parse_args(
            [noisy, out, "--quiet", "--preset", "modex", "--profile", "486",
             "--flip", "--rate", "22050", "--avg", "9", "--peak", "14",
             "--fit", "fill"])
        if not sched:                   # broken on purpose: no schedule
            real = venc.EncoderX.begin
            venc.EncoderX.begin = lambda self: (real(self), setattr(
                self, "ceil", self.peak))
        try:
            venc.encode(args)
        finally:
            if not sched:
                venc.EncoderX.begin = real
        r = vid.Reader(out)
        q = vid.HZ / r.fps * P["speed"]
        aud = venc.CYC_AUDIO * r.abytes + venc.HOOK_CYC
        end, late, prev, over = 0.0, 0, None, 0
        for i, (rec, _, _) in enumerate(r.records()):
            c = vid.cycles_of(rec, True, table=t, sub=sub) + aud
            if prev is not None:
                y0, y1 = struct.unpack_from("<HH", prev, 2)
                rows = max(0, y1 - y0)
                c += rows * 80 * kk + venc.CYC_LCOPY0 \
                    if len(prev) > vid.PREV_MAX or \
                    rows * 80 * venc.VP_LCW < len(prev) else \
                    vid.cycles_of(prev, True, table=t, sub=sub)
            prev = rec
            start = math.ceil(max(i * q, end) / (q / 2) - 1e-9) * (q / 2)
            late += start - i * q >= q
            over += c > q
            end = start + c
        print("   5g: flipped at 90%% / 140%%%s: %d frames over their period, "
              "%d late in the player's schedule"
              % ("" if sched else ", NO schedule", over, late))
        if sched and late:
            bad.append("the flip schedule: %d frames late" % late)
        if not sched and not late:
            bad.append("the flip schedule: without it nothing is late - the "
                       "leg tests nothing")
    shutil.rmtree(ftmp, ignore_errors=True)
    # --- 6: ADPCM4's exact search
    rnd = np.random.default_rng(7)
    t = np.arange(22050) / 11025.0
    sig = 128 + 60 * np.sin(2 * np.pi * 440 * t) + 30 * np.sin(
        2 * np.pi * 1330 * t) + rnd.normal(0, 6, len(t))
    pcm = bytes(np.clip(sig, 0, 255).astype(np.uint8))
    zs = list(range(1470, len(pcm), 1470))
    ref = np.frombuffer(pcm, np.uint8).astype(float)

    def snr(d):
        dec = np.frombuffer(vid.adpcm4_decode(d), np.uint8).astype(float)
        return 10 * np.log10(((ref - ref.mean()) ** 2).mean() /
                             ((dec - ref) ** 2).mean())
    g = vid.adpcm4_encode(pcm, zeros=zs)
    one = vid.adpcm4_search(pcm, zeros=zs)
    par = vid.adpcm4_search(pcm, zeros=zs, jobs=4, seg=4410, lead=1024)
    cut = vid.adpcm4_search(pcm, zeros=zs, jobs=4, seg=4410, lead=0)
    miss = [name for name, d in (("search", one), ("stitched", par),
                                 ("no lead", cut))
            for st in [vid.adpcm4_trace(d)]
            if any(st[z // 2][1] for z in zs)]
    print("   ADPCM4: greedy %.2f dB, search %.2f, stitched on 4 cores "
          "%s, with no lead %.2f" % (snr(g), snr(one),
                                     "IDENTICAL" if par == one else
                                     "%.2f" % snr(par), snr(cut)))
    if snr(one) < snr(g) + 3:
        bad.append("the search is %.2f dB, the greedy encoder %.2f"
                   % (snr(one), snr(g)))
    if par != one:
        bad.append("the stitched search differs from the whole one")
    for name in miss:
        bad.append("%s: a keyframe's scale is not 0" % name)
    # --- 7: composite colour (CGACOMP)
    import os88cgacomp
    pal = os88cgacomp.palette().round().astype(int)
    classic = [(0, 0, 0), (0, 99, 25), (39, 40, 188), (17, 153, 222),
               (129, 13, 86), (112, 112, 112), (176, 56, 255),
               (155, 168, 255), (74, 73, 0), (63, 184, 0), (113, 113, 113),
               (98, 237, 133), (222, 87, 18), (212, 198, 29),
               (255, 130, 234), (255, 255, 255)]
    off = [n for n in range(16) if max(abs(int(pal[n][c]) - classic[n][c])
                                       for c in range(3)) > 1]
    print("   composite palette: %d of 16 nibbles off the model's own "
          "(MartyPC / 86Box, reenigne's)" % len(off))
    if off:
        bad.append("the composite palette moved: nibbles %s" % off)
    cd = venc.CompDitherer(64, 8, 0.5, 0)
    wrong = []
    for n in range(16):
        m = 15 - n                  # cells alternating n, m: a byte n:m
        field = np.zeros((8, 16, 3), np.uint8)
        field[:, 0::2] = pal[n]
        field[:, 1::2] = pal[m]
        got = cd(field)
        cd.prev = None
        if not np.all(got == (n << 4 | m)):
            wrong.append(n)
    print("   cells alternating each colour with its complement: %d come "
          "back as some other byte" % len(wrong))
    if wrong:
        bad.append("composite colours %s alternating with their "
                   "complements come back as other bytes" % wrong)
    # --- 8: composite by diffusion through the model: a picture RENDERED
    # from known nibbles must come back as (nearly all of) them
    rnd = np.random.default_rng(3)
    h, n = 16, 160
    nib = np.zeros((h, n), np.int64)
    for y in range(h):
        g = 0
        while g < n:
            run = int(rnd.integers(2, 7))
            nib[y, g:g + run] = rnd.integers(0, 16)
            g += run
    bits = ((nib[:, :, None] >> (3 - np.arange(4))) & 1).reshape(h, 640)
    out = venc.CompDiffuser(640, h, 0)(os88cgacomp.render(bits))
    back = np.stack((out >> 4, out & 15), 2).reshape(h, n)
    frac = float((back == nib).mean())
    print("   composite diffusion: %.1f%% of cells come back from their own "
          "rendering (want 90%% or more)" % (100 * frac))
    if frac < 0.90:
        bad.append("composite diffusion gives back %.1f%% of the cells "
                   "its picture was rendered from" % (100 * frac))
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
