#!/usr/bin/env python3
"""Nothing calls `.boot3`, or the blob, after kmain gives them back
(BOOT3-PLAN 4.2 and R3, SPEC.md 2.9.15.4) - the RUNTIME half of the fence.

    python3 tests/b3poison.py [--kernel big|small] [--break late]

Registered as b3poison in soak. The build-time half is os88ovlchk's rules
B1, B3 and B9, which read every written reference; what no source scan can
see is a far pointer STORED somewhere, or a vector left aimed at the dead
RAM. Without help such a call runs whatever the bytes still say - usually
the body itself, which then WORKS until the heap reuses the RAM. So the row
boots `B3POISON=1` (memory.inc's b3p_lay): at mem_unblob_x the dead range -
`.boot3`, `.boot3m`, `.boot3b`, the lift and the blob, from `.boot3`'s first
live byte (or the floor, where that is lower) up to the paragraph past the
blob - is filled with CCh and QUARANTINED (kept out of the heap, where the
shipped drop's compaction would slide claims straight over it), and int 3
goes to b3p_trap, which records the CS:IP that ran the poison (b3p_hit) and
its far caller (b3p_from) and stops the machine. A late call therefore traps
at the very byte it lands on, for the whole session.

Per kernel, in a PRIVATE tree (os88build.tree, never build/), on
os8088_5150_both_gla - a CGA and a Hercules, so a mono machine at home: on
kern_big `.cold` slides and the range is its widest (the slid-off `.cold`,
the VGA rung and all of `.boot3`), and two cards give the Display page an
adapter to switch to:

  1. THE POISON IS THERE (the positive control - without it every leg below
     passes on a kernel that laid nothing): [b3p_lo] < [b3p_hi], the range
     covers `.boot3`'s kmain_o and the whole blob, IVT 3 is
     KERNEL_SEG:b3p_trap, no claim reaches into the range, and every byte
     of it reads CCh.
  2. A MENU: the system menu's Control Panel item opens the panel.
  3. A CONTROL PANEL ROUND: its Display page selected, the panel closed.
  4. A FILE OPEN: B:/APPS/CALC.O88 opened from the Disk window, and Calc
     closed from its own menu.
  5. A DISPLAY SWITCH: the panel again, Set Primary on the other adapter,
     [vid_kind] follows, the panel closed.
  After every leg: [b3p_hit] is 0 (nothing ran the poison) and the tick is
  moving (a machine that trapped is halted with interrupts off). At the end
  leg 1's read again: every byte still CCh, so nothing WROTE into the dead
  RAM either (an ISR, a buffer pointer left aimed at `.boot3b`).

The red arm (WRITING-TESTS 1), which must turn the row red:
  --break late  once the desktop is up, a FAR CALL to kmain_o is injected
                from wherever the CPU is (the return frame pushed on SS:SP,
                CS:IP parked on B3_SEG:kmain_o) - BOOT3-PLAN's "call a
                `.boot3` body from kmain after mem_unblob_x", made at the
                desktop so that the shipped kmain is the kmain under test.
                The trap must name B3_SEG:kmain_o and the parked caller.

What it cannot see: a call that only a path this row does not drive would
make, and the shipped kernel's reuse of the range - the knob keeps it out of
the heap, so a late call on a SHIPPED kernel runs whatever claim the
compaction put there, which is a crash somewhere else rather than a trap.
"""
import argparse
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [os.path.join(ROOT, "tools"), HERE]
import os88build                                            # noqa: E402

MACHINE = "os8088_5150_both_gla"    # a CGA and a Hercules, GLaBIOS
MC_SIZE = 10
POISON = 0xCC
fails = []

AP = argparse.ArgumentParser()
AP.add_argument("--kernel", choices=("big", "small"))
AP.add_argument("--break", dest="brk", choices=("late",))
A = AP.parse_args()

# THE KNOB KERNEL'S PRIVATE TREE, and the symbol reader pointed at it BEFORE
# os88ui is imported: os88geom reads $OS88_DEFINES at import time and picks
# its per-arm constants from it (a kern_small window record is 65 bytes, not
# 74), so on kern_small the arm has to be in the environment first - which
# is also why each kernel runs in a process of its own (main below).
# kern_small's map is in the tree's smallk/, its disk at the top.
if A.kernel == "big":
    _T = os88build.tree("B3POISON=1").apply()
    IMAGE, APPS = _T.img("os8088-360.img"), _T.img("apps360.img")
elif A.kernel == "small":
    _T = os88build.tree("B3POISON=1", targets=("small", "apps360.img")).apply()
    os.environ["OS88_BUILD"] = os.path.join(_T.dir, "smallk")
    os.environ["OS88_DEFINES"] = "KERN_SMALL B3_POISON"
    IMAGE, APPS = _T.img("small360.img"), _T.img("apps360.img")

import os88marty as M                                       # noqa: E402
import os88sym                                              # noqa: E402
import os88ui                                               # noqa: E402
import dispcp                                               # noqa: E402

