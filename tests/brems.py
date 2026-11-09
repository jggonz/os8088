#!/usr/bin/env python3
"""A page in EXPANDED MEMORY: what reads the source and writes the document
while both share EMS's frame (SPEC.md 71.18, 71.24).

    make && make browsertest emstest && python3 tests/brems.py

The source and the formatted document both go to EMS when a NETWORK page
outgrows the conventional reserve, and then they are mapped into the same
four frame quarters: mapping one moves the other out from under any pointer
still held into it. No other row reaches that state - the EMS board machine
has no wire, and a local file never goes to EMS - so this one runs on
`os8088_5150_cga_lpt_ems`, MartyPC's cable machine with a Lo-tech 2 MB board
and 256 KB of conventional memory (tools/martypc/configs/os8088_machines.toml;
`make marty` stages it, or append its block to the run tree's ibm5150.toml).

THREE LEGS, after the page is fetched over the cable from this file's server:

1. THE PAGE IS IN EMS. `br_srcemshnd` and `br_docems` are both set - or the
   rest of the row proves nothing, and it says so.
2. A SELECT'S NEXT OPTION (br_selcycle -> br_fsetval). The select is the
   page's first control and its options are in EMS like the rest. A click
   cycles it, and a breakpoint on br_frepaint - where br_fsetval ends - reads
   the field's cells straight out of the frame through ES:DI. They must be
   the second option's text. Until br_fsetval gathered the value BEFORE
   br_docmap, the three event-time copies read the source after the document
   was mapped, and wrote the document's own bytes into the field.
3. VIEW SOURCE (SPEC.md 71.24): the page's source projected into the
   document - the length must be the source with each CR LF, CR or LF one
   D_BR, plus D_END, its 64 KiB seam included. It read [br_srcseg]:SI and
   wrote [br_docseg]:DI with 16-bit offsets until 71.24. Then Close Source
   re-parses, and the document is the page's again.
"""
import argparse
import atexit
import os
import re
import shutil
import socket
import sys
import tempfile
import threading

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
from brclick import browser_syms                       # noqa: E402

S = os88sym.linear
MACHINE = "os8088_5150_cga_lpt_ems"
# --net: the same legs over a NETWORK CARD on 86Box (OS88_EMU=86box): an XT
# with 640 KB and a VGA, the owner's machine, an NE1000 on slirp and the same
# Lo-tech board - 768 KiB still outgrows conventional memory there
# (os88box86api's `_ne1k`, `_ems`), booting `make etheremstest`'s disk, so
# ETHER.DRV has bound by the first paint and the page comes from 10.0.2.2.
# A "does it work" run in emulated seconds where the cable takes tens of
# minutes; MartyPC's cable machine keeps the timing (SPEC.md 71.18)
NETMACHINE = "xt_vga_ne1k_ems"
NET_ROW = 4
DRVR_SZ, DRVR_SEG = 16, 2
CP_I0Y, CP_IROWH, CP_IDRV = 6, 14, 2
CP_RX = 96
PATH = "/ems.htm"
BN_DONE = 6
D_BR, D_END = None, None            # read off the map

FIRST, SECOND = b"FIRSTOPT", b"SECONDOPT"


def page(size):
    head = (b"<html><head><title>Expanded</title></head><body>\r\n"
            b"<form><select name=s><option>" + FIRST + b"<option>" + SECOND
            + b"</select></form>\r\n")
    body = []
    n = 0
    while len(head) + sum(map(len, body)) < size:
        body.append(b"<p>Line %05d of a page held in expanded memory, past "
                    b"the conventional reserve.\r\n" % n)
        n += 1
    return head + b"".join(body) + b"</body></html>\r\n"


