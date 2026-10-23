#!/usr/bin/env python3
"""x3fixtures: XTMP3's test MP3s, synthesized and encoded on the host (SPEC.md 121.1).

    python3 tools/x3fixtures.py --check     # verify tests/xtmp3/ against FIXTURES.txt
    python3 tools/x3fixtures.py --regen     # rebuild every fixture (needs lame)
    python3 tools/x3fixtures.py --wav DIR   # also keep the source WAVs

Every signal is made HERE from code: no recording, no third-party audio.  A
counter-hash noise source (splitmix64 of the sample index) replaces any
library RNG so the samples do not depend on numpy's generator streams, and
the float signal is rounded half-up to 16 bits once.  The signals are then
encoded with the host's LAME, used as a tool and never vendored; the exact
command line and the LAME version are written beside each file's SHA-256 in
tests/xtmp3/FIXTURES.txt, together with the SHA-256 of the WAV that went in,
so a regeneration that differs says whether the signal or the encoder moved.

The malformed fixtures are derived from encoded ones by deterministic byte
surgery (truncation, side-info fields rewritten, headers rewritten, tags and
garbage inserted); each says what the parser must do with it (`expect:`),
and `tools/x3sim.py --compare` holds the decoder to that.

`--check` needs neither LAME nor numpy: it hashes the committed files.
"""
import argparse
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FIXDIR = os.path.join(ROOT, 'tests', 'xtmp3')
LIST = os.path.join(FIXDIR, 'FIXTURES.txt')
LAME = os.environ.get('LAME', shutil.which('lame') or '/opt/homebrew/bin/lame')
GEN_VERSION = 1

# =============================================================================
# signals (numpy only below this line, imported lazily so --check needs none)
# =============================================================================


def _np():
    import numpy as np
    return np


def hnoise(n, seed):
    """Uniform noise in [-1, 1): splitmix64 of (seed, index)."""
    np = _np()
    with np.errstate(over='ignore'):
        z = (np.arange(n, dtype=np.uint64) + np.uint64(seed) * np.uint64(0x9E3779B97F4A7C15))
        z = (z ^ (z >> np.uint64(30))) * np.uint64(0xBF58476D1CE4E5B9)
        z = (z ^ (z >> np.uint64(27))) * np.uint64(0x94D049BB133111EB)
        z = z ^ (z >> np.uint64(31))
    return (z >> np.uint64(11)).astype(np.float64) / float(1 << 52) - 1.0


def tsec(n, rate):
    return _np().arange(n) / float(rate)


def sine(f, n, rate, ph=0.0):
    np = _np()
    return np.sin(2 * np.pi * f * tsec(n, rate) + ph)


def band_saw(f, n, rate, nh=None):
    """A sawtooth from harmonics below Nyquist (no aliasing, no RNG)."""
    np = _np()
    t = tsec(n, rate)
    out = np.zeros(n)
    k = 1
    while k * f < rate / 2 * 0.9 and (nh is None or k <= nh):
        out += np.sin(2 * np.pi * k * f * t) / k
        k += 1
    return out * (2 / np.pi)


def band_square(f, n, rate, nh=None):
    np = _np()
    t = tsec(n, rate)
    out = np.zeros(n)
    k = 1
    while k * f < rate / 2 * 0.9 and (nh is None or k <= nh):
        out += np.sin(2 * np.pi * k * f * t) / k
        k += 2
    return out * (4 / np.pi)


def env(n, rate, a=0.005, d=0.1, s=0.6, r=0.1):
    """ADSR over n samples (release inside n)."""
    np = _np()
    t = tsec(n, rate)
    dur = n / rate
    e = np.where(t < a, t / a, s + (1 - s) * np.exp(-(t - a) / max(d, 1e-6)))
    rel = np.clip((dur - t) / r, 0, 1)
    return e * rel


def onepole(x, rate, fc):
    """A one-pole lowpass (deterministic: a plain recurrence)."""
    np = _np()
    a = np.exp(-2 * np.pi * fc / rate)
    y = np.empty_like(x)
    acc = 0.0
    for i in range(len(x)):
        acc = (1 - a) * x[i] + a * acc
        y[i] = acc
    return y


def midi(n):
    return 440.0 * 2 ** ((n - 69) / 12.0)


def place(dst, src, at):
    end = min(len(dst), at + len(src))
    if end > at:
        dst[at:end] += src[:end - at]


def kick(rate, n=None):
    np = _np()
    n = n or int(0.35 * rate)
    t = tsec(n, rate)
    f = 45 + 110 * np.exp(-t * 28)
    ph = 2 * np.pi * np.cumsum(f) / rate
    return np.sin(ph) * np.exp(-t * 9)


def snare(rate, seed):
    np = _np()
    n = int(0.22 * rate)
    t = tsec(n, rate)
    return (0.6 * hnoise(n, seed) + 0.4 * np.sin(2 * np.pi * 190 * t)) * np.exp(-t * 18)


