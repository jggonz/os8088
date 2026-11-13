#!/usr/bin/env python3
"""Animated GIF in the Browser (SPEC.md 71.29), frame by frame against
tools/pixelsim.py's gif_anim.

    make && make browsertest ethertest && python3 tests/brganim.py

QEMU, because MartyPC has no NIC: the page is served from here. Four
animations, made with tools/pixcorpus.py's encoder, cover disposals 1, 2 and
3, transparent pixels, a local table mapped to frame 0's palette, an
interlaced frame, a box scaled up and across, a loop for ever and one that
plays once. Their delays differ (1.5 to 2.1 s) so that they seldom fall due
together.

THREE LEGS:

1. EVERY COPY IS A FRAME. While they play, the machine is stopped every
   0.4 s, and when the worker is outside its step (`br_ibusy` 0, the engine
   in IE_ANIM or IE_AFRAME) every animated image's copy in the store is read.
   It must be byte for byte one of its frames' copies - gif_anim's master for
   that frame, through the plans and the Bayer screen as tests/brgifref.py
   makes a ready copy - and over the run every frame of each must have been
   seen. A copy that is no frame's (a wrong disposal, a wrong mapping, a
   dirty rect too small) fails at once.
2. IT PLAYS: `br_iframes` grows.
3. A PAGE SCROLLED AWAY STOPS THEM. End takes the pictures off the window;
   once each has tried a frame there, `br_iframes` stands still. Home brings
   them back and it grows again.

And each GIF is asked for ONCE: the animation is set up from the fetch that
drew its first frame (SPEC.md 71.29, Set up as it arrives).
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
from ethernet import (Qemu, u16, S, Mouse, type_url,   # noqa: E402
                      settle, SOCK, ether_syms)
import os88qemu                                        # noqa: E402
import os88fixture                                     # noqa: E402
import pixcorpus as pc                                 # noqa: E402
import pixelsim                                        # noqa: E402
import brgifref                                        # noqa: E402
from brclick import browser_syms                       # noqa: E402

PORT = 8095
IE_ANIM, IE_AFRAME = 16, 17


def say(*a):
    print(*a)
    sys.stdout.flush()


def gifs():
    """name -> (bytes, attributes)"""
    p16 = pc.pal_n(16)
    sw, sh = 24, 16
    bg = pc.ablock(sw, sh, 0)
    out = {}
    out["A1"] = (pc.agif(sw, sh, [pc.gframe(bg, delay=150)] + [
        pc.gframe(pc.ablock(6, 5, k), left=2 + 6 * k, top=2 + 3 * k, disp=1,
                  delay=150) for k in range(3)], gpal=p16, bgi=4, loop=0),
        "width=48 height=32")
    hole = [[3 if (x + y) % 3 == 0 else (x * y) % 16 for x in range(7)]
            for y in range(6)]
    out["A2"] = (pc.agif(sw, sh, [pc.gframe(bg, trans=5, disp=2, delay=170)]
                         + [pc.gframe(hole, left=3 * k, top=2 * k + 1, trans=3,
                                      disp=2, delay=170) for k in range(1, 4)],
                         gpal=p16, bgi=9, loop=0), "")
    out["A3"] = (pc.agif(sw, sh, [pc.gframe(bg, disp=3, delay=190)] + [
        pc.gframe(pc.ablock(8, 6, k), left=4 * k, top=k + 2,
                  disp=3 if k != 2 else 1, trans=7 if k == 3 else None,
                  delay=190) for k in range(1, 4)], gpal=p16, bgi=2, loop=0),
        "width=36")
    near = [(c[0] ^ 3, c[1], min(255, c[2] + 9)) for c in p16[::-1]]
    g4 = p16[:12] + [(200, 10, 10), (200, 10, 10), (10, 20, 30),
                     (10, 20, 32)]
    out["A4"] = (pc.agif(sw, sh, [
        pc.gframe(bg, delay=210),
        pc.gframe(pc.ablock(10, 6, 1), left=1, top=1, lpal=near, disp=1,
                  delay=210),
        pc.gframe(pc.ablock(11, 13, 2), left=5, top=2, ilace=True, trans=0,
                  delay=210)], gpal=g4), "")          # plays once
    return out


def copy_of(data, master, attrs, mono):
    """A ready copy made from a master (sw x sh indices), as brgifref makes
    one from a decode: the plans of frame 0's palette, white for its key."""
    old = pixelsim.BG
    pixelsim.BG = 255
    try:
        p = pixelsim.decode(data, "GIF")
    finally:
        pixelsim.BG = old
    a = brgifref.parse_attrs(attrs)
    W, H = p.w, p.h
    b = a.get("border", 0)
    tw, th, cols, rows = brgifref.size(W, H, a.get("width", 0),
                                       a.get("height", 0), b)
    stride = cols if mono else 4 * cols
    pal = list(p.pal) + [(0, 0, 0)] * (256 - len(p.pal))
    pl = pixelsim.plans(pal)
    t1 = pixelsim.thresh1(pal)
    buf = bytearray([0xFF]) * (stride * rows * 8)

    def put(x, y, c):
        o = y * stride + (x >> 3)
        bit = 0x80 >> (x & 7)
        for q in range(1 if mono else 4):
            if c >> q & 1:
                buf[o + q * cols] |= bit
            else:
                buf[o + q * cols] &= ~bit & 0xFF
    for y in range(th + 2 * b):
        for x in range(tw + 2 * b):
            if x < b or y < b or x >= b + tw or y >= b + th:
                put(x, y, 0)
    for ty in range(th):
        sy = ty * H // th
        brow = pixelsim.BAYER8[(b + ty) & 7]
        for tx in range(tw):
            i = master[sy * W + tx * W // tw]
            x = b + tx
            if mono:
                c = 1 if brow[x & 7] < t1[i] else 0
            else:
                c1, c2, t = pl[i]
                c = c2 if brow[x & 7] < t else c1
            put(x, b + ty, c)
    return bytes(buf)


class Server(threading.Thread):
    def __init__(self, page, files):
        threading.Thread.__init__(self, daemon=True)
        self.page, self.files = page, files
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
                if name.endswith(".GIF") and name[:-4] in self.files:
                    body, ct = self.files[name[:-4]], b"image/gif"
                else:
                    body, ct = self.page, b"text/html"
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
    gs = gifs()
    names = sorted(gs)
    page = b"<html><head><title>Moving</title></head><body>\r\n"
    for n in names:
        page += (b"<p>%s<br><img src=\"/%s.GIF\" %s alt=\"%s\">\r\n"
                 % (n.encode(), n.encode(), gs[n][1].encode(), n.encode()))
    for i in range(150):
        page += b"<p>a line of text, %d, so that the pictures scroll away\r\n" % i
    page += b"</body></html>\r\n"
    sy = browser_syms()
    os88fixture.need("build/ether360.img", "build/brtest360.img")
    srv = Server(page, {n: gs[n][0] for n in names})
    srv.start()
    url = "http://10.0.2.2:%d/anim.htm" % PORT
    apps = os88qemu.scratch("build/brtest360.img", "brganim-apps.img")
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

    lx2, ly1 = rw("br_loc", 4), rw("br_loc", 2)
    Mouse().click(lx2 - 6, ly1 + 7)
    os88qemu.pace(m, 1.0)
    n = rw("br_loc", 12)                # the bar emptied first (brnav.py)
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
    os88qemu.acted(m, lambda: rb("br_ie") in (IE_ANIM, IE_AFRAME), secs=120,
                   what="the animations to start", poll=0.5)
    asked = {n: srv.asked.get(n + ".GIF", 0) for n in names}
    say("asked for: %s" % asked)
    for n in names:
        if asked[n] != 1:
            fails.append("%s.GIF was asked for %d times, not once"
                         % (n, asked[n]))
    mono = rb("br_bpp") == 1
    want = {}
    for k, n in enumerate(names):
        data, attrs = gs[n]
        old = pixelsim.BG
        pixelsim.BG = 255
        try:
            loop, frames = pixelsim.gif_anim(data)
        finally:
            pixelsim.BG = old
        want[k] = [copy_of(data, f["master"], attrs, mono) for f in frames]
        say("%s: %d frames, loop %s" % (n, len(frames), loop))

    # --- 1 and 2: every copy is a frame, every frame is seen -------------------
    seen = {k: set() for k in want}
    bad = set()
    f0 = rw("br_iframes")
    samples = 0
    states = {}
    for _ in range(110):
        os88qemu.pace(m, 0.4)
        m.hmp("stop")
        try:
            st = (rb("br_ie"), rb("br_ibusy"))
            states[st] = states.get(st, 0) + 1
            if rb("br_ibusy") or rb("br_ie") not in (IE_ANIM, IE_AFRAME):
                continue
            store = rw("br_istseg")
            samples += 1
            for k in want:
                e = m.readseg(pseg, sy["br_itab"] + 16 * k, 16)
                if e[0] != 1:
                    if k not in bad:
                        fails.append("%s is in state %d, not ready"
                                     % (names[k], e[0]))
                        bad.add(k)
                    continue
                para = e[12] | (e[13] << 8)
                got = bytes(m.readseg(store + para, 0, len(want[k][0])))
                hit = [i for i, w in enumerate(want[k]) if w == got]
                if not hit and k not in bad:
                    fails.append("%s: a copy that is none of its frames"
                                 % names[k])
                    bad.add(k)
                seen[k].update(hit)
        finally:
            m.hmp("cont")
    f1 = rw("br_iframes")
    say("%d samples, %d frames drawn; states (ie, busy) %s"
        % (samples, f1 - f0, sorted(states.items())))
    for k in want:
        say("%s: frames seen %s of %d" % (names[k], sorted(seen[k]),
                                           len(want[k])))
        if k not in bad and len(seen[k]) < len(want[k]):
            fails.append("%s: frames %s never seen" % (
                names[k], sorted(set(range(len(want[k]))) - seen[k])))
    if f1 - f0 < 10:
        fails.append("only %d frames drawn in 44 s" % (f1 - f0))

    # --- 3: scrolled away, they stop -----------------------------------------
    subprocess.run(["python3", "tools/qmp.py", SOCK, "sendkey esc"],
                   check=True, capture_output=True)
    os88qemu.pace(m, 0.5)
    subprocess.run(["python3", "tools/qmp.py", SOCK, "sendkey end"],
                   check=True, capture_output=True)
    os88qemu.pace(m, 6.0)               # each tries one frame, and pauses
    a = rw("br_iframes")
    os88qemu.pace(m, 8.0)
    b = rw("br_iframes")
    say("scrolled away: %d frames in 8 s" % (b - a))
    if b != a:
        fails.append("scrolled away, %d frames were still drawn" % (b - a))
    subprocess.run(["python3", "tools/qmp.py", SOCK, "sendkey home"],
                   check=True, capture_output=True)
    os88qemu.pace(m, 8.0)
    c = rw("br_iframes")
    say("back on the page: %d frames in 8 s" % (c - b))
    if c - b < 3:
        fails.append("back on the page, only %d frames in 8 s" % (c - b))
    m.quit()
    for f in fails:
        say("FAIL " + f)
    say("brganim: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
