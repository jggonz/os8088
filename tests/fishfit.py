#!/usr/bin/env python3
"""Does the most expensive sea the generator can roll still fit ONE TICK?

    make && python3 tests/fishfit.py [--machine os8088_5150_cga_gla]

SPEC.md 79.5.8. Sea life is the one saver mode that ever cost more than the
54.93 ms a `task_sleep(1)` parks for, and a mode over that tick cannot draw
18.2 frames a second whatever the scheduler does. On the kernel this runs on
it draws at what it COSTS - 76.23 ms a pass and 13.12 fps, measured against
the second draft (SPEC.md 79.5.8's table). On the kernel before SPEC.md
79.5.7 the same frame drew at 18.2 / (floor(work / 54.93) + 1), which is 9.1:
a mode a millisecond over the tick slept through the whole of the next one.
`tests/saverate.py` is the guard on THAT half and cannot be the guard on this
one, because a sea that legitimately costs 70 ms is slow and busy and passes
it. This row is the drawing half: the pass itself, against the tick.

**THE ASSERTION IS THE PASS, AND IT IS TAKEN IN GUEST CYCLES.** `sv_step` runs
once a frame and nowhere else, so the cycles between two consecutive entries
ARE the pass: the poll, the frame, and whatever sleep the deadline allowed. A
mode keeping up sits on exactly one tick and the number is flat to four digits
(54.90-54.95 over 199 frames when this was written); a mode that does not fit
runs at what it costs and the number goes over. There is no rate to convert
and no window to choose.

**IT FORCES THE SEA rather than waiting for one.** A scale is rolled per
swimmer, so an all-large sea is one roll in sixteen and a test that waited for
one would be a test that usually measured something cheaper. Both `sv_fs` and
`sv_fos` are written, or every frame takes the respawn path (SPEC.md 79.5.1)
and prices a scale change instead of a move.

ON A 5150 UNDER MARTYPC, because a pass is a cycle count at 4.77 MHz and QEMU
cannot time anything (docs/TESTING.md).
"""
import sys, os, re, time, argparse, subprocess, tempfile, statistics

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty                                            # noqa: E402

NFISH = 4
HZ = 4772727.0
TICK_MS = 1000.0 / 18.2065
BUDGET = TICK_MS * 1.02       # a pass at the tick, plus the width of one
                              # breakpoint's worth of rounding. A mode keeping
                              # up is FLAT here - 54.90 to 54.95 over 199
                              # consecutive frames - and the second draft,
                              # which did not fit, measured 76.23; so this line
                              # has 21 ms of clear air either side of it and
                              # separates two states rather than trimming one
FRAMES = 30


def offsets():
    """sv_fs / sv_fos and sv_step's address, out of a listing of the image.

    Not constants in this file: SAVER.DRV is loaded whole at DRVR_SEG:0000
    with no relocation of any kind (SPEC.md 51), so a listing address IS the
    address - and three numbers written down here would go stale on the next
    line anybody adds to that image.
    """
    with tempfile.TemporaryDirectory() as tmp:
        lst = os.path.join(tmp, "saver.lst")
        subprocess.run(["nasm", "-f", "bin", "-w+error",
                        "-I", "drivers/", "-I", "apps/", "-I", "drivers/saver/",
                        "-I", "apps/wire/", "-l", lst,
                        "-o", os.path.join(tmp, "saver.bin"),
                        "drivers/saver/saver.asm"],
                       cwd=ROOT, check=True, capture_output=True)
        text = open(lst, errors="replace").read().splitlines()
    out = {}
    for name in ("sv_fs", "sv_fos"):
        for line in text:
            if re.search(r"\[%s( |\]|\+)" % name, line):
                m = re.search(r"\[([0-9A-F]{4})\]", line)
                if m:
                    out[name] = int(m.group(1)[2:4] + m.group(1)[0:2], 16)
                    break
    seen = False                        # a code label has no address column of
    for line in text:                   # its own - nasm prints one only where
        if not seen:                    # bytes are emitted - so the label's
            seen = re.search(r"\ssv_step:\s*$", line) is not None
            continue                    # address is the first emitting line
        m = re.match(r"\s*\d+\s+([0-9A-F]{8})\s+[0-9A-F]", line)
        if m:
            out["sv_step"] = int(m.group(1), 16)
            break
    missing = [k for k in ("sv_fs", "sv_fos", "sv_step") if k not in out]
    if missing:
        raise SystemExit("fishfit: no listing address for %s" % ", ".join(missing))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--frames", type=int, default=FRAMES)
    ap.add_argument("--scale", type=int, default=2,
                    help="the size to pin every swimmer at (0 = leave it alone)")
    a = ap.parse_args()
    o = offsets()
    os.chdir(ROOT)
    out = []
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        m.write(m.sym("ss_modes"), bytes([8]))      # sea life, and only it
        m.write(m.sym("ss_secs"), b"\xff")          # one long turn: no re-pick
        m.write(m.sym("ss_idle"), b"\x1c\x00")      # ~1.5s of idle
        m.key("Space")
        t = time.time()
        while time.time() - t < 90 and m.read(m.sym("blk_sv"), 1)[0] != 1:
            time.sleep(0.2)
        if m.read(m.sym("blk_sv"), 1)[0] != 1:
            print("fishfit: the saver never started")
            return 1
        seg = int.from_bytes(m.read(m.sym("ss_row") + 2, 2), "little")
        m.advance(frames=40)                        # let the opening settle
        m.breakpoints([{"type": "execseg", "seg": seg, "off": o["sv_step"]}])
        prev = None
        for _ in range(a.frames + 1):
            if a.scale:
                for k in ("sv_fs", "sv_fos"):
                    m.write((seg << 4) + o[k], bytes([a.scale] * NFISH))
            m.run()
            if not m.wait_stop(40):
                print("fishfit: sv_step never reached")
                return 1
            c = m.status()["cycles"]
            if prev is not None:
                out.append(1000.0 * (c - prev) / HZ)
            prev = c
    med, worst = statistics.median(out), max(out)
    print("sea life, four swimmers at scale %d: pass median %.2f ms, worst "
          "%.2f ms -> %.2f fps  (one tick is %.2f ms, budget %.2f)"
          % (a.scale, med, worst, 1000.0 / med, TICK_MS, BUDGET))
    if worst > BUDGET:
        print("fishfit: OVER A TICK, so the mode cannot hold 18.2 fps "
              "(SPEC.md 79.5.8) - and on a kernel that quantises the poll it "
              "drops to the divisor below (SPEC.md 79.5.7): 1 finding")
        return 1
    print("fishfit: 0 findings")
    return 0


if __name__ == "__main__":
    sys.exit(main())