def hat(rate, seed):
    np = _np()
    n = int(0.06 * rate)
    x = hnoise(n, seed)
    x = x - np.concatenate(([0.0], x[:-1]))          # a crude highpass
    return x * np.exp(-tsec(n, rate) * 60) * 0.5


def song(rate, dur, bpm, prog, bass_pat, lead, seed, stereo, style):
    """A small additive-synth tune: drums, a bass line, chord pads, a lead."""
    np = _np()
    n = int(dur * rate)
    L = np.zeros(n)
    R = np.zeros(n)
    beat = 60.0 / bpm
    nb = int(dur / beat) + 1
    for b in range(nb):
        at = int(b * beat * rate)
        if style != 'lofi' or b % 2 == 0:
            k = kick(rate) * 0.55
            place(L, k, at)
            place(R, k, at)
        if b % 2 == 1:
            s = snare(rate, seed + b) * (0.35 if style != 'lofi' else 0.25)
            place(L, s, at)
            place(R, s * 0.9, at)
        for h8 in range(2):
            hh = hat(rate, seed * 7 + b * 2 + h8) * (0.18 if h8 else 0.25)
            place(L, hh * 0.7, at + int(h8 * beat / 2 * rate))
            place(R, hh, at + int(h8 * beat / 2 * rate))
    bar = 4 * beat
    for i in range(int(dur / bar) + 1):
        root, quality = prog[i % len(prog)]
        notes = [root + q for q in quality]
        at = int(i * bar * rate)
        ln = int(bar * rate)
        pad = np.zeros(ln)
        for j, nn in enumerate(notes):
            pad += band_saw(midi(nn), ln, rate, nh=12) * 0.07
            pad += sine(midi(nn) * 1.003, ln, rate) * 0.05
        pad *= env(ln, rate, a=0.08, d=0.6, s=0.7, r=0.25)
        place(L, pad, at)
        place(R, pad * 0.85, at)
        for k_, step in enumerate(bass_pat):
            if step is None:
                continue
            bn = root - 24 + step
            ln2 = int(beat / 2 * rate)
            bs = band_square(midi(bn), ln2, rate, nh=7) * 0.22
            bs *= env(ln2, rate, a=0.004, d=0.12, s=0.5, r=0.04)
            place(L, bs, at + int(k_ * beat / 2 * rate))
            place(R, bs, at + int(k_ * beat / 2 * rate))
    for k_, (step, nn) in enumerate(lead):
        at = int(step * beat / 2 * rate)
        ln3 = int(beat / 2 * rate * 0.95)
        ld = (band_saw(midi(nn), ln3, rate, nh=6) * 0.12 + sine(midi(nn), ln3, rate) * 0.1)
        ld *= env(ln3, rate, a=0.01, d=0.15, s=0.6, r=0.05)
        place(L, ld * (0.9 if k_ % 2 else 0.6), at)
        place(R, ld * (0.6 if k_ % 2 else 0.9), at)
    if not stereo:
        return (L + R) * 0.5
    return np.stack([L, R], axis=1)


MIN = (0, 3, 7)
MAJ = (0, 4, 7)
MIN7 = (0, 3, 7, 10)
MAJ7 = (0, 4, 7, 11)


def sig_neon(rate):
    lead = [(s, n) for s, n in zip(range(0, 64, 2),
            [76, 74, 72, 74, 76, 79, 76, 74, 72, 71, 72, 74, 76, 72, 69, 71,
             72, 74, 76, 79, 81, 79, 76, 74, 76, 74, 72, 71, 69, 71, 72, 69])]
    return song(rate, 10.0, 118, [(57, MIN), (53, MAJ), (60, MAJ), (55, MAJ)],
                [0, 0, 12, 0, 0, 12, 0, 7], lead, 11, True, 'synth')


def sig_night(rate):
    lead = [(s, n) for s, n in zip(range(0, 40, 1),
            [62, 65, 69, 72, 69, 65, 62, 65, 70, 74, 77, 74, 70, 74, 70, 65,
             67, 70, 74, 77, 74, 70, 67, 70, 69, 73, 76, 81, 76, 73, 69, 73,
             62, 65, 69, 74, 69, 65, 62, 57])]
    return song(rate, 10.0, 96, [(50, MIN), (58, MAJ), (55, MIN), (57, MAJ)],
                [0, None, 0, 7, 0, None, 12, 7], lead, 23, True, 'synth')


def sig_after(rate):
    lead = [(s, n) for s, n in zip(range(0, 28, 2),
            [72, 69, 65, 64, 67, 71, 74, 72, 69, 65, 62, 64, 65, 67])]
    return song(rate, 10.0, 84, [(53, MAJ7), (52, MIN7), (50, MIN7), (48, MAJ7)],
                [0, None, 7, None, 12, None, 7, 5], lead, 37, False, 'lofi')


def sig_chord(rate, dur):
    """A sustained chord, different on the two channels, plus a high tone."""
    np = _np()
    n = int(dur * rate)
    L = sum(sine(midi(m), n, rate) for m in (57, 61, 64)) * 0.2 + sine(9000, n, rate) * 0.05
    R = sum(sine(midi(m), n, rate, 0.7) for m in (52, 59, 64, 68)) * 0.17 + sine(3500, n, rate) * 0.08
    e = env(n, rate, a=0.05, d=0.5, s=0.8, r=0.1)
    return np.stack([L * e, R * e], axis=1)


