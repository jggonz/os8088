#!/usr/bin/env python3
"""WHAT ONE ARTFULTYPE KEYSTROKE COSTS, on the machine it was written for.

    make && python3 tests/atkey.py                  # CGA, both arms
    python3 tests/atkey.py --card herc
    python3 tests/atkey.py --card vga --lines 6

Nothing in this repository has ever measured ArtfulType. SPEC.md 46.1 states
a performance CONTRACT - a keystroke is a gap-buffer store, a one-paragraph
relayout and the repaint of that paragraph's visual lines - and every
millisecond attached to it, here and in docs/plans/ARTFUL-PERF-PLAN.md, was
PREDICTED from unit rates. This is the row that turns them into readings.
CLAUDE.md rule 4: measure before redesigning, and a counter is not a timer.

It brackets `at_onkey` from its entry to its RETURN, in guest CYCLES off
MartyPC's counter, and divides by 4,772,727. The return address is read off
the stack rather than assumed - `at_onkey` is a W_ONKEY callback reached
through the package's own three-byte dispatcher (SPEC.md 20), so its `ret` is
NEAR and the address on the stack at entry is the dispatcher's.

WHAT IT MEASURES IS A SCENE, NOT A KEY. `at_apply_edit` repaints
[at_dfrom .. at_dfrom+at_rlk-1], and `at_relayout` sets `at_dfrom` from
`at_lhome` - the edited PARAGRAPH's first visual line. So the answer depends
entirely on how many visual lines the caret's paragraph has, which is why
--lines exists and why the row prints the scene it measured. A number without
its paragraph length is not a number.

It also reads [at_rlk] and [at_dfrom] back after the keystroke, which is the
plan's W0.5: it turns 46.1's honest but easily-misread "that paragraph's
visual lines" into a table.
"""
import sys, os, argparse, subprocess, tempfile
sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import os88fixture                                       # noqa: E402
import os88marty, os88ui, os88build, os88geom            # noqa: E402
import os88mouse                                         # noqa: E402

ROOT = "/home/user/os8088"
HZ = 4772727.0                       # the 5150's 8088

CARDS = {"cga":  "os8088_5150_cga_gla",
         "herc": "os8088_5150_herc_gla",
         "vga":  "os8088_xt_vga"}


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def pkg_syms(defines=()):
    """ArtfulType's labels and bss offsets, by re-assembling it."""
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


