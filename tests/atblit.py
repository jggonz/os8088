#!/usr/bin/env python3
"""ArtfulType's band emit (SPEC.md 46.4.2) - the A/B that says it drew the
SAME PICTURE and did far less work to draw it.

    make && python3 tests/atblit.py              # CGA
    python3 tests/atblit.py --card herc
    python3 tests/atblit.py --card vga --census

WHAT IS UNDER TEST. `at_draw_line` used to widen `at_compose`'s 1bpp strip to
packed 4bpp (`at_expand`) purely so `OSAPI_GFX_BLIT4` could pack it straight
back down, on every adapter and every line. 46.4.2 composes the strip in
SCREEN polarity instead and hands it to `OSAPI_GFX_BLIT1`, which is a
`rep movsw` a row. `NOATBLIT1=1` is the arm before that, and it assembles BYTE
FOR BYTE IDENTICAL to the package that shipped - so the two trees differ in
this and in nothing else, which is what makes the comparison below mean
anything.

THE ASSERTION IS THE PICTURE, and it has to be, because every failure mode of
a polarity flip is a plausible-looking wrong image rather than a crash. Miss
one of the FIVE writers into `at_strip1` and that one element renders
inverted - and the fifth is `at_bigtext` in another file, which is exactly the
one an audit of `atrend.inc` misses. Complement above the italic `rcr` chain
and every italic grows a bar down its left edge. Forget `AT_X4TAB` and the
surviving 4bpp fallback draws the negative, which no kern_big row would ever
execute. None of those raise anything anywhere.

So the gate is `0 differing pixels` between the arms over a document that
exercises all six styles and a heading, and it FAILS LOUD on exactly the
mistakes the change invites. Break it on purpose to see it work: flip one of
`AT_PAPERAX`/`AT_MERGE`/`AT_RULE` in artful.asm and this goes red.

THE CENSUS is the second half, and it is what says the win is real rather than
that the picture merely survived: `gfx_blit4` must fall to ZERO on the lines
the band arm takes, and `gfx_blit1` must rise to one per line.

The band arm is reached by a DIFFERENT gate per adapter, which is why this
runs on three: 1bpp takes it outright (`at_codebg` has already cleared
`at_cellbg`, so no grey column survives), and a colour adapter takes it only
for a line with no code span. The `code` line below is therefore the one line
that must still go through the expander on VGA and must not on CGA or
Hercules - so on VGA the census floor for `gfx_blit4` is not zero.
"""
import sys, os, argparse
sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import os88fixture                                       # noqa: E402
ROOT = "/home/user/os8088"


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)
import os88marty, os88ui, os88build, os88sym, os88geom   # noqa: E402
import os88mouse                                         # noqa: E402
import os, subprocess, tempfile                          # noqa: E402