def sig_click(rate, dur):
    """Clicks and drum hits on silence and on a soft tone: short blocks."""
    np = _np()
    n = int(dur * rate)
    x = sine(330, n, rate) * 0.08
    for k in range(int(dur * 8)):
        at = int(k * rate / 8)
        if k % 3 == 0:
            place(x, kick(rate) * 0.7, at)
        elif k % 3 == 1:
            place(x, snare(rate, k) * 0.6, at)
        else:
            c = np.zeros(int(0.004 * rate))
            c[0], c[1] = 0.9, -0.9
            place(x, c, at)
    return x


def sig_sweep(rate, dur):
    np = _np()
    n = int(dur * rate)
    t = tsec(n, rate)
    f0, f1 = 30.0, rate * 0.45
    k = np.log(f1 / f0) / dur
    return 0.5 * np.sin(2 * np.pi * f0 * (np.exp(k * t) - 1) / k)


def sig_bass(rate, dur):
    np = _np()
    n = int(dur * rate)
    x = np.zeros(n)
    notes = [33, 33, 36, 38, 33, 31, 28, 31]
    ln = n // len(notes)
    for i, m in enumerate(notes):
        b = (band_saw(midi(m), ln, rate, nh=10) * 0.4 + sine(midi(m), ln, rate) * 0.3)
        place(x, b * env(ln, rate, a=0.01, d=0.3, s=0.8, r=0.02), i * ln)
    return np.stack([x, x * 0.95], axis=1)


def sig_noise(rate, dur):
    np = _np()
    n = int(dur * rate)
    L = np.zeros(n)
    R = np.zeros(n)
    for k in range(int(dur * 3)):
        at = int(k * rate / 3)
        ln = int(rate * (0.08 + 0.05 * (k % 3)))
        e = np.exp(-tsec(ln, rate) * 12)
        place(L, hnoise(ln, 100 + k) * e * 0.6, at)
        place(R, hnoise(ln, 200 + k) * e * 0.6, at + int(0.01 * rate))
    L += hnoise(n, 7) * 0.01
    R += hnoise(n, 8) * 0.01
    return np.stack([L, R], axis=1)


def sig_quiet(rate, dur):
    """The chord at -50 dBFS: a quiet passage."""
    return sig_chord(rate, dur) * (10 ** (-50 / 20.0) / 0.6)


def sig_clip(rate, dur):
    """Clipping stress: an overdriven square and saw at full scale."""
    np = _np()
    n = int(dur * rate)
    L = np.clip(1.4 * (band_square(110, n, rate) * 0.8 + band_saw(220, n, rate) * 0.5), -1, 1)
    R = np.clip(1.4 * (band_saw(165, n, rate) * 0.9 + hnoise(n, 5) * 0.2), -1, 1)
    return np.stack([L, R], axis=1)


def sig_speech(rate, dur):
    """A crude formant voice: a 110 Hz pulse train through three resonances
    that move, with noise bursts for consonants (the XT's speech target)."""
    np = _np()
    n = int(dur * rate)
    t = tsec(n, rate)
    f0 = 110 + 15 * np.sin(2 * np.pi * 0.7 * t)
    ph = np.cumsum(f0) / rate
    pulse = (np.diff(np.floor(ph), prepend=0.0) > 0).astype(np.float64)
    out = np.zeros(n)
    for fbase, bw, g in ((650, 80, 1.0), (1100, 120, 0.5), (2500, 200, 0.25)):
        fc = fbase * (1 + 0.3 * np.sin(2 * np.pi * 1.3 * t + fbase))
        y = np.zeros(n)
        y1 = y2 = 0.0
        r = np.exp(-np.pi * bw / rate)
        cc = 2 * r * np.cos(2 * np.pi * fc / rate)
        r2 = r * r
        for i in range(n):
            v = pulse[i] + cc[i] * y1 - r2 * y2
            y2, y1 = y1, v
            y[i] = v
        out += g * y
    out /= np.max(np.abs(out)) + 1e-9
    syl = (np.sin(2 * np.pi * 2.5 * t) > -0.3).astype(np.float64)
    out = out * syl * 0.6
    for k in range(int(dur * 2)):
        place(out, hnoise(int(0.05 * rate), 300 + k) * 0.15, int((k + 0.45) * rate / 2))
    return out


def sig_mix(rate, dur):
    """Tones, a transient and noise together, for VBR and the matrix."""
    np = _np()
    n = int(dur * rate)
    x = sig_chord(rate, dur) * 0.8
    c = sig_click(rate, dur) * 0.6
    x[:, 0] += c
    x[:, 1] += np.roll(c, int(0.02 * rate))
    return x


