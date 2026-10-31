#!/usr/bin/env python3
"""os88nettree - the tree INSTALL installs from over the network (SPEC.md 117.10.2).

    python3 tools/os88nettree.py SYSTEM.img APPS.img [MORE.img ...] OS88VER.TXT OUT

One tree out of the disks: the system disk's files and each other disk's,
their folders merged into the system disk's in the order given, with
OS88VER.TXT in
SYSTEM/ - where an install copies it, so an installed machine keeps the record
of its release - and the OS88.IDX tools/os88httpdrv.py writes for any
published tree. EDIT NOTHING IN OUT BY HAND: OS88.IDX carries every file's
size and the drive believes it, so a file edited in place is served cut to its
old length. Rebuild instead, or run `os88httpdrv.py export OUT OUT`.
A file two disks carry is the FIRST one's, and said; ASSOC.DAT is left out,
because an install builds the destination volume's own (SPEC.md 52.10.14) and
neither disk's cache describes the merged tree.

One tree per kernel: `make nettree` writes build/nettree/big/ (os8088.img +
apps.img + win16.img), build/nettree/mega/ (mega.img + megaapps.img: WIN16
refuses under kern_mega, SPEC.md 124.1) and build/nettree/emu/ (emu.img +
apps.img + win16.img), and the release publishes
build/nettree/ under http://os8088.com/net/ (SPEC.md 117.10.2's store) - so
each lands at net/big/, net/mega/ or net/emu/, the IN_URL of that kernel's
own INSTALL.
"""
import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88httpdrv as H                                       # noqa: E402

DROP = {"ASSOC.DAT"}
VER_NAME = "OS88VER.TXT"


def merge(dst, src, where, dupes):
    for n, b in src.items():
        if where == "" and n in DROP:
            continue
        if n not in dst:
            dst[n] = b
        elif isinstance(dst[n], dict) and isinstance(b, dict):
            merge(dst[n], b, where + n + "/", dupes)
        elif dst[n] != b:
            dupes.append(where + n)


def check_version(text):
    lines = text.replace(b"\r", b"").split(b"\n")
    if not re.fullmatch(rb"\d{8}", lines[0].strip()):
        raise ValueError("%s: line 1 must be the release as YYYYMMDD, not %r"
                         % (VER_NAME, lines[0][:20]))
    news = [l for l in lines[1:] if l.strip()]
    if len(news) > 4:
        H.warn("%s has %d lines of news; INSTALL shows the first 4"
               % (VER_NAME, len(news)))
    for i, l in enumerate(lines[1:5], 2):
        if len(l) > 40:
            H.warn("%s line %d is %d characters; INSTALL shows 40"
                   % (VER_NAME, i, len(l)))
    return lines[0].strip().decode()


def build(sysimg, others, verfile, out):
    sattr = {}
    tree = H.tree_from_img(sysimg, sattr)
    tree.pop("ASSOC.DAT", None)
    attrs = {}
    for img in others:
        aattr = {}
        more = H.tree_from_img(img, aattr)
        dupes = []
        merge(tree, more, "", dupes)
        for d in dupes:
            print("os88nettree: %s is on %s too with different bytes - the "
                  "first disk's is published" % (d, os.path.basename(img)))
        for k, v in aattr.items():
            attrs.setdefault(k, v)
    attrs.update(sattr)
    ver = open(verfile, "rb").read()
    rel = check_version(ver)
    tree.setdefault("SYSTEM", {})[VER_NAME] = ver
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    H.write_tree(tree, out)
    idx = H.index_build(tree, 0, attrs)
    with open(os.path.join(out, H.IDX_NAME), "wb") as f:
        f.write(idx)
    H.check(out)
    n = len(H.index_parse(idx))
    print("os88nettree: release %s -> %s: %d records, %s %d bytes"
          % (rel, out, n, H.IDX_NAME, len(idx)))


if __name__ == "__main__":
    if len(sys.argv) < 5:
        sys.exit(__doc__.strip().split("\n\n")[1])
    try:
        build(sys.argv[1], sys.argv[2:-2], sys.argv[-2], sys.argv[-1])
    except ValueError as e:
        sys.exit("os88nettree: %s" % e)
