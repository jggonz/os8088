#!/usr/bin/env python3
"""Does the formatter offer the DRIVE's two sizes, whatever the read said?

    python3 tests/fmtpair.py

SPEC.md 18.96.4. A bulk-erased 1.44MB disk in a 1.44MB drive was offered
1200K, 720K and 1440K on three erasures: the probe took the first read that
succeeded, and a read of magnetic noise sometimes does. dskw_fmt_probe_x now
answers AL = the default row and AH = the other row the drive makes (0FFh for
none), from int 13h AH=08h's BL - the CMOS drive type - before CL, and the
read only picks which of the two comes first. Rows: 0 = 1.44M, 1 = 1.2M,
2 = 720K, 3 = 360K.

QEMU AND NOT MARTYPC, by docs/TESTING.md's closed list, entry 1: the pair is
an AT-class ROM's answer - every MartyPC machine runs an XT ROM that refuses
AH=08h for a floppy, which is the 9-sector branch fmtreach and fmtlow already
cover. QEMU's SeaBIOS answers it, and its "auto" drive type follows the image
in B:, so one image a boot gives each drive:

  arm      B: holds                  drive   read   want AX
  blank    1.44MB of zeros           1.44M   18     0x0200  1.44M, else 720K
  dd144    the 720KB apps disk       1.44M   9      0x0002  720K, else 1.44M
  hd120    the 1.2MB apps disk       1.2M    15     0x0301  1.2M, else 360K
  dd120    the 360KB apps disk       1.2M    9      0x0103  360K, else 1.2M

QEMU cannot make noise succeed, so the field's random read is not
reproduced: what is asserted is the property that makes it harmless - every
arm offers BOTH of its drive's sizes, and a 9-sector read in an HD drive puts
the 9-sector one first. The probe is called directly (tests/colorpresets_qemu
.py's debugger trampoline) with [disk_drive] = B:; it reads only.

VERIFIED RED (2026-10-18): AH forced to 0 at .got2 - what the pre-18.96.4
probe always answered - reads blank 0x0000, hd120 0x0001 and dd120 0x0003
and fails three of the four; dd144 passes only because its other size is
row 0.
"""
import argparse
import os
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tests'))
import os88sym          # noqa: E402
import os88build        # noqa: E402
from colorpresets_qemu import Guest     # noqa: E402

ARMS = [('blank', None, 0x0200),
        ('dd144', 'build/apps720.img', 0x0002),
        ('hd120', 'build/apps120.img', 0x0301),
        ('dd120', 'build/apps360.img', 0x0103)]
B_VOL = 1                       # the volume table's B: row (SPEC.md 18.7)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--qemu', default='qemu-system-i386')
    ap.add_argument('--image', default=os88build.at('build/os8088.img'))
    args = ap.parse_args()
    os88sym.default_defines('KERN_BIG')
    os88sym.syms()              # verifies identity against kernel.bin
    lin = os88sym.linear
    out = Path(os88build.at('build')) / 'fmtpair'
    out.mkdir(exist_ok=True)
    shutil.copyfile(args.image, out / 'sys.img')
    args.image = str(out / 'sys.img')
    bad = 0
    for name, src, want in ARMS:
        b = out / ('%s.img' % name)
        if src is None:
            with open(b, 'wb') as f:
                f.truncate(1474560)
        else:
            shutil.copyfile(os88build.at(src), b)
        args.image_b = str(b)
        g = Guest(args)
        try:
            drv = lin('disk_drive')
            old = g.read(drv, 1)
            g.write(drv, bytes([B_VOL]))
            ax = g.call('dskw_fmt_probe', near=True) & 0xFFFF
            g.write(drv, old)
        finally:
            g.close()
        ok = ax == want
        bad += not ok
        print('%-4s %-6s AX=%04X want %04X' % ('ok' if ok else 'FAIL', name,
                                               ax, want))
    if bad:
        print('FAIL: %d of %d arms' % (bad, len(ARMS)))
        return 1
    print('PASS: every drive offers its two sizes (SPEC.md 18.96.4)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
