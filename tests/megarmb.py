#!/usr/bin/env python3
"""megarmb - kern_mega's REAL-MODE BRACKET under Bochs (SPEC.md 53.2.3,
docs/plans/MEGA-VIDEO-REALMODE-PLAN.md W1).

    make mega megarun && python3 tests/megarmb.py [--keep]

`FSXF_REAL` leaves protected mode once at an fsx bracket's entry and comes
back once at its exit. This boots build/mega.img under Bochs (a 386: the CR0
way down; tests/megarmb286.py is the 286's reset way, on 86Box) with
tests/megarmb's two builds on C:'s RUN folder - MEGARMB, whose region is
above 1MB (OS88_HIGH) so the bracket runs a COPY of it below, and MEGARMBL,
whose region is conventional - and lets MEGARUN start both. Each runs three
legs from its window's timer (tests/megarmb/megarmb.asm): R (FSXF_REAL with
FSXF_KEEPWORKER), A (FSXF_REAL + FSXF_RATE at 291 Hz, a hook that skips) and
B (FSXF_REAL alone, OSAPI_FSX_MODE to mode 13h inside). Asserted, per package:

  1. R FALLS BACK (one task runs in real mode, so W2's bracket runs it in
     PROTECTED mode instead): CF = 0, SI = FSXR_KEEP and CPU_F_PM inside;
     A and B are taken in real mode, SI = FSXR_REAL;
  2. INSIDE A the machine is in real mode and step 3's words are real: the
     row stops Bochs while [fsx_rmode] = 1 and [k_dsel] = KERNEL_SEG, and
     reads CR0.PE = 0, [k_seg0] = 0, CPU_F_PM clear, VSEG's four words and
     vid_tab's VGA row the segments themselves, IVT 20h = fsx_rtick;
  3. the entry ran at a REAL paragraph (its CS < A000h) with OSAPI_CPU_INFO
     answering no CPU_F_PM - and for MEGARMB that paragraph is NOT its
     region's (the region is above 1MB: its copy ran);
  4. the HOOK'S PERIODS AGAINST THE PIT: periods handed / [ticks] moved is
     65536 / 4096 within 3 periods (tests/vidkern.py's arithmetic) - the
     accumulator, run by fsx_rtick - and the BIOS's 40:6C moved EXACTLY with
     [ticks], inside A and inside B; and a call was handed 2+ periods, with
     fewer calls than periods, so the skip path ran;
  5. the bss the entry proc wrote in its copy READS BACK after the bracket
     (MEGARMB's `mark`, read in protected mode from the window's copy);
  6. B's FSI_SEG is A000h: OSAPI_FSX_MODE ran in real mode and answered a
     segment, not the selector protected mode holds;
  7. [ticks] - 40:6C is the same before R and after B (the clocks agree);
  8. AFTER: IVT 20h-27h is the copy of 08h-0Fh again, [mega_ivtsh] matches
     the IVT, and the IDT's gates for 20h-27h and 70h-77h are what they were
     before (the forced sync rebuilt them); [fsx_rmode] is 0 and no bracket
     is up; and the desktop is idle with the pointer moving;
  9. no kern_mega FAULT or CLOSED line.

THE FAILING CASES (docs/WRITING-TESTS.md 1) are written in
MEGA-VIDEO-REALMODE-PLAN's W1 entry with what each turned red.
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

PKGS = ["build/megarmb.o88", "build/megarmbl.o88"]
NAMES = ("MEGARMB", "MEGARMBL")
DIV = 4096
KSEG = 0x0060
FAILS = []


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def parse(lines, name):
    """{leg: {field: int}} off NAME's E9 lines"""
    out = {}
    pfx = name + ": "
    for l in lines:
        if not l.startswith(pfx):
            continue
        f = l[len(pfx):].split()
        if not f:
            continue
        if f[0] in ("R", "A", "B"):
            d = {}
            for k, v in zip(f[1::2], f[2::2]):
                d[k] = int(v, 16)
            out[f[0]] = d
        elif f[0] == "off" and len(f) == 3:
            out["off"] = (int(f[1], 16), int(f[2], 16))
        elif f[0] == "done":
            out["done"] = True
    return out


def gates(box):
    """the IDT's 8-byte gates for 20h-27h and 70h-77h, and the IDT's base"""
    r = box.read("mega_idtr", 6)
    base = r[2] | r[3] << 8 | r[4] << 16
    return (box.read(base + 0x20 * 8, 64) + box.read(base + 0x70 * 8, 64),
            base)


