#!/usr/bin/env python3
"""The packed .WAB bundles, read by something that did not write them.

    python3 tests/unit/t_wab.py [dir-with-WABs]     (default: build/)

`tools/weavesim.py --pack` writes the bundles and re-reads them through its
own reader before they hit disk - but that is the PACKER'S OWN CODE, so the
failure it cannot see is both halves agreeing on the same wrong thing.  That
is the t_image.py / wordfmt.py pattern (SPEC.md 68.4.2), and WEAVE-SPEC 12.2
makes it binding for this format: this file is written from docs/WEAVE-SPEC.md
alone, shares no code with any packer, and never imports or reads weavesim.
Two implementations, one format; where they disagree, the spec has a bug and
the spec is fixed first (WEAVE-SPEC's own rule, its preamble).

Every byte offset, range and invariant below cites the WEAVE-SPEC section
that pins it.  A bare `S2.2` here means WEAVE-SPEC 2.2.

WHAT THE SPEC ONCE LEFT AMBIGUOUS - eight read gaps this file recorded on
day one (A1-A8), every one pinned in the spec by the wave-1 reconciliation.
The former AMBIGUITY sites below now enforce the pinned readings:

  A1. An empty function table's CODE body: pinned in S2.8 - exactly one
      HALT byte, section length exactly 3.
  A2. Bytes after the last section: pinned in S2.3 - the file ends at the
      last section's UNPADDED end; total size equals it, no tail padding.
  A3. PUSHA of a well-known atom: pinned in S2.7 - a WJS string literal
      spelling a well-known name still interns as an app atom (ids 1-63
      have no runtime string table), so every PUSHA operand is 64-250.
  A4. The <sprite img=...> property record: pinned in S3.3 - named by
      atom 11 `frame`, kind PK_SPRITE, on <sprite> records only.
  A5. A <canvas>'s compiled w/h: pinned in S2.5 - w/8 cells and
      ceil(h/8) rows, never 0 (both attributes are required); a <sprite>
      record carries 0/0.
  A6. PROPS coverage: pinned in S2.14 rule 5 - blocks back to back with
      no gaps, blobs after all blocks in record-emission order, section
      length exactly their sum.  This reader still walks REFERENCED
      extents only (bounds, walkability, order) and does not assert every
      byte's membership - its one standing leniency, noted here so it is
      a decision and not a drift.
  A7. Inter-function gaps in CODE: pinned in S2.8 in as many words -
      functions packed back to back from 2+4F.  This file read it
      strictly from day one; nothing changed.
  A8. WABF_TIMER (S2.2.1 bit 2): pinned - exactly three causes: timer()
      in CODE, an <input>, or a <grid> (its formula bar is a library-wired
      input, S6.9).  Enforced both directions.

STILL AMBIGUOUS, and read leniently here rather than guessed at:

  A9. What `<canvas walls="TB">` COMPILES to.  S3.3 types the attribute
      as a string, a subset of `TBLR`, and pins no encoding; the packed
      bundles carry a PK_INT edge mask.  Both readings are admitted -
      a mask is bounded by S3.4's four edge codes (0..15), a pooled
      string by the TBLR subset - because a second reader that picks one
      and refuses the other is asserting a fact the spec does not state.
      Naming it here is the point: the spec should pin it, and until it
      does neither implementation can be called wrong.

  A10. S2.2 tabulates `section count` as 1-9 while S2.4 makes four
      sections mandatory and ICON always present.  Read here as 5-9,
      the only range the rest of the format can produce; S2.2's own
      figure is the one that should move.

WHAT S3.3 SAYS AND THIS FILE NOW ENFORCES.  S2.6 hands the reader the
attribute table as a validation duty - "which names are legal on which
component is S3.3's table; a reader treats an unknown pairing as a
malformed bundle" - so S33_PROPS below is that table transcribed: the
legal pairings per element, the kind each attribute compiles to, and the
range each is bounded by.  S33_REQUIRED is its `required` column.  These
are not ornaments on a well-formed file: `meter max = 0` divides by zero
drawing the bar, a `radio` with no `group` has nothing to be exclusive
against, and an out-of-range `cols` lays a component out past the card.

DETERMINISM.  S2.14 rule 1: no timestamps, no host paths, nothing
environmental.  Every field S2.2-S2.13 defines is structural, so there is no
field this file must wave through unvalidated as "environmental" - byte
identity between two packs of the same source (the S11.1 gate) is checkable
later by straight comparison, and nothing in the format can excuse a diff.
"""
import glob
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, eq, done                       # noqa: E402

# ---------------------------------------------------------------- constants
# All from docs/WEAVE-SPEC.md, section cited per table.

MAGIC = b"WAB\x1a"                                        # S2.2 +0
CAP = 0xF800                                              # S2.1: 63,488

# S2.2.1 - the flags word.
WABF_GRID, WABF_CANVAS, WABF_TIMER, WABF_STATE, WABF_SOURCE = 1, 2, 4, 8, 16
WABF_KNOWN = 0x001F                                       # bits 5-15 are 0

# S2.4 - the nine section types.
UISTREAM, PROPS, CODE, ATOMS, FXCODE, CELLS, SPRITES, ICON, SOURCE = range(1, 10)
SEC_NAMES = {UISTREAM: "UISTREAM", PROPS: "PROPS", CODE: "CODE",
             ATOMS: "ATOMS", FXCODE: "FXCODE", CELLS: "CELLS",
             SPRITES: "SPRITES", ICON: "ICON", SOURCE: "SOURCE"}

# S2.5.1 - ctype codes.
CT_LABEL, CT_TEXT, CT_RULE, CT_BOX, CT_SPACER, CT_METER, CT_BUTTON, \
    CT_CHECK, CT_RADIO, CT_INPUT, CT_LIST, CT_GRID, CT_CANVAS, CT_SPRITE = \
    range(0x01, 0x0F)

# S2.7.1 - the assigned well-known atom ids (25-31, 42-47, 63 reserved).
WK_PROPS = set(range(1, 25))                              # text..tick
WK_METHODS = set(range(32, 42))                           # cell..clear
WK_EVENTS = set(range(48, 61))                            # onclick..onalert
ATOM_ITEMS, ATOM_MENUS = 61, 62
ATOM_ROWS, ATOM_COLS, ATOM_CARD = 15, 16, 20
ATOM_FRAME, ATOM_START = 11, 40
ATOM_TEXT, ATOM_VALUE, ATOM_LABEL, ATOM_CHECKED = 1, 2, 3, 5
ATOM_X, ATOM_Y, ATOM_SHOWN, ATOM_MAX = 7, 8, 12, 14
ATOM_GROUP, ATOM_WALLS, ATOM_TICK = 19, 23, 24
ATOM_ONCLICK, ATOM_ONCHANGE, ATOM_ONKEY, ATOM_ONSELECT = 48, 49, 50, 51
ATOM_ONEDIT, ATOM_ONCALC, ATOM_ONCOLLIDE = 52, 53, 54
ATOM_ONWALL, ATOM_ONSCORE, ATOM_ONTICK = 55, 56, 57
WK_ASSIGNED = WK_PROPS | WK_METHODS | WK_EVENTS | {ATOM_ITEMS, ATOM_MENUS}

# S2.6 - property record kinds.
PK_INT, PK_ATOM, PK_BLOB, PK_FUNC, PK_SPRITE = range(5)

