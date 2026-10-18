#!/usr/bin/env python3
"""Can the PAGE be clicked? Links, a form field and the submit button.

    make && make browsertest && python3 tests/brclick.py [--adapter cga|herc]

Everything here was reachable by keyboard alone before, and on a real page
nobody presses Tab first: they click the box, type into a field that does not
have focus, and the keystrokes scroll the page instead. Four interaction paths.

1. LINKS PARSE AND UNDERLINE. tests/htm/demo.htm carries one of each shape the
   resolver has to tell apart - an in-page anchor, an https link, a relative
   one and an absolute one - so the hrefs coming back in order is also a check
   that none of them was swallowed or merged.

2. A CLICK ON A LINK FOLLOWS IT. The relative link is the one driven, because
   this page came off a FLOPPY and so has no server: the honest answer is a
   refusal naming that, and a browser that instead composed `http:///...` and
   let br_split reject it would report a bad ADDRESS about a good link.

3. RIGHT-CLICK ACTIONS. The link context menu opens on the selected href,
   copies that literal href to the system clipboard, and routes Open Link
   through the same navigation path as an ordinary click.

4. PAGE CONTEXT AND LOCAL BOOKMARKS. Right-clicking page background offers
   page actions, View Source toggles the retained source, and Ctrl+D on a local
   file reports why it cannot be bookmarked.

5. A CLICK REACHES A FORM FIELD AND THE SUBMIT BUTTON. Both are ordinary
   document text - that is what makes them wrap and scroll with no painter of
   their own - so both are found by document OFFSET, and this drives the whole
   path: click the field, type, click the button, read the composed query.

The positions are computed from the app's OWN line table and document rather
than from a screenshot: a click that lands one cell off is the failure this
test exists to catch, so it must not be aimed by eye.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

# THIS TREE'S root, DERIVED - never a hard-coded path. A literal is right in the
# checkout it was written in and wrong in a git worktree, which is how parallel
# work is done here: os88sym re-assembles ROOT/kernel/kernel.asm and compares it
# against ROOT/build/kernel.bin, so a literal ROOT answers about a DIFFERENT
# kernel from the image being booted.
_OS88_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tools"))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tests"))
import dispcp                                          # noqa: E402
import os88flush                                       # noqa: E402
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88sym                                         # noqa: E402
import os88build                                       # noqa: E402
from os88geom import MB_ENTSZ                          # noqa: E402

S = os88sym.linear
u16 = lambda b: b[0] | (b[1] << 8)                     # noqa: E731
u32 = lambda b: u16(b[:2]) | (u16(b[2:4]) << 16)      # noqa: E731
# THE MACHINE IS THE GLaBIOS TWIN, resolved rather than chosen here.
# This used to be a conditional on whether the IBM ROM happened to be
# in the checkout, which made the machine a property of the box - and
# on two of the four files that carried it, the path it tested was not
# the ROM's filename, so the IBM arm could never be taken at all.
# os88marty.machine() is the one place that decision lives now.
MACHINE = {c: os88marty.machine("os8088_5150_%s" % c)
           for c in ("cga", "herc")}

WANT_HREFS = ["#http", "https://example.invalid/", "torture.htm",
              "http://frogfind.de/", "#"]


def say(*a):
    print(*a)
    sys.stdout.flush()


def browser_syms():
    """The package's own map, asserted to describe the shipped binary."""
    d = tempfile.mkdtemp()
    src, mp, out = (os.path.join(d, n) for n in ("b.asm", "b.map", "b.bin"))
    shutil.copy("apps/browser/browser.asm", src)
    with open(src, "a") as f:
        f.write("\n[map all %s]\n" % mp)
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                    "-I", "apps/browser/", "-I", "drivers/net/",
                    "-o", out, src], check=True)
    if open(out, "rb").read() != open(os88build.at("build/browser.bin"), "rb").read():
        sys.exit("brclick: the mapped build is not build/browser.bin - every "
                 "offset it names would be plausible and wrong")
    syms = {}
    for line in open(mp):
        m = re.match(r"^\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+(\S+)\s*$", line)
        if m:
            syms[m.group(3)] = int(m.group(1), 16)
    return syms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", default="cga", choices=sorted(MACHINE))
    ap.add_argument("--apps", default="build/brtest360.img",
                    help="B: apps image (defaults to build/brtest360.img)")
    ap.add_argument("--shot", default=None)
    a = ap.parse_args()

    sy, fails = browser_syms(), []
    with os88marty.launch("build/os8088-360.img", apps=a.apps,
                          machine=MACHINE[a.adapter]) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "DEMO.HTM")
        w = dispcp.win_list(m, S)[-1]
        rec = m.read(S("wm_wins") + w * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        rb = lambda n: m.readseg(pseg, sy[n], 1)[0]        # noqa: E731
        rw = lambda n: u16(m.readseg(pseg, sy[n], 2))      # noqa: E731
        about = [
            ("br_ab1", "os8088 Browser 1.6b"),
            ("br_ab2", "Bringing the web to XT PCs"),
            ("br_ab3", "Version 1.0 contributed by Elendilon"),
            ("br_ab4", "Version 1.2-1.6b contributed by Pentagram666"),
        ]
        for symbol, expected in about:
            actual = bytes(m.readseg(pseg, sy[symbol], len(expected) + 1))
            if actual.split(b"\0", 1)[0].decode("ascii") != expected:
                fails.append("About line %s does not match %r" % (symbol, expected))
        # THE PAGE ARRIVES ON THE WAKE, after the window is up (SPEC.md 54.10):
        # read straight after the open and this is an empty browser
        os88marty.until(m, lambda _m: rw("br_nlines") > 0, "the page laid out",
                        limit=120)
        os88marty.settle(m)

        if rb("br_tfocus") == 3:
            fails.append("opening a page left keyboard link focus active")
        if rb("br_selkind") != 0 or rb("br_selmode") != 0:
            fails.append("opening a page left a page-text XOR selection active")

        if rb("br_islocal") != 1:
            fails.append("opening DEMO.HTM did not mark the page as local")
        m.ctrl("KeyD")
        os88marty.settle(m)
        if rw("br_nmsg") != sy["br_s_bmlocal"]:
            fails.append("Ctrl+D on a local file did not report the local-file refusal")
        say("Ctrl+D local status: %04X" % rw("br_nmsg"))

        # --- 0: the bar OPENS holding `http://`, focused, caret at the end
        # (SPEC.md 71.7). It is read before anything is clicked, because the
        # very next assertion below is that a page click takes the focus away.
        LN_LEN, LN_CAR, LN_FOCUS = 12, 14, 18
        bar = bytes(m.readseg(pseg, sy["br_ubuf"], 16)).split(b"\0")[0]
        blen = u16(m.readseg(pseg, sy["br_loc"] + LN_LEN, 2))
        bcar = u16(m.readseg(pseg, sy["br_loc"] + LN_CAR, 2))
        bfoc = m.readseg(pseg, sy["br_loc"] + LN_FOCUS, 1)[0]
        say("bar opens %r len %d caret %d focus %d"
            % (bar.decode("latin-1"), blen, bcar, bfoc))
        if bar != b"http://":
            fails.append("the bar opens holding %r, not 'http://'" % bar)
        if not bfoc:
            fails.append("the bar opens WITHOUT focus, so the first keystroke "
                         "goes to the page")
        if bcar != len(b"http://") or blen != len(b"http://"):
            fails.append("the caret is at %d of %d and should be at the end "
                         "of the scheme" % (bcar, blen))

        # --- 1: the link table ------------------------------------------
        n = rw("br_lnkn")
        lseg = rw("br_lnkseg")
        say("links: %d, arena %04X" % (n, lseg))
        if not lseg:
            sys.exit("brclick: no link arena was claimed - nothing below can "
                     "be measured")
        arena = bytes(m.readseg(lseg, 0, max(rw("br_lnkw"), 1)))
        # the arena IS the index (br_follow): hrefs packed in link order,
        # each ending at its NUL, so link N is the Nth string
        hrefs = [h.decode("latin-1") for h in arena.split(b"\0")[:n]]
        say("hrefs: %r" % hrefs)
        if hrefs != WANT_HREFS:
            fails.append("hrefs %r != %r" % (hrefs, WANT_HREFS))

        # --- the app's own line table, for aiming ------------------------
        docseg = rw("br_docseg")
        nl = rw("br_nlines")
        tab = bytes(m.readseg(rw("br_lseg"), 0, nl * 10))

        def line_of(off):
            for i in range(nl):
                o, e = u32(tab[i * 10:i * 10 + 4]), u32(tab[i * 10 + 4:i * 10 + 8])
                if o <= off < e:
                    return i
            return None

        def column_of(line, off):
            """br_build's walk, in Python: markers are not cells."""
            o = u32(tab[line * 10:line * 10 + 4])
            e = u32(tab[line * 10 + 4:line * 10 + 8])
            col = tab[line * 10 + 8]
            doc = bytes(m.readseg(docseg, o, e - o))
            i = 0
            while i < len(doc):
                if o + i == off:
                    return col
                ch = doc[i]
                if ch == 14:            # D_LNK1 carries three payload bytes
                    i += 4
                    continue
                i += 1
                if ch < 0x20:
                    continue
                col += 1
            return None

        def goto(line):
            """Scroll until `line` is in the band.

            **THE ARROWS ARE AIMED FROM THE APP'S OWN br_sb* RECT**, not from
            the window's corners. Guessing them cost this test a run: the band
            starts BR_LTOT below the content top, so a click 30px into the
            window lands in the location bar and the up arrow is never hit -
            which reads exactly like a browser that will not scroll up.
            """
            for _ in range(400):
                top, rows = rw("br_top"), rw("br_rows")
                if top <= line < top + rows:
                    return True
                before = rw("br_top")
                sx = rw("br_sbx") + 7
                if line < top:
                    mo.click(sx, rw("br_sby") + 5)          # up arrow cell
                else:
                    mo.click(sx, rw("br_sby2") - 5)         # ...and down
                if rw("br_top") == before:
                    return False        # clamped at an end: it cannot be shown
            return False

        def click_offset(off):
            ln = line_of(off)
            if ln is None:
                return "offset %d is on no display line" % off
            if not goto(ln):
                return "could not scroll display line %d into view" % ln
            col = column_of(ln, off)
            if col is None:
                return "offset %d is in no cell of line %d" % (off, ln)
            os88marty.settle(m)
            px = rw("br_cx") + col * 8 + 3
            py = rw("br_cy") + (ln - rw("br_top")) * 8 + 3
            mo.click(px, py)
            os88marty.settle(m)
            return None

        def point_offset(off):
            ln = line_of(off)
            if ln is None:
                return None, "offset %d is on no display line" % off
            if not goto(ln):
                return None, "could not scroll display line %d into view" % ln
            col = column_of(ln, off)
            if col is None:
                return None, "offset %d is in no cell of line %d" % (off, ln)
            os88marty.settle(m)
            return (rw("br_cx") + col * 8 + 3,
                    rw("br_cy") + (ln - rw("br_top")) * 8 + 3), None

        def right_click_at(px, py):
            mo.to(px, py)
            mo._edge(True, btn=2)
            mo._edge(False, btn=2)
            os88marty.settle(m)
            return None

        def right_click_offset(off):
            point, err = point_offset(off)
            if err:
                return err
            return right_click_at(*point)

        # A short drag across a rendered heading reaches the same source span
        # the painter uses. Ctrl+C must put only its visible text on the shared
        # OS clipboard; right-clicking while that selection is retained opens
        # a one-item Copy menu and preserves the selection until it is chosen.
        doc = bytes(m.readseg(docseg, 0, rw("br_doclen")))
        selected = b"os8088"
        sel_at = doc.find(selected)
        if sel_at < 0:
            fails.append("selection fixture text is not in the rendered document")
        else:
            # A press highlights one provisional cell so the drag can extend
            # it. Releasing without motion must remove that XOR; otherwise a
            # normal click leaves a stray glyph selected and hijacks the next
            # right-click as Copy.
            err = click_offset(sel_at)
            if err:
                fails.append("plain text click: " + err)
            else:
                marks = bytes(m.readseg(pseg, sy["br_lmark"], rw("br_cols")))
                if rb("br_selkind") != 0 or any(mark & 8 for mark in marks):
                    fails.append("a plain text click left its provisional XOR selection active")

            p0, e0 = point_offset(sel_at)
            p1, e1 = point_offset(sel_at + len(selected) - 1)
            if e0 or e1:
                fails.append("could not map the selection endpoints: %s %s"
                             % (e0, e1))
            else:
                mo.drag(p0[0], p0[1], p1[0], p1[1])
                os88marty.settle(m)
                marks = bytes(m.readseg(pseg, sy["br_lmark"], rw("br_cols")))
                selected_cells = sum(bool(mark & 8) for mark in marks)
                if rb("br_selkind") != 1 or selected_cells != len(selected):
                    fails.append("drag selection drew %d XOR cells for %d selected characters"
                                 % (selected_cells, len(selected)))
                m.ctrl("KeyC")
                os88marty.settle(m)
                cseg, cbytes = S("clip_seg"), S("clip_bytes")
                clipseg = u16(m.read(cseg, 2))
                cliplen = u16(m.read(cbytes, 2))
                clip = bytes(m.read(clipseg * 16, cliplen)) if cliplen else b""
                if clip != selected:
                    fails.append("Ctrl+C copied %r, expected %r" % (clip, selected))
                err = right_click_offset(sel_at)
                if err:
                    fails.append("right-click selected text: " + err)
                elif rb("br_ctxopen") != 1 or rb("br_ctxkind") != 2:
                    fails.append("right-click on retained text did not open its Copy menu")
                else:
                    if rb("br_ctxw") != 36:
                        fails.append("selection Copy menu width is %d, expected 36"
                                     % rb("br_ctxw"))
                    if rw("br_ctxx") + rb("br_ctxw") > rw("br_cols") * 8:
                        fails.append("selection Copy menu extends past the page band")
                    if rw("br_ctxy") + rb("br_ctxh") > rw("br_rows") * 8:
                        fails.append("selection Copy menu extends below the page band")
                    if rb("br_selkind") != 1:
                        fails.append("opening the selection menu discarded the text selection")
                    mo.click(rw("br_cx") + 2, rw("br_tby") + 2)
                    os88marty.settle(m)
                    if rb("br_ctxopen") != 0 or rb("br_selkind") != 1:
                        fails.append("dismissing the selection menu did not keep the text selected")
                    edge_x = rw("br_cx") + rw("br_cols") * 8 - 4
                    edge_y = p0[1]
                    err = right_click_at(edge_x, edge_y)
                    if err:
                        fails.append("right-edge selection menu: " + err)
                    elif rb("br_ctxopen") != 1 or rb("br_ctxkind") != 2:
                        fails.append("right-edge click did not open the selection menu")
                    elif (rw("br_ctxx") + rb("br_ctxw") - 1
                          != rw("br_cols") * 8 - 23):
                        fails.append("right-edge Copy menu did not flip left and align to the pointer")
                    mo.click(rw("br_cx") + rw("br_ctxx") + 8,
                             rw("br_cy") + rw("br_ctxy") + 8)
                    os88marty.settle(m)
                    if rb("br_ctxopen") != 0:
                        fails.append("Copy on the selection menu left it open")
                    if rb("br_selkind") != 1:
                        fails.append("Copy on the selection menu discarded the text selection")
                    clipseg = u16(m.read(cseg, 2))
                    cliplen = u16(m.read(cbytes, 2))
                    clip = bytes(m.read(clipseg * 16, cliplen)) if cliplen else b""
                    if clip != selected:
                        fails.append("selection menu copied %r, expected %r"
                                     % (clip, selected))
                    # A retained selection elsewhere on the page must not
                    # mask the link-specific menu under the pointer.
                    link_want = 2                    # 'torture.htm'
                    link_marker = bytes([14, 0x10 | (link_want & 15),
                                         0x10 | ((link_want >> 4) & 15),
                                         0x10 | (link_want >> 8)])
                    link_at = doc.find(link_marker)
                    if link_at < 0:
                        fails.append("selected-text fixture cannot find link %d"
                                     % link_want)
                    else:
                        err = right_click_offset(link_at + 4)
                        if err:
                            fails.append("right-click link with selection retained: "
                                         + err)
                        elif (rb("br_ctxopen") != 1
                              or rb("br_ctxkind") != 1
                              or rw("br_ctxlink") != link_want):
                            fails.append("right-click on a link with retained text "
                                         "selection did not open that link's menu")
                        else:
                            mo.click(rw("br_cx") + 2, rw("br_tby") + 2)
                            os88marty.settle(m)
                            if rb("br_ctxopen") != 0:
                                fails.append("dismissing the link menu left it open")
                err = click_offset(sel_at + len(selected) + 3)
                if err:
                    fails.append("clear text selection: " + err)

        # The first cell of DEMO.HTM's centered heading is blank. Clicking it
        # clears the retained selection without selecting another character;
        # then the same empty cell exercises the page context menu. A click on
        # ordinary text would create a one-cell selection and correctly route
        # the next right click to Copy instead.
        bgx, bgy = rw("br_cx") + 3, rw("br_cy") + 3
        mo.click(bgx, bgy)
        os88marty.settle(m)
        if rb("br_selkind") != 0:
            fails.append("clicking blank page padding did not clear the text selection")
        mo.to(bgx, bgy)
        os88marty.settle(m)
        baseline = m.fbuf()
        mo._edge(True, btn=2)
        mo._edge(False, btn=2)
        os88marty.settle(m)
        if rb("br_ctxopen") != 1 or rb("br_ctxkind") != 0:
            px, py, buttons = mo.where()
            fails.append("right-clicking page background did not open page actions "
                         "(pointer=%d,%d buttons=%02x target=%d,%d page=%d,%d "
                         "cols=%d rows=%d selection=%d popup=%d/%d)"
                         % (px, py, buttons,
                            rw("br_cx") + rw("br_cols") * 8 - 4,
                            rw("br_cy") + rw("br_rows") * 8 - 4,
                            rw("br_cx"), rw("br_cy"), rw("br_cols"),
                            rw("br_rows"), rb("br_selkind"),
                            rb("br_ctxopen"), rb("br_ctxkind")))
        else:
            old_ctxx, old_ctxy, old_ctxw, old_ctxh = (
                rw("br_ctxx"), rw("br_ctxy"), rb("br_ctxw"), rb("br_ctxh"))
            if rb("br_ctxw") != 124:
                fails.append("page context menu width is %d, expected 124"
                             % rb("br_ctxw"))
            if rw("br_ctxx") + rb("br_ctxw") > rw("br_cols") * 8:
                fails.append("page context menu extends past the page band")
            if rw("br_ctxy") + rb("br_ctxh") > rw("br_rows") * 8:
                fails.append("page context menu extends below the page band")
            # Assert the rasterised width as well as br_ctxw. On CGA the
            # framebuffer uses the same origin as the app's virtual coords;
            # Hercules' display-local origin differs, so its pixel check would
            # need the display transform rather than these window coordinates.
            if a.adapter == "cga":
                opened = m.fbuf()
                _fw, _fh, before = baseline
                aw, _ah, pixels = opened
                frame_y = rw("br_cy") + rw("br_ctxy")
                frame_x = rw("br_cx") + rw("br_ctxx")
                expected = (frame_x, frame_x + old_ctxw - 1)
                changed_ink = []
                for x in range(rw("br_cx"), rw("br_cx") + rw("br_cols") * 8):
                    i = (frame_y * aw + x) * 3
                    if pixels[i:i + 3] == b"\0\0\0" and before[i:i + 3] != pixels[i:i + 3]:
                        changed_ink.append(x)
                if (not changed_ink
                        or (min(changed_ink), max(changed_ink)) != expected):
                    actual = ((min(changed_ink), max(changed_ink))
                              if changed_ink else None)
                    fails.append("page menu top border pixels span %r, expected %r"
                                 % (actual, expected))
                outside = []
                left, top = frame_x, frame_y
                right, bottom = left + old_ctxw, top + rb("br_ctxh")
                for y in range(rw("br_cy"), rw("br_cy") + rw("br_rows") * 8):
                    for x in range(rw("br_cx"), rw("br_cx") + rw("br_cols") * 8):
                        if left <= x < right and top <= y < bottom:
                            continue
                        i = (y * aw + x) * 3
                        if before[i:i + 3] != pixels[i:i + 3]:
                            outside.append((x, y))
                if outside:
                    fails.append("opening the page menu changed %d pixels outside its box; bounds=%r..%r"
                                 % (len(outside),
                                    (min(x for x, _ in outside), min(y for _, y in outside)),
                                    (max(x for x, _ in outside), max(y for _, y in outside))))
                # Repeat over ordinary rendered text, not just the blank
                # heading margin above. The popup should cover only the text
                # cells under its own white rectangle; the line to its left
                # must remain byte-for-byte unchanged.
                text_at = doc.find(b"boots from a single")
                text_point, text_err = point_offset(text_at)
                if text_at < 0 or text_err:
                    fails.append("could not aim the menu-over-text raster check")
                else:
                    mo.click(rw("br_cx") + 2, rw("br_tby") + 2)  # dismiss
                    os88marty.settle(m)
                    if rb("br_ctxopen") != 0:
                        fails.append("could not dismiss the first page context menu before text raster check")
                    mo.to(*text_point)
                    os88marty.settle(m)
                    text_baseline = m.fbuf()
                    right_click_at(*text_point)
                    if rb("br_ctxopen") != 1 or rb("br_ctxkind") != 0:
                        fails.append("right-click on ordinary text did not open page actions")
                    else:
                        text_opened = m.fbuf()
                        tfw, tfh, tbefore = text_baseline
                        taw, _tah, tafter = text_opened
                        tx = rw("br_cx") + rw("br_ctxx")
                        ty = rw("br_cy") + rw("br_ctxy")
                        tw, th = rb("br_ctxw"), rb("br_ctxh")
                        changed_outside = []
                        erased_inside = 0
                        for yy in range(rw("br_cy"), rw("br_cy") + rw("br_rows") * 8):
                            for xx in range(rw("br_cx"), rw("br_cx") + rw("br_cols") * 8):
                                bi, ai = (yy * tfw + xx) * 3, (yy * taw + xx) * 3
                                old, new = tbefore[bi:bi + 3], tafter[ai:ai + 3]
                                if tx <= xx < tx + tw and ty <= yy < ty + th:
                                    if old == b"\0\0\0" and new != old:
                                        erased_inside += 1
                                elif old != new:
                                    changed_outside.append((xx, yy))
                        if not erased_inside:
                            fails.append("text-overlap check did not cover any underlying glyph pixels")
                        if changed_outside:
                            fails.append("opening the menu over text changed %d pixels outside its box; bounds=%r..%r"
                                         % (len(changed_outside),
                                            (min(x for x, _ in changed_outside), min(y for _, y in changed_outside)),
                                            (max(x for x, _ in changed_outside), max(y for _, y in changed_outside)))
                                         + " popup=(%d,%d %dx%d) pointer=%r"
                                         % (tx, ty, tw, th, text_point))
                        # Leave a clean page menu for the edge-placement checks.
                        mo.click(rw("br_cx") + 2, rw("br_tby") + 2)
                        os88marty.settle(m)
            edge_x = rw("br_cx") + rw("br_cols") * 8 - 4
            edge_y = rw("br_cy") + rw("br_rows") * 8 - 4
            err = right_click_at(edge_x, edge_y)
            if err:
                fails.append("reopen page menu at bottom-right: " + err)
            elif rb("br_ctxopen") != 1 or rb("br_ctxkind") != 0:
                fails.append("right-click did not replace the open page menu")
            elif (rw("br_ctxx") + rb("br_ctxw") - 1
                  != rw("br_cols") * 8 - 23):
                fails.append("page menu did not flip left from the right edge")
            elif (rw("br_ctxy") + rb("br_ctxh")
                  != rw("br_rows") * 8 - 22):
                fails.append("page menu did not move up from the bottom edge")
            # The old XOR frame must be fully removed when a second popup
            # replaces it. Compare its perimeter: the interior can contain
            # underlying text that is repainted when the old popup is erased.
            after = m.fbuf()
            fw, fh, _before_px = baseline
            ax, ay, current = after
            x0 = rw("br_cx") + old_ctxx
            y0 = rw("br_cy") + old_ctxy
            x1, y1 = min(x0 + old_ctxw, fw, ax), min(y0 + old_ctxh, fh, ay)
            perimeter = set()
            for x in range(x0, x1):
                perimeter.add((x, y0))
                perimeter.add((x, y1 - 1))
            for y in range(y0, y1):
                perimeter.add((x0, y))
                perimeter.add((x1 - 1, y))
            old_pixels = []
            for x, y in perimeter:
                i0, i1 = (y * fw + x) * 3, (y * ax + x) * 3
                old_pixels.append((baseline[2][i0:i0 + 3],
                                   current[i1:i1 + 3]))
            old_before = b"".join(pair[0] for pair in old_pixels)
            old_after = b"".join(pair[1] for pair in old_pixels)
            if old_before != old_after:
                fails.append("replacing the page menu left pixels on its old frame")
            mo.click(rw("br_cx") + rw("br_ctxx") + 8,
                     rw("br_cy") + rw("br_ctxy") + 18)  # View Source
            os88marty.settle(m)
            if rb("br_viewsrc") != 1:
                fails.append("page context View Source did not show the retained source")
            err = right_click_at(bgx, bgy)
            if err:
                fails.append("reopen the page context menu in source view: " + err)
            elif rb("br_ctxopen") != 1 or rb("br_ctxkind") != 0:
                fails.append("source view did not offer its page context menu")
            else:
                close_label = bytes(m.readseg(pseg, sy["br_s_ctxclose"], 16)).split(b"\0", 1)[0]
                if close_label != b"Close Source":
                    fails.append("source-view menu label is %r, expected Close Source"
                                 % close_label)
                mo.click(rw("br_cx") + rw("br_ctxx") + 8,
                         rw("br_cy") + rw("br_ctxy") + 18)
                os88marty.settle(m)
                if rb("br_viewsrc") != 0:
                    fails.append("Close Source did not restore the rendered page")

        # --- 2: click the RELATIVE link ---------------------------------
        # find where it sits in the document: the byte after its D_LNK1
        want = 2                                    # 'torture.htm', index 2
        marker = bytes([14, 0x10 | (want & 15),
                        0x10 | ((want >> 4) & 15), 0x10 | (want >> 8)])
        at = doc.find(marker)
        if at < 0:
            fails.append("link %d's marker is not in the document" % want)
        else:
            # The Browser owns a small package-side right-click menu. Exercise
            # both actions and inspect the system clipboard by its public state.
            err = right_click_offset(at + 4)
            if err:
                fails.append("right-click link: " + err)
            elif rb("br_ctxopen") != 1 or rw("br_ctxlink") != want:
                fails.append("right-click did not open the menu for link %d"
                             % want)
            else:
                if rb("br_ctxw") != 124:
                    fails.append("link context menu width is %d, expected 124"
                                 % rb("br_ctxw"))
                if rw("br_ctxx") + rb("br_ctxw") > rw("br_cols") * 8:
                    fails.append("link context menu extends past the page band")
                if rw("br_ctxy") + rb("br_ctxh") > rw("br_rows") * 8:
                    fails.append("link context menu extends below the page band")
                mx = rw("br_cx") + rw("br_ctxx") + 8
                my = rw("br_cy") + rw("br_ctxy") + 18
                mo.click(mx, my)               # Copy Link
                os88marty.settle(m)
                cseg, cbytes = S("clip_seg"), S("clip_bytes")
                clipseg, cliplen = u16(m.read(cseg, 2)), u16(m.read(cbytes, 2))
                clip = bytes(m.read(clipseg * 16, cliplen)) if cliplen else b""
                say("context copy: %r" % clip)
                if clip != b"torture.htm":
                    fails.append("Copy Link put %r in the clipboard, not the "
                                 "stored href" % clip)
                if rb("br_ctxopen") != 0:
                    fails.append("Copy Link left the context menu open")

                err = right_click_offset(at + 4)
                if err:
                    fails.append("right-click link for Open Link: " + err)
                else:
                    mx = rw("br_cx") + rw("br_ctxx") + 8
                    my = rw("br_cy") + rw("br_ctxy") + 6
                    mo.click(mx, my)           # Open Link
                    os88marty.settle(m)
                    if rb("br_ctxopen") != 0:
                        fails.append("Open Link left the context menu open")
                    if rb("br_nstate") != 7 or rw("br_nmsg") != sy["br_s_nohost"]:
                        fails.append("Open Link did not use the normal relative-"
                                     "link/no-server path")

            err = click_offset(at + 4)
            if err:
                fails.append("relative link: " + err)
            else:
                if rb("br_selkind") != 0:
                    fails.append("a plain link click left a provisional text "
                                 "selection behind")
                st = rb("br_nstate")
                msg = rw("br_nmsg")
                say("after clicking the relative link: state %d, msg %04X"
                    % (st, msg))
                if st != 7:                         # BN_ERR
                    fails.append("a relative link on a page with no server "
                                 "should refuse out loud; state is %d" % st)
                elif msg != sy["br_s_nohost"]:
                    fails.append("it refused, but not with the no-server "
                                 "reason (%04X vs %04X) - any old error here "
                                 "would pass a state check"
                                 % (msg, sy["br_s_nohost"]))

        # --- 2b: THAT PAGE CLICK TOOK THE FOCUS AWAY (SPEC.md 71.7) ------
        # A caret drawn in a control the keyboard is no longer going to is
        # the whole of the bug; LN_FOCUS is what draws it.
        bfoc = m.readseg(pseg, sy["br_loc"] + LN_FOCUS, 1)[0]
        say("bar focus after a click on the page: %d" % bfoc)
        if bfoc:
            fails.append("a click on the page left the location bar focused, "
                         "so it is still showing a caret")

        # --- 2c: A COALESCED SCROLL STILL DRAWS (SPEC.md 71.9) -----------
        # br_scroll_by skips the redraw while more events are queued, which is
        # only safe because the skip is BOUNDED - so the assertion is that the
        # glass catches up. [br_ptop] is what the screen shows and [br_top] is
        # where the view is; the two part during a burst and must not stay
        # parted. goto() above has just driven a burst of arrow clicks.
        os88marty.settle(m)
        top, ptop = rw("br_top"), rw("br_ptop")
        say("after the scrolling: br_top %d, br_ptop %d" % (top, ptop))
        if top != ptop:
            fails.append("the view is at line %d and the screen is drawn for "
                         "%d - a coalesced scroll never resolved" % (top, ptop))

        # --- 3: the form field, then the submit button -------------------
        fofs = u16(m.readseg(pseg, sy["br_flds"], 2))
        subs = sy["br_subs"]
        sub0 = u16(m.readseg(pseg, subs, 2))
        sub1 = u16(m.readseg(pseg, subs + 2, 2))
        say("field @%d, submit %d..%d" % (fofs, sub0, sub1))
        err = click_offset(fofs)
        if err:
            fails.append("field: " + err)
        else:
            say("focus after the click: %02X" % rb("br_fcur"))
            if rb("br_fcur") != 0:
                fails.append("clicking the field did not focus it (%02X)"
                             % rb("br_fcur"))
            m.type_text("hi")           # ...through the real 8255 and int 09h
            os88marty.settle(m)
            typed = bytes(m.readseg(docseg, fofs, 4)).decode("latin-1")
            say("the field holds %r" % typed)
            if not typed.startswith("hi"):
                fails.append("typing after a click did not reach the field: %r"
                             % typed)
            # --- 3b: BACKSPACE LEAVES NO TRAIL (SPEC.md 71.7) ------------
            # The field's caret is a document byte, so moving it left means
            # blanking where it was - and nothing did, so the field filled up
            # with '_', one per backspace. Exactly one may be present.
            m.type_text("\b")
            os88marty.settle(m)
            after = bytes(m.readseg(docseg, fofs, 8))
            say("after one backspace the field holds %r"
                % after.decode("latin-1"))
            if after.count(b"_") != 1:
                fails.append("backspace left %d carets in the field (%r) - "
                             "the vacated cell was not blanked"
                             % (after.count(b"_"), after))
            if not after.startswith(b"h_"):
                fails.append("backspace did not remove the character: %r"
                             % after)
            m.type_text("i")                        # ...put it back for 3
            os88marty.settle(m)
        err = click_offset(sub0 + 2)                # inside '< Submit >'
        if err:
            fails.append("submit: " + err)
        else:
            url = bytes(m.readseg(pseg, sy["br_url"], 120))
            url = url.split(b"\0")[0].decode("latin-1")
            say("composed query: %r" % url)
            if "q=hi" not in url:
                fails.append("clicking Submit did not compose the query: %r"
                             % url)

        # Tab is the Browser's focus ring, not a blind key sink. Check that
        # Ctrl+L erases it without repainting unrelated page rows.
        first_link = doc.find(bytes([14, 0x10, 0x10, 0x10]))
        if first_link < 0:
            fails.append("could not find the first link marker for focus rendering")
        else:
            first_link_line = line_of(first_link + 4)
            if first_link_line is None or not goto(first_link_line):
                fails.append("could not bring the first link into view for focus rendering")

        def page_pixels(frame):
            fw, fh, data = frame
            x0, x1 = rw("br_cx"), rw("br_cx") + rw("br_cols") * 8
            y0, y1 = rw("br_cy"), rw("br_cy") + rw("br_rows") * 8
            x1, y1 = min(fw, x1), min(fh, y1)
            return b"".join(data[(y * fw + x0) * 3:(y * fw + x1) * 3]
                            for y in range(y0, y1))

        m.ctrl("KeyL")
        os88marty.settle(m)
        if m.readseg(pseg, sy["br_loc"] + LN_FOCUS, 1)[0] != 1:
            fails.append("Ctrl+L did not focus the location field")
        base_focus = m.fbuf()
        for _ in range(rb("br_fn") + rb("br_lnkn") + 2):
            m.key("Tab")
            os88marty.settle(m)
            if rb("br_tfocus") == 3:
                break
        if rb("br_tfocus") != 3:
            fails.append("Tab did not reach a link in the focus cycle")
        if rw("br_tlinkidx") >= rw("br_lnkn"):
            fails.append("Tab left link focus without a valid link index")
        if rw("br_tend") <= rw("br_tstart"):
            fails.append("focused link has an empty visible text span")
        if rw("br_tlinkidx") == 0 and first_link >= 0:
            focus_line = line_of(rw("br_tstart"))
            cols = [column_of(focus_line, off)
                    for off in range(rw("br_tstart"), rw("br_tend"))]
            cols = [col for col in cols if col is not None]
            if focus_line is None or not cols or focus_line != first_link_line:
                fails.append("could not map the focused link's rendered cells")
            else:
                m.ctrl("KeyL")
                os88marty.settle(m)
                cleared_frame = m.fbuf()
                if page_pixels(cleared_frame) != page_pixels(base_focus):
                    fails.append("Ctrl+L left stale XOR pixels on the focused link")
                marks = bytes(m.readseg(pseg, sy["br_lmark"], rw("br_cols")))
                focus_cells = [i for i, mark in enumerate(marks) if mark & 4]
                if focus_cells:
                    fails.append("a line link flag leaked into LM_FOCUS cells: %r"
                                 % focus_cells)

        # Return to link focus so the mouse route below proves the same marker
        # is cleared when the address bar is clicked.
        m.ctrl("KeyL")
        os88marty.settle(m)
        for _ in range(rb("br_fn") + rb("br_lnkn") + 2):
            m.key("Tab")
            os88marty.settle(m)
            if rb("br_tfocus") == 3:
                break
        if rb("br_tfocus") == 3 and rw("br_tlinkidx") == 0:
            # The first fixture link is an in-page anchor, deliberately inert
            # in this browser version. Advance to the HTTPS link: Enter must
            # activate it and produce the browser's explicit TLS refusal.
            m.key("Tab")
            os88marty.settle(m)
            if rb("br_tfocus") != 3 or rw("br_tlinkidx") != 1:
                fails.append("Tab did not advance focus to the HTTPS link")
            else:
                m.key("Enter")
                os88marty.settle(m)
                if (rb("br_nstate") != 7
                        or rw("br_nmsg") != sy["br_s_nohttps"]):
                    fails.append("Enter did not activate the focused link: "
                                 "state=%d message=%04X"
                                 % (rb("br_nstate"), rw("br_nmsg")))
        # A mouse click on the location field must also remove the old link's
        # XOR marker from the page.
        lnx1 = u16(m.readseg(pseg, sy["br_loc"], 2))
        lny1 = u16(m.readseg(pseg, sy["br_loc"] + 2, 2))
        mo.click(lnx1 + 12, lny1 + 4)
        os88marty.settle(m)
        if rb("br_tfocus") != 1:
            fails.append("clicking the location bar left keyboard link focus active")
        m.ctrl("KeyL")
        os88marty.settle(m)
        if m.readseg(pseg, sy["br_loc"] + LN_FOCUS, 1)[0] != 1:
            fails.append("Ctrl+L did not return focus from a link to the address")

        # Replace the local file address with a remote URL, let the ordinary
        # no-host path settle, then exercise save/remove, the two icon rasters
        # and the live Bookmarks menu on this same instance.
        BR_LPAD, BR_TBH, BR_TBG, BR_LBAR = 3, 13, 1, 15
        mo.click(rw("br_cx") + rw("br_cw") - 40,
                 rw("br_tby") + BR_TBH + BR_TBG + BR_LBAR // 2)
        os88marty.settle(m)
        bookmark_url = "http://bookmark.invalid/"
        m.type_text("\b" * 120 + bookmark_url)
        os88marty.settle(m)
        m.key("Enter")
        os88marty.settle(m)
        os88marty.until(m, lambda _m: rb("br_nstate") in (6, 7),
                        "the no-host URL to settle before the bookmark test",
                        limit=60)
        if rb("br_islocal") != 0:
            fails.append("entering a remote URL did not leave local-file mode")
        if bytes(m.readseg(pseg, sy["br_ubuf"], 160)).split(b"\0", 1)[0] != bookmark_url.encode():
            fails.append("the address field lost the remote bookmark URL")

        bm = sy["br_bmdata"]
        bm_count = lambda: m.readseg(pseg, bm + 4, 1)[0]  # noqa: E731
        sx, sy0 = rw("br_starx1") + 7, rw("br_stary1") + 5
        try:
            mo.click(sx, sy0)
            os88marty.settle(m)
            os88marty.settle(m)
            os88marty.until(m, lambda _m: bm_count() == 1 and rb("br_bmsaved") == 1
                            and rb("br_bmreq") == 0
                            and rw("br_nmsg") == sy["br_s_bmsaved"],
                            "bookmark add wake", limit=60)
            say("bookmark after add: count=%d saved=%d req=%d op=%d status=%04X"
                % (bm_count(), rb("br_bmsaved"), rb("br_bmreq"),
                   rb("br_bmopwork"), rw("br_nmsg")))
            say("star rect %d,%d..%d,%d click %d,%d local=%d"
                % (rw("br_starx1"), rw("br_stary1"), rw("br_starx2"),
                   rw("br_stary2"), sx, sy0, rb("br_islocal")))
            if bm_count() != 1 or rb("br_bmsaved") != 1:
                fails.append("clicking the empty bookmark star did not add it")
            try:
                saved = os88flush.Flush(marty=m).volume(1).read(
                    "SYSTEM/APPDATA/BROWSE.DAT")
                if (saved[:5] != b"BM01\x01"
                        or bookmark_url.encode("ascii") not in saved):
                    fails.append("BROWSE.DAT does not contain the saved URL: %r"
                                 % saved[:64])
                else:
                    say("BROWSE.DAT persisted on B: (%d bytes)" % len(saved))
            except (OSError, os88flush.FlushError) as e:
                fails.append("BROWSE.DAT was not readable from B: after save: %s"
                             % e)
            mo.click(sx, sy0)
            os88marty.settle(m)
            say("after filled-star click: req=%d saved=%d local=%d ctx=%d sel=%d/%d focus=%d pointer=%r rect=%d,%d..%d,%d"
                % (rb("br_bmreq"), rb("br_bmsaved"), rb("br_islocal"),
                   rb("br_ctxopen"), rb("br_selkind"), rb("br_selmode"),
                   m.readseg(pseg, sy["br_loc"] + LN_FOCUS, 1)[0],
                   mo.where(), rw("br_starx1"), rw("br_stary1"),
                   rw("br_starx2"), rw("br_stary2")))
            os88marty.until(m, lambda _m: bm_count() == 0 and rb("br_bmsaved") == 0
                            and rb("br_bmreq") == 0
                            and rw("br_nmsg") == sy["br_s_bmremoved"],
                            "bookmark remove wake", limit=60)
            say("bookmark after remove: count=%d saved=%d req=%d op=%d status=%04X"
                % (bm_count(), rb("br_bmsaved"), rb("br_bmreq"),
                   rb("br_bmopwork"), rw("br_nmsg")))
            if bm_count() != 0 or rb("br_bmsaved") != 0:
                fails.append("clicking a filled bookmark star did not remove it")
            mo.click(sx, sy0)
            os88marty.settle(m)
            os88marty.until(m, lambda _m: bm_count() == 1 and rb("br_bmsaved") == 1,
                            "bookmark restore wake", limit=60)
            if bm_count() != 1 or rb("br_bmsaved") != 1:
                fails.append("clicking the empty bookmark star did not restore it")
        except os88mouse.MartyError as e:
            say("bookmark failure state: count=%d saved=%d req=%d op=%d status=%04X"
                % (bm_count(), rb("br_bmsaved"), rb("br_bmreq"),
                   rb("br_bmopwork"), rw("br_nmsg")))
            fails.append("bookmark star pointer test: %s" % e)

        bookmark_cell = None
        for c in range(7):
            base = S("menu_bar") + c * MB_ENTSZ
            xl = u16(m.read(base + 6, 2))
            xr = u16(m.read(base + 8, 2))
            tptr = u16(m.read(base, 2))
            if tptr and xr > xl:
                name = bytes(m.readseg(pseg, tptr, 16)).split(b"\0")[0]
                say("menu cell %d: %r x=%d..%d seg=%04X" %
                    (c, name, xl, xr, u16(m.read(base + 10, 2))))
                if name == b"Bookmarks":
                    bookmark_cell = (xl + xr) // 2
        if bookmark_cell is None:
            fails.append("no Bookmarks menu cell in the menu bar")
        else:
            try:
                mo.menu(bookmark_cell, 6, bookmark_cell, 26)
                os88marty.settle(m)
                selected_url = bytes(m.readseg(pseg, sy["br_ubuf"], 160)).split(b"\0", 1)[0]
                if selected_url != bookmark_url.encode():
                    fails.append("Bookmarks menu selected %r instead of %r"
                                 % (selected_url, bookmark_url))
                if rb("br_bmsaved") != 1:
                    fails.append("selecting the saved URL left its star empty")
            except os88mouse.MartyError as e:
                fails.append("Bookmarks menu pointer test: %s" % e)
        if a.shot:
            wpx, hpx, data = m.fbuf()
            os88marty.write_png_rgb(a.shot, wpx, hpx, data)
            say("wrote %s" % a.shot)

    for f in fails:
        say("FAIL: " + f)
    say("brclick: " + ("ok" if not fails else "FAILED"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
