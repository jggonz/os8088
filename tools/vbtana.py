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


SCALES = (100, 90, 75, 67, 50)     # a rendition's picture, % of its canvas


FITS = ("fill", "stretch")      # target.fit: crop to the canvas's shape, or
                                # squeeze the whole picture into it
CLIP_DEFAULT = 16               # target.clip: levels from black (and from
                                # white) drawn SOLID and never held


def fit_of(proj):
    f = proj.d["target"].get("fit") or "fill"
    return f if f in FITS else "fill"


def clip_of(proj):
    c = proj.d["target"].get("clip")
    return CLIP_DEFAULT if c is None else max(0, min(96, int(c)))


def scale_of(proj, kind):
    """Rendition KIND's PICTURE SIZE, % of the canvas (and of the screen,
    for a full-screen opening): target.scale[kind], 100 when not set"""
    return int((proj.d["target"].get("scale") or {}).get(kind, 100))


def boxed(w, h, pct):
    """The picture inside a W x H area at PCT%: (pw, ph, x0, y0), its width
    and left edge in whole BYTES of every adapter's (8 pixels), its height
    and top even - centred, black round it. A smaller picture is fewer
    bytes a frame on every adapter: the black never changes, so no record
    ever touches it, and the engine plays it as it is"""
    if pct >= 100:
        return w, h, 0, 0
    pw = max(8, int(round(w * pct / 100.0 / 8)) * 8)
    ph = max(2, int(round(h * pct / 100.0 / 2)) * 2)
    return pw, ph, (w - pw) // 16 * 8, (h - ph) // 4 * 2


def pad_into(fr, w, h, x0, y0):
    """Frames FR (n x ph x pw) centred in black, n x H x W"""
    fr = np.asarray(fr, np.uint8)
    n, ph, pw = fr.shape[:3]
    out = np.zeros((n, h, w), np.uint8)
    ph, pw = min(ph, h - y0), min(pw, w - x0)
    out[:, y0:y0 + ph, x0:x0 + pw] = fr[:, :ph, :pw]
    return out


def screens_npy(proj, sid, full=False):
    """Where source SID's encode (screens()) is cached: one key for every
    setting the encode depends on, so the builder's window asks here and
    cannot drift from it"""
    s, t = proj.d["sources"][sid], proj.d["target"]
    rows = 200 if full else t["canvas"][3]
    pct = scale_of(proj, "cga4")
    key = hashlib.sha1(json.dumps(
        [ENC_KEY, file_hash(proj.here(s["path"])), rows, t["fps"],
         t["profile"], t["palette"]] + ([pct] if pct < 100 else []) +
        ([fit_of(proj)] if fit_of(proj) != "fill" else []) +
        ([clip_of(proj)] if clip_of(proj) != CLIP_DEFAULT else [])).encode()
                       ).hexdigest()[:20]
    return os.path.join(cache_dir(proj), key + ".npy")


def screens(proj, sid, progress=_nop, full=False):
    """Source SID's frames, as the decoder will show them - the canvas's
    rows, or FULL: the whole 200 (a full-screen opening, 125.3)"""
    s = proj.d["sources"][sid]
    if s["kind"] == "scene":
        return [np.asarray(f, np.uint8) for f in _scene(s["name"])]
    t = proj.d["target"]
    rows = 200 if full else t["canvas"][3]
    pct = scale_of(proj, "cga4")
    pw, ph, x0, y0 = boxed(320, rows, pct)
    path = proj.here(s["path"])
    npy = screens_npy(proj, sid, full)
    key = os.path.basename(npy)[:-4]
    if os.path.exists(npy):
        return list(np.load(npy))
    progress("encode", 0, 0, sid)
    v88 = os.path.join(cache_dir(proj), key + ".V88")
    # the encoder's own reports (--progress, SPEC.md 98.2.11.1), passed on:
    # a first look at a video is a minute or two, and a window that says
    # nothing for that long looks stuck
    pr = subprocess.Popen(
        [sys.executable, "-u", os.path.join(HERE, "os88venc.py"), path, v88,
         "--quiet", "--progress", "--preset", "cga4",
         "--box", "%dx%d" % (pw, ph), "--fit", fit_of(proj),
         "--fps", str(t["fps"]), "--clip", str(clip_of(proj)),
         "--audio", "none", "--profile", t["profile"]] +
        palette_args(t["palette"]), stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, encoding="utf-8", errors="replace")
    mark = "@@progress "
    tail = []
    for line in pr.stdout:
        if line.startswith(mark):
            try:
                ev = json.loads(line[len(mark):])
                progress("encode", int(ev.get("done") or 0),
                         int(ev.get("total") or 0),
                         "%s: %s" % (sid, ev.get("step", "")))
            except (ValueError, TypeError):
                pass
        else:
            tail = (tail + [line.rstrip()])[-20:]
    if pr.wait() != 0:
        raise RuntimeError("os88venc could not read %s:\n%s"
                           % (path, "\n".join(tail)))
    r = V.Reader(v88)
    fr = []
    for n in range(r.frames):
        fr.append(unpack(V.decode_at(r, n), r.g.h, r.g.wb))
        progress("decode", n + 1, r.frames, sid)
    if pct < 100:                   # (the picture boxed in the canvas)
        fr = list(pad_into(fr, 320, rows, x0, y0))
    tmp = npy + ".part.npy"         # whole or not at all: a Cancel that
    np.save(tmp, np.stack(fr))      # kills this mid-write leaves no
    os.replace(tmp, npy)            # cache that loads short
    os.remove(v88)
    return fr


