#!/usr/bin/env python3
"""os88spkfx - the SPEAKER SHAPER's reference (docs/plans/completed/SPEAKER-PCM-PLAN.md).

    python3 tools/os88spkfx.py counts IN.WAV OUT.RAW [--rate R] [--preemph M]
    python3 tools/os88spkfx.py preview IN.WAV OUT.WAV [--rate R] [--host]
    python3 tools/os88spkfx.py bands IN.WAV [--rate R]
    python3 tools/os88spkfx.py gen OUT.INC
    python3 tools/os88spkfx.py --selfcheck

A 5150's speaker plays a straight 8-bit wave as the carrier and nothing else
(SPEC.md 98.2.15.1): the pulse width is spent on bass a 2 1/4-inch cone cannot
move. The Video Player's encoder shapes the sound on the host; Audio and
Tracker cannot - they are handed what they are handed - so the shaping moves
onto the machine, in the only form an 8088 can afford beside a pulse every
~600 cycles. This file is that shaper to the byte: apps/os88spkfx.inc does
exactly what `Shaper` does, and the gates compare the machine's port writes
against it.

THE SHAPER, per sample (x unsigned 8-bit at the SPEAKER's rate):
  1. PRE-EMPHASIS (the high-pass and the tilt, 98.2.15.1's step 1): the
     first difference, i = (prev - x + 256) >> 1 - +6 dB an octave, the bass
     gone and the top lifted (and the wave inverted, which no ear hears), for
     three instructions (`xchg`, `sub`, `rcr`).
     Tracker does its own at LOAD, on the instrument samples, so it asks for
     none here (PRE_NONE) - a linear filter commutes with the mix.
  2. ONE TABLE of a family of LEVELS, TT[l][i]: gain 2^(l/3) (0..20 dB in 2 dB
     steps), a soft clip (tanh to z = 1, flat beyond - the encoder's
     tanh(y)/tanh(1) with its clip), and the speaker's own count table
     (34.11.2) - so gain, drive, clip and count are one `xlatb`.
  3. THE CARRIER PUT AWAY IN THE QUIET (98.2.15.3): a centre shift d
     subtracted from the count, saturating at 1, moved once a SUB-BLOCK
     toward the headroom the level leaves - slowly up, quickly down.
The LEVEL is chosen once a SPAN (a block of ~32 ms) from the span's OWN peak,
read one sample in 32 before any of it is emitted: a leveller whose attack
is instant (an onset is never driven into the clip) and whose release is 2 dB
a span, ratio `RATIO`, at most 20 dB, and silence (a peak under `PGATE`)
left silent with the carrier put away.

Everything the machine needs that is not arithmetic on a sample - the tanh
curve, the level gains, the peak-to-level table - is GENERATED here (`gen`)
into apps/os88spkfx_t.inc, and tests/unit/t_spkfx.py holds the two equal.
"""
import argparse
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

PIT_HZ = 1193182
NLEV = 11                       # levels, 2 dB apart: 0..20 dB - ratio 3
                                # takes a peak at the gate to 20 dB, so
                                # the rows above would never be read
LEV_DB = 2.0
CURVE_N = 129                   # the soft clip's curve, z = m/128, m 0..128
RATIO = 3.0                     # the leveller's ratio (98.2.15.1's lifted)
ZT = 2.0                        # z at a loud block's peak, level 0: how
                                # hard the loudest parts drive the clip
PFULL = 96                      # ...a "loud" peak, in index units (of 128)
PGATE = 3                       # a block peaking under this is SILENCE
SPARSE = 32                     # the peak is read one sample in this many
SUB = 16                        # samples a sub-block: d moves once a sub-block
HOLD = 2                        # a span's level is asked by the largest peak
                                # of it and the HOLD spans before it (34.11.9)
D_UP, D_DOWN = 1, 8             # ...by at most this many counts
RTOL = 3                        # the ratchet steps down for a span that
                                # overdrives it by more than this many levels
PRE_NONE, PRE_DIFF = 0, 1


def curve():
    """The soft clip: C[m] = round(127 tanh(m/128) / tanh(1)), m 0..128 -
    so C[128] = 127, and any z past 1 is 127 (the encoder's clip)"""
    t1 = math.tanh(1.0)
    return [min(127, int(round(127.0 * math.tanh(m / 128.0) / t1)))
            for m in range(CURVE_N)]


def kbase():
    """K: at level 0 a peak of PFULL reaches z = ZT"""
    return ZT * 128.0 / PFULL


