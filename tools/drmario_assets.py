#!/usr/bin/env python3
"""Import DrMarco's committed NES reference into native XT caches (SPEC.md 100).

Two halves, and only the first is on `make`'s path (SPEC.md 100):

  drmario_assets.py SOURCE OUT
      STDLIB ONLY. Checks SOURCE's two files against their SHA-256 pins and
      writes the capsule/virus tile caches and the speed/colour tables to OUT.

  drmario_assets.py SOURCE OUT --art NATIVE
      ...and the PILLOW half as well: the two committed PNGs composed into
      the playfield screens, the animation patches and the DRMARCO.VGA/.CGA/
      .HRC front screens, written to NATIVE (apps/drmario/art/native/, which
      is COMMITTED - `make drmarco-art` is the command), with the preview PNGs
      in OUT. It is run by hand when the art changes, the way tools/1942art.py
      and tools/os88logovid.py are, so a plain `make` needs no Pillow.
"""
import argparse
import hashlib
from pathlib import Path
import re
import struct

Image = ImageDraw = None          # Pillow, loaded by the --art half alone


def load_pil():
    global Image, ImageDraw
    from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / 'apps/drmario/art/drmarco-screen.png'
# reference/drmario/README.md is the provenance; these are the bytes it names.
PINS = {'CHR_ROM.chr': '853123999e15a05723cfaa1f928980d294d985dfcea5da2ad407afd56d0586a3',
        'bank_FF.asm': '90466e12c051d210054b3d85f314988a61aa7ca1068774400af7ba213400accb'}


def check_source(source):
    """Refuse any input that is not the pinned reference, named by file."""
    for name, want in PINS.items():
        got = hashlib.sha256((source / name).read_bytes()).hexdigest()
        if got != want:
            raise SystemExit(f'drmario: {source / name} is SHA-256 {got}, '
                             f'not the pinned {want} (reference/drmario/README.md)')
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


def row_tokens(row, above, vga):
    """Cheapest packet list for one 80-byte native row (shortest path).

    `above` is the previous row of the same plane/bank, which the decoder's
    row buffer still holds, or None for a plane's first row. A COPY packet
    leaves that many buffer bytes as they were; the zero terminator leaves
    the rest of the row as it was, so it is only legal where that is right.
    VGA: (n<<3)|ink repeats one of eight inks 1..31 times, 1..6 copies that
    many, 7,n copies n. CGA: 1..127 repeat the next byte, 128..191 copy
    1..64 literal bytes, 192..255 copy 1..64 from above.
    """
    n = len(row)
    best = [None]*n + [(1, None)]          # cost (incl. terminator), first token, next x
    for x in range(n-1, -1, -1):
        options = []
        if above is not None and row[x:] == above[x:]:
            options.append((1, None, n))    # terminator: rest as above
        same = x+1
        while same < n and row[same] == row[x]:
            same += 1
        if above is not None:
            keep = x
            while keep < n and row[keep] == above[keep]:
                keep += 1
        else:
            keep = x
        if vga:
            for e in range(x+1, min(same, x+31)+1):
                options.append((1+best[e][0], bytes([((e-x) << 3) | row[x]]), e))
            for e in range(x+1, keep+1):
                code = bytes([e-x]) if e-x <= 6 else bytes([7, e-x])
                options.append((len(code)+best[e][0], code, e))
        else:
            for e in range(x+1, min(same, x+127)+1):
                options.append((2+best[e][0], bytes([e-x, row[x]]), e))
            for e in range(x+1, min(n, x+64)+1):
                options.append((1+e-x+best[e][0], bytes([127+e-x])+bytes(row[x:e]), e))
            for e in range(x+1, min(keep, x+64)+1):
                options.append((1+best[e][0], bytes([191+e-x]), e))
        best[x] = min(options, key=lambda o: o[0])
    out = bytearray()
    x = 0
    while x < n:
        cost, code, e = best[x]
        if code is None:
            break
        out += code
        x = e
    out.append(0)
    return out


