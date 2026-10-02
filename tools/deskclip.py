#!/usr/bin/env python3
"""Price a reveal repaint PHASE BY PHASE, and say which windows it repainted

    python3 tools/deskclip.py <scenario> [--machine M] [--runs N] [--png DIR]
                              [--detail] [--parked tm] [--image IMG]

    kern_small: OS88_BUILD=<tree>/smallk OS88_DEFINES=KERN_SMALL \\
                python3 tools/deskclip.py drag --image ../small360.img

    scenarios:  drag    a window dragged over the right-hand cell column, with
                        a second window parked partly over a cell
                        (--parked tm parks the Task Manager instead)
                cell    a cell repainted in place (a mount, a medium change)
                        with a window parked partly over it
                close   a window closed whose frame reaches a cell that a
                        second window sits over (tests/zonedmg.py's layout)
                plain   a small window moved over plain desktop beside two
                        others - the dither-only control, no cell touched

The instrument docs/plans/completed/DESK-CLIP-PLAN.md section 2 was measured
with, and what tests/deskclip.py drives. It
exists because a reveal repaint (`wm_paint_dmg`, SPEC.md 11.91) is FIVE jobs
in one call - the ground, the desktop cells, the dock and bar, the windows,
the promotion - and the question that plan asks ("what do the cells cost, and
what do they make the WINDOWS cost?") cannot be answered by a total.

It arms `bp_trace` (tools/os88marty.py) on the phase boundaries, on every
primitive entry a reveal reaches and on `wm_draw_win`, and reads the guest's
cycle counter at each stop, so every number is exact guest time on a
cycle-accurate 4.77 MHz 8088 and comparable between builds. A primitive is
counted at its PUBLIC entry (`gfx_fill` and friends, `font_run_x`, `font_char`,
`ico_core`), so `gfx_hline`/`gfx_vline`/`gfx_frame` count as the fills they
funnel into; a FRAGMENT is an entry to the raw body under the clip hook
(`gfx_fill_d`, `gfx_fill_gray_d`), which is one per call unclipped and one
per surviving fragment armed.

As os88span.py does, the gesture is driven with nothing armed and only the
packet that starts the repaint - the RELEASE - is sent with the breakpoints
up: os88mouse converges by reading the guest, and a 1200-baud UART drops
packets sent back to back.

`$OS88_BUILD` / `$OS88_DEFINES` select another kernel (a prototype in a
private tree, or kern_small) exactly as they do for every os88ui row.
"""
import argparse
import os
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import os88marty            # noqa: E402
import os88ui               # noqa: E402
import os88geom as geom     # noqa: E402

HZ = os88marty.GUEST_HZ
PHASES = [("wm_paint_dmg", "setup"), ("wm_dmg_gray", "ground"),
          ("desk_paint_mask_x", "cells"), ("dock_paint", "dock+bar"),
          ("wm_dmg_wins", "windows"), ("wm_dmg_wins.wdone", "promote")]
PRIMS = ["gfx_fill", "gfx_fill_d", "gfx_fill_gray", "gfx_fill_gray_d",
         "gfx_fill_pat", "gfx_xor_fill", "font_run_x", "font_char", "ico_core",
         "gfx_restore", "gfx_blit1", "gfx_blit4"]
MARKS = ["wm_paint_dmg.out", "desk_draw_zone", "wm_draw_win", "wm_dw_cull"]
CALLS = ["gfx_fill", "gfx_fill_gray", "gfx_fill_pat", "gfx_xor_fill",
         "font_run_x", "font_char", "ico_core", "gfx_restore", "gfx_blit1",
         "gfx_blit4"]


def ms(c):
    return 1000.0 * c / HZ


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def s16(b, i=0):
    v = u16(b, i)
    return v - 0x10000 if v & 0x8000 else v


# --- geometry ---------------------------------------------------------------

