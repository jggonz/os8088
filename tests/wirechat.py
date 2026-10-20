#!/usr/bin/env python3
"""WireChat on the machine, against SPEC.md 113.2's protocol.

    make && make wirechattest && python3 tests/wirechat.py

**QEMU BY NAME, for tests/ethernet.py's reason** (SPEC.md 72.9): MartyPC has no
network card, so ETHER.DRV - which is the only way this package reaches a
socket - cannot run there. The disks are `make wirechattest`'s: Telnet's gate
system disk, whose SYSTEM.CFG already asks for the driver, and a scratch B:
carrying WIRECHAT.O88 and a WIRECHAT.CFG aimed at the fixture below.

**THE FIXTURE IS A THIRD IMPLEMENTATION OF ONE PAGE.** SPEC.md 113.2 is the
contract; os8088-web/src/chat/hub.js is the server that ships; this file's
`Hub` is a small, deliberately literal reading of the same section, in the
standard library, so the gate runs with nothing but python3 - the tree's rule
for tests/ (CLAUDE.md). It does not moderate (that is hub.js's, and its own
tests'); it refuses one word, `badword`, so the 422 path has a trigger, and
it owns a nick only when assertion 11 gives it one, so the 428 path has one.

**IT SPEAKS SPEC.md 113.2.4 TOO** - `x=1`'s dates and mentions, to a request
that carries `x=1` and to no other, exactly as hub.js does: a `yyyymmdd`
ahead of every `hhmm` in `M` and `D` (the fixture's clock is fixed - `DAY`
and the hhmm each record was stored with - so every byte is predictable; pass
`date=` to `post`/`dm` for a history that crosses a day), and `A seq room date
hhmm nick` for a live x=1 session a ROOM message names (`@nick`, case-blind,
not continued by a nick character) whose view is not that room and that did
not send it. A mention is kept on the session - 20 at most, ten minutes - and
is delivered once: a request whose `s` is at or past it says the client has
it. `HUB.post(room, nick, text)` mentions as a `say` does, its `nick` standing
in for the sender. A request WITHOUT `x=1` gets the format above it to the
byte, so the package before 113.2.4 still passes against this fixture.

What QEMU costs is what it always costs: the machine is not an 8088 and no
timing here means anything. Every assertion is about BYTES - guest memory read
through the package's own map, and the requests the fixture logged.

FIFTEEN ASSERTIONS.

1.  THE JOIN is SPEC.md 113.3's request to the byte: `POST
    /chat/v1/join?v=0&st=0&x=1 HTTP/1.0` (the `x=1` since SPEC.md 113.2.4,
    which the client sends on every request), the Host, the User-Agent,
    `Connection: close`, and a Content-Length that is the nick's own length.
2.  THE SESSION: [wc_link] reaches LK_LIVE and [wc_tok] holds the token the
    fixture issued.
3.  THE HISTORY: the lobby's three seeded messages are in the store, in
    order, with their nicks, texts and sequence numbers.
4.  A HELD POLL IS ANSWERED: the fixture is holding the package's poll when a
    second session speaks, and the message reaches the store.
5.  A MESSAGE SENT is the body of a `say`, byte for byte, and comes back into
    the store marked as ours.
6.  A REFUSAL: a message the fixture answers 422 puts the fixture's sentence on
    the notice line and the message back in the field.
7.  A ROOM CHANGE (Tab) asks `v=1&h=1`, and the store becomes room 1's.
8.  A PRIVATE MESSAGE lights a conversation: [wc_dn] = 1, its nick, unread 1.
9.  A FLAG: a right press on a message and Yes send `flag` with that
    message's seq, and the fixture's `X` takes it out of the store.
10. A LOST SESSION (401) is joined again with the same nick, unasked.
11. AN OWNED NICK (428): the rejoin is refused for want of a code and the
    field asks for one; a wrong code is asked for again; the right one joins,
    as the join body's second line.
12. THE SETTINGS FILE: a press on `Notifications:` turns them off, and the
    WIRECHAT.CFG on the floppy says `notify 0` - and the code - afterwards.
13. THE DATE (SPEC.md 113.2.4): every request carried `x=1`, and a message
    the fixture dated 2026-10-19 is stored with that day - and, the first of
    its day, with the day divider's flag (SPEC.md 113.5).
14. A MENTION: a message in the view that names us is marked as one, and an
    `@Tester` in a room we are NOT looking at arrives as an `A` - the room
    marked, the notice line saying who and where.
15. TAB COMPLETES AN @NICK: `@ret` and Tab is `@RetroAmy` off the roster,
    and the view stays where it was.
"""
import argparse
import datetime
import os
import re
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import dispcp                                              # noqa: E402
import os88qemu                                            # noqa: E402
import os88sym                                             # noqa: E402
import os88build                                           # noqa: E402

