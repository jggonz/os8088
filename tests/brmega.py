#!/usr/bin/env python3
"""brmega - the Browser's arenas past 64KB under kern_mega's protected mode
(KERN-MEGA-MEMORY-PLAN MC7, SPEC.md 71.18).

    make && make mega && python3 tests/brmega.py [--keep] [--local]

Under kern_mega every claim a package holds is an LDT SELECTOR, and the
Browser's document, line table and source each grow past 64KB, addressed
until MC7 by paragraph sums - `[br_docseg] + (offset >> 4)` in br_docmap,
`[br_srcseg] + (pos >> 4)` in the parser's window, `[br_srcseg] + hi << 12`
in the worker's receive - which name some other descriptor on a selector.
They are bound windows now (OSAPI_SEG's SEG_BWIN + SEG_AIM, br_bwaim), with
every real-mode kernel's sum untouched.

The package declares OS88_HIGH and OS88_HIGHMEM (tests/megahigh.txt's two
Browser lines are retired on this row), so its region runs above 1MB and
every claim of 64KB or less goes there too; a claim regrown past 64KB comes
back down to conventional memory with a new selector.

Boots kern_mega under Bochs (16MB, an NE2000 on Bochs's slirp, SYSTEM.CFG
wanting ETHER.DRV) with a B: of its own: BROWSER.O88 and DOCWIDE.HTM,
tools/brmemory.py's page whose SOURCE (78,380 bytes) and FORMATTED STREAM
both cross the 64KB seam. Three legs, one boot, each asserting that the
region is above 1MB, the source length is the file's, the line count is the
page's, the stream's TAIL - read out of the document claim by its LDT
descriptor's base - holds the page's last text (so the window wrote where the
identity map's sum would have), the line table's last records (read out of
ITS claim, above 1MB) span that text, and that End paints the last lines:

  1. LOCAL: DOCWIDE.HTM opened off B:. br_load reads it, br_parse writes
     the stream through br_docmap's window, br_layout lays it out (1,205
     lines - the count the same page lays out to on the XT), and the line
     the painter composed last ([br_lbuf], br_build) is the page's last.
  2. NETWORK: the same page over HTTP from a server this row runs on the
     host (10.0.2.2, a port the OS picks), typed into the location bar. The
     worker's receive (br_take) stores past 64KB of the source claim, the
     claim grows four KB at a time (br_srcgrow, each regrow giving the bound
     windows back first), and the worker parses it.
  3. SMALL: linearena.htm, 3,000 lines in a 6KB stream, over HTTP - every
     claim of the page under 64KB and so ABOVE 1MB, read through windows
     bound to pool blocks.

And over all three: no `kern_mega CLOSED` (a package's #GP, completed by
closing it) or `kern_mega FAULT` line in e9.out. `--local` runs leg 1 alone.

Broken on purpose (docs/WRITING-TESTS.md 1), each seen when the row was
written: br_docmap's `[br_docfar]` test put back to `[br_docems]` (kern_mega
takes the paragraph sum) closes the Browser on leg 1 - `kern_mega CLOSED
BROWSER 0D` in br_docmap - and so does the Browser as it was before MC7;
br_take's `jne .convpm` taken out (the receive's `+ hi << 12` sum) passes
leg 1 and closes it on leg 2 (`CLOSED BROWSER 0D at ..:8B81`).
"""
import functools
import http.server
import os
import shutil
import socketserver
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "megak")
import os88bochs  # noqa: E402
import os88sym  # noqa: E402
from os88fixture import need  # noqa: E402

PORT = [0]                      # the host server's, picked by the OS so no
                                # other row's fixed port can collide
LN_SIZE = 10                    # a line record (browser.asm's LN_*)
ETHER_BIT = 0x10                # kernel/driver.inc drv_cfgbit: ETHER.DRV
# (file, lines it lays out to - the XT's own count for DOCWIDE, -N = at
# least N - the text
# its stream ends with, its last visible line, whether its arenas pass 64KB)
WIDE = ("docwide.htm", 1205, b"END OF WIDE DOCUMENT ABOVE 64K",
        b"HIGH-OFFSET-LINK", True)
TALL = ("linearena.htm", -3000, b"END OF 3000 LINE ARENA TEST",
        b"END OF 3000 LINE ARENA TEST", False)
FAILS = []


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


GETS = []


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        GETS.append(self.path)
        return super().do_GET()


def serve(root):
    socketserver.TCPServer.allow_reuse_address = True
    srv = socketserver.TCPServer(("0.0.0.0", 0),
                                 functools.partial(Quiet, directory=root))
    PORT[0] = srv.server_address[1]
    t = threading.Thread(target=srv.serve_forever, daemon=True)
    t.start()
    return srv