def inside(box, name, secs=120):
    """Stop the machine INSIDE `name`'s leg A: [fsx_rmode] = 1 and [k_dsel]
    the real segment - and read what step 3 promised. -> dict, or None"""
    end = time.time() + secs
    while time.time() < end:
        if any(l.startswith(name + ": A ") for l in box.lines()):
            return None             # the leg ended before a look landed
        rm, kd = box.reads([("fsx_rmode", 1), ("k_dsel", 2)])
        if rm[0] == 1 and u16(kd) == KSEG:
            t = box.dbg(["creg"])
            m = re.search(r"CR0=0x([0-9a-f]+)", t)
            cr0 = int(m.group(1), 16) if m else None
            ks, cf, vs, vt, ivt = box.reads([
                ("k_seg0", 2), ("cpu_feat", 1), ("k_vsa", 8),
                ("vid_tab", 2), (0x20 * 4, 4)])
            return {"cr0": cr0, "k_seg0": u16(ks), "feat": cf[0],
                    "vs": [u16(vs, i) for i in range(0, 8, 2)],
                    "vidtab": u16(vt), "ivt20": (u16(ivt, 2), u16(ivt)),
                    "rtick": box.sym("fsx_rtick") - KSEG * 16}
        time.sleep(0.05)
    return None