# S3.3 - the attribute table, element by element, as a bundle carries it:
# which property atoms may appear in a component's block, the kind each
# attribute compiles to, and the range S3.3 bounds it by (None = unbounded
# there).  S2.6 makes the pairing binding in as many words - "which names
# are legal on which component is S3.3's table; a reader treats an unknown
# pairing as a malformed bundle" - and a range is not decoration either:
# `meter max = 0` is a divide by zero on the runtime, and an `input` claiming
# 61 columns lays out past the widest card the flow walk can give it.
# Content-derived props are here too (S3.2's "children allowed: text"):
# `label`/`text` carry their content as `text`, the three controls as `label`.
S33_PROPS = {
    CT_LABEL:  {ATOM_TEXT: (PK_ATOM, None, None)},
    CT_TEXT:   {ATOM_TEXT: (PK_ATOM, None, None)},
    CT_RULE:   {},
    CT_BOX:    {},                                        # w,h live in the record
    CT_SPACER: {},
    CT_METER:  {ATOM_VALUE: (PK_INT, 0, 32000),           # and <= max, below
                ATOM_MAX: (PK_INT, 1, 32000)},
    CT_BUTTON: {ATOM_LABEL: (PK_ATOM, None, None),
                ATOM_ONCLICK: (PK_FUNC, None, None)},
    CT_CHECK:  {ATOM_LABEL: (PK_ATOM, None, None),
                ATOM_CHECKED: (PK_INT, 0, 1),
                ATOM_ONCHANGE: (PK_FUNC, None, None)},
    CT_RADIO:  {ATOM_LABEL: (PK_ATOM, None, None),
                ATOM_GROUP: (PK_ATOM, None, None),
                ATOM_CHECKED: (PK_INT, 0, 1),
                ATOM_ONCHANGE: (PK_FUNC, None, None)},
    CT_INPUT:  {ATOM_TEXT: (PK_ATOM, None, None),
                ATOM_COLS: (PK_INT, 2, 60),
                ATOM_ONCHANGE: (PK_FUNC, None, None),
                ATOM_ONKEY: (PK_FUNC, None, None)},
    CT_LIST:   {ATOM_ITEMS: (PK_BLOB, None, None),
                ATOM_ROWS: (PK_INT, 1, 40),
                ATOM_ONSELECT: (PK_FUNC, None, None)},
    CT_GRID:   {ATOM_COLS: (PK_INT, 1, 26),
                ATOM_ROWS: (PK_INT, 1, 256),
                ATOM_ONSELECT: (PK_FUNC, None, None),
                ATOM_ONEDIT: (PK_FUNC, None, None),
                ATOM_ONCALC: (PK_FUNC, None, None)},
    CT_CANVAS: {ATOM_WALLS: (PK_INT, 0, 15),              # or PK_ATOM - see below
                ATOM_TICK: (PK_INT, 0, 255),
                ATOM_ONKEY: (PK_FUNC, None, None),
                ATOM_ONCOLLIDE: (PK_FUNC, None, None),
                ATOM_ONWALL: (PK_FUNC, None, None),
                ATOM_ONSCORE: (PK_FUNC, None, None),
                ATOM_ONTICK: (PK_FUNC, None, None)},
    CT_SPRITE: {ATOM_FRAME: (PK_SPRITE, None, None),      # S3.3: `img` compiles here
                ATOM_X: (PK_INT, None, None),             # signed px
                ATOM_Y: (PK_INT, None, None),
                ATOM_SHOWN: (PK_INT, 0, 1)},
}
assert len(S33_PROPS) == 14                               # S2.5.1's fourteen ctypes

# S3.3's `required` column, for the attributes that survive into a block.
# (`app name` is the header field, `card id` is the card index, `canvas w/h`
# and `box`/`spacer` sizes are REC_COMP bytes, `menu title` and `item
# oncommand` are the MENUS blob - each checked where it lands.)
S33_REQUIRED = {
    CT_RADIO: (ATOM_GROUP,),      # one checked per group; absent = no group
    CT_GRID: (ATOM_COLS, ATOM_ROWS),
    CT_SPRITE: (ATOM_FRAME,),     # S3.3: `img` is required on <sprite>
}

CT_NAMES = {CT_LABEL: "label", CT_TEXT: "text", CT_RULE: "rule",
            CT_BOX: "box", CT_SPACER: "spacer", CT_METER: "meter",
            CT_BUTTON: "button", CT_CHECK: "check", CT_RADIO: "radio",
            CT_INPUT: "input", CT_LIST: "list", CT_GRID: "grid",
            CT_CANVAS: "canvas", CT_SPRITE: "sprite"}
ATOM_NAMES = {ATOM_TEXT: "text", ATOM_VALUE: "value", ATOM_LABEL: "label",
              ATOM_CHECKED: "checked", ATOM_X: "x", ATOM_Y: "y",
              ATOM_FRAME: "frame", ATOM_SHOWN: "shown", ATOM_MAX: "max",
              ATOM_ROWS: "rows", ATOM_COLS: "cols", ATOM_GROUP: "group",
              ATOM_WALLS: "walls", ATOM_TICK: "tick", ATOM_ITEMS: "ITEMS",
              ATOM_ONCLICK: "onclick", ATOM_ONCHANGE: "onchange",
              ATOM_ONKEY: "onkey", ATOM_ONSELECT: "onselect",
              ATOM_ONEDIT: "onedit", ATOM_ONCALC: "oncalc",
              ATOM_ONCOLLIDE: "oncollide", ATOM_ONWALL: "onwall",
              ATOM_ONSCORE: "onscore", ATOM_ONTICK: "ontick"}

# S4.5 - the 38 WVM opcodes and each one's total encoded length.
OP_HALT, OP_RET = 0x00, 0x1D
OP_JMP, OP_JZ, OP_JNZ = 0x19, 0x1A, 0x1B
OP_CALL, OP_GETP, OP_SETP, OP_CALLM, OP_BUILT = 0x1C, 0x1E, 0x1F, 0x20, 0x21
OP_PUSHA, OP_PUSHC = 0x02, 0x05
OP_LDG, OP_STG, OP_INCG, OP_DECG = 0x06, 0x07, 0x22, 0x23
OP_LDL, OP_STL = 0x08, 0x09
OPLEN = {
    0x00: 1, 0x01: 3, 0x02: 2, 0x03: 1, 0x04: 2, 0x05: 2, 0x06: 2, 0x07: 2,
    0x08: 2, 0x09: 2, 0x0A: 1, 0x0B: 1, 0x0C: 1, 0x0D: 1, 0x0E: 1, 0x0F: 1,
    0x10: 1, 0x11: 1, 0x12: 1, 0x13: 1, 0x14: 1, 0x15: 1, 0x16: 1, 0x17: 1,
    0x18: 1, 0x19: 3, 0x1A: 3, 0x1B: 3, 0x1C: 2, 0x1D: 1, 0x1E: 2, 0x1F: 2,
    0x20: 3, 0x21: 3, 0x22: 2, 0x23: 2, 0x24: 1, 0x25: 1,
}
assert len(OPLEN) == 38                                   # S4.5's own count

# S8.1 - builtin indices and arities (min, max args).
BUILTIN_ARITY = {0: (1, 2), 1: (2, 2), 2: (0, 0), 3: (0, 0), 4: (1, 1),
                 5: (2, 2), 6: (1, 1), 7: (1, 1), 8: (3, 3), 9: (2, 2),
                 10: (1, 1), 11: (1, 1)}
B_TIMER, B_SAVESTATE, B_LOADSTATE = 1, 2, 3

# S5.3 - the 23 FX opcodes: encoded length and stack effect (pop, push).
FEND, FNUM, FCELL, FRANGE = 0x00, 0x01, 0x02, 0x03
FXOPS = {0x00: (1, 0, 0), 0x01: (5, 0, 1), 0x02: (3, 0, 1), 0x03: (5, 0, 1),
         0x04: (1, 2, 1), 0x05: (1, 2, 1), 0x06: (1, 2, 1), 0x07: (1, 2, 1),
         0x08: (1, 1, 1), 0x09: (1, 2, 1), 0x0A: (1, 2, 1), 0x0B: (1, 2, 1),
         0x0C: (1, 2, 1), 0x0D: (1, 2, 1), 0x0E: (1, 2, 1), 0x0F: (1, 1, 1),
         0x10: (1, 1, 1), 0x11: (1, 1, 1), 0x12: (1, 1, 1), 0x13: (1, 1, 1),
         0x14: (1, 3, 1), 0x15: (1, 1, 1), 0x16: (1, 1, 1)}
assert len(FXOPS) == 23                                   # S5.3's own count

REC_END, REC_CARD, REC_COMP = 0x00, 0x01, 0x02            # S2.5
RECLEN = 10                                               # S2.5: 10-byte records


def word(b, o):
    return struct.unpack_from("<H", b, o)[0]


def signed(v):
    """S2.6: a property record's value is a SIGNED word for PK_INT."""
    return v - 0x10000 if v >= 0x8000 else v


def align16(n):
    return (n + 15) & ~15


def printable(bs):
    return all(0x20 <= c <= 0x7E for c in bs)


