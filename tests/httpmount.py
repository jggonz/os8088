#!/usr/bin/env python3
"""httpmount - SPEC.md 122.7: HTTPFS.DRV mounts a web server as a drive.

    make httpmounttest && python3 tests/httpmount.py

QEMU BY NAME: the drive's sockets are ETHER.DRV's and MartyPC has no NIC of
any kind (docs/TESTING.md's list, item 5). QEMU's `ne2k_isa` on `-netdev
user` reaches the host at 10.0.2.2, and the host side is tools/os88httpdrv.py's
own server, run in this process on a fixture tree this test builds - so every
assertion is about bytes the test chose, and every request the machine made
is in the server's log.

build/httpmount.img wants ETHER.DRV and HTTPFS.DRV and carries HTTPDRV.CFG
naming http://10.0.2.2:8095/fix/ with bit 0 set, so the drive is on the
desktop before the first paint, LAZY: nothing is fetched until it is opened.

  1. The row is loaded, a volume registered, and the server has seen NO
     request yet (SPEC.md 122.5: the boot fetches nothing).
  2. The drive's zone opens it: ONE request, for OS88.IDX, and the root
     lists what os88httpdrv.index_parse lists - names, folders, packages,
     and `A#B.TXT`, whose `#` must go on the wire as %23.
  3. DOCS, DOCS\\DEEP and `..` twice come back with NO further request: a
     listing is answered out of the index (SPEC.md 122.3).
  4. MINES.O88 launches off the drive (FSV_READ, one GET, and the loader's
     parts path).
  5. BIG.BIN - 100,001 bytes, over 64 KB and odd - copied to B: with Edit >
     Copy and Edit > Paste is the reference's bytes (FSV_READAT with Range:).
  6. ...and BIG2.BIN, the same with the server IGNORING Range: (a 200 for
     every read-at: the offset drained, SPEC.md 122.4) - the same bytes.
  7. A write is refused: Edit > Paste of BIG.BIN onto the HTTP drive asks
     to replace the one there, and the yes ends in FERR_WPROT - the delete
     and the write are refused by the driver, in FAT's words.
  8. PACKED.TXT, a 'CZ' file served as it is stored, opens in Note Pad as
     its UNPACKED text: no hint exists on a redirected volume, so this is the
     read's own sniff (SPEC.md 20.14.6.1) - why the host tool serves a
     release's bytes unexpanded (SPEC.md 122.7).

`--name` runs the same legs with the address http://localhost:8095/fix/,
which QEMU's slirp resolves through the host to 127.0.0.1 - this server. N:
ETHER.DRV's name cache holds `localhost` after the open, and its DNS
transaction id (random per query) has not moved by the end, across every
later connection (SPEC.md 72.5.1).

Break on purpose: drop the `[hf_skip]` load in hf_xfer's 200 arm and leg 6
reads the file's FIRST bytes for every chunk. (A 206's start is taken on
trust since size round 2, SPEC.md 122.4 - leg 5 is what a range-honouring
server delivers.)
"""
import atexit
import os
import re
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
import os88build                                              # noqa: E402
import os88drvsym                                             # noqa: E402
import os88fat                                                # noqa: E402
import os88geom as geom                                       # noqa: E402
import os88httpdrv                                            # noqa: E402
import os88qemu                                               # noqa: E402
import os88ui                                                 # noqa: E402
import rdxms                                                  # noqa: E402
import xmcheck                                                # noqa: E402

HF_ROW = 8                      # drv_tab's HTTPFS.DRV row on kern_big
DRVR_SIZE, DRVR_ERR = 16, 14
PORT = 8095
# --name: the address is http://localhost:8095/fix/. QEMU's slirp resolves it
# through the host to 127.0.0.1, and a guest connection there leaves by the
# gateway and reaches the host's own loopback - this server. It is the one
# way a row here can make ETHER.DRV look a NAME up (SPEC.md 72.5.1).
BYNAME = "--name" in sys.argv[1:]
BIGLEN = 100001

