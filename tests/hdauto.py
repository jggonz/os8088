#!/usr/bin/env python3
"""The hard disk loads itself, and a default never nags or lingers (SPEC.md
51.3.2), and a row nobody answered stays the machine's (SPEC.md 51.5.4).

    make && python3 tests/hdauto.py

`drv_sniff` asks the BIOS whether it knows a fixed disk (HDD.DRV's own rung 0
test) and, on a hit, wants the driver BY THE MACHINE'S CHOICE (DRVR_ANS = 0).
The boot then loads it, and drops it again if READY answers DRVE_NOVOL.
SYSTEM.CFG's 'DA' key says which rows the USER answered; only those take the
file's want. Every leg reads drv_tab, dsk_vtab and [drv_nerr] out of the
guest rather than looking at the screen:

  A  a FORMATTED disk, no SYSTEM.CFG: loaded, unanswered, a DRVC_DISK volume
     mounted, no boot notice.
  B  a BLANK disk (LBA 0 zeroed): loaded and taken down again - SEG 0, want
     0, DRVE_NOVOL in the row, no notice, no volume.
  C  NO disk: the row never tried - want 0, error 0, segment 0.
  D  a SYSTEM.CFG saved by closing the Control Panel on the machine with no
     disk, booted on the machine WITH one: the driver LOADS. The file has
     the row unanswered (DA bit 1 clear) and its DW bit clear - which is
     exactly the file that kept the disk off for ever before 51.5.4.
  E  the user UNTICKS the autoloaded row on the Drivers page (a real click):
     DRVR_ANS 1, and the file it saves (DA bit 1 set, DW bit 1 clear) keeps
     the driver off on the next boot - never attempted, error 0.
  F  an OLD SYSTEM.CFG with no 'DA' key, RAM disk on and the hard disk off:
     the RAM disk is an answer (loaded, DRVR_ANS 1) and the hard disk is the
     machine's (loaded, DRVR_ANS 0).

BROKEN ON PURPOSE, measured: take the DRVR_ANS mask out of want_set and D
goes red (the disk-less file switches the disk off); take drv_boot's unload
out and B goes red (the driver stays up with nothing mounted).

IT IS MARTY'S: os8088_xt_hdd is an 8088 behind an XT-IDE option ROM, which is
rung 0 - the only rung the sniff asks about - and os8088_5150_cga_gla is the
same class of machine with no hard disk at all.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import dispcp                                               # noqa: E402
import os88flush                                            # noqa: E402
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402

S = os88sym.linear
HDD_M = "os8088_xt_hdd"
NODISK_M = "os8088_5150_cga_gla"
IMG = os.path.join(ROOT, "build", "os8088-360.img")
APPS = os.path.join(ROOT, "build", "apps360.img")
VHD = os.path.join(ROOT, "build", "martypc", "run", "media", "hdds",
                   "default_xtide.vhd")

DRVR_SIZE, DRVR_SEG, DRVR_WANT, DRVR_ERR, DRVR_ANS = 16, 2, 13, 14, 15
HDD_ROW, RAM_ROW = 1, 3                 # drv_tab rows (kern_big)
BIT_HDD, BIT_RAM = 1, 2                 # drv_cfgbit
DRVE_NOVOL = 8
DVK_DRV, DRVC_DISK, DV_SIZE, DVOL_MAX = 1, 2, 16, 8
TITLE_H = 18                            # dispcp's / cpnames.py's
CP_IDRV, CP_DBY1, CP_DROWH, CP_RX = 2, 20, 26, 96

_fail = []


def check(ok, what, got=None):
    print("%-4s %s%s" % ("ok" if ok else "FAIL", what,
                         "" if ok or got is None else "   (got %s)" % (got,)))
    if not ok:
        _fail.append(what)


def row(m, r):
    b = m.read(S("drv_tab") + r * DRVR_SIZE, DRVR_SIZE)
    return {"seg": b[DRVR_SEG] | b[DRVR_SEG + 1] << 8, "want": b[DRVR_WANT],
            "err": b[DRVR_ERR], "ans": b[DRVR_ANS]}


def disk_vols(m):
    v = m.read(S("dsk_vtab"), DV_SIZE * DVOL_MAX)
    return [i for i in range(DVOL_MAX)
            if v[i * DV_SIZE] == DVK_DRV and v[i * DV_SIZE + 3] == DRVC_DISK]


def nerr(m):
    return m.read(S("drv_nerr"), 1)[0]


def boot(img, machine, vhd=None):
    extra = ["--mount", "hd:0:" + vhd] if vhd else ()
    m = os88marty.launch(img, apps=APPS, machine=machine, boot=False,
                         extra=extra)
    m.run()
    os88marty.settle(m, gate=os88marty.desktop_up)
    return m


def cfg_keys(data):
    """SYSTEM.CFG's records, {key: (ver, bytes)}, by an independent walk."""
    out, i = {}, 10
    if data[:6] != b"O88CFG":
        return out
    while i + 4 <= len(data):
        key = data[i:i + 2]
        if key == b"\0\0":
            break
        n = data[i + 3]
        out[key.decode("latin-1")] = (data[i + 2], data[i + 4:i + 4 + n])
        i += 4 + n
    return out


def word(b):
    return b[0] | b[1] << 8


def save_cfg(m, out, poke=True):
    """Close the panel so it writes SYSTEM.CFG, and keep the floppy."""
    f = os88flush.Flush(marty=m)
    if poke:
        m.write(S("cp_wdirty"), b"\x01")
    dispcp.close_panel(m, os88mouse.Mouse(marty=m), S, os88marty.settle)
    if m.read(S("cp_dsave"), 1)[0]:
        _fail.append("cp_cfg_save refused")
    data = f.volume(0).read("SYSTEM.CFG")
    f.save(0, out)
    return cfg_keys(data)


