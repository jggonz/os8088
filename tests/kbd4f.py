#!/usr/bin/env python3
"""The keyboard on an 8042 that loads its next byte on a read (SPEC.md 9.7.2).

    make && python3 tests/kbd4f.py

`kbm_isr` used to PEEK port 60h ahead of the ROM whenever the key-state map
was armed (SPEC.md 9.7) or no mouse had spoken (9.6.4's keypad 5). On an XT's
8255, or a real 8042, a read consumes nothing. On QEMU's 8042 - and v86's,
which os8088.com's demo runs on - a read loads the NEXT queued byte, so the
peek took a byte the ROM never saw:

  'a' press + release queued together   the press LOST: nothing stored
  grey Left (E0 4B queued at once)      4B00 stored TWICE, the E0 eaten

SPEC.md 9.7.2 takes the scancode from the ROM's own int 15h AH=4Fh call
instead, on a machine whose ROM makes one, and `kbm_isr` reads no port at all.
QEMU by name, on CLAUDE.md's closed list (entry 4, an 8042): MartyPC is an
8088 with an 8255, where the peek is correct and stays.

WHAT IT ASSERTS, with the map ARMED (poked, exactly what `kbd_down`'s first
call does):

  int 15h = kbm_i15     SeaBIOS advertises 4Fh (feature byte 1 bit 4), so the
  int 09h = the ROM's   kernel hooked int 15h INSTEAD of int 09h, before the
                        first keystroke: the ROM's read is the only one
  (a) 'a' down+up in ONE input-send-event: exactly one keystroke, 1E61, and
      the map back up; then 's' and 'd' down in one event: both stored, both
      bits set, and both up in one event: nothing stored, both bits clear
  (b) grey Left down: EXACTLY ONE keystroke and it is the ROM's E0 form
      (AH = 4B, AL = E0 - SeaBIOS's 4BE0, which only a ROM that SAW the
      prefix stores), map[4B] set; up: nothing stored, map[4B] clear
  (c) the map is (a) and (b)'s bits
  (d) Alt+Enter, both downs in ONE event (SPEC.md 9.7.1): `kbd_track`
      reaches its latch store with AL = 1Ch and Alt down in the map - caught
      by a BREAKPOINT at `kbd_track.latch` through QEMU's GDB stub, because
      ui_task spends [kbd_ae] within a pass - the ROM stores Alt+Enter once
      (SeaBIOS's 1CF0, so the ROM saw the Alt too), and ui_task spends it

Every keystroke the ROM stores is read back off the BIOS ring between the
head before the event and the tail after it, so a key the UI task has already
taken is still counted. Nothing here is a time: every wait is the guest's own
state, budgeted in guest seconds (os88qemu.acted).

THE RED ARM is the kernel before SPEC.md 9.7.2: (a) stores nothing for the
batched 'a', (b) stores 4B00 twice, and int 09h is kbm_isr's.
"""
import os
import socket
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88fixture                                          # noqa: E402
import os88qemu                                             # noqa: E402
import os88sym                                              # noqa: E402
import xmcheck                                              # noqa: E402

KB_HEAD, KB_TAIL = 0x41A, 0x41C
KB_BEG, KB_END = 0x1E, 0x3E     # 0040:001E..003D, the literal ring (SPEC.md 9.8)
GDB_SECS = 20.0                 # HOST seconds a stub may take to answer - it is
                                # the host's own socket, not the guest's time

FAILS = []


def check(ok, what):
    print("  %s  %s" % ("ok  " if ok else "FAIL", what))
    if not ok:
        FAILS.append(what)
    return ok


class Q(object):
    """Guest memory and the two QEMU doors this row uses."""

    def __init__(self, sock):
        self.sock = sock

    def read(self, a, n):
        return bytes(xmcheck.read_bytes(self.sock, a, n))

    def hmp(self, line):
        r = xmcheck.qmp_exec(self.sock, "human-monitor-command",
                             {"command-line": line})
        return r if isinstance(r, str) else ""

    def word(self, a):
        b = self.read(a, 2)
        return b[0] | (b[1] << 8)

    def keys(self, evs):
        """ONE input-send-event: every byte of every key QUEUED AT ONCE in
        QEMU's 8042, which is the case the peek could not survive."""
        xmcheck.qmp_exec(self.sock, "input-send-event", {"events": [
            {"type": "key", "data": {"down": d, "key": {"type": "qcode",
                                                         "data": k}}}
            for k, d in evs]})