# ---------------------------------------------------------------- per-bundle
class Bundle:
    """Everything one .WAB validation accumulates and shares between passes."""

    def __init__(self, blob, name):
        self.blob, self.name = blob, name
        self.sec = {}              # type -> (offset, length, extra)
        self.flags = 0
        self.entry_card = 0
        self.vm_kb = self.grid_kb = self.canvas_kb = 0
        self.atom_count = 0        # app atoms in the pool
        self.atom_text = {}        # atom id -> its pooled string
        self.comp_ids = set()      # every REC_COMP comp_id
        self.ctypes = {}           # comp_id -> ctype
        self.prop_ref = {}         # comp_id -> PROPS block offset (not 0xFFFF)
        self.cards = 0
        self.funcs = []            # (code offset, nargs, nlocals)
        self.nglobals = 0
        self.builtins_used = set()
        self.sprite_count = 0
        self.fx_count = 0
        self.grid_rows = self.grid_cols = None

    def atom_ok(self, a):
        """S2.7: assigned well-known, or an app atom the pool actually holds."""
        return a in WK_ASSIGNED or 64 <= a < 64 + self.atom_count

    def sbytes(self, t):
        off, length, _ = self.sec[t]
        return self.blob[off:off + length]


def check_header(w):
    """S2.2 - the 32-byte header, every field ranged."""
    b, n = w.blob, w.name
    eq(b[0:4], MAGIC, "%s: magic" % n, "S2.2: 'W','A','B',0x1A")
    eq(word(b, 4), 1, "%s: format version" % n, "S2.2: any other value refuses")
    total = word(b, 6)
    eq(total, len(b), "%s: total-size word equals the file size" % n,
       "S2.1/S10.1: the directory size must stand for the resident ask - "
       "the memory refusal is computed from it before any read")
    check(total <= CAP, "%s: total size %d within the 62KB cap" % (n, total),
          "S2.1: every internal offset is a 16-bit word within one segment",
          got=total, want="<= %d" % CAP)
    w.flags = word(b, 8)
    check(w.flags & ~WABF_KNOWN == 0, "%s: flags bits 5-15 are zero" % n,
          "S2.2.1: a set unknown bit refuses the bundle",
          got=hex(w.flags), want="& 0x%04X" % WABF_KNOWN)
    w.vm_kb, w.grid_kb = b[10], b[11]
    check(16 <= w.vm_kb <= 32, "%s: vm KB ask in 16..32" % n,
          "S2.2/S4.7: the VM claim ask", got=w.vm_kb)
    check(w.grid_kb == 0 or 8 <= w.grid_kb <= 26,
          "%s: grid KB ask 0 or 8..26" % n, "S2.2/S5.6", got=w.grid_kb)
    nsec = b[12]
    check(5 <= nsec <= 9, "%s: section count in 5..9" % n,
          "S2.2 tabulates the byte as 1-9, but S2.4 leaves no bundle with "
          "fewer than five sections: UISTREAM, PROPS, CODE and ATOMS are "
          "mandatory in every bundle and ICON is always present. A count "
          "below five is a header the format cannot produce", got=nsec)
    w.entry_card = b[13]
    check(1 <= w.entry_card <= 8, "%s: entry card in 1..8" % n,
          "S2.2: 1-based card index; S3.2 allows 1-8 cards", got=w.entry_card)
    w.canvas_kb = b[14]
    check(w.canvas_kb == 0 or 2 <= w.canvas_kb <= 8,
          "%s: canvas KB ask 0 or 2..8" % n, "S2.2/S6.10", got=w.canvas_kb)
    eq(b[15], 0, "%s: header reserved byte" % n, "S2.2: reserved, 0")
    name = b[16:32]
    nul = name.find(b"\x00")
    if check(0 < nul <= 15, "%s: app name is 1..15 chars then NUL" % n,
             "S2.2: 15 characters + NUL", got=name):
        check(printable(name[:nul]), "%s: app name is printable ASCII" % n,
              "S3.1's fold admits only 0x20..0x7E", got=name[:nul])
        check(all(c == 0 for c in name[nul:]),
              "%s: app name bytes after the NUL are 0x00" % n,
              "S2.2: space-padding before the NUL is not allowed; "
              "unused bytes after it are 0x00", got=name)
    return nsec


def check_sections(w, nsec):
    """S2.3 - the section table: bounds, alignment, packing, no overlap."""
    b, n = w.blob, w.name
    total = len(b)
    if not check(32 + 8 * nsec <= total, "%s: section table fits the file" % n):
        return False
    prev_type = 0
    expect = align16(32 + 8 * nsec)                       # S2.3: first section
    for i in range(nsec):
        row = 32 + 8 * i
        t, z, off, length, extra = b[row], b[row + 1], word(b, row + 2), \
            word(b, row + 4), word(b, row + 6)
        check(UISTREAM <= t <= SOURCE, "%s: row %d section type valid" % (n, i),
              "S2.4: types 1..9; a reader refuses others", got=t)
        eq(z, 0, "%s: row %d byte +1 is zero" % (n, i), "S2.3")
        check(t > prev_type, "%s: row %d type ascends, one row per type" % (n, i),
              "S2.3: rows sorted by ascending type, one row per type at most",
              got=t, want="> %d" % prev_type)
        prev_type = t
        eq(off % 16, 0, "%s: %s offset 16-byte aligned" % (n, SEC_NAMES.get(t, t)),
           "S2.1: a section is addressed as segment:0 from the claim base")
        eq(off, expect, "%s: %s begins where the previous section ends, aligned"
           % (n, SEC_NAMES.get(t, t)),
           "S2.3: each section begins at align16(previous offset + length) - "
           "a gap or overlap is a different file than the spec describes")
        check(off + length <= total, "%s: %s in bounds" % (n, SEC_NAMES.get(t, t)),
              "S2.3: the reader must bounds-check every offset + length",
              got="%d+%d" % (off, length), want="<= %d" % total)
        w.sec[t] = (off, length, extra)
        expect = align16(off + length)
    # Padding bytes between sections (and before the first) are 0x00 - S2.1.
    covered = [(32, 32 + 8 * nsec)] + \
              [(o, o + ln) for (o, ln, _) in w.sec.values()]
    covered.sort()
    pos = covered[0][1]
    for start, end in covered[1:]:
        check(all(c == 0 for c in b[pos:start]),
              "%s: inter-section padding at %d..%d is 0x00" % (n, pos, start),
              "S2.1: padding bytes between sections are 0x00 - anything else "
              "is bytes the spec does not account for", got=b[pos:start])
        pos = max(pos, end)
    # A2, pinned: the file ends at the last section's unpadded end.
    eq(total, pos, "%s: file ends at the last section's unpadded end" % n,
       "S2.3: total size equals the last section's offset + length exactly; "
       "padding exists only between sections, never after the last")
    # S2.4: the mandatory four, and ICON always.
    for t in (UISTREAM, PROPS, CODE, ATOMS):
        check(t in w.sec, "%s: mandatory section %s present" % (n, SEC_NAMES[t]),
              "S2.4: UISTREAM, PROPS, CODE and ATOMS are mandatory in every "
              "bundle - a scriptless app still gets an empty-table CODE")
    check(ICON in w.sec, "%s: ICON present" % n,
          "S2.4: ICON always - the packer supplies a default icon")
    if ICON in w.sec:
        eq(w.sec[ICON][1], 64, "%s: ICON is 64 bytes" % n,
           "S2.12: 16x16 1bpp, 4 bytes per row")
        eq(w.sec[ICON][2], 0, "%s: ICON extra word is 0" % n, "S2.4")
    for t in (CODE, ATOMS):
        if t in w.sec:
            eq(w.sec[t][2], 0, "%s: %s extra word is 0" % (n, SEC_NAMES[t]),
               "S2.4: extra has per-type meaning, else 0")
    return all(t in w.sec for t in (UISTREAM, PROPS, CODE, ATOMS))


