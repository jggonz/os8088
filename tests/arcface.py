#!/usr/bin/env python3
# =============================================================================
# os8088 - tests/arcface.py - ARCADE's frontend (ARCADE-SPEC 11)
#
#     python3 tests/arcface.py [--machine os8088_xt_vga] [--no-play]
#
# The game browser on MartyPC, read out of the package's own memory and off
# the glass:
#   - opening is a LOOK: every game's state from where its .ROM is, and no
#     game read (11.1) - on a display without VGA's 640x480 the two VGA
#     games say NEEDS VGA;
#   - the keys move the selection (Down, End, Home, a letter);
#   - a move redraws in one pass: m.flicker() sees no pixel changed and
#     changed back inside the window (11.3);
#   - the incremental face is the full one: the window after the moves is,
#     pixel for pixel, the window after a whole repaint (About up and down);
#   - a game played and left gets its picture (11.4): the slot filled, and
#     <SET>.PIC written beside its .ROM, 2,164 bytes, 'AP' 120 x 144.
# =============================================================================
import argparse
import os
import re
import struct
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
from arcimport import fat_cat       # noqa: E402


def var_offsets(names):
    """Each AR_VAR's offset in ARCADE's segment, off its listing: the first
    instruction that addresses [name] carries it."""
    lst = open(os88build.at('build/arcade.lst')).read()
    out = {}
    for n in names:
        m = re.search(r'\[([0-9A-F]{2})([0-9A-F]{2})\][^\n]*\[' + n + r'(\+[a-z0-9_]+)?\]', lst)
        if not m:
            raise SystemExit('arcface: %s is not in the listing' % n)
        out[n] = int(m.group(2) + m.group(1), 16)
    return out


