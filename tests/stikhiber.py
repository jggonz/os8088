#!/usr/bin/env python3
"""SPEC.md 110.4: a pending RESUME stands the Sticky App relaunch down.

    make && make stiktest && python3 tests/stikhiber.py

A hibernation is the whole session, widgets and all, so a boot that is about
to ask "resume?" must not start any of them first: a Resume would bring back
a machine with the widget TWICE in it, and a Discard is the user's to make.
hb_probe posts UI_RBQ_ASK into [ui_rebootq] from the boot overlay when the
hard disk's root holds HIBERNAT.PTR, and stik_boot reads that byte first.

`make stiktest`'s stikhiber360.img carries the restore row and a SYSTEM.CFG
that wants HDD.DRV; this script builds a hard disk of its OWN for this
process (tests/hibernate.py's lesson: a shared --mount is three emulators
writing one file) holding nothing but a HIBERNAT.PTR. One boot on
os8088_xt_hdd:

  A  stik_boot is entered with [ui_rebootq] = UI_RBQ_ASK - the question was
     posted before it ran, which is the ordering the stand-down rests on;
  B  ...and no StikProbe is ever launched: twenty guest seconds after the
     desktop there is no such window, and its row still carries the mark -
     NOT downgraded, so the next ordinary boot brings it back.

**BREAK IT ON PURPOSE** (docs/WRITING-TESTS.md 1): take stik_boot's
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
import os88stik    # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, ".."))
BDIR = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
TEMPLATE = os.path.join(ROOT, "build/martypc/run/media/hdds/default_xtide.vhd")
UI_RBQ_ASK = 5
DESK_SC0 = os88stik.DESK_SC0

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def tick(m):
    return int.from_bytes(m.read(0x46C, 2), "little")


def main():
    tmp = tempfile.mkdtemp(prefix="stikhiber-", dir=os.path.join(ROOT, "build"))
    ptr = os.path.join(tmp, "ptr.bin")
    open(ptr, "wb").write(b"\0" * 64)
    vhd = os.path.join(tmp, "stik.vhd")
    subprocess.check_call(
        ["python3", os.path.join(ROOT, "tools/os88hdd.py"), "--template",
         TEMPLATE, "--out", vhd, "--noboot", "--file", "HIBERNAT.PTR=" + ptr])
    floppy = os.path.join(tmp, "stikhiber360.img")
    shutil.copy(os.path.join(BDIR, "stikhiber360.img"), floppy)

    with os88marty.launch(floppy, machine="os8088_xt_hdd", boot=False,
                          extra=["--mount", "hd:0:" + vhd]) as m:
        print("A: stik_boot meets the posted question")
        m.bp_exec("stik_boot")
        m.run()
        hit = m.wait_stop(120)
        check(hit == "breakpoint", "stik_boot was entered (%r)" % hit)
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
        check("StikProbe" not in ui.titles(),
              "no StikProbe window (%r)" % ui.titles())
        seg = ui._word("sc_seg")
        r = os88stik.parse_row(m.read(seg << 4, os88stik.SC_REC)) if seg else {}
        check(r.get("stik") and m.read(ui._S("desk_zslot") + DESK_SC0, 1)[0]
              == 0xFF, "its row still carries the mark, with no cell: kept "
              "for the next boot (%r)" % (r,))

    shutil.rmtree(tmp, ignore_errors=True)
    print("stikhiber: %s" % ("PASS" if not fails else
                            "FAIL (%d): %s" % (len(fails), "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
