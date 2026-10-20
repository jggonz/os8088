#!/usr/bin/env python3
"""MIDIRack's guest probe (SPEC.md 105.11): the running package's own
variables, read by NAME out of a re-assembly of the source - the shape
tests/cycweb.py's Pkg is, for the rows in tests/midirack.py.

    import mrprobe
    p = mrprobe.attach(ui)            # after ui.path('B:/APPS/MIDIRACK.O88')
    p.b('mr_state'), p.w('mrq_div'), p.data('mrc_prog', 16)
"""
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import os88geom as geom                                          # noqa: E402

SRC = os.path.join(ROOT, 'apps', 'midirack', 'midirack.asm')
INCS = ('apps/', 'apps/midirack/', 'drivers/sound/', 'drivers/')


def syms():
    """Every label AND every bss equ (MRB/MRW/MRBUF are equs off
    os88_image_end), from a map of a re-assembly - NASM lists both."""
    with tempfile.TemporaryDirectory() as d:
        cp = os.path.join(d, 'p.asm')
        mp = os.path.join(d, 'p.map')
        open(cp, 'w').write(open(SRC).read() + '\n[map symbols %s]\n' % mp)
        subprocess.run(['nasm', '-f', 'bin', '-w+error']
                       + sum([['-I', os.path.join(ROOT, i)] for i in INCS], [])
                       + ['-o', os.path.join(d, 'p.bin'), cp], check=True,
                       cwd=ROOT)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in '0123456789ABCDEF' for c in f[0]):
                out[f[2]] = int(f[0], 16)
        return out


class Probe(object):
    def __init__(self, m, seg, s):
        self.m, self.seg, self.s = m, seg, s

    def addr(self, name):
        return self.seg * 16 + self.s[name]

    def data(self, name, n=1):
        return bytes(self.m.read(self.addr(name), n))

    def b(self, name):
        return self.data(name)[0]

    def w(self, name):
        return struct.unpack('<H', self.data(name, 2))[0]

    def dw(self, name):
        return struct.unpack('<I', self.data(name, 4))[0]

    def put(self, name, raw):
        self.m.write(self.addr(name), bytes(raw))


def seg_of(m, title='MIDIRack'):
    raw = m.read(m.sym('wm_wins'), geom.MAX_WIN * geom.WIN_SIZE)
    for i in range(geom.MAX_WIN):
        b = i * geom.WIN_SIZE
        flags = struct.unpack_from('<H', raw, b + geom.W_FLAGS)[0]
        if not flags & geom.WF_USED:
            continue
        seg = struct.unpack_from('<H', raw, b + geom.W_SEG)[0]
        tp = struct.unpack_from('<H', raw, b + geom.W_TITLE)[0]
        t = bytes(m.readseg(seg or geom.KERNEL_SEG, tp, 16)).split(b'\0')[0]
        if seg and t.decode('latin-1') == title:
            return seg
    raise RuntimeError('no %s window' % title)


def attach(ui, title='MIDIRack'):
    return Probe(ui.m, seg_of(ui.m, title), syms())
