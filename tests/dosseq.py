"""A DOS program's sequential file I/O, timed BY POSITION
(docs/plans/completed/DOS-STREAM-PLAN.md W0 and W6, SPEC.md 96.53).

tests/dostrap/seqcost.asm writes BIGSEQ.DAT in 8KB chunks, reads it back and
seeks between its ends, printing the BIOS ticks each block of chunks took.
The OS's cost of a chunk used to grow with its OFFSET - the box's window was
refilled with READ_AT, which re-walks the cluster chain from the front, and
drained with APPEND, which does the same - so the blocks' times rose along
the file. On the stream slots (SPEC.md 18.4.8, 18.4.9) they are flat.

THE ASSERTION IS THE SHAPE, not a number: the LAST block may not cost more
than FLAT x the first. A walk from the front cannot pass that on the fixed
disk and a stream cannot fail it, whatever the machine's absolute speed.

    python3 tests/dosseq.py                  the 360KB floppy, 256KB, the box
    python3 tests/dosseq.py --hdd            an XT-IDE fixed disk, 1MB, the box
    python3 tests/dosseq.py --hdd --kd       ...and then again under kern_dos
    --measure                                print the table, assert only bytes

The floppy form asserts only the bytes: a 360KB drive hides the walk behind
the rotation (the before-numbers read 1.11 last/first), so it is the fixed
disk that carries the shape. MartyPC throughout: the cost is CPU, and a
4.77MHz 8088 is the machine it is paid on.
"""
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88build                                               # noqa: E402
import os88marty                                               # noqa: E402
import os88mouse                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = os88build.at("build/os8088-360.img")
GATE = os88build.at("build/seqcost360.img")
SCRATCH = os88build.at("build/seqcost-run.img")

FLAT = 1.5      # the last block against the first, at most

# --hdd: the same probe at 1MB, off C: on an XT-IDE fixed disk, where the
# drive stops hiding the CPU. tests/kdhdd.py's fixture, less its markers.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
HMACHINE = "os8088_xt_hdd"
VHD = os.path.abspath("build/dosseq-%d.vhd" % os.getpid())
HPROG = os.path.abspath("build/dosseq-%d.com" % os.getpid())
HFLOP = os.path.abspath("build/dosseq-%d.img" % os.getpid())


def fail(msg):
    print("dosseq: FAIL: %s" % msg)
    sys.exit(1)


def hdd_fixture():
    for p in (TEMPLATE, "build/kernel.sys", "build/kdos/DOS.O88"):
        if not os.path.exists(p):
            fail("%s is missing - `make kdostest` and `make marty`" % p)
    subprocess.check_call(["nasm", "-f", "bin", "-w+error", "-DNCH=128",
                           "-DBLK=16", "-o", HPROG,
                           os.path.join(ROOT, "tests/dostrap/seqcost.asm")])
    subprocess.check_call(
        ["python3", "tools/os88hdd.py", "--template", TEMPLATE, "--out", VHD,
         "--kernel", "build/kernel.sys", "--vbr", "build/boothd.bin",
         "--mbr", "build/mbr.bin",
         "--file", "HIBER.DRV=build/hiber.drv",
         "--file", "CTRL.DRV=build/ctrl.drv",
         "--file", "HDD.DRV=build/hdd.drv",
         "--file", "DOS.O88=build/kdos/DOS.O88",
         "--file", "SEQCOST.COM=" + HPROG], cwd=ROOT)
    subprocess.check_call(["python3", "tools/os88disk.py", "-o", HFLOP,
                           "--size", "360", "build/SEQCOST.COM"], cwd=ROOT)


def run(m, what):
    """Wait out one run of the probe and return its text screen."""
    rows = []

    def done(mm):
        rows[:] = mm.screen() or []
        return any(r.startswith(("READY", "FAILED")) for r in rows)
    try:
        os88marty.until(m, done, "the probe's READY line (%s)" % what,
                        poll=2.0, limit=3000.0)
    except os88marty.MartyError:
        fail("%s: the probe never finished; the last text screen was %r"
             % (what, [r.rstrip() for r in rows if r.strip()]))
    return [r.rstrip() for r in rows if r.strip()]


