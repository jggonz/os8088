#!/usr/bin/env python3
"""Back, Forward, Reload and Save As (BROWSER-PLAN 5).

    make && make browsertest && make ethertest && python3 tests/brnav.py

**IT NEEDS THE NETWORK, and that is not a harness convenience.** History is
recorded by br_go and a page opened from a FLOPPY never goes through it, so a
local-file test would drive an empty stack and pass on a browser whose Back
button did nothing. Two pages are served on two paths so that "Back went to
the previous one" is a claim about CONTENT rather than about a counter.

Four assertions:

1. A NAVIGATION PUSHES AND MOVES. Two fetches leave two entries with the
   cursor on the second.
2. BACK AND FORWARD MOVE THE CURSOR AND FETCH. The state comes back to
   BN_DONE and the server sees the request, so this is the real path and not
   a repaint of what was already there.
3. NEITHER PUSHES. The stack is still two entries afterwards - the bug this
   is really guarding is Back recording itself, which turns the stack into a
   loop between two pages that Forward can never leave.
4. SAVE AS PUTS THE SERVER'S BYTES ON THE FLOPPY. Verified by reading the
   IMAGE FILE from the host afterwards - the writer and the reader are then
   not the same FAT12 code, which is CLAUDE.md's rule about checking a guest
   write.
"""
import os
import socket
import subprocess
import sys
import threading
import time

sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
sys.path.insert(0, "/home/user/os8088")
import dispcp                                          # noqa: E402
from ethernet import (Qemu, u16, S, Mouse, type_url,   # noqa: E402
                      settle, SOCK)
from os88geom import MB_ENTSZ                         # noqa: E402
import os88qemu                                              # noqa: E402

PORT = 8091
SLOW_SECS = 12          # how long /slow.htm is held open (SPEC.md 71.8)

PAGE_A = (b"<html><head><title>Page Alpha</title></head><body>\r\n"
          b"<h1>Page Alpha</h1>\r\n"
          b"<p>The first of two, and the one Back has to come home to.</p>\r\n"
          b"</body></html>\r\n")
PAGE_B = (b"<html><head><title>Page Beta</title></head><body>\r\n"
          b"<h1>Page Beta</h1>\r\n"
          b"<p>The second, reached by typing rather than by a link.</p>\r\n"
          b"</body></html>\r\n")
# The REAL FrogFind home page, saved off a 5150 through File > Save As. Its
# form action is `/` and its links are two absolute https ones and one
# RELATIVE `about.php` - which is why it is here rather than a page written to
# suit the test: both of the defects it caught were about a relative URL, and
# neither showed on a page whose author was the same person as the parser's.
PAGE_F = open("tests/htm/frogfind-home.htm", "rb").read()
URL_A = "http://10.0.2.2:%d/a.htm" % PORT
URL_SLOW = "http://10.0.2.2:%d/slow.htm" % PORT
URL_B = "http://10.0.2.2:%d/b.htm" % PORT
SAVED = "SAVED.HTM"


class TwoPages(threading.Thread):
    """Two paths, and it counts what was asked for."""

    def __init__(self):
        threading.Thread.__init__(self, daemon=True)
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.s.bind(("0.0.0.0", PORT))
        self.s.listen(4)
        self.log = []
        self.heads = []

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
                first = req.split(b"\r\n")[0] if req else b""
                if b"/slow.htm" in first:
                    # A FETCH THAT IS STILL IN FLIGHT is the whole point of
                    # SPEC.md 71.8, and on a loopback socket nothing is ever
                    # in flight long enough to click through. The server holds
                    # this one open instead, which is deterministic where a
                    # race is not - IN A THREAD, because this accept loop is
                    # one and the fetch that ABORTS it needs serving meanwhile.
                    self.log.append(first.decode("latin-1", "replace"))
                    threading.Thread(target=self._slow, args=(c,),
                                     daemon=True).start()
                    c = None                    # ...the thread owns it now
                    continue
                if b"/f.htm" in first or first.startswith(b"GET /?"):
                    body = PAGE_F
                elif b"/b.htm" in first:
                    body = PAGE_B
                else:
                    body = PAGE_A
                self.log.append(first.decode("latin-1", "replace"))
                self.heads.append(req.decode("latin-1", "replace"))
                c.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n"
                          b"Content-Length: %d\r\n\r\n" % len(body) + body)
            except OSError:
                pass
            finally:
                if c is not None:
                    c.close()

    def _slow(self, c):
        try:
            time.sleep(SLOW_SECS)
            c.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n"
                      b"Content-Length: %d\r\n\r\n" % len(PAGE_A) + PAGE_A)
        except OSError:
            pass                                # ...aborted, which is the pass
        finally:
            try:
                c.close()
            except OSError:
                pass


