#!/usr/bin/env python3
"""THE WIRE, end to end over a real card (SPEC.md 88.12).

    make && make thewiretest && python3 tests/thewire.py

**THIS ONE IS QEMU'S, AND THAT IS NOT A LAPSE.** CLAUDE.md's rule is MartyPC
first with a short list of exceptions, and this is on it for tests/ethernet.py's
reason exactly: MartyPC has no network card of any kind, so `ETHER.DRV` cannot
be hosted on it at all. QEMU's `ne2k_isa` on `-netdev user` is the only harness
there is - a gateway at 10.0.2.2, DHCP handing out 10.0.2.15.

What QEMU costs is what it always costs: the machine is not an 8088 and no
timing here means anything. Every assertion below is about BEHAVIOUR and none
is about speed, so there is no number in this file for PERFORMANCE.md to want
off the 5150.

THE HOST SERVES A FIXTURE CATALOG, packed by `tools/os88wire.py` out of
`build/hello.o88`, `build/mines.o88` and a tier-3 `WF_DISK` entry with one
sidecar, on **port 8092** - tests/ethernet.py's is 8090, and the two gates may
be run side by side; a bound port is a gate reading the other one's answers.
`make thewiretest`'s `SYSTEM/APPDATA/WIRE.CFG` names `10.0.2.2:8092/wire/`, so
the machine asks the host for it and every byte under test is a byte this file
chose.

SIX ASSERTIONS.

1. THE CATALOG ARRIVES AND IS UNDERSTOOD. `wr_n` is 3, `wr_state` is WS_DONE
   and `wr_nodrv` is 0 - which is the whole stack under it (ARP, a handshake,
   a GET, a windowed drain, a close) plus wr_catck having accepted every field.
2. THE HOST SAW THE REQUEST IT EXPECTED, `GET /wire/catalog.bin HTTP/1.0` with
   a Host: header, so the path composition and WIRE.CFG both worked.
3. THE LIST IS THE CATALOG. The scroll block's `total` - which is the machine's
   own `wr_count`, written by the painter - is 3.
4. THE FILTER FILTERS. Clicking `8088/8086` leaves `wr_filter` = 1 and that
   same count at 2, because the fixture's third entry is tier 3.
5. THE PREDICATE GREYS THE RIGHT BUTTON. Selecting the tier-3 `WF_DISK` entry
   leaves `wr_grey` bit 0 set and bit 1 clear: Load Program refuses (its files
   must be on a disk) and Add to Disk does not.
6. ADD TO DISK WRITES BOTH FILES, BYTE-IDENTICAL. The Save dialog's default
   button is clicked, the chain runs, and after `quit` the B: image is read on
   the HOST by an independent FAT12 reader (`tools/os88disk.py --verify`, then
   the directory walked here) and both files are compared with what the server
   sent.

**AND ONE MORE, PENDING THE KERNEL HALF.** Load Program is `OSAPI_PKG_RUN`
(SPEC.md 21.x), which is the other branch's. The assertion is written and runs
the moment `apps/os88api.inc` carries the slot - there is no flag to remember
and no second visit to this file.
"""
import argparse
import json
import os
import re
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import dispcp                                              # noqa: E402
import os88sym                                             # noqa: E402
import os88qemu                                            # noqa: E402
import os88wire                                            # noqa: E402

S = os88sym.linear
SOCK = "build/qmp.sock"
PORT = 8092
SYSIMG = "build/thewire360.img"
DATIMG = "build/thewiredata.img"

WS_DONE, WS_FAIL = 6, 7
TITLE_H = 18
FD_BX1, FD_BX2, FD_BY0, FD_BH = 224, 286, 20, 13     # kernel/fdlg.inc's button

FIXTURE = {
    "date": "20260904",
    "entries": [
        {"stem": "HELLO", "title": "Hello", "kind": 0, "tier": 0,
         "flags": {"new": True}, "files": ["hello.o88"],
         "description": "The smallest os8088 package."},
        {"stem": "MINES", "title": "Minesweeper", "kind": "game", "tier": 0,
         "files": ["mines.o88"],
         "description": "The 1990 game, in assembly."},
        {"stem": "BIGONE", "title": "Needs a disk", "kind": 0, "tier": 3,
         "files": ["hello.o88", "mines.o88"],
         "description": "Two files, so Load Program refuses it."},
    ],
}
SIDECAR = "MINES.O88"                   # what BIGONE's second file is called
                                        # in /wire/pkg/ and on the disk


