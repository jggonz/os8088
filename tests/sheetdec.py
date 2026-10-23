#!/usr/bin/env python3
"""A formula saved in Normal format comes back as a FORMULA (SPEC.md 81.10.10).

    make && python3 tests/sheetdec.py

Until 81.10.10 SHEET's BIFF reader kept a FORMULA record's cached result and
stepped over its tokens, so Save in Normal and reopen turned every formula
into a number. This drives both halves and holds each to something outside
SHEET.

ARM A - SHEET's OWN ROUND TRIP. The host authors a SYLK file of formulas;
SHEET opens it, saves it as Normal (BIFF3), and the HOST decodes that file
with tools/os88sheetfmt.py's decode_rpn - which checks the WRITER, and
through BIFF_FUNCS checks every function number it wrote against the
document (the six 81.10.9 found were exactly this kind). SHEET is closed,
reopens its own .BIF - the decoder - and saves it as SYLK, and the host
reads back each formula's TEXT and the VALUE SHEET computed from it. Every
formula was authored with a WRONG cached value, so a right value means the
decoded text was parsed and evaluated, not echoed.

ARM B - EXCEL'S TOKENS. SHEET's writer emitted no strings, no & and no
error constants until 81.61, and still emits no IF control tokens, so its
own files cannot reach half of the decoder. The
host writes two files the way Excel does - XL3.BIF in BIFF3, XL4.BIF as a
BIFF4 worksheet, where a function index is a word - with strings, &, TRUE,
an error, unary plus, IF with its tAttrIf/tAttrSkip control tokens, the
tAttrSum form of SUM and a tAttrVolatile. And five formulas the decoder
must REFUSE (tPercent, a name, CHOOSE's jump table, a function SHEET lacks,
a text longer than SHEET can hold): each must come back as its cached
value, which is the policy - an unreadable token costs a formula its
liveness and never its number.
"""
import math
import re
import os
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "unit"))
import os88marty as M                                       # noqa: E402
import os88flush                                            # noqa: E402
import os88geom                                             # noqa: E402
import os88sheetfmt as F                                     # noqa: E402
import os88sym                                              # noqa: E402
from os88mouse import Mouse                                  # noqa: E402
import dispcp                                                # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
import sheetfin as FN                                        # noqa: E402

WORK = "build/sheetdec"                 # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetdec.img"
WRONG = -999.0
MFLOWS = FN.MFLOWS

# --- arm A: what SHEET's writer can express ---------------------------------
VALUES = {(0, 0): 5.0, (1, 0): 3.0, (2, 0): 2.0, (4, 0): 'abc', (5, 0): 'XyZ'}
VALUES.update({(i, 1): v for i, v in enumerate(MFLOWS)})
T = ('bool', True)
ARM_A = [
    ('A1+A2*A3',              11.0),
    ('(A1+A2)*A3',            16.0),
    ('-A1+A2',                -2.0),
    ('$A$1*2',                10.0),
    ('SUM(A1:A3)',            10.0),
    ('AVERAGE(A1,A2,A3)',     10.0 / 3),
    ('ROUND(A1/A2,2)',        1.67),
    ('IF(A1>A2,1,0)',         1.0),
    ('MOD(A1,A2)',            2.0),
    ('ABS(-A1)',              5.0),
    ('PI()',                  math.pi),
    ('PMT(0.01,60,-20000)',   FN.pmt(0.01, 60, -20000)),
    ('RATE(60,-500,20000)',   FN.rate(60, -500, 20000)),
    ('INDEX(A1:A3,2)',        3.0),
    ('MIRR(B1:B6,0.1,0.12)',  FN.mirr(MFLOWS, 0.1, 0.12)),
    ('A1>=A2',                T),
    ('A1<>A2',                T),
    # 81.51: a bare TRUE is tBool, Excel's token for the constant, and a
    # comparison's cached result is the file's own logical (byte 0 = 1)
    ('TRUE',                  T),
    ('AND(A1>A2,TRUE)',       T),
    # 81.10.11: a TEXT result is a FORMULA record and a STRING record now...
    ('UPPER(A5)',             'ABC'),
    ('LOWER(A6)',             'xyz'),
    # ...and an ERROR result carries the FILE's code, 07H, not ERROR.TYPE's 2
    ('1/0',                   ('err', '#DIV/0!')),
    # 81.61: a STRING CONSTANT (tStr), '&' (tConcat) and an ERROR CONSTANT
    # (tErr) are tokens too. The writer refused all three and saved the
    # formula as its value. The first also carries a ';', which SYLK must
    # double in the ;E field both ways - read as the field's end, the
    # formula came in as `"a` and went out cut at the same place
    ('"a;b"&A5',              'a;babc'),
    ('A5&"-"&A1',             'abc-5'),
    ('IF(A1>A2,"big","small")', 'big'),
    ('"q""t"',                'q"t'),
    ('#N/A',                  ('err', '#N/A')),
    ('ISERROR(#REF!)',        T),
]
COL = 2


