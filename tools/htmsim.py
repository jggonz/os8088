#!/usr/bin/env python3
"""A host-side model of the os8088 browser's parse, layout and cost.

docs/BROWSER-PLAN.md is the design; this is that design run against real
pages before a byte of 8086 is written, for the reason PERFORMANCE.md
19.2.3.1 exists: the obvious refinement to the sector cache was MEASURED and
found wrong, and the negative result was cheaper than the code. The same
applies here to the layout-table heuristic (3.2.1) and to every cost estimate
in 1, all of which were arithmetic on a page nobody had parsed.

What it models, faithfully enough to be a reference the assembly is checked
against:

  * the parse (2.2)   - a byte-at-a-time state machine over a tag allowlist,
                        whitespace collapse, entity and charset folding to
                        ASCII 0x20..0x7E, colour dropped in both directions
                        (2.2.1);
  * the heuristic (3.2.1) - a 1x1 table is a block and a table with no text is
                        decoration;
  * the layout (2.3)  - greedy wrap on a character grid into a LINE TABLE, one
                        entry per display line, tables measured as an atomic
                        unit;
  * the cost (0, 1)   - PERFORMANCE.md Part 2's measured constants, so a page
                        can be priced in milliseconds rather than guessed at.

What it deliberately does NOT model: the pixels. It answers how many glyph
cells and how many drawing calls, which is what Part 2 says a redraw is priced
by. Anything about the framebuffer is the assembly's business.

  python3 tools/htmsim.py tests/htm/*.htm            # the cost table
  python3 tools/htmsim.py --render tests/htm/demo.htm  # what it would look like
  python3 tools/htmsim.py --adapter herc --render tests/htm/torture.htm
"""
import argparse
import html.entities
import re
import sys

# --- PERFORMANCE.md Part 2, measured on the field 5150 -----------------------
# A drawing call's fixed part, and a glyph cell. The row figure is DERIVED from
# these two and checked against the measured one in self_check() - if that
# check ever fails, the model has drifted from the machine.
CALL_US = 756.0
ADAPTERS = {
    #          cell us, cols, rows, screen w, content w in bytes
    "cga":  dict(cell=918.0, cols=79, rows=17, name="CGA 640x200"),
    "herc": dict(cell=905.0, cols=89, rows=36, name="Hercules 720x348"),
    "vga":  dict(cell=620.0, cols=79, rows=52, name="VGA 640x480 mode 12h"),
}
# gfx_scroll: 400 us per row for a 32-byte-wide rect (PERFORMANCE.md Set 54).
# Scaled linearly by width, which OVERSTATES it - the per-row cost has a fixed
# part this cannot separate without a second measured width. Flagged wherever
# it is printed; tests/gfxbench is what settles it.
SCROLL_US_PER_ROW_32B = 400.0

MAXCOL = 0x7E                    # the cell font is ASCII 0x20..0x7E (SPEC 6.1)

# --- 2.2: folding anything else to ASCII ------------------------------------
# ONE definition, and apps/browser/browser.asm's br_l1tab is GENERATED from it
# by --emit-l1tab. That matters more than it looks: the model is only a
# reference if it folds identically, and two hand-maintained 128-entry tables
# would have drifted on the first accented letter nobody checked.
#
# Folds are SINGLE-BYTE. A multi-character fold ("1/2", "(c)") would make the
# emit variable-length on the 8086 side for a handful of glyphs nobody misses.
# 0 means DROP the character entirely, and exactly two things do: the CP1252
# slots that really are control codes, and the SOFT HYPHEN, which is a hint
# about where a word MAY break and not a character.
FOLD = {}
# CP1252's 0x80..0x9F: Latin-1 calls them control, and real pages mean
# punctuation there - curly quotes and dashes above all.
for cp, ch in ((0x80, "?"), (0x82, ","), (0x83, "f"), (0x84, '"'), (0x85, "."),
               (0x86, "+"), (0x87, "+"), (0x88, "^"), (0x89, "%"), (0x8A, "S"),
               (0x8B, "<"), (0x8C, "O"), (0x8E, "Z"), (0x91, "'"), (0x92, "'"),
               (0x93, '"'), (0x94, '"'), (0x95, "*"), (0x96, "-"), (0x97, "-"),
               (0x98, "~"), (0x99, "?"), (0x9A, "s"), (0x9B, ">"), (0x9C, "o"),
               (0x9E, "z"), (0x9F, "Y")):
    FOLD[cp] = ch
