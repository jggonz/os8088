#!/usr/bin/env python3
"""x3xttab.py - XTMP3's XT table packs: the deterministic generator (SPEC.md
121.4, 121.10; docs/xtmp3/XT-STUDY.md 'Table packs').

    python3 tools/x3xttab.py --selfcheck          # the make-rule check
    python3 tools/x3xttab.py --manifest           # sizes, layout, checksums
    python3 tools/x3xttab.py --emit DIR           # XTGEN.BIN + the packs
    python3 tools/x3xttab.py --derive             # recompute the integer
                                                  # constants from floats

Standard library only, and no floating point on the path to a table byte:
every constant a table is made from is an INTEGER written below (the
GENERATOR INPUT), and `--derive` is the one place floats appear, to show
those integers are what the formulas round to. So the same tables come out
on any host, forever, and their CRC-32s are part of the record.

What ships is the generator input (XTGEN.BIN, a few KB). OPEN expands the
packs the profile needs by the exact integer recurrences below, which use
additions where the code map allows (and one `mul` an entry for the 18-row
IMDCT map, which is Lloyd-Max and has no doubling structure), and checks
the result's word sum (see 'Checks'). This file runs the same recurrences in
Python and asserts them against the direct formula, entry by entry.

THE TABLES (Q formats are tools/x3xtmodel.py's)
------------------------------------------------
WIN    synthesis window product rows. One row a distinct |D| among a
       profile's kept taps (D[i] = ISO_WINBASE[..] / 65536, below). Row r,
       code k in -128..127 (word at byte 2(k+128) of the 512-byte row):
           T[r][k] = sign(k) * round_half_up(|ib_r| * lv_V[|k|] / 2^14)
       = round(2^13 * |D| * decode_V(k) / 2^11): V at Q11 in, window sum at
       Q13 out. lv_V is V_MAP's levels (k = -128 is unused and 0). Every
       entry fits a word: the largest is 1.145 * 4 * lv_V[127] / 2^11 FS.
       RECURRENCE (k >= 0), 32-bit unsigned A, B:
           A = 2^15                                  (the rounding bias)
           T[0] = 0
           for each segment s (V_MAP lens[s] levels at step 2^(s0+s)):
               B = 4 * |ib_r| << (s0 + s)            (once a segment)
               repeat lens[s]: A = A + B; T[next k] = A >> 16
           T[-k] = -T[k]
       On the 8088: `add bx,bp / adc ax,si / stosw` an entry (x3xtkern
       expand_entry), then a negating pass.
ENCV   the V encoder: 8,192 signed bytes, ENCV[v + 4096] = the code of the
       nearest V level to v (Q11 LSB), ties toward zero, for v in
       -4096..4095 (|V| < 2.0 FS; larger V takes the clamp's slow path).
       RECURRENCE: walk v = 0..4095 with k: while 2v > lv[k] + lv[k+1]:
       k += 1; ENCV[4096 + v] = k; ENCV[4096 - v] = -k (v > 0).
LEE    byte-split rows for Lee's 31 matrixing constants c = 1/(2cos(phi)):
       c16 = LEE_C16[i]. Four 256-byte planes a constant:
           L[l] = round_half_up(c16 * l / 2^16),      l = 0..255
           H[h] = sign(h) round_half_up(c16 |h| / 2^8), h = -128..127,
                  STORED MOD 2^16: x = 256h + l with l >= 0, so an in-range
                  product (|x c| < 2^15, the node's proven range) can need
                  an H up to 256c beyond a word; the word sum H + L is exact
                  mod 2^16 and lands in range whenever the product does
       planes: L low bytes, L high bytes, H low bytes, H high bytes, each
       indexed by the byte itself (H by h & 255).
       RECURRENCE: L: A = 2^15, L[l] = A >> 16, A += c16.
                   H: A = 2^7, H[h] = (A >> 8) mod 2^16, A += c16;
                   H[-h] = -H[h] mod 2^16.
AASPL  the same byte-split rows for the antialias cs[0..7], |ca[0..7]|
       (AA_C16; the kernel's add/sub carries ca's sign).
IMDCT  18 rows for |cos(pi m / 72)|, m odd (COS_C15), over R_MAP (Q13 in
       and out), code k in -128..127 as WIN:
           T[m][k] = sign(k) round_half_up(c15 * lv_R[|k|] / 2^15)
       RECURRENCE: one `mul` an entry (lv_R is Lloyd-Max, not doubling).
ENCR   the R encoder for antialiased lines: 16,384 signed bytes indexed by
       b = (v + 2) >> 2 + 8192 (v int16 Q13): the code of the nearest R
       level to 4b, ties toward zero. Same walk as ENCV.
IMWIN  byte-split rows for the IMDCT window: the 19 distinct values of the
       36-point sine window (the 12-point one is a subset) and 1.0, each
       / 4 (Q13 in, Q11 out), as WIN36_C16.
RQL    requantise-to-LINEAR for |ix| <= 15 (XT Standard and XT Speech, whose
       antialias works on linear words): RQL[g - RQ_G0][ix] = round half up
       of ix^(4/3) * 2^(g/4) * 2^13 (Q13), saturated at 32767, as words.
       Exact: the 12th power of the value is ix^16 * 2^(3g + 156), an
       integer for every g here, and its integer 12th root decides.
RQ     requantise-to-code for |ix| <= 15: RQ[g - RQ_G0][ix] = the code of
       the nearest R level to ix^(4/3) * 2^(g/4) (FS units at Q13), for
       g = RQ_G0..RQ_G1. Exact: compared in integers raised to the 12th
       power, no float. The exponent g is the study's quantiser's; phase 2
       aligns it with x3sim.py's (global_gain - 210 - scalefactor terms).

LAYOUT: every table lies inside one 64 KiB segment; a window segment also
holds the per-pair V-code rings of the output pairs whose rows it holds
(pairs (j, 32-j) read the same unique V value at every tap; 16 ages
doubled, 64 bytes a ring). `layout()` packs pairs into segments greedily,
duplicating a row only when two segments need it.

CHECKS: CRC-32 (zlib's) of every table and segment is the identity record,
for x3sim.py, the cache key and a test build. On the 5150 a table-driven
CRC-32 is ~60 clocks a byte (~2.9 s over the XT Standard pack, modelled);
OPEN instead checks `fl32`, a Fletcher sum over words (s1 += w, s2 += s1,
mod 2^16; ~0.7 s over the same pack), recorded beside the CRC. A plain word
sum would not do: every odd-symmetric row sums to 0 (XT-STUDY.md 'Open
questions').
"""
import argparse
import json
import math
import os
import struct
import sys
import zlib