S = os88sym.linear
SOCK = os.path.join(ROOT, "build", "qmp.sock")
PORT = 8093                     # NOT 8090 (ethernet.py), 8092 (thewire.py) or
                                # 8094 (telansi.py): the gates may run side by
                                # side, and a bound port is a gate reading the
                                # OTHER one's answers. The disk's WIRECHAT.CFG
                                # names this number (the Makefile)
LK_LIVE = 3
MD_KEY = 4
WV_DM = 0xFF
WC_MREC, WC_MSEQ, WC_MFLG, WC_MNICK, WC_MTEXT = 224, 0, 6, 8, 21
WCM_MINE = 2
WCM_MENT, WCM_DAY, WC_MDAY = 4, 8, 222
WC_DREC, WC_DUNR = 16, 13
DAY = "20261019"                # the fixture's date, SPEC.md 113.2.4's yyyymmdd
MENTION_KEEP = 20               # a session's unread mentions (SPEC.md 113.2.4)
MENTION_AGE = 600.0             # ...and for this many seconds


def say(*a):
    print(*a)
    sys.stdout.flush()


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def cstr(b):
    return b.split(b"\0", 1)[0].decode("latin1")


# =============================================================================
# THE FIXTURE (SPEC.md 113.2, literally)
# =============================================================================
class Hub:
    ROOMS = [("lobby", "The os8088 community"), ("hardware", "Cards"),
             ("development", "Code"), ("games", "Play")]

    def __init__(self):
        self.lock = threading.Condition()
        self.seq = 1000
        self.msgs = []              # (seq, room, hhmm, nick, text, deleted, date)
        self.dms = []               # (seq, hhmm, from, to, text, date)
        self.dels = []              # (delseq, seq)
        self.sessions = {}          # token -> {nick, room, st, view, x, ments}
        self.rver = 1
        self.log = []               # (verb, query dict, headers text, body)
        self.held = 0               # polls being held right now
        self.ntok = 0
        self.owned = {}             # lowercase nick -> its code

    # --- the fixture's own hand ------------------------------------------
    def post(self, room, nick, text, date=DAY):
        with self.lock:
            self.seq += 1
            m = [self.seq, room, "1200", nick, text, False, date]
            self.msgs.append(m)
            self.mention(m, None)
            self.lock.notify_all()
            return self.seq

    def dm(self, frm, to, text, date=DAY):
        with self.lock:
            self.seq += 1
            self.dms.append((self.seq, "1201", frm, to, text, date))
            self.lock.notify_all()

    # --- SPEC.md 113.2.4: who a room message names ------------------------------
    @staticmethod
    def names(text, nick):
        """`@nick`, case-blind, not continued by a nick character."""
        return re.search(r"@" + re.escape(nick) + r"(?![A-Za-z0-9_-])", text,
                         re.IGNORECASE) is not None

    def mention(self, m, sender):
        """Room message `m`, said by session `sender` (None: the fixture's own
        hand, whose sender is `m`'s nick). Under the lock."""
        seq, room, hhmm, nick, text = m[0], m[1], m[2], m[3], m[4]
        for s in self.sessions.values():
            if s is sender or not s["x"] or (sender is None and s["nick"].lower() == nick.lower()):
                continue
            if s["view"].isdigit() and int(s["view"]) == room:
                continue
            if self.names(text, s["nick"]):
                s["ments"].append({"seq": seq, "room": room, "date": m[6], "hhmm": hhmm,
                                   "nick": nick, "at": time.time()})
                del s["ments"][:-MENTION_KEEP]

    def remove(self, seq):
        with self.lock:
            for m in self.msgs:
                if m[0] == seq:
                    m[5] = True
            self.seq += 1
            self.dels.append((self.seq, seq))
            self.lock.notify_all()

    def visit(self, nick, room):
        """A second session, so the roster has someone to complete (15); the
        seq moves so a held poll answers with the roster now."""
        with self.lock:
            self.ntok += 1
            self.sessions["vis%09d" % self.ntok] = {
                "nick": nick, "room": room, "st": 0, "view": str(room), "x": False,
                "ments": []}
            self.rver += 1
            self.seq += 1
            self.lock.notify_all()

    def drop_all(self):
        with self.lock:
            self.sessions.clear()
            self.rver += 1
            self.lock.notify_all()

    # --- the wire ------------------------------------------------------------
    def delta(self, s, q):
        since = int(q.get("s", "0") or 0)
        rver = int(q.get("r", "0") or 0)
        hist = q.get("h") == "1"
        x = q.get("x") == "1"
        view = s["view"]

        def dfmt(d):            # D seq [date] hhmm from to text
            if x:
                return "D %d %s %s %s %s %s" % (d[0], d[5], d[1], d[2], d[3], d[4])
            return "D %d %s %s %s %s" % d[:5]
        out = ["C %d 0 0 0" % sum(1 for m in self.msgs if m[1] == 0)]
        if view.startswith("@"):
            other = view[1:].lower()
            for d in self.dms:
                if (hist or d[0] > since) and other in (d[2].lower(), d[3].lower()) \
                        and s["nick"].lower() in (d[2].lower(), d[3].lower()):
                    out.append(dfmt(d))
        else:
            room = int(view)
            mine = [m for m in self.msgs if m[1] == room and not m[5]]
            if hist:
                mine = mine[-30:]
            else:
                mine = [m for m in mine if m[0] > since]
            for m in mine:
                if x:
                    out.append("M %d %d %s %s %s %s" % (m[0], m[1], m[6], m[2], m[3], m[4]))
                else:
                    out.append("M %d %d %s %s %s" % tuple(m[:5]))
            for d in self.dms:
                if d[0] > since and d[3].lower() == s["nick"].lower():
                    out.append(dfmt(d))
        for delseq, seq in self.dels:
            if delseq > since:
                out.append("X %d" % seq)
        if x:
            gone = {m[0] for m in self.msgs if m[5]}
            for a in s["ments"]:
                if a["seq"] > since and a["seq"] not in gone:
                    out.append("A %d %d %s %s %s" % (a["seq"], a["room"], a["date"],
                                                     a["hhmm"], a["nick"]))
        if rver != self.rver:
            out.append("R %d %d" % (self.rver, len(self.sessions)))
            for t, x in sorted(self.sessions.items(), key=lambda kv: kv[1]["nick"].lower()):
                out.append("P %s %d %d" % (x["nick"], x["room"], x["st"]))
        return out

    def answer(self, verb, q, body):
        with self.lock:
            if verb == "join":
                nick, _, code = body.partition("\n")
                nick, code = nick.strip(), code.strip().lower()
                if not re.match(r"^[A-Za-z][A-Za-z0-9_-]{1,11}$", nick):
                    return ["ER 400 0 Nick: 2-12 letters"]
                if nick.lower() in self.owned:
                    if not code:
                        return ["ER 428 0 That nick needs its code"]
                    if code != self.owned[nick.lower()]:
                        return ["ER 428 0 Wrong code for that nick"]
                self.ntok += 1
                tok = ("tok%09d" % self.ntok)[:12]
                v = q.get("v", "0")
                self.sessions[tok] = {"nick": nick, "room": int(v) if v.isdigit() else 0,
                                      "st": int(q.get("st", "0")), "view": v,
                                      "x": q.get("x") == "1", "ments": []}
                self.rver += 1
                lines = ["J %s %s" % (tok, nick)]
                lines += ["L %d %s %s" % (i, n, t) for i, (n, t) in enumerate(self.ROOMS)]
                q = dict(q, s=str(self.seq), r="0", h="1")
                return ["OK %d 1200 0" % self.seq] + lines + self.delta(self.sessions[tok], q)
            s = self.sessions.get(q.get("t", ""))
            if not s:
                return ["ER 401 0 Session ended - joining again"]
            # SPEC.md 113.2.4: x rides on every request; an `s` at or past a
            # mention says the client has read it; ten minutes is the most
            s["x"] = q.get("x") == "1"
            since = int(q.get("s", "0") or 0)
            old = time.time() - MENTION_AGE
            s["ments"] = [a for a in s["ments"] if a["seq"] > since and a["at"] >= old]
            if "v" in q:
                s["view"] = q["v"]
                if q["v"].isdigit() and int(q["v"]) != s["room"]:
                    s["room"] = int(q["v"])
                    self.rver += 1
            if "st" in q and int(q["st"]) != s["st"]:
                s["st"] = int(q["st"])
                self.rver += 1
            if verb == "say":
                text = body.strip()
                if "badword" in text:
                    return ["ER 422 0 Not sent - watch the language"]
                self.seq += 1
                if s["view"].startswith("@"):
                    self.dms.append((self.seq, "1202", s["nick"], s["view"][1:], text, DAY))
                else:
                    m = [self.seq, s["room"], "1202", s["nick"], text, False, DAY]
                    self.msgs.append(m)
                    self.mention(m, s)
                self.lock.notify_all()
            elif verb == "flag":
                pass                        # logged; the test removes it itself
            elif verb == "poll":
                lines = self.delta(s, q)
                if not any(not l.startswith("C ") for l in lines):
                    self.held += 1
                    seq0 = self.seq
                    end = time.time() + 20
                    while self.seq == seq0 and self.sessions.get(q["t"]) is s \
                            and time.time() < end:
                        self.lock.wait(0.5)
                    self.held -= 1
                    if self.sessions.get(q["t"]) is not s:
                        return ["ER 401 0 Session ended - joining again"]
            elif verb != "leave":
                return ["ER 400 0 Bad request"]
            out = ["OK %d 1200 0" % self.seq] + self.delta(s, q)
            if verb == "flag":
                out.insert(1, "N Flag noted - one more removes it")
            return out


