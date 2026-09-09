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
# absent (docs/APPLE2-PORT-PLAN.md Decision 12). Wave 3 added the other two.
IMPLEMENTED = {"text", "lores", "hires"}

# THE MIXED SPLIT: the top 160 scan lines in the graphics mode and the bottom
# 32 in text, which is 20 character rows exactly (APPLE2-SPEC section 7.4).
MIXROW = 20


def hires_map(i):
    """The interleaved hi-res row base - $1C00 above the text map, which is
    what makes `$2000 + $400*sub + $80*(r&7) + $28*(r>>3)` and the text map
    one table (APPLE2-SPEC section 7.2). Page 1; page 2 is +$2000."""
    return 0x1C00 + text_map(i)


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


def a2_lit(c):
    """Is lo-res colour c LIT on the monochrome windowed path?

    The threshold is HALF OF WHITE'S LUMINANCE, applied to luminances this
    file computes from MII's own RGBs - so the package's sixteen-byte rank
    ladder and this are two independent statements about the same palette, and
    a disagreement about one colour is a bit-for-bit frame mismatch rather
    than a matter of opinion (APPLE2-SPEC section 7.3)."""
    return a2_pm(c) >= 500


def compose_text(st, out, r0=0, r1=ROWS - 1):
    """40 x 24 cells of 7 x 8, packed eight cells to seven bytes.

    56 bits is exactly seven bytes, so a group of eight cells is byte-aligned
    in the output at both ends. The stream is MSB first: cell k occupies bits
    7k..7k+6 and within a cell the leftmost pixel is the most significant.
    """
    page = 1024 if st.page2 else 0
    for row in range(r0, r1 + 1):
        base = text_map(row) + page
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


def pack_row(bits, row, line, out):
    """280 bits -> 35 bytes, starting at the left letterbox's end. The stream
    is MSB first: cell k occupies bits 7k..7k+6 and within a cell the leftmost
    pixel is the most significant."""
    o = (row * 8 + line) * BSTRIDE + LBOX
    for i in range(0, SCRW, 8):
        v = 0
        for j in range(8):
            v = (v << 1) | bits[i + j]
        out[o + i // 8] = v


def compose_lores(st, out, r0=0, r1=ROWS - 1):
    """40 x 48 blocks of 7 x 4, off the TEXT page.

    One byte is TWO blocks stacked: the low nibble is the top four scan lines
    of the character row and the high nibble the bottom four
    (apple2emu src/video.cpp's lo-res walk, MII's mii_video.c:340-378). The
    windowed path is MONOCHROME, so a block is seven lit or seven dark pixels
    by its colour's luminance.
    """
    page = 1024 if st.page2 else 0
    for row in range(r0, r1 + 1):
        base = text_map(row) + page
        for line in range(8):
            bits = []
            for col in range(COLS):
                b = st.ram[(base + col) & 0xFFFF]
                c = (b >> (0 if line < 4 else 4)) & 0x0F
                bits += [1 if a2_lit(c) else 0] * 7
            pack_row(bits, row, line, out)


def compose_hires(st, out, r0=0, r1=ROWS - 1):
    """280 x 192 monochrome: 40 source bytes to 35 output bytes a SCAN LINE.

    Hi-res data carries BIT 0 AS THE LEFTMOST PIXEL - the other way up from
    the character generator, which is the one thing about this machine most
    likely to be got backwards - and BIT 7 IS THE HALF-DOT SHIFT AND IS
    DROPPED. That is what apple2emu's `render_mono_hires_cell`
    (src/video.cpp:511-537, `byte &= 0x7f`) and MII's mono arm
    (src/mii_video.c:459-470, which reads `run >> (2+i)` and never touches the
    offset) do.

    APPLEWIN IS A NAMED DEPARTURE HERE and not a third agreeing reference:
    `updateScreenSingleHires40` (NTSC.cpp:1628-1633) reads
    `g_aPixelDoubleMaskHGR[m & 0x7F]` and then, `if (m & 0x80)`, shifts the
    result left by one and pulls in the previous column's last pixel - the
    half-dot shift, applied at signal level BEFORE the monochrome tables are
    indexed. A 280-pixel 1bpp band cannot express half a dot; the 560-pixel
    foreign mode (APPLE2-SPEC section 13) is where it comes back.
    """
    page = 0x2000 if st.page2 else 0
    for row in range(r0, r1 + 1):
        for line in range(8):
            base = hires_map(row) + page + 0x400 * line
            bits = []
            for col in range(COLS):
                b = st.ram[(base + col) & 0xFFFF]
                for k in range(7):
                    bits.append((b >> k) & 1)
            pack_row(bits, row, line, out)


def compose(st):
    out = bytearray(FRAME_LEN)
    if st.mode == MODE_TEXT:
        compose_text(st, out)
        return bytes(out)
    if st.mode == MODE_LORES:
        gfx = compose_lores
    elif st.mode == MODE_HIRES:
        gfx = compose_hires
    else:
        raise SystemExit("a2ref: no reference for mode %d" % st.mode)
    # MIXED is the top 160 scan lines in the graphics mode and the bottom 32
    # in text - 20 character rows exactly, so no row is ever half one renderer
    # and half the other (section 7.4). The text half reads the TEXT page,
    # PAGE2 and all, which is what one switch selecting both halves means.
    if st.mixed:
        gfx(st, out, 0, MIXROW - 1)
        compose_text(st, out, MIXROW, ROWS - 1)
    else:
        gfx(st, out)
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
          "(mode %s, mixed %d, page2 %d, flash phase %d, %d bytes)"
          % (("text", "lores", "hires")[st.mode], st.mixed, st.page2,
             st.phase, FRAME_LEN))
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


