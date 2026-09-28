#!/usr/bin/env python3
"""DrMarco desktop splash/help, adapter artwork, input and title audio."""
import argparse
import struct
from PIL import Image
import drmario as D


def key(ui, name):
    ui.m.key(name, up=False)
    D.M.pace(ui.m, .04)
    ui.m.key(name, down=False)
    D.M.pace(ui.m, .15)
    D.M.ui_done(ui.m)


def shot(ui, tag, name):
    ui.mo.to(2,2)
    ui.settle()
    path=D.ROOT/f'build/drmario-proof/{tag}-{name}.png'
    if tag=='vga':
        D.capture(ui.m,path)
    else:
        w,h,pixels=ui.m.vram()
        D.M.write_png(str(path),w,h,pixels)
    return Image.open(path).convert('RGB')


def revealed(ui,p):
    D.M.until(ui.m,lambda _: int.from_bytes(p.data('reveal',2),'little')>=
              int.from_bytes(p.data('frontheight',2),'little'),'window shade finishes',guest=10)
    ui.settle()


def help_reveal(ui,p):
    m=ui.m
    m.pause();m.bp_exec(p.addr('fronttick'));m.key('KeyH');m.run()
    for _ in range(10):
        assert m.wait_stop(30)=='breakpoint'
        if p.b('help'):break
        m.run()
    else:raise AssertionError('H did not open help')
    assert p.data('reveal',2)==bytes(2)
    elapsed=p.call('fronttick')
    assert int.from_bytes(p.data('reveal',2),'little')==12
    assert int.from_bytes(p.data('frontrow',2),'little')==12
    times=[elapsed]
    while int.from_bytes(p.data('reveal',2),'little')<int.from_bytes(p.data('frontheight',2),'little'):
        times.append(p.call('fronttick'))
    print('maximum help shade step:',round(max(times),2),'ms',flush=True)
    assert max(times)<55, ('shade step delays music timer',max(times))
    p.put('help',0);p.put('reveal',bytes(2));times=[]
    while int.from_bytes(p.data('reveal',2),'little')<int.from_bytes(p.data('frontheight',2),'little'):
        times.append(p.call('fronttick'))
    print('maximum splash shade step:',round(max(times),2),'ms',flush=True)
    assert max(times)<55, ('splash shade step delays music timer',max(times))
    print('splash drawing total:',round(sum(times),2),'ms;',len(times),'timer steps',flush=True)
    # Settings must update text without even entering the image decoder.
    original=p.data('level',2);music=p.b('music')
    cache=p.data('fontvga',4032)
    timings=[]
    for code in (0x4d00,ord('s'),ord('m'),ord('q'),27):
        p.put('frontrow',b'\xa5\x5a')
        timings.append(p.call('onkey',ax=code))
        assert p.data('frontrow',2)==b'\xa5\x5a', 'key redrew the splash'
        assert p.data('fontvga',4032)==cache, 'key decoded image pixels'
    print('maximum settings key:',round(max(timings),2),'ms',flush=True)
    assert max(timings)<25, ('settings response',timings)
    p.put('level',original);p.put('music',music)
    # An initial paint shows controls before loading the file. Simulate the
    # unopened resource, then restore the existing claim without reallocating.
    seg=p.data('artseg',2);kind=p.b('artkind')
    p.put('artseg',bytes(2));p.put('artkind',0);p.put('reveal',bytes(2))
    initial=p.call('frontpaint')
    assert p.b('artkind')==0 and p.data('reveal',2)==bytes(2)
    print('initial controls paint before disk I/O:',round(initial,2),'ms',flush=True)
    p.put('artseg',seg);p.put('artkind',kind);p.put('reveal',p.data('frontheight',2))
    if p.b('frontcolor'):
        # The clipped fallback converts native planes to packed pixels. Check
        # that conversion against the uncompressed indexed image as well.
        resource=(D.ROOT/'build/drmario-art/DRMARCO.VGA').read_bytes()
        offset=struct.unpack_from('<H',resource,10+60*2)[0]
        p.call('frontunpack',si=offset,es=int.from_bytes(p.data('artseg',2),'little'))
        p.call('frontpacked',es=p.base>>4)
        row=list(Image.open(D.ROOT/'build/drmario-art/drmarco-vga-splash.png').getdata())[60*432:61*432]
        expected=bytes((row[x]<<4)|row[x+1] for x in range(0,432,2))
        assert m.read(p.addr('queue')+216,216)==expected, 'packed clipped fallback'
    p.put('help',1);p.call('frontpaint')
    m.bp_exec();m.run();revealed(ui,p)


