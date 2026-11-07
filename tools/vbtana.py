#!/usr/bin/env python3
"""vbtana - the analysis half of the Video Battle builder
(docs/plans/completed/VBATTLE-BUILDER-PLAN.md 2, 3, 4).

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
         "cad1" if cadence_on(proj) else None,
         t["profile"], t["palette"]] + ([pct] if pct < 100 else []) +
        ([fit_of(proj)] if fit_of(proj) != "fill" else []) +
        ([clip_of(proj)] if clip_of(proj) != CLIP_DEFAULT else [])).encode()
                       ).hexdigest()[:20]
    return os.path.join(cache_dir(proj), key + ".npy")


def screens(proj, sid, progress=_nop, full=False):
    """Source SID's frames, as the decoder will show them - the canvas's
    rows, or FULL: the whole 200 (a full-screen opening, 126.3)"""
    s = proj.d["sources"][sid]
    if s["kind"] == "scene":
        return [np.asarray(f, np.uint8) for f in _scene(s["name"])]
    t = proj.d["target"]
    rows = 200 if full else t["canvas"][3]
    pct = scale_of(proj, "cga4")
    pw, ph, x0, y0 = boxed(320, rows, pct)
    path = vid_path(proj, sid, progress)
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
#         whole battle, chosen from the footage for colour (battle_palette);
#         the band takes the nearest four (band_index) unless it keeps its
#         CGA colours (target.vga_band_cga)
#   herc: 640 x (348 - twice the band's rows): every logical band pixel two
#         by two, so the band is the same 40 x 8 cells; one bit a pixel

# --- A CAPTURE'S CADENCE: the frames its footage had --------------------------
#
# The owner's footage is CAPTURED: a 15 fps original recorded at 60, each of
# its frames four captured ones - but the capture drifts against it, three
# and a stray one, five, so taking every fourth captured frame by its time
# (ffmpeg's fps filter) now and then takes one original frame twice and
# steps over the next: the owner's "stutters exactly once, always the exact
# same spot" (2026-11-07; 07-Scope's frame 84 changed 455 bytes between
# frames of 14 KB). A source whose rate is a whole multiple of the target's
# and that changes on about one captured frame in that many is read by its
# CADENCE instead (cadence_picks): each original frame's start found where
# the picture changes, three to five captured frames apart, and its
# steadiest captured frame taken - written once to a lossless file the
# encodes read (vid_path). Every other source is read by time, as before.

CAD_PEN = {-2: 1.0, -1: 0.3, 0: 0.0, 1: 0.3, 2: 1.0}  # a gap off the
#                         multiple: what it costs against a change found
CAD_STILL = 0.1         # a captured frame changing under this (mean |gray|
#                         difference) is the same picture...
CAD_SHARE = 0.45        # ...and a capture has at least this share of them


def cadence_on(proj):
    return proj.d["target"].get("cadence", True) is not False


def cadence_picks(d, m):
    """D[i]: how much captured frame I changed from I - 1 (D[0] any);
    M: captured frames to an original one. The captured frame chosen for
    each original frame, in order: its starts placed on the changes, M - 2
    to M + 2 apart (M the cheapest), by the best path through them; then in
    each the steadiest frame, the least change into and out of it"""
    n = len(d)
    dn = np.minimum(np.asarray(d, float) / (np.percentile(d, 90) + 1e-6),
                    1.5)
    gaps = [(m + k, p) for k, p in CAD_PEN.items() if m + k >= 1]
    best = np.full(n + 1, -1e18)
    prev = np.zeros(n + 1, int)
    best[0] = 0.0
    for b in range(1, n + 1):
        sc = dn[b] if b < n else 0.0
        for g, p in gaps:
            a = b - g
            if a >= 0 and best[a] - p + sc > best[b]:
                best[b] = best[a] - p + sc
                prev[b] = a
    end = max(range(max(1, n - m - 2), n + 1), key=lambda b: best[b])
    bs = []
    b = end
    while b > 0:
        bs.append(b)
        b = prev[b]
    bs = [0] + bs[::-1]
    if bs[-1] < n:
        bs.append(n)
    picks = []
    for a, b in zip(bs[:-1], bs[1:]):
        sc = [(d[i] if i > a else 0) + (d[i + 1] if i + 1 < b else 0) +
              (1e9 if i == a and b - a > 1 else 0) for i in range(a, b)]
        picks.append(a + int(np.argmin(sc)))
    return picks


def _probe(path):
    """(rate as a Fraction, width, height, sample aspect "n:d") of PATH's
    video, or None"""
    from fractions import Fraction
    r = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0",
                        "-show_entries", "stream=r_frame_rate,width,height,"
                        "sample_aspect_ratio", "-of", "json", path],
                       capture_output=True, text=True)
    try:
        st = json.loads(r.stdout)["streams"][0]
        sar = st.get("sample_aspect_ratio") or "1:1"
        return (Fraction(st["r_frame_rate"]), int(st["width"]),
                int(st["height"]), sar if sar != "0:1" else "1:1")
    except (ValueError, KeyError, IndexError, ZeroDivisionError):
        return None


CADENCED = {}           # source path -> (captured fps, the target's), the
#                         sources the last reads took by their cadence


def vid_path(proj, sid, progress=_nop):
    """The file source SID's VIDEO is read from: the source, or - a capture
    at a whole multiple of the target's rate - its original frames, picked
    by its cadence, in a lossless file in the cache"""
    path = proj.here(proj.d["sources"][sid]["path"])
    if not cadence_on(proj):
        return path
    fps = proj.d["target"]["fps"]
    key = hashlib.sha1(json.dumps(["cad1", file_hash(path), fps]).encode()
                       ).hexdigest()[:20]
    out = os.path.join(cache_dir(proj), key + ".cad.mkv")
    no = os.path.join(cache_dir(proj), key + ".nocad")
    if os.path.exists(out):
        CADENCED[path] = tuple(json.load(open(out + ".json")))
        return out
    if os.path.exists(no):
        return path
    pr = _probe(path)
    m = pr and pr[0] / fps
    if not pr or m < 2 or abs(m - round(m)) > 0.01:
        open(no, "w").close()
        return path
    m = int(round(m))
    progress("encode", 0, 0, "%s (its cadence)" % sid)
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-an", "-vf",
                          "scale=80:45:flags=area", "-f", "rawvideo",
                          "-pix_fmt", "gray", "-"], capture_output=True
                         ).stdout
    g = np.frombuffer(raw, np.uint8).reshape(-1, 45, 80).astype(np.int16)
    if len(g) < 2 * m:
        open(no, "w").close()
        return path
    d = np.r_[255.0, np.abs(g[1:] - g[:-1]).mean(axis=(1, 2))]
    if (d < CAD_STILL).mean() < CAD_SHARE * (m - 1) / m:
        open(no, "w").close()       # (its own frames: no cadence to find)
        return path
    picks = set(cadence_picks(d, m))
    _, w, h, sar = pr
    tmp = out + ".part.mkv"
    enc = subprocess.Popen(["ffmpeg", "-v", "error", "-y", "-f", "rawvideo",
                            "-pix_fmt", "rgb24", "-s", "%dx%d" % (w, h),
                            "-r", str(fps), "-i", "-", "-vf",
                            "setsar=%s" % sar.replace(":", "/"),
                            "-c:v", "ffv1", tmp], stdin=subprocess.PIPE)
    dec = subprocess.Popen(["ffmpeg", "-v", "error", "-i", path, "-an",
                            "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
                           stdout=subprocess.PIPE)
    size = w * h * 3
    i = 0
    while True:
        f = dec.stdout.read(size)
        if len(f) < size:
            break
        if i in picks:
            enc.stdin.write(f)
        i += 1
    dec.wait()
    enc.stdin.close()
    if enc.wait() != 0:
        raise RuntimeError("could not write %s's frames by its cadence" % sid)
    json.dump([int(pr[0]), fps], open(out + ".json", "w"))
    os.replace(tmp, out)
    CADENCED[path] = (int(pr[0]), fps)
    return out


REND_KINDS = ("cga4", "vga4", "herc")
HOLD_LUM = 10           # Hercules: a pixel keeps its bit while its brightness
#                         moves less (VGA4's hold is Knoll's own, quant_vga)


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
                                  (["cad1"] if cadence_on(proj) else []) +
                                  ([fit] if fit != "fill" else [])).encode()
                       ).hexdigest()[:20]
    npy = os.path.join(cache_dir(proj), key + ".rgb.npy")
    if os.path.exists(npy):
        return np.load(npy)
    progress("encode", 0, 0, "%s (%s)" % (sid, kind))
    path = vid_path(proj, sid, progress)
    spf = t["rate"] // t["fps"]
    fr = np.stack(list(E.ffmpeg_video(path, w, h, _src_crop(kind, w, h)
                                      if fit == "fill" else None,
                                      "%d/%d" % (t["rate"], spf), None, None,
                                      None, pix="rgb24")))
    tmp = npy + ".part.npy"
    np.save(tmp, fr)
    os.replace(tmp, npy)
    return fr


VGA_SLOTS = {16: 0, 8: 0, 4: 0}    # a clip's own colours, by K (126.3.2):
#                         NONE unless asked (target.vga_clip_colours) - the
#                         owner, 2026-11-06: a clip's own browns dithered its
#                         background another way than the idle loop's, and
#                         footage that turns round its characters moves
#                         everywhere; every colour shared, chosen over the
#                         loop and every clip, keeps the joins one dither
PLAN_SAMPLES = 4000     # pixels a stream, for the choice
PLAN_IDLE_W = 3.0       # the idle loop is on the glass most of the play
PLAN_CANDS = 96


def vga_slots(proj, k=None):
    """How many of VGA's K colours are each clip's OWN (126.3.2): swapped in
    as the clip starts, the rest shared. target.vga_clip_colours, or none
    (VGA_SLOTS: the first plan's 4 of 16 and 1 of 8 redrew a join's whole
    background in another dither)"""
    t = proj.d["target"]
    k = k or t.get("vga_colours", 8)
    n = t.get("vga_clip_colours")
    if n is None:
        n = VGA_SLOTS.get(k, 0)
    return max(0, min(int(n), k - 4))


def vga_streams(proj):
    """What VGA's colours are chosen over: (key, source, first, end, weight)
    for the idle loop ("idle") and every video clip a build plays. A clip's
    key is its id: wherever it plays (an action, an opening, a loop cut
    from it) it has its own colours"""
    d = proj.d
    out = []
    i = d["idle"]
    if d["sources"].get(i["source"], {}).get("kind") not in (None, "scene"):
        a, b = i["loop"]
        out.append(("idle", i["source"], a, b, PLAN_IDLE_W))
    for cid in sorted(set(proj.clips_used())):
        c = d["clips"].get(cid)
        if c is None or d["sources"][c["source"]]["kind"] == "scene":
            continue
        out.append((cid, c["source"], c["in"], c["out"] + 1, 1.0))
    return out


def _lab2rgb8(C):
    """CIELAB (D65) rows to 8-bit sRGB through the DAC's six bits"""
    C = np.atleast_2d(C)
    fy = (C[:, 0] + 16) / 116
    fx, fz = fy + C[:, 1] / 500, fy - C[:, 2] / 200
    fi = lambda f: np.where(f ** 3 > 0.008856, f ** 3,
                            (f - 16 / 116) / 7.787)
    xyz = np.stack([fi(fx) * 0.95047, fi(fy), fi(fz) * 1.08883], -1)
    m = np.linalg.inv(np.array([[0.4124, 0.3576, 0.1805],
                                [0.2126, 0.7152, 0.0722],
                                [0.0193, 0.1192, 0.9505]]))
    c = np.clip(xyz @ m.T, 0, 1)
    c = np.where(c > 0.0031308, 1.055 * c ** (1 / 2.4) - 0.055, 12.92 * c)
    v6 = np.clip(np.rint(c * 63), 0, 63).astype(int)
    return [tuple(int((x << 2) | (x >> 4)) for x in v) for v in v6]


