#!/usr/bin/env python3
# =============================================================================
# os8088 - tools/a2ref.py
#
# THE INDEPENDENT PIXEL-LEVEL REFERENCE COMPOSITOR for the APPLE2 package
# (docs/APPLE2-SPEC.md section 16.6).
#
#     python3 tools/a2ref.py --check text STATE FRAME
#     python3 tools/a2ref.py --selftest text STATE FRAME
#     python3 tools/a2ref.py --romshape build/apple2-rom/APPLE2.ROM
#     python3 tools/a2ref.py --lumcheck LUM
#     python3 tools/a2ref.py --render text STATE -o OUT.pbm
#
# WHY IT EXISTS. apps/apple2/hosttest/a2uitest.c checks that the glass shows
# what the shadow says it shows - that is, that the DAMAGE MODEL is honest. It
# cannot check that the composed pixels are the RIGHT pixels, because the
# thing it would compare against is the composer. So this is a SECOND
# implementation, written from AppleWin's source/NTSC_CharSet.cpp, MII's
# src/mii_video.c and apple2emu's src/video.cpp as the authority and NEVER
# from apps/apple2/a2band.inc, that renders the same Apple II memory to a
# 320x192 1bpp band. The two are compared BIT FOR BIT.
#
# THE RISK IT CARRIES, STATED. A second implementation written by reading the
# first is not a second implementation. `--selftest` therefore injects a
# deliberate one-bit defect into the frame it was handed and requires the
# comparison to FAIL: a check that cannot fail is not a check.
#
# AND `--check` TAKES AN EXPLICIT MODE LIST AND ERRORS ON A MODE IT HAS NO
# REFERENCE FOR. Wave 1 asks for `text` alone and wave 3 adds `lores` and
# `hires` - which is the difference between a gate and a green pass over three
# modes that do not exist yet.
#
# ---------------------------------------------------------------------------
# THE STATE FILE, as a2uitest.c writes it
# ---------------------------------------------------------------------------
#   0x00000  65536   the Apple's RAM, $0000-$FFFF
#   0x10000   2048   the CHARGEN ROM - APPLE2.ROM's offset 0x3000
#   0x10800     16   the display state:
#                      [0] mode: 0 TEXT, 1 LORES, 2 HIRES
#                      [1] MIXED
#                      [2] PAGE2
#                      [3] the FLASH PHASE - 0 normal, 1 swapped
#                      [4..15] zero
#            ------
#             67600 bytes
#
# THE FRAME FILE is 7,680 bytes: 192 rows of 40, bit 7 leftmost, 1 = LIT.
# Bytes 0-1 and 37-39 of each row are the LETTERBOX (16 px left, 24 px right)
# and are always dark - the Apple's 280 pixels are bytes 2..36.
# =============================================================================
import argparse
import sys

RAM_LEN = 65536
CHR_LEN = 2048
ST_LEN = 16
STATE_LEN = RAM_LEN + CHR_LEN + ST_LEN

COLS, ROWS = 40, 24
SCRW, SCRH = 280, 192
BSTRIDE = 40                            # bytes per band row (320 px)
LBOX = 2                                # ...of which two are the left letterbox
FRAME_LEN = BSTRIDE * SCRH              # 7,680

MODE_TEXT, MODE_LORES, MODE_HIRES = 0, 1, 2
MODES = {"text": MODE_TEXT, "lores": MODE_LORES, "hires": MODE_HIRES}

# WHICH MODES THIS FILE HAS A REFERENCE FOR. `--check lores` on a build whose
# lo-res composer does not exist is a FAILURE and not a skip: the plan's rule
# is that every harness step fails rather than passing when its subject is
# absent (docs/APPLE2-PORT-PLAN.md Decision 12).
IMPLEMENTED = {"text"}