def arm(tag, sym):
    machine={'vga':'os8088_xt_vga','cga':'os8088_5150_cga_gla',
             'herc':'os8088_5150_herc_gla'}[tag]
    with D.os88ui.boot(str(D.ROOT/'build/os8088-360.img'),
            apps=str(D.ROOT/'build/drmario360.img'),machine=machine) as ui:
        m=ui.m
        ui.open_drive('B');ui.open('DRMARCO.O88');ui.settle()
        p=D.Probe(ui,sym)
        revealed(ui,p)
        assert int.from_bytes(p.data('artseg',2),'little'), 'graphics file did not load'
        assert not p.b('fs') and not p.b('help') and not p.b('started')
        assert bool(p.b('frontcolor'))==(tag=='vga')
        assert bool(p.b('frontplay'))==(tag!='herc')
        assert p.b('frontscale')==(1 if tag=='cga' else 2)
        # Title music is serviced by the window timer, not by a paint loop.
        phase=p.data('audio_voices',24)
        D.M.pace(m,.3)
        assert p.b('audio_live') and phase!=p.data('audio_voices',24)
        splash=shot(ui,tag,'splash')
        x,y=struct.unpack('<HH',p.data('frontx',4));scale=p.b('frontscale')
        art=splash.crop((x+24,y+14*scale,x+420,y+70*scale))
        colors=art.getcolors(100000)
        assert len(colors)>5 if tag=='vga' else len(colors)==2
        preview=Image.open(D.ROOT/f'build/drmario-art/drmarco-{"hrc" if tag=="herc" else tag}-splash.png').convert('RGB')
        # Compare all static pixels, including the final short batch. Only the
        # runtime menu lettering is excluded from the uncompressed art oracle.
        expected=preview.copy()
        actual=splash.crop((x+8,y,x+440,y+132*scale))
        menu=(104,83*scale,329,132*scale)
        expected.paste(0,menu);actual.paste(0,menu)
        assert actual.tobytes()==expected.tobytes(), ('splash pixels',tag)
        p.put('frontrow',b'\xa5\x5a')
        key(ui,'ArrowRight');key(ui,'KeyS')
        assert p.data('frontrow',2)==b'\xa5\x5a', 'delivered settings key repainted artwork'
        changed=shot(ui,tag,'settings')
        # Only the 17-character settings row may change after real key events.
        before=splash.copy();after=changed.copy()
        rect=(x+112,y+115*scale,x+112+17*8,y+115*scale+8)
        assert before.crop(rect).tobytes()!=after.crop(rect).tobytes(), 'settings text did not update'
        before.paste(0,rect);after.paste(0,rect)
        content=(x,y,x+440,y+132*scale)
        assert before.crop(content).tobytes()==after.crop(content).tobytes(), 'settings damaged splash'
        assert (p.b('level'),p.b('speed'))==(1,1)
        settings=p.data('level',2);board=p.data('board',128)
        help_reveal(ui,p)
        assert p.b('help') and not p.b('fs')
        revealed(ui,p)
        assert settings==p.data('level',2) and board==p.data('board',128)
        help_image=shot(ui,tag,'help')
        assert splash.tobytes()!=help_image.tobytes()
        expected=Image.open(D.ROOT/f'build/drmario-art/drmarco-{"hrc" if tag=="herc" else tag}-help.png').convert('RGB')
        actual=help_image.crop((x+8,y,x+440,y+132*scale))
        writing=(16,12*scale,312,126*scale)
        expected.paste(0,writing);actual.paste(0,writing)
        assert actual.tobytes()==expected.tobytes(), ('help pixels',tag)
        phase=p.data('audio_voices',24);D.M.pace(m,.3)
        assert p.b('audio_live') and phase!=p.data('audio_voices',24)
        key(ui,'Escape');revealed(ui,p);assert not p.b('help')
        key(ui,'KeyH');key(ui,'KeyH');revealed(ui,p);assert not p.b('help')
        # Repainting after a drag must translate both art and click targets.
        ui.move_window(ui.window('DrMarco'),127,21 if tag=='cga' else 28);ui.settle()
        w=ui.window('DrMarco');x,y=w.x+1,w.y+D.G.TITLE_H
        ui.mo.click(x+150,y+102*scale);revealed(ui,p)
        assert p.b('help')
        if tag=='herc':
            key(ui,'Enter');assert not p.b('fs') and not p.b('started')
            key(ui,'Escape');assert not p.b('help')
        else:
            # Enter from help starts the game; Escape restores that help page.
            # Model Enter arriving before the first resource load. Reuse the
            # existing claim so the next load also exercises claim replacement.
            if tag=='vga':p.put('artkind',0)
            m.pause();m.bp_exec(p.addr('input'));m.key('Enter');m.run()
            assert m.wait_stop(30)=='breakpoint' and p.b('fs')
            assert p.b('started') and p.b('level')==1
            board=p.data('board',128)
            m.bp_exec();m.run();key(ui,'Escape');ui.settle()
            revealed(ui,p)
            assert p.b('artkind') and int.from_bytes(p.data('artseg',2),'little')
            assert not p.b('fs') and p.b('help') and board==p.data('board',128)
            key(ui,'Escape');assert not p.b('help')
        print(tag,'splash/help, adapter art, music, navigation and restoration: PASS',flush=True)


if __name__=='__main__':
    ap=argparse.ArgumentParser()
    ap.add_argument('--arm',choices=('vga','cga','herc','all'),default='all')
    a=ap.parse_args()
    sym=D.symbols()
    for tag in (('vga','cga','herc') if a.arm=='all' else (a.arm,)):
        arm(tag,sym)
