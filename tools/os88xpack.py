#!/usr/bin/env python3
"""os88xpack.py - pack an MZ .EXE into the EXEPACK layout, for a TEST.

    python3 tools/os88xpack.py IN.EXE OUT.EXE [--spoil table|stream]

SPEC.md 96.40.9.1's probe needs a program kern_dos's loader has to UNPACK and
RELOCATE (kerndos/kdxpack.inc), so it needs the real format and not only its
shape. This writes it, from the published description of the format and from
nothing else:

  * the load module is the PACKED image, then the EXEPACK block at a paragraph
    of its own: the 16-byte header (real IP, real CS, a scratch word, the
    block's bytes, real SP, real SS, the unpacked paragraphs, 'RB'), a stub,
    the stub's "Packed file is corrupt" and the packed relocation table -
    sixteen groups, one per 64KB of the image, a count word and its offsets;
  * the packed image is read BACKWARD by the unpacker: a command byte, a
    16-bit length below it, then B0h/B1h one fill byte, or B2h/B3h LENGTH
    bytes as they lie; bit 0 marks the last command, and whatever is below it
    is a prefix already in place.

THE STUB IS NOT AN UNPACKER. It prints the message and exits 0FFh - so a
program that reaches it was NOT unpacked by the loader, which is exactly the
failure the probe exists to see. Packed with --spoil, the loader must refuse
(the table or the stream will not add up) and fall back (SPEC.md 96.40.9.1).

Every output is unpacked again here by an independent reader of the same
format and compared with the input image, relocations applied, before the
file is written; a mismatch is an error and no file.
"""
import struct
import sys

MSG = b'Packed file is corrupt'
RUN = 16            # a run of this many identical bytes becomes a fill


def read_mz(data):
    if data[:2] != b'MZ':
        raise SystemExit('os88xpack: not an MZ file')
    (cblp, cp, crlc, cparhdr, minalloc, maxalloc, ss, sp, _csum, ip, cs,
     lfarlc) = struct.unpack('<12H', data[2:26])
    size = (cp - 1) * 512 + (cblp or 512) if cp else 0
    image = bytearray(data[cparhdr * 16:size])
    relocs = []
    for i in range(crlc):
        off, seg = struct.unpack('<HH', data[lfarlc + 4 * i:lfarlc + 4 * i + 4])
        relocs.append(seg * 16 + off)
    return dict(image=image, relocs=relocs, minalloc=minalloc,
                maxalloc=maxalloc, ss=ss, sp=sp, ip=ip, cs=cs)


def segments(image):
    """(kind, start, length) from the image's bottom up; kind 'f' or 'c'."""
    out = []
    i, n = 0, len(image)
    lit = 0
    while i < n:
        j = i
        while j < n and image[j] == image[i] and j - i < 0xFFFF:
            j += 1
        if j - i >= RUN:
            if lit < i:
                out.append(('c', lit, i - lit))
            out.append(('f', i, j - i))
            i = lit = j
        else:
            i = j
    if lit < n:
        out.append(('c', lit, n - lit))
    # a copy is at most 0FFFFh long
    seg = []
    for k, s, ln in out:
        while k == 'c' and ln > 0xFFFF:
            seg.append((k, s, 0xFFFF)); s += 0xFFFF; ln -= 0xFFFF
        seg.append((k, s, ln))
    return seg


def compress(image):
    """The packed bytes: a raw prefix, then one command per segment."""
    seg = segments(image)
    # THE PREFIX: everything below the first fill stays where it is. Above
    # it each fill gains its length less 4 and each copy costs 3, and the
    # walk is only safe while the gain so far is never negative (a command
    # must not write over bytes still to be read). A run of RUN bytes gains
    # 12 and a copy always follows a fill, so it never is.
    k = 0
    while k < len(seg) and seg[k][0] == 'c':
        k += 1
    if k == len(seg):
        raise SystemExit('os88xpack: nothing to pack (no run of %d bytes)' % RUN)
    prefix = seg[k][1]
    packed = bytearray(image[:prefix])
    gain = 0
    for n, (kind, s, ln) in enumerate(seg[k:]):
        last = 1 if n == 0 else 0
        if kind == 'f':
            packed += bytes([image[s]]) + struct.pack('<H', ln) + bytes([0xB0 | last])
            gain += ln - 4
        else:
            packed += image[s:s + ln] + struct.pack('<H', ln) + bytes([0xB2 | last])
            gain -= 3
        if gain < 0:
            raise SystemExit('os88xpack: the stream would overwrite itself')
    return packed


def unpack(packed_area, dlen):
    """The independent reader: the packed paragraphs -> the image."""
    buf = bytearray(max(dlen * 16, len(packed_area)))
    buf[:len(packed_area)] = packed_area
    s = len(packed_area) - 1
    for _ in range(15):
        if buf[s] != 0xFF:
            break
        s -= 1
    d = dlen * 16 - 1
    while True:
        cmd = buf[s]
        ln = buf[s - 2] | buf[s - 1] << 8
        s -= 3
        if cmd & 0xFE == 0xB0:
            v = buf[s]; s -= 1
            buf[d - ln + 1:d + 1] = bytes([v]) * ln
        elif cmd & 0xFE == 0xB2:
            buf[d - ln + 1:d + 1] = buf[s - ln + 1:s + 1]
            s -= ln
        else:
            raise SystemExit('os88xpack: selfcheck - command %02X' % cmd)
        d -= ln
        if cmd & 1:
            break
    if s != d:
        raise SystemExit('os88xpack: selfcheck - prefix out of place')
    return buf[:dlen * 16]


