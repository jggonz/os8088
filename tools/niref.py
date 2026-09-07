#!/usr/bin/env python3
"""niref: an INDEPENDENT reference compositor for INFONES (SPEC.md 91.14.4).

    python3 tools/niref.py --selftest                 # the check checks itself
    python3 tools/niref.py --check STATE FRAME        # a dump against this
    python3 tools/niref.py --render STATE OUT         # ...or just write mine

tools/c64ref.py's role, one machine along. This file renders a PPU state to a
256x240 image of NES palette indices FROM THE NESDEV DOCUMENTATION, and
nothing in it is derived from apps/infones/niband.inc: two implementations of
one specification disagreeing is the whole point, and a "reference" written by
reading the routine it checks proves only that the transcription was faithful.

IT IS RUN AGAINST **BOTH** HALVES OF THE PORT (SPEC.md 91.14.4):

  * niuitest's C MODEL frame - which is what c64ref.py checks, and which
    touches no assembly at all; and
  * nimemtest's SHIPPING-ASSEMBLY frame, written out over the serial port from
    the routine that actually ships.

Running it against the model alone would be C64-SPEC 9.8's cautionary case one
level up: the C harness transcribed the doubler correctly, and that is exactly
what made the real routine's two independent defects invisible.

--selftest INJECTS A ONE-BIT DEFECT AND REQUIRES THE COMPARE TO FAIL, because
a check that cannot fail is not a check (c64ref.py's own rule). It is what
build.sh runs in wave 1, when there is not yet a composer to dump.

---------------------------------------------------------------------------
THE STATE BLOB - what a dump hands this file
---------------------------------------------------------------------------
Little-endian throughout.

    0    6   b"NIREF1"
    6    1   ctrl        ($2000 as it stood; bit 4 = the background pattern
                          table, bits 0-1 are already folded into `v`)
    7    1   mask        ($2001: bit 1 = show the leftmost 8 background
                          pixels, bit 3 = show the background at all)
    8    1   fine_x      (the low 3 bits of the first $2005 write)
    9    1   mirror      (0 horizontal, 1 vertical)
    10   480 v[240]      THE PER-LINE LOOPY ADDRESS, one word a visible line.
                         A SCANLINE MODEL IS WHAT THIS PORT PROMISES (SPEC.md
                         91.5), so the state it is checked against is per line
                         and not one snapshot: a $2005 write mid-frame moves
                         the picture from the NEXT line, and a single `v` could
                         not express that
    490  32  palette RAM (after the every-four mirror fold)
    522  2048 the two nametables, A then B
    2570 8192 the pattern tables
    10762 256 OAM        -- READ BUT NOT YET COMPOSED: the sprite pass is
                         wave 2's, and its rules are written below so that the
                         wave which adds it has a specification rather than a
                         routine to copy

The FRAME is 256 x 240 bytes, one NES palette index a pixel, row-major.
A dump may carry the port's own tag bits above the index (SPEC.md 91.5.2);
--check masks them off before comparing, because they are a rendering
convenience and not part of the picture.
"""
import argparse
import struct
import sys

W, H = 256, 240
HDR = 10
OFF_V = HDR
OFF_PAL = OFF_V + 480
OFF_NT = OFF_PAL + 32
OFF_CHR = OFF_NT + 2048
OFF_OAM = OFF_CHR + 8192
BLOB = OFF_OAM + 256

TAGS = 0xC0             # the priority and backdrop tags this port carries in
                        # the top two bits of a pixel byte (SPEC.md 91.5.2)


class State:
    def __init__(self, blob):
        if len(blob) < BLOB or blob[0:6] != b"NIREF1":
            raise ValueError("not a NIREF1 state blob (%d bytes)" % len(blob))
        self.ctrl = blob[6]
        self.mask = blob[7]
        self.fine_x = blob[8] & 7
        self.mirror = blob[9] & 1
        self.v = list(struct.unpack_from("<240H", blob, OFF_V))
        self.pal = blob[OFF_PAL:OFF_PAL + 32]
        self.nt = blob[OFF_NT:OFF_NT + 2048]
        self.chr = blob[OFF_CHR:OFF_CHR + 8192]
        self.oam = blob[OFF_OAM:OFF_OAM + 256]

    def nt_read(self, addr):
        """A nametable address to a byte. The four 1KB logical tables fold
        onto two physical ones and the arrangement is the cartridge's."""
        idx = (addr >> 10) & 3
        phys = (idx & 1) if self.mirror else ((idx >> 1) & 1)
        return self.nt[phys * 1024 + (addr & 0x3FF)]


