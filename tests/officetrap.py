#!/usr/bin/env python3
"""officetrap - SHEET, WORD and SCRIBE in real use under kern_mega, and not one
store through CS charged to them (docs/plans/KERN-MEGA-PLAN.md 14.162).

    make && make mega && make worddisk scribedisk
    python3 tests/officetrap.py --leg sheet [--sheet PKG]
    python3 tests/officetrap.py --leg word [--word PKG] [--scribe PKG]

kern_mega COMPLETES a store through a code selector (mega_gpw) instead of
faulting on it - right, and ~884 clocks on a 286 each, so a package that
does one on a per-step or per-paragraph path is slow while looking correct.
86Box BY NAME, the at286_vga_sb port (tools/os88mega86box.py): its fault log
prints every exception with its CS:IP, and that is the instrument here. Both
legs read the log from the first open on and charge NOTHING to a loaded
image - no exception at a CS that is not the kernel's (0060, 0028, `.mega`'s
MEGA_SEL_CODE) or the BIOS's. (The kernel's own api_x.done #GP at 0060:0525
after a file call is the kernel's, KERN-MEGA-PLAN's census, not this row's.)

SHEET (kern_mega's ARM, build/megak/apps/sheet.o88 - sheet.asm -DPKG_MEGA,
whose two modules store into their claims through each claim's DATA
selector, CSDO):
  1. MAC.SLK (the host writes it) opens; Macro > Run LOOP, a 100-turn
     FOR(H1,1,100) / SET.VALUE(H2,H2+H1) / NEXT();
  2. File > Save As, Normal (BIFF): the macro's answer is in the file - H2 =
     5050, read by the host (tools/os88sheetfmt.py);
  3. the sheet closed and MAC.BIF opened again - the BIFF decoder's formula
     path (sh_dc_*, the chart module) - and its title says so;
  4. no exception charged to a loaded image from the first open to here.
WORD and SCRIBE (the shipped packages, which kern_mega's disks carry as
they are - their conversions are shared source):
  1. Word opens WELCOME.DOC and saves it; Utilities > Sort and > Renumber
     over the whole document (toasts 'Sorted', 'Renumbered');
  2. Scribe opens SCWELCOM.DOC and saves it, opens SCWELCOM.RTF and saves
     it, then sorts and renumbers;
  3. the two .DOCs read back on the host through tools/wordfmt.py and say
     exactly what the originals say (os88doc markup), and the .RTF has the
     original's paragraph count;
  4. no exception charged to a loaded image.

THE FAILING CASE (docs/WRITING-TESTS.md 1): `--sheet build/sheet.o88` (the
SHIPPED Sheet, its modules storing through CS) fails SHEET 4 with 1,024
#GP at the macro module's selector for the run (shm_mstep .exec, shm_mesc,
shm_pmacro .act, five a step) and 75 more at the chart module's for the
BIFF load (sh_biff_dcrpn, sh_dc_index, three a formula); `--word`/
`--scribe` with the packages of before 14.162 fail WORD 4 - 29 at Word's
load (wd_dpapin's `push cs / pop ds`, a #GP a mark in wd_dfkfind), 28 a
Sort and a Renumber (wd_plist, one a paragraph), 51 at its save (wd_dattr,
wd_dpara) and Scribe's like them plus 36 at its RTF save (sc_rpap). Seen
when the row was written.

It reads build/ and runs the machine in a run directory of its own.
"""
import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
os.environ["OS88_MEGA"] = "1"
os.environ["OS88_MEGA_EMU"] = "86box"
sys.path[:0] = [os.path.join(ROOT, "tools"), HERE]

