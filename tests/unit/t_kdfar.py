#!/usr/bin/env python3
"""kern_dos calls the kernel's disk layer; near or far has to MATCH the body.

    python3 tests/unit/t_kdfar.py

`kerndos/` (docs/plans/KERN-DOS-PLAN.md) assembles the kernel's own
`disk.inc`, `diskw.inc` and `dskwin.inc` into a root that is not the kernel,
and reaches into them by hand: `kerndos/kdback.inc` is the DOS box's second
back end and `kerndos/kdgate.inc` / `kerndos/kdosgate.inc` are the two test
entries.  Those call sites are the only hand-written ones in the tree.

**THE DISK LAYER IS NOT ONE CALLING CONVENTION.**  Most of it is near, and a
handful of routines end in `retf` because in the kernel they are reached from
another segment (SPEC.md 2.6.1) - `.cold` and `.text` are two segments there
and one here.  Nothing in the NAME says which: `dskw_read_x` is near and
`dsk_find_x` is far, same suffix, same layer, same file naming scheme.

What a mismatch does is the reason this file exists.  A near `call` to a
`retf` body pops the return address AND two bytes of whatever was under it, so
control resumes at a plausible-looking address with the stack two bytes light
and **no fault of any kind**: the machine carries on somewhere in the arena
with nothing on the screen.  `dos_k_path` presented exactly that way - the
guest printed its arena line, ran `dos_build_psp` to completion, and stopped
in the middle of the heap twenty steps later.  A far `call` to a `ret` body is
the mirror image and just as quiet.

FOUR of the sixteen targets `kdback.inc` names are far and ALL FOUR were
written near.  Only one of them was on a path wave 4 reached, so three would
have waited for the first DOS program to call AH=4Eh, AH=36h or ask what kind
of drive it is standing on - which is to say, for a bug report.  That is what
makes this worth a gate rather than a careful reading.

THE LIST MAINTAINS ITSELF (`tests/unit/t_mirror.py`'s argument): nothing here
enumerates the routines.  It reads every proc in `kernel/` and decides near or
far from the BODY, then reads every transfer in `kerndos/` and checks the
spelling against it.  A routine that changes flavour, or a call site added
next year, is covered with nobody remembering to add it.

WHAT IT CHECKS, in both directions:

  1  `call` and tail `jmp`/`jc`/... from `kerndos/` into a proc defined in
     `kernel/`.  A segment-qualified transfer (`COLD_SEG:`, `KERNEL_SEG:`)
     must land on a `retf` body; a bare one on a `ret` body.  The tail
     transfer is judged against the flavour of the proc it sits IN, which is
     what a tail jump inherits.
  2  the other way: `kernel/` calling a stub `kerndos/kdshim.inc` supplies.
     Nine of those are reached as `call KERNEL_SEG:x` from inside the disk
     layer and so must end `retf`; the rest are near.  That direction has
     already cost a wave - four of the shim's stubs shipped with `ret`.

Targets defined in `apps/dos/` are deliberately NOT checked: the DOS core's
own conventions are the package's business, and one of them is a `retf` that
is not a return at all (`dos_prog_enter` jumps INTO the program with it).

VERIFIED TO FAIL: changing `call COLD_SEG:dsk_path_x` in kdback.inc back to a
near `call` takes rule 1 red naming that one line; moving `kerndos/kdshim.inc`'s
`fpg_begin` from the far group to the near one takes rule 2 red naming all nine
call sites in five kernel files.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
KERNEL = os.path.join(ROOT, "kernel")
KERNDOS = os.path.join(ROOT, "kerndos")

LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):(.*)$")
# A transfer out of the proc: `call`, or a tail branch, optionally through a
# segment. NASM spells the far form `call SEG:name`, and SEG is always one of
# the kernel's three segment constants here.
XFER = re.compile(r"\b(call|jmp|jz|jnz|jc|jnc|je|jne)\s+"
                  r"(?:near\s+|short\s+)?"
                  r"(?:(KERNEL_SEG|COLD_SEG|OVL_SEG|OVLW_SEG)\s*:\s*)?"
                  r"([A-Za-z_][A-Za-z0-9_]*)\b")
RET = re.compile(r"(?:^|\s)(retf|ret)(?:\s|$)")


def fail(msg):
    print("t_kdfar: FAIL: %s" % msg)
    sys.exit(1)


def sources(d):
    return [os.path.join(d, f) for f in sorted(os.listdir(d))
            if f.endswith((".inc", ".asm"))]


def procs(paths):
    """[(relpath, proc, [(line, code)])], in file order."""
    out = []
    for p in paths:
        cur = None
        rel = os.path.relpath(p, ROOT).replace("\\", "/")
        for i, ln in enumerate(open(p, errors="replace"), 1):
            code = ln.split(";")[0].rstrip()
            m = LABEL.match(code)
            if m:
                cur = (rel, m.group(1), [])
                out.append(cur)
                code = m.group(2)
            if cur is not None and code.strip():
                cur[2].append((i, code))
    return out


def flavour(plist):
    """proc -> 'far' | 'near' | 'both' | None. A body with no `ret` at all
    falls THROUGH to the next label and inherits its flavour, which is how
    kdback.inc's six refusals share one `stc`/`ret`."""
    kind, order = {}, []
    for _f, name, lines in plist:
        found = []
        for _i, c in lines:
            m = RET.search(c)
            if m and m.group(1) not in found:
                found.append(m.group(1))
        order.append(name)
        if len(found) > 1:
            kind[name] = "both"
        elif found:
            kind[name] = "far" if found[0] == "retf" else "near"
        else:
            kind[name] = None
    for i in range(len(order) - 1, -1, -1):
        if kind[order[i]] is None and i + 1 < len(order):
            kind[order[i]] = kind[order[i + 1]]
    return kind