# A 'CZ' file (SPEC.md 20.14), SERVED AS IT IS STORED: no directory entry on a
# redirected volume carries a hint, so Note Pad gets the right text only if
# the read sniffs the file's own header (20.14.6.1). LF-only, so np_load's
# CRLF fold leaves it alone and np_len is its length exactly.
PLAIN = b"".join(b"line %04d of a packed note, served over HTTP\n" % i
                 for i in range(120))


def _pack():
    import os88lz
    out, ok = os88lz.cz_wrap(PLAIN)
    assert ok and out[:2] == b"CZ", "the fixture did not compress"
    return out


PACKED = _pack()


def pattern(n, seed):
    out = bytearray(n)
    x = seed
    for i in range(n):
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        out[i] = (x >> 16) & 0xFF
    return bytes(out)


def fixture():
    return {
        "README.TXT": b"An os8088 drive, over HTTP.\r\n",
        "A#B.TXT": b"a name with a hash in it\r\n",
        "BIG.BIN": pattern(BIGLEN, 1),
        "BIG2.BIN": pattern(BIGLEN, 2),
        "APPS": {"MINES.O88": open(os88build.at("build/mines.o88"), "rb").read()},
        "DOCS": {"NOTE.TXT": b"one\r\n", "DEEP": {"DEEPER.TXT": b"two\r\n"}},
        "EMPTY": {},
        "PACKED.TXT": PACKED,
        "HIDDEN.SYS": b"hidden and system: listed by no Disk window\r\n",
    }


ATTRS = {"HIDDEN.SYS": 0x06}    # SPEC.md 62.9.17.1: the root lists without it


class Stop(Exception):
    """A leg that makes the rest meaningless has already said why."""


class Log(object):
    """Every request the server answered, by path."""

    def __init__(self):
        self.lines = []
        self.lock = threading.Lock()

    def add(self, s):
        with self.lock:
            self.lines.append(s)

    def ports(self, since=0):
        """The client ports of the GETs from `since` on: one per CONNECTION."""
        with self.lock:
            return [int(m.group(1)) for m in
                    (re.search(r'"GET .* port (\d+)$', s) for s in self.lines)
                    if m][since:]

    def gets(self):
        with self.lock:
            return [m.group(1) for m in
                    (re.search(r'"GET (\S+) HTTP', s) for s in self.lines) if m]


def dns_stub(log):
    """A resolver for --name: every A question answered 127.0.0.1, each one
    LOGGED, so a lookup the cache should have spared is counted rather than
    inferred. UDP on the host's 127.0.0.1:53 - QEMU's slirp is pointed at it
    through a private /etc/resolv.conf (QH, BYNAME), because the container's
    own nameservers are not reachable and nothing here may edit the real one."""
    import socket
    sk = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sk.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    os88qemu.lease_port(53)     # a fixed host port (SOAK-PARALLEL.md 17.3)
    sk.bind(("127.0.0.1", 53))

    def run():
        while True:
            try:
                q, addr = sk.recvfrom(512)
            except OSError:
                return
            if len(q) < 12:
                continue
            i, labels = 12, []
            while i < len(q) and q[i]:
                labels.append(q[i + 1:i + 1 + q[i]].decode("latin-1"))
                i += 1 + q[i]
            qend = i + 5
            log.add("DNS %s" % ".".join(labels))
            ans = (q[:2] + b"\x81\x80" + q[4:6] + b"\x00\x01\x00\x00\x00\x00"
                   + q[12:qend] + b"\xc0\x0c\x00\x01\x00\x01\x00\x00\x00\x3c"
                   + b"\x00\x04" + bytes([127, 0, 0, 1]))
            sk.sendto(ans, addr)
    threading.Thread(target=run, daemon=True).start()
    return sk


