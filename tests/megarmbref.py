#!/usr/bin/env python3
"""megarmbref - the real-mode bracket's FALLBACK and its TABLE under Bochs
(SPEC.md 53.2.3, docs/plans/MEGA-VIDEO-REALMODE-PLAN.md W2).

    make mega megarun && python3 tests/megarmbref.py [--keep]

FSXF_REAL asks kern_mega to run an fsx bracket in real mode. Where real mode
cannot be had the bracket does NOT refuse: it runs in protected mode, as it
always did, and SI says why (the owner's rule: a refusal is for a state the
user will not normally be in). And inside a real-mode bracket the API table
is SWAPPED: a cell the bracket does not offer answers CF = 1. This boots
build/mega.img under Bochs (a 386) with tests/megarmb/megarmbf.asm's MEGARMBF
on C:'s RUN folder (OS88_HIGH, OS88_HIGHMEM) and lets MEGARUN start it. Its
legs, and what is asserted of each:

  K  FSXF_KEEPWORKER - CF 0, SI = FSXR_KEEP (1), CPU_F_PM inside, the entry ran
  S  a TF_SERVICE task (this row sets the bit on task 0 while the package
     waits, and clears it after) - REAL since W7 (SI = 0, no CPU_F_PM): the
     worker is parked, where it was FSXR_SVC
  E  EXTD.DRV's row poked with a value that names no descriptor, then put
     back - REAL since W7: the door tests no row (it was FSXR_EXTD). A
     LOADED image is tests/megarmbext.py's, on two cards under 86Box
  L  a REAL SERIAL.DRV lease on COM1, a byte queued in its ring by hand -
     REAL since W7 (it was FSXR_SERIAL), and inside, the pointer's ISR run
     at IRQ4's vector served the leased row through SERIAL.DRV at its
     paragraph and sent the byte from the ring's COPY: after, the OWNER's
     ring (its region above 1MB) shows it gone (x = 0101)
  M  the heap filled by the package's own claims - SI = FSXR_ROOM (5),
     protected, ran
  H  an OSAPI_MEM_HUGE claim ABOVE 1MB in apps/os88real.inc's list:
     os88real_prep answers CF = 1, the package asks without FSXF_REAL - the
     PACKAGE's own fallback - and the bracket runs in protected mode
  P  a conventional claim in the list: prep CF = 0, a REAL-mode bracket
     (SI = 0, no CPU_F_PM), os88real_swap made the word the real paragraph
     prep banked, MF_PAT written through it there reads back through the
     selector afterwards
  N  a real-mode bracket in which OSAPI_MEM_P (a rare DOOR cell) and
     OSAPI_FILE_RENAME (a 3-byte JCELL) answer CF = 1 with every register
     kept; the OSAPI_SEG twin answers SEG_LIN = CS x 16 and refuses SEG_AT;
     OSAPI_FSX_WAIT (FSXW_FRAME) and OSAPI_MOUSE answer; and, stopped INSIDE
     N, the table is exactly the real-mode image: every byte `stc`, a `retf`
     before each kept run and at the end, the kept runs (kernel.asm's
     rmk_*) the protected-mode bytes, OSAPI_SEG a far jump to osapi_segr -
  n  ...and the package's own re-run without FSXF_REAL, where the claim N
     could not have is had
  and after all of it: the table byte for byte what it was, [fsx_rwhy] at
  rest (FFh), [fsx_rmode] 0, no bracket up, task 0's flags, EXTD.DRV's row
  and [ser_row] as they were, the pointer moving, and no kern_mega FAULT.

THE FAILING CASES (docs/WRITING-TESTS.md 1) are written in
MEGA-VIDEO-REALMODE-PLAN's W2 entry with what each turned red.
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
import os88bochs  # noqa: E402
from os88fixture import need  # noqa: E402

PKG = "build/megarmbf.o88"
NAME = "MEGARMBF"
KSEG = 0x0060
def _api_tlen():
    """the API table's length off kern_mega's map - a constant here went
    stale the day OSAPI_MEM_P joined the table (kern_mega size pass 2)"""
    import os88sym
    off, _, _ = os88sym._parse_map(os.path.join(ROOT, "build", "megak",
                                                "kernel.map"))
    return off["osapi_table_end"] - off["osapi_table"]


TBL, TLEN = 0x0010, _api_tlen()
FAILS = []
# (rmk_3 and rmk_6 went with T5, kernel size pass 19: OSAPI_SND_CAPS,
# _FONT_GLYPHS and _FSX_MODE are RARE cells now, answered inside the bracket
# by THE DOOR's first RD_RK published indices - fsx_rdownf's bound, below)
RUNS = ["rmk_1", "rmk_2", "rmk_4", "rmk_5", "rmk_7", "rmk_8", "rmk_9",
        "rmk_10"]


def cell(name):
    """a hot cell's offset in the table, off the SDK (no number typed here)"""
    m = re.search(r"^%%define\s+%s\s+KERNEL_SEG:(0x[0-9A-Fa-f]+)" % name,
                  open(os.path.join(ROOT, "apps", "os88api.inc")).read(), re.M)
    return int(m.group(1), 16)


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def equates():
    """the kern_mega map's equates (MOD_EXT, MODR_SIZE, ...)"""
    out = {}
    with open(os.path.join(ROOT, "build", "megak", "kernel.map")) as f:
        for l in f:
            m = re.match(r"\s*([0-9A-Fa-f]{8})\s+(\w+)\s*$", l)
            if m:
                out.setdefault(m.group(2), int(m.group(1), 16))
    return out