# Every style ArtfulType can draw, so a missed writer has somewhere to show.
#
# THE ITALIC HEADING IS DELIBERATE. at_cellwtab makes an H1 scale 2 at zoom 0
# and scale 3 at zoom 1, and 46.4.5's shear is a 32-bit rotate across four
# bytes - so an italic glyph wide enough for its overhang to reach strip byte 3
# is the only thing that exercises the DL end of it. Getting that byte order
# wrong mirrors every italic and raises nothing, so the ZOOM scene rendering
# this line at scale 3 is what would catch it.
# EVERY STYLED RUN HERE CONTAINS A SPACE, deliberately. SPEC.md 46.4.6 skips
# composing a blank cell, and the two styles that INK one - a link's underline
# and a strike - are drawn after the row loop, so a space inside either must
# still be composed. A run without a space in it would never test that.
DOC = ["# *Heading* one",
       # A heading with NO delimiter in it, which is the only thing that
       # isolates 46.4.7's heading test: the italic one above is excluded by
       # the `*` anyway, so removing the heading test on purpose would still
       # come out green without this line.
       "# Plain heading here",
       "Plain body text for the ordinary path.",
       "This is **bold text** and *italic here* and ~~struck out~~.",
       "A `code span` is the three-colour line.",
       "A [link text](http://os8088.com) underlines it.",
       # ...and a bold run long enough that its MIDDLE visual line carries an
       # open span and NO delimiter of any kind. That line is plain by every
       # test in at_plain but the entry-nibble one, so it is the only thing
       # that isolates it. The closing ** has to land on a THIRD line: put it
       # on the second and the character test catches it instead.
       "A **bold run that is deliberately long enough to wrap onto a "
       "second visual line which contains no delimiter of any kind at "
       "all and then carries on for a while longer still** and ends.",
       # ...and a line whose delimiters land ALL OVER IT, including in the
       # OVERSHOOT - the characters at_scan walks past the wrap point before
       # rewinding to the last space. That region is the only thing the span
       # rewind (SPEC.md 46.3.2) exists for: a delimiter there belongs to the
       # NEXT visual line, and carrying its toggle back into this one's end
       # state mis-styles everything after the break. One `*` every four
       # characters over three visual lines puts one in nearly every
       # overshoot; the wrapped run above has its delimiters at the ends and
       # never does.
       " ".join("*%c*" % c for c in "abcdefghijklmnopqrstuvwxyzabcdefghijklmn"),
       # ...and a PLAIN paragraph long enough to wrap onto three visual lines
       # on the narrowest adapter. SPEC.md 46.4.11 repaints ONE line instead
       # of the paragraph when the edit is an append to a plain one, and every
       # other paragraph above is either styled or one visual line long - so
       # without this the gate is never taken and the row is green whatever it
       # does. It has to be PLAIN in 46.4.8's sense: no ` * ~ [ anywhere in
       # it, which also rules out an apostrophe-free reading of the rule (the
       # four characters are the whole list).
       "this plain paragraph is deliberately long enough that it wraps onto "
       "three visual lines even on the narrowest of the three adapters, "
       "which is what makes it the witness for the one line repaint, and it "
       "carries no delimiter of any kind so that every keystroke in it takes "
       "the gate rather than the general path below it"]

CARDS = {"cga":  "os8088_5150_cga_gla",
         "herc": "os8088_5150_herc_gla",
         "vga":  "os8088_xt_vga"}

# HOW MANY LINES IT TAKES TO OVERFLOW THE VIEW, per adapter. at_geom_init's
# text region is ty0..ty1 and a body line is 10px, so CGA shows 16 lines
# (200-row screen, ty0 = 30), Hercules 28 and VGA 41. A count that overflows
# CGA leaves the other two with a document that FITS - no scroll bar, no
# scrolling, and two scenes that quietly test almost nothing. The zoom scene's
# [at_top] assertion is what caught that, on every knob at once.
FILLER = {"cga": 22, "herc": 34, "vga": 48}

COUNTED = ("gfx_blit4", "gfx_blit1")


def pkg_syms(defines=()):
    """ArtfulType's bss offsets, by re-assembling it - editmove.py's trick.

    They are `equ os88_image_end + N` behind macros, so they are neither
    greppable nor stable against an edit, and a wrong one reads a plausible
    word out of the middle of another variable. The DEFINES travel with it:
    NOATBLIT1 changes the image size, so the same name is at a different
    offset in the two arms and resolving both from one assembly would read
    the knob arm's caret out of the middle of its strip.
    """
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        open(cp, "w").write(open(ROOT + "/apps/artful/artful.asm").read()
                            + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error"] + list(defines)
                       + ["-I", ROOT + "/apps/", "-I", ROOT + "/apps/artful/",
                          "-o", os.path.join(d, "p.bin"), cp], check=True)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[0], 16)
        return out


