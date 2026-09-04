#!/usr/bin/env python3
"""Issue #137's three, on the page that carries all of them.

    make && make browsertest && python3 tests/brtable.py [--adapter cga|herc]

`tests/htm/pubzone.htm` is the shape of an Apache directory index - a table of
links with an `<address>` footer - because that is what was reported off
files.bs0dd.net, and it is one page with three separate defects on it
(BROWSER-PLAN 14):

1. THE LAST LINE DREW TWICE. `br_layout` emitted the pending line and then
   asked whether the marker was `D_END`, and `.fin` emits it again. It shows
   only when the last text is followed by no block tag this parser knows -
   `</address></body></html>` is three unknown ones - which is why every
   fixture ending `</p></body></html>` passed.

2. AN ANCHOR IN A TABLE CELL HAD NO BRACKET. Two routines dropped the markers
   on the way into the composed row, so the painter drew no underline and
   `br_linkat` - which answers a click by scanning backward for a `D_LNK1` -
   found nothing and the click did nothing.

3. A `..` WENT TO THE SERVER. `br_resolve` composed `/a/b/../c.htm` and sent
   it; removing dot segments is the client's job.

**None of the three is visible to tools/htmsim.py**, which renders this page
correctly: two live below the text and the third is in a routine the model
does not have. So the assertions here are on the app's own line table, on the
link state a click resolves to, and on the PATH br_split would put on the
wire - never on a screenshot, and never on the model.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import dispcp                                          # noqa: E402
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88sym                                         # noqa: E402
from brclick import browser_syms, MACHINE              # noqa: E402

S = os88sym.linear
u16 = lambda b: b[0] | (b[1] << 8)                     # noqa: E731

D_LNK1, D_LNK0 = 14, 15
LNF_RULE, LNF_LNK = 1, 4
ADDRESS = "Apache/2.4.58 (Ubuntu) Server at files.bs0dd.net Port 80"
WANT_HREFS = ["http://files.bs0dd.net/pubzone/soft/../readme.txt",
              "disks/os8088.img",
              "a-very-long-archive-name-that-wraps.tar.gz",
              "http://files.bs0dd.net/pubzone/soft/../../pub/notes.txt"]
# The `..` link that is driven is the PROSE one, so that assertion 6 stands on
# its own: a run that fails 3 and 5 must still be able to reach 6, or the two
# fixes cannot be told apart by a bisect.
UNDOT_LINK = 3
WANT_PATH = "/pub/notes.txt"                           # #137 sent this as
                                                       # /pubzone/soft/../../pub/notes.txt
BN_ERR = 7


def say(*a):
    print(*a)
    sys.stdout.flush()


def render(doc, tab, i):
    """br_build's walk, in Python: the text of display line `i` and, per cell,
    whether it is inside a link. The hit test and the underline are both this
    state, so a cell's mark IS the claim `a link in a cell works`."""
    o, e = u16(tab[i * 6:i * 6 + 2]), u16(tab[i * 6 + 2:i * 6 + 4])
    col, fl = tab[i * 6 + 4], tab[i * 6 + 5]
    if fl & LNF_RULE:
        return "-" * 60, []
    text, mark = " " * col, [0] * col
    inlink = 1 if (fl & LNF_LNK) else 0
    j = o
    while j < e:
        ch = doc[j]
        if ch == D_LNK1:
            inlink = 1
            j += 3                                     # ...and its payload
            continue
        j += 1
        if ch == D_LNK0:
            inlink = 0
            continue
        if ch < 0x20:
            continue
        text += chr(0x20 if ch == 0x7F else ch)         # D_NBSP draws as space
        mark.append(inlink)
    return text, mark


