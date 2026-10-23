#!/usr/bin/env python3
"""x3tables: XTMP3's STANDARD-decoder runtime tables, made on the host (SPEC.md 121.10).

    python3 tools/x3tables.py --out DIR      # x3std.bin + x3std.inc in DIR
    python3 tools/x3tables.py --selfcheck    # every table against its formula
    python3 tools/x3tables.py --huffstats    # first-level width study (4/6/8)

This file is the ONE definition of every constant the STANDARD backend
(`apps/xtmp3/x3std.asm`) multiplies by or looks up, in the exact integer form
the target uses.  `tools/x3sim.py` (the bit-exact reference model) imports
these same functions, so the model and the target cannot disagree about a
constant: if the model is right with these numbers, the port is right when it
reproduces the bytes this file writes.

The output is a little-endian (8086) blob of tables plus an asm include with
an equate for each table's offset, length and CRC-32, and a metadata header
(generator version, scales, dimensions).  `--selfcheck` asserts symmetry,
range, sign and rounding of each table against the float formula it comes
from, and that two generations give the same bytes.

SHIPPED RAW OR EXPANDED AT OPEN (SPEC.md 121.10), with the reasoning, is
written beside each table below and summarised by `shipping_report()`; the
STANDARD backend never runs on a machine where a floppy read is the
bottleneck it is for the XT packs, and its tables are small, so most ship raw.

Every rounding in this file is `rnd(x) = floor(x + 0.5)` on the exact value,
applied ONCE, and a value that would round to +32768 is clamped to 32767
(this happens only for a coefficient of exactly 1.0 in Q15; the model and the
target use the clamped value, so it is part of the definition, not an error
the port may "fix").
"""
import argparse
import math
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import x3tab_iso as ISO  # noqa: E402

GEN_VERSION = 1

# =============================================================================
# fixed-point helpers (shared with x3sim)
# =============================================================================


def rnd(x):
    """Round half up: the one rounding rule of every table."""
    return math.floor(x + 0.5)


def q15(c):
    """A coefficient |c| <= 1 in Q15, 1.0 clamped to 32767."""
    v = rnd(c * 32768.0)
    return max(-32768, min(32767, v))


def q14(c):
    v = rnd(c * 16384.0)
    assert -32768 <= v <= 32767, c
    return v


# =============================================================================
# HUFFMAN: multi-level lookup tables
# =============================================================================
#
# One table of 16-bit entries per Huffman TREE (13 pair trees plus the two
# count1 trees).  Decoding: peek FIRST bits, look up; an entry is either
#
#   LEAF     bit 15 = 0: bits 0..7 = the value ((x << 4) | y, or the count1
#            quadruple vwxy), bits 8..11 = the bits this level consumed
#            (1..FIRST or 1..its own width), bits 12..14 = 0
#   POINTER  bit 15 = 1: bits 0..10 = the subtable's first entry (an index
#            into this same table), bits 11..14 = the subtable's width w;
#            the pointer consumes the whole width of its level, and the
#            subtable is indexed by the next w bits.
#
# A subtable's width is min(SUB, the longest code remainder below that
# prefix), so no subtable is wider than it needs to be, and a tree whose
# longest code is shorter than FIRST indexes its first level with that many
# bits (first_width).  FIRST = 8 was chosen by measurement (`x3sim.py
# --huffstudy` over every fixture): 1.039 lookups a symbol in 8,856 bytes,
# against 1.140 in 5,372 at 6 and 1.311 in 4,424 at 4 - and on an 8088 an
# 8-bit first level is a byte move (the window's high byte IS the index),
# where 6 or 4 bits cost a shift by CL (8 + 4n cycles) on every symbol.

X3_HUF_FIRST = 8
X3_HUF_SUB = 4

PAIR_TREES = (1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 24)
COUNT1_TREES = (32, 33)


def tree_codes(t):
    """[(codeword string, value)] of tree t."""
    if t in ISO.HUFF:
        dim, codes = ISO.HUFF[t]
        return [(codes[x * dim + y], (x << 4) | y)
                for x in range(dim) for y in range(dim)]
    return [(ISO.COUNT1[t][v], v) for v in range(16)]


