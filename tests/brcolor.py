#!/usr/bin/env python3
"""Page colours (SPEC.md 71.34, Browser 1.8 step 2).

    make && make browsertest ethertest && python3 tests/brcolor.py

QEMU, because MartyPC has no NIC. Each page is served from here and the
band is read off a screendump, by colour:

  C1  <body bgcolor=navy text=yellow link=white>: the ground and the
      margins are blue, the text yellow
  C2  <font color=red> in a sentence: red and black on one row
  C3  a table cell with bgcolor and white text: the cell is red
  C4  <font color=red> across a wrapped paragraph: every line of it red
      (the layout's checkpoint)
  C5  <body background=x.gif text=yellow> and no bgcolor: black on white,
      the text colour dropped
  C6  text after a table that left a <font> open in a cell: black again
"""
import functools
import http.server
import os
import socketserver
import subprocess
import sys
import threading

_OS88_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tools"))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tests"))
sys.path.insert(0, _OS88_ROOT)
import dispcp                                          # noqa: E402
from ethernet import (Qemu, u16, S, Mouse, type_url,  # noqa: E402
                      settle, SOCK, ether_syms)
import os88qemu                                        # noqa: E402
import os88fixture                                     # noqa: E402
from brclick import browser_syms                       # noqa: E402
from PIL import Image                                  # noqa: E402

PORT = 8097
BLUE, RED, YELLOW = (0, 0, 168), (168, 0, 0), (255, 255, 87)
WHITE, BLACK = (255, 255, 255), (0, 0, 0)
LONG = b"red words " * 40


def say(*a):
    print(*a)
    sys.stdout.flush()


PAGES = {
    "C1": b'<body bgcolor="#000080" text=yellow link=white><p>yellow text '
          b'on blue <a href="/x">a link</a> and more text.</body>',
    "C2": b"<body><p>black words <font color=red>RED WORDS</font> black "
          b"again.</body>",
    "C3": b'<body><table border=1><tr><td bgcolor="#ff0000"><font '
          b'color=white>WHITE ON RED</font></td><td>plain</td></tr>'
          b"</table></body>",
    "C4": b"<body><p><font color=red>" + LONG + b"</font></body>",
    "C5": b'<body background="/x.gif" text=yellow><p>this text stays '
          b"black.</body>",
    "C6": b"<body><table><tr><td><font color=red>RED</td></tr></table>"
          b"<p>BLACK AFTER THE TABLE</body>",
}


def main():
    root = os.path.join(os88qemu.RUNDIR, "brcolor-www")
    os.makedirs(root, exist_ok=True)
    for name, html in PAGES.items():
        open(os.path.join(root, name + ".HTM"), "wb").write(html + b"\r\n")
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
    apps = os88qemu.scratch("build/brtest360.img", "brcolor-apps.img")
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
    shot = os.path.join(os88qemu.RUNDIR, "brcolor.png")

    def band():
        subprocess.run(["python3", "tools/shot.py", SOCK, shot], check=True,
                       capture_output=True)
        im = Image.open(shot).convert("RGB")
        x1, y1 = rw("br_cx"), rw("br_cy")
        x2 = rw("br_sbx") - 1
        y2 = y1 + rw("br_ch") - 1
        n = {}
        rows = {}
        for y in range(y1, y2 + 1):
            for x in range(x1, x2 + 1):
                p = im.getpixel((x, y))
                n[p] = n.get(p, 0) + 1
                rows.setdefault(p, set()).add(y)
        return n, rows, im.getpixel((x2 - 2, y2 - 2))

    for name in [p for p in PAGES if p in os.environ.get("BRCONLY", " ".join(PAGES))]:
        k0 = len(asked)
        go("http://10.0.2.2:%d/%s.HTM" % (PORT, name))
        os88qemu.acted(m, lambda: "/%s.HTM" % name in asked[k0:], secs=60,
                       what="%s asked" % name)
        os88qemu.acted(m, lambda: rb("br_nstate") in (6, 7), secs=60,
                       what="%s laid out" % name)
        os88qemu.pace(m, 2.0)
        if os.environ.get("BRCDBG"):
            nl = rw("br_nlines")
            tab = bytes(m.readseg(rw("br_lseg"), 0, nl * 10))
            dl = rw("br_comp")
            doc = bytes(m.readseg(rw("br_docseg"), 0, dl))
            say("doclen", rw("br_doclen"))
            for i in range(nl):
                r = tab[i * 10:i * 10 + 10]
                o, e = r[0] | r[1] << 8, r[4] | r[5] << 8
                say(i, o, e, r[8], r[9], doc[o:e])
        n, rows, corner = band()
        im = Image.open(shot)
        im.save(os.path.join(os88qemu.RUNDIR, "brcolor-%s.png") % name)
        if rb("br_bpp") == 1:
            say("%s: 1bpp, nothing to colour" % name)
            continue
        c = lambda p: n.get(p, 0)                      # noqa: E731
        if name == "C1":
            if corner != BLUE:
                fails.append("C1: the margin is %r, blue expected" % (corner,))
            if c(YELLOW) < 50:
                fails.append("C1: %d yellow pixels" % c(YELLOW))
            if c(BLUE) < c(WHITE):
                fails.append("C1: more white than blue in the band")
        elif name == "C2":
            if c(RED) < 30 or c(BLACK) < 30:
                fails.append("C2: red %d, black %d" % (c(RED), c(BLACK)))
            if rows.get(RED, set()) - rows.get(BLACK, set()):
                fails.append("C2: red on a row with no black")
        elif name == "C3":
            if c(RED) < 300:
                fails.append("C3: %d red pixels, a filled cell expected"
                             % c(RED))
        elif name == "C4":
            ry = rows.get(RED, set())
            if len(ry) < 20:
                fails.append("C4: red on %d pixel rows, three lines "
                             "expected" % len(ry))
            if c(BLACK) > 40:
                fails.append("C4: %d black pixels in a red paragraph"
                             % c(BLACK))
        elif name == "C5":
            if c(YELLOW) or corner != WHITE:
                fails.append("C5: yellow %d, margin %r" % (c(YELLOW), corner))
        elif name == "C6":
            if c(BLACK) < 30:
                fails.append("C6: the text after the table is not black")
        say("%s: %s" % (name, ", ".join("%s %d" % (k, c(v)) for k, v in
            (("blue", BLUE), ("red", RED), ("yellow", YELLOW),
             ("black", BLACK), ("white", WHITE)))))
    m.quit()
    for f in fails:
        say("FAIL " + f)
    say("brcolor: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
