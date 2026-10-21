#!/usr/bin/env python3
"""The project guide's video previews: each demo .V88 as a small H.264 MP4.

    python3 tools/guide/vidpreview.py            # make what is missing or stale
    python3 tools/guide/vidpreview.py --check    # list them; exit 1 if any is owed
    python3 tools/guide/vidpreview.py --force apps/video/os8088.v88

A .V88 plays on an os8088 machine and nowhere else, so the guide shows a
browser-playable copy beside each one. The copies are COMMITTED, unlike the
screenshots: making them takes ffmpeg and minutes per video, and a reader of
the guide should not need either. tools/guide/videos/previews.json maps each
source file's SHA-256 to its copy, so the guide finds a preview by the
CONTENT of a file on a disk, wherever that disk puts it. A re-encoded demo
video has a new hash and no preview until this is run again;
tools/guidelock.py reports it.

The picture is what the encoder's own window previews (os88vencgui.render):
the file's frames as its adapter shows them, at the screen's proportions,
scaled up by a whole number with no smoothing so the pixels stay pixels. The
sound is the file's own - PCM, ADPCM, or the PC speaker line rebuilt as the
speaker would drive it.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
vid = gui = None                # os88vid and os88vencgui, loaded when encoding


def _load():
    global vid, gui
    if vid is None:
        import os88vid
        import os88vencgui
        vid, gui = os88vid, os88vencgui

OUT = os.path.join(HERE, "videos")
MANIFEST = os.path.join(OUT, "previews.json")
CRF_MONO = 40                   # H.264 quality: dither is noise to it, and a
CRF_COLOUR = 38                 # one-bit picture keeps its look at 40


def sources():
    """Every .V88 the guide's disks carry: the demo drives' and the logo."""
    out = [os.path.join("apps", "video", "os8088.v88")]
    vd = os.path.join(ROOT, "apps", "video", "demo")
    for a in sorted(os.listdir(vd)):
        d = os.path.join(vd, a)
        if os.path.isdir(d):
            out += [os.path.join("apps", "video", "demo", a, f)
                    for f in sorted(os.listdir(d)) if f.upper().endswith(".V88")]
    return out


def sha(path):
    return hashlib.sha256(open(os.path.join(ROOT, path), "rb").read()).hexdigest()


def mp4_name(src):
    parts = src.split(os.sep)
    sub = parts[-2] if parts[-3:-2] == ["demo"] else "logo"
    return "%s/%s.mp4" % (sub, os.path.splitext(parts[-1])[0].lower())


def load():
    try:
        return json.load(open(MANIFEST))
    except (OSError, ValueError):
        return {}


def save(m):
    with open(MANIFEST, "w") as f:
        json.dump(dict(sorted(m.items(), key=lambda kv: kv[1]["mp4"])), f, indent=1)
        f.write("\n")


def sound_wav(r, path):
    """The file's sound as a 16-bit WAV, or False for a silent one."""
    import numpy as np
    if not r.abytes:
        return False
    raw = b"".join(r.sound(f) for f in range(r.frames))
    if r.spk:
        vid.write_spk_preview(path, raw, r.rate, r.spkp, fs=22050)
        return True
    if r.audio == vid.AUD_ADPCM4:
        raw = bytes(vid.adpcm4_decode(raw))
    pcm = (np.frombuffer(raw, np.uint8).astype(np.int16) - 128) * 256
    w = wave.open(path, "wb")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(r.rate)
    w.writeframes(pcm.astype("<i2").tobytes())
    w.close()
    return True




def frame_img(r, surf, f):
    """Frame f as stored - the screen's pixels with no aspect correction,
    which the MP4 carries as its sample aspect instead, so a 640 x 200
    picture is 640 x 200 pixels of video and not 800 x 600."""
    keep = r.aspect
    r.aspect = (1, 1)
    try:
        return gui.render(r, surf, f).convert("RGB")
    finally:
        r.aspect = keep