def cells_of(text, mark, word):
    """The marks under `word` in this line, or None if it is not on it."""
    at = text.find(word)
    if at < 0:
        return None
    return mark[at:at + len(word)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", default="cga", choices=sorted(MACHINE))
    ap.add_argument("--shot", default=None)
    a = ap.parse_args()

    sy, fails = browser_syms(), []
    with os88marty.launch("build/os8088-360.img", apps="build/brtest360.img",
                          machine=MACHINE[a.adapter]) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
        before = dispcp.win_list(m, S)
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "PUBZONE.HTM")
        wins = dispcp.win_list(m, S)
        if len(wins) <= len(before):
            sys.exit("brtable: PUBZONE.HTM opened no window")
        rec = m.read(S("wm_wins") + wins[-1] * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        rb = lambda n: m.readseg(pseg, sy[n], 1)[0]     # noqa: E731
        rw = lambda n: u16(m.readseg(pseg, sy[n], 2))   # noqa: E731

        nl = rw("br_nlines")
        docseg, doclen = rw("br_docseg"), rw("br_doclen")
        comp = rw("br_comp")                            # the composed rows sit
        doc = bytes(m.readseg(docseg, 0, max(comp, doclen) + 16))
        tab = bytes(m.readseg(rw("br_lseg"), 0, nl * 6))
        lines = [render(doc, tab, i) for i in range(nl)]
        say("%d display lines:" % nl)
        for i, (t, _) in enumerate(lines):
            say("  %2d |%s|" % (i, t.rstrip()))

        # --- 1: the hrefs, in order -------------------------------------
        n = rw("br_lnkn")
        arena = bytes(m.readseg(rw("br_lnkseg"), 0, max(rw("br_lnkw"), 1)))
        offs = [u16(m.readseg(pseg, sy["br_lnkoff"] + i * 2, 2))
                for i in range(n)]
        hrefs = [arena[o:arena.index(b"\0", o)].decode("latin-1") for o in offs]
        say("hrefs: %r" % hrefs)
        if hrefs != WANT_HREFS:
            fails.append("hrefs %r != %r" % (hrefs, WANT_HREFS))

        # --- 2: THE LAST LINE IS THERE ONCE (BROWSER-PLAN 14.1) ---------
        # Counted over the whole line table rather than by looking at the
        # bottom of the window: the doubled entry is a second LINE, so it is
        # in the table whether it is scrolled to or not.
        hits = [i for i, (t, _) in enumerate(lines) if ADDRESS in t]
        say("the <address> line is display line(s) %r" % hits)
        if len(hits) != 1:
            fails.append("the page's last line appears on %d display lines, "
                         "not 1 - it is being emitted twice (#137)"
                         % len(hits))
        # ...and the shape of the bug, stated directly: two entries over one
        # span. A blank line is 0..0 twice over legitimately, so text only.
        spans = {}
        for i in range(nl):
            o, e = u16(tab[i * 6:i * 6 + 2]), u16(tab[i * 6 + 2:i * 6 + 4])
            if e > o:
                spans.setdefault(o, []).append(i)
        dup = {o: v for o, v in spans.items() if len(v) > 1}
        if dup:
            fails.append("display lines share a start offset: %r - one run of "
                         "text is on two lines" % dup)

        # --- 3: A CELL'S ANCHOR IS MARKED (BROWSER-PLAN 14.2) -----------
        # The mark is what the underline is drawn from AND what a click
        # resolves through, so this one assertion carries both symptoms.
        for word in ("readme.txt", "os8088.img"):
            got = None
            for t, mk in lines:
                c = cells_of(t, mk, word)
                if c is not None:
                    got = c
                    break
            if got is None:
                fails.append("%r is on no display line at all" % word)
            elif not all(got):
                fails.append("%r is in a table cell and is NOT inside a link "
                             "(marks %r) - the anchor's brackets were dropped "
                             "composing the row (#137)" % (word, got))
            else:
                say("%-12s marked inside a link across all %d cells"
                    % (word, len(got)))

        # --- 4: ...AND IT STOPS AT THE CELL WALL (BROWSER-PLAN 14.2.2) --
        # The long name wraps, so its anchor is open at the end of a fragment.
        # A row is ONE arena line carrying every column and br_linkat scans
        # BACKWARD, so a link left open there swallows the columns after it and
        # sends their clicks to the wrong page - which is worse than the dead
        # link this fixes.
        for i, (t, mk) in enumerate(lines):
            for word in ("2026-09-01", "4.0M", "2026-08-30", "1.2K"):
                c = cells_of(t, mk, word)
                if c is not None and any(c):
                    fails.append("line %d: %r is plain text in a later column "
                                 "and reads as INSIDE a link (%r) - the "
                                 "previous cell's anchor bled across the cell "
                                 "wall" % (i, word, c))
                elif c is not None:
                    say("line %2d %-11s clear of the link beside it" % (i, word))

        if a.shot:
            sw, sh, srows = (m.vram("herc") if a.adapter == "herc"
                             else m.vram("cga"))
            os88marty.write_png(a.shot, sw, sh, srows)

        # --- the aiming machinery, from brclick -------------------------
        def line_of(off):
            for i in range(nl):
                o, e = u16(tab[i * 6:i * 6 + 2]), u16(tab[i * 6 + 2:i * 6 + 4])
                if o <= off < e:
                    return i
            return None

        def column_of(line, off):
            o = u16(tab[line * 6:line * 6 + 2])
            col = tab[line * 6 + 4]
            e = u16(tab[line * 6 + 2:line * 6 + 4])
            i = o
            while i < e:
                if i == off:
                    return col
                ch = doc[i]
                if ch == D_LNK1:
                    i += 3
                    continue
                i += 1
                if ch < 0x20:
                    continue
                col += 1
            return None

        def goto(line):
            for _ in range(400):
                top, rows = rw("br_top"), rw("br_rows")
                if top <= line < top + rows:
                    return True
                was = top
                sx = rw("br_sbx") + 7
                mo.click(sx, rw("br_sby") + 5 if line < top
                         else rw("br_sby2") - 5)
                if rw("br_top") == was:
                    return False
            return True

        def click_link(idx):
            """Click the first character of link `idx`.

            **THE MARKER IS LOOKED FOR EVERYWHERE, and for a table link it is
            found PAST `br_doclen`.** A table's rows are composed into the
            arena above the document and the line table spans THOSE bytes, so
            the copy of the anchor a display line covers is the composed one -
            searching the parsed document alone finds the original, which is
            on no display line at all.
            """
            marker = bytes([D_LNK1, 0x10 | (idx & 15), 0x10 | (idx >> 4)])
            at, ln = -1, None
            while True:
                at = doc.find(marker, at + 1)
                if at < 0:
                    break
                ln = line_of(at + 3)
                if ln is not None:
                    break
            if at < 0:
                return "link %d's marker is in neither the document nor the " \
                       "composed arena - it was dropped (#137)" % idx
            if ln is None:
                return "link %d's text is on no display line" % idx
            if not goto(ln):
                return "could not scroll display line %d into view" % ln
            col = column_of(ln, at + 3)
            if col is None:
                return "link %d's first byte is in no cell" % idx
            os88marty.settle(m)
            mo.click(rw("br_cx") + col * 8 + 3,
                     rw("br_cy") + (ln - rw("br_top")) * 8 + 3)
            os88marty.settle(m)
            return None

        # --- 5: A CLICK ON A CELL'S LINK FOLLOWS IT ---------------------
        # The relative one, because this page came off a FLOPPY and so has no
        # server: the honest answer is a refusal naming that, and getting it
        # proves the click RESOLVED A LINK - before the fix br_linkat found no
        # marker and the click did nothing at all.
        err = click_link(1)
        if err:
            fails.append("table-cell link: " + err)
        else:
            st, msg = rb("br_nstate"), rw("br_nmsg")
            say("after clicking the cell's link: state %d msg %04X" % (st, msg))
            if st != BN_ERR:
                fails.append("a click on a link inside a table cell did not "
                             "resolve to a link: state is %d, and a page with "
                             "no server must refuse out loud" % st)
            elif msg != sy["br_s_nohost"]:
                fails.append("it refused, but not with the no-server reason "
                             "(%04X vs %04X)" % (msg, sy["br_s_nohost"]))

        # --- 6: `..` IS REMOVED BEFORE THE FETCH (BROWSER-PLAN 14.3) ----
        # LAST, because this one is ABSOLUTE: br_resolve accepts it, br_go
        # runs, and br_go frees the document every assertion above reads.
        # br_path is what br_split would put on the wire.
        err = click_link(UNDOT_LINK)
        if err:
            fails.append("the `..` link: " + err)
        else:
            raw = bytes(m.readseg(pseg, sy["br_path"], 96))
            path = raw.split(b"\0")[0].decode("latin-1")
            say("br_path after following the `..` link: %r" % path)
            if path != WANT_PATH:
                fails.append("the path put on the wire is %r, expected %r - "
                             "the dot segments were not removed (#137)"
                             % (path, WANT_PATH))

    for f in fails:
        print("FAIL:", f)
    print("brtable: %d assertion(s) failed" % len(fails))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
