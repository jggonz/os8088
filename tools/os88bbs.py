#!/usr/bin/env python3
"""A host-side BBS, for testing the os8088 terminal against.

`tests/thewire.py`'s in-process HTTP server is the precedent: the other end of
the wire is a thread in the test, so what the guest sees is bytes the test
chose and every assertion is about those bytes rather than about whatever a
real service happened to be serving that morning.

What it is:

  * a TCP listener that NEGOTIATES like a BBS - DO TTYPE and then SB TTYPE
    SEND, DO NAWS, WILL ECHO, WILL SGA, and DO/WILL BINARY, which is the one
    that matters because Zmodem is not 8-bit clean without it;
  * a sender of ANSI fixtures in DELIBERATELY RAGGED fragments, with the cuts
    placed inside escape sequences and inside IAC sequences on purpose. A
    parser that works on whole fixtures and fails on split ones is the single
    most likely defect in a terminal, and it cannot be provoked by a file;
  * a ZMODEM SENDER in pure Python - CRC-16 only, hex headers for ZRQINIT and
    ZFIN, binary headers for ZFILE, ZDATA and ZEOF, ZCRCG streaming with a
    ZCRCW at the end of each file, batches of several files, ZRPOS/ZNAK/ZSKIP
    honoured, and the closing OO;
  * a ZMODEM RECEIVER in pure Python beside it, which exists so that the
    sender can be checked against something other than its author's reading of
    the specification - and, where lrzsz is installed, against `sz` and `rz`
    as well, in both directions;
  * a JSON log of everything the client said, decoded: every negotiation
    reply, every subnegotiation, every key byte, and every Zmodem header.

    python3 tools/os88bbs.py --selfcheck
    python3 tools/os88bbs.py --port 8093 --fixture torture --frag 7 \\
                             --log build/bbs.json --send README.md
    python3 tools/os88bbs.py --port 8093 --dsr --once

A test imports it instead:

    srv = os88bbs.BBSServer(port=8093, fixture=stream, frag=7)
    srv.start()
    ...
    srv.stop(); log = srv.log_dict()
"""
import argparse
import errno
import json
import os
import random
import re
import select
import socket
import subprocess
import sys
import threading
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FIXDIR = os.path.join(ROOT, "tests", "fixtures", "ansi")


# =============================================================================
# CRC-16/XMODEM, spelled the way lrzsz spells it
#
# Not `binascii.crc_hqx`, deliberately. Zmodem's CRC is the AUGMENTED form -
# two zero bytes are fed in after the data - and the two conventions differ.
# Writing the update step out is what makes the agreement with `sz` checkable
# rather than hopeful, and it is also the routine an 8086 implementation
# translates directly.
# =============================================================================
def _crctab():
    t = []
    for i in range(256):
        c = i << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
        t.append(c)
    return tuple(t)


CRCTAB = _crctab()


def updcrc(b, crc):
    return (CRCTAB[(crc >> 8) & 0xFF] ^ ((crc << 8) & 0xFFFF) ^ b) & 0xFFFF


def crc16(data):
    """The value Zmodem transmits: the data, then two zero bytes."""
    crc = 0
    for b in bytearray(data):
        crc = updcrc(b, crc)
    crc = updcrc(0, crc)
    crc = updcrc(0, crc)
    return crc


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


# =============================================================================
# TELNET
# =============================================================================
IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240
OPT_BINARY, OPT_ECHO, OPT_SGA, OPT_TTYPE, OPT_NAWS = 0, 1, 3, 24, 31
TT_IS, TT_SEND = 0, 1

CMDNAME = {DONT: "DONT", DO: "DO", WONT: "WONT", WILL: "WILL",
           SB: "SB", SE: "SE", IAC: "IAC"}
OPTNAME = {OPT_BINARY: "BINARY", OPT_ECHO: "ECHO", OPT_SGA: "SGA",
           OPT_TTYPE: "TTYPE", OPT_NAWS: "NAWS"}


def optname(o):
    return OPTNAME.get(o, "OPT-%d" % o)


class TelnetIn(object):
    """The client's stream, split into application bytes and events.

    IAC IAC is a literal 0xFF and goes into the application bytes - which is
    the rule Zmodem depends on, since a binary file is full of them.
    """
    D, I, NEG, SUB, SUB_I = range(5)

    def __init__(self):
        self.state = self.D
        self.cmd = 0
        self.sub = bytearray()
        self.events = []

    def feed(self, data):
        out = bytearray()
        for b in bytearray(data):
            if self.state == self.D:
                if b == IAC:
                    self.state = self.I
                else:
                    out.append(b)
            elif self.state == self.I:
                if b == IAC:
                    out.append(IAC)                  # a literal 0xFF
                    self.state = self.D
                elif b in (DO, DONT, WILL, WONT):
                    self.cmd = b
                    self.state = self.NEG
                elif b == SB:
                    self.sub = bytearray()
                    self.state = self.SUB
                else:
                    self.events.append(dict(kind="command",
                                            cmd=CMDNAME.get(b, str(b))))
                    self.state = self.D
            elif self.state == self.NEG:
                self.events.append(dict(kind="negotiation", dir="rx",
                                        cmd=CMDNAME[self.cmd], opt=optname(b),
                                        opt_num=b))
                self.state = self.D
            elif self.state == self.SUB:
                if b == IAC:
                    self.state = self.SUB_I
                else:
                    self.sub.append(b)
            else:                                     # SUB_I
                if b == IAC:
                    self.sub.append(IAC)
                    self.state = self.SUB
                elif b == SE:
                    self._subneg(bytes(self.sub))
                    self.state = self.D
                else:
                    self.state = self.D               # malformed; drop it
        return bytes(out)

    def _subneg(self, body):
        ev = dict(kind="subnegotiation", dir="rx",
                  opt=optname(body[0]) if body else "?",
                  raw=body.hex())
        if body and body[0] == OPT_TTYPE and len(body) >= 2:
            ev["what"] = "IS" if body[1] == TT_IS else "SEND/%d" % body[1]
            ev["terminal"] = body[2:].decode("latin-1")
        elif body and body[0] == OPT_NAWS and len(body) == 5:
            ev["cols"] = (body[1] << 8) | body[2]
            ev["rows"] = (body[3] << 8) | body[4]
        self.events.append(ev)


def telnet_escape(data):
    """0xFF is doubled on the way out. Everything else goes as it is."""
    return data.replace(b"\xff", b"\xff\xff")


# =============================================================================
# THE LINK - a duplex byte channel with a one-byte read
#
# One class over a socket, a pipe and a pty, because the Zmodem code has to be
# the same code in the server and in the lrzsz interop check. If it were not,
# the interop check would be checking a second implementation.
# =============================================================================
class Timeout(Exception):
    pass


class Link(object):
    def __init__(self, fd, reader, writer, escape=None, filt=None, slow=0):
        self.fd = fd
        self._read = reader
        self._write = writer
        self._escape = escape              # bytes -> bytes, on the way out
        self._filt = filt                  # a TelnetIn, on the way in
        self.slow = slow                   # bytes per second; 0 = flat out
        self.buf = b""
        self.pos = 0
        self.tx = 0
        self.rx = 0
        self.closed = False

    def send(self, data):
        if self._escape:
            data = self._escape(data)
        self.tx += len(data)
        if not self.slow:
            self._write(data)
            return
        step = max(1, int(self.slow / 20) or 1)
        for i in range(0, len(data), step):
            piece = data[i:i + step]
            self._write(piece)
            time.sleep(len(piece) / float(self.slow))

    def _fill(self, timeout):
        r = select.select([self.fd], [], [], max(0.0, timeout))[0]
        if not r:
            raise Timeout()
        try:
            d = self._read(4096)
        except OSError as e:
            if e.errno in (errno.EIO,):                # a pty whose peer went
                raise EOFError()
            raise
        if not d:
            self.closed = True
            raise EOFError()
        self.rx += len(d)
        if self._filt:
            d = self._filt.feed(d)
        if self.pos:
            self.buf = self.buf[self.pos:]
            self.pos = 0
        self.buf += d

    def getc(self, timeout=10.0):
        deadline = time.time() + timeout
        while self.pos >= len(self.buf):
            left = deadline - time.time()
            if left <= 0:
                raise Timeout()
            self._fill(left)
        b = self.buf[self.pos]
        self.pos += 1
        return b

    def unget(self, b):
        if self.pos:
            self.pos -= 1
        else:
            self.buf = bytes([b]) + self.buf

    def pending(self):
        """Application bytes waiting, after one non-blocking read."""
        try:
            self._fill(0.0)
        except (Timeout, EOFError):
            pass
        return len(self.buf) - self.pos

    def drain(self):
        """Read and discard whatever is buffered - used between fragments so
        the telnet filter sees the client's replies promptly."""
        self.pending()
        self.buf, self.pos = b"", 0