HUB = Hub()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *a):
        pass

    def do_POST(self):
        u = urlsplit(self.path)
        n = int(self.headers.get("Content-Length", "0") or 0)
        body = self.rfile.read(n).decode("latin1") if n else ""
        q = {k: v[0] for k, v in parse_qs(u.query, keep_blank_values=True).items()}
        verb = u.path.rsplit("/", 1)[-1]
        HUB.log.append((verb, q, self.requestline + "\n" + str(self.headers), body))
        if not u.path.startswith("/chat/v1/"):
            self.send_error(404)
            return
        data = ("\n".join(HUB.answer(verb, q, body)) + "\n").encode("ascii")
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=us-ascii")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)


# =============================================================================
# QEMU over QMP - tests/telansi.py's class (and its load-bearing note: a QMP
# connection held open makes every later one sit in the backlog for ever, so
# it opens and closes per command).
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
        import json
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
        self.hmp('pmemsave 0x%X %d "%s"' % (linear, n, p))
        return open(p, "rb").read()

    def readseg(self, seg, off, n):
        return self.read((seg << 4) + off, n)

    KEYS = {"ArrowUp": "up", "ArrowDown": "down", "Return": "ret", "Escape": "esc"}

    def key(self, name):
        self.hmp("sendkey " + self.KEYS.get(name, name.lower()))

    def quit(self):
        try:
            self.hmp("quit")
        except Exception:                                       # noqa: BLE001
            pass