def cell_rect(m, ordinal):
    """desk_cell_rect (SPEC.md 26.9): the cell plus the caption overhang."""
    import os88sym
    eq = os88sym.equates()
    rows = geom.word(m, "desk_rows")
    col, row = divmod(ordinal, rows)
    x = geom.word(m, "vid_desk_zx") - col * eq["DESK_PX"]
    y = eq["DESK_ZY0"] + row * geom.word(m, "desk_zstep")
    return (x - eq["DESK_ZOVER"], y, x + eq["DESK_CW"] + eq["DESK_ZOVER"] - 1,
            y + geom.word(m, "desk_zh1"))


def occ(w):
    return (w.x, w.y, w.x + w.w, w.y + w.h)


def isect(a, b):
    r = (max(a[0], b[0]), max(a[1], b[1]), min(a[2], b[2]), min(a[3], b[3]))
    return r if r[0] <= r[2] and r[1] <= r[3] else None


def shrink(ui, w, dx, dy):
    """Resize a window by dragging its grow box (no os88ui verb resizes)."""
    ui.raise_window(w)
    w = ui._refresh(w)
    gx, gy = w.x + w.w - 4, w.y + w.h - 4
    ui.mo.drag(gx, gy, gx + dx, gy + dy)
    ui.settle()
    return ui._refresh(w)


# --- the armed gesture -------------------------------------------------------

def armed(ui, trigger, quiet=3.0, first=40.0, names=None):
    """Run `trigger` with every mark armed; collect until the guest goes quiet.

    `names` narrows the set - a gate that asks only WHICH windows were drawn
    arms `wm_paint_dmg`, its `.out` and `wm_draw_win`, and runs at nearly the
    guest's own speed instead of stopping at every primitive."""
    m = ui.m
    if names is None:
        names = [p[0] for p in PHASES] + MARKS + PRIMS
    wins = m.sym("wm_wins")
    su = m.sym("wm_su_son")

    def on_hit(mm, rec):
        if rec["name"] == "wm_draw_win":
            r = rec.get("regs", {})
            slot = ((r.get("bx", 0) + 0x600) - wins) // geom.WIN_SIZE
            b = mm.read(su, 10)
            return {"slot": slot, "son": u16(b, 0),
                    "sub": (s16(b, 2), s16(b, 4), s16(b, 6), s16(b, 8))}
        if rec["name"] == "desk_draw_zone":
            return {"zone": rec.get("regs", {}).get("ax", 0) & 0xFF}
        return None

    with os88marty.bp_trace(m, *names, regs=True, cap=60000,
                            on_hit=on_hit) as tr:
        trigger()
        per = HZ * (os88marty.GUEST_PACE or 4.5)
        cyc = lambda: int(m.status()["cycles"])     # noqa: E731
        # QUIET IS MEASURED FROM THE LAST PASS'S END, not from the last hit:
        # a Task Manager on its performance page draws every TM_INT for ever
        # (SPEC.md 28.11.2), so "no hits at all" never arrives with one open
        c0 = cyc()
        while True:
            if tr.error is not None:
                raise tr.error
            now = cyc()
            outs = [h for h in tr.hits if h["name"] == "wm_paint_dmg.out"]
            ins = [h for h in tr.hits if h["name"] == "wm_paint_dmg"]
            if outs and len(outs) >= len(ins):
                if (now - outs[-1]["cycles"]) / per > quiet:
                    break
            elif not ins and (now - c0) / per > first:
                break
            time.sleep(0.05)
    if tr.overflowed:
        raise SystemExit("deskclip: the trace overflowed its cap")
    return tr.hits


def passes(hits):
    """Split a trace into wm_paint_dmg passes, entry to `.out`."""
    out, cur = [], None
    for h in hits:
        if h["name"] == "wm_paint_dmg":
            cur = [h]
        elif cur is not None:
            cur.append(h)
            if h["name"] == "wm_paint_dmg.out":
                out.append(cur)
                cur = None
    return out


