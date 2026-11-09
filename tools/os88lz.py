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
from collections import deque

# --- format ids, as stored in a header's format field ------------------------
LZ4, LZB = 0, 1
NAMES = {LZ4: "lz4", LZB: "lzb"}

MINMATCH = 4                    # LZ4: a match is at least 4 bytes
MFLIMIT  = 12                   # ...and none may START in the last 12
LASTLITS = 5                    # ...and a block ends in at least 5 literals
LZ_MAXLEN = 0xFFFF              # kernel/lz.inc counts a length - a match, or an
                                # LZ4 literal run - in ONE 16-bit register, so
                                # both encoders cap at it and both decoders
                                # REFUSE past it, as the kernel's would: a
                                # host decoder that accepted what the machine
                                # cannot read is how a 64KB run of one byte
                                # packed into a stream only the host could
                                # expand (SPEC.md 20.14.5.3)


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

    def find(self, pos, maxoff, maxlen, depth, seed=0, seed_off=0):
        """longest match at pos: (length, offset), or (0, 0).

        `seed` is a match already known to be there - the optimal parse hands
        in the previous position's match one byte shorter, at the same offset
        - so a candidate has to BEAT it, and the cheap reject below throws out
        nearly every one with a single compare."""
        src = self.src
        limit = min(maxlen, len(src) - pos)
        if limit < self.minlen:
            return 0, 0
        self.insert_to(pos)
        best_l, best_o = min(seed, limit), seed_off
        if best_l >= limit:
            return best_l, best_o
        lo = pos - maxoff
        c = self.prev[pos] - 1
        k = 0
        while c >= 0 and c > lo and k < depth:
            k += 1
            if src[c + best_l] == src[pos + best_l]:      # cheap reject
                # the common prefix, 16 bytes a slice compare and then a
                # byte at a time - the same `l` the byte loop alone finds,
                # at a fraction of the interpreter's cost on a long match
                l = 0
                while l + 16 <= limit and \
                        src[c + l:c + l + 16] == src[pos + l:pos + l + 16]:
                    l += 16
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
    if len(tail) >= LZ_ESC:
        # a tail the T word cannot count: the stream is over 64KB whatever
        # else is in it, which is a file that compressed badly and is stored
        # plain (cz_wrap, SPEC.md 20.14.5) - so this is a refusal, not a
        # stream. 0xFFFF exactly is refused too: it is the filter's escape
        # (SPEC.md 20.13.9.4), as kernel/compress.inc's cmz_pack refuses it
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
    if len(lits) > LZ_MAXLEN:
        # kernel/lz.inc counts a length in ONE register, so a literal run
        # longer than a word cannot be said to it. Nothing but a match can
        # break a run, so this is a refusal - cz_wrap stores the file plain
        raise ValueError(f"a literal run of {len(lits)} bytes does not fit "
                         "the decoder's 16-bit length")
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


LZ4_SKIP = 256                  # a match this long is followed, not searched
                                # inside: the next position's longest match is
                                # taken to be this one a byte shorter


