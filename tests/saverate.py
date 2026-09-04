#!/usr/bin/env python3
"""Is a saver mode ASLEEP while it is behind? (SPEC.md 79.5.7, 8.1.2.4)

    make && python3 tests/saverate.py [--machine os8088_5150_cga_gla]

Every mode says `[sv_ival]` = 1, which is one frame a TICK, and three of the
four have always got it. Sea life did not, and the way it lost it is the shape
this file exists to catch: not a routine that got slower, but a **deadline
that got quantised**.

`ui_task` blocks on `task_sleep(1)` (SPEC.md 8.1.2), which parks until
`[ticks]` reaches the value it holds AT THE CALL - so a pass that has already
run into the next tick sleeps through the whole of the one after it. Anything
polled once a pass therefore runs at `18.2 / (floor(work / 54.9ms) + 1)`: the
full rate while its work fits a tick and **half of it the moment it does
not**, with nothing in between. A sea-life pass costs 50-106 ms depending on
how many swimmers were born at scale 2 and how many are at a screen edge
(SPEC.md 79.5.1, 79.5.3), so it lands either side of that step, and the mode
measured **12.0 fps swinging between 9.2 and 17.8** where the spin it was
written against held 16.5.

**THE ASSERTION IS NOT A FRAME RATE, AND THAT IS THE WHOLE DESIGN OF THIS
FILE.** A floor on the rate cannot tell the defect from the weather: a sea of
four big swimmers legitimately costs more than a tick and legitimately draws
at 14 fps, and an early draft of this test failed exactly that - a healthy
kernel, a rate of 14.44, and nothing wrong. What separates them is not how
fast the mode ran but **what the machine was doing while it ran slowly**:

    the mode is not keeping up  AND  the machine is HALTED  ->  it was asleep
                                                                with a frame
                                                                already due

Measured, one 12-second session a row, the same kernel either side of the
fix:

    before   9.12 fps and 37.2% halted     7.95 fps and 15.9% halted
    after   18.27 fps and  2.5% halted    18.30 fps and  1.6% halted

An expensive sea on a healthy kernel comes out slow and BUSY, which passes; a
cheap sea comes out fast and halted, which passes; only slow AND halted is the
step, and that is a thing no content can produce. It is also exactly the
sentence SPEC.md 8.1.2.4 is about, so the test and the section fail together.

**Frames are counted off the swimmers themselves** - `sv_fx += sv_fvx` happens
once a frame in `sv_fish_one` and nowhere else - so a swimmer that neither
respawned nor changed speed inside a window gives an exact count, and windows
where none survived are dropped rather than guessed at.

**A WINDOW IS GUEST TIME AND NOT WALL CLOCK, and that is not tidiness.** It
was `time.sleep(3)`, and MartyPC runs at whatever multiple of real time the
host manages - four times it here - so three seconds of host was twelve of
guest: **218 frames at 18.2 fps, and every swimmer crosses a 640-pixel screen
in 160.** Every counter then reads a swimmer that respawned mid-window, and
what comes back is not noise, it is a plausible number that is too low. The
window survived only while the mode was too slow to cross - at 10.75 fps the
2-pixel swimmers just made it - so SPEC.md 79.5.8, which made the mode FASTER,
is what broke it: 18.18 fps measured over one host second read as 8.48 over
three. `m.advance(cycles=)` is exact in guest time and independent of the
host, which is what a window sized against a crossing needs.

**The other three modes are the CONTROL and are asserted more weakly**, which
is worth being explicit about. They have no swimmer, so they are counted off
`[sv_due]`, and that is a frame counter only while a mode is KEEPING UP: it
advances by `[sv_ival]` a drawn frame, but a mode more than SV_LAG behind
re-anchors it to the clock (SPEC.md 79.5), so a quantised mode reads 18.3
while drawing 9.1 - which is why sea life is not counted that way. Their row
therefore catches a mode that has stopped drawing and cannot catch one that
has been quantised. They are here because *four* modes moving is a scheduler
regression and *one* is this one, and because it was the three of them sitting
at 18.2 that ruled out the generator all four draw from.

ON A 5150 UNDER MARTYPC, because the tick, the pass and the halt are all cycle
counts at 4.77 MHz and QEMU cannot time anything (docs/TESTING.md).
"""
import sys, os, re, time, argparse, subprocess, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402

