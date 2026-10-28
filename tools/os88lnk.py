#!/usr/bin/env python3
"""os88lnk - write or read an os8088 LINK file, *.LNK (SPEC.md 26.10.1).

    python3 tools/os88lnk.py OUT.LNK SYSTEM\\TASKMGR.O88            # this disk
    python3 tools/os88lnk.py OUT.LNK APPS\\CALC.O88 --vol B          # B:, absolute
    python3 tools/os88lnk.py OUT.LNK MEDIA\\README.TXT --kind document
    python3 tools/os88lnk.py OUT.LNK INSTALL.O88 --arg CHECK        # a parameter
    python3 tools/os88lnk.py --read SOME.LNK
    python3 tools/os88lnk.py --selfcheck

A link is 50 bytes: 'LK', a version byte, the volume (an index, 0 = A:, or
0xFF = the volume the LINK FILE is on), the kind (0 document, 1 package, 2
folder) and the path below the root, NUL-terminated in 45 bytes - and, for
a package, an optional PARAMETER after that NUL in the same 45 bytes, which
the program reads with OSAPI_ARG_TEXT (SPEC.md 26.10.7). The kernel
follows one exactly as a double-click on what it names - a package runs, a
document goes to its association, a folder gets a Disk window - and a link in
the boot volume's \\AUTOSTRT is followed at every boot (SPEC.md 26.10.5).

This is the host's writer, for a disk built with one already on it - the
autostart gate's (`make autostarttest`) and anybody's own. The kernel's own
writer is CTRL.DRV's Alt-drop (SPEC.md 26.10.4) and a package's is
apps/os88link.inc; all three write the same bytes, and `--read` is how to
check a link a machine wrote.
"""
import argparse
import sys

SIZE = 50
PATHMAX = 45
VER = 1
SELF = 0xFF
KINDS = {"document": 0, "package": 1, "folder": 2}


def make(path, vol=SELF, kind=None, arg=None):
    """The 50 bytes of a link to `path` (below the root, either slash), with
    `arg` after the path's NUL for a package (SPEC.md 26.10.7)."""
    p = path.replace("/", "\\").lstrip("\\").upper()
    if kind is None:
        kind = KINDS["package"] if p.endswith(".O88") else KINDS["document"]
    raw = p.encode("ascii")
    if len(raw) > PATHMAX - 1:
        raise ValueError("%s: %d characters, and a link holds %d"
                         % (p, len(raw), PATHMAX - 1))
    for comp in p.split("\\"):
        stem, _, ext = comp.partition(".")
        if not stem or len(stem) > 8 or len(ext) > 3:
            raise ValueError("%s: %r is not an 8.3 name" % (p, comp))
    if not (vol == SELF or 0 <= vol < 8):
        raise ValueError("volume %r: an index 0..7, or 0xFF for the link's own"
                         % vol)
    rec = b"LK" + bytes([VER, vol, kind]) + raw
    if arg:
        if kind != KINDS["package"]:
            raise ValueError("a parameter goes to a PROGRAM, and %s is not "
                             "one (SPEC.md 26.10.7)" % p)
        a = arg.encode("ascii")
        if 0 in a or len(raw) + 1 + len(a) > PATHMAX - 1:
            raise ValueError("%s + %r: the path, its NUL and the parameter "
                             "share %d characters" % (p, arg, PATHMAX - 1))
        rec += b"\0" + a
    return rec + bytes(SIZE - len(rec))


def param(data):
    """The parameter a link carries after its path, or None (26.10.7)."""
    field = data[5:5 + PATHMAX - 1]
    rest = field.split(b"\0", 1)[1] if b"\0" in field else b""
    rest = rest.split(b"\0")[0]
    return rest.decode("latin-1") if rest and data[4] == 1 else None


def parse(data):
    """(volume, kind, path) of a link's bytes, or ValueError saying why the
    kernel would not follow it."""
    if len(data) != SIZE:
        raise ValueError("%d bytes, and a link is %d" % (len(data), SIZE))
    if data[:2] != b"LK":
        raise ValueError("no 'LK' signature")
    if data[2] != VER:
        raise ValueError("version %d, and this reads %d" % (data[2], VER))
    path = data[5:5 + PATHMAX].split(b"\0")[0].decode("latin-1")
    return data[3], data[4], path


def _vol(s):
    if s.lower() == "self":
        return SELF
    if len(s) == 1 and s.isalpha():
        return ord(s.upper()) - ord("A")
    return int(s, 0)


def selfcheck():
    r = make("SYSTEM\\RLAPPLY.O88")
    assert len(r) == SIZE and parse(r) == (SELF, 1, "SYSTEM\\RLAPPLY.O88")
    assert parse(make("apps/calc.o88", vol=1)) == (1, 1, "APPS\\CALC.O88")
    assert parse(make("MEDIA\\X.TXT"))[1] == 0
    c = make("INSTALL.O88", arg="CHECK")
    assert parse(c) == (SELF, 1, "INSTALL.O88") and param(c) == "CHECK"
    assert param(make("INSTALL.O88")) is None
    for bad in (dict(path="MEDIA\\X.TXT", arg="Y"),
                dict(path="A\\" + "B" * 8 + ".O88", arg="Z" * 40)):
        try:
            make(bad["path"], arg=bad["arg"])
        except ValueError:
            continue
        raise AssertionError("accepted %r" % bad)
    for bad in ("TOOLONGNAME.O88", "A\\" + "B" * 50):
        try:
            make(bad)
        except ValueError:
            continue
        raise AssertionError("accepted %r" % bad)
    try:
        parse(r[:-1])
    except ValueError:
        pass
    else:
        raise AssertionError("a short link was read")
    print("os88lnk: selfcheck ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("out", nargs="?", help="the .LNK to write")
    ap.add_argument("target", nargs="?", help="the path it names, below the "
                    "root (SYSTEM\\TASKMGR.O88)")
    ap.add_argument("--vol", default="self", help="A, B, ... or an index, or "
                    "'self' (the default): the volume the link file is on")
    ap.add_argument("--kind", choices=sorted(KINDS), help="default: package "
                    "for *.O88, else document")
    ap.add_argument("--arg", help="a parameter for the program it starts "
                    "(OSAPI_ARG_TEXT, SPEC.md 26.10.7)")
    ap.add_argument("--read", metavar="LNK", help="decode a link and exit")
    ap.add_argument("--selfcheck", action="store_true")
    a = ap.parse_args()
    if a.selfcheck:
        return selfcheck()
    if a.read:
        data = open(a.read, "rb").read()
        vol, kind, path = parse(data)
        where = "its own volume" if vol == SELF else "%s:" % chr(65 + vol)
        arg = param(data)
        print("%s: %s, %s on %s%s" % (a.read, path,
              {v: k for k, v in KINDS.items()}.get(kind, "kind %d" % kind),
              where, "" if arg is None else ", parameter %r" % arg))
        return
    if not (a.out and a.target):
        ap.error("OUT.LNK and a target, or --read")
    kind = KINDS[a.kind] if a.kind else None
    with open(a.out, "wb") as f:
        f.write(make(a.target, _vol(a.vol), kind, a.arg))


if __name__ == "__main__":
    try:
        main()
    except ValueError as e:
        sys.exit("os88lnk: %s" % e)