def analyse(p):
    """One pass -> its phases, its cells and its windows, counted."""
    t0, t1 = p[0]["cycles"], p[-1]["cycles"]
    res = {"total": t1 - t0, "phases": [], "zones": [], "wins": [],
           "counts": {}}
    bounds = [(h["cycles"], lbl) for h in p
              for nm, lbl in PHASES if h["name"] == nm]
    # the first of each boundary only (wm_dmg_wins is reached once a pass)
    firsts, seen = [], set()
    for c, lbl in bounds:
        if lbl not in seen:
            seen.add(lbl)
            firsts.append((c, lbl))
    firsts.append((t1, "end"))
    for (c, lbl), (c2, _) in zip(firsts, firsts[1:]):
        cnt = {}
        for h in p:
            if c <= h["cycles"] < c2 and h["name"] in PRIMS:
                cnt[h["name"]] = cnt.get(h["name"], 0) + 1
        res["phases"].append((lbl, c2 - c, cnt))
    for h in p:
        if h["name"] in PRIMS:
            res["counts"][h["name"]] = res["counts"].get(h["name"], 0) + 1
        if h["name"] == "desk_draw_zone":
            res["zones"].append(h["hit"]["zone"])
    # windows: each wm_draw_win to the next one, or to .wdone
    wd = [i for i, h in enumerate(p) if h["name"] == "wm_draw_win"]
    end = next((i for i, h in enumerate(p) if h["name"] == "wm_dmg_wins.wdone"),
               len(p) - 1)
    for k, i in enumerate(wd):
        j = wd[k + 1] if k + 1 < len(wd) else end
        if j < i:
            j = len(p) - 1
        seg = p[i:j]
        cnt = {}
        for h in seg:
            if h["name"] in PRIMS:
                cnt[h["name"]] = cnt.get(h["name"], 0) + 1
        res["wins"].append({"slot": p[i]["hit"]["slot"],
                            "son": p[i]["hit"]["son"],
                            "sub": p[i]["hit"]["sub"],
                            "paint": any(h["name"] == "wm_dw_cull" for h in seg),
                            "cycles": p[j]["cycles"] - p[i]["cycles"],
                            "cnt": cnt})
    return res


def calls(cnt):
    return sum(cnt.get(n, 0) for n in CALLS)


def fmt(cnt):
    keys = ["gfx_fill", "gfx_fill_d", "gfx_fill_gray", "gfx_fill_gray_d", "gfx_fill_pat",
            "font_run_x", "font_char", "ico_core", "gfx_restore", "gfx_blit1",
            "gfx_blit4"]
    ab = {"gfx_fill": "fill", "gfx_fill_d": "frag", "gfx_fill_gray": "gray",
          "gfx_fill_gray_d": "gfrag", "gfx_fill_pat": "pat",
          "font_run_x": "run", "font_char": "chr",
          "ico_core": "ico", "gfx_restore": "rst", "gfx_blit1": "b1",
          "gfx_blit4": "b4"}
    return " ".join("%s=%d" % (ab[k], cnt[k]) for k in keys if cnt.get(k))


def report(res, titles):
    print("  pass: %.2f ms (%d cycles), %d primitive calls"
          % (ms(res["total"]), res["total"], calls(res["counts"])))
    for lbl, c, cnt in res["phases"]:
        print("    %-9s %8.2f ms  %3d calls  %s"
              % (lbl, ms(c), calls(cnt), fmt(cnt)))
    if res["zones"]:
        print("    cells drawn: %r" % (res["zones"],))
    for w in res["wins"]:
        print("    window %d %-10s %8.2f ms  %3d calls  %s  %s%s"
              % (w["slot"], titles.get(w["slot"], "?")[:10], ms(w["cycles"]),
                 calls(w["cnt"]), "W_PAINT" if w["paint"] else "cache",
                 ("owed %r " % (w["sub"],)) if w["son"] else "owed WHOLE ",
                 fmt(w["cnt"])))


def detail(p):
    """The cells phase, stop by stop: what each primitive cost to the next."""
    a = next((i for i, h in enumerate(p) if h["name"] == "desk_paint_mask_x"),
             None)
    b = next((i for i, h in enumerate(p) if h["name"] == "dock_paint"), None)
    if a is None or b is None:
        return
    for h, n in zip(p[a:b], p[a + 1:b + 1]):
        print("      %-18s %8.2f ms" % (h["name"], ms(n["cycles"] - h["cycles"])))


