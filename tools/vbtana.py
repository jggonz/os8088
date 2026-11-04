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
    tmp = npy + ".part.npy"         # whole or not at all: a Cancel that
    np.save(tmp, np.stack(fr))      # kills this mid-write leaves no
    os.replace(tmp, npy)            # cache that loads short
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


# --- colour, for every picture the builder shows ----------------------------------

def palette_rgb(b):
    """CGA4 palette byte B's four colours as RGB triples (98.1.3.3)"""
    std = V.STD16
    return [tuple(int(std[3 * c + i]) * 255 // 63 for i in range(3))
            for c in V.cga4_colours(b)]


def to_rgb(px, b):
    """Pixel indexes 0..3 as an RGB array in palette B"""
    return np.array(palette_rgb(b), np.uint8)[px]


MARKS = ((255, 255, 0), (255, 0, 255), (0, 255, 0), (255, 128, 0),
         (0, 160, 255))


def mark_colour(pal):
    """The marking colour furthest from all four of palette PAL's, so that a
    marked pixel is never mistaken for a drawn one"""
    cs = palette_rgb(pal)
    return max(MARKS, key=lambda m: min(sum((a - b) ** 2 for a, b in zip(m, c))
                                        for c in cs))


def diff_rgb(a, b, pal):
    """Frame A with the pixels where B differs painted in the marking
    colour, the rest dimmed: the DIFFERENCE view (4.2, 4.3)"""
    rgb = to_rgb(a, pal) // 3
    rgb[a != b] = mark_colour(pal)
    return rgb


# --- the idle loop's cost ------------------------------------------------------

def idle_cost(proj, loop=None, progress=_nop):
    """The loop LOOP (default the project's) as the engine will hold it:
    each record's bytes, the seam's (record 0, its last frame onto its
    first) and the whole loop's"""
    import os88vbt as B
    i = proj.d["idle"]
    a, b = loop or i["loop"]
    fr = screens(proj, i["source"], progress)[a:b]
    rows = proj.d["target"]["canvas"][3]
    s = [B.surface_of(f, rows) for f in fr]
    sizes = []
    for k in range(len(s)):
        sizes.append(len(B.rec(s[k - 1], s[k], rows)))
        progress("cost", k + 1, len(s), "idle")
    return dict(frames=len(s), seam=sizes[0], records=sizes,
                total=sum(sizes), largest=max(sizes))


# --- the music's loop (VBATTLE-BUILDER-PLAN 10.2) -----------------------------------

HOP = 512


def features(x, rate, hop=HOP, bands=24):
    """The sound as a sequence of short spectra: log energy in BANDS bands,
    one row a HOP samples, each row scaled to unit length so that a loud
    and a quiet copy of a passage still match"""
    n = 2048
    if len(x) < n + hop:
        return np.zeros((0, bands))
    w = np.hanning(n)
    frames = np.lib.stride_tricks.sliding_window_view(x, n)[::hop] * w
    spec = np.abs(np.fft.rfft(frames, axis=1))
    edges = np.unique(np.geomspace(2, spec.shape[1] - 1, bands + 1).astype(int))
    f = np.stack([spec[:, edges[i]:edges[i + 1]].sum(1)
                  for i in range(len(edges) - 1)], 1)
    f = np.log1p(f)
    f -= f.mean(1, keepdims=True)
    nrm = np.linalg.norm(f, axis=1, keepdims=True)
    return f / np.maximum(nrm, 1e-9)


def music_loops(x, rate, min_secs=4.0, window_secs=2.0, top=5, hop=HOP):
    """The music's best LOOPS: (score, start, end) in samples, the loop
    playing start..end-1 and then start again. A candidate is good when
    the WINDOW_SECS that follow its end sound like the WINDOW_SECS that
    follow its start, so the jump is not heard. Coarse on the spectra,
    then each end placed to the sample by the waveforms' own correlation
    around it. Score 1 is the same passage"""
    f = features(x, rate, hop)
    m = len(f)
    win = max(1, int(window_secs * rate / hop))
    lag0 = int(min_secs * rate / hop)
    if m < lag0 + win + 1:
        return []
    out = []
    # for every lag, the similarity of the window at a and at a + lag, by a
    # running sum along the diagonal
    for lag in range(lag0, m - win):
        sim = np.einsum("ij,ij->i", f[:m - lag], f[lag:])
        c = np.convolve(sim, np.ones(win) / win, "valid")
        a = int(np.argmax(c))
        out.append((float(c[a]), a, a + lag))
    out.sort(reverse=True)
    picked = []
    for sc, a, b in out:        # distinct ones: not within a second of another
        if all(abs(a - pa) * hop > rate or abs((b - a) - (pb - pa)) * hop > rate
               for _, pa, pb in picked):
            picked.append((sc, a, b))
        if len(picked) == top:
            break
    res = []
    for sc, a, b in picked:
        s0, s1 = a * hop, b * hop
        res.append((sc, s0, refine_end(x, s0, s1, rate)))
    return res


def refine_end(x, start, end, rate, reach=None, n=None):
    """END moved by up to REACH samples to where the waveform after it best
    matches the waveform after START: the loop's jump in phase"""
    reach = reach or HOP
    n = n or rate // 10
    ref = x[start:start + n]
    if len(ref) < n:
        return end
    best, at = None, end
    for e in range(max(start + 1, end - reach), end + reach + 1):
        seg = x[e:e + n]
        if len(seg) < n:
            break
        c = float(np.dot(ref, seg) / (np.linalg.norm(ref) * np.linalg.norm(seg) + 1e-9))
        if best is None or c > best:
            best, at = c, e
    return at


def loop_wav(x, start, end, rate, path, around=3.0):
    """A WAV of the loop's JUMP to listen to: the AROUND seconds before its
    end, then the AROUND seconds from its start"""
    import wave
    k = int(around * rate)
    y = np.concatenate([x[max(start, end - k):end], x[start:start + k]])
    pcm = (np.clip(y, -1, 1) * 32767).astype("<i2").tobytes()
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm)
    return path


