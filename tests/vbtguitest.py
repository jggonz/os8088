#!/usr/bin/env python3
"""The Video Battle builder's WINDOW without a window
(tools/os88vbtgui.py, docs/plans/VBATTLE-BUILDER-PLAN.md wave 2).

    python3 tests/vbtguitest.py

The window is a view; what it decides and runs is reached here with no Tk:
  1. THE PALETTE: palette_parts and palette_byte are inverses over every
     CGA4 byte there is, and the parts are the ones os88vid's cga4_colours
     reads (a swatch that showed one set while the encode used another
     would pass every other check). The marking colour is never one of
     the palette's four.
  2. THE SIZES: a canvas of N rows leaves the band the rest of 200, even
     rows, inside the screen.
  3. THE SEAM'S PLAY ORDER runs the loop's last frames into its first.
  4. THE MUSIC'S LOOP: a sound made of one random passage repeated is found
     to loop at exactly that passage's length (to the sample), and its
     score is 1. A loop set in the model moves a join that falls outside
     it inside, and the BUILD honours it: the bundle's music is the loop,
     the join counted from its start.
  6. THE CLIPS TAB (wave 3): what Listen plays is what the build bakes,
     sample for sample (clip_sound against the bundle's records); the
     filmstrip's entry bytes are the bundle's entries' own lengths; a new
     range carries the cues with it and drops those off its end, and the
     clip's own soundtrack follows it; an ending has no exit; a cue cannot
     leave its clip; a clip in use cannot be deleted; and the timeline's
     place and frame are inverses.
  5. THE JOBS: a vbtana process answers as the in-process call does; a
     WAV of a loop's jump is as long as asked; and CANCEL kills an encode
     and everything it started, and leaves no cache that loads short.
"""
import os
import signal
import struct
import subprocess
import sys
import tempfile
import time
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import numpy as np      # noqa: E402
import os88vid as V     # noqa: E402
import os88vbtgui as G  # noqa: E402
import vbtana as A      # noqa: E402
import vbtproj as P     # noqa: E402

fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


# 1
good, rt = 0, True
for b in range(0x80):
    if b & 0x40 and b & 0x20:
        continue
    st, br, bg = G.palette_parts(b)
    rt &= G.palette_byte(st, br, bg) == b
    cs = V.cga4_colours(b)
    want = [bg] + [c + (8 if br else 0) for c in V.CGA4_SETS[st]]
    good += cs == want
    m = A.mark_colour(b)
    rt &= m not in A.palette_rgb(b)
ok(rt, "palette_parts and palette_byte are inverses, the mark never a "
   "palette colour")
ok(good == 96, "the parts are cga4_colours' own over all 96 bytes (%d)" % good)

# 2
for n in (142, 100, 141, 0, 300):
    cv, band = G.sizes_for(n)
    ok(cv[3] % 2 == 0 and 8 <= cv[3] <= 192 and band[1] == cv[3] and
       cv[3] + band[3] == 200, "sizes_for(%d): video %d rows, band %d" %
       (n, cv[3], band[3]))

# 3
ok(G.seam_order((1, 31), 3) == [28, 29, 30, 1, 2, 3],
   "the seam plays the loop's last frames into its first")

# 4
rate = 11025
rng = np.random.RandomState(1)
L = int(3.37 * rate)
t = np.arange(L) / rate
x = 0.3 * np.sin(2 * np.pi * 220 * t) * (1 + np.sin(2 * np.pi * 2 * t))
x += 0.05 * rng.randn(L)
pre = 0.2 * rng.randn(int(0.71 * rate))
snd = np.concatenate([pre, x, x, x, x])
cands = A.music_loops(snd, rate, min_secs=2.0)
best = cands[0]
ok(best[0] > 0.99 and (best[2] - best[1]) % L == 0 and best[1] >= len(pre)
   - A.HOP, "a passage repeated loops at its own length to the sample "
   "(%d, %d samples long; the passage %d)" % (best[1], best[2] - best[1], L))

