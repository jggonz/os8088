#!/usr/bin/env python3
"""Every package that ships LZB still SAVES A CLUSTER by it (SPEC.md 20.13.5).

    python3 tests/unit/t_lzbclust.py              # the check
    python3 tests/unit/t_lzbclust.py --candidates # ...and which LZ4 packages
                                                  #    LZB would save one for

The owner's rule for `LZBPKGS` in the Makefile is not "LZB is smaller" - it
always is, by 10-14% - but **"LZB saves this package a cluster on the 360KB
disks it rides"**, because what LZB costs is paid on every launch for ever
(+541 ms on ArtfulType's first one, measured on a 5150) and what it buys is
only real when a whole cluster comes back. A package is chosen against the
size it had THAT DAY. Its next edit can move it either side of a boundary,
and nothing else in the tree notices: the build stays green, the disk still
fits, and the package goes on paying ~4x the decode for a file that takes
exactly as many clusters as its LZ4 twin would. This row is what notices.

For every name in `LZBPKGS` it reads the SHIPPED `build/<name>.o88` and:

  * checks it really IS LZB (header flags bit 3 and bit 4). The list is
    stamped so a tree that already has an LZ4 build rebuilds - and the first
    try at this list shipped every one of them LZ4 with `make` reporting up
    to date - so a name in the list whose file is LZ4 is the stamp, or the
    rule, having come apart from the list;
  * re-packs its own `build/<name>.bin` with LZ4 exactly as `$(OS88PKG)`
    would, and FAILS unless the LZB file takes FEWER 1,024-byte clusters
    (the 360KB and 720KB geometries) than the LZ4 one. The 512-byte cluster
    figure is printed beside it and does not decide: the owner's test is the
    360KB disk.

A failure names the package and both sizes. The fix is a decision rather
than a code change: take the name out of `LZBPKGS`, point its rule back at
`$(OS88PKG)` and `rm build/<name>.o88` once (the Makefile says why) - or find
the bytes that put it back over the line.

`--candidates` is the other direction and is information, never a failure:
the shipped LZ4 packages LZB WOULD save a cluster for, less the three the
owner keeps LZ4 whatever it saves (Paint, Note Pad, the Task Manager: they
open the common documents and have to stay quick). It packs every package
LZB, so it is the slow half and runs only when asked.

SOAK, not fast or full: it re-packs a dozen packages (seconds, where `fast`
is charged to every build), and it is a question about which compressor a
package should use, not "did you obviously break the OS". It wants a plain
`make` and `make smallapps` built with packing on; `make PKGZ=` builds
everything plain and the row says so and fails rather than pass on nothing.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import harness  # noqa: E402

BUILD = os.environ.get("OS88_BUILD", os.path.join(ROOT, "build"))
F_Z, F_LZB = 0x08, 0x10                  # header flags byte, SPEC.md 20.2
KEEP_LZ4 = ("paint", "notepad", "taskmgr")  # the owner's: they stay quick


def lzbpkgs():
    """LZBPKGS out of the Makefile, continuation lines and all."""
    text = open(os.path.join(ROOT, "Makefile")).read().replace("\\\n", " ")
    m = re.search(r"^LZBPKGS\s*:=(.*)$", text, re.M)
    return m.group(1).split() if m else None


def clusters(n, size):
    return (n + size - 1) // size


def pack(binp, fmt, tmp):
    out = os.path.join(tmp, os.path.basename(binp) + "." + fmt)
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88pkg.py"),
                        binp, "-o", out, "--compress-if=" + fmt],
                       capture_output=True, text=True)
    if r.returncode or not os.path.exists(out):
        return None, (r.stderr or r.stdout).strip().splitlines()[-1:]
    return os.path.getsize(out), None


def check_list(names, tmp):
    print("%-18s %7s %7s %9s %9s" % ("package", "LZB", "LZ4", "1K cl", "512 cl"))
    for name in names:
        o88 = os.path.join(BUILD, name + ".o88")
        binp = os.path.join(BUILD, name + ".bin")
        if not harness.check(os.path.exists(o88) and os.path.exists(binp),
                             "%s: build/%s.o88 and .bin exist" % (name, name),
                             why="a name in LZBPKGS with nothing built is a list "
                                 "nobody has checked - `make` and `make smallapps`"):
            continue
        shipped = open(o88, "rb").read()
        lzb = len(shipped)
        flags = shipped[3]
        if not harness.check(flags & F_Z and flags & F_LZB,
                             "%s: the shipped file is LZB" % name,
                             why="it is in LZBPKGS, so its rule should pack with "
                                 "$(OS88PKGB) and the stamp should have rebuilt it",
                             got="flags 0x%02X (%s)" % (flags, "LZ4" if flags & F_Z
                                                         else "not compressed"),
                             want="flags bits 3 and 4 set"):
            continue
        lz4, err = pack(binp, "lz4", tmp)
        if not harness.check(lz4 is not None, "%s: re-packs with LZ4" % name,
                             got=err):
            continue
        c4, cb = clusters(lz4, 1024), clusters(lzb, 1024)
        h4, hb = clusters(lz4, 512), clusters(lzb, 512)
        print("%-18s %7d %7d %4d->%-4d %4d->%-4d%s"
              % (name, lzb, lz4, c4, cb, h4, hb, "" if cb < c4 else "   <- NO CLUSTER"))
        harness.check(cb < c4,
                      "%s: LZB saves a 1KB cluster over LZ4" % name,
                      why="LZB costs ~4x the decode on every launch (SPEC.md "
                          "20.13.5), and this package's file now takes as many "
                          "clusters either way - take it out of LZBPKGS, or win "
                          "the bytes back",
                      got="LZB %d bytes = %d clusters, LZ4 %d bytes = %d" % (lzb, cb, lz4, c4),
                      want="LZB in fewer clusters than LZ4")


def candidates(listed, tmp):
    """Shipped LZ4 packages LZB would save a 1KB cluster for. Information."""
    print("\ncandidates - shipped LZ4 packages LZB would save a cluster for:")
    seen = 0
    for d in ("", "smallapp"):
        base = os.path.join(BUILD, d)
        if not os.path.isdir(base):
            continue
        for f in sorted(os.listdir(base)):
            if not f.endswith(".o88"):
                continue
            name = os.path.join(d, f[:-4]) if d else f[:-4]
            binp = os.path.join(base, f[:-4] + ".bin")
            if name in listed or not os.path.exists(binp):
                continue
            data = open(os.path.join(base, f), "rb").read()
            if not (data[3] & F_Z) or data[3] & F_LZB:
                continue
            lzb, _ = pack(binp, "lzb", tmp)
            if lzb is None:
                continue
            c4, cb = clusters(len(data), 1024), clusters(lzb, 1024)
            if cb < c4:
                seen += 1
                note = "  (kept LZ4 by the owner)" if os.path.basename(name) in KEEP_LZ4 else ""
                print("  %-18s LZ4 %6d = %3d cl, LZB %6d = %3d cl%s"
                      % (name, len(data), c4, lzb, cb, note))
    if not seen:
        print("  none")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--candidates", action="store_true",
                    help="also list the LZ4 packages LZB would save a cluster for")
    a = ap.parse_args()
    names = lzbpkgs()
    if not harness.check(names, "the Makefile defines LZBPKGS",
                         why="this row reads the list out of the Makefile"):
        harness.done("t_lzbclust")
    with tempfile.TemporaryDirectory() as tmp:
        check_list(names, tmp)
        if a.candidates:
            candidates(set(names), tmp)
    harness.done("t_lzbclust")


if __name__ == "__main__":
    main()
