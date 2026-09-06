#!/usr/bin/env python3
"""os88sfx: pack a DOS .COM into the payload of a SELF-EXTRACTING one.

    python3 tools/os88sfx.py build/os88net.raw -o build/os88net.lz \\
                             --inc build/os88net.lzi

The sibling of tools/os88pkg.py and tools/os88drv.py in the one way that
matters -- it is a host-side step in the pipeline that produces a shipped
byte -- and their opposite in another: those two VALIDATE a binary nasm
already emitted, and this one REWRITES it. What comes out is not a program.
It is the compressed body that drivers/net/os88sfx.asm carries, and only
that stub knows how to turn it back into one.

WHY. OS88NET.COM is the DOS end of SPEC.md 62's parallel link, and it rides
the apps floppy for TRANSPORT only -- it is the one file on either disk that
does not run on os8088 at all (see APPS_DOS in the Makefile). The apps disk
is full, and 19,333 bytes of a 360KB floppy's 354 clusters is a lot to spend
on a file the machine cannot execute. So it ships PACKED and unpacks itself
on the DOS machine, which is the only machine that was ever going to run it.

THE FORMAT is LZSS with Elias-gamma lengths and offsets, in one stream that
interleaves bits with whole bytes:

    0 <byte>                        a literal
    1 <gamma(len-1)> <gamma(offhi+1)> <byte offlo>
                                    copy `len` bytes from `off` back, where
                                    len >= 2 and off = (offhi << 8) + offlo + 1

Bits come from a tag byte fetched LAZILY -- the decoder loads one only when
it has run out, so a tag byte sits in the stream exactly where the eight bits
above it were first needed, and every literal and offset byte is read whole
and byte-aligned with a plain lodsb. That is what keeps the decoder to the
~90 bytes in os88sfx.asm: there is no such thing here as reading 13 bits.

Gamma is INTERLACED, so the decoder is a four-instruction loop and needs no
bit counter:

    ax = 1;  while (getbit()) ax = ax*2 + getbit();

WHY NOT DEFLATE. Measured on the file this exists for: zlib -9 gets 11,467
bytes and this gets 11,569 -- 102 bytes, 0.9%, apart. An inflate for the 8086
is a Huffman decoder, its two dynamic tables and a window, which is over a
kilobyte of stub; it would have to beat this by 1KB before it broke even, and
it beats it by 102 bytes. Plain LZSS with a 12-bit offset and a 4-bit length
was the other candidate and is 13,510 -- 1,941 bytes worse, for a stub maybe
30 bytes smaller.

THE PARSE IS OPTIMAL, not greedy. Every cost above is a fixed number of bits
that depends only on (len, off), so the cheapest encoding is a shortest path
over the byte positions and a plain DP finds it. Greedy costs ~3% here.

DETERMINISM. The whole toolchain is reproducible on purpose (tools/os88disk.py
pins the volume serial and every FAT timestamp), so this must be too: the
match search takes the SMALLEST offset at each length, the DP breaks ties
towards the shorter encoding it reached first, and nothing consults a hash
seed, a dict ordering or a clock.

THE SELF-CHECK IS NOT OPTIONAL. Before it writes anything this decodes what
it just produced -- with decode(), which is a line-for-line model of the
8086 in os88sfx.asm rather than a second call into the encoder's own tables --
and refuses to emit a payload that does not come back byte-for-byte. A packer
that can silently emit a corrupt stream produces a floppy that fails on a
machine in somebody else's house, which is precisely the class of bug the
dosstub harness was written to end.
"""
import argparse
import sys

# The .COM load address. Everything the stub computes hangs off it: the image
# decodes to SFX_ORG and the packed body is staged at SFX_ORG + rawsize.
SFX_ORG = 0x100

# A .COM is ONE 64KB segment with DOS's stack at the top of it, and this is
# os88net.asm's own ceiling for the same reason (see the %error beside
# net_bss1). The stub asserts it again in its own terms; this is here so that
# the failure is a sentence from the packer rather than a nasm expression.
SEG_CEILING = 0xF000

MINLEN = 2                       # a 2-byte match is 11 bits against a
                                 # literal pair's 18, so it pays whenever the
                                 # offset is under 257 -- and the DP works out
                                 # where that is rather than a rule of thumb
MAXLEN = 2048                    # nothing in a 19KB .COM repeats for longer,
                                 # and it bounds the match scan's inner loop