def render(st):
    """The background, per visible line, from the loopy address that line
    started with. NESdev's own decomposition of `v`:

        coarse X = v & 0x1F, coarse Y = (v >> 5) & 0x1F,
        nametable = (v >> 10) & 3, fine Y = (v >> 12) & 7

    and the attribute byte for a tile is at 0x23C0 | (nt << 10) |
    ((coarse Y >> 2) << 3) | (coarse X >> 2), two bits of it chosen by
    (coarse Y & 2) and (coarse X & 2).
    """
    out = bytearray(W * H)
    # WHETHER THE BACKGROUND PIXEL WAS TRANSPARENT, kept beside the picture.
    # The port carries it as a TAG BIT in the pixel byte (SPEC.md 91.5.2) and
    # this file does not, deliberately: two implementations of one
    # specification agreeing is the point, and copying the port's
    # representation would make the sprite merge here a transcription of the
    # merge there.
    bg_clear = bytearray(W * H)
    base = 0x1000 if (st.ctrl & 0x10) else 0x0000
    backdrop = st.pal[0] & 0x3F

    for y in range(H):
        if not (st.mask & 0x08):
            for x in range(W):
                out[y * W + x] = backdrop
                bg_clear[y * W + x] = 1
            continue
        v = st.v[y]
        cx0 = v & 0x1F
        cy = (v >> 5) & 0x1F
        nt0 = (v >> 10) & 3
        fy = (v >> 12) & 7
        line = bytearray(33 * 8)
        clear = bytearray(33 * 8)
        for col in range(33):
            cx = cx0 + col
            nt = nt0 ^ (0x01 if cx >= 32 else 0)    # the HORIZONTAL wrap
            cx &= 0x1F                              # flips nametable bit 0
            ntbase = 0x2000 | (nt << 10)
            tile = st.nt_read(ntbase | (cy << 5) | cx)
            attr = st.nt_read(0x23C0 | (nt << 10) | ((cy >> 2) << 3) | (cx >> 2))
            shift = ((cy & 2) << 1) | (cx & 2)
            pal4 = (attr >> shift) & 3
            lo = st.chr[(base + tile * 16 + fy) & 0x1FFF]
            hi = st.chr[(base + tile * 16 + 8 + fy) & 0x1FFF]
            for b in range(8):
                val = ((lo >> (7 - b)) & 1) | (((hi >> (7 - b)) & 1) << 1)
                if val == 0:
                    line[col * 8 + b] = backdrop
                    clear[col * 8 + b] = 1
                else:
                    line[col * 8 + b] = st.pal[pal4 * 4 + val] & 0x3F
        for x in range(W):
            out[y * W + x] = line[st.fine_x + x]
            bg_clear[y * W + x] = clear[st.fine_x + x]
        if not (st.mask & 0x02):        # the left-column mask
            for x in range(8):
                out[y * W + x] = backdrop
                bg_clear[y * W + x] = 1

    # --- THE SPRITE PASS (SPEC.md 91.5.2, the port plan's R7) -------------
    # Up to eight sprites a line, painted into a scratch in ASCENDING OAM
    # order with FIRST WRITER WINS - so the lowest index owns a pixel, which
    # is what the hardware's priority says - then ONE merge with the
    # UNTOUCHED background resolves front/behind per pixel. Painting back to
    # front instead would give the same picture for opaque sprites and the
    # wrong one under a behind-tagged sprite, and could not find the sprite-0
    # strike at all, because by then the background it must be compared
    # against has been written over.
    if st.mask & 0x10:
        sbase = 0x1000 if (st.ctrl & 0x08) else 0x0000
        h = 16 if (st.ctrl & 0x20) else 8
        for y in range(H):
            chosen = []
            for i in range(64):
                row = y - st.oam[i * 4] - 1
                if row < 0 or row >= h:
                    continue
                if len(chosen) == 8:
                    break               # the eighth wins the cap
                chosen.append((i, row))
            scratch = [0] * 256
            for i, row in chosen:
                attr = st.oam[i * 4 + 2]
                if attr & 0x80:
                    row = h - 1 - row               # flipped vertically
                if h == 8:
                    addr = sbase + st.oam[i * 4 + 1] * 16 + row
                else:
                    t = st.oam[i * 4 + 1]
                    base = 0x1000 if (t & 1) else 0x0000
                    addr = base + (t & 0xFE) * 16 + (row >> 3) * 16 + (row & 7)
                lo = st.chr[addr & 0x1FFF]
                hi = st.chr[(addr + 8) & 0x1FFF]
                x0 = st.oam[i * 4 + 3]
                for b in range(8):
                    bit = (7 - b) if not (attr & 0x40) else b
                    val = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1)
                    x = x0 + b
                    if val == 0 or x > 255 or scratch[x]:
                        continue
                    scratch[x] = ((attr & 3) * 4 + val) \
                        | (0x20 if (attr & 0x20) else 0) | (0x10 if i == 0 else 0)
            for x in range(256):
                s = scratch[x]
                if not s:
                    continue
                if x < 8 and not (st.mask & 0x04):
                    continue            # the sprite left-column mask
                bgclear = bg_clear[y * W + x]
                if (s & 0x20) and not bgclear:
                    continue            # behind, over an opaque background
                out[y * W + x] = st.pal[0x10 + (s & 0x0F)] & 0x3F
    return bytes(out)


