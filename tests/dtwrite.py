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
              machine=MACHINE, boot=False) as m:
    m.run()
    M.kernel_up(m)
    mo = Mouse(marty=m)

    def dirty():
        return m.read(m.sym("clk_dirty"), 1)[0]

    def wdirty():
        return m.read(m.sym("cp_wdirty"), 1)[0]

    print(f"== {MACHINE} : the close spends the RTC write (37.94) ==")
    mo.menu(8, 8, 8, 40)
    M.ui_done(m)                # every read below is guest state
    w = panel(m)
    check("the Control Panel opened", w is not None)
    if not w:
        sys.exit("no panel")

    hide = m.read(m.sym("cp_hide"), 1)[0]
    row = sum(1 for r in range(CP_ITIME) if not (hide & (1 << r)))
    mo.click(w[0] + 1 + CP_IX + 30,
             w[1] + TITLE_H + 1 + CP_I0Y + row * CP_IROWH + CP_IROWH // 2)
    M.ui_done(m)
    check("nothing is owed before the edit", dirty() == 0 and wdirty() == 0,
          f"([clk_dirty]={dirty()} [cp_wdirty]={wdirty()})")

    # THE CHIP ITSELF, on kern_mega's Bochs: it HAS an MC146818 (rung 1),
    # so what the close is for can be asserted on the hardware - the stepped
    # field reaches the CMOS at the close and NOT before. MartyPC models no
    # clock chip, so there this half is not asked at all.
    chip = getattr(m, "cmos", None)

    def kclock():
        b = m.read(m.sym("clk_sec"), 9)       # tests/dtfield.py's clock()
        return dict(min=b[1], hour=b[2], day=b[3], mon=b[4],
                    year=b[7] | (b[8] << 8))

    def bcd(v):
        return (v >> 4) * 10 + (v & 15)

    def rtc():
        c = chip()
        yy = bcd(c[9])
        year = bcd(c[0x32]) * 100 + yy
        # A STALE CENTURY BYTE composes a year that does not exist here:
        # 86Box's mr286 keeps the 19 its BIOS defaulted to while the chip
        # follows the host to 26, and kernel/clock.inc's clk_yr_join windows
        # the two digits for exactly that case - so the chip is read the way
        # the kernel reads it (and the close's write puts 20 back)
        if not 1980 <= year <= 2099:
            year = (1900 if yy >= 80 else 2000) + yy
        return dict(min=bcd(c[2]), hour=bcd(c[4]), day=bcd(c[7]),
                    mon=bcd(c[8]), year=year)
    k0 = kclock() if chip else None
    if chip:
        # THE YEAR, not the default month: Bochs's MC146818 RACES while the
        # debugger holds the guest (measured: hours of chip time per stop,
        # with the kernel's own clock exact), so over a row's hundred reads
        # the chip's day rolls - and near a month's end its month would too.
        # A year only rolls at New Year. Field 2 is tests/dtfield.py's.
        mo.click(w[0] + 1 + CP_RX + 8 + 56 + 16, w[1] + TITLE_H + 1 + 26 + 4,
                 settle=0)
        M.ui_done(m)

    # --- 1: the arrow posts BOTH flags ------------------------------------
    px, py = w[0] + 1 + CP_RX, w[1] + TITLE_H + 1
    mo.click(px + CPT_BX + CPT_BW // 2, py + CPT_BUY + CPT_BH // 2, settle=0)
    M.ui_done(m)
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

    if chip:
        k1 = kclock()
        moved = [f for f in ("day", "mon", "year") if k1[f] != k0[f]]
        f = moved[0] if moved else "year"
        r1 = rtc()
        check("the step moved the year, and only the year, in the kernel's "
              "clock", moved == ["year"], f"({k0} -> {k1})")
        check("...and the CHIP still holds the old one before the close",
              r1[f] == k0[f], f"(CMOS {f} = {r1[f]}, was {k0[f]}, "
              f"stepped to {k1[f]})")

    # --- 3: the close spends it -------------------------------------------
    w = panel(m)
    mo.click(w[0] + 10, w[1] + TITLE_H // 2, settle=0)        # the close box
    M.ui_done(m)                # the close's module load and write included
    check("the panel's CLOSE spent it", dirty() == 0, f"({dirty()})")
    check("...and the panel is gone", panel(m) is None)
    if chip:
        k2, r2 = kclock(), rtc()
        check("the close WROTE the chip: its year is the panel's",
              r2[f] == k1[f] == k2[f], f"(CMOS {f} {r2[f]}, kernel {k2[f]}, "
              f"stepped to {k1[f]})")

print()
if fails:
    print(f"{len(fails)} FAILED: {', '.join(fails)}")
    sys.exit(1)
print("all pass")