def socket_link(sock, filt=None, slow=0):
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return Link(sock.fileno(), sock.recv, sock.sendall,
                escape=telnet_escape, filt=filt, slow=slow)


def fd_link(rfd, wfd=None, slow=0):
    wfd = rfd if wfd is None else wfd
    return Link(rfd, lambda n: os.read(rfd, n),
                lambda d: _write_all(wfd, d), slow=slow)


def _write_all(fd, data):
    while data:
        n = os.write(fd, data)
        data = data[n:]


# =============================================================================
# ZMODEM
# =============================================================================
ZPAD, ZDLE, ZDLEE = 0x2A, 0x18, 0x58
ZBIN, ZHEX, ZBIN32 = 0x41, 0x42, 0x43
XON, XOFF = 0x11, 0x13

(ZRQINIT, ZRINIT, ZSINIT, ZACK, ZFILE, ZSKIP, ZNAK, ZABORT, ZFIN, ZRPOS,
 ZDATA, ZEOF, ZFERR, ZCRC, ZCHALLENGE, ZCOMPL, ZCAN, ZFREECNT, ZCOMMAND,
 ZSTDERR) = range(20)

FRAMENAME = ("ZRQINIT", "ZRINIT", "ZSINIT", "ZACK", "ZFILE", "ZSKIP", "ZNAK",
             "ZABORT", "ZFIN", "ZRPOS", "ZDATA", "ZEOF", "ZFERR", "ZCRC",
             "ZCHALLENGE", "ZCOMPL", "ZCAN", "ZFREECNT", "ZCOMMAND", "ZSTDERR")

ZCRCE, ZCRCG, ZCRCQ, ZCRCW = 0x68, 0x69, 0x6A, 0x6B
ZRUB0, ZRUB1 = 0x6C, 0x6D
ENDNAME = {ZCRCE: "ZCRCE", ZCRCG: "ZCRCG", ZCRCQ: "ZCRCQ", ZCRCW: "ZCRCW"}

# ZRINIT capability bits, in hdr[3] (which is ZF0)
CANFDX, CANOVIO, CANBRK, CANCRY, CANLZW, CANFC32, ESCCTL, ESC8 = (
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80)
ZCBIN = 1                                   # ZFILE conversion: binary


def framename(t):
    return FRAMENAME[t] if 0 <= t < len(FRAMENAME) else "Z?%d" % t


class ZmError(Exception):
    pass


class ZmCancelled(ZmError):
    pass


def pos_bytes(pos):
    return [pos & 0xFF, (pos >> 8) & 0xFF, (pos >> 16) & 0xFF,
            (pos >> 24) & 0xFF]


def bytes_pos(hdr):
    return hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | (hdr[3] << 24)


class ZmodemBase(object):
    """The half both ends share: escaping out, un-escaping in, headers both
    ways. The sender and the receiver differ in their loops, not in this."""

    def __init__(self, link, log=None):
        self.link = link
        self.log = log if log is not None else []
        self.lastsent = 0
        self.escctl = False
        self.cans = 0

    # --- out ------------------------------------------------------------------
    def _esc(self, c, out):
        """ZDLE escaping, byte for byte as lrzsz's zsendline does it.

        The CR rule is the subtle one: a carriage return is escaped only when
        the byte before it was an '@', because `@\\r` is what a Telnet
        implementation of the old school would eat. Getting it wrong produces
        a transfer that works on every file except the ones with an '@' in
        them at a 1-in-16,000 rate, which is the worst kind of bug to have.
        """
        if c == ZDLE:
            out.append(ZDLE)
            c ^= 0x40
        elif c in (0x0D, 0x8D):
            if self.escctl or (self.lastsent & 0x7F) == 0x40:
                out.append(ZDLE)
                c ^= 0x40
        elif c in (0x10, 0x11, 0x13, 0x90, 0x91, 0x93):
            out.append(ZDLE)
            c ^= 0x40
        elif self.escctl and not (c & 0x60):
            out.append(ZDLE)
            c ^= 0x40
        out.append(c)
        self.lastsent = c

    def send_hex(self, typ, hdr):
        out = bytearray([ZPAD, ZPAD, ZDLE, ZHEX])
        crc = updcrc(typ, 0)
        out += b"%02x" % typ
        for b in hdr:
            crc = updcrc(b, crc)
            out += b"%02x" % b
        crc = updcrc(0, updcrc(0, crc))
        out += b"%02x%02x" % ((crc >> 8) & 0xFF, crc & 0xFF)
        out += b"\r\n"
        if typ not in (ZFIN, ZACK):
            out.append(XON)                  # uncork a remote that has XOFFed
        self.lastsent = 0
        self.link.send(bytes(out))
        self._note("tx", typ, hdr, "hex")

    def send_bin(self, typ, hdr, fmt=ZBIN):
        out = bytearray([ZPAD, ZDLE, fmt])   # ONE pad on a binary header
        self.lastsent = 0
        if fmt == ZBIN32:
            body = bytearray([typ]) + bytearray(hdr)
            for c in body:
                self._esc(c, out)
            c32 = crc32(bytes(body))
            for i in range(4):
                self._esc((c32 >> (8 * i)) & 0xFF, out)
        else:
            crc = updcrc(typ, 0)
            self._esc(typ, out)
            for b in hdr:
                crc = updcrc(b, crc)
                self._esc(b, out)
            crc = updcrc(0, updcrc(0, crc))
            self._esc((crc >> 8) & 0xFF, out)
            self._esc(crc & 0xFF, out)
        self.link.send(bytes(out))
        self._note("tx", typ, hdr, "bin32" if fmt == ZBIN32 else "bin")

    def send_data(self, data, frameend):
        out = bytearray()
        crc = 0
        for b in bytearray(data):
            crc = updcrc(b, crc)
            self._esc(b, out)
        out.append(ZDLE)
        out.append(frameend)                 # the terminator is NOT escaped
        crc = updcrc(frameend, crc)
        crc = updcrc(0, updcrc(0, crc))
        self._esc((crc >> 8) & 0xFF, out)
        self._esc(crc & 0xFF, out)
        if frameend == ZCRCW:
            out.append(XON)
        self.link.send(bytes(out))

    def send_cancel(self):
        self.link.send(b"\x18" * 8 + b"\x08" * 8)

    # --- in -------------------------------------------------------------------
    def _raw(self, timeout):
        c = self.link.getc(timeout)
        self.cans = self.cans + 1 if c == ZDLE else 0
        if self.cans >= 5:
            raise ZmCancelled("five CANs")
        return c

    def _unesc(self, timeout):
        """One decoded byte, or ('end', frameend). Raises on a bad escape."""
        c = self._raw(timeout)
        if c != ZDLE:
            return c
        c = self.link.getc(timeout)
        if c in (ZCRCE, ZCRCG, ZCRCQ, ZCRCW):
            return ("end", c)
        if c == ZRUB0:
            return 0x7F
        if c == ZRUB1:
            return 0xFF
        if c == ZDLE:
            raise ZmCancelled("CAN CAN")
        # `(c AND 0x60) == 0x40` is that byte XOR 0x40 - which is how a literal
        # 0x18 arrives, as `ZDLE X`. lrzsz's own test, and the terminal's: the
        # four terminators and the two rubouts sit inside the range and are
        # tested first, so the order of these arms is the rule.
        if (c & 0x60) == 0x40:
            return c ^ 0x40
        raise ZmError("bad ZDLE escape 0x%02X" % c)

    def _need(self, timeout):
        v = self._unesc(timeout)
        if isinstance(v, tuple):
            raise ZmError("a frame terminator where a byte was wanted")
        return v

    def read_header(self, timeout=15.0):
        """-> (type, [4 bytes], format). Resynchronises on rubbish, which is
        what the ZPAD scan is FOR: a hex header leaves a CR LF XON behind and
        a BBS leaves whatever it was saying."""
        deadline = time.time() + timeout
        while True:
            left = deadline - time.time()
            if left <= 0:
                raise Timeout()
            c = self._raw(left)
            if c != ZPAD:
                continue
            c = self._raw(max(0.1, deadline - time.time()))
            if c == ZPAD:
                c = self._raw(max(0.1, deadline - time.time()))
            if c != ZDLE:
                continue
            fmt = self._raw(max(0.1, deadline - time.time()))
            left = max(0.5, deadline - time.time())
            if fmt == ZHEX:
                got = self._hex_header(left)
            elif fmt == ZBIN:
                got = self._bin_header(left, False)
            elif fmt == ZBIN32:
                got = self._bin_header(left, True)
            else:
                continue
            if got is None:
                continue
            typ, hdr = got
            name = {ZHEX: "hex", ZBIN: "bin", ZBIN32: "bin32"}[fmt]
            self._note("rx", typ, hdr, name)
            return typ, hdr, fmt

    def _hex_header(self, timeout):
        digits = bytearray()
        while len(digits) < 14:
            digits.append(self.link.getc(timeout))
        try:
            raw = bytearray.fromhex(digits.decode("ascii"))
        except (ValueError, UnicodeDecodeError):
            return None
        typ, hdr, got = raw[0], list(raw[1:5]), (raw[5] << 8) | raw[6]
        crc = updcrc(typ, 0)
        for b in hdr:
            crc = updcrc(b, crc)
        crc = updcrc(0, updcrc(0, crc))
        if crc != got:
            self.log.append(dict(kind="zmodem", dir="rx", error="hex CRC"))
            return None
        # lrzsz throws away the CR and, if it was a CR, one more byte. The XON
        # that may follow is left for the next ZPAD scan to skip.
        c = self.link.getc(timeout)
        if c in (0x0D, 0x8D):
            self.link.getc(timeout)
        return typ, hdr

    def _bin_header(self, timeout, is32):
        body = bytearray()
        for _ in range(5):
            body.append(self._need(timeout))
        if is32:
            want = 0
            for i in range(4):
                want |= self._need(timeout) << (8 * i)
            ok = crc32(bytes(body)) == want
        else:
            crc = 0
            for b in body:
                crc = updcrc(b, crc)
            crc = updcrc(0, updcrc(0, crc))
            got = (self._need(timeout) << 8) | self._need(timeout)
            ok = crc == got
        if not ok:
            self.log.append(dict(kind="zmodem", dir="rx", error="header CRC"))
            return None
        return body[0], list(body[1:5])

    def read_subpacket(self, timeout=15.0, is32=False):
        """-> (data, frameend). A CRC failure raises rather than returning a
        flag: every caller answers it the same way, with a ZRPOS."""
        data = bytearray()
        while True:
            v = self._unesc(timeout)
            if isinstance(v, tuple):
                frameend = v[1]
                break
            data.append(v)
        if is32:
            want = 0
            for i in range(4):
                want |= self._need(timeout) << (8 * i)
            ok = crc32(bytes(data) + bytes([frameend])) == want
        else:
            got = (self._need(timeout) << 8) | self._need(timeout)
            ok = crc16(bytes(data) + bytes([frameend])) == got
        if not ok:
            raise ZmError("subpacket CRC (%d bytes, %s)"
                          % (len(data), ENDNAME.get(frameend, "?")))
        return bytes(data), frameend

    def _note(self, dr, typ, hdr, fmt):
        self.log.append(dict(kind="zmodem", dir=dr, frame=framename(typ),
                             hdr=list(hdr), fmt=fmt, pos=bytes_pos(hdr)))


