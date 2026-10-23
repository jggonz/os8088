#!/usr/bin/env python3
"""x3xtgen - the XT part's generated include (SPEC.md 121.4, 121.10;
docs/xtmp3/XT.md).

    python3 tools/x3xtgen.py --selfcheck          # the make-rule check
    python3 tools/x3xtgen.py -o build/x3xtk.inc   # the include
    python3 tools/x3xtgen.py --manifest           # layouts, sizes, fl32

apps/xtmp3/x3xt.asm is written by hand; what it can ASSEMBLE rather than
compute is generated here, from tools/x3xttab.py's design and
tools/x3simxt.py's chain:

  * the claim's LAYOUT per profile: the window segments (x3xttab's first-fit
    layout with this part's 128-byte pair rings: two doubled 16-age rings a
    pair, A and B), the synthesis segment SY (ENCV, the 18 slot records, Lee's
    byte-split planes), H1 (RQL, the IMDCT rows, ENCR, the antialias tables,
    the line buffers) and H2 (IMWIN, the overlap, the IMDCT scratch);
  * the expansion's inputs that are not in XTGEN.BIN: each window segment's
    row list (|D| as dd), the Lee constants' indices into XTGEN's LEE_C16,
    and the fl32 OPEN must reproduce over the expanded tables;
  * straight-line KERNELS whose addresses are constants: Lee's DCT-II of 32,
    16 and 8 points in place on a slot record (with the V codes), every
    profile's window pass for one slot, and the sparse IMDCT's per-line row
    blocks;
  * constant tables: the IMDCT window shapes, the short-band widths, the
    9-point DCT and twiddle constants, x3sim's pow43 and intensity factors.

--selfcheck runs the Lee code and the window passes as written here (an
interpreter over the same operation lists the NASM is printed from) against
tools/x3simxt.py on random input, so the generated code is checked before
the 8086 ever sees it; tests/x3xtunit.py then runs the assembled part.
"""
import argparse
import math
import os
import random
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import x3xttab as XT      # noqa: E402
import x3tables as TB     # noqa: E402
import x3tab_iso as ISO   # noqa: E402

# STANDARD LIBRARY ONLY at module level and on the make path (numpy is not a
# build dependency in this tree: Makefile, LOGOVID). --fullcheck is the
# numpy cross-check against tools/x3simxt.py, which imports the constants
# below from here so the model and the part have one definition of each.

# ---------------------------------------------------------- the XT chain's
# constants (x3simxt.py's docstring is the chain they belong to)
FAST_MIN = 13          # a long subband with this many nonzero lines takes the fast IMDCT (MartyPC: sparse 2,700 + 1,280 n cycles, factored 17,900)
PROFILES = {
    # name: (window taps, pack, antialias form)
    'std': (16, 'std', 'split'),
    'fast': (8, 'fast', 'tab'),
    'turbo': (4, 'turbo', 'tab'),
    'speech': (16, 'speech', 'split'),
}
TW1 = [XT.COS_C15[9 + i] for i in range(9)]       # cos(pi (19 + 2i) / 72), Q15
TW0 = [XT.COS_C15[8 - i] for i in range(9)]       # sin(pi (19 + 2i) / 72)
DCT9 = dict(TB.STD.DCT9)                           # minimp3's 9-point constants, Q15


def _wk(i):
    """The IMWIN index of the 36-point sine window's sample i (WIN36_C16
    holds the 18 distinct values ascending, then 1.0)."""
    return min(i, 35 - i)


def _block_windows():
    sine36 = [_wk(i) for i in range(36)]
    s12 = [_wk(3 * m + 1) for m in range(12)]   # sin(pi/12 (m + .5)) = sine36[3m + 1]
    w = {0: sine36,
         1: sine36[:18] + [18] * 6 + s12[6:] + [None] * 6,
         3: [None] * 6 + s12[:6] + [18] * 6 + sine36[18:]}
    return w, s12


WINDOWS, SHORT_WIN = _block_windows()