def judge(text, what, shape):
    print("dosseq: --- %s ---" % what)
    for r in text:
        print("   | %s" % r)
    for r in text:
        if r.startswith("FAILED"):
            fail("%s: the probe reported %r - the BYTES were wrong, which is "
                 "not a speed question" % (what, r))
    blocks, totals = {}, {}
    for r in text:
        mb = re.match(r"^([WRS]) (\d+) (\d+)$", r)
        mt = re.match(r"^([WRS]) total (\d+)$", r)
        if mb:
            blocks.setdefault(mb.group(1), []).append(int(mb.group(3)))
        elif mt:
            totals[mt.group(1)] = int(mt.group(2))
    for ph, name in (("W", "write"), ("R", "read")):
        b = blocks.get(ph)
        if not b or len(b) < 2:
            fail("%s: no %s blocks on the screen" % (what, name))
        print("dosseq: %s %-5s blocks %s ticks, total %d (%.1f s), "
              "last/first %.2f" % (what, name, b, totals.get(ph, 0),
                                   totals.get(ph, 0) / 18.2065,
                                   b[-1] / max(b[0], 1)))
    print("dosseq: %s seek   total %d ticks (%.1f s)"
          % (what, totals.get("S", 0), totals.get("S", 0) / 18.2065))
    if not shape:
        return
    for ph, name in (("W", "write"), ("R", "read")):
        b = blocks[ph]
        if b[-1] > FLAT * max(b[0], 1):
            fail("%s: the last block of the %s cost %d ticks against the "
                 "first's %d - the cost still grows with the offset, so "
                 "something walks the chain from the front (SPEC.md "
                 "18.4.8.1)" % (what, name, b[-1], b[0]))


def main():
    measure = "--measure" in sys.argv
    hdd = "--hdd" in sys.argv
    kd = "--kd" in sys.argv
    if kd and not hdd:
        fail("--kd wants --hdd: kern_dos's run is measured off the fixed disk")
    if hdd:
        hdd_fixture()
        ctx = os88marty.launch(None, apps=HFLOP, machine=HMACHINE,
                               extra=["--mount", "hd:0:" + VHD])
        where = "C:/SEQCOST.COM"
    else:
        for p in (SYS, GATE):
            if not os.path.exists(p):
                fail("%s is missing - `make build/seqcost360.img`" % p)
        shutil.copyfile(GATE, SCRATCH)      # the probe writes 256KB
        ctx = os88ui.boot(SYS, apps=SCRATCH)
        where = "B:/SEQCOST.COM"
    try:
        with ctx as c:
            if hdd:
                m = c
                ui = os88ui.UI(m)
                ui.ready(limit=240)
            else:
                ui = c
                m = ui.m
            if not ui.path(where):
                fail("double-clicking %s opened no window" % where)
            box = run(m, "box")
            m.type_text("x")
            kdt = None
            if kd:
                import kdhdd                    # its arm-3 driver, as is
                os88marty.until(m, lambda mm: kdhdd.box_state(mm) ==
                                kdhdd.DST_RAN, "the windowed run to end",
                                guest=60.0, poll=0.25)
                kdhdd.arm3_run(m, os88mouse.Mouse(marty=m))
                kdt = run(m, "kern_dos")
    finally:
        if hdd:
            for p in (VHD, HPROG, HFLOP):
                try:
                    os.remove(p)
                except OSError:
                    pass
    shape = hdd and not measure
    judge(box, "box", shape)
    if kdt is not None:
        judge(kdt, "kern_dos", shape)
    print("dosseq: ok" + (" (measured, shape not asserted)" if not shape
                          else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
