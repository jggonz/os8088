#!/usr/bin/env python3
"""Pictures in table cells (SPEC.md 71.33, Browser 1.8).

    make && make browsertest ethertest && python3 tests/brtabimg.py

QEMU, because MartyPC has no NIC: the pages are served from here. Before
1.8 the table composer dropped a cell's D_IMG record with the other markers,
so a picture in a table cell - most of the pictures on a 1996-1999 page -
was never laid out and never fetched. Each page here holds one table:

  P1  a picture alone in a cell, beside text
  P2  a picture between words in a cell (the fragment breaks around it)
  P3  a linked picture with an empty alt
  P4  a right-aligned cell
  P5  a picture inside a cell that also holds a nested table (flattened)
  P6  a picture wider than the band (468 pixels)
  P7  a picture aligned top: its rows go BELOW the text row
  P8  CNN's shape: a right-aligned cell whose picture is wider than the
      column the table's other cells squeeze it to. The alignment's spare
      went negative and padded 65,000 spaces; the table never ended

LEGS, per page: every picture is laid out, fetched once, ready, and byte for
byte tests/brgifref.py's copy at its box; the table's text row carries its
D_IMG record and the rows above or below it (LNF_IROW) are as many as the
picture reaches; and the text after the table is laid out.
"""
import functools
import http.server
import os
import socketserver
import subprocess
import sys
import threading
import time

_OS88_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tools"))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tests"))
sys.path.insert(0, _OS88_ROOT)
import dispcp                                          # noqa: E402
from ethernet import (Qemu, u16, S, Mouse, type_url,  # noqa: E402
                      settle, SOCK, ether_syms)
import os88qemu                                        # noqa: E402
import os88fixture                                     # noqa: E402
import pixcorpus as pc                                 # noqa: E402
import brgifref                                        # noqa: E402
from brclick import browser_syms                       # noqa: E402

PORT = 8096
D_IMG, LNF_IROW, LN_SIZE = 25, 8, 10


def say(*a):
    print(*a)
    sys.stdout.flush()


