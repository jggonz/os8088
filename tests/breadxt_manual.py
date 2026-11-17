#!/usr/bin/env python3
"""Build, solve, save and reload a complete LED circuit through native controls.

No example, file or fixture supplies the circuit. All four parts and both
wires start from the empty installed package document. Reads use BreadXT's
linked map and live Pkg snapshots; completion waits use the guest's state.
"""
import argparse
import os
from pathlib import Path
import struct
import sys
import tempfile
import time

from breadxt import App, BBX, MACHINES, M, ROOT, meaningful_doc, os88flush, os88ui, symbols

SAVED = 'BUILT.BBX'
# Physical hole numbers from the documented30-column breadboard: all leads
# have distinct holes, while terminal strips and rails make the intended nets.
PARTS = (
    ('Power', 4, (0, 30)),
    ('Ground', 5, (31,)),
    ('Resistor', 1, (63, 70)),
    ('LED', 2, (100, 110)),
)
WIRES = ((1, 93), (32, 140))


def place_part(app, menu, typ, holes):
    before = app.state()['np']
    app.ui.menu_pick('Parts', menu)
    for hole in holes:
        app.click(*app.hole_xy(hole))
    app.wait(lambda q: app.word('bu_ready') == 1, menu + ' placement preview', 30)
    app.ui.press('Enter')
    app.wait(lambda q: q['np'] == before + 1 and not q['op'], menu + ' placement commit', 60)
    record = app.part(before)
    assert record[1] == typ and record[2:2 + len(holes)] == holes, (menu, record)
    if typ == 4:
        assert record[5] == 5000, record
    if typ == 1:
        assert record[5] == 33, record  #330 ohms, not a hidden example value
    print(f'  placed {menu}: id{record[0]}, holes{holes}', flush=True)


