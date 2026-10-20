#!/usr/bin/env python3
"""Writes the synthetic ANSI fixtures under `tests/fixtures/ansi/`.

One stream per rule family of the BBS terminal's parser contract, drawn by
this tool rather than taken from anybody's artpack: nothing here is fetched,
copied or third-party, and every byte of every fixture is accounted for by a
line of Python above it. The stream is the fixture; the rendering beside it is
what `tools/ansisim.py` says the 80x25 buffer becomes, recorded so that a
change to the simulator shows up as a DIFF rather than as two green runs of a
test that quietly moved its own goalposts.

Three files per fixture, and each has a different reader:

  <name>.bin   the stream. This is what a BBS would send and what
               `tools/os88bbs.py` feeds down the wire, in fragments.
  <name>.txt   a person. The screen as CP437 text in a box, then the
               attribute plane run-length encoded one row per line. A
               reviewer can see what the fixture is FOR without running
               anything.
  <name>.json  a test. The buffer run-length encoded, the cursor, the
               attribute, what the terminal answered, and a SHA-256 of the
               4,000 bytes. A test can rebuild the expected buffer from this
               alone and compare it with guest memory, so the fixture is an
               oracle in its own right and not merely a cache of the
               simulator's opinion.

Both encodings are exact and reversible, and the run-length form is what
keeps a mostly-blank 80x25 screen down to a few hundred bytes in the repo.

  python3 tools/ansifix.py            # write them
  python3 tools/ansifix.py --check    # ...and fail if the tree disagrees
  python3 tools/ansifix.py --list
"""
import argparse
import hashlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import ansisim                                                   # noqa: E402

OUTDIR = os.path.join(ROOT, "tests", "fixtures", "ansi")

CSI = b"\x1b["


def sgr(*codes):
    return CSI + b";".join(b"%d" % c for c in codes) + b"m"


def at(row, col):
    """CUP, 1-based, the way a BBS writes it."""
    return CSI + b"%d;%dH" % (row, col)


# =============================================================================
# THE FIXTURES
#
# Each builder returns the stream. The name is the rule family and the
# docstring is what a reader of the .txt needs in order to know what they are
# looking at - it is copied into the .txt header, so it is documentation that
# ships beside the evidence rather than above the code that made it.
# =============================================================================
def f_cursor():
    """Cursor movement: CUP, HVP, CUU/CUD/CUF/CUB, CNL/CPL, CHA, and the
    clamps at all four edges. Every landing is marked with a letter, so a
    parser that lands one cell out is visible as a letter in the wrong cell
    rather than as a number in a log."""
    s = CSI + b"2J"
    s += at(1, 1) + b"A"                       # home
    s += at(3, 5) + b"B"                       # CUP proper
    s += CSI + b"3;10f" + b"C"                 # HVP is CUP
    s += at(10, 10) + CSI + b"2A" + b"U"       # up 2   -> row 8
    s += at(10, 10) + CSI + b"2B" + b"D"       # down 2 -> row 12
    s += at(10, 10) + CSI + b"5C" + b"R"       # right 5
    s += at(10, 10) + CSI + b"5D" + b"L"       # left 5
    s += at(10, 10) + CSI + b"A" + b"1"        # default of 1, all four
    s += at(10, 20) + CSI + b"B" + b"1"
    s += at(10, 30) + CSI + b"C" + b"1"
    s += at(10, 40) + CSI + b"D" + b"1"
    s += at(15, 40) + CSI + b"E" + b"N"        # CNL: down and to column 1
    s += at(15, 40) + CSI + b"F" + b"P"        # CPL: up and to column 1
    s += at(18, 1) + CSI + b"40G" + b"G"       # CHA
    s += at(18, 1) + CSI + b"G" + b"g"         # CHA default
    s += at(1, 1) + CSI + b"10A" + b"^"        # clamps: up at the top,
    s += at(25, 1) + CSI + b"10B" + b"v"       # down at the bottom,
    s += at(20, 80) + CSI + b"10C" + b">"      # right at the last column,
    s += at(20, 1) + CSI + b"10D" + b"<"       # left at the first
    s += CSI + b"99;99H" + b"@"                # and CUP past both edges
    s += at(22, 1) + CSI + b";40H" + b"m"      # a missing parameter is 1
    return s


