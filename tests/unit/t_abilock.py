#!/usr/bin/env python3
"""The ABI lock holds (SPEC.md 20.2.0.1) - and the fingerprint sees what it must.

    python3 tests/unit/t_abilock.py

Two halves. The first is the gate itself: `tools/abi.lock.json` describes the
tree as it stands, so a change to the package or driver ABI cannot reach
`main` without somebody deciding, in writing, whether it breaks what was built
before it. The second proves the fingerprint is worth locking: it MOVES for a
moved cell, a re-contracted cell and a changed header byte, and it does NOT
move for SDK prose - a lock that fired on every sentence would be clicked
through, and one that missed a contract would be the old silence with a file
attached.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, done                          # noqa: E402
import abilock                                           # noqa: E402


def main():
    bad = abilock.check(abilock.load())
    check(not bad, "tools/abi.lock.json describes the tree",
          "PKG_FMT and DRV_VER are the bytes the loaders compare before they "
          "far-call a package or a driver, and they only protect anybody if "
          "they move when the surface does (SPEC.md 20.2.0.1)",
          got=" | ".join(bad))

    api = open(os.path.join(ROOT, "apps", "os88api.inc")).read()
    base = abilock.fingerprint("PKG_FMT", {"apps/os88api.inc": api})

    def moves(what, new, why, want=True):
        fp = abilock.fingerprint("PKG_FMT", {"apps/os88api.inc": new})
        check(new != api, "the mutation for %s applies" % what,
              "a mutation that matched nothing proves nothing")
        check((fp != base) == want,
              "the fingerprint %s for %s" % ("moves" if want else "stands",
                                             what), why)

    # A MOVED CELL - the pass-4 renumbering.
    moves("a cell at a new address",
          api.replace("KERNEL_SEG:0x0010", "KERNEL_SEG:0x0011", 1),
          "a package built before far-calls the old address")
    # A RE-CONTRACTED CELL - #214's OSAPI_FSX_SPK: same address, new meaning,
    # said in the indented comment under the %define.
    first = api.index("%define OSAPI_PKG_START")
    cont = api.index("\n                                 ;", first)
    moves("a contract changed under the same address",
          api[:cont + 1] + "                                 ; CHANGED\n"
          + api[cont + 1:],
          "the register contract is written there and nowhere else, and a "
          "contract that moves at the same address is what the address "
          "alone cannot see")
    # A HEADER BYTE - the OS88_HEADER macro's layout.
    moves("a header field",
          api.replace("dw 0                    ; +4  link base",
                      "dw 1                    ; +4  link base", 1),
          "the loader reads the header before it runs a byte")
    # PROSE - a banner at column 0. Must NOT move it.
    moves("a column-0 comment",
          api.replace("; --- an ITEM on the desktop",
                      "; --- AN ITEM on the desktop", 1),
          "SDK prose is not ABI; a lock that fires on every sentence is a "
          "lock nobody reads", want=False)
    # THE GUARDED NUMBER ITSELF - excluded, so the bump is the decision.
    moves("the PKG_FMT line",
          api.replace("PKG_FMT equ %d" % abilock.version("PKG_FMT"),
                      "PKG_FMT equ %d" % (abilock.version("PKG_FMT") + 1), 1),
          "the bump is checked against the lock's version, not folded into "
          "the fingerprint it decides about", want=False)

    drv = open(os.path.join(ROOT, "drivers", "os88drv.inc")).read()
    d0 = abilock.fingerprint("DRV_VER", {"apps/os88api.inc": api,
                                         "drivers/os88drv.inc": drv})
    d1 = abilock.fingerprint("DRV_VER", {
        "apps/os88api.inc": api.replace("KERNEL_SEG:0x0010",
                                        "KERNEL_SEG:0x0011", 1),
        "drivers/os88drv.inc": drv})
    check(d0 != d1, "DRV_VER's fingerprint moves with the API table too",
          "a driver far-calls the same cells through the same SDK, so a "
          "moved cell is a driver decision as well as a package one")
    # THE C SDK (engsys-11): os88.h's prototypes and #defines are PKG_FMT's
    # surface too, its comments are not, and os88thunk.asm is read as
    # assembly - a thunk calling a different cell moves it.
    h = open(os.path.join(ROOT, "apps", "cc", "os88.h")).read()
    th = open(os.path.join(ROOT, "apps", "cc", "os88thunk.asm")).read()

    def cmoves(what, rel, new, why, want=True):
        texts = {"apps/os88api.inc": api, "apps/cc/os88.h": h,
                 "apps/cc/os88thunk.asm": th}
        orig = texts[rel]
        texts[rel] = new
        fp = abilock.fingerprint("PKG_FMT", texts)
        check(new != orig, "the mutation for %s applies" % what,
              "a mutation that matched nothing proves nothing")
        check((fp != base) == want,
              "PKG_FMT's fingerprint %s for %s" % ("moves" if want else "stands",
                                                  what), why)

    cmoves("an os88.h prototype", "apps/cc/os88.h",
           h.replace("void os88_batch_begin(void);",
                     "int os88_batch_begin(void);", 1),
           "a C package compiled against the old prototype reads a return "
           "the thunk no longer makes")
    cmoves("an os88.h #define", "apps/cc/os88.h",
           h.replace("#define OS88_FERR_BIG    10", "#define OS88_FERR_BIG    11", 1),
           "a constant a C package compares an answer against")
    cmoves("an os88.h comment", "apps/cc/os88.h",
           h.replace("/* --- memory beyond your region", "/* --- MEMORY beyond your region", 1),
           "C prose is not ABI", want=False)
    cmoves("the cell a thunk calls", "apps/cc/os88thunk.asm",
           th.replace("CC_T_VOID _os88_batch_end,     OSAPI_BATCH_END",
                      "CC_T_VOID _os88_batch_end,     OSAPI_BATCH_BEGIN", 1),
           "a thunk reaching a different cell is a C package calling the "
           "wrong routine")

    # THE PRIVATE ABIs (drivers-16): each guard's fingerprint moves for a
    # code line of its include and stands for the guarded number's own line.
    for guard in ("VX_ABI_VER", "XM_ABI_VER", "HD_ABI_VER"):
        rel = abilock.GUARDS[guard]["defined"]
        txt = open(os.path.join(ROOT, rel)).read()
        f0 = abilock.fingerprint(guard, {rel: txt})
        lines = txt.splitlines()
        code = next(i for i, l in enumerate(lines)
                    if l.strip() and not l.lstrip().startswith(";")
                    and " equ " in l and not l.startswith(guard))
        mut = list(lines)
        mut[code] = mut[code].replace(" equ ", " equ 1 + ", 1)
        f1 = abilock.fingerprint(guard, {rel: "\n".join(mut)})
        check(f1 != f0, "%s's fingerprint moves for %s" % (guard, lines[code].split(";")[0].strip()),
              "the image compares this number at attach and nothing else "
              "makes anybody bump it")
        own = re.sub(r"^(%s\s+equ\s+)(\d+)" % guard,
                     lambda m: m.group(1) + str(int(m.group(2)) + 1), txt,
                     count=1, flags=re.M)
        check(own != txt and abilock.fingerprint(guard, {rel: own}) == f0,
              "%s's own line is not part of its fingerprint" % guard,
              "the bump is the decision, checked against the lock's version")
    done("t_abilock")


if __name__ == "__main__":
    main()
