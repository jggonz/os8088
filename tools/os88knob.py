#!/usr/bin/env python3
"""os88knob - put a rewrite of kernel code behind a NASM knob, mechanically.

    python3 tools/os88knob.py view  FILE [--on K1,K2] [--prefix KHP_]
    python3 tools/os88knob.py merge OUT BASE KNOB=VARIANT [KNOB=VARIANT ...]
    python3 tools/os88knob.py gate  TREE KNOB COMMIT|R:COMMIT [...]
    python3 tools/os88knob.py --selfcheck

WHAT A KNOB IS (kernel/kernel.asm's KHP block). One source carries two
bodies of a hot path: `%ifdef KNOB` the rewrite, `%else` the body it
replaced, so with the knob undefined NASM assembles exactly the old file and
with it defined exactly the new one. Kernel hot-paths pass 1 put seventeen
of its rewrites behind KHP_* knobs this way (docs/plans/khp1/LOG.md L.2,
L.3), and this tool is how.

  view   FILE with every conditional on --prefix's knobs evaluated (knobs in
         --on defined, every other one undefined) and the rest of the file as
         it is: what NASM would see of those knobs. The check that a merge
         did what it says.
  merge  THE SAFE FORM. BASE is the plain file; each VARIANT is BASE with ONE
         knob's change applied and nothing else. Each variant's differing
         regions become that knob's arms around BASE's lines, in one pass.
         Two knobs whose regions overlap or touch are refused: they belong
         under one knob.
  gate   A convenience over `merge` for one knob: each kernel/ file the
         commits touch is patched (GNU patch, fuzz 2; `R:<commit>` applies a
         commit in REVERSE, which is how a revert on the branch re-applies
         the change it undid in exactly this tree's context) and merged. It
         REFUSES a file that already holds --prefix arms: patching a file with
         two near-identical bodies in it can land a hunk in the wrong one,
         silently (it did, in softgfx.inc). Build variants of the plain file
         and use `merge` for a second knob in one file.

TWO RULES THE MERGE KEEPS, each learned from a defect it had:

  * **Every arm is BALANCED in NASM's conditionals.** A region the line diff
    cuts can open or close a `%if` block it does not contain - the rewrite's
    own `%ifdef KERN_BIG` arms, say - and the knob's arm then mis-nests with
    whatever surrounds it. So each region is widened with the identical
    lines around it until both arms hold every `%if` they open and no stray
    `%else`/`%endif`. Repeating identical lines in both arms changes nothing.
  * **Regions that touch are merged before either is widened**, or the
    widening steps over a line pair that is not equal.

THE PROOF IS THE BUILD, not this tool. At ONE commit (the build number is
the commit count, SPEC.md 14.2), assemble every kernel with the knob off and
compare to the tree without the change - byte-identical - and with it on and
compare to the tree with the change applied plainly - byte-identical.
`--selfcheck` fuzzes the merge itself; it does not replace that.

tests/unit/t_khpknobs.py (row `khpknobs`) assembles kern_big and kern_small
with every KHP_* knob on, so an arm no shipped kernel assembles cannot rot.
"""
import difflib
import os
import random
import re
import subprocess
import sys
import tempfile

_IF = re.compile(r"^\s*%if")
_EI = re.compile(r"^\s*%endif\b")
_EL = re.compile(r"^\s*%el(se|if)")


def balanced(lines):
    """(ok, grow_up): every %if closed inside, no stray %else/%endif."""
    d = 0
    for ln in lines:
        if _IF.match(ln):
            d += 1
        elif _EI.match(ln):
            d -= 1
            if d < 0:
                return False, True
        elif _EL.match(ln) and d == 0:
            return False, True
    return d == 0, False