def main():
    tmp = tempfile.mkdtemp(prefix="hdauto-")
    try:
        blank = os.path.join(tmp, "blank.vhd")
        shutil.copyfile(VHD, blank)
        with open(blank, "r+b") as f:
            f.write(bytes(512))         # no MBR, so no partition to mount

        # --- A, and E's click: a formatted disk, nothing configured ----------
        print("-- A: formatted disk, no SYSTEM.CFG")
        with boot(IMG, HDD_M) as m:
            r = row(m, HDD_ROW)
            check(r["seg"] != 0, "A: HDD.DRV is loaded", r)
            check(r["ans"] == 0, "A: ...by the machine's choice (DRVR_ANS 0)", r)
            check(r["err"] == 0, "A: ...and READY mounted something", r)
            check(len(disk_vols(m)) >= 1, "A: a DRVC_DISK volume is mounted",
                  disk_vols(m))
            check(nerr(m) == 0, "A: no boot notice", nerr(m))

            print("-- E: untick it on the Drivers page")
            mo = os88mouse.Mouse(marty=m)
            dispcp.open_panel(m, mo, S, os88marty.settle, page=CP_IDRV)
            wx, wy = dispcp._cp_win(m, S)
            mo.click(wx + 1 + CP_RX + 40, wy + TITLE_H + 1 + CP_DBY1
                     + HDD_ROW * CP_DROWH + CP_DROWH // 2)
            os88marty.until(m, lambda mm: row(mm, HDD_ROW)["seg"] == 0,
                            "the click to unload HDD.DRV", guest=60.0)
            os88marty.settle(m)
            r = row(m, HDD_ROW)
            check(r["ans"] == 1 and r["want"] == 0,
                  "E: the click is an answer (DRVR_ANS 1, want 0)", r)
            keys = save_cfg(m, os.path.join(tmp, "off.img"), poke=False)
            da, dw = keys.get("DA"), keys.get("DW")
            check(da is not None and word(da[1]) >> BIT_HDD & 1 == 1,
                  "E: the file records the hard disk ANSWERED", da)
            check(dw is not None and word(dw[1]) >> BIT_HDD & 1 == 0,
                  "E: ...and not wanted", dw)

        # --- B: a blank disk -------------------------------------------------
        print("-- B: blank disk, no SYSTEM.CFG")
        with boot(IMG, HDD_M, blank) as m:
            r = row(m, HDD_ROW)
            check(r["seg"] == 0, "B: HDD.DRV was taken back down", r)
            check(r["want"] == 0 and r["err"] == DRVE_NOVOL,
                  "B: want 0, DRVE_NOVOL in the row", r)
            check(not disk_vols(m), "B: no DRVC_DISK volume", disk_vols(m))
            check(nerr(m) == 0, "B: no boot notice", nerr(m))

        # --- C, and D's file: no disk at all ----------------------------------
        print("-- C: no hard disk")
        with boot(IMG, NODISK_M) as m:
            r = row(m, HDD_ROW)
            check(r["seg"] == 0 and r["want"] == 0 and r["err"] == 0,
                  "C: the row never tried", r)
            print("-- D: save SYSTEM.CFG here")
            dispcp.open_panel(m, os88mouse.Mouse(marty=m), S,
                              os88marty.settle, page=CP_IDRV)
            keys = save_cfg(m, os.path.join(tmp, "nodisk.img"))
            da, dw = keys.get("DA"), keys.get("DW")
            check(da is not None and word(da[1]) >> BIT_HDD & 1 == 0,
                  "D: the disk-less file leaves the hard disk UNANSWERED", da)
            check(dw is not None and word(dw[1]) >> BIT_HDD & 1 == 0,
                  "D: ...with its DW bit clear (the file that used to keep it "
                  "off)", dw)

        print("-- D: that file, on the machine with a disk")
        with boot(os.path.join(tmp, "nodisk.img"), HDD_M) as m:
            r = row(m, HDD_ROW)
            check(r["seg"] != 0 and r["ans"] == 0,
                  "D: HDD.DRV loads anyway, still the machine's", r)

        print("-- E: the unticked file, on the next boot")
        with boot(os.path.join(tmp, "off.img"), HDD_M) as m:
            r = row(m, HDD_ROW)
            check(r["seg"] == 0 and r["want"] == 0 and r["err"] == 0
                  and r["ans"] == 1, "E: the answer holds - never attempted",
                  r)

        # --- F: an old file, no 'DA' -------------------------------------------
        print("-- F: an old SYSTEM.CFG: RAM disk on, hard disk off, no DA")
        old = os.path.join(tmp, "old.img")
        shutil.copyfile(IMG, old)
        cfg = os.path.join(tmp, "SYSTEM.CFG")
        with open(cfg, "wb") as f:
            f.write(b"O88CFG\0\0" + struct.pack("<H", 3) + b"DW"
                    + bytes([1, 2]) + struct.pack("<H", 1 << BIT_RAM)
                    + b"\0\0")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                        "os88fat.py"), "add", old, cfg, "SYSTEM.CFG"],
                       check=True, capture_output=True)
        with boot(old, HDD_M) as m:
            rr, rh = row(m, RAM_ROW), row(m, HDD_ROW)
            check(rr["seg"] != 0 and rr["ans"] == 1,
                  "F: the old file's ON row is an answer (RAM disk up)", rr)
            check(rh["seg"] != 0 and rh["ans"] == 0,
                  "F: its OFF row is the machine's (hard disk up)", rh)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print("hdauto: %s" % ("FAILED: " + "; ".join(_fail) if _fail else
                          "ok - the disk loads itself and a default never "
                          "sticks"))
    return 1 if _fail else 0


if __name__ == "__main__":
    sys.exit(main())