class Stub(object):
    """QEMU's GDB stub, for ONE thing os88qemu.poke has no use for: a WRITE
    WATCHPOINT (Z2). Connecting halts the guest; `D` detaches and resumes."""

    def __init__(self, q):
        self.q = q
        s0 = socket.socket()
        s0.bind(("127.0.0.1", 0))
        self.port = s0.getsockname()[1]
        s0.close()
        q.hmp("gdbserver tcp:127.0.0.1:%d" % self.port)
        self.s = None
        for _ in range(100):
            try:
                self.s = socket.create_connection(("127.0.0.1", self.port),
                                                  timeout=GDB_SECS)
                break
            except OSError:
                time.sleep(0.1)
        if self.s is None:
            raise SystemExit("kbd4f: no GDB stub on port %d" % self.port)
        self.buf = b""
        self.packet("?")

    def send(self, body):
        b = body.encode("ascii")
        self.s.sendall(b"$" + b + b"#%02x" % (sum(b) & 0xFF))

    def reply(self):
        while True:
            while b"#" not in self.buf or \
                    len(self.buf) < self.buf.index(b"#") + 3:
                got = self.s.recv(4096)
                if not got:
                    raise SystemExit("kbd4f: the GDB stub hung up")
                self.buf += got
            self.buf = self.buf.lstrip(b"+")
            if self.buf.startswith(b"$"):
                end = self.buf.index(b"#")
                r, self.buf = self.buf[1:end], self.buf[end + 3:]
                self.s.sendall(b"+")
                return r.decode("ascii", "replace")
            self.buf = self.buf[1:]

    def packet(self, body):
        self.send(body)
        return self.reply()

    def mem(self, a, n):
        return bytes.fromhex(self.packet("m%x,%x" % (a, n)))

    def close(self):
        try:
            self.packet("D")
        finally:
            self.s.close()
            self.q.hmp("gdbserver none")


def ring(q, head0):
    """The words the ROM stored from `head0` to the tail now, wrapped."""
    out, a = [], head0
    tail = q.word(KB_TAIL)
    for _ in range(16):
        if a == tail:
            break
        out.append(q.word(0x400 + a))
        a += 2
        if a >= KB_END:
            a = KB_BEG
    return out