def f_erase():
    """ED 0/1/2, EL 0/1/2 and ECH, each over a screen painted with a
    background colour first - because the rule that matters is that an erase
    fills with the CURRENT attribute and not with 0x07, and a screen erased
    on the default attribute cannot tell the two apart."""
    s = CSI + b"2J"
    for r in range(1, 26):                     # paint every row
        s += at(r, 1) + sgr(0, 33, 44) + (b"%-80s" % (b"row %d " % r * 6))[:80]
    s += at(5, 40) + sgr(0, 37, 41) + CSI + b"0K"     # EL 0 on red
    s += at(6, 40) + sgr(0, 37, 42) + CSI + b"1K"     # EL 1 on green
    s += at(7, 40) + sgr(0, 37, 46) + CSI + b"2K"     # EL 2 on cyan
    s += at(9, 10) + sgr(0, 30, 47) + CSI + b"10X"    # ECH 10, cursor stays
    s += b"|"                                          # ...and here it is
    s += at(12, 40) + sgr(0, 37, 45) + CSI + b"0J"    # ED 0, to the end
    s += at(20, 20) + sgr(0, 37, 44)
    s += at(3, 40) + CSI + b"1J"                       # ED 1, to the start
    s += at(24, 1) + sgr(0, 37, 40) + b"the last two rows survive ED 1 and ED 0"
    return s


def f_sgr():
    """Every SGR code in the contract, each on its own row and labelled: the
    eight foregrounds, the eight backgrounds, bold and its 22, blink and its
    25, reverse and its 27, concealed, the bright 90-97 and 100-107, 39 and
    49, and the codes that must be IGNORED."""
    s = CSI + b"2J" + sgr(0)
    s += at(1, 1) + b"SGR"
    s += at(2, 1)
    for c in range(30, 38):                    # the eight foregrounds
        s += sgr(0, c) + b"%d" % c
    s += at(3, 1)
    for c in range(30, 38):                    # ...and bold
        s += sgr(0, 1, c) + b"%d" % c
    s += at(4, 1)
    for c in range(40, 48):                    # the eight backgrounds
        s += sgr(0, 37, c) + b"%d" % c
    s += at(5, 1) + sgr(0, 1, 31) + b"bold" + sgr(22) + b"notbold"
    s += at(6, 1) + sgr(0, 5, 33) + b"blink" + sgr(25) + b"steady"
    s += at(7, 1) + sgr(0, 7) + b"reverse" + sgr(27) + b"normal"
    # Reverse is a FLAG on logical state, so the 31 lands on the FOREGROUND
    # and this row is black on RED. A destructive parser draws blue on white
    # here, which is the whole reason the row exists.
    s += at(8, 1) + sgr(0, 7, 31) + b"REV-THEN-31-IS-BLACK-ON-RED"
    s += at(9, 1) + sgr(0, 1, 7) + b"REV-KEEPS-BOLD"
    # The four check values of the contract, in the order it lists them.
    s += at(19, 1) + sgr(0, 7) + b"0;7=70" + sgr(0, 1, 7) + b"1;7=78" \
        + sgr(0, 7, 31) + b"7;31=10" + sgr(0, 44, 8) + b"44;8=44" + sgr(0)
    s += at(20, 1) + sgr(0, 44, 8) + b"concealed" + sgr(28) + b"and-28-back" \
        + sgr(0)
    s += at(10, 1) + sgr(0, 44) + b"vis" + sgr(8) + b"hidden" + sgr(0) + b"vis"
    s += at(11, 1)
    for c in range(90, 98):                    # the bright foregrounds
        s += sgr(0, c) + b"%d" % c
    s += at(12, 1)
    for c in range(100, 108):                  # ...and the bright backgrounds
        s += sgr(0, 30, c) + b"%d" % c
    s += at(13, 1) + sgr(0, 1, 31, 44) + b"set" + sgr(39) + b"fg39" + sgr(49) \
        + b"bg49"
    s += at(14, 1) + sgr(0, 2, 3, 6, 9, 21, 28, 38, 48, 99) + b"all-ignored"
    s += at(15, 1) + sgr(0, 4) + b"underline-is-not-in-the-attribute"
    s += at(16, 1) + CSI + b"m" + b"bare-m-is-a-reset"
    s += at(17, 1) + sgr(0, 1, 1, 1, 1, 1, 1, 31, 44) \
        + b"ninth-param-goes-to-a-sink-the-first-eight-still-apply"
    s += at(18, 1) + sgr(0) + CSI + b"99999;1m" + b"saturated-at-255"
    return s