def _lum(c):
    return (77 * c[0] + 150 * c[1] + 29 * c[2], tuple(c))


def plan_colours(streams, k, slots, fixed, band=None, band_w=0.0,
                 no_own=()):
    """VGA's colours by ERROR (126.3.2, step 2): SHARED K - SLOTS entries
    over every stream, and SLOTS more for each. STREAMS: {key: ((N, 3)
    sRGB pixels, weight)}; FIXED: colours always shared (black, white; the
    band's four when kept); BAND: the band's colours, a stream of their own
    at BAND_W (the menu draws with the nearest shared). The error of a
    pixel is its squared CIELAB distance to the nearest colour it may use,
    weighted 1 + C*/8 as Video Encoder's colourN weights it (a red that no
    entry is near can never be dithered into being; a grey can); the
    shared are chosen greedily out of candidates spread over the footage
    (k-means++), each the one that most lowers the whole battle's error,
    then each stream's own by the same rule against the shared, then two
    rounds of each shared entry swapped for the best candidate given the
    streams' own, then three Lloyd steps (each entry the weighted mean of
    the pixels nearest it). Deterministic. Returns (shared, {key: own}),
    8-bit, shared darkest first with black first, each own darkest first.
    NO_OWN: streams that use the shared alone (the idle loop's)"""
    import os88cgacomp as cc
    keys = sorted(streams)
    if band:
        keys.append("\0band")
        streams = dict(streams)
        streams["\0band"] = (np.array(band, float), band_w)
    lab, wt = {}, {}
    for key in keys:
        px, w = streams[key]
        L = cc.srgb2lab(np.asarray(px, np.float64))
        lab[key] = L.astype(np.float32)
        wt[key] = ((1 + np.hypot(L[:, 1], L[:, 2]) / 8) * w /
                   max(1, len(L))).astype(np.float32)
    fixl = [cc.srgb2lab(np.array([c], np.float64))[0] for c in fixed]
    nshare = k - slots
    pool = np.concatenate([lab[x] for x in keys])
    pw = np.concatenate([wt[x] for x in keys]).astype(np.float64)
    rng = np.random.default_rng(1)
    cands = list(fixl)
    near = np.min([((pool - c) ** 2).sum(1) for c in cands], 0) \
        if cands else np.full(len(pool), 1e9)
    for _ in range(PLAN_CANDS):
        dd = near * pw
        if not dd.sum():
            break
        c = pool[rng.choice(len(pool), p=dd / dd.sum())]
        cands.append(c)
        near = np.minimum(near, ((pool - c) ** 2).sum(1))
    cands = np.array(cands, np.float32)
    D = {x: ((lab[x][:, None, :] - cands[None]) ** 2).sum(2) for x in keys}
    nf = len(fixl)
    shared = list(range(nf))
    own = {x: [] for x in keys}

    def floor(x, sh, ow):
        cols = sh + ow
        return D[x][:, cols].min(1) if cols else np.full(len(D[x]), 1e9,
                                                         np.float32)

    def pick(xs, base, n):
        """N more of the candidates for streams XS from floors BASE: the one
        that lowers their weighted error most, in turn"""
        got = []
        for _ in range(n):
            gain = sum((wt[x][:, None] * np.maximum(
                base[x][:, None] - D[x], 0)).sum(0) for x in xs)
            for j in got:
                gain[j] = -1
            j = int(np.argmax(gain))
            got.append(j)
            for x in xs:
                base[x] = np.minimum(base[x], D[x][:, j])
        return got
    own_keys = [x for x in keys if x != "\0band" and x not in no_own]
    if slots:                   # EACH STREAM'S OWN FIRST: what it alone
        for x in own_keys:      # wants most, so that the shared go to what
            own[x] = pick([x], {x: floor(x, shared, [])}, slots)  # the
    for rnd in range(3):        # streams have in common (shared first gave
        if rnd == 0:            # one clip's colour to all, and the others
            base = {x: floor(x, shared, own.get(x, []))     # went without)
                    for x in keys}
            shared += pick(keys, base, max(0, nshare - nf))
        else:                   # each shared entry swapped for the best
            for i in range(nf, len(shared)):    # candidate, the streams'
                rest = shared[:i] + shared[i + 1:]  # own as they are
                base = {x: floor(x, rest, own.get(x, [])) for x in keys}
                shared[i] = pick(keys, base, 1)[0]
        if slots:
            for x in own_keys:
                own[x] = pick([x], {x: floor(x, shared, [])}, slots)
    # Lloyd: each entry to the weighted mean of
    C = cands.astype(np.float64)    # the pixels nearest it
    sh = [C[j].copy() for j in shared]
    ow = {x: [C[j].copy() for j in own[x]] for x in own_keys}
    for _ in range(3):
        acc = [[np.zeros(3), 0.0] for _ in sh]
        oacc = {x: [[np.zeros(3), 0.0] for _ in ow[x]] for x in own_keys}
        for x in keys:
            pal = np.array(sh + ow.get(x, []))
            a = np.argmin(((lab[x][:, None, :] - pal[None]) ** 2).sum(2), 1)
            for j in range(len(pal)):
                m = a == j
                if not m.any():
                    continue
                s_ = (lab[x][m] * wt[x][m, None]).sum(0)
                w_ = float(wt[x][m].sum())
                tgt = acc[j] if j < len(sh) else oacc[x][j - len(sh)]
                tgt[0] += s_
                tgt[1] += w_
        for j in range(nf, len(sh)):
            if acc[j][1] > 0:
                sh[j] = acc[j][0] / acc[j][1]
        for x in own_keys:
            for j in range(len(ow[x])):
                if oacc[x][j][1] > 0:
                    ow[x][j] = oacc[x][j][0] / oacc[x][j][1]
    shr = [tuple(c) for c in fixed] + _lab2rgb8(np.array(sh[nf:])) \
        if len(sh) > nf else [tuple(c) for c in fixed]
    shr = sorted(shr[:nshare], key=lambda c: (c != (0, 0, 0),) + _lum(c))
    owr = {x: sorted(_lab2rgb8(np.array(ow[x])), key=_lum) if ow[x] else []
           for x in own_keys}
    return shr, owr


