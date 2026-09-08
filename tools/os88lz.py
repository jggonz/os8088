#!/usr/bin/env python3
"""os8088 compression formats - the host-side authority (docs/plans/O88-COMPRESSION-PLAN.md).

Two formats, and which one a file uses is two bits in its header:

  LZ4  (id 0)  byte-oriented, min match 4, 16-bit offsets. The default: it is
               the only candidate that pays for itself at LOAD time on the
               target machine (plan 1), decoding at 50.6 cycles a byte.
  LZB  (id 1)  bit-oriented, aPLib-family - a tag BYTE carried with a guard
               bit, byte-aligned literals and low offset byte, interlaced
               Elias gamma for the length and the high offset byte. ~10 points
               better on ratio and four times the decode (plan 12.7).

**Every stream ends in a RAW TAIL, and that is what makes in-place expansion
need no margin** (SPEC.md 20.13.7). A stream is

    +0  word  T, the tail's length
    +2        the LZ symbols
    end-T     T raw bytes - the last T bytes of the output, stored as they are

and the encoder cuts the symbols at the FIRST point where (produced - consumed)
peaks, storing the rest raw. The suffix after that peak was net-EXPANDING by
exactly what the old in-place margin measured, so the cut makes the file
smaller by that much - and a decoder reading its source from `U - P` above
its output can never overtake it, the tail's source landing ON its
destination. `_cut` is the whole of it and both encoders (and the machine's,
`lzb_compress_machine`) go through it. The boot blob's kernel stream is the
one exception: boot/boot2.asm carries its own copy of the LZ4 arm and reads
the classic ending, so `tools/os88kz.py` asks for `tail=False`.

**This file is the reference implementation and the kernel is the copy**, so a
disagreement between them is a bug here or there and never a judgement call:
`--selfcheck` round-trips both formats over the tree's own binaries, and
tests/unit/t_lzfmt.py is the same check as a suite row.

NO THIRD-PARTY IMPORTS, deliberately - `make` needs nasm and python3 and the
plan is not going to be the thing that adds a pip install to the build.
"""
import sys

# --- format ids, as stored in a header's format field ------------------------
LZ4, LZB = 0, 1
NAMES = {LZ4: "lz4", LZB: "lzb"}

MINMATCH = 4                    # LZ4: a match is at least 4 bytes
MFLIMIT  = 12                   # ...and none may START in the last 12
LASTLITS = 5                    # ...and a block ends in at least 5 literals


# =============================================================================
# match finding - hash chains over 4-byte prefixes, shared by both encoders
# =============================================================================
class Chains:
    """prev[i] = 1 + the previous position with i's hash, 0 = end of chain."""

    def __init__(self, src, minlen):
        self.src = src
        self.minlen = minlen
        self.head = {}
        self.prev = [0] * (len(src) + 1)
        self.at = 0             # every position below this has been inserted

    def _key(self, i):
        return self.src[i:i + self.minlen]

    def insert_to(self, upto):
        """chains are only correct if every position is inserted, in order"""
        src, n = self.src, len(self.src) - self.minlen + 1
        while self.at <= min(upto, n - 1):
            k = self._key(self.at)
            self.prev[self.at] = self.head.get(k, -1) + 1
            self.head[k] = self.at
            self.at += 1

    def find(self, pos, maxoff, maxlen, depth):
        """longest match at pos: (length, offset), or (0, 0)."""
        src = self.src
        limit = min(maxlen, len(src) - pos)
        if limit < self.minlen:
            return 0, 0
        self.insert_to(pos)
        lo = pos - maxoff
        c = self.prev[pos] - 1
        best_l, best_o, k = 0, 0, 0
        while c >= 0 and c > lo and k < depth:
            k += 1
            if src[c + best_l] == src[pos + best_l]:      # cheap reject
                l = 0
                while l < limit and src[c + l] == src[pos + l]:
                    l += 1
                if l > best_l:
                    best_l, best_o = l, pos - c
                    if l >= limit:
                        break
            c = self.prev[c] - 1
        return (best_l, best_o) if best_l >= self.minlen else (0, 0)