def sig_is2(rate, dur):
    """For the MPEG-2 intensity surgery: the mix on the left and digital
    silence on the right.  LAME writes a silent channel with
    scalefac_compress 0 and no bits, which reads the same under the
    intensity tables (sfc >> 1 = 0, every slen 0), so relabelling keeps the
    bitstream aligned: every band is intensity coded at is_pos 0 (k = 1)."""
    x = sig_mix(rate, dur)
    x[:, 1] = 0.0
    return x


def sig_is(rate, dur):
    """For the intensity-stereo surgery: a full-band left channel and a right
    channel lowpassed to ~2.5 kHz, so the right channel's spectrum is zero
    above a bound."""
    np = _np()
    n = int(dur * rate)
    L = sig_mix(rate, dur)[:, 0] * 0.8
    R = np.zeros(n)
    for m in (45, 52, 57):
        R += sine(midi(m), n, rate) * 0.2
    R = onepole(onepole(R + L * 0.1, rate, 1200), rate, 1200)
    return np.stack([L, R], axis=1)


# =============================================================================
# WAV and LAME
# =============================================================================


def to_pcm16(x):
    np = _np()
    v = np.floor(np.asarray(x) * 32767.0 + 0.5)
    return np.clip(v, -32768, 32767).astype('<i2')


def wav_bytes(x, rate):
    np = _np()
    pcm = to_pcm16(x)
    nch = 1 if pcm.ndim == 1 else pcm.shape[1]
    data = pcm.tobytes()
    return (b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE' +
            b'fmt ' + struct.pack('<IHHIIHH', 16, 1, nch, rate, rate * nch * 2, nch * 2, 16) +
            b'data' + struct.pack('<I', len(data)) + data)


def lame_version():
    out = subprocess.run([LAME, '--version'], capture_output=True, text=True).stdout
    return out.splitlines()[0].strip()


def lame(args, wav, tmp):
    src = os.path.join(tmp, 'in.wav')
    dst = os.path.join(tmp, 'out.mp3')
    with open(src, 'wb') as f:
        f.write(wav)
    subprocess.run([LAME] + args + [src, dst], check=True, capture_output=True)
    with open(dst, 'rb') as f:
        return f.read()


# =============================================================================
# surgery: rewrite fields of the encoded stream
# =============================================================================


def frames_of(mp3):
    """[(offset, header)] of every frame, by walking frame sizes from the
    first sync (the fixtures are clean before surgery)."""
    sys.path.insert(0, HERE)
    import x3sim
    st = x3sim.Stream(mp3)
    out = []
    o = st.first
    while o + 4 <= len(mp3):
        h = x3sim.parse_header(mp3, o)
        if h is None:
            break
        out.append((o, h))
        o += h.size
    return out


def side_part23_bits(h):
    """Bit offsets (from the start of the side info) of each granule/channel's
    part2_3_length."""
    if h.mpeg1:
        pos = 9 + (5 if h.nch == 1 else 3) + 4 * h.nch
        per = 59
    else:
        pos = 8 + (1 if h.nch == 1 else 2)
        per = 63
    out = []
    for g in range(h.granules):
        for c in range(h.nch):
            out.append(pos)
            pos += per
    return out


def set_bits(buf, bitpos, n, v):
    for k in range(n):
        byte = (bitpos + k) >> 3
        bit = 7 - ((bitpos + k) & 7)
        if (v >> (n - 1 - k)) & 1:
            buf[byte] |= 1 << bit
        else:
            buf[byte] &= ~(1 << bit) & 0xFF


def get_bits(buf, bitpos, n):
    v = 0
    for k in range(n):
        byte = (bitpos + k) >> 3
        v = (v << 1) | ((buf[byte] >> (7 - ((bitpos + k) & 7))) & 1)
    return v


def surgery_header(mp3, fn, skip_first=True):
    """Apply fn(word) -> word to every frame header (the first, a tag frame,
    too unless skip_first)."""
    b = bytearray(mp3)
    for i, (o, h) in enumerate(frames_of(mp3)):
        w = struct.unpack('>I', bytes(b[o:o + 4]))[0]
        b[o:o + 4] = struct.pack('>I', fn(w) & 0xFFFFFFFF)
    return bytes(b)


