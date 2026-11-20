#!/usr/bin/env python3
"""Active international translation through the 8042's saved INT15 FLAGS.

QEMU is the instrument for its read-advances 8042. A private system disk
selects French at boot, then whole make/break and AltGr sequences are queued
in single QMP commands. Inspect BIOS words AND the physical key-state map.
This catches a second port60 read or a lost consumed carry in the ROM frame.
"""
from pathlib import Path
import argparse
import os
import shutil
import sys
import tempfile

HERE=Path(__file__).resolve().parent
ROOT=HERE.parent
sys.path.insert(0,str(ROOT/'tools'))
import os88build
import os88fat
import os88qemu
import os88sym
from kbd4f import Q, ring


def require(ok, why):
    if not ok:raise AssertionError(why)


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--negative-carry", action="store_true")
    args=ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='kbd-at-',dir=os88qemu.RUNDIR) as tmp:
        image=Path(tmp)/'source.img'
        shutil.copyfile(os88build.at('build/os8088.img'),image)
        cfg=Path(tmp)/'cfg.bin'
        cfg.write_bytes(b'O88CFG\0\0\x03\0KL\x01\x01\x04\0\0')
        fat=os88fat.Fat12(str(image));fat.delete('SYSTEM.CFG')
        fat.add(str(cfg),'SYSTEM.CFG');fat.save()
        work=Path(tmp)/'vm';work.mkdir()
        inst=os88qemu.Private(str(work),str(image),os88build.at('build/apps.img'))
        q=Q(inst.sock)
        try:
            sym=os88sym.linear
            require(os88qemu.kernel_up(q,sym),'kernel did not boot')
            seg=q.word(sym('mod_r_keyboard'))
            require(seg,'saved French selection did not load KEYBOARD.DRV')
            require(q.read(seg*16+os88sym.syms()['klm_layout'],1)==b'\x04',
                    'saved layout is not French')
            if args.negative_carry:
                start=sym('kbm_isr.kleat')
                body=q.read(start,32)
                anchor=b'\x36\x83\x66\x0a\xfe'
                at=body.find(anchor)
                require(at>=0,'saved-carry negative-control anchor moved')
                os88qemu.poke(q.hmp,start+at,b'\x90'*len(anchor))
                print('negative control: consumed INT15 carry left set; expecting duplicate')
            dmap=sym('kbd_dnmap')
            def bit(scan):return bool(q.read(dmap+(scan>>3),1)[0]&(1<<(scan&7)))
            def event(evs, expected, up, what):
                start=q.word(0x41c)
                q.keys(evs)
                os88qemu.acted(q,lambda:len(ring(q,start))>=len(expected)
                               and all(not bit(s) for s in up),secs=3,
                               what=what,poll=0.01)
                os88qemu.ui_done(q,sym)
                # The ring retains words after ui_task consumes them. Waiting
                # for the guest UI pass also gives a duplicate its chance.
                actual=ring(q,start)
                require(actual==expected,f'{what}: {actual!r} != {expected!r}')
            event([('q',True),('q',False)],[0x1061],[0x10],'French Q make/break')
            event([('a',True),('a',False),('w',True),('w',False)],
                  [0x1e71,0x117a],[0x1e,0x11],'two translated letters')
            event([('2',True),('2',False)],[0x0382],[3],'direct e-acute')
            event([('alt_r',True),('0',True),('0',False),('alt_r',False)],
                  [0x0b40],[0x38,0x0b],'batched right AltGr')
            event([('ctrl',True),('alt',True),('0',True),('0',False),
                   ('alt',False),('ctrl',False)],
                  [0x0b40],[0x1d,0x38,0x0b],'XT Ctrl+Alt alias')
            event([('bracket_left',True),('bracket_left',False),
                   ('e',True),('e',False)],[0x1288],[0x1a,0x12],'circumflex composition')
            print('kbdlayoutat: queued 8042 input translated exactly once; physical map released')
        finally:inst.close()


if __name__=='__main__':main()