def prepage(size):
    """ONE <pre> line of `size` bytes. The first parse lays it out in a dozen
    lines (a PRE line ends only at each 48 KiB window), and View Source in
    thousands - enough to move the line table into EMS, which is what
    br_ldocback exists for (SPEC.md 71.18). Then Close Source must lay out the
    dozen again, not the heading alone."""
    head = b"<title>EMS pre</title><h1>EMS pre</h1><pre>\n"
    unit = b"EMS-FORMATTED-GROWTH-0123456789-"
    return (head + unit * ((size - len(head)) // len(unit))
            + b"\nEND\n</pre>\n")


def projection(src, d_br, d_end):
    """View Source's output, byte for byte: a CR LF, a CR or an LF is one
    D_BR, a tab a space, any other control or high byte `?`, then D_END."""
    out, i = bytearray(), 0
    while i < len(src):
        c = src[i]
        i += 1
        if c == 13:
            if i < len(src) and src[i] == 10:
                i += 1
            out.append(d_br)
        elif c == 10:
            out.append(d_br)
        elif c == 9:
            out.append(32)
        elif c < 32 or c > 126:
            out.append(63)
        else:
            out.append(c)
    out.append(d_end)
    return bytes(out)


def fsetval_jmp():
    """The offset of br_fsetval's `jmp br_frepaint`, out of the listing: the
    one place where the cells are written and ES:DI is just past them. A
    breakpoint on br_frepaint itself also stops for the caret on a press."""
    seen = False
    for line in open(os88build.at("build/browser.lst"), encoding="latin-1"):
        f = line.split()
        if "br_fsetval:" in line:
            seen = True
        elif seen and "jmp br_frepaint" in line and len(f) >= 3:
            return int(f[1], 16)
    return None                     # a build from before br_fsetval


def mapconst(*names):
    """Constants out of build/browser.map (browser_syms has the labels)."""
    out = {}
    for line in open(os88build.at("build/browser.map"), encoding="latin-1"):
        f = line.split()
        if len(f) >= 2 and f[-1] in names:
            out[f[-1]] = int(f[0], 16)
    return [out[n] for n in names]


def lst_label(proc, local):
    """The offset of PROC's LOCAL label (".bad") out of the listing: a label
    line carries no offset, so it is the next line's."""
    seen, want = False, False
    for line in open(os88build.at("build/browser.lst"), encoding="latin-1"):
        f = line.split()
        if not seen:
            seen = len(f) >= 2 and f[-1] == proc + ":"
        elif not want:
            want = len(f) >= 2 and f[1] == local + ":"
        elif len(f) >= 3 and re.fullmatch(r"[0-9A-F]{8}", f[1]):
            return int(f[1], 16)
    raise SystemExit("brems: no %s in %s" % (local, proc))


def lst_line(proc, text):
    """The offset of the first line after PROC: whose source has TEXT."""
    seen = False
    for line in open(os88build.at("build/browser.lst"), encoding="latin-1"):
        f = line.split()
        if not seen:
            seen = len(f) >= 2 and f[-1] == proc + ":"
        elif text in line and len(f) >= 3 and re.fullmatch(r"[0-9A-F]{8}", f[1]):
            return int(f[1], 16)
    raise SystemExit("brems: no %r in %s" % (text, proc))


def say(*a):
    print(*a)
    sys.stdout.flush()


class Server(threading.Thread):
    def __init__(self, body):
        threading.Thread.__init__(self, daemon=True)
        self.body = body
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.s.bind(("127.0.0.1", 0))
        self.port = self.s.getsockname()[1]
        self.s.listen(2)

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
                c.settimeout(None)      # the guest drains a big page slower
                c.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n"
                          b"Content-Length: %d\r\n\r\n" % len(self.body)
                          + self.body)
                # A HALF-CLOSE, then wait for the client's own close: a socket
                # closed with the guest's bytes still unread in it is RESET,
                # and a reset throws away what the guest had not yet read
                c.shutdown(socket.SHUT_WR)
                while c.recv(4096):
                    pass
            except OSError as e:          # say why: a page cut here is the
                say("server: %s" % e)    # harness's, not the Browser's
            finally:
                c.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kb", type=int, default=80, help="the page's size")
    ap.add_argument("--file", default=None,
                    help="serve this page instead of the generated one - the "
                         "768 KiB field fixtures of tools/brmemory.py, say; "
                         "the select leg runs only when it has a <select>")
    ap.add_argument("--dump", default=None,
                    help="write Close Source's re-parse output, flush by "
                         "flush, to this file (a diagnostic)")
    ap.add_argument("--machine", default=MACHINE,
                    help="another machine, for a control run without EMS")
    ap.add_argument("--pre", action="store_true",
                    help="serve prepage(--kb): one long <pre> line, the "
                         "line-table-in-EMS case (use --kb 300; ~12 minutes)")
    ap.add_argument("--net", action="store_true",
                    help="fetch over a network card on 86Box instead of the "
                         "cable (needs OS88_EMU=86box and make etheremstest)")
    a = ap.parse_args()
    if a.net:
        if os.environ.get("OS88_EMU") != "86box":
            sys.exit("brems: --net runs on 86Box: OS88_EMU=86box")
        if a.machine == MACHINE:
            a.machine = NETMACHINE
    ems_m = a.machine in (MACHINE, NETMACHINE) or "_ems" in a.machine
    body = (open(a.file, "rb").read() if a.file
            else prepage(a.kb * 1024) if a.pre else page(a.kb * 1024))
    fails = []
    sy = browser_syms()
    sysimg = "build/etherems360.img" if a.net else "build/emstest.img"
    for f in (sysimg, "build/brtest360.img"):
        if not os.path.exists(os88build.at(f)):
            sys.exit("brems: %s is missing - make %s browsertest"
                     % (f, "etheremstest" if a.net else "emstest"))
    srv = Server(body)
    srv.start()
    url = "http://%s:%d%s" % ("10.0.2.2" if a.net else "127.0.0.1",
                              srv.port, PATH)
    say("http server on %s, %d byte page" % (url, len(body)))

    box = P.SocketBox()
    ft = P.FileTree()
    ft.add(0, "READ.ME", content=b"a link volume\r\n")
    tmpd = tempfile.mkdtemp(prefix="brems-")
    atexit.register(shutil.rmtree, tmpd, True)
    # THE DISK MUST CARRY THE BUILD THE MAP DESCRIBES: browser_syms checks
    # build/browser.bin, and a brtest360.img older than it boots another
    # Browser whose every offset reads as plausible garbage
    if (os.path.getmtime(os88build.at("build/brtest360.img"))
            < os.path.getmtime(os88build.at("build/browser.o88"))):
        sys.exit("brems: build/brtest360.img is older than build/browser.o88 "
                 "- make browsertest")
    apps_img = os.path.join(tmpd, "brems360.img")
    shutil.copyfile(os88build.at("build/brtest360.img"), apps_img)
    sysdisk = (sysimg if ems_m
               else "build/os8088-360.img")    # the control run: no EMS.DRV
    with os88marty.launch(sysdisk, apps=apps_img,
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.kernel_up(m)
        os88marty.no_saver(m)
        mo = os88mouse.Mouse(marty=m)
        p = None if a.net else P.Partner(m)

        dispcp.open_drive(m, mo, S, lambda mm, **k: os88marty.ui_done(mm), "B")
        wins = dispcp.win_list(m, S)
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, mo, S, lambda mm, **k: os88marty.ui_done(mm),
                          wx, wy, "BROWSER.O88")
        wins2 = dispcp.win_list(m, S)
        if len(wins2) <= len(wins):
            sys.exit("brems: BROWSER.O88 did not open a window")
        bw = wins2[-1]
        rec = m.read(S("wm_wins") + bw * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        say("browser at %04X" % pseg)

        def rb(name, off=0):
            return m.readseg(pseg, sy[name] + off, 1)[0]

        def rw(name, off=0):
            d = m.readseg(pseg, sy[name] + off, 2)
            return d[0] | (d[1] << 8)

        # --- the link driver, as tests/brfetch.py loads it ------------------
        # (--net: ETHER.DRV is the disk's SYSTEM.CFG's, bound before the
        # first paint, so there is no Control Panel and no cable partner)
        if not a.net:
            mo.menu(8, 8, 8, 40)
            os88marty.settle(m)
            cp = None
            for w in dispcp.win_list(m, S):
                x, y, ww, hh = dispcp.win_rect(m, S, w)
                if ww >= 280 and hh >= 100:
                    cp = (x, y)
            if cp is None:
                sys.exit("brems: no Control Panel window")
            cx, cy = cp
            x0, y0 = cx + 1, cy + 18
            mo.click(x0 + 40, y0 + CP_I0Y + CP_IDRV * CP_IROWH + 7)
            os88marty.settle(m)
            vis = dispcp.drv_show(mo, x0, y0, NET_ROW,
                                  lambda: os88marty.settle(m))
            mo.to(x0 + CP_RX + 40, dispcp.drv_row_y(y0, vis))
            m.pause()
            p.sync()
            p.click_paused_release()
            p.hello(P.NET_VER_SOCK)
            p.allow(60000000)
            p.serve(ft, limit=24, idle=8000000, sox=box)
            row = m.read(S("drv_tab") + NET_ROW * DRVR_SZ + DRVR_SEG, 2)
            if not (row[0] | (row[1] << 8)):
                sys.exit("brems: the link driver did not load")
        ems = m.read(S("drv_tab") + 6 * DRVR_SZ + DRVR_SEG, 2)
        if ems_m and not (ems[0] | (ems[1] << 8)):
            sys.exit("brems: EMS.DRV is not loaded - is the machine's board "
                     "there? (`make marty` stages os8088_5150_cga_lpt_ems; "
                     "86Box: os88box86api's `_ems`)")
        if not a.net:
            m.run()
            mo.click(cx + 8, cy + 9)
            os88marty.settle(m)

        # --- fetch -----------------------------------------------------------
        BR_LPAD, BR_TBH, BR_TBG, BR_LBAR = 3, 13, 1, 15
        bx0, by0, bww, bhh = dispcp.win_rect(m, S, bw)
        mo.click(bx0 + bww - 40, by0 + dispcp.TITLE_H + 1 + BR_LPAD + BR_TBH
                 + BR_TBG + BR_LBAR // 2)
        os88marty.settle(m)
        if a.net:
            # 86Box's automation puts a key on the XT's keyboard port at
            # once and the port holds ONE: typed as a string, `http://10.41mh`
            # is what arrived. A character at a time, each taken first
            for ch in "\b" * 12 + url:
                os88marty.text_taken(m, ch, ui=False)
        else:
            m.type_text("\b" * 12 + url)
        os88marty.settle(m)
        if a.net:                       # the card: nothing to serve here,
            m.key("Enter")               # the host's server answers slirp
            say("wire: the network card, %s" % url)
        else:
            m.pause()
            p.sync()
            m.key("Enter")
            p.allow(60000000000)         # a 768 KiB page is ~200,000 commands
            served = []
            try:
                served = p.serve(ft, limit=400000, idle=400000000, sox=box)   # an EMS growth is seconds of silence
            except P.LinkTimeout as e:
                fails.append("the wire stalled mid-fetch: %s" % e)
            say("wire: %d commands served; the socket box's last words: %s"
                % (len(served), " | ".join(box.log[-6:])[:600]))
            m.run()
        def state():
            return ("nstate %d fin %d parse_started %02X parsing %d nlen %d "
                    "srclen %d doclen %d docfull %d trunc %d nlines %d "
                    "srcEMS %d docEMS %d"
                    % (rb("br_nstate"), rb("br_fin"), rb("br_parse_started"),
                       rb("br_parsing"), rw("br_nlen") | rw("br_nlen_hi") << 16,
                       rw("br_srclen") | rw("br_srclen_hi") << 16,
                       rw("br_doclen") | rw("br_doclen_hi") << 16,
                       rb("br_docfull"), rb("br_trunc"), rw("br_nlines"),
                       rw("br_srcemshnd"), rb("br_docems")))
        def snap(tag):
            try:
                w, h, d = m.fbuf(None)
                os88marty.write_png_rgb("/tmp/brems_%s.png" % tag, w, h, d)
                now = dispcp.win_list(m, S, check=False)
                segs = []
                for i in now:
                    r = m.read(S("wm_wins") + i * dispcp.WIN_SIZE,
                               dispcp.WIN_SIZE)
                    segs.append("%d:%04X" % (i, r[22] | (r[23] << 8)))
                say("   %s: windows %s, shot /tmp/brems_%s.png"
                    % (tag, " ".join(segs), tag))
            except Exception as ex:
                say("   %s: snap failed: %r" % (tag, ex))
        say("after the wire: " + state())
        snap("wire")
        try:
            os88marty.until(m, lambda _m: rb("br_nstate") in (6, 7),
                            "the page to settle - a FREEZE if it never does",
                            limit=240 if len(body) < 200000 else 2400)
        except os88marty.MartyError as e:
            say("NEVER SETTLED: " + state())
            say("   CS:IP %(cs)04X:%(ip)04X" % m.regs())
            snap("never")
            for f in fails:
                say("  FAIL " + f)
            say("brems: FAILED (the page never settled)")
            return 1
        os88marty.settle(m)
        srclen = rw("br_srclen") | rw("br_srclen_hi") << 16
        say("fetch end: fin %d, nlen %d, srckb %d, src EMS pages %d, trunc %d, "
            "msg %04X" % (rb("br_fin"), rw("br_nlen") | rw("br_nlen_hi") << 16,
                          rw("br_srckb"), rw("br_srcemspages"), rb("br_trunc"),
                          rw("br_nmsg")))
        doclen = rw("br_doclen") | rw("br_doclen_hi") << 16
        if srclen != len(body) and not rb("br_trunc"):
            fails.append("the page kept %d of %d bytes and does not say it was "
                         "cut" % (srclen, len(body)))
        say("state %d, source %d of %d, document %d, src EMS handle %d, "
            "doc in EMS %d, truncated %d"
            % (rb("br_nstate"), srclen, len(body), doclen, rw("br_srcemshnd"),
               rb("br_docems"), rb("br_trunc")))

        # --- 1. in EMS -------------------------------------------------------
        if rb("br_nstate") != BN_DONE:
            fails.append("the fetch did not finish (state %d)" % rb("br_nstate"))
        # what must spill: past 64 KB on the cable machine's 256 KB, past
        # 512 KB on the --net machine's 640 KB (its reserve holds more)
        spill = 64 * 1024 if a.machine == MACHINE else 512 * 1024
        if (ems_m and len(body) >= spill and a.file
                and rw("br_srcemshnd") and not rb("br_docems")):
            # a --file page decides its own document; one that renders small
            # (sourcepressure: 768 KiB of source, 241 bytes laid out) keeps
            # the document in conventional memory, and that is not a fault
            print("note: the document (%d bytes) stayed in conventional "
                  "memory: the shared-frame legs below test the source only"
                  % doclen)
        elif (ems_m and len(body) >= spill
                and (not rw("br_srcemshnd") or not rb("br_docems"))):
            fails.append("the page is not in EMS (source handle %d, document "
                         "%d): nothing below tests the shared frame"
                         % (rw("br_srcemshnd"), rb("br_docems")))

        # --- 2. a select's next option, through br_fsetval -------------------
        # The select is the first control; find it by clicking along the first
        # row until br_frepaint is reached, and read the cells it was handed
        cxp, cyp = rw("br_cx"), rw("br_cy")
        jmp = fsetval_jmp() if b"<select" in body else 0
        got = None
        if jmp == 0:
            say("no <select> on this page: the select leg is not run")
        elif jmp is None:
            fails.append("this build has no br_fsetval: the select leg is not run")
        bp = {"type": "execseg", "seg": pseg, "off": jmp or 0}
        for col in range(0, 24 if jmp else 0):
            m.breakpoints([bp])
            try:                        # the click is handled on the PRESS, so
                mo.click(cxp + col * 8 + 3, cyp + 3)   # a hit stops the machine
            except os88marty.MartyError:               # before the release
                pass
            if m.status().get("state") == "breakpoint" or m.wait_stop(4) == "breakpoint":
                r = m.regs()
                es, di, bxr = r["es"], r["di"], r["bx"]
                w = m.readseg(pseg, bxr + 4, 1)[0]
                got = bytes(m.readseg(es, (di - w) & 0xFFFF, w))
                say("br_fsetval at column %d: ES:DI %04X:%04X, %d cells %r"
                    % (col, es, di, w, got))
            m.breakpoints([])
            m.run()
            os88marty.settle(m)
            if got is not None:
                break
        if got is None and jmp:
            fails.append("no click on the first row reached br_fsetval")
        elif got is not None and got != SECOND[:len(got)]:      # the field is as wide as it is
            fails.append("the select's field holds %r, not %r - the document's "
                         "bytes were copied in, not the option's"
                         % (got, SECOND[:len(got)]))

        # --- 3. View Source, then Close Source -------------------------------
        def collect(path):
            """Every br_flushbuf's bytes until the re-parse is done."""
            out = bytearray()
            bpf = {"type": "execseg", "seg": pseg, "off": sy["br_flushbuf"]}
            m.breakpoints([bpf] + traps)
            idle = 0
            while idle < 3:
                if m.status().get("state") != "breakpoint":
                    if m.wait_stop(5) != "breakpoint":
                        idle += 1
                        continue
                idle = 0
                r = m.regs()
                if traps and r["ip"] == traps[-1]["off"]:
                    pos = rw("br_srcpos") | rw("br_srcpos", 2) << 16
                    n = r["bx"]
                    got = bytes(m.readseg(pseg, sy["br_rxb"], n))
                    want = body[pos:pos + n]
                    if got != want and stagebad[0] < 6:
                        stagebad[0] += 1
                        k = next((j for j in range(n) if got[j:j+1] != want[j:j+1]), n)
                        say("  STAGE at source %d (%d bytes, out %d): differs at +%d "
                            "got %r want %r" % (pos, n, len(out), k,
                                                got[k:k+16], want[k:k+16]))
                    m.run()
                    continue
                if r["ip"] != sy["br_flushbuf"]:
                    say("  TRAP at %04X: ax %04X bx %04X cx %04X dx %04X "
                        "spoint %04X:%04X srcpages %d docpages %d out %d"
                        % (r["ip"], r["ax"], r["bx"], r["cx"], r["dx"],
                           rw("br_spoint", 2), rw("br_spoint"),
                           rw("br_srcemspages"), rw("br_emspages"), len(out)))
                    m.run()
                    continue
                n = rb("br_on")
                out += bytes(m.readseg(pseg, sy["br_obuf"], n))
                m.run()
            m.breakpoints([])
            m.run()
            open(path, "wb").write(bytes(out))
            say("dumped %d re-parse bytes to %s" % (len(out), path))

        traps = []
        stagebad = [0]
        if a.dump:
            for proc, local in (("br_srcems_map", ".bad"), ("br_docmap", ".mapbad")):
                traps.append({"type": "execseg", "seg": pseg,
                              "off": lst_label(proc, local)})
            stage = lst_line("br_srcmap", "mov si, br_rxb")
            traps.append({"type": "execseg", "seg": pseg, "off": stage})
            say("traps at %s" % ", ".join("%04X" % b["off"] for b in traps))

        def ctx(item_dy, dump=None):
            bgx, bgy = cxp + rw("br_cols") * 8 - 12, cyp + 3
            mo.to(bgx, bgy)
            os88marty.settle(m)
            mo._edge(True, btn=2)
            mo._edge(False, btn=2)
            os88marty.settle(m)
            if rb("br_ctxopen") != 1 or rb("br_ctxkind") != 0:
                fails.append("the right button did not open the page's menu")
                return False
            if dump:
                bpf = {"type": "execseg", "seg": pseg, "off": sy["br_flushbuf"]}
                m.breakpoints([bpf] + traps)
                try:
                    mo.click(cxp + rw("br_ctxx") + 8, cyp + rw("br_ctxy") + item_dy)
                except os88marty.MartyError:
                    pass
                collect(dump)
            else:
                mo.click(cxp + rw("br_ctxx") + 8, cyp + rw("br_ctxy") + item_dy)
            os88marty.settle(m)
            try:
                os88marty.until(m, lambda _m: rb("br_ctxopen") == 0,
                                "the menu to close", limit=120)
            except os88marty.MartyError:
                say("MENU STAYED OPEN: " + state() + " ctx at %d,%d"
                    % (rw("br_ctxx"), rw("br_ctxy")))
                snap("menu")
                fails.append("the page's menu did not close on its item")
                return False
            # the action runs AFTER the menu closes: a re-parse of a big page
            # is seconds of 8088, so wait for it to finish (2 EOF, 3 cut)
            os88marty.until(m, lambda _m: rb("br_viewsrc") == 1 or (
                                rb("br_parsing") == 0
                                and (rb("br_parse_started") & 0x7F) in (2, 3)),
                            "the action to finish", limit=1200)
            last = None
            while True:                 # and the document to stop changing
                os88marty.settle(m)
                now = (rw("br_doclen"), rw("br_doclen_hi"), rw("br_nlines"))
                if now == last:
                    break
                last = now
            return True

        # THE LAYOUT RUNS AFTER br_nstate says done, in slices on the worker,
        # and the Browser ignores clicks and right clicks until it ends
        # (br_onclick, br_onrclk): a right click before then opens nothing
        os88marty.until(m, lambda _m: not rb("br_parse_started") & 0x80,
                        "the page's layout to finish", limit=600)
        say("layout done: " + state())

        def emsdump(tag):               # EMS.DRV's handles and our arenas
            if not os.environ.get("BREMS_EMSDUMP"):
                return
            drow = m.read(S("drv_tab") + 6 * DRVR_SZ + DRVR_SEG, 2)
            dseg = drow[0] | drow[1] << 8
            ln = m.readseg(dseg, 0x2CD, 16)
            bo = m.readseg(dseg, 0x2DD, 16)
            hs = ["h%d:%d@%d" % (i + 1, ln[2 * i] | ln[2 * i + 1] << 8,
                                 bo[2 * i]) for i in range(8)
                  if ln[2 * i] | ln[2 * i + 1] << 8]
            ars = []
            for nm in ("br_srcar", "br_docar", "br_linear"):
                if nm not in sy:
                    continue
                n = rw(nm)
                ars.append("%s[%s]" % (nm[3:], " ".join(
                    "h%d/%d" % (rw(nm, 4 + 2 * i), rw(nm, 20 + 2 * i))
                    for i in range(min(n, 8)))))
            say("   EMS %s: %s | %s" % (tag, " ".join(hs), " ".join(ars)))
        emsdump("before trim")
        laid = rw("br_doclen") | rw("br_doclen_hi") << 16
        if laid != doclen:              # the layout keeps the prefix whose lines
            say("note: the layout kept %d of the %d document bytes (lines "   # fit
                "full %d): Close Source is held to that" % (laid, doclen,
                                                           rb("br_lfull")))
            doclen = laid
        os88marty.settle(m)                     # ...and the trim on its wake
        emsdump("after trim")
        need = (srclen + 16383) // 16384
        say("EMS pages after the trim: source %d (needs %d), document %d "
            "(needs %d)" % (rw("br_srcemspages"), need if rw("br_srcemshnd")
                            else 0, rw("br_emspages"),
                            (doclen + 16383) // 16384 if rb("br_docems") else 0))
        nl_page = rw("br_nlines")
        viewfile = (a.dump or os.path.join(tmpd, "re")) + ".view"
        if ctx(18, viewfile):
            vlen = rw("br_doclen") | rw("br_doclen_hi") << 16
            want = projection(body[:srclen], *mapconst("D_BR", "D_END"))
            got = open(viewfile, "rb").read()
            say("View Source: br_viewsrc %d, document %d bytes, want %d, "
                "truncated %d" % (rb("br_viewsrc"), vlen, len(want),
                                  rb("br_docfull")))
            if rb("br_viewsrc") != 1:
                fails.append("View Source did not switch to the source")
            elif got != want and not (rb("br_docfull") and want.startswith(got[:-1])):
                k = next((j for j in range(min(len(got), len(want)))
                          if got[j] != want[j]), min(len(got), len(want)))
                fails.append("View Source differs from the source at byte %d "
                             "of %d (got %r, want %r)" % (k, len(want),
                                                          got[k:k + 24],
                                                          want[k:k + 24]))
            emsdump("after View Source")
            nl0 = rw("br_nlines")
            if ctx(18, a.dump):
                say("lines: %d in the source view, %d after" % (nl0, rw("br_nlines")))
                back = rw("br_doclen") | rw("br_doclen_hi") << 16
                say("Close Source: br_viewsrc %d, document %d bytes (was %d)"
                    % (rb("br_viewsrc"), back, doclen))
                if rb("br_viewsrc") != 0 or back != doclen:
                    fails.append("Close Source left %d bytes, not the page's %d"
                                 % (back, doclen))
                try:                    # the layout runs in slices after it
                    os88marty.until(m, lambda _m: rw("br_nlines") == nl_page
                                    and not rb("br_parse_started") & 0x80,
                                    "the source's layout to finish", limit=180)
                except Exception:
                    pass
                if rw("br_nlines") != nl_page:
                    say("   after Close: " + state() + " lfull %d"
                        % rb("br_lfull"))
                    try:                # a slow layout, or a stopped one?
                        os88marty.until(m, lambda _m: rw("br_nlines") == nl_page,
                                        "the layout, longer", limit=1200)
                    except Exception:
                        pass
                    say("   1200 s later: " + state() + " lfull %d"
                        % rb("br_lfull"))
                    snap("close")
                if rw("br_nlines") != nl_page:
                    fails.append("Close Source laid out %d lines, not the "
                                 "page's %d" % (rw("br_nlines"), nl_page))
    for f in fails:
        say("  FAIL " + f)
    say("brems: " + ("ok" if not fails else "FAILED"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