def resoff():
    """the result block's offset: os88_image_end, the .bin header's +8"""
    with open(os.path.join(ROOT, "build", "megarmbf.bin"), "rb") as f:
        return u16(f.read(12), 8)


def parse(lines):
    out = {}
    pfx = NAME + ": "
    for l in lines:
        if not l.startswith(pfx):
            continue
        f = l[len(pfx):].split()
        if not f:
            continue
        if len(f[0]) == 1 and f[0] in "KSELMHPNn" and len(f) > 1:
            if f[0] == "N" and f[1] == "claim":
                d = {}
                for k, v in zip(f[1::2], f[2::2]):
                    d[k] = int(v, 16)
                out["Nx"] = d
                continue
            d = {}
            for k, v in zip(f[1::2], f[2::2]):
                d[k] = int(v, 16)
            out[f[0]] = d
        elif f[0] == "done":
            out["done"] = True
    return out


# W3's SERVICE-AREA twins (fsx_rkeepm): a far jump to [fsx_rmsseg]:twin, the
# twin's offset being `.mega`'s own
TWINS = (("apic_xm_caps", "rms_xcaps"), ("apic_xm_copy", "rms_xcopy"),
         ("apic_drv_pkg_call", "rms_drvx"), ("apic_drv_call_at", "rms_drva"),
         ("rmk_rseq", "rms_rseq"), ("rmk_sstr", "rms_sstr"),
         ("rmk_spk", "rms_spk"))       # (W4: the stream and the speaker)


def megaoff():
    """`.mega` labels' offsets (os88bochs's map places only the four
    segments real mode names)"""
    import os88sym
    off, sect, _ = os88sym._parse_map(os.path.join(ROOT, "build", "megak",
                                                   "kernel.map"))
    return off


def expect_table(before, box, msseg):
    """the real-mode image fsx_rdownf promises, off the protected-mode one"""
    img = bytearray([0xF9] * TLEN)
    img[TLEN - 1] = 0xCB
    mo = megaoff()
    for cell, twin in TWINS:
        c = box.sym(cell) - KSEG * 16 - TBL
        t = mo[twin]
        img[c - 1] = 0xCB
        img[c:c + 5] = bytes([0xEA, t & 0xFF, t >> 8, msseg & 0xFF,
                              msseg >> 8])
    runs = []
    for r in RUNS:
        a = box.sym(r) - KSEG * 16 - TBL
        e = box.sym(r + "e") - KSEG * 16 - TBL
        runs.append((a, e))
    for a, e in sorted(runs, reverse=True):
        img[a - 1] = 0xCB
        img[a:e] = before[a:e]
    s = box.sym("rmk_seg") - KSEG * 16 - TBL
    img[s - 1] = 0xCB
    t = box.sym("osapi_segr") - KSEG * 16
    img[s:s + 5] = bytes([0xEA, t & 0xFF, t >> 8, KSEG & 0xFF, KSEG >> 8])
    return bytes(img), runs


