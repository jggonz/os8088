#!/usr/bin/env python3
"""timerrace - SPEC.md 13.9.2: ui_timer_pass asks AGAIN, under the gfx lock,
whether the window it is about to call still exists.

    make && python3 tests/timerrace.py

THE RACE. ui_timer_pass tests a record's W_FLAGS, finds its deadline due,
and calls gfx_lock - which can BLOCK. When the holder is a package's dying
WORKER (inst_task_die), it destroys the window under that lock (W_FLAGS = 0)
and frees the region straight after letting go. The pass, resumed, used to
call the handler anyway: W_SEG still names the freed region, so the call
goes into memory the heap may already have handed to somebody else.

The real interleaving needs a worker to win a lock at one instant, so this
gate MAKES that instant instead of waiting for it, with two breakpoints:
  1. A Disk window's record is given a due timer (W_TIMER behind [ticks], a
     non-zero W_ONTIMER, [wm_tarm] set) and the guest is stopped at
     `ui_timer_pass.locked` - the first instruction after gfx_lock
     returned, the record in SI asserted to be that window's.
  2. W_FLAGS bit 0 is cleared there: the window was destroyed while the pass
     waited, exactly as a worker's teardown leaves it. Then run with
     breakpoints on `ui_bill` (the handler called - the defect) and on
     `ui_timer_pass.gone` (the dispatch skipped - the fix), and see which
     comes first.
The record is put back before the guest goes on. Break on purpose - the
`jz .gone` after the re-test taken out - and step 2 stops at ui_bill.
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
os.chdir(ROOT)
import os88geom as geom                                       # noqa: E402
import os88marty                                              # noqa: E402
import os88ui                                                 # noqa: E402

W_ONTIMER, W_TIMER = 30, 32     # kernel/wm.inc (the record's timer words)


def main():
    bad = []
    with os88ui.boot("build/os8088-360.img", apps="build/apps360.img",
                     machine="os8088_5150_cga_gla", settle=False) as ui:
        m = ui.m
        w = ui.open_drive("B")
        rec = m.sym("wm_wins") + w.i * geom.WIN_SIZE
        m.pause()
        raw = bytearray(m.read(rec, geom.WIN_SIZE))
        ticks = struct.unpack("<H", m.read(m.sym("ticks"), 2))[0]
        struct.pack_into("<HH", raw, W_ONTIMER, 1, (ticks - 4) & 0xFFFF)
        m.write(rec + W_ONTIMER, bytes(raw[W_ONTIMER:W_TIMER + 2]))
        m.write(m.sym("wm_tarm"), b"\x01")
        m.bp_exec("ui_timer_pass.locked")
        m.run()
        st = m.wait_stop(20)
        r = m.regs()
        si_lin = ((r["ds"] << 4) + r["si"]) & 0xFFFFF
        print("   1. stopped at ui_timer_pass.locked: %s, SI the record's %s"
              % (st, si_lin == rec))
        if st is None or si_lin != rec:
            bad.append("the pass never dispatched this window's timer under "
                       "the lock (state %r, record %s) - this tests nothing"
                       % (st, si_lin == rec))
        else:
            fl = m.read(rec, 1)[0]
            m.write(rec, bytes([fl & 0xFE]))         # destroyed meanwhile
            m.bp_exec("ui_bill", "ui_timer_pass.gone")
            m.run()
            st = m.wait_stop(20)
            r = m.regs()
            at = ((r["cs"] << 4) + r["ip"]) & 0xFFFFF
            where = "ui_bill" if at == m.sym("ui_bill") else \
                "ui_timer_pass.gone" if at == m.sym("ui_timer_pass.gone") \
                else "%05X" % at
            print("   2. W_FLAGS cleared; the next stop: %s" % where)
            if where != "ui_timer_pass.gone":
                bad.append("a window destroyed while the pass waited for the "
                           "lock was still dispatched (stopped at %s)" % where)
            m.write(rec, bytes([fl]))                 # ...put back, unarmed
            m.write(rec + W_ONTIMER, b"\0\0\0\0")
        m.bp_exec()
        m.run()
    for b in bad:
        print("   FAIL: %s" % b)
    print("timerrace: %s" % ("FAIL" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
