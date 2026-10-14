#!/usr/bin/env python3
"""SPEC.md 107.6 / PERFORMANCE.md Part 5: what the Desk Calendar's gestures
COST, in primitive calls and in 4.77 MHz time, and what an autostart costs a
boot.

    make && python3 tests/deskcalperf.py [--json out.json]

A redraw is priced by how many primitive calls it makes (PERFORMANCE.md), so
that is what this counts: a breakpoint on every DRAWING cell of the API table
(OSAPI_FONT_RUN, OSAPI_GFX_FILL, OSAPI_GFX_FRAME, ... - the set is read out
of apps/os88api.inc by name), and at each stop the FAR RETURN ADDRESS read
off the guest's stack, so a call is counted only when it came from the
calendar's own segment - the kernel's own drawing never goes through the
table, and nothing else is running. MartyPC is cycle-accurate on a 4.77 MHz
8088, so the cycles between the first and the last call of a gesture are
5150 time; a stopped guest does not count cycles.

On the VGA XT (TALL) and on the 5150's CGA (COMPACT), each off the SHIPPED
360KB disk with a state file holding two appointments today (09:00, 10:00)
and three tasks, the clock at Wed 14 Oct 2026 09:59:

  first paint   the double-click's W_PAINT, whole
  full paint    the About card taken down by a key: dc_all, the same picture
  day select    a click on the 15th, a day with nothing on it
  idle          three DIA beats AND a minute boundary with nothing due:
                MUST BE ZERO, and is asserted - the beats are counted at the
                package's own dc_tick so the zero is not a machine that slept
  reminder      10:00, `Review` goes off: the calendar's own share (its agenda
                line, counted at dc_run) and the alert's paint

and on the VGA XT, two cold boots of the shipped 360KB disk - with no
SYSTEM.CFG, and with one whose trailer holds the calendar's DIA row - timed
from reset to `dia_boot` (the desktop painted) and to `ui_task` (the
autostart done), with the drive's own counters.

Every number is PRINTED as the table PERFORMANCE.md Part 5 and SPEC.md 107.6
quote; the assertions are the ones the design binds: an idle beat and a
minute with nothing due draw NOTHING, a boot with no DIA row does no disk
call after its paint, and the selection of an empty day is the frame pair,
today's field re-run, the heading and the agenda lines that were occupied.
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
import os88dia     # noqa: E402
import os88sym     # noqa: E402
from deskcaledit import Cal, TITLE, disk_with   # noqa: E402
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
           "GFX_BLIT1", "GFX_FILL_PAT", "FONT_RUN", "GFX_POINTS")

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
    """The calendar's calls during one gesture: n, by name, and the span."""

    def __init__(self, hits, seg, extra=()):
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
            self.ms = (self.mine[-1]["cycles"] - self.mine[0]["cycles"]) \
                * 1000.0 / HZ
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
    return os88dia.dat(os88dia.deskcal(appts, tasks, lead=0), x=359, y=40,
                       screen=(640, 480))