# --- arm B: Excel's token arrays, built by hand (excelfileformat 3.4-3.10) --
def fn(idx, ver):
    return struct.pack('<B' if ver == 3 else '<H', idx)


def ref(roww, col):
    return bytes([0x44]) + struct.pack('<HB', roww, col)


R = 0xC000                              # both relative: A1 exactly as typed
num = lambda x: bytes([0x1F]) + struct.pack('<d', x)
tint = lambda n: bytes([0x1E]) + struct.pack('<H', n)
tstr = lambda s: bytes([0x17, len(s)]) + s.encode('latin-1')
attr = lambda flags, w=0: bytes([0x19, flags]) + struct.pack('<H', w)


def arm_b(ver):
    """[(tokens, expected text or None for REFUSED, expected value, cached)]"""
    fv = lambda argc, idx: bytes([0x42, argc]) + fn(idx, ver)
    f = lambda idx: bytes([0x41]) + fn(idx, ver)
    area = bytes([0x45]) + struct.pack('<HHBB', R, R | 2, 0, 0)
    return [
        (ref(R, 0) + tint(1) + b'\x0d' + attr(0x02, 7) + tstr('big') +
         attr(0x08, 0) + tstr('small') + attr(0x08, 0) + fv(3, 1),
         'IF(A1>1,"big","small")', 'big', WRONG),
        (tstr('a"b') + tstr('c') + b'\x08', '"a""b"&"c"', 'a"bc', WRONG),
        (area + attr(0x10), 'SUM(A1:A3)', 10.0, WRONG),
        (ref(R, 0) + b'\x12', 'A1', 5.0, WRONG),
        (b'\x1d\x01', 'TRUE', ('bool', True), WRONG),     # bare, as Excel
                                                          # shows it (81.51)
        (b'\x1c\x07', '#DIV/0!', ('err', '#DIV/0!'), WRONG),
        (ref(0x0000, 0) + ref(0x4001, 0) + b'\x03', '$A$1+A$2', 8.0, WRONG),
        (num(2.567) + tint(1) + f(27), 'ROUND(2.567,1)', 2.6, WRONG),
        (ref(R, 0) + ref(R | 1, 0) + fv(2, 7) + b'\x13\x15',
         '(-MAX(A1,A2))', -5.0, WRONG),
        (num(0.01) + tint(60) + tint(20000) + b'\x13' + fv(3, 59),
         'PMT(0.01,60,-20000)', FN.pmt(0.01, 60, -20000), WRONG),
        (tstr('abc') + f(113), 'UPPER("abc")', 'ABC', WRONG),
        (attr(0x01) + f(63), 'RAND()', 'rand', WRONG),
        # --- REFUSED: each keeps the value it was cached with ---
        (tint(50) + b'\x14', None, 12345.0, 12345.0),        # tPercent
        (bytes([0x43]) + struct.pack('<H', 1) + bytes(8), None, 777.0, 777.0),
        (tint(1) + attr(0x04, 2) + b'\x04\x00\x08\x00' + tint(2) + tint(3)
         + fv(3, 100), None, 2.0, 2.0),                      # tAttrChoose
        (area + fv(1, 110), None, 55.0, 55.0),               # EXEC: a real
                                                              # Excel function
                                                              # SHEET declines
                                                              # (81.93), with
                                                              # no entry in
                                                              # either table.
                                                              # This was DSUM
                                                              # until 81.65
                                                              # built it, and
                                                              # then OFFSET
                                                              # until 81.85 did
        (b''.join(ref(R | r, 0) for r in range(3)) + b'\x03\x03' +
         b''.join(ref(R | r, 0) + b'\x03' for r in range(20)), None, 99.0,
         99.0),                                              # > SH_EDITMAX
        # --- REFUSED, with a cached result that is NOT a number (81.10.11):
        # text comes in the STRING record after it, an error in the file's
        # own numbering (07H is #DIV/0!), a logical as the logical (81.51)
        (tint(2) + attr(0x04, 2) + b'\x04\x00\x0a\x00' + tstr('one') +
         tstr('two') + fv(3, 100), None, 'two', ('str', 'two')),
        (tint(50) + b'\x14', None, ('err', '#DIV/0!'), ('err', 0x07)),
        (tint(1) + b'\x14', None, ('bool', True), ('bool', 1)),
    ]


