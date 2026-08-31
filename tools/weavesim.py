#!/usr/bin/env python3
"""The Weave family's host reference implementation and executable spec.

docs/WEAVE-SPEC.md is the binding contract; this file is that contract run
before a byte of 8086 exists, for the reason tools/htmsim.py exists: the
browser's host model found three real bugs before any assembly was written,
and the negative results were cheaper than the code. Three instruments in
one file (WEAVE-SPEC 12.1):

  * the executable spec - the WML parser with the pack-time rejections, the
    WJS compiler emitting WEAVE-SPEC 4.6's pinned templates byte for byte,
    the FX formula compiler, the atom interner, the sprite converter and the
    bundle packer (--pack), all deterministic: same inputs, same bytes,
    because LOOM.OVL's on-machine pack must equal this one (WEAVE-SPEC 11.1);
  * the oracle - a WVM interpreter and FX evaluator that execute a packed
    bundle headlessly (--run, with a scripted event file), a 16.16 evaluator
    that wraps at 32 bits the way the 8086 core will, and the flow-walk
    layout (--render) the 8086 must match cell for cell;
  * the generator - --emit-optab (the 38-entry WVM jump table),
    --emit-foldtab (the Latin-1 fold, from htmsim's ONE definition), and
    --costs (WEAVE-SPEC 14's component cost table), so the model and the
    machine cannot drift apart on a shared table.

What it deliberately does NOT model: pixels, the gfx lock, claim arenas and
the between-slice collector, and time. It answers what the bundle CONTAINS,
what a handler DOES, where the walk PUTS things, and what an interaction
COSTS in gfx calls priced by the measured constants - which is what
PERFORMANCE.md Part 2 says a redraw is priced by.

  python3 tools/weavesim.py --pack apps/weave/demos/form.wml -o FORM.WAB
  python3 tools/weavesim.py --render apps/weave/demos/form.wml --all-adapters
  python3 tools/weavesim.py --run FORM.WAB --events events.txt
  python3 tools/weavesim.py --costs
  python3 tools/weavesim.py --selfcheck
"""
import argparse
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import htmsim  # the ONE Latin-1 fold definition (WEAVE-SPEC 3.1, --emit-foldtab)

# --- the measured constants, with provenance ---------------------------------
# A drawing call's fixed part and the per-cell glyph cost: PERFORMANCE.md
# Part 2, measured on the field 5150; the per-adapter cell figures are
# htmsim's calibrated ones (checked against the measured 78-cell row in
# selfcheck, the same check htmsim carries).
CALL_US = 756.0
GLYPH_US = 900.0            # the ~900 us/glyph headline figure (CLAUDE.md)
# The band composer (rcband shape): PERFORMANCE.md Set 68 - 860 us/call +
# 173 us/cell, blit included; a 79-cell row is 14.5 ms against font_run's ~60.
BAND_CALL_US = 860.0
BAND_CELL_US = 173.0
# Field-measured rows carried as constants, not derived (WEAVE-SPEC 14):
GLYPH_TOGGLE_MS = (35, 50)  # os88ui_glyph toggle, 44-64 set bits, field 5150
LIST_SCROLL_MS = (83, 90)   # PERFORMANCE.md Part 5's scroll-one-line contract
KEYSTROKE_MS = 1.8          # the Note Pad contract (SPEC.md 27.2), ~2 cells

# Content areas per adapter, derived in WEAVE-SPEC 7.1.1 from the platform's
# own constants - CW = floor([vid_w]/8), CH = floor(([vid_h]-64)/8) over
# SPEC.md 11.95's standard rect. The CW divisor lost its -1 at SPEC.md
# 11.95.3: a window spanning the screen has no RIGHT border either, so the
# last cell of the row now has somewhere to go. NOT htmsim's viewport: the browser is not
# maximized on Hercules (it takes 90% of the band), and copying its figure
# is where this table's herc ch=36 came from - four content pixels that do
# not exist, and a row the 8086 would have been told to lay out on.
# cell_us is Part 2's per-adapter glyph cost; the walk needs only cw/ch.
ADAPTERS = {
    "cga":  dict(name="CGA 640x200",      cell_us=918.0, cw=80, ch=17),
    "herc": dict(name="Hercules 720x348", cell_us=905.0, cw=90, ch=35),
    "vga":  dict(name="VGA 640x480",      cell_us=620.0, cw=80, ch=52),
}
GEOM_ALIAS = {"640x200": "cga", "720x348": "herc", "640x480": "vga"}

# --- the pinned format constants (WEAVE-SPEC 2) ------------------------------
WAB_MAGIC = b"WAB\x1a"
WAB_VERSION = 1
WAB_CAP = 0xF800            # 63,488 bytes - every offset a 16-bit word

WABF_GRID, WABF_CANVAS, WABF_TIMER, WABF_STATE, WABF_SOURCE = 1, 2, 4, 8, 16

(SEC_UISTREAM, SEC_PROPS, SEC_CODE, SEC_ATOMS, SEC_FXCODE,
 SEC_CELLS, SEC_SPRITES, SEC_ICON, SEC_SOURCE) = range(1, 10)

REC_END, REC_CARD, REC_COMP = 0, 1, 2
ST_BOLD, ST_INVERT = 1, 2               # style byte bits (WEAVE-SPEC 2.5.2)
ALIGNS = {"left": 0, "center": 1, "right": 2}
CF_BREAK, CF_HIDDEN, CF_DISABLED = 1, 2, 4

PK_INT, PK_ATOM, PK_BLOB, PK_FUNC, PK_SPRITE = range(5)

CTYPE = {"label": 0x01, "text": 0x02, "rule": 0x03, "box": 0x04,
         "spacer": 0x05, "meter": 0x06, "button": 0x07, "check": 0x08,
         "radio": 0x09, "input": 0x0A, "list": 0x0B, "grid": 0x0C,
         "canvas": 0x0D, "sprite": 0x0E}
CTYPE_NAME = {v: k for k, v in CTYPE.items()}

# The well-known atom table, WEAVE-SPEC 2.7.1 - pinned, never in the pool.
WK = {"text": 1, "value": 2, "label": 3, "enabled": 4, "checked": 5,
      "hidden": 6, "x": 7, "y": 8, "vx": 9, "vy": 10, "frame": 11,
      "shown": 12, "min": 13, "max": 14, "rows": 15, "cols": 16, "sel": 17,
      "editing": 18, "group": 19, "card": 20, "selrow": 21, "selcol": 22,
      "walls": 23, "tick": 24,
      "cell": 32, "setCell": 33, "recalc": 34, "select": 35, "stop": 36,
      "go": 37, "set": 38, "get": 39, "start": 40, "clear": 41,
      "onclick": 48, "onchange": 49, "onkey": 50, "onselect": 51,
      "onedit": 52, "oncalc": 53, "oncollide": 54, "onwall": 55,
      "onscore": 56, "ontick": 57, "oncommand": 58, "ontimer": 59,
      "onalert": 60, "ITEMS": 61, "MENUS": 62}
WK_NAME = {v: k for k, v in WK.items()}
APP_ATOM0, APP_ATOM_MAX = 64, 187       # ids 64..250; the cap is 187

# The 38 WVM opcodes, 0x00-0x25, WEAVE-SPEC 4.5. Operand spec: '' none,
# 'b' one byte, 'w' imm16, 'r' rel16, 'bb' two bytes.
OPS = [("HALT", ""), ("PUSHI", "w"), ("PUSHA", "b"), ("PUSHN", ""),
       ("PUSHB", "b"), ("PUSHC", "b"), ("LDG", "b"), ("STG", "b"),
       ("LDL", "b"), ("STL", "b"), ("POP", ""), ("DUP", ""),
       ("ADD", ""), ("SUB", ""), ("MUL", ""), ("DIV", ""), ("MOD", ""),
       ("NEG", ""), ("EQ", ""), ("NE", ""), ("LT", ""), ("LE", ""),
       ("GT", ""), ("GE", ""), ("NOT", ""), ("JMP", "r"), ("JZ", "r"),
       ("JNZ", "r"), ("CALL", "b"), ("RET", ""), ("GETP", "b"),
       ("SETP", "b"), ("CALLM", "bb"), ("BUILT", "bb"), ("INCG", "b"),
       ("DECG", "b"), ("AGET", ""), ("ASET", "")]
OP = {name: i for i, (name, _) in enumerate(OPS)}
assert len(OPS) == 38 and OP["ASET"] == 0x25

# The 23 FX opcodes, 0x00-0x16, WEAVE-SPEC 5.3.
FXOPS = [("FEND", ""), ("FNUM", "d"), ("FCELL", "bb"), ("FRANGE", "bbbb"),
         ("FADD", ""), ("FSUB", ""), ("FMUL", ""), ("FDIV", ""),
         ("FNEG", ""), ("FEQ", ""), ("FNE", ""), ("FLT", ""), ("FLE", ""),
         ("FGT", ""), ("FGE", ""), ("FSUM", ""), ("FMIN", ""), ("FMAX", ""),
         ("FAVG", ""), ("FCOUNT", ""), ("FIF", ""), ("FABS", ""),
         ("FROUND", "")]
FXOP = {name: i for i, (name, _) in enumerate(FXOPS)}
assert len(FXOPS) == 23 and FXOP["FROUND"] == 0x16
FX_FUNCS = {"SUM": ("FSUM", "range"), "MIN": ("FMIN", "range"),
            "MAX": ("FMAX", "range"), "AVG": ("FAVG", "range"),
            "COUNT": ("FCOUNT", "range"), "IF": ("FIF", 3),
            "ABS": ("FABS", 1), "ROUND": ("FROUND", 1)}

# Builtins, indices pinned (WEAVE-SPEC 8.1): (name, min argc, max argc).
BUILTINS = [("alert", 1, 2), ("timer", 2, 2), ("saveState", 0, 0),
            ("loadState", 0, 0), ("playSound", 1, 1), ("tone", 2, 2),
            ("str", 1, 1), ("len", 1, 1), ("substr", 3, 3), ("find", 2, 2),
            ("rand", 1, 1), ("array", 1, 1)]
BUILTIN = {name: i for i, (name, _, _) in enumerate(BUILTINS)}

# Value tags (WEAVE-SPEC 4.3).
T_INT, T_STR, T_ARR, T_COMP, T_NULL, T_BOOL = range(6)
NULL = (T_NULL, 0)

# The WJS surface of each component (WEAVE-SPEC 6): readable atoms, writable
# atoms, methods with arity. 'hidden' is get/set on every flow component
# (hide/show is the dynamic UI, WEAVE-SPEC 6.12); sprites use 'shown'.
def _surf(get="", set_="", methods=None):
    g = {WK[n] for n in get.split()} | {WK["hidden"]}
    s = {WK[n] for n in set_.split()} | {WK["hidden"]}
    return dict(get=g, set=s, methods=methods or {})

SURFACE = {
    "label":  _surf("text", "text"),
    "text":   _surf("text", "text"),
    "rule":   _surf(), "box": _surf(), "spacer": _surf(),
    "meter":  _surf("value max", "value"),
    "button": _surf("label enabled", "label enabled"),
    "check":  _surf("checked enabled label", "checked enabled"),
    "radio":  _surf("checked enabled label", "checked enabled"),
    "input":  _surf("text cols enabled", "text enabled"),
    "list":   _surf("sel rows", "sel", {WK["set"]: 2, WK["get"]: 1}),
    "grid":   _surf("selrow selcol rows cols", "",
                    {WK["cell"]: 2, WK["setCell"]: 3, WK["recalc"]: 0,
                     WK["select"]: 2, WK["clear"]: 0}),
    "canvas": _surf("", "", {WK["start"]: 1, WK["stop"]: 0}),
    "sprite": dict(get={WK[n] for n in ("x", "y", "vx", "vy", "frame",
                                        "shown")},
                   set={WK[n] for n in ("x", "y", "vx", "vy", "frame",
                                        "shown")},
                   methods={}),
    "app":    dict(get=set(), set=set(), methods={WK["go"]: 1}),
}

# --- WEAVE-SPEC 3.2/3.3: the closed element inventory ------------------------
# Children rule: which element names may nest where.
CHILDREN = {"app": {"card", "menu", "script"},
            "card": set(CTYPE) - {"sprite"},
            "canvas": {"sprite"}, "list": {"item"}, "menu": {"item"}}
TEXT_CONTENT = {"label", "text", "button", "check", "radio", "item"}

# Per-element attributes in WEAVE-SPEC 3.3's table order (the interning
# order, WEAVE-SPEC 2.14 rule 3a). Events are attributes too.
COMMON_ATTRS = ["id", "w", "h", "style", "align", "br", "hidden", "disabled"]
ELEM_ATTRS = {
    "app":    ["name", "vm"],
    "card":   ["id"],
    "label":  [], "text": [], "rule": [], "box": [], "spacer": [],
    "meter":  ["value", "max"],
    "button": ["onclick"],
    "check":  ["checked", "onchange"],
    "radio":  ["group", "checked", "onchange"],
    "input":  ["cols", "text", "onchange", "onkey"],
    "list":   ["rows", "onselect"],
    "grid":   ["cols", "rows", "onselect", "onedit", "oncalc"],
    "canvas": ["w", "h", "walls", "tick", "onkey", "oncollide", "onwall",
               "onscore", "ontick"],
    "sprite": ["id", "img", "x", "y", "shown"],
    "menu":   ["title"],
    "item":   ["oncommand"],
    "script": ["src"],
}
FLOW = set(CTYPE) - {"sprite"}      # takes the common attributes


# --- errors and text folding -------------------------------------------------
class PackError(Exception):
    """A pack-time refusal: '<file>:<line>: <message>' (WEAVE-SPEC 10.5)."""
    def __init__(self, fname, line, msg):
        super().__init__("%s:%d: %s" % (fname, line, msg))
        self.fname, self.line, self.msg = fname, line, msg


class BundleError(Exception):
    """A malformed bundle, refused with the field named (WEAVE-SPEC 10.4)."""
    def __init__(self, name, field):
        super().__init__("%s is not a Weave bundle (%s)." % (name, field))
        self.field = field


def fold_text(s):
    """Everything to ASCII 0x20..0x7E through htmsim's one fold table
    (WEAVE-SPEC 3.1). Newlines/tabs become spaces here - WML collapses
    whitespace and atom strings carry none of it."""
    out = []
    for ch in s:
        cp = ord(ch)
        if 0x20 <= cp <= 0x7E:
            out.append(ch)
        elif ch in "\n\t\r":
            out.append(" ")
        else:
            out.append(htmsim.FOLD.get(cp, "?"))
    return "".join(out)


ENTITIES = {"lt": "<", "gt": ">", "amp": "&", "quot": '"'}


def expand_entities(s, fname, line):
    """Exactly four entities; a bare '&' not forming one is a pack error
    (WEAVE-SPEC 3.1)."""
    out, i = [], 0
    while True:
        amp = s.find("&", i)
        if amp < 0:
            out.append(s[i:])
            break
        out.append(s[i:amp])
        semi = s.find(";", amp, amp + 6)
        body = s[amp + 1:semi] if semi > 0 else ""
        if body in ENTITIES:
            out.append(ENTITIES[body])
            i = semi + 1
        else:
            raise PackError(fname, line, "&%s: not one of &lt; &gt; &amp; "
                            "&quot; - the entity set is closed "
                            "(WEAVE-SPEC 3.1)" % (body if body else ""))
    return "".join(out)


def collapse_ws(s):
    return re.sub(r"\s+", " ", s).strip()


# --- the WML scanner (WEAVE-SPEC 3.1) ----------------------------------------
class Node:
    __slots__ = ("name", "attrs", "children", "texts", "line")

    def __init__(self, name, line):
        self.name, self.line = name, line
        self.attrs = {}          # lowercased name -> (value, line); bool: "1"
        self.children = []       # Node
        self.texts = []          # (string, line) raw text runs

    def content(self, fname):
        """Folded, whitespace-collapsed text content."""
        return collapse_ws(" ".join(
            fold_text(expand_entities(t, fname, ln)) for t, ln in self.texts))


NAME_RE = re.compile(r"[A-Za-z][A-Za-z0-9]*")


class WmlScanner:
    def __init__(self, text, fname):
        self.t, self.i, self.line, self.fname = text, 0, 1, fname

    def err(self, msg, line=None):
        raise PackError(self.fname, line or self.line, msg)

    def adv(self, k):
        self.line += self.t.count("\n", self.i, self.i + k)
        self.i += k

    def skip_ws(self):
        while self.i < len(self.t) and self.t[self.i] in " \t\r\n":
            self.adv(1)

    def lit(self, s):
        return self.t.startswith(s, self.i)

    def skip_ws_comments(self):
        while True:
            self.skip_ws()
            if self.lit("<!--"):
                end = self.t.find("-->", self.i)
                if end < 0:
                    self.err("unterminated comment")
                self.adv(end + 3 - self.i)
            else:
                return

    def parse(self):
        self.skip_ws_comments()
        root = self.element()
        self.skip_ws_comments()
        if self.i < len(self.t):
            self.err("text after the document element")
        return root

    def element(self):
        if not self.lit("<"):
            self.err("expected '<'")
        start_line = self.line
        self.adv(1)
        m = NAME_RE.match(self.t, self.i)
        if not m:
            self.err("expected an element name after '<'")
        node = Node(m.group(0).lower(), start_line)
        self.adv(len(m.group(0)))
        # attributes
        while True:
            self.skip_ws()
            if self.lit("/>"):
                self.adv(2)
                return node
            if self.lit(">"):
                self.adv(1)
                break
            m = NAME_RE.match(self.t, self.i)
            if not m:
                self.err("expected an attribute name in <%s>" % node.name)
            aname = m.group(0).lower()
            aline = self.line
            self.adv(len(m.group(0)))
            if self.lit("="):
                self.adv(1)
                if not self.lit('"'):
                    self.err('attribute values are double-quoted '
                             "(WEAVE-SPEC 3.1)")
                self.adv(1)
                end = self.t.find('"', self.i)
                if end < 0:
                    self.err("unterminated attribute value")
                raw = self.t[self.i:end]
                self.adv(end + 1 - self.i)
                val = fold_text(expand_entities(raw, self.fname, aline))
            else:
                val = "1"            # bare boolean attribute
            if aname in node.attrs:
                self.err('%s: attribute "%s" written twice'
                         % (node.name, aname), aline)
            node.attrs[aname] = (val, aline)
        # content
        while True:
            if self.i >= len(self.t):
                self.err("<%s> is never closed" % node.name, start_line)
            if self.lit("<!--"):
                end = self.t.find("-->", self.i)
                if end < 0:
                    self.err("unterminated comment")
                self.adv(end + 3 - self.i)
            elif self.lit("</"):
                cl = self.line
                self.adv(2)
                m = NAME_RE.match(self.t, self.i)
                if not m or m.group(0).lower() != node.name:
                    self.err("</%s> closes <%s> - tags must nest "
                             "(WEAVE-SPEC 3.1)"
                             % (m.group(0) if m else "", node.name), cl)
                self.adv(len(m.group(0)))
                self.skip_ws()
                if not self.lit(">"):
                    self.err("expected '>'")
                self.adv(1)
                return node
            elif self.lit("<"):
                node.children.append(self.element())
            else:
                nxt = self.t.find("<", self.i)
                if nxt < 0:
                    nxt = len(self.t)
                run = self.t[self.i:nxt]
                if run.strip():
                    node.texts.append((run, self.line))
                self.adv(nxt - self.i)


# --- pack-error message helpers (the pinned sentences, WEAVE-SPEC 10.5) ------
def unknown_attr_error(fname, line, elem, attr):
    if attr.startswith("on"):
        # No hover, and no unknown event either (WEAVE-SPEC 3.4).
        if attr in ("onhover", "onmouseover", "onmouseout", "onmousemove"):
            raise PackError(fname, line, "%s: no hover exists; pointer "
                            "movement reaches a package only between press "
                            "and release (SPEC.md 13.7)" % attr)
        if attr in WK:
            raise PackError(fname, line, '%s: no event "%s" exists on it '
                            "(WEAVE-SPEC 3.3)" % (elem, attr))
        raise PackError(fname, line, "%s: no hover exists; pointer movement "
                        "reaches a package only between press and release "
                        "(SPEC.md 13.7)" % attr)
    if "color" in attr or attr in ("fg", "bg", "font", "size"):
        raise PackError(fname, line, "%s: there are no colors; grey rounds "
                        "to black on 1bpp and state never rides on color "
                        "(SPEC.md 39.4)" % attr)
    raise PackError(fname, line, '%s: no such attribute "%s"; style is '
                    "bold/invert/align only - two of three adapters are 1bpp"
                    % (elem, attr))


# --- the atom interner (WEAVE-SPEC 2.7, 2.14 rule 3) -------------------------
class Interner:
    """App atoms 64..250, first-appearance order, duplicates intern once.
    Well-known names are NEVER pooled; a string literal that happens to
    spell a well-known name still interns as an app atom - the runtime has
    no name table for ids 1..63 (WEAVE-SPEC 2.7)."""

    def __init__(self, fname):
        self.fname, self.strings, self.ids = fname, [], {}

    def intern(self, s, line):
        if not 1 <= len(s) <= 255:
            raise PackError(self.fname, line, "empty string: an atom is "
                            "1..255 bytes (WEAVE-SPEC 2.7)"
                            if not s else "string is %d bytes; the cap is "
                            "255 (WEAVE-SPEC 2.7)" % len(s))
        if s in self.ids:
            return self.ids[s]
        if len(self.strings) >= APP_ATOM_MAX:
            raise PackError(self.fname, line, "%d app atoms; the cap is 187 "
                            "- atom ids are one byte"
                            % (len(self.strings) + 1))
        self.strings.append(s)
        self.ids[s] = APP_ATOM0 + len(self.strings) - 1
        return self.ids[s]


# --- the document model ------------------------------------------------------
class Comp:
    def __init__(self, tag, node):
        self.tag, self.node, self.line = tag, node, node.line
        self.ctype = CTYPE[tag]
        self.comp_id = 0
        self.cid = None                 # the WML id, if any
        self.w = self.h = 0
        self.style = self.cflags = 0
        self.props = []                 # (atom, kind, value) - value may be
        self.events = {}                # a ('blob', bytes) placeholder
        self.content = ""               # event atom -> handler fn name
        self.items = []                 # list items (atom ids)
        self.grid = None                # (cols, rows)
        self.canvas = None              # dict(w, h, walls, tick)
        self.sprite = None              # dict(img, x, y, shown)
        self.sprites = []               # canvas: child Comp list


class Menu:
    def __init__(self, title_atom, line):
        self.title_atom, self.line = title_atom, line
        self.items = []                 # (label_atom, fnname or None, line)


class AppModel:
    def __init__(self):
        self.name = ""
        self.vm_kb = 16
        self.cards = []                 # (card_id, [Comp]) incl. sprites
        self.menus = []
        self.script_src = None
        self.script_line = 0
        self.comps = []                 # every Comp in comp_id order
        self.idmap = {}                 # WML id -> comp_id
        self.cardmap = {}               # card id -> 1-based index
        self.entry_card = 1


def _int_attr(fname, elem, attr, val, line, lo, hi, signed=False):
    try:
        v = int(val, 10)
    except ValueError:
        raise PackError(fname, line, '%s: %s="%s" is not a number'
                        % (elem, attr, val))
    if (v < 0 and not signed) or not lo <= v <= hi:
        raise PackError(fname, line, '%s: %s="%s" is outside %d..%d'
                        % (elem, attr, val, lo, hi))
    return v


def _bool_attr(fname, elem, attr, val, line):
    if val not in ("0", "1"):
        raise PackError(fname, line, '%s: %s="%s" is not a boolean - bare '
                        'or "1"/"0" (WEAVE-SPEC 3.1)' % (elem, attr, val))
    return int(val)


class Analyzer:
    """WML tree -> AppModel, validating WEAVE-SPEC 3 and interning atoms in
    the pinned traversal order (WEAVE-SPEC 2.14 rule 3a)."""

    def __init__(self, fname, interner):
        self.fname, self.atoms = fname, interner
        self.app = AppModel()
        self.next_comp = 1

    def err(self, line, msg):
        raise PackError(self.fname, line, msg)

    def run(self, root):
        if root.name != "app":
            self.err(root.line, "<%s>: the document element is <app> "
                     "(WEAVE-SPEC 3.2)" % root.name)
        self.check_attrs(root)
        app = self.app
        name, ln = root.attrs.get("name", (None, root.line))
        if name is None:
            self.err(root.line, 'app: attribute "name" is required')
        if not 1 <= len(name) <= 15:
            self.err(ln, 'app: name="%s" is %d chars; the header field is '
                     "15 (WEAVE-SPEC 2.2)" % (name, len(name)))
        app.name = name
        if "vm" in root.attrs:
            app.vm_kb = _int_attr(self.fname, "app", "vm",
                                  *root.attrs["vm"], 16, 32)
        if root.texts:
            self.err(root.texts[0][1], "app: text content is not a card")
        for child in root.children:
            if child.name not in CHILDREN["app"]:
                self.reject_elem(child, "app")
            if child.name == "card":
                self.card(child)
            elif child.name == "menu":
                self.menu(child)
            else:
                self.script(child)
        if not 1 <= len(app.cards) <= 8:
            self.err(root.line, "%d cards; an app has 1..8 (WEAVE-SPEC 3.2)"
                     % len(app.cards))
        if len(app.menus) > 5:
            self.err(root.line, "%d menus; MENU_APPMAX is 5 (SPEC.md 12.2)" % len(app.menus))
        ngrid = sum(1 for c in app.comps if c.tag == "grid")
        ncanv = sum(1 for c in app.comps if c.tag == "canvas")
        if ngrid > 1 or ncanv > 1:
            self.err(root.line, "%d grids, %d canvases; at most one of each "
                     "- each owns a dedicated claim and the cap is 8 per "
                     "owner (SPEC.md 50.2)" % (ngrid, ncanv))
        # radio groups need >= 2 members (WEAVE-SPEC 11.3)
        groups = {}
        for c in app.comps:
            if c.tag == "radio":
                groups.setdefault(c.radio_group, []).append(c)
        for g, members in groups.items():
            if len(members) < 2:
                self.err(members[0].line, 'radio: group "%s" has one member;'
                         " a group is 2 or more" % g)
            if sum(1 for m in members if m.checked) > 1:
                self.err(members[0].line, 'radio: group "%s" has two checked'
                         " members; one per group" % g)
        return app

    def reject_elem(self, node, parent):
        if node.name in ELEM_ATTRS or node.name in ("app",):
            self.err(node.line, "<%s>: not a child of <%s> (WEAVE-SPEC 3.2)"
                     % (node.name, parent))
        self.err(node.line, "<%s>: not a Weave element; the inventory is "
                 "closed (WEAVE-SPEC 3.2)" % node.name)

    def check_attrs(self, node):
        legal = list(ELEM_ATTRS.get(node.name, []))
        if node.name in FLOW:
            legal = COMMON_ATTRS + legal
        for attr, (val, line) in node.attrs.items():
            if attr not in legal:
                unknown_attr_error(self.fname, line, node.name, attr)

    def common(self, comp, node):
        f = self.fname
        if "id" in node.attrs:
            cid, ln = node.attrs["id"]
            if cid in self.app.idmap or cid in self.app.cardmap:
                self.err(ln, '%s: id "%s" is already taken' % (comp.tag, cid))
            comp.cid = cid
            self.app.idmap[cid] = comp.comp_id
        if comp.tag != "canvas":        # a canvas's w/h are pixels, parsed
            if "w" in node.attrs:       # by el_canvas (WEAVE-SPEC 3.3)
                comp.w = _int_attr(f, comp.tag, "w", *node.attrs["w"],
                                   0, 160)
            if "h" in node.attrs:
                comp.h = _int_attr(f, comp.tag, "h", *node.attrs["h"],
                                   0, 40)
        if "style" in node.attrs:
            val, ln = node.attrs["style"]
            for tok in val.split():
                if tok == "bold":
                    comp.style |= ST_BOLD
                elif tok == "invert":
                    comp.style |= ST_INVERT
                else:
                    self.err(ln, 'style: no such style "%s"; style is '
                             "bold/invert/align only - two of three "
                             "adapters are 1bpp" % tok)
        if "align" in node.attrs:
            val, ln = node.attrs["align"]
            if val not in ALIGNS:
                self.err(ln, 'align: no such alignment "%s"; left/center/'
                         "right (WEAVE-SPEC 3.3)" % val)
            comp.style |= ALIGNS[val] << 2
        for attr, bit in (("br", CF_BREAK), ("hidden", CF_HIDDEN),
                          ("disabled", CF_DISABLED)):
            if attr in node.attrs:
                if _bool_attr(f, comp.tag, attr, *node.attrs[attr]):
                    comp.cflags |= bit

    def event(self, comp, node, attr):
        if attr in node.attrs:
            comp.events[WK[attr]] = node.attrs[attr]

    def card(self, node):
        self.check_attrs(node)
        if "id" not in node.attrs:
            self.err(node.line, 'card: attribute "id" is required')
        cid, ln = node.attrs["id"]
        if cid in self.app.cardmap or cid in self.app.idmap:
            self.err(ln, 'card: id "%s" is already taken' % cid)
        idx = len(self.app.cards) + 1
        self.app.cardmap[cid] = idx
        comps = []
        self.app.cards.append((cid, comps))
        if node.texts:
            self.err(node.texts[0][1], "card: bare text is not a component")
        for child in node.children:
            if child.name not in CHILDREN["card"]:
                self.reject_elem(child, "card")
            comps.append(self.component(child))
            if child.name == "canvas":
                comps.extend(comps[-1].sprites)

    def component(self, node):
        self.check_attrs(node)
        comp = Comp(node.name, node)
        comp.comp_id = self.next_comp
        self.next_comp += 1
        if self.next_comp > 251:
            self.err(node.line, "251 components; comp_id is one byte, 1..250"
                     " (WEAVE-SPEC 2.5)")
        self.common(comp, node)
        self.app.comps.append(comp)
        fn = getattr(self, "el_" + node.name)
        fn(comp, node)
        if node.name not in TEXT_CONTENT and node.name != "canvas" \
                and node.texts:
            self.err(node.texts[0][1], "%s: takes no text content"
                     % node.name)
        if node.children and node.name not in ("canvas", "list"):
            self.err(node.children[0].line, "<%s>: <%s> takes no children"
                     % (node.children[0].name, node.name))
        # sort the prop records by atom id (WEAVE-SPEC 2.14 rule 5)
        comp.props.sort(key=lambda r: r[0])
        return comp

    # -- per-element handlers; interning order = attrs in table order, then
    #    text content (WEAVE-SPEC 2.14 rule 3a) --
    def text_prop(self, comp, node, atom):
        s = comp.node.content(self.fname)
        comp.content = s
        if s:
            comp.props.append((atom, PK_ATOM,
                               self.atoms.intern(s, node.line)))

    def el_label(self, comp, node):
        self.text_prop(comp, node, WK["text"])

    def el_text(self, comp, node):
        self.text_prop(comp, node, WK["text"])

    def el_rule(self, comp, node):
        pass

    def el_box(self, comp, node):
        if comp.w < 2 or comp.h < 1:
            self.err(node.line, "box: w and h are required, 2x1 or more")

    def el_spacer(self, comp, node):
        if comp.w < 1:
            self.err(node.line, "spacer: w is required")

    def el_meter(self, comp, node):
        mx = 100
        if "max" in node.attrs:
            mx = _int_attr(self.fname, "meter", "max", *node.attrs["max"],
                           1, 32000)
            comp.props.append((WK["max"], PK_INT, mx))
        if "value" in node.attrs:
            v = _int_attr(self.fname, "meter", "value",
                          *node.attrs["value"], 0, mx)
            comp.props.append((WK["value"], PK_INT, v))

    def el_button(self, comp, node):
        self.event(comp, node, "onclick")
        self.text_prop(comp, node, WK["label"])

    def _checkable(self, comp, node):
        comp.checked = 0
        if "checked" in node.attrs:
            comp.checked = _bool_attr(self.fname, comp.tag, "checked",
                                      *node.attrs["checked"])
            comp.props.append((WK["checked"], PK_INT, comp.checked))
        self.event(comp, node, "onchange")
        self.text_prop(comp, node, WK["label"])

    def el_check(self, comp, node):
        self._checkable(comp, node)

    def el_radio(self, comp, node):
        if "group" not in node.attrs:
            self.err(node.line, 'radio: attribute "group" is required')
        g, ln = node.attrs["group"]
        comp.radio_group = g
        comp.props.append((WK["group"], PK_ATOM, self.atoms.intern(g, ln)))
        self._checkable(comp, node)

    def el_input(self, comp, node):
        if "cols" in node.attrs:
            comp.props.append((WK["cols"], PK_INT,
                               _int_attr(self.fname, "input", "cols",
                                         *node.attrs["cols"], 2, 60)))
        if "text" in node.attrs:
            val, ln = node.attrs["text"]
            if val:
                comp.props.append((WK["text"], PK_ATOM,
                                   self.atoms.intern(val, ln)))
        self.event(comp, node, "onchange")
        self.event(comp, node, "onkey")

    def el_list(self, comp, node):
        if "rows" in node.attrs:
            comp.props.append((WK["rows"], PK_INT,
                               _int_attr(self.fname, "list", "rows",
                                         *node.attrs["rows"], 1, 40)))
        self.event(comp, node, "onselect")
        for child in node.children:
            if child.name != "item":
                self.reject_elem(child, "list")
            self.check_attrs(child)
            if "oncommand" in child.attrs:
                self.err(child.attrs["oncommand"][1],
                         "item: oncommand is a menu item's; a list fires "
                         "onselect (WEAVE-SPEC 3.3)")
            s = child.content(self.fname)
            if not s:
                self.err(child.line, "item: an empty list item")
            comp.items.append(self.atoms.intern(s, child.line))
        if len(comp.items) > 64:
            self.err(node.line, "%d list items; the blob count byte caps at "
                     "64 (WEAVE-SPEC 2.6.1)" % len(comp.items))
        if comp.items:
            comp.props.append((WK["ITEMS"], PK_BLOB,
                               ("blob", bytes([len(comp.items)]
                                              + comp.items))))

    def el_grid(self, comp, node):
        for a in ("cols", "rows"):
            if a not in node.attrs:
                self.err(node.line, 'grid: attribute "%s" is required' % a)
        cols = _int_attr(self.fname, "grid", "cols", *node.attrs["cols"],
                         1, 26)
        rows = _int_attr(self.fname, "grid", "rows", *node.attrs["rows"],
                         1, 256)
        if rows * cols > 6140:
            self.err(node.line, "grid is %dx%d = %d cells; the cap is 6140 "
                     "- the cell store plus its pool must fit a 26KB claim"
                     % (cols, rows, rows * cols))
        comp.grid = (cols, rows)
        comp.props.append((WK["cols"], PK_INT, cols))
        comp.props.append((WK["rows"], PK_INT, rows))
        for ev in ("onselect", "onedit", "oncalc"):
            self.event(comp, node, ev)

    def el_canvas(self, comp, node):
        for a in ("w", "h"):
            if a not in node.attrs:
                self.err(node.line, 'canvas: attribute "%s" is required' % a)
        w = _int_attr(self.fname, "canvas", "w", *node.attrs["w"], 64, 320)
        h = _int_attr(self.fname, "canvas", "h", *node.attrs["h"], 32, 160)
        if w % 8:
            self.err(node.attrs["w"][1], 'canvas: w="%d" is not a multiple '
                     "of 8 - bands are byte-aligned (WEAVE-SPEC 3.3)" % w)
        walls = "TBLR"
        if "walls" in node.attrs:
            walls, ln = node.attrs["walls"]
            if any(c not in "TBLR" for c in walls):
                self.err(ln, 'canvas: walls="%s" is not a subset of TBLR'
                         % walls)
            mask = sum(1 << "TBLR".index(c) for c in set(walls))
            comp.props.append((WK["walls"], PK_INT, mask))
        tick = 0
        if "tick" in node.attrs:
            tick = _int_attr(self.fname, "canvas", "tick",
                             *node.attrs["tick"], 0, 255)
            if tick:
                comp.props.append((WK["tick"], PK_INT, tick))
        comp.canvas = dict(w=w, h=h, walls=walls, tick=tick)
        # REC_COMP w/h carry the pixel size in cells/rows; a height not a
        # multiple of 8 rounds the buffer UP to the next band (recorded
        # interpretation - the record has no finer granularity).
        comp.w, comp.h = w // 8, (h + 7) // 8
        for ev in ("onkey", "oncollide", "onwall", "onscore", "ontick"):
            self.event(comp, node, ev)
        if WK["ontick"] in comp.events and tick < 1:
            self.err(node.line, 'canvas: ontick requires tick="1" or more '
                     "(WEAVE-SPEC 3.3)")
        for child in node.children:
            if child.name != "sprite":
                self.reject_elem(child, "canvas")
            comp.sprites.append(self.component(child))
        if len(comp.sprites) > 16:
            self.err(node.line, "%d sprites; a canvas composes 16 at most "
                     "(WEAVE-SPEC 6.10)" % len(comp.sprites))

    def el_sprite(self, comp, node):
        if "img" not in node.attrs:
            self.err(node.line, 'sprite: attribute "img" is required')
        x = y = 0
        shown = 1
        if "x" in node.attrs:
            x = _int_attr(self.fname, "sprite", "x", *node.attrs["x"],
                          -320, 320, signed=True)
        if "y" in node.attrs:
            y = _int_attr(self.fname, "sprite", "y", *node.attrs["y"],
                          -160, 160, signed=True)
        if "shown" in node.attrs:
            shown = _bool_attr(self.fname, "sprite", "shown",
                               *node.attrs["shown"])
        comp.sprite = dict(img=node.attrs["img"][0].upper(), x=x, y=y,
                           shown=shown, line=node.line)
        if x:
            comp.props.append((WK["x"], PK_INT, x))
        if y:
            comp.props.append((WK["y"], PK_INT, y))
        if not shown:
            comp.props.append((WK["shown"], PK_INT, 0))
        comp.w = comp.h = 0     # sprites are not flow components

    def menu(self, node):
        self.check_attrs(node)
        if "title" not in node.attrs:
            self.err(node.line, 'menu: attribute "title" is required')
        title, ln = node.attrs["title"]
        if not 1 <= len(title) <= 8:
            self.err(ln, 'menu: title="%s" is over 8 chars' % title)
        m = Menu(self.atoms.intern(title, ln), node.line)
        self.app.menus.append(m)
        for child in node.children:
            if child.name != "item":
                self.reject_elem(child, "menu")
            self.check_attrs(child)
            s = child.content(self.fname)
            if not s or len(s) > 24:
                self.err(child.line, "item: a menu item label is 1..24 "
                         "glyphs")
            fn = child.attrs.get("oncommand", (None, 0))[0]
            m.items.append((self.atoms.intern(s, child.line), fn,
                            child.line))
        if not 1 <= len(m.items) <= 8:
            self.err(node.line, "%d menu items; a menu holds 1..8"
                     % len(m.items))

    def script(self, node):
        self.check_attrs(node)
        if self.app.script_src:
            self.err(node.line, "script: one <script> per app")
        if node.texts or node.children:
            self.err(node.line, "script: inline script is not packed; name "
                     "a .WJS file - the runtime never parses text")
        if "src" not in node.attrs:
            self.err(node.line, 'script: attribute "src" is required')
        self.app.script_src = node.attrs["src"][0]
        self.app.script_line = node.line


