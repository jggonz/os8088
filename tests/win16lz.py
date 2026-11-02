#!/usr/bin/env python3
"""win16lz - LZEXPAND and VER (SPEC.md 124.24.8, 124.24.9,
docs/plans/WIN16-ANY-PLAN.md S19).

    python3 tests/win16lz.py            (row win16lz)
    python3 tests/win16lz.py --host     the packer's own check, no machine
    python3 tests/win16lz.py --keep     leave the QEMU for a look

*As an installer, a Windows 3.1 setup program expands its packed files and
puts them where VER says they go.*

The host packs three SZDD files the way COMPRESS.EXE does (szdd_pack below;
tools/w16font.py's szdd() is the reference expansion, and the packed bytes
are checked against it before the machine is booted):

  SAMPLE.TX_   24,000 bytes of text, numbered lines and runs - more than
               the DOS's 8 KB window, matches far back and near;
  SECOND.DA_   bytes from a fixed seed, which mostly do not compress;
  LZT.EX_      LZT.EXE itself, so its version is read through LZEXPAND.

LZT.EXE (tests/win16/lzt.c, `make win16fixtures`) reads, seeks, copies and
names them, reads its version, and installs SECOND.DAT into WIN16's system
directory. Its MessageBox, read off the program's stack, must be "LZT OK"
and SAMPLE.TXT's length. When it has ended, SAMPLE.TXT at B:'s root and
SYSTEM\\APPDATA\\WIN16\\SECOND.DAT are read off the image and must be the
host's expansions, byte for byte.
"""
import argparse
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)

NAME = "LZT.EXE"


def szdd_pack(data, last):
    """DATA as COMPRESS.EXE's SZDD: the header, then LZSS over a 4 KB window
    of spaces first written at FF0h. LAST is the name's letter the '_'
    replaces. Greedy, 3 to 18 bytes a match, at most 4,095 back"""
    out = bytearray(b"SZDD\x88\xf0\x27\x33A" + last.encode("latin-1")
                    + struct.pack("<I", len(data)))
    heads = {}
    i, n = 0, len(data)
    while i < n:
        ctl, items, at = 0, bytearray(), len(out)
        for bit in range(8):
            if i >= n:
                break
            best, bj = 0, 0
            for j in reversed(heads.get(data[i:i + 3], [])[-64:]):
                if i - j > 4095:
                    break
                k = 0
                while k < 18 and i + k < n and data[j + k] == data[i + k]:
                    k += 1
                if k > best:
                    best, bj = k, j
                    if k == 18:
                        break
            if best >= 3:
                pos = (bj + 0xFF0) & 0xFFF
                items += bytes((pos & 0xFF, ((pos >> 4) & 0xF0) | (best - 3)))
                step = best
            else:
                ctl |= 1 << bit
                items.append(data[i])
                step = 1
            for k in range(step):
                if i + k + 3 <= n:
                    heads.setdefault(data[i + k:i + k + 3], []).append(i + k)
            i += step
        out.append(ctl)
        out += items
        assert len(out) > at
    return bytes(out)


def sample():
    lines = []
    for k in range(1, 800):
        lines.append("Line %04d of the sample: the quick brown fox %s\r\n"
                     % (k, "." * (k % 23)))
    return "".join(lines).encode("latin-1")[:24000]


def second():
    r = random.Random(8088)
    return bytes(r.randrange(256) for _ in range(6000)) + b"tail" * 50


def files(work):
    """the three packed files in WORK, and what each must expand to"""
    import w16font
    exe = open(os.path.join(ROOT, "build", "win16", NAME), "rb").read()
    want = {"SAMPLE.TX_": (sample(), "T"), "SECOND.DA_": (second(), "T"),
            "LZT.EX_": (exe, "E")}
    out = []
    for fn, (data, last) in want.items():
        packed = szdd_pack(data, last)
        if w16font.szdd(packed) != data:
            raise SystemExit("win16lz: the packer and tools/w16font.py "
                             "disagree on %s" % fn)
        p = os.path.join(work, fn)
        open(p, "wb").write(packed)
        out.append(p)
        print("  %s: %d bytes packed to %d" % (fn, len(data), len(packed)))
    return out


def host():
    work = tempfile.mkdtemp(prefix="w16z-")
    try:
        files(work)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("win16lz --host: the packer's files expand to their bytes")
    return 0


def mread(img, path):
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    r = subprocess.run(["mtype", "-i", img, "::" + path],
                       capture_output=True, env=env)
    return r.stdout if r.returncode == 0 else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--host", action="store_true")
    a = ap.parse_args()
    from win16mines import QDrive, b
    import win16fpu as F
    if not os.path.exists(b(os.path.join("win16", NAME))):
        print("win16lz: SKIP - no build/win16/%s (`make win16fixtures`)" % NAME)
        return 77
    if a.host:
        return host()
    import os88ui
    import xmcheck
    bad = []
    work = tempfile.mkdtemp(prefix="w16z-")     # short: a QMP socket path
    try:
        extra = files(work)
        bimg = os.path.join(work, "src.img")
        F.bdisk(bimg, 1440, [NAME], extra)
        d = QDrive(work, bimg, a.keep)
        d.key = lambda n: d.hmp("sendkey %s" % {"Enter": "ret"}.get(n, n))
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        S = F.pkg_syms()
        dw = F.open_b(d, ui, xmcheck.sym)
        F.run_prog(d, ui, xmcheck.sym, S, dw, NAME, bad, "QEMU",
                   want={NAME: "LZT OK %d" % len(sample())})
        d.idle(4.0)
        d.hmp("stop")                   # the image, as the guest left it
        for path, data in (("SAMPLE.TXT", sample()),
                           ("SYSTEM/APPDATA/WIN16/SECOND.DAT", second())):
            got = mread(d.p.b, path)
            if got is None:
                bad.append("%s is not on B:" % path)
            elif got != data:
                k = next((i for i in range(min(len(got), len(data)))
                          if got[i] != data[i]), min(len(got), len(data)))
                bad.append("%s: %d bytes, not the host's %d; first differs at %d"
                           % (path, len(got), len(data), k))
            else:
                print("%s: the host's %d bytes" % (path, len(data)))
        if not a.keep:
            d.p.close()
    finally:
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print("kept: %s (QMP %s)" % (work, os.path.join(work, "qmp.sock")))
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        print("win16lz: LZT's checks passed, and both files are the host's")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
