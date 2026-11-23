#!/usr/bin/env python3
"""CPWIDE.DAT, the wide Control Panel's assets, and the driver catalog behind it.

    python3 tests/unit/t_cpwdat.py

SPEC.md 135.6 / 135.7. Four things go wrong here without any compiler noticing:

  * A DRIVER IS ADDED and nobody writes its catalog entry: the wide Drivers
    page then shows it with the Unknown icon and no description (it still
    works, which is why nothing else fails), or an entry is left behind for a
    driver that was renamed or removed.
  * A LINE OUTGROWS ITS FIELD. The page draws a long name in a 25-character
    column and a description line in 46; past either the draw is clipped or the
    record format (cpwfmt.inc's 26-byte name) is overrun.
  * AN ICON SHEET IS EDITED with a colour that is not in the sixteen-colour
    palette (an anti-aliased brush, a re-exported PNG): the converter would
    have to guess an index, so it refuses - and this row refuses first, naming
    the cell and pixel.
  * build/cpwide.dat IS STALE - the tool, a sheet, the catalog or cpwfmt.inc
    changed and the file on the disks did not.

The drv_tab side is read from kernel/driver.inc as the UNION of every kernel's
rows (one source, ifdefs per kernel): a row on any shipped kernel needs an
entry. The file is then read back by a SECOND reader written here, from
cpwfmt.inc's numbers, and compared with the sheets pixel by pixel and with the
catalog record by record - so a converter that agreed with itself and disagreed
with the format fails.
"""
import os
import sys
import tomllib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, eq, done                        # noqa: E402
import cpwdat                                              # noqa: E402

BUILD = os.environ.get("OS88_BUILD") or os.path.join(ROOT, "build")


