#!/usr/bin/env python3
"""The KEPT PAGES of the image cache (SPEC.md 71.28), on a hard disk.

    make && make browsertest ethertest brkeeptest && python3 tests/brkeep.py

The Browser keeps a page's pictures in SYSTEM\\APPDATA\\BRCACHE only on a
volume OSAPI_VOL_KIND calls fixed, so this machine BOOTS ITS HARD DISK
(`make test TESTHD=`, tests/ftpkeep.py's shape): build/brkeep.img carries the
system, a SYSTEM.CFG that asks for ETHER.DRV, BROWSER.O88 and the APPDATA
folder, and the Browser is opened from C:. QEMU, because MartyPC has no NIC.
The server is this file's: tests/brgifs.py's GIFs that show, each answered
with a Last-Modified and an ETag, and 304 to a request that names either.

FIVE LEGS, one machine:

1. KEPT. The page's pictures come in (from memory: the machine has plenty),
   [br_ickeep] is set, and the cache file on C: holds an index whose slot
   for each picture - found by the request's hash, computed here from the
   bytes the server received - names a copy that is tests/brgifref.py's,
   byte for byte. Read off the image file on the host, by a FAT reader that
   is not the guest's.
2. THE LOOP. A second page, and the first page's file is in br_icring.
3. BACK (trusted): not one picture is asked for; every entry is IS_DISK (4);
   and the window's content is the same pixels as on the first visit, so
   every slice drawn from the file is right where it matters.
4. RELOAD (asked again): every picture is asked for with If-None-Match and
   If-Modified-Since, every answer is 304, every entry is IS_DISK, and the
   pixels are the first visit's again.
5. CLOSE: the Browser's files are gone from BRCACHE.
"""
import os
import socket
import struct
import subprocess
import sys
import tempfile
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
import brgifs                                          # noqa: E402
import brgifref                                        # noqa: E402
from brclick import browser_syms                       # noqa: E402

PORT = 8094
HD = "build/brkeep.img"
LM = b"Tue, 10 Nov 2026 12:00:00 GMT"
IS_READY, IS_DISK = 1, 4
IXH, S_SIZE, SLOTS = 64, 80, 32


def say(*a):
    print(*a)
    sys.stdout.flush()


def bri_hash(data, port):
    """The part's bri_hash and bri_kfind's port: rotated 5, the byte xored in."""
    h = 0
    for b in data:
        for _ in range(5):
            h = ((h << 1) | (h >> 31)) & 0xFFFFFFFF
        h ^= b
    lo = (h & 0xFFFF) ^ port
    if lo == 0:
        lo = 1
    return (h & 0xFFFF0000) | lo


class Server(threading.Thread):
    def __init__(self, gifs):
        threading.Thread.__init__(self, daemon=True)
        self.gifs = gifs                # NAME -> bytes
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        os88qemu.lease_port(PORT)
        self.s.bind(("0.0.0.0", PORT))
        self.s.listen(4)
        self.log = []                   # (path, status, request bytes)
        names = sorted(gifs)
        self.k1 = b"<html><head><title>Kept</title></head><body>\r\n"
        for n in names:
            attrs = dict(brgifs.cases())[n][1]
            self.k1 += (b"<p>%s<br><img src=\"/%s.GIF\" %s alt=\"%s\">\r\n"
                        % (n.encode(), n.encode(), attrs.encode(), n.encode()))
        self.k1 += b"<p>the end</body></html>\r\n"
        self.k2 = (b"<html><head><title>Other</title></head><body>"
                   b"<h1>Another page</h1></body></html>\r\n")

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
                if name.endswith(".GIF") and name[:-4] in self.gifs:
                    tag = b'"e-' + name[:-4].encode() + b'"'
                    low = req.lower()
                    if (b"if-none-match: " + tag.lower() in low
                            or b"if-modified-since: " + LM.lower() in low):
                        status, body, extra = b"304 Not Modified", b"", b""
                    else:
                        status, body = b"200 OK", self.gifs[name[:-4]]
                        extra = b"Content-Type: image/gif\r\n"
                    extra += b"Last-Modified: " + LM + b"\r\nETag: " + tag \
                        + b"\r\n"
                elif path.lower().startswith("/k2"):
                    status, body = b"200 OK", self.k2
                    extra = b"Content-Type: text/html\r\n"
                else:
                    status, body = b"200 OK", self.k1
                    extra = b"Content-Type: text/html\r\n"
                self.log.append((path, int(status[:3]), req))
                c.sendall(b"HTTP/1.0 " + status + b"\r\n" + extra
                          + b"Content-Length: %d\r\n\r\n" % len(body) + body)
                c.shutdown(socket.SHUT_WR)
                while c.recv(4096):
                    pass
            except OSError:
                pass
            finally:
                c.close()


