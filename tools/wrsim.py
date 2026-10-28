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
    E_BADFMT, E_NSRC = range(18)
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
    E_NSRC: "Clipboard uses 4 files",
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
# PLAYING THROUGH DISK READS (wrdoc.inc, wrxport.inc - SPEC.md 123.6)
# =============================================================================
RD_FIX, RD_HD, RD_DD = 16384, 12288, 9216       # wo_rdsize's reads
RINGKB, RINGKB2, LAKB, HMARGKB = 32, 16, 32, 16  # the ring, look-ahead, margin
RDT_MAX = 4096                                    # WO_RDTMAX
TICK_HZ = 18.2


def rd_size(fixed, clu):
    """a play's bounded read (wo_rdsize): 16 KB on a fixed disk; on a floppy
    about one int 13h's worth, which since SPEC.md 18.91.1 is a CYLINDER -
    12 KB where the cluster is a sector (1.2 / 1.44 MB: under 1.2's 15 KB
    cylinder, so at most two calls), 9 KB on any other (360 / 720 KB: 9
    sectors x 2 heads, a multiple of the 1 KB cluster)"""
    return RD_FIX if fixed else (RD_HD if clu == 512 else RD_DD)


def src_bps(rdb, rdt, rdsz):
    """what a source's reads have sustained (wt_srcbps): bytes x 182 / (ticks
    x 10), a tick at least; with nothing timed, the medium's floor"""
    if rdb == 0:
        return {RD_FIX: 65536, RD_HD: 24576}.get(rdsz, 12288)
    return rdb * 182 // (max(rdt, 1) * 10)


def time_reads(reads):
    """wo_rdrun's accounting over (bytes, ticks) reads: both halved together
    whenever the ticks pass RDT_MAX -> (rdb, rdt)"""
    rdb = rdt = 0
    for b, t in reads:
        rdb += b
        rdt += t
        if rdt >= RDT_MAX:
            rdt >>= 1
            rdb >>= 1
    return rdb, rdt


CYC = {(8, 1): 114, (8, 2): 205, (16, 1): 276, (16, 2): 456}   # wt_cyctab
BOX, CYCDIV, BUDGET = 300, 18643, 205