def rec(op, body):
    return struct.pack('<HH', op, len(body)) + body


def write_biff(ver, cases):
    """A worksheet stream as Excel writes one: BOF, the cells, EOF. No FONT or
    XF records - SHEET's reader applies a format it tracked and leaves an
    untracked XF index alone, which is what a file carrying none is."""
    out = rec(0x0209 if ver == 3 else 0x0409,
              struct.pack('<HHH', 0x0300 if ver == 3 else 0x0400, 0x0010, 0))
    for (r, c), v in sorted(VALUES.items()):
        if isinstance(v, float) and c == 0:
            out += rec(0x0203, struct.pack('<HHHd', r, c, 0, v))
    fop = 0x0206 if ver == 3 else 0x0406
    for i, (tok, _, _, cached) in enumerate(cases):
        after = b''
        if isinstance(cached, tuple):           # 5.50: not a number at all
            kind, v = cached
            if kind == 'str':
                res = bytes([0, 0, 0, 0, 0, 0, 0xFF, 0xFF])
                after = rec(0x0207, struct.pack('<H', len(v)) + v.encode())
            else:
                res = bytes([2 if kind == 'err' else 1, 0, v, 0, 0, 0, 0xFF, 0xFF])
        else:
            res = struct.pack('<d', cached)
        out += rec(fop, struct.pack('<HHH', i, COL, 0) + res
                   + struct.pack('<HH', 0, len(tok)) + tok) + after
    return out + rec(0x000A, b'')


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    cells = dict(VALUES)
    for i, (expr, _) in enumerate(ARM_A):
        cells[(i, COL)] = ('formula', expr, WRONG)
    files = {"SHIN.SLK": F.write_sylk(cells),
             "XL3.BIF": write_biff(3, arm_b(3)), "XL4.BIF": write_biff(4, arm_b(4))}
    for n, data in files.items():
        open(os.path.join(WORK, n), "wb").write(data)
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL", "build/MACRO.OVL"]
                   + [os.path.join(WORK, n) for n in files],
                   check=True, stdout=subprocess.DEVNULL)
    return files


def r1c1_to_a1(t, row, col):
    """SHEET's SYLK writer spells references R1C1, as SYLK does: R[-2]C is
    two rows up, R3C1 is absolute. Back to A1 relative to the cell, so the
    text compares with what was typed."""
    def part(p, base):
        if p == '':
            return base, True
        if p.startswith('['):
            return base + int(p[1:-1]), True
        return int(p) - 1, False

    def rep(m):
        r, rrel = part(m.group(1), row)
        c, crel = part(m.group(2), col)
        return (('' if crel else '$') + F.colname(c)
                + ('' if rrel else '$') + str(r + 1))
    return re.sub(r'(?<![A-Z])R(\[-?\d+\]|\d*)C(\[-?\d+\]|\d*)(?![A-Z(])',
                  rep, t)


def formula_is(g, expr, row):
    return (isinstance(g, tuple) and g[0] == 'formula' and g[1] is not None
            and r1c1_to_a1(g[1], row, COL) == expr)


def value_ok(want, got, tol=1e-6):
    if isinstance(got, tuple) and got and got[0] == 'formula':
        got = got[2]
    # (SHEET's comparisons and TRUE() answered 1 where Excel answers a
    # LOGICAL, and this accepted 1.0 for TRUE until 81.51 closed the gap)
    if want == 'rand':
        return isinstance(got, float) and 0.0 <= got < 1.0
    if isinstance(want, float):
        return (isinstance(got, float)
                and abs(got - want) <= tol * max(1.0, abs(want)))
    return got == want