def say(*a):
    print(*a)
    sys.stdout.flush()


# =============================================================================
# THE PACKAGE'S OWN BSS OFFSETS, out of its source
#
# apps/thewire declares them as `name equ os88_image_end + <expr>`, which is
# the convention every package here uses (tests/wirefps.py reads apps/wire's
# the same way) - and this evaluates the expressions rather than hard-coding
# the numbers, because a hard-coded offset is PLAUSIBLE after the bss moves and
# reads whatever landed there.
# =============================================================================
def bss_offsets():
    src = os.path.join(ROOT, "apps", "thewire", "thewire.asm")
    consts, out = {}, {}
    equ = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s+equ\s+([^;]+?)\s*(?:;.*)?$")
    for line in open(src):
        m = equ.match(line)
        if not m:
            continue
        name, expr = m.group(1), m.group(2).strip()
        if expr.startswith("os88_image_end"):
            e = expr[len("os88_image_end"):].strip().lstrip("+").strip() or "0"
            try:
                out[name] = int(eval(e, {"__builtins__": {}}, dict(consts)))
            except Exception:
                pass
        else:
            try:
                consts[name] = int(eval(expr, {"__builtins__": {}},
                                        dict(consts)))
            except Exception:
                pass
    for want in ("wr_n", "wr_state", "wr_nodrv", "wr_filter", "wr_sel",
                 "wr_grey", "wr_ox", "wr_oy", "wr_sb", "wr_catseg"):
        if want not in out:
            sys.exit("thewire: %s is not in apps/thewire/thewire.asm's bss "
                     "block - the block moved and every offset this file "
                     "would read is plausible and wrong" % want)
    return out


# =============================================================================
# QEMU over QMP - tests/ethernet.py's class, one connection at a time
# =============================================================================
class Qemu:
    def __init__(self, path=SOCK):
        self.path = path
        self.tmp = tempfile.mkdtemp()
        for _ in range(150):
            try:
                self.hmp("info status")
                return
            except OSError:
                time.sleep(0.2)
        raise RuntimeError("no QMP socket at %s" % path)

    def hmp(self, cmd):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(self.path)
        f = s.makefile("rw")

        def send(obj):
            f.write(json.dumps(obj) + "\n")
            f.flush()
            while True:
                line = f.readline()
                if not line:
                    raise RuntimeError("QMP closed")
                msg = json.loads(line)
                if "event" in msg:
                    continue
                return msg
        try:
            json.loads(f.readline())
            send({"execute": "qmp_capabilities"})
            r = send({"execute": "human-monitor-command",
                      "arguments": {"command-line": cmd}})
            return r.get("return", "")
        finally:
            f.close()
            s.close()

    def read(self, linear, n):
        p = os.path.join(self.tmp, "m.bin")
        # THE FILENAME IS QUOTED: HMP parses an unquoted /tmp/... as an
        # EXPRESSION and answers "invalid char 't'", which reads as a bad
        # address rather than a bad argument.
        self.hmp('pmemsave 0x%X %d "%s"' % (linear, n, p))
        return open(p, "rb").read()

    def readseg(self, seg, off, n):
        return self.read((seg << 4) + off, n)

    # dispcp.scroll_to drives the Disk window with ARROW KEYS, and it names
    # them the way MartyPC's harness does. QMP's `sendkey` has its own names,
    # so this is the two-entry translation that lets a QEMU row use the same
    # navigation helpers every MartyPC row uses - which is the point of them.
    KEYS = {"ArrowUp": "up", "ArrowDown": "down", "Return": "ret",
            "Escape": "esc"}

    def key(self, name):
        self.hmp("sendkey " + self.KEYS.get(name, name.lower()))

    def quit(self):
        try:
            self.hmp("quit")
        except Exception:
            pass


