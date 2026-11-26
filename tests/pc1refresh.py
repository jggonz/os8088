#!/usr/bin/env python3
"""Exercise the compact PC1 controls and SYSTEM.CFG on a CGA guest.

MartyPC has no PC1. After boot, replace ONLY CTRL.DRV's hardware identity
probe with CLC/RET, explicitly a synthetic PC1 UI fixture. The real probe and
I/O protocol are executed separately by pc1video. Reboot the saved disk on
the unmodified non-PC1 guest to check preservation and refusal.
"""
import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
import dispcp
import os88flush
import os88marty
import os88sym
import os88ui


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--shots', type=pathlib.Path)
    args = ap.parse_args()
    eq = os88sym.equates()
    offsets = os88sym.syms()
    saved = ROOT / 'build/pc1-refresh-written.img'
    with os88ui.boot('build/os8088-360.img', machine='os8088_5150_cga_gla') as ui:
        m, mo, S = ui.m, ui.mo, ui.sym
        cfg = S('drv_cfg') + eq['CFG_PC1']
        assert m.read(cfg, 1) == b'\0'
        # A normal one-card CGA guest offers no PC1-only Display page.
        assert m.read(S('cp_hide'), 1)[0] & (1 << eq['CP_IVID'])
        if args.shots:
            args.shots.mkdir(parents=True, exist_ok=True)
        m.pause()
        hide = m.read(S('cp_hide'), 1)[0]
        m.write(S('cp_hide'), bytes([hide & ~(1 << eq['CP_IVID'])]))
        m.write(S('cp_nst'), bytes([m.read(S('cp_nst'), 1)[0] + 1]))
        m.run()
        dispcp.open_panel(m, mo, S, os88marty.settle)
        os88marty.settle(m)
        if args.shots:
            os88marty.write_png_rgb(str(args.shots / 'cga-non-pc1-display.png'), *m.fbuf(0))
        m.pause()
        segment = int.from_bytes(m.read(S('mod_tab') + eq['MOD_CTRL'] * eq['MODR_SIZE'], 2), 'little')
        probe = (segment << 4) + offsets['cp_pc1_probe']
        assert m.read(probe, 2) == b'\x80\x3e', 'probe entry moved'
        m.write(probe, b'\xf8\xc3')  # synthetic identity, never shipped
        m.run()
        # Re-select Display through another page to paint the synthetic face.
        dispcp.open_panel(m, mo, S, os88marty.settle, page=0)
        dispcp.open_panel(m, mo, S, os88marty.settle)
        os88marty.settle(m)
        for hz in (60, 50, 60):
            wx, wy = dispcp._cp_win(m, S)
            x = eq['CP_RX'] + eq['CPV_P1X1' if hz == 60 else 'CPV_P1X0'] + 6
            y = eq['CPV_P1Y'] + 6
            mo.click(wx + 1 + x, wy + eq['TITLE_H'] + 1 + y, settle=0)
            os88marty.until(m, lambda _: m.read(cfg, 1) == bytes([hz]),
                           f'PC1 preference {hz} Hz', guest=20)
            assert m.read(S('cp_wdirty'), 1) == b'\x01'
        os88marty.settle(m)
        if args.shots:
            os88marty.write_png_rgb(str(args.shots / 'cga-pc1-fixture-60hz.png'), *m.fbuf(0))
        dispcp.close_panel(m, mo, S, os88marty.settle)
        f = os88flush.Flush(marty=m)
        data = f.volume(0).read('SYSTEM.CFG')
        assert b'P1\x01\x01\x3c' in data, 'P1 record not saved'
        f.save(0, str(saved))
    with os88ui.boot(str(saved), machine='os8088_5150_cga_gla') as ui:
        m, S = ui.m, ui.sym
        assert m.read(S('drv_cfg') + eq['CFG_PC1'], 1) == b'\x3c'
        assert m.read(S('cp_hide'), 1)[0] & (1 << eq['CP_IVID'])
    print('pc1refresh: synthetic CGA controls 60/50/60, save, reboot and non-PC1 refusal passed')


if __name__ == '__main__':
    main()