# --- RENDITIONS (Hercules and VGA): the same frames, another adapter's pixels -
#
# A project's frames are numbered once - its loop points, anchors and cues are
# frame numbers - so every rendition is made of the SAME frames: read through
# the encoder's own ffmpeg_video with the CGA encode's fps expression, and
# quantized here. "cga4" is the encoder's own encode (screens above).
#   vga4: mode 0Dh, 320 x the canvas's rows, K colours of ONE palette for the
#         whole battle - slots 0..3 the band's CGA colours (the band is the
#         same picture on every adapter), the rest the footage's own
#   herc: 640 x (348 - twice the band's rows): every logical band pixel two
#         by two, so the band is the same 40 x 8 cells; one bit a pixel

REND_KINDS = ("cga4", "vga4", "herc")
HOLD_RGB = 14           # a pixel keeps its colour while its source moves less
HOLD_LUM = 10           # ...and its bit, while its brightness does


def rend_size(proj, kind):
    """(width, canvas rows, full-screen rows) of rendition KIND"""
    rows = proj.d["target"]["canvas"][3]
    if kind == "herc":
        return 640, 348 - 2 * (200 - rows), 348
    return 320, rows, 200


def _src_crop(kind, w, h):
    par = (29, 45) if kind == "herc" else (5, 6)
    return w * par[0] / par[1] / h


def rgb_frames(proj, sid, w, h, kind, progress=_nop):
    """Source SID's frames as rgb24, w x h, the encoder's frames exactly"""
    import os88venc as E
    s = proj.d["sources"][sid]
    t = proj.d["target"]
    path = proj.here(s["path"])
    fit = fit_of(proj)
    key = hashlib.sha1(json.dumps(["rgb1", file_hash(path), w, h, kind,
                                   t["fps"], t["rate"]] +
                                  ([fit] if fit != "fill" else [])).encode()
                       ).hexdigest()[:20]
    npy = os.path.join(cache_dir(proj), key + ".rgb.npy")
    if os.path.exists(npy):
        return np.load(npy)
    progress("encode", 0, 0, "%s (%s)" % (sid, kind))
    spf = t["rate"] // t["fps"]
    fr = np.stack(list(E.ffmpeg_video(path, w, h, _src_crop(kind, w, h)
                                      if fit == "fill" else None,
                                      "%d/%d" % (t["rate"], spf), None, None,
                                      None, pix="rgb24")))
    tmp = npy + ".part.npy"
    np.save(tmp, fr)
    os.replace(tmp, npy)
    return fr


def battle_palette(proj, k, progress=_nop):
    """The VGA rendition's K colours (8-bit RGB): the band's four CGA colours,
    then K - 4 of the footage's own, a k-means over every video source"""
    t = proj.d["target"]
    band = palette_rgb(t["palette"])
    if k <= 4:
        return [tuple(c) for c in band][:k]
    w, h, _ = rend_size(proj, "vga4")
    samp = []
    rng = np.random.RandomState(1)
    for sid, s in proj.d["sources"].items():
        if s["kind"] == "scene":
            continue
        fr = rgb_frames(proj, sid, w, h, "vga4", progress)
        px = fr.reshape(-1, 3)
        samp.append(px[rng.randint(0, len(px), min(len(px), 20000))])
    if not samp:
        return [tuple(c) for c in band] + [(0, 0, 0)] * (k - 4)
    x = np.concatenate(samp).astype(float)
    fixed = np.array(band, float)
    c = x[rng.choice(len(x), k - 4, replace=False)]
    for _ in range(16):
        allc = np.vstack([fixed, c])
        d2 = ((x[:, None, :] - allc[None]) ** 2).sum(2)
        lab = np.argmin(d2, 1)
        worst = np.argsort(-d2.min(1))      # the pixels served worst
        w = 0
        for j in range(k - 4):
            m = lab == j + 4
            others = np.delete(allc, j + 4, 0)
            dup = ((others - c[j]) ** 2).sum(1).min() < 100
            if m.any() and not dup:
                c[j] = x[m].mean(0)
            else:                           # empty, or a copy of another
                c[j] = x[worst[w]]          # colour: re-seeded where the
                w += 1                      # palette is furthest off
    allc = np.vstack([fixed, c])            # (and, last, no near copies:
    for j in range(k - 4):                  # a mean can pull one back)
        others = np.delete(allc, j + 4, 0)
        if ((others - c[j]) ** 2).sum(1).min() < 400:
            d2 = ((x[:, None, :] - allc[None]) ** 2).sum(2).min(1)
            c[j] = x[int(np.argmax(d2))]
            allc = np.vstack([fixed, c])
    return [tuple(int(v) for v in col) for col in np.vstack([fixed, c])]