class ZmodemSender(ZmodemBase):
    """Sends one or more files. CRC-16 only, and it never offers CRC-32.

    The file loop is Forsberg's: ZRQINIT until a ZRINIT comes back, then per
    file a ZFILE and its name subpacket, a ZRPOS saying where to start, a
    ZDATA and a run of ZCRCG subpackets ending in a ZCRCW, a ZEOF, and another
    ZRINIT. Then ZFIN, the receiver's ZFIN, and `OO`.
    """

    def __init__(self, link, files, log=None, subpacket=1024, retries=10,
                 bin32_first=False):
        ZmodemBase.__init__(self, link, log)
        self.files = list(files)
        self.subpacket = subpacket
        self.retries = retries
        self.bin32_first = bin32_first     # send ONE ZBIN32 header, to be NAKed
        self.rx_bufsize = 0
        self.result = []

    def run(self, timeout=30.0):
        for _ in range(self.retries):
            self.send_hex(ZRQINIT, [0, 0, 0, 0])
            try:
                typ, hdr, _f = self.read_header(timeout)
            except Timeout:
                continue
            if typ == ZRINIT:
                self._rinit(hdr)
                break
            if typ == ZCHALLENGE:
                self.send_hex(ZACK, hdr)
            if typ == ZFIN:
                self.send_hex(ZFIN, [0, 0, 0, 0])
                return self.result
        else:
            raise Timeout("no ZRINIT")

        for i, path in enumerate(self.files):
            self.result.append(self._one(path, len(self.files) - i, timeout))

        for _ in range(self.retries):
            self.send_hex(ZFIN, [0, 0, 0, 0])
            try:
                typ, _h, _f = self.read_header(timeout)
            except Timeout:
                continue
            if typ == ZFIN:
                break
        self.link.send(b"OO")
        return self.result

    def _rinit(self, hdr):
        self.rx_bufsize = hdr[0] | (hdr[1] << 8)
        flags = hdr[3]
        self.escctl = bool(flags & ESCCTL)
        self.log.append(dict(kind="zmodem", dir="rx", what="ZRINIT flags",
                             bufsize=self.rx_bufsize, flags=flags,
                             canfdx=bool(flags & CANFDX),
                             canovio=bool(flags & CANOVIO),
                             canfc32=bool(flags & CANFC32),
                             escctl=self.escctl))

    def _one(self, path, files_left, timeout):
        with open(path, "rb") as fh:
            data = fh.read()
        name = os.path.basename(path).encode("latin-1", "replace")
        st = os.stat(path)
        info = (name + b"\0" + b"%d %o 0 0 %d %d"
                % (len(data), int(st.st_mtime), files_left, len(data)) + b"\0")

        pos = None
        fmt = ZBIN32 if self.bin32_first else ZBIN
        for _ in range(self.retries):
            # A receiver sends ZRINIT until somebody answers, so by the time
            # the first ZFILE goes out there is usually a SECOND ZRINIT in
            # flight - and a sender that treats it as the answer to this
            # ZFILE, resends, and then reads the previous attempt's reply is
            # one whole frame behind for the rest of the batch. It shows up as
            # every file after the first inheriting the file before's answer,
            # which is a bug that looks like a protocol disagreement and is
            # not. `sz` purges the line here for the same reason.
            self.link.drain()
            self.send_bin(ZFILE, [0, 0, 0, ZCBIN], fmt)
            self.send_data(info, ZCRCW)
            fmt = ZBIN                       # only ever ONE deliberate ZBIN32
            resend = False
            for _ in range(self.retries):
                try:
                    typ, hdr, _f = self.read_header(timeout)
                except Timeout:
                    resend = True
                    break
                if typ == ZRPOS:
                    pos = bytes_pos(hdr)
                    break
                if typ == ZSKIP:
                    return dict(name=name.decode("latin-1"), sent=0,
                                result="skipped")
                if typ == ZFIN:
                    return dict(name=name.decode("latin-1"), sent=0,
                                result="receiver finished")
                if typ == ZNAK:
                    resend = True
                    break
                # ZRINIT and anything else: a straggler. Look for the NEXT
                # header rather than saying the whole ZFILE again.
            if pos is not None or not resend:
                break
        if pos is None:
            raise Timeout("no ZRPOS for %s" % path)

        sent = self._stream(data, pos, timeout)
        return dict(name=name.decode("latin-1"), size=len(data), sent=sent,
                    sha256=__import__("hashlib").sha256(data).hexdigest(),
                    result="sent")

    def _stream(self, data, pos, timeout):
        total = 0
        for _ in range(self.retries):
            self.send_bin(ZDATA, pos_bytes(pos))
            restart = False
            while pos < len(data):
                chunk = data[pos:pos + self.subpacket]
                last = pos + len(chunk) >= len(data)
                end = ZCRCW if last else ZCRCG
                self.send_data(chunk, end)
                pos += len(chunk)
                total += len(chunk)
                if end == ZCRCW:
                    typ, hdr, _f = self.read_header(timeout)
                    if typ == ZACK:
                        continue
                    if typ == ZRPOS:
                        pos, restart = bytes_pos(hdr), True
                        break
                    if typ == ZSKIP:
                        return total
                    raise ZmError("%s where a ZACK was wanted"
                                  % framename(typ))
                elif self.link.pending():
                    # The receiver only speaks mid-stream to complain.
                    try:
                        typ, hdr, _f = self.read_header(2.0)
                    except (Timeout, ZmError):
                        continue
                    if typ == ZRPOS:
                        pos, restart = bytes_pos(hdr), True
                        break
                    if typ == ZSKIP:
                        return total
            if restart:
                continue
            for _ in range(self.retries):
                self.send_hex(ZEOF, pos_bytes(len(data)))
                typ, hdr, _f = self.read_header(timeout)
                if typ == ZRINIT:
                    return total
                if typ == ZRPOS:
                    pos, restart = bytes_pos(hdr), True
                    break
                if typ in (ZNAK, ZEOF):
                    continue
                if typ == ZFIN:
                    return total
            if not restart:
                raise Timeout("no ZRINIT after ZEOF")
        raise ZmError("too many restarts")