def sound_wav(x, rate, path):
    import wave
    pcm = (np.clip(x, -1, 1) * 32767).astype("<i2").tobytes()
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm)
    return path


# --- the command line: every long piece of work the window runs, as a process
# whose progress and answer are JSON lines on stdout (plan 2) ---------------------

def main(argv=None):
    import argparse
    import vbtproj as P
    ap = argparse.ArgumentParser(description="the builder's analysis")
    sp = ap.add_subparsers(dest="cmd", required=True)
    q = sp.add_parser("screens", help="encode and cache a source")
    q.add_argument("project")
    q.add_argument("source")
    q = sp.add_parser("loops", help="the idle loop's candidates")
    q.add_argument("project")
    q.add_argument("source")
    q.add_argument("--min", type=int, default=20)
    q = sp.add_parser("cost", help="the idle loop's records")
    q.add_argument("project")
    q.add_argument("--loop", type=int, nargs=2)
    q = sp.add_parser("musicloops", help="the music's loop candidates")
    q.add_argument("project")
    q.add_argument("sound")
    q.add_argument("--min-secs", type=float, default=4.0)
    q = sp.add_parser("wav", help="a WAV to listen to")
    q.add_argument("project")
    q.add_argument("sound")
    q.add_argument("out")
    q.add_argument("--loop", type=int, nargs=2,
                   help="the jump of a loop START END, in samples")
    a = ap.parse_args(argv)
    proj = P.Project.load(a.project)

    def say(o):
        print(json.dumps(o), flush=True)

    def progress(stage, done, of, what):
        say(dict(stage=stage, done=done, of=of, what=what))
    if a.cmd == "screens":
        fr = screens(proj, a.source, progress)
        say(dict(stage="done", frames=len(fr)))
    elif a.cmd == "loops":
        fr = screens(proj, a.source, progress)
        say(dict(stage="done", loops=[dict(score=s, start=x, end=y)
                                      for s, x, y in
                                      loop_candidates(fr, a.min)]))
    elif a.cmd == "cost":
        say(dict(stage="done", **idle_cost(proj, a.loop, progress)))
    elif a.cmd == "musicloops":
        x = sound(proj, a.sound)
        progress("analyse", 0, 1, a.sound)
        rate = proj.d["target"]["rate"]
        say(dict(stage="done", rate=rate, samples=len(x), loops=[
            dict(score=s, start=x0, end=x1) for s, x0, x1 in
            music_loops(x, rate, a.min_secs)]))
    elif a.cmd == "wav":
        x = sound(proj, a.sound)
        rate = proj.d["target"]["rate"]
        if a.loop:
            loop_wav(x, a.loop[0], a.loop[1], rate, a.out)
        else:
            sound_wav(x, rate, a.out)
        say(dict(stage="done", out=a.out))


if __name__ == "__main__":
    main()