FOLD[0xA0] = " "
FOLD[0xAD] = ""                      # soft hyphen: a hint, not a character
for cp, ch in ((0xA1, "!"), (0xA2, "c"), (0xA6, "|"), (0xA8, '"'), (0xA9, "c"),
               (0xAA, "a"), (0xAB, "<"), (0xAE, "R"), (0xAF, "-"), (0xB0, "o"),
               (0xB2, "2"), (0xB3, "3"), (0xB4, "'"), (0xB5, "u"), (0xB7, "."),
               (0xB8, ","), (0xB9, "1"), (0xBA, "o"), (0xBB, ">"), (0xD7, "x"),
               (0xF7, "/")):
    FOLD[cp] = ch
# Accented letters fold to their base letter: built rather than typed, because
# 60 hand-written rows is 60 chances to get one wrong.
for base, lo, hi in (("A", 0xC0, 0xC5), ("E", 0xC8, 0xCB), ("I", 0xCC, 0xCF),
                     ("O", 0xD2, 0xD6), ("U", 0xD9, 0xDC), ("a", 0xE0, 0xE5),
                     ("e", 0xE8, 0xEB), ("i", 0xEC, 0xEF), ("o", 0xF2, 0xF6),
                     ("u", 0xF9, 0xFC)):
    for cp in range(lo, hi + 1):
        FOLD.setdefault(cp, base)
for cp, ch in ((0xC6, "A"), (0xC7, "C"), (0xD0, "D"), (0xD1, "N"), (0xD8, "O"),
               (0xDD, "Y"), (0xDE, "T"), (0xDF, "s"), (0xE6, "a"), (0xE7, "c"),
               (0xF0, "d"), (0xF1, "n"), (0xF8, "o"), (0xFD, "y"), (0xFE, "t"),
               (0xFF, "y")):
    FOLD.setdefault(cp, ch)
# Everything else in Latin-1's printable range says "something was here".
for cp in range(0xA0, 0x100):
    FOLD.setdefault(cp, "?")
# Above Latin-1: what a proxy really emits. Anything else is '?', which is
# honest - no table buys the cell font a Cyrillic glyph.
WIDE = {0x2018: "'", 0x2019: "'", 0x201C: '"', 0x201D: '"', 0x2013: "-",
        0x2014: "-", 0x2026: ".", 0x2022: "*", 0x2039: "<", 0x203A: ">"}
FOLD.update(WIDE)


def emit_l1tab():
    """The nasm form of FOLD's 0x80..0xFF half, for apps/browser/browser.asm."""
    out = ["br_l1tab:"]
    for row in range(0x80, 0x100, 16):
        vals = []
        for cp in range(row, row + 16):
            ch = FOLD.get(cp, "")
            vals.append("0" if not ch else ("0x27" if ch == "'" else "'%s'" % ch))
        out.append("    db %s   ; %02X" % (",".join("%4s" % v for v in vals), row))
    return "\n".join(out)


def fold(ch):
    """One decoded character -> what the cell font can actually draw."""
    cp = ord(ch)
    if 0x20 <= cp <= MAXCOL:
        return ch
    if ch in "\n\t\r":
        return ch
    return FOLD.get(cp, "?")


# --- the parse ---------------------------------------------------------------
BLOCK = {"p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "li",
         "pre", "table", "tr", "hr", "center", "blockquote", "form"}
DROP_CONTENT = {"script", "style", "head", "title", "select", "textarea"}
HEADING = {"h1": 1, "h2": 2, "h3": 3, "h4": 4, "h5": 5, "h6": 6}

TAG_RE = re.compile(rb"<(/?)([A-Za-z][A-Za-z0-9]*)((?:[^>\"']|\"[^\"]*\"|'[^']*')*)>")
ATTR_RE = re.compile(r"""([A-Za-z-]+)\s*=\s*("[^"]*"|'[^']*'|[^\s>]*)""")


