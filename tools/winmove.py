#!/usr/bin/env python3
"""What does dragging a window BY ITS TITLE BAR cost the window itself?
(SPEC.md 11.96.12)

    python3 tools/winmove.py sol|disk [machine]

Not a card drag and not another window dragged off it: the window you are
holding, moved. It is the FRONT window by construction (ui_drag is reached
through wm_hit on the frontmost title bar), so SPEC.md 11.96.4 has already
dropped its cache - and `wm_su_ck` compares the ABSOLUTE content rect, which a
move changes in all four numbers. Either reason alone is enough; together they
mean no window can use the raise cache on a move. This counts what draws
instead.

Measured, cycle-accurate 5150/Hercules:

    disk    207 calls,  236.6 ms   (71 font_char - the listing re-lettered)
    sol   1,016 calls,  914.7 ms   (22 gfx_blit4 - every card back again)

`wm_su_try` appears in both and `gfx_restore` in neither, which is the whole
finding in two rows.

WHY THIS IS ITS OWN TOOL AND NOT AN os88span SCENARIO. os88span prices a SPAN
between two named symbols and its collector stops at the first gap; this
question is a COUNT of everything a burst issued, and a burst that is mostly
one primitive repeated is exactly what it is bad at. Two scenarios were
written there first and one of them silently returned three hits for an
operation that issued 207 - a harness that stops early reads as an operation
that did not happen, which is the failure this tree keeps paying for.
"""
import os
import sys
import time

ROOT = '/home/user/os8088'
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import os88marty
from os88mouse import Mouse
import sucheck as su
import subcheck as sc

CLK = 4772727.0
QUIET = int(0.40 * CLK)

PRIMS = ['gfx_pixel', 'gfx_hline', 'gfx_vline', 'gfx_fill', 'gfx_frame',
         'gfx_fill_gray', 'gfx_xor_rect', 'gfx_xor_fill', 'gfx_blit4',
         'font_char', 'font_run', 'gfx_line', 'gfx_restore', 'gfx_save',
         'wm_su_try', 'wm_su_bank', 'wm_draw_win']


def burst(m, by, budget=300.0, limit=4000):
    out, t0, last = [], time.time(), None
    for _ in range(limit):
        m.run()
        st = None
        while time.time() - t0 < budget:
            st = m.status()
            if st.get('state') == 'breakpoint':
                break
            time.sleep(0.004)
        if not st or st.get('state') != 'breakpoint':
            break
        cyc = st['cycles']
        if last is not None and cyc - last > QUIET:
            break
        out.append((by.get(((st['cs'] << 4) + st['ip']) & 0xFFFFF, '?'), cyc))
        last = cyc
    return out


def main():
    which = sys.argv[1] if len(sys.argv) > 1 else 'sol'
    machine = sys.argv[2] if len(sys.argv) > 2 else 'os8088_5150_herc_gla'
    apps = 'soltest.img' if which == 'sol' else 'apps360.img'
    with os88marty.launch(os.path.join(ROOT, 'build/os8088-360.img'),
                          apps=os.path.join(ROOT, 'build', apps),
                          machine=machine) as m:
        mo = Mouse(marty=m)
        mo.dblclick(*su.zone(m, 1))
        time.sleep(4)
        win = [w for w in su.windows(m) if w.visible][0]
        if which == 'sol':
            mo.dblclick(*su.row(win, 0))
            time.sleep(25)
            win = su.named(m, 'SOL')
        os88marty.settle(m)
        win = [w for w in su.windows(m) if w.visible and w.i == win.i][0]
        print('dragging %r by its title bar' % (win,))

        by = {m.sym(n): n for n in PRIMS}
        p = sc.titlebar(m, win)
        mo.to(*p)
        time.sleep(0.4)
        mo._edge(True)
        mo.to(p[0] - 60, p[1] + 40, l=True)
        time.sleep(1.0)

        m.breakpoints([{'type': 'exec', 'addr': a} for a in by])
        mo._pk(l=False)                     # the release: the repaint happens
        hits = burst(m, by)
        m.breakpoints([])
        m.run()

        if not hits:
            print('nothing drew at all')
        else:
            span = hits[-1][1] - hits[0][1]
            counts = {}
            for n, _ in hits:
                counts[n] = counts.get(n, 0) + 1
            print('\n%d calls over %.1f ms of guest time' %
                  (len(hits), 1000.0 * span / CLK))
            for k in sorted(counts, key=lambda k: -counts[k]):
                print('   %-14s %d' % (k, counts[k]))
        m.quit()


if __name__ == '__main__':
    sys.exit(main() or 0)
