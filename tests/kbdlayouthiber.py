#!/usr/bin/env python3
"""An active optional CP850 table survives hibernate's original-font repair.

A private fixed disk contains the current kernel/modules and Note Pad. Test
both explicit Spanish input and BIOS input with a glyph-only document hold.
The module header/code, accented glyph and document must survive, followed
by another physical key. The old wake copied760 bytes into module offset0.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
sys.path.insert(0,str(ROOT/'tests'))
import os88build
import os88marty as M
import os88sym
import os88ui
import hibernate as H
from kbdsettings import KeyboardPage, require
from kbdlayouttext import Note


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--machine',default='os8088_xt_hdd')
    args=ap.parse_args()
    eq,syms=os88sym.equates(),os88sym.syms()
    build=Path(os88build.at('build')).resolve()
    with tempfile.TemporaryDirectory(prefix='kbd-hiber-',dir=build) as directory:
        scratch=Path(directory)
        for glyph_only in (False,True):
            vhd=scratch/('glyph.vhd' if glyph_only else 'layout.vhd')
            cfg=scratch/'SYSTEM.CFG'
            cfg.write_bytes(b'O88CFG\0\0\x03\0KL\x01\x01\x02\0\0')
            command=['python3','tools/os88hdd.py','--template',H.TEMPLATE,
                     '--out',str(vhd),'--kernel',H._k('build/kernel.sys'),
                     '--vbr',H._k('build/boothd.bin'),'--mbr',os88build.at('build/mbr.bin')]
            for name in ('HIBER','CTRL','FORMAT','DISKOPS','KEYBOARD'):
                command+=['--file',name+'.DRV='+H._k('build/'+name.lower()+'.drv')]
            command+=['--file','NOTEPAD.O88='+os88build.at('build/notepad.o88'),
                      '--file','SYSTEM.CFG='+str(cfg)]
            subprocess.run(command,cwd=ROOT,check=True)
            with M.launch(None,machine=args.machine,boot='up',
                          extra=['--mount','hd:0:'+str(vhd)]) as m:
                M.no_saver(m)
                ui=os88ui.UI(m)
                page=KeyboardPage(ui,eq,syms)
                require(page.layout()==2,'saved Spanish did not activate on fixed disk')
                note=Note(ui,ui.path('C:/NOTEPAD.O88'),
                          Path(os88build.at('build/notepad.bin')).resolve())
                ui.press('Semicolon',need=True)
                note.expect(b'\xa4')
                if glyph_only:
                    page.open();page.pick(0,expected=0);page.close();note.front()
                def snapshot():
                    seg=page.word('mod_r_keyboard')
                    require(seg,'live glyph document lost its provider')
                    return (bytes(m.readseg(seg,0,96)),
                            bytes(m.readseg(seg,syms['klf_table']+(0xa4-32)*8,8)))
                before=snapshot()
                require(page.byte('font_last')==255,'provider was not active before hibernate')
                ui.menu_pick('Apple','Hibernate')
                ui.wait_window('Hibernate')
                ui.press('Enter',done=False)
                M.until(m,lambda mm:M.video_is_text(mm.video()),
                        'hibernation to reach the ROM sentence',guest=300)
                try:
                    H.restart(m,lambda mm:H.word(mm,'hb_resumes')==1,
                              'the keyboard session to resume')
                except M.MartyError:
                    after = snapshot()
                    require(after == before,
                            "resume overwrote KEYBOARD header/code or glyph: " +
                            before[0][:16].hex() + " -> " + after[0][:16].hex())
                    raise
                M.ui_done(m,'font repair and hibernate wake to finish')
                require(snapshot()==before,'hibernate damaged keyboard header/code or enie glyph')
                require(page.byte('font_last')==255,'resume left the extended provider suspended')
                require(page.layout()==(0 if glyph_only else 2),'resume changed input policy')
                note.expect(b'\xa4');note.front()
                ui.press('KeyA',need=True) if glyph_only else ui.press('Semicolon',need=True)
                note.expect(b'\xa4a' if glyph_only else b'\xa4\xa4')
                print('hibernate:', 'glyph-only BIOS' if glyph_only else 'Spanish layout',
                      'header/glyph/document preserved and physical input works')
    print('kbdlayouthiber: PASS')


if __name__=='__main__':main()