def main():
    keep = "--keep" in sys.argv
    for p in [PKG, "build/megarmbf.bin", "build/megarun.o88",
              "build/mega.img", "build/mbr.bin"]:
        need(p)
    eq = equates()
    roff = resoff()
    out = tempfile.mkdtemp(prefix="megarmbref-")
    t0 = time.time()
    box = os88bochs.Box(out, [os.path.join(ROOT, PKG)])
    lines, look, after, moved, msseg = [], None, None, False, 0
    lim_in = lim_after = None
    ext = None
    try:
        if not box.wait_pm(240):
            raise RuntimeError("kern_mega never reached protected mode in 240s")
        box.up()
        ext = box.sym("mod_tab") + eq["MOD_EXT"] * eq["MODR_SIZE"] \
            + eq["MODR_SEG"]
        tf = box.sym("sch_tasks") + eq["T_FLAGS"]
        before = box.reads([(KSEG * 16 + TBL, TLEN), (tf, 1), (ext, 2),
                            ("ser_row", 2), ("vx_ep", 4)])
        tbl0, tf0, ext0, ser0, vx0 = before
        box.megarun()
        end = time.time() + 30
        while time.time() < end and not any(
                l.startswith(NAME + ": start") for l in box.lines()):
            time.sleep(0.2)
        done = set()
        deadline = time.time() + 300
        pokes = {"S": [("setpmem 0x%x 1 0x%x" % (tf, tf0[0] | eq["TF_SERVICE"]))],
                 "E": ["setpmem 0x%x 2 0x1234" % ext]}
        unpokes = {"S": ["setpmem 0x%x 1 0x%x" % (tf, tf0[0])],
                   "E": ["setpmem 0x%x 2 0x%x" % (ext, u16(ext0))]}
        while time.time() < deadline:
            ls = box.lines()
            if any(l.startswith(NAME + ": done") for l in ls) or box.fault():
                break
            for l in ls:
                m = re.match(NAME + r": (un)?wait (\w)$", l)
                if not m or l in done:
                    continue
                done.add(l)
                base = box.pkg_base(NAME)
                go = base + roff
                c = m.group(2)
                if m.group(1):
                    box.dbg(unpokes[c] + ["setpmem 0x%x 1 0" % go])
                else:
                    box.dbg(pokes[c] + ["setpmem 0x%x 1 0x%x" % (go, ord(c))])
            if look is None and any(l.startswith(NAME + ": P ") for l in ls):
                rm, kd, rs, ms = box.reads([("fsx_rmode", 1),
                                            ("k_dsel", 2), ("fsx_rseg", 2),
                                            ("fsx_rmsseg", 2)])
                if rm[0] == 1 and u16(kd) == KSEG:
                    ins = box.read(u16(rs) * 16 + roff + 19, 1)[0]
                    if ins == 1:
                        look = box.read(KSEG * 16 + TBL, TLEN)
                        lim_in = box.read(box.sym("os88_door.lim") + 2, 1)[0]
                        msseg = u16(ms)
            time.sleep(0.05)
        else:
            want(False, "%s finished its legs in 300s" % NAME)
        box.guest_sleep(1)
        lines = box.lines() + box.faults()   # (MEGA_E9: the kernel
                                            # says nothing on E9)
        if not FAILS and not box.fault():
            box.ui_done("the desktop after the brackets", lock=False)
            after = box.reads([(KSEG * 16 + TBL, TLEN), (tf, 1), (ext, 2),
                               ("ser_row", 2), ("vx_ep", 4), ("fsx_rwhy", 1),
                               ("fsx_rmode", 1), ("fsx_task", 1)])
            exp, runs = expect_table(tbl0, box, msseg)
            lim_after = box.read(box.sym("os88_door.lim") + 2, 1)[0]
            box.capture()
            a = box.mouse()
            try:
                box.goto(300, 250)
                moved = box.mouse() != a
            except RuntimeError as e:
                print("   (pointer: %s)" % e)
    finally:
        box.stop()
    print("megarmbref: %.0fs under Bochs" % (time.time() - t0))
    for l in lines:
        if l.startswith((NAME, "kern_mega FAULT", "kern_mega CLOSED")):
            print("   | " + l)
        if l.startswith(("kern_mega FAULT", "kern_mega CLOSED")):
            FAILS.append(l)
    r = parse(lines)
    if not r.get("done"):
        want(False, "%s ran every leg (no `done` line)" % NAME)
    PM = 0x10

    def leg(k, why, pm, what):
        d = r.get(k, {})
        want(d.get("cf") == 0 and d.get("why") == why
             and bool(d.get("feat", 0) & PM) == pm
             and d.get("mark") == 0xC33C,
             "%s: %s - cf %s, SI %s (want %d), %s inside (feat %s), the "
             "entry %s" % (k, what, d.get("cf"), d.get("why"), why,
                           "protected" if pm else "real", d.get("feat"),
                           "ran" if d.get("mark") == 0xC33C else "DID NOT run"))
        return d
    leg("K", 1, True, "FSXF_KEEPWORKER fell back")
    leg("S", 0, False, "a TF_SERVICE task: real mode taken, the worker "
        "parked (W7)")
    leg("E", 0, False, "EXTD.DRV's row poked: real mode taken, the door "
        "tests no row (W7)")
    ll = leg("L", 0, False, "a SERIAL.DRV lease open: real mode taken (W7)")
    want(ll.get("x") == 0x0101, "L: the lease opened, the pointer's ISR ran "
         "at IVT 24h inside and sent the queued byte from the ring's copy - "
         "the OWNER's ring shows it gone (x %04x, want 0101: EEnn the open "
         "refused, 02nn IRQ4's vector not the kernel's, 0100 nothing sent)"
         % ll.get("x", 0))
    m = leg("M", 5, True, "no conventional room: fell back")
    want(m.get("x", 0) > 0, "M: the package held %d claims" % m.get("x", 0))
    h = leg("H", 0, True, "the package's own fallback (not asked)")
    want(h.get("prep") == 0xFF and 0x10 <= h.get("x", 0) < 0xFFFF,
         "H: the HUGE claim is above 1MB (its linear 64KB %04x) and "
         "os88real_prep refused it (%s)" % (h.get("x", 0), h.get("prep")))
    p = leg("P", 0, False, "a renamed claim, real mode taken")
    want(p.get("prep") == 0 and p.get("real") == p.get("bank")
         and 0 < p.get("real", 0) < 0xA000,
         "P: inside, the word was the real paragraph prep banked (%04x, "
         "bank %04x)" % (p.get("real", 0), p.get("bank", 0)))
    want(p.get("x") == 0x5AA5, "P: MF_PAT written through the real segment "
         "reads back through the selector (%04x)" % p.get("x", 0))
    leg("N", 0, False, "real mode taken")
    nx = r.get("Nx", {})
    want(nx.get("claim") == 0xFF01 and nx.get("kept") == 0x5BAD,
         "N: OSAPI_MEM_P (MEMP_CLAIM) answered CF = 1 with every register kept "
         "(%04x, %04x)" % (nx.get("claim", 0), nx.get("kept", 0)))
    want(nx.get("rename") == 0xFFFF, "N: OSAPI_FILE_RENAME (3 bytes) answered "
         "CF = 1 (%04x)" % nx.get("rename", 0))
    want(nx.get("lin") == nx.get("cs", -1) * 16 and nx.get("at") == 0xFFFF,
         "N: the SEG twin - SEG_LIN %08x = CS %04x x 16, SEG_AT refused (%04x)"
         % (nx.get("lin", 0), nx.get("cs", 0), nx.get("at", 0)))
    want(nx.get("wait") == 0 and nx.get("mouse", 0xFFFF) < 0x0400,
         "N: OSAPI_FSX_WAIT answered CF = 0 (%04x), OSAPI_MOUSE a pointer x "
         "(%04x)" % (nx.get("wait", 0), nx.get("mouse", 0)))
    n2 = leg("n", 0, True, "the package's protected-mode re-run")
    want(n2.get("x", 1) == 0, "n: the claim N could not have is had there")
    if look is None:
        want(False, "a look landed INSIDE leg N")
    elif after:
        bad = [i for i in range(TLEN) if look[i] != exp[i]]
        want(not bad, "inside N: the table is the real-mode image "
             "(%d runs kept, the twin at rmk_seg; %d bytes differ%s)"
             % (len(runs), len(bad), ", first at %04x: %02x want %02x"
                % (TBL + bad[0], look[bad[0]], exp[bad[0]]) if bad else ""))
        ren = cell("OSAPI_FILE_RENAME")
        want(look[ren - TBL] == 0xF9,
             "inside N: OSAPI_FILE_RENAME (%04x) begins `stc`" % ren)
        want(lim_in == eq["RD_P"] + eq["RD_RK"],
             "inside N: the rare door answers only its first RD_RK published "
             "indices - os88_door.lim's bound %s, want RD_P + RD_RK = %02x "
             "(OSAPI_MEM_P, a door past it, refused above)"
             % ("%02x" % lim_in if lim_in is not None else "unread",
                eq["RD_P"] + eq["RD_RK"]))
    if after:
        t1, tf1, ext1, ser1, vx1, why, rm, ft = after
        print("\n   after")
        want(t1 == tbl0, "the table is byte for byte what it was")
        want(lim_after == eq["RD_N"], "the rare door's bound is RD_N again "
             "(%s, want %02x)" % ("%02x" % lim_after if lim_after is not None
                                  else "unread", eq["RD_N"]))
        want((tf1, ext1, ser1, vx1) == (tf0, ext0, ser0, vx0),
             "task 0's flags, EXTD.DRV's row, [ser_row] and [vx_ep] as "
             "they were")
        want(why[0] == 0xFF and rm[0] == 0 and ft[0] == 0xFF,
             "[fsx_rwhy] at rest (%02x), [fsx_rmode] 0, no bracket up (%02x)"
             % (why[0], ft[0]))
        want(moved, "the pointer moves on the desktop")
    if keep or FAILS:
        print("megarmbref: the boot is in " + out)
    else:
        shutil.rmtree(out, ignore_errors=True)
    print("megarmbref: %s" % ("FAIL - %d" % len(FAILS) if FAILS else "ok"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