class ZmodemReceiver(ZmodemBase):
    """The other end, so the sender has something to be checked against.

    It advertises exactly what the os8088 terminal will advertise -
    CANFDX|CANOVIO and NOT CANFC32 - so a sender that would have chosen
    CRC-32 against a better-equipped receiver chooses CRC-16 here, which is
    the case the 8086 has to survive.
    """

    def __init__(self, link, outdir, log=None, bufsize=0, skip=()):
        ZmodemBase.__init__(self, link, log)
        self.outdir = outdir
        self.bufsize = bufsize
        self.skip = set(skip)                # names to answer ZSKIP for
        self.files = []

    def _rinit(self):
        self.send_hex(ZRINIT, [self.bufsize & 0xFF, (self.bufsize >> 8) & 0xFF,
                               0, CANFDX | CANOVIO])

    def run(self, timeout=30.0):
        self._rinit()
        pos = 0
        fh = None
        name = None
        want = 0
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                typ, hdr, fmt = self.read_header(min(15.0, timeout))
            except Timeout:
                self._rinit()
                continue
            is32 = fmt == ZBIN32
            if typ == ZRQINIT:
                self._rinit()
            elif typ == ZFILE:
                if is32:
                    # The terminal will not do CRC-32, and a sender that tries
                    # is told so rather than left waiting.
                    self.send_hex(ZNAK, [0, 0, 0, 0])
                    continue
                try:
                    info, _end = self.read_subpacket(15.0, is32)
                except ZmError:
                    self.send_hex(ZNAK, [0, 0, 0, 0])
                    continue
                name = info.split(b"\0")[0].decode("latin-1")
                rest = info.split(b"\0")[1].split() if b"\0" in info else []
                want = int(rest[0]) if rest else 0
                if name in self.skip:
                    self.send_hex(ZSKIP, [0, 0, 0, 0])
                    self.files.append(dict(name=name, result="skipped"))
                    name = None
                    continue
                if fh:
                    fh.close()
                fh = open(os.path.join(self.outdir, os.path.basename(name)),
                          "wb")
                pos = 0
                self.send_hex(ZRPOS, pos_bytes(0))
            elif typ == ZDATA:
                if fh is None or bytes_pos(hdr) != pos:
                    self.send_hex(ZRPOS, pos_bytes(pos))
                    continue
                while True:
                    try:
                        data, end = self.read_subpacket(15.0, is32)
                    except ZmError:
                        self.send_hex(ZRPOS, pos_bytes(pos))
                        break
                    fh.write(data)
                    pos += len(data)
                    if end == ZCRCW:
                        self.send_hex(ZACK, pos_bytes(pos))
                        break
                    if end == ZCRCQ:
                        self.send_hex(ZACK, pos_bytes(pos))
                    if end == ZCRCE:
                        break
            elif typ == ZEOF:
                if fh and bytes_pos(hdr) == pos:
                    fh.close()
                    fh = None
                    self.files.append(dict(name=name, size=pos,
                                           result="received"))
                    name = None
                    self._rinit()
                else:
                    self.send_hex(ZRPOS, pos_bytes(pos))
            elif typ == ZFIN:
                self.send_hex(ZFIN, [0, 0, 0, 0])
                try:
                    self.link.getc(3.0)      # the first O of OO
                    self.link.getc(1.0)
                except (Timeout, EOFError):
                    pass
                break
            elif typ in (ZSINIT, ZCOMMAND):
                try:
                    self.read_subpacket(5.0, is32)
                except (ZmError, Timeout):
                    pass
                self.send_hex(ZACK, [0, 0, 0, 0])
            elif typ == ZCAN or typ == ZABORT:
                break
        if fh:
            fh.close()
        return self.files


# =============================================================================
# 8.3 MANGLING, the FTP server's rule and not a second opinion
#
# A file downloaded here by Zmodem and the same file uploaded by FTP must land
# on the same name, so this is a second copy of `fd_mangle83`'s RULE - a
# package cannot call another package's proc - and the point of putting it
# here is that the Zmodem test asserts both ends against this one table.
#
# The characters are `fd_c83`'s set, which mirrors the kernel's `dskw_char_x`:
# A-Z, 0-9 upcased, and sixteen symbols. A SPACE is not among them, which is
# what `banana split.mod` fell foul of in the first place.
# =============================================================================
M83_SYMS = "$%'-_@~`!(){}^#&"


def _c83(ch):
    """One character upcased, or None when 8.3 may not hold it."""
    if "a" <= ch <= "z":
        ch = chr(ord(ch) - 32)
    if "A" <= ch <= "Z" or "0" <= ch <= "9" or ch in M83_SYMS:
        return ch
    return None


def is83(name):
    """Is this already a legal 8.3 name? Stem 1..8, at most one dot,
    extension 1..3 if the dot is there."""
    i, n, stem = 0, len(name), 0
    while i < n and name[i] != ".":
        if _c83(name[i]) is None or stem >= 8:
            return False
        stem += 1
        i += 1
    if i >= n:
        return stem > 0
    if stem == 0:
        return False                       # `.TXT` has no stem
    i += 1
    ext = 0
    while i < n:
        if name[i] == "." or _c83(name[i]) is None or ext >= 3:
            return False
        ext += 1
        i += 1
    return ext > 0                         # ...and a trailing dot is not one


def mangle83(path):
    """A sender's pathname -> the name this machine will store it under.

    Everything up to the last separator is dropped, **a name that is already
    legal is left alone** - which is what makes the mapping round-trip, since
    the mangled name is itself legal and passes through untouched - and
    otherwise it is the first six legal characters, `~1`, and the first three
    legal characters after the LAST dot. Always `~1`, never a search for a
    free `~2`: a counter resolved against the directory would make the same
    download land differently on two disks.
    """
    leaf = path.replace("\\", "/").rsplit("/", 1)[-1]
    if is83(leaf):
        return leaf
    dot = leaf.rfind(".")
    ext = ""
    if dot >= 0:
        for ch in leaf[dot + 1:]:
            c = _c83(ch)
            if c is None:
                continue
            if len(ext) >= 3:
                break
            ext += c
    stem = ""
    for ch in leaf[:dot if dot >= 0 else len(leaf)]:
        c = _c83(ch)
        if c is None:
            continue
        if len(stem) >= 6:
            break
        stem += c
    if not stem:
        stem = "FILE"                      # it still has to be something a
    out = stem + "~1"                      # person can see and delete
    return out + "." + ext if ext else out


# The table both ends are asserted against. A row here is a claim about the
# 8086 as much as about this file.
MANGLE83_CASES = (
    ("README.TXT", "README.TXT"),          # already legal: left ALONE
    ("readme.txt", "readme.txt"),          # ...case and all
    ("BANANA~1.MOD", "BANANA~1.MOD"),      # the round trip
    ("A", "A"),
    ("banana split.mod", "BANANA~1.MOD"),  # SPEC's own example
    ("/pub/files/banana split.mod", "BANANA~1.MOD"),   # the path is dropped
    ("C:\\dl\\banana split.mod", "BANANA~1.MOD"),
    ("longfilename.text", "LONGFI~1.TEX"),
    ("no-dot-at-all-here", "NO-DOT~1"),
    ("x.verylongext", "X~1.VER"),
    ("a.b.c", "AB~1.C"),                   # the LAST dot is the extension
    (".bashrc", "FILE~1.BAS"),             # nothing before the dot
    ("...", "FILE~1"),                     # nothing legal anywhere
    ("   ", "FILE~1"),
    ("", "FILE~1"),
    ("A B C.T X T", "ABC~1.TXT"),          # spaces are simply dropped
)


# =============================================================================
# THE SERVER
# =============================================================================
def load_fixture(spec):
    """A path, or the bare name of one of tests/fixtures/ansi/*.bin."""
    if spec is None:
        return b""
    for cand in (spec, os.path.join(FIXDIR, spec),
                 os.path.join(FIXDIR, spec + ".bin")):
        if os.path.isfile(cand):
            with open(cand, "rb") as fh:
                return fh.read()
    raise SystemExit("os88bbs: no fixture %r (looked in %s)" % (spec, FIXDIR))


def fragment(data, seed, maxlen=48):
    """Cut a stream into ragged fragments, deterministically.

    Random cuts alone are not enough. What has to be split is an ESCAPE
    SEQUENCE and an IAC SEQUENCE, and those are a small fraction of the bytes,
    so a uniform cut misses most of them: the cuts one byte and two bytes past
    every ESC and every IAC are FORCED, and the random ones fill in around
    them. A terminal that only ever sees whole sequences in its tests has not
    been tested.
    """
    rng = random.Random(seed)
    cuts = set()
    for i, b in enumerate(bytearray(data)):
        if b in (0x1B, 0xFF):
            for d in (1, 2, 3):
                if i + d < len(data):
                    cuts.add(i + d)
    i = 0
    while i < len(data):
        i += rng.randint(1, maxlen)
        if i < len(data):
            cuts.add(i)
    out, last = [], 0
    for c in sorted(cuts):
        out.append(data[last:c])
        last = c
    out.append(data[last:])
    return [f for f in out if f]


