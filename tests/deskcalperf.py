#!/usr/bin/env python3
"""SPEC.md 110.6 / PERFORMANCE.md Part 5: what the Desk Calendar WIDGET's
gestures COST, in primitive calls and in 4.77 MHz time, and what an autostart
costs a boot.

    make && python3 tests/deskcalperf.py [--json out.json] [--leg vga|cga|boot]

A redraw is priced by how many primitive calls it makes (PERFORMANCE.md), so
that is what this counts: a breakpoint on every DRAWING cell of the API table
(OSAPI_FONT_RUN, OSAPI_GFX_FILL, OSAPI_GFX_BLIT1, OSAPI_GFX_DESK, ... - the
set is read out of apps/os88api.inc by name), and at each stop the FAR
RETURN ADDRESS read off the guest's stack, so a call is counted only when it
came from the calendar's own segment (its include, os88stik.inc, is in that
segment too: the tag and the gaps are the widget's). MartyPC is
cycle-accurate on a 4.77 MHz 8088, so the cycles between the first and the
last call of a gesture are 5150 time; a stopped guest does not count cycles.

On the VGA XT (TALL) and on the 5150's CGA (COMPACT), each off the SHIPPED
360KB disk with a state file holding two appointments today (09:00, 10:00)
and three tasks, the clock at Wed 14 Oct 2026 09:58, v2's budgets (SPEC.md
110.3) ASSERTED:

  first paint   the launch, its W_PAINT whole: <= 20 calls TALL, <= 14 COMPACT
  About down    the About card taken down by a key: stik_redrawr over the
                card's rows alone - FEWER calls than the first paint (the
                slabs those 62 rows reach, v4 review)
  day select    a press on the 15th, a day with nothing on it: <= 5 - today's
                box and the 15th's, a sub-rect each, and card 2
  month         PgDn: <= 8 - card 1 from the nav down and card 2
  tick          Space on a selected task: EXACTLY 1, its box's BLIT1
  reveal/hide   the pointer onto the tag row and away: 1 / 3 (the include's)
  idle          three Sticky App beats AND a minute boundary with nothing due:
                MUST BE ZERO - the beats counted at the package's own dc_tick
  reminder      09:00 `Standup` was missed (DUE at launch) and is dismissed;
                at 10:00 `Review` goes off: card 3's slabs, 2 calls (COMPACT:
                the slot, which the ringing reminder takes) - and the save its
                FIRED and DUE owe, counted at the drive
  missed        the save the missed Standup's marks owe, the one write a
                launch makes by itself
  uncover       a Disk window over the left half of the cards dragged clear:
                the widget's W_PAINT over what it vacated, the slabs that
                meet OSAPI_WM_DAMAGE - more than 0 and no more than the
                first paint (PERFORMANCE.md Part 5's last Sticky App row)

and on the VGA XT, two cold boots of the shipped 360KB disk - with no
SYSTEM.CFG, and with one whose trailer holds the calendar's Sticky App row - timed
from reset to `stik_boot` (the desktop painted) and to `ui_task` (the
autostart done), with the drive's own counters.

Every number is PRINTED as the table PERFORMANCE.md Part 5 and SPEC.md 110.6
quote.
"""
import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88ui      # noqa: E402
import os88marty   # noqa: E402
import os88stik    # noqa: E402
import os88sym     # noqa: E402
from deskcaledit import Cal, TITLE, disk_with, STIK_S_DIRTY  # noqa: E402
from deskcaltask import pkg_sym                 # noqa: E402

