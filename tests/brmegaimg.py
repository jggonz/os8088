#!/usr/bin/env python3
"""The Browser's pictures under kern_mega, on a real 286 (SPEC.md 71.26 to
71.32): every format's copy byte for byte its reference, every package
claim a SELECTOR.

    make && make mega && python3 tests/brmegaimg.py

86Box's vm/at286-port (the owner's mr286: MR BIOS, 286 at 16 MHz, 4 MB) with
an NE2000 on slirp (`_ne2k`), A: build/mega.img's copy with a SYSTEM.CFG
wanting ETHER.DRV (tests/brmega.py's bit), B: a disk of BROWSER.O88 alone.
The page is served from here: a GIF, an animated GIF, a PNG (RGB, the cube),
a JPEG (decoded at its scale), a 24-bit BMP (B, G, R), a TIFF in LZW, a PCX
(by its URL's extension) and an XBM - one picture of each decoder part, so
part 1 and parts 2 to 6 are each fetched, hosted and dropped under protected
mode, their segments being selectors (op_seg's, SPEC.md 20.12). Every copy is
read out of the store by its claim's LDT descriptor and held to
tests/brgifref.py's; the animation must be playing.

Why a 286 and not Bochs: a 286 leaves protected mode by a CPU RESET
(KERN-MEGA-PLAN 14.64), on every reflected BIOS call - the floppy, the
display mode - and a 386's CR0 does not; the picture's fetch, decode and
spill are reflected calls among the rest.
"""
import os
import socket
import subprocess
import sys
import tempfile
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
os.environ.setdefault("OS88_MEGA", "1")
os.environ.setdefault("OS88_MEGA_EMU", "86box")
import dispcp                                          # noqa: E402
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88sym                                         # noqa: E402
import pixcorpus as pc                                 # noqa: E402
import brgifref                                        # noqa: E402
from brclick import browser_syms                       # noqa: E402
from brgifems import anim_gif                          # noqa: E402

S = os88sym.linear
MACHINE = "at286_vga_ne2k"
ETHER_BIT = 0x10                    # kernel/driver.inc drv_cfgbit: ETHER.DRV
IE_ANIM, IE_AFRAME = 16, 17


def say(*a):
    print(*a)
    sys.stdout.flush()


XBM = (b"#define face_width 19\n#define face_height 7\n"
       b"static char face_bits[] = {\n"
       b"   0xff, 0x07, 0x04, 0x01, 0xa0, 0x02, 0x55, 0x55, 0x05,\n"
       b"   0x0f, 0xf0, 0x03, 0x01, 0x00, 0x04, 0xF0, 0x0F, 0x00,\n"
       b"   0x81, 0x42, 0x01 };\n")


def pictures():
    """[(name, ext, data, attrs)], one a decoder part"""
    rgb = pc.rgb_rows(30, 20)
    i16 = pc.idx_rows(24, 16, 16)
    gif = pc.gif(24, 16, i16, gpal=pc.pal_n(16))
    bpl = (30 + 1) & ~1
    return [
        ("GIF", "GIF", gif, "width=48"),
        ("ANIM", "GIF", anim_gif(), "width=64 height=32"),
        ("PNG", "PNG", pc.png(30, 20, 2, 8, rgb), "width=60"),
        ("JPG", "JPG", open(os.path.join(ROOT, "tests", "pixel", "J420.JPG"),
                            "rb").read(), "width=40"),
        ("BMP", "BMP", pc.bmp(30, 20, 24, rgb), ""),
        ("TIF", "TIF", pc.tiff(30, 20, 2, 8, pc.t_rgb(rgb), spp=3, comp=5,
                               rps=4), ""),
        ("PCX", "PCX", pc.pcx(30, 20, 8, 3, pc.pcx_lines_rgb(rgb, 30, bpl)),
         ""),
        ("XBM", "XBM", XBM, "width=38"),
    ]


