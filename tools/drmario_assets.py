#!/usr/bin/env python3
"""Import Dr. Mario's local reference into native XT caches (no ROM committed)."""
import argparse
import hashlib
from pathlib import Path
import re
import struct
from PIL import Image, ImageDraw


ART = Path(__file__).resolve().parents[1] / 'apps/drmario/art/drmarco-screen.png'
VGA_PALETTE = [(0,0,0),(48,96,252),(252,48,64),(252,224,32),
               (20,40,120),(120,16,20),(120,96,8),(252,252,252)]
CGA_PALETTE = [(0,0,0),(85,255,85),(255,85,85),(255,255,85)]


def runs(data):
    """Row packets: 1..127 repeat an ink, 128..255 copy 1..128 literals."""
    out = bytearray()
    pos = 0
    while pos < len(data):
        end = pos + 1
        while end < len(data) and end-pos < 127 and data[end] == data[pos]:
            end += 1
        if end-pos >= 3:
            out.extend((end-pos, data[pos]))
        else:
            end = pos+1
            while end < len(data) and end-pos < 128:
                if data[end:end+3] == data[end:end+1]*3:
                    break
                end += 1
            out.append(128+end-pos-1)
            out.extend(data[pos:end])
        pos = end
    out.append(0)
    return out


def screen_runs(data, vga=False):
    """Repeat identical native scanlines without storing their runs again.

    Each record is a repeat count followed by one zero-terminated packet row.
    A zero repeat count ends the plane/bank. Decoding replays the source row,
    so Mode X needs no video reads or read-plane register changes.
    """
    rows = [data[i:i+80] for i in range(0,len(data),80)]
    out = bytearray()
    pos = 0
    while pos < len(rows):
        end = pos+1
        while end < len(rows) and end-pos < 255 and rows[end] == rows[pos]:
            end += 1
        out.append(end-pos)
        if vga:
            # VGA uses only eight inks: one byte holds a 1..31 pixel run
            # in its upper five bits and the ink in its lower three.
            row = rows[pos]
            x = 0
            while x < len(row):
                stop = x+1
                while stop < len(row) and stop-x < 31 and row[stop] == row[x]:
                    stop += 1
                out.append(((stop-x)<<3) | row[x])
                x = stop
            out.append(0)
        else:
            out.extend(runs(rows[pos]))
        pos = end
    out.append(0)
    return out


def screen_art(out):
    """Compile generated art into native video layouts; no runtime conversion."""
    source = Image.open(ART).convert('RGB')
    palette = Image.new('P', (1,1))
    palette.putpalette(sum((list(c) for c in VGA_PALETTE), []) + [0]*744)
    for tag, height in [('vga',240), ('cga',200)]:
        indexed = source.resize((320,height), Image.Resampling.NEAREST).quantize(
            palette=palette, dither=Image.Dither.NONE)
        pixels = bytes(indexed.getdata())
        # Collapse dark shades to the matching CGA hue; white becomes yellow.
        if tag == 'cga':
            pixels = bytes((0,1,2,3,1,2,3,3)[p] for p in pixels)
        preview = Image.new('P', (320,height))
        colors = VGA_PALETTE if tag == 'vga' else CGA_PALETTE
        preview.putpalette(sum((list(c) for c in colors), []) + [0]*(768-len(colors)*3))
        preview.putdata(pixels)
        screen_surround(preview, tag)
        animation_art(out, tag, preview)
        # Mascots replace the small decorations; the doctor stays in the background.
        preview.paste(0,(240,174 if tag=='vga' else 144,312,200 if tag=='vga' else 170))
        pixels = bytes(preview.getdata())
        if tag == 'cga':
            banks = [bytes(sum(pixels[y*320+x+j] << (6-2*j) for j in range(4))
                           for y in range(bank,height,2) for x in range(0,320,4))
                     for bank in range(2)]
        else:
            banks = [pixels[plane::4] for plane in range(4)]
        (out/f'dm-screen-{tag}.bin').write_bytes(b''.join(screen_runs(b, tag=='vga') for b in banks))
        preview.save(out/f'drmarco-{tag}-art.png')


