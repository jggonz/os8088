#!/usr/bin/env python3
"""ARTFULTYPE'S PULL-DOWN PUTS BACK EXACTLY WHAT IT COVERED (SPEC.md 46.5.1).

    make && python3 tests/atmenusu.py [--card cga|herc|vga]

ArtfulType is fullscreen and draws its own menu bar (SPEC.md 46.5), so it owns
the dismissal too.  It used to spell that as a piecewise repaint: erase the
panel, then `at_draw_line` every text line it had covered - and a line is drawn
FULL WIDTH, all `[at_tw]` of it, not the panel's ~120px.  Measured at 104.9 ms
on a 4.77MHz 8088 against 14.5 for a write-back.

THE ASSERTION IS PIXEL EQUALITY, and it is the only one worth making.  A
save-under that is fast and wrong is worse than a repaint that is slow and
right, and every way of getting it wrong shows up here: banking the panel
WITHOUT its drop shadow leaves a grey L on the glass, clamping the rect
differently from the erase leaves a column, and taking the plane count from the
wrong display leaves colour noise on a two-card machine.  So: photograph the
screen, open a menu, dismiss it WITHOUT PICKING, photograph again, and require
ZERO differing pixels.

DISMISSING WITHOUT PICKING IS THE LOAD-BEARING PART.  ArtfulType's menus are
press-drag-release (SPEC.md 43's `sol_drag` idiom), and every item runs a
command that repaints the screen - which would hide any error the restore made.
Releasing while still over the TITLE leaves `[at_mhil]` = -1, which
`at_menu_track` closes on without dispatching anything.

It is also the A/B for the fallback.  `--repaint` pokes `[at_suseg]` = 0 while
the panel is down, which is exactly what a refused claim leaves behind, and the
close must then take the repaint and land on the SAME pixels.  One run
therefore checks both paths against one reference.
"""
import sys, os, argparse, subprocess, tempfile

sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import os88fixture                                       # noqa: E402
import os88marty, os88ui, os88build, os88geom            # noqa: E402
import os88mouse                                         # noqa: E402

ROOT = "/home/user/os8088"
CARDS = {"cga":  "os8088_5150_cga_gla",
         "herc": "os8088_5150_herc_gla",
         "vga":  "os8088_xt_vga"}
FAIL = []


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def check(name, ok, detail=""):
    print("   %-52s %s%s" % (name, "ok" if ok else "FAIL",
                             "" if ok else "  " + detail))
    if not ok:
        FAIL.append(name)


def pkg_syms():
    """ArtfulType's bss offsets, by re-assembling it - atblit's trick."""
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        open(cp, "w").write(open(ROOT + "/apps/artful/artful.asm").read()
                            + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error",
                        "-I", ROOT + "/apps/", "-I", ROOT + "/apps/artful/",
                        "-o", os.path.join(d, "p.bin"), cp], check=True)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[0], 16)
        return out