def gif(w, h, k):
    return pc.gif(w, h, [[(x // 4 + y // 4 + k) % 16 for x in range(w)]
                         for y in range(h)], gpal=pc.pal_n(16))


def img(n, w, h, extra=b"", alt=b"G"):
    return (b'<img src="/%s.GIF" width=%d height=%d alt="%s" %s>'
            % (n.encode(), w, h, alt, extra))


# name: (table html, [(picture name, w, h, attrs for the reference)])
PAGES = {
    "P1": (b"<table border=1><tr><td>" + img("P1A", 40, 16) +
           b"</td><td>text</td></tr><tr><td>a</td><td>b</td></tr></table>",
           [("P1A", 40, 16)]),
    "P2": (b"<table border=1><tr><td>left " + img("P2A", 40, 16) +
           b" right</td><td>x</td></tr></table>", [("P2A", 40, 16)]),
    "P3": (b'<table border=1><tr><td><a href="/x">' +
           img("P3A", 40, 16, alt=b"") + b"</a></td><td>x</td></tr></table>",
           [("P3A", 40, 16)]),
    "P4": (b"<table border=1><tr><td align=right>" + img("P4A", 40, 16) +
           b"</td><td>x</td></tr></table>", [("P4A", 40, 16)]),
    "P5": (b"<table border=1><tr><td>" + img("P5A", 40, 16) +
           b"<table><tr><td>inner</td></tr></table></td><td>x</td></tr>"
           b"</table>", [("P5A", 40, 16)]),
    "P6": (b"<table border=1><tr><td>" + img("P6A", 468, 60, alt=b"") +
           b"</td><td>x</td></tr></table>", [("P6A", 468, 60)]),
    "P7": (b"<table border=1><tr><td>" +
           img("P7A", 10, 60, b'align="top"', b"ad info") +
           b"</td><td>x</td></tr></table>", [("P7A", 10, 60)]),
    "P8": (b'<table width="770" border=0><tr><td width="120" align="right">'
           b'<a href="/x1">' + img("P8A", 120, 60, alt=b"") + b"</a>"
           b'<table width="100%"><tr><td><a href="/x2">Click Here</a></td>'
           b"</tr></table></td><td width=2>" + img("P8B", 2, 2, alt=b"") +
           b'</td><td width="10"><a href="/x3">' +
           img("P8C", 10, 60, b'align="top"', b"ad info") +
           b'</a></td><td width="488"><a href="/x4">' +
           img("P8D", 468, 60, alt=b"") + b"</a></td></tr></table>",
           [("P8A", 120, 60), ("P8B", 2, 2), ("P8C", 10, 60),
            ("P8D", 468, 60)]),
}


def main():
    root = os.path.join(os88qemu.scratch_dir() if hasattr(
        os88qemu, "scratch_dir") else "/tmp", "brtabimg-www")
    os.makedirs(root, exist_ok=True)
    files = {}
    k = 0
    for name, (table, pics) in PAGES.items():
        html = (b"<html><body><p>before\r\n" + table +
                b"\r\n<p>AFTER THE TABLE</body></html>\r\n")
        open(os.path.join(root, name + ".HTM"), "wb").write(html)
        for pn, w, h in pics:
            data = gif(w, h, k)
            k += 1
            files[pn] = data
            open(os.path.join(root, pn + ".GIF"), "wb").write(data)
    asked = []

    class Handler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def do_GET(self):
            asked.append(self.path.upper())
            return super().do_GET()
    os88qemu.lease_port(PORT)
    socketserver.TCPServer.allow_reuse_address = True
    srv = socketserver.TCPServer(("0.0.0.0", PORT),
                                 functools.partial(Handler, directory=root))
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    sy = browser_syms()
    os88fixture.need("build/ether360.img", "build/brtest360.img")
    apps = os88qemu.scratch("build/brtest360.img", "brtabimg-apps.img")
    os88qemu.make_test("ETHER=1", "TESTIMG=build/ether360.img",
                       "TESTAPPS=" + apps)
    m = Qemu()
    esy = ether_syms()
    os88qemu.acted(m, lambda: u16(m.read(S("drv_tab") + 2 * 16 + 2, 2)) != 0,
                   secs=60, what="ETHER.DRV")
    eseg = u16(m.read(S("drv_tab") + 2 * 16 + 2, 2))
    os88qemu.acted(m, lambda: m.readseg(eseg, esy["dhcp_st"], 1)[0] == 3,
                   secs=30, what="DHCP bound")
    mo = Mouse()
    dispcp.open_drive(m, mo, S, settle, "B")
    wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
    dispcp.open_named(m, mo, S, settle, wx, wy, "BROWSER.O88")
    bw = dispcp.win_list(m, S)[-1]
    rec = m.read(S("wm_wins") + bw * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
    pseg = u16(rec[dispcp.W_SEG:dispcp.W_SEG + 2])

    def rb(n, off=0):
        return m.readseg(pseg, sy[n] + off, 1)[0]

    def rw(n, off=0):
        return u16(m.readseg(pseg, sy[n] + off, 2))

    def go(url):
        lx2, ly1 = rw("br_loc", 4), rw("br_loc", 2)
        mo.click(lx2 - 6, ly1 + 7)
        os88qemu.pace(m, 1.0)
        n = rw("br_loc", 12)
        if n:
            subprocess.run(["python3", "tools/qmp.py", SOCK]
                           + ["sendkey backspace", "sleep 0.03"] * n,
                           check=True, capture_output=True)
            os88qemu.acted(m, lambda: rw("br_loc", 12) == 0, secs=5,
                           what="the bar emptied")
        type_url(url)
        os88qemu.acted(m, lambda: rw("br_loc", 12) == len(url), secs=5,
                       what="the URL typed")
        subprocess.run(["python3", "tools/qmp.py", SOCK, "sendkey ret"],
                       check=True, capture_output=True)

    fails = []
    mono = None
    for name, (table, pics) in PAGES.items():
        k0 = len(asked)
        go("http://10.0.2.2:%d/%s.HTM" % (PORT, name))
        os88qemu.acted(m, lambda: "/%s.HTM" % name in asked[k0:], secs=60,
                       what="%s asked" % name)
        os88qemu.acted(m, lambda: rw("br_imgn") == len(pics)
                       and rb("br_nstate") in (6, 7), secs=60,
                       what="%s laid out" % name)
        os88qemu.acted(m, lambda: rb("br_ie") == 0 and all(
            m.readseg(pseg, sy["br_itab"] + 16 * i, 1)[0] != 0
            for i in range(len(pics))), secs=180,
            what="%s's pictures" % name)
        mono = rb("br_bpp") == 1
        store = rw("br_istseg")
        # the pictures: laid, fetched once, ready, identical
        for i, (pn, w, h) in enumerate(pics):
            e = m.readseg(pseg, sy["br_itab"] + 16 * i, 16)
            n_asked = asked[k0:].count("/%s.GIF" % pn)
            want = brgifref.expected(files[pn], w, h, 0, mono)
            if not e[1] & 4:
                fails.append("%s %s: not laid out (IF_LAID)" % (name, pn))
            if n_asked != 1:
                fails.append("%s %s: asked for %d times" % (name, pn, n_asked))
            if e[0] != 1:
                fails.append("%s %s: state %d, ready expected"
                             % (name, pn, e[0]))
                continue
            wc, wr, wb = want
            if (e[14], e[15]) != (wc, wr):
                fails.append("%s %s: box %dx%d, %dx%d expected"
                             % (name, pn, e[14], e[15], wc, wr))
                continue
            got = bytes(m.readseg(store + (e[12] | e[13] << 8), 0, len(wb)))
            if got != wb:
                fails.append("%s %s: the copy differs" % (name, pn))
        # the lines: a D_IMG on a composed row, IROW rows, the text after
        nl = rw("br_nlines")
        tab = bytes(m.readseg(rw("br_lseg"), 0, nl * LN_SIZE))
        doc = bytes(m.readseg(rw("br_docseg"), 0, rw("br_comp")))
        withimg = irows = 0
        after = False
        for i in range(nl):
            r = tab[i * LN_SIZE:(i + 1) * LN_SIZE]
            o, e_ = r[0] | r[1] << 8, r[4] | r[5] << 8
            if r[9] & LNF_IROW:
                irows += 1
                continue
            span = doc[o:e_]
            if D_IMG in span and span[:1] in (b"|", b"+", b" ",
                                              bytes([0x81]), bytes([0x82])):
                withimg += 1                    # 81h/82h: the grid (71.34.1)
            # a top-aligned picture's later rows are SLICES on the cell's own
            # lines (D_CMETA, C0h + k, then its record: SPEC.md 71.34.5)
            irows += sum(1 for j in range(len(span) - 1)
                         if span[j] == 24 and 0xC0 <= span[j + 1] < 0xFF)
            if b"AFTER THE TABLE" in span:
                after = True
        want_rows = max(-(-h // 8) - 1 for _, _, h in pics)
        if not after:
            fails.append("%s: the text after the table was never laid out"
                         % name)
        if withimg < 1:
            fails.append("%s: no composed row carries a D_IMG record" % name)
        if irows < want_rows:
            fails.append("%s: %d picture rows (LNF_IROW), at least %d "
                         "expected" % (name, irows, want_rows))
        say("%s: %d picture(s) ready, %d composed row(s) with one, %d "
            "picture rows, %d lines" % (name, len(pics), withimg, irows, nl))
    m.quit()
    for f in fails:
        say("FAIL " + f)
    say("brtabimg: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
