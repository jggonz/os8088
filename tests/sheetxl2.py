#!/usr/bin/env python3
"""SHEET opens Excel 2.1's own files (SPEC.md 81.52).

    make && python3 tests/sheetxl2.py

Excel 2.1 saves BIFF2, and SHEET's BIFF reader took BIFF3 and BIFF4 only:
every BIFF2 cell record (0002H-0007H) was skipped as unknown, so a worksheet
saved by the program this app is modelled on opened EMPTY. A BIFF2 cell is a
BIFF3 one with three attribute bytes where the XF index goes (2.5.13), and a
FORMULA has one byte of flags and one of cce where BIFF3 has words; tAttr's
data is a byte, not a word (3.10).

ARM A, always: a BIFF2 file the HOST authors from the published layouts -
every cell record, an INTEGER past 32767 (it is unsigned), SUM through
BIFF2's three-byte tAttrSum, IF through tAttrIf/tAttrSkip with its text
result in a BIFF2 STRING record, a formula SHEET refuses keeping its cached
error and its cached text, and the attribute bytes' currency, bold,
alignment, borders, shade and protection. SHEET opens it and saves Normal
(BIFF3), and the host reads the values back and the XF each cell names.

ARM B, when the archive is on this machine: Excel 2.1d's OWN sample
worksheets, from the release disks kept in the shared reference library
(../LIBRARY/platforms/msdos/install-media/, or $EXCEL21D_7Z). They are
Microsoft's and are never
copied into the tree - they are extracted into build/ at run time, from the
LIBRARY disk's COMPRESS.EXE archives (the old "SZ" LZSS variant, decoded
below), and the arm is skipped with a notice when the archive is absent.
Three of them go through SHEET and come back as SYLK, and every cell must be
what the host reads in Excel's file: the text, the number, and for a formula
the same expression and - recomputed by SHEET - Excel's own cached value.
"""
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "unit"))
import os88marty as M                                       # noqa: E402
import os88flush                                            # noqa: E402
import os88sheetfmt as F                                     # noqa: E402
import os88sym                                              # noqa: E402
from os88mouse import Mouse                                  # noqa: E402
import dispcp                                                # noqa: E402
import os88ui
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
import sheetdec as SD                                        # noqa: E402

WORK = "build/sheetxl2"                 # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetxl2.img"
ARCHIVE = os.environ.get("EXCEL21D_7Z", os.path.join(
    HERE, "..", "..", "LIBRARY", "platforms", "msdos", "install-media",
    "Microsoft Excel 2.1d for Windows (1990-07) (3.5-720k).7z"))
# Arm B's three: the most formulas, AVERAGE beside SUM, and arithmetic with
# no function at all. Named A1-A3 so each is inside the Open list's six rows
# when its turn comes (tests/sheetbool.py says why that list)
CORPUS = (("EXPENSES", "A1"), ("SAMPLES1", "A2"), ("PAYROLL", "A3"))
# ...and one of Excel 2.1's own MACRO SHEETS (81.68): KWWHAT's recorded
# SELECT("RC[1]"), FORMULA("=Price-Down_Pmt"), CLEAR(1) and RETURN()
MACROS = (("KWWHAT", "A0"),)                # A0: first on the Open list
OPEN_ITEM = (SF.FILE_MENU[0] + 15, 59 + 11)
LIST_X, LIST_Y0, LIST_DY, LIST_ROWS = 150, 67, 16, 6
R = 0xC000                              # a token's row word, both relative


# --- arm A: a BIFF2 worksheet, by hand ---------------------------------------
def rec(op, body):
    return struct.pack('<HH', op, len(body)) + body


def cell(op, r, c, attr, body):
    return rec(op, struct.pack('<HH', r, c) + bytes(attr) + body)


LOCK = 0x40                             # attribute byte 0: locked (2.5.13)


def formula(r, c, attr, res, toks, after=b''):
    return cell(0x0006, r, c, attr, res + b'\x00' + bytes([len(toks)]) + toks) + after


def fnum(x):
    return struct.pack('<d', x)


def fspecial(kind, v=0):
    return bytes([kind, 0, v, 0, 0, 0, 0xFF, 0xFF])


def ref(row, col):
    return bytes([0x44]) + struct.pack('<HB', R | row, col)


def tstr(s):
    return bytes([0x17, len(s)]) + s.encode('latin-1')


