#!/usr/bin/env python3
"""Can anything holding a movable claim's segment reach a CLAIM? (SPEC.md 66.5.10.2)

    python3 tools/dsegaudit.py            # [dsk_dseg], the donated listing claim
    python3 tools/dsegaudit.py --word dsk_fatseg

**THIS IS THE AUDIT, AND THE AUDIT IS THE WORK.** Declaring a claim movable is
three lines; what makes it safe is the answer to one question, and the question
is not "does the proc fix every naming word" - `mem_movable` cannot be given a
wrong proc without somebody noticing. It is this:

    is there a path that loads this block's segment into ES or DS and then
    calls something that can CLAIM?

Because `mem_claim` COMPACTS on its refusal path (SPEC.md 50.6.2, 66.9). A
routine holding the segment across such a call is holding a stale one the
moment the block moves, and it then reads or writes whatever took its place.
Nothing catches that: the walk succeeds, the bytes are plausible, and the
failure is a listing with another claim's contents in it.

It is a STATIC walk of the kernel's call graph and it is deliberately
pessimistic - a `jmp` counts as a call, and a routine reaching any of the
claim entry points at any depth is reported. Two things it cannot see, and
both are named in the output rather than assumed away:

  * a far call THROUGH a driver (`drv_fs_call`) or a package, which is a graph
    edge into an image this does not parse;
  * a segment stashed in a variable and reloaded later, rather than held in a
    register.

The first is why SPEC.md 66.5.10.2 says a `DVK_FILE` driver donating a listing
claim owes this audit its own answer: the `.fsicons` window calls `drv_fs_call`
and is clean today only because that pass never runs for the volume kind this
claim belongs to.
"""
import argparse
import collections
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                     "..", "kernel"))
LABEL = re.compile(r"^([a-z_][a-z0-9_]*):")
CALL = re.compile(r"^\s+(?:call|jmp)\s+(?:short\s+)?(?:COLD_SEG:|KERNEL_SEG:)?"
                  r"(?!short\b)([a-z_][a-z0-9_]*)\b")   # `jmp short .x` is a
                  # LOCAL jump: without the lookahead the optional group
                  # backtracks and captures `short` as a routine name

# Everything whose refusal path can compact (SPEC.md 66.9). mem_claim is the
# door; the rest are it under other names, plus the compactor itself.
CLAIMERS = {"mem_claim", "mem_claim_x", "mem_claim_dma", "mem_claim_dma_x",
            "mem_claim_hi", "mem_claim_hi_x", "mem_claim_dma_hi",
            "mem_claim_dma_hi_x", "mem_compact", "mem_compact_x",
            "mem_cp_run", "mem_regrow", "mem_regrow_x",
            "osapi_mem_claim_x", "osapi_mem_claim_dma_x"}

# A far call this walk cannot follow. Named rather than skipped.
OPAQUE = {"drv_fs_call", "drv_call", "drv_cp_call_x", "wm_pkgcall",
          "mem_reloc_call", "inst_pkg_call"}


def graph_of(root):
    graph, bodies = collections.defaultdict(set), {}
    for fn in sorted(os.listdir(root)):
        if not fn.endswith((".inc", ".asm")):
            continue
        cur, buf = None, []
        for line in open(os.path.join(root, fn)):
            line = line.split(";")[0].rstrip("\n")
            m = LABEL.match(line)
            if m and not line.startswith("."):
                if cur:
                    bodies[cur] = buf
                cur, buf = m.group(1), []
            elif cur is not None:
                buf.append((fn, line))
                c = CALL.match(line)
                if c:
                    graph[cur].add(c.group(1))
        if cur:
            bodies[cur] = buf
    return graph, bodies


def reaches(graph, start, seen=None):
    """A path from `start` to something that claims, or None."""
    seen = seen if seen is not None else set()
    if start in seen:
        return None
    seen.add(start)
    base = start[:-2] if start.endswith("_x") else start
    if start in CLAIMERS or base in CLAIMERS:
        return [start]
    for callee in sorted(graph.get(start, ())):
        sub = reaches(graph, callee, seen)
        if sub:
            return [start] + sub
    return None


def windows(bodies, word):
    """Every stretch where `word` is live in ES or DS, with the calls in it."""
    load = re.compile(r"^\s+mov\s+(es|ds)\s*,\s*\[%s\]" % re.escape(word))
    other = re.compile(r"^\s+mov\s+(es|ds)\s*,\s*(?!\[%s\])" % re.escape(word))
    out = []
    for name, body in sorted(bodies.items()):
        reg, calls, fname = None, [], None
        for fn, line in body:
            m = load.match(line)
            if m:
                if reg is not None:
                    out.append((name, fname, reg, calls, False))
                reg, calls, fname = m.group(1), [], fn
                continue
            if reg is None:
                continue
            r = other.match(line)
            if r and r.group(1) == reg:
                out.append((name, fname, reg, calls, False))
                reg, calls = None, []
                continue
            c = CALL.match(line)
            if c:
                calls.append(c.group(1))
        if reg is not None:
            out.append((name, fname, reg, calls, True))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--word", default="dsk_dseg",
                    help="the kernel word holding the segment (default dsk_dseg)")
    a = ap.parse_args()

    graph, bodies = graph_of(ROOT)
    wins = windows(bodies, a.word)
    if not wins:
        print("dsegaudit: nothing loads [%s] into ES or DS - either the word "
              "is misspelled or this block is never held in a segment "
              "register, which is the answer" % a.word)
        return 0

    print("dsegaudit: %d window(s) where [%s] is live in a segment register\n"
          % (len(wins), a.word))
    bad, opaque = 0, 0
    for name, fn, reg, calls, still_open in wins:
        print("  %s:%s   %s = [%s]%s"
              % (fn, name, reg, a.word,
                 "   *** STILL LIVE at the end of the routine ***"
                 if still_open else ""))
        if not calls:
            print("       (no calls in the window - nothing can claim)")
        for c in sorted(set(calls)):
            p = reaches(graph, c)
            if p:
                bad += 1
                print("    !! %-22s -> %s" % (c, " -> ".join(p)))
            elif c in OPAQUE:
                opaque += 1
                print("     ? %-22s  a FAR call this walk cannot follow - "
                      "answer it by hand" % c)
            else:
                print("       %-22s  no claim reachable" % c)
        print()

    if bad:
        print("dsegaudit: FAIL - %d path(s) can claim, and a claim COMPACTS "
              "(SPEC.md 50.6.2). The segment held across one of these is stale "
              "the moment the block moves." % bad)
        return 1
    print("dsegaudit: ok - no path holding [%s] can reach a claim%s"
          % (a.word,
             "; %d far call(s) flagged above are outside this walk" % opaque
             if opaque else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
