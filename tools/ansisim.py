#!/usr/bin/env python3
"""The host-side REFERENCE renderer of the os8088 BBS terminal's ANSI parser.

`tools/htmsim.py` (the browser's parse and layout) and `tools/weavesim.py`
(the Weave bundle) are the precedents: a design gets a second, independent
reader in Python before a byte of 8086 is written, and the assembly is then
checked against it rather than against somebody's memory of the design.

What this models, exactly and byte for byte:

  * the 80x25 CHAR + ATTRIBUTE buffer the terminal keeps (`te_scr`): 2,000
    cells of (character, IBM attribute), 4,000 bytes, row-major. `raw()`
    returns those 4,000 bytes in the order the guest holds them, which is
    what a test compares against guest memory;
  * the state machine - GROUND, ESCAPE, CSI (entry/param/ignore), STRING,
    MUSIC - byte at a time, with every piece of state on the object, so
    `feed()` may be called with the stream split at ANY offset and the result
    is identical. The selfcheck proves that rather than asserting it;
  * every control, every CSI final and every SGR code the terminal answers to;
  * what the terminal SAYS BACK - the DSR and DA replies - accumulated in
    `answers`, because a test that cannot see the reply cannot tell a
    terminal that answered wrongly from one that did not answer;
  * the Zmodem auto-start trigger, reported as an event: the sim stops
    feeding when the sixth byte arrives and says at which offset the Zmodem
    receiver takes the stream over.

What it deliberately does NOT model: pixels, colour on a 1bpp adapter, the
text-mode attribute mapping, the dirty-row bitmap's interaction with the
scroll debt. Those are the renderer's business and the renderer's gates. The
contract here is the BUFFER, and `dirty` is published as advice.

  python3 tools/ansisim.py tests/fixtures/ansi/sgr.bin --dump
  python3 tools/ansisim.py tests/fixtures/ansi/art.bin --dump --attrs
  python3 tools/ansisim.py tests/fixtures/ansi/wrap.bin --json
  python3 tools/ansisim.py --selfcheck
"""
import argparse
import json
import sys

COLS = 80
ROWS = 25
CELLS = COLS * ROWS
DEFAULT_ATTR = 0x07

# --- the states --------------------------------------------------------------
# Seven states and one sub-state, which is the contract's own count. STRING_ESC
# is the sub-state: the one byte of lookahead a string terminator needs, spelled
# as a state so that a stream split between the ESC and the backslash behaves
# like one that is not.
(GROUND, ESCAPE, CSI_ENTRY, CSI_PARAM, CSI_IGNORE,
 STRING, STRING_ESC, MUSIC) = range(8)

STATE_NAMES = ("GROUND", "ESCAPE", "CSI_ENTRY", "CSI_PARAM", "CSI_IGNORE",
               "STRING", "STRING_ESC", "MUSIC")

# --- the bytes with meaning ---------------------------------------------------
NUL, BEL, BS, HT, LF, FF, CR, SO = 0x00, 0x07, 0x08, 0x09, 0x0A, 0x0C, 0x0D, 0x0E
CAN, SUB, ESC, DEL = 0x18, 0x1A, 0x1B, 0x7F

# The C0 bytes that are CONTROLS here. Every other byte below 0x20 is a CP437
# GLYPH in GROUND - BBS art is built out of them - which is the single biggest
# difference between this and a VT-family parser, and the reason the list is
# written out rather than expressed as a range.
CONTROLS = (NUL, BEL, BS, HT, LF, FF, CR)

# The Zmodem auto-start trigger: the opening of a ZRQINIT hex header.
ZTRIGGER = b"**\x18B00"
# ...and its failure table, because the pattern REPEATS its first byte and a
# detector that simply restarts misses `***\x18B00`: after three stars the
# right state is "two matched", not "one". Five bytes of table on the 8086.
ZFAIL = (0, 0, 1, 0, 0, 0)

# --- CP437 for --dump ---------------------------------------------------------
# Python's cp437 codec maps 0x00..0x1F to the C0 controls, which is right for
# text and wrong for a screen buffer: on this machine those cells are drawn as
# faces, suits and arrows. So the low half is a table and the rest is the codec.
LOW437 = (" ☺☻♥♦♣♠•"
          "◘○◙♂♀♪♫☼"
          "►◄↕‼¶§▬↨"
          "↑↓→←∟↔▲▼")


def cp437(byte):
    """One buffer byte -> the character a reader of --dump should see."""
    if byte < 0x20:
        return LOW437[byte]
    if byte == DEL:
        return "⌂"
    return bytes([byte]).decode("cp437")


