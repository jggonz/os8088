#!/usr/bin/env python3
"""Production cold-menu geometry and selection; no guest-pixel claim."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import time
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'apps/boratype'
def run(tree,exe):
    subprocess.run([os.environ.get('CC','cc'),'-std=c99','-O1','-g','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-fno-sanitize-recover=all','-I',str(ROOT/'apps/cc'),
                    str(tree/'hosttest/menu_test.c'),'-o',str(exe)],check=True)
    return subprocess.run([str(exe)],text=True,capture_output=True,
                          env=dict(os.environ, BT_ANIM_DIR=str(exe.parent)))
def main():
    started=time.monotonic()
    with tempfile.TemporaryDirectory(prefix='boratype-menus-') as tmp:
        work=Path(tmp)
        subprocess.run([sys.executable, str(ROOT/'tools/os88btanim.py'), 'build',
                        '--out0', str(work/'BTAN0.BIN'), '--out1', str(work/'BTAN1.BIN'),
                        '--out2', str(work/'BTAN2.BIN')], check=True)
        positive=run(SOURCE,work/'menus')
        assert positive.returncode==0,positive.stdout+positive.stderr
        print(positive.stdout,end='')
        tree=work/'broken';(tree/'hosttest').mkdir(parents=True)
        for name in ('bt_face.c','bt_menus.c','bt_face.h','bt.h','art.h'):
            shutil.copyfile(SOURCE/name,tree/name)
        for name in ('face_test.c','menu_test.c'):
            shutil.copyfile(SOURCE/'hosttest'/name,tree/'hosttest'/name)
        p=tree/'bt_menus.c';s=p.read_text();needle='first=(bt_selection/rows)*rows;'
        assert s.count(needle)==1
        p.write_text(s.replace(needle,'first=0;'))
        broken=run(tree,work/'broken-menus')
        assert broken.returncode!=0,'selected lesson vanished off-page without rejection'
    from boratype_badges import main as badge_check
    badge_check()
    from boratype_settings import main as settings_check
    settings_check()
    print(f'boratype_menus: PASS; fixed-first-page mutation rejected ({time.monotonic()-started:.2f}s)')
if __name__=='__main__':main()
