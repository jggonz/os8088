#!/usr/bin/env python3
"""PNG and JPEG in the Browser (SPEC.md 71.30), through PiXEL's own decoder
parts hosted by part 1, every copy against tests/brgifref.py's expected_px.

    make && make browsertest ethertest && python3 tests/brpx.py

QEMU, because MartyPC has no NIC: the page is served from here. On it:

- PNG: an indexed picture with tRNS (composited over the page's white), a
  1-bit grey, an RGB one with a gradient (the cube), an RGBA one (alpha over
  white), an Adam7 RGB one of odd size (K_SCAT), and BALLOONS.PNG scaled down;
- JPEG: PiXEL's committed fixtures (tests/pixel/) - 4:2:0 scaled up, grey,
  progressive (decoded at 1/4 or below), EXIF orientation 6 (K_COLS), a
  128x96 one drawn 32 wide (the IDCT's 1/4), and LAKE.JPG at 120 wide;
- REFUSED, keeping their alt boxes: an arithmetic-coded JPEG and a PNG cut
  short.

LEGS: every picture shown is in state 1 with the box and the bytes
expected_px makes; every refused one is in state 2; each file is asked for
once; and a second page of GIFs afterwards still decodes (the decoder part
held for the first page did not get in the way).
"""
import os
import socket
import subprocess
import sys
import threading

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
import brgifref                                        # noqa: E402
from brclick import browser_syms                       # noqa: E402

PORT = 8096
IE_OFF = 0


def say(*a):
    print(*a)
    sys.stdout.flush()


def rd(*p):
    with open(os.path.join(_OS88_ROOT, *p), "rb") as f:
        return f.read()


