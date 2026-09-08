#!/usr/bin/env python3
"""A driver's SERVICE TASK may not reach a claim door.

    python3 tests/unit/t_drvclaim.py

WHY THIS IS A ROW, and it is not the reason it was first asked for.
docs/plans/HEAP-UNPIN-PLAN.md 12 question 4 asks whether any driver verb can
claim from a `TF_SERVICE` task, on the worry that piece C's `[wm_pkgd]`
predicate would then be reasoning about the wrong stack. That worry is
UNFOUNDED: `[wm_pkgd]` is a global count of kernel->package far calls in
flight, with one writer (the UI task, at `wm_pkgcall` and the loader's entry
call), so it answers "is any callback live" and not "what is under SP".

What a claim from a service task really costs is the STACK. `mem_claim` can
reach `mem_compact`, which far-calls a holder's relocation proc - a PACKAGE's,
through `PKG_DISP` - on whatever stack it was entered on. SHEET's `sh_reloc`
walks a table; the C SDK's `cc_ovbind` is a proc of its own. On `STK0` that is
fine. On a 384-byte worker slice, under docs/plans/completed/STACK-SLOTS-PLAN.md's
interrupt floor, it is a slice one nested proc from its canary - and the
failure would land in whatever ran next, not here.

SPEC.md 20.6 rule 7 already forbids a PACKAGE's worker from touching a file or
claiming. A driver has no such rule and needs none if nothing does it, so this
row is that fact made checkable: today `SOUND.DRV` is the only driver in the
tree that calls `OSAPI_DRV_TASK` at all (kernel/sched.inc says so in as many
words), its two tasks are `sbl_refill_task` and `sbl_drain_task`, and neither
cone reaches a claim - every claim in that driver is in `sbl_attach` (mount)
or `sbl_grant_alloc` under `sbl_v_grant` (a stream verb, on the CALLER's
task). The second driver to spawn a task is where this stops being obvious.

HOW IT READS THE SPAWN. `OSAPI_DRV_TASK` takes the entry in AX, and AX = 0
means "this worker is exiting" (SPEC.md 51.7) - so the row takes the last
`mov ax, <label>` before each call and ignores the literal-zero form. A spawn
whose entry is computed rather than named would be missed, and would fail this
row's own accounting check instead: every `call OSAPI_DRV_TASK` must resolve
to a label or to the exit form.

WHAT IT DOES NOT SEE. It walks `call`/`jmp` by NAME over the driver's own
sources. An indirect dispatch (`call [bx]`) inside a service task's cone is
invisible to it, and so is a claim made by a kernel routine the driver calls
- neither exists today, and both would be a bigger change than this row.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# the doors that can reach mem_compact. OSAPI_MEM_FREE cannot: it never moves
# anything. OSAPI_MEM_REGROW can - mem_regrow path 3 relocates (SPEC.md 66.9).
CLAIMS = ("OSAPI_MEM_CLAIM", "OSAPI_MEM_CLAIM_HI", "OSAPI_MEM_CLAIM_DMA",
          "OSAPI_MEM_CLAIM_DMA_HI", "OSAPI_MEM_REGROW")

LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")
LOCAL = re.compile(r"^(\.[A-Za-z0-9_]+):")
XFER = re.compile(r"^\s*(?:call|jmp)\s+(?:near\s+|short\s+)?"
                  r"([.A-Za-z_][A-Za-z0-9_.]*)\s*(?:;.*)?$")
MOVAX = re.compile(r"^\s*mov\s+ax\s*,\s*([A-Za-z_][A-Za-z0-9_.]*)\s*(?:;.*)?$")


def sources(top):
    """`top` and every .inc it %includes, in include order."""
    out, seen = [], set()

    def walk(path):
        path = os.path.normpath(path)
        if path in seen or not os.path.isfile(path):
            return
        seen.add(path)
        out.append(path)
        base = os.path.dirname(path)
        for line in open(path, encoding="latin1"):
            m = re.match(r'\s*%include\s+"([^"]+)"', line)
            if m:
                for cand in (os.path.join(base, m.group(1)),
                             os.path.join(ROOT, "drivers", m.group(1)),
                             os.path.join(ROOT, m.group(1))):
                    if os.path.isfile(cand):
                        walk(cand)
                        break
    walk(top)
    return out


def bodies(paths):
    """-> {global label: [line, ...]}, local labels folded into their owner."""
    out, cur = {}, None
    for p in paths:
        for line in open(p, encoding="latin1"):
            m = LABEL.match(line)
            if m:
                cur = m.group(1)
                out.setdefault(cur, [])
                continue
            if cur is not None:
                out[cur].append(line)
    return out


def targets(lines, owner):
    """Every named call/jmp in a body, locals resolved to their owner."""
    out = set()
    for line in lines:
        m = XFER.match(line)
        if m:
            t = m.group(1)
            out.add(owner if t.startswith(".") else t)
    return out


def check(drv, top):
    paths = sources(top)
    body = bodies(paths)

    # --- the spawn sites --------------------------------------------------
    entries, unresolved = set(), 0
    for name, lines in body.items():
        last = None
        for line in lines:
            m = MOVAX.match(line)
            if m:
                last = m.group(1)
            if re.match(r"^\s*(xor|sub)\s+ax\s*,\s*ax\b", line):
                last = None                     # AX = 0: the exit form
            if re.match(r"^\s*mov\s+ax\s*,\s*0\s*(?:;.*)?$", line):
                last = None
            if "call OSAPI_DRV_TASK" in line:
                if last is None:
                    continue                    # the documented exit call
                if last not in body:
                    unresolved += 1
                    print("FAIL %-12s spawns a task whose entry `%s` is not a "
                          "label in its own sources" % (drv, last))
                else:
                    entries.add(last)
                last = None
    if not entries:
        return 0, 0, unresolved

    # --- the cone ---------------------------------------------------------
    bad = 0
    for e in sorted(entries):
        seen, work, path = set(), [(e, [e])], {}
        while work:
            n, how = work.pop()
            if n in seen or n not in body:
                continue
            seen.add(n)
            path[n] = how
            for c in CLAIMS:
                if any(re.search(r"\bcall\s+%s\b" % c, l) for l in body[n]):
                    bad += 1
                    print("FAIL %-12s service task %s reaches %s via %s"
                          % (drv, e, c, " -> ".join(how)))
            for t in targets(body[n], n):
                if t not in seen:
                    work.append((t, how + [t]))
    return len(entries), bad, unresolved


def main():
    tops = sorted(p for p in
                  (os.path.join(ROOT, "drivers", d, f)
                   for d in os.listdir(os.path.join(ROOT, "drivers"))
                   if os.path.isdir(os.path.join(ROOT, "drivers", d))
                   for f in os.listdir(os.path.join(ROOT, "drivers", d))
                   if f.endswith(".asm"))
                  if os.path.isfile(p))
    spawners, tasks, bad, unres = 0, 0, 0, 0
    for top in tops:
        drv = os.path.basename(os.path.dirname(top))
        n, b, u = check(drv, top)
        if n:
            spawners += 1
            tasks += n
        bad += b
        unres += u
    if bad or unres:
        print("t_drvclaim: a driver service task may not claim - it runs on a "
              "worker SLICE, and mem_compact far-calls a holder's relocation "
              "proc on the stack it was entered on")
        return 1
    print("t_drvclaim: %d driver(s) spawn %d service task(s); no cone reaches "
          "a claim door" % (spawners, tasks))
    return 0


if __name__ == "__main__":
    sys.exit(main())
