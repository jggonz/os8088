#!/usr/bin/env python3
"""kern_mega: THE POOL FILLED AND GIVEN BACK, then file activity and the next
launch (KERN-MEGA-PLAN 14.176).

    python3 tests/megapfl.py               the row: Bochs, a 4MB machine
    python3 tests/megapfl.py --break bx    the probe this row came from: RED
    python3 tests/megapfl.py --break free  OSAPI_XMEM_FREE a no-op: RED
    python3 tests/megapfl.py --keep        ...and leave the boot's directory

MEGAPFL (tests/megapfl) makes a 128KB OSAPI_MEM_HUGE claim in the pool
(HUGE_NOBRIDGE) and writes it, fills the pool with OSAPI_XMEM_ALLOC blocks -
MEMH_AVAIL's DX each time, until under 8KB is left - and frees the claim and
every block. This asserts: the claim in the pool (above 1MB), the pool's best
run after the frees what it was before, MEGAPFL's own PACKED file read back
whole into its segment above 1MB with its name field the image's, and then
the launch finishing - MEGARUN's `ok` and its next OSAPI_FILE_FIND (`done`) -
with no kern_mega FAULT or CLOSED anywhere.

WHY IT EXISTS: a probe of exactly this shape was reported as a kern_mega
regression of the F4 merge (a14db9f8) - FAULT 00 at 0060:64A5, 0D at
0060:5079 - and it was the probe's: its entry returned BX = MEMH_AVAIL's
total free KB (023Eh) instead of its window, against SPEC.md 20.2, and the
loader's step 9 bound that as a window (inst_bind_win -> wm_ptr2idx: a divide
overflow; wm_strseg: a #GP on a selector out of a non-window). It faulted the
same at e6e7930c, before F4.

THE FAILING CASES (docs/WRITING-TESTS.md 1), both measured when the row was
written: --break bx rebuilds MEGAPFL with -DPFL_BADBX, the reported probe's
BX, and the row fails `kern_mega FAULT 00 err 0000 at 0060:64A5`; --break free
pokes OSAPI_XMEM_FREE's cell to a `retf` before MEGARUN starts, and the row
fails `the pool's best run back` (128KB back of 2978KB).
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
FIELDS = ["before", "ccf", "hi", "fillkb", "blocks", "acf", "after", "rcf",
          "rlen", "name", "done"]
XMEM_FREE_CELL = 0x0166             # OSAPI_XMEM_FREE, KERNEL_SEG's offset


def check(what, ok, more=""):
    print("   %s  %s%s" % ("ok  " if ok else "FAIL", what,
                           (" - " + str(more)) if more != "" and not ok
                           else ""), flush=True)
    if not ok:
        fails.append(what)
    return ok


def verdict(r):
    print("megapfl: %s" % " ".join("%s=%X" % (k, r[k]) for k in FIELDS))
    if not check("the 128KB claim granted, in the pool (above 1MB)",
                 r["ccf"] == 0 and r["hi"] == 1,
                 "CF %X, above 1MB %X" % (r["ccf"], r["hi"])):
        return
    check("the pool filled to under 8KB (%dK in %d blocks)"
          % (r["fillkb"], r["blocks"]), r["blocks"] >= 1 and
          r["fillkb"] + 128 + 8 > r["before"], "%dK of %dK"
          % (r["fillkb"], r["before"]))
    check("the pool's best run back after the frees (%dK)" % r["before"],
          r["acf"] == 0 and r["after"] == r["before"],
          "CF %X, %dK" % (r["acf"], r["after"]))
    if check("MEGAPFL.O88 read back whole after it", r["rcf"] == 0 and
             r["rlen"] > 0x100, "CF %X, AX %X" % (r["rcf"], r["rlen"])):
        check("...its name field the image's", r["name"] == 1)


def pkg(brk, tmp):
    if brk != "bx":
        return os.path.join(ROOT, "build", "megapfl.o88")
    b, o = os.path.join(tmp, "megapfl.bin"), os.path.join(tmp, "megapfl.o88")
    subprocess.check_call(["nasm", "-f", "bin", "-w+error", "-DPFL_BADBX",
                           "-I", os.path.join(ROOT, "apps") + "/", "-o", b,
                           os.path.join(HERE, "megapfl", "megapfl.asm")])
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools",
                                                        "os88pkg.py"), b,
                           "-o", o], stdout=subprocess.DEVNULL)
    return o


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--break", dest="brk", choices=("bx", "free"))
    a = ap.parse_args()
    t0 = time.time()
    import os88bochs
    from os88fixture import need
    for p in ["build/megapfl.o88", "build/megarun.o88", "build/mega.img",
              "build/mbr.bin"]:
        need(p)
    out = tempfile.mkdtemp(prefix="megapfl-")
    os88bochs.MEGS = 4
    box = os88bochs.Box(out, [pkg(a.brk, out)])
    lines = []
    try:
        if box.wait_pm(240):
            box.up()
            if a.brk == "free":
                import os88sym
                equ = os88sym._parse_map(os.path.join(
                    ROOT, "build", "megak", "kernel.map"))[2]
                box.dbg(["setpmem 0x%x 1 0xcb" % (equ["KERNEL_SEG"] * 16
                                                  + XMEM_FREE_CELL)])
            box.megarun()
            end = time.time() + 300
            while time.time() < end:
                lines = box.said()
                if any(l.startswith(("kern_mega FAULT", "MEGARUN: done"))
                       for l in lines):
                    break
                time.sleep(0.3)
            box.guest_sleep(1)
            lines = box.said()
            seen = list(box.faults())
        else:
            fails.append("kern_mega never switched to protected mode in 240s")
            seen = []
    finally:
        box.stop()
        if not a.keep:
            shutil.rmtree(out, ignore_errors=True)
        else:
            print("megapfl: kept %s" % out)
    got = [l for l in lines if l.startswith("MEGAPFL: ")]
    if check("MEGAPFL answered", bool(got)):
        w = [int(x, 16) for x in got[-1].split()[1:]]
        if check("its line whole", len(w) >= len(FIELDS), got[-1]):
            verdict(dict(zip(FIELDS, w)))
    check("the launch finished (MEGARUN: ok) and the folder's next FIND "
          "answered (MEGARUN: done)", "MEGARUN: ok" in lines and
          "MEGARUN: done" in lines)
    bad = sorted(set(l for l in lines + seen
                     if l.startswith(("kern_mega FAULT", "kern_mega CLOSED"))))
    check("no kern_mega FAULT or CLOSED", not bad, "; ".join(bad[:3]))
    print("megapfl: %s in %.0fs" % ("PASS" if not fails else "FAIL (%s)"
                                    % "; ".join(fails), time.time() - t0))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
