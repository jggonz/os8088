#!/usr/bin/env python3
"""SPEC.md 43.12.1: a VGA game taken to CGA through the Control Panel - are
the card backs re-rendered for the new card and the new bpp?

    python3 tools/solswitch.py OUTDIR IMG

Boots os8088_xt_vga, deals solcheck's seed, switches the Display page to CGA,
forces a full repaint with Restart Deal and writes the whole frame
(cga.png / cga.rgb). log.txt has [sol_cw] and [sol_stride] either side of
the switch: a stride that is still 16 for a 28-wide card is the defect. A
VGA card in a CGA mode has no flat framebuffer for vram(), so this reads
fbuf() (640x400, rows doubled) rather than cropping like solcheck.
"""
import os, struct, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88marty, os88sym, os88ui
from os88mouse import Mouse
from os88geom import W_SEG, WIN_SIZE
import sucheck as su
import subcheck as sc
import solcheck as sk
import dispcp, dispfit

S = os88sym.linear


def main():
    out, img = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    log = []
    def say(*a):
        t = " ".join(str(x) for x in a); print(t); log.append(t)
    with os88marty.launch(os.path.join(ROOT, "build/os8088-360.img"),
                          apps=img, machine="os8088_xt_vga") as m:
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
        Sy = sk.solsyms()
        def cur():
            return [w for w in su.windows(m) if w.i == sol.i][0]
        def words():
            r = {}
            for n in ("sol_cw", "sol_ch", "sol_stride"):
                r[n] = struct.unpack("<H", m.read((seg << 4) + Sy[n], 2))[0]
            r["sol_bpp"] = m.read((seg << 4) + Sy["sol_bpp"], 1)[0]
            return r
        m.write(m.sym("osapi_seed"), struct.pack("<H", sk.SEED))
        sc.pclick(mo, sol.x + sol.w // 2, sol.y + sol.h - 30)
        os88marty.settle(m)
        m.write(m.sym("osapi_seed"), struct.pack("<H", sk.SEED))
        m.key("KeyN")
        os88marty.settle(m)
        say("VGA:", words())
        sk.shot(m, cur(), os.path.join(out, "vga.raw"), "vga", mo)

        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispfit.switch_to(m, mo, os88marty.settle, 1, 2)    # VID_CGA
        say("CGA:", words(), "kind", dispfit.kind(m))
        dispcp.close_panel(m, mo, S, os88marty.settle)
        w = ui.window(cur().title)
        ui.raise_window(w)
        os88marty.settle(m)
        m.key("KeyR")                       # Restart Deal: a full sol_drawall
        os88marty.settle(m)
        mo.to(5, 100)
        os88marty.guest_sleep(m, 0.2)
        w_, h_, d_ = m.fbuf()
        os88marty.write_png_rgb(os.path.join(out, "cga.png"), w_, h_, d_)
        open(os.path.join(out, "cga.rgb"), "wb").write(struct.pack("<2H", w_, h_) + bytes(d_))
        say("fbuf", w_, h_, "window", cur())
        m.quit()
    open(os.path.join(out, "log.txt"), "w").write("\n".join(log) + "\n")


if __name__ == "__main__":
    main()