def main():
    import re
    import shutil
    import tempfile
    fails = []

    def bsyms():
        d = tempfile.mkdtemp()
        src, mp, out = (os.path.join(d, n) for n in ("b.asm", "b.map", "b.bin"))
        shutil.copy("apps/browser/browser.asm", src)
        with open(src, "a") as f:
            f.write("\n[map all %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                        "-I", "apps/browser/", "-I", "drivers/net/",
                        "-o", out, src], check=True)
        if open(out, "rb").read() != open("build/browser.bin", "rb").read():
            sys.exit("brnav: the mapped build is not build/browser.bin")
        r = {}
        for line in open(mp):
            mm = re.match(r"^\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+(\S+)\s*$",
                          line)
            if mm:
                r[mm.group(3)] = int(mm.group(1), 16)
        return r

    sy = bsyms()
    # BOTH images are rebuilt, and the apps one is the point: this test WRITES
    # to it (that is assertion 4), QEMU mounts it writable, and `make` cannot
    # see the difference because the guest's write leaves the image newer than
    # everything it was built from. Left alone, the second run finds its own
    # SAVED.HTM already there and proves nothing.
    for f in ("build/ether360.img", "build/brtest360.img"):
        if os.path.exists(f):
            os.remove(f)
    for t in ("ethertest", "browsertest"):
        r = subprocess.run(["make", t], capture_output=True, text=True)
        if r.returncode:
            sys.exit("brnav: make %s failed:\n%s%s" % (t, r.stdout, r.stderr))

    srv = TwoPages()
    srv.start()
    print("two pages on :%d" % PORT)
    sys.stdout.flush()

    if os.path.exists("build/qemu.pid"):
        try:
            os.kill(int(open("build/qemu.pid").read().strip()), 15)
            time.sleep(1.0)
        except (OSError, ValueError):
            pass
    for f in ("build/qmp.sock", "build/qemu.pid"):
        if os.path.exists(f):
            os.remove(f)
    # `make test` DAEMONISES the emulator, so it outlives this script
    # unless somebody kills it - and the somebody is us (os88qemu).
    os88qemu.own()
    r = subprocess.run(["make", "test", "ETHER=1",
                        "TESTIMG=build/ether360.img",
                        "TESTAPPS=build/brtest360.img"],
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit("brnav: make test failed:\n" + r.stdout + r.stderr)
    m = Qemu()

    for _ in range(150):
        time.sleep(0.4)
        if u16(m.read(S("drv_tab") + 2 * 16 + 2, 2)):
            break
    time.sleep(10)                                  # let DHCP bind

    dispcp.open_drive(m, Mouse(), S, settle, "B")
    wins = dispcp.win_list(m, S)
    wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
    dispcp.open_named(m, Mouse(), S, settle, wx, wy, "BROWSER.O88")
    bw = dispcp.win_list(m, S)[-1]
    rec = m.read(S("wm_wins") + bw * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
    pseg = u16(rec[22:24])
    print("browser at %04X" % pseg)
    sys.stdout.flush()

    rb = lambda n: m.readseg(pseg, sy[n], 1)[0]         # noqa: E731
    rw = lambda n: u16(m.readseg(pseg, sy[n], 2))       # noqa: E731

    def wait_done(what):
        for _ in range(80):
            time.sleep(0.5)
            if rb("br_nstate") in (6, 7):
                break
        st = rb("br_nstate")
        if st != 6:
            fails.append("%s: state %d, not BN_DONE" % (what, st))
        return st

    def fetch(url):
        # **CLEAR THE BAR FIRST.** type_url only types, and the ethernet gate
        # never needed more because it types into an empty field once. A
        # second URL appended to the first is a malformed one, br_go refuses
        # it, and every assertion below then measures a browser that was never
        # asked for the page.
        lx2 = u16(m.readseg(pseg, sy["br_loc"] + 4, 2))
        ly1 = u16(m.readseg(pseg, sy["br_loc"] + 2, 2))
        Mouse().click(lx2 - 6, ly1 + 7)         # past the text: the caret
        time.sleep(1.0)                         # lands at its END
        n = u16(m.readseg(pseg, sy["br_loc"] + 12, 2))
        if n:
            subprocess.run(["python3", "tools/qmp.py", SOCK]
                           + ["sendkey backspace", "sleep 0.05"] * n,
                           check=True, capture_output=True)
            time.sleep(0.5)
        type_url(url)
        time.sleep(1.0)
        subprocess.run(["python3", "tools/qmp.py", SOCK, "sendkey ret"],
                       check=True, capture_output=True)

    def m_type(text):
        for ch in text:
            subprocess.run(["python3", "tools/qmp.py", SOCK, "sendkey " + ch],
                           check=True, capture_output=True)
            time.sleep(0.12)
        time.sleep(0.6)

    def click_offset(off):
        """Aim from the app's OWN line table, never by eye."""
        lseg, nl, docseg = rw("br_lseg"), rw("br_nlines"), rw("br_docseg")
        tab = bytes(m.readseg(lseg, 0, nl * 6))
        ln = None
        for i in range(nl):
            o, e = u16(tab[i*6:i*6+2]), u16(tab[i*6+2:i*6+4])
            if o <= off < e:
                ln = i
                break
        if ln is None:
            return "offset %d is on no display line" % off
        for _ in range(200):
            top, rows = rw("br_top"), rw("br_rows")
            if top <= ln < top + rows:
                break
            x, y, ww, hh = dispcp.win_rect(m, S, bw)
            was = top
            if ln < top:
                Mouse().click(x + ww - 8, y + 30)
            else:
                Mouse().click(x + ww - 8, y + hh - 20)
            if rw("br_top") == was:
                return "could not scroll line %d into view" % ln
        o, e, lx = (u16(tab[ln*6:ln*6+2]), u16(tab[ln*6+2:ln*6+4]), tab[ln*6+4])
        doc = bytes(m.readseg(docseg, o, e - o))
        col, i = lx, 0
        target = None
        while i < len(doc):
            if o + i == off:
                target = col
                break
            ch = doc[i]
            if ch == 14:                    # D_LNK1 carries two payload bytes
                i += 3
                continue
            i += 1
            if ch < 0x20:
                continue
            col += 1
        if target is None:
            return "offset %d is in no cell of line %d" % (off, ln)
        Mouse().click(rw("br_cx") + target * 8 + 3,
                      rw("br_cy") + (ln - rw("br_top")) * 8 + 3)
        return None

    def toolbar_click(which):
        r = {"back": "br_r1", "fwd": "br_r2", "rel": "br_r3"}[which]
        x1 = u16(m.readseg(pseg, sy[r], 2))
        y1 = u16(m.readseg(pseg, sy[r] + 2, 2))
        x2 = u16(m.readseg(pseg, sy[r] + 4, 2))
        y2 = u16(m.readseg(pseg, sy[r] + 6, 2))
        Mouse().click((x1 + x2) // 2, (y1 + y2) // 2)

    # --- 1: two navigations ------------------------------------------------
    fetch(URL_A)
    wait_done("page A")
    print("after A: histn=%d histi=%d" % (rw("br_histn"), rw("br_histi")))
    fetch(URL_B)
    wait_done("page B")
    n2, i2 = rw("br_histn"), rw("br_histi")
    print("after B: histn=%d histi=%d" % (n2, i2))
    if (n2, i2) != (2, 1):
        fails.append("two fetches should leave histn=2 histi=1, got %d/%d"
                     % (n2, i2))

    # --- the bar's PIXELS, so that "it followed" is about the glass ---------
    # br_go moves the BUFFER and only a draw moves the screen, and every
    # caller but the typed one had forgotten the draw (SPEC.md 71.7). Reading
    # br_ubuf cannot see that: it is the buffer. So the bar's rect is captured
    # at each stop, and two stops on different pages must not look the same.
    def bar_pixels():
        # THE CARD'S OWN RASTER, through screendump, and not the framebuffer:
        # this runs on QEMU's VGA in mode 12h, where the bytes are four planes
        # behind the Graphics Controller and not flat-readable (CLAUDE.md).
        # `shot.py` needs Pillow, which a fresh container has not got, so the
        # NetPBM is parsed here - it is a P6 header and RGB triples.
        x1 = u16(m.readseg(pseg, sy["br_loc"] + 0, 2))
        y1 = u16(m.readseg(pseg, sy["br_loc"] + 2, 2))
        x2 = u16(m.readseg(pseg, sy["br_loc"] + 4, 2))
        y2 = u16(m.readseg(pseg, sy["br_loc"] + 6, 2))
        path = os.path.join(tempfile.gettempdir(), "brnav_bar.ppm")
        subprocess.run(["python3", "tools/qmp.py", SOCK,
                        'screendump %s' % path], check=True,
                       capture_output=True)
        raw = open(path, "rb").read()
        parts = raw.split(b"\n", 3)            # P6 / "w h" / maxval / data
        w, h = (int(v) for v in parts[1].split())
        data = parts[3]
        out = []
        for y in range(y1, min(y2 + 1, h)):
            o = (y * w + x1) * 3
            out.append(data[o:o + (min(x2, w - 1) - x1 + 1) * 3])
        return b"".join(out)

    bar_b = bar_pixels()

    # --- 2 and 3: Back, Forward, Reload ------------------------------------
    toolbar_click("back")
    wait_done("Back")
    bar_a = bar_pixels()
    if bar_a == bar_b:
        fails.append("the location bar is pixel-identical before and after "
                     "Back - it is still showing the previous page's address "
                     "(SPEC.md 71.7)")
    nb, ib = rw("br_histn"), rw("br_histi")
    print("after Back: histn=%d histi=%d, last request %r"
          % (nb, ib, srv.log[-1] if srv.log else None))
    if ib != 0:
        fails.append("Back should land on entry 0, got %d" % ib)
    if nb != 2:
        fails.append("Back must not PUSH - histn is %d, not 2. A Back that "
                     "records itself turns the stack into a loop Forward can "
                     "never leave" % nb)
    if not srv.log or "/a.htm" not in srv.log[-1]:
        fails.append("Back did not re-request /a.htm: %r" % (srv.log[-1:],))

    toolbar_click("fwd")
    wait_done("Forward")
    if bar_pixels() == bar_a:
        fails.append("the location bar did not change on Forward either")
    nf, iff = rw("br_histn"), rw("br_histi")
    print("after Fwd: histn=%d histi=%d, last request %r"
          % (nf, iff, srv.log[-1] if srv.log else None))
    if iff != 1:
        fails.append("Forward should land on entry 1, got %d" % iff)
    if nf != 2:
        fails.append("Forward must not push - histn is %d" % nf)
    if not srv.log or "/b.htm" not in srv.log[-1]:
        fails.append("Forward did not request /b.htm: %r" % (srv.log[-1:],))

    # --- 4: the History MENU, and it jumps straight there (SPEC.md 71.8) ---
    # The count and the labels first, because they are what a wrong answer
    # here looks like: a menu with the right number of items naming the wrong
    # pages is indistinguishable from a working one until it is clicked.
    MB_XL, MB_XR, BR_TITMAX = 6, 8, 32
    nit = (u16(m.readseg(pseg, sy["BR_HNITEM"], 2))
           if "BR_HNITEM" in sy else None)
    if nit is not None and nit != 2:
        fails.append("the History menu offers %d items for a two-entry stack"
                     % nit)
    labels = []
    for i in range(2):
        ptr = u16(m.readseg(pseg, sy["br_hitems"] + i * 2, 2))
        raw = bytes(m.readseg(pseg, ptr, BR_TITMAX)).split(b"\0")[0]
        labels.append(raw.decode("latin-1"))
    print("History menu: %s items, labels %r" % (nit, labels))
    if labels != ["Page Alpha", "Page Beta"]:
        fails.append("the History labels are %r - the <title> of each page "
                     "should have replaced its URL (SPEC.md 71.8)" % labels)

    # ...and now click it. The cell's x comes from the KERNEL's own bar table,
    # never from a screenshot: a menu is 8px of padding either side of a title
    # whose width depends on the app in front.
    hx = None
    for c in range(7):
        base = S("menu_bar") + c * MB_ENTSZ
        xl = u16(m.read(base + MB_XL, 2))
        xr = u16(m.read(base + MB_XR, 2))
        tptr = u16(m.read(base, 2))
        if tptr and xr > xl:
            nm = bytes(m.readseg(pseg, tptr, 12)).split(b"\0")[0]
            if nm == b"History":
                hx = (xl + xr) // 2
    if hx is None:
        fails.append("no History cell in the menu bar")
    else:
        before_h = len(srv.log)
        subprocess.run(["python3", "tools/mouse.py", SOCK, "down",
                        str(hx), "6"], check=True, capture_output=True)
        time.sleep(0.5)
        subprocess.run(["python3", "tools/mouse.py", SOCK, "to",
                        str(hx), "26"], check=True, capture_output=True)
        time.sleep(0.5)
        subprocess.run(["python3", "tools/mouse.py", SOCK, "up"],
                       check=True, capture_output=True)
        wait_done("History > Page A")
        print("after History > item 0: histi=%d, +%d request(s), last %r"
              % (rw("br_histi"), len(srv.log) - before_h,
                 srv.log[-1] if srv.log else None))
        if rw("br_histi") != 0:
            fails.append("History > item 0 landed on entry %d"
                         % rw("br_histi"))
        if not srv.log or "/a.htm" not in srv.log[-1]:
            fails.append("History > item 0 did not fetch /a.htm: %r"
                         % (srv.log[-1:],))
        toolbar_click("fwd")            # ...back where the Reload test wants
        wait_done("Forward")

    before = len(srv.log)
    toolbar_click("rel")
    wait_done("Reload")
    print("after Reload: histn=%d histi=%d, +%d request(s)"
          % (rw("br_histn"), rw("br_histi"), len(srv.log) - before))
    if rw("br_histn") != 2 or rw("br_histi") != 1:
        fails.append("Reload moved the stack: %d/%d"
                     % (rw("br_histn"), rw("br_histi")))

    # --- the request the server actually saw, headers and all ---------------
    ua = [h for h in srv.heads if "User-Agent:" in h]
    line = ""
    if ua:
        for ln in ua[-1].split("\r\n"):
            if ln.startswith("User-Agent:"):
                line = ln
    print("the server saw %r" % line)
    if line != "User-Agent: os8088 Browser 1.0":
        fails.append("the User-Agent is %r" % line)

    # --- 5: BACK DURING A FETCH DROPS IT (SPEC.md 71.8) --------------------
    # The reported bug: a second Back while a page was still loading said
    # `Still fetching` and then the browser never finished anything again.
    # The refusal set BN_ERR on a fetch whose worker was still running and
    # whose socket was still open - so nothing ever drained it (br_nstep
    # dispatches on the state, and BN_ERR is not in its range) and the page
    # never rendered.
    #
    # **THE ENTRY BEING GONE BACK TO HAS TO BE THE SLOW ONE**, which is what
    # the first draft of this got wrong: aborting the CURRENT fetch and then
    # pressing Back again passes with the bug in place, because the refusal
    # itself leaves BN_ERR - a FINISHED state - and so unblocks the next
    # navigation. Verified by putting the refusal back: the gate was green.
    # So the stack is built as [a, b, slow, b] and Back is pressed twice from
    # the end: the first lands on /slow.htm and is still in flight when the
    # second arrives.
    fetch(URL_SLOW)
    wait_done("/slow.htm")
    fetch(URL_B)
    wait_done("/b.htm again")
    hi0 = rw("br_histi")
    toolbar_click("back")               # ...into the slow entry
    time.sleep(3.0)
    inflight = rb("br_nstate")
    print("Back landed on /slow.htm; state = %d" % inflight)
    if not 1 <= inflight <= 5:
        fails.append("Back did not leave a fetch in flight (state %d) - the "
                     "assertion below measures nothing" % inflight)
    toolbar_click("back")               # ...and again, which is what wedged it
    st = wait_done("a second Back during a fetch")
    hi = rw("br_histi")
    print("after Back x2 (from %d): state %d, histi=%d, last %r"
          % (hi0, st, hi, srv.log[-1] if srv.log else None))
    if st != 6:
        fails.append("the browser is stuck at state %d after a second Back "
                     "during a fetch - the wedge SPEC.md 71.8 is about" % st)
    if hi != hi0 - 2:
        fails.append("two Backs from entry %d should land on %d, got %d"
                     % (hi0, hi0 - 2, hi))
    if not srv.log or "/b.htm" not in srv.log[-1]:
        fails.append("the second Back did not fetch the entry it landed on: "
                     "%r" % (srv.log[-1:],))
    # ...and the app still works afterwards, which is the half a state check
    # alone cannot see: a wedged browser also reports a state.
    toolbar_click("fwd")
    if wait_done("Forward after the aborts") != 6:
        fails.append("the browser did not recover: Forward after the aborts "
                     "never completed")


    # --- 5: a REAL page off the wire: links, and a relative form action -----
    fetch("http://10.0.2.2:%d/f.htm" % PORT)
    wait_done("FrogFind home")
    n = rw("br_lnkn")
    seg = rw("br_lnkseg")
    print("frogfind: %d link(s), arena %04X, action %r, %d field(s)"
          % (n, seg, bytes(m.readseg(pseg, sy["br_faction"], 40))
             .split(b"\0")[0].decode("latin-1"), rb("br_fn")))
    if not seg:
        fails.append("the link arena was not claimed on the NETWORK path - "
                     "anchors render as plain text on every page off the wire "
                     "while a page opened from a floppy links perfectly")
    if n != 3:
        fails.append("frogfind's home page has 3 links, %d parsed" % n)
    elif seg:
        arena = bytes(m.readseg(seg, 0, max(rw("br_lnkw"), 1)))
        offs = [u16(m.readseg(pseg, sy["br_lnkoff"] + i * 2, 2))
                for i in range(n)]
        hrefs = [arena[o:arena.index(b"\0", o)].decode("latin-1") for o in offs]
        print("  hrefs: %r" % hrefs)
        if "about.php" not in hrefs:
            fails.append("the relative link is missing: %r" % hrefs)

    # ...and SUBMIT it. The action is `/`, so an unresolved compose is
    # `/?q=...` - which br_split rightly refuses as not-http, and the search
    # box could then be typed into and never submitted anywhere.
    before = len(srv.log)
    if rb("br_fn"):
        fofs = u16(m.readseg(pseg, sy["br_flds"], 2))
        err = click_offset(fofs)
        if err:
            fails.append("frogfind field: " + err)
        else:
            m_type("os8088")
            sub0 = rw("br_subofs")
            err = click_offset(sub0 + 2)
            if err:
                fails.append("frogfind submit: " + err)
            else:
                wait_done("submit")
                got = srv.log[-1] if len(srv.log) > before else None
                print("  the server saw: %r" % got)
                if not got or "q=os8088" not in got:
                    fails.append("submitting a form whose action is `/` did "
                                 "not reach the server: %r" % got)

    # --- 4: Save As ---------------------------------------------------------
    if not rw("br_srcseg"):
        fails.append("the source was freed after the parse, so Save As has "
                     "nothing to write")
    else:
        # File > Save As..., the third item. **THE TITLES ARE MEASURED**, not
        # guessed: the bar is chip / app-name / the app's own menus, so File's
        # x depends on how long the application is called. Read off the glass
        # for this app it is 134, and 40 - the first version's guess - is the
        # CHIP menu.
        nwin = len(dispcp.win_list(m, S))
        subprocess.run(["python3", "tools/mouse.py", SOCK, "down", "134", "8"],
                       check=True, capture_output=True)
        time.sleep(0.4)
        subprocess.run(["python3", "tools/mouse.py", SOCK, "to", "134", "62"],
                       check=True, capture_output=True)
        time.sleep(0.4)
        subprocess.run(["python3", "tools/mouse.py", SOCK, "up"],
                       check=True, capture_output=True)
        time.sleep(3)
        wins_now = dispcp.win_list(m, S)
        print("windows after Save As: %r (was %d)" % (wins_now, nwin))
        if len(wins_now) <= nwin:
            fails.append("File > Save As... opened no dialog")
        type_url(SAVED)
        time.sleep(1.0)
        subprocess.run(["python3", "tools/qmp.py", SOCK, "sendkey ret"],
                       check=True, capture_output=True)
        time.sleep(6)
        st = rb("br_nstate")
        print("state after the save: %d" % st)
        if st != 6:
            fails.append("a save that WORKED reports state %d - a message and "
                         "a failure are different things (BN_DONE is 6)" % st)

    m.quit()
    time.sleep(1.0)
    raw = open("build/brtest360.img", "rb").read()
    if SAVED.encode() not in raw.replace(b".", b""):
        # the 8.3 directory entry is 'SAVED   HTM'
        if b"SAVED   HTM" not in raw:
            fails.append("%s is not in the apps image's directory" % SAVED)
    # ...the page CURRENTLY loaded, which is the search result the submit
    # above fetched - not PAGE_B. Assert against whatever was last served, or
    # this reads as a broken Save As every time a fetch is added ahead of it.
    if b"FrogFind!" not in raw:
        fails.append("the saved file does not hold the server's bytes")
    else:
        print("the image holds the server's own bytes")

    for f in fails:
        print("FAIL: " + f)
    print("brnav: " + ("ok" if not fails else "FAILED"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