def main():
    os88fixture.need("build/os8088.img", "build/apps.img")
    sym = os88sym.linear
    inst = os88qemu.make_test()
    q = Q(inst.sock)
    try:
        if not os88qemu.kernel_up(q, sym):
            raise SystemExit("kbd4f: the kernel never reached the desktop")
        armed, dmap = sym("kbd_armed"), sym("kbd_dnmap")
        ae = sym("kbd_ae")
        try:
            i15 = sym("kbm_i15") - 0x600        # KERNEL_SEG's offset
        except Exception:
            i15 = None
        v9, v15 = q.read(0x24, 4), q.read(0x54, 4)
        seg9 = v9[2] | (v9[3] << 8)
        off15, seg15 = v15[0] | (v15[1] << 8), v15[2] | (v15[3] << 8)
        check(i15 is not None and (seg15, off15) == (0x60, i15)
              and seg9 != 0x60,
              "int 15h is KERNEL_SEG:kbm_i15 and int 09h is the ROM's, before "
              "any key: the ROM's AH=4Fh call is the only reader "
              "(int 09h %04X:%04X, int 15h %04X:%04X%s)"
              % (seg9, v9[0] | (v9[1] << 8), seg15, off15,
                 ", no kbm_i15 - the peeking kernel" if i15 is None else ""))

        # ARM the map - what kbd_down's first call does (SPEC.md 9.7)
        os88qemu.poke(q.hmp, dmap, bytes(16))
        os88qemu.poke(q.hmp, armed, b"\x01")

        def bit(sc):
            return (q.read(dmap + (sc >> 3), 1)[0] >> (sc & 7)) & 1

        def event(evs, want_words, what, settle=None):
            """Send ONE batch; wait (guest time) until the ring holds
            `want_words` new keystrokes AND `settle()` holds, then one tick
            more for anything that should NOT have come; answer the words."""
            h0 = q.word(KB_TAIL)
            q.keys(evs)
            os88qemu.acted(q, lambda: len(ring(q, h0)) >= want_words and
                           (settle is None or settle()),
                           secs=3.0, what=what, poll=0.01)
            os88qemu.pace(q, 0.25)          # a second copy has its chance
            return ring(q, h0)

        print("kbd4f: (a) a press and its release in ONE event")
        w = event([("a", True), ("a", False)], 1, "'a' stored",
                  settle=lambda: bit(0x1E) == 0)
        check(w == [0x1E61], "'a' down+up stored ONCE as 1E61 (got %s)"
              % " ".join("%04X" % x for x in w))
        check(bit(0x1E) == 0, "map[1E] up again")
        w = event([("s", True), ("d", True)], 2, "'s' and 'd' stored",
                  settle=lambda: bit(0x1F) and bit(0x20))
        check(w == [0x1F73, 0x2064], "'s','d' downs in one event stored "
              "1F73 2064 (got %s)" % " ".join("%04X" % x for x in w))
        check(bit(0x1F) == 1 and bit(0x20) == 1, "(c) map[1F] and map[20] "
              "both DOWN")
        w = event([("s", False), ("d", False)], 0, "'s','d' released",
                  settle=lambda: not bit(0x1F) and not bit(0x20))
        check(w == [] and bit(0x1F) == 0 and bit(0x20) == 0,
              "(c) both up in one event: nothing stored, both bits clear "
              "(got %s)" % " ".join("%04X" % x for x in w))

        print("kbd4f: (b) grey Left, E0 4B queued at once")
        w = event([("left", True)], 1, "grey Left stored",
                  settle=lambda: bit(0x4B) == 1)
        check(len(w) == 1 and (w[0] >> 8) == 0x4B and (w[0] & 0xFF) == 0xE0,
              "grey Left stored EXACTLY ONCE in the ROM's E0 form, 4BE0 "
              "(got %s)" % " ".join("%04X" % x for x in w))
        check(bit(0x4B) == 1, "(c) map[4B] DOWN")
        w = event([("left", False)], 0, "grey Left released",
                  settle=lambda: bit(0x4B) == 0)
        check(w == [] and bit(0x4B) == 0, "grey Left up: nothing stored, "
              "map[4B] clear (got %s)" % " ".join("%04X" % x for x in w))

        print("kbd4f: (d) Alt+Enter, both downs in ONE event (SPEC.md 9.7.1)")
        h0 = q.word(KB_TAIL)
        try:
            latch = sym("kbd_track.latch")
        except KeyError:
            latch = None
        al = alt = None
        if latch is not None:
            st = Stub(q)                    # the guest is HALTED from here
            try:
                r = st.packet("Z0,%x,1" % latch)
                if r != "OK":
                    raise SystemExit("kbd4f: the stub refused a breakpoint "
                                     "at kbd_track.latch: %r" % r)
                st.send("c")
                # QEMU refuses input-send-event on a STOPPED VM, so the event
                # goes in once the continue has it running. The breakpoint is
                # the latch store itself, so the only stop is the one asked
                # for - a watchpoint on [kbd_ae] would stop on every ui_task
                # pass, whose spend is an XCHG and so a write
                # ...and the continue is the STUB's thread, so a QMP command
                # can still find the VM stopped: ask until it runs
                for _ in range(200):
                    if (xmcheck.qmp_exec(q.sock, "query-status") or
                            {}).get("running"):
                        break
                    time.sleep(0.01)
                q.keys([("alt", True), ("ret", True)])
                try:
                    st.reply()
                except (TimeoutError, socket.timeout):
                    st.s.sendall(b"\x03")  # never latched: stop it to look
                    st.reply()
                else:
                    al = int(st.packet("g")[0:2], 16)   # EAX's low byte
                    alt = st.mem(dmap + (0x38 >> 3), 1)[0] & 1
                st.packet("z0,%x,1" % latch)
            finally:
                st.close()
        check(al == 0x1C and alt == 1, "kbd_track LATCHED Alt+Enter: stopped "
              "at kbd_track.latch with AL = 1C and map[38] down (%s)"
              % ("no such label - the kernel before it" if latch is None else
                 "never reached" if al is None else
                 "AL %02X, Alt %d" % (al, alt)))
        os88qemu.acted(q, lambda: len(ring(q, h0)) >= 1, secs=3.0,
                       what="Alt+Enter stored", poll=0.01)
        os88qemu.pace(q, 0.25)
        w = ring(q, h0)
        check(w == [0x1CF0], "the ROM stored Alt+Enter ONCE, as SeaBIOS's "
              "1CF0 (got %s)" % " ".join("%04X" % x for x in w))
        os88qemu.acted(q, lambda: q.read(ae, 1)[0] == 0, secs=3.0,
                       what="ui_task to spend the latch", poll=0.01)
        check(q.read(ae, 1)[0] == 0, "ui_task spent the latch")
        q.keys([("ret", False), ("alt", False)])
        os88qemu.acted(q, lambda: not bit(0x1C) and not bit(0x38), secs=3.0,
                       what="Alt and Enter up", poll=0.01)
        check(not bit(0x1C) and not bit(0x38), "(c) map[1C], map[38] clear")
    finally:
        inst.close()
    if FAILS:
        print("kbd4f: FAIL - %d of the checks above" % len(FAILS))
        return 1
    print("kbd4f: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