LOOP = ['FOR(H1,1,100)', 'SET.VALUE(H2,H2+H1)', 'NEXT()', 'RETURN()']


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--leg", choices=("sheet", "word"), required=True)
    ap.add_argument("--sheet", default="build/megak/apps/sheet.o88")
    ap.add_argument("--word", default="build/word.o88")
    ap.add_argument("--scribe", default="build/scribe.o88")
    a = ap.parse_args()
    os.chdir(ROOT)
    import os88flush
    import os88marty as om
    import os88sheetfmt as F
    import os88sym
    import os88ui
    import sheetfmt as SF
    need = ["build/mega.img", "build/megak/kernel.map"]
    need += [a.sheet] if a.leg == "sheet" else [
        a.word, a.scribe, "build/WELCOME.DOC", "build/SCWELCOM.DOC",
        "build/SCWELCOM.RTF"]
    for f in need:
        if not os.path.exists(f):
            sys.exit("officetrap: %s is not built - `make mega` / `make "
                     "worddisk scribedisk`" % f)
    was = os.environ.get("OS88_BUILD")
    os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "megak")
    meq = os88sym.equates(("KERN_MEGA",))
    if was is None:
        os.environ.pop("OS88_BUILD")
    else:
        os.environ["OS88_BUILD"] = was
    kcs = {0x0060, 0x0028, meq["MEGA_SEL_CODE"]}
    fails = []
    work = tempfile.mkdtemp(prefix="officetrap-")
    disk = os.path.join(work, "disk.img")

    def want(ok, what):
        print("   %s  %s" % ("ok  " if ok else "FAIL", what), flush=True)
        if not ok:
            fails.append(what)
        return ok

    def image_faults(lines):
        sites = {}
        for l in lines:
            t = l.split()
            if len(t) > 1 and t[0].startswith("#") and ":" in t[1]:
                cs = int(t[1].split(":")[0], 16)
                if cs < 0xF000 and cs not in kcs:
                    sites[t[1]] = sites.get(t[1], 0) + 1
        return sites

    def charged(ui, f0, n):
        sites = image_faults(ui.m.faults()[f0:])
        want(not sites, "%s: no exception charged to a loaded image (%d at "
             "%d sites, the most: %s)" % (n, sum(sites.values()), len(sites),
                                          sorted(sites.items(),
                                                 key=lambda kv: -kv[1])[:6]))

    def mkdisk(files):
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "1440"] + files, check=True,
                       stdout=subprocess.DEVNULL)

    def readback(ui, names):
        im = os.path.join(work, "back.img")
        ui.m.flush(1, im)
        vol = os88flush.Volume(open(im, "rb").read())
        have = vol.names()
        return {n: (vol.read(n) if n in have else None) for n in names}

    def idle(ui, secs=0.5):
        om.ui_done(ui.m, cap=20.0)
        om.guest_sleep(ui.m, secs)

    def slow(ui, text, gap=0.2):
        for ch in text:                    # unpaced typing drops keys on
            ui.m.type_text(ch)             # 86Box (the hands-on pass's
            om.guest_sleep(ui.m, gap)      # harness note)

    def menu(ui, title, item, then=1.0):
        ui.m.alt(title)
        om.guest_sleep(ui.m, 0.6)
        ui.m.key(item)
        om.guest_sleep(ui.m, then)

    if a.leg == "sheet":
        cells = {(r, 0): ('formula', f, 0.0) for r, f in enumerate(LOOP)}
        for r in range(20):                       # the BIFF load's formulas
            cells[(r, 1)] = float(r + 1)
            cells[(r, 2)] = ('formula', 'B%d*2+SQRT(B%d)' % (r + 1, r + 1),
                             0.0)
        body = F.write_sylk(cells).decode('latin-1').split('\r\n')
        body[1:1] = ['NN;NLOOP;ER1C1']
        src = os.path.join(work, "MAC.SLK")
        open(src, "wb").write('\r\n'.join(body).encode('latin-1'))
        mkdisk([a.sheet, src])
        macro = SF.mega_y((435, 45), SF.SHEET_DY)
        run = SF.mega_y((macro[0] + 17, 57 + 12 + 2), SF.SHEET_DY)
        with os88ui.boot("build/mega.img", apps=disk, machine="at286_vga_sb",
                         label="officetrap-sheet") as ui:
            f0 = len(ui.m.faults())
            w = ui.path("B:/MAC.SLK")
            idle(ui, 2)
            for _ in range(3):
                ui.mo.menu(macro[0], macro[1], *run)
                idle(ui, 1)
                if any(t == "Run" for t in ui.titles()):
                    break
            want(any(t == "Run" for t in ui.titles()),
                 "1: Macro > Run... opened its dialog (%r)" % ui.titles())
            slow(ui, "\b" * 8 + "LOOP")
            ui.m.key("Enter")
            idle(ui, 4)
            ui.mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1], SF.SAVE_AS[0],
                       SF.SAVE_AS[1])
            idle(ui, 1)
            ui.mo.click(*SF.fdlg_xy(ui.m, SF.FMT_ROW['bif']))
            idle(ui)
            ui.mo.click(*SF.fdlg_okxy(ui.m))
            idle(ui, 1)
            ui.mo.click(*SF.SAVE_BUTTON)
            idle(ui, 4)
            got = readback(ui, ["MAC.BIF"])["MAC.BIF"]
            v = F.read_biff(got) if got else {}
            h2 = F.scalar(v.get((1, 7))) if v else None
            want(h2 == 5050.0, "2: the macro's answer saved: H2 = %r, 5050 "
                 "wanted" % (h2,))
            ui.close(w)
            idle(ui, 1)
            w = ui.path("B:/MAC.BIF")
            idle(ui, 3)
            want(any("MAC.BIF" in t for t in ui.titles()),
                 "3: MAC.BIF opened again (%r)" % ui.titles())
            charged(ui, f0, "4")
    else:
        mkdisk([a.word, a.scribe, "build/WELCOME.DOC", "build/SCWELCOM.DOC",
                "build/SCWELCOM.RTF"])
        with os88ui.boot("build/mega.img", apps=disk, machine="at286_vga_sb",
                         label="officetrap-word") as ui:
            f0 = len(ui.m.faults())
            ui.path("B:/WORD.O88")
            idle(ui, 1)
            menu(ui, "KeyF", "KeyO")
            ui.chooser_open("WELCOME.DOC")
            ui.wait_toast(says="Loaded WELCOME.DOC")
            idle(ui)
            menu(ui, "KeyF", "KeyS")
            ui.wait_toast(says="Saved WELCOME.DOC")
            idle(ui)
            menu(ui, "KeyU", "KeyO")
            ui.m.key("Enter")
            s1 = ui.wait_toast(says="Sorted")
            idle(ui)
            menu(ui, "KeyU", "KeyR")
            ui.m.key("Enter")
            s2 = ui.wait_toast(says="Renumbered")
            want(True, "1: Word loaded, saved, %r, %r" % (s1, s2))
            dw = ui.window("Disk")          # Word's window covers B:'s
            ui.raise_window(dw)             # icon: the Disk window that
            ui.open("SCRIBE.O88", win=dw)   # opened WORD.O88 opens it
            idle(ui, 1)
            menu(ui, "KeyF", "KeyO")
            ui.chooser_open("SCWELCOM.DOC")
            ui.wait_toast(says="Loaded SCWELCOM.DOC")
            idle(ui)
            menu(ui, "KeyF", "KeyS")
            ui.wait_toast(says="Saved SCWELCOM.DOC")
            idle(ui)
            menu(ui, "KeyF", "KeyO")
            ui.chooser_open("SCWELCOM.RTF")
            ui.wait_toast(says="Loaded SCWELCOM.RTF")
            idle(ui)
            menu(ui, "KeyF", "KeyS")
            ui.wait_toast(says="Saved SCWELCOM.RTF")
            idle(ui)
            menu(ui, "KeyU", "KeyO")
            ui.m.key("Enter")
            s1 = ui.wait_toast(says="Sorted")
            idle(ui)
            menu(ui, "KeyU", "KeyR")
            ui.m.key("Enter")
            s2 = ui.wait_toast(says="Renumbered")
            want(True, "2: Scribe loaded and saved both, %r, %r" % (s1, s2))
            back = readback(ui, ["WELCOME.DOC", "SCWELCOM.DOC",
                                 "SCWELCOM.RTF"])
            charged(ui, f0, "4")

        def wtx(path_or_bytes):
            p = path_or_bytes
            if isinstance(p, bytes):
                p = os.path.join(work, "x.doc")
                open(p, "wb").write(path_or_bytes)
            return subprocess.run([sys.executable, "tools/wordfmt.py", p,
                                   "--wtx"], capture_output=True,
                                  text=True).stdout
        for n in ("WELCOME.DOC", "SCWELCOM.DOC"):
            got = back[n]
            want(got is not None and wtx(got) == wtx("build/" + n)
                 and wtx(got), "3: %s saved says what the original says "
                 "(%s bytes)" % (n, len(got) if got else None))
        rtf = back["SCWELCOM.RTF"]
        orig = open("build/SCWELCOM.RTF", "rb").read()
        want(rtf is not None and rtf.startswith(b"{\\rtf")
             and rtf.count(b"\\par\r") + rtf.count(b"\\par\n")
             + rtf.count(b"\\par ") >= 1
             and rtf.count(b"\\pard") == orig.count(b"\\pard"),
             "3: SCWELCOM.RTF saved, %s \\pard against the original's %d"
             % (rtf.count(b"\\pard") if rtf else None,
                orig.count(b"\\pard")))
    print("officetrap %s: %s" % (a.leg, "ok" if not fails else
                                 "FAILED: " + "; ".join(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