# --- .WSP sprite art (WEAVE-SPEC 3.6) ----------------------------------------
class Sprite:
    def __init__(self, name, w, h, frames):
        self.name, self.w, self.h, self.frames = name, w, h, frames
        self.images = []     # per frame: bytes (h * w//8), 1 = ink
        self.masks = []      # per frame: bytes, AND mask = NOT coverage


def parse_wsp(text, fname):
    """-> [Sprite] in definition order (WEAVE-SPEC 2.14 rule 7)."""
    sprites, cur, rows, frames = [], None, [], []
    lineno = 0

    def close_frame(ln):
        if cur is None:
            return
        if len(rows) != cur.h:
            raise PackError(fname, ln, "sprite %s: %d rows; declared %d"
                            % (cur.name, len(rows), cur.h))
        img, msk = bytearray(), bytearray()
        for row in rows:
            for b0 in range(0, cur.w, 8):
                ib = mb = 0
                for k in range(8):
                    ib = (ib << 1) | (1 if row[b0 + k] == "#" else 0)
                    mb = (mb << 1) | (0 if row[b0 + k] == "#" else 1)
                img.append(ib)
                msk.append(mb)          # AND mask = NOT(coverage)
        cur.images.append(bytes(img))
        cur.masks.append(bytes(msk))
        rows.clear()

    def close_sprite(ln):
        if cur is None:
            return
        close_frame(ln)
        if len(cur.images) != cur.frames:
            raise PackError(fname, ln, "sprite %s: %d frames; declared %d"
                            % (cur.name, len(cur.images), cur.frames))

    for lineno, line in enumerate(text.split("\n"), 1):
        line = line.rstrip()
        if not line:
            continue
        if line.startswith("sprite "):
            close_sprite(lineno)
            parts = line.split()
            if len(parts) not in (4, 5):
                raise PackError(fname, lineno,
                                "sprite <name> <w_px> <h_px> [<frames>]")
            name = parts[1].upper()
            try:
                w, h = int(parts[2]), int(parts[3])
                nf = int(parts[4]) if len(parts) == 5 else 1
            except ValueError:
                raise PackError(fname, lineno, "sprite: numbers expected")
            if w % 8 or not 8 <= w <= 64 or not 1 <= h <= 64 \
                    or not 1 <= nf <= 8:
                raise PackError(fname, lineno, "sprite %s: w a multiple of "
                                "8 in 8..64, h 1..64, frames 1..8 "
                                "(WEAVE-SPEC 3.6)" % name)
            if any(s.name == name for s in sprites):
                raise PackError(fname, lineno, "sprite %s: defined twice"
                                % name)
            cur = Sprite(name, w, h, nf)
            sprites.append(cur)
        elif line == "-":
            close_frame(lineno)
        else:
            if cur is None:
                raise PackError(fname, lineno, "art before any sprite line")
            if len(line) != cur.w or any(c not in "#." for c in line):
                raise PackError(fname, lineno, "sprite %s: a row is exactly"
                                " %d of '#' and '.'" % (cur.name, cur.w))
            rows.append(line)
    close_sprite(lineno)
    if len(sprites) > 16:
        raise PackError(fname, lineno, "%d sprites; the section count byte "
                        "caps at 16 (WEAVE-SPEC 2.11)" % len(sprites))
    return sprites


# --- .WFX sheet file (WEAVE-SPEC 11.2) ---------------------------------------
CELLREF_RE = re.compile(r"^([A-Za-z])([0-9]{1,3})$")


def parse_cellref(tok, cols, rows, fname, line):
    m = CELLREF_RE.match(tok)
    if not m:
        raise PackError(fname, line, '"%s" is not a cell reference (A1..%s%d)'
                        % (tok, chr(64 + cols), rows))
    c = ord(m.group(1).upper()) - 65
    r = int(m.group(2)) - 1
    if not (0 <= c < cols and 0 <= r < rows):
        raise PackError(fname, line, "%s is outside the %dx%d grid"
                        % (tok.upper(), cols, rows))
    return r, c


