#!/usr/bin/env python3
"""Every `ret` must be reached with the stack exactly where it started.

WHY A COUNT IS NOT ENOUGH, and why this walks paths instead. A routine with two
exits repeats its pops on each, so comparing total pushes with total pops flags
409 of 4130 routines in this tree - one in ten, all of them fine. A gate wrong
that often is worse than no gate, because it gets ignored and then it is worse
than nothing when it is finally right.

So this builds a control-flow graph per routine and propagates stack depth
through it. A finding is one of three things, and each is a real bug:

  * a `ret` reached at non-zero depth   - returns to a saved register
  * a label reached at two depths       - one path pushed and another did not
  * a `jmp` to another routine at depth - a tail call carrying rubbish

The first is what hung Sheet's chart legend: `ch_legend` pushed SI, never
popped it, and its `ret` jumped to whatever SI held. No crash, no message - a
black canvas and a wedged app.

DELIBERATE STACK SURGERY is real and rare, and there are two kinds.

A routine that BANKS something on the caller's stack and a partner that takes
it off again - Sheet's sh_vpush/sh_binop_pre pair, os88fp's fp_push_a/fp_pop_a.
Mark those `; STKBALANCE-NET: +4` (or -4), and the declared delta is applied at
every `call` to them, so their CALLERS come out balanced instead of inheriting
a phantom leak. That is the difference between a tool that finds two real bugs
and one that reports six routines because two of them are unusual.

A loop that pushes N things and a second loop that pops them - every itoa in
this tree. The count lives in a register, so no static walk can pair them;
conflicts arriving on a BACK EDGE are therefore not reported, and the count of
suppressed ones is printed so the suppression is visible rather than silent.

A routine with NO `ret` of its own - every exit a tail jmp to a shared core -
is not walked at all: its depths cannot be checked without following the jmp
into the other routine. The count of such chunks that move SP is printed for
the back-edge count's reason, so the gap is visible rather than silent.

`; STKBALANCE-OK: <reason>` skips a routine outright. The reason is the point -
an unexplained exemption is how a gate stops meaning anything.

  python3 tools/stkbalance.py [file ...]     default: apps/ and kernel/
"""

import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CALL = re.compile(r"^call\s+(?:near\s+|far\s+|word\s+)?(\S+)", re.I)
NET = re.compile(r"STKBALANCE-NET:\s*([+-]?\d+)")
PUSH = re.compile(r"^(push|pusha|pushf)\b", re.I)
POP = re.compile(r"^(pop|popa|popf)\b", re.I)
RET = re.compile(r"^(ret|retn|retf|iret)\b", re.I)
JMP = re.compile(r"^jmp\s+(?:short\s+|near\s+|word\s+)?(\S+)", re.I)
JCC = re.compile(r"^(j[a-z]{1,3}|loop|loope|loopne|loopz|loopnz)\s+"
                 r"(?:short\s+)?(\S+)$", re.I)
SPADD = re.compile(r"^(add|sub)\s+sp\s*,\s*(\S+)$", re.I)
GLOBAL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")
LOCAL = re.compile(r"^(\.[A-Za-z0-9_]+):")


def strip(line):
    """Drop the comment, keeping quoted semicolons intact."""
    out, q = [], None
    for ch in line:
        if q:
            out.append(ch)
            if ch == q:
                q = None
            continue
        if ch in "'\"":
            q = ch
            out.append(ch)
            continue
        if ch == ";":
            break
        out.append(ch)
    return "".join(out).strip()


def routines(path):
    """([(name, [(idx, text, raw)], {label: idx})], skipped) - one entry per
    global label whose body reaches a `ret`. A chunk with no ret at all - every
    exit a tail jmp to a shared core - is NOT walked, and `skipped` counts the
    ones that move SP anyway, so the gap is printed rather than silent."""
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.read().split("\n")
    out, cur, body, labels, skipped = [], None, [], {}, 0

    def flush():
        nonlocal skipped
        if not cur:
            return
        if any(RET.match(t) for _, t, _ in body):
            out.append((cur, body, labels))
        elif any(delta(t) is not None for _, t, _ in body):
            skipped += 1

    for raw in lines:
        text = strip(raw)
        m = GLOBAL.match(text)
        if m and not text.startswith("."):
            flush()
            cur, body, labels = m.group(1), [], {}
            rest = text[m.end():].strip()
            if rest:
                body.append((len(body), rest, raw))
            continue
        if cur is None:
            continue
        m = LOCAL.match(text)
        if m:
            labels[m.group(1)] = len(body)
            rest = text[m.end():].strip()
            if rest:
                body.append((len(body), rest, raw))
            continue
        # a comment-only line is kept, with empty text: the STKBALANCE markers
        # live in comments, and dropping the line drops the marker with it
        body.append((len(body), text, raw))
    flush()
    return out, skipped


