#!/usr/bin/env python3
"""A RAM disk ATTACHED AT BOOT reads what its own image holds
(KERN-MEGA-PLAN 14.112).

    python3 tests/rdboot.py [--adapter cga|herc]
    OS88_MEGA=1 python3 tests/rdboot.py         # mega-rdboot, on kern_mega

THE QUESTION IS kern_mega's. A driver SYSTEM.CFG wants is attached by
`drv_boot_x` in REAL mode, before `mega_enter`. RAMDISK.DRV, NET.DRV and
CDROM.DRV each banked `OSAPI_CPU_INFO`'s `CPU_F_PM` into a byte at attach,
and a byte banked there said "real mode" for the whole session. The flag
decides how the driver folds a buffer's offset into its segment: paragraph
by paragraph in real mode, and in protected mode not at all for an LDT
selector (its offset walks) - so the banked zero folded a SELECTOR as if
it were a paragraph, which is #GP or, with bit 2 still set, silently an
alias of some other descriptor's memory. HTTPFS.DRV had the same shape and
14.87 fixed it by ASKING per call; the three now do the same.

WHY THE SEED. A transfer a PACKAGE asks for never reaches the fold with a
selector in it: `drv_fs_call` bounces every allocator selector through
`dsk_secbuf` (`drv_fs_hi`), and the kernel's own buffers are identity
selectors folded to granules by `dskw_norm` first. The path that does reach
it is the driver's OWN image: `RAMSEED=1` lays the seed files out of
RAMDISK.DRV's code segment - an LDT selector on kern_mega - at Mount, and
each lives at an offset that is no multiple of anything. So the row is a
private `RAMSEED=1` tree, a SYSTEM.CFG asking for the RAM disk, the Drivers
page's Mount, and the seeded README.TXT opened in Note Pad: its length is
the assertion.

RED on the driver before 14.112 (on kern_mega only - on the real-mode
kernels the banked byte was right, which is why the row also runs there).
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88build                                       # noqa: E402
import os88marty                                       # noqa: E402
import os88ui                                          # noqa: E402
import rdcz                                            # noqa: E402

# drivers/ramdisk/ramdisk.asm's rd_d_readme, which Note Pad holds with the
# CRLFs folded (SPEC.md 27.11)
SEED = ("This volume has no sectors.\n"
        "Its contents come from RAMDISK.DRV answering\n"
        "questions about FILES - no BPB, no FAT, and\n"
        "no int 13h anywhere behind it.\n")
RD_CFGBIT = 2                       # kernel/driver.inc's drv_cfgbit


def rd_loaded(ui):
    return int.from_bytes(ui.m.read(ui.sym("drv_tab") + rdcz.RD_ROW
                                    * rdcz.DRVR_SZ + rdcz.DRVR_SEG, 2),
                          "little")


def mount(ui):
    """Control Panel -> Drivers -> the Ram Disk's page -> Mount: rdcz's
    sequence WITHOUT the tick, the driver being loaded already (a click on
    its row would UNload it)."""
    m, mo, c = ui.m, ui.mo, rdcz
    mo.menu(8, 8, 8, 40)
    os88marty.ui_done(m, limit=15.0)
    cp = ui.window("Control Panel")
    x0, y0 = cp.x + 1, cp.y + c.TITLE_H
    mo.click(x0 + 40, y0 + c.CP_I0Y + c.CP_IDRV * c.CP_IROWH + 7)  # Drivers
    os88marty.ui_done(m, limit=15.0)
    nst = m.read(ui.sym("cp_nst"), 1)[0]        # its page, then Mount
    mo.click(x0 + 40, y0 + c.CP_I0Y + nst * c.CP_IROWH + 7)
    os88marty.ui_done(m, limit=25.0)
    mo.click(x0 + c.CP_RX + c.RP_MNTX + c.RP_MNTW // 2,
             y0 + c.RP_B0Y + c.RP_BH // 2)
    os88marty.ui_done(m, limit=25.0)
    ui.close(cp)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", default="cga", choices=sorted(rdcz.MACHINE))
    a = ap.parse_args()
    mega = bool(os.environ.get("OS88_MEGA"))
    if mega:
        t = os88build.tree("RAMSEED=1", targets=("mega",))
        os.environ["OS88_MEGA_BUILD"] = t.dir
        src = t.img("mega.img")
    else:
        t = os88build.tree("RAMSEED=1").apply()
        src = t.img("os8088-360.img")
    with tempfile.TemporaryDirectory() as tmp:
        img = os.path.join(tmp, "rdboot.img")
        shutil.copyfile(src, img)
        cfg = os.path.join(tmp, "system.cfg")
        with open(cfg, "wb") as f:
            f.write(b"O88CFG\0\0" + (3).to_bytes(2, "little") + b"DW"
                    + bytes([1, 2]) + (1 << RD_CFGBIT).to_bytes(2, "little")
                    + b"\0\0")
        fat = os.path.join(os.path.dirname(__file__), "..", "tools",
                           "os88fat.py")
        subprocess.call([sys.executable, fat, "del", img, "SYSTEM.CFG"],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.check_call([sys.executable, fat, "add", img, cfg,
                               "SYSTEM.CFG"], stdout=subprocess.DEVNULL)
        with os88ui.boot(img, machine=rdcz.MACHINE[a.adapter],
                         quick=True) as ui:
            if not rd_loaded(ui):
                sys.exit("rdboot: FAIL: SYSTEM.CFG asked for RAMDISK.DRV "
                         "and it is not loaded - the row would assert "
                         "nothing about a driver attached at boot")
            print("rdboot: RAMDISK.DRV attached at boot%s"
                  % (", before mega_enter" if mega else ""))
            mount(ui)
            ui.open_drive("D")
            got = rdcz.note_len(ui)
    want = len(SEED)
    if got != want:
        print("rdboot: FAIL: the seeded README.TXT reached Note Pad as %d "
              "bytes and the seed is %d. The seed is copied out of the "
              "driver's own image at Mount, and a driver that banked "
              "CPU_F_PM at a real-mode attach folds that SELECTOR as a "
              "paragraph (KERN-MEGA-PLAN 14.112)" % (got, want))
        return 1
    print("rdboot: ok - the seeded README.TXT is %d bytes, as laid" % got)
    return 0


if __name__ == "__main__":
    sys.exit(main())