def regions(A, B):
    """[i1, i2, j1, j2] spans where A and B differ, widened until balanced."""
    gs = [[i1, i2, j1, j2] for op, i1, i2, j1, j2 in
          difflib.SequenceMatcher(None, A, B, autojunk=False).get_opcodes()
          if op != "equal"]
    k = 0
    while k < len(gs):
        g = gs[k]
        while True:
            oka, ua = balanced(A[g[0]:g[1]])
            okb, ub = balanced(B[g[2]:g[3]])
            if oka and okb:
                break
            if ua or ub:
                if k > 0 and g[0] <= gs[k - 1][1]:
                    p = gs.pop(k - 1)
                    k -= 1
                    g[0], g[2] = min(g[0], p[0]), min(g[2], p[2])
                    continue
                if g[0] == 0 or g[2] == 0:
                    raise SystemExit("os88knob: cannot balance at the head")
                g[0] -= 1
                g[2] -= 1
            else:
                if k + 1 < len(gs) and g[1] >= gs[k + 1][0]:
                    n = gs.pop(k + 1)
                    g[1], g[3] = max(g[1], n[1]), max(g[3], n[3])
                    continue
                if g[1] >= len(A) or g[3] >= len(B):
                    raise SystemExit("os88knob: cannot balance at the end")
                g[1] += 1
                g[3] += 1
        k += 1
    return gs


def merge_lines(A, variants):
    """A gated by each (knob, B): one source for every knob combination."""
    rs = []
    for knob, B in variants:
        for i1, i2, j1, j2 in regions(A, B):
            rs.append((i1, i2, B[j1:j2], knob))
    rs.sort(key=lambda r: (r[0], r[1]))
    for a, b in zip(rs, rs[1:]):
        if b[0] <= a[1]:
            raise SystemExit("os88knob: %s and %s overlap or touch at lines "
                             "%d-%d / %d-%d - put them under one knob"
                             % (a[3], b[3], a[0] + 1, a[1], b[0] + 1, b[1]))
    out, ia = [], 0
    for i1, i2, new, knob in rs:
        out.extend(A[ia:i1])
        old = A[i1:i2]
        if new and old:
            out += ["%ifdef " + knob] + new + ["%else"] + old + ["%endif"]
        elif new:
            out += ["%ifdef " + knob] + new + ["%endif"]
        else:
            out += ["%ifndef " + knob] + old + ["%endif"]
        ia = i2
    out.extend(A[ia:])
    return out


def view_lines(lines, on, prefix="KHP_"):
    knob = re.compile(r"^\s*%(ifn?def)\s+(" + re.escape(prefix) + r"\w+)\b")
    st, out = [], []
    for raw in lines:
        m = knob.match(raw)
        live = all(a for _, a in st)
        if m:
            d = m.group(2) in on
            st.append(["k", d if m.group(1) == "ifdef" else not d])
            continue
        if _IF.match(raw):
            st.append(["o", True])
        elif st and re.match(r"^\s*%else\b", raw) and st[-1][0] == "k":
            st[-1][1] = not st[-1][1]
            continue
        elif st and _EI.match(raw):
            if st.pop()[0] == "k":
                continue
        if live:
            out.append(raw)
    return out


def read(p):
    with open(p, encoding="utf-8") as f:
        return f.read().split("\n")