def _bayer4():
    m = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9],
                  [15, 7, 13, 5]], float)
    return (m + 0.5) / 16


def quant_vga(fr, pal, clip=0):
    """rgb24 frames to palette indexes: an ordered dither anchored to the
    canvas, and a pixel's colour HELD while its source has moved less than
    HOLD_RGB, so still footage costs nothing frame to frame"""
    pal = np.array(pal, float)
    n, h, w, _ = fr.shape
    b = _bayer4()[np.arange(h)[:, None] % 4, np.arange(w)[None, :] % 4]
    spread = 40.0
    out = np.zeros((n, h, w), np.uint8)
    prev, prgb = None, None
    for i in range(n):
        x = fr[i].astype(float) + ((b - 0.5) * spread)[..., None]
        d = ((x[:, :, None, :] - pal[None, None]) ** 2).sum(3)
        idx = np.argmin(d, 2).astype(np.uint8)
        solid = None
        if clip:                    # NEAR A COLOUR: that colour, solid and
            d0 = ((fr[i].astype(float)[:, :, None, :] - pal[None, None]) **
                  2).sum(3)         # never held - a fade to black ends
            n0 = np.argmin(d0, 2)   # black, where a held dot stayed lit
            solid = np.abs(fr[i].astype(float) - pal[n0]).max(2) < clip
            idx[solid] = n0[solid]
        if prev is not None:
            still = np.abs(fr[i].astype(int) - prgb).max(2) < HOLD_RGB
            if solid is not None:
                still &= ~solid
            idx[still] = prev[still]
            prgb = np.where(still[..., None], prgb, fr[i].astype(int))
        else:
            prgb = fr[i].astype(int)
        out[i] = idx
        prev = idx
    return out


def quant_herc(fr, clip=0):
    """rgb24 frames to one bit: brightness against the ordered threshold,
    a pixel HELD while its brightness moves less than HOLD_LUM"""
    lum = fr[..., 0] * 0.299 + fr[..., 1] * 0.587 + fr[..., 2] * 0.114
    n, h, w = lum.shape
    b = _bayer4()[np.arange(h)[:, None] % 4, np.arange(w)[None, :] % 4] * 255
    out = np.zeros((n, h, w), np.uint8)
    prev, plum = None, None
    for i in range(n):
        bit = (lum[i] > b).astype(np.uint8)
        dark, lit = lum[i] < clip, lum[i] > 255 - clip     # (SOLID: never
        bit[dark], bit[lit] = 0, 1                          # held - a fade
        if prev is not None:                                # to black ends
            still = (np.abs(lum[i] - plum) < HOLD_LUM) & ~dark & ~lit  # black)
            bit[still] = prev[still]
            plum = np.where(still, plum, lum[i])
        else:
            plum = lum[i].copy()
        out[i] = bit
        prev = bit
    return out


# a CGA value as Hercules' two by two: 0 none, 1 one, 2 a checker, 3 all -
# the band's own rule (vbband.inc's present), so a made-up scene and the band
# look the same
HERC22 = np.array([[[0, 0], [0, 0]], [[1, 0], [0, 0]], [[1, 0], [0, 1]],
                   [[1, 1], [1, 1]]], np.uint8)


def herc_of_cga(px, rows):
    """A CGA4 frame (h x 320) as Hercules pixels, ROWS x 640: each pixel two
    wide, the rows scaled (a 142-row canvas is 232 there, a 200-row screen
    348), the 2 x 2 pattern by the Hercules pixel's own place"""
    h, w = px.shape
    src = (np.arange(rows) * h) // rows
    v = px[src]                                     # rows x 320
    pat = HERC22[v]                                 # rows x 320 x 2 x 2
    yy = (np.arange(rows) & 1)[:, None]
    return np.concatenate([pat[np.arange(rows)[:, None], np.arange(w)[None],
                                yy, 0][..., None],
                           pat[np.arange(rows)[:, None], np.arange(w)[None],
                               yy, 1][..., None]], 2).reshape(rows, w * 2)


