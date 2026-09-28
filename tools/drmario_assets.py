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
    """Trusted embedded stream: byte count/value pairs, zero terminator."""
    out = bytearray()
    pos = 0
    while pos < len(data):
        end = pos + 1
        while end < len(data) and end-pos < 255 and data[end] == data[pos]:
            end += 1
        out.extend((end-pos, data[pos]))
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
        (out/f'dm-screen-{tag}.bin').write_bytes(b''.join(runs(b) for b in banks))
        preview.save(out/f'drmarco-{tag}-art.png')


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
    for actor, poses in enumerate(actors):
        frames = [native(im) for im in poses]
        reference = base
        changed_planes = [[i for i,v in enumerate(reference[plane])
                           if any(f[plane][i] != v for f in frames)]
                          for plane in range(len(base))]
        for pose, frame in enumerate(frames):
            pointers.append(len(blob))
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