def write(p, lines):
    with open(p, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def cmd_view(args):
    on, prefix = set(), "KHP_"
    path = None
    it = iter(args)
    for a in it:
        if a == "--on":
            on = set(next(it).split(","))
        elif a == "--prefix":
            prefix = next(it)
        else:
            path = a
    sys.stdout.write("\n".join(view_lines(read(path), on, prefix)))


def cmd_merge(args):
    out, base = args[0], args[1]
    A = read(base)
    vs = []
    for a in args[2:]:
        knob, path = a.split("=", 1)
        vs.append((knob, read(path)))
    res = merge_lines(A, vs)
    write(out, res)
    print("os88knob: %s from %d variant(s)" % (out, len(vs)))


def cmd_gate(args, prefix="KHP_"):
    tree, knob, commits = args[0], args[1], args[2:]
    rev = {c[2:] for c in commits if c.startswith("R:")}
    commits = [c[2:] if c.startswith("R:") else c for c in commits]
    git = lambda *a: subprocess.run(["git", "-C", tree] + list(a),
                                    capture_output=True, text=True,
                                    check=True).stdout
    files = []
    for c in commits:
        for f in git("show", "--name-only", "--format=", c).split():
            if f.startswith("kernel/") and f not in files:
                files.append(f)
    arm = re.compile(r"^\s*%ifn?def\s+" + re.escape(prefix), re.M)
    for f in files:
        path = os.path.join(tree, f)
        with open(path, encoding="utf-8") as fh:
            if arm.search(fh.read()):
                sys.exit("os88knob: %s already holds %s* arms; build variants "
                         "of its plain form and use `merge`" % (f, prefix))
    for f in files:
        path = os.path.join(tree, f)
        with tempfile.TemporaryDirectory() as td:
            cur = os.path.join(td, "f")
            write(cur, read(path))
            for c in commits:
                patch = git("show", "--format=", c, "--", f)
                if not patch.strip():
                    continue
                nxt = cur + "+"
                r = subprocess.run(["patch", "--fuzz=2", "-s"] +
                                   (["-R"] if c in rev else []) +
                                   ["-o", nxt, cur], input=patch,
                                   capture_output=True, text=True)
                if r.returncode != 0:
                    sys.exit("os88knob: %s does not apply to %s: %s%s"
                             % (c, f, r.stdout, r.stderr))
                cur = nxt
            write(path, merge_lines(read(path), [(knob, read(cur))]))
        print("os88knob: %s -> %s" % (f, knob))


def selfcheck(n=400, seed=8088):
    """Random NASM-shaped files with nested conditionals; random edits on
    two independent knobs; every combination's view must be its variant."""
    rnd = random.Random(seed)

    def block(depth):
        out = []
        for _ in range(rnd.randint(1, 6)):
            r = rnd.random()
            if r < 0.25 and depth < 3:
                out.append("%ifdef KERN_BIG")
                out += block(depth + 1)
                if rnd.random() < 0.5:
                    out.append("%else")
                    out += block(depth + 1)
                out.append("%endif")
            else:
                out.append("    insn_%d" % rnd.randint(0, 40))
        return out

    def edit(lines, lo, hi):
        b = list(lines)
        for _ in range(rnd.randint(1, 3)):
            i = rnd.randint(lo, max(lo, min(hi, len(b)) - 1))
            r = rnd.random()
            if r < 0.4:
                b[i:i] = block(1)
            elif r < 0.7 and i < len(b) and not b[i].startswith("%"):
                b[i] = "    new_%d" % rnd.randint(0, 99)
            elif i < len(b) and not b[i].startswith("%"):
                del b[i]
        return b

    bad = 0
    for _ in range(n):
        A = block(0) + block(0) + block(0) + block(0)
        h = len(A) // 2
        B1 = edit(A[:h], 0, h) + A[h:]
        B2 = A[:h] + edit(A[h:], 0, len(A) - h)
        try:
            M = merge_lines(A, [("KHP_A", B1), ("KHP_B", B2)])
        except SystemExit:
            continue                    # touching: refused, which is correct
        both = B1[:len(B1) - (len(A) - h)] + B2[h:]
        for on, want in ((set(), A), ({"KHP_A"}, B1), ({"KHP_B"}, B2),
                         ({"KHP_A", "KHP_B"}, both)):
            if view_lines(M, on) != want:
                bad += 1
                break
            if not balanced(M)[0]:
                bad += 1
                break
    print("os88knob selfcheck: %d random cases, %d wrong" % (n, bad))
    return 1 if bad else 0


def main():
    a = sys.argv[1:]
    if a and a[0] == "--selfcheck":
        return selfcheck()
    if len(a) < 2:
        print(__doc__)
        return 2
    {"view": cmd_view, "merge": cmd_merge, "gate": cmd_gate}[a[0]](a[1:])
    return 0


if __name__ == "__main__":
    sys.exit(main())
