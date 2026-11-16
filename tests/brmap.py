#!/usr/bin/env python3
"""Image maps and Save Image As in the Browser (SPEC.md 71.32).

    make && make browsertest ethertest && python3 tests/brmap.py

QEMU, because MartyPC has no NIC: the pages are served from here, and what
a click did is read off the requests the server got. Each page holds ONE
picture at its very start, so its box's top-left is the band's
([br_cx], [br_cy]) and a point inside the picture is aimed by arithmetic, not
by eye.

- USEMAP (MAP.HTM): a 64x32 picture with a 2-pixel border and a <map> whose
  areas are a rect (0,0)-(31,31), a circle at (48,16) r 10, a triangle and a
  default with no href. A click in each asks for its href; a click the
  default holds asks for nothing; a click on the border asks for nothing.
  The <map> comes AFTER the <img>, as pages write it.
- ISMAP (ISMAP.HTM): <a href=/S.HTM><img ismap></a>: a click asks for
  /S.HTM?x,y with the point in the picture.
- SAVE IMAGE AS (MAP.HTM): a right click on the picture opens the image menu
  (kind 3, the picture's index), and its row hands the picture's own URL to
  the Download Manager, whose Save dialog is then cancelled. On ISMAP.HTM's
  linked picture the link's menu has it as a fourth row.
"""
import os
import socket
import subprocess
import sys
import threading
import time

_OS88_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tools"))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tests"))
sys.path.insert(0, _OS88_ROOT)
import dispcp                                          # noqa: E402
from ethernet import (Qemu, u16, S, Mouse, type_url, settle,  # noqa: E402
                      SOCK, ether_syms)
import os88qemu                                        # noqa: E402
import os88fixture                                     # noqa: E402
import pixcorpus as pc                                 # noqa: E402
from brclick import browser_syms                       # noqa: E402

PORT = 8096
B = 2                                   # MAP.HTM's border


def say(*a):
    print(*a)
    sys.stdout.flush()


MAP = (b"<html><body><img src=\"/A.GIF\" width=64 height=32 border=2 "
       b"usemap=\"#m\" alt=\"map\">\r\n"
       b"<p>below the picture\r\n"
       b"<map name=\"m\">\r\n"
       b"<area shape=\"rect\" coords=\"0,0,31,31\" href=\"/R.HTM\">\r\n"
       b"<area shape=\"circle\" coords=\"48,16,10\" href=\"/C.HTM\">\r\n"
       b"<area shape=\"poly\" coords=\"34,31 63,31 63,24\" href=\"/P.HTM\">\r\n"
       b"<area shape=\"default\" nohref>\r\n"
       b"</map></body></html>\r\n")
ISMAP = (b"<html><body><a href=\"/S.HTM\"><img src=\"/B.GIF\" width=40 "
         b"height=24 border=0 ismap alt=\"ismap\"></a>\r\n"
         b"<p>below</body></html>\r\n")
DONE = b"<html><body><p>a page</body></html>\r\n"


class Server(threading.Thread):
    def __init__(self, pages, files):
        threading.Thread.__init__(self, daemon=True)
        self.pages, self.files = pages, files
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        os88qemu.lease_port(PORT)
        self.s.bind(("0.0.0.0", PORT))
        self.s.listen(4)
        self.log = []

    def run(self):
        while True:
            try:
                c, _ = self.s.accept()
            except OSError:
                return
            c.settimeout(60)
            try:
                req = b""
                while b"\r\n\r\n" not in req and len(req) < 4096:
                    b = c.recv(1024)
                    if not b:
                        break
                    req += b
                path = req.split(b"\r\n")[0].split(b" ")[1].decode("latin-1")
                self.log.append(path)
                name = path.split("?")[0].lstrip("/").upper()
                if name in self.files:
                    body, ct = self.files[name], b"image/gif"
                else:
                    body, ct = self.pages.get(name, DONE), b"text/html"
                c.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: " + ct
                          + b"\r\nContent-Length: %d\r\n\r\n" % len(body)
                          + body)
                c.shutdown(socket.SHUT_WR)
                while c.recv(4096):
                    pass
            except OSError:
                pass
            finally:
                c.close()


