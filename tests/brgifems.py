#!/usr/bin/env python3
"""GIF ready copies in EXPANDED MEMORY (SPEC.md 71.26, The EMS tier).

    make && make browsertest etheremstest
    python3 tests/brgifems.py         (86Box: OS88_EMU defaults to it here)

A page of N large GIFs on a small XT: their ready copies outgrow the
conventional reserve, so the UI task moves them to EMS (`bri_spill`) and the
paint maps each slice into the frame (`br_imgmap`). The machine is
86Box's `xt_vga_ne1k_ems_384k` - a 4.77 MHz XT with 384 KB, a VGA, an
NE1000 on slirp and a 2 MB Lo-tech board (tools/os88box86api.py's `_ems`,
`_ne1k`, `_NNNk`), booting `make etheremstest`'s disk, so ETHER.DRV has
bound before the first paint and the page comes from this file's server at
10.0.2.2. 86Box is the one emulator here with a card AND a board.

THREE LEGS:

1. EVERY IMAGE IS SHOWN, some of them from EMS: each entry ready (1, in the
   store) or ready in EMS (3), at least one 3, and the store cut back to the
   copies it still holds.
2. THE COPIES IN THE STORE are byte-identical to tests/brgifref.py's.
3. THE COPIES IN EMS: a breakpoint where br_imgblits has its slice's source
   (`.src`) reads ES:SI - the frame, mapped by br_imgmap - while the page is
   scrolled from its top to its end, and every slice of every EMS copy must
   be the reference's bytes. A slice read through the frame is a slice the
   frame was mapped for, so a stale mapping (the document's pages, or the
   wrong copy's) fails here and not on the glass.

`--machine xt_vga_ne1k_384k` (no board) is the DISK TIER (SPEC.md 71.27,
row `brgifdisk`, with `--n 10`): what the reserve refuses goes to the cache
file in B:\\SYSTEM\\APPDATA\\BRCACHE, leg 3 reads its slices out of the
paint buffer instead of the frame, and

4. THE FILE GOES WITH ITS PAGE: it is on B: (read back on the host,
   os88fat), F5 deletes it, and a file the instance forgot - br_icstate and
   br_iclean zeroed, a crash's state - is swept by the reload's lone start.
   Twelve pictures fill the 360 KB floppy after eight - the last spill
   takes the one copy of two that fits (SPEC.md 71.27's prefix) - and the
   last two keep their alt boxes, which is the 16 KB margin holding. The
   row asks eight and the animation: with nine as well, the machine holds
   two copies and the disk eight, so the animation is let go (SPEC.md 71.29)
   and every still is shown.

Both rows put an ANIMATED GIF third on the page (`--anim-at`, SPEC.md 71.29):
it is fetched once, its copy stays in the store through every spill (state
1, while the stills before and after it go), and it plays - `br_iframes`
grows once the page is in. -1 leaves it out.
"""
import argparse
import os
import re
import socket
import sys
import tempfile
import threading

os.environ.setdefault("OS88_EMU", "86box")   # a card AND a board: 86Box
_OS88_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tools"))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tests"))
import dispcp                                          # noqa: E402
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88sym                                         # noqa: E402
import os88build                                       # noqa: E402
import pixcorpus                                       # noqa: E402
import os88fat                                         # noqa: E402
import brgifref                                        # noqa: E402
from brclick import browser_syms                       # noqa: E402

S = os88sym.linear
MACHINE = "xt_vga_ne1k_ems_384k"
IS_READY, IS_NONE, IS_EMS, IS_DISK = 1, 2, 3, 4
ICS_FILE = 1
IE_ANIM, IE_AFRAME = 16, 17
LZWPAR = 18 * 1024 // 16            # the table IE_AGO keeps past the copies
DW, DH = 256, 128                   # each box: 32 x 16 cells, 16,384 bytes


def say(*a):
    print(*a)
    sys.stdout.flush()


