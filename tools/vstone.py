#!/usr/bin/env python3
"""Count the breaks in a recording of VIDSND's tone rows (key T).

    python3 tools/vstone.py RECORDING [--freq 634.9] [--plot OUT.png]
    python3 tools/vstone.py --selftest

tests/vidbench/vidsnd.asm's T plays a 634.9 Hz sine through the Sound
Blaster (22,222 Hz, 35 samples a period) in ten 5 s rows, each after 2 s of
silence. The card's own interrupt (30 a second) is the holder in A to H,
IRQ0 (the timer, 18.2 a second - where Video Battle decodes) in I and J:

    A  the tone alone                      F  tracks, holding 16 ms
    B  holding 16 ms, no disk              G  one sector a call, holding 16 ms
    C  whole tracks read, no hold          H  as G, the holder's line masked
    D  tracks, holding 4 ms                   at the 8259 for each call
    E  tracks, holding 8 ms                I  IRQ0 holding 16 ms, one sector
                                           J  as I, IRQ0 masked for each call

RECORDING is anything ffmpeg reads (a phone's .m4a, a .wav, a MartyPC
capture). The rows are found by their gaps. In each, the tone is mixed down
to its own frequency and smoothed over one period: a clean tone is a steady
amplitude and a phase that drifts in a straight line. A HOLE is a run of
the amplitude under half the row's median - the card's output gone silent -
and a JUMP the phase moving more than 0.6 rad in one period with the
amplitude up: a piece of output dropped or played twice. Reported per row:
holes, holes a second, the median and longest hole, the milliseconds under
half amplitude in all, and jumps.

What the rows decide (docs/reports/PICOMEM2-STUTTER-2026-11-07.md): holes in
C to F while A and B are clean are the card's mixer starving while its
int 13h waits on the PC - the firmware's; a hole every few ms in C is the
8088's sector copy outlasting the card's 5 ms of mixed sound, and one per
hold in F and G is the hold landing in a copy. J clean where I holes is a
cure os8088 can apply for itself: one sector a call, the timer masked.
"""
import argparse
import math
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np

FREQ = 1000000.0 / 45 / 35          # the DSP's 22,222 Hz over 35 samples
ROWS = "ABCDEFGHIJ"
LABELS = {
    "A": "the tone alone",
    "B": "hold 16 ms, no disk",
    "C": "tracks, hold 0",
    "D": "tracks, hold 4 ms",
    "E": "tracks, hold 8 ms",
    "F": "tracks, hold 16 ms",
    "G": "1 sector/call, hold 16 ms",
    "H": "1 sector/call, line masked, hold 16 ms",
    "I": "IRQ0 holds 16 ms, 1 sector/call",
    "J": "IRQ0 holds 16 ms, 1 sector, IRQ0 masked",
}


def load(path):
    """(rate, mono float64) - through ffmpeg unless it is a WAV we read"""
    if path.lower().endswith(".wav"):
        try:
            return load_wav(path)
        except Exception:
            pass
    ff = shutil.which("ffmpeg")
    if not ff:
        sys.exit("vstone: %s needs ffmpeg (or give a .wav)" % path)
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "a.wav")
        subprocess.run([ff, "-loglevel", "error", "-i", path, "-ac", "1",
                        "-ar", "48000", "-c:a", "pcm_s16le", out], check=True)
        return load_wav(out)


def load_wav(path):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import sndcheck
    rate, vals, _ = sndcheck.load(path)
    return rate, np.asarray(vals, dtype=np.float64)


def envelope(x, rate, win_ms=5.0):
    n = max(1, int(rate * win_ms / 1000))
    k = len(x) // n
    e = np.sqrt(np.mean(x[:k * n].reshape(k, n) ** 2, axis=1))
    return e, n


def segments(x, rate, min_gap=1.0, min_len=2.0):
    """[(start, end)] sample ranges of tone between gaps of min_gap s"""
    e, n = envelope(x, rate)
    if not len(e):
        return []
    loud = np.percentile(e, 90)
    on = e > max(loud * 0.2, 1e-4)
    # close gaps shorter than min_gap: a break inside a row is not a row end
    segs, start, last_on = [], None, None
    gapw = int(min_gap * 1000 / 5.0)
    for i, v in enumerate(on):
        if v:
            if start is None:
                start = i
            elif i - last_on > gapw:
                segs.append((start, last_on + 1))
                start = i
            last_on = i
    if start is not None:
        segs.append((start, last_on + 1))
    return [(a * n, b * n) for a, b in segs if (b - a) * 5.0 / 1000 >= min_len]


def demod(x, rate, freq):
    """amplitude and unwrapped phase of x at freq, smoothed over a period"""
    t = np.arange(len(x)) / rate
    z = x * np.exp(-2j * np.pi * freq * t)
    per = max(1, int(round(rate / freq)))
    k = np.ones(per) / per
    z = np.convolve(z, k, mode="same")
    z = np.convolve(z, k, mode="same")      # twice: the 2f image is gone
    return np.abs(z) * 2, np.unwrap(np.angle(z)), per


