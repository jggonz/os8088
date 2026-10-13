#!/usr/bin/env python3
"""SPEC.md 107.10: a DESKTOP INTEGRATED APPLICATION's shared half, between
MartyPC runs.

`apps/os88dia.inc` is an SDK include (docs/INDEX.md lists it) and
`tools/os88dia.py` is the independent host reader and writer every DIA gate's
fixture comes from. Until this row, nothing in `make` touched either: the
include was assembled only by `make diatest`, the host tool's `--selfcheck`
was run by nobody, and the two agreed about the DIA header only because they
were written the same afternoon. So:

  A  `tools/os88dia.py --selfcheck` passes - its own round trips and every
     refusal it mirrors from the boot reader and from dia_load.
  B  `tests/diaprobe/diaprobe.asm` ASSEMBLES (into a temp file, never
     /dev/null - CLAUDE.md), so os88dia.inc is assembled by every `make`.
  C  the DIA header's layout in os88dia.inc (`DIA_H_*`, DIA_HDR_SIZE, the
     magic, the version, the position flag) is where os88dia.py's writer puts
     each field - checked by writing a header with the host tool and reading
     it back at the include's offsets, which is the comparison that matters.
  D  the host tool's copies of kern_big's DERIVED constants - CFG_FBUF (a
     SYSTEM.CFG this size or less carries no trailer the boot reads) and
     DESK_SC0 (the first shortcut's zone) - equal the kernel's, evaluated BY
     NASM from the kernel's own `equ` chain. Both are expressions, which
     os88geom deliberately refuses to evaluate, so t_mirror cannot hold them:
     CFG_FBUF went 131 -> 141 when #248 added two port-pin keys, and the
     second reader then disagreed with the machine about a 132-141-byte file
     with nothing failing. (DVOL_MAX and SC_MAX are plain integers and are
     os88geom's.)

**BREAK IT ON PURPOSE**: set os88dia.py's CFG_FBUF to 131 and D fails; move
DIA_H_SW to 9 in os88dia.inc and C fails; put a 32-bit register in
os88dia.inc and B fails.
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, done                           # noqa: E402
import os88dia                                            # noqa: E402

NASM = os.environ.get("NASM", "nasm").split()[0]
EQU = re.compile(r"^\s*([A-Z_][A-Z0-9_]*)\s+equ\s+([^;\n]+)", re.M)
COND = re.compile(r"^\s*%(\w+)\s*(\S*)")
IDENT = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")


def plain_equs(path):
    """{NAME: int} for os88dia.inc's plain-integer equs."""
    out = {}
    for name, val in EQU.findall(open(path, errors="replace").read()):
        try:
            out[name] = int(val.strip(), 0)
        except ValueError:
            pass
    return out


def kernel_equs(arm):
    """{NAME: expression} over every kernel source, the OTHER arm's
    `%ifdef KERN_BIG` / `%ifdef KERN_SMALL` blocks folded out (os88geom's
    rule, and its feature-symbol map), every other conditional opaque and
    first-wins."""
    try:
        import os88geom
        karm = os88geom._KARM
    except Exception:
        karm = {"KERN_BIG": "big", "KERN_SMALL": "small"}
    out = {}
    kdir = os.path.join(ROOT, "kernel")
    files = sorted(os.path.join(kdir, f) for f in os.listdir(kdir)
                   if f.endswith((".inc", ".asm")))
    for path in files:
        stack = []
        for line in open(path, errors="replace"):
            mo = COND.match(line)
            if mo:
                d, a = mo.group(1).lower(), mo.group(2)
                if d in ("ifdef", "ifndef") and a in karm:
                    stack.append(["k", (karm[a] == arm) == (d == "ifdef")])
                elif d.startswith("if"):
                    stack.append(["?", True])
                elif d == "else" and stack and stack[-1][0] == "k":
                    stack[-1][1] = not stack[-1][1]
                elif d.startswith("eli") and stack:
                    stack[-1] = ["?", True]
                elif d == "endif" and stack:
                    stack.pop()
                continue
            if not all(e[1] for e in stack):
                continue
            mo = EQU.match(line)
            if mo:
                out.setdefault(mo.group(1), mo.group(2).strip())
    return out


