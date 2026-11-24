#!/usr/bin/env python3
"""cpwdat.py - build CPWIDE.DAT, the wide Control Panel's asset file.

    python3 tools/cpwdat.py [-o build/cpwide.dat] [--fmt lz4|lzb] [--check]

Reads kernel/cpwart/cp-icons-16.png (256x48: 16 cells across, 3 down, the first
CWI16_N of them used, row-major) and cp-icons-32.png (416x32: thirteen 32x32
cells in one row), drivers/catalog.toml, and writes
the file SPEC.md 135.6 / 135.7 describe. Deterministic: same inputs, same
bytes. Every number it uses - offsets, sizes, counts, record layout, category
and icon indices - is PARSED out of kernel/cpwfmt.inc, the file the wide
panel's code assembles with, and the build refuses to write a file that
disagrees with it (the check is `check_layout`).

Pixels: every pixel of a sheet must be one of the sixteen EGA/VGA palette
colours (the index is its position in PALETTE) or the key #FF00FF, which is
transparent. Anything else is an error naming the file, cell and pixel.

--fmt wraps the result in a 'CZ' file (SPEC.md 20.13.5) with tools/os88lz.py,
the way the build packs a data file; the kernel's transparent read expands it.
--check prints the driver FILES drv_tab names that have no catalog entry and
exits non-zero (tests/unit/t_cpwdat.py uses the functions, not this flag).

Stdlib + PIL only.
"""
import argparse
import os
import re
import sys
import tomllib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

FMT_INC = os.path.join(ROOT, "kernel", "cpwfmt.inc")
DRIVER_INC = os.path.join(ROOT, "kernel", "driver.inc")
CATALOG = os.path.join(ROOT, "drivers", "catalog.toml")
SHEET16 = os.path.join(ROOT, "kernel", "cpwart", "cp-icons-16.png")
SHEET32 = os.path.join(ROOT, "kernel", "cpwart", "cp-icons-32.png")

# The sixteen EGA/VGA colours, index = position. This is the VGA default DAC
# set, which is what the sheets were drawn in.
PALETTE = [0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA,
           0xAA5500, 0xAAAAAA, 0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
           0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF]
KEY = 0xFF00FF                      # transparent
NAME_MAX = 25                       # CWD_C_LONG is 26 with its NUL


class CpwError(Exception):
    pass


# ---- cpwfmt.inc --------------------------------------------------------------
def parse_fmt(path=FMT_INC):
    """Every `NAME equ VALUE` of cpwfmt.inc -> {name: int}. VALUE is a decimal
    or hex number or a quoted two-character string ('CW' reads as the word
    whose first byte is 'C')."""
    out = {}
    for ln in open(path, encoding="utf-8"):
        ln = ln.split(";", 1)[0].strip()
        m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)\s+equ\s+(.+)$", ln)
        if not m:
            continue
        name, val = m.group(1), m.group(2).strip()
        if val.startswith("'") and val.endswith("'") and len(val) == 4:
            out[name] = ord(val[1]) | (ord(val[2]) << 8)
        elif re.fullmatch(r"0[xX][0-9a-fA-F]+|[0-9]+", val):
            out[name] = int(val, 0)
        else:
            raise CpwError(f"{path}: cannot read `{name} equ {val}`")
    return out


def check_layout(f):
    """The relations cpwfmt.inc's numbers must keep; a violation means the
    file is wrong, not this tool."""
    def need(cond, msg):
        if not cond:
            raise CpwError("kernel/cpwfmt.inc disagrees with itself: " + msg)
    need(f["CWD_I16_PIX"] == 16 * f["CWD_I16_STR"], "16x16 pixel bytes")
    need(f["CWD_I16_STR"] == 8, "16x16 stride is 8 (16 px at 4bpp)")
    need(f["CWD_I16_SZ"] == f["CWD_I16_PIX"] + 16 * 2, "16x16 icon size")
    need(f["CWD_I32_PIX"] == 32 * f["CWD_I32_STR"], "32x32 pixel bytes")
    need(f["CWD_I32_STR"] == 16, "32x32 stride is 16")
    need(f["CWD_I32_SZ"] == f["CWD_I32_PIX"] + 32 * 4, "32x32 icon size")
    need(f["CWD_HDR"] == f["CWD_H_STR"] + 2, "header is 16 bytes")
    need(f["CWD_C_PAD"] + 20 == f["CWD_CAT_SZ"], "catalog record size")
    need(f["CWD_C_D1"] == f["CWD_C_LONG"] + 26, "long name is 26 bytes")
    need(f["CWD_C_LONG"] == f["CWD_C_FILE"] + 12 + 2, "record fields")