def vga_plan(proj, progress=_nop):
    """THE BATTLE'S VGA COLOURS (126.3.2): dict(k, slots, shared - the
    K - SLOTS every frame may use, black first and the rest darkest first
    - and own - {stream: its SLOTS}, "idle" the idle loop's, which the
    header's sixteen carry and every other is swapped for as its clip
    starts). Chosen by plan_colours over a sample of every stream; black
    and white always shared (black alone at 4); the band's four CGA colours
    too when target.vga_band_cga keeps them, else the band's colours are a
    stream of their own, weighted by the band's share of the screen. A
    battle with no video: the band's four, padded"""
    t = proj.d["target"]
    k = t.get("vga_colours", 8)
    slots = vga_slots(proj, k)
    band = [tuple(c) for c in palette_rgb(t["palette"])]
    keep = bool(t.get("vga_band_cga"))
    streams = vga_streams(proj)
    if not streams and not proj.d.get("images"):
        sh = (band + [(0, 0, 0)] * k)[:k - slots]
        return dict(k=k, slots=slots, shared=sh, own={})
    if keep and k <= 4:
        return dict(k=k, slots=0, shared=band[:k], own={})
    w, h, _ = rend_size(proj, "vga4")
    key = json.dumps(["p5", t["palette"], k, slots, keep, w, h,
                      [(im.get("path"), os.path.exists(proj.here(im["path"]))
                        and file_hash(proj.here(im["path"])))
                       for im in proj.d.get("images", [])],
                      fit_of(proj), scale_of(proj, "vga4"),
                      [(x, file_hash(proj.here(proj.d["sources"][sid]
                                               ["path"])), a, b, wt)
                       for x, sid, a, b, wt in streams]])
    if key in _PALS:
        return _PALS[key]
    rng = np.random.RandomState(1)
    samp = {}
    no_own = ["idle"]
    ref = None
    i_ = proj.d["idle"]
    if any(x == "idle" for x, _, _, _, _ in streams):
        ifr = rgb_frames(proj, i_["source"], w, h, "vga4", progress)
    for x, sid, a, b, wt in streams:
        fr = rgb_frames(proj, sid, w, h, "vga4", progress)[a:b]
        if not len(fr):
            continue
        px = fr.reshape(-1, 3)
        samp[x] = (px[rng.randint(0, len(px), min(len(px), PLAN_SAMPLES))],
                   wt)
        an = (proj.d["clips"].get(x) or {}).get("anchors") or []
        if x != "idle" and an and "idle" in samp and \
                0 <= an[0] < i_["loop"][1] - i_["loop"][0]:
            # AN ACTION'S OWN come from what MOVED from the idle loop's
            # screen at its anchor - its background is the loop's, drawn
            # in the shared colours (quant_split): own colours spent on it
            # redrew the whole background at every join (the owner)
            ref = ifr[i_["loop"][0] + an[0]]
            mv = moved_from(fr, ref).reshape(-1)
            no_own.append(x)
            if mv.any():
                mp = px[mv]
                samp[x + "*"] = (mp[rng.randint(0, len(mp), min(
                    len(mp), PLAN_SAMPLES))], wt * float(mv.mean()))
    fixed = [(0, 0, 0)] + ([(255, 255, 255)] if k > 4 else [])
    if keep:
        fixed = sorted(set(fixed) | set(band), key=lambda c: (
            c != (0, 0, 0),) + _lum(c))
    cr = t["canvas"][3]
    bw = 0.0 if keep else PLAN_IDLE_W * max(0, 200 - cr) / float(cr) * \
        (max(0, k - 4) / 12.0) ** 2     # (the menu's own colours: two of
    #       16, about one of 8, none of 4 - the owner's plan. At 8 the full
    #       pull cost the footage: a title's error 11.1 -> 13.1)
    # THE BAND'S STREAM: on VGA its text is white on its ground and red is
    # a grey-out (126.5), so of its four only the ground (0) and white (3)
    # are drawn - and the plain look's rules (1) when it has no picture -
    # and its PICTURES' pixels, drawn in the shared colours. Every one of
    # the four took an entry, and the owner saw red and teal kept as "menu"
    # that nothing drew
    imgs = proj.d.get("images", [])
    bpx = ([band[0], band[3]] + ([] if imgs else [band[1]])) * 64 \
        if not keep else []
    for im in imgs:
        try:
            from PIL import Image
            pim = Image.open(proj.here(im["path"])).convert("RGB")
            pim.thumbnail((96, 48))
            bpx += [tuple(c) for c in np.asarray(pim).reshape(-1, 3)]
        except OSError:
            pass
    if bpx and not bw:
        bw = PLAN_IDLE_W * max(0, 200 - cr) / float(cr) * \
            (max(1, k - 4) / 12.0) ** 2
    sh, own = plan_colours(samp, k, slots, fixed[:k - slots],
                           bpx or None, bw, no_own=tuple(no_own))
    for x in list(own):         # (an action's own, from its moved pixels)
        if x.endswith("*"):
            own[x[:-1]] = own.pop(x)
    sh = (sh + [sh[-1]] * k)[:k - slots]
    out = dict(k=k, slots=slots, shared=sh, own=own)
    _PALS[key] = out
    return out


