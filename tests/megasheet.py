#!/usr/bin/env python3
"""megasheet - SHEET above 1MB under kern_mega's protected mode: a document
loaded, a CHART drawn and a MACRO run (docs/plans/completed/KERN-MEGA-MEMORY-PLAN.md
MC7, SPEC.md 81 and 82.16).

    make && make mega && python3 tests/megasheet.py [--keep]

Under kern_mega every claim and every region is an LDT selector PAIR: the
value the kernel hands back is the DATA selector, and value - 8 is a CODE
selector over the same bytes. SHEET far-calls code it put in two claims -
the chart module (apps/os88chartovl.inc's ch_ovneed, `call far [ch_ovfar]`)
and the macro module (sheet.asm's sh_m2need, `call far [sh_m2far]`), both
parts of SHEET.O88 - and the modules far-call back into the region through
vector tables stamped with the region's own CS (ch_ovbind, sh_ovbind), with
ch_xthunk telling the two callers apart by the CS they pushed. A far call
or a store through the wrong half often still WORKS - kern_mega completes
the #GP (mega_gpfix, mega_gpw), a fault apiece - so "it ran" is not the
whole test: the row boots MEGATRAP=1's kernel (a private tree,
tools/os88build.py), whose census line per completed #GP it reads.

Boots that kernel under Bochs at 16MB with SHEET.O88, a SYLK document and a
text file in C:\\DOCS, opens the document by double-clicking it, and:

  0. SHEET's region is above 1MB, and its five data claims too - it
     declares OS88_HIGH and OS88_HIGHMEM since this row first ran it so,
     which is what retired its two lines in tests/megahigh.txt;
  1. Data > Chart Column... on column C: the chart module draws the five
     points ([sh_msg] = Charted), and both far vectors into a module are
     the CODE half of its pair (a code descriptor, the data selector - 8),
     the macro module's channel door [sh_m2dseg] the DATA half;
  2. Macro > Run... COUNT, in the macro module: FOR/NEXT sums 1..5 into H2
     through its callbacks into the region, FOPEN/FREADLN read SRC.TXT's
     first line through a text channel in the module's own claim,
     FWRITELN/FCLOSE write it to OUT.TXT (read back off C: afterwards), and
     IF(H2=15,MESSAGE(TRUE,H4)) puts it on the status bar;
  3. no `kern_mega CLOSED` (a package's #GP, completed by closing it) or
     `kern_mega FAULT` line anywhere;
  4. the census from SHEET's start: NO far transfer completed by
     mega_gpfix, and no store with the macro module's CODE selector in DS or
     ES. What it does count (~164 in this script) is the two modules' own
     `mov [cs:x], ...` stores, which kern_mega completes one by one - a
     COST, listed for the speed pass (KERN-MEGA-MEMORY-PLAN item 11), and
     printed by CS.

A screenshot of each stage lands in the boot's directory (--keep keeps it).

Broken on purpose (docs/WRITING-TESTS.md 1), each seen red when the row was
written: ch_ovneed's vector built from op_seg (the data half) in place of
op_cseg, and ch_xthunk's compare put back to the module's DATA selector
(`[cs:ch_ovseg]`, the defect batch 1 fixed), each stop the machine at the
document's read - `kern_mega FAULT vector 0D`, ch_xthunk no longer knowing
its caller - failing 2 and 3; the four `mov es, [sh_m2dseg]` in the macro
module's channels put back to `push cs / pop es` WORK (3 completed #GPs,
one of them in the kernel's file read) and fail 4 alone; the parent
commit's SHEET.O88, which declared neither bit, fails 0 (region at 090000).
"""
import os
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [os.path.join(ROOT, "tools"), HERE]
os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "megak")
os.environ["OS88_DEFINES"] = "KERN_MEGA"
import os88bochs  # noqa: E402
import os88sheetfmt as F  # noqa: E402
from os88fixture import need  # noqa: E402
from sheetmove import pkg_syms  # noqa: E402
from megapaint import fat_file  # noqa: E402
import megatraps  # noqa: E402
import os88build  # noqa: E402