def compare(mine, theirs):
    """-> (n_differing, first (x, y, mine, theirs)) with the tags masked."""
    n = 0
    first = None
    for i in range(min(len(mine), len(theirs))):
        a = mine[i] & ~TAGS & 0xFF
        b = theirs[i] & ~TAGS & 0xFF
        if a != b:
            n += 1
            if first is None:
                first = (i % W, i // W, a, b)
    if len(mine) != len(theirs):
        n += abs(len(mine) - len(theirs))
        if first is None:
            first = (-1, -1, len(mine), len(theirs))
    return n, first


def synth():
    """THE FIXTURE, and it is written THREE TIMES on purpose.

    This function, `nifix_build` in apps/infones/hosttest/nimemtest.asm and
    `fixture()` in apps/infones/hosttest/niuitest.c are three independent
    spellings of one formula, and build.sh compares all three: the C model's
    own state blob against this one byte for byte, and the assembly's frame
    against this file's render of it. A fixture written once and shared would
    make the two frames agree about a state neither of them checked.

    It has something in every mechanism the composer implements: a non-zero
    fine X, a scroll that MOVES between lines (which is what a scanline model
    is FOR), two attribute quadrants, a tile whose four pattern values are all
    used, 8x16 sprites with both flips and both priorities, lines with more
    than eight sprites on them, and the sprite left-column mask ON with the
    background one OFF."""
    b = bytearray(BLOB)
    b[0:6] = b"NIREF1"
    b[6] = 0x20                     # bg pattern table at $0000, 8x16 SPRITES
    b[7] = 0x1A                     # bg on + bg left column on, sprites on,
                                    #   sprite LEFT COLUMN OFF - so one mask
                                    #   is exercised each way
    b[8] = 3                        # fine X
    b[9] = 1                        # vertical mirroring
    for y in range(H):
        # coarse Y walks with the line, and coarse X steps at line 100: a
        # mid-frame $2005 write, which is the case a single snapshot cannot
        # express
        cy = (y >> 3) & 0x1F
        fy = y & 7
        cx = 0 if y < 100 else 5
        nt = 0 if y < 100 else 1
        b[OFF_V + y * 2] = (cx | (cy << 5)) & 0xFF
        b[OFF_V + y * 2 + 1] = (((cx | (cy << 5)) >> 8) | (nt << 2)
                                | (fy << 4)) & 0xFF
    for i in range(32):
        b[OFF_PAL + i] = (i * 3 + 1) & 0x3F
    for i in range(2048):
        b[OFF_NT + i] = (i * 7) & 0xFF
    for i in range(8192):
        b[OFF_CHR + i] = (i * 13 + (i >> 4)) & 0xFF
    for i in range(64):
        b[OFF_OAM + i * 4 + 0] = (i * 17) & 0xFF        # Y (top - 1)
        b[OFF_OAM + i * 4 + 1] = (i * 5) & 0xFF         # tile
        b[OFF_OAM + i * 4 + 2] = i & 0xE3               # palette, flips,
                                                        #   behind
        b[OFF_OAM + i * 4 + 3] = (i * 13) & 0xFF        # X
    return bytes(b)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--check", nargs=2, metavar=("STATE", "FRAME"))
    ap.add_argument("--render", nargs=2, metavar=("STATE", "OUT"))
    ap.add_argument("--synth", metavar="OUT",
                    help="write the fixture state blob the two harnesses "
                         "build independently")
    a = ap.parse_args()

    if a.selftest:
        st = State(synth())
        frame = render(st)
        if len(frame) != W * H:
            print("niref: --selftest: the render is %d bytes, want %d"
                  % (len(frame), W * H))
            return 1
        n, first = compare(frame, frame)
        if n != 0:
            print("niref: --selftest: a frame does not equal itself")
            return 1
        # THE INJECTED DEFECT: one bit, in one pixel, in the middle of the
        # picture. A compare that cannot see this cannot see anything.
        bad = bytearray(frame)
        bad[120 * W + 128] ^= 0x01
        n, first = compare(frame, bytes(bad))
        if n != 1:
            print("niref: --selftest: the one-bit defect was seen %d times, "
                  "want exactly 1" % n)
            return 1
        if first[0] != 128 or first[1] != 120:
            print("niref: --selftest: the defect was reported at %r" % (first,))
            return 1
        # ...and a defect in a TAG bit must NOT be seen, because the tags are
        # this port's own and not part of the picture (SPEC.md 91.5.2)
        bad = bytearray(frame)
        bad[10 * W + 10] ^= 0x80
        n, _ = compare(frame, bytes(bad))
        if n != 0:
            print("niref: --selftest: a priority TAG bit was compared as "
                  "picture")
            return 1
        print("niref: --selftest PASS - a one-bit defect fails the compare, "
              "a tag bit does not")
        return 0

    if a.synth:
        open(a.synth, "wb").write(synth())
        return 0

    if a.check:
        st = State(open(a.check[0], "rb").read())
        mine = render(st)
        theirs = open(a.check[1], "rb").read()
        n, first = compare(mine, theirs)
        if n:
            print("niref: %d differing pixel(s); the first is at x=%d y=%d, "
                  "mine %d theirs %d" % ((n,) + first))
            return 1
        print("niref: %s matches this compositor bit for bit" % a.check[1])
        return 0

    if a.render:
        st = State(open(a.render[0], "rb").read())
        open(a.render[1], "wb").write(render(st))
        return 0

    ap.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
