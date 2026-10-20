#!/usr/bin/env python3
"""ARTFULTYPE'S MINIMIZE BOX AND ITS SYSTEM CLIPBOARD (SPEC.md 46.5.2, 46.6.1).

    make && python3 tests/atclip.py [--card cga|herc|vga]

Two capabilities, one boot, because both of them are reached only from the
FULLSCREEN surface and getting there is the expensive part.

THE BOX (SPEC.md 46.5.2, 46.5.3).  A `WF_FULL` window has no chrome - `wm_hit`
answers AL=0 for every point of one - and ArtfulType cannot bind `f` either,
because pressing F in a writer writes an f (SPEC.md 11.2.1).  So before this
there was no way out of the writing surface with the MOUSE at all, and the box
in the bar's right end is it.  It goes to the DOCK and comes back to the
DOCUMENT: the window is an intro dialog, so landing on it either way is landing
on no ArtfulType at all.

Three assertions carry that, and the middle one is the one worth having.  The
box must clear `[at_fs]` and a click a few pixels to its LEFT must not - a
hit-test that is really "anywhere top-right" passes the first alone.  Then
`I_FLAGS` bit 0 must be SET, because a plain `OSAPI_WM_HIDE` passes every other
check on this page and leaves a live-looking tile whose click goes to
`wm_front`, which never shows an invisible window (SPEC.md 29.6) - a docked
instance with no way back, and nothing but that bit tells the two apart.  Then
the tile must restore straight to fullscreen rather than to the splash.

AND THE CLOSE BOX (SPEC.md 46.7.1).  New, Open and Quit always asked about
unsaved changes; the close box did not, and lost the document silently.  It is
tested from the SPLASH because that is the only place it exists - the same
"no chrome" fact one step on.

THE CLIPBOARD (SPEC.md 46.6.1).  ArtfulType used to carry a buffer of its own,
which is the one thing SPEC.md 55's opening sentence says the machine's
clipboard is FOR.  Proving it is the system's needs the kernel's own words
read and written, and nothing else will do it: "copy then paste in the same
program" is a test a private buffer passes.

So the copy half reads `clip_seg`/`clip_bytes` OUT OF THE KERNEL and compares
the text there against what was typed here.  The paste half then OVERWRITES
that claim from the host with bytes ArtfulType has never seen - a payload with
a tab, a CR LF, a lone CR and a control byte in it - so a paste that produces
them can only have read the kernel's buffer, and the FOLD (SPEC.md 46.6.1:
at_load_named's rule, because the bytes came from another program) is asserted
on the same pass.  The length is kept inside the claim's rounded KB, which is
what makes poking it legitimate rather than lucky.

AND THE FORMATTING (SPEC.md 46.6.2).  A paste whose text ends `**B**` lands the
caret at the end of a delimiter run, which is exactly what 46.2.2 reveals while
you are TYPING - so the naive answer draws that trailing `**` and the document
does not come out formatted.  `[at_nrev]` is the distinction.

The assertion is an A/B ON THE GLASS rather than a state read, because the
claim is about pixels: photograph the pasted document, then type one character
and take it straight back.  Both keystrokes clear the latch on the way in, so
the document and the caret end EXACTLY where they were and the only thing that
has changed is whether the delimiters are on the screen.  The two photographs
must DIFFER.  Same bytes, same caret, two pictures - which is what "it redraws
as formatted" means, and no residual parse state is read to get there.
"""
import sys, os, argparse, subprocess, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # this tree, wherever it is checked out
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88fixture                                       # noqa: E402
import os88marty, os88ui, os88build, os88geom            # noqa: E402
import os88mouse                                         # noqa: E402

CARDS = {"cga":  "os8088_5150_cga_gla",
         "herc": "os8088_5150_herc_gla",
         "vga":  "os8088_xt_vga"}
FAIL = []

# What gets typed, and what gets poked in behind ArtfulType's back.  TYPED is
# markdown so the copy carries delimiters; PAYLOAD is what no keyboard in this
# test ever produced, and every byte outside 32..126 in it is a fold case.
TYPED = "**bold** ok"
PAYLOAD = b"a\tb\r\nc\rd\x07e **B**"
FOLDED = b"a b\nc\nde **B**"          # tab->space, CR LF->LF, CR->LF, 07 dropped


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def check(name, ok, detail=""):
    print("   %-54s %s%s" % (name, "ok" if ok else "FAIL",
                             "" if ok else "  " + detail))
    if not ok:
        FAIL.append(name)


