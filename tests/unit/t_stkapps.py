#!/usr/bin/env python3
"""Every `ret` in every SHIPPED PACKAGE AND DRIVER is reached at its entry depth.

    python3 tests/unit/t_stkapps.py

`tests/suite.py`'s `stkbalance` row walks the kernel plus SHEET, CHART and four
shared includes.  This walks **all of `apps/` and all of `drivers/`**: 9,038
entries across every package, every shared SDK include, the three CPU cores and
the loadable drivers - the TCP/IP stack among them, which nothing had walked
either.  What made that impossible was not the code, it was three blind spots
in the walker, and each of them hid a whole class of file:

  * **`apps/*/*.inc` was in nobody's file list.**  `tools/stkbalance.py`'s own
    default globs `apps/*.inc` and `apps/*/*.asm` and stops, so RunCPM's Z80
    core, the C64's 6510 and Weave's VM - the three largest bodies of assembly
    in `apps/` - had never been walked by anything at all.
  * **The dispatch is `jmp [cs:bx+ed_tab]`.**  All three cores put a segment
    override in front and the table SECOND, so a walker looking for
    `jmp [tab + reg]` reads every opcode handler as a routine entered at depth
    zero and reports the lot.
  * **The branch is inside a macro.**  `wvm.inc` wraps `je %%o / jmp %1 / %%o:`
    in seven macros (`RNE`, `RAE`, ...) and uses them at fifty sites; read as
    bare mentions, each target looks like an address being taken.

The generated dispatch table is the reason this is a test rather than another
row of files in `suite.py`: `wvm_tab` is written by `tools/weavesim.py
--emit-optab` into `build/wvmtab.inc`, which only the on-demand `weave` targets
build.  Without it every WVM opcode handler is an unreferenced label and the
walk starts each one from zero.  So this file generates it, into a temp dir,
and a tree that has never built Weave still gets the coverage.

TWO ROUTINES ARE EXEMPT.  `drivers/net`'s `hd_path` pushes one handle per path
level, counted in CX, and gives them all back with `loop` on both its unwinds -
the count lives in a register, so no static walk can pair them, and the two
unwinds meet on a FORWARD edge where the walker's back-edge suppression does
not reach.  The other is the shape worth knowing: `wvm_exit` is the VM's
unwind.  Every refusal funnels there and leaves through `_wvm_slice.out`, which
gives back the registers the SLICE banked rather than the ones the opcode
handler that refused did - so a depth measured against the escaping routine is
meaningless.  It carries a `; STKBALANCE-OK:` saying exactly that, and the
slice's own balance is still checked by walking `_wvm_slice`.
"""
import glob
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness as h

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.path.join(ROOT, "tools", "stkbalance.py")
SIM = os.path.join(ROOT, "tools", "weavesim.py")


def main():
    files = []
    for top in ("apps", "drivers"):
        for pat in ("*.inc", os.path.join("*", "*.asm"), os.path.join("*", "*.inc")):
            files += sorted(glob.glob(os.path.join(ROOT, top, pat)))
    h.check(len(files) > 60, "the file list found the packages and drivers",
            why="a glob that silently matches nothing passes this row and "
                "defends none of it; %d files is not a package tree plus a "
                "driver tree" % len(files))

    with tempfile.TemporaryDirectory() as d:
        tab = os.path.join(d, "wvmtab.inc")
        gen = subprocess.run([sys.executable, SIM, "--emit-optab"],
                             capture_output=True, text=True, cwd=ROOT)
        h.eq(gen.returncode, 0, "weavesim --emit-optab generated the table",
             why="without wvm_tab every WVM opcode handler is an unreferenced "
                 "label, so the walk starts each one at depth 0 and the row "
                 "reports the whole dispatch.\n" + gen.stderr[-400:])
        if gen.returncode == 0:
            with open(tab, "w") as f:
                f.write(gen.stdout)
            files.append(tab)

        r = subprocess.run([sys.executable, TOOL] + files,
                           capture_output=True, text=True, cwd=ROOT)
        last = r.stdout.strip().split("\n")[-1]
        n = int(last.split()[1]) if last.startswith("stkbalance:") else -1
        h.eq(n, 0, "every ret in apps/ and drivers/ is reached at its entry depth",
             why="a `ret` at non-zero depth returns to a saved register - no "
                 "crash and no message, a black canvas and a wedged app "
                 "(SPEC.md 82.7.3, and `op_size` in os88parts.inc did it on a "
                 "malformed part table). Deliberate surgery declares itself "
                 "with `; STKBALANCE-NET:` or `; STKBALANCE-OK: <reason>`.\n"
                 + r.stdout)
    return h.done("apps/ and drivers/ stack balance")


if __name__ == "__main__":
    sys.exit(main())