def main():
    fails = []
    gif = pc.gif(16, 8, [[(x + y) % 16 for x in range(16)] for y in range(8)],
                 gpal=pc.pal_n(16))
    sy = browser_syms()
    os88fixture.need("build/ether360.img", "build/brtest360.img")
    srv = Server({"MAP.HTM": MAP, "ISMAP.HTM": ISMAP},
                 {"A.GIF": gif, "B.GIF": gif})
    srv.start()
    apps = os88qemu.scratch("build/brtest360.img", "brmap-apps.img")
    os88qemu.make_test("ETHER=1", "TESTIMG=build/ether360.img",
                       "TESTAPPS=" + apps)
    m = Qemu()
    esy = ether_syms()
    os88qemu.acted(m, lambda: u16(m.read(S("drv_tab") + 2 * 16 + 2, 2)) != 0,
                   secs=60, what="ETHER.DRV", poll=0.4)
    eseg = u16(m.read(S("drv_tab") + 2 * 16 + 2, 2))
    os88qemu.acted(m, lambda: m.readseg(eseg, esy["dhcp_st"], 1)[0] == 3,
                   secs=30, what="DHCP bound", poll=0.4)
    mo = Mouse()
    dispcp.open_drive(m, mo, S, settle, "B")
    wins = dispcp.win_list(m, S)
    wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
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
                           + ["sendkey backspace", "sleep 0.05"] * n,
                           check=True, capture_output=True)
            os88qemu.acted(m, lambda: rw("br_loc", 12) == 0, secs=5,
                           what="the bar emptied", poll=0.1)
        type_url(url)
        os88qemu.acted(m, lambda: rw("br_loc", 12) == len(url), secs=5,
                       what="the URL typed", poll=0.1)
        subprocess.run(["python3", "tools/qmp.py", SOCK, "sendkey ret"],
                       check=True, capture_output=True)

    def page(name):
        n = len(srv.log)
        go("http://10.0.2.2:%d/%s" % (PORT, name))
        os88qemu.acted(m, lambda: "/" + name in srv.log[n:], secs=60,
                       what="%s asked for" % name, poll=0.3)
        os88qemu.acted(m, lambda: rw("br_imgn") == 1 and rw("br_nlines") > 0,
                       secs=60, what="%s laid out" % name, poll=0.3)
        os88qemu.acted(m, lambda: m.readseg(pseg, sy["br_itab"], 1)[0] != 0,
                       secs=120, what="its picture settled", poll=0.5)
        os88qemu.pace(m, 1.0)

    def at(x, y, b):
        """The screen point of picture pixel (x, y), border b."""
        return rw("br_cx") + b + x, rw("br_cy") + b + y

    def click(name, x, y, b, want, what):
        page(name)
        n = len(srv.log)
        mo.click(*at(x, y, b))
        if want is None:
            os88qemu.pace(m, 3.0)
            got = [p for p in srv.log[n:] if not p.endswith(".GIF")]
            if got:
                fails.append("%s: asked for %r, nothing expected" % (what, got))
            else:
                say("%-28s nothing asked, as it should" % what)
            return
        try:
            os88qemu.acted(m, lambda: want in srv.log[n:], secs=60,
                           what=want, poll=0.3)
            say("%-28s asked for %s" % (what, want))
        except Exception:                   # noqa: BLE001 - say what came
            fails.append("%s: %r asked, %s expected" % (what, srv.log[n:],
                                                        want))

    click("MAP.HTM", 10, 10, B, "/R.HTM", "usemap: the rect")
    click("MAP.HTM", 50, 18, B, "/C.HTM", "usemap: the circle")
    click("MAP.HTM", 61, 29, B, "/P.HTM", "usemap: the triangle")
    click("MAP.HTM", 40, 2, B, None, "usemap: the default (nohref)")
    click("MAP.HTM", -1, 5, B, None, "usemap: the border")
    click("ISMAP.HTM", 13, 7, 0, "/S.HTM?13,7", "ismap")

    # --- Save Image As: the image menu, then its row ----------------------
    page("MAP.HTM")
    x, y = at(20, 20, B)
    mo.run("rclick", str(x), str(y))
    os88qemu.pace(m, 1.5)
    kind, img = rb("br_ctxkind"), rw("br_ctximg")
    say("the menu: kind %d, image %d, open %d" % (kind, img, rb("br_ctxopen")))
    if "--shot" in sys.argv:                # a PNG of the image menu, open
        shot = sys.argv[sys.argv.index("--shot") + 1]
        subprocess.run(["python3", "tools/shot.py", SOCK, shot], check=True)
        say("shot %s" % shot)
    if (rb("br_ctxopen"), kind, img) != (1, 3, 0):
        fails.append("a right click on the picture opened kind %d image %d "
                     "(open %d), the image menu (3, 0) expected"
                     % (kind, img, rb("br_ctxopen")))
    else:
        mo.click(rw("br_cx") + rw("br_ctxx") + 6,
                 rw("br_cy") + rw("br_ctxy") + 6)
        os88qemu.pace(m, 3.0)
        url = bytes(m.readseg(pseg, sy["br_dl_url"], 80)).split(b"\0")[0]
        want = b"http://10.0.2.2:%d/A.GIF" % PORT
        say("Save Image As: %r" % url)
        if url != want:
            fails.append("Save Image As handed %r to the Download Manager, "
                         "%r expected" % (url, want))
        subprocess.run(["python3", "tools/qmp.py", SOCK, "sendkey esc"],
                       check=True, capture_output=True)
        os88qemu.pace(m, 1.0)

    # --- a linked picture: the link's three rows and Save Image As fourth --
    page("ISMAP.HTM")
    x, y = at(20, 12, 0)
    mo.run("rclick", str(x), str(y))
    os88qemu.pace(m, 1.5)
    kind, img, h = rb("br_ctxkind"), rw("br_ctximg"), rb("br_ctxh")
    say("the linked picture's menu: kind %d, image %d, %d high"
        % (kind, img, h))
    if (rb("br_ctxopen"), kind, img, h) != (1, 1, 0, 52):
        fails.append("a right click on a linked picture opened kind %d image "
                     "%d, %d high: the link menu with a fourth row (1, 0, 52) "
                     "expected" % (kind, img, h))
    else:
        mo.click(rw("br_cx") + rw("br_ctxx") + 6,
                 rw("br_cy") + rw("br_ctxy") + 44)
        os88qemu.pace(m, 3.0)
        url = bytes(m.readseg(pseg, sy["br_dl_url"], 80)).split(b"\0")[0]
        want = b"http://10.0.2.2:%d/B.GIF" % PORT
        say("its fourth row: %r" % url)
        if url != want:
            fails.append("the linked picture's Save Image As handed %r, %r "
                         "expected" % (url, want))
        subprocess.run(["python3", "tools/qmp.py", SOCK, "sendkey esc"],
                       check=True, capture_output=True)
        os88qemu.pace(m, 1.0)
    m.quit()
    for f in fails:
        say("FAIL " + f)
    say("brmap: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