def decode(raw):
    """Bytes -> text, honouring the page's own declared charset (1.1.2)."""
    head = raw[:2048].decode("ascii", "replace").lower()
    m = re.search(r"charset\s*=\s*[\"']?([a-z0-9-]+)", head)
    enc = (m.group(1) if m else "iso-8859-1")
    if enc in ("utf-8", "utf8"):
        return raw.decode("utf-8", "replace"), "utf-8"
    return raw.decode("latin-1"), enc


def unescape(text):
    """Entities -> folded ASCII, and **every other character folded too**.

    Folding only the entity expansions is a bug the renderer would have
    shipped: an entity is the case an author thinks about, and a page whose
    charset carries the accent as a RAW BYTE - which is most of the German
    web, and torture.htm section 7 - would have handed 0xF6 straight to a
    glyph table that has 95 entries. A malformed entity is literal text,
    never an error and never a swallowed line (section 6)."""
    out = []
    i = 0
    while True:
        amp = text.find("&", i)
        if amp < 0:
            out.append("".join(fold(c) for c in text[i:]))
            break
        out.append("".join(fold(c) for c in text[i:amp]))
        semi = text.find(";", amp, amp + 12)
        done = False
        if semi > 0:
            body = text[amp + 1:semi]
            if body.startswith("#"):
                try:
                    cp = int(body[2:], 16) if body[1:2] in "xX" else int(body[1:])
                    out.append(fold(chr(cp)) if cp else "")
                    done = True
                except ValueError:
                    pass
            elif body in html.entities.html5 or body + ";" in html.entities.html5:
                key = body + ";" if body + ";" in html.entities.html5 else body
                out.append("".join(fold(c) for c in html.entities.html5[key]))
                done = True
        if done:
            i = semi + 1
        else:
            out.append("&")
            i = amp + 1
    return "".join(out)


class Cell:
    def __init__(self, colspan=1, header=False):
        self.ops, self.colspan, self.header = [], colspan, header

    def text_len(self):
        return sum(len(o[1]) for o in self.ops if o[0] == "RUN")


class Table:
    def __init__(self):
        self.rows = []

    def ncols(self):
        return max((sum(c.colspan for c in r) for r in self.rows), default=0)

    def ncells(self):
        return sum(len(r) for r in self.rows)

    def has_text(self):
        return any(c.text_len() for r in self.rows for c in r)


