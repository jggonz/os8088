#!/usr/bin/env python3
"""The ANSI-BBS parser on the machine, against its reference renderer.

    make && make telnettest && python3 tests/telansi.py

**QEMU BY NAME, for tests/ethernet.py's reason** (SPEC.md 70.12): MartyPC has
no network card of any kind, so the emulator this tree develops on cannot host
`ETHER.DRV` at all and there is no way to put a byte stream through this
package's own receive path on it. The disk shape is `make telnettest`'s - a
`SYSTEM.CFG` that already asks for the driver, so the card is up and DHCP has
bound before the first paint and this drives a CONNECTION rather than the
Control Panel - and the B: floppy is a scratch image of its own, because wave
4's Zmodem receive writes to it.

What QEMU costs is what it always costs: the machine is not an 8088 and no
timing here means anything. Every assertion below is about BYTES.

**THE ORACLE IS COMPUTED, NOT STORED** (SPEC.md 70.12). Each fixture under
`tests/fixtures/ansi/` is a byte stream written by `tools/ansifix.py`, and its
expectation is `tools/ansisim.py`'s output for that stream worked out at test
time - so an oracle cannot drift from the reference renderer, and a change to
the reference renderer is a change to every expectation at once. A test whose
oracle can be edited to agree with the code is not a gate.

SEVEN ASSERTIONS.

1. EVERY FIXTURE, ALL 4,000 BYTES. `te_scr` read out of guest memory against
   `ansisim.render(stream).raw()`, characters and attributes alike. Thirteen
   fixtures covering every state, every control, every CSI final and every SGR
   code in SPEC.md 70.9.

2. AND THE BYTES ARRIVE RAGGED. `tools/os88bbs.py` splits its output inside
   escape sequences and inside IAC sequences, deliberately, because that is
   what TCP does and a parser that only works on whole sequences passes every
   test written by somebody who forgot. The torture fixture is driven at the
   finest fragmentation the server offers.

3. THE NEGOTIATION (SPEC.md 70.10.1). `WILL TTYPE`, `WILL NAWS`, `DO ECHO`,
   `DO SGA` and BINARY in both directions, read out of the server's own log -
   which is what makes 70.10 testable at all, because the negotiation table is
   about bytes this end SENDS and no screenshot can see one.

4. THE SUBNEGOTIATIONS. `SB TTYPE IS "ANSI"` and `SB NAWS 0 80 0 25` decoded
   by the server. **NAWS reports the BUFFER and not the viewport**: a client
   that reported a narrow window would have the host wrap its lines where the
   buffer is not going to wrap them.

5. WHAT THE TERMINAL ANSWERS. The `report` fixture asks DSR 6 three times,
   DSR 5 once and DA twice, and `ansisim` says exactly which bytes come back.
   Those bytes must be in the server's key log, in order.

6. FULL SCREEN IS THE BOARD'S OWN SCREEN. `te_scr` maps 1:1 onto text VRAM
   (SPEC.md 70.8.7), so this is a memcmp of 4,000 bytes and not a screenshot -
   and a screenshot besides, for a person.

7. THE KEYS (SPEC.md 70.10.2). Home, the four arrows, PgUp, PgDn, Ins, Del,
   F1 and F5 typed at the terminal, asserted as the exact bytes the server
   saw. And Enter, which is a BARE CR here rather than CR LF, because the
   server offered `DO BINARY` and this end agreed to it.

Plus the Zmodem trigger (SPEC.md 70.9.6), which the `zmodem` fixture carries:
`[te_zon]` set, `[te_zat]` at the offset just past the final `0`, and the
screen holding `**B0` - the same handover offset `ansisim` publishes.
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import ansisim                                             # noqa: E402
import dispcp                                              # noqa: E402
import os88bbs                                             # noqa: E402
import os88qemu                                            # noqa: E402
import os88sym                                             # noqa: E402

S = os88sym.linear
SOCK = os.path.join(ROOT, "build", "qmp.sock")
PORT = 8094                     # NOT 8090 (tests/ethernet.py) and NOT 8092
                                # (tests/thewire.py): the three gates may be
                                # run side by side, and a bound port is a gate
                                # reading the OTHER one's answers
HOSTLINE = "10.0.2.2:%d" % PORT
FIXDIR = os.path.join(ROOT, "tests", "fixtures", "ansi")

TE_COLS, TE_ROWS = 80, 25
TE_SCRSZ = TE_COLS * TE_ROWS * 2
TS_UP, TS_DOWN, TS_ERR = 3, 4, 5
LN_X1, LN_Y1, LN_X2, LN_Y2 = 0, 2, 4, 6

# The order the fixtures are driven in. `report` is early because its answers
# are assertion 5 and a later fixture's ragged stream must not be what a stale
# reply is read out of; `zmodem` is LAST because the handover leaves the parser
# fed nothing more until wave 4's receiver exists, and the next Connect is what
# clears it.
FIXTURES = ["cursor", "erase", "sgr", "wrap", "edit", "scroll", "save",
            "report", "swallow", "cp437", "art", "torture", "zmodem"]

# The keys of SPEC.md 70.10.2 this types, and what each must put on the wire.
# QEMU's sendkey names on the left; the table in the package is what is under
# test, so nothing here is derived from it.
KEYS = [("home", "1b5b48"), ("up", "1b5b41"), ("down", "1b5b42"),
        ("right", "1b5b43"), ("left", "1b5b44"), ("end", "1b5b4b"),
        ("pgup", "1b5b56"), ("pgdn", "1b5b55"), ("insert", "1b5b40"),
        ("delete", "7f"), ("f1", "1b4f50"), ("f5", "1b5b31357e")]


def say(*a):
    print(*a)
    sys.stdout.flush()


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


# -----------------------------------------------------------------------------
# QEMU over QMP. This is the FOURTH copy of this class in tests/ - ethernet.py,
# ftpd.py and thewire.py carry the other three - and it is a copy rather than a
# shared module because moving it would mean editing three passing gates, which
# is not this wave's to do. The comment those three carry is the load-bearing
# part and is repeated here: `-qmp unix:...,server,nowait` serves a SINGLE
# client, so a monitor connection held open across the test makes every
# subprocess sit in the listen backlog for ever - it connects, it does not
# error, and it does not return, which reads exactly like the guest having hung.
# So it opens and closes per command.
# -----------------------------------------------------------------------------
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

    # dispcp.scroll_to walks a Disk window with the ARROWS (SPEC.md 22.11),
    # so a QEMU driver has to answer to the names it uses.
    KEYS = {"ArrowUp": "up", "ArrowDown": "down", "Return": "ret",
            "Escape": "esc"}

    def key(self, name):
        self.hmp("sendkey " + self.KEYS.get(name, name.lower()))

    def quit(self):
        try:
            self.hmp("quit")
        except Exception:                                       # noqa: BLE001
            pass


class Mouse:
    """tools/mouse.py, one process per action - QEMU's msmouse is 1200 baud
    and the pacing inside that tool is what makes a click land."""

    def run(self, *args):
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "mouse.py"),
                        SOCK] + list(args), check=True, capture_output=True,
                       cwd=ROOT)

    def to(self, x, y):
        self.run("to", str(x), str(y))

    def click(self, x, y):
        self.run("click", str(x), str(y))
        time.sleep(0.4)

    def dblclick(self, x, y):
        # TWO `click`s ARE NOT A DOUBLE-CLICK (CLAUDE.md): the detectors
        # compare birth ticks in a 9-tick window and two processes are far too
        # slow. Position, then both presses down one QMP connection.
        self.run("to", str(x), str(y))
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "qmp.py"),
                        SOCK, "mouse_button 1", "sleep 0.08", "mouse_button 0",
                        "sleep 0.12",
                        "mouse_button 1", "sleep 0.08", "mouse_button 0"],
                       check=True, capture_output=True, cwd=ROOT)
        time.sleep(0.4)


def settle(m, card=None):
    time.sleep(2.0)


def qmp(*cmds):
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "qmp.py"),
                    SOCK] + list(cmds), check=True, capture_output=True,
                   cwd=ROOT)


SENDKEY = {".": "dot", ":": "shift-semicolon", "-": "minus", "/": "slash"}


def typetext(text):
    cmds = []
    for ch in text:
        if ch.isalnum():
            cmds.append("sendkey " + (ch.lower() if ch.isalpha() else ch))
        elif ch in SENDKEY:
            cmds.append("sendkey " + SENDKEY[ch])
        else:
            sys.exit("telansi: no sendkey mapping for %r" % ch)
        cmds.append("sleep 0.06")
    qmp(*cmds)


# -----------------------------------------------------------------------------
# The package's own map, asserted to describe the shipped binary. A map of a
# different build is worse than no map: every offset it names is plausible.
# -----------------------------------------------------------------------------
def te_syms():
    import re
    import shutil
    d = tempfile.mkdtemp()
    src, mp, out = (os.path.join(d, n) for n in ("t.asm", "t.map", "t.bin"))
    shutil.copy(os.path.join(ROOT, "apps/telnet/telnet.asm"), src)
    with open(src, "a") as f:
        f.write("\n[map all %s]\n" % mp)
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                    "-I", "apps/telnet/", "-I", "drivers/net/",
                    "-o", out, src], check=True, cwd=ROOT)
    if (open(out, "rb").read()
            != open(os.path.join(ROOT, "build/telnet.bin"), "rb").read()):
        sys.exit("telansi: the mapped build is not build/telnet.bin - every "
                 "offset it names would be plausible and wrong")
    syms = {}
    for line in open(mp):
        mm = re.match(r"^\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+(\S+)\s*$", line)
        if mm:
            syms[mm.group(3)] = int(mm.group(1), 16)
    return syms


def diff_report(got, want):
    """Which CELL differs, and how - a byte count says nothing about where."""
    bad = [i for i in range(0, TE_SCRSZ, 2)
           if got[i:i + 2] != want[i:i + 2]]
    if not bad:
        return None
    rows = sorted({(i // 2) // TE_COLS for i in bad})
    first = bad[0] // 2
    return ("%d cell(s) of 2,000 differ, on row(s) %s; the first is "
            "(%d,%d) - got char %02X attr %02X, want char %02X attr %02X"
            % (len(bad), ",".join(str(r) for r in rows[:8]),
               first // TE_COLS, first % TE_COLS,
               got[bad[0]], got[bad[0] + 1], want[bad[0]], want[bad[0] + 1]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot", default=os.path.join(ROOT, "build",
                                                   "telansi-fsx.png"))
    ap.add_argument("--keep", action="store_true",
                    help="leave QEMU running for a look afterwards")
    ap.add_argument("--only", default=None,
                    help="drive one fixture by name (a quick loop)")
    a = ap.parse_args()
    fails = []

    # **THE SYSTEM IMAGE IS REBUILT, NOT CHECKED.** QEMU mounts it WRITABLE and
    # the OS writes to it, so staleness is not the hazard here - a dirty image
    # is - and `make` cannot see the difference, because the guest's write
    # leaves the image NEWER than everything it was built from.
    for img in ("build/telnetsys.img", "build/telnetdata.img"):
        p = os.path.join(ROOT, img)
        if os.path.exists(p):
            os.remove(p)
    r = subprocess.run(["make", "telnettest"], capture_output=True, text=True,
                       cwd=ROOT)
    if r.returncode:
        sys.exit("telansi: make telnettest failed:\n" + r.stdout + r.stderr)

    sy = te_syms()
    for n in ("te_scr", "te_state", "te_soff", "te_zon", "te_zat", "te_obin",
              "te_rxi", "te_rxn", "te_btn", "te_line", "te_pndn", "te_cvis",
              "te_hbuf", "te_thint", "te_txm", "te_zn", "te_tseg",
              "te_cx", "te_cy", "te_sx", "te_sy"):
        if n not in sy:
            sys.exit("telansi: %s is not in the package map" % n)

    fixtures = [f for f in FIXTURES if not a.only or f == a.only]
    if not fixtures:
        sys.exit("telansi: --only %s names no fixture" % a.only)
    streams = {}
    for name in fixtures:
        p = os.path.join(FIXDIR, name + ".bin")
        if not os.path.exists(p):
            sys.exit("telansi: no %s - run `python3 tools/ansifix.py`" % p)
        streams[name] = open(p, "rb").read()

    # A SURVIVOR KEEPS THE SOCKET, so the new machine cannot bind and every
    # read below would come from the OLD one - which reads as a change that
    # did nothing. Kill it by PID out of the pidfile and never with a pattern
    # that names the emulator, which matches the killing shell's own line.
    os88qemu.kill()
    if not a.keep:
        # `own()` registers an atexit teardown, which is what stops a FAILED
        # row leaving its emulator running for the next one to trip over
        # (tests/os88qemu.py). --keep is the one case that wants the opposite,
        # and skipping the registration is the only way to get it: the handler
        # fires on the way out whatever `m.quit()` does or does not do. With
        # --keep the guest is then the CALLER's to kill.
        os88qemu.own()
    r = subprocess.run(["make", "test", "ETHER=1",
                        "TESTIMG=build/telnetsys.img",
                        "TESTAPPS=build/telnetdata.img"],
                       capture_output=True, text=True, cwd=ROOT)
    if r.returncode:
        sys.exit("telansi: make test failed:\n" + r.stdout + r.stderr)

    m = Qemu()
    mo = Mouse()
    try:
        time.sleep(6.0)                     # ...the boot, then DHCP
        dispcp.open_drive(m, mo, S, settle, "A")
        wins = dispcp.win_list(m, S)
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, mo, S, settle, wx, wy, "APPS")
        dispcp.open_named(m, mo, S, settle, wx, wy, "TELNET.O88")
        wins2 = dispcp.win_list(m, S)
        if len(wins2) <= len(wins):
            sys.exit("telansi: TELNET.O88 did not open a window")
        tw = wins2[-1]
        rec = m.read(S("wm_wins") + tw * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        pseg = u16(rec, 22)
        say("telnet at %04X" % pseg)

        def rb(n):
            return m.readseg(pseg, sy[n], 1)[0]

        def rw(n):
            return u16(m.readseg(pseg, sy[n], 2))

        def rect(name):
            d = m.readseg(pseg, sy[name], 8)
            return u16(d, 0), u16(d, 2), u16(d, 4), u16(d, 6)

        # --- the host box, then the port ------------------------------------
        lx1, ly1, lx2, ly2 = rect("te_line")
        bx1, by1, bx2, by2 = rect("te_btn")
        mo.click((lx1 + lx2) // 2, (ly1 + ly2) // 2)
        typetext(HOSTLINE)
        # **TAB AND NOT RETURN.** Return in the host box IS Connect (te_onkey's
        # `.go`), and a connect fired here would race the server this test has
        # not started yet - and worse, the Connect click in the loop below
        # would then land on a session that was already UP and CLOSE it.
        # Tab leaves the box without dialling, which is also the only way to
        # reach the screen with the keyboard, so every key after this is the
        # host's.
        qmp("sendkey tab")
        time.sleep(0.5)
        host = m.readseg(pseg, sy["te_hbuf"], 32).split(b"\0")[0]
        say("host box  %r" % host.decode("latin-1"))
        if host.decode("latin-1") != HOSTLINE:
            fails.append("the host box holds %r and not %r - nothing below "
                         "this connected to the test's server"
                         % (host.decode("latin-1"), HOSTLINE))

        def connected(timeout=25.0):
            end = time.time() + timeout
            while time.time() < end:
                if rb("te_state") == TS_UP:
                    return True
                time.sleep(0.3)
            return False

        def quiet(timeout=30.0, still=2.0):
            """Wait until the parser has stopped being fed.

            The fixture arrives in fragments with a delay between them, so
            "the buffer is empty" is true between two of them; what says the
            stream is over is the STREAM OFFSET standing still - which is the
            same byte `ansisim` counts - with the receive queue drained and no
            reply owed."""
            last, since, end = -1, time.time(), time.time() + timeout
            while time.time() < end:
                off = rw("te_soff")
                drained = rw("te_rxi") >= rw("te_rxn") and rb("te_pndn") == 0
                if off != last:
                    last, since = off, time.time()
                elif drained and time.time() - since >= still:
                    return off
                time.sleep(0.25)
            return rw("te_soff")

        def press_connect():
            mo.click((bx1 + bx2) // 2, (by1 + by2) // 2)

        logs = {}
        first = True
        for name in fixtures:
            data = streams[name]
            # THE FRAGMENTATION IS THE POINT (SPEC.md 70.12): a different seed
            # per fixture, and the finest one on the longest stream, so the
            # boundaries are the SERVER's choice rather than whatever TCP
            # happened to do.
            seed = 3 if name != "torture" else 13
            srv = os88bbs.BBSServer(port=PORT, fixture=data, frag=seed,
                                    fragdelay=0.01, timeout=40.0, once=True)
            srv.start()
            time.sleep(0.3)
            press_connect()
            if not connected():
                srv.stop()
                fails.append("%s: the session never reached TS_UP (te_state "
                             "%d) - nothing below it can have been tested"
                             % (name, rb("te_state")))
                break
            got_off = quiet()
            scr = m.readseg(pseg, sy["te_scr"], TE_SCRSZ)
            ref = ansisim.render(data)
            want = ref.raw()
            # **DID THE STREAM FINISH?** `quiet()` returns when [te_soff] has
            # stood still for two seconds with the queue drained - and its own
            # docstring says "the buffer is empty" is true BETWEEN TWO
            # FRAGMENTS, which is the condition it then tests. The server is a
            # Python thread on a host several agents share, so a two-second gap
            # between two of its fragments is a scheduling accident rather than
            # an extreme; when it happens the assertion that fires is
            # diff_report's, which reads as a parser bug and is host load. The
            # number is already here: [te_soff] counts APPLICATION bytes after
            # the option layer and os88bbs's telnet_escape doubles 0xFF on the
            # way out, so a literal 0xFF costs one offset at each end and the
            # equality holds exactly.
            expect = ref.zmodem_at if ref.zmodem_at is not None else len(data)
            if got_off != expect:
                say("%-8s %5d bytes, %d fed  STALLED" % (name, len(data),
                                                         got_off))
                fails.append("%s: the guest consumed %d application bytes of "
                             "%d - the stream did not finish, so the cell "
                             "comparison is meaningless. Under host load "
                             "quiet() can return between two of the server's "
                             "fragments" % (name, got_off, expect))
                press_connect()
                time.sleep(1.2)
                srv.stop()
                logs[name] = srv.log_dict()
                time.sleep(0.6)
                continue
            bad = diff_report(scr, want)
            say("%-8s %5d bytes, %d fed  %s"
                % (name, len(data), got_off, "ok" if not bad else "MISMATCH"))
            if bad:
                fails.append("%s: %s" % (name, bad))

            if ref.zmodem_at is not None:
                if rb("te_zon") != 1:
                    fails.append("%s: the Zmodem trigger did not fire - "
                                 "[te_zon] is %d (SPEC.md 70.9.6)"
                                 % (name, rb("te_zon")))
                elif rw("te_zat") != ref.zmodem_at:
                    fails.append("%s: the handover is at %d and ansisim says "
                                 "%d - the offsets must be the same byte"
                                 % (name, rw("te_zat"), ref.zmodem_at))
                else:
                    say("         zmodem handover at %d, [te_zn] %d"
                        % (rw("te_zat"), rb("te_zn") if "te_zn" in sy else -1))
            elif rb("te_zon"):
                fails.append("%s: [te_zon] is set and this fixture has no "
                             "trigger in it - the detector fired on somebody's "
                             "artwork (SPEC.md 70.9.6)" % name)

            if first:
                # ...the negotiation and the key table, on the first live
                # session, before Close takes the wire away.
                first = False
                check_keys(qmp, srv, fails)

            press_connect()                 # Close: the worker owns the wire,
            time.sleep(1.2)                 # so this asks and does not do
            srv.stop()
            logs[name] = srv.log_dict()
            time.sleep(0.6)                 # ...before the port is bound again

        # --- 3 and 4: the negotiation and the subnegotiations ---------------
        log = logs.get(fixtures[0], {})
        check_negotiation(log, fails)

        # --- 5: what the terminal ANSWERED ----------------------------------
        if "report" in logs:
            want = bytes(ansisim.render(streams["report"]).answers).hex()
            got = logs["report"]["keys_hex"]
            if want and want in got:
                say("answers   %s - in the server's key log" % want)
            else:
                fails.append("the DSR and DA answers ansisim publishes (%s) "
                             "are not in what the server received (%s) - a "
                             "terminal that answers wrongly and one that does "
                             "not answer look the same from the screen"
                             % (want, got[:120]))

        # --- and the MIRROR, which needs a host asking for something else ---
        check_mirror(press_connect, connected, fails)

        # --- 6: full screen IS the board's own screen -----------------------
        # **WITH A BOARD ON IT.** The first version ran this at the end of the
        # fixture loop, by which time Close and the mirror check had put the
        # terminal through te_reset and te_clear - so the memcmp compared 4,000
        # bytes of SPACE against 4,000 bytes of space and passed, which is a
        # comparison that would pass against a renderer that drew nothing at
        # all. The screenshot is what said so: a black screen with the leave
        # hint on it. One more session, one more fixture, and the assertion is
        # over 2,000 cells of a board's own art.
        check_fullscreen(m, pseg, sy, a.shot, fails, press_connect, connected,
                         quiet, streams.get("art", streams[fixtures[0]]))

    finally:
        if not a.keep:
            m.quit()

    say("")
    for f in fails:
        say("FAIL  " + f)
    say("telansi: %d failure(s)" % len(fails))
    return 1 if fails else 0


# -----------------------------------------------------------------------------
# THE MIRROR (SPEC.md 70.1, kept by 70.10.1) NEEDS A SERVER THAT ASKS FOR
# SOMETHING NOT IN THE TABLE, and `tools/os88bbs.py` deliberately offers only
# the five a board offers - so nothing it does exercises the fall-through that
# answers every OTHER option. This is that server and it is fourteen lines: it
# asks for LINEMODE and X-DISPLAY-LOCATION, neither of which this terminal
# implements, and reads back what it is told.
#
# It is worth its own connection because the mirror's SENSE is the one way a
# Telnet client can wedge a session that is working perfectly: a host that WILL
# must be told DONT and a host that asks us to DO must be told WONT, and a
# terminal that has those the wrong way round answers for ever.
# -----------------------------------------------------------------------------
OPT_LINEMODE, OPT_XDISPLOC = 34, 35
IAC, WILL, WONT, DO, DONT = 255, 251, 252, 253, 254


class MirrorServer(threading.Thread):
    def __init__(self, port):
        threading.Thread.__init__(self, daemon=True)
        self.rx = bytearray()
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("0.0.0.0", port))
        self.sock.listen(2)

    def run(self):
        try:
            conn, _peer = self.sock.accept()
        except OSError:
            return
        try:
            conn.sendall(bytes([IAC, DO, OPT_LINEMODE,
                                IAC, WILL, OPT_XDISPLOC]))
            conn.settimeout(0.5)
            end = time.time() + 12.0
            while time.time() < end:
                try:
                    d = conn.recv(256)
                except socket.timeout:
                    continue
                except OSError:
                    break
                if not d:
                    break
                self.rx += d
        finally:
            try:
                conn.close()
            except OSError:
                pass

    def stop(self):
        try:
            self.sock.close()
        except OSError:
            pass


def check_mirror(press_connect, connected, fails):
    srv = MirrorServer(PORT)
    srv.start()
    time.sleep(0.3)
    press_connect()
    if not connected():
        srv.stop()
        fails.append("the mirror check never connected")
        return
    time.sleep(3.0)
    got = bytes(srv.rx).hex()
    say("mirror    %s" % (got or "nothing"))
    for name, want in (("WONT LINEMODE", bytes([IAC, WONT, OPT_LINEMODE])),
                       ("DONT X-DISPLAY-LOCATION",
                        bytes([IAC, DONT, OPT_XDISPLOC]))):
        if want.hex() not in got:
            fails.append("an option this terminal does not implement was not "
                         "answered with %s (%s): the server saw %s. SPEC.md "
                         "70.1's mirror is what answers every option outside "
                         "70.10.1's table, and its sense the wrong way round "
                         "is an option loop"
                         % (name, want.hex(), got or "nothing"))
    press_connect()
    time.sleep(1.0)
    srv.stop()


def check_negotiation(log, fails):
    """SPEC.md 70.10.1's table, read out of the server's own log."""
    want = [("WILL", "TTYPE"), ("WILL", "NAWS"), ("DO", "ECHO"),
            ("DO", "SGA"), ("WILL", "BINARY"), ("DO", "BINARY")]
    rx = [(e.get("cmd"), e.get("opt")) for e in log.get("negotiation", [])
          if e.get("dir") == "rx"]
    for pair in want:
        if pair not in rx:
            fails.append("the terminal never answered %s %s - the board is "
                         "then told nothing about it and sends the "
                         "line-oriented fallback (SPEC.md 70.10.1)"
                         % (pair[0], pair[1]))
    sub = [e for e in log.get("subnegotiation", []) if e.get("dir") == "rx"]
    tt = [e for e in sub if e.get("terminal")]
    if not tt or tt[0]["terminal"].upper() != "ANSI":
        fails.append("SB TTYPE IS did not say ANSI: %r" % (tt or None))
    else:
        say("ttype     %s" % tt[0]["terminal"])
    naws = [e for e in sub if e.get("cols") is not None]
    if not naws:
        fails.append("no SB NAWS arrived - a board that asks the size and is "
                     "not told draws a different screen")
    elif (naws[0]["cols"], naws[0]["rows"]) != (80, 25):
        fails.append("NAWS said %dx%d and the BUFFER is 80x25 - NAWS reports "
                     "the buffer and never the viewport (SPEC.md 70.10.1), or "
                     "every line of art after the first is in the wrong place"
                     % (naws[0]["cols"], naws[0]["rows"]))
    else:
        say("naws      %dx%d" % (naws[0]["cols"], naws[0]["rows"]))


def check_keys(sendk, srv, fails):
    """SPEC.md 70.10.2, asserted as the BYTES the server saw."""
    before = len(srv.keys)
    for key, _hexs in KEYS:
        sendk("sendkey " + key, "sleep 0.10")
    sendk("sendkey ret", "sleep 0.10")
    time.sleep(2.0)
    got = bytes(srv.keys[before:]).hex()
    say("keys      %s" % got)
    for key, hexs in KEYS:
        if hexs not in got:
            fails.append("%s should send %s and the server saw %s "
                         "(SPEC.md 70.10.2)" % (key, hexs, got or "nothing"))
    # ENTER IS A BARE CR HERE. The server offers DO BINARY and this end agrees
    # (SPEC.md 70.10.1), so [te_obin] bit 0 is set and RFC 854's CR LF becomes
    # what BINARY means: one carriage return.
    if got.endswith("0d0a"):
        fails.append("Enter sent CR LF after agreeing to TRANSMIT-BINARY - a "
                     "bare CR is what BINARY means (SPEC.md 70.10.2)")
    elif not got.endswith("0d"):
        fails.append("Enter sent %r and not a carriage return" % got[-4:])


def check_fullscreen(m, pseg, sy, shot, fails, press_connect, connected, quiet,
                     stream):
    """SPEC.md 70.8.7: 80x25 onto 80x25, all of it, no status line."""
    srv = os88bbs.BBSServer(port=PORT, fixture=stream, frag=5, fragdelay=0.01,
                            timeout=90.0, once=True)
    srv.start()
    time.sleep(0.3)
    press_connect()
    if not connected():
        srv.stop()
        fails.append("full screen: no session to put a board on the screen")
        return
    quiet()
    scr0 = m.readseg(pseg, sy["te_scr"], TE_SCRSZ)
    drawn = sum(1 for i in range(0, TE_SCRSZ, 2) if scr0[i] != 0x20)
    say("fsx       %d of 2,000 cells hold a glyph before ^]" % drawn)
    if drawn < 100:
        fails.append("only %d cells of the buffer are non-blank, so the memcmp "
                     "below would pass against a renderer that drew nothing"
                     % drawn)
    qmp("sendkey ctrl-bracket_right")
    time.sleep(3.0)
    txm = m.readseg(pseg, sy["te_txm"], 1)[0] if "te_txm" in sy else 1
    if not txm:
        fails.append("Ctrl+] did not enter the full-screen bracket "
                     "([te_txm] is 0) - nothing below this was tested")
        return
    scr = m.readseg(pseg, sy["te_scr"], TE_SCRSZ)
    # **THE SEGMENT IS ASKED, NOT ASSUMED.** It was a hardcoded 0xB8000, which
    # is right on VGA and CGA and wrong on Hercules - and the same gate pointed
    # at a Hercules run then reports "4,000 of 4,000 bytes differ" instead of
    # "the wrong framebuffer". [te_tseg] is the answer OSAPI_FSX_MODE gave the
    # package (tetxt.inc), so this reads the machine's own.
    tseg = u16(m.readseg(pseg, sy["te_tseg"], 2))
    say("fsx seg   %04X" % tseg)
    if tseg not in (0xB800, 0xB000):
        fails.append("[te_tseg] is %04X, which is neither text framebuffer - "
                     "the memcmp below would compare the wrong memory" % tseg)
    vram = m.read(tseg << 4, TE_SCRSZ)
    if shot:
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "shot.py"),
                        SOCK, shot], check=False, capture_output=True, cwd=ROOT)
        say("shot      %s" % shot)
    # **THE LEAVE HINT IS THE ONE THING IN VRAM THAT IS NOT IN THE BUFFER**,
    # and that is SPEC.md 70.8.7's design rather than a defect: te_tx_hint
    # writes ` ^] to leave` straight into the last row's last twelve cells,
    # once, on entry, and the HOST IS ALLOWED TO OVERWRITE IT - a hint that
    # survives is a hint that fights the board for the row it needs. So the
    # memcmp skips those cells while [te_thint] still says row 24, and asserts
    # them separately: the two facts are both worth having and neither is the
    # other's excuse.
    hint = b" ^] to leave"
    skip = set()
    if m.readseg(pseg, sy["te_thint"], 1)[0] == TE_ROWS - 1:
        base = ((TE_ROWS - 1) * TE_COLS + TE_COLS - len(hint)) * 2
        skip = set(range(base, base + len(hint) * 2))
        got = bytes(vram[base + 2 * i] for i in range(len(hint)))
        att = {vram[base + 2 * i + 1] for i in range(len(hint))}
        if got != hint or att != {0x70}:
            fails.append("the leave hint reads %r at attribute(s) %s and "
                         "should be %r inverse (SPEC.md 70.8.7)"
                         % (got, sorted(att), hint))
        else:
            say("hint      %r inverse on row 24" % hint.decode())
    bad = [i for i in range(TE_SCRSZ) if scr[i] != vram[i] and i not in skip]
    if bad:
        fails.append("full screen: %d of 4,000 bytes differ between te_scr "
                     "and text VRAM, the first at offset %d (row %d, col %d) "
                     "- the buffer maps 1:1 onto VRAM (SPEC.md 70.8.7)"
                     % (len(bad), bad[0], (bad[0] // 2) // TE_COLS,
                        (bad[0] // 2) % TE_COLS))
    else:
        say("fullscreen 4,000 bytes identical, the hint's twelve cells apart")
    # --- THE SAVED CURSOR, AND IT IS ASKED INSIDE THE BRACKET --------------
    # This is the one place the w3 review's BLOCKER 1 could be reached: te_fsi
    # aliased [te_sx]/[te_sy], so OSAPI_FSX_MODE's sixteen bytes landed on the
    # saved cursor and the board's next `CSI u` sent te_putc 34,696 bytes past
    # the package's claim. The `save` fixture is driven WINDOWED with all the
    # others and `art` has no CSI s/CSI u in it, so nothing in wave 3 asked the
    # question after a mode set. A restore with no prior save is legal and is
    # what a board that keeps a status line sends.
    # It is asserted on the SLOT rather than by sending a restore, because the
    # slot is where the damage is: with the aliasing in place [te_sx] reads
    # 0xB800 the instant OSAPI_FSX_MODE returns, whether or not a board ever
    # sends the sequence that would spend it. The restore's own clamp
    # (te_restcur) is defence in depth and would MASK a send-based test.
    sx = u16(m.readseg(pseg, sy["te_sx"], 2))
    sy_ = u16(m.readseg(pseg, sy["te_sy"], 2))
    cx0 = u16(m.readseg(pseg, sy["te_cx"], 2))
    cy0 = u16(m.readseg(pseg, sy["te_cy"], 2))
    if sx >= TE_COLS or sy_ >= TE_ROWS:
        fails.append("inside the bracket the SAVED cursor is (%d,%d) and the "
                     "screen is 80x25 - OSAPI_FSX_MODE has written over it, "
                     "which is te_fsi aliasing te_sx (the w3 review's BLOCKER "
                     "1). te_celloff has no range check and te_putc is the one "
                     "unguarded write in the package" % (sx, sy_))
    elif cx0 >= TE_COLS or cy0 >= TE_ROWS:
        fails.append("inside the bracket the cursor is (%d,%d) and the screen "
                     "is 80x25" % (cx0, cy0))
    else:
        say("fsx cur   saved (%d,%d), live (%d,%d) - both in range after "
            "OSAPI_FSX_MODE" % (sx, sy_, cx0, cy0))
    qmp("sendkey ctrl-bracket_right")
    time.sleep(3.0)
    press_connect()                     # ...and the session closes with it
    time.sleep(1.0)
    srv.stop()


if __name__ == "__main__":
    sys.exit(main())
