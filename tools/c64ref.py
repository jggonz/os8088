#!/usr/bin/env python3
# =============================================================================
# os8088 - tools/c64ref.py
#
# THE INDEPENDENT PIXEL-LEVEL REFERENCE COMPOSITOR for the C64 package
# (C64-SPEC §14.5, docs/C64-PORT-PLAN.md Decision 21).
#
#     python3 tools/c64ref.py --check STATE FRAME
#     python3 tools/c64ref.py --render STATE -o OUT.pbm
#
# WHY IT EXISTS. apps/c64/hosttest/c64uitest.c checks that the glass shows
# what the shadow says it shows - that is, that the DAMAGE MODEL is honest.
# It cannot check that the composed pixels are the right pixels, because the
# thing it would compare against is the composer. So this is a SECOND
# implementation, written from VIC-II documentation and VICE 3.10's
# src/vicii/ as the authority and NEVER from apps/c64/c64band.inc, that
# renders the same C64 memory to a 320x200 1bpp image. The two are compared
# BIT FOR BIT.
#
# That is what validates the things a cell-identity model provably cannot: a
# custom character set, the hires-bitmap cell transpose, multicolour, and
# sprite priority and expansion. Wave 1 checks standard text with the CHARGEN
# ROM and with a RAM character set; the rest arrive with the modes
# (docs/C64-PORT-PLAN.md wave 4).
#
# THE RISK IT CARRIES, STATED. A second implementation written by reading the
# first is not a second implementation. `--selftest` therefore injects a
# deliberate one-bit defect into the frame it was handed and requires the
# comparison to FAIL: a check that cannot fail is not a check.
#
# ---------------------------------------------------------------------------
# THE STATE FILE, as c64uitest.c writes it
# ---------------------------------------------------------------------------
#   0x00000  65536   the C64's RAM
#   0x10000   1024   colour RAM, one nibble a cell in the low four bits
#   0x10400     64   the VIC register file, $D000-$D03F
#   0x10440     16   the CIA2 register file (PRA bits 0-1 are the VIC bank)
#   0x10450   4096   the character generator ROM, C64.ROM's third part
#            ------
#             86608 bytes
#
# THE FRAME FILE is 8,000 bytes: 200 rows of 40, bit 7 leftmost, 1 = LIT.
# =============================================================================
import argparse
import sys

RAM_LEN = 65536
COL_LEN = 1024
VIC_LEN = 64
CIA_LEN = 16
CHR_LEN = 4096
STATE_LEN = RAM_LEN + COL_LEN + VIC_LEN + CIA_LEN + CHR_LEN
FRAME_LEN = 8000

COLS, ROWS = 40, 25
W, H = 320, 200

# THE VIC-II's OWN LUMINANCE LADDER, and NOT a palette file's.
#
# src/vicii/vicii-color.c:441, `vicii_colors_6569r5` - the table VICE 3.10 as
# shipped compiles and picks for a PAL C64 (:672's switch, TOBIAS_COLORS at
# :52 with the other three #ifdef'd out). The first column is the Y the chip
# produces, and it is written here as PARTS PER THOUSAND, straight off those
# lines, so that this oracle derives the ordering from VICE's own numbers and
# not from apps/c64/c64scr.c's rounded 0..255 copy of them. Rounding is
# exactly where a second implementation written by reading the first would
# agree by accident; only the ORDER and the EQUALITIES matter to a monochrome
# composer, and per-mille preserves both.
#
# NINE LEVELS FOR SIXTEEN COLOURS, shared in SEVEN PAIRS - blue = brown,
# red = dark grey, purple = orange, medium grey = light blue, green = light
# red, cyan = light grey, yellow = light green. Deriving the table from
# data/C64/vice.vpl instead (a file VICE does not read by default) made all
# sixteen distinct and put contrast on the mono glass in seven places where a
# real C64 shows a flat field; this file carried that defect after the package
# was fixed, which is worse than either having it - an oracle that disagrees
# with the product blesses the wrong answer in whichever direction it is
# asked.
Y6569R5 = [
      0,   # 0  black        0.000
   1000,   # 1  white        1.000
    306,   # 2  red          0.306
    639,   # 3  cyan         0.639
    363,   # 4  purple       0.363
    500,   # 5  green        0.500
    237,   # 6  blue         0.237
    763,   # 7  yellow       0.763
    363,   # 8  orange       0.363
    237,   # 9  brown        0.237
    500,   # 10 light red    0.500
    306,   # 11 dark grey    0.306
    461,   # 12 medium grey  0.461
    763,   # 13 light green  0.763
    461,   # 14 light blue   0.461
    639,   # 15 light grey   0.639
]