def battle_palette(proj, k=None, progress=_nop):
    """The K colours the header carries (126.3.2), in index order: the
    shared, then the own slots - the shared's last, as nothing on the
    screen at the play's start uses them (the idle loop uses the shared
    alone, and every clip loads its own before it draws)"""
    p = vga_plan(proj, progress)
    return list(p["shared"]) + [p["shared"][-1]] * p["slots"]


def clip_palette(proj, cid, progress=_nop):
    """Clip CID's colours in index order: the shared, then its own - or the
    shared ALONE for a stream with none (the idle loop, a scene), so its
    frames never use an own slot (126.3.2)"""
    p = vga_plan(proj, progress)
    own = p["own"].get(cid)
    if not own:
        return list(p["shared"])
    return list(p["shared"]) + (list(own) + [own[-1]] * p["slots"])[
        :p["slots"]]


_PALS = {}


def band_index(proj, k=None):
    """The band's four CGA colours as indexes of the SHARED colours (126.3.2:
    a clip's own are swapped, the band's stay): FOUR DIFFERENT entries (the
    band's text, cursor and ground must stay apart), the assignment nearest
    in all (exact when target.vga_band_cga kept them). Fewer than four
    shared: the nearest each"""
    import itertools
    pal = np.array(vga_plan(proj)["shared"], float)
    band = np.array(palette_rgb(proj.d["target"]["palette"]), float)
    d = ((band[:, None, :] - pal[None]) ** 2).sum(2)        # 4 x S
    if len(pal) < 4:
        return [int(x) for x in d.argmin(1)]
    best = min(itertools.permutations(range(len(pal)), 4),
               key=lambda p: sum(d[i, p[i]] for i in range(4)))
    return list(best)