if A.kernel == "small":
    os88sym.default_defines("KERN_SMALL", "B3_POISON")


def check(ok, what):
    print("   %s  %s" % ("ok  " if ok else "FAIL", what), flush=True)
    if not ok:
        fails.append(what)
    return ok


def u16s(b, n):
    return struct.unpack("<%dH" % n, bytes(b))


class Guest(object):
    def __init__(self, ui):
        self.ui, self.m = ui, ui.m
        self.eq = os88sym.equates()

    def w(self, name, n=1):
        v = u16s(self.m.read(os88sym.linear(name), 2 * n), n)
        return v if n > 1 else v[0]

    def claims(self):
        mm = self.eq["MEM_MAX"]
        raw = self.m.read(os88sym.linear("mem_tab"), mm * MC_SIZE)
        out = []
        for k in range(mm):
            seg, para = struct.unpack_from("<HH", bytes(raw), k * MC_SIZE)
            if seg:
                out.append((seg, seg + para))
        return out

    def trap(self):
        hit, frm = self.w("b3p_hit", 2), self.w("b3p_from", 2)
        return None if hit == (0, 0) else (hit, frm)

    def ticking(self):
        t0 = self.w("ticks")
        M.until(self.m, lambda _: self.w("ticks") != t0,
                "the system tick to move", guest=2)


def name_of(cs, ip):
    """The nearest label at or below CS:IP, for the report - a body's own
    name over a section marker sharing its address (kmain_o is `.boot3`'s
    first live byte, so b3_live and cfx_tab_end are there too)."""
    lin, at = cs * 16 + ip, {}
    for n in os88sym.syms():
        try:
            a = os88sym.linear(n)
        except Exception:
            continue
        if a <= lin and lin - a < 0x1000:
            at.setdefault(a, []).append(n)
    if not at:
        return "%04X:%04X" % (cs, ip)
    a = max(at)
    names = sorted(at[a], key=lambda n: (n.endswith(("_end", "_live")),
                                         "." in n, n))
    return "%04X:%04X (%s+%d)" % (cs, ip, names[0], lin - a)


def alive(g, leg):
    """After every leg: nothing ran the poison, and the machine is running."""
    t = g.trap()
    if t:
        check(False, "%s: the poison was RUN at %s, far-called from %s"
              % (leg, name_of(*t[0]), name_of(t[1][1], t[1][0])))
        raise SystemExit(1)
    try:
        g.ticking()
    except M.MartyError as e:
        check(False, "%s: the machine stopped (%s)" % (leg, str(e)[:120]))
        raise SystemExit(1)
    check(True, "%s: no trap, the tick moving" % leg)