# ---- icon sheets -------------------------------------------------------------
def sheet_grid(count):
    """(columns, rows) of a sheet holding COUNT cells: at most 16 across, the
    last row may be part empty (its spare cells must be the key)."""
    cols = min(count, 16)
    return cols, -(-count // cols)


def _cells(path, cell, count, fmt):
    from PIL import Image
    im = Image.open(path).convert("RGB")
    w, h = im.size
    cols, rows = sheet_grid(count)
    if (w, h) != (cols * cell, rows * cell):
        raise CpwError(f"{os.path.relpath(path, ROOT)}: is {w}x{h}, "
                       f"cpwfmt.inc wants {count} cells of {cell}x{cell}: "
                       f"{cols * cell}x{rows * cell} (wrong cell count)")
    px = im.load()
    rev = {c: i for i, c in enumerate(PALETTE)}
    name = os.path.relpath(path, ROOT)
    cells = []
    for n in range(count):
        rows = []
        for y in range(cell):
            row = []
            for x in range(cell):
                r, g, b = px[(n % cols) * cell + x, (n // cols) * cell + y]
                rgb = (r << 16) | (g << 8) | b
                if rgb == KEY:
                    row.append(None)
                elif rgb in rev:
                    row.append(rev[rgb])
                else:
                    raise CpwError(f"{name}: cell {n} pixel ({x},{y}) is "
                                   f"#{rgb:06X}, not a palette colour or "
                                   f"the #FF00FF key")
            rows.append(row)
        cells.append(rows)
    return cells


def _pack(rows, size, f, stride, pixbytes, maskwords):
    out = bytearray()
    for row in rows:                                  # 4bpp, high nibble left
        for x in range(0, size, 2):
            out.append(((row[x] or 0) << 4) | (row[x + 1] or 0))
    assert len(out) == pixbytes
    for row in rows:                                  # mask, bit 15 leftmost
        for w in range(size // 16):
            v = 0
            for b in range(16):
                if row[w * 16 + b] is not None:
                    v |= 0x8000 >> b
            out += v.to_bytes(2, "little")
    return bytes(out)


def icon_blocks(f):
    n16, n32 = f["CWI16_N"], f["CWI32_N"]
    c16 = _cells(SHEET16, 16, n16, f)
    c32 = _cells(SHEET32, 32, n32, f)
    b16 = b"".join(_pack(c, 16, f, f["CWD_I16_STR"], f["CWD_I16_PIX"], 16)
                   for c in c16)
    b32 = b"".join(_pack(c, 32, f, f["CWD_I32_STR"], f["CWD_I32_PIX"], 32)
                   for c in c32)
    if len(b16) != n16 * f["CWD_I16_SZ"] or len(b32) != n32 * f["CWD_I32_SZ"]:
        raise CpwError("icon block sizes disagree with cpwfmt.inc")
    return n16, b16, n32, b32


# ---- driver catalog ----------------------------------------------------------
def load_catalog(path=CATALOG):
    with open(path, "rb") as fh:
        return tomllib.load(fh)


def validate_catalog(cat, f):
    """-> [(file, catnum, icon, longname, [lines])] in file order, or raise."""
    where = os.path.relpath(CATALOG, ROOT)
    rows = []
    for file, e in cat.items():
        tag = f"{where}: [{file}]"
        if not re.fullmatch(r"[A-Z0-9_]{1,8}\.[A-Z0-9]{1,3}", file):
            raise CpwError(f"{tag}: not an upper-case 8.3 name")
        extra = set(e) - {"category", "icon", "name", "description"}
        if extra:
            raise CpwError(f"{tag}: unknown key(s) {sorted(extra)}")
        for k in ("category", "icon", "name", "description"):
            if k not in e:
                raise CpwError(f"{tag}: missing `{k}`")
        c = e["category"]
        key = "CWC_" + str(c)
        if key not in f or key == "CWC_N":
            raise CpwError(f"{tag}: category {c!r} is not a CWC_* in cpwfmt.inc")
        ic = e["icon"]
        if isinstance(ic, str):
            if "CWI16_" + ic not in f:
                raise CpwError(f"{tag}: icon {ic!r} is not a CWI16_* name")
            ic = f["CWI16_" + ic]
        if not isinstance(ic, int) or not 0 <= ic < f["CWI16_N"]:
            raise CpwError(f"{tag}: icon index {ic!r} outside 0.."
                           f"{f['CWI16_N'] - 1}")
        nm = e["name"]
        if not isinstance(nm, str) or not 1 <= len(nm) <= NAME_MAX \
                or not nm.isascii():
            raise CpwError(f"{tag}: name {nm!r} must be 1..{NAME_MAX} ASCII "
                           f"characters")
        d = e["description"]
        if isinstance(d, str):
            d = [d]
        if not isinstance(d, list) or not 1 <= len(d) <= 2:
            raise CpwError(f"{tag}: description is one or two lines")
        for ln in d:
            if not isinstance(ln, str) or not ln.isascii() \
                    or not 1 <= len(ln) <= f["CWD_DESC_MAX"]:
                raise CpwError(f"{tag}: description line {ln!r} must be "
                               f"1..{f['CWD_DESC_MAX']} ASCII characters")
        rows.append((file, f[key], ic, nm, d))
    return rows


def drv_tab_files(path=DRIVER_INC):
    """The driver FILE names of every drv_tab row of every kernel: the union,
    because one source assembles all of them and ifdefs choose per kernel.
    A row is `dw drv_f_<x>, DRVT_<X>` between `drv_tab:` and the %error that
    closes the table; <x> is a label of `drv_f_<x>: db 'NAME.DRV', 0`, and a
    name that is split across two labels (PCIETHER.DRV's) is joined."""
    src = open(path, encoding="utf-8").read().splitlines()
    labels, order = {}, []
    for i, ln in enumerate(src):
        m = re.match(r"^(drv_f_[a-z0-9]+):\s*db\s+(.*)$", ln)
        if m:
            code = m.group(2).split(";", 1)[0]
            strs = re.findall(r"'([^']*)'", code)
            labels[m.group(1)] = (strs[0] if strs else "",
                                  code.rstrip().endswith("0"))
            order.append(m.group(1))
    names = {}
    for lab in order:
        s, term = labels[lab]
        j = order.index(lab)
        while not term and j + 1 < len(order):     # joined to the next label
            j += 1
            s2, term = labels[order[j]]
            s += s2
        names[lab] = s
    start = next(i for i, ln in enumerate(src) if ln.startswith("drv_tab:"))
    files = []
    for ln in src[start:]:
        if ln.startswith("%if $ - drv_tab"):
            break
        m = re.match(r"^\s*dw\s+(drv_f_[a-z0-9]+)\s*,", ln)
        if m:
            if m.group(1) not in names:
                raise CpwError(f"driver.inc: no label {m.group(1)}")
            files.append(names[m.group(1)])
    if not files:
        raise CpwError("driver.inc: found no drv_tab rows")
    return files


# ---- assembly ----------------------------------------------------------------
def build(f=None, cat=None):
    f = f or parse_fmt()
    check_layout(f)
    n16, b16, n32, b32 = icon_blocks(f)
    rows = validate_catalog(cat if cat is not None else load_catalog(), f)
    ncat = len(rows)
    if ncat > 255:
        raise CpwError("more than 255 catalog entries")
    off16 = f["CWD_HDR"]
    off32 = off16 + len(b16)
    offcat = off32 + len(b32)
    offstr = offcat + ncat * f["CWD_CAT_SZ"]
    strs = bytearray()
    recs = bytearray()
    for file, cnum, ic, nm, lines in rows:
        offs = []
        for ln in lines:
            offs.append(offstr + len(strs))
            strs += ln.encode("ascii") + b"\0"
        rec = bytearray(f["CWD_CAT_SZ"])
        rec[f["CWD_C_FILE"]:f["CWD_C_FILE"] + len(file)] = file.encode("ascii")
        rec[f["CWD_C_CAT"]] = cnum
        rec[f["CWD_C_ICON"]] = ic
        rec[f["CWD_C_LONG"]:f["CWD_C_LONG"] + len(nm)] = nm.encode("ascii")
        rec[f["CWD_C_D1"]:f["CWD_C_D1"] + 2] = offs[0].to_bytes(2, "little")
        d2 = offs[1] if len(offs) > 1 else 0
        rec[f["CWD_C_D2"]:f["CWD_C_D2"] + 2] = d2.to_bytes(2, "little")
        recs += rec
    size = offstr + len(strs)
    if size > 0xFFFF:
        raise CpwError("file longer than a word can count")
    hdr = bytearray(f["CWD_HDR"])
    hdr[f["CWD_H_MAGIC"]:f["CWD_H_MAGIC"] + 2] = f["CWD_MAGIC"].to_bytes(2, "little")
    hdr[f["CWD_H_VER"]] = f["CWD_VERSION"]
    hdr[f["CWD_H_N16"]] = n16
    hdr[f["CWD_H_N32"]] = n32
    hdr[f["CWD_H_NCAT"]] = ncat
    for key, v in (("OFF16", off16), ("OFF32", off32), ("OFFCAT", offcat),
                   ("SIZE", size), ("STR", offstr)):
        o = f["CWD_H_" + key]
        hdr[o:o + 2] = v.to_bytes(2, "little")
    data = bytes(hdr) + b16 + b32 + bytes(recs) + bytes(strs)
    verify(data, f)
    return data


def verify(data, f):
    """Re-read what was built against cpwfmt.inc; refuse a disagreement."""
    w = lambda o: int.from_bytes(data[o:o + 2], "little")
    if w(f["CWD_H_MAGIC"]) != f["CWD_MAGIC"] or data[:2] != b"CW":
        raise CpwError("bad magic")
    if data[f["CWD_H_VER"]] != f["CWD_VERSION"]:
        raise CpwError("bad version")
    if data[f["CWD_H_N16"]] != f["CWI16_N"] or data[f["CWD_H_N32"]] != f["CWI32_N"]:
        raise CpwError("icon counts disagree with cpwfmt.inc (wrong cell count)")
    if w(f["CWD_H_SIZE"]) != len(data):
        raise CpwError("size field disagrees with the file")
    o16, o32, oc, os_ = (w(f["CWD_H_OFF16"]), w(f["CWD_H_OFF32"]),
                         w(f["CWD_H_OFFCAT"]), w(f["CWD_H_STR"]))
    if o32 - o16 != f["CWI16_N"] * f["CWD_I16_SZ"] \
            or oc - o32 != f["CWI32_N"] * f["CWD_I32_SZ"] \
            or os_ - oc != data[f["CWD_H_NCAT"]] * f["CWD_CAT_SZ"]:
        raise CpwError("block sizes disagree with cpwfmt.inc")
    for i in range(data[f["CWD_H_NCAT"]]):
        r = oc + i * f["CWD_CAT_SZ"]
        d1 = w(r + f["CWD_C_D1"])
        if not os_ <= d1 < len(data):
            raise CpwError("description offset outside the string block")


def wrap(data, fmt):
    import os88lz
    z, packed = os88lz.cz_wrap(data, {"lz4": os88lz.LZ4, "lzb": os88lz.LZB}[fmt])
    return z


def unwrap(blob):
    """The raw file back from a (possibly) 'CZ'-wrapped one."""
    if blob[:2] == b"CZ":
        import os88lz
        return os88lz.cz_unwrap(blob)
    return blob


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("-o", "--output", default=os.path.join(ROOT, "build",
                                                           "cpwide.dat"))
    ap.add_argument("--fmt", choices=("lz4", "lzb"), default="",
                    help="wrap the file as 'CZ' (empty: plain)")
    ap.add_argument("--check", action="store_true",
                    help="list drv_tab files without a catalog entry")
    a = ap.parse_args()
    try:
        if a.check:
            miss = [x for x in drv_tab_files() if x not in load_catalog()]
            for m in miss:
                print(f"no catalog entry: {m}")
            return 1 if miss else 0
        data = build()
        out = wrap(data, a.fmt) if a.fmt else data
    except (CpwError, OSError) as e:
        print(f"cpwdat: {e}", file=sys.stderr)
        return 1
    tmp = a.output + ".tmp"
    with open(tmp, "wb") as fh:
        fh.write(out)
    os.replace(tmp, a.output)
    print(f"cpwdat: {a.output}: {len(data)} bytes"
          + (f", {len(out)} packed" if a.fmt else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