def check_atoms(w):
    """S2.7 - the pool: walkable end to end, packed, folded, NUL-consistent."""
    s, n = w.sbytes(ATOMS), w.name
    if not check(len(s) >= 2, "%s: ATOMS holds its count word" % n):
        return
    count = word(s, 0)
    w.atom_count = count
    check(count <= 187, "%s: app atom count <= 187" % n,
          "S2.7: ids 64-250 are app atoms; more refuses at pack", got=count)
    if not check(2 + 2 * count <= len(s), "%s: ATOMS offset table fits" % n,
                 got=len(s), want=">= %d" % (2 + 2 * count)):
        return
    pos = 2 + 2 * count                                   # S2.7: atom 64 here
    for i in range(count):
        off = word(s, 2 + 2 * i)
        eq(off, pos, "%s: atom %d at the packed offset" % (n, 64 + i),
           "S2.7: offsets ascend in id order and strings pack without gaps")
        if not check(off + 1 <= len(s), "%s: atom %d header in bounds" % (n, 64 + i)):
            return
        L = s[off]
        check(1 <= L <= 255, "%s: atom %d length 1..255" % (n, 64 + i), got=L)
        if not check(off + 1 + L + 1 <= len(s),
                     "%s: atom %d body + NUL in bounds" % (n, 64 + i),
                     got="%d+%d+2" % (off, L), want="<= %d" % len(s)):
            return
        body = s[off + 1:off + 1 + L]
        check(printable(body), "%s: atom %d folded to 0x20..0x7E" % (n, 64 + i),
              "S2.7/S3.1: the Latin-1 fold - the cell font has 95 glyphs, "
              "nothing outside them survives to a bundle", got=body)
        eq(s[off + 1 + L], 0, "%s: atom %d NUL-terminated" % (n, 64 + i),
           "S2.7: length byte and NUL must agree - a reader may use either")
        w.atom_text[64 + i] = bytes(body)
        pos = off + 1 + L + 1
    eq(pos if count else 2, len(s), "%s: ATOMS length is exactly the pool" % n,
       "S2.7: no trailing bytes - the section length is part of the format")


def walk_prop_block(w, off, owner):
    """S2.6 - one property block; returns [(name, kind, value)] or None."""
    s, n = w.sbytes(PROPS), w.name
    out, prev_name = [], 0
    while True:
        if not check(off + 4 <= len(s), "%s: %s prop block in bounds" % (n, owner),
                     "S2.6: a block is 4-byte records ending in four zero bytes",
                     got=off, want="<= %d" % (len(s) - 4)):
            return None
        name, kind, value = s[off], s[off + 1], word(s, off + 2)
        if name == 0:
            eq(s[off:off + 4], b"\x00\x00\x00\x00",
               "%s: %s block terminator is four zero bytes" % (n, owner), "S2.6")
            return out
        check(w.atom_ok(name), "%s: %s prop name atom %d assigned" % (n, owner, name),
              "S2.7: name atoms must be assigned well-known or in the pool",
              got=name)
        check(name > prev_name, "%s: %s prop names ascend, no repeats" % (n, owner),
              "S2.6: records sort by ascending name atom id, a name at most "
              "once per block", got=name, want="> %d" % prev_name)
        prev_name = name
        check(kind <= PK_SPRITE, "%s: %s prop kind valid" % (n, owner),
              "S2.6: kinds 0..4", got=kind)
        if kind == PK_ATOM:
            check(value <= 255 and w.atom_ok(value),
                  "%s: %s PK_ATOM value is an assigned atom" % (n, owner),
                  "S2.6: an atom id in the low byte", got=value)
        elif kind == PK_BLOB:
            check(value < len(s), "%s: %s PK_BLOB offset inside PROPS" % (n, owner),
                  "S2.6: a byte offset into PROPS", got=value)
        elif kind == PK_FUNC:
            check(value < len(w.funcs), "%s: %s PK_FUNC index in table" % (n, owner),
                  "S2.6/S2.8: a function index", got=value,
                  want="< %d" % len(w.funcs))
        elif kind == PK_SPRITE:
            check(value < w.sprite_count, "%s: %s PK_SPRITE index valid" % (n, owner),
                  "S2.6/S2.11: a SPRITES index", got=value,
                  want="< %d" % w.sprite_count)
        if name in WK_EVENTS:
            eq(kind, PK_FUNC, "%s: %s event atom %d bound as PK_FUNC" % (n, owner, name),
               "S2.6: event bindings are ordinary records of kind PK_FUNC")
        if name == ATOM_ITEMS or name == ATOM_MENUS:
            eq(kind, PK_BLOB, "%s: %s structural atom %d is PK_BLOB" % (n, owner, name),
               "S2.6.1/S2.6.2")
        out.append((name, kind, value))
        off += 4


def check_comp_props(w, cid, ctype, recs):
    """S3.3 - one component's block against the attribute table: every
    pairing legal on that element, every kind the one S3.3 compiles to,
    every value inside the range the table pins, and every attribute S3.3
    marks required actually present.

    S2.6 hands this to the reader in as many words - an unknown pairing IS
    a malformed bundle - and the ranges are what the runtime trusts: a
    `meter` whose `max` is 0 divides by zero drawing its bar, and a `radio`
    with no `group` has nothing to be exclusive against (S6.6).
    """
    n, el = w.name, CT_NAMES.get(ctype, "ctype 0x%02X" % ctype)
    table = S33_PROPS.get(ctype, {})
    seen = {}
    for name, kind, value in recs:
        aname = ATOM_NAMES.get(name, "atom %d" % name)
        if name == ATOM_WALLS and ctype == CT_CANVAS and kind == PK_ATOM:
            # AMBIGUITY (A9): S3.3 types `walls` as a string, a subset of
            # `TBLR`, and does not say what it compiles to.  The demo
            # bundles carry a PK_INT edge mask; a pooled string is the
            # other defensible reading, so both are admitted and each is
            # bounded on its own terms.  S3.4's edge codes (0 T, 1 B,
            # 2 L, 3 R) are what bound the mask.
            seen[name] = value
            txt = w.atom_text.get(value, b"")
            check(set(txt) <= set(b"TBLR"),
                  "%s: %s comp %d walls string is a subset of TBLR" % (n, el, cid),
                  "S3.3: `walls` is a subset of TBLR; missing edges are open",
                  got=txt)
            continue
        if not check(name in table,
                     "%s: %s comp %d carries `%s`, which S3.3 does not give a "
                     "<%s>" % (n, el, cid, aname, el),
                     "S2.6: which names are legal on which component is "
                     "S3.3's table, and a reader treats an unknown pairing "
                     "as a malformed bundle - the runtime would hand the "
                     "value to a library component that has no such field",
                     got="atom %d on <%s>" % (name, el)):
            continue
        want_kind, lo, hi = table[name]
        eq(kind, want_kind, "%s: %s comp %d `%s` kind" % (n, el, cid, aname),
           "S3.3/S2.6: the attribute's compiled kind - 0 PK_INT, 1 PK_ATOM, "
           "2 PK_BLOB, 3 PK_FUNC, 4 PK_SPRITE")
        if lo is not None and kind == PK_INT:
            check(lo <= signed(value) <= hi,
                  "%s: %s comp %d `%s` in %d..%d" % (n, el, cid, aname, lo, hi),
                  "S3.3: the range this attribute is pinned to - a value "
                  "outside it is refused at pack, so a bundle carrying one "
                  "was not written by a conforming packer",
                  got=signed(value), want="%d..%d" % (lo, hi))
        seen[name] = value
    for req in S33_REQUIRED.get(ctype, ()):
        check(req in seen, "%s: %s comp %d carries the required `%s`"
              % (n, el, cid, ATOM_NAMES.get(req, req)),
              "S3.3 marks this attribute required on <%s>: absent is "
              "malformed, not defaulted - there is no default for it to "
              "take" % el, got=sorted(seen))
    if ctype == CT_METER:
        # S3.3: `value` is 0..max, `max` defaults to 100 when absent.
        mx = signed(seen[ATOM_MAX]) if ATOM_MAX in seen else 100
        val = signed(seen[ATOM_VALUE]) if ATOM_VALUE in seen else 0
        check(0 <= val <= mx, "%s: meter comp %d value within 0..max"
              % (n, cid), "S3.3/S6.4: `value` is 0..max and the library "
              "clamps to it; a packed value outside means the two "
              "implementations disagree about the clamp",
              got=val, want="0..%d" % mx)
    if ctype == CT_CANVAS and ATOM_ONTICK in seen:
        check(signed(seen.get(ATOM_TICK, 0)) >= 1,
              "%s: canvas comp %d binds ontick and carries tick >= 1" % (n, cid),
              "S3.3: `ontick` requires `tick` >= 1 - tick 0 is no ontick "
              "(S6.10 step 7 never fires), so a bound handler is dead code")


