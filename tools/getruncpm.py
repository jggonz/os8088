#!/usr/bin/env python3
"""getruncpm: fetch what the RUNCPM floppies ship from RunCPM's repository.

    python3 tools/getruncpm.py -o build/runcpm-disk            # fetch + verify
    python3 tools/getruncpm.py -o build/runcpm-disk --check    # verify, never fetch
    python3 tools/getruncpm.py -o build/runcpm-disk --list     # what is shipped
    python3 tools/getruncpm.py -o build/runcpm-disk --select 360 \
                             --reserve build/runcpm.o88 ...   # the A/0 files
                             # a 360KB / 720KB / 1440KB disk carries beside
                             # the root files named
    python3 tools/getruncpm.py -o build/runcpm-disk --from DIR # take the files
                             # from a local checkout of RunCPM at the pin

**Nothing this script downloads is committed** (CONTRIBUTING.md 6): the DRI
CCP binary and the master disk are RunCPM's (Marcelo Dantas / Mockba the
Borg, MIT - the LICENSE lands beside them), and the programs on the master
disk are their own authors' (MBASIC, Z80ASM, TE, ...). The bytes land in
build/, which is ignored outright, and the floppies are built from there -
the same decision tools/getstories.py made for Frotz's stories, and the
os8088 tree pins the upstream COMMIT rather than a branch so that the images
rebuild byte for byte (tools/os88disk.py pins the volume serial and every
timestamp; this pins the input).

THE PIN is the commit SPEC.md 71's banner names as its 'Built' date, and
every artifact's SHA-256 is checked on the way in - a mismatch is a hard
failure and never a warning.

WHAT LANDS in the output directory:

    CCP-DR.60K       Digital Research's CCP for a 60K CP/M 2.2, the command
                     processor RUNCPM loads from its own folder (SPEC.md 71.3)
    LICENSE          RunCPM's MIT licence
    1STREAD.ME       the master disk's read-me
    A/0/*            the master disk, DISK/A0.zip unpacked: drive A user 0.
                     Files above 65,535 bytes are NOT extracted - the file API
                     a package reaches reads and writes whole files with a
                     16-bit count (SPEC.md 71.3, 71.4) - and are listed in
                     A/0/LEFT-OFF.TXT so the disk says what is missing (as of
                     the pin: Z80ASM.PDF, BDOS.ASM, ZCPR3.ASM)
    A0.list          every extracted file with its size, one per line

--select prints the A/0 files a given geometry carries, most useful first
(the texts and submit files - a few KB that make the disk explain itself
and give SUBMIT.COM something to submit - then the programs, then
documentation, libraries and sources), stopping when the geometry's cluster
budget is spent, so the Makefile's disk rules can pass them to os88disk.py
without a manifest checked in. THE BUDGET IS DERIVED, NOT GUESSED: --reserve
names the root files that ride beside A/0 (the package, its .OVL if one
comes, the CCP, LICENSE, the read-me, HELLO.COM on the 1.44MB disk) and
their sizes are priced in the geometry's own clusters, so the selection
re-shapes itself as the package grows and the disk build's --verify (still
the check) is never the first to hear of it; what stays a constant is the
directory arithmetic os88disk.py does - the folder chain A and A/0's own
directory (32 bytes an entry, counted as files are chosen) and the one-
cluster ASSOC.DAT it writes beside every package. Measured at the pin: a
720KB disk and a 1.44MB disk carry all of it; the 360KB one carries every
.COM, .SUB, .TXT and .ME and a little documentation (SPEC.md 71.5).
"""
import argparse
import hashlib
import io
import os
import sys
import urllib.error
import urllib.request
import zipfile

COMMIT = "e698e8ab59c2de915b23be7f5b146a5c621f5c76"      # 2026-07-21 20:43:19 -0400
RAW = "https://raw.githubusercontent.com/MockbaTheBorg/RunCPM/" + COMMIT + "/"
TIMEOUT = 120

# path in the repository -> (SHA-256 of the file at COMMIT, size)
PINNED = {
    "CCP/CCP-DR.60K":  ("ac62661629897b601c8fb8ebfb05a5b6265dd90de9406a264fcadc395dc3b7e8", 2048),
    "DISK/A0.zip":     ("52882689181d0ad17acbc113097e92b223395674dd4b16d6d8765f4643c9b518", 438350),
    "DISK/1STREAD.ME": ("dfccf8bd2cab3ae12653c3d5a2a7bb32277b18d7deb239158c71a43b46007ace", 3816),
    "LICENSE":         ("1ee0ceaa2d409ffe7d95b2ab1aac4948c6b4625c54aafc000c209084ac39e167", 1072),
}

MAX_FILE = 65535          # a whole file must fit a 16-bit count (SPEC.md 71.3)