def _bayer4():
    m = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9],
                  [15, 7, 13, 5]], float)
    return (m + 0.5) / 16


def quant_vga(fr, pal, clip=0, seed=None, progress=_nop, what=""):
    """rgb24 frames to palette indexes - VIDEO PLAYER'S PATTERN DITHER
    (os88venc.KnollDitherer, SPEC.md 98.2.5), with its defaults: each
    pixel's plan is sixteen palette colours whose mean is the target, the
    4 x 4 Bayer cell picks one, and a pixel keeps its colour while what it
    was chosen for has moved less than `stable` (24) and the colour is still
    in its plan - so still footage costs nothing frame to frame. CLIP
    levels: a pixel that near a colour is that colour, solid.

    It replaces an ordered dither that shifted all three channels together
    and took the nearest by RGB: that only ever mixes towards a brightness,
    never between hues, so a blue between two palette entries came out a
    dither of red and yellow and a red came out brown (the owner, 2026-11-06:
    the Video Player's own encode of a clip "yields a drastically better
    colour set" - its dither as much as its palette)"""
    import os88venc as VE
    pal6 = bytes(int(v) >> 2 for c in pal for v in c)
    n, h, w, _ = fr.shape
    d = VE.KnollDitherer(w, h, pal6, KNOLL_STABLE, float(clip), KNOLL_HOLD,
                         KNOLL_SCORE, KNOLL_CAP, 0)
    if seed is not None:        # THE SCREEN BEFORE IT (Video Encoder's
        d.prev = np.asarray(seed[0]).astype(np.int64)  # --scene-hold): its
        d.at = np.asarray(seed[1], np.float32)  # indexes and their source,
        #                         so the hold keeps what is still near
    out = np.zeros((n, h, w), np.uint8)
    for i in range(n):
        if not i % 8:               # (~70 ms a frame here: a long clip is
            progress("dither", i, n, what)  # minutes - say how far)
        out[i] = d(np.ascontiguousarray(fr[i]))
    return out


MOVED_RGB = 24          # a clip's pixel is MOVED from the idle loop's screen
#                         when a channel of its source differs by more


def moved_from(fr, ref):
    """Frames FR's pixels MOVED from REF (the idle loop's source at the
    clip's anchor): a channel more than MOVED_RGB from it, grown a pixel
    each way so an edge is not left in another dither (126.3.2)"""
    m = (np.abs(np.asarray(fr, np.int16) - np.asarray(ref, np.int16)[None])
         .max(-1) > MOVED_RGB)
    g = m.copy()
    g[:, 1:] |= m[:, :-1]
    g[:, :-1] |= m[:, 1:]
    g[:, :, 1:] |= m[:, :, :-1]
    g[:, :, :-1] |= m[:, :, 1:]
    return g