def parse(raw):
    """-> (ops, stats). ops is the flat marker/run stream of BROWSER-PLAN 2.2,
    with Table objects standing in for the atomic table unit of 3.2."""
    text, enc = decode(raw)
    ops = []
    stack = []            # open Table objects; a nested table pushes
    style = {"b": 0, "i": 0, "link": 0}
    pre = 0
    st = dict(enc=enc, tags=set(), dropped=set(), links=0, imgs=0, ents=0,
              bad_ents=0, unclosed=0)

    def emit(op):
        (stack[-1].rows[-1][-1].ops if stack and stack[-1].rows
         and stack[-1].rows[-1] else ops).append(op)

    def add_text(s):
        if not s:
            return
        if pre:
            emit(("RUN", s, dict(style), True))
            return
        s = re.sub(r"\s+", " ", s)      # collapsed, but LEADING and TRAILING
        emit(("RUN", s, dict(style), False))   # spaces are kept: they are the
                                               # only record of whether the
                                               # source had a break here

    raw_b = text.encode("latin-1", "replace") if enc != "utf-8" else None
    pos, n = 0, len(text)
    while pos < n:
        lt = text.find("<", pos)
        if lt < 0:
            add_text(unescape(text[pos:]))
            break
        add_text(unescape(text[pos:lt]))
        # comments and doctype: skipped whole, contents never parsed
        if text.startswith("<!--", lt):
            end = text.find("-->", lt)
            pos = n if end < 0 else end + 3
            continue
        if text.startswith("<!", lt):
            end = text.find(">", lt)
            pos = n if end < 0 else end + 1
            continue
        m = TAG_RE.match(text.encode("latin-1", "replace"), lt) if raw_b is None else None
        mt = re.match(r"<(/?)([A-Za-z][A-Za-z0-9]*)((?:[^>\"']|\"[^\"]*\"|'[^']*')*)>",
                      text[lt:lt + 4096])
        if not mt:
            add_text("<")
            pos = lt + 1
            continue
        closing, tag, attrs = bool(mt.group(1)), mt.group(2).lower(), mt.group(3)
        pos = lt + mt.end()
        st["tags"].add(tag)

        if tag in DROP_CONTENT:
            if not closing:
                end = re.search(r"</\s*%s\s*>" % tag, text[pos:], re.I)
                st["dropped"].add(tag)
                pos = n if not end else pos + end.end()
            continue

        a = {k.lower(): v.strip("\"'") for k, v in ATTR_RE.findall(attrs)}

        if tag == "table":
            if closing:
                if stack:
                    t = stack.pop()
                    emit(("TABLE", t))
            else:
                stack.append(Table())
        elif tag == "tr" and not closing:
            if stack:
                stack[-1].rows.append([])
        elif tag in ("td", "th") and not closing:
            if stack:
                if not stack[-1].rows:
                    stack[-1].rows.append([])
                try:
                    cs = max(1, min(16, int(a.get("colspan", "1"))))
                except ValueError:
                    cs = 1
                stack[-1].rows[-1].append(Cell(cs, tag == "th"))
        elif tag == "pre":
            pre = 0 if closing else 1
            emit(("PARA",))
        elif tag in HEADING:
            emit(("PARA",) if closing else ("HEAD", HEADING[tag]))
        elif tag == "br":
            emit(("BR",))
        elif tag == "hr":
            emit(("HR",))
        elif tag == "li" and not closing:
            emit(("LI", 1))
        elif tag == "center":
            emit(("CENTRE", not closing))
        elif tag == "img" and not closing:
            st["imgs"] += 1
            alt = a.get("alt", "").strip()
            if alt:
                add_text(unescape(alt))
        elif tag == "a":
            if closing:
                style["link"] = max(0, style["link"] - 1)
            elif "href" in a:
                style["link"] += 1
                st["links"] += 1
                emit(("HREF", a["href"]))
        elif tag in ("b", "strong"):
            style["b"] += -1 if closing else 1
        elif tag in ("i", "em", "cite", "var"):
            style["i"] += -1 if closing else 1
        elif tag in BLOCK:
            emit(("PARA",))
        # font, small, tt, span, big, style=, color=, bgcolor= : 2.2.1, dropped

    st["unclosed"] = max(0, style["b"]) + max(0, style["i"])
    while stack:                       # an unclosed <table> still renders
        emit(("TABLE", stack.pop()))
    return ops, st


# --- 3.2.1: most tables are not tables ---------------------------------------
def simplify(ops, st=None):
    """1x1 -> a block; no text in any cell -> decoration. Recursive, so a
    nested pair collapses to nothing (3.2's flattening rule)."""
    out, kept, boxed, dropped = [], 0, 0, 0
    for op in ops:
        if op[0] != "TABLE":
            out.append(op)
            continue
        t = op[1]
        for r in t.rows:
            for c in r:
                c.ops, k, b, d = simplify(c.ops)
                kept, boxed, dropped = kept + k, boxed + b, dropped + d
        if not t.has_text():
            dropped += 1
            continue
        if t.ncells() == 1 and t.ncols() == 1:
            boxed += 1
            out.append(("PARA",))
            out.extend(t.rows[0][0].ops)
            out.append(("PARA",))
            continue
        kept += 1
        out.append(op)
    return out, kept, boxed, dropped


# --- 2.3: the line table ------------------------------------------------------
class Line:
    __slots__ = ("cells", "x", "kind")

    def __init__(self, cells, x=0, kind="text"):
        self.cells, self.x, self.kind = cells, x, kind


def wrap(words, width):
    """Greedy, and a word longer than the line is hard-split - torture.htm
    section 1. Returns a list of strings."""
    lines, cur = [], ""
    for w in words:
        while len(w) > width:
            if cur:
                lines.append(cur)
                cur = ""
            lines.append(w[:width])
            w = w[width:]
        if not cur:
            cur = w
        elif len(cur) + 1 + len(w) <= width:
            cur += " " + w
        else:
            lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    return lines


def cell_text(ops):
    return "".join(o[1] for o in ops if o[0] == "RUN").strip()