def delta(text):
    """How this instruction moves SP, in words. None = not a stack move."""
    if PUSH.match(text):
        return 8 if text.lower().startswith("pusha") else 1
    if POP.match(text):
        return -8 if text.lower().startswith("popa") else -1
    m = SPADD.match(text)
    if m:
        try:
            n = int(m.group(2), 0)
        except ValueError:
            return None
        return -(n // 2) if m.group(1).lower() == "add" else (n // 2)
    return None


def declared_nets(files):
    """{routine: net stack delta} from every ; STKBALANCE-NET: marker."""
    nets = {}
    for path in files:
        for name, body, _ in routines(path)[0]:
            for _, _, raw in body:
                m = NET.search(raw)
                if m:
                    nets[name] = int(m.group(1))
                    break
    return nets


def check(path, nets):
    findings = []
    suppressed = 0
    chunks, skipped = routines(path)
    for name, body, labels in chunks:
        if any("STKBALANCE-OK" in raw for _, _, raw in body):
            continue
        if name in nets:
            continue          # it DECLARED its net: that is the exemption
        n = len(body)
        depth = {0: 0}
        work = [0]
        seen_conflict = False
        while work:
            i = work.pop()
            if i >= n:
                continue
            d = depth[i]
            _, text, raw = body[i]
            if RET.match(text):
                if d != 0:
                    findings.append((path, name, raw.strip(),
                                     "ret at depth %+d" % d))
                continue
            m = JMP.match(text)
            if m:
                tgt = m.group(1)
                if tgt in labels:
                    nxt = [labels[tgt]]
                elif tgt.startswith("."):
                    nxt = []                       # a label we could not see
                else:
                    if d != 0:
                        findings.append((path, name, raw.strip(),
                                         "tail jmp to %s at depth %+d" % (tgt, d)))
                    nxt = []
                for j in nxt:
                    if j in depth and depth[j] != d:
                        if j <= i:
                            suppressed += 1
                        elif not seen_conflict:
                            findings.append((path, name, raw.strip(),
                                             "%s reached at depth %+d and %+d"
                                             % (tgt, depth[j], d)))
                            seen_conflict = True
                    elif j not in depth:
                        depth[j] = d
                        work.append(j)
                continue
            nxts = [i + 1]
            m = JCC.match(text)
            if m:
                tgt = m.group(2)
                if tgt in labels:
                    nxts.append(labels[tgt])
            dd = delta(text)
            if dd is None:
                m2 = CALL.match(text)
                if m2 and m2.group(1) in nets:
                    dd = nets[m2.group(1)]     # a declared banking routine
            nd = d + dd if dd is not None else d
            for k, j in enumerate(nxts):
                jd = nd if k == 0 else d + (dd or 0)
                if j in depth:
                    if depth[j] != jd and not seen_conflict:
                        if j <= i:
                            suppressed += 1     # a loop whose body moves SP
                        else:
                            findings.append((path, name,
                                             body[min(j, n - 1)][2].strip(),
                                             "reached at depth %+d and %+d"
                                             % (depth[j], jd)))
                            seen_conflict = True
                else:
                    depth[j] = jd
                    work.append(j)
    return findings, suppressed, skipped


def main():
    args = sys.argv[1:]
    if args:
        files = args
    else:
        files = sorted(glob.glob(os.path.join(ROOT, "apps", "*.inc")) +
                       glob.glob(os.path.join(ROOT, "apps", "*", "*.asm")) +
                       glob.glob(os.path.join(ROOT, "kernel", "*.inc")))
    nets = declared_nets(files)
    all_f, sup, skp = [], 0, 0
    for f in files:
        got, n, k = check(f, nets)
        all_f += got
        sup += n
        skp += k
    for path, name, line, why in all_f:
        print("%s: %s: %s\n    %s" % (os.path.relpath(path, ROOT), name, why, line))
    print("stkbalance: %d routine(s) with an unbalanced path"
          "  (%d declared banking routines, %d loop back-edges not reported,"
          " %d no-ret chunks not walked)"
          % (len(all_f), len(nets), sup, skp))
    return 1 if all_f else 0


if __name__ == "__main__":
    sys.exit(main())
