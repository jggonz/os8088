#!/usr/bin/env python3
"""megatbl - kern_mega's tables, sized per machine (KERN-MEGA-MEMORY-PLAN MC7's
step 6).

    make mega && python3 tests/megatbl.py [--keep]

The GDT was the CPU's maximum, 64KB - an identity entry for every paragraph a
segment could name - and the LDT 16KB beside it, 82KB with the IDT on every
machine. MC7 made every claim, image, frame buffer and ROM an LDT selector,
so the identity map now stops at `.cold`'s segment, the LDT sits ON the GDT
(its base is the GDT's, its allocator starting at the first entry past it)
and the block is a pure function of the machine's extended memory. Boots
kern_mega under Bochs at 1MB, 4MB and 16MB and asserts, per machine:

  1. the GDTR's limit is MEGA_GDT_N x 8 - 1 - the identity map up to
     COLD_SEG and the scratches, and NOT ONE ENTRY MORE: a segment loaded
     past it is a #GP at the instruction;
  2. the LDT's descriptor has the GDT's base and [mega_ldtend] - 1 as its
     limit, [mega_ldtend] being [mega_ldtn] x 8, and [mega_ldtn] is
     MEGA_LDT_FREE0 + MEGA_LDT_BASE + one entry per 16KB of extended memory
     (to 15MB), rounded up to a 64-byte line;
  3. the IDTR's base is the GDT's plus [mega_ldtend], and [mega_tblkb] the
     block's KB - printed, so the saving is a number on every run;
  4. the desktop is up, B:'s Disk window and the Task Manager open, and no
     kern_mega FAULT or CLOSED line is written.

Broken on purpose: mega_gdtr's limit put back to the 64KB table (8192 x 8 -
1) fails 1 on every machine; the Task Manager's EBDA read put back to a bare
`mov es, cx` fails 4 with `kern_mega CLOSED TaskMgr 0D` (seen when the row
was written: that read and its ROM scan were the first two sites the cut
reported).
"""
import os
import re
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "megak")
import os88bochs  # noqa: E402
import os88sym  # noqa: E402
from os88fixture import need  # noqa: E402

EQ = os88sym.equates(("KERN_MEGA",))
GDT_N = EQ["MEGA_GDT_N"]
FREE0 = EQ["MEGA_LDT_FREE0"]
LBASE = EQ["MEGA_LDT_BASE"]
IDT = EQ["MEGA_IDT_SIZE"]
FAILS = []


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def menu(box, dy):
    box.goto(10, 5)
    box.xd("mousedown", 1)
    left = dy * 2
    while left > 30:
        box.xd("mousemove_relative", "--", 0, 30)
        time.sleep(0.04)
        left -= 30
    box.xd("mousemove_relative", "--", 0, left)
    time.sleep(0.5)
    box.xd("mouseup", 1)


def machine(megs, keep):
    out = tempfile.mkdtemp(prefix="megatbl%d-" % megs)
    box = os88bochs.Box(out, [], bimg=os.path.join(ROOT, "build",
                                                   "megaapps.img"),
                        btype="1_44", megs=megs)
    print("megatbl: %dMB" % megs, flush=True)
    try:
        box.up()
        box.capture()
        ext = u16(box.reads([("mega_extkb", 2)])[0])
        n = u16(box.reads([("mega_ldtn", 2)])[0])
        end = u16(box.reads([("mega_ldtend", 2)])[0])
        kb = u16(box.reads([("mega_tblkb", 2)])[0])
        sreg = box.dbg(["sreg"])
        g = re.search(r"gdtr:base=0x([0-9a-f]+), limit=0x([0-9a-f]+)", sreg)
        i = re.search(r"idtr:base=0x([0-9a-f]+), limit=0x([0-9a-f]+)", sreg)
        ldt = re.search(r"ldtr:0x[0-9a-f]+, dh=0x([0-9a-f]+), "
                        r"dl=0x([0-9a-f]+)", sreg)
        if not (g and i and ldt):
            want(False, "%dMB: Bochs's sreg named no GDTR, IDTR or LDTR"
                 % megs)
            return
        gb, gl = int(g.group(1), 16), int(g.group(2), 16)
        ib = int(i.group(1), 16)
        dh, dl = int(ldt.group(1), 16), int(ldt.group(2), 16)
        lb = (dl >> 16) | ((dh & 0xFF) << 16) | (dh & 0xFF000000)
        ll = (dl & 0xFFFF) | (dh & 0xF0000)
        exp_n = (FREE0 + LBASE + min(ext, 15360) // 16 + 7) & ~7
        want(gl == GDT_N * 8 - 1,
             "1. %dMB: the GDT's limit is %04Xh (MEGA_GDT_N x 8 - 1 = %04Xh)"
             % (megs, gl, GDT_N * 8 - 1))
        want(n == exp_n and end == n * 8 and lb == gb and ll == end - 1,
             "2. %dMB: the LDT on the GDT (base %06Xh against %06Xh), %d "
             "entries for %dKB extended (want %d), limit %04Xh"
             % (megs, lb, gb, n, ext, exp_n, ll))
        want(ib == gb + end and kb == (end + IDT + 1023) // 1024,
             "3. %dMB: the IDT at the GDT + %04Xh (%06Xh), the block %dKB "
             "(82 before MC7's step 6)" % (megs, end, ib, kb))
        box.dbl(591, 105, "B:'s Disk window")
        menu(box, 51)
        box.ui_done("the Task Manager", secs=120)
        bad = [l for l in box.lines() if "kern_mega FAULT" in l or
               "kern_mega CLOSED" in l]
        want(not bad, "4. %dMB: B:'s Disk window and the Task Manager, no "
             "fault%s" % (megs, "".join(" | " + l for l in bad[:3])))
    except Exception as e:      # noqa: BLE001 - the machine stopped
        want(False, "%dMB: %s" % (megs, e))
    finally:
        box.stop()
        if keep:
            print("megatbl: the boot is in " + out)
        else:
            shutil.rmtree(out, ignore_errors=True)


def main():
    keep = "--keep" in sys.argv
    for p in ("build/mega.img", "build/megaapps.img", "build/megak/kernel.map"):
        need(p)
    t0 = time.time()
    for megs in (1, 4, 16):
        machine(megs, keep)
    print("megatbl: %.0fs under Bochs" % (time.time() - t0))
    for f in FAILS:
        print("FAIL megatbl: " + f)
    print("megatbl: %s" % ("FAILED" if FAILS else "PASS"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
