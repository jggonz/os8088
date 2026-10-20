#!/usr/bin/env python3
"""PIXELSTEIN 3D's level rules, the EXPENSIVE one included (SPEC.md 97.7).

    python3 tests/unit/t_pxslevel.py

SOAK, beside a change to the package: `soak -k 'pxs*' -k 't_pxs*' -k 'pixelstein*'`. The fast row
(t_pxsgen) regenerates pxlev.inc with --no-sweep, which runs the cheap rules
- reachability with keys before doors, the counts, the sight line, the
melee rule - and skips the DDA sweep: every open cell x 16 headings, one ray
each through 97.2's walker, mean <= 12 crossings and worst <= 26. That sweep
is THE rule the frame table of 97.1 rests on (it is priced at ten crossings
a column), it is the rule E1M1 was re-carved for, and before this row it ran
only when a person typed `make pxsgen`: `$(BUILD)/pxslev.bin` is the only
Makefile rule that passes --check with the sweep and nothing in `all` or
`bench` depended on it - the instruments-break-silently shape.

So this row runs the level tool exactly as the stream's rule does - `--check
--stream <tmp>` on every level under apps/pixelstein/levels/ - and asserts
that it PASSED, that the sweep RAN (the tool prints the DDA mean and worst
only when it did), and that the stream it wrote is well-formed. Then the
negative control: a 40 x 40 open hall, written to a temporary file, must be
REFUSED by the sweep in words, because a gate that has never failed has not
been shown to be a gate.
"""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, eq, done                         # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.path.join(ROOT, "tools", "pxslevel.py")
LEVELS = os.path.join(ROOT, "apps", "pixelstein", "levels")
MAGIC = b"PXL\x01"


def run(args):
    r = subprocess.run([sys.executable, TOOL] + args, capture_output=True,
                       text=True, cwd=ROOT)
    return r.returncode, (r.stdout + r.stderr)


def main():
    tmp = tempfile.mkdtemp(prefix="pxslevel_")
    try:
        # --- the shipped levels, every rule, the sweep included --------------
        stream = os.path.join(tmp, "pxslev.bin")
        rc, out = run(["--check", "--stream", stream])
        check(rc == 0, "tools/pxslevel.py --check --stream passes on every level",
              why=out.strip()[-800:])
        levels = sorted(f for f in os.listdir(LEVELS) if f.endswith(".txt"))
        for f in levels:
            name = os.path.splitext(f)[0].upper()
            line = [l for l in out.splitlines()
                    if l.startswith("pxslevel: %s - " % name)]
            check(bool(line) and "DDA mean" in line[0],
                  "%s: the DDA sweep RAN (the tool printed its mean and worst)"
                  % name, why=out.strip()[-400:])
        check(os.path.exists(stream), "the stream was written", why=out[-400:])
        if os.path.exists(stream):
            data = open(stream, "rb").read()
            eq(data[:4], MAGIC, "the stream's first record carries 'PXL',1")
            check(data.count(MAGIC) >= len(levels),
                  "one record per level (%d levels, %d records)"
                  % (len(levels), data.count(MAGIC)))
        # --- the negative control: an open hall the sweep must refuse --------
        hall = os.path.join(tmp, "hall.txt")
        rows = ["#" * 42]
        rows += ["#" + "." * 40 + "#" for _ in range(40)]
        rows += ["#" * 42]
        rows[1] = "#@" + "." * 39 + "#"           # the spawn, facing east
        rows[40] = "#" + "." * 39 + "X#"          # the elevator switch, in a wall
        rows[20] = "#" + "." * 40 + "#"
        with open(hall, "w") as fh:
            fh.write("# the negative control: a 40 x 40 open hall\n")
            fh.write("\n".join(rows) + "\n")
        rc, out = run([hall, "-o", os.path.join(tmp, "hall.inc"),
                       "--stream", os.path.join(tmp, "hall.bin")])
        check(rc != 0, "an open 40 x 40 hall is REFUSED", why=out[-400:])
        check("crossings" in out, "...by the DDA sweep, in words (%s)"
              % (out.strip().splitlines()[-1] if out.strip() else "no output"))
        check(not os.path.exists(os.path.join(tmp, "hall.bin")),
              "...and no stream was written for it")
    finally:
        for f in os.listdir(tmp):
            os.unlink(os.path.join(tmp, f))
        os.rmdir(tmp)
    done("t_pxslevel")


if __name__ == "__main__":
    main()
