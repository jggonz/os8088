#!/usr/bin/env python3
# =============================================================================
# os8088 - tests/arcimport.py - a MAME .ZIP made into ARCADE's .ROM on the
# machine itself (ARCADE-SPEC 1.5; docs/plans/ARCADE-RELEASE-PLAN.md 2.2)
#
#     python3 tests/arcimport.py [--machine os8088_xt_vga] [ZIPSET ...]
#
# A 720KB disk (invaders.zip alone is 220KB) with ARCADE.O88 and the sets' zips as MAME has them - fetched
# at their pins (make arcaderom), never committed - and NO .ROM file. ARCADE
# is opened (no game: nothing to pick), each zip is chosen through Game > Add
# ROM sets... and the system file chooser, and when the import is done the
# floppy is flushed and read back on the host: each <SET>.ROM the zip should
# have made must be there and be, byte for byte, the file `make arcaderoms`
# writes from the same pins. Then the imported game is picked and must RUN
# (its frames counted in the view). invaders.zip is in by default because it
# is the hard one: forty clone sets in folders, the parent's four chips at
# the root, a TorrentZip comment after the directory.
# =============================================================================
import argparse
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, HERE)
os.chdir(ROOT)
import os88build                    # noqa: E402
import os88marty                    # noqa: E402
import os88ui                       # noqa: E402
from arcspeed import find_view      # noqa: E402

# a zip, the .ROM files it must make, and a game to play from it
SETS = {'invaders': (['INVADERS.ROM'], 'Space Invaders'),
        'qbert': (['QBERT.ROM'], 'Q*bert'),
        'gunfight': (['GUNFIGHT.ROM'], 'Gun Fight')}