def text_map(i):
    """The interleaved row base - apple2emu src/video.cpp:687-718, whose own
    cited source is "Apple 2 Monitors Peeled, pg 15". Page 1; page 2 is the
    same plus 1024."""
    return 1024 + 256 * ((i // 2) % 4) + 128 * (i % 2) + 40 * ((i // 8) % 4)


class State(object):
    def __init__(self, blob):
        if len(blob) != STATE_LEN:
            raise SystemExit("a2ref: state file is %d bytes, expected %d"
                             % (len(blob), STATE_LEN))
        self.ram = blob[0:RAM_LEN]
        self.chr = blob[RAM_LEN:RAM_LEN + CHR_LEN]
        st = blob[RAM_LEN + CHR_LEN:]
        self.mode = st[0]
        self.mixed = st[1]
        self.page2 = st[2]
        self.phase = st[3]

    def glyph(self, code, line):
        """The eight bits the video circuit shifts out for a screen byte.

        AppleWin's NTSC_CharSet.cpp reads `rom[i*8 + (y&7)]` and XORs $7F
        where the entry is in the low 1KB and bit 7 is clear. Reading BLOCK
        $C0 instead makes that XOR unnecessary - those 64 characters are
        already the normal form - and the inverse and flashing forms are then
        one per-cell XOR mask:

            $00-$3F  INVERSE    mask $7F
            $40-$7F  FLASHING   mask $7F while the phase is swapped
            $80-$FF  NORMAL     mask $00

        and the glyph index is `byte & 0x3F` in all three ranges, because
        blocks $40, $80 and $C0 are byte-identical (--romshape asserts it).

        THE BIT ORDER: in the character ROM bit 6 is the LEFTMOST pixel, which
        is the framebuffer's own order. Hi-res data is the other way up and is
        what the package's 128-entry reverse table exists for; they are not
        the same convention.
        """
        if code < 0x40:
            mask = 0x7F
        elif code < 0x80:
            mask = 0x7F if self.phase else 0x00
        else:
            mask = 0x00
        g = self.chr[(0xC0 + (code & 0x3F)) * 8 + line] & 0x7F
        return g ^ mask


def compose_text(st, out):
    """40 x 24 cells of 7 x 8, packed eight cells to seven bytes.

    56 bits is exactly seven bytes, so a group of eight cells is byte-aligned
    in the output at both ends. The stream is MSB first: cell k occupies bits
    7k..7k+6 and within a cell the leftmost pixel is the most significant.
    """
    base0 = text_map
    page = 1024 if st.page2 else 0
    for row in range(ROWS):
        base = base0(row) + page
        for line in range(8):
            bits = []
            for col in range(COLS):
                g = st.glyph(st.ram[(base + col) & 0xFFFF], line)
                for b in range(6, -1, -1):
                    bits.append((g >> b) & 1)
            # 280 bits -> 35 bytes, starting at the left letterbox's end
            o = (row * 8 + line) * BSTRIDE + LBOX
            for i in range(0, SCRW, 8):
                v = 0
                for j in range(8):
                    v = (v << 1) | bits[i + j]
                out[o + i // 8] = v


def compose(st):
    out = bytearray(FRAME_LEN)
    if st.mode == MODE_TEXT:
        compose_text(st, out)
    else:
        raise SystemExit("a2ref: no reference for mode %d - lo-res and hi-res "
                         "arrive with the composers that draw them (wave 3)"
                         % st.mode)
    return bytes(out)


def to_pbm(frame):
    hdr = ("P4\n%d %d\n" % (BSTRIDE * 8, SCRH)).encode()
    # PBM's 1 is BLACK; the frame's 1 is LIT, so the bits are inverted.
    return hdr + bytes((b ^ 0xFF) for b in frame)


def want_modes(names):
    bad = [n for n in names if n not in MODES]
    if bad:
        raise SystemExit("a2ref: unknown mode(s) %s - known: %s"
                         % (", ".join(bad), ", ".join(sorted(MODES))))
    missing = [n for n in names if n not in IMPLEMENTED]
    if missing:
        raise SystemExit("a2ref: asked to check %s and there is no reference "
                         "for it in this build - a step whose subject is "
                         "absent FAILS rather than passing "
                         "(docs/APPLE2-PORT-PLAN.md Decision 12)"
                         % ", ".join(missing))
    return [MODES[n] for n in names]


def check(modes, state_path, frame_path, selftest=False):
    st = State(open(state_path, "rb").read())
    if st.mode not in modes:
        print("a2ref: the state file is mode %d and the check asked for %s"
              % (st.mode, modes))
        return 1
    got = bytearray(open(frame_path, "rb").read())
    if len(got) != FRAME_LEN:
        raise SystemExit("a2ref: frame file is %d bytes, expected %d"
                         % (len(got), FRAME_LEN))
    if selftest:
        got[FRAME_LEN // 2 + LBOX] ^= 0x01      # a deliberate one-bit defect
    want = compose(st)
    for i in range(FRAME_LEN):
        if got[i] != want[i]:
            line, col = i // BSTRIDE, i % BSTRIDE
            print("a2ref: MISMATCH at byte %d - scan line %d, band byte %d "
                  "(character row %d, pixel line %d): composer 0x%02X, "
                  "reference 0x%02X"
                  % (i, line, col, line // 8, line % 8, got[i], want[i]))
            print("a2ref: mode %d, mixed %d, page2 %d, flash phase %d"
                  % (st.mode, st.mixed, st.page2, st.phase))
            return 1
    print("a2ref: the composed frame equals the reference bit for bit "
          "(mode %d, page2 %d, flash phase %d, %d bytes)"
          % (st.mode, st.page2, st.phase, FRAME_LEN))
    return 0


def romshape(path):
    """THE PINNED ROM'S MEASURED SHAPE, so the 64-glyph decision is checkable
    rather than remembered (APPLE2-SPEC section 7.3).

    Decoding the character generator through AppleWin's own algorithm yields
    128 DISTINCT 8-byte bitmaps and not 64: blocks $40, $80 and $C0 are
    byte-identical (normal) and block $00-$3F is their XOR $7F (inverse). The
    common reading - 64 glyphs repeating every $40 - is true only across
    $40-$FF. The package therefore stores 64 glyphs and applies a per-cell XOR
    mask, and "512 bytes and no mask" are mutually exclusive.
    """
    blob = open(path, "rb").read()
    if len(blob) < 0x3800:
        print("a2ref: %s is %d bytes - not APPLE2.ROM" % (path, len(blob)))
        return 1
    cg = blob[0x3000:0x3800]

    def raw(i, y):
        n = cg[i * 8 + (y & 7)]
        if i < 128 and not (n & 0x80):
            n ^= 0x7F                       # AppleWin NTSC_CharSet.cpp:180-234
        return n & 0x7F

    def blk(i):
        return tuple(raw(i, y) for y in range(8))

    distinct = len(set(blk(i) for i in range(256)))
    bad = 0
    if distinct != 128:
        print("a2ref: the character generator yields %d distinct bitmaps, "
              "expected 128" % distinct)
        bad += 1
    for i in range(0x40):
        if not (blk(i + 0x40) == blk(i + 0x80) == blk(i + 0xC0)):
            print("a2ref: blocks $40/$80/$C0 differ at index %d" % i)
            bad += 1
            break
    for i in range(0x40):
        if blk(i) != tuple(v ^ 0x7F for v in blk(i + 0x80)):
            print("a2ref: block $00 is not block $80 XOR $7F at index %d" % i)
            bad += 1
            break
    if bad:
        return 1
    print("a2ref: romshape OK - 128 distinct bitmaps, blocks $40/$80/$C0 "
          "identical, block $00 = block $80 XOR $7F")
    return 0


# THE LO-RES LUMINANCE LADDER'S GATE. apple2emu's src/video.cpp:94-113 carries
# the 16 Apple colours as mrob.com RGB values; a monochrome composer only ever
# asks whether one is lighter than, darker than or equal to another, so the
# table is right exactly when all 256 ORDERED answers match. The RGBs are
# transcribed here and the Y is Rec.601 in PARTS PER THOUSAND, so that this
# oracle derives its ordering from the reference's own numbers and not from a
# rounded 0..255 copy of them in the package.
#
# IT HAS NO SUBJECT UNTIL THE LO-RES COMPOSER EXISTS (wave 3), which is why
# apps/apple2/build.sh does not call it before then: a green pass over a table
# that is not there is exactly what these harnesses are built not to print.
A2_RGB = [
    (0x00, 0x00, 0x00),                 # 0  black
    (0x9D, 0x09, 0x66),                 # 1  magenta
    (0x2A, 0x2A, 0xE5),                 # 2  dark blue
    (0xC7, 0x34, 0xFF),                 # 3  purple
    (0x00, 0x80, 0x2F),                 # 4  dark green
    (0x80, 0x80, 0x80),                 # 5  grey 1
    (0x0D, 0xA1, 0xFF),                 # 6  medium blue
    (0xAA, 0xAA, 0xFF),                 # 7  light blue
    (0x55, 0x55, 0x00),                 # 8  brown
    (0xF2, 0x5E, 0x00),                 # 9  orange
    (0xC0, 0xC0, 0xC0),                 # 10 grey 2
    (0xFF, 0x89, 0xE5),                 # 11 pink
    (0x38, 0xCB, 0x00),                 # 12 green
    (0xD5, 0xD5, 0x1A),                 # 13 yellow
    (0x62, 0xF6, 0x99),                 # 14 aqua
    (0xFF, 0xFF, 0xFF),                 # 15 white
]


def a2_luma(i):
    r, g, b = A2_RGB[i]
    return (299 * r + 587 * g + 114 * b) // 255


def lumcheck(path):
    blob = open(path, "rb").read()
    if len(blob) != 16:
        print("a2ref: %s is %d bytes, expected 16 - the package's luminance "
              "ladder is not there" % (path, len(blob)))
        return 1
    bad = 0
    for a in range(16):
        for b in range(16):
            wa, wb = a2_luma(a), a2_luma(b)
            want = (wa > wb) - (wa < wb)
            got = (blob[a] > blob[b]) - (blob[a] < blob[b])
            if want != got:
                if bad < 6:
                    print("a2ref: colour %2d vs %2d: the package says %s, the "
                          "reference palette says %s (%d vs %d per mille)"
                          % (a, b,
                             ("lighter", "the same", "darker")[1 - got],
                             ("lighter", "the same", "darker")[1 - want],
                             wa, wb))
                bad += 1
    if bad:
        print("a2ref: %d luminance disagreement(s)" % bad)
        return 1
    print("a2ref: the luminance ladder agrees with apple2emu's palette over "
          "all 256 ordered pairs")
    return 0


def main():
    ap = argparse.ArgumentParser(
        description="An independent reference compositor for the APPLE2 "
                    "package.")
    ap.add_argument("--check", nargs=3, metavar=("MODES", "STATE", "FRAME"),
                    help="compare a composed frame against the reference. "
                         "MODES is a comma-separated explicit list and this "
                         "ERRORS on a mode it has no reference for")
    ap.add_argument("--selftest", nargs=3, metavar=("MODES", "STATE", "FRAME"),
                    help="inject a one-bit defect and require the compare to "
                         "FAIL: a check that cannot fail is not a check")
    ap.add_argument("--romshape", metavar="ROM",
                    help="assert the pinned ROM's measured character-generator "
                         "shape (128 distinct bitmaps)")
    ap.add_argument("--lumcheck", metavar="LUM",
                    help="the package's 16-byte lo-res luminance ladder over "
                         "all 256 ordered pairs (wave 3's subject)")
    ap.add_argument("--render", nargs=2, metavar=("MODES", "STATE"),
                    help="render a state file and write a PBM")
    ap.add_argument("-o", "--out", default="build/a2ref.pbm")
    args = ap.parse_args()

    if args.check:
        return check(want_modes(args.check[0].split(",")),
                     args.check[1], args.check[2])
    if args.selftest:
        rc = check(want_modes(args.selftest[0].split(",")),
                   args.selftest[1], args.selftest[2], selftest=True)
        if rc == 0:
            print("a2ref: SELFTEST FAILED - a one-bit defect went unnoticed")
            return 1
        print("a2ref: selftest OK - the one-bit defect was caught")
        return 0
    if args.romshape:
        return romshape(args.romshape)
    if args.lumcheck:
        return lumcheck(args.lumcheck)
    if args.render:
        want_modes(args.render[0].split(","))
        st = State(open(args.render[1], "rb").read())
        open(args.out, "wb").write(to_pbm(compose(st)))
        print("a2ref: %s" % args.out)
        return 0
    ap.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
