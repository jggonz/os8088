#!/usr/bin/env python3
"""Native sampled weakness, reaction recovery and practice group acceptance."""
import argparse
import json
import os
from pathlib import Path
import sys
import time
import traceback
ROOT = Path(os.environ.get('OS88_SRC', Path(__file__).resolve().parents[1])).resolve()
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
import boratype_target as T
import os88sym
import os88ui
from os88fat import Fat12


def scenario(app, folder, ev):
    L, ui = app.L, app.ui
    app.press('Enter').wait_screen('PLAY')
    baseline = app.finish_passage()
    assert baseline['accuracy'] == 100 and baseline['wpm'] > 0
    app.press('KeyR').wait_screen('PLAY')
    def cooldown():
        ui._wait(lambda: ((ui._word('ticks') - app.global_word('bta_spoke')) & 65535) >= 219,
                 'real reaction cooldown', 30)
    def tag(wanted):
        ui._wait(lambda: app.global_word('bta_lasttag') == wanted,
                 'expected real reaction ' + str(wanted), 15)
    cooldown()
    for _ in range(3):
        app.key('x', False)
    tag(L['BT_R_ERROR']); app.shot(folder, '01-three-errors')
    for _ in range(16):
        app.key('x', False)
    cooldown()
    app.key(app.state()['passage'][0], True)
    tag(L['BT_R_LOW']); app.shot(folder, '02-low-accuracy')
    cooldown()
    for _ in range(9):
        state = app.state(); app.key(state['passage'][state['pos']], True)
    tag(L['BT_R_RECOVER']); app.shot(folder, '03-comeback')
    while app.state()['pos'] < app.state()['length'] - 1:
        state = app.state(); app.key(state['passage'][state['pos']], True)
    cooldown()
    state = app.state(); app.key(state['passage'][state['pos']], True)
    app.wait_screen('RESULTS'); result = app.state()
    assert result['errors'] == 19 and result['accuracy'] < 85
    assert not result['achievement_new'] & L['BT_ACH_BEST'], 'fixture unexpectedly set a personal speed best'
    tag(L['BT_R_POOR']); app.shot(folder, '04-poor-result')
    off = (ord('f') - 32) * 2
    assert app.pword('key_attempt', off) >= 20 and app.pword('key_error', off) == 19
    app.click_action(L['BT_A_WEAK']).wait_screen('PRACTICE')
    assert app.global_word('bt_practice_weak') == 1
    app.shot(folder, '05-sampled-weak-key')
    app.press('Enter').wait_screen('PLAY')
    weak = app.state()
    assert weak['mode'] == L['BT_PRACTICE'] and set(weak['passage']) <= set('f ')
    assert 'f' in weak['passage']
    for _ in range(8):
        state = app.state(); app.key(state['passage'][state['pos']], True)
    assert app.state()['net'] == 8 and not app.state()['errors']
    app.shot(folder, '06-weak-practice-input')
    app.press('F4').wait_screen('PAUSE'); app.press('KeyX').wait_screen('HOME')
    app.press('KeyP').wait_screen('PRACTICE')
    app.click_action(L['BT_A_PRACTICE_BASE'] + 2)
    assert app.global_word('bt_ch_diff') == 2 and not app.global_word('bt_practice_weak')
    app.shot(folder, '07-selected-digits')
    app.press('Enter').wait_screen('PLAY'); mixed = app.state()['passage']
    assert any(c.isdigit() for c in mixed) and set(mixed) <= set('0123456789 ')
    for _ in range(8):
        state = app.state(); app.key(state['passage'][state['pos']], True)
    assert app.state()['mode'] == L['BT_PRACTICE'] and app.state()['net'] == 8
    app.shot(folder, '07-digits-practice')
    ev.update(baseline=baseline, poor_result=result, weak_passage=weak['passage'], group_passage=mixed,
              reaction_tags=[L[n] for n in ('BT_R_ERROR','BT_R_LOW','BT_R_RECOVER','BT_R_POOR')])
    ui.close(app.win, limit=90)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--evidence', type=Path, default=ROOT / 'build/boratype-practice')
    args = ap.parse_args(); args.evidence = args.evidence.resolve(); args.evidence.mkdir(parents=True, exist_ok=True)
    T.CONSTANTS.extend(['BT_A_WEAK','BT_A_PRACTICE_BASE','BT_R_ERROR','BT_R_LOW','BT_R_RECOVER','BT_R_POOR','BT_ACH_BEST'])
    build = Path(os88sym.kernel_image()).parent
    image, disk = build / 'os8088-360.img', build / 'boratype360.img'
    assert Fat12(image).read('KERNEL.SYS') == (build / 'kernel.sys').read_bytes()
    assert Fat12(disk).read('BORATYPE.O88') == (build / 'boratype.o88').read_bytes()
    symbols, layout = T.app_symbols(build), T.target_layout(); os88sym.syms()
    ev = {'status':'RUNNING', 'app_build':str(build), 'package_sha256':T.digest(build/'boratype.o88'),
          'kernel_sha256':T.digest(build/'kernel.bin')}
    started = time.monotonic()
    try:
        with os88ui.boot(str(image), apps=str(disk), machine='os8088_xt_vga', quick=True) as ui:
            app = T.App(ui, T.open_package(ui,'B:/BORATYPE.O88'), symbols, layout, ev)
            try:
                scenario(app,args.evidence,ev)
            except BaseException:
                app.shot(args.evidence,'FAIL-current-guest',settled=False)
                ev['failure_state'] = app.state()
                raise
        ev['status'] = 'PASS'
    except BaseException as error:
        ev.update(status='FAIL', error=repr(error), traceback=traceback.format_exc())
        raise
    finally:
        ev['host_seconds'] = round(time.monotonic()-started,3)
        (args.evidence/'report.json').write_text(json.dumps(ev,indent=2)+'\n')
    print('boratype_practice: native weakness, recovery and both practice paths PASS')


if __name__ == '__main__':
    main()