def check_uistream(w):
    """S2.5 - the display list: record shapes, ids, cards, ctypes, styles."""
    s, n = w.sbytes(UISTREAM), w.name
    _, length, extra = w.sec[UISTREAM]
    eq(length % RECLEN, 0, "%s: UISTREAM length a multiple of 10" % n,
       "S2.5: a sequence of 10-byte records")
    nrec = length // RECLEN
    eq(extra, nrec, "%s: UISTREAM extra word is the record count" % n, "S2.4")
    next_comp, next_card, prev_ctype = 1, 1, None
    grids = canvases = 0
    end_seen = False
    for i in range(nrec):
        r = s[i * RECLEN:(i + 1) * RECLEN]
        kind = r[0]
        check(not end_seen, "%s: rec %d after REC_END" % (n, i),
              "S2.5: REC_END is the final record, exactly one, last")
        if kind == REC_END:
            end_seen = True
            check(all(c == 0 for c in r[1:]), "%s: REC_END bytes +1..+9 zero" % n,
                  "S2.5", got=r)
            eq(i, nrec - 1, "%s: REC_END is the last record" % n, "S2.5")
        elif kind == REC_CARD:
            eq(r[1], next_card, "%s: rec %d card index in document order" % (n, i),
               "S2.5/S2.14: card indices are document order, 1-based")
            next_card += 1
            check(r[1] <= 8, "%s: card index <= 8" % n, "S2.5/S3.2", got=r[1])
            check(all(c == 0 for c in r[2:8]), "%s: rec %d REC_CARD pad zero" % (n, i),
                  "S2.5: bytes +2..+7 are 0", got=r)
            # v1 cards carry no props - S2.5's own parenthetical.
            eq(word(r, 8), 0xFFFF, "%s: rec %d card prop offset is 0xFFFF" % (n, i),
               "S2.5: v1 cards carry no props")
            prev_ctype = None
        elif kind == REC_COMP:
            comp_id, ctype = r[1], r[2]
            eq(comp_id, next_comp, "%s: rec %d comp_id in document order" % (n, i),
               "S2.5/S2.14: comp_ids are 1..250 assigned in document order - "
               "unique by construction, and both packers must agree on the order")
            next_comp += 1
            check(1 <= comp_id <= 250, "%s: comp_id in 1..250" % n, got=comp_id)
            check(next_card > 1, "%s: rec %d component before any card" % (n, i),
                  "S2.5: components follow their card's REC_CARD")
            if not check(CT_LABEL <= ctype <= CT_SPRITE,
                         "%s: rec %d ctype valid" % (n, i),
                         "S2.5.1: codes 0x0F+ are unassigned; a reader refuses",
                         got=hex(ctype)):
                continue
            w.comp_ids.add(comp_id)
            w.ctypes[comp_id] = ctype
            cw, ch, style, cflags = r[3], r[4], r[5], r[6]
            if ctype == CT_CANVAS:
                # A5, pinned: px divided down to the cell grid, never 0.
                check(8 <= cw <= 40, "%s: canvas w is px/8 cells" % n,
                      "S2.5: w/8 at +3, never 0 - w px 64..320 mult of 8 "
                      "(S3.3)", got=cw)
                check(4 <= ch <= 20, "%s: canvas h is ceil(px/8) rows" % n,
                      "S2.5: ceil(h/8) at +4, never 0 - h px 32..160 (S3.3)",
                      got=ch)
            elif ctype == CT_SPRITE:
                check(cw == 0 and ch == 0, "%s: sprite record w/h are 0" % n,
                      "S2.5: a sprite's geometry lives in SPRITES, not the "
                      "record", got=(cw, ch))
            else:
                check(cw <= 160, "%s: rec %d w <= 160 cells" % (n, i),
                      "S3.3: w cells 0-160; 0 = natural", got=cw)
                check(ch <= 40, "%s: rec %d h <= 40 rows" % (n, i),
                      "S3.3: h rows 0-40", got=ch)
            # S3.3's two sizes that are REQUIRED rather than natural: a
            # `box` is a frame and a `spacer` is a width, so 0 (= natural,
            # S7.3) is not a reading either one has.
            if ctype == CT_BOX:
                check(cw >= 2, "%s: rec %d box w >= 2" % (n, i),
                      "S3.3: <box> `w`,`h` required, >= 2x1 - a frame "
                      "narrower than its two verticals is not a frame",
                      got=cw)
                check(ch >= 1, "%s: rec %d box h >= 1" % (n, i),
                      "S3.3: <box> `w`,`h` required, >= 2x1", got=ch)
            elif ctype == CT_SPACER:
                check(cw >= 1, "%s: rec %d spacer w >= 1" % (n, i),
                      "S3.3: <spacer> `w` required - a spacer draws nothing "
                      "(S6.3), so its width is the whole of it", got=cw)
            check(style & 0xF0 == 0, "%s: rec %d style bits 4-7 zero" % (n, i),
                  "S2.5.2: a set bit refuses at load", got=hex(style))
            check((style >> 2) & 3 != 3, "%s: rec %d ALIGN != 3" % (n, i),
                  "S2.5.2: 3 refused by the packer", got=hex(style))
            check(cflags & 0xF8 == 0, "%s: rec %d cflags bits 3-7 zero" % (n, i),
                  "S2.5.3", got=hex(cflags))
            eq(r[7], 0, "%s: rec %d byte +7 zero" % (n, i), "S2.5")
            poff = word(r, 8)
            if poff != 0xFFFF:
                w.prop_ref[comp_id] = poff
            if ctype == CT_GRID:
                grids += 1
            if ctype == CT_CANVAS:
                canvases += 1
            if ctype == CT_SPRITE:
                check(prev_ctype in (CT_CANVAS, CT_SPRITE),
                      "%s: rec %d sprite follows its canvas" % (n, i),
                      "S2.5: a <sprite> record follows its <canvas> record "
                      "directly - sprites are not flow components")
            prev_ctype = ctype
        else:
            check(False, "%s: rec %d unknown record kind" % (n, i),
                  "S2.5: kinds are 0x00/0x01/0x02", got=hex(kind))
            prev_ctype = None
    check(end_seen, "%s: UISTREAM ends with REC_END" % n, "S2.5")
    w.cards = next_card - 1
    check(1 <= w.cards <= 8, "%s: 1..8 cards" % n, "S3.2", got=w.cards)
    check(w.entry_card <= w.cards, "%s: entry card exists" % n,
          "S2.2/S11.3: the entry card must exist", got=w.entry_card,
          want="<= %d" % w.cards)
    check(grids <= 1, "%s: at most one grid" % n,
          "S3.2: each owns a dedicated claim; the claim cap is 8 per owner "
          "(SPEC.md 50.2)", got=grids)
    check(canvases <= 1, "%s: at most one canvas" % n, "S3.2", got=canvases)
    # S2.2.1 - flags vs the tree, both directions.
    eq(bool(w.flags & WABF_GRID), grids == 1,
       "%s: WABF_GRID iff a <grid> is present" % n,
       "S2.2.1: flags are computed by the packer, never hand-set")
    eq(bool(w.flags & WABF_CANVAS), canvases == 1,
       "%s: WABF_CANVAS iff a <canvas> is present" % n, "S2.2.1")


def check_flags_sections(w):
    """S2.4 - which sections ride which flags, and the claim-KB couplings."""
    n = w.name
    grid = bool(w.flags & WABF_GRID)
    eq(FXCODE in w.sec, grid, "%s: FXCODE present iff WABF_GRID" % n,
       "S2.4: FXCODE and CELLS appear iff WABF_GRID")
    eq(CELLS in w.sec, grid, "%s: CELLS present iff WABF_GRID" % n, "S2.4")
    eq(w.grid_kb > 0, grid, "%s: grid KB non-zero iff WABF_GRID" % n,
       "S2.2.1 bit 0: grid KB byte must be non-zero; the ask must stand "
       "for the resident requirement before any I/O")
    canvas = bool(w.flags & WABF_CANVAS)
    eq(w.canvas_kb > 0, canvas, "%s: canvas KB non-zero iff WABF_CANVAS" % n,
       "S2.2/S6.10: the canvas claim exists only with a <canvas>")
    if SPRITES in w.sec:
        check(canvas, "%s: SPRITES present only with a canvas" % n,
              "S3.2: <sprite> is a child of <canvas> only")
    eq(SOURCE in w.sec, bool(w.flags & WABF_SOURCE),
       "%s: SOURCE present iff WABF_SOURCE" % n, "S2.2.1 bit 4 / S2.4")


