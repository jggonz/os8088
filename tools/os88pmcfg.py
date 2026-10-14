#!/usr/bin/env python3
"""The PicoMEM field disk's settings: SYSTEM.CFG and FTPD.CFG for pmsys720.

    python3 tools/os88pmcfg.py --syscfg OUT --ftpdcfg OUT --ftpd build/ftpd.o88

`make picomemtest`'s 720KB disk is the one the owner carries to a 286 with a
PicoMEM 1.x, round after round (docs/FIELD-NOTES.md 66). What it is tested
WITH rides it already; this writes the two files that make a round need no
clicking first:

  SYSTEM.CFG  SOUND, HDD, ETHER and USB MOUSE wanted at boot (SPEC.md 51.5,
              bits 0, 1, 4 and 6), and a desktop SHORTCUT to A:\\APPS\\FTPD.O88
              in SPEC.md 26.8.6's trailer - the same 128-byte row CTRL.DRV
              writes when a package is dragged onto the desktop, its picture
              the package's own embedded 16x16 icon (SPEC.md 20.2, 25).
  FTPD.CFG    FTPD's keyed settings (SPEC.md 77.12.1) with one record: Root,
              C:\\ (77.16.1), so the server starts on the hard disk.

The boot reader (kernel/desksc.inc, ovl_sc_load) looks for a trailer only in
a file LONGER than the settings buffer, CFG_FBUF; a file that big is what
CTRL.DRV writes, because its writer emits every key. This one carries a
single key, so the settings head is padded with zeros to HEAD bytes - a zero
is the records' own terminator, and the reader stops the head at the rows
the footer names. Nothing ships it: the shipped disks carry no SYSTEM.CFG.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88pkg  # noqa: E402

# SPEC.md 51.5 / kernel/driver.inc drv_cfgbit (kern_big)
BIT_SOUND, BIT_HDD, BIT_ETHER, BIT_USBM = 0, 1, 4, 6
# SPEC.md 26.8.1 / kernel/desksc.inc and desk.inc
SC_REC, SC_VER, DESK_SC0 = 128, 2, 9
SC_R_PATH, SC_PATHMAX, SC_R_CAP, SC_CAPMAX, SC_R_HDR, SC_R_BODY = \
    4, 45, 49, 13, 62, 64
DSL_PIN = 0x40
SC_KIND_PKG = 1
HEAD = 256          # past CFG_FBUF on every build (it is ~150), so the trailer
                    # is read; well under the reader's 4KB refusal
SHORTCUT_CELL = 5   # cell 0 is the top of the rightmost column; the drives
                    # flow into the first free cells, the Wire after them


def syscfg(icon, cell):
    want = (1 << BIT_SOUND) | (1 << BIT_HDD) | (1 << BIT_ETHER) | \
        (1 << BIT_USBM)
    head = b"O88CFG\0\0" + struct.pack("<H", 3) + \
        b"DW" + bytes([1, 2]) + struct.pack("<H", want) + b"\0\0"
    head = head.ljust(HEAD, b"\0")
    row = bytearray(SC_REC)
    row[0] = 0                                  # A:, the disk it boots from
    row[1] = DSL_PIN | cell                     # a shortcut is always placed
    row[2] = SC_KIND_PKG
    path = b"APPS\\FTPD.O88"
    cap = b"FTPD"                               # the header name (26.8.4)
    assert len(path) < SC_PATHMAX and len(cap) < SC_CAPMAX
    row[SC_R_PATH:SC_R_PATH + len(path)] = path
    row[SC_R_CAP:SC_R_CAP + len(cap)] = cap
    row[SC_R_HDR:SC_R_HDR + 2] = bytes([1, 16])
    row[SC_R_BODY:SC_R_BODY + 64] = icon
    pins = b"\xff" * DESK_SC0                   # no drive is placed
    return head + bytes(row) + pins + b"SC" + bytes([SC_VER, 1])


def ftpdcfg(root):
    r = root.encode("ascii")
    assert 0 < len(r) < 256
    return b"O88FTPD\0" + struct.pack("<H", 1) + \
        b"R" + bytes([len(r)]) + r + b"\0"


def ftpd_icon(path):
    image = os88pkg.image_unwrap(open(path, "rb").read())
    icon = image[32:96]                         # header 32, then the icon
    if not image[3] & 1 or len(icon) != 64 or not any(icon):
        sys.exit("os88pmcfg: %s carries no 16x16 icon" % path)
    return icon


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--syscfg", required=True)
    ap.add_argument("--ftpdcfg", required=True)
    ap.add_argument("--ftpd", required=True, help="FTPD.O88, for its icon")
    ap.add_argument("--root", default="C:\\")
    ap.add_argument("--cell", type=int, default=SHORTCUT_CELL)
    a = ap.parse_args()
    with open(a.syscfg, "wb") as f:
        f.write(syscfg(ftpd_icon(a.ftpd), a.cell))
    with open(a.ftpdcfg, "wb") as f:
        f.write(ftpdcfg(a.root))


if __name__ == "__main__":
    main()