def poisoned(g):
    """Leg 1, the positive control."""
    m, eq = g.m, g.eq
    lo, hi = g.w("b3p_lo"), g.w("b3p_hi")
    b3 = eq["B3_SEG"]
    km = os88sym.linear("kmain_o")
    blob = eq["BLOB_SEG_AT"]
    print("   range [%04X, %04X) = %d bytes; B3_SEG %04X, kmain_o %05X, "
          "blob %04X..%04X" % (lo, hi, (hi - lo) * 16, b3, km, blob,
                               blob + eq["BOOT2_PAD"] // 16))
    check(lo and lo < hi, "b3p_lay ran: [b3p_lo] %04X < [b3p_hi] %04X"
          % (lo, hi))
    check(lo * 16 <= km < hi * 16 and lo <= blob
          and blob + eq["BOOT2_PAD"] // 16 <= hi,
          "the range covers `.boot3`'s kmain_o and the whole blob")
    vec = u16s(m.read(3 * 4, 4), 2)
    check(vec == (os88sym.linear("b3p_trap") - os88sym.KERNEL_SEG * 16,
                  os88sym.KERNEL_SEG),
          "IVT 3 is KERNEL_SEG:b3p_trap (%04X:%04X)" % (vec[1], vec[0]))
    intact(g, lo, hi, "")
    return km


def intact(g, lo, hi, when):
    """No claim in [lo, hi), and every byte of it CCh."""
    m = g.m
    held = [(a, b) for a, b in g.claims() if a < hi and b > lo]
    check(not held, "no claim reaches into the quarantined range%s%s"
          % (when, "" if not held else " - %s" % ["%04X..%04X" % c
                                                   for c in held]))
    raw = bytes(m.read(lo * 16, (hi - lo) * 16))
    bad = [i for i, c in enumerate(raw) if c != POISON]
    check(not bad, "all %d bytes of the range read CCh%s%s"
          % (len(raw), when, "" if not bad else " - NOT %d of them, the "
             "first at %05X" % (len(bad), lo * 16 + bad[0])))


def late_call(g, km):
    """--break late: a far call into the dead `.boot3`, from wherever the CPU
    is. The frame is the caller's CS:IP, as `call far` would push it."""
    m, b3 = g.m, g.eq["B3_SEG"]
    m.pause()
    r = m.regs()
    sp = (r["sp"] - 4) & 0xFFFF
    m.write((r["ss"] << 4) + sp, struct.pack("<HH", r["ip"], r["cs"]))
    # PARK FIRST, THEN THE REGISTERS - tests/desklayer.py's order, and not a
    # style: `park` does not keep SS:SP, so registers set before it are lost
    # and the CCh traps on whatever stack it left (SS 0, measured). IF and TF
    # off, so the far call is the next thing the CPU does and no tick lands
    # between the park and the poison
    m.cmd(cmd="park", cs=b3, ip=km - b3 * 16)
    for reg in ("ax", "bx", "cx", "dx", "si", "di", "bp", "ss", "ds", "es"):
        m.setreg(reg, r[reg])
    m.setreg("sp", sp)
    m.setreg("flags", r["flags"] & ~0x0300)
    m.run()
    print("   RED ARM: far call to %04X:%04X (kmain_o) injected from "
          "%04X:%04X" % (b3, km - b3 * 16, r["cs"], r["ip"]), flush=True)
    try:
        M.until(m, lambda _: g.trap() is not None, "the injected call to "
                "trap", guest=2)
    except M.MartyError:
        pass
    t = g.trap()
    if t:
        check(t[0] == (b3, km - b3 * 16) and t[1] == (r["ip"], r["cs"]),
              "the trap names the call: %s from %s"
              % (name_of(*t[0]), name_of(t[1][1], t[1][0])))


def run(kernel, brk):
    print("== kern_%s, B3POISON=1, %s" % (kernel, MACHINE), flush=True)
    with os88ui.boot(IMAGE, apps=APPS, machine=MACHINE, quick=True) as ui:
        g = Guest(ui)
        m = ui.m
        M.ui_passed(m)
        km = poisoned(g)
        alive(g, "the boot")
        if brk:
            late_call(g, km)
            alive(g, "the late call")
        S = os88sym.linear

        def settle(mm, **k):
            return M.ui_done(mm)

        # 2-3: a menu, and the Control Panel on its Display page
        ui.menu_pick(0, "Control Panel")
        ui.wait_window("Control Panel")
        alive(g, "a menu: Control Panel")
        dispcp.open_panel(m, ui.mo, S, settle, page=g.eq["CP_IVID"])
        alive(g, "the Control Panel's Display page")
        dispcp.close_panel(m, ui.mo, S, settle)
        alive(g, "the Control Panel round, closed")
        # 4: a file open, from the Disk window
        ui.path("B:/APPS/CALC.O88")
        ui.raise_window(ui.wait_window("Calculator"))
        ui.menu_pick("Calc", "Close")
        M.ui_passed(m)
        alive(g, "a file open: CALC.O88, and closed")
        ui.menu_pick(0, "Control Panel")
        ui.wait_window("Control Panel")
        dispcp.open_panel(m, ui.mo, S, settle, page=g.eq["CP_IVID"])
        # 5: a display switch, to the adapter that is not the primary
        kind0 = m.read(S("vid_kind"), 1)[0]
        avail = m.read(S("vid_avail"), 1)[0]
        slots = [s for s in range(bin(avail).count("1"))
                 if dispcp.adapter_kind(avail, s) != kind0]
        want = dispcp.adapter_kind(avail, slots[0])
        for _ in range(3):
            dispcp.set_primary(m, ui.mo, S, settle, slots[0])
            if m.read(S("vid_kind"), 1)[0] == want:
                break
        check(m.read(S("vid_kind"), 1)[0] == want,
              "Set Primary: [vid_kind] %d -> %d" % (kind0, want))
        dispcp.close_panel(m, ui.mo, S, settle)
        alive(g, "a display switch, and the panel closed")
        intact(g, g.w("b3p_lo"), g.w("b3p_hi"), ", after the drive")


def main():
    if not A.kernel:
        # ...one process per kernel (the import-time arm above)
        rc = 0
        for k in ("big", "small"):
            cmd = [sys.executable, os.path.abspath(__file__), "--kernel", k]
            rc |= subprocess.call(cmd + (["--break", A.brk] if A.brk else []))
        print("b3poison: %s" % ("FAILED" if rc else "both kernels ok"))
        return 1 if rc else 0
    try:
        run(A.kernel, A.brk)
    except SystemExit:
        pass
    except Exception as e:
        check(False, "kern_%s: %s" % (A.kernel, str(e).splitlines()[0][:200]))
    if fails:
        print("b3poison kern_%s: %d FAILED: %s"
              % (A.kernel, len(fails), "; ".join(fails)))
        return 1
    print("b3poison kern_%s: the dead `.boot3` and blob are CCh, and a menu, "
          "a Control Panel round, a file open and a display switch ran none "
          "of it and wrote none of it%s"
          % (A.kernel, " - WITH THE RED ARM APPLIED, WHICH SHOULD HAVE "
             "FAILED IT" if A.brk else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