def front_art(out):
    """Disk-resident splash/help: indexed game artwork, one bounded row at a time."""
    ega=[(0,0,0),(0,0,170),(0,170,0),(0,170,170),(170,0,0),(170,0,170),
         (170,85,0),(170,170,170),(85,85,85),(85,85,255),(85,255,85),
         (85,255,255),(255,85,85),(255,85,255),(255,255,85),(255,255,255)]
    palette=Image.new('P',(1,1));palette.putpalette(sum(map(list,ega),[])+[0]*720)
    splash=Image.open(ART.with_name('drmarco-splash.png')).convert('RGB').resize(
        (432,264),Image.Resampling.NEAREST).quantize(palette=palette,dither=Image.Dither.NONE)
    # Help keeps actual gameplay portrait and cells, with a black writing area.
    help_image=splash.copy();d=ImageDraw.Draw(help_image)
    d.rectangle((8,8,314,258),fill=0,outline=9)
    d.rectangle((315,8,431,263),fill=0)
    game=Image.open(ART).convert('RGB').resize((320,240),Image.Resampling.NEAREST)
    portrait=game.crop((240,64,320,172)).resize((96,130),Image.Resampling.NEAREST)
    help_image.paste(portrait.quantize(palette=palette,dither=Image.Dither.NONE),(328,12))
    d.rectangle((330,158,424,244),fill=0,outline=9)
    # Native capsule/virus tiles are decoded from the gameplay cache, preserving
    # their exact silhouettes and three colors in the matching-four illustration.
    tiles=(out/'dm-vga.bin').read_bytes()
    inks=(0,9,12,14,1,4,6,15)
    for tile,x,y in [(5,336,178),(8,352,178),(5,368,178),(8,384,178),
                     (17,338,224),(16,398,224)]:
        raw=tiles[tile*192:(tile+1)*192]
        im=Image.new('P',(16,12));im.putpalette(palette.getpalette())
        im.putdata([inks[raw[(xx%4)*48+yy*4+xx//4]] for yy in range(12) for xx in range(16)])
        help_image.paste(im,(x,y))
    # Reserve runtime menu lettering, including the compact settings line.
    ImageDraw.Draw(splash).rectangle((104,166,328,261),fill=0)
    sizes=[]
    for tag,height,mono in [('VGA',264,False),('HRC',264,True),('CGA',132,True)]:
        rows=[]
        for name,original in [('splash',splash),('help',help_image)]:
            im=original.resize((432,height),Image.Resampling.NEAREST)
            if mono:
                # Trace palette boundaries, not luminance dithering. Remove
                # green checks first so the line-art foreground stays readable.
                source=list(im.getdata());p=[0 if v in (2,10) else v for v in source]
                edge=Image.new('1',im.size)
                edge.putdata([bool(p[y*432+x]) and any(p[yy*432+xx]!=p[y*432+x]
                    for xx,yy in ((max(0,x-1),y),(min(431,x+1),y),(x,max(0,y-1)),(x,min(height-1,y+1))))
                    for y in range(height) for x in range(432)])
                im=edge
                data=im.tobytes();stride=54
            else:
                p=bytes(im.getdata())
                data=bytes(sum(((p[y*432+x+j]>>plane)&1)<<(7-j) for j in range(8))
                           for y in range(height) for plane in range(4) for x in range(0,432,8))
                stride=216
            im.save(out/f'drmarco-{tag.lower()}-{name}.png')
            rows.extend(data[i:i+stride] for i in range(0,len(data),stride))
        header=bytearray(b'DMF1'+struct.pack('<HHH',432,height,1 if mono else 4))
        offset=len(header)+2*len(rows);directory=bytearray();payload=bytearray()
        shared={}
        for row in rows:
            if row not in shared:
                shared[row]=offset+len(payload)
                payload.extend(runs(row))
            directory.extend(struct.pack('<H',shared[row]))
        blob=header+directory+payload
        if len(blob)>65535:raise ValueError('frontend graphics exceed one segment')
        (out/f'DRMARCO.{tag}').write_bytes(blob)
        sizes.append(f'DM_FRONT_{tag}_SIZE equ {len(blob)}')
    (out/'dm-front.inc').write_text('\n'.join(sizes)+'\n')


def screen_surround(screen, tag):
    """Original native checker/clipboard layout, composed only at build time.

    NES tbl_C198_playfield_1p_mode alternates FC/FF background tiles around
    opaque score and setup boards. Keep that separation at our wider bottle
    size, with black writing surfaces matching the opaque native font cache.
    """
    vga = tag == 'vga'
    height = screen.height
    art = screen.copy()
    d = ImageDraw.Draw(screen)
    d.rectangle((0,0,319,height-1),fill=0)
    # Larger squares keep the surround quiet at native resolution. CGA has
    # no dark ink: alternate red scanlines with black inside its lit squares.
    for y in range(height):
        if not vga and y % 2:
            continue
        for x in range(0,320,24):
            if (x//24+y//24) % 2 == 0:
                d.line((x,y,x+23,y),fill=4 if vga else 2)
    # Preserve the bottle's opaque interior/neck and the complete portrait.
    # Outside them, retain the original glass highlights over the checker.
    for box in [(92,24 if vga else 20,228,228 if vga else 188),
                (112,12,208,32 if vga else 24),
                (240,64 if vga else 52,320,200 if vga else 170)]:
        screen.paste(art.crop(box),box)
    mask = art.point(lambda p: 255 if p else 0, 'L')
    screen.paste(art,(0,0),mask)

    edge, highlight = (6,3) if vga else (2,3)
    def panel(box, clip=False):
        x0,y0,x1,y1 = box
        d.rectangle((x0+2,y0+2,min(x1+2,319),min(y1+2,height-1)),fill=0)
        d.rectangle(box,fill=0,outline=edge)
        d.line((x0+1,y1-1,x0+1,y0+1,x1-1,y0+1),fill=highlight)
        if clip:
            cx = (x0+x1)//2
            d.rectangle((cx-10,y0-4,cx+10,y0+3),fill=edge)
            d.rectangle((cx-7,y0-3,cx+7,y0),fill=highlight)
            d.line((cx-4,y0-2,cx+4,y0-2),fill=0)

    panel((3,26,77,184),clip=True)          # all left HUD rows, including status
    panel((236,26,315,67 if vga else 51),clip=True)
    panel((236,202 if vga else 174,315,215 if vga else 187))
    panel((124,3,195,20))                  # title
    panel((28,height-12,315,height-1))     # controls


def animation_art(out, tag, background):
    """Original code-drawn germs and poses of the existing DrMarco portrait.

    Each actor owns a fixed rectangle. Compile the union of changed native
    bytes across ALL poses, including restoration to the background. A patch
    can therefore follow any other pose, including after a full mode repaint.
    Records: destination word, literal count byte, pixels; FFFF ends a plane.
    The doctor patches only the lens interiors, with no clear-before-draw pass.
    """
    vga = tag == 'vga'
    def native(im):
        p = bytes(im.getdata())
        if vga:
            return [p[i::4] for i in range(4)]
        return [bytes(sum(p[y*320+x+j] << (6-2*j) for j in range(4))
                      for y in range(bank,200,2) for x in range(0,320,4))
                for bank in range(2)]
    base = native(background)
    actors = []
    # Keep the glasses, head and body completely stationary. Close the eyes
    # inside the lenses, then restore the original pixels on reopening.
    closed = background.copy()
    d = ImageDraw.Draw(closed)
    white = 7 if vga else 3
    for box, lid in ([((269,94,273,100),98), ((281,95,286,102),99)] if vga else
                     [((269,78,273,83),81), ((281,79,286,84),82)]):
        d.rectangle(box,fill=white)
        d.line((box[0],lid,box[2],lid),fill=0)
    # Four table slots per actor; unused doctor slots alias the two eye poses.
    actors.append([background.copy(),closed,background.copy(),closed.copy()])
    # Three different silhouettes, tiny boots, waving feelers and googly eyes.
    y0 = 174 if vga else 144
    for color in range(1,4):
        poses = []
        x0 = 240+(color-1)*24
        for pose in range(4):
            im = background.copy()
            d = ImageDraw.Draw(im)
            d.rectangle((x0,y0,x0+23,y0+25),fill=0)
            if pose != 3:
                y = y0+5-(2 if pose == 1 else 0)
                x = x0+5
                ink, eye = color, 7 if vga else 3
                if color == 1:
                    d.ellipse((x,y,x+14,y+13),fill=ink)
                elif color == 2:
                    d.rectangle((x+1,y+1,x+13,y+12),fill=ink)
                    d.rectangle((x+3,y-1,x+11,y+14),fill=ink)
                else:
                    d.polygon([(x+7,y-2),(x+16,y+11),(x-1,y+11)],fill=ink)
                for side in (0,1):
                    xx=x+side*14
                    d.line((xx,y+6,xx+(-3 if side==0 else 3),y+(1 if pose==1 else 9)),fill=ink,width=2)
                    d.line((x+3+side*8,y+12,x+1+side*12,y+16),fill=ink,width=2)
                    ex=x+3+side*7
                    d.rectangle((ex-1,y+3,ex+3,y+7),fill=eye)
                    if pose == 2:
                        d.line((ex-1,y+3,ex+3,y+7),fill=0)
                        d.line((ex+3,y+3,ex-1,y+7),fill=0)
                    else:
                        d.rectangle((ex+(1 if pose==1 else 0),y+5,ex+1+(1 if pose==1 else 0),y+7),fill=0)
                d.line((x+5,y+10,x+9,y+10),fill=0)
                if pose == 2:
                    d.line((x0+8,y0+1,x0+14,y0+1),fill=eye)
            poses.append(im)
        actors.append(poses)
    blob = bytearray()
    pointers = []
    stored = {}
    for actor, poses in enumerate(actors):
        frames = [native(im) for im in poses]
        reference = base
        changed_planes = [[i for i,v in enumerate(reference[plane])
                           if any(f[plane][i] != v for f in frames)]
                          for plane in range(len(base))]
        for pose, frame in enumerate(frames):
            start_offset = len(blob)
            for plane, data in enumerate(frame):
                # Merge nearby changes: a few unchanged bytes cost less than
                # another record and keep the 8088 copy loop short.
                changed = changed_planes[plane]
                pos = 0
                while pos < len(changed):
                    start = end = changed[pos]
                    pos += 1
                    while pos < len(changed) and changed[pos]-end <= 4 and changed[pos]-start < 255:
                        end = changed[pos];pos += 1
                    dest = start + (8192*plane if not vga else 0)
                    blob.extend(struct.pack('<HB',dest,end-start+1))
                    blob.extend(data[start:end+1])
                blob.extend(b'\xff\xff')
            encoded = bytes(blob[start_offset:])
            if encoded in stored:
                del blob[start_offset:]
            else:
                stored[encoded] = start_offset
            pointers.append(stored[encoded])
        for pose, im in enumerate(poses):
            im.save(out/f'dm-{tag}-actor{actor}-{pose}.png')
    (out/f'dm-anim-{tag}.bin').write_bytes(blob)
    (out/f'dm-anim-{tag}.inc').write_text(
        f'dm_animptrs_{tag}: dw '+','.join(f'dm_animdata_{tag}+{p}' for p in pointers)+'\n'+
        f'dm_animdata_{tag}: incbin "dm-anim-{tag}.bin"\n')


def build(source, out):
    chrdata = (source / 'CHR_ROM.chr').read_bytes()
    bank = (source / 'bank_FF.asm').read_text()
    if len(chrdata) != 32768:
        raise ValueError('expected the supplied 32768-byte CHR_ROM.chr')
    # Byte addresses in the annotated disassembly, not the comment's file offsets.
    rom = {}
    for line in bank.splitlines():
        m = re.search(r'00:([0-9A-F]{4}):.*?\.byte\s+([^;]+)', line)
        if m:
            for i, v in enumerate(re.findall(r'\$([0-9A-F]{2})', m[2])):
                rom[int(m[1], 16)+i] = int(v, 16)
    def table(addr, n):
        return bytes(rom[addr+i] for i in range(n))
    def tile(t):
        b = chrdata[t*16:t*16+16]
        return [[((b[y] >> (7-x)) & 1) | (((b[y+8] >> (7-x)) & 1) << 1)
                 for x in range(8)] for y in range(8)]
    # A4A0 sprite records: top 40, bottom 50, left 60, right 70.
    # Color variants are adjacent. D0..D2 are the three bottle viruses.
    ids = [None] + [0x80+c for c in (2, 1, 0)]
    ids += [base+c for base in (0x60, 0x70, 0x40, 0x50, 0xd0) for c in (2, 1, 0)]
    # A56B palette 80: sprite palette 2 and bottle background palette 2
    # are 0F,28,15,21: black/yellow/red/blue. The CHR variants are in
    # yellow/red/blue order; native color IDs reverse that order.
    cells = [[[ (0,3,2,1)[v] for v in row] for row in tile(t)]
             if t is not None else [[0]*8 for _ in range(8)] for t in ids]
    # Keep tile 19 for the clear flash, then three cheeky mirrored virus poses.
    cells += [[[7]*8 for _ in range(8)]]
    cells += [[list(reversed(row)) for row in cell] for cell in cells[16:19]]
    def vgacell(cell):
        pix = [[cell[y*8//12][x//2] for x in range(16)] for y in range(12)]
        return bytes(pix[y][x] for plane in range(4) for y in range(12) for x in range(plane,16,4))
    # NES blue -> CGA green, red -> red, yellow -> yellow. The three inks
    # preserve the source's contrasting capsule highlights and virus faces.
    def cga_color(v):
        return 0 if not v else 3 if v == 7 else (v-1)%3+1
    def cgacell(cell):
        pix = [[cga_color(cell[y*8//10][x//2]) for x in range(16)] for y in range(10)]
        return bytes(sum(row[x+j] << (6-2*j) for j in range(4)) for row in pix for x in range(0,16,4))
    # Font is the OS font at runtime; these caches are exclusively game art.
    out.mkdir(parents=True, exist_ok=True)
    screen_art(out)
    (out/'dm-vga.bin').write_bytes(b''.join(map(vgacell,cells)))
    (out/'dm-cga.bin').write_bytes(b''.join(map(cgacell,cells)))
    front_art(out)
    text = ['; Generated from local Dr. Mario bank_FF.asm. Do not edit.']
    for name, addr, n in [('dm_speeds',0xa795,81), ('dm_pair_a',0xa7fd,9),
                           ('dm_viruscolors',0xa7ed,16), ('dm_pair_b',0xa806,9), ('dm_heights',0xa3de,21)]:
        data = table(addr,n)
        if name == 'dm_speeds':
            data = bytes(max(1, ((v+1)*546+300)//600) for v in data)
        text += [name+': db '+','.join(map(str,data))]
    (out/'dm-tables.inc').write_text('\n'.join(text)+'\n')
    (out/'dm-source.txt').write_text('\n'.join(f'{p.name} SHA256 {hashlib.sha256(p.read_bytes()).hexdigest()}'
        for p in [source/'CHR_ROM.chr',source/'bank_FF.asm'])+'\n')

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();build(a.source,a.output)
