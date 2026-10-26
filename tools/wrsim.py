#!/usr/bin/env python3
"""wrsim.py - WaveRack's host REFERENCE (SPEC.md 123.3-123.5): htmsim's and
weavesim's role for apps/waverack.

    python3 tools/wrsim.py --selfcheck [--quiet]   # the fast row
    python3 tools/wrsim.py --fixtures DIR          # write the fixture WAVs
    python3 tools/wrsim.py --ref FILE.WAV ...      # what WaveRack must say

Three things live here, and the 8086 code is held to each:

  THE READER   parse(bytes) walks a RIFF/WAVE file exactly as wrwav.inc does
               (SPEC.md 123.4): any chunk order, the odd-length pad byte,
               unknown chunks skipped, at most WW_NCHUNK = 64 chunks, every
               length checked against the file before it is trusted, and
               every refusal its own CODE - the same numbers the package
               leaves in its bss ([ww_err]), so tests/waverack.py compares
               the guest with this file field for field.
  THE ENVELOPE envelope(...) is SPEC.md 123.5's coarse envelope: the block
               B = the smallest power of two >= 16 with ceil(frames / B) <=
               2,048, and per block and channel the minimum and maximum as
               signed 8-bit (an 8-bit sample - 80h; a 16-bit sample's high
               byte). columns(...) is the view cache at a zoom: exact block
               ranges at fpp >= B, and below it the samples themselves with
               each column's span stretched to the previous column's last
               value (the trace is a line, not dots).
  PLAYBACK     out_rate(), vtab(), mono(), convert() and play_pos() are
               SPEC.md 123.6's conversion and transport arithmetic: the
               divisor d (the smallest whole number that brings the rate to
               the card's ceiling), the box filter of d frames, a 16-bit
               sample's high byte, the stereo average, the VOLUME table, and
               the frame a played count stands for - through the first lap
               and every loop after it. sel_text() is the selection field
               (SPEC.md 123.9). tests/waverackplay.py holds the captured
               output of QEMU's SB16 to convert(), sample for sample.
  THE FIXTURES fixtures() makes every WAV the soak row opens: tones,
               impulses (single-sample spikes a coarse envelope MUST show),
               stereo 16-bit, odd and unknown chunks, chunk orders, the
               extensible format, clamped, floored and unfinished data, the
               refusals, and one LONG file sized for a 1.44 MB scratch disk.
               Deterministic: integer arithmetic and a fixed sine table, so
               the bytes are the same on every host.

--selfcheck checks every fixture against this reader's expectation AND
against Python's own `wave` module wherever that module can read the file
(PCM, 8/16-bit, mono/stereo, fmt before data; the extensible one where the
module knows the format), and that each impulse survives the coarse
envelope at Fit. Standard library only; it must cost well under a second.
"""
import io
import math
import os
import struct
import sys
import wave

WW_NCHUNK = 64          # chunks the walk reads before it refuses
ENV_MAXBLK = 2048       # SPEC.md 123.5
ENV_MINSHIFT = 4        # B >= 16

# --- the refusal codes ([ww_err]) and their status sentences (<= 22 chars,
#     tools/os88wrlay.py STATUS_MAX). The package spells the same sentences
E_OK, E_NOTRIFF, E_NOFMT, E_NODATA, E_NOTPCM, E_BITS, E_CHANS, E_BALIGN, \
    E_RATE, E_CHUNKS, E_BIG, E_PACKED, E_IO, E_CLUSTER, E_NOFILE, E_MEM, \
    E_BADFMT = range(17)
SENTENCE = {
    E_NOTRIFF: "Not a RIFF/WAVE file",
    E_NOFMT: "No fmt chunk",
    E_NODATA: "No data chunk",
    E_NOTPCM: "Format %04Xh: not PCM",
    E_BITS: "%d-bit is unsupported",
    E_CHANS: "%d channels unsupported",
    E_BALIGN: "Bad block align",
    E_RATE: "Bad sample rate",
    E_CHUNKS: "More than 64 chunks",
    E_BIG: "File over 2 GB",
    E_PACKED: "File is compressed",
    E_IO: "Cannot read the file",
    E_CLUSTER: "Cluster too large",
    E_NOFILE: "File not found",
    E_MEM: "Not enough memory",
    E_BADFMT: "Bad fmt chunk",
}
# --- the notes an accepted file may carry ([ww_note])
N_NONE, N_CLAMP, N_IGNORED, N_RECOVER = range(4)
NOTE = {N_CLAMP: "Data clamped to file",
        N_IGNORED: "%d byte%s ignored at end",
        N_RECOVER: "Recovered; save to fix"}