def measure(img, apps, machine, tree, lines, samples, scroll=False,
            key=None, ctrl=None, zoom=False, heading=False):
    tree.apply()
    syms = pkg_syms(["-D" + a.split("=")[0] for a in tree.args
                     if a.startswith("NOAT")])
    with os88ui.boot(img, apps=apps, machine=machine) as ui:
        m = ui.m
        w = ui.path("B:/APPS/ARTFUL.O88")
        seg = u16(m.read(os88geom.winptr(m, w.i, ui.sym) + os88geom.W_SEG, 2))
        ui.settle()
        m.key("KeyN")
        m.write(seg * 16 + syms["at_drag"], b"\x01")     # freeze the blink
        m.write(seg * 16 + syms["at_cphase"], b"\x00")
        ui.settle()

        caret = syms["at_caret"]

        def absorbed(n):
            os88marty.until(
                m, lambda _: u16(m.readseg(seg, caret, 2)) == n,
                "ArtfulType to absorb %d characters" % n, poll=0.05,
                limit=120.0)

        # --- the ZOOM scene: the one command that double-draws the page ----
        # A Zoom In is the whole-page command that changes the GEOMETRY -
        # at_lgeom's cell width and row height both move, and at_maxtop, the
        # wrap, the line table and the scroll bar's travel move with them.
        # Driven through the app's
        # OWN menu bar - os88ui.menu_pick reads the kernel's tables and a
        # fullscreen ArtfulType draws its own (46.5) - with at_mcell's
        # geometry mirrored here and at_menu_track's press-drag-release idiom.
        top = syms["at_top"]
        if zoom:
            want = 0
            for _ in range(24):
                m.key("Enter")
                want += 1
                absorbed(want)
                m.type_text("x")
                want += 1
                absorbed(want)
            ui.settle()
            VIEW_X = 8 + (8*4 + 16) + (8*4 + 16) + (8*5 + 16) + (8*4 + 16)//2
            mo = os88mouse.Mouse(marty=m)
            onkey = seg * 16 + syms["at_onkey"]
            out = []
            for i in range(samples):
                item = 3 if i % 2 == 0 else 4        # Zoom In / Zoom Out
                mo.to(VIEW_X, 9)
                mo._edge(True)
                mo.to(VIEW_X, 21 + 13 * item + 6, l=True)
                m.bp_exec(onkey)
                m.run()
                mo._edge(False)                      # the release IS the command
                if not m.wait_stop(limit=90.0):
                    sys.exit("atkey: at_onkey never ran after the menu pick")
                r = m.regs()
                ret = u16(m.read((r["ss"] << 4) + r["sp"], 2))
                m.bp_exec(seg * 16 + ret)
                c0 = m.status()["cycles"]
                m.run()
                if not m.wait_stop(limit=300.0):
                    sys.exit("atkey: at_onkey never returned from the zoom")
                out.append(m.status()["cycles"] - c0)
                m.bp_exec()
                m.run()
                os88marty.quiesce(
                    m, lambda: m.readseg(seg, syms["at_zoom"], 2)
                    + m.readseg(seg, top, 2) + m.readseg(seg, caret, 2),
                    what="ArtfulType to finish the zoom")
            return (out, u16(m.readseg(seg, syms["at_rlk"], 2)),
                    u16(m.readseg(seg, syms["at_dfrom"], 2)),
                    u16(m.readseg(seg, syms["at_nlines"], 2)))

        # --- the SCROLL scene: a document taller than the view -------------
        # 46.4.4 changes at_scroll_to and nothing a character types reaches
        # it. Enter is the cheap way to a tall document - one keystroke a
        # line against ~64 of filler - and the bracketed key is then a
        # PageDown, whose move is the whole view and so was on the wrong
        # side of the old three-line bound by construction.
        if scroll:
            want = 0
            for _ in range(24):
                m.key("Enter")
                want += 1
                absorbed(want)
                m.type_text("x")
                want += 1
                absorbed(want)
            onkey = seg * 16 + syms["at_onkey"]
            top = syms["at_top"]
            out, moved = [], []
            for _ in range(samples):
                # RESET, THEN SPEND THE FIRST PAGEDOWN UNBRACKETED. From the
                # top of the document the first PageDown only moves the CARET
                # to the bottom of the view - at_seecaret finds it already
                # visible and at_scroll_to is never called - so bracketing
                # that one measures a caret move and calls it a scroll. The
                # SECOND is the one that has to move [at_top].
                def nav(k):
                    m.key(k)
                    os88marty.quiesce(
                        m, lambda: m.readseg(seg, top, 2)
                        + m.readseg(seg, caret, 2),
                        what="the view to stop moving after " + k)
                for _ in range(6):
                    nav("PageUp")
                nav("PageDown")
                t0 = u16(m.readseg(seg, top, 2))
                m.bp_exec(onkey)
                m.run()
                m.key("PageDown")
                if not m.wait_stop(limit=60.0):
                    sys.exit("atkey: at_onkey never ran after PageDown")
                r = m.regs()
                ret = u16(m.read((r["ss"] << 4) + r["sp"], 2))
                m.bp_exec(seg * 16 + ret)
                c0 = m.status()["cycles"]
                m.run()
                if not m.wait_stop(limit=300.0):
                    sys.exit("atkey: at_onkey never returned from PageDown")
                out.append(m.status()["cycles"] - c0)
                m.bp_exec()
                m.run()
                ui.settle()
                moved.append(u16(m.readseg(seg, top, 2)) - t0)
            print("      [at_top] moved by %s lines" % moved)
            return (out, u16(m.readseg(seg, syms["at_rlk"], 2)),
                    u16(m.readseg(seg, syms["at_dfrom"], 2)),
                    u16(m.readseg(seg, syms["at_nlines"], 2)))

        # --- build the scene: one paragraph of `lines` visual lines --------
        # No Enter anywhere: a paragraph is what at_lhome backs up to, so a
        # WRAPPED run of text is the thing under test and a run of short
        # lines would measure at_rlk = 1 and call it a keystroke.
        # A HEADING IS HOW YOU MEASURE SCALE 2 AT ZOOM 0. at_cellwtab makes an
        # H1 cell 16px wide, so the whole paragraph goes through at_glyph's
        # GENERAL arm - which is what SPEC.md 46.4.5 changes and what 46.4.3's
        # straight-line emitter deliberately does not touch.
        text = (("# " if heading else "")
                + "the quick brown fox jumps over the lazy dog and keeps on "
                  "running well past the right margin " * 8)
        want = 0
        percell = {"cga": 64, "herc": 74, "vga": 64}[a_card]
        if heading:
            percell //= 2                       # a 16px cell, half the count
        target = max(1, (lines - 1)) * percell + percell // 2
        for ch in text[:target]:
            m.type_text(ch)
            want += 1
            absorbed(want)
        ui.settle()

        # --- and now bracket ONE more keystroke ---------------------------
        onkey = seg * 16 + syms["at_onkey"]
        out = []
        for _ in range(samples):
            m.bp_exec(onkey)
            m.run()
            if ctrl:
                m.ctrl(ctrl)
            elif key:
                m.key(key)
            else:
                m.type_text("x")
            if not m.wait_stop(limit=60.0):
                sys.exit("atkey: at_onkey never ran within 60s of a keypress")
            r = m.regs()
            ret = u16(m.read((r["ss"] << 4) + r["sp"], 2))
            m.bp_exec(seg * 16 + ret)
            c0 = m.status()["cycles"]
            m.run()
            if not m.wait_stop(limit=180.0):
                sys.exit("atkey: at_onkey never returned")
            out.append(m.status()["cycles"] - c0)
            m.bp_exec()
            m.run()
            # RESYNC rather than increment: the bracketed key is not always a
            # printable. Ctrl+Z and PageDown both MOVE the caret, so
            # `want += 1` then waits for a number that can never arrive and
            # sits out its whole guest budget.
            os88marty.quiesce(
                m, lambda: m.readseg(seg, caret, 2)
                + m.readseg(seg, syms["at_top"], 2),
                what="ArtfulType to finish the keystroke")
            want = u16(m.readseg(seg, caret, 2))
        rlk = u16(m.readseg(seg, syms["at_rlk"], 2))
        dfrom = u16(m.readseg(seg, syms["at_dfrom"], 2))
        nlines = u16(m.readseg(seg, syms["at_nlines"], 2))
    return out, rlk, dfrom, nlines


