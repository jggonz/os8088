#!/usr/bin/env python3
# =============================================================================
# os8088 - apps/arcade/arcsfx.py - Space Invaders' sounds, SYNTHESISED
# (docs/ARCADE-SPEC.md section 8.3)
#
#     python3 apps/arcade/arcsfx.py OUT.PRT [--wav DIR]
#
# The real board's sounds are analog circuits, and emulators play somebody's
# recordings of them. ARCADE ships none: every clip here is computed from a
# short description of what its circuit does - a noise burst through a
# falling filter, a triangle swept by a slower one - with a fixed noise seed,
# so the part is the same bytes on every build. Nothing is read from a ROM.
#
# The part: 'AS', the clip count, then per clip its offset and its length
# (words), then the samples - signed 8-bit at 8,000 Hz, peaks within +-42 so
# that three sounding at once cannot wrap the mixer's byte (8.1).
# --wav writes each clip as a WAV too, for listening on the host.
# =============================================================================
import math
import os
import struct
import sys

RATE = 8000
PEAK = 42
# The clip order is the package's (arcsnd.inc's inv_cbits and lr_cbits):
# the saucer, a shot, the base hit, an invader hit, an extra base, the
# fleet's four steps, the saucer hit - Space Invaders' and Part II's - then
# Lunar Rescue's own: the thrust, a footstep high and low, the count of the
# men saved, the rescue ship's beep (it shares the shot, the hits and the
# extra ship).
NAMES = ('ufo', 'shot', 'basehit', 'invhit', 'extra',
         'fleet1', 'fleet2', 'fleet3', 'fleet4', 'ufohit',
         'thrust', 'stephi', 'steplo', 'count', 'rescue')


class Noise:
    """A 17-bit LFSR, seeded: white noise in -1..1, the same every build."""
    def __init__(self, seed=0x1ACE5):
        self.s = seed

    def __call__(self):
        b = ((self.s >> 0) ^ (self.s >> 3)) & 1
        self.s = (self.s >> 1) | (b << 16)
        return (self.s & 0xFF) / 127.5 - 1.0


def lowpass(x, fc):
    """One pole, cutoff fc Hz - fc may be a list, a cutoff a sample."""
    out, y = [], 0.0
    for i, v in enumerate(x):
        f = fc[i] if isinstance(fc, list) else fc
        a = 1.0 - math.exp(-2 * math.pi * f / RATE)
        y += a * (v - y)
        out.append(y)
    return out


def bandpass(x, fc, q=4.0):
    """A state-variable band-pass whose centre may move per sample."""
    out, lo, bp = [], 0.0, 0.0
    for i, v in enumerate(x):
        f = fc[i] if isinstance(fc, list) else fc
        g = 2 * math.sin(math.pi * min(f, RATE / 6) / RATE)
        hi = v - lo - bp / q
        bp += g * hi
        lo += g * bp
        out.append(bp)
    return out


def tri(ph):
    """A triangle of phase ph (cycles)."""
    p = ph % 1.0
    return 4 * p - 1 if p < 0.5 else 3 - 4 * p


def env_exp(n, tau):
    return [math.exp(-i / (tau * RATE)) for i in range(n)]


def tone(freqs, shape=tri):
    """A tone whose frequency follows freqs, sample by sample."""
    out, ph = [], 0.0
    for f in freqs:
        out.append(shape(ph))
        ph += f / RATE
    return out


