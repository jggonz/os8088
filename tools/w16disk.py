#!/usr/bin/env python3
"""w16disk - a floppy of YOUR Windows programs for WIN16 (SPEC.md 124.18.1).

    python3 tools/w16disk.py --dir <WIN3X> --size 1440 -o build/w16prog.img

`make win16disk WIN3X=<dir>` runs this once per geometry. The disk carries
WIN16\\ (WIN16.O88, WIN16.TXT, COPYING.W16), SYSTEM\\APPDATA\\WIN16 - the
Windows directory a program's profiles are kept in (124.14) - and, at the
root, the Windows programs found DIRECTLY in <dir>, in name order, as many as
the geometry holds. PROGRAMS.TXT, beside them, says which went on and why
each other one did not.

The verdicts are the loader's own refusals (apps/win16/w16load.inc), taken
before a cluster is spent, so a program the machine would refuse is never
put on a disk it would be refused from:

    a DOS program      no NE header
    a library          NEF_LIB / NEF_SELF (WIN16 runs programs, R4)
    protected mode     NEF_PMODE (Windows 3.1's /S-only, R3)
    big tables         its NE tables and relocations past 64KB (124.20.1)
    not 8.3            os8088's FAT names are 8.3
    no room            it did not fit after the ones before it

"No room" is decided by building: each program is tried in name order with
everything already chosen, and a refusal from tools/os88disk.py means it did
not fit. A later, smaller one may still fit, and does. Microsoft's files are
read from <dir> and never enter the tree; the image goes in build/.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

NEF_PMODE, NEF_SELF, NEF_LIB = 0x0008, 0x0800, 0x8000
MAXTABLES = 0xFFF0              # w16_copysegs' bound (SPEC.md 124.20.1)
EXTS = (".EXE",)


def verdict(path):
    """None if WIN16 would load it, else the reason it would not."""
    with open(path, "rb") as f:
        d = f.read()
    if len(d) < 0x40 or d[:2] != b"MZ" or struct.unpack_from("<H", d, 0x18)[0] < 0x40:
        return "a DOS program, not a Windows one"
    ne = struct.unpack_from("<I", d, 0x3C)[0]
    if ne + 0x40 > len(d) or d[ne:ne + 2] != b"NE":
        return "a DOS program, not a Windows one"
    flags = struct.unpack_from("<H", d, ne + 0x0C)[0]
    if flags & (NEF_LIB | NEF_SELF):
        return "a library or a self-loading program: not yet"
    if flags & NEF_PMODE:
        return "it needs protected mode"
    if tables(d, ne) > MAXTABLES:
        return "its tables are larger than WIN16 can read"
    return None


def tables(d, ne):
    """the bytes w16_load keeps while it loads (SPEC.md 124.20.1): the NE
    tables, then every segment's relocation records, word-aligned between"""
    u = lambda o: struct.unpack_from("<H", d, ne + o)[0]
    end = max(u(0x22) + 8 * u(0x1C), u(0x28) + 2 * u(0x1E), u(0x04) + u(0x06),
              u(0x24), u(0x26), u(0x2A) + 1, 0x40)
    n = end + (end & 1)
    align = u(0x32) or 9
    for i in range(u(0x1C)):
        sec, ln, fl, _ = struct.unpack_from("<4H", d, ne + u(0x22) + 8 * i)
        r = (sec << align) + (ln or 0x10000)
        if sec and fl & 0x100 and r + 2 <= len(d):
            n += 2 + 8 * struct.unpack_from("<H", d, r)[0]
    return n


def is83(name):
    stem, _, ext = name.partition(".")
    ok = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-$~!#%&@^")
    return (1 <= len(stem) <= 8 and len(ext) <= 3 and "." not in ext
            and set(stem.upper()) <= ok and set(ext.upper()) <= ok)


def listing(size, chosen, refused):
    t = ["Windows programs on this %dKB disk, for WIN16." % size, "",
         "Double-click one to run it. WIN16 is in the WIN16 folder;",
         "a program's settings go in SYSTEM\\APPDATA\\WIN16.", ""]
    t += ["ON THIS DISK"] + (["  " + n for n in chosen] or ["  none"]) + [""]
    if refused:
        t += ["LEFT OFF"] + ["  %-12s %s" % (n, why) for n, why in refused] + [""]
    return "\r\n".join(t).encode("ascii")


def build(out, size, win16, programs, txt, quiet=True):
    cmd = [sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
           "-o", out, "--size", str(size)]
    cmd += ["WIN16:" + p for p in win16] + list(programs) + [txt]
    cmd += ["--folder", "SYSTEM/APPDATA", "--folder", "SYSTEM/APPDATA/WIN16"]
    r = subprocess.run(cmd, capture_output=quiet, text=True)
    return r.returncode == 0, (r.stderr or "") + (r.stdout or "")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dir", required=True, help="where the Windows programs are")
    ap.add_argument("--size", type=int, required=True, choices=(1440, 1200, 720, 360))
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--win16", nargs=3, required=True,
                    metavar=("WIN16.O88", "WIN16.TXT", "COPYING.W16"))
    a = ap.parse_args()

    if not os.path.isdir(a.dir):
        sys.exit("w16disk: %s is not a directory" % a.dir)
    names = sorted(f for f in os.listdir(a.dir)
                   if f.upper().endswith(EXTS) and os.path.isfile(os.path.join(a.dir, f)))
    cand, refused = [], []
    for n in names:
        why = None if is83(n) else "not an 8.3 name"
        why = why or verdict(os.path.join(a.dir, n))
        if why:
            refused.append((n.upper(), why))
        else:
            cand.append(n)

    with tempfile.TemporaryDirectory() as td:
        txt = os.path.join(td, "PROGRAMS.TXT")
        trial = os.path.join(td, "trial.img")
        # the listing at its LONGEST, every candidate named under LEFT OFF,
        # so the one written at the end is never a cluster bigger than this
        with open(txt, "wb") as f:
            f.write(listing(a.size, [n.upper() for n in cand],
                            refused + [(n.upper(), "no room on this disk") for n in cand]))
        ok, err = build(trial, a.size, a.win16, [], txt)
        if not ok:
            sys.exit("w16disk: WIN16 alone does not fit %dKB:\n%s" % (a.size, err))
        chosen = []
        for n in cand:
            p = os.path.join(a.dir, n)
            ok, _ = build(trial, a.size, a.win16, chosen + [p], txt)
            if ok:
                chosen.append(p)
            else:
                refused.append((n.upper(), "no room on this disk"))
        refused.sort()
        with open(txt, "wb") as f:
            f.write(listing(a.size, [os.path.basename(p).upper() for p in chosen], refused))
        ok, err = build(a.output, a.size, a.win16, chosen, txt, quiet=False)
        if not ok:
            sys.exit("w16disk: the final build failed:\n%s" % err)
    print("w16disk: %s - %d program(s) on, %d left off (PROGRAMS.TXT says why)"
          % (a.output, len(chosen), len(refused)))


if __name__ == "__main__":
    main()
