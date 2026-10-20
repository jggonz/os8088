#!/usr/bin/env python3
# =============================================================================
# os8088 - tools/getapple2rom.py
#
# Fetch the Apple II+ ROM images the APPLE2 package is built against and
# assemble APPLE2.ROM, the PART the packer appends to APPLE2.O88
# (docs/APPLE2-SPEC.md, on C64-SPEC §1.4's mechanism).
#
#     python3 tools/getapple2rom.py                  -> build/apple2-rom/APPLE2.ROM
#     python3 tools/getapple2rom.py -o <path>        -> somewhere else
#     python3 tools/getapple2rom.py --check          -> verify the cache, fetch
#                                                       nothing, write nothing
#     python3 tools/getapple2rom.py --from <dir>     -> take the three files
#                                                       from a local AppleWin
#                                                       checkout at the pin
#
# NOTHING THIS SCRIPT DOWNLOADS IS COMMITTED (CONTRIBUTING.md 6). The Apple
# II+ ROMs are Copyright (C) Apple Computer, Inc.; the copies used are the
# ones the AppleWin emulator (GPL-2) carries in its resource/ directory, taken
# at ONE pinned commit and checked by SHA-256 on the way in, exactly as
# tools/getruncpm.py pins RunCPM's master disk and tools/getstories.py the
# Frotz stories. The bytes land in build/, which is ignored outright, and the
# package is built from there. A mismatch is a hard failure and never a
# warning: a ROM that is not the one the port was written against is a
# machine that boots to something else.
#
# THE LAYOUT IS FIXED AND THE PACKAGE DEPENDS ON IT:
#
#     0x0000  12288  ROM      Apple2_Plus.rom   $D000-$FFFF: Applesoft BASIC
#                                               ($D000-$F7FF) and the Autostart
#                                               Monitor ($F800-$FFFF); its RESET
#                                               vector at $FFFC reads $FA62
#     0x3000   2048  CHARGEN  Apple2_Video.rom  the II+ character generator,
#                                               64 glyphs x 8 rows, 7 bits wide
#     0x3800    256  DISK2    DISK2.rom         the Disk II P5 boot ROM, mapped
#                                               at $C600 when a controller is
#                                               in slot 6
#     0x3900    256  (zero)   pad
#     0x3A00                                    = 14,848 bytes = 29 sectors,
#                                               512-aligned by construction so
#                                               the part lands at a 512-aligned
#                                               offset with no scratch (SPEC.md
#                                               2.1.1)
#
# It is deterministic - the same three inputs give the same 14,848 bytes on
# every host - so a released APPLE2.O88 rebuilds byte for byte.
# =============================================================================
import argparse
import hashlib
import os
import sys
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# THE PIN: AppleWin at the commit the port was written against (2026-07-27,
# "Replace BOOL (almost all) with bool (PR #1509)"). The ROM images have not
# changed in years, but a pin is a pin.
COMMIT = "3e8054b4627624398e4589f7f27b3d40a6b9718e"
RAW = "https://raw.githubusercontent.com/AppleWin/AppleWin/" + COMMIT + "/"
TIMEOUT = 60

# (repository path, offset in APPLE2.ROM, length, sha-256)
PARTS = [
    ("resource/Apple2_Plus.rom", 0x0000, 12288,
     "fc3e9d41e9428534a883df5aa10eb55b73ea53d2fcbb3ee4f39bed1b07a82905"),
    ("resource/Apple2_Video.rom", 0x3000, 2048,
     "08f5d22230481019844492dde0a29a018cb193712a9e4a43770a3870608f28de"),
    ("resource/DISK2.rom", 0x3800, 256,
     "de1e3e035878bab43d0af8fe38f5839c527e9548647036598ee6fe7ec74d2a7d"),
]

ROMSIZE = 0x3A00            # 14,848 = 29 x 512
RESET_VECTOR = 0xFA62       # the Autostart Monitor's RESET, at $FFFC