class Mouse:
    def run(self, *args):
        subprocess.run(["python3", "tools/mouse.py", SOCK] + list(args),
                       check=True, capture_output=True)

    def click(self, x, y):
        self.run("click", str(x), str(y))
        time.sleep(0.4)

    def dblclick(self, x, y):
        # TWO `click`s ARE NOT A DOUBLE-CLICK (CLAUDE.md): the detectors
        # compare birth ticks in a 9-tick window and two processes are far too
        # slow. Position, then both presses down one QMP connection.
        self.run("to", str(x), str(y))
        subprocess.run(["python3", "tools/qmp.py", SOCK,
                        "mouse_button 1", "sleep 0.08", "mouse_button 0",
                        "sleep 0.12",
                        "mouse_button 1", "sleep 0.08", "mouse_button 0"],
                       check=True, capture_output=True)
        time.sleep(0.4)


def settle(m, card=None):
    time.sleep(2.0)


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


# =============================================================================
# THE HOST'S WIRE - one catalog, two packages, and it remembers what was asked
# =============================================================================
class Server(threading.Thread):
    def __init__(self, files):
        threading.Thread.__init__(self, daemon=True)
        self.files = files              # path -> bytes
        self.asked = []
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.s.bind(("0.0.0.0", PORT))
        self.s.listen(4)

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
                self.asked.append(req)
                line = req.split(b"\r\n")[0].decode("latin1", "replace")
                path = line.split(" ")[1] if " " in line else ""
                body = self.files.get(path)
                if body is None:
                    c.sendall(b"HTTP/1.0 404 Not Found\r\n"
                              b"Content-Length: 0\r\n\r\n")
                else:
                    c.sendall(b"HTTP/1.0 200 OK\r\n"
                              b"Content-Type: application/octet-stream\r\n"
                              b"Content-Length: %d\r\n\r\n" % len(body) + body)
            except OSError:
                pass
            finally:
                c.close()


# =============================================================================
# AN INDEPENDENT FAT12 READER for the write-back check
#
# `tools/os88disk.py --verify` is the STRUCTURAL fsck and is run first; this
# walks the root directory for the two names and pulls their cluster chains,
# so what the assertion compares is bytes off the image and not the guest's
# opinion of them.
# =============================================================================
def fat12_files(path):
    img = open(path, "rb").read()
    bps = u16(img, 11)
    spc = img[13]
    res = u16(img, 14)
    nfat = img[16]
    nroot = u16(img, 17)
    spf = u16(img, 22)
    root = (res + nfat * spf) * bps
    data = root + nroot * 32
    fat = img[res * bps:(res + spf) * bps]

    def nxt(c):
        o = c + (c >> 1)
        v = u16(fat, o)
        return (v >> 4) if (c & 1) else (v & 0xFFF)

    def chain(c, size=None):
        blob = b""
        while 2 <= c < 0xFF8 and (size is None or len(blob) < size):
            off = data + (c - 2) * spc * bps
            blob += img[off:off + spc * bps]
            c = nxt(c)
        return blob if size is None else blob[:size]

    out = {}

    def walk(entries, prefix):
        for i in range(0, len(entries) - 31, 32):
            e = entries[i:i + 32]
            if e[0] in (0x00, 0xE5) or e[11] & 0x08:
                continue
            name = e[0:8].decode("latin1").rstrip()
            ext = e[8:11].decode("latin1").rstrip()
            if name in (".", ".."):
                continue
            nm = prefix + name + ("." + ext if ext else "")
            c = u16(e, 26)
            if e[11] & 0x10:            # a folder, and the Save dialog OPENS
                if prefix:              # IN ONE: SPEC.md 38.10 starts it in
                    continue            # MEDIA, so the write lands there and
                walk(chain(c), nm + "/")    # a reader that only knew the root
                continue                    # would report it as never written
            out[nm.upper()] = chain(c, struct.unpack_from("<I", e, 28)[0])

    walk(img[root:root + nroot * 32], "")
    return out


