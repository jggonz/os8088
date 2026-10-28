#!/usr/bin/env python3
"""WireFleet's link on a machine whose serial mouse never speaks (SPEC.md 115.1.1).

Registered as wirefleetnomouse in soak. The field case: the keyboard mouse
drives the pointer, a modem is on COM1 and the null modem on COM2, and no
serial packet ever arrives - so the mouse contest never settles. The lease
used to refuse every port with "move the mouse" for the whole session.

Every MartyPC machine here has a mouse on COM1, and opening the package by
name moves it. So the row opens WireFleet first and then puts the probe's
verdict back to "nobody has spoken" - [mou_seen], [mou_hpst], [mou_ident],
[mou_idany], [mou_ptr] - which also switches the keyboard mouse ON (9.6), and drives
every later gesture with the keypad and Space, the way the field machine has
to. The real mouse is never moved again.

What it asserts, after a first leg with the BIOS COM slots swapped (a
Compaq's order, 115.1) - and then in 115.1.1's order:
  1. an AUTOMATIC search with no evidence is refused 2 and says how to name
     the port (rule 5) - the lease stays closed;
  2. Link > Connect COM2 is the user naming it, and is TAKEN (rule 2). This is
     the leg the old kernel fails;
  3. with COM1 having answered the reset edge like a mouse, Link > Connect
     COM1 is refused 3 even though the user named it (rule 1);
  4. and Link > Automatic then takes COM2 by itself, because the mouse is
     known to be elsewhere (rule 3).
Rule 4 (DSR/DCD driven) needs a device on the line, which MartyPC has not
got; it is the one rule this row cannot reach.
"""
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tests')]
import os88marty as M
import os88ui
import os88geom as geom
import dispapps
from wirefleet import artifact


def main():
    sy = dispapps._map('wirefleet')
    with os88ui.boot(artifact('os8088-360.img'), apps=artifact('wireflet360.img'),
                     machine='os8088_5150_cga_gla', quick=True) as ui:
        m = ui.m
        w = ui.path('B:/WIREFLET.O88')
        M.ui_passed(m)

        def val(name, n=1):
            seg = dispapps.pkg_seg(m, 0)[1]
            return int.from_bytes(m.readseg(seg, sy['wf_'+name], n), 'little')

        def wait(cond, what, guest=10):
            M.until(m, lambda _: cond(), what, guest=guest, poll=.02)

        wait(lambda: val('linkopen') == 1 and val('curport') == 2,
             'automatic search leased COM2 with the mouse settled')

        # 0. The Compaq Portable III's BIOS lists its COM bases in SETUP's
        #    order: swap the two BIOS slots and Connect COM2 (2F8, the free
        #    port here) must still be taken - it was refused "no UART".
        m.pause()
        bda = m.read(0x400, 4)
        m.write(0x400, struct.pack('<HH', 0x2f8, 0x3f8))
        m.run()
        ui.menu_pick('Link', 'Connect COM2')
        wait(lambda: val('linkopen') == 1 and val('curport') == 2,
             'Connect COM2 taken with the BIOS listing 2F8 in slot 0')
        assert val('message', 2) == sy['wf_searching'], hex(val('message', 2))
        # ...and with NO slot listing 2F8 the refusal says the BIOS is why
        # (9), not that there is no UART (4), and the lease stays closed.
        m.pause()
        m.write(0x400, struct.pack('<HH', 0x3f8, 0))
        m.run()
        ui.menu_pick('Link', 'Connect COM2')
        wait(lambda: val('message', 2) == sy['wf_nobios'], 'an unlisted 2F8 refused')
        assert val('linkopen') == 0
        m.pause()
        m.write(0x400, bda)
        m.run()

        # The machine with no serial mouse. Paused, so no pass sees half of it.
        m.pause()
        m.write(m.sym('mou_ident'), b'\0\0\0\0')
        m.write(m.sym('mou_idany'), b'\0')
        m.write(m.sym('mou_seen'), b'\0')
        m.write(m.sym('mou_hpst'), b'\0')
        m.write(m.sym('mou_ptr'), b'\0')    # 9.6.1.1: the keyboard mouse back
        m.run()

        cells = ui.menus()
        link = [c for c in cells if c[0].upper() == 'LINK']
        assert len(link) == 1, [c[0] for c in cells]
        _, x0, x1, items = link[0]
        names = [t.strip().upper() for t, _ in items]

        def kpick(item):
            """Link > item with the KEYBOARD mouse: Space over the title,
            arrows step by item while it is down (9.6.3), Space chooses."""
            k = names.index(item.upper())
            m.pause()
            m.write(m.sym('mouse_x'), struct.pack('<HH', (x0+x1)//2, geom.MBAR_H//2))
            m.run()
            m.key('Space')
            wait(lambda: ui._byte('menu_dropd') and ui._word('menu_y1'),
                 'the Link menu to drop off the keyboard mouse')
            for _ in range(len(names) + 2):
                if ui._byte('menu_sel') == k:
                    break
                m.key('ArrowDown')
                M.pace(m, .1)
            assert ui._byte('menu_sel') == k, ('menu_sel', ui._byte('menu_sel'), k)
            m.key('Space')
            wait(lambda: not ui._byte('menu_dropd'), 'the Link menu to close')
            # the pick's pass over; every read after it is memory. (A
            # settle here could run 360 guest s: the link's search message
            # keeps the picture moving)
            M.ui_passed(m)

        # 1. Link > Automatic closes the lease and searches again - and with
        #    no evidence on either port it must be refused and say so.
        kpick('Automatic')
        M.pace(m, 3.0)
        assert ui._byte('mou_seen') == 0, 'the real mouse spoke - the row is void'
        assert val('linkopen') == 0, 'an unsettled port was leased with no evidence'
        assert val('message', 2) == sy['wf_movemouse'], hex(val('message', 2))

        # 2. The user names COM2.
        kpick('Connect COM2')
        wait(lambda: val('linkopen') == 1, 'Connect COM2 taken before the mouse settled')
        assert val('curport') == 2
        assert m.read(m.sym('ser_row'), 2) == b'\2\0'
        assert val('message', 2) == sy['wf_searching'], hex(val('message', 2))

        # 3. COM1 answered the reset edge like a mouse: naming it is refused.
        m.pause()
        m.write(m.sym('mou_ident'), b'\1')
        m.write(m.sym('mou_idany'), b'\1')
        m.run()
        kpick('Connect COM1')
        M.pace(m, 1.0)
        assert val('linkopen') == 0, 'an identified mouse port was leased'
        assert val('message', 2) == sy['wf_mouseport'], hex(val('message', 2))
        assert m.read(m.sym('ser_row'), 2) == b'\xff\xff'

        # 4. ...and the search finds COM2 by itself now.
        kpick('Automatic')
        wait(lambda: val('linkopen') == 1 and val('curport') == 2,
             'automatic search took COM2, the mouse being on COM1')
        assert ui._byte('mou_seen') == 0, 'the real mouse spoke - the row is void'
        ui.close(w)
        assert m.read(m.sym('ser_row'), 2) == b'\xff\xff', 'lease outlived its window'
    print('wirefleetnomouse: ok - refused without evidence, taken when named, '
          'refused on the mouse, found beside it')


if __name__ == '__main__':
    main()
