#!/usr/bin/env python3
"""WireArena's match setup on the glass (docs/plans/completed/WIREARENA-NET-PLAN.md
9.4), MartyPC's 5150 with a CGA, the shipped package.

The owner saw the whole window flash on every click and key: every row and
both buttons were redrawn for each input. This row holds the fix in the
flicker instrument's units (PERFORMANCE.md 3.1), with the pointer parked
outside the window so it is not counted:

  - A KEY: Down moves the focus - the screen settles within 6 frames and no
    pixel is TRANSIENT (drawn and then drawn again);
  - THE SAME PICTURE BACK: Up again, and the window is pixel-identical to
    the first capture (nothing left over, nothing missing);
  - THE KEYS CARD: K puts the About card up, a key takes it down, and the
    window is pixel-identical again - the page's own repaint covers every
    pixel the card was on, with no white first (the ground fills only the
    gaps between controls);
  - A DROP-DOWN: a click on Arena's box drops its list (the
    record says open), a click outside it takes it down without a pick,
    and the window is pixel-identical again;
  - ONE BOT MEANS ONE: Bots 1 picked from the keyboard and Play - for ten
    guest seconds the other two figures never leave their parking.

Registered as wasetup in soak; make wirearenadisk first.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, 'tools'), os.path.join(ROOT, 'tests')]
import os88marty                                                # noqa: E402
import os88ui                                                   # noqa: E402
from os88fixture import need                                    # noqa: E402
from walift import amap, SYS, APPS                              # noqa: E402

fails = []


def check(ok, what):
    print('  %s %s' % ('ok  ' if ok else 'FAIL', what), flush=True)
    if not ok:
        fails.append(what)


def main():
    need(APPS)
    sym = amap()
    with os88ui.boot(SYS, apps=APPS, machine="os8088_5150_cga",
                     quick=True) as ui:
        m = ui.m
        w = ui.path("B:/WIRERENA.O88")
        p = os88marty.Pkg(m, w)
        wa = [x for x in ui.windows() if x.title == 'Wire Arena'][0]
        ui.mo.to(4, 190)                    # (the pointer off the window)
        ui.settle()

        def region():
            W, H, d = m.fbuf()
            out = bytearray()
            for y in range(wa.y, wa.y + wa.h):
                o = (y * W + wa.x) * 3
                out += d[o:o + wa.w * 3]
            return bytes(out)
        base = region()

        m.pause()
        m.key("ArrowDown")
        r = m.flicker(frames=60)
        m.go()
        moved = [i for i, f in enumerate(r['per_frame']) if f['changed']]
        check(r['settled'] and r['worst_transient'] == 0 and moved and
              moved[-1] <= 6, 'A KEY: Down settles in %s frames, %d '
              'transient pixels (was every row and both buttons)'
              % (moved[-1] if moved else None, r['worst_transient']))
        check(p.byte(sym['wa_sfoc']) == 1, 'A KEY: the focus on Columns')
        m.key("ArrowUp")
        ui.settle()
        check(region() == base, 'THE SAME PICTURE BACK: Up again, the window '
              'pixel-identical')

        m.key("KeyK")
        ui.settle()
        check(p.byte(sym['wa_abon']) == 1 and region() != base,
              'THE KEYS CARD: K puts it up')
        m.key("KeyX")
        ui.settle()
        check(p.byte(sym['wa_abon']) == 0 and region() == base,
              'THE KEYS CARD: a key takes it down, the window pixel-'
              'identical')

        ox, oy = wa.x + 1, wa.y + 18
        dr = sym['wa_sdr_a']            # (the layout's places, the guest's
        gy = p.word(dr + 2) - oy        # own: the Arena box's top and the
        by = p.word(sym['wa_sby'])      # bottom row's - a CGA's content is
                                        # fitted shorter, its group higher)
        check(p.byte(sym['wa_slay']) == 0 and
              abs((gy - 12) - (by - gy - 70)) <= 1 and by + 20 == wa.h - 19,
              'THE LAYOUT, ALONE: the drop-downs centred between row 0 and '
              'the bottom row (%d above, %d under), Nick, Play and Keys 4 '
              'from the bottom (the owner, before release)'
              % (gy - 12, by - gy - 70))
        dr = sym['wa_sdr_a']

        def under(x0, y0, x1, y1):
            W, H, d = m.fbuf()
            return [d[(y * W + x0) * 3:(y * W + x1) * 3]
                    for y in range(y0, y1)]
        # (the list's place: under the Arena box, the box's width)
        lrect = (ox + 57, oy + gy + 18, ox + 182, oy + gy + 60)
        closed = under(*lrect)
        ui.mo.click(ox + 120, oy + gy + 8)
        os88marty.until(m, lambda _: p.byte(dr + 16) == 1,
                        'the Arena list open', guest=5)
        check(p.byte(dr + 16) == 1, 'A DROP-DOWN: a click drops its list')
        ui.settle()
        nrect = (ox + 57, oy + gy + 18, ox + 182, oy + gy + 70)
        listpic = under(*nrect)         # (down to the nick's row, for the
        rows = sum(a != b for a, b in zip(under(*lrect), closed))
        check(p.byte(dr + 16) == 1 and rows >= 30,
              'A DROP-DOWN: the list still OPEN and ON THE GLASS once the '
              'window settles (%d of %d rows changed under the box; the CTF '
              'focus mapping\'s compare once set drpress\'s CF and the list '
              'was painted over at once, its record left open)'
              % (rows, len(closed)))
        ui.mo.click(ox + 300, oy + 120)
        os88marty.until(m, lambda _: p.byte(dr + 16) == 0,
                        'the Arena list down', guest=5)
        ui.mo.to(4, 190)
        ui.settle()
        check(p.byte(sym['wa_lkarena']) == 2 and region() == base,
              'A DROP-DOWN: a click outside takes it down, no pick, the '
              'window pixel-identical')

        # THE NICK UNDER A LIST (the owner, before release): with the focus
        # in the nick, a click on the Arena box drew the field back over its
        # list, and the list's bank put the field's caret back when it went
        ui.mo.click(ox + 120, oy + by + 8)
        os88marty.until(m, lambda _: p.byte(sym['wa_sfoc']) == 8,
                        'the nick focused', guest=5)
        ui.mo.to(4, 190)
        ui.settle()
        ui.mo.click(ox + 120, oy + gy + 8)
        os88marty.until(m, lambda _: p.byte(dr + 16) == 1,
                        'the Arena list open again', guest=5)
        ui.settle()
        check(under(*nrect) == listpic,
              'THE NICK UNDER A LIST: the Arena list over the focused nick '
              'as it is over nothing (the field not drawn back over it)')
        ui.mo.click(ox + 300, oy + 120)
        os88marty.until(m, lambda _: p.byte(dr + 16) == 0,
                        'the Arena list down again', guest=5)
        ui.mo.to(4, 190)
        ui.settle()
        check(p.byte(sym['wa_sfoc']) == 0 and region() == base,
              'THE NICK UNDER A LIST: the list down, the focus on Arena and '
              'the window pixel-identical - no caret left in the nick')

        # ONE BOT MEANS ONE: Bots to 1 from the keyboard (Arena, Columns,
        # Bots; 3, 2, 1), Enter - and for ten guest seconds only figure 0
        # is ever on the board (b38a065a's fast tick left the other two
        # running: the owner saw two bots, and a "respawn" at once)
        for k in ("ArrowDown", "ArrowDown", "ArrowLeft", "ArrowLeft"):
            m.key(k)
        ui.settle()
        check(p.byte(sym['wa_lkbots']) == 1, 'ONE BOT: the setup says 1')
        f0 = p.byte(sym['wa_frame'])
        m.key("Enter")
        os88marty.until(m, lambda _: p.byte(sym['wa_frame']) != f0,
                        'the match running', guest=20)
        figs = sym['wa_figs']
        seen = set()
        for _ in range(20):
            m.advance(cycles=4772727 // 2)
            dead = [p.byte(figs + i * 56 + 17) for i in range(3)]
            seen |= {i for i in (1, 2) if dead[i] != 250}
        check(not seen and p.byte(sym['wa_lknb']) == 1,
              'ONE BOT: figures 1 and 2 off the board for ten seconds (%s '
              'were on it)' % (sorted(seen) or 'none'))
    if fails:
        print('wasetup: FAIL - %d of its checks' % len(fails))
        sys.exit(1)
    print('wasetup: ok - the match setup draws what changed, once')


if __name__ == '__main__':
    main()