def ui_leg(tmp, machine, label, results):
    img = disk_with(tmp, "perf-%s.img" % label, fixture())
    with os88ui.boot(img, machine=machine) as ui:
        m = ui.m
        m.write(ui._S("clk_sec"), bytes([0, 58, 9]))
        box = {}

        def launch():
            box["c"] = Cal(ui, tmp, check=check)
            c = box["c"]
            st_off = os88dia.dia_st_off(os88dia.DESKCAL_DIA_BSS,
                                        os88dia.DESKCAL_PAYLEN)
            ui._wait(lambda: c.bss(st_off)[0] & 3 == 3,
                     "the first tick's registration", 120)
            os88marty.guest_sleep(m, 1.0)
        hits, _ = measure(m, launch)
        c = box["c"]
        seg = c.wseg
        r = {}
        first = Gesture(hits, seg)
        print("%s: first paint      %s" % (label, first))
        r["first_paint"] = first.row()

        tick_at = seg * 16 + pkg_sym(tmp, "dc_tick")
        run_at = seg * 16 + pkg_sym(tmp, "dc_run")

        print("%s: the About card, then a key takes it down" % label)
        c.front()
        about = [(mm[0], it) for mm in ui.menus() for it, _ in mm[3]
                 if it.startswith("About Desk")]
        check(bool(about), "%s: an About item on the bar (%r)"
              % (label, [mm[0] for mm in ui.menus()]))
        ui.menu_pick(*about[0])
        os88marty.pace(m, 1.0)
        hits, _ = measure(m, lambda: (m.key("Escape"),
                                      os88marty.guest_sleep(m, 3.0)))
        full = Gesture(hits, seg)
        print("%s: full paint       %s" % (label, full))
        r["full_paint"] = full.row()
        check(abs(full.n - first.n) <= 2, "%s: the About card's take-down is "
              "the first paint's picture (%d against %d calls)"
              % (label, full.n, first.n))

        print("%s: a click on the 15th" % label)
        ox, oy = c.origin()
        # the 14th is Wednesday's field in week row 2 (Oct 2026 starts on a
        # Thursday): the 15th is the next column. Field k of row w is at
        # content x 16 + 24k, y w0 + pitch * w
        tall = ui.window(TITLE).h > 200     # 255 against COMPACT's 139
        w0, pitch = (24, 10) if tall else (18, 9)
        col, row = 4, 2
        x, y = ox + 16 + 24 * col + 12, oy + w0 + pitch * row + 4
        c.front()
        hits, _ = measure(m, lambda: (ui.mo.click(x, y),
                                      os88marty.guest_sleep(m, 2.0)))
        sel = Gesture(hits, seg)
        check(c.bss(os88dia.DESKCAL_BSS_SEL)[0] == 15,
              "%s: the 15th is selected" % label)
        print("%s: day select       %s" % (label, sel))
        r["day_select"] = sel.row()
        check(sel.n == 6 and sel.by.get("GFX_FRAME") == 2,
              "%s: an empty day after TODAY with two appointments is 6 "
              "calls - the old frame and the new, today's field re-run (the "
              "white frame cut its inverse), the heading, and the two agenda "
              "lines that were occupied (%d: %r)" % (label, sel.n, sel.by))

        print("%s: idle - three beats and a minute boundary" % label)
        m.write(ui._S("clk_sec"), bytes([54, 58, 9]))
        lnow0 = struct.unpack("<H", bytes(c.bss(os88dia.DESKCAL_BSS_LNOW,
                                                2)))[0]
        hits, _ = measure(m, lambda: os88marty.guest_sleep(m, 8.0),
                          extra=(tick_at,))
        idle = Gesture(hits, seg, extra=(tick_at,))
        lnow1 = struct.unpack("<H", bytes(c.bss(os88dia.DESKCAL_BSS_LNOW,
                                                2)))[0]
        print("%s: idle             %s, %d beats, minute %d -> %d"
              % (label, idle, idle.extra[tick_at], lnow0, lnow1))
        check(idle.extra[tick_at] >= 3, "%s: three beats or more ran (%d)"
              % (label, idle.extra[tick_at]))
        check(lnow1 == lnow0 + 1, "%s: ...and one minute passed (%d -> %d)"
              % (label, lnow0, lnow1))
        check(idle.n == 0, "%s: an idle beat and a minute with nothing due "
              "draw NOTHING (%d calls)" % (label, idle.n))
        r["idle"] = dict(idle.row(), beats=idle.extra[tick_at])

        print("%s: 10:00, Review goes off" % label)
        c.key("KeyT")               # today again, so its line is on show
        os88marty.pace(m, 1.5)
        m.write(ui._S("clk_sec"), bytes([54, 59, 9]))

        def fire():
            ui._wait(lambda: c.bss(os88dia.DESKCAL_BSS_ALERT)[0] == 1,
                     "Review's alert", 60)
            os88marty.guest_sleep(m, 3.0)
        hits, _ = measure(m, fire, extra=(run_at,))
        rem = Gesture(hits, seg, extra=(run_at,))
        print("%s: reminder         %s, %d of them the calendar's own runs"
              % (label, rem, rem.extra[run_at]))
        check(rem.extra[run_at] == 1, "%s: the calendar drew ONE run, the "
              "line (%d)" % (label, rem.extra[run_at]))
        r["reminder"] = dict(rem.row(), calendar_runs=rem.extra[run_at],
                             alert_calls=rem.n - rem.extra[run_at])
        m.key("Enter")
        os88marty.pace(m, 1.0)
    results[label] = r


def boot_once(img, machine):
    with os88marty.launch(img, machine=os88marty.machine(machine),
                          boot=False) as m:
        out = {}
        for name in ("dia_boot", "ui_task"):
            m.bp_exec(name)
            m.run()
            if m.wait_stop(240.0) is None:
                raise SystemExit("deskcalperf: never reached %s" % name)
            st = m.status()
            out[name] = (int(st["cycles"]), m.disk())
        m.bp_exec()
        m.run()
    a, b = out["dia_boot"], out["ui_task"]
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
                                                 "os88dia.py"),
                    "cfg", "-o", cfg, "--dia",
                    "A:SYSTEM/DIA/DESKCAL.O88=Calendar"], check=True,
                   stdout=subprocess.DEVNULL)
    subprocess.run(["mcopy", "-o", "-i", row, cfg, "::/SYSTEM.CFG"],
                   check=True, stdout=subprocess.DEVNULL,
                   env=dict(os.environ, MTOOLS_SKIP_CHECK="1"))
    b0 = boot_once(plain, "os8088_xt_vga")
    print("boot, no DIA row:   %r" % b0)
    b1 = boot_once(row, "os8088_xt_vga")
    print("boot, the row:      %r" % b1)
    check(b0["reads_after_paint"] == 0,
          "a boot with no DIA row makes NO disk call after its paint (%d)"
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
