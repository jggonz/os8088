#!/usr/bin/env python3
"""The INT 21h core reaches the file system through the DOORS and nothing else.

    python3 tests/unit/t_dosseam.py

SPEC.md 96.4.1 states the discipline in one line - *no INT 21h handler may
call an OSAPI_* file slot directly* - and apps/dos/dos.asm repeats it above
the door block.  It exists for a reason that has nothing to do with
docs/plans/KERN-DOS-PLAN.md: a kernel file slot must run on the UI task's
stack, because dsk_secbuf is .lowbss and reached through SS (96.4.1.1).

**THE PLAN RESTS ON IT BEING TRUE, WHICH IS A DIFFERENT CLAIM.**  KERN-DOS-PLAN
4 gives a DOS program the whole machine by assembling the DOS core against a
second, kernel-less back end; its 3 says the port is *"a second implementation
of twenty doors plus dos_load, and nothing above them changes"*.  Every
file-system call that is NOT behind a door is a straggler that port has to
find - and the way such a call gets written is not carelessness, it is that
the slot in question does no disk I/O, so the RULE'S OWN STATED REASON does
not bind and it looks fine.  All three this row found were exactly that
(96.4.2), and all three were reachable from dos_int21.

WHAT IT CHECKS.  Two rules, and the first is a hard zero rather than a ratchet
because the count IS zero and three is a list somebody can fix rather than a
population somebody has to live with:

  1. **No file-system slot is reachable from an interrupt entry except
     through a door.**  Zero, no registry, no exceptions.
  2. **Every OTHER OSAPI_* the core can reach is registered** in
     tests/dosseam.txt with a reason - the port's whole surface, so it cannot
     grow silently.  t_textrules' ratchet, one subject along.

IT WALKS THE CALL GRAPH AND DOES NOT TRUST A NAME.  A prefix rule - "a proc
called dos_k_* is a door, dos_paint_* is window" - was tried first and got two
of the three wrong: dos_drv_count and dos_drv_sel LOOK like the window's
drive list and are called straight from dos_int21 (AH=0Eh/19h).  So the
roots are the interrupt entries and dos_load, the walk follows call and jmp,
and it STOPS at the doors, everything past dos_be_go being the far side by
design.

VERIFIED TO FAIL: putting `call OSAPI_FILE_HERE` back into dos_walk_at takes
rule 1 red naming the file, the line and the path from dos_int21; adding a
`call OSAPI_TASK_YIELD` to dos_fh_enter takes rule 2 red.
"""
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                           # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
DOS = os.path.join(ROOT, "apps", "dos")
REGISTRY = os.path.join(ROOT, "tests", "dosseam.txt")

# The interrupt entries a DOS program can reach us through, plus the one
# non-interrupt path KERN-DOS-PLAN 3 names: reading the program image.
ROOTS = ("dos_int21", "dos_int2f", "dos_int33", "dos_int20", "dos_int22",
         "dos_int24", "dos_load")

# ...AND EVERY ROUTINE A HOST HOOK IS BOUND TO (SPEC.md 96.44.3).  A hook is a
# word in `dos_hkv` the core calls where a `%ifndef KD_BACKEND` used to stand,
# so the walk cannot follow it - `call word [dos_hkv + DHK_TTY]` reaches
# wherever this host's `dos_hk_bind` put it.  The BINDING is the edge, and it
# is right there in the source: every `mov word [dos_hkv + DHK_x], <name>` is
# a root as surely as an interrupt entry, and reading them keeps this row's
# picture of the port's surface honest.  Without it `OSAPI_MOUSE` read as
# registered with zero call sites the day INT 33h's read became a hook.
HOOKBIND = re.compile(r"mov\s+word\s+\[dos_hkv\s*\+\s*DHK_\w+\]\s*,"
                      r"\s*([A-Za-z_]\w*)")

LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")
# `jc`/`jnc`/`jz`/... are in here beside call and jmp because a tail branch to
# another proc is a reach like any other, and this file uses them that way.
XFER = re.compile(r"\b(?:call|jmp|jz|jnz|jc|jnc|je|jne)\s+"
                  r"(?:near\s+|short\s+)?([A-Za-z_][A-Za-z0-9_]*)\b")
OSAPI = re.compile(r"\bcall\s+(OSAPI_[A-Z0-9_]+)\b")
FS = re.compile(r"^OSAPI_(?:FILE|VOL|FIND)_")
INDIRECT = re.compile(r"\bcall\s+(?:word\s+)?\[")


def sources():
    return [os.path.join(DOS, f) for f in sorted(os.listdir(DOS))
            if f.endswith((".asm", ".inc"))]


def read():
    """proc -> [(path, line, code)], every line of its body."""
    body = collections.defaultdict(list)
    for p in sources():
        proc = None
        rel = os.path.relpath(p, ROOT).replace("\\", "/")
        for i, ln in enumerate(open(p, errors="replace"), 1):
            t = ln.lstrip()
            m = LABEL.match(t)
            if m:
                proc = m.group(1)
            if proc and not t.startswith(";"):
                body[proc].append((rel, i, ln.split(";")[0]))
    return body


