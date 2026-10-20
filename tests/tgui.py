#!/usr/bin/env python3
"""TameGram's UI half: every key, both menus and both panels, on the machine.

    make && python3 tests/tgui.py                 # CGA
    python3 tests/tgui.py --card herc --save base.json
    python3 tests/tgui.py --ref base.json         # the panels, pixel for pixel

tests/tgplay.py plays the game by POSTING the worker's command byte, so it
never goes through W_ONKEY - and W_ONKEY is a table now (SPEC.md 49.8): two
lookups, the letters folded to upper case, and four of its entries are not
commands at all but tg_act's toggles, the same four the Protocol menu
reaches. This is the row for that half. Every step is a real key or a real
menu pick through tools/os88ui.py, and every assertion is the package's own
state read back on the guest's clock:

  * the Help panel and the About panel go up (`[tg_panel]` 2 and 1), a key
    takes one down and is SPENT doing it (the game stays halted under it), a
    click takes the other down;
  * C/c and Protocol > Colour-Blind flip `[tg_cblind]`, F/f and Lock Vector
    `[tg_glock]`, P/p and Halt pause and resume (`[tg_mode]`), N and New
    Protocol empty the board and zero THREAT;
  * Space rotates, A/D and an arrow ACROSS the vector strafe the piece one
    cell on the cross axis, Enter hard-drops it (THREAT moves);
  * an unmapped key does nothing.
With --save/--ref, the two panels' pixels (CGA and Hercules) against another
build's.
"""
import argparse
import hashlib
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88marty, os88ui, os88geom                         # noqa: E402
import tgplay                                              # noqa: E402

