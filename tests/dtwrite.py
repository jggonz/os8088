#!/usr/bin/env python3
"""SPEC.md 37.94: the RTC write is spent by the panel's CLOSE, not by the tick.

    python3 tests/dtwrite.py [machine]

`[clk_dirty]` used to be drained by the UI ladder on the system tick (§13.12)
and is now spent by `cp_flush_close_x`, with the write half that reads it
living in `CTRL.DRV` beside the page. The flag IS the observable here, and
that is not a convenience: `clk_rtc_write` is module code now, loaded into a
heap claim at whatever address the heap gives it, so there is no static
address left to put a breakpoint on.

  1  step a field         -> [clk_dirty] = 1 and [cp_wdirty] = 1
  2  hold the panel open  -> STILL 1 after ~54 system ticks
  3  close the panel      -> 0

**Leg 2 is the whole test.** On the old kernel ui_task drained the flag inside
55 ms, so it would read 0 long before any close; on the new one nothing but
the close may touch it. Leg 1's second half is the other half of the change
and fails just as quietly: the `+`/`-` path did not use to mark the PANEL
dirty, and `cpf_cp_flush_close` early-outs on `[cp_wdirty]` before it loads
the module - so a clock-only edit would close, load nothing, and write no
chip, with every leg but that one still passing.

WHAT THIS CANNOT REACH is any rung of the ladder. An IBM 5150 has no RTC -
the MC146818 arrived with the AT - and MartyPC models no XT clock card, so
`[clk_tier]` is 0 here and `clk_rtc_write` returns at its `[clk_rtc]` test.
The rungs' own writers are exercised under QEMU, which has an MC146818 and
nothing else (docs/TESTING.md): set the clock, close the panel, `system_reset`
and read the menu bar - the value survives the reboot only if rung 1 really
reached the chip. That is a screenshot session rather than a row, and it is
the one case where the closed list's reasoning points at QEMU because MartyPC
lacks the hardware entirely, the way it lacks a NIC.

Driving the page: see tests/dtfield.py's docstring. `M.settle` cannot return
on it and `m.advance` leaves the guest paused; both apply here.
"""
import sys

sys.path.insert(0, "tools")
import os88marty as M
from os88mouse import Mouse
from os88geom import WIN_SIZE, MAX_WIN

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
W_FLAGS, W_X, W_Y, W_W, W_H, W_TITLE = 0, 2, 4, 6, 8, 10
TITLE_H = 18

# ctrl.inc's geometry, mirrored deliberately (SPEC.md 31.1 and 31.5).
CP_IX, CP_I0Y, CP_IROWH, CP_RX = 6, 6, 14, 96
CP_ITIME = 1
CPT_BX, CPT_BW, CPT_BUY, CPT_BH = 150, 28, 22, 18

fails = []


def check(name, cond, note=""):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} {note}")
    if not cond:
        fails.append(name)


def _u16(b, o):
    return b[o] | (b[o + 1] << 8)


def panel(m):
    want = m.sym("cp_ttl") - (M.KERNEL_SEG << 4)
    blob = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    for i in range(MAX_WIN):
        r = blob[i * WIN_SIZE:(i + 1) * WIN_SIZE]
        if _u16(r, W_FLAGS) & 2 and _u16(r, W_TITLE) == want:
            return tuple(_u16(r, o) for o in (W_X, W_Y, W_W, W_H))
    return None


with M.launch("build/os8088-360.img", apps="build/apps360.img",
              machine=MACHINE) as m:
    M.settle(m)
    mo = Mouse(marty=m)

    def dirty():
        return m.read(m.sym("clk_dirty"), 1)[0]

    def wdirty():
        return m.read(m.sym("cp_wdirty"), 1)[0]

    print(f"== {MACHINE} : the close spends the RTC write (37.94) ==")
    mo.menu(8, 8, 8, 40)
    M.settle(m)
    w = panel(m)
    check("the Control Panel opened", w is not None)
    if not w:
        sys.exit("no panel")

    hide = m.read(m.sym("cp_hide"), 1)[0]
    row = sum(1 for r in range(CP_ITIME) if not (hide & (1 << r)))
    mo.click(w[0] + 1 + CP_IX + 30,
             w[1] + TITLE_H + 1 + CP_I0Y + row * CP_IROWH + CP_IROWH // 2)
    m.advance(frames=30)
    m.run()
    check("nothing is owed before the edit", dirty() == 0 and wdirty() == 0,
          f"([clk_dirty]={dirty()} [cp_wdirty]={wdirty()})")

    # --- 1: the arrow posts BOTH flags ------------------------------------
    px, py = w[0] + 1 + CP_RX, w[1] + TITLE_H + 1
    mo.click(px + CPT_BX + CPT_BW // 2, py + CPT_BUY + CPT_BH // 2, settle=0.4)
    m.advance(frames=20)
    m.run()
    check("the step posts [clk_dirty]", dirty() == 1, f"({dirty()})")
    check("...and [cp_wdirty], or the close would not load the module",
          wdirty() == 1, f"({wdirty()})")

    # --- 2: many ticks pass and NOBODY drains it --------------------------
    # 60 frames of a 60Hz card is one guest second, so this is ~54 system
    # ticks against a drain that used to fire on the first.
    m.advance(frames=180)
    m.run()
    check("three guest seconds on, the tick has NOT drained it",
          dirty() == 1, f"([clk_dirty] = {dirty()}, ~54 ticks)")

    # --- 3: the close spends it -------------------------------------------
    w = panel(m)
    mo.click(w[0] + 10, w[1] + TITLE_H // 2, settle=0.4)      # the close box
    m.advance(frames=120)
    m.run()
    check("the panel's CLOSE spent it", dirty() == 0, f"({dirty()})")
    check("...and the panel is gone", panel(m) is None)

print()
if fails:
    print(f"{len(fails)} FAILED: {', '.join(fails)}")
    sys.exit(1)
print("all pass")