with tempfile.TemporaryDirectory() as tdir:
    vbp = os.path.join(tdir, "demo.vbp")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88vbt.py"),
                    "demo-project", vbp], check=True, stdout=subprocess.DEVNULL)
    pr = P.Project.load(vbp)
    pr.set(("music", "join"), 1000)
    pr.set_music_loop(4410, 4410 + 11025 * 6)
    ok(pr.d["music"]["join"] == 4410, "a join outside the new loop moves to "
       "its start")
    pr.set(("music", "join"), 9000)
    pr.save()
    import os88vbt as B
    data = B.build_project(P.Project.load(vbp))
    bd = B.Bundle(data)
    ok(bd.mlen == 11025 * 6 and bd.music_at == 9000 - 4410,
       "the bundle's music is the loop (%d samples), the join from its start "
       "(%d)" % (bd.mlen, bd.music_at))
    ok(not pr.errors(), "the project with its loop has no errors")
    pr.set(("music", "join"), 99)
    ok(any(p.where == "music" for p in pr.errors()),
       "a join outside the loop is an ERROR")

    # 6
    import os88vbt as B2
    pr = P.Project.load(vbp)
    bd = B2.Bundle(B2.build_project(pr))
    spf = pr.d["target"]["rate"] // pr.d["target"]["fps"]
    for cid in ("ari_attack", "ari_limit", "bram_heal"):
        snd = B2.frame_sound(A.clip_sound(pr, cid), pr.clip_frames(cid), spf,
                             False)
        bc = next(c for c in bd.clips if c["name"] == cid.upper()[:8])
        recs = bd.stream(bc)
        baked = [r[struct.unpack_from("<HH", r)[0] -
                   struct.unpack_from("<HH", r)[1]:] for r in recs]
        ok(baked[:len(snd)] == snd, "%s: Listen is the baked sound, frame "
           "for frame" % cid)
        rp = A.clip_report(pr, cid)
        ents = {k: struct.unpack_from("<H", bd.ents, o * 16)[0]
                for k, o in bc["anch"]}
        ok(all(rp["entry_bytes"][k] == n for k, n in ents.items()),
           "%s: the filmstrip's entry bytes are the bundle's (%s)" %
           (cid, sorted(ents.items())[:2]))
    ok(rp["wait"] == 6, "four anchors 6 apart wait 6 frames (%s)" % rp["wait"])
    c = pr.d["clips"]["ari_bolt"]
    pr.add_cue("ari_bolt", c["out"] - c["in"], "zap")
    n0 = len(c["cues"])
    pr.set_clip_range("ari_bolt", c["in"] + 2, c["out"] - 1)
    c = pr.d["clips"]["ari_bolt"]
    ok(len(c["cues"]) < n0 and all(0 <= q["frame"] <= c["out"] - c["in"]
                                   for q in c["cues"]),
       "a new range moves the cues and drops the one off its end")
    pr.use_own_sound("ari_bolt")
    pr.set_clip_range("ari_bolt", c["in"] + 1, c["out"])
    own = pr.d["sounds"][P.own_sound("ari_bolt")]
    ok((own["in"], own["out"]) == (c["in"], c["out"]),
       "the clip's own soundtrack follows its range")
    pr.set_ends("ari_bolt", True)
    ok(pr.d["clips"]["ari_bolt"]["joins"] is False, "an ending does not join")
    try:
        pr.move_cue("ari_bolt", 0, 999)
        ok(False, "a cue cannot leave its clip")
    except ValueError:
        ok(True, "a cue cannot leave its clip")
    try:
        pr.remove_clip("ari_attack")
        ok(False, "a clip in use cannot be deleted")
    except ValueError:
        ok(True, "a clip in use cannot be deleted")
    ok(all(G.x_cue(G.cue_x(f, 37, 640), 37, 640) == f for f in range(37)),
       "the timeline's place and frame are inverses")

    # 5
    r = G.Job("vbtana", ["loops", vbp, "idle", "--min", "12"]).run()
    mine = A.loop_candidates(A.screens(pr, "idle"), 12)
    ok([(c["start"], c["end"]) for c in r["loops"]] ==
       [(a, b) for _, a, b in mine], "a vbtana job answers as the call does")
    wav = os.path.join(tdir, "jump.wav")
    G.Job("vbtana", ["wav", vbp, "music", wav, "--loop", "11025",
                     str(11025 * 8)]).run()
    with wave.open(wav) as w:
        n = w.getnframes()
    ok(n == 2 * 3 * 11025, "the jump's WAV is the 3 s before and after "
       "(%d samples)" % n)

    # a source to encode, long enough to be cancelled in the middle
    have_ff = subprocess.run(["which", "ffmpeg"],
                             capture_output=True).returncode == 0
    if not have_ff:
        print("SKIP the cancel: no ffmpeg")
    else:
        mp4 = os.path.join(tdir, "long.mp4")
        subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
                        "testsrc=size=640x400:rate=15:duration=40",
                        "-pix_fmt", "yuv420p", mp4], check=True)
        pr.add_source("long", path=mp4)
        pr.save()
        j = G.Job("vbtana", ["screens", vbp, "long"]).start()
        time.sleep(4)
        pid = j.p.pid
        j.cancel()
        ok(j.p.poll() is not None, "Cancel ends the job")
        for _ in range(50):     # (a pool's workers take a moment to go)
            out = subprocess.run(["ps", "-o", "pid=,stat=", "-s", str(pid)],
                                 capture_output=True, text=True).stdout
            left = [ln.split()[0] for ln in out.splitlines()
                    if ln.split() and not ln.split()[1].startswith("Z")]
            if not left:
                break
            time.sleep(0.1)
        ok(not left, "...and everything it started (%s left)" % left)
        cache = A.cache_dir(pr)
        bad = []
        for f in os.listdir(cache):
            if f.endswith(".npy"):
                try:
                    np.load(os.path.join(cache, f))
                except Exception:
                    bad.append(f)
        ok(not bad, "no cache that loads short (%s)" % bad)


if fails:
    print("%d FAILED" % len(fails))
    sys.exit(1)
print("all passed")
