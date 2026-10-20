#!/usr/bin/env python3
"""A navigation WHILE THE WORKER PARSES must not free what the parse writes
into (SPEC.md 71.14).

    make && make browsertest && python3 tests/brrace.py

The Browser's fetch hands the page to the UI task to claim its buffers and
back to the WORKER to parse it, and the state stays BN_BODY - in flight -
for the whole of the parse. So a Reload, a typed URL or a File > Open in that
window went through br_abort and br_free_all while the worker was still
writing the document stream: into a claim that had just been freed, and,
because br_free_all ZEROES [br_docseg] and br_go claims only the source,
into segment 0 - the interrupt vector table - at the next flush. And the
stale parse then stored BN_DONE over the new fetch's BN_OPEN, so the fetch
asked for never ran.

This row puts the machine in exactly that window, which no human could aim
for: a 4KB page over brfetch.py's cable, the guest STOPPED by a breakpoint
inside br_parse's tag dispatch while the worker is parsing it, a click on
Reload, the guest stopped again where br_abort is entered (so the row can
SAY the navigation landed inside the parse, with [br_fin] = 3 and nothing
laid out), and the second fetch served. Three assertions:

1. the interrupt vector table is byte-identical across the navigation;
2. the SECOND request reached the server - the reload ran;
3. the browser ends BN_DONE with the page laid out.

BROKEN ON PURPOSE: against the browser before br_abort's wait (the parent
of the commit that added this row) it reads 250 bytes of the vector table
changed, the reload never on the wire and the state left at BN_OPEN - all
four assertions red. Against the fix: 0 bytes, two requests, BN_DONE.
QEMU is not needed: the cable's partner is real host sockets on MartyPC.
"""
import argparse
import os
import socket
import sys
import threading
import time

# THIS TREE'S root, DERIVED - never a hard-coded path. A literal is right in the
# checkout it was written in and wrong in a git worktree, which is how parallel
# work is done here: os88sym re-assembles ROOT/kernel/kernel.asm and compares it
# against ROOT/build/kernel.bin, so a literal ROOT answers about a DIFFERENT
# kernel from the image being booted.
_OS88_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tools"))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tests"))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tests/lptlink"))
import dispcp                                          # noqa: E402
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88sym                                         # noqa: E402
import partner as P                                    # noqa: E402
import os88build                                       # noqa: E402

S = os88sym.linear

MACHINE = "os8088_5150_cga_lpt"
NET_ROW = 4                        # ...last since the reorder (SPEC.md 31.1)
DRVR_SZ, DRVR_SEG = 16, 2
CP_I0Y, CP_IROWH, CP_IDRV = 6, 14, 2
CP_RX, CP_DBY1, CP_DROWH = 96, 20, 26
PORT = 0        # an EPHEMERAL port, read back off the socket, as brfetch.py's
                # (a fixed 8089 collided with it on a shared box)
PATH = "/race.htm"

PAGE = (b"<html><head><title>Race</title></head><body>\r\n"
        + b"".join(b"<p>Paragraph %d of a page long enough that the parse is "
                   b"still running when the reader presses Return: <a href="
                   b"\"/p%d\">link %d</a> and <b>bold %d</b> &amp; more.</p>"
                   b"\r\n" % (i, i, i, i) for i in range(24))
        + b"</body></html>\r\n")


def codeoff(name):
    """A browser code label's offset, by assembling the source with a `dw`
    just before OS88_IMAGE_END - which moves nothing above it."""
    import subprocess
    import tempfile
    src = open(os.path.join(_OS88_ROOT, "apps/browser/browser.asm")).read()
    probe = src.replace("    OS88_IMAGE_END",
                        "    dw %s\n    OS88_IMAGE_END" % name, 1)
    with tempfile.TemporaryDirectory() as td:
        asm, binf = os.path.join(td, "p.asm"), os.path.join(td, "p.bin")
        open(asm, "w").write(probe)
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                        "-I", "apps/browser/", "-I", "drivers/net/", "-o",
                        binf, asm], cwd=_OS88_ROOT, check=True)
        d = open(binf, "rb").read()
    img = open(os88build.at("build/browser.bin"), "rb").read()
    n = len(img)
    if len(d) != n + 2:                 # the bss names move by the 2 bytes,
        sys.exit("brrace: build/browser.bin is not what the source assembles "
                 "to - `make browsertest` first")   # so the LENGTH is the test
    return d[n] | (d[n + 1] << 8)