def build_huff(t, first=X3_HUF_FIRST, sub=X3_HUF_SUB):
    """The lookup table of tree t: a list of uint16 entries."""
    codes = tree_codes(t)
    table = []

    def build(prefix_codes, width):
        # prefix_codes: [(remaining bits, value)], all remainders non-empty
        base = len(table)
        table.extend([None] * (1 << width))
        groups = {}
        for rem, v in prefix_codes:
            if len(rem) <= width:
                pad = width - len(rem)
                idx0 = int(rem, 2) << pad
                for k in range(1 << pad):
                    assert table[base + idx0 + k] is None
                    table[base + idx0 + k] = (len(rem) << 8) | v
            else:
                groups.setdefault(rem[:width], []).append((rem[width:], v))
        for head, sub_codes in sorted(groups.items()):
            w = min(sub, max(len(r) for r, _ in sub_codes))
            off = build(sub_codes, w)
            assert off < 2048 and 1 <= w <= 15
            table[base + int(head, 2)] = 0x8000 | (w << 11) | off
        return base

    build(codes, min(first, max(len(c) for c, _ in codes)))
    assert all(e is not None for e in table), t   # complete prefix code
    return table


def first_width(t, first=X3_HUF_FIRST):
    """A tree's first-level width: FIRST, or less if its longest code is
    shorter (tables 1..6 and count1 B would otherwise be mostly copies)."""
    return min(first, max(len(c) for c, _ in tree_codes(t)))


def huff_decode(table, peek, first):
    """Walk one symbol.  peek(n) returns the next n bits without consuming;
    first is the tree's first_width().  Returns (value, bits consumed,
    lookups)."""
    used = 0
    w = first
    e = table[peek(w)]
    lookups = 1
    while e & 0x8000:
        used += w
        w = (e >> 11) & 15
        off = e & 0x7FF
        e = table[off + ((peek(used + w)) & ((1 << w) - 1))]
        lookups += 1
    used += (e >> 8) & 15
    return e & 0xFF, used, lookups


def huff_sizes(first, sub=X3_HUF_SUB):
    return {t: 2 * len(build_huff(t, first, sub)) for t in PAIR_TREES + COUNT1_TREES}


# =============================================================================
# REQUANTISATION: |is|^(4/3) as a 15-bit mantissa and an exponent
# =============================================================================
#
# P43_M[i], P43_E[i] for i = 0..X3_P43_N-1:  i^(4/3) = M * 2^(E - 14), with
# M in [16384, 32767] (M = 0 for i = 0).  Values i >= X3_P43_N (escapes up to
# 8206) are interpolated from the entries at i >> 3 (x3sim.pow43 is the
# definition).  The fractional powers 2^(k/4), k = 0..3, are P43_F in Q14.

X3_P43_N = 1024
P43_F = tuple(q14(2.0 ** (k / 4.0)) for k in range(4))


def pow43_entry(i):
    if i == 0:
        return 0, 0
    v = i ** (4.0 / 3.0)
    e = math.floor(math.log2(v))
    m = rnd(v * 2.0 ** (14 - e))
    if m >= 32768:          # rounding carried into the next octave
        e += 1
        m = rnd(v * 2.0 ** (14 - e))
    # exact octave test, independent of log2's last bit
    while m < 16384:
        e -= 1
        m = rnd(v * 2.0 ** (14 - e))
    assert 16384 <= m <= 32767, (i, m, e)
    return m, e


def pow43_tables():
    ms, es = [], []
    for i in range(X3_P43_N + 3):   # 8206 >> 3 = 1025, and [j + 1] is read
        m, e = pow43_entry(i)
        ms.append(m)
        es.append(e)
    return ms, es


