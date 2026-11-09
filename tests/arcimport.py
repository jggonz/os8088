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


def chips(name):
    """{chip name: its bytes} of a set, out of the board image the cache
    holds (getarcrom.py's layout), or None without one."""
    import getarcrom
    st = getarcrom.SETS[name]
    path = os88build.at('build/arcade-rom/%s.ROM' % name.upper())
    if not os.path.exists(path):
        return None
    img = open(path, 'rb').read()
    return {n: img[at:at + getarcrom.chip_size(img, at, sha)]
            for n, at, sha in st['members']}


def zipof(path, members):
    import zipfile
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        for n, d in members:
            z.writestr(n, d)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--machine', default='os8088_xt_vga_720b')
    a = ap.parse_args()
    td = tempfile.mkdtemp(prefix='arcimport-')
    disk = os.path.join(td, 'imp720.img')
    files = [os88build.at('build/arcade.o88')]
    # each case: the file the chooser opens, the .ROM files it must make
    # (each byte for byte make arcaderoms's), or () for one that must make
    # nothing, and what it shows
    cases = []
    # BAD.ZIP: Space Invaders' four chips by their names, one bit of one
    # flipped - the directory's CRC is the flipped chip's, so only the pin
    # can refuse it
    inv = chips('invaders')
    bad = os.path.join(td, 'BAD.ZIP')
    zipof(bad, [(n, d if n != 'invaders.e' else d[:100] + bytes([d[100] ^ 1]) + d[101:])
                for n, d in inv.items()])
    files.append('ROMS:' + bad)
    cases.append(('ROMS/BAD.ZIP', (), 'a chip one bit wrong'))
    # TRUNC.ZIP: gunfight.zip cut short - its directory is gone
    tr = os.path.join(td, 'TRUNC.ZIP')
    g = open(os88build.at('build/arcade-rom/gunfight.zip'), 'rb').read()
    open(tr, 'wb').write(g[:len(g) * 3 // 5])
    files.append('ROMS:' + tr)
    cases.append(('ROMS/TRUNC.ZIP', (), 'a zip cut short'))
    for s in ('invaders', 'qbert'):
        files.append('ROMS:' + os88build.at('build/arcade-rom/%s.zip' % s))
        cases.append(('ROMS/%s.ZIP' % s.upper(), (s.upper() + '.ROM',),
                      'MAME\'s zip'))
    # MERGED.ZIP: Gun Fight's chips in a clone-named folder under other
    # names, beside a file of nothing - found by CRC, as a merged set's are
    mg = os.path.join(td, 'MERGED.ZIP')
    zipof(mg, [('readme.txt', b'not a chip\n')] +
          [('gunfigt2/chip%d.bin' % k, d) for k, d in enumerate(chips('gunfight').values())])
    files.append('ROMS:' + mg)
    cases.append(('ROMS/MERGED.ZIP', ('GUNFIGHT.ROM',), 'chips by CRC, in a folder'))
    # a SPLIT Pac-Man: pacman.zip with the four program chips alone, the rest
    # in puckman.zip beside it (the owner's set: only with it in the cache)
    pm = chips('pacman')
    if pm:
        prog = [n for n in pm if n.startswith('pacman.6')]
        zipof(os.path.join(td, 'PACMAN.ZIP'), [(n, pm[n]) for n in prog])
        zipof(os.path.join(td, 'PUCKMAN.ZIP'), [(n, d) for n, d in pm.items() if n not in prog])
        files += ['ROMS:' + os.path.join(td, 'PACMAN.ZIP'), 'ROMS:' + os.path.join(td, 'PUCKMAN.ZIP')]
        cases.append(('ROMS/PACMAN.ZIP', ('PACMAN.ROM',), 'a split clone and its parent'))
    # LOOSE: Sea Wolf's chips as files in a folder, any one of them picked
    lo = os.path.join(td, 'loose')
    os.makedirs(lo)
    for n, d in chips('seawolf').items():
        open(os.path.join(lo, n.upper()), 'wb').write(d)
        files.append('ROMS/LOOSE:' + os.path.join(lo, n.upper()))
    cases.append(('ROMS/LOOSE/SW0043.F', ('LOOSE/SEAWOLF.ROM',), 'loose chips'))
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
        fpg = m.sym('fpg_total')
        back = os.path.join(td, 'back.img')

        def look():
            m.pause()
            m.flush(1, back)
            m.run()
        for pick, want, what in cases:
            ui.menu_pick('Game', 'Add ROM sets...')
            ui.chooser_open(pick)           # (ARCADE goes home after each)
            if not want:
                os88marty.guest_sleep(m, 20)
                look()
                made = [w for w in ('INVADERS.ROM', 'GUNFIGHT.ROM')
                        if fat_cat(back, 'ROMS/' + w) is not None]
                print('arcimport: %s (%s) -> %s' % (pick, what, 'WROTE ' + ' '.join(made)
                                                    if made else 'refused, nothing written'))
                if made:
                    bad.append('%s made %s' % (pick, ' '.join(made)))
                continue
            # the import reads, unpacks and writes: wait for the floppy to
            # have the file, and see the menu bar's progress armed meanwhile
            got = barred = False
            for _ in range(480):
                os88marty.guest_sleep(m, 0.5)
                barred = barred or struct.unpack('<H', bytes(m.read(fpg, 2)))[0] != 0
                look()
                if all(fat_cat(back, 'ROMS/' + w) is not None for w in want):
                    got = True
                    break
            if not got:
                bad.append('%s: no %s after 240 guest seconds' % (pick, ' '.join(want)))
                continue
            os88marty.guest_sleep(m, 2)     # (the last sectors, the FAT)
            look()
            for w in want:
                mine = fat_cat(back, 'ROMS/' + w)
                ref = open(os88build.at('build/arcade/roms/%s' % w.split('/')[-1]), 'rb').read()
                same = mine == ref
                print('arcimport: %s (%s) -> %s: %d bytes, %s%s'
                      % (pick, what, w, len(mine),
                         'the same as make arcaderoms\'s' if same else 'DIFFERENT',
                         ', progress shown' if barred else ''))
                if not same:
                    bad.append('%s: not the file the pins make' % w)
            if not barred and pick.endswith('QBERT.ZIP'):
                bad.append('%s: the menu bar\'s progress never armed' % pick)
        v = None
        for g in ('Space Invaders', 'Q*bert'):   # and the imported games run
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
            os88marty.guest_sleep(m, 20)    # (a first leave takes the game's
                                            #   picture and writes it: 11.4)
        # ARCADE again, from its own folder: the .ROM files are in ROMS, so a
        # pick finds them only through ARCADE.DAT's places
        ui.close(win)
        ui.path('B:/ARCADE.O88')
        m.run()
        os88marty.guest_sleep(m, 1)
        g = 'Q*bert'
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