def server(root, log):
    class H(os88httpdrv.Handler):
        timeout = 3             # idle connections closed between legs, so
                                # the drive's reopen path runs too

        def log_message(self, fmt, *a):
            log.add((fmt % a) + " [%s] port %d" % (self.headers.get("Range", ""),
                                                    self.client_address[1]))
    h = lambda *a, **k: H(*a, directory=root, **k)            # noqa: E731
    # the guest reaches this at 10.0.2.2:PORT, baked into HTTPDRV.CFG - the
    # one shared thing left, so it is LEASED (SOAK-PARALLEL.md 17.3)
    os88qemu.lease_port(PORT)
    httpd = os88httpdrv.Server(("127.0.0.1", PORT), h)
    httpd.ranges = True
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd


class QH(object):
    """A private QEMU: the gate disk in A:, a blank scratch floppy in B:, and
    an NE2000 on slirp."""

    def __init__(self, tmp, sysimg, bimg):
        self.sock = os.path.join(tmp, "qmp.sock")
        self.pid = os.path.join(tmp, "qemu.pid")
        self.pidn = None
        atexit.register(self.close)
        os88qemu.own(self.pid, self.sock)
        pre = []
        if BYNAME:
            rc = os.path.join(tmp, "resolv.conf")
            open(rc, "w").write("nameserver 127.0.0.1\n")
            pre = ["unshare", "-m", "sh", "-c",
                   'mount --bind "$0" /etc/resolv.conf && exec "$@"', rc]
        os88qemu.start(
            pre + ["qemu-system-i386", "-machine", "pc,vmport=off", "-m", "4",
             "-drive", "file=%s,format=raw,if=floppy" % sysimg,
             "-drive", "file=%s,format=raw,if=floppy,index=1" % bimg,
             "-netdev", "user,id=n0",
             "-device", "ne2k_isa,netdev=n0,iobase=0x300,irq=3",
             "-boot", "a", "-chardev", "msmouse,id=m0",
             "-serial", "chardev:m0", "-display", "none",
             "-qmp", "unix:%s,server,nowait" % self.sock,
             "-daemonize", "-pidfile", self.pid], check=True)
        self.pidn = int(open(self.pid).read().strip())

    def close(self):
        if self.pidn:
            try:
                os.kill(self.pidn, signal.SIGTERM)
                os88qemu.gone(self.pidn)
            except OSError:
                pass
            self.pidn = None

    def hmp(self, *cmds):
        return xmcheck.qmp(self.sock, *cmds)

    def read(self, linear, n):
        return bytes(xmcheck.read_bytes(self.sock, linear, n))

    def readseg(self, seg, off, n):
        return self.read((seg << 4) + off, n)

    def mouse(self, *args):
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "mouse.py"), self.sock]
                       + [str(a) for a in args], check=True,
                       capture_output=True, cwd=ROOT)

    def done(self, cap=2.0):
        os88qemu.ui_done(self, xmcheck.sym, cap=cap)

    def click(self, x, y, cap=2.0):
        """CLOSED LOOP (os88qemu.mouse_click: every move and both edges
        confirmed on mouse_x/mouse_btn), where it was tools/mouse.py's
        dead reckoning - which a starved guest took somewhere else
        (docs/plans/SOAK-PARALLEL.md 17.5's httpmountname, whose
        double-click xmcheck.goto already fixed; this is the single click
        beside it). tools/mouse.py stays the fallback for a kernel this
        process cannot resolve."""
        try:
            os88qemu.mouse_click(self, xmcheck.sym, self.hmp, x, y)
        except os88qemu.GuestStopped:
            raise
        except Exception:                   # noqa: BLE001 - no map: reckon
            self.mouse("click", x, y)
        self.done(cap)

    def need(self, cond, what, secs=30):
        if not os88qemu.acted(self, cond, secs=secs, what=what, poll=0.1):
            raise RuntimeError("timed out waiting for %s" % what)


def front_on(ui, drv):
    f = ui.front()
    if f is None or not f.visible:
        return None
    fs = ui.fs_of(f)
    return f if fs is not None and fs[0] == drv and fs[2] == 1 else None