def gains():
    """GF[l] = round(256 K 2^(l LEV_DB / 6.02)): m = (a GF[l]) >> 8 for a
    sample a index units from the centre"""
    k = kbase()
    return [int(round(256.0 * k * 10 ** (l * LEV_DB / 20.0)))
            for l in range(NLEV)]


def ltab():
    """LT[p], p 0..128: the level a block whose peak is p asks for - the
    gain that brings p to PFULL, to the power (1 - 1/RATIO), in LEV_DB
    steps, 0..NLEV-1; a peak under PGATE is silence and asks for 0"""
    out = []
    for p in range(129):
        if p < PGATE:
            out.append(0)
            continue
        db = 20.0 * math.log10(PFULL / float(p)) * (1.0 - 1.0 / RATIO)
        out.append(max(0, min(NLEV - 1, int(round(db / LEV_DB)))))
    return out


def spk_n(rate):
    return PIT_HZ // rate


def count_table(n):
    """34.11.2's t[s] = 1 + s(N-2)/255, apps/os88spk.inc's os88spk_init"""
    return [1 + s * (n - 2) // 255 for s in range(256)]


def family(n):
    """TT[l][i] - built on the machine by os88spkfx_init exactly so"""
    t, c, gf = count_table(n), curve(), gains()
    fam = []
    for l in range(NLEV):
        row = []
        for i in range(256):
            a = abs(i - 128)
            m = min(CURVE_N - 1, (a * gf[l]) >> 8)
            y = c[m]
            row.append(t[128 + y] if i >= 128 else t[128 - y])
        fam.append(row)
    return fam


def block_for(rate):
    """samples a block: ~32 ms, a power of two"""
    return 256 if rate <= 11025 else 512


class Shaper(object):
    """The machine's shaper, state and all, in the machine's two calls.
    level(span) is handed a SPAN of source before any of it is emitted and
    decides that span's level and the carrier's target from its own peak -
    so the gain is right from the span's first sample and an onset is never
    driven into the clip. emit(piece) then translates the span, in as many
    pieces as the producer likes (a ring's wrap splits one): the decisions
    are the span's, the sub-block count runs on across the pieces"""

    def __init__(self, rate, pre=PRE_DIFF, idle=True):
        self.n = spk_n(rate)
        self.fam = family(self.n)
        self.lt = ltab()
        self.B = block_for(rate)
        self.pre, self.idle = pre, idle
        self.c0 = self.fam[0][128]
        self.lev = 0
        self.gate = True                        # a play starts as after silence
        self.rat = 0                            # levelled (1 ratchet, 2 frozen)
        self.held = [0] * HOLD                  # the last HOLD spans' peaks
        self.d = self.c0 - 1 if idle else 0     # the carrier starts away
        self.h = self.d
        self.sub = 0
        self.prev = 128

    def index(self, x, prev):
        """the table's index: PRE_DIFF is prev - x, not x - prev - the
        wave inverted, which no ear hears, and what lets the machine keep
        the previous sample in AH for nothing (os88spkfx_emit)"""
        if self.pre == PRE_DIFF:
            return (prev - x + 256) >> 1
        return x

    def ratchet(self, lev):
        """os88spkfx_ratchet: ONE level for the piece, stepping down only,
        a step for a span that overdrives it by more than RTOL levels
        (SPEC.md 34.11.9.1)"""
        self.lev, self.rat = lev, 1

    def freeze(self, lev):
        """os88spkfx_rat = 2: the level is the user's, and steps no more
        (Tracker's volume bar, SPEC.md 45.25.3)"""
        self.lev, self.rat = lev, 2

    def level(self, xs):
        """the span's peak, one sample in SPARSE from its first, of the
        index the table is read at; the level the largest of it and the
        last HOLD spans' peaks asks for (a rise of one step a span at most, a
        fall at once - and after a SILENT span, the level it asks for at once:
        nothing to step from); and the carrier's target, from this span's own
        peak"""
        p, prev = 0, self.prev
        for j in range(0, len(xs), SPARSE):
            a = abs(self.index(xs[j], xs[j - 1] if j else prev) - 128)
            if a > p:
                p = a
        if self.rat:                            # the ratchet: down only, a
            if (self.rat == 1 and p >= PGATE and      # step a span; frozen,
                    self.lt[p] + RTOL < self.lev):    # not at all
                self.lev -= 1
        else:
            want = self.lt[max([p] + self.held) if p >= PGATE else p]
            self.held = [p] + self.held[:-1]
            self.lev = (self.lev + 1 if want > self.lev and not self.gate
                        else want)
        self.gate = p < PGATE
        row = self.fam[self.lev]
        if p < PGATE:
            h = self.c0 - 1
        else:
            q = row[min(255, 128 + p)] - row[128]
            q += (q >> 2) + 2
            h = max(0, self.c0 - 1 - q)
        self.h = h if self.idle else 0
        self.sub = 0
        return p

    def emit(self, xs):
        out = bytearray()
        row = self.fam[self.lev]
        for x in xs:
            if self.sub == 0:
                if self.d < self.h:
                    self.d += min(D_UP, self.h - self.d)
                elif self.d > self.h:
                    self.d -= min(D_DOWN, self.d - self.h)
            self.sub = (self.sub + 1) % SUB
            i = self.index(x, self.prev)
            self.prev = x
            c = row[i] - self.d
            out.append(c if c >= 1 else 1)
        return bytes(out)

    def feed(self, xs):
        """a stream, in spans of B - what Audio and Tracker hand it"""
        out = []
        for j in range(0, len(xs), self.B):
            self.level(xs[j:j + self.B])
            out.append(self.emit(xs[j:j + self.B]))
        return b"".join(out)


# --------------------------------------------------------------------------
# resampling: what Audio does between a file's rate and the speaker's
# --------------------------------------------------------------------------
RATE_MAX_8088 = 8000
RATE_MAX_AT = 24858             # a pulse of 48 counts (SPEC.md 34.11.8)


def plan_rate(src, at=False):
    """(speaker rate, k, stepping): the rate a file of `src` Hz plays at on
    an 8088 (`at` False) or a 286 and up. An integer k is a box average of
    k samples; stepping (k = 0) walks the source 16.16 at src/rate"""
    top = RATE_MAX_AT if at else RATE_MAX_8088
    if src <= top:
        return src, 1
    k = -(-src // top)              # the least k that brings it under
    if src % k == 0:
        return src // k, k
    return top, 0


def resample(xs, src, rate, k):
    xs = bytes(xs)
    if k == 1:
        return xs
    if k:
        return bytes(sum(xs[j:j + k]) // k
                     for j in range(0, len(xs) - k + 1, k))
    step = (src << 16) // rate
    out, pos = bytearray(), 0
    while (pos >> 16) < len(xs):
        out.append(xs[pos >> 16])
        pos += step
    return bytes(out)


# --------------------------------------------------------------------------
# the tables apps/os88spkfx.inc assembles
# --------------------------------------------------------------------------
def gen_inc():
    c, gf, lt = curve(), gains(), ltab()
    lines = ["; apps/os88spkfx_t.inc - GENERATED by tools/os88spkfx.py gen;",
             "; do not edit. tests/unit/t_spkfx.py holds it to the model.",
             "SPKFX_NLEV    equ %d" % NLEV,
             "SPKFX_PGATE   equ %d" % PGATE,
             "SPKFX_SPARSE  equ %d" % SPARSE,
             "SPKFX_SUB     equ %d" % SUB,
             "SPKFX_DUP     equ %d" % D_UP,
             "SPKFX_DDOWN   equ %d" % D_DOWN,
             "SPKFX_RTOL    equ %d" % RTOL,
             "os88spkfx_curve:"]
    for j in range(0, CURVE_N, 16):
        lines.append("    db " + ", ".join(str(v) for v in c[j:j + 16]))
    lines.append("os88spkfx_gf:")
    lines.append("    dw " + ", ".join(str(v) for v in gf))
    lines.append("os88spkfx_lt:")
    for j in range(0, 129, 16):
        lines.append("    db " + ", ".join(str(v) for v in lt[j:j + 16]))
    return "\n".join(lines) + "\n"


# --------------------------------------------------------------------------
# listening at the desk: the speaker line, and where its power goes
# --------------------------------------------------------------------------
BANDS = ((20, 150), (150, 400), (400, 800), (800, 1600), (1600, 2700))


def bands(counts, rate):
    """98.2.15.1's table: each band's share of the 20 Hz - 20 kHz power of
    the speaker LINE the counts drive (os88vid.spk_preview), and the
    carrier's, in dB"""
    import numpy as np
    import os88vid
    y = os88vid.spk_preview(counts, rate)
    Y = np.abs(np.fft.rfft(y)) ** 2
    f = np.fft.rfftfreq(len(y), 1.0 / 44100)
    tot = Y[(f >= 20) & (f <= 20000)].sum()
    out = []
    for lo, hi in BANDS:
        out.append(10 * math.log10(Y[(f >= lo) & (f < hi)].sum() / tot))
    cr = PIT_HZ / float(spk_n(rate))
    out.append(10 * math.log10(Y[(f >= cr - 50) & (f < cr + 50)].sum()
                               / tot))
    return out


def host_counts(xs, rate):
    """the encoder's own shaping (98.2.15.1), for comparison"""
    import os88vid
    return bytes(os88vid.spk_shape(xs, rate)).translate(
        bytes(count_table(spk_n(rate))))


def load(path, rate=None, at=False):
    import os88vid
    src, xs = os88vid.read_wav(path)
    r, k = plan_rate(src, at) if rate is None else (rate, 0)
    if rate is not None and src % rate == 0:
        k = src // rate
    return r, resample(xs, src, r, k)


# --------------------------------------------------------------------------
# TRACKER'S LOAD-TIME FILTER (SPEC.md 45.25): the natural style's high-pass
# (98.2.15.1), paid once over the module's SAMPLES rather than per output
# sample - a linear filter commutes with the mix. A one-pole DC blocker,
# y = (x - x_prev) + 7/8 y_prev, in the sample's own time base: its corner
# is ~190 Hz for a sample played at C-2 (8,363 Hz) and moves with the note.
# Integer, exactly as apps/tracker/trkspk.inc's tsp_natural runs it, with
# Y = 128 y so the output y/2 is Y's high byte: for x in -128..127, |y| is
# at most 255 (y = x - 1/8 sum 7/8^(k-1) x[n-k]), so Y fits a word and the
# output a signed byte with no clamp
#   Y = Y - (Y >> 3) + 128 (x - x_prev),  out = Y >> 8
# (Python's >> is the 8086's `sar`: both floor)
# --------------------------------------------------------------------------
def tracker_hp(data):
    """one sample's signed 8-bit bytes through tsp_natural, from rest"""
    out = bytearray(len(data))
    y = xp = 0
    for i, b in enumerate(data):
        x = b - 256 if b > 127 else b
        y = y - (y >> 3) + (x - xp) * 128
        xp = x
        out[i] = (y >> 8) & 0xFF
    return bytes(out)


BSHIFT, BKICK = 4, 2            # the bass x16 into +-127, a drum x4 (45.25.4)


def tracker_natural(data):
    """one sample as tsp_natural leaves it (SPEC.md 45.25.4), exactly: the
    load filter; and a sample it took ~5/7 of - 7 sum |y| under 2 sum |x|,
    each over one byte in 8 from the first - squared up, by its zero
    crossings (sign changes of y from a
    non-negative start). Under len / 16 is a BASS: y << BSHIFT clamped at
    +-127, then two passes of y = y - (y >> 2) + (x - x_prev), out clamped to
    +-127. More is a DRUM: y << BKICK clamped at +-min(2P, 127), P its
    filtered peak, then the load filter again"""
    sgn = lambda b: b - 256 if b > 127 else b
    y = tracker_hp(data)
    ay = [abs(sgn(b)) for b in y]
    if not ay or 7 * sum(ay[::8]) >= 2 * sum(abs(sgn(b)) for b in data[::8]) \
            or not max(ay):
        return y
    zc, neg = 0, False
    for b in y:
        if (sgn(b) < 0) != neg:
            zc, neg = zc + 1, not neg
    sq = lambda sh, lim: bytes(max(-lim, min(lim, sgn(b) << sh)) & 0xFF
                               for b in y)
    if zc >= len(y) >> 4:                       # a DRUM
        return tracker_hp(sq(BKICK, min(2 * max(ay), 127)))
    out = sq(BSHIFT, 127)                       # a BASS
    for _ in range(2):
        nxt, v, xp = bytearray(len(out)), 0, 0
        for i, b in enumerate(out):
            x = sgn(b)
            v = v - (v >> 2) + (x - xp)
            xp = x
            nxt[i] = max(-127, min(127, v)) & 0xFF
        out = bytes(nxt)
    return out


# --------------------------------------------------------------------------
# a WAV made for the speaker on the host: Audio copies it (SPEC.md 86.21.1).
# The ENCODER makes them - `os88venc.py IN OUT.WAV`, the same shaping and
# options a speaker .V88 gets - and the format is os88vid's; these names are
# what the gates spell it with
# --------------------------------------------------------------------------
SPK_SHAPED, SPK_COUNTS = 1, 2   # shaped PCM8 / the speaker's counts themselves


def write_wav(path, rate, data, kind=0, n=0):
    """os88vid.write_spk_wav, which is the format (`n` is implied by the
    rate: it is kept for the callers that pass it)"""
    import os88vid
    os88vid.write_spk_wav(path, rate, data, kind)


def cmd_counts(a):
    r, xs = load(a.src, a.rate, a.at)
    c = Shaper(r, a.preemph, not a.noidle).feed(xs)
    open(a.out, "wb").write(c)
    print("os88spkfx: %s: %d counts at %d Hz" % (a.out, len(c), r))


def cmd_preview(a):
    import os88vid
    r, xs = load(a.src, a.rate, a.at)
    xs = xs[:int(r * a.secs)] if a.secs else xs
    if a.host:
        c = host_counts(xs, r)
    elif a.raw:
        c = xs.translate(bytes(count_table(spk_n(r))))
    else:
        c = Shaper(r, a.preemph, not a.noidle).feed(xs)
    s = os88vid.write_spk_preview(a.out, c, r)
    print("os88spkfx: %s: %.1f s of the speaker line at %d Hz" % (a.out, s, r))


def cmd_bands(a):
    r, xs = load(a.src, a.rate, a.at)
    xs = xs[:int(r * a.secs)] if a.secs else xs
    rows = [("raw", xs.translate(bytes(count_table(spk_n(r))))),
            ("host shaped", host_counts(xs, r)),
            ("machine", Shaper(r, PRE_DIFF).feed(xs)),
            ("machine, no idle", Shaper(r, PRE_DIFF, False).feed(xs)),
            ("machine, no pre", Shaper(r, PRE_NONE).feed(xs))]
    print("%-18s" % ("%d Hz" % r) + "".join("%9s" % ("%d-%d" % b)
                                               for b in BANDS) + "  carrier")
    for name, c in rows:
        print("%-18s" % name + "".join("%9.1f" % v for v in bands(c, r)))


def selfcheck():
    ok = True
    for rate in (5512, 7350, 8000, 11025, 22050):
        n = spk_n(rate)
        for l, row in enumerate(family(n)):
            if min(row) < 1 or max(row) > n - 1:
                print("selfcheck: level %d at %d Hz leaves 1..N-1" % (l, rate))
                ok = False
            if any(row[j] > row[j + 1] for j in range(255)):
                print("selfcheck: level %d not monotonic" % l)
                ok = False
    # a span emitted in pieces is the span emitted whole
    import random
    rnd = random.Random(8088)
    xs = bytes(rnd.randrange(256) for _ in range(5000))
    whole = Shaper(8000).feed(xs)
    s, parts = Shaper(8000), []
    for j in range(0, len(xs), s.B):
        span = xs[j:j + s.B]
        s.level(span)
        k = rnd.randrange(1, len(span) + 1)
        parts.append(s.emit(span[:k]) + s.emit(span[k:]))
    if b"".join(parts) != whole:
        print("selfcheck: pieces differ from the whole")
        ok = False
    # silence plays at a count of 1 once the carrier is away
    if set(Shaper(8000).feed(bytes([128]) * 4096)[-512:]) != {1}:
        print("selfcheck: silence is not a count of 1")
        ok = False
    for src, want in ((8000, (8000, 1)), (11025, (8000, 0)),
                      (16000, (8000, 2)), (22050, (7350, 3)),
                      (44100, (7350, 6)), (32000, (8000, 4))):
        if plan_rate(src) != want:
            print("selfcheck: plan_rate(%d) = %r, not %r"
                  % (src, plan_rate(src), want))
            ok = False
    for src, want in ((22050, (22050, 1)), (44100, (22050, 2)),
                      (32000, (16000, 2))):
        if plan_rate(src, True) != want:
            print("selfcheck: plan_rate(%d, AT) = %r, not %r"
                  % (src, plan_rate(src, True), want))
            ok = False
    print("os88spkfx: selfcheck %s" % ("ok" if ok else "FAILED"))
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--selfcheck", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    for name, fn in (("counts", cmd_counts), ("preview", cmd_preview),
                     ("bands", cmd_bands)):
        p = sub.add_parser(name)
        p.add_argument("src")
        if name != "bands":
            p.add_argument("out")
        p.add_argument("--rate", type=int)
        p.add_argument("--at", action="store_true",
                       help="plan the rate for a 286 or better")
        p.add_argument("--secs", type=float, default=0)
        p.add_argument("--preemph", type=int, default=PRE_DIFF)
        p.add_argument("--noidle", action="store_true")
        p.add_argument("--host", action="store_true")
        p.add_argument("--raw", action="store_true")
        p.set_defaults(fn=fn)
    g = sub.add_parser("gen")
    g.add_argument("out")
    a = ap.parse_args()
    if a.selfcheck:
        sys.exit(0 if selfcheck() else 1)
    if a.cmd == "gen":
        open(a.out, "w").write(gen_inc())
        return
    if not a.cmd:
        ap.print_help()
        return
    a.fn(a)


if __name__ == "__main__":
    main()
