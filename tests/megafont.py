#!/usr/bin/env python3
"""kern_mega: no kernel claim pins CONVENTIONAL memory - the glyph table's
copy and DESK.DRV live above 1MB (docs/plans/KERN-MEGA-PLAN.md 14.120).

    python3 tests/megafont.py            the row
    python3 tests/megafont.py --keep     ...and leave the boots' directory
    python3 tests/megafont.py --nohib    the boot leg alone (a minute)

THE REPORT: the owner's Victory 286 (an 8-bit Paradise PVGA1A option ROM)
showed the Task Manager's map with a claim at the top of 640K, and the font
and DESK.DRV listed under Kernel. On that machine FONT_PICK times the ROM
table more than a quarter slower than RAM, so font_init COPIES it into a 1KB
MEM_K_FONT claim - top-down and pinned, in real mode, before mega_enter - and
nothing ever moved it: a kilobyte of the conventional arena (the DOS trip's,
and a bridged huge claim's low piece) for a table whose every use is a
segment-register load. DESK.DRV was ALREADY above 1MB (mod_hi, 14.47); the
row holds it there.

This boots `make FONTSLOW=1 mega` (a private tree - the knob forces the copy
on any machine, the Victory's case) under Bochs at 4MB off C:'s boot
partition with the system floppy's root files beside it (HIBER.DRV), opens
C:\\RUN\\DESKCLOK.O88 - a desk widget, so the loader takes DESK.DRV - and
asserts:

  1. [font_seg] is a POOL selector (its base at or above 1MB) holding a
     glyph table - SPACE blank, `A` not;
  2. mem_tab holds NO kernel record (owner FBxx-FFxx; 14.121): no
     MEM_K_FONT, no MEM_K_MOD, nothing that walls the conventional run;
  3. DESK.DRV loaded, and its MODR_SEG a pool selector;
  4. hibernate, power off, boot the same C: and resume: `kern_mega:
     resumed`, and [font_seg] still the pool selector with the SAME bytes -
     hbm_wake's step 4a used to read a pool selector as a ROM (s2seg reads
     FFFF above 1MB) and swap the pointer for the slow ROM's, stranding the
     block;
  5. no kern_mega FAULT or CLOSED in either boot.

THE FAILING CASE (docs/WRITING-TESTS.md 1): take mem_hicaches_f's MEM_K_FONT
call out and (1) and (2) go red - `font_seg 114C base 0009F800` and a pinned
FF10 record at 9F80, the top of conventional memory. Take hbm_wake's
MEGA_ISHI out and (4) goes red on the resume.

THE SECOND HALF (KERN-MEGA-PLAN 14.121): the owner's Task Manager then showed
"Shortcut" beside the font - the desktop shortcuts' table, MEM_K_SC, read out
of SYSTEM.CFG by the boot overlay in real mode - and the audit found two more
the boot leaves low: the read-ahead store (MEM_P_DIRW, 32KB) and, on a
floppy boot with a hard disk, C:'s FAT window, which mega_pmtail's
dsk_fatw_lowf freed by PARAGRAPH after mega_on and so LEAKED. Both boots
here carry a SYSTEM.CFG with two shortcuts, and (2) is widened to EVERY
kernel record (owner FBxx-FFxx) bar the read-ahead's 5KB landing, which is an
int 13h target and must stay low; and:

  6. [sc_seg] is a pool selector and its rows' captions are the file's;
  7. a third boot, off the FLOPPY with C: beside it: the same, and C:'s FAT
     window above 1MB too.

Red on the tree before 14.121: `kernel claim(s) in conventional memory:
['2080 own FE02', '2880 own FF11']` on every boot, and `2B00 own FD22` on
the floppy one.
"""
import os
import shutil
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs as ob  # noqa: E402
import os88build  # noqa: E402
import subprocess  # noqa: E402

MC_SIZE, MEM_MAX = 10, 32
MOD_DESK, MODR_SIZE = 7, 4          # kernel/mod.inc
FONT_BYTES = 760                    # kernel/font.inc (95 glyphs x 8)


MEM_P_RAHB = 0xFE03                 # the read-ahead's landing: low by need
SC_REC, SC_R_CAP = 128, 49          # kernel/desksc.inc
SHORTS = [("C:RUN/0RUN.O88", "MEGARUN", 5), ("C:RUN/DESKCLOK.O88", "CLOCK", 6)]


