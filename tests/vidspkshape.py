#!/usr/bin/env python3
"""vidspkshape - a speaker clip's sound SHAPED for the speaker (SPEC.md
98.2.15.1), on the host.

A straight encode of music for the PC speaker spends the pulse width on
bass the cone cannot move, and on the owner's 5150 that left the song 23-28
dB under the carrier's whine: loud, and nothing but the whine. This row
makes a clip of a loud 60 Hz bass and a quiet 880 Hz line and asserts:

  1. `os88venc --audio speaker` (shaping on, the default) puts the line at
     least 25 dB higher and the bass at least 20 dB lower than
     `--spk-shape off` does, measured off the counts in the file;
  2. `os88vid speaker` on the unshaped file does the same after the fact,
     changes no byte outside the frame records' sound, and leaves a file
     that verifies.

Broken on purpose (spk_shape_f returning its input's scaling, or
spk_reshape writing nothing) it fails at 1 or 2. Needs ffmpeg and numpy.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88vid as vid                                         # noqa: E402

try:
    import numpy as np
except ImportError:
    sys.exit("vidspkshape: needs numpy")


def levels(path):
    """(the 60 Hz bass, the 880 Hz line) in dB of full-scale power, off the
    counts the file carries"""
    r = vid.Reader(path)
    c = b"".join(rec[-r.abytes:] for rec, _, _ in r.records())
    s = np.frombuffer(vid.spk_samples(c, r.rate), dtype=np.uint8) \
        .astype(float) - 128
    X = np.abs(np.fft.rfft(s)) ** 2
    f = np.fft.rfftfreq(len(s), 1.0 / r.rate)
    full = len(s) ** 2 * 127 ** 2 / 2

    def db(lo, hi):
        return 10 * np.log10(X[(f >= lo) & (f < hi)].sum() / full + 1e-12)
    return db(50, 70), db(860, 900)


def main():
    if not shutil.which("ffmpeg"):
        sys.exit("vidspkshape: needs ffmpeg")
    bad = []
    tmp = tempfile.mkdtemp(prefix="vidspkshape")
    try:
        src = os.path.join(tmp, "src.mp4")
        subprocess.run(
            ["ffmpeg", "-v", "error", "-nostdin", "-y", "-f", "lavfi", "-i",
             "testsrc2=s=320x200:r=10:d=6", "-f", "lavfi", "-i",
             "sine=f=60:d=6", "-f", "lavfi", "-i", "sine=f=880:d=6",
             "-filter_complex", "[1:a]volume=0.9[b];[2:a]volume=0.08[m];"
             "[b][m]amix=inputs=2:normalize=0[a]", "-map", "0:v", "-map",
             "[a]", "-c:v", "libx264", "-c:a", "aac", src], check=True)
        out = {}
        for sh in ("on", "off"):
            out[sh] = os.path.join(tmp, "t_%s.v88" % sh)
            subprocess.run(
                [sys.executable, os.path.join(ROOT, "tools", "os88venc.py"),
                 src, out[sh], "--preset", "herc", "--box", "160x58",
                 "--audio", "speaker", "--fps", "5", "--spk-shape", sh,
                 "--quiet"], check=True)
        on, off = levels(out["on"]), levels(out["off"])
        print("   1: encoded - bass %.1f -> %.1f dB, line %.1f -> %.1f dB"
              % (off[0], on[0], off[1], on[1]))
        if on[1] - off[1] < 25:
            bad.append("1: the line rose %.1f dB, not 25" % (on[1] - off[1]))
        if off[0] - on[0] < 20:
            bad.append("1: the bass fell %.1f dB, not 20" % (off[0] - on[0]))

        after = os.path.join(tmp, "t_after.v88")
        subprocess.run([sys.executable,
                        os.path.join(ROOT, "tools", "os88vid.py"), "speaker",
                        out["off"], after], check=True,
                       stdout=subprocess.DEVNULL)
        aft = levels(after)
        print("   2: after the fact - bass %.1f -> %.1f dB, line %.1f -> "
              "%.1f dB" % (off[0], aft[0], off[1], aft[1]))
        if aft[1] - off[1] < 25 or off[0] - aft[0] < 20:
            # (the bass bar is 20: from the file's own 8-bit counts, the
            # leveller lifts their rounding a little - -45 dB of full scale,
            # where the broken shaper moved the bass the WRONG way)
            bad.append("2: os88vid speaker did not shape the sound")
        a, b = open(out["off"], "rb").read(), open(after, "rb").read()
        r = vid.Reader(out["off"])
        sound = bytearray(len(a))
        at, nsec = r.sp0, r.sp0n
        while nsec:
            nf, nxt = struct.unpack_from("<HH", a, at)
            o = at + 4
            for i in range(nf):
                m = struct.unpack_from("<H", a, o)[0]
                sound[o + m - r.abytes:o + m] = b"\1" * r.abytes
                o += m
            at, nsec = at + nsec * vid.SECTOR, nxt
        if len(a) != len(b):
            bad.append("2: the file changed size")
        else:
            moved = sum(1 for i in range(len(a))
                        if a[i] != b[i] and not sound[i])
            print("   2: %d bytes changed outside the sound" % moved)
            if moved:
                bad.append("2: %d bytes outside the sound changed" % moved)
        v = subprocess.run([sys.executable,
                            os.path.join(ROOT, "tools", "os88vid.py"),
                            "verify", after], capture_output=True, text=True)
        if v.returncode:
            bad.append("2: the shaped file does not verify: %s"
                       % v.stderr.strip())
    finally:
        shutil.rmtree(tmp, True)
    for b in bad:
        print("FAIL", b)
    print("vidspkshape: %s" % ("FAIL" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
