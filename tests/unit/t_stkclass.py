#!/usr/bin/env python3
"""Every package's DECLARED stack class covers its worker's deepest chain.

    python3 tests/unit/t_stkclass.py

`tests/unit/t_stkapps.py` walks every `ret` in `apps/` and `drivers/` and
checks it is reached at its entry depth - that the arithmetic BALANCES.  It
says nothing about whether the slice the package asked for is big enough to
hold that arithmetic, and until this file nothing did: `OS88_STACK_192` was a
number a human typed after reading `tools/stkdepth.py` once, and no gate ever
read it back.

**That is how the machine came to freeze.**  SPEC.md 8.7.4 is the account.
`stkdepth.py` followed `call` edges and not tail jumps, so `tm_worker` priced
at 56 bytes when the heap page it reaches by `ja tm_upd_heap` is 96; the Task
Manager was given 192 on the strength of 56, `tools/stkwater.py` measured 180
of those 192 with the heap page open beside PAINT, and on a real IBM 5150 the
deeper interrupt floor put it through the canary - `sch_switch` -> `sch_stkdie`
-> `cli`/`hlt`, a dead machine with no way to read why.

WHAT IT CHECKS.  For every package that spawns a worker: the class in the
built `.o88`'s header byte +15 (SPEC.md 8.7.2), against `stkdepth.py`'s deepest
chain from that worker's entry plus SPEC.md 8.7's 64-byte interrupt floor.
The bar is **1.25x**, which is not a round number - it is the thinnest margin
the tree already accepts, Frotz's 384 over a 240-byte chain
(docs/STACK-SLOTS-PLAN.md 12), so nothing shipping has to move to pass and
anything thinner than the worst thing here is new.

TWO THINGS IT DELIBERATELY DOES NOT DO.

  * **It does not price the KERNEL below an `OSAPI_*` call.**  `stkdepth.py`
    stops at "not in this image", so the 64-byte floor is the only allowance
    for everything on the far side of the far call.  The Task Manager measured
    180 against a 96 + 64 = 160 prediction, so that allowance is ~20 bytes
    light on a drawing worker - which is exactly why the bar is a ratio over
    the prediction and not `>= prediction`.
  * **It does not find the worker for you.**  The root is the symbol in the
    `mov ax, <entry>` that precedes `call OSAPI_TASK_SPAWN` - the SDK's own
    register contract - and a package whose worker cannot be found that way is
    reported, not silently skipped.  A skip that nobody sees is how a gate
    stops being one.
"""
import glob
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness as h

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.path.join(ROOT, "tools", "stkdepth.py")
BUILD = os.environ.get("OS88_BUILD", os.path.join(ROOT, "build"))

# SPEC.md 8.7's interrupt floor, from the worst real machine measured
# (docs/STACK-SLOTS-PLAN.md 7.1), and the thinnest margin the tree accepts.
FLOOR = 64
BAR = 1.25

# SPEC.md 8.7.2's header byte +15, an index into the kernel's sch_clsbytes.
# 0 and 4 are both "the largest", which is SCH_STACK.
CLASS_BYTES = {0: 384, 1: 128, 2: 192, 3: 256, 4: 384}
H_CLASS = 15

SPAWN = re.compile(r"^\s*call\s+OSAPI_TASK_SPAWN\b")
ENTRY = re.compile(r"^\s*mov\s+ax\s*,\s*([A-Za-z_][\w]*)\s*(?:;.*)?$")


def worker_of(path):
    """The near entry the package hands OSAPI_TASK_SPAWN, or None.

    The spawn is not always in the top-level .asm: TANK's is in tkattr.inc and
    BROWSER's in brnet.inc, and a gate that read only the .asm sized neither
    and said nothing - so every .inc beside the .asm is read too, the .asm
    first.
    """
    here = os.path.dirname(path)
    files = [path] + sorted(p for p in glob.glob(os.path.join(here, "*.inc")))
    for f in files:
        lines = open(f, errors="replace").read().splitlines()
        for i, ln in enumerate(lines):
            if not SPAWN.match(ln.split(";")[0]):
                continue
            for j in range(i - 1, max(-1, i - 8), -1):   # AX is loaded just above
                m = ENTRY.match(lines[j].split(";")[0].rstrip())
                if m:
                    return m.group(1)
    return None