# THE LO-RES LUMINANCE LADDER'S GATE. The sixteen Apple lo-res colours are
# MII's `palettes[0]` "Color NTSC" (mii_emu src/mii_video.c:94-113), taken
# through MII's own lo-res mapping `mii_base_clut.lores[0]`
# (src/mii_video.c:173-177) - which is the half that is easy to lose, because
# MII's CI_* enum is NOT in Apple colour order (CI_PURPLE is 1; lo-res colour
# 1 is MAGENTA). A monochrome composer only ever asks whether one colour is
# lighter than, darker than or equal to another, so the package's rank table
# is right exactly when all 256 ORDERED answers match. The RGBs are
# transcribed here and the Y is Rec.601, so that this oracle derives its
# ordering from the reference's own numbers and not from a rounded copy of
# them in the package.
#
# WHY MII AND NOT APPLEWIN. This gate first shipped against AppleWin's
# `PaletteRGB_NTSC` lores block, whose own first line
# (source/RGBMonitor.cpp:148) reads "Note: this is a placeholder. This palette
# is overwritten by VideoInitializeOriginal()" - and it is, at start-up
# (RGBMonitor.cpp:1186-1196, called from NTSC.cpp:2366-2368), with sixteen
# colours GenerateBaseColors (NTSC.cpp:2697-2721) computes from a signal-level
# simulation rather than lists. So AppleWin never displays those literals and
# they cannot be transcribed; under them purple and medium blue came out at
# 467 and 499 per mille and drew BLACK, decided by one part per mille of a
# palette no emulator shows.
#
# apple2emu's `Lores_colors` (src/video.cpp:100-115, the mrob.com values) is
# the CROSS-CHECK: it is indexed by lo-res colour directly, agrees with MII
# exactly on twelve of the sixteen, and agrees on ALL SIXTEEN lit/dark
# decisions at the 500-per-mille threshold. It is not the definer, because
# where it and MII disagree it is MII's live CLUT that this port's mapping was
# read out of.
#
# TRANSCRIBED, NOT ADAPTED. An earlier draft carried dark green as
# 0x00,0x80,0x2F, which is in NO reference - AppleWin's placeholder dark green
# crossed with Le Chat Mauve Feline's (RGBMonitor.cpp:191) - and it was
# precisely the byte that made this gate green over a wrong ladder. A gate
# that transcribes an adapted palette is a gate that checks the package
# against itself.
#
# IT HAS NO SUBJECT UNTIL THE LO-RES COMPOSER EXISTS (wave 3), which is why
# apps/apple2/build.sh does not call it before then: a green pass over a table
# that is not there is exactly what these harnesses are built not to print.
A2_RGB = [
    (0x00, 0x00, 0x00),                 # 0  black      CI_BLACK
    (0xE3, 0x1E, 0x60),                 # 1  magenta    CI_MAGENTA
    (0x60, 0x4E, 0xBD),                 # 2  dark blue  CI_DARKBLUE
    (0xFF, 0x44, 0xFD),                 # 3  purple     CI_PURPLE
    (0x00, 0xA3, 0x60),                 # 4  dark green CI_DARKGREEN
    (0x9C, 0x9C, 0x9C),                 # 5  grey 1     CI_GRAY1
    (0x14, 0xCF, 0xFD),                 # 6  medium blue CI_BLUE
    (0xD0, 0xC3, 0xFF),                 # 7  light blue CI_LIGHTBLUE
    (0x60, 0x72, 0x03),                 # 8  brown      CI_BROWN
    (0xFF, 0x6A, 0x3C),                 # 9  orange     CI_ORANGE
    (0x9C, 0x9C, 0x9C),                 # 10 grey 2     CI_GRAY2
    (0xFF, 0xA0, 0xD0),                 # 11 pink       CI_PINK
    (0x14, 0xF5, 0x3C),                 # 12 green      CI_GREEN
    (0xD0, 0xDD, 0x8D),                 # 13 yellow     CI_YELLOW
    (0x72, 0xFF, 0xD0),                 # 14 aqua       CI_AQUA
    (0xFF, 0xFF, 0xFF),                 # 15 white      CI_WHITE
]


def a2_luma(i):
    """Rec.601 luma of lo-res colour i, at FULL precision (0 .. 255000).

    NOT divided down. The ladder has pairs a per-mille rounding would TIE -
    dark blue is 376.6 and brown 376.3 - and a tie the palette does not have
    is exactly the kind of accident this oracle exists to refuse. The only
    tie in the sixteen is the palette's own: grey 1 and grey 2 are the same
    three bytes.
    """
    r, g, b = A2_RGB[i]
    return 299 * r + 587 * g + 114 * b


def a2_pm(i):
    """...and the same figure in PARTS PER THOUSAND of white, for messages."""
    return a2_luma(i) // 255


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
                             a2_pm(a), a2_pm(b)))
                bad += 1
    if bad:
        print("a2ref: %d luminance disagreement(s)" % bad)
        return 1
    print("a2ref: the luminance ladder agrees with MII's Color NTSC palette "
          "over all 256 ordered pairs")
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