def shot(m, path):
    w, h, d = m.fbuf(None)
    os88marty.write_png_rgb(path, w, h, d)


# --- the scenarios -----------------------------------------------------------
#
# Each sets the desktop up with nothing armed and returns (trigger, check):
# `trigger` sends the one packet that starts the repaint being priced, and
# `check` confirms afterwards that the gesture did what it was meant to.

def drag_setup(ui, dx, dy, w):
    """move_window up to (not including) the release."""
    ui.raise_window(w)
    w = ui._refresh(w)
    gx, gy = w.x + w.w // 2, w.y + geom.TITLE_H // 2
    ui._grab(w, gx, gy)
    ui.mo.to(gx + dx, gy + dy, l=True)
    return lambda: ui.m.mouse(0, 0, l=False)


def sc_drag(ui):
    """B:'s window parked partly over B:'s cell (its right edge across the
    cell's left half, its top across the cell); A:'s window, SHRUNK, dragged
    from the left of the screen to land over the right-hand column - over
    A:'s cell and the top of B:'s, and over the parked window's corner. The
    drag's vacated rect never touches the parked window, so 11.91.2 has
    nothing to mark it for: the cells are the only reason it repaints. (The
    mover is shrunk so that on a 640-wide screen too its VACATED rect is clear
    of the parked window, and so that it does not cover the parked window
    outright - wm_covered would then skip it on any kernel.)"""
    m = ui.m
    cb = cell_rect(m, geom.drive_ordinal(m, "B"))
    a = ui.open_drive("A")
    if PARKED == "tm":
        # the Task Manager: a window whose content is not a raise-cache
        # restore, so a window marked for nothing pays its W_PAINT
        b = ui.path("A:/SYSTEM/TASKMGR.O88")
        a = ui.open_drive("B")
        # ...and the folder window it was opened from goes LEFT, clear of the
        # Task Manager: overlapping it would mark the Task Manager
        # TRANSITIVELY (SPEC.md 11.91.3), which is not the cells' doing
        for o in ui.windows():
            if o.title == "SYSTEM":
                ui.move_window(o, 0, 150)
    else:
        b = ui.open_drive("B")
        a = shrink(ui, a, -128, -100)
    b = ui.move_window(b, cb[0] + 48 - b.w, cb[1] + 24)
    a = ui.move_window(a, 0, 24)
    sw = geom.word(m, "vid_w")
    tx = sw - a.w - 4
    trig = drag_setup(ui, tx - a.x, 0, a)
    over = isect(occ(b), cb)
    LAST["spared"] = (b.i, over)
    print("  parked %r %r over B:'s cell at %r; mover %r dragged %d px right"
          % (b.title, (b.x, b.y, b.w, b.h), over, (a.x, a.y, a.w, a.h),
             tx - a.x))
    return trig, lambda: ui._refresh(a).x != a.x


def sc_close(ui):
    """tests/zonedmg.py's layout: W (B:'s window, shrunk) with its corner over
    B:'s cell, V (A:'s window) below it with its top edge across the cell's
    bottom; V is CLOSED. Its frame reaches the cell and not W."""
    m = ui.m
    cell = cell_rect(m, geom.drive_ordinal(m, "B"))
    v = ui.open_drive("A")
    w = ui.open_drive("B")
    w = shrink(ui, w, -200, -150)
    w = ui.move_window(w, cell[0] + 40 - (w.w - 1), 20)
    v = ui.move_window(v, geom.word(m, "vid_w") - v.w - 10, w.y + w.h + 6)
    ui.mo.to(4, 4)
    ui.settle()
    LAST["spared"] = (w.i, isect(occ(w), cell))
    print("  W %r over the cell at %r; V %r"
          % ((w.x, w.y, w.w, w.h), isect(occ(w), cell), (v.x, v.y, v.w, v.h)))
    ui.raise_window(v)
    v = ui._refresh(v)
    x, y = geom.close_xy(v.x, v.y)
    ui.mo.to(x, y)
    ui.mo._edge(True)
    gone = lambda: all(o.i != v.i for o in ui.windows())   # noqa: E731
    return (lambda: ui.m.mouse(0, 0, l=False)), gone


