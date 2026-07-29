#!/usr/bin/env python3
"""Assert what a QEMU wav-audiodev capture actually contains. Stdlib only.

    python3 tools/sndcheck.py <wav> <expected_hz> [options]
    python3 tools/sndcheck.py <wav> --expect-silence

QEMU quirks this tool absorbs (learned building the Phase 1 harness,
docs/SOUND-PLAN.md):
  - The wav backend often leaves the RIFF/data size fields ZERO (the
    daemonized QEMU exits without the fixup), so the header is parsed by
    hand and a zero data size means "to end of file".
  - The pcspk stream only runs while the speaker gate is on, so file time
    is speaker-on time, not wall time: a clean boot with no beep yields an
    EMPTY capture (that is the snd_live-gate assertion, --expect-silence),
    and any boot noise would precede the expected tone inside the file.

Checks:
  1. --expect-silence: the capture holds no active block at all (an empty
     or header-only file passes - nothing ever opened the speaker).
  2. otherwise: at least one active region (block RMS above --min-rms)
     exists; with --quiet-before T the first T seconds must be silent.
  3. the first active region's dominant frequency (Goertzel scan) is
     within --tol of <expected_hz>.
  4. --exclusive: no activity exists outside that first region - the
     expected tone is the only sound the machine ever made.

Exit 0 with a one-line report per check, exit 1 with a diagnosis.
"""
import argparse
import math
import struct
import sys

BLOCK_MS = 10.0


