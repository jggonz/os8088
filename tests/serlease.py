#!/usr/bin/env python3
"""A serial lease taken BEFORE the mouse contest settles survives the settle
(SPEC.md 115.1.1, 115.2, 9.5).

Registered as serlease in soak. The field case: a machine boots with no mouse
moving, WireFleet leases COM2 (IRQ3), a serial mouse on COM1 (IRQ4, a
DIFFERENT line) is then moved, the contest settles, and mou_lockon retires
every port but the mouse's - which before this row masked IRQ3 at the 8259
for the rest of the lease, leaving the link serviced only by srl_pump inside
each Read/Write (an original 8250 at 2400 baud drops bytes). And when the
lease then closed, srl_close put back the IER and the PIC bit it banked at
OPEN - the contest's armed, unmasked state - re-arming a port the contest
had retired.

Every MartyPC machine here has a mouse on COM1 and opening the package by
name moves it, so the row puts the machine back into "nobody has spoken" the
way wirefleetnomouse does - [mou_seen], [mou_hpst], [mou_ident], [mou_idany],
[mou_ptr] - AND puts COM2's hardware back the way mouse_init leaves a port no
mouse has retired (IER = RX, IRQ3 unmasked), because the boot's settle has
already retired it here and a no-mouse boot never does. Then, on the keyboard
mouse:

  A. Link > Connect COM2 is taken while unsettled (115.1.1 rule 2);
  B. while still unsettled, mou_hotplug's reset cycle drops DTR/RTS on COM1
     (the positive control: the cycle ran) and NOT on the leased COM2 -
     mou_pall reaches the UARTs through mou_pout, which skips [ser_row];
  C. the real mouse is moved, the contest settles on COM1 and mou_lockon
     runs: COM2's IRQ3 must still be UNMASKED with its IER still armed, the
     lease still held, and the mouse must still move the pointer;
  D. WireFleet is closed, which releases the lease: COM2 must now be RETIRED
     as mou_lockon leaves a loser - IER 0, IRQ3 masked (its line is not the
     mouse's) - and the mouse still works.

MartyPC because it is the 8088 with both UARTs, a serial mouse on COM1 and
COM2 on its own line, and `inb` reads the 8259's mask and the UARTs'
registers without side effects; this is not one of the closed list's QEMU
cases (no cross-wired card, no modem - nothing is on COM2 at all).
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

COM1, COM2 = 0x3F8, 0x2F8       # the machine's UARTs: mou_bases' two rows
IRQ3, IRQ4 = 0x08, 0x10         # their 8259 bits (MOU_L3, MOU_L4)


def main():
    fails = []

    def check(ok, what):
        if not ok:
            fails.append(what)
            print('FAIL', what)

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

        def row():
            return int.from_bytes(m.read(m.sym('ser_row'), 2), 'little')

        wait(lambda: val('linkopen') == 1 and val('curport') == 2,
             'automatic search leased COM2 with the mouse settled')

        # The machine with no serial mouse, as wirefleetnomouse makes it.
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
            """Link > item with the KEYBOARD mouse (wirefleetnomouse's)."""
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
            M.ui_passed(m)

        kpick('Disconnect')
        wait(lambda: val('linkopen') == 0 and row() == 0xFFFF, 'the lease closed')

        # ...and COM2 as a no-mouse boot leaves it: mouse_init armed RX on
        # every probed port and unmasked its line; only a SETTLE retires one,
        # and the boot's settle (the package opened by the mouse) did that
        # here already.
        m.pause()
        m.outb(COM2 + 1, 0x01)
        m.outb(0x21, m.inb(0x21) & ~IRQ3)
        m.run()
        assert m.inb(COM2 + 1) & 1 and not m.inb(0x21) & IRQ3, 'the fixture'

        # A. Named, unsettled: taken.
        kpick('Connect COM2')
        wait(lambda: val('linkopen') == 1 and row() == 2,
             'Connect COM2 taken before the mouse settled')
        assert ui._byte('mou_seen') == 0, 'the real mouse spoke - the row is void'

        # B. mou_hotplug's reset cycle, caught in state 1 (DTR/RTS held low).
        seen = {}

        def dropped():
            # ONE INSTANT OF GUEST TIME: the state and both MCRs are read
            # with the guest paused. Read on a running guest they were three
            # moments, and a loaded host let mou_hotplug leave state 1 between
            # the first and the second - COM1 read back raised (MCR 0b) in a
            # soak, a cycle that had dropped it correctly
            m.pause()
            try:
                if ui._byte('mou_hpst') != 1:
                    return False
                seen['com1'] = m.inb(COM1 + 4)
                seen['com2'] = m.inb(COM2 + 4)
                return True
            finally:
                m.run()
        wait(dropped, "mou_hotplug's reset drop (state 1)", guest=8)
        check(seen['com1'] & 3 == 0,
              'B: the reset cycle did not drop COM1 (MCR %02x) - the control' % seen['com1'])
        check(seen['com2'] & 3 == 3,
              'B: the reset cycle dropped DTR/RTS on the LEASED COM2 (MCR %02x)' % seen['com2'])
        wait(lambda: ui._byte('mou_hpst') == 3, "mou_hotplug's raise", guest=4)
        check(m.inb(COM2 + 4) & 3 == 3, 'B: COM2 lost DTR/RTS across the cycle')

        # C. The mouse is moved on COM1: past the identify drain, then
        #    packets until the contest settles and the UI pass retires.
        M.pace(m, 1.5)
        for _ in range(80):
            if ui._byte('mou_seen'):
                break
            m.mouse(4, 0)
            M.pace(m, .05)
        wait(lambda: ui._byte('mou_seen') == 1, 'the contest to settle', guest=6)
        wait(lambda: ui._byte('mou_hpst') == 2, 'mou_lockon to retire the losers', guest=6)
        check(ui._byte('mou_port') == 0, 'C: the mouse settled on row %d' % ui._byte('mou_port'))
        imr = m.inb(0x21)
        check(not imr & IRQ3, 'C: the LEASED COM2 line IRQ3 masked by the settle (IMR %02x)' % imr)
        check(m.inb(COM2 + 1) & 1, 'C: the leased COM2 IER disarmed (%02x)' % m.inb(COM2 + 1))
        check(not imr & IRQ4, "C: the mouse's own line IRQ4 masked (IMR %02x)" % imr)
        check(row() == 2 and val('linkopen') == 1, 'C: the lease was lost')
        x = ui._word('mouse_x')
        for _ in range(10):
            m.mouse(5, 0)
            M.pace(m, .05)
        wait(lambda: ui._word('mouse_x') != x, 'C: the mouse to move the pointer', guest=4)

        # D. Release: COM2 must be left as the contest retired it.
        ui.close(w)
        wait(lambda: row() == 0xFFFF, 'the lease released with its window')
        imr = m.inb(0x21)
        check(m.inb(COM2 + 1) == 0,
              'D: COM2 re-armed at close (IER %02x) - a retired port' % m.inb(COM2 + 1))
        check(imr & IRQ3, 'D: IRQ3 unmasked at close (IMR %02x) - a retired line' % imr)
        check(not imr & IRQ4, "D: the mouse's IRQ4 masked (IMR %02x)" % imr)
        x = ui._word('mouse_x')
        for _ in range(10):
            m.mouse(-5, 0)
            M.pace(m, .05)
        wait(lambda: ui._word('mouse_x') != x, 'D: the mouse to move the pointer', guest=4)

    if fails:
        print('serlease: FAIL - %d: %s' % (len(fails), '; '.join(fails)))
        sys.exit(1)
    print('serlease: ok - an unsettled lease kept its line and its control '
          'lines through the settle, and was left retired at close')


if __name__ == '__main__':
    main()