def tool(name, *args):
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", name), SOCK]
                   + list(args), check=True, capture_output=True, cwd=ROOT)


class Mouse:
    def to(self, x, y):
        tool("mouse.py", "to", str(x), str(y))

    def click(self, x, y):
        tool("mouse.py", "click", str(x), str(y))
        tool("qmp.py", "gsleep 0.4")

    def rclick(self, x, y):
        tool("mouse.py", "rclick", str(x), str(y))
        tool("qmp.py", "gsleep 0.4")

    def dblclick(self, x, y):
        tool("mouse.py", "to", str(x), str(y))
        tool("qmp.py", "mouse_button 1", "sleep 0.08", "mouse_button 0",
             "sleep 0.12", "mouse_button 1", "sleep 0.08", "mouse_button 0",
             "gsleep 0.4")


def settle(m, card=None):
    os88qemu.ui_done(m, S, cap=2.0)


KEYMAP = {" ": "spc", ".": "dot", ",": "comma", "!": "shift-1", "?": "shift-slash",
          "-": "minus", "@": "shift-2", "\n": "ret"}


def typetext(text):
    cmds = []
    for ch in text:
        if ch.isalpha():
            cmds.append("sendkey " + ("shift-" + ch.lower() if ch.isupper() else ch))
        elif ch.isdigit():
            cmds.append("sendkey " + ch)
        else:
            cmds.append("sendkey " + KEYMAP[ch])
        cmds.append("gsleep 0.05")
    tool("qmp.py", *cmds)