def quant_split(fr, pal, nshared, clip, seed, progress=_nop, what=""):
    """An ACTION's frames (126.3.2): the idle loop's dither where its
    picture is the loop's, its own colours only where it MOVED. Two Knoll
    ditherers, both started from the loop's screen at the anchor: one in
    the shared colours alone - the loop's own, so a still background stays
    the loop's pattern and the join redraws what changed - and one in all
    its colours, taken where the source moved from the loop's"""
    a = quant_vga(fr, pal[:nshared], clip, seed, progress, what + " 1/2")
    b = quant_vga(fr, pal, clip, seed, progress, what + " 2/2")
    return np.where(moved_from(fr, seed[1]), b, a).astype(np.uint8)


KNOLL_STABLE = 24.0     # os88venc's --vga4-stable default
KNOLL_HOLD = 24.0       # --vga4-hold: Video Encoder's MEASURED pair with a
#                         score of 60. Off (0), a still picture with a little
#                         noise changed 21.9% of its pixels a frame - each a
#                         write of several planes the beam can catch half
#                         done (the owner's tearing "always in its bands");
#                         on, 1.1%, and a moving clip's error 2.3 -> 2.5
KNOLL_SCORE = 60.0      # ...--vga4-hold-score's
KNOLL_CAP = 64.0        # ...--vga4-plan-cap's


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


def rend_screens(proj, sid, kind, progress=_nop, full=False, stream=None,
                 rng=None, seed=None):
    """Source SID's frames in rendition KIND, as the adapter will show them:
    palette indexes for cga4 and vga4, bits for herc. FULL: over the whole
    screen (a full-screen opening), not the canvas. RNG (first, end): those
    frames alone; on VGA they are quantized on their own, in the colours of
    STREAM (a clip's id, or "idle"; 126.3.2)"""
    if kind == "cga4":
        sc = screens(proj, sid, progress, full)
        return sc[rng[0]:rng[1]] if rng else sc
    s = proj.d["sources"][sid]
    if s["kind"] == "scene":
        base = screens(proj, sid, progress)     # (CGA values: on VGA, each
        if kind == "vga4":                      # the nearest of the K)
            lut = np.array(band_index(proj), np.uint8)
            base = base[rng[0]:rng[1]] if rng else base
            return [lut[f] for f in base]
        _, crow, frow = rend_size(proj, "herc")
        cr = proj.d["target"]["canvas"][3]
        out = []
        for f in base:              # a WHOLE screen in two parts: its canvas
            if f.shape[0] == 200:   # rows onto Hercules' canvas, its band's
                out.append(np.vstack([herc_of_cga(f[:cr], crow),    # two
                                      herc_of_cga(f[cr:], frow - crow)]))
            else:                   # for one onto Hercules' band - scaled
                out.append(herc_of_cga(f, crow))    # whole, the picture ran
        return out[rng[0]:rng[1]] if rng else out   # into the band's rows
    w, h, fh = rend_size(proj, kind)
    if full:
        h = fh
    pct = scale_of(proj, kind)
    pw, ph, x0, y0 = boxed(w, h, pct)
    t = proj.d["target"]
    pal = clip_palette(proj, stream or "idle") if kind == "vga4" else None
    key = hashlib.sha1(json.dumps(
        ["q5", file_hash(proj.here(s["path"])), kind, w, h, t["palette"],
         KNOLL_STABLE, KNOLL_HOLD, KNOLL_SCORE, KNOLL_CAP, HOLD_LUM, pal,
         list(rng) if rng and kind == "vga4" else None,
         seed[2] if seed is not None else None] +
        (["cad1"] if cadence_on(proj) else []) +
        ([pct] if pct < 100 else []) +
        ([fit_of(proj)] if fit_of(proj) != "fill" else []) +
        [clip_of(proj)]).encode()).hexdigest()[:20]
    npy = os.path.join(cache_dir(proj), key + ".q.npy")
    if os.path.exists(npy):
        q = list(np.load(npy))
        return q if kind == "vga4" or not rng else q[rng[0]:rng[1]]
    fr = _rgb_n(proj, sid, pw, ph, kind, progress, full)
    if kind == "vga4" and rng:  # VGA: a STREAM's frames with its colours
        fr = fr[rng[0]:rng[1]]  # (126.3.2), quantized on their own
    what = "%s (%s)" % (stream or sid, {"vga4": "VGA", "herc": "Hercules"}
                        .get(kind, kind))
    progress("dither", 0, 1, what)
    nsh = len(vga_plan(proj)["shared"]) if kind == "vga4" else 0
    if kind != "vga4":
        q = quant_herc(fr, clip_of(proj))
    elif seed is not None and len(pal) > nsh:
        q = quant_split(fr, pal, nsh, clip_of(proj), seed, progress, what)
    else:
        q = quant_vga(fr, pal, clip_of(proj), seed, progress, what)
    if pct < 100:
        q = pad_into(q, w, h, x0, y0)
    np.save(npy + ".part.npy", q)
    os.replace(npy + ".part.npy", npy)
    q = list(q)
    return q if kind == "vga4" or not rng else q[rng[0]:rng[1]]


def _rgb_n(proj, sid, pw, ph, kind, progress, full):
    """Source SID's rgb frames at PW x PH, as many as the CGA encode's"""
    fr = rgb_frames(proj, sid, pw, ph, kind, progress)
    n = len(screens(proj, sid, progress, full))   # (the CGA encode's count)
    return fr[:n] if len(fr) >= n else np.concatenate(
        [fr, np.repeat(fr[-1:], n - len(fr), 0)])


