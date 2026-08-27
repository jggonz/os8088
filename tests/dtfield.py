#!/usr/bin/env python3
"""SPEC.md 37.93: the Date/Time field editor, run from inside CTRL.DRV.

    python3 tests/dtfield.py [machine]

`clk_fld_str`, `clk_fld_adj` and `clk_step` were `.text` and are now `.modc`,
so they execute at a segment of their own with `DS` still `KERNEL_SEG` and
reach five formatters plus `clk_mlen` through the `cw_clk_*` thunks. That move
had no behavioural test at all - `tools/os88ovlchk.py` proves no call crosses
near and nothing proved the editor still edits.

WHAT A NEAR/FAR MISTAKE ACTUALLY LOOKS LIKE is why this asserts values rather
than pixels. `ret` pops two bytes and `retf` four; a wrong one does not fault,
it resumes at whatever the next word on the stack names. A page that had lost
its editor entirely would still paint, and the fields would simply stop
moving - so every leg here reads a KERNEL BYTE back.

  1  the month steps and wraps                     clk_fld_adj + clk_step
  2  [clk_dirty] is posted from inside the module  the byte (SPEC.md 37.94)
  3  30 Jan + one month lands on 28/29 Feb         cw_clk_mlen, the far call
  4  the year steps down and floors at CLK_YMIN    clk_step's signed wrap
  5  the hour steps 0..23                          the stored form, not 12h
  6  the field TEXT matches the field's value      clk_fld_str + cw_clk_put2

3 is the load-bearing one. The day clamp is the only thing in `clk_fld_adj`
that leaves the image, so a broken thunk shows up there and nowhere else:
every other leg is arithmetic the module does by itself.

TWO THINGS ABOUT DRIVING THIS PAGE, both of which cost an afternoon:

  * `M.settle` CANNOT RETURN ON IT. The page redraws once a second (SPEC.md
    31.5), and settle waits for two identical frames a second apart - so it
    runs to its 120s limit and reports a machine that never finished booting.
    Everything after the page is selected runs on `m.advance(frames=)`, which
    is guest time and deterministic besides.
  * `advance` STOPS THE GUEST. Without an `m.run()` after it the next mouse
    move never arrives and `os88mouse` reports the target as one it cannot
    reach - which reads as a coordinate bug several frames from the cause.
"""
import sys

sys.path.insert(0, "tools")
import os88marty as M
from os88mouse import Mouse
from os88geom import WIN_SIZE, MAX_WIN

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
W_FLAGS, W_X, W_Y, W_W, W_H, W_TITLE = 0, 2, 4, 6, 8, 10
TITLE_H = 18

# ctrl.inc's geometry, mirrored deliberately (a gate that derives it from the
# kernel is testing the kernel against itself) - SPEC.md 31.1 and 31.5.
CP_IX, CP_I0Y, CP_IROWH, CP_RX = 6, 6, 14, 96
CP_ITIME = 1                                    # cp_items' Date/Time record
CPT_FX, CPT_DY, CPT_TY = 8, 26, 48
CPT_MONW, CPT_NUMW, CPT_YRW = 24, 16, 32
CPT_BX, CPT_BW, CPT_BUY, CPT_BDY, CPT_BH = 150, 28, 22, 44, 18
CLK_YMIN, CLK_YMAX = 1980, 2099

# field index -> (x, width, y), pane-relative. The index IS the number
# clk_fld_str/clk_fld_adj take (SPEC.md 31.5's cp_tflds).
FIELDS = [(CPT_FX, CPT_MONW, CPT_DY), (CPT_FX + 32, CPT_NUMW, CPT_DY),
          (CPT_FX + 56, CPT_YRW, CPT_DY), (CPT_FX, CPT_NUMW, CPT_TY),
          (CPT_FX + 24, CPT_NUMW, CPT_TY), (CPT_FX + 48, CPT_NUMW, CPT_TY)]
MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]

fails = []


def check(name, cond, note=""):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} {note}")
    if not cond:
        fails.append(name)


def _u16(b, o):
    return b[o] | (b[o + 1] << 8)


def panel(m):
    """The Control Panel window, by its title pointer."""
    want = m.sym("cp_ttl") - (M.KERNEL_SEG << 4)
    blob = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    for i in range(MAX_WIN):
        r = blob[i * WIN_SIZE:(i + 1) * WIN_SIZE]
        if _u16(r, W_FLAGS) & 2 and _u16(r, W_TITLE) == want:
            return tuple(_u16(r, o) for o in (W_X, W_Y, W_W, W_H))
    return None


def clock(m):
    """The live fields, in one read - they are laid out adjacent (SPEC.md 37)."""
    b = m.read(m.sym("clk_sec"), 10)
    return dict(sec=b[0], min=b[1], hour=b[2], day=b[3], mon=b[4],
                year=b[8] | (b[9] << 8))


def fbuf(m):
    """[clk_fbuf] - the LAST field clk_fld_str rendered, NUL-terminated."""
    return bytes(m.read(m.sym("clk_fbuf"), 6)).split(b"\0")[0].decode("latin-1")