# --select: what a geometry carries, in the order it is filled. Category first,
# then name, so the choice is deterministic and reads as a policy rather than
# a list. The texts and the submit files go FIRST: they are ~18 clusters in
# all, and a disk without them ships SUBMIT.COM with nothing to submit, no
# INFO.TXT and no LEFT-OFF.TXT saying what is missing - which is what an
# alphabetical .COM fill did to the 360KB disk once.
CATEGORY = [".TXT", ".ME", ".SUB", ".COM", ".DOC", ".LIB", ".Z80", ".ASM"]
# a geometry's cluster size and its DATA clusters (tools/os88disk.py's
# layouts: 354 / 713 / 2,847). What A/0 may take is that minus the root files
# --reserve names, priced in these clusters, minus the directory arithmetic
# os88disk.py does (below) - derived from the files, so a bigger package or
# an .OVL beside it re-selects the disk instead of overflowing it. Checked
# against the built images: the arithmetic below reproduces --verify's
# 'in use' exactly (353 / 672 / 1,294 with the 24,848-byte package)
GEOMETRY = {360: (1024, 354), 720: (1024, 713), 1440: (512, 2847)}
DIR_ENTRY = 32            # a FAT directory entry
FIXED_CLUSTERS = 2        # the folder A's own directory (one cluster: '.',
                          # '..' and '0') and ASSOC.DAT, the one-cluster
                          # icon/association cache os88disk.py writes in the
                          # root beside every package (SPEC.md 54.7)


def fail(msg):
    print(f"getruncpm: error: {msg}", file=sys.stderr)
    sys.exit(1)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def fetch(path):
    url = RAW + path
    req = urllib.request.Request(url, headers={"User-Agent": "os8088-getruncpm/1"})
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT) as fh:
            return fh.read()
    except (urllib.error.URLError, OSError) as exc:
        fail(f"cannot fetch {url}: {exc}\n"
             f"  The RUNCPM disks need the network once; after that the output\n"
             f"  directory is a cache and nothing is downloaded again.")


def get_artifact(path, out, check, src):
    """The bytes of one pinned repository file, from the cache, a local
    checkout (--from) or the network, verified whichever way."""
    want_sha, want_size = PINNED[path]
    cache = os.path.join(out, ".artifacts", os.path.basename(path))
    data = None
    if os.path.exists(cache):
        with open(cache, "rb") as fh:
            data = fh.read()
        if sha256(data) != want_sha:
            data = None
    if data is None and src:
        p = os.path.join(src, path)
        if not os.path.exists(p):
            fail(f"--from {src}: no {path}")
        with open(p, "rb") as fh:
            data = fh.read()
    if data is None:
        if check:
            fail(f"{path} is not cached in {out} (--check does not fetch)")
        print(f"getruncpm: fetching {path}")
        data = fetch(path)
    if len(data) != want_size or sha256(data) != want_sha:
        fail(f"{path}: SHA-256/size mismatch against the pin {COMMIT[:12]}\n"
             f"  expected {want_sha} ({want_size} bytes)\n"
             f"  got      {sha256(data)} ({len(data)} bytes)\n"
             f"  The upstream copy is not the pinned one. Check it by hand.")
    os.makedirs(os.path.dirname(cache), exist_ok=True)
    if not os.path.exists(cache):
        with open(cache, "wb") as fh:
            fh.write(data)
    return data


def write_if_changed(path, data):
    if os.path.exists(path):
        with open(path, "rb") as fh:
            if fh.read() == data:
                return False
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "wb") as fh:
        fh.write(data)
    return True


def unpack(out, raw):
    """DISK/A0.zip -> out/A/0, dropping what cannot be shipped."""
    zf = zipfile.ZipFile(io.BytesIO(raw))
    kept, left = [], []
    for info in zf.infolist():
        if info.is_dir():
            continue
        name = info.filename
        if not name.startswith("A/0/"):
            fail(f"A0.zip holds {name}: not under A/0/ - the layout moved")
        base = name[4:]
        if "/" in base or len(base) > 12 or base != base.upper():
            fail(f"A0.zip holds {name}: not an 8.3 name")
        if info.file_size > MAX_FILE:
            left.append((base, info.file_size))
            continue
        data = zf.read(info)
        write_if_changed(os.path.join(out, "A", "0", base), data)
        kept.append((base, len(data)))
    kept.sort()
    left.sort()
    # the disk says what is not on it, in its own terms (a CP/M user reads
    # this with TYPE and has no SPEC.md)
    text = ("Files of RunCPM's master disk that are NOT on this disk:\r\n"
            "this port reads and writes whole files and cannot open one\r\n"
            "larger than 65535 bytes, so these are left off.\r\n")
    for base, size in left:
        text += f"  {base:<12} {size:>7} bytes\r\n"
    # ...and the consequence, derived from the disk itself: a submit file
    # whose command names a left-off SOURCE by its stem (`MAC BDOS` assembles
    # BDOS.ASM) will not run here. The stem is matched as an OPERAND only -
    # Z80ASM.PDF's stem is also the assembler's name, and `Z80ASM INFO` runs
    # fine (as of the pin: BDOS.SUB and ZCPR3.SUB)
    stems = {os.path.splitext(b)[0] for b, _ in left
             if os.path.splitext(b)[1] in (".ASM", ".Z80", ".MAC")}
    broken = []
    for base, _ in kept:
        if not base.endswith(".SUB"):
            continue
        with open(os.path.join(out, "A", "0", base), "rb") as fh:
            body = fh.read().decode("latin-1")
        hit = None
        for line in body.splitlines():
            words = line.replace("=", " ").split()
            if not words or words[0].startswith(";"):
                continue
            for w in words[1:]:
                if w.upper() in stems:
                    hit = w.upper() + ".ASM"
                    break
            if hit:
                break
        if hit:
            broken.append((base, hit))
    if broken:
        text += "\r\n"
        for sub, src in broken:
            text += f"{sub} assembles {src} and will not run on this disk.\r\n"
    write_if_changed(os.path.join(out, "A", "0", "LEFT-OFF.TXT"), text.encode())
    kept.append(("LEFT-OFF.TXT", len(text)))
    kept.sort()
    listing = "".join(f"{base} {size}\n" for base, size in kept)
    write_if_changed(os.path.join(out, "A0.list"), listing.encode())
    return kept, left


