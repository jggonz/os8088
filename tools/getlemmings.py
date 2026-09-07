#!/usr/bin/env python3
"""getlemmings: fetch the 26 DOS Lemmings (1991) data files the LEMMINGS
package is converted from (SPEC.md 92.2).

    python3 tools/getlemmings.py                       # fetch + verify
    python3 tools/getlemmings.py --from ~/lemdata/orig # copy from a local dir
    python3 tools/getlemmings.py --from orig.zip       # or a local zip
    python3 tools/getlemmings.py --list                # print the manifest
    python3 tools/getlemmings.py --check               # verify, never fetch

**Nothing this script downloads is committed** (CONTRIBUTING.md 6, SPEC.md
92.2). The 26 files are DMA Design's and Psygnosis's work, a git repository is
not a distribution channel for them, and so the bytes land in build/lemdata/,
which is ignored outright. This is getstories.py's decision taken again for a
stronger reason: a story file is its author's freeware release and these are
not released at all.

ONE PINNED URL and ONE SHA-256 (SPEC.md 92.2): Lemmix's own copy of the
original data, `src/Data/Styles/Orig/orig.zip` at commit
40e9bc34451f0e9127fd53b290a3877e700d143d. Per-file SHA-256s are pinned as
well, so `--from` verifies a local copy to exactly the same standard the
download is held to and neither route can put a changed byte on a floppy. The
images are meant to rebuild byte-for-byte (tools/os88disk.py pins the volume
serial and every FAT timestamp) and tools/os88lem.py is deterministic, so a
data file that quietly changed under us would break that silently.

`--from` exists because a build here must not need the network: point it at a
directory holding the 26 files, or at a local copy of the zip, and the same
verification runs. It is the only way to build the LEMMINGS floppies on a
machine that cannot reach github.

On success it writes build/lemdata.stamp, which is what gates the disk target
(never `make lemmings`: the package builds against the committed synthetic
fixture written by `tools/os88lem.py --fixture`, SPEC.md 92.12).
"""
import argparse
import hashlib
import io
import os
import sys
import urllib.error
import urllib.request
import zipfile

COMMIT = "40e9bc34451f0e9127fd53b290a3877e700d143d"
URL = ("https://raw.githubusercontent.com/ericlangedijk/Lemmix/"
       + COMMIT + "/src/Data/Styles/Orig/orig.zip")
ZIP_SHA = "bf1f2dbd11cd20df9f748047f8c4a32e7033d3ac7bb4b77058c3ecf27c0a9d6d"
ZIP_SIZE = 336839
TIMEOUT = 120