def screen_runs(data, vga=False):
    """Repeat identical native scanlines without storing their packets again.

    Each record is a repeat count followed by one zero-terminated packet row,
    decoded into an 80-byte RAM row buffer and copied to video memory once
    per repeat. A zero repeat count ends the plane/bank. The buffer keeps the
    previous row, so packets can leave bytes as the row above had them; that
    is what the checker surround is made of. No video reads.
    """
    rows = [data[i:i+80] for i in range(0,len(data),80)]
    out = bytearray()
    pos = 0
    above = None
    while pos < len(rows):
        end = pos+1
        while end < len(rows) and end-pos < 255 and rows[end] == rows[pos]:
            end += 1
        out.append(end-pos)
        out.extend(row_tokens(rows[pos], above, vga))
        above = rows[pos]
        pos = end
    out.append(0)
    return out


def screen_art(out, native):
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
        animation_art(out, native, tag, preview)
        # Mascots replace the small decorations; the doctor stays in the background.
        preview.paste(0,(240,174 if tag=='vga' else 144,312,200 if tag=='vga' else 170))
        pixels = bytes(preview.getdata())
        if tag == 'cga':
            banks = [bytes(sum(pixels[y*320+x+j] << (6-2*j) for j in range(4))
                           for y in range(bank,height,2) for x in range(0,320,4))
                     for bank in range(2)]
        else:
            banks = [pixels[plane::4] for plane in range(4)]
        (native/f'dm-screen-{tag}.bin').write_bytes(b''.join(screen_runs(b, tag=='vga') for b in banks))
        preview.save(out/f'drmarco-{tag}-art.png')