def rend_screens(proj, sid, kind, progress=_nop, full=False):
    """Source SID's frames in rendition KIND, as the adapter will show them:
    palette indexes for cga4 and vga4, bits for herc. FULL: over the whole
    screen (a full-screen opening), not the canvas"""
    if kind == "cga4":
        return screens(proj, sid, progress, full)
    s = proj.d["sources"][sid]
    if s["kind"] == "scene":
        base = screens(proj, sid, progress)     # (CGA values: the VGA's
        if kind == "vga4":                      # slots 0..3 are CGA's)
            return base
        _, crow, frow = rend_size(proj, "herc")
        cr = proj.d["target"]["canvas"][3]
        out = []
        for f in base:              # a WHOLE screen in two parts: its canvas
            if f.shape[0] == 200:   # rows onto Hercules' canvas, its band's
                out.append(np.vstack([herc_of_cga(f[:cr], crow),    # two
                                      herc_of_cga(f[cr:], frow - crow)]))
            else:                   # for one onto Hercules' band - scaled
                out.append(herc_of_cga(f, crow))    # whole, the picture ran
        return out                                  # into the band's rows
    w, h, fh = rend_size(proj, kind)
    if full:
        h = fh
    pct = scale_of(proj, kind)
    pw, ph, x0, y0 = boxed(w, h, pct)
    t = proj.d["target"]
    k = t.get("vga_colours", 8)
    key = hashlib.sha1(json.dumps(
        ["q1", file_hash(proj.here(s["path"])), kind, w, h, k, t["palette"],
         HOLD_RGB, HOLD_LUM, battle_palette(proj, k) if kind == "vga4"
         else None] + ([pct] if pct < 100 else []) +
        ([fit_of(proj)] if fit_of(proj) != "fill" else []) +
        [clip_of(proj)]).encode()).hexdigest()[:20]
    npy = os.path.join(cache_dir(proj), key + ".q.npy")
    if os.path.exists(npy):
        return list(np.load(npy))
    fr = rgb_frames(proj, sid, pw, ph, kind, progress)
    n = len(screens(proj, sid, progress, full))   # (the CGA encode's count)
    fr = fr[:n] if len(fr) >= n else np.concatenate(
        [fr, np.repeat(fr[-1:], n - len(fr), 0)])
    progress("analyse", 0, 1, "%s (%s)" % (sid, kind))
    q = quant_vga(fr, battle_palette(proj, k), clip_of(proj)) \
        if kind == "vga4" else quant_herc(fr, clip_of(proj))
    if pct < 100:
        q = pad_into(q, w, h, x0, y0)
    np.save(npy + ".part.npy", q)
    os.replace(npy + ".part.npy", npy)
    return list(q)


def frames_of(proj, cid, progress=_nop, kind="cga4", full=False):
    """Clip CID's frames: its source's, in..out (FULL: the whole screen's)"""
    c = proj.d["clips"][cid]
    return rend_screens(proj, c["source"], kind, progress, full)[
        c["in"]:c["out"] + 1]