def hooks(body):
    """Every routine this host binds into `dos_hkv` (SPEC.md 96.44.3)."""
    out = []
    for rows in body.values():
        for _f, _i, c in rows:
            m = HOOKBIND.search(c)
            if m:
                out.append(m.group(1))
    return out


def reach(body):
    """Every proc the roots can reach, and the path to each."""
    stop = {"dos_be_go"} | {p for p in body
                            if p.startswith(("dos_be_", "dos_k_"))}
    roots = list(ROOTS) + hooks(body)
    prev = {r: None for r in roots}
    seen, queue = set(), list(roots)
    while queue:
        p = queue.pop(0)
        if p in seen or p in stop or p not in body:
            continue
        seen.add(p)
        for _f, _i, c in body[p]:
            for t in XFER.findall(c):
                if t in body and t not in seen and t not in prev:
                    prev[t] = p
                    queue.append(t)
    return seen, prev


def path_to(prev, proc):
    out, n = [], proc
    while n is not None:
        out.append(n)
        n = prev.get(n)
    return " <- ".join(out)


def registry():
    out = {}
    with open(REGISTRY) as fh:
        for ln in fh:
            ln = ln.split("#")[0].strip()
            if not ln:
                continue
            slot, _, why = ln.partition(" ")
            out[slot.strip()] = why.strip()
    return out


def main():
    body = read()
    for r in ROOTS:
        check(r in body, "dosseam: %s is a proc in apps/dos/" % r,
              why="a root that does not exist is silently walked over, and "
                  "the whole scan then reports a clean seam it never looked "
                  "at. This is the check that makes the rest mean anything")
    seen, prev = reach(body)

    fs, other, indirect = [], collections.Counter(), []
    for p in sorted(seen):
        for f, i, c in body[p]:
            m = OSAPI.search(c)
            if m:
                if FS.match(m.group(1)):
                    fs.append((f, i, p, m.group(1)))
                else:
                    other.update([m.group(1)])
            if INDIRECT.search(c) and "dos_hkv" not in c:
                indirect.append((f, i, p, c.strip()))

    # --- rule 1: a hard zero ------------------------------------------------
    for f, i, p, slot in fs:
        check(False, "dosseam: %s calls %s outside a door" % (p, slot),
              why="SPEC.md 96.4.1: no INT 21h handler may call an OSAPI_* "
                  "file slot directly. Add a DBE_* door and a dos_k_* target "
                  "beside the other %d (96.4.2) - it is about nine bytes, and "
                  "it is what keeps docs/plans/KERN-DOS-PLAN.md 3's port a "
                  "list rather than a hunt. A slot that does no disk I/O is "
                  "not an exception: the rule's stated reason is the stack "
                  "swap, and the PLAN's reason is the seam"
                  % len([1 for k in body if k.startswith("dos_be_")]),
              got="%s:%d, reachable as %s" % (f, i, path_to(prev, p)),
              want="call dos_be_<verb>")
    check(not fs, "dosseam: no file-system slot is reachable outside a door",
          got="%d call site(s)" % len(fs), want=0)

    # --- rule 2: the rest of the surface is registered -----------------------
    want = registry()
    for slot in sorted(set(other) | set(want)):
        if slot not in want:
            check(False, "dosseam: %s is not registered" % slot,
                  why="every OSAPI_* the INT 21h core can reach is a slot "
                      "kern_dos has to answer or refuse, and that surface is "
                      "the plan's whole cost. Add a line to tests/dosseam.txt "
                      "saying what kern_dos will do about it, so the decision "
                      "is in the diff rather than in somebody's head",
                  got="%d call site(s), not in the registry" % other[slot],
                  want="a registry line carrying a reason")
        elif slot not in other:
            check(False, "dosseam: %s is registered and no longer reached"
                  % slot,
                  why="the registry is the port's surface and a stale line "
                      "makes it look bigger than it is",
                  got="registered, 0 call sites", want="drop the line")

    # --- and the one thing that would make the walk a fiction ----------------
    check(not indirect,
          "dosseam: no indirect call in the reachable set",
          why="`call [x]` reaches wherever the word says, so the walk above "
              "cannot follow it and a file slot behind one would be invisible "
              "to this row. dos_be_go's own `call word [dos_betgt]` is inside "
              "a door and so is not in the set",
          got=["%s:%d %s" % (f, i, c) for f, i, _p, c in indirect[:6]],
          want="none")

    print("dosseam: %d procs reachable from %s; %d door(s); surface %s"
          % (len(seen), "/".join(ROOTS),
             len([1 for k in body if k.startswith("dos_be_")]),
             ", ".join(sorted(other)) or "(nothing)"))
    return done("dosseam")


if __name__ == "__main__":
    sys.exit(main())
