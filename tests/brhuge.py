#!/usr/bin/env python3
"""brhuge - a page larger than the Browser's kern_mega cap of before, whole
(docs/plans/MEGA-HUGE-PLAN.md 3, SPEC.md 71.18.2).

    make && make mega && python3 tests/brhuge.py [--keep]

On kern_mega the Browser's source and formatted document were claims of the
KB doors: above 1MB while 64KB or less (OS88_HIGHMEM), and past 64KB moved
down to the CONVENTIONAL heap, where br_memroom kept a fifth of the free KB
in reserve - so a page whose source and stream together passed about 80% of
~560KB was cut short whatever the machine had above 1MB. Since 71.18.2 the
two are OSAPI_MEM_HUGE claims of one run (HUGE_NOBRIDGE): above 1MB at any
size, regrown there (SPEC.md 50.3.5), and the reserve is a share of the free
KB of both regions (MEMH_AVAIL).

Boots kern_mega under Bochs (16MB) with a B: of its own: BROWSER.O88 and
DOCHUGE.HTM, 5,000 numbered rows of 65 bytes in a <pre> - a ~331KB source
that formats to a ~331KB stream, 5,001 lines (the line table is 50,010
bytes, under BR_LINEKB). Opened off B: by association, this asserts:

  1. the source is the file's whole size, and both claims are ABOVE 1MB;
  2. the page is not marked truncated, its stream is longer than the
     source's text rows, and the stream's tail - read out of the document
     claim by its descriptor - holds the page's end marker;
  3. 5,001 lines laid out, and End paints the last of them;
  4. no kern_mega FAULT or CLOSED line.

Broken on purpose (docs/WRITING-TESTS.md 1): mega-release before 71.18.2
(br_hclaim taking the KB doors, br_avail conventional memory) - measured: the
source conventional, the document stopped at the reserve and the page cut
(br_trunc 1), the end marker absent, fewer lines.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "megak")
import os88bochs  # noqa: E402
import os88sym  # noqa: E402
from os88fixture import need  # noqa: E402

ROWS = 5000
END = b"END OF HUGE DOCUMENT ABOVE 64K"
FAILS = []


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def page(path):
    body = bytearray(b"<!doctype html><title>Huge Document</title><pre>\n")
    row = b"huge-document-content-0123456789-ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    for n in range(ROWS):
        body.extend(("%04d " % n).encode("ascii") + row + b"\n")
    body.extend(END + b"\n</pre>")
    with open(path, "wb") as f:
        f.write(body)
    return len(body)


def main():
    keep = "--keep" in sys.argv
    for p in ("build/mega.img", "build/browser.o88", "build/megak/kernel.map"):
        need(p)
    syms = os88sym._parse_map(os.path.join(ROOT, "build", "browser.map"))[0]
    import dispcp                                     # (after OS88_BUILD)
    fix = tempfile.mkdtemp(prefix="brhuge-fix-")
    pg = os.path.join(fix, "dochuge.htm")
    size = page(pg)
    img = os.path.join(fix, "b.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", img, "--size", "1440",
                    os.path.join(ROOT, "build", "browser.o88"), pg],
                   check=True, stdout=subprocess.DEVNULL)
    out = tempfile.mkdtemp(prefix="brhuge-")
    t0 = time.time()
    disp = (None if os.environ.get("OS88_BOCHS_DISPLAY")
            else os88bochs.take_display())
    box = os88bochs.Box(out, [], bimg=img, btype="1_44", display=disp)
    try:
        box.up()
        box.capture()

        def br(label, n):
            return box.pkg_read("BROWSER", syms[label], n)

        def at(w):
            sel = u16(br(w, 2))
            return box.sel_base(sel) if sel & 4 else sel * 16

        box.dbl(591, 105, "B:'s Disk window")
        box.dbl(150, 139, "DOCHUGE.HTM - the Browser, by association")
        def state():
            return ("srclen %d srckb %d dockb %d doclen %d trunc %d docfull %d "
                    "nlines %d parse %d" % (
                        u16(br("br_srclen", 2)) | u16(br("br_srclen_hi", 2)) << 16,
                        u16(br("br_srckb", 2)), u16(br("br_dockb", 2)),
                        u16(br("br_doclen", 2)) | u16(br("br_doclen_hi", 2)) << 16,
                        br("br_trunc", 1)[0], br("br_docfull", 1)[0],
                        u16(br("br_nlines", 2)), br("br_parse_started", 1)[0]))
        try:
            box.until(lambda: u16(br("br_nlines", 2)) >= 1 and
                      br("br_parse_started", 1)[0] in (0, 2),
                      "the page parsed and laid out", secs=150)
        except Exception:
            print("   the Browser's state: " + state(), flush=True)
            raise
        print("   the Browser's state: " + state(), flush=True)
        box.ui_done("the page painted", secs=180)
        srclen = u16(br("br_srclen", 2)) | u16(br("br_srclen_hi", 2)) << 16
        want(srclen == size, "the source is the file's %d bytes (reads %d)"
             % (size, srclen))
        src, doc = at("br_srcseg"), at("br_docseg")
        print("   the source at %06Xh (%d KB), the document at %06Xh (%d KB)"
              % (src, u16(br("br_srckb", 2)), doc, u16(br("br_dockb", 2))))
        want(src >= 0x100000 and doc >= 0x100000,
             "the source and the document both ABOVE 1MB")
        trunc = br("br_trunc", 1)[0]
        want(trunc == 0, "the page is not cut short (br_trunc %d)" % trunc)
        dl = u16(br("br_doclen", 2)) | u16(br("br_doclen_hi", 2)) << 16
        want(dl > ROWS * 64, "the stream is %d bytes (the rows alone are %d)"
             % (dl, ROWS * 64))
        tb = box.read(doc + max(0, dl - 600), 600)
        want(END in tb, "the stream's tail, at linear %06Xh + %d, holds the "
             "page's end marker" % (doc, max(0, dl - 600)))
        nl = u16(br("br_nlines", 2))
        want(nl >= ROWS + 1, "%d lines laid out (want %d or more)"
             % (nl, ROWS + 1))
        raw = box.read("wm_wins", dispcp.MAX_WIN * dispcp.WIN_SIZE)
        best = None
        for i in range(dispcp.MAX_WIN):
            r = raw[i * dispcp.WIN_SIZE:(i + 1) * dispcp.WIN_SIZE]
            if u16(r, dispcp.W_FLAGS) & 3 == 3:
                x, y, w, h = (u16(r, dispcp.W_X), u16(r, dispcp.W_Y),
                              u16(r, dispcp.W_W), u16(r, dispcp.W_H))
                if w > 300 and (best is None or w * h > best[2] * best[3]):
                    best = (x, y, w, h)
        if best:
            wx, wy, ww, wh = best
            box.click(wx + ww // 2, wy + wh - 12, "a click in the page")
            box.key("End")
            box.ui_done("End", secs=120)
            top = u16(br("br_top", 2))
            rows = u16(br("br_rows", 2))
            lb = br("br_lbuf", 200)
            print("   top %d of %d lines, %d rows; last row painted %r"
                  % (top, nl, rows, lb[:80].rstrip(b" \0")), flush=True)
            want(top + rows >= nl and END in lb,
                 "End painted the last lines, the last of them the page's")
        else:
            want(False, "the Browser's window is on the desk")
        bad = box.faults()
        want(not bad, "no kern_mega FAULT or CLOSED line%s"
             % "".join(" | " + ln for ln in bad[:3]))
    except Exception as e:      # noqa: BLE001 - the machine stopped
        bad = box.faults()
        want(False, "%s%s" % (e, "".join(" | " + ln for ln in bad[:3])))
    finally:
        try:
            box.shot(os.path.join(out, "last.png"))
        except Exception:       # noqa: BLE001
            pass
        box.stop()
        if keep:
            print("brhuge: the boot is in " + out)
        else:
            shutil.rmtree(out, ignore_errors=True)
        shutil.rmtree(fix, ignore_errors=True)
    print("brhuge: %.0fs under Bochs" % (time.time() - t0))
    for f in FAILS:
        print("FAIL brhuge: " + f)
    print("brhuge: %s" % ("FAILED" if FAILS else "PASS"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
