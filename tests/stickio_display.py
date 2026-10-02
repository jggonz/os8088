#!/usr/bin/env python3
"""Stickio: VGA CGA-mode raster and desktop restore on QEMU (SPEC.md 103)."""
from pathlib import Path
import struct, subprocess
from stickio import ROOT, symbols

def qemu_display():
    import ethernet as E
    import os88qemu
    import os88sym
    import dispcp
    import os88geom as G
    from PIL import Image
    out=ROOT/'build/stickio-proof';out.mkdir(exist_ok=True)
    os88qemu.own()
    subprocess.run(["make","test","TESTIMG=build/os8088-360.img",
                    'TESTAPPS=build/stickio360.img'],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
    m=E.Qemu();mo=E.Mouse();S=os88sym.linear
    try:
        os88qemu.pace(m,8);E.settle(m);dispcp.open_drive(m,mo,S,E.settle,'B')
        w=next(w for w in G.windows(m,S) if w.visible and w.title=='Disk')
        dispcp.open_named(m,mo,S,E.settle,w.x,w.y,'STICKIO.O88')
        win=next(w for w in G.windows(m,S) if w.visible and w.title=='Stickio')
        base=struct.unpack_from('<H',m.read(S('wm_wins'),G.MAX_WIN*G.WIN_SIZE),win.i*G.WIN_SIZE+G.W_SEG)[0]<<4
        sym=symbols();m.hmp('sendkey ret');os88qemu.pace(m,2)
        assert m.read(base+sym['st_fs'],1)==b'\x01'
        m.hmp('sendkey p');os88qemu.pace(m,.5)
        path=out/'vga-qemu.ppm';m.hmp('screendump "'+str(path)+'"')
        im=Image.open(path).convert('RGB');assert im.size in ((320,200),(640,400)),im.size
        im=im.resize((320,200),Image.Resampling.NEAREST)
        colors=im.getcolors(64000);assert len(colors)==2 and set(c for _,c in colors)=={(0,0,0),(255,255,255)},colors
        # Terrain outside the stationary figure matches the independently packed background.
        bg=m.read(base+sym['st_bg'],80*128)
        for y in range(128):
            for x in range(320):
                if 32<=x<48 and 72<=y<96:continue
                expected=255 if (bg[y*80+x//4]>>(6-2*(x%4)))&3 else 0
                assert im.getpixel((x,y+32))==(expected,)*3,(x,y)
        im.resize((640,400),Image.Resampling.NEAREST).save(out/'vga-qemu.png')
        assert m.read(base+sym['st_pause'],1)==b'\x01','pause tap must survive between frames'
        m.hmp('sendkey esc 500')
        os88qemu.acted(m,lambda:m.read(base+sym['st_fs'],1)==b'\x00',secs=10,what='Stickio returns to desktop')
        m.hmp('screendump "'+str(path)+'"');restored=Image.open(path);assert restored.size==(640,480)
        restored.save(out/'vga-qemu-restored.png')
        print('QEMU VGA: monochrome 320x200 logical glass, terrain pixels, desktop restoration',flush=True)
    finally:os88qemu.kill()

if __name__=='__main__': qemu_display()