# ...and the one threshold that is not a comparison: a cell whose ink and
# paper are the SAME level is drawn uniform, and which way is "lit" is the
# level against the middle of the range (c64scr.c: `lb >= 128` on its 0..255
# scale, which is 500 here).
Y_MID = 500


def luma(i):
    """The VIC-II's Y for colour i, per mille (vicii-color.c:441)."""
    return Y6569R5[i]


class State(object):
    def __init__(self, blob):
        if len(blob) != STATE_LEN:
            raise SystemExit("c64ref: state file is %d bytes, expected %d"
                             % (len(blob), STATE_LEN))
        o = 0
        self.ram = blob[o:o + RAM_LEN]; o += RAM_LEN
        self.col = blob[o:o + COL_LEN]; o += COL_LEN
        self.vic = blob[o:o + VIC_LEN]; o += VIC_LEN
        self.cia2 = blob[o:o + CIA_LEN]; o += CIA_LEN
        self.chargen = blob[o:o + CHR_LEN]

    # --- the frame registers, from the VIC-II's own documentation ----------
    def bank(self):
        # CIA2 port A bits 0-1 select the VIC's 16KB bank, INVERTED.
        return ((~self.cia2[0]) & 3) << 14

    def matrix_base(self):
        return self.bank() + (((self.vic[0x18] >> 4) & 0x0F) << 10)

    def char_base(self):
        return self.bank() + (((self.vic[0x18] >> 1) & 0x07) << 11)

    def mode(self):
        ecm = (self.vic[0x11] >> 6) & 1
        bmm = (self.vic[0x11] >> 5) & 1
        mcm = (self.vic[0x16] >> 4) & 1
        return (ecm << 2) | (bmm << 1) | mcm

    def glyph(self, code, line):
        """The eight bytes the VIC fetches for a character. In banks 0 and 2 a
        character base of $1000-$1FFF is the CHARACTER ROM, not RAM: that is
        how a stock machine gets its face, and it is a property of the address
        decoding rather than of any register."""
        cb = self.char_base()
        off = cb - self.bank()
        if self.bank() in (0x0000, 0x8000) and 0x1000 <= off < 0x2000:
            return self.chargen[((off - 0x1000) + code * 8 + line) & 0x0FFF]
        return self.ram[(cb + code * 8 + line) & 0xFFFF]


def compose(st):
    """Render the C64's screen to 200 rows of 40 bytes, bit 7 leftmost,
    1 = lit. Standard text mode only in wave 1; every other mode is composed
    as a uniform background, which is what apps/c64/c64band.inc does until
    wave 4 writes them, and the harness does not gate the others."""
    out = bytearray(FRAME_LEN)
    bg = st.vic[0x21] & 15
    lb = luma(bg)
    bgfill = 0xFF if lb >= Y_MID else 0x00

    if st.mode() != 0:
        for i in range(FRAME_LEN):
            out[i] = bgfill
        return bytes(out)

    vm = st.matrix_base()
    for row in range(ROWS):
        for col in range(COLS):
            code = st.ram[(vm + row * COLS + col) & 0xFFFF]
            fg = st.col[row * COLS + col] & 15
            lf = luma(fg)
            for line in range(8):
                g = st.glyph(code, line)
                if lf > lb:
                    v = g                       # ink lit over dark paper
                elif lf < lb:
                    v = g ^ 0xFF                # dark ink over lit paper
                else:
                    v = bgfill                  # one colour: a uniform cell
                out[(row * 8 + line) * COLS + col] = v
    return bytes(out)


def to_pbm(frame):
    hdr = ("P4\n%d %d\n" % (W, H)).encode()
    # PBM's 1 is BLACK; the frame's 1 is LIT, so the bits are inverted.
    return hdr + bytes((b ^ 0xFF) for b in frame)


