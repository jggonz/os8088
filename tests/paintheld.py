#!/usr/bin/env python3
"""WHAT A PAINT NOBODY IS FILLING OR COPYING WITH HOLDS (SPEC.md 42.6.2.1, 42.6.6)

    make && python3 tests/paintheld.py [--machine M] [--o88 P --src S]

Two of Paint's heap claims used to be taken at startup and held for the life
of the window whatever the user did: the flood fill's span stack (9 KB on a
machine that can fund the full tier) and a 4 KB clipboard floor. Neither holds
anything until its tool or command is used, so the apps size pass moved them:

  * the span stack belongs to the FILL TOOL. Picking the tool up claims it,
    picking any other tool hands it back - per FILL was measured at 14 ms of
    claim-and-free on every fill, so the tool is where the cost goes;
  * the clipboard is claimed by the COPY that fills it, at the size it holds.

What the user sees did not change - [pt_havefill] and [pt_haveclip] still say
whether the heap could fund each, and that is what the tool's grey and the
Edit menu's labels are drawn from. So this reads the claim words themselves,
which is the one place the difference shows:

  idle     a fresh Paint: no stack, no clipboard, and both still OFFERED
  fill     the fill tool in hand holds the full stack; a fill leaves it held
  pencil   putting the fill tool down hands it back
  copy     a small selection copied claims a clipboard SMALLER than the old
           floor - it is sized to what it holds, not to 4 KB

BREAK IT ON PURPOSE: `--o88 <a PAINT.O88 from before the pass> --src <its
paint.asm>` runs the same script against it, and it fails at `idle` - the
stack and the floor are both there before anybody touched anything.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88marty                                         # noqa: E402
import os88sym                                           # noqa: E402
import os88ui                                            # noqa: E402
import dispapps                                          # noqa: E402

FAILS = []


def pkgmap(src):
    """Every symbol of the package as assembled from `src` (nasm's own map)."""
    d = tempfile.mkdtemp(prefix="paintheld", dir=os.path.join(ROOT, "build"))
    try:
        w = os.path.join(d, "w.asm")
        mp = os.path.join(d, "m.map")
        with open(w, "w") as f:
            f.write('[map symbols %s]\n%%include "%s"\n' % (mp, os.path.abspath(src)))
        subprocess.check_call(["nasm", "-f", "bin", "-I", os.path.join(ROOT, "apps") + "/",
                               "-o", os.path.join(d, "o.bin"), w])
        out = {}
        for line in open(mp):
            m = re.match(r'\s*([0-9A-F]+)\s+([0-9A-F]+)\s+(\S+)$', line)
            if m:
                out[m.group(3)] = int(m.group(2), 16)
            m = re.match(r'([0-9A-F]{8})\s+(\S+)$', line)
            if m:
                out[m.group(2)] = int(m.group(1), 16)
        return out
    finally:
        shutil.rmtree(d)


def check(name, ok, detail):
    print("   %-7s %s  %s" % (name, "ok" if ok else "FAIL", detail))
    if not ok:
        FAILS.append(name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--o88", default="build/paint.o88")
    ap.add_argument("--src", default="apps/paint/paint.asm")
    a = ap.parse_args()
    os.chdir(ROOT)
    M = pkgmap(a.src)
    apps = os88marty.scratch_disk(os.path.join(ROOT, "build", "paintheld-%d.img"
                                               % os.getpid()), "APPS:" + a.o88)
    try:
        with os88ui.boot("build/os8088-360.img", apps=apps, machine=a.machine) as ui:
            m, mo = ui.m, ui.mo
            ui.path("B:/APPS/PAINT.O88")
            got = dispapps.pkg_seg(m, 0)
            if got is None:
                sys.exit("paintheld: Paint did not open")
            seg = got[1]

            def rd(n, size=2):
                return int.from_bytes(m.read((seg << 4) + M[n], size), "little")

            def idle():
                os88marty.quiesce(m, lambda: m.disk().get("reads"), guest=1.0,
                                  what="the drive to go quiet")
                os88marty.until(m, lambda mm: mm.read(os88sym.linear("gfx_lock_flag"), 1)[0] == 0,
                                "the gfx lock to be free", poll=0.2, limit=60)
                os88marty.quiesce(m, lambda: (rd("pt_scseg"), rd("pt_cbseg"), rd("pt_tool", 1)),
                                  guest=0.5, what="Paint's claim words")

            def tool(i):
                cx0, cy0 = rd("pt_cx0"), rd("pt_cy0")
                x = 1 if not (i & 1) else 22
                mo.click(cx0 - 48 + x + 10, cy0 + 1 + 21 * (i >> 1) + 10)
                idle()

            os88marty.until(m, lambda mm: rd("pt_cx0") != 0, "Paint's first paint",
                            poll=0.2, limit=60)
            idle()
            check("idle", rd("pt_scseg") == 0 and rd("pt_cbseg") == 0
                  and rd("pt_havefill", 1) == 1 and rd("pt_haveclip", 1) == 1,
                  "(scseg %04x cbseg %04x, havefill %d haveclip %d - want none held "
                  "and both offered)" % (rd("pt_scseg"), rd("pt_cbseg"),
                                         rd("pt_havefill", 1), rd("pt_haveclip", 1)))

            tool(M["PT_T_FILL"])
            held, spans = rd("pt_scseg"), rd("pt_fstkmax")
            check("fill", held != 0 and spans == M["PT_FSTK_MAX"] and rd("pt_tool", 1) == M["PT_T_FILL"],
                  "(the tool in hand: scseg %04x, %d spans - want held, the full %d)"
                  % (held, spans, M["PT_FSTK_MAX"]))
            cx0, cy0 = rd("pt_cx0"), rd("pt_cy0")
            mo.click(cx0 + 100, cy0 + 60)
            idle()
            check("filled", rd("pt_scseg") != 0,
                  "(after a fill: scseg %04x - the tool still holds it)" % rd("pt_scseg"))

            tool(M["PT_T_PENCIL"])
            check("pencil", rd("pt_scseg") == 0,
                  "(the fill tool put down: scseg %04x - want it handed back)" % rd("pt_scseg"))

            tool(M["PT_T_SEL"])
            cx0, cy0 = rd("pt_cx0"), rd("pt_cy0")
            mo.drag(cx0 + 20, cy0 + 20, cx0 + 60, cy0 + 40)
            idle()
            m.ctrl("KeyC")
            idle()
            cb, paras, w = rd("pt_cbseg"), rd("pt_cbparas"), rd("pt_cbw")
            check("copy", cb != 0 and w > 0 and 0 < paras < M["PT_CLIPMINP"],
                  "(%d-wide selection: cbseg %04x holding %d paragraphs - want "
                  "under the old %d-paragraph floor)" % (w, cb, paras, M["PT_CLIPMINP"]))
    finally:
        os88marty.scratch_drop(apps)    # and scratch_disk's sidecar
    if FAILS:
        print("paintheld: FAIL - %s" % ", ".join(FAILS))
        return 1
    print("paintheld: PASS - a Paint holds a fill stack only with the fill tool "
          "in hand, and a clipboard only once something is copied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
