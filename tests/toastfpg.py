#!/usr/bin/env python3
"""SPEC.md 12.8.7: a toast that is up and within its time outranks the file
widget, and the widget has the bar back the moment the toast's time is spent.

    python3 tests/toastfpg.py [machine]

A package launch off B: is the operation: it reads well past FPG_WARM sectors
in one freeze, so the widget arms. Three legs, each a fresh launch of the
Calculator:

  A. NO TOAST - the baseline: fpg_paint runs. Without it the other two legs
     prove nothing, because a widget that never draws also never draws while
     a toast is up.
  B. A TOAST FOR LONGER THAN THE LOAD - injected at the first fpg_arm, with
     the guest stopped, so it is up before the widget's first pixel exactly
     as a toast_now from the handler would be. fpg_arm must still arm (the
     freeze is still reported, mou_apply still defers) and fpg_paint must
     never run.
  C. A TOAST THAT EXPIRES A FEW TICKS IN - the hand-back. fpg_paint must run,
     and not before the tick the toast dies at: toast_up asks the CLOCK,
     because the idle pass that retires a toast cannot run inside the freeze.

Break it on purpose and it goes red: make fpg_vis ignore the toast and leg B
paints; make toast_up read [toast_on] alone and leg C never paints.

THE TOAST IS POKED, not raised: toast_on, toast_die and toast_buf are what
toast_arm leaves, written at a stop on fpg_arm. Nothing composes it - no
handler ran toast_now - so this asserts the WIDGET's decision and not the
strip's pixels; tests/toastbar.py and tests/mixer.py are the strip's.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88marty as M      # noqa: E402
import os88ui              # noqa: E402

SYS = "build/os8088-360.img"
APPS = "build/apps360.img"
MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
PKG = "B:/APPS/CALC.O88"


def check(cond, what):
    if not cond:
        raise AssertionError(what)
    print("  ok " + what, flush=True)


def leg(ui, ttl):
    """Launch PKG once; ttl None = no toast, else inject one at the arm.
    Answers (arms, paints, die, first paint's tick)."""
    m = ui.m
    state = {"die": None}

    def at_stop(mm, rec):
        if rec["name"] == "fpg_arm" and ttl is not None and state["die"] is None:
            now = struct.unpack("<H", mm.read(ui._S("ticks"), 2))[0]
            die = (now + ttl) & 0xFFFF
            mm.write(ui._S("toast_buf"), b"Toast outranks the bar\0")
            mm.write(ui._S("toast_die"), struct.pack("<H", die))
            mm.write(ui._S("toast_on"), b"\1")
            state["die"] = die
        if rec["name"] == "fpg_paint":
            return (struct.unpack("<H", mm.read(ui._S("ticks"), 2))[0],
                    mm.read(ui._S("toast_on"), 1)[0])
        return None

    with M.bp_trace(m, "fpg_arm", "fpg_paint", on_hit=at_stop) as tr:
        w = ui.path(PKG)
        tr.until(lambda: tr.count("fpg_arm") > 0, "the launch armed the widget", 120)
    paints = [h["hit"] for h in tr.hits if h["name"] == "fpg_paint"]
    ui.menu_pick("Calc", "Close")
    m.write(ui._S("toast_on"), b"\0")       # leave nothing for the next leg
    return tr.count("fpg_arm"), paints, state["die"]


with os88ui.boot(SYS, apps=APPS, machine=MACHINE, verbose=False) as ui:
    arms, paints, _ = leg(ui, None)
    check(arms > 0 and len(paints) > 0,
          "A: no toast - the launch arms the widget and it draws "
          "(%d arm calls, %d paints)" % (arms, len(paints)))

    arms, paints, die = leg(ui, 0x2000)
    check(arms > 0, "B: a toast up - the widget still ARMS (%d)" % arms)
    check(not [t for t, on in paints if on],
          "B: ...and never draws while the toast is within its time "
          "(%d paints with the toast up)" % len([t for t, on in paints if on]))

    arms, paints, die = leg(ui, 3)
    held = [t for t, on in paints if on]
    check(held, "C: a toast whose time runs out mid-load - the widget has the "
                "bar back INSIDE the freeze, the toast not yet retired "
                "(%d of %d paints)" % (len(held), len(paints)))
    late = ((held[0] - die) & 0xFFFF) < 0x8000
    check(late, "C: ...and not before the toast's last tick "
                "(first paint at tick %04x, toast died at %04x)" % (held[0], die))
    print("toastfpg: PASS", flush=True)
