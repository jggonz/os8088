#!/usr/bin/env python3
"""getruncpm: fetch what the RUNCPM floppies ship from RunCPM's repository.

    python3 tools/getruncpm.py -o build/runcpm-disk            # fetch + verify
    python3 tools/getruncpm.py -o build/runcpm-disk --check    # verify, never fetch
    python3 tools/getruncpm.py -o build/runcpm-disk --list     # what is shipped
    python3 tools/getruncpm.py -o build/runcpm-disk --select 360 \
                             --reserve build/runcpm.o88 ...   # the A/0 files
                             # a 360KB / 720KB / 1440KB disk carries beside
                             # the root files named (--folders N: how many
                             # other folders the disk has; apps-all's ten)
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

THE PIN is the commit SPEC.md 74's banner names as its 'Built' date, and
every artifact's SHA-256 is checked on the way in - a mismatch is a hard
failure and never a warning.

WHAT LANDS in the output directory:

    CCP-DR.60K       Digital Research's CCP for a 60K CP/M 2.2, the command
                     processor RUNCPM loads from its own folder (SPEC.md 74.3)
    LICENSE          RunCPM's MIT licence
    1STREAD.ME       the master disk's read-me
    A/0/*            the master disk, DISK/A0.zip unpacked: drive A user 0.
                     Files above 65,535 bytes are NOT extracted - the file API
                     a package reaches reads and writes whole files with a
                     16-bit count (SPEC.md 74.3, 74.4) - and are listed in
                     A/0/LEFT-OFF.TXT so the disk says what is missing (as of
                     the pin: Z80ASM.PDF, BDOS.ASM, ZCPR3.ASM)
    A0.list          every extracted file with its size, one per line
    left-off.list    the files above 65,535 bytes, with their sizes
    left-off/<KB>/LEFT-OFF.TXT   written by --select: the geometry's own
                     note (below), which the disk rules put in A/0 in place
                     of the master folder's

--select prints the A/0 files a given geometry carries, most useful first
(the texts and submit files - a few KB that make the disk explain itself
and give SUBMIT.COM something to submit - then the programs, then
documentation, libraries and sources), stopping when the geometry's cluster
budget is spent, so the Makefile's disk rules can pass them to os88disk.py
without a manifest checked in. THE 360KB DISK IS CURATED, NOT MERELY FILLED
(SPEC.md 74.5): it carries the programs and the texts - every .COM, .SUB,
.TXT and .ME - and none of the sources, libraries or documentation, so that
an XT session has room to SAVE (the kernel does not grow a directory and a
full disk cannot take a $$$.SUB, an MBASIC program or TE's file); the 720KB
and 1.44MB disks carry the whole master disk. Whatever a geometry leaves off
is NAMED ON THAT DISK: --select writes the geometry's own LEFT-OFF.TXT
(<out>/left-off/<KB>/LEFT-OFF.TXT) - the files above 65,535 bytes, every
submit file whose source this disk lacks (two on 720KB/1.44MB, eleven on
360KB - only CLEAN.SUB runs there), and every master-disk file this geometry does not carry by policy
or has no room for, each under its own heading - and prints it in place of
A/0's, so a CP/M user can TYPE what is missing and why. 1STREAD.ME
(upstream's, on A/0 and in the root) credits the programs beyond Digital
Research's distribution. THE BUDGET IS DERIVED, NOT GUESSED: --reserve
names the root files that ride beside A/0 (the package, its .OVL if one
comes, the CCP, LICENSE, the read-me) and
their sizes are priced in the geometry's own clusters, so the selection
re-shapes itself as the package grows and the disk build's --verify (still
the check) is never the first to hear of it; what stays a constant is the
directory arithmetic os88disk.py does - the disk's other folders (--folders,
a cluster each; the folder A by default), A/0's own directory (32 bytes an
entry, counted as files are chosen) and ASSOC.DAT, which is priced by
os88disk.py's own build_assoc on the packages --reserve names (one cluster
here, four on apps-all's 22 packages). Measured at the pin: a
720KB disk and a 1.44MB disk carry all of it; the 360KB one carries every
.COM, .SUB, .TXT and .ME and nothing else (52 files, 350/354 clusters;
SPEC.md 74.5).
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

MAX_FILE = 65535          # a whole file must fit a 16-bit count (SPEC.md 74.3)

# --select: what a geometry carries, in the order it is filled. Category first,
# then name, so the choice is deterministic and reads as a policy rather than
# a list. The texts and the submit files go FIRST: they are ~18 clusters in
# all, and a disk without them ships SUBMIT.COM with nothing to submit, no
# INFO.TXT and no LEFT-OFF.TXT saying what is missing - which is what an
# alphabetical .COM fill did to the 360KB disk once.
CATEGORY = [".TXT", ".ME", ".SUB", ".COM", ".DOC", ".LIB", ".Z80", ".ASM"]
# ...and the 360KB disk stops after the programs (SPEC.md 74.5): the sources,
# libraries and documentation stay on the 720KB/1.44MB disks, so the XT disk
# ships with room to save into instead of full to the last cluster (which is
# how the wave-4 build shipped - and no session could save on it)
# The 1.44MB disk needs no rule of its own once it carries games (SPEC.md
# 71.6): the games are priced first (--reserve-clusters) and this RANKED fill
# then spends what is left, which stops it in the libraries - every program,
# every text and both .DOC files, and the sources left off for want of room
# rather than by a policy. Adding a CURATED[1440] here would have said the
# same thing about build/runcpm.img and a DIFFERENT one about apps-all.img,
# whose RUNCPM folder carries no games and has room for all 77.
CURATED = {360: CATEGORY[:4]}
# a geometry's cluster size and its DATA clusters (tools/os88disk.py's
# layouts: 354 / 713 / 2,847). What A/0 may take is that minus the root files
# --reserve names, priced in these clusters, minus the directory arithmetic
# os88disk.py does (below) - derived from the files, so a bigger package or
# an .OVL beside it re-selects the disk instead of overflowing it. Checked
# against the built images: the arithmetic below reproduces --verify's
# 'in use' exactly (353 / 672 / 1,294 with the 24,848-byte package)
GEOMETRY = {360: (1024, 354), 720: (1024, 713), 1440: (512, 2847)}
DIR_ENTRY = 32            # a FAT directory entry
# ROOM TO SAVE IN KB, held back from A/0's fill (SPEC.md 71.5): a disk full to its
# last cluster cannot take a $$$.SUB, an MBASIC program or TE's file, which is
# how wave 4's 360KB disk shipped. The 360KB disk's own CURATION is its guard
# - it stops after the programs and never reaches the budget - so it needs no
# reserve; the 720KB and 1.44MB disks fill until the budget stops them, and
# once the games are priced beside them (SPEC.md 71.6) that is exactly what
# the 720KB one did: 713 of 713 clusters, no room for a single save. The fill
# is RANKED, so what this holds back is the last thing chosen - a source or a
# library, never a program. IN KB and not in clusters, which is the mistake
# this constant was written with: 16 clusters is 16KB on the 720KB disk and
# 8KB on the 1.44MB one, whose clusters are 512 bytes - the geometry with the
# most room got the least. The 1.44MB disk holds back 64KB because it is the
# one a session actually works on (SPEC.md 71.6: WordStar and Turbo Pascal
# are on it, and both write files).
SAVE_ROOM_KB = {360: 0, 720: 16, 1440: 64}
# ASSOC.DAT, the icon/association cache os88disk.py writes in the root beside
# the packages (SPEC.md 54.7), is priced by asking os88disk.py itself
# (assoc_clusters below): one cluster on the RUNCPM disks, whose only package
# is RUNCPM.O88, four on apps-all's 22. The disk's OTHER folder directories -
# the folder A above A/0 (one cluster: '.', '..' and '0') on the RUNCPM
# disks, ten folders on apps-all - are --folders, priced a cluster each


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
    text = left_off_text(out, kept, left)
    write_if_changed(os.path.join(out, "A", "0", "LEFT-OFF.TXT"), text.encode())
    kept.append(("LEFT-OFF.TXT", len(text)))
    kept.sort()
    listing = "".join(f"{base} {size}\n" for base, size in kept)
    write_if_changed(os.path.join(out, "A0.list"), listing.encode())
    # what --select's per-geometry note needs without re-reading the zip
    write_if_changed(os.path.join(out, "left-off.list"),
                     "".join(f"{b} {s}\n" for b, s in left).encode())
    return kept, left


def left_off_text(out, kept, left, geometry=None, by_policy=(), no_room=()):
    """The disk says what is not on it, in its own terms (a CP/M user reads
    this with TYPE and has no SPEC.md): the files above 65,535 bytes, every
    submit file whose source this disk lacks and - for a geometry that does
    not carry the whole master disk - what it leaves off (`by_policy`: the
    curation, `no_room`: the budget) and why."""
    text = ("Files of RunCPM's master disk that are NOT on this disk:\r\n"
            "this port reads and writes whole files and cannot open one\r\n"
            "larger than 65535 bytes, so these are left off.\r\n")
    for base, size in left:
        text += f"  {base:<12} {size:>7} bytes\r\n"
    # ...and the consequence, derived from the disk itself: a submit file
    # whose command names a SOURCE that is not on this disk by its stem
    # (`MAC BDOS` assembles BDOS.ASM, `Z80ASM ABDOS/H` assembles ABDOS.Z80)
    # will not run here - whether the source is above the size limit or the
    # geometry left it off. The stem is matched as an OPERAND only -
    # Z80ASM.PDF's stem is also the assembler's name, and `Z80ASM INFO` runs
    # fine (as of the pin: BDOS.SUB and ZCPR3.SUB on 720KB/1.44MB, eleven of
    # the twelve on the curated 360KB disk - only CLEAN.SUB runs there)
    sources = {}
    for base, _ in list(left) + list(by_policy) + list(no_room):
        stem, ext = os.path.splitext(base)
        if ext in (".ASM", ".Z80", ".MAC"):
            sources[stem] = base
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
                w = w.split("/")[0].upper()          # ABDOS/H: the option
                if w in sources:
                    hit = sources[w]
                    break
            if hit:
                break
        if hit:
            broken.append((base, hit))
    if broken:
        text += "\r\n"
        for sub, src in broken:
            text += f"{sub} assembles {src} and will not run on this disk.\r\n"
    if by_policy:
        text += (f"\r\nThis is the {geometry}KB disk: it carries the programs and\r\n"
                 "the texts and leaves room to save. The rest of the master\r\n"
                 "disk - sources, libraries, documentation and the ABDOS\r\n"
                 "images - is on the 720KB and 1.44MB disks:\r\n")
        for base, size in by_policy:
            text += f"  {base:<12} {size:>7} bytes\r\n"
    if no_room:
        text += (f"\r\nThe {geometry}KB disk has no room for these master-disk files:\r\n")
        for base, size in no_room:
            text += f"  {base:<12} {size:>7} bytes\r\n"
    text += ("\r\n1STREAD.ME (RunCPM's own) credits the programs beyond\r\n"
             "Digital Research's distribution.\r\n")
    return text


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


def assoc_clusters(reserve, cbytes):
    """What ASSOC.DAT costs on a disk carrying the packages among `reserve`
    - os88disk.py's own build_assoc, on the same bytes, so the two cannot
    disagree (16 bytes + 80 a package + 4 an association; SPEC.md 54.7)."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import os88disk
    group = []
    for path in reserve:
        stem, ext = os.path.splitext(os.path.basename(path))
        if ext.upper() != ".O88":
            continue
        with open(path, "rb") as fh:
            body = fh.read()
        group.append((stem.upper().encode().ljust(8)[:8] + b"O88", body, 0))
    body, _ = os88disk.build_assoc({"": group})
    return clusters(len(body), cbytes) if body else 0


