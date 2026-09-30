#!/usr/bin/env python3
"""Real desktop drops: source integrity, clipboard, persistence and repaint.

Run after make: python3 tests/desktop.py [--machine os8088_5150_herc]
The host FAT reader checks the saved config independently of the kernel.
"""
import argparse
import os
from pathlib import Path
import struct
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import os88flush
import os88marty
import os88ui
import dispapps

args = argparse.ArgumentParser()
args.add_argument('--image', default=os.environ.get('OS88_SYSIMG', 'build/os8088-360.img'))
args.add_argument('--machine', default='os8088_5150_herc')
a = args.parse_args()


def wait(ui, predicate, what):
    os88marty.until(ui.m, lambda _: predicate(), what, limit=45)


def saved(ui):
    wait(ui, lambda: not ui._byte('dl_dirty') and not ui._byte('dl_save'),
         'desktop configuration committed')
    ui.settle()


def record(ui, index=0):
    return ui.m.read((ui._word('dl_seg') << 4) + 512 + index * 256, 256)


def target(r):
    return r[8:136].split(b'\0')[0]


def drop(ui, w, name, x, y, count):
    ui.raise_window(w)
    row, _ = ui.entry(name, win=w)
    visible = ui.scroll_to(row, win=w)
    ui.mo.drag(*ui.row_xy(w, visible), x, y)
    wait(ui, lambda: ui._byte('dl_count') == count, 'desktop drop')
    wait(ui, lambda: struct.unpack_from('<H', record(ui, count - 1), 2)[0]
         in range(max(24, y - 8) - 4, max(24, y - 8) + 5), 'shortcut placed')
    saved(ui)


def repaint_check(ui):
    ui.mo.to(620, 22)
    ui.settle()
    card = 'cga' if 'cga' in a.machine else 'herc'
    _, _, before = ui.m.vram(card)
    ui.m.write(ui._S('cp_dirty'), b'\x01')
    wait(ui, lambda: not ui._byte('cp_dirty'), 'full repaint')
    ui.settle()
    _, _, after = ui.m.vram(card)
    # Only the desktop band; the clock can change during a repaint.
    assert before[24:-20] == after[24:-20], 'partial paint differs from full paint'