def die(msg):
    sys.stderr.write("getapple2rom: %s\n" % msg)
    raise SystemExit(1)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def fetch(path):
    url = RAW + path
    req = urllib.request.Request(url, headers={"User-Agent": "os8088-getapple2rom/1"})
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT) as fh:
            return fh.read()
    except (urllib.error.URLError, OSError) as exc:
        die("cannot fetch %s: %s\n"
            "  The APPLE2 package needs the network once; after that the\n"
            "  cache beside the output is used and nothing is downloaded again."
            % (url, exc))


def get_part(path, cachedir, check, src):
    """The bytes of one pinned file: from the cache, a local checkout
    (--from) or the network, verified whichever way it came."""
    want_sha = [p[3] for p in PARTS if p[0] == path][0]
    want_len = [p[2] for p in PARTS if p[0] == path][0]
    cache = os.path.join(cachedir, os.path.basename(path))
    data = None
    if os.path.exists(cache):
        with open(cache, "rb") as fh:
            data = fh.read()
        if sha256(data) != want_sha:
            data = None
    if data is None and src:
        p = os.path.join(src, path)
        if not os.path.exists(p):
            die("--from %s: no %s" % (src, path))
        with open(p, "rb") as fh:
            data = fh.read()
    if data is None:
        if check:
            die("%s is not cached in %s (--check does not fetch)" % (path, cachedir))
        print("getapple2rom: fetching %s" % path)
        data = fetch(path)
    if len(data) != want_len or sha256(data) != want_sha:
        die("%s: SHA-256/size mismatch against the pin %s\n"
            "  expected %s (%d bytes)\n"
            "  got      %s (%d bytes)\n"
            "  This is not the ROM the port was written against. Check it by hand."
            % (path, COMMIT[:12], want_sha, want_len, sha256(data), len(data)))
    os.makedirs(cachedir, exist_ok=True)
    if not os.path.exists(cache):
        with open(cache, "wb") as fh:
            fh.write(data)
    return data


def assemble(cachedir, check, src):
    img = bytearray(ROMSIZE)
    for path, off, length, _ in PARTS:
        img[off:off + length] = get_part(path, cachedir, check, src)
    vec = img[0x2FFC] | (img[0x2FFD] << 8)
    if vec != RESET_VECTOR:
        die("the RESET vector at $FFFC reads $%04X, expected $%04X: this is not "
            "the Autostart Monitor" % (vec, RESET_VECTOR))
    return bytes(img)


def main():
    ap = argparse.ArgumentParser(
        description="Fetch the pinned Apple II+ ROM images and build APPLE2.ROM.")
    ap.add_argument("-o", "--out",
                    default=os.path.join(ROOT, "build", "apple2-rom", "APPLE2.ROM"),
                    help="output path (default build/apple2-rom/APPLE2.ROM)")
    ap.add_argument("--check", action="store_true",
                    help="verify the cached inputs; fetch nothing, write nothing")
    ap.add_argument("--from", dest="src", default=None,
                    help="a local AppleWin checkout at the pinned commit")
    args = ap.parse_args()

    outdir = os.path.dirname(os.path.abspath(args.out))
    cachedir = os.path.join(outdir, ".artifacts")
    img = assemble(cachedir, args.check, args.src)
    if len(img) != ROMSIZE:
        die("internal: assembled %d bytes, expected %d" % (len(img), ROMSIZE))

    if args.check:
        print("getapple2rom: 3 inputs verified, %d bytes would be written" % ROMSIZE)
        return

    os.makedirs(outdir, exist_ok=True)
    if os.path.exists(args.out):
        with open(args.out, "rb") as fh:
            if fh.read() == img:
                print("getapple2rom: %s is up to date" % args.out)
                return
    with open(args.out, "wb") as fh:
        fh.write(img)
    print("getapple2rom: %s  %d bytes  (ROM 12288 + CHARGEN 2048 + DISK2 256, "
          "padded to 29 sectors; AppleWin %s)" % (args.out, ROMSIZE, COMMIT[:12]))


if __name__ == "__main__":
    main()
