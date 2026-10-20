#!/usr/bin/env python3
"""Solitaire's other A/B session: the paths tools/solcheck.py's PLAY never
reaches (SPEC.md 43.11.5) - the win plaque put up by a played card and taken
down by dragging it back, the About card over it, the Deal menu's radio pair
read back off the live menu bar, Draw Three and Draw One, an extended key
(AL = 0) that must not deal, Restart, and a stock dealt dry and recycled.

    python3 tools/solpanel.py OUTDIR IMG [machine]
    python3 tools/solcheck.py diff OUTDIR-A OUTDIR-B   # and diff the log.txt

The same SOLBIN / SOLSRC / SOLSMALL as solcheck, so a frozen copy of a base
tree can be captured while the working one is edited. The win is POKED - a
board one card from won, the pile layout read off the source being run - so
it needs no lucky deal. The self-check in `diff` finds no play*.raw here and
passes vacuously; the captures are what is compared.
"""
import os, struct, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui
from os88mouse import Mouse
from os88geom import W_SEG, WIN_SIZE
import sucheck as su
import subcheck as sc
import solcheck as sk


def main():
    out, img = sys.argv[1], sys.argv[2]
    machine = sys.argv[3] if len(sys.argv) > 3 else "os8088_5150_cga_gla"
    os.makedirs(out, exist_ok=True)
    log = []
    def say(*a):
        t = " ".join(str(x) for x in a); print(t); log.append(t)
    with os88marty.launch(os.path.join(ROOT, "build/os8088-360.img"),
                          apps=img, machine=machine) as m:
        mo = Mouse(marty=m)
        os88marty.no_saver(m)
        ui = os88ui.UI(m, mouse=mo, verbose=False)
        mo.dblclick(*su.zone(m, 1))
        os88marty.until(m, lambda _: any(w.visible for w in su.windows(m)),
                        "the Disk window", poll=0.1, limit=60)
        disk = [w for w in su.windows(m) if w.visible][0]
        mo.dblclick(*su.row(disk, 0))
        def solwins():
            return [w for w in su.windows(m) if w.visible and w.title.upper().startswith("SOL")]
        os88marty.until(m, lambda _: solwins(), "Solitaire", poll=0.1, limit=60)
        os88marty.settle(m)
        sol = solwins()[0]
        seg = struct.unpack("<H", m.read(m.sym("wm_wins") + sol.i * WIN_SIZE + W_SEG, 2))[0]
        S = sk.solsyms()
        def cur():
            return [w for w in su.windows(m) if w.visible and w.i == sol.i][0]
        def shot(tag):
            sk.shot(m, cur(), os.path.join(out, tag + ".raw"), tag, mo)
        def menus():
            return [(t, it) for (t, x0, x1, it) in ui.menus()]
        m.write(m.sym("osapi_seed"), struct.pack("<H", sk.SEED))
        sc.pclick(mo, sol.x + sol.w // 2, sol.y + sol.h - 30)
        os88marty.settle(m)
        m.write(m.sym("osapi_seed"), struct.pack("<H", sk.SEED))
        m.key("KeyN")
        os88marty.settle(m)
        shot("a_deal")

        # --- the win plaque: a board one card from won, and the card played
        # by the press-and-release gesture (sol_tofnd -> sol_domove ->
        # sol_checkwin -> the full repaint that carries the plaque)
        offs, total = sk.pile_offsets(open(os.path.join(ROOT, sk.SOLSRC)).read())
        raw = bytearray(total)
        cnt = [0] * 13
        for f in range(4):
            n = 12 if f == 0 else 13
            for r in range(n):
                raw[offs[7 + f] + r] = (f << 4) | r | 0x40
            cnt[7 + f] = n
        raw[offs[sk.P_WASTE]] = 0x0C | 0x40          # the king of spades
        cnt[sk.P_WASTE] = 1
        base = seg << 4
        m.write(base + S["sol_pile"], bytes(raw))
        m.write(base + S["sol_pcnt"], bytes(cnt))
        m.write(base + S["sol_wfan"], b"\x01")
        ui.menu_pick("Deal", "Draw Three")          # a menu command repaints
        os88marty.settle(m)                         # nothing: so poke, then
        ui.menu_pick("Deal", "Draw One")            # make the board visible
        b = sk.Board(m, seg, S)                     # through a move instead
        px, py = sk.grabxy(b, sk.P_WASTE, 0)
        sc.pclick(mo, px, py)
        os88marty.settle(m)
        b.reload()
        say("won: [sol_won] = %d, foundations %r" % (m.read(base + S["sol_won"], 1)[0], b.cnt[7:11]))
        shot("b_won")
        # ...and dragged back off onto the empty first column: unwon, and the
        # plaque comes down with a full repaint again
        fx, fy = b.pilexy(sk.P_FND0)        # (grabxy fans a TABLEAU pile)
        fx, fy = b.ox + fx + b.cw // 2, b.oy + fy + 4
        tx, ty = b.pilexy(0)
        qx, qy = b.ox + tx + b.cw // 2, b.oy + ty + 6
        mo.to(fx, fy)
        mo._edge(True)
        os88marty.guest_sleep(m, 0.3)
        mo.to((fx + qx) // 2, (fy + qy) // 2, l=True)
        os88marty.guest_sleep(m, 0.3)
        mo.to(qx, qy, l=True)
        os88marty.guest_sleep(m, 0.3)       # the drag samples once a tick
        mo._edge(False)
        os88marty.ui_done(m, "the drop")
        os88marty.settle(m)
        b.reload()
        say("unwon: [sol_won] = %d, col0 %r" % (m.read(base + S["sol_won"], 1)[0], b.pile[0]))
        shot("c_unwon")
        # ...and up again for the About card to land on
        px, py = sk.grabxy(b, 0, 0)
        sc.pclick(mo, px, py)
        os88marty.settle(m)
        say("won again: [sol_won] = %d" % m.read(base + S["sol_won"], 1)[0])

        # --- the About card (the full arm only)
        names = [t for t, it in menus()]
        say("bar:", names)
        about = [it for t, it in menus() if t == "Solitaire"]
        say("Solitaire menu:", about)
        if about and any("About" in x for x, e in about[0]):
            ui.menu_pick("Solitaire", [x for x, e in about[0] if "About" in x][0])
            os88marty.settle(m)
            shot("d_about")
            m.key("KeyQ")               # any key takes it down, and is spent
            os88marty.settle(m)
            shot("e_dismissed")
        m.key("KeyN")                   # a fresh deal for what follows
        os88marty.settle(m)

        # --- the Deal menu: a radio pair of MENU_DIS twins
        say("Deal before:", [it for t, it in menus() if t == "Deal"])
        ui.menu_pick("Deal", "Draw Three")
        os88marty.settle(m)
        say("Deal after 3:", [it for t, it in menus() if t == "Deal"])
        b = sk.Board(m, seg, S)
        w0 = b.cnt[sk.P_WASTE]
        m.key("Space")
        os88marty.settle(m)
        b.reload()
        say("draw three: waste %d -> %d, wfan %d" % (w0, b.cnt[sk.P_WASTE], b.wfan))
        shot("f_draw3")
        ui.menu_pick("Deal", "Draw One")
        os88marty.settle(m)
        say("Deal after 1:", [it for t, it in menus() if t == "Deal"])
        m.key("Space")
        os88marty.settle(m)
        b.reload()
        say("draw one: waste -> %d, wfan %d" % (b.cnt[sk.P_WASTE], b.wfan))
        shot("g_draw1")

        # --- keys: an extended key (AL = 0) must do nothing - a case fold in
        # front of the space test would read it as Space and deal
        b.reload(); before = (list(b.cnt), b.wfan)
        m.key("ArrowLeft")
        os88marty.settle(m)
        b.reload()
        say("arrow key: board %s" % ("UNCHANGED" if (list(b.cnt), b.wfan) == before else "CHANGED %r -> %r" % (before, (b.cnt, b.wfan))))
        # Game menu: Restart Deal puts the dealt position back
        ui.menu_pick("Game", "Restart Deal")
        os88marty.settle(m)
        shot("h_restart")
        b.reload()
        say("restart: stock %d waste %d" % (b.cnt[sk.P_STOCK], b.cnt[sk.P_WASTE]))
        # recycle: deal the whole stock through, then once more (the ring)
        for k in range(30):
            m.key("Space")
            os88marty.settle(m, quiet=0.3)
            b.reload()
            if b.cnt[sk.P_STOCK] == 0:
                break
        shot("i_ring")
        m.key("Space")
        os88marty.settle(m)
        b.reload()
        say("recycled: stock %d waste %d" % (b.cnt[sk.P_STOCK], b.cnt[sk.P_WASTE]))
        shot("j_recycled")
        m.quit()
    open(os.path.join(out, "log.txt"), "w").write("\n".join(log) + "\n")


if __name__ == "__main__":
    main()