def wc_syms():
    """build/wirechat.map, from the same nasm run that wrote build/wirechat.bin
    (the Makefile's STKLST): a map of another build names plausible offsets."""
    mp = os.path.join(ROOT, os88build.at("build/wirechat.map"))
    syms = {}
    for line in open(mp):
        mm = re.match(r"^\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+(\S+)\s*$", line)
        if mm:
            syms[mm.group(3)] = int(mm.group(1), 16)
    for n in ("wc_link", "wc_tok", "wc_mn", "wc_msgs", "wc_view", "wc_dn", "wc_dms",
              "wc_note", "wc_lbuf", "wc_ox", "wc_oy", "wc_tx", "wc_py1", "wc_cw",
              "wc_sy", "wc_notify", "wc_save", "wc_cfgld", "wc_hist", "wc_mode", "wc_key",
              "wc_ment", "wc_rn"):
        if n not in syms:
            sys.exit("wirechat: %s is not in build/wirechat.map" % n)
    return syms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true",
                    help="leave QEMU running for a look afterwards")
    ap.add_argument("--shots", default=None, help="a folder for screendumps")
    a = ap.parse_args()
    fails = []

    def check(ok, what):
        say(("  ok   " if ok else "  FAIL ") + what)
        if not ok:
            fails.append(what)

    # THE IMAGES ARE REBUILT, NOT CHECKED (telansi.py's reason): QEMU mounts
    # them writable, and this gate's own assertion 11 writes B:.
    for img in ("build/telnetsys.img", "build/wirechatdata.img"):
        p = os.path.join(ROOT, img)
        if os.path.exists(p):
            os.remove(p)
    r = subprocess.run(["make", "wirechattest"], capture_output=True, text=True, cwd=ROOT)
    if r.returncode:
        sys.exit("wirechat: make wirechattest failed:\n" + r.stdout + r.stderr)
    sy = wc_syms()

    # --- the fixture, seeded --------------------------------------------------
    HUB.post(0, "Daniel", "Hello from a real IBM XT!")
    HUB.post(0, "RetroAmy", "Hercules. Looking sharp in mono.")
    HUB.post(0, "Daniel", "Anyone up for WireBall?")
    HUB.post(1, "Bot", "Room one, message one.")
    srv = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    srv.daemon_threads = True
    threading.Thread(target=srv.serve_forever, daemon=True).start()

    os88qemu.kill()
    if not a.keep:
        os88qemu.own()
    r = subprocess.run(["make", "test", "ETHER=1", "TESTIMG=build/telnetsys.img",
                        "TESTAPPS=build/wirechatdata.img"],
                       capture_output=True, text=True, cwd=ROOT)
    if r.returncode:
        sys.exit("wirechat: make test failed:\n" + r.stdout + r.stderr)

    m = Qemu()
    mo = Mouse()

    def shot(name):
        if a.shots:
            os.makedirs(a.shots, exist_ok=True)
            tool("shot.py", os.path.join(os.path.abspath(a.shots), name + ".png"))

    try:
        for letter in ("A", "B"):
            if not os88qemu.acted(m, lambda: _zone(m, letter), secs=90,
                                  what="drive %s's zone" % letter, poll=0.4):
                sys.exit("wirechat: drive %s: never had a desktop zone - a boot "
                         "failure, not a WireChat one" % letter)
        os88qemu.ui_done(m, S, cap=8.0)
        dispcp.open_drive(m, mo, S, settle, "B")
        wins = dispcp.win_list(m, S)
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, mo, S, settle, wx, wy, "WIRECHAT.O88")
        wins2 = dispcp.win_list(m, S)
        if len(wins2) <= len(wins):
            sys.exit("wirechat: WIRECHAT.O88 did not open a window")
        rec = m.read(S("wm_wins") + wins2[-1] * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        pseg = u16(rec, 22)
        say("wirechat at %04X" % pseg)

        def rb(n, off=0):
            return m.readseg(pseg, sy[n] + off, 1)[0]

        def rw(n, off=0):
            return u16(m.readseg(pseg, sy[n] + off, 2))

        def rs(n, size, off=0):
            return cstr(m.readseg(pseg, sy[n] + off, size))

        def msg(i):
            b = m.readseg(pseg, sy["wc_msgs"] + i * WC_MREC, WC_MREC)
            return (struct.unpack_from("<I", b, WC_MSEQ)[0], b[WC_MFLG],
                    cstr(b[WC_MNICK:WC_MNICK + 13]), cstr(b[WC_MTEXT:WC_MTEXT + 201]))

        def wait(cond, what, secs=30):
            return os88qemu.acted(m, cond, secs=secs, what=what, poll=0.3)

        # --- 1, 2: the join ----------------------------------------------------
        live = wait(lambda: rb("wc_link") == LK_LIVE, "the session", secs=45)
        joins = [e for e in HUB.log if e[0] == "join"]
        head = joins[0][2] if joins else ""
        check(bool(joins) and head.startswith("POST /chat/v1/join?v=0&st=0&x=1 HTTP/1.0\n"),
              "1  the join's request line is SPEC.md 113.3's")
        check(bool(joins) and "Host: 10.0.2.2:8093" in head
              and "User-Agent: os8088 WireChat 1.0" in head
              and "Connection: close" in head
              and "Content-Length: 6" in head and joins[0][3] == "Tester",
              "1  ...and its headers and body")
        check(live and rs("wc_tok", 13) == "tok000000001",
              "2  the session is live with the issued token")
        shot("1-joined")

        # --- 3: the history ------------------------------------------------------
        got = [msg(i)[2:] for i in range(rb("wc_mn"))]
        check(got == [("Daniel", "Hello from a real IBM XT!"),
                      ("RetroAmy", "Hercules. Looking sharp in mono."),
                      ("Daniel", "Anyone up for WireBall?")]
              and msg(0)[0] == 1001 and msg(2)[0] == 1003,
              "3  the lobby's history, in order (got %r)" % (got,))

        # --- 4: a held poll, answered ----------------------------------------------
        wait(lambda: HUB.held > 0, "a held poll", secs=20)
        check(HUB.held > 0, "4  the package's poll is being held")
        HUB.post(0, "Alin", "Welcome aboard!")
        check(wait(lambda: rb("wc_mn") == 4 and msg(3)[3] == "Welcome aboard!",
                   "the held poll's message"),
              "4  ...and the message that ends it reaches the store")

        # --- 5: a message sent -----------------------------------------------------
        typetext("Hello from QEMU\n")
        check(wait(lambda: rb("wc_mn") == 5, "our message back"),
              "5  a sent message comes back into the store")
        says = [e for e in HUB.log if e[0] == "say"]
        check(bool(says) and says[-1][3] == "Hello from QEMU"
              and msg(4)[1] & WCM_MINE and msg(4)[3] == "Hello from QEMU",
              "5  ...as the say's whole body, marked ours")

        # --- 6: a refusal --------------------------------------------------------------
        typetext("a badword here\n")
        check(wait(lambda: rs("wc_note", 64) == "Not sent - watch the language",
                   "the refusal"),
              "6  a 422's sentence is on the notice line")
        check(rs("wc_lbuf", 201) == "a badword here",
              "6  ...and the message is back in the field")
        shot("2-refused")
        m.key("Escape")
        settle(m)

        # --- 7: a room change ------------------------------------------------------------
        n = len(HUB.log)
        m.key("tab")
        check(wait(lambda: rb("wc_view") == 1 and rb("wc_mn") == 1
                   and msg(0)[3] == "Room one, message one.", "room 1"),
              "7  Tab: the view is room 1 and the store is room 1's")
        later = [e for e in HUB.log[n:] if e[0] == "poll"]
        check(any(e[1].get("v") == "1" and e[1].get("h") == "1" for e in later),
              "7  ...asked for as v=1&h=1")

        # --- 8: a private message ---------------------------------------------------------
        HUB.dm("Bot", "Tester", "psst - WireBall later?")
        check(wait(lambda: rb("wc_dn") == 1, "the conversation"),
              "8  a private message makes a conversation")
        d = m.readseg(pseg, sy["wc_dms"], WC_DREC)
        check(cstr(d[:13]) == "Bot" and d[WC_DUNR] == 1,
              "8  ...with Bot, one unread")
        shot("3-dm")

        # --- 9: a flag -------------------------------------------------------------------
        ox, oy, tx, py1 = rw("wc_ox"), rw("wc_oy"), rw("wc_tx"), rw("wc_py1")
        mo.rclick(ox + tx + 40, oy + py1 + 5)        # the first band: room 1's
        settle(m)                                    # message's header
        m.key("Return")                              # Yes
        flags = []
        wait(lambda: bool([e for e in HUB.log if e[0] == "flag"]), "the flag")
        flags = [e for e in HUB.log if e[0] == "flag"]
        check(bool(flags) and flags[-1][1].get("m") == "1004",
              "9  a flag carries the message's seq (got %r)"
              % (flags[-1][1] if flags else None,))
        HUB.remove(1004)
        check(wait(lambda: rb("wc_mn") == 0, "the removal"),
              "9  ...and its X takes it out of the store")

        # --- 10: a lost session --------------------------------------------------------
        HUB.drop_all()
        check(wait(lambda: len([e for e in HUB.log if e[0] == "join"]) >= 2
                   and rb("wc_link") == LK_LIVE, "the rejoin", secs=45),
              "10 a 401 is joined again, unasked")
        rj = [e for e in HUB.log if e[0] == "join"]
        check(len(rj) >= 2 and rj[-1][3] == "Tester",
              "10 ...with the same nick")

        # --- 11: an owned nick -----------------------------------------------------------
        HUB.owned["tester"] = "0b5e-c0de-42"
        HUB.drop_all()
        check(wait(lambda: rb("wc_mode") == MD_KEY
                   and rs("wc_note", 64) == "That nick needs its code", "the code prompt",
                   secs=45),
              "11 an owned nick's join without its code asks for one")
        typetext("wrong\n")
        check(wait(lambda: rb("wc_mode") == MD_KEY
                   and rs("wc_note", 64) == "Wrong code for that nick", "the wrong code"),
              "11 ...a wrong one is asked for again")
        shot("4-code")
        typetext("0b5e-c0de-42\n")
        check(wait(lambda: rb("wc_link") == LK_LIVE and rs("wc_key", 37) == "0b5e-c0de-42",
                   "the owned join", secs=45),
              "11 ...and the right one joins")
        oj = [e for e in HUB.log if e[0] == "join"]
        check(oj[-1][3] == "Tester\n0b5e-c0de-42" and "Content-Length: 19" in oj[-1][2],
              "11 ...sent as the join body's second line (got %r)" % (oj[-1][3],))

        # --- 12: the settings file ------------------------------------------------------
        cw, syy = rw("wc_cw"), rw("wc_sy")
        mo.click(ox + cw - 40, oy + syy + 5)
        check(wait(lambda: rb("wc_notify") == 0 and rb("wc_save") == 0,
                   "the notification switch and its save"),
              "12 Notifications: off, and the save done")

        # --- 13, 14: dates and mentions (SPEC.md 113.2.4) ------------------------------
        check(all(e[1].get("x") == "1" for e in HUB.log),
              "13 every request carried x=1")
        n0 = rb("wc_mn")
        HUB.post(rb("wc_view"), "Bot", "hello Tester, WireBall later?")
        check(wait(lambda: rb("wc_mn") == n0 + 1, "the dated message"),
              "13 a message in the view arrives")
        b = m.readseg(pseg, sy["wc_msgs"] + n0 * WC_MREC, WC_MREC)
        want = (datetime.date(2026, 10, 19) - datetime.date(2000, 1, 1)).days + 1
        check(struct.unpack_from("<H", b, WC_MDAY)[0] == want,
              "13 ...stored with its day, %d (got %d)"
              % (want, struct.unpack_from("<H", b, WC_MDAY)[0]))
        check(n0 > 0 or b[WC_MFLG] & WCM_DAY,
              "13 ...and the first of its day has the divider's flag")
        check(b[WC_MFLG] & WCM_MENT,
              "14 a message in the view that names us is marked")
        HUB.post(2, "Alin", "@tester the 5150 build is up")
        check(wait(lambda: rb("wc_ment", 2) == 1
                   and rs("wc_note", 64).startswith("Alin mentioned you in #"),
                   "the mention"),
              "14 an @mention in another room marks it and says who (%r)"
              % rs("wc_note", 64))

        # --- 15: an @nick completed --------------------------------------------------
        HUB.visit("RetroAmy", rb("wc_view"))
        wait(lambda: rb("wc_rn") >= 2, "the roster")
        view = rb("wc_view")
        typetext("@ret")
        m.key("tab")
        check(wait(lambda: rs("wc_lbuf", 201) == "@RetroAmy", "the completion"),
              "15 Tab after @ret is @RetroAmy (got %r)" % rs("wc_lbuf", 201))
        check(rb("wc_view") == view, "15 ...and the view stays")
        shot("5-end")
    finally:
        if not a.keep:
            m.quit()
            time.sleep(1.0)
        srv.shutdown()

    if not a.keep:
        files = fat12_files(os.path.join(ROOT, "build", "wirechatdata.img"))
        cfg = files.get("SYSTEM/APPDATA/WIRECHAT.CFG", b"(no file)").decode("latin1")
        check("notify 0" in cfg and "friend Alin" in cfg and "nick Tester" in cfg
              and "code 0b5e-c0de-42" in cfg,
              "12 ...and WIRECHAT.CFG on B: says so (%r)" % cfg)

    if fails:
        say("wirechat: %d failure(s)" % len(fails))
        sys.exit(1)
    say("wirechat: all assertions passed")


def fat12_files(path):
    """Every file on a FAT12 image, by its path - tests/thewire.py's walker
    (tools/os88fat.py reads the root's 8.3 names, and this file is two
    folders down)."""
    img = open(path, "rb").read()
    bps, spc, res, nfat = u16(img, 11), img[13], u16(img, 14), img[16]
    nroot, spf = u16(img, 17), u16(img, 22)
    root = (res + nfat * spf) * bps
    data = root + nroot * 32
    fat = img[res * bps:(res + spf) * bps]

    def nxt(c):
        v = u16(fat, c + (c >> 1))
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
            if e[11] & 0x10:
                if nm.count("/") <= 4:
                    walk(chain(u16(e, 26)), nm + "/")
                continue
            out[nm.upper()] = chain(u16(e, 26), struct.unpack_from("<I", e, 28)[0])

    walk(img[root:root + nroot * 32], "")
    return out


def _zone(m, letter):
    try:
        return dispcp.drive_ordinal(m, S, letter) is not None
    except Exception:                                       # noqa: BLE001
        return False


if __name__ == "__main__":
    main()