def check_code(w):
    """S2.8/S4.5 - the function table, then every instruction boundary."""
    s, n = w.sbytes(CODE), w.name
    if not check(len(s) >= 2, "%s: CODE holds its two count bytes" % n):
        return
    F, G = s[0], s[1]
    check(F <= 128, "%s: function count <= 128" % n, "S2.8/S4.2", got=F)
    check(G <= 128, "%s: globals count <= 128" % n, "S2.8/S4.2", got=G)
    w.nglobals = G
    table_end = 2 + 4 * F
    if not check(table_end <= len(s), "%s: function table fits CODE" % n,
                 got=len(s), want=">= %d" % table_end):
        return
    for i in range(F):
        off, nargs, nlocals = word(s, 2 + 4 * i), s[2 + 4 * i + 2], s[2 + 4 * i + 3]
        check(nargs <= 8, "%s: fn %d nargs <= 8" % (n, i), "S2.8/S4.2", got=nargs)
        check(nargs <= nlocals <= 16, "%s: fn %d nlocals in nargs..16" % (n, i),
              "S2.8: args are locals 0..nargs-1", got=nlocals)
        w.funcs.append((off, nargs, nlocals))
    if F == 0:
        # A1, pinned: the body is exactly one HALT byte, length exactly 3.
        eq(s[table_end:], b"\x00",
           "%s: empty function table carries exactly one HALT guard" % n,
           "S2.8: a scriptless bundle's CODE body is one HALT byte - "
           "the section length is exactly 3")
        return
    # A7, pinned: functions packed back to back from the table's end.
    offs = [f[0] for f in w.funcs]
    eq(offs[0], table_end, "%s: fn 0 begins right after the table" % n,
       "S2.8: then bytecode; S2.14: nothing environmental, no padding")
    check(all(offs[i] < offs[i + 1] for i in range(F - 1)),
          "%s: function offsets strictly ascend" % n,
          "S2.14 rule 4: function indices are definition order", got=offs)
    ends = offs[1:] + [len(s)]
    for i, (off, nargs, nlocals) in enumerate(w.funcs):
        walk_function(w, s, i, off, ends[i], nlocals)


def walk_function(w, s, fi, start, end, nlocals):
    """One function's run: boundaries exact, targets in bounds and aligned."""
    n = w.name
    bounds = set()
    pos, last_op = start, None
    jumps = []                                            # (site, target)
    while pos < end:
        op = s[pos]
        if not check(op in OPLEN, "%s: fn %d op 0x%02X at +%d unknown"
                     % (n, fi, op, pos),
                     "S4.5: 38 opcodes, 0x00-0x25 - the dispatch table is "
                     "exactly 38 entries"):
            return
        ln = OPLEN[op]
        if not check(pos + ln <= end, "%s: fn %d op at +%d truncated" % (n, fi, pos),
                     "S4.5: an instruction may not straddle the function end",
                     got="%d+%d" % (pos, ln), want="<= %d" % end):
            return
        bounds.add(pos)
        # Operand validity, per S4.5's table.
        if op in (OP_JMP, OP_JZ, OP_JNZ):
            rel = struct.unpack_from("<h", s, pos + 1)[0]
            jumps.append((pos, pos + 3 + rel))            # rel16 is from the
        elif op == OP_CALL:                               # byte after itself
            check(s[pos + 1] < len(w.funcs), "%s: fn %d CALL target in table"
                  % (n, fi), "S4.5", got=s[pos + 1], want="< %d" % len(w.funcs))
        elif op in (OP_LDG, OP_STG, OP_INCG, OP_DECG):
            check(s[pos + 1] < w.nglobals, "%s: fn %d global index in range"
                  % (n, fi), "S2.8: global indices are declaration order",
                  got=s[pos + 1], want="< %d" % w.nglobals)
        elif op in (OP_LDL, OP_STL):
            check(s[pos + 1] < nlocals, "%s: fn %d local index < nlocals"
                  % (n, fi), "S2.8/S4.7.1", got=s[pos + 1], want="< %d" % nlocals)
        elif op == OP_PUSHA:
            # A3, pinned: every PUSHA operand is an app atom the pool holds.
            check(64 <= s[pos + 1] < 64 + w.atom_count,
                  "%s: fn %d PUSHA atom is a pooled app atom" % (n, fi),
                  "S2.7: string literals always intern to the pool - ids "
                  "1..63 have no runtime string table, so a well-known id "
                  "here would push nothing", got=s[pos + 1])
        elif op == OP_PUSHC:
            check(s[pos + 1] == 0 or s[pos + 1] in w.comp_ids,
                  "%s: fn %d PUSHC names a component (or 0, the app)"
                  % (n, fi), "S4.3: comp_id, 0 = app", got=s[pos + 1])
        elif op in (OP_GETP, OP_SETP):
            check(s[pos + 1] in WK_PROPS, "%s: fn %d GETP/SETP atom is a "
                  "property atom" % (n, fi),
                  "S2.7.1/S6: component surfaces are well-known property atoms",
                  got=s[pos + 1])
        elif op == OP_CALLM:
            check(s[pos + 1] in WK_METHODS, "%s: fn %d CALLM atom is a method "
                  "atom" % (n, fi), "S2.7.1: methods are atoms 32..41",
                  got=s[pos + 1])
        elif op == OP_BUILT:
            b8, argc = s[pos + 1], s[pos + 2]
            if check(b8 in BUILTIN_ARITY, "%s: fn %d BUILT index valid" % (n, fi),
                     "S8.1: indices 0..11 pinned", got=b8):
                lo, hi = BUILTIN_ARITY[b8]
                check(lo <= argc <= hi, "%s: fn %d BUILT %d argc" % (n, fi, b8),
                      "S8.1: wrong argc is a pack error", got=argc,
                      want="%d..%d" % (lo, hi))
                w.builtins_used.add(b8)
        pos += ln
        last_op = op
    eq(pos, end, "%s: fn %d boundaries land exactly at its end" % (n, fi),
       "S2.8: each function a contiguous run - an overhang means the two "
       "implementations disagree about where an instruction starts")
    check(last_op in (OP_RET, OP_HALT), "%s: fn %d ends in RET or HALT" % (n, fi),
          "S2.8/S4.5: the compiler emits PUSHN+RET at a fall-off",
          got=hex(last_op) if last_op is not None else None)
    for site, target in jumps:
        check(start <= target < end, "%s: fn %d jump at +%d stays in the "
              "function" % (n, fi, site),
              "S4.5: rel16 - a jump out of the function is a corrupt handler",
              got=target, want="%d..%d" % (start, end - 1))
        check(target in bounds, "%s: fn %d jump at +%d lands on an instruction "
              "boundary" % (n, fi, site),
              "a mid-instruction target executes operand bytes as opcodes",
              got=target)


def check_flags_code(w):
    """S2.2.1 bits 2-3 against what CODE actually calls."""
    n = w.name
    uses_timer = B_TIMER in w.builtins_used
    uses_state = bool({B_SAVESTATE, B_LOADSTATE} & w.builtins_used)
    has_input = CT_INPUT in w.ctypes.values()
    has_grid = CT_GRID in w.ctypes.values()
    # A8, pinned: exactly three causes, enforced both directions.
    eq(bool(w.flags & WABF_TIMER), uses_timer or has_input or has_grid,
       "%s: WABF_TIMER iff timer()/input/grid" % n,
       "S2.2.1 bit 2: exactly three causes - timer() in CODE, an <input>, "
       "or a <grid> (its formula bar is a library-wired input, S6.9); "
       "flags are computed by the packer, and a wrong bit makes the "
       "load-time capability check lie")
    eq(bool(w.flags & WABF_STATE), uses_state,
       "%s: WABF_STATE iff saveState/loadState is called" % n, "S2.2.1 bit 3")