# =============================================================================
# the raw tail (SPEC.md 20.13.7) - the one rule both formats share
# =============================================================================
def _cut(src, out, bounds):
    """Finish a stream: cut the symbols at the first peak of (produced -
    consumed) and store the rest raw, behind the T word the stream begins with.

    `out` is the symbols with two bytes reserved in front; `bounds` is
    (produced, consumed) at every symbol boundary in order, `consumed`
    counting those two bytes. The peak is tracked EXACTLY as kernel/
    compress.inc tracks it - strictly greater than a running maximum that
    starts at 0, so a negative lead never wins and a tie keeps the earlier
    point - because tests/lzcomp.py compares the machine's stream with
    lzb_compress_machine's byte for byte.
    """
    mx, kp, kc = 0, 0, 2
    for p, c in bounds:
        if p - c > mx:
            mx, kp, kc = p - c, p, c
    tail = src[kp:]
    if len(tail) > 0xFFFF:
        # a tail the T word cannot count: the stream is over 64KB whatever
        # else is in it, which is a file that compressed badly and is stored
        # plain (cz_wrap, SPEC.md 20.14.5) - so this is a refusal, not a
        # stream
        raise ValueError(f"a raw tail of {len(tail)} bytes does not fit T")
    return len(tail).to_bytes(2, "little") + bytes(out[2:kc]) + tail


def _tail_bounds(buf):
    """(first symbol byte, where the tail begins) for a stream, or an error."""
    if len(buf) < 2:
        raise ValueError("no T word")
    t = int.from_bytes(buf[0:2], "little")
    end = len(buf) - t
    if end < 2:
        raise ValueError(f"a tail of {t} in a stream of {len(buf)}")
    return 2, end


# =============================================================================
# LZ4
# =============================================================================
def _lz4_seq_len(litlen, mlen):
    c = 1 + litlen
    if litlen >= 15:
        c += 1 + (litlen - 15) // 255
    if mlen:
        c += 2
        e = mlen - MINMATCH
        if e >= 15:
            c += 1 + (e - 15) // 255
    return c


def _lz4_emit(out, lits, mlen, off):
    tok = (min(len(lits), 15) << 4) | (min(mlen - MINMATCH, 15) if mlen else 0)
    out.append(tok)
    if len(lits) >= 15:
        n = len(lits) - 15
        while n >= 255:
            out.append(255)
            n -= 255
        out.append(n)
    out += lits
    if mlen:
        out.append(off & 0xFF)
        out.append(off >> 8)
        e = mlen - MINMATCH
        if e >= 15:
            n = e - 15
            while n >= 255:
                out.append(255)
                n -= 255
            out.append(n)


def lz4_compress(src, depth=128, tail=True):
    """Hash chains with a two-step lazy parse.

    Lazy rather than a shortest-path DP because LZ4's literal run is carried in
    the same token as the match, so the two are not separable and an exact
    parse costs far more than the fraction of a percent it buys - measured
    against the reference `lz4hc` at level 12 while this was written.

    `tail=False` is the CLASSIC block - no T word, the last sequence literals
    only - and exists for one caller: the boot blob's own decoder
    (tools/os88kz.py, boot/boot2.asm kz_expand).
    """
    src = bytes(src)
    n = len(src)
    out = bytearray(2)
    bounds = [(0, 2)]
    if n <= MFLIMIT:
        _lz4_emit(out, src, 0, 0)
        bounds.append((n, len(out)))
        return _cut(src, out, bounds) if tail else bytes(out[2:])
    ch = Chains(src, MINMATCH)
    lim = n - MFLIMIT
    mend = n - LASTLITS
    anchor = i = 0
    while i < lim:
        l, o = ch.find(i, 65535, mend - i, depth)
        if not l:
            i += 1
            continue
        # lazy: a longer match one or two bytes on beats emitting this one
        while i + 1 < lim:
            l2, o2 = ch.find(i + 1, 65535, mend - (i + 1), depth)
            if l2 > l + 1:
                i += 1
                l, o = l2, o2
                continue
            break
        if i + l > mend:                       # never consume the last 5
            l = mend - i
            if l < MINMATCH:
                i += 1
                continue
        _lz4_emit(out, src[anchor:i], l, o)
        i += l
        anchor = i
        bounds.append((i, len(out)))
    _lz4_emit(out, src[anchor:n], 0, 0)
    bounds.append((n, len(out)))
    return _cut(src, out, bounds) if tail else bytes(out[2:])


