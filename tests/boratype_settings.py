#!/usr/bin/env python3
"""Host-spy the exact production native action dispatcher; never boots."""
from pathlib import Path
import os
import subprocess
import tempfile
import time
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'apps/boratype'
def run(source,directory,name):
    c=directory/(name+'.c');exe=directory/name;c.write_text(source)
    subprocess.run([os.environ.get('CC','cc'),'-std=c99','-O1','-g','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-fno-sanitize-recover=all','-I',str(SOURCE),
                    str(c),'-o',str(exe)],check=True)
    return subprocess.run([str(exe)],text=True,capture_output=True)
def main():
    started=time.monotonic();platform=(SOURCE/'bt_platform.c').read_text()
    start=platform.index('void bt_action(int a)');end=platform.index('static void ovl_bt_menu_key',start)
    body=platform[start:end];fixture=(SOURCE/'hosttest/settings_test.c').read_text()
    assert fixture.count('/* BT_ACTION_BODY */')==1
    with tempfile.TemporaryDirectory(prefix='boratype-settings-') as tmp:
        d=Path(tmp);positive=run(fixture.replace('/* BT_ACTION_BODY */',body),d,'settings')
        assert positive.returncode==0,positive.stdout+positive.stderr;print(positive.stdout,end='')
        needle='a == BT_A_FULL || bt_full != (a == BT_A_FULLSCREEN)'
        assert body.count(needle)==1,'mode-request guard anchor moved'
        broken=run(fixture.replace('/* BT_ACTION_BODY */',body.replace(needle,'1')),d,'broken')
        assert broken.returncode!=0,'same-state mode request toggled without rejection'
    print(f'boratype_settings: PASS; unconditional-toggle mutation rejected ({time.monotonic()-started:.2f}s)')
if __name__=='__main__':main()