def best_freq(x, rate, around):
    """the tone's frequency in this recording, to a few hundredths of a Hz:
    a phone's or an emulator's clock is not the card's"""
    best, bf = -1, around
    for f in np.linspace(around * 0.97, around * 1.03, 241):
        t = np.arange(len(x)) / rate
        p = abs(np.dot(x, np.exp(-2j * np.pi * f * t)))
        if p > best:
            best, bf = p, f
    return bf


def breaks(x, rate, freq):
    """(hole lengths in ms, hole start times in s, ms under half amplitude,
    phase jumps) over one row"""
    amp, ph, per = demod(x, rate, freq)
    t = np.arange(len(ph))
    trim = slice(per * 4, len(ph) - per * 4)    # the row's own start and end
    if trim.stop - trim.start < per * 8:
        return [], [], 0.0, 0
    a, b = np.polyfit(t[trim], ph[trim], 1)     # the row's drift taken out
    res = ph - (a * t + b)
    med = np.median(amp[trim])
    low = amp < med * 0.5
    low[:trim.start] = False
    low[trim.stop:] = False
    d = np.diff(np.concatenate(([0], low.astype(np.int8), [0])))
    st, en = np.flatnonzero(d == 1), np.flatnonzero(d == -1)
    holes = list((en - st) * 1000.0 / rate)
    starts = list(st / rate)
    jump = np.zeros(len(res), dtype=bool)
    jump[per:] = np.abs(res[per:] - res[:-per]) > 0.6
    near = np.convolve(low.astype(np.int8), np.ones(per * 2), mode="same") > 0
    jump &= ~near                       # a hole's edges are not a jump
    jump[:trim.start] = False
    jump[trim.stop:] = False
    nj, last = 0, -10 ** 9
    for i in np.flatnonzero(jump):
        if i - last > rate * 0.020:
            nj += 1
        last = i
    return holes, starts, float(sum(holes)), nj


def analyse(rate, x, freq, quiet=False):
    segs = segments(x, rate)
    want = len(ROWS)
    if not quiet:
        print("vstone: %.1f s at %d Hz, %d tone rows found (%d expected)"
              % (len(x) / rate, rate, len(segs), want))
    out = []
    if len(segs) > want:                # leading noise: keep the last ones
        segs = segs[-want:]
    whole = np.concatenate([x[a:b] for a, b in segs]) if segs else x
    f = best_freq(whole[:int(rate * 4)], rate, freq) if len(whole) else freq
    if not quiet:
        print("vstone: the tone is at %.2f Hz here (the card's %.2f)\n"
              % (f, freq))
        print("  row  %-40s %5s %6s %6s %6s %6s %7s %5s"
              % ("", "secs", "holes", "per s", "median", "most", "ms low",
                 "jumps"))
    for i, (a, b) in enumerate(segs):
        holes, starts, lowms, nj = breaks(x[a:b], rate, f)
        secs = (b - a) / rate
        name = ROWS[i] if len(segs) == want else "%d" % (i + 1)
        out.append((name, secs, holes, starts, lowms, nj))
        if not quiet:
            print("  %-4s %-40s %5.2f %6d %6.1f %6.1f %6.1f %7.1f %5d"
                  % (name, LABELS.get(name, ""), secs, len(holes),
                     len(holes) / secs,
                     float(np.median(holes)) if holes else 0.0,
                     max(holes) if holes else 0.0, lowms, nj))
    return out


def selftest():
    """a synthetic recording of the eight rows, with breaks put in on
    purpose: the tool must find exactly those"""
    rate = 48000
    rng = np.random.default_rng(1)
    want = [0, 0, 0, 3, 6, 12, 12, 0, 4, 0]
    parts = [np.zeros(rate)]
    for n in want:
        t = np.arange(int(rate * 5)) / rate
        y = 0.3 * np.sin(2 * np.pi * FREQ * t)
        at = np.sort(rng.choice(np.arange(int(rate * 0.3), int(rate * 4.7),
                                          int(rate * 0.3)), n, replace=False))
        for j, s in enumerate(at):
            if j % 2:                   # a dropout: 5 ms of silence
                y[s:s + int(rate * 0.005)] = 0
            else:                       # a repeat: 1.3 ms played again
                L = int(rate * 0.0013)
                y[s + L:] = np.concatenate([y[s:s + L], y[s + L:-L]])
        y += rng.normal(0, 0.003, len(y))
        parts += [y, np.zeros(rate * 2)]
    x = np.concatenate(parts)
    got = [len(h) + nj for _, _, h, _, _, nj in
           analyse(rate, x, FREQ, quiet=True)]
    print("vstone selftest: breaks put in %s, found %s" % (want, got))
    return 0 if got == want else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("recording", nargs="?")
    ap.add_argument("--freq", type=float, default=FREQ)
    ap.add_argument("--times", action="store_true",
                    help="list each hole: its time in its row (its ms)")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.recording:
        ap.error("a recording, or --selftest")
    rate, x = load(a.recording)
    rows = analyse(rate, x, a.freq)
    if a.times:
        for name, _, holes, starts, _, _ in rows:
            print("  %s: %s" % (name, " ".join(
                "%.3f(%.1f)" % (t, h) for t, h in zip(starts, holes))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