FAILS = []
NAME = "MAC.SLK"
COUNT = ['FOR(H1,1,5)', 'SET.VALUE(H2,H2+H1)', 'NEXT()',
         'SET.VALUE(H3,FOPEN("SRC.TXT",2))', 'SET.VALUE(H4,FREADLN(H3))',
         'FCLOSE(H3)', 'SET.VALUE(H5,FOPEN("OUT.TXT",3))',
         'FWRITELN(H5,H4)', 'FCLOSE(H5)',
         'IF(H2=15,MESSAGE(TRUE,H4))', 'RETURN()']
IN = b"alpha\r\nbeta\r\n"
SH_MBAR_H, SH_MPAD, SH_MI_H = 14, 8, 12     # sheet.asm's menu geometry


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)
    return cond


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def fixture(path):
    """COUNT in column A (named, as Macro > Run... asks for it), and a
    column of numbers in C for the chart."""
    cells = {(r, 0): ('formula', f, 0.0) for r, f in enumerate(COUNT)}
    cells.update({(r, 2): float(v) for r, v in enumerate((3, 1, 4, 1, 5))})
    body = F.write_sylk(cells).decode('latin-1').split('\r\n')
    body[1:1] = ['NN;NCOUNT;ER1C1']
    with open(path, "wb") as f:
        f.write('\r\n'.join(body).encode('latin-1'))


class Sheet(object):
    """SHEET's state, read through its region's descriptor."""

    def __init__(self, box):
        self.box = box
        self.s = pkg_syms(os.path.join(ROOT, "apps", "sheet", "sheet.asm"))

    def base(self):
        return self.box.pkg_base("SHEET")

    def raw(self, label, n):
        return self.box.read(self.base() + self.s[label], n)

    def word(self, label):
        return u16(self.raw(label, 2))

    def words(self, labels):
        b = self.base()
        got = self.box.reads([(b + self.s[l], 2) for l in labels])
        return dict((l, u16(g)) for l, g in zip(labels, got))

    def cstr(self, off, n=40):
        return self.box.read(self.base() + off, n).split(b"\0")[0].decode(
            "latin-1")


