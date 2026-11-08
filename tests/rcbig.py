#!/usr/bin/env python3
"""RunCPM opens, reads and writes CP/M files past 64KB (SPEC.md 74.3.3).

    make runcpm && python3 tests/rcbig.py            kern_big, MartyPC's 5150 (720KB)
    OS88_MEGA=1 OS88_MEGA_EMU=86box python3 tests/rcbig.py    kern_mega

An open file past 65,535 bytes is ONE os88_mem_huge claim now, 512 records
a 64KB window. A disk of its own carries RUNCPM.O88 and, in A\\0, PIP.COM and
two of the master disk's files that were left off for the 64KB limit -
BDOS.ASM (67,301 bytes) and Z80ASM.PDF (134,899) - and at the A> prompt the
row types `pip copy.pdf=z80asm.pdf[o]` and `pip bdos2.asm=bdos.asm[o]`:
PIP reads each through BDOS 20 across its windows and writes the copy
through BDOS 21, which makes the copy's entry huge at record 511 and regrows
it on, and F_CLOSE writes it back whole. The floppy is read back on the
host: each copy must be its original byte for byte, then ^Z to its last
record's end (CP/M's files are records; RunCPM fills a partial last record
with 1Ah as upstream's _sys_readseq does).

RED (docs/WRITING-TESTS.md 1): the tree before 74.3.3 refused both opens
(`RunCPM: Z80ASM.PDF>64K`), PIP stopped with `NO FILE`, no copy was
written - measured when the row was written.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
os.chdir(ROOT)
import os88build                                        # noqa: E402
import os88fixture                                      # noqa: E402
import os88marty                                        # noqa: E402
import os88ui                                           # noqa: E402
from arcimport import fat_cat                           # noqa: E402
from cmemmove import pkg_syms, u16, pkg_seg             # noqa: E402

RC_ROWS, RC_COLS = 25, 80
RC_M_BLOCKED = 3                            # runcpm.c's rc_mode
A0 = "build/runcpm-disk/A/0"
FILES = [("Z80ASM.PDF", "COPY.PDF"), ("BDOS.ASM", "BDOS2.ASM")]


def main():
    mega = bool(os.environ.get("OS88_MEGA"))
    if mega:
        os.environ.setdefault("OS88_MEGA_EMU", "86box")
        os.environ.setdefault("OS88_MEGA_MEGS", "1.375")
    for p in ["build/runcpm.o88", "build/runcpm-src.stamp"]:
        os88fixture.need(p)             # (the stamp is what puts A\0's
                                        # files in build/runcpm-disk)
    RC = pkg_syms("apps/runcpm/runcpm.asm")
    td = tempfile.mkdtemp(prefix="rcbig-")
    fails = []
    try:
        disk = os.path.join(td, "b.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "720" if not mega else "1440",
                        "--deep-folders",
                        os88build.at("build/runcpm.o88"),
                        "A/0:" + A0 + "/PIP.COM"] +
                       ["A/0:" + A0 + "/" + f for f, _ in FILES],
                       check=True, capture_output=True)
        sysimg = "build/mega.img" if mega else "build/os8088-720.img"
        machine = None
        if mega:
            import megajw
            machine = megajw.MACHINE
        kw = {"machine": machine} if machine else {"machine":
                                                   "os8088_5150_herc_720_gla"}
        with os88ui.boot(os88build.at(sysimg), apps=disk, quick=not mega,
                         **kw) as ui:
            m, S = ui.m, ui.sym

            def lin(seg, off=0):
                return m.lin(seg, off) if mega or hasattr(m, "bochs") and \
                    m.bochs else seg * 16 + off

            def screen(seg):
                raw = m.read(lin(seg, RC["_rc_ch"]), RC_ROWS * RC_COLS)
                return [raw[r * RC_COLS:(r + 1) * RC_COLS].decode("latin-1")
                        for r in range(RC_ROWS)]

            def mode(seg):
                return u16(m.read(lin(seg, RC["_rc_mode"]), 2))

            def prompts():
                seg = pkg_seg(m, S, "RunCPM")[0]
                return sum(r.count("A>") for r in screen(seg)), seg

            ui.open_drive("B")
            rcw = ui.open("RUNCPM.O88")
            os88marty.until(m, lambda _: prompts()[0] >= 1 and
                            mode(prompts()[1]) == RC_M_BLOCKED,
                            "the CCP's A> prompt", poll=0.5, guest=240)
            for src, dst in FILES:
                n0, seg = prompts()
                ui.raise_window(rcw)
                m.type_text("pip %s=%s[o]\n" % (dst.lower(), src.lower()))
                try:
                    os88marty.until(m, lambda _: prompts()[0] > n0 and
                                    mode(prompts()[1]) == RC_M_BLOCKED,
                                    "PIP's copy of %s and a fresh A>" % src,
                                    poll=0.5, guest=600)
                except os88marty.MartyError as e:
                    fails.append(str(e))
                rows = [r.rstrip() for r in screen(prompts()[1]) if r.strip()]
                for r in rows[-3:]:
                    print("      |%s" % r)
            back = os.path.join(td, "back.img")
            os88marty.guest_sleep(m, 2)
            m.flush(1, back)
        for src, dst in FILES:
            orig = open(os.path.join(A0, src), "rb").read()
            got = fat_cat(back, "A/0/" + dst)
            if got is None:
                fails.append("no A\\0\\%s on the disk" % dst)
                continue
            pad = (-len(orig)) % 128
            ok = got[:len(orig)] == orig and got[len(orig):] == b"\x1a" * pad
            print("rcbig: %s -> %s: %d bytes (the original %d, then %d of "
                  "^Z) %s" % (src, dst, len(got), len(orig), pad,
                              "byte for byte" if ok else "DIFFERENT"))
            if not ok:
                at = next((i for i in range(min(len(got), len(orig)))
                           if got[i] != orig[i]), None)
                fails.append("%s is not %s (%d bytes, first difference at "
                             "%s)" % (dst, src, len(got), at))
    finally:
        shutil.rmtree(td, ignore_errors=True)
    for f in fails:
        print("rcbig: FAIL - " + f)
    print("rcbig: %s" % ("FAIL" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
