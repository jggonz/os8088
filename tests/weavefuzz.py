#!/usr/bin/env python3
"""Two packers, a thousand mutated projects, and one question each.

    python3 tests/weavefuzz.py
    python3 tests/weavefuzz.py --seeds 7,11,23,99 --cases 250

WEAVE-SPEC 11.1 says LOOM's pack and `weavesim --pack` produce the same bytes.
`tests/unit/t_lmpack.py` holds them to it on eleven fixtures - seven projects
and forty refusals - which is exactly as many inputs as somebody thought of.
This row makes up inputs instead: it takes a committed project, damages it
(substitute, delete or insert a byte of the grammar's own alphabet, one to
three times, in any of its files), hands the wreckage to both packers, and
asks the two questions a fixture cannot:

  1. DID THEY AGREE ABOUT WHETHER IT IS A PROGRAM? One accepting what the
     other refuses is the worst failure this family has: a bundle that packs
     on the host and refuses on the machine, or - much worse - the other way
     round, where LOOM writes a `.WAB` out of a document weavesim would not
     have compiled and the runtime meets bytes no packer stands behind.
  2. WHEN BOTH ACCEPTED, ARE THE BYTES IDENTICAL? WEAVE-SPEC 11.1, asked of
     an input nobody chose.

MESSAGE TEXT IS REPORTED AND NOT ASSERTED, and the reason is structural
rather than a shrug. `tools/weavesim.py` scans a whole element - children and
close tag included - before it analyses any of it, so in a document with TWO
faults its SCANNER speaks first; LOOM analyses as it goes, so the semantic
fault inside an element that is also never closed is reported before the
missing close tag. Both refuse. They name different lines, and on the
single-fault documents an author actually types they agree sentence for
sentence, which is what `tests/weave/packerr/` proves over all forty rules.
Asserting equality here would be asserting that LOOM has a tree, which is a
design decision WEAVE-SPEC 11.4 took the other way (a tree is a second
allocator over the scratch claim for a structure read once).

So the divergence RATE is printed every run, and a run in which it moves a
long way is worth reading even though it does not fail. Measured when this
was written: 1,000 cases, **0 verdict disagreements, 0 byte disagreements**,
and 93 messages out of 1,000 naming a different line or fault - all of them
in doubly-broken documents.

THE SEEDS ARE FIXED so the row is deterministic: a fuzzer that finds
something on Tuesday and not on Wednesday is a bug report nobody can act on.
Change or add a seed to search somewhere new, and leave the old ones alone.
"""
import argparse
import os
import random
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(HERE, "unit"))
from harness import check, done                                # noqa: E402

BUILD = os.path.join(ROOT, "build")
HOST = os.path.join(BUILD, "lmhost")
TMP = os.path.join(BUILD, "lmfuzz")

# The alphabet a mutation draws from: the punctuation of all four grammars
# plus a few letters and digits. Random BYTES would mostly produce "not a
# Weave element" over and over; these produce documents that are nearly
# legal, which is where two implementations differ.
ALPHA = list(b'<>/"=&#. -abcABC019\n\t;{}()[]*+,:')


def which(prog):
    for d in os.environ.get("PATH", "").split(os.pathsep):
        p = os.path.join(d, prog)
        if os.path.isfile(p) and os.access(p, os.X_OK):
            return p
    return None


def sources():
    out = []
    d = os.path.join(ROOT, "apps", "weave", "demos")
    out.append([os.path.join(d, f) for f in sorted(os.listdir(d))])
    t = os.path.join(ROOT, "apps", "loom", "templates")
    if os.path.isdir(t):
        for name in sorted(os.listdir(t)):
            p = os.path.join(t, name)
            if os.path.isdir(p):
                out.append([os.path.join(p, f) for f in sorted(os.listdir(p))])
    return out


def damage(rnd, b):
    for _ in range(rnd.randint(1, 3)):
        if not b:
            b.append(rnd.choice(ALPHA))
            continue
        op = rnd.random()
        i = rnd.randrange(len(b))
        if op < 0.40:
            b[i] = rnd.choice(ALPHA)
        elif op < 0.70:
            del b[i:i + rnd.randint(1, 4)]
        else:
            b[i:i] = bytes([rnd.choice(ALPHA)])
    return b