class Server(threading.Thread):
    def __init__(self, files):
        threading.Thread.__init__(self, daemon=True)
        self.files = files
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.s.bind(("127.0.0.1", 0))
        self.port = self.s.getsockname()[1]
        self.s.listen(4)
        self.asked = {}

    def run(self):
        while True:
            try:
                c, _ = self.s.accept()
            except OSError:
                return
            c.settimeout(120)
            try:
                req = b""
                while b"\r\n\r\n" not in req and len(req) < 4096:
                    b = c.recv(1024)
                    if not b:
                        break
                    req += b
                path = req.split(b" ")[1].decode("latin-1").upper() \
                    if b" " in req else "/"
                self.asked[path] = self.asked.get(path, 0) + 1
                body = self.files.get(path)
                if body is None:
                    c.sendall(b"HTTP/1.0 404 Not Found\r\n"
                              b"Content-Length: 0\r\n\r\n")
                else:
                    c.sendall(b"HTTP/1.0 200 OK\r\nContent-Length: %d\r\n\r\n"
                              % len(body) + body)
                c.shutdown(socket.SHUT_WR)
                while c.recv(4096):
                    pass
            except OSError as e:
                say("server: %s" % e)
            finally:
                c.close()


def main():
    shot = sys.argv[sys.argv.index("--shot") + 1] \
        if "--shot" in sys.argv else None    # a PNG of the page, at the end
    pics = pictures()
    page = [b"<html><head><title>Pictures</title></head><body>\r\n"]
    for n, ext, _, attrs in pics:
        page.append(b"<p>%s<br><img src=\"/%s.%s\" %s alt=\"%s\">\r\n"
                    % (n.encode(), n.encode(), ext.encode(), attrs.encode(),
                       n.encode()))
    page.append(b"<p>the end\r\n</body></html>\r\n")
    files = {"/MEGA.HTM": b"".join(page)}
    for n, ext, data, _ in pics:
        files["/%s.%s" % (n, ext)] = data
    for p in ("build/mega.img", "build/browser.o88"):
        if not os.path.exists(os.path.join(ROOT, p)):
            sys.exit("brmegaimg: %s is missing - make && make mega" % p)
    tmp = tempfile.mkdtemp(prefix="brmegaimg-")
    # A: mega.img's copy with SYSTEM.CFG wanting ETHER.DRV (the disk a row
    # makes goes in as it is: os88megacommon.mega_images, OS88_MEGA_A)
    sysimg = os.path.join(tmp, "a.img")
    with open(os.path.join(ROOT, "build", "mega.img"), "rb") as fi, \
            open(sysimg, "wb") as fo:
        fo.write(fi.read())
    cfg = os.path.join(tmp, "SYSTEM.CFG")
    with open(cfg, "wb") as fh:
        fh.write(b"O88CFG\0\0" + (3).to_bytes(2, "little") + b"DW"
                 + bytes([1, 2]) + ETHER_BIT.to_bytes(2, "little") + b"\0\0")
    subprocess.check_call(["mcopy", "-o", "-i", sysimg, cfg, "::SYSTEM.CFG"])
    os.environ["OS88_MEGA_A"] = sysimg
    bimg = os.path.join(tmp, "b.img")
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools",
                                                        "os88disk.py"),
                           "-o", bimg, "--size", "1440", "--folder",
                           "SYSTEM/APPDATA",
                           os.path.join(ROOT, "build", "browser.o88")],
                          stdout=subprocess.DEVNULL)
    srv = Server(files)
    srv.start()
    url = "http://10.0.2.2:%d/MEGA.HTM" % srv.port
    say("http server on %s, %d pictures" % (url, len(pics)))
    sy = browser_syms()
    fails = []
    with os88marty.launch(sysimg, apps=bimg, machine=MACHINE,
                          boot=False) as m:
        m.run()
        os88marty.kernel_up(m)
        os88marty.no_saver(m)
        mo = os88mouse.Mouse(marty=m)
        settle = lambda mm, **k: os88marty.ui_done(mm)       # noqa: E731
        dispcp.open_drive(m, mo, S, settle, "B")
        wins = dispcp.win_list(m, S)
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, mo, S, settle, wx, wy, "BROWSER.O88")
        wins2 = dispcp.win_list(m, S)
        if len(wins2) <= len(wins):
            sys.exit("brmegaimg: BROWSER.O88 did not open a window")
        bw = wins2[-1]
        rec = m.read(S("wm_wins") + bw * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        pseg = rec[dispcp.W_SEG] | (rec[dispcp.W_SEG + 1] << 8)
        say("browser at selector %04X" % pseg)

        def rb(name, off=0):
            return m.readseg(pseg, sy[name] + off, 1)[0]

        def rw(name, off=0):
            d = m.readseg(pseg, sy[name] + off, 2)
            return d[0] | (d[1] << 8)

        BR_LPAD, BR_TBH, BR_TBG, BR_LBAR = 3, 13, 1, 15
        bx0, by0, bww, _ = dispcp.win_rect(m, S, bw)
        mo.click(bx0 + bww - 40, by0 + dispcp.TITLE_H + 1 + BR_LPAD + BR_TBH
                 + BR_TBG + BR_LBAR // 2)
        os88marty.settle(m)
        for ch in "\b" * 12 + url:      # one key at a time (see brems.py)
            os88marty.text_taken(m, ch, ui=False)
        os88marty.settle(m)
        m.key("Enter")
        os88marty.until(m, lambda _m: rb("br_nstate") in (6, 7),
                        "the page to settle", limit=240)
        say("page settled: state %d, %d pictures"
            % (rb("br_nstate"), rw("br_imgn")))

        def table():
            return [m.readseg(pseg, sy["br_itab"] + 16 * k, 16)
                    for k in range(len(pics))]

        def engine_done(_m):
            return (rb("br_ie") in (0, IE_ANIM, IE_AFRAME) and
                    all(e[0] != 0 for e in table()))
        os88marty.until(m, engine_done, "every picture decoded", limit=1800)
        os88marty.guest_sleep(m, 2.0)       # (the animation never settles)
        tab = table()
        mono = rb("br_bpp") == 1
        store = rw("br_istseg")
        say("states %s, engine %d, store selector %04X"
            % ("".join(str(e[0]) for e in tab), rb("br_ie"), store))
        for k, (n, ext, data, attrs) in enumerate(pics):
            e = tab[k]
            a = brgifref.parse_attrs(attrs)
            if n == "ANIM":
                if e[0] != 1:
                    fails.append("ANIM: state %d, in the store (1) expected"
                                 % e[0])
                continue
            if ext == "GIF":
                want = brgifref.expected(data, a.get("width", 0),
                                         a.get("height", 0),
                                         a.get("border", 0), mono)
            else:
                want = brgifref.expected_px(data, a.get("width", 0),
                                            a.get("height", 0),
                                            a.get("border", 0), mono, ext)
            if e[0] != 1:
                fails.append("%s: state %d, ready (1) expected" % (n, e[0]))
                continue
            if want is None:
                say("%-5s ready (no reference for it here)" % n)
                continue
            wc, wr, wb = want
            if (e[14], e[15]) != (wc, wr):
                fails.append("%s: box %dx%d, %dx%d expected"
                             % (n, e[14], e[15], wc, wr))
                continue
            para = e[12] | (e[13] << 8)
            got = bytes(m.readseg(store, para * 16, len(wb)))
            if got != wb:
                diff = sum(1 for i in range(len(wb)) if got[i] != wb[i])
                fails.append("%s: %d of %d bytes differ" % (n, diff, len(wb)))
            else:
                say("%-5s %2dx%-3d %5d bytes, identical" % (n, wc, wr,
                                                          len(wb)))
        if rb("br_ie") not in (IE_ANIM, IE_AFRAME):
            fails.append("the animation is not playing (engine %d)"
                         % rb("br_ie"))
        else:
            f0 = rw("br_iframes")
            os88marty.guest_sleep(m, 3.0)
            f1 = rw("br_iframes")
            say("ANIM plays: %d frames in 3 s" % (f1 - f0))
            if f1 == f0:
                fails.append("the animation drew no frame in 3 s")
        if shot:
            w, h, d = m.fbuf(None)
            os88marty.write_png_rgb(shot, w, h, d)
            say("shot %s" % shot)
        for n, ext, _, _ in pics:
            k = srv.asked.get("/%s.%s" % (n, ext), 0)
            if k != 1:
                fails.append("%s.%s asked for %d times, not once"
                             % (n, ext, k))
    for f in fails:
        say("FAIL " + f)
    say("brmegaimg: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
