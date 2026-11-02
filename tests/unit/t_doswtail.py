#!/usr/bin/env python3
"""The DOS window's tail is a GATE that goes red (docs/plans/DOS-HEADLESS-PLAN.md 4.4).

    python3 tests/unit/t_doswtail.py

Every `make` runs tools/doswtail.py over the shipped host: the resident host
may name the window's tail - `.wtext`, everything that only SHOWS the window -
only inside a WIN_ONLY bracket, whose opening is the `call dos_hq` (its
`cmp byte [dos_head], 0`) that a headless launch answers no to. A headless region does not hold the
tail at all, so an unbracketed reference names FREE MEMORY: a `call
con_write` runs whatever claim landed there and a `mov [con_cx], ax` writes
into it. No windowed row can see that, because a windowed region has the
tail and every such reference works.

So this row is the gate's own red: a gate nobody has seen fail is a gate that
may pass everything. It assembles the host three ways in a scratch directory
and asks the checker about each:

  1. as shipped - the checker must PASS;
  2. with the FIRST bracket's test left out (-DDOS_WBREAK=1, dos.asm's macro)
     - the checker must FAIL, naming a site;
  3. with a bracket in the MIDDLE left out - the same, so the gate is not
     only reading the first one.

nasm only; seconds. SOAK, being about one package (docs/WRITING-TESTS.md
2.1) - the every-build half of the gate is `make`'s own run of the checker.
"""
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                           # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
FLAGS = ["-f", "bin", "-w+error", "-I", "apps/", "-I", "apps/dos/", "-I",
         "drivers/net/", "-I", "kerndos/", "-DDOSKPART", "-DDOS_EXTCORE",
         "-DDOS_WIN16", "-DDOS_WTAIL"]


def nasm(out, *extra):
    r = subprocess.run(["nasm"] + FLAGS + list(extra) + ["-o", out,
                       "apps/dos/dos.asm"], cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        sys.exit("t_doswtail: nasm failed:\n" + r.stderr)


def checker(tmp, *extra):
    a, m, b = (os.path.join(tmp, n) for n in ("a.bin", "a.map", "b.bin"))
    nasm(a, "-DDOSP_MAP=" + m, *extra)
    nasm(b, "-DDOS_WSHIFT=0x110", *extra)
    r = subprocess.run([sys.executable, "tools/doswtail.py", "check", a, m, b],
                       cwd=ROOT, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def nbrackets(out):
    for line in out.splitlines():
        if "bracket(s)" in line:
            return int(line.split("; ")[1].split()[0])
    return 0


if not shutil.which("nasm"):
    sys.exit("t_doswtail: nasm is not installed (`make deps`)")
tmp = tempfile.mkdtemp(prefix="t_doswtail")
try:
    rc, out = checker(tmp)
    n = nbrackets(out)
    check(rc == 0, "the shipped host passes the tail checker",
          "every reference into the window's tail is inside a WIN_ONLY "
          "bracket, or a headless region runs or writes freed memory", got=out)
    check(n >= 10, "the host has its brackets (%d)" % n,
          "fewer than ten means the checker found no tail to guard - a build "
          "without DOS_WTAIL, or a bracket pattern it no longer reads")
    for which in (1, max(2, n // 2)):
        rc, out = checker(tmp, "-DDOS_WBREAK=%d" % which)
        check(rc != 0 and "outside a WIN_ONLY bracket" in out,
              "bracket %d of %d taken out: the checker goes RED" % (which, n),
              "a gate that passes a host with a reference left unguarded is "
              "not a gate", got=out[-400:])
finally:
    shutil.rmtree(tmp, ignore_errors=True)
done("t_doswtail")