PCM_GUID_TAIL = bytes.fromhex("000000001000800000aa00389b71")


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


# =============================================================================
# THE READER (wrwav.inc, ww_parse)
# =============================================================================
def parse(data, size=None):
    """The reference walk. Answers a dict: err (E_*), arg (the number a
    refusal's sentence names), and for an accepted file rate, bits, ch,
    balign, dataoff, datalen (after the clamp and the floor), frames, note
    (N_*), notearg, dirty, chunks (how many headers were read)."""
    size = len(data) if size is None else size
    r = dict(err=E_OK, arg=0, chunks=0)

    def refuse(e, arg=0):
        r.update(err=e, arg=arg)
        return r
    if size > 0x7FFFFFFF:
        return refuse(E_BIG)
    if size < 12 or data[0:4] != b"RIFF" or data[8:12] != b"WAVE":
        return refuse(E_NOTRIFF)
    pos, n = 12, 0
    fmt = data_at = None
    while pos + 8 <= size:
        n += 1
        if n > WW_NCHUNK:
            r["chunks"] = n - 1
            return refuse(E_CHUNKS)
        cid, csz = data[pos:pos + 4], u32(data, pos + 4)
        if cid == b"fmt " and fmt is None:
            avail = size - (pos + 8)
            if csz < 16 or avail < 16:
                r["chunks"] = n
                return refuse(E_BADFMT)
            body = data[pos + 8:pos + 8 + min(40, avail)]
            tag = u16(body, 0)
            if tag == 0xFFFE:
                if csz < 40 or avail < 40:
                    r["chunks"] = n
                    return refuse(E_BADFMT)
                if u16(body, 24) != 1 or body[26:40] != PCM_GUID_TAIL:
                    tag = 0xFFFF            # an extensible that is not PCM
                else:
                    tag = 1
            fmt = dict(tag=tag if tag != 0xFFFF else 0xFFFE,
                       pcm=(tag == 1), ch=u16(body, 2), rate=u32(body, 4),
                       balign=u16(body, 12), bits=u16(body, 14))
        elif cid == b"data" and data_at is None:
            data_at = (pos + 8, csz)
        if fmt is not None and data_at is not None:
            break
        nxt = pos + 8 + csz + (csz & 1)
        if nxt > 0xFFFFFFFF:
            break
        pos = nxt
    r["chunks"] = n
    if fmt is None:
        return refuse(E_NOFMT)
    if data_at is None:
        return refuse(E_NODATA)
    if not fmt["pcm"]:
        return refuse(E_NOTPCM, fmt["tag"])
    if fmt["bits"] not in (8, 16):
        return refuse(E_BITS, fmt["bits"])
    if fmt["ch"] not in (1, 2):
        return refuse(E_CHANS, fmt["ch"])
    if not 1000 <= fmt["rate"] <= 65535:
        return refuse(E_RATE)
    balign = fmt["ch"] * fmt["bits"] // 8
    if fmt["balign"] != balign:
        return refuse(E_BALIGN)
    dataoff, dlen = data_at
    avail = size - dataoff
    note, narg, dirty = N_NONE, 0, False
    if dlen in (0, 0xFFFFFFFF):
        if avail > 0:
            dlen, note, dirty = avail, N_RECOVER, True
        else:
            dlen = 0
    elif dlen > avail:
        dlen, note = avail, N_CLAMP
    rem = dlen % balign
    if rem and note != N_RECOVER:
        note, narg = N_IGNORED, rem
    frames = dlen // balign
    r.update(rate=fmt["rate"], bits=fmt["bits"], ch=fmt["ch"], balign=balign,
             dataoff=dataoff, datalen=frames * balign, frames=frames,
             note=note, notearg=narg, dirty=dirty)
    return r


def sentence(r):
    """The status bar's sentence for a parse result (None for a plain open)."""
    if r["err"]:
        s = SENTENCE[r["err"]]
        return s % r["arg"] if "%" in s else s
    if r["note"] == N_IGNORED:
        return NOTE[N_IGNORED] % (r["notearg"], "" if r["notearg"] == 1 else "s")
    return NOTE.get(r["note"])