NFISH = 4
# The scheduler's slot count, READ from the kernel rather than kept here: the
# copy this replaced said 8 while sched.inc said 14, so sch_cycles was read
# six slots short. tools/os88geom.py mirrors it too, so t_mirror catches the
# next copy.
MAX_TASKS = os88sym.equates()["MAX_TASKS"]
MODES = [("cube", 1), ("starfield", 2), ("shapes", 4), ("sea life", 8)]

TICKRATE = 18.2065
KEEPUP = 17.0       # below this a mode is not getting its frame a tick...
HALTED = 12.0       # ...and above this the machine was asleep rather than busy.
                    # After the fix: 1.6-8.6% halted. Before it: 15.9-42.3%.
WINDOWS = 4
SECS = 2.0          # GUEST seconds a window (see the docstring). 2 s is ~36
                    # frames at one a tick, so the fastest swimmer covers 146
                    # of the 640 pixels it would need to cross and respawn -
                    # and 36 ticks is plenty to divide a halt fraction by
HZ = 4772727.0      # the 8088 this is all measured on


def offsets():
    """sv_fx / sv_fvx / sv_fs / sv_due / sv_ival, out of a listing of the image.

    Not a table of constants in this file: SAVER.DRV is loaded whole at
    DRVR_SEG:0000 with no relocation of any kind (SPEC.md 51), so a listing
    address IS the address - and four numbers written down here would go stale
    on the next line anybody adds to that image.
    """
    want = {"sv_fx": None, "sv_fvx": None, "sv_fs": None,
            "sv_due": None, "sv_ival": None}
    with tempfile.TemporaryDirectory() as tmp:
        lst = os.path.join(tmp, "saver.lst")
        subprocess.run(["nasm", "-f", "bin", "-w+error",
                        "-I", "drivers/", "-I", "apps/", "-I", "drivers/saver/",
                        "-I", "apps/wire/", "-l", lst,
                        "-o", os.path.join(tmp, "saver.bin"),
                        "drivers/saver/saver.asm"],
                       cwd=ROOT, check=True, capture_output=True)
        for line in open(lst, errors="replace"):
            for name in [k for k, v in want.items() if v is None]:
                if not re.search(r"\[%s( |\]|\+)" % name, line):
                    continue
                m = re.search(r"\[([0-9A-F]{4})\]", line)
                if m:                       # the listing prints it little-endian
                    b = m.group(1)
                    want[name] = int(b[2:4] + b[0:2], 16)
                break
    missing = [k for k, v in want.items() if v is None]
    if missing:
        raise SystemExit("saverate: no listing address for %s" % ", ".join(missing))
    return want


def word(b, i):
    v = int.from_bytes(b[i * 2:i * 2 + 2], "little")
    return v - 65536 if v >= 32768 else v


def sample(m, seg, o):
    b = m.read(m.sym("sch_cycles"), MAX_TASKS * 4)
    fx = m.readseg(seg, o["sv_fx"], NFISH * 2)
    fv = m.readseg(seg, o["sv_fvx"], NFISH * 2)
    return dict(
        t=int.from_bytes(m.read(m.sym("ticks"), 2), "little"),
        x=[word(fx, i) for i in range(NFISH)],
        v=[word(fv, i) for i in range(NFISH)],
        s=list(m.readseg(seg, o["sv_fs"], NFISH)),
        due=int.from_bytes(m.readseg(seg, o["sv_due"], 2), "little"),
        cyc=[int.from_bytes(b[i * 4:i * 4 + 4], "little") for i in range(MAX_TASKS)])