def layout_table(t, width, out):
    """A table is the ATOMIC unit of layout (3.2): one measuring pass over the
    parsed cells, integer arithmetic and not one glyph, then rows."""
    ncols = min(t.ncols(), 16)
    if not ncols:
        return
    want = [0] * ncols
    for r in t.rows:
        col = 0
        for c in r:
            if col < ncols and c.colspan == 1:
                want[col] = max(want[col], min(len(cell_text(c.ops)), width))
            col += c.colspan
    # The DRAWN width is sum(w) + 2*ncols + 1 - a '|' and a space either side
    # of every column and a '+' at each edge - so that is what has to fit, not
    # the sum alone. The first version subtracted ncols+1 and could overflow
    # the band by ncols; apps/browser/browser.asm's br_tfit uses this one.
    avail = width - 2 * ncols - 1
    if avail < ncols:
        avail = max(ncols, width)
    # The longest single WORD in each column is its natural floor: squeezing
    # below it splits words, which rendered "Storage" as "Stora|ge".
    floor = [0] * ncols
    for r in t.rows:
        col = 0
        for c in r:
            if col < ncols and c.colspan == 1:
                for w in cell_text(c.ops).split():
                    floor[col] = max(floor[col], min(len(w), 14))
            col += c.colspan
    total = sum(want) or 1
    if total > avail:                            # squeeze proportionally...
        want = [max(1, w * avail // total) for w in want]
        for i in range(ncols):                   # ...but not through a word
            want[i] = max(want[i], min(floor[i], avail // ncols + floor[i]))
        while sum(want) > avail:                 # give the slack back from the
            i = want.index(max(want))            # widest column, never a floor
            if want[i] <= 1:
                break
            want[i] -= 1
    rule = "+" + "+".join("-" * (w + 1) for w in want) + "+"
    out.append(Line(rule, 0, "rule"))
    for r in t.rows:
        wrapped, col = [], 0
        for c in r:
            span = sum(want[col:col + c.colspan]) + (c.colspan - 1) * 2
            wrapped.append(wrap(cell_text(c.ops).split(), max(1, span)) or [""])
            col += c.colspan
        for k in range(max(len(w) for w in wrapped)):
            col, parts = 0, []
            for ci, c in enumerate(r):
                span = sum(want[col:col + c.colspan]) + (c.colspan - 1) * 2
                parts.append((wrapped[ci][k] if k < len(wrapped[ci]) else "").ljust(span))
                col += c.colspan
            out.append(Line("|" + "|".join(" " + p for p in parts) + "|", 0, "row"))
        out.append(Line(rule, 0, "rule"))


def layout(ops, width):
    """-> the line table. One entry per display line (2.3)."""
    out, pending, centre, indent = [], [], 0, 0

    def flush():
        nonlocal pending
        # Concatenate FIRST and split into words after, so an inline tag
        # boundary with no whitespace across it does not become one.
        words = "".join(pending).split()
        pending = []
        if not words:
            return
        for ln in wrap(words, width - indent):
            s = " " * indent + ln
            if centre and len(s) < width:
                s = " " * ((width - len(ln)) // 2) + ln
            out.append(Line(s))

    for op in ops:
        k = op[0]
        if k == "RUN":
            if op[3]:                                   # PRE: verbatim
                flush()
                for ln in op[1].split("\n"):
                    out.append(Line(ln[:width * 4], 0, "pre"))
            else:
                pending.append(op[1])
        elif k in ("PARA", "BR"):
            flush()
            if k == "PARA" and out and out[-1].kind != "blank":
                out.append(Line("", 0, "blank"))
        elif k == "HEAD":
            flush()
            if out:
                out.append(Line("", 0, "blank"))
        elif k == "HR":
            flush()
            out.append(Line("-" * width, 0, "rule"))
        elif k == "LI":
            flush()
            pending.append(" * ")
        elif k == "CENTRE":
            flush()
            centre = op[1]
        elif k == "TABLE":
            flush()
            layout_table(op[1], width, out)
        elif k == "HREF":
            pass
    flush()
    while out and out[-1].kind == "blank":
        out.pop()
    return out


# --- 0/1: what it costs -------------------------------------------------------
def cost_line(line, cell_us, width):
    """One display line is ONE opaque font_run **space-padded to the full band**
    (2.4) - the padding IS the erase, which is what stops the row being blank
    while it is drawn. So a line costs the BAND, not its ink, and a short line
    is not a cheap one."""
    return CALL_US + width * cell_us


def cost_ink(line, cell_us):
    """What the same line would cost if a row remembered how far it was last
    drawn and padded only to that - Missile Command's mc_srun (SPEC.md 48.17).
    It is a FLOOR and not an alternative: a row a blit has just exposed holds
    undefined pixels and must be erased to the band whatever it now says."""
    return CALL_US + len(line.cells) * cell_us


def report(path, adapter, render=False, verbose=False):
    raw = open(path, "rb").read()
    ops, st = parse(raw)
    ops, kept, boxed, dropped = simplify(ops)
    A = ADAPTERS[adapter]
    lines = layout(ops, A["cols"])
    rows = A["rows"]
    cell_us = A["cell"]

    cols = A["cols"]
    page = sum(cost_line(l, cell_us, cols) for l in lines[:rows])
    ink = sum(cost_ink(l, cell_us) for l in lines[:rows])
    one = cost_line(lines[0], cell_us, cols) if lines else 0.0
    scroll_us = rows * SCROLL_US_PER_ROW_32B * (A["cols"] / 32.0)
    below = max(0, len(lines) - rows)

    print("%-26s %-22s %6d bytes  %s" % (path.split("/")[-1], A["name"],
                                         len(raw), st["enc"]))
    print("   parse    %d ops, %d links, %d images, %d tags"
          % (len(ops), st["links"], st["imgs"], len(st["tags"])))
    if st["dropped"]:
        print("            dropped whole: %s" % ", ".join(sorted(st["dropped"])))
    print("   tables   %d real, %d collapsed to a block, %d dropped as decoration"
          % (kept, boxed, dropped))
    print("   layout   %d display lines, %d fit the window, %d below it (%.0f%%)"
          % (len(lines), min(rows, len(lines)), below,
             100.0 * below / max(1, len(lines))))
    print("   cost     first line %6.1f ms   |   one window %7.1f ms   (ink only %6.1f ms, %.0f%%)"
          % (one / 1000.0, page / 1000.0, ink / 1000.0, 100.0 * ink / max(1.0, page)))
    print("            scroll one line: blit %5.1f ms + %5.1f ms of text = %6.1f ms  [blit is a MODEL]"
          % (scroll_us / 1000.0, one / 1000.0, (scroll_us + one) / 1000.0))
    if page:
        print("            a full repaint is %.1fx the cost of scrolling one line"
              % (page / (scroll_us + one)))
    if verbose and st["unclosed"]:
        print("   note     %d inline tag(s) left unclosed at EOF (handled)"
              % st["unclosed"])
    if render:
        print("   " + "-" * (A["cols"] + 2))
        for i, l in enumerate(lines):
            mark = "|" if i < rows else ":"
            print("  %s%s" % (mark, l.cells))
        print("   " + "-" * (A["cols"] + 2))
    print()
    return lines


def self_check():
    """The model's constants against PERFORMANCE.md's MEASURED row figure. If
    this drifts, the cost model has stopped describing the machine."""
    ok = True
    for ad, measured in (("herc", 71400.0), ("cga", 72700.0)):
        got = CALL_US + 78 * ADAPTERS[ad]["cell"]
        err = abs(got - measured) / measured
        flag = "ok " if err < 0.02 else "OFF"
        print("selfcheck %s  78-cell row: model %.1f ms vs measured %.1f ms  (%.1f%%) %s"
              % (flag, got / 1000.0, measured / 1000.0, 100 * err, ad))
        ok = ok and err < 0.02
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="*")
    ap.add_argument("--adapter", default="cga", choices=sorted(ADAPTERS))
    ap.add_argument("--all-adapters", action="store_true")
    ap.add_argument("--render", action="store_true")
    ap.add_argument("--verbose", "-v", action="store_true")
    ap.add_argument("--selfcheck", action="store_true")
    ap.add_argument("--emit-l1tab", action="store_true",
                    help="print br_l1tab for apps/browser/browser.asm")
    args = ap.parse_args()

    if args.emit_l1tab:
        print(emit_l1tab())
        return 0
    if args.selfcheck or not args.files:
        ok = self_check()
        if not args.files:
            return 0 if ok else 1
        print()
    for f in args.files:
        for ad in (sorted(ADAPTERS) if args.all_adapters else [args.adapter]):
            report(f, ad, args.render, args.verbose)
    return 0


if __name__ == "__main__":
    sys.exit(main())