def main():
    keep = "--keep" in sys.argv
    for p in PKGS + ["build/megarun.o88", "build/mega.img", "build/mbr.bin"]:
        need(p)
    out = tempfile.mkdtemp(prefix="megarmb-")
    t0 = time.time()
    box = os88bochs.Box(out, [os.path.join(ROOT, p) for p in PKGS])
    lines = []
    looks = {}
    bases = {}
    after = None
    try:
        if not box.wait_pm(240):
            raise RuntimeError("kern_mega never reached protected mode in 240s")
        box.up()
        g0, idt = gates(box)
        box.megarun()
        for name in NAMES:
            end = time.time() + 30
            while time.time() < end and not any(
                    l.startswith(name + ": start") for l in box.lines()):
                time.sleep(0.2)
            bases[name] = box.pkg_base(name)
            looks[name] = inside(box, name)
            try:
                box.until(lambda: any(l.startswith(name + ": done")
                                      for l in box.lines()) or box.fault(),
                          "%s's three legs" % name, 120, 0.25)
            except RuntimeError as e:
                # A HANG: a real-mode bracket has no kern_mega FAULT line
                # (SPEC.md 53.2.3), so a wild far call there stops dead
                want(False, "%s finished its legs - it hung in one: %s"
                     % (name, e))
                break
            if box.fault():
                break
        box.guest_sleep(1)
        lines = box.lines() + box.faults()   # (MEGA_E9: the kernel
                                            # says nothing on E9)
        if not FAILS:                   # (a hang has no desktop to read)
            box.ui_done("the desktop after the brackets", lock=False)
            g1, _ = gates(box)
            after = box.reads([(0, 0x200), ("mega_ivtsh", 64),
                               ("fsx_rmode", 1), ("fsx_task", 1)])
            box.capture()
            a = box.mouse()
            try:
                box.goto(300, 250)
                moved = box.mouse() != a
            except RuntimeError as e:
                moved = False
                print("   (pointer: %s)" % e)
    finally:
        box.stop()
    print("megarmb: %.0fs under Bochs" % (time.time() - t0))
    for l in lines:
        if l.startswith(("MEGARMB", "kern_mega FAULT", "kern_mega CLOSED")):
            print("   | " + l)
        if l.startswith(("kern_mega FAULT", "kern_mega CLOSED")):
            FAILS.append(l)
    for name in NAMES:
        r = parse(lines, name)
        print("\n   %s" % name)
        if not r.get("done"):
            want(False, "%s ran every leg (no `done` line)" % name)
            continue
        R, A, B = r.get("R", {}), r.get("A", {}), r.get("B", {})
        want(R.get("cf") == 0 and R.get("why") == 1
             and R.get("feat", 0) & 0x10,
             "1. R (FSXF_REAL + FSXF_KEEPWORKER) fell back: cf %s, SI %s "
             "(FSXR_KEEP), CPU_F_PM inside (feat %s)"
             % (R.get("cf"), R.get("why"), R.get("feat")))
        want(A.get("cf") == 0 and B.get("cf") == 0
             and A.get("why") == 0 and B.get("why") == 0,
             "1. A and B were taken in real mode (cf %s, %s; SI %s, %s)"
             % (A.get("cf"), B.get("cf"), A.get("why"), B.get("why")))
        lk = looks.get(name)
        if lk is None:
            want(False, "2. a look landed inside %s's leg A" % name)
        else:
            want(lk["cr0"] is not None and not lk["cr0"] & 1,
                 "2. inside: CR0.PE = 0 (CR0 %s)" % (
                     "%x" % lk["cr0"] if lk["cr0"] is not None else "?"))
            want(lk["k_seg0"] == 0, "2. inside: [k_seg0] = 0 (%04x)"
                 % lk["k_seg0"])
            want(not lk["feat"] & 0x10, "2. inside: CPU_F_PM clear in "
                 "[cpu_feat] (%02x)" % lk["feat"])
            want(lk["vs"] == [0xA000, 0xB000, 0xB800, 0xF000],
                 "2. inside: VSEG's words are segments (%s)"
                 % " ".join("%04x" % v for v in lk["vs"]))
            want(lk["vidtab"] == 0xA000, "2. inside: vid_tab's VGA row is "
                 "A000h (%04x)" % lk["vidtab"])
            want(lk["ivt20"] == (KSEG, lk["rtick"]),
                 "2. inside: IVT 20h is fsx_rtick (%04x:%04x)" % lk["ivt20"])
        base, cs = bases.get(name), A.get("cs", 0)
        if name == "MEGARMB":
            want(base is not None and base >= 0x100000 and cs * 16 != base,
                 "3. the region is above 1MB (%s) and the entry ran a COPY "
                 "of it, at %04x" % ("%06x" % base if base else base, cs))
        else:
            want(base is not None and cs * 16 == base,
                 "3. the region is conventional (%s) and the entry ran IN "
                 "it, at %04x" % ("%06x" % base if base else base, cs))
        want(A.get("cs", 0xFFFF) < 0xA000 and not A.get("feat", 0x10) & 0x10,
             "3. the entry ran at a real paragraph (cs %04x) and "
             "OSAPI_CPU_INFO said no CPU_F_PM (feat %02x)"
             % (A.get("cs", 0), A.get("feat", 0)))
        per, tk, bios = A.get("periods", 0), A.get("ticks", 0), A.get("bios",
                                                                       -1)
        exp = tk * 65536.0 / DIV
        print("     A: %d calls, %d periods, most %d in one; [ticks] %d, "
              "40:6C %d; periods/ticks %.4f (want %.4f)"
              % (A.get("calls", 0), per, A.get("max", 0), tk, bios,
                 per / max(tk, 1), 65536.0 / DIV))
        want(tk > 0 and abs(per - exp) <= 3,
             "4. the hook's periods against the PIT: %d against %.1f" %
             (per, exp))
        want(bios == tk, "4. 40:6C moved with [ticks] in A (%d, %d)" %
             (bios, tk))
        want(B.get("bios", -1) == B.get("ticks", -2) and B.get("ticks", 0),
             "4. ...and in B (%s, %s)" % (B.get("bios"), B.get("ticks")))
        want(A.get("max", 0) >= 2 and A.get("calls", per) < per,
             "4. the skip path ran: a call handed %d periods, %d calls for "
             "%d periods" % (A.get("max", 0), A.get("calls", 0), per))
        want(A.get("mark") == 0xA55A, "5. the bss written inside reads back "
             "(mark %04x)" % A.get("mark", 0))
        want(B.get("seg") == 0xA000, "6. B's OSAPI_FSX_MODE answered FSI_SEG "
             "A000h in real mode (%04x)" % B.get("seg", 0))
        off = r.get("off")
        want(off is not None and off[0] == off[1],
             "7. [ticks] - 40:6C the same before and after (%s)" % (off,))
    if after:
        ivt, sh, rm, ft = after
        print("\n   after")
        want(ivt[0x80:0xA0] == ivt[0x20:0x40],
             "8. IVT 20h-27h is the copy of 08h-0Fh again")
        want(sh == ivt[0x20:0x40] + ivt[0x1C0:0x1E0],
             "8. [mega_ivtsh] is the IVT's 08h-0Fh and 70h-77h")
        want(g1 == g0, "8. the IDT's gates 20h-27h and 70h-77h are as they "
             "were before (IDT at %06x)" % idt)
        want(rm[0] == 0 and ft[0] == 0xFF, "8. [fsx_rmode] 0, no bracket "
             "up (%d, %02x)" % (rm[0], ft[0]))
        want(moved, "8. the pointer moves on the desktop")
    if keep or FAILS:
        print("megarmb: the boot is in " + out)
    else:
        shutil.rmtree(out, ignore_errors=True)
    print("megarmb: %s" % ("FAIL - %d" % len(FAILS) if FAILS else "ok"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