def f_wrap():
    """The wrap rule, which is the one place a terminal's arithmetic shows.
    A glyph in column 80 stays visible and does not move the cursor; the next
    GLYPH wraps; any cursor motion cancels it; and at the bottom right the
    screen scrolls."""
    s = CSI + b"2J" + sgr(0)
    s += at(1, 1) + b"1" + b"." * 78 + b"8"    # a full row, no wrap yet
    s += b"W"                                   # ...and here is the wrap
    s += at(3, 80) + b"X" + at(3, 80) + b"Y"   # motion cancels the pending
    s += at(5, 76) + b"abcde"                  # five into four columns
    s += at(7, 80) + b"Z" + b"\r" + b"c"       # CR cancels it too
    s += at(9, 80) + b"P" + b"\x08" + b"q"     # ...and so does BS
    s += at(11, 1) + b"long: " + b"#" * 160    # two full wraps in a run
    s += at(20, 1) + b"tabs:" + b"\t1\t2\t3\t4"
    s += at(21, 74) + b"\tclamped"             # a tab past the last stop
    s += at(25, 80) + b"S"                     # the bottom-right corner...
    s += b"T"                                   # ...and the scroll it causes
    return s


def f_edit():
    """IL, DL, ICH, DCH and ECH. Each is run over a numbered ladder so that a
    row or a character moving the wrong distance is legible."""
    s = CSI + b"2J" + sgr(0)
    for r in range(1, 13):
        s += at(r, 1) + b"row%02d ABCDEFGHIJKLMNOPQRSTUVWXYZ" % r
    s += at(3, 1) + CSI + b"2L"                # insert 2 rows at row 3
    s += at(9, 1) + CSI + b"2M"                # delete 2 rows at row 9
    s += at(12, 7) + CSI + b"5@"               # open a 5-cell gap
    s += at(13, 7) + CSI + b"5P"               # ...and close one
    s += at(14, 7) + CSI + b"5X"               # ...and blank five in place
    s += at(16, 1) + sgr(0, 37, 41) + CSI + b"3@"   # a gap fills with the
    s += at(17, 1) + b"IL/DL clamp at the bottom:"  # CURRENT attribute
    s += at(24, 1) + CSI + b"20L"              # insert more rows than fit
    s += at(23, 1) + CSI + b"20M"              # ...and delete more than fit
    return s


def f_scroll():
    """LF at the bottom row, SU and SD. The vacated rows take the CURRENT
    attribute, which is why the last block sets a background first."""
    s = CSI + b"2J" + sgr(0)
    for r in range(1, 26):
        s += at(r, 1) + b"line %02d" % r
    s += at(25, 1) + b"\n"                     # one LF at the bottom: scroll
    s += CSI + b"3S"                            # three more, by SU
    s += CSI + b"2T"                            # ...and two back, by SD
    s += sgr(0, 37, 44) + at(25, 1) + b"\n\n"  # two more on a blue ground
    s += at(1, 60) + b"| scrolled"
    return s


