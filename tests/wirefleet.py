#!/usr/bin/env python3
"""WireFleet's shipped assembly: deployment on VGA, CGA and Hercules.

Registered as wirefleetdeploy / wirefleetart in the soak tier. Build with
make wirefleetdisk first. Uses guest time, by-name navigation and owned VMs.
The negative cases require a successful placement first, so a routine that
refuses everything cannot pass. Additional delivery waves extend this file.
"""
import argparse
from collections import Counter
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
import os88build
import os88marty as M
import os88ui
import dispapps


def artifact(name):
    return str(ROOT / os88build.at('build/' + name))


def art():
    import wirefleet_assets as A
    subprocess.run([sys.executable, str(ROOT / 'tools/wirefleet_assets.py'), '--check'],
                   cwd=ROOT, check=True)
    tiles = A.tiles()
    assert len(tiles) == 72
    assert len({A.packed(t) for t in tiles[:4]}) == 4, 'color shot shapes collapsed'
    assert len({A.packed(t, True) for t in tiles[:4]}) == 4, 'mono shot shapes collapsed'
    assert all(set(t.tobytes()) <= set(range(16)) for t in tiles)
    for tile in tiles:
        assert len(A.packed(tile)) == 288 and len(A.packed(tile, True)) == 72
        assert len(A.cga(tile)) == 72
    assert len({A.cga(t) for t in tiles[:4]}) == 4, 'CGA shot shapes collapsed'
    for offset in (4, 21):
        for start, length in zip((0, 5, 9, 12, 15), (5, 4, 3, 3, 2)):
            hull = tiles[offset + start:offset + start + length]
            assert any(A.packed(t, True) != A.packed(tiles[0], True) for t in hull), 'ship vanished in mono'
    print('wirefleetart: color/mono marks distinct; all five silhouettes and both orientations bounded')


def deployment():
    from PIL import Image
    sy = dispapps._map('wirefleet')
    for adapter, machine in (('vga', 'os8088_xt_vga'),
                             ('cga', 'os8088_5150_cga'),
                             ('herc', 'os8088_5150_herc')):
        with os88ui.boot(artifact('os8088-360.img'), apps=artifact('wireflet360.img'),
                         machine=machine, settle=False) as ui:
            ui.open_drive('B')
            w = ui.open('WIREFLET.O88')
            ui.settle()
            m = ui.m
            seg = dispapps.pkg_seg(m, 0)[1]
            def val(name, n=1):
                return int.from_bytes(m.readseg(seg, sy['wf_' + name], n), 'little')
            def fleet():
                return m.readseg(seg, sy['wf_board'], 100)
            def key(name):
                m.key(name)
                ui.settle()
            def square(x, y, right=False):
                px = val('ox', 2) + val('gx0', 2) + x*24 + 12   # 114.10 moves it
                py = val('oy', 2) + val('gridy', 2) + y*val('cellh', 2) + val('cellh', 2)//2
                if right:
                    ui.mo.to(px, py, r=True)
                    ui.mo.to(px, py, r=False)
                else:
                    ui.mo.click(px, py)
                ui.settle()
            assert val('placed') == 0 and fleet() == bytes(100)
            assert val('colour') == (adapter == 'vga'), 'colour face on the wrong adapter (114.10)'
            assert val('controlsy', 2) + 26 <= val('h', 2), 'CGA controls/footer clipped'
            square(0, 0)
            assert val('placed') == 1 and fleet()[:5] == b'\1'*5, 'valid carrier refused'
            saved = fleet()
            square(0, 0)          # next ship overlaps the carrier
            assert fleet() == saved and val('placed') == 1, 'overlap mutated fleet'
            key('Digit1')
            square(9, 9)          # moving carrier beyond the edge must keep it
            assert fleet() == saved and val('placed') == 1, 'invalid move ate old ship'
            key('KeyR')
            square(9, 0)
            assert val('placed') == 1 and [fleet()[i] for i in (9,19,29,39,49)] == [1]*5
            assert fleet()[:5] == bytes(5), 'moving a ship left its old cells'
            square(9, 2, right=True)
            assert val('placed') == 0 and fleet() == bytes(100), 'right-click did not remove whole ship'
            key('KeyA')
            assert Counter(fleet()) == {0:83,1:5,2:4,3:3,4:3,5:2}, 'auto fleet lengths/occupancy'
            anchors = m.readseg(seg, sy['wf_anchors'], 5)
            directions = m.readseg(seg, sy['wf_dirs'], 5)
            for ship, (anchor, direction, length) in enumerate(zip(anchors, directions, (5,4,3,3,2)), 1):
                cells = [i for i,c in enumerate(fleet()) if c == ship]
                step = 10 if direction else 1
                assert cells == [anchor + step*i for i in range(length)]
                assert direction or anchor//10 == cells[-1]//10, 'auto ship crossed a row'
            # A normal control must not fire while held or after drag-cancel.
            rects = m.readseg(seg, sy['wf_btnrects'], 40)
            import struct
            x1,y1,x2,y2 = struct.unpack_from('<4H', rects, 2*8)  # Clear
            before = fleet()
            ui.mo.to((x1+x2)//2, (y1+y2)//2, l=True)
            assert fleet() == before, 'Clear fired on press'
            ui.mo.to(val('ox',2)+290, val('oy',2)+12, l=True)
            ui.mo.to(val('ox',2)+290, val('oy',2)+12, l=False)
            ui.settle()
            assert fleet() == before, 'cancelled Clear fired'
            key('KeyY')
            assert val('ready') == 1
            key('KeyC')
            assert fleet() == before and val('ready') == 1, 'Ready fleet remained editable'
            ui.menu_pick('WireFleet', 'About WireFleet')
            ui.settle()
            assert val('abon') == 1
            key('Escape')
            assert val('abon') == 0
            if adapter == 'vga':
                width, height, data = m.fbuf()
                image = Image.frombytes('RGB', (width, height), data)
            else:
                width, height, rows = m.vram(adapter)
                image = Image.new('L', (width, height))
                image.putdata([v*255 for row in rows for v in row])
            image.save(artifact('wirefleet-%s-acceptance.png' % adapter))
            ui.close(w)
            print('wirefleetdeploy:', adapter, 'placement/refusals/rotation/removal/auto/ready/button cancel/About/close')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--art', action='store_true')
    args = parser.parse_args()
    art() if args.art else deployment()