def fail(msg: str) -> None:
    print(f"os88sfx: error: {msg}", file=sys.stderr)
    sys.exit(1)


def gamma_bits(v: int) -> int:
    """Bits an interlaced Elias-gamma code costs for v >= 1."""
    return 2 * (v.bit_length() - 1) + 1


# =============================================================================
# The match search
# =============================================================================
def find_matches(data: bytes):
    """For every position, {length: smallest offset that achieves it}.

    rfind gives the LAST occurrence, which is the smallest offset, which is
    the cheapest -- the cost of a match never decreases with distance, so the
    nearest source is optimal at every length and there is nothing to weigh.

    The `i + l - 1` end bound is what allows an OVERLAPPING match: it lets the
    source start anywhere up to i-1, so a source run may reach forward into
    the bytes this match is itself producing. That is exactly what the
    decoder's byte-at-a-time copy does, so an RLE run costs one match.
    """
    n = len(data)
    out = []
    for i in range(n):
        d = {}
        l = MINLEN
        while l <= MAXLEN and i + l <= n:
            p = data.rfind(data[i:i + l], 0, i + l - 1)
            if p < 0:
                break
            d[l] = i - p
            l += 1
        out.append(d)
    return out


def match_bits(l: int, off: int):
    """The exact cost of one match token, or None if it cannot be encoded."""
    if l < MINLEN:
        return None
    offhi = (off - 1) >> 8
    if offhi > 255:              # the stub builds the offset as ah:al, so the
        return None              # high half is one byte and this is its range
    return 1 + gamma_bits(l - 1) + gamma_bits(offhi + 1) + 8


LIT_BITS = 1 + 8


def parse(data: bytes):
    """Shortest-path parse. Returns a list of ('lit', byte) / ('m', l, off)."""
    n = len(data)
    m = find_matches(data)
    INF = float("inf")
    cost = [INF] * (n + 1)
    link = [None] * (n + 1)
    cost[0] = 0
    for i in range(n):
        ci = cost[i]
        if ci == INF:
            continue
        if ci + LIT_BITS < cost[i + 1]:
            cost[i + 1] = ci + LIT_BITS
            link[i + 1] = (i, None)
        for l, off in m[i].items():          # ascending l: deterministic
            b = match_bits(l, off)
            if b is None:
                continue
            if ci + b < cost[i + l]:
                cost[i + l] = ci + b
                link[i + l] = (i, (l, off))
    if cost[n] == INF:
        fail("no parse - this cannot happen while literals are always legal")

    ops = []
    j = n
    while j > 0:
        i, mt = link[j]
        ops.append(("lit", data[i]) if mt is None else ("m", mt[0], mt[1]))
        j = i
    ops.reverse()
    return ops


# =============================================================================
# The encoder - and it must mirror the DECODER's fetch order, not its own
# =============================================================================
class Emitter:
    """A byte stream with a lazily-allocated tag byte, exactly as read back.

    The decoder pulls a tag byte only when its bit buffer runs dry, so a tag
    byte belongs in the stream at the point the FIRST of its eight bits was
    needed -- ahead of the literal or offset byte that same token goes on to
    read. Appending bits and bytes to one buffer in token order reproduces
    that placement for free; the tag byte's later bits are written back into
    a position the stream has already moved past, which is correct, because
    by then the decoder is holding them in BX.
    """

    def __init__(self):
        self.buf = bytearray()
        self.tag = None          # index of the tag byte being filled
        self.nbits = 8           # bits used in it; 8 forces the first alloc

    def bit(self, b: int) -> None:
        if self.nbits == 8:
            self.buf.append(0)
            self.tag = len(self.buf) - 1
            self.nbits = 0
        if b:
            self.buf[self.tag] |= 1 << self.nbits   # LSB first: the stub's
        self.nbits += 1                             # `shr bx,1` takes bit 0

    def byte(self, v: int) -> None:
        self.buf.append(v)

    def gamma(self, v: int) -> None:
        """Interlaced Elias gamma for v >= 1: a continuation bit, then a data
        bit, repeated -- which is what makes the stub's loop a `jnc` out of
        the top and an `adc ax, ax` at the bottom."""
        assert v >= 1
        for shift in range(v.bit_length() - 2, -1, -1):
            self.bit(1)
            self.bit((v >> shift) & 1)
        self.bit(0)


