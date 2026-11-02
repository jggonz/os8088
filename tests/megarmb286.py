#!/usr/bin/env python3
"""megarmb286 - kern_mega's REAL-MODE BRACKET on a 286, by the RESET path
(SPEC.md 53.2.3, docs/plans/MEGA-VIDEO-REALMODE-PLAN.md W1).

    make mega megarun && python3 tests/megarmb286.py [--keep] [--vm 286-victory]

tests/megarmb.py is the same bracket under Bochs, whose CPU is a 386 and so
leaves protected mode by clearing CR0.PE. A 286 cannot: lmsw sets PE and
nothing clears it, so the way down is the reflection's - CMOS shutdown code
0Ah, a triple fault, and the BIOS's reset path back to mega_r286 at 40:67,
with the Victory's quirks on the way (its BIOS re-initialises the 8259 there,
KERN-MEGA-PLAN 14.62 and 14.64). This boots build/mega.img on 86Box's
`286-victory` (the owner's Packard Bell Victory 286 as near as 86Box comes:
an AMI 286 at 16 MHz, 4MB, a Paradise PVGA1A) with tests/megarmb's two builds
and MEGARUN on B:, double-clicks MEGARUN, and lets it start both. 86Box has
no port E9, so every verdict is read out of the packages' own bss (megarmb.asm's
result block) and the kernel's words. Asserted, per package:

  1. R (FSXF_REAL + FSXF_KEEPWORKER) FALLS BACK - it runs in protected
     mode, SI = FSXR_KEEP, CPU_F_PM inside (W2), and costs no reset; A and
     B are taken in real mode, SI = FSXR_REAL;
  2. INSIDE A, [k_dsel] = KERNEL_SEG and [fsx_rmode] = 1 (the CPU is in real
     mode in the bracket), [k_seg0] = 0, CPU_F_PM clear, VSEG's words the
     segments themselves, IVT 20h = fsx_rtick;
  3. the entry ran at a real paragraph without CPU_F_PM, and for MEGARMB a
     COPY of its region above 1MB;
  4. the hook's periods against [ticks] are 65536 / 4096 within 3, 40:6C
     moved exactly with [ticks] in A and in B, and the skip path ran;
  5. the bss the entry proc wrote inside reads back after;
  6. B's FSI_SEG is A000h;
  7. [ticks] - 40:6C is the same before and after;
  8. THE RESET PATH: every taken bracket cost exactly ONE CPU reset at
     mega_down's own `int 3` (86Box's OS88SOFTRESET log) - two a package -
     and the return none (lmsw);
  9. after: IVT 20h-27h the copy of 08h-0Fh, [mega_ivtsh] the IVT's,
     [fsx_rmode] 0, no bracket up, the pointer moving; and no exception
     charged to a loaded image.

THE FAILING CASES (docs/WRITING-TESTS.md 1) are in MEGA-VIDEO-REALMODE-PLAN's
W1 entry. Needs a built 86Box (python3 tools/os88box.py build).
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests"),
                os.path.join(ROOT, "tests", "rdgcore")]
ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
ap.add_argument("--keep", action="store_true")
ap.add_argument("--vm", default="286-victory")
A = ap.parse_args()
import os88box                                                # noqa: E402
import os88geom as G                                          # noqa: E402

B = os.path.join(ROOT, "build")
NAMES = ("MEGARMB", "MEGARMBL")
DIV = 4096
KSEG = 0x0060
# the ami286's POST from a fresh CMOS, answered (tests/rdgcore/rdgwin.py's)
PROMPTS = (("bypass MEMORY test", "esc"), ("to RESUME", "f1"),
           ("EXIT FOR BOOT", "return"))
DRIVE_B = (592, 105)            # the B: zone on the VGA desktop
FIRST = (160, 127)              # the first file of a fresh B: window
FAILS = []


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def resoff(name):
    """the result block's offset in NAME's image: os88_image_end, which is
    the header's image size (+8) of its UNcompressed .bin - per build, the
    two differing by their strings"""
    with open(os.path.join(B, name.lower() + ".bin"), "rb") as f:
        h = f.read(12)
    return u16(h, 8)


def insts(b):
    """{name: I_SPTR} for every live package instance"""
    raw = b.memr(b.sym("inst_tab"), G.INST_MAX * G.I_RECSZ)
    out = {}
    for i in range(G.INST_MAX):
        r = raw[i * G.I_RECSZ:(i + 1) * G.I_RECSZ]
        if r[G.I_STATE] != 1 or not r[G.I_KIND] & G.KIND_PKG:
            continue
        nm = r[G.I_NAME:G.I_NAME + 16].split(b"\0")[0].decode("latin-1")
        out[nm] = u16(r, G.I_SPTR)
    return out


def resets(b, ip):
    """OS88SOFTRESETs at KERNEL_SEG:IP so far in this run"""
    n = 0
    for line in open(os.path.join(b.run, "stderr.log"), errors="replace"):
        m = os88box.RESET.search(line)
        if m and int(m.group(1), 16) == KSEG and int(m.group(2), 16) == ip:
            n += 1
    return n


def lines_now(b):
    return sum(1 for _ in open(os.path.join(b.run, "stderr.log"),
                               errors="replace"))


def faults(b, since=0):
    """exceptions charged to a loaded image, after stderr line SINCE: the
    boot's own completed faults (two #GPs this machine always takes before
    a desktop, a write through CS that mega_gpfix finishes) are not the
    bracket's"""
    out = []
    for k, line in enumerate(open(os.path.join(b.run, "stderr.log"),
                                  errors="replace")):
        if k < since:
            continue
        m = os88box.FAULT.search(line)
        if m and int(m.group(2), 16) < 0xF000 and \
                int(m.group(2), 16) not in (0x0060, 0x0028):
            out.append(line.strip())
    return out