def main():
    keep = "--keep" in sys.argv
    local_only = "--local" in sys.argv
    for p in ("build/mega.img", "build/browser.o88", "build/megak/kernel.map",
              "build/megarun.o88"):
        need(p)
    syms = os88sym._parse_map(os.path.join(ROOT, "build", "browser.map"))[0]
    import dispcp                                     # (after OS88_BUILD)
    fix = tempfile.mkdtemp(prefix="brmega-fix-")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "brmemory.py"),
                    "--out", fix], check=True, stdout=subprocess.DEVNULL)
    page = os.path.join(fix, WIDE[0])
    img = os.path.join(fix, "b.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", img, "--size", "1440",
                    os.path.join(ROOT, "build", "browser.o88"), page],
                   check=True, stdout=subprocess.DEVNULL)
    srv = None if local_only else serve(fix)
    out = tempfile.mkdtemp(prefix="brmega-")
    t0 = time.time()
    # a display of its own unless the runner handed one (os88test.py does),
    # so a Bochs somebody else left on :99 cannot take this row's clicks
    disp = (None if os.environ.get("OS88_BOCHS_DISPLAY")
            else os88bochs.take_display())
    box = os88bochs.Box(out, [], bimg=img, btype="1_44", display=disp,
                        ne2k=not local_only,
                        cfg=None if local_only else ETHER_BIT)
    try:
        box.up()
        box.capture()

        def br(label, n):
            return box.pkg_read("BROWSER", syms[label], n)

        def tail_bytes():
            """The formatted stream's last 600 bytes, read out of the
            document claim by its descriptor's base - past its first 64KB."""
            sel = u16(br("br_docseg", 2))
            dl = u16(br("br_doclen", 2)) | u16(br("br_doclen_hi", 2)) << 16
            base = box.sel_base(sel)
            return box.read(base + max(0, dl - 600), 600), sel, base, dl

        def brwin():
            raw = box.read("wm_wins", dispcp.MAX_WIN * dispcp.WIN_SIZE)
            best = None
            for i in range(dispcp.MAX_WIN):
                r = raw[i * dispcp.WIN_SIZE:(i + 1) * dispcp.WIN_SIZE]
                if u16(r, dispcp.W_FLAGS) & 3 == 3:
                    x, y, w, h = (u16(r, dispcp.W_X), u16(r, dispcp.W_Y),
                                  u16(r, dispcp.W_W), u16(r, dispcp.W_H))
                    if w > 300 and (best is None or w * h > best[2] * best[3]):
                        best = (x, y, w, h)
            return best

        def where(leg):
            """Print where the region and the four page claims landed - a
            selector's base off its descriptor - and answer them."""
            at = {"region": box.pkg_base("BROWSER")}
            for w in ("br_srcseg", "br_docseg", "br_lseg", "br_lnkseg"):
                sel = u16(br(w, 2))
                at[w] = box.sel_base(sel) if sel & 4 else sel * 16
            print("   %s: %s" % (leg, ", ".join(
                "%s %06Xh%s" % (k, v, " (above 1MB)" if v >= 0x100000 else "")
                for k, v in at.items())), flush=True)
            return at

        def check(leg, page):
            name, lines, tail, last, wide = page
            size = os.path.getsize(os.path.join(fix, name))
            def laid(n):
                return n == lines or (lines < 0 and n >= -lines)
            box.until(lambda: laid(u16(br("br_nlines", 2))) and
                      br("br_parse_started", 1)[0] in (0, 2),
                      "%s: the page laid out at %d lines" % (leg, abs(lines)),
                      secs=240)
            box.ui_done("%s: the page painted" % leg, secs=120)
            wx, wy, ww, wh = brwin()
            at = where(leg)
            want(at["region"] >= 0x100000, "%s: the region runs above 1MB "
                 "(OS88_HIGH) at %06Xh" % (leg, at["region"]))
            srclen = (u16(br("br_srclen", 2)) |
                      u16(br("br_srclen_hi", 2)) << 16)
            want(srclen == size, "%s: the source is the file's %d bytes "
                 "(reads %d)" % (leg, size, srclen))
            tb, sel, base, dl = tail_bytes()
            if wide:
                want(dl > 65536, "%s: the formatted stream is %d bytes, past "
                     "the 64KB seam" % (leg, dl))
            else:
                want(at["br_docseg"] >= 0x100000 and
                     at["br_lseg"] >= 0x100000,
                     "%s: a %d-byte stream and its line table are claims of "
                     "64KB or less, ABOVE 1MB (OS88_HIGHMEM), read through "
                     "a window bound to a pool block" % (leg, dl))
            want(tail in tb and last in tb,
                 "%s: the stream's tail, at linear %06Xh + %d (selector "
                 "%04Xh), holds the page's last text" % (leg, base,
                                                        max(0, dl - 600), sel))
            nl = u16(br("br_nlines", 2))
            want(laid(nl), "%s: %d lines laid out (want %s)"
                 % (leg, nl, lines if lines > 0 else "%d or more" % -lines))
            # End: the band paints the page's last rows, each through
            # br_docmap's window - the painter's last line is in br_lbuf.
            # The page takes the keys only once the location bar lets go of
            # them (SPEC.md 71.7): a click in the band, then End
            box.click(wx + ww // 2, wy + wh - 12, "a click in the page")
            box.key("End")
            box.ui_done("%s: End" % leg)
            top = u16(br("br_top", 2))
            rows = u16(br("br_rows", 2))
            lb = br("br_lbuf", 200)
            print("   %s: top %d of %d lines, %d rows; last row painted %r"
                  % (leg, top, nl, rows, lb[:80].rstrip(b" \0")), flush=True)
            want(top + rows >= nl and (last in lb or not wide),
                 "%s: End painted the last lines (top %d + %d rows, %d "
                 "lines)%s" % (leg, top, rows, nl,
                               ", the last of them the page's" if wide
                               else ""))
            # ...and the LINE TABLE agrees with the stream: the records of
            # the last few lines, read out of the table's own claim (above
            # 1MB on this machine) by its descriptor, span the page's last
            # text in the document claim
            lsel = u16(br("br_lseg", 2))
            lbase = box.sel_base(lsel) if lsel & 4 else lsel * 16
            recs = box.read(lbase + (nl - 3) * LN_SIZE, 3 * LN_SIZE)
            texts = []
            for k in range(3):
                r = recs[k * LN_SIZE:(k + 1) * LN_SIZE]
                o = u16(r, 0) | u16(r, 2) << 16
                e = u16(r, 4) | u16(r, 6) << 16
                if 0 <= o < e <= dl and e - o < 200:
                    texts.append(box.read(base + o, e - o))
            want(any(last in t for t in texts),
                 "%s: the last line records span %r" % (leg, last))
            box.shot(os.path.join(out, "%s-end.png" % leg))

        def fetch(leg, page):
            """Type the page's URL into the bar and wait for the fetch."""
            bx0, by0, bww, _ = brwin() or (None, None, None, None)
            want(bx0 is not None, "%s: the Browser's window is on the desk"
                 % leg)
            if bx0 is None:
                return
            # SPEC.md 71.6's toolbar is above the bar (brfetch's constants):
            # click past the text, backspace it away
            box.click(bx0 + bww - 40, by0 + dispcp.TITLE_H + 1 + 3 + 13
                      + 1 + 15 // 2, "the location bar")
            for _ in range(40):
                box.key("BackSpace")
            box.type("http://10.0.2.2:%d/%s" % (PORT[0], page[0]))
            box.ui_done("the URL typed")
            n = len(GETS)
            box.key("Return")
            # THE LAST PAGE IS STILL UP, so what check() waits on may already
            # be true: wait for the fetch itself first - the host saw the GET,
            # and the browser says DONE (7 = ERR)
            box.until(lambda: len(GETS) > n, "the GET at the host", secs=120)
            box.until(lambda: br("br_nstate", 1)[0] in (6, 7),
                      "the fetch to finish", secs=300)
            want(br("br_nstate", 1)[0] == 6 and GETS[n:] == ["/" + page[0]],
                 "%s: one GET of /%s, and the fetch is DONE (state %d)"
                 % (leg, page[0], br("br_nstate", 1)[0]))
            check(leg, page)

        # --- 1: the page off B: --------------------------------------------
        box.dbl(591, 105, "B:'s Disk window")
        box.dbl(150, 139, "DOCWIDE.HTM - the Browser, by association")
        check("local", WIDE)

        # --- 2, 3: over HTTP - the wide page again, then a tall small one --
        if not local_only:
            fetch("net", WIDE)
            fetch("small", TALL)

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
        if srv:
            srv.shutdown()
        if keep:
            print("brmega: the boot is in " + out)
        else:
            shutil.rmtree(out, ignore_errors=True)
        shutil.rmtree(fix, ignore_errors=True)
    print("brmega: %.0fs under Bochs" % (time.time() - t0))
    for f in FAILS:
        print("FAIL brmega: " + f)
    print("brmega: %s" % ("FAILED" if FAILS else "PASS"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