def sheet_problems(path, cell):
    """Every pixel that is not a palette colour or the key, by cell."""
    from PIL import Image
    im = Image.open(path).convert("RGB")
    w, h = im.size
    ok = set(cpwdat.PALETTE) | {cpwdat.KEY}
    bad = []
    px = im.load()
    cols = w // cell
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            if ((r << 16) | (g << 8) | b) not in ok:
                n = (y // cell) * cols + x // cell
                bad.append("cell %d pixel (%d,%d) #%02X%02X%02X"
                           % (n, x % cell, y % cell, r, g, b))
    return (w, h), bad


def main():
    f = cpwdat.parse_fmt()
    why_fmt = ("kernel/cpwfmt.inc is what CPWIDE.DRV assembles with; "
               "tools/cpwdat.py must write what it says")

    # --- the catalog against drv_tab ----------------------------------------
    with open(cpwdat.CATALOG, "rb") as fh:
        cat = tomllib.load(fh)
    rows = cpwdat.drv_tab_files()
    check(len(rows) >= 13, "drv_tab scrape found %d rows (>= 13)" % len(rows),
          "kernel/driver.inc's table layout changed under the scraper")
    for r in rows:
        check(r in cat, "drv_tab row %s has an entry in drivers/catalog.toml" % r,
              "SPEC.md 135.7: without one the wide Drivers page shows it with "
              "the Unknown icon and no description. Add a table named %s; the "
              "recipe is the comment at the top of the file" % r)
    for k in cat:
        check(k in rows, "catalog entry [%s] names a drv_tab row" % k,
              "a renamed or removed driver leaves its entry behind; delete it "
              "or fix the name to the file kernel/driver.inc's drv_f_* label "
              "spells")

    # --- the limits, read from the raw TOML (not through the converter) -----
    for k, e in cat.items():
        nm = e.get("name", "")
        check(isinstance(nm, str) and 1 <= len(nm) <= 25,
              "[%s] long name %r is 1..25 characters (%d)" % (k, nm, len(nm)),
              "CWD_C_LONG is 26 bytes with its NUL and the table column is "
              "25 characters wide")
        d = e.get("description", [])
        d = [d] if isinstance(d, str) else d
        check(1 <= len(d) <= 2, "[%s] description is one or two lines" % k,
              "a catalog record has two line offsets, CWD_C_D1 and CWD_C_D2")
        for ln in d:
            check(isinstance(ln, str) and 1 <= len(ln) <= f["CWD_DESC_MAX"],
                  "[%s] description line %r is 1..%d characters (%d)"
                  % (k, ln, f["CWD_DESC_MAX"], len(ln)),
                  "CWD_DESC_MAX: the details box clips at that column")
        ic = e.get("icon")
        if isinstance(ic, str):
            ic = f.get("CWI16_" + ic)
        check(isinstance(ic, int) and 0 <= ic < f["CWI16_N"],
              "[%s] icon %r is a cell of the 16x16 sheet (0..%d)"
              % (k, e.get("icon"), f["CWI16_N"] - 1),
              "an index past CWI16_N reads the next record's bytes as a bitmap")
        check(("CWC_" + str(e.get("category"))) in f
              and e.get("category") != "N",
              "[%s] category %r is a CWC_* of cpwfmt.inc" % (k, e.get("category")))

    # --- the sheets ----------------------------------------------------------
    for path, cell, n in ((cpwdat.SHEET16, 16, f["CWI16_N"]),
                          (cpwdat.SHEET32, 32, f["CWI32_N"])):
        (w, h), bad = sheet_problems(path, cell)
        rel = os.path.relpath(path, ROOT)
        eq((w // cell) * (h // cell), n, "%s has %d cells of %dx%d"
           % (rel, n, cell, cell), why_fmt)
        check(not bad, "%s uses only palette colours and the key (%d bad)"
              % (rel, len(bad)),
              "the converter maps a pixel to a palette index by EXACT match; "
              "first offenders: " + "; ".join(bad[:3]))

    # --- the built file ------------------------------------------------------
    try:
        fresh = cpwdat.build(f, None)
    except cpwdat.CpwError as e:
        check(False, "cpwdat.build() succeeds: %s" % e, why_fmt)
        done("t_cpwdat")
    check(len(fresh) <= 16384, "CPWIDE.DAT is %d bytes (<= 16,384 unpacked)"
          % len(fresh), "the wide panel reads it into one heap claim (135.6)")

    have = os.path.join(BUILD, "cpwide.dat")
    if not check(os.path.exists(have), "%s exists" % have,
                 "make builds it and puts it on every big/emu/mega system disk"):
        done("t_cpwdat")
    blob = open(have, "rb").read()
    raw = cpwdat.unwrap(blob)
    check(raw == fresh, "build/cpwide.dat is what tools/cpwdat.py writes now",
          "a sheet, the catalog, cpwfmt.inc or the tool changed and the file "
          "did not: run make", got=raw, want=fresh)
    if blob[:2] == b"CZ":
        eq(int.from_bytes(blob[4:8], "little"), len(fresh),
           "the 'CZ' header's unpacked size is the file's length",
           "the directory hint sizes the claim from it (SPEC.md 20.13.5)")

    # --- a second reader: the file against the sheets and the catalog -------
    w = lambda o: int.from_bytes(fresh[o:o + 2], "little")
    eq(fresh[:2], b"CW", "magic", why_fmt)
    eq(fresh[f["CWD_H_VER"]], f["CWD_VERSION"], "version", why_fmt)
    eq(w(f["CWD_H_SIZE"]), len(fresh), "size field", why_fmt)
    from PIL import Image
    for tag, path, cell, n, off, sz, stride, pixb in (
            ("16x16", cpwdat.SHEET16, 16, f["CWI16_N"], w(f["CWD_H_OFF16"]),
             f["CWD_I16_SZ"], f["CWD_I16_STR"], f["CWD_I16_PIX"]),
            ("32x32", cpwdat.SHEET32, 32, f["CWI32_N"], w(f["CWD_H_OFF32"]),
             f["CWD_I32_SZ"], f["CWD_I32_STR"], f["CWD_I32_PIX"])):
        im = Image.open(path).convert("RGB")
        px = im.load()
        cols = im.size[0] // cell
        wrong = 0
        for i in range(n):
            base = off + i * sz
            for y in range(cell):
                mrow = fresh[base + pixb + y * (cell // 8):
                             base + pixb + (y + 1) * (cell // 8)]
                mword = [int.from_bytes(mrow[j:j + 2], "little")
                         for j in range(0, len(mrow), 2)]
                for x in range(cell):
                    r, g, b = px[(i % cols) * cell + x, (i // cols) * cell + y]
                    rgb = (r << 16) | (g << 8) | b
                    byte = fresh[base + y * stride + x // 2]
                    nib = byte >> 4 if x % 2 == 0 else byte & 15
                    opaque = (mword[x // 16] >> (15 - x % 16)) & 1
                    if rgb == cpwdat.KEY:
                        okp = (not opaque) and nib == 0
                    else:
                        okp = opaque and cpwdat.PALETTE[nib] == rgb
                    wrong += not okp
        eq(wrong, 0, "%s icons read back equal the sheet (%d wrong pixels)"
           % (tag, wrong), why_fmt)
    ncat = fresh[f["CWD_H_NCAT"]]
    eq(ncat, len(cat), "catalog count is the catalog's table count", why_fmt)
    oc = w(f["CWD_H_OFFCAT"])
    cstr = lambda b: b.split(b"\0", 1)[0].decode("ascii")
    for i, (file, e) in enumerate(cat.items()):
        r = fresh[oc + i * f["CWD_CAT_SZ"]:oc + (i + 1) * f["CWD_CAT_SZ"]]
        eq(cstr(r[f["CWD_C_FILE"]:f["CWD_C_FILE"] + 12]), file,
           "record %d file" % i, why_fmt)
        eq(cstr(r[f["CWD_C_LONG"]:f["CWD_C_LONG"] + 26]), e["name"],
           "record %d (%s) long name" % (i, file), why_fmt)
        d = e["description"]
        d = [d] if isinstance(d, str) else d
        d1 = int.from_bytes(r[f["CWD_C_D1"]:f["CWD_C_D1"] + 2], "little")
        d2 = int.from_bytes(r[f["CWD_C_D2"]:f["CWD_C_D2"] + 2], "little")
        eq(cstr(fresh[d1:]), d[0], "record %d line 1" % i, why_fmt)
        eq(cstr(fresh[d2:]) if d2 else None, d[1] if len(d) > 1 else None,
           "record %d line 2" % i, why_fmt)
        eq(r[f["CWD_C_PAD"]:], bytes(20), "record %d pad is zero" % i, why_fmt)

    print("t_cpwdat: %d drv_tab rows, %d catalog entries, %d bytes (%d on disk)"
          % (len(rows), len(cat), len(fresh), len(blob)))
    done("t_cpwdat")


if __name__ == "__main__":
    main()