# =============================================================================
# THE ENVELOPE AND THE VIEW (wrenv.inc)
# =============================================================================
def samples(data, r):
    """Per channel, the signed 8-bit view of every frame (SPEC.md 123.5)."""
    raw = data[r["dataoff"]:r["dataoff"] + r["datalen"]]
    ch, bits = r["ch"], r["bits"]
    if bits == 8:
        v = [b - 128 for b in raw]
    else:
        v = [(b - 256 if b > 127 else b) for b in raw[1::2]]
    return [v[c::ch] for c in range(ch)]


def block_shift(frames):
    s = ENV_MINSHIFT
    while (frames + (1 << s) - 1) >> s > ENV_MAXBLK:
        s += 1
    return s


def envelope(chans, frames):
    """[(min, max) per channel] per block, and the block shift."""
    s = block_shift(frames)
    B = 1 << s
    env = []
    for b0 in range(0, frames, B):
        env.append([(min(c[b0:b0 + B]), max(c[b0:b0 + B])) for c in chans])
    return env, s


def fit_zoom(frames, ww):
    z = 0
    while (ww << z) < frames:
        z += 1
    return z


def view_clamp(first, z, ww, frames):
    """we_clamp: the view's first frame bounded to frames - span ROUNDED UP to
    a multiple of fpp (so the view's end reaches the document's last frame
    below Fit), at least 0, and rounded down to a multiple of fpp"""
    fpp = 1 << z
    hi = max(0, frames - (ww << z))
    if hi:
        hi = (hi + fpp - 1) & ~(fpp - 1)
    return min(max(0, first), hi) & ~(fpp - 1)


def columns(chans, env, s, frames, first, z, ww):
    """The view cache when it is EXACT: per column, per channel, (min, max)
    or None past the end. fpp >= B: the blocks it covers; below: the samples,
    each span stretched to the previous column's last sample."""
    fpp = 1 << z
    out = []
    for c in range(ww):
        f0 = first + c * fpp
        if f0 >= frames:
            out.append(None)
            continue
        f1 = min(frames, f0 + fpp)
        col = []
        for k, ch in enumerate(chans):
            if fpp >= (1 << s):
                bl = env[f0 >> s:((f1 - 1) >> s) + 1]
                col.append((min(b[k][0] for b in bl), max(b[k][1] for b in bl)))
            else:
                lo = f0 - 1 if c > 0 and f0 > 0 else f0
                seg = ch[lo:f1]
                col.append((min(seg), max(seg)))
        out.append(col)
    return out


# =============================================================================
# PLAYBACK AND SELECTION (wrxport.inc, SPEC.md 123.6, 123.9)
# =============================================================================
CEIL, CEIL_HI, MIN_OUT = 22222, 44100, 4000     # the card; the refusal


def out_rate(rate, hi=False):
    """(d, out) for a document at RATE: d the smallest whole number with
    rate / d at most the card's ceiling (44,100 with SND_CAP_PCM_HI), out
    the rate the card is opened at (floored). None: refused, under 4,000"""
    ceil = CEIL_HI if hi else CEIL
    d = (rate + ceil - 1) // ceil
    out = rate // d
    return (d, out) if out >= MIN_OUT else None


def vtab(v):
    """VOLUME's 256-byte table: out = 80h + (in - 80h) x v / 100, the
    product truncated toward zero (the 8086's idiv)"""
    t = []
    for i in range(256):
        x = (i - 128) * v
        q = abs(x) // 100
        t.append(128 + (q if x >= 0 else -q))
    return bytes(t)


def mono(data, r):
    """per frame, the unsigned 8-bit mono value the conversion starts from:
    an 8-bit sample as it is, a 16-bit one's high byte XOR 80h, and the two
    channels' (L + R) >> 1"""
    raw = data[r["dataoff"]:r["dataoff"] + r["datalen"]]
    if r["bits"] == 8:
        u = list(raw)
    else:
        u = [b ^ 0x80 for b in raw[1::2]]
    if r["ch"] == 2:
        u = [(u[i] + u[i + 1]) >> 1 for i in range(0, len(u) - 1, 2)]
    return u


def frames_played(n, sp, e1, r0, r1, loop):
    """the document frames a play that began at SP plays, the first N of
    them: SP up to E1, then (looping) [R0, R1) for ever"""
    out = list(range(sp, min(e1, sp + n)))
    while loop and len(out) < n and r1 > r0:
        out += list(range(r0, min(r1, r0 + n - len(out))))
    return out