# The 26 files, with the size and SHA-256 of each. 358,154 bytes shipped,
# 956,492 fully decompressed (SPEC.md 92.2), and every one of the 101
# compressed sections in the 21 compressed files decodes on the first attempt
# - which is what tests/unit/t_lemdat.py checks rather than assumes.
#
#   LEVELxxx.DAT  8 compressed sections each, one 2,048-byte level record per
#                 section: 80 records for the 120 levels the game presents
#   GROUNDxO.DAT  NOT compressed - 16 OBJECT_INFO, 64 TERRAIN_INFO, palettes
#   VGAGRx.DAT    2 sections: terrain bitmaps, then object frames
#   VGASPECx.DAT  1 section, and a second RLE layer inside it
#   MAIN.DAT      7 sections: sprites, masks, panels, menu art, fonts
#   ODDTABLE.DAT  NOT compressed - 80 records of 56 bytes
FILES = [
    ("LEVEL000.DAT", 3722,
     "2b9d4502ae1837faa3a6ff7a8b11eb75cbb6a555aa0e5c9f266f0e33a4cf444f"),
    ("LEVEL001.DAT", 4519,
     "52c60bf66d31514ba7f05970901cbde98e10e4020b8cc5d99c5bdf0eca43a2a9"),
    ("LEVEL002.DAT", 4819,
     "3216e70e91b5a1f8fc34d92e90167762df018f56dbd253b02e655e2beb1e0854"),
    ("LEVEL003.DAT", 6827,
     "6a4984e3ef13e8450c65dffe67baf9b7dee22f2f97af42072c60c8965d7ba94c"),
    ("LEVEL004.DAT", 3889,
     "c4c103ef26a275918586eed9e9fc63ba1f91125fe7bd70c0c1f97614367f50df"),
    ("LEVEL005.DAT", 7255,
     "ba7ae3d24331e1a0d87b46d1332b4cf74a09c9e584ad12ea672ee555da1a52cc"),
    ("LEVEL006.DAT", 7149,
     "34052d7c5ea66748c5a81fe69060bbf081d3be3232dce05945b51bf2cb4f0472"),
    ("LEVEL007.DAT", 5253,
     "f827359ac4a66e5e945d74f322982e8419d7c55f2e47044674fe10fd423a9cde"),
    ("LEVEL008.DAT", 7037,
     "b014f73c65aa57c7a014c896ac4f7dd58291946526db5ebb059ebbd3ea80d174"),
    ("LEVEL009.DAT", 4932,
     "0cb73f259510c25c29f21103ac1d04c0643756b6ea81b91b47d0c46259fe1a83"),
    ("GROUND0O.DAT", 1056,
     "86ecce12dea563b97d07339028780a0a50fc2cc75a67f682bf4d9aff8cf4b24a"),
    ("GROUND1O.DAT", 1056,
     "baf7a2ea30b07d2dc7968a5fb590987b3775840c57e6268437d3dce969b64d65"),
    ("GROUND2O.DAT", 1056,
     "52e62aeefd6cd548690c02af6474936c5e16112e115ca7808c3748e500a9412d"),
    ("GROUND3O.DAT", 1056,
     "f4df8579a30cb55dae4a06f606e0453cb0931219efadf3e9465d681a43a2722c"),
    ("GROUND4O.DAT", 1056,
     "9669ee978e5adf5f8d5bfeb80ccc5d6c20e75e32f55e2a5c1c738f64af8faa91"),
    ("VGAGR0.DAT", 24464,
     "9bd5ae2e0aed013bd369a469ac14afeb1beb22524fc820920d6e38766ea48680"),
    ("VGAGR1.DAT", 32966,
     "bf49fc528b68d00c5738b6e7cd9244812bc74b0fc03ea9f862009a11009149e5"),
    ("VGAGR2.DAT", 22290,
     "f8305e71295c5909ee7692e4391aae65443298faef4d96e5fe17b24afa0eadcc"),
    ("VGAGR3.DAT", 22775,
     "19d3e2ba0db60bf855d8ce83459f53d83a2048763124932f533eb1c42f7809d9"),
    ("VGAGR4.DAT", 28692,
     "40a99d3d9540609da9d6a74c07a832786818aed361326e37c01a909a1615921d"),
    ("VGASPEC0.DAT", 28655,
     "a647110ca059fd18917e8a289668167a3a77975ca1096d7f7fdd9557fee95ce0"),
    ("VGASPEC1.DAT", 18567,
     "b0a8ce1a8888feda8720c7df1222911ae9241f3acbfd42adda243bd5ba5a2306"),
    ("VGASPEC2.DAT", 23284,
     "602fde697bb49df8d86fd800643cc3439a4f136223af0bf1ee46e2bff5e0e3cd"),
    ("VGASPEC3.DAT", 34827,
     "918f21db3a16741f1a58b06bcef2b4ff63dc16f0e8746d0393dc023ea57cb2d4"),
    ("MAIN.DAT", 56472,
     "2aec688c334e60322998811a8b6f9d5c13f8090b46ca6c2a6cb1c8db3b733545"),
    ("ODDTABLE.DAT", 4480,
     "e6d425592fe2cda43ec1169995cd8deeb0bdb74eb464728f4c05eb1ddefae8d0"),
]

BY_NAME = {n: (sz, sha) for (n, sz, sha) in FILES}
TOTAL = sum(sz for (_, sz, _) in FILES)


def fail(msg):
    print("getlemmings: error: " + msg, file=sys.stderr)
    sys.exit(1)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def verify(name, data):
    """Check one data file, whatever produced it."""
    size, want = BY_NAME[name]
    if len(data) != size:
        fail("%s: %d bytes, the manifest says %d" % (name, len(data), size))
    got = sha256(data)
    if got != want:
        fail("%s: SHA-256 mismatch\n"
             "  expected %s\n"
             "  got      %s\n"
             "  This is not the file SPEC.md 92.2 pins. Check it by hand."
             % (name, want, got))


def fetch():
    req = urllib.request.Request(
        URL, headers={"User-Agent": "os8088-getlemmings/1"})
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT) as fh:
            return fh.read()
    except (urllib.error.URLError, OSError) as exc:
        fail("cannot fetch %s: %s\n"
             "  LEMMINGS needs the data once; after that build/lemdata is a\n"
             "  cache. On a machine with no network, use\n"
             "    python3 tools/getlemmings.py --from <dir-or-zip>\n"
             "  and point it at a copy you already have." % (URL, exc))


def from_zip(raw, source, check_archive):
    """Pull the 26 files out of a zip, by basename, case-insensitively.

    The archive lays them out under its own directory names; what matters is
    the file, so the member is found by basename and the SHA-256 decides
    whether it is the right one.
    """
    if check_archive:
        if len(raw) != ZIP_SIZE:
            fail("%s: %d bytes, SPEC.md 92.2 pins %d"
                 % (source, len(raw), ZIP_SIZE))
        got = sha256(raw)
        if got != ZIP_SHA:
            fail("%s: archive SHA-256 mismatch\n"
                 "  expected %s\n"
                 "  got      %s" % (source, ZIP_SHA, got))
    try:
        zf = zipfile.ZipFile(io.BytesIO(raw))
    except zipfile.BadZipFile as exc:
        fail("%s: not a readable zip: %s" % (source, exc))
    index = {}
    for member in zf.namelist():
        if member.endswith("/"):
            continue
        index.setdefault(os.path.basename(member).upper(), member)
    out = {}
    for (name, _, _) in FILES:
        member = index.get(name)
        if member is None:
            fail("%s: no member named %s in the archive" % (source, name))
        out[name] = zf.read(member)
    return out


