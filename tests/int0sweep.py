#!/usr/bin/env python3
"""Arm INT 0 (divide error) across a broad UI session on the IBM ROM.

Nothing in this kernel should ever raise one; on an IBM 5150/5160 ROM the
vector is the BIOS stub that masks the whole 8259, so a single divide
overflow is a dead machine.
"""
import sys, time
sys.path.insert(0, "tools"); sys.path.insert(0, "tests")
import os88marty, os88mouse, os88sym, dispcp
S = os88sym.linear
OFF = os88sym.syms()
def near(o):
    b = None
    for n, a in OFF.items():
        if a <= o and (b is None or a > b[1]): b = (n, a)
    return "%s+%d" % (b[0], o - b[1])
def say(s): print("  " + s, flush=True)
fired = []
def check(m, where):
    st = m.status().get("state")
    if st == "breakpoint":
        r = m.regs()
        stk = m.read((r["ss"] << 4) + r["sp"], 12)
        w = [stk[j] | stk[j+1] << 8 for j in range(0, 12, 2)]
        say("!! INT 0 during %s: frame %04X:%04X = %s, bx=%04X, stack %s"
            % (where, w[1], w[0], near(w[0]), r["bx"],
               " ".join("%04X" % v for v in w)))
        fired.append(where)
        m.breakpoints([]); m.run()
        return False
    return True

with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine="os8088_5150_cga") as m:
    mo = os88mouse.Mouse(marty=m)
    m.breakpoints([{"type": "int", "addr": 0}])
    for drv in ("A", "B"):
        dispcp.open_drive(m, mo, S, os88marty.settle, drv)
        if not check(m, "open drive %s" % drv): break
        wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
        for f in [n for n, _ in dispcp.listing(m, S) if "." not in n]:
            dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, f)
            if not check(m, "%s:\\%s" % (drv, f)): break
            dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "..")
            if not check(m, "%s:\\%s ->.." % (drv, f)): break
    else:
        # a drag, a raise and a menu
        slot = dispcp.win_list(m, S)[0]
        x, y, w, h = dispcp.win_rect(m, S, slot)
        mo.to(x + w // 2, y + 9); mo._edge(True)
        m.mouse(30, 20, l=True); m.advance(frames=45); m.run()
        m.mouse(0, 0, l=False); m.run(); os88marty.settle(m)
        check(m, "title drag")
        mo.click(12, 8); os88marty.settle(m)
        check(m, "menu drop")
        mo.click(300, 190); os88marty.settle(m)
        check(m, "menu dismiss")
    m.breakpoints([]); m.run()
    say("INT 0 fired in: %r" % fired if fired else "INT 0 never fired")
