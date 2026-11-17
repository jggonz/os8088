#!/usr/bin/env python3
"""hdhiber - a hibernation RESUMES on a machine that boots from the image's own
fixed disk and has an HMA (SPEC.md 87.5; jggonz/os8088#370).

    make && python3 tests/hdhiber.py

The release of 2026-11-16 refused every such resume: `Hibernation: other
build`, both files deleted, a fresh desktop. Nothing caught it because no row
had this machine. tests/hmahiber.py boots from the FLOPPY with the image on
HDD.DRV's volume, so the probe before the drivers (P1) never finds a pointer
and the one after them (P2) decides alone; every MartyPC hibernate row is an
8088, with no HMA at all. This is the live USB's shape: the boot volume IS the
image's volume, so P1 meets the pointer first.

What went wrong (two changes of 2026-11-11, each right alone): P1 runs before
XMEM.DRV has put `.cold` in the HMA, so it cannot judge an HMA image's head
and DEFERS - correctly - after reading HIBER.DRV. The drop that follows KEEPS
the image under MOD_CACHE, so its mod_tab row is still non-zero when drv_boot's
pass 1 attaches XMEM.DRV - and COLDHMA.SYS refuses to move `.cold` while any
module row is (boot/osrom.asm `.held`). `.cold` stayed in RAM; P2 compared an
image whose head says FF against a boot that says 08, and refused it.
hb_probe_x now evicts the kept image after P1 (kernel/hiber.inc).

The fixture: a FAT16 fixed disk holding the kernel and the three files the
path needs - XMEM.DRV, COLDHMA.SYS and HIBER.DRV - booted from C: with an
empty A:, under QEMU 9 at -m 128.

  0. premise: `.cold` runs from the HMA on the first boot ([api_coldseg] FFFF)
     - without it the row would prove nothing about the HMA;
  1. Hibernate... from the System menu, Enter: text mode, both files on C:;
  2. QEMU killed (the power-off), a fresh QEMU boots C: and RESUMES:
     [hb_resumes] 1, with no `Hibernation: other build` toast;
  3. ...with `.cold` in the HMA again - the condition the image needs;
  4. both files are gone from C: (read on the host).

RED, verified: with the eviction hunk in hb_probe_x reverted, 2 fails -
[hb_resumes] 0, toast `Hibernation: other build` - and the boot's
[api_coldseg] reads 0842 (step 3 fails too).

WHY QEMU: docs/TESTING.md's closed list, entry 1 - the HMA is a 286-and-up
place and every MartyPC machine is an 8088. NEEDS QEMU 9 for hmahiber's
reason: the resume stub's stack is in VGA memory (SPEC.md 87.5).
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88qemu    # noqa: E402
import os88sym     # noqa: E402
import hmacold     # noqa: E402
import hmahiber    # noqa: E402
import vidxms      # noqa: E402
import xmcheck     # noqa: E402

S = os88sym.linear
B = os.path.join(ROOT, "build")
NEED = ("kernel.sys", "mbr.bin", "boothd.bin", "xmem.drv", "coldhma.sys",
        "hiber.drv")
BOTH = {"HIBERNAT.PTR", "HIBERNAT.IMG"}


def u16(b):
    return b[0] | b[1] << 8


def hd_image(tmp):
    hd = os.path.join(tmp, "hd.img")
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", hd, "--hdd",
                    "--mbr", os.path.join(B, "mbr.bin"),
                    "--boot", os.path.join(B, "boothd.bin"),
                    "--kernel", os.path.join(B, "kernel.sys")]
                   + [os.path.join(B, n) for n in NEED[3:]],
                   check=True, capture_output=True)
    return hd


def boot(tmp, hd):
    # vidxms.Q boots `-boot a`; an A: with no boot signature falls through
    # to the fixed disk, so C: is the boot volume - the shape under test
    blank = [os.path.join(tmp, "blank%d.img" % i) for i in (0, 1)]
    for b in blank:
        with open(b, "wb") as f:
            f.write(bytes(1474560))
    q = vidxms.Q(tmp, blank[0], blank[1], 128,
                 ["-drive", "file=%s,format=raw,if=ide,index=0,media=disk"
                  % hd])
    xmcheck.wait_desktop(q.sock, "hdhiber")
    return q


def main():
    os.chdir(ROOT)
    for n in NEED:
        if not os.path.exists(os.path.join(B, n)):
            sys.exit("hdhiber: no build/%s - run `make`" % n)
    fails = []

    def want(cond, what):
        print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
        if not cond:
            fails.append(what)

    with tempfile.TemporaryDirectory(dir=B) as tmp:
        hd = hd_image(tmp)
        q = boot(tmp, hd)
        os88qemu.ui_done(q, S, cap=1.0)
        if u16(q.read(S("api_coldseg"), 2)) != 0xFFFF:
            q.close()
            sys.exit("hdhiber: `.cold` is not in the HMA on the first boot - "
                     "the row's premise (0)")
        print("   ok    0. `.cold` runs from the HMA")
        ui = hmacold.QUI(q, mouse=hmacold.QMouse(q), sym=S, verbose=False)
        ui.menu_pick("Apple", "Hibernate...")
        ui.wait_window("Hibernate")
        os88qemu.ui_done(q, S, cap=1.0)
        xmcheck.qmp(q.sock, "sendkey ret")
        text = os88qemu.acted(q, lambda: q.read(0x449, 1)[0] in (3, 7),
                              secs=120, what="the hibernated sentence")
        want(text and hmahiber.host_wait(lambda: BOTH <= hmahiber.on_c(hd)),
             "1. the hibernate reached text mode and both files are on C:")
        q.close()                               # the power-off
        if fails:
            sys.exit("hdhiber: %d failed" % len(fails))

        q = boot(tmp, hd)
        ok = os88qemu.acted(q, lambda: u16(q.read(S("hb_resumes"), 2)) == 1,
                            secs=120, what="[hb_resumes] to reach 1")
        if ok:
            os88qemu.ui_done(q, S, cap=1.0)
        ui = hmacold.QUI(q, mouse=hmacold.QMouse(q), sym=S, verbose=False)
        toast = ui.toast()[0]
        want(ok and "other build" not in toast,
             "2. C: booted and RESUMED: [hb_resumes] %d, toast %r"
             % (u16(q.read(S("hb_resumes"), 2)), toast))
        cold = u16(q.read(S("api_coldseg"), 2))
        want(cold == 0xFFFF, "3. ...with `.cold` in the HMA: [api_coldseg] "
             "%04X" % cold)
        q.close()
        gone = hmahiber.host_wait(lambda: not (BOTH & hmahiber.on_c(hd)))
        want(gone, "4. both files are gone from C:: %s"
             % sorted(BOTH & hmahiber.on_c(hd)))

    if fails:
        sys.exit("hdhiber: %d failed" % len(fails))
    print("hdhiber: ok")


if __name__ == "__main__":
    main()