def sc_plain(ui):
    """Two Disk windows shrunk and parked side by side top-left, clear of
    every cell; a third (Calculator) moved across plain desktop below them,
    its old rect overlapping A:'s window and its new one B:'s. No cell is in
    the damage, so this is what a reveal costs with the desktop layer ONLY
    the dither - the control the cell scenarios are read against."""
    m = ui.m
    a = ui.open_drive("A")
    b = ui.open_drive("B")
    a = shrink(ui, a, -120, -110)
    b = shrink(ui, b, -120, -110)
    a = ui.move_window(a, 0, 24)
    b = ui.move_window(b, a.w + 24, 24)
    c = ui.path("B:/APPS/CALC.O88")
    c = ui.move_window(c, 40, a.y + a.h - 20)
    trig = drag_setup(ui, (b.x + 30) - c.x, 0, c)
    print("  A %r  B %r  Calc %r -> x %d"
          % ((a.x, a.y, a.w, a.h), (b.x, b.y, b.w, b.h), (c.x, c.y, c.w, c.h),
             b.x + 30))
    return trig, lambda: ui._refresh(c).x != c.x


def sc_cell(ui):
    """B:'s window parked partly over B:'s cell (sc_drag's parked window),
    and then the CELL is repainted the way a mount, a medium change or an
    item moving between cells repaints it: its bit in [desk_cdirty], posted
    (desk_cmark's three stores), so ui_task's desk_zones_paint runs one
    wm_paint_dmg with the cell's rect as the damage. No window moved."""
    m = ui.m
    cell = geom.drive_ordinal(m, "B")
    cb = cell_rect(m, cell)
    a = ui.open_drive("A")
    b = ui.open_drive("B")
    b = ui.move_window(b, cb[0] + 48 - b.w, cb[1] + 24)
    a = ui.move_window(a, 0, 24)
    ui.mo.to(4, 4)
    ui.settle()
    LAST["spared"] = (b.i, isect(occ(b), cb))
    print("  parked %r over B:'s cell (cell %d) at %r"
          % ((b.x, b.y, b.w, b.h), cell, isect(occ(b), cb)))
    dirty = m.sym("desk_cdirty") + (cell >> 3)

    def trig():
        v = m.read(dirty, 1)[0] | (1 << (cell & 7))
        m.write(dirty, bytes([v]))
        m.write(m.sym("desk_zdirty"), b"\x01")
        m.write(m.sym("ui_post"), b"\x01")
    return trig, lambda: True


PARKED = "disk"
# what the last scenario set up: "spared" = (the slot of the window that sits
# over a cell and that nothing but the cell would mark, and its rect AND the
# cell's) - tests/deskclip.py's two assertions are about exactly that window
LAST = {}
SCENARIOS = {"cell": sc_cell, "drag": sc_drag, "close": sc_close, "plain": sc_plain}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenario", choices=sorted(SCENARIOS))
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--runs", type=int, default=1)
    ap.add_argument("--png", default=None)
    ap.add_argument("--detail", action="store_true",
                    help="print the cells phase stop by stop")
    ap.add_argument("--image", default="os8088-360.img",
                    help="the system disk, under $OS88_BUILD (kern_small: "
                         "OS88_BUILD=<tree>/smallk OS88_DEFINES=KERN_SMALL "
                         "--image ../small360.img)")
    ap.add_argument("--parked", choices=("disk", "tm"), default="disk",
                    help="drag: what is parked over B:'s cell")
    a = ap.parse_args()
    global PARKED
    PARKED = a.parked
    bdir = os.environ.get("OS88_BUILD", os.path.join(ROOT, "build"))
    for run in range(a.runs):
        with os88ui.boot(os.path.join(bdir, a.image),
                         apps=os.path.join(ROOT, "build", "apps360.img"),
                         machine=a.machine) as ui:
            print("%s / %s / run %d" % (a.machine, a.scenario, run + 1))
            trig, check = SCENARIOS[a.scenario](ui)
            titles = {w.i: w.title for w in ui.windows()}
            if a.png:
                shot(ui.m, os.path.join(a.png, "%s-%s-before.png"
                                        % (a.scenario, a.machine)))
            hits = armed(ui, trig)
            ui.settle()
            if not check():
                raise SystemExit("deskclip: the gesture did not happen")
            if a.png:
                shot(ui.m, os.path.join(a.png, "%s-%s-after.png"
                                        % (a.scenario, a.machine)))
            ps = passes(hits)
            if not ps:
                raise SystemExit("deskclip: no wm_paint_dmg pass was traced")
            for p in ps:
                report(analyse(p), titles)
                if a.detail:
                    detail(p)
            verify(ui, a.png and os.path.join(a.png, "%s-%s" % (a.scenario, a.machine)))