def spawns(path):
    """Does any source of this package call OSAPI_TASK_SPAWN at all?"""
    here = os.path.dirname(path)
    for f in [path] + glob.glob(os.path.join(here, "*.inc")):
        for ln in open(f, errors="replace"):
            if SPAWN.match(ln.split(";")[0]):
                return True
    return False


def declared(o88):
    """The class byte out of the BUILT package, not out of the source."""
    with open(o88, "rb") as f:
        hdr = f.read(32)
    if len(hdr) < 32:
        return None
    return CLASS_BYTES.get(hdr[H_CLASS], 384)


def depth(asm, root):
    """stkdepth.py's deepest chain from `root`, in bytes."""
    # drivers/net is on the path for ftpd and Telnet: netpkg.inc is the socket
    # ABI they include from the DRIVER's tree (SPEC.md 72), so a package's own
    # directory is not enough to assemble one.
    r = subprocess.run([sys.executable, TOOL, asm, "-I", os.path.join(ROOT, "apps"),
                        "-I", BUILD, "-I", os.path.dirname(asm),
                        "-I", os.path.join(ROOT, "drivers", "net"),
                        "--from", root],
                       capture_output=True, text=True, cwd=ROOT, timeout=900)
    m = re.search(r"^== %s: (\d+) bytes ==" % re.escape(root), r.stdout, re.M)
    if not m:
        return None, (r.stdout + r.stderr)[:300]
    return int(m.group(1)), None


def main():
    rows, unfound, unbuilt = [], [], []
    for asm in sorted(glob.glob(os.path.join(ROOT, "apps", "*", "*.asm"))):
        app = os.path.basename(os.path.dirname(asm))
        root = worker_of(asm)
        if root is None:
            if spawns(asm):
                unfound.append(app)      # a spawner this scan could not size
            continue                     # no worker: nothing to size
        o88 = os.path.join(BUILD, "%s.o88" % os.path.splitext(os.path.basename(asm))[0])
        if not os.path.exists(o88):
            unbuilt.append(app)
            continue
        cls = declared(o88)
        got, why = depth(asm, root)
        if got is None:
            unfound.append("%s (%s): %s" % (app, root, why))
            continue
        rows.append((app, root, cls, got, cls / float(got + FLOOR)))

    h.check(not unfound,
            "every worker root resolves to a chain",
            "a package whose depth cannot be measured is one this gate is not "
            "watching, and a silent skip is how a gate stops being one",
            got="; ".join(unfound) or "none", want="none")

    thin = [r for r in rows if r[4] < BAR]
    h.check(not thin,
            "every declared class is at least %.2fx its chain + the %d-byte floor"
            % (BAR, FLOOR),
            "SPEC.md 8.7.4: a slice sized from a chain that was never walked is "
            "how the Task Manager came to overrun 192 bytes and halt the machine "
            "in sch_stkdie. The bar is Frotz's, the thinnest the tree already "
            "carries, so only something NEW can fail this",
            got="; ".join("%s (%s) %d bytes + %d floor in a %d slice = %.2fx"
                          % (a, w, d, FLOOR, c, r) for a, w, c, d, r in thin)
                or "none",
            want="none thinner than %.2fx" % BAR)

    worst = min(rows, key=lambda r: r[4]) if rows else None
    print("t_stkclass: %d worker%s measured%s, thinnest %s"
          % (len(rows), "" if len(rows) == 1 else "s",
             (", %d package(s) not built" % len(unbuilt)) if unbuilt else "",
             "%s %.2fx (%d + %d in %d)"
             % (worst[0], worst[4], worst[3], FLOOR, worst[2]) if worst else "-"))
    h.done("t_stkclass")


if __name__ == "__main__":
    main()