def drive(img, apps, machine, card, tree, census, shot=None):
    """Boot one tree, type DOC into a fullscreen ArtfulType, return the glass.

    Returns (w, h, splash rgb24, document rgb24, {symbol: entries}).

    TWO SCENES, because they exercise DIFFERENT writers into at_strip1. The
    fullscreen document is at_compose + at_glyph + at_ruleat; the windowed
    splash card is at_bigtext (the fifth writer, in this other file) and
    at_drawimg (the artwork, which is the one source of pixels nothing
    composes). Breaking at_bigtext on purpose with only the document scene
    captured left this row GREEN - which is why both are here.

    at_drawimg is skipped when at_vh < 300, so the artwork is covered by the
    herc and vga arms and not by cga.
    """
    tree.apply()                       # os88sym resolves against THIS kernel
    # THE DEFINE COMES OFF THE TREE'S MAKE ARGS, NOT ITS `defines`.
    # os88build's `defines` describes the KERNEL, and NOATBLIT1 reaches no
    # kernel byte (it is in the Makefile's KERN_KNOB exemption beside
    # NOHEDGE), so it is not in that list - the knob tree reports
    # `defines: KERN_BIG` exactly like the shipped one. Resolving the knob
    # arm's symbols without it read at_caret at the shipped arm's offset,
    # which is 58 bytes along and inside another variable: the wait then sat
    # out its whole guest budget waiting for a number that could not arrive.
    syms = pkg_syms(["-D" + a.split("=")[0] for a in tree.args
                     if a.startswith("NOAT")])
    counts = {}
    with os88ui.boot(img, apps=apps, machine=machine) as ui:
        m = ui.m
        w = ui.path("B:/APPS/ARTFUL.O88")          # the splash card
        seg = u16(m.read(os88geom.winptr(m, w.i, ui.sym) +
                    os88geom.W_SEG, 2))
        ui.settle()
        sw, sh, splash = m.fbuf()                  # SCENE 1: the splash card
        if shot:
            os88marty.write_png_rgb(shot.replace(".png", "-splash.png"),
                                    sw, sh, splash)
        m.key("KeyN")                              # New -> fullscreen (46.5)
        # FREEZE THE BLINK BEFORE THE FIRST SETTLE. at_worker toggles an XOR
        # caret every 9 ticks, so a fullscreen ArtfulType NEVER stops
        # changing and `settle` spends its whole guest budget and raises. It
        # would also land in the capture as a difference between two arms
        # that merely sampled different phases. [at_drag] is at_worker's own
        # .gate (artful.asm:636) and nothing else reads it, so setting it is
        # exactly "no blink" and touches no drawing state.
        m.write(seg * 16 + syms["at_drag"], b"\x01")
        m.write(seg * 16 + syms["at_cphase"], b"\x00")
        ui.settle()
        caret = syms["at_caret"]

        def key1(send, what):
            """Send one keystroke and wait for the APP to take it.

            SELF-SYNCING, and it has to be: a counter of typed characters is
            only equal to at_caret while typing forward from the start, and
            every nav key, undo and menu command below moves the caret
            somewhere else. Counting then waits for a number that can never
            arrive and sits out the whole guest budget.

            The wait itself is the load-bearing part. type_text fires keys as
            fast as the debug server accepts them, and one ArtfulType
            keystroke is 50-190 ms of 4.77 MHz work, so the kernel's key queue
            overflows and kbd_ovflow drops what will not fit. The first run of
            this gate typed the document into BOTH arms and got two DIFFERENT
            garbled documents - the slow arm dropped more - and reported 4,214
            differing pixels, which reads exactly like a rendering bug.
            """
            before = m.readseg(seg, caret, 2)
            send()
            os88marty.until(
                m, lambda _: m.readseg(seg, caret, 2) != before,
                "ArtfulType to take " + what, poll=0.05, limit=120.0)

        def typec(ch):
            key1(lambda: m.type_text(ch), repr(ch))

        def typedoc():
            for i, line in enumerate(DOC):
                if i:
                    key1(lambda: m.key("Enter"), "Enter")
                for ch in line:
                    typec(ch)

        if census:
            for sym in COUNTED:
                counts[sym] = os88marty.bp_count(m, sym, typedoc)
                m.bp_exec()
                m.run()
        else:
            typedoc()
        # ...and an edit with text AFTER the caret, in the same paragraph.
        # The gap buffer puts its gap at the caret, so a relayout walk from
        # the paragraph's start reads the bytes before the caret out of the
        # LOW run and the bytes after it out of the HIGH one - and the high
        # run is the only thing that exercises SPEC.md 46.3.3's gap
        # arithmetic. Typing at the end of the document, which is all every
        # other scene does, never crosses it: breaking that arm on purpose
        # left this row green until this edit existed.
        for _ in range(20):
            key1(lambda: m.key("ArrowLeft"), "ArrowLeft")
        for ch in "MID":
            typec(ch)
        # ...and an edit with text after it ON LATER VISUAL LINES, which the
        # one above has not got: 20 characters back from the end of the
        # document is still the LAST visual line of its paragraph, so nothing
        # is pushed past a wrap and every line below it is already correct.
        # SPEC.md 46.4.11 narrows an APPEND to one line, and removing its
        # end-of-line test on purpose was GREEN until this existed - the two
        # ArrowUps put the caret two visual lines up a plain paragraph, where
        # typing pushes a word over a wrap and the lines after it change.
        for _ in range(2):
            key1(lambda: m.key("ArrowUp"), "ArrowUp")
        for ch in "QQQQ":
            typec(ch)
        ui.settle()
        w, h, rgb = m.fbuf()                       # SCENE 2: the document

        # --- SCENE 3: a document TALLER THAN THE VIEW, scrolled -------------
        # at_scroll_to is the only path 46.4.4 changes and neither scene above
        # reaches it: five short lines never overflow even CGA's 16-line
        # region. Enter is the cheap way to get there - one keystroke a line
        # against ~64 characters of filler - and PageUp then forces the
        # UPWARD arm, which is the one whose dy at_sumn negates.
        for _ in range(FILLER[card]):
            key1(lambda: m.key("Enter"), "Enter")
            typec("x")
        ui.settle()

        # PACE THE NAVIGATION TOO, and on the app's own state. A settle can
        # return before a PageUp has been taken - the screen is still while
        # the key sits in the queue - and the two arms then end in DIFFERENT
        # SCROLL STATES. That is not subtle in the capture and is not
        # obviously a harness fault either: the first run of this scene put
        # the thumb at the top of the shaft in one arm and the bottom in the
        # other, and reported 224 differing pixels in the gutter, which reads
        # exactly like a scroll-bar bug. `quiesce` is `settle` over a handful
        # of guest bytes instead of a framebuffer, which is what the two
        # words that actually decide this picture live in.
        def nav(key):
            m.key(key)
            os88marty.quiesce(
                m, lambda: m.readseg(seg, syms["at_top"], 2)
                + m.readseg(seg, caret, 2),
                what="ArtfulType's view to stop moving after " + key)

        nav("PageUp")
        nav("PageDown")
        ui.settle()
        w3, h3, scrolled = m.fbuf()
        if shot:
            os88marty.write_png_rgb(shot.replace(".png", "-scroll.png"),
                                    w3, h3, scrolled)

        # --- SCENE 4: ZOOM IN, through ArtfulType's OWN menu bar -----------
        # The zoom/mode change is one of ArtfulType's three whole-page
        # commands (SPEC.md 46.1) and the ONLY one that changes the geometry
        # every other wave's arithmetic is expressed in - at_lgeom's cell
        # width and row height both move, so at_maxtop, the wrap, the line
        # table and the scroll bar's travel all move with them. Nothing else
        # in this row exercises that.
        #
        # IT RUNS BEFORE THE UNDO AND THAT ORDER MATTERS: undo collapses this
        # document to one line, and a one-line document has [at_top] = 0, no
        # scroll bar and nothing to relayout, so a zoom there would test
        # almost none of the above.
        #
        # os88ui.menu_pick cannot reach this menu: it reads the KERNEL's menu
        # tables, and a fullscreen ArtfulType draws its own bar (SPEC.md 46.5).
        # That is a convenience layer, not a limit - the mouse driver takes
        # absolute coordinates, so the app's own bar is drivable by mirroring
        # its geometry, and this is the only way to reach any of ArtfulType's
        # fullscreen menu commands from a test.
        # The geometry is at_mcell's, mirrored here - the bar starts 8px in and
        # each title cell is 8*len + 16 - and at_mitem_at's, which is
        # (y - 21) / AT_ITEMH. The press-drag-release is the Mac idiom that
        # at_menu_track is written around, not a click.
        for ch in "zoom":
            typec(ch)
        ui.settle()
        VIEW_X = 8 + (8*4 + 16) + (8*4 + 16) + (8*5 + 16) + (8*4 + 16)//2
        ZIN_Y = 21 + 13*3 + 6           # Markdown, Writer, separator, Zoom In
        mo = os88mouse.Mouse(marty=m)
        mo.to(VIEW_X, 9)
        mo._edge(True)                  # press on View
        if not u16(m.readseg(seg, syms["at_menuon"], 2)) & 0xFF:
            raise SystemExit("atblit: the View menu did not open at x=%d - "
                             "at_mcell's geometry has moved" % VIEW_X)
        mo.to(VIEW_X, ZIN_Y, l=True)    # drag onto Zoom In
        mo._edge(False)                 # ...and release: that is the command
        os88marty.quiesce(
            m, lambda: m.readseg(seg, syms["at_zoom"], 2)
            + m.readseg(seg, syms["at_top"], 2)
            + m.readseg(seg, caret, 2),
            what="ArtfulType to finish the zoom")
        if not u16(m.readseg(seg, syms["at_zoom"], 2)) & 0xFF:
            raise SystemExit("atblit: [at_zoom] is still 0 - the Zoom In item "
                             "was not chosen, so this scene tested nothing")
        ui.settle()
        # CAPTURE WHILE ZOOMED IN, WITH A PLAIN LINE ON THE GLASS. Every
        # other scene is at zoom 0, where at_cellwtab makes a body cell 8px
        # and every plain line scale 1; zoom 1 makes it 16, so a plain body
        # line is SCALE 2 - the one state in which at_parse's .rloop runs
        # with at_psc != 1, and the only thing that can witness SPEC.md
        # 46.4.9's scale guard.
        #
        # PAGING TO A PLAIN LINE IS THE LOAD-BEARING HALF. Capturing right
        # here renders the caret's own line, which after the scroll scene is
        # the delimiter-per-four-characters one - STYLED, so at_parse clears
        # the flag and the fast composer is never entered. Removing the
        # guard on purpose was GREEN with the zoom capture alone: the
        # picture cannot show a bug in a path the scene does not take. The
        # document's TAIL is the filler, one `x` a line, which is plain at
        # every zoom.
        #
        # IT IS A SEARCH AND NOT A COUNT, for two reasons. The three
        # adapters fit different numbers of rows and carry different filler
        # counts, so "page down N times" lands somewhere different on each;
        # and the very bottom is one line SHORT of useful, because at_layout
        # emits the trailing empty logical line through at_emitend without
        # at_mkplain, so its attr is 0 and a view scrolled to at_maxtop
        # shows that line and nothing else.
        #
        # THE WITNESS IS at_lattr AND NOT [at_pplain], which looks like the
        # obvious reading and is confounded: SPEC.md 46.4.8 lets at_caret_on
        # SKIP at_parse on a plain line, so the last parse of a repaint is
        # whichever STYLED line was drawn last and the flag reads 0 with a
        # perfectly good plain line on the glass. Bit 3 of a line's attr is
        # the same fact stated where nothing can overwrite it.
        def band():
            """The attrs of the visual lines currently on the glass."""
            top = u16(m.readseg(seg, syms["at_top"], 2))
            nl = u16(m.readseg(seg, syms["at_nlines"], 2))
            rows = ((u16(m.readseg(seg, syms["at_ty1"], 2))
                     - u16(m.readseg(seg, syms["at_ty0"], 2)))
                    // max(1, u16(m.readseg(seg, syms["at_prh"], 2))))
            n = max(1, min(rows, nl - top))
            return top, nl, m.readseg(seg, syms["at_lattr"] + top, n)

        # THE SEARCH MASKS BIT 3 OUT AND THE ASSERTION DOES NOT, because the
        # two have to answer different questions. The search must pick the
        # SAME page on both arms or they end up comparing different views -
        # and NOATPLAIN=1 compiles 46.4.8's flag away entirely, so bit 3 is
        # never set there and a search on it pages to the end and raises. The
        # rest of the attr is knob-independent: level 0, no continuation and a
        # zero span nibble is what the `x` filler lines are on either arm.
        for _ in range(6):
            nav("PageDown")                 # ...to a known end, then back up
        for _ in range(10):
            top, nl, attrs = band()
            if any(not (b & 0xF7) for b in attrs):
                break
            nav("PageUp")
        else:
            raise SystemExit(
                "atblit: zoomed in at psc=%d, no page of this document has an "
                "unstyled body line on it - the last band tried was %d of %d, "
                "attrs %s. Rendering a plain line at a scale above 1 is the "
                "whole point of this scene."
                % (u16(m.readseg(seg, syms["at_psc"], 2)), top, nl,
                   " ".join("%02x" % b for b in attrs)))
        # ...and on an arm that HAS the flag, one of them must really be plain
        # in SPEC.md 46.4.8's sense - which is the state 46.4.9's scale guard
        # is about, and the thing the mask above cannot prove.
        if "NOATPLAIN=1" not in tree.args and not any(b & 8 for b in attrs):
            raise SystemExit(
                "atblit: zoomed in, the visible band %d of %d is attrs %s - "
                "not one of them carries 46.4.8's plain bit, so at_compose's "
                "fast path is not on the glass"
                % (top, nl, " ".join("%02x" % b for b in attrs)))
        if u16(m.readseg(seg, syms["at_psc"], 2)) == 1:
            raise SystemExit("atblit: zoomed in and at_psc is still 1 - "
                             "at_cellwtab's zoom row has moved and this "
                             "scene is testing scale 1 twice")
        ui.settle()
        w6, h6, zoomin = m.fbuf()
        if shot:
            os88marty.write_png_rgb(shot.replace(".png", "-zoomin.png"),
                                    w6, h6, zoomin)

        # ...and back OUT again, which is the other half of the geometry
        # change and the one that puts the row back in a comparable state:
        # the lines shrink, more of them fit, at_maxtop grows and the view
        # stays scrolled. Capturing after the round trip is what makes the
        # scene assert that a zoom is REVERSIBLE - a wave that got at_lgeom's
        # arithmetic subtly wrong would come back to a different picture.
        mo.to(VIEW_X, 9)
        mo._edge(True)
        mo.to(VIEW_X, 21 + 13 * 4 + 6, l=True)      # Zoom Out
        mo._edge(False)
        os88marty.quiesce(
            m, lambda: m.readseg(seg, syms["at_zoom"], 2)
            + m.readseg(seg, syms["at_top"], 2)
            + m.readseg(seg, caret, 2),
            what="ArtfulType to finish the zoom out")
        if u16(m.readseg(seg, syms["at_zoom"], 2)) & 0xFF:
            raise SystemExit("atblit: [at_zoom] is still set - Zoom Out was "
                             "not chosen, so the already-visible exit was "
                             "never taken and this scene tested nothing")
        if not u16(m.readseg(seg, syms["at_top"], 2)):
            raise SystemExit(
                "atblit: [at_top] is 0 after a zoom out that should have left "
                "the view scrolled. EITHER the view jumped to the top of the "
                "document, which is a regression in whatever wave is under "
                "test, OR the scene has drifted and no longer reaches a "
                "scrolled state, in which case it tests much less than it "
                "looks like it does. /tmp/atblit/*-zoom.png tells them "
                "apart.")
        ui.settle()
        w5, h5, zoomed = m.fbuf()
        if shot:
            os88marty.write_png_rgb(shot.replace(".png", "-zoom.png"),
                                    w5, h5, zoomed)
        if shot:
            os88marty.write_png_rgb(shot, w, h, rgb)
        # --- SCENE 5: UNDO ------------------------------------------------
        # Undo is the third of ArtfulType's whole-page commands (SPEC.md
        # 46.1) and the only one that RESTORES a document rather than
        # relaying out the live one - at_snap_take's arena, not at_layout - so
        # it is the one path where the line table and the gap buffer can
        # disagree. Ctrl+Z is the cheapest way in (at_ctltab: 26 ->
        # AT_CMD_UNDO).
        #
        # THE SECOND TYPING RUN IS WHAT KEEPS IT INTERESTING. Undoing the
        # FIRST run collapses the document to one line, and a one-line
        # document has no scroll bar, no wrap and nothing below the caret.
        # The nav keys above closed the first run ([at_typrun]), so this is a
        # run of its own and undoing it leaves the document tall.
        for ch in "abc":
            typec(ch)
        ui.settle()
        m.ctrl("KeyZ")
        os88marty.quiesce(
            m, lambda: m.readseg(seg, syms["at_top"], 2)
            + m.readseg(seg, caret, 2)
            + m.readseg(seg, syms["at_nlines"], 2),
            what="ArtfulType to finish the undo")
        ui.settle()
        w4, h4, undone = m.fbuf()
        if shot:
            os88marty.write_png_rgb(shot.replace(".png", "-undo.png"),
                                    w4, h4, undone)

    return w, h, splash, rgb, scrolled, zoomin, zoomed, undone, counts


def diff(a, b, w, h, y0=0):
    """Differing pixels from row y0 down, and their bounding box.

    The two FULLSCREEN scenes compare the whole screen and that is safe:
    ArtfulType owns every pixel there, so the menu bar is its own and carries
    no clock, and os88ui.boot parks the pointer and turns the saver off.

    THE SPLASH SCENE IS WINDOWED and does not get that. The kernel's desktop
    menu bar is on screen with its running CLOCK in the top right, so a
    whole-screen compare asks two boots to agree about the time - which they
    do until they happen to straddle a minute, and then it reports ~31 pixels
    at (624,6) and reads like a rendering bug. y0 = MBAR_H is why that scene
    starts below it; the splash card is far below the bar either way.
    """
    n, box = 0, None
    for row in range(y0, h):
        base = row * w * 3
        if a[base:base + w * 3] == b[base:base + w * 3]:
            continue
        for col in range(w):
            i = base + col * 3
            if a[i:i+3] != b[i:i+3]:
                n += 1
                box = ((min(box[0], col), min(box[1], row),
                        max(box[2], col), max(box[3], row))
                       if box else (col, row, col, row))
    return n, box


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", default="cga", choices=sorted(CARDS))
    ap.add_argument("--knob", default="NOATBLIT1",
                    help="which ArtfulType A/B to run: the shipped package "
                         "against this knob's arm. NOATBLIT1 is 46.4.2's "
                         "band emit, NOATFAST is 46.4.3's scale-1 composer. "
                         "Every one of them must draw the IDENTICAL picture, "
                         "so this row generalises rather than being copied "
                         "per wave.")
    ap.add_argument("--census", action="store_true",
                    help="COUNT the two blits INSTEAD of comparing pixels. "
                         "The two are not additive: bp_count stops the guest "
                         "at every hit and the caret pacing needs the guest "
                         "RUNNING to make progress, so together they type a "
                         "different document into each arm and the compare "
                         "is meaningless - it reported 3,506 differing "
                         "pixels the one time they were run together. Pixels "
                         "are the gate; this is the reading you take by hand.")
    a = ap.parse_args()
    machine = CARDS[a.card]

    shipped = os88build.plain()
    knob = os88build.tree(a.knob + "=1")
    print("   shipped arm: %s" % os.path.relpath(shipped.dir, ROOT))
    print("   %s arm: %s" % (a.knob, os.path.relpath(knob.dir, ROOT)))

    os.makedirs("/tmp/atblit", exist_ok=True)
    w, h, bsp, band, bsc, bzi, bzm, bun, cb = drive(shipped.img("os8088-360.img"),
                                shipped.img("apps360.img"), machine, a.card,
                                shipped,
                                a.census, "/tmp/atblit/%s-shipped-%s.png" % (a.knob, a.card))
    w2, h2, esp, expa, esc, ezi, ezm, eun, ce = drive(knob.img("os8088-360.img"),
                                  knob.img("apps360.img"), machine, a.card,
                                  knob,
                                  a.census,
                                  "/tmp/atblit/%s-knob-%s.png" % (a.knob, a.card))

    if (w, h) != (w2, h2):
        print("FAIL: the two arms rasterised %dx%d against %dx%d"
              % (w, h, w2, h2))
        return 1

    if a.census:
        print("\n   %s, per document (%d lines):" % (a.card, len(DOC)))
        for sy in COUNTED:
            print("      %-10s band=%-6s expand=%s" % (sy, cb.get(sy),
                                                      ce.get(sy)))
        print("   (pixels NOT compared - see --census's help)")
        return 0

    bad = 0
    MBAR_H = 20                      # the kernel's desktop bar (SPEC.md 12)
    for scene, x, y, y0 in (("splash", bsp, esp, MBAR_H),
                            ("document", band, expa, 0),
                            ("scrolled", bsc, esc, 0),
                            ("zoomed-in", bzi, ezi, 0),
                            ("zoomed-out", bzm, ezm, 0),
                            ("undone", bun, eun, 0)):
        n, box = diff(x, y, w, h, y0)
        print("   %s/%s %-9s %d differing pixels of %d%s"
              % (a.knob, a.card, scene, n, w * h,
                 "" if not box else "  box %r" % (box,)))
        bad += n
    n = bad
    if n:
        print("   per-row (row: differing px):")
        for row in range(h):
            base = row * w * 3
            if band[base:base + w*3] == expa[base:base + w*3]:
                continue
            c = sum(1 for col in range(w)
                    if band[base+col*3:base+col*3+3]
                    != expa[base+col*3:base+col*3+3])
            print("      y=%-4d %d" % (row, c))
        print("   captures in /tmp/atblit/")
        print("FAIL: the shipped arm and %s drew different pictures" % a.knob)
        return 1
    print("ok: %s draws the identical picture on %s" % (a.knob, a.card))
    return 0


if __name__ == "__main__":
    sys.exit(main())