def dshare(rate, sh, medium):
    """wt_dshare: the medium's share of a second, in 256ths, a streamed play
    takes - (R << sh) x 256 / the medium, both over 16"""
    m16 = medium >> 4
    if m16 >= 65536:
        return 0
    if m16 == 0:
        return 0xFFFF
    return min(((rate << sh) >> 4) * 256 // m16, 0xFFFF)


def cshare(rate, cost, tier0=True):
    """wt_cshare: the CPU's share, 256ths, of a conversion costing COST
    cycles a frame at RATE frames a second - on an 8088 only"""
    return min(rate * cost // CYCDIV, 0xFFFF) if tier0 else 0


def hold_kb(frames):
    """the hold's claim: ONE BYTE A FRAME - mono()'s value, the 8-bit mono
    sample the conversion starts from - rounded up to a KB"""
    return (frames + 1023) >> 10


def hold_bytes(data, r, lo, hi):
    """what wt_hput writes into the hold for frames [lo, hi), from the raw
    frames as its four loops take them (8-bit as it is, 16-bit's high byte
    XOR 80h, a stereo pair's add-and-rcr) - the selfcheck holds it to
    mono(), which is what makes a held play the card's same bytes"""
    ba = r["bits"] // 8 * r["ch"]
    raw = data[r["dataoff"] + lo * ba:r["dataoff"] + hi * ba]
    out = bytearray()
    for i in range(0, len(raw) - ba + 1, ba):
        f = raw[i:i + ba]
        u = [b ^ 0x80 for b in f[1::2]] if r["bits"] == 16 else list(f)
        out.append(u[0] if len(u) == 1 else (u[0] + u[1]) >> 1)
    return bytes(out)


def plan(rate, bits, ch, d, medium, frames, largest_kb, la_held=False,
         tier0=True):
    """wt_plan -> ('stream' | 'hold' | 'slow-disk' | 'slow-cpu', the hold's
    KB). A play streams when the medium's share and the conversion's
    together stay inside the budget (80% of a second); else it is held when
    the hold, the ring, the margin and (unless held) the look-ahead fit the
    heap's largest run and the held play's own conversion (8-bit mono) fits
    the budget; else it plays with gaps, blaming the CPU when its share
    alone is past the budget"""
    sh = {1: 0, 2: 1, 4: 2}[bits // 8 * ch]
    box = BOX if d > 1 else 0
    csh = cshare(rate, CYC[(bits, ch)] + box, tier0)
    tot = dshare(rate, sh, medium) + csh
    if tot <= 0xFFFF and tot <= BUDGET:
        return "stream", 0
    kb = hold_kb(frames)
    want = kb + RINGKB + 1 + HMARGKB + (0 if la_held else LAKB)
    if want <= largest_kb and cshare(rate, CYC[(8, 1)] + box,
                                     tier0) <= BUDGET:
        return "hold", kb
    return ("slow-cpu" if csh > BUDGET else "slow-disk"), 0


def ring_secs(out, ringkb=RINGKB):
    """how long the external ring plays alone at OUT samples a second -
    what a disk call anywhere on the machine may take before a gap"""
    return ringkb * 1024 / out


def streams(out, d, sh, readsz, bps, call_s, ringkb=RINGKB):
    """the pipeline's guarantee (SPEC.md 123.6): the ring is FULL as each of
    our reads starts, so a read of READSZ bytes at BPS plus CALL_S of seek
    and latency is silent-free when it takes less than the ring plays, and
    the play keeps up when the reads bring the source's bytes faster than
    they are spent. -> (no gap inside a read, keeps up)"""
    t = call_s + readsz / bps
    return t < ring_secs(out, ringkb), readsz / t >= out * d * (1 << sh)


DISK_FX = {     # name -> (rate, bits, ch, seconds, why): tests/waverackdisk.py
    "BIG22.WAV": (22050, 8, 1, 95, "2 MB of 8-bit mono 22,050 Hz: streams "
                  "off a fixed disk - 15% of an XT's second the disk's, 53% "
                  "the conversion's"),
    "MID11.WAV": (11025, 8, 1, 9, "99 KB of 8-bit mono 11,025 Hz: saved on "
                  "a volume of 32 KB clusters, four of them"),
    "ST22S.WAV": (22050, 16, 2, 4, "4 s of 16-bit stereo 22,050 Hz: 88 KB/s, "
                  "past MartyPC's disk - held"),
    "ST22L.WAV": (22050, 16, 2, 20, "20 s of the same: past the disk AND the "
                  "heap - it plays with gaps, and says so"),
}


def disk_fixture(name):
    """a DISK_FX file: a slow sweep (so every second differs) at -6 dB, made
    with bytes.translate/array so megabytes cost milliseconds"""
    rate, bits, ch, secs, _ = DISK_FX[name]
    n = rate * secs
    sec = tone(rate, rate, 440, 20000)
    if bits == 8:
        one = pcm8(sec)
        out = bytearray()
        k = 0
        while len(out) < n:
            a = 30 + (k * 23) % 70
            out += one.translate(bytes(max(0, min(255, 128 + (v - 128) * a
                                                  // 100)) for v in range(256)))
            k += 1
        data = bytes(out[:n])
    else:
        left = pcm16(sec)
        right = pcm16(tone(rate, rate, 660, 16000))
        st = bytearray(len(left) * 2)
        st[0::4], st[1::4] = left[0::2], left[1::2]
        st[2::4], st[3::4] = right[0::2], right[1::2]
        data = (bytes(st) * (secs + 1))[:n * 4]
    return riff([chunk(b"fmt ", fmt_body(ch, rate, bits)),
                 chunk(b"data", data)])


def disk_selfcheck(ok):
    """the arithmetic SPEC.md 123.6 pins, and the soak row's predictions"""
    ok(rd_size(True, 2048) == 16384 and rd_size(False, 512) == 12288 and
       rd_size(False, 1024) == 9216, "the reads: 16 KB fixed, 12/9 KB floppy")
    ok(src_bps(0, 0, RD_FIX) == 65536 and src_bps(0, 0, RD_DD) == 12288,
       "an untimed source's floor")
    ok(src_bps(38 * 1024 * 10, 182, RD_FIX) == 38 * 1024 * 10 * 182 // 1820,
       "bytes x 18.2 / ticks")
    rdb, rdt = time_reads([(16384, 8)] * 1000)
    ok(rdt < RDT_MAX and abs(src_bps(rdb, rdt, RD_FIX) - 16384 * 18.2 / 8)
       < 40, "halving keeps the ratio (%d %d)" % (rdb, rdt))
    mart = 148000                       # MartyPC's XT-IDE, as WaveRack's
    ok(dshare(22050, 0, mart) == 38 and cshare(22050, 114) == 134,
       "BIG22 on MartyPC: 38 + 134 of 256 - a 4.77 MHz 8088's 53% is the "
       "conversion")                    # reads timed it (wave 4b's scan)
    ok(plan(22050, 8, 1, 1, mart, 22050 * 95, 400)[0] == "stream",
       "BIG22 streams off MartyPC's disk")
    ok(plan(22050, 16, 2, 1, mart, 22050 * 4, 400) == ("hold", 87),
       "ST22S is held (87 KB: one byte a frame) - its 16-bit stereo "
       "conversion alone is 2.1 s a second on an 8088")
    ok(plan(22050, 16, 2, 1, mart, 22050 * 20, 400)[0] == "slow-cpu",
       "ST22L neither streams nor fits: gaps, and the CPU named")
    ok(plan(22050, 16, 2, 1, mart, 22050 * 20, 400, tier0=False)[0] ==
       "stream", "...where a 286's CPU is not the limit: it streams")
    ok(plan(22050, 16, 2, 1, 38 * 1024, 22050 * 20, 400,
            tier0=False)[0] == "slow-disk",
       "...and a slow disk under a fast CPU blames the disk")
    ok(plan(11025, 8, 1, 1, src_bps(0, 0, RD_HD), 1150000, 400)[0] ==
       "stream", "LONG.WAV streams off a 1.44 MB floppy at its floor")
    ok(plan(44100, 8, 1, 2, mart, 44100, 400)[0] == "slow-cpu",
       "44.1 kHz at d = 2 is past an 8088 whether it streams or is held")
    for name in ("STEREO16.WAV", "TONE8K.WAV", "TONE44.WAV"):
        data, _, _ = fixtures()[name]
        r = parse(data)
        held = hold_bytes(data, r, 0, r["frames"])
        ok(list(held) == mono(data, r) and
           convert(list(held), 1, 75) == convert(mono(data, r), 1, 75),
           "%s: a held play gives the card a streamed play's bytes" % name)
    ok(abs(ring_secs(22050) - 1.486) < 0.01 and
       abs(ring_secs(11025) - 2.972) < 0.01, "the ring: 1.49 s / 2.97 s")
    ok(streams(22050, 1, 0, 16384, 38 * 1024, 0.0) == (True, True),
       "a 16 KB read at 38 KB/s fits inside the ring at 22 kHz")
    dd = 300 // 60 * 9 * 512            # a 360 KB track a revolution
    ok(streams(11025, 1, 0, RD_DD, dd, 0.2) == (True, True),
       "a 9 KB read off a 360 KB floppy (0.2 s to find it) keeps 11 kHz up")
    ok(streams(11025, 1, 0, 4096, dd, 0.2)[1] is False,
       "...where a 4 KB track-sized one would NOT: a call costs a call")
    ok(streams(22050, 1, 2, 16384, 38 * 1024, 0.0)[1] is False,
       "16-bit stereo cannot keep up at 38 KB/s")
    for name in DISK_FX:
        r = parse(disk_fixture(name))
        rate, bits, ch, secs, _ = DISK_FX[name]
        ok(r["err"] == E_OK and (r["rate"], r["bits"], r["ch"], r["frames"])
           == (rate, bits, ch, rate * secs), "%s parses as made" % name)
    for s_ in ("Loading 100%", "Load cancelled", "Disk too slow: gaps",
               "CPU too slow: gaps"):
        ok(len(s_) <= 22, "'%s' fits 22" % s_)


# =============================================================================
# EDITING AND SAVING (wredit.inc, wrsave.inc - SPEC.md 123.3, 123.9, 123.10)
# =============================================================================
WO_NPC = 64             # pieces a document may hold
WO_NCLIP = 16           # pieces the clipboard may hold
WO_NUNDO = 8            # undo steps; the claim holds WO_NUNDO + 1 states
X_OK, X_NOSEL, X_NPC, X_CPLX, X_FMT, X_NOCLIP, X_NOHIST = range(7)
XSAY = {X_NPC: "Too many edits; save", X_CPLX: "Selection too complex",
        X_NOCLIP: "Clipboard is empty"}


def pc_add(out, piece):
    """append (src, first, count) to a piece list, COALESCED: a piece that
    continues the last one's source where it stopped extends it, and an empty
    one is nothing - so a cut pasted back where it came from is one piece
    again, and repeated edits do not leak pieces"""
    src, first, cnt = piece
    if not cnt:
        return
    if out and out[-1][0] == src and out[-1][1] + out[-1][2] == first:
        out[-1] = (src, out[-1][1], out[-1][2] + cnt)
    else:
        out.append((src, first, cnt))


def pc_frames(pieces):
    return sum(p[2] for p in pieces)


def pc_range(pieces, a, b, out=None):
    """the document frames [a, b) as pieces, appended to OUT (coalesced)"""
    out = [] if out is None else out
    pos = 0
    for src, first, cnt in pieces:
        lo, hi = max(a, pos), min(b, pos + cnt)
        if lo < hi:
            pc_add(out, (src, first + lo - pos, hi - lo))
        pos += cnt
    return out


def clip_text(fmt):
    """Paste's refusal, naming the clipboard's format in 22 characters"""
    rate, bits, ch = fmt
    return "Clip: %d %db %s" % (rate, bits, "stereo" if ch == 2 else "mono")


class Editor:
    """the document's piece list, selection, cursor, clipboard and undo, as
    wredit.inc keeps them. A state is (pieces, cur, s, e, anc); the history
    holds at most WO_NUNDO + 1 of them, the current one at [pos]: an edit
    first writes the selection the user has NOW into [pos] (an undo puts it
    back), drops the redo side, and appends its result - the oldest falling
    off - so Undo is pos - 1 and Redo pos + 1."""

    def __init__(self, pieces=(), fmt=None):
        self.pieces = list(pieces)
        self.fmt = fmt              # None: an empty document (after New)
        self.cur = self.s = self.e = self.anc = 0
        self.clip, self.cfmt = [], None
        self.hist, self.pos = [], 0
        self.dirty = False

    frames = property(lambda self: pc_frames(self.pieces))

    def state(self):
        return (tuple(self.pieces), self.cur, self.s, self.e, self.anc)

    def load(self, st):
        p, self.cur, self.s, self.e, self.anc = st
        self.pieces = list(p)

    def select(self, s, e):
        self.s, self.e = min(s, e), max(s, e)
        self.anc = self.s
        if self.s < self.e:
            self.cur = self.s

    def commit(self, new, cur, s, e):
        if len(new) > WO_NPC:
            return X_NPC
        if not self.hist:
            self.hist, self.pos = [self.state()], 0
        else:
            p = self.hist[self.pos][0]
            self.hist[self.pos] = (p, self.cur, self.s, self.e, self.anc)
        del self.hist[self.pos + 1:]
        self.pieces, self.cur, self.s, self.e, self.anc = new, cur, s, e, s
        self.hist.append(self.state())
        if len(self.hist) > WO_NUNDO + 1:
            del self.hist[0]
        self.pos = len(self.hist) - 1
        self.dirty = True
        return X_OK

    def trim(self):
        if self.s >= self.e:
            return X_NOSEL
        return self.commit(pc_range(self.pieces, self.s, self.e), 0, 0, 0)

    def delete(self):
        if self.s >= self.e:
            return X_NOSEL
        new = pc_range(self.pieces, 0, self.s)
        pc_range(self.pieces, self.e, self.frames, new)
        return self.commit(new, self.s, self.s, self.s)

    def copy(self):
        if self.s >= self.e:
            return X_NOSEL
        c = pc_range(self.pieces, self.s, self.e)
        if len(c) > WO_NCLIP:
            return X_CPLX
        self.clip, self.cfmt = c, self.fmt
        return X_OK

    def cut(self):
        r = self.copy()
        return r if r else self.delete()

    def paste(self):
        if not self.clip:
            return X_NOCLIP
        if self.fmt is None:            # an empty document takes the clip's
            self.fmt = self.cfmt
        elif self.fmt != self.cfmt:
            return X_FMT
        a, b = (self.s, self.e) if self.s < self.e else (self.cur, self.cur)
        new = pc_range(self.pieces, 0, a)
        for p in self.clip:
            pc_add(new, p)
        pc_range(self.pieces, b, self.frames, new)
        n = pc_frames(self.clip)
        return self.commit(new, a, a, a + n)

    def undo(self):
        if not self.hist or self.pos == 0:
            return X_NOHIST
        self.pos -= 1
        self.load(self.hist[self.pos])
        self.dirty = True
        return X_OK

    def redo(self):
        if self.pos >= len(self.hist) - 1:
            return X_NOHIST
        self.pos += 1
        self.load(self.hist[self.pos])
        self.dirty = True
        return X_OK

    def new(self):
        """File > New: the document emptied, the history with it, the
        clipboard KEPT (SPEC.md 123.3)"""
        self.pieces, self.fmt, self.hist, self.pos = [], None, [], 0
        self.cur = self.s = self.e = self.anc = 0
        self.dirty = False


def wav_header(fmt, nbytes):
    """the canonical 44-byte header (SPEC.md 123.4's writer): RIFF, WAVE, a
    16-byte fmt chunk, data - the RIFF size counting the pad byte"""
    rate, bits, ch = fmt
    ba = ch * bits // 8
    return (b"RIFF" + struct.pack("<I", 36 + nbytes + (nbytes & 1)) +
            b"WAVE" + b"fmt " + struct.pack("<IHHIIHH", 16, 1, ch, rate,
                                           rate * ba, ba, bits) +
            b"data" + struct.pack("<I", nbytes))


def write_wav(pieces, sources, fmt, a=0, b=None):
    """the file Save writes for document frames [a, b) (all of it by
    default): the header, then each piece's frames read out of its source
    (SOURCES[src] = the source's data bytes, frame 0 first), then a pad byte
    when the data's length is odd"""
    ba = fmt[2] * fmt[1] // 8
    b = pc_frames(pieces) if b is None else b
    data = b"".join(sources[s][f * ba:(f + n) * ba]
                    for s, f, n in pc_range(pieces, a, b))
    return wav_header(fmt, len(data)) + data + (b"\0" if len(data) & 1 else b"")


def edit_script():
    """the edit sequence the soak row types on the guest (tests/
    waverackedit.py) and the reference replays: (command, argument).
    'sel' selects [a, b) frames, 'cur' places the cursor"""
    return [("sel", (1000, 3000)), ("cut", None), ("cur", 500), ("paste", None),
            ("sel", (6000, 7000)), ("delete", None), ("undo", None),
            ("redo", None), ("sel", (100, 9000)), ("copy", None),
            ("cur", 2000), ("paste", None), ("undo", None), ("undo", None),
            ("redo", None)]


def run_script(ed, script):
    for cmd, arg in script:
        if cmd == "sel":
            ed.select(*arg)
        elif cmd == "cur":
            ed.s = ed.e = ed.anc = ed.cur = arg
        else:
            r = getattr(ed, cmd)()
            assert r == X_OK, (cmd, r)
    return ed


# =============================================================================
# THE FIXTURES
# =============================================================================
SINE =[round(math.sin(2 * math.pi * i / 4096) * 32767) for i in range(4096)]


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
    # an 18-byte fmt (cbSize 0, as many writers make it): the data at byte
    # 46, so with 4-byte frames one straddles every whole-cluster read's end
    # - the reader's slide (wrdoc.inc) carries it, sequentially
    st46 = pcm16(interleave(tone(4410, 11025, 700, 22000),
                            tone(4410, 11025, 1100, 15000)))
    f["ODD46.WAV"] = (riff([chunk(b"fmt ", fmt_body(2, 11025, 16) + b"\0\0"),
                            chunk(b"data", st46)]), "GOOD",
                      "stereo 16-bit 11,025 Hz, 0.4 s, an 18-byte fmt: data "
                      "at 46, a frame across the 16 KB read's end")
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
    "ODD46.WAV": dict(rate=11025, bits=16, ch=2, frames=4410, note=N_NONE,
                      dataoff=46),
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
    # --- editing (SPEC.md 123.9): every step against a FLAT model - the
    #     document as a plain list of frames, edited by slicing - so the
    #     piece arithmetic is checked by something that has none
    ok(pc_range([(0, 0, 100)], 10, 20) == [(0, 10, 10)] and
       pc_range([(0, 0, 10), (1, 5, 10)], 5, 15) == [(0, 5, 5), (1, 5, 5)],
       "pc_range splits at piece edges")
    p = []
    for q in ((0, 0, 5), (0, 5, 5), (0, 0, 0), (1, 10, 2), (0, 10, 1)):
        pc_add(p, q)
    ok(p == [(0, 0, 10), (1, 10, 2), (0, 10, 1)], "pc_add coalesces")
    for name in ("TONE8.WAV", "STEREO16.WAV"):
        data = fx[name][0]
        r = parse(data)
        fmt, ba = (r["rate"], r["bits"], r["ch"]), r["balign"]
        src = {0: data[r["dataoff"]:r["dataoff"] + r["datalen"]]}
        frm = [src[0][i * ba:(i + 1) * ba] for i in range(r["frames"])]
        ed = Editor([(0, 0, r["frames"])], fmt)
        flat, clip, hist = list(frm), [], [list(frm)]
        pos = 0
        for cmd, arg in edit_script():
            if cmd == "sel":
                ed.select(*arg)
                continue
            if cmd == "cur":
                ed.s = ed.e = ed.anc = ed.cur = arg
                continue
            s, e, c = ed.s, ed.e, ed.cur
            ok(getattr(ed, cmd)() == X_OK, "%s: %s" % (name, cmd))
            if cmd in ("cut", "copy"):
                clip = flat[s:e]
            if cmd in ("cut", "delete"):
                flat = flat[:s] + flat[e:]
            elif cmd == "paste":
                a, b = (s, e) if s < e else (c, c)
                flat = flat[:a] + clip + flat[b:]
            if cmd in ("cut", "delete", "paste", "trim"):
                del hist[pos + 1:]
                hist.append(list(flat))
                pos = len(hist) - 1
            elif cmd == "undo":
                pos -= 1
                flat = list(hist[pos])
            elif cmd == "redo":
                pos += 1
                flat = list(hist[pos])
            got = write_wav(ed.pieces, src, fmt)
            ok(got[44:44 + len(flat) * ba] == b"".join(flat),
               "%s: after %s the pieces read the flat model's frames"
               % (name, cmd))
        w = wave.open(io.BytesIO(got))
        ok((w.getnchannels(), w.getsampwidth() * 8, w.getframerate(),
            w.getnframes()) == (fmt[2], fmt[1], fmt[0], len(flat)),
           "%s: wave reads the saved file" % name)
        rr = parse(got)
        ok(rr["err"] == E_OK and rr["dataoff"] == 44 and
           rr["frames"] == len(flat), "%s: parse() reads it back" % name)
        sel = write_wav(ed.pieces, src, fmt, 100, 1101)
        ok(sel[44:] == b"".join(flat[100:1101]) + (b"\0" if 1001 * ba & 1
                                                    else b""),
           "%s: Save Selection As writes [s, e)" % name)
    # the pad byte, and the RIFF size that counts it
    odd = write_wav([(0, 0, 3)], {0: b"\x10\x20\x30"}, (8000, 8, 1))
    ok(len(odd) == 48 and odd[-1] == 0 and struct.unpack_from("<I", odd, 4)[0]
       == 40 and struct.unpack_from("<I", odd, 40)[0] == 3,
       "an odd data length: the pad byte, outside data's size, inside RIFF's")
    # a cut pasted back where it came from is ONE piece again
    ed = Editor([(0, 0, 1000)], (8000, 8, 1))
    ed.select(100, 200)
    ed.cut()
    ed.s = ed.e = ed.cur = 100
    ed.paste()
    ok(ed.pieces == [(0, 0, 1000)], "a cut pasted back coalesces to one piece")
    # undo: eight steps, the oldest falling off; a new edit drops the redo
    ed = Editor([(0, 0, 1000)], (8000, 8, 1))
    for k in range(10):
        ed.select(0, 10)
        ed.delete()
    n = 0
    while ed.undo() == X_OK:
        n += 1
    ok(n == WO_NUNDO and ed.frames == 1000 - 10 * (10 - WO_NUNDO),
       "undo: %d steps back, the oldest gone (%d)" % (WO_NUNDO, n))
    ed.redo()
    ed.select(0, 1)
    ed.delete()
    ok(ed.redo() == X_NOHIST, "a new edit drops the redo side")
    # the WO_NPC refusal: nothing changes
    ed = Editor([(0, 0, 10000)], (8000, 8, 1))
    ed.select(0, 1)
    ed.copy()
    r = X_OK
    k = 0
    while r == X_OK:
        ed.s = ed.e = ed.cur = 100 + 50 * k
        before = list(ed.pieces)
        r = ed.paste()
        k += 1
    ok(r == X_NPC and ed.pieces == before and len(before) <= WO_NPC,
       "past %d pieces the edit is refused and nothing changes" % WO_NPC)
    # the clipboard: its format named, paste of another refused, an empty
    # document adopts it
    ok(len(clip_text((65535, 16, 2))) <= 22 and
       clip_text((22050, 16, 2)) == "Clip: 22050 16b stereo",
       "Paste's refusal names the clipboard's format in 22 characters")
    ed.new()
    ok(ed.clip and ed.paste() == X_OK and ed.fmt == (8000, 8, 1) and
       ed.frames == 1, "New keeps the clipboard; a paste adopts its format")
    ed2 = Editor([(1, 0, 50)], (11025, 8, 1))
    ed2.clip, ed2.cfmt = ed.clip, ed.cfmt
    ok(ed2.paste() == X_FMT, "a paste of another format is refused")
    for s_ in XSAY.values():
        ok(len(s_) <= 22, "'%s' fits 22" % s_)
    disk_selfcheck(ok)
    if not quiet or bad:
        for b in bad:
            print("wrsim: FAIL " + b)
        print("wrsim: selfcheck %s (%d fixtures, %d bytes)"
              % ("FAILED" if bad else "ok", len(fx), total))
    return not bad


def main(argv):
    if "--selfcheck" in argv:
        return 0 if selfcheck("--quiet" in argv) else 1
    if "--disk-fixtures" in argv:
        d = argv[argv.index("--disk-fixtures") + 1]
        os.makedirs(d, exist_ok=True)
        for name in DISK_FX:
            with open(os.path.join(d, name), "wb") as fh:
                fh.write(disk_fixture(name))
        return 0
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