def read_list(out):
    p = os.path.join(out, "A0.list")
    if not os.path.exists(p):
        fail(f"no {p}: run without --select first (or make runcpm-src)")
    rows = []
    with open(p) as fh:
        for line in fh:
            base, size = line.split()
            rows.append((base, int(size)))
    return rows


def clusters(size, cbytes):
    return max(1, (size + cbytes - 1) // cbytes)


def select(out, geometry, reserve):
    """The A/0 files a geometry carries, in fill order, beside the root
    files `reserve` names (paths; their sizes are priced here)."""
    if geometry not in GEOMETRY:
        fail(f"--select wants 360, 720 or 1440, not {geometry}")
    cbytes, total = GEOMETRY[geometry]
    rows = read_list(out)
    root = 0
    for path in reserve:
        try:
            root += clusters(os.path.getsize(path), cbytes)
        except OSError as exc:
            fail(f"--reserve {path}: {exc}")
    budget = total - root - FIXED_CLUSTERS

    def rank(row):
        base = row[0]
        ext = os.path.splitext(base)[1]
        c = CATEGORY.index(ext) if ext in CATEGORY else len(CATEGORY)
        return (c, base)

    def dir_clusters(nfiles):
        return clusters((2 + nfiles) * DIR_ENTRY, cbytes)   # '.', '..', files

    used, chosen = 0, []
    for base, size in sorted(rows, key=rank):
        n = clusters(size, cbytes)
        if used + n + dir_clusters(len(chosen) + 1) > budget:
            continue
        used += n
        chosen.append(base)
    used += dir_clusters(len(chosen))
    return chosen, used, budget


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-o", "--output", metavar="DIR", default="build/runcpm-disk",
                    help="where the files land (a cache; default build/runcpm-disk)")
    ap.add_argument("--check", action="store_true",
                    help="verify what is cached; never download")
    ap.add_argument("--list", action="store_true", help="print what is shipped and exit")
    ap.add_argument("--select", type=int, metavar="KB",
                    help="print the A/0 files a 360/720/1440 disk carries, one path per line")
    ap.add_argument("--reserve", nargs="*", default=[], metavar="FILE",
                    help="with --select: the root files that ride beside A/0, priced first")
    ap.add_argument("--from", dest="src", metavar="DIR",
                    help="a local checkout of RunCPM at the pinned commit to take the files from")
    args = ap.parse_args()

    if args.select:
        chosen, used, budget = select(args.output, args.select, args.reserve)
        for base in chosen:
            print(os.path.join(args.output, "A", "0", base))
        print(f"getruncpm: {len(chosen)} files, {used} of {budget} clusters "
              f"for A/0 on the {args.select}KB disk (after {len(args.reserve)} "
              f"root files)", file=sys.stderr)
        return

    out = args.output
    os.makedirs(out, exist_ok=True)
    ccp = get_artifact("CCP/CCP-DR.60K", out, args.check, args.src)
    write_if_changed(os.path.join(out, "CCP-DR.60K"), ccp)
    lic = get_artifact("LICENSE", out, args.check, args.src)
    write_if_changed(os.path.join(out, "LICENSE"), lic)
    rme = get_artifact("DISK/1STREAD.ME", out, args.check, args.src)
    write_if_changed(os.path.join(out, "1STREAD.ME"), rme)
    a0 = get_artifact("DISK/A0.zip", out, args.check, args.src)
    kept, left = unpack(out, a0)
    total = sum(s for _, s in kept)
    if args.list:
        for base, size in kept:
            print(f"  A/0/{base:<12} {size:>6}")
        print(f"  left off (above {MAX_FILE} bytes):")
        for base, size in left:
            print(f"  A/0/{base:<12} {size:>6}")
    print(f"getruncpm: RunCPM {COMMIT[:12]}: CCP-DR.60K, LICENSE, 1STREAD.ME and "
          f"{len(kept)} files ({total} bytes) in {out}/A/0; {len(left)} left off")


if __name__ == "__main__":
    main()