def pkg_base(ui, w):
    """ARCADE's segment, as a flat address: its window's W_SEG (the region is
    movable, so it is asked again every time)."""
    import os88geom as geom
    slot = w.i if hasattr(w, 'i') else w
    rec = ui._S('wm_wins') + slot * geom.WIN_SIZE
    return struct.unpack('<H', bytes(ui.m.read(rec + geom.W_SEG, 2)))[0] * 16


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--machine', default='os8088_xt_vga')
    ap.add_argument('--no-play', action='store_true')
    a = ap.parse_args()
    V = var_offsets(['af_n', 'af_sel', 'af_state', 'af_pics', 'af_nready', 'af_ox', 'af_oy'])
    bad = []
    # the disk: room for the picture this writes. VGA: the 720KB development
    # disk on the XT whose B: is 720KB; a mono 5150's drives are 40-cylinder,
    # so there a 360KB disk of its own, without the two VGA games' sets
    # (they say NEEDS VGA there whether their .ROM is found or not)
    vga = 'vga' in a.machine
    if vga:
        machine, apps = 'os8088_xt_vga_720b', os88build.at('build/arcade720.img')
    else:
        machine = a.machine
        apps = os.path.join(tempfile.mkdtemp(prefix='arcface-'), 'mono360.img')
        roms = [os88build.at('build/arcade/roms/%s.ROM' % n) for n in
                ('INVADERS', 'INVADPT2', 'LRESCUE', 'GUNFIGHT', 'SEAWOLF')]
        import subprocess
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'os88disk.py'), '-o', apps,
                        '--size', '360', '--folder', 'SYSTEM/APPDATA',
                        os88build.at('build/arcade.o88')] + roms, check=True, capture_output=True)
    with os88ui.boot('build/os8088-360.img', apps=apps,
                     machine=machine, quick=True) as ui:
        m = ui.m

        def rd(name, n=1):
            m.pause()
            b = pkg_base(ui, w)
            v = bytes(m.read(b + V[name], n))
            m.run()
            return v

        def settle_ui():
            os88marty.guest_sleep(m, 1.5)
        w = ui.path('B:/ARCADE.O88')
        m.run()
        settle_ui()
        n = rd('af_n')[0]
        st = list(rd('af_state', n))
        want = [0] * n
        if not vga:
            want[5:7] = [2] * (n - 5)
        print('arcface: %d games, states %s (0 READY, 1 ROM NEEDED, 2 NEEDS VGA)' % (n, st))
        if st != want:
            bad.append('states %s, wanted %s' % (st, want))
        # the keys
        for k, sel in (('ArrowDown', 1), ('ArrowDown', 2), ('End', n - 1), ('Home', 0),
                       ('KeyG', 3), ('ArrowDown', 4)):
            m.key(k)
            settle_ui()
            got = rd('af_sel')[0]
            if got != sel:
                bad.append('%s: selection %d, wanted %d' % (k, got, sel))
        print('arcface: Down, End, Home, a letter: the selection followed')
        # the mouse: a row selects, the ADD ROMS cap opens the chooser
        ox, oy = struct.unpack('<HH', rd('af_ox', 2) + rd('af_oy', 2))
        full = 'cga' not in a.machine
        ly, lh, by = (48, 24, 252) if full else (16, 10, 102)
        ui.mo.click(ox + 40, oy + ly + 2 * lh + lh // 2)
        settle_ui()
        if rd('af_sel')[0] != 2:
            bad.append('a click on the third row: selection %d' % rd('af_sel')[0])
        ui.mo.click(ox + 136 + 40, oy + by + 6)
        try:
            ui.chooser_cancel()
            print('arcface: a click on a row selects it; ADD ROMS opens the chooser')
        except Exception as e:
            bad.append('ADD ROMS: no chooser (%s)' % str(e)[:80])
        settle_ui()
        # a move, priced on the glass: nothing drawn twice
        x, y, ww, wh = ui._rect(w.i if hasattr(w, 'i') else w)
        m.pause()
        m.key('ArrowUp')
        r = m.flicker(frames=30)
        tr = r['worst_transient']
        box = [f['bbox'] for f in r['per_frame'] if f['transient']]
        print('arcface: a move: settled %s, %d frames changed, worst transient %d px %s'
              % (r['settled'], r['moved_frames'], tr, box[:2]))
        if not r['settled']:
            bad.append('the move never settled')
        if tr:
            bad.append('a move drew %s pixels twice (box %s)' % (tr, box))
        m.run()
        settle_ui()

        def glass():
            m.pause()
            fw, fh, d = m.fbuf()
            m.run()
            return fw, d
        ui.mo.to(2, y + wh // 2)        # (the pointer parked off the window
        settle_ui()                     #   for both captures)
        fw, before = glass()
        ui.menu_pick('Arcade', 'About Arcade')
        settle_ui()
        os88marty.guest_sleep(m, 1)
        ui.mo.click(x + ww // 2, y + wh // 2)   # (a click puts the card away)
        ui.mo.to(2, y + wh // 2)
        settle_ui()
        fw, after = glass()
        diff = 0
        box = None
        for yy in range(y + 20, y + wh):
            o = (yy * fw + x) * 3
            if before[o:o + ww * 3] != after[o:o + ww * 3]:
                diff += 1
                xs = [i for i in range(ww) if before[o + i * 3:o + i * 3 + 3] != after[o + i * 3:o + i * 3 + 3]]
                b = (x + xs[0], yy, x + xs[-1], yy)
                box = b if box is None else (min(box[0], b[0]), box[1], max(box[2], b[2]), yy)
        if os.environ.get('ARCFACE_PNG'):
            fh = len(before) // 3 // fw
            os88marty.write_png_rgb(os.environ['ARCFACE_PNG'] + 'a.png', fw, fh, before)
            os88marty.write_png_rgb(os.environ['ARCFACE_PNG'] + 'b.png', fw, fh, after)
        print('arcface: after a whole repaint, %d rows of the window differ from the '
              'incremental face %s' % (diff, box or ''))
        if diff:
            bad.append('the incremental face is not the full one (%d rows)' % diff)
        if not a.no_play:
            m.key('Home')
            settle_ui()
            m.key('Enter')
            os88marty.guest_sleep(m, 12)
            m.key('Escape')
            for k in range(40):
                os88marty.guest_sleep(m, 0.5)
                if rd('af_pics')[0]:
                    break
            print('arcface: the picture taken and kept within %.1f guest seconds of Esc'
                  % ((k + 1) * 0.5))
            os88marty.guest_sleep(m, 2)
            m.pause()
            back = os.path.join(tempfile.mkdtemp(prefix='arcface-'), 'b.img')
            m.flush(1, back)
            m.run()
            pic = fat_cat(back, 'INVADERS.PIC')
            ok = rd('af_pics')[0] == 1 and pic is not None and len(pic) == 2164 \
                and pic[:4] == b'AP' + bytes([120, 144])
            lit = sum(bin(b).count('1') for b in (pic or b'')[4:])
            print('arcface: Space Invaders played and left: picture %s, INVADERS.PIC %s, '
                  '%d of 17,280 points lit' % ('kept' if rd('af_pics')[0] else 'NONE',
                                               '%d bytes' % len(pic) if pic else 'MISSING', lit))
            if not ok:
                bad.append('no picture kept and written')
            elif not (200 < lit < 9000):
                bad.append('the picture is %d points lit: not a game screen' % lit)
    for b in bad:
        print('arcface: FAIL - ' + b)
    if bad:
        return 1
    print('arcface: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