ROOT = os.path.join(HERE, "..")
BDIR = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
HZ = 4772727.0
TODAY = (2026, 10, 14)
KSEG = 0x0060
DRAWING = ("GFX_PIXEL", "GFX_HLINE", "GFX_VLINE", "GFX_FILL", "GFX_FRAME",
           "GFX_FILL_GRAY", "GFX_XOR_RECT", "GFX_XOR_FILL",
           "FONT_CHAR_XPARENT", "FONT_STR_XPARENT", "ICON_DRAW",
           "GFX_SCROLL", "GFX_BLITP", "GFX_REST", "GFX_SPANS", "GFX_BLIT4",
           "GFX_BLIT1", "GFX_FILL_PAT", "FONT_RUN", "GFX_POINTS", "GFX_DESK")

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def cells():
    """{flat address: name} for every drawing cell, out of the SDK."""
    src = open(os.path.join(ROOT, "apps", "os88api.inc")).read()
    out = {}
    for name in DRAWING:
        mm = re.search(r"^%%define OSAPI_%s\s+KERNEL_SEG:0x([0-9A-Fa-f]+)"
                       % name, src, re.M)
        if not mm:
            raise SystemExit("deskcalperf: no OSAPI_%s in os88api.inc" % name)
        out[KSEG * 16 + int(mm.group(1), 16)] = name
    return out


CELLS = cells()


def caller(m, rec):
    r = rec.get("regs") or m.regs()
    ip, cs = struct.unpack("<HH", bytes(m.read((r["ss"] << 4) + r["sp"], 4)))
    return cs


class Gesture(object):
    """The calendar's calls during one gesture: n, by name, and the span -
    from the FIRST of the calls and of the hits at `start` (dc_compose's
    entry, so the composing before the first blit is in it) to the LAST
    call's start: the last call's own blit is not in it."""

    def __init__(self, hits, seg, extra=(), start=None):
        self.mine = [h for h in hits if h["addr"] in CELLS
                     and h.get("hit") == seg]
        self.extra = dict((a, len([h for h in hits if h["addr"] == a]))
                          for a in extra)
        self.n = len(self.mine)
        self.by = {}
        for h in self.mine:
            k = CELLS[h["addr"]]
            self.by[k] = self.by.get(k, 0) + 1
        if self.mine:
            t0 = self.mine[0]["cycles"]
            if start is not None:
                st = [h["cycles"] for h in hits if h["addr"] == start]
                if st:
                    t0 = min(t0, st[0])
            self.ms = (self.mine[-1]["cycles"] - t0) * 1000.0 / HZ
        else:
            self.ms = 0.0

    def row(self):
        return {"calls": self.n, "by": self.by, "span_ms": round(self.ms, 1)}

    def __str__(self):
        return "%d calls, %.0f ms span %s" % (self.n, self.ms, self.by)


def measure(m, act, seg=None, extra=()):
    with os88marty.bp_trace(m, *(list(CELLS) + list(extra)), regs=True,
                            on_hit=lambda mm, rec: caller(mm, rec)
                            if rec["addr"] in CELLS else None) as tr:
        out = act()
    return tr.hits, out


def fixture():
    appts = [(TODAY, 540, "Standup"), (TODAY, 600, "Review")]
    tasks = [("Buy milk", False), ("Call plumber", True), ("Post letter",
                                                           False)]
    return os88stik.dat(os88stik.deskcal(appts, tasks, lead=0))