def koff(b, name):
    """a .text label's offset in KERNEL_SEG (os88box's own map reading)"""
    a = os88box.map_index(b.kmap()).get(name)
    if a is None:
        raise SystemExit("megarmb286: no %s in %s" % (name, b.kmap()))
    return a


def main():
    for f in ("mega.img", "megak/kernel.map", "megarun.o88", "megarmb.o88",
              "megarmbl.o88", "megarmb.bin", "megarmbl.bin"):
        if not os.path.exists(os.path.join(B, f)):
            sys.exit("megarmb286: build/%s is not built - `make mega megarun`"
                     % f)
    tmp = tempfile.mkdtemp(prefix="megarmb286")
    img = os.path.join(tmp, "mega.img")         # (named mega*: os88box then
    shutil.copy(os.path.join(B, "mega.img"), img)   # reads megak/'s map)
    stage = os.path.join(tmp, "stage")
    os.makedirs(stage)
    files = []
    for src, dst in (("megarun.o88", "0RUN.O88"), ("megarmb.o88", "MEGARMB.O88"),
                     ("megarmbl.o88", "MEGARMBL.O88")):
        shutil.copy(os.path.join(B, src), os.path.join(stage, dst))
        files.append(os.path.join(stage, dst))
    bimg = os.path.join(tmp, "megab.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", bimg, "--size", "1440"] + files, check=True,
                   stdout=subprocess.DEVNULL)
    b = os88box.launch(A.vm, name="megarmb286%d" % os.getpid(), hold=True,
                       fresh=True, a=img, b=bimg, temp=not A.keep, turbo=True)
    got = {}
    after = None
    rtick = None
    since = 0
    try:
        b.kernel_up(prompts=PROMPTS)
        b.until(lambda: b.byte("mega_on"), "protected mode", 30000, 250)
        b.ui_done(cap_ms=10000)
        ip = koff(b, "mega_down.hang") - 2    # (`int 3` is CD 03 before it)
        rtick = koff(b, "fsx_rtick")
        r0 = resets(b, ip)
        since = lines_now(b)
        print("   mega_down's reset is at %04x:%04x; %d before" % (KSEG, ip,
                                                                  r0))
        b.to(*DRIVE_B)
        b.click(2)
        b.ui_done(limit_ms=20000)
        b.to(*FIRST)
        b.click(2)
        for name in NAMES:
            b.until(lambda: name in insts(b), "%s to start" % name, 60000, 100)
            sptr = insts(b)[name]
            base = b.lin(sptr)
            res = base + resoff(name)
            look = {}

            def inside():
                rm = b.byte("fsx_rmode")
                kd = b.word("k_dsel")
                if rm == 1 and kd == KSEG:
                    look["k_seg0"] = b.word("k_seg0")
                    look["feat"] = b.byte("cpu_feat")
                    vs = b.memr(b.sym("k_vsa"), 8)
                    look["vs"] = [u16(vs, i) for i in range(0, 8, 2)]
                    iv = b.memr(0x80, 4)
                    look["ivt20"] = (u16(iv, 2), u16(iv))
                    return True
                return b.memr(res, 1)[0] == 1     # done before a look landed
            b.until(inside, "%s inside its real-mode bracket" % name, 60000, 20)
            b.until(lambda: b.memr(res, 1)[0] == 1, "%s's three legs" % name,
                    120000, 100)
            got[name] = (base, b.memr(res, 48), look)
        r1 = resets(b, ip)
        b.ui_done(limit_ms=20000, lock=False)
        ivt = b.memr(0, 0x200)
        sh = b.memr(b.sym("mega_ivtsh"), 64)
        rm, ft = b.byte("fsx_rmode"), b.byte("fsx_task")
        p0 = b.pointer()
        b.to(300, 250)
        moved = b.pointer() != p0
        after = (ivt, sh, rm, ft, moved, r1 - r0)
    except SystemExit as e:
        want(False, "the machine stopped answering or a step missed: %s" % e)
    finally:
        f = faults(b, since)
        b.quit(remove=not A.keep)
    for name in NAMES:
        if name not in got:
            want(False, "%s ran every leg" % name)
            continue
        base, r, lk = got[name]
        print("\n   %s (region at %06x)" % (name, base))
        rcf, acf, bcf = r[1], r[2], r[3]
        cs, feat, calls = u16(r, 4), r[6], u16(r, 8)
        per = u16(r, 10) | u16(r, 12) << 16
        mx, adt, adb, mark = u16(r, 14), u16(r, 16), u16(r, 18), u16(r, 20)
        bseg, bdt, bdb, off0, off1 = (u16(r, 22), u16(r, 24), u16(r, 26),
                                      u16(r, 28), u16(r, 30))
        rwhy, rfeat, awhy, bwhy = r[44], r[45], r[46], r[47]
        want(rcf == 0 and rwhy == 1 and rfeat & 0x10,
             "1. R fell back to protected mode (cf %02x, SI %02x = "
             "FSXR_KEEP, feat %02x)" % (rcf, rwhy, rfeat))
        want(acf == 0 and bcf == 0 and awhy == 0 and bwhy == 0,
             "1. A and B were taken in real mode (%02x, %02x; SI %02x, %02x)"
             % (acf, bcf, awhy, bwhy))
        if not lk:
            want(False, "2. a look landed inside %s's leg A" % name)
        else:
            want(lk["k_seg0"] == 0, "2. inside: [k_seg0] = 0 (%04x)" %
                 lk["k_seg0"])
            want(not lk["feat"] & 0x10, "2. inside: CPU_F_PM clear (%02x)" %
                 lk["feat"])
            want(lk["vs"] == [0xA000, 0xB000, 0xB800, 0xF000],
                 "2. inside: VSEG's words are segments (%s)" %
                 " ".join("%04x" % v for v in lk["vs"]))
            want(lk["ivt20"] == (KSEG, rtick), "2. inside: IVT 20h is "
                 "fsx_rtick (%04x:%04x)" % lk["ivt20"])
        if name == "MEGARMB":
            want(base >= 0x100000 and cs * 16 != base,
                 "3. the region is above 1MB and a COPY ran, at %04x" % cs)
        else:
            want(cs * 16 == base, "3. the region is conventional and ran "
                 "where it is, at %04x" % cs)
        want(cs < 0xA000 and not feat & 0x10, "3. a real paragraph (%04x), "
             "no CPU_F_PM (%02x)" % (cs, feat))
        exp = adt * 65536.0 / DIV
        print("     A: %d calls, %d periods, most %d; [ticks] %d, 40:6C %d"
              % (calls, per, mx, adt, adb))
        want(adt and abs(per - exp) <= 3, "4. the hook's periods against the "
             "PIT: %d against %.1f" % (per, exp))
        want(adb == adt and bdb == bdt and bdt,
             "4. 40:6C moved with [ticks] (A %d/%d, B %d/%d)" %
             (adb, adt, bdb, bdt))
        want(mx >= 2 and calls < per, "4. the skip path ran (most %d, %d "
             "calls)" % (mx, calls))
        want(mark == 0xA55A, "5. the bss written inside reads back (%04x)" %
             mark)
        want(bseg == 0xA000, "6. B's FSI_SEG is A000h (%04x)" % bseg)
        want(off0 == off1, "7. [ticks] - 40:6C the same before and after "
             "(%04x, %04x)" % (off0, off1))
    if after:
        ivt, sh, rm, ft, moved, nres = after
        print("\n   after")
        want(nres == 2 * len(NAMES), "8. one reset a taken bracket at "
             "mega_down's int 3: %d for %d brackets" % (nres, 2 * len(NAMES)))
        want(ivt[0x80:0xA0] == ivt[0x20:0x40],
             "9. IVT 20h-27h is the copy of 08h-0Fh again")
        want(sh == ivt[0x20:0x40] + ivt[0x1C0:0x1E0],
             "9. [mega_ivtsh] is the IVT's 08h-0Fh and 70h-77h")
        want(rm == 0 and ft == 0xFF, "9. [fsx_rmode] 0, no bracket up")
        want(moved, "9. the pointer moves on the desktop")
    want(not f, "9. exceptions charged to a loaded image: %d%s" % (
        len(f), "".join("\n        " + x for x in f[:6])))
    if A.keep:
        print("megarmb286: kept %s" % tmp)
    else:
        shutil.rmtree(tmp, ignore_errors=True)
    print("megarmb286: %s" % ("FAIL - %d" % len(FAILS) if FAILS else "ok"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
