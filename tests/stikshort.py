#!/usr/bin/env python3
"""SPEC.md 26.8.1 / 110.2: a DRAG-MADE shortcut is not a Sticky App row.

    make && make stiktest && python3 tests/stikshort.py

THE REGRESSION FOR +3. SPEC.md 26.8.1 called the record's byte at +3
"spare", and every shortcut a user has ever dragged out of a Disk window
carries `\\` there - sc_m_new writes the path one byte early so dsk_path's
leading separator lands on it. A Sticky App mark tested as "+3 non-zero" would have
hidden every one of them at the next boot. The mark is an EQUALITY with
'D', and this is the disk that holds it to that.

`make stiktest`'s stikshort360.img: three shortcuts written as a drag writes
them (`\\` at +3) - a package, a folder and a document, PLACED in cells 3, 4
and 5 - and no Sticky App row. One boot on a VGA XT:

  A  each of the three is SHOWN, in its own cell: [desk_zslot] is
     DSL_PIN | cell with no DSL_GONE, and the cell's pixels are not the bare
     desktop's (a free cell beside them is the control);
  B  each row still carries `\\` at +3 - nothing rewrote it;
  C  nothing was launched, and nothing was "not found".

**BREAK IT ON PURPOSE** (docs/WRITING-TESTS.md 1): make desk_live's test
`cmp byte [es:bx+SC_R_STIK], 0 / jne` (non-zero, not 'D') and A fails three
times; make stik_boot test the mark alone and not KIND too, and nothing here
changes - which is why stikboot's fixture is the KIND gate.
"""
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88ui      # noqa: E402
import os88geom as geom   # noqa: E402
import os88stik    # noqa: E402

ROOT = os.path.join(HERE, "..")
BDIR = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
DESK_SC0 = os88stik.DESK_SC0
SHORTS = ((3, "TASKMGR"), (4, "SYSTEM"), (5, "README.TXT"))
FREE_CELL = 6

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def solid(ui, cell):
    """The share of a cell's horizontal pixel PAIRS that are the same colour.
    The bare desktop is SPEC.md 63's 50% dither, a checkerboard, so every
    pair differs; an item's white mask, black outline and caption rect are
    runs. A cell that is drawn reads well above a free one."""
    rows = ui._word("desk_rows")
    col, r = divmod(cell, rows)
    x0 = ui._word("vid_desk_zx") - col * geom.DESK_PX
    y0 = 32 + r * ui._word("desk_zstep")
    w, h, data = ui.m.fbuf()
    n = same = 0
    for y in range(y0, min(h, y0 + ui._word("desk_zh1"))):
        for x in range(x0, min(w - 1, x0 + geom.DESK_CW - 1)):
            p = (y * w + x) * 3
            n += 1
            same += data[p:p + 3] == data[p + 3:p + 6]
    return same / float(n or 1)


def main():
    tmp = tempfile.mkdtemp(prefix="stikshort-")
    sysimg = os.path.join(tmp, "stikshort360.img")
    shutil.copy(os.path.join(BDIR, "stikshort360.img"), sysimg)
    with os88ui.boot(sysimg, machine="os8088_xt_vga") as ui:
        m = ui.m
        seg = ui._word("sc_seg")
        bare = solid(ui, FREE_CELL)
        print("A/B: each shortcut shown in its cell, its +3 untouched")
        for k, (cell, cap) in enumerate(SHORTS):
            zb = m.read(ui._S("desk_zslot") + DESK_SC0 + k, 1)[0]
            check(zb == os88stik.DSL_PIN | cell,
                  "%s is SHOWN in cell %d (%#x)" % (cap, cell, zb))
            r = os88stik.parse_row(m.read((seg << 4) + k * os88stik.SC_REC,
                                         os88stik.SC_REC))
            check(r["mark"] == 0x5C and r["cap"] == cap,
                  "%s's row keeps `\\` at +3 (%#x)" % (cap, r["mark"]))
            sf = solid(ui, cell)
            check(sf > bare + 0.10, "%s's cell is drawn: %.0f%% of its pixel "
                  "pairs solid against the bare cell's %.0f%%"
                  % (cap, 100 * sf, 100 * bare))
        print("C: nothing launched, nothing not found")
        check(ui.titles() == [], "no window (%r)" % ui.titles())
        txt, _ = ui.toast()
        check("not found" not in txt.lower(), "no toast (%r)" % txt)
    shutil.rmtree(tmp, ignore_errors=True)
    print("stikshort: %s" % ("PASS" if not fails else
                            "FAIL (%d): %s" % (len(fails), "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
