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
    for a, bs, run in ops:
        if run or len(bs) < rmin:
            out.append((a, bs, run))
            continue
        i = 0
        start = 0
        n = len(bs)
        while i < n:
            j = i
            while j < n and bs[j] == bs[i]:
                j += 1
            if j - i >= rmin:
                if i > start:
                    out.append((a + start, bs[start:i], False))
                out.append((a + i, bs[i:j], True))
                start = j
            i = j
        if start < n:
            out.append((a + start, bs[start:], False))
    return out


ABS_BELOW = 4                   # a cluster of fewer entries than this goes
                                # into an ABSOLUTE segment (wave 0: a skip
                                # segment's set-up is ~228 cycles, an
                                # absolute entry ~11 more than a skip entry,
                                # and below 4 the absolute form is ALSO the
                                # smaller - so it wins both ways there)


def to_lists(ops, abs_below=ABS_BELOW):
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

    for k in range(NLISTS):
        L = sorted(lists[k])
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
F_KNOWN = F_RESIDENT | F_LOOPREC | F_REPEAT | F_LIVE | F_RUNS | F_SPKPWM

PIT_HZ = 1193182


def spk_table(rate):
    """SPEC.md 34.11.2's count table for a rate: t[s] = 1 + s(N-2)/255, N =
    1,193,182 / rate - apps/os88spk.inc's os88spk_init, to the byte"""
    n = PIT_HZ // rate
    if not 74 <= n <= 255:
        raise V88Error("%d Hz is not a rate the speaker plays (N = %d, "
                       "74..255)" % (rate, n))
    return bytes(1 + s * (n - 2) // 255 for s in range(256))


def spk_counts(samples, rate):
    """PCM8 samples as the speaker's counts (98.1.1.3)"""
    return bytes(samples).translate(spk_table(rate))
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

    def __init__(self, layout, wb, h, bitplanes=False):
        if layout not in LAYOUTS:
            raise V88Error("layout %d is not one of SPEC.md 98.1.2's" % layout)
        banks, stride, rows, self.name = LAYOUTS[layout]
        if not (1 <= wb <= stride and 1 <= h <= rows):
            raise V88Error("a %d x %d canvas does not fit %s (%d x %d)"
                           % (wb, h, self.name, stride, rows))
        self.layout, self.wb, self.h = layout, wb, h
        self.banks, self.stride = banks, stride
        self.bitplanes = bitplanes and layout == LAY_LIN80
        self.planes = 4 if layout == LAY_MODEX or self.bitplanes else 1
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
    (65,535: its length is a word, and it rides in no super-packet)"""
    if g.planes > 1:
        # MODEX (98.1.3.1): sub-records, each its Map Mask and ten lists,
        # a 0 after the last - ops is [(mask, ops), ...]
        body, every = bytearray(), []
        for mask, sub in ops:
            if sub:
                body.append(mask)
                body += to_lists(sub)[0]
                every += sub
        body.append(0)
        lists = bytes(body)
        y0, y1 = band_of(every, g)
    else:
        lists, e, s = to_lists(ops)
        y0, y1 = band_of(ops, g)
    n = REC_HDR + len(lists) + len(audio)
    if n > limit:
        raise V88Error("a record of %d bytes cannot fit %s" % (
            n, "a super-packet" if limit < 65534 else "its length word"))
    return struct.pack("<HHH", n, y0, y1) + lists + audio


PREV_MAX = 31 * 1024             # a flipped play's copy of the last record
                                # (98.3.8): the seam passes through it too


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


def vga4_pack(cv, w, h, step=1):
    """The canvas as OSAPI_GFX_BLIT4 takes it - two pixels a byte, the
    left in the high nibble - every `step`-th pixel of every `step`-th row:
    the player's vp_v4pack, byte for byte"""
    ow, oh = w // step, (h + step - 1) // step
    obw = (ow + 1) // 2
    out = bytearray(obw * oh)
    for oy in range(oh):
        row = cv[oy * step * w:(oy * step + 1) * w]
        for ox in range(ow):
            v = row[ox * step] & 15
            out[oy * obw + ox // 2] |= v << 4 if not ox & 1 else v
    return bytes(out), obw, ow, oh


def modex_subs(cv, changed, g):
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
RING_SLOTS = (0, 2, 4, 8)   # ...0 or a power of two to the player's most
SLOT = 32768


def ring_for(reserve):
    """The ring slots a disk reserve of `reserve` bytes needs: what it
    banks is read ahead of the slot being decoded, so the reserve and one
    slot more, as a power of two - None if no ring holds it"""
    for k in RING_SLOTS[1:]:
        if (k - 1) * SLOT >= reserve:
            return k
    return None


class Writer:
    """Collects a stream frame by frame and writes SPEC.md 98.1's file."""

    def __init__(self, g, rate, spf, audio_fmt, abytes, pixfmt, title="",
                 credits="", aspect=None, keysecs=KEY_SECS, palette=None,
                 rowscale=1, flip=False, loop=None, repeat=False,
                 cgapal=None, spk=False):
        if spk and audio_fmt != AUD_PCM8:
            raise V88Error("speaker counts are PCM8's (98.1.1.3)")
        if spk:
            spk_table(rate)
        self.spk = spk
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
        if flip and g.layout != LAY_MODEX:
            raise V88Error("page flipping is Mode X's (98.3.8)")
        self.flip = flip
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
        if (pixfmt == PF_VGA8) != (palette is not None) or \
                (palette is not None and (len(palette) != PAL_BYTES or
                                          max(palette) > 63)):
            raise V88Error("a VGA8 file carries 768 palette bytes of 0..63, "
                           "and no other file carries any")
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

    def frame(self, ops, surf, audio=b""):
        """`ops` are the frame's writes, already applied to `surf`."""
        if len(audio) != self.abytes:
            raise V88Error("frame %d has %d audio bytes, not %d"
                           % (len(self.recs), len(audio), self.abytes))
        k = len(self.recs)
        self.recs.append(record(ops, self.g, audio))
        self.last = surf
        if k == self.loop:
            self.loop_surf, self.loop_audio = bytes(surf), audio
        if k >= self.key0 and (k - self.key0) % self.keyint == 0:
            kop = keyframe_ops(surf, self.g)
            try:
                # 65,535 is the table's length word; an ADPCM4 file's
                # write() appends the reference byte (98.1.1.1) to every
                # key, so its records must leave that byte room
                self.keys.append((k, record(
                    kop, self.g, limit=65535 - (
                        self.audio_fmt == AUD_ADPCM4)),
                    self.g.canvas(surf)))
            except V88Error:
                # a VGA8 canvas past its record's length word (a 320 x
                # 240 MODEX one can be) has no keyframe here: the file
                # still plays from the start, and seeks to the ones it has
                self.skipped = getattr(self, "skipped", 0) + 1

    def write(self, path, poster=None):
        g = self.g
        if not self.recs:
            raise V88Error("no frames")
        # the stream: greedy super-packets, then chained
        sps, where = [], []          # where[f] = (super-packet, index)
        cur, size = [], 4
        for r in self.recs:
            if cur and size + len(r) > SP_MAX * SECTOR:
                sps.append(cur)
                cur, size = [], 4
            where.append((len(sps), len(cur)))
            cur.append(r)
            size += len(r)
        sps.append(cur)
        secs = [-(-(4 + sum(len(r) for r in sp)) // SECTOR) for sp in sps]
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
            seam = record(seam_ops(self.last, self.loop_surf, g), g,
                          self.loop_audio, limit=65535)
            if self.flip and len(seam) > PREV_MAX:
                raise V88Error("the seam is %d bytes, past a flipped play's "
                               "%d" % (len(seam), PREV_MAX))
        nk = len(keys)
        ktab = -(-16 * nk // SECTOR) * SECTOR if nk else 0
        krec = sum(len(r) for k, r, c in keys) + len(seam)
        pal = SECTOR if self.palette else 0     # the palette: sector 1
        kbase = SECTOR + (2 * SECTOR if self.palette else 0) + ktab
        ktoff = SECTOR + (2 * SECTOR if self.palette else 0)
        s0 = kbase + -(-krec // SECTOR) * SECTOR
        spoff, o = [], s0
        for n in secs:
            spoff.append(o)
            o += n * SECTOR
        stream = bytearray()
        for i, sp in enumerate(sps):
            nxt = secs[i + 1] if i + 1 < len(sps) else 0
            b = struct.pack("<HH", len(sp), nxt) + b"".join(sp)
            stream += b + bytes(secs[i] * SECTOR - len(b))
        kt, kr, o = bytearray(), bytearray(), kbase
        for k, r, c in keys:
            if k + 1 < len(self.recs):
                spi, idx = where[k + 1]
                sp_at, sp_n = spoff[spi], secs[spi]
            else:
                sp_at, sp_n, idx = 0, 0, 0
            if idx > 255:
                raise V88Error("frame %d is record %d of its super-packet; "
                               "a keyframe can name 255" % (k + 1, idx))
            kt += struct.pack("<IIHIBB", k, o, len(r), sp_at, sp_n, idx)
            kr += r
            o += len(r)
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
        if poster is None:
            poster = next((i for i, (k, r, c) in enumerate(keys)
                           if not flat(c)), 0 if nk else 0xFFFF)
        elif not 0 <= poster < nk:
            raise V88Error("--poster %d: there are %d keyframes" % (poster, nk))
        hdr = bytearray(SECTOR)
        hdr[0:4] = V88_SIG
        flags = (F_LOOPREC if seam else 0) | (F_REPEAT if self.repeat else 0) \
            | (F_SPKPWM if self.spk else 0)
        struct.pack_into("<HHIHHBBH", hdr, 4, 1, flags, len(self.recs),
                         self.rate,
                         self.spf, self.audio_fmt, 1, self.abytes)
        struct.pack_into("<HB", hdr, 20, *pit_rate(self.rate, self.spf))
        hdr[H_RING] = getattr(self, "ring", 0)
        for off, size, text in ((32, 48, self.title), (80, 96, self.credits)):
            t = text.encode("ascii", "replace")[:size - 1]
            hdr[off:off + len(t)] = t
        struct.pack_into("<BBHHBBIHHIHHIHH", hdr, 192, self.pixfmt,
                         g.layout, g.wb, g.h, self.aspect[0], self.aspect[1],
                         ktoff if nk else 0, nk, poster, s0, secs[0],
                         max(secs), len(stream),
                         max(len(r) for r in self.recs),
                         max((len(r) for k, r, c in keys), default=0))
        struct.pack_into("<IBB", hdr, 224, pal,
                         self.rowscale if self.rowscale > 1 else 0,
                         2 if self.flip else 0)
        if self.cgapal is not None:
            hdr[192 + R_CGAPAL] = self.cgapal
        hdr[LOOP_AT:LOOP_AT + len(loopblk)] = loopblk
        front = bytes(hdr)
        if self.palette:
            front += self.palette + bytes(2 * SECTOR - PAL_BYTES)
        out = front + kt + bytes(ktab - len(kt)) + kr + \
            bytes(s0 - kbase - len(kr)) + stream
        write_whole(path, out)
        return dict(bytes=len(out), keys=nk, keybytes=ktab + len(kr),
                    stream=len(stream), sps=len(sps), poster=poster,
                    seam=len(seam))


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
        si = walk_lists(bytearray(65536), rec, si + 1)
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
            si = walk_lists(bytearray(65536), rec, si + 1, write)
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
                   posters=None, live=None, targets=None, spk=False):
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
        (F_SPKPWM if spk else 0)
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
    out = bytes(hdr) + bytes(body)
    write_whole(path, out)
    return dict(bytes=len(out), blocks=sizes,
                audio=(len(audio), len(apk) if audio else 0))


def walk_lists(buf, data, si, write=None, seg=None):
    """Apply the ten lists at data[si:] to `buf` (a surface image); call
    write(k, di, n) for each write first and seg(absolute) for each
    segment. Returns the index past the tenth list. A truncated list raises
    V88Error."""
    try:
        for k in range(NLISTS):
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
        if self.frames < 1 or self.spf < 1 or self.rate < 1:
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
        # its bursts are banked in, a power of two the player's own; 0 says
        # nothing, and a RESIDENT file has no ring
        self.ring = d[H_RING]
        if self.ring not in RING_SLOTS or \
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
        if self.spk:
            spk_table(self.rate)
        if self.runs and not self.live:
            raise V88Error("blit runs in a file that is not LIVE (98.1.3.4)")
        self.target = d[self.slot + R_TARGET]
        if self.live and not self.resident:
            raise V88Error("a LIVE file is RESIDENT (98.3.10)")
        if self.target > 3 or (self.target and not self.resident):
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
        self.g = Geom(layout, wb, h, bitplanes=self.pixfmt == PF_VGA4)
        pal, rs, fl = struct.unpack_from("<IBB", d, self.slot + 32)
        self.rowscale = rs or 1
        if fl not in (0, 1, 2) or (fl == 2 and layout != LAY_MODEX):
            raise V88Error("a flip byte of %d on layout %d" % (fl, layout))
        self.flip = fl == 2
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
        elif pal:
            raise V88Error("a palette at %d in a %s file"
                           % (pal, PF_NAMES[self.pixfmt]))
        self.aspect = (an, ad)
        if self.resident:
            self._blocks(d)
        elif not (1 <= self.sp0n <= SP_MAX and 1 <= self.spmax <= SP_MAX) \
                or self.sp0 % SECTOR:
            raise V88Error("first super-packet at %d, %d sectors, largest %d"
                           % (self.sp0, self.sp0n, self.spmax))
        if self.nkeys and (self.ktab % SECTOR or
                           self.ktab + 16 * self.nkeys > len(d)):
            raise V88Error("the keyframe table at %d does not fit" % self.ktab)
        if self.poster != 0xFFFF and self.poster >= self.nkeys:
            raise V88Error("poster %d of %d keyframes"
                           % (self.poster, self.nkeys))
        self.keys = [struct.unpack_from("<IIHIBB", d, self.ktab + 16 * i)
                     for i in range(self.nkeys)]
        self.repeat = bool(flags & F_REPEAT)
        self.loop = None
        blk = struct.unpack_from(LOOP_FMT, d, LOOP_AT)
        if flags & F_LOOPREC and self.resident:
            # RESIDENT: the seam is each block's record after the last
            # frame's, so the loop block names only L
            if not blk[0] + 1 < self.frames or any(blk[1:]) or \
                    any(d[LOOP_AT + 16:SECTOR]):
                raise V88Error("a resident file's loop block is L alone, "
                               "L + 1 < frames")
            self.loop = (blk[0], 0, len(self._seam), 0, 0, 0)
        elif flags & F_LOOPREC:
            L, off, n, secs, idx, spo = blk
            minrec = REC_HDR + (1 if self.g.planes > 1 else 10) + self.abytes
            if not (L + 1 < self.frames and n >= minrec and
                    off + n <= len(d) and 1 <= secs <= SP_MAX and
                    not spo % SECTOR and spo >= self.sp0 and
                    spo + secs * SECTOR <= len(d)):
                raise V88Error("the loop block (from frame %d, seam %d+%d, "
                               "super-packet %d x %d, record %d) does not "
                               "fit the file" % (L, off, n, spo, secs, idx))
            self.loop = blk
        elif any(blk) or any(d[LOOP_AT + 16:SECTOR]):
            raise V88Error("a loop block with no LOOPREC flag")

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
            if not 1 <= nsec <= SP_MAX:
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

    def apply(self, surf, rec, key=False, check=True):
        """Decode one record onto `surf`; with `check`, enforce the WRITER's
        rules too (SPEC.md 98.1.3): canvas-only, no byte written twice,
        rows inside y0..y1, audio exact."""
        g = self.g
        n, y0, y1 = struct.unpack_from("<HHH", rec, 0)
        seens = [bytearray(65536) for _ in range(g.planes)]
        wrote = [0]
        plane = [0]
        writes = []

        def write(k, di, m):
            seen = seens[plane[0]]
            if not all(g.valid[di:di + m]):
                raise V88Error("a write at %04x+%d leaves the canvas" % (di, m))
            for a in (di, di + m - 1):
                if not y0 <= g.rowof[a] < y1:
                    raise V88Error("a write on row %d is outside the band "
                                   "%d..%d" % (g.rowof[a], y0, y1))
            if any(seen[di:di + m]):
                raise V88Error("two writes overlap at %04x" % di)
            seen[di:di + m] = b"\x01" * m
            wrote[0] += m
            writes.append((di, m))
        if g.planes == 1:
            end = walk_lists(surf, rec, REC_HDR, write if check else None)
        else:                       # 98.1.3.1: a Map Mask, then its lists
            si, mv = REC_HDR, memoryview(surf)
            while True:
                if si >= len(rec):
                    raise V88Error("the sub-records run off their record")
                mask = rec[si]
                si += 1
                if not mask:
                    break
                if mask > 15:
                    raise V88Error("a Map Mask of %02x" % mask)
                for p in range(4):
                    if mask >> p & 1:
                        plane[0] = p
                        end = walk_lists(mv[p * PLANE:(p + 1) * PLANE], rec,
                                         si, write if check else None)
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
        want = (1 if self.audio == AUD_ADPCM4 else 0) if key else self.abytes
        if tail != want:
            raise V88Error("%d bytes follow the lists, not %d" % (tail, want))
        if check and not wrote[0] and y0 != y1:
            raise V88Error("an empty record with a band %d..%d" % (y0, y1))
        return rec[end:]

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

    def key(self, i):
        k, off, n, spo, spn, idx = self.keys[i]
        rec = self.d[off:off + n]
        if len(rec) != n or n < REC_HDR + (1 if self.g.planes > 1 else 10):
            raise V88Error("keyframe %d runs off the file" % i)
        return k, rec, spo, spn, idx


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


def cycles_of(rec, planar=False, layout=None):
    """The wave 0 model's cycles for one record, writing CGA's screen: the
    frame's fixed cost, a set-up per skip segment, each entry by its list,
    and an absolute entry's extra address. A MODEX record's sub-records
    are decoded once each whatever their mask, and pay an OUT. `layout`
    picks another decoder's constants (CYC_LAYOUT)"""
    fr, sg, ab, cp, csl, crn = cyc_table(layout)
    c = [fr]

    def write(k, di, m):
        c[0] += ab if mode[0] else 0
        if k <= L_P6:
            c[0] += cp[k]
        elif k in (L_SLICE, L_SLICEL):
            c[0] += csl[0] + csl[1] * m
        else:
            c[0] += crn[0] + crn[1] * m

    def seg(absolute):
        c[0] += 0 if absolute else sg
        mode[0] = absolute
    mode = [False]
    if not planar:
        walk_lists(bytearray(65536), rec, REC_HDR, write, seg)
        return c[0]
    si = REC_HDR
    while rec[si]:
        c[0] += CYC_SUB
        si = walk_lists(bytearray(65536), rec, si + 1, write, seg)
    return c[0]


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
    idx, nibt, gidx, sq = T["idx"], T["nibt"], T["gidx"], T["sq"]
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
        cand = cost[gidx]                           # (4, 256, G)
        j = cand.argmin(axis=2)
        best = cand.min(axis=2)
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


def adpcm4_search(pcm, ref=ADPCM4_REF, scale=0, zeros=(), jobs=1,
                  seg=88200, lead=4096):
    """PCM8 -> ADPCM4 by EXACT SEARCH (Viterbi): the nibble sequence whose
    decode has the least total squared error, where adpcm4_encode picks
    each nibble for its own sample alone. The decoder has 1,024 states - a
    sample and a scale of 0, 16, 32 or 48 - so the trellis is a numpy step
    a sample. MEASURED on 12 s of Bad Apple's sound: 27.3 dB against the
    greedy encoder's 19.9. `zeros` as adpcm4_encode's.

    ON `jobs` CORES the stream is cut into segments, and each is searched
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
                  spk=False):
    """SPEC.md 98.2's minimal encoder: every changed byte, losslessly.
    `spk` stores PCM8 as the speaker's counts (98.1.1.3)
    `live` (cga, herc, vga) makes the resident file LIVE for that screen
    (98.3.10) - the layout must be lin80"""
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
            chunks = [spk_counts(c, rate) for c in chunks]
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
                spk=spk and bool(rab))
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
                    flip=False, loop=None, repeat=False, cgapal=None):
    """encode_frames for canvases already in hand (bytes, g.wb a row) - the
    only way to make a VGA8 file without a video (SPEC.md 98.2): silent,
    every changed byte"""
    rate, spf = max(1, round(fps * 100)), 100
    wr = Writer(g, rate, spf, AUD_NONE, 0, pixfmt, title=title,
                keysecs=keysecs, palette=palette, rowscale=rowscale,
                flip=flip, loop=loop, repeat=repeat, cgapal=cgapal)
    surf = g.surface()
    prev = g.canvas(surf)
    for cv in canvases:
        if g.planes > 1:
            subs = vga4_subs(cv, prev, g) if g.bitplanes else \
                modex_subs(cv, [a != b for a, b in zip(cv, prev)], g)
            g.put(surf, cv)
            prev = cv
            wr.frame(subs, surf)
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


def v88_frames(r, check=True):
    """(frame, surface after it) for every frame of the stream, checking
    each record; and the stream's own totals against the header's."""
    surf = r.g.surface()
    f = 0
    for rec, at, i in r.records():
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


def cmd_info(a):
    for path in a.files:
        r = Reader(path)
        g = r.g
        secs = r.frames / r.fps
        cyc = [cycles_of(rec, r.g.planes > 1, r.g.layout)
               for rec, at, i in r.records()]
        period = HZ / r.fps
        print("%s: '%s'" % (path, r.title))
        if r.credits:
            print("   credits: %s" % r.credits)
        print("   %d frames at %.3f fps (%d Hz / %d), %.1f s; audio %s; "
              "PIT %d x %d" % (r.frames, r.fps, r.rate, r.spf, secs,
                               {0: "none", 1: "PCM8", 2: "ADPCM4"}[r.audio]
                               + (" as speaker counts" if r.spk else ""),
                              r.pitdiv,
                               r.pitper))
        print("   canvas %d x %d%s on %s, %s, aspect %d:%d"
              % (g.wb // 2 if r.pixfmt in (PF_C512, PF_TEXT) else
                 g.wb * (4 if r.pixfmt == PF_CGA4 else
                         PIX_PER_BYTE[g.layout]), g.h,
                 " (each row shown twice)" if r.rowscale > 1 else "",
                 g.name, PF_NAMES[r.pixfmt], *r.aspect))
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
        print("   %d keyframes, poster %s; front matter %d bytes"
              % (r.nkeys, r.poster if r.poster != 0xFFFF else "none", r.sp0))
        print("   CPU (wave 0 model, CGA screen): mean %.1f%%, worst %.1f%% "
              "(frame %d)" % (100 * sum(cyc) / len(cyc) / period,
                              100 * max(cyc) / period, cyc.index(max(cyc))))


def decode_at(r, n):
    """The canvas after frame `n`, through the keyframe at or before it."""
    if not 0 <= n < r.frames:
        raise V88Error("frame %d of %d" % (n, r.frames))
    surf = r.g.surface()
    f = 0
    best = [i for i, e in enumerate(r.keys) if e[0] <= n]
    if best:
        k, rec, spo, spn, idx = r.key(best[-1])
        r.apply(surf, rec, key=True)
        if k == n:
            return r.g.canvas(surf)
        f = k + 1
        src = r.after_key(r.keys[best[-1]])
    else:
        src = r.records()
    for rec, _, _ in src:
        r.apply(surf, rec)
        if f == n:
            return r.g.canvas(surf)
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
            if rec[len(rec) - r.abytes:] != (want_audio[f] if want_audio
                                             else xdc_audio(pk[f], hdr)):
                raise V88Error("frame %d's audio differs from XDC's" % f)
        if f in kat:
            k, krec, spo, spk, idx = r.key(kat[f])
            kmax = max(kmax, len(krec))
            kb = g.surface()
            r.apply(kb, krec, key=True)
            if g.canvas(kb) != g.canvas(surf):
                raise V88Error("keyframe %d is not the screen after frame %d"
                               % (kat[f], f))
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
        st = adpcm4_trace(b"".join(rec[len(rec) - r.abytes:]
                                   for rec, _, _ in r.records()))
        for i, (k, off, n, spo, spk, idx) in enumerate(r.keys):
            want = st[(k + 1) * r.abytes]
            if (r.d[off + n - 1], 0) != want:
                raise V88Error("keyframe %d's ADPCM4 reference is %d; the "
                               "stream holds %d at scale %d there"
                               % (i, r.d[off + n - 1], want[0], want[1]))
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
                laud = rec[len(rec) - r.abytes:] if r.abytes else b""
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
            raise V88Error("the seam's audio is not frame %d's" % L)
        if r.flip and n > PREV_MAX:
            raise V88Error("a flipped file's seam of %d bytes" % n)
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
                        lambda d: d[:H_RING] + b"\x03" + d[H_RING + 1:],
                        "a ring of 3 slots")
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
    for f in fails:
        print("os88vid --selfcheck: FAIL - %s" % f)
    if not fails:
        print("os88vid --selfcheck: ok - encode, import (cga, herc, lin80), "
              "decode and verify agree, ADPCM4 carries a tone and seeks "
              "exactly, a seam joins its laps, a resident file's blocks "
              "round-trip, CGA4, C160, C512 and TEXT round-trip, a live "
              "file's blit runs cover its writes, and eighteen corruptions "
              "were "
              "refused")
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
                "title": cmd_title}[a.cmd](a) or 0
    except (XdvError, V88Error) as e:
        sys.exit("os88vid: %s" % e)


if __name__ == "__main__":
    sys.exit(main())
