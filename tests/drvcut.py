#!/usr/bin/env python3
"""A DRIVER volume's sector across a 64KB page is staged, not refused
(SPEC.md 18.91.4.1).

DOS loads an .EXE with READ_AT straight into its arena at PSP+16 (96.8.2),
which is wherever the environment left it - so on `C:` mounted by HDD.DRV
(an XT booted off a floppy, the driver's int 13h rung 0) a load module over
64KB has a sector ACROSS a DMA page at an unaligned address. The driver
cannot make a run of it and refuses (status 04h), and the program read as
"It could not be read." - the owner's TREKRUN.EXE on an 86Box pc5150 with an
ST11M. `dsk_xfer`'s DVK_DRV arm now cuts the run there and bounces that one
sector through `dsk_secbuf`.

The row builds DRVCUT.EXE - tests/dostrap/bigsum.asm and 72,000 seeded bytes
- onto an XT-IDE disk, boots the 360KB system floppy on os8088_xt_hdd (so
HDD.DRV mounts C:), double-clicks it and asserts the checksum the program
computes over its OWN load module equals the host's. It also asserts the
load buffer really was unaligned AND that the module spans a page: a run in
which it happened to be 512-aligned would test nothing, and says so.

VERIFIED TO FAIL: with `call .drvcut` put back to `call drv_blk_call_x` the
window prints "C:\\DRVCUT.EXE: It could not be read." and the row goes red
("the program never printed its answer").
"""
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build                                               # noqa: E402
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = os88build.at("build/os8088-360.img")
NEED = ("build/kernel.sys", "build/boothd.bin", "build/mbr.bin",
        "build/hiber.drv", "build/ctrl.drv", "build/hdd.drv")
WORDS = 36000                       # 72,000 bytes of data


def fail(msg):
    print("drvcut: FAIL: %s" % msg)
    sys.exit(1)


def bigsum(data):
    s = 0
    for i in range(0, len(data), 2):
        w = data[i] | data[i + 1] << 8
        s = ((s << 1) | (s >> 15)) & 0xFFFF
        s = (s + w) & 0xFFFF
    return s


def make_exe(d):
    code = os.path.join(d, "bigsum.bin")
    subprocess.check_call(["nasm", "-f", "bin", "-w+error",
                           "-DBIGSUM_W=%d" % WORDS, "-o", code,
                           os.path.join(ROOT, "tests/dostrap/bigsum.asm")])
    body = open(code, "rb").read()
    assert len(body) == 0x100
    data = bytes(random.Random(18914).randrange(256) for _ in range(2 * WORDS))
    mod = body + data
    total = 32 + len(mod)
    paras = (len(mod) + 15) // 16
    hdr = struct.pack("<2s13H", b"MZ", total % 512, (total + 511) // 512, 0,
                      2, 0x20, 0xFFFF, paras, 0x200, 0, 0, 0, 0x1C, 0)
    exe = os.path.join(d, "DRVCUT.EXE")
    with open(exe, "wb") as f:
        f.write(hdr.ljust(32, b"\0") + mod)
    return exe, bigsum(data), len(mod)


def main():
    for p in (SYS,) + NEED:
        if not os.path.exists(os.path.join(ROOT, p)):
            fail("%s is missing - `make`" % p)
    d = tempfile.mkdtemp(prefix="drvcut-")
    exe, want, size = make_exe(d)
    raw, vhd = os.path.join(d, "hd.img"), os.path.join(d, "hd.vhd")
    subprocess.check_call(
        ["python3", "tools/os88disk.py", "-o", raw, "--hdd",
         "--geometry", "615/4/17", "--mbr", "build/mbr.bin",
         "--boot", "build/boothd.bin", "--kernel", "build/kernel.sys",
         "build/hiber.drv", "build/ctrl.drv", "build/hdd.drv", exe], cwd=ROOT)
    subprocess.check_call(
        ["python3", "tools/os88hdd.py", "--wrap", raw, "--cyls", "615",
         "--heads", "4", "--spt", "17", "--out", vhd], cwd=ROOT)
    rows = []
    with os88ui.boot(SYS, machine="os8088_xt_hdd", quick=True,
                     extra=["--mount", "hd:0:" + vhd]) as ui:
        m = ui.m
        ui.open_drive("C")
        ui.open("DRVCUT.EXE", expect=None)

        def done(mm):
            rows[:] = [r.rstrip() for r in (mm.screen() or [])]
            t = "\n".join(rows)
            return "DRVCUT SUM=" in t or "could not be read" in t
        try:
            os88marty.until(m, done, "DRVCUT's answer", poll=0.25, guest=60.0)
        except os88marty.MartyError:
            pass
        text = "\n".join(rows)
        wd, ht, fb = m.fbuf()
        os88marty.write_png_rgb(os.path.join(ROOT, "build/drvcut.png"),
                                wd, ht, fb)
        m.type_text("x")
    shutil.rmtree(d, ignore_errors=True)
    print("drvcut: the screen:")
    for r in rows:
        if r.strip():
            print("   | %s" % r)
    if "could not be read" in text:
        fail("DOS could not read DRVCUT.EXE off the HDD.DRV volume - the "
             "sector across the 64KB page was refused (SPEC.md 18.91.4.1)")
    mt = re.search(r"DRVCUT SUM=([0-9A-F]{4}) PSP=([0-9A-F]{4})", text)
    if not mt:
        fail("the program never printed its answer - it never ran. The "
             "window (build/drvcut.png) is where DOS says why; before "
             "18.91.4.1 it said 'It could not be read.'")
    got, psp = int(mt.group(1), 16), int(mt.group(2), 16)
    lin = (psp + 16) * 16
    print("drvcut: sum %04X (want %04X), load buffer at %05X, %d bytes"
          % (got, want, lin, size))
    if lin % 512 == 0:
        fail("the load buffer was 512-aligned (%05X): this run tested "
             "nothing - the arena moved, so move the row's fixture" % lin)
    if lin // 0x10000 == (lin + size - 1) // 0x10000:
        fail("the load module did not cross a 64KB page")
    if got != want:
        fail("the program's own bytes sum %04X where the file's sum %04X: "
             "it was loaded WRONG" % (got, want))
    print("drvcut: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