def _cos_rows(n):
    """For the n-point DCT-IV cos(pi (2r+1)(2k+1) / 4n), each entry as (row of
    the 18 |cos(pi m / 72)|, sign): x3xtmodel.R18/SG18 (n = 18) and R6/SG6
    (n = 6), in integers."""
    R, SG = [], []
    mult = 72 // (4 * n) * 2            # the angle in units of pi / 72
    for r in range(n):
        rr, ss = [], []
        for k in range(n):
            m = (2 * r + 1) * (2 * k + 1) * (mult // 2) % 144   # cos(pi m / 72)
            sg = 1
            if m > 72:
                m = 144 - m
            if m > 36:
                m = 72 - m
                sg = -1
            assert m % 2 == 1, (n, r, k, m)
            rr.append((m - 1) // 2)
            ss.append(sg)
        R.append(rr)
        SG.append(ss)
    return R, SG


R18, SG18 = _cos_rows(18)
R6, SG6 = _cos_rows(6)


def wrap(v):
    """v as a 16-bit two's complement word."""
    return ((int(v) + 32768) & 0xFFFF) - 32768


RING = 128               # a pair's two rings: 2 x (16 ages doubled) words
PNAMES = ('std', 'fast', 'turbo', 'speech')
PACK_OF = {'std': 'std', 'fast': 'std', 'turbo': 'std', 'speech': 'speech'}

# ---------------------------------------------------------------- XTGEN.BIN
def gen_offsets():
    vm = XT.DESIGN['v_map']
    o = {}
    p = 6
    o['VMAP'] = p
    p += 3 + len(vm['lens'])
    o['RMAP'] = p
    p += 2 + 2 * len(XT.r_levels())
    o['ZMAP'] = p
    p += 2 + 2 * len(XT.z_levels())
    o['TAPS8'] = p
    p += sum(1 + len(t) for t in XT.DESIGN['taps8'])
    o['TAPS4'] = p
    p += sum(1 + len(t) for t in XT.DESIGN['taps4'])
    o['LEE'] = p
    p += 4 * len(XT.LEE_C16)
    o['AA16'] = p
    p += 4 * len(XT.AA_C16)
    o['AA15'] = p
    p += 2 * len(XT.AA_C15)
    o['COS15'] = p
    p += 2 * len(XT.COS_C15)
    o['WIN16'] = p
    p += 4 * len(XT.WIN36_C16)
    o['WIN13'] = p
    p += 2 * len(XT.WIN36_C13)
    o['RQ'] = p
    nrq = XT.RQ_G1 - XT.RQ_G0 + 1
    p += 2 + 16 * nrq
    o['RQL'] = p
    p += 32 * nrq
    o['END'] = p
    g = XT.gen_input()
    assert len(g) == p, (len(g), p)
    assert struct.unpack_from('<I', g, o['LEE'])[0] == XT.LEE_C16[0]
    assert struct.unpack_from('<H', g, o['COS15'])[0] == XT.COS_C15[0]
    return o


# ------------------------------------------------------------------ layouts
def win_layout(n, div):
    """x3xttab.layout's first fit with this part's ring size."""
    save = XT.RING_BYTES
    XT.RING_BYTES = RING
    try:
        segs = XT.layout(n, div)
    finally:
        XT.RING_BYTES = save
    out = []
    for sg in segs:
        rows = list(sg['rows'])
        rowoff = {ib: 512 * k for k, ib in enumerate(rows)}
        ringoff = {}
        for k, p in enumerate(sg['pairs']):
            ringoff[p] = 512 * len(rows) + RING * k
        out.append(dict(pairs=list(sg['pairs']), rows=rows, rowoff=rowoff, ringoff=ringoff,
                        bytes=512 * len(rows) + RING * len(sg['pairs'])))
    return out


def lee_list(pack):
    """The Lee constants a pack expands, in x3xttab's order."""
    return XT.lee_consts(16 if pack == 'speech' else 32)


def paras(nbytes):
    return (nbytes + 15) // 16


H1 = {}
H1['RQL'] = 0
H1['IMR'] = H1['RQL'] + 32 * (XT.RQ_G1 - XT.RQ_G0 + 1)
H1['ENCR'] = H1['IMR'] + 18 * 512
H1['AA'] = H1['ENCR'] + 16384 + 2           # +1 sentinel byte (127), padded
H1['AA_END_SPL'] = H1['AA'] + 16 * 1024
H1['AA_END_TAB'] = H1['AA'] + 16 * 512
H1['X'] = H1['AA_END_SPL']
H1['XL'] = H1['X'] + 1152
H1['XR'] = H1['XL'] + 1152
H1['FW'] = H1['XR'] + 1152
H1['SIZE'] = H1['FW'] + 96
H2 = {}
H2['IMWIN'] = 0
H2['OVL'] = H2['IMWIN'] + 19 * 1024
H2['Z'] = H2['OVL'] + 32 * 36
H2['Y36'] = H2['Z'] + 64
H2['YS'] = H2['Y36'] + 72
H2['SIZE'] = H2['YS'] + 32 * 72
SLOT_S = 0                                  # 32 subband samples, Lee in place
SLOT_CODE = 64                              # 17 pairs x (A, B) code words
SLOT_PCM = SLOT_CODE + 17 * 4               # 32 output bytes
SLOTSZ = SLOT_PCM + 32
SY = {}
SY['ENCV'] = 0
SY['SLOT'] = 8192
SY['LEE'] = SY['SLOT'] + 18 * SLOTSZ


def layout(profile):
    n, pack, aa = PROFILES[profile]
    div = 2 if profile == 'speech' else 1
    ws = win_layout(n, div)
    lee = lee_list(pack)
    sy = SY['LEE'] + 1024 * len(lee)
    segs = [('W%d' % i, s['bytes']) for i, s in enumerate(ws)]
    segs += [('SY', sy), ('H1', H1['SIZE']), ('H2', H2['SIZE'])]
    off = 0
    base = {}
    for name, nb in segs:
        assert nb <= 65536, (profile, name, nb)
        base[name] = off
        off += paras(nb)
    return dict(n=n, pack=pack, aa=aa, div=div, win=ws, lee=lee, sy=sy, segs=segs,
                base=base, paras=off)


# --------------------------------------------------------- expanded bytes
def expanded(profile):
    """The table bytes OPEN writes, region by region, in the order fl32 is
    taken: each window segment's rows, SY's ENCV and Lee planes, H1's RQL,
    IMDCT rows, ENCR + sentinel and the antialias tables, H2's IMWIN."""
    L = layout(profile)
    vm = XT.DESIGN['v_map']
    lvR = XT.r_levels()
    out = []
    for s in L['win']:
        out.append(b''.join(XT.words(XT.win_row_recurrence(ib, vm['lens'], vm['s0']))
                            for ib in s['rows']))
    out.append(XT.sbytes(XT.enc_v()))
    out.append(b''.join(XT.split_planes(c) for c in L['lee']))
    out.append(XT.words([v for row in XT.rql_table() for v in row]))
    out.append(b''.join(XT.words(XT.mul_row(c, lvR, 15)) for c in XT.COS_C15))
    out.append(XT.sbytes(XT.enc_r()) + bytes([127, 0]))
    if L['aa'] == 'split':
        out.append(b''.join(XT.split_planes(c) for c in XT.AA_C16))
    else:
        out.append(b''.join(XT.words(XT.mul_row(c, lvR, 15)) for c in XT.AA_C15))
    out.append(b''.join(XT.split_planes(c) for c in XT.WIN36_C16))
    return out


def fl32_chain(blobs):
    """XT.fl32 continued across the regions (s1, s2 carried)."""
    s1 = s2 = 0
    for b in blobs:
        assert len(b) % 2 == 0
        for (w,) in struct.iter_unpack('<H', b):
            s1 = (s1 + w) & 0xFFFF
            s2 = (s2 + s1) & 0xFFFF
    return (s2 << 16) | s1


# ===================================================================== LEE
def lee_ops(N):
    """Lee's DCT-II of size N IN PLACE on N words (x3xtmodel.DctPlan's
    recursion): ('bf', a, b, c16, n) is a = a + b, b = split(a - b, c16), at
    the recursion's size n; ('add', o, o1) is o = o + o1. Returns (ops,
    where) with where[l] the word that holds X[l] at the end."""
    ops = []

    def rec(xs):
        n = len(xs)
        if n == 1:
            return list(xs)
        a = xs[:n // 2]
        b = xs[::-1][:n // 2]
        for k in range(n // 2):
            c = 0.5 / math.cos((2 * k + 1) * math.pi / (2 * n))
            ops.append(('bf', a[k], b[k], int(round(c * 65536)), n))
        ev = rec(a)
        od = rec(b)
        out = [None] * n
        for m in range(n // 2):
            out[2 * m] = ev[m]
        for m in range(n // 2 - 1):
            ops.append(('add', od[m], od[m + 1]))
            out[2 * m + 1] = od[m]
        out[n - 1] = od[n // 2 - 1]
        return out
    where = rec(list(range(N)))
    return ops, where


def lee_run(ops, x, smul):
    """Interpret lee_ops on a list of ints (wrapping), with smul(v, c16)."""
    x = list(x)
    for op in ops:
        if op[0] == 'bf':
            _, a, b, c = op[:4]
            s = wrap(x[a] + x[b])
            d = wrap(x[a] - x[b])
            x[a] = int(s)
            x[b] = int(smul(d, c))
        else:
            _, o, o1 = op
            x[o] = int(wrap(x[o] + x[o1]))
    return x


def code_pairs(div):
    """For a divisor: the pairs (p, A index, B index) into the N-point X,
    A = X[16+p] (None for p = 16), B = X[16-p] (X[16] for p = 0, X[0] for
    p = 16), every index divided by div."""
    out = []
    for p in range(0, 17, div):
        a = None if p == 16 else (16 + p) // div
        b = (16 - p) // div if p else 16 // div
        out.append((p, a, b))
    return out


def _split(pb):
    """AX times the constant at planes pb into DX (BH = 0)."""
    return ['    mov bl,ah', '    mov dl,[bx+%d]' % (pb + 512), '    mov dh,[bx+%d]' % (pb + 768),
            '    mov bl,al', '    add dl,[bx+%d]' % pb, '    adc dh,[bx+%d]' % (pb + 256)]


def _add_run(run):
    """A run of Lee's odd adds (o = o + o1) with CX, SI and BP as a small
    write-back cache: a value is loaded once while it stays in a register,
    every add is register to register or register from memory, and each
    changed value is stored once when the run ends (or its register is
    taken). The same adds, in the same order: wrapping makes order free
    anyway, and the stored words are exactly the one-at-a-time ones."""
    regs = ['cx', 'si', 'bp']
    held = {}          # slot -> register
    dirty = set()
    lru = []           # slots, least recent first
    L = []

    def touch(sl):
        if sl in lru:
            lru.remove(sl)
        lru.append(sl)

    def later(sl, i):
        return any(sl in (o, o1) for _, o, o1 in run[i + 1:])

    def grab(i):
        free = [r for r in regs if r not in held.values()]
        if free:
            return free[0]
        # evict: the least recently used slot not needed by this add
        for sl in lru:
            if sl not in (run[i][1], run[i][2]):
                r = held.pop(sl)
                lru.remove(sl)
                if sl in dirty:
                    L.append('    mov [di+%d],%s' % (2 * sl, r))
                    dirty.discard(sl)
                return r
        raise AssertionError('no register')

    for i, (_, o, o1) in enumerate(run):
        if o not in held:
            r = grab(i)
            L.append('    mov %s,[di+%d]' % (r, 2 * o))
            held[o] = r
        ro = held[o]
        touch(o)
        if o1 in held:
            L.append('    add %s,%s' % (ro, held[o1]))
            touch(o1)
        elif later(o1, i):
            r = grab(i)
            L.append('    mov %s,[di+%d]' % (r, 2 * o1))
            held[o1] = r
            touch(o1)
            L.append('    add %s,%s' % (ro, r))
        else:
            L.append('    add %s,[di+%d]' % (ro, 2 * o1))
        dirty.add(o)
    for sl in sorted(dirty):
        L.append('    mov [di+%d],%s' % (2 * sl, held[sl]))
    return L


def lee_asm(N, lee, label):
    """NASM for one slot: DS = SY, DI = the slot record, BH = 0. In place,
    then each pair's two V codes into the record's code area. Ends with
    BH = 0. Clobbers AX, BX, CX, DX, SI, BP.

    Every DCT-4 at the bottom of the recursion - two butterflies, two
    DCT-2s and the odd add, five operations on four words - runs in
    registers (CX, SI, BP beside the multiply's AX and DX): four loads and
    four stores where the operations one at a time make nineteen memory
    accesses (MartyPC: matrixing was a quarter of XT Standard)."""
    ops, where = lee_ops(N)
    plane = {c: SY['LEE'] + 1024 * i for i, c in enumerate(lee)}
    L = ['%s:' % label]
    i = 0
    while i < len(ops):
        op = ops[i]
        if op[0] == 'bf' and op[4] == 4:
            g = ops[i:i + 5]
            x0, x3, c40 = g[0][1], g[0][2], g[0][3]
            x1, x2, c41 = g[1][1], g[1][2], g[1][3]
            assert g[2][:3] == ('bf', x0, x1) and g[2][4] == 2, g
            assert g[3][:3] == ('bf', x3, x2) and g[3][4] == 2 and g[3][3] == g[2][3], g
            assert g[4] == ('add', x3, x2), g
            c2 = g[2][3]
            L += ['    mov ax,[di+%d]' % (2 * x0), '    mov cx,[di+%d]' % (2 * x3),
                  '    mov si,ax', '    add si,cx',
                  '    sub ax,cx']
            L += _split(plane[c40])
            L += ['    mov bp,dx',
                  '    mov ax,[di+%d]' % (2 * x1), '    mov cx,[di+%d]' % (2 * x2),
                  '    mov dx,ax', '    sub ax,cx', '    add cx,dx']
            L += _split(plane[c41])
            # SI x0', CX x1', BP x3', DX x2'
            L += ['    mov ax,si', '    sub ax,cx', '    add si,cx', '    mov cx,dx']
            L += _split(plane[c2])
            L += ['    mov [di+%d],dx' % (2 * x1), '    mov [di+%d],si' % (2 * x0),
                  '    mov ax,bp', '    sub ax,cx', '    add bp,cx']
            L += _split(plane[c2])
            L += ['    mov [di+%d],dx' % (2 * x2), '    add bp,dx', '    mov [di+%d],bp' % (2 * x3)]
            i += 5
            continue
        if op[0] == 'bf':
            _, a, b, c = op[:4]
            L += ['    mov ax,[di+%d]' % (2 * a), '    mov cx,[di+%d]' % (2 * b),
                  '    mov dx,ax', '    add dx,cx', '    mov [di+%d],dx' % (2 * a),
                  '    sub ax,cx']
            L += _split(plane[c])
            L += ['    mov [di+%d],dx' % (2 * b)]
            i += 1
            continue
        j = i
        while j < len(ops) and ops[j][0] == 'add':
            j += 1
        L += _add_run(ops[i:j])
        i = j
    # the V codes: each needed X value once, three to a group, the rare slow
    # path (|x| >= 2 FS) out of line after its group
    div = 32 // N
    need = {}
    for p, a, b in code_pairs(div):
        if a is not None:
            need.setdefault(a, []).append(SLOT_CODE + 4 * (p // div))
        need.setdefault(b, []).append(SLOT_CODE + 4 * (p // div) + 2)
    items = list(enumerate(sorted(need.items())))
    for g0 in range(0, len(items), 3):
        grp = items[g0:g0 + 3]
        for k, (l, dests) in grp:
            L += ['    mov ax,[di+%d]' % (2 * where[l]), '    add ax,4096',
                  '    cmp ax,8191', '    ja .s%d' % k, '    xchg bx,ax',
                  '    mov al,[bx+%d]' % SY['ENCV'], '    cbw', '    shl ax,1', '.b%d:' % k]
            for d in dests:
                L.append('    mov [di+%d],ax' % d)
        L.append('    jmp short .g%d' % g0)
        for k, _ in grp:
            L += ['.s%d:' % k, '    call [cs:x3x_vslowp]', '    jmp short .b%d' % k]
        L.append('.g%d:' % g0)
    L += ['    xor bh,bh', '    ret']
    return L, ops, where, need


# ================================================================== WINDOW
def win_pairs(profile, div):
    """Per kept pair: (p, segment, ring offset, outputs) with outputs a list
    of (PCM index, [(age, row offset, '+'/'-')])."""
    L = layout(profile)
    D = XT.iso_window_int()
    taps = XT.kept_taps(L['n'])
    seg_of = {}
    for g, s in enumerate(L['win']):
        for p in s['pairs']:
            seg_of[p] = g
    out = []
    for p in range(0, 17, div):
        g = seg_of[p]
        s = L['win'][g]
        outs = []
        for j, other in ((p, False), (32 - p, True)):
            if j == 32 or (other and p in (0, 16)):
                continue
            tl = []
            for i in taps[j]:
                d = D[32 * i + j]
                if not d:
                    continue
                if j == 16 and i % 2 == 0:
                    continue                    # V[16] = 0: no product
                # output j (p): even ages +A, odd ages -B; output 32-p: both -
                if not other:
                    sg = (1 if d > 0 else -1) * (1 if i % 2 == 0 else -1)
                else:
                    sg = (1 if d > 0 else -1) * -1
                tl.append((i, s['rowoff'][abs(d)], '+' if sg > 0 else '-'))
            outs.append((j // div, tl))
        out.append((p, g, s['ringoff'][p], outs))
    return out


def win_asm(profile, div, label):
    """NASM for one slot of the window pass. In: ES = SY, BP = the slot
    record, DI = rA (64 * (s & 1) + 2 * ((-s) & 15)); DS is loaded here
    from x3x_wseg. Writes the slot's PCM. Clobbers AX, BX, CX, DX, SI, DS."""
    n = PROFILES[profile][0]
    lodsw = n == 16
    pairs = sorted(win_pairs(profile, div), key=lambda q: (q[1], q[0]))
    L = ['%s:' % label]
    cur = None
    for p, g, ring, outs in pairs:
        if g != cur:
            L += ['    mov ds,[cs:x3x_wseg+%d]' % (2 * g)]
            cur = g
        cp = SLOT_CODE + 4 * (p // div)
        if p != 16:
            L += ['    mov ax,[es:bp+%d]' % cp, '    mov [di+%d],ax' % ring,
                  '    mov [di+%d],ax' % (ring + 32)]
        L += ['    mov ax,[es:bp+%d]' % (cp + 2), '    xor di,64', '    mov [di+%d],ax' % ring,
              '    mov [di+%d],ax' % (ring + 32), '    xor di,64']
        accs = ['dx', 'cx'][:len(outs)]
        for r in accs:
            L.append('    mov %s,32' % r)
        ages = sorted(set(a for _, tl in outs for a, _, _ in tl))
        use_lodsw = lodsw and len(ages) > 8
        if use_lodsw:
            L.append('    lea si,[di+%d]' % ring)
            last = -1
        for age in ages:
            if use_lodsw:
                while last < age - 1:
                    L.append('    lodsw')          # an age no output of the pair reads
                    last += 1
                L += ['    lodsw', '    xchg bx,ax']
                last = age
            else:
                L.append('    mov bx,[di+%d]' % (ring + 2 * age))
            for (m, tl), r in zip(outs, accs):
                for a, ro, sg in tl:
                    if a == age:
                        L.append('    %s %s,[bx+%d]' % ('add' if sg == '+' else 'sub', r, ro + 256))
        # PCM8: two shl/jo pairs catch |acc| >= 2^13 and the high byte is
        # then acc >> 6. On a clip CF holds the sign (bit 15 at the first jo,
        # bit 14 = bit 15 at the second), which the stub reads after `inc`
        for k, ((m, tl), r) in enumerate(zip(outs, accs)):
            lab = '.p%do%d' % (p, k)
            L += ['    shl %s,1' % r, '    jo %sc' % lab, '    shl %s,1' % r,
                  '    jo %sc' % lab, '    mov al,%sh' % r[0], '    xor al,80h',
                  '%sw:' % lab, '    mov [es:bp+%d],al' % (SLOT_PCM + m)]
        L.append('    jmp short .p%dx' % p)
        for k, ((m, tl), r) in enumerate(zip(outs, accs)):
            lab = '.p%do%d' % (p, k)
            L += ['%sc:' % lab, '    inc word [cs:x3x_clips]', '    mov al,0',
                  '    jc %sw' % lab, '    dec ax', '    jmp short %sw' % lab]
        L.append('.p%dx:' % p)
    L.append('    ret')
    return L


def win_run(profile, div, rows, ring_codes, slot_codes):
    """Interpret the window pass for one slot: rows {offset: row}, the two
    rings' read sequence given per pair as the 16 code words by age.
    Returns PCM bytes by output index."""
    out = {}
    clips = 0
    for p, g, ring, outs in win_pairs(profile, div):
        seq = ring_codes[p]
        for m, tl in outs:
            acc = 32
            for a, ro, sg in tl:
                v = rows[g][ro][seq[a] // 2 + 128]
                acc += v if sg == '+' else -v
            acc = int(wrap(acc))
            if acc >= 8192:
                out[m] = 255
                clips += 1
            elif acc < -8192:
                out[m] = 0
                clips += 1
            else:
                out[m] = (acc >> 6) + 128
    return out, clips


# ============================================================ SPARSE IMDCT
def imdct_blocks():
    """Per long input line k: 18 row adds into Z; per short input k: 6."""
    L = []
    for k in range(18):
        L.append('x3x_im18_%d:' % k)
        for r in range(18):
            ro = H1['IMR'] + 512 * R18[r][k] + 256
            op = 'add' if SG18[r][k] > 0 else 'sub'
            L += ['    mov ax,[bx+%d]' % ro, '    %s [es:%d],ax' % (op, H2['Z'] + 2 * r)]
        L.append('    ret')
    for k in range(6):
        L.append('x3x_im6_%d:' % k)
        for r in range(6):
            ro = H1['IMR'] + 512 * R6[r][k] + 256
            op = 'add' if SG6[r][k] > 0 else 'sub'
            L += ['    mov ax,[bx+%d]' % ro, '    %s [es:%d],ax' % (op, H2['Z'] + 2 * r)]
        L.append('    ret')
    L.append('x3x_im18tab:')
    L += ['    dw x3x_im18_%d' % k for k in range(18)]
    L.append('x3x_im6tab:')
    L += ['    dw x3x_im6_%d' % k for k in range(6)]
    return L


# ===================================================== the IMDCT window
def shape_code():
    """The IMDCT window, unrolled per shape (DS = H2, BH = 0): a long block's
    36 outputs from Z (folded, negated where the fold says) times IMWIN into
    Y36; a short window's 12 ADDED into Y36 at [di + 12 + 2i] (DI = Y36 +
    12 w). Clobbers AX, BX, DX."""
    L = []
    zsrc36 = []
    for i in range(36):
        if i < 9:
            zsrc36.append((i + 9, False))
        elif i < 27:
            zsrc36.append((26 - i, True))
        else:
            zsrc36.append((i - 27, True))
    zsrc12 = []
    for i in range(12):
        if i < 3:
            zsrc12.append((i + 3, False))
        elif i < 9:
            zsrc12.append((8 - i, True))
        else:
            zsrc12.append((i - 9, True))

    def mul(pl):
        return ['    mov bl,ah', '    mov dl,[bx+%d]' % (pl + 512), '    mov dh,[bx+%d]' % (pl + 768),
                '    mov bl,al', '    add dl,[bx+%d]' % pl, '    adc dh,[bx+%d]' % (pl + 256)]
    for bt in (0, 1, 3):
        L.append('x3x_wlong%d:' % bt)
        for i in range(36):
            k = WINDOWS[bt][i]
            z, neg = zsrc36[i]
            if k is None:
                L.append('    mov word [%d],0' % (H2['Y36'] + 2 * i))
                continue
            L.append('    mov ax,[%d]' % (H2['Z'] + 2 * z))
            if neg:
                L.append('    neg ax')
            L += mul(H2['IMWIN'] + 1024 * k)
            L.append('    mov [%d],dx' % (H2['Y36'] + 2 * i))
        L.append('    ret')
    # mode 0: the window AND the overlap, no Y36. DS = H2, ES = SY, SI = the
    # subband's overlap row, DI = its slot column, BP = 0 (an even subband)
    # or FFFFh (odd: xor/sub negates its odd slots), BH = 0
    for bt in (0, 1, 3):
        L.append('x3x_wfuse%d:' % bt)
        for i in range(36):
            k = WINDOWS[bt][i]
            z, neg = zsrc36[i]
            if i < 18:
                if k is None:
                    L.append('    mov dx,[si+%d]' % (2 * i))
                else:
                    L.append('    mov ax,[%d]' % (H2['Z'] + 2 * z))
                    if neg:
                        L.append('    neg ax')
                    L += mul(H2['IMWIN'] + 1024 * k)
                    L.append('    add dx,[si+%d]' % (2 * i))
                if i & 1:
                    L += ['    xor dx,bp', '    sub dx,bp']
                L.append('    mov [es:di+%d],dx' % (i * SLOTSZ))
            else:
                if k is None:
                    L.append('    mov word [si+%d],0' % (2 * (i - 18)))
                    continue
                L.append('    mov ax,[%d]' % (H2['Z'] + 2 * z))
                if neg:
                    L.append('    neg ax')
                L += mul(H2['IMWIN'] + 1024 * k)
                L.append('    mov [si+%d],dx' % (2 * (i - 18)))
        L.append('    ret')
    L.append('x3x_wshort:')
    for i in range(12):
        z, neg = zsrc12[i]
        L.append('    mov ax,[%d]' % (H2['Z'] + 2 * z))
        if neg:
            L.append('    neg ax')
        L += mul(H2['IMWIN'] + 1024 * SHORT_WIN[i])
        L.append('    add [di+%d],dx' % (12 + 2 * i))
    L.append('    ret')
    return L


# ================================================================== tables
def const_tables():
    L = []
    C = DCT9
    L.append('x3x_dct9:     ; c1 c2 c3 c4 c5 c6 s3, Q15')
    L.append('    dw %s' % ', '.join(str(C[k]) for k in ('c1', 'c2', 'c3', 'c4', 'c5', 'c6', 's3')))
    L.append('x3x_tw:       ; per i: -tw1, -tw0, tw1 (Q15): z[9+i], z[8-i]')
    for i in range(9):
        L.append('    dw %d, %d, %d' % (-TW1[i], -TW0[i], TW1[i]))
    Q = TB.STD
    L.append('x3x_p43f:     dw %s' % ', '.join(str(v) for v in Q.P43_F))
    L.append('x3x_p43m:')
    m = list(Q.P43_M)
    for i in range(0, len(m), 16):
        L.append('    dw %s' % ', '.join(str(v) for v in m[i:i + 16]))
    L.append('x3x_p43e:')
    e = list(Q.P43_E)
    for i in range(0, len(e), 32):
        L.append('    db %s' % ', '.join(str(v) for v in e[i:i + 32]))
    L.append('x3x_is1:      ; MPEG-1 kl, kr (Q14) by is_pos; then with M/S')
    for tab in (Q.IS1_Q, Q.IS1_QMS):
        L.append('    dw %s' % ', '.join('%d, %d' % kk for kk in tab))
    L.append('x3x_is2:      ; MPEG-2 k by scale (0, 1) and is_pos; then with M/S')
    for tab in (Q.IS2_Q, Q.IS2_QMS):
        for s in (0, 1):
            L.append('    dw %s' % ', '.join(str(v) for v in tab[s]))
    L.append('X3X_SQRT2_Q14 equ %d' % Q.SQRT2_Q14)
    # ENCV slow path: twice the midpoints from the first level at or above
    # 4096 (the table covers |v| < 4096)
    lv = XT.v_levels()
    k0 = max(0, next(k for k in range(len(lv)) if lv[k] >= 4096) - 1)
    L.append('X3X_VSLOW_K0 equ %d' % k0)
    L.append('x3x_vmid2:    ; lv[k] + lv[k+1] for k = K0..126, then FFFFh')
    mids = [lv[k] + lv[k + 1] for k in range(k0, len(lv) - 1)] + [0xFFFF]
    L.append('    dw %s' % ', '.join(str(v) for v in mids))
    return L


def profile_data(profile, tail=False):
    """The descriptor OPEN reads, and the per-profile constants. RESIDENT:
    CAPS' block and the window segments' table (RESET zeroes the rings from
    it). TAIL (only OPEN reads it): the row lists and the Lee constants'
    indices."""
    L = layout(profile)
    P = profile.upper()
    out = []
    if tail:
        for g, s_ in enumerate(L['win']):
            out.append('x3x_%s_rows%d:' % (profile, g))
            for i in range(0, len(s_['rows']), 8):
                out.append('    dd %s' % ', '.join(str(v) for v in s_['rows'][i:i + 8]))
        out.append('x3x_%s_lee:   ; indices into XTGEN LEE_C16, in plane order' % profile)
        out.append('    db %s' % ', '.join(str(XT.LEE_C16.index(c)) for c in L['lee']))
        return out
    out.append('X3X_%s_PARAS equ %d' % (P, L['paras']))
    out.append('X3X_%s_NWSEG equ %d' % (P, len(L['win'])))
    out.append('X3X_%s_NLEE equ %d' % (P, len(L['lee'])))
    fl = fl32_chain(expanded(profile))
    out.append('X3X_%s_FL32_LO equ 0%04Xh' % (P, fl & 0xFFFF))
    out.append('X3X_%s_FL32_HI equ 0%04Xh' % (P, fl >> 16))
    # CAPS' block (x3gran.inc X3C_NSEG, X3C_PARA[6], X3C_TKB): the segments in
    # OPEN's order - the window segments, SY, H1, H2 - each its own claim
    paras_ = [paras(nb) for _, nb in L['segs']]
    tkb = (sum(len(b) for b in expanded(profile)) + 1023) // 1024
    out.append('x3x_%s_caps:   ; db segments, 6 dw paragraphs, dw KB of tables' % profile)
    out.append('    db %d' % len(paras_))
    out.append('    dw %s' % ', '.join(str(v) for v in paras_ + [0] * (6 - len(paras_))))
    out.append('    dw %d' % tkb)
    out.append('x3x_%s_wsegs:  ; per window segment: paragraph offset, rows, ring offset, ring bytes, row list' % profile)
    for g, s_ in enumerate(L['win']):
        out.append('    dw %d, %d, %d, %d, x3x_%s_rows%d' % (
            L['base']['W%d' % g], len(s_['rows']), 512 * len(s_['rows']),
            RING * len(s_['pairs']), profile, g))
    return out


KERNELS = (('lee32', 32, 'std'), ('lee16', 16, 'speech'), ('lee8', 8, 'speech'))
WINKERNELS = (('win_std', 'std', 1), ('win_fast', 'fast', 1), ('win_turbo', 'turbo', 1),
              ('win_sp2', 'speech', 2), ('win_sp4', 'speech', 4))
PAIRING = (('std', 'win_std', 'lee32'), ('fast', 'win_fast', 'lee32'),
           ('turbo', 'win_turbo', 'lee32'), ('sp2', 'win_sp2', 'lee16'),
           ('sp4', 'win_sp4', 'lee8'))


def kernel_lines():
    """{name: NASM lines} for every profile's Lee and window kernels."""
    out = {}
    for name, N, pack in KERNELS:
        out[name] = lee_asm(N, lee_list(pack), 'x3x_' + name)[0]
    for name, prof, div in WINKERNELS:
        out[name] = win_asm(prof, div, 'x3x_' + name)
    return out


def kernel_sizes(kern):
    """Each kernel's assembled bytes, measured by assembling it alone (its
    only outside references are absolute [cs:disp16] words, whose encoding
    is the same wherever they land)."""
    import subprocess
    import tempfile
    nasm = os.environ.get('NASM', 'nasm').split()[0]
    sizes = {}
    with tempfile.TemporaryDirectory() as d:
        for name, lines in kern.items():
            src = os.path.join(d, 'k.asm')
            out = os.path.join(d, 'k.bin')
            with open(src, 'w') as f:
                f.write('cpu 8086\nbits 16\norg 0\n'
                        'x3x_wseg equ 1000h\nx3x_clips equ 1000h\nx3x_vslowp equ 1000h\n')
                f.write('\n'.join(lines) + '\n')
            subprocess.run([nasm, '-f', 'bin', '-w+error', '-o', out, src], check=True)
            sizes[name] = os.path.getsize(out)
    return sizes


def emit(path):
    go = gen_offsets()
    L = ['; GENERATED by tools/x3xtgen.py - do not edit (SPEC.md 121.4, docs/xtmp3/XT.md)',
         '; Included twice: the equates at the top of x3xt.asm, and with',
         '; X3XTK_BODY defined the tables and code at its end.', '%ifndef X3XTK_BODY']
    L.append('X3X_GEN_VERSION equ %d' % XT.GEN_VERSION)
    L.append('X3X_GEN_BYTES equ %d' % go['END'])
    L.append('X3X_GEN_CRC_LO equ 0%04Xh' % (zlib.crc32(XT.gen_input()) & 0xFFFF))
    L.append('X3X_GEN_CRC_HI equ 0%04Xh' % ((zlib.crc32(XT.gen_input()) >> 16) & 0xFFFF))
    gfl = XT.fl32(XT.gen_input())
    L.append('X3X_GEN_FL32_LO equ 0%04Xh' % (gfl & 0xFFFF))
    L.append('X3X_GEN_FL32_HI equ 0%04Xh' % (gfl >> 16))
    for k, v in go.items():
        L.append('XG_%s equ %d' % (k, v))
    L.append('XG_NRQ equ %d' % (XT.RQ_G1 - XT.RQ_G0 + 1))
    L.append('X3X_RQ_G0 equ %d' % XT.RQ_G0)
    L.append('X3X_RQ_G1 equ %d' % XT.RQ_G1)
    L.append('X3X_FAST_MIN equ %d' % FAST_MIN)
    for k, v in H1.items():
        L.append('X3X_H1_%s equ %d' % (k, v))
    for k, v in H2.items():
        L.append('X3X_H2_%s equ %d' % (k, v))
    for k, v in SY.items():
        L.append('X3X_SY_%s equ %d' % (k, v))
    L += ['X3X_SLOT_S equ %d' % SLOT_S, 'X3X_SLOT_CODE equ %d' % SLOT_CODE,
          'X3X_SLOT_PCM equ %d' % SLOT_PCM, 'X3X_SLOTSZ equ %d' % SLOTSZ,
          'X3X_RING equ %d' % RING, '']
    kern = kernel_lines()
    ks = kernel_sizes(kern)
    for name, size in ks.items():
        L.append('X3X_K_%s equ %d' % (name.upper(), size))
    L.append('X3X_KSLOT equ %d     ; the largest window + Lee pair a profile copies in'
             % max(ks[w] + ks[l] for _, w, l in PAIRING))
    L.append('%elifndef X3XTK_TAIL')
    L.append('; ---- RESIDENT: profile descriptors')
    for p in PNAMES:
        L += profile_data(p)
    L.append('; ---- constant tables')
    L += const_tables()
    L += shape_code()
    L.append('; ---- the sparse IMDCT row blocks')
    L += imdct_blocks()
    L.append('%else')
    L.append('; ---- TAIL (OPEN reads it, and copies the chosen kernels out):')
    L.append('; the row lists, the Lee constants, every Lee and window kernel')
    for p in PNAMES:
        L += profile_data(p, tail=True)
    for name in [k[0] for k in KERNELS] + [k[0] for k in WINKERNELS]:
        L += kern[name]
        L.append('x3x_%s_end:' % name)
    L.append('%endif')
    with open(path, 'w') as f:
        f.write('\n'.join(L) + '\n')


# ================================================================ selfcheck
def _smul_tab():
    """x * c16 through the generator's own split rows (x3xttab)."""
    cache = {}

    def smul(v, c):
        if c not in cache:
            cache[c] = XT.split_recurrence(c)
        H, L = cache[c]
        v &= 0xFFFF
        return wrap(H[(v >> 8) & 255] + L[v & 255])
    return smul


def _dct2(x):
    N = len(x)
    return [sum(x[k] * math.cos(math.pi * n * (2 * k + 1) / (2 * N)) for k in range(N))
            for n in range(N)]


def selfcheck(verbose=False):
    """Standard library only (the make rule's): Lee's in-place operation
    lists ARE the DCT-II (run in floats with exact multiplies), the window
    pass's tap signs and rows are the window's (against the V-domain formula
    written out here), the cos sign tables are the DCT-IVs, the layouts fit,
    XTGEN.BIN's offsets are what the part reads. tests/x3xtunit.py and
    --fullcheck hold the arithmetic to tools/x3simxt.py."""
    fails = []
    rnd = random.Random(1234)
    for N in (32, 16, 8):
        ops, where = lee_ops(N)
        for _ in range(5):
            x = [rnd.uniform(-1, 1) for _ in range(N)]
            y = list(x)
            for op in ops:
                if op[0] == 'bf':
                    _, a_, b_, c = op[:4]
                    y[a_], y[b_] = y[a_] + y[b_], (y[a_] - y[b_]) * (c / 65536.0)
                else:
                    y[op[1]] = y[op[1]] + y[op[2]]
            ref = _dct2(x)
            if max(abs(y[where[l]] - ref[l]) for l in range(N)) > 1e-3:
                fails.append('Lee %d: the operation list is not the DCT-II' % N)
                break
    for n, R, SG in ((18, R18, SG18), (6, R6, SG6)):
        for r in range(n):
            for k in range(n):
                v = math.cos(math.pi * (2 * r + 1) * (2 * k + 1) / (4 * n))
                c = math.cos(math.pi * (2 * R[r][k] + 1) / 72) * SG[r][k]
                if abs(v - c) > 1e-12:
                    fails.append('cos table %d (%d, %d)' % (n, r, k))
    # the window pass against the V-domain sum, codes by age
    smul = _smul_tab()
    D = XT.iso_window_int()
    vm = XT.DESIGN['v_map']
    for prof, div in (('std', 1), ('fast', 1), ('turbo', 1), ('speech', 2), ('speech', 4)):
        L = layout(prof)
        rows = [{s_['rowoff'][ib]: XT.win_row_recurrence(ib, vm['lens'], vm['s0'])
                 for ib in s_['rows']} for s_ in L['win']]
        taps = XT.kept_taps(L['n'])
        nX = 32 // div
        for _ in range(8):
            hist = [[rnd.randint(-127, 127) for _ in range(nX)] for _ in range(16)]
            ring = {}
            for p, a, b in code_pairs(div):
                ring[p] = [2 * (hist[age][a] if (age % 2 == 0 and a is not None) else
                                (hist[age][b] if age % 2 else 0)) for age in range(16)]
            got, _ = win_run(prof, div, rows, ring, None)
            for m, j in enumerate(range(0, 32, div)):
                acc = 32
                for i in taps[j]:
                    d = D[32 * i + j]
                    if not d:
                        continue
                    X = [0] * 32
                    for l in range(nX):
                        X[div * l] = hist[i][l]
                    # V from X: V[i] = X[16+i] (i < 16), V[16] = 0, V[32-i] = -V[i],
                    # V[48+i] = -X[i], V[96-i] = V[i] (i = 49..63)
                    V = [0] * 64
                    for q in range(16):
                        V[q] = X[16 + q]
                    for q in range(1, 16):
                        V[32 - q] = -V[q]
                    V[32] = -V[0]
                    for q in range(16):
                        V[48 + q] = -X[q]
                    for q in range(49, 64):
                        V[96 - q] = V[q]
                    c = V[j] if i % 2 == 0 else V[32 + j]
                    row = XT.win_row_recurrence(abs(d), vm['lens'], vm['s0'])
                    acc += (1 if d > 0 else -1) * row[c + 128]
                acc = wrap(acc)
                want = 255 if acc >= 8192 else 0 if acc < -8192 else (acc >> 6) + 128
                if got.get(m) != want:
                    fails.append('window %s d%d output %d: %r != %r' % (prof, div, j, got.get(m), want))
                    break
    for p in PNAMES:
        for name, nb in layout(p)['segs']:
            if nb > 65536:
                fails.append('%s %s over 64 KiB' % (p, name))
    gen_offsets()
    if fails:
        for f in fails[:20]:
            print('x3xtgen: FAIL', f)
        return 1
    if verbose:
        print('x3xtgen: selfcheck ok')
    return 0


def fullcheck(verbose=False):
    """numpy: the in-place Lee with the split multiplies against
    tools/x3simxt.py's DctPlan arithmetic, and the cos sign tables against
    x3xtmodel's."""
    import numpy as np
    import x3simxt as SX
    import x3xtmodel as XM
    fails = []
    if [list(r) for r in XM.R18] != R18 or [list(r) for r in XM.SG18] != SG18:
        fails.append('R18/SG18 differ from x3xtmodel')
    if [list(r) for r in XM.R6] != R6 or [list(r) for r in XM.SG6] != SG6:
        fails.append('R6/SG6 differ from x3xtmodel')
    rnd = random.Random(99)
    T = SX.Tabs('std')
    for N in (32, 16, 8):
        ops, where = lee_ops(N)
        plan = XM.DctPlan(N)
        for _ in range(40):
            amp = rnd.choice((50, 2000, 30000))
            x = [rnd.randint(-amp, amp) for _ in range(N)]
            got = lee_run(ops, x, T.smul)
            ref = SX.wrap(plan.run(np.array([x], dtype=np.int64),
                                   lambda v, c, n: T.smul(v, SX._c16(c))))[0]
            if [got[where[l]] for l in range(N)] != [int(v) for v in ref]:
                fails.append('Lee %d in place differs from DctPlan' % N)
                break
    if fails:
        for f in fails:
            print('x3xtgen: FAIL', f)
        return 1
    if verbose:
        print('x3xtgen: fullcheck ok')
    return 0


def seal(path):
    """Stamp the part's tail Fletcher-32 (x3xt.asm's x3x_open checks the
    tail - OPEN's half - against it before entering it). The header is 18
    bytes; at 18 and 20 the image holds the tail's and the end's offsets,
    and the sum goes at 22. Returns (tail offset, end offset)."""
    b = bytearray(open(path, 'rb').read())
    tail, end = struct.unpack_from('<HH', b, 18)
    assert b[0:2] == b'X3' and 0 < tail < end <= len(b) and (end - tail) % 2 == 0, (tail, end)
    fl = XT.fl32(bytes(b[tail:end]))
    struct.pack_into('<HH', b, 22, fl & 0xFFFF, fl >> 16)
    open(path, 'wb').write(bytes(b))
    return tail, end


def xtgen(path):
    with open(path, 'wb') as f:
        f.write(XT.gen_input())


def manifest():
    for p in PNAMES:
        L = layout(p)
        fl = fl32_chain(expanded(p))
        tb = sum(len(b) for b in expanded(p))
        print('%-6s %6d bytes claimed (%d paragraphs), %6d of tables, fl32 %08X, segments %s' % (
            p, 16 * L['paras'], L['paras'], tb, fl,
            ', '.join('%s %d' % s for s in L['segs'])))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('-o', metavar='INC')
    ap.add_argument('--selfcheck', action='store_true')
    ap.add_argument('--manifest', action='store_true')
    ap.add_argument('--fullcheck', action='store_true')
    ap.add_argument('--xtgen', metavar='BIN', help='write XTGEN.BIN (x3xttab.gen_input)')
    ap.add_argument('--seal', metavar='PART', help="stamp x3xt.bin's tail Fletcher-32")
    a = ap.parse_args()
    if a.seal:
        tail, end = seal(a.seal)
        print('x3xtgen: sealed %s: resident %d bytes (%d KB), tail %d bytes' % (
            a.seal, tail, (tail + 1023) // 1024, end - tail))
        return 0
    if a.fullcheck:
        return fullcheck(True)
    if a.xtgen:
        xtgen(a.xtgen)
        return 0
    if a.selfcheck:
        r = selfcheck(True)
        if r or not a.o:
            return r
    if a.manifest:
        manifest()
        return 0
    if a.o:
        emit(a.o)
        return 0
    ap.print_help()
    return 0


if __name__ == '__main__':
    sys.exit(main())
