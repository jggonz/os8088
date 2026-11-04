#!/usr/bin/env python3
"""vbtana - the analysis half of the Video Battle builder
(docs/plans/VBATTLE-BUILDER-PLAN.md 2, 3, 4).

Everything the builder RECOMMENDS or MEASURES, with no window and no build:

  screens(proj, source)    a source's frames as the engine will hold them:
                           encoded by os88venc for the project's target,
                           decoded back (os88vid), CGA4 indexes 0..3, one
                           array of (rows, 320) a frame. Cached by the
                           source's CONTENT and the target, so a second
                           look costs a file read
  sound(proj, sound)       a sound as floats at the battle's rate
  loop_candidates(frames)  the idle loop's best (start, end) pairs
  attach(frames, idle)     a clip's in-point, anchors and exit against the
                           idle loop, each with its score
  budget(proj, data)       per clip: bytes, KB/s against the profile's disk

A score is the fraction of pixels that differ: 0 is the same screen. It is
measured between DECODED screens, the ones the joins are made between, so
the encoder's dither is counted as it will be seen. Progress, for a window
or a JSON-lines build, goes to a callback: progress(stage, done, of, what).
"""

import hashlib
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import os88vid as V    # noqa: E402

ENC_KEY = 1         # bumped when the way a source is encoded here changes


def _nop(*a):
    pass


def cache_dir(proj):
    if proj.path:
        d = os.path.splitext(proj.path)[0] + ".cache"
    else:
        d = os.path.join(tempfile.gettempdir(), "vbtana-cache")
    os.makedirs(d, exist_ok=True)
    return d