M_PLAY, M_PAUSE = 0, 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", default="cga", choices=sorted(tgplay.CARDS))
    ap.add_argument("--src", default=os.path.join(ROOT, "apps", "tamegram",
                                                  "tamegram.asm"))
    ap.add_argument("--system", default=os.path.join(ROOT, "build",
                                                     "os8088-360.img"))
    ap.add_argument("--games", default=os.path.join(ROOT, "build",
                                                    "games360.img"))
    ap.add_argument("--shots", help="a directory for PNGs of each panel")
    ap.add_argument("--save")
    ap.add_argument("--ref")
    a = ap.parse_args()
    syms, equ, _ = tgplay.pkg_syms(a.src)
    G = tgplay.Grid(equ)
    bad, out = [], {"card": a.card, "panels": {}}

    def check(ok, what):
        print("tgui: %s %s" % ("PASS" if ok else "FAIL", what))
        if not ok:
            bad.append(what)

    with os88ui.boot(a.system, apps=a.games,
                     machine=tgplay.CARDS[a.card]) as ui:
        m = ui.m
        w = ui.path("B:/TAMEGRAM.O88")
        seg = tgplay.u16(m.read(os88geom.winptr(m, w.i, ui.sym)
                                + os88geom.W_SEG, 2))
        S = lambda n: seg * 16 + syms[n]           # noqa: E731
        B = lambda n: m.read(S(n), 1)[0]           # noqa: E731
        W = lambda n: tgplay.u16(m.read(S(n), 2))  # noqa: E731

        def wait(cond, what="that", limit=30.0):
            try:
                os88marty.until(m, lambda _: cond(), what, poll=0.05,
                                limit=limit)
                return True
            except os88marty.MartyError:
                return False

        def key(name, shift=False):
            m._shifted(name, shift)

        wait(lambda: B("tg_hired") == 1, "the worker", 120.0)
        ui.mo.to(4, 30)

        def card():
            """The panel's rect, by os88ui_about_d's own arithmetic (SPEC.md
            20.5.1.1): the card keeps it in REGISTERS, so it is re-derived
            from the text [tg_plines] names and the pitch the measure left
            in os88ui_abp - an 8px cell a glyph, a leading OS88UI_ABLT byte
            not measured, 12px of margin a side and 7 above and below."""
            p, n, wide = W("tg_plines"), 0, 0
            text = m.read(seg * 16 + p, 512)
            for ln in text[:text.index(0xFF)].split(b"\0")[:-1]:
                n += 1
                wide = max(wide, 8 * len(ln.lstrip(b"\x01")))
            cw, ch = W("tg_cwid"), W("tg_chgt")
            w_ = min(wide + 24, cw)
            h_ = min(n * W("os88ui_abp") + 14, ch)
            return (cw - w_) // 2, (ch - h_) // 2, w_, h_

        def panel(name):
            ox, oy = W("tg_ox"), W("tg_oy")
            l, t, w_, h_ = card()
            if a.card != "vga":
                _, _, rows = m.vram(a.card)
                pix = bytes(b for r in rows[oy + t:oy + t + h_]
                            for b in r[ox + l:ox + l + w_])
                out["panels"][name] = hashlib.sha1(pix).hexdigest()[:16]
            if a.shots:
                os.makedirs(a.shots, exist_ok=True)
                ww, hh, data = m.fbuf(0)
                os88marty.write_png_rgb(os.path.join(
                    a.shots, "%s-%s.png" % (a.card, name)), ww, hh, data)

        # --- halt first, so nothing moves under the panels ---------------
        key("KeyP")
        check(wait(lambda: B("tg_mode") == M_PAUSE), "p halts")
        def panel_up(n, pick):
            # os88ui_abp is the card's measure's, written before the panel is
            # drawn: zero it, and the measure is what says the draw began.
            # The menu's handler is still drawing when [tg_panel] reads 1 or 2
            # - half a guest second is a whole panel on a 4.77MHz 8088.
            m.write(S("os88ui_abp"), b"\0\0")
            pick()
            up = wait(lambda: B("tg_panel") == n and W("os88ui_abp") != 0)
            os88marty.guest_sleep(m, 0.5)
            return up

        check(panel_up(2, lambda: ui.menu_pick("Help", "How to Play")),
              "Help > How to Play puts the instruction sheet up")
        check(W("os88ui_abp") in range(8, 13), "the sheet's pitch is on the "
              "card's ladder, 12 down to 8 (%d)" % W("os88ui_abp"))
        # ...and it is the LARGEST pitch at which all thirteen lines fit, which
        # is what OS88UI_ABFIT is for: a fixed 12 hangs the key list out of
        # CGA's 124-row box and the card clips its last lines at the frame
        pt, ch = W("os88ui_abp"), W("tg_chgt")
        check(13 * pt + 14 <= ch and (pt == 12 or 13 * (pt + 1) + 14 > ch),
              "the sheet is drawn whole at the largest pitch that fits "
              "(%d rows of %d)" % (13 * pt + 14, ch))
        panel("help")
        key("KeyQ")
        check(wait(lambda: B("tg_panel") == 0), "a key takes it down")
        check(B("tg_mode") == M_PAUSE, "...and is spent doing it: still "
              "halted")
        app, about = next(((t, it) for t, _, _, items in ui.menus()
                           for it, _en in items
                           if it.startswith("About Tame")), (None, None))
        check(about is not None, "the bar carries an About TameGram item")
        check(panel_up(1, lambda: ui.menu_pick(app, about)),
              "%s > %s puts the credits up" % (app, about))
        panel("about")
        ox, oy, cw, ch = (W(n) for n in ("tg_ox", "tg_oy", "tg_cwid",
                                         "tg_chgt"))
        ui.mo.click(ox + cw // 2, oy + ch // 2)
        check(wait(lambda: B("tg_panel") == 0), "a click takes it down")
        ui.mo.to(4, 30)

        # --- the four toggles, by key and by menu -------------------------
        for name, sh, var, want, what in (
                ("KeyC", False, "tg_cblind", 1, "c: colour-blind on"),
                ("KeyC", True, "tg_cblind", 0, "C: colour-blind off"),
                ("KeyF", False, "tg_glock", 1, "f: vector locked"),
                ("KeyF", True, "tg_glock", 0, "F: vector unlocked")):
            key(name, sh)
            check(wait(lambda: B(var) == want), what)
        for item, var, want in (("Colour-Blind", "tg_cblind", 1),
                                ("Lock Vector", "tg_glock", 1),
                                ("Colour-Blind", "tg_cblind", 0),
                                ("Lock Vector", "tg_glock", 0)):
            ui.menu_pick("Protocol", item)
            check(wait(lambda: B(var) == want), "Protocol > %s -> %s %d"
                  % (item, var, want))
        ui.menu_pick("Protocol", "Halt")
        check(wait(lambda: B("tg_mode") == M_PLAY), "Protocol > Halt "
              "resumes")
        ui.menu_pick("Protocol", "Halt")
        check(wait(lambda: B("tg_mode") == M_PAUSE), "Protocol > Halt "
              "halts")
        key("KeyP", True)
        check(wait(lambda: B("tg_mode") == M_PLAY), "P resumes")

        # --- steering, through W_ONKEY ------------------------------------
        def sgn(v):
            return v - 256 if v > 127 else v

        bytewise = syms["tg_pr"] == syms["tg_pc"] + 1   # this build's or
                                                        # the words before
        def piece():
            if bytewise:
                return sgn(B("tg_pc")), sgn(B("tg_pr"))
            return W("tg_pc"), W("tg_pr")


        key("KeyN")
        check(wait(lambda: W("tg_score") == 0 and B("tg_mode") == M_PLAY),
              "n: a new game")
        # EVERY ONE WAITS FOR ITS EFFECT. Waiting for [tg_cmd] to read 0 is
        # true before the key has even arrived, which is how the first draft
        # of this read every steering key as doing nothing.
        rot0 = B("tg_prot")
        key("Space")
        check(wait(lambda: B("tg_prot") == (rot0 + 1) & 3), "Space rotates")
        for k, sh, dlat, what in (("KeyA", False, -1, "a strafes"),
                                  ("KeyD", True, 1, "D strafes"),
                                  (None, False, -1, "an arrow across the "
                                   "vector strafes")):
            g = B("tg_grav")
            c0, r0 = piece()
            if k is None:                   # the arrow ACROSS the vector
                k = "ArrowLeft" if g in (0, 2) else "ArrowUp"
            key(k, sh)
            cross = (lambda: piece()[0] - c0) if g in (0, 2) else \
                (lambda: piece()[1] - r0)
            wait(lambda: cross() != 0)
            moved = cross()
            check(moved == dlat, "%s (vector %d: moved %d across it)"
                  % (what, g, moved))
        s0 = W("tg_score")
        key("Enter")
        check(wait(lambda: W("tg_score") > s0), "Enter hard-drops: THREAT "
              "%d -> %d" % (s0, W("tg_score")))
        before = (B("tg_cblind"), B("tg_glock"), B("tg_panel"))
        key("KeyQ")
        os88marty.guest_sleep(m, 0.5)
        check((B("tg_cblind"), B("tg_glock"), B("tg_panel")) == before
              and B("tg_cmd") == 0, "q does nothing")
        ui.menu_pick("Protocol", "New Protocol")
        check(wait(lambda: W("tg_score") == 0 and not any(
            G.decode(m.read(S("tg_grid"), G.nbytes)))),
            "Protocol > New Protocol empties the board")

    if a.save:
        json.dump(out, open(a.save, "w"))
    if a.ref:
        ref = json.load(open(a.ref))
        for k, v in sorted(out["panels"].items()):
            check(ref["panels"].get(k) == v, "the %s panel is the "
                  "reference's, pixel for pixel" % k)
    print("tgui: ok" if not bad else "tgui: %d FAILED" % len(bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