def lz4_compress(src, depth=512, tail=True, parse="opt", prefix=0):
    """An OPTIMAL parse over hash chains - the cheapest LZ4 stream for the
    matches the chains find, priced in the format's real bytes.

    LZ4 carries a literal run and the match after it in ONE token, and a run
    of 15 or more costs extension bytes (one more every 255), so the price of
    a literal depends on how long its run already is and a forward
    shortest-path over positions is not exact. A BACKWARD one over SEQUENCE
    STARTS is: F[i], the cheapest encoding of src[i:] when a sequence begins
    at i, is the cheaper of "all literals to the end" and

        1 + k + litext(k) + G[i + k]          over every run length k >= 0

    where G[j] is a match at j plus F after it. litext(k) is 0 below 15 and
    1 + (k - 15) // 255 above, so k < 15 is fifteen lookups and the rest is
    one quantity per position computed off the one 255 places on - a sliding
    window minimum and a recurrence, which makes it exact for every run
    length at O(1) a position. Every match length from 4 to the longest at
    that offset is legal and costs the same two offset bytes, so G[j] tries
    lengths 4..32 exactly and, past that, the longest and the last length of
    each 255-step extension tier.

    The two places it is not exact are the MATCH FINDER's, and both are for
    time: each position's search is seeded with the previous position's match
    a byte shorter (so a candidate must beat it, and the cheap reject throws
    nearly all of them out), and inside a match longer than LZ4_SKIP the
    search is not made at all - which took GBDEMO.GB, 32KB of mostly one
    pattern, from 3.0 s to 0.2 s for 0 bytes and BEVERLY.MOD's 1.8 s to 1.3.

    It replaced a two-step lazy parse whose docstring said an exact parse
    "costs far more than the fraction of a percent it buys": it buys 0.3-0.8%
    on code and ~1.3% on BEVERLY.MOD (docs/reports/DOS-PACKED-COST-2026-11-01.md
    section 4 row 3) - 9,794 bytes over a clean `make`'s 220 streams - for
    ~11 s more encoder CPU spread over make's jobs. SAME FORMAT, same
    raw tail (SPEC.md 20.13.7, `_cut`), same MFLIMIT and LASTLITS rules, so
    every decoder in the tree reads it unchanged. Deterministic: no hashing
    of anything but the input, ties broken by position.

    `tail=False` is the CLASSIC block - no T word, the last sequence literals
    only - and exists for one caller: the boot blob's own decoder
    (tools/os88kz.py, boot/boot2.asm kz_expand). `parse="lazy"` is the old
    encoder, for an A/B.

    `prefix` is a DICTIONARY (SPEC.md 2.9.13.6): the first `prefix` bytes of
    `src` are already in the decoder's output when the block starts and are
    not encoded - the stream says `src[prefix:]`, and its matches may reach
    back into the prefix as into anything else already produced. One caller
    again, the boot blob's (tools/os88kz.py), and only with `tail=False`:
    kernel/lz.inc and the packagers start every stream on an empty window.
    """
    if prefix:
        if tail or parse != "opt":
            raise ValueError("a dictionary is the classic block's alone "
                             "(tail=False, the optimal parse)")
        if not 0 < prefix <= len(src):
            raise ValueError(f"a prefix of {prefix} in {len(src)} bytes")
    if parse == "lazy":
        return _lz4_lazy(src, depth, tail)
    src = bytes(src)
    n = len(src)
    if n - prefix <= MFLIMIT:                    # one literal sequence either way
        if not prefix:
            return _lz4_lazy(src, depth, tail)
        out = bytearray()
        _lz4_emit(out, src[prefix:], 0, 0)
        return bytes(out)
    ch = Chains(src, MINMATCH)
    lim = n - MFLIMIT
    mend = n - LASTLITS
    INF = 1 << 60
    # the longest match at every legal start, each seeded by the one before.
    # Chains inserts every position in order, the dictionary's included, so a
    # search from `prefix` on finds candidates inside it too
    ml = [0] * n
    mo = [0] * n
    pl = po = 0
    for i in range(prefix, lim):
        mx = min(mend - i, LZ_MAXLEN)
        if pl > LZ4_SKIP:
            pl, po = min(pl - 1, mx), po
        else:
            pl, po = ch.find(i, 65535, mx, depth,
                             pl - 1 if pl > MINMATCH else 0, po)
        ml[i] = pl
        mo[i] = po
    F = [0] * (n + 1)           # cheapest from a sequence starting at i
    D = [-1] * (n + 1)          # ...whose match starts at D[i]; -1 = literals
    G = [INF] * (n + 1)         # a match at j, and everything after it
    GL = [0] * (n + 1)          # ...of this length
    H = [INF] * (n + 256)       # G[j] + j
    Q = [INF] * (n + 256)       # min over j >= x of H[j] + (j - x) // 255
    QA = [-1] * (n + 256)       # ...at this j
    win = deque()               # sliding minimum of H over [x, x + 254]
    F[n] = 1                    # the empty last-literals token
    for i in range(n - 1, prefix - 1, -1):
        L = ml[i] if i < lim else 0
        if L:
            top = L if L < 33 else 32
            best, bl = INF, 0
            for k in range(MINMATCH, top + 1):
                c = F[i + k] if k < 19 else F[i + k] + 1
                if c <= best:
                    best, bl = c, k
            if top < L:
                t, e = 273, 1
                while t < L:
                    c = F[i + t] + e
                    if c <= best:
                        best, bl = c, t
                    t += 255
                    e += 1
                c = F[i + L] + 1 + (L - 19) // 255
                if c <= best:
                    best, bl = c, L
            G[i] = best + 2
            GL[i] = bl
            H[i] = best + 2 + i
        x = i + 15
        if x <= n:
            hx = H[x]
            while win and H[win[0]] >= hx:
                win.popleft()
            win.appendleft(x)
            while win[-1] > x + 254:
                win.pop()
            wj = win[-1]
            if Q[x + 255] + 1 < H[wj]:
                Q[x], QA[x] = Q[x + 255] + 1, QA[x + 255]
            else:
                Q[x], QA[x] = H[wj], wj
        rem = n - i
        best = 1 + rem + (0 if rem < 15 else 1 + (rem - 15) // 255)
        bd = -1
        for j in range(i, min(i + 15, lim)):
            c = 1 + (j - i) + G[j]
            if c < best:
                best, bd = c, j
        if x <= n and QA[x] >= 0 and 2 - i + Q[x] < best:
            best, bd = 2 - i + Q[x], QA[x]
        F[i] = best
        D[i] = bd
    out = bytearray(2)
    bounds = [(0, 2)]
    i = prefix
    while D[i] >= 0:
        j = D[i]
        _lz4_emit(out, src[i:j], GL[j], mo[j])
        i = j + GL[j]
        bounds.append((i, len(out)))
    _lz4_emit(out, src[i:n], 0, 0)
    bounds.append((n, len(out)))
    return _cut(src, out, bounds) if tail else bytes(out[2:])


def _lz4_lazy(src, depth=128, tail=True):
    """Hash chains with a two-step lazy parse - what the build shipped until
    the optimal parse below replaced it, kept as `parse="lazy"` for the A/B
    and for a caller that wants speed over the last 0.4%."""
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
        l, o = ch.find(i, 65535, min(mend - i, LZ_MAXLEN), depth)
        if not l:
            i += 1
            continue
        # lazy: a longer match one or two bytes on beats emitting this one
        while i + 1 < lim:
            l2, o2 = ch.find(i + 1, 65535, min(mend - (i + 1), LZ_MAXLEN),
                             depth)
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


def lz4_decompress(buf, outlen, tail=True, prefix=b""):
    """`prefix`: the dictionary lz4_compress(prefix=) was given, as bytes -
    already in the output when the stream starts, and not part of what is
    returned."""
    if prefix and tail:
        raise ValueError("a dictionary is the classic block's alone")
    out = bytearray(prefix)
    base = len(out)
    outlen += base
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
        if ll > LZ_MAXLEN:
            raise ValueError(f"LZ4: a literal run of {ll} - past a word")
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
        if ml > LZ_MAXLEN:
            raise ValueError(f"LZ4: a match of {ml} - past a word")
        s = len(out) - off
        for k in range(ml):
            out.append(out[s + k])
    if tail:
        out += buf[end:]
    if len(out) != outlen:
        raise ValueError(f"LZ4: produced {len(out) - base} bytes, expected "
                         f"{outlen - base}")
    return bytes(out[base:])


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
        ml, off = ch.find(i, 65535, min(n - i, LZ_MAXLEN), depth)
        # A shorter match at the same offset is legal too and can win, because
        # its gamma is cheaper - so the candidates are not just the longest.
        # But walking every length is O(n * matchlen), which on a 4,066-byte
        # run of zeros (ether.drv has one) is quadratic and was seconds a file.
        # The cost is FLAT inside a gamma bracket [2^j, 2^(j+1)-1], so only the
        # bracket ends can win: try every length up to LZB_DENSE exactly, then
        # one candidate per bracket above it.
        if ml:
            # _lzb_cost(k, off) with the offset's part hoisted out of the loop
            base = 9 + _gamma_bits((off >> 8) + 2) - 2
            best = dp[i]
            for k in _lzb_lengths(ml):
                c = base + 2 * k.bit_length() + dp[i + k]
                if c < best:
                    best = c
                    nxt[i] = (k, off)
            dp[i] = best
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
CMZ_MAXM = 0x3FF0               # the longest match cmz_pack emits: the FORMAT
                                # has no limit, the encoder's slide does
                                # (SPEC.md 20.15.4) - every byte a probe or a
                                # hash reads stays inside the 48KB window a
                                # streamed Compress holds (22.22.5)


def lzb_compress_machine(src, window=CMZ_WMAX, depth=CMZ_DEPTH):
    """cmz_pack. Returns the packed bytes, or None for `it did not get
    smaller` - which is CF=1 out of the module and a plain store by the verb.

    `window` is the verb's dial: it claims 8KB + 2*window of scratch and drops
    the window until the claim fits (SPEC.md 22.6), so a 128KB machine runs
    the same encoder over less history rather than a different one.

    ANY LENGTH (SPEC.md 20.15.4). The machine slides its source and output
    segments forward as it goes, which changes no decision the parse makes,
    so none of that is modelled here; the one thing it does change - no match
    longer than CMZ_MAXM - is. A raw tail over 64KB raises ValueError from
    _cut, which is the machine's `Its end won't compress`."""
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
        most = min(n - i, CMZ_MAXM)
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
            if ml > LZ_MAXLEN:
                raise ValueError(f"LZB: a match of {ml} - past a word")
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
# the near-call filter (SPEC.md 20.13.9) - a property of the STREAM, not of
# either format, so it is kept out of both encoders and wraps them instead
# =============================================================================
LZ_ESC = 0xFFFF                 # a T word that says "filtered; the real T
                                # follows". kernel/lz.inc's LZ_ESC, and an
                                # older kernel REFUSES it: a tail of 0xFFFF
                                # is longer than any stream under 64KB
LZ_E8 = 0xE8                    # `call rel16`, the one opcode rewritten
FILT_MAX = 0xFFFF               # the filter is defined over a stream offset
                                # mod 64KB, and the decoder runs it over
                                # ES:0..DI, so an output past a segment is
                                # never filtered


def bcj_encode(src):
    """The filter: every E8 at a position p with two bytes after it has its
    word operand made ABSOLUTE, `rel + p + 3` mod 64KB, and the operand is
    stepped over. Two calls to one routine are then the same three bytes,
    which is a match LZ4 can take and a `call` at its own site never was.
    A data byte that happens to be E8 is rewritten too, and back: the walk
    visits the same positions both ways because it never looks at a byte it
    rewrote, so this is a bijection on any input (docs/reports/
    DOS-PACKED-COST-2026-11-01.md section 5)."""
    s = bytearray(src)
    i, n = 0, len(s)
    while i + 3 <= n:
        if s[i] == LZ_E8:
            v = (s[i + 1] | s[i + 2] << 8) + i + 3
            s[i + 1] = v & 0xFF
            s[i + 2] = (v >> 8) & 0xFF
            i += 3
        else:
            i += 1
    return bytes(s)


def bcj_decode(src):
    """bcj_encode's inverse - kernel/lz.inc's `repne scasb` loop, byte for
    byte: `p + 3` comes back off every operand it put one on."""
    s = bytearray(src)
    i, n = 0, len(s)
    while i + 3 <= n:
        if s[i] == LZ_E8:
            v = (s[i + 1] | s[i + 2] << 8) - (i + 3)
            s[i + 1] = v & 0xFF
            s[i + 2] = (v >> 8) & 0xFF
            i += 3
        else:
            i += 1
    return bytes(s)


def is_filtered(stream, outlen=None):
    """True when a stream (with its T word, SPEC.md 20.13.7) carries the
    filter's escape. Given `outlen`, only an output the filter can cover
    counts: a T of 0xFFFF in front of more than that is a real tail an
    earlier encoder wrote (SPEC.md 20.13.9.4, kernel/lz.inc's BX test)."""
    if outlen is not None and outlen > FILT_MAX:
        return False
    return len(stream) >= 2 and int.from_bytes(stream[:2], "little") == LZ_ESC


# =============================================================================
# the two entry points everything else uses
# =============================================================================
def compress(data, fmt=LZ4, tail=True, filt=False):
    """`filt`: also try the near-call filter and keep whichever stream is
    SHORTER - per file, so data never takes it (SPEC.md 20.13.9). Only a
    stream with a tail can carry the escape, and only an output the decoder
    can address from one segment can be filtered."""
    z = lz4_compress(data, tail=tail) if fmt == LZ4 else lzb_compress(data)
    if filt and tail and 3 <= len(data) <= FILT_MAX:
        f = bcj_encode(data)
        if f != bytes(data):
            try:
                zf = compress(f, fmt)
            except ValueError:  # all tail at 0xFFFF (_cut): longer than z
                zf = z
            if len(zf) + 2 < len(z):
                return LZ_ESC.to_bytes(2, "little") + zf
    return z


def decompress(data, fmt, outlen, tail=True):
    if tail and is_filtered(data, outlen):
        return bcj_decode(decompress(data[2:], fmt, outlen))
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
    esc = 2 if tail and is_filtered(z, n) else 0   # the escape is consumed
                                    # before anything is produced, like T
    if fmt == LZ4:
        p, end = (_tail_bounds(z[esc:]) if tail else (0, zl))
        p, end = p + esc, end + esc
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
        r.p, end = _tail_bounds(z[esc:])
        r.p, end = r.p + esc, end + esc
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
CZ_HDR = 8                      # ...and NO ceiling on the packed form in
                                # either format: the decoder's input crosses a
                                # segment for LZB (SPEC.md 20.14.5.1) and LZ4
                                # (20.14.5.2) alike


def cz_wrap(data, fmt=LZ4, packed=None, filt=False):
    """compress `data` into a 'CZ' file, or return it unchanged if that would
    not be smaller - a file that grew is a file the reader pays to expand for
    nothing.

    THERE IS NO SIZE RULE ANY MORE. The kernel expands a 'CZ' file in place
    inside a buffer of exactly the unpacked size (SPEC.md 20.14.2), because
    the stream's own tail is what makes that safe (20.13.7) - so a file that
    lands one byte under a kilobyte boundary compresses like any other, and
    the reader that was told U needs U. The old margin rule refused one size
    in sixteen and had the manual EDITED to fit.

    `packed` is `data` already compressed some other way - the MACHINE's
    parse (`lzb_compress_machine`), which tools/os88cz.py hands in because
    this module's own LZB parse is exact and takes minutes on anything big
    and repetitive. None: compress here, as the build does.

    `filt`: CODE - a driver, a module - may take the near-call filter
    (SPEC.md 20.13.9) when it makes the file shorter. A data file is wrapped
    without even asking.
    """
    try:
        z = compress(data, fmt, filt=filt) if packed is None else packed
    except ValueError:          # a tail T cannot count, or an LZ4 literal
        return data, False      # run the decoder cannot (20.14.5.3)
    if CZ_HDR + len(z) >= len(data):
        return data, False
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
        # the FILE is not the image any more (SPEC.md 20.13): a shipped
        # package or driver is already packed, and packing a packed stream
        # again round-trips nothing the build does - and refuses outright
        # once the packed file is over 64KB (SHEET.O88 with its parts)
        if f.endswith(".o88"):
            import os88pkg
            d = os88pkg.image_unwrap(d)
        elif f.endswith(".drv"):
            d = cz_unwrap(d)
        raw += len(d)
        if bcj_decode(bcj_encode(d)) != d:
            print(f"os88lz: THE FILTER DOES NOT INVERT on {f}")
            bad += 1
        zs = {}
        for fmt in (LZ4, LZB):
            z = zs[fmt] = compress(d, fmt, filt=True)   # what the build packs
            tot[fmt] += len(z)
            try:
                if decompress(z, fmt, len(d)) != d:
                    print(f"os88lz: ROUND TRIP FAILED {f} {NAMES[fmt]}")
                    bad += 1
            except Exception as e:
                print(f"os88lz: {NAMES[fmt]} {f}: {e}")
                bad += 1
        for fmt in (LZ4, LZB):          # the streams just checked, not a
            marg = max(marg, in_place_margin(d, fmt, packed=zs[fmt]))
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
    ap.add_argument("--filter", action="store_true",
                    help="the file is CODE: with --wrap, take the near-call "
                         "filter when it makes the file shorter (SPEC.md "
                         "20.13.9). A data file is wrapped without it")
    ap.add_argument("--selfcheck", action="store_true",
                    help="round-trip both formats over the tree's binaries")
    ap.add_argument("files", nargs="*", help="files to check (default: build/)")
    a = ap.parse_args()
    if a.wrap:
        if len(a.files) != 1:
            sys.exit("os88lz: --wrap takes exactly one input file")
        src = open(a.files[0], "rb").read()
        out, did = cz_wrap(src, LZ4 if a.fmt == "lz4" else LZB,
                           filt=a.filter)
        open(a.wrap, "wb").write(out)
        print(f"os88lz: {a.files[0]} {len(src)} -> {len(out)} bytes "
              + (f"({len(out)/len(src):.1%}, {NAMES[out[2]]}"
                 f"{'+E8' if is_filtered(out[CZ_HDR:]) else ''})" if did
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