def front_art(out, native):
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
    # dm-vga.bin is vgacell()'s: per cell, planes 0 and 2 (1 and 3 repeat
    # them), each the cell's eight distinct rows of four bytes.
    tiles=(out/'dm-vga.bin').read_bytes()
    inks=(0,9,12,14,1,4,6,15)
    for tile,x,y in [(5,336,178),(8,352,178),(5,368,178),(8,384,178),
                     (17,338,224),(16,398,224)]:
        raw=tiles[tile*64:(tile+1)*64]
        im=Image.new('P',(16,12));im.putpalette(palette.getpalette())
        im.putdata([inks[raw[(xx%4)//2*32+(yy*8//12)*4+xx//4]] for yy in range(12) for xx in range(16)])
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
        (native/f'DRMARCO.{tag}').write_bytes(blob)
        sizes.append(f'DM_FRONT_{tag}_SIZE equ {len(blob)}')
    (native/'dm-front.inc').write_text('\n'.join(sizes)+'\n')


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


def patch_plane(changed, data, bias):
    """One plane/bank of a pose patch: the cheapest cover of `changed`.

    Stream: destination word, then records of a count byte, its payload and
    a skip byte added to the destination after it; a count of 128 ends the
    plane. Count 1..127 copies that many literal bytes, 128+n fills n bytes
    with the one byte that follows. A record may span unchanged bytes, whose
    pixels are the background's anyway. Fewest bytes wins, then fewest 8088
    cycles under dm_animpatch's costs (57+17n a copy, 80+10n a fill, which
    is why a fill needs four bytes). Against the literal-only records this
    replaced (destination word per record, 64+17n), every pose is cheaper.
    """
    out = bytearray()
    if not changed:
        return bytes([0, 0, 128])     # nothing changes: destination, end
    n = len(changed)
    # best[k] = (bytes, cycles, record end, fill?) covering changed[k:]
    best = [None]*n + [(1, 0, None, None)]
    for k in range(n-1, -1, -1):
        start = changed[k]
        options = []
        for j in range(k, n):
            end = changed[j]
            length = end-start+1
            if length > 127:
                break
            nxt = changed[j+1] if j+1 < n else None
            if nxt is not None and nxt-end-1 > 255:
                continue
            tail = best[j+1]
            span = data[start:end+1]
            options.append((2+length+tail[0], 57+17*length+tail[1], end, False))
            if length >= 4 and span.count(span[0]) == length:
                options.append((3+tail[0], 80+10*length+tail[1], end, True))
        best[k] = min(options, key=lambda o: (o[0], o[1]))
    out += struct.pack('<H', changed[0]+bias)
    k = 0
    while k < n:
        _, _, end, fill = best[k]
        start = changed[k]
        length = end-start+1
        if fill:
            out += bytes([128+length, data[start]])
        else:
            out += bytes([length]) + bytes(data[start:end+1])
        while k < n and changed[k] <= end:
            k += 1
        out.append(changed[k]-end-1 if k < n else 0)
    out.append(128)
    return bytes(out)


def animation_art(out, native, tag, background):
    """Original code-drawn germs and poses of the existing DrMarco portrait.

    Each actor owns a fixed rectangle. Compile the union of changed native
    bytes across ALL poses, including restoration to the background. A patch
    can therefore follow any other pose, including after a full mode repaint.
    Each plane/bank is patch_plane()'s stream. The doctor patches only the
    lens interiors, with no clear-before-draw pass.
    """
    vga = tag == 'vga'
    def to_native(im):
        p = bytes(im.getdata())
        if vga:
            return [p[i::4] for i in range(4)]
        return [bytes(sum(p[y*320+x+j] << (6-2*j) for j in range(4))
                      for y in range(bank,200,2) for x in range(0,320,4))
                for bank in range(2)]
    base = to_native(background)
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
        frames = [to_native(im) for im in poses]
        reference = base
        changed_planes = [[i for i,v in enumerate(reference[plane])
                           if any(f[plane][i] != v for f in frames)]
                          for plane in range(len(base))]
        for pose, frame in enumerate(frames):
            start_offset = len(blob)
            for plane, data in enumerate(frame):
                blob.extend(patch_plane(changed_planes[plane], data,
                                        8192*plane if not vga else 0))
            encoded = bytes(blob[start_offset:])
            if encoded in stored:
                del blob[start_offset:]
            else:
                stored[encoded] = start_offset
            pointers.append(stored[encoded])
        for pose, im in enumerate(poses):
            im.save(out/f'dm-{tag}-actor{actor}-{pose}.png')
    (native/f'dm-anim-{tag}.bin').write_bytes(blob)
    (native/f'dm-anim-{tag}.inc').write_text(
        f'dm_animptrs_{tag}: dw '+','.join(f'dm_animdata_{tag}+{p}' for p in pointers)+'\n'+
        f'dm_animdata_{tag}: incbin "dm-anim-{tag}.bin"\n')


def build(source, out, native=None):
    check_source(source)
    chrdata = (source / 'CHR_ROM.chr').read_bytes()
    # Only ASCII byte/address annotations are parsed; extended comment bytes
    # must not depend on the host's default text encoding.
    bank = (source / 'bank_FF.asm').read_text(encoding='latin-1')
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
        # Pixels are doubled horizontally, so planes 0/1 and 2/3 are equal:
        # store 0 and 2, and the renderer writes each to two planes at once.
        # The 12 rows are the source's 8 (y*8//12 repeats 0, 2, 4 and 6), and
        # dm_flush's unrolled rows step back over each repeat: 8 rows stored.
        planes = [bytes(cell[y][x//2] for y in range(8) for x in range(plane,16,4))
                  for plane in range(4)]
        assert planes[0] == planes[1] and planes[2] == planes[3]
        return planes[0] + planes[2]
    # NES blue -> CGA green, red -> red, yellow -> yellow. The three inks
    # preserve the source's contrasting capsule highlights and virus faces.
    def cga_color(v):
        return 0 if not v else 3 if v == 7 else (v-1)%3+1
    def cgacell(cell):
        # 10 rows from the source's 8 (y*8//10 repeats 0 and 4): 8 stored,
        # and dm_flush's unrolled rows step back over each repeat.
        pix = [[cga_color(cell[y][x//2]) for x in range(16)] for y in range(8)]
        return bytes(sum(row[x+j] << (6-2*j) for j in range(4)) for row in pix for x in range(0,16,4))
    # Font is the OS font at runtime; these caches are exclusively game art.
    out.mkdir(parents=True, exist_ok=True)
    (out/'dm-vga.bin').write_bytes(b''.join(map(vgacell,cells)))
    (out/'dm-cga.bin').write_bytes(b''.join(map(cgacell,cells)))
    if native is not None:
        # front_art reads the dm-vga.bin just written for its help panel.
        load_pil()
        native.mkdir(parents=True, exist_ok=True)
        screen_art(out, native)
        front_art(out, native)
    text = ['; Generated from reference/drmario/bank_FF.asm. Do not edit.']
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
    p.add_argument('--art',type=Path,metavar='NATIVE',
                   help='also run the Pillow half, writing the committed art to NATIVE')
    a=p.parse_args();build(a.source,a.output,a.art)