def load(path):
    """Hand-parse a RIFF/WAVE file: (rate, [floats -1..1] mono, duration).

    Tolerates the zeroed size fields QEMU's wav audiodev leaves behind.
    """
    raw = open(path, "rb").read()
    if len(raw) < 12 or raw[0:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise SystemExit("sndcheck: %s: not a RIFF/WAVE file" % path)
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(raw):
        cid = raw[pos:pos + 4]
        sz = struct.unpack("<I", raw[pos + 4:pos + 8])[0]
        body = raw[pos + 8:pos + 8 + sz] if sz else raw[pos + 8:]
        if cid == b"fmt ":
            fmt = body[:16]
            pos += 8 + ((sz + 1) & ~1 if sz else len(body))
        elif cid == b"data":
            data = body                 # sz == 0: QEMU never fixed it up -
            break                       # take everything to end of file
        else:
            if sz == 0:
                break
            pos += 8 + ((sz + 1) & ~1)
    if fmt is None:
        raise SystemExit("sndcheck: %s: no fmt chunk" % path)
    _, nch, rate, _, _, bits = struct.unpack("<HHIIHH", fmt)
    data = data or b""
    if bits == 16:
        data = data[:len(data) & ~1]
        vals = struct.unpack("<%dh" % (len(data) // 2), data)
        scale = 32768.0
    elif bits == 8:
        vals = [b - 128 for b in data]
        scale = 128.0
    else:
        raise SystemExit("sndcheck: unsupported bit depth %d" % bits)
    if nch > 1:
        vals = [sum(vals[i:i + nch]) / nch for i in range(0, len(vals), nch)]
    mono = [v / scale for v in vals]
    return rate, mono, len(mono) / rate if rate else 0.0


def block_rms(mono, rate):
    """[(start_s, rms)] over BLOCK_MS windows."""
    step = max(1, int(rate * BLOCK_MS / 1000.0))
    out = []
    for i in range(0, len(mono), step):
        blk = mono[i:i + step]
        if not blk:
            break
        out.append((i / rate, math.sqrt(sum(v * v for v in blk) / len(blk))))
    return out


def goertzel(x, rate, freq):
    """Power of x at freq."""
    w = 2.0 * math.pi * freq / rate
    c = 2.0 * math.cos(w)
    s1 = s2 = 0.0
    for v in x:
        s0 = v + c * s1 - s2
        s2 = s1
        s1 = s0
    return s1 * s1 + s2 * s2 - c * s1 * s2


def dominant(x, rate, lo, hi, step):
    """(freq, power) of the strongest bin in a Goertzel scan."""
    mean = sum(x) / len(x)
    x = [v - mean for v in x]           # kill DC so bin 0 leakage can't win
    if len(x) > 8192:                   # bound the O(n*m) scan
        x = x[:8192]
    best = (0.0, -1.0)
    f = lo
    while f <= hi:
        p = goertzel(x, rate, f)
        if p > best[1]:
            best = (f, p)
        f += step
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("freq", nargs="?", type=float,
                    help="expected dominant frequency, Hz")
    ap.add_argument("--tol", type=float, default=0.05,
                    help="relative frequency tolerance (default 0.05)")
    ap.add_argument("--min-rms", type=float, default=0.02,
                    help="block RMS above this = activity")
    ap.add_argument("--silence-floor", type=float, default=0.005,
                    help="block RMS below this = silence")
    ap.add_argument("--quiet-before", type=float, default=None, metavar="T",
                    help="assert silence for the first T seconds")
    ap.add_argument("--exclusive", action="store_true",
                    help="assert no activity outside the first burst")
    ap.add_argument("--expect-silence", action="store_true",
                    help="assert the capture holds no sound at all")
    a = ap.parse_args()

    rate, mono, dur = load(a.wav)
    blocks = block_rms(mono, rate)
    print("sndcheck: %s: %.2fs @ %d Hz, %d blocks" %
          (a.wav, dur, rate, len(blocks)))

    if a.expect_silence:
        loud = [(t, r) for t, r in blocks if r > a.silence_floor]
        if loud:
            print("sndcheck: FAIL: expected silence, but rms=%.4f at %.2fs" %
                  (loud[0][1], loud[0][0]))
            return 1
        peak = max((r for _, r in blocks), default=0.0)
        print("sndcheck: OK: no sound captured (peak block rms %.4f)" % peak)
        return 0

    if a.freq is None:
        ap.error("expected frequency required unless --expect-silence")
    if not blocks:
        print("sndcheck: FAIL: empty capture - the tone never sounded")
        return 1

    # 1. the quiet window
    if a.quiet_before is not None:
        bad = [(t, r) for t, r in blocks
               if t < a.quiet_before and r > a.silence_floor]
        if bad:
            print("sndcheck: FAIL: noise before %.2fs: rms=%.4f at %.2fs" %
                  (a.quiet_before, bad[0][1], bad[0][0]))
            return 1
        print("sndcheck: OK: silent through %.2fs" % a.quiet_before)

    # 2. find the first active region after the quiet window
    start = a.quiet_before or 0.0
    active = [(t, r) for t, r in blocks if t >= start and r > a.min_rms]
    if not active:
        print("sndcheck: FAIL: no activity after %.2fs "
              "(peak block rms %.4f)" %
              (start, max((r for t, r in blocks if t >= start), default=0.0)))
        return 1
    t0 = active[0][0]
    t1 = t0
    for t, _ in active:                 # extend through the contiguous burst
        if t - t1 > 3 * BLOCK_MS / 1000.0:
            break
        t1 = t
    t1 += BLOCK_MS / 1000.0
    print("sndcheck: OK: activity %.2fs..%.2fs (peak rms %.4f)" %
          (t0, t1, max(r for t, r in active)))

    # 3. dominant frequency of that region
    seg = mono[int(t0 * rate):int(t1 * rate)]
    lo = max(40.0, a.freq / 4.0)
    hi = a.freq * 4.0
    step = max(2.0, a.freq * a.tol / 4.0)
    f, p = dominant(seg, rate, lo, hi, step)
    if abs(f - a.freq) > a.tol * a.freq:
        print("sndcheck: FAIL: dominant %.1f Hz, expected %.1f Hz +/-%.0f%%" %
              (f, a.freq, a.tol * 100))
        return 1
    print("sndcheck: OK: dominant %.1f Hz (expected %.1f, +/-%.0f%%)" %
          (f, a.freq, a.tol * 100))

    # 4. nothing else ever sounded
    if a.exclusive:
        stray = [(t, r) for t, r in active if t > t1]
        if stray:
            print("sndcheck: FAIL: activity outside the burst: rms=%.4f "
                  "at %.2fs" % (stray[0][1], stray[0][0]))
            return 1
        print("sndcheck: OK: no sound outside %.2fs..%.2fs" % (t0, t1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
