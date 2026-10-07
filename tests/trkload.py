#!/usr/bin/env python3
"""HOW LONG TRACKER'S SPEAKER LOAD TAKES - SPEC.md 45.25 and 45.25.4.

    make && python3 tests/trkload.py

With no card Tracker runs every sample of a module through the speaker's
load filter at load, in place (tsp_natural), and squares up a bass or a drum
the filter took most of (45.25.4). On an 8088 that is seconds for a big
module, so its cost is a number to hold rather than to notice: this opens
Tracker on the card-less 5150, arms breakpoints at tsp_natural's entry and
its .done in the package as loaded, loads BEVERLY.MOD through Tracker's own
L and the file chooser, and reads the cycles between them off MartyPC's
counter - exact, and the guest charged nothing.

What must hold: under BUDGET cycles. MEASURED at the commit that landed it:
2.86 s without the bass handling (-DTSP_NOBASS), 4.01 s with it. The first
version summed |x| and |y| inside the filter's own loop - ~170 cycles a byte
of instruction fetch on an 8088 - and read 6.16 s; that is the regression
this row exists to catch.

Broken on purpose - the sums folded back into the filter's loop - it reads
6.16 s and FAILS.
"""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88build                  # noqa: E402
import os88geom as geom                              # noqa: E402
import trkspk                                        # noqa: E402

HZ = 4772727.0
BUDGET = int(4.4 * HZ)


def main():
    os.chdir(ROOT)
    syms, _ = trkspk.pkg_syms(*trkspk.SRC)
    tmp = tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build"))
    vhd = trkspk.vhd_for(tmp.name, [
        ("TRACKER.O88", os88build.at("build/tracker.o88")),
        ("BEVERLY.MOD", "apps/tracker/beverly.mod")])
    m = os88marty.launch(None, machine=trkspk.MACHINE,
                         extra=["--mount", "hd:0:" + os.path.abspath(vhd)])
    try:
        ui = os88ui.UI(m)
        ui.ready(limit=240)
        w = ui.path("C:/TRACKER.O88")
        rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE, geom.WIN_SIZE)
        base = (rec[geom.W_SEG] | rec[geom.W_SEG + 1] << 8) << 4
        a0 = base + syms["tsp_natural"]
        a1 = base + syms["tsp_natural.done"]
        cyc = {}

        def hit(mm, r):
            cyc.setdefault(r.get("addr"), r["cycles"])
        with os88marty.bp_trace(m, a0, a1, on_hit=hit) as tr:
            m.key("KeyL")
            # The chooser is a Disk window in a chooser role (SPEC.md 38.1):
            # its rows are its OWN listing, and a click selects (its arrows
            # scroll, 38.4) - FS_SEL is confirmed before Enter answers with it
            ch = ui.chooser(limit=120)
            rows = [r[0] for r in ui.listing(ch)]
            if "BEVERLY.MOD" not in rows:
                print("   FAIL: the chooser lists %r" % rows)
                return 1
            ui.chooser_select("BEVERLY.MOD", ch)
            m.key("Enter")
            tr.until(lambda: a1 in cyc, "tsp_natural to finish", limit=300.0)
    finally:
        m.close()
        tmp.cleanup()
    n = cyc[a1] - cyc[a0]
    ok = n < BUDGET
    print("   tsp_natural on BEVERLY.MOD: %d cycles = %.2f s on a 4.77 MHz "
          "8088 (budget %.2f s)" % (n, n / HZ, BUDGET / HZ))
    if not ok:
        print("   FAIL: the speaker's load is over its budget")
    print("trkload: %s" % ("ok" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
