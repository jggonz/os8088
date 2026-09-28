#!/usr/bin/env python3
"""Import Dr. Mario's local reference into native XT caches (no ROM committed)."""
import argparse
import hashlib
from pathlib import Path
import re


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
