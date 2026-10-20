"""Delete one file ANYWHERE in a FAT12 floppy image - a sub-folder too,
which tools/os88fat.py's `del` refuses - marking its entry deleted and
freeing its chain in every FAT copy. For a test that needs a disk with a
file taken away; tests/assocstale.py is the first.
"""
import struct


def delete(path, target):
    target = target.upper()
    img = bytearray(open(path, 'rb').read())
    bps, spc, res, nf, nroot = struct.unpack_from('<HBHBH', img, 11)
    spf = struct.unpack_from('<H', img, 22)[0]
    fat = res * 512; root = fat + nf * spf * 512; data = root + nroot * 32
    def nxt(c):
        v = struct.unpack_from('<H', img, fat + c * 3 // 2)[0]
        return (v >> 4) if c & 1 else v & 0xFFF
    def setc(c, val):
        for k in range(nf):
            o = fat + k * spf * 512 + c * 3 // 2
            v = struct.unpack_from('<H', img, o)[0]
            v = (v & 0x000F) | (val << 4) if c & 1 else (v & 0xF000) | val
            struct.pack_into('<H', img, o, v)
    def chain(c):
        out = []
        while 2 <= c < 0xFF8: out.append(c); c = nxt(c)
        return out
    def entries(c):
        if c is None: return [(root + i*32) for i in range(nroot)]
        return [data + (x-2)*spc*512 + i*32 for x in chain(c) for i in range(spc*16)]
    c = None; parts = target.split('/')
    for depth, part in enumerate(parts):
        for o in entries(c):
            e = img[o:o+32]
            if e[0] == 0: break
            n = e[0:8].decode('latin1').rstrip() + ('.' + e[8:11].decode('latin1').rstrip() if e[8:11].strip() else '')
            if n == part:
                cl = struct.unpack_from('<H', e, 26)[0]
                if depth == len(parts) - 1:
                    for x in chain(cl): setc(x, 0)
                    img[o] = 0xE5
                    open(path, 'wb').write(img); return
                c = cl; break
        else:
            raise SystemExit('fatdel: not found: ' + part)