def main():
    kp = procs(sources(KERNEL))
    kdp = procs(sources(KERNDOS))
    kkind = flavour(kp)
    kdkind = flavour(kdp)
    kernel_procs = {n for _f, n, _l in kp}
    shim = {n for _f, n, _l in kdp if _f.endswith("kdshim.inc")}

    # --- rule 1: kerndos -> the kernel's disk layer -------------------------
    bad, checked = [], 0
    for f, name, lines in kdp:
        me = kdkind.get(name)
        for i, c in lines:
            for op, seg, tgt in XFER.findall(c):
                if tgt not in kernel_procs or tgt in shim:
                    continue
                k = kkind.get(tgt)
                if k in (None, "both"):
                    continue
                if op == "call":
                    want = "far" if seg else "near"
                elif me in ("far", "near"):
                    # a tail transfer returns with the flavour of the proc it
                    # is in, so a bare one has to match that and a qualified
                    # one is a far CALL that happens to be spelled `jmp` only
                    # when the body it lands on is far too
                    want = "far" if seg else me
                else:
                    continue
                checked += 1
                if want != k:
                    bad.append("%s:%d  `%s %s%s` - that body ends in `%s`, so "
                               "this must be %s"
                               % (f, i, op, (seg + ":") if seg else "", tgt,
                                  "retf" if k == "far" else "ret",
                                  "`call %s:%s`" % ("COLD_SEG", tgt) if k == "far"
                                  else "near"))
    if bad:
        fail("kerndos calls the kernel's disk layer with the wrong flavour, "
             "which faults nothing and runs on into the heap:\n  "
             + "\n  ".join(bad))
    print("t_kdfar: 1/2 %d transfers from kerndos/ into kernel/ agree with "
          "the body they land on" % checked)

    # --- rule 2: the kernel calling one of the shim's stubs -----------------
    bad, checked = [], 0
    for f, name, lines in kp:
        for i, c in lines:
            for op, seg, tgt in XFER.findall(c):
                if tgt not in shim or op != "call":
                    continue
                want = "far" if seg else "near"
                checked += 1
                if kdkind.get(tgt) != want:
                    bad.append("%s:%d  `call %s%s` - kerndos/kdshim.inc's stub "
                               "ends in `%s` and this needs `%s`"
                               % (f, i, (seg + ":") if seg else "", tgt,
                                  "retf" if kdkind.get(tgt) == "far" else "ret",
                                  "retf" if seg else "ret"))
    if bad:
        fail("the kernel's own code calls a kerndos shim stub with the wrong "
             "flavour:\n  " + "\n  ".join(bad))
    print("t_kdfar: 2/2 %d calls from kernel/ into kdshim.inc's stubs agree"
          % checked)
    print("t_kdfar: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