def lz4_decompress(buf, outlen, tail=True):
    out = bytearray()
    p, end = (_tail_bounds(buf) if tail else (0, len(buf)))
    while True:
        if p >= end:
            if p > end:
                raise ValueError(f"LZ4: a length ran {p - end} past the tail")
            break
        tok = buf[p]; p += 1
        ll = tok >> 4
        if ll == 15:
            while True:
                e = buf[p]; p += 1; ll += e
                if e != 255:
                    break
        out += buf[p:p + ll]; p += ll
        if p >= end:
            if tail:
                raise ValueError("LZ4: the symbols end in a literal run")
            break                       # a classic block ends in literals
        off = buf[p] | (buf[p + 1] << 8); p += 2
        if off == 0 or off > len(out):
            raise ValueError(f"LZ4: bad offset {off} at output {len(out)}")
        ml = tok & 15
        if ml == 15:
            while True:
                e = buf[p]; p += 1; ml += e
                if e != 255:
                    break
        ml += MINMATCH
        s = len(out) - off
        for k in range(ml):
            out.append(out[s + k])
    if tail:
        out += buf[end:]
    if len(out) != outlen:
        raise ValueError(f"LZ4: produced {len(out)} bytes, expected {outlen}")
    return bytes(out)


# =============================================================================
# LZB - a tag byte, byte-aligned literals, interlaced Elias gamma
# =============================================================================
LZB_MIN = 2
LZB_DENSE = 34                  # try every match length up to here exactly


def _lzb_lengths(ml):
    """the match lengths worth costing, longest first.

    `_lzb_cost` changes only when the gamma of the length grows a bit, i.e. at
    a power of two, so inside a bracket the only thing that varies is where the
    match ENDS - and the ends worth trying are the bracket's own, plus every
    short length where the brackets are dense anyway."""
    if ml <= LZB_DENSE:
        return range(ml, LZB_MIN - 1, -1)
    out = [ml]
    k = 1 << (ml.bit_length() - 1)
    while k > LZB_DENSE:
        out.append(k - 1)                    # the top of the bracket below
        out.append(k)                        # ...and the bottom of this one
        k >>= 1
    out += list(range(LZB_DENSE, LZB_MIN - 1, -1))
    return out


def _gamma_bits(v):
    return 2 * (v.bit_length() - 1)


def _lzb_cost(mlen, off):
    return 1 + _gamma_bits(mlen) + _gamma_bits((off >> 8) + 2) + 8


class _BitOut:
    """Tag BYTES, MSB first. A byte and not a word so the 8086 reader can
    carry the tag in DL behind a guard bit and spend no register on a
    counter - see the kernel's GETBIT."""

    def __init__(self):
        self.buf = bytearray()
        self.pos = None
        self.tag = 0
        self.left = 0

    def bit(self, b):
        if self.left == 0:
            self.pos = len(self.buf)
            self.buf.append(0)
            self.tag = 0
            self.left = 8
        self.left -= 1
        if b:
            self.tag |= 1 << self.left
        self.buf[self.pos] = self.tag

    def byte(self, v):
        self.buf.append(v & 0xFF)

    def gamma(self, v):                      # v >= 2
        for i in range(v.bit_length() - 2, -1, -1):
            self.bit((v >> i) & 1)
            self.bit(1 if i else 0)


