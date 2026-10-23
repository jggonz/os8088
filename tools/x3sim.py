#!/usr/bin/env python3
"""x3sim: XTMP3's reference Layer III decoder (SPEC.md 121.4) - PLACEHOLDER DOCSTRING

(replaced below once the numerics are fixed)
"""
import argparse
import math
import os
import struct
import subprocess
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import x3tab_iso as ISO  # noqa: E402
import x3tables as TB    # noqa: E402

# =============================================================================
# x3p_: THE PARSER (the spec for apps/xtmp3/x3pars.inc)
# =============================================================================

X3_RESERVOIR = 4096        # bytes of main data kept (a ring on the target)
X3_FIRST_READ = 4096       # the first read: ID3v2 frames are looked for here
X3_TITLE = 40              # bytes of title / artist kept
X3_ID3_MAX_TAGS = 4        # consecutive ID3v2 tags skipped at the head

# refusals, by name (SPEC.md 121.2)
REFUSE_FREE = 'FREE FORMAT'
REFUSE_L1 = 'LAYER I'
REFUSE_L2 = 'LAYER II'
REFUSE_NOSYNC = 'NO MPEG AUDIO'

# frame verdicts
F_OK = 'ok'
F_SHORTRES = 'reservoir'     # main_data_begin reaches past what is held: silence
F_BADSIDE = 'badside'        # side info exceeds its own budget: dropped
F_TRUNC = 'truncated'        # the file ends inside the frame: dropped, end
F_CHANGE = 'changed'         # a frame's channel count differs from the first's (counted, adapted)
F_BADGR = 'badgranule'       # a granule's Huffman data overran: that granule silent


class Header:
    __slots__ = ('word', 'version', 'layer', 'crc', 'br_idx', 'sr_idx', 'pad',
                 'mode', 'mode_ext', 'bitrate', 'rate', 'size', 'nch', 'lsf',
                 'free')

    @property
    def mpeg1(self):
        return self.version == 1

    @property
    def ms(self):
        return self.mode == 1 and bool(self.mode_ext & 2)

    @property
    def intensity(self):
        return self.mode == 1 and bool(self.mode_ext & 1)

    @property
    def granules(self):
        return 2 if self.version == 1 else 1

    @property
    def side_bytes(self):
        if self.version == 1:
            return 17 if self.nch == 1 else 32
        return 9 if self.nch == 1 else 17