GEN_VERSION = 1

# ISO/IEC 11172-3 Annex B, Table 3-B.3: the synthesis window D[i] for
# i = 0..256, as integers in units of 2^-16 (D[i] = ISO_WINBASE[i] / 65536).
# The standard prints D to nine decimals and every value is an exact
# multiple of 2^-16 (its analysis twin C[i] = D[i] / 32 is a multiple of
# 2^-21), so this integer form IS the standard's table, not a rounding of
# it. The other 255 entries follow by symmetry (iso_window_int below). The
# same 257 integers are mpg123's `intwinbase`; they were read out of the
# host's libmpg123 to be sure of every digit.
ISO_WINBASE = (
    0, -1, -1, -1, -1, -1, -1, -2, -2, -2, -2, -3, -3, -4, -4, -5, -5, -6,
    -7, -7, -8, -9, -10, -11, -13, -14, -16, -17, -19, -21, -24, -26, -29,
    -31, -35, -38, -41, -45, -49, -53, -58, -63, -68, -73, -79, -85, -91,
    -97, -104, -111, -117, -125, -132, -139, -147, -154, -161, -169, -176,
    -183, -190, -196, -202, -208, -213, -218, -222, -225, -227, -228, -228,
    -227, -224, -221, -215, -208, -200, -189, -177, -163, -146, -127, -106,
    -83, -57, -29, 2, 36, 72, 111, 153, 197, 244, 294, 347, 401, 459, 519,
    581, 645, 711, 779, 848, 919, 991, 1064, 1137, 1210, 1283, 1356, 1428,
    1498, 1567, 1634, 1698, 1759, 1817, 1870, 1919, 1962, 2001, 2032, 2057,
    2075, 2085, 2087, 2080, 2063, 2037, 2000, 1952, 1893, 1822, 1739, 1644,
    1535, 1414, 1280, 1131, 970, 794, 605, 402, 185, -45, -288, -545, -814,
    -1095, -1388, -1692, -2006, -2330, -2663, -3004, -3351, -3705, -4063,
    -4425, -4788, -5153, -5517, -5879, -6237, -6589, -6935, -7271, -7597,
    -7910, -8209, -8491, -8755, -8998, -9219, -9416, -9585, -9727, -9838,
    -9916, -9959, -9966, -9935, -9863, -9750, -9592, -9389, -9139, -8840,
    -8492, -8092, -7640, -7134, -6574, -5959, -5288, -4561, -3776, -2935,
    -2037, -1082, -70, 998, 2122, 3300, 4533, 5818, 7154, 8540, 9975, 11455,
    12980, 14548, 16155, 17799, 19478, 21189, 22929, 24694, 26482, 28289,
    30112, 31947, 33791, 35640, 37489, 39336, 41176, 43006, 44821, 46617,
    48390, 50137, 51853, 53534, 55178, 56778, 58333, 59838, 61289, 62684,
    64019, 65290, 66494, 67629, 68692, 69679, 70590, 71420, 72169, 72835,
    73415, 73908, 74313, 74630, 74856, 74992, 75038)


_WIN = None