def lzb_compress(src, depth=128):
    """Shortest-path parse. LZB's literal costs one tag bit wherever it sits,
    so unlike LZ4 the symbols ARE separable and an exact parse is affordable."""
    src = bytes(src)
    n = len(src)
    if n == 0:
        return b"\0\0"                 # a T word saying 0, and nothing else
    ch = Chains(src, LZB_MIN)
    INF = float("inf")
    dp = [INF] * (n + 1)
    nxt = [(0, 0)] * (n + 1)
    dp[n] = 0
    for i in range(n - 1, -1, -1):
        dp[i] = 9 + dp[i + 1]
        nxt[i] = (0, 0)
        ml, off = ch.find(i, 65535, n - i, depth)
        # A shorter match at the same offset is legal too and can win, because
        # its gamma is cheaper - so the candidates are not just the longest.
        # But walking every length is O(n * matchlen), which on a 4,066-byte
        # run of zeros (ether.drv has one) is quadratic and was seconds a file.
        # The cost is FLAT inside a gamma bracket [2^j, 2^(j+1)-1], so only the
        # bracket ends can win: try every length up to LZB_DENSE exactly, then
        # one candidate per bracket above it.
        if ml:
            for k in _lzb_lengths(ml):
                c = _lzb_cost(k, off) + dp[i + k]
                if c < dp[i]:
                    dp[i] = c
                    nxt[i] = (k, off)
    ch.insert_to(n)
    w = _BitOut()
    w.buf += b"\0\0"                     # the T word, filled in by _cut
    bounds = [(0, 2)]
    i = 0
    while i < n:
        ml, off = nxt[i]
        if ml == 0:
            w.bit(0)
            w.byte(src[i])
            i += 1
        else:
            w.bit(1)
            w.gamma(ml)
            w.gamma((off >> 8) + 2)
            w.byte(off & 0xFF)
            i += ml
        bounds.append((i, len(w.buf)))
    return _cut(src, w.buf, bounds)


# =============================================================================
# LZB as the 8088 writes it - the reference implementation for kernel/
# compress.inc (SPEC.md 20.15.1)
# =============================================================================
# `lzb_compress` above is a shortest-path parse over a chain matcher at depth
# 128, with a cost array over every position: 2n bytes of `dp` plus 2n of
# chains, 464KB for BEVERLY.MOD. The machine cannot run it and does not try -
# it runs the parse below, and the two produce DIFFERENT streams that both
# decode to the same bytes.
#
# THIS IS NOT A MODEL OF THAT PARSE, IT IS A MIRROR OF THE ASSEMBLY, statement
# for statement, and its value is that the machine's output must equal it BYTE
# FOR BYTE (tests/lzcomp.py). A model that merely agreed about the SIZE would
# not have caught either of the two bugs the first draft of the module had.
CMZ_HBITS = 12
CMZ_HSIZE = 1 << CMZ_HBITS      # head[], 8KB, hashed on three bytes
CMZ_NIL = 0xFFFF
CMZ_MIN = 3                     # the encoder's, not the format's - LZB_MIN
                                # is 2 and a two-byte match needs a two-byte
                                # hash
CMZ_DEPTH = 16                  # chain candidates tried per position
CMZ_WMAX = 16384                # ...over a window this big, 2 bytes of prev[]
CMZ_WMIN = 1024                 # each. The verb takes the largest that fits
CMZ_SLACK = 16                  # how far past the bail limit one pass can go