def frames(prev, cur, dticks, fish, ival):
    """Frames drawn between two samples, or None if this window cannot say."""
    if not fish:                            # no swimmer: [sv_due], and only
        n = ((cur["due"] - prev["due"]) & 0xFFFF) // ival   # while it keeps up
        return n if n <= dticks else None
    out = []
    for i in range(NFISH):
        if cur["v"][i] != prev["v"][i] or cur["s"][i] != prev["s"][i]:
            continue                        # respawned inside the window
        dx, v = cur["x"][i] - prev["x"][i], prev["v"][i]
        if v and dx and dx % v == 0 and 1 <= dx // v <= dticks:
            out.append(dx // v)
    return max(out) if out else None


def measure(m, o, name, bit, secs, windows):
    """(fps, halted %) over the windows this mode could be counted in."""
    m.write(m.sym("ss_modes"), bytes([bit]))
    m.write(m.sym("ss_secs"), b"\xff")       # one long turn: never re-picks
    m.write(m.sym("ss_idle"), b"\x1c\x00")   # ~1.5s of idle
    m.key("Space")
    t = time.time()
    while time.time() - t < 60 and m.read(m.sym("blk_sv"), 1)[0] != 1:
        time.sleep(0.2)
    if m.read(m.sym("blk_sv"), 1)[0] != 1:
        print("  %-10s NEVER STARTED" % name)
        return None
    seg = int.from_bytes(m.read(m.sym("ss_row") + 2, 2), "little")
    idle = m.read(m.sym("sch_idleslot"), 1)[0]
    time.sleep(2.0)                          # let the opening settle
    ival = m.readseg(seg, o["sv_ival"], 1)[0] or 1

    nf = nt = 0
    halt = busy = 0
    prev = sample(m, seg, o)
    for _ in range(windows):
        m.advance(cycles=int(secs * HZ))     # GUEST seconds, not host ones
        cur = sample(m, seg, o)
        dt = (cur["t"] - prev["t"]) & 0xFFFF
        n = frames(prev, cur, dt, bit == 8, ival)
        if n and dt:                         # only a window that CAN be counted
            d = [(cur["cyc"][i] - prev["cyc"][i]) & 0xFFFFFFFF
                 for i in range(MAX_TASKS)]
            nf, nt = nf + n * ival, nt + dt
            halt, busy = halt + d[idle], busy + sum(d)
        prev = cur

    m.run()                                  # advance() leaves it PAUSED, and
                                             # the teardown below is wall clock
    m.write(m.sym("ss_idle"), b"\x00\x40")   # a quarter hour: back to a desktop
    m.key("Space")
    time.sleep(2.0)
    if not nt or not busy:
        print("  %-10s no window could be counted" % name)
        return None
    return nf * TICKRATE / nt, 100.0 * halt / busy


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--secs", type=float, default=SECS,
                    help="GUEST seconds a window")
    ap.add_argument("--windows", type=int, default=WINDOWS)
    a = ap.parse_args()

    o = offsets()
    bad = 0
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        print("one frame a tick is %.2f fps; a mode below %.1f that is also "
              "over %.0f%% halted was ASLEEP while late"
              % (TICKRATE, KEEPUP, HALTED))
        for name, bit in MODES:
            got = measure(m, o, name, bit, a.secs, a.windows)
            if got is None:
                bad += 1
                continue
            fps, halted = got
            asleep = fps < KEEPUP and halted > HALTED
            print("  %-10s %5.2f fps   %4.1f%% halted   %s"
                  % (name, fps, halted,
                     "ASLEEP WHILE LATE (SPEC.md 79.5.7)" if asleep else
                     "keeping up" if fps >= KEEPUP else
                     "slow, but busy: the sea is expensive, not the scheduler"))
            bad += asleep
    print("saverate: %d finding(s)" % bad)
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