def stub(entry):
    """Print the message on handle 2 and exit 0FFh. It runs at CS:entry and
    the message follows it, so its offset is that plus the code's length."""
    def code(msg_off):
        return bytes([
            0x8C, 0xC8,                    # mov ax, cs
            0x8E, 0xD8,                    # mov ds, ax
            0xB4, 0x40,                    # mov ah, 40h
            0xBB, 0x02, 0x00,              # mov bx, 2
            0xB9, len(MSG), 0x00,          # mov cx, 22
            0xBA, msg_off & 0xFF, msg_off >> 8,  # mov dx, msg
            0xCD, 0x21,                    # int 21h
            0xB8, 0xFF, 0x4C,              # mov ax, 4CFFh
            0xCD, 0x21,                    # int 21h
        ])
    return code(entry + len(code(0)))


def main(argv):
    spoil = None
    skip = 0                            # 0 = the 16-byte header
    if '--long' in argv:
        i = argv.index('--long')
        skip = int(argv[i + 1])
        del argv[i:i + 2]
        if skip < 1:
            raise SystemExit('os88xpack: --long takes a skip of 1 or more')
    if '--spoil' in argv:
        i = argv.index('--spoil')
        spoil = argv[i + 1]
        del argv[i:i + 2]
    if len(argv) != 3:
        raise SystemExit(__doc__)
    mz = read_mz(open(argv[1], 'rb').read())
    image = mz['image']
    image += bytes((-len(image)) % 16)
    dlen = len(image) // 16
    packed = compress(image)
    pad = (-len(packed)) % 16
    packed += b'\xFF' * pad
    pp = len(packed) // 16              # the packed paragraphs

    groups = [[] for _ in range(16)]
    for lin in mz['relocs']:
        groups[lin >> 16].append(lin & 0xFFFF)
    table = b''.join(struct.pack('<H', len(g)) + b''.join(
        struct.pack('<H', o) for o in sorted(g)) for g in groups)
    hlen = 18 if skip else 16
    body = stub(hlen) + MSG + table
    exsz = hlen + len(body)
    if spoil == 'table':
        exsz += 2                       # the groups no longer end at the end
        body += b'\0\0'
    hdr = struct.pack('<7H', mz['ip'], mz['cs'], 0, exsz, mz['sp'], mz['ss'],
                      dlen)
    if skip:
        hdr += struct.pack('<H', skip)  # the long header: the skip, THEN 'RB'
    block = hdr + b'RB' + body
    if spoil == 'stream':
        packed[len(packed) - pad - 1] = 0xB4   # the top command: not one
    # the long header's skip: the packed image ends skip paragraphs under
    # the block, so skip - 1 paragraphs of nothing sit between them
    load = packed + b'\x00' * (16 * max(skip - 1, 0)) + block
    load += bytes((-len(load)) % 16)
    blkp = (len(block) + 15) // 16
    cs = pp + max(skip - 1, 0)          # the block's paragraph
    # memory: the image unpacked, the block above it, a stub stack, and what
    # the program itself asked for beyond its image
    need = dlen + blkp + 0x20 + mz['minalloc']
    have = len(load) // 16
    minalloc = max(need - have, 0x20)

    # --- the selfcheck, before a byte is written --------------------------
    if spoil is None:
        back = unpack(packed, dlen)
        if back != image:
            raise SystemExit('os88xpack: selfcheck - the image did not come back')
        t = 0
        tbl = table
        n = 0
        for g in range(16):
            cnt = struct.unpack('<H', tbl[t:t + 2])[0]; t += 2
            for _ in range(cnt):
                o = struct.unpack('<H', tbl[t:t + 2])[0]; t += 2
                if (g << 16 | o) + 2 > dlen * 16:
                    raise SystemExit('os88xpack: selfcheck - fixup outside the image')
                n += 1
        if t != len(tbl) or n != len(mz['relocs']):
            raise SystemExit('os88xpack: selfcheck - the table does not read back')

    hdrsz = 32
    fsize = hdrsz + len(load)
    mzh = struct.pack('<2s12H', b'MZ', fsize % 512, (fsize + 511) // 512, 0,
                      hdrsz // 16, minalloc, 0xFFFF, cs + blkp, 0x80, 0,
                      hlen, cs, 0x1C)
    mzh += bytes(hdrsz - len(mzh))
    open(argv[2], 'wb').write(mzh + load)
    print('os88xpack: %s -> %s: %d bytes of image packed to %d, %d relocations, %d-byte header%s'
          % (argv[1], argv[2], len(image), len(packed), len(mz['relocs']),
             hlen, (', SPOILED (%s)' % spoil) if spoil else ''))


if __name__ == '__main__':
    main(sys.argv)