with M.launch("build/os8088-360.img", apps="build/apps360.img",
              machine=MACHINE) as m:
    M.settle(m)
    mo = Mouse(marty=m)
    print(f"== {MACHINE} : the Date/Time editor from inside CTRL.DRV (37.93) ==")

    # The chip menu's Control Panel item. menu, not click: a menu cannot be
    # opened with a click (os88mouse's own note).
    mo.menu(8, 8, 8, 40)
    M.settle(m)
    w = panel(m)
    check("the Control Panel opened", w is not None,
          "" if w else "(the module's entry table is the first suspect)")
    if not w:
        sys.exit("no panel")

    # Select Date/Time. Its DRAWN row is the number of shown records above it
    # ([cp_hide] is a bit per record) - and this click is where the settles
    # STOP, because it is what puts the animating page on screen.
    hide = m.read(m.sym("cp_hide"), 1)[0]
    row = sum(1 for r in range(CP_ITIME) if not (hide & (1 << r)))
    mo.click(w[0] + 1 + CP_IX + 30,
             w[1] + TITLE_H + 1 + CP_I0Y + row * CP_IROWH + CP_IROWH // 2)

    px, py = w[0] + 1 + CP_RX, w[1] + TITLE_H + 1

    def tick():
        m.advance(frames=20)                # guest time, not wall time...
        m.run()                             # ...and advance leaves it PAUSED

    tick()                                  # NOT settle: the click above put
                                            # the animating page on screen

    def select(i):
        x, wide, y = FIELDS[i]
        mo.click(px + x + wide // 2, py + y + 4, settle=0.4)
        tick()

    def step(up=True):
        mo.click(px + CPT_BX + CPT_BW // 2,
                 py + (CPT_BUY if up else CPT_BDY) + CPT_BH // 2, settle=0.4)
        tick()

    print(f"  clock on arrival: {clock(m)}")

    # --- 1/2: the month steps, and the module posts [clk_dirty] -----------
    # This USED to breakpoint clk_rtc_write, because ui_task drained the flag
    # on the tick and reading the byte was a race against that drain. SPEC.md
    # 37.94 changed both halves of that: nothing spends [clk_dirty] until the
    # panel closes, so the byte is now the honest observable - and the
    # breakpoint is no longer possible anyway, since clk_rtc_write moved into
    # CTRL.DRV and os88sym refuses a `.modc` symbol rather than handing back a
    # KERNEL_SEG address that would be wrong.
    select(0)
    before = clock(m)["mon"]
    step(True)
    now = clock(m)["mon"]
    posted = m.read(m.sym("clk_dirty"), 1)[0]
    check("the month steps", now == before % 12 + 1,
          f"({before} -> {now})")
    check("...and clk_fld_adj posted [clk_dirty] from inside the module",
          posted == 1, f"({posted})")

    # --- 3: the day clamp, which is the ONLY far call out of clk_fld_adj --
    # Park on the 30th of January, then step the month. clk_fld_adj asks
    # cw_clk_mlen how long February is and re-clamps; a broken thunk leaves
    # the day at 30 or returns rubbish, and nothing else on this page would
    # notice.
    select(1)
    for _ in range(40):
        if clock(m)["day"] == 30:
            break
        step(True)
    select(0)
    for _ in range(13):
        if clock(m)["mon"] == 1:
            break
        step(True)
    pre = clock(m)
    step(True)                                          # January -> February
    post = clock(m)
    want = 29 if post["year"] % 4 == 0 else 28
    check("30 Jan + one month clamps to February's length (cw_clk_mlen)",
          post["mon"] == 2 and post["day"] == want,
          f"({pre['day']}/{pre['mon']} -> {post['day']}/{post['mon']}, "
          f"wanted {want}/2)")

    # --- 4: the year, down, and its floor ---------------------------------
    select(2)
    y0 = clock(m)["year"]
    step(False)
    y1 = clock(m)["year"]
    check("the year steps down and wraps at the range's floor",
          y1 == (y0 - 1 if y0 > CLK_YMIN else CLK_YMAX), f"({y0} -> {y1})")

    # --- 5: the hour is STORED 0..23 (SPEC.md 37) -------------------------
    select(3)
    h0 = clock(m)["hour"]
    step(True)
    h1 = clock(m)["hour"]
    check("the hour steps 0..23", h1 == (h0 + 1) % 24, f"({h0} -> {h1})")

    # --- 6: clk_fld_str ran, and cw_clk_put2 answered ---------------------
    # cp_time_rows redraws EVERY field once a second, so [clk_fbuf] holds the
    # last one it drew - the seconds - rather than the one just stepped. That
    # is what to check it against; checking it against the stepped field is
    # the trap, and it reads as a rendering bug.
    # PAUSE FIRST. The page re-renders every second, so two separate reads can
    # straddle a tick: [clk_sn_sec] read at 44 against a [clk_fbuf] the redraw
    # has already moved to '45'. That is a race in the HARNESS - the kernel is
    # doing exactly what it should - and it passed twice before it failed, so
    # it is the kind that gets diagnosed as a rendering bug. Reading the pair
    # with the guest stopped makes them one instant, which is what the
    # assertion is about; a +/-1 tolerance would not be, and would also pass a
    # clk_fld_str that had stopped rendering the seconds at all.
    m.pause()
    sec = m.read(m.sym("clk_sn_sec"), 1)[0]
    shown = fbuf(m)
    m.run()
    check("clk_fld_str renders the field it drew last (cw_clk_put2)",
          shown == "%02d" % sec, f"({shown!r} against second {sec})")

    # --- and the page is still alive, which the module ABI decides --------
    check("...and the panel is still up and drawing", panel(m) is not None)

print()
if fails:
    print(f"{len(fails)} FAILED: {', '.join(fails)}")
    sys.exit(1)
print("all pass")