# =============================================================================
# STEREO
# =============================================================================
#
# Intensity stereo, MPEG-1: is_pos 0..6, kl = tan/(1+tan), kr = 1/(1+tan),
# tan = tan(is_pos * pi / 12).  MPEG-2: k = 2^(-(is_pos+1)/2 * 1/4 or 1/2)
# applied to one channel.  Both in Q14, and a second set times sqrt(2) for a
# frame that is ALSO M/S (requantisation folded M/S's 1/sqrt(2) into every
# line, and an intensity band was never M/S coded, so it is given back).


def is_mpeg1():
    out = []
    for p in range(7):
        t = math.tan(p * math.pi / 12.0)
        if p == 6:
            kl, kr = 1.0, 0.0
        else:
            kl, kr = t / (1.0 + t), 1.0 / (1.0 + t)
        out.append((kl, kr))
    return out


def is_mpeg2(scale):
    """scale = intensity_scale bit; returns k for is_pos 0..31 (k applies to
    the channel named by the parity rule in x3sim.stereo)."""
    io = 2.0 ** (-0.5 if scale else -0.25)
    return [io ** ((p + 1) >> 1) for p in range(32)]


IS1_Q = [(q14(kl), q14(kr)) for kl, kr in is_mpeg1()]
IS1_QMS = [(q14(kl * math.sqrt(2)), q14(kr * math.sqrt(2))) for kl, kr in is_mpeg1()]
IS2_Q = {s: [q14(k) for k in is_mpeg2(s)] for s in (0, 1)}
IS2_QMS = {s: [q14(k * math.sqrt(2)) for k in is_mpeg2(s)] for s in (0, 1)}
SQRT2_Q14 = q14(math.sqrt(2))

# =============================================================================
# ANTIALIAS butterflies, Q15
# =============================================================================


def aa_float():
    cs = [1.0 / math.sqrt(1.0 + c * c) for c in ISO.AA_CI]
    ca = [-c / math.sqrt(1.0 + c * c) for c in ISO.AA_CI]   # positive
    return cs, ca


AA_CS = tuple(q15(c) for c in aa_float()[0])
AA_CA = tuple(q15(c) for c in aa_float()[1])

# =============================================================================
# IMDCT constants (minimp3's factorisation, every multiplier <= 1), Q15
# =============================================================================
#
# The 36-point IMDCT is two 9-point DCT-IIIs and a twiddle-and-window stage;
# the 12-point one is two 3-point ones (x3sim.imdct36_i / imdct12_i).

DCT9_F = {
    'c1': math.cos(math.pi * 2 / 9),     # 0.76604444
    'c2': math.cos(math.pi * 1 / 9),     # 0.93969262
    'c3': math.cos(math.pi * 4 / 9),     # 0.17364818
    'c4': math.cos(math.pi * 1 / 18),    # 0.98480775
    'c5': math.cos(math.pi * 7 / 18),    # 0.34202014
    'c6': math.cos(math.pi * 5 / 18),    # 0.64278761
    's3': math.cos(math.pi / 6),         # 0.86602540
}
DCT9_C = DCT9_F


def mdct_window_float():
    """The long-block window words, minimp3's g_mdct_window: [0] for block
    types 0 and 1 (and a mixed block's two long subbands), [1] for a stop
    block.  Each is 18 words: nine applied to the overlap and nine to the
    new half (x3sim.imdct36_i)."""
    w = [math.sin(math.pi / 36.0 * (i + 0.5)) for i in range(18)]
    w0 = [w[17 - i] for i in range(9)] + [w[i] for i in range(9)]
    ws = [math.sin(math.pi / 12.0 * (i + 0.5)) for i in range(6)]
    w1 = [1.0] * 6 + [ws[5], ws[4], ws[3]] + [0.0] * 6 + [ws[0], ws[1], ws[2]]
    return w0, w1


def twiddles_float():
    tw9 = ([math.sin(math.pi * (19 + 2 * i) / 72.0) for i in range(9)],
           [math.cos(math.pi * (19 + 2 * i) / 72.0) for i in range(9)])
    tw3 = ([math.sin(math.pi * (7 + 2 * i) / 24.0) for i in range(3)],
           [math.cos(math.pi * (7 + 2 * i) / 24.0) for i in range(3)])
    return tw9, tw3