def menu_pick(box, sh, menu, label):
    """SHEET's own in-window bar (sh_mtrack): press on title MENU, drag to
    the item whose string pointer is at LABEL (sh_i_*.<item>), release -
    every position off the package's own geometry words, the open menu and
    the highlighted row confirmed before the release."""
    w = sh.words(["sh_ox", "sh_oy"])
    mw = sh.raw("sh_mw", 2 * (menu + 1))
    x = w["sh_ox"] + sum(u16(mw, 2 * j) + 2 * SH_MPAD for j in range(menu))
    x += (u16(mw, 2 * menu) + 2 * SH_MPAD) // 2
    box.goto(x, w["sh_oy"] + SH_MBAR_H // 2)
    for _ in range(3):          # a press on a window that is not on top
        box.xd("mousedown", 1)  # only raises it (the chart window opens
        try:                    # over SHEET's): press again
            box.until(lambda: sh.raw("sh_mopen", 1)[0] == menu,
                      "menu %d open" % menu, 5)
            break
        except RuntimeError:
            box.xd("mouseup", 1)
            box.ui_done("the raise", secs=30)
    else:
        raise RuntimeError("SHEET's menu %d never opened" % menu)
    menus = {4: "sh_i_data", 6: "sh_i_macro"}[menu]
    k = (sh.s[label] - sh.s[menus]) // 2
    g = sh.words(["sh_mrx1", "sh_mry1"])
    box.goto(g["sh_mrx1"] + 20, g["sh_mry1"] + 2 + k * SH_MI_H + SH_MI_H // 2)
    box.until(lambda: sh.raw("sh_mhi", 1)[0] == k, "item %d lit" % k, 30)
    box.xd("mouseup", 1)


def code_desc(box, sel):
    """True when SEL's descriptor is an executable (code) segment."""
    d = box.read(box.ldt_base() + (sel & ~7), 8)
    return bool(d[5] & 0x08) and bool(d[5] & 0x10)


def main():
    keep = "--keep" in sys.argv
    need("build/sheet.o88")
    t = os88build.tree("MEGATRAP=1", targets=("mega.img", "megarun.o88",
                                              "mbr.bin"))
    out = tempfile.mkdtemp(prefix="megasheet-")
    slk = os.path.join(out, NAME)
    fixture(slk)
    inp = os.path.join(out, "SRC.TXT")
    with open(inp, "wb") as f:
        f.write(IN)
    disp = os.environ.get("OS88_BOCHS_DISPLAY") or os88bochs.take_display()
    box = os88bochs.Box(out, [], megs=16, display=disp, build=t.dir,
                        put=("DOCS:" + slk, "DOCS:" + inp,
                             "DOCS:" + os.path.join(ROOT, "build",
                                                    "sheet.o88")))
    t0 = time.time()
    try:
        box.up()
        box.capture()
        box.dbl(591, 160, "C:'s Disk window")
        box.dbl(150, 123, "DOCS's listing")
        box.goto(160, 139)                  # MAC.SLK, the first row
        box.dblclick()
        box.until(lambda: box.pkg_base("SHEET"), "SHEET started", 120)
        mark = len(box.lines())     # the trap census from here on
        rbase = box.pkg_base("SHEET")
        print("megasheet: SHEET up after %.0fs, its region at %06X"
              % (time.time() - t0, rbase), flush=True)
        want(rbase >= 0x100000, "0. the region above 1MB (it declares "
             "OS88_HIGH, tests/megahigh.txt)")
        sh = Sheet(box)
        box.until(lambda: sh.word("sh_ncells") >= 10, "the document read",
                  180)
        box.ui_done("SHEET's first paint", secs=180)
        print("megasheet: the document read, %d cells"
              % sh.word("sh_ncells"), flush=True)
        box.shot(os.path.join(out, "0-opened.png"))
        segs = sh.words(["sh_cellseg", "sh_txtseg", "sh_stgseg",
                         "sh_bordseg", "sh_chartseg"])
        hi = dict((k, box.sel_base(v)) for k, v in segs.items() if v & 4)
        print("megasheet: claims %s" % ", ".join(
            "%s %04X at %06X" % (k[3:], segs[k], hi.get(k, 0)) for k in segs),
            flush=True)
        want(len(hi) == len(segs) and all(b >= 0x100000 for b in
                                           hi.values()),
             "0. its claims above 1MB too (it declares OS88_HIGHMEM)")

        # --- 1. the chart: Data > Chart Column... on column C ---------------
        box.key("Right", "Right")
        box.until(lambda: sh.word("sh_selcol") == 2, "C1 selected", 30)
        menu_pick(box, sh, 4, "sh_i_data.chart")
        box.until(lambda: sh.word("sh_chartwin"), "the chart window", 120)
        box.ui_done("the chart drawn", secs=180)
        box.shot(os.path.join(out, "1-chart.png"))
        w = sh.words(["sh_msg", "ch_ovseg", "ch_ovfar", "sh_m2far",
                      "sh_m2dseg", "sh_chart_cnt"])
        cvec = u16(sh.raw("ch_ovfar", 4), 2)
        mvec = u16(sh.raw("sh_m2far", 4), 2)
        print("megasheet: chart module data %04X, vector %04X:%04X; macro "
              "module data %04X, vector %04X:%04X; %d points" % (
                  w["ch_ovseg"], w["ch_ovfar"], cvec, w["sh_m2dseg"],
                  w["sh_m2far"], mvec, w["sh_chart_cnt"]), flush=True)
        want(w["sh_msg"] == sh.s["sh_s_charted"] and w["sh_chart_cnt"] == 5,
             "1. Data > Chart Column... drew column C's five points through "
             "the chart module ([sh_msg] = Charted)")
        want(w["ch_ovseg"] & 4 and cvec == w["ch_ovseg"] - 8 and
             code_desc(box, cvec) and w["ch_ovfar"] == 0,
             "1. the chart module's far vector is its pair's CODE half "
             "(%04X, data %04X)" % (cvec, w["ch_ovseg"]))
        want(w["sh_m2dseg"] & 4 and mvec == w["sh_m2dseg"] - 8 and
             code_desc(box, mvec) and not code_desc(box, w["sh_m2dseg"]),
             "1. the macro module's far vector is its CODE half (%04X) and "
             "its channels' ES the DATA half (%04X)" % (mvec, w["sh_m2dseg"]))

        # --- 2. the macro: Macro > Run... COUNT ---------------------------
        box.key("ctrl+Home")
        menu_pick(box, sh, 6, "sh_i_macro.run")
        box.ui_done("the Run dialog", secs=60)
        box.shot(os.path.join(out, "2-run.png"))
        box.key(*(["BackSpace"] * 8))
        box.type("COUNT")
        box.key("Return")
        box.until(lambda: sh.word("sh_msg") == sh.s["sh_macro_stmsg"],
                  "the macro's MESSAGE", 120)
        box.ui_done("the macro run", secs=120)
        box.shot(os.path.join(out, "2-macro.png"))
        msg = sh.cstr(sh.s["sh_macro_stmsg"])
        want(msg == "alpha", "2. Macro > Run... COUNT: FOR/NEXT summed "
             "1..5 to 15, FOPEN/FREADLN read SRC.TXT's first line through a "
             "channel in the macro module's claim, and MESSAGE says %r" % msg)

        # --- 4. the trap census (MEGATRAP=1) -------------------------------
        traps = [m.groups() for m in (megatraps.TLINE.match(l) for l in
                                      box.lines()[mark:]) if m]
        xfer = [x for x in traps if int(x[0], 16)]
        esc = [x for x in traps if int(x[4], 16) == mvec or
               int(x[3], 16) == mvec]
        print("megasheet: %d #GPs completed while SHEET ran, %d far "
              "transfers, %d with the macro module's CODE selector in DS/ES"
              % (len(traps), len(xfer), len(esc)), flush=True)
        bycs = {}
        for x in traps:
            bycs[x[1]] = bycs.get(x[1], 0) + 1
        print("megasheet: by CS (the modules' stores through CS - a cost, "
              "not a defect): %s" % ", ".join(
                  "%s %d" % kv for kv in sorted(bycs.items())), flush=True)
        want(not xfer, "4. no far transfer completed by mega_gpfix - every "
             "vector into a module and back is a code selector%s"
             % "".join(" | T " + " ".join(x) for x in xfer[:3]))
        want(not esc, "4. no store through the macro module's CODE half in "
             "DS or ES (its channels' file calls take [sh_m2dseg])%s"
             % "".join(" | T " + " ".join(x) for x in esc[:3]))
    except Exception as e:      # noqa: BLE001 - the machine stopped
        want(False, "the run stopped: %s" % e)
        try:
            box.shot(os.path.join(out, "stopped.png"))
        except Exception:       # noqa: BLE001
            pass
    finally:
        bad = box.faults()
        want(not bad, "3. no kern_mega FAULT or CLOSED%s"
             % "".join(" | " + l for l in bad[:3]))
        box.stop()
        try:
            got = fat_file(os.path.join(out, "c.img"), "DOCS/OUT.TXT")
        except RuntimeError as e:
            got = str(e).encode()
        want(got == b"alpha\r\n", "2. FWRITELN/FCLOSE wrote OUT.TXT from "
             "the channel: %r" % got)
        print("megasheet: %.0fs under Bochs" % (time.time() - t0))
        if keep or FAILS:
            print("megasheet: the boot is in " + out)
        else:
            shutil.rmtree(out, ignore_errors=True)
    for f in FAILS:
        print("FAIL megasheet: " + f)
    print("megasheet: %s" % ("FAILED" if FAILS else "PASS"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