def lzb_compress_machine(src, window=CMZ_WMAX, depth=CMZ_DEPTH):
    """cmz_pack. Returns the packed bytes, or None for `it did not get
    smaller` - which is CF=1 out of the module and a plain store by the verb.

    `window` is the verb's dial: it claims 8KB + 2*window of scratch and drops
    the window until the claim fits (SPEC.md 22.6), so a 128KB machine runs
    the same encoder over less history rather than a different one."""
    src = bytes(src)
    n = len(src)
    if n <= CMZ_SLACK:
        return None
    lim = n - CMZ_SLACK
    mask = window - 1
    head = [CMZ_NIL] * CMZ_HSIZE
    prev = [0] * window          # NOT cleared on the machine either: a stale
                                 # entry is only reachable through a candidate
                                 # already outside the window, and the bound
                                 # test below rejects that first

    def hsh(i):
        return ((src[i] << 8) ^ (src[i + 1] << 4) ^ src[i + 2]) \
            & (CMZ_HSIZE - 1)

    def ins(i):                                          # cmz_ins
        if n - i < CMZ_MIN:
            return
        h = hsh(i)
        prev[i & mask] = head[h]
        head[h] = i

    def probe(i):                                        # cmz_probe
        if n - i < CMZ_MIN:
            return 0, 0
        most = n - i
        lo = max(0, i - mask)    # the oldest position still in the window
        best, boff = 0, 0
        cand = head[hsh(i)]
        d = depth
        while cand != CMZ_NIL and cand >= lo:
            ml = 0
            while ml < most and src[cand + ml] == src[i + ml]:
                ml += 1
            if ml > best:
                best, boff = ml, i - cand
                if best >= most:
                    break
            d -= 1
            if d == 0:
                break
            cand = prev[cand & mask]
        return (best, boff) if best >= CMZ_MIN else (0, 0)

    w = _BitOut()
    w.buf += b"\0\0"                     # the T word, patched at the end
    bounds = [(0, 2)]
    i = 0
    while True:
        # cmz_pack's cut, measured at every symbol boundary - inside a symbol
        # the decoder consumes and only then produces, so this is where
        # (produced - consumed) peaks. _cut tracks it as the assembly does
        if i >= n:
            break
        if len(w.buf) >= lim:
            return None
        ml, off = probe(i)
        if ml == 0:
            w.bit(0)
            w.byte(src[i])
            ins(i)
            i += 1
        else:
            w.bit(1)
            w.gamma(ml)
            w.gamma((off >> 8) + 2)
            w.byte(off & 0xFF)
            for k in range(i, i + ml):          # cmz_fill
                ins(k)
            i += ml
        bounds.append((i, len(w.buf)))
    z = _cut(src, w.buf, bounds)
    if len(z) >= n:
        return None
    return z


class _BitIn:
    def __init__(self, buf):
        self.buf = buf
        self.p = 0
        self.tag = 0
        self.left = 0

    def bit(self):
        if self.left == 0:
            self.tag = self.buf[self.p]
            self.p += 1
            self.left = 8
        self.left -= 1
        return (self.tag >> self.left) & 1

    def byte(self):
        v = self.buf[self.p]
        self.p += 1
        return v

    def gamma(self):
        v = 1
        while True:
            v = v * 2 + self.bit()
            if not self.bit():
                return v


def lzb_decompress(buf, outlen):
    r = _BitIn(buf)
    r.p, end = _tail_bounds(buf)
    out = bytearray()
    while True:
        if r.p >= end:
            if r.p > end:
                raise ValueError(f"LZB: a symbol ran {r.p - end} past the tail")
            break
        if r.bit() == 0:
            out.append(r.byte())
        else:
            ml = r.gamma()
            off = ((r.gamma() - 2) << 8) | r.byte()
            if off == 0 or off > len(out):
                raise ValueError(f"LZB: bad offset {off} at output {len(out)}")
            s = len(out) - off
            for k in range(ml):
                out.append(out[s + k])
    out += buf[end:]
    if len(out) != outlen:
        raise ValueError(f"LZB: produced {len(out)} bytes, expected {outlen}")
    return bytes(out)


# =============================================================================
# the two entry points everything else uses
# =============================================================================
def compress(data, fmt=LZ4, tail=True):
    return lz4_compress(data, tail=tail) if fmt == LZ4 else lzb_compress(data)


def decompress(data, fmt, outlen, tail=True):
    if fmt == LZ4:
        return lz4_decompress(data, outlen, tail=tail)
    return lzb_decompress(data, outlen)