with tempfile.TemporaryDirectory(prefix='os88-desktop-') as tmp:
    tmp = Path(tmp)
    note = tmp / 'NOTE.TXT'
    note.write_bytes(b'Desktop shortcut document\r\n')
    apps = os88marty.scratch_disk(str(tmp / 'source.img'),
                                'APPS:build/notepad.o88',
                                'DOCS/DEEP:' + str(note))
    boot = a.image
    height = 200 if 'cga' in a.machine else 348
    y = height - 86
    persisted = tmp / 'persisted.img'
    with os88ui.boot(boot, apps=apps, machine=a.machine) as ui:
        assert ui._byte('dl_count') == 0 and ui._word('dl_seg') == 0
        # An existing file clipboard must survive shortcut creation/cancellation.
        clipboard = b'\x01\x01\x00\x00\x01\x00' + b'NOTEPAD.O88\0\0'
        assert len(clipboard) == 19
        ui.m.write(ui._S('fcp_cbop'), clipboard)
        w = ui.open_drive('B')
        w = ui.open('APPS', win=w)
        drop(ui, w, 'NOTEPAD.O88', 50, y, 1)
        assert target(record(ui)) == b'\\APPS\\NOTEPAD.O88'
        assert ui.m.read(ui._S('fcp_cbop'), 19) == clipboard
        # Duplicate drops reposition the same link; the old cell must erase.
        drop(ui, w, 'NOTEPAD.O88', 50, y - 64, 1)
        assert struct.unpack_from('<H', record(ui), 2)[0] != y - 8
        ui.close(w)
        repaint_check(ui)
        r = record(ui)
        x0, y0 = struct.unpack_from('<HH', r)
        ui.mo.drag(x0 + 56, y0 + 8, x0 + 56, y + 8)
        # UART mouse positioning has a two-pixel tolerance at each endpoint.
        wait(ui, lambda: abs(struct.unpack_from('<H', record(ui), 2)[0] - y) <= 4, 'shortcut moved')
        saved(ui)
        assert abs(struct.unpack_from('<H', record(ui), 2)[0] - y) <= 4
        repaint_check(ui)
        # Open the package through the File menu after selecting its tile.
        ui.menu_pick('File', 'Open Shortcut')
        wait(ui, lambda: any('Note' in s for s in ui.titles()), 'shortcut package opens')
        ui.close(ui.front())
        w = ui.open_drive('B')
        w = ui.move_window(w, 240, 40)
        drop(ui, w, 'DOCS', 165, y, 2)
        w = ui.open('DOCS', win=w)
        w = ui.open('DEEP', win=w)
        drop(ui, w, 'NOTE.TXT', 50, y - 64, 3)
        assert target(record(ui, 2)) == b'\\DOCS\\DEEP\\NOTE.TXT'
        ui.close(w)
        repaint_check(ui)
        fl = os88flush.Flush(marty=ui.m)
        cfg = fl.volume(0).read('DESKTOP.CFG')
        assert cfg[:8] == b'O88DESK\0'
        assert struct.unpack_from('<HHH', cfg, 8) == (1, 256, 3)
        assert len(cfg) == 512 + 3 * 256
        assert fl.volume(0).find('DESKTOP.CFG')[0].attr & 7 == 6
        assert fl.volume(1).img == Path(apps).read_bytes(), 'source disk changed'
        fl.save(0, str(persisted))
        print('PASS drop, clipboard, duplicate, move, package, FAT and repaint', flush=True)

    with os88ui.boot(str(persisted), apps=apps, machine=a.machine) as ui:
        assert ui._byte('dl_count') == 3
        assert target(record(ui, 2)) == b'\\DOCS\\DEEP\\NOTE.TXT'
        # A nested document uses the association and full path after reboot.
        x, yy = struct.unpack_from('<HH', record(ui, 2))
        ui.mo.dblclick(x + 56, yy + 8)
        if 'KERN_SMALL' in os.environ.get('OS88_DEFINES', ''):
            # kern_small omits associations (SPEC 54.0); this is the Disk
            # window's existing refusal for a document, with no extra subsystem.
            assert ui.toast()[0] == 'Load failed'
            assert not ui.titles()
        else:
            wait(ui, lambda: any('Note' in s for s in ui.titles()), 'shortcut document opens')
            _, pseg = dispapps.pkg_seg(ui.m, 0)
            plen = dispapps.sym('notepad', 'np_len')
            want = note.read_bytes().replace(b'\r\n', b'\r')
            wait(ui, lambda: struct.unpack('<H', ui.m.read((pseg << 4) + plen, 2))[0]
                 == len(want), 'document contents loaded')
            dseg = struct.unpack('<H', ui.m.read((pseg << 4)
                                 + dispapps.sym('notepad', 'np_dseg'), 2))[0]
            assert ui.m.read(dseg << 4, len(want)) == want
            ui.close(ui.front())
        x, yy = struct.unpack_from('<HH', record(ui, 1))
        ui.mo.dblclick(x + 20, yy + 8)
        wait(ui, lambda: bool(ui.titles()), 'shortcut folder opens')
        w = ui.front()
        assert 'DEEP' in [n for n, _ in ui.listing(w)]
        ui.close(w)
        ui.mo.click(x + 20, yy + 8)
        ui.menu_pick('File', 'Remove Shortcut')
        wait(ui, lambda: ui._byte('dl_count') == 2, 'shortcut removed')
        saved(ui)
        assert target(record(ui, 1)) == b'\\DOCS\\DEEP\\NOTE.TXT'
        repaint_check(ui)
        fl = os88flush.Flush(marty=ui.m)
        assert fl.volume(1).img == Path(apps).read_bytes(), 'removal changed target disk'
        assert struct.unpack_from('<H', fl.volume(0).read('DESKTOP.CFG'), 12)[0] == 2
        for left in (1, 0):
            x, yy = struct.unpack_from('<HH', record(ui, left))
            ui.mo.click(x + 20, yy + 8)
            ui.m.key('Delete')
            wait(ui, lambda: ui._byte('dl_count') == left, 'Delete removes shortcut')
            saved(ui)
        assert ui._word('dl_seg') == 0, 'empty desktop retained its record claim'
        assert struct.unpack_from('<H', fl.volume(0).read('DESKTOP.CFG'), 12)[0] == 0
        print('PASS reboot, document behavior, folder, removal, compaction and release', flush=True)
        # The delayed clipboard arm must still preserve Disk-window moves.
        src = ui.open_drive('B')
        src = ui.open('APPS', win=src)
        src = ui.move_window(src, 0, 20)
        dst = ui.open_drive('B')
        dst = ui.open('DOCS', win=dst)
        dst = ui.move_window(dst, 240, 20)
        ui.raise_window(src)
        ix, _ = ui.entry('NOTEPAD.O88', win=src)
        row = ui.scroll_to(ix, win=src)
        ui.mo.drag(*ui.row_xy(src, row), dst.x + dst.w - 32, dst.y + dst.h - 35)
        wait(ui, lambda: 'NOTEPAD.O88' not in [n for n, _ in ui.listing(src)],
             'Disk-window move committed')
        moved = fl.volume(1)
        original = os88flush.Volume(Path(apps).read_bytes())
        assert moved.read('DOCS/NOTEPAD.O88') == original.read('APPS/NOTEPAD.O88')
        assert not [e for e in moved.listdir('APPS') if e.name == 'NOTEPAD.O88']
        assert ui._byte('dl_count') == 0
        print('PASS Disk-window drag retains move semantics', flush=True)

    # A malformed path in an otherwise valid file rejects the ENTIRE desktop.
    image = bytearray(persisted.read_bytes())
    v = os88flush.Volume(image)
    entry = v.find('DESKTOP.CFG')[0]
    clusters = v.chain(entry.cluster)
    off = (v.data_lba + (clusters[1] - 2) * v.spc) * 512
    image[off + 8:off + 12] = b'\\..\0'
    bad = tmp / 'invalid.img'
    bad.write_bytes(image)
    with os88ui.boot(str(bad), apps=apps, machine=a.machine) as ui:
        assert ui._byte('dl_count') == 0 and ui._word('dl_seg') == 0
        print('PASS malformed configuration rejected without a record claim', flush=True)
