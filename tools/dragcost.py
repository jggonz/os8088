#!/usr/bin/env python3
"""The field repro for SPEC.md 11.96.15: four cascaded windows, top one dragged.

    python3 tools/dragcost.py [machine]

Builds the reported arrangement - a Disk window on B:\\GAMES, Solitaire,
Minesweeper and a second Disk window on A: - and drags the top one 8px right,
4px down. Every coordinate comes from the guest (wm_wins / wm_zord /
desk_zstep), so it runs on any adapter.

It prints each window's raise-cache segment before and after.

TWO INSTRUMENTS ARE NOT IN THE SHIPPED KERNEL, and this runs without either.
To see what each window is TOLD it owes, put a trace back into wm_dmg_wins
beside wm_su_sub (`dbgt`/`dbgt_n`, 14 bytes a row); the table below is skipped
when those symbols are absent rather than raising. To see what a restore
actually COSTS, put a counter and a 32-bit accumulator beside the gfx_restore
in wm_su_try's piece loop - that is what measured SPEC.md 11.96.15's
125,454 -> 59,646 pixels, and 32-bit because one drag of this cascade is more
than a word.

THE A/B IS `make NOSUOCCL=1`, NOT `make REDRAWFULL=1`. REDRAWFULL turns off
every incremental path at once and its kernel is an image rung smaller, so the
volume has a kilobyte more free and the Disk window's status line alone
differs by 25 pixels in a gate whose standard is zero (SPEC.md 11.96.15.2).
"""
import sys, os, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools"))
import os88marty
from os88mouse import Mouse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SYM = {}
def sym(m, n):
    if n not in SYM: SYM[n] = m.sym(n)
    return SYM[n]
def w(m, name, idx=0):
    d = m.read(sym(m, name) + idx * 2, 2); return d[0] | (d[1] << 8)
def bb(m, name, idx=0):
    return m.read(sym(m, name) + idx, 1)[0]

from os88geom import WIN_SIZE   # NOT a local copy: it moved 28 -> 30
W_FLAGS, W_X, W_Y, W_W, W_H = 0, 2, 4, 6, 8
TITLE_H, FM_ROW_Y0, FM_ROW_H, DESK_ZY0 = 18, 22, 16, 32

def win(m, slot):
    d = m.read(sym(m, "wm_wins") + slot * WIN_SIZE, WIN_SIZE)
    g = lambda o: d[o] | (d[o + 1] << 8)
    return dict(slot=slot, flags=g(W_FLAGS), x=g(W_X), y=g(W_Y), w=g(W_W),
                h=g(W_H), vis=bool(g(W_FLAGS) & 2), su=w(m, "wm_su_segs", slot))
def vis(m):
    return [e for e in (win(m, s) for s in range(12)) if e["vis"]]
def zorder(m):
    n = bb(m, "wm_zn")
    return list(m.read(sym(m, "wm_zord"), max(n, 1)))[:n]

def zone(m, vol):
    zx, step, rows = w(m, "vid_desk_zx"), w(m, "desk_zstep"), w(m, "desk_rows")
    col, row = divmod(vol, rows)
    return zx - col * 44 + 16, DESK_ZY0 + row * step + 16
def row(e, n):
    return e["x"] + 48, e["y"] + TITLE_H + FM_ROW_Y0 + n * FM_ROW_H + 8

def has_trace(m):
    """The trace block is NOT in the shipped kernel - see the note above."""
    try:
        sym(m, "dbgt_n")
        return True
    except Exception:
        return False

def trace_reset(m):
    if has_trace(m):
        m.write(sym(m, "dbgt_n"), b"\0\0")
def trace(m):
    if not has_trace(m):
        return []
    n = w(m, "dbgt_n")
    base = sym(m, "dbgt")
    out = []
    for i in range(min(n, 8)):
        d = m.read(base + i * 14, 14)
        g = lambda o: d[o] | (d[o + 1] << 8)
        out.append(dict(slot=g(0), hit=g(2), x1=g(4), y1=g(6), x2=g(8),
                        y2=g(10), dmgx2=g(12)))
    return out

def show(m, label):
    print("--- %s  z=%s" % (label, zorder(m)))
    for e in vis(m):
        print("      slot %d (%3d,%3d %3dx%3d) su=%04X"
              % (e["slot"], e["x"], e["y"], e["w"], e["h"], e["su"]))
    sys.stdout.flush()

def main():
    machine = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_herc_gla"
    with os88marty.launch(os.path.join(ROOT, "build/os8088-360.img"),
                          apps=os.path.join(ROOT, "build/apps360.img"),
                          machine=machine) as m:
        mo = Mouse(marty=m)
        print("mode", m.video()["mode"], "screen", w(m, "vid_w"), w(m, "vid_h"))

        mo.dblclick(*zone(m, 1))                       # B:
        os88marty.settle(m)
        b = vis(m)[0]
        mo.dblclick(*row(m and b, 1))                  # GAMES
        os88marty.settle(m)
        # GAMES sorted: .. ARKANOID MINES MISSILE SOLITAIR TAMEGRAM
        mo.dblclick(*row(b, 4))                        # SOLITAIR
        time.sleep(6); os88marty.settle(m)
        show(m, "solitaire up")
        mo.click(b["x"] + 40, b["y"] + 8)              # raise the Disk window
        os88marty.settle(m)
        mo.dblclick(*row(b, 2))                        # MINES
        time.sleep(5); os88marty.settle(m)
        show(m, "minesweeper up")
        mo.dblclick(*zone(m, 0))                       # a Disk window on A:
        time.sleep(3); os88marty.settle(m)
        show(m, "disk A: up")

        ws = vis(m); z = zorder(m)
        top = [e for e in ws if e["slot"] == z[-1]][0]
        print("top window is slot %d at (%d,%d) %dx%d"
              % (top["slot"], top["x"], top["y"], top["w"], top["h"]))

        trace_reset(m)
        mo.drag(top["x"] + 60, top["y"] + 8, top["x"] + 68, top["y"] + 12)
        os88marty.settle(m)
        show(m, "top dragged 8px right, 4 down")
        print("  damage pass, in draw order:")
        for t in trace(m):
            print("    slot %d  %s  owed (%d,%d)-(%d,%d)  w=%d h=%d   dmg x2 after=%d"
                  % (t["slot"], "HIT " if t["hit"] else "MISS",
                     t["x1"], t["y1"], t["x2"], t["y2"],
                     t["x2"] - t["x1"] + 1, t["y2"] - t["y1"] + 1, t["dmgx2"]))

if __name__ == "__main__":
    main()