# =============================================================================
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true",
                    help="leave QEMU running for a look afterwards")
    ap.add_argument("--shots", metavar="DIR", default=None,
                    help="write a screenshot per step into DIR")
    a = ap.parse_args()
    fails = []

    def no(msg):
        fails.append(msg)
        say("FAIL: " + msg)

    off = bss_offsets()

    # **THE DISKS ARE REBUILT, NOT CHECKED.** QEMU mounts both writable and
    # assertion 6 WRITES to the data disk, so a run that inherited the last
    # run's B: would find the two files already there and pass having proved
    # nothing. Staleness is not the hazard here - a dirty image is - and
    # `make` cannot see the difference, because the guest's write leaves the
    # image NEWER than everything it was built from (tests/ethernet.py's note,
    # one disk along).
    for f in (SYSIMG, DATIMG):
        if os.path.exists(f):
            os.remove(f)
    r = subprocess.run(["make", "thewiretest"], capture_output=True, text=True)
    if r.returncode:
        sys.exit("thewire: make thewiretest failed:\n" + r.stdout + r.stderr)

    # --- the fixture the host will serve ------------------------------------
    tmp = tempfile.mkdtemp()
    man = os.path.join(tmp, "wire.json")
    json.dump(FIXTURE, open(man, "w"))
    try:
        cat = os88wire.pack(json.load(open(man)), "build", None)
    except os88wire.Refused as e:
        sys.exit("thewire: the fixture will not pack: %s" % e)
    hello = open("build/hello.o88", "rb").read()
    mines = open("build/mines.o88", "rb").read()
    served = {"/wire/catalog.bin": cat,
              "/wire/pkg/HELLO.O88": hello,
              "/wire/pkg/MINES.O88": mines,
              "/wire/pkg/BIGONE.O88": hello}
    srv = Server(served)
    srv.start()
    say("http server on 10.0.2.2:%d, catalog %d bytes, 3 records"
        % (PORT, len(cat)))

    # A SURVIVOR KEEPS THE SOCKET, so the new machine cannot bind and every
    # read below would come from the OLD one - which reads as a change that
    # did nothing (CLAUDE.md). Kill it by PID out of the pidfile and never
    # with `pkill -f qemu`, whose pattern matches the calling shell.
    if os.path.exists("build/qemu.pid"):
        try:
            os.kill(int(open("build/qemu.pid").read().strip()), 15)
            time.sleep(1.0)
        except (OSError, ValueError):
            pass
    for f in ("build/qmp.sock", "build/qemu.pid"):
        if os.path.exists(f):
            os.remove(f)
    os88qemu.own()
    r = subprocess.run(["make", "test", "ETHER=1", "TESTIMG=" + SYSIMG,
                        "TESTAPPS=" + DATIMG], capture_output=True, text=True)
    if r.returncode:
        sys.exit("thewire: make test failed:\n" + r.stdout + r.stderr)

    m = Qemu()
    mo = Mouse()
    img = os.path.getsize("build/thewire.bin")

    def shot(tag):
        if a.shots:
            os.makedirs(a.shots, exist_ok=True)
            subprocess.run(["python3", "tools/shot.py", SOCK,
                            os.path.join(a.shots, tag + ".png")], check=True)

    try:
        # --- launch it. **TODO: THE DESKTOP ZONE.** Once the kernel half
        # (SPEC.md 26.x's Wire zone) merges, this is a DOUBLE-CLICK ON THE
        # ICON and the two dispcp calls go. Two things change with it and
        # both are in this file: the instance's current directory becomes A:
        # (the zone launches out of SYSTEM/ on the boot volume, SPEC.md 88.1),
        # so assertion 6's Save dialog has to be walked to B: first, and
        # WIRE.CFG is then read off A: rather than off B:.
        dispcp.open_drive(m, mo, S, settle, "B")
        wins = dispcp.win_list(m, S)
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, mo, S, settle, wx, wy, "THEWIRE.O88")
        wins2 = dispcp.win_list(m, S)
        if len(wins2) <= len(wins):
            m.quit()
            sys.exit("thewire: THEWIRE.O88 did not open a window")
        ww = wins2[-1]
        rec = m.read(S("wm_wins") + ww * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        pseg = u16(rec, 22)
        say("The Wire at %04X, image %d bytes" % (pseg, img))

        def b(name, n=1):
            return m.readseg(pseg, img + off[name], n)

        def w(name):
            return u16(b(name, 2))

        # --- 1: the catalog arrives and is understood -----------------------
        state = n = 0
        for _ in range(80):
            time.sleep(0.5)
            state, n = b("wr_state")[0], w("wr_n")
            if n or state == WS_FAIL:
                break
        say("wr_state = %d, wr_n = %d, wr_nodrv = %d, wr_catseg = %04X"
            % (state, n, b("wr_nodrv")[0], w("wr_catseg")))
        shot("01-catalog")
        if b("wr_nodrv")[0]:
            no("wr_nodrv is set: net_find or NETV_STATE refused, so the card "
               "or the driver never came up and nothing below is about the "
               "Wire")
        if state == WS_FAIL:
            no("wr_state is WS_FAIL: the fetch failed and the package knows")
        elif state != WS_DONE:
            no("wr_state is %d and not WS_DONE - the fetch never completed"
               % state)
        if n != 3:
            no("wr_n is %d and the fixture catalog has 3 records: either it "
               "did not arrive or wr_catck refused a field" % n)

        # --- 2: what the host was actually asked for ------------------------
        first = srv.asked[0] if srv.asked else None
        say("server saw: %r" % (first.split(b"\r\n")[0] if first else None))
        if first is None:
            no("nothing reached the host at all")
        else:
            if not first.startswith(b"GET /wire/catalog.bin HTTP/1.0\r\n"):
                no("the first request line is %r, and WIRE.CFG named "
                   "10.0.2.2:8092/wire/" % first.split(b"\r\n")[0])
            if b"Host: 10.0.2.2" not in first:
                no("no Host: header in the request")
            if b"User-Agent: os8088 Wire 1.0" not in first:
                no("no User-Agent: the Wire identifies itself (SPEC.md 88.4)")

        # --- 3: the list is the catalog -------------------------------------
        # [wr_sb+8] is the scroll block's `total`, which the painter fills from
        # the machine's own wr_count. Reading it is reading what the LIST says
        # rather than what the catalog holds.
        def shown():
            return u16(m.readseg(pseg, img + off["wr_sb"] + 8, 2))

        say("the list shows %d of %d" % (shown(), n))
        if shown() != 3:
            no("the list shows %d rows over a 3-record catalog" % shown())

        ox, oy = w("wr_ox"), w("wr_oy")
        say("content origin (%d, %d)" % (ox, oy))
        if ox % 8:
            no("the content origin's x is %d, which is not a multiple of 8 - "
               "SPEC.md 11.94 snaps it, and every 8-aligned pen in the "
               "package is derived from that" % ox)

        # --- 4: the filter filters ------------------------------------------
        mo.click(ox + 96 + 6, oy + 2 + 6)        # the `8088/8086` radio
        time.sleep(1.0)
        shot("02-filter")
        say("wr_filter = %d, the list shows %d" % (b("wr_filter")[0], shown()))
        if b("wr_filter")[0] != 1:
            no("wr_filter is %d after clicking the 8088/8086 radio"
               % b("wr_filter")[0])
        if shown() != 2:
            no("the 8088/8086 filter shows %d rows and the fixture has two "
               "tier-0 entries" % shown())

        mo.click(ox + 48 + 6, oy + 2 + 6)        # ...and back to All
        time.sleep(1.0)
        if shown() != 3:
            no("back on All the list shows %d rows" % shown())

        # --- 5: the predicate greys the right button ------------------------
        # Row 2 is BIGONE: tier 3, two files, so WF_DISK. Load Program must
        # refuse with 'Needs its files on a disk' and Add to Disk must not.
        mo.click(ox + 40, oy + 19 + 2 * 16 + 8)
        time.sleep(1.5)
        shot("03-selected")
        sel, grey = w("wr_sel"), b("wr_grey")[0]
        say("wr_sel = %d, wr_grey = %d" % (sel, grey))
        if sel != 2:
            no("wr_sel is %d after clicking the third row" % sel)
        if not grey & 1:
            no("Load Program is NOT greyed on a WF_DISK record: SPEC.md 88.7 "
               "refuses it because OSAPI_PKG_RUN would run it with its "
               "overlay nowhere (wr_grey = %d)" % grey)
        if grey & 2:
            no("Add to Disk is greyed on a WF_DISK record, and Add to Disk is "
               "exactly what such a record is FOR (wr_grey = %d)" % grey)

        # --- 6: Add to Disk writes both files -------------------------------
        wr = dispcp.win_rect(m, S, ww)
        y2 = w("wr_y2")
        mo.click(ox + 280, oy + y2 - 11)         # the lower button's middle
        time.sleep(2.0)
        shot("04-savedlg")
        wins3 = dispcp.win_list(m, S)
        if len(wins3) <= len(wins2):
            no("Add to Disk... put up no Save dialog")
        else:
            dx, dy = dispcp.win_rect(m, S, wins3[-1])[:2]
            mo.click(dx + 1 + (FD_BX1 + FD_BX2) // 2,
                     dy + TITLE_H + FD_BY0 + FD_BH // 2)
            time.sleep(2.0)
            for _ in range(60):
                time.sleep(0.5)
                if b("wr_job")[0] == 0 and b("wr_state")[0] in (WS_DONE,
                                                               WS_FAIL):
                    break
            shot("05-added")
            say("after the chain: wr_job = %d, wr_state = %d"
                % (b("wr_job")[0], b("wr_state")[0]))

        # --- and the one that waits on the kernel half ----------------------
        if have_pkg_run():
            mo.click(ox + 40, oy + 19 + 0 * 16 + 8)   # HELLO, tier 0
            time.sleep(1.5)
            before = dispcp.win_list(m, S)
            mo.click(ox + 280, oy + w("wr_y2") - 30)  # the upper button
            for _ in range(60):
                time.sleep(0.5)
                if len(dispcp.win_list(m, S)) > len(before):
                    break
            after = dispcp.win_list(m, S)
            shot("06-loaded")
            titles = []
            for slot in after:
                r = m.read(S("wm_wins") + slot * dispcp.WIN_SIZE,
                           dispcp.WIN_SIZE)
                ttl, seg = u16(r, 10), u16(r, 22)
                titles.append(m.readseg(seg, ttl, 16).split(b"\0")[0]
                              .decode("latin1", "replace"))
            say("windows now: %r" % titles)
            if len(after) <= len(before):
                no("Load Program opened no window: OSAPI_PKG_RUN refused, or "
                   "the wake never ran the image")
            elif "HELLO" not in [t.upper() for t in titles]:
                no("Load Program opened a window and it is not HELLO's: %r"
                   % titles)
        else:
            say("Load Program: SKIPPED - apps/os88api.inc has no "
                "OSAPI_PKG_RUN yet, so the kernel half (SPEC.md 21.x) has "
                "not merged. The assertion above runs the moment it does.")
    finally:
        if not a.keep:
            m.quit()
            time.sleep(1.5)

    # --- assertion 6's other half, on the host ------------------------------
    r = subprocess.run(["python3", "tools/os88disk.py", "--verify", DATIMG],
                       capture_output=True, text=True)
    say((r.stdout + r.stderr).strip().splitlines()[-1] if (r.stdout + r.stderr)
        else "")
    if r.returncode:
        no("the data disk does not pass os88disk.py --verify after the write")
    got = fat12_files(DATIMG)
    say("B: now holds %r" % sorted(got))
    # THE PATH IS NOT ASSERTED, only the NAME. The Standard File dialog opens
    # in MEDIA (SPEC.md 38.10) and the user can walk anywhere from there, so
    # where a file lands is the DIALOG's answer and not the Wire's - what the
    # Wire owes is that both files went to the SAME folder under the right
    # names with the right bytes.
    where = None
    for name, want in (("BIGONE.O88", hello), (SIDECAR, mines)):
        hit = [k for k in got if k.split("/")[-1] == name]
        if not hit:
            no("%s is nowhere on B: - Add to Disk did not write it" % name)
            continue
        k = hit[0]
        if where is None:
            where = k.rsplit("/", 1)[0] if "/" in k else ""
        elif (k.rsplit("/", 1)[0] if "/" in k else "") != where:
            no("%s landed in %r and the .O88 landed in %r - a sidecar has to "
               "go beside its package or the package cannot find it" % (k, where))
        if got[k] != want:
            no("%s on B: is %d bytes and the host sent %d, or the bytes differ"
               % (k, len(got[k]), len(want)))
        else:
            say("%s: %d bytes, byte-identical to what the host sent"
                % (k, len(want)))

    say("thewire: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


def have_pkg_run():
    """Has the kernel half landed? The SDK is the one place that says so."""
    src = open(os.path.join(ROOT, "apps", "os88api.inc")).read()
    return "%define OSAPI_PKG_RUN" in src


if __name__ == "__main__":
    sys.exit(main())
