#!/usr/bin/env python3
"""THE FIRMWARE AND UTILITIES ZIP - everything a release ships that is not a
disk, in one archive beside the disk zip.

    make toolszip          # -> build/os8088-tools.zip

One folder, `os8088-tools/`:

  FIRMWARE/PICOMEM1/  firmware/picomem1/ as committed - the PicoMEM 1.x image
                      os8088 needs, its licence (GPL-2) and its source patch
  FIRMWARE/ROM/       the kernel ROMs for an IBM 5150's spare sockets, cut
                      against THIS tree's kernels (`make rom-big rom-small`),
                      and the socket-check ROM (`make socketrom`)
  ENCODER/            `make vencbundle`'s folder - the video encoder
  GBSTATIC/           `make gbaotbundle`'s folder - the Game Boy precompiler
  CZ/                 `make czbundle`'s folder - the file splitter
  PROXY/              the Browser's HTTPS proxy, os88proxygui.py and what it
                      imports (os88vbundle.closure, standard library only)
  DOS/                OS88CZ.COM and OS88NET.COM, for a PC running DOS

The three bundles are taken AS BUILT, their own top folder renamed, so each
keeps its own README and its own computed file list and this file restates
none of them. osrom-big pairs with the release's ordinary system disks and
osrom-small with its 128K-RAM ones (SPEC.md 24.5.7). The zip is DETERMINISTIC:
sorted entries, every timestamp 1980-01-01.
"""
import argparse
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import os88vbundle  # noqa: E402

TOP = "os8088-tools"
ROMS = ("osrom-big", "osrom-small", "osrom-socket")
SOCKETS = ("U28", "U29", "U30", "U31", "U32")
PROXY_ROOTS = ("os88proxygui.py", "os88proxy.py")

README = """os8088 FIRMWARE AND UTILITIES
=============================

Everything in this release that is not a disk image. Each folder stands on
its own and most have a README.TXT of their own.

FIRMWARE/PICOMEM1
  The PicoMEM 1.x firmware os8088 needs: without its fixes the card's Sound
  Blaster crackles or stays silent and a busy machine loses disk sectors.
  Copy the .uf2 to the Pico W in its USB boot mode. README.md says what it
  fixes and where its source is; it is GPL-2 (COPYING).

FIRMWARE/ROM
  os8088's kernel in ROM, for an IBM PC 5150's five spare ROM sockets, and a
  ROM that tests the sockets first. See ROM/README.TXT.

ENCODER
  The video encoder: turns a video into a .V88 file for os8088's Video
  Player, and makes floppies or bootable hard disks with it.
  python3 os88vencgui.py - see ENCODER/README.TXT.

GBSTATIC
  The Game Boy precompiler: turns a Game Boy cartridge you own into an
  os8088 package that runs it with RealDMG, translated ahead of time.
  python3 gbaotgui.py - see GBSTATIC/README.TXT.

CZ
  The file splitter and compressor: cuts a file too big for a floppy into
  parts os8088 joins back together. python3 os88czgui.py - see CZ/README.TXT.

PROXY
  The Browser's proxy: run it on a modern computer on the same network and
  os8088's Browser reaches HTTPS sites through it. python3 os88proxygui.py -
  see PROXY/README.TXT.

DOS
  For a PC running DOS at the other end of a disk or a cable:
    OS88CZ.COM   joins, splits and expands the CZ tool's files
    OS88NET.COM  the DOS end of os8088's serial cable link

Every Python tool here needs Python 3.8 or newer and nothing else unless its
README names a package. The ROM and the tools are built from the same os8088
tree as the disks in this release.
"""

ROM_README = """os8088 IN ROM - IBM PC 5150
===========================

An IBM PC 5150 has five 8 KB ROM sockets beside its BIOS, U28 to U32, at
F4000-FDFFF: U28 is empty and U29-U32 hold Cassette BASIC. These files put
part of os8088's kernel there, so it runs from ROM and leaves more memory
free: about 37 KB with the ordinary kernel, 21 KB with the small one.

THE FILES
  osrom-socket-U28.bin ... -U32.bin   the socket check: burn these FIRST. At
                                      power-on the machine prints
                                      "U28 ok ... U32 ok" if every chip and
                                      socket works. No os8088 in them.
  osrom-big-U28.bin ... -U32.bin      os8088's kernel ROM, one file per
                                      socket - for the ordinary system disks
  osrom-small-U28.bin ... -U32.bin    the same for the small kernel - for the
                                      system disks in 128K-RAM folders; it
                                      also holds the file dialog, Cut/Copy/
                                      Paste, the Control Panel, the
                                      formatter and the Task Manager
  osrom-big.bin, osrom-small.bin,     each set as one 40 KB file, for a ROM
  osrom-socket.bin                    board or a programmer that wants it whole

NEEDS
  An IBM PC 5150 with the 10/27/82 BIOS - the first one that looks for a
  ROM at F4000 when it starts. Burning these replaces Cassette BASIC.
  The sockets take 2364-type ROMs: a 2764 or 27C64 EPROM needs a 2364
  adapter; a Motorola MCM68766 fits as it is.

THIS ROM BELONGS TO THIS RELEASE
  Each is cut against the kernel on this release's system disks - big
  for the ordinary ones, small for the 128K-RAM ones - and checks that
  kernel byte for byte before it is used. With any other release's
  disks it is ignored and the machine boots exactly as it would without it.
  Burn the ROM from the same release as the disks you boot.
"""