def check_props(w):
    """S2.6 - every referenced block walks; the app block; blobs; menus."""
    s, n = w.sbytes(PROPS), w.name
    _, _, app_off = w.sec[PROPS]
    # S2.14 rule 5: blocks emitted in UISTREAM owner order, app block last.
    # w.ctypes is insertion-ordered = UISTREAM order, so this walk is too.
    ordered = [(cid, w.prop_ref[cid]) for cid in w.ctypes if cid in w.prop_ref]
    offs = [o for _, o in ordered]
    check(all(offs[i] < offs[i + 1] for i in range(len(offs) - 1)),
          "%s: component prop blocks ascend in UISTREAM order" % n,
          "S2.14 rule 5", got=offs)
    check(not offs or app_off > offs[-1], "%s: app block emitted last" % n,
          "S2.14 rule 5: the app block is last", got=app_off)
    for cid, off in ordered:
        recs = walk_prop_block(w, off, "comp %d" % cid)
        if recs is None:
            continue
        for name, kind, value in recs:
            if name == ATOM_MENUS:
                check(False, "%s: comp %d carries MENUS" % (n, cid),
                      "S2.6.2: MENUS belongs to the app block alone")
            if name == ATOM_ITEMS:
                check(w.ctypes[cid] == CT_LIST, "%s: ITEMS on a non-list" % n,
                      "S2.6.1: the list's prop block carries ITEMS")
                check_items_blob(w, value)
            if kind == PK_SPRITE:
                check(w.ctypes[cid] == CT_SPRITE,
                      "%s: PK_SPRITE on comp %d, not a sprite" % (n, cid),
                      "S3.3: img -> PK_SPRITE on <sprite> only")
                # A4, pinned: the record is named by atom 11 `frame`.
                eq(name, ATOM_FRAME,
                   "%s: comp %d PK_SPRITE record named `frame`" % (n, cid),
                   "S3.3: no `img` atom exists; the record doubles as the "
                   "frame property's initial value")
        check_comp_props(w, cid, w.ctypes[cid], recs)
        if w.ctypes[cid] == CT_GRID:
            got = {nm: v for nm, k, v in recs if k == PK_INT}
            # Presence and the 1..26 / 1..256 ranges are S33_PROPS' and
            # S33_REQUIRED's; what is left here is the pair's arithmetic.
            if ATOM_ROWS in got and ATOM_COLS in got:
                w.grid_rows, w.grid_cols = got[ATOM_ROWS], got[ATOM_COLS]
                check(w.grid_rows * w.grid_cols <= 6140,
                      "%s: rows x cols <= 6140" % n,
                      "S5.6: the cell store plus its pool must fit a 26KB claim",
                      got=w.grid_rows * w.grid_cols)
                # S5.6: the packer's own sizing formula, 8-floored.
                ask = max(8, (16 + w.grid_rows * w.grid_cols * 4 + 1023)
                          // 1024 + 2)
                eq(w.grid_kb, ask, "%s: grid KB is the S5.6 formula" % n,
                   "max(8, ceil((16 + rows*cols*4)/1024) + 2) - the 8 floor "
                   "is S2.2's claim envelope; a drifted ask either wastes "
                   "heap or overflows the pool at run time")
    app = walk_prop_block(w, app_off, "app")
    if app is not None:
        for name, kind, value in app:
            check(name in (ATOM_CARD, ATOM_MENUS, ATOM_START),
                  "%s: app block carries only CARD, MENUS and start" % n,
                  "S2.6.2: nothing else may appear in the app block", got=name)
            if name == ATOM_START:
                eq(kind, PK_FUNC, "%s: app start is PK_FUNC" % n,
                   "S2.6.2: the synthesized module-init function")
                eq(value, len(w.funcs) - 1,
                   "%s: app start names the last function" % n,
                   "S2.6.2: the init is appended as the last function-table "
                   "entry")
            if name == ATOM_CARD:
                eq(kind, PK_INT, "%s: app CARD is PK_INT" % n, "S2.6.2")
                eq(value, w.entry_card, "%s: app CARD mirrors the header" % n,
                   "S2.6.2: mirror of the header's entry card")
            if name == ATOM_MENUS:
                check_menus_blob(w, value)


def check_items_blob(w, off):
    """S2.6.1 - a list's items blob."""
    s, n = w.sbytes(PROPS), w.name
    if not check(off < len(s), "%s: ITEMS blob offset in PROPS" % n, got=off):
        return
    count = s[off]
    check(count <= 64, "%s: list items <= 64" % n, "S2.6.1", got=count)
    if not check(off + 1 + count <= len(s), "%s: ITEMS blob in bounds" % n):
        return
    for i in range(count):
        check(w.atom_ok(s[off + 1 + i]), "%s: item %d atom assigned" % (n, i),
              "S2.6.1: count atom-id bytes, one per item", got=s[off + 1 + i])


def check_menus_blob(w, off):
    """S2.6.2 - the MENUS blob: counts, atoms, function indices."""
    s, n = w.sbytes(PROPS), w.name
    if not check(off < len(s), "%s: MENUS blob offset in PROPS" % n, got=off):
        return
    nmenus = s[off]
    check(1 <= nmenus <= 5, "%s: menu count 1..5" % n,
          "S2.6.2: MENU_APPMAX is the kernel's own bound (SPEC.md 12.2)",
          got=nmenus)
    pos = off + 1
    for m in range(nmenus):
        if not check(pos + 2 <= len(s), "%s: menu %d header in bounds" % (n, m)):
            return
        title, nitems = s[pos], s[pos + 1]
        check(w.atom_ok(title), "%s: menu %d title atom assigned" % (n, m),
              got=title)
        check(len(w.atom_text.get(title, b"")) <= 8,
              "%s: menu %d title <= 8 chars" % (n, m),
              "S3.3: <menu> `title` <= 8 chars - the kernel's bar is drawn "
              "from these and a longer one runs into the next menu",
              got=w.atom_text.get(title))
        check(1 <= nitems <= 8, "%s: menu %d item count 1..8" % (n, m),
              "S2.6.2", got=nitems)
        pos += 2
        if not check(pos + 2 * nitems <= len(s),
                     "%s: menu %d items in bounds" % (n, m)):
            return
        for it in range(nitems):
            label, fn = s[pos + 2 * it], s[pos + 2 * it + 1]
            check(w.atom_ok(label), "%s: menu %d item %d label atom" % (n, m, it),
                  got=label)
            check(len(w.atom_text.get(label, b"")) <= 24,
                  "%s: menu %d item %d label <= 24 glyphs" % (n, m, it),
                  "S3.3: <item> content is the label, <= 24 glyphs",
                  got=w.atom_text.get(label))
            check(fn == 0xFF or fn < len(w.funcs),
                  "%s: menu %d item %d oncommand index" % (n, m, it),
                  "S2.6.2: a function index, or 0xFF for none (present, inert)",
                  got=fn, want="0xFF or < %d" % len(w.funcs))
        pos += 2 * nitems


def check_fxcode(w):
    """S2.9/S5.3 - every RPN stream: boundaries, FEND, depth <= 16."""
    if FXCODE not in w.sec:
        return
    s, n = w.sbytes(FXCODE), w.name
    _, _, extra = w.sec[FXCODE]
    if not check(len(s) >= 2, "%s: FXCODE holds its count word" % n):
        return
    N = word(s, 0)
    w.fx_count = N
    eq(extra, N, "%s: FXCODE extra word is the formula count" % n, "S2.4")
    if not check(2 + 2 * N <= len(s), "%s: FXCODE offset table fits" % n):
        return
    offs = [word(s, 2 + 2 * i) for i in range(N)]
    ends = offs[1:] + [len(s)]
    check(all(offs[i] < offs[i + 1] for i in range(N - 1)),
          "%s: formula offsets ascend" % n,
          "S2.9: N offset words, then N streams", got=offs)
    if N:
        eq(offs[0], 2 + 2 * N, "%s: formula 0 begins after the table" % n, "S2.9")
    for i in range(N):
        pos, end, depth = offs[i], ends[i], 0
        while True:
            if not check(pos < end, "%s: formula %d ran past its end without "
                         "FEND" % (n, i), "S2.9: each stream terminated by FEND"):
                break
            op = s[pos]
            if not check(op in FXOPS, "%s: formula %d op 0x%02X unknown"
                         % (n, i, op), "S5.3: 23 opcodes, 0x00-0x16"):
                break
            ln, pop, push = FXOPS[op]
            if not check(pos + ln <= end, "%s: formula %d op at +%d truncated"
                         % (n, i, pos)):
                break
            if op == FCELL or op == FRANGE:
                cols = [s[pos + 2]] if op == FCELL else [s[pos + 2], s[pos + 4]]
                for c in cols:
                    check(c <= 25, "%s: formula %d column <= 25 (A..Z)" % (n, i),
                          "S5.1: column A..Z", got=c)
                if w.grid_cols is not None:
                    rows = [s[pos + 1]] if op == FCELL else [s[pos + 1], s[pos + 3]]
                    for c in cols:
                        check(c < w.grid_cols, "%s: formula %d column inside "
                              "the grid" % (n, i), got=c,
                              want="< %d" % w.grid_cols)
                    for r in rows:
                        check(r < w.grid_rows, "%s: formula %d row inside the "
                              "grid" % (n, i), got=r, want="< %d" % w.grid_rows)
            if op == FEND:
                eq(depth, 1, "%s: formula %d leaves one result slot" % (n, i),
                   "S5.3: the single remaining slot is the result")
                eq(pos + 1, end, "%s: formula %d FEND lands exactly at its "
                   "end" % (n, i), "S2.9: streams pack back to back")
                break
            check(depth >= pop, "%s: formula %d underflows its stack" % (n, i),
                  "S5.3", got=depth, want=">= %d" % pop)
            depth = depth - pop + push
            check(depth <= 16, "%s: formula %d exceeds depth 16" % (n, i),
                  "S5.3: stack depth cap 16; deeper is refused at pack",
                  got=depth)
            pos += ln


def check_cells(w):
    """S2.10 - fixed 8-byte records, sorted row-major, payloads typed."""
    if CELLS not in w.sec:
        return
    s, n = w.sbytes(CELLS), w.name
    _, length, extra = w.sec[CELLS]
    eq(length % 8, 0, "%s: CELLS length a multiple of 8" % n, "S2.10")
    count = length // 8
    eq(extra, count, "%s: CELLS extra word is the record count" % n, "S2.4")
    prev = (-1, -1)
    for i in range(count):
        r = s[i * 8:(i + 1) * 8]
        row, col, kind = r[0], r[1], r[2]
        check((row, col) > prev, "%s: cell %d sorted row-major, no repeats"
              % (n, i), "S2.10: sorted (row, then column), only non-empty "
              "cells present", got=(row, col), want="> %s" % (prev,))
        prev = (row, col)
        check(col <= 25, "%s: cell %d col 0..25" % (n, i), "S2.10", got=col)
        if w.grid_cols is not None:
            check(col < w.grid_cols and row < w.grid_rows,
                  "%s: cell %d inside the grid" % (n, i),
                  "S3.3: the grid's declared rows x cols bound its cells",
                  got=(row, col), want="< (%d, %d)" % (w.grid_rows, w.grid_cols))
        check(1 <= kind <= 3, "%s: cell %d kind 1..3" % (n, i), "S2.10", got=kind)
        eq(r[3], 0, "%s: cell %d byte +3 zero" % (n, i), "S2.10")
        if kind == 2:
            eq(word(r, 6), 0, "%s: cell %d atom payload high word zero" % (n, i),
               "S2.10: an atom id in the low word")
            check(w.atom_ok(word(r, 4)), "%s: cell %d atom assigned" % (n, i),
                  got=word(r, 4))
        elif kind == 3:
            eq(word(r, 6), 0, "%s: cell %d formula payload high word zero"
               % (n, i), "S2.10: a formula index in the low word")
            check(word(r, 4) < w.fx_count, "%s: cell %d formula index in "
                  "FXCODE" % (n, i), "S2.9: CELLS reference formulas by index",
                  got=word(r, 4), want="< %d" % w.fx_count)


def check_sprites(w):
    """S2.11 - descriptors ranged, image+mask data packed and in bounds."""
    if SPRITES not in w.sec:
        return
    s, n = w.sbytes(SPRITES), w.name
    _, length, extra = w.sec[SPRITES]
    if not check(len(s) >= 2, "%s: SPRITES holds its count" % n):
        return
    S = s[0]
    w.sprite_count = S
    check(1 <= S <= 16, "%s: sprite count 1..16" % n,
          "S2.11/S6.10: <= 16 sprites per canvas", got=S)
    eq(extra, S, "%s: SPRITES extra word is the sprite count" % n, "S2.4")
    eq(s[1], 0, "%s: SPRITES byte +1 zero" % n, "S2.11")
    if not check(2 + 8 * S <= len(s), "%s: sprite descriptors fit" % n):
        return
    expect = 2 + 8 * S                                    # data in desc order
    for i in range(S):
        d = s[2 + 8 * i:2 + 8 * i + 8]
        wb, h, frames = d[0], d[1], d[2]
        check(1 <= wb <= 8, "%s: sprite %d w_bytes 1..8" % (n, i),
              "S2.11: width px = 8 x w_bytes, 8..64", got=wb)
        check(1 <= h <= 64, "%s: sprite %d h_px 1..64" % (n, i), got=h)
        check(1 <= frames <= 8, "%s: sprite %d frames 1..8" % (n, i), got=frames)
        eq(d[3], 0, "%s: sprite %d byte +3 zero" % (n, i), "S2.11")
        eq(word(d, 6), 0, "%s: sprite %d byte +6 zero" % (n, i), "S2.11")
        off = word(d, 4)
        eq(off, expect, "%s: sprite %d data packed in descriptor order" % (n, i),
           "S2.11: image data, in descriptor order - per frame, image then "
           "AND mask")
        expect = off + frames * 2 * h * wb
        check(expect <= len(s), "%s: sprite %d data in bounds" % (n, i),
              got=expect, want="<= %d" % len(s))
    eq(expect, len(s), "%s: SPRITES length is exactly its data" % n,
       "S2.11: no trailing bytes")


def check_source(w):
    """S2.13 - the optional round-trip text."""
    if SOURCE not in w.sec:
        return
    s, n = w.sbytes(SOURCE), w.name
    _, length, wml_len = w.sec[SOURCE]
    check(wml_len <= length, "%s: SOURCE extra word (WML length) fits" % n,
          "S2.13: the extra word is the WML length, so the reader splits "
          "without a scan", got=wml_len, want="<= %d" % length)
    check(all(0x20 <= c <= 0x7E or c == 0x0A for c in s),
          "%s: SOURCE is folded text plus LF" % n,
          "S2.13: both texts folded and LF-terminated; S3.1's fold admits "
          "only 0x20..0x7E")
    for half, label in ((s[:wml_len], "WML"), (s[wml_len:], "WJS")):
        check(not half or half.endswith(b"\n"),
              "%s: SOURCE %s text LF-terminated" % (n, label), "S2.13")


def check_bundle(path):
    with open(path, "rb") as f:
        blob = f.read()
    w = Bundle(blob, os.path.basename(path))
    if not check(len(blob) >= 32, "%s: at least a header" % w.name,
                 "S2.2: the header ends at +32 exactly", got=len(blob)):
        return
    nsec = check_header(w)
    if not check_sections(w, nsec):
        return
    # Order is dependency order, stated because it is load-bearing:
    check_atoms(w)         # atom count - every later atom-id check needs it
    check_sprites(w)       # sprite count - PK_SPRITE refs need it
    check_uistream(w)      # comp_ids/ctypes - PUSHC and prop owners need them
    check_code(w)          # function table - PK_FUNC and MENUS refs need it
    check_flags_sections(w)
    check_flags_code(w)
    check_props(w)         # needs comp_ids, funcs, sprites, atoms; reads the
    check_fxcode(w)        # grid's rows/cols, which the FXCODE and CELLS
    check_cells(w)         # in-grid bounds checks then use
    check_source(w)


def main():
    where = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build")
    paths = sorted(glob.glob(os.path.join(where, "*.WAB")) +
                   glob.glob(os.path.join(where, "*.wab")))
    if not check(len(paths) > 0, "no .WAB bundles under %s" % where,
                 "the build should have packed the demo bundles "
                 "(weavesim --pack runs host-side in `all`, WEAVE-SPEC 13.1) "
                 "- run `make`, or pass the bundle directory as argv[1]"):
        done("t_wab")
    for p in paths:
        try:
            check_bundle(p)
        except Exception as e:                            # a crash IS a failure
            check(False, "%s: reader crashed" % os.path.basename(p),
                  "every byte off a disk is hostile (SPEC.md 19) and this "
                  "reader must survive anything the glob hands it",
                  got="%s: %s" % (type(e).__name__, e))
    print("t_wab: %d bundles read from the spec alone" % len(paths))
    done("t_wab")


if __name__ == "__main__":
    main()