def select(out, geometry, reserve, slots=0, folders=1, reserve_clusters=0):
    """The A/0 files a geometry carries, in fill order, beside the root
    files `reserve` names (paths; their sizes are priced here) and the
    `folders` other folder directories on the disk (a cluster each) and
    `reserve_clusters` more that something else on the disk has already spent
    - the GAMES (tools/getcpmsw.py --cost, SPEC.md 71.6), which are priced
    FIRST because an area is carried whole or not at all while A/0's fill is
    ranked and shrinks by itself; A/0's own
    directory is priced for at least `slots` entries - the spare ones the
    disk rule asks os88disk.py for with --dir-slots (the kernel does not
    grow a directory, SPEC.md 18.5, so a CP/M session's saves need room
    that ships with the disk, SPEC.md 74.3). Answers (paths, used, budget):
    the paths are A/0's files, except that LEFT-OFF.TXT is the GEOMETRY'S
    OWN - written to <out>/left-off/<KB>/LEFT-OFF.TXT, naming what this
    disk leaves off - and is priced at its real size."""
    if geometry not in GEOMETRY:
        fail(f"--select wants 360, 720 or 1440, not {geometry}")
    cbytes, total = GEOMETRY[geometry]
    rows = read_list(out)
    kept = [(b, s) for b, s in rows if b != "LEFT-OFF.TXT"]
    left = []
    p = os.path.join(out, "left-off.list")
    if not os.path.exists(p):               # a cache an older script unpacked
        fail(f"{p} missing - run tools/getruncpm.py -o {out} first")
    with open(p) as fh:
        left = [(b, int(s)) for b, s in (l.split() for l in fh)]
    root = 0
    for path in reserve:
        try:
            root += clusters(os.path.getsize(path), cbytes)
        except OSError as exc:
            fail(f"--reserve {path}: {exc}")
    budget = total - root - assoc_clusters(reserve, cbytes) - folders \
             - reserve_clusters - clusters(SAVE_ROOM_KB[geometry] * 1024, cbytes)
    carried = CURATED.get(geometry)         # None: everything, ranked

    def rank(row):
        base = row[0]
        ext = os.path.splitext(base)[1]
        c = CATEGORY.index(ext) if ext in CATEGORY else len(CATEGORY)
        return (c, base)

    def dir_clusters(nfiles):
        return clusters(max(2 + nfiles, slots) * DIR_ENTRY, cbytes)   # '.',
                                                            # '..', files

    # LEFT-OFF.TXT is chosen first and priced at the size it will have,
    # which depends on what is left off - so choose, write, and if the note
    # grew past its allowance, choose again with the allowance raised
    # (converges in a pass or two: the note is a few hundred bytes)
    note_clusters = 1
    while True:
        used, chosen = note_clusters, ["LEFT-OFF.TXT"]
        by_policy, no_room = [], []      # the two reasons a file is dropped
        for base, size in sorted(kept, key=rank):
            n = clusters(size, cbytes)
            ext = os.path.splitext(base)[1]
            if carried is not None and ext not in carried:
                by_policy.append((base, size))
                continue
            if used + n + dir_clusters(len(chosen) + 1) > budget:
                no_room.append((base, size))
                continue
            used += n
            chosen.append(base)
        used += dir_clusters(len(chosen))
        by_policy.sort()
        no_room.sort()
        text = left_off_text(out, [(b, 0) for b in chosen], left, geometry,
                             by_policy, no_room)
        if clusters(len(text), cbytes) <= note_clusters:
            break
        note_clusters = clusters(len(text), cbytes)
    note = os.path.join(out, "left-off", str(geometry), "LEFT-OFF.TXT")
    write_if_changed(note, text.encode())
    paths = [note] + [os.path.join(out, "A", "0", b) for b in chosen[1:]]
    return paths, used, budget


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
    ap.add_argument("--dir-slots", type=int, default=0, metavar="N",
                    help="with --select: price A/0's directory for at least N entries "
                         "(what os88disk.py --dir-slots A/0=N will build)")
    ap.add_argument("--reserve-clusters", type=int, default=0, metavar="N",
                    help="with --select: clusters already spent on this disk by "
                         "something else (the games: tools/getcpmsw.py --cost)")
    ap.add_argument("--folders", type=int, default=1, metavar="N",
                    help="with --select: how many OTHER folder directories the disk "
                         "has, priced a cluster each (default 1: the folder A "
                         "above A/0; apps-all has ten)")
    ap.add_argument("--from", dest="src", metavar="DIR",
                    help="a local checkout of RunCPM at the pinned commit to take the files from")
    args = ap.parse_args()

    if args.select:
        chosen, used, budget = select(args.output, args.select, args.reserve,
                                      args.dir_slots, args.folders,
                                      args.reserve_clusters)
        for path in chosen:
            print(path)
        print(f"getruncpm: {len(chosen)} files, {used} of {budget} clusters "
              f"for A/0 on the {args.select}KB disk (after {len(args.reserve)} "
              f"root files"
              f"{f' and {args.reserve_clusters} clusters of games' if args.reserve_clusters else ''})",
              file=sys.stderr)
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
