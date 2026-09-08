#!/usr/bin/env python3
"""A reformatting HTTP proxy for the os8088 browser (docs/plans/completed/PROXY-PLAN.md).

It runs on the OWNER'S machine - Windows, macOS or Linux, python3 and the
standard library and nothing else - terminates TLS, and hands os8088 a page
in the only dialect apps/browser/browser.asm can read.

WHY IT EXISTS, in one paragraph each:

  * **HTTPS is arithmetic this machine cannot do.** SPEC.md 71.2: an RSA-2048
    private operation is minutes on a 4.77MHz 8088, so `br_split` refuses the
    scheme by name. Everything on the modern web is behind it. The proxy is
    where the handshake happens.
  * **A modern page is 200KB of markup that renders to two screens of text**,
    and the cable moves 3,741 bytes a second (PERFORMANCE.md Set 39). Stripping
    is worth more than any transport change available.
  * **The browser has HARD LIMITS and exceeding one is SILENT.** A path over
    95 bytes is truncated by `br_split` and fetches the wrong page; a body over
    32KB is cut mid-tag; a page past 200 links stops recording anchors. The
    proxy is the only place that can guarantee none of that happens, so it
    checks its own output against the guest's own constants - read out of
    apps/browser at --selfcheck rather than copied here and left to drift.

WHAT IT IS NOT. It is not the renderer and must not become one
(BROWSER-PLAN 8.3.1): the browser parses HTML itself, because on the Ethernet
path there may be no proxy at all and a browser that renders one server's
dialect is not a browser. This is an accelerator and a TLS bridge.

Python 3.8 or newer, standard library only - no pip, no requests, no
BeautifulSoup, for the reason the rest of tools/ has none: a proxy that needs
a package manager is one that stops working on the machine it was installed on.

  python3 tools/os88proxy.py                     # serve on 0.0.0.0:8088
  python3 tools/os88proxy.py --selfcheck         # the gate: no network needed
  python3 tools/os88proxy.py --render URL        # what os8088 would receive
  python3 tools/os88proxy.py --cost URL          # ...and what it costs to draw

On os8088: Browser > Open Location, type `<host>:8088` and press Return.
"""
import argparse
import base64
import calendar
import gzip
import hashlib
import html.parser
import io
import json
import os
import re
import socket
import ssl
import sys
import threading
import time
import traceback
import urllib.error
import urllib.parse
import urllib.request
import zlib
from http.cookiejar import MozillaCookieJar
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# =============================================================================
# 1. THE GUEST'S LIMITS
#
# Every one of these is a constant in apps/browser, and every one of them fails
# SILENTLY when it is exceeded - the page renders, it just renders the wrong
# thing. They are copied here because the proxy must be runnable from a folder
# on a Windows machine with no repo in it, and --selfcheck re-reads the
# assembly and refuses to agree with itself if a number has moved.
# =============================================================================
LIM = {
    "BR_HOSTMAX": 64,     # brnet.inc - the host buffer, NUL included
    "BR_PATHMAX": 96,     # brnet.inc - THE request path. 95 usable bytes.
    "BR_URLMAX": 160,     # browser.asm - the composed URL / location bar
    "BR_UBUF": 160,       # browser.asm - br_resolve's output buffer
    "BR_MAXKB": 32,       # browser.asm - the source claim, so the BODY ceiling
    "BR_LNKMAX": 200,     # browser.asm - anchors recorded per page
    "BR_LINKKB": 6,       # browser.asm - and the KB their hrefs SHARE
    "BR_LINEKB": 8,       # browser.asm - line table: 6 bytes a display line
    "BR_FMAX": 4,         # browser.asm - TEXT INPUTS the whole page may carry
    "BR_ACTMAX": 64,      # browser.asm - the form action, taken VERBATIM
    "BR_HIDMAX": 96,      # browser.asm - every hidden pair, already encoded
}
# ...and what each one means for a response, derived once so no call site does
# the arithmetic twice.
MAX_PATH = LIM["BR_PATHMAX"] - 1                     # 95
MAX_URL = LIM["BR_URLMAX"] - 1                       # 159
MAX_HOST = LIM["BR_HOSTMAX"] - 1                     # 63
HARD_BODY = LIM["BR_MAXKB"] * 1024 - 1               # 32,767: br_take's cap
MAX_LINES = LIM["BR_LINEKB"] * 1024 // 6             # 1,365 display lines
MAX_LINKS = LIM["BR_LNKMAX"]                         # 200
# The arena all the hrefs on a page are packed into, NUL-terminated. It is the
# second reason a link here is a token: 190 anchors of `/l/6q4mzt3a` is 2.3KB
# of the 6, where 190 real URLs would be 15KB and the ones past the arena would
# be drawn and not clickable.
MAX_HREF_BYTES = LIM["BR_LINKKB"] * 1024
MAX_FIELDS = LIM["BR_FMAX"]                          # 4 text inputs
MAX_ACTION = LIM["BR_ACTMAX"] - 1                    # 63 bytes of action
MAX_HIDDEN = LIM["BR_HIDMAX"] - 1                    # 95 bytes of hidden pairs
# ONE form per page. `br_form` zeroes [br_fn]/[br_hn] on every <form> it opens
# and the field table is global, so a second form on a page silently takes the
# first one's fields away and `br_submit` then posts the LAST action with
# whatever fields survived. Nothing on screen says so.
MAX_FORMS = 1
# The action is copied verbatim and handed to `br_split`, which wants a HOST -
# a root-relative action parses as host `f`, path `/tok` and fetches a machine
# that does not exist. So every action this proxy emits is absolute, and 63
# bytes is not much of a URL: `http://192.168.1.50:8088/f/ab12cd` is 33.

# The default page budget sits UNDER the hard cap on purpose: the hard one is
# where the guest truncates, and a page that lands exactly on it has no room
# for the footer the paginator has to add. Pagination is not optional - a page
# cut at 32,767 bytes ends mid-tag and the parser eats the rest of the document
# as an attribute.
DEFAULT_BUDGET = 22 * 1024

# The tags the browser knows (br_tagtab, plus h1..h6 which br_act handles
# specially). Anything else it IGNORES and keeps the text of - which is why a
# modern page runs together into one block: <section>, <article> and <header>
# are not paragraph breaks to it. Turning those into <div> is most of the job.
GUEST_TAGS = {
    "p", "div", "ul", "ol", "dl", "dt", "dd", "table", "tr", "td", "th",
    "caption", "blockquote", "a", "form", "input", "br", "hr", "li", "pre",
    "center", "h1", "h2", "h3", "h4", "h5", "h6",
}
# ...and the ones it drops WITH their content. We drop them ourselves, earlier,
# so they never cost a byte on the wire.
GUEST_DROPS = {"script", "style", "title", "select", "textarea"}

# The only attributes the guest reads (br_a_*). Everything else is bytes the
# cable carries for nothing.
GUEST_ATTRS = {
    "a": ("href",),
    "form": ("action", "method"),
    "input": ("type", "name", "value", "size"),
}

VERSION = "1.0"
# The whole sign-in has to fit inside the patience of a 4.77MHz machine's TCP
# stack: os8088 is sitting on an open socket while this happens, and when it
# gives up the browser says `Connection refused`, which points at the network
# rather than at the four upstream round trips that caused it.
FETCH_QUICK = 12        # a short deadline for a probe rather than a page

# --- two hooks, so a second front end is a FRONT END and not a fork ----------
# tools/os88proxygui.py is the Windows one (PROXY-PLAN 13). It must not be a
# copy of any of this: one engine, two faces, which is SPEC.md 20.5.1's rule
# about os88ui.inc at the scale of a host tool. `say` is the whole log and
# `note_page` is the one event a person watching a window actually wants -
# WHICH SITE, how big it was, and how big it got.
LOG_SINK = None                      # f(str)
PAGE_SINK = None                     # f(dict)


def note_page(**rec):
    """One event a watcher cares about. `kind` is 'page' or 'route'."""
    rec.setdefault("kind", "page")
    if PAGE_SINK is None:
        return
    try:
        PAGE_SINK(rec)
    except Exception:                # a broken front end may not take the
        pass                         # server down with it
UA_DESKTOP = ("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
              "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36")
# ...and what the JSON endpoints are asked as. Reddit's API rules want a
# descriptive agent rather than a browser's, and a browser's is a good way to
# be handed the JavaScript app instead of the data.
REDDIT_UA = "os8088proxy/1.0 (a text-browser proxy for a 1981 PC)"

# =============================================================================
# 2. FOLDING TO WHAT THE CELL FONT CAN DRAW
#
# ONE definition of the fold, and it is tools/htmsim.py's - which is also where
# the browser's own br_l1tab is generated from (--emit-l1tab). Two hand-kept
# tables would drift on the first accented letter nobody checked, and the
# failure mode is a glyph index past the 95 the font has.
# =============================================================================
_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
try:
    import htmsim                                                # noqa: E402
    HAVE_HTMSIM = True
except Exception:                                                # pragma: no cover
    HAVE_HTMSIM = False


def _fallback_fold(text):
    """Used only when this file has been copied away from the repo.

    It is deliberately CRUDE and says so at startup: a second, cleverer fold
    living here would be a second opinion about what the font can draw, and
    this tree has paid for one of those before.
    """
    import unicodedata
    out = []
    for ch in unicodedata.normalize("NFKD", text):
        cp = ord(ch)
        if 0x20 <= cp <= 0x7E or ch in "\n\t":
            out.append(ch)
        elif cp == 0xA0:
            out.append(" ")
        elif cp < 0x20:
            pass
        else:
            out.append("?")
    return "".join(out)


def fold_text(text):
    """Decoded text -> ASCII 0x20..0x7E (plus \\n and \\t), entities resolved."""
    if HAVE_HTMSIM:
        return htmsim.unescape(text)
    return _fallback_fold(html.unescape(text))


def esc(text):
    """The THREE entities the guest's table carries, and no others.

    br_enttab has sixty-odd names in it, but everything else in this file has
    already been folded to ASCII, so emitting `&mdash;` would be asking the
    guest to do work we already did.
    """
    return (text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))


# =============================================================================
# 3. A TREE, BECAUSE THE HEURISTICS NEED ONE
#
# The browser's own parse is a linear byte scan and is right to be (BROWSER-PLAN
# 13.1). The proxy cannot be: "is this <div> a column of a layout" and "which
# subtree is the article" are both questions about CHILDREN, and a stream has
# none. The tree is thrown away the moment it has been serialised.
# =============================================================================
VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link",
        "meta", "param", "source", "track", "wbr"}

# Elements that close an open <p> (or <li>, <td>...) when they open. Real pages
# leave all of these unclosed and a parser that does not know it builds a tree
# 300 deep out of one unclosed <li>.
BLOCKY = {"address", "article", "aside", "blockquote", "details", "div", "dl",
          "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2",
          "h3", "h4", "h5", "h6", "header", "hr", "main", "nav", "ol", "p",
          "pre", "section", "table", "ul"}
AUTO_CLOSE = {
    "p": BLOCKY,
    "li": {"li"},
    "dt": {"dt", "dd"},
    "dd": {"dt", "dd"},
    "tr": {"tr", "tbody", "thead", "tfoot"},
    "td": {"td", "th", "tr", "tbody", "thead", "tfoot"},
    "th": {"td", "th", "tr", "tbody", "thead", "tfoot"},
    "option": {"option", "optgroup"},
    "thead": {"tbody", "tfoot"},
    "tbody": {"tbody", "tfoot"},
}


class Node(object):
    __slots__ = ("tag", "attrs", "kids", "parent")

    def __init__(self, tag, attrs=None):
        self.tag = tag
        self.attrs = attrs or {}
        self.kids = []
        self.parent = None

    def add(self, kid):
        if isinstance(kid, Node):
            kid.parent = self
        self.kids.append(kid)
        return kid

    def elems(self):
        return [k for k in self.kids if isinstance(k, Node)]

    def text(self, limit=None):
        out = []
        n = 0
        stack = list(self.kids)
        while stack:
            k = stack.pop(0)
            if isinstance(k, Node):
                if k.tag in GUEST_DROPS or k.tag in ("script", "style"):
                    continue
                stack = list(k.kids) + stack
            else:
                out.append(k)
                n += len(k)
                if limit is not None and n > limit:
                    break
        return "".join(out)

    def cls(self):
        return (self.attrs.get("class", "") + " " + self.attrs.get("id", "")).lower()

    def style(self):
        return self.attrs.get("style", "").lower().replace(" ", "")

    def find(self, tag):
        out = []
        for k in self.elems():
            if k.tag == tag:
                out.append(k)
            out.extend(k.find(tag))
        return out

    def dump(self, depth=0):                                    # debugging aid
        pad = "  " * depth
        yield "%s<%s %s>" % (pad, self.tag, self.attrs.get("class", ""))
        for k in self.elems():
            for line in k.dump(depth + 1):
                yield line


class Builder(html.parser.HTMLParser):
    """Tolerant enough for the real web: implicit closes, stray end tags
    ignored, and CONTENT of <script>/<style> discarded rather than kept as
    text - HTMLParser hands it over as data and a page that lost its script
    tags but kept their bodies is the classic way to render JavaScript."""

    def __init__(self, templates=False):
        html.parser.HTMLParser.__init__(self, convert_charrefs=True)
        self.root = Node("html")
        self.stack = [self.root]
        self.mute = 0
        # A <template> is content JavaScript will clone, so on a machine with
        # no JavaScript it is either nothing or EVERYTHING - and on modern
        # reddit it is everything: the whole feed ships inside
        # `<template for="s_…">` blocks that hydration copies out. Muted, a
        # 597KB page parses to 81 divs and no posts at all.
        self.hide = ("script", "style", "noscript", "svg") if templates \
            else ("script", "style", "noscript", "svg", "template")

    def _top(self):
        return self.stack[-1]

    def handle_starttag(self, tag, attrs):
        tag = tag.lower()
        if tag in self.hide:
            self.mute += 1
            return
        if self.mute:
            return
        closers = AUTO_CLOSE.get(self._top().tag)
        while closers and tag in closers and len(self.stack) > 1:
            self.stack.pop()
            closers = AUTO_CLOSE.get(self._top().tag)
        node = Node(tag, {k.lower(): (v or "") for k, v in attrs})
        self._top().add(node)
        if tag not in VOID:
            self.stack.append(node)

    def handle_startendtag(self, tag, attrs):
        tag = tag.lower()
        if self.mute or tag in ("script", "style", "noscript", "svg"):
            return
        self._top().add(Node(tag, {k.lower(): (v or "") for k, v in attrs}))

    def handle_endtag(self, tag):
        tag = tag.lower()
        if tag in self.hide:
            self.mute = max(0, self.mute - 1)
            return
        if self.mute:
            return
        for i in range(len(self.stack) - 1, 0, -1):
            if self.stack[i].tag == tag:
                del self.stack[i:]
                return
        # a stray </div>: ignored, which is what every browser does

    def handle_data(self, data):
        if not self.mute:
            self._top().add(data)


def build_tree(text, templates=False):
    b = Builder(templates)
    try:
        b.feed(text)
        b.close()
    except Exception:
        pass                       # a half-built tree beats an exception page
    return b.root


# =============================================================================
# 4. THE TRANSFORMS
#
# Each pass is separate, each is named for what it removes or what it asserts,
# and each can be turned off from the command line - because every one of them
# is a HEURISTIC, and a heuristic with no escape hatch is a page the user
# cannot reach. The footer of every rendered page carries a `Full` link that
# re-fetches with all of them off (BROWSER-PLAN 8.3.1's rule that the proxy
# must never become load-bearing, at the scale of one page).
# =============================================================================
JUNK_CLASS = re.compile(
    r"(^|[-_ ])(nav|navbar|menu|sidebar|side-?bar|footer|masthead|banner|"
    r"cookie|consent|gdpr|promo|newsletter|subscribe|social|share|sharing|"
    r"related|recommend|advert|advertisement|ad-slot|ads?|popup|modal|"
    r"breadcrumb|pagination-ad|skip-link|screen-?reader|sr-only|visually-?hidden)"
    r"([-_ ]|$)")
JUNK_TAGS = {"nav", "aside", "footer", "header", "noscript", "iframe",
             "canvas", "video", "audio", "object", "embed", "svg", "map",
             "menu", "dialog", "meta", "link", "base", "colgroup", "col"}
HIDDEN_STYLE = re.compile(r"display:none|visibility:hidden|opacity:0(?![.\d])")
# What a CSS-laid-out row of columns looks like from the outside. `col-\d` is
# Bootstrap, `grid`/`flex` is everything since; the class names are the only
# evidence there is, because we do not have a CSS engine and are not getting one.
ROWISH = re.compile(r"(^|[-_ ])(row|flex|grid|columns?|col-\d+|cards?|tiles?|"
                    r"stats?|features?)([-_ ]|$)")
FLEXSTYLE = re.compile(r"display:(flex|grid|inline-flex|table)")


# What is dropped even on a `Full` view: not furniture, but content the guest
# has no way to show at all.
UNSHOWABLE = {"noscript", "iframe", "canvas", "video", "audio", "object",
              "embed", "svg", "map", "dialog", "meta", "link", "base",
              "colgroup", "col"}


# Elements a junk rule may NEVER remove, whatever their class says. This list
# is not caution, it is a bug: `en.wikipedia.org` puts feature flags on the
# <html> tag - `vector-feature-language-in-main-menu-disabled` - and the class
# matcher below found `-menu-` in it and dropped THE WHOLE DOCUMENT. The page
# rendered as a title and `(empty page)`, which is indistinguishable from a
# site being down.
STRUCTURAL = {"html", "body", "main", "article"}
# ...and the share of the document's text above which a CLASS is not to be
# believed. A nav, a sidebar or a footer does not hold a third of the page; an
# element that does is content whose class name happens to contain a word.
JUNK_SHARE = 0.30


