#!/usr/bin/env python3
"""A worker's entry into an image is COUNTED, not stacked (NO-PINNING K9).

    make && python3 tests/tnest.py

WHAT IT IS ABOUT. `wm_nest_pushd`/`wm_nest_pop` record which images the
machine is executing in, so `mem_can_move` does not move one out from under
a return address (SPEC.md 66.6.1, 66.6.3). `wm_pkgs` is ONE stack, and it was
pushed from EVERY task: a package worker's `OSAPI_DRV_CALL` (ETHER.DRV's verbs
run on FTPD's, Telnet's and the Browser's workers) interleaved with the UI
task's callbacks, so a pop could leave it naming the wrong image. And a
driver's DATA claim was never protected while a worker stood in one of its
verbs: ETHER.DRV's socket pool is movable, and a worker pre-empted
mid-`rep movsb` with ES = the pool came back writing into somebody else's
block. K9 keeps the stack for the UI task alone and COUNTS every other task's
entries in `wm_tcnt`; while it is non-zero, `mem_in_nest` pins every image and
`inst_seg_parked`'s driver arm pins every driver claim. It is coarse on
purpose: it binds only at the instant a compaction runs while a worker is in
an image, and it cost 42 resident bytes where the exact per-task form cost 148.

HOW: A UNIT TEST INSIDE THE LIVE MACHINE. Nothing on MartyPC can stand a
worker inside a driver verb on demand (no NIC), so the row CALLS the routines
itself: it stops the guest on the UI task at a clean point, banks every
register and the tables, and enters each routine with chosen registers and a
return address it has a breakpoint on - the real kernel code at its real
addresses, in the shipped build - then puts everything back and checks the
desktop still answers.

SEVEN ASSERTIONS:
  1. a push with [sch_cur] = a worker counts, and leaves the UI task's stack
     (`wm_pkgd`, `wm_pkgs`) untouched;
  2. while it stands, `mem_in_nest` pins an image nothing else names...
  3. ...and `inst_seg_parked` pins a driver's claims (a segment no instance
     owns), with no service task running;
  4. a nested push counts twice, and two pops return the count to zero;
  5. with the count back at zero both answer "movable" again;
  6. the UI task ([sch_cur] = 0) still uses the stack;
  7. a UI push made WHILE a worker's count stands is still recorded on the
     stack. `wm_tcnt` is the high byte of the word `mem_in_nest` reads
     (kernel size pass 14), so a push that bounded the whole word against
     WM_PKGD_MAX would skip it and leave a stale level behind once the count
     fell back to zero.

VERIFIED TO FAIL: with the `jne .count` in wm_nest_pushd taken out (every push
goes to the stack, as before K9), all six go red; with the push's
`mov bl`/`xor bh, bh` made a word load again, 7 goes red.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty                                        # noqa: E402
import os88sym                                          # noqa: E402
import os88ui                                           # noqa: E402

FAILS = []


def check(ok, what, got=None):
    print("  %s  %s%s" % ("ok  " if ok else "FAIL", what,
                          "" if ok else "  got %r" % (got,)))
    if not ok:
        FAILS.append(what)


def main():
    S = os88sym.syms()
    KSEG = os88sym.KERNEL_SEG
    cold = os88sym.segment_of("wm_nest_pushd")
    lin = os88sym.linear

    with os88ui.boot("build/os8088-360.img",
                     apps="build/apps360.img") as ui:
        m = ui.m
        # (where `.cold` RUNS - [api_coldseg], not COLD_SEG: on a machine with no VGA and no EGA it slid down at boot, docs/plans/MOVABLE-COLD-PLAN.md 2)
        cold = int.from_bytes(m.read(lin("api_coldseg"), 2), "little")

        def w16(name, i=0):
            b = m.read(lin(name) + i, 2)
            return b[0] | b[1] << 8

        def b8(name, i=0):
            return m.read(lin(name) + i, 1)[0]

        # --- stop on the UI task, at a clean point ---------------------------
        m.bp_exec("task_sleep")
        for _ in range(50):
            m.go()
            if not m.wait_stop(20):
                break
            if b8("sch_cur") == 0:
                break
        m.bp_exec()
        if b8("sch_cur") != 0:
            print("tnest: could not stop on the UI task")
            return 2
        saved = m.regs()
        tabs = [(n, m.read(lin(n), sz)) for n, sz in
                (("sch_cur", 1), ("wm_pkgd", 1), ("wm_pkgs", 16),
                 ("wm_tcnt", 1))]
        ss, sp = saved["ss"], saved["sp"]
        ret = S["cold_kretf"]           # any .cold offset: never executed
        scratch_sp = (sp - 0x80) & 0xFFFF

        def call(routine, ds, bx=0):
            """Enter a .cold near routine and stop at its return."""
            m.write((ss << 4) + scratch_sp, bytes([ret & 0xFF, ret >> 8]))
            m.cmd(cmd="park", cs=cold, ip=S[routine])   # (os88marty's own
            for r, v in (("ss", ss),                    # stub idiom: setreg
                                                        # cannot move IP)
                         ("sp", scratch_sp), ("ds", ds), ("es", KSEG),
                         ("bx", bx), ("flags", saved["flags"] & ~1)):
                m.setreg(r, v)
            m.bp_exec(cold * 16 + ret)
            m.go()
            st = m.wait_stop(20)
            m.bp_exec()
            rg = m.regs()
            if rg["ip"] != ret or rg["cs"] != cold:
                raise RuntimeError("%s did not return where expected: %r %r"
                                   % (routine, st, rg))
            return rg["flags"] & 1

        T, X = 3, 0x5A00                # a worker slot, a fake image
        kseg = KSEG

        def far_call(routine, bx):
            """inst_seg_parked is a FAR routine in KERNEL_SEG."""
            m.write((ss << 4) + scratch_sp,
                    bytes([ret & 0xFF, ret >> 8, cold & 0xFF, cold >> 8]))
            m.cmd(cmd="park", cs=kseg, ip=S[routine])
            for r, v in (("ss", ss), ("sp", scratch_sp), ("ds", kseg),
                         ("es", kseg), ("bx", bx),
                         ("flags", saved["flags"] & ~1)):
                m.setreg(r, v)
            m.bp_exec(cold * 16 + ret)
            m.go()
            m.wait_stop(20)
            m.bp_exec()
            rg = m.regs()
            if rg["ip"] != ret or rg["cs"] != cold:
                raise RuntimeError("%s did not return: %r" % (routine, rg))
            return rg["flags"] & 1

        try:
            m.write(lin("sch_cur"), bytes([T]))
            pkgd0 = b8("wm_pkgd")
            stack0 = m.read(lin("wm_pkgs"), 16)

            call("wm_nest_pushd", X)
            check(b8("wm_tcnt") == 1 and b8("wm_pkgd") == pkgd0
                  and m.read(lin("wm_pkgs"), 16) == stack0,
                  "1. a worker's push is counted, and the stack is untouched",
                  (b8("wm_tcnt"), b8("wm_pkgd"), pkgd0))
            check(call("mem_in_nest", KSEG, 0x6B00) == 1,
                  "2. while it stands, mem_in_nest pins any image")
            check(far_call("inst_seg_parked", 0x6B00) == 1,
                  "3. ...and a driver's claims")
            call("wm_nest_pushd", X)
            two = b8("wm_tcnt")
            call("wm_nest_pop", KSEG)
            call("wm_nest_pop", KSEG)
            check(two == 2 and b8("wm_tcnt") == 0,
                  "4. a nested push counts twice, and both pops undo it",
                  (two, b8("wm_tcnt")))
            m.write(lin("sch_cur"), bytes([0]))
            check(call("mem_in_nest", KSEG, 0x6B00) == 0
                  and far_call("inst_seg_parked", 0x6B00) == 0,
                  "5. at zero both answer movable again")
            call("wm_nest_pushd", X)
            ui_ok = (b8("wm_pkgd") == pkgd0 + 1 and b8("wm_tcnt") == 0)
            call("wm_nest_pop", KSEG)
            check(ui_ok and b8("wm_pkgd") == pkgd0,
                  "6. the UI task still uses the stack")
            m.write(lin("sch_cur"), bytes([T]))
            call("wm_nest_pushd", X)            # a worker inside an image...
            m.write(lin("sch_cur"), bytes([0]))
            Y = 0x5B00
            call("wm_nest_pushd", Y)            # ...while the UI task enters
            at = w16("wm_pkgs", 2 * pkgd0)      # another: still RECORDED
            call("wm_nest_pop", KSEG)
            m.write(lin("sch_cur"), bytes([T]))
            call("wm_nest_pop", KSEG)
            m.write(lin("sch_cur"), bytes([0]))
            check(pkgd0 >= 8 or (at == Y and b8("wm_pkgd") == pkgd0
                                 and b8("wm_tcnt") == 0),
                  "7. a UI push while a worker's count stands is still "
                  "recorded (the count is wm_pkgd's HIGH byte, and the push "
                  "indexes by the low one)", (hex(at), b8("wm_pkgd"),
                                               b8("wm_tcnt")))
        finally:
            for n, data in tabs:
                m.write(lin(n), data)
            m.cmd(cmd="park", cs=saved["cs"], ip=saved["ip"])
            for r in ("ax", "bx", "cx", "dx", "si", "di", "bp", "sp", "ss",
                      "ds", "es", "flags"):
                m.setreg(r, saved[r])
            m.go()
        os88marty.settle(m)
        ui.open_drive("A")              # the machine still answers
        check(True, "the desktop answers after the registers are put back")
    print("tnest: %s" % ("FAIL (%d)" % len(FAILS) if FAILS else
                         "7 assertions passed"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
