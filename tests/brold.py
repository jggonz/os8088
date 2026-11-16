#!/usr/bin/env python3
"""The older formats in the Browser (SPEC.md 71.31): PiXEL's EXTRAS and
SIMPLE parts and the Browser's own XBM part, hosted by part 1 as the PNG and
JPEG parts are, every copy against tests/brgifref.py's expected_px.

    make && make browsertest ethertest && python3 tests/brold.py

QEMU, because MartyPC has no NIC: the page is served from here. On it:

- pxsimp: BMP at 8 bits, 24 (B, G, R: [px_bgr]), RLE8, 1 bit and 16 (the
  masks); PCX at 8 bits with its tail palette and at 24 (three planes); TGA
  at 24 bits and colour-mapped RLE - a PCX and a TGA named by their URL's
  extension, a TGA having no signature;
- pxextra: TIFF RGB in LZW and grey in PackBits, an ICO with its AND mask
  (transparent over white), an ILBM in ByteRun1, MacPaint in MacBinary and a
  plain one named .MAC;
- brxbm: an XBM;
- REFUSED, keeping their alt boxes: a BMP cut short and a TIFF in JPEG
  compression.

LEGS: every picture shown is in state 1 with the box and the bytes
expected_px makes; every refused one is in state 2; each file is asked for
once; and a page of PNG and JPEG afterwards still decodes (a different part
held, then the one before it again).
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


XBM = (b"/* an X bitmap */\n#define face_width 19\n#define face_height 7\n"
       b"static char face_bits[] = {\n"
       b"   0xff, 0x07, 0x04, 0x01, 0xa0, 0x02, 0x55, 0x55, 0x05,\n"
       b"   0x0f, 0xf0, 0x03, 0x01, 0x00, 0x04, 0xF0, 0x0F, 0x00,\n"
       b"   0x81, 0x42, 0x01 };\n")


def pictures():
    """[(name, ext, data, attrs)], in page order"""
    out = []
    p16 = pc.pal_n(16)
    i16 = pc.idx_rows(24, 16, 16)
    rgb = pc.rgb_rows(30, 20)
    out.append(("BMP8", "BMP", pc.bmp(24, 16, 8, i16, pal=p16, used=16), "width=48"))
    out.append(("BMP24", "BMP", pc.bmp(30, 20, 24, rgb), ""))
    out.append(("BMPR8", "BMP", pc.bmp_rle(16, 8, 1, pc.rle8(16, 8), 256),
                ""))
    out.append(("BMP1", "BMP", pc.bmp(33, 9, 1, pc.idx_rows(33, 9, 2),
                                      pal=[(0, 0, 0), (255, 255, 255)]), ""))
    out.append(("BMP16", "BMP", pc.bmp(20, 10, 16, pc.rgb_rows(20, 10)),
                ""))
    bpl = (24 + 1) & ~1
    out.append(("PCX8", "PCX",
                pc.pcx(24, 16, 8, 1, pc.pcx_lines_idx(i16, 24, 8, 1, bpl),
                       tailpal=pc.pal_n(256)), "width=48"))
    bpl = (30 + 1) & ~1
    out.append(("PCX24", "PCX",
                pc.pcx(30, 20, 8, 3, pc.pcx_lines_rgb(rgb, 30, bpl)), ""))
    out.append(("TGA24", "TGA", pc.tga(30, 20, 2, 24, pc.tga_px_rgb(rgb, 24)),
                ""))
    out.append(("TGAR", "TGA", pc.tga(24, 16, 9, 8, pc.tga_px_byte(i16),
                                      cmap=p16), "width=48"))
    out.append(("TIFL", "TIF", pc.tiff(30, 20, 2, 8, pc.t_rgb(rgb), spp=3,
                                       comp=5, rps=4), ""))
    grey = [[(x * 19 + y * 7) & 255 for x in range(24)] for y in range(16)]
    out.append(("TIFP", "TIF", pc.tiff(24, 16, 1, 8, pc.t_grey(grey, 8),
                                       comp=32773, rps=3), ""))
    mask = [[1 if (x - 8) ** 2 + (y - 8) ** 2 > 49 else 0
             for x in range(16)] for y in range(16)]
    out.append(("ICO", "ICO",
                pc.ico([(16, 16, 4, pc.dib(16, 16, 4, pc.idx_rows(16, 16, 16),
                                           pal=p16, mask=mask))]),
                "width=32"))
    out.append(("LBM", "LBM", pc.iff(24, 16, 4, i16, cmap=p16, comp=1), ""))
    out.append(("MACB", "MAC", pc.macpaint(macbin=True), "width=72"))
    out.append(("MACR", "MAC", pc.macpaint(), "width=72"))
    out.append(("XBM", "XBM", XBM, "width=38"))
    cut = pc.bmp(30, 20, 24, rgb)
    out.append(("BMPCUT", "BMP", cut[:len(cut) * 2 // 3], ""))
    out.append(("TIFJ", "TIF", pc.tiff(24, 16, 1, 8, pc.t_grey(grey, 8),
                                       comp=7), ""))
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
                if stem in self.files and name not in self.pages:
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
    page = b"<html><head><title>Older formats</title></head><body>\r\n"
    for n, ext, _, attrs in pics:
        page += (b"<p>%s<br><img src=\"/%s.%s\" %s alt=\"%s\">\r\n"
                 % (n.encode(), n.encode(), ext.encode(), attrs.encode(),
                    n.encode()))
    page += b"</body></html>\r\n"
    rgb = [[((x * 9) & 255, (y * 13) & 255, ((x + y) * 5) & 255)
            for x in range(30)] for y in range(20)]
    after = [("APNG", "PNG", pc.png(30, 20, 2, 8, rgb), "width=60"),
             ("AJPG", "JPG", rd("tests", "pixel", "J420.JPG"), "width=40"),
             ("ABMP", "BMP", pc.bmp(24, 16, 8, pc.idx_rows(24, 16, 16),
                                    pal=pc.pal_n(16), used=16), "")]
    page2 = b"<html><body><p>after\r\n"
    for n, ext, _, attrs in after:
        page2 += (b"<br><img src=\"/%s.%s\" %s>\r\n"
                  % (n.encode(), ext.encode(), attrs.encode()))
    page2 += b"</body></html>\r\n"
    files = {n: d for n, _, d, _ in pics + after}
    sy = browser_syms()
    os88fixture.need("build/ether360.img", "build/brtest360.img")
    srv = Server({"OLD.HTM": page, "AFTER.HTM": page2}, files)
    srv.start()
    apps = os88qemu.scratch("build/brtest360.img", "brold-apps.img")
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

    go("http://10.0.2.2:%d/OLD.HTM" % PORT)
    os88qemu.acted(m, lambda: rw("br_imgn") == len(pics), secs=60,
                   what="the page laid out", poll=0.5)
    os88qemu.acted(m, lambda: settled(len(pics)), secs=600,
                   what="every picture decoded", poll=1.0)
    mono = rb("br_bpp") == 1
    def check(k, n, ext, data, attrs):
        store = rw("br_istseg")
        e = m.readseg(pseg, sy["br_itab"] + 16 * k, 16)
        a = brgifref.parse_attrs(attrs)
        want = brgifref.expected_px(data, a.get("width", 0),
                                    a.get("height", 0), a.get("border", 0),
                                    mono, ext)
        if want is None:
            if e[0] != 2:
                fails.append("%s: state %d, its alt box (2) expected"
                             % (n, e[0]))
            else:
                say("%-6s keeps its alt box, as it should" % n)
            return
        wc, wr, wb = want
        if e[0] != 1:
            fails.append("%s: state %d, ready (1) expected" % (n, e[0]))
            return
        if (e[14], e[15]) != (wc, wr):
            fails.append("%s: box %dx%d, %dx%d expected"
                         % (n, e[14], e[15], wc, wr))
            return
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
    for k, (n, ext, data, attrs) in enumerate(pics):
        check(k, n, ext, data, attrs)
    for n, ext, _, _ in pics:
        k = srv.asked.get(n + "." + ext, 0)
        if k != 1:
            fails.append("%s.%s asked for %d times, not once" % (n, ext, k))
    # --- a page of PNG, JPEG and BMP after: three parts in turn -----------
    go("http://10.0.2.2:%d/AFTER.HTM" % PORT)
    os88qemu.acted(m, lambda: rw("br_imgn") == len(after), secs=60,
                   what="the second page", poll=0.5)
    os88qemu.acted(m, lambda: settled(len(after)), secs=300,
                   what="its pictures decoded", poll=0.5)
    for k, (n, ext, data, attrs) in enumerate(after):
        check(k, n, ext, data, attrs)
    m.quit()
    for f in fails:
        say("FAIL " + f)
    say("brold: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
