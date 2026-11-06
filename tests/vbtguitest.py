#!/usr/bin/env python3
"""The Video Battle builder's WINDOW without a window
(tools/os88vbtgui.py, docs/plans/completed/VBATTLE-BUILDER-PLAN.md wave 2).

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
     clip's own soundtrack follows it - or, a part of it (frames x to y),
     stays on its footage; a cue that stops is silent after it and still
     what is baked; a dropped file's kind and id; an ending has no exit; a cue cannot
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
sys.path.insert(0, HERE)
import numpy as np      # noqa: E402
import os88vid as V     # noqa: E402
import os88vbtgui as G  # noqa: E402
import vbtana as A      # noqa: E402
import vbtproj as P     # noqa: E402

fails = []


def shutil_which(x):
    import shutil
    return shutil.which(x)


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
ok(G.play_order((5, 40)) == list(range(5, 40)),
   "Play the loop plays it whole, start to end-1, as the engine does")
ok(G.play_order((1, 31), False, 3, 2) == [28, 29, 30, 1, 2, 3, None, None],
   "...and across the seam holds black before it goes round, so its own "
   "restart is not a seam")

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
        ents = {k: len(bd.entry(bc, o)) for k, o in bc["anch"]}
        ok(all((rp["entry_bytes"][k] + 15) & ~15 == n    # (each on a
               for k, n in ents.items()),                # paragraph)
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
    c = pr.d["clips"]["ari_bolt"]
    pr.use_own_sound("ari_bolt", 1.0, 2, 5)
    own = pr.d["sounds"][P.own_sound("ari_bolt")]
    mine = [q for q in c["cues"] if q["sound"] == P.own_sound("ari_bolt")]
    ok((own["in"], own["out"]) == (c["in"] + 2, c["in"] + 5) and
       [q["frame"] for q in mine] == [2],
       "its own soundtrack from frame 2 to 5: that footage's sound, one cue "
       "at frame 2")
    at = own["in"]
    pr.set_clip_range("ari_bolt", c["in"] + 1, c["out"])
    c = pr.d["clips"]["ari_bolt"]
    mine = [q for q in c["cues"] if q["sound"] == P.own_sound("ari_bolt")]
    ok(own["in"] == at and mine[0]["frame"] == at - c["in"],
       "...and a new in point leaves it on that footage (cue at %d)"
       % mine[0]["frame"])
    # a cue that STOPS: what Listen plays is still what the build bakes
    q0 = pr.d["clips"]["bram_heal"]["cues"][0]
    pr.set_cue_until("bram_heal", 0, q0["frame"] + 3)
    try:
        pr.set_cue_until("bram_heal", 0, q0["frame"])
        ok(False, "a cue cannot stop where it starts")
    except ValueError:
        ok(True, "a cue cannot stop where it starts")
    bd = B2.Bundle(B2.build_project(pr))
    snd = B2.frame_sound(A.clip_sound(pr, "bram_heal"),
                         pr.clip_frames("bram_heal"), spf, False)
    bc = next(c for c in bd.clips if c["name"] == "BRAM_HEA")
    recs = bd.stream(bc)
    baked = [r[struct.unpack_from("<HH", r)[0] -
               struct.unpack_from("<HH", r)[1]:] for r in recs]
    tail = A.clip_sound(pr, "bram_heal")[(q0["frame"] + 3) * spf:]
    ok(baked[:len(snd)] == snd and not np.any(tail),
       "a cue stopped at frame %d: silent after it, and Listen is the baked "
       "sound" % (q0["frame"] + 3))
    ok([G.drop_kind(x) for x in ("a.VBP", "b.mp4", "c.wav", "d.png", "e.txt")]
       == ["project", "video", "sound", "picture", None],
       "a dropped file is a project, a video, a sound, a picture or nothing")
    ok(G.unique_id("my clip!", {"my_clip_"}) == "my_clip_2" and
       G.unique_id("x" * 20, {}) == "x" * 16 and
       G.unique_id("x" * 20, {"x" * 16}) == "x" * 15 + "2",
       "a dropped file's id: cut, cleaned, numbered against what is taken")
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


    # 7 (wave 5): the openings, the join, the budget, the memory, a disk
    rate = 11025
    rng = np.random.RandomState(3)
    env = np.repeat(rng.uniform(0.02, 0.9, 200), rate // 8)
    mus = np.sin(np.arange(len(env)) * 0.3) * env
    J = rate * 9 + 1234
    op = np.concatenate([np.zeros(rate), mus[J - 3 * rate:J]])
    js = A.join_candidates(op, mus, rate)
    ok(abs(js[0][0] - J) <= 256 and js[0][1] > 0.9 and js[1][1] < js[0][1] - 0.2,
       "the join: openings ending on the music before sample %d are joined "
       "at %d (score %.2f, the next %.2f)" % (J, js[0][0], js[0][1], js[1][1]))
    js = A.join_candidates(np.concatenate([op, np.zeros(3 * rate)]), mus, rate)
    ok(js == [], "...and openings that end in silence suggest nothing")
    pr = P.Project.load(vbp)
    x, at = A.openings_sound(pr)
    spf = pr.d["target"]["rate"] // pr.d["target"]["fps"]
    nf = [pr.clip_frames(o["clip"]) for o in pr.d["openings"]]
    starts = [sum(nf[:i]) * spf for i in range(len(nf))]
    ok(len(x) == sum(nf) * spf and [a for a, _ in at] == starts,
       "the openings' sound is each one's frames long, one after another "
       "(%d samples)" % len(x))
    pr.add_opening("title")
    pr.move_opening(1, -1)
    ok([o["clip"] for o in pr.d["openings"]] == ["title", "reveal"] and
       G.opening_lines(pr)[1].endswith("joins idle frame %d" %
                                       pr.d["clips"]["reveal"]["exit"]),
       "an opening moved earlier; the last is the scene's, and says where it "
       "joins")
    for _ in range(6):
        pr.add_opening("reveal")
    try:
        pr.add_opening("title")
        ok(False, "a ninth opening is refused")
    except ValueError:
        ok(True, "a ninth opening is refused (the engine's VB_MAXOPEN)")
    ok([G.check_tab(w) for w in ("clips.x", "openings.1", "music", "enemy",
                                 "characters.0")] ==
       ["clips", "openings", "battle", "enemy", "party"],
       "a check's place sends the window to its tab")
    # THE MEMORY: the engine's own arithmetic (vb_load, vb_rclaim) out of
    # VB_FREE, MEASURED on MartyPC's Hercules 5150 (VB_FREE_SRC): at format
    # 8 the entries ride in their clips, the band's buffers are the band's
    import vbtrulesdemo as D
    rdata, _ = D.bundle(D.make_project())
    mem = A.memory(rdata)
    ok(mem["resident"] == 72 * 1024 and mem["ring"] == 180 * 1024,
       "the rules battle: a 72 KB resident claim, rings of 180 KB (format "
       "8; it was 120 and 140 at format 7) (%s, %s)"
       % (mem["resident"], mem["ring"]))
    ok(A.memory(rdata, free_kb=200)["ring"] is None,
       "...and with 200 KB free, no ring of 48 KB: Not enough memory")
    pr = P.Project.load(vbp)
    data = B2.build_project(pr)
    pr.set(("target", "profile"), "floppy")
    rows = G.budget_rows(A.budget(pr, data))
    tags = dict((v[0], t) for v, t in rows)
    ok(rows[0][0][0] == pr.d["openings"][0]["clip"] and
       tags["ari_limit"] == "over" and
       any(v[0] == "the title (a loop)" for v, _ in rows),
       "the budget names clips as the project does - the title loop too - "
       "and the Limit is over a floppy's disk (%s)" % tags["ari_limit"])
    # A CLIP NOTHING PLAYS, ahead of the rest: not built, so not named -
    # it was, and every row after it carried the NEXT clip's name
    pu = P.Project.load(vbp)
    first = next(iter(pu.d["clips"]))
    pu.d["clips"] = dict([("unused", dict(pu.d["clips"][first]))] +
                         list(pu.d["clips"].items()))
    du = B2.build_project(pu)
    rows = G.budget_rows(A.budget(pu, du))
    bn = [c["name"].strip() for c in B2.Bundle(du).clips]
    ids = [v[0] for v, _ in rows]
    ok("unused" not in ids and len(ids) == len(bn) and all(
        i.endswith("(a loop)") or B2.name8(i).decode().strip() == n
        for i, n in zip(ids, bn)),
       "...and a clip nothing plays shifts no row's name (%s / %s)"
       % (ids[:4], bn[:4]))
    ok(B2.name8("aaaaaaaÉ") == b"AAAAAAAE" and
       B2.name8("Ünïcødé") == b"UNICDE  ",
       "a clip id in any letters is ASCII in the clip table - never a "
       "letter's UTF-8 cut at its eighth byte, which the bundle's own "
       "reader raised on")
    pu.add_source("gone", path="nowhere/gone.mp4")
    lv = [(x.level, x.where) for x in pu.check()]
    fsrc = pu.d["clips"][first]["source"]
    pu.set(("sources", fsrc), {"kind": "video", "path": "nowhere/also.mp4"})
    lv2 = [(x.level, x.where) for x in pu.check()]
    ok(("WARN", "sources.gone") in lv and ("ERROR", "sources.gone") not in lv
       and ("ERROR", "sources." + fsrc) in lv2,
       "a missing video nothing built uses is a WARN, one the build reads "
       "an ERROR (%s; %s)" % (lv, [x for x in lv2 if x[0] == "ERROR"]))
    open(os.path.join(tdir, "BATTLE.VBT"), "wb").write(data)
    try:
        B2.disk_plan(pr, 0)
        ok(False, "a bundle too big for a 360 KB floppy is refused")
    except ValueError as e:
        ok("360 KB" in str(e), "a bundle too big for a 360 KB floppy is "
           "refused before it is tried: %s" % e)
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools/os88vbt.py"),
                        "disk", vbp, "--disk", "3", "--json"],
                       capture_output=True, text=True)
    img = os.path.join(tdir, "BATTLE-1440.img")
    v = subprocess.run([sys.executable, os.path.join(ROOT, "tools/os88disk.py"),
                        "--verify", img], capture_output=True, text=True)
    ls = subprocess.run(["mdir", "-b", "-i", img, "::"], capture_output=True,
                        text=True).stdout.split()
    ok(r.returncode == 0 and v.returncode == 0 and
       sorted(os.path.basename(f) for f in ls) == ["BATTLE.VBT", "VBATTLE.O88"],
       "a 1.44 MB floppy of it: VBATTLE.O88 and BATTLE.VBT, verified (%s)" % ls)

    # 8: the title loop, the fade and the endings (SPEC.md 126.3.1)
    import re
    src = open(os.path.join(ROOT, "apps/vbattle/vbloop.inc")).read()
    tab = src[src.index("vb_fmasks:"):]
    eng = [[int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", ln)]
           for ln in tab.splitlines()[1:17]]
    ok(G.fade_masks() == eng, "the window's fade is the engine's: its 16 x 4 "
       "masks are vbloop.inc's VB_FMASK")
    px = np.full((8, 8), 3, np.uint8)
    ok(not G.fade_frame(px, 16, 16).any() and
       (G.fade_frame(px, 8, 16) == 0).sum() == 32 and
       G.fade_frame(px, 0, 16).all(),
       "a fade puts none, half, then all of the frame out")
    ok(G.spread_anchors([0.03] * 16, [0, 1, 2, 3]) == [0, 4, 8, 12],
       "anchors as good as each other are spread a quarter of the loop apart")
    pr = P.Project.load(vbp)
    pr.set_title("title", (10, 34), leave="reveal")
    ok(any(x.where == "title" for x in pr.errors()),
       "a title left by a clip with nowhere to enter it is an ERROR")
    pr.set_title("title", (10, 34), leave="reveal", anchors=(0, 8, 16))
    pr.set_ending("victory", "join", "victory", (0, 20))
    pr.set_ending("defeat", "fade", "defeat", (0, 12))
    ok(not pr.errors(), "a title left by REVEAL, victory joining and defeat "
       "fading to their loops: no errors (%s)" % pr.errors())
    data = B2.build_project(pr)         # (verify runs inside: every path)
    bd = B2.Bundle(data)
    tl = bd.clips[bd.title]
    ok(bd.title != 0xFF and bd.leave == 0 and tl["flags"] & B2.CF_LOOP and
       tl["nrec"] == 25 and bd.clips[0]["flags"] & B2.CF_ENTER and
       len(bd.clips[0]["anch"]) == 3 and bd.endings["victory"][0] == 2 and
       bd.endings["defeat"][0] == 1,
       "it builds: the title a loop of 24 frames and its seam, opening 0 "
       "entered at its 3 anchors, the endings' ways and loops in the header")
    bad = bytearray(data)               # a lap that does not come round:
    rs = bd.stream(tl)                  # its SEAM misses frame 0
    s0 = tl["off"] + sum(len(r) for r in rs[:-1])
    bad[s0 + len(rs[-1]) // 2] ^= 0x55
    try:
        B2.verify(bytes(bad), quiet=True)
        ok(False, "a title whose lap is broken is refused")
    except ValueError as e:
        ok(any(w in str(e) for w in ("loop", "title", "join", "lists end")),
           "a title whose lap is broken is refused: %s" % e)
    pr.set_ending("victory", "join", None)
    ok(any(x.where == "endings.victory" for x in pr.errors()),
       "joining an end loop that is not there is an ERROR")

    # 9: the RENDITIONS (VBATTLE-MEMORY-PLAN 9): VGA4 and Hercules
    pr = P.Project.load(vbp)
    for kind in ("vga4", "herc"):
        d9 = B2.build_project(pr, kind=kind)    # (verify inside: every path)
        b9 = B2.Bundle(d9)
        ok(b9.rend.kind == kind and d9[337] == {"vga4": 1, "herc": 2}[kind],
           "the demo builds for %s and every path verifies (%d KB)"
           % (kind, len(d9) // 1024))
    B2.REND = B2.Rend("cga4")
    pal = A.battle_palette(pr, 8)
    band4 = [tuple(c) for c in A.palette_rgb(pr.d["target"]["palette"])]
    bi = A.band_index(pr, 8)
    pr.set(("target", "vga_band_cga"), True)
    kept = [tuple(c) for c in A.battle_palette(pr, 8)]
    pr.set(("target", "vga_band_cga"), False)
    ok(len(pal) == 8 and len(set(bi)) == 4 and all(c in kept for c in band4),
       "VGA's eight chosen from the footage, the band mapped to four DIFFERENT "
       "of them; target.vga_band_cga keeps the band's CGA four exactly")
    dv = B2.build_project(pr, kind="vga4")      # FORMAT 9 (126.3.2): the
    codes = B2.Bundle(dv).codes                 # sixteen at 464 by pixel
    p16 = [tuple(dv[464 + 3 * i:467 + 3 * i]) for i in range(16)]  # value,
    ok(dv[4] == 10 and all(p16[codes[i]] == tuple(v >> 2 for v in c)  # the
                           for i, c in enumerate(pal)) and         # band map
       list(dv[352:356]) == [codes[j] for j in A.band_index(pr, 8)] and
       len(set(dv[352:356])) == 4 and tuple(dv[349:351]) == (8, 0),
       "format 10: VGA's palette at 464 by plane code, the band's map at 352 "
       "- four DIFFERENT values - and K and a clip's own at 349 (8, none by "
       "default)")
    # THE VALUES AGAINST THE TEARING (vga_order): two browns a pixel moves
    # between, a cyan and black - in Gray order the browns are two planes
    # apart, and a half-written pixel is black or cyan; chosen, one plane
    fr = [np.full((4, 8), 1 + 2 * (i % 2), np.uint8) for i in range(8)]
    po4 = [(0, 0, 0), (120, 80, 40), (0, 255, 255), (130, 85, 45)]
    g4 = B2.vga_codes(4)
    c4 = B2.vga_order([(fr, po4)], g4)
    gp = B2.plane_groups(g4)

    def stores(a, b):
        return sum(1 for g in gp if (a ^ b) & g)
    ok(gp == [3, 12] and stores(g4[1], g4[3]) == 2 and
       stores(c4[1], c4[3]) == 1 and c4[0] == 0 and
       sorted(c4) == sorted(g4) and sorted(codes) == sorted(
           B2.vga_codes(8)) and codes[0] == 0,
       "VGA's values against the tearing: two browns a pixel moves between "
       "one store apart (%s; Gray order %s, two), black at 0, the values a "
       "permutation" % (c4, g4))
    # THE BUILD TAB'S PALETTE VIEWER (126.3.2): the K colours in index
    # order, the band's four marked, and a clip's share of each - read by
    # decoding the clip as the engine does, so a clip drawn with several
    # colours shows several
    vfile = os.path.join(os.path.dirname(vbp), "pv.vbt")
    open(vfile, "wb").write(dv)
    bv = B2.Bundle(dv)
    pv = max((G.vga_colours(vfile, 8, i) for i in range(len(bv.clips))),
             key=lambda v: sum(u > 0.01 for u in v["usage"]))
    B2.REND = B2.Rend("cga4")
    us = pv["usage"] or [0]
    ok([x["rgb"] for x in pv["cols"]] == [tuple(c) for c in pal] and
       sorted(x["band"] for x in pv["cols"] if x["band"] is not None)
       == [0, 1, 2, 3] and abs(sum(us) - 1) < 1e-6 and
       sum(u > 0.01 for u in us) >= 3,
       "the palette viewer: the 8 in order, the band's four marked, %s's "
       "pixels over %d of them (%s)"
       % (pv["clip"], sum(u > 0.01 for u in us), " ".join("%.0f" % (u * 100) for u in us)))
    B2.REND = B2.Rend("cga4")
    fr = np.zeros((3, 8, 16, 3), np.uint8)
    fr[:] = 120
    fr[2, 0, 0] = (124, 118, 121)       # a pixel that barely moved
    q = A.quant_vga(fr, [(0, 0, 0), (255, 255, 255), (120, 120, 120),
                         (60, 60, 60)])
    ok((q[1] == q[0]).all() and (q[2] == q[1]).all(),
       "a still picture's colours are HELD frame to frame (no dither crawl)")
    hq = A.quant_herc(fr)
    ok((hq[2] == hq[0]).all(), "...and its bits, on Hercules")
    # A FADE TO BLACK ends black (target.clip, the owner's "videos that fade
    # completely to black, but are leaving smears"): a slow fade, each step
    # under the hold, left dots lit for good; within the clip they are
    # drawn solid and never held
    fade = np.zeros((40, 8, 16, 3), np.uint8)
    for i in range(40):                 # (dark, a dot lit in it, fading to
        fade[i] = max(0, 9 - i // 4)    # black by less than the hold)
    lit0 = int(A.quant_herc(fade)[-1].sum())
    lit16 = int(A.quant_herc(fade, 16)[-1].sum())
    vf = np.zeros((40, 8, 16, 3), np.uint8)
    for i in range(40):                 # (VGA: a grey slot of 40, fading)
        vf[i] = max(0, 26 - i)
    vpal = [(0, 0, 0), (255, 255, 255), (40, 40, 40), (120, 120, 120)]
    v0 = int(A.quant_vga(vf, vpal)[-1].astype(bool).sum())
    vq = A.quant_vga(vf, vpal, 16)
    ok(lit0 > 0 and lit16 == 0 and not vq[-1].any(),
       "a slow fade to black: Hercules %d dots left lit unclipped, %d "
       "clipped at 16; VGA all black clipped (%d unclipped - Knoll's hold "
       "is relative, so it leaves none either)" % (lit0, lit16, v0))
    # VGA DITHERS FOR COLOUR (126.3.2, the owner's "red things choose to be
    # brown"): a pink between red and white in a palette that also holds
    # yellow - a brightness-only dither mixed red with YELLOW (mean error
    # 74); Video Player's Knoll pattern dither mixes it to the source's mean
    hpal = [(0, 0, 0), (255, 255, 255), (200, 40, 40), (40, 40, 200),
            (130, 90, 40), (210, 200, 50), (100, 100, 100), (60, 140, 60)]
    pk = np.zeros((1, 16, 16, 3), np.uint8)
    pk[:] = (230, 120, 120)
    pq = A.quant_vga(pk, hpal)[0]
    perr = np.abs(np.array(hpal, float)[pq].mean((0, 1)) -
                  (230, 120, 120)).max()
    ok(perr < 25,
       "a pink dithers to pink, mostly red and white: mean error %.0f "
       "(%d of 256 yellow)" % (perr, int((pq == 5).sum())))
    # VGA'S COLOURS BY ERROR, AND EACH CLIP'S OWN (126.3.2, steps 2 and 3):
    # two streams, each grey with a colour no other has - with one colour
    # of its own each gets its colour; shared alone, one of them goes
    # without. Black and white are always shared
    rs_ = np.random.RandomState(3)
    grey = rs_.randint(40, 200, (3000, 1)).repeat(3, 1)
    orange = np.tile([[250, 120, 10]], (1500, 1))
    teal = np.tile([[10, 160, 150]], (1500, 1))
    st = {"a": (np.vstack([grey, orange]), 1.0),
          "b": (np.vstack([grey, teal]), 1.0)}
    fx = [(0, 0, 0), (255, 255, 255)]
    sh1, own1 = A.plan_colours(st, 4, 1, fx)
    sh0, _ = A.plan_colours(st, 4, 0, fx)

    def err(pal, px):
        d = ((np.asarray(px, float)[:, None] - np.asarray(pal, float)[None])
             ** 2).sum(2).min(1)
        return float(d.mean())
    e1 = [err(sh1 + own1[x], st[x][0]) for x in "ab"]
    e0 = [err(sh0, st[x][0]) for x in "ab"]
    ok(sh1[0] == (0, 0, 0) and (255, 255, 255) in sh1 and
       all(x < 0.7 * y for x, y in zip(e1, e0)),
       "VGA's colours by error: a colour of each clip's own cuts its error "
       "(%.0f, %.0f against %.0f, %.0f shared alone - the shared chosen "
       "first gave one clip's colour to all, and b 7235); black and white "
       "shared" % tuple(e1 + e0))
    if shutil_which("ffmpeg"):
        tv = os.path.join(os.path.dirname(vbp), "tsrc.mp4")
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi",
                        "-i", "testsrc2=size=320x240:rate=12", "-t", "3",
                        "-pix_fmt", "yuv420p", tv], check=True)
        po = P.Project.load(vbp)
        po.add_source("tsrc", path=tv)
        cl = po.d["clips"]["ari_attack"]
        cl["source"], cl["in"], cl["out"] = "tsrc", 0, 20
        po.set(("target", "vga_colours"), 16)
        po.set(("target", "vga_clip_colours"), 4)   # (asked for: 126.3.2)
        po.save()
        B2.REND = B2.Rend("cga4")
        d16 = B2.build_project(po, kind="vga4")     # (verify inside)
        b16 = B2.Bundle(d16)
        ci = [c["name"] for c in b16.clips].index("ARI_ATTA")
        cc_ = b16.clips[ci]
        codes = b16.codes
        rs2 = b16.stream(cc_)

        def loaded(r):
            """The four own slots' colours once record R is played"""
            B2.REND.dac = [(1, 2, 3)] * 16
            B2.REND.dac_check = False
            try:
                B2.decode(B2.REND.blank(), r)
            finally:
                B2.REND.dac_check = True
            return [tuple(B2.REND.dac[v]) for v in codes[12:]]
        e0 = b16.entry(cc_, cc_["anch"][0][1])
        n_, al_ = struct.unpack_from("<HH", e0, 0)
        own_e = loaded(e0)
        seam = rs2[-1]
        ns_, as_ = struct.unpack_from("<HH", seam, 0)
        ok(tuple(d16[349:351]) == (16, 4) and len(cc_["own"]) == 4 and
           own_e == cc_["own"] and e0[4] == 0x84 and
           not any(seam[4 + j] & 0x80 for j in (0,)) and
           not A.vga_plan(po)["own"].get("idle"),
           "a clip's own four: its entries load them FIRST, before the "
           "picture - a cut, as Video Player loads a scene's (%s); the seam "
           "back loads nothing, the idle loop using the shared alone"
           % (own_e,))
        # ...and a clip drawn without its load is refused: its entries'
        # loads taken out, its frames use own slots under the loop's colours
        keep_wd = B2.with_dac
        B2.with_dac = lambda r, *loads: r
        try:
            B2.REND = B2.Rend("cga4")
            B2.build_project(po, kind="vga4")
            ok(False, "a clip drawn in its own colours with no load of them "
               "is refused")
        except ValueError as e:
            ok("own colours" in str(e), "a clip drawn in its own colours "
               "with no load of them is refused: %s" % e)
        finally:
            B2.with_dac = keep_wd
            B2.REND = B2.Rend("cga4")
    # HOW A VIDEO FITS (target.fit): a 4:3 source with a bar along its top
    # edge - FILL crops it on every rendition (each canvas is wider than
    # 4:3 shows), STRETCH keeps it
    import shutil
    if shutil.which("ffmpeg"):
        ev = os.path.join(os.path.dirname(vbp), "edge.mp4")
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi",
                        "-i", "color=black:size=640x480:rate=15,drawbox=x=0:"
                        "y=0:w=640:h=24:color=white:t=fill", "-t", "1", ev],
                       check=True)
        pf = P.Project.load(vbp)
        pf.add_source("edge", path=ev)
        tops = {}
        for fit in ("fill", "stretch"):
            pf.d["target"]["fit"] = None if fit == "fill" else fit
            h = A.rend_screens(pf, "edge", "herc")[0]
            v = A.rend_screens(pf, "edge", "vga4")[0]
            c = A.screens(pf, "edge")[0]
            tops[fit] = (int(h[:4].sum()), int((v[:3] != v[-1, 0]).sum()),
                         int((c[:3] != c[-1, 0]).sum()))
        ok(tops["fill"] == (0, 0, 0) and min(tops["stretch"]) > 0,
           "a 4:3 video's top edge: cropped by FILL %s, kept by STRETCH %s "
           "(Hercules, VGA, CGA)" % (tops["fill"], tops["stretch"]))
    full = np.zeros((200, 320), np.uint8)
    full[141, :] = 3
    full[142, :] = 2
    hf = np.vstack([A.herc_of_cga(full[:142], 232),
                    A.herc_of_cga(full[142:], 116)])
    ok(hf.shape == (348, 640) and hf[231].all() and hf[232].sum() == 320 and
       hf[233].sum() == 320 and not hf[234:].any(),
       "a whole CGA screen on Hercules: its canvas onto the canvas, its band "
       "two for one onto the band")


if fails:
    print("%d FAILED" % len(fails))
    sys.exit(1)
print("all passed")
