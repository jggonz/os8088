#!/usr/bin/env python3
"""Every FIXED toast message fits the bar (SPEC.md 59.10).

    python3 tests/unit/t_toast.py

`toast_show` copies at most `TOAST_MAX` characters and drops the rest
**silently**.  That cap is GEOMETRY, not a budget: the clock's field is 25
cells on every screen this runs on and the strip spends one of them on the gap
that keeps its bed off SPEC.md 12.8's widget, so it cannot be raised without
moving the toast somewhere a window can cover it - which is the arrangement
SPEC.md 59 exists to have left behind.

So the rule is that every fixed message is WRITTEN to fit, and
`kernel/toast.inc` claims in as many words that *"every message in the tree
was revised to fit rather than left to truncate"*.  That claim was true of the
tree it was written on and nothing held it: a bug report off a 286 -
`Hibernation file is from another build` arriving as `Hibernation file is
from` - found **twenty-one** strings over the cap, ten of them in
`HIBER.DRV`, which is a MODULE and was never walked at all.  The six DOS
handoff refusals were the sharpest: the comment above them says a refusal has
to say WHICH step failed because *"kern_dos could not be reached is true of
all six and useful for none"*, and all six truncated to something equally
useless.

HOW IT LOOKS, and the direction it is wrong in.  A toast argument cannot be
resolved exactly from source - it arrives in SI, AX or BX, through wrappers,
through shared `jmp` tails, and sometimes composed into a buffer at runtime.
So this OVER-APPROXIMATES: it takes every `db` string that any procedure
which toasts loads into a register, plus every string named by a `dw` table
such a procedure loads.  That cannot MISS a fixed toast string, which is the
direction that matters; what it produces instead is false positives - a
routine that draws an About box and also toasts one line of it - and
`tests/toastlong.txt` is where those go, with a reason and as a ratchet.

A string composed at runtime is not checked here and cannot be: a file name is
the user's and may be any length.  Those are what the cap's truncation is
FOR, and SPEC.md 59.10 says which is which.

AND THE CLOSURE IS TIGHT, which is what makes over-approximating affordable:
the wrapper walk runs to a FIXED POINT and reaches **249 of the tree's 13,158
top-level labels, 1.9%** - so it is not quietly converging on "every procedure
in the program", where a long string anywhere would need a registry line.  If
that ratio ever climbs, the registry is the thing that will say so, by
filling up with strings nobody toasts.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
REG = os.path.join(ROOT, "tests", "toastlong.txt")

SRC_GLOBS = ("kernel/*.inc", "kernel/*.asm", "apps/**/*.asm", "apps/*.inc",
             "drivers/**/*.asm", "drivers/**/*.inc")

DB = re.compile(r"^(\w+):\s*db\s+(.*)$")
DW = re.compile(r"^(\w+):\s*dw\s+(.*)$")
TOP = re.compile(r"^([A-Za-z_]\w*):")
# `cw_toast_show` is the kernel's own far thunk and `OSAPI_TOAST` the SDK's
# cell; `toast_say` takes a TABLE in BX rather than a string in SI.
CALL = re.compile(r"call\s+(?:\w+:)?(?:cw_)?(?:toast_show|toast_say)\b"
                  r"|call\s+OSAPI_TOAST\b")
LOAD = re.compile(r"mov\s+(?:si|ax|bx|cx|dx|di|bp),\s*(\w+)\s*$")
# The cap lives in kernel/toast.inc and is READ from it, never transcribed -
# t_mirror.py's rule applied to the one constant this file is about.
CAP = re.compile(r"^TOAST_MAX\s+equ\s+(\d+)")


def fail(msg):
    print("t_toast: FAIL: %s" % msg)
    sys.exit(1)


def sources():
    import glob
    out = []
    for pat in SRC_GLOBS:
        out += glob.glob(os.path.join(ROOT, pat), recursive=True)
    return sorted(out)


def cap():
    p = os.path.join(ROOT, "kernel", "toast.inc")
    for line in open(p, encoding="utf-8", errors="replace"):
        m = CAP.match(line)
        if m:
            return int(m.group(1))
    fail("kernel/toast.inc has no `TOAST_MAX equ <n>` - this row reads the "
         "cap out of the source rather than carrying a copy of it")


def registry():
    if not os.path.exists(REG):
        fail("tests/toastlong.txt is missing - it is the registry this "
             "checker's false positives live in")
    out = {}
    for n, line in enumerate(open(REG, encoding="utf-8"), 1):
        line = line.split("#")[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) != 2:
            fail("tests/toastlong.txt:%d is %r; a line is `<label> <path>` "
                 "and then a `#` reason" % (n, line))
        out[parts[0]] = parts[1]
    return out


def main():
    MAX = cap()
    reg = registry()
    text, strings, tables = {}, {}, {}
    for f in sources():
        rel = os.path.relpath(f, ROOT)
        text[rel] = open(f, encoding="utf-8", errors="replace").read().split("\n")
        for line in text[rel]:
            line = line.split(";")[0].rstrip()
            m = DB.match(line)
            if m:
                parts = re.findall(r"'([^']*)'|\"([^\"]*)\"", m.group(2))
                txt = "".join(a or b for a, b in parts)
                if txt:
                    strings[m.group(1)] = (rel, txt)
            m = DW.match(line)
            if m:
                tables.setdefault(m.group(1), [rel, []])[1].extend(
                    w.strip() for w in m.group(2).split(","))

    # THE PROCEDURE INDEX IS BUILT ONCE, and that is not tidiness: the
    # obvious spelling of step 2 - for every file, for every toasting proc,
    # scan every proc - is files x procs x procs and ran this row in 13
    # SECONDS, against a 30-second budget for the whole fast tier. One
    # alternation and two linear passes is under a second.
    pindex = {}
    for rel in text:
        ix = [(m.group(1), i) for i, l in enumerate(text[rel])
              for m in [TOP.match(l)] if m]
        pindex[rel] = [(nm, i, ix[k + 1][1] if k + 1 < len(ix) else len(text[rel]))
                       for k, (nm, i) in enumerate(ix)]

    def scan(pattern):
        """{rel: {proc}} for every procedure with a line matching pattern."""
        hit = {}
        for rel in text:
            marks = [i for i, l in enumerate(text[rel])
                     if pattern.search(l.split(";")[0])]
            if not marks:
                continue
            k, ms = 0, sorted(marks)
            for nm, a, b in pindex[rel]:
                while k < len(ms) and ms[k] < a:
                    k += 1
                if k < len(ms) and ms[k] < b:
                    hit.setdefault(rel, set()).add(nm)
        return hit

    # 1. procedures that toast...
    toasty = scan(CALL)
    if not toasty:
        fail("no procedure in the tree calls a toast at all - this checker's "
             "call pattern has stopped matching and it is passing vacuously")

    # 2. ...and whoever calls one of those, TO A FIXED POINT. A wrapper takes
    #    the string in whatever register it likes, so the CALLER is where the
    #    label is - `hbm_toast`, `wd_saymsg` and six more are that shape - and
    #    a wrapper around a wrapper is a shape nothing forbids. Iterating is
    #    two or three passes over an alternation and costs nothing; ONE level
    #    is a number somebody chose, and the first draft of this row got 129
    #    procedures out of a loop that mutated the dict it was walking, which
    #    is a fixed point reached by accident and not reproducibly.
    while True:
        names = sorted({n for v in toasty.values() for n in v})
        wrapped = scan(re.compile(r"call\s+(?:\w+:)?(?:%s)\b"
                                  % "|".join(re.escape(n) for n in names)))
        grew = False
        for rel, v in wrapped.items():
            before = len(toasty.get(rel, ()))
            toasty.setdefault(rel, set()).update(v)
            grew = grew or len(toasty[rel]) != before
        if not grew:
            break

    # 3. every db string such a procedure loads, directly or through a table
    reach = {}
    for rel, procnames in toasty.items():
        for nm, a, b in pindex[rel]:
            if nm not in procnames:
                continue
            for line in text[rel][a:b]:
                m = LOAD.search(line.split(";")[0].rstrip())
                if not m:
                    continue
                lbl = m.group(1)
                if lbl in strings:
                    reach[lbl] = strings[lbl]
                elif lbl in tables:
                    for e in tables[lbl][1]:
                        if e in strings:
                            reach[e] = strings[e]

    over = {k: v for k, v in reach.items() if len(v[1]) > MAX}
    bad = []
    for lbl, (rel, txt) in sorted(over.items()):
        if lbl not in reg:
            bad.append("%s (%s) is %d characters and TOAST_MAX is %d - it "
                       "reaches the bar as %r. Shorten it, or - if it is not "
                       "a toast at all - register it in tests/toastlong.txt "
                       "with the routine that draws it"
                       % (lbl, rel, len(txt), MAX, txt[:MAX]))
        elif reg[lbl] != rel:
            bad.append("%s is registered against %s and is defined in %s"
                       % (lbl, reg[lbl], rel))
    stale = [k for k in reg if k not in over]
    for k in sorted(stale):
        bad.append("%s is in tests/toastlong.txt and is no longer over the "
                   "cap - take the line out. THE LIST IS A RATCHET and only "
                   "goes down" % k)

    if bad:
        for b in bad:
            print("t_toast:   %s" % b)
        fail("%d problem(s)" % len(bad))

    print("t_toast: TOAST_MAX %d; %d procedure(s) toast, %d fixed string(s) "
          "reachable, %d registered as not-a-toast"
          % (MAX, sum(len(v) for v in toasty.values()), len(reach), len(reg)))
    print("t_toast: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