# =============================================================================
# SYNTHESIS constants
# =============================================================================
#
# The N-point DCT-II (N = 32, 16, 8) is recursive: X[2k] from a DCT-II of
# half the length, X[2k+1] from a DCT-IV of half the length computed as a
# pre-rotation, a complex FFT of a quarter of the length and a
# post-rotation (x3sim.dct2_i).  The rotations are (cos, sin) pairs in Q15:
#   DCT4_PRE[m][k]  = angle pi (4k+1) / (4m),  k < m/2
#   DCT4_POST[m][k] = angle pi k / m,          0 < k < m/2  (k = 0 is exact)
#   FFT_TW[n][k]    = angle 2 pi k / n,        0 < k < n/2, k != n/4
#
# THE WINDOW.  ISO's D[] reaches 1.144989, so it is kept in Q14.  Output j of
# a slot (x3sim.synth_taps) reads, for i = 0..7, X index a_j from the slot of
# age 2i and X index b_j from the slot of age 2i+1:
#   SYNW[j][i] = (sA_j * D[64i + j], -D[64i + 32 + j])   (Q14, signs folded)
#   SYNIDX[j]  = (a_j, b_j)
# a_j = 16 + j (j < 16), 48 - j (j > 16), and for j = 16 the even taps are
# zero (their coefficient is stored as 0 and a_j as 0); b_j = |16 - j|.
# A reduced-rate output m (divisor d) is row j = d*m with both indices / d,
# so one table serves all three divisors.


def rot(theta):
    return (q15(math.cos(theta)), q15(math.sin(theta)))


def synth_window():
    D = [rnd(d / 4.0) for d in ISO.D_INT]
    w, ix = [], []
    for j in range(32):
        if j < 16:
            a, sa = 16 + j, 1
        elif j > 16:
            a, sa = 48 - j, -1
        else:
            a, sa = 0, 0
        b = 16 - j if j <= 16 else j - 16
        w.append([(sa * D[64 * i + j], -D[64 * i + 32 + j]) for i in range(8)])
        ix.append((a, b))
    return D, w, ix


# =============================================================================
# THE NUMERIC FORMATS (x3sim's docstring is the table; these are its numbers)
# =============================================================================

X3_XRQ = 28          # requantised lines, stereo, antialias: int32 Q28
X3_SBQ = 14          # overlap, subband samples, the DCT, V: int16 Q14
X3_DQ = 14           # the synthesis window D: Q14
X3_PCMQ = 15         # output PCM: int16 Q15 (full scale = 1.0)
X3_IMDCT_HEAD = 4    # bits left above a band's largest IMDCT input
X3_K_MIN = -6        # coarsest band scale (Q8): bitlength 31 at Q28
X3_K_MAX = 14        # finest band scale (Q28): a silent band


class _Std:
    pass