def in_place_margin(data, fmt=LZ4, packed=None, tail=True):
    """Bytes a region must have ABOVE the image so the decoder, reading its
    own compressed source from the top of that region, never overtakes its
    write pointer (plan 7.2). Measured rather than bounded: this simulates the
    decode and reports the worst point.

    **FOR A STREAM WITH A TAIL THE ANSWER IS ZERO BY CONSTRUCTION** (SPEC.md
    20.13.7), and this is what asserts it: tests/unit/t_lzfmt.py and the
    packagers call it on every stream they write. The measurement still means
    something for the classic block the boot blob decodes (`tail=False`),
    where the margin is real and tools/os88kz.py reserves it.

    `packed` is an ALREADY-compressed stream of the same bytes - the machine's
    (`lzb_compress_machine`), when the question is about what the 8088 wrote
    rather than about what this module would have written."""
    z = compress(data, fmt, tail=tail) if packed is None else packed
    n, zl = len(data), len(z)
    if fmt == LZ4:
        p, end = (_tail_bounds(z) if tail else (0, zl))
        prod = 0
        worst = prod - p        # the T word is consumed before anything is
                                # produced, so the first boundary's lead is
                                # -2 and not 0; a stream that is ALL tail
                                # (noise) has that as its peak
        while p < end:
            tok = z[p]; p += 1
            ll = tok >> 4
            if ll == 15:
                while True:
                    e = z[p]; p += 1; ll += e
                    if e != 255:
                        break
            p += ll; prod += ll
            worst = max(worst, prod - p)
            if p >= end:
                break
            p += 2
            ml = tok & 15
            if ml == 15:
                while True:
                    e = z[p]; p += 1; ml += e
                    if e != 255:
                        break
            prod += ml + MINMATCH
            worst = max(worst, prod - p)
    else:
        r = _BitIn(z)
        r.p, end = _tail_bounds(z)
        prod = 0
        worst = prod - r.p
        while r.p < end:
            if r.bit() == 0:
                r.byte(); prod += 1
            else:
                ml = r.gamma(); r.gamma(); r.byte(); prod += ml
            worst = max(worst, prod - r.p)
    return max(0, worst - (n - zl))


# =============================================================================
# The 'CZ' header - a compressed file that is NOT a package (SPEC.md 20.14,
# docs/plans/O88-COMPRESSION-PLAN.md 12.2). Eight bytes in front of the stream:
#
#     +0  2  magic 'CZ'
#     +2  1  format: 0 = LZ4, 1 = LZB
#     +3  1  flags, 0
#     +4  4  the UNPACKED size
#
# The kernel does not find it by reading the file: the mount would have to open
# every file on the disk to do that. It finds it in four bytes of the FAT
# DIRECTORY ENTRY that os8088 already zeroes and never reads (12) - so knowing
# costs no I/O at all. **That hint is a CACHE and this header is the
# AUTHORITY**: a foreign tool may drop those bytes, and the read path checks
# the magic here before it believes anything.
CZ_MAGIC = b"CZ"
CZ_HDR = 8
CZ_SRCMAX = 0xFFFF             # the source's own ceiling: the decoder's
                                # output crosses 64KB and its input does not


def cz_wrap(data, fmt=LZ4):
    """compress `data` into a 'CZ' file, or return it unchanged if that would
    not be smaller - a file that grew is a file the reader pays to expand for
    nothing.

    THERE IS NO SIZE RULE ANY MORE. The kernel expands a 'CZ' file in place
    inside a buffer of exactly the unpacked size (SPEC.md 20.14.2), because
    the stream's own tail is what makes that safe (20.13.7) - so a file that
    lands one byte under a kilobyte boundary compresses like any other, and
    the reader that was told U needs U. The old margin rule refused one size
    in sixteen and had the manual EDITED to fit.
    """
    try:
        z = compress(data, fmt)
    except ValueError:          # a tail T cannot count: over 64KB packed
        return data, False
    if CZ_HDR + len(z) >= len(data):
        return data, False
    if CZ_HDR + len(z) > CZ_SRCMAX:
        return data, False      # lz_decomp_x reads its source inside ONE
                                # segment (SPEC.md 20.14.5). A packed file
                                # this big is one that compressed badly, and
                                # storing it plain is the right answer to that
                                # rather than a limitation worked around
    if decompress(z, fmt, len(data)) != data:
        raise ValueError("round trip failed - os88lz is the reference")
    if in_place_margin(data, fmt, packed=z):
        raise ValueError("a stream with a tail needs no margin - os88lz is "
                         "the reference and its cut is wrong")
    return (CZ_MAGIC + bytes([fmt, 0])
            + len(data).to_bytes(4, "little") + z), True