def say(*a):
    print(*a)
    sys.stdout.flush()


class Server(threading.Thread):
    """One page, and it REMEMBERS THE REQUEST - which is assertion 1."""

    def __init__(self):
        threading.Thread.__init__(self, daemon=True)
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.s.bind(("127.0.0.1", PORT))
        self.s.listen(2)
        self.port = self.s.getsockname()[1]
        self.request = None

    def run(self):
        self.requests = []
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
                self.request = req
                self.requests.append(req)
                c.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n"
                          b"Content-Length: %d\r\n\r\n" % len(PAGE) + PAGE)
            except OSError:
                pass
            finally:
                c.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot", default=None)
    a = ap.parse_args()

    fails = []
    img = os.path.getsize(os88build.at("build/browser.bin"))
    # THE DISK IS BUILT FROM THE PACKAGE AND CAN BE OLDER THAN IT. That is not
    # hypothetical: this test's first real run drove a browser two commits
    # behind, reported `no such host` for a URL the bar was holding correctly,
    # and looked exactly like a parser bug. `make build/browser.o88` does not
    # rebuild the image, and nothing said so.
    if (os.path.getmtime("build/brtest360.img")
            < os.path.getmtime("build/browser.o88")):
        sys.exit("brrace: build/brtest360.img is OLDER than browser.o88 - "
                 "run `make browsertest` (this would have tested the wrong "
                 "binary and blamed the browser)")
    srv = Server()
    srv.start()
    url = "http://127.0.0.1:%d%s" % (srv.port, PATH)
    say("http server on %s, %d byte page" % (url, len(PAGE)))

    box = P.SocketBox()
    ft = P.FileTree()
    ft.add(0, "READ.ME", content=b"a link volume\r\n")

    with os88marty.launch("build/os8088-360.img", apps="build/brtest360.img",
                          machine=MACHINE) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        p = P.Partner(m)

        # --- the browser first, empty: it touches no wire until asked ------
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wins = dispcp.win_list(m, S)
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "BROWSER.O88")
        wins2 = dispcp.win_list(m, S)
        if len(wins2) <= len(wins):
            sys.exit("brrace: BROWSER.O88 did not open a window")
        bw = wins2[-1]
        rec = m.read(S("wm_wins") + bw * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        say("browser at %04X" % pseg)

        # THE BROWSER'S OWN STATE, at bss offsets derived from the image
        # size the way tests/brtest.py already reads br_url. Reading a STRING
        # CONSTANT instead is the trap this test fell into first time round:
        # `Ready` is in the image whether or not the fetch worked, so the
        # status assertion passed over a run that had failed.
        BR_NLINES, BR_NSTATE, BR_FIN, BR_SRCLEN = 38, 946, 944, 30
        BN_BODY = 5
        BN_DONE, BN_ERR = 6, 7

        def bss_b(off):
            return m.readseg(pseg, img + off, 1)[0]

        def bss_w(off):
            d = m.readseg(pseg, img + off, 2)
            return d[0] | (d[1] << 8)

        def field(tag):
            raw = m.readseg(pseg, 0, img + 3000)
            i = raw.find(tag)
            if i < 0:
                return None
            return raw[i:raw.index(b"\0", i)].decode("latin-1")

        # --- tick the link driver, with the far end answering --------------
        mo.menu(8, 8, 8, 40)
        os88marty.settle(m)
        cp = None
        for w in dispcp.win_list(m, S):
            x, y, ww, hh = dispcp.win_rect(m, S, w)
            if ww >= 280 and hh >= 100:
                cp = (x, y)
        if cp is None:
            sys.exit("brrace: no Control Panel window")
        cx, cy = cp
        x0, y0 = cx + 1, cy + 18
        mo.click(x0 + 40, y0 + CP_I0Y + CP_IDRV * CP_IROWH + 7)
        os88marty.settle(m)
        # ROW 4 IS BELOW THE FOLD (SPEC.md 31.1.1) - scroll, then click the
        # VISIBLE index.
        vis = dispcp.drv_show(mo, x0, y0, NET_ROW,
                              lambda: os88marty.settle(m))
        mo.to(x0 + CP_RX + 40, dispcp.drv_row_y(y0, vis))
        m.pause()
        p.sync()
        p.click_paused_release()
        p.hello(P.NET_VER_SOCK)
        p.allow(60000000)
        seen = p.serve(ft, limit=24, idle=8000000, sox=box)
        say("connect served: %r" % "".join(seen))
        row = m.read(S("drv_tab") + NET_ROW * DRVR_SZ + DRVR_SEG, 2)
        if not (row[0] | (row[1] << 8)):
            sys.exit("brrace: the link driver did not load")
        m.run()
        mo.click(cx + 8, cy + 9)                    # close the panel (31.8)
        os88marty.settle(m)

        # --- type the URL into the location bar ----------------------------
        # The bar is NOT the top strip of the content: SPEC.md 71.6's toolbar
        # - Back, Forward, Reload and the state - is above it. Clicking the old
        # row landed on Forward, so the bar never took focus and the URL was
        # typed into the page.
        #
        # AND THE BAR IS NOT EMPTY: it opens holding `http://` with the caret
        # at the end (br_entry's os88line_set - the scheme is mandatory and
        # the pre-fill is how the browser says so). A URL typed over that
        # composes `http:/http://...`, which br_split reads as host `http` -
        # this test shipped in the same round as the pre-fill and was never
        # re-run against it. Click PAST the text (os88line_click clamps to
        # the end), then backspace the pre-fill away - a backspace at the
        # start of an empty field is a no-op the field owns, so overshooting
        # is free.
        BR_LPAD, BR_TBH, BR_TBG, BR_LBAR = 3, 13, 1, 15
        bx0, by0, bww, bhh = dispcp.win_rect(m, S, bw)
        mo.click(bx0 + bww - 40,
                 by0 + dispcp.TITLE_H + 1 + BR_LPAD + BR_TBH + BR_TBG
                 + BR_LBAR // 2)
        os88marty.settle(m)
        m.type_text("\b" * 12 + url)
        os88marty.settle(m)
        typed = field(b"http://127.0.0.1")
        say("location bar holds %r" % typed)
        if typed is None:
            fails.append("the URL never reached the location bar")

        def serve(what, idle):
            try:
                seen = p.serve(ft, limit=5000, idle=idle, sox=box)
                say("%s served: %d commands" % (what, len(seen)))
            except P.LinkTimeout as e:
                say("%s STALLED: %s" % (what, e))
                fails.append("the wire stalled during the %s: %s" % (what, e))

        # --- the first fetch, and the guest stopped INSIDE its parse --------
        # The pointer goes over RELOAD first, while the machine runs: Return
        # in the bar takes the bar's focus away (br_onkey), so the
        # navigation that lands mid-parse is a click, and a click into a
        # paused guest needs the pointer already there. Reload is the third
        # toolbar button: BR_LPAD in, after Back and Fwd (BR_BTNW 40 each,
        # BR_BTNG 3 between), BR_BTNW2 = 56 wide, BR_TBH = 13 tall.
        ccx, ccy = bx0 + 1, by0 + dispcp.TITLE_H + 1
        mo.to(ccx + BR_LPAD + 2 * (40 + 3) + 56 // 2, ccy + BR_LPAD + 13 // 2)
        os88marty.settle(m)
        # RETURN IS DELIVERED TO A PAUSED MACHINE (brfetch.py's reason).
        m.pause()
        p.sync()
        m.key("Enter")
        p.allow(400000000)
        # SERVED ONE COMMAND AT A TIME, and stopped at the CLOSE: the parse
        # needs no wire, so a serve left to run out its `idle` would spend the
        # whole parse inside itself where nothing can look. The worker
        # parses a tick or two after the close, on the far side of the UI
        # task's claims.
        n = 0
        for _ in range(3000):
            try:
                got = p.serve(ft, limit=1, idle=12000000, sox=box)
            except P.LinkTimeout as e:
                fails.append("the wire stalled during the first fetch: %s"
                             % e)
                break
            if not got:
                fails.append("the first fetch went quiet before its close")
                break
            n += 1
            if ord(got[0]) == P.NW_CLOSE:
                break
        say("first fetch served: %d commands, ending %r" % (n, got))
        off = codeoff("br_parse.tag")
        m.breakpoints([{"type": "execseg", "seg": pseg, "off": off}])
        m.run()
        caught, t0 = None, time.time()
        while time.time() - t0 < 120:
            if m.stopped():
                caught = (bss_b(BR_NSTATE), bss_b(BR_FIN))
                break
            if bss_b(BR_NSTATE) in (BN_DONE, BN_ERR):
                break
            time.sleep(0.02)
        m.breakpoints([])
        if caught is None:
            m.pause()
            fails.append("the guest was never stopped inside the parse - "
                         "the row tested nothing (state %d)"
                         % bss_b(BR_NSTATE))
        else:
            say("stopped inside br_parse: state %d, [br_fin] = %d" % caught)
            if caught[0] != BN_BODY:
                fails.append("stopped in a parse with the state at %d, not "
                             "the in-flight BN_BODY - not the window this "
                             "row is about" % caught[0])
        ivt0 = bytes(m.read(0, 1024))

        # --- the navigation, in that window: a click on Reload -------------
        # Press, a bounded 80 ms for the packet and the arm, release - and
        # the guest is stopped again where br_abort is ENTERED, so the row
        # can say whether the navigation really landed inside the parse
        # rather than after it, which is the difference between a test and
        # a pass.
        m.pause()
        p.sync()
        p.click_paused_release(settle=400000)
        m.breakpoints([{"type": "execseg", "seg": pseg,
                        "off": codeoff("br_abort")}])
        m.run()
        t0, at = time.time(), None
        while time.time() - t0 < 60:
            if m.stopped():
                at = (bss_b(BR_NSTATE), bss_b(BR_FIN), bss_w(BR_NLINES))
                break
            time.sleep(0.02)
        m.breakpoints([])
        m.pause()
        if at is None:
            fails.append("the Reload click never reached br_abort")
        else:
            say("br_abort entered with state %d, [br_fin] = %d, %d lines "
                "laid out" % at)
            if at[0] != BN_BODY or at[1] not in (0, 3) or at[2]:
                fails.append("the navigation landed AFTER the parse (state "
                             "%d, fin %d, %d lines) - the row tested nothing"
                             % at)
        p.sync()
        p.allow(400000000)
        serve("reload", 12000000)
        m.run()
        os88marty.settle(m)
        ivt1 = bytes(m.read(0, 1024))
        diff = [i for i in range(1024) if ivt0[i] != ivt1[i]]
        say("interrupt vector table: %d bytes changed" % len(diff))
        if diff:
            fails.append("the navigation changed %d bytes of the interrupt "
                         "vector table (first at %04X) - the parse flushed "
                         "into segment 0" % (len(diff), diff[0]))

        nstate, nlines = bss_b(BR_NSTATE), bss_w(BR_NLINES)
        say("br_nstate = %d, br_nlines = %d" % (nstate, nlines))
        if nstate != BN_DONE:
            fails.append("the browser's state is %d, not BN_DONE - the reload "
                         "never completed" % nstate)
        if nlines < 5:
            fails.append("br_nlines is %d: the reloaded page was not laid out"
                         % nlines)
        if a.shot:
            w0, h0, rows = m.vram("cga")
            os88marty.write_png(a.shot, w0, h0, rows)
            say("wrote %s" % a.shot)

    n = len(getattr(srv, "requests", []))
    say("server saw %d request(s)" % n)
    if n != 2:
        fails.append("the server saw %d requests, not 2 - the reload %s"
                     % (n, "never reached the wire" if n < 2 else
                        "was asked for twice"))

    for f in fails:
        say("FAIL: " + f)
    say("brrace: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