PROXY_README = """os8088 BROWSER PROXY
====================

os8088's Browser cannot do HTTPS - the encryption is minutes of arithmetic
on an 8088 - and a modern page is far bigger than it can read. This proxy
runs on a modern computer, fetches the page over HTTPS, and hands os8088 a
version of it the Browser can read.

    python3 os88proxygui.py

or, without a window:

    python3 os88proxy.py              serve on port 8088
    python3 os88proxy.py --render URL show what os8088 would receive

NEEDS
  Python 3.8 or newer with Tk for the window (python.org's installer has it;
  on Linux the python3-tk package). Nothing else.

USING IT
  Start it on a computer the os8088 machine can reach. In os8088's Browser,
  choose File > Open Location..., type that computer's address and :8088,
  and press Return.
"""


def rebase(zpath, old, new):
    """[(name, data, mode)] of a bundle zip, its top folder `old` renamed"""
    out = []
    with zipfile.ZipFile(zpath) as z:
        for zi in z.infolist():
            if zi.is_dir():
                continue
            head, _, rest = zi.filename.partition("/")
            if head != old or not rest:
                sys.exit("toolszip: %s holds %s, outside its %s/ folder"
                         % (zpath, zi.filename, old))
            out.append(("%s/%s" % (new, rest), z.read(zi),
                        (zi.external_attr >> 16) & 0o777 or 0o644))
    return out


def readf(path):
    with open(path, "rb") as f:
        return f.read()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("--encoder", required=True, help="build/os8088-encoder.zip")
    ap.add_argument("--gbstatic", required=True, help="build/os8088-gbaot.zip")
    ap.add_argument("--cz", required=True, help="build/os8088-cz.zip")
    ap.add_argument("--rom", required=True, help="build/rom")
    ap.add_argument("--picomem", required=True, help="firmware/picomem1")
    ap.add_argument("--dos", nargs="+", required=True, help="the .COM files")
    a = ap.parse_args()

    files = []
    files += rebase(a.encoder, "os8088-encoder", "ENCODER")
    files += rebase(a.gbstatic, "os8088-gbaot", "GBSTATIC")
    files += rebase(a.cz, "os8088-cz", "CZ")

    for n in sorted(os.listdir(a.picomem)):
        p = os.path.join(a.picomem, n)
        if os.path.isfile(p):
            files.append(("FIRMWARE/PICOMEM1/" + n, readf(p), 0o644))

    for r in ROMS:
        for n in [r + ".bin"] + ["%s-%s.bin" % (r, s) for s in SOCKETS]:
            p = os.path.join(a.rom, n)
            if not os.path.isfile(p):
                sys.exit("toolszip: %s is not built - `make rom-big socketrom`" % p)
            files.append(("FIRMWARE/ROM/" + n, readf(p), 0o644))
    files.append(("FIRMWARE/ROM/README.TXT", ROM_README, 0o644))

    mods, other = os88vbundle.closure(PROXY_ROOTS)
    if other:
        sys.exit("toolszip: the proxy imports %s, which is not in tools/ - it is "
                 "meant to need the standard library only" % ", ".join(sorted(other)))
    for m in mods:
        files.append(("PROXY/" + m, readf(os.path.join(HERE, m)), 0o755))
    files.append(("PROXY/README.TXT", PROXY_README, 0o644))

    for p in a.dos:
        files.append(("DOS/" + os.path.basename(p).upper(), readf(p), 0o644))
    files.append(("README.TXT", README, 0o644))

    names = [n for n, _, _ in files]
    dup = sorted({n for n in names if names.count(n) > 1})
    if dup:
        sys.exit("toolszip: two files would land at %s" % ", ".join(dup))

    tmp = a.out + ".part"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data, mode in sorted(files):
            if isinstance(data, str):
                data = data.replace("\n", "\r\n").encode()
            zi = zipfile.ZipInfo("%s/%s" % (TOP, name), os88vbundle.STAMP)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = (0o100000 | mode) << 16
            z.writestr(zi, data)
    os.replace(tmp, a.out)
    print("toolszip: %s - %d files, %d bytes" % (a.out, len(files),
                                                os.path.getsize(a.out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