def std_constants():
    S = _Std()
    S.P43_F = P43_F
    S.P43_M, S.P43_E = pow43_tables()
    S.IS1_Q, S.IS1_QMS = IS1_Q, IS1_QMS
    S.IS2_Q, S.IS2_QMS = IS2_Q, IS2_QMS
    S.SQRT2_Q14 = SQRT2_Q14
    S.AA_CS, S.AA_CA = AA_CS, AA_CA
    S.DCT9 = {k: q15(v) for k, v in DCT9_F.items()}
    (t90, t91), (t30, t31) = twiddles_float()
    S.TW9 = ([q15(v) for v in t90], [q15(v) for v in t91])
    S.TW3 = ([q15(v) for v in t30], [q15(v) for v in t31])
    w0, w1 = mdct_window_float()
    S.WIN = ([q15(v) for v in w0], [q15(v) for v in w1])
    # the twiddle merged into the window: out[i] = o w[i] - co a[i] - si b[i],
    # out[17-i] = o w9[i] + co c[i] + si d[i] (x3sim._merge)
    S.IMW36 = []
    for w in (w0, w1):
        S.IMW36.append({
            'w': [q15(w[i]) for i in range(9)], 'w9': [q15(w[9 + i]) for i in range(9)],
            'a': [q15(t91[i] * w[9 + i]) for i in range(9)],
            'b': [q15(t90[i] * w[9 + i]) for i in range(9)],
            'c': [q15(t91[i] * w[i]) for i in range(9)],
            'd': [q15(t90[i] * w[i]) for i in range(9)]})
    S.IMW12 = {
        'w': [q15(t30[2 - i]) for i in range(3)], 'w9': [q15(t31[2 - i]) for i in range(3)],
        'a': [q15(t31[i] * t31[2 - i]) for i in range(3)],
        'b': [q15(t30[i] * t31[2 - i]) for i in range(3)],
        'c': [q15(t31[i] * t30[2 - i]) for i in range(3)],
        'd': [q15(t30[i] * t30[2 - i]) for i in range(3)]}
    S.COS_PI4 = q15(math.cos(math.pi / 4))
    S.DCT4_PRE = {m: [rot(math.pi * (4 * k + 1) / (4 * m)) for k in range(m // 2)]
                  for m in (2, 4, 8, 16)}
    S.DCT4_POST = {m: [rot(math.pi * k / m) for k in range(m // 2)] for m in (2, 4, 8, 16)}
    S.FFT_TW = {n: [rot(2 * math.pi * k / n) for k in range(n // 2)] for n in (2, 4, 8)}
    S.D_Q14, S.SYNW, S.SYNIDX = synth_window()
    S.HUF = {t: build_huff(t) for t in PAIR_TREES + COUNT1_TREES}
    return S


STD = std_constants()


# =============================================================================
# THE BLOB: what the STANDARD part's OPEN reads (little-endian, 8086)
# =============================================================================
#
#   header  'X3ST'  dw GEN_VERSION, dw table count, then per table
#           dw offset, dw length (bytes), dd CRC-32 (zlib's)
#   tables  each word-aligned, in the order of TABLES below
#
# Every table ships RAW: see shipping_report() for the arithmetic.

def _w(vals):
    return struct.pack('<%dh' % len(vals), *vals)


def _uw(vals):
    return struct.pack('<%dH' % len(vals), *vals)


def table_blobs(S=None):
    """[(name, bytes, description)] in blob order."""
    S = S or STD
    out = []
    huf_dir, huf = [], b''
    for t in PAIR_TREES + COUNT1_TREES:
        e = S.HUF[t]
        huf_dir.append((t, len(huf) // 2, first_width(t)))
        huf += _uw(e)
    hdir = b''.join(struct.pack('<BBH', t, fw, off) for t, off, fw in huf_dir)
    out.append(('HUFDIR', hdir, '17 x (tree, first width, entry offset into HUF)'))
    out.append(('HUF', huf, 'the Huffman lookup entries of 17 trees, FIRST %d SUB %d'
                % (X3_HUF_FIRST, X3_HUF_SUB)))
    out.append(('P43M', _uw(S.P43_M), '|i|^(4/3) mantissas, i = 0..%d' % (len(S.P43_M) - 1)))
    out.append(('P43E', bytes(S.P43_E), '|i|^(4/3) exponents (bytes)'))
    out.append(('P43F', _w(S.P43_F), '2^(k/4), k = 0..3, Q14'))
    is_words = [v for kl, kr in S.IS1_Q for v in (kl, kr)] + \
               [v for kl, kr in S.IS1_QMS for v in (kl, kr)]
    for tab in (S.IS2_Q, S.IS2_QMS):
        for sc in (0, 1):
            is_words += tab[sc][:16]
    is_words.append(S.SQRT2_Q14)
    out.append(('IS', _w(is_words), 'intensity: MPEG-1 (kl,kr) x7, the same x sqrt 2, '
                'MPEG-2 k x16 for scale 0/1, the same x sqrt 2, sqrt 2; Q14'))
    out.append(('AA', _w(list(S.AA_CS) + list(S.AA_CA)), 'antialias cs[8], ca[8], Q15'))
    d9 = [S.DCT9[k] for k in ('c1', 'c2', 'c3', 'c4', 'c5', 's3', 'c6')]
    out.append(('DCT9', _w(d9), '9-point DCT-III c1 c2 c3 c4 c5 s3 c6, Q15'))
    imw = []
    for W in S.IMW36:
        for key in ('w', 'w9', 'a', 'b', 'c', 'd'):
            imw += W[key]
    imw += S.TW9[0] + S.TW9[1]
    for key in ('w', 'w9', 'a', 'b', 'c', 'd'):
        imw += S.IMW12[key]
    imw += S.TW3[0] + S.TW3[1]
    out.append(('IMW', _w(imw), 'IMDCT merged twiddle-window: 36-point normal and stop '
                '(w w9 a b c d, 9 each), tw0 tw1 (9 each), 12-point (w w9 a b c d, 3 '
                'each), tw0 tw1 (3 each); Q15'))
    rots = [S.COS_PI4]
    for m in (2, 4, 8, 16):
        rots += [v for cs in S.DCT4_PRE[m] for v in cs]
        rots += [v for cs in S.DCT4_POST[m] for v in cs]
    for n in (2, 4, 8):
        rots += [v for cs in S.FFT_TW[n] for v in cs]
    out.append(('ROT', _w(rots), 'cos pi/4; DCT-IV pre and post rotations for m = 2,4,8,16; '
                'FFT twiddles for n = 2,4,8; (cos, sin) pairs, Q15'))
    synw = [v for row in S.SYNW for pair in row for v in pair]
    out.append(('SYNW', _w(synw), 'synthesis window, 32 rows x 8 x (even, odd), Q14, '
                'signs folded, consumption order'))
    out.append(('SYNIDX', bytes(v for ab in S.SYNIDX for v in ab), '32 x (a_j, b_j)'))
    return out


def blob_and_inc(S=None):
    tabs = table_blobs(S)
    hdr_len = 8 + 8 * len(tabs)
    body = b''
    dirs = []
    for name, data, _ in tabs:
        if (hdr_len + len(body)) & 1:
            body += b'\0'
        off = hdr_len + len(body)
        dirs.append((name, off, len(data), zlib.crc32(data) & 0xFFFFFFFF))
        body += data
    head = b'X3ST' + struct.pack('<HH', GEN_VERSION, len(tabs))
    head += b''.join(struct.pack('<HHI', off, ln, crc) for _, off, ln, crc in dirs)
    blob = head + body
    inc = ['; x3std.inc - made by tools/x3tables.py (generator %d); do not edit.' % GEN_VERSION,
           '; The STANDARD backend\'s table blob x3std.bin: offsets, lengths, CRC-32s.',
           '; Formats are tools/x3sim.py\'s docstring table (SPEC.md 121.4).',
           'X3T_GEN equ %d' % GEN_VERSION,
           'X3T_COUNT equ %d' % len(tabs),
           'X3T_BYTES equ %d' % len(blob),
           'X3T_CRC equ 0x%08X' % (zlib.crc32(blob) & 0xFFFFFFFF)]
    for name, off, ln, crc in dirs:
        inc.append('X3T_%s_OFF equ %d' % (name, off))
        inc.append('X3T_%s_LEN equ %d' % (name, ln))
        inc.append('X3T_%s_CRC equ 0x%08X' % (name, crc))
    for k in ('X3_XRQ', 'X3_SBQ', 'X3_DQ', 'X3_PCMQ', 'X3_IMDCT_HEAD', 'X3_K_MIN',
              'X3_K_MAX', 'X3_P43_N', 'X3_HUF_FIRST', 'X3_HUF_SUB'):
        inc.append('%s equ %d' % (k, globals()[k]))
    return blob, '\n'.join(inc) + '\n', dirs


# =============================================================================
# SHIPPING: raw or expanded at OPEN (SPEC.md 121.10)
# =============================================================================

SHIPPING = {
    'HUFDIR': 'raw',
    'HUF': 'raw: building these from the 1,474 codewords at OPEN needs ~3.4 KB of '
           'codebook input to save ~5.4 KB, and a recursive table builder in the part',
    'P43M': 'raw: x^(4/3) has no exact integer recurrence a 16-bit CPU runs cheaply '
            '(every entry is a rounded cube root)',
    'P43E': 'raw: derivable from P43M only with the same cube roots',
    'P43F': 'raw',
    'IS': 'raw',
    'AA': 'raw',
    'DCT9': 'raw',
    'IMW': 'raw: products of rounded trigonometry; no recurrence reproduces '
           'the per-entry rounding',
    'ROT': 'raw: a Chebyshev recurrence drifts from the per-entry rounding',
    'SYNW': 'raw: |D[i]| = |D[512-i]| would save 512 bytes for a permuting loop '
            'at OPEN - not worth the code',
    'SYNIDX': 'raw (also a closed formula)',
}


def shipping_report():
    blob, inc, dirs = blob_and_inc()
    lines = ['STANDARD table blob x3std.bin: %d bytes, CRC-32 %08X' % (
        len(blob), zlib.crc32(blob) & 0xFFFFFFFF)]
    for name, off, ln, crc in dirs:
        lines.append('  %-7s %6d bytes  crc %08X  %s' % (name, ln, crc, SHIPPING[name]))
    return '\n'.join(lines)


# =============================================================================
# SELF-CHECK
# =============================================================================

def selfcheck():
    """Every table against the formula it came from; returns failures."""
    bad = []

    def check(ok, msg):
        if not ok:
            bad.append(msg)

    S = STD
    # Huffman: every codeword of every tree decodes to its value and length
    for t in PAIR_TREES + COUNT1_TREES:
        tab = S.HUF[t]
        fw = first_width(t)
        check(len(tab) < 2048, 'HUF %d: %d entries > 2047' % (t, len(tab)))
        for code, v in tree_codes(t):
            bits = code + '0' * 24

            def peek(n, bits=bits):
                return int(bits[:n], 2) if n else 0
            val, used, _ = huff_decode(tab, peek, fw)
            check(val == v and used == len(code),
                  'HUF %d: %s decodes to %x/%d' % (t, code, val, used))
        kraft = sum(2.0 ** -len(c) for c, _ in tree_codes(t))
        check(abs(kraft - 1.0) < 1e-12, 'HUF %d: Kraft %r' % (t, kraft))
    # pow43: mantissa range, rounding, monotonic, octave
    prev = -1.0
    for i in range(len(S.P43_M)):
        m, e = S.P43_M[i], S.P43_E[i]
        if i == 0:
            check(m == 0, 'P43 0')
            continue
        v = i ** (4.0 / 3.0)
        check(16384 <= m <= 32767, 'P43 %d mantissa %d' % (i, m))
        check(abs(m - v * 2.0 ** (14 - e)) <= 0.5 + 1e-9, 'P43 %d rounding' % i)
        q = m * 2.0 ** (e - 14)
        check(q >= prev, 'P43 %d not monotonic' % i)
        prev = q
    check(len(S.P43_M) == X3_P43_N + 3, 'P43 length')
    # Q15/Q14 constants: half an LSB of the float, clamped only at 1.0
    for name, vals, fl, sc in (
            ('AA_CS', S.AA_CS, aa_float()[0], 32768), ('AA_CA', S.AA_CA, aa_float()[1], 32768),
            ('P43_F', S.P43_F, [2 ** (k / 4) for k in range(4)], 16384)):
        for q, f in zip(vals, fl):
            check(q == max(-32768, min(32767, rnd(f * sc))) and abs(q - f * sc) <= 1.0,
                  '%s %d vs %r' % (name, q, f))
    w0, w1 = mdct_window_float()
    for wi, w in enumerate((w0, w1)):
        for i, (q, f) in enumerate(zip(S.WIN[wi], w)):
            check(abs(q - f * 32768) <= 0.5 or (f == 1.0 and q == 32767), 'WIN %d %d' % (wi, i))
        # the merged IMDCT stage cannot overflow 32 bits:
        # 32767 * (|w| + |a| + |b|) and 32767 * (|w9| + |c| + |d|) < 2^31
        W = S.IMW36[wi]
        for i in range(9):
            for k1, k2, k3 in (('w', 'a', 'b'), ('w9', 'c', 'd')):
                s = abs(W[k1][i]) + abs(W[k2][i]) + abs(W[k3][i])
                check(32768 * s < 2 ** 31, 'IMW36 %d %d bound %d' % (wi, i, s))
            # Princen-Bradley: the halves of a window are power-complementary
            check(abs(w[i] ** 2 + w[9 + i] ** 2 - 1.0) < 1e-9 or wi == 1,
                  'window %d not power-complementary at %d' % (wi, i))
    for i in range(3):
        for k1, k2, k3 in (('w', 'a', 'b'), ('w9', 'c', 'd')):
            s = abs(S.IMW12[k1][i]) + abs(S.IMW12[k2][i]) + abs(S.IMW12[k3][i])
            check(32768 * s < 2 ** 31, 'IMW12 %d bound' % i)
    # rotations are rotations
    for tab in list(S.DCT4_PRE.values()) + list(S.DCT4_POST.values()) + list(S.FFT_TW.values()):
        for c, s_ in tab:
            check(abs((c * c + s_ * s_) / 32768.0 ** 2 - 1.0) < 2.0 / 32768, 'rotation %d %d' % (c, s_))
    # the window: ISO's symmetry, rounding, and the 32-bit accumulation bound
    D = S.D_Q14
    for i in range(1, 512):
        check(abs(ISO.D_INT[i]) == abs(ISO.D_INT[512 - i]), 'D symmetry %d' % i)
        check(abs(D[i] - ISO.D_INT[i] / 4.0) <= 0.5, 'D rounding %d' % i)
    for j in range(32):
        s = sum(abs(a) + abs(b) for a, b in S.SYNW[j])
        check(32768 * s < 2 ** 31, 'SYNW row %d: accumulation bound %d' % (j, s))
        if j == 16:
            check(all(a == 0 for a, _ in S.SYNW[j]), 'SYNW row 16 even taps not zero')
    # determinism: a second generation is byte-identical
    b1 = blob_and_inc(STD)[0]
    b2 = blob_and_inc(std_constants())[0]
    check(b1 == b2, 'two generations differ')
    return bad


def huffstats_report():
    lines = []
    for first in (4, 6, 8):
        sizes = huff_sizes(first)
        lines.append('first=%d sub=%d: %d bytes total (%s)' % (
            first, X3_HUF_SUB, sum(sizes.values()),
            ' '.join('%d:%d' % (t, s) for t, s in sorted(sizes.items()))))
    return '\n'.join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', metavar='DIR', help='write x3std.bin and x3std.inc there')
    ap.add_argument('--selfcheck', action='store_true')
    ap.add_argument('--huffstats', action='store_true')
    ap.add_argument('--report', action='store_true', help='the shipping report')
    a = ap.parse_args()
    rc = 0
    if a.selfcheck:
        bad = selfcheck()
        for m in bad[:40]:
            print('x3tables: FAIL %s' % m)
        if bad:
            print('x3tables: %d failure(s)' % len(bad))
            rc = 1
        else:
            blob = blob_and_inc()[0]
            print('x3tables: selfcheck ok, blob %d bytes, CRC-32 %08X' % (
                len(blob), zlib.crc32(blob) & 0xFFFFFFFF))
    if a.huffstats:
        print(huffstats_report())
    if a.report:
        print(shipping_report())
    if a.out:
        blob, inc, _ = blob_and_inc()
        os.makedirs(a.out, exist_ok=True)
        with open(os.path.join(a.out, 'x3std.bin'), 'wb') as f:
            f.write(blob)
        with open(os.path.join(a.out, 'x3std.inc'), 'w') as f:
            f.write(inc)
    if not (a.selfcheck or a.huffstats or a.report or a.out):
        ap.print_help()
    return rc


if __name__ == '__main__':
    sys.exit(main())