def from_dir(path):
    out = {}
    for (name, _, _) in FILES:
        # A DOS floppy's names are uppercase and a unix copy of one may not
        # be, so both spellings are accepted and neither is preferred.
        for spelling in (name, name.lower()):
            full = os.path.join(path, spelling)
            if os.path.exists(full):
                with open(full, "rb") as fh:
                    out[name] = fh.read()
                break
        else:
            fail("%s: %s is not in that directory" % (path, name))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-o", "--output", metavar="DIR", default="build/lemdata",
                    help="where the 26 files land (default build/lemdata)")
    ap.add_argument("--from", dest="source", metavar="PATH",
                    help="take the files from a local zip or directory "
                         "instead of the network")
    ap.add_argument("--list", action="store_true",
                    help="print the manifest and exit")
    ap.add_argument("--check", action="store_true",
                    help="verify what is already there; never fetch")
    ap.add_argument("--stamp", metavar="FILE", default=None,
                    help="the stamp file to write (default "
                         "<output>/../lemdata.stamp)")
    args = ap.parse_args()

    if args.list:
        for (name, size, sha) in FILES:
            print("%-13s %7d  %s" % (name, size, sha))
        print("\n%d files, %d bytes shipped, 956,492 fully decompressed."
              % (len(FILES), TOTAL))
        print("Source: %s" % URL)
        print("        SHA-256 %s (%d bytes)" % (ZIP_SHA, ZIP_SIZE))
        return

    out_dir = args.output
    stamp = args.stamp or os.path.join(os.path.dirname(out_dir.rstrip("/"))
                                       or ".", "lemdata.stamp")

    # Already there and correct? Then there is nothing to do, on any route.
    # A cache that is there and WRONG is re-acquired rather than refused: it
    # is only a cache, and refusing would leave no way to repair it but rm.
    # --check is the one caller that wants the refusal, and it says so.
    complete = True
    for (name, size, want) in FILES:
        full = os.path.join(out_dir, name)
        if not os.path.exists(full):
            complete = False
            break
        with open(full, "rb") as fh:
            data = fh.read()
        if len(data) != size or sha256(data) != want:
            if args.check:
                verify(name, data)              # fails, with the two hashes
            print("getlemmings: %s in %s does not match the manifest - "
                  "taking the set again" % (name, out_dir))
            complete = False
            break
    if complete:
        print("getlemmings: %d files already in %s, all verified (%d bytes)"
              % (len(FILES), out_dir, TOTAL))
        write_stamp(stamp, out_dir)
        return

    if args.check:
        fail("%s does not hold all 26 files (--check does not fetch)"
             % out_dir)

    if args.source:
        src = args.source
        if os.path.isdir(src):
            print("getlemmings: copying from %s" % src)
            data = from_dir(src)
        elif os.path.isfile(src):
            print("getlemmings: unpacking %s" % src)
            with open(src, "rb") as fh:
                raw = fh.read()
            # A local zip is verified as an archive only when it IS the
            # pinned archive; a re-zipped copy still has to produce 26
            # files whose own SHA-256s match, which is the check that
            # matters.
            data = from_zip(raw, src, check_archive=(len(raw) == ZIP_SIZE))
        else:
            fail("%s is neither a directory nor a file" % src)
    else:
        print("getlemmings: fetching %s" % URL)
        data = from_zip(fetch(), URL, check_archive=True)

    for (name, _, _) in FILES:
        verify(name, data[name])

    os.makedirs(out_dir, exist_ok=True)
    for (name, _, _) in FILES:
        with open(os.path.join(out_dir, name), "wb") as fh:
            fh.write(data[name])

    print("getlemmings: %d files in %s, %d bytes, every SHA-256 verified"
          % (len(FILES), out_dir, TOTAL))
    write_stamp(stamp, out_dir)


def write_stamp(path, out_dir):
    """The stamp the disk target hangs off (SPEC.md 92.2).

    It names the directory and the commit rather than being empty, so a stale
    stamp beside a moved cache is readable rather than mysterious.
    """
    parent = os.path.dirname(path)
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(path, "w") as fh:
        fh.write("lemdata %s\n%s\n%d files %d bytes\n"
                 % (COMMIT, os.path.abspath(out_dir), len(FILES), TOTAL))
    print("getlemmings: stamp -> %s" % path)


if __name__ == "__main__":
    main()