def convert(m, d, v):
    """mono values M -> the bytes the card is given: the box filter of D
    frames (a sum floored by d - a trailing part group is dropped), then
    VOLUME's table"""
    vt = vtab(v)
    return bytes(vt[sum(m[i:i + d]) // d] for i in range(0, len(m) - d + 1, d))


def lap_end(sp, r1, frames):
    """where the first lap ends: the region's end, or (a play begun past
    it) the document's"""
    return r1 if sp < r1 else frames


def play_pos(F, sp, e1, r0, r1, loop):
    """the document frame F frames into a play stands at (SPEC.md 123.6):
    SP + F through the first lap, then [R0, R1) round and round - or, not
    looping, the lap's end"""
    if F < e1 - sp:
        return sp + F
    if not loop or r1 <= r0:
        return e1
    return r0 + (F - (e1 - sp)) % (r1 - r0)


def sel_text(s, e, rate):
    """the selection field's value (SPEC.md 123.9): `SS.cc - SS.cc` while
    the end is under a minute, `M:SS.c - M:SS.c` from there; '' for none"""
    if s >= e:
        return ""
    if e < 60 * rate:
        f = lambda x: "%02d.%02d" % (x // rate, (x % rate) * 100 // rate)
    else:
        f = lambda x: "%d:%02d.%d" % (x // rate // 60, x // rate % 60,
                                       (x % rate) * 10 // rate)
    return "%s - %s" % (f(s), f(e))


# =============================================================================
# THE FIXTURES
# =============================================================================
SINE = [round(math.sin(2 * math.pi * i / 4096) * 32767) for i in range(4096)]


def tone(n, rate, hz, amp, phase=0):
    """n samples of a sine, amp of full scale 32767 (integers throughout)"""
    out = []
    step = hz * 4096
    for i in range(n):
        out.append(SINE[((i * step) // rate + phase) & 4095] * amp // 32767)
    return out


def pcm8(vals):
    return bytes((v >> 8) + 128 for v in vals)


def pcm16(vals):
    return struct.pack("<%dh" % len(vals), *vals)


def interleave(a, b):
    out = []
    for x, y in zip(a, b):
        out += [x, y]
    return out


def chunk(cid, body, size=None):
    sz = len(body) if size is None else size
    pad = b"\0" if len(body) & 1 else b""
    return cid + struct.pack("<I", sz) + body + pad


def fmt_body(ch, rate, bits, tag=1, balign=None):
    ba = ch * bits // 8 if balign is None else balign
    return struct.pack("<HHIIHH", tag, ch, rate, rate * ba, ba, bits)


def riff(chunks, size=None):
    body = b"WAVE" + b"".join(chunks)
    return b"RIFF" + struct.pack("<I", len(body) if size is None else size) + body


IMPULSES = [(1000, 127), (5000, -128), (9001, 127), (15003, -128),
            (21500, 127)]       # (frame, signed peak): IMPULSE.WAV's spikes
IMP_FRAMES = 22050
ST_IMPULSE = 7777               # STEREO16.WAV's one spike, on R
LONG_FRAMES = 1150000           # LONG.WAV: ~1.1 MB, 104 s at 11,025 Hz


def long_wav():
    """LONG.WAV, made with bytes.translate so it costs milliseconds: one
    second of a 300 Hz tone, scaled per second by a table, so the envelope
    has a shape to show (a swell, a gap of silence, a loud burst)"""
    sec = pcm8(tone(11025, 11025, 300, 30000))
    out = bytearray()
    k = 0
    while len(out) < LONG_FRAMES:
        a = (k * 37) % 100          # 0..99: the second's loudness
        if 40 <= k < 44:
            a = 0                   # four seconds of silence
        tab = bytes(max(0, min(255, 128 + (v - 128) * a // 100))
                    for v in range(256))
        out += sec.translate(tab)
        k += 1
    data = bytes(out[:LONG_FRAMES])
    return riff([chunk(b"fmt ", fmt_body(1, 11025, 8)), chunk(b"data", data)])


def fixtures():
    """name -> (bytes, folder, why). folder: GOOD, BAD or '' (MEDIA's root)"""
    f = {}
    # --- accepted ------------------------------------------------------------
    t = pcm8(tone(11025, 11025, 1000, 28000))
    f["TONE8.WAV"] = (riff([chunk(b"fmt ", fmt_body(1, 11025, 8)),
                            chunk(b"data", t)]), "GOOD",
                      "mono 8-bit 11,025 Hz, a 1 kHz tone, 1 s")
    imp = [0] * IMP_FRAMES
    for fr, pk in IMPULSES:
        imp[fr] = pk
    f["IMPULSE.WAV"] = (riff([chunk(b"fmt ", fmt_body(1, 11025, 8)),
                              chunk(b"data", bytes(v + 128 for v in imp))]),
                        "GOOD", "mono 8-bit silence with single-sample spikes")
    left = tone(22050, 22050, 440, 24000)
    right = [0] * 22050
    right[ST_IMPULSE] = 32767
    f["STEREO16.WAV"] = (riff([chunk(b"fmt ", fmt_body(2, 22050, 16)),
                               chunk(b"data", pcm16(interleave(left, right)))]),
                         "GOOD", "stereo 16-bit 22,050 Hz: L a tone, R silence "
                         "and one spike")
    f["TONE8K.WAV"] = (riff([chunk(b"fmt ", fmt_body(1, 8000, 16)),
                             chunk(b"data", pcm16(tone(8000, 8000, 500,
                                                       24000)))]), "GOOD",
                       "mono 16-bit 8,000 Hz, a 500 Hz tone, 1 s")
    f["TONE44.WAV"] = (riff([chunk(b"fmt ", fmt_body(2, 44100, 8)),
                             chunk(b"data", pcm8(interleave(
                                 tone(22050, 44100, 1000, 26000),
                                 tone(22050, 44100, 1500, 20000))))]), "GOOD",
                       "stereo 8-bit 44,100 Hz (1 kHz L, 1.5 kHz R), 0.5 s: "
                       "d = 2 on a card without PCM_HI")
    t2 = pcm8(tone(4000, 8000, 500, 20000))
    f["LISTODD.WAV"] = (riff([chunk(b"fmt ", fmt_body(1, 8000, 8)),
                              chunk(b"LIST", b"INFOx"),     # 5: a pad byte
                              chunk(b"data", t2)]), "GOOD",
                        "an odd-length LIST chunk (and its pad) before data")
    f["UNKNOWN.WAV"] = (riff([chunk(b"junk", b"\x11" * 30),
                              chunk(b"fmt ", fmt_body(1, 8000, 8)),
                              chunk(b"fact", struct.pack("<I", 4000)),
                              chunk(b"cue ", b"\0" * 7),
                              chunk(b"data", t2),
                              chunk(b"zzzz", b"tail")]), "GOOD",
                        "unknown chunks before and after")
    f["DATA1ST.WAV"] = (riff([chunk(b"data", t2),
                              chunk(b"fmt ", fmt_body(1, 8000, 8))]), "GOOD",
                        "data before fmt")
    ext = struct.pack("<HHIIHHHHI", 0xFFFE, 1, 11025, 22050, 2, 16, 22, 16, 4) \
        + struct.pack("<H", 1) + PCM_GUID_TAIL
    f["EXTENS.WAV"] = (riff([chunk(b"fmt ", ext),
                             chunk(b"data", pcm16(tone(5000, 11025, 700, 20000)))]),
                       "GOOD", "WAVE_FORMAT_EXTENSIBLE, PCM sub-format")
    f["PASTEND.WAV"] = (riff([chunk(b"fmt ", fmt_body(1, 8000, 8)),
                              chunk(b"data", t2[:3000], size=100000)]), "GOOD",
                        "a data size past the file's end: clamped")
    st = pcm16(interleave(tone(1000, 11025, 600, 20000),
                          tone(1000, 11025, 900, 20000)))
    f["NONWHOLE.WAV"] = (riff([chunk(b"fmt ", fmt_body(2, 11025, 16)),
                               chunk(b"data", st + b"\x01\x02\x03")]), "GOOD",
                         "a data size not a whole number of frames: floored")
    body = riff([chunk(b"fmt ", fmt_body(1, 11025, 8))]) + b"data" + \
        struct.pack("<I", 0) + pcm8(tone(8000, 11025, 800, 25000))
    f["UNFIN.WAV"] = (body, "GOOD",
                      "an unfinished take: data size 0, bytes after it")
    # --- refused -------------------------------------------------------------
    f["NOTRIFF.WAV"] = (b"This is not a sound file at all.\r\n" * 4, "BAD",
                        "not RIFF")
    f["ADPCM.WAV"] = (riff([chunk(b"fmt ", struct.pack("<HHIIHHHH", 0x11, 1,
                                                       11025, 5588, 256, 4, 2,
                                                       505)),
                            chunk(b"data", b"\0" * 512)]), "BAD",
                      "IMA ADPCM, tag 0011h")
    f["BITS24.WAV"] = (riff([chunk(b"fmt ", fmt_body(1, 11025, 24)),
                             chunk(b"data", b"\0" * 300)]), "BAD", "24-bit")
    f["CHAN3.WAV"] = (riff([chunk(b"fmt ", fmt_body(3, 11025, 8)),
                            chunk(b"data", b"\x80" * 300)]), "BAD",
                      "three channels")
    f["BADALIGN.WAV"] = (riff([chunk(b"fmt ", fmt_body(1, 11025, 16, balign=4)),
                               chunk(b"data", b"\0" * 400)]), "BAD",
                         "a block align that disagrees")
    # a chunk size that carries the walk's next position to FFFFFFF8h: the
    # header after it would wrap past 4 GB to offset 0..7 (wave 2's review,
    # M1) - the walk ends there, with fmt and without data
    f["WRAPPOS.WAV"] = (riff([chunk(b"fmt ", fmt_body(1, 8000, 8))]) +
                        b"JUNK" + struct.pack("<I", 0xFFFFFFF8 - 44) +
                        b"\0" * 16, "BAD",
                        "a chunk size that wraps the walk past 4 GB")
    many = [chunk(b"x%03d" % i, b"ab") for i in range(70)]
    f["CHUNK70.WAV"] = (riff(many + [chunk(b"fmt ", fmt_body(1, 8000, 8)),
                                     chunk(b"data", t2[:100])]), "BAD",
                        "70 chunks before fmt")
    # --- streaming -----------------------------------------------------------
    f["LONG.WAV"] = (long_wav(), "", "~1.1 MB, 104 s: proves the scan streams")
    return f


EXPECT = {      # name -> (err, arg) or the accepted file's facts
    "TONE8.WAV": dict(rate=11025, bits=8, ch=1, frames=11025, note=N_NONE),
    "TONE8K.WAV": dict(rate=8000, bits=16, ch=1, frames=8000, note=N_NONE),
    "TONE44.WAV": dict(rate=44100, bits=8, ch=2, frames=22050, note=N_NONE),
    "IMPULSE.WAV": dict(rate=11025, bits=8, ch=1, frames=IMP_FRAMES,
                        note=N_NONE),
    "STEREO16.WAV": dict(rate=22050, bits=16, ch=2, frames=22050, note=N_NONE),
    "LISTODD.WAV": dict(rate=8000, bits=8, ch=1, frames=4000, note=N_NONE,
                        dataoff=12 + 24 + 14 + 8),
    "UNKNOWN.WAV": dict(rate=8000, bits=8, ch=1, frames=4000, note=N_NONE),
    "DATA1ST.WAV": dict(rate=8000, bits=8, ch=1, frames=4000, note=N_NONE,
                        dataoff=20),
    "EXTENS.WAV": dict(rate=11025, bits=16, ch=1, frames=5000, note=N_NONE),
    "PASTEND.WAV": dict(rate=8000, bits=8, ch=1, frames=3000, note=N_CLAMP),
    "NONWHOLE.WAV": dict(rate=11025, bits=16, ch=2, frames=1000,
                         note=N_IGNORED, notearg=3),
    "UNFIN.WAV": dict(rate=11025, bits=8, ch=1, frames=8000, note=N_RECOVER,
                      dirty=True),
    "NOTRIFF.WAV": (E_NOTRIFF, 0),
    "ADPCM.WAV": (E_NOTPCM, 0x11),
    "BITS24.WAV": (E_BITS, 24),
    "CHAN3.WAV": (E_CHANS, 3),
    "BADALIGN.WAV": (E_BALIGN, 0),
    "CHUNK70.WAV": (E_CHUNKS, 0),
    "WRAPPOS.WAV": (E_NODATA, 0),
    "LONG.WAV": dict(rate=11025, bits=8, ch=1, frames=LONG_FRAMES, note=N_NONE),
}


# =============================================================================
# THE SELFCHECK
# =============================================================================
def selfcheck(quiet=False):
    bad = []

    def ok(cond, what):
        if not cond:
            bad.append(what)
    fx = fixtures()
    ok(set(fx) == set(EXPECT), "every fixture has an expectation")
    total = 0
    for name, (data, folder, why) in sorted(fx.items()):
        total += len(data)
        r = parse(data)
        want = EXPECT[name]
        if isinstance(want, tuple):
            ok((r["err"], r["arg"]) == want,
               "%s: refused with %r, want %r" % (name, (r["err"], r["arg"]),
                                                  want))
            ok(len(sentence(r)) <= 22, "%s: the sentence fits 22" % name)
            continue
        ok(r["err"] == E_OK, "%s: refused (%d)" % (name, r["err"]))
        if r["err"]:
            continue
        for k, v in want.items():
            ok(r.get(k) == v, "%s: %s = %r, want %r" % (name, k, r.get(k), v))
        s = sentence(r)
        ok(s is None or len(s) <= 22, "%s: the note fits 22" % name)
        # --- Python's own reader, where it reads the file at all
        try:
            w = wave.open(io.BytesIO(data))
        except Exception as e:                          # noqa: BLE001
            ok(name in ("DATA1ST.WAV", "UNFIN.WAV", "EXTENS.WAV"),
               "%s: wave refuses it (%s)" % (name, e))
            continue
        ok((w.getnchannels(), w.getsampwidth() * 8, w.getframerate()) ==
           (r["ch"], r["bits"], r["rate"]), "%s: wave's format" % name)
        if name not in ("PASTEND.WAV", "NONWHOLE.WAV"):
            ok(w.getnframes() == r["frames"],
               "%s: wave says %d frames" % (name, w.getnframes()))
        got = w.readframes(r["frames"])
        ok(got == data[r["dataoff"]:r["dataoff"] + r["datalen"]],
           "%s: wave's frames are the bytes at our data offset" % name)
    ok(total <= 1440 * 1024 - 64 * 1024, "the fixtures fit a 1.44 MB disk "
       "with WaveRack beside them (%d bytes)" % total)
    # --- the impulses survive the coarse envelope at Fit, on every layout
    data = fx["IMPULSE.WAV"][0]
    r = parse(data)
    ch = samples(data, r)
    env, s = envelope(ch, r["frames"])
    for ww in (588, 615):
        z = fit_zoom(r["frames"], ww)
        ok((1 << z) >= (1 << s), "IMPULSE at Fit is a coarse view")
        cols = columns(ch, env, s, r["frames"], 0, z, ww)
        for fr, pk in IMPULSES:
            mn, mx = cols[fr >> z][0]
            ok((mx if pk > 0 else mn) == pk,
               "IMPULSE: the spike at frame %d shows at Fit (ww %d)" % (fr, ww))
    # ...and at fpp = 1 the spike is its own column, its neighbours reaching it
    cols = columns(ch, env, s, r["frames"], 990, 0, 588)
    ok(cols[10][0] == (0, 127) and cols[11][0] == (0, 127)
       and cols[12][0] == (0, 0), "IMPULSE at fpp 1: the line, not a dot")
    # --- the stereo spike is on R alone
    data = fx["STEREO16.WAV"][0]
    r = parse(data)
    ch = samples(data, r)
    env, s = envelope(ch, r["frames"])
    z = fit_zoom(r["frames"], 588)
    cols = columns(ch, env, s, r["frames"], 0, z, 588)
    ok(cols[ST_IMPULSE >> z][1][1] == 127, "STEREO16: R's spike shows")
    ok(all(c is None or c[1][0] == 0 for i, c in enumerate(cols)
           if i != ST_IMPULSE >> z), "STEREO16: R is silent elsewhere")
    ok(max(c[0][1] for c in cols if c) > 80, "STEREO16: L carries the tone")
    # --- the long file: B and the claim
    r = parse(fx["LONG.WAV"][0])
    s = block_shift(r["frames"])
    ok((r["frames"] + (1 << s) - 1) >> s <= ENV_MAXBLK and
       (r["frames"] + (1 << (s - 1)) - 1) >> (s - 1) > ENV_MAXBLK,
       "LONG: B is the smallest that fits 2,048 blocks")
    # --- playback (SPEC.md 123.6): the divisor, the refusal, the table
    for rate, hi, want in ((8000, 0, (1, 8000)), (11025, 0, (1, 11025)),
                           (22050, 0, (1, 22050)), (22222, 0, (1, 22222)),
                           (22223, 0, (2, 11111)), (44100, 0, (2, 22050)),
                           (48000, 0, (3, 16000)), (44100, 1, (1, 44100)),
                           (48000, 1, (2, 24000)), (65535, 1, (2, 32767)),
                           (4000, 0, (1, 4000)), (3999, 0, None),
                           (1000, 1, None)):
        ok(out_rate(rate, hi) == want, "out_rate(%d, %d) = %r, want %r" %
           (rate, hi, out_rate(rate, hi), want))
    ok(vtab(100) == bytes(range(256)), "VOLUME 100% is the identity")
    ok(set(vtab(0)) == {128}, "VOLUME 0% is silence")
    ok(vtab(75)[0] == 32 and vtab(75)[255] == 223 and vtab(75)[127] == 128
       and vtab(50)[1] == 65, "VOLUME 75%/50%: truncated toward 80h")
    # the conversion: 16-bit high byte, the stereo average, the box filter
    data = fx["TONE44.WAV"][0]
    r = parse(data)
    m = mono(data, r)
    raw = data[r["dataoff"]:r["dataoff"] + 4]
    ok(m[0] == (raw[0] + raw[1]) >> 1 and m[1] == (raw[2] + raw[3]) >> 1,
       "TONE44: the stereo average")
    out = convert(m, 2, 100)
    ok(len(out) == 11025 and out[1] == (m[2] + m[3]) // 2,
       "TONE44 at d = 2: 11,025 bytes, each the floor of a pair's mean")
    data = fx["TONE8K.WAV"][0]
    r = parse(data)
    m = mono(data, r)
    ok(m[2] == data[r["dataoff"] + 5] ^ 0x80, "TONE8K: a 16-bit high byte")
    ok(len(convert(m[:7], 3, 100)) == 2, "a trailing part group is dropped")
    # the transport's arithmetic: through the lap, around the loop
    ok(play_pos(0, 100, 300, 100, 300, True) == 100 and
       play_pos(199, 100, 300, 100, 300, True) == 299 and
       play_pos(200, 100, 300, 100, 300, True) == 100 and
       play_pos(450, 100, 300, 100, 300, True) == 150,
       "play_pos around a selection loop")
    ok(play_pos(500, 50, 1000, 0, 1000, False) == 550 and
       play_pos(5000, 50, 1000, 0, 1000, False) == 1000 and
       play_pos(960, 50, 1000, 0, 1000, True) == 10,
       "play_pos: to the end, and Loop with no selection (the whole)")
    ok(lap_end(400, 300, 1000) == 1000 and lap_end(100, 300, 1000) == 300,
       "a play begun past the region runs to the document's end")
    seq = frames_played(7, 8, 10, 4, 10, True)
    ok(seq == [8, 9, 4, 5, 6, 7, 8], "frames_played: the lap, then the loop")
    ok(sel_text(0, 0, 11025) == "" and
       sel_text(11025, 22050 + 5513, 11025) == "01.00 - 02.50" and
       sel_text(0, 60 * 8000 + 12000, 8000) == "0:00.0 - 1:01.5",
       "sel_text: SS.cc under a minute, M:SS.c from there")
    # --- the view's end reaches the document's below Fit (wave 2's review,
    #     M2): LONG.WAV at z = 10 on FULL's 588 columns, 48 frames past the
    #     last whole fpp - the bound rounded up shows them, and the cursor on
    #     the last frame has a column
    fr, ww = LONG_FRAMES, 588
    for z in range(0, fit_zoom(fr, ww)):
        f0 = view_clamp(1 << 40, z, ww, fr)
        ok(f0 % (1 << z) == 0 and f0 + (ww << z) >= fr and
           f0 + ((ww - 1) << z) < fr and 0 <= (fr - 1 - f0) >> z < ww,
           "view_clamp z %d: the last frame in the view at its end" % z)
    ok(view_clamp(1 << 40, fit_zoom(fr, ww), ww, fr) == 0, "Fit is frame 0")
    if not quiet or bad:
        for b in bad:
            print("wrsim: FAIL " + b)
        print("wrsim: selfcheck %s (%d fixtures, %d bytes)"
              % ("FAILED" if bad else "ok", len(fx), total))
    return not bad


def main(argv):
    if "--selfcheck" in argv:
        return 0 if selfcheck("--quiet" in argv) else 1
    if "--fixtures" in argv:
        d = argv[argv.index("--fixtures") + 1]
        for name, (data, folder, _) in fixtures().items():
            p = os.path.join(d, folder) if folder else d
            os.makedirs(p, exist_ok=True)
            with open(os.path.join(p, name), "wb") as fh:
                fh.write(data)
        return 0
    if "--ref" in argv:
        for p in argv[argv.index("--ref") + 1:]:
            r = parse(open(p, "rb").read())
            print(os.path.basename(p), r, sentence(r))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