def nasm_eval(names, arm, tmp):
    """The kernel's own value of each of `names`, as nasm evaluates the
    kernel's own expressions. Raises with the reason when a name in the chain
    is not an `equ` (a label, a %define) - a loud refusal, not a guess."""
    eq = kernel_equs(arm)
    order, seen = [], set()

    def need(n):
        if n in seen:
            return
        seen.add(n)
        if n not in eq:
            raise KeyError("%s is not a kernel `equ` under kern_%s" % (n, arm))
        for dep in IDENT.findall(eq[n]):
            if dep in eq or dep.isupper():
                need(dep)
        order.append(n)
    for n in names:
        need(n)
    src = os.path.join(tmp, "eval_%s.asm" % arm)
    out = os.path.join(tmp, "eval_%s.bin" % arm)
    with open(src, "w") as f:
        f.write("cpu 8086\n")
        for n in order:
            f.write("%s equ %s\n" % (n, eq[n]))
        for n in names:
            f.write("dw %s\n" % n)
    r = subprocess.run([NASM, "-f", "bin", "-o", out, src],
                       capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError("nasm refused the chain: %s" % r.stderr.strip())
    data = open(out, "rb").read()
    return dict(zip(names, struct.unpack("<%dH" % len(names), data)))


def main():
    tmp = tempfile.mkdtemp(prefix="t_dia.")
    try:
        # A ---------------------------------------------------------------
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                         "os88dia.py"),
                            "--selfcheck"], capture_output=True, text=True,
                           cwd=ROOT)
        check(r.returncode == 0, "tools/os88dia.py --selfcheck passes",
              why="every DIA gate's fixture is written by it, and its "
                  "refusals are the boot reader's and dia_load's",
              got=(r.stdout + r.stderr)[-400:], want="exit 0")

        # B ---------------------------------------------------------------
        out = os.path.join(tmp, "diaprobe.bin")
        r = subprocess.run([NASM, "-f", "bin", "-w+error", "-I", "apps/",
                            "-o", out, "tests/diaprobe/diaprobe.asm"],
                           capture_output=True, text=True, cwd=ROOT)
        check(r.returncode == 0 and os.path.getsize(out) > 64,
              "tests/diaprobe/diaprobe.asm assembles: os88dia.inc builds",
              why="the include is SDK; without this only `make diatest` "
                  "assembled it, so an edit that broke it passed `make` and "
                  "`make test-full`",
              got=r.stderr.strip()[-400:], want="a package image")

        # C ---------------------------------------------------------------
        inc = plain_equs(os.path.join(ROOT, "apps", "os88dia.inc"))
        check(inc.get("DIA_HDR_SIZE") == os88dia.DIA_HDR_SIZE,
              "DIA_HDR_SIZE agrees", got=inc.get("DIA_HDR_SIZE"),
              want=os88dia.DIA_HDR_SIZE)
        check(struct.pack("<H", inc.get("DIA_HDR_MAGIC", 0))
              == os88dia.DIA_HDR_MAGIC, "the 'DA' magic agrees",
              got=inc.get("DIA_HDR_MAGIC"), want=os88dia.DIA_HDR_MAGIC)
        check(inc.get("DIA_HDR_VER") == os88dia.DIA_HDR_VER,
              "the header version agrees", got=inc.get("DIA_HDR_VER"),
              want=os88dia.DIA_HDR_VER)
        check(inc.get("DIA_HF_POS") == os88dia.DIA_HF_POS,
              "the position flag agrees", got=inc.get("DIA_HF_POS"),
              want=os88dia.DIA_HF_POS)
        pay = b"\xAA" * 7
        h = os88dia.dat(pay, x=0x1234, y=0x2345, screen=(0x3456, 0x4567),
                        payver=0x56)
        fields = [("DIA_H_MAGIC", "<H", struct.unpack("<H",
                                                      os88dia.DIA_HDR_MAGIC)[0]),
                  ("DIA_H_VER", "<B", os88dia.DIA_HDR_VER),
                  ("DIA_H_FLAGS", "<B", os88dia.DIA_HF_POS),
                  ("DIA_H_X", "<H", 0x1234), ("DIA_H_Y", "<H", 0x2345),
                  ("DIA_H_SW", "<H", 0x3456), ("DIA_H_SH", "<H", 0x4567),
                  ("DIA_H_PAYVER", "<B", 0x56), ("DIA_H_LEN", "<H", len(pay))]
        for name, fmt, want in fields:
            off = inc.get(name)
            got = (struct.unpack_from(fmt, h, off)[0]
                   if off is not None and off + struct.calcsize(fmt) <= len(h)
                   else None)
            check(got == want, "os88dia.inc's %s (+%s) is where os88dia.py "
                  "writes that field" % (name, off), got=got, want=want,
                  why="the package and the host tool read one file; a skew "
                      "is a state file every boot calls DAMAGED")

        # D ---------------------------------------------------------------
        try:
            k = nasm_eval(["CFG_FBUF", "DESK_SC0"], "big", tmp)
        except (KeyError, RuntimeError) as e:
            k = {}
            check(False, "the kernel's CFG_FBUF and DESK_SC0 evaluate",
                  got=str(e), want="two words")
        for name in ("CFG_FBUF", "DESK_SC0"):
            check(k.get(name) == getattr(os88dia, name),
                  "tools/os88dia.py's %s is kern_big's" % name,
                  got=getattr(os88dia, name), want=k.get(name),
                  why="the host tool is the second reader of SYSTEM.CFG's "
                      "trailer; a stale copy disagrees with ovl_sc_load "
                      "about which files carry one, and no gate fails")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    done("t_dia")


if __name__ == "__main__":
    main()
