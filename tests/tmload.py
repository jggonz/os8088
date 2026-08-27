#!/usr/bin/env python3
"""SPEC.md 28.7: the CPU meter and the process rows read the same numbers.

    python3 tests/tmload.py

The Task Manager used to measure load by SPINNING - `{ count += 1; yield }`
for the interval, scored against a rolling maximum, because a spin count has
no absolute scale.  The page then showed two figures that contradicted each
other in plain sight and were both correct: it charged its own worker 34-38%
of CPU TIME while the graph beside it drew 0-2% of SPIN COUNT.

Since SPEC.md 8.1.2 there is an exact idle bucket, and the meter is

    load% = 100 - 100 * idle_cycles / total_cycles

off the same interval and the same total that the process rows are shares of.
So they cannot disagree any more, and THAT is what this checks - not the
absolute value, which depends on what else is running, but the AGREEMENT
between three things that are computed three different ways:

  * what the KERNEL says, read straight out of sch_cycles either side of a
    window (the authority - it is what the scheduler actually charged);
  * what the PAGE's meter says, read out of tm_load in the package's own bss;
  * what the PAGE's rows say, read out of tm_pct.

A regression in any one of them shows here as the three ceasing to line up,
and nothing else in this tree compares them.

IT ALSO GATES THE SPIN STAYING GONE.  tm_worker sleeping instead of spinning
is worth 34-38% of the machine for as long as this window is open, and if it
came back the page would still look right - the meter would read a plausible
number and the rows would still sum to 100.  The task's own CPU share is what
says otherwise, and it is row 4.

ON A 5150 UNDER MARTYPC, because every number here is a cycle count at
4.77 MHz and QEMU cannot time anything (docs/TESTING.md).
"""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88marty                                             # noqa: E402
import os88mouse                                             # noqa: E402
import os88sym                                               # noqa: E402
import os88geom as geom                                      # noqa: E402

S = os88sym.linear
HZ = 4772727.0
PIT_PER_CPU = 4
NAME = b"TaskMgr"
CHIP, MI_TASKS = (12, 8), (60, 60)

# The three readings are taken over DIFFERENT windows - the kernel's is a
# 3-second sample, the page's is its own 9-tick interval - so they cannot be
# equal to the point. What they must not do is tell different stories.
AGREE = 12                      # percentage points
# tm_worker's own share. It was 34-38% spinning; sampling costs the snapshot,
# the walk and the paint, measured at 7-8%.
WORKER_MAX = 20                 # %

FAILED = []
_MAP = {}


def say(*a):
    print(*a)
    sys.stdout.flush()


def check(cond, what, why="", got=None, want=None):
    if cond:
        say("  ok   %s" % what)
        return True
    m = what
    if got is not None or want is not None:
        m += "\n        want: %s\n        got:  %s" % (want, got)
    if why:
        m += "\n        why:  " + why
    FAILED.append(m)
    say("  FAIL %s" % what)
    return False


def report():
    if FAILED:
        say("tmload: %d FAILED" % len(FAILED))
        for m in FAILED:
            say("  FAIL: %s" % m)
        sys.exit(1)
    say("tmload: pass")
    sys.exit(0)


def sym(name):
    """A taskmgr symbol's link address, out of NASM's own map."""
    if not _MAP:
        src = os.path.join(ROOT, "apps", "taskmgr", "taskmgr.asm")
        tmp = tempfile.mktemp(suffix=".asm")
        mp = tempfile.mktemp(suffix=".map")
        open(tmp, "w").write(open(src).read() + "\n[map all %s]\n" % mp)
        r = subprocess.run(["nasm", "-f", "bin", "-w+error",
                            "-I", os.path.join(ROOT, "apps") + os.sep,
                            "-o", os.devnull, tmp],
                           capture_output=True, text=True)
        if r.returncode:
            sys.exit("tmload: could not map taskmgr:\n%s" % r.stderr[:400])
        for line in open(mp):
            p = line.split()                # "<vaddr> <raddr> <name>", HEX
            if len(p) == 3:
                try:
                    _MAP[p[2]] = int(p[0], 16)
                except ValueError:
                    pass
        for f in (tmp, mp):
            if os.path.exists(f):
                os.remove(f)
    return _MAP[name]


def img_size():
    with open(os.path.join(ROOT, "build", "taskmgr.o88"), "rb") as f:
        return struct.unpack_from("<H", f.read(16), 8)[0]


def pkg_slot(m, name):
    t = m.read(S("wm_wins"), geom.MAX_WIN * geom.WIN_SIZE)
    for i in range(geom.MAX_WIN):
        b = i * geom.WIN_SIZE
        if int.from_bytes(t[b + geom.W_FLAGS:b + geom.W_FLAGS + 2],
                          "little") & 3 != 3:
            continue
        seg = int.from_bytes(t[b + geom.W_SEG:b + geom.W_SEG + 2], "little")
        if not seg:
            continue
        hdr = m.read(seg << 4, 32)
        if hdr[:3] == b"O8\x03" and hdr[16:32].split(b"\0")[0] == name:
            return i, seg
    return None