def vga_alone(proj, cid, i, full=False, progress=_nop):
    """Frame I of clip CID in the SHARED colours alone (126.3.2): a clip's
    first frame, and its last before a seam onto the idle loop, are drawn
    so - the screen holds none of any clip's own colours when the DAC
    swaps them, so the swap changes nothing on the glass"""
    c = proj.d["clips"][cid]
    s = proj.d["sources"][c["source"]]
    if s["kind"] == "scene":
        return frames_of(proj, cid, progress, "vga4", full)[i]
    w, h, fh = rend_size(proj, "vga4")
    if full:
        h = fh
    pct = scale_of(proj, "vga4")
    pw, ph, x0, y0 = boxed(w, h, pct)
    fr = _rgb_n(proj, c["source"], pw, ph, "vga4", progress, full)
    j = min(c["in"] + i, len(fr) - 1)
    q = quant_vga(fr[j:j + 1], vga_plan(proj)["shared"], clip_of(proj))
    if pct < 100:
        q = pad_into(q, w, h, x0, y0)
    return q[0]


def frames_of(proj, cid, progress=_nop, kind="cga4", full=False,
              seed=False):
    """Clip CID's frames: its source's, in..out (FULL: the whole screen's) -
    on VGA quantized in the clip's own colours (126.3.2), and with SEED (an
    action's, entered from the idle loop) its dither STARTED FROM the idle
    loop's screen at its first anchor: the hold keeps every pixel still near
    its source as the loop had it, so the join redraws what changed and not
    a whole other dither of the same background (the owner, 2026-11-06:
    "they choose a completely different way of dithering")"""
    c = proj.d["clips"][cid]
    sd = idle_seed(proj, cid, progress) if seed and kind == "vga4" and \
        not full else None
    return rend_screens(proj, c["source"], kind, progress, full, cid,
                        (c["in"], c["out"] + 1), sd)


def idle_seed(proj, cid, progress=_nop):
    """The idle loop's screen at clip CID's first anchor, as quant_vga's
    SEED: (its indexes, its source's pixels, a tag for the cache) in the
    picture's box - or None (no anchor, no video idle loop)"""
    c = proj.d["clips"][cid]
    i = proj.d["idle"]
    an = c.get("anchors") or []
    if not an or proj.d["sources"][i["source"]]["kind"] == "scene":
        return None
    k = an[0]
    a, b = i["loop"]
    if not 0 <= k < b - a:
        return None
    w, h, _ = rend_size(proj, "vga4")
    pct = scale_of(proj, "vga4")
    pw, ph, x0, y0 = boxed(w, h, pct)
    q = idle_of(proj, progress, "vga4")[k]
    rgb = _rgb_n(proj, i["source"], pw, ph, "vga4", progress, False)[a + k]
    return (np.asarray(q)[y0:y0 + ph, x0:x0 + pw], rgb,
            ["idle", file_hash(proj.here(proj.d["sources"][i["source"]]
                                         ["path"])), a + k,
             clip_palette(proj, "idle")])