def f_save():
    """SCP/RCP and their ESC 7 / ESC 8 spellings, interleaved so that a
    parser that keeps one saved position rather than two is caught."""
    s = CSI + b"2J" + sgr(0)
    s += at(6, 6) + CSI + b"s"                 # save by CSI
    s += at(1, 1) + b"top-left"
    s += CSI + b"u" + b"SAVED-BY-CSI"          # ...and come back
    s += at(9, 9) + b"\x1b7"                   # save by ESC 7
    s += at(2, 1) + b"second-row"
    s += b"\x1b8" + b"SAVED-BY-ESC7"           # ...and come back
    s += at(12, 1) + b"a restore cancels a pending wrap:"
    s += at(14, 40) + CSI + b"s" + at(14, 80) + b"E" + CSI + b"u" + b"R"
    # NEITHER save touches the attribute, and both write the SAME slot - so
    # a CSI s after an ESC 7 is what an ESC 8 comes back to.
    s += at(16, 1) + sgr(0, 41) + b"\x1b7" + sgr(0, 42) + b"\x1b8" \
        + b"NEITHER SAVE CARRIES THE COLOUR"
    s += at(18, 1) + b"\x1b7" + at(20, 30) + CSI + b"s" + at(1, 1) \
        + b"\x1b8" + b"ONE SLOT: CSI-s WON"
    return s


def f_report():
    """DSR 6, DSR 5 and DA. Nothing lands on the screen but the labels; the
    fixture's whole point is the `answers` value beside it, which is what the
    terminal transmits back and what the test server logs."""
    s = CSI + b"2J" + sgr(0)
    s += at(1, 1) + b"DSR6 at 1,1:" + CSI + b"6n"
    s += at(12, 34) + b"DSR6:" + CSI + b"6n"
    s += at(25, 80) + CSI + b"6n"              # the far corner, 1-based
    s += at(3, 1) + b"DSR5:" + CSI + b"5n"
    s += at(4, 1) + b"DA:" + CSI + b"c"
    s += at(5, 1) + b"DA 0:" + CSI + b"0c"
    s += at(6, 1) + b"private, unanswered:" + CSI + b"?c" + CSI + b"?6n"
    s += at(7, 1) + b"unknown final, ignored:" + CSI + b"5Z"
    return s


def f_swallow():
    """What is consumed and never drawn: ANSI music, OSC, DCS, APC, an ESC
    followed by a byte that means nothing, a private sequence, an
    intermediate byte, and a CSI aborted by CAN and by SUB. Every one of them
    is followed by a letter, and the letters spell what survived."""
    s = CSI + b"2J" + sgr(0)
    s += at(1, 1) + CSI + b"MMFcdefgab\x0e" + b"m"        # ANSI music
    s += at(2, 1) + CSI + b"NT180O2L8\x0e" + b"n"         # ...the N form
    s += at(3, 1) + b"\x1b]0;a window title\x07" + b"o"   # OSC to BEL
    s += at(4, 1) + b"\x1bPq#0;2;0;0;0\x1b\\" + b"s"      # DCS to ST
    s += at(5, 1) + b"\x1b_an application command\x1b\\" + b"c"
    s += at(6, 1) + b"\x1bZ" + b"e"                       # ESC + a stray byte
    s += at(7, 1) + CSI + b"?7h" + CSI + b"?33l" + b"p"   # private, ignored
    s += at(8, 1) + CSI + b"=3h" + b"q"                   # the '=' prefix
    s += at(9, 1) + CSI + b"1 q" + b"i"                   # an intermediate
    s += at(10, 1) + CSI + b"41\x18m" + b"C"              # CAN aborts it
    s += at(11, 1) + CSI + b"41\x1am" + b"S"              # SUB aborts it
    s += at(12, 1) + CSI + b"41\x1b[1;40H" + b"E"         # ESC restarts it
    s += at(13, 1) + b"a C0 inside a CSI runs:" + CSI + b"1;\r30H" + b"X"
    s += at(14, 1) + b"?25l hides the cursor:" + CSI + b"?25l"
    s += at(15, 1) + b"ESC ESC restarts, so this CUP still runs:" \
        + b"\x1b\x1b[15;45H" + b"R"
    s += at(16, 1) + b"a control inside an ESC runs and stays there:" \
        + b"\x1b\r[16;50H" + b"c"
    s += at(17, 1) + b"a string's ESC hands the byte on: " \
        + b"\x1b]title\x1b[32m" + b"GREEN" + sgr(0)
    s += at(18, 1) + b"CAN gets out of a music string:" \
        + CSI + b"MMFcdefg\x18" + b"k"
    return s


