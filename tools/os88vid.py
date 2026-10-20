#!/usr/bin/env python3
"""os88vid - the host half of Video Player (SPEC.md 98; docs/plans/VIDEO-PLAN.md).

    python3 tools/os88vid.py import   IN.XDV OUT.V88 [--target cga|herc|lin80]
    python3 tools/os88vid.py encode   FRAME... OUT.V88 --fps F [--wav W.WAV]
                                      [--layout cga|herc|lin80]
    python3 tools/os88vid.py info     FILE.V88...
    python3 tools/os88vid.py decode   FILE.V88 --frame N --png OUT.PNG
    python3 tools/os88vid.py poster   FILE.V88 --key K | --frame F  (in place)
    python3 tools/os88vid.py title    FILE.V88 "A title"              (in place)
    python3 tools/os88vid.py verify   FILE.V88... [--against IN.XDV]
    python3 tools/os88vid.py verify   FILE.XDV...     (the lists vs XDC's code)
    python3 tools/os88vid.py stat     FILE.XDV...
    python3 tools/os88vid.py benchdat OUT.DAT FILE.XDV...   (tests/vidbench)
    python3 tools/os88vid.py --selfcheck

THE .V88 FILE IS SPEC.md 98.1, and this file is its reference: Writer writes
it, Reader reads it the way 98.1.6 says a player must, and verify_v88 holds
a file to every writer's rule as well. `import` re-expresses an XDC stream
(MobyGamer's XDC, MIT, (c) 2014 Jim Leonard) EXACTLY - `verify --against`
proves it frame by frame, audio included - and `--target` re-lays it out
for another surface by simulating it. `encode` is the minimal encoder:
lossless, every changed byte; the budgets are wave 8's.

AN XDC FRAME IS A PROGRAM. The player far-calls the packet: a fixed 14-byte
header (push ds / push cs / pop ds / mov si,<data> / mov ax,B800 / mov es,ax /
mov ch,0 / cld), then per changed span `mov di,imm16` and one of: unrolled
movsw/movsb, `mov cl|cx / rep movsb`, `mov al / rep stosb`, unrolled stosb,
or `es: mov [imm],imm`, then `pop ds / retf`. xdc_ops() executes that grammar
and REFUSES anything outside it, naming the packet and the offset - so a
stream that parses here is a stream whose every write is known. Its audio is
the packet's last `achunk` bytes.

THE CHECKSUM IS SHARED WITH THE GUEST (tests/vidbench/vidbench.asm vb_sum):
over the 16,384-byte canvas, 16-bit words little-endian, s = rol(s + w, 1).
Change one and the other must change with it.
"""
import argparse
import os
import re
import struct
import sys

CANVAS = 16384                  # CGA mode 6: two 8 KB banks, 80 bytes a row
ROWB = 80
HDRLEN = 14
MOVSW, STOSW = 38.4, 27.4       # XDC_CODE.PAS's own constants (CGA waits and
                                # DRAM refresh folded in), for its cost model

# EIGHT lists (VIDEO-PLAN 2.2, revised by wave 0): a change of 1..6 bytes has
# a list of its own, decoded by straight-line stores exactly as XDC unrolls
# them - wave 0 measured a short span through `rep movsw` + `rep movsb` at
# +40-60 cycles against XDC's unrolled stores, and the rep start-up was all
# of it. SLICE is 7 bytes and up; RUN is a run of 7 and up (a shorter run is
# cheaper stored than set up).
L_P1, L_P2, L_P3, L_P4, L_P5, L_P6, L_SLICE, L_RUN, L_SLICEL, L_RUNL = \
    range(10)
NLISTS = 10                     # ...and a span of 256+ bytes has a list of
                                # its own (SLICEL, RUNL, 16-bit length), so
                                # the short ones' loops test for nothing


class XdvError(Exception):
    pass


# --------------------------------------------------------------------------
# reading an XDV
# --------------------------------------------------------------------------
def read_xdv(path):
    """(header dict, [packet bytes]) - XDC_GLOB.PAS's layout: a 512-byte
    header, packets padded to 512, and a one-byte-per-packet sector index
    at the very end."""
    d = open(path, "rb").read()
    if len(d) < 512 or d[:4] != b"XDCV":
        raise XdvError("%s: not an XDC stream (no XDCV signature)" % path)
    npk, largest, ach, rate, vm, cols, rows, feat = struct.unpack_from(
        "<HHHHBBBB", d, 4)
    if npk == 0 or ach == 0 or len(d) < 512 + npk:
        raise XdvError("%s: header says %d packets of %d audio bytes"
                       % (path, npk, ach))
    idx = d[len(d) - npk:]
    off, pk = 512, []
    for i in range(npk):
        n = idx[i] * 512
        if n == 0 or off + n > len(d) - npk:
            raise XdvError("%s: packet %d runs past the index" % (path, i))
        pk.append(d[off:off + n])
        off += n
    hdr = dict(path=path, packets=npk, largest=largest, achunk=ach,
               rate=rate, mode=vm, cols=cols, rows=rows, features=feat,
               fps=rate / ach)
    return hdr, pk


def xdc_ops(p, where=""):
    """The spans one XDC packet writes, in program order:
    [(address, bytes written, is_run)]. Also returns the code length.
    Every opcode outside XDC_CODE.PAS's vocabulary is refused."""
    if len(p) < HDRLEN + 2 or p[0:4] != b"\x1e\x0e\x1f\xbe" \
            or p[6:14] != b"\xb8\x00\xb8\x8e\xc0\xb5\x00\xfc":
        raise XdvError("%s: not XDC's frame header" % where)
    si = struct.unpack_from("<H", p, 4)[0]
    code = si
    pc, cx, al = HDRLEN, 0, 0
    ops, cur = [], None

    def need(k):
        if pc + k > code:
            raise XdvError("%s: instruction at +%d runs past the code"
                           % (where, pc))

    def data(n):
        nonlocal si
        if si + n > len(p):
            raise XdvError("%s: data at +%d runs past the packet"
                           % (where, si))
        b = p[si:si + n]
        si += n
        return b

    while True:
        need(1)
        b = p[pc]
        if b == 0x1F:
            need(2)
            if p[pc + 1] != 0xCB:
                raise XdvError("%s: pop ds at +%d not followed by retf"
                               % (where, pc))
            break
        if b == 0xBF:
            need(3)
            cur = [struct.unpack_from("<H", p, pc + 1)[0], bytearray(), False]
            ops.append(cur)
            pc += 3
            continue
        if cur is None and b not in (0xB1, 0xB9, 0xB0):
            raise XdvError("%s: a write at +%d before any mov di"
                           % (where, pc))
        if b == 0xB1:
            need(2); cx = p[pc + 1]; pc += 2
        elif b == 0xB9:
            need(3); cx = struct.unpack_from("<H", p, pc + 1)[0]; pc += 3
        elif b == 0xB0:
            need(2); al = p[pc + 1]; pc += 2
        elif b == 0xF3:
            need(2)
            o = p[pc + 1]
            pc += 2
            if o == 0xA4:
                cur[1] += data(cx)
            elif o == 0xA5:
                cur[1] += data(2 * cx)
            elif o == 0xAA:
                cur[1] += bytes([al]) * cx; cur[2] = True
            elif o == 0xAB:
                cur[1] += bytes([al]) * (2 * cx); cur[2] = True
            else:
                raise XdvError("%s: rep %02x at +%d" % (where, o, pc - 2))
            cx = 0
        elif b in (0xA4, 0xA5):
            cur[1] += data(1 if b == 0xA4 else 2); pc += 1
        elif b in (0xAA, 0xAB):
            cur[1] += bytes([al]) * (1 if b == 0xAA else 2)
            cur[2] = True
            pc += 1
        elif b == 0x26:                 # es: mov byte|word [imm], imm
            need(2)
            o = p[pc + 1]
            if o == 0xC6 and p[pc + 2] == 0x06:
                need(6)
                a = struct.unpack_from("<H", p, pc + 3)[0]
                ops.append([a, bytearray(p[pc + 5:pc + 6]), False])
                pc += 6
            elif o == 0xC7 and p[pc + 2] == 0x06:
                need(7)
                a = struct.unpack_from("<H", p, pc + 3)[0]
                ops.append([a, bytearray(p[pc + 5:pc + 7]), False])
                pc += 7
            else:
                raise XdvError("%s: es: %02x at +%d" % (where, o, pc))
            cur = None
        else:
            raise XdvError("%s: opcode %02x at +%d is not XDC's" %
                           (where, b, pc))
    out = []
    for a, bs, run in ops:
        if not bs:
            continue
        if a + len(bs) > CANVAS:
            raise XdvError("%s: a write at %04x+%d leaves the canvas"
                           % (where, a, len(bs)))
        out.append((a, bytes(bs), run and len(set(bs)) == 1))
    return out, code


def xdc_cycles(ops):
    """XDC's own cycle model for the program that wrote `ops`
    (XDC_CODE.PAS): header + footer, a mov di each, then the store."""
    c = 18 * 4 + 12 + 34
    for a, bs, run in ops:
        n = len(bs)
        if run:
            c += 12 + 8 + 8 + 4 + n * STOSW / 2
        elif n <= 6:
            c += 12 + (n // 2) * (4 + MOVSW) + (n % 2) * (4 + MOVSW / 2)
        else:
            c += 12 + 8 + 4 + n * MOVSW / 2
    return c


def apply_ops(buf, ops):
    for a, bs, run in ops:
        buf[a:a + len(bs)] = bs


# --------------------------------------------------------------------------
# the operand format (VIDEO-PLAN 2.2)
# --------------------------------------------------------------------------
def screen_row(a):
    """The CGA screen row holding canvas address `a`."""
    return ((a & 0x1FFF) // ROWB) * 2 + (a >> 13)


def band(ops):
    """(first, last) screen row the frame writes; (255, 0) for none."""
    rows = [screen_row(a + i) for a, bs, r in ops for i in (0, len(bs) - 1)]
    rows += [screen_row(a + i) for a, bs, r in ops
             for i in range(0, len(bs), ROWB)]
    return (min(rows), max(rows)) if rows else (255, 0)


def rowend(a):
    """The address one past the end of the CGA row holding `a`."""
    return (a - (a & 0x1FFF) % ROWB) + ROWB


def split_rows(ops):
    """Every span cut at its row's end - the rule that lets one file drive a
    surface of another layout."""
    out = []
    for a, bs, run in ops:
        i = 0
        while i < len(bs):
            k = min(len(bs) - i, rowend(a + i) - (a + i))
            piece = bs[i:i + k]
            out.append((a + i, piece, run))
            i += k
    return out


def classify(a, bs, run):
    n = len(bs)
    if n >= RUN_MIN and (run or len(set(bs)) == 1):
        return L_RUN if n < 256 else L_RUNL
    if n <= 6:
        return L_P1 + n - 1
    return L_SLICE if n < 256 else L_SLICEL


RUN_MIN = 6                     # a stretch of one byte value this long
                                # inside a slice becomes a RUN of its own


def hidden_runs(ops, rmin=RUN_MIN):
    """Every slice cut around the runs of one byte value inside it - XDC's
    FindHiddenRuns, re-applied because the streams do not carry it through:
    a run is 3 bytes on disk however long it is, and `rep stosw` into RAM is
    ~7 cycles a byte where a copy is ~13 (wave 0). Pieces keep their order
    and their addresses; nothing written changes."""
    out = []
    # (the runs found by a regular expression, in C, where a byte at a
    # time in Python was a third of the encoder: a run can only start
    # where the value changes, and one starting inside a shorter group of
    # its value is shorter still, so it finds exactly what that walk found)
    find = _RUNS.get(rmin)
    if find is None:
        find = _RUNS[rmin] = re.compile(
            b"(.)\\1{%d,}" % (rmin - 1), re.S).finditer
    for a, bs, run in ops:
        if run or len(bs) < rmin:
            out.append((a, bs, run))
            continue
        start = 0
        n = len(bs)
        for m in find(bs):
            i, j = m.span()
            if i > start:
                out.append((a + start, bs[start:i], False))
            out.append((a + i, bs[i:j], True))
            start = j
        if start < n:
            out.append((a + start, bs[start:], False))
    return out


_RUNS = {}


ABS_BELOW = 4                   # a cluster of fewer entries than this goes
                                # into an ABSOLUTE segment (wave 0: a skip
                                # segment's set-up is ~228 cycles, an
                                # absolute entry ~11 more than a skip entry,
                                # and below 4 the absolute form is ALSO the
                                # smaller - so it wins both ways there)


def to_lists(ops, abs_below=ABS_BELOW, mapped=False):
    """The eight lists, in the plan's byte layout:
        list    = segment* 00
        segment = count(1..127) address(16) entry*count      skip-coded
                | 80h+count(1..127)          aentry*count     absolute
        entry   = skip, then the change      aentry = address(16), change
        Pn      = n bytes (n = 1..6)
        SLICE   = len8 bytes (7..255)        RUN    = len8 value (6..255)
        SLICEL  = len16 bytes (256+)         RUNL   = len16 value (256+)
        (a fill of a whole screen is one RUNL, as XDC makes it one
        rep stosb)
    NOTHING IS SPLIT AT A ROW: the addresses are the target adapter's own
    memory image (a file is laid out for its surface on the host - wave 0
    measured translating at playback at ~480 cycles a row change), so a
    span that is contiguous in memory is one entry however many rows it
    crosses. A row split is what made the worst frames 290 runs where XDC
    has one `rep stosb`.
    `skip` is from the end of the previous write in the segment (from the
    segment's address for its first entry). Entries one skip byte can reach
    form a CLUSTER; a cluster of `abs_below` or more is a skip segment, and
    the rest are pooled into absolute segments. Only P1..P6 take the
    absolute form; a SLICE or RUN cluster is always a skip segment.
    With `mapped`, SUB2's form (98.1.3.2.4): a u16 map of the lists that
    have entries, then only those.
    Returns (bytes, entries, segments)."""
    lists = [[] for _ in range(NLISTS)]
    for a, bs, run in hidden_runs(ops):
        lists[classify(a, bs, run)].append((a, bs))
    out = bytearray()
    ents = segs = 0

    def change(k, bs):
        if k <= L_P6:
            return bs
        if k == L_SLICE:
            return bytes([len(bs)]) + bs
        if k == L_RUN:                  # len then value: ONE lodsw
            return bytes([len(bs), bs[0]])
        if k == L_SLICEL:
            return struct.pack("<H", len(bs)) + bs
        return struct.pack("<H", len(bs)) + bs[:1]     # RUNL

    lmap = 0
    for k in range(NLISTS):
        L = sorted(lists[k])
        if mapped and not L:
            continue
        lmap |= 1 << k
        clusters, cur, end = [], [], None
        for a, bs in L:
            if cur and (a - end > 255 or len(cur) == 127):
                clusters.append(cur)
                cur = []
            cur.append((a, bs))
            end = a + len(bs)
        if cur:
            clusters.append(cur)
        pool = []
        for c in clusters:
            if k <= L_P6 and len(c) < abs_below:
                pool += c
                continue
            out += bytes([len(c)]) + struct.pack("<H", c[0][0])
            e = c[0][0]
            for a, bs in c:
                out += bytes([a - e]) + change(k, bs)
                e = a + len(bs)
            ents += len(c)
            segs += 1
        for i in range(0, len(pool), 127):
            part = pool[i:i + 127]
            out.append(0x80 | len(part))
            for a, bs in part:
                out += struct.pack("<H", a) + change(k, bs)
            ents += len(part)
            segs += 1
        out.append(0)
    if mapped:
        return struct.pack("<H", lmap) + bytes(out), ents, segs
    return bytes(out), ents, segs


def decode_lists(buf, lists):
    """The reference decoder: what tests/vidbench's vd_native must do."""
    si = 0

    def put(k, di):
        nonlocal si
        if k <= L_P6:
            n = k - L_P1 + 1
            buf[di:di + n] = lists[si:si + n]
            si += n
        else:
            if k in (L_SLICE, L_RUN):
                n = lists[si]
                si += 1
            else:
                n = struct.unpack_from("<H", lists, si)[0]
                si += 2
            if k in (L_SLICE, L_SLICEL):
                buf[di:di + n] = lists[si:si + n]
                si += n
            else:
                buf[di:di + n] = bytes([lists[si]]) * n
                si += 1
        return di + n

    for k in range(NLISTS):
        while True:
            n = lists[si]
            si += 1
            if n == 0:
                break
            if n & 0x80:
                for _ in range(n & 0x7F):
                    di = struct.unpack_from("<H", lists, si)[0]
                    si += 2
                    put(k, di)
                continue
            di = struct.unpack_from("<H", lists, si)[0]
            si += 2
            for _ in range(n):
                di += lists[si]
                si += 1
                di = put(k, di)
    return si


def checksum(buf):
    """vb_sum's: s = rol16(s + word) over the canvas, little-endian words."""
    s = 0
    for i in range(0, len(buf), 2):
        s = (s + buf[i] + (buf[i + 1] << 8)) & 0xFFFF
        s = ((s << 1) | (s >> 15)) & 0xFFFF
    return s


# --------------------------------------------------------------------------
# commands
# --------------------------------------------------------------------------
def frames(path):
    hdr, pk = read_xdv(path)
    for i, p in enumerate(pk):
        ops, code = xdc_ops(p, "%s packet %d" % (os.path.basename(path), i))
        yield hdr, i, p, ops, code


def cmd_stat(a):
    for path in a.files:
        n = ents = segs = lbytes = xbytes = 0
        for hdr, i, p, ops, code in frames(path):
            lists, e, s = to_lists(ops)
            n += 1
            ents += e
            segs += s
            lbytes += len(lists)
            xbytes += code - HDRLEN - 2 + sum(
                0 if r else len(b) for _, b, r in ops)
        fps = hdr["fps"]
        print("%s: mode %d, %d frames, %.3f fps, %d Hz x %d; per frame: "
              "XDC video %.0f B, lists %.0f B (%d entries, %.1f segments)"
              % (os.path.basename(path), hdr["mode"], n, fps, hdr["rate"],
                 hdr["achunk"], xbytes / n, lbytes / n, ents // n, segs / n))


def verify_xdv(path):
    """Decode every frame of an XDC stream both ways - XDC's program and our
    lists - onto one running screen each, and compare after every frame."""
    x = bytearray(CANVAS)
    y = bytearray(CANVAS)
    n = 0
    for hdr, i, p, ops, code in frames(path):
        apply_ops(x, ops)
        lists, e, s = to_lists(ops)
        used = decode_lists(y, lists)
        if used != len(lists) or x != y:
            print("%s: frame %d DIFFERS" % (path, i))
            return 1
        n += 1
    print("%s: %d frames, the lists reproduce XDC's screen exactly"
          % (os.path.basename(path), n))
    return 0


def xdc_emit(ops, pad=True):
    """An XDC packet for `ops`, in XDC_CODE.PAS's own forms: a change of 6
    bytes or fewer is unrolled movsw/movsb (or stosb for a run), a longer
    one `mov cl,n / rep movsb` (`mov al,v / mov cl,n / rep stosb` for a
    run), AL cached across runs of one value as XDC caches it. For the
    bench's SYNTHETIC frames, so each construct is priced against exactly
    what XDC would have emitted for it."""
    code = bytearray(b"\x1e\x0e\x1f\xbe\0\0\xb8\x00\xb8\x8e\xc0\xb5\x00\xfc")
    data = bytearray()
    al = None
    for a, bs, run in ops:
        n = len(bs)
        code += b"\xbf" + struct.pack("<H", a)
        if run:
            if al != bs[0]:
                code += bytes([0xB0, bs[0]])
                al = bs[0]
            if n <= 6:
                code += b"\xaa" * n
            else:
                code += xdc_count(n) + b"\xf3\xaa"
        else:
            if n <= 6:
                code += b"\xa5" * (n // 2) + b"\xa4" * (n % 2)
            else:
                code += xdc_count(n) + b"\xf3\xa4"
            data += bs
    code += b"\x1f\xcb"
    struct.pack_into("<H", code, 4, len(code))
    p = bytes(code + data)
    return p + bytes(-len(p) % 512) if pad else p


def xdc_count(n):
    """XDC's count load: `mov cl` (CH is 0 from the header), `mov cx` past
    255."""
    return bytes([0xB1, n]) if n < 256 else b"\xb9" + struct.pack("<H", n)


def synth_frames():
    """Frames that price ONE construct each: n changes of one kind, dense
    (one skip apart, so a segment holds 255) or sparse (300 bytes apart, so
    every change is a segment of its own), and an empty frame for the fixed
    cost of a frame. All inside bank 0 and never across a row, so the
    translating decoder sees exactly the row changes the layout forces."""
    def row_ok(a, n):
        return (a % ROWB) + n <= ROWB

    def dense(n, k, run=False, step=None):
        ops, a = [], 0
        step = step or (k + 1)
        while len(ops) < n:
            if not row_ok(a, k):
                a += ROWB - a % ROWB
                continue
            v = (len(ops) * 37 + 11) & 0xFF
            bs = bytes([v]) * k if run else bytes((v + j * 13) & 0xFF
                                                 for j in range(k))
            ops.append((a, bs, run))
            a += step
            if a + k > 8000:
                break
        return ops
    return [
        ("S empty", []),
        ("S P1 x400", dense(400, 1)),
        ("S P1 sparse", dense(26, 1, step=300)),
        ("S P2 x400", dense(400, 2)),
        ("S P3 x300", dense(300, 3)),
        ("S P4 x300", dense(300, 4)),
        ("S P6 x200", dense(200, 6)),
        ("S SL16 x150", dense(150, 16)),
        ("S SL40 x60", dense(60, 40)),
        ("S RUN16x150", dense(150, 16, run=True)),
        ("S RUN40 x60", dense(60, 40, run=True)),
        ("S P1 row", [(ROWB * i, bytes([i & 0xFF]), False)
                      for i in range(100)]),
    ]


BENCH_PICK = (("max cycles", lambda r: r["cyc"]),
              ("max entries", lambda r: r["ents"]),
              ("p95 cycles", None),
              ("median cycles", None))


def cmd_synthxdv(a):
    """SYNTHXDV: synth_frames() as an XDC stream (_write_xdv), 8,040 Hz
    and 134 bytes of noise a frame - what `benchdat` reads when the owner's
    XDC samples are not to hand, so a field disk's VIDBENCH times frames
    built from every list kind and nothing else (make vid486)"""
    import random
    rnd = random.Random(486)
    ops = [o for label, o in synth_frames()] * max(1, a.repeat)
    _write_xdv(a.out, ops, 8040, 134, 2, rnd)
    print("os88vid: %s, %d frames" % (a.out, len(ops)))


def cmd_benchdat(a):
    """VIDBENCH.DAT: the frames tests/vidbench/ times, each twice - XDC's
    own packet (paragraph-aligned, so it can be far-called at seg:0) and
    our lists - plus the checksum of the canvas after that frame is applied
    to BLACK, which all three of the guest's check rows must reproduce.

        0    'VBD1'
        4    frame count (word)
        6    0
        8    per frame, 32 bytes:
               +0  label, 12 bytes, NUL-padded
               +12 XDC packet paragraph (from the file's start), +14 length
               +16 lists paragraph, +18 length
               +20 checksum on black
               +22 XDC spans, +24 list entries, +26 bytes written
               +28 XDC's cycle model / 16
               +30 first and +31 last screen row the frame writes (the
                   dirty band a shadow copies; 255, 0 when it writes none)
        then the blobs, each on a paragraph."""
    picked = []
    want = {}
    for f in a.frame or []:
        name, _, idx = f.rpartition(":")
        want.setdefault(name.upper(), set()).add(int(idx))
    extra = {}
    for f in a.extra or []:
        name, _, idx = f.rpartition(":")
        extra.setdefault(name.upper(), set()).add(int(idx))
    for path in a.files:
        rows = []
        base = os.path.splitext(os.path.basename(path))[0][:6].upper()
        if want:
            only = want.get(os.path.basename(path).upper(), set())
            for hdr, i, p, ops, code in frames(path):
                if i in only:
                    lists, e, s = to_lists(ops)
                    picked.append(dict(i=i, p=p, ops=ops, lists=lists,
                                       ents=e, cyc=xdc_cycles(ops),
                                       wb=sum(len(b) for _, b, r in ops),
                                       label=("%s %d" % (base, i))[:12]))
            continue
        for hdr, i, p, ops, code in frames(path):
            lists, e, s = to_lists(ops)
            rows.append(dict(i=i, p=p, ops=ops, lists=lists, ents=e,
                             cyc=xdc_cycles(ops),
                             wb=sum(len(b) for _, b, r in ops)))
        by = sorted(rows, key=lambda r: r["cyc"])
        chosen = []
        for tag, key in BENCH_PICK:
            if tag == "p95 cycles":
                r = by[int(len(by) * 0.95)]
            elif tag == "median cycles":
                r = by[len(by) // 2]
            else:
                r = max(rows, key=key)
            if r["i"] not in [c["i"] for c in chosen]:
                chosen.append(r)
        for i in sorted(extra.get(os.path.basename(path).upper(), ())):
            if i not in [c["i"] for c in chosen]:
                chosen.append(next(r for r in rows if r["i"] == i))
        for r in chosen:
            r["label"] = ("%s %d" % (base, r["i"]))[:12]
            picked.append(r)
    if a.synth:
        for label, ops in synth_frames():
            lists, e, sg = to_lists(ops)
            picked.append(dict(label=label[:12], p=xdc_emit(ops), ops=ops,
                               lists=lists, ents=e, cyc=xdc_cycles(ops),
                               wb=sum(len(b) for _, b, r in ops)))
    if len(picked) > a.max:
        picked = picked[:a.max]
    out = bytearray(b"VBD1" + struct.pack("<HH", len(picked), 0))
    out += bytes(32 * len(picked))

    def para():
        while len(out) % 16:
            out.append(0)
        return len(out) // 16

    for n, r in enumerate(picked):
        black = bytearray(CANVAS)
        apply_ops(black, r["ops"])
        chk = bytearray(CANVAS)
        decode_lists(chk, r["lists"])
        assert chk == black, "lists and XDC disagree on %s" % r["label"]
        xp = para()
        out += r["p"]
        lp = para()
        out += r["lists"]
        d = 8 + 32 * n
        out[d:d + 12] = r["label"].encode().ljust(12, b"\0")
        y0, y1 = band(r["ops"])
        struct.pack_into("<HHHHHHHHHBB", out, d + 12, xp, len(r["p"]), lp,
                         len(r["lists"]), checksum(black), len(r["ops"]),
                         r["ents"], r["wb"], int(r["cyc"] / 16), y0, y1)
        print("  %-12s  XDC %5d B  lists %5d B  spans %4d  entries %4d  "
              "written %5d B  XDC model %6.0f cyc  sum %04x"
              % (r["label"], len(r["p"]), len(r["lists"]), len(r["ops"]),
                 r["ents"], r["wb"], r["cyc"], checksum(black)))
    para()
    if len(out) > a.limit:
        sys.exit("os88vid: %s would be %d bytes, over --limit %d"
                 % (a.out, len(out), a.limit))
    open(a.out, "wb").write(out)
    print("os88vid: %s, %d frames, %d bytes" % (a.out, len(picked), len(out)))


# --------------------------------------------------------------------------
# the .V88 file (SPEC.md 98.1)
# --------------------------------------------------------------------------
class V88Error(Exception):
    pass


V88_SIG = b"V88\x1a"
# THE HEADER'S FLAGS (SPEC.md 98.1.1.2): a reader refuses a bit it does not
# know. LOOPREC: the file carries a SEAM record, the change from its last
# frame back to frame L, and a loop block at 448 names it. REPEAT: a player
# starts with Repeat on. Bits 0 and 3 are named for waves 10 and 9
F_RESIDENT, F_LOOPREC, F_REPEAT, F_LIVE = 1, 2, 4, 8
F_RUNS = 16                     # a LIVE file's frame records carry their blit
                                # RUNS after their lists (98.1.3.4)
F_SPKPWM = 32                   # PCM8 stored as the SPEAKER's PWM counts
                                # (98.1.1.3), made for a machine with no card
F_SPKMUL = 64                   # ...made for PULSES A SAMPLE past one: the
H_SPKP = 24                     # header byte says how many (98.1.1.3.1)
F_AHEAD = 128                   # SOUND AHEAD of the picture (98.1.8): frame
H_AHEAD = 25                    # record r carries frame r + A's sound, A the
H_LEAD0 = 464                   # header's byte 25; u32 at 464: the START's
AHEAD_MAX = 8000                # lead. A x abytes <= this: the player
                                # stages a lead in its 16 KB sound ring's
                                # tail, clear of what it queues first
                                # (video.asm's VP_ALMAX, the same number)
F_BIGSP = 256                   # SUPER-PACKETS PAST 32 KB (98.1.4.1): up to
SP_BIG = 127                    # 127 sectors - a stream's, never flipped
F_KLEADS = 512                  # THE KEYS' LEADS APART (98.1.8.1): with
H_KLEADS = 468                  # AHEAD, key i's lead is A x abytes at the
                                # u32 at 468 + i x that, not its entry's
                                # tail - so a key's read is its picture's
H_XBANK = 472                   # THE BANK (98.3.18.3): u16, the XMS bank
H_XPRE = 474                    # the encode assumed, KB, and u16 the prefill
                                # it banked on before frame 0, KB (FFFFh the
                                # whole bank, 0 the player's 10 s) - NO FLAG:
                                # a player made before reads neither
XPRE_ALL = 0xFFFF
H_LAYER = 476                   # THE LAYER (98.1.9): u32 where its stream
LAYER_FMT = "<IHHHHIHHHH"       # starts, u16 L_CONT, u16 the disk it
                                # was made for (KB a second), u16 the memory
                                # it is banked in (KB), u16 the prefill (KB,
                                # FFFFh all of it), u32 its key table, u16
                                # its longest record, u16 its XMS bank (KB),
                                # u16 x 2 key 0's decode modelled on the
                                # layer's machine and the base's (10 us) -
                                # NO FLAG: a player made before reads none of
                                # it and plays the base
LAYER_LEN = struct.calcsize(LAYER_FMT)
H_YBASE = 500                   # THE BASE'S YARDSTICK (98.3.18.9): u16 key
                                # 0's decode modelled on the machine the file
                                # was made for, 10 us - 0 none. NO FLAG
H_LYORD = 502                   # THE LAYER BEFORE THE BANK (98.1.9.1): 1 -
                                # the layer was made for a reader that fetches
                                # it before refilling the base's bank while
                                # the bank holds H_LYNEED's KB, which is how
                                # the player reads; 0 (every file before it)
                                # the bank first. NO FLAG: an older player
                                # reads the bank first
H_LYNEED = 504                  # ...u16: that KB, whole 32 KB slots - the
                                # bank the layer's machine needs to carry
                                # the base, 0 when its disk does alone
H_LYBKS = 506                   # THE BASE'S DISK WITH NOTHING DECODING
                                # (98.1.9.3, 98.1.9.6): u16 KB/s, written
                                # with a layer - the profile's disk times its
                                # curve at 0%, because the player times its
                                # own on the ring's fill, which nothing
                                # decodes beside. 0: the CPU alone
H_LYIDL = 508                   # ...and the LAYER machine's, the same way.
                                # The test runs only with both
H_BENCHL = 510                  # THE BENCH (98.1.9.13): u16 bench_record()'s
                                # decode modelled on the LAYER's machine, 10
                                # us - with a layer and a bench, else 0
R_BENCH = 62                    # ...and the rendition slot's u16: the same on
                                # the machine the file was made for. 0 none:
                                # the player times key 0 as it always did.
                                # NO FLAG - an older player reads neither
BENCH_SPAN = 192                # every write inside 192 bytes: the least
                                # off-screen gap the player decodes it into
                                # (CGA's after bank 0) - video.asm's VP_BNSPAN
# (list, bytes a change, changes, absolute): the generic mix, tuned so that
# its 286:486 ratio sits within 3% of a VGA8 and a VGA4 file's own frames
# (98.1.9.13's table) - and video.asm's vp_bnmix, byte for byte
BENCH_MIX = ((0, 1, 400, 0), (0, 1, 120, 1), (1, 2, 300, 0), (2, 3, 200, 0),
             (3, 4, 160, 0), (4, 5, 100, 0), (5, 6, 100, 0), (6, 16, 100, 0),
             (6, 40, 40, 0), (7, 16, 120, 0), (7, 64, 40, 0))
BENCH_CMIX = ((6, 16, 60, 0),)  # ...and a SUB2 file's colour sub-record: a
                                # VGA4 play's write mode 3 (98.1.3.2.4), card-
                                # bound and ~18% of one's decode on a 286
L_EMPTY = b"\x02\x00"           # a frame the layer has nothing for
L_CONT = 0x8040                 # THE LAYER'S STREAM IS CONTINUOUS (98.1.9.8):
                                # a record for every frame, end to end from a
                                # 32 KB boundary and read in 32 KB chunks (64
                                # sectors, bit 15 the kind). A layer of 64 is
                                # the padded super-packets before it, which
                                # neither side reads any more: the base plays
L_RECMAX = 32 * 1024 - 8         # a layer record's most bytes (the player's
                                # mirror is sized to the file's longest)
KLEADS_APART = True             # ...what write() does: False writes the
                                # inline kind, every file before it (a test's)
F_SCREEN = 1024                 # A SCREEN OF ITS OWN (98.1.3.2.1): the
                                # rendition's byte 38 is not 0 - so a player
                                # from before refuses it rather than playing
                                # it in 12h at the wrong size and colours
F_PALEV = 2048                  # A PALETTE A SCENE (98.1.3.2.3): a VGA4 file
R_PEV = 56                      # with its own sixteen carries a table, a
                                # u32 at slot +56 (on a sector) and a u16 at
                                # +60 its entries: u32 the frame a scene
                                # starts at, 48 bytes its sixteen - entry 0
                                # frame 0 and the header's palette, frames
                                # ascending. Before decoding that frame, the
                                # player loads them
PEV_LEN = 52
PEV_MAX = 255
F_SUB2 = 4096                   # SUB2 (98.1.3.2.4): a planar file's sub-
                                # records carry a LIST MAP (u16, bit k: list k
                                # present) and only those lists; and a VGA4
                                # one's lead may be 10h..1Fh, WRITE MODE 3 -
                                # one colour, its lists' bytes pixel masks
SUB2_WM3 = 0x10                 # ...the lead of colour c is SUB2_WM3 + c
WM3_BYTE = 47                   # a write mode 3 byte's cycles past a plain
                                # store's, wave 0's model (MartyPC's XT, an
                                # AND-store loop against rep movsb: VM3BENCH)
WM3_REC = 120                   # ...and a record's six OUTs that set the
                                # mode and put mode 12h's back
F_PAGE = 8192                   # PAGE-AHEAD (98.1.3.7): the second page is
                                # where a frame the budgets would cut is BUILT
                                # ahead, out of what earlier frames left, and
                                # shown with one flip; rendition byte 37 is 3
REC_FLIP = 0x8000               # ...a record's y0: drawn on the HIDDEN page,
                                # which is then shown
REC_HID = 0x8000                # ...a record's y1: a hidden-page record
                                # follows its lists, inside its len
FLIP_PAGE = 3                   # (rendition byte 37: 2 flipped, 3 page-ahead)
FLIP_MPAGE = 4                  # PAGE-AHEAD ON N PAGES (98.1.3.7.1): byte 37
R_PAGES = 39                    # ...and byte 39 N, the pages it uses, 3..8
MPAGE_MAX = 8
PG_SHIFT = 12                   # ...a record's y0 bits 12-14 a PAGE, its rows
PG_BITS = 0x7000                # bits 0-11
ROW_MASK = 0x0FFF
HID_MORE = 0x8000               # ...a hidden record's y1: another follows
FLIP_FPAGE = 5                  # FLIPPED AND BUILT AHEAD (98.1.3.7.2): byte
                                # 37 5, every frame record flips, on byte
                                # 39's N pages
FLIP_HELD = 6                   # FLIPPED WITH HELD RECORDS (98.1.3.9): byte 37
                                # 2's every frame flipped, but a frame record
                                # with REC_HELD is drawn on the back page and
                                # NOT shown - the next is drawn on it too
REC_HELD = 0x8000               # ...a frame record's y0 bit 15 there
REC_BUILT = 0x1000              # ...a frame record's y1: its page was BUILT
                                # ahead, drawn on as it stands, not brought up
COPY_LEN = 6                    # ...a COPY: a hidden record with y0 bit 15,
                                # its header alone, y1 bits 12-14 the source
F_KPARTS = 16384                # KEYS IN PARTS (98.1.3.8): a key past one
                                # read is several key records back to back,
                                # each a band of the screen and one read
KEY_PART = 32768                # ...a read, at most: two clusters of any
                                # volume fill the player's one 64 KB claim
KEY_MORE = 0x8000               # ...a key part's y0: another part follows,
                                # and this one's last two bytes, inside its
                                # len, are the next read's
F_KPOOL = 32768                 # THE PAGE POOL (98.1.3.8.1): a page picture
                                # several keys carry is stored once, after
                                # the key records, and each key names it
REF_LEN = 12                    # ...a key's reference: len y0 y1 at(32) n
F_KNOWN = F_RESIDENT | F_LOOPREC | F_REPEAT | F_LIVE | F_RUNS | F_SPKPWM \
    | F_SPKMUL | F_AHEAD | F_BIGSP | F_KLEADS | F_PALEV | F_SUB2 | F_PAGE \
    | F_KPARTS | F_SCREEN | F_KPOOL

# THE OPTIONS A FILE WAS MADE WITH (98.1.1.4): the header's bytes 26-31
# point at a block the ENCODER wrote - every option it used, deflated - so
# the encoder's window can load a .V88 and show how it was made. No player
# reads it: it sits before the stream in a streamed file and after the
# blocks in a resident one, where nothing the player reads by offset or
# by sequence reaches. Byte 25 is A, with AHEAD (98.1.8)
H_OPTS = 26                     # u32: the block's offset, 0 for none
H_OPTSN = 30                    # u16: its length
OPTS_MAGIC = b"V88O"
OPTS_UNPACKED_MAX = 65536       # a block inflating past this is refused
# ...deflated with a PRESET DICTIONARY, which is what makes storing EVERY
# option cost ~160 bytes rather than ~590: a record is mostly the names of
# the options and their values, and the dictionary is exactly that - the
# encoder's version 1 options at their defaults. IT IS FROZEN: a byte of
# it changed and every file made with it is unreadable (zlib names the
# dictionary's checksum, so that is refused, not misread). A different
# dictionary is a new container version beside this one, never an edit;
# tests/vencguitest.py pins its SHA-256
OPTS_ZDICT = {1: (
    b'{"o":{"adpcm":"search","aim":"asked","audio":null,"avg":null,"box":n'
    b'ull,"brightness":0.0,"c512_dither":6.0,"c512_mix":0,"c512_stable":3.'
    b'0,"cga_bg":null,"cga_bright":null,"cga_card":"both","cga_palette":nu'
    b'll,"clip":16.0,"comp_dither":"diffuse","comp_quick":false,"comp_stab'
    b'le":50000.0,"contrast":1.0,"credits":null,"detail":null,"disk":null,'
    b'"dither":"bayer","end":null,"error":"visible","fit":"fit","flip":fal'
    b'se,"fps":null,"gamma":1.0,"invert":false,"jobs":null,"keysecs":2.0,"'
    b'layout":null,"levels":"auto","levels_mix":4,"live":null,"lookahead":'
    b'2,"loop_from":null,"mix":0.5,"owe":null,"peak":null,"pixfmt":null,"p'
    b'oster":null,"poster_at":null,"preset":null,"profile":"5150-st225","r'
    b'ate":null,"repeat":false,"reserve":null,"resident":false,"spk_drive"'
    b':0.5,"spk_highpass":null,"spk_idle":0.02,"spk_lows":0.5,"spk_preview'
    b'":null,"spk_pulses":1,"spk_range":null,"spk_ratio":null,"spk_shape":'
    b'"on","spk_style":"natural","stable":6.0,"start":0.0,"text_busy":null'
    b',"text_colour":null,"text_detail":0.5,"text_glyphs":"blocks","text_o'
    b'cr":false,"text_ocr_conf":80.0,"text_ocr_every":5,"text_ocr_large":f'
    b'alse,"text_prefer_colour":1.0,"text_sharpen":0.6,"text_stable":6.0,"'
    b'title":null,"vga4_stable":24.0,"vga8_dither":24.0,"vga8_stable":18.0'
    b',"volume":null,"worth":null,"xms":false},"src":"","v":1}'
)}


def pack_options(doc):
    """A record (a dict the encoder built, 98.2.17) -> the block: magic,
    the container version, and the canonical JSON deflated with that
    version's dictionary"""
    import json
    import zlib
    raw = json.dumps(doc, separators=(",", ":"), sort_keys=True,
                     ensure_ascii=True).encode("ascii")
    if len(raw) > OPTS_UNPACKED_MAX:
        raise V88Error("an options record of %d bytes: %d at most"
                       % (len(raw), OPTS_UNPACKED_MAX))
    c = zlib.compressobj(9, zlib.DEFLATED, 15, 9, zlib.Z_DEFAULT_STRATEGY,
                         OPTS_ZDICT[1])
    blob = OPTS_MAGIC + bytes([1]) + c.compress(raw) + c.flush()
    if len(blob) > 0xFFFF:
        raise V88Error("an options block of %d bytes" % len(blob))
    return blob


def unpack_options(blob):
    """The block -> the record, or V88Error: a wrong magic, an unknown
    container, a checksum or dictionary zlib refuses, a record inflating
    past OPTS_UNPACKED_MAX or one that is not a JSON object"""
    import json
    import zlib
    if blob[:4] != OPTS_MAGIC or len(blob) < 5:
        raise V88Error("the options block has no signature")
    if blob[4] not in OPTS_ZDICT:
        raise V88Error("options container %d: this reader knows %s"
                       % (blob[4], sorted(OPTS_ZDICT)))
    try:
        d = zlib.decompressobj(15, zdict=OPTS_ZDICT[blob[4]])
        raw = d.decompress(blob[5:], OPTS_UNPACKED_MAX)
        if d.unconsumed_tail or not d.eof:
            raise V88Error("the options block is cut short or too large")
        doc = json.loads(raw.decode("ascii"))
    except (zlib.error, ValueError, UnicodeDecodeError) as e:
        raise V88Error("the options block does not read: %s" % e)
    if not isinstance(doc, dict):
        raise V88Error("the options record is not an object")
    return doc

PIT_HZ = 1193182


def spk_table(rate, pulses=1, fast=True):
    """SPEC.md 34.11.2's count table for a rate: t[s] = 1 + s(N-2)/255, N =
    1,193,182 / rate / pulses - a PULSE's period, 34.11.7 - apps/os88spk.inc's
    os88spk_init, to the byte. A pulse is 74..255 counts on an 8088 and
    48..255 on a 286 or better (34.11.8): `fast` says a 286 may play it,
    and a FILE is valid down to 48 - which machine plays it is the
    player's to decide"""
    n = PIT_HZ // rate
    if not 1 <= pulses <= 4 or n % pulses:
        raise V88Error("%d pulses a sample at %d Hz: N = %d, which they do "
                       "not divide" % (pulses, rate, n))
    n //= pulses
    if not (48 if fast else 74) <= n <= 255:
        raise V88Error("%d Hz%s is not a rate the speaker plays (a pulse of "
                       "%d counts, %d..255)" % (
                           rate, " x %d pulses" % pulses if pulses > 1
                           else "", n, 48 if fast else 74) + ("" if fast else
                                          "; 48.. on a 286 or better"))
    return bytes(1 + s * (n - 2) // 255 for s in range(256))


def spk_counts(samples, rate, pulses=1):
    """PCM8 samples as the speaker's counts (98.1.1.3)"""
    return bytes(samples).translate(spk_table(rate, pulses))


# A SPEAKER WAV FOR AUDIO (SPEC.md 86.21.1): an ordinary 8-bit mono WAV
# with one more chunk, 'o8sp' = <u8 kind><u8 pulses><u16 N>. Kind 1 is PCM8
# already shaped for the speaker (Audio skips the pre-emphasis); kind 2 is
# the speaker's COUNTS themselves, which Audio copies straight into the ring
# on the speaker and turns back into samples on a card. Any other player
# skips the chunk as RIFF says it may.
SPK_WAV_CHUNK = b"o8sp"
SPK_WAV_SHAPED, SPK_WAV_COUNTS = 1, 2


def write_spk_wav(path, rate, data, kind=SPK_WAV_COUNTS, pulses=1):
    """`data` as a speaker WAV of `kind` at `rate` (0 writes a plain WAV)"""
    extra = b""
    if kind:
        extra = SPK_WAV_CHUNK + struct.pack("<IBBH", 4, kind, pulses,
                                            PIT_HZ // rate // pulses)
    body = (b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate, 1, 8)
            + extra + b"data" + struct.pack("<I", len(data)) + bytes(data)
            + (b"\0" if len(data) & 1 else b""))
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", len(body)) + body)


def spk_samples(counts, rate, pulses=1):
    """spk_counts undone: each count back to the lowest sample that makes
    it (the table is monotonic, and several samples share a count)"""
    inv = [None] * 256
    for s, c in enumerate(spk_table(rate, pulses)):
        if inv[c] is None:
            inv[c] = s
    return bytes(inv[c] if inv[c] is not None else 128 for c in counts)


# THE TWO STYLES (98.2.15.1), both the owner's picks off the 5150: LIFTED
# levels harder and cuts lower, so a quiet passage is heard ("K"); NATURAL
# keeps more of the song's own rise and fall ("W"), and is the default
# (the owner, 2026-09-28). A style fills in whatever of hp, ratio and range
# was not given, and the three defaults below are the default style's
SPK_STYLES = {"lifted": dict(hp=200, ratio=3.0, rng=30.0),
              "natural": dict(hp=250, ratio=2.0, rng=24.0)}
SPK_STYLE = "natural"
SPK_HP = SPK_STYLES[SPK_STYLE]["hp"]        # the high-pass, Hz (98.2.15.1)
SPK_DRIVE = 0.5                 # ...and its level, an RMS of full scale
SPK_LOWS = 0.5                  # ...the band under SPK_SPLIT, against it
SPK_RANGE = SPK_STYLES[SPK_STYLE]["rng"]    # the most a quiet passage rises
SPK_RATIO = SPK_STYLES[SPK_STYLE]["ratio"]  # ...the leveller's ratio
SPK_IDLE = 0.02                 # ...and the carrier's slide in the quiet, s
SPK_SPLIT = 700                 # ...and where --spk-lows starts, Hz


def spk_shape(pcm, rate, hp=SPK_HP, drive=SPK_DRIVE, clip="soft",
              lows=SPK_LOWS, rng=SPK_RANGE, ratio=SPK_RATIO, idle=SPK_IDLE):
    """SOUND SHAPED FOR THE SPEAKER (98.2.15.1): unsigned 8-bit PCM in and
    out, the same length. A pulse's width is the only thing the speaker
    has, and it spends it on whatever is loudest - in most music the bass
    a 2 1/4-inch cone cannot move, which leaves what it CAN play 25-30 dB
    under the carrier. So: nothing under `hp` Hz and the top tilted up
    (+9 dB from 400 Hz to 2.4 kHz, where the cone and the ear are both
    at their best), the level evened out over ~30 ms (quiet passages
    raised - `ratio`:1, at most `rng` dB - silence left silent), then
    driven to an RMS of
    `drive` of full scale through a soft clip - loudness is what a pulse
    width buys, and ~5% of samples rounded off is the cheap end of it"""
    import numpy as np
    return spk_shape_f(np.frombuffer(bytes(pcm), dtype=np.uint8)
                       .astype(np.float64) - 128.0, rate, hp, drive,
                       bytes(pcm), clip, lows, rng=rng, ratio=ratio,
                       idle=idle)


def spk_limit(y, rate, ceil=0.98, look=0.002):
    """A PEAK LIMITER in place of a clip (98.2.15.1): the gain dips around a
    peak - a centred max over `look` seconds either side, held and ramped
    over the same again - so the waveform is scaled rather than bent, and a
    loud low tone is not given the harmonics a clip adds. What little still
    passes `ceil` is clipped"""
    import numpy as np
    L = max(1, int(round(rate * look)))
    sw = np.lib.stride_tricks.sliding_window_view
    pk = sw(np.pad(np.abs(y), (L, L), mode="edge"), 2 * L + 1).max(axis=1)
    g = np.minimum(1.0, ceil / np.maximum(pk, 1e-12))
    g = sw(np.pad(g, (L, L), mode="edge"), 2 * L + 1).min(axis=1)
    k = np.ones(2 * L + 1) / (2 * L + 1)
    g = np.convolve(np.pad(g, (L, L), mode="edge"), k, mode="valid")
    return np.clip(y * g, -1.0, 1.0)


def spk_shape_f(x, rate, hp=SPK_HP, drive=SPK_DRIVE, raw=None, clip="soft",
                lows=SPK_LOWS, split=SPK_SPLIT, rng=SPK_RANGE,
                ratio=SPK_RATIO, idle=SPK_IDLE):
    """spk_shape's body, from samples at any scale (the encoder hands it
    ffmpeg's floats, so the quiet passages it raises are not raised out of
    8-bit steps). Out: unsigned 8-bit PCM; `raw` is what an input too
    short or all silence comes back as"""
    import numpy as np
    x = np.asarray(x, dtype=np.float64)
    n = len(x)
    if raw is None:
        raw = bytes([128]) * n
    if n < 16:
        return raw
    if hp:                          # a brick wall with a one-octave ramp,
        X = np.fft.rfft(x)          # squared so it is smooth at both ends
        f = np.fft.rfftfreq(n, 1.0 / rate)
        X *= np.clip((f - hp / 2.0) / (hp / 2.0), 0.0, 1.0) ** 2
        tilt = np.clip(np.log2(np.maximum(f, 1.0) / 400.0)
                       / np.log2(6.0), 0.0, 1.0)
        X *= 10.0 ** (9.0 * tilt / 20.0)
        x = np.fft.irfft(X, n)

    def env_of(x):
        w = max(1, int(rate * 0.03))    # the level: a centred 30 ms RMS...
        c = np.concatenate(([0.0], np.cumsum(x * x)))
        lo = np.clip(np.arange(n) - w // 2, 0, n)
        hi = np.clip(np.arange(n) + w // 2 + 1, 0, n)
        env = np.sqrt((c[hi] - c[lo]) / (hi - lo))
        # ...held over the window either side, so a transient is not pumped
        k = np.lib.stride_tricks.sliding_window_view(
            np.pad(env, (w, w), mode="edge"), 2 * w + 1)
        return k.max(axis=1)

    def level(x, top=None):
        env = env_of(x)
        if top is None:
            top = np.percentile(env, 99.5)
        if top <= 0:
            return x * 0.0
        floor = top / 10 ** (rng / 20.0)    # below it, gain stops growing
        gain = (top / np.maximum(env, floor)) ** (1.0 - 1.0 / ratio)
        gate = np.clip(env / (top / 250.0), 0.0, 1.0)   # silence (-48 dB) stays
        return x * gain * gate
    if lows != 1.0 and hp:
        # TWO BANDS (98.2.15.1): under `split` and over it levelled apart,
        # brought to one level, and the lows scaled - the voice keeps its
        # drive while the lower tones, which are what the clip bends first,
        # are given less of it
        m = np.clip((f - split * 0.8) / (split * 0.4), 0.0, 1.0)
        top = np.percentile(env_of(x), 99.5)    # ONE reference for both,
        lo, hi = level(np.fft.irfft(X * (1 - m), n), top), \
            level(np.fft.irfft(X * m, n), top)  # so a band with nothing
        y = hi + lows * lo                      # in it stays that way
    else:
        y = level(x)
    if not np.any(y):
        return raw
    rms = np.sqrt(np.mean(y * y))
    if rms <= 0:
        return raw
    if clip == "limit":
        y = spk_limit(y * (drive / rms), rate)
    else:
        y = np.tanh(y * (drive / rms)) / np.tanh(1.0)
    if idle:
        # THE CARRIER PUT AWAY IN THE QUIET (98.2.15.3): a pulse's width
        # rests where the sound is centred, and at 50% the carrier is at its
        # LOUDEST - so the centre slides toward the short end as the sound
        # falls, by the headroom it leaves: e = a moving max of |y| over
        # +-2W, averaged over +-W, is >= |y| at every sample, so y + e - 1
        # never passes -1, and silence rests at a count of 1
        w = max(1, int(rate * idle))
        sw = np.lib.stride_tricks.sliding_window_view
        e = sw(np.pad(np.abs(y), (2 * w, 2 * w), mode="edge"),
               4 * w + 1).max(axis=1)
        e = np.convolve(np.pad(e, (w, w), mode="edge"),
                        np.ones(2 * w + 1) / (2 * w + 1), mode="valid")
        y = np.clip(y + np.minimum(e, 1.0) - 1.0, -1.0, 1.0)
    y = np.clip(np.round(y * 127.0), -127, 127) + 128
    return y.astype(np.uint8).tobytes()
RUNS_MAX = 32                   # ...at most this many a record
# What a Live pass's blit costs (98.3.10.2), fitted by timing vp_blitb on
# MartyPC's CGA and Hercules 5150s over Live plays: a call's fixed part,
# and a byte of the band. A run is merged into the one above it when one
# call of the union costs less than two
BLIT_CALL, BLIT_ROW, BLIT_BYTE = 9000, 100, 21
# LIVE (98.3.10): a RESIDENT file that may play on the live desktop - its
# renditions LIN80 one-bit canvases, blitted from a RAM shadow, each naming
# the SCREEN it was drawn for at slot byte 53 (1 CGA, 2 Hercules, 3 VGA/EGA)
R_TARGET = 53
TARGETS = {"cga": 1, "herc": 2, "vga": 3}
# RESIDENT (98.1.7): each rendition's records are one BLOCK, read whole and
# expanded before the play - back to back, no chain, no audio in them - and
# the sound one AUDIO block for every rendition. A block's fields sit in its
# rendition slot's spare bytes, the audio block's at 176
R_BLOCK = 40                    # slot: the block's offset, packed bytes,
PK_NONE, PK_LZ4, PK_LZB = 0, 1, 2   # unpacked bytes (<i4) and packing at 52
# 98.1.7.1: a PACKED resident block reads in one call of under 60 KB and
# expands into under 128 KB (OSAPI_DECOMP's input is one segment); a STORED
# one is read in pieces and may be any size under 1 MB - vp_pbk's bounds
BLK_PACKED_MAX = 61440
BLK_UNPACKED_MAX = 0x1FFF0
BLK_STORED_MAX = 0xFFFFF
AUD_AT = 176                    # the audio block: the same four fields
LOOP_AT = 448                   # the loop block: L, seam offset and length,
LOOP_FMT = "<IIHBBI"            # the super-packet of frame L+1 (sectors,
                                # records before it there, offset)
SECTOR = 512
SP_MAX = 64                     # sectors a super-packet may take (32 KB)
AUD_NONE, AUD_PCM8, AUD_ADPCM4 = 0, 1, 2
AUD_BY_NAME = {"pcm8": AUD_PCM8, "adpcm4": AUD_ADPCM4}
ADPCM4_REF = 0x80               # the stream's reference byte (SPEC.md 98.1.1)
PF_MONO1, PF_CGACOMP, PF_VGA8, PF_VGA4 = 1, 2, 3, 4
PF_CGA4, PF_C160 = 5, 6         # CGA in COLOUR (98.1.3.3): 320 x 200 x 4 on
                                # mode 4, and 160 x 100 x 16 on the text hack
PF_C512 = 7                     # ...and 80 x 100 x 512 on its COMPOSITE
                                # output, the cells as the screen has them
                                # (98.1.3.5)
PF_TEXT = 8                     # ...and TEXT: the 80 x 25 text screen of
                                # any adapter, its characters the picture
                                # (98.1.3.6)
PF_NAMES = {PF_MONO1: "MONO1", PF_CGACOMP: "CGACOMP", PF_VGA8: "VGA8",
            PF_VGA4: "VGA4", PF_CGA4: "CGA4", PF_C160: "C160",
            PF_C512: "C512", PF_TEXT: "TEXT"}
# VGA4 (98.1.3.2): mode 12h's own sixteen, which no theme changes - the
# DAC's six bits, black to white in the EGA's order
STD16 = bytes((0, 0, 0, 0, 0, 42, 0, 42, 0, 0, 42, 42, 42, 0, 0, 42, 0, 42,
               42, 21, 0, 42, 42, 42, 21, 21, 21, 21, 21, 63, 21, 63, 21,
               21, 63, 63, 63, 21, 21, 63, 21, 63, 63, 63, 21, 63, 63, 63))
LAY_CGA, LAY_HERC, LAY_LIN80, LAY_LIN320, LAY_MODEX = 1, 2, 3, 4, 5
LAY_C160 = 6
LAY_TXT = 7
LAY_TEXT = 8
LAYOUTS = {                     # SPEC.md 98.1.2: banks, stride, rows, name
    LAY_CGA: (2, 80, 200, "cga"),
    LAY_HERC: (4, 90, 348, "herc"),
    LAY_LIN80: (1, 80, 480, "lin80"),
    LAY_LIN320: (1, 320, 200, "lin320"),
    LAY_MODEX: (1, 80, 240, "modex"),   # a PLANE's image: 80 bytes a row
    LAY_C160: (1, 80, 100, "c160"),     # packed nibbles, the LEFT high: the
                                        # text hack's attributes (98.1.3.3)
    LAY_TXT: (1, 160, 100, "text-80x100"),      # ...and the text screen AS IT IS: a
                                        # cell's character, its attribute
    LAY_TEXT: (1, 160, 25, "text-80x25"),     # ...and not retimed: 80 x 25, the
}                                       # screen every adapter has (98.1.3.6)
LAYOUT_BY_NAME = {v[3]: k for k, v in LAYOUTS.items()}
# ...and the names the two text layouts had before they said which text
# screen they are (98.1.2): still taken, never offered
LAYOUT_ALIASES = {"txt": "text-80x100", "text": "text-80x25"}


# VGA4'S SCREENS (98.1.3.2.1): rendition byte 38 names the mode a VGA4
# file plays in. 0 is mode 12h's 640 x 480, the desktop's own - the only
# one before, and the only one a window can host. The rest are full screen
# only, and each puts its rows 80 plane bytes apart whatever its width, so
# every one of them is LIN80's layout and its two pages fit a plane's 64 KB:
# name, pixels across, rows, pixel aspect
SCR_480, SCR_320X200, SCR_320X240, SCR_640X350, SCR_640X400 = 0, 1, 2, 3, 4
SCR_320X200N, SCR_320X240N = 5, 6
SCREENS = {
    SCR_480: ("640x480", 640, 480, (1, 1)),
    SCR_320X200: ("320x200", 320, 200, (5, 6)),     # mode 0Dh
    SCR_320X240: ("320x240", 320, 240, (1, 1)),     # 0Dh on 480 lines
    SCR_640X350: ("640x350", 640, 350, (35, 48)),   # 12h on 350 (mode 10h's)
    SCR_640X400: ("640x400", 640, 400, (5, 6)),     # 12h on 400 lines
    SCR_320X200N: ("320x200n", 320, 200, (5, 6)),   # 1 and 2 NARROW: rows
    SCR_320X240N: ("320x240n", 320, 240, (1, 1)),   # 40 bytes apart, eight
}                                                   # and six pages (98.1.3.2.5)
NARROW = (SCR_320X200N, SCR_320X240N)


def screen_stride(screen):
    """A VGA4 screen's row in plane bytes: LIN80's 80, or a NARROW screen's
    40 (98.1.3.2.5)"""
    return 40 if screen in NARROW else 80
SCREEN_BY_NAME = {v[0]: k for k, v in SCREENS.items()}
R_SCREEN = 38
# a VGA4 file's own palette (98.1.3.2.1): the first 16 of the 256 entries,
# indexed by the PIXEL'S VALUE, the four planes' bits. N colours take the
# values PLANE_CODES[N] - each colour's bits written to a GROUP of planes,
# so planes of one group always agree and 98.1.3.2's rule stores them as
# one: two colours are one store a byte under 0Fh, as one bit is, and four
# are two (03h and 0Ch) - the data is the bits the colours need
PLANE_CODES = {
    2: (0, 15),
    4: (0, 3, 12, 15),
    8: tuple((k & 1) * 9 | (k & 2) | (k & 4) for k in range(8)),
    16: tuple(range(16)),
}


def plane_code(n):
    """The pixel values N colours take (PLANE_CODES), N up to 16"""
    for m in sorted(PLANE_CODES):
        if n <= m:
            return PLANE_CODES[m][:n]
    raise V88Error("%d colours: a VGA4 file holds 16 at most" % n)


def layout_name(n):
    """A layout's name as the command lines take it: its own, or an old
    one's, made its own - argparse's `type`, ahead of its `choices`"""
    return LAYOUT_ALIASES.get(n, n)
ASPECT = {LAY_CGA: (5, 12), LAY_HERC: (29, 45), LAY_LIN80: (1, 1),
          LAY_LIN320: (5, 6), LAY_MODEX: (1, 1), LAY_C160: (5, 6),
          LAY_TXT: (5, 3),              # TXT's is a CELL's: 8 x 2 of 640 x 200
          LAY_TEXT: (5, 12)}            # ...and TEXT's too: 8 x 8 of it
# a byte is a PIXEL on a VGA8 layout and eight of them on the others - and
# on MODEX a byte of each of four planes, so a plane row's byte is 4 pixels
PIX_PER_BYTE = {LAY_CGA: 8, LAY_HERC: 8, LAY_LIN80: 8, LAY_LIN320: 1,
                LAY_MODEX: 4, LAY_C160: 2, LAY_TXT: 1,   # (TXT: a byte a byte;
                LAY_TEXT: 1}
                                                  # a CELL is two of them)
CGA4_ASPECT = (5, 6)            # mode 4's pixel: 320 x 200 on a 4:3 tube
# CGA4's PALETTE (98.1.3.3), slot byte 54: bits 0-3 the background, any of
# the sixteen; bit 4 the intensity of the other three; bit 5 the palette
# (0 green, red, brown; 1 cyan, magenta, white); bit 6 mode 5's third
# (cyan, red, white), bit 5 then 0. Bit 7 is refused
R_CGAPAL = 54
# ...and C512's CARD (98.1.3.5), the same byte: the composite output it was
# made for - 0 IBM's old CGA, 1 the new (1985) one, 2 chosen for both
CARD_OLD, CARD_NEW, CARD_BOTH = 0, 1, 2
CARD_BY_NAME = {"old": CARD_OLD, "new": CARD_NEW, "both": CARD_BOTH}
CARD_NAMES = {v: k for k, v in CARD_BY_NAME.items()}
CGA4_SETS = {0: (2, 4, 6), 1: (3, 5, 7), 2: (3, 4, 7)}
# ...and TEXT's COLOUR (98.1.3.6), the same byte: 0 MONO - the three
# attributes an MDA draws as a colour card does, 07h, 0Fh and 70h, so it
# plays on every adapter - or 1 COLOUR, sixteen foregrounds on sixteen
# backgrounds, blink off: a CGA, an EGA or a VGA
TEXT_MONO, TEXT_COLOUR = 0, 1
TEXT_MONO_ATTRS = (0x07, 0x0F, 0x70)


def cga4_colours(sel):
    """The four colours, as indexes of the sixteen, that palette byte
    `sel` puts on pixel values 0..3"""
    if sel & 0x80 or (sel & 0x40 and sel & 0x20):
        raise V88Error("a CGA4 palette byte of %02Xh" % sel)
    p = 2 if sel & 0x40 else (sel >> 5) & 1
    return [sel & 15] + [c + (8 if sel & 16 else 0) for c in CGA4_SETS[p]]


def c16_lum16():
    """The sixteen CGA colours' lumas, 0..16, as vga8_lum16 makes them"""
    return vga8_lum16(STD16 + bytes(768 - 48))[:16]


def cga4_mono(cv, wb, h, sel):
    """A CGA4 canvas (wb bytes a row, four pixels a byte, the leftmost in
    bits 7-6) as the Preview's one-bit picture: wb / 2 bytes a row, a pixel
    lit when its colour's luma beats the 4 x 4 Bayer cell - the player's
    vp_c4mono (98.4.6)"""
    lum = c16_lum16()
    lv = [lum[c] for c in cga4_colours(sel)]
    out = bytearray(wb // 2 * h)
    for y in range(h):
        by = BAYER4[(y & 3) * 4:(y & 3) * 4 + 4]
        for x in range(wb * 4):
            v = (cv[y * wb + x // 4] >> (6 - 2 * (x & 3))) & 3
            if lv[v] > by[x & 3]:
                out[y * (wb // 2) + x // 8] |= 0x80 >> (x & 7)
    return bytes(out)


def c512_mono(cv, wb, h):
    """A C512 canvas (wb bytes a row, character then attribute) as the
    Preview's one-bit picture: its ATTRIBUTES' C160 poster (98.4.6) - a
    cell's two nibbles are the colours its pattern mixes"""
    return c160_mono(b"".join(bytes(cv[y * wb + 1:(y + 1) * wb:2])
                              for y in range(h)), wb // 2, h)


def text_mono(cv, wb, h, font=None):
    """A TEXT canvas (wb bytes a row, character then attribute) as the
    Preview's one-bit picture (98.4.6): every cell FOUR pixels wide and
    four rows tall - wb / 4 bytes a row, h x 4 rows, the half size C160's
    poster is - each quarter of it lit by its quadrant's share of the
    glyph between the attribute's two colours, against the 4 x 4 Bayer
    cell. The player's vp_tmono. `font` is {code: 8 rows} for 32..126, the
    machine's (OSAPI_FONT_GLYPHS); the model's when None"""
    import os88txtfont
    lum = c16_lum16()
    cells = wb // 2
    ob = wb // 4
    out = bytearray(ob * h * 4)
    q = {}
    for y in range(h):
        for x in range(cells):
            ch, at = cv[y * wb + 2 * x], cv[y * wb + 2 * x + 1]
            if ch not in q:
                q[ch] = os88txtfont.quads(ch, font)
            qs = q[ch]
            fg, bg = lum[at & 15], lum[at >> 4]
            for sub in range(4):
                by = BAYER4[sub * 4:sub * 4 + 4]
                row = (y * 4 + sub) * ob
                for sx in range(4):
                    n = qs[(sub >> 1) * 2 + (sx >> 1)]
                    if (bg * (16 - n) + fg * n) >> 4 > by[sx]:
                        px = x * 4 + sx
                        out[row + px // 8] |= 0x80 >> (px & 7)
    return bytes(out)


def c160_mono(cv, wb, h):
    """A C160 canvas (wb bytes a row, two pixels a byte) as the Preview's
    one-bit picture, each pixel TWO wide - wb / 2 bytes a row, so the
    picture keeps its shape on a 640 x 200 desktop - the player's
    vp_c16mono (98.4.6)"""
    lum = c16_lum16()
    out = bytearray(wb // 2 * h)
    for y in range(h):
        by = BAYER4[(y & 3) * 4:(y & 3) * 4 + 4]
        for x in range(wb * 4):
            b = cv[y * wb + x // 4]
            v = b >> 4 if not x & 2 else b & 15
            if lum[v] > by[x & 3]:
                out[y * (wb // 2) + x // 8] |= 0x80 >> (x & 7)
    return bytes(out)
VGA8_LAYOUTS = (LAY_LIN320, LAY_MODEX)
PLANE = 65536                   # a planar surface: plane p at p x 64 KB
CYC_SUB = 40                    # a MODEX sub-record's Map Mask OUT
PAL_BYTES = 768                 # VGA8's palette: 256 x (r, g, b), 0..63
BAYER4 = (0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5)


def vga8_lum16(pal):
    """The player's luma of each palette entry, 0..16 (SPEC.md 98.4):
    (77 r + 150 g + 29 b) >> 8 on the DAC's six bits, then x 17 + 32 >> 6"""
    out = []
    for i in range(256):
        r, g, b = pal[3 * i:3 * i + 3]
        l6 = (77 * r + 150 * g + 29 * b) >> 8
        out.append((l6 * 17 + 32) >> 6)
    return out


def vga8_mono(cv, w, h, pal):
    """A VGA8 canvas (w pixels a row, a multiple of 8) as the MONO1 canvas
    the Preview shows: a pixel is lit when its luma beats the 4 x 4 Bayer
    cell over it - exactly the player's vp_v8mono"""
    lum = vga8_lum16(pal)
    wb = w // 8
    out = bytearray(wb * h)
    for y in range(h):
        row = cv[y * w:(y + 1) * w]
        by = BAYER4[(y & 3) * 4:(y & 3) * 4 + 4]
        for xb in range(wb):
            v = 0
            for i in range(8):
                x = xb * 8 + i
                if lum[row[x]] > by[x & 3]:
                    v |= 0x80 >> i
            out[y * wb + xb] = v
    return bytes(out)
KEY_SECS = 2.0                  # a keyframe every 2 seconds (98.1.3)
POSTER_FLAT = 0.98              # ...the poster is the first that is not
                                # this much one byte value
REC_HDR = 6                     # len, y0, y1

# The wave 0 cost model, MartyPC's CGA 5150 writing the screen, cycles
# (docs/reports/VIDEO-W0-2026-09-25.md, section (a)). A slice and a run are
# linear fits through the 16- and 40-byte rows, which both land on them.
CYC_FRAME, CYC_SEG, CYC_ABS = 1214, 215, 14
CYC_P = (49.6, 65.2, 89.1, 104.8, 125.5, 146.1)
CYC_SLICE = (84, 18.0)          # base, per byte
CYC_RUN = (101, 13.0)
# ...and a layout whose decoder is not that one. C160 (98.3.12.1) is written
# straight to the text screen, a cell's attribute at every other address:
# FITTED on MartyPC's CGA 5150 by `tools/os88vidprof.py --cal` over 149
# frames of camera footage (residual 0.6%). Before it the file was decoded
# into a shadow and copied, and this model priced the decode alone - a
# quarter of what the play really cost (VIDEO-PLAN 15.8)
CYC_LAYOUT = {
    6: dict(frame=CYC_FRAME, seg=242, abs=27,
            p=(58.5, 86.3, 120.4, 141.3, 180.9, 209.0),
            slice=(188, 33.9), run=(83, 25.8)),
}
# TXT (C512, 98.1.3.5) is vd_native onto the SAME text screen, so until
# `os88vidprof.py --cal` fits its own it is priced at C160's constants: the
# 80-column screen's wait states are most of either, and C160's also pay an
# `inc di` a byte that TXT does not, which errs on the side of a frame kept
CYC_LAYOUT[7] = dict(CYC_LAYOUT[6])
# TEXT (98.1.3.6) is the same decoder onto the same kind of screen, not
# retimed: TXT's constants, until --cal fits its own
CYC_LAYOUT[8] = dict(CYC_LAYOUT[7])


def cyc_table(layout=None):
    """The model's constants for a layout's decoder: (frame, seg, abs, P,
    SLICE, RUN)"""
    t = CYC_LAYOUT.get(layout)
    if t is None:
        return CYC_FRAME, CYC_SEG, CYC_ABS, CYC_P, CYC_SLICE, CYC_RUN
    return t["frame"], t["seg"], t["abs"], t["p"], t["slice"], t["run"]
HZ = 4772727.0


class Geom:
    """A canvas on a layout: where each row starts in the surface's memory
    image, which addresses belong to the canvas, and which row each is."""

    def __init__(self, layout, wb, h, bitplanes=False, stride=None):
        if layout not in LAYOUTS:
            raise V88Error("layout %d is not one of SPEC.md 98.1.2's" % layout)
        banks, lstride, rows, self.name = LAYOUTS[layout]
        # (a NARROW screen's rows, 98.1.3.2.5: LIN80's bit-planes 40 apart)
        if stride is not None and (layout != LAY_LIN80 or not bitplanes or
                                   stride not in (40, 80)):
            raise V88Error("a stride of %r: a VGA4 file's 40 or 80" % stride)
        stride = stride or lstride
        if not (1 <= wb <= stride and 1 <= h <= rows):
            raise V88Error("a %d x %d canvas does not fit %s (%d x %d)"
                           % (wb, h, self.name, stride, rows))
        self.layout, self.wb, self.h = layout, wb, h
        self.banks, self.stride = banks, stride
        self.bitplanes = bitplanes and layout == LAY_LIN80
        self.planes = 4 if layout == LAY_MODEX or self.bitplanes else 1
        self.sub2 = False           # SUB2's sub-records (98.1.3.2.4): the
                                    # file's flag, set by its Writer's maker
                                    # and by a Reader
        self.w = wb * PIX_PER_BYTE[layout]
        self.base = [(y % banks) * 8192 + (y // banks) * stride
                     for y in range(h)]
        self.valid = bytearray(65536)
        self.rowof = [-1] * 65536
        for y, b in enumerate(self.base):
            self.valid[b:b + wb] = b"\x01" * wb
            for x in range(wb):
                self.rowof[b + x] = y

    def surface(self):
        """A black surface image: 64 KB, or a plane of it each for MODEX"""
        return bytearray(PLANE * self.planes)

    def canvas(self, surf):
        """The canvas out of a surface image, top row first - for MODEX a
        pixel a byte, pixel x in plane x mod 4"""
        if self.planes == 1:
            return b"".join(bytes(surf[b:b + self.wb]) for b in self.base)
        if self.bitplanes:          # VGA4: bit x of each plane, a pixel
            w, out = self.w, bytearray(self.w * self.h)
            for y, b in enumerate(self.base):
                for xb in range(self.wb):
                    bt = [surf[p * PLANE + b + xb] for p in range(4)]
                    for i in range(8):
                        out[y * w + xb * 8 + i] = sum(
                            ((bt[p] >> (7 - i)) & 1) << p for p in range(4))
            return bytes(out)
        w, out = self.w, bytearray(self.w * self.h)
        for y, b in enumerate(self.base):
            for p in range(4):
                out[y * w + p:(y + 1) * w:4] = surf[p * PLANE + b:
                                                    p * PLANE + b + self.wb]
        return bytes(out)

    def put(self, surf, cv):
        if self.planes == 1:
            for y, b in enumerate(self.base):
                surf[b:b + self.wb] = cv[y * self.wb:(y + 1) * self.wb]
            return
        if self.bitplanes:
            for y, b in enumerate(self.base):
                row = cv[y * self.w:(y + 1) * self.w]
                for p in range(4):
                    for xb in range(self.wb):
                        v = 0
                        for i in range(8):
                            v |= ((row[xb * 8 + i] >> p) & 1) << (7 - i)
                        surf[p * PLANE + b + xb] = v
            return
        w = self.w
        for y, b in enumerate(self.base):
            for p in range(4):
                surf[p * PLANE + b:p * PLANE + b + self.wb] = \
                    cv[y * w + p:(y + 1) * w:4]


def spans(changed, surf, g, valid=None, gaps=True):
    """Writes for the canvas addresses in `changed`, whose new values
    are in `surf`. Adjacent addresses make one span. A gap of one byte is
    closed (a P1 + P1 costs more than the P3 that covers both), and so is a
    gap of up to four between two spans of 7 or more (a slice entry's ~84
    cycles of set-up against ~18 a byte) - but only across canvas bytes,
    so nothing outside the canvas is ever written."""
    sp = []
    for a in sorted(changed):      # a banked layout's rows are not in
        if sp and a == sp[-1][1]:  # address order
            sp[-1][1] = a + 1
        else:
            sp.append([a, a + 1])
    out = []
    valid = g.valid if valid is None else valid
    for s, e in sp:
        if out and gaps:
            ps, pe = out[-1]
            gap = s - pe
            if gap <= 4 and all(valid[pe:s]) and \
                    (gap <= 1 or (pe - ps >= 7 and e - s >= 7)):
                out[-1][1] = e
                continue
        out.append([s, e])
    return [(s, bytes(surf[s:e]), False) for s, e in out]


def clip_ops(ops, g):
    """`ops` cut to canvas addresses: XDC's full-screen fill is one 16 KB
    rep stosb straight across the two 192-byte holes at the ends of CGA's
    banks, which no row owns. The picture is the same; the writer's rule
    (SPEC.md 98.1.3) is that nothing outside the canvas is written."""
    out = []
    for a, bs, run in ops:
        i, n = 0, len(bs)
        while i < n:
            while i < n and not g.valid[a + i]:
                i += 1
            j = i
            while j < n and g.valid[a + j]:
                j += 1
            if j > i:
                out.append((a + i, bs[i:j], run))
            i = j
    return out


def band_of(ops, g):
    """(y0, y1): the canvas rows `ops` write, y0 <= row < y1; (0, 0) none.
    A span stays inside one bank, and a bank's rows rise with its
    addresses, so a span's first and last byte bound its rows."""
    if not ops:
        return 0, 0
    y0 = min(g.rowof[a] for a, bs, r in ops)
    y1 = max(g.rowof[a + len(bs) - 1] for a, bs, r in ops) + 1
    return y0, y1


def record(ops, g, audio=b"", limit=SP_MAX * SECTOR - 4):
    """A frame record; `limit` is a super-packet's room, or a keyframe's
    (65,535: its length is a word, and it rides in no super-packet). None
    MEASURES: the encoder's attempt at a frame, which is never written -
    the Writer builds the record again from the ops - so one past the
    length word comes back at its true length (its word saying 65,535)
    for the encoder to cut, rather than ending the encode"""
    if g.planes > 1:
        # MODEX (98.1.3.1): sub-records, each its Map Mask and ten lists,
        # a 0 after the last - ops is [(mask, ops), ...]; with SUB2 each
        # carries its list map, and a lead past 0Fh is write mode 3
        body, every = bytearray(), []
        for mask, sub in ops:
            if sub:
                if not 1 <= mask <= (0x1F if g.sub2 and g.bitplanes
                                     else 0x0F):
                    raise V88Error("a sub-record's lead %02x" % mask)
                body.append(mask)
                body += to_lists(sub, mapped=g.sub2)[0]
                every += sub
        body.append(0)
        lists = bytes(body)
        y0, y1 = band_of(every, g)
    else:
        lists, e, s = to_lists(ops)
        y0, y1 = band_of(ops, g)
    n = REC_HDR + len(lists) + len(audio)
    if limit is not None and n > limit:
        raise V88Error("a record of %d bytes cannot fit %s" % (
            n, "a super-packet" if limit < 65534 else "its length word"))
    return struct.pack("<HHH", min(n, 65535), y0, y1) + lists + audio


PREV_MAX = 31 * 1024             # a flipped play's copy of the last record
                                # (98.3.8): the seam passes through it too
FRAMES_MAX = 65535              # the player's frame count is a word, and it
KEYS_MAX = 16383                # refuses more; keyframes 0..16,383 (98.1.1)


def seam_ops(a, b, g):
    """The writes that take surface image `a` to `b` - the seam's (98.1.1.2)"""
    if g.planes > 1:
        ca, cb = g.canvas(a), g.canvas(b)
        if g.bitplanes:
            return vga4_subs(cb, ca, g)
        return modex_subs(cb, [x != y for x, y in zip(cb, ca)], g)
    changed = [x for base in g.base for x in range(base, base + g.wb)
               if a[x] != b[x]]
    return spans(changed, b, g)


def delta_ops(surf, base, g):
    """The writes that take surface `base` to `surf` - a key's hidden page
    over its shown one (98.1.3.7)"""
    if g.bitplanes:
        return vga4_subs(g.canvas(surf), g.canvas(base), g)
    if g.planes > 1:
        cv, bv = g.canvas(surf), g.canvas(base)
        return modex_subs(cv, [a != b for a, b in zip(cv, bv)], g)
    changed = [a for b in g.base for a in range(b, b + g.wb)
               if surf[a] != base[a]]
    return spans(changed, surf, g)


def page_record(ops, hidden, g, flip=False, limit=SP_MAX * SECTOR - 4):
    """PAGE-AHEAD (98.1.3.7): a record of `ops` - with REC_FLIP, drawn on
    the hidden page and then shown - and, when `hidden` writes anything,
    the hidden page's own record after its lists, inside its len. No audio:
    the caller appends it, after both"""
    rec = bytearray(record(ops, g, limit=limit))
    n, y0, y1 = struct.unpack_from("<HHH", rec, 0)
    if hidden and (g.planes == 1 or any(sub for m, sub in hidden)):
        rec += record(hidden, g, limit=limit)
        y1 |= REC_HID
    if flip:
        y0 |= REC_FLIP
    if limit is not None and len(rec) > limit:
        raise V88Error("a record of %d bytes cannot fit %s" % (
            len(rec), "a super-packet" if limit < 65534 else
            "its length word"))
    struct.pack_into("<HHH", rec, 0, min(len(rec), 65535), y0, y1)
    return bytes(rec)


def page_bytes(layout, screen):
    """A page of a screen with more than one (98.1.3.7.1): Mode X's 19,200
    plane bytes or 320 x 200's 16,000 (98.1.3.1.1), a Hercules's 32 KB, a
    VGA4 screen's rows x 80 (98.1.3.2.1)"""
    if layout == LAY_MODEX:
        return 16000 if screen == SCR_320X200 else 19200
    if layout == LAY_HERC:
        return 0x8000
    return SCREENS[screen][2] * screen_stride(screen)


def mpage_record(ops, hidden, g, flip=None, kpage=None,
                 limit=SP_MAX * SECTOR - 4, built=False, copies=()):
    """PAGE-AHEAD ON N PAGES (98.1.3.7.1): a record of `ops` - with `flip`
    a page, drawn on that page and then shown; with `kpage`, a KEY naming
    the page shown after it - and the hidden records of `hidden`, a list of
    (page, ops), chained after its lists inside its len, each naming its
    page. A page whose ops write nothing is left out. No audio: the caller
    appends it, after them all. FLIPPED AND BUILT AHEAD (98.1.3.7.2):
    `built`, the flip's page drawn on as it stands; `copies`, a list of
    (destination, source, y0, y1), COPY records after the hidden ones"""
    rec = bytearray(record(ops, g, limit=limit))
    n, y0, y1 = struct.unpack_from("<HHH", rec, 0)
    if y0 > ROW_MASK or y1 > ROW_MASK:
        raise V88Error("rows %d..%d past an N-page record's 4,095"
                       % (y0, y1))
    hs = [(p, h) for p, h in (hidden or ())
          if h and (g.planes == 1 or any(sub for m, sub in h))]
    pages = [p for p, h in hs]
    if len(set(pages)) != len(pages) or \
            any(not 0 <= p < MPAGE_MAX for p in pages):
        raise V88Error("hidden records on pages %r" % (pages,))
    cps = list(copies or ())
    for i, (p, h) in enumerate(hs):
        hr = bytearray(record(h, g, limit=limit))
        hn, hy0, hy1 = struct.unpack_from("<HHH", hr, 0)
        struct.pack_into("<HH", hr, 2, hy0 | (p << PG_SHIFT),
                         hy1 | (HID_MORE if i + 1 < len(hs) or cps else 0))
        rec += hr
    for i, (d, src, c0, c1) in enumerate(cps):
        if not (0 <= d < MPAGE_MAX and 0 <= src < MPAGE_MAX and d != src and
                0 <= c0 < c1 <= g.h) or d in pages or \
                d in [x[0] for x in cps[:i]]:
            raise V88Error("a copy of rows %d..%d of page %d onto %d"
                           % (c0, c1, src, d))
        rec += struct.pack("<HHH", COPY_LEN, c0 | (d << PG_SHIFT) | REC_FLIP,
                           c1 | (src << PG_SHIFT) |
                           (HID_MORE if i + 1 < len(cps) else 0))
    if hs or cps:
        y1 |= REC_HID
    if built:
        if flip is None or flip is False:
            raise V88Error("a record built ahead that does not flip")
        y1 |= REC_BUILT
    if flip is not None and flip is not False:
        if kpage is not None:
            raise V88Error("a keyframe that flips")
        y0 |= REC_FLIP | (flip << PG_SHIFT)
    elif kpage is not None:
        y0 |= kpage << PG_SHIFT
    if limit is not None and len(rec) > limit:
        raise V88Error("a record of %d bytes cannot fit %s" % (
            len(rec), "a super-packet" if limit < 65534 else
            "its length word"))
    struct.pack_into("<HHH", rec, 0, min(len(rec), 65535), y0, y1)
    return bytes(rec)


def mpage_parts(rec, g, npages, fpage=False, kref=False):
    """An N-page record (98.1.3.7.1) as (flip, page, its own record with
    its tail and the bits cleared, [(page, hidden record)]): `page` is the
    page a flip shows, or a key's page shown after it, else 0. With
    `fpage` (98.1.3.7.2) a fifth, BUILT, and a COPY in the list as (its
    destination, (source, y0, y1)); with `kref` a key's last part's
    REFERENCE to the page pool as (its page, ("ref", at, n)), 98.1.3.8.1"""
    n, y0, y1 = struct.unpack_from("<HHH", rec, 0)
    flip, pg = bool(y0 & REC_FLIP), (y0 & PG_BITS) >> PG_SHIFT
    built = bool(y1 & REC_BUILT) if fpage else False
    if y1 & PG_BITS & ~(REC_BUILT if fpage else 0) or pg >= npages:
        raise V88Error("a record on page %d of %d" % (pg, npages))
    own = bytearray(rec)
    struct.pack_into("<HH", own, 2, y0 & ROW_MASK, y1 & ROW_MASK)
    if not y1 & REC_HID:
        return (flip, pg, bytes(own), [], built) if fpage else \
            (flip, pg, bytes(own), [])
    e0 = e = lists_end(own, g)
    hs, seen = [], set()
    while True:
        if e + REC_HDR > n:
            raise V88Error("a hidden-page record past its record's end")
        hn, hy0, hy1 = struct.unpack_from("<HHH", rec, e)
        hp = (hy0 & PG_BITS) >> PG_SHIFT
        if kref and hy0 & REC_FLIP:     # A REFERENCE (98.1.3.8.1)
            if hn != REF_LEN or e + hn > n or hp >= npages or hp in seen or \
                    hy0 & ROW_MASK or hy1 & ~HID_MORE & 0xFFFF:
                raise V88Error("a page pool reference of %d bytes at %d "
                               "(page %d)" % (hn, e, hp))
            at, rn = struct.unpack_from("<IH", rec, e + 6)
            seen.add(hp)
            hs.append((hp, ("ref", at, rn)))
            e += hn
            if not hy1 & HID_MORE:
                break
            continue
        if fpage and hy0 & REC_FLIP:    # A COPY (98.1.3.7.2): its header
            src = (hy1 & PG_BITS) >> PG_SHIFT   # alone
            c0, c1 = hy0 & ROW_MASK, hy1 & ROW_MASK
            if hn != COPY_LEN or e + hn > n or hp >= npages or \
                    src >= npages or src == hp or hp in seen or \
                    not c0 < c1 <= g.h:
                raise V88Error("a copy of rows %d..%d of page %d onto page "
                               "%d at %d" % (c0, c1, src, hp, e))
            seen.add(hp)
            hs.append((hp, (src, c0, c1)))
            e += hn
            if not hy1 & HID_MORE:
                break
            continue
        if hn < REC_HDR + 1 or e + hn > n or hy0 & REC_FLIP or \
                hy1 & PG_BITS or hp >= npages or hp in seen:
            raise V88Error("a hidden-page record of %d bytes at %d (page "
                           "%d)" % (hn, e, hp))
        seen.add(hp)
        hr = bytearray(rec[e:e + hn])
        struct.pack_into("<HH", hr, 2, hy0 & ROW_MASK, hy1 & ROW_MASK)
        hs.append((hp, bytes(hr)))
        e += hn
        if not hy1 & HID_MORE:
            break
    own = own[:e0] + own[e:]
    struct.pack_into("<H", own, 0, n - (e - e0))
    return (flip, pg, bytes(own), hs, built) if fpage else \
        (flip, pg, bytes(own), hs)


def _ops_cut(ops, lo, hi, planar):
    """`ops` cut to the addresses lo <= a < hi - a span across an end cut
    there, which to_lists takes at any length"""
    if planar:
        return [(m, _ops_cut(sub, lo, hi, False)) for m, sub in ops]
    out = []
    for a, bs, run in ops:
        s, e = max(a, lo), min(a + len(bs), hi)
        if s < e:
            out.append((s, bs[s - a:e - a], run))
    return out


def _ops_any(ops, planar):
    return any(sub for m, sub in ops) if planar else bool(ops)


def key_record(ops, g, hidden=None, page=False, ref=0, part=KEY_PART,
               kpage=None):
    """A KEYFRAME's bytes (98.1.3, 98.1.3.8): its record - with `page`, a
    page-ahead key's, `hidden` the hidden page's difference after its lists
    - when it and the `ref` bytes the writer appends after it (an ADPCM4
    key's reference) are one read of `part` at most; else KEY PARTS, the
    canvas cut by address into bands each a key record of its own (its
    band of the hidden page with it), every one but the last with KEY_MORE
    in its y0 and the next read's bytes as its last two. `part` None: one
    record whatever its size, as before the parts"""
    planar = g.planes > 1

    def one(lo, hi):
        o = _ops_cut(ops, lo, hi, planar)
        if kpage is not None:           # N PAGES (98.1.3.7.1): (page, ops)s
            hs = [(p, _ops_cut(h, lo, hi, planar)) for p, h in hidden or ()]
            return mpage_record(o, hs, g, kpage=kpage, limit=None), \
                _ops_any(o, planar) or any(_ops_any(h, planar)
                                           for p, h in hs if h)
        if page:
            h = _ops_cut(hidden, lo, hi, planar) if hidden else hidden
            return page_record(o, h, g, limit=None), \
                _ops_any(o, planar) or bool(h and _ops_any(h, planar))
        return record(o, g, limit=None), _ops_any(o, planar)
    whole = one(0, 0x10000)[0]
    if part is None or len(whole) + ref <= part:
        return whole
    parts = []

    def cut(lo, hi):
        r, used = one(lo, hi)
        if not used:
            return
        if len(r) + 2 + ref <= part or hi - lo <= 1:
            if len(r) + 2 + ref > part:
                raise V88Error("a key's byte at %04x is past a read" % lo)
            parts.append(r)
            return
        mid = (lo + hi) // 2
        cut(lo, mid)
        cut(mid, hi)
    n = -(-len(whole) * 10 // (part * 9))      # (bands of a tenth's room)
    for i in range(n):
        cut(0x10000 * i // n, 0x10000 * (i + 1) // n)
    reads = [len(r) + 2 for r in parts[:-1]] + [len(parts[-1]) + ref]
    out = bytearray()
    for i, r in enumerate(parts):
        r = bytearray(r)
        if i + 1 < len(parts):
            r += struct.pack("<H", reads[i + 1])
            n_, y0, y1 = struct.unpack_from("<HHH", r, 0)
            struct.pack_into("<HH", r, 0, len(r), y0 | KEY_MORE)
        out += r
    return bytes(out)


def key_refs(krec, refs, g):
    """A key's bytes with REFERENCES to the page pool (98.1.3.8.1) - a list
    of (page, at, n) - after its last part's hidden records, and where each
    one's `at` is in the bytes returned"""
    if not refs:
        return krec, []
    off = 0
    while True:                         # (the last part)
        n, y0 = struct.unpack_from("<HH", krec, off)
        if not y0 & KEY_MORE:
            break
        off += n
    last = bytearray(krec[off:])
    n, y0, y1 = struct.unpack_from("<HHH", last, 0)
    if y1 & REC_HID:                    # (the chain's last entry links on)
        own = bytearray(last)
        struct.pack_into("<HH", own, 2, y0 & ROW_MASK, y1 & ROW_MASK)
        e = lists_end(own, g)
        while True:
            hn, hy0, hy1 = struct.unpack_from("<HHH", last, e)
            if not hy1 & HID_MORE:
                struct.pack_into("<H", last, e + 4, hy1 | HID_MORE)
                break
            e += hn
    tail = bytes(last[n:])              # (nothing: the writer's ADPCM4
    last = last[:n]                     # byte comes after this)
    at = []
    for i, (p, a, rn) in enumerate(refs):
        at.append(off + len(last) + 6)
        last += struct.pack("<HHHIH", REF_LEN, REC_FLIP | (p << PG_SHIFT),
                            HID_MORE if i + 1 < len(refs) else 0, a, rn)
    struct.pack_into("<HHH", last, 0, len(last), y0, y1 | REC_HID)
    head = bytearray(krec[:off])
    if off:                             # (the part before names this read,
        struct.pack_into("<H", head, off - 2,   # which the references
                         len(last) + len(tail))  # have just made longer)
    return bytes(head) + bytes(last) + tail, at


def key_split(krec):
    """A key's bytes -> its records: each part with KEY_MORE and its last
    two bytes gone, and the last with what follows it (an ADPCM4 key's
    reference), as one record's bytes always were (98.1.3.8). A key of one
    record is itself. Every read is checked against what follows"""
    out, off = [], 0
    while True:
        if off + REC_HDR > len(krec):
            raise V88Error("a key's part runs off its bytes")
        n, y0, y1 = struct.unpack_from("<HHH", krec, off)
        if not y0 & KEY_MORE:
            out.append(bytes(krec[off:]))
            return out
        if n < REC_HDR + 3 or off + n > len(krec):
            raise V88Error("a key's part of %d bytes at %d" % (n, off))
        nxt = struct.unpack_from("<H", krec, off + n - 2)[0]
        r = bytearray(krec[off:off + n - 2])
        struct.pack_into("<HH", r, 0, n - 2, y0 & ~KEY_MORE)
        out.append(bytes(r))
        off += n
        if off + REC_HDR > len(krec):
            raise V88Error("a key's part says more follow, and none does")
        n2, y02 = struct.unpack_from("<HH", krec, off)
        if nxt != (n2 if y02 & KEY_MORE else len(krec) - off):
            raise V88Error("a key's part says the next read is %d bytes"
                           % nxt)


def key_reads(krec):
    """The reads a key's bytes take (98.1.3.8): one per part"""
    out, off = [], 0
    while True:
        if off + REC_HDR > len(krec):
            raise V88Error("a key's part runs off its bytes")
        n, y0 = struct.unpack_from("<HH", krec, off)
        if not y0 & KEY_MORE:
            return out + [len(krec) - off]
        out.append(n)
        off += n


def page_parts(rec, g):
    """A page-ahead record (98.1.3.7) as (flip, its own record with its
    tail - audio, or a key's reference - and the bits cleared, the hidden
    page's record or None)"""
    n, y0, y1 = struct.unpack_from("<HHH", rec, 0)
    flip, hid = bool(y0 & REC_FLIP), bool(y1 & REC_HID)
    own = bytearray(rec)
    struct.pack_into("<HH", own, 2, y0 & ~REC_FLIP, y1 & ~REC_HID)
    if not hid:
        return flip, bytes(own), None
    e = lists_end(own, g)
    if e + REC_HDR > n:
        raise V88Error("a hidden-page record past its record's end")
    hn, hy0, hy1 = struct.unpack_from("<HHH", rec, e)
    if hn < REC_HDR + 1 or e + hn > n or hy0 & REC_FLIP or hy1 & REC_HID:
        raise V88Error("a hidden-page record of %d bytes at %d" % (hn, e))
    hrec = bytes(rec[e:e + hn])
    own = own[:e] + own[e + hn:]
    struct.pack_into("<H", own, 0, n - hn)
    return flip, bytes(own), hrec


def keyframe_ops(surf, g):
    if g.bitplanes:
        return vga4_subs(g.canvas(surf), bytes(g.w * g.h), g)
    if g.planes > 1:
        cv = g.canvas(surf)
        return modex_subs(cv, [v != 0 for v in cv], g)
    changed = [a for b in g.base for a in range(b, b + g.wb) if surf[a]]
    return spans(changed, surf, g)


def vga4_planes(cv, g):
    """A VGA4 canvas's four plane images, each g.wb x g.h bytes, dense"""
    out = [bytearray(g.wb * g.h) for _ in range(4)]
    w = g.w
    for y in range(g.h):
        row = cv[y * w:(y + 1) * w]
        for xb in range(g.wb):
            px = row[xb * 8:xb * 8 + 8]
            for p in range(4):
                v = 0
                for i in range(8):
                    v |= ((px[i] >> p) & 1) << (7 - i)
                out[p][y * g.wb + xb] = v
    return out


def vga4_subs(cv, prev, g):
    """A VGA4 frame's writes (98.1.3.2): canvas `cv` over canvas `prev`. A
    byte is eight pixels' bit of ONE plane; at each byte where a plane
    changes, the planes that want the SAME value - changed or not, since
    writing a plane its own value changes nothing - are one store under
    their combined mask. So black-and-white is a store per eight pixels
    under 0Fh, as a one-bit file is, and colour costs what it differs by.
    No span closes a gap: a gap byte's planes need not share a value"""
    tp, sp = vga4_planes(cv, g), vga4_planes(prev, g)
    addr = {}
    msurf = {}
    for y, b in enumerate(g.base):
        for xb in range(g.wb):
            i = y * g.wb + xb
            t = [tp[p][i] for p in range(4)]
            ch = [t[p] != sp[p][i] for p in range(4)]
            if not any(ch):
                continue
            done = 0
            for p in range(4):
                if not ch[p] or done >> p & 1:
                    continue
                m = sum(1 << q for q in range(4) if t[q] == t[p])
                done |= m
                addr.setdefault(m, []).append(b + xb)
                msurf.setdefault(m, bytearray(PLANE))[b + xb] = t[p]
    order = sorted(addr, key=lambda m: (m != 15, m))
    return [(m, spans(addr[m], msurf[m], g, gaps=False)) for m in order]


def vga4_xlat(palette):
    """A VGA4 file's own sixteen (98.1.3.2.1) as the desktop's: each pixel
    value -> the nearest of STD16 in RGB, the first of equals - what the
    Preview's poster draws them in (98.4.5), the player's vp_v4xl"""
    out = []
    for i in range(16):
        c = palette[3 * i:3 * i + 3]
        d = [sum((c[j] - STD16[3 * k + j]) ** 2 for j in range(3))
             for k in range(16)]
        out.append(d.index(min(d)))
    return bytes(out)


def vga4_pack(cv, w, h, step=1, xlat=None):
    """The canvas as OSAPI_GFX_BLIT4 takes it - two pixels a byte, the
    left in the high nibble - every `step`-th pixel of every `step`-th row,
    through `xlat` (vga4_xlat) for a file of its own colours: the player's
    vp_v4pack, byte for byte"""
    ow, oh = w // step, (h + step - 1) // step
    obw = (ow + 1) // 2
    out = bytearray(obw * oh)
    for oy in range(oh):
        row = cv[oy * step * w:(oy * step + 1) * w]
        for ox in range(ow):
            v = row[ox * step] & 15
            if xlat:
                v = xlat[v]
            out[oy * obw + ox // 2] |= v << 4 if not ox & 1 else v
    return bytes(out), obw, ow, oh


def modex_subs(cv, changed, g):
    """modex_subs_loop's writes, worked out over whole arrays (the encoder
    speed pass, VIDEO-SEEN-PLAN 3a: the loop was 43 ms a frame on a 320 x
    180 canvas and the encoder asks it several times a frame). The same
    classification - a group of one colour, a pair, a plane's own - and the
    same spans; `tests/videnc.py` holds the two to the same answer"""
    import numpy as np
    w, h, wb = g.w, g.h, g.wb
    G = np.frombuffer(bytes(cv), np.uint8)[:h * w].reshape(h, wb, 4)
    C = np.asarray(changed, dtype=bool)[:h * w].reshape(h, wb, 4)
    n = C.sum(2)
    any_ = n > 0
    addr = np.asarray(g.base, np.int64)[:h, None] + np.arange(wb)[None, :]
    grpm = any_ & (n >= 2) & (G == G[:, :, :1]).all(2)
    rest = any_ & ~grpm
    ga = addr[grpm]
    gsa = np.zeros(PLANE, np.uint8)
    gsa[ga] = G[:, :, 0][grpm]
    gsurf = bytearray(gsa.tobytes())
    psurf = []
    for p_ in range(4):
        ps = np.zeros(PLANE, np.uint8)
        ps[addr.ravel()] = G[:, :, p_].ravel()
        psurf.append(bytearray(ps.tobytes()))
    pv = [np.frombuffer(bytes(g.valid), np.uint8).copy() for _ in range(4)]
    for p_ in range(4):
        pv[p_][ga] = 0
    pair, psv, per = {}, {}, [None] * 4
    for hh in (0, 2):
        pm = rest & C[:, :, hh] & C[:, :, hh + 1] & \
            (G[:, :, hh] == G[:, :, hh + 1])
        pa = addr[pm]
        pair[hh] = pa.tolist()
        sv = np.zeros(PLANE, np.uint8)
        sv[pa] = G[:, :, hh][pm]
        psv[hh] = bytearray(sv.tobytes())
        pv[hh][pa] = 0
        pv[hh + 1][pa] = 0
        for p_ in (hh, hh + 1):
            per[p_] = addr[rest & ~pm & C[:, :, p_]].tolist()
    pvalid = [bytearray(v.tobytes()) for v in pv]
    return [(0x0F, spans(ga.tolist(), gsurf, g, gaps=False)),
            (0x03, spans(pair[0], psv[0], g, gaps=False)),
            (0x0C, spans(pair[2], psv[2], g, gaps=False))] + \
        [(1 << p_, spans(per[p_], psurf[p_], g, valid=pvalid[p_]))
         for p_ in range(4)]


def modex_subs_loop(cv, changed, g):
    """A MODEX frame's writes (98.1.3.1): the canvas `cv` (a pixel a byte)
    at the pixels `changed` flags. An aligned group of four pixels of ONE
    colour with two or more of them changed is one byte under Map Mask 0Fh
    - four pixels a store; a PAIR (pixels 0-1 or 2-3 of a group) of one
    colour, both changed, is one byte under 03h or 0Ch; every other changed
    pixel is its plane's. A
    plane's spans may close a gap over bytes that are already right (the
    target's own bytes, so nothing changes), but never over a group the
    0Fh sub-record writes, and 0Fh's spans close no gap at all: a byte
    there is four pixels, and a gap's four need not be one colour"""
    w = g.w
    grp, per = [], [[] for _ in range(4)]
    pair = {0: [], 2: []}
    psv = {0: bytearray(PLANE), 2: bytearray(PLANE)}
    gsurf = bytearray(PLANE)
    psurf = [bytearray(PLANE) for _ in range(4)]
    pvalid = [bytearray(g.valid) for _ in range(4)]
    for y, b in enumerate(g.base):
        row = cv[y * w:(y + 1) * w]
        for p in range(4):
            psurf[p][b:b + g.wb] = row[p::4]
        for xb in range(g.wb):
            x = xb * 4
            ch = changed[y * w + x:y * w + x + 4]
            n = sum(1 for c in ch if c)
            if not n:
                continue
            v = row[x]
            if n >= 2 and row[x + 1] == v and row[x + 2] == v and \
                    row[x + 3] == v:
                grp.append(b + xb)
                gsurf[b + xb] = v
                for p in range(4):
                    pvalid[p][b + xb] = 0
                continue
            for h, m in ((0, 3), (2, 12)):  # a PAIR of one colour, both
                if ch[h] and ch[h + 1] and row[x + h] == row[x + h + 1]:
                    pair[h].append(b + xb)      # changed: a byte under 3
                    psv[h][b + xb] = row[x + h]  # or 0Ch
                    pvalid[h][b + xb] = pvalid[h + 1][b + xb] = 0
                    continue
                for p in (h, h + 1):
                    if ch[p]:
                        per[p].append(b + xb)
    return [(0x0F, spans(grp, gsurf, g, gaps=False)),
            (0x03, spans(pair[0], psv[0], g, gaps=False)),
            (0x0C, spans(pair[2], psv[2], g, gaps=False))] + \
        [(1 << p, spans(per[p], psurf[p], g, valid=pvalid[p]))
         for p in range(4)]


def flat(cv):
    return max(cv.count(bytes([v])) for v in set(cv)) >= POSTER_FLAT * len(cv) \
        if cv else True


PIT_HZ = 1193182
FSX_RATE_MIN = 2048             # apps/os88api.inc's


def pit_rate(rate, spf):
    """(divisor, periods a frame) for FSXF_RATE (SPEC.md 98.1.1): the fewest
    periods a frame whose period fits 16 bits, and that period rounded."""
    n = 1
    while PIT_HZ * spf / (rate * n) > 65535:
        n += 1
    div = round(PIT_HZ * spf / (rate * n))
    if div < FSX_RATE_MIN:
        raise V88Error("%.1f fps is faster than FSXF_RATE can pace"
                       % (rate / spf))
    return div, n


def write_whole(path, data):
    """A .V88 written WHOLE or not at all (98.2.11.1): into `path`.part and
    renamed over `path`, so an encode stopped mid-write - the encoder
    window's Cancel kills its process - leaves an older file of the name as
    it was, never half of a new one"""
    part = path + ".part"
    with open(part, "wb") as f:
        f.write(data)
    os.replace(part, path)


H_RING = 23             # the header's byte: the ring the stream assumes
RING_MAX = 15           # ...0, or 2 to the player's most (VP_KBIG): any
                        # count, where it was a power of two to 8 until the
                        # player took as many slots as the machine has
                        # (98.2.1.3.1)
SLOT = 32768


def ring_for(reserve):
    """The ring slots a disk reserve of `reserve` bytes needs: what it
    banks is read ahead of the slot being decoded AND of a super-packet
    straddling into the next, so the reserve and two slots more - None if
    no ring holds it (SPEC.md 98.2.1.3). It was one slot more, which let
    --reserve 224 bank a slot an 8-slot player never has"""
    for k in range(2, RING_MAX + 1):
        if (k - 2) * SLOT >= reserve:
            return k
    return None


class Writer:
    """Collects a stream frame by frame and writes SPEC.md 98.1's file."""

    def __init__(self, g, rate, spf, audio_fmt, abytes, pixfmt, title="",
                 credits="", aspect=None, keysecs=KEY_SECS, palette=None,
                 rowscale=1, flip=False, loop=None, repeat=False,
                 cgapal=None, spk=False, live=None, spkp=1, ahead=0,
                 kcap=65535, spcap=SP_MAX, screen=SCR_480, page=False,
                 pages=2):
        # `spcap`: a super-packet's sectors at most - SP_MAX, or past it
        # (BIGSP, 98.1.4.1) up to SP_BIG for a stream played in the bracket
        if not SP_MAX <= spcap <= SP_BIG or (spcap > SP_MAX and
                                             live is not None):
            raise V88Error("super-packets of %d sectors: %d to %d, and past "
                           "%d never live (98.1.4.1)"
                           % (spcap, SP_MAX, SP_BIG, SP_MAX))
        self.spcap = spcap
        # `kcap`: the largest key entry a play can read (keep_keys)
        self.kcap = kcap
        # `kpart`: a key's read at most - past it, KEY PARTS (98.1.3.8);
        # None keeps every key one record, as a resident file must
        self.kpart = KEY_PART if live is None else None
        # `live` (a TARGETS value): a STREAMED Live file (98.3.18.1) - its
        # frame records and seam carry blit runs between lists and audio
        if live is not None and (pixfmt not in (PF_MONO1, PF_VGA4)
                                 or g.layout != LAY_LIN80
                                 or (pixfmt == PF_VGA4 and live !=
                                     TARGETS["vga"])
                                 or g.h > 255 or g.wb > 255):
            raise V88Error("a streamed live file is a MONO1 LIN80 canvas, or "
                           "a VGA4 one for the VGA, of at most 255 rows and "
                           "bytes")
        self.live = live
        self.pev = None                 # [(frame, 48 bytes)], 98.1.3.2.3
        self.opts = None                # the options block, 98.1.1.4
        self.xbank = self.xpre = 0      # the bank's ask (98.3.18.3), KB
        if ahead and (audio_fmt == AUD_NONE or live is not None or
                      not 1 <= ahead <= 255 or ahead * abytes > AHEAD_MAX):
            raise V88Error("sound ahead by %d frames: a streamed file with "
                           "sound, 1..255 frames and at most %d bytes of it "
                           "(98.1.8)" % (ahead, AHEAD_MAX))
        self.ahead = ahead
        if spk and audio_fmt != AUD_PCM8:
            raise V88Error("speaker counts are PCM8's (98.1.1.3)")
        if spkp > 1 and not spk:
            raise V88Error("pulses a sample are a speaker file's (98.1.1.3.1)")
        if spk:
            spk_table(rate, spkp)
        self.spk, self.spkp = spk, spkp
        if (pixfmt in (PF_CGA4, PF_C512, PF_TEXT)) != (cgapal is not None):
            raise V88Error("a CGA4 file carries its palette byte, a C512 "
                           "file its card and a TEXT file its colour, and "
                           "no other file any of them")
        if pixfmt == PF_TEXT:
            if cgapal not in (TEXT_MONO, TEXT_COLOUR):
                raise V88Error("a TEXT colour byte of %r: 0 mono, 1 colour"
                               % (cgapal,))
            if g.wb % 4:
                raise V88Error("TEXT is an even number of whole cells")
        if pixfmt == PF_C512:
            if cgapal not in CARD_NAMES:
                raise V88Error("a C512 card of %r: 0 old, 1 new, 2 both"
                               % (cgapal,))
            if g.layout != LAY_TXT or g.wb % 4:
                raise V88Error("C512 is the text-80x100 layout's, an even number "
                               "of whole cells")
        if pixfmt == PF_CGA4:
            cga4_colours(cgapal)
            if g.layout != LAY_CGA:
                raise V88Error("CGA4 is mode 4's: the cga layout")
        if (pixfmt == PF_C160) != (g.layout == LAY_C160):
            raise V88Error("C160 is the c160 layout's, and it takes nothing "
                           "else")
        if (pixfmt == PF_C512) != (g.layout == LAY_TXT):
            raise V88Error("C512 is the text-80x100 layout's, and it takes "
                           "nothing "
                           "else")
        if (pixfmt == PF_TEXT) != (g.layout == LAY_TEXT):
            raise V88Error("TEXT is the text-80x25 layout's, and it takes "
                           "nothing "
                           "else")
        self.cgapal = cgapal
        if screen not in SCREENS or (screen and (
                not (pixfmt == PF_VGA4 or (g.layout == LAY_MODEX and
                                           screen == SCR_320X200)) or
                live is not None or
                g.w > SCREENS[screen][1] or g.h > SCREENS[screen][2])):
            raise V88Error("screen %r: a VGA4 file's, or Mode X's 320 x 200 "
                           "(98.1.3.1.1), not Live, and the canvas inside "
                           "it (98.1.3.2.1)" % (screen,))
        self.screen = screen
        if pixfmt == PF_VGA4 and g.stride != screen_stride(screen):
            raise V88Error("screen %r with rows %d bytes apart: a NARROW "
                           "screen's are 40, every other's 80 (98.1.3.2.5)"
                           % (screen, g.stride))
        if flip and screen in NARROW:
            raise V88Error("a NARROW screen is never flipped every frame "
                           "(98.1.3.2.5)")
        if flip and not (g.layout == LAY_MODEX or screen):
            raise V88Error("page flipping is Mode X's, or a VGA4 file's on "
                           "a screen of its own (98.3.8)")
        # FLIPPED AND BUILT AHEAD (98.1.3.7.2): every frame flipped AND
        # page-ahead, on three pages or more - a VGA's, never a Hercules's
        self.fpage = bool(flip and page and pages > 2 and
                          g.layout != LAY_HERC)
        self.flip = flip and not self.fpage
        # PAGE-AHEAD (98.1.3.7): the two pages a flipped play has, but a
        # flip only where the encoder built a frame ahead - full screen, a
        # stream, no lap (the seam would join pages it does not know). A
        # REPEAT is fine: its wrap is to key 0, which builds both pages
        if page and ((flip and not self.fpage) or
                     not (g.layout in (LAY_MODEX, LAY_HERC) or screen) or
                     live is not None or loop is not None):
            raise V88Error("page-ahead is Mode X's, a Hercules's, or a VGA4 "
                           "file's on a screen of its own: not flipped every "
                           "frame, not Live, no loop (98.1.3.7)")
        self.page = page
        # N PAGES (98.1.3.7.1): page-ahead past the second page, on a screen
        # whose plane holds them - Mode X or a VGA4 screen of its own
        if pages != 2 and (not page or not 3 <= pages <= MPAGE_MAX or
                           g.layout == LAY_HERC or
                           pages * page_bytes(g.layout, screen) > 0x10000):
            raise V88Error("page-ahead on %d pages: 3 to %d, Mode X's or a "
                           "VGA4 screen's of its own, and all of them in a "
                           "plane's 64 KB (98.1.3.7.1)" % (pages, MPAGE_MAX))
        self.pages = pages
        self.mpage = pages > 2
        self.kpool = True               # THE PAGE POOL (98.1.3.8.1), where
        self.kpg = {}                   # it saves bytes; a writer's choice
        self.pool = []
        if rowscale not in (1, 2) or (rowscale > 1 and
                                      g.layout not in VGA8_LAYOUTS):
            raise V88Error("a row scale of %d: 1, or 2 on a VGA8 layout"
                           % rowscale)
        self.rowscale = rowscale
        if rowscale * g.h > LAYOUTS[g.layout][2]:
            raise V88Error("%d rows shown twice do not fit %s"
                           % (g.h, g.name))
        if (pixfmt == PF_VGA8) != (g.layout in VGA8_LAYOUTS):
            raise V88Error("VGA8 is LIN320's and MODEX's format, and they "
                           "take no other")
        if (pixfmt == PF_VGA4) != g.bitplanes or \
                (pixfmt == PF_VGA4 and g.layout != LAY_LIN80):
            raise V88Error("VGA4 is LIN80's bit-planes, and nothing else's")
        if (pixfmt == PF_VGA8 and palette is None) or \
                (palette is not None and (
                    pixfmt not in (PF_VGA8, PF_VGA4) or
                    len(palette) != PAL_BYTES or max(palette) > 63 or
                    (pixfmt == PF_VGA4 and (live is not None or
                                            any(palette[48:]))))):
            raise V88Error("a VGA8 file carries 768 palette bytes of 0..63, "
                           "a VGA4 file played full screen may (its 16, "
                           "the rest 0), and no other file carries any")
        self.palette = bytes(palette) if palette is not None else None
        self.g, self.rate, self.spf = g, rate, spf
        self.audio_fmt, self.abytes, self.pixfmt = audio_fmt, abytes, pixfmt
        self.title, self.credits = title, credits
        self.aspect = aspect or ASPECT[g.layout]
        self.keyint = max(1, round(keysecs * rate / spf))
        self.recs, self.keys = [], []       # keys: (k, record, canvas)
        self.key0 = 0                       # the first key's frame (98.2.9)
        if loop is not None and audio_fmt == AUD_ADPCM4:
            # the card's decoder carries its state across the seam, and the
            # state at frame L is not the state after the last frame
            raise V88Error("a loop record with ADPCM4 audio: its decoder "
                           "state cannot join the seam; use PCM8")
        self.loop, self.repeat = loop, repeat
        self.loop_surf = self.loop_audio = self.last = None
        self.ybase = 0                  # (98.3.18.9)
        self.bench = (0, 0)             # (98.1.9.13: base's, layer's)
        self.lrecs = None               # THE LAYER (98.1.9): a record or
        self.layer = None               # L_EMPTY a frame, and (kbs, ram, pre)

    def layer_frame(self, ops):
        """The layer's writes for the next frame in order - [] for none -
        made on top of the base's record for the same frame (98.1.9)"""
        if self.lrecs is None:
            self.lrecs = []
        if len(self.lrecs) >= len(self.recs):
            raise V88Error("a layer record for frame %d, which the stream "
                           "has not reached" % len(self.lrecs))
        empty = not ops or (self.g.planes > 1 and
                            not any(sub for m, sub in ops))
        self.lrecs.append(L_EMPTY if empty else record(
            ops, self.g, limit=L_RECMAX))

    def frame(self, ops, surf, audio=b"", hidden=None, flip=False,
              hsurf=None, hsurfs=None, shown=0, built=False, copies=(),
              held=False):
        """`ops` are the frame's writes, already applied to `surf`. PAGE-
        AHEAD (98.1.3.7): `flip`, they were drawn on the page that is shown
        from this frame on; `hidden`, the hidden page's writes after them,
        and `hsurf` that page after them (what a key carries of it).
        ON N PAGES (98.1.3.7.1): `flip` is the page drawn on and shown (or
        None), `hidden` a list of (page, ops), `shown` the page shown after
        the frame and `hsurfs` every page by its number after it - what a
        key carries of each that is not the page shown. FLIPPED AND BUILT
        AHEAD (98.1.3.7.2): `flip` on every frame, `built` its page drawn
        on as it stands, `copies` (destination, source, y0, y1) COPYs after
        the hidden records - and `hsurfs` leaving out the page the next
        frame is drawn on (it is the key's own picture after a seek)"""
        if self.mpage:
            return self._mframe(ops, surf, audio, hidden, flip, hsurfs,
                                shown, built, copies)
        if built or copies:
            raise V88Error("a record built ahead or a copy, in a file not "
                           "flipped and built ahead (98.1.3.7.2)")
        if held and (not self.flip or len(self.recs) == self.loop):
            raise V88Error("a HELD record outside a flipped file, or at its "
                           "loop (98.1.3.9)")
        if (hidden or flip) and not self.page:
            raise V88Error("a hidden page's record in a file without "
                           "page-ahead")
        if len(audio) != self.abytes:
            raise V88Error("frame %d has %d audio bytes, not %d"
                           % (len(self.recs), len(audio), self.abytes))
        k = len(self.recs)
        if k >= FRAMES_MAX:
            raise V88Error("frame %d: a file is %d frames at most"
                           % (k, FRAMES_MAX))
        rec = self.with_runs(
            page_record(ops, hidden, self.g, flip,
                        limit=self.spcap * SECTOR - 4) if self.page else
            record(ops, self.g, limit=self.spcap * SECTOR - 4), audio,
            limit=self.spcap * SECTOR - 4)
        if self.flip and len(rec) > PREV_MAX and self.spcap == SP_MAX:
            raise V88Error("frame %d is %d bytes, past a flipped play's %d"
                           % (k, len(rec), PREV_MAX))
        if held:                        # (98.1.3.9: drawn, not shown)
            rec = bytearray(rec)
            rec[3] |= REC_HELD >> 8
            rec = bytes(rec)
            self.held = True
        self.recs.append(rec)
        self.last = surf
        if k == self.loop:
            self.loop_surf, self.loop_audio = bytes(surf), audio
        # (a key is the picture SHOWN: one due on a held frame is the next
        # frame's, which shows what it was drawing - 98.1.3.9)
        due = k >= self.key0 and (k - self.key0) % self.keyint == 0
        if held:
            self.kheld = getattr(self, "kheld", False) or due
            due = False
        elif getattr(self, "kheld", False):
            due, self.kheld = True, False
        if due:
            kop = keyframe_ops(surf, self.g)
            try:
                # 65,535 is the table's length word; an ADPCM4 file's
                # write() appends the reference byte (98.1.1.1) to every
                # key, so its records must leave that byte room - and a
                # key with no room for its lead (98.1.8) is write()'s to
                # leave out, the lead being undecided until then. A key
                # past one read is KEY PARTS (98.1.3.8), each one read,
                # unless `kpart` is None (a resident file's, whose keys
                # are in its block)
                ref = int(self.audio_fmt == AUD_ADPCM4)
                klim = 65535 - ref
                if self.page and hsurf is None:
                    raise V88Error("frame %d: a page-ahead key without "
                                   "its hidden page" % k)
                # the shown page's key, onto both pages, and the hidden
                # page's difference from it after its lists
                krec = key_record(kop, self.g, delta_ops(hsurf, surf, self.g)
                                  if self.page else None, self.page, ref,
                                  self.kpart)
                if key_reads(krec)[0] > klim + ref:
                    raise V88Error("a key past its length word")
                self.keys.append((k, krec, self.g.canvas(surf)))
            except V88Error:
                # a VGA8 canvas past its record's length word (a 320 x
                # 240 MODEX one can be) has no keyframe here: the file
                # still plays from the start, and seeks to the ones it has
                self.skipped = getattr(self, "skipped", 0) + 1

    def _mframe(self, ops, surf, audio, hidden, flip, hsurfs, shown,
                built=False, copies=()):
        """frame() on N pages (98.1.3.7.1), and flipped (98.1.3.7.2)"""
        if len(audio) != self.abytes:
            raise V88Error("frame %d has %d audio bytes, not %d"
                           % (len(self.recs), len(audio), self.abytes))
        k = len(self.recs)
        if k >= FRAMES_MAX:
            raise V88Error("frame %d: a file is %d frames at most"
                           % (k, FRAMES_MAX))
        if flip is False:
            flip = None
        if self.fpage and flip is None:
            raise V88Error("frame %d does not flip, in a file flipped and "
                           "built ahead (98.1.3.7.2)" % k)
        if (built or copies) and not self.fpage:
            raise V88Error("frame %d: built ahead or a copy, in a file not "
                           "flipped and built ahead" % k)
        if any(d == shown or not 0 <= src < self.pages or
               not 0 <= d < self.pages for d, src, c0, c1 in copies or ()):
            raise V88Error("frame %d: a copy onto page %r with page %d "
                           "shown" % (k, [c[0] for c in copies], shown))
        last = getattr(self, "mshown", 0)
        if not 0 <= shown < self.pages or \
                (flip is None and shown != last) or \
                (flip is not None and (flip != shown or flip == last)):
            raise V88Error("frame %d: page %d shown after page %d, flip %r"
                           % (k, shown, last, flip))
        if any(p == shown or not 0 <= p < self.pages
               for p, h in hidden or ()):
            raise V88Error("frame %d: a hidden record on page %r with page "
                           "%d shown" % (k, [p for p, h in hidden], shown))
        self.mshown = shown
        lim = self.spcap * SECTOR - 4
        rec = self.with_runs(mpage_record(ops, hidden, self.g, flip,
                                          limit=lim, built=built,
                                          copies=copies), audio, limit=lim)
        self.recs.append(rec)
        self.last = surf
        if k == self.loop:              # (a gate's: no writer loops one)
            self.loop_surf, self.loop_audio = bytes(surf), audio
        if k >= self.key0 and (k - self.key0) % self.keyint == 0:
            if hsurfs is None or shown not in hsurfs:
                raise V88Error("frame %d: an N-page key without its pages"
                               % k)
            try:
                ref = int(self.audio_fmt == AUD_ADPCM4)
                klim = 65535 - ref
                # the shown page's key, onto every page, and each other
                # page's difference from it after its lists
                # (a page left out of `hsurfs` holds the key's own picture
                # after a seek: the caller's to treat as unknown, 98.1.3.7.1)
                hs = [(p, delta_ops(hsurfs[p], surf, self.g))
                      for p in sorted(hsurfs) if p != shown and
                      self.g.canvas(hsurfs[p]) != self.g.canvas(surf)]
                krec = key_record(keyframe_ops(surf, self.g), self.g, hs,
                                  True, ref, self.kpart, kpage=shown)
                if key_reads(krec)[0] > klim + ref:
                    raise V88Error("a key past its length word")
                self.keys.append((k, krec, self.g.canvas(surf)))
                # (THE PAGE POOL's, 98.1.3.8.1: the pages it carries, by
                # picture, for write() to share between keys)
                self.kpg[k] = (shown, {p: bytes(self.g.canvas(hsurfs[p]))
                                       for p, h in hs})
            except V88Error:
                self.skipped = getattr(self, "skipped", 0) + 1

    def with_runs(self, rec, audio=b"", limit=SP_MAX * SECTOR - 4):
        """A record made without its audio: its runs after the lists if the
        file is live, then the audio"""
        if self.live is not None:
            rec = with_runs(rec, self.g)
        n = len(rec) + len(audio)
        if n > limit:
            raise V88Error("a record of %d bytes cannot fit %s" % (
                n, "a super-packet" if limit < 65534 else "its length word"))
        out = bytearray(rec + audio)
        struct.pack_into("<H", out, 0, n)
        return bytes(out)

    def pool_keys(self):
        """THE PAGE POOL (98.1.3.8.1): a page picture two keys or more carry
        stored once, and each such key made again naming it - placeholders
        for where it lands, which write() puts in (`krefs`). Not on a file
        whose keys are one whole read (a page-ahead lap's, kpart None)"""
        self.pool, self.krefs = [], {}
        if not (self.mpage and getattr(self, "kpool", False) and
                self.kpart is not None and len(self.keys) > 1):
            return
        import hashlib
        g = self.g
        cnt = {}
        for k, (shown, pcs) in self.kpg.items():
            for cv in pcs.values():
                hv = hashlib.md5(cv).digest()
                cnt[hv] = cnt.get(hv, 0) + 1
        pidx, out = {}, []
        ref = int(self.audio_fmt == AUD_ADPCM4)
        for k, krec, c in self.keys:
            shown, pcs = self.kpg[k]
            share = [p for p, cv in pcs.items()
                     if cnt[hashlib.md5(cv).digest()] > 1]
            if not share:
                out.append((k, krec, c))
                continue
            surf = g.surface()
            g.put(surf, c)
            hs, refs = [], []
            for p in sorted(pcs):
                ps = g.surface()
                g.put(ps, pcs[p])
                if p in share:
                    hv = hashlib.md5(pcs[p]).digest()
                    if hv not in pidx:
                        pidx[hv] = len(self.pool)
                        self.pool.append(key_record(keyframe_ops(ps, g), g,
                                                    part=self.kpart))
                    refs.append((p, pidx[hv]))
                else:
                    hs.append((p, delta_ops(ps, surf, g)))
            kr = key_record(keyframe_ops(surf, g), g, hs, True, ref,
                            self.kpart - REF_LEN * len(refs), kpage=shown)
            kr, at = key_refs(kr, [(p, 0, 0) for p, i in refs], g)
            self.krefs[k] = [(a, i) for a, (p, i) in zip(at, refs)]
            out.append((k, kr, c))
        self.keys = out

    def keep_keys(self, keys, lead, poster):
        """WHICH KEYS THE TABLE KEEPS (98.1.1, 98.1.8): `keys` with each
        entry's `lead` bytes of sound ahead behind it. The FIRST key is
        where a colour play starts, so it is kept whatever it costs - past
        `kcap`, the player's one read, the file then plays from the start
        and does not seek - and if it cannot be stored at all, past the
        entry's length word, NO key is: a later one would start the play
        there and skip the frames before it. Every other key past `kcap`
        or the word is left out, and a seek there lands on the one before.
        The poster follows its key to the nearest one kept. Sets
        `kdropped`, (frame, entry bytes) a key left out, and `kleadcost`,
        whether the lead cost a seek the file had without it - which Auto
        (98.2.1.3) answers by writing it in step"""
        cap = min(65535, self.kcap)
        # (a key IN PARTS, 98.1.3.8, is judged by its largest read - and
        # its first is the entry's length word)
        rd = lambda r, lb: max(key_reads(r)) + lb
        r0 = lambda r, lb: key_reads(r)[0] + lb

        def table(lb):
            if not keys or keys[0][0] != self.key0 or \
                    r0(keys[0][1], lb) > 65535:
                return []
            return [keys[0]] + [kk for kk in keys[1:]
                                if rd(kk[1], lb) <= cap and
                                r0(kk[1], lb) <= 65535]

        def seeks(ks, lb):
            return len(ks) if ks and rd(ks[0][1], lb) <= self.kcap else 0
        kept = table(lead)
        left = set(kk[0] for kk in kept)
        self.kdropped = [(k, rd(r, lead)) for k, r, c in keys
                         if k not in left]
        self.kleadcost = bool(lead) and seeks(kept, lead) < seeks(table(0), 0)
        if poster is not None and 0 <= poster < len(self.keys) and \
                len(kept) != len(keys):
            pk = self.keys[poster][0]
            poster = min(range(len(kept)), key=lambda i: abs(
                kept[i][0] - pk)) if kept else None
        return kept, poster

    def write_layer(self, out, keys):
        """THE LAYER (98.1.9, 98.1.9.8) after the stream: one CONTINUOUS
        run of records, a record for every frame, end to end from a 32 KB
        boundary of the file - then the table of where each kept key's next
        frame's record is in it - and the header's words for them"""
        lr = self.lrecs
        if len(lr) != len(self.recs) or self.layer is None:
            raise V88Error("a layer of %d records for %d frames"
                           % (len(lr), len(self.recs)))
        if self.live is not None or self.flip or self.mpage:
            raise V88Error("a layer is a stream's, played unflipped in the "
                           "bracket: not Live, not flipped, not on N pages "
                           "(98.1.9, 98.1.3.7.1)")
        # ON A 32 KB BOUNDARY (98.1.9): the player reads it in 32 KB chunks
        # at a cluster multiple, and the file cannot know the cluster of the
        # disk it will be played from - 32 KB is one of every FAT volume's
        # up to 32 KB clusters, and a player's slot. Records run on across
        # the chunks with no padding between them: 98.1.9.8
        out += bytes(-len(out) % SLOT)
        l0 = len(out)
        where = []
        for r in lr:
            where.append(len(out) - l0)
            out += r
        out += bytes(-len(out) % SECTOR)
        kat = 0
        if keys:
            kat = len(out)
            kt = bytearray()
            for k, r, c in keys:
                kt += struct.pack("<II", where[k + 1] if k + 1 < len(lr)
                                  else 0xFFFFFFFF, 0)
            out += kt + bytes(-len(kt) % SECTOR)
        ly = tuple(self.layer) + (0,) * (6 - len(self.layer))
        kbs, ram, pre, bank, tl, tb = ly[:6]
        struct.pack_into(LAYER_FMT, out, H_LAYER, l0, L_CONT, kbs, ram,
                         pre, kat, max(len(r) for r in lr), bank, tl, tb)
        out[H_LYORD] = 1                # (98.1.9.1: read before the bank)
        struct.pack_into("<H", out, H_LYNEED,
                         min(0xFFE0, getattr(self, "lyneed", 0)))
        struct.pack_into("<H", out, H_LYBKS,
                         min(0xFFFF, getattr(self, "lybks", 0)))
        struct.pack_into("<H", out, H_LYIDL,
                         min(0xFFFF, getattr(self, "lyidl", 0)))
        return bytes(out), dict(bytes=len(out) - l0, stream=sum(map(len, lr)),
                                recs=sum(r != L_EMPTY for r in lr))


    def write(self, path, poster=None):
        g = self.g
        if not self.recs:
            raise V88Error("no frames")
        # the stream: greedy super-packets, then chained
        sps, where = [], []          # where[f] = (super-packet, index)
        cur, size = [], 4
        # (a key names the record after it by its index in the super-packet,
        # a byte: one that would land past 255 starts a super-packet of its
        # own - a stream of small records only, and nothing else moves)
        knext = set(k + 1 for k, r, c in self.keys)
        for f_, r in enumerate(self.recs):
            if cur and (size + len(r) > self.spcap * SECTOR or
                        (len(cur) > 255 and f_ in knext)):
                sps.append(cur)
                cur, size = [], 4
            where.append((len(sps), len(cur)))
            cur.append(r)
            size += len(r)
        sps.append(cur)
        sps_l = sps
        secs = [-(-(4 + sum(len(r) for r in sp)) // SECTOR) for sp in sps]
        self.pool_keys()
        keys = self.keys
        if self.audio_fmt == AUD_ADPCM4:
            # THE REFERENCE BYTE (98.1.1.1): the sample the decoder holds at
            # frame k+1, where a seek starts the card - and its scale must
            # be 0 there, which only audio_chunks(keys=) arranges
            st = adpcm4_trace(b"".join(r[len(r) - self.abytes:]
                                       for r in self.recs))
            keys = []
            for k, r, c in self.keys:
                ref, sc = st[(k + 1) * self.abytes]
                if sc:
                    raise V88Error("the ADPCM4 stream's scale is %d at frame "
                                   "%d, where keyframe %d's seek starts; "
                                   "encode it with audio_chunks(keys=)"
                                   % (sc, k + 1, k))
                if len(r) + 1 > 65535:
                    raise V88Error("keyframe %d is %d bytes, and its ADPCM4 "
                                   "reference byte takes it past its length "
                                   "word" % (k, len(r)))
                keys.append((k, r + bytes([ref]), c))
        recs, A, ab, nfr = self.recs, self.ahead, self.abytes, len(self.recs)
        leads, lead0 = {}, b""
        # THE KEYS' LEADS APART (98.1.8.1): a table of their own, so a key
        # is judged by its picture alone - getattr, so a Writer made before
        # the attribute (a test's) writes them apart too
        apart = bool(A) and getattr(self, "kleads", KLEADS_APART)
        kparts = any(len(key_reads(r)) > 1 for k, r, c in keys) or \
            bool(self.pool)             # (98.1.3.8.1: a pool's file reads
                                        # its keys as KPARTS's do)
        if kparts and A:
            apart = True        # (98.1.3.8: a key in parts has its lead
                                # apart - its entry is its first read)
        keys, poster = self.keep_keys(keys, 0 if apart else A * ab, poster)
        if A:
            # SOUND AHEAD (98.1.8): record r carries the sound of the frame
            # A places on in PLAY ORDER - past the last frame, the lap's: a
            # seam stands for frame L and a key's join for its own frame J,
            # and either goes on from there, so the last A records carry
            # what the frames after a join will want, whether the file asked
            # for Repeat or the user presses the button
            aud = [r[len(r) - ab:] for r in self.recs]
            J = self.loop if self.loop is not None else \
                (keys[0][0] if keys else None)

            def sound(q):
                if q < nfr:
                    return aud[q]
                if J is None:
                    return bytes([0 if self.audio_fmt == AUD_ADPCM4
                                  else 0x80]) * ab
                return aud[J + (q - nfr) % (nfr - J)]
            recs = []
            for p, r in enumerate(self.recs):
                n = bytearray(r[:len(r) - ab] + sound(p + A))
                recs.append(bytes(n))
            lead0 = b"".join(sound(q) for q in range(A))
            for k, r, c in keys:
                leads[k] = b"".join(sound(q) for q in range(k + 1, k + 1 + A))
        seam_audio = self.loop_audio
        if A and self.loop is not None:
            seam_audio = sound(nfr + A)     # the seam is one more record
        seam = b""
        if self.loop is not None:
            # THE SEAM (98.1.1.2): the change from the last frame back to
            # frame L, carrying frame L's audio - it stands in for frame L
            # on every lap after the first, and rides beside the keyframes
            # rather than in the chain, so a play that does not repeat
            # never reads it
            if not 0 <= self.loop < len(self.recs) - 1:
                raise V88Error("a loop from frame %d: it must start before "
                               "the last of %d frames"
                               % (self.loop, len(self.recs)))
            seam = self.with_runs(record(seam_ops(self.last, self.loop_surf,
                                                  g), g, limit=65535),
                                  seam_audio, limit=65535)
            if self.flip and len(seam) > PREV_MAX and self.spcap == SP_MAX:
                raise V88Error("the seam is %d bytes, past a flipped play's "
                               "%d: a bigger super-packet takes it "
                               "(os88venc --frame-cap 48, 98.1.4.1)"
                               % (len(seam), PREV_MAX))
        nk = len(keys)
        if nk > KEYS_MAX:
            raise V88Error("%d keyframes: %d at most" % (nk, KEYS_MAX))
        ktab = -(-16 * nk // SECTOR) * SECTOR if nk else 0
        krec = sum(len(r) + len(leads.get(k, b"")) for k, r, c in keys) + \
            len(seam) + len(lead0) + \
            sum(len(r) for r in self.pool)    # (apart or not, the same bytes)
        pal = SECTOR if self.palette else 0     # the palette: sector 1
        # A PALETTE A SCENE (98.1.3.2.3): its table after the palette's two
        # sectors, before the keyframe table
        pevb = b""
        if self.pev:
            pv = self.pev
            if self.pixfmt != PF_VGA4 or not self.palette or \
                    self.live is not None:
                raise V88Error("a palette a scene is a full-screen VGA4 "
                               "file's with its own sixteen - not Live "
                               "(98.1.3.2.3)")
            if not 1 <= len(pv) <= PEV_MAX or pv[0][0] != 0 or \
                    bytes(pv[0][1]) != self.palette[:48] or \
                    any(b[0] <= a[0] for a, b in zip(pv, pv[1:])) or \
                    pv[-1][0] >= len(self.recs) or \
                    any(len(p) != 48 or max(p) > 63 for _, p in pv):
                raise V88Error("a palette table of %d entries: 1 to %d, "
                               "frame 0's the header's, frames ascending "
                               "and in the file, 16 colours of 0..63"
                               % (len(pv), PEV_MAX))
            pevb = b"".join(struct.pack("<I", f) + bytes(p) for f, p in pv)
        ptab = -(-len(pevb) // SECTOR) * SECTOR
        pev_at = SECTOR + 2 * SECTOR if pevb else 0
        kbase = SECTOR + (2 * SECTOR if self.palette else 0) + ptab + ktab
        ktoff = SECTOR + (2 * SECTOR if self.palette else 0) + ptab
        # the OPTIONS block (98.1.1.4) in sectors of its own between the
        # keyframe records and the stream: behind every offset the player
        # reads and in front of the stream it reads in sequence, so no read
        # of the player's ever reaches it
        opts = self.opts or b""
        oat = kbase + -(-krec // SECTOR) * SECTOR
        s0 = oat + -(-len(opts) // SECTOR) * SECTOR
        spoff, o = [], s0
        for n in secs:
            spoff.append(o)
            o += n * SECTOR
        if A:
            sps, it = [], iter(recs)        # the same lengths, shifted sound
            for sp in sps_l:
                sps.append([next(it) for _ in sp])
        stream = bytearray()
        for i, sp in enumerate(sps):
            nxt = secs[i + 1] if i + 1 < len(sps) else 0
            b = struct.pack("<HH", len(sp), nxt) + b"".join(sp)
            stream += b + bytes(secs[i] * SECTOR - len(b))
        kt, kr, o = bytearray(), bytearray(), kbase
        kpatch = []
        for k, r, c in keys:
            if k + 1 < len(self.recs):
                spi, idx = where[k + 1]
                sp_at, sp_n = spoff[spi], secs[spi]
            else:
                sp_at, sp_n, idx = 0, 0, 0
            if idx > 255:
                raise V88Error("frame %d is record %d of its super-packet; "
                               "a keyframe can name 255" % (k + 1, idx))
            r2 = r if apart else r + leads.get(k, b"")  # (98.1.8: its lead
                                                        # behind it)
            kt += struct.pack("<IIHIBB", k, o, key_reads(r2)[0], sp_at,
                              sp_n, idx)      # (98.1.3.8: its first read)
            for a, pi in self.krefs.get(k, ()):     # (the pool's names,
                kpatch.append((len(kr) + a, pi))    # filled in below)
            kr += r2
            o += len(r2)
        loopblk = b""
        if seam:
            spi, idx = where[self.loop + 1]
            if idx > 255:
                raise V88Error("frame %d is record %d of its super-packet; "
                               "the loop block can name 255"
                               % (self.loop + 1, idx))
            loopblk = struct.pack(LOOP_FMT, self.loop, o, len(seam),
                                  secs[spi], idx, spoff[spi])
            kr += seam
            o += len(seam)
        lead0_at = 0
        if A:
            lead0_at = o
            kr += lead0
            o += len(lead0)
        klead_at = 0
        if apart and keys:
            klead_at = o
            for k, r, c in keys:
                kr += leads[k]
                o += len(leads[k])
        if self.pool:                   # THE PAGE POOL (98.1.3.8.1), and
            pat = []                    # every key's names of it
            for r in self.pool:
                pat.append((o, key_reads(r)[0]))
                kr += r
                o += len(r)
            for at, pi in kpatch:
                struct.pack_into("<IH", kr, at, *pat[pi])
        if poster is None:
            poster = next((i for i, (k, r, c) in enumerate(keys)
                           if not flat(c)), 0 if nk else 0xFFFF)
        elif not 0 <= poster < nk:
            raise V88Error("--poster %d: there are %d keyframes" % (poster, nk))
        hdr = bytearray(SECTOR)
        hdr[0:4] = V88_SIG
        flags = (F_LOOPREC if seam else 0) | (F_REPEAT if self.repeat else 0) \
            | (F_SPKPWM if self.spk else 0) \
            | (F_SPKMUL if self.spkp > 1 else 0) \
            | (F_LIVE | F_RUNS if self.live is not None else 0) \
            | (F_AHEAD if A else 0) \
            | (F_KLEADS if klead_at else 0) \
            | (F_PALEV if pevb else 0) \
            | (F_SUB2 if self.g.sub2 else 0) \
            | (F_PAGE if self.page else 0) \
            | (F_KPARTS if kparts or self.pool else 0) \
            | (F_KPOOL if self.pool else 0) \
            | (F_SCREEN if self.screen else 0) \
            | (F_BIGSP if max(secs) > SP_MAX or (self.flip and max(
                len(r) for r in self.recs + [seam]) > PREV_MAX) else 0)
        struct.pack_into("<HHIHHBBH", hdr, 4, 1, flags, len(self.recs),
                         self.rate,
                         self.spf, self.audio_fmt, 1, self.abytes)
        struct.pack_into("<HB", hdr, 20, *pit_rate(self.rate, self.spf))
        if self.spkp > 1:
            hdr[H_SPKP] = self.spkp
        hdr[H_RING] = getattr(self, "ring", 0)
        if A:
            hdr[H_AHEAD] = A
            struct.pack_into("<I", hdr, H_LEAD0, lead0_at)
        if klead_at:
            struct.pack_into("<I", hdr, H_KLEADS, klead_at)
        struct.pack_into("<HH", hdr, H_XBANK, min(self.xbank, 0xFFFE),
                         self.xpre)
        struct.pack_into("<H", hdr, H_YBASE, min(65535, self.ybase))
        struct.pack_into("<H", hdr, 192 + R_BENCH, min(65535, self.bench[0]))
        struct.pack_into("<H", hdr, H_BENCHL, min(65535, self.bench[1]))
        for off, size, text in ((32, 48, self.title), (80, 96, self.credits)):
            t = text.encode("ascii", "replace")[:size - 1]
            hdr[off:off + len(t)] = t
        struct.pack_into("<BBHHBBIHHIHHIHH", hdr, 192, self.pixfmt,
                         g.layout, g.wb, g.h, self.aspect[0], self.aspect[1],
                         ktoff if nk else 0, nk, poster, s0, secs[0],
                         max(secs), len(stream),
                         max(len(r) for r in self.recs),
                         max((max(key_reads(r)) + (0 if apart else
                                                   len(leads.get(k, b"")))
                              for k, r, c in keys), default=0))
        struct.pack_into("<IBBBB", hdr, 224, pal,
                         self.rowscale if self.rowscale > 1 else 0,
                         (FLIP_HELD if getattr(self, "held", False) else 2)
                         if self.flip else FLIP_FPAGE if self.fpage else
                         FLIP_MPAGE if self.mpage else
                         FLIP_PAGE if self.page else 0,
                         self.screen, self.pages if self.mpage else 0)
        if self.cgapal is not None:
            hdr[192 + R_CGAPAL] = self.cgapal
        if self.live is not None:
            hdr[192 + R_TARGET] = self.live
        if pevb:
            struct.pack_into("<IH", hdr, 192 + R_PEV, pev_at, len(self.pev))
        hdr[LOOP_AT:LOOP_AT + len(loopblk)] = loopblk
        if opts:
            struct.pack_into("<IH", hdr, H_OPTS, oat, len(opts))
        front = bytes(hdr)
        if self.palette:
            front += self.palette + bytes(2 * SECTOR - PAL_BYTES)
        front += pevb + bytes(ptab - len(pevb))
        out = front + kt + bytes(ktab - len(kt)) + kr + \
            bytes(oat - kbase - len(kr)) + opts + \
            bytes(s0 - oat - len(opts)) + stream
        lstat = None
        if self.lrecs is not None:
            out, lstat = self.write_layer(bytearray(out), keys)
        write_whole(path, out)
        return dict(bytes=len(out), keys=nk, keybytes=ktab + len(kr),
                    flags=flags, layer=lstat,
                    stream=len(stream), sps=len(sps), poster=poster,
                    seam=len(seam), ahead=A, kdropped=self.kdropped,
                    kleadcost=self.kleadcost, leadbytes=len(lead0) + sum(len(v) for v in
                                               leads.values()))


def blit_cost(rows, nbytes, planes=1):
    """The model's cycles for one blit of `rows` rows of `nbytes` bytes - of
    each of `planes` bit-planes, for a VGA4 shadow (98.3.10.4)"""
    return BLIT_CALL + rows * planes * (BLIT_ROW + nbytes * BLIT_BYTE)


def lists_end(rec, g):
    """The offset past a record's lists - its ten, or a planar record's
    sub-records and the 0 after them"""
    if g.planes == 1:
        return walk_lists(bytearray(65536), rec, REC_HDR)
    si = REC_HDR
    while rec[si]:
        si = walk_sub(bytearray(65536), rec, si + 1, g)
    return si + 1


def live_runs(rec, g):
    """98.1.3.4: the rectangles a Live frame record writes, as its BLIT RUNS
    - rows [y, y + n) at bytes [x, x + w) - each row's written bytes, and a
    run grown down onto the next written row when one blit of the union
    costs the model less than two. Every write lies inside a run"""
    rows = {}

    def write(k, di, m):
        for a in range(di, di + m):
            y = g.rowof[a]
            x = a - g.base[y]
            lo, hi = rows.get(y, (x, x + 1))
            rows[y] = (min(lo, x), max(hi, x + 1))
    if g.planes == 1:
        walk_lists(bytearray(65536), rec, REC_HDR, write)
    else:                       # VGA4 (98.1.3.2): every sub-record's writes,
        si = REC_HDR            # a byte column the four planes share
        while rec[si]:
            si = walk_sub(bytearray(65536), rec, si + 1, g, write)
    pl = g.planes
    runs = []
    for y in sorted(rows):
        lo, hi = rows[y]
        if runs:
            a, b, rlo, rhi = runs[-1]
            ml, mh = min(lo, rlo), max(hi, rhi)
            if blit_cost(y + 1 - a, mh - ml, pl) <= \
                    blit_cost(b - a, rhi - rlo, pl) + blit_cost(1, hi - lo, pl):
                runs[-1] = (a, y + 1, ml, mh)
                continue
        runs.append((y, y + 1, lo, hi))
    while len(runs) > RUNS_MAX:         # the cheapest pair merged, again
        best = None
        for i in range(len(runs) - 1):
            a, b, l0, h0 = runs[i]
            c, d, l1, h1 = runs[i + 1]
            m = blit_cost(d - a, max(h0, h1) - min(l0, l1), pl) - \
                blit_cost(b - a, h0 - l0, pl) - \
                blit_cost(d - c, h1 - l1, pl)
            if best is None or m < best[0]:
                best = (m, i)
        i = best[1]
        a, b, l0, h0 = runs[i]
        c, d, l1, h1 = runs.pop(i + 1)
        runs[i] = (a, d, min(l0, l1), max(h0, h1))
    return runs


def with_runs(rec, g):
    """A Live frame record with its runs after its lists (it has no audio:
    a resident file's sound is a block of its own)"""
    runs = live_runs(rec, g)
    out = bytearray(rec)
    out.append(len(runs))
    for a, b, lo, hi in runs:
        out += bytes([a, b - a, lo, hi - lo])
    struct.pack_into("<H", out, 0, len(out))
    return bytes(out)


def runs_of(rec, end, g):
    """(runs, the offset past them) of a record whose lists end at `end` -
    each checked against the canvas (98.1.6)"""
    if end >= len(rec):
        raise V88Error("a record's runs are missing")
    n = rec[end]
    if n > RUNS_MAX:
        raise V88Error("%d blit runs: %d at most" % (n, RUNS_MAX))
    if end + 1 + 4 * n > len(rec):
        raise V88Error("a record's runs run off its end")
    runs = []
    for i in range(n):
        y, ny, x, nx = rec[end + 1 + 4 * i:end + 5 + 4 * i]
        if not ny or not nx or y + ny > g.h or x + nx > g.wb:
            raise V88Error("a blit run %d+%d x %d+%d outside the %d x %d "
                           "canvas" % (y, ny, x, nx, g.wb, g.h))
        runs.append((y, y + ny, x, x + nx))
    return runs, end + 1 + 4 * n


def write_resident(path, writers, audio_fmt=AUD_NONE, abytes=0, audio=b"",
                   title="", credits="", repeat=False, pack=PK_LZB,
                   posters=None, live=None, targets=None, spk=False,
                   opts=None):
    """A RESIDENT file (98.1.7): one rendition per Writer - each made SILENT
    at the file's rate, with the file's loop if it has one - its records one
    block, packed on its own; the sound one audio block for them all. The
    player reads the rendition native to its screen whole, expands it in
    place, and plays it from memory. Returns the byte counts, per block"""
    import os88lz
    if not 1 <= len(writers) <= 4:
        raise V88Error("%d renditions: 1 to 4" % len(writers))
    w0 = writers[0]
    n = len(w0.recs)
    for w in writers:
        if (w.rate, w.spf, len(w.recs), w.loop) != (w0.rate, w0.spf, n,
                                                    w0.loop):
            raise V88Error("the renditions differ in rate, frames or loop")
        if w.abytes:
            raise V88Error("a resident rendition's records carry no audio")
        if w.pixfmt == PF_VGA4 and w.palette:
            raise V88Error("a VGA4 file's own sixteen play full screen, and "
                           "a resident file plays on the desktop "
                           "(98.1.3.2.2)")
    if len(audio) != n * abytes:
        raise V88Error("%d bytes of sound for %d frames of %d"
                       % (len(audio), n, abytes))
    loop = w0.loop
    if live is not None and targets is not None:
        raise V88Error("a live file's targets are its `live`")
    if targets is not None:
        # 98.1.5: a resident file may name every rendition's screen without
        # being live - the player prefers the one drawn for its screen
        if len(targets) != len(writers) or \
                any(t not in TARGETS.values() for t in targets):
            raise V88Error("a target of 1 to 3 for every rendition")
    if live is not None:
        # 98.3.10: one target a rendition, and each a LIN80 canvas - the
        # shadow the worker blits is laid out as the band GFX_BLIT1 takes,
        # or for VGA4 (98.3.10.4) as the four planes GFX_BLITP takes
        if len(live) != len(writers):
            raise V88Error("a live file names a target for every rendition")
        for w, t in zip(writers, live):
            if w.g.layout != LAY_LIN80 or t not in TARGETS.values() or \
                    w.pixfmt not in (PF_MONO1, PF_VGA4) or \
                    (w.pixfmt == PF_VGA4 and t != TARGETS["vga"]):
                raise V88Error("a live rendition is a MONO1 LIN80 canvas "
                               "with a target of 1 to 3, or a VGA4 one "
                               "for VGA")

    def packed(data):
        """(bytes, packing): stored when packing would not make it smaller,
        or when the block is past what a PACKED block may be (98.1.7.1) -
        a stored one is any size under 1 MB"""
        if len(data) > BLK_STORED_MAX:
            raise V88Error("a block of %d bytes: a resident block is under "
                           "1 MB" % len(data))
        if pack == PK_NONE or len(data) > BLK_UNPACKED_MAX:
            return data, PK_NONE
        try:
            p = os88lz.compress(data, pack - 1)
        except ValueError:              # (incompressible: no tail fits T)
            return data, PK_NONE
        return (p, pack) if len(p) < len(data) and \
            len(p) <= BLK_PACKED_MAX else (data, PK_NONE)

    def pad(b):
        return b + bytes(-len(b) % SECTOR)
    hdr = bytearray(SECTOR)
    body = bytearray()                  # everything after the header
    base = SECTOR
    slots, sizes = [], []
    for ri, w in enumerate(writers):
        g = w.g
        recs = w.recs
        if live is not None:            # 98.1.3.4: every frame record, and
            if g.h > 255 or g.wb > 255:     # the seam, with its blit runs
                raise V88Error("a live canvas is at most 255 rows and bytes")
            recs = [with_runs(r, g) for r in recs]
        pal = 0
        if w.palette:
            pal = base + len(body)
            body += pad(w.palette)
        seam = b""
        if loop is not None:
            if not 0 <= loop < n - 1:
                raise V88Error("a loop from frame %d of %d" % (loop, n))
            seam = record(seam_ops(w.last, w.loop_surf, g), g, b"",
                          limit=65535)
            if live is not None:
                seam = with_runs(seam, g)
        keys = w.keys
        if any(len(key_reads(r)) > 1 for k, r, c in keys):
            raise V88Error("a resident file's keys are one record each "
                           "(98.1.3.8): its Writer's kpart is None")
        if audio_fmt == AUD_ADPCM4:
            # 98.1.7.2: each key record carries the card's REFERENCE after
            # its lists, as a streamed one does (98.1.1.1) - the sample the
            # decoder holds at frame k+1, its scale steered to 0 there
            st = adpcm4_trace(audio)
            kk = []
            for k, r, c in keys:
                ref, sc = st[(k + 1) * abytes] if k + 1 < n else st[-1]
                if sc:
                    raise V88Error("the ADPCM4 sound's scale is %d at frame "
                                   "%d, where keyframe %d's seek starts; "
                                   "encode it with audio_chunks(keys=)"
                                   % (sc, k + 1, k))
                kk.append((k, r + bytes([ref]), c))
            keys = kk
        nk = len(keys)
        if nk > KEYS_MAX:
            raise V88Error("%d keyframes: %d at most" % (nk, KEYS_MAX))
        ktab = base + len(body) if nk else 0
        kt = bytearray()
        o = base + len(body) + -(-16 * nk // SECTOR) * SECTOR
        kr = bytearray()
        for k, r, c in keys:
            kt += struct.pack("<IIHIBB", k, o + len(kr), len(r), 0, 0, 0)
            kr += r
        if nk:
            body += pad(bytes(kt)) + pad(bytes(kr))
        blk = b"".join(recs) + seam
        pb, bpk = packed(blk)
        boff = base + len(body)
        body += pad(pb)
        poster = posters[ri] if posters else None
        if poster is None:
            poster = next((i for i, (k, r, c) in enumerate(keys)
                           if not flat(c)), 0 if nk else 0xFFFF)
        slots.append((w, ktab, nk, poster, len(blk), boff, len(pb), bpk, pal,
                      max(len(r) for r in recs),
                      max((len(r) for k, r, c in keys), default=0)))
        sizes.append((len(blk), len(pb)))
    aoff = apk = 0
    if audio:
        apk, apack = packed(audio)
        aoff = base + len(body)
        body += pad(apk)
    hdr[0:4] = V88_SIG
    flags = F_RESIDENT | (F_LOOPREC if loop is not None else 0) | \
        (F_REPEAT if repeat else 0) | \
        (F_LIVE | F_RUNS if live is not None else 0) | \
        (F_SPKPWM if spk else 0) | \
        (F_SUB2 if any(w.g.sub2 for w in writers) else 0)
    if len({w.g.sub2 for w in writers if w.g.planes > 1}) > 1:
        raise V88Error("planar renditions with and without SUB2's maps: "
                       "the flag is the file's (98.1.3.2.4)")
    if spk and audio_fmt != AUD_PCM8:
        raise V88Error("speaker counts are PCM8's (98.1.1.3)")
    struct.pack_into("<HHIHHBBH", hdr, 4, 1, flags, n, w0.rate, w0.spf,
                     audio_fmt, len(writers), abytes)
    struct.pack_into("<HB", hdr, 20, *pit_rate(w0.rate, w0.spf))
    for off, size, text in ((32, 48, title), (80, 96, credits)):
        t = text.encode("ascii", "replace")[:size - 1]
        hdr[off:off + len(t)] = t
    if audio:
        struct.pack_into("<IIIB", hdr, AUD_AT, aoff, len(apk), len(audio),
                         apack)
    for ri, (w, ktab, nk, poster, ulen, boff, plen, bpk, pal, rmax,
             kmax) in enumerate(slots):
        so = 192 + 64 * ri
        struct.pack_into("<BBHHBBIHHIHHIHH", hdr, so, w.pixfmt, w.g.layout,
                         w.g.wb, w.g.h, w.aspect[0], w.aspect[1], ktab, nk,
                         poster, 0, 0, 0, ulen, rmax, kmax)
        struct.pack_into("<IBB", hdr, so + 32, pal,
                         w.rowscale if w.rowscale > 1 else 0,
                         2 if w.flip else 0)
        struct.pack_into("<IIIB", hdr, so + R_BLOCK, boff, plen, ulen, bpk)
        if w.cgapal is not None:
            hdr[so + R_CGAPAL] = w.cgapal
        if live is not None or targets is not None:
            hdr[so + R_TARGET] = (live or targets)[ri]
    if loop is not None:
        struct.pack_into("<I", hdr, LOOP_AT, loop)
    if opts:                    # the OPTIONS block (98.1.1.4), last: every
        struct.pack_into("<IH", hdr, H_OPTS,    # block before it is read by
                         SECTOR + len(body), len(opts))   # offset and size
    out = bytes(hdr) + bytes(body) + (opts or b"")
    write_whole(path, out)
    return dict(bytes=len(out), blocks=sizes,
                audio=(len(audio), len(apk) if audio else 0))


def walk_lists(buf, data, si, write=None, seg=None, lmap=None):
    """Apply the ten lists at data[si:] to `buf` (a surface image); call
    write(k, di, n) for each write first and seg(absolute) for each
    segment. Returns the index past the tenth list. A truncated list raises
    V88Error. With `lmap`, only the lists its bits name are there (SUB2)."""
    try:
        for k in range(NLISTS):
            if lmap is not None and not lmap >> k & 1:
                continue
            while True:
                n = data[si]
                si += 1
                if n == 0:
                    break
                if n & 0x80:
                    todo, di, absolute = n & 0x7F, None, True
                else:
                    todo, absolute = n, False
                    di = data[si] | data[si + 1] << 8
                    si += 2
                if seg:
                    seg(absolute)
                for _ in range(todo):
                    if absolute:
                        di = data[si] | data[si + 1] << 8
                        si += 2
                    else:
                        di += data[si]
                        si += 1
                    if k <= L_P6:
                        m = k - L_P1 + 1
                        src = data[si:si + m]
                        si += m
                    else:
                        if k in (L_SLICE, L_RUN):
                            m = data[si]
                            si += 1
                        else:
                            m = data[si] | data[si + 1] << 8
                            si += 2
                        if k in (L_SLICE, L_SLICEL):
                            src = data[si:si + m]
                            si += m
                        else:
                            src = bytes([data[si]]) * m
                            si += 1
                    if len(src) != m or si > len(data):
                        raise IndexError
                    if write:
                        write(k, di, m)
                    if di + m > 65536:
                        raise V88Error("a write at %04x+%d wraps the segment"
                                       % (di, m))
                    buf[di:di + m] = src
                    di += m
    except IndexError:
        raise V88Error("the lists run off the end of their record")
    return si


def walk_sub(buf, data, si, g, write=None, seg=None):
    """One planar sub-record's lists at data[si:], past its lead, in the
    file's grammar: ten lists, or SUB2's map and the lists it names
    (98.1.3.2.4). Returns the index past them"""
    if not g.sub2:
        return walk_lists(buf, data, si, write, seg)
    if si + 2 > len(data):
        raise V88Error("the lists run off the end of their record")
    lmap = data[si] | data[si + 1] << 8
    if not lmap or lmap >> NLISTS:
        raise V88Error("a sub-record's list map %04x" % lmap)
    return walk_lists(buf, data, si + 2, write, seg, lmap)


def subs_of(rec, g):
    """A planar record's sub-records: [(lead, the lists' start, past
    them)] - the lists' start past the lead (and a SUB2 map, which
    walk_sub reads)"""
    out, si = [], REC_HDR
    scratch = bytearray(65536)
    while True:
        if si >= len(rec):
            raise V88Error("the sub-records run off their record")
        lead = rec[si]
        if not lead:
            return out
        end = walk_sub(scratch, rec, si + 1, g)
        out.append((lead, si + 1, end))
        si = end


def wm3_apply(surf, di, masks, colour):
    """Write mode 3 (98.1.3.2.4): the pixels each mask sets take `colour`
    on all four planes of a VGA4 surface image, the rest keep theirs"""
    for i, m in enumerate(masks):
        a = di + i
        for p in range(4):
            o = p * PLANE + a
            surf[o] = (surf[o] & ~m & 0xFF) | (m if colour >> p & 1 else 0)


class Reader:
    """A .V88, read the way SPEC.md 98.1.6 says a player must: every field
    it sizes by is checked, and a failure is a V88Error naming it."""

    def __init__(self, path, rend=0):
        self.path = path
        self.d = d = open(path, "rb").read()
        if len(d) < SECTOR or d[:4] != V88_SIG:
            raise V88Error("%s: not a .V88 (no signature)" % path)
        (ver, flags, self.frames, self.rate, self.spf, self.audio,
         self.nrend, self.abytes) = struct.unpack_from("<HHIHHBBH", d, 4)
        if ver != 1 or flags & ~F_KNOWN:
            raise V88Error("version %d, flags %04x: this reader knows "
                           "version 1 and flags %04x" % (ver, flags, F_KNOWN))
        self.flags = self.flags_ = flags
        if not 1 <= self.frames <= FRAMES_MAX or self.spf < 1 or \
                self.rate < 1:
            raise V88Error("frames %d, rate %d, samples per frame %d"
                           % (self.frames, self.rate, self.spf))
        if not 1 <= self.nrend <= 4:
            raise V88Error("%d renditions" % self.nrend)
        want = {AUD_NONE: 0, AUD_PCM8: self.spf}
        if self.spf % 2 == 0:
            want[AUD_ADPCM4] = self.spf // 2
        if self.audio not in want:
            raise V88Error("audio format %d is not a version 1 one"
                           % self.audio)
        if self.abytes != want[self.audio]:
            raise V88Error("%d audio bytes a frame with format %d and %d "
                           "samples" % (self.abytes, self.audio, self.spf))
        # THE RING THE STREAM ASSUMES (98.1.1): the slots of read-ahead
        # its bursts are banked in, 2 to the player's most; 0 says
        # nothing, and a RESIDENT file has no ring
        self.ring = d[H_RING]
        if self.ring == 1 or self.ring > RING_MAX or \
                (self.ring and flags & F_RESIDENT):
            raise V88Error("a ring of %d slots%s" % (
                self.ring, " in a RESIDENT file" if self.ring else ""))
        self.pitdiv, self.pitper = struct.unpack_from("<HB", d, 20)
        if (self.pitdiv, self.pitper) != pit_rate(self.rate, self.spf):
            raise V88Error("PIT divisor %d x %d for %d Hz / %d; it should be "
                           "%d x %d" % ((self.pitdiv, self.pitper, self.rate,
                                         self.spf) +
                                        pit_rate(self.rate, self.spf)))
        self.title = d[32:80].split(b"\0")[0].decode("ascii", "replace")
        self.credits = d[80:176].split(b"\0")[0].decode("ascii", "replace")
        # where the options block is (98.1.1.4): read only by options(), so
        # a file whose block is damaged still opens, plays and previews
        self.optsat, self.optslen = struct.unpack_from("<IH", d, H_OPTS)
        (self.pixfmt, layout, wb, h, an, ad, self.ktab, self.nkeys,
         self.poster, self.sp0, self.sp0n, self.spmax, self.slen, self.rmax,
         self.kmax) = struct.unpack_from("<BBHHBBIHHIHHIHH", d, 192 + 64 * rend)
        if not 0 <= rend < self.nrend:
            raise V88Error("rendition %d of %d" % (rend, self.nrend))
        self.rend, self.slot = rend, 192 + 64 * rend
        self.resident = bool(flags & F_RESIDENT)
        self.live = bool(flags & F_LIVE)
        self.runs = bool(flags & F_RUNS)
        self.spk = bool(flags & F_SPKPWM)
        if self.spk and self.audio != AUD_PCM8:
            raise V88Error("speaker counts in a file whose audio is not "
                           "PCM8 (98.1.1.3)")
        self.spkp = d[H_SPKP] if flags & F_SPKMUL else 1
        if flags & F_SPKMUL and (not self.spk or not 2 <= self.spkp <= 4):
            raise V88Error("%d pulses a sample (98.1.1.3.1)" % self.spkp)
        if not flags & F_SPKMUL and d[H_SPKP]:
            raise V88Error("a pulses byte with no SPKMUL flag")
        if self.spk:
            spk_table(self.rate, self.spkp)
        if self.runs and not self.live:
            raise V88Error("blit runs in a file that is not LIVE (98.1.3.4)")
        self.target = d[self.slot + R_TARGET]
        # a LIVE file is RESIDENT, or a stream played Live once it is held
        # in XMS (98.3.18.1)
        if self.target > 3 or (self.target and not self.resident
                               and not self.live):
            raise V88Error("a target of %d" % self.target)
        if self.pixfmt not in PF_NAMES:
            raise V88Error("pixel format %d" % self.pixfmt)
        if (self.pixfmt == PF_VGA8) != (layout in VGA8_LAYOUTS):
            raise V88Error("pixel format %d on layout %d: VGA8 is LIN320's "
                           "and MODEX's, and only theirs"
                           % (self.pixfmt, layout))
        if self.pixfmt == PF_VGA4 and layout != LAY_LIN80:
            raise V88Error("VGA4 on layout %d: it is LIN80's" % layout)
        if self.pixfmt == PF_CGA4 and layout != LAY_CGA:
            raise V88Error("CGA4 on layout %d: it is CGA's" % layout)
        if (self.pixfmt == PF_C160) != (layout == LAY_C160):
            raise V88Error("pixel format %d on layout %d: C160 is the c160 "
                           "layout's, and only its" % (self.pixfmt, layout))
        if (self.pixfmt == PF_C512) != (layout == LAY_TXT):
            raise V88Error("pixel format %d on layout %d: C512 is the "
                           "text-80x100 layout's, and only its" % (self.pixfmt, layout))
        if (self.pixfmt == PF_TEXT) != (layout == LAY_TEXT):
            raise V88Error("pixel format %d on layout %d: TEXT is the "
                           "text-80x25 layout's, and only its" % (self.pixfmt, layout))
        self.cgapal = d[self.slot + R_CGAPAL]
        if self.pixfmt == PF_C512:
            if self.cgapal not in CARD_NAMES:
                raise V88Error("a C512 card byte of %d" % self.cgapal)
            if wb % 4:
                raise V88Error("a C512 canvas of %d bytes: an even number "
                               "of whole cells" % wb)
        elif self.pixfmt == PF_TEXT:
            if self.cgapal not in (TEXT_MONO, TEXT_COLOUR):
                raise V88Error("a TEXT colour byte of %d" % self.cgapal)
            if wb % 4:
                raise V88Error("a TEXT canvas of %d bytes: an even number "
                               "of whole cells" % wb)
        elif self.pixfmt == PF_CGA4:
            cga4_colours(self.cgapal)
        elif self.cgapal:
            raise V88Error("a CGA palette byte in a %s file"
                           % PF_NAMES[self.pixfmt])
        # (a NARROW screen's rows are 40 bytes apart, 98.1.3.2.5: the screen
        # byte, read here and checked below, says the canvas's geometry)
        nscr = d[self.slot + R_SCREEN]
        self.g = Geom(layout, wb, h, bitplanes=self.pixfmt == PF_VGA4,
                      stride=40 if self.pixfmt == PF_VGA4 and
                      layout == LAY_LIN80 and nscr in NARROW else None)
        if flags & F_SUB2 and self.g.planes > 1:  # 98.1.3.2.4: a planar
            self.g.sub2 = True      # rendition's (a Live file's one-bit
                                    # renditions beside it have no sub-records)
        pal, rs, fl, scr, npg = struct.unpack_from("<IBBBB", d,
                                                   self.slot + 32)
        self.rowscale = rs or 1
        if scr not in SCREENS or (scr and (
                not flags & F_SCREEN or
                not (self.pixfmt == PF_VGA4 or (layout == LAY_MODEX and
                                                scr == SCR_320X200)) or
                self.resident or self.live or
                self.g.w > SCREENS[scr][1] or h > SCREENS[scr][2])):
            raise V88Error("a screen byte of %d (98.1.3.2.1, 98.1.3.1.1)"
                           % scr)
        self.screen = scr
        self.held = fl == FLIP_HELD     # (98.1.3.9: a flipped file, with
        if self.held:                   # HELD records)
            fl = 2
        pg = fl in (FLIP_PAGE, FLIP_MPAGE, FLIP_FPAGE)
        if fl not in (0, 1, 2, FLIP_PAGE, FLIP_MPAGE, FLIP_FPAGE) or (
                fl >= 2 and not (layout == LAY_MODEX or scr or (
                    fl == FLIP_PAGE and layout == LAY_HERC))) or \
                pg != bool(flags & F_PAGE) or \
                (pg and flags & (F_RESIDENT | F_LIVE | F_LOOPREC)):
            raise V88Error("a flip byte of %d on layout %d" % (fl, layout))
        if fl in (2, FLIP_FPAGE) and scr in NARROW:
            raise V88Error("a NARROW screen flipped every frame (98.1.3.2.5)")
        # N PAGES (98.1.3.7.1): byte 39, 3..8 of them in a plane's 64 KB
        if (fl in (FLIP_MPAGE, FLIP_FPAGE)) != bool(npg) or (npg and (
                not 3 <= npg <= MPAGE_MAX or
                npg * page_bytes(layout, scr) > 0x10000)):
            raise V88Error("%d pages with a flip byte of %d (98.1.3.7.1)"
                           % (npg, fl))
        self.flip = fl == 2
        self.page = pg                  # PAGE-AHEAD (98.1.3.7)
        if getattr(self, "kpool", False) and not (
                fl in (FLIP_MPAGE, FLIP_FPAGE) and self.kparts):
            raise V88Error("a page pool in a file not on N pages in parts "
                           "(98.1.3.8.1)")
        self.mpage = fl in (FLIP_MPAGE, FLIP_FPAGE)  # ...ON N PAGES
        self.fpage = fl == FLIP_FPAGE   # (98.1.3.7.1), FLIPPED (98.1.3.7.2)
        self.flast = None               # ...its last record's own, played
        self.pages = npg or 2
        # BIGSP (98.1.4.1): super-packets to SP_BIG sectors, in a stream
        # played in the bracket - never RESIDENT, LIVE or flipped
        self.spcap = SP_BIG if flags & F_BIGSP else SP_MAX
        if flags & F_BIGSP and (self.resident or self.live):
            raise V88Error("super-packets past 32 KB in a resident or live "
                           "file (98.1.4.1)")
        if self.rowscale > 2 or (self.rowscale > 1 and
                                 layout not in VGA8_LAYOUTS):
            raise V88Error("a row scale of %d on layout %d" % (rs, layout))
        if self.rowscale * h > LAYOUTS[layout][2]:
            raise V88Error("%d rows shown %d times do not fit %s"
                           % (h, self.rowscale, LAYOUTS[layout][3]))
        self.palette = None
        if self.pixfmt == PF_VGA8:
            if not pal or pal % SECTOR or pal + PAL_BYTES > len(d):
                raise V88Error("a VGA8 file's palette at %d does not fit"
                               % pal)
            self.palette = d[pal:pal + PAL_BYTES]
            if max(self.palette) > 63:
                raise V88Error("the palette holds values past the DAC's 63")
        elif pal and self.pixfmt == PF_VGA4 and not (self.resident or
                                                     self.live):
            # ITS OWN SIXTEEN, on any screen (98.1.3.2.1, 98.1.3.2.2): a
            # file that carries them plays full screen, never in a window
            if pal % SECTOR or pal + PAL_BYTES > len(d):
                raise V88Error("a VGA4 file's palette at %d does not fit"
                               % pal)
            self.palette = d[pal:pal + PAL_BYTES]
            if max(self.palette[:48]) > 63 or any(self.palette[48:]):
                raise V88Error("a VGA4 palette is 16 entries of 0..63")
        elif pal:
            raise V88Error("a palette at %d in a %s file"
                           % (pal, PF_NAMES[self.pixfmt]))
        # A PALETTE A SCENE (98.1.3.2.3)
        self.pev = None
        pev_at, pev_n = struct.unpack_from("<IH", d, self.slot + R_PEV)
        if flags & F_PALEV:
            if self.pixfmt != PF_VGA4 or not self.palette or \
                    self.resident or self.live:
                raise V88Error("a palette a scene in a file that is not a "
                               "full-screen VGA4 one with its own sixteen "
                               "(98.1.3.2.3)")
            if not 1 <= pev_n <= PEV_MAX or not pev_at or \
                    pev_at % SECTOR or pev_at + pev_n * PEV_LEN > len(d):
                raise V88Error("a palette table of %d entries at %d"
                               % (pev_n, pev_at))
            pv = []
            for i in range(pev_n):
                o = pev_at + i * PEV_LEN
                fr = struct.unpack_from("<I", d, o)[0]
                p = bytes(d[o + 4:o + PEV_LEN])
                if max(p) > 63 or fr >= min(self.frames, 65536) or \
                        (pv and fr <= pv[-1][0]) or (not pv and fr):
                    raise V88Error("palette table entry %d: frame %d" %
                                   (i, fr))
                pv.append((fr, p))
            if pv[0][1] != bytes(self.palette[:48]):
                raise V88Error("the palette table's frame 0 is not the "
                               "header's palette")
            self.pev = pv
        elif pev_at or pev_n:
            raise V88Error("a palette table with no PALEV flag")
        self.aspect = (an, ad)
        if self.resident:
            self._blocks(d)
        elif not (1 <= self.sp0n <= self.spcap and
                  1 <= self.spmax <= self.spcap) or self.sp0 % SECTOR:
            raise V88Error("first super-packet at %d, %d sectors, largest %d"
                           % (self.sp0, self.sp0n, self.spmax))
        if self.nkeys > KEYS_MAX:
            raise V88Error("%d keyframes: %d at most" % (self.nkeys, KEYS_MAX))
        if self.nkeys and (self.ktab % SECTOR or
                           self.ktab + 16 * self.nkeys > len(d)):
            raise V88Error("the keyframe table at %d does not fit" % self.ktab)
        if self.poster != 0xFFFF and self.poster >= self.nkeys:
            raise V88Error("poster %d of %d keyframes"
                           % (self.poster, self.nkeys))
        self.keys = [struct.unpack_from("<IIHIBB", d, self.ktab + 16 * i)
                     for i in range(self.nkeys)]
        self.repeat = bool(flags & F_REPEAT)
        # SOUND AHEAD (98.1.8): A, the start's lead, and the bytes after
        # them in the header's last sector, which are zero
        self.ahead = d[H_AHEAD] if flags & F_AHEAD else 0
        self.lead0at = struct.unpack_from("<I", d, H_LEAD0)[0] \
            if flags & F_AHEAD else 0
        if not flags & F_AHEAD and d[H_AHEAD]:
            raise V88Error("a sound-ahead byte with no AHEAD flag")
        if flags & F_AHEAD and (
                not self.ahead or self.audio == AUD_NONE or self.resident
                or self.live or self.ahead * self.abytes > AHEAD_MAX or
                self.lead0at < SECTOR or
                self.lead0at + self.ahead * self.abytes > len(d)):
            raise V88Error("sound ahead by %d frames, its start's lead at %d"
                           " (98.1.8)" % (self.ahead, self.lead0at))
        # THE KEYS' LEADS APART (98.1.8.1): a table of A x abytes a key,
        # with AHEAD and keys only, inside the file and past the header
        self.kleadat = struct.unpack_from("<I", d, H_KLEADS)[0] \
            if flags & F_KLEADS else 0
        if flags & F_KLEADS and (
                not flags & F_AHEAD or not self.nkeys or
                self.kleadat < SECTOR or self.kleadat + self.nkeys *
                self.ahead * self.abytes > len(d)):
            raise V88Error("the keys' leads apart at %d (98.1.8.1)"
                           % self.kleadat)
        # KEYS IN PARTS (98.1.3.8): never RESIDENT (a resident file's keys
        # are in its block), and a key's lead apart if it has one
        self.kparts = bool(flags & F_KPARTS)
        self.kpool = bool(flags & F_KPOOL)  # (98.1.3.8.1: N pages, parts)
        if self.kparts and (self.resident or self.live or
                            (flags & F_AHEAD and not flags & F_KLEADS)):
            raise V88Error("keys in parts with %s (98.1.3.8)" % (
                "a resident block" if self.resident else "LIVE"
                if self.live else "their leads in their entries"))
        tail0 = H_KLEADS + 4 if flags & F_KLEADS else \
            H_LEAD0 + 4 if flags & F_AHEAD else LOOP_AT + 16
        self.xbank, self.xpre = struct.unpack_from("<HH", d, H_XBANK)
        if self.xpre and not self.xbank or self.xbank and self.resident:
            raise V88Error("a prefill of %d KB with a bank of %d (98.3.18.3)"
                           % (self.xpre, self.xbank))
        # (the bank's two words are the tail's, and not zero)
        dz = d[:H_XBANK] + bytes(4) + d[H_XBANK + 4:]
        # THE LAYER (98.1.9): its words are the tail's too
        (self.lsp0, self.lsp0n, self.lkbs, self.lram, self.lpre, self.lktab,
         self.lmax, self.lbank, self.ltl, self.ltb) = struct.unpack_from(
             LAYER_FMT, d, H_LAYER)
        dz = dz[:H_LAYER] + bytes(LAYER_LEN) + dz[H_LAYER + LAYER_LEN:]
        self.ybase = struct.unpack_from("<H", d, H_YBASE)[0]
        dz = dz[:H_YBASE] + bytes(2) + dz[H_YBASE + 2:]
        # THE BENCH (98.1.9.13): the rendition's word and the header's
        self.bench = (struct.unpack_from("<H", d, self.slot + R_BENCH)[0],
                      struct.unpack_from("<H", d, H_BENCHL)[0])
        dz = dz[:H_BENCHL] + bytes(2) + dz[H_BENCHL + 2:]
        self.lyord = d[H_LYORD]
        self.lyneed = struct.unpack_from("<H", d, H_LYNEED)[0]
        dz = dz[:H_LYORD] + bytes(4) + dz[H_LYORD + 4:]
        self.lybks, self.lyidl = struct.unpack_from("<HH", d, H_LYBKS)
        dz = dz[:H_LYBKS] + bytes(4) + dz[H_LYBKS + 4:]
        if (self.lybks or self.lyidl) and not struct.unpack_from(
                "<I", d, H_LAYER)[0] or self.lyidl and not self.lybks:
            raise V88Error("the disks %d and %d KB/s with no layer, or the "
                           "layer's without the base's (98.1.9.6)"
                           % (self.lybks, self.lyidl))
        if self.lyord > 1 or self.lyord and not struct.unpack_from(
                "<I", d, H_LAYER)[0] or self.lyneed and not self.lyord \
                or self.lyneed % 32 or d[H_LYORD + 1]:
            raise V88Error("a layer-order byte %d with %s (98.1.9.1)"
                           % (self.lyord, "no layer" if self.lyord == 1
                              else "a value past 1"))
        if self.ybase and self.resident:
            raise V88Error("a yardstick in a resident file (98.3.18.9)")
        if self.bench[0] and not bench_ok(self.pixfmt, self.g.layout,
                                          self.resident, self.live) or \
                self.bench[1] and not (self.bench[0] and struct.unpack_from(
                    "<I", d, H_LAYER)[0]):
            raise V88Error("a bench of %d and %d where %s (98.1.9.13)" % (
                self.bench + ("the layer's is with no layer or no base's"
                              if self.bench[1] else
                              "the rendition cannot carry one",)))
        if not self.lsp0:
            if d[H_LAYER:H_LAYER + LAYER_LEN] != bytes(LAYER_LEN):
                raise V88Error("a layer's words with no layer (98.1.9)")
        elif self.lsp0n == SP_MAX:
            raise V88Error("a layer in the padded super-packets of before "
                           "98.1.9.8, which nothing reads any more: "
                           "re-encode it")
        elif (self.resident or self.live or self.mpage or
              self.lsp0 % SLOT or
              self.lsp0 < self.sp0 or self.lsp0n != L_CONT or
              self.lsp0 + 2 > len(d) or not self.lkbs or
              not self.lram or not 2 <= self.lmax <= L_RECMAX or
              (self.lpre != XPRE_ALL and
                          self.lpre > self.lram + self.lbank)
              or (self.nkeys and (not self.lktab or self.lktab % SECTOR or
                                  self.lktab + 8 * self.nkeys > len(d)))
              or (not self.nkeys and self.lktab)):
            raise V88Error("a layer at %d, %d sectors, %d KB/s, %d KB "
                           "(prefill %d), key table %d, longest %d: it does "
                           "not fit the file (98.1.9)"
                           % (self.lsp0, self.lsp0n, self.lkbs, self.lram,
                              self.lpre, self.lktab, self.lmax))
        self.loop = None
        blk = struct.unpack_from(LOOP_FMT, d, LOOP_AT)
        if flags & F_LOOPREC and self.resident:
            # RESIDENT: the seam is each block's record after the last
            # frame's, so the loop block names only L
            if not blk[0] + 1 < self.frames or any(blk[1:]) or \
                    any(dz[tail0:SECTOR]):
                raise V88Error("a resident file's loop block is L alone, "
                               "L + 1 < frames")
            self.loop = (blk[0], 0, len(self._seam), 0, 0, 0)
        elif flags & F_LOOPREC:
            L, off, n, secs, idx, spo = blk
            minrec = REC_HDR + (1 if self.g.planes > 1 else 10) + self.abytes
            if not (L + 1 < self.frames and n >= minrec and
                    off + n <= len(d) and 1 <= secs <= self.spcap and
                    not spo % SECTOR and spo >= self.sp0 and
                    spo + secs * SECTOR <= len(d)):
                raise V88Error("the loop block (from frame %d, seam %d+%d, "
                               "super-packet %d x %d, record %d) does not "
                               "fit the file" % (L, off, n, spo, secs, idx))
            self.loop = blk
        elif any(blk) or any(dz[tail0:SECTOR]):
            raise V88Error("a loop block with no LOOPREC flag")
        if flags & F_LOOPREC and any(dz[tail0:SECTOR]):
            raise V88Error("bytes after the loop block that are not zero")

    @property
    def fps(self):
        return self.rate / self.spf

    def records(self, at=None, nsec=None, skip=0, first=0):
        """(frame record bytes, super-packet offset, index) for every frame
        from super-packet `at` onward, following the chain. RESIDENT: from
        frame `first`, each record with its frame's audio on the end, as a
        streamed one carries it (so apply() is the same for both)"""
        if self.resident:
            for i in range(first, self.frames):
                yield self._recs[i] + self._audio(i), 0, i
            return
        at = self.sp0 if at is None else at
        nsec = self.sp0n if nsec is None else nsec
        while nsec:
            if not 1 <= nsec <= self.spcap:
                raise V88Error("a super-packet of %d sectors" % nsec)
            sp = self.d[at:at + nsec * SECTOR]
            if len(sp) != nsec * SECTOR:
                raise V88Error("the super-packet at %d runs off the file" % at)
            nf, nxt = struct.unpack_from("<HH", sp, 0)
            if nf < 1:
                raise V88Error("the super-packet at %d holds no frames" % at)
            o = 4
            for i in range(nf):
                if o + REC_HDR > len(sp):
                    raise V88Error("record %d of the super-packet at %d runs "
                                   "off it" % (i, at))
                n = struct.unpack_from("<H", sp, o)[0]
                if n < REC_HDR + 1 + self.abytes or o + n > len(sp):
                    raise V88Error("record %d of the super-packet at %d says "
                                   "%d bytes" % (i, at, n))
                if i >= skip:
                    yield sp[o:o + n], at, i
                o += n
            if any(sp[o:]):
                raise V88Error("the super-packet at %d does not end in padding"
                               % at)
            skip = 0
            at, nsec = at + nsec * SECTOR, nxt

    def layer_key(self, i):
        """Where key i's NEXT frame's record is in the layer: its offset
        from the layer's start - 0xFFFFFFFF after the last frame
        (98.1.9.8)"""
        return struct.unpack_from("<II", self.d, self.lktab + 8 * i)[0]

    def layer_records(self, at=0, frame0=0):
        """(frame, its layer record or None, its offset from the layer's
        start) from offset `at` - frame `frame0`'s record - to the last
        frame: one continuous run, a record for every frame (98.1.9.8)"""
        if not self.lsp0:
            return
        end = self.lktab if self.lktab else len(self.d)
        o = self.lsp0 + at
        for f in range(frame0, self.frames):
            if o + 2 > end:
                raise V88Error("layer record %d at %d runs off the layer"
                               % (f, o - self.lsp0))
            n = struct.unpack_from("<H", self.d, o)[0]
            if not (n == 2 or n >= REC_HDR + 1) or o + n > end or \
                    n > self.lmax:
                raise V88Error("layer record %d at %d says %d bytes"
                               % (f, o - self.lsp0, n))
            yield f, None if n == 2 else self.d[o:o + n], o - self.lsp0
            o += n
        if any(self.d[o:end]):
            raise V88Error("the layer does not end in padding at %d"
                           % (o - self.lsp0))

    def apply(self, surf, rec, key=False, check=True, layer=False,
              part=False):
        """Decode one record onto `surf`; with `check`, enforce the WRITER's
        rules too (SPEC.md 98.1.3): canvas-only, no byte written twice,
        rows inside y0..y1, audio exact. A key IN PARTS (98.1.3.8) is each
        part in turn, `part` the ones before the last, which end in nothing"""
        g = self.g
        n, y0, y1 = struct.unpack_from("<HHH", rec, 0)
        if getattr(self, "held", False) and not key:
            y0 &= ~REC_HELD             # (a HELD record's bit, 98.1.3.9)
        if key and y0 & KEY_MORE:
            if not self.kparts:
                raise V88Error("a key in parts in a file without KPARTS")
            ps = key_split(rec)
            for p in ps[:-1]:
                self.apply(surf, p, key=True, check=check, part=True)
            return self.apply(surf, ps[-1], key=True, check=check)
        seens = [bytearray(65536) for _ in range(g.planes)]
        wrote = [0]
        plane = [0]
        writes = []

        def bounds(di, m):
            if not all(g.valid[di:di + m]):
                raise V88Error("a write at %04x+%d leaves the canvas" % (di, m))
            for a in (di, di + m - 1):
                if not y0 <= g.rowof[a] < y1:
                    raise V88Error("a write on row %d is outside the band "
                                   "%d..%d" % (g.rowof[a], y0, y1))

        def write(k, di, m):
            seen = seens[plane[0]]
            bounds(di, m)
            if any(seen[di:di + m]):
                raise V88Error("two writes overlap at %04x" % di)
            seen[di:di + m] = b"\x01" * m
            wrote[0] += m
            writes.append((di, m))
        if g.planes == 1:
            end = walk_lists(surf, rec, REC_HDR, write if check else None)
        else:                       # 98.1.3.1: a Map Mask, then its lists
            si, mv = REC_HDR, memoryview(surf)
            m3 = {}                 # (SUB2) a byte -> the masks written to it
            m3on = False
            while True:
                if si >= len(rec):
                    raise V88Error("the sub-records run off their record")
                mask = rec[si]
                si += 1
                if not mask:
                    break
                if mask >= SUB2_WM3 and g.sub2 and g.bitplanes and \
                        mask < SUB2_WM3 + 16:
                    # WRITE MODE 3 (98.1.3.2.4): one colour, its bytes masks
                    if key:
                        raise V88Error("a keyframe with a write mode 3 "
                                       "sub-record")
                    m3on = True
                    sc, got = bytearray(65536), []
                    end = walk_sub(sc, rec, si, g,
                                   lambda k, di, m: got.append((di, m)))
                    for di, m in got:
                        if check:
                            bounds(di, m)
                            for a in range(di, di + m):
                                if any(sn[a] for sn in seens):
                                    raise V88Error(
                                        "a byte at %04x written by planes "
                                        "and by write mode 3" % a)
                                if m3.get(a, 0) & sc[a]:
                                    raise V88Error(
                                        "two write mode 3 masks overlap at "
                                        "%04x" % a)
                                m3[a] = m3.get(a, 0) | sc[a]
                            wrote[0] += m
                            writes.append((di, m))
                        wm3_apply(surf, di, sc[di:di + m], mask - SUB2_WM3)
                    si = end
                    continue
                if mask > 15:
                    raise V88Error("a Map Mask of %02x" % mask)
                if m3on:
                    raise V88Error("a plane sub-record after a write mode 3 "
                                   "one")
                for p in range(4):
                    if mask >> p & 1:
                        plane[0] = p
                        end = walk_sub(mv[p * PLANE:(p + 1) * PLANE], rec,
                                       si, g, write if check else None)
                si = end
            end = si
        if self.runs and not key:      # 98.1.3.4: the blit runs, and every
            runs, end = runs_of(rec, end, g)    # write inside one of them
            if check:
                for di, m in writes:
                    for a in range(di, di + m):
                        y = g.rowof[a]
                        x = a - g.base[y]
                        if not any(r0 <= y < r1 and x0 <= x < x1
                                   for r0, r1, x0, x1 in runs):
                            raise V88Error("a write at %04x is in no blit run"
                                           % a)
        tail = len(rec) - end
        want = (1 if self.audio == AUD_ADPCM4 and not part else 0) if key \
            else 0 if layer else self.abytes
        if tail != want:
            raise V88Error("%d bytes follow the lists, not %d" % (tail, want))
        if check and not wrote[0] and y0 != y1:
            raise V88Error("an empty record with a band %d..%d" % (y0, y1))
        return rec[end:]

    def apply_page(self, pages, vis, rec, check=True):
        """PAGE-AHEAD (98.1.3.7): one frame record onto two pages, `vis`
        the one shown. A FLIP record is drawn on the hidden page, which is
        then shown; a hidden-page record after its lists goes onto the page
        hidden after it. (the page shown now, the record's tail)
        ON N PAGES (98.1.3.7.1) `pages` are all N by number: a flip names
        the page it shows, each hidden record its own"""
        if self.fpage:
            return self._fpage(pages, vis, rec, check)
        if self.mpage:
            flip, pg, own, hs = mpage_parts(rec, self.g, self.pages)
            if flip:
                if pg == vis:
                    raise V88Error("a flip onto the page shown")
                vis = pg
            elif pg:
                raise V88Error("a page in a record that does not flip")
            tail = self.apply(pages[vis], own, check=check)
            for hp, hrec in hs:
                if hp == vis:
                    raise V88Error("a hidden record on the page shown")
                self.apply(pages[hp], hrec, check=check, layer=True)
            return vis, tail
        flip, own, hrec = page_parts(rec, self.g)
        if flip:
            vis ^= 1
        tail = self.apply(pages[vis], own, check=check)
        if hrec is not None:
            self.apply(pages[vis ^ 1], hrec, check=check, layer=True)
        return vis, tail

    def _fpage(self, pages, vis, rec, check):
        """apply_page FLIPPED AND BUILT AHEAD (98.1.3.7.2): every record is
        drawn on the page it names, which is then shown. Unless BUILT, that
        page is first BROUGHT UP - the last record's own lists onto it
        again (`flast`; none after a key) - and must then be the page shown,
        byte for byte, because a player may bring it up by copying the last
        record's rows off the glass instead. Then its hidden records and
        COPYs, in order"""
        g = self.g
        flip, pg, own, hs, built = mpage_parts(rec, g, self.pages, True)
        if not flip or pg == vis:
            raise V88Error("a record on page %d with page %d shown, in a "
                           "file flipped every frame" % (pg, vis))
        if not built:
            if self.flast is not None:
                self.apply(pages[pg], self.flast, check=False)
            if check and g.canvas(pages[pg]) != g.canvas(pages[vis]):
                raise V88Error("page %d brought up is not page %d, the page "
                               "shown (98.1.3.7.2)" % (pg, vis))
        tail = self.apply(pages[pg], own, check=check)
        vis = pg
        for hp, x in hs:
            if hp == vis:
                raise V88Error("a hidden record or a copy on the page shown")
            if isinstance(x, tuple):    # A COPY: whole rows, every plane
                src, c0, c1 = x
                for p in range(g.planes):
                    o = p * PLANE
                    for y in range(c0, c1):
                        b = o + g.base[y]
                        pages[hp][b:b + g.wb] = pages[src][b:b + g.wb]
            else:
                self.apply(pages[hp], x, check=check, layer=True)
        self.flast = own
        return vis, tail

    def key_view(self, krec, check=True):
        """A page-ahead key's pages BY NUMBER and the page shown after it:
        (pages, shown, tail) - on two pages, page 0 shown (98.1.3.7); on N,
        the page its parts name (98.1.3.7.1)"""
        if not self.mpage:
            ps, tail = self.key_pages(krec, check)
            return ps, 0, tail
        pages = [self.g.surface() for _ in range(self.pages)]
        ps = key_split(krec)
        if len(ps) > 1 and not self.kparts:
            raise V88Error("a key in parts in a file without KPARTS")
        shown = None
        held, named = set(), {}
        for i, p in enumerate(ps):
            flip, pg, own, hs = mpage_parts(
                p, self.g, self.pages, kref=self.kpool and i + 1 == len(ps))
            for hp, x in hs:
                if isinstance(x, tuple):
                    named[hp] = x
                else:
                    held.add(hp)
            hs = [(hp, x) for hp, x in hs if not isinstance(x, tuple)]
            if flip:
                raise V88Error("a keyframe that flips")
            if shown is not None and pg != shown:
                raise V88Error("a key's parts name pages %d and %d"
                               % (shown, pg))
            shown = pg
            last = i + 1 == len(ps)
            for n, surf in enumerate(pages):
                t = self.apply(surf, own, key=True,
                               check=check and n == pg, part=not last)
                if n == pg:
                    tail = t
            for hp, hrec in hs:
                if hp == pg:
                    raise V88Error("a key's hidden record on its page shown")
                self.apply(pages[hp], hrec, check=check, layer=True)
        for hp, (kind, at, n) in sorted(named.items()):
            # A PAGE FROM THE POOL (98.1.3.8.1): black, then the picture
            if hp == shown or hp in held:
                raise V88Error("a key names page %d from the pool and %s"
                               % (hp, "shows it" if hp == shown
                                  else "carries it too"))
            pages[hp] = self.g.surface()
            pp = self.pool_pic(at, n)
            for q in pp:            # (each part a record of its own, with
                self.apply(pages[hp], q, check=check, layer=True)  # no tail)
        return pages, shown, tail

    def key_pages(self, krec, check=True):
        """...and a page-ahead key: its own record onto black on BOTH pages,
        then the hidden page's difference onto page 1. (pages, tail)"""
        a, b = self.g.surface(), self.g.surface()
        ps = key_split(krec)            # (98.1.3.8: each part, in turn)
        if len(ps) > 1 and not self.kparts:
            raise V88Error("a key in parts in a file without KPARTS")
        for i, p in enumerate(ps):
            flip, own, hrec = page_parts(p, self.g)
            if flip:
                raise V88Error("a keyframe that flips")
            last = i + 1 == len(ps)
            tail = self.apply(a, own, key=True, check=check, part=not last)
            self.apply(b, own, key=True, check=False, part=not last)
            if hrec is not None:
                self.apply(b, hrec, check=check, layer=True)
        return [a, b], tail

    def _unpack(self, at, what):
        """A block: its four fields at `at`, read and expanded"""
        off, packed, unpacked, pk = struct.unpack_from("<IIIB", self.d, at)
        data = self.d[off:off + packed]
        # 98.1.7.1: PACKED, one read of under 60 KB into under 128 KB (the
        # kernel's decompressor takes its input in one segment); STORED, any
        # size under 1 MB, read in pieces - memory is the bound
        big = packed > BLK_PACKED_MAX or unpacked > BLK_UNPACKED_MAX \
            if pk != PK_NONE else packed != unpacked or packed > BLK_STORED_MAX
        if len(data) != packed or pk not in (PK_NONE, PK_LZ4, PK_LZB) or big:
            raise V88Error("the %s block at %d (%d bytes, packing %d) does "
                           "not fit" % (what, off, packed, pk))
        if pk == PK_NONE:
            out = data
        else:
            import os88lz
            try:
                out = os88lz.decompress(data, pk - 1, unpacked)
            except Exception as e:
                raise V88Error("the %s block does not expand: %s" % (what, e))
        if len(out) != unpacked:
            raise V88Error("the %s block expands to %d bytes, not %d"
                           % (what, len(out), unpacked))
        return out

    def _blocks(self, d):
        """98.1.7: the rendition's block, walked into its records - the
        frames', then the seam's with LOOPREC - and the audio block"""
        if self.sp0 or self.sp0n or self.spmax:
            raise V88Error("a resident rendition names a chain")
        blk = self._unpack(self.slot + R_BLOCK, "picture")
        n = self.frames + (1 if self.flags_ & F_LOOPREC else 0)
        self._recs, o = [], 0
        minrec = REC_HDR + (1 if self.g.planes > 1 else 10)
        for i in range(n):
            if o + 2 > len(blk):
                raise V88Error("the block ends at record %d of %d" % (i, n))
            ln = struct.unpack_from("<H", blk, o)[0]
            if ln < minrec or o + ln > len(blk):
                raise V88Error("record %d of the block says %d bytes"
                               % (i, ln))
            self._recs.append(blk[o:o + ln])
            o += ln
        if o != len(blk) or len(blk) != self.slen:
            raise V88Error("the block holds %d bytes past its records, or "
                           "is not the %d the slot says" % (len(blk) - o,
                                                           self.slen))
        self._seam = self._recs.pop() if self.flags_ & F_LOOPREC else b""
        self._aud = b""
        if self.abytes:
            self._aud = self._unpack(AUD_AT, "audio")
            if len(self._aud) != self.frames * self.abytes:
                raise V88Error("the audio block is %d bytes, not %d"
                               % (len(self._aud), self.frames * self.abytes))
        elif any(d[AUD_AT:AUD_AT + 16]):
            raise V88Error("an audio block in a silent file")

    def _audio(self, f):
        return self._aud[f * self.abytes:(f + 1) * self.abytes]

    def sound(self, f):
        """FRAME f's sound, wherever the file keeps it: the frame's own
        record's tail, a RESIDENT file's block, or - sound ahead (98.1.8) -
        the start's lead for the first A frames and record f - A's tail
        after them. What a test compares a capture with, never a slice"""
        if not self.abytes:
            return b""
        if self.resident:
            return self._audio(f)
        if getattr(self, "_aseq", None) is None:
            tails = [rec[len(rec) - self.abytes:]
                     for rec, _, _ in self.records()]
            A, ab = self.ahead, self.abytes
            lead = self.d[self.lead0at:self.lead0at + A * ab]
            self._aseq = [lead[i * ab:(i + 1) * ab] for i in range(A)] + \
                tails[:len(tails) - A]
        return self._aseq[f]

    def key_lead(self, i):
        """Keyframe i's LEAD (98.1.8): frames k + 1 .. k + A's sound, the
        entry's bytes after its record; b"" in a file without AHEAD"""
        if not self.ahead:
            return b""
        L = self.ahead * self.abytes
        if self.kleadat:                # apart (98.1.8.1)
            return self.d[self.kleadat + i * L:self.kleadat + (i + 1) * L]
        k, off, n, spo, spn, idx = self.keys[i]
        return self.d[off + n - L:off + n]

    def lead0(self):
        """The start's lead: frames 0 .. A - 1's sound"""
        return self.d[self.lead0at:self.lead0at + self.ahead * self.abytes]

    def after_key(self, e):
        """The records from the frame after keyframe entry `e`"""
        k, off, n, spo, spn, idx = e
        if self.resident:
            return self.records(first=k + 1)
        return self.records(spo, spn, idx)

    def seam(self):
        """The seam record (98.1.1.2), with frame L's audio, or None"""
        if not self.loop:
            return None
        L, off, n, secs, idx, spo = self.loop
        if self.resident:
            return self._seam + self._audio(L)
        return self.d[off:off + n]

    def options(self):
        """The record the encoder stored (98.1.1.4, 98.2.17), or None for a
        file made before it stored one - V88Error for a block that points
        outside the file or does not read"""
        if not self.optsat and not self.optslen:
            return None
        if self.optsat < SECTOR or not self.optslen or \
                self.optsat + self.optslen > len(self.d):
            raise V88Error("an options block of %d bytes at %d, in a file "
                           "of %d" % (self.optslen, self.optsat, len(self.d)))
        return unpack_options(self.d[self.optsat:self.optsat + self.optslen])

    def pool_pic(self, at, n):
        """A page pool picture (98.1.3.8.1) at `at`, its first read `n`: its
        parts, each read checked as a key's are"""
        total = at
        while True:
            if n < REC_HDR + 1 or n > KEY_PART + 1 or total + n > len(self.d):
                raise V88Error("a page pool picture at %d runs off the file"
                               % at)
            if not struct.unpack_from("<H", self.d, total + 2)[0] & KEY_MORE:
                break
            if struct.unpack_from("<H", self.d, total)[0] != n:
                raise V88Error("a page pool picture's part of a read of %d"
                               % n)
            total += n
            n = struct.unpack_from("<H", self.d, total - 2)[0]
        return key_split(self.d[at:total + n])

    def key(self, i):
        k, off, n, spo, spn, idx = self.keys[i]
        rec = self.d[off:off + n]
        if len(rec) != n or n < REC_HDR + (1 if self.g.planes > 1 else 10):
            raise V88Error("keyframe %d runs off the file" % i)
        if self.kparts:
            # KEYS IN PARTS (98.1.3.8): the entry is the first read, and a
            # part with KEY_MORE ends in the next one's bytes
            at, total = off, 0
            while struct.unpack_from("<H", self.d, at + 2)[0] & KEY_MORE:
                pn = struct.unpack_from("<H", self.d, at)[0]
                if pn != n or pn < REC_HDR + 3:
                    raise V88Error("keyframe %d: a part of %d in a read of "
                                   "%d" % (i, pn, n))
                total += n
                at += n
                n = struct.unpack_from("<H", self.d, at - 2)[0]
                if at + n > len(self.d) or n < REC_HDR + 1 or \
                        n > KEY_PART + 1:
                    raise V88Error("keyframe %d runs off the file" % i)
            rec = self.d[off:at + n]
            key_split(rec)
            return k, rec, spo, spn, idx
        if struct.unpack_from("<H", rec, 2)[0] & KEY_MORE:
            raise V88Error("keyframe %d is in parts, in a file without "
                           "KPARTS (98.1.3.8)" % i)
        if self.ahead and not self.kleadat:
            # the entry is the record AND its lead, the entry's last
            # A x abytes bytes (98.1.8; an ADPCM4 key's reference byte is
            # the record's last, after its length word's reach)
            m = n - self.ahead * self.abytes
            if m < struct.unpack_from("<H", rec, 0)[0]:
                raise V88Error("keyframe %d: an entry of %d, a record of %d "
                               "and a lead of %d" % (
                                   i, n, struct.unpack_from("<H", rec, 0)[0],
                                   self.ahead * self.abytes))
            rec = rec[:m]
        return k, rec, spo, spn, idx


class HeadOptions:
    """A .V88's title, credits and stored options (98.1.1.4) ALONE, read
    off its first sector and its options block - none of the stream,
    which a Reader reads and a preview decodes. What the encoder window
    fills its form from the moment a file is opened (98.2.17), so a person
    who wants only how it was made does not wait on a preview. The same
    checks of the block a Reader makes; form_from_file takes either"""

    def __init__(self, path):
        with open(path, "rb") as f:
            d = f.read(SECTOR)
            if len(d) < SECTOR or d[:4] != V88_SIG:
                raise V88Error("not a .V88")
            size = os.fstat(f.fileno()).st_size
            self.title = d[32:80].split(b"\0")[0].decode("ascii", "replace")
            self.credits = d[80:176].split(b"\0")[0].decode("ascii",
                                                            "replace")
            self.optsat, self.optslen = struct.unpack_from("<IH", d, H_OPTS)
            self._blk = None
            if self.optsat or self.optslen:
                if self.optsat < SECTOR or not self.optslen or \
                        self.optsat + self.optslen > size:
                    raise V88Error("an options block of %d bytes at %d, in a "
                                   "file of %d" % (self.optslen, self.optsat,
                                                   size))
                f.seek(self.optsat)
                self._blk = f.read(self.optslen)

    def options(self):
        return None if self._blk is None else unpack_options(self._blk)


def first_shown(r):
    """The first frame a play from the start SHOWS (98.2.9, 98.3.5): a
    colour play starts from key 0, past any pre-roll, and a one-bit or
    composite one from the stream's first record"""
    return r.keys[0][0] if r.keys and r.pixfmt >= PF_VGA8 else 0


def key_at(r, f):
    """The keyframe a play from frame `f` starts at (98.3.5): the last at or
    before it, or the first when `f` is before every key (a pre-roll's
    frames, 98.2.9). None when the file has no keys."""
    if not r.keys:
        return None
    i = 0
    for j, e in enumerate(r.keys):
        if e[0] <= f:
            i = j
    return i


def set_poster(path, frame):
    """THE POSTER, CHANGED IN PLACE (98.2.11): every rendition's poster word
    (slot + 14) set to its key_at(`frame`), and nothing else in the file
    touched. A poster is a keyframe INDEX, and renditions need not share
    their keys, so each is placed by the frame rather than by the index.
    The file is re-read whole afterwards, every rendition; returns the
    posters written."""
    d = bytearray(open(path, "rb").read())
    n = Reader(path).nrend
    posters = []
    for ri in range(n):
        r = Reader(path, ri)
        i = key_at(r, frame)
        if i is None:
            raise V88Error("rendition %d has no keyframes to be a poster" % ri)
        struct.pack_into("<H", d, r.slot + 14, i)
        posters.append(i)
    with open(path, "r+b") as f:
        f.write(d[:SECTOR])
    for ri in range(n):
        Reader(path, ri)
    return posters


TITLE_AT, TITLE_LEN = 32, 48       # the header's title field (98.1.1)


def title_bytes(title):
    """A title as the header holds it: ASCII, a character the model face
    has no glyph for a '?', cut to the field less its NUL - the Writer's
    own rule, so a title set later is the title an encode would have
    written"""
    t = "".join(c if " " <= c <= "~" else "?" for c in title.strip())
    return t.encode("ascii")[:TITLE_LEN - 1]


def set_title(path, title):
    """THE TITLE, CHANGED IN PLACE (98.2.11): the header's 48 bytes at +32
    rewritten - the title, then NULs to the field's end, so no tail of a
    longer old one is left behind it - and nothing else in the file
    touched. The file is re-read whole afterwards, every rendition;
    returns the title written"""
    t = title_bytes(title)
    d = bytearray(open(path, "rb").read(SECTOR))
    n = Reader(path).nrend
    d[TITLE_AT:TITLE_AT + TITLE_LEN] = t + bytes(TITLE_LEN - len(t))
    with open(path, "r+b") as f:
        f.write(d)
    for ri in range(n):
        Reader(path, ri)
    return t.decode("ascii")


def spk_preview(counts, rate, pulses=1, fs=44100):
    """WHAT THE SPEAKER LINE CARRIES (98.2.15.2), as floats at `fs`: the
    pulses the counts drive, built at the PIT's own 1,193,182 Hz - low for
    a count's ticks from each write, high for the rest of the pulse - and
    brought down to `fs` through a 4x oversampled bin and a brick wall at
    20 kHz, so the carrier and its harmonics are there as they are and
    none of them folds into a false tone. reenigne's mod_convert does the
    same for 8088 MPH. It is the ELECTRICAL line: no cone, no room"""
    import numpy as np
    n = PIT_HZ // rate // pulses
    c = np.frombuffer(bytes(counts), dtype=np.uint8).astype(np.int64)
    over, out, tail = fs * 4, [], None
    per = 4 * rate                      # samples a chunk (4 s)
    for at in range(0, len(c), per):
        cc = np.repeat(c[at:at + per], pulses)
        t = np.ones(len(cc) * n, dtype=np.float32)      # 1 = high
        k = np.arange(n)
        t[(np.arange(len(cc))[:, None] * n + k)[k < cc[:, None]]] = 0.0
        cs = np.concatenate(([0.0], np.cumsum(t, dtype=np.float64)))
        t0 = at * pulses * n
        edges = np.arange(np.ceil(t0 * over / PIT_HZ),
                          (t0 + len(t)) * over / PIT_HZ) * PIT_HZ / over - t0
        edges = edges[(edges >= 0) & (edges <= len(t))]
        v = np.diff(np.interp(edges, np.arange(len(cs)), cs)) / \
            np.diff(edges)
        out.append(v)
    y = np.concatenate(out) if out else np.zeros(0)
    Y = np.fft.rfft(y - y.mean())
    f = np.fft.rfftfreq(len(y), 1.0 / over)
    Y[(f > 20000) | (f < 20)] = 0
    y = np.fft.irfft(Y, len(y))[::4]
    pk = np.abs(y).max() if len(y) else 0
    return y / pk * 0.9 if pk else y


def write_spk_preview(path, counts, rate, pulses=1, fs=44100):
    """spk_preview into a 16-bit mono WAV"""
    import numpy as np
    import wave
    y = spk_preview(counts, rate, pulses, fs)
    w = wave.open(path, "wb")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(fs)
    w.writeframes((np.clip(y, -1, 1) * 32767).astype("<i2").tobytes())
    w.close()
    return len(y) / float(fs)


def spk_reshape(src, dst, hp=SPK_HP, drive=SPK_DRIVE, lows=SPK_LOWS,
                rng=SPK_RANGE, ratio=SPK_RATIO, idle=SPK_IDLE, style=None):
    """A SPEAKER FILE'S SOUND SHAPED AFTER THE FACT (98.2.15.1): every
    rendition's counts read back to samples, spk_shape'd and written as
    counts again, in the same bytes - each frame record's last `abytes`
    and the seam's - so nothing else in the file moves. A resident file's
    sound is a packed block, so it is refused: encode it again. The file's
    stored options (98.2.17), when it has them, are brought up to date with
    the new shaping - in the sectors they already had, or, should they no
    longer fit there, taken out rather than left saying what is no longer
    true. Returns the renditions done"""
    d = bytearray(open(src, "rb").read())
    n = Reader(src).nrend
    for ri in range(n):
        r = Reader(src, ri)
        if not r.spk:
            raise V88Error("rendition %d is not made for the speaker (no "
                           "SPKPWM, 98.1.1.3)" % ri)
        if r.resident:
            raise V88Error("a resident file's sound is packed: encode it "
                           "again with --audio speaker")
        at, nsec, where = r.sp0, r.sp0n, []
        while nsec:
            nf, nxt = struct.unpack_from("<HH", d, at)
            o = at + 4
            for i in range(nf):
                m = struct.unpack_from("<H", d, o)[0]
                where.append(o + m - r.abytes)
                o += m
            at, nsec = at + nsec * SECTOR, nxt
        if len(where) != r.frames:
            raise V88Error("rendition %d: %d records for %d frames"
                           % (ri, len(where), r.frames))
        ab, A, N = r.abytes, r.ahead, r.frames
        # every place a frame's sound is kept: record p carries the frame A
        # on in play order (98.1.8; A = 0 without AHEAD), and with AHEAD the
        # start's lead and each key's hold copies too
        J = r.loop[0] if r.loop else (r.keys[0][0] if r.keys else None)

        def play(q):
            if q < N:
                return q
            return None if J is None else J + (q - N) % (N - J)
        held = [(w, play(p + A)) for p, w in enumerate(where)]
        if A:
            held += [(r.lead0at + i * ab, i) for i in range(A)]
            for k, off, n, spo, spn, idx in r.keys:
                held += [(off + n - (A - j) * ab, play(k + 1 + j))
                         for j in range(A)]
        if r.loop is not None:
            L, off, m = r.loop[:3]
            held.append((off + m - ab, play(N + A)))
        first = {}
        for w, f in held:
            if f is not None:
                first.setdefault(f, w)
        old = b"".join(bytes(d[first[f]:first[f] + ab]) for f in range(N))
        new = spk_counts(spk_shape(spk_samples(old, r.rate, r.spkp), r.rate,
                                   hp, drive, lows=lows, rng=rng,
                                   ratio=ratio, idle=idle), r.rate,
                         r.spkp)
        for w, f in held:
            if f is not None:
                d[w:w + ab] = new[f * ab:(f + 1) * ab]
    _reshape_options(d, Reader(src), dict(
        spk_shape="encoder", spk_style=style or SPK_STYLE, spk_highpass=hp,
        spk_drive=drive, spk_lows=lows, spk_range=rng, spk_ratio=ratio,
        spk_idle=idle))
    with open(dst, "wb") as f:
        f.write(d)
    for ri in range(n):
        for rec, at, i in Reader(dst, ri).records():
            pass
    return n


def _reshape_options(d, r, changed):
    """A streamed file's stored options (98.1.1.4) with `changed` put in,
    written into the sectors the block has before the stream (bytes `d`,
    patched in place); the block and its pointer cleared when the new one
    does not fit, or the old one does not read"""
    if not r.optsat:
        return
    import os88venc                     # (the schema is the encoder's)
    room = r.sp0 - r.optsat
    try:
        o = os88venc.opts_migrate(r.options())[0]
        doc = r.options()
        o.update(changed)
        blob = pack_options({"v": os88venc.OPTS_VERSION,
                             "src": doc.get("src", ""), "o": o})
    except V88Error:
        blob = b""
    if not 0 < len(blob) <= room:
        blob = b""
    d[r.optsat:r.optsat + room] = blob + bytes(room - len(blob))
    struct.pack_into("<IH", d, H_OPTS, r.optsat if blob else 0, len(blob))


def cycles_of(rec, planar=False, layout=None, table=None, sub=CYC_SUB,
              sub2=False, wm3=WM3_BYTE):
    """The wave 0 model's cycles for one record, writing CGA's screen: the
    frame's fixed cost, a set-up per skip segment, each entry by its list,
    and an absolute entry's extra address. A MODEX record's sub-records
    are decoded once each whatever their mask, and pay an OUT (`sub`).
    `layout` picks another decoder's constants (CYC_LAYOUT), and `table`
    a machine's own, in cyc_table's shape (os88venc's profile_table).
    With `sub2` (98.1.3.2.4) a sub-record carries its list map, and a
    write mode 3 one's bytes cost `wm3` more each, its record WM3_REC
    once"""
    t = cyc_table(layout) if table is None else table
    if not planar:
        return _cycles_lists(rec, REC_HDR, t[0], t)[1]
    c, si, m3 = t[0], REC_HDR, False
    while rec[si]:
        c += sub
        lead = rec[si]
        if not sub2:
            si, c = _cycles_lists(rec, si + 1, c, t)
            continue
        lmap = rec[si + 1] | rec[si + 2] << 8
        if lead >= SUB2_WM3:
            if not m3:
                c += WM3_REC
                m3 = True
            got, sc = [], bytearray(65536)  # (a RUN of FFh masks is a
            walk_lists(sc, rec, si + 3,     # rep stosb: no read)
                       lambda k, di, m: got.append((k, di, m)), lmap=lmap)
            c += wm3 * sum(m for k, di, m in got
                           if k not in (L_RUN, L_RUNL) or sc[di] != 0xFF)
        si, c = _cycles_lists(rec, si + 3, c, t, lmap)
    return c


def _cycles_lists(data, si, c, t, lmap=None):
    """cycles_of's walk: walk_lists' over the ten lists at data[si:] -
    the same entries, the same checks and the same sums in the same order
    - with nothing written, where the encoder had a fresh 64 KB surface
    written for every record it priced. -> (the index past them, c plus
    their cycles)"""
    fr, sg, ab, cp, csl, crn = t
    nd = len(data)
    mode = False
    try:
        for k in range(NLISTS):
            if lmap is not None and not lmap >> k & 1:
                continue
            short = k <= L_P6
            slice_ = k in (L_SLICE, L_SLICEL)
            len8 = k in (L_SLICE, L_RUN)
            while True:
                n = data[si]
                si += 1
                if n == 0:
                    break
                if n & 0x80:
                    todo, absolute = n & 0x7F, True
                else:
                    todo, absolute = n, False
                    di = data[si] | data[si + 1] << 8
                    si += 2
                c += 0 if absolute else sg
                mode = absolute
                for _ in range(todo):
                    if absolute:
                        di = data[si] | data[si + 1] << 8
                        si += 2
                    else:
                        di += data[si]
                        si += 1
                    if short:
                        m = k - L_P1 + 1
                        si += m
                    else:
                        if len8:
                            m = data[si]
                            si += 1
                        else:
                            m = data[si] | data[si + 1] << 8
                            si += 2
                        if slice_:
                            si += m
                        else:
                            data[si]        # (the value: there, or short)
                            si += 1
                    if si > nd:
                        raise IndexError
                    c += ab if mode else 0
                    if short:
                        c += cp[k]
                    elif slice_:
                        c += csl[0] + csl[1] * m
                    else:
                        c += crn[0] + crn[1] * m
                    if di + m > 65536:
                        raise V88Error("a write at %04x+%d wraps the segment"
                                       % (di, m))
                    di += m
    except IndexError:
        raise V88Error("the lists run off the end of their record")
    return si, c


def _bench_lists(mix, div):
    """bench_record's ten lists: each row of `mix` (list, bytes, changes,
    absolute) in list order, its changes >> div, every write inside
    BENCH_SPAN and every byte of it 0"""
    out = bytearray()
    for k in range(NLISTS):
        for kk, n, num, ab in mix:
            if kk != k:
                continue
            num >>= div
            chg = bytes(n) if k <= L_P6 else bytes([n]) + (
                bytes(n) if k == L_SLICE else b"\0")
            if ab:
                a = 0
                while num:
                    c = min(num, 127)
                    num -= c
                    out.append(0x80 | c)
                    for _ in range(c):
                        out += struct.pack("<H", a) + chg
                        a += 3
                        if a + n > BENCH_SPAN:
                            a = 0
                continue
            per = min(127, (BENCH_SPAN - n) // (n + 1) + 1)
            while num:
                c = min(num, per)
                num -= c
                out += bytes((c, 0, 0))
                for i in range(c):
                    out += (b"\1" if i else b"\0") + chg
        out.append(0)
    return out


def bench_record(planar, sub2):
    """THE BENCH (98.1.9.13): the record the player decodes to time itself,
    which it builds itself - video.asm's vp_bngen, byte for byte. `planar`
    is 0, 1 (MODEX) or 2 (VGA4): four sub-records, a plane each, a quarter
    of the mix in each, and with SUB2 a list map before each and a colour
    sub-record after them"""
    if not planar:
        body = _bench_lists(BENCH_MIX, 0)
    else:
        body = bytearray()
        for m in (1, 2, 4, 8):
            body.append(m)
            if sub2:
                body += struct.pack("<H", (1 << NLISTS) - 1)
            body += _bench_lists(BENCH_MIX, 2)
        if planar == 2 and sub2:
            body.append(SUB2_WM3)
            body += struct.pack("<H", (1 << NLISTS) - 1)
            body += _bench_lists(BENCH_CMIX, 0)
        body.append(0)
    return struct.pack("<HHH", REC_HDR + len(body), 0, 1) + bytes(body)


def bench_sum(rec):
    """The player's check on what it built ([vp_bnsum]): each byte added to
    the word rotated left one"""
    s = 0
    for b in rec:
        s = (((s << 1) | (s >> 15)) & 0xFFFF) + b & 0xFFFF
    return s


def bench_ok(pixfmt, layout, resident, live):
    """Whether a rendition carries a bench: one the player decodes natively
    into the layouts' off-screen gaps (98.1.9.13) - not the text screens',
    not a resident block or a Live file"""
    return (not resident and not live and
            pixfmt in (PF_MONO1, PF_CGACOMP, PF_VGA8, PF_VGA4, PF_CGA4) and
            layout in (LAY_CGA, LAY_HERC, LAY_LIN80, LAY_LIN320, LAY_MODEX))


# --------------------------------------------------------------------------
# frames in, pictures out
# --------------------------------------------------------------------------
def _tokens(d, n):
    """The first `n` whitespace-separated header tokens of a PNM, skipping
    comments; and the offset just past the one whitespace after the last."""
    out, i = [], 2
    while len(out) < n:
        while d[i:i + 1].isspace():
            i += 1
        if d[i:i + 1] == b"#":
            while d[i:i + 1] not in (b"\n", b""):
                i += 1
            continue
        j = i
        while not d[j:j + 1].isspace():
            j += 1
        out.append(int(d[i:j]))
        i = j
    return out, i + 1


def read_frame(path):
    """(width px, height, 1bpp rows packed MSB first, 1 = WHITE) from a PBM
    (P4 or P1), a PGM (P5, >= 128 is white) or an uncompressed BMP of 1, 8
    or 24 bits (luminance >= 128 is white). The frames are expected to be
    dithered already; this only thresholds."""
    d = open(path, "rb").read()
    lum = None
    if d[:2] in (b"P4", b"P1"):
        (w, h), o = _tokens(d, 2)
        if d[:2] == b"P4":
            rb = (w + 7) // 8
            px = [255 * (1 - ((d[o + y * rb + x // 8] >> (7 - x % 8)) & 1))
                  for y in range(h) for x in range(w)]
        else:
            bits = [c for c in d[o:] if c in b"01"]
            px = [255 * (1 - (c - 48)) for c in bits[:w * h]]
        lum = px
    elif d[:2] == b"P5":
        (w, h, mx), o = _tokens(d, 3)
        lum = [v * 255 // mx for v in d[o:o + w * h]]
    elif d[:2] == b"BM":
        off, = struct.unpack_from("<I", d, 10)
        w, h, planes, bpp, comp = struct.unpack_from("<iiHHI", d, 18)
        if comp not in (0, 3) or bpp not in (1, 8, 24):
            raise V88Error("%s: a %d-bit BMP (compression %d)"
                           % (path, bpp, comp))
        hsz, = struct.unpack_from("<I", d, 14)
        pal = d[14 + hsz:off]
        top = h < 0
        h = abs(h)
        stride = (w * bpp + 31) // 32 * 4
        lum = []
        for y in range(h):
            r = d[off + (y if top else h - 1 - y) * stride:][:stride]
            for x in range(w):
                if bpp == 24:
                    b_, g_, r_ = r[3 * x:3 * x + 3]
                else:
                    i = (r[x // 8] >> (7 - x % 8)) & 1 if bpp == 1 else r[x]
                    b_, g_, r_ = pal[4 * i:4 * i + 3]
                lum.append((r_ * 299 + g_ * 587 + b_ * 114) // 1000)
    else:
        raise V88Error("%s: not a PBM, PGM or BMP" % path)
    if len(lum) != w * h:
        raise V88Error("%s: %d pixels for %d x %d" % (path, len(lum), w, h))
    rows = bytearray()
    for y in range(h):
        for x0 in range(0, w, 8):
            b = 0
            for x in range(x0, x0 + 8):
                b = b << 1 | (x < w and lum[y * w + x] >= 128)
            rows.append(b)
    return w, h, bytes(rows)


def read_wav(path):
    """(rate, unsigned 8-bit mono samples) from a PCM WAV of 8 or 16 bits,
    mono or stereo (the channels are averaged)."""
    d = open(path, "rb").read()
    if d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        raise V88Error("%s: not a WAV" % path)
    i, fmt, data = 12, None, None
    while i + 8 <= len(d):
        cid, n = d[i:i + 4], struct.unpack_from("<I", d, i + 4)[0]
        if cid == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", d, i + 8)
        elif cid == b"data":
            data = d[i + 8:i + 8 + n]
        i += 8 + n + (n & 1)
    if not fmt or data is None or fmt[0] != 1 or fmt[5] not in (8, 16) \
            or fmt[1] not in (1, 2):
        raise V88Error("%s: only PCM, 8 or 16 bits, mono or stereo" % path)
    ch, rate, bits = fmt[1], fmt[2], fmt[5]
    if bits == 8:
        s = list(data)
    else:
        s = [((v + 32768) >> 8) for v in
             struct.unpack("<%dh" % (len(data) // 2), data[:len(data) & ~1])]
    if ch == 2:
        s = [(s[j] + s[j + 1]) // 2 for j in range(0, len(s) - 1, 2)]
    return rate, bytes(s)


def write_png(path, wb, h, cv):
    """A 1-bit greyscale PNG of a canvas, 1 = white - MONO1's own polarity,
    so the rows go in as they are."""
    import zlib

    def chunk(t, b):
        c = t + b
        return struct.pack(">I", len(b)) + c + \
            struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + cv[y * wb:(y + 1) * wb] for y in range(h))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" +
                chunk(b"IHDR", struct.pack(">IIBBBBB", wb * 8, h, 1, 0, 0, 0,
                                           0)) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


# --------------------------------------------------------------------------
# the V88 commands
# --------------------------------------------------------------------------
def import_xdv(src, out, target="cga", title=None, keysecs=KEY_SECS,
               poster=None, audio_fmt=AUD_PCM8):
    """SPEC.md 98.2's import. On CGA the XDV's own writes become the lists
    (clipped to the canvas, which changes no pixel). On another layout the
    stream is simulated and each frame re-encoded from the bytes it
    changed."""
    hdr, pk = read_xdv(src)
    layout = LAYOUT_BY_NAME[target]
    g = Geom(layout, ROWB, 200)
    cga = Geom(LAY_CGA, ROWB, 200) if layout != LAY_CGA else g
    pix = PF_CGACOMP if hdr["mode"] == 1 else PF_MONO1
    keyint = max(1, round(keysecs * hdr["rate"] / hdr["achunk"]))
    chunks = audio_chunks(b"".join(xdc_audio(p, hdr) for p in pk), len(pk),
                          hdr["achunk"], audio_fmt,
                          key_frames(len(pk), keyint))
    w = Writer(g, hdr["rate"], hdr["achunk"], audio_fmt, len(chunks[0]), pix,
               title=title or os.path.splitext(os.path.basename(src))[0],
               credits="imported from XDC (MobyGamer's XDC, MIT)",
               aspect=ASPECT[LAY_CGA], keysecs=keysecs)
    xs = bytearray(65536)
    ts = bytearray(65536) if layout != LAY_CGA else xs
    for i, p in enumerate(pk):
        ops, code = xdc_ops(p, "%s packet %d" % (os.path.basename(src), i))
        audio = chunks[i]
        if layout == LAY_CGA:
            ops = clip_ops(ops, g)
            apply_ops(xs, ops)
        else:
            changed = set()
            for a, bs, run in ops:
                for j, v in enumerate(bs):
                    ca = a + j
                    if not cga.valid[ca]:
                        continue
                    xs[ca] = v
                    y = cga.rowof[ca]
                    ta = g.base[y] + (ca - cga.base[y])
                    if ts[ta] != v:
                        ts[ta] = v
                        changed.add(ta)
            ops = spans(changed, ts, g)
        w.frame(ops, ts, audio)
    return w.write(out, poster)


def xdc_audio(p, hdr):
    """A packet's audio: XDC puts its `achunk` bytes at the very END of the
    packet, after the code, the data and the padding."""
    return bytes(p[len(p) - hdr["achunk"]:])


# --- ADPCM4 (SPEC.md 98.1.1): Creative's 4-bit ADPCM as a DSP 2.00 plays it
# with DSP 7Dh - the reference byte first, then two samples a byte, high
# nibble first. The tables are DOSBox's (decode_ADPCM_4_sample), and
# MartyPC's card (tools/martypc/patches/06) decodes with the same ones, so
# what is encoded here is what that card plays, sample for sample.
ADPCM4_SCALE = (
    0, 1, 2, 3, 4, 5, 6, 7, 0, -1, -2, -3, -4, -5, -6, -7,
    1, 3, 5, 7, 9, 11, 13, 15, -1, -3, -5, -7, -9, -11, -13, -15,
    2, 6, 10, 14, 18, 22, 26, 30, -2, -6, -10, -14, -18, -22, -26, -30,
    4, 12, 20, 28, 36, 44, 52, 60, -4, -12, -20, -28, -36, -44, -52, -60)
ADPCM4_ADJUST = (
    0, 0, 0, 0, 0, 16, 16, 16, 0, 0, 0, 0, 0, 16, 16, 16,
    -16, 0, 0, 0, 0, 16, 16, 16, -16, 0, 0, 0, 0, 16, 16, 16,
    -16, 0, 0, 0, 0, 16, 16, 16, -16, 0, 0, 0, 0, 16, 16, 16,
    -16, 0, 0, 0, 0, 0, 0, 0, -16, 0, 0, 0, 0, 0, 0, 0)


def _adpcm4_step(ref, scale, nib):
    i = min(63, max(0, nib + scale))
    return (min(255, max(0, ref + ADPCM4_SCALE[i])),
            min(48, max(0, scale + ADPCM4_ADJUST[i])))


def adpcm4_decode(data, ref=ADPCM4_REF, scale=0):
    """ADPCM4 bytes -> PCM8, the card's way"""
    out = bytearray()
    for b in data:
        for nib in (b >> 4, b & 15):
            ref, scale = _adpcm4_step(ref, scale, nib)
            out.append(ref)
    return bytes(out)


def adpcm4_encode(pcm, ref=ADPCM4_REF, scale=0, zeros=()):
    """PCM8 (an even count) -> ADPCM4, greedily: each nibble the one whose
    decoded sample is nearest, against the decoder's own state.

    `zeros` are sample indexes where the decoder must ARRIVE with its scale
    at 0 - a keyframe's frame k+1 (SPEC.md 98.1.1.1). The scale moves in
    steps of 16 and a zero-step nibble lowers it one, so the three samples
    before one are chosen only among nibbles that get it there: a card
    started at that byte with its reference is then in exactly the state the
    continuous stream is, and a seek plays the file's own sound."""
    if len(pcm) % 2:
        raise V88Error("ADPCM4 packs two samples a byte; %d is odd"
                       % len(pcm))
    zs = sorted(z for z in zeros if 0 < z <= len(pcm))
    left = {}                       # sample index -> steps to a zero
    for z in zs:
        for d in (1, 2, 3):
            if z - d >= 0:
                left[z - d] = min(left.get(z - d, d), d)
    out = bytearray()
    hi = None
    for j, x in enumerate(pcm):
        best = None
        cap = 16 * (left[j] - 1) if j in left else 48
        for nib in range(16):
            r2, s2 = _adpcm4_step(ref, scale, nib)
            if s2 > cap:
                continue
            e = abs(r2 - x)
            if best is None or e < best[0]:
                best = (e, nib, r2, s2)
        _, nib, ref, scale = best
        if hi is None:
            hi = nib
        else:
            out.append(hi << 4 | nib)
            hi = None
    return bytes(out)


_A4 = {}


def _adpcm4_tables():
    """The trellis's fixed tables, once: for each arriving scale q, the
    (scale, nibble, step) pairs that land there, as gather indexes into the
    1,024 states (sample r + 256 x scale/16) with 1,024 an INF sentinel"""
    if _A4:
        return _A4
    import numpy as np
    groups = [[] for _ in range(4)]
    for p in range(4):
        for nib in range(16):
            i = min(63, nib + 16 * p)
            q = min(3, max(0, p + ADPCM4_ADJUST[i] // 16))
            groups[q].append((p, nib, ADPCM4_SCALE[i]))
    G = max(len(g) for g in groups)
    r = np.arange(256)
    idx = np.full((4, G, 256), 1024, np.int64)
    nibt = np.zeros((4, G), np.uint8)
    for q, g in enumerate(groups):
        for j, (p, nib, d) in enumerate(g):
            src = r - d
            v = (src >= 0) & (src < 256)
            idx[q, j] = np.where(v, p * 256 + np.clip(src, 0, 255), 1024)
            nibt[q, j] = nib
    _A4.update(idx=idx, nibt=nibt,
               gidx=np.ascontiguousarray(idx.transpose(0, 2, 1)),
               # (the same, arriving groups second: a step's cheapest way
               # in is then an elementwise minimum across rows)
               gidxt=np.ascontiguousarray(idx),
               sq=((r[None, :] - r[:, None]) ** 2).astype(np.int32))
    return _A4


def _adpcm4_viterbi(pcm, start, zeros, chunk=2048, end=None):
    """(nibbles, states) for PCM8 `pcm`: the least-squared-error path, from
    the state `start` (sample + 256 x scale/16), or from ANY state when it
    is None - and, with `end`, the best path that FINISHES in that state
    (a resident file's lap join, 98.1.7.2). states[k] is the decoder's state
    after sample k. A decision is committed where every live state's
    survivor agrees, so the memory is a window and not the stream."""
    import numpy as np
    T = _adpcm4_tables()
    idx, nibt, gidxt, sq = T["idx"], T["nibt"], T["gidxt"], T["sq"]
    x = np.frombuffer(bytes(pcm), np.uint8).astype(np.int64)
    n = len(x)
    cap = np.full(n, 3, np.int64)
    for z in zeros:
        for d in (1, 2, 3):
            if 0 <= z - d < n:
                cap[z - d] = min(cap[z - d], d - 1)
    INF = np.int32(1) << 28
    cost = np.full(1025, INF, np.int32)
    if start is None:
        cost[:1024] = 0
    else:
        cost[start] = 0
    nibs = np.zeros(n, np.uint8)
    states = np.zeros(n, np.int32)
    win = [0, []]                   # the window's first sample, backpointers

    def trace(t, S, all_agree):
        w0, bp = win
        path = None if all_agree else t
        for k in range(t - 1, w0 - 1, -1):
            j = bp[k - w0][S]
            q, rr = S // 256, S % 256
            if path is None and np.all(S == S[0]):
                path = k + 1
            if path is not None:
                if len(S) > 1:
                    S, j, q, rr = S[:1], j[:1], q[:1], rr[:1]
                nibs[k] = nibt[q[0], j[0]]
                states[k] = S[0]
            S = idx[q, j, rr]
        return path

    for t in range(n):
        cand = cost[gidxt]                          # (4, G, 256)
        best = cand.min(axis=1)
        # the FIRST way in at that cost, as argmin's: about twice as fast
        # as argmin over a short last axis, and the same choice
        j = (cand == best[:, None, :]).argmax(axis=1)
        best += sq[x[t]]
        if cap[t] < 3:
            best[cap[t] + 1:, :] = INF
        dead = best >= INF
        best -= best.min()
        best[dead] = INF            # renormalised, and the dead stay dead
        cost[:1024] = best.reshape(1024)
        win[1].append(j.reshape(1024).astype(np.uint8))
        if len(win[1]) >= chunk:
            live = np.nonzero(cost[:1024] < INF)[0]
            path = trace(t + 1, live, True)
            if path is not None:    # decided before `path`: drop it
                win[1] = win[1][path - win[0]:]
                win[0] = path
    if end is not None:
        if cost[end] >= INF:
            raise V88Error("ADPCM4: no path ends in state %d (sample %d, "
                           "scale %d)" % (end, end % 256, 16 * (end // 256)))
        trace(n, np.array([end]), False)
    else:
        trace(n, np.array([int(cost[:1024].argmin())]), False)
    return nibs, states


def _adpcm4_seg(args):
    return _adpcm4_viterbi(*args)


ADPCM4_SEG = 88200         # adpcm4_search's segment, at most (8 s at 11 kHz)
ADPCM4_SEG_MIN = 16384     # ...and at least, where there are more cores:
                           # its lead is a quarter of that again


def adpcm4_search(pcm, ref=ADPCM4_REF, scale=0, zeros=(), jobs=1,
                  seg=None, lead=4096):
    """PCM8 -> ADPCM4 by EXACT SEARCH (Viterbi): the nibble sequence whose
    decode has the least total squared error, where adpcm4_encode picks
    each nibble for its own sample alone. The decoder has 1,024 states - a
    sample and a scale of 0, 16, 32 or 48 - so the trellis is a numpy step
    a sample. MEASURED on 12 s of Bad Apple's sound: 27.3 dB against the
    greedy encoder's 19.9. `zeros` as adpcm4_encode's.

    ON `jobs` CORES the stream is cut into segments - a core's share of it,
    from ADPCM4_SEG_MIN samples up to ADPCM4_SEG - and each is searched
    from `lead` samples BEFORE its cut, starting from any state: survivors
    merge within tens of samples, so over the lead its path becomes the one
    the whole search would have taken. The two are stitched at the latest
    sample where their STATES agree - the prefix is the best way into that
    state and the rest the best way on from it, which is the whole search's
    path through it - and a segment whose lead never meets the one before
    it is searched again from where that one ended. A step that would
    CLAMP at 0 or 255 is a second route into an edge state and is not
    taken; every stream written decodes by the card's arithmetic."""
    if len(pcm) % 2:
        raise V88Error("ADPCM4 packs two samples a byte; %d is odd"
                       % len(pcm))
    n = len(pcm)
    s0 = (scale // 16) * 256 + ref
    if seg is None:     # A SEGMENT A CORE: a clip whose sound is under two
        seg = min(ADPCM4_SEG,   # of the long ones was searched on one
                  max(ADPCM4_SEG_MIN, -(-n // max(1, jobs))))
    if jobs <= 1 or n <= 2 * seg:
        nibs, _ = _adpcm4_viterbi(pcm, s0, zeros)
    else:
        import multiprocessing
        import numpy as np
        cuts = list(range(seg, n, seg))
        bounds = [0] + cuts + [n]
        work = []
        for i in range(len(bounds) - 1):
            a = bounds[i] - (lead if i else 0)
            b = bounds[i + 1]
            work.append((bytes(pcm[a:b]), None if i else s0,
                         [z - a for z in zeros if a < z <= b]))
        with multiprocessing.Pool(jobs) as pool:
            parts = pool.map(_adpcm4_seg, work)
        nibs = np.zeros(n, np.uint8)
        states = np.zeros(n, np.int32)
        nibs[:bounds[1]], states[:bounds[1]] = parts[0]
        for i in range(1, len(bounds) - 1):
            a, c, b = bounds[i] - lead, bounds[i], bounds[i + 1]
            pn, ps = parts[i]
            m = None                # the latest sample both agree after
            for k in range(c - 1, a - 1, -1):
                if states[k] == ps[k - a]:
                    m = k
                    break
            if m is None:           # never met: on from where it really is
                rn, rs = _adpcm4_viterbi(
                    pcm[c:b], int(states[c - 1]),
                    [z - c for z in zeros if c < z <= b])
                nibs[c:b], states[c:b] = rn, rs
            else:
                nibs[m + 1:b] = pn[m + 1 - a:]
                states[m + 1:b] = ps[m + 1 - a:]
    out = bytes((int(nibs[i]) << 4) | int(nibs[i + 1])
                for i in range(0, n, 2))
    st = adpcm4_trace(out, ref, scale)
    for z in zeros:
        if 0 < z <= n and z % 2 == 0 and st[z // 2][1]:
            raise V88Error("adpcm4_search: the scale is %d at sample %d"
                           % (st[z // 2][1], z))
    return out


def adpcm4_join(data, pcm, target, zeros=(), after=0, tail=8192):
    """A RESIDENT file's lap join made exact (98.1.7.2): `data` (the ADPCM4
    of `pcm`, from 80h at scale 0) with its last samples searched again so
    the decoder ENDS in `target`, a (sample, scale) - the state the player's
    join continues from. Only the tail after sample `after` (and the last
    `tail` samples) is changed, so a state the target was read before it
    stands. `zeros` as adpcm4_encode's"""
    n = len(pcm)
    c = max(after, n - tail)
    c += c % 2
    if c >= n:
        raise V88Error("ADPCM4: no samples after %d to steer the join with"
                       % after)
    st = adpcm4_trace(data[:c // 2])
    r0, q0 = st[-1]
    nibs, _ = _adpcm4_viterbi(bytes(pcm[c:]), (q0 // 16) * 256 + r0,
                              [z - c for z in zeros if c < z <= n],
                              end=(target[1] // 16) * 256 + target[0])
    out = bytes(data[:c // 2]) + bytes(
        (int(nibs[i]) << 4) | int(nibs[i + 1]) for i in range(0, n - c, 2))
    if adpcm4_trace(out)[-1] != tuple(target):
        raise V88Error("ADPCM4: the join ends at %s, not %s"
                       % (adpcm4_trace(out)[-1], target))
    return out


def adpcm4_trace(data, ref=ADPCM4_REF, scale=0):
    """The decoder's (sample, scale) before each byte of `data`, and after
    the last: [i] is the state a card started at byte i must be in"""
    st = [(ref, scale)]
    for b in data:
        for nib in (b >> 4, b & 15):
            ref, scale = _adpcm4_step(ref, scale, nib)
        st.append((ref, scale))
    return st


def key_frames(nf, keyint, first=0):
    """The frames keyframes are written after (98.1.3): every keyint-th,
    from `first` - which is 0 unless the encoder PRE-ROLLED the first picture
    (98.2.9)"""
    return list(range(first, nf, keyint))


def audio_chunks(pcm, nf, spf, afmt, keys=(), search=0, join=None):
    """A stream's PCM8 cut into the frames' audio parts in format afmt: PCM8
    a sample a byte, or ADPCM4 encoded ONCE across the whole stream - its
    state runs on from frame to frame, the reference byte being the
    player's (SPEC.md 98.1.1) - with the scale steered to 0 at frame k+1 of
    every keyframe k in `keys`, where a seek starts the card afresh.

    `join` is a RESIDENT file's lap (98.1.7.2), and ends the stream in the
    state the player's join continues from: "start" - the join queues a
    frame of silence (nibbles of 0) and then frame 0, which was encoded from
    80h at scale 0, so the stream ends THERE - or an int L, the seam, which
    queues frame L's sound and goes on, so it ends in the state before
    frame L's."""
    pcm = bytes(pcm[:nf * spf]) + b"\x80" * max(0, nf * spf - len(pcm))
    if afmt == AUD_ADPCM4:
        if spf % 2:
            raise V88Error("ADPCM4 needs an even number of samples a frame, "
                           "and this stream has %d" % spf)
        zs = [(k + 1) * spf for k in keys]
        # `search`: 0 the greedy encoder, n > 0 the exact search on n cores
        data = adpcm4_search(pcm, zeros=zs, jobs=search) if search \
            else adpcm4_encode(pcm, zeros=zs)
        n = spf // 2
        if join is not None:
            if join == "start":
                target, after = (ADPCM4_REF, 0), 0
            else:
                target = adpcm4_trace(data[:join * n])[-1]
                after = (join + 1) * spf
            data = adpcm4_join(data, pcm, target, zs, after)
    else:
        data, n = pcm, spf
    return [data[f * n:(f + 1) * n] for f in range(nf)]


def encode_frames(paths, out, fps, wav=None, layout="cga", title="",
                  keysecs=KEY_SECS, poster=None, audio_fmt=AUD_PCM8,
                  loop=None, repeat=False, resident=None, live=None,
                  spk=False, spkp=1, ahead=0, spcap=SP_MAX, kpart=KEY_PART):
    """SPEC.md 98.2's minimal encoder: every changed byte, losslessly.
    `kpart` is a key's read at most (98.1.3.8) - a gate's small one makes
    KEY PARTS of a small clip's keys
    `ahead` carries the sound that many frames ahead (98.1.8), `spcap` packs
    super-packets to that many sectors (BIGSP past SP_MAX, 98.1.4.1)
    `spk` stores PCM8 as the speaker's counts (98.1.1.3)
    `live` (cga, herc, vga) makes the file LIVE for that screen (98.3.10) -
    the layout must be lin80. Resident, or a stream played Live once it is
    held in XMS (98.3.18.1)"""
    lay = LAYOUT_BY_NAME[layout]
    w0, h0, _ = read_frame(paths[0])
    if w0 % 8:
        raise V88Error("%s: %d pixels wide; a MONO1 canvas is whole bytes"
                       % (paths[0], w0))
    g = Geom(lay, w0 // 8, h0)
    if wav:
        rate, samples = read_wav(wav)
        spf = max(1, round(rate / fps))
        afmt, abytes = audio_fmt, spf
        if afmt == AUD_ADPCM4:
            spf += spf % 2              # two samples a byte: the frame rate
            abytes = spf // 2           # moves by a hair to keep it even
        chunks = audio_chunks(samples, len(paths), spf, afmt, key_frames(
            len(paths), max(1, round(keysecs * rate / spf))),
            join=(loop if loop is not None else "start")
            if resident is not None and afmt == AUD_ADPCM4 else None)
        if spk:
            chunks = [spk_counts(c, rate, spkp) for c in chunks]
    else:
        rate, spf, afmt, abytes = max(1, round(fps * 100)), 100, AUD_NONE, 0
        samples = b""
    if rate > 65535:
        raise V88Error("a %d Hz rate does not fit the header" % rate)
    rab = abytes
    if resident is not None:            # 98.1.7: silent records, the sound
        rab = 0                         # one block beside them
    wr = Writer(g, rate, spf, afmt if rab else AUD_NONE, rab, PF_MONO1,
                title=title, keysecs=keysecs, loop=loop, repeat=repeat,
                spk=spk and bool(rab), spkp=spkp if spk and rab else 1,
                live=TARGETS[live] if live and resident is None else None,
                ahead=ahead if rab else 0, spcap=spcap)
    wr.kpart = kpart
    if resident is not None or live:    # (in its block, or decoded into the
        wr.kpart = None                 # shadow off one read: one record
                                        # each, 98.1.3.8)
    surf = bytearray(65536)
    for f, path in enumerate(paths):
        w, h, cv = read_frame(path)
        if (w, h) != (w0, h0):
            raise V88Error("%s is %d x %d; the first frame was %d x %d"
                           % (path, w, h, w0, h0))
        changed = []
        for y, b in enumerate(g.base):
            row = cv[y * g.wb:(y + 1) * g.wb]
            if row == surf[b:b + g.wb]:
                continue
            for x in range(g.wb):
                if surf[b + x] != row[x]:
                    changed.append(b + x)
            surf[b:b + g.wb] = row
        wr.frame(spans(changed, surf, g), surf, chunks[f] if rab else b"")
    if resident is not None:
        return write_resident(out, [wr], afmt, abytes,
                              b"".join(chunks) if abytes else b"",
                              title=title, repeat=repeat, pack=resident,
                              posters=[poster],
                              live=[TARGETS[live]] if live else None,
                              spk=spk and bool(abytes))
    return wr.write(out, poster)


def encode_canvases(canvases, g, out, fps, pixfmt=PF_MONO1, palette=None,
                    title="", keysecs=KEY_SECS, poster=None, rowscale=1,
                    flip=False, loop=None, repeat=False, cgapal=None,
                    screen=SCR_480, pev=None, subs=None, kpart=KEY_PART,
                    held=()):
    """encode_frames for canvases already in hand (bytes, g.wb a row) - the
    only way to make a VGA8 file without a video (SPEC.md 98.2): silent,
    every changed byte. `pev`: a palette a scene's table (98.1.3.2.3).
    `subs(cv, prev, g)`: a VGA4 frame's sub-records in place of vga4_subs'
    (a SUB2 file's colour ones, 98.1.3.2.4). `held`: the frames whose
    records are HELD (98.1.3.9, a flipped file's) - drawn, not shown"""
    rate, spf = max(1, round(fps * 100)), 100
    wr = Writer(g, rate, spf, AUD_NONE, 0, pixfmt, title=title,
                keysecs=keysecs, palette=palette, rowscale=rowscale,
                flip=flip, loop=loop, repeat=repeat, cgapal=cgapal,
                screen=screen)
    wr.pev = pev
    wr.kpart = kpart
    surf = g.surface()
    prev = g.canvas(surf)
    for k, cv in enumerate(canvases):
        if g.planes > 1:
            ops = (subs or vga4_subs)(cv, prev, g) if g.bitplanes else \
                modex_subs(cv, [a != b for a, b in zip(cv, prev)], g)
            g.put(surf, cv)
            prev = cv
            wr.frame(ops, surf, held=k in held)
            continue
        changed = []
        for y, b in enumerate(g.base):
            row = cv[y * g.wb:(y + 1) * g.wb]
            if row == surf[b:b + g.wb]:
                continue
            for x in range(g.wb):
                if surf[b + x] != row[x]:
                    changed.append(b + x)
            surf[b:b + g.wb] = row
        wr.frame(spans(changed, surf, g), surf)
    return wr.write(out, poster)


def v88_layered(r, check=True, drop=None):
    """(frame, the ENHANCED surface after it) - the base's record then the
    layer's, for every frame (98.1.9). `drop(f)` true leaves frame f's layer
    record out, as a late player does"""
    surf = r.g.surface()
    lit = r.layer_records()
    # PAGE-AHEAD (98.1.3.7, 98.2.1.8.3): two pages, as v88_frames keeps
    # them - the base's record on the page shown (or flipped to), its
    # hidden record on the other, key 0's preload - and the layer's record
    # on the page shown
    pages, vis = [surf, r.g.surface()], 0
    k0 = r.keys[0][0] if r.page and r.keys else None
    for f, (rec, at, i) in enumerate(r.records()):
        if r.page:
            vis, _ = r.apply_page(pages, vis, rec, check=False)
            if f == k0:
                kp, _ = r.key_pages(r.key(0)[1], check=False)
                pages[vis ^ 1][:] = kp[1]
            cur = pages[vis]
        else:
            r.apply(surf, rec, check=False)     # (verify_v88 checks the
            cur = surf                          # base)
        lf, lrec = next(lit, (None, None))[:2]
        if lf != f:
            raise V88Error("the layer has no record for frame %d" % f)
        if lrec is not None and not (drop and drop(f)) and \
                not (r.page and f == k0):   # (a page-ahead play starts AT
            r.apply(cur, lrec, check=check, layer=True)     # key 0: its
        yield f, cur                        # frame's layer is never drawn)


def verify_layer(r):
    """THE LAYER (98.1.9): a record for every frame, each by the writer's
    rules, the run within the header's sizes, and the key table
    naming each key's next frame where the chain holds it"""
    lpos = {}
    n = 0
    for f, lrec, at in r.layer_records():
        lpos[f] = at
        n += 1
    if n != r.frames:
        raise V88Error("the layer holds %d frames; the stream %d"
                       % (n, r.frames))
    for f, surf in v88_layered(r):
        pass
    for i, (k, off, ln, spo, spk, idx) in enumerate(r.keys):
        want = lpos[k + 1] if k + 1 < r.frames else 0xFFFFFFFF
        if r.layer_key(i) != want:
            raise V88Error("layer key %d names %s; the layer holds frame %d "
                           "at %s" % (i, r.layer_key(i), k + 1, want))


def v88_frames(r, check=True):
    """(frame, surface after it) for every frame of the stream, checking
    each record; and the stream's own totals against the header's."""
    surf = r.g.surface()
    f = 0
    pages, vis = [surf] + [r.g.surface() for _ in range(r.pages - 1)], 0
    k0 = r.keys[0][0] if r.page and r.keys else None
    r.flast = None
    for rec, at, i in r.records():
        if r.mpage:                 # (98.1.3.7.1: every page by number)
            vis, _ = r.apply_page(pages, vis, rec, check=check)
            if f == k0:             # key 0 PRELOADS every page it carries
                kp, kvis, _ = r.key_view(r.key(0)[1], check=False)
                if kvis != vis:
                    raise V88Error("key 0 shows page %d; the stream page %d"
                                   % (kvis, vis))
                for n in range(r.pages):
                    if n != vis:
                        pages[n][:] = kp[n]
                r.flast = None      # (a play from it brings nothing up)
            r.mpages, r.vis = pages, vis
            r.hidden = None
            yield f, pages[vis], rec, at, i
        elif r.page:                # (98.1.3.7: the page shown, and the
            vis, _ = r.apply_page(pages, vis, rec, check=check)
            if f == k0:             # every play starts at key 0, whose
                kp, _ = r.key_pages(r.key(0)[1], check=False)  # hidden
                pages[vis ^ 1][:] = kp[1]   # page is PRELOADED (98.1.3.7)
            r.hidden = pages[vis ^ 1]   # other for a key's check)
            yield f, pages[vis], rec, at, i
        else:
            r.apply(surf, rec, check=check)
            yield f, surf, rec, at, i
        f += 1
    if f != r.frames:
        raise V88Error("the stream holds %d frames; the header says %d"
                       % (f, r.frames))


def cmd_import(a):
    s = import_xdv(a.src, a.out, a.target, a.title, a.keysecs, a.poster,
                   AUD_BY_NAME[a.audio])
    print("os88vid: %s -> %s: %d bytes, %d super-packets, %d keyframes "
          "(%.1f%% of the file), poster %d"
          % (a.src, a.out, s["bytes"], s["sps"], s["keys"],
             100.0 * s["keybytes"] / s["bytes"], s["poster"]))


def cmd_encode(a):
    s = encode_frames(a.frames, a.out, a.fps, a.wav, a.layout, a.title,
                      a.keysecs, a.poster, AUD_BY_NAME[a.audio])
    print("os88vid: %d frames -> %s: %d bytes, %d keyframes (%.1f%%)"
          % (len(a.frames), a.out, s["bytes"], s["keys"],
             100.0 * s["keybytes"] / s["bytes"]))


def cmd_poster(a):
    r = Reader(a.file)
    if a.key is not None:
        if not 0 <= a.key < r.nkeys:
            raise V88Error("--key %d: there are %d keyframes"
                           % (a.key, r.nkeys))
        f = r.keys[a.key][0]
    else:
        f = a.frame
    ps = set_poster(a.file, f)
    print("os88vid: %s: poster %s (frame %d)"
          % (a.file, "/".join(str(p) for p in ps), Reader(a.file).keys[ps[0]][0]))


def cmd_title(a):
    t = set_title(a.file, a.title)
    print("os88vid: %s: title '%s'" % (a.file, t))


def cmd_spkwav(a):
    r = Reader(a.file, a.rendition)
    if not r.spk:
        raise V88Error("%s is not made for the speaker (98.1.1.3)" % a.file)
    c = b"".join(r.sound(f) for f in range(r.frames))
    secs = write_spk_preview(a.out, c, r.rate, r.spkp)
    print("os88vid: %s: %.1f s of the speaker line at %d Hz x %d pulse%s"
          % (a.out, secs, r.rate, r.spkp, "" if r.spkp == 1 else "s"))


def cmd_speaker(a):
    st = SPK_STYLES[a.style]
    for k, v in (("highpass", "hp"), ("ratio", "ratio"), ("range", "rng")):
        if getattr(a, k) is None:
            setattr(a, k, st[v])
    n = spk_reshape(a.file, a.out, a.highpass, a.drive, a.lows, a.range,
                    a.ratio, a.idle, a.style)
    print("os88vid: %s: %d rendition%s' sound shaped for the speaker "
          "(high-pass %d Hz, drive %.2f)" % (a.out, n, "" if n == 1 else "s",
                                             a.highpass, a.drive))


def cmd_info(a):
    for path in a.files:
        r = Reader(path)
        g = r.g
        secs = r.frames / r.fps
        cyc = [cycles_of(rec, r.g.planes > 1, r.g.layout, sub2=r.g.sub2)
               for rec, at, i in r.records()]
        period = HZ / r.fps
        print("%s: '%s'" % (path, r.title))
        if r.credits:
            print("   credits: %s" % r.credits)
        print("   %d frames at %.3f fps (%d Hz / %d), %.1f s; audio %s; "
              "PIT %d x %d" % (r.frames, r.fps, r.rate, r.spf, secs,
                               {0: "none", 1: "PCM8", 2: "ADPCM4"}[r.audio]
                               + (" as speaker counts" if r.spk else "")
                               + (", %d frames ahead" % r.ahead
                                  if r.ahead else ""),
                              r.pitdiv,
                               r.pitper))
        print("   canvas %d x %d%s on %s, %s, aspect %d:%d"
              % (g.wb // 2 if r.pixfmt in (PF_C512, PF_TEXT) else
                 g.wb * (4 if r.pixfmt == PF_CGA4 else
                         PIX_PER_BYTE[g.layout]), g.h,
                 " (each row shown twice)" if r.rowscale > 1 else "",
                 g.name, PF_NAMES[r.pixfmt], *r.aspect))
        if r.page and r.pixfmt not in (PF_VGA4, PF_VGA8):
            print("   two pages, page-ahead (%d flips)" % sum(
                1 for rec, _, _ in r.records()
                if struct.unpack_from("<H", rec, 2)[0] & REC_FLIP))
        if r.pixfmt in (PF_VGA4, PF_VGA8) and (r.screen or r.flip or
                                               r.page or (
                r.pixfmt == PF_VGA4 and r.palette)):
            print("   %s%s%s" % (
                "screen %s, full screen; " % SCREENS[r.screen][0]
                if r.screen else "full screen; " if r.palette and
                r.pixfmt == PF_VGA4 else "", "two pages, flipped"
                if r.flip else "%d pages, flipped and built ahead (%d built "
                "flips)" % (r.pages, sum(
                    1 for rec, _, _ in r.records()
                    if struct.unpack_from("<H", rec, 4)[0] & REC_BUILT))
                if getattr(r, "fpage", False) else
                "%s pages, page-ahead (%d flips)" % (
                    r.pages if r.mpage else "two", sum(
                        1 for rec, _, _ in r.records()
                        if struct.unpack_from("<H", rec, 2)[0] & REC_FLIP))
                if r.page else "one page",
                "; its own 16 colours%s" % (
                    ", a palette for each of %d scenes" % len(r.pev)
                    if r.pev else "") if r.pixfmt == PF_VGA4 and
                r.palette else ""))
        if r.pixfmt == PF_CGA4:
            print("   palette %02Xh: colours %s" % (
                r.cgapal, " ".join(str(c) for c in cga4_colours(r.cgapal))))
        elif r.pixfmt == PF_TEXT:
            print("   text mode, %s" % (
                "colour: a CGA, an EGA or a VGA" if r.cgapal else
                "mono: any adapter, an MDA or a Hercules too"))
        elif r.pixfmt == PF_C512:
            print("   composite, made for %s CGA" % {
                CARD_OLD: "the OLD", CARD_NEW: "the NEW",
                CARD_BOTH: "either"}[r.cgapal])
        print("   stream %d bytes = %.1f KB/s; largest super-packet %d "
              "sectors, largest record %d"
              % (r.slen, r.slen / 1024.0 / secs, r.spmax, r.rmax))
        if r.lsp0:                      # THE LAYER (98.1.9): to the end
            lyb = len(r.d) - r.lsp0     # of the file, its key table in it
            print("   layer %d bytes = %.1f KB/s, read only by a machine "
                  "that plays it" % (lyb, lyb / 1024.0 / secs))
        if r.bench[0]:
            print("   bench (98.1.9.13): %.2f ms on its machine%s"
                  % (r.bench[0] / 100.0, ", %.2f on the layer's"
                     % (r.bench[1] / 100.0) if r.bench[1] else ""))
        print("   %d keyframes, poster %s; front matter %d bytes"
              % (r.nkeys, r.poster if r.poster != 0xFFFF else "none", r.sp0))
        print("   CPU (wave 0 model, CGA screen): mean %.1f%%, worst %.1f%% "
              "(frame %d)" % (100 * sum(cyc) / len(cyc) / period,
                              100 * max(cyc) / period, cyc.index(max(cyc))))


def palette_at(r, n):
    """The sixteen (or the 768-byte palette) the screen after frame `n` is
    shown in: the header's, or with a palette a scene (98.1.3.2.3) the
    last table entry at or before n"""
    if not r.pev:
        return r.palette
    p = r.pev[0][1]
    for f, q in r.pev:
        if f > n:
            break
        p = q
    return p + bytes(PAL_BYTES - 48)


def decode_at(r, n):
    """The canvas after frame `n`, through the keyframe at or before it -
    the one SHOWN: through a HELD record's run, the frame before it
    (98.1.3.9)."""
    if not 0 <= n < r.frames:
        raise V88Error("frame %d of %d" % (n, r.frames))
    surf = r.g.surface()
    pages, vis = [surf] + [r.g.surface() for _ in range(r.pages - 1)], 0
    f = 0
    r.flast = None
    best = [i for i, e in enumerate(r.keys) if e[0] <= n]
    if best:
        k, rec, spo, spn, idx = r.key(best[-1])
        if r.mpage:                 # (98.1.3.7.1: every page, by number,
            pages, vis, _ = r.key_view(rec)     # and the one it shows)
            surf = pages[vis]
            r.flast = None          # (98.1.3.7.2: nothing to bring up)
        elif r.page:                # (98.1.3.7: both pages)
            pages, _ = r.key_pages(rec)
            surf = pages[0]
        else:
            r.apply(surf, rec, key=True)
        if k == n:
            return r.g.canvas(surf)
        f = k + 1
        src = r.after_key(r.keys[best[-1]])
    else:
        src = r.records()
    shown = None                    # (98.1.3.9: what the glass shows
    for rec, _, _ in src:           # through a run of HELD records)
        held = getattr(r, "held", False) and \
            struct.unpack_from("<H", rec, 2)[0] & REC_HELD
        if held and shown is None:
            shown = bytes(surf)
        elif not held:
            shown = None
        if r.page:
            vis, _ = r.apply_page(pages, vis, rec)
            surf = pages[vis]
        else:
            r.apply(surf, rec)
        if f == n:
            return r.g.canvas(shown if shown is not None else surf)
        f += 1
    raise V88Error("the stream ended before frame %d" % n)


# THE POSTER (SPEC.md 98.4): the Preview shows a keyframe at the scale the
# window's layout chose (98.4.1) - the canvas itself, or halved 2x2 into 1,
# each output pixel lit when its four source pixels hold MORE lit ones than
# its threshold (2x2 ordered dither, by output row and column parity), so a
# grey stays grey and a one-pixel line survives; or that halved again.
THUMB_T = ((0, 2), (3, 1))


def thumb_half(cv, wb, h):
    """(bytes, wb, h) of the canvas `cv` halved - apps/video/video.asm's
    vp_half, bit for bit. An odd last row pairs with itself; an odd last
    byte's missing partner is black."""
    owb, oh = (wb + 1) // 2, (h + 1) // 2
    out = bytearray(owb * oh)
    for y in range(oh):
        up = cv[2 * y * wb:(2 * y + 1) * wb]
        lo = cv[(2 * y + 1) * wb:(2 * y + 2) * wb] if 2 * y + 1 < h else up
        t = THUMB_T[y & 1]
        for j in range(owb):
            v = 0
            for s in (0, 1):
                a = up[2 * j + s] if 2 * j + s < wb else 0
                b = lo[2 * j + s] if 2 * j + s < wb else 0
                for p in range(4):
                    sh = 6 - 2 * p
                    n = bin((a >> sh) & 3).count("1") + \
                        bin((b >> sh) & 3).count("1")
                    if n > t[p & 1]:
                        v |= 0x80 >> (4 * s + p)
            out[y * owb + j] = v
    return bytes(out), owb, oh


def poster(cv, wb, h, scale=2):
    """(bytes, bytes a row, width in pixels, rows) of the picture the
    Preview's box shows for canvas `cv` at `scale` 1, 2 or 4 (SPEC.md 98.4)"""
    if scale == 1:
        return bytes(cv), wb, wb * 8, h
    img, bw, bh = thumb_half(cv, wb, h)
    if scale == 4:
        img, bw, bh = thumb_half(img, bw, bh)
    return img, bw, wb * 8 // scale, bh


def cmd_decode(a):
    r = Reader(a.file)
    cv = decode_at(r, a.frame)
    if r.pixfmt == PF_TEXT:             # the cells, in the model's face
        import numpy as np              # (98.1.3.6): numpy's and PIL's
        from PIL import Image
        import os88txtfont
        rgb = os88txtfont.render(cv, r.g.wb, r.g.h, np.frombuffer(
            STD16, np.uint8).reshape(16, 3).astype(np.uint16) * 255 // 63)
        Image.fromarray(rgb).save(a.png)
    elif r.pixfmt in (PF_VGA8, PF_VGA4):    # a pixel a byte, through its
        import numpy as np                  # colours: the one-bit writer
        from PIL import Image               # read it as bits
        pal = np.frombuffer(r.palette or STD16,
                            np.uint8).astype(np.uint16).reshape(-1, 3)
        idx = np.frombuffer(cv, np.uint8).reshape(r.g.h, r.g.w)
        Image.fromarray((pal[idx] * 255 // 63).astype(np.uint8)).save(a.png)
    else:
        write_png(a.png, r.g.wb, r.g.h, cv)
    print("os88vid: frame %d of %s -> %s" % (a.frame, a.file, a.png))


def verify_v88(path, against=None, rend=None):
    """Everything SPEC.md 98.1.6 lets a reader check, and every writer's
    rule of 98.1.3, over the whole file - EVERY rendition, unless `rend`
    names one. Returns the frame count."""
    if rend is None:
        n = Reader(path).nrend
        for i in range(1, n):
            verify_v88(path, against, i)
        rend = 0
    r = Reader(path, rend)
    r.options()                     # a block this writer wrote must READ
    g = r.g
    ref = None
    if against:
        hdr, pk = read_xdv(against)
        if len(pk) != r.frames:
            raise V88Error("%s has %d frames, %s %d" % (against, len(pk),
                                                       path, r.frames))
        ref = (hdr, pk, bytearray(65536), Geom(LAY_CGA, ROWB, 200))
        want_audio = audio_chunks(b"".join(xdc_audio(p, hdr) for p in pk),
                                  len(pk), hdr["achunk"], r.audio,
                                  [e[0] for e in r.keys]) \
            if r.audio == AUD_ADPCM4 else None
    ks = [e[0] for e in r.keys]
    if ks != sorted(set(ks)) or (ks and ks[-1] >= r.frames):
        raise V88Error("the keyframe table is not ascending frames of the "
                       "stream")
    kat = {k: i for i, k in enumerate(ks)}
    pos, rmax, kmax = {}, 0, 0
    seen, later = [], []            # (page-ahead: each frame's two pages)
    adj = r.abytes if r.resident else 0     # a block's records carry none
    for f, surf, rec, at, i in v88_frames(r):
        pos[f] = (at, i)
        rmax = max(rmax, len(rec) - adj)
        if ref:
            hdr, pk, xs, cga = ref
            ops, code = xdc_ops(pk[f], "%s packet %d" % (against, f))
            apply_ops(xs, ops)
            if cga.canvas(xs) != g.canvas(surf):
                raise V88Error("frame %d differs from XDC's screen" % f)
            if r.sound(f) != (want_audio[f] if want_audio
                              else xdc_audio(pk[f], hdr)):
                raise V88Error("frame %d's audio differs from XDC's" % f)
        if r.mpage:                     # (98.1.3.7.1: the page shown,
            seen.append((hash(bytes(g.canvas(surf))), r.vis, tuple(
                hash(bytes(g.canvas(pg))) for pg in r.mpages)))  # and all)
        elif r.page:
            seen.append((hash(bytes(g.canvas(surf))),
                         hash(bytes(g.canvas(r.hidden)))))
        if f in kat:
            k, krec, spo, spk, idx = r.key(kat[f])
            kmax = max(kmax, max(key_reads(krec)) +
                       (0 if r.kleadat else len(r.key_lead(kat[f]))))
            kb = g.surface()
            if r.mpage:                 # (98.1.3.7.1: the page it names
                kp, kvis, _ = r.key_view(krec)  # is the page shown, and
                if kvis != r.vis:               # the rest as for two)
                    raise V88Error("keyframe %d shows page %d; the stream "
                                   "page %d" % (kat[f], kvis, r.vis))
                kb = kp[kvis]
                if any(g.canvas(kp[n]) != g.canvas(r.mpages[n])
                       for n in range(r.pages) if n != kvis):
                    later.append(kat[f])
            elif r.page:                # (98.1.3.7: and the hidden page,
                kp, _ = r.key_pages(krec)   # or one a play from the key
                kb = kp[0]                  # comes back to the stream from)
                if g.canvas(kp[1]) != g.canvas(r.hidden):
                    later.append(kat[f])
            else:
                r.apply(kb, krec, key=True)
            if g.canvas(kb) != g.canvas(surf):
                raise V88Error("keyframe %d is not the screen after frame %d"
                               % (kat[f], f))
    for i in later:
        # A KEY THAT PRELOADS (98.1.3.7): its hidden page is not the stream's
        # - it holds the next flip's picture whole - so a play from it must
        # SHOW what the stream shows, frame for frame, until both its pages
        # are the stream's again
        e = r.keys[i]
        if r.mpage:                     # (98.1.3.7.1: by number, from the
            pages, vis, _ = r.key_view(r.key(i)[1])     # page it shows)
            r.flast = None
            f = e[0] + 1
            for rec, _, _ in r.after_key(e):
                vis, _ = r.apply_page(pages, vis, rec, check=False)
                hv = tuple(hash(bytes(g.canvas(pg))) for pg in pages)
                if hv[vis] != seen[f][0] or vis != seen[f][1]:
                    raise V88Error("a play from keyframe %d shows frame %d "
                                   "wrong" % (i, f))
                if hv == seen[f][2]:
                    break
                f += 1
            continue
        pages, _ = r.key_pages(r.key(i)[1])
        vis, f = 0, e[0] + 1
        for rec, _, _ in r.after_key(e):
            vis, _ = r.apply_page(pages, vis, rec, check=False)
            hv = (hash(bytes(g.canvas(pages[vis]))),
                  hash(bytes(g.canvas(pages[vis ^ 1]))))
            if hv[0] != seen[f][0]:
                raise V88Error("a play from keyframe %d shows frame %d "
                               "wrong" % (i, f))
            if hv == seen[f]:
                break
            f += 1
    if r.lsp0:
        verify_layer(r)
    sp_secs = {}
    at, n = r.sp0, r.sp0n
    while n:
        sp_secs[at] = n
        at, n = at + n * SECTOR, struct.unpack_from("<H", r.d, at + 2)[0]
    if r.resident:
        pos = {f: (0, 0) for f in pos}
        sp_secs = {0: 0}
    elif at - r.sp0 != r.slen or max(sp_secs.values()) != r.spmax:
        raise V88Error("the stream is %d bytes, largest %d sectors; the "
                       "header says %d and %d" % (at - r.sp0,
                                                 max(sp_secs.values()),
                                                 r.slen, r.spmax))
    for k, off, n, spo, spk, idx in r.keys:
        want = (pos[k + 1] + (sp_secs[pos[k + 1][0]],)) \
            if k + 1 < r.frames and not r.resident else (0, 0, 0)
        if (spo, idx, spk) != want:
            raise V88Error("keyframe after frame %d names super-packet %d "
                           "record %d (%d sectors); the stream says %s"
                           % (k, spo, idx, spk, want))
    if r.audio == AUD_ADPCM4 and r.keys:
        # 98.1.1.1: each keyframe's reference byte is the sample the decoder
        # holds at frame k+1, with its scale 0 - so a card started there with
        # it plays exactly what the continuous stream plays
        st = adpcm4_trace(b"".join(r.sound(f) for f in range(r.frames)))
        for i in range(len(r.keys)):
            k, krec = r.key(i)[:2]
            want = st[(k + 1) * r.abytes]
            if (krec[-1], 0) != want:
                raise V88Error("keyframe %d's ADPCM4 reference is %d; the "
                               "stream holds %d at scale %d there"
                               % (i, krec[-1], want[0], want[1]))
    if r.resident and r.audio == AUD_ADPCM4:
        # 98.1.7.2: a RESIDENT file's lap joins EXACTLY - the stream ends in
        # the state its join continues from: the state before frame L's
        # sound with a seam, or 80h at scale 0 (a frame of silence, then
        # frame 0) without one
        st = adpcm4_trace(r._aud)
        want = st[r.loop[0] * r.abytes] if r.loop else (ADPCM4_REF, 0)
        if st[-1] != want:
            raise V88Error("the ADPCM4 sound ends at sample %d, scale %d; "
                           "its lap joins at sample %d, scale %d"
                           % (st[-1] + want))
    if r.loop:
        # 98.1.1.2: the seam takes the last frame's screen to frame L's, with
        # frame L's audio, and the loop block names frame L+1's place
        L, off, n, secs, idx, spo = r.loop
        last = g.surface()
        want = None
        for f, surf, rec, at, i in v88_frames(r, check=False):
            if f == L:
                want = g.canvas(surf)
                # the seam carries frame L's sound, or - one more record in
                # play order, sound ahead (98.1.8) - frame L + A's
                laud = r.sound(L + r.ahead if L + r.ahead < r.frames else
                               L + (L + r.ahead - r.frames) %
                               (r.frames - L)) if r.abytes else b""
            if f == L + 1 and not r.resident and (spo, idx, secs) != \
                    (at, i, sp_secs[at]):
                raise V88Error("the loop block names super-packet %d record "
                               "%d (%d sectors) for frame %d; the stream "
                               "says %d, %d" % (spo, idx, secs, L + 1, at, i))
            last = surf
        tail = r.apply(last, r.seam())
        if g.canvas(last) != want:
            raise V88Error("the seam does not bring the last frame back to "
                           "frame %d" % L)
        if tail != laud:
            raise V88Error("the seam's audio is not frame %d's"
                           % (L + r.ahead))
        if r.flip and n > PREV_MAX and r.spcap == SP_MAX:
            raise V88Error("a flipped file's seam of %d bytes" % n)
    if r.ahead:
        # 98.1.8: every lead is the frames a play from there wants, and the
        # last A records carry the frames after the join a lap makes
        A, ab, N = r.ahead, r.abytes, r.frames
        J = r.loop[0] if r.loop else (r.keys[0][0] if r.keys else None)

        def want(q):
            if q < N:
                return r.sound(q)
            if J is None:
                return bytes([0 if r.audio == AUD_ADPCM4 else 0x80]) * ab
            return r.sound(J + (q - N) % (N - J))
        # (the start's lead is the only copy of frames 0 .. A - 1 - the
        # tail records below repeat them when the lap's join is key 0)
        for i, e in enumerate(r.keys):
            k = e[0]
            if r.key_lead(i) != b"".join(want(q)
                                         for q in range(k + 1, k + 1 + A)):
                raise V88Error("keyframe %d's lead is not frames %d to %d's "
                               "sound" % (i, k + 1, k + A))
        recs = [rec for rec, _, _ in r.records()]
        for p in range(N - A, N):
            if recs[p][len(recs[p]) - ab:] != want(p + A):
                raise V88Error("record %d does not carry the sound a lap "
                               "wants after its join" % p)
    if (rmax, kmax) != (r.rmax, r.kmax):
        raise V88Error("the largest record and keyframe are %d and %d bytes; "
                       "the header says %d and %d" % (rmax, kmax, r.rmax,
                                                     r.kmax))
    return r.frames


def cmd_verify(a):
    bad = 0
    for path in a.files:
        if path.upper().endswith(".XDV"):
            bad |= verify_xdv(path)
            continue
        try:
            n = verify_v88(path, a.against)
            print("%s: %d frames verified%s" % (
                path, n, (", every one XDC's screen and audio exactly"
                          if a.against else "")))
        except V88Error as e:
            print("%s: FAIL - %s" % (path, e))
            bad = 1
    return bad


# --------------------------------------------------------------------------
# --selfcheck: generated fixtures, no samples needed
# --------------------------------------------------------------------------
def _fixture_canvases(wb, h, n, rnd):
    """`n` canvases that between them reach every list: a black start, a
    white fill (RUNL), noise (SLICEL), isolated bytes (absolute P1..P6),
    runs and slices of every short length, and a box that moves."""
    cv = bytearray(wb * h)
    out = []
    for f in range(n):
        kind = f % 8
        if kind == 1:
            cv = bytearray(b"\xff" * (wb * h))
        elif kind == 2:
            cv = bytearray(rnd.getrandbits(8) for _ in range(wb * h))
        elif kind == 3:
            for _ in range(40):
                a = rnd.randrange(wb * h - 6)
                m = rnd.randint(1, 6)
                cv[a:a + m] = bytes(rnd.getrandbits(8) for _ in range(m))
        elif kind == 4:
            for _ in range(12):
                a = rnd.randrange(wb * h - 300)
                m = rnd.choice((6, 7, 40, 255, 256, 300))
                cv[a:a + m] = bytes([rnd.getrandbits(8)]) * m
        elif kind == 5:
            for _ in range(12):
                a = rnd.randrange(wb * h - 300)
                m = rnd.choice((7, 16, 255, 256, 290))
                cv[a:a + m] = bytes(rnd.getrandbits(8) for _ in range(m))
        elif kind == 6:
            x, y = f % (wb - 4), (3 * f) % (h - 8)
            for r in range(8):
                cv[(y + r) * wb + x:(y + r) * wb + x + 4] = b"\x3c" * 4
        elif kind == 7:
            cv = bytearray(wb * h)
        out.append(bytes(cv))
    return out


def _write_pbm(path, wb, h, cv):
    with open(path, "wb") as f:
        f.write(b"P4\n%d %d\n" % (wb * 8, h) + bytes(255 - b for b in cv))


def _write_wav(path, rate, samples):
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(samples)) + b"WAVEfmt " +
                struct.pack("<IHHIIHH", 16, 1, 1, rate, rate, 1, 8) +
                b"data" + struct.pack("<I", len(samples)) + samples)


def _write_xdv(path, frames_ops, rate, achunk, mode, rnd):
    """A synthetic XDC stream in XDC_GLOB.PAS's layout, from xdc_emit's
    packets, each with its audio in its last `achunk` bytes."""
    pks = []
    for ops in frames_ops:
        raw = xdc_emit(ops, pad=False)
        aud = bytes(rnd.getrandbits(8) for _ in range(achunk))
        size = -(-(len(raw) + achunk) // 512) * 512
        pks.append(raw + bytes(size - len(raw) - achunk) + aud)
    hdr = bytearray(512)
    hdr[0:4] = b"XDCV"
    struct.pack_into("<HHHHBBBB", hdr, 4, len(pks),
                     max(len(p) for p in pks), achunk, rate, mode, 80, 200, 0)
    with open(path, "wb") as f:
        f.write(hdr + b"".join(pks) + bytes(len(p) // 512 for p in pks))


def selfcheck():
    import random
    import tempfile
    rnd = random.Random(8088)
    fails = []

    def expect_fail(what, path, mutate, why):
        """`mutate` the file; verify must refuse it, and for `why`."""
        d = mutate(bytearray(open(path, "rb").read()))
        bad = path + ".bad"
        open(bad, "wb").write(d)
        try:
            verify_v88(bad)
        except (V88Error, XdvError) as e:
            if why not in str(e):
                fails.append("a %s was refused for another reason: %s"
                             % (what, e))
            return
        fails.append("a %s was not refused" % what)

    def swap_keys(d, r):
        """Two keyframes' records swapped in the table: both well formed,
        each the wrong picture."""
        recs = [r.key(i)[1] for i in range(r.nkeys)]
        i = next(i for i in range(r.nkeys - 1) if recs[i] != recs[i + 1])
        a, b = r.ktab + 16 * i + 4, r.ktab + 16 * (i + 1) + 4
        d[a:a + 6], d[b:b + 6] = d[b:b + 6], d[a:a + 6]
        return d

    with tempfile.TemporaryDirectory() as tmp:
        for lay, wb, h, wav in (("cga", 80, 200, True), ("cga", 40, 100, False),
                                ("herc", 50, 200, True),
                                ("lin80", 40, 240, False)):
            cvs = _fixture_canvases(wb, h, 24, rnd)
            paths = []
            for i, cv in enumerate(cvs):
                pth = os.path.join(tmp, "%s%d_%03d.pbm" % (lay, wb, i))
                _write_pbm(pth, wb, h, cv)
                paths.append(pth)
            wv = None
            if wav:
                wv = os.path.join(tmp, "a.wav")
                _write_wav(wv, 8000, bytes(rnd.getrandbits(8)
                                           for _ in range(8000)))
            out = os.path.join(tmp, "%s%d.v88" % (lay, wb))
            encode_frames(paths, out, 30.0, wv, lay, "selfcheck", 0.2)
            try:
                verify_v88(out)
                r = Reader(out)
                for f in range(len(cvs)):
                    if decode_at(r, f) != cvs[f]:
                        fails.append("%s %dx%d frame %d decodes wrong"
                                     % (lay, wb, h, f))
                if wav and r.frames * r.abytes and r.audio != AUD_PCM8:
                    fails.append("%s: the audio was dropped" % lay)
                if r.nkeys < 3:
                    fails.append("%s: %d keyframes" % (lay, r.nkeys))
            except V88Error as e:
                fails.append("%s %dx%d: %s" % (lay, wb, h, e))
            # corruptions verify must refuse
            r = Reader(out)
            rec0 = r.sp0 + 4
            expect_fail("record length past its super-packet", out,
                        lambda d: d[:rec0] + b"\xff\x7f" + d[rec0 + 2:],
                        "says 32767 bytes")
            expect_fail("truncated file", out, lambda d: d[:len(d) - 700],
                        "runs off the file")
            expect_fail("stale keyframe", out, lambda d: swap_keys(d, r),
                        "is not the screen after frame")
            expect_fail("a ring no player has", out,
                        lambda d: d[:H_RING] + b"\x10" + d[H_RING + 1:],
                        "a ring of 16 slots")
            if wb < LAYOUTS[LAYOUT_BY_NAME[lay]][1]:
                # keyframe 0's record rewritten as one absolute P1 entry,
                # aimed one byte past the canvas's first row
                def outside(d):
                    kr = r.keys[0]
                    body = struct.pack("<HHH", kr[2], 0, 1) + \
                        bytes([0x81]) + struct.pack("<H", wb) + b"\x55" + \
                        bytes(kr[2] - 10)
                    return d[:kr[1]] + body + d[kr[1] + len(body):]
                expect_fail("write outside the canvas", out, outside,
                            "leaves the canvas")
        # the importer, on a synthetic XDC stream
        ops = [ops for label, ops in synth_frames()]
        ops += [[(0, b"\xaa" * 16384, True)], [(100, b"\x0f" * 3, False)]]
        xdv = os.path.join(tmp, "SYN.XDV")
        _write_xdv(xdv, ops, 8040, 134, 2, rnd)
        for tgt in ("cga", "herc", "lin80"):
            out = os.path.join(tmp, "syn_%s.v88" % tgt)
            import_xdv(xdv, out, tgt, keysecs=0.1)
            try:
                verify_v88(out, xdv)
            except (V88Error, XdvError) as e:
                fails.append("import --target %s: %s" % (tgt, e))
        png = os.path.join(tmp, "f.png")
        write_png(png, 80, 200, decode_at(Reader(os.path.join(
            tmp, "syn_cga.v88")), 3))
        if open(png, "rb").read(8) != b"\x89PNG\r\n\x1a\n":
            fails.append("the PNG writer")
        # ADPCM4: an import that re-encodes the sound, verified against the
        # XDC stream it came from, and a tone that survives the codec
        out = os.path.join(tmp, "syn_adpcm.v88")
        import_xdv(xdv, out, "cga", keysecs=0.1, audio_fmt=AUD_ADPCM4)
        try:
            if verify_v88(out, xdv) and Reader(out).abytes != 67:
                fails.append("ADPCM4 import: %d bytes a frame, not 67"
                             % Reader(out).abytes)
        except (V88Error, XdvError) as e:
            fails.append("ADPCM4 import: %s" % e)
        import math
        tone = bytes(128 + int(90 * math.sin(i * 2 * math.pi * 440 / 22050))
                     for i in range(4410))
        back = adpcm4_decode(adpcm4_encode(tone))
        err = max(abs(a - b) for a, b in zip(tone[200:], back[200:]))
        if len(back) != len(tone) or err > 24:
            fails.append("ADPCM4 does not carry a 440 Hz tone (worst "
                         "sample %d off)" % err)
        # ...and a SEEK into it plays the stream's own sound (98.1.1.1): the
        # keyframes' reference bytes, and a scale steered to 0 there
        rs = Reader(out)
        whole = adpcm4_decode(b"".join(rec[len(rec) - rs.abytes:]
                                       for rec, _, _ in rs.records()))
        for i in range(1, len(rs.keys)):
            k, rec, spo, spn, idx = rs.key(i)
            tail = b"".join(r[len(r) - rs.abytes:]
                            for r, _, _ in rs.records(spo, spn, idx))
            got = adpcm4_decode(tail, ref=rec[-1])
            if got != whole[len(whole) - len(got):]:
                fails.append("ADPCM4: a seek to keyframe %d does not play "
                             "the stream's sound" % i)
                break
    # REPEAT (98.1.1.2): a seam verifies, and a seam that does not bring the
    # last frame back to frame L is refused - as is a loop block past the
    # stream, and ADPCM4 with a seam at all
    with tempfile.TemporaryDirectory() as tmp:
        g = Geom(LAY_HERC, 20, 40)
        cvs = [bytes(rnd.getrandbits(8) if (i + f) % 9 == 0 else 0
                     for i in range(20 * 40)) for f in range(24)]
        out = os.path.join(tmp, "L.V88")
        encode_canvases(cvs, g, out, 15.0, loop=6, repeat=True)
        rl = Reader(out)
        if verify_v88(out) != 24 or rl.loop[0] != 6 or not rl.repeat:
            fails.append("a looped file did not read back as written")
        L, off, n = rl.loop[:3]
        expect_fail("wrong seam", out, lambda d: d[:off + n - 1] +
                    bytes([d[off + n - 1] ^ 0x5A]) + d[off + n:],
                    "")
        expect_fail("loop past the stream", out, lambda d: d[:LOOP_AT] +
                    struct.pack("<I", 24) + d[LOOP_AT + 4:], "loop block")
        try:
            Writer(g, 8000, 534, AUD_ADPCM4, 267, PF_MONO1, loop=3)
            fails.append("ADPCM4 with a seam was not refused")
        except V88Error:
            pass
        # SOUND AHEAD (98.1.8): the same clip with and without it gives
        # every frame the same sound, a seek from each key plays the stream's
        # own (its lead, then the records after it - ADPCM4's reference
        # byte included), the laps' tails verify, and a damaged lead, a byte
        # 25 with no flag, and a writer one frame out are refused
        acvs = _fixture_canvases(20, 40, 30, rnd)
        apaths = []
        for i, cv in enumerate(acvs):
            pth = os.path.join(tmp, "a_%03d.pbm" % i)
            _write_pbm(pth, 20, 40, cv)
            apaths.append(pth)
        awav = os.path.join(tmp, "ah.wav")
        _write_wav(awav, 8000, bytes(rnd.getrandbits(8) for _ in range(9000)))
        for afmt, lp in ((AUD_ADPCM4, None), (AUD_PCM8, 7), (AUD_PCM8, None)):
            o0 = os.path.join(tmp, "ah0.v88")
            o4 = os.path.join(tmp, "ah4.v88")
            kw = dict(keysecs=0.2, audio_fmt=afmt, loop=lp,
                      repeat=lp is not None)
            encode_frames(apaths, o0, 30.0, awav, "herc", **kw)
            encode_frames(apaths, o4, 30.0, awav, "herc", ahead=4, **kw)
            what = "%s%s" % ("ADPCM4" if afmt == AUD_ADPCM4 else "PCM8",
                             " with a seam" if lp else "")
            try:
                verify_v88(o4)
                r0, r4 = Reader(o0), Reader(o4)
                if r4.ahead != 4 or not r4.flags & F_AHEAD:
                    fails.append("ahead %s: the header says %d" % (what,
                                                                  r4.ahead))
                if any(r0.sound(f) != r4.sound(f) for f in range(r0.frames)):
                    fails.append("ahead %s: a frame's sound moved" % what)
                for f in range(r4.frames):
                    if decode_at(r4, f) != acvs[f]:
                        fails.append("ahead %s: frame %d decodes wrong"
                                     % (what, f))
                        break
                for i in range(r4.nkeys):
                    k, krec, spo, spn, idx = r4.key(i)
                    if k + 1 >= r4.frames:
                        continue
                    got = r4.key_lead(i) + b"".join(
                        rr[len(rr) - r4.abytes:]
                        for rr, _, _ in r4.records(spo, spn, idx))
                    want = b"".join(r4.sound(f) for f in range(k + 1,
                                                               r4.frames))
                    if afmt == AUD_ADPCM4:
                        got = adpcm4_decode(got, ref=krec[-1])
                        want = adpcm4_decode(b"".join(
                            r4.sound(f) for f in range(r4.frames)))
                        want = want[len(want) - (r4.frames - k - 1) *
                                    r4.spf:]
                    if got[:len(want)] != want:
                        fails.append("ahead %s: a seek to keyframe %d does "
                                     "not play the stream's sound"
                                     % (what, i))
                        break
            except V88Error as e:
                fails.append("ahead %s: %s" % (what, e))
        r4 = Reader(o4)
        la = r4.lead0at
        expect_fail("damaged start lead", o4, lambda d: d[:la] +
                    bytes([d[la] ^ 0x5A]) + d[la + 1:], "lap wants")
        kl = r4.kleadat + r4.ahead * r4.abytes     # key 1's, apart
        expect_fail("damaged key lead", o4, lambda d: d[:kl] +
                    bytes([d[kl] ^ 0x5A]) + d[kl + 1:], "lead is not")
        expect_fail("keys' leads past the file", o4, lambda d: d[:H_KLEADS]
                    + struct.pack("<I", len(d)) + d[H_KLEADS + 4:],
                    "leads apart")
        expect_fail("keys' leads with no AHEAD", o4, lambda d: d[:6] +
                    struct.pack("<H", struct.unpack_from("<H", d, 6)[0] &
                                ~F_AHEAD) + d[8:H_AHEAD] + b"\0" +
                    d[H_AHEAD + 1:], "")
        # ...and a file of the inline kind, every player's until 98.1.8.1,
        # still reads: its key leads are its entries' tails
        oi = os.path.join(tmp, "ahi.v88")
        wi_args = dict(keysecs=0.2, audio_fmt=AUD_PCM8, ahead=4)
        global KLEADS_APART
        KLEADS_APART = False
        try:
            encode_frames(apaths, oi, 30.0, awav, "herc", **wi_args)
        finally:
            KLEADS_APART = True
        try:
            verify_v88(oi)
            ri = Reader(oi)
            if ri.kleadat or ri.flags & F_KLEADS or ri.nkeys < 2 or \
                    ri.key_lead(1) != r4.key_lead(1):
                fails.append("an inline-lead file reads its leads wrong")
            k1, off1, n1 = ri.keys[1][:3]
            expect_fail("damaged inline key lead", oi, lambda d:
                        d[:off1 + n1 - 1] + bytes([d[off1 + n1 - 1] ^ 0x5A])
                        + d[off1 + n1:], "lead is not")
        except V88Error as e:
            fails.append("an inline-lead file: %s" % e)
        o0 = os.path.join(tmp, "ah0.v88")
        expect_fail("ahead byte with no flag", o0, lambda d: d[:H_AHEAD] +
                    b"\x04" + d[H_AHEAD + 1:], "no AHEAD flag")
        expect_fail("ahead one frame short", o4, lambda d: d[:H_AHEAD] +
                    b"\x03" + d[H_AHEAD + 1:], "")
        # WHICH KEYS THE TABLE KEEPS (keep_keys): a 640 x 480 VGA4 key of
        # ~61.7 KB is past the player's one read (61,440). A LATER one is
        # left out and the poster moves to its neighbour; the FIRST is kept
        # however big, the file then seeking nowhere; and a first that
        # cannot be stored at all - 4,000 bytes of lead past the entry's
        # word - takes every key with it, rather than let the next one
        # start the play and skip the frames before it. With the keys'
        # leads APART (98.1.8.1), the default, a lead costs no key at all
        gk = Geom(LAY_LIN80, 80, 480, bitplanes=True)
        sk, sb = gk.surface(), gk.surface()
        for y in range(186):
            for pl in range(4):
                for x in range(80):
                    sk[pl * PLANE + gk.base[y] + x] = rnd.getrandbits(8)
        kn = len(record(keyframe_ops(sk, gk), gk, limit=1 << 30))
        for what, seq, A, cap, want, apart in (
                ("a later key", (sb, sb, sk, sb, sb), 0, 61440,
                 ([0, 4], [(2, kn)], 0), True),
                ("a later key, no cap", (sb, sb, sk, sb, sb), 0, 65535,
                 ([0, 2, 4], [], 1), True),
                ("the first key", (sk, sb, sb), 0, 61440,
                 ([0, 2], [], 1), True),
                ("the first key and its lead inline", (sk, sb, sb), 4,
                 61440, ([], [(0, kn + 4000), (2, 7 + 4000)], 0xFFFF),
                 False),
                ("the first key and its lead apart", (sk, sb, sb), 4,
                 61440, ([0, 2], [], 1), True)):
            w = Writer(gk, 25000, 1000, AUD_PCM8, 1000, PF_VGA4, ahead=A,
                       keysecs=0.08, kcap=cap)
            w.kleads = apart
            w.kpart = None              # (whole keys: the table's own rule)
            for sf in seq:
                w.frame([], sf, b"\x80" * 1000)
            ok = os.path.join(tmp, "kd.v88")
            try:
                st = w.write(ok, 1 if len(w.keys) > 1 else 0)
                rk = Reader(ok)
                got = ([rk.keys[i][0] for i in range(rk.nkeys)],
                       st["kdropped"], rk.poster)
                if got != want:
                    fails.append("keys kept, %s (%d bytes): %r, not %r"
                                 % (what, kn, got, want))
            except V88Error as e:
                fails.append("keys kept, %s (%d bytes): %s" % (what, kn, e))
        # KEYS IN PARTS (98.1.3.8): the same ~61.7 KB key, made the way a
        # file is now - in parts of one read each - is KEPT, first or later,
        # with its lead asked inline (a key in parts takes it apart), reads
        # back to the canvas from the stream and from the key, and a part
        # that names the wrong next read is refused
        for what, seq in (("first", (sk, sb, sb)),
                          ("later", (sb, sb, sk, sb, sb))):
            w = Writer(gk, 25000, 1000, AUD_PCM8, 1000, PF_VGA4, ahead=4,
                       keysecs=0.08)
            w.kleads = False
            for sf in seq:
                w.frame([], sf, b"\x80" * 1000)
            kp = os.path.join(tmp, "kp.v88")
            try:
                st = w.write(kp, 0)
                rk = Reader(kp)     # (no verify: the frames are empty -
                                    # what is checked is the key alone)
                ki = seq.index(sk) // 2
                rd = key_reads(rk.key(ki)[1])
                cv = decode_at(rk, rk.keys[ki][0])
                if st["kdropped"] or not rk.kparts or not rk.kleadat or \
                        len(rd) < 2 or max(rd) > KEY_PART or \
                        rk.kmax != max(rd) or cv != gk.canvas(sk):
                    fails.append("keys in parts, the %s key: dropped %r, "
                                 "kparts %d, apart %d, reads %r, kmax %d, "
                                 "canvas %s" % (what, st["kdropped"],
                                                rk.kparts, rk.kleadat, rd,
                                                rk.kmax, cv == gk.canvas(sk)))
            except V88Error as e:
                fails.append("keys in parts, the %s key: %s" % (what, e))
        k0, ko, kn0 = Reader(kp).keys[1][:3]
        dk = open(kp, "rb").read()
        for what, d in (
                ("a key part naming the wrong next read",
                 dk[:ko + kn0 - 2] + struct.pack(
                     "<H", struct.unpack_from("<H", dk, ko + kn0 - 2)[0] + 1)
                 + dk[ko + kn0:]),
                ("a key in parts with no KPARTS flag",
                 dk[:7] + bytes([dk[7] & ~(F_KPARTS >> 8)]) + dk[8:]),
                ("a key part of no bytes",      # (the player re-read it
                 dk[:ko - 2] + struct.pack("<HH", kn0, 0) + dk[ko + 2:])):
            bad = kp + ".bad"
            open(bad, "wb").write(d)
            try:
                rb_ = Reader(bad)
                decode_at(rb_, rb_.keys[1][0])
                fails.append("%s was not refused" % what)
            except V88Error:
                pass
        # RESIDENT (98.1.7): two renditions and a sound block round-trip,
        # and a damaged block is refused
        ws = []
        for lay in (LAY_CGA, LAY_HERC):
            gg = Geom(lay, 20, 40)
            w = Writer(gg, 1500, 100, AUD_NONE, 0, PF_MONO1, loop=6)
            sf = gg.surface()
            for cv in cvs:
                ch = []
                for y, b in enumerate(gg.base):
                    for x in range(20):
                        if sf[b + x] != cv[y * 20 + x]:
                            ch.append(b + x)
                            sf[b + x] = cv[y * 20 + x]
                w.frame(spans(ch, sf, gg), sf)
            ws.append(w)
        res = os.path.join(tmp, "R.V88")
        snd = bytes(rnd.getrandbits(8) for _ in range(24 * 100))
        write_resident(res, ws, AUD_PCM8, 100, snd, repeat=True)
        rr = Reader(res, 1)
        if verify_v88(res) != 24 or not rr.resident or rr.loop[0] != 6 or \
                b"".join(rec[-100:] for rec, _, _ in rr.records()) != snd:
            fails.append("a resident file did not read back as written")
        boff = struct.unpack_from("<I", open(res, "rb").read(),
                                  192 + R_BLOCK)[0]
        expect_fail("damaged block", res, lambda d: d[:boff + 9] +
                    bytes([d[boff + 9] ^ 0x77]) + d[boff + 10:], "")
        # A LIVE FILE'S BLIT RUNS (98.1.3.4): written, every write in one,
        # and a run off the canvas, a run short of its writes and RUNS
        # without LIVE refused
        gl = Geom(LAY_LIN80, 20, 40)
        wl = Writer(gl, 1500, 100, AUD_NONE, 0, PF_MONO1)
        sf = gl.surface()
        for cv in cvs:
            ch = []
            for y, b in enumerate(gl.base):
                for x in range(20):
                    if sf[b + x] != cv[y * 20 + x]:
                        ch.append(b + x)
                        sf[b + x] = cv[y * 20 + x]
            wl.frame(spans(ch, sf, gl), sf)
        lv = os.path.join(tmp, "L.V88")
        write_resident(lv, [wl], live=[TARGETS["cga"]], pack=PK_NONE)
        rl = Reader(lv)
        if verify_v88(lv) != 24 or not rl.runs:
            fails.append("a live file's runs did not read back")
        dl = open(lv, "rb").read()
        boff = struct.unpack_from("<I", dl, 192 + R_BLOCK)[0]
        r0 = next(rec for rec, _, _ in rl.records())
        at = boff + walk_lists(bytearray(65536), r0, REC_HDR)
        expect_fail("blit run off the canvas", lv, lambda d: d[:at + 3] +
                    b"\x7F" + d[at + 4:], "outside")
        expect_fail("blit run short of its writes", lv, lambda d: d[:at + 1]
                    + bytes([d[at + 1], 1, d[at + 3], 1]) + d[at + 5:],
                    "no blit run")
        expect_fail("RUNS without LIVE", lv, lambda d: d[:6] +
                    struct.pack("<H", F_RESIDENT | F_RUNS) + d[8:], "not LIVE")
        # ...and a VGA4 one (98.3.10.4): its runs cover every plane's
        # writes, its lists end past the sub-records' 0, and a VGA4 live
        # rendition for a one-bit screen is refused
        g4 = Geom(LAY_LIN80, 6, 12, bitplanes=True)
        w4 = Writer(g4, 1500, 100, AUD_NONE, 0, PF_VGA4)
        s4, prev = g4.surface(), bytes(g4.w * g4.h)
        rn4 = random.Random(983104)
        for f in range(8):
            cv = bytearray(prev)
            for _ in range(20):
                cv[rn4.randrange(len(cv))] = rn4.randrange(16)
            g4.put(s4, cv)
            w4.frame(vga4_subs(bytes(cv), prev, g4), s4)
            prev = bytes(cv)
        l4 = os.path.join(tmp, "L4.V88")
        write_resident(l4, [w4], live=[TARGETS["vga"]], pack=PK_NONE)
        r4 = Reader(l4)
        if verify_v88(l4) != 8 or not r4.runs or decode_at(r4, 7) != prev:
            fails.append("a VGA4 live file did not read back")
        try:
            write_resident(l4, [w4], live=[TARGETS["cga"]])
            fails.append("a VGA4 live rendition for CGA was written")
        except V88Error:
            pass
        # CGA IN COLOUR (98.1.3.3): a CGA4 file with its palette byte and a
        # C160 one round-trip, and a palette where none belongs, a bad
        # palette byte and C160 on another layout are refused
        rn = random.Random(98133)
        for pf, lay, wb, pal in ((PF_CGA4, LAY_CGA, 10, 0x51),
                                 (PF_C160, LAY_C160, 12, None),
                                 (PF_C512, LAY_TXT, 16, CARD_BOTH),
                                 (PF_TEXT, LAY_TEXT, 16, TEXT_COLOUR)):
            gg = Geom(lay, wb, 20 if lay != LAY_TEXT else 25)
            cvs2 = [bytes(rn.getrandbits(8) for _ in range(wb * gg.h))
                    for _ in range(6)]
            out2 = os.path.join(tmp, "C%d.V88" % pf)
            encode_canvases(cvs2, gg, out2, 15.0, pf, keysecs=1.0,
                            cgapal=pal)
            rc = Reader(out2)
            if verify_v88(out2) != 6 or rc.pixfmt != pf or \
                    (pal is not None and rc.cgapal != pal) or \
                    decode_at(rc, 5) != cvs2[5]:
                fails.append("a %s file did not read back as written"
                             % PF_NAMES[pf])
        c4 = os.path.join(tmp, "C%d.V88" % PF_CGA4)
        c16 = os.path.join(tmp, "C%d.V88" % PF_C160)
        expect_fail("CGA4 palette byte of 60h", c4, lambda d: d[:246] +
                    b"\x60" + d[247:], "CGA4 palette")
        expect_fail("palette byte in a C160 file", c16, lambda d: d[:246] +
                    b"\x01" + d[247:], "palette byte")
        expect_fail("C160 on the CGA layout", c16, lambda d: d[:193] +
                    bytes([LAY_CGA]) + d[194:], "C160")
        c512 = os.path.join(tmp, "C%d.V88" % PF_C512)
        expect_fail("C512 card byte of 3", c512, lambda d: d[:246] +
                    b"\x03" + d[247:], "card byte")
        expect_fail("C512 on the C160 layout", c512, lambda d: d[:193] +
                    bytes([LAY_C160]) + d[194:], "only its")
        ctx = os.path.join(tmp, "C%d.V88" % PF_TEXT)
        expect_fail("TEXT colour byte of 2", ctx, lambda d: d[:246] +
                    b"\x02" + d[247:], "TEXT colour")
        expect_fail("TEXT on the text-80x100 layout", ctx, lambda d: d[:193] +
                    bytes([LAY_TXT]) + d[194:], "only its")
        cvt = decode_at(Reader(ctx), 5)
        if len(text_mono(cvt, 16, 25)) != 4 * 25 * 4:
            fails.append("a TEXT poster is not a cell's four by four")
        # A PALETTE A SCENE (98.1.3.2.3): three scenes' sixteen read back,
        # each frame's the table says, and four hostile tables refused
        gp = Geom(LAY_LIN80, 4, 16, bitplanes=True)
        cvp = [bytes([(f + i) % 16 for i in range(32 * 16)])
               for f in range(12)]
        pals = [bytes(rnd.randrange(64) if i >= 3 else 0 for i in range(48))
                for _ in range(3)]
        pv = [(0, pals[0]), (4, pals[1]), (9, pals[2])]
        pf_ = os.path.join(tmp, "PALEV.V88")
        encode_canvases(cvp, gp, pf_, 15.0, PF_VGA4,
                        pals[0] + bytes(PAL_BYTES - 48), keysecs=0.5,
                        pev=pv)
        rp = Reader(pf_)
        if rp.pev != pv or palette_at(rp, 3)[:48] != pals[0] or \
                palette_at(rp, 4)[:48] != pals[1] or \
                palette_at(rp, 11)[:48] != pals[2] or \
                decode_at(rp, 7) != cvp[7]:
            fails.append("a palette a scene did not read back as written")
        at = struct.unpack_from("<I", open(pf_, "rb").read(), 192 + R_PEV)[0]
        expect_fail("palette table out of order", pf_, lambda d: d[:at + 52]
                    + struct.pack("<I", 10) + d[at + 56:],
                    "palette table entry")
        expect_fail("PALEV with no table", pf_, lambda d: d[:252] +
                    b"\0\0" + d[254:], "palette table of 0")
        expect_fail("palette table's frame 0 not the header's", pf_,
                    lambda d: d[:at + 4] + bytes([(d[at + 4] + 1) % 64]) +
                    d[at + 5:], "not the header's")
        expect_fail("PALEV on a C160 file", c16, lambda d: d[:7] +
                    bytes([d[7] | F_PALEV >> 8]) + d[8:], "palette a scene")
        # PAGE-AHEAD (98.1.3.7): a scene change built a band a frame on
        # the hidden page, its last rows left for the flip record, keys in
        # the window carrying the hidden page - every frame read back, from
        # the start and from every key, and two hostile files refused
        gq = Geom(LAY_LIN80, 4, 16, bitplanes=True)
        sa = [bytes([1 + (i // 7) % 15 for i in range(32 * 16)]),
              bytes([(i * 5 + 3) % 16 for i in range(32 * 16)])]
        cvq = [bytearray(sa[0] if f < 6 else sa[1]) for f in range(12)]
        for f, cv in enumerate(cvq):
            cv[(f % 14) * 32 + 4] = (f * 3) % 16
        cvq = [bytes(cv) for cv in cvq]
        wq = Writer(gq, 1500, 100, AUD_NONE, 0, PF_VGA4, keysecs=0.2,
                    palette=pals[0] + bytes(PAL_BYTES - 48),
                    screen=SCR_640X400, page=True)
        vq, hq = gq.surface(), gq.surface()
        for f in range(12):
            flip = f == 6
            if flip:
                vq, hq = hq, vq
            ops = vga4_subs(cvq[f], gq.canvas(vq), gq)
            gq.put(vq, cvq[f])
            hops = None
            if f < 5:                   # rows 0..11 of the next scene, 3 a
                new = bytearray(gq.canvas(hq))      # frame from frame 1
                r0, r1 = max(0, f - 1) * 3, f * 3
                new[r0 * 32:r1 * 32] = cvq[6][r0 * 32:r1 * 32]
                hops = vga4_subs(bytes(new), gq.canvas(hq), gq)
                gq.put(hq, bytes(new))
            wq.frame(ops, vq, hidden=hops, flip=flip, hsurf=hq)
        pq = os.path.join(tmp, "PAGE.V88")
        wq.write(pq, 0)
        verify_v88(pq)
        rq = Reader(pq)
        if not rq.page or any(decode_at(rq, f) != cvq[f] for f in range(12)):
            fails.append("a page-ahead file did not read back as written")
        # ...and with its keys IN PARTS (98.1.3.8): a read of 80 bytes cuts
        # every key, the hidden page's band riding with each part
        wq = Writer(gq, 1500, 100, AUD_NONE, 0, PF_VGA4, keysecs=0.2,
                    palette=pals[0] + bytes(PAL_BYTES - 48),
                    screen=SCR_640X400, page=True)
        wq.kpart = 80
        vq, hq = gq.surface(), gq.surface()
        for f in range(12):
            flip = f == 6
            if flip:
                vq, hq = hq, vq
            ops = vga4_subs(cvq[f], gq.canvas(vq), gq)
            gq.put(vq, cvq[f])
            hops = None
            if f < 5:
                new = bytearray(gq.canvas(hq))
                r0, r1 = max(0, f - 1) * 3, f * 3
                new[r0 * 32:r1 * 32] = cvq[6][r0 * 32:r1 * 32]
                hops = vga4_subs(bytes(new), gq.canvas(hq), gq)
                gq.put(hq, bytes(new))
            wq.frame(ops, vq, hidden=hops, flip=flip, hsurf=hq)
        pq2 = os.path.join(tmp, "PAGEKP.V88")
        try:
            wq.write(pq2, 0)
            verify_v88(pq2)
            rq2 = Reader(pq2)
            np_ = [len(key_reads(rq2.key(i)[1])) for i in range(rq2.nkeys)]
            if not rq2.kparts or max(np_) < 2 or \
                    any(decode_at(rq2, f) != cvq[f] for f in range(12)):
                fails.append("a page-ahead file with keys in parts (%r) did "
                             "not read back as written" % np_)
        except V88Error as e:
            fails.append("a page-ahead file with keys in parts: %s" % e)
        dq = open(pq, "rb").read()
        expect_fail("page-ahead byte 37 with no PAGE flag", pq,
                    lambda d: d[:7] + bytes([d[7] & ~(F_PAGE >> 8)]) +
                    d[8:], "a flip byte")
        o, hat = rq.sp0 + 4, None
        for rec, at, i in rq.records():
            if struct.unpack_from("<H", rec, 4)[0] & REC_HID:
                n0, y0, y1 = struct.unpack_from("<HHH", rec, 0)
                own = bytearray(rec)
                struct.pack_into("<HH", own, 2, y0 & ~REC_FLIP,
                                 y1 & ~REC_HID)
                hat = o + lists_end(own, gq)
                break
            o += len(rec)
        if hat is None or struct.unpack_from("<H", dq, hat)[0] < REC_HDR:
            fails.append("the page-ahead file has no hidden record to break")
        else:
            expect_fail("hidden record past its record", pq,
                        lambda d: d[:hat] + struct.pack("<H", 4000) +
                        d[hat + 2:], "hidden-page record")
        # PAGE-AHEAD ON N PAGES (98.1.3.7.1): four pages by number - three
        # scene changes each built ahead on a page of its own, two of them
        # in the same frames (a chain), keys naming the page they show and
        # carrying each page that is not it - read back from the start and
        # from every key, then the same on Mode X's 320 x 200 (98.1.3.1.1),
        # and the hostile files refused
        def mpage_clip(g, mk, scenes, cuts, builds, **kw):
            """`cuts`: frame -> the page it flips to; `builds`: page ->
            (scene, first frame, last frame), its rows a frame in turn"""
            kp = kw.pop("kpart", None)
            w = Writer(g, 1500, 100, AUD_NONE, 0, mk, keysecs=0.2,
                       page=True, **kw)
            if kp is not None:
                w.kpart = kp
            P = [g.surface() for _ in range(w.pages)]
            cvs, s0 = [], 0
            for f in range(12):
                cv = bytearray(scenes[sum(1 for c in cuts if c <= f)])
                cv[(f % g.h) * g.wb + 1] = (f * 3) % 16
                cvs.append(bytes(cv))
            for f in range(12):
                flip = cuts.get(f)
                if flip is not None:
                    s0 = flip
                ops = mk_ops(g, cvs[f], g.canvas(P[s0]))
                g.put(P[s0], cvs[f])
                hid = []
                for pg, (sc, a, b) in sorted(builds.items()):
                    if a <= f <= b and pg != s0:
                        rows = -(-g.h // (b - a + 1))
                        new = bytearray(g.canvas(P[pg]))
                        r0 = (f - a) * rows
                        new[r0 * g.wb:(r0 + rows) * g.wb] = \
                            scenes[sc][r0 * g.wb:(r0 + rows) * g.wb]
                        hid.append((pg, mk_ops(g, bytes(new),
                                               g.canvas(P[pg]))))
                        g.put(P[pg], bytes(new))
                w.frame(ops, P[s0], hidden=hid, flip=flip, shown=s0,
                        hsurfs=dict(enumerate(P)))
            return w, cvs

        def mk_ops(g, cv, base):
            if g.bitplanes:
                return vga4_subs(cv, base, g)
            return modex_subs(cv, [a != b for a, b in zip(cv, base)], g)
        gm = Geom(LAY_LIN80, 4, 16, bitplanes=True)
        scn = [bytes([(i * k + k) % 16 for i in range(32 * 16)])
               for k in (1, 3, 5, 7)]
        cuts = {4: 1, 6: 2, 9: 3}
        builds = {1: (1, 0, 2), 2: (2, 1, 4), 3: (3, 2, 7)}
        wm, cvm = mpage_clip(gm, PF_VGA4, scn, cuts, builds,
                             palette=pals[0] + bytes(PAL_BYTES - 48),
                             screen=SCR_320X200, pages=4)
        pm = os.path.join(tmp, "MPAGE.V88")
        wm.write(pm, 0)
        try:
            verify_v88(pm)
            rm = Reader(pm)
            chains = sum(1 for rec, _, _ in rm.records()
                         if len(mpage_parts(rec, gm, 4)[3]) > 1)
            if not rm.mpage or rm.pages != 4 or not chains or \
                    any(decode_at(rm, f) != cvm[f] for f in range(12)):
                fails.append("an N-page file did not read back as written")
        except V88Error as e:
            fails.append("an N-page file: %s" % e)
        gx = Geom(LAY_MODEX, 40, 20)
        scx = [bytes([(i * k + 7 * k) % 256 for i in range(gx.w * gx.h)])
               for k in (1, 3, 5, 7)]
        try:
            wx, cvx = mpage_clip(gx, PF_VGA8, scx, cuts, builds,
                                 palette=bytes(range(64)) * 12,
                                 screen=SCR_320X200, pages=4)
            px = os.path.join(tmp, "MPAGEMX.V88")
            wx.write(px, 0)
            verify_v88(px)
            rx = Reader(px)
            if not rx.mpage or rx.screen != SCR_320X200 or \
                    any(decode_at(rx, f) != cvx[f] for f in range(12)):
                fails.append("a Mode X 320 x 200 file on four pages did not "
                             "read back as written")
        except V88Error as e:
            fails.append("a Mode X 320 x 200 file on four pages: %s" % e)
        try:
            Writer(gx, 1500, 100, AUD_NONE, 0, PF_VGA8, page=True,
                   palette=bytes(range(64)) * 12, pages=4)
            fails.append("four Mode X pages at 320 x 240 were written")
        except V88Error:
            pass
        # NARROW (98.1.3.2.5): sixteen colours with rows 40 bytes apart,
        # eight pages at 320 x 200 - the last one used - read back, and the
        # canvas of an 80-byte stride and a flipped NARROW file refused
        gn = Geom(LAY_LIN80, 4, 16, bitplanes=True, stride=40)
        scnn = [bytes([(i * k + k) % 16 for i in range(32 * 16)])
                for k in (1, 3, 5, 7, 9)]
        npal = pals[0] + bytes(PAL_BYTES - 48)
        try:
            wn, cvn = mpage_clip(gn, PF_VGA4, scnn, {3: 1, 5: 2, 7: 3, 9: 7},
                                 {1: (1, 0, 2), 2: (2, 0, 4), 3: (3, 1, 6),
                                  7: (4, 2, 8)}, palette=npal,
                                 screen=SCR_320X200N, pages=8)
            pn = os.path.join(tmp, "MPAGEN.V88")
            wn.write(pn, 0)
            verify_v88(pn)
            rn = Reader(pn)
            if not rn.mpage or rn.pages != 8 or rn.g.stride != 40 or \
                    rn.screen != SCR_320X200N or \
                    any(decode_at(rn, f) != cvn[f] for f in range(12)):
                fails.append("a NARROW file on eight pages did not read back "
                             "as written")
        except V88Error as e:
            fails.append("a NARROW file on eight pages: %s" % e)
        # FLIPPED AND BUILT AHEAD (98.1.3.7.2): four pages, every frame
        # flipped - page 2 built toward the first scene change and COPIED
        # onto page 3 in the calm, so the frame after the flip draws on a
        # page already brought up; page 0 built toward the second and a
        # COPY in the flip's own record bringing page 1 up - with keys
        # leaving out the page the next frame is drawn on. Read back from
        # the start and from every key; and its two breaks refused
        gf = Geom(LAY_LIN80, 4, 16, bitplanes=True)
        scf = [bytes([(i * k + 2 * k) % 16 for i in range(32 * 16)])
               for k in (1, 3, 5)]
        sched = {0: (1, False, [(2, 1, 0, 6)], []),
                 1: (0, False, [(2, 1, 6, 11)], []),
                 2: (1, False, [(2, 1, 11, 16)], []),
                 3: (0, False, [], [(3, 2, 0, 8)]),
                 4: (1, False, [], [(3, 2, 8, 16)]),
                 5: (0, False, [], []),
                 6: (2, True, [], []),
                 7: (3, False, [(0, 2, 0, 6)], []),
                 8: (2, False, [(0, 2, 6, 12)], []),
                 9: (3, False, [(0, 2, 12, 16)], []),
                 10: (0, True, [], [(1, 0, 0, 16)]),
                 11: (1, False, [], [])}
        wf = Writer(gf, 1500, 100, AUD_NONE, 0, PF_VGA4, keysecs=0.2,
                    page=True, flip=True, pages=4, screen=SCR_320X200,
                    palette=npal)
        PF = [gf.surface() for _ in range(4)]
        cvf, shown = [], 0
        for f in range(12):
            cv = bytearray(scf[(f >= 6) + (f >= 10)])
            cv[(f % gf.h) * gf.wb * 8 + 3] = (f * 5 + 1) % 16
            cvf.append(bytes(cv))
        for f in range(12):
            pg, built, hb, cps = sched[f]
            if not built:               # (brought up: the page shown)
                gf.put(PF[pg], gf.canvas(PF[shown]))
            ops = mk_ops(gf, cvf[f], gf.canvas(PF[pg]))
            gf.put(PF[pg], cvf[f])
            shown = pg
            hid = []
            for hp, sc, r0, r1 in hb:
                new = bytearray(gf.canvas(PF[hp]))
                rb = gf.wb * 8
                new[r0 * rb:r1 * rb] = scf[sc][r0 * rb:r1 * rb]
                hid.append((hp, mk_ops(gf, bytes(new), gf.canvas(PF[hp]))))
                gf.put(PF[hp], bytes(new))
            for d_, src, c0, c1 in cps:
                cv_d = bytearray(gf.canvas(PF[d_]))
                rb = gf.wb * 8
                cv_d[c0 * rb:c1 * rb] = gf.canvas(PF[src])[c0 * rb:c1 * rb]
                gf.put(PF[d_], bytes(cv_d))
            nxt = sched.get(f + 1)
            carry = {n: PF[n] for n in range(4)
                     if not (nxt and not nxt[1] and n == nxt[0])}
            wf.frame(ops, PF[pg], hidden=hid, flip=pg, shown=pg,
                     hsurfs=carry, built=built, copies=cps)
        pf_ = os.path.join(tmp, "FPAGE.V88")
        try:
            wf.write(pf_, 0)
            verify_v88(pf_)
            rf = Reader(pf_)
            if not rf.fpage or rf.pages != 4 or len(rf.keys) < 3 or \
                    any(decode_at(rf, f) != cvf[f] for f in range(12)):
                fails.append("a file flipped and built ahead did not read "
                             "back as written")
            o, bat, cat = rf.sp0 + 4, None, None
            for rec, at, i in rf.records():
                n0, y0, y1 = struct.unpack_from("<HHH", rec, 0)
                if y1 & REC_BUILT:
                    fl_, pg_, own_, hs_, b_ = mpage_parts(rec, gf, 4, True)
                    if bat is None:
                        bat = o
                    elif hs_ and cat is None:
                        own2 = bytearray(rec)
                        struct.pack_into("<HH", own2, 2, y0 & ROW_MASK,
                                         y1 & ROW_MASK)
                        cat = o + lists_end(own2, gf)
                o += len(rec)
            if bat is None or cat is None:
                fails.append("the flipped built-ahead file has no built "
                             "record or copy to break")
            else:
                # the first built flip brought up as an ordinary one
                expect_fail("built flip brought up", pf_, lambda d:
                            d[:bat + 5] + bytes([d[bat + 5] & 0xEF]) +
                            d[bat + 6:], "brought up is not")
                # the second flip's COPY onto the page it shows (from page
                # 2: the copy's own source is that page)
                expect_fail("copy onto the page shown", pf_, lambda d:
                            d[:cat + 3] + bytes([d[cat + 3] & 0x8F]) +
                            d[cat + 4:cat + 5] +
                            bytes([d[cat + 5] & 0x8F | 0x20]) + d[cat + 6:],
                            "on the page shown")
        except V88Error as e:
            fails.append("a file flipped and built ahead: %s" % e)
        for what, kw in (("an 80-byte canvas on a NARROW screen",
                          dict(screen=SCR_320X200N, page=True, pages=8,
                               g=gm)),
                         ("a NARROW screen flipped every frame",
                          dict(screen=SCR_320X200N, flip=True, g=gn))):
            try:
                Writer(kw.pop("g"), 1500, 100, AUD_NONE, 0, PF_VGA4,
                       palette=npal, **kw)
                fails.append("%s was written" % what)
            except V88Error:
                pass
        rm = Reader(pm)
        r37 = rm.slot + 37              # (the slot's byte 37, and 39)
        expect_fail("N-page file of 9 pages", pm, lambda d: d[:r37 + 2] +
                    bytes([9]) + d[r37 + 3:], "pages with a flip byte")
        expect_fail("N-page count in a two-page file", pm, lambda d:
                    d[:r37] + bytes([FLIP_PAGE]) + d[r37 + 1:],
                    "pages with a flip byte")
        o, hat, fat = rm.sp0 + 4, None, None
        for rec, at, i in rm.records():
            n0, y0, y1 = struct.unpack_from("<HHH", rec, 0)
            if y1 & REC_HID and hat is None and not y0 & REC_FLIP:
                own = bytearray(rec)
                struct.pack_into("<HH", own, 2, y0 & ROW_MASK, y1 & ROW_MASK)
                hat = o + lists_end(own, gm)
            if y0 & REC_FLIP and fat is None:
                fat = o
            o += len(rec)
        if hat is None or fat is None:
            fails.append("the N-page file has no hidden record or flip to "
                         "break")
        else:
            # a hidden record onto page 0, the page shown before any flip
            expect_fail("hidden record on the page shown", pm, lambda d:
                        d[:hat + 3] + bytes([d[hat + 3] & 0x8F]) +
                        d[hat + 4:], "on the page shown")
            # the first flip onto page 0, the page already shown
            expect_fail("flip onto the page shown", pm, lambda d:
                        d[:fat + 3] + bytes([d[fat + 3] & 0x8F]) +
                        d[fat + 4:], "onto the page shown")
        # ...and a pool that MATTERS: page 1 built whole by frame 2, carried
        # unchanged by keys 3 and 6, flipped to at frame 8 - so a play from
        # key 6 shows frame 8 right only if the pool's picture was read
        try:
            wq, cvq = mpage_clip(gm, PF_VGA4, scn, {8: 1}, {1: (1, 0, 2)},
                                 palette=pals[0] + bytes(PAL_BYTES - 48),
                                 screen=SCR_320X200, pages=4)
            pq = os.path.join(tmp, "POOL.V88")
            wq.write(pq, 0)
            verify_v88(pq)
            rq = Reader(pq)
            k6 = [i for i, e in enumerate(rq.keys) if e[0] == 6]
            named = []
            if k6:
                lp = key_split(rq.key(k6[0])[1])[-1]
                named = [hp for hp, x in mpage_parts(
                    lp, gm, 4, kref=rq.kpool)[3] if isinstance(x, tuple)]
            if not rq.kpool or 1 not in named or \
                    any(decode_at(rq, f) != cvq[f] for f in range(12)):
                fails.append("a page pool's picture did not read back as "
                             "written (key 6 names %r)" % (named,))
        except V88Error as e:
            fails.append("a page pool: %s" % e)
        # ...and that pool in a file whose keys are IN PARTS (98.1.3.8): the
        # references lengthen a key's last part, so the part before it must
        # name the longer read - a stale word reads the last part short, and
        # its chain runs past the bytes read
        try:
            wq, cvq = mpage_clip(gm, PF_VGA4, scn, {8: 1}, {1: (1, 0, 2)},
                                 palette=pals[0] + bytes(PAL_BYTES - 48),
                                 screen=SCR_320X200, pages=4, kpart=200)
            pq = os.path.join(tmp, "POOLP.V88")
            wq.write(pq, 0)
            verify_v88(pq)
            rq = Reader(pq)
            k6 = [i for i, e in enumerate(rq.keys) if e[0] == 6]
            ps = key_split(rq.key(k6[0])[1]) if k6 else []
            named = [hp for hp, x in mpage_parts(
                ps[-1], gm, 4, kref=rq.kpool)[3]
                if isinstance(x, tuple)] if ps else []
            if not rq.kpool or len(ps) < 2 or 1 not in named or \
                    any(decode_at(rq, f) != cvq[f] for f in range(12)):
                fails.append("a page pool with keys in parts did not read "
                             "back as written (key 6: %d parts, names %r)"
                             % (len(ps), named))
        except (V88Error, struct.error) as e:
            fails.append("a page pool with keys in parts: %s" % e)
        # THE PAGE POOL (98.1.3.8.1): this file's keys name pages from it -
        # a reference past the file, one naming the page its key shows, and
        # one in a file without KPOOL must each be refused
        rat = None
        for ki in range(rm.nkeys):
            off = rm.keys[ki][1]
            while struct.unpack_from("<H", rm.d, off + 2)[0] & KEY_MORE:
                off += struct.unpack_from("<H", rm.d, off)[0]
            n0, y0, y1 = struct.unpack_from("<HHH", rm.d, off)
            own = bytearray(rm.d[off:off + n0])
            struct.pack_into("<HH", own, 2, y0 & ROW_MASK, y1 & ROW_MASK)
            e = lists_end(own, gm) if y1 & REC_HID else n0
            while e < n0:
                hn, hy0, hy1 = struct.unpack_from("<HHH", rm.d, off + e)
                if hy0 & REC_FLIP:
                    rat, rshown = off + e, (y0 & PG_BITS) >> PG_SHIFT
                    break
                e += hn
            if rat is not None:
                break
        if not rm.kpool or rat is None:
            fails.append("the N-page file has no page pool reference to "
                         "break")
        else:
            expect_fail("pool reference past the file", pm, lambda d:
                        d[:rat + 6] + struct.pack("<I", len(d)) +
                        d[rat + 10:], "runs off the file")
            expect_fail("pool reference on the page shown", pm, lambda d:
                        d[:rat + 3] + bytes([0x80 | rshown << 4]) +
                        d[rat + 4:], "from the pool and shows it")
            expect_fail("pool reference without KPOOL", pm, lambda d:
                        d[:7] + bytes([d[7] & 0x7F]) + d[8:],
                        "hidden-page record")
        kat = rm.keys[1][1]             # (key 1's first part's y0: a page
        expect_fail("key naming another page", pm, lambda d:  # it carries,
                    d[:kat + 3] + bytes([d[kat + 3] ^ 0x30]) + d[kat + 4:],
                    "page")             # or one the stream does not show)
        # SUB2 (98.1.3.2.4): list maps, and a colour sub-record's masks in
        # write mode 3 - a frame of plane stores and one of each, read back
        # to the pixel, and the four ways a writer could break the rules
        # refused
        gs = Geom(LAY_LIN80, 8, 16, bitplanes=True)
        gs.sub2 = True
        rs2 = random.Random(9813024)

        def sub2_file(name, frames):
            """frames: [(ops, the canvas after)], into a SUB2 file"""
            ws = Writer(gs, 1500, 100, AUD_NONE, 0, PF_VGA4, keysecs=60.0)
            sf = gs.surface()
            for ops, cvx in frames:
                gs.put(sf, cvx)
                ws.frame(ops, sf)
            pth = os.path.join(tmp, name)
            ws.write(pth)
            return pth
        cv0 = bytes(rs2.randrange(16) for _ in range(gs.w * gs.h))
        sf0 = gs.surface()
        gs.put(sf0, cv0)
        # frame 1: rows 0..7 new by plane stores, rows 8..15 colour 9 under
        # masks - a whole row of FFh (a RUN, no read), and loose bytes
        cv1 = bytearray(cv0)
        for i in range(8 * gs.w):
            cv1[i] = rs2.randrange(16)
        pl = vga4_subs(bytes(cv1), cv0, gs)
        sf1 = bytearray(sf0)
        gs.put(sf1, bytes(cv1))
        masks = bytearray(PLANE)
        m3a = list(range(gs.base[9], gs.base[9] + gs.wb))
        for a in m3a:
            masks[a] = 0xFF
        loose = [gs.base[12] + 1, gs.base[12] + 2, gs.base[14] + 5]
        for a, m in zip(loose, (0x81, 0x3C, 0x01)):
            masks[a] = m
            m3a.append(a)
        cv1 = bytearray(gs.canvas(sf1))     # (by the pixel, not wm3_apply:
        for a in m3a:                       # the expectation is the
            y = gs.rowof[a]
            xb = a - gs.base[y]
            for i in range(8):              # reader's to meet)
                if masks[a] >> (7 - i) & 1:
                    cv1[y * gs.w + xb * 8 + i] = 9
        cv1 = bytes(cv1)
        m3ops = spans(m3a, masks, gs, gaps=False)
        s2 = sub2_file("SUB2.V88", [(vga4_subs(cv0, bytes(len(cv0)), gs),
                                     cv0), (pl + [(SUB2_WM3 + 9, m3ops)],
                                            cv1)])
        try:
            rs2r = Reader(s2)
            if verify_v88(s2) != 2 or not rs2r.g.sub2 or \
                    decode_at(rs2r, 1) != cv1:
                fails.append("a SUB2 file with write mode 3 did not read "
                             "back")
            if not any(lead >= SUB2_WM3 for rec, at, i in rs2r.records()
                       for lead, a, b in subs_of(rec, rs2r.g)):
                fails.append("a SUB2 file's colour sub-record is not there")
        except V88Error as e:
            fails.append("a SUB2 file was refused: %s" % e)
        for what, ops, why in (
                ("plane store after a colour one",
                 [(SUB2_WM3 + 9, m3ops)] + pl, "after a write mode 3"),
                ("byte stored by planes and write mode 3",
                 [(15, spans([loose[0]], bytes(PLANE), gs, gaps=False)),
                  (SUB2_WM3 + 9, m3ops)], "planes and by write mode 3"),
                ("two colour masks overlapping",
                 [(SUB2_WM3 + 9, m3ops), (SUB2_WM3 + 2, m3ops[-1:])],
                 "masks overlap")):
            bp = sub2_file("SUB2B.V88", [(vga4_subs(cv0, bytes(len(cv0)),
                                                    gs), cv0), (ops, cv1)])
            try:
                verify_v88(bp)
                fails.append("a SUB2 %s was not refused" % what)
            except V88Error as e:
                if why not in str(e):
                    fails.append("a SUB2 %s was refused for another reason: "
                                 "%s" % (what, e))
        rk = Reader(s2)
        kb = bytearray(struct.pack("<HHH", 0, 0, gs.h)) + \
            bytes([SUB2_WM3 + 1]) + struct.pack("<H", 1) + b"\x01" + \
            struct.pack("<H", gs.base[0]) + b"\x00\xff\x00\x00"
        struct.pack_into("<H", kb, 0, len(kb))
        for what, rec, key, why in (
                ("colour sub-record in a keyframe", bytes(kb), True,
                 "keyframe with a write mode 3"),
                ("list map of 0", bytes(kb[:7]) + b"\0\0" + bytes(kb[9:]),
                 False, "list map"),
                ("list map past the tenth list", bytes(kb[:7]) +
                 struct.pack("<H", 0x0401) + bytes(kb[9:]), False,
                 "list map")):
            try:
                rk.apply(rk.g.surface(), rec, key=key)
                fails.append("a SUB2 %s was not refused" % what)
            except V88Error as e:
                if why not in str(e):
                    fails.append("a SUB2 %s was refused for another reason: "
                                 "%s" % (what, e))
        try:                        # (a one-bit rendition is not its)
            mut = bytearray(open(c16, "rb").read())
            mut[7] |= F_SUB2 >> 8
            open(c16 + ".s2", "wb").write(mut)
            if verify_v88(c16 + ".s2") != 6:
                fails.append("a C160 file with SUB2 set read differently")
        except V88Error as e:
            fails.append("a C160 file with SUB2 set was refused: %s" % e)
    for f in fails:
        print("os88vid --selfcheck: FAIL - %s" % f)
    if not fails:
        print("os88vid --selfcheck: ok - encode, import (cga, herc, lin80), "
              "decode and verify agree, ADPCM4 carries a tone and seeks "
              "exactly, a seam joins its laps, sound ahead round-trips and seeks, "
              "a resident file's blocks "
              "round-trip, CGA4, C160, C512 and TEXT round-trip, a palette "
              "a scene and a page-ahead file round-trip, a live file's "
              "blit runs cover its writes, and twenty-nine corruptions "
              "were refused")
    return 1 if fails else 0


def main():
    if sys.argv[1:] == ["--selfcheck"]:
        return selfcheck()
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)

    def keyargs(s):
        s.add_argument("--audio", choices=sorted(AUD_BY_NAME), default="pcm8",
                       help="the sound's format (SPEC.md 98.1.1): ADPCM4 is "
                       "half the bytes, decoded by the card")
        s.add_argument("--keysecs", type=float, default=KEY_SECS,
                       help="seconds between keyframes (SPEC.md 98.1.3)")
        s.add_argument("--poster", type=int,
                       help="the poster keyframe (default: the first that is "
                       "not all one value)")
        s.add_argument("--title", default=None)
    s = sub.add_parser("import", help="XDV -> V88, exactly")
    s.add_argument("src")
    s.add_argument("out")
    s.add_argument("--target", choices=sorted(LAYOUT_BY_NAME), default="cga",
                   type=layout_name)
    keyargs(s)
    s = sub.add_parser("encode", help="frames (+ a WAV) -> V88, losslessly")
    s.add_argument("frames", nargs="+")
    s.add_argument("out")
    s.add_argument("--fps", type=float, required=True)
    s.add_argument("--wav")
    s.add_argument("--layout", choices=sorted(LAYOUT_BY_NAME), default="cga",
                   type=layout_name)
    keyargs(s)
    s = sub.add_parser("info")
    s.add_argument("files", nargs="+")
    s = sub.add_parser("poster", help="set a .V88's poster in place, "
                       "with no re-encode (SPEC.md 98.2.11)")
    s.add_argument("file")
    w = s.add_mutually_exclusive_group(required=True)
    w.add_argument("--key", type=int, help="the keyframe, by its index")
    w.add_argument("--frame", type=int, help="the keyframe a play from this "
                   "frame starts at")
    s = sub.add_parser("title", help="set a .V88's title in place, "
                       "with no re-encode (SPEC.md 98.2.11)")
    s.add_argument("file")
    s.add_argument("title", help="up to 47 characters; the player's panel "
                   "shows the first 35")
    s = sub.add_parser("spkwav", help="what a speaker .V88 drives the "
                       "speaker line with, as a WAV - carrier and all "
                       "(SPEC.md 98.2.15.2)")
    s.add_argument("file")
    s.add_argument("out")
    s.add_argument("--rendition", type=int, default=0)
    s = sub.add_parser("speaker", help="a speaker .V88's sound shaped for "
                       "the speaker, into a new file (SPEC.md 98.2.15.1)")
    s.add_argument("file")
    s.add_argument("out")
    s.add_argument("--style", choices=sorted(SPK_STYLES), default=SPK_STYLE,
                   help="lifted: quiet passages raised; natural: more of the "
                        "song's own rise and fall (default %(default)s). "
                        "--highpass, --ratio and --range override it")
    s.add_argument("--highpass", type=int, default=None,
                   help="Hz; nothing under it (the style's; 0 off)")
    s.add_argument("--drive", type=float, default=SPK_DRIVE,
                   help="the level, an RMS of full scale (default "
                        "%(default)s)")
    s.add_argument("--lows", type=float, default=SPK_LOWS,
                   help="the band under %d Hz against the one over it, "
                        "each levelled apart (1: one band; default "
                        "%%(default)s)" % SPK_SPLIT)
    s.add_argument("--idle", type=float, default=SPK_IDLE,
                   help="s: the resting width's slide in the quiet (default "
                        "%(default)s; 0 off)")
    s.add_argument("--ratio", type=float, default=None,
                   help="the leveller's ratio (the style's)")
    s.add_argument("--range", type=float, default=None,
                   help="dB: the most a quiet passage is raised (the "
                        "style's)")
    s = sub.add_parser("decode")
    s.add_argument("file")
    s.add_argument("--frame", type=int, required=True)
    s.add_argument("--png", required=True)
    s = sub.add_parser("stat", help="an XDV's frames in the plan's lists")
    s.add_argument("files", nargs="+")
    s = sub.add_parser("verify", help="a V88 (SPEC.md 98.1.6), or an XDV's "
                       "lists against its own programs")
    s.add_argument("files", nargs="+")
    s.add_argument("--against", help="the XDV a V88 was imported from")
    s = sub.add_parser("synthxdv",
                       help="a synthetic XDC stream of synth_frames(), for "
                       "a VIDBENCH.DAT made from nothing outside the tree "
                       "(make vid486)")
    s.add_argument("out")
    s.add_argument("--repeat", type=int, default=3)
    s = sub.add_parser("benchdat")
    s.add_argument("out")
    s.add_argument("files", nargs="+")
    s.add_argument("--max", type=int, default=32)
    s.add_argument("--frame", action="append", metavar="FILE:INDEX",
                   help="time exactly these frames instead of the picks")
    s.add_argument("--extra", action="append", metavar="FILE:INDEX",
                   help="time this frame as well as the picks")
    s.add_argument("--synth", action="store_true",
                   help="append the one-construct frames (synth_frames)")
    s.add_argument("--limit", type=int, default=150 * 1024)
    a = ap.parse_args()
    if a.cmd == "encode":
        a.title = a.title or os.path.splitext(os.path.basename(a.out))[0]
    try:
        return {"stat": cmd_stat, "verify": cmd_verify, "import": cmd_import,
                "encode": cmd_encode, "info": cmd_info, "decode": cmd_decode,
                "benchdat": cmd_benchdat, "poster": cmd_poster,
                "synthxdv": cmd_synthxdv,
                "title": cmd_title, "speaker": cmd_speaker,
                "spkwav": cmd_spkwav}[a.cmd](a) or 0
    except (XdvError, V88Error) as e:
        sys.exit("os88vid: %s" % e)


if __name__ == "__main__":
    sys.exit(main())