def one(rnd, group):
    """Write one damaged project into TMP; answer the .WML's path."""
    shutil.rmtree(TMP, ignore_errors=True)
    os.makedirs(TMP)
    wml = None
    for p in group:
        b = bytearray(open(p, "rb").read())
        if rnd.random() < 0.75:
            b = damage(rnd, b)
        name = os.path.basename(p).upper()
        open(os.path.join(TMP, name), "wb").write(bytes(b))
        if name.endswith(".WML"):
            wml = os.path.join(TMP, name)
    return wml


def lastline(s):
    s = s.strip()
    if not s:
        return ""
    return s.split("\n")[-1].replace("weavesim: ", "")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", default="7,11,23,99")
    ap.add_argument("--cases", type=int, default=250)
    args = ap.parse_args()

    cc = os.environ.get("CC") or which("cc") or which("gcc") or which("clang")
    if not cc:
        print("weavefuzz: SKIP - no host C compiler on PATH")
        done("weavefuzz")
        return
    os.makedirs(BUILD, exist_ok=True)
    r = subprocess.run(["python3", "tools/weavesim.py", "--emit-foldtab-c"],
                       cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        check(False, "weavesim --emit-foldtab-c runs", got=r.stderr[:200])
        done("weavefuzz")
        return
    open(os.path.join(BUILD, "lmfoldc.h"), "w").write(r.stdout)
    r = subprocess.run([cc, "-std=c89", "-O1", "-w", "-I", "build", "-o",
                        HOST, "apps/loom/hosttest/lmhost.c"], cwd=ROOT,
                       capture_output=True, text=True)
    if not check(r.returncode == 0, "the LOOM compilers build with the host cc",
                 got=r.stderr[-600:], want="exit 0"):
        done("weavefuzz")
        return

    groups = sources()
    a_p = os.path.join(BUILD, "lmfuzz_a.wab")
    b_p = os.path.join(BUILD, "lmfuzz_b.wab")
    n = verdicts = bytesbad = msgs = 0
    for seed in [int(x) for x in args.seeds.split(",")]:
        rnd = random.Random(seed)
        for case in range(args.cases):
            wml = one(rnd, rnd.choice(groups))
            if wml is None:
                continue
            n += 1
            r1 = subprocess.run(["python3", "tools/weavesim.py", "--pack",
                                 wml, "-o", a_p], cwd=ROOT,
                                capture_output=True, text=True)
            r2 = subprocess.run([HOST, wml, b_p], cwd=ROOT,
                                capture_output=True, text=True)
            ok1 = r1.returncode == 0
            ok2 = r2.returncode == 0
            if ok1 != ok2:
                verdicts += 1
                keep = os.path.join(BUILD, "lmfuzz-%d-%d" % (seed, case))
                shutil.rmtree(keep, ignore_errors=True)
                shutil.copytree(TMP, keep)
                check(False, "seed %d case %d: the two packers DISAGREE about "
                             "whether it is a program" % (seed, case),
                      "one accepting what the other refuses is the worst "
                      "failure this family has - the kept copy is at %s"
                      % keep,
                      got="weavesim %s / LOOM %s"
                          % ("packed" if ok1 else lastline(r1.stderr),
                             "packed" if ok2 else lastline(r2.stdout
                                                           + r2.stderr)),
                      want="the same verdict")
                continue
            if ok1:
                a = open(a_p, "rb").read()
                b = open(b_p, "rb").read()
                if a != b:
                    bytesbad += 1
                    keep = os.path.join(BUILD, "lmfuzz-%d-%d" % (seed, case))
                    shutil.rmtree(keep, ignore_errors=True)
                    shutil.copytree(TMP, keep)
                    check(False, "seed %d case %d: the bytes differ"
                          % (seed, case),
                          "WEAVE-SPEC 11.1, asked of an input nobody chose. "
                          "`python3 tools/lmdiff.py` names the section; the "
                          "project is kept at %s" % keep,
                          got=b, want=a)
            elif lastline(r1.stderr) != lastline(r2.stdout + r2.stderr):
                msgs += 1

    check(verdicts == 0, "%d mutated projects, and the two packers never "
          "disagreed about whether one was a program" % n,
          "question 1 of the module docstring", got=verdicts, want=0)
    check(bytesbad == 0, "...and never about the bytes of one that was",
          "question 2, which is WEAVE-SPEC 11.1 itself", got=bytesbad, want=0)
    print("weavefuzz: %d cases, %d message divergences (%d%%) - reported, "
          "not asserted; see the module docstring" % (n, msgs,
                                                      (100 * msgs) // max(n, 1)))
    shutil.rmtree(TMP, ignore_errors=True)
    done("weavefuzz")


if __name__ == "__main__":
    main()