def cz_parse(blob):
    """(format, unpacked size) for a 'CZ' file, or None."""
    if len(blob) < CZ_HDR or blob[:2] != CZ_MAGIC:
        return None
    if blob[2] not in (LZ4, LZB) or blob[3] != 0:
        return None
    return blob[2], int.from_bytes(blob[4:8], "little")


def cz_unwrap(blob):
    p = cz_parse(blob)
    if not p:
        return blob
    fmt, n = p
    return decompress(blob[CZ_HDR:], fmt, n)


# =============================================================================
def _selfcheck(paths):
    import glob, os
    files = []
    for p in paths or ["build/*.o88", "build/*.drv", "build/kernel.bin"]:
        files += sorted(glob.glob(p))
    if not files:
        print("os88lz: nothing to check (build the tree first)")
        return 0
    bad = 0
    tot = {LZ4: 0, LZB: 0}
    raw = 0
    marg = 0
    for f in files:
        d = open(f, "rb").read()
        raw += len(d)
        for fmt in (LZ4, LZB):
            z = compress(d, fmt)
            tot[fmt] += len(z)
            try:
                if decompress(z, fmt, len(d)) != d:
                    print(f"os88lz: ROUND TRIP FAILED {f} {NAMES[fmt]}")
                    bad += 1
            except Exception as e:
                print(f"os88lz: {NAMES[fmt]} {f}: {e}")
                bad += 1
        for fmt in (LZ4, LZB):
            marg = max(marg, in_place_margin(d, fmt))
    if marg:
        print(f"os88lz: A STREAM NEEDED {marg} BYTES OF IN-PLACE MARGIN - the "
              "tail (SPEC.md 20.13.7) is meant to make that zero")
        bad += 1
    print(f"os88lz: {len(files)} files, {raw:,} bytes -> "
          f"LZ4 {tot[LZ4]:,} ({tot[LZ4]/raw:.1%}), "
          f"LZB {tot[LZB]:,} ({tot[LZB]/raw:.1%}); "
          f"in-place margin {marg}; "
          + ("ALL ROUND TRIPS OK" if not bad else f"{bad} FAILURE(S)"))
    return 1 if bad else 0


def main():
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--wrap", metavar="OUT",
                    help="compress ONE file into a 'CZ' file (SPEC.md 20.14) "
                         "- what the file manager's Compress verb will do on "
                         "the machine, and what builds a fixture for it now")
    ap.add_argument("--fmt", default="lz4", choices=("lz4", "lzb"))
    ap.add_argument("--selfcheck", action="store_true",
                    help="round-trip both formats over the tree's binaries")
    ap.add_argument("files", nargs="*", help="files to check (default: build/)")
    a = ap.parse_args()
    if a.wrap:
        if len(a.files) != 1:
            sys.exit("os88lz: --wrap takes exactly one input file")
        src = open(a.files[0], "rb").read()
        out, did = cz_wrap(src, LZ4 if a.fmt == "lz4" else LZB)
        open(a.wrap, "wb").write(out)
        print(f"os88lz: {a.files[0]} {len(src)} -> {len(out)} bytes "
              + (f"({len(out)/len(src):.1%}, {NAMES[out[2]]})" if did
                 else "(UNCHANGED - compressing it made it bigger)"))
        return 0
    if a.selfcheck or not a.files:
        return _selfcheck(a.files)
    for f in a.files:
        d = open(f, "rb").read()
        print(f"{f}: {len(d)} -> LZ4 {len(compress(d, LZ4))} "
              f"LZB {len(compress(d, LZB))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
