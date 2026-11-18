#!/usr/bin/env python3
"""Tall headings (SPEC.md 71.35, Browser 1.8 step 3).

    make && make browsertest ethertest && python3 tests/brfont.py

QEMU, because MartyPC has no NIC. One page is served from here:

  <h1>ABC</h1><h2>Times</h2><p>body

and read off the machine and the glass:

  - part 8 was fetched on demand ([br_fwant] 2, [br_fnseg] set) and the
    Times face opened from the Browser's own volume ([br_fface] 1)
  - the line table holds two TALL records (LN_X bit 7), the first marked
    as the doubled cell (LNF_UNDER), each followed by its continuation row
  - "small <big>BIG</big> text" is a tall line on the 8x8 cell whose ink
    spans more than 8 rows (the face's capitals above the cell's)
  - a click on the bottom half of h1's second letter follows its link
  - on the glass, h1's ink spans more than 12 pixel rows, h2's capitals at
    least 8 (the face's cap height; no descender in the text) and the body's
    line no more than 8
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

PORT = 8100
LN_SIZE, LNF_IROW, LNF_UNDER, BRT_X2ADV = 10, 8, 128, 16
PAGE = (b"<body><h1><a href=/L.HTM>ABC</a></h1><h2>Times Roman</h2><p>small <big>BIG</big> text"
        b"<p><font face=arial color=red>Red in the face</font>"
        b"<p>body text in the cell.</body>")


def say(*a):
    print(*a)
    sys.stdout.flush()


def main():
    root = os.path.join(os88qemu.RUNDIR, "brfont-www")
    os.makedirs(root, exist_ok=True)
    look = os.environ.get("BRFPAGE")   # LOOK ONLY: serve this file, shoot
    open(os.path.join(root, "F1.HTM"), "wb").write(
        open(look, "rb").read() if look else PAGE + b"\r\n")
    open(os.path.join(root, "L.HTM"), "wb").write(b"<p>linked\r\n")
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
    apps = os88qemu.scratch("build/brtest360.img", "brfont-apps.img")
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

    fails = []
    if rb("br_fon") == 0:
        say("brfont: fonts are off on this CPU - nothing to test")
        m.quit()
        return 0
    url = "http://10.0.2.2:%d/F1.HTM" % PORT
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
    say("before: fwant %d fnseg %04x fface %d" % (rb("br_fwant"),
        rw("br_fnseg"), rb("br_fface")))
    type_url(url)
    os88qemu.acted(m, lambda: rw("br_loc", 12) == len(url), secs=5,
                   what="the URL typed")
    subprocess.run(["python3", "tools/qmp.py", SOCK, "sendkey ret"],
                   check=True, capture_output=True)
    os88qemu.acted(m, lambda: "/F1.HTM" in asked, secs=60, what="F1 asked")
    os88qemu.acted(m, lambda: rb("br_fwant") == 2, secs=60,
                   what="part 8 asked for")
    os88qemu.pace(m, 3.0)
    say("after: fwant %d fnseg %04x fface %d" % (rb("br_fwant"),
        rw("br_fnseg"), rb("br_fface")))
    if look:
        os88qemu.pace(m, float(os.environ.get("BRFWAIT", "2")))
        say("look: tbw %d tbok %d fin %d started %d nlines %d" % (
            rb("br_tbw"), rb("br_tbok"), rb("br_fin"),
            rb("br_parse_started"), rw("br_nlines")))
        nl = rw("br_nlines")
        tab = bytes(m.readseg(rw("br_lseg"), 0, nl * 10))
        say("doclen", rw("br_doclen"))
        for i in range(nl):
            say("L", i, tab[i*10:i*10+10].hex())
            r = tab[i*10:i*10+10]
            a, b = r[0] | r[1] << 8, r[4] | r[5] << 8
            if b > a:
                say("  ", bytes(m.readseg(rw("br_docseg"), a, b - a)))
        if os.environ.get("BRFIT"):

            st = [m.readseg(pseg, sy["br_itab"] + k * 16, 1)[0] for k in range(int(os.environ["BRFIT"]))]
            say("itab states", st, "ie", rb("br_ie"), "imvec", hex(rw("br_im_vec", 2)))
        if os.environ.get("BRFMEM"):
            raw = bytes(m.read(S("mem_tab"), 32 * 10))
            say("segs", " ".join("%s=%04x" % (n, rw(n)) for n in ("br_srcseg", "br_docseg", "br_lseg", "br_lnkseg", "br_urlseg", "br_istseg") if n in sy))
            for k in range(32):
                r = raw[k * 10:k * 10 + 10]
                if r[0] | r[1]:
                    say("claim seg %04x para %5d own %04x" % (r[0] | r[1] << 8, r[2] | r[3] << 8, r[4] | r[5] << 8))
        say("look2: tcells %d tn %d tany %d tw %s" % (rw("br_tcells"), rb("br_tn"), rb("br_tany"), bytes(m.readseg(pseg, sy["br_tw"], 4)).hex()))
        if os.environ.get("BRFMENU"):        # LOOK: a menu title pressed
            mx, my = (int(v) for v in os.environ["BRFMENU"].split(","))
            subprocess.run(["python3", "tools/mouse.py", SOCK, "to", str(mx),
                            str(my)], check=True, capture_output=True)
            os88qemu.pace(m, 0.5)
            subprocess.run(["python3", "tools/mouse.py", SOCK, "down"],
                           check=True, capture_output=True)
            os88qemu.pace(m, 1.0)
            it = os.environ.get("BRFITEM")
            if it:
                ix, iy = (int(v) for v in it.split(","))
                subprocess.run(["python3", "tools/mouse.py", SOCK, "to",
                                str(ix), str(iy)], check=True,
                               capture_output=True)
                os88qemu.pace(m, 0.5)
                subprocess.run(["python3", "tools/mouse.py", SOCK, "up"],
                               check=True, capture_output=True)
                os88qemu.pace(m, 6.0)
                say("after item: opts %04x itab0 %d" % (rw("br_opts"),
                    m.readseg(pseg, sy["br_itab"], 1)[0]))
        subprocess.run(["python3", "tools/shot.py", SOCK, os.path.join(os88qemu.RUNDIR, "brlook.png")],
                       check=True, capture_output=True)
        m.quit()
        return 0
    if rw("br_fnseg") == 0:
        fails.append("part 8 is not in ([br_fnseg] 0)")
    if rb("br_fface") != 1:
        fails.append("the Times face did not open ([br_fface] %d)"
                     % rb("br_fface"))
    nl = rw("br_nlines")
    tab = bytes(m.readseg(rw("br_lseg"), 0, nl * LN_SIZE))
    tall = []
    for i in range(nl):
        r = tab[i * LN_SIZE:(i + 1) * LN_SIZE]
        say(i, r.hex())
        if r[8] & 0x80 and not r[9] & LNF_IROW:
            nxt = tab[(i + 1) * LN_SIZE:(i + 2) * LN_SIZE]
            if len(nxt) < LN_SIZE or not nxt[9] & LNF_IROW:
                fails.append("line %d is tall with no continuation row" % i)
            tall.append((i, "x2" if r[9] & LNF_UNDER else
                         "face" if r[9] & 32 else "cell"))
    if [k for _, k in tall] != ["x2", "face", "cell", "cell"]:
        fails.append("tall lines %r: the doubled h1, the face's h2, and the "
                     "body's two lines in the face expected" % (tall,))

    shot = os.path.join(os88qemu.RUNDIR, "brfont.png")
    subprocess.run(["python3", "tools/shot.py", SOCK, shot], check=True,
                   capture_output=True)
    im = Image.open(shot).convert("RGB")
    im.save(os.path.join(os88qemu.RUNDIR, "brfont-F1.png"))
    x1, y1 = rw("br_cx"), rw("br_cy")
    x2 = rw("br_sbx") - 1
    ink = (0, 0, 0)
    red_ok = True                                       # noqa: F841

    def inkrows(row0, nrows):
        ys = set()
        for y in range(y1 + row0 * 8, y1 + (row0 + nrows) * 8):
            for x in range(x1, x2):
                if im.getpixel((x, y)) != (255, 255, 255):
                    ys.add(y)
                    break
        return len(ys)
    if len(tall) == 4:
        top = rw("br_top")
        h1 = inkrows(tall[0][0] - top, 2)
        if rb("br_bpp") != 1:
            ry = y1 + (tall[0][0] - top) * 8
            blue = sum(1 for y in range(ry, ry + 16) for x in range(x1, x2)
                       if im.getpixel((x, y)) == (0, 0, 168))
            say("h1 link: %d blue pixels (the theme's accent)" % blue)
            if blue < 50:
                fails.append("the h1 link has %d blue pixels: links are in "
                             "the theme's accent" % blue)
        h2 = inkrows(tall[1][0] - top, 2)
        body = inkrows(nl - 1 - top, 1)
        big = inkrows(tall[2][0] - top, 2)
        say("ink rows: <big> line %d" % big)
        if big <= 8:
            fails.append("the line with <big> spans %d rows, more than 8 "
                         "expected" % big)
        say("ink rows: h1 %d, h2 %d, body %d" % (h1, h2, body))
        if h1 <= 12:
            fails.append("h1's ink spans %d rows, more than 12 expected" % h1)
        if h2 < 8:
            fails.append("h2's ink spans %d rows, 8 or more expected" % h2)
        if body > 8 or body == 0:
            fails.append("the body line's ink spans %d rows, the cell's 8 "
                         "expected" % body)
        if rb("br_bpp") != 1:
            red = 0
            ry = y1 + (tall[3][0] - top) * 8
            for y in range(ry, ry + 16):
                for x in range(x1, x2):
                    if im.getpixel((x, y)) == (168, 0, 0):
                        red += 1
            say("red pixels in the <font face color> line: %d" % red)
            if red < 30:
                fails.append("the red face line has %d red pixels" % red)
        # THE LINK IN THE TALL h1: the bottom half of its SECOND letter,
        # cells 2-3 of a doubled line, which an 8x8 hit map would put past
        # the text (71.35's br_thit)
        k0 = len(asked)
        hx = x1 + BRT_X2ADV + 8
        hy = y1 + (tall[0][0] - top) * 8 + 12
        mo.click(hx, hy)
        try:
            os88qemu.acted(m, lambda: "/L.HTM" in asked[k0:], secs=30,
                           what="the h1 link followed")
        except Exception:
            fails.append("a click on h1's second letter did not follow its "
                         "link")
        # ...AND BACK: the link is now followed, so it is in the text's
        # colour and not the theme's accent (71.34's visited links)
        if rb("br_bpp") != 1:
            os88qemu.acted(m, lambda: rb("br_nstate") in (6, 7), secs=60,
                           what="L laid out")
            k0 = len(asked)
            mo.click(lx2 - 6, ly1 + 7)
            os88qemu.pace(m, 1.0)
            n = rw("br_loc", 12)
            subprocess.run(["python3", "tools/qmp.py", SOCK]
                           + ["sendkey backspace", "sleep 0.03"] * n,
                           check=True, capture_output=True)
            type_url(url)
            subprocess.run(["python3", "tools/qmp.py", SOCK, "sendkey ret"],
                           check=True, capture_output=True)
            os88qemu.acted(m, lambda: "/F1.HTM" in asked[k0:], secs=60,
                           what="F1 again")
            os88qemu.acted(m, lambda: rb("br_nstate") in (6, 7), secs=60,
                           what="F1 laid out again")
            os88qemu.pace(m, 2.0)
            subprocess.run(["python3", "tools/shot.py", SOCK, shot],
                           check=True, capture_output=True)
            im = Image.open(shot).convert("RGB")
            ry = y1 + (tall[0][0] - top) * 8
            blue = sum(1 for y in range(ry, ry + 16) for x in range(x1, x2)
                       if im.getpixel((x, y)) == (0, 0, 168))
            say("h1 link once followed: %d blue pixels" % blue)
    m.quit()
    for f in fails:
        say("FAIL " + f)
    say("brfont: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