def pkg_syms():
    """ArtfulType's bss offsets, by re-assembling it - atmenusu's trick."""
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
    """Differing pixels and their bounding box, over the WHOLE screen."""
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
        m.key("KeyN")                          # New -> fullscreen (SPEC.md 46.5)
        # Freeze the blink before the first settle, exactly as atmenusu does:
        # a fullscreen ArtfulType otherwise never stops changing.
        m.write(seg * 16 + syms["at_drag"], b"\x01")
        m.write(seg * 16 + syms["at_cphase"], b"\x00")
        ui.settle()

        caret = syms["at_caret"]
        rw = lambda n: u16(m.readseg(seg, syms[n], 2))
        rb = lambda n: m.readseg(seg, syms[n], 1)[0]

        def key1(send, what):
            before = m.readseg(seg, caret, 2)
            send()
            os88marty.until(m, lambda _: m.readseg(seg, caret, 2) != before,
                            "ArtfulType to take " + what, poll=0.05, limit=120.0)

        for ch in TYPED:
            key1(lambda c=ch: m.type_text(c), repr(ch))
        ui.settle()

        vw, vh = rw("at_vw"), rw("at_vh")
        print("   %s: %dx%d, bpp %d, writer=%d"
              % (a.card, vw, vh, rb("at_vbpp"), rb("at_writer")))

        # ------------------------------------------------------------------
        # 1. Copy reaches the KERNEL's clipboard (SPEC.md 46.6.1)
        # ------------------------------------------------------------------
        cseg, cbytes, ckb = ui.sym("clip_seg"), ui.sym("clip_bytes"), \
            ui.sym("clip_kb")
        m.ctrl("KeyA")                         # ^A select all
        os88marty.quiesce(m, lambda: m.readseg(seg, syms["at_selb"], 2),
                          what="Select All to settle")
        m.ctrl("KeyC")                         # ^C copy
        os88marty.until(m, lambda _: u16(m.read(cseg, 2)) != 0,
                        "the kernel clipboard to be claimed",
                        poll=0.05, limit=120.0)

        kseg = u16(m.read(cseg, 2))
        klen = u16(m.read(cbytes, 2))
        kkb = m.read(ckb, 1)[0]
        got = bytes(m.read(kseg * 16, klen)) if klen else b""
        check("^C put the selection in the KERNEL's clipboard",
              got == TYPED.encode(), "clip=%r want %r" % (got, TYPED.encode()))
        check("...and it is the machine's claim, not ours",
              kseg != seg and kseg != 0, "clip_seg=%04x pkg_seg=%04x"
              % (kseg, seg))

        # ------------------------------------------------------------------
        # 2. Paste reads it, and FOLDS what another program put there
        # ------------------------------------------------------------------
        # Overwrite the claim in place.  Legitimate rather than lucky: the
        # claim is rounded up to a kilobyte (SPEC.md 55.2) and the payload is
        # far inside that, so nothing outside the clipboard's own block moves.
        assert len(PAYLOAD) <= kkb * 1024, "payload outside the claim"
        m.write(kseg * 16, PAYLOAD)
        m.write(cbytes, bytes([len(PAYLOAD) & 255, len(PAYLOAD) >> 8]))

        m.ctrl("KeyA")                         # replace the whole document
        os88marty.quiesce(m, lambda: m.readseg(seg, syms["at_selb"], 2),
                          what="Select All to settle")
        m.ctrl("KeyV")                         # ^V paste
        os88marty.until(m, lambda _: u16(m.readseg(seg, caret, 2)) == len(FOLDED),
                        "the paste to land", poll=0.05, limit=120.0)
        ui.settle()

        dseg, gs, ge = rw("at_dseg"), rw("at_gs"), rw("at_ge")
        dcap = rw("at_dcap")
        doc = bytes(m.read(dseg * 16, gs)) + \
            bytes(m.read(dseg * 16 + ge, dcap - ge))
        check("^V pasted bytes ArtfulType never typed", doc == FOLDED,
              "doc=%r want %r" % (doc, FOLDED))
        check("...and the fold dropped the control byte",
              b"\x07" not in doc, "doc=%r" % doc)

        # ------------------------------------------------------------------
        # 3. It redraws FORMATTED, and stays that way (SPEC.md 46.6.2)
        # ------------------------------------------------------------------
        check("Writer mode is on (the styling is a mode, SPEC.md 46.2.1)",
              rb("at_writer") != 0, "at_writer=0")
        check("the paste suppressed 46.2.2's reveal", rb("at_nrev") == 1,
              "at_nrev=%d" % rb("at_nrev"))
        check("...and it SURVIVED the draw - a latch, not a one-shot",
              rb("at_nrev") == 1, "at_nrev cleared by the repaint it caused")

        formatted = m.fbuf()
        if a.shot:
            os88marty.write_png_rgb(a.shot.replace(".png", "-pasted.png"),
                                    *formatted)

        # THE A/B, and it is the whole claim: type one character and take it
        # back.  The document and the caret end exactly where they were, both
        # keystrokes clear the latch on the way in (SPEC.md 46.6.2), so the
        # ONLY difference between these two pictures is whether the pasted
        # line's trailing `**` is on the glass.  Same bytes, same caret, two
        # pictures - which is what "it redraws as formatted" MEANS.
        key1(lambda: m.type_text("x"), "'x'")
        key1(lambda: m.key("Backspace"), "Backspace")
        ui.settle()
        revealed = m.fbuf()
        if a.shot:
            os88marty.write_png_rgb(a.shot.replace(".png", "-revealed.png"),
                                    *revealed)
        gs3, ge3 = rw("at_gs"), rw("at_ge")
        doc3 = bytes(m.read(rw("at_dseg") * 16, gs3)) + \
            bytes(m.read(rw("at_dseg") * 16 + ge3, rw("at_dcap") - ge3))
        check("the A/B left the document untouched", doc3 == FOLDED,
              "doc=%r" % doc3)
        check("...and the caret back where the paste left it",
              u16(m.readseg(seg, caret, 2)) == len(FOLDED),
              "caret=%d" % u16(m.readseg(seg, caret, 2)))
        check("the latch is clear again after a keystroke", rb("at_nrev") == 0,
              "at_nrev=%d" % rb("at_nrev"))
        n, box = diff(formatted[2], revealed[2], vw, vh)
        check("the pasted line DID render formatted (delimiters hidden)",
              n > 0, "0 differing px - the trailing ** was drawn either way")
        print("   the A/B moved %d px, box %r" % (n, box))

        # ------------------------------------------------------------------
        # 4. The minimize box goes to the DOCK (SPEC.md 46.5.2, 46.5.3)
        # ------------------------------------------------------------------
        mo = os88mouse.Mouse(marty=m)
        BOX_X, BOX_Y = vw - 14, 9              # the box spans vw-19 .. vw-9
        NEAR_X = vw - 40                       # bar, right of every title,
                                               # left of the box: a no-op
        wptr = os88geom.winptr(m, w.i, ui.sym)
        vis = lambda: bool(u16(m.read(wptr + os88geom.W_FLAGS, 2)) & 2)

        check("still fullscreen before the box is touched", rb("at_fs") == 1,
              "at_fs=%d" % rb("at_fs"))
        mo.click(NEAR_X, BOX_Y)
        ui.settle()
        check("a click on the bar BESIDE the box does nothing",
              rb("at_fs") == 1, "at_fs=%d - the box is too wide" % rb("at_fs"))

        mo.click(BOX_X, BOX_Y)
        os88marty.until(m, lambda _: m.readseg(seg, syms["at_fs"], 1)[0] == 0,
                        "the minimize box to leave the surface",
                        poll=0.05, limit=120.0)
        ui.settle()
        # Through wm_owner (window slot -> instance slot), which is tile_xy's
        # own route and the kernel's: I_WIN holds a 16-bit OFFSET and winptr()
        # answers a LINEAR address, so the two never compare equal.
        islot = m.read(ui.sym("wm_owner"), os88geom.MAX_WIN)[w.i]
        rec = os88geom.instances(m, ui.sym).get(islot)
        check("the box left a live instance to look at", rec is not None,
              "wm_owner says slot %d, which is not live" % islot)
        rec = rec or {"flags": 0, "minimized": False}
        # THE WHOLE POINT OF THE SLOT: a plain OSAPI_WM_HIDE would pass every
        # other check here and leave this bit CLEAR - a live-looking tile whose
        # click goes to wm_front, which never shows an invisible window
        # (SPEC.md 29.6).
        check("it MINIMIZED rather than hid (I_FLAGS bit 0)", rec["minimized"],
              "I_FLAGS=%02x - a hide, not a minimize" % rec["flags"])
        check("...and IF_FSMIN says the way back must not zoom",
              bool(rec["flags"] & 2), "I_FLAGS=%02x" % rec["flags"])
        check("...and the app owes itself a fullscreen re-entry",
              rb("at_fsowed") == 1, "at_fsowed=%d" % rb("at_fsowed"))
        check("...and the window really is off the glass", not vis(),
              "W_FLAGS still says visible")

        # ------------------------------------------------------------------
        # 5. ...and the tile comes back to the DOCUMENT, not the splash
        # ------------------------------------------------------------------
        tx, ty = os88geom.tile_xy(m, w, ui.sym)
        mo.click(tx, ty)
        os88marty.until(m, lambda _: m.readseg(seg, syms["at_fs"], 1)[0] == 1,
                        "the dock tile to bring the DOCUMENT back",
                        poll=0.05, limit=120.0)
        ui.settle()
        check("the tile restored it straight to FULLSCREEN", rb("at_fs") == 1,
              "at_fs=%d - it stopped at the splash" % rb("at_fs"))
        check("...and the owed flag is spent", rb("at_fsowed") == 0,
              "at_fsowed=%d - the next paint would wake for ever"
              % rb("at_fsowed"))
        gs4, ge4 = rw("at_gs"), rw("at_ge")
        doc4 = bytes(m.read(rw("at_dseg") * 16, gs4)) + \
            bytes(m.read(rw("at_dseg") * 16 + ge4, rw("at_dcap") - ge4))
        check("...with the document intact across the round trip",
              doc4 == FOLDED, "doc=%r" % doc4)
        if a.shot:
            os88marty.write_png_rgb(a.shot.replace(".png", "-restored.png"),
                                    *m.fbuf())

        # ------------------------------------------------------------------
        # 6. The close box ASKS about unsaved changes (SPEC.md 46.7.1)
        # ------------------------------------------------------------------
        # Esc is still the door to the splash (46.5.3), and the close box is
        # only reachable from there - a WF_FULL window has no chrome at all.
        m.key("Escape")
        os88marty.until(m, lambda _: m.readseg(seg, syms["at_fs"], 1)[0] == 0,
                        "Esc to hand the screen back", poll=0.05, limit=120.0)
        ui.settle()
        check("the document is still unsaved, which is what makes this a test",
              rb("at_dirty") == 1, "at_dirty=%d" % rb("at_dirty"))

        before_n = len(os88geom.windows(m, ui.sym))
        wx, wy = os88geom.win_rect(m, w.i, ui.sym)[:2]
        mo.click(*os88geom.close_xy(wx, wy))
        ui.settle()
        after = os88geom.windows(m, ui.sym)
        still = [x for x in after if x.i == w.i]
        check("the close box did NOT take the document with it",
              len(still) == 1, "our window is gone - it closed silently")
        check("...it put the question up instead",
              len(after) == before_n + 1,
              "%d windows, was %d - no alert appeared" % (len(after), before_n))

        # Cancel: the one answer that must change nothing at all.
        m.key("Escape")                        # dismissed == OS88UI_ACANCEL
        ui.settle()
        after2 = os88geom.windows(m, ui.sym)
        check("Cancel left the window open", any(x.i == w.i for x in after2),
              "it closed on a cancel")
        check("...and the alert gone", len(after2) == before_n,
              "%d windows, was %d" % (len(after2), before_n))
        check("...and the document still unsaved", rb("at_dirty") == 1,
              "at_dirty=%d" % rb("at_dirty"))

    print()
    print("atclip: %s" % ("FAILED: " + ", ".join(FAIL) if FAIL else "ok"))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