def want_names(recs, folder):
    """What the index lists for `folder` ('' or 'DOCS/DEEP'), by the reference
    reader: (name, type) with type 2 a folder, 1 a package, 0 a file."""
    paths = os88httpdrv.index_paths(recs)
    idx = next(i for i, p in paths.items() if p == folder)
    _n, _a, _p, first, count = recs[idx]
    out = []
    for i in range(first, first + count):
        name, attr = recs[i][0], recs[i][1]
        if attr & 0x06:
            continue                    # hidden or system: FSV_LIST skips it
        pn = os88httpdrv.pretty(name)
        t = 2 if attr & 0x10 else (1 if pn.endswith(".O88") else 0)
        out.append((pn, t))
    return sorted(out)


def got_names(ui, w):
    return sorted((n, t) for n, t in ui.listing(w) if n != "..")


def compare(bad, tag, ui, w, recs, folder):
    want, got = want_names(recs, folder), got_names(ui, w)
    print("   %s %s lists %d entries%s" % (
        tag, folder or "the root", len(got),
        "" if got == want else " - NOT the index's %d" % len(want)))
    if got != want:
        bad.append("%s %s: guest %r, index %r" % (tag, folder or "root", got, want))


def open_row(q, ui, w, name, drv, what):
    i, _ = ui.entry(name, w)
    xmcheck.dblclick(q.sock, *ui.row_xy(w, i - ui.scroll(w)))
    q.need(lambda: front_on(ui, drv) is not None and
           (what is None or what(front_on(ui, drv))), "%s to open" % name)
    q.done()
    return front_on(ui, drv)


def raise_vol(q, ui, vol):
    for o in ui.windows():
        fs = ui.fs_of(o)
        if o.visible and fs is not None and fs[0] == vol:
            q.click(o.x + 30, o.y + 4)
            return


def copy_to_b(q, ui, S, vol, name, bad, tag):
    if not front_on(ui, vol):
        raise_vol(q, ui, vol)
        q.need(lambda: front_on(ui, vol) is not None, "the drive in front")
    w = front_on(ui, vol)
    i, _ = ui.entry(name, w)
    q.click(*ui.row_xy(w, i - ui.scroll(w)))
    rdxms.menu_pick(q, ui, "Edit", "Copy")
    bx, by = geom.drive_pt(q, "B", S)
    xmcheck.dblclick(q.sock, bx, by)
    q.need(lambda: front_on(ui, 1) is not None, "B:'s window", 30)
    q.done()
    rdxms.menu_pick(q, ui, "Edit", "Paste")
    q.need(lambda: q.read(S("fcp_busy"), 1)[0] == 0, "the paste of " + name, 600)
    q.done(4.0)
    return q.read(S("fcp_err"), 1)[0]