def pictures():
    """[(name, data, attrs)], in page order"""
    out = []
    p16 = pc.pal_n(16)
    idx = [[(x // 3 + y // 2) % 16 for x in range(24)] for y in range(16)]
    trns = bytes([255] * 5 + [0, 128] + [255] * 9)
    out.append(("PIDX", pc.png(24, 16, 3, 8, idx, plte=p16, trns=trns),
                "width=48 height=32"))
    grey = [[(x + y) & 1 for x in range(20)] for y in range(12)]
    out.append(("PGRY", pc.png(20, 12, 0, 1, grey), ""))
    rgb = [[((x * 9) & 255, (y * 13) & 255, ((x + y) * 5) & 255)
            for x in range(30)] for y in range(20)]
    out.append(("PRGB", pc.png(30, 20, 2, 8, rgb), "width=60"))
    rgba = [[(200, (x * 17) & 255, 40, (y * 21) & 255) for x in range(24)]
            for y in range(12)]
    out.append(("PRGBA", pc.png(24, 12, 6, 8, rgba), ""))
    il = [[((x * 11) & 255, (y * 15) & 255, (x * y) & 255)
           for x in range(23)] for y in range(17)]
    out.append(("PIL", pc.png(23, 17, 2, 8, il, ilace=True), "width=46"))
    out.append(("BALL", rd("apps", "pixel", "samples", "BALLOONS.PNG"),
                "width=160"))
    for n, attrs in (("J420", "width=80"), ("JGREY", ""), ("JPROG", ""),
                     ("JO6", "width=32"), ("JBIG", "width=32"),
                     ("JARITH", "")):
        out.append((n, rd("tests", "pixel", n + ".JPG"), attrs))
    out.append(("LAKE", rd("apps", "pixel", "samples", "LAKE.JPG"),
                "width=120"))
    cut = pc.png(30, 20, 2, 8, rgb)
    out.append(("PCUT", cut[:len(cut) * 2 // 3], ""))
    return out


class Server(threading.Thread):
    def __init__(self, pages, files):
        threading.Thread.__init__(self, daemon=True)
        self.pages, self.files = pages, files
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        os88qemu.lease_port(PORT)
        self.s.bind(("0.0.0.0", PORT))
        self.s.listen(4)
        self.asked = {}

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
                name = os.path.basename(path).upper()
                self.asked[name] = self.asked.get(name, 0) + 1
                stem = name.rsplit(".", 1)[0]
                if stem in self.files:
                    body, ct = self.files[stem], b"application/octet-stream"
                else:
                    body, ct = self.pages.get(name, b""), b"text/html"
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
    pics = pictures()
    page = b"<html><head><title>PNG and JPEG</title></head><body>\r\n"
    for n, _, attrs in pics:
        page += (b"<p>%s<br><img src=\"/%s.IMG\" %s alt=\"%s\">\r\n"
                 % (n.encode(), n.encode(), attrs.encode(), n.encode()))
    page += b"</body></html>\r\n"
    gpal = pc.pal_n(16)
    gif = pc.gif(16, 8, [[(x + y) % 16 for x in range(16)] for y in range(8)],
                 gpal=gpal)
    page2 = (b"<html><body><p>after<br><img src=\"/G1.IMG\" width=32>"
             b"</body></html>\r\n")
    files = {n: d for n, d, _ in pics}
    files["G1"] = gif
    sy = browser_syms()
    os88fixture.need("build/ether360.img", "build/brtest360.img")
    srv = Server({"PX.HTM": page, "GIF.HTM": page2}, files)
    srv.start()
    apps = os88qemu.scratch("build/brtest360.img", "brpx-apps.img")
    os88qemu.make_test("ETHER=1", "TESTIMG=build/ether360.img",
                       "TESTAPPS=" + apps)
    m = Qemu()
    esy = ether_syms()
    os88qemu.acted(m, lambda: u16(m.read(S("drv_tab") + 2 * 16 + 2, 2)) != 0,
                   secs=60, what="ETHER.DRV", poll=0.4)
    eseg = u16(m.read(S("drv_tab") + 2 * 16 + 2, 2))
    os88qemu.acted(m, lambda: m.readseg(eseg, esy["dhcp_st"], 1)[0] == 3,
                   secs=30, what="DHCP bound", poll=0.4)
    dispcp.open_drive(m, Mouse(), S, settle, "B")
    wins = dispcp.win_list(m, S)
    wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
    dispcp.open_named(m, Mouse(), S, settle, wx, wy, "BROWSER.O88")
    bw = dispcp.win_list(m, S)[-1]
    rec = m.read(S("wm_wins") + bw * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
    pseg = u16(rec[dispcp.W_SEG:dispcp.W_SEG + 2])

    def rb(n, off=0):
        return m.readseg(pseg, sy[n] + off, 1)[0]

    def rw(n, off=0):
        return u16(m.readseg(pseg, sy[n] + off, 2))

    def go(url):
        lx2, ly1 = rw("br_loc", 4), rw("br_loc", 2)
        Mouse().click(lx2 - 6, ly1 + 7)
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

    def settled(k):
        return (rb("br_ie") == IE_OFF and
                all(m.readseg(pseg, sy["br_itab"] + 16 * i, 1)[0] != 0
                    for i in range(k)))

    go("http://10.0.2.2:%d/PX.HTM" % PORT)
    os88qemu.acted(m, lambda: rw("br_imgn") == len(pics), secs=60,
                   what="the page laid out", poll=0.5)
    os88qemu.acted(m, lambda: settled(len(pics)), secs=600,
                   what="every picture decoded", poll=1.0)
    mono = rb("br_bpp") == 1
    store = rw("br_istseg")
    for k, (n, data, attrs) in enumerate(pics):
        e = m.readseg(pseg, sy["br_itab"] + 16 * k, 16)
        a = brgifref.parse_attrs(attrs)
        want = brgifref.expected_px(data, a.get("width", 0),
                                    a.get("height", 0), a.get("border", 0),
                                    mono)
        if want is None:
            if e[0] != 2:
                fails.append("%s: state %d, its alt box (2) expected"
                             % (n, e[0]))
            else:
                say("%-6s keeps its alt box, as it should" % n)
            continue
        wc, wr, wb = want
        if e[0] != 1:
            fails.append("%s: state %d, ready (1) expected" % (n, e[0]))
            continue
        if (e[14], e[15]) != (wc, wr):
            fails.append("%s: box %dx%d, %dx%d expected"
                         % (n, e[14], e[15], wc, wr))
            continue
        para = e[12] | (e[13] << 8)
        got = bytes(m.readseg(store + para, 0, len(wb)))
        if got != wb:
            diff = sum(1 for i in range(len(wb)) if got[i] != wb[i])
            first = next(i for i in range(len(wb)) if got[i] != wb[i])
            fails.append("%s: %d of %d bytes differ, the first at %d "
                         "(%02X, %02X expected)"
                         % (n, diff, len(wb), first, got[first], wb[first]))
        else:
            say("%-6s %2dx%-3d %5d bytes, identical" % (n, wc, wr, len(wb)))
    for n, _, _ in pics:
        k = srv.asked.get(n + ".IMG", 0)
        if k != 1:
            fails.append("%s.IMG asked for %d times, not once" % (n, k))
    # --- a page of GIFs after, with the decoder part gone with part 1 -----
    go("http://10.0.2.2:%d/GIF.HTM" % PORT)
    os88qemu.acted(m, lambda: rw("br_imgn") == 1, secs=60,
                   what="the second page", poll=0.5)
    os88qemu.acted(m, lambda: settled(1), secs=120,
                   what="its GIF decoded", poll=0.5)
    st = m.readseg(pseg, sy["br_itab"], 1)[0]
    say("the GIF after: state %d" % st)
    if st != 1:
        fails.append("the GIF on the next page is in state %d, not 1" % st)
    m.quit()
    for f in fails:
        say("FAIL " + f)
    say("brpx: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