def idle_of(proj, progress=_nop, kind="cga4"):
    i = proj.d["idle"]
    a, b = i["loop"]
    return rend_screens(proj, i["source"], kind, progress)[a:b]


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
    elif proj.d["sources"][s["source"]]["kind"] == "scene":
        x = np.zeros((s["out"] + 1 - s["in"]) * rate // fps)  # (a made-up
    else:                                       # video has no soundtrack)
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
    d = proj.d                  # the project's own names, in the bundle's
    tt = d.get("title") or {}   # order (os88vbt.battle_of's)
    opened = ([tt["leave"]] if tt.get("leave") else []) + \
        [o["clip"] for o in d["openings"]]
    opened = opened or ["(start)"]
    looped = {tt.get("clip"), tt.get("leave")} | {
        en["loop"]["clip"] for en in d["endings"].values() if en.get("loop")}
    used = set(proj.clips_used()) - looped | {
        d["enemy"].get("victory"), d["enemy"].get("defeat")}
    for a in d["actions"].values():
        for pool in a["pools"].values():
            used |= set(pool)
    ids = opened + [c for c in d["clips"] if c not in opened and
                    (c not in looped or c in used)]
    for c, cid in zip(clips, ids):
        c["id"] = cid
    for c, bc in zip(clips, b.clips):
        c["loop"] = bool(bc["flags"] & 32)      # (os88vbt.CF_LOOP)
        if c["loop"]:
            c["id"] = "%s (a loop)" % ("the title" if b.clips.index(bc) ==
                                       b.title else "an end loop")
    mem = memory(data)
    mrate = 0 if mem["music_resident"] else b.rate * 1.0   # 8-bit mono
    for c in clips:
        c["with_music"] = (c["rate"] + mrate) / disk
        c["head_secs"] = min(c["bytes"], VB_HEAD) / disk
        c["fits_ring"] = mem["ring"] is not None and c["bytes"] <= mem["ring"]
    order = [n * 4 for _, n in getattr(b, "hold", [])]     # KB, in order
    holds = {}
    for pool in (1024, 3072):       # a 286's common pools (98.3.18.2.1)
        whole = len(data) // 1024 <= pool - 4
        got, left = 0, pool - 4
        for kb in order:
            take = min(kb, max(0, left))
            got += take
            left -= take
        holds[pool] = dict(whole=whole, kb=len(data) // 1024 if whole
                           else got, order_kb=sum(order))
    return dict(clips=clips, idle_bytes=len(b.idle_r), entries=b.ents_len,
                hold=holds,
                music=b.mlen, ui=len(b.uib), file=len(data), disk=disk,
                memory=mem, music_rate=mrate)


# THE ENGINE'S MEMORY (SPEC.md 125.4), carved as vb_load and vb_rclaim carve
# it, so that the Build tab's figures are the engine's own arithmetic
VB_MR, VB_MPIECE, VB_MRES = 32768, 4096, 61440      # the music's ring, or
                                                    # all of a short one;
VB_CARD, VB_RKEEP, VB_CGUARD = 17, 8, 16            # KB
VB_CRMIN, VB_HEAD, VB_LEAD = 48, 40960, 4096        # (vbattle.asm's names)
# The KB free to the engine once it is loaded, on a 640 KB 5150 with a
# Sound Blaster and os8088's hard disk driver: MEASURED (VB_FREE_SRC)
VB_FREE = 495
VB_FREE_SRC = ("measured on MartyPC's Hercules 5150, 640 KB, with a Sound "
               "Blaster and os8088's hard disk driver "
               "(os8088_5150_herc_hdd_sb_gla), 2026-11-05, the owner's 100% "
               "Hercules battle at format 8: rings 168 + 172 KB (each with "
               "its 16 KB guard) beside a 96 KB resident claim and the "
               "card's 17, 10 KB left free - the desktop's caches shed for "
               "the play (VBATTLE-RAM-PLAN R1, R2)")


def _r4k(n):
    return min((n + 4095) & ~4095, 0xF000)


def memory(data, free_kb=None):
    """What the engine claims for bundle DATA (125.4), in bytes: the
    resident claim's parts, the card's ring, and the two clip rings a play
    is given out of FREE_KB (the largest run once it is loaded). "rings" is
    None when they would be under VB_CRMIN: Play says Not enough memory"""
    import os88vbt as B
    b = B.Bundle(data)
    parts = resident_parts(len(b.vres), len(b.idle_r), len(b.uib), b.mlen,
                           len(b.imgs), b.band[1])
    res = sum(n for _, n in parts)
    ring = ring_of(res, b.maxrec, free_kb)
    emin = (max(c["esz"] for c in b.clips) + 1023) // 1024 + 16
    if ring is not None and ring < emin * 1024:   # (vb_rmin: the largest
        ring = None                     # clip's entries and 16 KB past them)
    return dict(parts=parts, resident=res, card=VB_CARD * 1024,
                ring=ring, free=(free_kb or VB_FREE) * 1024, music_resident=b.mlen <=
                VB_MRES)


def resident_parts(vres, idle, ui, mlen, imgs, band_row):
    """The resident claim's parts (vb_load), each its 4 KBs: from the
    sizes alone, so that the builder can ask before its clips are made"""
    mres = mlen <= VB_MRES
    return [("the block (its tables)", _r4k(vres)),
            ("the idle loop", (idle + 4095) & ~4095),
            ("the menu's sounds", _r4k(ui)),
            ("the music, all of it" if mres else "the music's ring",
             _r4k(mlen) if mres else VB_MR + VB_MPIECE),
            ("the band's pictures", _r4k(imgs)),
            ("the band's two buffers",      # (vb_bpara: the band's rows at
             2 * _r4k((200 - band_row) * 80))]  # 80 bytes, VBATTLE-RAM R1)


def ring_of(res, maxrec, free_kb=None):
    """A clip ring's bytes (the smaller of the two, its guard out) beside
    a resident claim of RES bytes, out of FREE_KB - or None under VB_CRMIN,
    when Play says Not enough memory (vb_rclaim)"""
    if free_kb is None:
        free_kb = VB_FREE
    left = free_kb - (res + 1023) // 1024 - VB_CARD - VB_RKEEP
    guard = VB_CGUARD if maxrec <= 16384 else 2 * VB_CGUARD
    ring = (left // 2 - guard) // 4 * 4
    return ring * 1024 if ring >= VB_CRMIN else None


def cap_hold(ring, income, fps, srate):
    """What a ring of RING bytes holds ahead of a clip's SOUND, for the
    builder's reader's model (125.4.3, VBATTLE-RAM-PLAN R4): the ring less
    the lead's share - the card's lead (VB_LEAD samples at SRATE) and a
    frame, at the clip's INCOME of bytes a second - and a piece"""
    lead_frames = -(-VB_LEAD * fps // srate) + 1
    return max(0, ring - int(lead_frames * income / float(fps)) - VB_MPIECE)


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


# --- a clip against the loop (4.3), and its sound (10.2) -------------------------------

# --- the openings, and where the music comes in (plan 10.3) ----------------------

def openings_sound(proj):
    """The openings' sound as the engine plays it (125.3): each one's own,
    as long as its frames (cut, or silence after it), the next starting at
    the sample the last ends; then the MUSIC comes in. Floats, and the
    (first sample, clip) of each opening. The music's place is its end"""
    d, t = proj.d, proj.d["target"]
    spf = t["rate"] // t["fps"]
    parts, at, n0 = [], [], 0
    for o in d["openings"]:
        c = d["clips"][o["clip"]]
        n = (c["out"] - c["in"] + 1) * spf
        x = np.zeros(n)
        if o.get("sound"):
            y = sound(proj, o["sound"])[:n]
            x[:len(y)] = y
        at.append((n0, o["clip"]))
        parts.append(x)
        n0 += n
    return (np.concatenate(parts) if parts else np.zeros(0)), at


def music_region(proj):
    """The music as the engine holds it: its LOOP (or all of it), the join's
    place in it, and the loop's first sample in the sound"""
    m = proj.d["music"]
    if not m:                       # (no music: the build's second of
        return np.zeros(proj.d["target"]["rate"]), 0, 0     # silence)
    x = sound(proj, m["sound"])
    lp = m.get("loop") or [0, len(x)]
    return x[lp[0]:lp[1]], m["join"] - lp[0], lp[0]


def envelope(x, hop=256):
    """The sound's loudness a HOP samples at a time (its RMS), in decibels -
    the shape the join is matched by"""
    n = len(x) // hop
    if n == 0:
        return np.zeros(0)
    r = np.sqrt(np.mean(x[:n * hop].reshape(n, hop) ** 2, axis=1))
    return 20 * np.log10(r + 1e-4)


def music_joins(proj, window=2.0, top=5, hop=256):
    """Where the music should COME IN after the scene opening (plan 10.3):
    the joins J whose WINDOW seconds of music BEFORE J are most like the
    openings' last WINDOW seconds, by their envelopes' correlation - so
    the music goes on with what the opening's sound was doing. Best first,
    at least half a second apart: dicts of score (-1..1) and join (the
    sound's sample, as the project keeps it). [] when the openings end in
    silence or the music has no shape to match"""
    op, _ = openings_sound(proj)
    reg, _, lp0 = music_region(proj)
    rate = proj.d["target"]["rate"]
    return [dict(score=sc, join=lp0 + j) for j, sc in
            join_candidates(op, reg, rate, window, top, hop)]


def join_candidates(op, reg, rate, window=2.0, top=5, hop=256):
    """music_joins' matching, on the sounds themselves: the places J in the
    music REG (round its loop) whose WINDOW seconds before are most like
    OP's last WINDOW seconds, as (J in samples, score), best first"""
    w = int(window * rate / hop)
    a = envelope(op[-w * hop:], hop)
    if len(a) < w or np.std(a) < 0.5 or len(reg) < (w + 1) * hop:
        return []
    e = envelope(reg, hop)
    m = len(e)
    ee = np.concatenate([e, e])                 # round the loop
    a = (a - a.mean()) / np.std(a)
    win = np.lib.stride_tricks.sliding_window_view(ee, w)[:m]
    mu = win.mean(1, keepdims=True)
    sd = win.std(1, keepdims=True)
    sc = ((win - mu) / np.maximum(sd, 1e-6) @ a) / w
    sc[sd[:, 0] < 0.5] = -1
    out = []                    # window k covers e[k:k+w]: the join is after
    for k in np.argsort(-sc):
        j = int((k + w) % m)
        if all(abs(j - o) * hop > rate // 2 and
               (m - abs(j - o)) * hop > rate // 2 for o, _ in out):
            out.append((j, float(sc[k])))
        if len(out) == top:
            break
    return [(j * hop, s) for j, s in out]


def join_wav(proj, path, join=None, after=4.0, before=4.0):
    """A WAV of the openings' last BEFORE seconds and the music's first
    AFTER seconds from JOIN (the project's own by default), mixed at the
    engine's levels - an opening's sound is OWN (the whole range), the
    music half of it (125.2) - so the join is heard as it will be"""
    op, _ = openings_sound(proj)
    reg, j, lp0 = music_region(proj)
    if join is not None:
        j = join - lp0
    rate = proj.d["target"]["rate"]
    n = int(after * rate)
    idx = (j + np.arange(n)) % max(1, len(reg))
    y = np.concatenate([op[-int(before * rate):], 0.5 * reg[idx]])
    sound_wav(np.clip(y, -1, 1), rate, path)
    return len(op[-int(before * rate):])


def clip_report(proj, cid, progress=_nop):
    """Everything the Clips tab shows about clip CID against the idle loop:
    for its IN frame against every idle frame, the score and the ENTRY's
    bytes (the record that would join them); for its OUT frame, the score
    against every idle frame (its exit); the longest wait its anchors make;
    and what the analysis RECOMMENDS over its whole source"""
    import os88vbt as B
    c = proj.d["clips"][cid]
    rows = proj.d["target"]["canvas"][3]
    idle = idle_of(proj, progress)
    src = screens(proj, c["source"], progress)
    first, last = src[c["in"]], src[c["out"]]
    s0 = B.surface_of(first, rows)
    ent, ins = [], []
    for k, f in enumerate(idle):
        ins.append(diff(first, f))
        ent.append(len(B.rec(B.surface_of(f, rows), s0, rows)))
        progress("entries", k + 1, len(idle), cid)
    outs = [diff(last, f) for f in idle]
    rec = attach(src, idle)
    if c.get("ends"):       # an ending plays to its source's end: no exit
        rec["last"] = len(src) - 1
        rec["exit"] = None
    return dict(entry_scores=ins, entry_bytes=ent, exit_scores=outs,
                wait=wait_frames(c["anchors"], len(idle)),
                frames=len(src), recommend=dict(
                    first=rec["first"], last=rec["last"],
                    anchors=rec["anchors"], exit=rec["exit"]))


# --- LEVELS: every sound as loud as the music (SPEC.md 125.3) --------------------
#
# What a sound sounds like depends on HOW it plays: the music is stored at half
# scale (music_bytes, 64..191) and an effect - a cue, a menu sound - at half
# scale (sfx_bytes, +-64) so that the two add without wrapping; a clip's OWN
# sound, which plays INSTEAD of the music (an opening's, a loop's), at full
# scale (own_bytes). So the same file is 6 dB louder as an opening's sound
# than as the music. Loudness here is AS HEARD: the sound with its level, at
# its scale.

PLAY_SCALE = {"music": 0.5, "effect": 0.5, "own": 1.0}
LEVEL_GATE = -50.0          # dB: quieter 100 ms windows are not counted


def plays_as(proj, sid):
    """How sound SID is played: "music", "own" (an opening's or a loop's,
    instead of the music) or "effect" (a cue, a menu sound); None unused"""
    users = proj.sound_users(sid)
    if "the music" in users:
        return "music"
    if any(u.startswith("opening") or u == "the title loop" or
           u.endswith("end loop") for u in users):
        return "own"
    return "effect" if users else None


def loudness(x, rate):
    """(the gated loudness in dB, the peak in dB) of floats X: the mean
    power of its 100 ms windows louder than LEVEL_GATE - so silence and
    the tail of a hit do not pull a sound's figure down"""
    x = np.asarray(x, float)
    if not len(x) or not np.any(x):
        return None, None
    peak = 20 * np.log10(max(1e-9, float(np.max(np.abs(x)))))
    w = max(1, rate // 10)
    n = len(x) // w
    if n == 0:
        p = [float(np.mean(x ** 2))]
    else:
        p = list(np.mean(x[:n * w].reshape(n, w) ** 2, axis=1))
    p = [v for v in p if v > 0 and 10 * np.log10(v) > LEVEL_GATE]
    if not p:
        return None, peak
    return 10 * np.log10(float(np.mean(p))), peak


def levels(proj, progress=_nop):
    """Every sound in use, measured AS HEARD, and the AUTO-LEVEL suggestion:
    each one's level moved so it is as loud as the music (with no music, as
    the median of the rest), no further than its peak allows - its loudest
    sample at full scale and not past it. A list of dicts: sound, plays (as
    music, own or effect), uses, level (now, linear), loud and peak (dB, as
    heard, at the level now), suggest (the level suggested, linear)"""
    rate = proj.d["target"]["rate"]
    rows = []
    used = sorted(proj.sounds_used())
    for i, sid in enumerate(used):
        progress("measure", i, len(used), sid)
        how = plays_as(proj, sid)
        x = sound(proj, sid)            # (its level applied)
        loud, peak = loudness(x, rate)
        sc = 20 * np.log10(PLAY_SCALE[how])
        rows.append(dict(sound=sid, plays=how, uses=proj.sound_users(sid),
                         level=proj.d["sounds"][sid].get("level", 1.0),
                         loud=None if loud is None else loud + sc,
                         peak=None if peak is None else peak + sc,
                         headroom=None if peak is None else -peak))
    ref = [r["loud"] for r in rows if r["plays"] == "music" and r["loud"]]
    if not ref:
        ref = [float(np.median([r["loud"] for r in rows if r["loud"]
                                is not None]))] if any(
            r["loud"] is not None for r in rows) else []
    target = ref[0] if ref else None
    for r in rows:
        if target is None or r["loud"] is None or r["plays"] == "music":
            r["suggest"] = r["level"]
            continue
        gain = min(target - r["loud"], r["headroom"])   # (dB: never clipped)
        r["suggest"] = round(r["level"] * 10 ** (gain / 20.0), 3)
    progress("measure", len(used), len(used), "")
    return dict(target=target, sounds=rows)


def clip_sound(proj, cid):
    """Clip CID's sound as the BUILD bakes it: its cues placed at their
    frames and mixed (os88vbt.place, the same call), floats at the battle's
    rate; None for a silent clip"""
    import os88vbt as B
    c = proj.d["clips"][cid]
    t = proj.d["target"]
    spf = t["rate"] // t["fps"]
    sfx, cues = {}, []
    for q in c["cues"]:
        if q["sound"] not in sfx:
            sfx[q["sound"]] = sound(proj, q["sound"])
        cues.append((q["frame"], B.cue_key(q, sfx, spf)))
    return B.place(cues, c["out"] - c["in"] + 1, spf, sfx)


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
    q.add_argument("--full", action="store_true",
                   help="over the whole screen (a full-screen opening)")
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
    q = sp.add_parser("levels", help="every sound's loudness as heard, and "
                      "the level that matches it to the music")
    q.add_argument("project")
    q = sp.add_parser("wav", help="a WAV to listen to")
    q.add_argument("project")
    q.add_argument("sound")
    q.add_argument("out")
    q.add_argument("--loop", type=int, nargs=2,
                   help="the jump of a loop START END, in samples")
    q = sp.add_parser("clip", help="a clip against the idle loop")
    q.add_argument("project")
    q.add_argument("clip")
    q = sp.add_parser("clipwav", help="a clip's sound, as the build bakes it")
    q.add_argument("project")
    q.add_argument("clip")
    q.add_argument("out")
    q = sp.add_parser("joins", help="where the music should come in after "
                      "the openings")
    q.add_argument("project")
    q = sp.add_parser("joinwav", help="the openings' end and the music "
                      "coming in, as a WAV")
    q.add_argument("project")
    q.add_argument("out")
    q.add_argument("--join", type=int, help="this join, not the project's")
    q.add_argument("--before", type=float, default=4.0,
                   help="seconds of the openings before it (default 4)")
    q.add_argument("--after", type=float, default=4.0)
    a = ap.parse_args(argv)
    proj = P.Project.load(a.project)

    def say(o):
        print(json.dumps(o), flush=True)

    def progress(stage, done, of, what):
        say(dict(stage=stage, done=done, of=of, what=what))
    if a.cmd == "screens":
        fr = screens(proj, a.source, progress, a.full)
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
    elif a.cmd == "clip":
        say(dict(stage="done", **clip_report(proj, a.clip, progress)))
    elif a.cmd == "clipwav":
        x = clip_sound(proj, a.clip)
        t = proj.d["target"]
        c = proj.d["clips"][a.clip]
        n = (c["out"] - c["in"] + 1) * (t["rate"] // t["fps"])
        sound_wav(x if x is not None else np.zeros(n), t["rate"], a.out)
        say(dict(stage="done", out=a.out, silent=x is None))
    elif a.cmd == "joins":
        progress("analyse", 0, 1, "the openings and the music")
        say(dict(stage="done", rate=proj.d["target"]["rate"],
                 joins=music_joins(proj)))
    elif a.cmd == "joinwav":
        at = join_wav(proj, a.out, a.join, a.after, a.before)
        say(dict(stage="done", out=a.out, music_at=at))
    elif a.cmd == "levels":
        say(dict(stage="done", **levels(proj, progress)))
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