def lst_label(proc, local):
    """PROC's LOCAL label's offset out of build/browser.lst, an %include's
    lines too (brems.py's reader, with the listing's <1> nesting mark
    dropped): a label line has no offset, so it is the next line's."""
    seen, want = False, False
    for line in open(os88build.at("build/browser.lst"), encoding="latin-1"):
        f = [t for t in line.split() if not re.fullmatch(r"<\d+>", t)]
        if not seen:
            seen = len(f) >= 2 and f[-1] == proc + ":"
        elif not want:
            want = len(f) >= 2 and f[1] == local + ":"
        elif len(f) >= 3 and re.fullmatch(r"[0-9A-F]{8}", f[1]):
            return int(f[1], 16)
    raise SystemExit("brgifems: no %s in %s" % (local, proc))


def cache_files(img):
    """The names in B:'s SYSTEM\\APPDATA\\BRCACHE, off the image on the
    host (os88fat reads the root; the walk below it is here). None when
    there is no such folder."""
    f = os88fat.Fat12(img)

    def walk(entries, name):
        for raw in entries:
            if raw[:11] == os88fat.Fat12.raw11(name) and raw[11] & 0x10:
                return raw[26] | (raw[27] << 8)
        return None

    def sub(first):
        body = b"".join(bytes(f.img[f.cluster_off(c):f.cluster_off(c)
                                    + f.spc * f.bps]) for c in f.chain(first))
        out = []
        for i in range(0, len(body), 32):
            e = body[i:i + 32]
            if e[0] == 0:
                break
            if e[0] != 0xE5:
                out.append(e)
        return out
    c = walk([e for _, _, e in f.entries()], "SYSTEM")
    for name in ("APPDATA", "BRCACHE"):
        if c is None:
            return None
        c = walk(sub(c), name)
    if c is None:
        return None
    return sorted(os88fat.Fat12.pretty(e[:11]) for e in sub(c)
                  if not e[11] & 0x18)