def drop_junk(root, aggressive=True, furniture=True):
    """Elements that are furniture, hidden, or content this machine has no
    way to show. Runs BEFORE the main-content pick so their text cannot win
    the scoring.

    `furniture=False` is what the page footer's `Full` link asks for: the nav,
    the header and the sidebar come back, because the whole point of that link
    is to reach what a heuristic decided you did not want."""
    n = [0]
    drops = JUNK_TAGS if furniture else UNSHOWABLE
    total = max(1, len(root.text()))

    def walk(node):
        for kid in list(node.elems()):
            drop = byclass = False
            if kid.tag in drops:
                drop = True
            elif "hidden" in kid.attrs or kid.attrs.get("aria-hidden") == "true":
                drop = True
            elif HIDDEN_STYLE.search(kid.style()):
                drop = True
            elif kid.attrs.get("type", "").lower() == "hidden" and kid.tag != "input":
                drop = True
            elif aggressive and JUNK_CLASS.search(kid.cls()):
                drop = byclass = True
            if drop and kid.tag in STRUCTURAL:
                drop = False              # never, on any evidence
            if drop and byclass and len(kid.text()) > total * JUNK_SHARE:
                # The class says furniture and the size says article. Size wins:
                # a wrong keep costs a screenful, a wrong drop costs the page.
                say("kept <%s class=%r>: %d%% of the page's text"
                    % (kid.tag, short(kid.attrs.get("class", ""), 40),
                       100 * len(kid.text()) // total))
                drop = False
            if drop:
                node.kids.remove(kid)
                n[0] += 1
            else:
                walk(kid)
    walk(root)
    return n[0]


def pick_main(root):
    """Readability-lite: the subtree with the most PROSE and the fewest links.

    Deliberately conservative - a wrong pick loses the page, and the fallback
    (the whole body) is merely verbose. So a candidate must hold a quarter of
    the document's text before it is allowed to win.
    """
    body = root.find("body")
    base = body[0] if body else root
    total = len(base.text())
    if total < 400:
        return base, "whole document (short)"
    for sel, why in (("main", "<main>"), ("article", "<article>")):
        found = base.find(sel)
        if found:
            best = max(found, key=lambda n: len(n.text()))
            if len(best.text()) >= total * 0.25:
                return best, why
    best, score, why = base, 0, "whole document"
    for kid in [k for k in base.find("div") + base.find("section")]:
        txt = len(kid.text())
        if txt < total * 0.25:
            continue
        links = sum(len(a.text()) for a in kid.find("a"))
        paras = len(kid.find("p")) + len(kid.find("li"))
        s = txt - 3 * links + 40 * paras
        role = (kid.attrs.get("role", "") + " " + kid.cls())
        if re.search(r"(^|[-_ ])(content|main|article|post|story|entry)", role):
            s += 500
        if s > score:
            best, score, why = kid, s, "<%s class=%s>" % (
                kid.tag, (kid.attrs.get("class") or "-")[:24])
    return best, why


def unwrap(node, kid):
    """Replace a child element with its own children, in place."""
    i = node.kids.index(kid)
    node.kids[i:i + 1] = kid.kids
    for k in kid.kids:
        if isinstance(k, Node):
            k.parent = node


def table_cells(t):
    rows = []
    for tr in t.find("tr"):
        cells = [c for c in tr.elems() if c.tag in ("td", "th")]
        if cells:
            rows.append((tr, cells))
    return rows


def simplify_tables(root, maxcols, stats):
    """Two thirds of the tables on the old web are LAYOUT, and htmsim's
    3.2.1 heuristic is the one this tree already settled on: a 1x1 table is a
    block and a table with no text is decoration. Two more are added here
    because the modern web has them - a `role=presentation` table says so
    itself, and a table whose cells each hold their own block structure is a
    page skeleton rather than data.

    A table that survives but is too WIDE is turned on its side: a six-column
    row on a 79-cell screen gives every column thirteen characters, which is
    not a table, it is a mess. One record per paragraph is readable.
    """
    for t in list(root.find("table")):
        if t.parent is None:
            continue
        rows = table_cells(t)
        ncell = sum(len(c) for _, c in rows)
        widest = max([len(c) for _, c in rows] or [0])
        txt = len(t.text().strip())
        pres = t.attrs.get("role", "").lower() == "presentation"
        blocky = sum(1 for _, cs in rows for c in cs
                     if c.find("table") or c.find("form") or len(c.find("p")) > 1)
        if pres or ncell <= 1 or txt == 0 or widest < 2 or blocky > 1:
            stats["tables_unwrapped"] += 1
            for tr in t.find("tr"):
                for c in tr.elems():
                    c.tag = "div"
                tr.tag = "div"
            t.tag = "div"
            continue
        if widest > maxcols:
            stats["tables_flattened"] += 1
            head = [c.text().strip()[:20] for c in rows[0][1]] if rows else []
            for i, (tr, cells) in enumerate(rows):
                if i == 0 and all(c.tag == "th" for c in cells):
                    tr.tag = "div"
                    tr.kids = []
                    continue
                parts = []
                for j, c in enumerate(cells):
                    label = head[j] if j < len(head) and head[j] else ""
                    body = c.text().strip()
                    if not body:
                        continue
                    parts.append(("%s: %s" % (label, body)) if label else body)
                tr.tag = "p"
                tr.kids = [" | ".join(parts)]


def divs_to_table(root, maxcols, cellmax, stats):
    """A row of <div>s laid out by CSS becomes a row of <td>s.

    This is the ask's "reformat divs and css into simple tables" and it is
    worth being exact about what it can and cannot know: there is no CSS
    engine here, so the evidence is the class name, an inline `display:flex`,
    and the SHAPE - several sibling elements of similar, small size. Where
    that evidence is absent the divs stay divs and become paragraphs, which is
    what they would have been anyway. It never fires inside a real table.
    """
    def candidate(node):
        kids = [k for k in node.elems() if k.tag not in ("br", "hr")]
        if not 2 <= len(kids) <= maxcols:
            return False
        if node.tag not in ("div", "section", "ul", "dl"):
            return False
        if node.find("table") or node.find("form"):
            return False
        hint = bool(ROWISH.search(node.cls()) or FLEXSTYLE.search(node.style()))
        if not hint:
            child_hint = sum(1 for k in kids if ROWISH.search(k.cls())
                             or FLEXSTYLE.search(k.style())
                             or "float:" in k.style())
            hint = child_hint >= len(kids) - 1 and child_hint >= 2
        if not hint:
            return False
        lens = [len(k.text().strip()) for k in kids]
        if any(n == 0 for n in lens) or max(lens) > cellmax:
            return False
        return True

    def walk(node):
        if candidate(node):
            stats["divs_to_table"] += 1
            row = Node("tr")
            for k in [k for k in node.elems()]:
                k.tag = "td"
                row.add(k)
            node.kids = [row]
            node.tag = "table"
            return                      # a converted row is not walked again
        for k in list(node.elems()):
            walk(k)
    walk(root)


def dl_to_table(root, stats):
    """<dl> is a two-column table that lost its columns on the way to HTML.

    The guest renders <dt> and <dd> as bare paragraph breaks, so a glossary
    reads as an undifferentiated column of lines; as a table the term and its
    definition line up and the eye can skip.
    """
    for dl in list(root.find("dl")):
        pairs, term = [], None
        for k in dl.elems():
            if k.tag == "dt":
                term = k
            elif k.tag == "dd" and term is not None:
                pairs.append((term, k))
                term = None
        if len(pairs) < 2:
            continue
        stats["dl_to_table"] += 1
        dl.tag = "table"
        dl.kids = []
        for dt, dd in pairs:
            tr = Node("tr")
            dt.tag, dd.tag = "th", "td"
            tr.add(dt)
            tr.add(dd)
            dl.add(tr)


# =============================================================================
# 5. THE EMITTER - the tree becomes the guest's dialect, and only that
#
# Nothing this writes may be outside GUEST_TAGS, and no attribute may be
# outside GUEST_ATTRS: an unknown tag is not an error on the guest, it is
# IGNORED, so a `<section>` that reached the wire would silently lose the
# paragraph break the author meant by it. --selfcheck asserts the output
# against both sets, because that failure is invisible from the host.
# =============================================================================
INLINE = {"a", "abbr", "b", "big", "button", "cite", "code", "data", "del",
          "em", "font", "i", "img", "ins", "kbd", "label", "mark", "q", "s",
          "samp", "small", "span", "strong", "sub", "sup", "time", "tt", "u",
          "var", "wbr", "bdi", "bdo", "output", "picture", "source"}
HEADS = {"h1", "h2", "h3", "h4", "h5", "h6"}
CONTAINER = {"div", "section", "article", "main", "figure", "figcaption",
             "details", "summary", "address", "fieldset", "html", "body",
             "center", "font", "picture", "hgroup", "search"}
SPACE = re.compile(r"[ \t\r\n\f\v]+")


class Ctx(object):
    """Everything the emitter needs that is not the tree."""

    def __init__(self, base, links, opts, selfbase="http://os8088"):
        self.base = base                 # the page's own URL, for urljoin
        self.selfbase = selfbase         # what the GUEST calls this proxy
        self.links = links               # a LinkStore
        self.opts = opts
        self.nlinks = 0
        self.nforms = 0
        self.stats = {"tables_unwrapped": 0, "tables_flattened": 0,
                      "divs_to_table": 0, "dl_to_table": 0, "links": 0,
                      "dropped": 0, "images": 0, "forms": 0,
                      "forms_dropped": 0}

    def href(self, raw):
        """An href on the page -> a path on this proxy, or None.

        Returns None for everything the guest cannot follow - fragments,
        `mailto:`, `javascript:`, `data:` - so the caller can emit the text
        without an anchor rather than an anchor that does nothing. A link that
        silently does nothing reads as a broken browser (SPEC.md 71.2's own
        argument, one level up).
        """
        raw = (raw or "").strip()
        if not raw or raw.startswith("#"):
            return None
        try:
            absu = urllib.parse.urljoin(self.base, raw)
        except Exception:
            return None
        sch = urllib.parse.urlsplit(absu).scheme.lower()
        if sch not in ("http", "https"):
            return None
        if self.nlinks >= MAX_LINKS - 8:      # 8 for the chrome. Past BR_LNKMAX
            return None                       # the guest stops recording them
        self.nlinks += 1
        self.stats["links"] += 1
        return self.links.path_of(absu)


def collapse(text):
    return SPACE.sub(" ", text)


def wrap_pre(text, cols):
    """<pre> does not wrap in the guest (TA_PRE is the no-wrap marker), so a
    120-column code block runs off the right of a 79-cell screen and is simply
    not there. Wrapping it HERE is the only place it can happen."""
    out = []
    for line in text.replace("\t", "    ").split("\n"):
        line = line.rstrip()
        while len(line) > cols:
            cut = line.rfind(" ", 0, cols)
            if cut < cols // 2:
                cut = cols
            out.append(line[:cut])
            line = line[cut:].lstrip()
        out.append(line)
    return "\n".join(out)


class Emitter(object):
    def __init__(self, ctx):
        self.ctx = ctx
        self.o = ctx.opts

    # --- inline ------------------------------------------------------------
    def inline(self, node):
        parts = []
        for kid in node.kids:
            if not isinstance(kid, Node):
                parts.append(esc(collapse(fold_text(kid))))
                continue
            t = kid.tag
            if t in GUEST_DROPS or t in UNSHOWABLE:
                continue
            if t == "br":
                parts.append("<br>")
            elif t == "img":
                parts.append(self.img(kid))
            elif t == "a":
                parts.append(self.anchor(kid))
            elif t in ("strong", "b") and self.o.emphasis == "stars":
                inner = self.inline(kid).strip()
                parts.append("*%s*" % inner if 0 < len(inner) <= 60 else inner)
            else:
                parts.append(self.inline(kid))
        s = "".join(parts)
        return SPACE.sub(" ", s)

    def img(self, node):
        """An image with no alt text says NOTHING, so it becomes nothing.

        Measured on PyPI's project page: 170 images, nearly all of them
        decorative icons in the release history, and marking each one put
        `[image]` on 170 separate lines - a third of the page, and 12 seconds
        of drawing on a CGA, to say there were pictures here. An alt-less
        image inside a LINK is the exception, and `anchor` covers it with
        `[link]` when nothing else names it.
        """
        alt = collapse(fold_text(node.attrs.get("alt", ""))).strip()
        self.ctx.stats["images"] += 1
        if self.o.images == "drop" or not alt:
            return ""
        return esc("[%s]" % alt)

    def anchor(self, node):
        text = self.inline(node).strip()
        path = self.ctx.href(node.attrs.get("href"))
        if not text:
            text = esc("[link]") if path else ""
        if not path:
            return text
        return '<a href="%s">%s</a>' % (path, text)

    # --- blocks ------------------------------------------------------------
    def blocks(self, node, out=None, depth=0):
        """The children of `node` as a list of self-contained guest-HTML
        blocks. A LIST rather than one string because pagination needs
        somewhere legal to cut, and a block is the smallest thing that is
        legal to cut between."""
        out = [] if out is None else out
        buf = []

        def flush():
            s = "".join(buf).strip()
            del buf[:]
            if s and s not in ("<br>",):
                out.append("<p>%s</p>" % s)

        if depth > 40:                          # a real page has been seen at
            flush()                             # 300 deep; the guest's parse
            return out                          # is linear and ours must end
        for kid in node.kids:
            if not isinstance(kid, Node):
                buf.append(esc(collapse(fold_text(kid))))
                continue
            t = kid.tag
            if t in GUEST_DROPS or t in UNSHOWABLE:
                self.ctx.stats["dropped"] += 1
                continue
            if t == "br":
                buf.append("<br>")
                continue
            if t == "img":
                buf.append(self.img(kid))     # its own element, NOT its
                continue                      # children - an <img> has none,
            if t == "a":                      # and `inline` walked those
                buf.append(self.anchor(kid))
                continue
            if t in INLINE:
                buf.append(self.inline(kid))
                continue
            flush()
            if t in HEADS:
                s = self.inline(kid).strip()
                if s:
                    out.append("<%s>%s</%s>" % (t, s, t))
            elif t == "hr":
                out.append("<hr>")
            elif t == "pre":
                body = wrap_pre(fold_text(kid.text()), self.o.cols - 2)
                if body.strip():
                    out.append("<pre>%s</pre>" % esc(body))
            elif t == "table":
                out.extend(self.table(kid))
            elif t in ("ul", "ol"):
                out.extend(self.list_(kid, ordered=(t == "ol")))
            elif t == "li":
                s = self.inline(kid).strip()
                if s:
                    out.append("<li>%s</li>" % s)
            elif t == "blockquote":
                for b in self.blocks(kid, depth=depth + 1):
                    out.append(self.quote(b))
            elif t == "form":
                out.extend(self.form(kid))
            elif t in ("dt", "dd", "caption", "p"):
                s = self.inline(kid).strip()
                if s:
                    out.append("<p>%s</p>" % s)
                else:
                    self.blocks(kid, out, depth + 1)
            elif t in CONTAINER or t in ("tr", "td", "th", "dl", "tbody",
                                         "thead", "tfoot", "nav"):
                self.blocks(kid, out, depth + 1)
            else:
                self.blocks(kid, out, depth + 1)
        flush()
        return out

    def quote(self, block):
        """The guest draws <blockquote> as a bare paragraph break - no indent,
        no rule - so a quotation is indistinguishable from the prose around
        it. The mark has to be characters."""
        m = re.match(r"^<(p|li)>(.*)</\1>$", block, re.S)
        if m:
            return "<%s>&gt; %s</%s>" % (m.group(1), m.group(2), m.group(1))
        return block

    def list_(self, node, ordered):
        out, n = [], 0
        for kid in node.elems():
            if kid.tag != "li":
                self.blocks(kid, out)
                continue
            n += 1
            inner = self.inline(kid).strip()
            sub = [k for k in kid.elems() if k.tag in ("ul", "ol", "table", "p")]
            if not inner and sub:
                self.blocks(kid, out)
                continue
            if ordered:
                inner = "%d. %s" % (n, inner)
            if inner:
                out.append("<li>%s</li>" % inner)
            for s in sub:
                if s.tag in ("ul", "ol"):
                    out.extend(self.list_(s, s.tag == "ol"))
        return out

    def cell(self, node):
        blocky = any(k.tag in BLOCKY or k.tag in HEADS
                     for k in node.elems()) or bool(node.find("p"))
        if blocky:
            parts = [re.sub(r"<[^>]*>", " ", b) if b.startswith("<pre>")
                     else re.sub(r"</?(p|li|h[1-6]|div|blockquote)>", " ", b)
                     for b in self.blocks(node)]
            s = SPACE.sub(" ", " ".join(parts)).strip()
        else:
            s = self.inline(node).strip()
        if not s:
            s = SPACE.sub(" ", esc(fold_text(node.text()))).strip()
        cap = max(400, self.o.cellmax * 3)
        if len(s) > cap:
            # Truncation here is LOST TEXT and nothing downstream can tell, so
            # it is counted and the ellipsis is the only honest marker there is.
            self.ctx.stats["cells_cut"] = self.ctx.stats.get("cells_cut", 0) + 1
            s = s[:cap].rsplit(" ", 1)[0] + "..."
        return s

    def table(self, node):
        rows = table_cells(node)
        if not rows:
            return []
        chunks, cur, size = [], [], 0
        cap = self.o.budget // 2
        for _, cells in rows:
            tds = []
            for c in cells:
                # NO colspan: br_a_* has no such attribute, so the guest reads
                # it with nothing and it is bytes on a 3,741 B/s wire. A cell
                # that spanned columns is simply a cell here.
                tag = "th" if c.tag == "th" else "td"
                tds.append("<%s>%s</%s>" % (tag, self.cell(c), tag))
            row = "<tr>%s</tr>" % "".join(tds)
            if cur and size + len(row) > cap:
                chunks.append("<table>%s</table>" % "".join(cur))
                cur, size = [], 0
            cur.append(row)
            size += len(row)
        if cur:
            chunks.append("<table>%s</table>" % "".join(cur))
        return chunks

    # --- forms -------------------------------------------------------------
    def form(self, node):
        """A GET form is what makes a search engine work (BROWSER-PLAN 7), and
        the browser can only GET - so a POST form is rewritten to a GET
        THROUGH THIS PROXY, which then issues the POST upstream. That is the
        one capability here the guest does not have on its own.

        Four of the guest's limits meet in this one routine and every one of
        them fails silently: ONE form a page, FOUR text inputs, 95 bytes of
        hidden pairs and a 63-byte action that must be ABSOLUTE. What does not
        fit is dropped with a visible marker rather than left to be truncated
        into a query that fetches the wrong thing.
        """
        if self.ctx.nforms >= MAX_FORMS:
            self.ctx.stats["forms_dropped"] += 1
            return ["<p>%s</p>" % esc("[a second form is not shown: the "
                                      "browser keeps one field table for the "
                                      "whole page]")]
        action = urllib.parse.urljoin(self.ctx.base,
                                      node.attrs.get("action", "") or self.ctx.base)
        method = (node.attrs.get("method", "get") or "get").lower()
        if urllib.parse.urlsplit(action).scheme not in ("http", "https"):
            return []
        if method == "post" and self.o.forms != "all":
            return ["<p>%s</p>"
                    % esc("[form: POST is not enabled - run with --forms all]")]
        tok = self.ctx.links.form(action, method)
        # ABSOLUTE where it fits, RELATIVE where it does not. `br_submit` sends
        # a composed action through `br_resolve` now - the same resolver an
        # href goes through - so `/f/<tok>` is eleven bytes and works; before
        # that merge it parsed as host `f`, path `/<tok>` and fetched a machine
        # that does not exist. Absolute stays the default because it is the one
        # that also works on a browser built before that change, and 63 bytes
        # of BR_ACTMAX is enough for any address with an IP in it.
        rel = "/f/%s" % tok
        act = "%s%s" % (self.ctx.selfbase.rstrip("/"), rel)
        if self.o.form_action == "relative" or (
                self.o.form_action == "auto" and len(act) > MAX_ACTION):
            act = rel
        if len(act) > MAX_ACTION:
            self.ctx.stats["forms_dropped"] += 1
            return ["<p>%s</p>"
                    % esc("[form dropped: the action is %d bytes and the "
                          "browser holds %d]" % (len(act), MAX_ACTION))]
        parts = ['<form action="%s" method="get">' % act]
        nfield, hidbytes, lost = 0, 0, 0
        for el in node.find("input") + node.find("select") + node.find("textarea"):
            typ = (el.attrs.get("type", "text") or "text").lower()
            name = el.attrs.get("name", "")
            val = collapse(fold_text(el.attrs.get("value", ""))).strip()
            if el.tag == "select":
                chosen = ""
                for opt in el.find("option"):
                    if "selected" in opt.attrs:
                        chosen = opt.attrs.get("value", opt.text().strip())
                if not (name and chosen):
                    continue
                typ, val = "hidden", collapse(fold_text(chosen)).strip()
            elif el.tag == "textarea":
                continue
            if typ in ("file", "image", "reset", "button", "checkbox",
                       "radio", "color", "range"):
                lost += 1
                continue
            if typ == "password":
                # SPEC.md 71.2: there is no TLS between os8088 and this proxy,
                # so a password box would be a plaintext credential prompt. The
                # session lives HERE, in the cookie jar (--cookies).
                parts.append(esc("[password field: not shown]"))
                continue
            if typ == "hidden":
                if not name:
                    continue
                pair = "%s=%s&" % (urllib.parse.quote_plus(name),
                                   urllib.parse.quote_plus(val))
                if hidbytes + len(pair) > MAX_HIDDEN:
                    lost += 1
                    continue
                hidbytes += len(pair)
                parts.append('<input type="hidden" name="%s" value="%s">'
                             % (esc(name), esc(val)))
                continue
            if typ == "submit":
                parts.append('<input type="submit" value="%s">'
                             % esc(val or "Submit"))
                continue
            if not name:
                continue
            if nfield >= MAX_FIELDS:
                lost += 1
                continue
            nfield += 1
            size = el.attrs.get("size", "")
            sz = ' size="%s"' % size if size.isdigit() and int(size) <= 40 else ' size="24"'
            parts.append('<input type="text" name="%s" value="%s"%s>'
                         % (esc(name), esc(val), sz))
        if nfield == 0:
            return []
        if not any('type="submit"' in p for p in parts):
            parts.append('<input type="submit" value="Go">')
        if lost:
            parts.append(esc(" [%d field(s) this browser cannot carry]" % lost))
        parts.append("</form>")
        self.ctx.nforms += 1
        self.ctx.stats["forms"] += 1
        return ["".join(parts)]


# =============================================================================
# 6. LINKS ARE TOKENS, BECAUSE THE PATH IS 95 BYTES
#
# This is the constraint that shapes the whole URL space. `br_split` copies at
# most BR_PATHMAX-1 bytes of path and TRUNCATES the rest in silence, so
# `/https/www.reddit.com/r/vintagecomputing/comments/1abcdef/a_long_title/`
# is not a link, it is a 500 error waiting to happen. Every href this proxy
# emits is therefore a minted token - `/l/6q4mzt3a`, eleven bytes - and the
# real URL lives on the host where there is room for it.
#
# Tokens are a hash rather than a counter so they SURVIVE A RESTART: a page
# left on os8088's screen overnight still has working links in the morning,
# and the store is a JSON file rather than a database for the same reason
# tools/os88disk.py writes a deterministic image - it must be readable with a
# text editor when something goes wrong.
# =============================================================================
class LinkStore(object):
    def __init__(self, path=None, cap=40000):
        self.file = path             # NOT self.path: `path_of` is a method and
                                     # an instance attribute of that name would
                                     # shadow it
        self.cap = cap
        self.map = {}                     # token -> ["u", url] | ["f", act, meth]
        self.rev = {}                     # url -> token
        self.dirty = 0
        self.lock = threading.Lock()
        if path and os.path.exists(path):
            try:
                with open(path, "r") as fh:
                    self.map = json.load(fh)
                for tok, ent in self.map.items():
                    if ent and ent[0] == "u":
                        self.rev[ent[1]] = tok
            except Exception as exc:
                say("link store %s unreadable (%s) - starting empty" % (path, exc))

    def _mint(self, key, entry):
        with self.lock:
            tok = self.rev.get(key)
            if tok and self.map.get(tok) == entry:
                return tok
            digest = base64.b32encode(hashlib.sha1(key.encode("utf-8", "replace"))
                                      .digest()).decode("ascii").lower()
            for n in range(8, 26):
                tok = digest[:n]
                got = self.map.get(tok)
                if got is None or got == entry:
                    break                 # a collision EXTENDS rather than
            self.map[tok] = entry         # overwrites: two pages sharing a
            if entry[0] == "u":           # token is the user sent to the
                self.rev[key] = tok       # wrong page, silently
            self.dirty += 1
            if len(self.map) > self.cap:
                for old in list(self.map)[:len(self.map) - self.cap]:
                    ent = self.map.pop(old, None)
                    if ent and ent[0] == "u":
                        self.rev.pop(ent[1], None)
            if self.dirty >= 64:
                self.save()
            return tok

    def path_of(self, url):
        return "/l/" + self._mint(url, ["u", url])

    def form(self, action, method):
        return self._mint("form\\0%s\\0%s" % (method, action), ["f", action, method])

    def get(self, tok):
        return self.map.get(tok)

    def save(self):
        if not self.file:
            return
        self.dirty = 0
        try:
            tmp = self.file + ".tmp"
            with open(tmp, "w") as fh:
                json.dump(self.map, fh)
            os.replace(tmp, self.file)
        except Exception as exc:                                # pragma: no cover
            say("link store not saved: %s" % exc)


class PageCache(object):
    """Rendered pages, keyed by the same token their URL has.

    It is what makes `Back`, `Next page` and a re-read of page 2 cost NOTHING -
    no TLS handshake, no fetch, no transform - which on a machine where the
    fetch is seconds is the difference between paging through a document and
    giving up on it. It is also what stops pagination lying: page 2 is a slice
    of the SAME render page 1 came from, not a second render that may have
    moved underneath the reader.
    """
    def __init__(self, cap=64, ttl=900):
        self.cap, self.ttl = cap, ttl
        self.d = {}
        self.lock = threading.Lock()

    def put(self, tok, rec):
        with self.lock:
            rec["ts"] = time.time()
            self.d[tok] = rec
            while len(self.d) > self.cap:
                self.d.pop(next(iter(self.d)))

    def get(self, tok):
        with self.lock:
            rec = self.d.get(tok)
            if rec and time.time() - rec["ts"] > self.ttl:
                self.d.pop(tok, None)
                return None
            return rec

    def drop(self, tok):
        with self.lock:
            self.d.pop(tok, None)


class History(object):
    """One stack, because BROWSER-PLAN 8.3.1 says this proxy is single-user and
    lives on the owner's own network. Per-client history keyed on the peer
    address is four more lines and is in the plan as a want, not a need."""
    def __init__(self, cap=64):
        self.stack = []
        self.cap = cap

    def push(self, path):
        if self.stack and self.stack[-1] == path:
            return
        self.stack.append(path)
        del self.stack[:-self.cap]

    def back(self):
        if len(self.stack) >= 2:
            self.stack.pop()
            return self.stack[-1]
        return self.stack[-1] if self.stack else "/"


# =============================================================================
# 7. THE FETCH - the only place TLS, gzip, redirects and cookies exist
# =============================================================================
class Fetched(object):
    def __init__(self, url, status, ctype, text, raw_len):
        self.url, self.status, self.ctype = url, status, ctype
        self.text, self.raw_len = text, raw_len


class Fetcher(object):
    def __init__(self, opts):
        self.opts = opts
        self.jar = MozillaCookieJar()
        if opts.cookies:
            try:
                self.jar.load(opts.cookies, ignore_discard=True, ignore_expires=True)
                say("cookies: %d loaded from %s" % (len(self.jar), opts.cookies))
            except Exception as exc:
                say("cookies NOT loaded from %s: %s" % (opts.cookies, exc))
        ctx = ssl.create_default_context()
        if opts.insecure:
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
        handlers = [urllib.request.HTTPSHandler(context=ctx),
                    urllib.request.HTTPCookieProcessor(self.jar)]
        self.opener = urllib.request.build_opener(*handlers)

    def get(self, url, method="GET", data=None, extra=None, timeout=None):
        headers = {
            "User-Agent": self.opts.user_agent,
            "Accept": "text/html,application/xhtml+xml,text/plain;q=0.9,*/*;q=0.5",
            "Accept-Language": "en-GB,en;q=0.9",
            "Accept-Encoding": "gzip, deflate",
            "Connection": "close",
        }
        headers.update(extra or {})
        body = None
        if data is not None:
            body = data.encode("utf-8")
            headers["Content-Type"] = "application/x-www-form-urlencoded"
        req = urllib.request.Request(url, data=body, headers=headers, method=method)
        try:
            resp = self.opener.open(req, timeout=timeout or self.opts.timeout)
        except urllib.error.HTTPError as exc:
            # A 404 has a BODY and it is usually the site's own page saying so.
            # Rendering it and passing the code through is strictly better than
            # replacing it with our own notice: the browser reads the code off
            # the status line (br_hdrb) and the reader gets the site's words.
            resp = exc
        with resp:
            cap = self.opts.max_download
            chunks, n = [], 0
            while n < cap:
                blk = resp.read(min(65536, cap - n))
                if not blk:
                    break
                chunks.append(blk)
                n += len(blk)
            raw = b"".join(chunks)
            enc = (resp.headers.get("Content-Encoding") or "").lower()
            if "gzip" in enc:
                try:
                    raw = gzip.GzipFile(fileobj=io.BytesIO(raw)).read()
                except Exception:
                    pass
            elif "deflate" in enc:
                try:
                    raw = zlib.decompress(raw, -zlib.MAX_WBITS)
                except Exception:
                    try:
                        raw = zlib.decompress(raw)
                    except Exception:
                        pass
            ctype = (resp.headers.get("Content-Type") or "text/html").lower()
            text = decode_body(raw, ctype)
            # `.status` on a response is 3.9+; `.code` is what an HTTPError and
            # an older Python answer with. Windows machines run whatever they
            # were installed with.
            code = getattr(resp, "status", None) or getattr(resp, "code", 200)
            return Fetched(resp.geturl(), code, ctype, text, len(raw))


SAVE_N = [0]


def save_page(opts, url, text, pages, extra=None):
    """Write what came in and what went out, side by side.

    Asked for by the field ("maybe add a way to save the retrieved pages") and
    kept because it is the only way a page that renders wrong HERE can be
    looked at THERE: the source is what the server sent, the .out files are
    what os8088 was handed, and the .json is the sizes and the transform
    counters. Nothing else in this tool can answer "why is that page empty"
    from a screenshot.
    """
    if not opts.save_dir:
        return
    try:
        os.makedirs(opts.save_dir, exist_ok=True)
        SAVE_N[0] += 1
        sp = urllib.parse.urlsplit(url)
        slug = re.sub(r"[^A-Za-z0-9._-]+", "_",
                      (sp.netloc + sp.path))[:60].strip("_") or "page"
        stem = os.path.join(opts.save_dir, "%04d-%s" % (SAVE_N[0], slug))
        with open(stem + ".src.html", "w", encoding="utf-8",
                  errors="replace") as fh:
            fh.write(text)
        for i, page in enumerate(pages or [], 1):
            with open("%s.out%d.html" % (stem, i), "wb") as fh:
                fh.write(page)
        with open(stem + ".json", "w") as fh:
            json.dump(dict(extra or {}, url=url, src_chars=len(text),
                           out_bytes=sum(len(p) for p in pages or []),
                           pages=len(pages or [])), fh, indent=1)
        say("saved %s.*" % os.path.basename(stem))
    except Exception as exc:
        say("could not save %s: %s" % (short(url, 40), exc))


def decode_body(raw, ctype):
    """The header's charset first, the document's own declaration second, and
    latin-1 last - which cannot fail, and is what a 1990s page without a
    declaration actually is."""
    m = re.search(r"charset\s*=\s*[\"']?([\w-]+)", ctype or "")
    if m:
        try:
            return raw.decode(m.group(1), "replace")
        except (LookupError, UnicodeDecodeError):
            pass
    if HAVE_HTMSIM:
        try:
            return htmsim.decode(raw)[0]
        except Exception:
            pass
    head = raw[:2048].decode("ascii", "replace").lower()
    m = re.search(r"charset\s*=\s*[\"']?([\w-]+)", head)
    if m:
        try:
            return raw.decode(m.group(1), "replace")
        except (LookupError, UnicodeDecodeError):
            pass
    return raw.decode("latin-1")


# =============================================================================
# 8. PAGINATION - because 32,767 bytes is a CUT, not an error
#
# `br_take` stops writing at BR_MAXKB*1024-1 and the parse then reads whatever
# the cut left: a page truncated mid-tag loses the rest of the document into an
# attribute, and nothing on the guest says a word about it. So the proxy splits
# at a block boundary and says which page of how many this is - which is also
# the only way a long article is READABLE on a machine where one screenful is
# 71ms a row to draw.
# =============================================================================
def est_lines(block, cols):
    """What this block will cost the guest's LINE TABLE, near enough.

    Bytes are not the only ceiling: BR_LINEKB holds 1,365 display lines and a
    page of short paragraphs is mostly tags, so 22KB of markup can lay out to
    more lines than the table has entries. Overshoot it and the tail of the
    page is not merely unreadable, it does not exist.
    """
    text = re.sub(r"<[^>]*>", "", block)
    rows = max(1, -(-len(text) // max(20, cols)))
    return rows + (2 if block.startswith(("<h", "<table", "<pre")) else 1)


def paginate(blocks, budget, chrome_bytes, cols=79, maxlines=900):
    room = max(2048, budget - chrome_bytes)
    pages, cur, n, links, lines = [], [], 0, 0, 0
    for b in blocks:
        nl = b.count("<a ")
        el = est_lines(b, cols)
        if cur and (n + len(b) > room or links + nl > MAX_LINKS - 12
                    or lines + el > maxlines):
            pages.append(cur)
            cur, n, links, lines = [], 0, 0, 0
        lines += el
        if len(b) > room:                     # one block bigger than a page:
            for piece in hard_split(b, room):  # split it rather than truncate
                if cur:
                    pages.append(cur)
                    cur, n, links, lines = [], 0, 0, 0
                pages.append([piece])
            continue
        cur.append(b)
        n += len(b)
        links += nl
    if cur:
        pages.append(cur)
    return pages or [["<p>(empty page)</p>"]]


def hard_split(block, room):
    m = re.match(r"^<(\w+)[^>]*>(.*)</\1>$", block, re.S)
    tag, body = (m.group(1), m.group(2)) if m else ("p", block)
    out, cur = [], ""
    for word in body.split(" "):
        if len(cur) + len(word) + 1 > room - 16 and cur:
            out.append("<%s>%s</%s>" % (tag, cur, tag))
            cur = ""
        cur += (" " if cur else "") + word
    if cur:
        out.append("<%s>%s</%s>" % (tag, cur, tag))
    return out


def chrome(title, url, tok, pageno, npages, ctx, opts, note=""):
    """Two lines at the top and one at the bottom, and no more - a row of text
    is 71ms on the field machine (PERFORMANCE.md Part 2), so four lines of
    furniture is a quarter of a second of every page."""
    # TWO links, not four. The browser grew a Back / Forward / Reload toolbar
    # with a history stack of its own (BROWSER-PLAN 5.2), so a Back and a
    # Reload in the page are a row of text and two anchors spent on what a
    # button above the page already does - and a row is 71ms on the field
    # machine. `Home` and `Full` have no button, so they stay.
    nav = ['<a href="/">Home</a>']
    if tok:
        nav.append('<a href="/x/%s">Full</a>' % tok)
    head = "<p>%s</p>" % " | ".join(nav)
    if title:
        head += "<h1>%s</h1>" % esc(title[:120])
    if note:
        head += "<p>%s</p>" % esc(note)
    foot = ["<hr>"]
    bits = []
    if npages > 1:
        bits.append("page %d of %d" % (pageno, npages))
        if pageno < npages:
            bits.append('<a href="/p/%s/%d">Next &gt;</a>' % (tok, pageno + 1))
        if pageno > 1:
            bits.append('<a href="/p/%s/%d">&lt; Prev</a>' % (tok, pageno - 1))
    host = urllib.parse.urlsplit(url).netloc if url else ""
    if host:
        bits.append(esc(host))
    if opts.verbose_page:
        bits.append(esc("t%d/%d d%d img%d" % (
            ctx.stats["tables_unwrapped"], ctx.stats["divs_to_table"],
            ctx.stats["dropped"], ctx.stats["images"])))
    if bits:                          # an empty <p> is a blank line the guest
        foot.append("<p>%s</p>" % " | ".join(bits))   # draws and nobody wanted
    return head, "".join(foot)


def page_bytes(head, body_blocks, foot):
    return ("<html><body>" + head + "".join(body_blocks) + foot +
            "</body></html>").encode("ascii", "replace")


# =============================================================================
# 9. THE GENERIC PIPELINE
# =============================================================================
def transform(text, url, opts, links, selfbase, full=False):
    """Markup in, (title, blocks, ctx) out. No I/O, so it is testable from a
    file - which is what --selfcheck does and what tests/proxy/*.htm are for."""
    root = build_tree(text)
    tnode = root.find("title")
    title = collapse(fold_text(tnode[0].text())).strip() if tnode else ""
    ctx = Ctx(url, links, opts, selfbase)
    if not full:
        ctx.stats["dropped"] += drop_junk(root, aggressive=opts.strip != "light")
        main, why = pick_main(root)
    else:
        drop_junk(root, aggressive=False, furniture=False)
        body = root.find("body")
        main, why = (body[0] if body else root), "full page"
    ctx.stats["main"] = why
    if opts.tables != "off":
        # Unwrapping a LAYOUT table runs even in full mode: it removes no
        # content and leaving it in makes the whole page a table cell, which
        # is then truncated. `Full` means "keep the boilerplate and invent
        # nothing", not "keep the 1990s page skeleton".
        simplify_tables(main, opts.maxcols, ctx.stats)
        if opts.tables == "auto" and not full:
            divs_to_table(main, opts.maxcols, opts.cellmax, ctx.stats)
            dl_to_table(main, ctx.stats)
    blocks = Emitter(ctx).blocks(main)
    blocks = [b for b in blocks if b.strip() not in ("<p></p>", "")]
    return title, blocks, ctx


def render(text, url, opts, links, selfbase, full=False, site=None):
    """The whole of what a fetched document becomes: a list of pages, each one
    a complete little HTML file in the guest's dialect."""
    # FULL SKIPS THE FIRST-CLASS HANDLER, which it did not use to. `Full` is
    # the one control that promises the reader they can get past every guess
    # this proxy makes (BROWSER-PLAN 8.3.1: the proxy must never become
    # load-bearing) - and a site handler is the biggest guess of the lot. It
    # is also the only way to SEE what reddit actually answered when a page
    # comes out wrong, which is what the field needs to be able to send back.
    if site is not None and not full:
        out = site.transform(text, url, opts, links, selfbase)
    else:
        out = None
    if out is not None:
        title, blocks, ctx = out
    elif text.lstrip()[:1] in ("{", "["):
        # A JSON body has NO generic reading - the pipeline is an HTML one, so
        # it produces a blank page. Show it as text: unreadable on a good day
        # and exactly what a field report needs on a bad one.
        ctx = Ctx(url, links, opts, selfbase)
        ctx.stats["main"] = "json (raw)"
        title = short(url, 60)
        blocks = ["<pre>%s</pre>"
                  % esc(wrap_pre(fold_text(text[:20000]), opts.cols - 2))]
    else:
        title, blocks, ctx = transform(text, url, opts, links, selfbase, full)

    # A HEURISTIC MAY NOT PRODUCE NOTHING. Boilerplate removal and the
    # main-content pick are guesses about somebody else's markup, and the one
    # outcome they must never reach is a page that renders as `(empty page)` -
    # a reader cannot tell that from a site that is down, and the Full link
    # that would have fixed it is exactly the thing they have no reason to
    # press. So an empty result RETRIES ITSELF unstripped, and says it did.
    if not blocks and not full:
        title2, blocks2, ctx2 = transform(text, url, opts, links, selfbase,
                                          full=True)
        if blocks2:
            say("stripped render was empty for %s - showing it unstripped"
                % short(url, 60))
            title, ctx = (title2 or title), ctx2
            ctx.stats["fallback"] = "full"
            blocks = ["<p>%s</p>" % esc("[the tidied version of this page came "
                                        "out empty, so this is the whole of "
                                        "it]")] + blocks2
    if not blocks:
        # Still nothing, and now it is a FINDING rather than a blank: say how
        # much markup arrived and how much of it was dropped, because that is
        # the difference between "this page is all script" and "the proxy ate
        # it", and a tester can read it off the screen.
        blocks = ["<p>%s</p>" % esc(
            "This page arrived with %s of markup and no text this browser can "
            "show. Dropped %d element(s); content picked from %s. A page built "
            "entirely by JavaScript looks exactly like this."
            % (human(len(text)), ctx.stats.get("dropped", 0),
               ctx.stats.get("main", "?")))]
    # The <title> and the article's own <h1> are nearly always the same
    # sentence with the site name on the end, and two of them is a whole row of
    # a 17-row CGA window spent saying it twice. The h1 wins - it is what the
    # page calls itself - and the chrome then carries no heading of its own.
    if blocks and blocks[0].startswith("<h1>"):
        title = re.sub(r"<[^>]*>", "", blocks[0])
        blocks = blocks[1:]
    tok = links.path_of(url).rsplit("/", 1)[-1]
    pages = paginate(blocks, opts.budget, 420, opts.cols, opts.maxlines)
    out_pages = []
    for i, blks in enumerate(pages, 1):
        head, foot = chrome(title, url, tok, i, len(pages), ctx, opts)
        out_pages.append(page_bytes(head, blks, foot))
    return {"pages": out_pages, "title": title, "url": url, "stats": ctx.stats,
            "npages": len(pages)}


# =============================================================================
# 10. FIRST-CLASS SITES
#
# The generic pipeline is a guess about somebody else's markup. A first-class
# site is one where we stop guessing: the handler knows what a listing looks
# like, what a comment tree looks like, and what is furniture, so it can emit
# the shape os8088 wants rather than a reduction of the shape a desktop
# wanted. Reddit is the first (PROXY-PLAN 6) and the seam is built for it now
# rather than retro-fitted around it.
#
# THREE RULES, and the third is the one that keeps this honest:
#   1. a handler REWRITES THE UPSTREAM URL (`upstream`) before the fetch, so
#      old.reddit.com is asked for rather than the React one;
#   2. a handler returns the same (title, blocks, ctx) triple the generic
#      pipeline does, so nothing downstream - pagination, chrome, the limits
#      gate - knows the difference;
#   3. **a handler that does not recognise what it was given returns None and
#      the generic pipeline runs.** A site redesigns overnight; a proxy that
#      answers "cannot parse" to a page it merely stopped recognising is worse
#      than one that shows the plain reduction.
# =============================================================================
def has_class(node, *names):
    cl = " %s " % node.attrs.get("class", "").lower()
    return any((" %s " % n) in cl for n in names)


class Site(object):
    name = "generic"
    hosts = ()

    def matches(self, url):
        host = urllib.parse.urlsplit(url).netloc.lower().split(":")[0]
        return any(host == h or host.endswith("." + h) for h in self.hosts)

    def upstream(self, url, opts=None):
        return url

    def headers_for(self, url, opts=None):
        """Extra request headers for THIS url, or None. A site knows things
        about its own endpoints that the fetcher cannot."""
        return None

    def after_fetch(self, got, fetcher, opts=None, asked=None):
        """A chance to answer something that is not the page - a challenge, a
        block, a redirect the site does in JavaScript. `asked` is the URL that
        was REQUESTED, which is not `got.url` when the site redirected on the
        way in. Returns a replacement `Fetched` or None to keep what arrived."""
        return None

    def transform(self, text, url, opts, links, selfbase):
        return None


class RedditSite(Site):
    """The first first-class site, built against REAL captures.

    `tests/proxy/reddit-front.htm` and `reddit-thread.htm` are pages saved
    from a signed-in old.reddit session and scrubbed by `tests/proxy/scrub.py`;
    everything below is written against those rather than against a guess, and
    what it emits is what the owner asked for and nothing else:

        the categories row
        a post's TITLE, on its own line, as a link
        its comment count as a link, and its group as a link, on the next line

    and for a thread, the group and the title, the post, a rule, and then
    `author points ago` / body per comment, indented by one character a level.

    **The `data-` attributes are the interface, not the chrome.** Every
    `div.thing` on a listing carries `data-permalink`, `data-comments-count`,
    `data-subreddit-prefixed`, `data-author` and `data-score`, which is where
    all of this comes from - the scores in the DOM are three divs deep in a
    `midcol` and the comment count is a sentence to be parsed. The visible
    markup is scraped only for the title text and the comment bodies, which
    are the two things not in an attribute.

    Signing in is §11's job and lives on this class because it is this site's
    protocol; the browser never holds a credential (BROWSER-PLAN 8.3.1).
    """
    name = "reddit"
    hosts = ("reddit.com", "redd.it")
    OLD = "https://old.reddit.com"
    WWW = "https://www.reddit.com"
    json_ok = True          # `load` may hand this site an application/json

    # The categories row. They are ours rather than the page's on purpose: the
    # signed-in front page puts `my subreddits` and a 100-name dropdown where a
    # logged-out one puts POPULAR / ALL, so scraping it gives a different row
    # depending on who is looking. These four always mean the same thing.
    # `Saved` was here and is gone with the sign-in: it is `/user/me/saved/`,
    # which answers `{}` to anybody who is not signed in - a link that always
    # leads nowhere is worse than no link (SPEC.md 47 rule 4, one level up).
    CATS = (("Home", "/"), ("Popular", "/r/popular/"), ("All", "/r/all/"))
    SORTS = ("hot", "new", "top", "rising")

    def upstream(self, url, opts=None):
        sp = urllib.parse.urlsplit(url)
        netloc, host = sp.netloc, sp.netloc.lower().split(":")[0]
        scheme = sp.scheme or "https"
        want = getattr(opts, "reddit_site", "www") if opts else "www"
        if host in ("reddit.com", "www.reddit.com", "np.reddit.com",
                    "m.reddit.com", "i.reddit.com", "new.reddit.com",
                    "old.reddit.com"):
            # `/svc/` is modern reddit's own fragment endpoint - a feed's
            # `load-after` cursor is one - and it exists on www ALONE, so it
            # is never moved whatever the switch says. Without this exception
            # `--reddit-site old` turns every More posts link into a 404 on a
            # host that has never heard of the path.
            if sp.path.startswith("/svc/"):
                netloc = "www.reddit.com"
            else:
                netloc = "old.reddit.com" if want == "old" else "www.reddit.com"
            scheme = "https"
        elif host.endswith("reddit.com") or host.endswith("redd.it"):
            scheme = "https"
        # The scheme is only FORCED for reddit's own hosts. Forcing it always
        # is what broke tests/proxytest.py's local stand-in - and would break
        # any bench server this is ever pointed at - by asking a plain HTTP
        # port for a TLS handshake and reporting the failure as the site
        # being down.
        q = [(k, v) for k, v in urllib.parse.parse_qsl(sp.query)
             if not k.startswith("utm_") and k not in ("share_id", "rdt")]
        path = sp.path
        # RUNG 0: reddit's own public JSON (PROXY-PLAN 17). It is a suffix on
        # the path a reader would have typed, so every link this proxy already
        # mints keeps working and only the fetch changes.
        if (getattr(opts, "reddit_api", "json") == "json"
                and netloc.endswith("reddit.com")
                and not path.startswith("/svc/")
                and not path.endswith(".json")
                and not path.startswith("/api/")):
            # `/r/x/` -> `/r/x/.json`, `/r/x/hot` -> `/r/x/hot.json`. Both
            # are reddit's own forms and the third one - `/r/x/hot/.json` -
            # is a 404, which is what the sorts row was minting.
            path = path + ".json"
            have = set(k for k, _ in q)
            if "raw_json" not in have:
                q.append(("raw_json", "1"))
            if "limit" not in have:
                q.append(("limit", "50" if "/comments/" not in path else "100"))
        return urllib.parse.urlunsplit((scheme, netloc, path,
                                        urllib.parse.urlencode(q), ""))



    # --- reddit's public JSON, and the JavaScript check (PROXY-PLAN 17) ---
    #
    # **The HTML is not reliably there.** Asked by anything that is not a
    # browser, www.reddit.com answers a 1KB page whose whole body is a spinner
    # and a hidden form: a JavaScript challenge (`js_challenge=1`), which the
    # field hit on the first real fetch. The shreddit parser above is correct
    # and was never going to run.
    #
    # So the ladder is three rungs deep and this is rung 0:
    #
    #   1. `.json` - reddit's own public endpoint. No account, no OAuth, no
    #      markup: `title`, `num_comments`, `permalink`, `score`, `author`,
    #      `created_utc`, and a comment tree with `replies` nested inside it.
    #      It is a TENTH the bytes of the page and carries MORE than the page
    #      does, because the reply chains the HTML collapses are simply in it.
    #   2. the shreddit HTML above, for a machine where the JSON is refused
    #      and the page is not;
    #   3. old.reddit's HTML, which is what a signed-in session still gets.
    #
    # ...and the challenge is answered rather than worked around, because the
    # cookie it earns unlocks whichever rung is being asked for. It is small
    # and deliberate on reddit's part: a one-line transform of a literal, in
    # the page, with the answer posted back through a hidden form. `_jsc_eval`
    # does the handful of shapes it takes and REFUSES anything else - a wrong
    # answer is a refusal with extra steps, and the refusal says what it could
    # not read so the next capture can teach it.
    JSC_FN = re.compile(r"\(\s*async\s+(\w+)\s*=>\s*(.+?)\s*\)\s*\(\s*"
                        r"[\"']([^\"']*)[\"']\s*\)", re.S)

    @staticmethod
    def _jsc_terms(expr):
        """Split on the `+`s that are not inside quotes, brackets or calls."""
        out, buf, depth, quote = [], [], 0, ""
        for ch in expr:
            if quote:
                buf.append(ch)
                if ch == quote:
                    quote = ""
                continue
            if ch in "\"'":
                quote = ch
                buf.append(ch)
            elif ch in "([{":
                depth += 1
                buf.append(ch)
            elif ch in ")]}":
                depth -= 1
                buf.append(ch)
            elif ch == "+" and depth == 0:
                out.append("".join(buf))
                buf = []
            else:
                buf.append(ch)
        out.append("".join(buf))
        return out

    def _jsc_eval(self, expr, var, arg):
        v = re.escape(var)
        parts = []
        for raw in self._jsc_terms(expr):
            t = raw.strip()
            m = re.match(r'^"([^"]*)"$', t) or re.match(r"^'([^']*)'$", t)
            if m:
                parts.append(m.group(1))
            elif t == var:
                parts.append(arg)
            elif re.match(r"^%s\.split\((\"\"|'')\)\.reverse\(\)"
                          r"\.join\((\"\"|'')\)$" % v, t) or \
                    re.match(r"^\[\s*\.\.\.\s*%s\s*\]\.reverse\(\)"
                             r"\.join\((\"\"|'')\)$" % v, t):
                parts.append(arg[::-1])
            elif re.match(r"^%s\.toUpperCase\(\)$" % v, t):
                parts.append(arg.upper())
            elif re.match(r"^%s\.toLowerCase\(\)$" % v, t):
                parts.append(arg.lower())
            else:
                m = re.match(r"^%s\.repeat\((\d+)\)$" % v, t)
                if not m:
                    say("reddit's javascript check does something this proxy "
                        "cannot read: %r" % t[:120])
                    return None
                parts.append(arg * int(m.group(1)))
        return "".join(parts)

    # A DESCRIPTIVE User-Agent AND `Accept: application/json`, for the JSON
    # endpoints only. Both are honest and both are what reddit's own API
    # rules ask for - and the field's evidence says they may be the whole
    # difference: asking for `/hot.json` with a Chrome UA and
    # `Accept: text/html,…` came back as 597KB of HTML front page, which is
    # exactly what content negotiation plus "this is a browser, send it the
    # app" looks like from the outside.
    def headers_for(self, url, opts=None):
        if not urllib.parse.urlsplit(url).path.endswith(".json"):
            return None
        ua = getattr(opts, "reddit_ua", "") if opts else ""
        return {"Accept": "application/json, text/plain;q=0.9, */*;q=0.5",
                "User-Agent": ua or REDDIT_UA}

    def _jsc_solve(self, text):
        m = self.JSC_FN.search(text)
        if not m:
            say("reddit sent a javascript check with no transform in it")
            return None
        return self._jsc_eval(m.group(2), m.group(1), m.group(3))

    def after_fetch(self, got, fetcher, opts=None, asked=None, _retry=0,
                    _solved=False):
        """What came back was not the page. `asked` is the URL we REQUESTED.

        **Every decision here is about the URL we asked for and never about
        `got.url`**, and the field is why: reddit answers `/.json` with a
        REDIRECT to `/` and then the challenge, so a `Fetched` carries `/` as
        its url and every path test in the first version looked at the wrong
        one. The challenge was answered, the answer landed on the HTML front
        page - which for a signed-out reader has no posts in it at all - and
        the reader got the shell: `Skip to main content`, `Open menu`, a Log
        In link and nothing else.
        """
        want = asked or got.url
        wsp = urllib.parse.urlsplit(want)
        if got.status == 429:
            # The public endpoint is rate-limited per address, and what comes
            # back is a page about it. Saying so in one line is the whole of
            # what a reader can act on - the alternative is reddit's own
            # 30KB apology rendered as three words.
            return Fetched(got.url, 429, "text/html",
                           "<html><body><p>Reddit is rate-limiting this "
                           "address. Wait a minute and press Reload.</p>"
                           "</body></html>", got.raw_len)
        if _retry > 2 or not got.text:
            return None
        # A challenge page is a spinner and a hidden form - 8KB with reddit's
        # logo in it - and the FORM IS AT THE BOTTOM, under a 6KB inline SVG.
        # The first version looked in the first 4,000 characters and so never
        # saw it: it fell through to the JSON-answered-as-HTML branch below,
        # asked for the same page without `.json`, and got the same challenge.
        # ...and it is answered ONCE per chain. A check that comes back a
        # second time is a refusal wearing the check's clothes - the cookie
        # did not satisfy it - and solving it again just spends the retry
        # budget on the same answer instead of dropping to the next rung.
        small = len(got.text) < 65536
        if (small and not _solved and 'name="js_challenge"' in got.text
                and "solution" in got.text):
            sol = self._jsc_solve(got.text)
            root = build_tree(got.text)
            forms = root.find("form")
            if sol is None or not forms:
                return None                # the caller renders what came back
            fields = {}
            for inp in forms[0].find("input"):
                nm = inp.attrs.get("name", "")
                if nm:
                    fields[nm] = inp.attrs.get("value", "")
            fields["solution"] = sol
            # The page's own script copies `document.location.search` into the
            # form before submitting, so the query we ASKED with rides along.
            for k, v in urllib.parse.parse_qsl(wsp.query):
                fields.setdefault(k, v)
            action = urllib.parse.urljoin(got.url,
                                          forms[0].attrs.get("action", "") or "")
            nxt = "%s?%s" % (action.split("?")[0],
                             urllib.parse.urlencode(fields))
            say("reddit asked for a javascript check - answering it")
            try:
                got2 = fetcher.get(nxt, extra=self.headers_for(nxt, opts))
            except Exception as exc:
                say("the javascript check failed: %s" % exc)
                return None
            # **AND THEN ASK FOR WHAT WE WANTED.** The action is where reddit
            # wants the answer posted, not what we were after; what the
            # exchange buys is the COOKIE. Compared against `want`, so a
            # redirect on the way in cannot make this look like a match.
            if urllib.parse.urlsplit(nxt).path != wsp.path:
                say("...and asking again for %s" % short(want, 70))
                try:
                    got2 = fetcher.get(want,
                                       extra=self.headers_for(want, opts))
                except Exception as exc:
                    say("the page after the javascript check failed: %s" % exc)
                    return got2
            return self.after_fetch(got2, fetcher, opts, want, _retry + 1,
                                    True) or got2

        ctype = (got.ctype or "").split(";")[0].strip()
        page = ctype in ("text/html", "application/xhtml+xml")
        posts = -1                        # -1 = not JSON at all
        if not page:
            try:
                posts = len([c for c in self._j_kids(json.loads(got.text))
                             if c.get("kind") == "t3"])
            except ValueError:
                posts = -1
        # THE FRONT PAGE IS A SPECIAL CASE and the field found it twice.
        # `/.json` answers a Listing in a browser and answers somebody else
        # with a redirect, a challenge or an empty list; `/hot.json` is an
        # ordinary listing endpoint and answers either way. Signed out, `hot`
        # is what the front page shows anyway, so this changes what is asked
        # for and not what is seen - and it is tried BEFORE the HTML rung,
        # because the signed-out HTML front page carries no posts at all.
        if _retry < 2 and wsp.path in ("/.json", "/best.json") and posts <= 0:
            say("reddit's front page did not answer as a listing (%s) - "
                "asking for /hot.json instead"
                % (ctype or "no type"))
            hot = urllib.parse.urlunsplit(
                (wsp.scheme, wsp.netloc, "/hot.json", wsp.query, ""))
            try:
                got2 = fetcher.get(hot, extra=self.headers_for(hot, opts))
            except Exception:
                return None
            return self.after_fetch(got2, fetcher, opts, hot, _retry + 1,
                                    _solved) or got2
        # A `.json` that came back as a PAGE is a refusal, not an answer: ask
        # for the same thing as HTML rather than rendering reddit's block page
        # (rung 1). One retry, and only downwards.
        if _retry < 2 and wsp.path.endswith(".json") and page:
            plain = urllib.parse.urlunsplit(
                (wsp.scheme, wsp.netloc, wsp.path[:-5], wsp.query, ""))
            say("reddit answered the JSON endpoint with a page - trying the "
                "HTML one")
            try:
                got2 = fetcher.get(plain, extra=self.headers_for(plain, opts))
            except Exception:
                return None
            return self.after_fetch(got2, fetcher, opts, plain,
                                    _retry + 1, _solved) or got2
        return None

    # --- the JSON, rendered ------------------------------------------------
    def _j_ago(self, epoch):
        try:
            d = int(time.time() - float(epoch))
        except (TypeError, ValueError):
            return ""
        return self._ago_words(d)

    def _j_html(self, raw, em, plain=""):
        """`body_html` through the ordinary emitter, `body` if there is none.

        The html field is a fragment of reddit's own markdown rendering - <p>,
        <a>, <blockquote>, <ul> - so it goes through the same emitter as any
        page and comes out with its links minted. It arrives HTML-ESCAPED
        (`&lt;p&gt;`) unless `raw_json=1` was asked for, and both forms turn
        up in the wild, so it is unescaped when it looks escaped rather than
        on the strength of the request.
        """
        raw = raw or ""
        if raw and "<" not in raw and "&lt;" in raw:
            raw = html.unescape(raw)
        if raw.strip():
            return em.blocks(build_tree(raw))
        plain = (plain or "").strip()
        if not plain:
            return []
        return ["<p>%s</p>" % esc(collapse(fold_text(para)))
                for para in re.split(r"\n\s*\n", plain) if para.strip()]

    @staticmethod
    def _j_kids(listing):
        if not isinstance(listing, dict):
            return []
        data = listing.get("data") or {}
        return [c for c in (data.get("children") or []) if isinstance(c, dict)]

    def _j_listing(self, doc, url, ctx, em):
        kids = self._j_kids(doc)
        posts = [c.get("data") or {} for c in kids if c.get("kind") == "t3"]
        if not posts:
            return None
        out = self._nav(url, ctx, self.WWW)
        for d in posts:
            title = collapse(fold_text(d.get("title") or "")).strip()
            if not title:
                continue
            perma = d.get("permalink") or ""
            dest = d.get("url_overridden_by_dest") or d.get("url") or perma
            if d.get("is_self"):
                dest = perma
            thref = ctx.href(urllib.parse.urljoin(self.WWW, dest))
            chref = ctx.href(urllib.parse.urljoin(self.WWW, perma))
            line = ('<a href="%s">%s</a>' % (thref, esc(title))) if thref \
                else esc(title)
            n = d.get("num_comments")
            n = str(n) if isinstance(n, int) else ""
            label = ("%s %s" % (n, "comment" if n == "1" else "comments")).strip()
            second = ('<a href="%s">%s</a>' % (chref, esc(label))) if chref \
                else esc(label)
            sub = d.get("subreddit_name_prefixed") or \
                (("r/" + d["subreddit"]) if d.get("subreddit") else "")
            if sub:
                shref = ctx.href("%s/%s/" % (self.WWW, sub.strip("/")))
                second += "  " + (('<a href="%s">%s</a>' % (shref, esc(sub)))
                                  if shref else esc(sub))
            out.append("<p>%s<br>%s</p>" % (line, second))
        after = ((doc.get("data") or {}).get("after") or "") \
            if isinstance(doc, dict) else ""
        if after:
            # The endpoint's OWN pagination, which the HTML does not admit to
            # having: `after` is a fullname and `count` is only cosmetic.
            sp = urllib.parse.urlsplit(url)
            q = dict(urllib.parse.parse_qsl(sp.query))
            q["after"] = after
            q["count"] = str(len(posts))
            href = ctx.href(urllib.parse.urlunsplit(
                (sp.scheme, sp.netloc, sp.path, urllib.parse.urlencode(q), "")))
            if href:
                out.append('<p><a href="%s">More posts</a></p>' % href)
        return out

    def _j_comments(self, doc, url, ctx, em, opts):
        post = None
        trees = []
        for part in (doc if isinstance(doc, list) else [doc]):
            for c in self._j_kids(part):
                if c.get("kind") == "t3" and post is None:
                    post = c.get("data") or {}
                elif c.get("kind") in ("t1", "more"):
                    trees.append(c)
        # The post's own path, for naming a collapsed subtree below. It is
        # an ARGUMENT and not an attribute on self: the server is threaded and
        # the sites are one shared instance apiece, so a second reader would
        # otherwise re-point the first reader's `more` links mid-page.
        ppath = (post or {}).get("permalink") or ""
        out = []
        if post:
            out.append("<h1>%s</h1>"
                       % esc(collapse(fold_text(post.get("title") or ""))
                             or "(untitled)"))
            sub = post.get("subreddit_name_prefixed") or ""
            if sub:
                href = ctx.href("%s/%s/" % (self.WWW, sub.strip("/")))
                out.append("<p>%s</p>" % (('<a href="%s">%s</a>'
                                           % (href, esc(sub))) if href
                                          else esc(sub)))
            body = self._j_html(post.get("selftext_html"), em,
                                post.get("selftext"))
            if body:
                out.extend(body)
            else:
                dest = post.get("url_overridden_by_dest") or post.get("url") or ""
                href = ctx.href(dest) if dest else None
                if href:
                    out.append('<p><a href="%s">%s</a></p>'
                               % (href, esc(short(dest, 70))))
        out.append("<hr>")
        n = self._j_thread(trees, 0, ctx, em, opts, out, [0], ppath)
        if post and n:
            total = post.get("num_comments")
            if isinstance(total, int) and total > n:
                out.append("<p>%s</p>"
                           % esc("[%d of %d comments on this page]" % (n, total)))
        return out if (n or post) else None

    def _j_thread(self, kids, depth, ctx, em, opts, out, count, ppath=""):
        """The tree, depth first, which is the order it is READ in.

        `replies` is a whole Listing nested inside each comment, so the depth
        is where we are rather than a field to trust - and a `more` node is
        the same collapsed chain the HTML shows a button for, linked at the
        depth it stands at.
        """
        for c in kids:
            if count[0] >= opts.reddit_maxcomments:
                out.append("<p>%s</p>"
                           % esc("[thread cut at %d comments]" % count[0]))
                return count[0]
            d = c.get("data") or {}
            pad = self._pad_of(depth, opts)
            if c.get("kind") == "more":
                more = d.get("count") or len(d.get("children") or [])
                # A `more` carries a permalink only sometimes; when it does
                # not, the first child's id is enough to name the subtree,
                # because a comment's own permalink under the post renders
                # that comment and everything below it.
                link = d.get("permalink") or ""
                kid = (d.get("children") or [None])[0]
                if not link and kid and ppath:
                    link = "%scomment/%s/" % (ppath, kid)
                href = ctx.href(urllib.parse.urljoin(self.WWW, link)) \
                    if link else None
                text = "[%d more replies]" % more if more else "[more replies]"
                out.append("<p>%s%s</p>"
                           % (pad, ('<a href="%s">%s</a>' % (href, esc(text)))
                              if href else esc(text)))
                continue
            author = d.get("author") or ""
            body = self._j_html(d.get("body_html"), em, d.get("body"))
            if not body and author in ("", "[deleted]"):
                continue
            head = author or "?"
            score = d.get("score")
            if isinstance(score, int) and not d.get("score_hidden"):
                head += " %d point%s" % (score, "" if score == 1 else "s")
            ago = self._j_ago(d.get("created_utc"))
            if ago:
                head += " " + ago
            self._emit_comment(out, pad, head, body, opts, depth)
            count[0] += 1
            reps = d.get("replies")
            if isinstance(reps, dict):
                self._j_thread(self._j_kids(reps), depth + 1, ctx, em, opts,
                               out, count, ppath)
        if depth == 0:
            self._tables_close(out)
        return count[0]

    # --- modern reddit: the shreddit elements (PROXY-PLAN 16) -------------
    #
    # **www.reddit.com needs no sign-in, and its markup is EASIER to read
    # than old.reddit's, not harder.** Every field this proxy wants is an
    # attribute on a custom element that reddit renders on the server:
    #
    #   <shreddit-post permalink= post-title= comment-count= score= author=
    #                  subreddit-prefixed-name= created-timestamp=
    #                  content-href= id="t3_...">
    #   <shreddit-comment author= depth= score= created= thingid="t1_..."
    #                     permalink=>
    #
    # so there is no class-name archaeology, no tagline sentence to parse for
    # a comment count, and a reply's depth is a NUMBER rather than a count of
    # nested `div.child`s. `Builder` keeps unknown elements and their
    # attributes, so all of this is ordinary tree work.
    #
    # Three things it cannot read, because they are not in the document:
    # the feed's second screenful, a deep reply chain, and the "N more
    # replies" tails - all of which the desktop page fetches later with
    # JavaScript. The cursor for the first of those IS in the document
    # (`faceplate-partial slot="load-after"`), so `_m_more` emits it as a
    # plain link and the fragment it returns is more shreddit-posts, which
    # this same code reads. The other two are simply absent, and a proxy that
    # pretended otherwise would be inventing a thread.
    def _m_ago(self, iso):
        """`2026-08-18T14:18:25.910000+0000` -> `4 hours ago`.

        old.reddit ships the phrase; modern ships the instant, so this is the
        one field the new markup makes us compute rather than read. It is
        deliberately COARSE - `4 hours ago`, not `4h 12m` - because it is the
        old page's wording and because a listing is read, not audited.
        """
        m = re.match(r"(\d{4})-(\d\d)-(\d\d)[T ](\d\d):(\d\d):(\d\d)", iso or "")
        if not m:
            return ""
        try:
            when = calendar.timegm([int(x) for x in m.groups()] + [0, 0, 0])
        except Exception:
            return ""
        return self._ago_words(int(time.time() - when))

    @staticmethod
    def _ago_words(d):
        if d < 0:                      # a clock that disagrees is not an error
            d = 0
        for n, unit in ((31536000, "year"), (2592000, "month"), (86400, "day"),
                        (3600, "hour"), (60, "minute")):
            if d >= n:
                v = d // n
                return "%d %s%s ago" % (v, unit, "" if v == 1 else "s")
        return "just now"

    def _m_content(self, node, ident, kind, em):
        """The rich-text body of one post or one comment.

        Matched on the EXACT id (`t3_x-post-rtjson-content`) and not on
        `slot="comment"`, because a comment's children are inside it and a
        comment's own body contains a div named `-post-rtjson-content` too.
        Either looser test returns a parent quoting its own replies, which is
        a plausible-looking wrong answer rather than a failure.
        """
        want = ("%s-%s-rtjson-content" % (ident, kind)) if ident else ""
        for d in node.find("div"):
            i = d.attrs.get("id", "")
            if (i == want) if want else i.endswith("-%s-rtjson-content" % kind):
                return em.blocks(d)
        return []

    def _m_more(self, root, ctx, label="More posts"):
        """The feed's own next-page cursor, as a link.

        `<faceplate-partial slot="load-after" src="/svc/shreddit/...?after=">`
        is what the desktop page fetches when the reader reaches the bottom;
        the endpoint answers a FRAGMENT of shreddit-posts, which `_m_listing`
        reads exactly as it reads a page. So pagination costs one link and no
        new parser.
        """
        for n in root.find("faceplate-partial"):
            if n.attrs.get("slot") != "load-after":
                continue
            href = ctx.href(n.attrs.get("src", ""))
            if href:
                return ['<p><a href="%s">%s</a></p>' % (href, esc(label))]
        return []

    def _m_listing(self, root, url, ctx, em):
        posts = root.find("shreddit-post")
        if not posts:
            return None
        base = self._base(url)
        out = self._nav(url, ctx, base)
        for p in posts:
            a = p.attrs
            title = collapse(fold_text(a.get("post-title", ""))).strip()
            if not title:
                continue
            perma = a.get("permalink", "")
            # The TITLE goes where the post goes - the article for a link
            # post, the thread for a self post - and the comment count is the
            # way to the thread either way (the old parser's rule, and the
            # reason `content-href` is preferred here).
            dest = a.get("content-href", "") or perma
            thref = ctx.href(dest) if dest else None
            chref = ctx.href(perma) if perma else None
            line = ('<a href="%s">%s</a>' % (thref, esc(title))) if thref \
                else esc(title)
            n = a.get("comment-count", "")
            n = n if n.isdigit() else ""
            label = ("%s %s" % (n, "comment" if n == "1" else "comments")).strip()
            second = ('<a href="%s">%s</a>' % (chref, esc(label))) if chref \
                else esc(label)
            sub = a.get("subreddit-prefixed-name", "") or \
                (("r/" + a["subreddit-name"]) if a.get("subreddit-name") else "")
            if sub:
                shref = ctx.href("%s/%s/" % (base, sub.strip("/")))
                second += "  " + (('<a href="%s">%s</a>' % (shref, esc(sub)))
                                  if shref else esc(sub))
            out.append("<p>%s<br>%s</p>" % (line, second))
        out.extend(self._m_more(root, ctx))
        return out

    def _m_comments(self, root, url, ctx, em, opts):
        posts = root.find("shreddit-post")
        base = self._base(url)
        out = []
        if posts:
            p = posts[0]
            a = p.attrs
            title = collapse(fold_text(a.get("post-title", ""))).strip()
            # <h1> FIRST so `render` folds it into the page title: a 17-row
            # CGA window cannot spare a line to say the same sentence twice.
            out.append("<h1>%s</h1>" % esc(title or "(untitled)"))
            sub = a.get("subreddit-prefixed-name", "")
            if sub:
                href = ctx.href("%s/%s/" % (base, sub.strip("/")))
                out.append("<p>%s</p>" % (('<a href="%s">%s</a>'
                                           % (href, esc(sub))) if href
                                          else esc(sub)))
            body = self._m_content(p, a.get("id", ""), "post", em)
            if body:
                out.extend(body)
            else:
                # An image, a gallery or a link post: the guest cannot show
                # the picture, so what it gets is the address of it.
                dest = a.get("content-href", "")
                href = ctx.href(dest) if dest else None
                if href:
                    out.append('<p><a href="%s">%s</a></p>'
                               % (href, esc(short(dest, 70))))
        out.append("<hr>")
        n = self._m_thread(root, ctx, em, opts, out)
        # WHAT IS NOT ON THE PAGE IS WORTH A LINE. Reddit server-renders the
        # first screenful of a thread and fetches the rest as the reader
        # scrolls, so a 612-comment post arrives with 25 in it. Saying so
        # costs one line and stops the reader concluding the proxy ate them.
        if posts and n:
            total = posts[0].attrs.get("comment-count", "")
            if total.isdigit() and int(total) > n:
                out.append("<p>%s</p>"
                           % esc("[%d of %s comments on this page]" % (n, total)))
        return out if (n or posts) else None

    def _m_order(self, node, out):
        """Comments AND the `more replies` stubs, in document order.

        `find` would give either one or the other; the stubs have to be
        interleaved because each belongs to the chain it is standing at the
        bottom of, and a list of them at the end of the page says nothing
        about which reply they continue.
        """
        for k in node.elems():
            if k.tag == "shreddit-comment":
                out.append(k)
                self._m_order(k, out)
            elif (k.tag == "faceplate-partial"
                  and "more-comments-partial" in k.attrs.get("class", "")):
                out.append(k)
            else:
                self._m_order(k, out)
        return out

    def _m_thread(self, root, ctx, em, opts, out):
        n = 0
        step = max(0, min(4, opts.reddit_indent))
        for c in self._m_order(root, []):
            a = c.attrs
            if c.tag == "faceplate-partial":
                # A collapsed reply chain. Its own `startingDepth` is what
                # says where it belongs, so it lands under the reply it
                # continues rather than at the left margin.
                src = a.get("src", "")
                href = ctx.href(src)
                m = re.search(r"startingDepth=(\d+)", src)
                d = int(m.group(1)) if m else 1
                pad = self._pad_of(d, opts)
                if href:
                    out.append('<p>%s<a href="%s">[more replies]</a></p>'
                               % (pad, href))
                continue
            author = a.get("author", "")
            ident = a.get("thingid", "")
            body = self._m_content(c, ident, "comment", em)
            if not body and author in ("", "[deleted]"):
                continue
            try:
                depth = int(a.get("depth", "0"))
            except ValueError:
                depth = 0
            pad = self._pad_of(depth, opts)
            head = author or "?"
            pts = a.get("score", "")
            if pts.lstrip("-").isdigit():
                head += " %s point%s" % (pts, "" if pts == "1" else "s")
            ago = self._m_ago(a.get("created", "") or
                              a.get("created-timestamp", ""))
            if ago:
                head += " " + ago
            self._emit_comment(out, pad, head, body, opts, depth)
            n += 1
            if n >= opts.reddit_maxcomments:
                out.append("<p>%s</p>" % esc("[thread cut at %d comments]" % n))
                break
        self._tables_close(out)
        return n

    # --- how a thread is LAID OUT (PROXY-PLAN 18) -------------------------
    #
    # A reply tree on 79 columns of a 17-row window is hard to follow however
    # it is drawn, so this is a knob rather than a decision: `--reddit-thread`
    # picks between four shapes that cost the guest different things, and the
    # only way to choose is to look at them on the machine.
    #
    #   quote  `>` a level, the header and the first line of the reply in one
    #          paragraph. The default, and the cheapest.
    #   bar    the same with `|`, which draws a straighter gutter than `>` at
    #          the price of looking like a table that is not one.
    #   rule   quote plus a horizontal rule between top-level threads, so the
    #          eye has somewhere to stop. Costs one line a thread.
    #   table  a real table - a narrow indent column and the comment beside
    #          it - so the guest rules the gutter and the wrap hangs under the
    #          text rather than under the marker. The dearest by a distance:
    #          every row is a border, and a table is the ATOMIC unit of layout
    #          on the guest, so it is emitted in CHUNKS or a long thread
    #          cannot be paginated at all.
    def _pad_of(self, depth, opts):
        step = max(0, min(4, opts.reddit_indent))
        n = min(depth * step, opts.reddit_maxindent)
        if not n:
            return ""
        return esc(("|" if opts.reddit_thread == "bar" else ">") * n) + " "

    def _emit_comment(self, out, pad, head, body, opts=None, depth=0):
        """The header and the first paragraph as ONE <p> with a <br>: two
        paragraphs put a blank line between a name and what it said, which
        doubles the height of a thread on a 17-row window."""
        style = getattr(opts, "reddit_thread", "quote") if opts else "quote"
        if style == "table":
            return self._emit_row(out, pad, head, body, opts, depth)
        # ...and NOT before the first one: the post and the comments are
        # already separated by a rule, and two in a row reads as a mistake.
        if style == "rule" and depth == 0 and out and out[-1] != "<hr>":
            out.append("<hr>")
        first = body[0] if body else ""
        m = re.match(r"^<p>(.*)</p>$", first, re.S) if first else None
        if m:
            out.append("<p>%s%s<br>%s%s</p>" % (pad, esc(head), pad, m.group(1)))
            body = body[1:]
        else:
            out.append("<p>%s%s</p>" % (pad, esc(head)))
        for b in body:
            out.append(self._pad(b, pad))

    # A table row per comment, accumulated into blocks of TROWS. The open and
    # close tags live in the SAME block as their rows, because `paginate`
    # splits between blocks and a table cut in half is not a table.
    TROWS = 10

    def _emit_row(self, out, pad, head, body, opts, depth):
        text = " ".join(re.sub(r"<[^>]*>", " ", b) for b in body)
        text = collapse(text).strip()
        row = ("<tr><td>%s</td><td>%s%s</td></tr>"
               % (pad.strip(), esc(head) + (" - " if text else ""), text))
        if out and out[-1].startswith("<table>") and \
                out[-1].count("<tr>") < self.TROWS and \
                not out[-1].endswith("</table>"):
            out[-1] += row
        else:
            out.append("<table>" + row)

    @staticmethod
    def _tables_close(out):
        """Shut whichever table block is open. Called at the end of a thread
        and nowhere else - an unterminated <table> renders as a box with the
        rest of the page inside it."""
        for i, b in enumerate(out):
            if b.startswith("<table>") and not b.endswith("</table>"):
                out[i] = b + "</table>"

    # --- finding things ----------------------------------------------------
    def _things(self, root, kind):
        out = []
        for n in root.find("div"):
            if not has_class(n, "thing"):
                continue
            if n.attrs.get("data-type", "") == kind:
                out.append(n)
            elif not n.attrs.get("data-type") and has_class(n, kind):
                out.append(n)
        return out

    def _title_a(self, thing):
        for a in thing.find("a"):
            if has_class(a, "title"):
                return a
        return None

    def _body(self, thing, em):
        """A `usertext-body` belonging to THIS thing and not to a reply.

        `find` is recursive and a comment's children are inside it, so the
        first match walking down from a parent is the parent's own - but only
        because `div.child` comes after `div.entry` in the document. Anything
        that stops being true here shows up as a comment quoting its own
        replies, which is a plausible-looking wrong answer.
        """
        for d in thing.find("div"):
            if has_class(d, "usertext-body"):
                return em.blocks(d)
        return []

    def _score(self, thing):
        """`data-score` first. The DOM has THREE `score` spans - dislikes,
        unvoted, likes, one below and one above the truth - and the container
        they sit in also carries `unvoted`, so a careless class test returns
        `184118421843` or the whole tagline."""
        n = thing.attrs.get("data-score", "")
        if n.isdigit():
            return n
        for d in thing.find("div") + thing.find("span"):
            if has_class(d, "score") and has_class(d, "unvoted"):
                t = collapse(fold_text(d.text())).strip()
                if t and len(t) <= 14:
                    return t.split()[0]
        return ""

    def _ago(self, thing):
        for t in thing.find("time"):
            if has_class(t, "live-timestamp"):
                return collapse(fold_text(t.text())).strip()
        return ""

    def _base(self, url):
        """The reddit host THIS page came from, so a row of links built here
        stays on it. Mixing the two hosts in one page is what makes a session
        look like it half-works: old.reddit answers a login wall logged out
        and www does not, so a categories row pointing at the other one is a
        dead end reached from a page that rendered perfectly."""
        sp = urllib.parse.urlsplit(url)
        return ("%s://%s" % (sp.scheme or "https", sp.netloc)) if sp.netloc \
            else self.WWW

    def _nav(self, url, ctx, base=None):
        """One line: the categories, then this listing's sorts."""
        base = base or self._base(url)
        bits = []
        for label, path in self.CATS:
            href = ctx.href(base + path)
            if href:
                bits.append('<a href="%s">%s</a>' % (href, label))
        path = urllib.parse.urlsplit(url).path
        path = re.sub(r"/(hot|new|top|rising|controversial|best)/?$", "/", path)
        if not path.endswith("/"):
            path += "/"
        # NO TRAILING SLASH ON A SORT, and the field found it: reddit's
        # endpoint is `/r/x/hot.json`, so `/r/x/hot/` + `.json` is
        # `/r/x/hot/.json`, which 404s. `/r/x/` + `.json` is the form that
        # works for a listing, so the two are not the same rule and the
        # difference lives here rather than in `upstream`.
        for srt in self.SORTS:
            href = ctx.href(base + path + srt)
            if href:
                bits.append('<a href="%s">%s</a>' % (href, srt))
        return ["<p>%s</p>" % " ".join(bits)] if bits else []

    # --- the listing -------------------------------------------------------
    def listing(self, root, url, ctx, em):
        things = self._things(root, "link")
        if not things:
            return None
        out = self._nav(url, ctx, self.OLD)
        for thing in things:
            ta = self._title_a(thing)
            title = (em.inline(ta).strip() if ta is not None else "") or \
                collapse(fold_text(thing.attrs.get("data-url", ""))).strip()
            if not title:
                continue
            # data- first, the DOM second. old.reddit emits every one of
            # these attributes today (the captures prove it) and the fallback
            # is what keeps a listing working the day one of them goes away -
            # rule 3, at the level of a field rather than a page.
            ca = None
            for a in thing.find("a"):
                if has_class(a, "comments"):
                    ca = a
                    break
            perma = thing.attrs.get("data-permalink", "")
            if perma:
                chref = ctx.href(self.OLD + perma)
            elif ca is not None:
                chref = ctx.href(ca.attrs.get("href"))
            else:
                chref = None
            # The TITLE goes to the post's own destination - the article for a
            # link post, the thread for a self post - and the comment count is
            # the way to the thread either way. Pointing both at the thread
            # would make one of the two links dead weight on a 95-byte path.
            dest = (thing.attrs.get("data-url", "")
                    or (ta.attrs.get("href", "") if ta is not None else "")
                    or (self.OLD + perma if perma else ""))
            thref = ctx.href(dest) if dest else None
            line = ('<a href="%s">%s</a>' % (thref, title)) if thref else title
            n = thing.attrs.get("data-comments-count", "")
            if not n.isdigit() and ca is not None:
                m = re.search(r"\d+", collapse(fold_text(ca.text())))
                n = m.group(0) if m else ""
            word = "comment" if n == "1" else "comments"
            if not n.isdigit():
                n, word = "", "comments"
            label = ("%s %s" % (n, word)).strip()
            second = ('<a href="%s">%s</a>' % (chref, label)) if chref else label
            sub = thing.attrs.get("data-subreddit-prefixed", "") or \
                (("r/" + thing.attrs["data-subreddit"])
                 if thing.attrs.get("data-subreddit") else "")
            if sub:
                shref = ctx.href("%s/%s/" % (self.OLD, sub.strip("/")))
                second += "  " + (('<a href="%s">%s</a>' % (shref, esc(sub)))
                                  if shref else esc(sub))
            # ONE <p> with a <br> in it, not two paragraphs: the guest puts a
            # blank line between paragraphs, so two would double the height of
            # a listing and separate a title from its own comment count.
            out.append("<p>%s<br>%s</p>" % (line, second))
        out.extend(self._nextprev(root, ctx))
        return out

    def _nextprev(self, root, ctx):
        out = []
        for s in root.find("span"):
            if not has_class(s, "nextprev"):
                continue
            for a in s.find("a"):
                href = ctx.href(a.attrs.get("href"))
                if href:
                    out.append('<a href="%s">%s</a>'
                               % (href, esc(collapse(fold_text(a.text())).strip()
                                            or "more")))
        return ["<p>%s</p>" % " | ".join(out)] if out else []

    # --- one thread --------------------------------------------------------
    def _depth(self, node):
        d, p = 0, node.parent
        while p is not None and d < 40:
            if p.tag == "div" and has_class(p, "child"):
                d += 1
            p = p.parent
        return d

    def comments(self, root, url, ctx, em, opts):
        posts = self._things(root, "link")
        out = []
        if posts:
            post = posts[0]
            ta = self._title_a(post)
            if ta is not None:
                # <h1> FIRST so `render` folds it into the page title: the
                # chrome draws the title anyway and a 17-row CGA window cannot
                # spare a line to say the same sentence twice.
                out.append("<h1>%s</h1>" % (em.inline(ta).strip() or "(untitled)"))
            sub = post.attrs.get("data-subreddit-prefixed", "")
            if sub:
                href = ctx.href("%s/%s/" % (self.OLD, sub.strip("/")))
                out.append("<p>%s</p>" % (('<a href="%s">%s</a>'
                                           % (href, esc(sub))) if href
                                          else esc(sub)))
            body = self._body(post, em)
            if body:
                out.extend(body)
            else:
                dest = post.attrs.get("data-url", "")
                href = ctx.href(dest) if dest else None
                if href:
                    out.append('<p><a href="%s">%s</a></p>'
                               % (href, esc(short(dest, 70))))
        out.append("<hr>")
        n = self._thread(root, ctx, em, opts, out)
        return out if (n or posts) else None

    def _thread(self, root, ctx, em, opts, out):
        n = 0
        step = max(0, min(4, opts.reddit_indent))
        for c in root.find("div"):
            if not (has_class(c, "thing") and
                    (c.attrs.get("data-type") == "comment"
                     or has_class(c, "comment"))):
                continue
            author = c.attrs.get("data-author", "")
            if not author:
                for a in c.find("a"):
                    if has_class(a, "author"):
                        author = collapse(fold_text(a.text())).strip()
                        break
            body = self._body(c, em)
            if not body and author in ("", "[deleted]"):
                continue
            # The indent is CHARACTERS, because the guest has no indent: it
            # draws <blockquote> as a bare paragraph break. One `>` a level
            # keeps a deep thread readable on 79 cells where a box would have
            # squeezed it to nothing by the fourth reply.
            depth = self._depth(c)
            pad = self._pad_of(depth, opts)
            head = author or "?"
            pts = self._score(c)
            if pts:
                head += " %s point%s" % (pts, "" if pts == "1" else "s")
            ago = self._ago(c)
            if ago:
                head += " " + ago
            self._emit_comment(out, pad, head, body, opts, depth)
            n += 1
            if n >= opts.reddit_maxcomments:
                out.append("<p>%s</p>"
                           % esc("[thread cut at %d comments]" % n))
                break
        self._tables_close(out)
        return n

    @staticmethod
    def _pad(block, pad):
        if not pad:
            return block
        m = re.match(r"^<(p|li)>(.*)</\1>$", block, re.S)
        if m:
            return "<%s>%s%s</%s>" % (m.group(1), pad, m.group(2), m.group(1))
        return block

    # --- entry -------------------------------------------------------------
    def transform(self, text, url, opts, links, selfbase):
        try:
            head = text.lstrip()[:1]
            if head in ("{", "["):
                return self._json_transform(text, url, opts, links, selfbase)
            # TEMPLATES ARE CONTENT HERE. Modern reddit renders the feed into
            # `<template for="s_…">` blocks for hydration to clone, so a
            # JavaScript-less parse that skips them sees a 597KB page with no
            # posts in it (PROXY-PLAN 21). This handler only looks for
            # `shreddit-*` elements by tag, so anything else a template holds
            # cannot mislead it.
            root = build_tree(text, templates=True)
            tnode = root.find("title")
            title = collapse(fold_text(tnode[0].text())).strip() if tnode else "reddit"
            title = re.sub(r"\s*:\s*reddit\.com$", "", title, flags=re.I)
            ctx = Ctx(url, links, opts, selfbase)
            ctx.stats["main"] = "reddit"
            em = Emitter(ctx)
            # WHICH REDDIT IS THIS? Asked of the markup and never of the host,
            # because either host can answer with either page: old.reddit
            # redirects to a modern login wall logged out, and www.reddit
            # serves an old-style page to some crawlers. The elements are the
            # evidence, and if neither is there rule 3 sends it to the
            # generic pipeline.
            if root.find("shreddit-post") or root.find("shreddit-comment"):
                ctx.stats["main"] = "reddit(www)"
                path = urllib.parse.urlsplit(url).path
                thread = ("/comments/" in path
                          or bool(root.find("shreddit-comment")))
                blocks = (self._m_comments(root, url, ctx, em, opts) if thread
                          else self._m_listing(root, url, ctx, em))
                title = re.sub(r"\s*[-\u2013]\s*Reddit\s*$", "", title)
                return (title, blocks, ctx) if blocks else None
            if root.find("shreddit-app") or root.find("shreddit-title"):
                # MODERN MARKUP WITH NOTHING IN IT, which is what a signed-out
                # www.reddit.com front page IS: the posts are fetched by
                # script. The generic pipeline can read this page - it is full
                # of menus, a search box and a Log In link - and reading it is
                # WORSE than saying so, because the reader gets a page that
                # looks like reddit and contains none of it. This is the one
                # place rule 3 is overridden, and `Full` still shows the
                # reduction (PROXY-PLAN 18.1: Full skips this handler).
                say("reddit answered a page with no posts in the HTML - the "
                    "posts on this one are fetched by script")
                return ("reddit",
                        ["<p>%s</p>" % esc(
                            "Reddit sent the page frame and no posts: on this "
                            "page they are fetched by script, which this "
                            "browser cannot run."),
                         "<p>%s</p>" % esc(
                             "Reload to try reddit's own feed again, or press "
                             "Full to see the page as it arrived."),
                         '<p><a href="/rd/r/vintagecomputing/">'
                         'A subreddit works</a></p>'], ctx)
            drop_junk(root, aggressive=False, furniture=False)
            thread = any(has_class(d, "commentarea") for d in root.find("div"))
            blocks = (self.comments(root, url, ctx, em, opts) if thread
                      else self.listing(root, url, ctx, em))
            if not blocks:
                return None                 # rule 3: fall back, do not fail
            return title, blocks, ctx
        except Exception:
            say("reddit handler failed, falling back to generic:\n%s"
                % traceback.format_exc())
            return None

    def _json_transform(self, text, url, opts, links, selfbase):
        """rung 0: what came back was the API, not a page."""
        try:
            doc = json.loads(text)
        except ValueError:
            return None
        # An ERROR OBJECT is reddit answering, and it renders as nothing at
        # all through the generic pipeline - which is what `(empty page)` in
        # the field looks like. Say what it said instead.
        if isinstance(doc, dict) and ("error" in doc or "message" in doc):
            ctx = Ctx(url, links, opts, selfbase)
            ctx.stats["main"] = "reddit(json)"
            why = "%s %s" % (doc.get("error", ""), doc.get("message", ""))
            return ("reddit says no",
                    ["<p>%s</p>" % esc("Reddit answered: %s" % why.strip()),
                     "<p>%s</p>" % esc("If that is 429, wait a minute and "
                                       "press Reload.")], ctx)
        ctx = Ctx(url, links, opts, selfbase)
        ctx.stats["main"] = "reddit(json)"
        em = Emitter(ctx)
        thread = isinstance(doc, list) or "/comments/" in \
            urllib.parse.urlsplit(url).path
        blocks = (self._j_comments(doc, url, ctx, em, opts) if thread
                  else self._j_listing(doc, url, ctx, em))
        if not blocks:
            # There IS no generic reading of a JSON body, so falling through
            # to the pipeline here produces a blank page and a reader who
            # cannot tell that from reddit being down. Name what arrived.
            kind = doc.get("kind", "?") if isinstance(doc, dict) else "list"
            n = len(self._j_kids(doc)) if isinstance(doc, dict) else len(doc)
            say("reddit's JSON had nothing in it: kind=%s, %d children"
                % (kind, n))
            return ("reddit",
                    ["<p>%s</p>" % esc(
                        "Reddit answered %s of JSON with nothing in it that "
                        "this proxy could lay out (kind %s, %d children). "
                        "Press Full for the raw reply."
                        % (human(len(text)), kind, n))], ctx)
        title = "reddit"
        for b in blocks:
            m = re.match(r"^<h1>(.*)</h1>$", b, re.S)
            if m:
                title = re.sub(r"<[^>]*>", "", m.group(1))
                break
        else:
            sp = urllib.parse.urlsplit(url).path
            m = re.match(r"^/r/([^/]+)/", sp)
            if m:
                title = "r/" + m.group(1)
        return title, blocks, ctx


SITES = [RedditSite()]


def site_for(url, opts):
    if opts.sites == "off":
        return None
    for s in SITES:
        if s.matches(url):
            return s
    return None


# =============================================================================
# 11. THE SERVER
#
# An ORIGIN server, not a CONNECT proxy, and that is a decision rather than a
# shortcut: the browser has no proxy setting and sends origin-form requests
# (`GET /path HTTP/1.0` with a Host: header), so anything else would need a
# change to the guest - and the guest is where changes are expensive. Point
# the location bar at this machine and every URL is a path on it.
#
# HTTP/1.0 and `Connection: close` on every answer, because that is exactly
# what brnet.inc is written against: no chunked encoding, no keep-alive, no
# compression, a Content-Length and a close.
# =============================================================================
# TWO BOXES AND ONE BUTTON, and that is a guest limit rather than a taste:
# the browser holds exactly ONE form per page (BR_FMAX is four text fields
# inside it), so an address form and a search form cannot both exist. The
# route decides on what came back - an address wins, because typing one is
# deliberate where an old search term is just still sitting in the box.
HOME_TEXT = """<p>%(nav)s</p><h1>os8088 proxy</h1>
<form action="%(base)s/s" method="get">
<p>Address <input type="text" name="u" value="" size="24"></p>
<p>Search  <input type="text" name="q" value="" size="24"></p>
<p><input type="submit" value="Go"></p>
</form>
%(marks)s
<hr><p>%(stat)s</p>"""


class Server(ThreadingHTTPServer):
    """The same server for both front ends, and it never touches stderr.

    `socketserver.handle_error` prints a traceback to `sys.stderr`, and a
    PyInstaller `--noconsole` binary HAS NO STDERR - it is None, and writing
    to it raises inside the accept loop. That is one of the two ways a proxy
    can stop answering without saying anything, so it is routed through `say`
    like everything else rather than left to chance.
    """
    daemon_threads = True
    allow_reuse_address = True

    def handle_error(self, request, client_address):
        say("request from %s failed: %s"
            % (client_address[0] if client_address else "?",
               traceback.format_exc().strip().splitlines()[-1]))


class Proxy(object):
    def __init__(self, opts):
        self.opts = opts
        self.links = LinkStore(opts.link_file)
        self.cache = PageCache(cap=opts.cache_pages, ttl=opts.cache_ttl)
        self.hist = History()
        self.fetch = Fetcher(opts)
        self.served = 0
        self.bytes_in = 0
        self.bytes_out = 0
        self.started = time.time()

    # --- rendering ---------------------------------------------------------
    def selfbase(self, host_header):
        """What the GUEST should call this proxy - and the PORT is ours, not
        the header's.

        `br_req` sends `Host: ` + `br_host`, and `br_split` has already taken
        the port off into `br_port`, so **the guest's Host header carries no
        port**. Trusting it built form actions like `http://192.168.1.189/li`,
        which is port 80, which is nothing - and the browser reported
        `Connection refused` half a second after Submit, on a proxy that was
        running perfectly on 8088. Every other click worked because links are
        rooted and `br_ubase` puts the page's own port back; a form action is
        the one absolute URL this proxy writes.
        """
        if self.opts.public_base:
            return self.opts.public_base.rstrip("/")
        host = (host_header or "").strip().split(":")[0]
        if not host:
            host = self.opts.advertise or "127.0.0.1"
        port = self.opts.port
        return "http://%s%s" % (host, "" if port == 80 else ":%d" % port)

    def load(self, url, host_header, full=False, reload=False, method="GET",
             data=None):
        """Fetch, transform, cache. Returns the cache record."""
        site = site_for(url, self.opts)
        tok = self.links.path_of(url).rsplit("/", 1)[-1]
        if not reload and method == "GET":
            rec = self.cache.get(tok + ("!full" if full else ""))
            if rec:
                rec["cached"] = True
                note_page(url=url, status=rec.get("status", 200), raw=0,
                          out=sum(len(p) for p in rec.get("pages") or []),
                          pages=rec.get("npages", 1), ms=0, cached=True,
                          site=(site.name if site else "generic"),
                          title=rec.get("title", ""))
                return rec
        target = site.upstream(url, self.opts) if site else url
        t0 = time.time()
        got = self.fetch.get(target, method=method, data=data,
                             extra=(site.headers_for(target, self.opts)
                                    if site else None))
        if site is not None:
            got = site.after_fetch(got, self.fetch, self.opts,
                                   asked=target) or got
        ms = int((time.time() - t0) * 1000)
        self.bytes_in += got.raw_len
        ctype = got.ctype.split(";")[0].strip()
        api = ctype in ("application/json", "text/json") and site is not None \
            and getattr(site, "json_ok", False)
        if not api and ctype not in ("text/html", "application/xhtml+xml",
                                     "text/plain", "text/markdown", ""):
            rec = self.notice(
                "Not a page",
                "%s is %s, %s - this browser shows text and tables only."
                % (short(got.url, 60), ctype or "unknown", human(got.raw_len)),
                url=got.url, tok=tok)
        elif not api and ctype in ("text/plain", "text/markdown"):
            body = "<pre>%s</pre>" % esc(wrap_pre(fold_text(got.text),
                                                  self.opts.cols - 2))
            rec = {"pages": None, "title": short(got.url, 60), "url": got.url,
                   "stats": {}, "blocks": [body]}
            rec = self.repaginate(rec, tok)
        else:
            # A first-class handler sees the page it asked for; the generic
            # pipeline sees whatever came back after redirects (got.url), which
            # is what relative links must resolve against.
            rec = render(got.text, got.url, self.opts, self.links,
                         self.selfbase(host_header), full=full,
                         site=site if site else None)
        save_page(self.opts, got.url, got.text, rec.get("pages"),
                  extra={"status": got.status, "ctype": ctype,
                         "fetch_ms": ms, "site": (site.name if site else ""),
                         "stats": rec.get("stats", {})})
        rec["fetch_ms"] = ms
        rec["raw_len"] = got.raw_len
        rec["status"] = got.status
        rec["cached"] = False
        self.cache.put(tok + ("!full" if full else ""), rec)
        note_page(url=got.url, status=got.status, raw=got.raw_len,
                  out=sum(len(p) for p in rec.get("pages") or []),
                  pages=rec.get("npages", 1), ms=ms, cached=False,
                  site=(site.name if site else "generic"),
                  title=rec.get("title", ""))
        return rec

    def repaginate(self, rec, tok):
        ctx = Ctx(rec["url"], self.links, self.opts)
        pages = paginate(rec["blocks"], self.opts.budget, 420, self.opts.cols,
                         self.opts.maxlines)
        out = []
        for i, blks in enumerate(pages, 1):
            head, foot = chrome(rec["title"], rec["url"], tok, i, len(pages),
                                ctx, self.opts)
            out.append(page_bytes(head, blks, foot))
        rec["pages"], rec["npages"] = out, len(pages)
        return rec

    def notice(self, title, text, url="", tok=None, extra=""):
        ctx = Ctx(url or "", self.links, self.opts)
        head, foot = chrome(title, url, tok, 1, 1, ctx, self.opts)
        body = ["<p>%s</p>" % esc(text)]
        if extra:
            body.append("<p>%s</p>" % esc(extra))
        return {"pages": [page_bytes(head, body, foot)], "npages": 1,
                "title": title, "url": url, "stats": {}, "cached": False}

    def home(self, host_header):
        base = self.selfbase(host_header)
        nav = '<a href="/b">Back</a> | <a href="/about">About</a>'
        # REDDIT FIRST, and through `/rd` rather than through a URL: the route
        # knows which reddit to ask (PROXY-PLAN 16/17) where a bookmark would
        # pin the host it was written with - which is exactly what the old
        # `old.reddit.com/r/vintagecomputing` mark did, and it rendered as a
        # login wall with three links on it.
        marks = ['<li><a href="/rd/">Reddit front page</a></li>',
                 '<li><a href="/rd/r/vintagecomputing/">'
                 'r/vintagecomputing</a></li>']
        for name, url in self.opts.bookmarks:
            marks.append('<li><a href="%s">%s</a></li>'
                         % (self.links.path_of(url), esc(name)))
        # ONE LINK A LINE, each labelled with what it does. It used to be
        # `<li><a href="/rd/">Reddit</a> <a href="/in">sign in</a></li>` - two
        # links on one line, one of them the word `Reddit` - and the field
        # pressed the one that reads like the sign-in and got the front page,
        # which while logged out is a login wall that renders as nothing. The
        # guest's hit-test is not at fault: `br_hitofs` maps the column
        # through `br_lmap`, which the PAINTER fills, so a click lands on the
        # link it is over. The label was the fault.

        stat = ("%d pages served, %s in, %s out, up %s"
                % (self.served, human(self.bytes_in), human(self.bytes_out),
                   human_time(time.time() - self.started)))
        body = HOME_TEXT % {"nav": nav, "base": base,
                            "marks": ("<p>Bookmarks</p>" + "".join(marks))
                            if marks else "", "stat": esc(stat)}
        return {"pages": [("<html><body>%s</body></html>" % body).encode("ascii",
                                                                        "replace")],
                "npages": 1, "title": "os8088 proxy", "url": "", "stats": {},
                "cached": True}


def safe_path(path):
    """A path with anything credential-shaped taken out of it.

    The sign-in that put a password in a request line is gone (PROXY-PLAN
    19.2) and this stays, because the guest can only GET: anything a form on
    a fetched page ever posts arrives here as a query string, and the request
    line is what a server logs. It is applied at every place this file can
    print a path - the access log and the gate's label - and nothing writes
    one to disk.
    """
    out = re.sub(r"([?&](?:p|pw|pass|passwd|password|token|auth|key)=)[^&]*",
                 r"\1<redacted>", path, flags=re.I)
    return out


def short(s, n):
    return s if len(s) <= n else s[:n - 3] + "..."


def human(n):
    return "%dB" % n if n < 1024 else ("%.1fK" % (n / 1024.0) if n < 1048576
                                       else "%.1fM" % (n / 1048576.0))


def human_time(sec):
    sec = int(sec)
    return "%dh%02dm" % (sec // 3600, (sec % 3600) // 60) if sec >= 3600 \
        else "%dm%02ds" % (sec // 60, sec % 60)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    server_version = "os88proxy/" + VERSION
    sys_version = ""
    # `socketserver` puts this on the connection, and without it a guest that
    # opens a socket and then goes quiet holds a thread and a connection for
    # as long as the proxy runs. A 4.77MHz machine with a small socket table
    # is exactly the peer that does that.
    timeout = 30

    # --- plumbing ----------------------------------------------------------
    def log_message(self, fmt, *args):
        if self.server.proxy.opts.quiet:
            return
        line = fmt % args
        if "/li?" in line:
            line = re.sub(r"/li\?\S*", "/li?<redacted>", line)
        say("%s %s" % (self.client_address[0], line))

    def deny(self):
        allow = self.server.proxy.opts.allow
        if not allow:
            return False
        peer = self.client_address[0]
        return not any(peer == a or peer.startswith(a.rstrip("*"))
                       for a in allow)

    def send(self, body, status=200, ctype="text/html; charset=us-ascii"):
        opts = self.server.proxy.opts
        if opts.gate:
            # The gate is not only for --selfcheck. It runs on every real
            # answer because the failures it catches are ones os8088 cannot
            # report: an unknown tag is ignored, a long path fetches something
            # else, a fifth field is simply not in the query. Complaining in
            # the host's log is the only place a complaint can be heard.
            for bad in check_output(body, opts,
                                    "served %s" % safe_path(self.path)[:40]):
                say("GATE: " + bad)
        if len(body) > HARD_BODY:
            # Never let this happen quietly: it is the one failure the guest
            # cannot see. Everything upstream paginates; this is the backstop.
            body = body[:HARD_BODY - 64] + b"<p>[truncated by the proxy]</p>"
            say("WARNING: a page reached the guest's 32KB ceiling")
        # A line per request that is not a page fetch (those have their own
        # event). It is what tells a tester whether the proxy HEARD the click
        # at all - the difference between a guest that never connected and a
        # proxy that answered something unhelpful, which from the front of the
        # machine look identical.
        route = safe_path(self.path).split("?")[0]
        if not route.startswith(("/l/", "/p/", "/x/", "/f/", "/h/", "/u/",
                                 "/g", "/s", "/rd")):
            note_page(kind="route", path=safe_path(self.path)[:60],
                      status=status, out=len(body))
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        try:
            self.wfile.write(body)
        except Exception:
            pass
        self.server.proxy.bytes_out += len(body)

    def page(self, rec, n=1, push=None):
        p = self.server.proxy
        p.served += 1
        if push:
            p.hist.push(push)
        pages = rec.get("pages") or [b"<html><body><p>empty</p></body></html>"]
        n = max(1, min(n, len(pages)))
        st = rec.get("status", 200)
        if not isinstance(st, int) or not 100 <= st <= 599:
            st = 200
        self.send(pages[n - 1], status=st)

    # --- routes ------------------------------------------------------------
    def do_GET(self):
        if self.deny():
            self.send(b"<html><body><p>Not allowed</p></body></html>", 403)
            return
        p = self.server.proxy
        try:
            self.route(p)
        except urllib.error.HTTPError as exc:
            self.page(p.notice("HTTP %s" % exc.code, "%s said %s."
                               % (short(self.path, 40), exc.reason or exc.code)))
        except urllib.error.URLError as exc:
            self.page(p.notice("Cannot reach it", str(exc.reason)))
        except socket.timeout:
            self.page(p.notice("Timed out",
                               "no answer in %ds" % p.opts.timeout))
        except Exception as exc:
            say(traceback.format_exc())
            self.page(p.notice("Proxy error", "%s: %s"
                               % (exc.__class__.__name__, exc)))

    def do_POST(self):
        # Nothing here needs a body, but an unread one leaves the socket in a
        # state a client can wait on.
        try:
            n = int(self.headers.get("Content-Length") or 0)
            if 0 < n < 1 << 20:
                self.rfile.read(n)
        except Exception:
            pass
        self.do_GET()

    def route(self, p, depth=0):
        host_header = self.headers.get("Host", "")
        path = self.path
        # Absolute-form, in case a future guest learns to speak to a real proxy
        if path.lower().startswith("http://") or path.lower().startswith("https://"):
            self.go(p, path, host_header)
            return
        sp = urllib.parse.urlsplit(path)
        route = sp.path
        q = dict(urllib.parse.parse_qsl(sp.query, keep_blank_values=True))

        if route in ("/", "/index.html"):
            self.page(p.home(host_header), push="/")
        elif route == "/about":
            self.page(self.about(p))
        elif route == "/b":
            prev = p.hist.back()
            if prev and prev != "/b" and depth == 0:
                self.path = prev
                self.route(p, depth + 1)
            else:
                self.page(p.home(host_header))
        elif route == "/s":
            # One form, two boxes: an address if there is one, a search
            # otherwise. `http://` is added rather than demanded - it is three
            # of the 95 bytes br_split will carry and nobody types it.
            where = q.get("u", "").strip()
            term = q.get("q", "").strip()
            if where:
                if "://" not in where:
                    where = "http://" + where
                self.go(p, where, host_header)
            elif term:
                self.go(p, p.opts.search % urllib.parse.quote_plus(term),
                        host_header)
            else:
                self.page(p.home(host_header))
        elif route == "/g":
            self.go(p, q.get("u", ""), host_header)
        elif route.startswith("/u/") or route.startswith("/h/"):
            # `/u/host/path` is https and `/h/host/path` is plain http - two
            # routes rather than a scheme in the path, because a location bar
            # on an os8088 machine is typed one key at a time and `://`
            # costs three of the 95 bytes br_split will carry.
            rest = path[3:]
            if "://" not in rest:
                rest = ("http://" if route.startswith("/h/") else "https://") + rest
            self.go(p, rest, host_header)
        elif route.startswith("/rd"):
            # `/rd/`, `/rd/pics` and `/rd/r/pics` all mean the same thing: the
            # bookmark on the home page is written the second way and the
            # first version of this route made it `/r/r/pics/`.
            sub = route[4:].strip("/") if route.startswith("/rd/") else ""
            if sub.lower().startswith("r/"):
                sub = sub[2:]
            base = (RedditSite.OLD if p.opts.reddit_site == "old"
                    else RedditSite.WWW)
            self.go(p, ("%s/r/%s/" % (base, sub)) if sub else base + "/",
                    host_header)
        elif route.startswith("/l/") or route.startswith("/x/"):
            tok = route[3:].strip("/")
            ent = p.links.get(tok)
            if not ent or ent[0] != "u":
                self.page(p.notice("Link expired",
                                   "this proxy has forgotten token %s. Go Home "
                                   "and start again." % tok))
                return
            rec = p.load(ent[1], host_header, full=route.startswith("/x/"),
                         reload=bool(q.get("r")))
            self.page(rec, 1, push=route)
        elif route.startswith("/p/"):
            bits = route[3:].strip("/").split("/")
            tok = bits[0]
            n = int(bits[1]) if len(bits) > 1 and bits[1].isdigit() else 1
            rec = p.cache.get(tok) or p.cache.get(tok + "!full")
            if not rec:
                ent = p.links.get(tok)
                if not ent or ent[0] != "u":
                    self.page(p.notice("Page expired", "re-open it from Home."))
                    return
                rec = p.load(ent[1], host_header)
            self.page(rec, n, push=route)
        elif route.startswith("/f/"):
            self.submit(p, route[3:].strip("/"), sp.query, host_header)
        else:
            self.page(p.notice("No such page", "%s is not a route on this "
                               "proxy." % short(route, 40)))

    def go(self, p, url, host_header):
        url = (url or "").strip()
        if not url:
            self.page(p.home(host_header))
            return
        if "://" not in url:
            url = "https://" + url
        sp = urllib.parse.urlsplit(url)
        if sp.scheme not in ("http", "https") or not sp.netloc:
            self.page(p.notice("Not a URL", short(url, 60)))
            return
        rec = p.load(url, host_header)
        self.page(rec, 1, push=p.links.path_of(url))

    def submit(self, p, tok, query, host_header):
        ent = p.links.get(tok)
        if not ent or ent[0] != "f":
            self.page(p.notice("Form expired", "reload the page it was on."))
            return
        action, method = ent[1], ent[2]
        if method == "post":
            rec = p.load(action, host_header, method="POST", data=query,
                         reload=True)
        else:
            join = "&" if urllib.parse.urlsplit(action).query else "?"
            rec = p.load(action + (join + query if query else ""), host_header,
                         reload=True)
        self.page(rec, 1)

    def about(self, p):
        rows = [("version", VERSION),
                ("python", sys.version.split()[0]),
                ("fold table", "htmsim" if HAVE_HTMSIM else "FALLBACK (crude)"),
                ("pages served", str(p.served)),
                ("bytes fetched", human(p.bytes_in)),
                ("bytes to os8088", human(p.bytes_out)),
                ("links minted", str(len(p.links.map))),
                ("page budget", "%d bytes (guest cap %d)"
                 % (p.opts.budget, HARD_BODY)),
                ("columns", str(p.opts.cols)),
                ("cookies", str(len(p.fetch.jar))),
                ("first-class sites", ", ".join(s.name for s in SITES)),
                ("uptime", human_time(time.time() - p.started))]
        body = ["<table>"]
        for k, v in rows:
            body.append("<tr><th>%s</th><td>%s</td></tr>" % (esc(k), esc(v)))
        body.append("</table>")
        ctx = Ctx("", p.links, p.opts)
        head, foot = chrome("About this proxy", "", None, 1, 1, ctx, p.opts)
        return {"pages": [page_bytes(head, body, foot)], "npages": 1,
                "title": "about", "url": "", "stats": {}, "cached": True}


# =============================================================================
# 12. THE GATE
#
# Every check here is for a failure that is INVISIBLE FROM THE HOST and nearly
# invisible from the front of the machine: an unknown tag is ignored rather
# than reported, an over-long path fetches a different page, a body over 32KB
# is cut mid-tag, a fifth text field simply is not in the query. The proxy is
# the last place any of them can be caught, so it catches them on its own
# output rather than trusting itself.
# =============================================================================
TAG_RE = re.compile(rb"<(/?)([A-Za-z][A-Za-z0-9]*)([^>]*)>")
ATTR_RE = re.compile(r'([A-Za-z-]+)\s*=\s*"([^"]*)"')


def check_output(body, opts, where="page"):
    """A list of complaints, empty when the page is legal for the guest."""
    bad = []
    for i, ch in enumerate(body):
        if ch in (9, 10, 13) or 0x20 <= ch <= 0x7E:
            continue
        bad.append("%s: byte 0x%02X at offset %d is not ASCII the cell font "
                   "has a glyph for" % (where, ch, i))
        break
    if len(body) > HARD_BODY:
        bad.append("%s: %d bytes, and br_take cuts at %d - mid-tag"
                   % (where, len(body), HARD_BODY))
    nlink = nform = nfield = 0
    hidden = 0
    for m in TAG_RE.finditer(body):
        closing, tag, attrs = m.group(1), m.group(2).decode().lower(), \
            m.group(3).decode("latin-1")
        if tag in ("html", "body"):
            continue                  # the guest ignores both, and a desktop
        if tag not in GUEST_TAGS:     # browser wants them while debugging
            bad.append("%s: <%s> is not a tag the browser knows - it is "
                       "IGNORED, so its meaning is lost in silence" % (where, tag))
            continue
        if closing:
            continue
        for name, val in ATTR_RE.findall(attrs):
            name = name.lower()
            if name not in GUEST_ATTRS.get(tag, ()):
                bad.append("%s: <%s %s=> is read by nothing on the guest"
                           % (where, tag, name))
            if name == "href":
                bad.extend(check_href(val, where))
        if tag == "a" and "href=" in attrs:
            nlink += 1
        if tag == "form":
            nform += 1
            am = re.search(r'action="([^"]*)"', attrs)
            act = am.group(1) if am else ""
            if len(act) > MAX_ACTION:
                bad.append("%s: form action is %d bytes and BR_ACTMAX holds %d"
                           % (where, len(act), MAX_ACTION))
            if "://" not in act and not act.startswith("/"):
                bad.append("%s: form action %r is neither absolute nor rooted"
                           % (where, act))
        if tag == "input":
            typ = (dict(ATTR_RE.findall(attrs)).get("type", "text")).lower()
            if typ == "text":
                nfield += 1
            elif typ == "hidden":
                d = dict(ATTR_RE.findall(attrs))
                hidden += len(urllib.parse.quote_plus(d.get("name", ""))) + \
                    len(urllib.parse.quote_plus(d.get("value", ""))) + 2
    hrefs = [v for m in TAG_RE.finditer(body) if m.group(2).lower() == b"a"
             for k, v in ATTR_RE.findall(m.group(3).decode("latin-1"))
             if k.lower() == "href"]
    arena = sum(len(h) + 1 for h in hrefs)
    if arena > MAX_HREF_BYTES:
        bad.append("%s: %d bytes of hrefs and the link arena is %d - the ones "
                   "past it are drawn and not clickable"
                   % (where, arena, MAX_HREF_BYTES))
    if nlink > MAX_LINKS:
        bad.append("%s: %d anchors, and the guest records BR_LNKMAX=%d - the "
                   "rest are drawn and not clickable" % (where, nlink, MAX_LINKS))
    if nform > MAX_FORMS:
        bad.append("%s: %d forms; the guest keeps ONE field table for the page"
                   % (where, nform))
    if nfield > MAX_FIELDS:
        bad.append("%s: %d text inputs, and BR_FMAX is %d" % (where, nfield,
                                                              MAX_FIELDS))
    if hidden > MAX_HIDDEN:
        bad.append("%s: %d bytes of hidden pairs, and br_hid holds %d"
                   % (where, hidden, MAX_HIDDEN))
    if HAVE_HTMSIM:
        try:
            ops = htmsim.simplify(htmsim.parse(body)[0])[0]
            lines = htmsim.layout(ops, opts.cols)
            if len(lines) > MAX_LINES:
                bad.append("%s: %d display lines and the line table holds %d"
                           % (where, len(lines), MAX_LINES))
        except Exception as exc:
            bad.append("%s: htmsim could not lay it out: %s" % (where, exc))
    return bad


def check_href(val, where):
    bad = []
    if val.lower().startswith("https://"):
        bad.append("%s: href %r is https, which br_split refuses BY NAME"
                   % (where, short(val, 40)))
        return bad
    if val.lower().startswith("http://"):
        sp = urllib.parse.urlsplit(val)
        if len(val) > MAX_URL:
            bad.append("%s: href is %d bytes and br_ubuf holds %d"
                       % (where, len(val), MAX_URL))
        if len(sp.netloc) > MAX_HOST:
            bad.append("%s: host is %d bytes and BR_HOSTMAX holds %d"
                       % (where, len(sp.netloc), MAX_HOST))
        path = sp.path + (("?" + sp.query) if sp.query else "")
        if len(path) > MAX_PATH:
            bad.append("%s: path is %d bytes and BR_PATHMAX holds %d - it is "
                       "TRUNCATED and fetches something else"
                       % (where, len(path), MAX_PATH))
    elif val.startswith("/"):
        if len(val) > MAX_PATH:
            bad.append("%s: path %r is %d bytes and BR_PATHMAX holds %d"
                       % (where, short(val, 30), len(val), MAX_PATH))
    return bad


def verify_guest_limits(root):
    """Re-read apps/browser and refuse to agree with ourselves if a constant
    has moved. This is the whole reason the numbers above are safe to copy:
    the guest is free to change and this file finds out on the next run rather
    than on the next page that renders wrong."""
    src = ""
    for name in ("apps/browser/browser.asm", "apps/browser/brnet.inc"):
        try:
            with open(os.path.join(root, name), "r", errors="replace") as fh:
                src += fh.read()
        except IOError:
            return ["cannot read %s - run --selfcheck from the repo" % name]
    bad = []
    for key, want in sorted(LIM.items()):
        m = re.search(r"^%s\s+equ\s+(\d+)" % key, src, re.M)
        if not m:
            bad.append("%s is no longer defined in apps/browser" % key)
        elif int(m.group(1)) != want:
            bad.append("%s is %s in apps/browser and %d here - the proxy's "
                       "output is now sized against a browser that does not "
                       "exist" % (key, m.group(1), want))
    for tag in sorted(GUEST_TAGS - set("h%d" % n for n in range(1, 7))):
        if ("db '%s',0," % tag) not in src:
            bad.append("<%s> is in GUEST_TAGS and not in br_tagtab" % tag)
    return bad


# =============================================================================
# 13. THE COMMAND LINE
# =============================================================================
_SAY_LOCK = threading.Lock()


def say(msg):
    if LOG_SINK is not None:
        try:
            LOG_SINK(msg)
        except Exception:
            pass
    with _SAY_LOCK:
        try:
            sys.stderr.write("%s %s\n" % (time.strftime("%H:%M:%S"), msg))
            sys.stderr.flush()
        except Exception:
            # A windowed .exe has no stderr at all - PyInstaller's --noconsole
            # leaves it None - and a log line must never be what takes the
            # proxy down.
            pass


DEFAULT_MARKS = [
    ("FrogFind (search for old machines)", "http://frogfind.com/"),
    ("Wikipedia", "https://en.wikipedia.org/wiki/Main_Page"),
    ("Hacker News", "https://news.ycombinator.com/"),
    ("os8088", "https://os8088.com/"),
]


def default_link_file():
    # %APPDATA% on Windows, a dotfile everywhere else - the tester's machine is
    # a Windows one and a stray `.os8088proxy` in their user folder is litter.
    base = os.environ.get("APPDATA") if os.name == "nt" else None
    home = base or os.path.expanduser("~")
    d = os.path.join(home, "os8088proxy" if base else ".os8088proxy")
    try:
        os.makedirs(d, exist_ok=True)
        return os.path.join(d, "links.json")
    except Exception:
        return None


def read_bookmarks(path):
    marks = []
    with open(path, "r", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.rsplit(None, 1) if " " in line else [line, line]
            if len(parts) == 2 and "://" in parts[1]:
                marks.append((parts[0], parts[1]))
            elif "://" in line:
                marks.append((line, line))
    return marks


def parse_args(argv):
    ap = argparse.ArgumentParser(
        description="A reformatting HTTP proxy for the os8088 browser.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    g = ap.add_argument_group("serving")
    g.add_argument("--port", type=int, default=8088)
    g.add_argument("--bind", default="0.0.0.0",
                   help="127.0.0.1 to refuse everything but this machine")
    g.add_argument("--advertise", default="",
                   help="the address to print at startup and to build form "
                        "actions from when a request carries no Host:")
    g.add_argument("--public-base", default="",
                   help="force the base a form action is built on, e.g. "
                        "http://192.168.1.50:8088 - it must be under %d bytes "
                        "with the token on the end" % MAX_ACTION)
    g.add_argument("--allow", action="append", default=[],
                   help="client addresses allowed to use this proxy (repeatable; "
                        "a trailing * matches a prefix). Default: anybody who "
                        "can reach the port")
    g.add_argument("--quiet", action="store_true")
    g.add_argument("--no-gate", dest="gate", action="store_false", default=True,
                   help="stop checking every answer against the guest's limits")

    g = ap.add_argument_group("the page os8088 gets")
    g.add_argument("--cols", type=int, default=79,
                   help="the guest's text columns: CGA 79, Hercules 89, VGA 79")
    g.add_argument("--budget", type=int, default=DEFAULT_BUDGET,
                   help="bytes per page before it is split (guest cap %d)"
                        % HARD_BODY)
    g.add_argument("--tables", choices=("auto", "off"), default="auto",
                   help="auto: layout tables unwrapped, CSS rows turned into "
                        "real tables, <dl> into two columns")
    g.add_argument("--maxlines", type=int, default=900,
                   help="display lines per page; the guest's line table holds "
                        "%d and cannot show a line past it" % MAX_LINES)
    g.add_argument("--maxcols", type=int, default=6,
                   help="a table wider than this is turned into one record "
                        "per paragraph")
    g.add_argument("--cellmax", type=int, default=200,
                   help="longest cell a CSS row may hold and still be a table")
    g.add_argument("--strip", choices=("full", "light"), default="full",
                   help="full also drops elements whose class says nav, "
                        "sidebar, cookie banner, share buttons...")
    g.add_argument("--images", choices=("mark", "drop"), default="mark",
                   help="mark shows [alt text]; the guest cannot draw a picture")
    g.add_argument("--emphasis", choices=("off", "stars"), default="off",
                   help="stars wraps <b>/<strong> in asterisks")
    g.add_argument("--form-action", choices=("auto", "absolute", "relative"),
                   default="auto",
                   help="auto: an absolute action while it fits BR_ACTMAX (63 "
                        "bytes), relative after that - which needs a browser "
                        "whose br_submit resolves the action")
    g.add_argument("--forms", choices=("get", "all"), default="get",
                   help="all lets the proxy turn a POST form into a GET and "
                        "issue the POST itself")
    g.add_argument("--sites", choices=("on", "off"), default="on",
                   help="off disables every first-class site handler")
    g.add_argument("--reddit-ua", default="",
                   help="the User-Agent the JSON endpoints are asked with. "
                        "Blank is os88proxy's own, which is what reddit's API "
                        "rules ask for; a browser's is a good way to be sent "
                        "the JavaScript app instead of the data")
    g.add_argument("--reddit-api", choices=("json", "html"), default="json",
                   help="json asks reddit's own public endpoint, which needs "
                        "no account and is a tenth the bytes; html reads the "
                        "page, which reddit guards with a javascript check")
    g.add_argument("--reddit-site", choices=("www", "old"), default="www",
                   help="which reddit to ask. www needs no sign-in and is "
                        "what the parser is built against; old is smaller on "
                        "the wire and wants a session for most of it")
    g.add_argument("--reddit-thread",
                   choices=("quote", "bar", "rule", "table"), default="quote",
                   help="how a reply tree is drawn: quote is `>` a level, bar "
                        "is `|`, rule puts a line between top-level threads, "
                        "table gives the guest a real two-column table")
    g.add_argument("--reddit-indent", type=int, default=1,
                   help="characters of indent per comment level. The guest "
                        "cannot indent, so it is '>' characters; 0 is flat")
    g.add_argument("--reddit-maxindent", type=int, default=8,
                   help="...and the most a deep reply may be pushed in")
    g.add_argument("--reddit-maxcomments", type=int, default=200,
                   help="comments rendered from one thread before it is cut")
    g.add_argument("--verbose-page", action="store_true",
                   help="put the transform counters in the page footer")

    g = ap.add_argument_group("fetching")
    g.add_argument("--timeout", type=int, default=20)
    g.add_argument("--max-download", type=int, default=3 * 1024 * 1024)
    g.add_argument("--user-agent", default=UA_DESKTOP)
    g.add_argument("--cookies", default="",
                   help="a Netscape cookies.txt exported from a desktop "
                        "browser: this is how a signed-in site works, and the "
                        "os8088 never holds the credential")
    g.add_argument("--insecure", action="store_true",
                   help="do not verify TLS certificates (a LAN box with a "
                        "self-signed one)")
    g.add_argument("--search", default="https://html.duckduckgo.com/html/?q=%s",
                   help="the search URL, with %%s where the query goes")
    g.add_argument("--bookmarks", default="",
                   help="a file of 'Name  URL' lines for the home page")
    g.add_argument("--link-file", default=default_link_file(),
                   help="where minted link tokens are remembered")
    g.add_argument("--save-dir", default="",
                   help="write every page to this folder: what the server "
                        "sent (.src.html), what os8088 was handed "
                        "(.outN.html) and the sizes (.json)")
    g.add_argument("--cache-pages", type=int, default=64)
    g.add_argument("--cache-ttl", type=int, default=900)

    g = ap.add_argument_group("without serving anything")
    g.add_argument("--selfcheck", action="store_true",
                   help="the gate: the guest's constants, the fixtures, and "
                        "every page checked against what the browser can read")
    g.add_argument("--render", metavar="URL",
                   help="fetch and print what os8088 would receive")
    g.add_argument("--render-file", metavar="FILE",
                   help="...from a file instead, so it needs no network")
    g.add_argument("--page", type=int, default=1,
                   help="with --render: which page goes to stdout")
    g.add_argument("--cost", action="store_true",
                   help="with --render: what the page costs to DRAW, through "
                        "tools/htmsim.py's measured constants")
    g.add_argument("--base", default="http://example.invalid/",
                   help="with --render-file: the URL the file stands for")
    opts = ap.parse_args(argv)
    opts.bookmarks = (read_bookmarks(opts.bookmarks) if opts.bookmarks
                      else list(DEFAULT_MARKS))
    if "%s" not in opts.search:
        opts.search += "%s"
    opts.budget = max(2048, min(opts.budget, HARD_BODY - 512))
    return opts


# =============================================================================
# 14. MODES
# =============================================================================
def mode_render(opts):
    try:
        return _mode_render(opts)
    except (urllib.error.URLError, socket.timeout, OSError) as exc:
        say("cannot fetch %s: %s" % (opts.render or opts.render_file, exc))
        return 2


def _mode_render(opts):
    links = LinkStore(None)
    if opts.render_file:
        with open(opts.render_file, "rb") as fh:
            raw = fh.read()
        text = decode_body(raw, "")
        url = opts.base
        raw_len = len(raw)
    else:
        pre = site_for(opts.render, opts)
        got = Fetcher(opts).get(pre.upstream(opts.render, opts)
                                if pre else opts.render)
        text, url, raw_len = got.text, got.url, got.raw_len
    site = site_for(url, opts)
    rec = render(text, url, opts, links, "http://os8088:%d" % opts.port,
                 site=site)
    total = sum(len(p) for p in rec["pages"])
    bad = []
    want = max(1, min(opts.page, len(rec["pages"])))
    for i, page in enumerate(rec["pages"], 1):
        if i == want:
            # ONE page on stdout. Printing them all concatenated makes a file
            # nothing will ever fetch, and measuring THAT is how a 552-line
            # page gets reported as 1,136.
            sys.stdout.write(page.decode("ascii", "replace"))
            sys.stdout.write("\n")
        bad.extend(check_output(page, opts, "page %d" % i))
    say("%s -> %s in %d page(s)  [%s]"
        % (human(raw_len), human(total), len(rec["pages"]),
           ", ".join("%s=%s" % kv for kv in sorted(rec["stats"].items()))))
    if opts.cost and HAVE_HTMSIM:
        for i, page in enumerate(rec["pages"], 1):
            ops = htmsim.simplify(htmsim.parse(page)[0])[0]
            lines = htmsim.layout(ops, opts.cols)
            ad = htmsim.ADAPTERS["cga"]
            ms = sum(htmsim.cost_line(ln, ad["cell"], opts.cols)
                     for ln in lines) / 1000.0
            say("page %d: %d lines, ~%.0f ms to draw on %s (fetch %s at "
                "3,741 B/s = %.1f s)"
                % (i, len(lines), ms, ad["name"], human(len(page)),
                   len(page) / 3741.0))
    for b in bad:
        say("GATE: " + b)
    return 1 if bad else 0


def thread_marks(opts):
    """(the tagline pattern, the mark an indented reply carries) for whichever
    thread style is armed. The shape assertions below are about the LAYOUT, so
    they have to know which layout was asked for - written against `quote`
    alone they fail a perfectly good `table` render, which is a gate that can
    only be believed on one setting."""
    style = getattr(opts, "reddit_thread", "quote")
    head = (rb"<(?:p|td)>(?:&gt;|\|)*\s*([\w-]+) (-?\d+) points? "
            rb"(\d+ \w+ ago)(?:<br>| - |</td>)")
    mark = {"bar": b"<p>|", "table": b"<td>&gt;"}.get(style, b"<p>&gt; ")
    return head, mark


def reddit_shape(name, pages, opts):
    """The first-class layout, asserted rather than looked at.

    A gate that only checks legality passes on a page that is legal and
    wrong - this tree's own lesson about two identical nothings agreeing
    perfectly - so the SHAPE the owner asked for is checked here: a post is a
    title line and a `N comments  r/group` line and nothing else, and a
    thread is a rule and then `author points ago` with its body under it.
    """
    body = b"".join(pages)
    bad = []
    if "front" in name:
        posts = re.findall(rb"<p><a[^>]*>[^<]*</a><br>(.*?)</p>", body)
        if len(posts) < 15:
            bad.append("%s: %d posts laid out; the capture has 25"
                       % (name, len(posts)))
        for second in posts[:40]:
            if b"comment" not in second:
                bad.append("%s: a post's second line has no comment count: %r"
                           % (name, second[:60]))
                break
            if b"r/" not in second:
                bad.append("%s: a post's second line has no group: %r"
                           % (name, second[:60]))
                break
        if b"<table" in body:
            bad.append("%s: a table on a listing - it is two lines a post now"
                       % name)
        for want in (b">Popular</a>", b">All</a>", b">hot</a>"):
            if want not in body:
                bad.append("%s: no %s in the categories row"
                           % (name, want.decode()))
        if re.search(rb"submitted|points</p>|\bago\b", body):
            bad.append("%s: the listing carries tagline fluff" % name)
    if "json-listing" in name:
        # The same assertions as the HTML listing, off the OTHER rung: what
        # is being checked is that the reader cannot tell which one answered.
        posts = re.findall(rb"<p><a[^>]*>[^<]*</a><br>(.*?)</p>", body)
        if len(posts) < 20:
            bad.append("%s: %d posts laid out; the capture has 28"
                       % (name, len(posts)))
        for second in posts[:40]:
            if b"comment" not in second or b"r/" not in second:
                bad.append("%s: a post's second line is not `N comments  "
                           "r/group`: %r" % (name, second[:70]))
                break
        for want in (b">Popular</a>", b">More posts</a>"):
            if want not in body:
                bad.append("%s: no %s" % (name, want.decode()))
    if "json-comments" in name or "json-more" in name:
        if b"<hr>" not in body:
            bad.append("%s: no rule between the post and the comments" % name)
        hre, mark = thread_marks(opts)
        heads = re.findall(hre, body)
        if len(heads) < 20:
            bad.append("%s: %d comment taglines; the fixture has 25"
                       % (name, len(heads)))
        if opts.reddit_indent and mark not in body:
            bad.append("%s: the reply tree is flat" % name)
        if "json-more" in name and b"more replies]" not in body:
            # ONLY that fixture: a cut chain needs a thread big enough to have
            # one and neither field capture is, which is why it still exists.
            bad.append("%s: a `more` node did not become a link" % name)
    if "www-template" in name:
        # THE WHOLE FIXTURE IS INSIDE A <template>, which is where modern
        # reddit renders its feed: with the parser skipping templates this
        # page has no posts in it at all and the reader gets a notice.
        posts = re.findall(rb"<p><a[^>]*>[^<]*</a><br>(.*?)</p>", body)
        if len(posts) < 2:
            bad.append("%s: %d posts - the feed is inside a <template> and "
                       "the parser is not reading it" % (name, len(posts)))
        if b">More posts</a>" not in body:
            bad.append("%s: no More posts link" % name)
        if b"fetched by script" in body:
            bad.append("%s: rendered as the no-posts notice" % name)
    if "www-listing" in name:
        # THE SAME SHAPE, off markup that has nothing in common with the old
        # site's. A listing is two lines a post - title, then `N comments`
        # and the group - and this asserts it against a REAL www.reddit.com
        # capture, so a shreddit redesign fails here rather than in the field.
        posts = re.findall(rb"<p><a[^>]*>[^<]*</a><br>(.*?)</p>", body)
        if len(posts) < 20:
            bad.append("%s: %d posts laid out; the capture has 28"
                       % (name, len(posts)))
        for second in posts[:40]:
            if b"comment" not in second or b"r/" not in second:
                bad.append("%s: a post's second line is not `N comments  "
                           "r/group`: %r" % (name, second[:70]))
                break
        for want in (b">Popular</a>", b">All</a>", b">hot</a>",
                     b">More posts</a>"):
            if want not in body:
                bad.append("%s: no %s" % (name, want.decode()))
        if b"<table" in body:
            bad.append("%s: a table on a listing" % name)
        if re.search(rb"\bJoin\b|Log In|Create Post|shreddit", body):
            bad.append("%s: the site chrome survived" % name)
    if "www-comments" in name:
        if b"<hr>" not in body:
            bad.append("%s: no rule between the post and the comments" % name)
        # `4 hours ago` is computed from the capture's timestamps, so it grows
        # with the age of the fixture - the unit is not pinned, the SHAPE is.
        hre, mark = thread_marks(opts)
        heads = re.findall(hre, body)
        if len(heads) < 20:
            bad.append("%s: %d comment taglines; the capture has 25"
                       % (name, len(heads)))
        if opts.reddit_indent and mark not in body:
            bad.append("%s: nothing is indented" % name)
        if b"[more replies]" not in body:
            bad.append("%s: the collapsed reply chains are not linked" % name)
        if not re.search(rb"\[\d+ of \d+ comments on this page\]", body):
            bad.append("%s: the page does not say how much of the thread it "
                       "is showing" % name)
        if b"r/mildlyinfuriating" not in body:
            bad.append("%s: the group is not named at the top" % name)
    if "wikipedia" in name:
        # THE REGRESSION THIS FIXTURE EXISTS FOR: en.wikipedia.org puts
        # `vector-feature-language-in-main-menu-disabled` on the <html> tag,
        # JUNK_CLASS found `-menu-` in it, and the WHOLE DOCUMENT went. It
        # rendered as a title and `(empty page)` - 173 bytes off a 470KB page,
        # which is exactly what a site being down looks like. The floor below
        # is what makes this a check that can fail: the broken build produces
        # 362 bytes with the safety net off, and cannot reach 6,000.
        if len(body) < 6000:
            bad.append("%s: %d bytes out of a 470KB page - the document was "
                       "dropped again" % (name, len(body)))
        if b"(empty page)" in body:
            bad.append("%s: rendered as (empty page)" % name)
        # The phrase is checked in the PIECES the page actually has: the
        # sentence carries two links inside it, so `free encyclopedia that
        # anyone can edit` is never contiguous in the output. An assertion
        # that reads a value the renderer cannot produce fails a good build.
        for want in (b"encyclopedia that", b"From today"):
            if want not in body:
                bad.append("%s: %r is not in it - the article did not render"
                           % (name, want.decode()))
    if "thread" in name:
        if b"<hr>" not in body:
            bad.append("%s: no rule between the post and the comments" % name)
        hre, mark = thread_marks(opts)
        heads = re.findall(hre, body)
        if len(heads) < 40:
            bad.append("%s: %d comment taglines; the capture has 191 comments"
                       % (name, len(heads)))
        if opts.reddit_indent and mark not in body:
            bad.append("%s: nothing is indented two levels deep" % name)
        if b"permalink" in body or b"save parent report" in body:
            bad.append("%s: the comment button row survived" % name)
    return bad


def check_challenge(root):
    """Answer reddit's JavaScript check, off the page the field was given.

    The fixture is the real thing, and the assertion is the whole mechanism:
    the transform is read out of the script, the answer is computed, and the
    request that goes back carries the hidden fields AND the query the page
    was asked with - `?limit=50` is the difference between the answer and the
    front page. A stub fetcher stands in for reddit, so this proves the shape
    of the reply and never that reddit accepts it.
    """
    path = os.path.join(root, "tests", "proxy", "reddit-jscheck.htm")
    if not os.path.exists(path):
        return ["tests/proxy/reddit-jscheck.htm is missing"]
    with open(path, "rb") as fh:
        text = decode_body(fh.read(), "")
    site = RedditSite()
    bad = []
    sol = site._jsc_solve(text)
    want = "b23a5eb64d4b8037" * 2
    if sol != want:
        bad.append("the js check was answered %r, not %r" % (sol, want))

    class Stub(object):
        seen = []

        def get(self, url, **kw):
            Stub.seen.append(url)
            return Fetched(url, 200, "application/json", '{"kind":"Listing"}', 20)

    asked = "https://www.reddit.com/r/x/.json?limit=50&raw_json=1"
    got = Fetched(asked, 200, "text/html", text, len(text))
    out = site.after_fetch(got, Stub())
    if not Stub.seen:
        bad.append("the js check was not answered at all")
    else:
        q = dict(urllib.parse.parse_qsl(
            urllib.parse.urlsplit(Stub.seen[0]).query))
        for k in ("solution", "js_challenge", "token", "limit", "raw_json"):
            if k not in q:
                bad.append("the answer does not carry %s: %r" % (k, q))
        if q.get("solution") != want:
            bad.append("the answer carries the wrong solution")
        # This capture's form posts to `/`, so the page we actually wanted has
        # to be asked for again once the cookie is in the jar - THAT is the
        # request whose absence would leave a reader looking at the front page
        # after clicking a subreddit.
        if len(Stub.seen) != 2 or Stub.seen[1] != asked:
            bad.append("the page we asked for was not re-fetched after the "
                       "check: %r" % (Stub.seen,))
    if out is None or "Listing" not in (out.text if out else ""):
        bad.append("what came back from the check was not returned")
    # ...and the one that matters as much: an unreadable transform REFUSES.
    if site._jsc_eval("crypto.subtle.digest(e)", "e", "x") is not None:
        bad.append("an unreadable transform was answered rather than refused")

    # THE FIELD'S OWN SEQUENCE, replayed. Asking for `/.json` got a REDIRECT
    # to `/` and then the challenge, so the `Fetched` came back naming `/` -
    # and every path test in the first version read that instead of what was
    # asked for. The answer landed on the HTML front page, which for a
    # signed-out reader carries no posts at all, and the reader got the shell.
    class Field(object):
        seen = []

        def get(self, url, **kw):
            Field.seen.append(url)
            if "solution=" in url:              # the check, answered
                return Fetched("https://www.reddit.com/", 200, "text/html",
                               "<html><body>the front page shell</body></html>",
                               60)
            if url.endswith("/hot.json?raw_json=1&limit=50"):
                return Fetched(url, 200, "application/json",
                               '{"kind":"Listing","data":{"children":['
                               '{"kind":"t3","data":{"title":"x"}}]}}', 80)
            if url == asked2 and len(Field.seen) > 1:
                # ...and once the cookie is in the jar the check is not
                # repeated - what comes back is the signed-out HTML front
                # page, which has no posts in it at all.
                return Fetched("https://www.reddit.com/", 200, "text/html",
                               "<html><body>the front page shell</body></html>",
                               60)
            # `/.json` REDIRECTS to `/` and answers the challenge there
            return Fetched("https://www.reddit.com/", 200, "text/html",
                           text, len(text))

    asked2 = "https://www.reddit.com/.json?raw_json=1&limit=50"
    out2 = site.after_fetch(
        Fetched("https://www.reddit.com/", 200, "text/html", text, len(text)),
        Field(), None, asked2)
    if len(Field.seen) < 2 or "solution=" not in Field.seen[0]:
        bad.append("the check was not answered first: %r" % (Field.seen,))
    elif Field.seen[1] != asked2:
        bad.append("the URL we ASKED for was not re-fetched after the check "
                   "- it went to %r" % (Field.seen[1],))
    elif not any(u.startswith("https://www.reddit.com/hot.json")
                 for u in Field.seen):
        bad.append("a front page that answers HTML did not fall back to "
                   "/hot.json: %r" % (Field.seen,))
    elif out2 is None or "t3" not in (out2.text or ""):
        bad.append("the listing that ended the sequence was not returned")
    return bad


def check_json(root):
    """The FIELDS rung 0 reads, asserted on the real capture.

    `tests/proxy/reddit-json-listing.json` is a signed-out save of
    `www.reddit.com/r/vintagecomputing/.json`, so this is the one place in
    this tree where reddit's own schema is checked rather than assumed. A
    field that goes away here is a rename upstream, and it would otherwise
    show as a listing that renders with no comment counts and no groups -
    legal, plausible, and wrong.
    """
    path = os.path.join(root, "tests", "proxy", "reddit-json-listing.json")
    if not os.path.exists(path):
        return ["tests/proxy/reddit-json-listing.json is missing"]
    try:
        doc = json.load(open(path, encoding="utf-8"))
    except ValueError as exc:
        return ["the JSON fixture does not parse: %s" % exc]
    bad = []
    if doc.get("kind") != "Listing":
        bad.append("the fixture is not a Listing")
    data = doc.get("data") or {}
    kids = [c for c in (data.get("children") or []) if c.get("kind") == "t3"]
    if len(kids) < 10:
        bad.append("%d posts in the fixture" % len(kids))
    if not data.get("after"):
        bad.append("no `after` cursor - More posts would never appear")
    for f in ("title", "permalink", "num_comments", "subreddit_name_prefixed",
              "score", "author", "created_utc", "url", "is_self"):
        missing = [i for i, c in enumerate(kids) if f not in (c.get("data") or {})]
        if missing:
            bad.append("`%s` is not on %d of %d posts - the schema moved"
                       % (f, len(missing), len(kids)))
    if data.get("modhash"):
        bad.append("the fixture carries a live modhash - re-run scrub.py")
    # ...and the THREAD, which is the other half of the schema and the one
    # with a shape to it: `[post-listing, comment-listing]`, and a comment's
    # `replies` is a whole Listing again rather than an array.
    cpath = os.path.join(root, "tests", "proxy", "reddit-json-comments.json")
    if not os.path.exists(cpath):
        return bad + ["tests/proxy/reddit-json-comments.json is missing"]
    try:
        doc = json.load(open(cpath, encoding="utf-8"))
    except ValueError as exc:
        return bad + ["the comments fixture does not parse: %s" % exc]
    if not isinstance(doc, list) or len(doc) < 2:
        bad.append("a thread is [post-listing, comment-listing]; this is %s"
                   % type(doc).__name__)
        return bad
    tops = [c for c in (doc[1]["data"].get("children") or [])
            if c.get("kind") == "t1"]
    if not tops:
        bad.append("no t1 comments in the thread fixture")
    nested = 0
    for c in tops:
        for f in ("author", "score", "created_utc", "body_html", "permalink"):
            if f not in (c.get("data") or {}):
                bad.append("`%s` is not on a comment - the schema moved" % f)
                break
        reps = (c.get("data") or {}).get("replies")
        if isinstance(reps, dict):
            nested += 1
            if reps.get("kind") != "Listing":
                bad.append("`replies` is not a Listing")
    if not nested:
        bad.append("no comment in the fixture has replies - the nesting is "
                   "what the walk is written against")

    # ...and the FRONT PAGE, which the field found: `/.json` answers a
    # Listing in a browser and can answer an empty one to somebody else, so
    # an empty one has to become a request for `/hot/`.
    class Stub(object):
        seen = []

        def get(self, url, **kw):
            Stub.seen.append(url)
            return Fetched(url, 200, "application/json",
                           '{"kind":"Listing","data":{"children":[]}}', 40)

    empty = '{"kind": "Listing", "data": {"after": null, "children": []}}'
    site = RedditSite()
    site.after_fetch(Fetched("https://www.reddit.com/.json?limit=50", 200,
                             "application/json", empty, len(empty)), Stub())
    if not Stub.seen or "/hot.json" not in Stub.seen[0]:
        bad.append("an empty front page did not fall back to /hot.json: %r"
                   % (Stub.seen,))
    elif "limit=50" not in Stub.seen[0]:
        bad.append("the /hot/ fallback dropped the query: %r" % Stub.seen[0])
    # ...and the escaping, which is the one thing about these fields that is
    # not obvious: `selftext_html` arrives ESCAPED unless `raw_json=1` was
    # asked for, and the field's own save is the proof - both forms have to
    # come out as the same paragraph.
    site = RedditSite()
    opts = parse_args([])
    em = Emitter(Ctx("https://www.reddit.com/", LinkStore(None), opts))
    esc_form = site._j_html("&lt;div class=\"md\"&gt;&lt;p&gt;hi&lt;/p&gt;"
                            "&lt;/div&gt;", em)
    raw_form = site._j_html('<div class="md"><p>hi</p></div>', em)
    if esc_form != raw_form or "hi" not in "".join(raw_form):
        bad.append("an escaped body_html does not render as the plain one: "
                   "%r against %r" % (esc_form, raw_form))
    return bad


def check_styles(root):
    """Every thread style, on one thread, whichever one is armed.

    A style is a knob the field turns, so all four have to be checked on a
    run that turns none of them - and the table one has a failure mode the
    others cannot have: `paginate` splits between blocks, so a <table> whose
    </table> is in the next block renders the rest of the page inside a box.
    """
    path = os.path.join(root, "tests", "proxy", "reddit-json-comments.json")
    if not os.path.exists(path):
        return []
    text = open(path, encoding="utf-8").read()
    url = ("https://www.reddit.com/r/vintagecomputing/comments/1vmmr2k/x/"
           ".json?raw_json=1&limit=100")
    bad = []
    for style in ("quote", "bar", "rule", "table"):
        opts = parse_args(["--reddit-thread", style])
        rec = render(text, url, opts, LinkStore(None), "http://h:8088",
                     site=site_for(url, opts))
        body = b"".join(rec["pages"])
        bad.extend("%s [%s]" % (b, style)
                   for b in reddit_shape("reddit-json-comments.json",
                                         rec["pages"], opts))
        bad.extend("%s [%s]" % (b, style) for b in check_output(body, opts))
        opened = body.count(b"<table>")
        closed = body.count(b"</table>")
        if opened != closed:
            bad.append("%s: %d <table> and %d </table> - a table split across "
                       "blocks boxes the rest of the page" % (style, opened,
                                                              closed))
        if style == "table" and not opened:
            bad.append("the table style emitted no table")
        if style != "table" and opened:
            bad.append("%s emitted a table" % style)
    return bad


def fixtures(root):
    d = os.path.join(root, "tests", "proxy")
    return sorted(os.path.join(d, f) for f in os.listdir(d)
                  if f.endswith(".htm") or f.endswith(".json")) \
        if os.path.isdir(d) else []


def mode_selfcheck(opts):
    root = os.path.dirname(_HERE)
    fails = 0
    say("os88proxy %s, python %s, fold table: %s"
        % (VERSION, sys.version.split()[0],
           "tools/htmsim.py" if HAVE_HTMSIM else "FALLBACK - fix this"))
    if not HAVE_HTMSIM:
        say("FAIL: tools/htmsim.py did not import, so the fold and the cost "
            "model are not the ones the browser was built from")
        fails += 1
    for bad in verify_guest_limits(root):
        say("FAIL: " + bad)
        fails += 1
    for bad in check_challenge(root):
        say("FAIL: " + bad)
        fails += 1
    for bad in check_json(root):
        say("FAIL: " + bad)
        fails += 1
    for bad in check_styles(root):
        say("FAIL: " + bad)
        fails += 1
    files = fixtures(root) + sorted(
        os.path.join(root, "tests", "htm", f)
        for f in (os.listdir(os.path.join(root, "tests", "htm"))
                  if os.path.isdir(os.path.join(root, "tests", "htm")) else [])
        if f.endswith(".htm"))
    if not files:
        say("FAIL: no fixtures found under tests/proxy or tests/htm")
        fails += 1
    for path in files:
        with open(path, "rb") as fh:
            raw = fh.read()
        text = decode_body(raw, "")
        name = os.path.basename(path)
        # The URL a fixture is rendered AS matters: `_base` builds every link
        # in the page off it, and the modern handler asks the path whether it
        # is looking at a thread. A www capture rendered as old.reddit would
        # pass every assertion below while emitting links to the host it did
        # not come from.
        if "json-comments" in name or "json-more" in name:
            url = ("https://www.reddit.com/r/mildlyinfuriating/comments/"
                   "1vrqi5a/x/.json?raw_json=1&limit=100")
        elif "json-listing" in name:
            url = "https://www.reddit.com/r/vintagecomputing/.json?raw_json=1"
        elif "www-comments" in name:
            url = "https://www.reddit.com/r/vintagecomputing/comments/a1/t/"
        elif "www-template" in name:
            url = "https://www.reddit.com/hot/"
        elif "www" in name:
            url = "https://www.reddit.com/r/vintagecomputing/"
        elif "reddit" in name:
            url = "http://old.reddit.com/r/x/"
        else:
            url = "http://example.invalid/dir/page.html"
        links = LinkStore(None)
        site = site_for(url, opts)
        rec = render(text, url, opts, links, "http://192.168.1.50:8088",
                     site=site)
        total = sum(len(p) for p in rec["pages"])
        bad = []
        for i, page in enumerate(rec["pages"], 1):
            bad.extend(check_output(page, opts, "%s p%d"
                                    % (os.path.basename(path), i)))
        bad.extend(reddit_shape(os.path.basename(path), rec["pages"], opts))
        if rec["stats"].get("main") == "reddit" and "reddit" not in path:
            bad.append("%s: the reddit handler claimed a page that is not one"
                       % path)
        for b in bad:
            say("FAIL: " + b)
        fails += len(bad)
        say("%-28s %6s -> %6s  %d page(s)  %s"
            % (os.path.basename(path), human(len(raw)), human(total),
               len(rec["pages"]), rec["stats"].get("main", "-")))
    # The one property no fixture can carry: a page BIGGER than the guest's
    # ceiling must come back as several pages and never as one cut one.
    big = ("<html><body>" + "<p>%s</p>" % ("word " * 40) * 400 + "</body></html>")
    links = LinkStore(None)
    rec = render(big, "http://example.invalid/", opts, links, "http://p:8088")
    if len(rec["pages"]) < 2:
        say("FAIL: a %s document did not paginate" % human(len(big)))
        fails += 1
    for i, page in enumerate(rec["pages"], 1):
        if len(page) > HARD_BODY:
            say("FAIL: paginated page %d is still %d bytes" % (i, len(page)))
            fails += 1
    say("a %s document paginated into %d pages, largest %s"
        % (human(len(big)), len(rec["pages"]),
           human(max(len(p) for p in rec["pages"]))))
    # Both action shapes, because the merged browser resolves one and a
    # browser built before it can only take the other, and a page that drops
    # its form is a search box that cannot be searched.
    form = ("<html><body><main><form action='/s' method='get'>"
            "<input name='q' size='20'><input type=submit value=Go>"
            "</form></main></body></html>")
    for mode, want in (("absolute", b'action="http://'), ("relative", b'action="/f/')):
        o2 = parse_args(["--form-action", mode])
        rec = render(form, "http://example.invalid/", o2, LinkStore(None),
                     "http://192.168.1.50:8088")
        page = rec["pages"][0]
        if want not in page:
            say("FAIL: --form-action %s did not emit %s" % (mode, want.decode()))
            fails += 1
        for b in check_output(page, o2, "form-action %s" % mode):
            say("FAIL: " + b)
            fails += 1
    say("SELFCHECK: %s" % ("%d FAILURE(S)" % fails if fails else "all good"))
    return 1 if fails else 0


def serve(opts):
    proxy = Proxy(opts)
    httpd = Server((opts.bind, opts.port), Handler)
    httpd.proxy = proxy
    where = opts.advertise or guess_address()
    say("os88proxy %s serving on %s:%d" % (VERSION, opts.bind, opts.port))
    say("on os8088:  Browser > Open Location  ->  %s:%d" % (where, opts.port))
    say("page budget %d bytes, %d columns, first-class: %s"
        % (opts.budget, opts.cols, ", ".join(s.name for s in SITES)))
    if os.name == "nt":
        say("Windows: the first run raises a firewall prompt - allow it on "
            "PRIVATE networks only")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        say("stopping")
    finally:
        proxy.links.save()
    return 0


def guess_address():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))      # no packet is sent
        return s.getsockname()[0]
    except Exception:
        return "127.0.0.1"
    finally:
        s.close()


def main(argv=None):
    opts = parse_args(argv if argv is not None else sys.argv[1:])
    if opts.selfcheck:
        return mode_selfcheck(opts)
    if opts.render or opts.render_file:
        return mode_render(opts)
    return serve(opts)


if __name__ == "__main__":
    sys.exit(main())
