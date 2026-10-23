#!/usr/bin/env python3
"""RealDMG's damage renderer against its whole one, frame by frame.

    python3 tests/gbdamage.py [--frames N]

docs/REALDMG-SPEC.md section 13.2. rdg_renderd composes only the lines whose
inputs changed since its last call; this gate shows that what it leaves in
its buffer is, every time, exactly what rdg_render composes from nothing. The
bare harness (tests/rdgcore/rdghost.asm, MODE 6) plays each ROM under QEMU,
and every third frame composes it both ways and compares the two 5,760-byte
pictures; the DONE line counts the frames that differ, which must be none.

The ROMs:
- CLOCK25.GB, ours: tools/gbclock.py's frame counter, a map row rewritten
  every frame with the rest still.
- With $OS88_GB_ROMS, the owner's Super Mario Land and Kirby's Dream Land.
  These scroll, use the window, move sprites, write the raster log and
  rewrite tiles.

It also reports how many lines were composed against 144 a call - what the
renderer saved. QEMU counts work exactly; the time is another row's
(tests/rdgcore/rdgbench.py). Taking out the map-row marks (RDG_DMROW) leaves
CLOCK25's counter row stale, and the row fails naming the frame.
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tests", "rdgcore"))
import rdgimg  # noqa: E402
import rdgqemu  # noqa: E402

LINE = re.compile(r"RDGHOST DONE frames (\d+).*damage-bad (\d+) first (\d+) lines (\d+)")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--frames", type=int, default=1500)
    a = ap.parse_args()
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "gbclock.py"), "--busy", "25"],
                          stdout=subprocess.DEVNULL)
    roms = [("CLOCK25", os.path.join(ROOT, "build", "gbclock", "CLOCK25.GB"))]
    d = os.environ.get("OS88_GB_ROMS")
    if d:
        roms += [("SML", os.path.join(d, "SML.GB")), ("KIRBY", os.path.join(d, "KIRBY.GB"))]
    out = os.path.join(ROOT, "build", "realdmg")
    os.makedirs(out, exist_ok=True)
    fails = []
    # each also interlaced (REALDMG-SPEC 13.2), On, Scrolling and Doubled:
    # two calls a checked frame, which together must be the whole picture
    for name, rom, mode in [(n, r, m) for n, r in roms
                            for m in ("damage", "damage-il", "damage-is", "damage-id")]:
        name = name + {"damage": "", "damage-il": "/il", "damage-is": "/is",
                       "damage-id": "/id"}[mode]
        img = os.path.join(out, "gbdamage-%s.img" % name.replace("/", "-"))
        rdgimg.make_image(rom, img, mode=mode, frames=a.frames)
        text = rdgqemu.run(img, [" END\n", "RDGHOST BADOP"], 600, quiet=True)
        m = LINE.search(text)
        if not m:
            fails.append("%s: no DONE line (%s)" % (name, text.strip().splitlines()[-1:] or "nothing"))
            continue
        frames, bad, first, lines = map(int, m.groups())
        calls = frames // 3
        print("gbdamage %-10s %d frames, %d compared: %d differ; %.1f lines composed a call of 144"
              % (name, frames, calls, bad, lines / max(1, calls)))
        if frames < a.frames:
            fails.append("%s: stopped at frame %d" % (name, frames))
        if bad:
            fails.append("%s: %d of %d pictures differ, the first at frame %d" % (name, bad, calls, first))
    for f in fails:
        print("FAIL gbdamage " + f)
    print("gbdamage: %s" % ("PASS" if not fails else "FAIL"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
