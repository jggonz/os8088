#!/usr/bin/env python3
"""A COMPRESSED DRIVER loads, expands and answers (docs/plans/O88-COMPRESSION-PLAN.md 12.6).

The subject is RAMDISK.DRV, on a system disk otherwise identical to the
shipped 360KB one. It is the right one because it has BOTH halves of wave 3:
a 2,416-byte bss that `drv_bss` re-makes, and a body the transparent read
unpacks - a compressed driver is a 'CZ' file since SPEC.md 20.13.3.1, expanded
into the claim drv_load cut from the directory hint - so one file exercises
the whole path.

Three assertions, and the middle one is what a working driver alone would not
prove:

  * it ATTACHES - the row has a segment, so drv_load got through the
    hint-sized claim, the expanding read, drv_check, drv_bss and the driver's
    own attach;
  * its image in memory is byte-for-byte the UNCOMPRESSED driver, with the
    stripped zeros back. A decoder that fumbled the last run would still
    attach, and this is what catches it;
  * it ANSWERS - the DRVCALL package's three probes, which is
    tests/drvcall.py's own assertion re-run against a driver that arrived
    compressed.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88drv                                         # noqa: E402
import os88build                                       # noqa: E402
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88sym                                         # noqa: E402
from os88sym import KERNEL_SEG                         # noqa: E402
import dispcp                                          # noqa: E402
import drvcall                                         # noqa: E402
from os88fixture import need                           # noqa: E402

S = os88sym.linear
MACHINE = {"cga": "os8088_5150_cga_gla", "herc": "os8088_5150_herc_gla"}
# The Control Panel geometry and the Ram Disk's row are tests/drvcall.py's,
# imported rather than copied: this row loads the SAME driver through the SAME
# page, and two files disagreeing about where that row is would be a test
# failure that means nothing about the kernel.
DRVR_SZ, DRVR_SEG = drvcall.DRVR_SZ, drvcall.DRVR_SEG
DRVC_FILE = 5                       # SPEC.md 51.2's class, RAMDISK.DRV's
RD_ROW = drvcall.RD_ROW


def say(*a):
    print(*a, flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", default="cga", choices=sorted(MACHINE))
    a = ap.parse_args()
    # THE PATHS AND NOT THE TARGET NAME: `Row(wants=...)` carries
    # what `make <path>` produces, and the runner builds it before
    # any row starts - a phony name never satisfies the existence
    # check that follows. Same build, and sayable.
    need("build/lzdrv360.img", "build/drvcall360.img")              # `all` builds nothing under tests/

    # THE SHIPPED DRIVER IS COMPRESSED TOO NOW (SPEC.md 20.13.5), so the
    # reference this whole row compares against has to be unwrapped: what the
    # guest holds after the read is the IMAGE, and build/ramdisk.drv is a
    # FILE. Without this the row reports 4,832 differing bytes on a kernel
    # that expanded perfectly, which reads exactly like a broken decoder.
    # `at` on every host-side read: under a frozen run these live in the
    # run's own tree and not in build/ (docs/plans/SOAK-PARALLEL.md 14.2), and a
    # comparison that takes one build's driver and boots another's is the
    # failure that reads as a broken decoder.
    plain = os88drv.image_unwrap(
        open(os88build.at("build/ramdisk.drv"), "rb").read())
    packed = open(os88build.at("build/lzd/ramdisk.drv"), "rb").read()
    say("lzdrv: RAMDISK.DRV %d bytes plain, %d compressed (%.1f%%), "
        "+%d bss" % (len(plain), len(packed), 100.0 * len(packed) / len(plain),
                     plain[31] * 16))

    fails = []
    with os88marty.launch("build/lzdrv360.img",
                          apps="build/drvcall360.img",
                          machine=MACHINE[a.adapter]) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        # 1. tick the Ram Disk on, exactly as tests/drvcall.py does - a
        #    driver is not attached at boot unless SYSTEM.CFG asks for it
        mo.menu(8, 8, 8, 40)                        # chip menu -> Control Panel
        os88marty.settle(m)
        cp = None
        for w in dispcp.win_list(m, S):
            x, y, ww, hh = dispcp.win_rect(m, S, w)
            if ww >= 280 and hh >= 100:
                cp = (w, x, y)
        if cp is None:
            sys.exit("lzdrv: no Control Panel window")
        _, cx, cy = cp
        x0, y0 = cx + 1, cy + 18
        mo.click(x0 + 40, y0 + drvcall.CP_I0Y
                 + drvcall.CP_IDRV * drvcall.CP_IROWH + 7)
        os88marty.settle(m)
        # **THE IMAGE IS READ BEFORE THE DRIVER RUNS**, and that is a
        # breakpoint rather than a wait. This row compared the expanded image
        # against the file AFTER the attach, and got away with it only
        # because the stripping happened to put every mutable byte past
        # `image` - its own note below records the first version failing on
        # "96 bytes at offset 6,406, every one of them the Ram Disk's own
        # state". SPEC.md 51.1.2 moved that boundary: the strip now stops at
        # the KB rung the footprint needs, so part of the driver's working
        # memory is inside `image` again and the comparison came back with 75
        # bytes differing at 6,408 - the same region, about a decoder that
        # had expanded every byte correctly.
        #
        # Stopping at `drv_attach` is the assertion this row was always
        # making: `drv_load` has read, checked, expanded and zeroed by then,
        # and NOTHING has entered the driver. What the file says is what
        # memory must hold, all of it, with no region to carve out.
        m.bp_exec("drv_attach")
        mo.click(x0 + drvcall.CP_RX + 40,
                 y0 + drvcall.CP_DBY1 + RD_ROW * drvcall.CP_DROWH
                 + drvcall.CP_DROWH // 2)
        # THE BYTES, not the segment. Banking the segment here and reading it
        # after `m.run()` reads exactly what the old code read - a driver that
        # has attached and started using its own memory - and is the same
        # failure with an extra step in front of it.
        loaded = None
        if m.wait_stop(limit=120.0):
            r = m.regs()
            sg = int.from_bytes(
                m.read(KERNEL_SEG * 16 + r["bx"] + DRVR_SEG, 2), "little")
            if sg:
                loaded = bytes(m.readseg(sg, 0, len(plain)))
        m.bp_exec()
        m.run()
        # WAITED FOR, not slept through - AND NOT ON THE SEGMENT, which is
        # the trap this row taught. `drv_load` writes DRVR_SEG the moment
        # mem_claim_hi_x answers, BEFORE the file is read, checked, expanded,
        # its bss re-made and drv_attach far-called (kernel/driver.inc); so a
        # non-zero segment means "a claim was made", not "the driver is up",
        # and waiting on it returns three quarters of the way through a load.
        # Measured: the probes below then read `Ping: ..` and
        # `Upcase: hello world` - a driver that answered nothing - which is
        # exactly what a broken decoder looks like.
        #
        # `drv_owner` for the CLASS is the signal, because drv_publish is
        # called from drv_attach and nothing else writes it: DRVC_FILE is 5
        # and class 1 is index 0, so this is the row's own address appearing
        # in slot 4 (SPEC.md 51.2.1).
        row = S("drv_tab") + RD_ROW * DRVR_SZ
        # ...and drv_owner holds a near OFFSET (BX inside the kernel segment),
        # where S() answers a linear address - so the comparison is against
        # the offset and not against what `row` is used for two lines down.
        want = (row - KERNEL_SEG * 16) & 0xFFFF

        def published(mm):
            return int.from_bytes(
                mm.read(S("drv_owner") + (DRVC_FILE - 1) * 2, 2),
                "little") == want
        try:
            os88marty.until(m, published, "RAMDISK.DRV to attach and publish",
                            poll=0.1, guest=30.0)
        except os88marty.MartyError:
            pass
        os88marty.settle(m)
        seg = int.from_bytes(m.read(row + DRVR_SEG, 2), "little")
        mo.click(cx + 8, cy + 9)                    # close the panel (31.8)
        os88marty.settle(m)
        if not seg:
            fails.append("RAMDISK.DRV is not in drv_tab with a segment - it "
                         "did not attach")
        else:
            say("  attached at %04X" % seg)
            # 2. did it expand to the right bytes?
            #
            # THE IMAGE ONLY. The bss cannot be compared against zeros here
            # and the reason is not a limitation of the test: by the time the
            # row has a segment the driver has ATTACHED, and its bss is its
            # working memory - the first version of this row failed on 96
            # bytes at offset 6,406, every one of them the Ram Disk's own
            # state. What stands in for it is below.
            if loaded is None:
                fails.append("drv_attach was never reached, so the image "
                             "could not be read before the driver ran")
            else:
                got = loaded
                if got == plain:
                    say("  %d bytes of image expanded EXACTLY (read at "
                        "drv_attach, before the driver ran)" % len(plain))
                else:
                    bad = [i for i in range(len(plain)) if got[i] != plain[i]]
                    fails.append("expanded WRONG: %d of %d image bytes "
                                 "differ, first at %d"
                                 % (len(bad), len(plain), bad[0]))

            # ...AND THERE IS NO DIRECT ASSERTION ON THE BSS'S CONTENTS,
            # which is worth stating rather than leaving as an omission. A
            # version of this row checked that the far end of the bss was
            # still zero, reasoning that an unzeroed one would hold whatever
            # the heap last had; it failed on a single 0xFF nine bytes in,
            # which is the attached Ram Disk's own state. Once the row has a
            # segment the driver has run, and nothing here can tell its data
            # from memory drv_bss missed. What stands in for it is the probes
            # below - a driver handed a bss full of floppy leftovers does not
            # answer them - and tests/unit/t_drvmem.py's host-side check that
            # the stripped file plus its bss IS the assembled image.
            bss = plain[31] * 16

            # 3. and the claim covers image + bss
            kb = int.from_bytes(
                m.read(S("drv_tab") + RD_ROW * DRVR_SZ + 8, 2), "little")
            if kb * 1024 < len(plain) + bss:
                fails.append("the claim is %d KB and the driver needs %d bytes"
                             % (kb, len(plain) + bss))

        # 3. does it answer? - drvcall's own three probes
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wins = dispcp.win_list(m, S)
        if not wins:
            fails.append("no Disk window after double-clicking B:")
        else:
            wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
            dispcp.open_named(m, mo, S, os88marty.settle, wx, wy,
                              "DRVCALL.O88")
            w2 = dispcp.win_list(m, S)
            if len(w2) <= len(wins):
                fails.append("DRVCALL.O88 did not open")
            else:
                rec = m.read(S("wm_wins") + w2[-1] * dispcp.WIN_SIZE,
                             dispcp.WIN_SIZE)
                pseg = rec[22] | (rec[23] << 8)
                n = os.path.getsize(os88build.at("build/drvcall.bin"))

                # RE-PROBE, AND CONFIRM IT RAN. dc_probe runs from dc_paint,
                # and DRVCALL's FIRST paint does not reach the driver - the
                # three lines are still the image's own 'Ping: ..' after the
                # window is up and the screen still. tests/drvcall.py has
                # always clicked the window to force a second probe; this row
                # did not, and read whatever was there. It passed by luck: a
                # second paint arriving before the read is a race, and it is
                # LOST under load - two emulators on this box turned it into
                # `Ping: ..` on the shipped tree, which reads exactly like a
                # driver that failed to expand.
                dx, dy, dw, dh = dispcp.win_rect(m, S, w2[-1])
                mo.click(dx + dw // 2, dy + dh - 8)
                try:
                    os88marty.until(
                        m, lambda mm: b"Ping: .." not in bytes(
                            mm.readseg(pseg, 0, n)),
                        "DRVCALL's probe to run", poll=0.1, guest=20.0)
                except os88marty.MartyError:
                    pass
                os88marty.settle(m)
                raw = m.readseg(pseg, 0, n)
                for tag, want_txt in ((b"Ping: ", b"Ping: DR"),
                                      (b"Upcase: ", b"Upcase: HELLO WORLD")):
                    i = raw.find(tag)
                    got_txt = bytes(raw[i:i + len(want_txt)])
                    if got_txt == want_txt:
                        say("  %s" % got_txt.decode())
                    else:
                        fails.append("%r, want %r" % (got_txt, want_txt))

    for f in fails:
        say("  FAIL: " + f)
    say("lzdrv: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
