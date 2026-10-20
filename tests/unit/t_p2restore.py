#!/usr/bin/env python3
"""§9.9's FAILURE paths must put the 8042's command byte back UNDOCTORED.

    python3 tests/unit/t_p2restore.py

The field defect (SPEC.md 9.9.7) was one bit: `.fail` restored
`[mou_p2cmd0]`, which is banked with bit 5 forced SET for `mou_p2_off`'s
sake. On a PS/2 controller that bit is the auxiliary clock and forcing it is
intended. On an AT controller it is **PC MODE**, which stops the 8042
translating set 2 to set 1 — so every key on the machine came out as a
different character, for the rest of the session, and `f` typed `\\`.

THIS CANNOT BE GATED AT RUNTIME ON ANYTHING IN THIS TREE, and that is why
the check is here and is structural. §9.9's probe is gated on `[cpu_tier]`
so it never runs on an 8088 (MartyPC is one); QEMU's i8042 is a PS/2
controller with a mouse on it, so the probe SUCCEEDS there and no failure
path runs at all — and QEMU does not model the translate bit either, which
was measured: clearing it deliberately in the success path leaves
`tests/ps2mouse.py` fully green. 86Box has the controller and no automation.

So the invariant is asserted over the SOURCE: inside `mou_p2_init`, the
paths that leave WITHOUT a mouse write the raw bank, and the doctored one is
read only by the success path and by `mou_p2_off`, which by construction
runs on a controller where a PS/2 mouse answered its own reset.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "kernel", "mouse.inc")
RAW, DOC = "mou_p2cmdr", "mou_p2cmd0"
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def body(text, label):
    """The lines of `label:` up to the next top-level label."""
    out, on = [], False
    for ln in text.splitlines():
        if re.match(r"^%s:" % re.escape(label), ln):
            on = True
            continue
        if on and re.match(r"^[a-z_][a-z0-9_]*:", ln):
            break
        if on:
            out.append(ln.split(";")[0])
    return out


def main():
    text = open(SRC, errors="replace").read()
    init = body(text, "mou_p2_init")
    check(init, "mou_p2_init is in kernel/mouse.inc (%d lines)" % len(init))

    # the local labels of mou_p2_init, and which lines belong to each
    where, cur = {}, None
    for ln in init:
        m = re.match(r"^(\.[a-z0-9_]+):", ln)
        if m:
            cur = m.group(1)
            where.setdefault(cur, [])
            continue
        if cur:
            where[cur].append(ln)
    for arm in (".fail", ".noaux"):
        check(arm in where, "mou_p2_init still has a %s arm" % arm)

    for arm in (".fail", ".noaux"):
        lines = where.get(arm, [])
        reads = [l.strip() for l in lines if DOC in l]
        check(not reads,
              "%s does not restore the DOCTORED bank (%s): %s"
              % (arm, DOC, reads or "none"))
        check(any(RAW in l for l in lines),
              "...and does write the raw one (%s)" % RAW)

    # ...and the doctored bank stays legal exactly where it is meant to be
    holders = set()
    cur = None
    for ln in text.splitlines():
        m = re.match(r"^([a-z_][a-z0-9_]*):", ln)
        if m:
            cur = m.group(1)
        if DOC in ln.split(";")[0] and "db 0" not in ln and cur:
            holders.add(cur)
    # mou_dbg_p2 is MOUDIAG=1's panel: it SHOWS the byte and cannot write a
    # port, and the offset guard names it in an %if - neither is a restore
    ok = {"mou_p2_init", "mou_p2_off", "mou_dbg_p2"}
    check(holders <= ok,
          "%s reaches only mou_p2_init's success path, mou_p2_off and the "
          "MOUDIAG panel (%s)" % (DOC, ", ".join(sorted(holders - ok)) or "-"))

    # the raw bank is taken from the byte as READ, with bit 4 alone masked
    m = re.search(r"and al, 0xEF\s*\n[^\n]*\n?\s*mov \[%s\], al" % RAW, text)
    check(bool(m) or re.search(r"and al, 0xEF[^\n]*\n\s*mov \[%s\], al" % RAW,
                               text),
          "%s is the byte as read with bit 4 alone cleared" % RAW)

    print("  %s" % ("ok" if not bad else "FAILED: %d" % len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
