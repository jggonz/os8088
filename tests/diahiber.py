#!/usr/bin/env python3
"""SPEC.md 110.4: a pending RESUME stands the DIA relaunch down.

    make && make diatest && python3 tests/diahiber.py

A hibernation is the whole session, widgets and all, so a boot that is about
to ask "resume?" must not start any of them first: a Resume would bring back
a machine with the widget TWICE in it, and a Discard is the user's to make.
hb_probe posts UI_RBQ_ASK into [ui_rebootq] from the boot overlay when the
hard disk's root holds HIBERNAT.PTR, and dia_boot reads that byte first.

`make diatest`'s diahiber360.img carries the restore row and a SYSTEM.CFG
that wants HDD.DRV; this script builds a hard disk of its OWN for this
process (tests/hibernate.py's lesson: a shared --mount is three emulators
writing one file) holding nothing but a HIBERNAT.PTR. One boot on
os8088_xt_hdd:

  A  dia_boot is entered with [ui_rebootq] = UI_RBQ_ASK - the question was
     posted before it ran, which is the ordering the stand-down rests on;
  B  ...and no DiaProbe is ever launched: twenty guest seconds after the
     desktop there is no such window, and its row still carries the mark -
     NOT downgraded, so the next ordinary boot brings it back.

**BREAK IT ON PURPOSE** (docs/WRITING-TESTS.md 1): take dia_boot's
`cmp byte [ui_rebootq], 0 / jne .r0` out and B fails (the probe opens).
"""
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88marty   # noqa: E402
import os88ui      # noqa: E402
import os88dia     # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, ".."))
BDIR = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
TEMPLATE = os.path.join(ROOT, "build/martypc/run/media/hdds/default_xtide.vhd")
UI_RBQ_ASK = 5
DESK_SC0 = os88dia.DESK_SC0

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def tick(m):
    return int.from_bytes(m.read(0x46C, 2), "little")


def main():
    tmp = tempfile.mkdtemp(prefix="diahiber-", dir=os.path.join(ROOT, "build"))
    ptr = os.path.join(tmp, "ptr.bin")
    open(ptr, "wb").write(b"\0" * 64)
    vhd = os.path.join(tmp, "dia.vhd")
    subprocess.check_call(
        ["python3", os.path.join(ROOT, "tools/os88hdd.py"), "--template",
         TEMPLATE, "--out", vhd, "--noboot", "--file", "HIBERNAT.PTR=" + ptr])
    floppy = os.path.join(tmp, "diahiber360.img")
    shutil.copy(os.path.join(BDIR, "diahiber360.img"), floppy)

    with os88marty.launch(floppy, machine="os8088_xt_hdd", boot=False,
                          extra=["--mount", "hd:0:" + vhd]) as m:
        print("A: dia_boot meets the posted question")
        m.bp_exec("dia_boot")
        m.run()
        hit = m.wait_stop(120)
        check(hit == "breakpoint", "dia_boot was entered (%r)" % hit)
        q = m.read(m.sym("ui_rebootq"), 1)[0]
        check(q == UI_RBQ_ASK, "[ui_rebootq] = UI_RBQ_ASK when it ran (%d)" % q)
        m.bp_exec()
        m.run()

        print("B: and launched nothing")
        ui = os88ui.UI(m)
        t0 = tick(m)                    # 20 GUEST seconds, off the BIOS tick
        end = time.time() + 600         # (memory: the liveness signal)
        while (tick(m) - t0) & 0xFFFF < 364 and time.time() < end:
            time.sleep(0.5)
        check((tick(m) - t0) & 0xFFFF >= 364, "twenty guest seconds passed")
        check("DiaProbe" not in ui.titles(),
              "no DiaProbe window (%r)" % ui.titles())
        seg = ui._word("sc_seg")
        r = os88dia.parse_row(m.read(seg << 4, os88dia.SC_REC)) if seg else {}
        check(r.get("dia") and m.read(ui._S("desk_zslot") + DESK_SC0, 1)[0]
              == 0xFF, "its row still carries the mark, with no cell: kept "
              "for the next boot (%r)" % (r,))

    shutil.rmtree(tmp, ignore_errors=True)
    print("diahiber: %s" % ("PASS" if not fails else
                            "FAIL (%d): %s" % (len(fails), "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