def sysimg(src, out):
    """`src` with a SYSTEM.CFG carrying SHORTS in its trailer - what the boot
    overlay reads into MEM_K_SC in real mode, before the switch."""
    a = os.path.join(out, "sys.img")
    shutil.copy(src, a)
    cfg = os.path.join(out, "SYSTEM.CFG")
    args = [sys.executable, os.path.join(ROOT, "tools", "os88stik.py"),
            "cfg", "-o", cfg]
    for path, cap, cell in SHORTS:
        args += ["--short", "%s=%s@1,%d" % (path, cap, cell)]
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL)
    fat = os.path.join(ROOT, "tools", "os88fat.py")
    subprocess.run([sys.executable, fat, "del", a, "SYSTEM.CFG"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run([sys.executable, fat, "add", a, cfg], check=True,
                   stdout=subprocess.DEVNULL)
    return a


def low_and_sc(box, tag, fails):
    """14.121: no kernel record in conventional memory but the landing, and
    the shortcuts' table above 1MB holding the file's rows."""
    M, S, N = box.reads([("mem_tab", MC_SIZE * MEM_MAX), ("sc_seg", 2),
                         ("sc_nrows", 1)])
    low = []
    for k in range(MEM_MAX):
        sg, p, o, _, r = struct.unpack_from("<HHHHH", M, k * MC_SIZE)
        if sg:
            print("megafont: %-8s %04X %6d B own %04X%s" %
                  (tag, sg, p * 16, o, "  PINNED" if not r else ""))
            if o >= 0xFB00 and o != MEM_P_RAHB:
                low.append("%04X own %04X" % (sg, o))
    if low:
        fails.append("%s: kernel claim(s) in conventional memory: %s"
                     % (tag, low))
    sc = struct.unpack_from("<H", S, 0)[0]
    base = box.sel_base(sc) if sc & 4 else sc * 16
    print("megafont: %-8s sc_seg %04X base %08X rows %d" % (tag, sc, base,
                                                            N[0]))
    if not sc or base < 0x100000:
        fails.append("%s: the shortcuts' table is not above 1MB (sc_seg "
                     "%04X, base %05X)" % (tag, sc, base))
        return
    t = box.read(base, SC_REC * len(SHORTS))
    caps = [t[k * SC_REC + SC_R_CAP:(k + 1) * SC_REC].split(b"\0")[0]
            .decode("latin-1") for k in range(len(SHORTS))]
    if caps != [c for _, c, _ in SHORTS]:
        fails.append("%s: the shortcuts' rows are not the file's: %s"
                     % (tag, caps))


def state(box):
    M, F, T = box.reads([("mem_tab", MC_SIZE * MEM_MAX), ("font_seg", 2),
                         ("mod_tab", MODR_SIZE * (MOD_DESK + 1))])
    recs = [struct.unpack_from("<HHHHH", M, k * MC_SIZE)
            for k in range(MEM_MAX)]
    recs = [r for r in recs if r[0]]
    fseg = struct.unpack_from("<H", F, 0)[0]
    dseg = struct.unpack_from("<H", T, MOD_DESK * MODR_SIZE)[0]
    return recs, fseg, dseg


def check(box, tag, fails, want=None):
    recs, fseg, dseg = state(box)
    low_and_sc(box, tag, fails)
    base = box.sel_base(fseg) if fseg & 4 else fseg * 16
    print("megafont: %-8s font_seg %04X base %08X" % (tag, fseg, base))
    if base < 0x100000:
        fails.append("%s: the glyph table's copy is below 1MB (font_seg "
                     "%04X, base %05X)" % (tag, fseg, base))
        return None
    g = box.read(base, FONT_BYTES)
    if any(g[0:8]) or not any(g[33 * 8:34 * 8]):
        fails.append("%s: [font_seg] does not hold a glyph table" % tag)
    if want is not None and g != want:
        fails.append("%s: the table's bytes changed across the hibernate"
                     % tag)
    return g, dseg


def floppy(t, out, aimg, fails):
    """The third boot: off A: with C: beside it, so the boot's mount takes
    C:'s FAT window as a CLAIM - in real mode, before the switch."""
    fo = os.path.join(out, "floppy")
    os.makedirs(fo, exist_ok=True)
    box = ob.Box(fo, [], build=t.dir, csys=True, megs=4, aimg=aimg)
    try:
        if not box.wait_pm(300):
            fails.append("the floppy boot never reached protected mode")
            return
        box.up()
        low_and_sc(box, "floppy", fails)
        W = box.reads([("dsk_fatwc", 2 * 8), ("dsk_fatseg", 2)])
        for k, w in enumerate(struct.unpack("<8H", W[0])):
            if w & 4:               # (a paragraph is FAT_SEG, the pin
                b = box.sel_base(w)     # below the heap: no claim at all)
                print("megafont: floppy   FAT window %d %04X base %08X"
                      % (k, w, b))
                if b < 0x100000:
                    fails.append("floppy: volume %d's FAT window is below "
                                 "1MB (%04X)" % (k, w))
        lines = box.said()
    finally:
        box.stop()
    for ln in lines:
        if ln.startswith(("kern_mega FAULT", "kern_mega CLOSED")):
            fails.append("floppy: " + ln)


def main():
    keep = "--keep" in sys.argv
    t = os88build.tree("FONTSLOW=1", targets=(
        "mega.img", "megarun.o88", "mbr.bin")).apply()
    out = os.path.join(t.dir, "megafont")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    t0 = time.time()
    fails = []
    glyphs = None
    aimg = sysimg(os.path.join(t.dir, "mega.img"), out)
    box = ob.Box(out, [os.path.join(ROOT, "build", "deskclok.o88")],
                 build=t.dir, bootc=True, noa=True, csys=True, megs=4,
                 aimg=aimg)
    try:
        if not box.wait_pm(300):
            print("FAIL: kern_mega never reached protected mode")
            return 1
        box.up()
        r = check(box, "desktop", fails)
        box.capture()
        box.dbl(591, 160, "C:'s Disk window")
        box.dbl(150, 123, "RUN's listing")
        box.dbl(160, 155, "DESKCLOK")
        try:
            box.until(lambda: (box.mod_base("DESK.DRV") or 0) >= 0x100000,
                      "DESK.DRV above 1MB", 60, poll=0.5)
        except RuntimeError:
            fails.append("DESK.DRV was never loaded above 1MB")
        box.ui_done("the clock widget")
        r = check(box, "widget", fails)
        if r:
            glyphs, dseg = r
            if not dseg or not dseg & 4 or box.sel_base(dseg) < 0x100000:
                fails.append("DESK.DRV's MODR_SEG %04X is not a pool "
                             "selector" % dseg)
        box.shot(os.path.join(out, "widget.png"))
        if "--nohib" not in sys.argv:
            box.goto(10, 5)             # System menu > Hibernate...
            box.xd("mousedown", 1)
            for _ in range(5):          # 83 guest pixels, two X pixels each
                box.xd("mousemove_relative", "--", 0, 30)
                time.sleep(0.04)
            box.xd("mousemove_relative", "--", 0, 16)
            box.guest_sleep(0.5)    # the hold, in GUEST time
            box.xd("mouseup", 1)
            box.ui_done("the Hibernate dialog")
            box.goto(225, 225)          # Hibernate
            box.xd("click", 1)

            def off():
                s = box.read(0xB8000, 160 * 6)[0::2].decode("latin-1")
                return "hibernated" in s
            try:
                box.until(off, "the hibernate's `os8088 has hibernated`",
                          400, poll=1.0)
            except RuntimeError as e:
                fails.append(str(e))
        lines = box.said()
    finally:
        box.stop()
    for ln in lines:
        if ln.startswith(("kern_mega FAULT", "kern_mega CLOSED")):
            fails.append("boot: " + ln)
    if "--nohib" not in sys.argv and not fails:
        box = ob.Box(out, [], build=t.dir, cmode="keep", bootc=True,
                     noa=True, megs=4)
        try:
            if not box.wait_pm(300):
                fails.append("the second boot never reached protected mode")
            else:
                box.up()                # the desktop and its Resume card
                box.capture()
                box.goto(225, 225)      # Resume
                box.xd("click", 1)
                if not box.wait_resumed(120):
                    fails.append("no `kern_mega: resumed`")
                box.ui_done("the resumed desktop")
                box.shot(os.path.join(out, "resumed.png"))
                check(box, "resumed", fails, glyphs)
            lines = box.said()
        finally:
            box.stop()
        for ln in lines:
            if ln.startswith(("kern_mega FAULT", "kern_mega CLOSED")):
                fails.append("resume: " + ln)
    if "--nofloppy" not in sys.argv:
        floppy(t, out, aimg, fails)
    print("megafont: %.0fs under Bochs" % (time.time() - t0))
    for f in fails:
        print("FAIL: " + f)
    if keep or fails:
        print("megafont: the boots are in " + out)
    else:
        shutil.rmtree(out, ignore_errors=True)
    if not fails:
        print("megafont: ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