def check(state_path, frame_path, selftest=False):
    st = State(open(state_path, "rb").read())
    got = bytearray(open(frame_path, "rb").read())
    if len(got) != FRAME_LEN:
        raise SystemExit("c64ref: frame file is %d bytes, expected %d"
                         % (len(got), FRAME_LEN))
    if selftest:
        got[FRAME_LEN // 2] ^= 0x01         # a deliberate one-bit defect
    want = compose(st)
    for i in range(FRAME_LEN):
        if got[i] != want[i]:
            row, line, col = i // 320, (i // 40) % 8, i % 40
            print("c64ref: MISMATCH at byte %d - character row %d, pixel line "
                  "%d, cell %d: composer 0x%02X, reference 0x%02X"
                  % (i, row, line, col, got[i], want[i]))
            print("c64ref: mode %d, matrix $%04X, chargen $%04X, bank $%04X"
                  % (st.mode(), st.matrix_base(), st.char_base(), st.bank()))
            return 1
    print("c64ref: the composed frame equals the reference bit for bit "
          "(mode %d, matrix $%04X, chargen $%04X)"
          % (st.mode(), st.matrix_base(), st.char_base()))
    return 0


def lumcheck(path):
    """The PRODUCT's 16-entry luminance table against the VIC-II's own ladder,
    over all 256 ORDERED PAIRS.

    A monochrome composer only ever asks one question about two colours - is
    the ink lighter than the paper, darker, or the same - so the table is
    right exactly when every one of those 256 answers matches, and rounding
    into 0..255 is free to differ. This is what covers the seven
    equal-luminance pairs in BOTH directions, which a rendered frame can only
    do one background at a time.

    apps/c64/hosttest/c64uitest.c writes the table; this reads it.
    """
    blob = open(path, "rb").read()
    if len(blob) != 16:
        print("c64ref: %s is %d bytes, expected 16" % (path, len(blob)))
        return 1
    bad = 0
    for a in range(16):
        for b in range(16):
            want = (Y6569R5[a] > Y6569R5[b]) - (Y6569R5[a] < Y6569R5[b])
            got = (blob[a] > blob[b]) - (blob[a] < blob[b])
            if want != got:
                if bad < 6:
                    print("c64ref: colour %2d vs %2d: the package says %s, "
                          "the 6569R5 ladder says %s (%d vs %d per mille)"
                          % (a, b,
                             ("lighter", "the same", "darker")[1 - got],
                             ("lighter", "the same", "darker")[1 - want],
                             Y6569R5[a], Y6569R5[b]))
                bad += 1
    for i in range(16):
        want = 1 if Y6569R5[i] >= Y_MID else 0
        got = 1 if blob[i] >= 128 else 0
        if want != got:
            print("c64ref: colour %2d falls on the wrong side of the uniform "
                  "cell's threshold" % i)
            bad += 1
    if bad:
        print("c64ref: %d luminance disagreement(s)" % bad)
        return 1
    print("c64ref: the luminance ladder agrees with vicii_colors_6569r5 over "
          "all 256 ordered pairs (7 equal pairs, both ways)")
    return 0


def main():
    ap = argparse.ArgumentParser(
        description="An independent reference compositor for the C64 package.")
    ap.add_argument("--check", nargs=2, metavar=("STATE", "FRAME"),
                    help="compare a composed frame against the reference")
    ap.add_argument("--selftest", nargs=2, metavar=("STATE", "FRAME"),
                    help="inject a one-bit defect and require the compare to "
                         "FAIL: a check that cannot fail is not a check")
    ap.add_argument("--lumcheck", metavar="LUM",
                    help="the package's 16-byte luminance table against the "
                         "VIC-II's own ladder, over all 256 ordered pairs")
    ap.add_argument("--render", metavar="STATE",
                    help="render a state file and write a PBM")
    ap.add_argument("-o", "--out", default="build/c64ref.pbm")
    args = ap.parse_args()

    if args.check:
        return check(args.check[0], args.check[1])
    if args.selftest:
        rc = check(args.selftest[0], args.selftest[1], selftest=True)
        if rc == 0:
            print("c64ref: SELFTEST FAILED - a one-bit defect went unnoticed")
            return 1
        print("c64ref: selftest OK - the one-bit defect was caught")
        return 0
    if args.lumcheck:
        return lumcheck(args.lumcheck)
    if args.render:
        st = State(open(args.render, "rb").read())
        open(args.out, "wb").write(to_pbm(compose(st)))
        print("c64ref: %s" % args.out)
        return 0
    ap.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