def gifs(n):
    """n distinct GIFs, (name, data, attrs, shows): each its own palette
    rotation, so a copy drawn for the wrong entry differs."""
    out = []
    for k in range(n):
        pal = [(((i + 37 * k) * 53) & 255, ((i + 11 * k) * 97 + 40) & 255,
                (255 - (i + 5 * k) * 29) & 255) for i in range(16)]
        rows = [[(x * 16 // 64 + y + k) % 16 for x in range(64)]
                for y in range(32)]
        out.append(("G%02d" % k, pixcorpus.gif(64, 32, rows, gpal=pal),
                    "width=%d height=%d" % (DW, DH), True))
    return out


def anim_gif():
    """A 64 x 32 animation of three frames in a box of half the others (its
    block and copy must fit beside two stills on the 384 KB XT):
    a ground, then a block that steps across it, looping for ever."""
    pal = pixcorpus.pal_n(16)
    bg = [[(x // 8 + y // 8) % 4 for x in range(64)] for y in range(32)]
    frames = [pixcorpus.gframe(bg, delay=20)]
    for k in range(1, 3):
        frames.append(pixcorpus.gframe(pixcorpus.ablock(12, 10, 8 + k),
                                       left=16 * k, top=8 + 4 * k, disp=1,
                                       delay=20))
    return pixcorpus.agif(64, 32, frames, gpal=pal, loop=0)


def page(gs):
    body = [b"<html><head><title>EMS pictures</title></head><body>\r\n"]
    for name, _, attrs, _ in gs:
        body.append(b"<p>%s<br><img src=\"/%s.GIF\" %s alt=\"%s\">\r\n"
                    % (name.encode(), name.encode(), attrs.encode(),
                       name.encode()))
    body.append(b"<p>the end\r\n</body></html>\r\n")
    return b"".join(body)


class Server(threading.Thread):
    def __init__(self, files):
        threading.Thread.__init__(self, daemon=True)
        self.files = files
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.s.bind(("127.0.0.1", 0))
        self.port = self.s.getsockname()[1]
        self.s.listen(4)
        self.served = []

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
                path = req.split(b" ")[1].decode("latin-1") if b" " in req \
                    else "/"
                body = self.files.get(path.upper())
                self.served.append(path)
                if body is None:
                    c.sendall(b"HTTP/1.0 404 Not Found\r\n"
                              b"Content-Length: 0\r\n\r\n")
                else:
                    ct = b"text/html" if path.upper().endswith(".HTM") \
                        else b"image/gif"
                    c.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: " + ct
                              + b"\r\nContent-Length: %d\r\n\r\n" % len(body)
                              + body)
                c.shutdown(socket.SHUT_WR)
                while c.recv(4096):
                    pass
            except OSError as e:
                say("server: %s" % e)
            finally:
                c.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=12, help="how many pictures")
    ap.add_argument("--machine", default=MACHINE,
                    help="xt_vga_ne1k_384k: the control, no board")
    ap.add_argument("--shot", default=None, help="a PNG of the end")
    ap.add_argument("--anim-at", type=int, default=2,
                    help="where the animated GIF goes; -1 none")
    a = ap.parse_args()
    if os.environ.get("OS88_EMU") != "86box":
        sys.exit("brgifems: runs on 86Box (a card and a board), not %s"
                 % os.environ.get("OS88_EMU"))
    ems_m = "_ems" in a.machine
    gs = gifs(a.n)
    ak = a.anim_at if 0 <= a.anim_at <= a.n else -1
    if ak >= 0:
        gs.insert(ak, ("ANIM", anim_gif(),
                       "width=%d height=%d" % (DW // 2, DH // 2), True))
    a.n = len(gs)
    files = {"/IMGEMS.HTM": page(gs)}
    for name, data, _, _ in gs:
        files["/%s.GIF" % name] = data
    fails = []
    sy = browser_syms()
    for f in ("build/etherems360.img", "build/brtest360.img"):
        if not os.path.exists(os88build.at(f)):
            sys.exit("brgifems: %s is missing - make etheremstest "
                     "browsertest" % f)
    if (os.path.getmtime(os88build.at("build/brtest360.img"))
            < os.path.getmtime(os88build.at("build/browser.o88"))):
        sys.exit("brgifems: build/brtest360.img is older than "
                 "build/browser.o88 - make browsertest")
    src_at = lst_label("br_imgblits", ".src")
    srv = Server(files)
    srv.start()
    url = "http://10.0.2.2:%d/IMGEMS.HTM" % srv.port
    say("http server on %s, %d pictures of %dx%d" % (url, a.n, DW, DH))
    tmpd = tempfile.mkdtemp(prefix="brgifems-")
    apps_img = os.path.join(tmpd, "brgifems360.img")
    with open(os88build.at("build/brtest360.img"), "rb") as fi, \
            open(apps_img, "wb") as fo:
        fo.write(fi.read())
    sysdisk = "build/etherems360.img"
    with os88marty.launch(sysdisk, apps=apps_img, machine=a.machine,
                          boot=False) as m:
        m.run()
        os88marty.kernel_up(m)
        os88marty.no_saver(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, lambda mm, **k: os88marty.ui_done(mm), "B")
        wins = dispcp.win_list(m, S)
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, mo, S, lambda mm, **k: os88marty.ui_done(mm),
                          wx, wy, "BROWSER.O88")
        wins2 = dispcp.win_list(m, S)
        if len(wins2) <= len(wins):
            sys.exit("brgifems: BROWSER.O88 did not open a window")
        bw = wins2[-1]
        rec = m.read(S("wm_wins") + bw * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        say("browser at %04X" % pseg)

        def rb(name, off=0):
            return m.readseg(pseg, sy[name] + off, 1)[0]

        def rw(name, off=0):
            d = m.readseg(pseg, sy[name] + off, 2)
            return d[0] | (d[1] << 8)

        ems = m.read(S("drv_tab") + 6 * 16 + 2, 2)
        if ems_m and not (ems[0] | (ems[1] << 8)):
            sys.exit("brgifems: EMS.DRV is not loaded")
        BR_LPAD, BR_TBH, BR_TBG, BR_LBAR = 3, 13, 1, 15
        bx0, by0, bww, bhh = dispcp.win_rect(m, S, bw)
        mo.click(bx0 + bww - 40, by0 + dispcp.TITLE_H + 1 + BR_LPAD + BR_TBH
                 + BR_TBG + BR_LBAR // 2)
        os88marty.settle(m)
        for ch in "\b" * 12 + url:      # one key at a time (see brems.py)
            os88marty.text_taken(m, ch, ui=False)
        os88marty.settle(m)
        m.key("Enter")
        os88marty.until(m, lambda _m: rb("br_nstate") in (6, 7),
                        "the page to settle", limit=240)
        say("page settled; membase %d KB free at load" % rw("br_membase"))

        def table():
            return [m.readseg(pseg, sy["br_itab"] + 16 * k, 16)
                    for k in range(a.n)]

        def engine_done(_m):
            return (rb("br_ie") in (0, IE_ANIM, IE_AFRAME) and
                    all(e[0] != 0 for e in table()))
        os88marty.until(m, engine_done, "every picture fetched", limit=1800)
        os88marty.settle(m)
        tab = table()
        sts = [e[0] for e in tab]
        playing = rb("br_ie") in (IE_ANIM, IE_AFRAME)
        say("states %s; store %d paragraphs, %d of them copies; "
            "EMS arena %d paragraphs used, %d pages held"
            % ("".join(str(s) for s in sts), rw("br_istpar"),
               rw("br_iready"), rw("br_iemsp"),
               rw("br_imgar", 2)))

        # --- 1. shown, some from EMS ---------------------------------------
        if ems_m:
            if any(s not in (IS_READY, IS_EMS) for s in sts):
                fails.append("not every picture is shown: states %s" % sts)
            if IS_EMS not in sts:
                fails.append("no picture went to EMS: the page did not "
                             "outgrow the reserve (raise --n)")
            keep = 64 + (LZWPAR if playing else 0)
            if rw("br_istpar") > rw("br_iready") + keep:
                fails.append("the store kept %d paragraphs for %d of copies"
                             % (rw("br_istpar"), rw("br_iready")))
        else:
            # no board: what the reserve refuses goes to the cache file on
            # B: (SPEC.md 71.27)
            if IS_EMS in sts:
                fails.append("a picture is in EMS on a machine with no board")
            if any(s not in (IS_READY, IS_DISK) for s in sts):
                fails.append("not every picture is shown: states %s" % sts)
            if IS_DISK not in sts:
                fails.append("no picture went to the cache file")
        # --- the animation: asked for once, held in the store, playing ----
        if ak >= 0:
            asked = sum(1 for p in srv.served if p.upper() == "/ANIM.GIF")
            say("ANIM: state %d, asked for %d times, engine %d"
                % (sts[ak], asked, rb("br_ie")))
            if asked != 1:
                fails.append("ANIM.GIF was asked for %d times, not once"
                             % asked)
            if sts[ak] != IS_READY:
                fails.append("ANIM's copy left the store (state %d)"
                             % sts[ak])
            if not playing:
                fails.append("the animation is not playing (engine %d)"
                             % rb("br_ie"))
            elif not any(s in (IS_EMS, IS_DISK) for s in sts[ak + 1:]):
                fails.append("no still after ANIM was spilt: the row does "
                             "not show a spill passing over it")
            else:
                f0 = rw("br_iframes")
                os88marty.until(m, lambda _m: rw("br_iframes") >= f0 + 3,
                                "three frames of ANIM", limit=120)
                say("ANIM plays: %d frames drawn" % (rw("br_iframes") - f0))
        name = bytes(m.readseg(pseg, sy["br_icname"], 12)).decode("latin-1")
        say("cache state %d, file %s, %d bytes allocated"
            % (rb("br_icstate"), name,
               rw("br_ifend") | rw("br_ifend", 2) << 16))

        def files():
            snap = os.path.join(tmpd, "b.img")
            m.flush(1, snap)
            return cache_files(snap)
        if IS_DISK in sts:
            on = files()
            say("BRCACHE on B: holds %s" % on)
            if not on or name not in on:
                fails.append("%s is not in B:'s BRCACHE (%s)" % (name, on))

        # --- 2. the copies in the store -------------------------------------
        want = {}
        store = rw("br_istseg")
        for k, (name, data, attrs, _) in enumerate(gs):
            if k == ak:
                continue                # its copy is whichever frame shows
            at = brgifref.parse_attrs(attrs)
            wc, wr, wb = brgifref.expected(data, at["width"], at["height"],
                                           0, False)
            want[k] = (wc, wr, wb)
            e = tab[k]
            if e[0] == IS_READY:
                para = e[12] | (e[13] << 8)
                if (e[14], e[15]) != (wc, wr):
                    fails.append("%s: box %dx%d, %dx%d expected"
                                 % (name, e[14], e[15], wc, wr))
                    continue
                got = m.readseg(store + para, 0, len(wb))
                if got != wb:
                    fails.append("%s: the copy in the store differs" % name)
                else:
                    say("%s in the store, identical" % name)

        # --- 3. the copies in EMS, through the frame ------------------------
        seen = {}
        bp = {"type": "execseg", "seg": pseg, "off": src_at}
        m.breakpoints([bp])

        def drain():
            idle = 0                    # an animation paints for ever: the
            while idle < 60:            # page is drawn once 60 of its own
                st = m.wait_stop(6)     # slices in a row went by
                if st != "breakpoint":
                    return
                idle += 1
                r = m.regs()
                irec = rw("br_irec")
                rc = m.readseg(pseg, sy["br_rimg"] + irec, 6)
                k, sl = rc[4], rc[2]
                if k < a.n and k != ak and tab[k][0] in (IS_EMS, IS_DISK):
                    idle = 0
                    wc, wr, wb = want[k]
                    stride = 4 * wc
                    n = 8 * stride
                    got = bytes(m.readseg(r["es"], r["si"], n))
                    ok = got == wb[sl * n:(sl + 1) * n]
                    seen.setdefault(k, {})[sl] = ok
                    if not ok:
                        say("  %s slice %d from %04X:%04X DIFFERS"
                            % (gs[k][0], sl, r["es"], r["si"]))
                m.run()
        m.key("Escape")                 # the keyboard back to the page
        os88marty.settle(m)
        for _ in range(4 * a.n):        # down to the end, then the top again:
            if rw("br_top") + rw("br_rows") >= rw("br_nlines"):
                break                   # the load drew the top before its
            m.key("PageDown")           # copies moved, and Home at the top
            drain()                     # draws nothing
        m.key("Home")
        drain()
        m.breakpoints([])
        m.run()
        for k in range(a.n):
            if k == ak or tab[k][0] not in (IS_EMS, IS_DISK):
                continue
            where = "EMS" if tab[k][0] == IS_EMS else "the file"
            got = seen.get(k, {})
            rows = want[k][1]
            bad = [s for s, ok in got.items() if not ok]
            if bad:
                fails.append("%s: %d slices read from %s differ "
                             "(first %d)" % (gs[k][0], len(bad), where,
                                             min(bad)))
            elif len(got) < rows:
                fails.append("%s: only %d of %d slices were painted from %s"
                             % (gs[k][0], len(got), rows, where))
            else:
                say("%s from %s: all %d slices identical"
                    % (gs[k][0], where, rows))

        # --- 4. the file goes with its page, and a crash's with a lone start
        if IS_DISK in sts:
            def reload_wait(what):
                s0 = len(srv.served)
                m.key("F5")
                os88marty.until(m, lambda _m: len(srv.served) >= s0 + 1 + a.n,
                                "the reload's page and pictures, once each",
                                limit=1800)
                os88marty.until(m, engine_done, what, limit=1800)
                os88marty.settle(m)
                got = srv.served[s0:]
                if len(got) != 1 + a.n:
                    fails.append("%s: %d requests, %d expected"
                                 % (what, len(got), 1 + a.n))
            reload_wait("the reload's pictures")
            name2 = bytes(m.readseg(pseg, sy["br_icname"], 12)).decode(
                "latin-1")
            on = files()
            say("after F5: BRCACHE holds %s, this page's is %s" % (on, name2))
            if on is None or name in on:
                fails.append("F5 left the last page's %s in BRCACHE (%s)"
                             % (name, on))
            # a crash: the instance forgets its file (and that it swept),
            # so only the lone start's sweep can take it away
            m.write(pseg * 16 + sy["br_icstate"], bytes([0]))
            m.write(pseg * 16 + sy["br_iclean"], bytes([0]))
            reload_wait("the pictures after the forgotten file")
            on = files()
            say("after a forgotten file and F5: BRCACHE holds %s" % on)
            if on is None or name2 in on:
                fails.append("the lone start did not sweep %s (%s)"
                             % (name2, on))
        if a.shot:
            w, h, d = m.fbuf(None)
            os88marty.write_png_rgb(a.shot, w, h, d)
            say("shot %s" % a.shot)
    for f in fails:
        say("FAIL " + f)
    say("brgifems: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