def arm_a():
    out = rec(0x0009, struct.pack('<HH', 0x0002, 0x0010))       # BOF, BIFF2
    out += rec(0x0031, struct.pack('<HH', 200, 0) + b'\x04Helv')   # font 0
    out += rec(0x0031, struct.pack('<HH', 200, 1) + b'\x04Helv')   # 1: bold
    out += cell(0x0002, 0, 0, (LOCK, 0, 0), struct.pack('<H', 40000))
    out += cell(0x0003, 1, 0, (LOCK, 5, 0), fnum(2.5))             # $#,##0
    out += cell(0x0002, 2, 0, (LOCK, 0x40, 0), struct.pack('<H', 7))  # bold
    out += cell(0x0004, 3, 0, (LOCK, 0, 2 | 0x20 | 0x40 | 0x80),   # centre,
                b'\x02Hi')                                      # top+bottom,
    out += cell(0x0005, 4, 0, (LOCK, 0, 0), b'\x01\x00')        # TRUE  shaded
    out += cell(0x0005, 5, 0, (LOCK, 0, 0), b'\x07\x01')        # #DIV/0!
    out += cell(0x0002, 6, 0, (0, 0, 0), struct.pack('<H', 5))  # UNLOCKED
    area = bytes([0x25]) + struct.pack('<HHBB', R, R | 2, 0, 0)
    out += formula(0, 1, (LOCK, 0, 0), fnum(-999.0),
                   area + b'\x19\x10\x00')                      # tAttrSum
    iff = (ref(2, 0) + b'\x1e\x05\x00\x0d' + b'\x19\x02\x08' + tstr('big')
           + b'\x19\x08\x0a' + tstr('small') + b'\x19\x08\x03'
           + b'\x42\x03\x01')
    out += formula(1, 1, (LOCK, 0, 0), fspecial(0), iff,
                   rec(0x0007, b'\x03big'))                     # BIFF2 STRING
    out += formula(2, 1, (LOCK, 0, 0), fspecial(2, 0x2A),       # refused:
                   b'\x01\x02\x00\x01\x00')                     # tExp, #N/A
    out += formula(3, 1, (LOCK, 0, 0), fspecial(0),             # refused, and
                   b'\x01\x02\x00\x01\x00',                     # its text in
                   rec(0x0007, b'\x04kept'))                    # the STRING
    out += formula(4, 1, (LOCK, 0, 0), fnum(-999.0),
                   ref(0, 0) + b'\x1e\x02\x00\x05')             # A1*2
    return out + rec(0x000A, b'')


ARM_A = {
    (0, 0): 40000.0, (1, 0): 2.5, (2, 0): 7.0, (3, 0): 'Hi',
    (4, 0): ('bool', True), (5, 0): ('err', '#DIV/0!'), (6, 0): 5.0,
    (0, 1): ('formula', 'SUM(A1:A3)', 40009.5),
    (1, 1): ('formula', 'IF(A3>5,"big","small")', 'big'),
    (2, 1): ('err', '#N/A'),            # refused: the cached error, as a value
    (3, 1): 'kept',                     # ...and the cached text
    (4, 1): ('formula', 'A1*2', 80000.0),
}


def biff3_xfs(data):
    """{(row, col): (format id, bold, align, border bytes, pattern, locked)}
    from a BIFF3 stream: the XF each cell names, and the FONT that names. The
    format is the XF's own byte, the BUILT-IN id - SHEET writes no FORMAT
    records, only the four built-ins it draws (sh_biff_numfmt_tab)."""
    fonts, xfs, cells = [], [], {}
    i = 0
    while i + 4 <= len(data):
        op, ln = struct.unpack_from('<HH', data, i)
        b = data[i + 4:i + 4 + ln]
        if op == 0x0231:
            fonts.append(struct.unpack_from('<H', b, 2)[0])
        elif op == 0x0243:
            xfs.append(b)
        elif op in (0x027E, 0x0203, 0x0204, 0x0205, 0x0206):
            r, c, x = struct.unpack_from('<HHH', b, 0)
            cells[(r, c)] = x
        elif op == 0x000A:
            break
        i += 4 + ln
    out = {}
    for k, x in cells.items():
        if x >= len(xfs):
            continue
        b = xfs[x]
        font = fonts[b[0]] if b[0] < len(fonts) else 0
        out[k] = (b[1], bool(font & 1),
                  b[4] & 7, bytes(b[8:12]), b[6] & 0x3F, b[2] & 1)
    return out


# --- arm B: Excel 2.1d's own files -------------------------------------------
def unsz(d):
    """COMPRESS.EXE's early "SZ" format: an 8-byte signature, the length, then
    LZSS over a 4KB window that starts 18 bytes from its end, a flag byte per
    eight items, a set bit a literal and a clear one a 12-bit position with a
    4-bit length less three."""
    if d[:8] != b'SZ \x88\xf0\x27\x33\xd1':
        raise ValueError("not an SZ archive")
    n = struct.unpack_from('<I', d, 8)[0]
    win, pos, out, i = bytearray(b' ' * 4096), 4096 - 18, bytearray(), 12
    while i < len(d) and len(out) < n:
        flags = d[i]
        i += 1
        for b in range(8):
            if i >= len(d) or len(out) >= n:
                break
            if flags & (1 << b):
                run = d[i:i + 1]
                i += 1
            else:
                p = d[i] | ((d[i + 1] & 0xF0) << 4)
                run = bytearray()
                for k in range((d[i + 1] & 0x0F) + 3):
                    run.append(win[(p + k) & 0xFFF])
                    win[pos] = run[-1]          # a copy may overlap its own
                    pos = (pos + 1) & 0xFFF     # output, so byte by byte
                i += 2
                out += run
                continue
            out += run
            win[pos] = run[0]
            pos = (pos + 1) & 0xFFF
    return bytes(out[:n])