def parse_header(b, o):
    """x3p_header: the four bytes at o, or None if they are not a header we
    could believe (sync, no reserved field).  Free format parses, with
    size 0 and free set, so the caller can refuse it by name."""
    if o + 4 > len(b):
        return None
    w = (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]
    if (w >> 21) & 0x7FF != 0x7FF:
        return None
    vb = (w >> 19) & 3
    lb = (w >> 17) & 3
    br = (w >> 12) & 15
    sr = (w >> 10) & 3
    if vb == 1 or lb == 0 or br == 15 or sr == 3:
        return None
    h = Header()
    h.word = w
    h.version = {0: 25, 2: 2, 3: 1}[vb]
    h.layer = 4 - lb
    h.crc = not ((w >> 16) & 1)
    h.br_idx = br
    h.sr_idx = sr
    h.pad = (w >> 9) & 1
    h.mode = (w >> 6) & 3
    h.mode_ext = (w >> 4) & 3
    h.nch = 1 if h.mode == 3 else 2
    h.lsf = h.version != 1
    h.rate = ISO.SAMPLE_RATE[h.version][sr]
    h.bitrate = ISO.BITRATE[h.version == 1][h.layer][br]
    h.free = br == 0
    if h.free:
        h.size = 0
    elif h.layer == 1:
        h.size = (12000 * h.bitrate // h.rate + h.pad) * 4
    elif h.layer == 2 or h.version == 1:
        h.size = 144000 * h.bitrate // h.rate + h.pad
    else:
        h.size = 72000 * h.bitrate // h.rate + h.pad
    return h


def agrees(a, b):
    """Two headers belong to one stream: version, layer and rate."""
    return a.version == b.version and a.layer == b.layer and a.sr_idx == b.sr_idx


def synchsafe(b4):
    if any(x & 0x80 for x in b4):
        return None
    return (b4[0] << 21) | (b4[1] << 14) | (b4[2] << 7) | b4[3]


def _text_field(data):
    """An ID3 text frame's payload as at most X3_TITLE printable ASCII bytes:
    byte 0 is the encoding (0 Latin-1, 1 UTF-16 with BOM, 2 UTF-16BE,
    3 UTF-8); a character outside 0x20..0x7E becomes '?'; NUL ends it."""
    if not data:
        return ''
    enc, body = data[0], data[1:]
    out = []
    if enc in (1, 2):
        be = enc == 2
        if enc == 1 and len(body) >= 2:
            if body[:2] == b'\xfe\xff':
                be = True
                body = body[2:]
            elif body[:2] == b'\xff\xfe':
                be = False
                body = body[2:]
        for k in range(0, len(body) - 1, 2):
            hi, lo = (body[k], body[k + 1]) if be else (body[k + 1], body[k])
            if hi == 0 and lo == 0:
                break
            out.append(chr(lo) if hi == 0 and 0x20 <= lo < 0x7F else '?')
            if len(out) == X3_TITLE:
                break
    else:
        for c in body:
            if c == 0:
                break
            if enc == 3 and 0x80 <= c < 0xC0:
                continue            # a UTF-8 continuation byte: one '?' a char
            out.append(chr(c) if 0x20 <= c < 0x7F else '?')
            if len(out) == X3_TITLE:
                break
    return ''.join(out).strip()


def id3v2_scan(b, o, info):
    """x3p_id3: skip ID3v2 tags at o by their synchsafe size, never reading a
    tag whole; TIT2/TPE1 (TT2/TP1 in v2.2) are copied when they lie inside
    the first X3_FIRST_READ bytes of the file.  Returns the first byte after
    the tags."""
    for _ in range(X3_ID3_MAX_TAGS):
        if b[o:o + 3] != b'ID3' or o + 10 > len(b):
            return o
        ver, flags = b[o + 3], b[o + 5]
        size = synchsafe(b[o + 6:o + 10])
        if ver not in (2, 3, 4) or b[o + 4] == 0xFF or size is None:
            return o
        end = o + 10 + size + (10 if ver == 4 and flags & 0x10 else 0)
        info['id3v2'] = info.get('id3v2', 0) + (end - o)
        p = o + 10
        lim = min(o + 10 + size, X3_FIRST_READ, len(b))
        if not flags & 0x80:                    # unsynchronised tags: no title
            if flags & 0x40 and ver >= 3:       # an extended header
                if ver == 3:
                    p += 4 + int.from_bytes(b[p:p + 4], 'big')
                else:
                    es = synchsafe(b[p:p + 4])
                    p = lim if es is None else p + es
            hdr = 6 if ver == 2 else 10
            while p + hdr <= lim:
                fid = b[p:p + (3 if ver == 2 else 4)]
                if ver == 2:
                    fs = int.from_bytes(b[p + 3:p + 6], 'big')
                elif ver == 3:
                    fs = int.from_bytes(b[p + 4:p + 8], 'big')
                else:
                    fs = synchsafe(b[p + 4:p + 8])
                if fs is None or fid[:1] == b'\0' or fs == 0:
                    break
                body = p + hdr
                if body + fs > lim:
                    break                       # not inside the first read
                key = {b'TIT2': 'title', b'TT2': 'title',
                       b'TPE1': 'artist', b'TP1': 'artist'}.get(bytes(fid))
                if key and key not in info:
                    t = _text_field(b[body:body + fs])
                    if t:
                        info[key] = t
                p = body + fs
        o = end
    return o


def id3v1(b, info):
    """The last 128 bytes: TAG + title[30] + artist[30] ...  Returns the end
    of the audio."""
    n = len(b)
    if n >= 128 and b[n - 128:n - 125] == b'TAG':
        for key, a in (('title', 3), ('artist', 33)):
            raw = b[n - 128 + a:n - 128 + a + 30]
            t = ''.join(chr(c) if 0x20 <= c < 0x7F else '?'
                        for c in raw.split(b'\0')[0])[:X3_TITLE].strip()
            if t and key not in info:
                info[key] = t
        info['id3v1'] = True
        return n - 128
    return n


def find_sync(b, o, end, lock=None):
    """x3p_sync: the first offset >= o where a header is followed, at its own
    frame size, by a header that agrees with it (and with `lock`, once the
    stream is known).  Free-format headers cannot be checked and are passed
    over.  A header whose frame ends exactly at `end` is also believed (the
    last frame, or a file of one frame); with `lock` set, so is one whose
    frame would end inside the last three bytes.  Returns (offset, header)
    or None."""
    for p in range(o, end - 3):
        if b[p] != 0xFF:
            continue
        h = parse_header(b, p)
        if h is None or h.free:
            continue
        if lock is not None and not (agrees(h, lock) and h.layer == 3):
            continue
        n = p + h.size
        if n + 4 <= end:
            h2 = parse_header(b, n)
            if h2 is not None and agrees(h, h2):
                return p, h
        elif n == end or (lock is not None and n < end):
            return p, h
    return None


class SideInfo:
    pass


class Granule:
    __slots__ = ('part23', 'big_values', 'global_gain', 'sfc', 'win_switch',
                 'block_type', 'mixed', 'table', 'region0', 'region1',
                 'subblock_gain', 'preflag', 'scale', 'count1_table', 'is_scale')


class BitReader:
    """x3p_bits: big-endian bit reads from a byte buffer.  Reading past the
    buffer returns zero bits and sets `over` (the target never reads past
    its buffer either: the bytes there are not this stream's)."""

    def __init__(self, buf, pos=0):
        self.buf = bytes(buf) + b'\0\0\0\0'
        self.n = len(buf) * 8
        self.pos = pos
        self.over = False

    def get(self, k):
        if k == 0:
            return 0
        v = self.peek(k)
        self.pos += k
        if self.pos > self.n:
            self.over = True
        return v

    def peek(self, k):
        p = self.pos
        if p >= self.n:
            return 0
        i = p >> 3
        w = int.from_bytes(self.buf[i:i + 4], 'big')
        return (w >> (32 - (p & 7) - k)) & ((1 << k) - 1)


def parse_side(h, data):
    """x3p_side: side information from the bytes after the header (and CRC).
    Returns a SideInfo, or None if a field is reserved (block_type 0 with
    window switching, big_values > 288, Huffman table 4 or 14)."""
    r = BitReader(data)
    s = SideInfo()
    nch = h.nch
    if h.mpeg1:
        s.main_data_begin = r.get(9)
        r.get(5 if nch == 1 else 3)
        s.scfsi = [[r.get(1) for _ in range(4)] for _ in range(nch)]
    else:
        s.main_data_begin = r.get(8)
        r.get(1 if nch == 1 else 2)
        s.scfsi = [[0] * 4 for _ in range(nch)]
    s.gr = []
    ok = True
    for g in range(h.granules):
        row = []
        for ch in range(nch):
            gi = Granule()
            gi.part23 = r.get(12)
            gi.big_values = r.get(9)
            gi.global_gain = r.get(8)
            gi.sfc = r.get(4 if h.mpeg1 else 9)
            gi.win_switch = r.get(1)
            if gi.win_switch:
                gi.block_type = r.get(2)
                gi.mixed = r.get(1)
                gi.table = [r.get(5), r.get(5), 0]
                gi.subblock_gain = [r.get(3), r.get(3), r.get(3)]
                gi.region0 = 8 if gi.block_type == 2 and not gi.mixed else 7
                gi.region1 = 36             # i.e. everything else is region 1
                if gi.block_type == 0:
                    ok = False
            else:
                gi.block_type = 0
                gi.mixed = 0
                gi.table = [r.get(5), r.get(5), r.get(5)]
                gi.subblock_gain = [0, 0, 0]
                gi.region0 = r.get(4)
                gi.region1 = r.get(3)
            if h.mpeg1:
                gi.preflag = r.get(1)
            else:
                gi.preflag = 0                # set by the scalefactor decode
            gi.scale = r.get(1)
            gi.count1_table = r.get(1)
            if gi.big_values > 288 or 4 in gi.table[:3] or 14 in gi.table[:3]:
                ok = False
            row.append(gi)
        s.gr.append(row)
    return s if ok else None


def xing_info(b, o, h, info):
    """Xing/Info/VBRI in the first frame, and a LAME tag's delay/padding.
    Returns True if the frame is a tag frame (metadata, not audio)."""
    base = o + 4 + (2 if h.crc else 0) + h.side_bytes
    tag = b[base:base + 4]
    if tag in (b'Xing', b'Info'):
        flags = int.from_bytes(b[base + 4:base + 8], 'big')
        p = base + 8
        if flags & 1:
            info['frames'] = int.from_bytes(b[p:p + 4], 'big')
            p += 4
        if flags & 2:
            info['bytes'] = int.from_bytes(b[p:p + 4], 'big')
            p += 4
        if flags & 4:
            p += 100
        if flags & 8:
            p += 4
        if p + 24 <= o + h.size and b[p:p + 4] in (b'LAME', b'Lavf', b'Lavc', b'GOGO'):
            d = b[p + 21:p + 24]
            info['delay'] = (d[0] << 4) | (d[1] >> 4)
            info['padding'] = ((d[1] & 15) << 8) | d[2]
            info['encoder'] = b[p:p + 9].decode('latin-1').rstrip('\0 ')
        info['vbrtag'] = tag.decode()
        return True
    vb = o + 4 + 32
    if b[vb:vb + 4] == b'VBRI':
        info['delay'] = int.from_bytes(b[vb + 6:vb + 8], 'big')
        info['frames'] = int.from_bytes(b[vb + 14:vb + 18], 'big')
        info['vbrtag'] = 'VBRI'
        return True
    return False


class Stream:
    """An opened MP3: its metadata and the frame iterator."""

    def __init__(self, data):
        self.b = bytes(data)
        self.info = {}
        self.refused = None
        self.counts = {'frames': 0, F_SHORTRES: 0, F_BADSIDE: 0, F_TRUNC: 0,
                       F_CHANGE: 0, F_BADGR: 0, 'resync': 0, 'skipped': 0,
                       'maxreach': 0}
        b = self.b
        self.start = id3v2_scan(b, 0, self.info)
        self.end = id3v1(b, self.info)
        h0 = parse_header(b, self.start)
        if h0 is not None and h0.free:
            self.refused = REFUSE_FREE
            return
        s = find_sync(b, self.start, self.end)
        if s is None:
            self.refused = REFUSE_NOSYNC
            return
        o, h = s
        if h.layer != 3:
            self.refused = REFUSE_L1 if h.layer == 1 else REFUSE_L2
            return
        self.first = o
        self.h0 = h
        self.counts['skipped'] += o - self.start
        self.tagframe = xing_info(b, o, h, self.info)
        self.info.update(version=h.version, rate=h.rate, nch=h.nch,
                         bitrate=h.bitrate)
        if 'frames' in self.info:
            self.info['duration'] = self.info['frames'] * 576 * h.granules / h.rate
            self.info['exact'] = True
        else:
            self.info['duration'] = (self.end - o) * 8 / (h.bitrate * 1000)
            self.info['exact'] = False

    def frames(self):
        """x3p_next: yields (header, side info or None, main data bytes for
        this frame's granules or None, verdict).  The reservoir is a byte
        string bounded at X3_RESERVOIR."""
        b, end = self.b, self.end
        o = self.first
        lock = self.h0
        res = b''
        if self.tagframe:
            o += self.h0.size
        while o + 4 <= end:
            h = parse_header(b, o)
            if h is None or not agrees(h, lock) or h.layer != 3 or h.free:
                s = find_sync(b, o + 1, end, lock)
                self.counts['resync'] += 1
                if s is None:
                    self.counts['skipped'] += end - o
                    return
                self.counts['skipped'] += s[0] - o
                o, h = s
            if o + h.size > end:
                self.counts[F_TRUNC] += 1
                yield h, None, None, F_TRUNC
                return
            self.counts['frames'] += 1
            p = o + 4 + (2 if h.crc else 0)
            main = b[p + h.side_bytes:o + h.size]
            side = parse_side(h, b[p:p + h.side_bytes]) if len(main) >= 0 and \
                p + h.side_bytes <= o + h.size else None
            verdict = F_OK
            md = None
            if h.nch != lock.nch:
                self.counts[F_CHANGE] += 1      # adapted by the decoder
            if side is None:
                verdict = F_BADSIDE
            else:
                mdb = side.main_data_begin
                total = sum(gi.part23 for row in side.gr for gi in row)
                if total > (mdb + len(main)) * 8:
                    verdict = F_BADSIDE
                elif mdb > len(res):
                    verdict = F_SHORTRES
                else:
                    md = res[len(res) - mdb:] + main
                    self.counts['maxreach'] = max(self.counts['maxreach'], mdb)
            if verdict != F_OK:
                self.counts[verdict] += 1
            res = (res + main)[-X3_RESERVOIR:]
            yield h, side, md, verdict
            o += h.size


# =============================================================================
# PART 2 AND 3: scalefactors and Huffman (shared by every model)
# =============================================================================

def _cum(widths):
    c = [0]
    for w in widths:
        c.append(c[-1] + w)
    return c


class Bands:
    """The scalefactor-band layout of one (version, rate)."""

    def __init__(self, version, sr_idx):
        self.long = ISO.SFB_LONG[(version, sr_idx)]
        self.short = ISO.SFB_SHORT[(version, sr_idx)]
        self.lc = _cum(self.long)            # 23 boundaries, lines
        self.sc = _cum(self.short)           # 14 boundaries, lines per window
        self.mixed_long = 8 if version == 1 else 6

    def line_map(self, gi):
        """For each of the 576 lines (in BITSTREAM order): (kind, sfb, window,
        subband).  kind 0 = long, 1 = short."""
        out = []
        if gi.block_type != 2:
            for s in range(22):
                for l in range(self.lc[s], self.lc[s + 1]):
                    out.append((0, s, 0, l // 18))
            return out
        first = 3 if gi.mixed else 0
        if gi.mixed:
            for s in range(self.mixed_long):
                for l in range(self.lc[s], self.lc[s + 1]):
                    out.append((0, s, 0, l // 18))
        for s in range(first, 13):
            for w in range(3):
                for f in range(self.sc[s], self.sc[s + 1]):
                    out.append((1, s, w, f // 6))
        assert len(out) == 576
        return out


_BANDS = {}
_LMAP = {}


def bands(h):
    k = (h.version, h.sr_idx)
    if k not in _BANDS:
        _BANDS[k] = Bands(h.version, h.sr_idx)
    return _BANDS[k]


def line_map(h, gi):
    k = (h.version, h.sr_idx, gi.block_type == 2, gi.mixed if gi.block_type == 2 else 0)
    if k not in _LMAP:
        _LMAP[k] = bands(h).line_map(gi)
    return _LMAP[k]


class Scf:
    """Decoded scalefactors of one channel/granule: long[22], short[13][3],
    and for intensity, whether each is_pos is the illegal (no-IS) value."""

    def __init__(self):
        self.long = [0] * 22
        self.short = [[0, 0, 0] for _ in range(13)]
        self.ill_long = [False] * 22
        self.ill_short = [[False] * 3 for _ in range(13)]


def read_scf_mpeg1(r, gi, scfsi, gr, prev):
    """x3p_scf1: MPEG-1 scalefactors.  scfsi reuse copies granule 0's long
    scalefactors group by group."""
    s = Scf()
    slen1, slen2 = ISO.SLEN_MPEG1[gi.sfc]
    if gi.block_type == 2:
        if gi.mixed:
            for b in range(8):
                s.long[b] = r.get(slen1)
            lo = 3
        else:
            lo = 0
        for b in range(lo, 12):
            n = slen1 if b < 6 else slen2
            for w in range(3):
                s.short[b][w] = r.get(n)
    else:
        groups = ((0, 6), (6, 11), (11, 16), (16, 21))
        for gi_, (a, e) in enumerate(groups):
            n = slen1 if gi_ < 2 else slen2
            if gr == 1 and scfsi[gi_] and prev is not None:
                for b in range(a, e):
                    s.long[b] = prev.long[b]
            else:
                for b in range(a, e):
                    s.long[b] = r.get(n)
    # MPEG-1 intensity: is_pos 7 is illegal (checked in the stereo stage)
    return s


ILLEGAL2 = 'iso'
TOPRULE = 'default'
PREFLAG_IS = False


def _illegal2(n, v, mx):
    if ILLEGAL2 == 'iso':
        return n > 0 and v == mx
    if ILLEGAL2 == 'seven':
        return v == 7
    return False


def read_scf_mpeg2(r, gi, right_is):
    """x3p_scf2: MPEG-2/2.5 scalefactors (13818-3 2.4.3.2).  Sets
    gi.preflag, and gi.is_scale for the intensity-stereo right channel."""
    s = Scf()
    sfc = gi.sfc
    if right_is:
        gi.is_scale = sfc & 1
        sfc >>= 1
        if sfc < 180:
            slen, tbl = [sfc // 36, (sfc % 36) // 6, sfc % 6, 0], 3
        elif sfc < 244:
            v = sfc - 180
            slen, tbl = [v // 16, (v % 16) // 4, v % 4, 0], 4
        else:
            v = sfc - 244
            slen, tbl = [v // 3, v % 3, 0, 0], 5
        gi.preflag = 1 if (PREFLAG_IS and gi.sfc >= 500) else 0
    else:
        if sfc < 400:
            slen, tbl = [(sfc >> 4) // 5, (sfc >> 4) % 5, (sfc & 15) >> 2, sfc & 3], 0
        elif sfc < 500:
            v = sfc - 400
            slen, tbl = [(v >> 2) // 5, (v >> 2) % 5, v & 3, 0], 1
        else:
            v = sfc - 500
            slen, tbl = [v // 3, v % 3, 0, 0], 2
        gi.preflag = 1 if sfc >= 500 else 0
    kind = 0 if gi.block_type != 2 else (2 if gi.mixed else 1)
    counts = ISO.NR_OF_SFB[tbl][kind]
    vals, ill = [], []
    for part in range(4):
        n = slen[part]
        mx = (1 << n) - 1
        for _ in range(counts[part]):
            v = r.get(n)
            vals.append(v)
            ill.append(right_is and _illegal2(n, v, mx))
    k = 0
    if kind == 0:
        for b in range(21):
            s.long[b], s.ill_long[b] = vals[k], ill[k]
            k += 1
    else:
        if kind == 2:
            for b in range(6):
                s.long[b], s.ill_long[b] = vals[k], ill[k]
                k += 1
            lo = 3
        else:
            lo = 0
        for b in range(lo, 12):
            for w in range(3):
                s.short[b][w], s.ill_short[b][w] = vals[k], ill[k]
                k += 1
    return s


def huffman(r, gi, end, h, tables, count=False):
    """x3p_huff: the 576 quantised values of one channel/granule, in
    bitstream order.  Returns (ix list, ok, stats).  ok is False when the
    big-values region ran past the granule's own end (part2_3_length)."""
    ix = [0] * 576
    nbig = min(gi.big_values * 2, 576)
    bd = bands(h)
    if gi.win_switch:
        r1 = 3 * bd.sc[3] if gi.block_type == 2 else bd.lc[8]
        r2 = 576
    else:
        r1 = bd.lc[min(gi.region0 + 1, 22)]
        r2 = bd.lc[min(gi.region0 + gi.region1 + 2, 22)]
    lookups = 0
    i = 0
    while i < nbig:
        reg = 0 if i < r1 else (1 if i < r2 else 2)
        t = gi.table[reg]
        if t == 0:
            ix[i] = ix[i + 1] = 0
            i += 2
            continue
        tab, first = tables[ISO.TREE[t]]
        p0 = r.pos
        v, used, lk = TB.huff_decode(tab, lambda n: _peekat(r, p0, n), first)
        lookups += lk
        r.pos += used
        lin = ISO.LINBITS[t]
        x, y = v >> 4, v & 15
        if x == 15 and lin:
            x += r.get(lin)
        if x and r.get(1):
            x = -x
        if y == 15 and lin:
            y += r.get(lin)
        if y and r.get(1):
            y = -y
        ix[i], ix[i + 1] = x, y
        i += 2
    nsym = i // 2
    if r.pos > end or r.over:
        return [0] * 576, False, ((lookups, nsym) if count else lookups)
    tab, first = tables[32 + gi.count1_table]
    while i + 4 <= 576 and r.pos < end:
        p0 = r.pos
        v, used, lk = TB.huff_decode(tab, lambda n: _peekat(r, p0, n), first)
        lookups += lk
        if p0 + used > end:
            break
        r.pos = p0 + used
        q = [(v >> 3) & 1, (v >> 2) & 1, (v >> 1) & 1, v & 1]
        for k in range(4):
            if q[k] and r.get(1):
                q[k] = -1
        if r.pos > end:
            break
        ix[i:i + 4] = q
        i += 4
        nsym += 1
    return ix, True, ((lookups, nsym) if count else lookups)


def _peekat(r, p0, n):
    save = r.pos
    r.pos = p0
    v = r.peek(n)
    r.pos = save
    return v


_HUFTAB = None


def huff_tables():
    global _HUFTAB
    if _HUFTAB is None:
        _HUFTAB = {t: (TB.build_huff(t), TB.first_width(t))
                   for t in TB.PAIR_TREES + TB.COUNT1_TREES}
    return _HUFTAB


def quarter_exponents(h, gi, scf, ms):
    """The requantisation exponent of each line in quarter powers of two
    (bitstream order): global_gain - 210 - 8*subblock_gain - mult*(sf + pre),
    mult = 2 or 4 by scalefac_scale, less 2 more when the frame is M/S (its
    1/sqrt(2), so the stereo stage is an add and a subtract)."""
    lm = line_map(h, gi)
    mult = 4 if gi.scale else 2
    base = gi.global_gain - 210 - (2 if ms else 0)
    q = np.empty(576, dtype=np.int64)
    for l, (kind, b, w, _) in enumerate(lm):
        if kind == 0:
            pre = ISO.PRETAB[b] if gi.preflag else 0
            q[l] = base - mult * (scf.long[b] + pre)
        else:
            q[l] = base - 8 * gi.subblock_gain[w] - mult * scf.short[b][w]
    return q


# =============================================================================
# STEREO decisions (shared): which lines are M/S, which intensity, and how
# =============================================================================

def stereo_plan(h, gi_r, scf_r, ix_r):
    """For a joint-stereo granule: a list over the 576 lines (bitstream order)
    of ('ms' | 'lr' | ('is', pos-code)) decided from the RIGHT channel.
    The intensity bound is the first band above the right channel's last
    non-zero quantised value: per window for short blocks, over the whole
    band list for long and mixed blocks.  The top band (long 21, short 12)
    carries no is_pos and uses the one below it (long 20, short 11).
    pos-code: MPEG-1 is_pos 0..6, MPEG-2 is_pos with the scale bit."""
    lm = line_map(h, gi_r)
    n = 576
    ms = h.ms
    plan = ['ms' if ms else 'lr'] * n
    if not h.intensity:
        return plan
    # the last non-zero line of the right channel, per window for short blocks
    short = gi_r.block_type == 2 and not gi_r.mixed
    top = [-1, -1, -1]
    last = -1
    for l in range(n):
        if ix_r[l]:
            kind, b, w, _ = lm[l]
            last = l
            if short:
                top[w] = max(top[w], b)
    for l in range(n):
        kind, b, w, _ = lm[l]
        if short:
            above = b > top[w]
        else:
            above = l > last and not (last >= 0 and _same_band(lm, l, last))
        if not above:
            continue
        if kind == 0:
            bb = 20 if b == 21 else b
            pos, ill = scf_r.long[bb], scf_r.ill_long[bb]
            below = (b == 21 and last >= 0 and lm[last][1] >= 20 and lm[last][0] == 0)
        else:
            bb = 11 if b == 12 else b
            pos, ill = scf_r.short[bb][w], scf_r.ill_short[bb][w]
            below = (b == 12 and top[w] >= 11) if short else False
        if h.mpeg1:
            ill = pos >= 7
        if TOPRULE == 'default' and below:
            pos, ill = (3 if h.mpeg1 else 0), False
        if not ill:
            plan[l] = ('is', pos)
    return plan


def _same_band(lm, a, b):
    return lm[a][:3] == lm[b][:3]


# =============================================================================
# THE FLOAT REFERENCE (diagnosis only: the same parser, float64 arithmetic)
# =============================================================================

def is_factors(h, gi_r, pos):
    """(kl, kr) of an intensity line, float, including sqrt(2) when M/S."""
    if h.mpeg1:
        kl, kr = TB.is_mpeg1()[pos]
    else:
        k = TB.is_mpeg2(getattr(gi_r, 'is_scale', 0))[pos]
        kl, kr = (k, 1.0) if pos & 1 else (1.0, k)
    if h.ms:
        kl *= math.sqrt(2)
        kr *= math.sqrt(2)
    return kl, kr


def reorder_index(h, gi):
    """The permutation from bitstream order to subband order (short blocks
    interleave their three windows line by line): out[k] = in[perm[k]]."""
    if gi.block_type != 2:
        return None
    bd = bands(h)
    perm = list(range(576))
    first = 3 if gi.mixed else 0
    start = 36 if gi.mixed else 0
    pos = start
    for s in range(first, 13):
        wdt = bd.short[s]
        for i in range(wdt):
            for w in range(3):
                perm[pos] = start + 3 * (bd.sc[s] - bd.sc[first]) + w * wdt + i
                pos += 1
    return perm


def imdct_kind(gi, b):
    """Which transform subband b of a granule takes: -1 the three short
    IMDCTs, 0 the long one with the normal window's first half, 1 with the
    stop window's.  With window switching and the mixed flag, subbands 0 and
    1 are long with the normal window whatever the block type (ISO dist10's
    `bt = (mixed && sb < 2) ? 0 : block_type`, which start and stop blocks
    obey too).  A silent granule (gi None) is a normal long block."""
    if gi is None:
        return 0
    if gi.win_switch and gi.mixed and b < 2:
        return 0
    if gi.block_type == 2:
        return -1
    return 1 if gi.block_type == 3 else 0


def imdct36_f(x, ovl, win):
    """minimp3's L3_imdct36 for one subband, float.  x: 18 inputs (modified
    in place to the 18 outputs); ovl: 9 overlap values (in/out)."""
    co = [0.0] * 9
    si = [0.0] * 9
    co[0] = -x[0]
    si[0] = x[17]
    for i in range(4):
        si[8 - 2 * i] = x[4 * i + 1] - x[4 * i + 2]
        co[1 + 2 * i] = x[4 * i + 1] + x[4 * i + 2]
        si[7 - 2 * i] = x[4 * i + 4] - x[4 * i + 3]
        co[2 + 2 * i] = -(x[4 * i + 3] + x[4 * i + 4])
    dct3_9_f(co)
    dct3_9_f(si)
    for k in (1, 3, 5, 7):
        si[k] = -si[k]
    tw0, tw1 = TW9_F
    for i in range(9):
        o = ovl[i]
        s = co[i] * tw1[i] + si[i] * tw0[i]
        ovl[i] = co[i] * tw0[i] - si[i] * tw1[i]
        x[i] = o * win[i] - s * win[9 + i]
        x[17 - i] = o * win[9 + i] + s * win[i]


def dct3_9_f(y):
    C = TB.DCT9_C
    s0, s2, s4, s6, s8 = y[0], y[2], y[4], y[6], y[8]
    t0 = s0 + s6 * 0.5
    s0 -= s6
    t4 = (s4 + s2) * C['c2']
    t2 = (s8 + s2) * C['c1']
    s6 = (s4 - s8) * C['c3']
    s4 += s8 - s2
    s2 = s0 - s4 * 0.5
    y[4] = s4 + s0
    s8 = t0 - t2 + s6
    s0 = t0 - t4 + t2
    s4 = t0 + t4 - s6
    s1, s3, s5, s7 = y[1], y[3], y[5], y[7]
    s3 *= C['s3']
    t0 = (s5 + s1) * C['c4']
    t4 = (s5 - s7) * C['c5']
    t2 = (s1 + s7) * C['c6']
    s1 = (s1 - s5 - s7) * C['s3']
    s5 = t0 - s3 - t2
    s7 = t4 - s3 - t0
    s3 = t4 + s3 - t2
    y[0] = s4 - s7
    y[1] = s2 + s1
    y[2] = s0 - s3
    y[3] = s8 + s5
    y[5] = s8 - s5
    y[6] = s0 + s3
    y[7] = s2 - s1
    y[8] = s4 + s7


def idct3_f(x0, x1, x2):
    m1 = x1 * TB.DCT9_C['s3']
    a1 = x0 - x2 * 0.5
    return [a1 + m1, x0 + x2, a1 - m1]


def imdct12_f(x, dst, ovl):
    """minimp3's L3_imdct12: x[0], x[3], ... x[15] (stride 3) in, 6 outputs
    to dst, 3 overlap values ovl (in/out)."""
    co = idct3_f(-x[0], x[6] + x[3], x[12] + x[9])
    si = idct3_f(x[15], x[12] - x[9], x[6] - x[3])
    si[1] = -si[1]
    t0, t1 = TW3_F
    for i in range(3):
        o = ovl[i]
        s = co[i] * t1[i] + si[i] * t0[i]
        ovl[i] = co[i] * t0[i] - si[i] * t1[i]
        dst[i] = o * t0[2 - i] - s * t1[2 - i]
        dst[5 - i] = o * t1[2 - i] + s * t0[2 - i]


def imdct_short_f(x, ovl):
    """minimp3's L3_imdct_short for one subband: x 18 in/out, ovl 9."""
    tmp = list(x)
    out = list(ovl[:6]) + [0.0] * 12
    o6 = ovl[6:9]
    d = [0.0] * 6
    imdct12_f(tmp, d, o6)
    out[6:12] = d
    imdct12_f(tmp[1:], d, o6)
    out[12:18] = d
    d2 = [0.0] * 6
    imdct12_f(tmp[2:], d2, o6)
    ovl[0:6] = d2
    ovl[6:9] = o6
    x[:] = out


def _f_consts():
    tw0 = [math.sin(math.pi * (19 + 2 * i) / 72.0) for i in range(9)]
    tw1 = [math.cos(math.pi * (19 + 2 * i) / 72.0) for i in range(9)]
    t30 = [math.sin(math.pi * (7 + 2 * i) / 24.0) for i in range(3)]
    t31 = [math.cos(math.pi * (7 + 2 * i) / 24.0) for i in range(3)]
    return (tw0, tw1), (t30, t31)


TW9_F, TW3_F = _f_consts()
WIN_F = TB.mdct_window_float()


# =============================================================================
# POLYPHASE SYNTHESIS: the tap layout every model shares
# =============================================================================
#
# ISO's synthesis (V = N.S over a 1,024-word shift buffer, U gathered from V,
# W = U.D, sixteen W summed per output) rewritten in terms of the 32-point
# DCT-II of the subband samples, X[m] = sum_k S[k] cos(pi (2k+1) m / 64):
# V[i] = X[16+i] for i < 16, 0 for i = 16, -X[48-i] for 17..47, -X[i-48]
# for 48..63.  Output j of a slot is then
#
#   out[j] = sum_{i=0..7} D[64i+j] * sA_j * X_{age 2i}[a_j]
#                       + D[64i+32+j] * sB_j * X_{age 2i+1}[b_j]
#
# with a_j = 16+j (j < 16, sA +1), none (j = 16), 48-j (j > 16, sA -1), and
# b_j = 16-j (j <= 16) or j-16 (j > 16), sB = -1.  So the history is the last
# sixteen X vectors (32 words each), not V, and output j reads one X index
# from each of them: 16 products (8 for j = 16).
#
# REDUCED RATE.  With the subbands at and above N = 32/div zero, the
# decimated output out[div*m] needs only X[div*l], and X[div*l] is exactly
# the N-point DCT-II of S[0..N-1]:  sum_{k<N} S[k] cos(pi (2k+1) l / (2N)).
# So divisor 2 runs a 16-point DCT and keeps 16 words a slot, divisor 4 an
# 8-point one and 8 words: the same window taps D[64i + div*m], the indices
# divided by div.  Nothing is computed at the full rate and thrown away.


def synth_taps(div):
    """For each kept output m of a slot (32/div of them): a list of
    (age, X index in the N-point vector, D index, sign)."""
    taps = []
    for m in range(32 // div):
        j = div * m
        t = []
        for i in range(8):
            if j < 16:
                a, sa = 16 + j, 1
            elif j > 16:
                a, sa = 48 - j, -1
            else:
                a = None
            if a is not None:
                assert a % div == 0
                t.append((2 * i, a // div, 64 * i + j, sa))
            b = 16 - j if j <= 16 else j - 16
            assert b % div == 0
            t.append((2 * i + 1, b // div, 64 * i + 32 + j, -1))
        taps.append(t)
    return taps


def dct2_matrix(n):
    k = np.arange(n)
    return np.cos(np.pi * np.outer(np.arange(n), 2 * k + 1) / (2 * n))


class FloatSynth:
    def __init__(self, div):
        self.div = div
        self.n = 32 // div
        self.ring = np.zeros((16, self.n))
        self.pos = 0
        self.C = dct2_matrix(self.n)
        taps = synth_taps(div)
        D = np.array(ISO.D_INT, dtype=np.float64) / 65536.0
        nt = 16
        self.age = np.zeros((self.n, nt), dtype=np.int64)
        self.idx = np.zeros((self.n, nt), dtype=np.int64)
        self.co = np.zeros((self.n, nt))
        for m, t in enumerate(taps):
            for k, (age, ix, di, s) in enumerate(t):
                self.age[m, k], self.idx[m, k], self.co[m, k] = age, ix, s * D[di]

    def slot(self, s):
        """s: the N kept subband samples of one slot -> N output samples."""
        self.pos = (self.pos + 1) & 15
        self.ring[self.pos] = self.C @ s
        rows = (self.pos - self.age) & 15
        return (self.co * self.ring[rows, self.idx]).sum(axis=1)


# =============================================================================
# THE STANDARD INTEGER MODEL (x3s_): what apps/xtmp3/x3std.asm computes
# =============================================================================
#
# Every value below is an int; numpy int64 arrays hold vectors of them, and
# every operation is one an 8086 performs exactly:
#   16-bit:  an add or subtract SATURATED to int16 (a `jo` to the clamp on the
#            target); an `imul` of an int16 by a 16-bit constant into 32 bits;
#            a sum of two such products in 32 bits (it cannot overflow: both
#            factors are at most 2^15 in magnitude, the constants < 2^15);
#            a rounding shift (acc + 2^(s-1)) >> s, arithmetic (floor),
#            then saturated to int16.
#   32-bit:  an int32 in a register pair; add/adc, saturated to int32; a
#            product of an int32 by a 16-bit constant is the exact 48-bit
#            product (two multiplies), rounded the same way back to int32.
# Integer addition is associative only without saturation, so the ORDER
# written here is part of the definition.

STAGES = ('requant', 'stereo', 'alias', 'scale', 'imdct', 'mix', 'dct', 'window')
KINDS = ('mul', 'mul32', 'add', 'add32', 'shift', 'read')
I32 = (-(1 << 31), (1 << 31) - 1)

# the formats: one definition, in x3tables
X3_XRQ, X3_SBQ, X3_IMDCT_HEAD, X3_K_MIN = TB.X3_XRQ, TB.X3_SBQ, TB.X3_IMDCT_HEAD, TB.X3_K_MIN


class Ops:
    """Saturation counts per stage (always kept: STATS reports them), the
    output clip count, and - when `counting` - operation counts per stage."""

    def __init__(self, counting=False):
        self.counting = counting
        self.sat = {s: 0 for s in STAGES}
        self.n = {s: {k: 0 for k in KINDS} for s in STAGES}
        self.clip = 0

    def count(self, st, kind, k):
        if self.counting:
            self.n[st][kind] += k


OPS = Ops()


def _size(a):
    return a.size if isinstance(a, np.ndarray) else 1


def _sat(x, lo, hi, st):
    c = np.clip(x, lo, hi)
    k = int(np.count_nonzero(c != x)) if isinstance(x, np.ndarray) else int(c != x)
    if k:
        OPS.sat[st] += k
    return c


def s16(x, st):
    return _sat(x, -32768, 32767, st)


def s32(x, st):
    return _sat(x, I32[0], I32[1], st)


def iadd(a, b, st):
    OPS.count(st, 'add', _size(a))
    return s16(a + b, st)


def isub(a, b, st):
    OPS.count(st, 'add', _size(a))
    return s16(a - b, st)


def ineg(a, st):
    OPS.count(st, 'add', _size(a))
    return s16(-a, st)


def ihalf(a):
    """x/2 rounded half up: (x + 1) >> 1 (never saturates)."""
    return (a + 1) >> 1


def imul(a, c, st, sh=15):
    """round(a * c / 2^sh), saturated to int16: one imul, one rounding shift."""
    OPS.count(st, 'mul', _size(a))
    OPS.count(st, 'shift', _size(a))
    return s16((a * c + (1 << (sh - 1))) >> sh, st)


def imul2(a, c1, b, c2, st, sh=15):
    """round((a*c1 + b*c2) / 2^sh), saturated to int16: two imuls summed in
    32 bits, one rounding shift."""
    n = _size(a)
    OPS.count(st, 'mul', 2 * n)
    OPS.count(st, 'add', n)
    OPS.count(st, 'shift', n)
    return s16((a * c1 + b * c2 + (1 << (sh - 1))) >> sh, st)


def iadd32(a, b, st):
    OPS.count(st, 'add32', _size(a))
    return s32(a + b, st)


def isub32(a, b, st):
    OPS.count(st, 'add32', _size(a))
    return s32(a - b, st)


def imul32(a, c, st, sh):
    """round(a * c / 2^sh) for an int32 a and a 16-bit constant c (the exact
    48-bit product), saturated to int32."""
    OPS.count(st, 'mul32', _size(a))
    return s32((a * c + (1 << (sh - 1))) >> sh, st)


def imul2_32(a, c1, b, c2, st, sh=15):
    n = _size(a)
    OPS.count(st, 'mul32', 2 * n)
    OPS.count(st, 'add32', n)
    return s32((a * c1 + b * c2 + (1 << (sh - 1))) >> sh, st)


# ---- the constants, from x3tables (the one definition) ----------------------

Q = TB.STD


# ---- requantisation ---------------------------------------------------------

def requant_i(ix, q):
    """x3s_requant: xr as int32 Q28 from the Huffman values ix and the
    quarter exponents q (arrays of 576).  |ix|^(4/3) = M * 2^(E-14) from the
    pow43 table (interpolated above X3_P43_N), 2^(q/4) = F[q & 3] * 2^(q >> 2)
    with F in Q14, so xr * 2^28 = M * F >> s,  s = -E - (q >> 2): one
    unsigned 16x16 multiply (M * F < 2^30) and one rounding shift.  s > 31
    gives 0; s <= 0 shifts left (exact) instead, and a result above
    2^31 - 1 saturates."""
    a = np.abs(ix)
    nz = a > 0
    n = int(np.count_nonzero(nz))
    OPS.count('requant', 'mul', n)
    OPS.count('requant', 'shift', n)
    OPS.count('requant', 'read', 3 * n)
    m, e = pow43_i(a)
    s = 28 - X3_XRQ - e - (q >> 2)
    p = m * np.asarray(Q.P43_F, dtype=np.int64)[q & 3]
    sc = np.clip(s, 1, 62)
    r = (p + (np.int64(1) << (sc - 1))) >> sc
    r = np.where(s > 31, 0, r)
    left = np.clip(-s, 0, 8)
    r = np.where(s <= 0, p << left, r)
    over = (s < -8) | (r > I32[1])
    r = np.where(over, I32[1], r)
    k = int(np.count_nonzero(over & nz))
    if k:
        OPS.sat['requant'] += k
    r = np.where(nz, r, 0)
    return np.where(ix < 0, -r, r)


_P43M = np.asarray(Q.P43_M, dtype=np.int64)
_P43E = np.asarray(Q.P43_E, dtype=np.int64)


def pow43_i(a):
    """(M, E) of |ix| for an array: a table read below X3_P43_N; above it,
    linear interpolation between the entries at j = a >> 3 and j + 1, in j's
    octave (the upper mantissa doubled when its exponent is one higher):
    M = Mj + ((Mj1 - Mj) * (a & 7) + 4) >> 3, then x16 (E + 4) and, if
    M >= 32768, M = (M + 1) >> 1 and E + 1."""
    small = a < TB.X3_P43_N
    ai = np.where(small, a, 0)
    m = _P43M[ai].copy()
    e = _P43E[ai].copy()
    if not np.all(small):
        big = ~small
        j = np.where(big, a >> 3, 0)
        f = a & 7
        mj, ej = _P43M[j], _P43E[j]
        mj1 = _P43M[j + 1] << (_P43E[j + 1] - ej)
        mi = mj + (((mj1 - mj) * f + 4) >> 3)
        ei = ej + 4
        hi = mi >= 32768
        mi = np.where(hi, (mi + 1) >> 1, mi)
        ei = np.where(hi, ei + 1, ei)
        m = np.where(big, mi, m)
        e = np.where(big, ei, e)
        OPS.count('requant', 'mul', int(np.count_nonzero(big)))
    return m, e


# ---- stereo -----------------------------------------------------------------

def stereo_i(h, gi_r, plan, L, R):
    """x3s_stereo, int32 Q28: M/S is an add and a subtract (1/sqrt 2 was
    folded into requantisation); an intensity line is xL times kl and kr
    (Q14 constants)."""
    ms = np.array([p == 'ms' for p in plan])
    isl = np.array([isinstance(p, tuple) for p in plan])
    L2, R2 = L.copy(), R.copy()
    if ms.any():
        a, b = L[ms], R[ms]
        L2[ms] = iadd32(a, b, 'stereo')
        R2[ms] = isub32(a, b, 'stereo')
    if isl.any():
        kl = np.zeros(576, dtype=np.int64)
        kr = np.zeros(576, dtype=np.int64)
        for l in np.nonzero(isl)[0]:
            kl[l], kr[l] = is_factors_q(h, gi_r, plan[l][1])
        a = L[isl]
        L2[isl] = imul32(a, kl[isl], 'stereo', 14)
        R2[isl] = imul32(a, kr[isl], 'stereo', 14)
        OPS.count('stereo', 'read', 2 * int(np.count_nonzero(isl)))
    return L2, R2


def is_factors_q(h, gi_r, pos):
    if h.mpeg1:
        return (Q.IS1_QMS if h.ms else Q.IS1_Q)[pos]
    s = getattr(gi_r, 'is_scale', 0) or 0
    k = (Q.IS2_QMS if h.ms else Q.IS2_Q)[s][pos]
    one = Q.SQRT2_Q14 if h.ms else 16384
    return (k, one) if pos & 1 else (one, k)


# ---- antialias --------------------------------------------------------------

def alias_i(x, nb):
    """x3s_alias, int32 Q28: butterflies across the boundaries of subbands
    0..nb, eight a boundary: u' = u cs - d ca, d' = d cs + u ca (Q15)."""
    if nb <= 0:
        return x
    x = x.copy()
    b = np.arange(1, nb + 1)
    for i in range(8):
        u = x[18 * b + i]
        d = x[18 * b - 1 - i]
        cs, ca = Q.AA_CS[i], Q.AA_CA[i]
        x[18 * b + i] = imul2_32(u, cs, d, -ca, 'alias')
        x[18 * b - 1 - i] = imul2_32(d, cs, u, ca, 'alias')
    OPS.count('alias', 'read', 16 * nb)
    return x


# ---- the block scale of a subband ------------------------------------------

def band_scale(xb):
    """x3s_scale: each subband's 18 int32 Q28 lines become int16 at its own
    Q(14 + k), with bl = bitlength(max |x|):
        k = max(min(29 - X3_IMDCT_HEAD - bl, 14), min(0, 29 - bl)),
    then at least X3_K_MIN; x16 = (x + 2^(13-k)) >> (14 - k) (k = 14: x
    itself), saturated.  A quiet band's largest input uses 15 - HEAD bits,
    leaving the IMDCT's internal sums that much room.  A loud band stays at
    Q14 (k = 0) - as fine as the fixed format and no coarser, its internal
    values bounded by the absolute range instead - and only a band at or
    above 2.0 (a stream clipping far past full scale) takes k < 0, enough to
    hold it in 16 bits.  A silent band takes 14.  Returns (k, x16)."""
    m = np.abs(xb).max(axis=1)
    bl = np.array([int(v).bit_length() for v in m], dtype=np.int64)
    k = np.maximum(np.minimum(29 - X3_IMDCT_HEAD - bl, 14), np.minimum(0, 29 - bl))
    k = np.maximum(k, X3_K_MIN)
    sh = (14 - k)[:, None]
    r = np.where(sh > 0, (xb + (np.int64(1) << np.maximum(sh - 1, 0))) >> sh, xb)
    nb = xb.shape[0]
    OPS.count('scale', 'add32', 18 * nb)
    OPS.count('scale', 'shift', 18 * nb)
    return k, s16(r, 'scale')


# ---- the IMDCT ---------------------------------------------------------------

def dct3_9_i(y):
    """minimp3's 9-point DCT-III in 16-bit integers (y: 9 arrays, returned
    new).  Constants Q15; x*0.5 is ihalf."""
    C = Q.DCT9
    st = 'imdct'
    s0, s2, s4, s6, s8 = y[0], y[2], y[4], y[6], y[8]
    t0 = iadd(s0, ihalf(s6), st)
    s0 = isub(s0, s6, st)
    t4 = imul(iadd(s4, s2, st), C['c2'], st)
    t2 = imul(iadd(s8, s2, st), C['c1'], st)
    s6 = imul(isub(s4, s8, st), C['c3'], st)
    s4 = iadd(s4, isub(s8, s2, st), st)
    s2 = isub(s0, ihalf(s4), st)
    o4 = iadd(s4, s0, st)
    s8 = iadd(isub(t0, t2, st), s6, st)
    s0 = iadd(isub(t0, t4, st), t2, st)
    s4 = isub(iadd(t0, t4, st), s6, st)
    s1, s3, s5, s7 = y[1], y[3], y[5], y[7]
    s3 = imul(s3, C['s3'], st)
    t0 = imul(iadd(s5, s1, st), C['c4'], st)
    t4 = imul(isub(s5, s7, st), C['c5'], st)
    t2 = imul(iadd(s1, s7, st), C['c6'], st)
    s1 = imul(isub(isub(s1, s5, st), s7, st), C['s3'], st)
    s5 = isub(isub(t0, s3, st), t2, st)
    s7 = isub(isub(t4, s3, st), t0, st)
    s3 = isub(iadd(t4, s3, st), t2, st)
    return [isub(s4, s7, st), iadd(s2, s1, st), isub(s0, s3, st), iadd(s8, s5, st),
            o4, isub(s8, s5, st), iadd(s0, s3, st), isub(s2, s1, st), iadd(s4, s7, st)]


def _merge(o, w, co, a, si, b, k, sign):
    """o*w (Q14 x Q15) + sign * ((co*a + si*b) >> k) (Q(14+k) x Q15, floor-
    shifted to Q29), rounded to Q14: the window and twiddle in one rounding.
    The 32-bit sums cannot overflow (x3tables' --selfcheck proves the bound)."""
    st = 'imdct'
    n = _size(o)
    OPS.count(st, 'mul', 3 * n)
    OPS.count(st, 'add', 2 * n)
    OPS.count(st, 'shift', 2 * n)
    p = co * a + si * b
    t = np.where(k >= 0, p >> np.maximum(k, 0), s32(p << np.maximum(-k, 0), st))
    return s16(s32(o * w + sign * t, st) + (1 << 14) >> 15, st)


def _ovl(co, c0, si, c1, k):
    """(co*c0 - si*c1) rounded from Q(29+k) to Q14: the new overlap."""
    st = 'imdct'
    n = _size(co)
    OPS.count(st, 'mul', 2 * n)
    OPS.count(st, 'add', n)
    OPS.count(st, 'shift', n)
    sh = 15 + k
    return s16((co * c0 - si * c1 + (np.int64(1) << (sh - 1))) >> sh, st)


def imdct36_i(x, k, ovl, wi):
    """x3s_imdct36 for a group of subbands: x (nb, 18) int16 at Q(14+k),
    k (nb,) their block scales, ovl (nb, 9) the Q14 overlap; wi the window
    (0 normal, 1 stop).  Returns (18 outputs at Q14, the new Q14 overlap)."""
    st = 'imdct'
    co = [None] * 9
    si = [None] * 9
    co[0] = ineg(x[:, 0], st)
    si[0] = x[:, 17]
    for i in range(4):
        si[8 - 2 * i] = isub(x[:, 4 * i + 1], x[:, 4 * i + 2], st)
        co[1 + 2 * i] = iadd(x[:, 4 * i + 1], x[:, 4 * i + 2], st)
        si[7 - 2 * i] = isub(x[:, 4 * i + 4], x[:, 4 * i + 3], st)
        co[2 + 2 * i] = ineg(iadd(x[:, 4 * i + 3], x[:, 4 * i + 4], st), st)
    co = dct3_9_i(co)
    si = dct3_9_i(si)
    for j in (1, 3, 5, 7):
        si[j] = ineg(si[j], st)
    W = Q.IMW36[wi]
    out = np.empty_like(x)
    novl = np.empty_like(ovl)
    for i in range(9):
        o = ovl[:, i]
        out[:, i] = _merge(o, W['w'][i], co[i], W['a'][i], si[i], W['b'][i], k, -1)
        out[:, 17 - i] = _merge(o, W['w9'][i], co[i], W['c'][i], si[i], W['d'][i], k, 1)
        novl[:, i] = _ovl(co[i], Q.TW9[0][i], si[i], Q.TW9[1][i], k)
    OPS.count(st, 'read', 63 * x.shape[0])
    return out, novl


def idct3_i(x0, x1, x2):
    st = 'imdct'
    m1 = imul(x1, Q.DCT9['s3'], st)
    a1 = isub(x0, ihalf(x2), st)
    return [iadd(a1, m1, st), iadd(x0, x2, st), isub(a1, m1, st)]


def imdct12_i(x, w, k, ovl3):
    """Window w (0..2) of a short block: its six lines are x[:, w + 3j].
    ovl3: three Q14 arrays (the overlap inside the short block).  Returns
    (six Q14 outputs, the new three)."""
    st = 'imdct'
    g = [x[:, w + 3 * j] for j in range(6)]
    co = idct3_i(ineg(g[0], st), iadd(g[2], g[1], st), iadd(g[4], g[3], st))
    si = idct3_i(g[5], isub(g[4], g[3], st), isub(g[2], g[1], st))
    si[1] = ineg(si[1], st)
    W = Q.IMW12
    dst = [None] * 6
    nov = [None] * 3
    for i in range(3):
        o = ovl3[i]
        dst[i] = _merge(o, W['w'][i], co[i], W['a'][i], si[i], W['b'][i], k, -1)
        dst[5 - i] = _merge(o, W['w9'][i], co[i], W['c'][i], si[i], W['d'][i], k, 1)
        nov[i] = _ovl(co[i], Q.TW3[0][i], si[i], Q.TW3[1][i], k)
    return dst, nov


def imdct_short_i(x, k, ovl):
    """minimp3's L3_imdct_short: the first six outputs are the overlap's
    first six words, then the three windows' 12-point transforms chained
    through the overlap's last three; the third window's six outputs become
    the new overlap's first six."""
    out = np.empty_like(x)
    novl = np.empty_like(ovl)
    out[:, 0:6] = ovl[:, 0:6]
    o3 = [ovl[:, 6], ovl[:, 7], ovl[:, 8]]
    d, o3 = imdct12_i(x, 0, k, o3)
    for j in range(6):
        out[:, 6 + j] = d[j]
    d, o3 = imdct12_i(x, 1, k, o3)
    for j in range(6):
        out[:, 12 + j] = d[j]
    d, o3 = imdct12_i(x, 2, k, o3)
    for j in range(6):
        novl[:, j] = d[j]
    for j in range(3):
        novl[:, 6 + j] = o3[j]
    OPS.count('imdct', 'read', 3 * 19 * x.shape[0])
    return out, novl


# ---- synthesis: the DCT-II and the window ------------------------------------

def dct2_i(x):
    """x3s_dct: N-point DCT-II (N = 32, 16 or 8) of a list of N arrays (one
    per subband, each a vector over the granule's 18 slots).  Recursive:
    X[2k] = DCT-II(N/2) of u = x[n] + x[N-1-n], X[2k+1] = DCT-IV(N/2) of
    v = x[n] - x[N-1-n]; the DCT-IV by pre-rotation, an N/4-point complex
    FFT and post-rotation, so every multiplier is a cosine or sine <= 1."""
    st = 'dct'
    n = len(x)
    if n == 1:
        return [x[0]]
    h = n // 2
    u = [iadd(x[i], x[n - 1 - i], st) for i in range(h)]
    v = [isub(x[i], x[n - 1 - i], st) for i in range(h)]
    e = dct2_i(u)
    o = dct4_i(v)
    out = [None] * n
    for k in range(h):
        out[2 * k] = e[k]
        out[2 * k + 1] = o[k]
    return out


def dct4_i(v):
    st = 'dct'
    m = len(v)
    if m == 1:
        return [imul(v[0], Q.COS_PI4, st)]
    l = m // 2
    pre = Q.DCT4_PRE[m]
    zr, zi = [], []
    for k in range(l):
        a, b = v[2 * k], v[m - 1 - 2 * k]
        c, s = pre[k]
        zr.append(imul2(a, c, b, s, st))
        zi.append(imul2(b, c, a, -s, st))
    fr, fi = fft_i(zr, zi)
    post = Q.DCT4_POST[m]
    out = [None] * m
    for k in range(l):
        if k == 0:
            yr, yi = fr[0], fi[0]
        else:
            c, s = post[k]
            yr = imul2(fr[k], c, fi[k], s, st)
            yi = imul2(fi[k], c, fr[k], -s, st)
        out[2 * k] = yr
        out[m - 1 - 2 * k] = ineg(yi, st)
    return out


def fft_i(re, im):
    """Radix-2 decimation-in-time complex FFT (forward, e^-i), of a power-of
    -two length; W^0 and W^(n/4) = -i are exact, every other twiddle is a
    rotation in Q15.  Butterfly sums saturate."""
    st = 'dct'
    n = len(re)
    if n == 1:
        return re, im
    er, ei = fft_i(re[0::2], im[0::2])
    orr, oi = fft_i(re[1::2], im[1::2])
    outr = [None] * n
    outi = [None] * n
    tw = Q.FFT_TW[n]
    for k in range(n // 2):
        if k == 0:
            tr, ti = orr[k], oi[k]
        elif 4 * k == n:
            tr, ti = oi[k], ineg(orr[k], st)       # times -i
        else:
            c, s = tw[k]                            # W = c - i s
            tr = imul2(orr[k], c, oi[k], s, st)
            ti = imul2(oi[k], c, orr[k], -s, st)
        outr[k] = iadd(er[k], tr, st)
        outi[k] = iadd(ei[k], ti, st)
        outr[k + n // 2] = isub(er[k], tr, st)
        outi[k + n // 2] = isub(ei[k], ti, st)
    return outr, outi


class IntSynth:
    """x3s_synth for one output channel: the ring of the last 16 X vectors
    (N = 32/div words each, Q14) and the window: output m of a slot is row
    j = div*m of x3tables' SYNW, eight (even, odd) coefficient pairs read
    against X[a_j / div] of the slots aged 0, 2, .. 14 and X[b_j / div] of
    those aged 1, 3, .. 15, summed in 32 bits (it cannot overflow: x3tables
    proves the row sums), then (acc + 2^12) >> 13 to Q15 PCM, clipped to
    int16 and counted."""

    def __init__(self, div):
        self.div = div
        self.n = 32 // div
        self.ring = np.zeros((16, self.n), dtype=np.int64)
        self.pos = 0
        nt = 16
        self.age = np.zeros((self.n, nt), dtype=np.int64)
        self.idx = np.zeros((self.n, nt), dtype=np.int64)
        self.co = np.zeros((self.n, nt), dtype=np.int64)
        for m in range(self.n):
            j = div * m
            a, b = Q.SYNIDX[j]
            for i in range(8):
                ce, cd = Q.SYNW[j][i]
                self.age[m, 2 * i], self.idx[m, 2 * i], self.co[m, 2 * i] = 2 * i, a // div, ce
                self.age[m, 2 * i + 1], self.idx[m, 2 * i + 1], self.co[m, 2 * i + 1] = \
                    2 * i + 1, b // div, cd
        self.nprod = int(np.count_nonzero(self.co))

    def granule(self, sb):
        """sb: list of N arrays (18 slots each) -> int16 PCM (18 * N)."""
        X = dct2_i(sb)
        out = np.empty(18 * self.n, dtype=np.int64)
        for t in range(18):
            self.pos = (self.pos + 1) & 15
            self.ring[self.pos] = [X[k][t] for k in range(self.n)]
            rows = (self.pos - self.age) & 15
            acc = (self.co * self.ring[rows, self.idx]).sum(axis=1)
            out[t * self.n:(t + 1) * self.n] = (acc + 4096) >> 13
        OPS.count('window', 'mul', 18 * self.nprod)
        OPS.count('window', 'add', 18 * self.nprod)
        OPS.count('window', 'read', 36 * self.nprod)
        OPS.count('window', 'shift', 18 * self.n)
        c = np.clip(out, -32768, 32767)
        OPS.clip += int(np.count_nonzero(c != out))
        return c.astype(np.int16)


# =============================================================================
# THE DECODE DRIVER (x3s_granule): one flow, two arithmetics
# =============================================================================

class Decoder:
    """One stream's decode state.  model: 'int' (the STANDARD integer model,
    bit-exact for the port) or 'float' (diagnosis).  div: 1, 2, 4.
    mono: downmix a stereo source to one channel after the hybrid
    filterbank, before synthesis (one synthesis pass)."""

    def __init__(self, stream, model='int', div=1, mono=False):
        self.s = stream
        self.model = model
        self.div = div
        self.nsb = 32 // div
        self.out_nch = 1 if (mono or stream.h0.nch == 1) else 2
        self.prev_bt = [0, 0]
        self.lookups = 0
        self.symbols = 0
        if model == 'float':
            self.ovl = [np.zeros((32, 9)) for _ in range(2)]
            self.syn = [FloatSynth(div) for _ in range(self.out_nch)]
            self.fclip = 0
        else:
            self.ovl = [np.zeros((32, 9), dtype=np.int64) for _ in range(2)]
            self.syn = [IntSynth(div) for _ in range(self.out_nch)]

    def granule_spectra(self, h, side, gr, r, prev_scf):
        """Part 2 and 3 for every channel of granule gr: [(gi, scf, ix, ok)]."""
        out = []
        for ch in range(h.nch):
            gi = side.gr[gr][ch]
            start = r.pos
            end = start + gi.part23
            if h.version == 25 and h.sr_idx == 2 and gi.block_type == 2 and gi.mixed:
                out.append((gi, Scf(), [0] * 576, False))   # undefined by ISO
                r.pos = end
                continue
            if h.mpeg1:
                scf = read_scf_mpeg1(r, gi, side.scfsi[ch], gr, prev_scf[ch])
            else:
                scf = read_scf_mpeg2(r, gi, h.intensity and ch == 1)
            prev_scf[ch] = scf
            if r.pos > end:
                ix, ok = [0] * 576, False
            else:
                ix, ok, lk = huffman(r, gi, end, h, huff_tables())
                self.lookups += lk
            r.pos = end
            r.over = False
            out.append((gi, scf, ix, ok))
        return out

    def decode_frame(self, h, side, md, verdict):
        """-> int16 array (samples, out_nch) for the frame's granules."""
        pcm = []
        r = BitReader(md) if verdict == F_OK else None
        prev = [None, None]
        for gr in range(h.granules):
            sp = None
            if verdict == F_OK:
                sp = self.granule_spectra(h, side, gr, r, prev)
                if not all(ok for _, _, _, ok in sp):
                    self.s.counts[F_BADGR] += 1
            pcm.append(self.granule(h, sp))
        return np.concatenate(pcm, axis=0)

    def granule(self, h, sp):
        """x3s_granule.  sp None: a silent granule (dropped frame, short
        reservoir): zero spectra through the filterbank, history kept."""
        nch = h.nch
        nsb = self.nsb
        flt = self.model == 'float'
        xs, gis = [], []
        for ch in range(nch):
            if sp is None or not sp[ch][3]:
                gis.append(None)
                xs.append(np.zeros(576) if flt else np.zeros(576, dtype=np.int64))
                continue
            gi, scf, ix, _ = sp[ch]
            gis.append(gi)
            q = quarter_exponents(h, gi, scf, h.ms and nch == 2)
            ixa = np.array(ix, dtype=np.int64)
            if flt:
                xs.append(np.sign(ixa) * np.abs(ixa) ** (4.0 / 3.0) * 2.0 ** (q / 4.0))
            else:
                xs.append(requant_i(ixa, q))
        if nch == 2 and h.mode == 1 and gis[0] is not None and gis[1] is not None:
            plan = stereo_plan(h, gis[1], sp[1][1], sp[1][2])
            if flt:
                xs = list(stereo_f(h, gis[1], plan, xs[0], xs[1]))
            else:
                xs = list(stereo_i(h, gis[1], plan, xs[0], xs[1]))
        sbs = []
        for ch in range(nch):
            gi = gis[ch]
            if gi is None:      # a silent granule continues the window shape
                gi = _silent_gi(self.prev_bt[ch])
            self.prev_bt[ch] = gi.block_type
            sbs.append(self.hybrid(h, gi, xs[ch], ch))
        return self.synth(sbs)

    def hybrid(self, h, gi, x, ch):
        """Reorder, cut at the kept subbands, antialias, IMDCT with overlap,
        frequency inversion: -> (18, nsb) subband samples."""
        nsb = self.nsb
        flt = self.model == 'float'
        if gi.block_type == 2 and getattr(gi, 'big_values', None) is not None:
            x = x[reorder_index(h, gi)]
        x = x.copy()
        x[18 * nsb:] = 0
        if gi.block_type == 2:
            nlong = 2 if gi.mixed else 0
            aa = nlong - 1
        else:
            aa = 31
        aa = min(aa, nsb - 1)
        if flt:
            x = alias_f(x, aa)
        else:
            x = alias_i(x, aa)
        xb = x.reshape(32, 18)[:nsb].copy()
        if not flt:
            kb, xb16 = band_scale(xb)
        ovl = self.ovl[ch]
        kinds = [imdct_kind(gi, b) for b in range(nsb)]
        out = np.empty_like(xb)
        for kind in set(kinds):
            sel = np.array([k == kind for k in kinds])
            if flt:
                for b in np.nonzero(sel)[0]:
                    seg, ov = list(xb[b]), list(ovl[b])
                    if kind < 0:
                        imdct_short_f(seg, ov)
                    else:
                        imdct36_f(seg, ov, WIN_F[kind])
                    out[b], ovl[b] = seg, ov
            else:
                xs, ks = xb16[sel], kb[sel]
                if kind < 0:
                    o, nv = imdct_short_i(xs, ks, ovl[:nsb][sel])
                else:
                    o, nv = imdct36_i(xs, ks, ovl[:nsb][sel], kind)
                out[sel] = o
                idx = np.nonzero(sel)[0]
                ovl[idx] = nv
        out[1::2, 1::2] = -out[1::2, 1::2] if flt else ineg(out[1::2, 1::2], 'imdct')
        return out.T      # (18, nsb)

    def synth(self, sbs):
        flt = self.model == 'float'
        if self.out_nch == 1 and len(sbs) == 2:
            if flt:
                sbs = [(sbs[0] + sbs[1]) * 0.5]
            else:
                OPS.count('mix', 'add', 2 * sbs[0].size)
                sbs = [(sbs[0] + sbs[1] + 1) >> 1]
        elif self.out_nch == 2 and len(sbs) == 1:
            sbs = [sbs[0], sbs[0]]
        n = self.nsb
        cols = []
        for ch in range(self.out_nch):
            if flt:
                syn = self.syn[ch]
                o = np.zeros(18 * n)
                for t in range(18):
                    o[t * n:(t + 1) * n] = syn.slot(sbs[ch][t])
                v = np.round(o * 32768.0)
                self.fclip += int(np.sum(np.abs(v) > 32767))
                cols.append(np.clip(v, -32768, 32767).astype(np.int16))
            else:
                cols.append(self.syn[ch].granule([sbs[ch][:, k] for k in range(n)]))
        return np.stack(cols, axis=1)


def _silent_gi(prev_bt):
    """The block a silent granule is transformed as: a stop block after a
    start or short block (whose tail is the short window's), else normal."""
    g = Granule()
    g.block_type = 3 if prev_bt in (1, 2) else 0
    g.mixed = 0
    g.win_switch = 1 if g.block_type else 0
    g.big_values = None
    return g


def stereo_f(h, gi_r, plan, L, R):
    L2, R2 = L.copy(), R.copy()
    for l, p in enumerate(plan):
        if p == 'ms':
            L2[l], R2[l] = L[l] + R[l], L[l] - R[l]
        elif p != 'lr':
            kl, kr = is_factors(h, gi_r, p[1])
            L2[l], R2[l] = L[l] * kl, L[l] * kr
    return L2, R2


def alias_f(x, nb):
    x = x.copy()
    cs, ca = TB.aa_float()
    for b in range(1, nb + 1):
        for i in range(8):
            u, d = x[18 * b + i], x[18 * b - 1 - i]
            x[18 * b + i] = u * cs[i] - d * ca[i]
            x[18 * b - 1 - i] = d * cs[i] + u * ca[i]
    return x


def decode_file(path_or_bytes, model='int', div=1, mono=False, trim=False):
    """Decode a whole file: (pcm int16 (n, ch), rate, Stream, Decoder) or
    raises Refused."""
    data = path_or_bytes if isinstance(path_or_bytes, (bytes, bytearray)) else \
        open(path_or_bytes, 'rb').read()
    st = Stream(data)
    if st.refused:
        raise Refused(st.refused)
    dec = Decoder(st, model, div, mono)
    chunks = []
    for h, side, md, verdict in st.frames():
        if verdict == F_TRUNC:
            break
        chunks.append(dec.decode_frame(h, side, md, verdict))
    pcm = np.concatenate(chunks, axis=0) if chunks else np.zeros((0, dec.out_nch), np.int16)
    if trim:
        d, p = trim_counts(st.info)
        d //= div
        p //= div
        pcm = pcm[d:len(pcm) - p if p else len(pcm)]
    return pcm, st.h0.rate // div, st, dec


X3_DECODER_DELAY = 529     # the hybrid filterbank's own delay, in samples


def trim_counts(info):
    """The gapless trim (SPEC.md 121.7), mpglib's rule exactly: the decoder's
    529-sample delay always comes off the head; a LAME tag adds its encoder
    delay to the head and takes (padding - 529) off the tail.  Returns
    (head, tail) in source-rate samples."""
    if 'padding' in info:
        return (info.get('delay', 0) + X3_DECODER_DELAY,
                max(0, info['padding'] - X3_DECODER_DELAY))
    return info.get('delay', 0) + X3_DECODER_DELAY, 0


class Refused(Exception):
    pass


# =============================================================================
# STUDIES: the Huffman first level, the operation counts
# =============================================================================

def huff_study(paths):
    """For first-level widths 4, 6, 8 and subtable widths 4 and 8: the
    table bytes and, over the fixtures, the lookups per symbol (pairs and
    quadruples) from the real symbol mix."""
    global _HUFTAB
    rows = []
    for first in (4, 6, 8):
        for sub in (4, 8):
            tabs = {t: (TB.build_huff(t, first, sub), TB.first_width(t, first))
                    for t in TB.PAIR_TREES + TB.COUNT1_TREES}
            size = sum(2 * len(v[0]) for v in tabs.values())
            look = sym = 0
            for p in paths:
                st = Stream(open(p, 'rb').read())
                if st.refused:
                    continue
                for h, side, md, v in st.frames():
                    if v != F_OK:
                        continue
                    r = BitReader(md)
                    prev = [None, None]
                    for gr in range(h.granules):
                        for ch in range(h.nch):
                            gi = side.gr[gr][ch]
                            end = r.pos + gi.part23
                            if h.mpeg1:
                                prev[ch] = read_scf_mpeg1(r, gi, side.scfsi[ch], gr, prev[ch])
                            else:
                                prev[ch] = read_scf_mpeg2(r, gi, h.intensity and ch == 1)
                            if r.pos <= end:
                                _, ok, lk = huffman(r, gi, end, h, tabs, count=True)
                                look += lk[0]
                                sym += lk[1]
                            r.pos = end
                            r.over = False
            rows.append((first, sub, size, look / max(sym, 1), sym))
    return rows


# The 8088 cost model of NUMERICS.md: cycles per counted operation, each
# with its instruction bytes at 4 cycles a byte (the fetch floor), the
# execution time from the 8088's tables, whichever is larger, plus operand
# fetch from memory where the operation must load one.
CYCLES = {
    'mul': 150,      # imul r16 (128..154) with one operand from memory
    'mul32': 330,    # int32 x int16: mul + imul, three adds, sign fix-up
    'add': 16,       # add r,r + jo (not taken): 4 bytes, fetch-bound
    'add32': 28,     # add + adc + jo: 6..7 bytes
    'shift': 40,     # add 2^14 / adc 0 / shl ax,1 / rcl dx,1 (a rescale)
    'read': 18,      # mov r16, [bx+si+disp]: 8 + EA 7..9 + 4 (word on an 8-bit bus)
}


def op_counts(path, div=1, mono=False):
    """Per-stage operation counts of one decode, per granule and per
    channel decoded through the hybrid filterbank (and per output channel
    for the synthesis stages).  Returns ({stage: {kind: n}}, granules, nch)."""
    global OPS
    save = OPS
    OPS = Ops(counting=True)
    try:
        pcm, rate, st, dec = decode_file(path, 'int', div, mono)
        n = OPS.n
    finally:
        OPS = save
    gr = len(pcm) * div // 576
    return n, gr, st.h0.nch, dec.out_nch


# =============================================================================
# THE COMPARISON HARNESS
# =============================================================================

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXDIR = os.path.join(ROOT, 'tests', 'xtmp3')
GOLDEN = os.path.join(FIXDIR, 'X3SIM.txt')
LAME = os.environ.get('LAME') or '/opt/homebrew/bin/lame'

# THE TOLERANCE (NUMERICS.md 4).  Against `lame --decode` (mpglib), at the
# source rate, for every fixture the comparison applies to: the error's RMS
# and peak in 16-bit LSB, and on the music fixtures the SNR.  At divisors 2
# and 4 the arithmetic is held to the same RMS and peak against the float
# reference at the same divisor (the reduced synthesis is exact in float,
# so that difference is the integer arithmetic alone); the alias leakage
# against an ideal lowpass-and-decimate of mpglib's output is reported.
TOL_RMS = 6.0
TOL_PEAK = 48
TOL_MUSIC_SNR = 60.0
MUSIC = ('NEONDRV.MP3', 'NIGHTSIG.MP3', 'AFTERHRS.MP3', 'C128.MP3', 'C320.MP3',
         'VBR2.MP3', 'MSF.MP3', 'DUAL44.MP3', 'BASS.MP3', 'C48.MP3', 'M22.MP3',
         'M24.MP3', 'X11K24.MP3', 'IS44.MP3', 'ISMS44.MP3', 'RSV32.MP3', 'MONO44.MP3')


def read_wav(path):
    with wave.open(path) as w:
        n = w.getnchannels()
        return np.frombuffer(w.readframes(w.getnframes()), '<i2').reshape(-1, n), w.getframerate()


def write_wav(path, pcm, rate):
    pcm = np.asarray(pcm, dtype='<i2')
    with wave.open(path, 'wb') as w:
        w.setnchannels(pcm.shape[1] if pcm.ndim > 1 else 1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def lame_decode(path, cache):
    """mpglib's output for a fixture (cached by the fixture's hash)."""
    import hashlib
    h = hashlib.sha256(open(path, 'rb').read()).hexdigest()[:16]
    out = os.path.join(cache, '%s-%s.wav' % (os.path.basename(path), h))
    if not os.path.exists(out):
        tmp = os.path.join(cache, 'in.mp3')
        with open(path, 'rb') as f, open(tmp, 'wb') as g:
            g.write(f.read())
        subprocess.run([LAME, '--decode', '-S', tmp, out], check=True,
                       capture_output=True)
    return read_wav(out)


def err_stats(a, ref):
    a = a.astype(np.float64)
    ref = ref.astype(np.float64)
    n = min(len(a), len(ref))
    e = a[:n] - ref[:n]
    sig = float(np.sum(ref[:n] ** 2))
    noise = float(np.sum(e * e))
    snr = 10 * math.log10(sig / noise) if noise > 0 and sig > 0 else float('inf')
    rms = math.sqrt(noise / max(e.size, 1))
    peak = int(np.max(np.abs(e))) if e.size else 0
    dc = float(np.mean(e)) if e.size else 0.0
    return dict(snr=snr, rms=rms, peak=peak, dc=dc, n=n)


def ideal_decimate(ref, head, div, nout):
    """mpglib's full-rate output, lowpassed at the reduced Nyquist and
    sampled where the reduced decoder's samples fall: our sample m is the
    full-rate raw sample div*m, which is mpglib's sample div*m - head."""
    taps = 255
    n = np.arange(taps) - (taps - 1) / 2
    fc = 0.5 / div
    h = 2 * fc * np.sinc(2 * fc * n) * np.kaiser(taps, 8.0)
    h /= h.sum()
    out = np.zeros((nout, ref.shape[1]))
    for c in range(ref.shape[1]):
        y = np.convolve(ref[:, c].astype(np.float64), h)[(taps - 1) // 2:]
        idx = np.arange(nout) * div - head
        ok = (idx >= 0) & (idx < len(ref))
        out[ok, c] = y[idx[ok]]
    return out


def parse_expect(e):
    """'decode title=A B ...' -> (kind, {key: value}).  Keys: title, artist,
    same, frames, iso, and COUNT>=N for recover."""
    words = e.split()
    kind = words[0] if words else 'decode'
    if kind == 'refuse':
        return kind, {'name': ' '.join(words[1:])}
    opts = {}
    key = None
    for w in words[1:]:
        if '>=' in w:
            k, v = w.split('>=')
            opts.setdefault('counts', {})[k] = int(v)
            key = None
        elif '=' in w:
            key, v = w.split('=', 1)
            opts[key] = v
        elif w == 'iso':
            opts['iso'] = True
            key = None
        elif key:
            opts[key] += ' ' + w
    return kind, opts


def fixture_list():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import x3fixtures
    return x3fixtures.read_list()


def compare(only=(), divs=(1, 2, 4), wavdir=None, cache=None, verbose=True):
    """`--compare`: every fixture against its expectation.  Returns the
    number of failures."""
    import tempfile
    cache = cache or os.path.join(tempfile.gettempdir(), 'x3sim-lame')
    os.makedirs(cache, exist_ok=True)
    fails = []
    rows = []
    raw_of = {}
    for rec in fixture_list():
        name = rec['name']
        if only and name not in only:
            continue
        path = os.path.join(FIXDIR, name)
        kind, opt = parse_expect(rec.get('expect', 'decode'))
        data = open(path, 'rb').read()
        if kind == 'refuse':
            st = Stream(data)
            ok = st.refused == opt['name']
            rows.append('%-13s refused %-14s %s' % (name, st.refused, 'ok' if ok else 'FAIL'))
            if not ok:
                fails.append('%s: expected refusal %r, got %r' % (name, opt['name'], st.refused))
            continue
        try:
            pcm, rate, st, dec = decode_file(data, 'int', 1)
        except Exception as ex:           # noqa: BLE001 - the harness reports it
            fails.append('%s: exception %r' % (name, ex))
            rows.append('%-13s EXCEPTION %r' % (name, ex))
            continue
        raw_of[name] = pcm
        notes = []
        for k, n in opt.get('counts', {}).items():
            if st.counts.get(k, 0) < n:
                fails.append('%s: %s %d < %d' % (name, k, st.counts.get(k, 0), n))
        for key in ('title', 'artist'):
            if key in opt and st.info.get(key) != opt[key]:
                fails.append('%s: %s %r != %r' % (name, key, st.info.get(key), opt[key]))
        if 'frames' in opt and st.info.get('frames') != int(opt['frames']):
            fails.append('%s: frames %r != %s' % (name, st.info.get('frames'), opt['frames']))
        if 'same' in opt:
            other = raw_of.get(opt['same'])
            if other is None:
                other = decode_file(os.path.join(FIXDIR, opt['same']), 'int', 1)[0]
            same = len(other) == len(pcm) and np.array_equal(other, pcm)
            notes.append('same as %s: %s' % (opt['same'], 'yes' if same else 'NO'))
            if not same:
                fails.append('%s: decodes differently from %s' % (name, opt['same']))
        counts = ' '.join('%s %d' % (k, v) for k, v in st.counts.items()
                          if v and k not in ('frames', 'maxreach'))
        if kind == 'recover':
            rows.append('%-13s recovered: %s %s' % (name, counts or '-', '; '.join(notes)))
            continue
        # decode: the numerics
        head, tail = trim_counts(st.info)
        if opt.get('iso'):
            ref = decode_file(data, 'float', 1, trim=True)[0]
            lame_ref = lame_decode(path, cache)[0]
        else:
            ref = lame_decode(path, cache)[0]
            lame_ref = None
        for div in divs:
            OPS.__init__()
            pcm_d, rate_d, st_d, dec_d = decode_file(data, 'int', div)
            sat = sum(OPS.sat.values())
            clip = OPS.clip
            if wavdir:
                os.makedirs(wavdir, exist_ok=True)
                write_wav(os.path.join(wavdir, '%s-d%d.wav' % (name[:-4], div)),
                          trim_pcm(pcm_d, st.info, div), rate_d)
            if div == 1:
                s = err_stats(trim_pcm(pcm_d, st.info, 1), ref)
                refclip = int(np.count_nonzero(np.abs(ref.astype(np.int32)) >= 32767))
                line = '%-13s d1  vs %-6s snr %6.1f  rms %5.2f  peak %4d  dc %+6.3f  clip %d/%d  sat %d' % (
                    name, 'float' if opt.get('iso') else 'mpglib', s['snr'], s['rms'], s['peak'],
                    s['dc'], clip, refclip, sat)
                if lame_ref is not None:
                    s2 = err_stats(trim_pcm(pcm_d, st.info, 1), lame_ref)
                    line += '  (mpglib %.1f dB: it treats MPEG-2 is_pos 7 as illegal)' % s2['snr']
                rows.append(line)
                if s['rms'] > TOL_RMS or s['peak'] > TOL_PEAK:
                    fails.append('%s d1: rms %.2f peak %d past %.1f/%d' % (
                        name, s['rms'], s['peak'], TOL_RMS, TOL_PEAK))
                if name in MUSIC and s['snr'] < TOL_MUSIC_SNR:
                    fails.append('%s d1: music SNR %.1f < %.1f' % (name, s['snr'], TOL_MUSIC_SNR))
            else:
                fl = decode_file(data, 'float', div)[0]
                a = err_stats(pcm_d, fl)
                full_ref = decode_file(data, 'float', 1)[0] if opt.get('iso') else None
                if full_ref is not None:
                    ideal = ideal_decimate(full_ref.astype(np.float64), 0, div, len(pcm_d))
                else:
                    ideal = ideal_decimate(ref.astype(np.float64), head, div, len(pcm_d))
                lo = (head // div) + 64
                hi = len(pcm_d) - (tail // div) - 64
                lk = err_stats(fl[lo:hi], ideal[lo:hi]) if hi > lo else dict(snr=float('inf'))
                rows.append('%-13s d%d  vs float snr %6.1f  rms %5.2f  peak %4d  dc %+6.3f  clip %d  sat %d | leak vs ideal %6.1f dB' % (
                    name, div, a['snr'], a['rms'], a['peak'], a['dc'], clip, sat, lk['snr']))
                if a['rms'] > TOL_RMS or a['peak'] > TOL_PEAK:
                    fails.append('%s d%d: rms %.2f peak %d past %.1f/%d' % (
                        name, div, a['rms'], a['peak'], TOL_RMS, TOL_PEAK))
        if verbose:
            pass
    if verbose:
        print('\n'.join(rows))
        print('tolerance: d1 vs mpglib rms <= %.1f LSB, peak <= %d LSB, music SNR >= %.0f dB; '
              'd2/d4 vs float at the same divisor, rms <= %.1f, peak <= %d' % (
                  TOL_RMS, TOL_PEAK, TOL_MUSIC_SNR, TOL_RMS, TOL_PEAK))
        for f in fails:
            print('FAIL ' + f)
        print('x3sim --compare: %d failure(s)' % len(fails))
    return len(fails)


def trim_pcm(pcm, info, div):
    d, p = trim_counts(info)
    d //= div
    p //= div
    return pcm[d:len(pcm) - p if p else len(pcm)]


# =============================================================================
# THE GOLDEN OUTPUTS: what x3std.asm must reproduce byte for byte
# =============================================================================

def golden_cases():
    """(fixture, div, mono) for every decodable fixture at 1, 2 and 4, and
    the mono downmix of every stereo one at 1 and 4."""
    out = []
    for rec in fixture_list():
        kind, _ = parse_expect(rec.get('expect', 'decode'))
        if kind == 'refuse':
            continue
        st = Stream(open(os.path.join(FIXDIR, rec['name']), 'rb').read())
        for div in (1, 2, 4):
            out.append((rec['name'], div, False))
        if st.h0.nch == 2:
            out.append((rec['name'], 1, True))
            out.append((rec['name'], 4, True))
    return out


def golden_line(name, div, mono):
    import hashlib
    global OPS
    OPS = Ops()
    pcm, rate, st, dec = decode_file(os.path.join(FIXDIR, name), 'int', div, mono)
    h = hashlib.sha256(np.ascontiguousarray(pcm, dtype='<i2').tobytes()).hexdigest()
    return '%-13s d%d %-6s %7d x%d %s clip %d sat %d' % (
        name, div, 'mono' if mono else 'native', len(pcm), pcm.shape[1], h, OPS.clip,
        sum(OPS.sat.values()))


def write_golden():
    lines = ['# XTMP3: the STANDARD integer model\'s output for every fixture (SPEC.md 121.4).',
             '# Made by `tools/x3sim.py --golden`; x3std.asm is BIT-EXACT when its PCM',
             '# (int16 little-endian, interleaved, untrimmed, from the first audio frame)',
             '# hashes to these.  fixture, divisor, channels, samples x channels, SHA-256,',
             '# output clips, saturations.  Tables: x3std.bin CRC-32 %08X.' % (
                 zlib_crc(TB.blob_and_inc()[0])),
             '']
    for c in golden_cases():
        lines.append(golden_line(*c))
    with open(GOLDEN, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print('wrote %s (%d cases)' % (GOLDEN, len(lines) - 6))


def zlib_crc(b):
    import zlib
    return zlib.crc32(b) & 0xFFFFFFFF


def read_golden():
    out = {}
    with open(GOLDEN) as f:
        for line in f:
            if line.startswith('#') or not line.strip():
                continue
            w = line.split()
            out[(w[0], int(w[1][1:]), w[2] == 'mono')] = line.rstrip('\n')
    return out


QUICK = (('X8K16.MP3', 1, False), ('X8K16.MP3', 4, False), ('M16.MP3', 2, False),
         ('IS22.MP3', 1, False), ('BBADSIDE.MP3', 1, False))


def check_quick(full=False):
    """`--check`: the fixtures' hashes, the tables' self-check, and the model
    against its golden outputs - QUICK's five cases (well under a second),
    or every case with `full`.  Returns failures."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import x3fixtures
    bad = 0
    if x3fixtures.check() != 0:
        bad += 1
    tb = TB.selfcheck()
    for m in tb:
        print('x3tables: FAIL ' + m)
    bad += len(tb)
    gold = read_golden()
    cases = list(gold.keys()) if full else QUICK
    for c in cases:
        want = gold.get(c)
        got = golden_line(*c)
        if want != got:
            print('x3sim: GOLDEN MISMATCH\n  want %s\n  got  %s' % (want, got))
            bad += 1
    print('x3sim: %s, %d golden case(s), %d failure(s)' % (
        'ok' if not bad else 'FAILED', len(cases), bad))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--decode', metavar='FILE')
    ap.add_argument('--model', default='int', choices=('int', 'float'))
    ap.add_argument('--div', type=int, default=1, choices=(1, 2, 4))
    ap.add_argument('--mono', action='store_true')
    ap.add_argument('--trim', action='store_true', help='apply the gapless trim')
    ap.add_argument('--out', metavar='WAV')
    ap.add_argument('--compare', action='store_true')
    ap.add_argument('--only', nargs='*', default=())
    ap.add_argument('--divs', default='1,2,4')
    ap.add_argument('--wav', metavar='DIR', help='with --compare: write each output')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--check-all', action='store_true')
    ap.add_argument('--golden', action='store_true', help='rewrite tests/xtmp3/X3SIM.txt')
    ap.add_argument('--huffstudy', action='store_true')
    ap.add_argument('--opcount', action='store_true')
    a = ap.parse_args()
    if a.decode:
        try:
            pcm, rate, st, dec = decode_file(a.decode, a.model, a.div, a.mono, a.trim)
        except Refused as e:
            print('refused: %s' % e)
            return 2
        print('%s: %d Hz x%d, %d samples, info %s, counts %s, clip %d, sat %s' % (
            a.decode, rate, pcm.shape[1], len(pcm), st.info,
            {k: v for k, v in st.counts.items() if v}, OPS.clip,
            {k: v for k, v in OPS.sat.items() if v}))
        if a.out:
            write_wav(a.out, pcm, rate)
        return 0
    if a.golden:
        write_golden()
        return 0
    if a.check or a.check_all:
        return 1 if check_quick(a.check_all) else 0
    if a.compare:
        if not os.path.exists(LAME):
            print('x3sim --compare needs lame (set LAME=)')
            return 2
        divs = tuple(int(x) for x in a.divs.split(','))
        return 1 if compare(a.only, divs, a.wav) else 0
    if a.huffstudy:
        import glob
        for r in huff_study(sorted(glob.glob(os.path.join(FIXDIR, '*.MP3')))):
            print('first %d sub %d: %5d bytes, %.3f lookups a symbol over %d symbols' % r)
        return 0
    if a.opcount:
        print_opcounts()
        return 0
    ap.print_help()
    return 0


def print_opcounts():
    cases = (('MONO44.MP3', 1, False), ('MONO44.MP3', 2, False), ('MONO44.MP3', 4, False),
             ('C128.MP3', 1, False), ('C128.MP3', 2, False), ('C128.MP3', 4, False),
             ('C128.MP3', 1, True), ('C128.MP3', 4, True))
    for name, div, mono in cases:
        n, gr, nch, onch = op_counts(os.path.join(FIXDIR, name), div, mono)
        tot = 0
        print('%s div %d %s: per granule (%d source channel(s) -> %d output), %d granules' % (
            name, div, 'mono' if mono else 'native', nch, onch, gr))
        for stg in STAGES:
            c = {k: v / gr for k, v in n[stg].items()}
            cyc = sum(c[k] * CYCLES[k] for k in KINDS)
            tot += cyc
            print('  %-8s mul %7.0f mul32 %6.0f add %7.0f add32 %6.0f shift %6.0f read %6.0f  ~%9.0f cycles' % (
                stg, c['mul'], c['mul32'], c['add'], c['add32'], c['shift'], c['read'], cyc))
        print('  total ~%.0f 8088 cycles a granule (Huffman and the parser not included)' % tot)


if __name__ == '__main__':
    sys.exit(main())