def file_hash(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for blk in iter(lambda: f.read(1 << 20), b""):
            h.update(blk)
    return h.hexdigest()


def palette_args(b):
    """The CGA4 palette byte (98.1.3.3) as os88venc's three options"""
    st = 2 if b & 0x40 else (b >> 5) & 1
    return ["--cga-palette", str(st), "--cga-bright", str((b >> 4) & 1),
            "--cga-bg", str(b & 15)]


def _scene(name):
    import vbtscene as S
    if name == "idle":
        return S.clip_idle()
    if name == "title":
        return S.clip_title()[0]
    if name == "reveal":
        return S.clip_reveal()[0]
    for n, mk, _ in S.CLIPS:
        if n == name:
            return mk()[0]
    raise ValueError("no scene %s" % name)


def unpack(cv, h, wb):
    """A CGA4 canvas (bytes, rows x wb) as pixel indexes"""
    a = np.frombuffer(cv, np.uint8).reshape(h, wb)
    px = np.zeros((h, wb * 4), np.uint8)
    for k in range(4):
        px[:, k::4] = (a >> (6 - 2 * k)) & 3
    return px


def screens(proj, sid, progress=_nop):
    """Source SID's frames, as the decoder will show them"""
    s = proj.d["sources"][sid]
    if s["kind"] == "scene":
        return [np.asarray(f, np.uint8) for f in _scene(s["name"])]
    t = proj.d["target"]
    rows = t["canvas"][3]
    path = proj.here(s["path"])
    key = hashlib.sha1(json.dumps(
        [ENC_KEY, file_hash(path), rows, t["fps"], t["profile"],
         t["palette"]]).encode()).hexdigest()[:20]
    npy = os.path.join(cache_dir(proj), key + ".npy")
    if os.path.exists(npy):
        return list(np.load(npy))
    progress("encode", 0, 1, sid)
    v88 = os.path.join(cache_dir(proj), key + ".V88")
    subprocess.run([sys.executable, os.path.join(HERE, "os88venc.py"), path,
                    v88, "--quiet", "--preset", "cga4",
                    "--box", "320x%d" % rows, "--fit", "fill",
                    "--fps", str(t["fps"]), "--audio", "none",
                    "--profile", t["profile"]] + palette_args(t["palette"]),
                   check=True)
    r = V.Reader(v88)
    fr = []
    for n in range(r.frames):
        fr.append(unpack(V.decode_at(r, n), r.g.h, r.g.wb))
        progress("decode", n + 1, r.frames, sid)
    np.save(npy, np.stack(fr))
    os.remove(v88)
    return fr


def frames_of(proj, cid, progress=_nop):
    """Clip CID's frames: its source's, in..out"""
    c = proj.d["clips"][cid]
    return screens(proj, c["source"], progress)[c["in"]:c["out"] + 1]


def idle_of(proj, progress=_nop):
    i = proj.d["idle"]
    a, b = i["loop"]
    return screens(proj, i["source"], progress)[a:b]


def _ffmpeg(path, rate, start=None, end=None):
    cmd = ["ffmpeg", "-v", "error"]
    if start is not None:
        cmd += ["-ss", "%.4f" % start]
    cmd += ["-i", path]
    if end is not None:
        cmd += ["-t", "%.4f" % (end - (start or 0))]
    cmd += ["-ac", "1", "-ar", str(rate), "-f", "s16le", "-"]
    raw = subprocess.run(cmd, capture_output=True, check=True).stdout
    return np.frombuffer(raw, np.int16).astype(float) / 32768


def sound(proj, sid):
    """Sound SID as floats -1..1 at the battle's rate, its level applied"""
    s = proj.d["sounds"][sid]
    rate, fps = proj.d["target"]["rate"], proj.d["target"]["fps"]
    if s["kind"] == "scene":
        import vbtscene as S
        x = S.music(bars=s["bars"]) if s["name"] == "music" else \
            S.sfx(s["name"])
    elif s["kind"] == "file":
        x = _ffmpeg(proj.here(s["path"]), rate)
    else:
        src = proj.d["sources"][s["source"]]
        x = _ffmpeg(proj.here(src["path"]), rate, s["in"] / fps,
                    (s["out"] + 1) / fps)
    lv = s.get("level", 1.0)
    return x if lv == 1.0 else x * lv


# --- similarity ------------------------------------------------------------------

def diff(a, b):
    """The fraction of pixels that differ"""
    return float(np.mean(a != b))


def loop_candidates(frames, min_len=20, top=5):
    """The (score, start, end) pairs at least MIN_LEN apart whose frames are
    most alike: the loop plays start..end-1 and its seam takes end-1 onto
    start. Best first"""
    out = []
    n = len(frames)
    for a in range(n):
        for b in range(a + min_len, n):
            out.append((diff(frames[a], frames[b]), a, b))
    out.sort()
    return out[:top]


def attach(frames, idle, search=10, margin=1.5, slack=0.002, most=4):
    """A clip against the idle loop (4.3): its IN point (of its first
    SEARCH frames, the one nearest any idle frame), the ANCHORS (the idle
    frames within MARGIN of the best score for that in-point, local minima,
    at most MOST), and its OUT point and EXIT (of its last SEARCH frames,
    the pair nearest). Each with its score"""
    n = len(idle)
    s_in, i0 = min((diff(frames[i], idle[k]), i)
                   for i in range(min(search, len(frames))) for k in range(n))
    sc = [diff(frames[i0], f) for f in idle]
    lo = min(sc)
    anch = [k for k in range(n)
            if sc[k] <= lo * margin + slack
            and sc[k] <= sc[k - 1] and sc[k] <= sc[(k + 1) % n]][:most]
    s_out, j1, ex = min((diff(frames[j], idle[k]), j, k)
                        for j in range(max(0, len(frames) - search),
                                       len(frames)) for k in range(n))
    return dict(first=i0, last=j1, anchors=anch, entry_scores=sc,
                exit=ex, exit_score=s_out, in_score=s_in)


def wait_frames(anchors, n):
    """The longest a command waits for an anchor: the largest gap between
    them around the loop, in frames (125.3)"""
    if not anchors:
        return None
    a = sorted(anchors)
    return max(((a[(i + 1) % len(a)] - a[i]) % n) or n for i in range(len(a)))


# --- the budget -------------------------------------------------------------------

def disk_rate(profile):
    """The profile's disk, bytes a second (os88venc's PROFILES)"""
    import os88venc
    return os88venc.PROFILES[profile]["disk"]


def budget(proj, data):
    """Per clip of a built bundle: bytes, KB/s, and that against the
    profile's disk; and the bundle's resident parts"""
    import os88vbt as B
    b = B.Bundle(data)
    disk = disk_rate(proj.d["target"]["profile"])
    clips = []
    for c in b.clips:
        sec = c["nrec"] / b.fps
        rate = c["len"] / max(sec, 1e-6)
        clips.append(dict(name=c["name"], bytes=c["len"], secs=sec,
                          rate=rate, of_disk=rate / disk,
                          anchors=[k for k, _ in c["anch"]],
                          wait=wait_frames([k for k, _ in c["anch"]],
                                           b.nidle)))
    return dict(clips=clips, idle_bytes=len(b.vres), entries=len(b.ents),
                music=b.mlen, ui=len(b.uib), file=len(data), disk=disk)