def encode(src, dst, crf):
    """src (.V88, relative to ROOT) -> dst (.mp4). Returns (w, h, seconds)
    with (w, h) the size the picture is SHOWN at."""
    import numpy as np
    path = os.path.join(ROOT, src)
    r = vid.Reader(path)
    f0 = vid.first_shown(r)
    fps = r.fps
    w0 = h0 = None
    for f, surf, rec, at, i in vid.v88_frames(vid.Reader(path)):
        if f == f0:
            w0, h0 = frame_img(r, surf, f).size
            break
    an, ad = r.aspect
    # A one-bit picture has no colour to lose, so it is coded as it is. A
    # colour one under 640 wide is doubled first: H.264 keeps colour at half
    # the resolution, which would smear a single coloured pixel. At 640 the
    # colour is already coarser than that (a text cell, a composite dot).
    k = 1 if r.pixfmt == vid.PF_MONO1 or w0 >= 640 else 2
    crf = crf if crf else CRF_MONO if r.pixfmt == vid.PF_MONO1 else CRF_COLOUR
    w, h = w0 * k, h0 * k
    w, h = w + (w & 1), h + (h & 1)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with tempfile.TemporaryDirectory() as td:
        wav = os.path.join(td, "a.wav")
        has = sound_wav(r, wav)
        cmd = ["ffmpeg", "-loglevel", "error", "-y",
               "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "%dx%d" % (w0, h0),
               "-r", "%.6f" % fps, "-i", "-"]
        if has:
            cmd += ["-i", wav]
        cmd += ["-vf", "scale=%d:%d:flags=neighbor,setsar=%d/%d,format=yuv420p" % (w, h, an, ad),
                "-c:v", "libx264", "-preset", "slow", "-crf", str(crf), "-tune", "animation",
                "-movflags", "+faststart"]
        if has:
            cmd += ["-c:a", "aac", "-b:a", "48k", "-ac", "1", "-shortest"]
        cmd.append(dst)
        p = subprocess.Popen(cmd, stdin=subprocess.PIPE)
        rr = vid.Reader(path)
        n = 0
        for f, surf, rec, at, i in vid.v88_frames(rr):
            if f < f0:
                continue
            img = frame_img(rr, surf, f)
            if img.size != (w0, h0):
                img = img.resize((w0, h0))
            p.stdin.write(np.asarray(img, np.uint8).tobytes())
            n += 1
        p.stdin.close()
        if p.wait():
            raise SystemExit("vidpreview: ffmpeg failed on %s" % src)
    return round(w * an / ad), h, round(n / fps, 1), make_poster(src, dst)


def make_poster(src, dst):
    """THE STILL a browser shows before Play: the video's own poster frame
    (the one Video Player shows when the file opens), at the screen's
    proportions - a poster is drawn at its own shape, not the video's.
    Returns its file name, beside dst."""
    from PIL import Image
    path = os.path.join(ROOT, src)
    r = vid.Reader(path)
    f0 = vid.first_shown(r)
    pf = max(f0, r.keys[r.poster][0] if r.nkeys and r.poster != 0xFFFF else f0)
    for f, surf, rec, at, i in vid.v88_frames(r):
        if f == pf:
            still = frame_img(r, surf, f)
            break
    an, ad = r.aspect
    w0, h0 = still.size
    sc = max(1, -(-400 // h0))
    still = still.resize((round(w0 * an / ad) * sc, h0 * sc), Image.NEAREST)
    if r.pixfmt == vid.PF_MONO1:
        pst = dst[:-4] + ".png"
        still.convert("1").save(pst, optimize=True)
    else:
        pst = dst[:-4] + ".jpg"
        still.save(pst, quality=85, optimize=True)
    return os.path.basename(pst)


def owed():
    """The sources with no preview made from their current bytes."""
    m = load()
    return [s for s in sources() if sha(s) not in m]


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true", help="list, make nothing; exit 1 if owed")
    ap.add_argument("--crf", type=int, default=0, help="H.264 quality, overriding the per-format default: lower is better and bigger")
    ap.add_argument("--force", nargs="*", help="re-make these sources (all, with none named)")
    a = ap.parse_args(argv)
    m = load()
    want = {sha(s): s for s in sources()}
    owed = [s for h, s in want.items() if h not in m]
    if a.check:
        for h, s in want.items():
            print("%s %s" % ("ok  " if h in m else "OWED", s))
        return 1 if owed else 0
    if a.force is not None:
        owed = a.force or list(want.values())
    if owed:
        _load()
    for s in owed:
        h = sha(s)
        dst = mp4_name(s)
        r = vid.Reader(os.path.join(ROOT, s))
        print("vidpreview: %s -> tools/guide/videos/%s ..." % (s, dst), flush=True)
        w, ht, secs, pst = encode(s, os.path.join(OUT, dst), a.crf)
        for old in [k for k, v in m.items() if v["src"] == s]:
            del m[old]
        m[h] = {"src": s, "mp4": dst, "poster": os.path.dirname(dst) + "/" + pst,
                "w": w, "h": ht, "secs": secs,
                "title": r.title, "kb": os.path.getsize(os.path.join(OUT, dst)) // 1024}
        save(m)
        print("vidpreview:   %dx%d, %.1f s, %d KB" % (w, ht, secs, m[h]["kb"]), flush=True)
    for h, v in m.items():
        if not v.get("poster") and h in want:
            _load()
            v["poster"] = os.path.dirname(v["mp4"]) + "/" + make_poster(v["src"], os.path.join(OUT, v["mp4"]))
            print("vidpreview: poster %s" % v["poster"], flush=True)
    live = set(want)
    for gone in [k for k in m if k not in live]:
        for f in ("mp4", "poster"):
            p = os.path.join(OUT, m[gone].get(f, ""))
            if os.path.isfile(p) and all(v.get(f) != m[gone].get(f) for k, v in m.items() if k != gone):
                os.remove(p)
        del m[gone]
    save(m)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