def idle_of(proj, progress=_nop, kind="cga4"):
    i = proj.d["idle"]
    a, b = i["loop"]
    return rend_screens(proj, i["source"], kind, progress, False, "idle",
                        (a, b))


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
    them around the loop, in frames (126.3)"""
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
        for per in a.get("by_target", {}).values():
            for pool in per.values():
                used |= set(pool)
    every = set(proj.clips_used())  # (a clip nothing plays is not built -
    ids = opened + [c for c in d["clips"] if c not in opened and   # battle_of
                    (c not in looped or c in used) and c in every]  # skips it)
    for c, cid in zip(clips, ids):
        c["id"] = cid
    for c, bc in zip(clips, b.clips):
        c["loop"] = bool(bc["flags"] & 32)      # (os88vbt.CF_LOOP)
        if c["loop"]:
            c["id"] = "%s (a loop)" % ("the title" if b.clips.index(bc) ==
                                       b.title else "an end loop")
    xms = proj.d["target"].get("xms_kb") or 0
    mem = memory(data, xms_kb=xms)
    mrate = 0 if mem["music_resident"] else b.rate * 1.0   # 8-bit mono
    for c in clips:
        c["with_music"] = (c["rate"] + mrate) / disk
        c["head_secs"] = min(c["bytes"], VB_HEAD) / disk
        c["fits_ring"] = mem["ring"] is not None and c["bytes"] <= mem["ring"]
    order = [n * 4 for _, n in getattr(b, "hold", [])]     # KB, in order
    holds = {}
    lens = [c["len"] for c in b.clips]
    for pool in sorted({896, 2944} | ({xms} if xms else set())):
        whole = len(data) // 1024 <= pool - 4   # (a 286's common pools,
        got, left = 0, pool - 4                 # 98.3.18.2.1, and the
        for kb in order:                        # target's own)
            take = min(kb, max(0, left))
            got += take
            left -= take
        holds[pool] = dict(whole=whole, kb=len(data) // 1024 if whole
                           else got, order_kb=sum(order),
                           cache=cache_plan(pool, len(data), len(b.idle_r),
                                            b.mlen, lens),
                           target=pool == xms)
    return dict(clips=clips, idle_bytes=len(b.idle_r), entries=b.ents_len,
                hold=holds,
                music=b.mlen, ui=len(b.uib), file=len(data), disk=disk,
                memory=mem, music_rate=mrate)


# THE ENGINE'S MEMORY (SPEC.md 126.4), carved as vb_load and vb_rclaim carve
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


VB_CMIN4 = 64           # the cache's least room for slots, 4 KBs (vbcache.inc)


def cache_plan(xms_kb, file_len, idle_len, mlen, clip_lens):
    """THE CLIP CACHE (SPEC.md 126.4.6) in a pool of XMS_KB, as vb_xopen
    and vb_cplan lay it out: None when it is off (no pool, or no room for
    the idle loop and VB_CMIN4 of slots); else its block, whether it is the
    whole file, the music held, the slots' room, and which clips (their
    lengths) fit it. Bytes throughout"""
    if not xms_kb:
        return None
    k4 = lambda n: (n + 4095) // 4096
    pool4 = xms_kb // 4 - 1
    if pool4 <= 0:
        return None
    blk4 = min(pool4, k4(file_len))
    idle4, mus4 = k4(idle_len), 0 if mlen <= VB_MRES else k4(mlen)
    big4 = max([k4(n) for n in clip_lens] or [0])
    if blk4 >= k4(file_len):
        return dict(block=blk4 * 4096, whole=True, music=bool(mus4),
                    room=blk4 * 4096, fits=list(clip_lens), big=big4 * 4096)
    if blk4 < idle4 + VB_CMIN4:
        return None
    base = idle4
    left = blk4 - idle4 - mus4
    music = bool(mus4) and left >= max(2 * big4, VB_CMIN4)
    if music:
        base += mus4
    room4 = blk4 - base
    return dict(block=blk4 * 4096, whole=False, music=music,
                room=room4 * 4096, big=big4 * 4096,
                fits=[n for n in clip_lens if k4(n) <= room4])


def memory(data, free_kb=None, xms_kb=0):
    """What the engine claims for bundle DATA (126.4), in bytes: the
    resident claim's parts, the card's ring, and the two clip rings a play
    is given out of FREE_KB (the largest run once it is loaded). "rings" is
    None when they would be under VB_CRMIN: Play says Not enough memory.
    With XMS_KB of XMS and room for the CACHE (126.4.6) the idle loop is
    in its block, not the claim, ring 0 takes the run and ring 1 the least
    - the rings only for a clip the cache has no slot for"""
    import os88vbt as B
    b = B.Bundle(data)
    cache = cache_plan(xms_kb, len(data), len(b.idle_r), b.mlen,
                       [c["len"] for c in b.clips])
    parts = resident_parts(len(b.vres), 0 if cache else len(b.idle_r),
                           len(b.uib), b.mlen, len(b.imgs), b.band[1],
                           b.vlayer)
    if cache:
        parts = [p for p in parts if p[0] != "the idle loop"]
    res = sum(n for _, n in parts)
    emin = (max(c["esz"] for c in b.clips) + 1023) // 1024 + 16
    ring = ring_of(res, b.maxrec, free_kb, cache=bool(cache), rmin=emin)
    if ring is not None and ring < emin * 1024:   # (vb_rmin: the largest
        ring = None                     # clip's entries and 16 KB past them)
    return dict(parts=parts, resident=res, card=VB_CARD * 1024,
                ring=ring, free=(free_kb or VB_FREE) * 1024, music_resident=b.mlen <=
                VB_MRES, cache=cache, guard=guard_kb(b.maxrec) * 1024)


def resident_parts(vres, idle, ui, mlen, imgs, band_row, vlayer=False):
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
             _r4k((200 - band_row) * 80) +  # 80 bytes, VBATTLE-RAM R1;
             _r4k(vband_bytes(band_row, vlayer)))]   # vb_gpara the ground's)


def vband_bytes(band_row, vlayer):
    """The band's GROUND buffer (vb_gpara): its rows at 80 bytes, and with
    VGA's own pictures (126.5) their layer after it, from a paragraph -
    four planes of 40 bytes a row"""
    n = 200 - band_row
    return ((n * 80 + 15) & ~15) + (n * 160 if vlayer else 0)


def guard_kb(maxrec):
    """A ring's GUARD (vb_load): the largest record, in 4 KB (126.4.6)"""
    return max(4, (maxrec + 4095) // 4096 * 4)


def ring_of(res, maxrec, free_kb=None, cache=False, rmin=0):
    """A clip ring's bytes (the smaller of the two, its guard out) beside
    a resident claim of RES bytes, out of FREE_KB - or None under VB_CRMIN,
    when Play says Not enough memory (vb_rclaim). With the CACHE (126.4.6)
    ring 0's: the run less ring 1's least and both guards"""
    if free_kb is None:
        free_kb = VB_FREE
    left = free_kb - (res + 1023) // 1024 - VB_CARD - VB_RKEEP
    guard = guard_kb(maxrec)
    if cache:
        ring = (left - (max(VB_CRMIN, rmin) + 3) // 4 * 4 - 4 -
                2 * guard) // 4 * 4     # (ring 1's least, up to 4 KB, slack)
    else:
        ring = (left // 2 - guard) // 4 * 4
    return ring * 1024 if ring >= VB_CRMIN else None


def cap_hold(ring, income, fps, srate):
    """What a ring of RING bytes holds ahead of a clip's SOUND, for the
    builder's reader's model (126.4.3, VBATTLE-RAM-PLAN R4): the ring less
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
    """The openings' sound as the engine plays it (126.3): each one's own,
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
    music half of it (126.2) - so the join is heard as it will be"""
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


# --- LEVELS: every sound as loud as the music (SPEC.md 126.3) --------------------
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