def place_wire(app, a, b):
    before = app.state()['nw']
    app.ui.type('w')
    app.click(*app.hole_xy(a))
    app.click(*app.hole_xy(b))
    app.wait(lambda q: app.word('bu_ready') == 1, 'orthogonal wire preview', 30)
    app.ui.press('Enter')
    app.wait(lambda q: q['nw'] == before + 1 and not q['op'], 'wire commit', 60)
    raw = app.p.read(app.s['_bb_doc'] + 1024 + before * 16, 16)
    record = struct.unpack('<7H2B', raw)
    assert record[1:3] == (a, b) and record[8] == 1, record
    # The automatic elbow is a path point, not another electrically joined
    # terminal. Verify the actual native preview generated the orthogonal bend.
    assert record[3:5] == (b % 30, a // 30), record
    print(f'  wired {a} to {b}, native elbow{record[3:5]}', flush=True)


def assert_current(app, negative=False):
    app.step()
    assert app.state()['np'] == 4 and app.state()['nw'] == 2
    assert app.word('bb_nnet') == 3, app.word('bb_nnet')
    if negative:
        app.p.write(app.s['_bb_result'] + 128 + 3 * 2, struct.pack('<h', 0))
    actual = app.currents()
    # Passive pin1-to-pin2 direction: the source supplies negative current;
    # the resistor and forward LED carry positive current. Ground has no
    # two-terminal branch current and is deliberately omitted.
    assert (actual[0], actual[2], actual[3]) == (-8824, 8824, 8824), actual[:4]
    print('  native source/R/LED currents: -8824/+8824/+8824uA, three nets', flush=True)


def led_body(app):
    # Pixels are the subject here: derive the diode's interior from its actual
    # live pin geometry, not from a remembered screen coordinate or injected
    # result. The first genuine solve is the positive raster baseline.
    app.ui.still()
    part = app.part(3)
    a, b = app.hole_xy(part[2]), app.hole_xy(part[3])
    cx, cy = (a[0] + b[0]) // 2, (a[1] + b[1]) // 2
    width, height, rgb = app.ui.m.fbuf()
    assert 10 <= cx < width - 10 and 9 <= cy < height - 6
    def pixel(x, y):
        offset = (y * width + x) * 3
        return tuple(rgb[offset:offset + 3])
    interior = tuple(pixel(x, y) for y in range(cy - 2, cy + 3)
                     for x in range(cx - 2, cx + 3))
    body = tuple(pixel(x, y) for y in range(cy - 9, cy + 6)
                 for x in range(cx - 10, cx + 11))
    red = sum(r >= 128 and r > g + 40 and r > blue + 40 for r, g, blue in interior)
    assert red >= 20, ('powered LED body lacks red pixels', (cx, cy), red, interior)
    print(f'  powered LED body at live center{(cx, cy)}: {red}/25 red interior pixels', flush=True)
    return body


def bind_probes(app):
    app.ui.type('p')
    app.click(*app.hole_xy(100))
    app.ui.press('Tab')
    app.click(*app.hole_xy(0))
    app.ui.type('v')
    led_id, source_id = app.part(3)[0], app.part(0)[0]
    assert tuple(app.word('bb_doc', off) for off in (3094, 3096)) == (1, 1)
    assert tuple(app.word('bb_doc', off) for off in (3098, 3100)) == (led_id, source_id)
    assert tuple(app.word('bb_doc', off) for off in (3102, 3104)) == (1, 1)
    print(f'  A bound to LED id{led_id} pin1, B to source id{source_id} pin1', flush=True)
    return led_id, source_id


def screenshot(ui):
    folder = Path(os.environ.get('BREADXT_SHOTS', tempfile.gettempdir()))
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / 'vga-manual-built-led.png'
    ui.still()
    width, height, pixels = ui.m.fbuf()
    M.write_png_rgb(str(path), width, height, pixels)
    print(f'  evidence {path.resolve()}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--negative-current', action='store_true',
                        help='deliberately corrupt a real result; this invocation must fail')
    args = parser.parse_args()
    started = time.monotonic()
    sys.stdout.reconfigure(line_buffering=True)
    os.chdir(ROOT)
    offsets = symbols()
    with tempfile.TemporaryDirectory(prefix='breadxt-manual-') as folder:
        with os88ui.boot('build/os8088-360.img', apps='build/breadxt360.img',
                         machine=MACHINES['vga'], quick=True) as ui:
            ui.open_drive('B')
            app = App(ui, offsets)
            initial = app.state()
            assert initial['np'] == initial['nw'] == initial['dirty'] == 0, initial
            for menu, typ, holes in PARTS:
                place_part(app, menu, typ, holes)
            for a, b in WIRES:
                place_wire(app, a, b)
            assert_current(app, args.negative_current)
            baseline = led_body(app)
            led_id, source_id = bind_probes(app)
            app.save_as(SAVED)
            before = app.doc()
            # Independent FAT and bounded BBX readers inspect the actual guest
            # writes, rather than asking the application's loader to validate
            # its own save as the only durability proof.
            image = Path(folder) / 'manual-saved.img'
            os88flush.Flush(marty=ui.m).save(1, str(image))
            decoded = BBX.decode(os88flush.Volume(image.read_bytes()).read(SAVED))
            assert len(decoded['parts']) == 4 and len(decoded['wires']) == 2
            assert [part['type'] for part in decoded['parts']] == [4, 5, 1, 2]
            assert decoded['parts'][2]['value'] == 33
            assert [(wire['a'], wire['b']) for wire in decoded['wires']] == list(WIRES)
            assert all(wire['nbend'] == 1 for wire in decoded['wires'])
            meta = decoded['metadata']
            assert (meta['probe_kind_a'], meta['probe_kind_b']) == (1, 1)
            assert (meta['probe_id_a'], meta['probe_id_b']) == (led_id, source_id)
            assert (meta['probe_pin_a'], meta['probe_pin_b']) == (1, 1)
            ui.menu_pick('File', 'New')
            app.wait(lambda q: q['np'] == q['nw'] == q['dirty'] == 0 and not q['op'],
                     'clean empty New document', 60)
            app.open(SAVED, 4)
            assert meaningful_doc(app.doc()) == meaningful_doc(before), 'saved manual source changed'
            assert_current(app)
            assert led_body(app) == baseline, 'New/reload changed powered LED body raster'
            screenshot(ui)
            app.close()
    print(f'breadxt manual native: PASS elapsed {time.monotonic() - started:.2f}s', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