def f_cp437():
    """The CP437 house rule: bytes 0x80-0xFF are GLYPHS and never C1
    controls, and so is every C0 byte that is not one of the seven this
    terminal answers to. The block-drawing range 0xB0-0xDF is what BBS art is
    actually made of, so it gets a row of its own."""
    s = CSI + b"2J" + sgr(0)
    s += at(1, 1) + b"C0 glyphs (00 and 07-0D and 18 1A 1B excepted):"
    s += at(2, 1) + bytes(b for b in range(0x01, 0x20)
                          if b not in (0x07, 0x08, 0x09, 0x0A, 0x0C, 0x0D,
                                       0x18, 0x1A, 0x1B))
    s += at(3, 1) + b"7F is a glyph too: \x7f"
    for i, base in enumerate(range(0x20, 0x100, 0x20)):
        s += at(5 + i, 1) + b"%02X " % base + bytes(range(base, base + 0x20))
    s += at(14, 1) + b"blocks on colour:"
    for i, c in enumerate(range(40, 48)):
        s += at(15, 1 + i * 9) + sgr(0, 37, c) + b"\xb0\xb1\xb2\xdb\xdc\xdd\xde\xdf"
    s += sgr(0)
    return s


def f_art():
    """A small ANSI-art scene, drawn geometrically by this tool: a double
    box-drawing frame, a title on a reverse bar, a shaded gradient, and a
    four-colour block strip. It is the only fixture that looks like a BBS
    screen, and it exists so that the byte-for-byte comparison has one case
    where a human can SEE that the answer is right."""
    W, H = 60, 16
    TL, TR, BL, BR, HZ, VT = 0xC9, 0xBB, 0xC8, 0xBC, 0xCD, 0xBA
    s = CSI + b"2J" + sgr(0, 37, 44)
    s += at(2, 10) + bytes([TL]) + bytes([HZ]) * (W - 2) + bytes([TR])
    for r in range(3, 3 + H - 2):
        s += at(r, 10) + bytes([VT]) + b" " * (W - 2) + bytes([VT])
    s += at(2 + H - 1, 10) + bytes([BL]) + bytes([HZ]) * (W - 2) + bytes([BR])
    title = b" os8088 :: THE BBS TERMINAL "
    s += at(3, 10 + (W - len(title)) // 2) + sgr(0, 1, 7) + title
    s += sgr(0, 37, 44)
    s += at(5, 14) + sgr(0, 1, 33) + b"Main Menu"
    menu = ((b"M", b"Message Bases"), (b"F", b"File Areas"),
            (b"D", b"Download  (Zmodem)"), (b"G", b"Goodbye"))
    for i, (key, item) in enumerate(menu):
        s += at(7 + i, 14) + sgr(0, 1, 37) + b"[" + sgr(1, 36) + key \
            + sgr(0, 1, 37) + b"] " + sgr(0, 37, 44) + item
    s += at(12, 14) + sgr(0, 33) + b"gradient "
    for ch in (0xB0, 0xB1, 0xB2, 0xDB):
        s += bytes([ch]) * 6
    s += at(13, 14) + sgr(0) + b"blocks   "
    for c in (41, 43, 42, 46, 44, 45):
        s += sgr(0, c) + b"  "
    s += sgr(0, 37, 44)
    s += at(15, 14) + sgr(0, 1, 30, 44) + b"dark grey on blue is the INVERSE case"
    s += at(2 + H, 10) + sgr(0) + b"Command: "
    return s


def f_zmodem():
    """The Zmodem auto-start trigger arriving in the middle of a page. The
    fixture's rendering is what the screen holds AT the handover: two stars,
    a B and a nought, the 0x18 having been consumed as CAN and the final nought
    never drawn. Everything after the trigger is the receiver's."""
    s = CSI + b"2J" + sgr(0)
    s += at(1, 1) + b"Sending FILE.ZIP by Zmodem. Press Ctrl-X twice to abort."
    s += at(2, 1) + b"**\x18B00000000226ed2\r\n"
    s += b"this text is never seen by the terminal: it is Zmodem's"
    return s


def f_torture():
    """Every family at once, in one stream, with sequences deliberately
    adjacent so that a state machine that leaks one byte of state between
    them shows it. This is the fixture the ragged-fragment server sends."""
    return (f_cursor() + f_sgr() + f_wrap() + f_edit() + f_scroll()
            + f_save() + f_swallow() + f_cp437() + f_art() + f_erase())


FIXTURES = (
    ("cursor", f_cursor),
    ("erase", f_erase),
    ("sgr", f_sgr),
    ("wrap", f_wrap),
    ("edit", f_edit),
    ("scroll", f_scroll),
    ("save", f_save),
    ("report", f_report),
    ("swallow", f_swallow),
    ("cp437", f_cp437),
    ("art", f_art),
    ("zmodem", f_zmodem),
    ("torture", f_torture),
)


# =============================================================================
# THE TWO ENCODINGS
# =============================================================================
def rle(buf):
    """[[count, value], ...]. Exact and reversible; a blank 80x25 screen is
    two pairs rather than 4,000 numbers."""
    out = []
    for b in bytearray(buf):
        if out and out[-1][1] == b and out[-1][0] < 0xFFFF:
            out[-1][0] += 1
        else:
            out.append([1, b])
    return out


def unrle(pairs):
    out = bytearray()
    for n, v in pairs:
        out += bytes([v]) * n
    return bytes(out)


def rle_text(buf):
    """The same thing for a person: `12*07 4*17` and so on."""
    return " ".join("%d*%02x" % (n, v) for n, v in rle(buf))


def render_txt(name, doc, stream, scr):
    st = scr.state_dict()
    L = []
    L.append("fixture: %s" % name)
    for line in doc.strip().split("\n"):
        L.append("  " + " ".join(line.split()))
    L.append("")
    L.append("stream:  %d bytes" % len(stream))
    L.append("cursor:  row %d col %d (0-based)   attribute %02X   %d glyph(s)"
             % (st["row"], st["col"], st["attr"], st["glyphs"]))
    L.append("state:   %s   bells %d   scrolls %d"
             % (st["state"], st["bells"], st["scrolls"]))
    L.append("cursor visible: %s" % st["cursor_visible"])
    if st["answers"]:
        L.append("answers: %s" % repr(st["answers"].encode("latin-1")))
    if st["zmodem_at"] is not None:
        L.append("zmodem:  the receiver takes over at stream offset %d"
                 % st["zmodem_at"])
    L.append("")
    L.append("    +" + "-" * ansisim.COLS + "+")
    for r in range(ansisim.ROWS):
        L.append("%3d |%s|" % (r, scr.line(r)))
    L.append("    +" + "-" * ansisim.COLS + "+")
    L.append("")
    L.append("attributes, run-length encoded, one row per line:")
    for r in range(ansisim.ROWS):
        row = scr.attrs[r * ansisim.COLS:(r + 1) * ansisim.COLS]
        L.append("%3d %s" % (r, rle_text(row)))
    return "\n".join(L) + "\n"


def render_json(name, doc, stream, scr):
    st = scr.state_dict()
    return json.dumps(dict(
        name=name,
        what=" ".join(doc.split()),
        stream_bytes=len(stream),
        stream_sha256=hashlib.sha256(stream).hexdigest(),
        cols=ansisim.COLS, rows=ansisim.ROWS,
        chars_rle=rle(scr.chars),
        attrs_rle=rle(scr.attrs),
        raw_sha256=hashlib.sha256(scr.raw()).hexdigest(),
        cursor=[st["row"], st["col"]],
        attr=st["attr"],
        cursor_visible=st["cursor_visible"],
        answers=list(bytearray(scr.answers)),
        bells=st["bells"],
        scrolls=st["scrolls"],
        glyphs=st["glyphs"],
        end_state=st["state"],
        zmodem_at=st["zmodem_at"],
    ), indent=1, sort_keys=True) + "\n"


README = """\
Synthetic ANSI-BBS fixtures for the os8088 terminal.

WRITTEN BY tools/ansifix.py - never by hand. Change the builder, run
`python3 tools/ansifix.py`, and commit what it wrote; `--check` fails if the
tree and the tool disagree. Nothing here is third-party: every byte is drawn
by a line of Python in that tool, including the one fixture that looks like
artwork.

  <name>.bin   the byte stream a BBS would send
  <name>.txt   what tools/ansisim.py makes of it, for a person: the screen as
               CP437 text, then the attribute plane run-length encoded
  <name>.json  the same for a test: the buffer run-length encoded (`chars_rle`
               and `attrs_rle`, [count, value] pairs), the cursor, what the
               terminal answered, and a SHA-256 of the 4,000 bytes the guest
               holds as char,attr,char,attr,...

A test rebuilds the expected buffer from the .json and compares it with guest
memory. It does not have to run the simulator to do that, which is the point:
the fixture is an oracle, not a cache.
"""


def build():
    """{relative path: bytes}. Nothing touches the disk."""
    out = {"README.txt": README.encode("utf-8")}
    for name, fn in FIXTURES:
        stream = fn()
        scr = ansisim.render(stream)
        doc = fn.__doc__ or ""
        out[name + ".bin"] = stream
        out[name + ".txt"] = render_txt(name, doc, stream, scr).encode("utf-8")
        out[name + ".json"] = render_json(name, doc, stream, scr).encode("utf-8")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default=OUTDIR, help="where to write (default %s)"
                    % os.path.relpath(OUTDIR, ROOT))
    ap.add_argument("--check", action="store_true",
                    help="compare with what is on disk and fail on a difference")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()

    files = build()

    if args.list:
        for name, fn in FIXTURES:
            print("%-10s %5d bytes  %s"
                  % (name, len(files[name + ".bin"]),
                     " ".join((fn.__doc__ or "").split())[:60]))
        return 0

    if args.check:
        bad = 0
        for rel in sorted(files):
            path = os.path.join(args.out, rel)
            try:
                with open(path, "rb") as fh:
                    have = fh.read()
            except OSError:
                print("ansifix: %s is MISSING" % rel, file=sys.stderr)
                bad += 1
                continue
            if have != files[rel]:
                print("ansifix: %s differs from what the tool writes" % rel,
                      file=sys.stderr)
                bad += 1
        extra = [f for f in sorted(os.listdir(args.out))
                 if os.path.isfile(os.path.join(args.out, f))
                 and f not in files] if os.path.isdir(args.out) else []
        for f in extra:
            print("ansifix: %s is in the tree and not in the tool" % f,
                  file=sys.stderr)
            bad += 1
        print("ansifix --check: %d files, %d problem(s)" % (len(files), bad))
        return 1 if bad else 0

    if not os.path.isdir(args.out):
        os.makedirs(args.out)
    written = 0
    for rel in sorted(files):
        path = os.path.join(args.out, rel)
        try:
            with open(path, "rb") as fh:
                if fh.read() == files[rel]:
                    continue
        except OSError:
            pass
        with open(path, "wb") as fh:
            fh.write(files[rel])
        written += 1
    total = sum(len(v) for v in files.values())
    print("ansifix: %d files, %d written, %d unchanged, %d bytes"
          % (len(files), written, len(files) - written, total))
    return 0


if __name__ == "__main__":
    sys.exit(main())