def norm(x, peak=PEAK, fade=64):
    """Scaled to the peak, faded in and out over a few samples so a clip
    starts and stops without a click."""
    m = max(abs(v) for v in x) or 1.0
    y = [v * peak / m for v in x]
    n = len(y)
    for i in range(min(fade, n // 2)):
        y[i] *= i / fade
        y[n - 1 - i] *= i / fade
    return y


def ufo():
    """The saucer: a triangle swept 450-900 Hz by a 6.25 Hz triangle. The
    loop is one sweep (1,280 samples), and the tone's frequency is scaled so
    the loop holds whole cycles - it repeats without a seam."""
    n = 1280
    sweep = [675 + 225 * tri(i / n + 0.25) for i in range(n)]
    cycles = sum(sweep) / RATE
    k = round(cycles) / cycles
    x = tone([f * k for f in sweep])
    m = max(abs(v) for v in x)
    return [v * PEAK * 0.8 / m for v in x]          # a loop: no fade


def shot():
    """A shot: noise through a band-pass falling 2,400-300 Hz, decaying."""
    n = int(0.40 * RATE)
    nz = Noise(0x0F00D)
    fc = [300 + 2100 * math.exp(-i / (0.08 * RATE)) for i in range(n)]
    x = bandpass([nz() for _ in range(n)], fc, q=3.0)
    e = env_exp(n, 0.12)
    return norm([a * b for a, b in zip(x, e)])


def basehit():
    """The base hit: low noise, rumbling at 14 Hz, over 1.2 s."""
    n = int(1.2 * RATE)
    nz = Noise(0x0BA5E)
    x = lowpass([nz() for _ in range(n)], 900)
    x = lowpass(x, 1400)
    e = env_exp(n, 0.45)
    r = [0.65 + 0.35 * math.sin(2 * math.pi * 14 * i / RATE) for i in range(n)]
    return norm([a * b * c for a, b, c in zip(x, e, r)])


def invhit():
    """An invader hit: a short crunch - noise and a falling square."""
    n = int(0.30 * RATE)
    nz = Noise(0x01A7D)
    sq = tone([700 * math.exp(-i / (0.15 * RATE)) + 120 for i in range(n)],
              shape=lambda p: 1.0 if p % 1.0 < 0.5 else -1.0)
    x = bandpass([0.7 * nz() + 0.5 * s for s in sq], 1200, q=1.5)
    e = env_exp(n, 0.07)
    return norm([a * b for a, b in zip(x, e)])


def extra():
    """An extra base: a bright tone pulsed eight times a second."""
    n = int(0.75 * RATE)
    x = tone([1250] * n, shape=lambda p: 1.0 if p % 1.0 < 0.5 else -1.0)
    x = lowpass(x, 2500)
    g = [1.0 if (i * 8 // RATE) % 2 == 0 else 0.0 for i in range(n)]
    g = lowpass(g, 200)
    return norm([a * b for a, b in zip(x, g)], peak=PEAK * 0.8)


def fleet(hz):
    """A fleet step: a deep thump - a square at the step's pitch, its edges
    softened, dying in a tenth of a second."""
    n = int(0.13 * RATE)
    x = tone([hz] * n, shape=lambda p: 1.0 if p % 1.0 < 0.5 else -1.0)
    x = lowpass(x, 600)
    e = env_exp(n, 0.05)
    return norm([a * b for a, b in zip(x, e)])


def ufohit():
    """The saucer hit: a triangle warbling at 12 Hz as it falls 1,300-500
    Hz over a second."""
    n = int(1.0 * RATE)
    f = [500 + 800 * (1 - i / n) + 180 * tri(12 * i / RATE) for i in range(n)]
    x = tone(f)
    e = [min(1.0, 4 * (1 - i / n)) for i in range(n)]
    return norm([a * b for a, b in zip(x, e)], peak=PEAK * 0.8)


def thrust():
    """Lunar Rescue's thrust: a low roar - noise through a low band-pass,
    swelling and fading over half a second."""
    n = int(0.5 * RATE)
    nz = Noise(0x7A57)
    x = bandpass([nz() for _ in range(n)], 260, q=1.2)
    e = [min(1.0, i / (0.08 * RATE)) * math.exp(-i / (0.3 * RATE)) for i in range(n)]
    return norm([a * b for a, b in zip(x, e)])


def step(hz):
    """A footstep: a short soft square blip."""
    n = int(0.06 * RATE)
    x = tone([hz] * n, shape=lambda p: 1.0 if p % 1.0 < 0.5 else -1.0)
    x = lowpass(x, 1800)
    e = env_exp(n, 0.02)
    return norm([a * b for a, b in zip(x, e)], peak=PEAK * 0.7, fade=16)


def rescue():
    """The rescue ship's beep, a LOOP: a 1,000 Hz triangle on for 0.1 s and
    off for 0.1 s, whole cycles so it repeats without a seam."""
    n = int(0.2 * RATE)
    on = n // 2
    x = tone([1000] * n)
    g = [1.0 if i < on else 0.0 for i in range(n)]
    g = lowpass(g, 300)
    y = [a * b for a, b in zip(x, g)]
    m = max(abs(v) for v in y)
    return [v * PEAK * 0.7 / m for v in y]


def clips():
    # the fleet's pitches are the speaker's (arcade.asm's ar_n_f1..f4)
    return [ufo(), shot(), basehit(), invhit(), extra(),
            fleet(110), fleet(98), fleet(87), fleet(82), ufohit(),
            thrust(), step(660), step(440), step(1500), rescue()]


def wav(path, x):
    data = bytes((int(round(v)) + 128) & 255 for v in x)
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVEfmt ' +
                struct.pack('<IHHIIHH', 16, 1, 1, RATE, RATE, 1, 8) +
                b'data' + struct.pack('<I', len(data)) + data)


def main():
    if len(sys.argv) < 2:
        sys.exit('usage: arcsfx.py OUT.PRT [--wav DIR]')
    cs = clips()
    head = 4 + 4 * len(cs)
    out = bytearray(b'AS' + struct.pack('<H', len(cs)))
    off = head
    for c in cs:
        out += struct.pack('<HH', off, len(c))
        off += len(c)
    for c in cs:
        for v in c:
            s = int(round(v))
            if not -PEAK <= s <= PEAK:
                sys.exit('arcsfx: a sample of %d, outside +-%d' % (s, PEAK))
            out.append(s & 255)
    if len(out) > 0xFFF0:
        sys.exit('arcsfx: %d bytes, over a segment' % len(out))
    open(sys.argv[1], 'wb').write(out)
    if '--wav' in sys.argv:
        d = sys.argv[sys.argv.index('--wav') + 1]
        os.makedirs(d, exist_ok=True)
        for name, c in zip(NAMES, cs):
            wav(os.path.join(d, name + '.wav'), c)
    print('arcsfx: %d clips, %d bytes -> %s' % (len(cs), len(out), sys.argv[1]))


if __name__ == '__main__':
    main()
