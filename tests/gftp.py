#!/usr/bin/env python3
"""ftplib on the GUEST's clock: every timeout a QEMU FTP row hands ftplib
is read as GUEST seconds rather than host ones.

WHY.  `ftplib.FTP.connect(host, port, timeout=30)` puts a 30 HOST-second
timeout on every socket call the session makes - the 220, each reply, each
data block. That is a statement about the host, and the thing on the other
end is a guest the host may starve: tests/ftpd.py passed alone in 287 s and
failed in a full soak (load 4-7 on four cores) with

    Setup: field 0 = '127.0.0.1'
      ... ftplib getmultiline -> socket readinto
    OSError: cannot read from timed out object

- the machine was still working, the host's 30 seconds ran out first.
docs/plans/SOAK-PARALLEL.md 17.5 is the general case and the reason a
bigger number is not the fix: a host second buys a starved guest less of
the machine at every load, so any host budget is a load at which the row
fails.

WHAT.  `install(m)` swaps the `socket` module ftplib calls for a shim whose
create_connection/create_server hand back a GuestSocket. A GuestSocket is
the real socket waited on in short host SLICES (POLL), and after each empty
slice it reads the BIOS tick count (os88qemu.Clock): the call times out only
once the GUEST has run `timeout` seconds with nothing arriving, and a guest
whose clock has STOPPED raises os88qemu.GuestStopped naming the call, in
os88qemu.STALL host seconds, rather than reading as a slow server. Without
-icount QEMU's PIT follows the host, so on an idle box this is the budget
it replaces; a host that starves the guest loses IRQ0s with it (an edge the
CPU has not yet taken merges into the one pending) and the budget stretches
by exactly the work the guest was denied.

Every budget is PER CALL and restarts on progress: `recv` waits for the
next bytes, `sendall` for the next send to go, so a long transfer is never
charged as one wait. A slice that times out has moved no data - Python's
timeout is all-or-nothing per call - so the retry is exact.

The session's `self.timeout` is untouched, so `connect(..., timeout=30)` in
a row means 30 GUEST seconds after `install`, and a row is converted by the
one call. tests/ftpd.py is the first; ftpkeep and ftpdpix still run on the
host's clock.
"""
import errno
import ftplib
import select
import socket
import time

import os88qemu

POLL = 0.25             # HOST seconds per slice - how often the clock is read,
                        # never a budget
_M = [None]             # the guest whose clock budgets every call


def _budget(timeout):
    if timeout is None or timeout is socket._GLOBAL_DEFAULT_TIMEOUT:
        return None                     # blocking: no budget, stall still ends it
    return float(timeout)


class _Waiter(object):
    """One call's budget: `secs` guest seconds, a stalled clock raises."""

    def __init__(self, secs, what):
        self.c = os88qemu.Clock(_M[0])
        self.lim = None if secs is None else os88qemu.to_ticks(secs)
        self.secs, self.what = secs, what
        self.t0 = time.monotonic()

    def spent(self):
        """Raise if the budget is gone; called after an EMPTY slice."""
        n = self.c.step()
        if self.lim is not None and n >= self.lim:
            raise socket.timeout("%s: nothing in %.0f GUEST seconds (%.0f "
                                 "host)" % (self.what, self.secs,
                                            time.monotonic() - self.t0))
        if self.c.stalled():
            raise os88qemu.GuestStopped(
                "the guest's clock stopped (%s at 0040:006C for %ds) during "
                "%s" % (hex(self.c.last), os88qemu.STALL, self.what))


class GuestSocket(object):
    """A connected or listening TCP socket whose waits are guest-budgeted.
    Anything not defined here is the real socket's (setsockopt, getsockname,
    family, shutdown...)."""

    def __init__(self, sock, timeout):
        self._s = sock
        self._secs = _budget(timeout)
        self._io_refs = 0
        self._closed = False
        sock.settimeout(POLL)

    def __getattr__(self, name):
        return getattr(self._s, name)

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()

    def settimeout(self, t):            # ftplib's own, in GUEST seconds
        self._secs = _budget(t)

    def gettimeout(self):
        return self._secs

    def _loop(self, op, what):
        w = _Waiter(self._secs, what)
        while True:
            try:
                return op()
            except socket.timeout:
                w.spent()

    def recv(self, n, flags=0):
        return self._loop(lambda: self._s.recv(n, flags), "recv")

    def recv_into(self, buf, n=0, flags=0):
        return self._loop(lambda: self._s.recv_into(buf, n, flags),
                          "a reply/read")

    def send(self, data, flags=0):
        return self._loop(lambda: self._s.send(data, flags), "send")

    def sendall(self, data, flags=0):
        v = memoryview(data).cast("B")
        while len(v):
            v = v[self.send(v, flags):]  # each send restarts the budget

    def accept(self):
        c, a = self._loop(self._s.accept, "the server's active-mode connect")
        return GuestSocket(c, self._secs), a

    def makefile(self, mode="r", buffering=None, *, encoding=None,
                 errors=None, newline=None):
        # socket.socket's own, run on THIS object: its SocketIO calls our
        # recv_into/send, so a line read waits on the guest's clock too
        return socket.socket.makefile(self, mode, buffering, encoding=encoding,
                                      errors=errors, newline=newline)

    def _decref_socketios(self):
        if self._io_refs > 0:
            self._io_refs -= 1
        if self._closed:
            self.close()

    def close(self):
        self._closed = True
        if self._io_refs <= 0:
            self._s.close()


def create_connection(address, timeout=socket._GLOBAL_DEFAULT_TIMEOUT,
                      source_address=None, **kw):
    """A non-blocking connect watched on the guest's clock - a timed-out
    blocking connect leaves the socket unusable, so it is never sliced."""
    host, port = address
    last = None
    for fam, typ, proto, _, sa in socket.getaddrinfo(host, port, 0,
                                                     socket.SOCK_STREAM):
        s = socket.socket(fam, typ, proto)
        try:
            if source_address:
                s.bind(source_address)
            s.setblocking(False)
            err = s.connect_ex(sa)
            if err not in (0, errno.EINPROGRESS, errno.EWOULDBLOCK):
                raise OSError(err, "connect: %s" % errno.errorcode.get(err))
            w = _Waiter(_budget(timeout), "connect to %s:%d" % (host, port))
            while err:
                if select.select([], [s], [], POLL)[1]:
                    err = s.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
                    if err:
                        raise OSError(err, "connect: %s"
                                      % errno.errorcode.get(err, err))
                    break
                w.spent()
            return GuestSocket(s, timeout)
        except Exception as e:
            s.close()
            last = e
    raise last or OSError("getaddrinfo returned nothing for %r" % (address,))


def create_server(address, *, family=socket.AF_INET, backlog=None, **kw):
    return GuestSocket(socket.create_server(address, family=family,
                                            backlog=backlog, **kw), None)


class _Shim(object):
    """The `socket` module as ftplib sees it after `install`."""
    create_connection = staticmethod(create_connection)
    create_server = staticmethod(create_server)

    def __getattr__(self, name):
        return getattr(socket, name)


def install(m):
    """Budget every ftplib socket call in this process on `m`'s clock - any
    object with read(linear, n), e.g. os88qemu._Sock(os88qemu.SOCK)."""
    _M[0] = m
    ftplib.socket = _Shim()