def a1(t, r, c):
    """a SYLK formula in A1 outside its string constants (SELECT("RC[1]")
    keeps its R1C1 text, which is the macro's, not a reference to convert)"""
    parts = t.split('"')
    return '"'.join(SD.r1c1_to_a1(p, r, c) if k % 2 == 0 else p
                    for k, p in enumerate(parts))


def corpus():
    """{short name: Excel's bytes}, or None with the reason."""
    if not os.path.exists(ARCHIVE):
        return None, "no Excel 2.1d archive at %s" % ARCHIVE
    if not (shutil.which("7z") and shutil.which("mcopy")):
        return None, "7z and mtools are needed to open the archive"
    tmp = os.path.join(WORK, "excel21d")
    os.makedirs(tmp, exist_ok=True)
    img = os.path.join(tmp, "library.img")
    if not os.path.exists(img):
        subprocess.run(["7z", "e", "-y", "-o" + tmp, ARCHIVE, "*/library.img"],
                       check=True, stdout=subprocess.DEVNULL)
    got = {}
    for (src, short), ext in ([(c, "CPS") for c in CORPUS] +
                              [(c, "CPM") for c in MACROS]):
        cps = os.path.join(tmp, src + "." + ext)
        subprocess.run(["mcopy", "-n", "-o", "-i", img,
                        "::/EXCELCBT/%s.%s" % (src, ext), cps], check=True)
        got[short] = unsz(open(cps, "rb").read())
    return got, None


def build_disk(files):
    os.makedirs(WORK, exist_ok=True)
    for n, data in files.items():
        open(os.path.join(WORK, n), "wb").write(data)
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL"]
                   + [os.path.join(WORK, n) for n in files],
                   check=True, stdout=subprocess.DEVNULL)


def same(want, got):
    if isinstance(want, tuple) and want[0] == 'formula':
        return (isinstance(got, tuple) and got[0] == 'formula'
                and got[1] == want[1] and same(want[2], got[2]))
    if isinstance(got, tuple) and got and got[0] == 'formula':
        return False                    # a value where a formula came back
    if isinstance(want, float):
        return F.close(want, got)
    return got == want