def parse_number_16_16(tok, fname, line):
    """Decimal to 16.16, the pinned conversion both packers share: integer
    part shifted, fraction digits rounded half up (recorded decision)."""
    m = re.match(r"^(-?)([0-9]+)(?:\.([0-9]+))?$", tok)
    if not m:
        raise PackError(fname, line, '"%s" is not a number' % tok)
    sign, ip, fp = m.group(1), int(m.group(2)), m.group(3) or ""
    if ip > 32767:
        raise PackError(fname, line, "%s: |value| < 32768 (WEAVE-SPEC 5.1)"
                        % tok)
    if len(fp) > 4:
        # WEAVE-SPEC 5.1: the fifth decimal place is below 16.16's own
        # resolution of 1/65536, so it cannot change the value - all it could
        # do is make two parsers disagree about the rounding.
        raise PackError(fname, line,
                        "%s: at most 4 fraction digits; 16.16 resolves to "
                        "1/65536 (WEAVE-SPEC 5.1)" % tok)
    frac = 0
    if fp:
        d = 10 ** len(fp)
        frac = (int(fp) * 65536 + d // 2) // d
    v = (ip << 16) + frac
    return wrap32(-v if sign else v)


def parse_wfx(text, fname, cols, rows):
    """-> {(r,c): ('num', v1616) | ('label', s) | ('formula', src, line)},
    plus the row-major cell order. Blank lines and '#' comments allowed
    (recorded decision)."""
    cells = {}
    for lineno, line in enumerate(text.split("\n"), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = re.match(r"^(\S+)\s*=\s*(.+)$", line)
        if not m:
            raise PackError(fname, lineno,
                            '<cellref> = <formula|number|"label">')
        rc = parse_cellref(m.group(1), cols, rows, fname, lineno)
        if rc in cells:
            raise PackError(fname, lineno, "%s: set twice"
                            % m.group(1).upper())
        rhs = m.group(2).strip()
        if rhs.startswith("="):
            cells[rc] = ("formula", rhs[1:], lineno)
        elif rhs.startswith('"'):
            if not rhs.endswith('"') or len(rhs) < 3:
                raise PackError(fname, lineno, "unterminated label")
            cells[rc] = ("label", fold_text(rhs[1:-1]), lineno)
        else:
            cells[rc] = ("num", parse_number_16_16(rhs, fname, lineno),
                         lineno)
    return cells


# --- FX: 16.16 fixed point exactly as the 8086 core will ---------------------
def wrap32(v):
    """32-bit two's-complement wrap - overflow is defined, not an error
    (WEAVE-SPEC 5.2)."""
    v &= 0xFFFFFFFF
    return v - 0x100000000 if v & 0x80000000 else v


FX_ERR = object()      # the error value; displays #DIV0 and propagates


def fx_add(a, b):
    return wrap32(a + b)


def fx_sub(a, b):
    return wrap32(a - b)


def fx_mul(a, b):
    # exact 64-bit product, arithmetic shift right 16, wrapped - the 8086
    # core's imul/shift pipeline.
    return wrap32((a * b) >> 16)


def fx_div(a, b):
    if b == 0:
        return FX_ERR
    q = abs(a << 16) // abs(b)          # idiv truncates toward zero
    return wrap32(-q if (a < 0) != (b < 0) else q)


def fx_round(a):
    ip = (abs(a) + 0x8000) >> 16        # half away from zero
    return wrap32(-(ip << 16) if a < 0 else ip << 16)


def fmt_16_16(v):
    """Display form: integer when whole, else up to 2 decimals, truncated
    (display format is the model's own - recorded decision)."""
    if v is FX_ERR:
        return "#DIV0"
    neg = v < 0
    a = -v if neg else v
    ip, fp = a >> 16, a & 0xFFFF
    cents = (fp * 100) >> 16
    s = "%d" % ip if not cents else ("%d.%02d" % (ip, cents)).rstrip("0")
    return ("-" if neg else "") + s


# --- the FX compiler (WEAVE-SPEC 5.1, 5.3) -----------------------------------
FX_TOK_RE = re.compile(
    r"\s*(<>|<=|>=|[=<>+\-*/(),:]|[A-Za-z][A-Za-z0-9]*|[0-9]+(?:\.[0-9]+)?)")


def fx_tokenize(src, fname, line):
    toks, i = [], 0
    while i < len(src):
        m = FX_TOK_RE.match(src, i)
        if not m or not m.group(1):
            raise PackError(fname, line, 'formula: cannot read "%s"'
                            % src[i:i + 8])
        toks.append(m.group(1))
        i = m.end()
    return toks


class FxCompiler:
    """Recursive descent per WEAVE-SPEC 5.1's grammar, emitting the RPN of
    5.3 - operand order left, right, op; function arguments in order, op
    last - which is exactly the pinned shunting-yard output.

    THE REFUSAL SENTENCES ARE THE RESIDENT COMPILER'S (WEAVE-SPEC 6.9.2), and
    that is a contract rather than a coincidence: LOOM's FX pre-compiler IS
    apps/weave/wfxc.c, `#include`d rather than rewritten (WEAVE-SPEC 1.2's
    rule that what the two packages share they share as source). A shared
    compiler has one vocabulary by construction, so this one was moved onto
    it - the family now says the same thing about a bad formula whether it
    was typed into a cell or packed from a .WFX. WEAVE-SPEC 10.5 records the
    amendment; tests/weave/packerr/ is what holds the two to it.
    """

    CMP = {"=": "FEQ", "<>": "FNE", "<": "FLT", "<=": "FLE", ">": "FGT",
           ">=": "FGE"}

    def __init__(self, cols, rows, fname, line):
        self.cols, self.rows = cols, rows
        self.fname, self.line = fname, line
        self.out = bytearray()

    def err(self, msg):
        raise PackError(self.fname, self.line, "formula: " + msg)

    def emit(self, b):
        self.out.append(b)
        if len(self.out) > 256:         # W_FXCMAX, wfxc.c's own cap
            self.err("too long for one cell.")

    def compile(self, src):
        self.toks = fx_tokenize(src, self.fname, self.line)
        self.i = 0
        self.cmp()
        if self.i != len(self.toks):
            self.err("there is something after the formula.")
        self.check_depth()              # ...as the resident core does, before
        self.out.append(FXOP["FEND"])   #    the FEND
        return bytes(self.out)

    def peek(self):
        return self.toks[self.i] if self.i < len(self.toks) else None

    def take(self):
        if self.i >= len(self.toks):
            self.err("a number, a cell or a function is needed.")
        self.i += 1
        return self.toks[self.i - 1]

    def want(self, ch, msg):
        if self.peek() != ch:
            self.err(msg)
        self.i += 1

    def cmp(self):
        self.sum_()
        t = self.peek()
        if t in self.CMP:
            self.take()
            self.sum_()
            self.emit(FXOP[self.CMP[t]])

    def sum_(self):
        self.term()
        while self.peek() in ("+", "-"):
            op = self.take()
            self.term()
            self.emit(FXOP["FADD" if op == "+" else "FSUB"])

    def term(self):
        self.factor()
        while self.peek() in ("*", "/"):
            op = self.take()
            self.factor()
            self.emit(FXOP["FMUL" if op == "*" else "FDIV"])

    def factor(self):
        if self.peek() == "-":
            self.take()
            self.atom()
            self.emit(FXOP["FNEG"])
        else:
            self.atom()

    def number(self, tok):
        """5.1's number, with 6.9.2's sentences. It is NOT
        parse_number_16_16: that one is the .WFX line format's and keeps its
        own wording, because a cell's value is not a formula."""
        m = re.match(r"^([0-9]+)(?:\.([0-9]*))?$", tok)
        if not m:
            self.err("a number, a cell or a function is needed.")
        ip = int(m.group(1))
        fp = m.group(2)
        if ip > 32767:
            self.err("|value| < 32768.")
        frac = 0
        if fp is not None:
            if len(fp) == 0:
                self.err("a digit must follow the point.")
            if len(fp) > 4:
                self.err("at most 4 decimals - 16.16 stops there.")
            d = 10 ** len(fp)
            frac = (int(fp) * 65536 + d // 2) // d
        return (ip << 16) + frac

    def cellref(self, tok):
        """...and the same for a reference: 6.9.2's two sentences, not
        parse_cellref's."""
        m = CELLREF_RE.match(tok)
        if not m:
            return None
        r = int(m.group(2))
        if r > 999:
            self.err("a row is 1..256.")
        c = ord(m.group(1).upper()) - 65
        r -= 1
        if not (0 <= c < self.cols and 0 <= r < self.rows):
            self.err("that cell is outside the grid.")
        return r, c

    def atom(self, range_ok=False):
        t = self.take()
        if t == "(":
            self.cmp()
            self.want(")", "a ')' is missing.")
            return
        if re.match(r"^[0-9]", t):
            v = self.number(t)
            self.emit(FXOP["FNUM"])
            for b in struct.pack("<i", v):
                self.emit(b)
            return
        rc = self.cellref(t) if CELLREF_RE.match(t) else None
        if rc is not None:
            r, c = rc
            if self.peek() == ":":
                if not range_ok:
                    self.err("a range is legal only in an aggregate.")
                self.take()
                nxt = self.peek()
                rc2 = self.cellref(nxt) if nxt and CELLREF_RE.match(nxt) \
                    else None
                if rc2 is None:
                    self.err("a cell must follow the ':'.")
                self.take()
                r2, c2 = rc2
                self.emit(FXOP["FRANGE"])
                for b in (min(r, r2), min(c, c2), max(r, r2), max(c, c2)):
                    self.emit(b)
                return
            self.emit(FXOP["FCELL"])
            self.emit(r)
            self.emit(c)
            return
        if not re.match(r"^[A-Za-z]", t):
            self.err("a number, a cell or a function is needed.")
        name = t.upper()[:7]            # wfxc.c reads at most seven letters
        if name not in FX_FUNCS:
            self.err("SUM MIN MAX AVG COUNT IF ABS ROUND is the whole set.")
        op, sig = FX_FUNCS[name]
        self.want("(", "a '(' must follow the function.")
        if sig == "range":
            k = len(self.out)
            self.atom(range_ok=True)
            if len(self.out) != k + 5 or self.out[k] != FXOP["FRANGE"]:
                self.err("an aggregate takes exactly one range.")
            self.want(")", "a ')' is missing.")
        else:
            for k in range(sig):
                if k:
                    self.want(",", "the arguments need a ',' between them.")
                self.cmp()
            self.want(")", "a ')' is missing.")
        self.emit(FXOP[op])

    def check_depth(self):
        """FX eval slots are 16 deep (WEAVE-SPEC 5.3); a deeper formula is
        refused at pack, and an incomplete one leaves a depth that is not 1.
        The resident core checks both as it emits; this walks the finished
        stream and reaches the same two answers."""
        depth = peak = i = 0
        b = self.out
        while i < len(b):
            op = b[i]
            name, spec = FXOPS[op]
            i += 1 + {"": 0, "d": 4, "bb": 2, "bbbb": 4}[spec]
            if name in ("FNUM", "FCELL", "FRANGE"):
                depth += 1
            elif name in ("FADD", "FSUB", "FMUL", "FDIV", "FEQ", "FNE",
                          "FLT", "FLE", "FGT", "FGE"):
                depth -= 1
            elif name == "FIF":
                depth -= 2
            peak = max(peak, depth)
        if peak > 16:
            self.err("too deep - the stack is 16 slots.")
        if depth != 1:
            self.err("the formula is incomplete.")


def fx_eval(rpn, read_cell):
    """Execute an RPN stream. read_cell(r, c) -> 16.16 value, FX_ERR, or
    None for empty/label (reads as 0 under FCELL, skipped by aggregates).
    Returns a 16.16 int or FX_ERR."""
    st, i = [], 0

    def cells_of(rng):
        r1, c1, r2, c2 = rng
        for r in range(r1, r2 + 1):
            for c in range(c1, c2 + 1):
                yield read_cell(r, c)

    while True:
        op = rpn[i]
        name, spec = FXOPS[op]
        i += 1
        if name == "FEND":
            return st[-1]
        if name == "FNUM":
            st.append(struct.unpack_from("<i", rpn, i)[0])
            i += 4
        elif name == "FCELL":
            v = read_cell(rpn[i], rpn[i + 1])
            st.append(0 if v is None else v)
            i += 2
        elif name == "FRANGE":
            st.append(("range", tuple(rpn[i:i + 4])))
            i += 4
        elif name in ("FADD", "FSUB", "FMUL", "FDIV", "FEQ", "FNE", "FLT",
                      "FLE", "FGT", "FGE"):
            b, a = st.pop(), st.pop()
            if a is FX_ERR or b is FX_ERR:
                st.append(FX_ERR)
            elif name == "FADD":
                st.append(fx_add(a, b))
            elif name == "FSUB":
                st.append(fx_sub(a, b))
            elif name == "FMUL":
                st.append(fx_mul(a, b))
            elif name == "FDIV":
                st.append(fx_div(a, b))
            else:
                r = {"FEQ": a == b, "FNE": a != b, "FLT": a < b,
                     "FLE": a <= b, "FGT": a > b, "FGE": a >= b}[name]
                st.append(0x10000 if r else 0)
        elif name == "FNEG":
            a = st.pop()
            st.append(FX_ERR if a is FX_ERR else wrap32(-a))
        elif name == "FABS":
            a = st.pop()
            st.append(FX_ERR if a is FX_ERR else wrap32(abs(a)))
        elif name == "FROUND":
            a = st.pop()
            st.append(FX_ERR if a is FX_ERR else fx_round(a))
        elif name == "FIF":
            b, a, c = st.pop(), st.pop(), st.pop()
            # both arms already evaluated - eager, which is why FIF cannot
            # guard a #DIV0 (WEAVE-SPEC 5.3)
            if c is FX_ERR or a is FX_ERR or b is FX_ERR:
                st.append(FX_ERR)
            else:
                st.append(a if c != 0 else b)
        elif name in ("FSUM", "FMIN", "FMAX", "FAVG", "FCOUNT"):
            rng = st.pop()[1]
            vals, err = [], False
            for v in cells_of(rng):
                if v is FX_ERR:
                    err = True
                elif v is not None:
                    vals.append(v)
            if err:
                st.append(FX_ERR)
            elif name == "FCOUNT":
                st.append(len(vals) << 16)
            elif name == "FSUM":
                s = 0
                for v in vals:
                    s = fx_add(s, v)
                st.append(s)
            elif name == "FMIN":
                st.append(min(vals) if vals else 0)
            elif name == "FMAX":
                st.append(max(vals) if vals else 0)
            else:                       # FAVG: 0 over an all-empty range
                if not vals:
                    st.append(0)
                else:
                    s = 0
                    for v in vals:
                        s = fx_add(s, v)
                    st.append(fx_div(s, len(vals) << 16))


# --- WJS: tokens (WEAVE-SPEC 4.2) --------------------------------------------
JS_KEYWORDS = {"var", "function", "if", "else", "while", "for", "break",
               "continue", "return", "true", "false", "null"}
JS_PUNCT = ["&&", "||", "==", "!=", "<=", ">=", "++", "--", "{", "}", "(",
            ")", "[", "]", ";", ",", "+", "-", "*", "/", "%", "<", ">",
            "=", "!", "."]


class Tok:
    __slots__ = ("kind", "val", "line", "atom")

    def __init__(self, kind, val, line, atom=None):
        self.kind, self.val, self.line, self.atom = kind, val, line, atom

    def __repr__(self):
        return "%s(%r)" % (self.kind, self.val)


def js_tokenize(text, fname, interner):
    """-> [Tok]. String literals intern here, in token order - that IS
    WEAVE-SPEC 2.14 rule 3b."""
    toks, i, line, n = [], 0, 1, len(text)
    while i < n:
        ch = text[i]
        if ch == "\n":
            line += 1
            i += 1
        elif ch in " \t\r":
            i += 1
        elif text.startswith("//", i):
            i = text.find("\n", i)
            i = n if i < 0 else i
        elif text.startswith("/*", i):
            end = text.find("*/", i)
            if end < 0:
                raise PackError(fname, line, "unterminated /* comment")
            line += text.count("\n", i, end)
            i = end + 2
        elif ch == '"':
            start = line
            i += 1
            out = []
            while True:
                if i >= n or text[i] == "\n":
                    raise PackError(fname, start, "unterminated string")
                c = text[i]
                if c == '"':
                    i += 1
                    break
                if c == "\\":
                    e = text[i + 1:i + 2]
                    if e == "n":
                        out.append("\n")
                    elif e in ('"', "\\"):
                        out.append(e)
                    else:
                        raise PackError(fname, start, '\\%s: the escapes '
                                        'are \\" \\\\ \\n (WEAVE-SPEC 4.2)'
                                        % e)
                    i += 2
                else:
                    out.append(c)
                    i += 1
            s = fold_text("".join(out))
            toks.append(Tok("str", s, start, interner.intern(s, start)))
        elif ch.isdigit():
            m = re.match(r"[0-9]+", text[i:])
            v = int(m.group(0))
            if v > 32767:
                raise PackError(fname, line, "%d: numbers are 0..32767; "
                                "16-bit int is THE number type "
                                "(WEAVE-SPEC 4.1)" % v)
            toks.append(Tok("num", v, line))
            i += m.end()
        elif ch.isalpha() or ch == "_":
            m = re.match(r"[A-Za-z_][A-Za-z0-9_]*", text[i:])
            name = m.group(0)
            if len(name) > 31:
                raise PackError(fname, line, "%s: identifiers are 31 chars "
                                "at most" % name)
            kind = "kw" if name in JS_KEYWORDS else "id"
            toks.append(Tok(kind, name, line))
            i += m.end()
        else:
            for p in JS_PUNCT:
                if text.startswith(p, i):
                    toks.append(Tok("p", p, line))
                    i += len(p)
                    break
            else:
                raise PackError(fname, line, "cannot read %r" % ch)
    toks.append(Tok("eof", "", line))
    return toks


class JsFunc:
    def __init__(self, name, params, body, line, index):
        self.name, self.params, self.body = name, params, body
        self.line, self.index = line, index
        self.code = b""
        self.nlocals = len(params)
        self.nops = 0
        self.has_call = False
        self.has_backjump = False


class JsProgram:
    def __init__(self):
        self.globals = []       # (name, init or None, line)
        self.gindex = {}
        self.funcs = []
        self.fnindex = {}


def js_collect(toks, fname):
    """Top level is declarations only (WEAVE-SPEC 4.1): collect globals and
    function signatures + body token spans."""
    prog = JsProgram()
    i = 0

    def err(t, msg):
        raise PackError(fname, t.line, msg)

    def declare_name(t, name):
        if name in BUILTIN:
            err(t, "%s: shadows a builtin (WEAVE-SPEC 4.6.5)" % name)
        if name in prog.gindex or name in prog.fnindex:
            err(t, "%s: declared twice" % name)

    while toks[i].kind != "eof":
        t = toks[i]
        if t.kind == "kw" and t.val == "var":
            nt = toks[i + 1]
            if nt.kind != "id":
                err(nt, "var: a name is expected")
            declare_name(nt, nt.val)
            i += 2
            init = None
            if toks[i].val == "=":
                i += 1
                init, i = js_initexpr(toks, i, fname)
            if toks[i].val != ";":
                err(toks[i], "var: ';' expected")
            i += 1
            if len(prog.globals) >= 128:
                err(t, "129 globals; the table is 128 (WEAVE-SPEC 4.2)")
            prog.gindex[nt.val] = len(prog.globals)
            prog.globals.append((nt.val, init, nt.line))
        elif t.kind == "kw" and t.val == "function":
            nt = toks[i + 1]
            if nt.kind != "id":
                err(nt, "function: a name is expected")
            declare_name(nt, nt.val)
            i += 2
            if toks[i].val != "(":
                err(toks[i], "function %s: '(' expected" % nt.val)
            i += 1
            params = []
            while toks[i].val != ")":
                if params:
                    if toks[i].val != ",":
                        err(toks[i], "',' expected")
                    i += 1
                if toks[i].kind != "id":
                    err(toks[i], "a parameter name is expected")
                if toks[i].val in params:
                    err(toks[i], "%s: parameter written twice"
                        % toks[i].val)
                params.append(toks[i].val)
                i += 1
            if len(params) > 8:
                err(nt, "%s: %d parameters; the cap is 8 (WEAVE-SPEC 4.2)"
                    % (nt.val, len(params)))
            i += 1
            if toks[i].val != "{":
                err(toks[i], "function %s: '{' expected" % nt.val)
            depth, start = 0, i
            while True:
                if toks[i].kind == "eof":
                    err(toks[start], "function %s is never closed" % nt.val)
                if toks[i].val == "{":
                    depth += 1
                elif toks[i].val == "}":
                    depth -= 1
                    if depth == 0:
                        break
                i += 1
            i += 1
            if len(prog.funcs) >= 128:
                err(t, "129 functions; the table is 128 (WEAVE-SPEC 4.2)")
            fn = JsFunc(nt.val, params, (start, i - 1), nt.line,
                        len(prog.funcs))
            prog.fnindex[nt.val] = fn.index
            prog.funcs.append(fn)
        else:
            err(t, "top level is declarations only - handlers are the only "
                "entry points (WEAVE-SPEC 4.1)")
    return prog


def js_initexpr(toks, i, fname):
    """initexpr = number|string|true|false|null|array(n). -> (init, i)."""
    t = toks[i]
    if t.kind == "num":
        return ("int", t.val), i + 1
    if t.kind == "str":
        return ("str", t.atom), i + 1
    if t.val == "-" and toks[i + 1].kind == "num":
        return ("int", -toks[i + 1].val), i + 2
    if t.val in ("true", "false"):
        return ("bool", 1 if t.val == "true" else 0), i + 1
    if t.val == "null":
        return ("null",), i + 1
    if t.val == "array":
        if toks[i + 1].val != "(" or toks[i + 2].kind != "num" \
                or toks[i + 3].val != ")":
            raise PackError(fname, t.line, "array(<size>) expected")
        n = toks[i + 2].val
        if not 1 <= n <= 2048:
            raise PackError(fname, t.line, "array(%d): size is 1..2048 "
                            "(WEAVE-SPEC 4.2)" % n)
        return ("array", n), i + 4
    raise PackError(fname, t.line, "an initializer is a constant: number, "
                    "string, true/false, null, array(n) (WEAVE-SPEC 4.2)")


# --- WJS: code generation (WEAVE-SPEC 4.6's pinned templates) ----------------
METHOD_ARITY = {WK["cell"]: 2, WK["setCell"]: 3, WK["recalc"]: 0,
                WK["select"]: 2, WK["stop"]: 0, WK["go"]: 1, WK["set"]: 2,
                WK["get"]: 1, WK["start"]: 1, WK["clear"]: 0}
PROP_ATOMS = {n: a for n, a in WK.items() if 1 <= a <= 24}


class FnCompiler:
    """One function body -> bytecode, per the normative templates. No
    optimisation of any kind (WEAVE-SPEC 2.14 rule 6)."""

    def __init__(self, prog, fn, toks, fname, ctx):
        self.prog, self.fn, self.toks = prog, fn, toks
        self.fname, self.ctx = fname, ctx
        self.b = bytearray()
        self.i = fn.body[0]
        self.loops = []                 # (break patches, continue sink)
        # pre-scan for locals: function scope, no block scoping
        self.localslot = {p: k for k, p in enumerate(fn.params)}
        self.declared = set(fn.params)
        j = fn.body[0]
        while j < fn.body[1]:
            t = toks[j]
            if t.kind == "kw" and t.val == "var":
                nt = toks[j + 1]
                if nt.kind != "id":
                    self.err(nt, "var: a name is expected")
                if nt.val in self.localslot:
                    self.err(nt, "%s: declared twice" % nt.val)
                if nt.val in BUILTIN:
                    self.err(nt, "%s: shadows a builtin (WEAVE-SPEC 4.6.5)"
                             % nt.val)
                self.localslot[nt.val] = len(self.localslot)
                j += 1
            j += 1
        if len(self.localslot) > 16:
            self.err(toks[fn.body[0]], "%s: %d locals; the cap is 16, "
                     "parameters included (WEAVE-SPEC 4.2)"
                     % (fn.name, len(self.localslot)))
        fn.nlocals = len(self.localslot)

    def err(self, tok, msg):
        raise PackError(self.fname, tok.line, msg)

    # -- emission --
    def op(self, name, *args):
        self.b.append(OP[name])
        spec = OPS[OP[name]][1]
        if spec == "b":
            self.b.append(args[0] & 0xFF)
        elif spec == "w":
            self.b += struct.pack("<h", args[0])
        elif spec == "bb":
            self.b += bytes([args[0] & 0xFF, args[1] & 0xFF])
        self.fn.nops += 1

    def jfwd(self, name):
        self.b.append(OP[name])
        self.b += b"\0\0"
        self.fn.nops += 1
        return len(self.b) - 2

    def patch(self, pos, target=None):
        t = len(self.b) if target is None else target
        rel = t - (pos + 2)
        if rel < 0:
            self.fn.has_backjump = True
        self.b[pos:pos + 2] = struct.pack("<h", rel)

    def jback(self, name, target):
        self.b.append(OP[name])
        pos = len(self.b)
        self.b += b"\0\0"
        self.fn.nops += 1
        self.patch(pos, target)

    # -- token plumbing --
    def peek(self, k=0):
        return self.toks[self.i + k]

    def take(self, want=None):
        t = self.toks[self.i]
        if want is not None and (t.val != want
                                 or t.kind not in ("p", "kw")):
            self.err(t, "'%s' expected, found '%s'" % (want, t.val))
        self.i += 1
        return t

    def pv(self, k=0):
        """The token's value ONLY when it is punctuation or a keyword -
        a string literal spelling an operator is data, not syntax."""
        t = self.toks[self.i + k]
        return t.val if t.kind in ("p", "kw") else None

    # -- name resolution: local, global, component id, function name
    #    (WEAVE-SPEC 4.2's pinned order); 'app' is comp 0, a card id is its
    #    index as an int (recorded interpretations) --
    def resolve(self, tok):
        n = tok.val
        if n in self.localslot:
            if n not in self.declared:
                self.err(tok, "%s: used before its declaration" % n)
            return ("local", self.localslot[n])
        if n in self.prog.gindex:
            return ("global", self.prog.gindex[n])
        if n == "app":
            return ("comp", 0)
        if n in self.ctx["idmap"]:
            return ("comp", self.ctx["idmap"][n])
        if n in self.ctx["cardmap"]:
            return ("card", self.ctx["cardmap"][n])
        if n in self.prog.fnindex:
            return ("fn", self.prog.fnindex[n])
        if n in BUILTIN:
            return ("builtin", BUILTIN[n])
        self.err(tok, "%s: not a local, global, component id or function "
                 "(WEAVE-SPEC 4.2)" % n)

    def push_value(self, tok):
        kind, v = self.resolve(tok)
        if kind == "local":
            self.op("LDL", v)
        elif kind == "global":
            self.op("LDG", v)
        elif kind == "comp":
            self.op("PUSHC", v)
        elif kind == "card":
            self.op("PUSHI", v)
        elif kind == "fn":
            self.err(tok, "%s: a function is not a value; it is named only "
                     "as a callback argument (WEAVE-SPEC 4.6.6)" % tok.val)
        else:
            self.err(tok, "%s: a builtin is called, not read" % tok.val)
        return kind, v

    def prop_atom(self, tok, base, writing=False, method=False):
        """The atom for c.<name>, checked statically where the base ctype is
        known (WEAVE-SPEC 4.4)."""
        name = tok.val
        atom = METHOD_ARITY_NAMES.get(name) if method \
            else PROP_ATOMS.get(name)
        if atom is None:
            self.err(tok, 'no such %s "%s" (WEAVE-SPEC 2.7.1)'
                     % ("method" if method else "property", name))
        if base is not None and base[0] == "comp":
            tag = self.ctx["ctype_of"].get(base[1], "app")
            surf = SURFACE[tag]
            ok = atom in (surf["methods"] if method
                          else (surf["set"] if writing else surf["get"]))
            if not ok:
                self.err(tok, '%s: a %s has no %s "%s" (WEAVE-SPEC 6)'
                         % (name, tag,
                            "method" if method else
                            ("writable property" if writing else "property"),
                            name))
        return atom

    # -- statements --
    def compile(self):
        self.take("{")
        while self.pv() != "}":
            self.statement()
        self.take("}")
        # fall off the end: PUSHN + RET, emitted always - no reachability
        # analysis exists (WEAVE-SPEC 4.5, recorded decision)
        self.op("PUSHN")
        self.op("RET")
        self.fn.code = bytes(self.b)
        return self.fn

    def statement(self):
        t = self.peek()
        if t.kind == "p" and t.val == "{":
            self.take()
            while self.pv() != "}":
                self.statement()
            self.take("}")
        elif t.kind == "kw" and t.val == "var":
            self.local_var()
        elif t.kind == "kw" and t.val == "if":
            self.if_stmt()
        elif t.kind == "kw" and t.val == "while":
            self.while_stmt()
        elif t.kind == "kw" and t.val == "for":
            self.for_stmt()
        elif t.kind == "kw" and t.val in ("break", "continue"):
            self.take()
            self.take(";")
            if not self.loops:
                self.err(t, "%s outside a loop" % t.val)
            brk, cont = self.loops[-1]
            if t.val == "break":
                brk.append(self.jfwd("JMP"))
            elif cont[0] == "at":
                self.jback("JMP", cont[1])
            else:
                cont[1].append(self.jfwd("JMP"))
        elif t.kind == "kw" and t.val == "return":
            self.take()
            if self.pv() == ";":
                self.op("PUSHN")
            else:
                self.expr()
            self.take(";")
            self.op("RET")
        elif t.kind == "id" and self.pv(1) in ("++", "--"):
            self.incdec(self.take(), self.take().val)
            self.take(";")
        elif self.try_assign():
            self.take(";")
        else:
            self.expr()
            self.take(";")
            self.op("POP")

    def local_var(self):
        self.take("var")
        nt = self.take()
        self.declared.add(nt.val)
        slot = self.localslot[nt.val]
        if self.pv() == "=":
            self.take()
            init, self.i = js_initexpr(self.toks, self.i, self.fname)
            self.push_init(init)
            self.op("STL", slot)
        self.take(";")

    def push_init(self, init):
        if init[0] == "int":
            self.op("PUSHI", init[1])
        elif init[0] == "str":
            self.op("PUSHA", init[1])
        elif init[0] == "bool":
            self.op("PUSHB", init[1])
        elif init[0] == "null":
            self.op("PUSHN")
        else:                           # array(n)
            self.op("PUSHI", init[1])
            self.op("BUILT", BUILTIN["array"], 1)
            self.fn.has_call = True

    def incdec(self, tok, opname):
        kind, v = self.resolve(tok)
        if kind == "global":
            self.op("INCG" if opname == "++" else "DECG", v)
        elif kind == "local":
            self.op("LDL", v)
            self.op("PUSHI", 1)
            self.op("ADD" if opname == "++" else "SUB")
            self.op("STL", v)
        else:
            self.err(tok, "%s: ++/-- takes a variable" % tok.val)

    def if_stmt(self):
        self.take("if")
        self.take("(")
        self.expr()
        self.take(")")
        jz = self.jfwd("JZ")
        self.statement()
        if self.pv() == "else":
            self.take()
            jend = self.jfwd("JMP")
            self.patch(jz)
            self.statement()
            self.patch(jend)
        else:
            self.patch(jz)

    def while_stmt(self):
        self.take("while")
        top = len(self.b)
        self.take("(")
        self.expr()
        self.take(")")
        jz = self.jfwd("JZ")
        self.loops.append(([], ("at", top)))
        self.statement()
        brk, _ = self.loops.pop()
        self.jback("JMP", top)
        self.patch(jz)
        for p in brk:
            self.patch(p)

    def for_stmt(self):
        self.take("for")
        self.take("(")
        if self.pv() != ";":
            if self.pv() == "var":
                self.local_var_no_semi()
            elif not self.try_assign():
                self.err(self.peek(), "for: an assignment is expected")
        self.take(";")
        top = len(self.b)
        if self.pv() == ";":
            self.op("PUSHB", 1)         # omitted condition (WEAVE-SPEC 4.6.3)
        else:
            self.expr()
        self.take(";")
        jz = self.jfwd("JZ")
        # step tokens are compiled AFTER the body (the template's Lstep)
        step_start = self.i
        depth = 0
        while not (depth == 0 and self.pv() == ")"):
            if self.pv() == "(":
                depth += 1
            elif self.pv() == ")":
                depth -= 1
            self.i += 1
        step_end = self.i
        self.take(")")
        conts = ("patches", [])
        self.loops.append(([], conts))
        self.statement()
        brk, _ = self.loops.pop()
        for p in conts[1]:
            self.patch(p)               # continue -> Lstep
        save = self.i
        self.i = step_start
        if self.i < step_end:
            t = self.peek()
            if t.kind == "id" and self.pv(1) in ("++", "--"):
                self.incdec(self.take(), self.take().val)
            elif not self.try_assign():
                self.err(t, "for: the step is an assignment or ++/--")
            if self.i != step_end:
                self.err(self.toks[self.i], "for: cannot read the step")
        self.i = save
        self.jback("JMP", top)
        self.patch(jz)
        for p in brk:
            self.patch(p)

    def local_var_no_semi(self):
        self.take("var")
        nt = self.take()
        self.declared.add(nt.val)
        slot = self.localslot[nt.val]
        if self.pv() == "=":
            self.take()
            init, self.i = js_initexpr(self.toks, self.i, self.fname)
            self.push_init(init)
            self.op("STL", slot)

    def try_assign(self):
        """assign = lvalue '=' expr - lvalue is one level deep. Returns
        False (position untouched) when the statement is not one."""
        t = self.peek()
        if t.kind != "id":
            return False
        n1, n2 = self.pv(1), self.peek(2)
        if n1 == "=" and self.pv(2) != "=":
            self.take()
            self.take("=")
            kind, v = self.resolve(t)
            if kind == "fn":
                self.err(t, "%s: assignment to a function name" % t.val)
            if kind not in ("local", "global"):
                self.err(t, "%s: not assignable" % t.val)
            self.expr()
            self.op("STG" if kind == "global" else "STL", v)
            return True
        if n1 == "." and n2.kind == "id" and self.pv(3) == "=" \
                and self.pv(4) != "=":
            self.take()
            base = self.resolve(t)
            self.take(".")
            ptok = self.take()
            self.take("=")
            atom = self.prop_atom(ptok, base, writing=True)
            # [c] [e] SETP (WEAVE-SPEC 4.6.1)
            self.push_value(t)
            self.expr()
            self.op("SETP", atom)
            return True
        if n1 == "[":
            # find the matching ']' and look for '='
            j, depth = self.i + 1, 0
            while True:
                tj = self.toks[j]
                tv = tj.val if tj.kind == "p" else None
                if tv == "[":
                    depth += 1
                elif tv == "]":
                    depth -= 1
                    if depth == 0:
                        break
                elif tj.kind == "eof":
                    self.err(self.peek(1), "']' expected")
                j += 1
            n1t = self.toks[j + 1]
            n2t = self.toks[j + 2]
            if n1t.kind == "p" and n1t.val == "=" \
                    and not (n2t.kind == "p" and n2t.val == "="):
                self.take()
                self.push_value(t)      # [a] [i] [e] ASET
                self.take("[")
                self.expr()
                self.take("]")
                self.take("=")
                self.expr()
                self.op("ASET")
                return True
        return False

    # -- expressions (WEAVE-SPEC 4.6.4/4.6.5) --
    def expr(self):
        self.orexpr()

    def orexpr(self):
        self.andexpr()
        while self.pv() == "||":
            self.take()                 # a || b: [a] DUP JNZ Lend POP [b]
            self.op("DUP")
            j = self.jfwd("JNZ")
            self.op("POP")
            self.andexpr()
            self.patch(j)

    def andexpr(self):
        self.eqexpr()
        while self.pv() == "&&":
            self.take()                 # a && b: [a] DUP JZ Lend POP [b]
            self.op("DUP")
            j = self.jfwd("JZ")
            self.op("POP")
            self.eqexpr()
            self.patch(j)

    def eqexpr(self):
        self.relexpr()
        while self.pv() in ("==", "!="):
            op = self.take().val
            self.relexpr()
            self.op("EQ" if op == "==" else "NE")

    def relexpr(self):
        self.addexpr()
        while self.pv() in ("<", "<=", ">", ">="):
            op = self.take().val
            self.addexpr()
            self.op({"<": "LT", "<=": "LE", ">": "GT", ">=": "GE"}[op])

    def addexpr(self):
        self.mulexpr()
        while self.pv() in ("+", "-"):
            op = self.take().val
            self.mulexpr()
            self.op("ADD" if op == "+" else "SUB")

    def mulexpr(self):
        self.unary()
        while self.pv() in ("*", "/", "%"):
            op = self.take().val
            self.unary()
            self.op({"*": "MUL", "/": "DIV", "%": "MOD"}[op])

    def unary(self):
        if self.pv() == "-":
            self.take()
            self.unary()
            self.op("NEG")
        elif self.pv() == "!":
            self.take()
            self.unary()
            self.op("NOT")
        else:
            self.postfix()

    def postfix(self):
        t = self.peek()
        if t.kind == "id" and self.pv(1) == "(":
            self.call(self.take())
        elif t.kind == "id" and self.pv(1) == ".":
            self.take()
            base = self.resolve(t)
            self.push_value(t)
            self.suffixes(base)
            return
        else:
            self.primary()
        self.suffixes(None)

    def suffixes(self, base):
        while True:
            t = self.peek()
            tv = t.val if t.kind == "p" else None
            if tv == "[":
                self.take()
                self.expr()
                self.take("]")
                self.op("AGET")
                base = None
            elif tv == ".":
                self.take()
                ptok = self.take()
                if ptok.kind != "id":
                    self.err(ptok, "a property name is expected after '.'")
                if self.pv() == "(":
                    atom = self.prop_atom(ptok, base, method=True)
                    self.take("(")
                    argc = 0
                    while self.pv() != ")":
                        if argc:
                            self.take(",")
                        self.expr()
                        argc += 1
                    self.take(")")
                    if argc != METHOD_ARITY[atom]:
                        self.err(ptok, "%s: takes %d arguments; %d written"
                                 % (ptok.val, METHOD_ARITY[atom], argc))
                    self.op("CALLM", atom, argc)
                else:
                    atom = self.prop_atom(ptok, base)
                    self.op("GETP", atom)
                base = None
            elif tv == "(":
                self.err(t, "only a function or builtin name is called "
                         "(WEAVE-SPEC 4.6.5)")
            else:
                return

    def primary(self):
        t = self.take()
        if t.kind == "num":
            self.op("PUSHI", t.val)
        elif t.kind == "str":
            self.op("PUSHA", t.atom)
        elif t.kind == "kw" and t.val == "true":
            self.op("PUSHB", 1)
        elif t.kind == "kw" and t.val == "false":
            self.op("PUSHB", 0)
        elif t.kind == "kw" and t.val == "null":
            self.op("PUSHN")
        elif t.kind == "p" and t.val == "(":
            self.expr()
            self.take(")")
        elif t.kind == "id":
            self.push_value(t)
        else:
            self.err(t, "cannot read '%s' here" % t.val)

    def call(self, t):
        name = t.val
        self.take("(")
        if name in BUILTIN:
            b = BUILTIN[name]
            _, lo, hi = BUILTINS[b]
            if name == "array":
                self.err(t, "array: legal only as a var initializer "
                         "(WEAVE-SPEC 8.1)")
            argc = 0
            while self.pv() != ")":
                if argc:
                    self.take(",")
                if name in ("alert", "timer") and argc == 1:
                    cb = self.take()
                    if cb.kind != "id" or cb.val not in self.prog.fnindex:
                        self.err(cb, "%s: the callback names a top-level "
                                 "function (WEAVE-SPEC 4.6.6)" % name)
                    self.op("PUSHI", self.prog.fnindex[cb.val])
                else:
                    self.expr()
                argc += 1
            self.take(")")
            if not lo <= argc <= hi:
                self.err(t, "%s: takes %s arguments; %d written"
                         % (name, lo if lo == hi else "%d..%d" % (lo, hi),
                            argc))
            self.op("BUILT", b, argc)
            self.fn.has_call = True
            return
        kind, v = self.resolve(t)
        if kind != "fn":
            self.err(t, "%s: not a function" % name)
        argc = 0
        while self.pv() != ")":
            if argc:
                self.take(",")
            self.expr()
            argc += 1
        self.take(")")
        want = len(self.prog.funcs[v].params)
        if argc != want:
            self.err(t, "%s: takes %d arguments; %d written"
                     % (name, want, argc))
        self.op("CALL", v)
        self.fn.has_call = True


METHOD_ARITY_NAMES = {n: a for n, a in WK.items() if a in METHOD_ARITY}


def compile_wjs(prog, toks, fname, ctx):
    """Compile every function; synthesize the module-init function when any
    global initializer differs from the zeroed default (int 0). The init
    is appended as the LAST function-table entry and named in the app
    block under atom 40 ('start', PK_FUNC); the runtime runs it once at
    VM start (WEAVE-SPEC 2.6.2). Returns (funcs, start_fn_index or None)."""
    for fn in prog.funcs:
        FnCompiler(prog, fn, toks, fname, ctx).compile()
    inits = [(g, init) for g, (name, init, ln) in
             enumerate(prog.globals)
             if init is not None and init != ("int", 0)]
    start = None
    if inits:
        if len(prog.funcs) >= 128:
            raise PackError(fname, 1, "129 functions; the table is 128 "
                            "(WEAVE-SPEC 4.2)")
        fn = JsFunc("(init)", [], (0, 0), 0, len(prog.funcs))
        b = bytearray()
        for g, init in inits:
            if init[0] == "int":
                b.append(OP["PUSHI"])
                b += struct.pack("<h", init[1])
            elif init[0] == "str":
                b += bytes([OP["PUSHA"], init[1]])
            elif init[0] == "bool":
                b += bytes([OP["PUSHB"], init[1]])
            elif init[0] == "null":
                b.append(OP["PUSHN"])
            else:
                b.append(OP["PUSHI"])
                b += struct.pack("<h", init[1])
                b += bytes([OP["BUILT"], BUILTIN["array"], 1])
            b += bytes([OP["STG"], g])
        b += bytes([OP["PUSHN"], OP["RET"]])
        fn.code = bytes(b)
        prog.funcs.append(fn)
        start = fn.index
    return prog.funcs, start


def check_ontick(fn, fname, line):
    """WEAVE-SPEC 4.11.1: <= 64 emitted ops, no backward jump, no CALL -
    statically checkable, packer-rejected."""
    if fn.nops > 64:
        raise PackError(fname, line, "ontick handler is %d ops; the cap is "
                        "64 - per-frame JS does not fit 10-30k ops/s"
                        % fn.nops)
    if fn.has_backjump:
        raise PackError(fname, line, "ontick handler %s has a backward "
                        "jump; the cap is 64 straight-line ops - per-frame "
                        "JS does not fit 10-30k ops/s" % fn.name)
    if fn.has_call:
        raise PackError(fname, line, "ontick handler %s makes a call; the "
                        "cap is 64 straight-line ops - per-frame JS does "
                        "not fit 10-30k ops/s" % fn.name)


# --- the default icon (WEAVE-SPEC 2.12) --------------------------------------
# 16x16, stored as icon_draw16's body: 16 mask words then 16 data words, MSB
# leftmost (SPEC.md 25's header-less layout - the recorded reading of
# WEAVE-SPEC 2.12's "4 bytes per row": 2 mask + 2 data per row, blocked).
_ICON_DATA = [
    "................",
    ".##############.",
    ".#............#.",
    ".#.#..#..#..#.#.",
    ".#.#..#..#..#.#.",
    ".#.#..#..#..#.#.",
    ".#.#..#..#..#.#.",
    ".#.#.#.##.#.#.#.",
    ".#.#.#.##.#.#.#.",
    ".#..#......#..#.",
    ".#..#......#..#.",
    ".#............#.",
    ".#..#.#..#.#..#.",
    ".#............#.",
    ".##############.",
    "................",
]


def default_icon():
    def row_word(row, pred):
        w = 0
        for ch in row:
            w = (w << 1) | (1 if pred(ch) else 0)
        return w
    mask = b"".join(struct.pack(">H", row_word(r, lambda c: True)
                                if 0 < i < 15 else 0)
                    for i, r in enumerate(_ICON_DATA))
    data = b"".join(struct.pack(">H", row_word(r, lambda c: c == "#"))
                    for r in _ICON_DATA)
    return mask + data


def align16(n):
    return (n + 15) & ~15


def scan_builtins(code):
    """Which builtin indices a code blob calls - drives the flags word."""
    used, i = set(), 0
    while i < len(code):
        name, spec = OPS[code[i]]
        i += 1
        if name == "BUILT":
            used.add(code[i])
        i += {"": 0, "b": 1, "w": 2, "r": 2, "bb": 2}[spec]
    return used


# --- the packer --------------------------------------------------------------
class PackResult:
    def __init__(self):
        self.data = b""
        self.app = None
        self.prog = None
        self.start_fn = None
        self.warnings = []
        self.ask_kb = 0


def _find_file(dirname, name):
    """Case-insensitive 8.3 lookup - committed sources are lower-case, WML
    names them upper (recorded decision)."""
    for f in sorted(os.listdir(dirname or ".")):
        if f.lower() == name.lower():
            return os.path.join(dirname, f)
    return None


def pack_project(wml_path, with_source=False):
    """One project -> one deterministic .WAB (WEAVE-SPEC 11). The project is
    the .WML file's directory; companion files are found by the WML stem
    (FORM.WJS, FORM.WFX, FORM.WSP) with MAIN/SHEET/SPRITES as the spec's
    project-folder spellings."""
    if os.path.isdir(wml_path):
        p = _find_file(wml_path, "MAIN.WML")
        if not p:
            raise PackError(wml_path, 0, "no MAIN.WML in the project folder")
        wml_path = p
    dirname = os.path.dirname(wml_path)
    fname = os.path.basename(wml_path)
    stem = os.path.splitext(fname)[0]
    wml_text = open(wml_path, "rb").read().decode("latin-1")

    res = PackResult()
    atoms = Interner(fname)
    root = WmlScanner(wml_text, fname).parse()
    app = Analyzer(fname, atoms).run(root)
    res.app = app

    # the script (interning: WJS token order, after the WML walk)
    wjs_text = ""
    wjs_name = fname
    prog = JsProgram()
    toks = None
    if app.script_src:
        sp = _find_file(dirname, app.script_src)
        if not sp:
            raise PackError(fname, app.script_line, 'script: src="%s" not '
                            "found beside the .WML" % app.script_src)
        wjs_text = open(sp, "rb").read().decode("latin-1")
        wjs_name = os.path.basename(sp)
        toks = js_tokenize(wjs_text, wjs_name, atoms)
        prog = js_collect(toks, wjs_name)
    res.prog = prog

    # the sheet (interning: labels in CELLS row-major order, last)
    grid_comp = next((c for c in app.comps if c.grid), None)
    cells, formulas = [], []
    if grid_comp:
        cols, rows = grid_comp.grid
        wfx = _find_file(dirname, stem + ".WFX") \
            or _find_file(dirname, "SHEET.WFX")
        if wfx:
            raw = parse_wfx(open(wfx, "rb").read().decode("latin-1"),
                            os.path.basename(wfx), cols, rows)
            for (r, c) in sorted(raw):
                kind = raw[(r, c)]
                if kind[0] == "num":
                    cells.append((r, c, 1, kind[1]))
                elif kind[0] == "label":
                    cells.append((r, c, 2,
                                  atoms.intern(kind[1], kind[2])))
                else:
                    fxc = FxCompiler(cols, rows, os.path.basename(wfx),
                                     kind[2])
                    formulas.append(fxc.compile(kind[1]))
                    cells.append((r, c, 3, len(formulas) - 1))

    # sprites
    sprites = []
    sprite_comps = [c for c in app.comps if c.sprite]
    if sprite_comps:
        wsp = _find_file(dirname, stem + ".WSP") \
            or _find_file(dirname, "SPRITES.WSP")
        if not wsp:
            raise PackError(fname, sprite_comps[0].line, "sprites declared "
                            "and no .WSP art file beside the .WML")
        sprites = parse_wsp(open(wsp, "rb").read().decode("latin-1"),
                            os.path.basename(wsp))
        index = {s.name: k for k, s in enumerate(sprites)}
        for c in sprite_comps:
            img = c.sprite["img"]
            if img not in index:
                raise PackError(fname, c.sprite["line"], 'sprite: img="%s" '
                                "is not in the .WSP file" % img)
            # the sprite-image record: name atom 11 ('frame'), kind
            # PK_SPRITE - no 'img' well-known atom exists, and the record
            # doubles as frame's initial value (WEAVE-SPEC 3.3)
            c.props.append((WK["frame"], PK_SPRITE, index[img]))
            c.props.sort(key=lambda r: r[0])

    # compile WJS with the component map
    ctx = dict(idmap=app.idmap, cardmap=app.cardmap,
               ctype_of={c.comp_id: c.tag for c in app.comps})
    funcs, start_fn = compile_wjs(prog, toks, wjs_name, ctx) if toks \
        else ([], None)
    res.start_fn = start_fn

    # resolve event bindings to function indices
    for c in app.comps:
        for atom, (fnname, ln) in list(c.events.items()):
            if fnname not in prog.fnindex:
                raise PackError(fname, ln, '%s="%s": no such function in '
                                "the script" % (WK_NAME[atom], fnname))
            fi = prog.fnindex[fnname]
            if atom == WK["ontick"]:
                check_ontick(prog.funcs[fi], fname, ln)
            c.props.append((atom, PK_FUNC, fi))
        c.props.sort(key=lambda r: r[0])
    menu_blob = b""
    if app.menus:
        mb = bytearray([len(app.menus)])
        for m in app.menus:
            mb.append(m.title_atom)
            mb.append(len(m.items))
            for label_atom, fnname, ln in m.items:
                mb.append(label_atom)
                if fnname is None:
                    mb.append(0xFF)
                else:
                    if fnname not in prog.fnindex:
                        raise PackError(fname, ln, 'oncommand="%s": no such'
                                        " function in the script" % fnname)
                    mb.append(prog.fnindex[fnname])
        menu_blob = bytes(mb)

    if scan_builtins(b"".join(f.code for f in prog.funcs)) \
            & {BUILTIN["playSound"]}:
        res.warnings.append("playSound: v1 carries no clips; it refuses "
                            "politely at run time (WEAVE-SPEC 8.4)")

    data = _assemble(app, prog, start_fn, atoms, formulas, cells, sprites,
                     menu_blob, wml_text if with_source else None,
                     wjs_text if with_source else None, fname, root.line)
    res.data = data
    # the memory ask, computable before any I/O (WEAVE-SPEC 10.1)
    hdr = data[:32]
    res.ask_kb = (len(data) + 1023) // 1024 + hdr[10] + hdr[11] + hdr[14]
    # the packer's own re-read (WEAVE-SPEC 11.3) - not the t_wab gate
    Bundle(data, stem.upper() + ".WAB")
    return res


def _assemble(app, prog, start_fn, atoms, formulas, cells, sprites,
              menu_blob, src_wml, src_wjs, fname, app_line):
    """Sections in ascending type order, 16-byte aligned, no timestamps -
    WEAVE-SPEC 2.14's determinism rules, each numbered where it lands."""
    has_grid = any(c.grid for c in app.comps)
    has_canvas = any(c.canvas for c in app.comps)

    # UISTREAM (rule 2: document order)
    ui = bytearray()
    nrec = 0
    for k, (cid, comps) in enumerate(app.cards):
        ui += bytes([REC_CARD, k + 1]) + b"\0" * 6 + b"\xff\xff"
        nrec += 1
        for c in comps:
            ui += bytes([REC_COMP, c.comp_id, c.ctype, c.w, c.h,
                         c.style, c.cflags, 0])
            c._prop_pos = len(ui)       # patched with the block offset
            ui += b"\xff\xff"
            nrec += 1
    ui += bytes(10)                     # REC_END
    nrec += 1

    # PROPS (rule 5): blocks in UISTREAM order, the app block last, gap-
    # free; blobs after all blocks in record-emission order
    all_comps = [c for _, comps in app.cards for c in comps]
    app_records = [(WK["card"], PK_INT, app.entry_card)]
    if start_fn is not None:            # the module-init function
        app_records.append((WK["start"], PK_FUNC, start_fn))    # (WEAVE-
    if menu_blob:                       # SPEC 2.6.2 - see compile_wjs)
        app_records.append((WK["MENUS"], PK_BLOB, ("blob", menu_blob)))
    app_records.sort(key=lambda r: r[0])
    offset, block_at = 0, {}
    for c in all_comps:
        if c.props:
            block_at[c.comp_id] = offset
            offset += 4 * (len(c.props) + 1)
    app_at = offset
    offset += 4 * (len(app_records) + 1)
    blobs = []
    props = bytearray()

    def put_block(records):
        nonlocal offset
        for atom, kind, val in records:
            if kind == PK_BLOB:
                blobs.append(val[1])
                v = offset
                offset += len(val[1])
            else:
                v = val & 0xFFFF
            props.extend(bytes([atom, kind]) + struct.pack("<H", v))
        props.extend(bytes(4))

    for c in all_comps:
        if c.props:
            put_block(c.props)
    put_block(app_records)
    for b in blobs:
        props.extend(b)
    for c in all_comps:
        pos = c._prop_pos
        ui[pos:pos + 2] = struct.pack(
            "<H", block_at.get(c.comp_id, 0xFFFF))

    # CODE (rule 4)
    code = bytearray([len(prog.funcs), len(prog.globals)])
    body_at = 2 + 4 * len(prog.funcs)
    off = body_at
    for fn in prog.funcs:
        code += struct.pack("<H", off)
        code += bytes([len(fn.params), fn.nlocals])
        off += len(fn.code)
    for fn in prog.funcs:
        code += fn.code
    if not prog.funcs:
        code += bytes([OP["HALT"]])     # the empty table's guard

    # ATOMS
    at = bytearray(struct.pack("<H", len(atoms.strings)))
    pos = 2 + 2 * len(atoms.strings)
    for s in atoms.strings:
        at += struct.pack("<H", pos)
        pos += len(s) + 2
    for s in atoms.strings:
        raw = s.encode("ascii")
        at += bytes([len(raw)]) + raw + b"\0"

    sections = [(SEC_UISTREAM, bytes(ui), nrec),
                (SEC_PROPS, bytes(props), app_at),
                (SEC_CODE, bytes(code), 0),
                (SEC_ATOMS, bytes(at), 0)]

    if has_grid:
        fx = bytearray(struct.pack("<H", len(formulas)))
        pos = 2 + 2 * len(formulas)
        for f in formulas:
            fx += struct.pack("<H", pos)
            pos += len(f)
        for f in formulas:
            fx += f
        ce = bytearray()
        for r, c, kind, payload in cells:     # rule 7: row-major, sorted
            ce += bytes([r, c, kind, 0])
            ce += struct.pack("<i" if kind == 1 else "<I",
                              payload if kind == 1 else payload & 0xFFFF)
        sections.append((SEC_FXCODE, bytes(fx), len(formulas)))
        sections.append((SEC_CELLS, bytes(ce), len(cells)))

    if sprites:
        sp = bytearray([len(sprites), 0])
        data_at = 2 + 8 * len(sprites)
        blobs2 = []
        for s in sprites:
            wb = s.w // 8
            sp += bytes([wb, s.h, s.frames, 0])
            sp += struct.pack("<H", data_at)
            sp += b"\0\0"
            blob = b"".join(img + msk
                            for img, msk in zip(s.images, s.masks))
            blobs2.append(blob)
            data_at += len(blob)
        for b in blobs2:
            sp += b
        sections.append((SEC_SPRITES, bytes(sp), len(sprites)))

    sections.append((SEC_ICON, default_icon(), 0))

    flags = 0
    used = scan_builtins(b"".join(f.code for f in prog.funcs))
    if has_grid:
        flags |= WABF_GRID
    if has_canvas:
        flags |= WABF_CANVAS
    # WABF_TIMER: timer() called, or a caret blinks by WM_TIMER - any input,
    # or the grid's formula bar, itself a library-wired input (WEAVE-SPEC
    # 2.2.1's three causes)
    if BUILTIN["timer"] in used or has_grid \
            or any(c.tag == "input" for c in app.comps):
        flags |= WABF_TIMER
    if used & {BUILTIN["saveState"], BUILTIN["loadState"]}:
        flags |= WABF_STATE
    if src_wml is not None:
        flags |= WABF_SOURCE
        wml_b = src_wml.replace("\r\n", "\n").encode("latin-1")
        if not wml_b.endswith(b"\n"):
            wml_b += b"\n"
        wjs_b = (src_wjs or "").replace("\r\n", "\n").encode("latin-1")
        if wjs_b and not wjs_b.endswith(b"\n"):
            wjs_b += b"\n"
        sections.append((SEC_SOURCE, wml_b + wjs_b, len(wml_b)))

    # claim asks (WEAVE-SPEC 2.2)
    grid_kb = 0
    gc = next((c for c in app.comps if c.grid), None)
    if gc:
        cols, rows = gc.grid
        # max(8, ceil((16 + rows*cols*4)/1024) + 2), capped at 26 - the 8
        # floor is the header's claim envelope (WEAVE-SPEC 5.6)
        grid_kb = min(26, max(8, (16 + rows * cols * 4 + 1023) // 1024 + 2))
    canvas_kb = 0
    cc = next((c for c in app.comps if c.canvas), None)
    if cc:
        # WEAVE-SPEC 6.10.4: the claim holds a 16-byte header, 24 bytes per
        # sprite record and the 1bpp buffer - and the buffer's height is the
        # ROUNDED one the runtime derives from the record byte (cc.h * 8),
        # never the WML `h`.  Sizing it from the WML `h` under-asked by up to
        # seven rows: a canvas of h="153" asked for 6KB and needed 6,800
        # bytes.  The largest legal canvas, 320x160 with sixteen sprites, is
        # 16 + 384 + 6,400 = 6,800 -> 7KB, inside the byte's 8.
        need = 16 + 24 * len(cc.sprites) + (cc.canvas["w"] // 8) * (cc.h * 8)
        canvas_kb = min(8, max(2, (need + 1023) // 1024))

    # lay the file out (rule 1)
    first = align16(32 + 8 * len(sections))
    table = bytearray()
    body = bytearray()
    at_ = first
    for typ, payload, extra in sections:
        table += bytes([typ, 0]) + struct.pack("<HHH", at_, len(payload),
                                               extra)
        body += payload
        nxt = align16(at_ + len(payload))
        body += bytes(nxt - at_ - len(payload))
        at_ = nxt
    # total size = end of the LAST payload, unpadded (WEAVE-SPEC 2.3)
    ends = []
    a = first
    for typ, payload, extra in sections:
        ends.append(a + len(payload))
        a = align16(a + len(payload))
    total = ends[-1]
    if total > WAB_CAP:
        raise PackError(fname, app_line, "bundle is %d bytes; the cap is "
                        "63488 - the directory size must stand for the "
                        "resident ask" % total)
    hdr = bytearray()
    hdr += WAB_MAGIC
    hdr += struct.pack("<HHH", WAB_VERSION, total, flags)
    hdr += bytes([app.vm_kb, grid_kb, len(sections), app.entry_card,
                  canvas_kb, 0])
    name_b = app.name.encode("ascii")
    hdr += name_b + bytes(16 - len(name_b))
    assert len(hdr) == 32
    pad = bytes(first - 32 - len(table))
    out = bytes(hdr) + bytes(table) + pad + bytes(body)
    return out[:total]


# --- the bundle reader (WEAVE-SPEC 2, refusing per 10.4) ---------------------
class CompRec:
    def __init__(self, comp_id, ctype, w, h, style, cflags):
        self.comp_id, self.ctype = comp_id, ctype
        self.tag = CTYPE_NAME[ctype]
        self.w, self.h, self.style, self.cflags = w, h, style, cflags
        self.props = {}                 # atom -> (kind, value)


class Bundle:
    """The reader.  WEAVE-SPEC 2.1: every byte read off a disk is hostile,
    so nothing here believes an offset it has not bounds-checked, and a
    malformed bundle refuses with the field named (WEAVE-SPEC 10.4) rather
    than raising - on the 8086 the same byte is a wild read, a bad divide
    or a claim sized from a lie, and there is no traceback to catch."""

    def __init__(self, data, name):
        self.data, self.name = data, name
        e = lambda field: (_ for _ in ()).throw(BundleError(name, field))
        if len(data) < 32 or data[:4] != WAB_MAGIC:
            e("magic")
        ver, total, flags = struct.unpack_from("<HHH", data, 4)
        if ver != WAB_VERSION:
            e("version")
        if total != len(data) or total > WAB_CAP:
            e("total size")
        if flags & ~0x1F:
            e("flags")
        self.flags = flags
        self.vm_kb, self.grid_kb, nsec, self.entry, self.canvas_kb, rsvd \
            = data[10:16]
        if rsvd:
            e("header")
        # The three claim-KB bytes ARE WEAVE-SPEC 10.1's memory refusal:
        # the whole ask is computed from the directory entry plus these,
        # BEFORE any I/O, so an out-of-range one is a lie the heap would
        # believe.  All three are ranged, not just the VM's.
        if not 16 <= self.vm_kb <= 32:
            e("vm KB")
        if self.grid_kb and not 8 <= self.grid_kb <= 26:
            e("grid KB")
        if self.canvas_kb and not 2 <= self.canvas_kb <= 8:
            e("canvas KB")
        # 2.4: UISTREAM, PROPS, CODE and ATOMS are mandatory and ICON is
        # always present, so five rows is the floor and nine the ceiling.
        if not 5 <= nsec <= 9:
            e("section count")
        if not 1 <= self.entry <= 8:
            e("entry card")
        nm = data[16:32]
        z = nm.find(b"\0")
        if z <= 0 or any(b for b in nm[z:]):
            e("app name")
        if any(not 0x20 <= b <= 0x7E for b in nm[:z]):
            e("app name")
        self.app_name = nm[:z].decode("ascii")
        self.sections = {}
        if 32 + 8 * nsec > total:
            e("section table")
        prev, end = 0, 32 + 8 * nsec
        for k in range(nsec):
            typ, z2, ofs, ln, extra = struct.unpack_from("<BBHHH", data,
                                                         32 + 8 * k)
            if typ <= prev or typ > 9 or z2:
                e("section table")
            # 2.3: the first section begins at align16(32 + 8*count) and
            # each next at align16(previous offset + length) - sections
            # abut, so a gap or an overlap is a different file.
            if ofs != align16(end) or ofs + ln > total:
                e("section table")
            if any(data[end:ofs]):      # 2.1: padding bytes are 0x00
                e("section padding")
            prev, end = typ, ofs + ln
            self.sections[typ] = (data[ofs:ofs + ln], extra)
        if end != total:                # 2.3: no tail padding, ever
            e("total size")
        for typ in (SEC_UISTREAM, SEC_PROPS, SEC_CODE, SEC_ATOMS):
            if typ not in self.sections:
                e("section table")
        # 2.4: ICON always - the packer supplies a default when the
        # project has none, so an absent one is a truncated bundle.
        if SEC_ICON not in self.sections \
                or len(self.sections[SEC_ICON][0]) != 64:
            e("ICON")
        self._check_couplings(e)
        # Parse order is dependency order: the atom pool, the sprite
        # table and the function table are what a property record's value
        # is bounds-checked against, so they are read first.
        self._parse_atoms(e)
        self._parse_sprites(e)
        self._parse_code(e)
        self._parse_uistream(e)
        self._parse_props(e)
        self._parse_fx(e)
        self._check_comp_props(e)
        self.icon = self.sections[SEC_ICON][0]
        src, wml_len = self.sections.get(SEC_SOURCE, (b"", 0))
        if src and wml_len > len(src):
            e("SOURCE")
        self.source = (src[:wml_len], src[wml_len:]) if src else None

    def _check_couplings(self, e):
        """2.2.1/2.4 - which sections ride which flag bits.  The packer
        computes the flags word; a bundle whose flags disagree with its own
        section table makes the load-time capability test (10.2) lie."""
        grid = bool(self.flags & WABF_GRID)
        if (SEC_FXCODE in self.sections) != grid:
            e("flags")
        if (SEC_CELLS in self.sections) != grid:
            e("flags")
        if (self.grid_kb > 0) != grid:
            e("flags")
        if (self.canvas_kb > 0) != bool(self.flags & WABF_CANVAS):
            e("flags")
        if (SEC_SOURCE in self.sections) != bool(self.flags & WABF_SOURCE):
            e("flags")
        if SEC_SPRITES in self.sections \
                and not self.flags & WABF_CANVAS:
            e("flags")

    def _parse_atoms(self, e):
        """2.7 - the pool walked end to end: the count word must leave
        room for its own offset table, every offset and length must stay
        inside the section, and every body byte must be one the cell font
        has a glyph for."""
        b, _ = self.sections[SEC_ATOMS]
        if len(b) < 2:
            e("atom pool")
        n = struct.unpack_from("<H", b, 0)[0]
        if n > APP_ATOM_MAX:
            e("atom id")
        if 2 + 2 * n > len(b):
            e("atom pool")
        self.atom_strings = []
        pos = 2 + 2 * n
        for k in range(n):
            ofs = struct.unpack_from("<H", b, 2 + 2 * k)[0]
            # Offsets ascend in id order and strings pack without gaps.
            if ofs != pos or ofs >= len(b):
                e("atom pool")
            ln = b[ofs]
            if ln < 1 or ofs + 1 + ln + 1 > len(b):
                e("atom pool")
            s = b[ofs + 1:ofs + 1 + ln]
            if b[ofs + 1 + ln] != 0:
                e("atom pool")
            # 2.7/3.1: folded to 0x20..0x7E.  A decode() here would raise
            # instead of refusing - and the 8086 has no decode at all.
            if any(not 0x20 <= c <= 0x7E for c in s):
                e("atom pool")
            self.atom_strings.append(s.decode("ascii"))
            pos = ofs + 1 + ln + 1
        if pos != len(b):
            e("atom pool")

    def atom_str(self, aid):
        if APP_ATOM0 <= aid < APP_ATOM0 + len(self.atom_strings):
            return self.atom_strings[aid - APP_ATOM0]
        if aid in WK_NAME:
            return WK_NAME[aid]
        raise BundleError(self.name, "atom id")

    def _atom_ok(self, aid):
        return aid in WK_NAME or \
            APP_ATOM0 <= aid < APP_ATOM0 + len(self.atom_strings)

    def _block(self, ofs, e):
        b, _ = self.sections[SEC_PROPS]
        out = {}
        while True:
            if ofs + 4 > len(b):
                e("prop block")
            atom, kind = b[ofs], b[ofs + 1]
            val = struct.unpack_from("<H", b, ofs + 2)[0]
            if atom == 0:
                return out
            if kind > PK_SPRITE:
                e("prop kind")
            if not self._atom_ok(atom):
                e("atom id")
            if kind == PK_ATOM and not self._atom_ok(val):
                e("atom id")
            if kind == PK_BLOB and val >= len(b):
                e("prop blob")
            if kind == PK_FUNC and val >= len(self.functions):
                e("function index")
            if kind == PK_SPRITE and val >= len(self.sprites):
                e("sprite index")
            if kind == PK_INT and val & 0x8000:
                val -= 0x10000          # PK_INT is a signed word
            out[atom] = (kind, val)
            ofs += 4

    def blob(self, ofs):
        return self.sections[SEC_PROPS][0][ofs:]

    def _items_blob(self, ofs, e):
        """2.6.1 - count byte then count atom-id bytes.  Walked AT LOAD:
        a count larger than the blob's remaining bytes is a short list on
        the model and a wild read on the machine."""
        b = self.sections[SEC_PROPS][0]
        if ofs >= len(b):
            e("prop blob")
        n = b[ofs]
        if n > 64 or ofs + 1 + n > len(b):
            e("list items")
        for a in b[ofs + 1:ofs + 1 + n]:
            if not self._atom_ok(a):
                e("atom id")

    def _menus_blob(self, ofs, e):
        """2.6.2 - the MENUS blob, walked AT LOAD.  The model reads menus
        lazily and the 8086 cannot: it must have them before the first
        OSAPI_MENU_SET, so an unwalked blob is a wild read there and clean
        here, which no differential can see."""
        b = self.sections[SEC_PROPS][0]
        if ofs >= len(b):
            e("prop blob")
        nm = b[ofs]
        if not 1 <= nm <= 5:            # MENU_APPMAX (SPEC.md 12.2)
            e("menu count")
        pos = ofs + 1
        self.menus = []
        for _ in range(nm):
            if pos + 2 > len(b):
                e("menu blob")
            title, ni = b[pos], b[pos + 1]
            if not self._atom_ok(title):
                e("atom id")
            if not 1 <= ni <= 8:
                e("menu item count")
            pos += 2
            if pos + 2 * ni > len(b):
                e("menu blob")
            items = []
            for it in range(ni):
                label, fn = b[pos + 2 * it], b[pos + 2 * it + 1]
                if not self._atom_ok(label):
                    e("atom id")
                if fn != 0xFF and fn >= len(self.functions):
                    e("menu command")
                items.append((label, fn))
            self.menus.append((title, items))
            pos += 2 * ni

    def _parse_props(self, e):
        _, app_at = self.sections[SEC_PROPS]
        if app_at >= len(self.sections[SEC_PROPS][0]):
            e("app block")
        self.app_props = self._block(app_at, e)
        self.menus = []
        for atom, (kind, val) in self.app_props.items():
            if atom not in (WK["card"], WK["start"], WK["MENUS"]):
                e("app block")          # 2.6.2: nothing else may appear
            if atom == WK["MENUS"]:
                self._menus_blob(val, e)

    def _parse_uistream(self, e):
        b, nrec = self.sections[SEC_UISTREAM]
        if len(b) != 10 * nrec:
            e("record count")
        self.cards = []
        seen_ids = set()
        comps = None
        end_seen = False
        next_id, prev_ct = 1, None
        for k in range(nrec):
            r = b[10 * k:10 * k + 10]
            if r[0] == REC_END:
                if k != nrec - 1 or any(r[1:]):
                    e("REC_END")
                end_seen = True
            elif r[0] == REC_CARD:
                if not 1 <= r[1] <= 8 or r[1] != len(self.cards) + 1:
                    e("card index")
                # 2.5: bytes +2..+7 are 0 and v1 cards carry no props.
                if any(r[2:8]) or struct.unpack_from("<H", r, 8)[0] != 0xFFFF:
                    e("card record")
                comps = []
                prev_ct = None
                self.cards.append(comps)
            elif r[0] == REC_COMP:
                if comps is None:
                    e("component before any card")
                cid, ct = r[1], r[2]
                if ct not in CTYPE_NAME:
                    e("ctype")
                # 2.5/2.14 rule 2: comp_ids are document order, 1-based.
                if cid in seen_ids or not 1 <= cid <= 250 or cid != next_id:
                    e("comp_id")
                seen_ids.add(cid)
                next_id += 1
                if r[7]:
                    e("component record")
                if r[5] & 0xF0:
                    e("style byte")
                if (r[5] >> 2) & 3 == 3:
                    e("style byte")
                if r[6] & ~7:
                    e("cflags")
                self._geometry(CTYPE_NAME[ct], r[3], r[4], prev_ct, e)
                c = CompRec(cid, ct, r[3], r[4], r[5], r[6])
                pofs = struct.unpack_from("<H", r, 8)[0]
                if pofs != 0xFFFF:
                    if pofs >= len(self.sections[SEC_PROPS][0]):
                        e("prop block")
                    c.props = self._block(pofs, e)
                comps.append(c)
                prev_ct = ct
            else:
                e("record kind")
        if not end_seen:
            e("REC_END")
        if not self.cards or not 1 <= self.entry <= len(self.cards):
            e("entry card")
        self.comps = {c.comp_id: c for comps in self.cards for c in comps}

    def _geometry(self, tag, w, h, prev_ct, e):
        """2.5/3.3 - the record's w/h bytes, per ctype.  A 0 where the
        spec says "never 0" is a divide or a negative repeat count on the
        machine, so it refuses here."""
        if tag == "sprite":
            # 2.5: a <sprite> record follows its <canvas> directly.
            if prev_ct not in (CTYPE["canvas"], CTYPE["sprite"]):
                e("sprite record")
            if w or h:                  # its geometry lives in SPRITES
                e("sprite record")
            return
        if tag == "canvas":
            # w/8 cells (64..320 px) and ceil(h/8) rows (32..160 px).
            if not 8 <= w <= 40:
                e("canvas w")
            if not 4 <= h <= 20:
                e("canvas h")
            return
        if w > 160:
            e("component w")
        if h > 40:
            e("component h")
        if tag == "box":                # 3.3: w,h required, >= 2x1
            if w < 2:
                e("box w")
            if h < 1:
                e("box h")
        elif tag == "spacer":           # 3.3: w required
            if w < 1:
                e("spacer w")

    def _check_comp_props(self, e):
        """3.3's required properties and their bounds - checked by no
        reader in this tree before now.  Each is something the runtime
        divides by, sizes a claim from, or dereferences."""
        for c in self.comps.values():
            t = c.tag
            if t == "grid":
                if WK["cols"] not in c.props:
                    e("grid cols")
                if WK["rows"] not in c.props:
                    e("grid rows")
                cols = c.props[WK["cols"]][1]
                rows = c.props[WK["rows"]][1]
                if not 1 <= cols <= 26:
                    e("grid cols")
                if not 1 <= rows <= 256:
                    e("grid rows")
                # 5.6: the cell store plus its pool must fit a 26KB claim
                # - and the header's grid KB was sized from these numbers.
                if rows * cols > 6140:
                    e("grid size")
            elif t == "radio":
                if WK["group"] not in c.props:
                    e("radio group")
            elif t == "meter":
                if WK["max"] in c.props \
                        and not 1 <= c.props[WK["max"]][1] <= 32000:
                    e("meter max")
            elif t == "list" and WK["ITEMS"] in c.props:
                self._items_blob(c.props[WK["ITEMS"]][1], e)

    def _parse_code(self, e):
        b, _ = self.sections[SEC_CODE]
        if len(b) < 2:
            e("function table")
        nf, ng = b[0], b[1]
        if nf > 128 or ng > 128:
            e("function table")
        self.nglobals = ng
        self.functions = []
        body_at = 2 + 4 * nf
        if body_at > len(b):
            e("function table")
        if nf == 0:
            # 2.8: a scriptless bundle's body is exactly one HALT byte.
            if b[body_at:] != bytes([OP["HALT"]]):
                e("function table")
        prev = body_at
        for k in range(nf):
            ofs, nargs, nloc = struct.unpack_from("<HBB", b, 2 + 4 * k)
            # 2.8: functions are packed back to back from 2+4F, each a
            # contiguous run - so every offset bounds the previous body.
            bad = (ofs != prev) if k == 0 else (ofs <= prev)
            if bad:
                e("function table")
            if ofs >= len(b) or nargs > 8 or nloc > 16 or nloc < nargs:
                e("function table")
            prev = ofs
            self.functions.append((ofs, nargs, nloc))
        self.code = b

    def _parse_fx(self, e):
        self.formulas, self.cells = [], []
        if SEC_FXCODE in self.sections:
            b, nf = self.sections[SEC_FXCODE]
            if len(b) < 2 or struct.unpack_from("<H", b, 0)[0] != nf:
                e("formula count")
            if 2 + 2 * nf > len(b):
                e("formula count")
            prev = 2 + 2 * nf
            for k in range(nf):
                ofs = struct.unpack_from("<H", b, 2 + 2 * k)[0]
                bad = (ofs != prev) if k == 0 else (ofs <= prev)
                if bad:
                    e("formula table")
                if ofs >= len(b):
                    e("formula table")
                prev = ofs
                self.formulas.append(b[ofs:])
        if SEC_CELLS in self.sections:
            b, nc = self.sections[SEC_CELLS]
            if len(b) != 8 * nc:
                e("cell record count")
            for k in range(nc):
                r, c, kind, z = b[8 * k:8 * k + 4]
                if z or kind not in (1, 2, 3):
                    e("cell record")
                if c > 25:
                    e("cell record")
                if kind == 1:
                    payload = struct.unpack_from("<i", b, 8 * k + 4)[0]
                else:
                    payload = struct.unpack_from("<I", b, 8 * k + 4)[0] \
                        & 0xFFFF
                    if kind == 2 and not self._atom_ok(payload):
                        e("cell record")
                    # 2.9/2.10: CELLS reference formulas by INDEX into
                    # the FXCODE table - past its end is a wild read at
                    # the first recalc, which is at open.
                    if kind == 3 and payload >= len(self.formulas):
                        e("cell record")
                self.cells.append((r, c, kind, payload))

    def _parse_sprites(self, e):
        self.sprites = []
        if SEC_SPRITES not in self.sections:
            return
        b, ns = self.sections[SEC_SPRITES]
        if len(b) < 2 or b[0] != ns or b[1] or not 1 <= ns <= 16:
            e("sprite count")
        if 2 + 8 * ns > len(b):
            e("sprite count")
        for k in range(ns):
            wb, h, nf, z1 = b[2 + 8 * k:2 + 8 * k + 4]
            dofs = struct.unpack_from("<H", b, 2 + 8 * k + 4)[0]
            if z1 or not 1 <= wb <= 8 or not 1 <= h <= 64 \
                    or not 1 <= nf <= 8:
                e("sprite descriptor")
            if struct.unpack_from("<H", b, 2 + 8 * k + 6)[0]:
                e("sprite descriptor")
            fsz = wb * h
            if dofs + nf * 2 * fsz > len(b):
                e("sprite data")
            images, masks = [], []
            for f in range(nf):
                base = dofs + f * 2 * fsz
                images.append(b[base:base + fsz])
                masks.append(b[base + fsz:base + 2 * fsz])
                if len(masks[-1]) != fsz:
                    e("sprite data")
            self.sprites.append(dict(wb=wb, h=h, frames=nf, images=images,
                                     masks=masks))


# --- the runtime model: VM, components, ring (WEAVE-SPEC 4, 6) ---------------
class ScriptError(Exception):
    """Carries the pinned sentence (WEAVE-SPEC 10.6); the fn index is
    substituted where it is raised."""


class Runaway(Exception):
    pass


def truthy(cell):
    t, v = cell
    if t == T_INT or t == T_BOOL:
        return v != 0
    if t == T_STR:
        return len(v) > 0
    if t == T_NULL:
        return False
    return True                         # arrays, components


def wrap16(v):
    return ((v + 0x8000) & 0xFFFF) - 0x8000


# WEAVE-SPEC 6.9.1's pinned geometry.  The column width is FIXED - a fitted
# one would turn a one-cell edit into a re-compose of every band, and two
# implementations would have to fit identically or the diff is noise.
WG_GUT = 4                              # the row-number gutter, in cells
WG_COLW = 8                             # every data column, in cells
WG_BAR = 2                              # the formula bar, in 8-px rows
WG_HDR = 1                              # ...and the column-header band


def grid_geom(cols, rows, w, h):
    """WEAVE-SPEC 6.9.1: (visible columns, visible rows) for a `w` x `h`
    component rect."""
    vc = max(1, min(cols, (w - WG_GUT) // WG_COLW))
    vr = max(0, min(rows, h - (WG_BAR + WG_HDR)))
    return vc, vr


class GridRt:
    """The grid cell store and WEAVE-SPEC 5.5's two-pass recalc."""

    def __init__(self, cols, rows, bundle):
        self.cols, self.rows, self.b = cols, rows, bundle
        self.vals = {}                  # (r,c) -> ['num',v] ['label',s]
        self.circ = set()               # ['formula',idx,cached] (bundle RPN)
        self.top = self.left = 0        # 6.9.1's scroll origin
        for r, c, kind, payload in bundle.cells:
            if kind == 1:
                self.vals[(r, c)] = ["num", payload]
            elif kind == 2:
                self.vals[(r, c)] = ["label", bundle.atom_str(payload)]
            else:
                self.vals[(r, c)] = ["formula", payload, 0]
        self.dirty = set()              # 5.5.1's damage: grid ROWS
        self.recalc()                   # cached values computed at load

    # -- reading ----------------------------------------------------------
    def rpn(self, v):
        """The RPN stream of a formula cell: the BUNDLE's for kind 4, the
        cell's own for a runtime formula (WEAVE-SPEC 5.6 kind 6)."""
        return v[3] if len(v) > 3 else self.b.formulas[v[1]]

    def read_cell(self, r, c):
        v = self.vals.get((r, c))
        if v is None or v[0] == "label":
            return None
        return v[1] if v[0] == "num" else v[2]

    def display(self, r, c):
        v = self.vals.get((r, c))
        if v is None:
            return ""
        if v[0] == "label":
            return v[1]
        if v[0] == "formula" and (r, c) in self.circ:
            return "#CIRC"
        return fmt_16_16(v[1] if v[0] == "num" else v[2])

    def is_label(self, r, c):
        """WEAVE-SPEC 6.9.1 justifies a LABEL left and everything else -
        a number, an empty cell, an error - right."""
        v = self.vals.get((r, c))
        return v is not None and v[0] == "label"

    def source(self, r, c):
        """WEAVE-SPEC 6.9.3 run backwards: what the formula bar loads.

        A BUNDLE formula has no source anywhere - 2.9 carries compiled RPN
        and no text - so it loads as `=?`, the cell's own honest answer to
        "what is in you"."""
        v = self.vals.get((r, c))
        if v is None:
            return ""
        if v[0] == "label":
            return v[1]
        if v[0] == "formula":
            return "=" + v[4] if len(v) > 4 else "=?"
        return fmt_16_16(v[1])

    def band(self, w, h, row):
        """WEAVE-SPEC 6.9.1's band text: `row` = -1 for the header band,
        else the BAND index 0..VR-1 (grid row `top` + row).  Exactly `w`
        characters."""
        vc, _ = grid_geom(self.cols, self.rows, w, h)
        if row < 0:
            s = " " * WG_GUT
            for k in range(vc):
                c = self.left + k
                s += "   " + (chr(65 + c) if c < self.cols else " ") + "    "
            return s[:w].ljust(w)
        r = self.top + row
        s = ("%3d " % (r + 1)) if r < self.rows else " " * WG_GUT
        for k in range(vc):
            c = self.left + k
            if r >= self.rows or c >= self.cols:
                s += " " * WG_COLW
                continue
            t = self.display(r, c)[:WG_COLW - 1]
            s += (t.ljust(WG_COLW - 1) if self.is_label(r, c)
                  else t.rjust(WG_COLW - 1)) + " "
        return s[:w].ljust(w)

    # -- writing (WEAVE-SPEC 6.9.3) ---------------------------------------
    def set_num(self, r, c, v1616):
        self.vals[(r, c)] = ["num", wrap32(v1616)]

    def set_label(self, r, c, s):
        self.vals[(r, c)] = ["label", s]

    def clear_cell(self, r, c):
        self.vals.pop((r, c), None)

    def commit(self, r, c, text, fname="formula bar"):
        """WEAVE-SPEC 6.9.3's classification, in its pinned order.  Returns
        None, or the message a refusal shows."""
        t = text.strip()
        if not t:
            self.clear_cell(r, c)
            return None
        if t.startswith("="):
            try:
                rpn = FxCompiler(self.cols, self.rows, fname, 0).compile(t[1:])
            except PackError as e:
                return e.args[0] if e.args else "cannot read the formula"
            self.vals[(r, c)] = ["formula", -1, 0, rpn, t[1:]]
            return None
        if re.match(r"^-?[0-9]+(\.[0-9]+)?$", t):
            self.set_num(r, c, parse_number_16_16(t, fname, 0))
            return None
        self.set_label(r, c, fold_text(t))
        return None

    # -- 5.5's two passes -------------------------------------------------
    def formula_cells(self):
        return sorted(k for k, v in self.vals.items() if v[0] == "formula")

    def recalc(self):
        before = {k: self.display(*k) for k in self.vals}
        pass1 = {}
        for rc in self.formula_cells():   # pass 1: current values
            v = self.vals[rc]
            v[2] = fx_eval(self.rpn(v), self.read_cell)
            pass1[rc] = v[2]
        self.circ.clear()
        for rc in self.formula_cells():   # pass 2: differ -> #CIRC, pass-2
            v = self.vals[rc]             # value stands
            v[2] = fx_eval(self.rpn(v), self.read_cell)
            if v[2] != pass1[rc]:
                self.circ.add(rc)
        # 5.5.1's damage: a cell whose DISPLAY changed marks its grid ROW.
        self.dirty = set()
        n = 0
        for k in set(before) | set(self.vals):
            if before.get(k, "") != self.display(*k):
                self.dirty.add(k[0])
                n += 1
        return n


# ring policy classes (WEAVE-SPEC 4.9)
RING_COALESCE = {WK["onchange"], WK["onselect"], WK["onclick"],
                 WK["onscore"]}
RING_SINGLETON = {WK["ontimer"], WK["ontick"]}
RING_KEY = WK["onkey"]


class Ring:
    """16 slots x (comp, atom, d1, d2); the binding overflow policy."""

    def __init__(self, bel):
        self.q, self.bel = [], bel

    def enqueue(self, cid, atom, d1=0, d2=0):
        rec = [cid, atom, d1, d2]
        if atom in RING_SINGLETON:      # at most one queued; newest wins
            self.q = [r for r in self.q if r[1] != atom]
            self.q.append(rec)
            return True
        if atom in RING_COALESCE:       # per (comp, atom); newest wins,
            for k, r in enumerate(self.q):        # full or not
                if r[0] == cid and r[1] == atom:
                    self.q[k] = rec
                    return True
        if len(self.q) >= 16:
            if atom == RING_KEY:        # drop the newest queued non-key
                for k in range(len(self.q) - 1, -1, -1):
                    if self.q[k][1] != RING_KEY:
                        del self.q[k]
                        self.q.append(rec)
                        return True
                self.bel()              # full of keys: BEL, key refused
                return False
            return False                # non-key at a full ring: dropped
        self.q.append(rec)              # (recorded decision)
        return True


# how many of the record's words are handler arguments (WEAVE-SPEC 4.9.1,
# per 3.4's table); the runtime pads/truncates to the function's own nargs
# (recorded decision)
EVENT_NARGS = {WK["onclick"]: 0, WK["onchange"]: 1, WK["onkey"]: 2,
               WK["onselect"]: 2, WK["onedit"]: 2, WK["oncalc"]: 1,
               WK["oncollide"]: 2, WK["onwall"]: 2, WK["onscore"]: 2,
               WK["ontick"]: 1, WK["oncommand"]: 2}


class Runtime:
    MAX_OPS = 2_000_000     # the host stand-in for the 90-tick alert

    def __init__(self, bundle, idmap=None, sav_path=None):
        self.b = bundle
        self.idmap = dict(idmap or {})
        self.names = {v: k for k, v in self.idmap.items()}
        self.sav_path = sav_path
        self.sav_mem = None
        self.out = []
        self.ring = Ring(lambda: self.log("BEL (ring full of keys; key "
                                          "refused)"))
        self.timers = []
        self.now = 0
        self.rand_seed = 0x1234         # pinned: --run is deterministic
        self.card = bundle.entry
        self.globals = [(T_INT, 0)] * 128   # zeroed = int 0 (WEAVE-SPEC 4.7)
        self.stopped = False
        self.gfx_calls = 0
        self.comps = {}
        self.grid = None
        self.adapter = "cga"            # the layout a scroll clamp uses
        self._grect = None              # ...and the grid's rect on it
        self.canvas = None
        canvas_cid = None
        for comps in bundle.cards:
            for c in comps:
                st = self._init_comp(c)
                self.comps[c.comp_id] = st
                if c.tag == "canvas":
                    canvas_cid = c.comp_id
                elif c.tag == "sprite":
                    self.comps[canvas_cid]["sprites"].append(c.comp_id)
        # the module-init function, app block atom 40 (recorded reading)
        st = bundle.app_props.get(WK["start"])
        if st and st[0] == PK_FUNC:
            self.invoke(st[1], [])

    def log(self, s):
        self.out.append(s)

    def cname(self, cid):
        return self.names.get(cid, "#%d" % cid)

    def _init_comp(self, c):
        b = self.b
        g = lambda atom, dflt: c.props.get(atom, (0, dflt))[1]
        st = dict(rec=c, tag=c.tag,
                  hidden=1 if c.cflags & CF_HIDDEN else 0,
                  enabled=0 if c.cflags & CF_DISABLED else 1,
                  handlers={a: v for a, (k, v) in c.props.items()
                            if k == PK_FUNC})
        t = c.tag
        if t in ("label", "text"):
            st["text"] = b.atom_str(c.props[WK["text"]][1]) \
                if WK["text"] in c.props else ""
        elif t == "meter":
            st["max"] = g(WK["max"], 100)
            st["value"] = g(WK["value"], 0)
        elif t == "button":
            st["label"] = b.atom_str(c.props[WK["label"]][1]) \
                if WK["label"] in c.props else ""
        elif t in ("check", "radio"):
            st["label"] = b.atom_str(c.props[WK["label"]][1]) \
                if WK["label"] in c.props else ""
            st["checked"] = g(WK["checked"], 0)
            if t == "radio":
                st["group"] = b.atom_str(c.props[WK["group"]][1])
        elif t == "input":
            st["cols"] = g(WK["cols"], 20)
            st["text"] = b.atom_str(c.props[WK["text"]][1]) \
                if WK["text"] in c.props else ""
        elif t == "list":
            st["rows"] = g(WK["rows"], 8)
            st["sel"] = -1
            st["items"] = []
            if WK["ITEMS"] in c.props:
                blob = b.blob(c.props[WK["ITEMS"]][1])
                st["items"] = [b.atom_str(a) for a in blob[1:1 + blob[0]]]
        elif t == "grid":
            st["cols"] = g(WK["cols"], 1)
            st["rows"] = g(WK["rows"], 1)
            st["selrow"] = st["selcol"] = 1
            self.grid = GridRt(st["cols"], st["rows"], b)
            st["grid"] = self.grid
            self.grid_cid = c.comp_id
            self.grid_pending = False
            st["barsrc"] = self.grid.source(0, 0)   # 6.9.3, run backwards
        elif t == "canvas":
            st["walls"] = g(WK["walls"], 0xF)
            st["tick"] = g(WK["tick"], 0)
            st["running"] = False
            st["sleep"] = 1
            st["frame"] = 0
            st["phase"] = 0
            st["w"] = c.w * 8
            st["h"] = c.h * 8
            st["sprites"] = []
            st["contacts"] = set()
            self.canvas = st
            self.canvas_cid = c.comp_id
        elif t == "sprite":
            si = c.props.get(WK["frame"], (PK_SPRITE, 0))[1]
            spr = b.sprites[si] if b.sprites else dict(wb=1, h=8)
            st.update(x=g(WK["x"], 0), y=g(WK["y"], 0), vx=0, vy=0,
                      frame=0, shown=g(WK["shown"], 1), img=si,
                      pw=spr["wb"] * 8, ph=spr["h"], scored=False)
            st["px16"] = st["x"] * 16
            st["py16"] = st["y"] * 16
        return st

    # -- the interpreter (WEAVE-SPEC 4.5's semantics) --
    def invoke(self, fnidx, args):
        """One handler, to completion (WEAVE-SPEC 4.9). Raises ScriptError
        with the pinned sentence on a script error."""
        b = self.b
        try:
            ofs, nargs, nlocals = b.functions[fnidx]
        except IndexError:
            raise BundleError(b.name, "function index")
        args = (args + [(T_INT, 0)] * nargs)[:nargs]
        args = [a if isinstance(a, tuple) else (T_INT, wrap16(a))
                for a in args]
        stack = list(args) + [NULL] * (nlocals - nargs)
        frames = [[None, 0, fnidx, nlocals]]
        pc = ofs
        code = b.code
        ops = 0

        def serr(msg):
            raise ScriptError("Script error in fn %d: %s"
                              % (frames[-1][2], msg))

        def pop():
            return stack.pop()

        def push(v):
            if len(stack) >= 64:
                serr("too deep.")
            stack.append(v)

        def ints2():
            bv, av = pop(), pop()
            if av[0] != T_INT or bv[0] != T_INT:
                serr("type mismatch.")
            return av[1], bv[1]

        while True:
            ops += 1
            if ops > self.MAX_OPS:
                raise Runaway("fn %d still running after %d ops - the "
                              "90-tick alert (WEAVE-SPEC 4.11) would have "
                              "fired" % (frames[-1][2], ops))
            opc = code[pc]
            name, spec = OPS[opc]
            pc += 1
            if name == "HALT":
                return NULL
            elif name == "PUSHI":
                push((T_INT, struct.unpack_from("<h", code, pc)[0]))
                pc += 2
            elif name == "PUSHA":
                push((T_STR, b.atom_str(code[pc])))
                pc += 1
            elif name == "PUSHN":
                push(NULL)
            elif name == "PUSHB":
                push((T_BOOL, code[pc]))
                pc += 1
            elif name == "PUSHC":
                push((T_COMP, code[pc]))
                pc += 1
            elif name == "LDG":
                push(self.globals[code[pc]])
                pc += 1
            elif name == "STG":
                self.globals[code[pc]] = pop()
                pc += 1
            elif name == "LDL":
                push(stack[frames[-1][1] + code[pc]])
                pc += 1
            elif name == "STL":
                stack[frames[-1][1] + code[pc]] = pop()
                pc += 1
            elif name == "POP":
                pop()
            elif name == "DUP":
                push(stack[-1])
            elif name in ("ADD", "SUB", "MUL", "DIV", "MOD"):
                bv, av = pop(), pop()
                if name == "ADD" and av[0] == T_STR and bv[0] == T_STR:
                    s = av[1] + bv[1]
                    if len(s) > 255:
                        serr("out of string space.")
                    push((T_STR, s))
                elif av[0] == T_INT and bv[0] == T_INT:
                    a, d = av[1], bv[1]
                    if name == "ADD":
                        r = a + d
                    elif name == "SUB":
                        r = a - d
                    elif name == "MUL":
                        r = a * d
                    elif d == 0:
                        serr("divide by zero.")
                    elif name == "DIV":
                        r = abs(a) // abs(d)        # truncate toward zero
                        r = -r if (a < 0) != (d < 0) else r
                    else:                           # MOD: dividend's sign
                        r = abs(a) % abs(d)
                        r = -r if a < 0 else r
                    push((T_INT, wrap16(r)))
                else:
                    serr("type mismatch.")
            elif name == "NEG":
                av = pop()
                if av[0] != T_INT:
                    serr("type mismatch.")
                push((T_INT, wrap16(-av[1])))
            elif name in ("EQ", "NE"):
                bv, av = pop(), pop()
                if av[0] == T_ARR and bv[0] == T_ARR:
                    r = av[1] is bv[1]              # handle identity
                else:
                    r = av == bv
                push((T_BOOL, int(r if name == "EQ" else not r)))
            elif name in ("LT", "LE", "GT", "GE"):
                bv, av = pop(), pop()
                if not ((av[0] == T_INT and bv[0] == T_INT)
                        or (av[0] == T_STR and bv[0] == T_STR)):
                    serr("type mismatch.")
                r = {"LT": av[1] < bv[1], "LE": av[1] <= bv[1],
                     "GT": av[1] > bv[1], "GE": av[1] >= bv[1]}[name]
                push((T_BOOL, int(r)))
            elif name == "NOT":
                push((T_BOOL, int(not truthy(pop()))))
            elif name == "JMP":
                pc += 2 + struct.unpack_from("<h", code, pc)[0]
            elif name == "JZ":
                d = struct.unpack_from("<h", code, pc)[0]
                pc += 2 + (d if not truthy(pop()) else 0)
            elif name == "JNZ":
                d = struct.unpack_from("<h", code, pc)[0]
                pc += 2 + (d if truthy(pop()) else 0)
            elif name == "CALL":
                f = code[pc]
                pc += 1
                if len(frames) >= 16:
                    serr("too deep.")
                fofs, fargs, flocs = b.functions[f]
                base = len(stack) - fargs
                for _ in range(flocs - fargs):
                    push(NULL)
                frames.append([pc, base, f, flocs])
                pc = fofs
            elif name == "RET":
                v = pop()
                retpc, base, _, _ = frames.pop()
                del stack[base:]
                if retpc is None:
                    return v
                push(v)
                pc = retpc
            elif name == "GETP":
                av = pop()
                if av[0] != T_COMP:
                    serr("type mismatch.")
                push(self.getp(av[1], code[pc], serr))
                pc += 1
            elif name == "SETP":
                v, av = pop(), pop()
                if av[0] != T_COMP:
                    serr("type mismatch.")
                self.setp(av[1], code[pc], v, serr)
                pc += 1
            elif name == "CALLM":
                atom, argc = code[pc], code[pc + 1]
                pc += 2
                margs = [pop() for _ in range(argc)][::-1]
                av = pop()
                if av[0] != T_COMP:
                    serr("type mismatch.")
                push(self.callm(av[1], atom, margs, serr))
            elif name == "BUILT":
                bi, argc = code[pc], code[pc + 1]
                pc += 2
                bargs = [pop() for _ in range(argc)][::-1]
                push(self.builtin(bi, bargs, serr))
            elif name in ("INCG", "DECG"):
                g = code[pc]
                pc += 1
                cell = self.globals[g]
                if cell[0] != T_INT:
                    serr("type mismatch.")
                self.globals[g] = (T_INT, wrap16(
                    cell[1] + (1 if name == "INCG" else -1)))
            elif name == "AGET":
                iv, av = pop(), pop()
                if av[0] != T_ARR or iv[0] != T_INT:
                    serr("type mismatch.")
                if not 0 <= iv[1] < len(av[1]):
                    serr("array index %d of %d." % (iv[1], len(av[1])))
                push((T_INT, av[1][iv[1]]))
            elif name == "ASET":
                v, iv, av = pop(), pop(), pop()
                if av[0] != T_ARR or iv[0] != T_INT or v[0] != T_INT:
                    serr("type mismatch.")
                if not 0 <= iv[1] < len(av[1]):
                    serr("array index %d of %d." % (iv[1], len(av[1])))
                av[1][iv[1]] = v[1]
            else:
                serr("bad opcode.")

    # -- component surface (WEAVE-SPEC 6) --
    def _check(self, cid, atom, kind, serr):
        st = self.comps.get(cid) if cid else {"tag": "app"}
        if st is None:
            serr("no component %d." % cid)
        surf = SURFACE[st["tag"] if cid else "app"]
        if atom not in surf[kind] and not (
                kind == "methods" and atom in surf["methods"]):
            serr('no %s "%s" on a %s.'
                 % ("method" if kind == "methods" else "property",
                    WK_NAME.get(atom, atom), st["tag"] if cid else "app"))
        return st

    def getp(self, cid, atom, serr):
        st = self._check(cid, atom, "get", serr)
        name = WK_NAME[atom]
        if name == "hidden":
            return (T_INT, st["hidden"])
        v = st.get(name)
        if isinstance(v, str):
            return (T_STR, v)
        return (T_INT, wrap16(int(v)))

    def setp(self, cid, atom, v, serr):
        st = self._check(cid, atom, "set", serr)
        name = WK_NAME[atom]
        tag = st["tag"]
        if name in ("text", "label"):
            if v[0] != T_STR:
                serr("type mismatch.")
            st[name] = v[1]
            self.paint(cid, "%s.%s = \"%s\"" % (self.cname(cid), name,
                                                v[1]))
            return
        if v[0] not in (T_INT, T_BOOL):
            serr("type mismatch.")
        val = v[1]
        if name == "value" and tag == "meter":
            val = max(0, min(st["max"], val))       # clamped 0..max
            if val != st["value"]:
                st["value"] = val
                self.paint(cid, "%s.value = %d" % (self.cname(cid), val))
            return
        if name == "checked":
            val = 1 if val else 0
            if tag == "radio" and val:
                for ocid, ost in self.comps.items():
                    if ost["tag"] == "radio" and ocid != cid \
                            and ost.get("group") == st.get("group") \
                            and ost["checked"]:
                        ost["checked"] = 0
                        self.paint(ocid, "%s.checked = 0"
                                   % self.cname(ocid))
            st["checked"] = val
            self.paint(cid, "%s.checked = %d" % (self.cname(cid), val))
            return
        if name == "sel":
            if not -1 <= val < len(st["items"]):
                serr("list index %d of %d." % (val, len(st["items"])))
            st["sel"] = val             # programmatic: no onselect fires
            self.paint(cid, "%s.sel = %d" % (self.cname(cid), val))
            return
        if tag == "sprite" and name in ("x", "y"):
            st[name] = val
            st["p%s16" % name] = val * 16
            return
        if tag == "sprite" and name == "frame":
            spr = self.b.sprites[st["img"]]
            if not 0 <= val < spr["frames"]:
                serr("frame %d of %d." % (val, spr["frames"]))
        st[name] = val
        if name in ("hidden", "enabled"):
            self.paint(cid, "%s.%s = %d" % (self.cname(cid), name, val))

    def callm(self, cid, atom, args, serr):
        st = self._check(cid, atom, "methods", serr)
        name = WK_NAME[atom]
        tag = st["tag"] if cid else "app"
        if tag == "app":
            if name == "go":
                n = args[0][1]
                if args[0][0] != T_INT or not 1 <= n <= len(self.b.cards):
                    serr("card %s of %d." % (args[0][1],
                                             len(self.b.cards)))
                self.card = n
                self.paint(cid, "app.go(%d) - full-card repaint" % n)
                return NULL
        if tag == "list":
            if name == "get":
                i = args[0][1]
                if not 0 <= i < len(st["items"]):
                    serr("list index %d of %d." % (i, len(st["items"])))
                return (T_STR, st["items"][i])
            if name == "set":
                i, sv = args[0][1], args[1]
                if sv[0] != T_STR:
                    serr("type mismatch.")
                if not 0 <= i < len(st["items"]):
                    serr("list index %d of %d." % (i, len(st["items"])))
                st["items"][i] = sv[1]
                self.paint(cid, "%s.set(%d, \"%s\")" % (self.cname(cid),
                                                        i, sv[1]))
                return NULL
        if tag == "grid":
            g = st["grid"]
            if name in ("cell", "setCell", "select"):
                r, c = args[0][1] - 1, args[1][1] - 1   # 1-based in WJS
                if not (0 <= r < g.rows and 0 <= c < g.cols):
                    serr("grid cell %d,%d of %dx%d."
                         % (args[0][1], args[1][1], g.rows, g.cols))
            if name == "cell":
                v = g.read_cell(r, c)
                if v is None:
                    return (T_INT, 0)
                if v is FX_ERR:
                    serr("cell is #DIV0.")
                iv = v >> 16 if v >= 0 else -((-v) >> 16)   # truncation,
                if not -32768 <= iv <= 32767:               # WEAVE-SPEC 5.2
                    serr("cell %s is out of int range." % fmt_16_16(v))
                return (T_INT, iv)
            if name == "setCell":
                v = args[2]
                if v[0] == T_INT:
                    g.vals[(r, c)] = ["num", wrap32(v[1] << 16)]
                elif v[0] == T_STR:
                    g.vals[(r, c)] = ["label", v[1]]
                else:
                    serr("type mismatch.")
                self.grid_pending = True    # triggers collapse to one
                return NULL
            if name == "select":
                self.gselect(cid, r + 1, c + 1)   # 6.9.4: one body for the
                self.paint(cid, "%s.select(%d, %d)"     # click, the arrow key
                           % (self.cname(cid), r + 1, c + 1))   # and this
                return NULL
            if name == "recalc":
                self.grid_pending = True
                return NULL
            if name == "clear":
                for k in [k for k, v in g.vals.items()
                          if v[0] != "formula"]:
                    del g.vals[k]
                self.grid_pending = True
                return NULL
        if tag == "canvas":
            if name == "start":
                fps = args[0][1]
                if args[0][0] != T_INT or not 1 <= fps <= 18:
                    serr("start(%s): fps is 1..18." % args[0][1])
                st["running"] = True
                st["sleep"] = max(1, int(18.2 / fps + 0.5))
                st["phase"] = 0
                self.log("canvas start(%d) - worker hired, one frame per "
                         "%d tick(s)" % (fps, st["sleep"]))
                return NULL
            if name == "stop":
                st["running"] = False
                self.log("canvas stop() - worker released")
                return NULL
        serr("no method.")

    # -- builtins (WEAVE-SPEC 8) --
    def builtin(self, bi, args, serr):
        name = BUILTINS[bi][0]
        if name == "alert":
            if args[0][0] != T_STR:
                serr("type mismatch.")
            self.log('alert "%s"' % args[0][1])
            if len(args) > 1:           # callback -> a later onalert event
                self.ring.enqueue(0, WK["onalert"], args[1][1], 1)
            return NULL
        if name == "timer":
            if args[0][0] != T_INT:
                serr("type mismatch.")
            self.timers.append([self.now + max(1, args[0][1]),
                                args[1][1]])
            return NULL
        if name == "saveState":
            return (T_BOOL, int(self.save_state()))
        if name == "loadState":
            return (T_BOOL, int(self.load_state()))
        if name == "playSound":
            self.log("playSound refused - v1 carries no clips "
                     "(WEAVE-SPEC 8.4)")
            return NULL
        if name == "tone":
            if args[0][0] != T_INT or args[1][0] != T_INT:
                serr("type mismatch.")
            self.log("tone %d,%d" % (args[0][1], args[1][1]))
            return NULL
        if name == "str":
            t, v = args[0]
            if t == T_INT:
                return (T_STR, "%d" % v)
            if t == T_STR:
                return args[0]
            if t == T_BOOL:
                return (T_STR, "true" if v else "false")
            if t == T_NULL:
                return (T_STR, "null")
            serr("type mismatch.")
        if name == "len":
            t, v = args[0]
            if t not in (T_STR, T_ARR):
                serr("type mismatch.")
            return (T_INT, len(v))
        if name == "substr":
            s, st_, ln = args
            if s[0] != T_STR or st_[0] != T_INT or ln[0] != T_INT:
                serr("type mismatch.")
            a = max(0, st_[1])
            return (T_STR, s[1][a:a + max(0, ln[1])])
        if name == "find":
            s, n = args
            if s[0] != T_STR or n[0] != T_STR:
                serr("type mismatch.")
            return (T_INT, s[1].find(n[1]))
        if name == "rand":
            if args[0][0] != T_INT or args[0][1] < 1:
                serr("rand of %s." % args[0][1])
            self.rand_seed = (self.rand_seed * 25173 + 13849) & 0xFFFF
            return (T_INT, self.rand_seed % args[0][1])
        if name == "array":
            n = args[0][1]
            return (T_ARR, [0] * n)
        serr("bad builtin.")

    def paint(self, cid, what):
        self.gfx_calls += 1
        self.log("  " + what)

    # -- state (WEAVE-SPEC 8.3) --
    def save_state(self):
        out = bytearray(b"WSV\x1a")
        out += struct.pack("<H", 1)
        for t, v in self.globals:
            out += struct.pack("<H", t)
            if t == T_STR:
                out += bytes([len(v)]) + v.encode("ascii")
            elif t == T_ARR:
                out += struct.pack("<H", len(v))
                for x in v:
                    out += struct.pack("<h", x)
            else:
                out += struct.pack("<h", v if t != T_COMP else v)
        self.sav_mem = bytes(out)
        if self.sav_path:
            try:
                open(self.sav_path, "wb").write(self.sav_mem)
            except OSError:
                return False
        self.log("saveState -> true (%d bytes)" % len(self.sav_mem))
        return True

    def load_state(self):
        raw = self.sav_mem
        if self.sav_path and os.path.exists(self.sav_path):
            raw = open(self.sav_path, "rb").read()
        if not raw or raw[:4] != b"WSV\x1a":
            self.log("loadState -> false")
            return False
        i = 6
        cells = []
        for _ in range(128):
            t = struct.unpack_from("<H", raw, i)[0]
            i += 2
            if t == T_STR:
                ln = raw[i]
                cells.append((T_STR, raw[i + 1:i + 1 + ln].decode("ascii")))
                i += 1 + ln
            elif t == T_ARR:
                n = struct.unpack_from("<H", raw, i)[0]
                i += 2
                cells.append((T_ARR, list(struct.unpack_from(
                    "<%dh" % n, raw, i))))
                i += 2 * n
            elif t == T_COMP:
                v = struct.unpack_from("<h", raw, i)[0]
                i += 2
                # a handle that no longer resolves loads as null
                cells.append((T_COMP, v) if v in self.comps or v == 0
                             else NULL)
            else:
                v = struct.unpack_from("<h", raw, i)[0]
                i += 2
                cells.append((t, v))
        self.globals = cells
        self.log("loadState -> true")
        return True

    # -- events (WEAVE-SPEC 4.9) --
    def dispatch(self, rec):
        cid, atom, d1, d2 = rec
        if atom in (WK["ontimer"], WK["onalert"]):
            # invoke the function named by data1 (WEAVE-SPEC 4.9.1)
            fnidx = d1
            args = [(T_INT, d2)] if atom == WK["onalert"] else []
            self.log("event %s -> fn %d" % (WK_NAME[atom], fnidx))
            self.run_handler(fnidx, args)
            return
        if atom == WK["oncommand"]:
            fn = self.menu_fn(d1, d2)
            if fn is not None and fn != 0xFF:
                self.log("event oncommand menu %d item %d -> fn %d"
                         % (d1, d2, fn))
                self.run_handler(fn, [(T_INT, d1), (T_INT, d2)])
            return
        st = self.comps.get(cid)
        fn = st["handlers"].get(atom) if st else None
        if fn is None:
            return                      # no binding: discarded
        self.log("event %s %s -> fn %d" % (WK_NAME[atom],
                                           self.cname(cid), fn))
        n = EVENT_NARGS.get(atom, 2)
        args = [(T_INT, wrap16(d1)), (T_INT, wrap16(d2))][:n]
        self.run_handler(fn, args)

    def run_handler(self, fnidx, args):
        try:
            self.invoke(fnidx, args)
        except ScriptError as ex:
            # handler stopped, stacks cleared, ring kept, app lives on
            self.log(str(ex))
    def drain(self):
        """Handlers and 5.5's recalculation, in the runtime's own priority.

        THE RECALCULATION IS NOT A HANDLER'S TAIL, and wave 3's model had it
        as one - `run_handler` ran it after the script returned. That is right
        for `setCell()` and `recalc()` and WRONG for everything else: a cell
        committed from the formula bar (6.9.3) sets the trigger with no
        handler in sight, and on a bundle that binds no `onedit` the record is
        discarded and the passes never ran at all. The sheet then showed
        stale values with no error anywhere - found by tests/weavegrid.py's
        first green run, where the MACHINE had recalculated and the model had
        not.

        The order is wevent.c's w_wake to the letter: a pending walk runs
        BEFORE the next record is dequeued, because that is where w_gbusy()
        is tested."""
        while True:
            if self.grid and self.grid_pending:
                self.grid_pending = False
                changed = self.grid.recalc()
                self.log("recalc: %d cell(s) changed" % changed)
                self.ring.enqueue(self.grid_cid, WK["oncalc"], changed, 0)
                continue
            if not self.ring.q:
                return
            self.dispatch(self.ring.q.pop(0))

    def tick(self, n=1):
        """Advance the 18.2 Hz clock: timers fire, the canvas worker frames
        (WEAVE-SPEC 6.10), the ring drains after every tick."""
        for _ in range(n):
            self.now += 1
            for t in [t for t in self.timers if t[0] <= self.now]:
                self.timers.remove(t)
                self.ring.enqueue(0, WK["ontimer"], t[1], 0)
            cv = self.canvas
            if cv and cv["running"]:
                cv["phase"] += 1
                if cv["phase"] >= cv["sleep"]:
                    cv["phase"] = 0
                    self.canvas_frame()
            self.drain()

    def canvas_frame(self):
        cv = self.canvas
        cv["frame"] += 1
        W, H, walls = cv["w"], cv["h"], cv["walls"]
        shown = []
        for cid in cv["sprites"]:
            s = self.comps[cid]
            if not s["shown"]:
                continue
            shown.append(cid)
            outedge = -1
            # sub-pixel velocities: 1/16 px per frame, remainders kept
            s["px16"] += s["vx"]
            s["py16"] += s["vy"]
            s["x"] = s["px16"] >> 4
            s["y"] = s["py16"] >> 4
            for edge, hit, wallbit in (
                    (0, s["y"] < 0, 1),                      # T
                    (1, s["y"] + s["ph"] > H, 2),            # B
                    (2, s["x"] < 0, 4),                      # L
                    (3, s["x"] + s["pw"] > W, 8)):           # R
                if not hit:
                    continue
                if walls & wallbit:
                    # bounce: reposition inside, negate the component
                    if edge == 0:
                        s["py16"] = -s["py16"]
                    elif edge == 1:
                        s["py16"] = 2 * (H - s["ph"]) * 16 - s["py16"]
                    elif edge == 2:
                        s["px16"] = -s["px16"]
                    else:
                        s["px16"] = 2 * (W - s["pw"]) * 16 - s["px16"]
                    if edge < 2:
                        s["vy"] = -s["vy"]
                    else:
                        s["vx"] = -s["vx"]
                    s["x"], s["y"] = s["px16"] >> 4, s["py16"] >> 4
                    self.ring.enqueue(self.canvas_cid, WK["onwall"],
                                      cid, edge)
                elif outedge < 0:
                    # fully out an open edge: stop, onscore
                    out = ((edge == 0 and s["y"] + s["ph"] < 0)
                           or (edge == 1 and s["y"] > H)
                           or (edge == 2 and s["x"] + s["pw"] < 0)
                           or (edge == 3 and s["x"] > W))
                    if out:
                        outedge = edge
            # WEAVE-SPEC 6.10.1: onscore fires ONCE per exit and RE-ARMS the
            # frame the sprite is no longer fully out of any open edge.  The
            # latch used to be permanent, which made PONG score exactly one
            # goal per launch - doServe() put the ball back and it could never
            # score again.  An event that fires "once per contact" needs a
            # definition of leaving the contact, and 6.10 already had one for
            # collisions; this is that sentence said about an edge.
            if outedge < 0:
                s["scored"] = False
            elif not s["scored"]:
                s["scored"] = True
                s["vx"] = s["vy"] = 0
                self.ring.enqueue(self.canvas_cid, WK["onscore"],
                                  cid, outedge)
        # AABB collision, once per contact, re-armed on separation - and a
        # pair either of whose sprites is not shown counts as SEPARATED
        # (6.10.1).  Walking `shown` alone left such a pair latched forever:
        # hide a sprite mid-contact, unhide it, and it never collides again.
        sprs = cv["sprites"]
        for i in range(len(sprs)):
            for j in range(i + 1, len(sprs)):
                a, bb = self.comps[sprs[i]], self.comps[sprs[j]]
                overlap = (a["shown"] and bb["shown"]
                           and a["x"] < bb["x"] + bb["pw"]
                           and bb["x"] < a["x"] + a["pw"]
                           and a["y"] < bb["y"] + bb["ph"]
                           and bb["y"] < a["y"] + a["ph"])
                pair = (sprs[i], sprs[j])
                if overlap and pair not in cv["contacts"]:
                    cv["contacts"].add(pair)
                    self.ring.enqueue(self.canvas_cid, WK["oncollide"],
                                      pair[0], pair[1])
                elif not overlap:
                    cv["contacts"].discard(pair)
        if cv["tick"] and cv["frame"] % cv["tick"] == 0:
            self.ring.enqueue(self.canvas_cid, WK["ontick"],
                              cv["frame"] & 0xFFFF, 0)

    # -- user gestures, as the scripted event file drives them --
    def grid_rect(self):
        """The grid's laid-out (w, h) in cells on `self.adapter` - the walk's
        own answer, so a scroll clamp and a picture cannot disagree about how
        much of the sheet is on screen."""
        if self._grect is None:
            self._grect = (1, 1)
            for p in flow_walk(self, self.adapter)[0]:
                if p.comp.tag == "grid":
                    self._grect = (p.w, p.h)
        return self._grect

    def gselect(self, cid, r1, c1):
        """WEAVE-SPEC 6.9.4: move the selection to (r1, c1), 1-based, scroll
        by the MINIMUM that keeps it visible, reload the bar, and enqueue
        onselect exactly once.  One body for the click, the arrow key and
        `select()` - three copies of a scroll clamp is three places to get
        the minimum wrong in."""
        st = self.comps[cid]
        g = st["grid"]
        st["selrow"], st["selcol"] = r1, c1
        r, c = r1 - 1, c1 - 1
        gw, gh = self.grid_rect()
        vc, vr = grid_geom(g.cols, g.rows, gw, gh)
        if r < g.top:
            g.top = r
        elif vr and r >= g.top + vr:
            g.top = r - vr + 1
        if c < g.left:
            g.left = c
        elif c >= g.left + vc:
            g.left = c - vc + 1
        st["barsrc"] = g.source(r, c)
        self.ring.enqueue(cid, WK["onselect"], r1, c1)

    def gesture(self, verb, target, a1="", a2="", a3=""):
        cid = None
        if target:
            if target.startswith("#"):
                cid = int(target[1:])
            elif target in self.idmap:
                cid = self.idmap[target]
            elif verb not in ("tick", "dump", "command"):
                raise SystemExit("run: no component '%s' (no id map - "
                                 "pass --src)" % target)
        st = self.comps.get(cid)
        if verb == "click":
            if st["tag"] == "button":
                if st["enabled"] and not st["hidden"]:
                    self.paint(cid, "%s pressed+released"
                               % self.cname(cid))
                    self.ring.enqueue(cid, WK["onclick"])
                else:
                    self.log("%s: disabled - the SPEC.md 47 pen refuses "
                             "the click" % self.cname(cid))
            elif st["tag"] in ("check", "radio"):
                self.setp(cid, WK["checked"],
                          (T_INT, 0 if st["checked"] else 1),
                          self._gerr)
                self.ring.enqueue(cid, WK["onchange"], st["checked"], 0)
            else:
                raise SystemExit("run: click on a %s" % st["tag"])
        elif verb == "set":
            st["text"] = a1
        elif verb == "change":
            if st["tag"] == "input":
                if a1:
                    st["text"] = a1
                self.ring.enqueue(cid, WK["onchange"], 0, 0)
            else:
                self.ring.enqueue(cid, WK["onchange"], int(a1 or 0), 0)
        elif verb == "key":
            ch = ord(a1[0]) if a1 and not a1.isdigit() else int(a1 or 0)
            self.ring.enqueue(cid, WK["onkey"], ch,
                              int(a2 or (1 if st["tag"] == "canvas"
                                         else 0)))
        elif verb == "select":
            if st["tag"] == "list":
                st["sel"] = int(a1)
                self.ring.enqueue(cid, WK["onselect"], int(a1), 0)
            else:
                self.gselect(cid, int(a1), int(a2))
        elif verb == "edit":
            # 6.9.3: the bar's text is CLASSIFIED and committed, then onedit
            # and 5.5's recalculation.  A gesture that only enqueued the
            # event would test the ring and nothing the grid does.
            r, c = int(a1) - 1, int(a2) - 1
            msg = st["grid"].commit(r, c, a3)
            if msg:
                self.log("Formula: %s" % msg)
            else:
                st["barsrc"] = st["grid"].source(r, c)
                self.ring.enqueue(cid, WK["onedit"], r + 1, c + 1)
                self.grid_pending = True
        elif verb == "command":
            mi, ii = int(target), int(a1)
            if self.menu_fn(mi, ii) is None:
                raise SystemExit("run: no menu %d item %d" % (mi, ii))
            self.ring.enqueue(0, WK["oncommand"], mi, ii)
        self.drain()

    def menu_fn(self, mi, ii):
        """The oncommand function index of menu mi item ii (1-based), from
        the MENUS blob (WEAVE-SPEC 2.6.2); None when absent."""
        blob = self.b.app_props.get(WK["MENUS"])
        if not blob:
            return None
        mb = self.b.blob(blob[1])
        pos = 1
        for m in range(mb[0]):
            nitems = mb[pos + 1]
            if m + 1 == mi and 1 <= ii <= nitems:
                return mb[pos + 2 + 2 * (ii - 1) + 1]
            pos += 2 + 2 * nitems
        return None

    def _gerr(self, msg):
        raise ScriptError("Script error: " + msg)


# --- the flow walk (WEAVE-SPEC 7) --------------------------------------------
class Placed:
    __slots__ = ("comp", "x", "y", "w", "h")

    def __init__(self, comp, x, y, w, h):
        self.comp, self.x, self.y, self.w, self.h = comp, x, y, w, h


def natural_w(rt, comp, cw):
    """WEAVE-SPEC 7.3's table. rt is a Runtime (for live prop values)."""
    st = rt.comps[comp.comp_id]
    t = comp.tag
    if t == "label":
        return max(1, len(st["text"]))
    if t in ("text", "rule", "grid"):
        return cw
    if t in ("box", "spacer"):
        return comp.w
    if t == "meter":
        return 10
    if t == "button":
        return len(st["label"]) + 2
    if t in ("check", "radio"):
        return len(st["label"]) + 2
    if t == "input":
        return st["cols"] + 2
    if t == "list":
        longest = max((len(s) for s in st["items"]), default=1)
        return longest + 3              # + scroll bar
    if t == "canvas":
        return comp.w                   # already w px / 8
    return 1


def natural_h(rt, comp, w, cw, ch, y):
    st = rt.comps[comp.comp_id]
    t = comp.tag
    if t in ("label", "rule", "spacer", "meter"):
        return 1
    if t == "text":
        return max(1, len(htmsim.wrap(st["text"].split(), max(1, w))))
    if t == "box":
        return comp.h
    if t in ("button", "check", "radio", "input"):
        return 2
    if t == "list":
        return st["rows"]
    if t == "grid":
        return max(6, ch - y)           # CH - consumed rows above, min 6
    if t == "canvas":
        return comp.h
    return 1


def flow_walk(rt, adapter, card=None):
    """The normative walk (WEAVE-SPEC 7.2): one pass, deterministic; hidden
    components still take part. Returns ([Placed], total_rows)."""
    A = ADAPTERS[adapter]
    cw, ch = A["cw"], A["ch"]
    card = card or rt.b.entry
    comps = [c for c in rt.b.cards[card - 1] if c.tag != "sprite"]
    rows, cur, x = [], [], 0
    for c in comps:
        w = c.w if c.w > 0 else natural_w(rt, c, cw)
        w = min(w, cw)
        if (c.cflags & CF_BREAK) or (x > 0 and x + w > cw):
            if cur:                     # closing an EMPTY row is a no-op
                rows.append(cur)        # (WEAVE-SPEC 7.2): a CF_BREAK on a
                cur = []                # card's first component is already
            x = 0                       # at the start of a row
        cur.append((c, x, w))
        x += w + 1                      # one gutter cell
    if cur:
        rows.append(cur)
    placed, y = [], 0
    for row in rows:
        heights = []
        for c, x, w in row:
            h = c.h if c.h > 0 else natural_h(rt, c, w, cw, ch, y)
            heights.append(h)
        rh = max(heights)
        n = len(row)
        total = sum(w for _, _, w in row) + (n - 1)
        slack = max(0, cw - total)
        align = (row[0][0].style >> 2) & 3      # the row's FIRST component
        shift = 0 if align == 0 else (slack // 2 if align == 1 else slack)
        for (c, x, w), h in zip(row, heights):
            placed.append(Placed(c, x + shift, y, w, h))
        y += rh                         # rows abut; components top-align
    return placed, y


def render(rt, adapter, card=None, out=print):
    """--render: the walk drawn as text, one char per cell, one line per
    8-px row - the htmsim --render shape."""
    A = ADAPTERS[adapter]
    cw, ch = A["cw"], A["ch"]
    placed, total = flow_walk(rt, adapter, card)
    grid_h = max(total, 1)
    canvas = [[" "] * cw for _ in range(grid_h)]

    def put(x, y, s):
        for k, chr_ in enumerate(s):
            if 0 <= x + k < cw and 0 <= y < grid_h:
                canvas[y][x + k] = chr_

    for p in placed:
        st = rt.comps[p.comp.comp_id]
        t = p.comp.tag
        if st["hidden"]:
            continue                    # hidden: takes part, draws nothing
        if t == "label":
            s = st["text"][:p.w]
            if p.comp.style & ST_INVERT:
                s = "[%s]" % s[:max(0, p.w - 2)]
            put(p.x, p.y, s)
        elif t == "text":
            for k, line in enumerate(htmsim.wrap(st["text"].split(),
                                                 max(1, p.w))[:p.h]):
                put(p.x, p.y + k, line)
        elif t == "rule":
            put(p.x, p.y, "-" * p.w)
        elif t == "box":
            put(p.x, p.y, "+" + "-" * (p.w - 2) + "+")
            for k in range(1, p.h - 1):
                put(p.x, p.y + k, "|" + " " * (p.w - 2) + "|")
            put(p.x, p.y + p.h - 1, "+" + "-" * (p.w - 2) + "+")
        elif t == "meter":
            fill = 0 if st["max"] == 0 else \
                (p.w - 2) * st["value"] // st["max"]
            put(p.x, p.y, "[" + "#" * fill + "-" * (p.w - 2 - fill) + "]")
        elif t == "button":
            lbl = st["label"][:p.w - 2]
            pad = p.w - 2 - len(lbl)
            put(p.x, p.y, "+" + "-" * (p.w - 2) + "+")
            put(p.x, p.y + 1, "|" + lbl + " " * pad + "|")
        elif t in ("check", "radio"):
            mark = ("X " if st["checked"] else "_ ") if t == "check" \
                else ("O " if st["checked"] else "o ")
            put(p.x, p.y, mark + st["label"][:p.w - 2])
        elif t == "input":
            put(p.x, p.y, "[" + st["text"][:p.w - 2].ljust(p.w - 2, "_")
                + "]")
        elif t == "list":
            for k in range(p.h):
                if k < len(st["items"]):
                    s = st["items"][k][:p.w - 2]
                    row = ("> " if k == st["sel"] else "  ") + s
                else:
                    row = ""
                put(p.x, p.y + k, row.ljust(p.w - 1) + "|")
        elif t == "grid":
            # WEAVE-SPEC 6.9.1, and the 8086 draws these same characters:
            # the formula bar, the header band, then one data band a row.
            g = st["grid"]
            _, vr = grid_geom(g.cols, g.rows, p.w, p.h)
            put(p.x, p.y, "[" + st["barsrc"][:p.w - 2].ljust(p.w - 2, "_")
                + "]")
            put(p.x, p.y + WG_BAR, g.band(p.w, p.h, -1))
            for k in range(vr):
                put(p.x, p.y + WG_BAR + WG_HDR + k, g.band(p.w, p.h, k))
        elif t == "canvas":
            put(p.x, p.y, "+" + "-" * (p.w - 2) + "+")
            for k in range(1, p.h - 1):
                put(p.x, p.y + k, "|" + " " * (p.w - 2) + "|")
            put(p.x, p.y + p.h - 1, "+" + "-" * (p.w - 2) + "+")
            for scid in st["sprites"]:
                s = rt.comps[scid]
                if s["shown"]:
                    put(p.x + max(0, min(p.w - 1, s["x"] // 8)),
                        p.y + max(0, min(p.h - 1, s["y"] // 8)), "o")
    out("%s  card %d/%d  %s  content %dx%d cells"
        % (rt.b.app_name, card or rt.b.entry, len(rt.b.cards),
           A["name"], cw, ch))
    out("+" + "-" * cw + "+")
    for i, row in enumerate(canvas):
        mark = "|" if i < ch else ":"       # beyond the window clips
        out(mark + "".join(row) + mark)
    out("+" + "-" * cw + "+")
    return placed


# --- the cost model (WEAVE-SPEC 14) ------------------------------------------
def first_paint_us(adapter, comps=None):
    """What OPENING a card costs, which WEAVE-SPEC 14 priced nowhere: every
    other row of that table is an INTERACTION, and paint() counts only
    mutations, so the one spend wave 2's gate actually measures had no
    number.  One gfx call per painted component plus its cells:
    sum(CALL_US + cells x cell_us).  With no component list, the worst
    case - a fully lettered card, one component per content row, every
    cell of the content area a glyph."""
    A = ADAPTERS[adapter]
    if comps is None:
        comps = [A["cw"]] * A["ch"]
    return sum(CALL_US + cells * A["cell_us"] for cells in comps)


def costs_table(adapter="cga"):
    """WEAVE-SPEC 14's rows, computed from the measured constants. The
    appendix is regenerated FROM this function - the model owns the
    numbers; field figures land on the 5150 and supersede them row by row."""
    cell = GLYPH_US                 # the headline ~900 us/glyph figure;
    #                                 per-adapter exacts are ADAPTERS[]
    ms = lambda us: us / 1000.0
    band_row = lambda cells: ms(BAND_CALL_US + cells * BAND_CELL_US)
    rows = [
        ("label", ".text set (20 cells)", "1",
         "~%.0f ms" % ms(CALL_US + 20 * cell)),
        ("text", "repaint (per wrapped row, 40 cells)", "1/row",
         "~%.0f ms/row" % ms(CALL_US + 40 * cell)),
        ("rule / box / spacer", "card paint", "1 / 1 / 0",
         "~%.1f ms" % ms(CALL_US)),
        ("meter", ".value delta", "1", "~0.8-1 ms"),
        ("button", "press+release", "~2 + label",
         "~%.1f-%.0f ms" % (ms(2 * CALL_US), ms(2 * CALL_US + 8 * cell))),
        ("check / radio", "toggle (one glyph)", "1",
         "%d-%d ms (field)" % GLYPH_TOGGLE_MS),
        ("input", "keystroke", "~2 cells", "~%.1f ms" % KEYSTROKE_MS),
        ("list", "selection move", "2 (XOR)",
         "~%.1f ms" % ms(2 * CALL_US + 0.5 * CALL_US / 4)),
        ("list", "scroll one line", "2", "~%d-%d ms" % LIST_SCROLL_MS),
        ("grid", "edit one cell (compose+blit 1 row)", "1",
         "~%.0f-%.0f ms" % (band_row(13), band_row(24))),
        ("grid", "selection move (2 XOR rects)", "2",
         "~%.1f ms" % ms(2 * CALL_US)),
        ("grid", "%d-cell row compose+blit" % ADAPTERS["cga"]["cw"], "1",
         "~%.1f ms" % band_row(ADAPTERS["cga"]["cw"])),
        ("grid", "full 20-row page", "20",
         "~%.0f ms" % (20 * band_row(ADAPTERS["cga"]["cw"]))),
        ("grid", "scroll one row (GFX_SCROLL + 1 composed band)", "2",
         "~%d-%d ms" % LIST_SCROLL_MS),
        ("canvas", "frame, 1 moving sprite (one dirty run)", "1-2",
         "~%.0f-%.0f ms" % (ms(1 * CALL_US) + 0.3, ms(2 * CALL_US) + 1)),
        ("canvas", "frame, 2 sprites (dirty bands)", "2-4",
         "~%.0f-%.0f ms" % (ms(2 * CALL_US) + 0.5, ms(4 * CALL_US) + 2)),
        ("card", "switch (full-card repaint, text-heavy CGA card)",
         "~1/row", "~0.3-1.2 s"),
    ]
    # The first paint, worst case, per adapter - the row wave 2's gate is.
    for ad in ("cga", "herc", "vga"):
        A = ADAPTERS[ad]
        rows.append(("card", "first paint, fully lettered %s (%d rows x "
                     "%d cells)" % (A["name"], A["ch"], A["cw"]),
                     "%d" % A["ch"],
                     "~%.2f s" % (first_paint_us(ad) / 1e6)))
    rows += [
        ("alert", "raise + dismiss", "~8",
         "~%.0f-%.0f ms" % (ms(8 * CALL_US) + 24, ms(8 * CALL_US) + 34)),
    ]
    return rows


def print_costs(adapter):
    A = ADAPTERS[adapter]
    print("Weave component cost table (WEAVE-SPEC 14) - the model's, "
          "regenerated; do not edit by hand")
    print("constants: %d us fixed per gfx call, ~%d us/glyph cell "
          "(%s exact: %d us)" % (CALL_US, GLYPH_US, A["name"],
                                 A["cell_us"]))
    print("           band composer %d us/call + %d us/cell "
          "(PERFORMANCE.md Set 68, confirmed for wband.inc by Set 113 at "
          "915/162)" % (BAND_CALL_US, BAND_CELL_US))
    print("           field rows carried as measured: glyph toggle "
          "%d-%d ms, list scroll %d-%d ms/line,"
          % (GLYPH_TOGGLE_MS + LIST_SCROLL_MS))
    print("           keystroke ~%.1f ms (the Note Pad contract, "
          "SPEC.md 27.2)" % KEYSTROKE_MS)
    print()
    print("| component | interaction | gfx calls | modelled cost |")
    print("|---|---|---|---|")
    for comp, inter, calls, cost in costs_table(adapter):
        print("| %s | %s | %s | %s |" % (comp, inter, calls, cost))
    print()
    print("A change that moves a row of this table upward is a regression "
          "against a documented")
    print("number, not a neutral refactor - PERFORMANCE.md Part 5's "
          "discipline as a table.")


# --- the generators (WEAVE-SPEC 12.1) ----------------------------------------
def emit_optab():
    """The 8086 dispatch table for wvm.inc - 38 entries, generated so the
    model and the core cannot drift (the --emit-l1tab precedent)."""
    out = ["; WVM dispatch table - 38 entries, 0x00-0x25 (WEAVE-SPEC 4.5).",
           "; Generated by `python3 tools/weavesim.py --emit-optab`.",
           "; Do not edit - regenerate, so the model and the 8086 core",
           "; cannot drift. Dispatch is the rcz80 shape:",
           ";   xor bh,bh / mov bl,[si] / inc si / shl bx,1",
           ";   jmp [cs:bx+wvm_tab]",
           "wvm_tab:"]
    opnd = {"": "-", "b": "b8", "w": "imm16", "r": "rel16", "bb": "b8,b8"}
    for i, (name, spec) in enumerate(OPS):
        out.append("    dw wvm_%-8s ; 0x%02X %-5s %s"
                   % (name.lower(), i, name, opnd[spec]))
    out.append("wvm_tab_end:")
    out.append("; wvm_tab_end - wvm_tab = %d = 2 x %d" % (2 * len(OPS),
                                                          len(OPS)))
    return "\n".join(out)


def emit_foldtab():
    """The Latin-1 fold for the WML/WJS text path, from htmsim's ONE
    definition (WEAVE-SPEC 3.1) - the same table the browser generates as
    br_l1tab, relabelled for the Loom compilers."""
    return htmsim.emit_l1tab().replace("br_l1tab:", "wv_foldtab:")


def emit_foldtab_c():
    """The same 128 bytes, as a C initialiser for apps/loom's compilers
    (WEAVE-SPEC 3.1, 12.1). LOOM's WML and WJS scanners are C in an overlay
    and a C file cannot name an nasm table, so the ONE definition is emitted
    twice from the same source rather than copied once by hand - which is the
    whole of why --emit-optab exists and is the same argument said about a
    different consumer."""
    out = ["/* The Latin-1 fold, 0x80..0xFF (WEAVE-SPEC 3.1). GENERATED by",
           " * `python3 tools/weavesim.py --emit-foldtab-c` out of",
           " * tools/htmsim.py's ONE definition - do not edit, regenerate.",
           " * A zero entry means DROP THE BYTE (lm_fold answers -1). */",
           "static const unsigned char lm_foldtab[128] = {"]
    for i in range(0x80, 0x100, 16):
        row = []
        for cp in range(i, i + 16):
            f = fold_text(chr(cp))
            row.append("0x%02X," % (ord(f) if f else 0))
        out.append("    " + " ".join(row))
    out.append("};")
    return "\n".join(out)


# --- the differential corpus (WEAVE-SPEC 12.1.1) -----------------------------
#
# The WVM's end state, per case, in a form apps/weave/hosttest/weavevm.asm can
# assemble beside the SHIPPING wvm.inc and compare on an 8086 in raw QEMU.
#
# What is compared is WEAVE-SPEC 8.3's SERIALIZED GLOBALS - the bytes
# saveState writes - and not a transcript, for two reasons that are both about
# what a comparison can mean. The image is handle-free by construction
# (strings are flattened, arrays counted), so the model - which has no handle
# table at all - and the machine, which has nothing else, describe the same
# thing. And the machine already has to produce those bytes for 8.3, so the
# gate exercises shipping code rather than a routine written for it.
#
# The two rules the c64cputest gate learned, kept: the model visits the cases
# in the harness's order (sorted by file name, and the harness walks the table
# it is handed), and the corpus carries NEGATIVE CONTROLS - a row whose
# expected state is deliberately one byte wrong, which the harness must FAIL.

# 10.6.1's sentences, as the codes wvm.inc raises. Only the seven the CORE
# owns appear here; the rest are the runtime's and are weavesession's.
VMC_ERRC = [("type mismatch.", 0), ("divide by zero.", 1),
            ("out of string space.", 2), ("too deep.", 3),
            ("array index ", 4), ("bad opcode.", 5), ("bad builtin.", 6)]
VMC_OK = 0xFFFF                 # the row is expected to run to completion


def _vmc_errcode(msg):
    for text, code in VMC_ERRC:
        if text in msg:
            return code
    raise SystemExit("--emit-vmcorpus: '%s' is not one of the core's own "
                     "sentences (WEAVE-SPEC 10.6.1) - a case that reaches a "
                     "runtime error belongs in weavesession, not here" % msg)


def _vmc_case(tmp, path):
    """Compile one corpus .wjs and run it on the model's WVM.
    Answers (name, bundle, init fn index, entry fn index, end-state bytes,
    expected error code)."""
    src = open(path, "r").read()
    first = src.split("\n", 1)[0]
    name = first.lstrip("/ ").strip() or os.path.basename(path)
    stem = "C"
    proj = os.path.join(tmp, os.path.basename(path)[:-4])
    os.makedirs(proj, exist_ok=True)
    # The smallest legal app that can carry a script: one card, one label.
    # Components are deliberately absent - a corpus case that touched one
    # would need wvm_native, and the harness has no runtime behind it.
    open(os.path.join(proj, stem + ".WML"), "w").write(
        '<app name="VMC"><card id="m"><label>x</label></card>'
        '<script src="%s.WJS"/></app>' % stem)
    open(os.path.join(proj, stem + ".WJS"), "w").write(src)
    res = pack_project(os.path.join(proj, stem + ".WML"))
    b = Bundle(res.data, "VMC.WAB")
    init = b.app_props.get(WK["start"])
    init_fn = init[1] if init and init[0] == PK_FUNC else 0xFFFF
    if "main" not in res.prog.fnindex:
        raise SystemExit("%s: a corpus case needs a function main()" % path)
    entry = res.prog.fnindex["main"]
    rt = Runtime(b)                     # ...which runs the init function
    err = VMC_OK
    try:
        rt.invoke(entry, [])
    except ScriptError as ex:
        err = _vmc_errcode(str(ex))
    rt.save_state()
    return name, b, init_fn, entry, rt.sav_mem, err


def _vmc_ring(path):
    """The ring-policy cases (WEAVE-SPEC 4.9), driven through the model's own
    Ring so the ORACLE is the policy rather than a second reading of it."""
    cases = []
    ops, name = None, None
    for line in open(path):
        line = line.split("#", 1)[0].split()
        if not line:
            continue
        if line[0] == "case":
            if ops is not None:
                cases.append((name, ops))
            name, ops = line[1], []
        elif line[0] in ("enq", "deq"):
            ops.append([line[0]] + [int(x) for x in line[1:]])
        else:
            raise SystemExit("%s: no ring verb '%s'" % (path, line[0]))
    if ops is not None:
        cases.append((name, ops))
    out = []
    for name, ops in cases:
        r = Ring(lambda: None)
        for op in ops:
            if op[0] == "enq":
                r.enqueue(op[1], op[2], op[3], op[4])
            elif r.q:
                r.q.pop(0)
        out.append((name, ops, list(r.q)))
    return out


# =============================================================================
# THE CANVAS COMPOSER (WEAVE-SPEC 6.10.2) AND ITS DIFFERENTIAL CORPUS
#
# The model deliberately does not draw pixels for any other component - the
# docstring at the top of this file says so - and for the canvas it has to,
# because 6.10.2 is the ONE part of wave 5 with no other oracle. weavegrid
# diffs the band composer against `band()`; weavegfx diffs a card against
# `--render`; the canvas's buffer is not on any card and its dirty-band choice
# is not visible in a picture at all. So this is the reference implementation
# of a thing the machine does, written from 6.10.2 and from nothing else, and
# apps/weave/hosttest/weavecv.asm runs the SHIPPING wspr.inc against it in raw
# QEMU with no kernel underneath (WEAVE-SPEC 12.1.3).
# =============================================================================


class CvSprite:
    """One sprite record (WEAVE-SPEC 6.10.4), host-side."""

    def __init__(self, desc, img, msk, wb, ph, nfr, x, y, vx, vy, shown):
        self.desc, self.img, self.msk = desc, img, msk
        self.wb, self.ph, self.nfr = wb, ph, nfr
        self.pw = wb * 8
        self.x, self.y, self.vx, self.vy = x, y, vx, vy
        self.px16, self.py16 = x * 16, y * 16
        self.shown, self.frame, self.scored = shown, 0, False
        self.ox, self.oy, self.oframe, self.oshown = x, y, 0, False


class CvCanvas:
    """WEAVE-SPEC 6.10's frame, 6.10.2's composition and 6.10.6's ring."""

    RING = 32

    def __init__(self, w, h, walls, tick, cid):
        self.w, self.h, self.walls, self.tick, self.cid = w, h, walls, tick, cid
        self.stride = w // 8
        self.buf = bytearray(b"\xFF" * (self.stride * h))
        self.spr = []
        self.contacts = set()
        self.dirty = [0] * (h // 8)
        self.frame = 0
        self.ring = []              # the STAGING ring (6.10.6), not 4.9's
        self.tickp = False
        self.ovf = 0
        self.bels = 0
        self.blits = []             # (band0, nbands) per emitted run

    # --- 6.10.6: the staging ring -------------------------------------------
    def stage(self, comp, atom, d1, d2):
        if atom == 57:                              # ontick collapses to one
            if self.tickp:
                return
            self.tickp = True
        if len(self.ring) >= self.RING - 1:         # head == tail is empty, so
            self.ovf += 1                           # 31 of 32 slots are usable
            if atom != 50:
                return
            if self.ring and self.ring[-1][1] != 50 and len(self.ring) > 1:
                self.ring[-1] = (comp, atom, d1 & 0xFFFF, d2 & 0xFFFF)
                return
            self.bels += 1
            return
        self.ring.append((comp, atom, d1 & 0xFFFF, d2 & 0xFFFF))

    # --- 6.10.1: the frame's arithmetic -------------------------------------
    def step(self):
        self.frame += 1
        W, H, walls = self.w, self.h, self.walls
        for k, s in enumerate(self.spr):
            if not s.shown:
                continue
            s.px16 = _w16(s.px16 + s.vx)
            s.py16 = _w16(s.py16 + s.vy)
            s.x, s.y = s.px16 >> 4, s.py16 >> 4
            hits = ((s.y < 0), (s.y + s.ph > H), (s.x < 0), (s.x + s.pw > W))
            outedge = -1
            for edge in range(4):
                if not hits[edge]:
                    continue
                if walls & (1 << edge):
                    if edge == 0:
                        s.py16 = _w16(-s.py16)
                    elif edge == 1:
                        s.py16 = _w16(2 * (H - s.ph) * 16 - s.py16)
                    elif edge == 2:
                        s.px16 = _w16(-s.px16)
                    else:
                        s.px16 = _w16(2 * (W - s.pw) * 16 - s.px16)
                    if edge < 2:
                        s.vy = _w16(-s.vy)
                    else:
                        s.vx = _w16(-s.vx)
                    s.x, s.y = s.px16 >> 4, s.py16 >> 4
                    self.stage(self.cid, 55, self.cid + 1 + k, edge)
                elif outedge < 0:
                    out = ((edge == 0 and s.y + s.ph < 0)
                           or (edge == 1 and s.y > H)
                           or (edge == 2 and s.x + s.pw < 0)
                           or (edge == 3 and s.x > W))
                    if out:
                        outedge = edge
            if outedge < 0:
                s.scored = False
            elif not s.scored:
                s.scored = True
                s.vx = s.vy = 0
                self.stage(self.cid, 56, self.cid + 1 + k, outedge)
        # AABB over every PAIR, hidden counting as separated (6.10.1)
        for i in range(len(self.spr)):
            for j in range(i + 1, len(self.spr)):
                a, b = self.spr[i], self.spr[j]
                ov = (a.shown and b.shown
                      and a.x < b.x + b.pw and b.x < a.x + a.pw
                      and a.y < b.y + b.ph and b.y < a.y + a.ph)
                pair = (i, j)
                if ov and pair not in self.contacts:
                    self.contacts.add(pair)
                    self.stage(self.cid, 54, self.cid + 1 + i, self.cid + 1 + j)
                elif not ov:
                    self.contacts.discard(pair)
        self.mark()
        self.flush()
        if self.tick and self.frame % self.tick == 0:
            self.stage(self.cid, 57, self.frame & 0xFFFF, 0)

    # --- 6.10.2: the dirty bands --------------------------------------------
    def markrows(self, r0, r1):
        r0 = max(0, r0)
        r1 = min(self.h - 1, r1)
        if r0 > r1:
            return
        for b in range(r0 >> 3, (r1 >> 3) + 1):
            self.dirty[b] = 1

    def markall(self):
        self.dirty = [1] * len(self.dirty)

    def mark(self):
        for s in self.spr:
            if (s.shown == s.oshown and s.x == s.ox and s.y == s.oy
                    and s.frame == s.oframe):
                continue
            if s.oshown:
                self.markrows(s.oy, s.oy + s.ph - 1)
            if s.shown:
                self.markrows(s.y, s.y + s.ph - 1)

    def compose(self, s, r0, r1):
        """2.11's rule, complemented into the FRAMEBUFFER's polarity (6.10.2):

            dst = (dst OR coverage) AND NOT image

        where coverage is NOT(mask).  The buffer is 1 = LIT because on a 1bpp
        adapter GFX_BLIT1_PEN is not read at all (SPEC.md 5.4.2.2) and two
        adapters of three are 1bpp.
        """
        if not s.shown or not s.wb:
            return
        top = max(r0, s.y, 0)
        bot = min(r1, s.y + s.ph - 1, self.h - 1)
        if top > bot:
            return
        shift = s.x & 7
        col0 = s.x >> 3
        fsz = s.ph * s.wb
        base = s.frame * fsz            # img and msk are per-frame runs here;
                                        # 2.11 interleaves them in the SECTION
                                        # and wsm_drawspr walks that, which is
                                        # the machine's business and not the
                                        # picture's
        for r in range(top, bot + 1):
            srow = r - s.y
            ci = cc = 0
            for j in range(s.wb + 1):
                if j < s.wb:
                    v = s.img[base + srow * s.wb + j]
                    c = (~s.msk[base + srow * s.wb + j]) & 0xFF
                else:
                    v = c = 0
                oi = (v << 8) >> shift
                outi = ci | ((oi >> 8) & 0xFF)
                ci = oi & 0xFF
                oc = (c << 8) >> shift
                outc = cc | ((oc >> 8) & 0xFF)
                cc = oc & 0xFF
                col = col0 + j
                if 0 <= col < self.stride:
                    p = r * self.stride + col
                    self.buf[p] = (self.buf[p] | outc) & (~outi & 0xFF)

    def flush(self):
        """One GFX_BLIT1 per maximal run of dirty bands."""
        self.blits = []
        b = 0
        n = len(self.dirty)
        while b < n:
            if not self.dirty[b]:
                b += 1
                continue
            b0 = b
            while b < n and self.dirty[b]:
                b += 1
            r0, r1 = b0 * 8, b * 8 - 1
            for r in range(r0, r1 + 1):
                for c in range(self.stride):
                    self.buf[r * self.stride + c] = 0xFF   # paper is LIT
            for s in self.spr:
                self.compose(s, r0, r1)
            self.blits.append((b0, b - b0))
        self.dirty = [0] * n
        for s in self.spr:
            s.ox, s.oy, s.oframe, s.oshown = s.x, s.y, s.frame, bool(s.shown)


def _w16(v):
    """The 8086's word, signed: every accumulator in 6.10.1 is one."""
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def _vmc_bytes(label, data):
    lines = ["%s:" % label]
    for k in range(0, len(data), 16):
        lines.append("    db " + ", ".join("0x%02X" % x
                                           for x in data[k:k + 16]))
    return lines


# --- --emit-cvcorpus: the canvas differential (WEAVE-SPEC 12.1.3) ------------

def _cv_spritesec(sprites):
    """The SPRITES section (2.11) for a list of Sprite, exactly as the packer
    lays one out - so the machine's wsm_desc reads a real section and not a
    shape invented for the harness."""
    sp = bytearray([len(sprites), 0])
    at = 2 + 8 * len(sprites)
    blobs = []
    for s in sprites:
        sp += bytes([s.w // 8, s.h, s.frames, 0])
        sp += struct.pack("<H", at)
        sp += b"\0\0"
        blob = b"".join(i + m for i, m in zip(s.images, s.masks))
        blobs.append(blob)
        at += len(blob)
    for b in blobs:
        sp += b
    return bytes(sp)


# The corpus itself.  It is a TABLE and not a directory of files, which is a
# deliberate divergence from 12.1.1's and 12.1.2's shape and is worth the
# sentence: a WVM case is one .wjs and an FX case is one .fx, but a canvas
# case is a canvas, a set of sprite images, an initial placement AND a frame
# count - four kinds of thing - and a text format for it would be a fifth
# language in a family that already has four (3, 4, 5 and the .WSP art).  The
# art is real .WSP text, parsed by parse_wsp, so the one part that HAS a
# language keeps it.
_CV_ART = {
    "BALL": "sprite BALL 8 8\n..####..\n.######.\n########\n########\n"
            "########\n########\n.######.\n..####..\n",
    "BAR":  "sprite BAR 8 16\n" + "########\n" * 16,
    "DOT":  "sprite DOT 8 8\n#.......\n........\n........\n........\n"
            "........\n........\n........\n.......#\n",
    "TWO":  "sprite TWO 8 8 2\n####....\n####....\n####....\n####....\n"
            "####....\n####....\n####....\n####....\n-\n....####\n....####\n"
            "....####\n....####\n....####\n....####\n....####\n....####\n",
}

#  name, W, H, walls, tick, cid, [(art, x, y, vx, vy, shown, frame)],
#  frames, negctl, hideframe (0 = never; else the sprite 0 is hidden BEFORE
#  that frame, which is the only mid-run change a case can make)
_CV_CASES = [
    ("still", 64, 32, 0xF, 0, 3, [("BALL", 8, 8, 0, 0, 1, 0)], 3, 0, 0),
    ("slide", 64, 32, 0xF, 0, 3, [("BALL", 8, 8, 32, 0, 1, 0)], 4, 0, 0),
    ("subpixel", 64, 32, 0xF, 0, 3, [("BALL", 8, 8, 1, 0, 1, 0)], 40, 0, 0),
    ("bounce-r", 64, 32, 0xF, 0, 3, [("BALL", 48, 8, 64, 0, 1, 0)], 6, 0, 0),
    ("bounce-t", 64, 32, 0xF, 0, 3, [("BALL", 8, 4, 0, -48, 1, 0)], 5, 0, 0),
    ("negx", 64, 32, 0, 0, 3, [("BALL", -3, 8, 0, 0, 1, 0)], 2, 0, 0),
    ("shift3", 64, 32, 0, 0, 3, [("DOT", 3, 5, 0, 0, 1, 0)], 2, 0, 0),
    ("shift7", 64, 32, 0, 0, 3, [("DOT", 57, 9, 0, 0, 1, 0)], 2, 0, 0),
    ("overlap", 64, 32, 0xF, 0, 3,
     [("BAR", 8, 4, 0, 0, 1, 0), ("DOT", 10, 6, 0, 0, 1, 0)], 2, 0, 0),
    ("collide", 64, 32, 0xF, 0, 3,
     [("BALL", 8, 8, 48, 0, 1, 0), ("BAR", 32, 8, 0, 0, 1, 0)], 8, 0, 0),
    ("hide-separates", 64, 32, 0xF, 0, 3,
     [("BALL", 8, 8, 0, 0, 1, 0), ("BAR", 10, 8, 0, 0, 1, 0)], 3, 0, 2),
    ("score-open", 64, 32, 0x3, 0, 3, [("BALL", 8, 8, -48, 0, 1, 0)], 8, 0, 0),
    ("frames", 64, 32, 0, 0, 3, [("TWO", 16, 8, 0, 0, 1, 1)], 3, 0, 0),
    ("tick", 64, 32, 0xF, 3, 3, [("BALL", 8, 8, 16, 0, 1, 0)], 7, 0, 0),
    # ...and the staging ring's OVERFLOW, which is where a lost event would
    # live. A sprite crossing a 32-pixel canvas at 20 px a frame bounces every
    # other frame, so 120 frames stage far more than 6.10.6's 31 usable slots
    # and nothing ever drains them.
    ("ring-flood", 64, 32, 0xF, 0, 3,
     [("BALL", 8, 4, 0, -320, 1, 0)], 120, 0, 0),
    # NEGATIVE CONTROLS: the expected answer is deliberately wrong and the
    # harness must FAIL them.  A differential that cannot see a broken core
    # has proved nothing (12.1.1's rule).
    ("NEG-buffer", 64, 32, 0xF, 0, 3, [("BALL", 8, 8, 32, 0, 1, 0)], 4, 1, 0),
    ("NEG-state", 64, 32, 0xF, 0, 3, [("BALL", 48, 8, 64, 0, 1, 0)], 6, 2, 0),
]


def _cv_run(case):
    name, w, h, walls, tick, cid, places, nframes, neg, hidef = case
    names = []
    for pl in places:
        if pl[0] not in names:
            names.append(pl[0])
    sprites = []
    for n in names:
        sprites += parse_wsp(_CV_ART[n], n + ".wsp")
    sec = _cv_spritesec(sprites)
    cv = CvCanvas(w, h, walls, tick, cid)
    for art, x, y, vx, vy, sh, fr in places:
        k = names.index(art)
        s = sprites[k]
        cv.spr.append(CvSprite(k, b"".join(s.images), b"".join(s.masks),
                               s.w // 8, s.h, s.frames, x, y, vx, vy, sh))
        cv.spr[-1].frame = fr
    cv.markall()
    cv.flush()                      # the birth composition, as the load does
    for f in range(nframes):
        if hidef and f + 1 == hidef:
            cv.spr[0].shown = 0
        cv.step()
    return cv, sec, sprites, names


def emit_cvcorpus():
    """WEAVE-SPEC 12.1.3.  Answers the nasm text."""
    out = ["; GENERATED by `python3 tools/weavesim.py --emit-cvcorpus` - do",
           "; not edit. WEAVE-SPEC 12.1.3: the canvas core's differential, and",
           "; the ONLY oracle 6.10.2's composition has.",
           "",
           "cv_ncase: dw %d" % len(_CV_CASES),
           "cv_tab:"]
    body = []
    for k, case in enumerate(_CV_CASES):
        name, w, h, walls, tick, cid, places, nframes, neg, hidef = case
        cv, sec, sprites, names = _cv_run(case)
        out.append("    dw cvn_%d, cvs_%d, cvi_%d, cve_%d, cvr_%d, cvb_%d, "
                   "cvx_%d" % (k, k, k, k, k, k, k))
        out.append("    dw %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d"
                   % (w, h, walls, tick, cid, len(places), nframes, neg,
                      cv.ovf, cv.bels, hidef))
        body.append('cvn_%d: db "%s", 0' % (k, name))
        body += _vmc_bytes("cvs_%d" % k, sec)
        init = bytearray()
        for art, x, y, vx, vy, sh, fr in places:
            init += struct.pack("<7H", names.index(art), x & 0xFFFF,
                                y & 0xFFFF, vx & 0xFFFF, vy & 0xFFFF, sh, fr)
        body += _vmc_bytes("cvi_%d" % k, bytes(init))
        exp = bytearray()
        for s in cv.spr:
            fl = (1 if s.shown else 0) | (2 if s.scored else 0) \
                 | (4 if s.oshown else 0)
            exp += struct.pack("<8H", s.px16 & 0xFFFF, s.py16 & 0xFFFF,
                               s.x & 0xFFFF, s.y & 0xFFFF, s.vx & 0xFFFF,
                               s.vy & 0xFFFF, fl,
                               (s.frame & 0x0F) | ((s.oframe & 0x0F) << 4))
        if neg == 2:
            exp[0] ^= 0x01          # the negative control's wrong end state
        body += _vmc_bytes("cve_%d" % k, bytes(exp))
        ring = bytearray(struct.pack("<H", len(cv.ring)))
        for comp, atom, d1, d2 in cv.ring:
            ring += bytes([comp & 0xFF, atom & 0xFF])
            ring += struct.pack("<2H", d1, d2)
        body += _vmc_bytes("cvr_%d" % k, bytes(ring))
        bl = bytearray(struct.pack("<H", len(cv.blits)))
        for b0, nb in cv.blits:
            bl += struct.pack("<2H", b0, nb)
        body += _vmc_bytes("cvb_%d" % k, bytes(bl))
        buf = bytes(cv.buf)
        if neg == 1:
            buf = bytes([buf[0] ^ 0xFF]) + buf[1:]
        body += _vmc_bytes("cvx_%d" % k, struct.pack("<H", len(buf)) + buf)
    return "\n".join(out + [""] + body)


def emit_vmcorpus(dirname):
    """WEAVE-SPEC 12.1.1.  Answers the nasm text."""
    import tempfile
    files = sorted(f for f in os.listdir(dirname) if f.endswith(".wjs"))
    if not files:
        raise SystemExit("--emit-vmcorpus: no .wjs in %s" % dirname)
    out = ["; The WVM differential corpus (WEAVE-SPEC 12.1.1).",
           "; GENERATED by `python3 tools/weavesim.py --emit-vmcorpus "
           "tests/weave/vmcorpus`.",
           "; Do not edit - regenerate. The expected states are the MODEL's,",
           "; serialized by WEAVE-SPEC 8.3's rules, which is what makes this",
           "; a differential rather than a second opinion.",
           ";",
           "; Row: name, code, codelen, nfunc, atoms, atomslen, natoms,",
           ";      init fn, entry fn, expected, expectedlen, errcode, neg",
           "WVC_ROW equ 26"]
    rows, blobs, n = [], [], 0
    tmp = tempfile.mkdtemp(prefix="wvc")
    for f in files:
        name, b, init_fn, entry, exp, err = _vmc_case(tmp, os.path.join(
            dirname, f))
        code = b.sections[SEC_CODE][0]
        atoms = b.sections[SEC_ATOMS][0]
        tag = "wvc%d" % n
        rows.append("    dw %s_name, %s_code, %d, %d, %s_atoms, %d, %d, "
                    "%d, %d, %s_exp, %d, %d, 0"
                    % (tag, tag, len(code), len(b.functions), tag,
                       len(atoms), len(b.atom_strings), init_fn, entry,
                       tag, len(exp), err))
        blobs.append("%s_name: db '%s', 0" % (tag, name.replace("'", " ")))
        blobs += _vmc_bytes(tag + "_code", code)
        blobs += _vmc_bytes(tag + "_atoms", atoms)
        blobs += _vmc_bytes(tag + "_exp", exp)
        n += 1
        # ...and the NEGATIVE CONTROL for the same case: the identical
        # program against an expectation one byte wrong. The harness must
        # FAIL it, which is what proves the comparison is running at all.
        if n == 1:
            bad = bytearray(exp)
            bad[6] ^= 0xFF          # the first global's tag word
            tag2 = "wvc%d" % n
            rows.append("    dw %s_name, %s_code, %d, %d, %s_atoms, %d, %d, "
                        "%d, %d, %s_exp, %d, %d, 1"
                        % (tag2, tag, len(code), len(b.functions), tag,
                           len(atoms), len(b.atom_strings), init_fn, entry,
                           tag2, len(bad), err))
            blobs.append("%s_name: db 'NEG end state', 0" % tag2)
            blobs += _vmc_bytes(tag2 + "_exp", bytes(bad))
            n += 1
    out.append("WVC_N equ %d" % n)
    out.append("wvc_tab:")
    out += rows
    out += blobs

    rp = os.path.join(dirname, "ring.txt")
    rings = _vmc_ring(rp) if os.path.exists(rp) else []
    out.append("WVR_ROW equ 8")
    out.append("WVR_N equ %d" % (len(rings) + (1 if rings else 0)))
    out.append("wvr_tab:")
    rblob = []
    for k, (name, ops, want) in enumerate(rings):
        tag = "wvr%d" % k
        out.append("    dw %s_name, %s_ops, %d, %s_exp"
                   % (tag, tag, len(ops), tag))
        rblob.append("%s_name: db '%s', 0" % (tag, name))
        b = bytearray()
        for op in ops:
            if op[0] == "enq":
                b += bytes([0, op[1] & 0xFF, op[2] & 0xFF, 0])
                b += struct.pack("<hh", op[3], op[4])
            else:
                b += bytes([1, 0, 0, 0, 0, 0, 0, 0])
        rblob += _vmc_bytes(tag + "_ops", bytes(b))
        e = bytearray([len(want)])
        for rec in want:
            e += bytes([rec[0] & 0xFF, rec[1] & 0xFF])
            e += struct.pack("<hh", rec[2], rec[3])
        rblob += _vmc_bytes(tag + "_exp", bytes(e))
    if rings:                       # the ring's negative control, same shape
        name, ops, want = rings[0]
        out.append("    dw wvrN_name, wvr0_ops, %d, wvrN_exp" % len(ops))
        rblob.append("wvrN_name: db 'NEG ring', 0")
        e = bytearray([len(want) + 1])
        for rec in want:
            e += bytes([rec[0] & 0xFF, rec[1] & 0xFF])
            e += struct.pack("<hh", rec[2], rec[3])
        e += bytes(6)
        rblob += _vmc_bytes("wvrN_exp", bytes(e))
    out += rblob
    return "\n".join(out)


# --- the FX differential corpus (WEAVE-SPEC 12.1.2) --------------------------
def grid_image(g):
    """A GridRt serialized as WEAVE-SPEC 5.6's cell store, exactly as the
    grid claim holds it: the 16-byte header, the dense row-major array of
    4-byte records, then the bump-allocated pool.

    THE MODEL OWNS THESE BYTES, which is what makes 12.1.2 a differential:
    the machine's FX VM reads an image this function wrote, so a defect in
    how a cell is READ shows in the corpus rather than in the app. Labels
    become kind 5 (a pool string) and formulas kind 6 (a pool RPN) so that
    the image is self-contained - kinds 3 and 4 name a bundle, and the read
    path for a formula is the pool slot's cached value whichever kind it is.
    """
    ncell = g.rows * g.cols
    cells = bytearray(ncell * 4)
    pool = bytearray()
    base = 16 + ncell * 4

    def alloc(b):
        off = base + len(pool)
        pool.extend(b)
        if len(pool) & 1:
            pool.append(0)              # slots stay even, as the claim's do
        return off

    for (r, c), v in sorted(g.vals.items()):
        k = (r * g.cols + c) * 4
        if v[0] == "num":
            val = v[1]
            if (val & 0xFFFF) == 0 and -32768 <= (val >> 16) <= 32767:
                cells[k] = 1
                struct.pack_into("<h", cells, k + 2, val >> 16)
            else:
                cells[k] = 2
                struct.pack_into("<H", cells, k + 2,
                                 alloc(struct.pack("<i", val)))
        elif v[0] == "label":
            s = v[1].encode("ascii", "replace")[:255]
            cells[k] = 5
            struct.pack_into("<H", cells, k + 2,
                             alloc(bytes([len(s)]) + s))
        else:
            rpn = g.rpn(v)
            cached = v[2]
            body = struct.pack("<H", len(rpn))
            body += struct.pack("<i", 0 if cached is FX_ERR else cached)
            body += struct.pack("<i", 0)
            body += bytes(rpn)
            cells[k] = 6
            if (r, c) in g.circ:
                cells[k + 1] |= 1       # 5.6's CIRC bit
            struct.pack_into("<H", cells, k + 2, alloc(body))
            if cached is FX_ERR:        # the error value is a KIND, not a
                cells[k + 1] |= 4       # number - see wfx.inc's WG_FERR
    hdr = bytearray(16)
    hdr[0] = g.cols
    struct.pack_into("<H", hdr, 2, g.rows)
    struct.pack_into("<H", hdr, 4, base + len(pool))     # pool-next
    struct.pack_into("<H", hdr, 6, base + len(pool))     # pool-end
    return bytes(hdr) + bytes(cells) + bytes(pool)


def parse_fxcase(text, fname):
    """One `tests/weave/fxcorpus/*.fx` file -> (name, GridRt, [(src, rpn,
    expected)]).  The syntax is .WFX's (11.2) plus a `grid` line and `?`
    lines for the expressions to evaluate."""
    name = None
    cols = rows = None
    cellsrc, asks = [], []
    for lineno, line in enumerate(text.split("\n"), 1):
        line = line.strip()
        if not line:
            continue
        if line.startswith("#"):
            if name is None:
                name = line[1:].strip()
            continue
        if line.startswith("grid "):
            cols, rows = (int(x) for x in line.split()[1:3])
            continue
        if line.startswith("?"):
            asks.append((line[1:].strip().lstrip("="), lineno))
            continue
        cellsrc.append((line, lineno))
    if cols is None:
        raise SystemExit("%s: no `grid <cols> <rows>` line" % fname)
    g = GridRt.__new__(GridRt)
    g.cols, g.rows, g.b = cols, rows, None
    g.vals, g.circ, g.dirty = {}, set(), set()
    g.top = g.left = 0
    for line, lineno in cellsrc:
        m = re.match(r"^(\S+)\s*=\s*(.+)$", line)
        if not m:
            raise SystemExit("%s:%d: <cellref> = <value>" % (fname, lineno))
        r, c = parse_cellref(m.group(1), cols, rows, fname, lineno)
        msg = g.commit(r, c, m.group(2).strip().strip('"')
                       if m.group(2).strip().startswith('"')
                       else m.group(2).strip(), fname)
        if msg:
            raise SystemExit("%s:%d: %s" % (fname, lineno, msg))
    g.recalc()
    out = []
    for src, lineno in asks:
        rpn = FxCompiler(cols, rows, fname, lineno).compile(src)
        out.append((src, rpn, fx_eval(rpn, g.read_cell)))
    return name or os.path.basename(fname), g, out


def emit_fxcorpus(dirname):
    """WEAVE-SPEC 12.1.2.  Answers the nasm text."""
    files = sorted(f for f in os.listdir(dirname) if f.endswith(".fx"))
    if not files:
        raise SystemExit("--emit-fxcorpus: no .fx in %s" % dirname)
    out = ["; The FX differential corpus (WEAVE-SPEC 12.1.2).",
           "; GENERATED by `python3 tools/weavesim.py --emit-fxcorpus "
           "tests/weave/fxcorpus`.",
           "; Do not edit - regenerate. Every expected value is the MODEL's,",
           "; computed over a 5.6 cell store the model also wrote.",
           ";",
           "; Row: name, store, storelen, cols, rows, rpn, rpnlen,",
           ";      type (0 number, 2 #DIV0), value lo, value hi, neg",
           "FXC_ROW equ 22"]
    rows, blobs, n = [], [], 0
    for fi, f in enumerate(files):
        path = os.path.join(dirname, f)
        name, g, asks = parse_fxcase(open(path).read(), path)
        img = grid_image(g)
        stag = "fxs%d" % fi
        blobs += _vmc_bytes(stag, img)
        for k, (src, rpn, want) in enumerate(asks):
            tag = "fxc%d" % n
            ty = 2 if want is FX_ERR else 0
            v = 0 if want is FX_ERR else want & 0xFFFFFFFF
            rows.append("    dw %s_name, %s, %d, %d, %d, %s_rpn, %d, "
                        "%d, 0x%04X, 0x%04X, 0"
                        % (tag, stag, len(img), g.cols, g.rows, tag,
                           len(rpn), ty, v & 0xFFFF, (v >> 16) & 0xFFFF))
            blobs.append("%s_name: db '%s: %s', 0"
                         % (tag, name.replace("'", " ")[:20],
                            src.replace("'", " ")[:28]))
            blobs += _vmc_bytes(tag + "_rpn", rpn)
            n += 1
            # ...and the NEGATIVE CONTROL on the FIRST expression of the
            # FIRST case: the same formula against a value one bit wrong,
            # which the harness must FAIL (12.1.1's rule, said for FX).
            if n == 1:
                rows.append("    dw fxcN_name, %s, %d, %d, %d, %s_rpn, %d, "
                            "%d, 0x%04X, 0x%04X, 1"
                            % (stag, len(img), g.cols, g.rows, tag,
                               len(rpn), ty, (v ^ 1) & 0xFFFF,
                               (v >> 16) & 0xFFFF))
                blobs.append("fxcN_name: db 'NEG fx value', 0")
                n += 1
    out.append("FXC_N equ %d" % n)
    out.append("fxc_tab:")
    out += rows
    out += blobs
    return "\n".join(out)


# --- CLI verbs ---------------------------------------------------------------
def cmd_pack(path, out_path, with_source):
    res = pack_project(path, with_source)
    if not out_path:
        stem = os.path.splitext(os.path.basename(path.rstrip("/")))[0]
        out_path = stem.upper() + ".WAB"
    open(out_path, "wb").write(res.data)
    hdr = res.data
    print("%s: %d bytes, %d sections, %d atoms, %d functions"
          % (out_path, len(res.data), hdr[12],
             len(Bundle(res.data, out_path).atom_strings),
             len(res.prog.funcs)))
    print("  ask = %dKB bundle + %dKB vm + %dKB grid + %dKB canvas "
          "= %dKB before any I/O (WEAVE-SPEC 10.1)"
          % ((len(res.data) + 1023) // 1024, hdr[10], hdr[11], hdr[14],
             res.ask_kb))
    for w in res.warnings:
        print("  note: " + w)
    return res


def runtime_for(bundle_path, src=None):
    data = open(bundle_path, "rb").read()
    name = os.path.basename(bundle_path)
    b = Bundle(data, name)
    idmap = {}
    if src is None:
        # try the sibling source for the id map (ids are not in the bundle
        # - CODE carries no name table, WEAVE-SPEC 10.6)
        stem = os.path.splitext(bundle_path)[0]
        for cand in (stem + ".wml", stem + ".WML", stem.lower() + ".wml"):
            if os.path.exists(cand):
                src = cand
                break
    if src:
        try:
            res = pack_project(src)
        except PackError as ex:
            print("note: %s does not pack (%s); running without the id "
                  "map" % (src, ex))
            res = None
        if res and res.data == data:
            idmap = res.app.idmap
        elif res:
            print("note: %s does not repack to these bytes; running "
                  "without the id map" % src)
    sav = os.path.splitext(bundle_path)[0] + ".SAV"
    return Runtime(b, idmap=idmap, sav_path=sav)


def cmd_run(bundle_path, events_path, src=None, adapter="cga",
            render_after=False):
    """--run, and with --render-after the CARD as it stands when the events
    are spent rather than a state dump. That is what a differential against
    the glass needs: tests/weavegrid.py compares the machine's bands with the
    model's, and a band is a line of this picture."""
    rt = runtime_for(bundle_path, src)
    rt.adapter = adapter            # 6.9.4's scroll clamp needs a layout
    print("%s: %d cards, %d components, entry card %d"
          % (rt.b.app_name, len(rt.b.cards), len(rt.b.comps), rt.b.entry))
    for lineno, line in enumerate(open(events_path), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        verb = parts[0]
        if verb == "tick":
            rt.tick(int(parts[1]) if len(parts) > 1 else 1)
        elif verb == "dump":
            dump_state(rt)
        elif verb == "set":
            rt.gesture("set", parts[1], " ".join(parts[2:]))
        elif verb == "edit":
            # `edit <grid> <row> <col> <text...>` - the text runs to the end
            # of the line, because a label and a formula both have spaces in
            # them and splitting on the fourth field would truncate both.
            rt.gesture("edit", parts[1], parts[2], parts[3],
                       " ".join(parts[4:]))
        elif verb in ("click", "change", "key", "select", "command"):
            rt.gesture(verb, parts[1] if len(parts) > 1 else "",
                       parts[2] if len(parts) > 2 else "",
                       parts[3] if len(parts) > 3 else "")
        else:
            raise SystemExit("%s:%d: no such event verb '%s'"
                             % (events_path, lineno, verb))
    if render_after:
        render(rt, adapter)
        return
    for line in rt.out:
        print(line)
    dump_state(rt)


def dump_state(rt):
    print("-- state --")
    shown = []
    for g in range(rt.b.nglobals):
        t, v = rt.globals[g]
        if (t, v) != (T_INT, 0):
            tag = ["int", "str", "arr", "comp", "null", "bool"][t]
            shown.append("g%d=%s:%r" % (g, tag, v))
    if shown:
        print("globals: " + " ".join(shown))
    for cid in sorted(rt.comps):
        st = rt.comps[cid]
        keys = [k for k in ("text", "label", "value", "checked", "sel",
                            "x", "y", "vx", "vy", "frame", "shown")
                if k in st]
        print("  %s %s: %s" % (st["tag"], rt.cname(cid),
                               " ".join("%s=%r" % (k, st[k])
                                        for k in keys)))
    if rt.grid:
        g = rt.grid
        for (r, c) in sorted(g.vals):
            print("  cell %s%d = %s" % (chr(65 + c), r + 1,
                                        g.display(r, c)))
    print("gfx calls this session: %d" % rt.gfx_calls)


def cmd_render(path, adapters, card=None):
    if path.lower().endswith(".wab"):
        rt = runtime_for(path)
    else:
        res = pack_project(path)
        rt = Runtime(Bundle(res.data, "x.WAB"), idmap=res.app.idmap)
    # Card indices are 1-based (WEAVE-SPEC 2.5).  Python's are not: --card 0
    # rendered the LAST card and --card 9 traced back, so both refuse here
    # with the range, the way every other out-of-range answer does.
    if card is not None and not 1 <= card <= len(rt.b.cards):
        raise SystemExit("--card %d: %s has %d card%s, numbered 1..%d "
                         "(WEAVE-SPEC 2.5)"
                         % (card, rt.b.app_name, len(rt.b.cards),
                            "" if len(rt.b.cards) == 1 else "s",
                            len(rt.b.cards)))
    for ad in adapters:
        render(rt, ad, card)
        print()


# --- selfcheck (WEAVE-SPEC 12.1) ---------------------------------------------
class Check:
    def __init__(self, verbose=False):
        self.fails, self.count, self.verbose = [], 0, verbose

    def ok(self, cond, what):
        self.count += 1
        if not cond:
            self.fails.append(what)
            print("selfcheck FAIL: %s" % what)
        elif self.verbose:
            print("selfcheck ok:   %s" % what)

    def raises(self, what, exc, fn, *args, contains=None):
        self.count += 1
        try:
            fn(*args)
        except exc as ex:
            if contains and contains not in str(ex):
                self.fails.append("%s: raised %r, wanted %r in it"
                                  % (what, str(ex), contains))
                print("selfcheck FAIL: %s -> %s" % (what, ex))
            elif self.verbose:
                print("selfcheck ok:   %s" % what)
            return
        except Exception as ex:
            self.fails.append("%s: raised %r" % (what, ex))
            print("selfcheck FAIL: %s raised %r" % (what, ex))
            return
        self.fails.append("%s: did not refuse" % what)
        print("selfcheck FAIL: %s did not refuse" % what)


def _proj(tmp, wml, wjs=None, wfx=None, wsp=None, stem="app"):
    import shutil
    d = os.path.join(tmp, stem)
    if os.path.exists(d):
        shutil.rmtree(d)
    os.makedirs(d)
    open(os.path.join(d, stem + ".wml"), "w").write(wml)
    if wjs is not None:
        open(os.path.join(d, stem + ".wjs"), "w").write(wjs)
    if wfx is not None:
        open(os.path.join(d, stem + ".wfx"), "w").write(wfx)
    if wsp is not None:
        open(os.path.join(d, stem + ".wsp"), "w").write(wsp)
    return os.path.join(d, stem + ".wml")


MINI_WML = '<app name="Mini"><card id="main"><label>hi</label></card></app>'


def demo_dir():
    return os.path.normpath(os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "apps", "weave",
        "demos"))


def selfcheck(verbose=False):
    import tempfile
    ck = Check(verbose)
    tmp = tempfile.mkdtemp(prefix="weavesim-")

    # 1. tables and header arithmetic
    ck.ok(len(OPS) == 38, "38 WVM opcodes")
    ck.ok(len(FXOPS) == 23, "23 FX opcodes")
    ck.ok(len(BUILTINS) == 12, "12 builtins")
    ck.ok(len(CTYPE) == 14, "14 ctypes")
    res = pack_project(_proj(tmp, MINI_WML))
    ck.ok(res.data[:4] == WAB_MAGIC, "magic")
    ck.ok(struct.unpack_from("<H", res.data, 6)[0] == len(res.data),
          "header total size == file size")
    b = Bundle(res.data, "MINI.WAB")
    ck.ok(b.app_name == "Mini", "app name round-trips")
    ck.ok(b.vm_kb == 16 and b.entry == 1, "vm/entry defaults")
    ck.ok(all(struct.unpack_from("<H", res.data, 32 + 8 * k + 2)[0] % 16
              == 0 for k in range(res.data[12])), "sections 16-aligned")
    ck.ok(b.code[:3] == bytes([0, 0, OP["HALT"]]),
          "no-script CODE = empty table + HALT guard")
    ck.ok(len(b.icon) == 64, "ICON is 64 bytes")

    # 2. WML rejections - the pinned sentences (WEAVE-SPEC 10.5)
    def rej(wml, sub, what, wjs=None, wsp=None, wfx=None):
        ck.raises(what, PackError,
                  lambda: pack_project(_proj(tmp, wml, wjs=wjs, wsp=wsp,
                                             wfx=wfx)),
                  contains=sub)
    rej('<app name="x"><card id="m"><zap/></card></app>',
        "<zap>: not a Weave element; the inventory is closed",
        "unknown element sentence")
    rej('<app name="x"><card id="m"><button color="red">b</button>'
        "</card></app>",
        'there are no colors', "color attribute sentence")
    rej('<app name="x"><card id="m"><button zork="1">b</button>'
        "</card></app>",
        'button: no such attribute "zork"; style is bold/invert/align only',
        "unknown attribute sentence")
    rej('<app name="x"><card id="m"><button onhover="f">b</button>'
        "</card></app>",
        "onhover: no hover exists; pointer movement reaches a package only "
        "between press and release (SPEC.md 13.7)", "hover sentence")
    rej('<app name="x"><card id="m"><label style="red">t</label>'
        "</card></app>",
        'style: no such style "red"', "style vocabulary sentence")
    rej('<app name="x"><card id="m"><label>t</label></card>'
        "<script>var x;</script></app>",
        "script: inline script is not packed; name a .WJS file - the "
        "runtime never parses text", "inline script sentence")
    rej('<app name="x"><card id="m"><grid id="g" cols="26" rows="256"/>'
        "</card></app>",
        "grid is 26x256 = 6656 cells; the cap is 6140", "grid cap sentence")
    rej('<app name="x"><card id="m"><radio group="g">only</radio>'
        "</card></app>", "one member", "radio group of one")
    rej('<app name="x"><card id="m"><label>a &copy; b</label></card></app>',
        "the entity set is closed", "entity set closed")
    rej('<app name="x"><card id="m"><label>t</wrong></card></app>',
        "tags must nest", "mismatched close tag")
    rej('<app name="x"><card id="m"><grid id="a" cols="2" rows="2"/>'
        '<grid id="b" cols="2" rows="2"/></card></app>',
        "at most one of each", "two grids refused")
    # ontick over budget: a loop is a backward jump
    rej('<app name="x"><card id="m"><canvas id="c" w="64" h="32" tick="1" '
        'ontick="t"><sprite id="s" img="B"/></canvas></card>'
        '<script src="APP.WJS"/></app>',
        "backward jump", "ontick backward jump refused",
        wjs="function t(n){ while (n) { n = n - 1; } }",
        wsp="sprite B 8 8\n" + "########\n" * 8)
    rej('<app name="x"><card id="m"><canvas id="c" w="64" h="32" tick="1" '
        'ontick="t"><sprite id="s" img="B"/></canvas></card>'
        '<script src="APP.WJS"/></app>',
        "ops; the cap is 64 - per-frame JS does not fit 10-30k ops/s",
        "ontick op budget sentence",
        wjs="function t(n){ %s }" % ("n = n + 1; " * 30),
        wsp="sprite B 8 8\n" + "########\n" * 8)

    # 3. WJS codegen - the pinned templates, byte for byte
    def compile_one(src):
        atoms = Interner("t.wjs")
        toks = js_tokenize(src, "t.wjs", atoms)
        prog = js_collect(toks, "t.wjs")
        ctx = dict(idmap={}, cardmap={}, ctype_of={})
        compile_wjs(prog, toks, "t.wjs", ctx)
        return prog
    prog = compile_one("var x; var y;\n"
                       "function f() { if (x) { y = 1; } else { y = 2; } }")
    want = bytes([OP["LDG"], 0, OP["JZ"], 8, 0, OP["PUSHI"], 1, 0,
                  OP["STG"], 1, OP["JMP"], 5, 0, OP["PUSHI"], 2, 0,
                  OP["STG"], 1, OP["PUSHN"], OP["RET"]])
    ck.ok(prog.funcs[0].code == want, "if/else template bytes "
          "(got %s)" % prog.funcs[0].code.hex())
    prog = compile_one("var x;\nfunction h(a) { return a && x; }")
    want = bytes([OP["LDL"], 0, OP["DUP"], OP["JZ"], 3, 0, OP["POP"],
                  OP["LDG"], 0, OP["RET"], OP["PUSHN"], OP["RET"]])
    ck.ok(prog.funcs[0].code == want, "&& short-circuit template bytes")
    prog = compile_one("var x;\nfunction w() { while (x < 3) { x++; } }")
    want = bytes([OP["LDG"], 0, OP["PUSHI"], 3, 0, OP["LT"],
                  OP["JZ"], 5, 0, OP["INCG"], 0,
                  OP["JMP"], 0x100 - 14 & 0xFF, 0xFF,
                  OP["PUSHN"], OP["RET"]])
    ck.ok(prog.funcs[0].code == want, "while template bytes "
          "(got %s)" % prog.funcs[0].code.hex())
    ck.ok(prog.funcs[0].has_backjump, "backward jump detected")

    # 4. WJS rejections
    def jrej(src, sub, what):
        ck.raises(what, PackError, lambda: compile_one(src), contains=sub)
    jrej("function f() { break; }", "break outside a loop",
         "break outside loop")
    jrej("var alert;", "shadows a builtin", "builtin shadowing")
    jrej("var x; var x;", "declared twice", "redeclaration")
    jrej("function f() { y = 1; }", "not a local, global, component id",
         "undeclared identifier")
    jrej("function f(a, b) { } function g() { f(1); }",
         "takes 2 arguments; 1 written", "call arity")
    jrej("function f() { alert(1, 2); }", "callback names a top-level "
         "function", "alert callback form")
    jrej("function f() { x(); } ", "not a local, global",
         "call of unknown name")
    jrej('function f() { return "" ; }', "an atom is 1..255 bytes",
         "empty string literal refused")
    jrej("function f() { return 40000; }", "numbers are 0..32767",
         "number range")
    jrej("x = 1;", "top level is declarations only", "top-level statement")

    # 5. FX vectors: 16.16 exactly as the 8086 will
    def fxv(src, want, grid=None):
        fc = FxCompiler(26, 256, "t", 1)
        rpn = fc.compile(src)
        got = fx_eval(rpn, (grid or (lambda r, c: None)))
        ck.ok(got == want, "fx %s = %s (want %s)"
              % (src, "ERR" if got is FX_ERR else fmt_16_16(got),
                 "ERR" if want is FX_ERR else fmt_16_16(want)))
    fxv("2+3*4", 14 << 16)
    fxv("7/2", (7 << 16) // 2)
    fxv("(1+2)*(3+4)", 21 << 16)
    fxv("-5+3", -(2 << 16))
    fxv("30000+30000", wrap32(60000 << 16))     # overflow wraps, defined
    fxv("1/0", FX_ERR)
    fxv("1/0+5", FX_ERR)                        # the error propagates
    fxv("IF(1,2,1/0)", FX_ERR)                  # FIF is eager - stated
    fxv("IF(2>1,10,20)", 10 << 16)
    fxv("ROUND(2.5)", 3 << 16)                  # half away from zero
    fxv("-ROUND(2.5)", -(3 << 16))
    fxv("ROUND(2.4)", 2 << 16)
    fxv("ABS(0-9)", 9 << 16)
    fxv("3=3", 1 << 16)
    fxv("3<>3", 0)
    ck.ok(fx_mul(parse_number_16_16("1.5", "t", 1), 2 << 16) == 3 << 16,
          "16.16 multiply")
    ck.ok(fmt_16_16(fx_div(1 << 16, 3 << 16)) == "0.33", "16.16 divide "
          "truncates")
    grid = {(1, 1): 3 << 16, (2, 1): None, (3, 1): 7 << 16}
    fxv("SUM(B2:B4)", 10 << 16, grid=lambda r, c: grid.get((r, c)))
    fxv("COUNT(B2:B4)", 2 << 16, grid=lambda r, c: grid.get((r, c)))
    fxv("AVG(B2:B4)", 5 << 16, grid=lambda r, c: grid.get((r, c)))
    fxv("MIN(B2:B4)", 3 << 16, grid=lambda r, c: grid.get((r, c)))
    fxv("AVG(B9:B9)", 0, grid=lambda r, c: None)    # all-empty -> 0
    ck.raises("range outside aggregate", PackError,
              lambda: FxCompiler(26, 256, "t", 1).compile("A1:A3+1"),
              contains="aggregate")
    ck.raises("unknown fx function", PackError,
              lambda: FxCompiler(26, 256, "t", 1).compile("SIN(1)"),
              contains="whole set")
    deep = "1" + "+(1" * 17 + ")" * 17
    ck.raises("fx depth cap", PackError,
              lambda: FxCompiler(26, 256, "t", 1).compile(deep),
              contains="the stack is 16 slots")

    # 6. sprites: mask = NOT(coverage)
    sp = parse_wsp("sprite B 8 2\n#.......\n.#......\n", "t.wsp")
    ck.ok(sp[0].images[0] == bytes([0x80, 0x40]), "sprite image bits")
    ck.ok(sp[0].masks[0] == bytes([0x7F, 0xBF]), "sprite AND mask = NOT "
          "coverage")

    # 7. the ring policy (WEAVE-SPEC 4.9)
    bel = []
    r = Ring(lambda: bel.append(1))
    for k in range(20):
        r.enqueue(1, WK["onchange"], k, 0)
    ck.ok(len(r.q) == 1 and r.q[0][2] == 19, "onchange coalesces per "
          "component, newest wins")
    r = Ring(lambda: bel.append(1))
    for k in range(16):
        r.enqueue(k, WK["onclick"])
    ck.ok(len(r.q) == 16, "ring fills to 16")
    r.enqueue(99, WK["onedit"], 0, 0)
    ck.ok(len(r.q) == 16, "non-key at a full ring is dropped")
    ck.ok(r.enqueue(5, WK["onkey"], 65, 0), "key drops the newest non-key")
    ck.ok(r.q[-1][1] == WK["onkey"], "the key got in")
    r = Ring(lambda: bel.append(1))
    for k in range(16):
        r.enqueue(1, WK["onkey"], k, 0)
    ck.ok(len(r.q) == 16, "keys never coalesce")
    ck.ok(not r.enqueue(1, WK["onkey"], 99, 0) and bel,
          "a ring full of keys answers BEL and refuses the key")
    r = Ring(lambda: None)
    r.enqueue(1, WK["ontick"], 1, 0)
    r.enqueue(1, WK["ontick"], 2, 0)
    ck.ok(len([q for q in r.q if q[1] == WK["ontick"]]) == 1,
          "ontick collapses to one")

    return ck, tmp


def selfcheck_demos(ck, tmp):
    dd = demo_dir()
    sizes = {}
    for stem in ("form", "sheet", "pong"):
        p = os.path.join(dd, stem + ".wml")
        r1 = pack_project(p)
        r2 = pack_project(p)
        ck.ok(r1.data == r2.data, "%s packs byte-identically twice" % stem)
        ck.ok(len(r1.data) < 16384, "%s.WAB well under 16KB (%d bytes)"
              % (stem, len(r1.data)))
        Bundle(r1.data, stem.upper() + ".WAB")   # re-reads clean
        sizes[stem] = len(r1.data)
        wab = os.path.join(tmp, stem.upper() + ".WAB")
        open(wab, "wb").write(r1.data)

    # form: inputs, button, label updated by a handler, an alert
    res = pack_project(os.path.join(dd, "form.wml"))
    rt = Runtime(Bundle(res.data, "FORM.WAB"), idmap=res.app.idmap,
                 sav_path=os.path.join(tmp, "FORM.SAV"))
    rt.gesture("set", "who", "Ada")
    rt.gesture("click", "greet")
    ck.ok(rt.comps[res.app.idmap["status"]]["text"] == "Hello, Ada.",
          "form: greet wrote the label (got %r)"
          % rt.comps[res.app.idmap["status"]]["text"])
    rt.gesture("click", "loud")
    rt.gesture("click", "greet")
    ck.ok(rt.comps[res.app.idmap["status"]]["text"] == "HELLO, Ada!",
          "form: the check changed the handler's path (got %r)"
          % rt.comps[res.app.idmap["status"]]["text"])
    ck.ok(rt.comps[res.app.idmap["count"]]["value"] == 2,
          "form: meter counted two greetings")
    rt.gesture("set", "who", "")
    rt.gesture("click", "greet")
    ck.ok(any('alert "Type a name first."' in l for l in rt.out),
          "form: the empty-name alert fired")
    rt.gesture("command", "1", "1")
    ck.ok(rt.comps[res.app.idmap["status"]]["text"] == "Reset done."
          and rt.comps[res.app.idmap["count"]]["value"] == 0,
          "form: menu Reset ran and the alert callback arrived as a "
          "later onalert event")

    # form: layout invariants on all three adapters
    for ad in ADAPTERS:
        placed, total = flow_walk(rt, ad)
        A = ADAPTERS[ad]
        cells = set()
        for p in placed:
            ck.ok(p.x + p.w <= A["cw"], "%s: %s fits the row (x=%d w=%d "
                  "cw=%d)" % (ad, p.comp.tag, p.x, p.w, A["cw"]))
            for yy in range(p.y, p.y + p.h):
                for xx in range(p.x, p.x + p.w):
                    ck.ok((xx, yy) not in cells,
                          "%s: no overlap at %d,%d" % (ad, xx, yy)) \
                        if (xx, yy) in cells else None
                    cells.add((xx, yy))
        ck.ok(total >= max(p.y + p.h for p in placed), "%s: total rows "
              "cover the walk" % ad)

    # CF_BREAK on a card's FIRST component, and two in a row: closing an
    # empty row is a no-op (WEAVE-SPEC 7.2). A row with no component in it
    # has no height, and the walk once raised on max() of an empty list.
    brk = os.path.join(tmp, "brk.wml")
    with open(brk, "w") as f:
        f.write('<app name="Brk"><card id="main">'
                '<label br="1">A</label><label br="1">B</label>'
                '<label>C</label></card></app>\n')
    resb = pack_project(brk)
    rtb = Runtime(Bundle(resb.data, "BRK.WAB"), idmap=resb.app.idmap)
    pl, tot = flow_walk(rtb, "cga")
    ck.ok(tot == 2, "br: two rows, not three - the leading break emits no "
          "empty row (got %d)" % tot)
    ck.ok([(p.x, p.y) for p in pl] == [(0, 0), (0, 1), (2, 1)],
          "br: A at 0,0; B at 0,1; C at 2,1 (got %s)"
          % [(p.x, p.y) for p in pl])

    # sheet: SUM/AVG/IF over the budget, the WJS truncation seam
    res = pack_project(os.path.join(dd, "sheet.wml"))
    rt = Runtime(Bundle(res.data, "SHEET.WAB"), idmap=res.app.idmap)
    g = rt.grid
    ck.ok(g.display(5, 1) == "22.75", "sheet: SUM(B2:B4) = 22.75 (got %s)"
          % g.display(5, 1))
    ck.ok(g.display(6, 1) == "7.58", "sheet: AVG = 7.58 (got %s)"
          % g.display(6, 1))
    ck.ok(g.display(5, 2) == "23", "sheet: ROUND(22.75) = 23")
    ck.ok(g.display(7, 1) == "1", "sheet: IF(B6<25,1,0) = 1")
    rt.gesture("click", "btnTotal")
    ck.ok(any('alert "Total is 22"' in l for l in rt.out),
          "sheet: cell() truncates 22.75 to 22 across the WJS seam")
    rt.gesture("select", "g", "6", "2")
    ck.ok("22" in rt.comps[res.app.idmap["status"]]["text"],
          "sheet: onselect handler read the cell")
    rt.gesture("click", "btnBump")
    ck.ok(g.display(3, 1) == "13", "sheet: setCell landed")
    ck.ok(g.display(5, 1) == "23.75", "sheet: recalc followed the commit")
    ck.ok("Recalculated 3 cells." in
          rt.comps[res.app.idmap["status"]]["text"],
          "sheet: oncalc reported the changed count (got %r)"
          % rt.comps[res.app.idmap["status"]]["text"])

    # a circular sheet shows #CIRC (two-pass, pass-2 value stands)
    p = _proj(tmp, '<app name="Circ"><card id="m">'
              '<grid id="g" cols="2" rows="2"/></card></app>',
              wfx="A1 = =A1+1\n", stem="circ")
    res2 = pack_project(p)
    rt2 = Runtime(Bundle(res2.data, "CIRC.WAB"))
    ck.ok(rt2.grid.display(0, 0) == "#CIRC", "circular formula shows #CIRC")

    # pong: canvas, sprites, collide, score, tone - the host worker model
    res = pack_project(os.path.join(dd, "pong.wml"))
    rt = Runtime(Bundle(res.data, "PONG.WAB"), idmap=res.app.idmap)
    rt.gesture("click", "serve")
    ball = rt.comps[res.app.idmap["ball"]]
    ck.ok(ball["vx"] != 0, "pong: serve set the ball moving")
    x0 = ball["x"]
    rt.tick(10)
    ck.ok(ball["x"] != x0, "pong: frames move the ball by 1/16-px "
          "velocities")
    rt.tick(300)
    ck.ok(any("tone 880" in l for l in rt.out),
          "pong: a collision bounced and beeped")
    rt.gesture("key", "field", "a", "1")
    rt.tick(2000)
    score = rt.comps[res.app.idmap["score"]]["text"]
    ck.ok(score != "0 : 0" and any("tone 220" in l for l in rt.out),
          "pong: a ball out an open edge scored (score %r)" % score)
    ck.ok(not rt.canvas["running"], "pong: the score handler stopped the "
          "worker")
    # 6.10.1's two re-arms, and both rows are REGRESSION guards rather than
    # feature checks.  `scored` used to latch for the life of the instance,
    # so a second serve could never score and PONG was a one-goal game; a
    # contact whose sprite had been hidden was never discarded, because the
    # AABB pass walked only the SHOWN list.  Neither is visible in a
    # single-shot harness, which is why these rows serve twice and hide one.
    first = rt.comps[res.app.idmap["score"]]["text"]
    rt.gesture("click", "serve")
    rt.tick(3000)
    ck.ok(rt.comps[res.app.idmap["score"]]["text"] != first,
          "pong: a SECOND ball out an open edge scores too (6.10.1)")
    pad = rt.comps[res.app.idmap["pad"]]
    ball["x"], ball["y"] = pad["x"], pad["y"]
    ball["px16"], ball["py16"] = ball["x"] * 16, ball["y"] * 16
    rt.canvas["running"] = True
    rt.tick(1)
    ck.ok(rt.canvas["contacts"], "pong: an overlap latches a contact")
    ball["shown"] = 0
    rt.tick(1)
    ck.ok(not rt.canvas["contacts"],
          "pong: hiding a sprite SEPARATES its contacts (6.10.1)")
    rt.canvas["running"] = False
    return sizes


def selfcheck_vm(ck, tmp):
    # VM caps and the pinned script-error sentences (WEAVE-SPEC 10.6)
    wml = ('<app name="Caps"><card id="m"><label id="l">x</label></card>'
           '<script src="CAPS.WJS"/></app>')
    wjs = ("var a = array(10);\nvar n = 5;\nvar s = \"hi\";\n"
           "function boom() { return 1 / 0; }\n"
           "function deep(k) { return deep(k + 1); }\n"
           "function idx() { return a[12]; }\n"
           "function wrap() { return 30000 + 30000; }\n"
           "function inc() { n++; return n; }\n"
           "function cat() { return s + \" there\"; }\n")
    res = pack_project(_proj(tmp, wml, wjs=wjs, stem="caps"))
    ck.ok(res.start_fn is not None, "global initializers synthesized the "
          "init function (recorded reading: app block atom 40)")
    b = Bundle(res.data, "CAPS.WAB")
    st = b.app_props.get(WK["start"])
    ck.ok(st and st[0] == PK_FUNC and st[1] == res.start_fn,
          "app block names the init function")
    rt = Runtime(b, idmap=res.app.idmap,
                 sav_path=os.path.join(tmp, "CAPS.SAV"))
    ck.ok(rt.globals[1] == (T_INT, 5) and rt.globals[2] == (T_STR, "hi")
          and rt.globals[0][0] == T_ARR, "initializers applied at VM start")
    fni = res.prog.fnindex

    def run_expect(fn, sub, what):
        try:
            rt.invoke(fni[fn], [])
            ck.ok(False, what + " (no error raised)")
        except ScriptError as ex:
            ck.ok(sub in str(ex), "%s (got %r)" % (what, str(ex)))
    run_expect("boom", "Script error in fn %d: divide by zero."
               % fni["boom"], "divide-by-zero sentence")
    run_expect("deep", "too deep.", "call-depth sentence")
    run_expect("idx", "array index 12 of 10.", "array-bounds sentence")
    ck.ok(rt.invoke(fni["wrap"], []) == (T_INT, wrap16(60000)),
          "16-bit arithmetic wraps, defined")
    ck.ok(rt.invoke(fni["inc"], []) == (T_INT, 6), "INCG")
    ck.ok(rt.invoke(fni["cat"], []) == (T_STR, "hi there"), "str concat")
    ck.ok(rt.save_state() and rt.load_state(), "saveState/loadState "
          "round-trips the .SAV format")
    raw = rt.sav_mem
    ck.ok(raw[:4] == b"WSV\x1a" and struct.unpack_from("<H", raw, 4)[0]
          == 1, ".SAV magic and version")

    # the reader refuses per WEAVE-SPEC 10.4, field named
    good = res.data

    def broken(mut, field, what):
        d = bytearray(good)
        mut(d)
        ck.raises(what, BundleError, lambda: Bundle(bytes(d), "X.WAB"),
                  contains="is not a Weave bundle (%s)." % field)
    broken(lambda d: d.__setitem__(0, 0x57 + 1), "magic", "bad magic")
    broken(lambda d: d.__setitem__(4, 2), "version", "version != 1")
    broken(lambda d: d.__setitem__(6, d[6] ^ 1), "total size",
           "size word vs the actual read")
    broken(lambda d: d.__setitem__(9, 0x80), "flags", "unknown flag bit")
    ck.raises("truncated file", BundleError,
              lambda: Bundle(good[:40], "X.WAB"))

    # cost anchors: the model against the measured rows (htmsim's check,
    # plus the band composer's Set 68 figure)
    for ad, measured in (("herc", 71400.0), ("cga", 72700.0)):
        got = CALL_US + 78 * ADAPTERS[ad]["cell_us"]
        ck.ok(abs(got - measured) / measured < 0.02,
              "78-cell row model vs measured, %s (%.1f vs %.1f ms)"
              % (ad, got / 1000, measured / 1000))
    band79 = BAND_CALL_US + 79 * BAND_CELL_US
    ck.ok(abs(band79 - 14527) < 1, "79-cell band row = 14.5 ms (Set 68)")

    # The first paint (WEAVE-SPEC 14's added row): every other row of that
    # table is an interaction, and paint() counts only mutations, so
    # nothing priced opening a card - which is exactly wave 2's gate.
    A = ADAPTERS["cga"]
    ck.ok(abs(first_paint_us("cga")
              - A["ch"] * (CALL_US + A["cw"] * A["cell_us"])) < 1,
          "card first paint: one call plus its cells, per painted "
          "component, worst case a fully lettered card")
    ck.ok(first_paint_us("cga", [20, 20])
          == 2 * CALL_US + 40 * A["cell_us"],
          "first paint sums over the components it is given")
    ck.ok(any(c == "card" and "first paint" in i
              for c, i, _n, _v in costs_table("cga")),
          "the cost table carries a card first-paint row")

    # the generators
    tab = emit_optab()
    ck.ok(tab.count("\n    dw ") == 38, "--emit-optab emits 38 entries")
    ck.ok("wvm_aset" in tab and "0x25" in tab, "optab ends at 0x25 ASET")
    ft = emit_foldtab()
    ck.ok(ft.startswith("wv_foldtab:") and ft.count("db ") == 8,
          "--emit-foldtab: 128 entries from htmsim's one definition")
    ftc = emit_foldtab_c()
    ck.ok(ftc.count("0x") == 130 and "lm_foldtab[128]" in ftc,
          "--emit-foldtab-c: the same 128 entries, as C")


# --- the hostile corpus (WEAVE-SPEC 2.1, 10.4) -------------------------------
# Every byte read off a disk is hostile.  These build a real packed bundle,
# break exactly one thing in it, and assert the reader REFUSES with the field
# named - never a traceback.  Each row is a defect the model once accepted or
# crashed on; on the 8086 each is a wild read, a bad divide or a claim sized
# from a lie, so they stay here as a regression suite rather than a one-off.
def _wab_take(data):
    """A packed bundle taken apart: [[type, bytearray body, extra], ...]."""
    out = []
    for k in range(data[12]):
        typ, _z, ofs, ln, extra = struct.unpack_from("<BBHHH", data,
                                                     32 + 8 * k)
        out.append([typ, bytearray(data[ofs:ofs + ln]), extra])
    return out


def _wab_make(data, secs):
    """...and put back together, with the section table, the 16-byte
    padding and the total-size word recomputed (WEAVE-SPEC 2.3).  A corpus
    bundle must be malformed in exactly ONE place, or the reader refuses
    the scaffolding instead of the defect under test."""
    out = bytearray(data[:32])
    out[12] = len(secs)
    out += bytearray(8 * len(secs))
    for i, (typ, body, extra) in enumerate(secs):
        while len(out) % 16:
            out.append(0)
        struct.pack_into("<BBHHH", out, 32 + 8 * i, typ, 0, len(out),
                         len(body), extra)
        out += body
    struct.pack_into("<H", out, 6, len(out))
    return bytes(out)


def _sec(secs, typ):
    for row in secs:
        if row[0] == typ:
            return row
    raise KeyError(typ)


def _find_prop(props, name, start=0, stop=None):
    """The offset of the first 4-byte property record with this name atom.
    Blocks run back to back from PROPS+0 and the blobs follow all of them
    (WEAVE-SPEC 2.14 rule 5), so a 4-byte stride is the record grid."""
    for i in range(start, (len(props) if stop is None else stop) - 3, 4):
        if props[i] == name:
            return i
    raise KeyError(name)


def _find_rec(ui, ctype):
    for i in range(0, len(ui), 10):
        if ui[i] == REC_COMP and ui[i + 2] == ctype:
            return i
    raise KeyError(ctype)


RADIO_WML = ('<app name="Radio"><card id="m">'
             '<radio id="r" group="pick">One</radio>'
             '<radio id="r2" group="pick">Two</radio></card></app>')
LIST_WML = ('<app name="Lister"><card id="m"><list id="l">'
            '<item>Alpha</item><item>Beta</item></list></card></app>')
BOX_WML = '<app name="Boxy"><card id="m"><box w="4" h="2"/></card></app>'


def selfcheck_hostile(ck, tmp):
    dd = demo_dir()
    form = pack_project(os.path.join(dd, "form.wml")).data
    sheet = pack_project(os.path.join(dd, "sheet.wml")).data
    pong = pack_project(os.path.join(dd, "pong.wml")).data
    radio = pack_project(_proj(tmp, RADIO_WML, stem="radio")).data
    lister = pack_project(_proj(tmp, LIST_WML, stem="lister")).data
    boxy = pack_project(_proj(tmp, BOX_WML, stem="boxy")).data

    def hostile(base, mut, field, what):
        secs = _wab_take(base)
        mut(secs)
        d = _wab_make(base, secs)
        ck.raises(what, BundleError, lambda: Bundle(d, "X.WAB"),
                  contains="is not a Weave bundle (%s)." % field)

    def header(base, mut, field, what):
        d = bytearray(base)
        mut(d)
        ck.raises(what, BundleError, lambda: Bundle(bytes(d), "X.WAB"),
                  contains="is not a Weave bundle (%s)." % field)

    # UB-1..UB-4: ATOMS.  The pool is a table of offsets into itself -
    # every one of these was an IndexError or a UnicodeDecodeError.
    def ub1(secs):
        a = _sec(secs, SEC_ATOMS)[1]
        struct.pack_into("<H", a, 2, len(a) + 8)        # atom 64 past the end
    hostile(form, ub1, "atom pool", "UB-1 atom offset past the section")

    def ub2(secs):
        a = _sec(secs, SEC_ATOMS)[1]
        n = struct.unpack_from("<H", a, 0)[0]
        a[struct.unpack_from("<H", a, 2 + 2 * (n - 1))[0]] = 250
    hostile(form, ub2, "atom pool", "UB-2 atom length overruns the section")

    def ub3(secs):
        a = _sec(secs, SEC_ATOMS)[1]
        a[struct.unpack_from("<H", a, 2)[0] + 1] = 0xE9
    hostile(form, ub3, "atom pool", "UB-3 non-ASCII byte in an atom body")

    def ub4(secs):
        struct.pack_into("<H", _sec(secs, SEC_ATOMS)[1], 0, 150)
    hostile(form, ub4, "atom pool", "UB-4 atom count larger than the pool")

    # UB-5: a sprite record with no canvas before it - the model's own
    # canvas_cid was still None and the append raised KeyError.
    def ub5(secs):
        ui = _sec(secs, SEC_UISTREAM)[1]
        ui[12], ui[13], ui[14] = CTYPE["sprite"], 0, 0
    hostile(form, ub5, "sprite record", "UB-5 sprite with no canvas")

    # UB-6/UB-8/UB-16/UB-17: PROPS values that address nothing.
    def ub6(secs):
        p, app_at = _sec(secs, SEC_PROPS)[1], _sec(secs, SEC_PROPS)[2]
        for i in range(0, app_at, 4):
            if p[i + 1] == PK_SPRITE:
                struct.pack_into("<H", p, i + 2, 99)
                return
        raise KeyError("PK_SPRITE")
    hostile(pong, ub6, "sprite index", "UB-6 PK_SPRITE past the SPRITES table")

    def ub8(secs):
        p = _sec(secs, SEC_PROPS)[1]
        struct.pack_into("<H", p, _find_prop(p, WK["ITEMS"]) + 2, len(p) + 4)
    hostile(lister, ub8, "prop blob", "UB-8 ITEMS blob offset past PROPS")

    def ub16(secs):
        p, app_at = _sec(secs, SEC_PROPS)[1], _sec(secs, SEC_PROPS)[2]
        struct.pack_into("<H", p,
                         _find_prop(p, WK["MENUS"], app_at) + 2, len(p) + 4)
    hostile(form, ub16, "prop blob", "UB-16 MENUS blob offset past PROPS")

    def ub17(secs):
        p = _sec(secs, SEC_PROPS)[1]
        p[struct.unpack_from("<H", p,
                             _find_prop(p, WK["ITEMS"]) + 2)[0]] = 60
    hostile(lister, ub17, "list items", "UB-17 ITEMS count past the blob")

    # UB-7/UB-13..UB-15/UB-20: WEAVE-SPEC 3.3's required properties.
    def drop_prop(name, start=0):
        def mut(secs):
            p = _sec(secs, SEC_PROPS)[1]
            i = _find_prop(p, name, start)
            p[i:i + 4] = bytes(4)      # the block now terminates here
        return mut
    hostile(radio, drop_prop(WK["group"]), "radio group",
            "UB-7 radio with no group")
    hostile(sheet, drop_prop(WK["rows"]), "grid cols",
            "UB-13 grid with no cols/rows")

    def set_prop(name, val):
        def mut(secs):
            p = _sec(secs, SEC_PROPS)[1]
            struct.pack_into("<H", p, _find_prop(p, name) + 2, val)
        return mut
    hostile(sheet, set_prop(WK["rows"], 60000), "grid rows",
            "UB-14 grid rows = 60000")
    hostile(sheet, set_prop(WK["cols"], 0), "grid cols",
            "UB-15 grid cols = 0")
    hostile(form, set_prop(WK["max"], 0), "meter max",
            "UB-20 meter max = 0")

    # UB-9/UB-10: a section emptied while its count word still lies.
    def empty(typ):
        def mut(secs):
            _sec(secs, typ)[1] = bytearray()
        return mut
    hostile(pong, empty(SEC_SPRITES), "sprite count",
            "UB-9 empty SPRITES, count 2")
    hostile(sheet, empty(SEC_FXCODE), "formula count",
            "UB-10 empty FXCODE, count 4")

    # UB-11: a CELLS record naming a formula that is not in FXCODE.
    def ub11(secs):
        c = _sec(secs, SEC_CELLS)[1]
        for i in range(0, len(c), 8):
            if c[i + 2] == 3:
                struct.pack_into("<H", c, i + 4, 99)
                return
        raise KeyError("formula cell")
    hostile(sheet, ub11, "cell record", "UB-11 cell formula index past FXCODE")

    # UB-18/UB-19: a geometry byte WEAVE-SPEC 2.5/3.3 says is never 0 -
    # a divide, or a negative repeat count, on the machine.
    def zero_geom(ctype, off):
        def mut(secs):
            ui = _sec(secs, SEC_UISTREAM)[1]
            ui[_find_rec(ui, ctype) + off] = 0
        return mut
    hostile(pong, zero_geom(CTYPE["canvas"], 3), "canvas w",
            "UB-18 canvas w = 0")
    hostile(pong, zero_geom(CTYPE["canvas"], 4), "canvas h",
            "UB-18 canvas h = 0")
    hostile(boxy, zero_geom(CTYPE["box"], 3), "box w", "UB-19 box w = 0")
    hostile(boxy, zero_geom(CTYPE["box"], 4), "box h", "UB-19 box h = 0")

    # UB-12: the CLI's own 1-based/0-based seam (WEAVE-SPEC 2.5).
    wab = os.path.join(tmp, "CARDS.WAB")
    open(wab, "wb").write(form)
    for n in (0, 9):
        ck.raises("UB-12 --card %d refuses with the range" % n, SystemExit,
                  cmd_render, wab, ["cga"], n, contains="numbered 1..")

    # The header's three claim-KB bytes ARE 10.1's memory refusal.
    header(form, lambda d: d.__setitem__(10, 15), "vm KB", "vm KB below 16")
    header(form, lambda d: d.__setitem__(11, 4), "grid KB",
           "grid KB outside 0 or 8..26")
    header(pong, lambda d: d.__setitem__(14, 9), "canvas KB",
           "canvas KB outside 0 or 2..8")
    header(form, lambda d: d.__setitem__(12, 4), "section count",
           "fewer than the four mandatory sections plus ICON")
    header(form, lambda d: d.__setitem__(13, 0), "entry card",
           "entry card 0")
    header(form, lambda d: d.__setitem__(16, 0), "app name",
           "an empty app name")

    # 2.3: sections abut at align16, padding is 0x00, the file ends at the
    # last section's UNPADDED end.
    header(form, lambda d: d.__setitem__(203, 1), "section padding",
           "a non-zero inter-section padding byte")
    padded = bytearray(form) + bytes(16)
    struct.pack_into("<H", padded, 6, len(padded))   # a consistent lie
    ck.raises("tail padding after the last section", BundleError,
              lambda: Bundle(bytes(padded), "X.WAB"),
              contains="(total size)")

    def shift(secs):
        _sec(secs, SEC_ICON)[1] = bytearray(63)
    hostile(form, shift, "ICON", "ICON that is not 64 bytes")

    # 2.5/2.14 rule 2: comp_ids are document order.
    def outoforder(secs):
        ui = _sec(secs, SEC_UISTREAM)[1]
        ui[11] = 7
    hostile(form, outoforder, "comp_id", "a comp_id out of document order")

    # 2.4/2.2.1: the flag/section couplings, both directions.
    header(form, lambda d: d.__setitem__(8, d[8] | WABF_SOURCE), "flags",
           "WABF_SOURCE with no SOURCE section")
    header(sheet, lambda d: d.__setitem__(8, d[8] & ~WABF_GRID), "flags",
           "FXCODE and CELLS with WABF_GRID clear")

    # 2.8: the function table bounds its own bodies.
    def badfn(secs):
        c = _sec(secs, SEC_CODE)[1]
        struct.pack_into("<H", c, 2, len(c) + 4)
    hostile(form, badfn, "function table", "a function offset past CODE")


def run_selfcheck(verbose=False):
    ck, tmp = selfcheck(verbose)
    sizes = selfcheck_demos(ck, tmp)
    selfcheck_vm(ck, tmp)
    selfcheck_hostile(ck, tmp)
    if ck.fails:
        print("selfcheck: %d of %d checks FAILED" % (len(ck.fails),
                                                     ck.count))
        return 1
    print("selfcheck PASS: %d checks, demos %s"
          % (ck.count, ", ".join("%s.WAB %d bytes" % (s.upper(), n)
                                 for s, n in sorted(sizes.items()))))
    return 0


# --- main --------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(
        description="Weave's host reference implementation "
                    "(docs/WEAVE-SPEC.md)")
    ap.add_argument("--pack", metavar="WML_OR_DIR",
                    help="compile a project to a .WAB bundle")
    ap.add_argument("-o", metavar="OUT", help="output bundle path")
    ap.add_argument("--with-source", action="store_true",
                    help="carry the SOURCE section (WEAVE-SPEC 2.13)")
    ap.add_argument("--run", metavar="WAB", help="run a bundle headlessly")
    ap.add_argument("--events", metavar="FILE",
                    help="scripted events for --run: click/set/change/key/"
                         "select/edit/command/tick/dump")
    ap.add_argument("--src", metavar="WML",
                    help="the bundle's source, for the id map")
    ap.add_argument("--render", metavar="WML_OR_WAB",
                    help="draw the flow-walk layout as text")
    ap.add_argument("--card", type=int, help="card to render (default: "
                    "the entry card)")
    ap.add_argument("--adapter", default="cga",
                    help="cga|herc|vga or 640x200|720x348|640x480")
    ap.add_argument("--all-adapters", action="store_true")
    ap.add_argument("--costs", action="store_true",
                    help="print WEAVE-SPEC 14's component cost table")
    ap.add_argument("--emit-optab", action="store_true",
                    help="print the 38-entry WVM jump table for wvm.inc")
    ap.add_argument("--emit-foldtab", action="store_true",
                    help="print the Latin-1 fold table for LOOM.OVL")
    ap.add_argument("--emit-foldtab-c", action="store_true",
                    help="...and the same table as a C initialiser, for "
                         "apps/loom's compilers (WEAVE-SPEC 3.1)")
    ap.add_argument("--emit-vmcorpus", metavar="DIR",
                    help="the WVM differential corpus for weavevm "
                         "(WEAVE-SPEC 12.1.1)")
    ap.add_argument("--render-after", action="store_true",
                    help="--run: print the CARD when the events are spent")
    ap.add_argument("--emit-cvcorpus", action="store_true",
                    help="the CANVAS differential corpus for weavecv "
                         "(WEAVE-SPEC 12.1.3)")
    ap.add_argument("--emit-fxcorpus", metavar="DIR",
                    help="the FX differential corpus for weavevm "
                         "(WEAVE-SPEC 12.1.2)")
    ap.add_argument("--selfcheck", action="store_true")
    ap.add_argument("--verbose", "-v", action="store_true")
    args = ap.parse_args()

    adapter = GEOM_ALIAS.get(args.adapter, args.adapter)
    if adapter not in ADAPTERS:
        ap.error("no such adapter %r" % args.adapter)
    adapters = sorted(ADAPTERS) if args.all_adapters else [adapter]

    try:
        if args.selfcheck:
            return run_selfcheck(args.verbose)
        if args.emit_optab:
            print(emit_optab())
            return 0
        if args.emit_foldtab:
            print(emit_foldtab())
        if args.emit_foldtab_c:
            print(emit_foldtab_c())
            return 0
        if args.emit_vmcorpus:
            text = emit_vmcorpus(args.emit_vmcorpus)
            if args.o:
                open(args.o, "w").write(text + "\n")
            else:
                print(text)
            return 0
        if args.emit_cvcorpus:
            text = emit_cvcorpus()
            if args.o:
                open(args.o, "w").write(text + "\n")
            else:
                print(text)
            return 0
        if args.emit_fxcorpus:
            text = emit_fxcorpus(args.emit_fxcorpus)
            if args.o:
                open(args.o, "w").write(text + "\n")
            else:
                print(text)
            return 0
        if args.emit_vmcorpus:
            text = emit_vmcorpus(args.emit_vmcorpus)
            if args.o:
                open(args.o, "w").write(text + "\n")
            else:
                print(text)
            return 0
        if args.emit_cvcorpus:
            text = emit_cvcorpus()
            if args.o:
                open(args.o, "w").write(text + "\n")
            else:
                print(text)
            return 0
        if args.emit_fxcorpus:
            text = emit_fxcorpus(args.emit_fxcorpus)
            if args.o:
                open(args.o, "w").write(text + "\n")
            else:
                print(text)
            return 0
        if args.costs:
            print_costs(adapter)
            return 0
        if args.pack:
            cmd_pack(args.pack, args.o, args.with_source)
            return 0
        if args.run:
            if not args.events:
                ap.error("--run needs --events")
            cmd_run(args.run, args.events, args.src, adapter,
                    args.render_after)
            return 0
        if args.render:
            cmd_render(args.render, adapters, args.card)
            return 0
    except PackError as ex:
        print(ex, file=sys.stderr)
        return 1
    except BundleError as ex:
        print(ex, file=sys.stderr)
        return 1
    ap.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())