def verify(ui, png=None, only=None):
    """The screen the damage pass left, against a forced WHOLE repaint.

    `[cp_dirty]` makes ui_task run wm_paint_all, which draws the desktop and
    then every window over it whole and so cannot get the layering wrong
    (tests/zonedmg.py's reference). A Task Manager is masked out: its
    performance page changes by construction (SPEC.md 28.11.2), and so is the
    menu bar's clock, which a slow traced run carries across a minute.

    `only` restricts the compare to one rect, which is what a GATE wants:
    the whole repaint is not a perfect reference (DESK-CLIP-PLAN 6.2 - it
    under-draws a title whose visible part spans two fragments). Answers the
    count of differing pixels."""
    m = ui.m
    ui.mo.to(4, 4)
    ui.settle()
    print("  windows after: %s" % " ".join(
        "%s(%d,%d,%d,%d)" % (w.title[:8], w.x, w.y, w.w, w.h)
        for w in ui.windows()))
    skip = [occ(w) for w in ui.windows() if w.title.startswith("Task")]
    # ...and the menu bar's CLOCK, which a long trace carries across a minute
    sw = geom.word(m, "vid_w")
    skip.append((sw - 80, 0, sw - 1, geom.TITLE_H))
    def grab():
        # SCREEN coordinates: the 1bpp framebuffer out of memory where there
        # is one (`fbuf`'s rendered Hercules aperture is not 720 wide and its
        # columns are not the screen's), the rendered card on VGA
        if m.cmd(cmd="video")["type"] == "vga":
            w, h, d = m.fbuf(None)
            return w, h, [d[3 * w * y:3 * w * (y + 1):3] for y in range(h)]
        w, h, rows = m.vram()
        return w, h, [bytes(r) for r in rows]
    w, h, got = grab()
    m.write(m.sym("cp_dirty"), b"\x01")
    ui.settle()
    _, _, ref = grab()
    if png:
        os88marty.write_png(png + "-got.png", w, h, got)
        os88marty.write_png(png + "-ref.png", w, h, ref)
    bad, boxes = 0, []
    for y in range(h):
        for x in range(w):
            if got[y][x] != ref[y][x]:
                if any(r[0] <= x <= r[2] and r[1] <= y <= r[3] for r in skip):
                    continue
                if only and not (only[0] <= x <= only[2]
                                 and only[1] <= y <= only[3]):
                    continue
                bad += 1
                for k, b in enumerate(boxes):       # cluster, 8 px slack
                    if b[0] - 8 <= x <= b[2] + 8 and b[1] - 8 <= y <= b[3] + 8:
                        boxes[k] = (min(b[0], x), min(b[1], y),
                                    max(b[2], x), max(b[3], y), b[4] + 1)
                        break
                else:
                    boxes.append((x, y, x, y, 1))
    print("  verify: %d pixels differ from a whole repaint%s"
          % (bad, "" if not bad else " in %s" % " ".join(
              "(%d,%d)-(%d,%d):%d" % b for b in boxes[:8])))
    return bad


if __name__ == "__main__":
    main()