ANSWER_RE = re.compile(rb"\x1b\[[0-9;?]*[Rnc]")


class BBSServer(threading.Thread):
    """One connection at a time, on purpose: a test wants one client and a
    log it can read afterwards, and a second connection would make the log
    ambiguous about which client said what."""

    def __init__(self, port=8093, fixture=b"", frag=0, fragdelay=0.02,
                 dsr=False, files=(), log_path=None, banner=None,
                 timeout=180.0, once=True, slow=0, bin32=False, zwait=0.0,
                 host="0.0.0.0", verbose=False):
        threading.Thread.__init__(self, daemon=True)
        self.fixture = fixture
        self.frag = frag
        self.fragdelay = fragdelay
        self.dsr = dsr
        self.files = list(files)
        self.log_path = log_path
        self.banner = banner
        self.timeout = timeout
        self.once = once
        self.slow = slow
        self.bin32 = bin32
        self.zwait = zwait
        self.verbose = verbose
        self.events = []
        self.keys = bytearray()
        self.raw_rx = bytearray()
        self.zsummary = []
        self.done = threading.Event()
        # Re-entrant: _ev takes it and then calls _flush, which takes it again.
        self._logl = threading.RLock()
        self.error = None
        self._stop = False
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((host, port))
        self.sock.listen(2)
        self.port = self.sock.getsockname()[1]
        self.t0 = time.time()

    # --- the log --------------------------------------------------------------
    def _ev(self, **kw):
        kw["t"] = round(time.time() - self.t0, 3)
        with self._logl:
            self.events.append(kw)
        if self.verbose:
            print("  bbs %s" % json.dumps(kw, sort_keys=True), file=sys.stderr)
        self._flush()

    def log_dict(self):
        answers = ANSWER_RE.findall(bytes(self.keys))
        return dict(
            port=self.port,
            fixture_bytes=len(self.fixture),
            events=list(self.events),
            negotiation=[e for e in self.events if e.get("kind") == "negotiation"],
            subnegotiation=[e for e in self.events
                            if e.get("kind") == "subnegotiation"],
            zmodem=[e for e in self.events if e.get("kind") == "zmodem"],
            zmodem_files=self.zsummary,
            keys=bytes(self.keys).decode("latin-1"),
            keys_hex=bytes(self.keys).hex(),
            key_count=len(self.keys),
            answers=[a.decode("latin-1") for a in answers],
            raw_rx_hex=bytes(self.raw_rx[:262144]).hex(),
            error=self.error,
        )

    def _flush(self):
        """Rewrite the log after every event, so a test can poll it while the
        session is still running. Under a lock and through a UNIQUE temporary:
        the session thread and whoever calls stop() both flush, and two
        writers sharing one `.tmp` name lose the race as a FileNotFoundError
        out of os.replace rather than as a corrupt file - which is better, but
        still a crash at the end of a clean run."""
        if not self.log_path:
            return
        with self._logl:
            tmp = "%s.%d.tmp" % (self.log_path, os.getpid())
            with open(tmp, "w") as fh:
                json.dump(self.log_dict(), fh, indent=1, sort_keys=True)
            os.replace(tmp, self.log_path)

    # --- the connection -------------------------------------------------------
    def run(self):
        try:
            while not self._stop:
                try:
                    conn, peer = self.sock.accept()
                except OSError:
                    return
                self._ev(kind="connect", peer="%s:%d" % peer)
                try:
                    self._session(conn)
                except (EOFError, ConnectionResetError, BrokenPipeError):
                    self._ev(kind="disconnect")
                except Exception as e:                       # noqa: BLE001
                    self.error = "%s: %s" % (type(e).__name__, e)
                    self._ev(kind="error", error=self.error)
                finally:
                    try:
                        conn.close()
                    except OSError:
                        pass
                if self.once:
                    break
        finally:
            self.done.set()
            self._flush()

    def _session(self, conn):
        filt = TelnetIn()
        link = socket_link(conn, filt, self.slow)
        raw_read = conn.recv

        def reader(n):
            d = raw_read(n)
            self.raw_rx += d
            return d
        link._read = reader

        def pump(seconds=0.0):
            """Read whatever is there, move the filter's events into the log
            and the application bytes into `keys`."""
            end = time.time() + seconds
            while True:
                n = link.pending()
                if n:
                    self.keys += link.buf[link.pos:]
                    link.buf, link.pos = b"", 0
                for e in filt.events:
                    self._ev(**e)
                del filt.events[:]
                if time.time() >= end:
                    return
                time.sleep(0.01)

        # 1. the negotiation a BBS opens with
        for cmd, opt in ((DO, OPT_TTYPE), (DO, OPT_NAWS), (WILL, OPT_ECHO),
                         (WILL, OPT_SGA), (DO, OPT_BINARY), (WILL, OPT_BINARY)):
            conn.sendall(bytes([IAC, cmd, opt]))
            self._ev(kind="negotiation", dir="tx", cmd=CMDNAME[cmd],
                     opt=optname(opt), opt_num=opt)
        pump(0.6)

        # ...and the TTYPE question, which only makes sense once it has said
        # WILL TTYPE. Asked anyway if it has not: a terminal that answers is
        # more interesting to a test than one that is never asked.
        conn.sendall(bytes([IAC, SB, OPT_TTYPE, TT_SEND, IAC, SE]))
        self._ev(kind="subnegotiation", dir="tx", opt="TTYPE", what="SEND")
        pump(0.6)

        if self.banner:
            link.send(self.banner)
            pump(0.1)

        # 2. the fixture, in ragged fragments
        if self.fixture:
            frags = (fragment(self.fixture, self.frag) if self.frag
                     else [self.fixture])
            for f in frags:
                link.send(f)
                if self.fragdelay:
                    time.sleep(self.fragdelay)
                pump()
            self._ev(kind="fixture", bytes=len(self.fixture),
                     fragments=len(frags), longest=max(len(f) for f in frags))
            pump(0.4)

        # 3. ask where the cursor is, and log what comes back
        if self.dsr:
            before = len(self.keys)
            link.send(b"\x1b[6n")
            self._ev(kind="dsr", dir="tx", what="ESC[6n")
            pump(3.0)
            reply = bytes(self.keys[before:])
            m = ANSWER_RE.search(reply)
            self._ev(kind="dsr", dir="rx", reply=reply.decode("latin-1"),
                     matched=m.group(0).decode("latin-1") if m else None)

        # 4. Zmodem
        if self.files:
            if self.zwait:
                pump(self.zwait)
            zlog = []
            link.buf, link.pos = b"", 0
            snd = ZmodemSender(link, self.files, zlog, bin32_first=self.bin32)
            try:
                self.zsummary = snd.run()
            except (ZmError, Timeout, EOFError) as e:
                self.zsummary = [dict(result="failed", why=str(e))]
            for e in zlog:
                self._ev(**e)
            self._ev(kind="zmodem-done", files=self.zsummary)

        # 5. sit there logging until the client goes away
        end = time.time() + self.timeout
        while not self._stop and time.time() < end:
            try:
                link._fill(0.5)
            except Timeout:
                continue
            except EOFError:
                break
            self.keys += link.buf[link.pos:]
            link.buf, link.pos = b"", 0
            for e in filt.events:
                self._ev(**e)
            del filt.events[:]
        self._ev(kind="disconnect")

    def stop(self, timeout=5.0):
        self._stop = True
        try:
            self.sock.close()
        except OSError:
            pass
        self.done.wait(timeout)
        self._flush()


# =============================================================================
# THE SELFCHECK
# =============================================================================
def _pipe_pair():
    """Two Links, back to back, over a pair of OS pipes."""
    a_r, b_w = os.pipe()
    b_r, a_w = os.pipe()
    return (fd_link(a_r, a_w), fd_link(b_r, b_w), (a_r, a_w, b_r, b_w))


def _pty_child(argv, cwd):
    """Run `argv` on a pty and return (pid, master_fd). lrzsz wants a terminal
    and puts it in raw mode itself; the master is raw bytes either way."""
    import pty
    import tty
    pid, fd = pty.fork()
    if pid == 0:                                    # the child
        try:
            os.chdir(cwd)
            os.dup2(os.open(os.devnull, os.O_WRONLY), 2)
            os.execvp(argv[0], argv)
        finally:
            os._exit(127)
    try:
        tty.setraw(fd)
    except Exception:                               # noqa: BLE001
        pass
    return pid, fd