def kernel_shares(m, secs=3.0):
    """What the SCHEDULER charged, either side of a window: {slot: percent}."""
    def cyc():
        raw = m.readseg(0x60, S("sch_cycles") - 0x600, 32)
        return [struct.unpack_from("<i", raw, i * 4)[0] for i in range(8)]
    m.pause()
    a, c0 = cyc(), m.status()["cycles"]
    m.run()
    m.advance(cycles=int(secs * HZ))
    m.run()
    m.pause()
    b, c1 = cyc(), m.status()["cycles"]
    m.run()
    el = c1 - c0
    return {i: 100.0 * (b[i] - a[i]) * PIT_PER_CPU / el for i in range(8)}


def main(argv):
    os.chdir(ROOT)
    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine="os8088_5150_cga_gla") as m:
        mo = os88mouse.Mouse(marty=m)
        mo.menu(CHIP[0], CHIP[1], MI_TASKS[0], MI_TASKS[1])
        m.advance(frames=300)
        m.run()
        mo.to(320, 150)                 # the pointer OFF the window, so it
        m.advance(frames=120)           # is not sitting on a row
        m.run()

        got = pkg_slot(m, NAME)
        if not check(got is not None, "the Task Manager opened",
                     "everything below reads its bss"):
            report()
        slot, seg = got
        bss = (seg << 4) + img_size()

        def word(name):
            return int.from_bytes(
                m.read(bss + sym(name) - sym("os88_image_end"), 2), "little")

        def byte(name):
            return m.read(bss + sym(name) - sym("os88_image_end"), 1)[0]

        k = kernel_shares(m)
        idle_slot = byte("tm_idle")
        check(idle_slot < 8,
              "the page found the idle task (slot %d)" % idle_slot,
              "the snapshot names it with T_STATE = 3 (SPEC.md 28.7); 0xFF "
              "means no slot answered, so the meter has no numerator and the "
              "System row is missing the idle time",
              got=hex(idle_slot), want="a slot < MAX_TASKS")

        kidle = k.get(idle_slot, 0.0)
        kload = 100.0 - kidle
        load = word("tm_load")
        check(abs(load - kload) <= AGREE,
              "the METER agrees with the kernel (%d%% against %.1f%%)"
              % (load, kload),
              "SPEC.md 28.7's whole point. The meter is 100 - 100*idle/total "
              "off the page's own snapshot; this is the same figure off "
              "sch_cycles directly, which is what the scheduler actually "
              "charged. They diverging means the page's total is not the "
              "kernel's - most likely tm_idlec missing the shift tm_total "
              "takes during normalisation, which is a ratio of two scales",
              got="meter %d%%, kernel %.1f%%" % (load, kload),
              want="within %d points" % AGREE)

        # TM_ROWS bytes and not one more: past the end is tm_state, and a
        # byte of that reads as a share of 130%.
        nrows = 1 + geom.INST_MAX if hasattr(geom, "INST_MAX") else 13
        pct = m.read(bss + sym("tm_pct") - sym("os88_image_end"), nrows)
        rows = [p for p in pct if p]
        total = sum(rows)
        check(90 <= total <= 110,
              "the ROWS still sum to the whole machine (%d%%, %s)"
              % (total, sorted(rows, reverse=True)[:4]),
              "the idle task's cycles are folded into row 0 (System), so a "
              "fully accounted machine adds to 100. Well under means idle is "
              "in no row and the rows are shares of the busy time only, which "
              "is the shape this page had before SPEC.md 28.7 and reads as "
              "every application using far more of the machine than it does",
              got="%d%%" % total, want="90..110%")

        row0 = pct[0]
        check(abs(row0 - (kidle + k.get(0, 0.0))) <= AGREE,
              "row 0 is System = the UI task PLUS idle (%d%% against %.1f%%)"
              % (row0, kidle + k.get(0, 0.0)),
              "SPEC.md 28.7 folds the idle task into row 0 because it owns no "
              "instance. If it is not there the row understates the machine "
              "and every other row overstates it",
              got="row 0 %d%%, kernel %.1f%%" % (row0, kidle + k.get(0, 0.0)),
              want="within %d points" % AGREE)

        # --- and the spin is gone -------------------------------------------
        mine = max((v for i, v in k.items()
                    if i not in (0, idle_slot)), default=0.0)
        check(mine <= WORKER_MAX,
              "tm_worker SLEEPS its interval (its task is %.1f%% of the "
              "machine)" % mine,
              "it used to spin the interval and that WAS the load meter, so "
              "it could not be removed until the idle bucket replaced it "
              "(SPEC.md 28.7). Spinning again would cost 34-38% of the "
              "machine for as long as this window is open - and the page "
              "would still LOOK right, because the meter would read a "
              "plausible number and the rows would still sum to 100",
              got="%.1f%%" % mine, want="<= %d%%" % WORKER_MAX)

    report()


main(sys.argv[1:])
