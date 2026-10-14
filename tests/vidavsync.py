#!/usr/bin/env python3
"""The encoder's sound on its picture's timeline - SPEC.md 98.2.1.7.

    python3 tests/vidavsync.py

Host-side. ffmpeg builds a six-second source with a white FLASH on the
picture and a 1 kHz BEEP on the sound at the same instant, 2.0 s, and then
two copies of it whose streams start apart - the PICTURE half a second late
and the SOUND half a second late - each with its timestamps true, so every
one of the three plays in step in any player that honours them. Each is
read through tools/os88venc.py's own two pipes, `ffmpeg_video` and
`ffmpeg_audio`, from the start and from `--start 1.0`, and the flash's
frame must land within two frames (83 ms) of the beep's sample.

The late-SOUND source is the case: a raw pipe carries no timestamps, and
before 98.2.1.7 the sound's dropped its leading gap and played 0.45 s
early - the owner's report off a downloaded clip. Broken on purpose
(`first_pts=0` taken out of ffmpeg_audio) it FAILS on that source alone,
at -0.500 s. SKIPS without ffmpeg.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))

FPS = 24
RATE = 11025
AT = 2.0                # the flash and the beep
TOL = 2.0 / FPS         # within two frames


def ff(*args):
    subprocess.run(["ffmpeg", "-v", "error", "-nostdin", "-y"] + list(args),
                   check=True)


def sources(tmp):
    """{name: path}: in step, the picture late, the sound late"""
    sync = os.path.join(tmp, "sync.mp4")
    ff("-f", "lavfi", "-i",
       "color=black:size=160x120:rate=%d:duration=6,format=gray,"
       "geq=lum='if(between(T\\,%g\\,%g)\\,255\\,16)'" % (FPS, AT, AT + 0.08),
       "-f", "lavfi", "-i",
       "aevalsrc='if(between(t\\,%g\\,%g)\\,0.8*sin(2*PI*1000*t)\\,0)'"
       ":s=22050:d=6" % (AT, AT + 0.08),
       "-c:v", "libx264", "-g", "240", "-pix_fmt", "yuv420p",
       "-c:a", "pcm_s16le", sync.replace(".mp4", ".mkv"))
    sync = sync.replace(".mp4", ".mkv")
    # each stream's first half second cut away and its timestamps moved on
    # by as much, so the content keeps its place on the timeline
    v05 = os.path.join(tmp, "v05.mkv")
    a05 = os.path.join(tmp, "a05.mkv")
    ff("-i", sync, "-an", "-vf", "trim=start=0.5", "-fps_mode", "passthrough",
       "-c:v", "libx264", "-g", "240", v05)
    ff("-i", sync, "-vn", "-af", "atrim=start=0.5", "-c:a", "pcm_s16le", a05)
    vlate = os.path.join(tmp, "vlate.mkv")
    alate = os.path.join(tmp, "alate.mkv")
    ff("-itsoffset", "0.5", "-i", v05, "-i", sync, "-map", "0:v",
       "-map", "1:a", "-c", "copy", vlate)
    ff("-i", sync, "-itsoffset", "0.5", "-i", a05, "-map", "0:v",
       "-map", "1:a", "-c", "copy", alate)
    return [("in step", sync), ("picture late", vlate),
            ("sound late", alate)]


def main():
    if not shutil.which("ffmpeg"):
        print("vidavsync: SKIP - no ffmpeg")
        return 0
    import numpy as np
    import os88venc as venc
    bad = []
    with tempfile.TemporaryDirectory() as tmp:
        for name, path in sources(tmp):
            for start in (None, 1.0):
                t0 = AT - (start or 0)
                fr = [f.mean() for f in venc.ffmpeg_video(
                    path, 80, 60, None, str(FPS), start, None, None)]
                hit = [i for i, v in enumerate(fr) if v > 128]
                a = np.frombuffer(venc.ffmpeg_audio(
                    path, RATE, start, None, None), np.uint8).astype(int) - 128
                loud = np.nonzero(np.abs(a) > 30)[0]
                if not hit or not len(loud):
                    bad.append("%s: no flash or no beep" % name)
                    continue
                flash, beep = hit[0] / float(FPS), loud[0] / float(RATE)
                print("   %-12s from %-4s flash %.3f s, beep %.3f s: the "
                      "sound %+.3f s" % (name, "%gs" % start if start else
                                         "0", flash, beep, beep - flash))
                if abs(beep - flash) > TOL:
                    bad.append("%s from %s: the sound %+.3f s off its "
                               "picture" % (name, start or 0, beep - flash))
                if abs(flash - t0) > TOL:
                    bad.append("%s from %s: the flash at %.3f s, not %.3f"
                               % (name, start or 0, flash, t0))
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("\n   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