def _selfcheck_internal(verbose=False):
    """Our sender against our receiver, over pipes, in a thread each."""
    import hashlib
    import shutil
    import tempfile
    ok = True
    tmp = tempfile.mkdtemp(prefix="os88bbs.")
    try:
        payloads = {
            "SMALL.TXT": b"the quick brown fox\r\n" * 7,
            # every byte value, so the ZDLE escaping is exercised on all of
            # them - 0x18, 0x10, 0x11, 0x13 and their high-bit twins, and the
            # '@' before a CR that is the one context-sensitive rule
            "BYTES.BIN": bytes(range(256)) * 40 + b"@\r@\r@\x0d",
            "BIG.DAT": bytes((i * 37 + (i >> 8) * 11) & 0xFF
                             for i in range(40000)),
        }
        src = os.path.join(tmp, "src")
        dst = os.path.join(tmp, "dst")
        os.makedirs(src)
        os.makedirs(dst)
        paths = []
        for name, body in payloads.items():
            p = os.path.join(src, name)
            with open(p, "wb") as fh:
                fh.write(body)
            paths.append(p)

        a, b, fds = _pipe_pair()
        out = {}

        def rx():
            try:
                out["files"] = ZmodemReceiver(b, dst).run(60.0)
            except Exception as e:                          # noqa: BLE001
                out["error"] = "%s: %s" % (type(e).__name__, e)

        t = threading.Thread(target=rx, daemon=True)
        t.start()
        snd = ZmodemSender(a, paths, subpacket=512)
        summary = snd.run(30.0)
        t.join(30.0)
        for fd in fds:
            try:
                os.close(fd)
            except OSError:
                pass

        if out.get("error"):
            print("selfcheck FAIL zmodem.internal: %s" % out["error"])
            ok = False
        for name, body in payloads.items():
            got = os.path.join(dst, name)
            if not os.path.exists(got):
                print("selfcheck FAIL zmodem.internal.%s never arrived" % name)
                ok = False
                continue
            with open(got, "rb") as fh:
                have = fh.read()
            if have != body:
                print("selfcheck FAIL zmodem.internal.%s: %d bytes, sha %s "
                      "!= %s" % (name, len(have),
                                 hashlib.sha256(have).hexdigest()[:16],
                                 hashlib.sha256(body).hexdigest()[:16]))
                ok = False
            elif verbose:
                print("selfcheck ok   zmodem.internal.%s (%d bytes)"
                      % (name, len(body)))
        if ok:
            print("selfcheck ok   zmodem.internal: %d files, %d bytes, "
                  "CRC-16, our sender to our receiver"
                  % (len(payloads), sum(len(v) for v in payloads.values())))
        if verbose:
            print("               %s" % json.dumps(summary))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return ok


def _selfcheck_skip(verbose=False):
    """A cancelled save is a ZSKIP, and the sender must move on to the next
    file rather than hang - which is exactly what the terminal does when the
    user cancels the Save dialog."""
    import shutil
    import tempfile
    tmp = tempfile.mkdtemp(prefix="os88bbs.")
    try:
        src, dst = os.path.join(tmp, "s"), os.path.join(tmp, "d")
        os.makedirs(src)
        os.makedirs(dst)
        paths = []
        for name in ("ONE.TXT", "TWO.TXT"):
            p = os.path.join(src, name)
            with open(p, "wb") as fh:
                fh.write(name.encode() * 100)
            paths.append(p)
        a, b, fds = _pipe_pair()
        out = {}

        def rx():
            try:
                out["files"] = ZmodemReceiver(b, dst, skip=("ONE.TXT",)).run(40.0)
            except Exception as e:                          # noqa: BLE001
                out["error"] = "%s: %s" % (type(e).__name__, e)

        t = threading.Thread(target=rx, daemon=True)
        t.start()
        summary = ZmodemSender(a, paths).run(20.0)
        t.join(20.0)
        for fd in fds:
            try:
                os.close(fd)
            except OSError:
                pass
        skipped = [f for f in summary if f.get("result") == "skipped"]
        two = os.path.join(dst, "TWO.TXT")
        got_two = os.path.exists(two) and \
            open(two, "rb").read() == b"TWO.TXT" * 100
        no_one = not os.path.exists(os.path.join(dst, "ONE.TXT"))
        ok = len(skipped) == 1 and got_two and no_one and not out.get("error")
        print("selfcheck %s zmodem.zskip: %d skipped, TWO.TXT %s, ONE.TXT %s%s"
              % ("ok  " if ok else "FAIL", len(skipped),
                 "arrived whole" if got_two else "WRONG OR MISSING",
                 "absent" if no_one else "PRESENT",
                 "" if not out.get("error") else " (%s)" % out["error"]))
        return ok
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def _selfcheck_bin32(verbose=False):
    """A ZBIN32 header must be answered with a ZNAK and the transfer must
    still complete - which is the terminal's contract, since it will not do
    CRC-32 and a sender that tries one has to be told."""
    import shutil
    import tempfile
    tmp = tempfile.mkdtemp(prefix="os88bbs.")
    try:
        src, dst = os.path.join(tmp, "s"), os.path.join(tmp, "d")
        os.makedirs(src)
        os.makedirs(dst)
        p = os.path.join(src, "C32.TXT")
        body = b"crc32 refused\r\n" * 50
        with open(p, "wb") as fh:
            fh.write(body)
        a, b, fds = _pipe_pair()
        out = {}

        def rx():
            try:
                out["files"] = ZmodemReceiver(b, dst).run(40.0)
            except Exception as e:                          # noqa: BLE001
                out["error"] = "%s: %s" % (type(e).__name__, e)

        t = threading.Thread(target=rx, daemon=True)
        t.start()
        log = []
        ZmodemSender(a, [p], log, bin32_first=True).run(20.0)
        t.join(20.0)
        for fd in fds:
            try:
                os.close(fd)
            except OSError:
                pass
        naks = [e for e in log if e.get("frame") == "ZNAK" and e["dir"] == "rx"]
        arrived = os.path.exists(os.path.join(dst, "C32.TXT")) and \
            open(os.path.join(dst, "C32.TXT"), "rb").read() == body
        ok = bool(naks) and arrived
        print("selfcheck %s zmodem.bin32-refused: %d ZNAK(s), file %s"
              % ("ok  " if ok else "FAIL", len(naks),
                 "arrived" if arrived else "MISSING"))
        return ok
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def _selfcheck_lrzsz(verbose=False):
    """Both directions against lrzsz, if it is here. THE point of this one:
    two implementations that agree with each other and with nothing else
    would both be wrong in the same way, and `sz` is what the BBS at the
    other end of a real call is running.
    """
    import hashlib
    import shutil
    import tempfile
    sz = _which("sz")
    rz = _which("rz")
    if not (sz and rz):
        print("selfcheck SKIP zmodem.lrzsz: `sz`/`rz` are not installed "
              "(brew install lrzsz)")
        return True, False
    ok = True
    tmp = tempfile.mkdtemp(prefix="os88bbs.")
    try:
        src, dst = os.path.join(tmp, "s"), os.path.join(tmp, "d")
        os.makedirs(src)
        os.makedirs(dst)
        bodies = {"LZ1.BIN": bytes(range(256)) * 30 + b"@\r@\r",
                  "LZ2.TXT": b"lrzsz interop\r\n" * 300}
        paths = []
        for n, v in bodies.items():
            p = os.path.join(src, n)
            with open(p, "wb") as fh:
                fh.write(v)
            paths.append(p)

        # --- our sender -> lrzsz's rz ----------------------------------------
        pid, fd = _pty_child([rz, "-y", "-vv"], dst)
        link = fd_link(fd)
        try:
            ZmodemSender(link, paths, subpacket=1024).run(40.0)
        except (ZmError, Timeout, EOFError) as e:
            print("selfcheck FAIL zmodem.lrzsz.tx: %s" % e)
            ok = False
        _reap(pid, fd)
        for n, v in bodies.items():
            got = os.path.join(dst, n)
            have = open(got, "rb").read() if os.path.exists(got) else None
            if have != v:
                print("selfcheck FAIL zmodem.lrzsz.tx.%s: %s" %
                      (n, "missing" if have is None
                       else "%d bytes, sha %s != %s"
                       % (len(have), hashlib.sha256(have).hexdigest()[:12],
                          hashlib.sha256(v).hexdigest()[:12])))
                ok = False
        if ok:
            print("selfcheck ok   zmodem.lrzsz.tx: our sender -> `rz`, "
                  "%d files, %d bytes" % (len(bodies),
                                          sum(len(v) for v in bodies.values())))

        # --- lrzsz's sz -> our receiver --------------------------------------
        dst2 = os.path.join(tmp, "d2")
        os.makedirs(dst2)
        pid, fd = _pty_child([sz, "-b"] + paths, src)
        link = fd_link(fd)
        rok = True
        try:
            ZmodemReceiver(link, dst2).run(60.0)
        except (ZmError, Timeout, EOFError) as e:
            print("selfcheck FAIL zmodem.lrzsz.rx: %s" % e)
            rok = False
        _reap(pid, fd)
        for n, v in bodies.items():
            got = os.path.join(dst2, n)
            have = open(got, "rb").read() if os.path.exists(got) else None
            if have != v:
                print("selfcheck FAIL zmodem.lrzsz.rx.%s: %s" %
                      (n, "missing" if have is None else "%d bytes differ"
                       % len(have)))
                rok = False
        if rok:
            print("selfcheck ok   zmodem.lrzsz.rx: `sz` -> our receiver, "
                  "%d files" % len(bodies))
        ok = ok and rok
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return ok, True