def iso_window_int():
    """D[0..511] in units of 2^-16, sign included."""
    global _WIN
    if _WIN is None:
        out = []
        for i in range(512):
            b = ISO_WINBASE[i] if i <= 256 else ISO_WINBASE[512 - i]
            out.append(-b if (i // 64) & 1 else b)
        _WIN = tuple(out)
    return list(_WIN)


# ----------------------------------------------------------------------
# THE DESIGN, as chosen and measured by tools/x3xtstudy.py on its training
# material and validated on held-out material (XT-STUDY.md). The study's
# --check-design recomputes every one of these and fails on a difference.
# ----------------------------------------------------------------------
#@@DESIGN-BEGIN
DESIGN = {
    # the window's V code: optseg8, 9 segments, finest step 2^0 at Q11
    "v_map": {'name': 'optseg8', 'q': 11, 's0': 0, 'lens': [19, 18, 18, 18, 14, 10, 10, 10, 10]},
    # the IMDCT input code (Lloyd-Max on the training lines)
    "r_map": {"name": 'lloyd8', "q": 13, "levels": [
        0, 3, 6, 9, 12, 15, 19, 23, 27, 31, 35, 40,
        44, 48, 53, 57, 63, 69, 74, 82, 90, 97, 102, 108,
        120, 128, 136, 146, 152, 162, 173, 179, 193, 208, 216, 229,
        245, 256, 272, 290, 304, 324, 345, 362, 383, 389, 411, 429,
        463, 491, 511, 529, 551, 583, 610, 652, 690, 715, 740, 773,
        820, 861, 909, 923, 981, 1042, 1085, 1146, 1173, 1240, 1292, 1385,
        1442, 1493, 1552, 1646, 1721, 1775, 1829, 1928, 2037, 2168, 2216, 2321,
        2487, 2564, 2681, 2805, 2883, 3066, 3205, 3351, 3504, 3663, 3829, 4003,
        4185, 4374, 4572, 4779, 4996, 5221, 5457, 5704, 5961, 6230, 6511, 6804,
        7111, 7431, 7766, 8115, 8480, 8861, 9260, 9676, 10111, 10565, 11039, 11535,
        12052, 12593, 13158, 13748, 14365, 15009, 15681, 16384,
    ]},
    # XT Fast's and Turbo's IMDCT-window code (Lloyd-Max)
    "z_map": {"name": 'lloyd8', "q": 13, "levels": [
        0, 2, 4, 7, 10, 13, 17, 21, 24, 29, 33, 37,
        42, 47, 52, 57, 63, 70, 76, 84, 91, 98, 105, 112,
        119, 127, 136, 145, 155, 164, 174, 185, 196, 206, 218, 230,
        245, 257, 271, 287, 303, 319, 337, 355, 374, 396, 414, 428,
        449, 472, 500, 525, 549, 578, 608, 639, 670, 703, 738, 778,
        815, 856, 899, 945, 989, 1038, 1085, 1136, 1186, 1245, 1297, 1357,
        1423, 1496, 1567, 1637, 1714, 1792, 1879, 1960, 2066, 2151, 2253, 2354,
        2456, 2585, 2698, 2786, 2922, 3055, 3189, 3318, 3483, 3704, 3787, 4007,
        4171, 4374, 4582, 4811, 4950, 5221, 5457, 5717, 6048, 6230, 6511, 6804,
        7111, 7431, 7766, 8115, 8480, 8861, 9260, 9676, 10111, 10565, 11039, 11535,
        12052, 12593, 13158, 13748, 14365, 15009, 15681, 16384,
    ]},
    # 8 taps an output phase: the least-error subset, mirrored pairs
    "taps8": [
        [5, 6, 7, 8, 9, 10, 11, 12],  # phase 0
        [5, 6, 7, 8, 9, 10, 11, 12],  # phase 1
        [4, 5, 6, 7, 8, 9, 10, 11],  # phase 2
        [5, 6, 7, 8, 9, 10, 11, 12],  # phase 3
        [4, 5, 6, 7, 8, 9, 10, 11],  # phase 4
        [5, 6, 7, 8, 9, 10, 11, 12],  # phase 5
        [4, 5, 6, 7, 8, 9, 10, 11],  # phase 6
        [5, 6, 7, 8, 9, 10, 11, 12],  # phase 7
        [4, 5, 7, 8, 9, 10, 11, 12],  # phase 8
        [5, 6, 7, 8, 9, 10, 11, 12],  # phase 9
        [5, 6, 7, 8, 9, 10, 11, 12],  # phase 10
        [3, 5, 6, 7, 8, 9, 10, 12],  # phase 11
        [5, 6, 7, 8, 9, 10, 11, 12],  # phase 12
        [3, 5, 6, 7, 8, 9, 10, 12],  # phase 13
        [5, 6, 7, 8, 9, 10, 11, 12],  # phase 14
        [3, 5, 6, 7, 8, 9, 10, 12],  # phase 15
        [1, 3, 5, 7, 9, 11, 13, 15],  # phase 16
        [3, 5, 6, 7, 8, 9, 10, 12],  # phase 17
        [3, 4, 5, 6, 7, 8, 9, 10],  # phase 18
        [3, 5, 6, 7, 8, 9, 10, 12],  # phase 19
        [3, 4, 5, 6, 7, 8, 9, 10],  # phase 20
        [3, 5, 6, 7, 8, 9, 10, 12],  # phase 21
        [3, 4, 5, 6, 7, 8, 9, 10],  # phase 22
        [3, 4, 5, 6, 7, 8, 9, 10],  # phase 23
        [3, 4, 5, 6, 7, 8, 10, 11],  # phase 24
        [3, 4, 5, 6, 7, 8, 9, 10],  # phase 25
        [4, 5, 6, 7, 8, 9, 10, 11],  # phase 26
        [3, 4, 5, 6, 7, 8, 9, 10],  # phase 27
        [4, 5, 6, 7, 8, 9, 10, 11],  # phase 28
        [3, 4, 5, 6, 7, 8, 9, 10],  # phase 29
        [4, 5, 6, 7, 8, 9, 10, 11],  # phase 30
        [3, 4, 5, 6, 7, 8, 9, 10],  # phase 31
    ],
    # 4 taps an output phase: the least-error subset, mirrored pairs
    "taps4": [
        [7, 8, 9, 10],  # phase 0
        [7, 8, 9, 10],  # phase 1
        [7, 8, 9, 10],  # phase 2
        [7, 8, 9, 10],  # phase 3
        [7, 8, 9, 10],  # phase 4
        [7, 8, 9, 10],  # phase 5
        [7, 8, 9, 10],  # phase 6
        [7, 8, 9, 10],  # phase 7
        [7, 8, 9, 10],  # phase 8
        [5, 7, 8, 9],  # phase 9
        [7, 8, 9, 10],  # phase 10
        [5, 7, 8, 9],  # phase 11
        [7, 8, 9, 10],  # phase 12
        [5, 7, 8, 9],  # phase 13
        [7, 8, 9, 10],  # phase 14
        [6, 7, 8, 9],  # phase 15
        [3, 5, 7, 9],  # phase 16
        [6, 7, 8, 9],  # phase 17
        [5, 6, 7, 8],  # phase 18
        [6, 7, 8, 10],  # phase 19
        [5, 6, 7, 8],  # phase 20
        [6, 7, 8, 10],  # phase 21
        [5, 6, 7, 8],  # phase 22
        [6, 7, 8, 10],  # phase 23
        [5, 6, 7, 8],  # phase 24
        [5, 6, 7, 8],  # phase 25
        [5, 6, 7, 8],  # phase 26
        [5, 6, 7, 8],  # phase 27
        [5, 6, 7, 8],  # phase 28
        [5, 6, 7, 8],  # phase 29
        [5, 6, 7, 8],  # phase 30
        [5, 6, 7, 8],  # phase 31
    ],
}
#@@DESIGN-END

# Lee's matrixing constants c = 1/(2 cos((2k+1) pi / 2N)) as round(c * 2^16),
# in the order Lee's recursion meets them (N = 32 first, then the halves).
#@@CONSTS-BEGIN
LEE_C16 = (
    46341, 35468, 85627, 33410, 39410, 58981, 167963, 32927,
    34242, 37155, 42390, 51653, 69513, 112882, 334309, 32808,
    33127, 33780, 34802, 36248, 38203, 40796, 44224, 48794,
    55008, 63738, 76640, 97266, 134859, 223321, 667812,
)
AA_C16 = (
    56197, 57786, 62235, 64443, 65242, 65481, 65529, 65536,
    33718, 30915, 20538, 11922, 6198, 2685, 931, 242,
)
AA_C15 = (
    28098, 28893, 31117, 32221, 32621, 32740, 32765, 32768,
    16859, 15458, 10269, 5961, 3099, 1342, 465, 121,
)
COS_C15 = (
    32737, 32488, 31991, 31251, 30274, 29066, 27636, 25997,
    24159, 22138, 19948, 17606, 15131, 12540, 9854, 7092,
    4277, 1429,
)
WIN36_C16 = (
    715, 2139, 3546, 4927, 6270, 7565, 8803, 9974,
    11069, 12080, 12998, 13818, 14533, 15137, 15626, 15996,
    16244, 16368, 16384,
)
WIN36_C13 = (
    357, 1069, 1773, 2463, 3135, 3783, 4402, 4987,
    5534, 6040, 6499, 6909, 7266, 7568, 7813, 7998,
    8122, 8184, 8192,
)
#@@CONSTS-END

RQ_G0, RQ_G1 = -80, 7


# ================================================================ helpers
def u32(a):
    assert 0 <= a < (1 << 32), a
    return a


def s16(v):
    assert -32768 <= v <= 32767, v
    return v


def wrap(v):
    """v as a 16-bit two's complement word."""
    return ((v + 32768) & 0xFFFF) - 32768


def words(vals):
    return struct.pack("<%dh" % len(vals), *vals)


def sbytes(vals):
    return struct.pack("<%db" % len(vals), *vals)


def rha(num, den):
    """round half away from zero of the rational num/den (den > 0)."""
    q, r = divmod(abs(num), den)
    if 2 * r >= den:
        q += 1
    return q if num >= 0 else -q


def v_levels():
    m = DESIGN["v_map"]
    lv = [0]
    for i, n in enumerate(m["lens"]):
        for _ in range(n):
            lv.append(lv[-1] + (1 << (m["s0"] + i)))
    return lv


def r_levels():
    return list(DESIGN["r_map"]["levels"])


def z_levels():
    return list(DESIGN["z_map"]["levels"])


# ================================================================ WIN
def kept_taps(n):
    if n == 16:
        D = iso_window_int()
        return [[i for i in range(16) if D[32 * i + j] != 0] for j in range(32)]
    return DESIGN["taps%d" % n]


def win_row_values(n):
    """Distinct |ib| over the kept taps of an n-term profile, ascending."""
    D = iso_window_int()
    vals = set()
    for j, taps in enumerate(kept_taps(n)):
        for i in taps:
            if D[32 * i + j]:
                vals.add(abs(D[32 * i + j]))
    return sorted(vals)


def win_row_recurrence(ib, lens, s0):
    """The 8088's expansion of one window row: positive half by the running
    sum, negative half by negation. Returns 256 entries, codes -128..127."""
    pos = [0]
    A = 1 << 15
    for s, n in enumerate(lens):
        B = u32((4 * ib) << (s0 + s))
        for _ in range(n):
            A = u32(A + B)
            pos.append(A >> 16)
    assert len(pos) == 128
    row = [0] * 256
    for k in range(128):
        row[128 + k] = s16(pos[k])
        if k:
            row[128 - k] = s16(-pos[k])
    return row


def win_row_formula(ib, lv):
    """T[k] = sign(k) round(2^13 * (ib / 2^16) * lv[|k|] / 2^11), exactly."""
    # (ib / 2^16) * 2^13 * (lv / 2^11) = ib * lv / 2^14, exactly
    row = [0] * 256
    for k in range(-127, 128):
        e = rha(ib * lv[abs(k)], 1 << 14)
        row[128 + k] = e if k >= 0 else -e
    return row


# ================================================================ ENC
def enc_walk(lv, n_pos, at):
    """codes for magnitudes at(0..n_pos-1) by the walk: nearest level, ties
    toward zero (2v > lv[k] + lv[k+1] advances)."""
    out = []
    k = 0
    for i in range(n_pos):
        v = at(i)
        while k + 1 < len(lv) and 2 * v > lv[k] + lv[k + 1]:
            k += 1
        out.append(k)
    return out


def enc_v():
    lv = v_levels()
    pos = enc_walk(lv, 4097, lambda v: v)
    tab = [0] * 8192
    for v in range(-4096, 4096):
        tab[v + 4096] = pos[v] if v >= 0 else -pos[-v]
    return tab


def enc_r():
    lv = r_levels()
    pos = enc_walk(lv, 8193, lambda b: 4 * b)
    tab = [0] * 16384
    for b in range(-8192, 8192):
        tab[b + 8192] = pos[b] if b >= 0 else -pos[-b]
    return tab


def enc_formula(lv, x):
    """nearest level to |x| (ties toward zero), signed."""
    a = abs(x)
    best = 0
    for k in range(len(lv)):
        if abs(lv[k] - a) < abs(lv[best] - a):
            best = k
    return best if x >= 0 else -best


# ============================================================== split
def split_recurrence(c16):
    L = []
    A = 1 << 15
    for _ in range(256):
        L.append(A >> 16)
        A = u32(A + c16)
    Hp = []
    A = 1 << 7
    for _ in range(129):
        Hp.append(A >> 8)
        A = u32(A + c16)
    H = [0] * 256
    for h in range(-128, 128):
        H[h & 255] = wrap(Hp[h] if h >= 0 else -Hp[-h])
    return H, L


def split_formula(c16):
    L = [rha(c16 * l, 1 << 16) for l in range(256)]
    H = [0] * 256
    for h in range(-128, 128):
        H[h & 255] = wrap(rha(c16 * h, 1 << 8))
    return H, L


def split_planes(c16):
    H, L = split_recurrence(c16)
    Lw = [v & 0xFFFF for v in L]
    Hw = [v & 0xFFFF for v in H]
    return (bytes(v & 255 for v in Lw) + bytes(v >> 8 for v in Lw)
            + bytes(v & 255 for v in Hw) + bytes(v >> 8 for v in Hw))


# ============================================================== IMDCT
def mul_row(c, lv, shift):
    """sign(k) round(c * lv[|k|] / 2^shift), one multiply an entry."""
    row = [0] * 256
    for k in range(128):
        e = (c * lv[k] + (1 << (shift - 1))) >> shift
        row[128 + k] = s16(e)
        if k:
            row[128 - k] = s16(-e)
    return row


# ================================================================= RQ
_RQ = None


def rq_table():
    global _RQ
    if _RQ is None:
        _RQ = _rq_table()
    return [list(r) for r in _RQ]


def _rq_table():
    """RQ[g][ix] for g = RQ_G0..RQ_G1, ix = 0..15: the nearest R level (at
    Q13) to ix^(4/3) 2^(g/4), ties toward zero, in exact integers: the
    value exceeds a midpoint m/2^13 iff ix^16 2^(3g) > (m / 2^13)^12, both
    sides scaled to integers."""
    lv = r_levels()
    mids2 = [lv[k] + lv[k + 1] for k in range(len(lv) - 1)]     # 2 * mid
    out = []
    for g in range(RQ_G0, RQ_G1 + 1):
        row = []
        for ix in range(16):
            if ix == 0:
                row.append(0)
                continue
            # v = ix^(4/3) 2^(g/4); compare 2v against mids2[k] / 2^13:
            # (2v)^12 = 2^12 ix^16 2^(3g) vs (mids2[k])^12 / 2^156
            k = 0
            while k < len(mids2):
                lhs_num = (1 << 12) * ix ** 16
                e = 3 * g + 156
                rhs = mids2[k] ** 12
                if e >= 0:
                    gt = lhs_num * (1 << e) > rhs
                else:
                    gt = lhs_num > rhs * (1 << (-e))
                if not gt:
                    break
                k += 1
            row.append(k)
        out.append(row)
    return out


def iroot(n, k):
    """floor(n ** (1/k)) for a non-negative integer n, exactly."""
    if n < 2:
        return n
    x = 1 << ((n.bit_length() + k - 1) // k)
    while True:
        y = ((k - 1) * x + n // x ** (k - 1)) // k
        if y >= x:
            break
        x = y
    while x ** k > n:
        x -= 1
    while (x + 1) ** k <= n:
        x += 1
    return x


def rql_table():
    """RQL[g][ix]: round half up of ix^(4/3) 2^(g/4) 2^13, saturated. The
    value v has v^12 = ix^16 2^(3g + 156); scaled by 2^s so the exponent is
    not negative, r = floor(v 2^s) is an exact integer 12th root, and
    round(v) = (r + 2^(s-1)) >> s (or the half-up test at s = 0)."""
    out = []
    for g in range(RQ_G0, RQ_G1 + 1):
        row = []
        e = 3 * g + 156
        sc = max(0, -(e // 12)) if e < 0 else 0
        while e + 12 * sc < 0:
            sc += 1
        for ix in range(16):
            if ix == 0:
                row.append(0)
                continue
            N = ix ** 16 << (e + 12 * sc)
            r = iroot(N, 12)
            if sc:
                v = (r + (1 << (sc - 1))) >> sc
            else:
                v = r + 1 if (2 * r + 1) ** 12 <= (N << 12) else r
            row.append(min(v, 32767))
        out.append(row)
    return out


# ============================================================= packs
PACKS = {
    # pack -> (window terms, output-phase divisor, the tables it needs)
    "std": (16, 1, ("WIN", "ENCV", "LEE", "AASPL", "IMDCT", "ENCR", "IMWIN", "RQL")),
    "fast": (8, 1, ("WIN", "ENCV", "LEE", "IMDCT", "ENCR", "RQ", "AATAB", "IMWTAB")),
    "turbo": (4, 1, ("WIN", "ENCV", "LEE", "IMDCT", "ENCR", "RQ", "AATAB", "IMWTAB")),
    "speech": (16, 2, ("WIN", "ENCV", "LEE", "AASPL", "IMDCT", "ENCR", "IMWIN", "RQL")),
}


def lee_consts(N):
    """LEE_C16's constants that a Lee DCT of size N meets (sizes N, N/2..2),
    in LEE_C16's order."""
    want = set()
    M = N
    while M >= 2:
        for k in range(M // 2):
            want.add(int(round(0.5 / math.cos((2 * k + 1) * math.pi / (2 * M)) * 65536)))
        M //= 2
    return [c for c in LEE_C16 if c in want]
RING_BYTES = 64


def pair_of(j):
    return min(j, (32 - j) % 32)


def layout(n, div=1):
    """Pack the window's output pairs (only the phases a divisor keeps) into
    segments, first fit, largest row set first; a row two segments need is
    duplicated. Returns [{'pairs', 'rows' (ib values), 'bytes'}]."""
    D = iso_window_int()
    taps = kept_taps(n)
    need = {}
    for j in range(0, 32, div):
        p = pair_of(j)
        for i in taps[j]:
            if D[32 * i + j]:
                need.setdefault(p, set()).add(abs(D[32 * i + j]))
    segs = []
    for p in sorted(need, key=lambda q: (-len(need[q]), q)):
        for sg in segs:
            rows = sg["rows"] | need[p]
            if len(rows) * 512 + (len(sg["pairs"]) + 1) * RING_BYTES <= 65536:
                sg["rows"] = rows
                sg["pairs"].append(p)
                break
        else:
            segs.append({"pairs": [p], "rows": set(need[p])})
    for sg in segs:
        sg["pairs"].sort()
        sg["rows"] = sorted(sg["rows"])
        sg["bytes"] = len(sg["rows"]) * 512 + len(sg["pairs"]) * RING_BYTES
        assert sg["bytes"] <= 65536
    return segs


def build_tables(names, n, div=1):
    lvV, lvR = v_levels(), r_levels()
    vm = DESIGN["v_map"]
    T = {}
    for name in names:
        if name == "WIN":
            segs = layout(n, div)
            blobs = []
            for s in segs:
                b = b"".join(words(win_row_recurrence(ib, vm["lens"], vm["s0"])) for ib in s["rows"])
                b += bytes(len(s["pairs"]) * RING_BYTES)
                blobs.append(b)
            T["WIN"] = blobs
        elif name == "ENCV":
            T[name] = [sbytes(enc_v())]
        elif name == "ENCR":
            T[name] = [sbytes(enc_r())]
        elif name == "LEE":
            # a reduced rate's DCT-32/div needs only its own sizes' constants
            T[name] = [b"".join(split_planes(c) for c in lee_consts(32 // div))]
        elif name == "AASPL":
            T[name] = [b"".join(split_planes(c) for c in AA_C16)]
        elif name == "IMWIN":
            T[name] = [b"".join(split_planes(c) for c in WIN36_C16)]
        elif name == "IMDCT":
            T[name] = [b"".join(words(mul_row(c, lvR, 15)) for c in COS_C15)]
        elif name == "AATAB":
            T[name] = [b"".join(words(mul_row(c, lvR, 15)) for c in AA_C15)]
        elif name == "IMWTAB":
            lz = z_levels()
            T[name] = [b"".join(words(mul_row(c, lz, 15)) for c in WIN36_C13)]
        elif name == "RQ":
            T[name] = [bytes(v for row in rq_table() for v in row)]
        elif name == "RQL":
            T[name] = [words([v for row in rql_table() for v in row])]
        else:
            raise KeyError(name)
    return T


def fl32(b):
    """The on-target check: Fletcher over words mod 2^16, s1 = sum of words,
    s2 = sum of the running s1s; returned as s2 << 16 | s1. Position
    weighted, so the odd-symmetric rows (whose plain word sum is always 0)
    are checked too. Two `add`s a word on the 8088."""
    if len(b) % 2:
        b += b"\0"
    s1 = s2 = 0
    for w in struct.unpack("<%dH" % (len(b) // 2), b):
        s1 = (s1 + w) & 0xFFFF
        s2 = (s2 + s1) & 0xFFFF
    return (s2 << 16) | s1


def gen_input():
    """XTGEN.BIN: everything OPEN expands from. Little-endian.
      'X3XT' u16 version
      V map:  u8 q, u8 s0, u8 nseg, nseg x u8 lens
      R map:  u8 q, u8 n, n x u16 levels;  Z map: the same
      taps8:  32 x (u8 count, count x u8 tap);  taps4: the same
      LEE_C16 31 x u32, AA_C16 16 x u32, AA_C15 16 x u16, COS_C15 18 x u16,
      WIN36_C16 19 x u32, WIN36_C13 19 x u16,
      RQ: i8 g0, u8 rows, rows x 16 x u8;  RQL: rows x 16 x u16
    (RQ and RQL are shipped as data: they are small and need the 4/3
    power, which is the one thing the 8088 should not compute)
    (the ISO window is 257 x i32 more, 1,028 bytes; it is in the package
    image, which the STANDARD decoder shares).
    """
    b = bytearray(b"X3XT" + struct.pack("<H", GEN_VERSION))
    vm = DESIGN["v_map"]
    b += struct.pack("<BBB", vm["q"], vm["s0"], len(vm["lens"])) + bytes(vm["lens"])
    for key in ("r_map", "z_map"):
        lv = DESIGN[key]["levels"]
        b += struct.pack("<BB", DESIGN[key]["q"], len(lv)) + struct.pack("<%dH" % len(lv), *lv)
    for key in ("taps8", "taps4"):
        for taps in DESIGN[key]:
            b += bytes([len(taps)] + list(taps))
    b += struct.pack("<31I", *LEE_C16) + struct.pack("<16I", *AA_C16)
    b += struct.pack("<16H", *AA_C15) + struct.pack("<18H", *COS_C15)
    b += struct.pack("<19I", *WIN36_C16) + struct.pack("<19H", *WIN36_C13)
    rq = rq_table()
    b += struct.pack("<bB", RQ_G0, len(rq)) + bytes(v for row in rq for v in row)
    b += words([v for row in rql_table() for v in row])
    return bytes(b)


PASS_OF = {"LEE": "synthesis", "ENCV": "synthesis"}


def table_segments(names, T):
    """The non-window tables grouped by the pass that reads them, one
    segment a pass: matrixing reads LEE and forms codes through ENCV; the
    hybrid pass reads everything else. Each must fit 64 KiB."""
    segs = {}
    for name in names:
        if name == "WIN":
            continue
        segs.setdefault(PASS_OF.get(name, "hybrid"), []).append(name)
    out = []
    for k in ("synthesis", "hybrid"):
        if k in segs:
            nb = sum(len(b) for n in segs[k] for b in T[n])
            assert nb <= 65536, (k, nb)
            out.append({"pass": k, "tables": segs[k], "bytes": nb})
    return out


def manifest():
    out = {"generator": "x3xttab %d" % GEN_VERSION, "gen_input_bytes": len(gen_input()),
           "gen_input_crc32": "%08x" % zlib.crc32(gen_input()), "packs": {}}
    for pk, (n, div, names) in PACKS.items():
        T = build_tables(names, n, div)
        rec = {"window_terms": n, "divisor": div, "tables": {}, "total": 0}
        for name, blobs in T.items():
            rec["tables"][name] = [{"bytes": len(b), "crc32": "%08x" % zlib.crc32(b),
                                    "fl32": "%08x" % fl32(b)} for b in blobs]
            rec["total"] += sum(len(b) for b in blobs)
        segs = layout(n, div)
        rec["window_segments"] = [{"pairs": s["pairs"], "rows": len(s["rows"]),
                                   "bytes": s["bytes"]} for s in segs]
        rec["table_segments"] = table_segments(names, T)
        out["packs"][pk] = rec
    return out


# ============================================================ derive
def derive_consts():
    """The integer constants from their formulas (floats, HERE only)."""
    lee = []

    def walk(N):
        # x3xtmodel.DctPlan's order: the even half's recursion, then this
        # size's odd constants, then the odd half's recursion
        if N == 1:
            return
        walk(N // 2)
        for k in range(N // 2):
            lee.append(int(round(0.5 / math.cos((2 * k + 1) * math.pi / (2 * N)) * 65536)))
        walk(N // 2)
    walk(32)
    uniq = []
    for c in lee:
        if c not in uniq:
            uniq.append(c)
    cc = [-0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037]
    cs = [int(round(1 / math.sqrt(1 + c * c) * 65536)) for c in cc]
    ca = [int(round(abs(c) / math.sqrt(1 + c * c) * 65536)) for c in cc]
    cos15 = [int(round(math.cos(math.pi * m / 72) * 32768)) for m in range(1, 36, 2)]
    cs15 = [int(round(1 / math.sqrt(1 + c * c) * 32768)) for c in cc]
    ca15 = [int(round(abs(c) / math.sqrt(1 + c * c) * 32768)) for c in cc]
    w = sorted(set([round(math.sin(math.pi / 36 * (i + 0.5)), 15) for i in range(18)] + [1.0]))
    win = [int(round(v / 4 * 65536)) for v in w]
    win13 = [int(round(v * 8192)) for v in w]
    return {"LEE_C16": uniq, "AA_C16": cs + ca, "AA_C15": cs15 + ca15, "COS_C15": cos15,
            "WIN36_C16": win, "WIN36_C13": win13}


# ========================================================== selfcheck
def selfcheck(verbose=False):
    fails = []

    def check(cond, msg):
        if not cond:
            fails.append(msg)
    D = iso_window_int()
    check(len(D) == 512 and D[0] == 0 and D[256] == 75038, "window shape")
    for i in range(1, 512):
        if i % 64:
            check(D[512 - i] == -D[i], "window antisymmetry %d" % i)
        else:
            check(D[512 - i] == D[i], "window symmetry %d" % i)
    lvV, lvR = v_levels(), r_levels()
    check(len(lvV) == 128 and lvV[0] == 0, "V map has 128 levels from 0")
    check(all(lvV[k] < lvV[k + 1] for k in range(127)), "V map increasing")
    check(len(lvR) == 128 and lvR[0] == 0 and all(lvR[k] < lvR[k + 1] for k in range(127)), "R map")
    vm = DESIGN["v_map"]
    # WIN: recurrence == formula on every row of every profile, fits a word,
    # odd symmetry, code 0 is 0, the -128 entry 0, monotone in k
    rows = set()
    for n in (16, 8, 4):
        rows |= set(win_row_values(n))
    for ib in sorted(rows):
        r = win_row_recurrence(ib, vm["lens"], vm["s0"])
        f = win_row_formula(ib, lvV)
        check(r == f, "WIN row %d: recurrence != formula" % ib)
        check(r[128] == 0 and r[0] == 0, "WIN row %d: zero codes" % ib)
        check(all(r[128 + k] == -r[128 - k] for k in range(1, 128)), "WIN row %d: odd symmetry" % ib)
        check(all(r[128 + k] <= r[129 + k] for k in range(127)), "WIN row %d: monotone" % ib)
    check(max(abs(v) for v in win_row_recurrence(max(rows), vm["lens"], vm["s0"])) <= 32767,
          "WIN: the largest entry fits a word")
    # the 8- and 4-term tap sets: mirror pairs keep mirrored taps
    for n in (8, 4):
        taps = kept_taps(n)
        check(all(len(t) == n for t in taps), "taps%d: %d a phase" % (n, n))
        for j in range(1, 32):
            if j != 16:
                check(sorted(15 - i for i in taps[j]) == sorted(taps[32 - j]),
                      "taps%d: phases %d and %d mirror" % (n, j, 32 - j))
    # ENC: the walk is the nearest-level rule everywhere, odd, saturating
    ev = enc_v()
    for v in list(range(-4096, 4096, 7)) + [-4096, -1, 0, 1, 4095]:
        check(ev[v + 4096] == enc_formula(lvV, v), "ENCV %d" % v)
    check(ev[4096] == 0, "ENCV(0) = 0")
    er = enc_r()
    for b in list(range(-8192, 8192, 13)) + [-8192, -1, 0, 1, 8191]:
        check(er[b + 8192] == enc_formula(lvR, 4 * b), "ENCR %d" % b)
    # split rows: recurrence == formula, and the pair reconstructs x * c
    for c in list(LEE_C16) + list(AA_C16) + list(WIN36_C16):
        H, L = split_recurrence(c)
        Hf, Lf = split_formula(c)
        check(H == Hf and L == Lf, "split %d: recurrence != formula" % c)
        lim = min(32767, (32766 * 65536) // c)
        for x in list(range(-lim, lim + 1, 97)) + [-lim, lim, -1, 0, 1, 255, 256, -256]:
            got = wrap(H[(x >> 8) & 255] + L[x & 255])
            check(abs(got - x * c / 65536.0) <= 1.0, "split %d at %d" % (c, x))
    # IMDCT rows: odd, fit, equal the direct formula
    for c in COS_C15:
        r = mul_row(c, lvR, 15)
        check(all(r[128 + k] == rha(c * lvR[k], 1 << 15) for k in range(128)), "IMDCT row %d" % c)
    # RQ: monotone in ix and in g; exact against a float check away from ties
    rq = rq_table()
    for gi, row in enumerate(rq):
        check(all(row[i] <= row[i + 1] for i in range(15)), "RQ row %d monotone" % gi)
        g = RQ_G0 + gi
        for ix in range(1, 16):
            v = ix ** (4.0 / 3) * 2 ** (g / 4.0) * 8192
            k = enc_formula(lvR, v)
            near_tie = any(abs(2 * v - (lvR[q] + lvR[q + 1])) < 1e-6 for q in range(127))
            if not near_tie:
                check(row[ix] == k, "RQ g=%d ix=%d" % (g, ix))
    for gi in range(len(rq) - 1):
        check(all(rq[gi][i] <= rq[gi + 1][i] for i in range(16)), "RQ monotone in g")
    # RQL: exact against a float check away from ties, monotone, saturating
    rql = rql_table()
    for gi, row in enumerate(rql):
        g = RQ_G0 + gi
        check(all(row[i] <= row[i + 1] for i in range(15)), "RQL row %d monotone" % gi)
        for ix in range(1, 16):
            v = ix ** (4.0 / 3) * 2 ** (g / 4.0) * 8192
            if abs(v - math.floor(v) - 0.5) > 1e-6:
                check(row[ix] == min(32767, int(math.floor(v + 0.5))), "RQL g=%d ix=%d" % (g, ix))
    # layout: every segment fits, every pair's rows are in its segment
    for n in (16, 8, 4):
        segs = layout(n)
        D = iso_window_int()
        taps = kept_taps(n)
        for s in segs:
            check(s["bytes"] <= 65536, "segment over 64 KiB")
            for p in s["pairs"]:
                for j in {p, (32 - p) % 32}:
                    for i in taps[j]:
                        if D[32 * i + j]:
                            check(abs(D[32 * i + j]) in s["rows"], "pair %d row missing" % p)
    # determinism: the whole manifest twice would double the run; the
    # window segments (the recurrence-built bulk) are built twice instead
    a = build_tables(("WIN",), 16)["WIN"]
    b = build_tables(("WIN",), 16)["WIN"]
    check(a == b, "two builds differ")
    m = manifest()
    check(m["packs"]["std"]["total"] < 240000, "the std pack grew past 240,000 bytes")
    # NEGATIVE CONTROLS: each check above must be able to fail. A window row
    # built without the rounding bias, a split table built from c16 + 1, an
    # encoder walk that advances on ties, and a mirrored row error that a
    # plain word sum cannot see (fl32 must)
    ib = max(rows)
    nb = []
    A = 0
    for sgi, nn in enumerate(vm["lens"]):
        for _ in range(nn):
            A += (4 * ib) << (vm["s0"] + sgi)
            nb.append(A >> 16)
    check(nb != win_row_formula(ib, lvV)[129:], "control: an unbiased row passed")
    check(split_recurrence(LEE_C16[0] + 1) != split_formula(LEE_C16[0]), "control: c16+1 passed")
    bad = [0] * 4097
    k = 0
    for v in range(4097):
        while k + 1 < len(lvV) and 2 * v >= lvV[k] + lvV[k + 1]:
            k += 1
        bad[v] = k
    check(any(bad[v] != enc_formula(lvV, v) for v in range(4097)), "control: ties-up walk passed")
    good = win_row_recurrence(ib, vm["lens"], vm["s0"])
    mir = list(good)
    mir[128 + 5] += 1
    mir[128 - 5] -= 1
    check(sum(good) % 65536 == sum(mir) % 65536, "control: the word sum sees a mirrored error")
    check(fl32(words(good)) != fl32(words(mir)), "control: fl32 misses a mirrored error")
    # the constants are what their formulas round to
    dv = derive_consts()
    for k in ("LEE_C16", "AA_C16", "AA_C15", "COS_C15", "WIN36_C16", "WIN36_C13"):
        check(list(globals()[k]) == list(dv[k]), "%s differs from --derive" % k)
    if fails:
        for f in fails[:40]:
            print("x3xttab: FAIL", f)
        print("x3xttab: %d failures" % len(fails))
        return 1
    if verbose:
        print("x3xttab: selfcheck ok")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--selfcheck", action="store_true")
    ap.add_argument("--manifest", action="store_true")
    ap.add_argument("--derive", action="store_true")
    ap.add_argument("--emit", metavar="DIR")
    a = ap.parse_args()
    if a.derive:
        for k, v in derive_consts().items():
            print("%s = %r" % (k, tuple(v)))
        return 0
    if a.manifest:
        print(json.dumps(manifest(), indent=1))
        return 0
    if a.emit:
        os.makedirs(a.emit, exist_ok=True)
        with open(os.path.join(a.emit, "XTGEN.BIN"), "wb") as f:
            f.write(gen_input())
        for pk, (n, div, names) in PACKS.items():
            T = build_tables(names, n, div)
            for name, blobs in T.items():
                for i, b in enumerate(blobs):
                    with open(os.path.join(a.emit, "%s_%s%d.BIN" % (pk.upper(), name, i)), "wb") as f:
                        f.write(b)
        with open(os.path.join(a.emit, "manifest.json"), "w") as f:
            json.dump(manifest(), f, indent=1)
        return 0
    return selfcheck(verbose=True)


if __name__ == "__main__":
    sys.exit(main())