def s_trunc(mp3):
    fr = frames_of(mp3)
    o, h = fr[len(fr) * 3 // 5]
    return mp3[:o + h.size // 2]


def s_badside(mp3):
    """Frames 5, 9, 13: the granules' part2_3_lengths raised until their sum
    passes the frame's whole main-data budget (main_data_begin + this frame's
    main data), one granule at a time up to 4095 each."""
    b = bytearray(mp3)
    for k in (5, 9, 13):
        o, h = frames_of(mp3)[k]
        sp = (o + 4 + (2 if h.crc else 0)) * 8
        budget = 8 * (get_bits(b, sp, 9 if h.mpeg1 else 8) + h.size - 4 - h.side_bytes)
        fields = side_part23_bits(h)
        total = sum(get_bits(b, sp + p, 12) for p in fields)
        for p in fields:
            if total > budget:
                break
            cur = get_bits(b, sp + p, 12)
            new = min(4095, cur + budget - total + 8)
            set_bits(b, sp + p, 12, new)
            total += new - cur
        assert total > budget
    return bytes(b)


def s_huge23(mp3):
    """Frame 6: granule 0's part2_3_length = 4095.  Whether that passes the
    budget depends on main_data_begin; when it does not, the granule's read
    runs on into the next granule's bits, which must be survived."""
    b = bytearray(mp3)
    o, h = frames_of(mp3)[6]
    sp = (o + 4 + (2 if h.crc else 0)) * 8
    set_bits(b, sp + side_part23_bits(h)[0], 12, 4095)
    return bytes(b)


def s_bigval(mp3):
    """Frame 4: big_values = 511 (more than 288 is reserved)."""
    b = bytearray(mp3)
    o, h = frames_of(mp3)[4]
    sp = (o + 4 + (2 if h.crc else 0)) * 8
    set_bits(b, sp + side_part23_bits(h)[0] + 12, 9, 511)
    return bytes(b)


def s_free(mp3):
    return surgery_header(mp3, lambda w: w & ~(15 << 12))


def s_layer2(mp3):
    return surgery_header(mp3, lambda w: (w & ~(3 << 17)) | (2 << 17))


def s_garbage(mp3):
    """300 bytes of garbage (with 0xFF bytes, so false syncs are tried)
    between frames 7 and 8, and 77 more between 15 and 16."""
    fr = frames_of(mp3)
    g1 = bytes(((i * 73 + 41) & 0xFF) | (0xE0 if i % 9 == 0 else 0) for i in range(300))
    g2 = bytes((0xFF if i % 5 == 0 else (i * 31) & 0xFF) for i in range(77))
    a, b_ = fr[8][0], fr[16][0]
    return mp3[:a] + g1 + mp3[a:b_] + g2 + mp3[b_:]


def id3v2_tag(title, artist, pad, padding=512):
    """An ID3v2.3 tag: TIT2, TPE1, a PRIV frame of `pad` bytes (none at 0)
    and `padding` zero bytes."""
    def frame(fid, text):
        body = b'\x00' + text.encode('latin-1')
        return fid + struct.pack('>I', len(body)) + b'\0\0' + body
    priv = b'PRIV' + struct.pack('>I', pad) + b'\0\0' + \
        bytes((i * 7) & 0xFF for i in range(pad)) if pad else b''
    frames = frame(b'TIT2', title) + frame(b'TPE1', artist) + priv
    size = len(frames) + padding
    ss = bytes(((size >> 21) & 0x7F, (size >> 14) & 0x7F, (size >> 7) & 0x7F, size & 0x7F))
    return b'ID3\x03\x00\x00' + ss + frames + b'\0' * padding


def s_id3big(mp3):
    return id3v2_tag('NEON DRIVE', 'XTMP3 FIXTURES', 20000) + mp3


def s_id3v1(mp3):
    def f(s, n):
        return s.encode('latin-1')[:n].ljust(n, b'\0')
    return mp3 + b'TAG' + f('AFTER HOURS', 30) + f('XTMP3 FIXTURES', 30) + \
        f('', 30) + b'2026' + f('', 30) + b'\x0c'


def s_vbri(mp3):
    """A Fraunhofer VBRI frame put in front of the first audio frame: frame
    0's header, side information all zero (so a decoder that plays it plays
    silence), and the VBRI header 32 bytes after the frame header naming the
    audio frames that follow.  (The base is too small a frame for LAME to
    have written an Info tag, so nothing is replaced.)"""
    fr = frames_of(mp3)
    o, h = fr[0]
    body = bytearray(h.size - 4)
    vb = b'VBRI' + struct.pack('>HHHII', 1, 0, 75, len(mp3) + h.size, len(fr)) + \
        struct.pack('>HHHH', 0, 1, 2, 1)
    body[32:32 + len(vb)] = vb
    return mp3[:o] + mp3[o:o + 4] + bytes(body) + mp3[o:]


def s_is(mp3, ext):
    """Joint stereo with intensity (ext 1) or intensity + M/S (ext 3) on
    every audio frame of an L/R-coded file."""
    def fn(w):
        return (w & ~(0xF << 4)) | (1 << 6) | (ext << 4)
    b = bytearray(mp3)
    for i, (o, h) in enumerate(frames_of(mp3)):
        if i == 0:
            continue
        w = struct.unpack('>I', bytes(b[o:o + 4]))[0]
        b[o:o + 4] = struct.pack('>I', fn(w))
    return bytes(b)


# =============================================================================
# THE MATRIX
# =============================================================================
#
# (name, signal, rate, seconds, lame args, derive, expect, note)
#   derive: None, or (base name, surgery function)
#   expect: 'decode' (compared with lame --decode; 'decode iso' compared with
#           x3sim's float model, where mpglib departs from ISO), 'refuse NAME',
#           or 'recover KEY>=N ...' (decoded; parser counts at least these);
#           either may add title=/artist= (the tag text), frames= (the VBR
#           header's count) and same=NAME (the identical PCM, untrimmed)

M44 = '44.1'


def matrix():
    S = []

    def add(name, sig, rate, dur, args, note, derive=None, expect='decode', tag=None):
        if tag:
            expect = 'decode title=%s artist=%s' % tag
        S.append(dict(name=name, sig=sig, rate=rate, dur=dur, args=args,
                      note=note, derive=derive, expect=expect, tag=tag))

    # the three demo songs (the mockup's queue)
    add('NEONDRV.MP3', 'neon', 44100, 10, ['-m', 'j', '-b', '56', '--resample', M44],
        'NEON DRIVE: MPEG-1 44.1 kHz joint stereo CBR 56, an ID3v2.3 title',
        tag=('NEON DRIVE', 'XTMP3 DEMO'))
    add('NIGHTSIG.MP3', 'night', 22050, 10, ['-m', 'j', '-b', '40', '--resample', '22.05'],
        'NIGHT SIGNALS: MPEG-2 22.05 kHz joint stereo CBR 40, an ID3v2.3 title',
        tag=('NIGHT SIGNALS', 'XTMP3 DEMO'))
    add('AFTERHRS.MP3', 'after', 11025, 10, ['-m', 'm', '-b', '24', '--resample', '11.025'],
        'AFTER HOURS: MPEG-2.5 11.025 kHz mono CBR 24 (an XT target), an ID3v2.3 title',
        tag=('AFTER HOURS', 'XTMP3 DEMO'))
    # MPEG-1 matrix
    add('C128.MP3', 'chord', 44100, 1.5, ['-m', 's', '-b', '128', '--resample', M44],
        'MPEG-1 44.1 kHz L/R stereo CBR 128, a different chord each side')
    add('C320.MP3', 'mix', 44100, 1.0, ['-m', 'j', '-b', '320', '--resample', M44],
        'MPEG-1 44.1 kHz joint stereo CBR 320 (the top bitrate)')
    add('VBR2.MP3', 'mix', 44100, 1.5, ['-m', 'j', '-V', '2', '--resample', M44],
        'MPEG-1 44.1 kHz joint stereo VBR -V2 (a Xing tag)')
    add('MSF.MP3', 'chord', 44100, 1.5, ['-m', 'f', '-b', '128', '--resample', M44],
        'MPEG-1 forced M/S on every frame')
    add('MONO44.MP3', 'click', 44100, 2.0, ['-m', 'm', '-b', '64', '--resample', M44],
        'MPEG-1 mono 64 kbps, clicks and drums (short blocks)')
    add('DUAL44.MP3', 'chord', 44100, 1.5, ['-m', 'd', '-b', '128', '--resample', M44],
        'MPEG-1 dual channel')
    add('RSV32.MP3', 'click', 44100, 3.0, ['-m', 'm', '-b', '32', '--resample', M44],
        'reservoir-heavy: 44.1 kHz mono at 32 kbps, transients on a tone')
    add('SWEEP.MP3', 'sweep', 44100, 2.0, ['-m', 'm', '-b', '96', '--resample', M44],
        'a log sweep 30 Hz to 19.8 kHz, mono 96')
    add('BASS.MP3', 'bass', 44100, 2.0, ['-m', 'j', '-b', '64', '--resample', M44],
        'a sustained bass line, joint stereo 64')
    add('NOISE.MP3', 'noise', 44100, 1.5, ['-m', 'j', '-b', '96', '--resample', M44],
        'noise bursts on a noise floor, joint stereo 96')
    add('SILENT.MP3', 'silence', 44100, 1.0, ['-m', 'j', '-b', '128', '--resample', M44],
        'digital silence')
    add('QUIET.MP3', 'quiet', 44100, 1.5, ['-m', 'j', '-b', '96', '--resample', M44],
        'the chord at -50 dBFS')
    add('CLIP.MP3', 'clip', 44100, 1.0, ['-m', 's', '-b', '192', '--resample', M44],
        'clipping stress: overdriven square and saw at full scale, L/R 192')
    add('C48.MP3', 'chord', 48000, 1.0, ['-m', 'j', '-b', '112', '--resample', '48'],
        'MPEG-1 48 kHz joint stereo 112')
    add('M32K.MP3', 'speech', 32000, 1.5, ['-m', 'm', '-b', '48', '--resample', '32'],
        'MPEG-1 32 kHz mono 48, the formant voice')
    # MPEG-2
    add('M22.MP3', 'mix', 22050, 1.5, ['-m', 'j', '-b', '64', '--resample', '22.05'],
        'MPEG-2 22.05 kHz joint stereo 64')
    add('M24.MP3', 'chord', 24000, 1.5, ['-m', 's', '-b', '48', '--resample', '24'],
        'MPEG-2 24 kHz L/R stereo 48')
    add('M16.MP3', 'speech', 16000, 2.0, ['-m', 'm', '-b', '32', '--resample', '16'],
        'MPEG-2 16 kHz mono 32, the formant voice')
    # MPEG-2.5: the XT targets
    add('X8K16.MP3', 'speech', 8000, 3.0, ['-m', 'm', '-b', '16', '--resample', '8'],
        'MPEG-2.5 8 kHz mono 16 kbps, the formant voice')
    add('X11K24.MP3', 'click', 11025, 3.0, ['-m', 'm', '-b', '24', '--resample', '11.025'],
        'MPEG-2.5 11.025 kHz mono 24 kbps, clicks on a tone')
    add('X12K32.MP3', 'mix', 12000, 2.0, ['-m', 'm', '-b', '32', '--resample', '12'],
        'MPEG-2.5 12 kHz mono 32 kbps')
    # intensity stereo: LAME does not encode it, so an L/R stream is relabelled
    add('ISBASE.TMP', 'is', 44100, 1.0, ['-m', 's', '-b', '96', '--resample', M44],
        'base for the intensity surgery (not committed)')
    add('IS44.MP3', None, 44100, 0, None,
        'MPEG-1: an L/R stream relabelled joint stereo with intensity on: the right '
        'channel is zero above ~2.5 kHz, so the bands above are intensity coded',
        derive=('ISBASE.TMP', 'is1'))
    add('ISMS44.MP3', None, 44100, 0, None,
        'MPEG-1: the same with intensity AND M/S (an intensity band gets sqrt 2 back)',
        derive=('ISBASE.TMP', 'is3'))
    add('IS22BASE.TMP', 'is2', 22050, 1.0, ['-m', 's', '-b', '48', '--resample', '22.05'],
        'base for the MPEG-2 intensity surgery (not committed)')
    add('IS22.MP3', None, 22050, 0, None,
        'MPEG-2: an L/R stream with a silent right channel relabelled intensity '
        'stereo: every band intensity coded at is_pos 0 (the right channel '
        'becomes the left).  mpglib does not follow ISO in MPEG-2 intensity '
        'stereo (it scores 16.7 dB on the ISO vector l3-test45), so the '
        'reference is the float model, which scores 97 dB there; see '
        'NUMERICS.md 6', derive=('IS22BASE.TMP', 'is1'), expect='decode iso')
    # malformed, from a small MPEG-1 base
    add('BASE.TMP', 'speech', 32000, 1.5, ['-m', 'm', '-b', '32', '--resample', '32'],
        'base for the malformed fixtures (not committed)')
    add('BTRUNC.MP3', None, 32000, 0, None, 'cut inside a frame 60% in',
        derive=('BASE.TMP', 'trunc'), expect='recover truncated>=1')
    add('BBADSIDE.MP3', None, 32000, 0, None,
        'frames 5, 9, 13: part2_3_length past the main-data budget',
        derive=('BASE.TMP', 'badside'), expect='recover badside>=3')
    add('BHUGE23.MP3', None, 32000, 0, None, 'frame 6: every part2_3_length 4095',
        derive=('BASE.TMP', 'huge23'), expect='recover')
    add('BBIGVAL.MP3', None, 32000, 0, None, 'frame 4: big_values 511 (> 288)',
        derive=('BASE.TMP', 'bigval'), expect='recover badside>=1')
    add('BFREE.MP3', None, 32000, 0, None, 'every header rewritten to free format',
        derive=('BASE.TMP', 'free'), expect='refuse FREE FORMAT')
    add('BLAYER2.MP3', None, 32000, 0, None,
        'every header rewritten to Layer II (same frame size at MPEG-1)',
        derive=('BASE.TMP', 'layer2'), expect='refuse LAYER II')
    add('BID3V1.MP3', None, 32000, 0, None,
        'an ID3v1 tag behind: title "AFTER HOURS"',
        derive=('BASE.TMP', 'id3v1'), expect='decode title=AFTER HOURS artist=XTMP3 FIXTURES')
    add('BGARBAGE.MP3', None, 32000, 0, None,
        '300 and 77 bytes of garbage between frames (decodes exactly as BASE)',
        derive=('BASE.TMP', 'garbage'), expect='recover resync>=2 same=BID3V1.MP3')
    add('BID3BIG.MP3', None, 32000, 0, None,
        'a 20.6 KB ID3v2.3 tag with TIT2 "NEON DRIVE" and TPE1 "XTMP3 FIXTURES" ahead',
        derive=('BASE.TMP', 'id3big'),
        expect='decode title=NEON DRIVE artist=XTMP3 FIXTURES same=BID3V1.MP3')
    add('BVBRI.MP3', None, 32000, 0, None,
        'a Fraunhofer VBRI frame in front of the first audio frame',
        derive=('BASE.TMP', 'vbri'), expect='recover frames=43 same=BID3V1.MP3')
    return S


SIGNALS = {
    'neon': lambda r, d: sig_neon(r), 'night': lambda r, d: sig_night(r),
    'after': lambda r, d: sig_after(r), 'chord': sig_chord, 'mix': sig_mix,
    'click': sig_click, 'sweep': sig_sweep, 'bass': sig_bass, 'noise': sig_noise,
    'silence': lambda r, d: _np().zeros((int(r * d), 2)), 'quiet': sig_quiet,
    'clip': sig_clip, 'speech': sig_speech, 'is': sig_is, 'is2': sig_is2,
}

SURGERY = {
    'trunc': s_trunc, 'badside': s_badside, 'huge23': s_huge23, 'bigval': s_bigval,
    'free': s_free, 'layer2': s_layer2, 'garbage': s_garbage, 'id3big': s_id3big,
    'id3v1': s_id3v1, 'vbri': s_vbri, 'is1': lambda m: s_is(m, 1),
    'is3': lambda m: s_is(m, 3),
}


def sha(b):
    return hashlib.sha256(b).hexdigest()


def regen(wavdir=None):
    os.makedirs(FIXDIR, exist_ok=True)
    ver = lame_version()
    made = {}
    recs = []
    with tempfile.TemporaryDirectory() as tmp:
        for f in matrix():
            if f['derive'] is None:
                x = SIGNALS[f['sig']](f['rate'], f['dur'])
                wav = wav_bytes(x, f['rate'])
                if wavdir:
                    os.makedirs(wavdir, exist_ok=True)
                    with open(os.path.join(wavdir, f['name'][:-4] + '.WAV'), 'wb') as g:
                        g.write(wav)
                mp3 = lame(['-S'] + f['args'], wav, tmp)
                f['wavsha'] = sha(wav)
                f['cmd'] = 'lame -S ' + ' '.join(f['args']) + ' IN.WAV OUT.MP3'
                if f.get('tag'):
                    mp3 = id3v2_tag(f['tag'][0], f['tag'][1], 0, 64) + mp3
                    f['cmd'] += '; an ID3v2.3 tag (TIT2 %r, TPE1 %r, 64 bytes of padding) ahead' % f['tag']
            else:
                base, op = f['derive']
                mp3 = SURGERY[op](made[base])
                f['cmd'] = 'surgery %s on %s' % (op, base)
            made[f['name']] = mp3
            if f['name'].endswith('.TMP'):
                continue
            with open(os.path.join(FIXDIR, f['name']), 'wb') as g:
                g.write(mp3)
            f['sha'] = sha(mp3)
            f['bytes'] = len(mp3)
            recs.append(f)
    lines = [
        '# XTMP3 fixtures (SPEC.md 121.1): made by tools/x3fixtures.py --regen.',
        '# Signals are synthesized in that file; each was encoded by the LAME named',
        '# below (a host tool, never vendored).  `x3fixtures.py --check` verifies the',
        '# hashes; `x3sim.py --compare` decodes each and holds it to `expect`.',
        '# generator %d; encoder: %s' % (GEN_VERSION, ver),
        '',
    ]
    total = 0
    for f in recs:
        total += f['bytes']
        lines.append('%-13s %s %7d' % (f['name'], f['sha'], f['bytes']))
        lines.append('  what:   %s' % f['note'])
        if f['derive'] is None:
            lines.append('  signal: %s, %d Hz, %.1f s; wav sha256 %s' % (
                f['sig'], f['rate'], f['dur'], f['wavsha']))
            lines.append('  cmd:    %s' % f['cmd'])
        else:
            base = [g for g in matrix() if g['name'] == f['derive'][0]][0]
            lines.append('  cmd:    %s (%s: signal %s, %d Hz, %.1f s, lame -S %s)' % (
                f['cmd'], base['name'], base['sig'], base['rate'], base['dur'],
                ' '.join(base['args'])))
        lines.append('  expect: %s' % f['expect'])
    lines.append('')
    lines.append('# total %d bytes in %d files' % (total, len(recs)))
    with open(LIST, 'w') as g:
        g.write('\n'.join(lines) + '\n')
    print('wrote %d fixtures, %d bytes, encoder %s' % (len(recs), total, ver))


def read_list():
    """[(name, sha, bytes, expect, what)] from FIXTURES.txt."""
    out = []
    cur = None
    with open(LIST) as f:
        for line in f:
            if line.startswith('#') or not line.strip():
                continue
            if not line.startswith(' '):
                name, h, n = line.split()
                cur = dict(name=name, sha=h, bytes=int(n), expect='decode', what='')
                out.append(cur)
            else:
                k, _, v = line.strip().partition(':')
                cur[k.strip()] = v.strip()
    return out


def check():
    bad = 0
    recs = read_list()
    for r in recs:
        p = os.path.join(FIXDIR, r['name'])
        if not os.path.exists(p):
            print('MISSING %s' % r['name'])
            bad += 1
            continue
        with open(p, 'rb') as f:
            b = f.read()
        if sha(b) != r['sha'] or len(b) != r['bytes']:
            print('CHANGED %s' % r['name'])
            bad += 1
    extra = sorted(set(n for n in os.listdir(FIXDIR) if n.endswith('.MP3')) -
                   set(r['name'] for r in recs))
    for n in extra:
        print('UNLISTED %s' % n)
        bad += 1
    if bad:
        print('x3fixtures: %d problem(s)' % bad)
        return 1
    print('x3fixtures: %d fixtures verified' % len(recs))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--regen', action='store_true')
    ap.add_argument('--wav', metavar='DIR')
    a = ap.parse_args()
    if a.regen:
        regen(a.wav)
        return check()
    return check()


if __name__ == '__main__':
    sys.exit(main())