def _which(name):
    for d in os.environ.get("PATH", "").split(os.pathsep):
        p = os.path.join(d, name)
        if os.path.isfile(p) and os.access(p, os.X_OK):
            return p
    return None


def _reap(pid, fd):
    for _ in range(100):
        try:
            done, _st = os.waitpid(pid, os.WNOHANG)
        except ChildProcessError:
            break
        if done:
            break
        try:
            select.select([fd], [], [], 0.05)
            os.read(fd, 4096)
        except OSError:
            break
    else:
        try:
            os.kill(pid, 9)
            os.waitpid(pid, 0)
        except OSError:
            pass
    try:
        os.close(fd)
    except OSError:
        pass


def _selfcheck_telnet(verbose=False):
    """The negotiation half: the filter has to split an IAC sequence that
    arrives one byte at a time exactly as it splits one that arrives whole,
    and IAC IAC has to come out as a single 0xFF."""
    stream = (bytes([IAC, WILL, OPT_TTYPE]) + b"ab"
              + bytes([IAC, SB, OPT_TTYPE, TT_IS]) + b"ANSI" + bytes([IAC, SE])
              + bytes([IAC, WILL, OPT_NAWS])
              + bytes([IAC, SB, OPT_NAWS, 0, 80, 0, 25, IAC, SE])
              + bytes([IAC, IAC]) + b"z"
              + bytes([IAC, DO, OPT_ECHO]) + b"\x1b[12;34R")
    whole = TelnetIn()
    ref_app = whole.feed(stream)
    ref_ev = json.dumps(whole.events, sort_keys=True)
    ok = True
    if ref_app != b"ab\xffz\x1b[12;34R":
        print("selfcheck FAIL telnet.app-bytes: %r" % ref_app)
        ok = False
    kinds = [(e.get("cmd"), e.get("opt")) for e in whole.events
             if e["kind"] == "negotiation"]
    if kinds != [("WILL", "TTYPE"), ("WILL", "NAWS"), ("DO", "ECHO")]:
        print("selfcheck FAIL telnet.negotiation: %r" % (kinds,))
        ok = False
    subs = [e for e in whole.events if e["kind"] == "subnegotiation"]
    if len(subs) != 2 or subs[0].get("terminal") != "ANSI" \
            or subs[1].get("cols") != 80 or subs[1].get("rows") != 25:
        print("selfcheck FAIL telnet.subnegotiation: %r" % (subs,))
        ok = False
    for cut in range(len(stream) + 1):
        f = TelnetIn()
        app = f.feed(stream[:cut]) + f.feed(stream[cut:])
        if app != ref_app or json.dumps(f.events, sort_keys=True) != ref_ev:
            print("selfcheck FAIL telnet.fragment: split at %d differs" % cut)
            ok = False
            break
    if ok:
        print("selfcheck ok   telnet: %d events, %d split points"
              % (len(whole.events), len(stream) + 1))
    return ok


def _selfcheck_fragment(verbose=False):
    """The ragged sender must be lossless and deterministic, and it must
    actually cut inside escape sequences - the property it exists for."""
    data = load_fixture("torture") if os.path.isdir(FIXDIR) else \
        b"\x1b[1;31mhello\x1b[0m" * 200 + bytes([IAC, IAC]) * 50
    a = fragment(data, 7)
    b = fragment(data, 7)
    ok = True
    if b"".join(a) != data:
        print("selfcheck FAIL fragment.lossless")
        ok = False
    if [len(x) for x in a] != [len(x) for x in b]:
        print("selfcheck FAIL fragment.deterministic")
        ok = False
    inside = 0
    off = 0
    starts = set()
    for f in a:
        starts.add(off)
        off += len(f)
    for i, byte in enumerate(bytearray(data)):
        if byte == 0x1B and (i + 1) in starts:
            inside += 1
    if inside < 5:
        print("selfcheck FAIL fragment.cuts-inside-escapes: only %d" % inside)
        ok = False
    if ok:
        print("selfcheck ok   fragment: %d bytes -> %d fragments, %d cut "
              "immediately after an ESC" % (len(data), len(a), inside))
    return ok


def _selfcheck_crc(verbose=False):
    """The CRC against an implementation that is not ours.

    `binascii.crc_hqx` is CRC-16/XMODEM computed the ordinary table way, and
    this file computes it the AUGMENTED way lrzsz does - two zero bytes fed in
    at the end. The two are the same function and the identity is worth
    checking rather than believing, because it is the one place where a
    misreading would produce a transfer that works against itself and against
    nothing else. The published check word 0x31C3 pins the pair to the
    standard rather than to each other.
    """
    import binascii
    ok = crc16(b"123456789") == 0x31C3
    if not ok:
        print("selfcheck FAIL crc16('123456789') = %04X, want the published "
              "0x31C3" % crc16(b"123456789"))
    vectors = [b"", b"A", b"123456789", bytes(range(256)),
               b"@\r@\r", b"\x18\x10\x11\x13" * 9, bytes(range(256)) * 5]
    for data in vectors:
        if crc16(data) != binascii.crc_hqx(data, 0):
            print("selfcheck FAIL crc16 vs binascii.crc_hqx on %d bytes"
                  % len(data))
            ok = False
    if ok:
        print("selfcheck ok   crc16: 0x31C3, and %d vectors against "
              "binascii.crc_hqx" % len(vectors))
    return ok


def _selfcheck_server(verbose=False):
    """The whole server, against a client that is a socket in this process:
    negotiate, answer the TTYPE question, take a ragged fixture and answer a
    DSR - which is the shape tests/telansi.py will drive from the guest."""
    stream = load_fixture("report") if os.path.isdir(FIXDIR) else b"hello"
    srv = BBSServer(port=0, fixture=stream, frag=3, fragdelay=0.0, dsr=True,
                    timeout=6.0, once=True)
    srv.start()
    time.sleep(0.2)
    c = socket.create_connection(("127.0.0.1", srv.port), 5.0)
    c.settimeout(0.5)
    got = bytearray()
    replied = False
    end = time.time() + 10.0
    # Read until the fixture has arrived or the deadline does. A loop that
    # gave up on the first idle half-second would stop between the server's
    # negotiation and its fixture and report an empty screen, which is the
    # test being wrong rather than the server.
    while time.time() < end:
        try:
            d = c.recv(4096)
        except socket.timeout:
            d = b""
        if d == b"" and not replied:
            # answer the negotiation the way the terminal will, and the DSR
            # the server will ask for once the fixture is out
            c.sendall(bytes([IAC, WILL, OPT_TTYPE])
                      + bytes([IAC, SB, OPT_TTYPE, TT_IS]) + b"ANSI"
                      + bytes([IAC, SE])
                      + bytes([IAC, WILL, OPT_NAWS])
                      + bytes([IAC, SB, OPT_NAWS, 0, 80, 0, 25, IAC, SE])
                      + bytes([IAC, DO, OPT_ECHO])
                      + bytes([IAC, DO, OPT_SGA])
                      + bytes([IAC, WILL, OPT_BINARY])
                      + bytes([IAC, DO, OPT_BINARY])
                      + b"\x1b[7;7R")
            replied = True
            continue
        got += d
        if replied and stream in TelnetIn().feed(bytes(got)):
            break
    c.close()
    srv.stop()
    log = srv.log_dict()
    ok = True
    rx = [(e["cmd"], e["opt"]) for e in log["negotiation"] if e["dir"] == "rx"]
    tx = [(e["cmd"], e["opt"]) for e in log["negotiation"] if e["dir"] == "tx"]
    for want in (("DO", "TTYPE"), ("DO", "NAWS"), ("WILL", "ECHO"),
                 ("WILL", "SGA"), ("DO", "BINARY"), ("WILL", "BINARY")):
        if want not in tx:
            print("selfcheck FAIL server.negotiation: never sent %s" % (want,))
            ok = False
    if ("WILL", "TTYPE") not in rx:
        print("selfcheck FAIL server.negotiation: never logged WILL TTYPE")
        ok = False
    terms = [e.get("terminal") for e in log["subnegotiation"]
             if e.get("dir") == "rx"]
    if "ANSI" not in terms:
        print("selfcheck FAIL server.ttype: %r" % (terms,))
        ok = False
    naws = [(e.get("cols"), e.get("rows")) for e in log["subnegotiation"]
            if e.get("cols") is not None]
    if (80, 25) not in naws:
        print("selfcheck FAIL server.naws: %r" % (naws,))
        ok = False
    if "\x1b[7;7R" not in log["answers"]:
        print("selfcheck FAIL server.dsr: answers %r" % (log["answers"],))
        ok = False
    # the fixture arrived whole, once the IAC doubling is undone
    body = TelnetIn().feed(bytes(got))
    if stream not in body:
        print("selfcheck FAIL server.fixture: %d bytes out, %d in"
              % (len(stream), len(body)))
        ok = False
    frags = [e for e in log["events"] if e.get("kind") == "fixture"]
    if ok:
        print("selfcheck ok   server: negotiated, %d fragments, TTYPE ANSI, "
              "NAWS 80x25, DSR answered"
              % (frags[0]["fragments"] if frags else 0))
    return ok