def diff(a, b, w, h):
    """Differing pixels and their bounding box, over the WHOLE screen.

    The whole screen is safe and is the point: ArtfulType is fullscreen, so
    every pixel is its own - there is no kernel bar with a running clock to
    make two photographs of a still machine disagree, which is the trap the
    windowed scenes in atblit have to dodge.  The pointer is parked at the same
    place for both, so the arrow is in both or in neither.
    """
    n, box = 0, None
    for row in range(h):
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
    ap.add_argument("--card", default="herc", choices=sorted(CARDS))
    ap.add_argument("--shot", default="")
    a = ap.parse_args()

    tree = os88build.plain()
    tree.apply()
    syms = pkg_syms()

    with os88ui.boot(tree.img("os8088-360.img"), apps=tree.img("apps360.img"),
                     machine=CARDS[a.card]) as ui:
        m = ui.m
        w = ui.path("B:/APPS/ARTFUL.O88")
        seg = u16(m.read(os88geom.winptr(m, w.i, ui.sym) + os88geom.W_SEG, 2))
        ui.settle()
        m.key("KeyN")                          # New -> fullscreen (46.5)
        # Freeze the blink before the first settle, exactly as atblit does:
        # a fullscreen ArtfulType otherwise never stops changing.
        m.write(seg * 16 + syms["at_drag"], b"\x01")
        m.write(seg * 16 + syms["at_cphase"], b"\x00")
        ui.settle()
        caret = syms["at_caret"]

        def key1(send, what):
            before = m.readseg(seg, caret, 2)
            send()
            os88marty.until(m, lambda _: m.readseg(seg, caret, 2) != before,
                            "ArtfulType to take " + what, poll=0.05, limit=120.0)

        # A PAGE OF TEXT, because that is what makes the difference visible:
        # over an empty document the panel covers nothing and a broken restore
        # puts back white on white.
        for i in range(22):
            if i:
                key1(lambda: m.key("Enter"), "Enter")
            for ch in "the quick brown fox jumps over the lazy dog":
                key1(lambda c=ch: m.type_text(c), repr(ch))
        ui.settle()

        rw = lambda n: u16(m.readseg(seg, syms[n], 2))
        vw, vh = rw("at_vw"), rw("at_vh")
        print("   %s: %dx%d, bpp %d, %d lines"
              % (a.card, vw, vh, m.readseg(seg, syms["at_vbpp"], 1)[0],
                 rw("at_nlines")))

        # The View menu's title cell, mirrored from at_mcell the way atblit
        # does: the bar starts 8px in and each cell is 8*len + 16.
        VIEW_X = 8 + (8*4 + 16) + (8*4 + 16) + (8*5 + 16) + (8*4 + 16)//2
        PARK = (vw - 24, vh - 24)              # off the panel, on the text area
        mo = os88mouse.Mouse(marty=m)

        mo.to(*PARK)
        ui.settle()
        before = m.fbuf()
        if a.shot:
            os88marty.write_png_rgb(a.shot.replace(".png", "-before.png"),
                                    *before)

        def cycle(poke_refuse):
            mo.to(VIEW_X, 9)
            mo._edge(True)                     # press: the panel drops
            os88marty.quiesce(
                m, lambda: m.readseg(seg, syms["at_menuon"], 2),
                what="the pull-down to open")
            opened = m.readseg(seg, syms["at_menuon"], 1)[0]
            banked = rw("at_suseg")
            if poke_refuse:                    # what a REFUSED claim leaves
                m.write(seg * 16 + syms["at_suseg"], b"\x00\x00")
            mo._edge(False)                    # release ON THE TITLE: no pick
            os88marty.quiesce(
                m, lambda: m.readseg(seg, syms["at_menuon"], 2)
                + m.readseg(seg, caret, 2),
                what="the pull-down to close")
            mo.to(*PARK)
            ui.settle()
            return opened, banked, m.fbuf()

        op, bk, after = cycle(False)
        check("the pull-down opened", op == 1, "at_menuon=%d" % op)
        check("the drop BANKED its pixels", bk != 0,
              "at_suseg=0 - the claim or the save refused")
        n, box = diff(before[2], after[2], vw, vh)
        if a.shot:
            os88marty.write_png_rgb(a.shot.replace(".png", "-after.png"), *after)
        check("save-under restores the screen EXACTLY", n == 0,
              "%d differing px, box %r" % (n, box))

        op2, bk2, after2 = cycle(True)
        check("the pull-down opened again", op2 == 1, "at_menuon=%d" % op2)
        n2, box2 = diff(before[2], after2[2], vw, vh)
        if a.shot:
            os88marty.write_png_rgb(a.shot.replace(".png", "-fallback.png"),
                                    *after2)
        check("the REPAINT fallback lands on the same pixels", n2 == 0,
              "%d differing px, box %r" % (n2, box2))

    print()
    print("atmenusu: %s" % ("FAILED: " + ", ".join(FAIL) if FAIL else "ok"))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