def ui_leg(tmp, machine, label, results):
    img = disk_with(tmp, "perf-%s.img" % label, fixture())
    compact = label == "cga"
    with os88ui.boot(img, machine=machine) as ui:
        m = ui.m
        m.write(ui._S("clk_sec"), bytes([0, 58, 9]))
        box = {}

        def launch():
            # bare=False: closing the folder the launch was made from would
            # UNCOVER the widget and repaint that part in the same count
            box["c"] = Cal(ui, tmp, check=check, bare=False)
            c = box["c"]
            st_off = os88stik.stik_st_off(os88stik.DESKCAL_STIK_BSS,
                                        os88stik.DESKCAL_PAYLEN)
            ui._wait(lambda: c.bss(st_off)[0] & 3 == 3,
                     "the first tick's registration", 120)
            # 09:00 Standup was missed: its FIRED and DUE marks owe the one
            # save a launch makes by itself (SPEC.md 110.5.2)
            m.disk(reset=True)
            ui._wait(lambda: not c.st() & STIK_S_DIRTY,
                     "the missed reminder's save", 60)
            box["miss"] = m.disk()
            os88marty.guest_sleep(m, 1.0)
        hits, _ = measure(m, launch)
        c = box["c"]
        seg = c.wseg
        for o in list(ui.windows()):
            if o.i != c.slot and o.visible:
                ui.close(o)
        os88marty.guest_sleep(m, 1.0)
        r = {}
        first = Gesture(hits, seg)
        print("%s: first paint      %s" % (label, first))
        r["first_paint"] = first.row()
        budget = 14 if compact else 20
        check(first.n <= budget, "%s: the first paint is <= %d calls (%d)"
              % (label, budget, first.n))
        d = box["miss"]
        print("%s: missed at launch, the save: %d reads, %d writes"
              % (label, d["reads"], d["writes"]))
        check(d["writes"] > 0, "%s: a launch that missed a reminder SAVES "
              "its marks (%d reads, %d writes)"
              % (label, d["reads"], d["writes"]))
        r["missed_save"] = {"reads": d["reads"], "writes": d["writes"]}

        tick_at = seg * 16 + pkg_sym(tmp, "dc_tick")
        comp_at = seg * 16 + pkg_sym(tmp, "dc_compose")

        def gesture(act, settle=2.0, extra=(), tag=True):
            """The calendar's calls during `act`. tag=False sets aside the
            tag row's: a PRESS reveals the handle at once (SPEC.md 110.11),
            one plate BLIT1 of the include's that every press pays when the
            handle is down, and not the gesture's own draw."""
            hits, _ = measure(m, lambda: (act(),
                                          os88marty.guest_sleep(m, settle)),
                              extra=tuple(extra) + (comp_at,))
            if not tag:
                wv = c.win()
                ty = wv.y + wv.h - os88stik.DESKCAL_GEO[c.li()]["tag"]
                hits = [h for h in hits if not (
                    h["addr"] in CELLS and h.get("regs")
                    and h["regs"]["bx"] >= ty)]
            return Gesture(hits, seg, extra=extra, start=comp_at)

        print("%s: the About card, then a key takes it down" % label)
        c.front()
        about = [(mm[0], it) for mm in ui.menus() for it, _ in mm[3]
                 if it.startswith("About Desk")]
        check(bool(about), "%s: an About item on the bar (%r)"
              % (label, [mm[0] for mm in ui.menus()]))
        ui.menu_pick(*about[0])
        os88marty.pace(m, 1.0)
        c.front()
        full = gesture(lambda: m.key("KeyX"), 3.0)
        print("%s: About down       %s" % (label, full))
        r["about_down"] = full.row()
        check(0 < full.n < first.n, "%s: the About card's take-down repaints "
              "the rows it covered, not the face (%d against the first "
              "paint's %d calls)" % (label, full.n, first.n))

        print("%s: a press on the 15th" % label)
        c.front()
        sel = gesture(lambda: ui.mo.click(*c.day_xy(15)), tag=False)
        check(c.bss(os88stik.DESKCAL_BSS_SEL)[0] == 15,
              "%s: the 15th is selected" % label)
        print("%s: day select       %s" % (label, sel))
        r["day_select"] = sel.row()
        check(sel.n <= 5, "%s: a day select is <= 5 calls - two boxes and "
              "card 2 (%d: %r)" % (label, sel.n, sel.by))

        print("%s: PgDn, a month" % label)
        mon = gesture(lambda: m.key("PageDown"))
        print("%s: month            %s" % (label, mon))
        r["month"] = mon.row()
        check(mon.n <= 8, "%s: a month is <= 8 calls (%d)" % (label, mon.n))
        c.key("KeyT")
        os88marty.pace(m, 1.5)

        print("%s: Space on a selected task" % label)
        if compact:
            while c.pane() != 1:
                c.press(c.heading())
        c.press(c.task_text(0))
        check(c.bss(os88stik.DESKCAL_BSS_SELK)[0] == 2, "%s: a task is "
              "selected" % label)
        tick = gesture(lambda: m.key("Space"))
        print("%s: tick             %s" % (label, tick))
        r["tick"] = tick.row()
        check(tick.n == 1 and tick.by == {"GFX_BLIT1": 1},
              "%s: a tick is ONE BLIT1, its box (%s)" % (label, tick))
        if compact:
            while c.pane() != 0:
                c.press(c.heading())

        print("%s: the handle revealed and hidden" % label)
        wv = c.win()
        ui.mo.to(40, 60)
        ui._wait(lambda: c.wb("stik_hv") == 0, "the handle down", 30)
        tagpt = (wv.x + 1 + 20, wv.y + wv.h - 6)
        rev = gesture(lambda: (ui.mo.to(*tagpt), ui._wait(
            lambda: c.wb("stik_tagp") == 2, "the handle", 20)), 0.5)
        hide = gesture(lambda: (ui.mo.to(40, 60), ui._wait(
            lambda: c.wb("stik_tagp") in (0, 1), "the plate", 30)), 0.5)
        print("%s: reveal / hide    %s / %s" % (label, rev, hide))
        r["reveal"], r["hide"] = rev.row(), hide.row()
        check(rev.n == 1 and hide.n <= 3, "%s: a reveal is 1 call and a hide "
              "<= 3 (%d / %d)" % (label, rev.n, hide.n))

        print("%s: idle - three beats and a minute boundary" % label)
        m.write(ui._S("clk_sec"), bytes([53, 58, 9]))

        def lnow():
            return struct.unpack("<H", bytes(c.bss(
                os88stik.DESKCAL_BSS_LNOW, 2)))[0]
        ui._wait(lambda: lnow() == 9 * 60 + 58, "a beat at 09:58", 10)
        lnow0 = lnow()
        hits, _ = measure(m, lambda: os88marty.guest_sleep(m, 8.0),
                          extra=(tick_at,))
        idle = Gesture(hits, seg, extra=(tick_at,))
        lnow1 = lnow()
        print("%s: idle             %s, %d beats, minute %d -> %d"
              % (label, idle, idle.extra[tick_at], lnow0, lnow1))
        check(idle.extra[tick_at] >= 3, "%s: three beats or more ran (%d)"
              % (label, idle.extra[tick_at]))
        check(lnow1 == lnow0 + 1, "%s: ...and one minute passed (%d -> %d)"
              % (label, lnow0, lnow1))
        check(idle.n == 0, "%s: an idle beat and a minute with nothing due "
              "draw NOTHING (%d calls)" % (label, idle.n))
        r["idle"] = dict(idle.row(), beats=idle.extra[tick_at])

        print("%s: Standup dismissed; 10:00, Review goes off" % label)
        c.key("KeyD")
        ui._wait(lambda: c.c3()[0] == 1, "card 3 to show Review NEXT", 20)
        if compact:
            while c.pane() != 0:
                c.press(c.heading())
        m.write(ui._S("clk_sec"), bytes([54, 59, 9]))
        m.disk(reset=True)
        rem = gesture(lambda: ui._wait(lambda: c.c3()[0] == 2,
                                       "Review to ring", 60), 3.0)
        ui._wait(lambda: not c.st() & STIK_S_DIRTY,
                 "the save Review's firing owed", 60)
        d = m.disk()
        print("%s: reminder         %s; the save: %d reads, %d writes"
              % (label, rem, d["reads"], d["writes"]))
        check(rem.n == 2 and rem.by == {"GFX_BLIT1": 2},
              "%s: a firing is card 3's two slabs%s (%s)"
              % (label, " - the slot, taken" if compact else "", rem))
        check(d["writes"] > 0, "%s: the firing's marks are saved (%d writes)"
              % (label, d["writes"]))
        r["reminder"] = dict(rem.row(), save_reads=d["reads"],
                             save_writes=d["writes"])

        # THE UNCOVER (PERFORMANCE.md Part 5's last Sticky App row, SPEC.md 11.103.6
        # risk 3): a widget is the BACKMOST window and has no drag cache, so
        # an application window moved off it repaints what it vacated through
        # the widget's own W_PAINT, which recomposes only the slabs that meet
        # OSAPI_WM_DAMAGE. A Disk window over the left half of the cards,
        # then dragged clear: the calendar's calls in that drag are the bill.
        print("%s: a Disk window over the widget, dragged clear" % label)
        dw = ui.open_drive("A")
        wv = c.win()
        ui.move_window(dw, wv.x - dw.w // 2, wv.y + 20)
        os88marty.guest_sleep(m, 1.0)
        dw = ui._refresh(dw)
        check(dw.x + dw.w > wv.x and dw.x < wv.x + wv.w,
              "%s: the Disk window overlaps the widget (%d..%d over %d..%d)"
              % (label, dw.x, dw.x + dw.w, wv.x, wv.x + wv.w))
        unc = gesture(lambda: ui.move_window(dw, 16, dw.y), 2.0)
        print("%s: uncover          %s" % (label, unc))
        r["uncover"] = unc.row()
        check(0 < unc.n <= first.n, "%s: the uncover repaints the widget "
              "through its slabs, no more calls than the first paint (%d "
              "against %d: %r)" % (label, unc.n, first.n, unc.by))
    results[label] = r


def boot_once(img, machine):
    with os88marty.launch(img, machine=os88marty.machine(machine),
                          boot=False) as m:
        out = {}
        for name in ("stik_boot", "ui_task"):
            m.bp_exec(name)
            m.run()
            if m.wait_stop(240.0) is None:
                raise SystemExit("deskcalperf: never reached %s" % name)
            st = m.status()
            out[name] = (int(st["cycles"]), m.disk())
        m.bp_exec()
        m.run()
    a, b = out["stik_boot"], out["ui_task"]
    return {"to_paint_ms": round(a[0] * 1000.0 / HZ),
            "autostart_ms": round((b[0] - a[0]) * 1000.0 / HZ),
            "to_ui_task_ms": round(b[0] * 1000.0 / HZ),
            "reads_after_paint": b[1]["reads"] - a[1]["reads"],
            "sectors_after_paint": b[1]["read_sectors"]
            - a[1]["read_sectors"]}


def boot_leg(tmp, results):
    plain = os.path.join(tmp, "boot-plain.img")
    shutil.copy(os.path.join(BDIR, "os8088-360.img"), plain)
    row = disk_with(tmp, "boot-row.img", fixture())
    cfg = os.path.join(tmp, "SYSTEM.CFG")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                 "os88stik.py"),
                    "cfg", "-o", cfg, "--stik",
                    "A:STIKAPPS/DESKCAL.O88=Calendar"], check=True,
                   stdout=subprocess.DEVNULL)
    subprocess.run(["mcopy", "-o", "-i", row, cfg, "::/SYSTEM.CFG"],
                   check=True, stdout=subprocess.DEVNULL,
                   env=dict(os.environ, MTOOLS_SKIP_CHECK="1"))
    b0 = boot_once(plain, "os8088_xt_vga")
    print("boot, no Sticky App row:   %r" % b0)
    b1 = boot_once(row, "os8088_xt_vga")
    print("boot, the row:      %r" % b1)
    check(b0["reads_after_paint"] == 0,
          "a boot with no Sticky App row makes NO disk call after its paint (%d)"
          % b0["reads_after_paint"])
    check(b1["reads_after_paint"] > 0, "the row's boot reads the calendar "
          "(%d calls)" % b1["reads_after_paint"])
    results["boot"] = {"no_row": b0, "row": b1}


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--json")
    ap.add_argument("--leg", choices=("all", "vga", "cga", "boot"),
                    default="all")
    a = ap.parse_args(argv)
    tmp = tempfile.mkdtemp(prefix="deskcalperf-")
    results = {}
    try:
        if a.leg in ("all", "vga"):
            ui_leg(tmp, "os8088_xt_vga", "vga", results)
        if a.leg in ("all", "cga"):
            ui_leg(tmp, os88marty.machine("os8088_5150_cga"), "cga", results)
        if a.leg in ("all", "boot"):
            boot_leg(tmp, results)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(json.dumps(results, indent=1, sort_keys=True))
    if a.json:
        json.dump(results, open(a.json, "w"), indent=1, sort_keys=True)
    print("deskcalperf: %s" % ("PASS" if not fails else
                               "FAIL (%d): %s" % (len(fails),
                                                  "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