def _selfcheck_wire(verbose=False):
    """The whole thing over TCP, THROUGH the Telnet escaping.

    This is the check the others cannot make: Zmodem sends binary, binary is
    full of 0xFF, and a Telnet connection carries 0xFF as IAC IAC. A sender
    that forgets to double them and a receiver that forgets to halve them
    agree with each other over a pipe and fail on the first file with a run of
    0xFF in it - so one of the payloads here is nothing else.
    """
    import hashlib
    import shutil
    import tempfile
    tmp = tempfile.mkdtemp(prefix="os88bbs.")
    try:
        src, dst = os.path.join(tmp, "s"), os.path.join(tmp, "d")
        os.makedirs(src)
        os.makedirs(dst)
        bodies = {"FF.BIN": b"\xff" * 3000 + bytes(range(256)) * 8,
                  "TEXT.TXT": b"over the wire\r\n" * 200}
        paths = []
        for n, v in bodies.items():
            p = os.path.join(src, n)
            with open(p, "wb") as fh:
                fh.write(v)
            paths.append(p)

        srv = BBSServer(port=0, fixture=b"ready.\r\n", frag=0, fragdelay=0.0,
                        files=paths, timeout=20.0, once=True)
        srv.start()
        time.sleep(0.2)
        c = socket.create_connection(("127.0.0.1", srv.port), 5.0)
        filt = TelnetIn()
        link = socket_link(c, filt)
        # Answer the negotiation the way the terminal will, then receive.
        c.sendall(bytes([IAC, WILL, OPT_TTYPE, IAC, WILL, OPT_NAWS,
                         IAC, DO, OPT_ECHO, IAC, DO, OPT_SGA,
                         IAC, WILL, OPT_BINARY, IAC, DO, OPT_BINARY]))
        err = None
        try:
            ZmodemReceiver(link, dst).run(40.0)
        except (ZmError, Timeout, EOFError) as e:
            err = "%s: %s" % (type(e).__name__, e)
        c.close()
        srv.stop()
        ok = err is None
        if err:
            print("selfcheck FAIL zmodem.wire: %s" % err)
        for n, v in bodies.items():
            p = os.path.join(dst, n)
            have = open(p, "rb").read() if os.path.exists(p) else None
            if have != v:
                print("selfcheck FAIL zmodem.wire.%s: %s"
                      % (n, "missing" if have is None
                         else "%d of %d bytes, sha %s != %s"
                         % (len(have), len(v),
                            hashlib.sha256(have).hexdigest()[:12],
                            hashlib.sha256(v).hexdigest()[:12])))
                ok = False
        if ok:
            print("selfcheck ok   zmodem.wire: over TCP with IAC doubling, "
                  "%d files, %d bytes, %d of them 0xFF"
                  % (len(bodies), sum(len(v) for v in bodies.values()),
                     sum(v.count(b"\xff") for v in bodies.values())))
        return ok
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def _selfcheck_mangle(verbose=False):
    """The 8.3 rule against its table, and the property that makes it usable:
    the output of a mangle is itself a legal name, so mangling it again
    changes nothing. Without that, a name this machine has just listed could
    not be asked for by the name it listed."""
    ok = True
    for src, want in MANGLE83_CASES:
        got = mangle83(src)
        if got != want:
            print("selfcheck FAIL mangle83(%r) = %r, want %r"
                  % (src, got, want))
            ok = False
        elif verbose:
            print("selfcheck ok   mangle83(%r) -> %r" % (src, got))
        again = mangle83(got)
        if again != got:
            print("selfcheck FAIL mangle83 is not idempotent: %r -> %r -> %r"
                  % (src, got, again))
            ok = False
    if ok:
        print("selfcheck ok   mangle83: %d cases, and every result is a fixed "
              "point" % len(MANGLE83_CASES))
    return ok


def self_check(verbose=False):
    results = [
        _selfcheck_mangle(verbose),
        _selfcheck_crc(verbose),
        _selfcheck_telnet(verbose),
        _selfcheck_fragment(verbose),
        _selfcheck_server(verbose),
        _selfcheck_internal(verbose),
        _selfcheck_skip(verbose),
        _selfcheck_bin32(verbose),
        _selfcheck_wire(verbose),
    ]
    lz_ok, lz_ran = _selfcheck_lrzsz(verbose)
    results.append(lz_ok)
    bad = sum(0 if r else 1 for r in results)
    print("os88bbs selfcheck: %d checks, lrzsz interop %s, %d problem(s)"
          % (len(results), "RAN" if lz_ran else "SKIPPED", bad))
    return bad == 0


# =============================================================================
def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", type=int, default=8093)
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--fixture", help="a path, or a name under %s"
                    % os.path.relpath(FIXDIR, ROOT))
    ap.add_argument("--frag", type=int, default=0, metavar="SEED",
                    help="send in ragged fragments, deterministically")
    ap.add_argument("--fragdelay", type=float, default=0.02,
                    help="seconds between fragments (default 0.02)")
    ap.add_argument("--dsr", action="store_true",
                    help="ask ESC[6n after the fixture and log the reply")
    ap.add_argument("--send", nargs="*", default=[], metavar="FILE",
                    help="send these by Zmodem once the fixture is out")
    ap.add_argument("--zwait", type=float, default=0.0,
                    help="wait this long before starting Zmodem, so the "
                         "operator can open the Receive dialog first")
    ap.add_argument("--bin32", action="store_true",
                    help="send ONE ZBIN32 header, which the terminal must NAK")
    ap.add_argument("--slow", type=int, default=0, metavar="BPS",
                    help="pace the wire at this many bytes a second")
    ap.add_argument("--banner", default=None)
    ap.add_argument("--log", metavar="FILE", help="write the JSON log here")
    ap.add_argument("--timeout", type=float, default=180.0)
    ap.add_argument("--forever", action="store_true",
                    help="keep listening after the first client")
    ap.add_argument("--recv", metavar="DIR",
                    help="be a Zmodem RECEIVER on stdin/stdout into DIR, "
                         "instead of a server (for checking a sender by hand)")
    ap.add_argument("--selfcheck", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if args.selfcheck:
        return 0 if self_check(args.verbose) else 1

    if args.recv:
        link = fd_link(0, 1)
        files = ZmodemReceiver(link, args.recv).run(120.0)
        print(json.dumps(files, indent=1), file=sys.stderr)
        return 0

    banner = args.banner.encode("latin-1") if args.banner else None
    srv = BBSServer(port=args.port, host=args.host,
                    fixture=load_fixture(args.fixture) if args.fixture else b"",
                    frag=args.frag, fragdelay=args.fragdelay, dsr=args.dsr,
                    files=args.send, log_path=args.log, banner=banner,
                    timeout=args.timeout, once=not args.forever,
                    slow=args.slow, bin32=args.bin32, zwait=args.zwait,
                    verbose=args.verbose)
    print("os88bbs: listening on %s:%d%s%s"
          % (args.host, srv.port,
             ", fixture %d bytes" % len(srv.fixture) if srv.fixture else "",
             ", %d file(s) by Zmodem" % len(srv.files) if srv.files else ""),
          file=sys.stderr)
    srv.start()
    try:
        while srv.is_alive():
            srv.done.wait(0.5)
            if srv.done.is_set():
                break
    except KeyboardInterrupt:
        pass
    srv.stop()
    if args.log:
        print("os88bbs: log in %s" % args.log, file=sys.stderr)
    else:
        json.dump(srv.log_dict(), sys.stdout, indent=1, sort_keys=True)
        print()
    return 1 if srv.error else 0


if __name__ == "__main__":
    sys.exit(main())
