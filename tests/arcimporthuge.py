#!/usr/bin/env python3
# =============================================================================
# os8088 - tests/arcimporthuge.py - a MAME zip past ARCADE's 448KB cap,
# imported on kern_mega's arm (ARCADE-SPEC 14; docs/plans/MEGA-HUGE-PLAN.md
# 3).
#
#     python3 tests/arcimporthuge.py [--keep]
#
# The ROMs are fetched data and not in the tree, so the zip is SYNTHETIC:
# Space Invaders' four 2KB chips FORGED to the CRC-32s arcsets.inc pins (the
# import's only identity for a chip - ARCADE-SPEC 1.5 - so a forged chip is
# taken as the real one would be), stored AFTER 600KB of filler members and
# so past every 64KB window and past the old 448KB cap. On 86Box's
# at286_vga_sb with 4MB (the zip is more than the heap alone holds), ARCADE's
# arm is opened off B:, Game > Add ROM sets... picks ROMS\BIG.ZIP, and the
# floppy is read back: ROMS\INVADERS.ROM must be ARCADE's .ROM of the forged
# chips, byte for byte (tools/getarcrom.py's rom_file is the reference).
#
# RED (docs/WRITING-TESTS.md 1): the arm before refused the zip as too
# big (AR_ZMAXKB 448) and wrote nothing - measured when the row was written.
# What it cannot say: that a REAL MAME zip of that size imports (no such zip
# is in the container; invaders.zip is 220KB), or that the game then runs
# (the chips are forged: they hold the CRC, not the program).
# =============================================================================
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, HERE)
os.chdir(ROOT)
os.environ.setdefault('OS88_MEGA', '1')
os.environ.setdefault('OS88_MEGA_EMU', '86box')
os.environ.setdefault('OS88_MEGA_MEGS', '4')
import os88build                    # noqa: E402
import os88marty                    # noqa: E402
import os88ui                       # noqa: E402
from arcimport import fat_cat       # noqa: E402
import getarcrom                    # noqa: E402
import megajw as J                  # noqa: E402

FILLKB = 600


def crc_table():
    t = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (c >> 1) ^ 0xEDB88320 if c & 1 else c >> 1
        t.append(c)
    return t


def forge(prefix, target):
    """prefix + 4 bytes whose CRC-32 is TARGET (the CRC is linear: run the
    register back from the target through four bytes of zero, and the
    difference from where the prefix leaves it is the patch)."""
    t = crc_table()
    rev = {c >> 24: i for i, c in enumerate(t)}
    fwd = zlib.crc32(prefix) ^ 0xFFFFFFFF
    bkd = target ^ 0xFFFFFFFF
    for _ in range(4):
        j = rev[bkd >> 24]
        bkd = (((bkd ^ t[j]) << 8) & 0xFFFFFFFF) | j
    out = prefix + struct.pack('<I', fwd ^ bkd)
    assert zlib.crc32(out) & 0xFFFFFFFF == target
    return out


def pinned(name):
    """The set's chips as arcsets.inc pins them: [(mame name, place, size,
    crc)] - the import's table, no byte of any ROM"""
    src = open(os88build.at('build/arcade/pre/arcsets.inc')).read()
    at = src.index('ar_set_%s:' % name)
    body = src[at:src.index('ar_set_', at + 10)]
    out = []
    lines = body.splitlines()
    for k, ln in enumerate(lines):
        m = re.match(r'\s*dw 0x([0-9A-F]{4}), 0x([0-9A-F]{4})', ln)
        if m:
            nm = bytes(int(x) for x in lines[k - 1].split('db')[1].split(','))
            crc = int(lines[k + 1].split('0x')[1], 16)
            out.append((nm.split(b'\0')[0].decode(), int(m.group(1), 16) * 16,
                        int(m.group(2), 16), crc))
    return out


def main():
    keep = '--keep' in sys.argv
    td = tempfile.mkdtemp(prefix='arcimporthuge-')
    fails = []
    try:
        chips = pinned('invaders')
        img = bytearray(8192)
        members = []
        for n, at, k, crc in chips:
            pre = bytes((i * 7 + at) & 0xFF for i in range(k - 4))
            d = forge(pre, crc)
            img[at:at + k] = d
            members.append((n, d))
        big = os.path.join(td, 'BIG.ZIP')
        with zipfile.ZipFile(big, 'w', zipfile.ZIP_STORED) as z:
            for f in range(FILLKB // 100):
                z.writestr('filler/f%d.bin' % f,
                           bytes((i * 13 + f) & 0xFF for i in range(100 * 1024)))
            for n, d in members:
                z.writestr(n, d)
        size = os.path.getsize(big)
        print('arcimporthuge: BIG.ZIP %d bytes (%d KB), the four chips from '
              'offset %d' % (size, (size + 1023) // 1024, size - 8192 - 400))
        ref = getarcrom.rom_file('invaders', bytes(img))
        disk = os.path.join(td, 'b.img')
        subprocess.run([sys.executable, 'tools/os88disk.py', '-o', disk,
                        '--size', '1440', '--folder', 'SYSTEM/APPDATA',
                        os88build.at('build/megak/apps/arcade.o88'),
                        'ROMS:' + big], check=True, capture_output=True)
        with os88ui.boot(os88build.at('build/mega.img'), apps=disk,
                         machine=J.MACHINE, label='arcimporthuge') as ui:
            m = ui.m
            ui.path('B:/ARCADE.O88')
            os88marty.guest_sleep(m, 1)
            ui.menu_pick('Game', 'Add ROM sets...')
            ui.chooser_open('ROMS/BIG.ZIP')
            back = os.path.join(td, 'back.img')
            got = None
            for _ in range(240):
                os88marty.guest_sleep(m, 1)
                m.flush(1, back)
                got = fat_cat(back, 'ROMS/INVADERS.ROM')
                if got is not None:
                    os88marty.guest_sleep(m, 2)
                    m.flush(1, back)
                    got = fat_cat(back, 'ROMS/INVADERS.ROM')
                    break
            if got is None:
                fails.append('no ROMS\\INVADERS.ROM after 240 guest seconds '
                             '(the zip refused?)')
            elif got != ref:
                fails.append('INVADERS.ROM is %d bytes and not the forged '
                             'chips\' .ROM' % len(got))
            else:
                print('arcimporthuge: ROMS\\INVADERS.ROM, %d bytes, the forged '
                      'chips\' .ROM byte for byte' % len(got))
            for ln in [f for f in m.faults() if 'strcal' not in f
                       and 'r286s' not in f and 'RESET' not in f][:6]:
                print('   fault: ' + ln)
    finally:
        if keep:
            print('arcimporthuge: kept ' + td)
        else:
            shutil.rmtree(td, ignore_errors=True)
    for f in fails:
        print('arcimporthuge: FAIL - ' + f)
    print('arcimporthuge: %s' % ('FAIL' if fails else 'ok'))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