def main():
    bad = []
    try:
        syms = os88drvsym.symbols("httpfs.bin")
    except os88drvsym.Mismatch as e:
        sys.exit("httpmount: %s" % e)
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    q = httpd = None
    base = 0
    log = Log()
    try:
        tree = fixture()
        web = os.path.join(tmp, "web")
        fix = os.path.join(web, "fix")
        os.makedirs(fix)
        os88httpdrv.write_tree(tree, fix)
        idx = os88httpdrv.index_build(tree, 0, ATTRS)
        open(os.path.join(fix, os88httpdrv.IDX_NAME), "wb").write(idx)
        os88httpdrv.check(fix)
        recs = os88httpdrv.index_parse(idx)
        httpd = server(web, log)
        dns = dns_stub(log) if BYNAME else None

        sysimg = os.path.join(tmp, "sys.img")
        shutil.copy(os88build.at("build/httpmount.img"), sysimg)
        if BYNAME:                      # the same disk, the address by NAME
            cfg = os.path.join(tmp, "HTTPDRV.CFG")
            u = b"http://localhost:%d/fix/" % PORT
            open(cfg, "wb").write(b"HD" + bytes([1, 1]) + u + bytes(96 - len(u)))
            subprocess.run([sys.executable, "tools/os88fat.py", "del", sysimg,
                            "HTTPDRV.CFG"], check=True, capture_output=True)
            subprocess.run([sys.executable, "tools/os88fat.py", "add", sysimg,
                            cfg, "HTTPDRV.CFG"], check=True, capture_output=True)
        bimg = os.path.join(tmp, "b.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", bimg,
                        "--size", "1440"], check=True, capture_output=True)
        q = QH(tmp, sysimg, bimg)
        os88qemu.acted(q, lambda: os.path.exists(q.sock), secs=30,
                       what="the QMP socket")
        xmcheck.wait_desktop(q.sock, "httpmount")
        S = xmcheck.sym

        # --- 1: loaded, a volume, and not one request --------------------
        row = q.read(S("drv_tab") + HF_ROW * DRVR_SIZE, DRVR_SIZE)
        seg, err = struct.unpack_from("<H", row, geom.DRVR_SEG)[0], row[DRVR_ERR]
        if not seg:
            bad.append("1. HTTPFS.DRV is not loaded (DRVR_ERR %d)" % err)
            raise Stop()
        base = geom.drv_base(seg)
        vol = q.read(base + syms["hf_vol"], 1)[0]
        url = q.read(base + syms["hf_url"], 96).split(b"\0")[0]
        print("   1. HTTPFS.DRV at %04Xh: volume %d, address %r, %d request(s)"
              % (seg, vol, url.decode("latin-1"), len(log.gets())))
        if vol == 0xFF:
            bad.append("1. HTTPDRV.CFG's address was not mounted at READY")
            raise Stop()
        if log.gets():
            bad.append("1. the boot fetched %r - the mount is meant to be lazy"
                       % log.gets())

        # --- 2: open it: one request, and the root ------------------------
        ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)
        x, y = geom.drive_pt(q, chr(ord("A") + vol), S)
        xmcheck.dblclick(q.sock, x, y)
        q.need(lambda: front_on(ui, vol) is not None, "the drive's window", 90)
        q.done()
        w = front_on(ui, vol)
        gets = log.gets()
        print("   2. opened with %r" % gets)
        if gets != ["/fix/OS88.IDX"]:
            bad.append("2. opening the drive asked %r, not the index alone" % gets)
        compare(bad, "2.", ui, w, recs, "")
        if BYNAME:
            esyms = os88drvsym.symbols("ether.bin")
            erow = q.read(S("drv_tab") + 2 * DRVR_SIZE, DRVR_SIZE)
            ebase = geom.drv_base(struct.unpack_from("<H", erow, geom.DRVR_SEG)[0])
            id0 = q.read(ebase + esyms["dns_id"], 2)
            cnm = q.read(ebase + esyms["dns_cnm"], 16).split(b"\0")[0]
            print("   N. the name looked up once: cache %r" % cnm)
            if cnm != b"localhost":
                bad.append("N. ETHER.DRV's name cache holds %r, not localhost" % cnm)

        # --- 3: down two, up two, and no request --------------------------
        n0 = len(log.gets())
        w = open_row(q, ui, w, "DOCS", vol, None)
        compare(bad, "3.", ui, w, recs, "DOCS")
        w = open_row(q, ui, w, "DEEP", vol, None)
        compare(bad, "3.", ui, w, recs, "DOCS/DEEP")
        w = open_row(q, ui, w, "..", vol,
                     lambda f: any(n == "DEEP" for n, _ in ui.listing(f)))
        w = open_row(q, ui, w, "..", vol,
                     lambda f: any(n == "DOCS" for n, _ in ui.listing(f)))
        compare(bad, "3. (..)", ui, w, recs, "")
        extra = log.gets()[n0:]
        print("   3. walking the folders asked %d request(s)" % len(extra))
        if extra:
            bad.append("3. a listing went to the wire: %r" % extra)

        # --- 4: a package off the drive -----------------------------------
        w = open_row(q, ui, w, "APPS", vol, None)
        before = set(o.i for o in ui.windows())
        n0 = len(log.gets())
        i, _ = ui.entry("MINES.O88", w)
        xmcheck.dblclick(q.sock, *ui.row_xy(w, i - ui.scroll(w)))
        opened = os88qemu.acted(
            q, lambda: any(o.i not in before and o.visible
                           for o in ui.windows()), secs=60, what="Minesweeper")
        ports = log.ports(n0)
        print("   4. MINES.O88 launches off the drive: %s, with %r over %d "
              "connection(s)" % (opened, log.gets()[n0:], len(set(ports))))
        if len(ports) > 1 and len(set(ports)) != 1:
            bad.append("4. the launch's %d requests took %d connections - the "
                       "kept one was not reused" % (len(ports), len(set(ports))))
        if not opened:
            bad.append("4. MINES.O88 off the drive did not open")
        raise_vol(q, ui, vol)
        q.need(lambda: front_on(ui, vol) is not None, "the drive in front")
        w = open_row(q, ui, front_on(ui, vol), "..", vol,
                     lambda f: any(n == "APPS" for n, _ in ui.listing(f)))

        # --- 5 and 6: BIG.BIN with Range:, BIG2.BIN without ---------------
        n0 = len(log.gets())
        e5 = copy_to_b(q, ui, S, vol, "BIG.BIN", bad, "5.")
        ports = log.ports(n0)
        print("   5. BIG.BIN pasted (fcp_err %d) in %d request(s) over %d "
              "connection(s)" % (e5, len(ports), len(set(ports))))
        if len(ports) > 1 and len(set(ports)) != 1:
            bad.append("5. the copy's %d requests took %d connections"
                       % (len(ports), len(set(ports))))
        httpd.ranges = False
        n0 = len(log.gets())
        e6 = copy_to_b(q, ui, S, vol, "BIG2.BIN", bad, "6.")
        print("   6. BIG2.BIN pasted with Range: IGNORED (fcp_err %d) in %d "
              "request(s)" % (e6, len(log.gets()) - n0))
        httpd.ranges = True

        # --- 7: a paste ONTO the drive is refused -------------------------
        w = front_on(ui, 1)
        if w:
            i, _ = ui.entry("BIG.BIN", w)
            q.click(*ui.row_xy(w, i - ui.scroll(w)))
            rdxms.menu_pick(q, ui, "Edit", "Copy")
        raise_vol(q, ui, vol)
        q.need(lambda: front_on(ui, vol) is not None, "the drive in front")
        rdxms.menu_pick(q, ui, "Edit", "Paste")
        os88qemu.pace(q, 4)
        subprocess.run([sys.executable, "tools/shot.py", q.sock,
                        os.path.join(ROOT, "build", "httpmount-wprot.png")],
                       capture_output=True)
        q.hmp("sendkey ret")            # "Replace BIG.BIN?" - yes: the
                                        # write must reach the driver
        q.need(lambda: q.read(S("fcp_busy"), 1)[0] == 0, "the refused paste", 60)
        q.done(3.0)
        busy = q.read(S("fcp_busy"), 1)[0]
        e7 = q.read(S("fcp_err"), 1)[0]
        if e7 != 9:                     # FERR_WPROT
            bad.append("7. the paste onto the drive ended fcp_err %d, not "
                       "FERR_WPROT (9)" % e7)
        listed = [n for n, _t in ui.listing(front_on(ui, vol))] \
            if front_on(ui, vol) else None
        print("   7. a paste onto the drive: busy %d, fcp_err %d, the drive "
              "lists BIG.BIN: %s" % (busy, e7, listed and "BIG.BIN" in listed))
        # --- 8: a PACKED file, opened, arrives unpacked -----------------
        from trackmove import pkg_syms
        P = pkg_syms("apps/notepad/notepad.asm", incs=("apps/",))
        raise_vol(q, ui, vol)
        q.need(lambda: front_on(ui, vol) is not None, "the drive in front")
        w = front_on(ui, vol)
        before = set(o.i for o in ui.windows())
        i, _ = ui.entry("PACKED.TXT", w)
        xmcheck.dblclick(q.sock, *ui.row_xy(w, i - ui.scroll(w)))
        q.need(lambda: any(o.i not in before and o.visible
                           for o in ui.windows()), "Note Pad", 60)
        nw = next(o for o in ui.windows() if o.i not in before and o.visible)
        rec = q.read(S("wm_wins") + nw.i * geom.WIN_SIZE, geom.WIN_SIZE)
        pseg = struct.unpack_from("<H", rec, geom.W_SEG)[0]
        q.need(lambda: struct.unpack("<H", q.readseg(pseg, P["np_len"], 2))[0],
               "Note Pad to hold the file", 30)
        q.done(2.0)
        nl = struct.unpack("<H", q.readseg(pseg, P["np_len"], 2))[0]
        print("   8. PACKED.TXT (%d bytes on the server, %d unpacked) opens in "
              "Note Pad as %d bytes" % (len(PACKED), len(PLAIN), nl))
        if nl != len(PLAIN):
            bad.append("8. a packed file read %d bytes into Note Pad, not its "
                       "%d unpacked - the sniff did not run on this volume (%s)"
                       % (nl, len(PLAIN), "the packed size" if nl == len(PACKED)
                          else "neither size"))
        if BYNAME:
            id1 = q.read(ebase + esyms["dns_id"], 2)
            print("   N. DNS transaction id after the open %s, at the end %s - "
                  "%s" % (id0.hex(), id1.hex(), "NO further lookup" if id0 == id1
                          else "LOOKED UP AGAIN"))
            if id0 != id1:
                bad.append("N. a later connection looked localhost up again: "
                           "the name cache did not answer")
            asked = [l for l in log.lines if l.startswith("DNS ")]
            print("   N. the resolver was asked %d time(s): %r" % (len(asked), asked))
            if len(asked) != 1:
                bad.append("N. %d lookups for one name - the cache should leave "
                           "exactly one" % len(asked))
        q.hmp("quit")
        q.close()
        q = None
        b = os88fat.Fat12(bimg)
        for leg, name, seed in (("5.", "BIG.BIN", 1), ("6.", "BIG2.BIN", 2)):
            try:
                got = b.read(name)
            except Exception as ex:             # noqa: BLE001
                bad.append("%s %s is not on B: (%s)" % (leg, name, ex))
                continue
            want = pattern(BIGLEN, seed)
            if got != want:
                first = next((k for k in range(min(len(got), len(want)))
                              if got[k] != want[k]), min(len(got), len(want)))
                bad.append("%s %s on B: is %d bytes, first wrong at %d"
                           % (leg, name, len(got), first))
            else:
                print("   %s %s on B: is the server's %d bytes" % (leg, name, BIGLEN))
        allp = log.ports()
        runs = sum(1 for k in range(len(allp)) if k == 0 or allp[k] != allp[k - 1])
        print("   the whole run: %d requests over %d connections (the server "
              "closes an idle one after 3 s, so each leg after the first "
              "reopens)" % (len(allp), runs))
        hashed = [g for g in log.gets() if "A#B" in g]
        if hashed:
            bad.append("2. a path went out unescaped: %r" % hashed)
    except Stop:
        pass
    except Exception as e:                      # noqa: BLE001
        bad.append("the gate died: %s" % e)
        try:
            d = {k: q.read(base + syms[k], 2) for k in
                 ("hf_err", "hf_status", "hf_failed", "hf_sock", "hf_iseg",
                  "hf_nrec", "hf_port")}
            print("   diag: %s; requests %r; host %r pfx %r" % (
                {k: struct.unpack("<H", v)[0] for k, v in d.items()},
                log.lines, q.read(base + syms["hf_host"], 32).split(b"\0")[0],
                q.read(base + syms["hf_pfx"], 32).split(b"\0")[0]))
        except Exception as e2:                 # noqa: BLE001
            print("   diag failed: %s" % e2)
    finally:
        if q is not None:
            q.close()
        if httpd is not None:
            httpd.shutdown()
        shutil.rmtree(tmp, ignore_errors=True)
        for b in bad:
            print("   FAIL: %s" % b)
        print("httpmount: %s" % ("FAIL" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