def encode(ops) -> bytes:
    e = Emitter()
    for op in ops:
        if op[0] == "lit":
            e.bit(0)
            e.byte(op[1])
        else:
            _, l, off = op
            e.bit(1)
            e.gamma(l - 1)
            e.gamma(((off - 1) >> 8) + 1)
            e.byte((off - 1) & 0xFF)
    return bytes(e.buf)


# =============================================================================
# The decoder - a model of drivers/net/os88sfx.asm, and the build's own gate
# =============================================================================
def decode(payload: bytes, n: int) -> bytes:
    """Unpack `payload` to `n` bytes, the way the 8086 stub does.

    Written against the assembly rather than against encode() on purpose: a
    decoder derived from the encoder agrees with it by construction and so
    proves nothing. Every step below is one instruction group in os88sfx.asm,
    including the bit buffer's sentinel, whose spurious carry on the refill
    path is discarded here for the same reason it is discarded there.
    """
    out = bytearray()
    state = {"si": 0, "bx": 0}

    def getbit() -> int:
        bx = state["bx"]
        cf = bx & 1                          # shr bx, 1
        bx >>= 1
        if bx == 0:                          # jnz .r  -- buffer spent
            bx = 0x100 | payload[state["si"]]    # mov bl,[si] / mov bh,1
            state["si"] += 1
            cf = bx & 1                      # shr bx, 1 (the real bit)
            bx >>= 1
        state["bx"] = bx
        return cf

    def gamma() -> int:
        ax = 1
        while getbit():
            ax = ax + ax + getbit()
        return ax

    while len(out) < n:
        if not getbit():
            out.append(payload[state["si"]])         # movsb
            state["si"] += 1
        else:
            length = gamma() + 1
            offhi = gamma() - 1
            offlo = payload[state["si"]]
            state["si"] += 1
            off = (offhi << 8) + offlo + 1
            src = len(out) - off
            if src < 0:
                fail("stream refers behind the start of the image")
            for k in range(length):                  # rep movsb, byte at a
                out.append(out[src + k])             # time, so it overlaps
    return bytes(out)


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Pack a DOS .COM for drivers/net/os88sfx.asm.")
    ap.add_argument("input", metavar="RAW.COM")
    ap.add_argument("-o", "--output", metavar="OUT.lz", required=True)
    ap.add_argument("--inc", metavar="OUT.lzi",
                    help="write the nasm constants the stub %%includes")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    try:
        with open(args.input, "rb") as f:
            raw = f.read()
    except OSError as e:
        fail(f"cannot read {args.input}: {e}")

    if not raw:
        fail("the input is empty")
    if raw[0] == 0:
        fail(f"{args.input} starts with a zero byte - a .COM is entered at "
             "its first byte, so this is not one")

    n = len(raw)
    packed = encode(parse(raw))

    # --- the gate ----------------------------------------------------------
    back = decode(packed, n)
    if back != raw:
        where = next((i for i in range(min(len(back), n)) if back[i] != raw[i]),
                     min(len(back), n))
        fail(f"round trip failed at byte {where} - refusing to write a "
             f"payload the stub cannot restore")

    # --- and the arithmetic the stub depends on ----------------------------
    # The stub stages the packed body at SFX_ORG + n, which is the first byte
    # PAST the image it is about to write, so the two can never overlap.
    top = SFX_ORG + n + len(packed)
    if top > SEG_CEILING:
        fail(f"staging the packed body ends at {top:#06x}, past {SEG_CEILING:#06x} "
             "- a .COM is one 64KB segment and DOS's stack is at the top of it")

    with open(args.output, "wb") as f:
        f.write(packed)

    if args.inc:
        with open(args.inc, "w") as f:
            f.write("; GENERATED by tools/os88sfx.py - do not edit.\n"
                    "; The unpacked size of the payload beside this file, which is\n"
                    "; what tells the stub where to stage it and when it is done.\n"
                    f"SFX_RAW  equ {n}\n"
                    f"SFX_PACK equ {len(packed)}\n")

    if not args.quiet:
        pct = 100.0 * len(packed) / n
        print(f"os88sfx: {n} -> {len(packed)} bytes ({pct:.1f}%), "
              f"staged at {SFX_ORG + n:#06x}..{top:#06x}, round trip OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