def fat_cat(img, path):
    """A file's bytes off a FAT12 image, through its folders ('ROMS/X.ROM'),
    or None."""
    import struct
    from os88fat import Fat12
    v = Fat12(img)
    csz = v.spc * v.bps
    entries = [e for _, _, e in v.entries()]
    parts = path.upper().split('/')
    for k, name in enumerate(parts):
        raw = Fat12.raw11(name)
        e = next((e for e in entries if e[:11] == raw), None)
        if e is None:
            return None
        first = struct.unpack_from('<H', e, 26)[0]
        body = b''.join(bytes(v.img[v.cluster_off(c):v.cluster_off(c) + csz])
                        for c in v.chain(first)) if first else b''
        if k == len(parts) - 1:
            return body[:struct.unpack_from('<I', e, 28)[0]]
        entries = [body[i:i + 32] for i in range(0, len(body), 32)
                   if body[i] not in (0, 0xE5)]
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--machine', default='os8088_xt_vga_720b')
    ap.add_argument('sets', nargs='*')
    a = ap.parse_args()
    sets = a.sets or ['invaders', 'qbert']
    td = tempfile.mkdtemp(prefix='arcimport-')
    disk = os.path.join(td, 'imp720.img')
    # BAD.ZIP: Space Invaders' four chips by their names, one byte of one
    # flipped - the directory's CRC is the flipped chip's, so only the pin
    # can refuse it, and no INVADERS.ROM may come of it
    import zipfile
    good = zipfile.ZipFile(os88build.at('build/arcade-rom/invaders.zip'))
    badzip = os.path.join(td, 'BAD.ZIP')
    with zipfile.ZipFile(badzip, 'w', zipfile.ZIP_DEFLATED) as z:
        for n in ('invaders.h', 'invaders.g', 'invaders.f', 'invaders.e'):
            d = bytearray(good.read(n))
            if n == 'invaders.e':
                d[100] ^= 1
            z.writestr(n, bytes(d))
    # the zips in a folder of their own: the .ROM goes beside its zip, the
    # chooser leaves ARCADE standing there, and a pick must still read
    # ARCADE.O88's own parts - and, back in its own folder, find the .ROM
    # through ARCADE.DAT's places
    files = [os88build.at('build/arcade.o88'), 'ROMS:' + badzip] + \
        ['ROMS:' + os88build.at('build/arcade-rom/%s.zip' % s) for s in sets]
    subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'os88disk.py'), '-o', disk,
                    '--size', '720', '--folder', 'SYSTEM/APPDATA'] + files,
                   check=True, capture_output=True)   # (ARCADE.DAT's, as every
                                                      #   application's disk has)
    bad = []
    with os88ui.boot('build/os8088-360.img', apps=disk, machine=a.machine, quick=True) as ui:
        m = ui.m
        win = ui.path('B:/ARCADE.O88')
        m.run()
        os88marty.guest_sleep(m, 1)
        ui.menu_pick('Game', 'Add ROM sets...')
        ui.chooser_open('ROMS/BAD.ZIP')
        os88marty.guest_sleep(m, 20)
        m.pause()
        back = os.path.join(td, 'back.img')
        m.flush(1, back)
        m.run()
        made = fat_cat(back, 'ROMS/INVADERS.ROM') is not None
        print('arcimport: BAD.ZIP (a chip one bit wrong) -> %s'
              % ('INVADERS.ROM WRITTEN' if made else 'refused, nothing written'))
        if made:
            bad.append('BAD.ZIP made an INVADERS.ROM')
        for s in sets:
            ui.menu_pick('Game', 'Add ROM sets...')
            ui.chooser_open('ROMS/' + s.upper() + '.ZIP')   # (ARCADE goes home
                                                            #   after each)
            # the import reads the zip and writes the .ROM: wait for the
            # floppy to have the file (flushed and listed on the host)
            want = SETS[s][0]
            got = None
            for _ in range(240):
                os88marty.guest_sleep(m, 1)
                m.pause()
                back = os.path.join(td, 'back.img')
                m.flush(1, back)
                m.run()
                if all(fat_cat(back, 'ROMS/' + w) is not None for w in want):
                    got = back
                    break
            if not got:
                bad.append('%s.zip: no %s after 240 guest seconds' % (s, ' '.join(want)))
                continue
            os88marty.guest_sleep(m, 2)     # (the last sectors, the FAT)
            m.pause()
            m.flush(1, got)
            m.run()
            for w in want:
                mine = fat_cat(got, 'ROMS/' + w)
                ref = open(os88build.at('build/arcade/roms/%s' % w), 'rb').read()
                same = mine == ref
                print('arcimport: %s.zip -> %s: %d bytes, %s'
                      % (s, w, len(mine), 'the same as make arcaderoms\'s' if same else 'DIFFERENT'))
                if not same:
                    bad.append('%s: not the file the pins make' % w)
        v = None
        for s in sets:                      # and each imported game runs
            g = SETS[s][1]
            m.pause()
            if v is None:
                v = find_view(m)
            m.write(v + 0x6044, b'\0\0')
            m.run()
            ui.menu_pick('Game', g)
            for _ in range(240):
                os88marty.guest_sleep(m, 0.5)
                if struct.unpack('<H', bytes(m.read(v + 0x6044, 2)))[0] > 30:
                    break
            else:
                bad.append('%s: never ran from its imported .ROM' % g)
                continue
            print('arcimport: %s runs' % g)
            m.key('Escape')
            os88marty.guest_sleep(m, 1)
        # ARCADE again, from its own folder: the .ROM files are in ROMS, so a
        # pick finds them only through ARCADE.DAT's places
        ui.close(win)
        ui.path('B:/ARCADE.O88')
        m.run()
        os88marty.guest_sleep(m, 1)
        g = SETS[sets[-1]][1]
        m.pause()
        v = find_view(m)
        m.write(v + 0x6044, b'\0\0')
        m.run()
        ui.menu_pick('Game', g)
        for _ in range(240):
            os88marty.guest_sleep(m, 0.5)
            if struct.unpack('<H', bytes(m.read(v + 0x6044, 2)))[0] > 30:
                print('arcimport: %s runs from ROMS\\, ARCADE started in B:\\ (ARCADE.DAT)' % g)
                break
        else:
            bad.append('%s: not found through ARCADE.DAT after a restart' % g)
    for b in bad:
        print('arcimport: FAIL - ' + b)
    if bad:
        return 1
    print('arcimport: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