def main():
    os.chdir(os.path.join(HERE, ".."))
    files = build_disk()
    S = os88sym.linear
    with M.launch(SF.SYS, apps=DISK, machine=SF.MACHINE) as m:
        M.settle(m)
        mo = Mouse(marty=m)
        dispcp.open_drive(m, mo, S, M.settle, letter="B")
        M.settle(m)
        ds = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, ds)
        vol = lambda: os88flush.Flush(marty=m).volume(1)

        def open_file(name):
            dispcp.open_named(m, mo, S, M.settle, wx, wy, name=name)
            M.settle(m, limit=240)

        def save_as(kind, name, before=None):
            mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1], SF.SAVE_AS[0],
                    SF.SAVE_AS[1])
            M.settle(m)
            mo.click(*SF.fdlg_xy(m, SF.FMT_ROW[kind]))
            M.settle(m)
            mo.click(*SF.fdlg_okxy(m))
            M.settle(m, limit=120)
            mo.click(*SF.SAVE_BUTTON)
            # the file is the event - and when SHEET overwrites one that was
            # already there, the event is its CONTENTS changing
            for _ in range(60):
                v = vol()
                if name in v.names() and v.read(name) != before:
                    return v.read(name)
                M.settle(m, quiet=2.0, stable=2, limit=60)
            return None

        def close_sheet():
            w = [x for x in os88geom.windows(m, S)
                 if x.visible and x.title.startswith("Sheet")]
            if w:
                mo.click(w[-1].x + 8, w[-1].y + 9)
                M.settle(m)

        # --- arm A --------------------------------------------------------
        open_file("SHIN.SLK")
        FN.shot(m, "dec-1-loaded")
        bif = save_as('bif', "SHIN.BIF")
        check(bif is not None, "SHEET saved SHIN.BIF",
              "no SHIN.BIF on B: after Save As Normal")
        close_sheet()
        open_file("SHIN.BIF")
        FN.shot(m, "dec-2-reopened")
        slk_a = save_as('slk', "SHIN.SLK", before=files["SHIN.SLK"])
        close_sheet()
        # --- arm B --------------------------------------------------------
        slk_b = {}
        for ver in (3, 4):
            open_file("XL%d.BIF" % ver)
            FN.shot(m, "dec-3-xl%d" % ver)
            slk_b[ver] = save_as('slk', "XL%d.SLK" % ver)
            close_sheet()

    # ARM A, the writer: every formula SHEET wrote decodes, on the host, to
    # the text it was typed as - and so to the function it was typed as
    if bif:
        written = F.read_biff(bif)
        for i, (expr, want) in enumerate(ARM_A):
            g = written.get((i, COL))
            # g[0] too: an error VALUE reads as ('err', '#N/A'), whose second
            # field IS the text of =#N/A - a writer that saved the value alone
            # passed this without it
            check(isinstance(g, tuple) and g[0] == 'formula'
                  and g[1] == expr and value_ok(want, g),
                  "written as a formula: =%s" % expr,
                  "the host reads SHEET's FORMULA record as %r - the text "
                  "decoded from its tokens, and the cached result (a STRING "
                  "record for text, the file's own code for an error)" % (g,))
    # ARM A, the decoder: SHEET's own file, reopened and saved as SYLK
    check(slk_a is not None, "SHEET saved the reopened .BIF as SYLK",
          "SHIN.SLK never changed after the reopen")
    back = F.read_sylk(slk_a) if slk_a else {}
    for i, (expr, want) in enumerate(ARM_A):
        g = back.get((i, COL))
        check(formula_is(g, expr, i) and value_ok(want, g),
              "reopened as a formula: =%s" % expr,
              "SHEET holds %r - wanted the formula back, computing %r" % (g, want))
    # ARM B: Excel's tokens, and the ones that must be refused
    for ver in (3, 4):
        got = F.read_sylk(slk_b[ver]) if slk_b.get(ver) else {}
        check(bool(got), "XL%d.BIF opened and saved" % ver,
              "no XL%d.SLK - SHEET did not open the file Excel's way" % ver)
        for i, (tok, expr, want, cached) in enumerate(arm_b(ver)):
            g = got.get((i, COL))
            if expr is None:
                check(value_ok(want, g) and not (isinstance(g, tuple)
                                                 and g[0] == 'formula'),
                      "BIFF%d refused, kept its value: %s" % (ver, tok.hex()),
                      "SHEET holds %r - a refused token must leave the "
                      "cached %r as a plain value" % (g, cached))
            else:
                check(formula_is(g, expr, i) and value_ok(want, g),
                      "BIFF%d: =%s" % (ver, expr),
                      "SHEET holds %r - wanted =%s computing %r"
                      % (g, expr, want))
    done("sheetdec")


if __name__ == "__main__":
    main()
