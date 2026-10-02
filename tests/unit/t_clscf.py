#!/usr/bin/env python3
"""Every caller of drv_cls_svc / drv_cls_fp tests the carry it is handed.

    python3 tests/unit/t_clscf.py

docs/plans/LAST-DROP-BYTES.md 7.7.8. Both routines publish "CF = 1 if the
class has no slot" and a caller that misses the test does not crash: it reads
or writes whatever DI happens to name, which for drv_cls_svc was drv_svc+0 -
the SOUND driver's table - and is the silent cross-class disconnection SPEC.md
51.2.1 exists to prevent, arriving through the routine that implements it.
Since kernel size pass 8 drv_cls_svc REFUSES a real class (DRVC_POINT has no
slot: USBMOUSE.DRV publishes DSV_NAME alone and nothing reads it), so a missed
test is no longer theoretical, and this is what makes it a build error.

The rule, per call site of `drv_cls_svc_x`, `drv_cls_fp_x` or their far
`drvf_drv_cls_svc` form:

  * a `jc`/`jnc` within the next few instructions, with nothing between that
    can change CF (a push, a pop, a mov, an xchg), OR
  * the very next instruction is `retf` or `ret` - a thunk hands CF straight
    back, and ITS callers are the ones checked, by its name (drvf_drv_cls_svc
    far, CTRL.DRV's cp_clssvc near), OR
  * a `; CLSCF: <reason>` on the call line, for a site whose class is known to
    have a slot - the reason is required and is what a reviewer reads.

NASM cannot do this (it has no control flow) and neither can a .bss canary
(the bad write lands at the START of the table, not past its end). Break it
on purpose: drop the `jc .out` in drv_svc_clear and this names the line.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CALL = re.compile(r"^(?:[A-Za-z_]\w*:)?\s*call\s+(?:COLD_SEG:)?"
                  r"(drv_cls_svc_x|drv_cls_fp_x|drvf_drv_cls_svc|cp_clssvc)\b",
                  re.I)
JCF = re.compile(r"^\s*j(?:c|nc|b|nb|ae|nae)\s", re.I)
KEEPS = re.compile(r"^\s*(?:push|pop|mov|xchg|lea|pushf)\b", re.I)
RETF = re.compile(r"^\s*retf?\b", re.I)
REACH = 4


def code(line):
    return line.split(";", 1)[0].strip()


def main():
    kdir = os.path.join(ROOT, "kernel")
    bad, n = [], 0
    for fn in sorted(os.listdir(kdir)):
        if not fn.endswith((".inc", ".asm")):
            continue
        path = os.path.join(kdir, fn)
        lines = open(path, errors="replace").read().split("\n")
        for i, ln in enumerate(lines):
            if not CALL.match(ln):
                continue
            n += 1
            if re.search(r";.*CLSCF:\s*\S", ln):
                continue
            ok, seen = False, 0
            for nxt in lines[i + 1:]:
                c = code(nxt)
                if not c or c.startswith("%"):
                    continue
                if seen == 0 and RETF.match(c):
                    ok = True
                    break
                if JCF.match(c):
                    ok = True
                    break
                if not KEEPS.match(c) or seen >= REACH:
                    break
                seen += 1
            if not ok:
                bad.append("kernel/%s:%d: %s" % (fn, i + 1, ln.strip()[:60]))
    for b in bad:
        print("  FAIL: " + b)
        print("        want: a jc/jnc within %d CF-neutral instructions, or "
              "`; CLSCF: <why this class has a slot>`" % REACH)
    if n < 10:
        print("t_clscf: found only %d call sites - the pattern has rotted" % n)
        return 1
    print("t_clscf: %d drv_cls_svc/drv_cls_fp call sites, %d unchecked"
          % (n, len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