def main():
    global a_card
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", default="cga", choices=sorted(CARDS))
    ap.add_argument("--lines", type=int, default=3,
                    help="visual lines in the caret's paragraph - the thing "
                         "the answer actually depends on")
    ap.add_argument("--samples", type=int, default=3)
    ap.add_argument("--key",
                    help="bracket this MartyKey instead of a printable. "
                         "Enter is the one that matters for the scroll bar: a "
                         "printable keystroke leaves the line count alone and "
                         "takes at_apply_edit's .rng arm, which never calls "
                         "at_sbar at all - only the line-count-change arms do.")
    ap.add_argument("--ctrl",
                    help="bracket this key with Control held - Ctrl+Z is "
                         "AT_CMD_UNDO, one of 46.4.5's three whole-page tails")
    ap.add_argument("--heading", action="store_true",
                    help="measure inside an H1, whose 16px cell puts every "
                         "character on at_glyph's GENERAL arm (scale 2)")
    ap.add_argument("--zoom", action="store_true",
                    help="bracket a ZOOM IN chosen from the app's own menu "
                         "bar - the one command that pushes the caret "
                         "off-view and so makes 46.4.5's tail draw twice")
    ap.add_argument("--scroll", action="store_true",
                    help="bracket a PAGEDOWN on a document taller than the "
                         "view, instead of a character in a paragraph")
    ap.add_argument("--knob", default="NOATBLIT1",
                    help="the arm to compare the shipped package against")
    ap.add_argument("--no-knob", action="store_true",
                    help="measure the shipped package only")
    a = ap.parse_args()
    a_card = a.card
    machine = CARDS[a.card]

    arms = [("shipped", os88build.plain())]
    if not a.no_knob:
        arms.append(("%s=1" % a.knob, os88build.tree(a.knob + "=1")))

    res = {}
    for name, t in arms:
        cyc, rlk, dfrom, nlines = measure(t.img("os8088-360.img"),
                                          t.img("apps360.img"), machine, t,
                                          a.lines, a.samples, a.scroll,
                                          a.key, a.ctrl, a.zoom, a.heading)
        res[name] = cyc
        best = min(cyc)
        print("   %-22s %s cycles  -> %.1f ms  "
              "[at_rlk=%d at_dfrom=%d at_nlines=%d]"
              % (name, "/".join(str(c) for c in cyc), best / HZ * 1000.0,
                 rlk, dfrom, nlines))

    if len(res) == 2:
        a1, a2 = [min(res[n]) for n, _ in arms]
        print("\n   %s: one %s is %.1f ms against %.1f - %.2fx"
              % (a.card, "PageDown" if a.scroll else "keystroke",
                 a1 / HZ * 1000.0, a2 / HZ * 1000.0, a2 / float(a1)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