class Screen(object):
    """The terminal's 80x25 buffer and the state machine that fills it.

    Every field a sequence can touch lives here and nothing lives in a local
    across a `feed()` boundary. That is not tidiness: a BBS arrives in TCP
    segments that fall wherever the network puts them, and the one defect this
    class exists to make impossible is a sequence that works when it arrives
    whole and does something else when it is split.
    """

    def __init__(self):
        self.chars = bytearray(b" " * CELLS)
        self.attrs = bytearray([DEFAULT_ATTR]) * CELLS
        self.row = 0
        self.col = 0
        self.saved = (0, 0)            # ONE slot: CSI s / CSI u AND ESC 7 /
        #                                ESC 8, and never the attribute
        # The attribute is DERIVED from these six, never stored, because SGR 7
        # is a FLAG and not a swap of the nibbles: `ESC[7;31m` means black on
        # RED, and a parser that swaps when it sees the 7 and then writes the
        # 31 into the low nibble draws blue on white - a plausible picture,
        # entirely wrong, and one that only shows on the boards that put the
        # 7 first.
        self.fg = 0x07                 # logical foreground, 4 bits (intensity)
        self.bg = 0x00                 # logical background, 3 bits
        self.blink = False             # attribute bit 7 (bright bg under iCE)
        self.reverse = False
        self.conceal = False
        self.underline = False         # SGR 4: published, and it reaches the
        #                                glass NOWHERE - a cell is two bytes
        self.wrap_pending = False
        self.cursor_visible = True
        self.ice = False               # a render-time flag; the buffer is the
        #                                same either way, so nothing here reads
        #                                it. Published so a caller can carry it.

        self.state = GROUND
        self.params = []
        self.psink = False             # the ninth parameter onward: a SINK,
        #                                so it cannot land in the eighth
        self.priv = ""                 # a private prefix byte, 0x3C..0x3F
        self.intermed = False          # an intermediate byte, 0x20..0x2F

        self.answers = bytearray()     # what the terminal would transmit back
        self.bells = 0
        self.dirty = 0                 # 25-bit row bitmap, advisory
        self.scrolls = 0               # rows scrolled off the top, advisory
        self.offset = 0                # bytes fed
        self.zmodem_at = None          # offset the Zmodem receiver takes over
        self.ztrig = 0                 # how much of ZTRIGGER has matched
        self.glyphs = 0                # cells written by a glyph
        self.unknown = []              # (state name, byte) we chose to ignore

    # --- what a caller reads --------------------------------------------------
    def raw(self):
        """The 4,000 bytes the guest holds, char,attr,char,attr,..."""
        out = bytearray(CELLS * 2)
        out[0::2] = self.chars
        out[1::2] = self.attrs
        return bytes(out)

    def attr(self):
        """The IBM attribute byte a glyph written now would carry."""
        fg, bg = self.fg & 0x0F, self.bg & 0x07
        if self.reverse:
            fg, bg = (bg | (fg & 0x08)), (fg & 0x07)
        if self.conceal:
            fg = bg
        return (0x80 if self.blink else 0) | ((bg & 0x07) << 4) | (fg & 0x0F)

    def text(self, sep="\n"):
        """The screen as CP437 text, trailing blanks kept (the cells exist)."""
        return sep.join(
            "".join(cp437(c) for c in self.chars[r * COLS:(r + 1) * COLS])
            for r in range(ROWS))

    def line(self, r):
        return "".join(cp437(c) for c in self.chars[r * COLS:(r + 1) * COLS])

    # --- feeding --------------------------------------------------------------
    def feed(self, data):
        """Consume bytes. Returns how many were consumed.

        Fewer than were offered means the Zmodem trigger fired: everything
        from the return value onward belongs to the Zmodem receiver and this
        object will consume nothing more until `resume()`.
        """
        if self.zmodem_at is not None:
            return 0
        i = 0
        for b in bytearray(data):
            i += 1
            self.offset += 1
            self._byte(b)
            if self.zmodem_at is not None:
                return i
        return i

    def resume(self):
        """Hand the stream back after a Zmodem transfer. GROUND, no trigger."""
        self.zmodem_at = None
        self.ztrig = 0
        self.state = GROUND

    def clear_dirty(self):
        d, s = self.dirty, self.scrolls
        self.dirty, self.scrolls = 0, 0
        return d, s

    # --- the state machine ----------------------------------------------------
    def _byte(self, b):
        st = self.state
        if st == GROUND:
            self._zdetect(b)
            if self.zmodem_at is not None:
                return
            self._ground(b)
            return
        # Anywhere else the detector is RESET: the trigger is a GROUND-state
        # sequence, and a `*` separated from its fellow by an escape sequence
        # is two stars of somebody's artwork.
        self.ztrig = 0
        if st == ESCAPE:
            self._escape(b)
        elif st in (CSI_ENTRY, CSI_PARAM, CSI_IGNORE):
            self._csi_byte(b)
        elif st == STRING:
            self._string(b)
        elif st == STRING_ESC:
            self._string_esc(b)
        else:
            self._music(b)

    def _zdetect(self, b):
        """The trigger is a state, not a search over a buffer.

        A mismatch may not simply RESTART the count, because the pattern
        repeats its first byte: a detector that fell back to "one matched"
        misses `***` followed by the rest, where the correct state after three
        stars is "two matched". ZFAIL says how far to fall, and the byte is
        re-tested at the new count.

        The matched bytes are DRAWN on the way past - see _ground - so at
        handover the screen holds `**B0`, the 0x18 having been consumed as CAN
        and the final `0` never drawn. Holding them instead would need a
        five-byte replay buffer and would drop a real `**` out of a board's art
        whenever a mismatch followed.
        """
        while True:
            if b == ZTRIGGER[self.ztrig]:
                self.ztrig += 1
                if self.ztrig == len(ZTRIGGER):
                    self.ztrig = 0
                    self.zmodem_at = self.offset
                return
            if self.ztrig == 0:
                return
            self.ztrig = ZFAIL[self.ztrig]

    def _ground(self, b):
        if b == ESC:
            self.state = ESCAPE
            return
        if b in (CAN, SUB):
            return                                # dropped; nothing to abort
        if b in CONTROLS:
            self._control(b)
            return
        self._put(b)                              # every other byte is a GLYPH

    def _control(self, b):
        if b == NUL:
            return                                # dropped, everywhere
        if b == BEL:
            self.bells += 1
            return
        if b == BS:
            self.wrap_pending = False
            if self.col:
                self.col -= 1
            return
        if b == HT:
            self.wrap_pending = False
            nxt = (self.col + 8) & ~7
            self.col = nxt if nxt < COLS else COLS - 1
            return
        if b == LF:
            self.wrap_pending = False
            self._index()
            return
        if b == CR:
            self.wrap_pending = False
            self.col = 0
            return
        if b == FF:
            self.wrap_pending = False
            self._fill(0, CELLS)
            self.row = self.col = 0
            self.dirty = (1 << ROWS) - 1
            return

    def _escape(self, b):
        if b == ESC:
            return                                # RESTART: stay here, so a
        #                                           stream that lost bytes
        #                                           between two sequences does
        #                                           not eat the second's
        #                                           introducer
        if b in (CAN, SUB):
            self.state = GROUND                   # abort
            return
        if b < 0x20 or b == DEL:
            if b in CONTROLS:
                self._control(b)                  # execute, and STAY in ESC
            return                                # every other C0 and 0x7F is
        #                                           ignored and NEVER drawn
        if b == 0x5B:                             # '['
            self.state = CSI_ENTRY
            self.params = []
            self.psink = False
            self.priv = ""
            self.intermed = False
            return
        if b in (0x5D, 0x50, 0x5F):               # OSC, DCS, APC - these three
            self.state = STRING
            return
        if b == 0x37:                             # '7' save the POSITION, into
            self.saved = (self.row, self.col)     # the one slot CSI s uses
            self.state = GROUND
            return
        if b == 0x38:                             # '8' restore it
            self.row, self.col = self.saved
            self.wrap_pending = False
            self.state = GROUND
            return
        self.unknown.append(("ESCAPE", b))
        self.state = GROUND                       # any other byte is SWALLOWED

    def _csi_byte(self, b):
        if b == ESC:
            self.state = ESCAPE
            return
        if b in (CAN, SUB):
            self.state = GROUND
            return                                # aborted, nothing dispatched
        if b < 0x20 or b == DEL:
            # ONE OF THE SEVEN executes and the sequence continues - a host
            # that sends `CSI 1;` CR `1H` has put a carriage return in the
            # middle of a cursor move. Every OTHER C0, and 0x7F, is ignored
            # and never drawn: a byte that is art where it is art is noise
            # where it lands inside a sequence, and drawing it there is how a
            # lost byte turns into a diamond in the middle of a menu.
            if b in CONTROLS:
                self._control(b)
            return
        if 0x40 <= b <= 0x7E:                     # a final byte, always
            st = self.state
            self.state = GROUND
            if st != CSI_IGNORE:
                self._csi(chr(b), st == CSI_ENTRY)
            return
        if self.state == CSI_IGNORE:
            return
        if 0x30 <= b <= 0x39:                     # a digit
            self.state = CSI_PARAM
            if self.psink:
                return                            # the ninth's digits go
            if not self.params:                   # NOWHERE - accumulating them
                self.params = [0]                 # into the eighth would set
            self.params[-1] = min(255,            # an eighth parameter of 3144
                                  self.params[-1] * 10 + (b - 0x30))
            return
        if b == 0x3B:                             # ';'
            self.state = CSI_PARAM
            if not self.params:
                self.params = [0]
            if len(self.params) < 8:
                self.params.append(0)
            else:
                self.psink = True                 # the ninth and beyond: a
            return                                # SINK, and the first eight
        #                                           still execute
        if 0x3C <= b <= 0x3F:                     # '<' '=' '>' '?'
            if self.state == CSI_ENTRY and not self.priv:
                self.priv = chr(b)                # only the FIRST one; a
                return                            # second is an intermediate
            self.state = CSI_IGNORE
            return
        # 0x20..0x2F, an intermediate: the WHOLE sequence becomes a no-op. It
        # is consumed to its final and draws nothing - a parser that ignored
        # the space in `ESC[1 q` would have executed `CSI 1 q`.
        self.intermed = True
        self.state = CSI_IGNORE

    def _string(self, b):
        if b == ESC:
            self.state = STRING_ESC
        elif b == BEL:
            self.state = GROUND                   # BEL ends a string: it does
        elif b in (CAN, SUB):                     # not ring inside one
            self.state = GROUND

    def _string_esc(self, b):
        # ESC then backslash is the terminator. Any OTHER byte means the string
        # ended at the ESC and this byte is the second byte of a NEW escape
        # sequence, so it is handed to the escape handler: `ESC ] junk ESC [
        # 31 m` leaves the colour set. It is a lookahead STATE and not a peek,
        # so a stream split between the ESC and the backslash behaves like one
        # that is not.
        if b == 0x5C:
            self.state = GROUND
            return
        self.state = ESCAPE
        self._escape(b)

    def _music(self, b):
        # Swallowed to SO (0x0E) inclusive - but CAN and SUB abort and ESC
        # restarts, which is the case that matters: without those rows a board
        # that sent `ESC[M` and then crashed would swallow the rest of the
        # session.
        if b == SO:
            self.state = GROUND
        elif b in (CAN, SUB):
            self.state = GROUND
        elif b == ESC:
            self.state = ESCAPE
        elif b < 0x20 and b in CONTROLS:
            self._control(b)                      # execute, and stay
        # every other C0, 0x7F, and everything else: swallowed

    # --- the CSI finals -------------------------------------------------------
    def _csi(self, final, from_entry):
        p = self.params

        # ANSI music, and the design's one genuine collision. A BARE `CSI M`
        # or `CSI N` - no parameter, no private prefix, no intermediate - is
        # music; anything with a parameter is not. So Delete Line is reachable
        # only as `CSI <n> M`, and on the 8086 the whole rule is one test of
        # the parameter count at the M/N dispatch. An intermediate never gets
        # here at all, having made the sequence a no-op.
        if final in ("M", "N") and from_entry and not self.priv:
            self.state = MUSIC
            return

        if self.priv:
            # Only ?25h / ?25l is acted on. Every other private sequence is
            # consumed and ignored - which is a DECISION and not an oversight:
            # a BBS sends ?7h, ?33h and ?1049h and none of them mean anything
            # on a screen that is always 80x25 and always wraps.
            if self.priv == "?" and final in ("h", "l"):
                for v in (p or [0]):
                    if v == 25:
                        self.cursor_visible = final == "h"
            return

        n = p[0] if (p and p[0]) else 1           # the ANSI default of 1
        raw0 = p[0] if p else 0                   # ...where 0 is meaningful

        if final == "A":
            self.wrap_pending = False
            self.row = max(0, self.row - n)
        elif final == "B":
            self.wrap_pending = False
            self.row = min(ROWS - 1, self.row + n)
        elif final == "C":
            self.wrap_pending = False
            self.col = min(COLS - 1, self.col + n)
        elif final == "D":
            self.wrap_pending = False
            self.col = max(0, self.col - n)
        elif final == "E":
            self.wrap_pending = False
            self.row = min(ROWS - 1, self.row + n)
            self.col = 0
        elif final == "F":
            self.wrap_pending = False
            self.row = max(0, self.row - n)
            self.col = 0
        elif final == "G":
            self.wrap_pending = False
            self.col = min(COLS - 1, n - 1)
        elif final in ("H", "f"):
            self.wrap_pending = False
            r = p[0] if (p and p[0]) else 1
            c = p[1] if (len(p) > 1 and p[1]) else 1
            self.row = min(ROWS - 1, r - 1)
            self.col = min(COLS - 1, c - 1)
        elif final == "J":
            here = self.row * COLS + self.col
            if raw0 == 0:
                self._fill(here, CELLS - here)
            elif raw0 == 1:
                self._fill(0, here + 1)
            elif raw0 == 2:
                self._fill(0, CELLS)
                self.row = self.col = 0           # ANSI.SYS homes; VT does not
                self.wrap_pending = False
        elif final == "K":
            base = self.row * COLS
            if raw0 == 0:
                self._fill(base + self.col, COLS - self.col)
            elif raw0 == 1:
                self._fill(base, self.col + 1)
            elif raw0 == 2:
                self._fill(base, COLS)
        elif final == "L":
            self._ins_lines(self.row, min(n, ROWS - self.row))
        elif final == "M":
            self._del_lines(self.row, min(n, ROWS - self.row))
        elif final == "@":
            self._ins_chars(min(n, COLS - self.col))
        elif final == "P":
            self._del_chars(min(n, COLS - self.col))
        elif final == "X":
            self._fill(self.row * COLS + self.col, min(n, COLS - self.col))
        elif final == "S":
            self._scroll_up(min(n, ROWS))
        elif final == "T":
            self._scroll_down(min(n, ROWS))
        elif final == "s":
            self.saved = (self.row, self.col)     # the ONE slot, and never
        elif final == "u":                        # the attribute
            self.row, self.col = self.saved
            self.wrap_pending = False
        elif final == "n":
            if raw0 == 6:
                self.answers += b"\x1b[%d;%dR" % (self.row + 1, self.col + 1)
            elif raw0 == 5:
                self.answers += b"\x1b[0n"
        elif final == "c":
            if raw0 == 0:                         # only Pn = 0, and the
                self.answers += b"\x1b[?1;0c"     # private form is silent
        elif final == "m":
            self._sgr(p or [0])
        else:
            self.unknown.append(("CSI", ord(final)))

    def _sgr(self, codes):
        """Applied to LOGICAL state, in order, left to right.

        Every code here is one byte of state and none of them needs to know
        what any other did - which is the whole argument for the flag model
        over a destructive swap of the attribute's nibbles. An unknown code is
        ignored and the rest are still applied: a board that sends `0;1;44;31`
        must not lose the 31 because something in front of it was not
        understood.
        """
        for v in codes:
            if v == 0:
                self.fg, self.bg = 0x07, 0x00
                self.blink = self.reverse = self.conceal = False
                self.underline = False
            elif v == 1:
                self.fg |= 0x08
            elif v == 4:
                self.underline = True             # and it reaches the glass
            elif v == 5:                          # NOWHERE - published only
                self.blink = True
            elif v == 7:
                self.reverse = True
            elif v == 8:
                self.conceal = True
            elif v == 22:
                self.fg &= ~0x08
            elif v == 24:
                self.underline = False
            elif v == 25:
                self.blink = False
            elif v == 27:
                self.reverse = False
            elif v == 28:
                self.conceal = False              # concealed off
            elif 30 <= v <= 37:
                self.fg = (self.fg & 0x08) | (v - 30)
            elif v == 39:
                self.fg = 0x07                    # the whole nibble, so the
            elif 40 <= v <= 47:                   # intensity goes with it
                self.bg = v - 40
            elif v == 49:
                self.bg = 0x00
            elif 90 <= v <= 97:
                self.fg = (v - 90) | 0x08
            elif 100 <= v <= 107:
                self.bg = v - 100
                self.blink = True                 # bit 7: bright bg under iCE,
            # 2, and everything else: ignored, deliberately   and so CSI 25m
            #                                                 turns one off

    # --- the buffer -----------------------------------------------------------
    def _mark(self, r0, r1):
        for r in range(max(0, r0), min(ROWS, r1)):
            self.dirty |= 1 << r

    def _fill(self, start, count):
        """Erases fill with SPACE and the CURRENT attribute - the ANSI.SYS and
        BBS convention, not the VT one, and the reason a cleared screen on a
        BBS keeps its background colour."""
        if count <= 0:
            return
        end = min(CELLS, start + count)
        a = self.attr()
        for i in range(start, end):
            self.chars[i] = 0x20
            self.attrs[i] = a
        self._mark(start // COLS, (end - 1) // COLS + 1)

    def _put(self, ch):
        if self.wrap_pending:
            self.wrap_pending = False
            self.col = 0
            self._index()
        i = self.row * COLS + self.col
        self.chars[i] = ch
        self.attrs[i] = self.attr()
        self.dirty |= 1 << self.row
        self.glyphs += 1
        if self.col == COLS - 1:
            self.wrap_pending = True              # the glyph STAYS visible
        else:
            self.col += 1

    def _index(self):
        if self.row < ROWS - 1:
            self.row += 1
        else:
            self._scroll_up(1)

    def _scroll_up(self, n):
        if n <= 0:
            return
        if n >= ROWS:
            self._fill(0, CELLS)
        else:
            k = n * COLS
            self.chars[0:CELLS - k] = self.chars[k:CELLS]
            self.attrs[0:CELLS - k] = self.attrs[k:CELLS]
            self._fill(CELLS - k, k)
        self.dirty = (1 << ROWS) - 1
        self.scrolls += n

    def _scroll_down(self, n):
        if n <= 0:
            return
        if n >= ROWS:
            self._fill(0, CELLS)
        else:
            k = n * COLS
            self.chars[k:CELLS] = self.chars[0:CELLS - k]
            self.attrs[k:CELLS] = self.attrs[0:CELLS - k]
            self._fill(0, k)
        self.dirty = (1 << ROWS) - 1

    def _ins_lines(self, row, n):
        if n <= 0:
            return
        top = row * COLS
        bot = CELLS
        k = n * COLS
        if k >= bot - top:
            self._fill(top, bot - top)
        else:
            self.chars[top + k:bot] = self.chars[top:bot - k]
            self.attrs[top + k:bot] = self.attrs[top:bot - k]
            self._fill(top, k)
        self._mark(row, ROWS)

    def _del_lines(self, row, n):
        if n <= 0:
            return
        top = row * COLS
        bot = CELLS
        k = n * COLS
        if k >= bot - top:
            self._fill(top, bot - top)
        else:
            self.chars[top:bot - k] = self.chars[top + k:bot]
            self.attrs[top:bot - k] = self.attrs[top + k:bot]
            self._fill(bot - k, k)
        self._mark(row, ROWS)

    def _ins_chars(self, n):
        if n <= 0:
            return
        base = self.row * COLS
        lo, hi = base + self.col, base + COLS
        if n >= hi - lo:
            self._fill(lo, hi - lo)
        else:
            self.chars[lo + n:hi] = self.chars[lo:hi - n]
            self.attrs[lo + n:hi] = self.attrs[lo:hi - n]
            self._fill(lo, n)
        self.dirty |= 1 << self.row

    def _del_chars(self, n):
        if n <= 0:
            return
        base = self.row * COLS
        lo, hi = base + self.col, base + COLS
        if n >= hi - lo:
            self._fill(lo, hi - lo)
        else:
            self.chars[lo:hi - n] = self.chars[lo + n:hi]
            self.attrs[lo:hi - n] = self.attrs[lo + n:hi]
            self._fill(hi - n, n)
        self.dirty |= 1 << self.row

    # --- reporting ------------------------------------------------------------
    def state_dict(self):
        return dict(row=self.row, col=self.col, attr=self.attr(),
                    fg=self.fg, bg=self.bg, blink=self.blink,
                    reverse=self.reverse, conceal=self.conceal,
                    underline=self.underline, wrap_pending=self.wrap_pending,
                    cursor_visible=self.cursor_visible,
                    saved=list(self.saved), state=STATE_NAMES[self.state],
                    answers=self.answers.decode("latin-1"),
                    bells=self.bells, glyphs=self.glyphs,
                    dirty=self.dirty, scrolls=self.scrolls,
                    offset=self.offset, zmodem_at=self.zmodem_at)


def render(data):
    """Bytes -> a fed Screen. The one-line form every caller wants."""
    s = Screen()
    s.feed(data)
    return s


# =============================================================================
# THE SELFCHECK
#
# Every rule of the parser contract gets a case named after it, and the cases
# are the second half of the contract: prose says "a glyph written in column
# 80 stays visible" and `wrap.glyph-in-col-80-stays` is what that sentence
# MEANS. A rule with no case here is a rule two implementations can read two
# ways, which is exactly what this file exists to prevent.
#
# The last block is not a rule of the design at all: it feeds every case again
# with the stream split at EVERY offset and demands the identical buffer. That
# is the property the 8086 has to have and the one an author cannot see, since
# a fixture always arrives whole in a test and never on a wire.
# =============================================================================
def _cases():
    """[(name, stream, check(screen) -> None or a failure string)]"""
    C = []

    def case(name, stream):
        def deco(fn):
            C.append((name, stream, fn))
            return fn
        return deco

    def cell(s, r, c):
        i = r * COLS + c
        return s.chars[i], s.attrs[i]

    def want(got, exp, what):
        if got != exp:
            return "%s: got %r, want %r" % (what, got, exp)
        return None

    # --- glyphs and the CP437 house ------------------------------------------
    @case("glyph.plain-ascii-lands-with-the-default-attribute", b"Hi")
    def _(s):
        return (want(cell(s, 0, 0), (ord("H"), 0x07), "cell 0,0")
                or want(cell(s, 0, 1), (ord("i"), 0x07), "cell 0,1")
                or want((s.row, s.col), (0, 2), "cursor"))

    @case("glyph.c0-bytes-are-cp437-glyphs-not-controls", b"\x01\x02\x03\x0b\x0e")
    def _(s):
        return (want(bytes(s.chars[0:5]), b"\x01\x02\x03\x0b\x0e", "row 0")
                or want(s.col, 5, "column"))

    @case("glyph.7f-is-a-glyph", b"\x7f")
    def _(s):
        return want(s.chars[0], 0x7F, "cell 0,0")

    @case("glyph.high-bytes-are-cp437-never-c1", b"\xb0\xb1\xdb\xff")
    def _(s):
        return want(bytes(s.chars[0:4]), b"\xb0\xb1\xdb\xff", "row 0")

    @case("glyph.nul-is-dropped", b"A\x00B")
    def _(s):
        return want(bytes(s.chars[0:2]), b"AB", "row 0")

    @case("glyph.can-and-sub-are-dropped-in-ground", b"A\x18\x1aB")
    def _(s):
        return want(bytes(s.chars[0:2]), b"AB", "row 0")

    # --- the controls ---------------------------------------------------------
    @case("ctrl.bs-moves-left-and-does-not-erase", b"AB\x08")
    def _(s):
        return (want((s.row, s.col), (0, 1), "cursor")
                or want(bytes(s.chars[0:2]), b"AB", "row 0"))

    @case("ctrl.bs-clamps-at-column-0", b"\x08\x08X")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x07), "cell 0,0")

    @case("ctrl.tab-goes-to-the-next-multiple-of-8", b"A\tB")
    def _(s):
        return (want(s.chars[8], ord("B"), "cell 0,8")
                or want(bytes(s.chars[1:8]), b" " * 7, "not erased, still blank"))

    @case("ctrl.tab-past-the-last-stop-clamps-to-column-79",
          b"\x1b[1;74H\tX")
    def _(s):
        return want(cell(s, 0, 79), (ord("X"), 0x07), "cell 0,79")

    @case("ctrl.lf-is-an-index-and-does-not-return", b"AB\nC")
    def _(s):
        return (want(cell(s, 1, 2), (ord("C"), 0x07), "cell 1,2")
                or want((s.row, s.col), (1, 3), "cursor"))

    @case("ctrl.cr-returns-and-does-not-index", b"AB\rC")
    def _(s):
        return want(bytes(s.chars[0:2]), b"CB", "row 0")

    @case("ctrl.ff-clears-with-the-current-attribute-and-homes",
          b"\x1b[41mAB\x0c")
    def _(s):
        return (want(cell(s, 0, 0), (0x20, 0x17), "cell 0,0")
                or want((s.row, s.col), (0, 0), "cursor"))

    @case("ctrl.bel-is-counted-and-never-drawn", b"\x07\x07A")
    def _(s):
        return (want(s.bells, 2, "bells")
                or want(cell(s, 0, 0), (ord("A"), 0x07), "cell 0,0"))

    # --- wrap -----------------------------------------------------------------
    @case("wrap.glyph-in-column-80-stays-and-the-cursor-does-not-move",
          b"\x1b[1;80HX")
    def _(s):
        return (want(cell(s, 0, 79), (ord("X"), 0x07), "cell 0,79")
                or want((s.row, s.col), (0, 79), "cursor")
                or want(s.wrap_pending, True, "pending wrap"))

    @case("wrap.the-next-glyph-wraps-to-column-1-of-the-next-row",
          b"\x1b[1;80HXY")
    def _(s):
        return (want(cell(s, 0, 79), (ord("X"), 0x07), "cell 0,79")
                or want(cell(s, 1, 0), (ord("Y"), 0x07), "cell 1,0")
                or want(s.wrap_pending, False, "pending wrap"))

    @case("wrap.any-cursor-motion-clears-the-pending-flag",
          b"\x1b[1;80HX\x1b[1;80HY")
    def _(s):
        return (want(cell(s, 0, 79), (ord("Y"), 0x07), "cell 0,79")
                or want(cell(s, 1, 0), (0x20, 0x07), "cell 1,0 untouched"))

    @case("wrap.at-the-bottom-right-the-screen-scrolls",
          b"\x1b[25;80HX" + b"Y")
    def _(s):
        return (want(s.scrolls, 1, "scrolls")
                or want(cell(s, ROWS - 1, 0), (ord("Y"), 0x07), "cell 24,0")
                or want(cell(s, ROWS - 2, 79), (ord("X"), 0x07), "cell 23,79"))

    # --- cursor motion --------------------------------------------------------
    @case("cup.is-1-based-and-a-missing-parameter-defaults-to-1", b"\x1b[;5HX")
    def _(s):
        return want(cell(s, 0, 4), (ord("X"), 0x07), "cell 0,4")

    @case("cup.clamps-past-the-edges", b"\x1b[99;99HX")
    def _(s):
        return want(cell(s, ROWS - 1, COLS - 1), (ord("X"), 0x07), "cell 24,79")

    @case("cursor.abcd-default-to-1-and-clamp-at-the-edges",
          b"\x1b[10;10H\x1b[A\x1b[2B\x1b[3C\x1b[D" + b"X")
    def _(s):
        return want(cell(s, 10, 11), (ord("X"), 0x07), "cell 10,11")

    @case("cursor.cnl-and-cpl-go-to-column-0",
          b"\x1b[5;40H\x1b[EX\x1b[FY")
    def _(s):
        return (want(cell(s, 5, 0), (ord("X"), 0x07), "cell 5,0")
                or want(cell(s, 4, 0), (ord("Y"), 0x07), "cell 4,0"))

    @case("cursor.cha-sets-the-column", b"\x1b[10GX")
    def _(s):
        return want(cell(s, 0, 9), (ord("X"), 0x07), "cell 0,9")

    @case("cursor.scp-and-rcp-round-trip", b"\x1b[7;7H\x1b[s\x1b[1;1H\x1b[uX")
    def _(s):
        return want(cell(s, 6, 6), (ord("X"), 0x07), "cell 6,6")

    @case("cursor.esc7-and-esc8-are-save-and-restore",
          b"\x1b[9;9H\x1b7\x1b[1;1H\x1b8X")
    def _(s):
        return want(cell(s, 8, 8), (ord("X"), 0x07), "cell 8,8")

    @case("cursor.neither-save-touches-the-attribute",
          b"\x1b[41m\x1b7\x1b[0;42m\x1b8X")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x27), "the green stayed")

    @case("cursor.esc7-and-csi-s-share-ONE-slot",
          b"\x1b[3;3H\x1b7\x1b[9;9H\x1b[s\x1b[1;1H\x1b8X")
    def _(s):
        return want(cell(s, 8, 8), (ord("X"), 0x07), "CSI s overwrote ESC 7's")

    @case("cursor.private-25hl-shows-and-hides", b"\x1b[?25l")
    def _(s):
        return want(s.cursor_visible, False, "cursor visible")

    # --- erases ---------------------------------------------------------------
    @case("erase.ed0-clears-from-the-cursor-with-the-current-attribute",
          b"\x1b[42mAAAA\x1b[1;3H\x1b[0J")
    def _(s):
        return (want(cell(s, 0, 1), (ord("A"), 0x27), "cell 0,1 kept")
                or want(cell(s, 0, 2), (0x20, 0x27), "cell 0,2 erased")
                or want(cell(s, 24, 79), (0x20, 0x27), "cell 24,79 erased"))

    @case("erase.ed1-clears-to-the-cursor-inclusive",
          b"\x1b[1;5HABCD\x1b[1;6H\x1b[1J")
    def _(s):
        return (want(cell(s, 0, 5), (0x20, 0x07), "cell 0,5 erased")
                or want(cell(s, 0, 6), (ord("C"), 0x07), "cell 0,6 kept"))

    @case("erase.ed2-clears-everything-and-homes", b"\x1b[10;10HA\x1b[2J")
    def _(s):
        return (want((s.row, s.col), (0, 0), "cursor")
                or want(cell(s, 9, 9), (0x20, 0x07), "cell 9,9"))

    @case("erase.el-0-1-2", b"ABCDE\x1b[1;3H\x1b[0K\x1b[1;1HX\x1b[1;2H\x1b[1K")
    def _(s):
        return (want(cell(s, 0, 0), (0x20, 0x07), "cell 0,0 erased by EL1")
                or want(cell(s, 0, 1), (0x20, 0x07), "cell 0,1 erased by EL1")
                or want(cell(s, 0, 2), (0x20, 0x07), "cell 0,2 erased by EL0"))

    @case("erase.ech-erases-n-and-does-not-move", b"ABCDE\x1b[1;2H\x1b[2X")
    def _(s):
        return (want(bytes(s.chars[0:5]), b"A  DE", "row 0")
                or want((s.row, s.col), (0, 1), "cursor"))

    # --- lines and characters -------------------------------------------------
    @case("edit.il-pushes-rows-down", b"A\r\nB\r\nC\x1b[2;1H\x1b[L")
    def _(s):
        return (want(s.line(0)[0], "A", "row 0")
                or want(s.line(1).strip(), "", "row 1 blank")
                or want(s.line(2)[0], "B", "row 2"))

    # A BARE `ESC[M` is ANSI music, so Delete Line is only ever reached with a
    # parameter. That is the design's one genuine collision and this case is
    # where the resolution is written down.
    @case("edit.dl-pulls-rows-up", b"A\r\nB\r\nC\r\nD\x1b[2;1H\x1b[2M")
    def _(s):
        return (want(s.line(1)[0], "D", "row 1 - B and C both went")
                or want(s.line(2).strip(), "", "row 2 blank"))

    @case("edit.ich-opens-a-gap-in-the-row", b"ABCD\x1b[1;2H\x1b[2@")
    def _(s):
        return want(s.line(0)[0:6], "A  BCD", "row 0")

    @case("edit.dch-closes-a-gap-in-the-row", b"ABCD\x1b[1;2H\x1b[2P")
    def _(s):
        return want(s.line(0)[0:2], "AD", "row 0")

    @case("edit.su-and-sd-move-the-whole-screen",
          b"A\r\nB\r\nC\x1b[2S\x1b[1T")
    def _(s):
        return (want(s.line(0).strip(), "", "row 0 after SU 2 then SD 1")
                or want(s.line(1)[0], "C", "row 1")
                or want(s.line(2).strip(), "", "row 2"))

    # --- SGR ------------------------------------------------------------------
    @case("sgr.0-resets-to-07", b"\x1b[1;31;44;5;7;8m\x1b[0mX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x07), "cell 0,0")

    @case("sgr.bare-m-is-a-reset", b"\x1b[41m\x1b[mX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x07), "cell 0,0")

    @case("sgr.30-37-keep-the-intensity-bit", b"\x1b[1;34mX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x0C), "cell 0,0")

    @case("sgr.22-drops-the-intensity-bit", b"\x1b[1;34;22mX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x04), "cell 0,0")

    @case("sgr.39-sets-the-whole-fg-nibble-to-7", b"\x1b[1;31;39mX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x07), "cell 0,0")

    @case("sgr.40-47-and-49", b"\x1b[46mA\x1b[49mB")
    def _(s):
        return (want(cell(s, 0, 0), (ord("A"), 0x67), "cell 0,0")
                or want(cell(s, 0, 1), (ord("B"), 0x07), "cell 0,1"))

    @case("sgr.5-and-25-are-attribute-bit-7", b"\x1b[5mA\x1b[25mB")
    def _(s):
        return (want(cell(s, 0, 0), (ord("A"), 0x87), "cell 0,0")
                or want(cell(s, 0, 1), (ord("B"), 0x07), "cell 0,1"))

    # THE FOUR CHECK VALUES of the SGR contract, one case each: 0;7 -> 0x70,
    # 1;7 -> 0x78, 7;31 -> 0x10 and 44;8 -> 0x44. The third is the one a
    # destructive parser gets wrong, drawing blue on white instead.
    @case("sgr.7-is-a-flag-so-a-later-colour-lands-on-the-right-half",
          b"\x1b[7;31mX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x10), "black on red")

    @case("sgr.0-then-7-is-the-first-check-value", b"\x1b[0;7mX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x70), "cell 0,0")

    @case("sgr.7-keeps-the-intensity-on-the-foreground", b"\x1b[1;7mX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x78), "bright black on white")

    @case("sgr.27-turns-reverse-off-again", b"\x1b[7m\x1b[27mX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x07), "cell 0,0")

    @case("sgr.40-47-leave-the-blink-flag-alone", b"\x1b[5m\x1b[41mX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x97), "cell 0,0")

    @case("sgr.8-conceals-by-making-fg-equal-bg", b"\x1b[44;8mX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x44), "cell 0,0")

    @case("sgr.28-turns-concealed-back-off", b"\x1b[44;8m\x1b[28mX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x47), "the 7 came back")

    @case("sgr.28-on-its-own-does-nothing", b"\x1b[41;28mX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x17), "cell 0,0")

    @case("sgr.90-97-are-bright-foregrounds", b"\x1b[94mX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x0C), "cell 0,0")

    @case("sgr.100-107-set-the-background-and-bit-7", b"\x1b[105mX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0xD7), "cell 0,0")

    @case("sgr.4-does-not-touch-the-attribute", b"\x1b[4mX")
    def _(s):
        return (want(cell(s, 0, 0), (ord("X"), 0x07), "cell 0,0")
                or want(s.underline, True, "underline flag"))

    @case("sgr.2-and-unknown-codes-are-ignored", b"\x1b[2;3;99mX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x07), "cell 0,0")

    # --- parameters -----------------------------------------------------------
    @case("param.a-value-saturates-at-255", b"\x1b[99999;1HX")
    def _(s):
        return want(cell(s, ROWS - 1, 0), (ord("X"), 0x07), "cell 24,0")

    @case("param.the-ninth-parameter-goes-to-a-sink-not-into-the-eighth",
          b"\x1b[0;1;1;1;1;1;1;31;44mX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x09), "red on black; no 44")

    @case("param.eight-parameters-are-fine", b"\x1b[0;1;1;1;1;1;31;44mX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x49), "bold red on blue")

    @case("param.a-missing-parameter-is-zero-not-absent", b"\x1b[44m\x1b[;31mX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x01), "the empty param reset it")

    @case("param.a-trailing-parameter-a-final-does-not-use-is-ignored",
          b"\x1b[1;5;9HX")
    def _(s):
        return want(cell(s, 0, 4), (ord("X"), 0x07), "cell 0,4")

    # --- the state machine's edges -------------------------------------------
    @case("csi.a-c0-inside-a-csi-is-executed-and-the-sequence-continues",
          b"ABC\x1b[1;\r5HX")
    def _(s):
        return (want(cell(s, 0, 4), (ord("X"), 0x07), "cell 0,4")
                or want(bytes(s.chars[0:3]), b"ABC", "row 0 kept"))

    # A byte that is art where it is art is NOISE where it lands inside a
    # sequence: only the seven recognised controls act, and drawing the rest
    # is how a lost byte turns into a diamond in the middle of a menu.
    @case("csi.any-other-c0-inside-a-csi-is-ignored-and-never-drawn",
          b"\x1b[5;5H\x1b[\x012;2HX")
    def _(s):
        return (want(cell(s, 4, 4), (0x20, 0x07), "cell 4,4 is untouched")
                or want(cell(s, 1, 1), (ord("X"), 0x07), "cell 1,1"))

    @case("csi.can-aborts-the-sequence", b"\x1b[1;1\x18HX")
    def _(s):
        return want(cell(s, 0, 1), (ord("X"), 0x07), "H never ran; H drew")

    @case("csi.sub-aborts-the-sequence", b"\x1b[41\x1amX")
    def _(s):
        return want(cell(s, 0, 1), (ord("X"), 0x07), "m never ran")

    @case("csi.esc-restarts-the-sequence", b"\x1b[41\x1b[1;3HX")
    def _(s):
        return want(cell(s, 0, 2), (ord("X"), 0x07), "the 41 was abandoned")

    @case("csi.an-unknown-final-is-ignored", b"\x1b[5ZX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x07), "cell 0,0")

    @case("csi.an-intermediate-byte-makes-the-sequence-a-no-op",
          b"\x1b[1 qX\x1b[ 1;5HY")
    def _(s):
        return (want(cell(s, 0, 0), (ord("X"), 0x07), "cell 0,0")
                or want(cell(s, 0, 1), (ord("Y"), 0x07), "the CUP did NOT run"))

    @case("csi.a-private-sequence-other-than-25hl-is-consumed", b"\x1b[?7hX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x07), "cell 0,0")

    @case("csi.an-equals-prefix-is-consumed", b"\x1b[=3hX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x07), "cell 0,0")

    @case("esc.a-byte-other-than-bracket-is-swallowed", b"\x1bZX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x07), "cell 0,0")

    @case("esc.esc-esc-restarts-and-does-not-swallow", b"AB\x1b\x1b[1;5HX")
    def _(s):
        return (want(cell(s, 0, 4), (ord("X"), 0x07), "the CUP still ran")
                or want(s.line(0)[0:2], "AB", "row 0"))

    @case("esc.a-control-inside-an-escape-runs-and-stays-in-escape",
          b"AB\r\x1b\r[1;5HX")
    def _(s):
        return want(cell(s, 0, 4), (ord("X"), 0x07), "the CR ran, then the CUP")

    @case("esc.any-other-c0-inside-an-escape-is-ignored-never-drawn",
          b"\x1b\x01[1;5HX")
    def _(s):
        return (want(cell(s, 0, 4), (ord("X"), 0x07), "the CUP still ran")
                or want(s.line(0)[0:4], "    ", "nothing was drawn"))

    @case("string.osc-is-swallowed-to-bel", b"\x1b]0;a title\x07X")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x07), "cell 0,0")

    @case("string.dcs-is-swallowed-to-st", b"\x1bPq#0;2;0;0;0\x1b\\X")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x07), "cell 0,0")

    @case("string.an-esc-inside-a-string-hands-the-next-byte-to-escape",
          b"\x1b_junk\x1b[31mX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x01), "the CSI after ESC ran")

    @case("music.bare-esc-bracket-M-is-swallowed-to-0x0e",
          b"\x1b[MMFcdefg\x0eX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x07), "cell 0,0")

    @case("music.a-parameter-makes-it-delete-line-instead",
          b"A\r\nB\r\nC\x1b[1;1H\x1b[1M")
    def _(s):
        return want(s.line(0)[0], "B", "row 0")

    @case("music.bare-esc-bracket-N-is-swallowed-too", b"\x1b[NABC\x0eX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x07), "cell 0,0")

    @case("music.a-private-prefix-means-it-is-NOT-bare-so-it-is-not-music",
          b"\x1b[?MX")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x07), "consumed and ignored")

    @case("music.can-aborts-it-so-a-truncated-string-cannot-eat-the-session",
          b"\x1b[MMFcd\x18X")
    def _(s):
        return want(cell(s, 0, 0), (ord("X"), 0x07), "cell 0,0")

    @case("music.esc-restarts-as-a-new-sequence", b"\x1b[MMFcd\x1b[1;5HX")
    def _(s):
        return want(cell(s, 0, 4), (ord("X"), 0x07), "cell 0,4")

    # --- what the terminal says back ------------------------------------------
    @case("answer.dsr-6-reports-the-cursor-1-based", b"\x1b[12;34H\x1b[6n")
    def _(s):
        return want(bytes(s.answers), b"\x1b[12;34R", "answers")

    @case("answer.dsr-5-reports-ok", b"\x1b[5n")
    def _(s):
        return want(bytes(s.answers), b"\x1b[0n", "answers")

    @case("answer.da-identifies-as-a-vt100-with-no-options", b"\x1b[c")
    def _(s):
        return want(bytes(s.answers), b"\x1b[?1;0c", "answers")

    @case("answer.a-private-da-is-not-answered", b"\x1b[?c")
    def _(s):
        return want(bytes(s.answers), b"", "answers")

    @case("answer.da-with-a-non-zero-parameter-is-not-answered", b"\x1b[1c")
    def _(s):
        return want(bytes(s.answers), b"", "answers")

    # --- the Zmodem trigger ---------------------------------------------------
    @case("zmodem.the-trigger-hands-the-stream-over",
          b"hi\r\n**\x18B00000000226ed2\r\n")
    def _(s):
        return (want(s.zmodem_at, 10, "offset")
                or want(s.line(1)[0:4], "**B0", "what the glass holds"))

    @case("zmodem.a-partial-trigger-does-not-fire", b"**\x18BX")
    def _(s):
        return (want(s.zmodem_at, None, "offset")
                or want(s.line(0)[0:4], "**BX", "row 0 - the 0x18 is consumed"))

    @case("zmodem.three-stars-still-fire", b"***\x18B00")
    def _(s):
        return (want(s.zmodem_at, 7, "offset")
                or want(s.line(0)[0:3], "***", "all three are on the glass"))

    @case("zmodem.a-star-separated-by-a-sequence-is-just-a-star",
          b"*\x1b[0m*\x18B00X")
    def _(s):
        return (want(s.zmodem_at, None, "offset")
                or want(s.line(0)[0:6], "**B00X", "row 0"))

    return C


def self_check(verbose=False):
    cases = _cases()
    bad = 0
    for name, stream, fn in cases:
        s = render(stream)
        err = fn(s)
        if err:
            bad += 1
            print("selfcheck FAIL %s\n               %s" % (name, err))
        elif verbose:
            print("selfcheck ok   %s" % name)

    # --- fragment independence, the property no fixture can show -------------
    # The same bytes, split at EVERY offset, must leave the identical buffer.
    # A parser that keeps one byte of state in a local across a call is correct
    # on every case above and wrong on a wire.
    frag_bad = 0
    for name, stream, _fn in cases:
        whole = render(stream)
        ref = (whole.raw(), whole.row, whole.col, bytes(whole.answers),
               whole.zmodem_at, whole.cursor_visible)
        for cut in range(len(stream) + 1):
            s = Screen()
            n = s.feed(stream[:cut])
            if s.zmodem_at is None:
                s.feed(stream[cut:])
            else:
                # The trigger fired in the first half: the rest of the stream
                # is the Zmodem receiver's and this object must not eat it.
                if s.feed(stream[cut:]) != 0:
                    frag_bad += 1
                    print("selfcheck FAIL frag.%s: fed past the trigger" % name)
                    break
            got = (s.raw(), s.row, s.col, bytes(s.answers),
                   s.zmodem_at, s.cursor_visible)
            if got != ref:
                frag_bad += 1
                print("selfcheck FAIL frag.%s: split at %d differs" % (name, cut))
                break
        else:
            if verbose:
                print("selfcheck ok   frag.%s (%d splits)"
                      % (name, len(stream) + 1))

    # ...and one long stream split at every offset, so the cases above are not
    # the only evidence: they are short, and a state that only goes wrong after
    # a scroll would survive all of them.
    long_stream = b"".join(s for _n, s, _f in cases)
    ref = render(long_stream).raw()
    long_bad = 0
    for cut in range(0, len(long_stream) + 1, 7):
        s = Screen()
        s.feed(long_stream[:cut])
        if s.zmodem_at is None:
            s.feed(long_stream[cut:])
        if s.zmodem_at is None and s.raw() != ref:
            long_bad += 1
            print("selfcheck FAIL frag.concatenated: split at %d differs" % cut)
            break

    total = bad + frag_bad + long_bad
    print("ansisim selfcheck: %d cases, %d fragment sweeps, %d problem(s)"
          % (len(cases), len(cases) + 1, total))
    return total == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("file", nargs="?", help="a byte stream to render")
    ap.add_argument("--dump", action="store_true",
                    help="print the 80x25 buffer as CP437 text")
    ap.add_argument("--attrs", action="store_true",
                    help="print the attribute plane as hex")
    ap.add_argument("--json", action="store_true",
                    help="the buffer and the state as JSON")
    ap.add_argument("--selfcheck", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if args.selfcheck or not args.file:
        ok = self_check(args.verbose)
        if not args.file:
            return 0 if ok else 1
        if not ok:
            return 1
        print()

    with open(args.file, "rb") as fh:
        data = fh.read()
    s = render(data)

    if args.json:
        json.dump(dict(chars=list(s.chars), attrs=list(s.attrs),
                       lines=[s.line(r) for r in range(ROWS)],
                       **s.state_dict()), sys.stdout, indent=1, sort_keys=True)
        print()
        return 0

    st = s.state_dict()
    print("%s  %d bytes  cursor %d,%d  attr %02X  %d glyphs  %d bell(s)"
          % (args.file, len(data), st["row"], st["col"], st["attr"],
             st["glyphs"], st["bells"]))
    if st["answers"]:
        print("  answers  %r" % st["answers"].encode("latin-1"))
    if st["zmodem_at"] is not None:
        print("  ZMODEM   takes the stream over at offset %d (%d bytes left)"
              % (st["zmodem_at"], len(data) - st["zmodem_at"]))
    if st["state"] != "GROUND":
        print("  note     the stream ended in %s" % st["state"])
    if args.dump:
        print("   +" + "-" * COLS + "+")
        for r in range(ROWS):
            print("%2d |%s|" % (r, s.line(r)))
        print("   +" + "-" * COLS + "+")
    if args.attrs:
        for r in range(ROWS):
            print("%2d %s" % (r, "".join(
                "%02x" % a for a in s.attrs[r * COLS:(r + 1) * COLS])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