def main():
    os.chdir(os.path.join(HERE, ".."))
    xl, why = corpus()
    files = {"Z2.BIF": arm_a()}
    if xl:
        files.update({short + ".XLS": d for short, d in xl.items()})
    build_disk(files)
    S = os88sym.linear
    saved = {}
    with M.launch(SF.SYS, apps=DISK, machine=SF.MACHINE) as m:
        M.settle(m)
        M.no_saver(m)
        mo = Mouse(marty=m)
        ui = os88ui.UI(m, mouse=mo)
        dispcp.open_drive(m, mo, S, M.settle, letter="B")
        M.settle(m)
        ds = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, ds)
        vol = lambda: os88flush.Flush(marty=m).volume(1)

        def save(kind, out, before=None):
            mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1], SF.SAVE_AS[0],
                    SF.SAVE_AS[1])
            M.settle(m)
            mo.click(*SF.fdlg_xy(m, SF.FMT_ROW[kind]))
            M.settle(m)
            mo.click(*SF.fdlg_okxy(m))
            M.settle(m, limit=120)
            mo.click(*SF.SAVE_BUTTON)
            for _ in range(60):
                v = vol()
                if out in v.names() and v.read(out) != before:
                    return v.read(out)
                M.settle(m, quiet=2.0, stable=2, limit=60)
            return None

        dispcp.open_named(m, mo, S, M.settle, wx, wy, name="Z2.BIF")
        M.settle(m, limit=240)
        # SYLK for what each cell IS - it carries a formula's text where the
        # BIFF writer cannot carry a string literal - and Normal for the XF
        # each one names, which SYLK has no way to say
        saved["Z2s"] = save('slk', "Z2.SLK")
        saved["Z2"] = save('bif', "Z2.BIF", before=files["Z2.BIF"])
        for _, short in ((CORPUS + MACROS) if xl else ()):
            name = short + ".XLS"
            shown = sorted(e.name for e in vol().listdir()
                           if not e.is_system and not e.is_dir)
            row = shown.index(name)
            check(row < LIST_ROWS, "%s is on the Open list's first page"
                  % name, "row %d of %s" % (row, shown))
            mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1], *OPEN_ITEM)
            M.settle(m)
            ui.chooser_open(name)       # BY NAME: the chooser is a Disk
                                        # window since the 2026-10-14 merge
                                        # (38.3), LIST_* being its old
                                        # geometry
            M.settle(m, limit=240)
            saved[short] = save('slk', short + ".SLK")

    # ARM A
    data = saved.get("Z2s")
    check(data is not None, "Z2.BIF (BIFF2) opened and saved as SYLK",
          "no Z2.SLK on B:")
    got = F.read_sylk(data) if data else {}
    for key, want in sorted(ARM_A.items()):
        g = got.get(key)
        if isinstance(g, tuple) and g[0] == 'formula':
            g = ('formula', SD.r1c1_to_a1(g[1], key[0], key[1]), g[2])
        check(same(want, g), "BIFF2 %s%d reads as %r"
              % (chr(65 + key[1]), key[0] + 1, want), "SHEET saved %r" % (g,))
    data = saved.get("Z2")
    check(data is not None, "...and saved as Normal", "no new Z2.BIF on B:")
    xf = biff3_xfs(data) if data else {}
    fmt = lambda k: xf.get(k, (-1, False, 0, b'', 0, 1))
    check(fmt((1, 0))[0] == 5, "A2's attribute format 5 is currency",
          "its XF names format %r" % (fmt((1, 0))[0],))
    check(fmt((2, 0))[1], "A3's font index 1 is the BOLD font",
          "its XF's font is not bold")
    check(fmt((3, 0))[2] == 2, "A4 is centred", "alignment %d" % fmt((3, 0))[2])
    b = fmt((3, 0))[3]
    check(len(b) == 4 and b[0] & 7 and b[2] & 7 and not b[1] & 7
          and not b[3] & 7, "A4 has its top and bottom borders, no others",
          "border bytes %s" % b.hex())
    check(fmt((3, 0))[4] != 0, "A4 is shaded", "no pattern in its XF")
    check(fmt((6, 0))[5] == 0 and fmt((0, 0))[5] == 1,
          "A7 is unlocked and A1 locked, as their attribute bytes say",
          "locked bits %d, %d" % (fmt((6, 0))[5], fmt((0, 0))[5]))

    # ARM B
    if not xl:
        print("sheetxl2: ARM B SKIPPED - %s" % why)
    for src, short in (CORPUS if xl else ()):
        want = F.read_biff(xl[short])
        data = saved.get(short)
        check(data is not None, "Excel 2.1d's %s.XLS opened and saved" % src,
              "no %s.SLK on B:" % short)
        got = F.read_sylk(data) if data else {}
        bad = []
        for key, w in want.items():
            g = got.get(key)
            if isinstance(w, tuple) and w[0] == 'formula':
                ok = False
                if w[1] is None:                # refused on the host too:
                    ok = same(w[2], g)          # the value, kept
                elif isinstance(g, tuple) and g[0] == 'formula':
                    ok = (SD.r1c1_to_a1(g[1], key[0], key[1]) == w[1]
                          and same(w[2], g[2]))
            else:
                ok = same(w, g)
            if not ok:
                bad.append((key, w, g))
        extra = [k for k in got if k not in want]
        check(not bad and not extra,
              "%s.XLS: all %d cells as Excel wrote them" % (src, len(want)),
              "%d differ, %d extra - first %r" % (len(bad), len(extra),
                                                  bad[:3] or extra[:3]))
    # ARM C (81.68): Excel's own macro sheet. A formula the host decodes -
    # a macro function or command SHEET has - must come back as that formula;
    # one it cannot must keep Excel's value; nothing is compared by value
    # otherwise, because a macro cell's cached value is whatever Excel last
    # left there and SHEET, outside a run, answers FALSE
    for src, short in (MACROS if xl else ()):
        want = F.read_biff(xl[short])
        data = saved.get(short)
        got = F.read_sylk(data) if data else {}
        bad, nf = [], 0
        for key, w in want.items():
            g = got.get(key)
            if isinstance(w, tuple) and w[0] == 'formula':
                if w[1] is None:
                    ok = same(w[2], g)
                else:
                    nf += 1
                    ok = (isinstance(g, tuple) and g[0] == 'formula'
                          and a1(g[1], key[0], key[1]) == w[1])
            else:
                ok = same(w, g)
            if not ok:
                bad.append((key, w, g))
        check(data is not None and nf >= 20 and not bad,
              "Excel 2.1's macro sheet %s: its %d macro formulas SHEET has come "
              "back as formulas, the rest as Excel's values" % (src, nf),
              "%d differ - first %r" % (len(bad), bad[:3]))
    done("sheetxl2")


if __name__ == "__main__":
    main()
