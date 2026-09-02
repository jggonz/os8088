#!/usr/bin/env python3
"""LOOM's compilers, built with the HOST cc, against `weavesim --pack`.

    python3 tests/unit/t_lmpack.py

WEAVE-SPEC 12.3.3. `apps/loom/hosttest/lmhost.c` `#include`s the five shipping
compiler sources - `lmerr.c`, `lmatom.c`, `lmwml.c`, `lmwjs.c`, `lmsheet.c`,
`lmwrite.c`, the text that ships and not a copy of it - stands the scratch and
output claims up as plain arrays, and packs a project. This row runs that over
every demo, every template and every case in `tests/weave/packerr/`, and
asserts two things WEAVE-SPEC pins:

  1. THE BUNDLE IS BYTE-IDENTICAL to `weavesim --pack`'s (WEAVE-SPEC 11.1).
     A failure names the first differing offset and the SECTION it lands in
     (WEAVE-SPEC 12.4), because "the bundles differ" is not a finding: a
     packer disagreement is almost always one field, and which section it is
     in says which of the five compilers to open.
  2. THE REFUSAL IS THE SAME SENTENCE (WEAVE-SPEC 10.5). The whole line,
     `<file>:<line>: <message>`, which is why the corpus files are named in
     the 8.3 spelling the machine would use.

IT IS NOT THE GATE, AND THE DIFFERENCE IS ONE WORD WIDE. `int` is 32 bits here
and 16 bits on the target, so the compilers are written never to depend on the
width and this row proves the LOGIC while `weavepack` - which packs ON THE
MACHINE and reads the bundle back off the guest's floppy - proves the
ARITHMETIC. Two instruments; a wave closes on the second. Said here because a
green run of this row is exactly the evidence somebody would mistake for the
gate.

WHY IT IS IN THE FAST TIER AT ALL, given that tier's "no build" rule. What it
compiles is not an artifact this tree ships: it is a host instrument over
sources `all` never touches, the same standing `tests/unit/t_wab.py` has as a
second reader. What it buys is that a change to `tools/weavesim.py` or to any
of the five compilers is caught by the next `make` rather than by the next
soak run - and those two are the pair whose whole contract is that they agree.

IT SKIPS RATHER THAN FAILS WITH NO HOST COMPILER. A clone with nasm and
python3 builds every floppy this project ships (CLAUDE.md), and a row that
turns that into a red suite would be reporting on the box instead of on the
tree.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                                # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.path.join(ROOT, "build")
HOST = os.path.join(BUILD, "lmhost")

SEC = {1: "UISTREAM", 2: "PROPS", 3: "CODE", 4: "ATOMS", 5: "FXCODE",
       6: "CELLS", 7: "SPRITES", 8: "ICON", 9: "SOURCE"}


def which(prog):
    for d in os.environ.get("PATH", "").split(os.pathsep):
        p = os.path.join(d, prog)
        if os.path.isfile(p) and os.access(p, os.X_OK):
            return p
    return None


def run(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, **kw)


def where(ref, off):
    """The section a byte offset falls in, read out of the REFERENCE bundle's
    own table - the other one may be malformed, which is itself the answer."""
    if len(ref) < 32 or ref[:4] != b"WAB\x1a":
        return "a file that does not begin WAB\\x1a"
    n = ref[12]
    if off < 32:
        return "the 32-byte header (WEAVE-SPEC 2.2), field at +%d" % off
    if off < 32 + 8 * n:
        return "the section table (WEAVE-SPEC 2.3), row %d field +%d" \
            % ((off - 32) // 8, (off - 32) % 8)
    for i in range(n):
        r = 32 + 8 * i
        t = ref[r]
        so = ref[r + 2] | (ref[r + 3] << 8)
        ln = ref[r + 4] | (ref[r + 5] << 8)
        if so <= off < so + ln:
            return "%s, +%d of %d" % (SEC.get(t, "type %d" % t), off - so, ln)
        if so <= off < so + ln + 16:
            return "the 0x00 padding after %s (WEAVE-SPEC 2.3)" % SEC.get(t, t)
    return "past every section - a tail the format does not have"


def firstdiff(a, b):
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            return i
    return min(len(a), len(b)) if len(a) != len(b) else -1


def projects():
    out = []
    demos = os.path.join(ROOT, "apps", "weave", "demos")
    for f in sorted(os.listdir(demos)):
        if f.endswith(".wml"):
            out.append(("demos/" + f, os.path.join(demos, f)))
    tdir = os.path.join(ROOT, "apps", "loom", "templates")
    if os.path.isdir(tdir):
        for d in sorted(os.listdir(tdir)):
            p = os.path.join(tdir, d, "MAIN.WML")
            if os.path.isfile(p):
                out.append(("templates/" + d, p))
    return out


def main():
    cc = os.environ.get("CC") or which("cc") or which("gcc") or which("clang")
    if not cc:
        print("t_lmpack: SKIP - no host C compiler on PATH; `weavepack` is "
              "the gate and this is its fast half (WEAVE-SPEC 12.3.3)")
        done("t_lmpack")
        return

    os.makedirs(BUILD, exist_ok=True)
    r = run(["python3", "tools/weavesim.py", "--emit-foldtab-c"])
    if not check(r.returncode == 0, "weavesim --emit-foldtab-c runs",
                 why="the fold table is GENERATED from tools/htmsim.py's one "
                     "definition (WEAVE-SPEC 3.1); a copy would drift",
                 got=r.stderr.strip()[:200], want="exit 0"):
        done("t_lmpack")
        return
    with open(os.path.join(BUILD, "lmfoldc.h"), "w") as f:
        f.write(r.stdout)

    r = run([cc, "-std=c89", "-O1", "-w", "-I", "build", "-o", HOST,
             "apps/loom/hosttest/lmhost.c"])
    if not check(r.returncode == 0, "the LOOM compilers build with the host cc",
                 why="lmhost.c #includes the SHIPPING sources; a C error here "
                     "is a C error in the package",
                 got=r.stderr.strip()[-800:], want="exit 0"):
        done("t_lmpack")
        return

    # --- 1. the bundles, byte for byte (WEAVE-SPEC 11.1) ---
    ref_p = os.path.join(BUILD, "t_lmpack_ref.wab")
    got_p = os.path.join(BUILD, "t_lmpack_got.wab")
    for name, wml in projects():
        r = run(["python3", "tools/weavesim.py", "--pack", wml, "-o", ref_p])
        if not check(r.returncode == 0, "weavesim packs %s" % name,
                     why="a project that the reference packer refuses is a "
                         "broken fixture, not a LOOM defect",
                     got=r.stderr.strip()[:200], want="exit 0"):
            continue
        r = run([HOST, wml, got_p])
        if not check(r.returncode == 0, "LOOM packs %s" % name,
                     why="LOOM refused a project weavesim packed - the two "
                         "must agree about what is legal as well as about "
                         "what the bytes are (WEAVE-SPEC 11.3)",
                     got=(r.stdout + r.stderr).strip()[:300], want="exit 0"):
            continue
        a = open(ref_p, "rb").read()
        b = open(got_p, "rb").read()
        i = firstdiff(a, b)
        check(a == b, "%s packs byte-identically (%d bytes)" % (name, len(a)),
              why="WEAVE-SPEC 11.1's gate. The first difference is at +0x%04X, "
                  "in %s" % (max(i, 0), where(a, max(i, 0))),
              got=b, want=a)

    # --- 2. the refusals, sentence for sentence (WEAVE-SPEC 10.5) ---
    corpus = os.path.join(ROOT, "tests", "weave", "packerr")
    cases = sorted(d for d in os.listdir(corpus)
                   if os.path.isdir(os.path.join(corpus, d)))
    check(len(cases) >= 30, "the packerr corpus has its cases",
          why="WEAVE-SPEC 12 says one case per rule; an empty directory would "
              "pass every assertion below and prove nothing",
          got=len(cases), want=">= 30")
    for c in cases:
        wml = os.path.join(corpus, c, "MAIN.WML")
        r = run(["python3", "tools/weavesim.py", "--pack", wml, "-o",
                 os.devnull])
        ref = (r.stderr.strip().split("\n") or [""])[-1]
        if ref.startswith("weavesim: "):
            ref = ref[len("weavesim: "):]
        if not check(r.returncode != 0 and ref,
                     "%s: weavesim refuses it" % c,
                     why="a corpus case that PACKS is a fixture that has "
                         "stopped exercising its rule",
                     got=r.returncode, want="non-zero with a sentence"):
            continue
        r = run([HOST, wml, os.devnull])
        got = ((r.stdout + r.stderr).strip().split("\n") or [""])[-1]
        check(got == ref, "%s: the same sentence" % c,
              why="WEAVE-SPEC 10.5 pins the sentences so that three "
                  "implementations refuse identically; this is the corpus "
                  "that holds two of them to it",
              got=got, want=ref)

    done("t_lmpack")


if __name__ == "__main__":
    main()
