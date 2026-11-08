#!/usr/bin/env python3
"""kern_mega: A HUGE CLAIM REGROWS, every shape (docs/plans/MEGA-HUGE-PLAN.md
F8, KERN-MEGA-PLAN 14.174, SPEC.md 50.3.5).

    python3 tests/megargw.py            the row: Bochs, a 4MB machine
    python3 tests/megargw.py --box      ...86Box's at286_vga_sb, 640K + 384K
    python3 tests/megargw.py --keep     ...and leave the boot's directory

OSAPI_MEM_HUGE's claim on kern_mega in protected mode is one run of LDT
selectors stepped by 8 over a pool block, over a conventional claim
(mega_hugec), or over one piece of each (a BRIDGE). MEGARGW (tests/megargw)
writes a pattern by the step walk, regrows, and reads it back by the walk:

  L1  a 128KB pool claim (HUGE_NOBRIDGE), the pool filled with XMEM blocks,
      regrown to 256KB: claimed again in the heap, copied, the old freed -
      windows 0 and 1 the pattern, window 3 new and writable;
  L2  a 256KB claim made with the pool still full (mega_hugec's run in the
      heap) to 384KB (windows 0..5 one contiguous run, 0, 1 and 5 the
      pattern) and to 160KB (windows 0..2, window 3 freed, window 2's limit
      32KB - 1);
  L3  the largest bridge: its conventional piece shrunk by 128KB and grown
      back by 64KB (windows P-1, P the pattern, the piece's windows one
      contiguous run), then the claim taken below its cut - the pool's one
      run, every window above 1MB, window P-2 the pattern.

THE FAILING CASE (docs/WRITING-TESTS.md 1): mega-release before F8 refused
L1's regrow (the pool's arm never moved a run out of the pool) and every L3
regrow (a bridge "does not regrow"), and L2's grow regrew the claim as a
plain one - windows 4 and 5 whatever descriptors followed the run.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)

fails = []
FIELDS = ["acf", "apool", "fillkb", "g1cf", "g1conv", "g1pat", "g1w3",
          "g2cf", "g2contig", "g2pat", "s2cf", "s2fit", "s2lim",
          "m", "bcf", "p", "b1cf", "b1pat", "b2cf", "b2pat", "b3cf", "b3run",
          "done"]


def check(what, ok, more=""):
    print("   %s  %s%s" % ("ok  " if ok else "FAIL", what,
                           (" - " + str(more)) if more != "" and not ok
                           else ""), flush=True)
    if not ok:
        fails.append(what)
    return ok


def verdict(r):
    print("megargw: %s" % " ".join("%s=%X" % (k, r[k]) for k in FIELDS))
    if check("L1: a 128KB claim in the pool", r["acf"] == 0 and
             r["apool"] == 1, "CF %X, pool %X" % (r["acf"], r["apool"])):
        print("   --    the pool filled with %dK of XMEM blocks" % r["fillkb"])
        if check("L1: regrown to 256KB past the full pool (claim, copy, "
                 "free)", r["g1cf"] == 0, "refused"):
            check("L1: ...in the heap now", r["g1conv"] == 1)
            check("L1: windows 0 and 1 the pattern", r["g1pat"] == 1)
            check("L1: window 3 written and read", r["g1w3"] == 1)
        if check("L2: a 256KB run in the heap (the pool full) regrown to "
                 "384KB", r["g2cf"] == 0, "refused"):
            check("L2: windows 0..5 one contiguous run", r["g2contig"] == 1,
                  "windows 4 and 5 are not the claim's")
            check("L2: windows 0, 1 and 5 the pattern", r["g2pat"] == 1)
            if check("L2: ...and to 160KB", r["s2cf"] == 0, "refused"):
                check("L2: windows 0..2 contiguous, window 3's entry freed",
                      r["s2fit"] == 1, {2: "not contiguous",
                                        3: "window 3 still readable"}.get(
                                            r["s2fit"], r["s2fit"]))
                check("L2: window 2's limit 32KB - 1", r["s2lim"] == 0x7FFF,
                      "%X" % r["s2lim"])
    print("   --    L3: the largest bridge %dK, cut at window %d"
          % (r["m"], r["p"]))
    if not check("L3: the bridge claimed", r["bcf"] == 0 and r["p"] >= 2,
                 "CF %X, P %X" % (r["bcf"], r["p"])):
        return
    if not check("L3: its conventional piece shrunk by 128KB",
                 r["b1cf"] == 0, "refused"):
        return
    check("L3: windows P-1 and P the pattern", r["b1pat"] == 1)
    if not check("L3: ...grown back by 64KB", r["b2cf"] == 0, "refused"):
        return
    check("L3: windows P-1, P the pattern, the piece's windows one run",
          r["b2pat"] == 1, {2: "the pattern wrong",
                            3: "not contiguous"}.get(r["b2pat"], r["b2pat"]))
    if check("L3: the claim taken below its cut", r["b3cf"] == 0, "refused"):
        check("L3: the pool's one run, every window above 1MB, window P-2 "
              "the pattern", r["b3run"] == 1,
              {2: "a window below 1MB", 3: "the pattern wrong"}.get(
                  r["b3run"], r["b3run"]))


def unpack(raw, partial=False):
    at = raw.find(b"MRGRES01")
    if at < 0:
        return None
    w = struct.unpack_from("<%dH" % len(FIELDS), raw, at + 8)
    r = dict(zip(FIELDS, w))
    return r if r["done"] == 1 or partial else None


def run_bochs(keep):
    import os88bochs
    from os88fixture import need
    for p in ["build/megargw.o88", "build/megarun.o88", "build/mega.img",
              "build/mbr.bin"]:
        need(p)
    out = tempfile.mkdtemp(prefix="megargw-")
    os88bochs.MEGS = int(os.environ.get("OS88_BOCHS_MEGS", "4"))
    box = os88bochs.Box(out, [os.path.join(ROOT, "build/megargw.o88")])
    lines = []
    try:
        if box.wait_pm(240):
            box.up()
            box.megarun()
            end = time.time() + 300
            while time.time() < end:
                lines = box.said()
                if any(l.startswith(("kern_mega FAULT", "MEGARGW: "))
                       for l in lines):
                    break
                time.sleep(0.3)
            box.guest_sleep(1)
            lines = box.said()
        else:
            fails.append("kern_mega never switched to protected mode in 240s")
    finally:
        box.stop()
        if not keep:
            shutil.rmtree(out, ignore_errors=True)
        else:
            print("megargw: kept %s" % out)
    for l in lines:
        if l.startswith(("kern_mega FAULT", "kern_mega CLOSED")):
            fails.append(l)
    for l in lines:
        if l.startswith("MEGARGW") and not l.startswith("MEGARGW: "):
            print("megargw: a leg's line: " + l)
    got = [l for l in lines if l.startswith("MEGARGW: ")]
    if not check("MEGARGW answered", bool(got)):
        return
    w = [int(x, 16) for x in got[-1].split()[1:]]
    if len(w) < len(FIELDS):
        fails.append("short line: %s" % got[-1])
        return
    verdict(dict(zip(FIELDS, w)))


def run_box(keep):
    os.environ.setdefault("OS88_MEGA", "1")
    os.environ.setdefault("OS88_MEGA_EMU", "86box")
    import os88build
    import os88marty as M
    import os88ui
    import megajw as J
    B = lambda p: os88build.at(os.path.join("build", p))     # noqa: E731
    tmp = tempfile.mkdtemp(prefix="megargw-")
    try:
        disk = os.path.join(tmp, "b.img")
        subprocess.check_call(
            [sys.executable, os.path.join(ROOT, "tools", "os88disk.py"), "-o",
             disk, "--size", "1440", B("megargw.o88"), "--folder",
             "SYSTEM/APPDATA"], stdout=subprocess.DEVNULL)
        os.environ["OS88_MEGA_MEGS"] = os.environ.get("OS88_MEGA_MEGS", "1.375")
        with os88ui.boot(B("mega.img"), apps=disk, machine=J.MACHINE,
                         label="megargw") as ui:
            m = ui.m
            ui.open_drive("B")
            ui.open("MEGARGW.O88", expect=None)
            r, last = None, None
            for _ in range(240):
                spots = [J.u16(m.read(m.sym("ld_base"), 2), 0)]
                spots += [sp for nm, sp in J.insts(m).values()]
                for sp in spots:
                    try:
                        raw = m.readseg(sp, 0, 2048)
                        r = unpack(raw) or r
                        last = unpack(raw, True) or last
                    except Exception:
                        pass
                if r:
                    break
                M.guest_sleep(m, 1)
            if not check("MEGARGW answered", r is not None,
                         "titles %s" % ui.titles()):
                for l in m.faults()[-12:]:
                    print("   fault: " + l)
                if last:
                    print("megargw: what it had written: " + " ".join(
                        "%s=%X" % (k, last[k]) for k in FIELDS))
                return
            verdict(r)
    finally:
        if not keep:
            shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--box", action="store_true", help="86Box, 640K + 384K")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    t0 = time.time()
    (run_box if a.box else run_bochs)(a.keep)
    print("megargw: %s in %.0fs" % ("PASS" if not fails else "FAIL (%s)"
                                    % "; ".join(fails), time.time() - t0))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