def hd_files(img, path):
    """{NAME: bytes} of the folder PATH (a list of names) on the image's
    first partition: a FAT reader that is not the guest's."""
    d = open(img, "rb").read()
    lba0 = struct.unpack_from("<I", d, 446 + 8)[0]
    b = d[lba0 * 512:]
    bps, spc, rsv, nfat, nroot = struct.unpack_from("<HBHBH", b, 11)
    tot16, _, fatsz = struct.unpack_from("<HBH", b, 19)
    tot = tot16 or struct.unpack_from("<I", b, 32)[0]
    rootoff = (rsv + nfat * fatsz) * bps
    dataoff = rootoff + nroot * 32
    nclus = (tot - (rsv + nfat * fatsz) - nroot * 32 // bps) // spc
    fat16 = nclus >= 4085
    fat = b[rsv * bps:(rsv + fatsz) * bps]
    csz = spc * bps

    def nxt(c):
        if fat16:
            return struct.unpack_from("<H", fat, c * 2)[0]
        v = struct.unpack_from("<H", fat, c * 3 // 2)[0]
        return (v >> 4) if c & 1 else (v & 0xFFF)
    eoc = 0xFFF8 if fat16 else 0xFF8

    def chain(c, size=None):
        out = bytearray()
        while 2 <= c < eoc and (size is None or len(out) < size):
            o = dataoff + (c - 2) * csz
            out += b[o:o + csz]
            c = nxt(c)
        return bytes(out if size is None else out[:size])

    def entries(raw):
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if e[0] == 0:
                return
            if e[0] != 0xE5 and not e[11] & 0x08:
                yield e
    raw = b[rootoff:rootoff + nroot * 32]
    for name in path:
        want = name.ljust(11).encode()
        for e in entries(raw):
            if e[:11] == want and e[11] & 0x10:
                raw = chain(struct.unpack_from("<H", e, 26)[0])
                break
        else:
            return None
    out = {}
    for e in entries(raw):
        if e[11] & 0x10:
            continue
        nm = e[:8].decode("latin-1").rstrip() + "." + \
            e[8:11].decode("latin-1").rstrip()
        out[nm] = chain(struct.unpack_from("<H", e, 26)[0],
                        struct.unpack_from("<I", e, 28)[0])
    return out


def main():
    fails = []
    shows = {n: c for n, c in brgifs.cases().items() if c[2] and c[0]}
    gifs = {n: c[0] for n, c in shows.items()}
    sy = browser_syms()
    os88fixture.need("build/ether360.img", "build/brtest360.img", HD)
    srv = Server(gifs)
    srv.start()
    k1 = "http://10.0.2.2:%d/k1.htm" % PORT
    k2 = "http://10.0.2.2:%d/k2.htm" % PORT
    say("%d pictures on :%d" % (len(gifs), PORT))
    hdimg = os88qemu.scratch(HD, "brkeep-hd.img")
    inst = os88qemu.make_test("ETHER=1", "TESTHD=" + hdimg,
                              "TESTIMG=build/ether360.img",
                              "TESTAPPS=build/brtest360.img")
    m = Qemu()
    esy = ether_syms()
    os88qemu.acted(m, lambda: u16(m.read(S("drv_tab") + 2 * 16 + 2, 2)) != 0,
                   secs=60, what="ETHER.DRV's drv_tab row", poll=0.4)
    eseg = u16(m.read(S("drv_tab") + 2 * 16 + 2, 2))
    os88qemu.acted(m, lambda: m.readseg(eseg, esy["dhcp_st"], 1)[0] == 3,
                   secs=30, what="DHCP bound", poll=0.4)
    dispcp.open_drive(m, Mouse(), S, settle, "C")
    wins = dispcp.win_list(m, S)
    wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
    dispcp.open_named(m, Mouse(), S, settle, wx, wy, "BROWSER.O88")
    bw = dispcp.win_list(m, S)[-1]
    rec = m.read(S("wm_wins") + bw * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
    pseg = u16(rec[22:24])
    say("browser at %04X, from C:" % pseg)

    def rb(n, off=0):
        return m.readseg(pseg, sy[n] + off, 1)[0]

    def rw(n, off=0):
        return u16(m.readseg(pseg, sy[n] + off, 2))

    def table():
        return [bytes(m.readseg(pseg, sy["br_itab"] + 16 * k, 16))
                for k in range(len(gifs))]

    def wait_page(what):
        os88qemu.pace(m, 0.5)
        os88qemu.acted(m, lambda: rb("br_nstate") in (6, 7), secs=60,
                       what=what, poll=0.5)
        os88qemu.acted(m, lambda: rb("br_ie") == 0
                       and all(e[0] != 0 for e in table()),
                       secs=120, what=what + ": its pictures", poll=0.5)
        os88qemu.pace(m, 1.0)

    def fetch(url):
        lx2 = rw("br_loc", 4)
        ly1 = rw("br_loc", 2)
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

    def toolbar(which):
        r = {"back": "br_r1", "rel": "br_r3"}[which]
        x1, y1, x2, y2 = (rw(r, i) for i in (0, 2, 4, 6))
        Mouse().click((x1 + x2) // 2, (y1 + y2) // 2)

    def content():
        """The page's pixels, off the card's raster (brnav.py's way)."""
        x1, y1 = rw("br_cx"), rw("br_cy")
        x2, y2 = x1 + rw("br_cols") * 8, y1 + rw("br_rows") * 8
        p = os.path.join(tempfile.gettempdir(), "brkeep.ppm")
        subprocess.run(["python3", "tools/qmp.py", SOCK, "screendump %s" % p],
                       check=True, capture_output=True)
        raw = open(p, "rb").read()
        parts = raw.split(b"\n", 3)
        w, h = (int(v) for v in parts[1].split())
        out = []
        for y in range(y1, min(y2, h)):
            o = (y * w + x1) * 3
            out.append(parts[3][o:o + (x2 - x1) * 3])
        return b"".join(out)

    def gifreqs(since):
        return [x for x in srv.log[since:] if x[0].upper().endswith(".GIF")]

    # --- 1. kept ---------------------------------------------------------------
    fetch(k1)
    wait_page("the first visit")
    tab = table()
    sts = [e[0] for e in tab]
    say("first visit: states %s, keep %d, cache state %d, file %s"
        % (sts, rb("br_ickeep"), rb("br_icstate"),
           bytes(m.readseg(pseg, sy["br_icname"], 12)).decode("latin-1")))
    if rb("br_ickeep") != 1:
        fails.append("C: is a fixed disk and [br_ickeep] is %d"
                     % rb("br_ickeep"))
    if any(s != IS_READY for s in sts):
        fails.append("not every picture is in: %s" % sts)
    first = content()
    reqs1 = gifreqs(0)
    names = sorted(gifs)
    want = {}
    for k, n in enumerate(names):
        at = brgifref.parse_attrs(shows[n][1])
        want[n] = brgifref.expected(gifs[n], at.get("width", 0),
                                    at.get("height", 0), at.get("border", 0),
                                    rb("br_bpp") == 1)
    fname = bytes(m.readseg(pseg, sy["br_icname"], 12)).decode("latin-1")
    files = hd_files(hdimg, ["SYSTEM", "APPDATA", "BRCACHE"]) or {}
    say("BRCACHE on C: holds %s" % sorted(files))
    body = files.get(fname)
    if body is None:
        fails.append("%s is not in C:'s BRCACHE" % fname)
    else:
        if body[:4] != b"BRC1":
            fails.append("the file does not start with its index (%r)"
                         % body[:4])
        slots = {}
        for i in range(SLOTS):
            o = IXH + i * S_SIZE
            h = struct.unpack_from("<I", body, o)[0]
            if h:
                slots[h] = body[o:o + S_SIZE]
        for path, st, req in reqs1:
            n = os.path.basename(path).upper()[:-4]
            sl = slots.get(bri_hash(req, PORT))
            wc, wr, wb = want[n]
            if sl is None:
                fails.append("%s: no slot for its request's hash" % n)
                continue
            para = struct.unpack_from("<H", sl, 12)[0]
            lm = sl[14:44].split(b"\0")[0]
            et = sl[44:80].split(b"\0")[0]
            if (sl[8], sl[9]) != (wc, wr):
                fails.append("%s: slot says %dx%d, the box is %dx%d"
                             % (n, sl[8], sl[9], wc, wr))
            elif body[para * 16:para * 16 + len(wb)] != wb:
                fails.append("%s: the kept copy differs from the reference"
                             % n)
            elif lm != LM or et != b'"e-' + n.encode() + b'"':
                fails.append("%s: validators %r %r" % (n, lm, et))
            else:
                say("%-8s kept at paragraph %d, identical, with its "
                    "validators" % (n, para))

    # --- 2. the loop -------------------------------------------------------------
    fetch(k2)
    wait_page("the second page")
    if rb("br_icrn") != 1:
        fails.append("after a second page the loop holds %d, not 1"
                     % rb("br_icrn"))

    # --- 3. back: trusted -----------------------------------------------------
    n0 = len(srv.log)
    toolbar("back")
    wait_page("Back")
    tab = table()
    sts = [e[0] for e in tab]
    got = gifreqs(n0)
    say("after Back: states %s, %d picture requests" % (sts, len(got)))
    if got:
        fails.append("Back asked for %d pictures; the kept copies are "
                     "trusted for the session" % len(got))
    if any(s != IS_DISK for s in sts):
        fails.append("after Back not every picture is drawn from the file: "
                     "%s" % sts)
    if content() != first:
        fails.append("after Back the page's pixels differ from the first "
                     "visit's")
    else:
        say("after Back: the page is the first visit's, pixel for pixel")

    # --- 4. reload: asked again ------------------------------------------------
    n0 = len(srv.log)
    toolbar("rel")
    wait_page("Reload")
    tab = table()
    sts = [e[0] for e in tab]
    got = gifreqs(n0)
    say("after Reload: states %s, %d picture requests, statuses %s"
        % (sts, len(got), sorted(set(x[1] for x in got))))
    if len(got) != len(gifs):
        fails.append("Reload asked for %d of %d pictures"
                     % (len(got), len(gifs)))
    for path, st, req in got:
        low = req.lower()
        if b"if-none-match:" not in low or b"if-modified-since:" not in low:
            fails.append("%s was asked for without its validators" % path)
        if st != 304:
            fails.append("%s was answered %d, not 304" % (path, st))
    if any(s != IS_DISK for s in sts):
        fails.append("after Reload not every picture is the kept copy: %s"
                     % sts)
    if content() != first:
        fails.append("after Reload the page's pixels differ")
    else:
        say("after Reload: 304s, and the first visit's pixels")

    # --- 5. close ------------------------------------------------------------
    x, y = dispcp.win_rect(m, S, bw)[:2]
    Mouse().click(x + 8, y + 5)                 # the close box
    os88qemu.acted(m, lambda: bw not in dispcp.win_list(m, S, check=False),
                   secs=20, what="the Browser closed", poll=0.5)
    os88qemu.pace(m, 2.0)
    m.quit()
    inst.wait() if hasattr(inst, "wait") else None
    files = hd_files(hdimg, ["SYSTEM", "APPDATA", "BRCACHE"])
    say("after the close BRCACHE holds %s" % (sorted(files or {})))
    if files:
        fails.append("the close left %s in BRCACHE" % sorted(files))
    for f in fails:
        say("FAIL " + f)
    say("brkeep: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
