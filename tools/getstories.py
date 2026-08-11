#!/usr/bin/env python3
"""getstories: fetch the Z-machine story files the Frotz disk ships.

    python3 tools/getstories.py -o build/stories          # fetch + verify
    python3 tools/getstories.py -o build/stories --list    # print the manifest
    python3 tools/getstories.py -o build/stories --check   # verify, never fetch

**Nothing this script downloads is committed.** Every story below is someone
else's work under someone else's copyright, and a git repository is not a
distribution channel for it - so the bytes land in build/, which is ignored
outright (see the note above `clean` in the Makefile), and the disk images are
built from there. That is the same decision the tree already made for
build/big.dat and build/click.mod, taken for a stronger reason.

The three Infocom-authored entries are here because Infocom and later
Activision gave them away on purpose - two cover-disk/demo releases and one
1997 freeware release - and the IF Archive has hosted them openly for decades.
Everything else is its author's own freeware or open release. **The games
Infocom sold and Activision still owns - Zork I/II/III, The Hitchhiker's Guide
to the Galaxy, Planetfall, Enchanter - are not here and cannot be**, which is
what the STORIES= knob in the Makefile is for: point it at files you own and
they land on the disk beside these, unmodified.

Two entries are not a plain download:

  ZTUU.Z5    arrives inside a .zip, one member of several (the rest is a map
             and a walkthrough), so it is extracted by member name.
  BRONZE.Z8  arrives as a Blorb (SS 59.7) - an IFF container holding the
             Z-code AND a JPEG cover. Pulling the ZCOD chunk out drops it from
             492KB to 359KB, which is the difference between fitting a 640KB
             machine's heap and not; tools/os88pix.py takes the same file's
             picture chunks the other way, into a .PIX.

Each entry pins the SHA-256 of the ARTIFACT (what the archive serves) and of
the RESULT (what lands in the output directory). Both are checked. A hash
mismatch is a hard failure and never a warning: the disk images are meant to
rebuild byte-for-byte (tools/os88disk.py pins the volume serial and every FAT
timestamp), and a story file that quietly changed under us would break that
silently.
"""
import argparse
import hashlib
import io
import os
import struct
import sys
import urllib.error
import urllib.request
import zipfile

ARCHIVE = "https://ifarchive.org/if-archive/"
TIMEOUT = 120


class Story:
    """One shippable story file.

    name    the 8.3 name it takes on the disk (tools/os88disk.py uppercases
            the basename and hard-fails anything that is not already 8.3, so
            these are pre-shortened here rather than truncated there)
    folder  which directory of the Frotz disk it belongs in
    url     path under the IF Archive root
    member  zip member to extract, or None for a plain file
    blorb   True to pull the ZCOD chunk out of an IFF Blorb container
    """

    def __init__(self, name, folder, url, artifact_sha, result_sha, size,
                 zver, title, author, note, member=None, blorb=False):
        self.name = name
        self.folder = folder
        self.url = url
        self.artifact_sha = artifact_sha
        self.result_sha = result_sha
        self.size = size
        self.zver = zver
        self.title = title
        self.author = author
        self.note = note
        self.member = member
        self.blorb = blorb


# The manifest. `size` is the size of the RESULT, and it is what the Makefile
# budgets a geometry against - a 360KB disk holds 354 clusters of 1KB and the
# interpreter takes some of them, so the per-geometry lists there are chosen
# against these numbers and checked at build time rather than guessed.
MANIFEST = [
    # --- INFOCOM: given away by Infocom or Activision, hosted openly since ---
    Story("MINIZORK.Z3", "INFOCOM", "infocom/demos/minizork.z3",
          "c74f01a232e8df4b05d7ebcba14870143f49b3c9a25f194f7a7d2c69e31ea4a6",
          "c74f01a232e8df4b05d7ebcba14870143f49b3c9a25f194f7a7d2c69e31ea4a6",
          52216, 3, "Mini-Zork I", "Infocom",
          "Cut-down Zork I, given away on a UK magazine cover disk."),
    Story("SAMPLER1.Z3", "INFOCOM", "infocom/demos/sampler1_R55.z3",
          "7a2e5b7698f42a83e73dab5d9c5fbead294cf7b407f6daa8fe57caa429bd35bf",
          "7a2e5b7698f42a83e73dab5d9c5fbead294cf7b407f6daa8fe57caa429bd35bf",
          126902, 3, "Infocom Sampler I", "Infocom",
          "Free demo disk: the openings of Zork I, Planetfall and Infidel."),
    Story("SAMPLER2.Z3", "INFOCOM", "infocom/demos/sampler2.z3",
          "c60b85b61d89a9684cb60436efac92c521bc17f925e1f928bc62086abb1d511b",
          "c60b85b61d89a9684cb60436efac92c521bc17f925e1f928bc62086abb1d511b",
          125315, 3, "Infocom Sampler II", "Infocom",
          "The second free demo disk."),
    Story("ZTUU.Z5", "INFOCOM", "games/zcode/ZTUU.zip",
          "d1a926d289dae1c6b15da55885bec508770b700016a19eeb5c279cd364110dd3",
          "9529cf7545b80c50d9942f58d8374c95ccbd1e196ba3dddf79f5d885d25a8f9f",
          229888, 5, "Zork: The Undiscovered Underground",
          "Michael Berlyn / Marc Blank",
          "Released free by Activision in 1997 to promote Zork Grand Inquisitor.",
          member="ZTUU/ZTUU.z5"),

    # --- CLASSIC: freely distributable ports and Inform's own samples -------
    Story("ADVENT.Z3", "CLASSIC", "games/zcode/advent.z3",
          "03c19b3730f1b46b6e882944a255d1d58e4148e51fcce423dcd96792f476ebfa",
          "03c19b3730f1b46b6e882944a255d1d58e4148e51fcce423dcd96792f476ebfa",
          66414, 3, "Colossal Cave Adventure", "Crowther & Woods, ported by Graham Nelson",
          "The 350-point original, freely distributable."),
    Story("ADVENT5.Z5", "CLASSIC", "games/zcode/Advent.z5",
          "6179b5d5b17d692ec83fe64101ff8e4092390166c2b05063e7310eb976b93ea0",
          "6179b5d5b17d692ec83fe64101ff8e4092390166c2b05063e7310eb976b93ea0",
          138240, 5, "Colossal Cave Adventure (v5)", "ported by Graham Nelson",
          "The same game as a v5 story - the pair is a useful version A/B."),
    Story("ZORK285.Z5", "CLASSIC", "games/zcode/zork_285.z5",
          "59b6248f4c8db9412bda48b9c834f93413c0b6e3f642d0e6f2579e68d3c814ca",
          "59b6248f4c8db9412bda48b9c834f93413c0b6e3f642d0e6f2579e68d3c814ca",
          38600, 5, "Zork: A Troll's Eye View", "Dean Menezes",
          "A port of the 1977 MIT Zork's 285-point version."),
    Story("BALANCES.Z5", "CLASSIC", "games/zcode/Balances.z5",
          "10e717578f85200c6aadc8149d95e43f7b6c09fb473770d309a0d6afe3b29538",
          "10e717578f85200c6aadc8149d95e43f7b6c09fb473770d309a0d6afe3b29538",
          75264, 5, "Balances", "Graham Nelson",
          "Inform's own demonstration game - Enchanter-style spell puzzles."),
    Story("CURSES.Z5", "CLASSIC", "games/zcode/curses.z5",
          "330100bf1c1ab8ed64065ceb58145ebb87cb6faef5b331fbc6684be4f14fe7da",
          "330100bf1c1ab8ed64065ceb58145ebb87cb6faef5b331fbc6684be4f14fe7da",
          259072, 5, "Curses", "Graham Nelson",
          "The game Inform was written to produce."),

    # --- MODERN: the authors' own freeware releases -------------------------
    # Nothing is fetched that does not ship, and nothing ships that cannot
    # RUN. Anchorhead was here and came out again on the second count: 508KB
    # of story plus a 41KB save buffer needs 565KB resident, and the largest
    # claim a 640KB machine can offer is about 501KB once the kernel and the
    # interpreter have taken theirs (SPEC.md 61.4). No real-mode machine has
    # the memory, so shipping it would have shipped a file that only ever
    # produces a refusal. getstories.py can still fetch it for you --
    # `--only ANCHOR.Z8` is gone with the entry, so use STORIES= with your
    # own copy if you want to watch the refusal path work.
    Story("PHOTOPIA.Z5", "MODERN", "games/zcode/photopia.z5",
          "ba14a14dfdede4b7ad97846c13ca0ea07048334ac767649d9aa84add7fae20a5",
          "ba14a14dfdede4b7ad97846c13ca0ea07048334ac767649d9aa84add7fae20a5",
          239616, 5, "Photopia", "Adam Cadre",
          "IF Comp 1998 winner; narrative over puzzles, and it recolours the "
          "screen constantly - a real exercise for the style path."),
    Story("905.Z5", "MODERN", "games/zcode/905.z5",
          "0f41ff7811668e236060b399ced1cd0481c1bb624a8876fc7e16dc58816f1ecb",
          "0f41ff7811668e236060b399ced1cd0481c1bb624a8876fc7e16dc58816f1ecb",
          87040, 5, "9:05", "Adam Cadre", "Short, and famous for its ending."),
    Story("BEAR.Z5", "MODERN", "games/zcode/bear.z5",
          "1df8dd3487a8af6be10a92e84a1225b97eb1f8181af198568d4f884e0bea97fb",
          "1df8dd3487a8af6be10a92e84a1225b97eb1f8181af198568d4f884e0bea97fb",
          109568, 5, "A Bear's Night Out", "David Dyte", "Freeware."),
    Story("LOSTPIG.Z8", "MODERN", "games/zcode/LostPig.z8",
          "7bffe25e933aa22a2b1e9bfc8073bee5687d79a70394da19e50bfed5755368c4",
          "7bffe25e933aa22a2b1e9bfc8073bee5687d79a70394da19e50bfed5755368c4",
          285184, 8, "Lost Pig", "Admiral Jota", "IF Comp 2007 winner."),
    Story("BRONZE.Z8", "MODERN", "games/zcode/Bronze.zblorb",
          "173f8aa40366c809909441cd0296186f13f8c33e5c2323d3ba75b35cda21ffe5",
          "fbbc21f86ff460b14f06a998d202239cd28184c2009cc374b0e1f824d8db9d3b",
          359424, 8, "Bronze", "Emily Short",
          "Beauty and the Beast, and the one bundled game that carries cover "
          "art - the Blorb's JPEG becomes BRONZE.PIX (SS 59.7).",
          blorb=True),
    Story("DREAMHLD.Z8", "MODERN", "games/zcode/dreamhold.z8",
          "399dbfb92024d2a702029dd1921a1023569888941683d953d3755d6856ce2941",
          "399dbfb92024d2a702029dd1921a1023569888941683d953d3755d6856ce2941",
          386560, 8, "The Dreamhold", "Andrew Plotkin",
          "Written as a tutorial game, with a built-in hint system - and the "
          "largest story that still fits a 640KB machine after Bronze."),
]

BY_NAME = {s.name: s for s in MANIFEST}


def fail(msg):
    print(f"getstories: error: {msg}", file=sys.stderr)
    sys.exit(1)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def blorb_zcod(data, path):
    """Pull the Z-code out of an IFF Blorb (SS 59.7).

    FORM....IFRS then a flat run of typed chunks; ZCOD is the story. This is
    deliberately not a general Blorb reader - tools/os88pix.py owns the
    picture side - it just walks the top level and takes the one chunk.
    """
    if data[:4] != b"FORM" or data[8:12] != b"IFRS":
        fail(f"{path}: not a Blorb (want FORM....IFRS)")
    off = 12
    end = min(len(data), 8 + struct.unpack(">I", data[4:8])[0])
    while off + 8 <= end:
        cid = data[off:off + 4]
        ln = struct.unpack(">I", data[off + 4:off + 8])[0]
        body = data[off + 8:off + 8 + ln]
        if cid == b"ZCOD":
            if len(body) != ln:
                fail(f"{path}: ZCOD chunk is truncated")
            return body
        off += 8 + ln + (ln & 1)      # chunks are word-aligned
    fail(f"{path}: no ZCOD chunk - is this a Glulx blorb?")


def extract(story, raw):
    """Turn the downloaded artifact into the bytes that land on the disk."""
    if story.member is not None:
        try:
            zf = zipfile.ZipFile(io.BytesIO(raw))
            return zf.read(story.member)
        except (zipfile.BadZipFile, KeyError) as exc:
            fail(f"{story.name}: cannot read '{story.member}' from the zip: {exc}")
    if story.blorb:
        return blorb_zcod(raw, story.name)
    return raw


def zversion(data):
    """Byte 0 of a story file is its Z-machine version (SS 59.2)."""
    return data[0] if data else 0


def verify_result(story, data):
    """Check a story file we are about to ship, whatever produced it."""
    if len(data) != story.size:
        fail(f"{story.name}: {len(data)} bytes, manifest says {story.size}")
    if story.result_sha is not None and sha256(data) != story.result_sha:
        fail(f"{story.name}: SHA-256 mismatch\n"
             f"  expected {story.result_sha}\n"
             f"  got      {sha256(data)}")
    v = zversion(data)
    if v != story.zver:
        fail(f"{story.name}: header says Z-machine v{v}, manifest says v{story.zver}")


def fetch(url):
    full = ARCHIVE + url
    req = urllib.request.Request(full, headers={"User-Agent": "os8088-getstories/1"})
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT) as fh:
            return fh.read()
    except (urllib.error.URLError, OSError) as exc:
        fail(f"cannot fetch {full}: {exc}\n"
             f"  The Frotz disk needs the network once; after that build/stories\n"
             f"  is a cache and nothing is downloaded again.")


def write_catalog(path, only):
    """The disk's own CATALOG.TXT: what each story is and who wrote it.

    A floppy that arrives with fifteen 8.3 names on it and no explanation is a
    floppy you have to look things up about, so the catalogue ships beside
    them. Note Pad word-wraps prose (SPEC.md 27.11), so paragraphs run on and
    only the SHAPED lines - the headings and the rules - are hand-wrapped.
    CRLF, because a .TXT on a FAT floppy is expected to have it and these
    disks are meant to be readable on a DOS PC (SPEC.md 19).
    """
    picked = [s for s in MANIFEST if not only or s.name in only]
    out = []
    out.append("FROTZ STORY DISK")
    out.append("=" * 28)
    out.append("")
    out.append("Open one with File > Open Story. Every story here was released "
               "freely by the people who made it - see the notes below.")
    out.append("")
    for folder in ("INFOCOM", "CLASSIC", "MODERN"):
        rows = [s for s in picked if s.folder == folder]
        if not rows:
            continue
        out.append("")
        out.append(folder)
        out.append("-" * 28)
        for s in rows:
            out.append("")
            out.append(f"  {s.name}")
            out.append(f"  {s.title}")
            out.append(f"  {s.author}, v{s.zver}, {s.size // 1024}KB")
            out.append("")
            out.append(s.note)
    out.append("")
    out.append("")
    out.append("MEMORY")
    out.append("-" * 28)
    out.append("")
    out.append("Frotz keeps the whole story in RAM - there is no paging, because "
               "a floppy seek costs about 400ms on this machine and one turn "
               "would take minutes. So a story needs about its own size free, "
               "and Frotz says so and stops rather than half-loading one. On a "
               "640KB machine everything here fits. On a 256KB one, the v3 "
               "stories do.")
    out.append("")
    out.append("")
    out.append("NOT HERE")
    out.append("-" * 28)
    out.append("")
    out.append("Zork I, II and III, The Hitchhiker's Guide to the Galaxy, "
               "Planetfall and Enchanter are still Activision's and are not "
               "ours to give away. Frotz plays them: put your own copies on "
               "this disk and open them like any other.")
    out.append("")
    body = "\r\n".join(out) + "\r\n"
    with open(path, "wb") as fh:
        fh.write(body.encode("ascii", "replace"))
    print(f"getstories: catalogue for {len(picked)} stories -> {path} "
          f"({len(body)} bytes)")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-o", "--output", metavar="DIR", default="build/stories",
                    help="where the story files land (a cache; default build/stories)")
    ap.add_argument("--list", action="store_true", help="print the manifest and exit")
    ap.add_argument("--check", action="store_true",
                    help="verify what is already there; never download")
    ap.add_argument("--catalog", metavar="FILE",
                    help="write the disk's CATALOG.TXT (CRLF, 28 columns) and "
                         "exit; no network, no story files needed")
    ap.add_argument("only", nargs="*", metavar="NAME",
                    help="fetch only these 8.3 names (default: all of them)")
    args = ap.parse_args()

    if args.list:
        w = max(len(s.name) for s in MANIFEST)
        for s in MANIFEST:
            print(f"{s.folder + '/' + s.name:<{w + 9}} v{s.zver}  {s.size:>7}  "
                  f"{s.title} - {s.author}")
            print(f"{'':<{w + 9}}         {s.note}")
        total = sum(s.size for s in MANIFEST)
        print(f"\n{len(MANIFEST)} stories, {total} bytes "
              f"({total / 1024:.0f}KB) - more than any one floppy holds, which "
              f"is why the Makefile picks a subset per geometry.")
        return

    if args.catalog:
        write_catalog(args.catalog, args.only)
        return

    want = MANIFEST
    if args.only:
        for n in args.only:
            if n not in BY_NAME:
                fail(f"no story named {n}; --list shows the manifest")
        want = [BY_NAME[n] for n in args.only]

    os.makedirs(args.output, exist_ok=True)
    cache = os.path.join(args.output, ".artifacts")
    os.makedirs(cache, exist_ok=True)

    fetched = cached = 0
    for s in want:
        out = os.path.join(args.output, s.name)
        if os.path.exists(out):
            with open(out, "rb") as fh:
                data = fh.read()
            verify_result(s, data)
            cached += 1
            continue
        if args.check:
            fail(f"{s.name} is missing from {args.output} (--check does not fetch)")

        # The artifact cache is keyed by the archive path, so a re-extraction
        # (a changed member name, a Blorb fix) costs no second download.
        art = os.path.join(cache, os.path.basename(s.url))
        if os.path.exists(art):
            with open(art, "rb") as fh:
                raw = fh.read()
        else:
            raw = None
        if raw is None or sha256(raw) != s.artifact_sha:
            print(f"getstories: fetching {s.url}")
            raw = fetch(s.url)
            got = sha256(raw)
            if got != s.artifact_sha:
                fail(f"{s.url}: artifact SHA-256 mismatch\n"
                     f"  expected {s.artifact_sha}\n"
                     f"  got      {got}\n"
                     f"  The archive's copy changed. Check it by hand before "
                     f"updating the manifest.")
            with open(art, "wb") as fh:
                fh.write(raw)

        data = extract(s, raw)
        verify_result(s, data)
        with open(out, "wb") as fh:
            fh.write(data)
        fetched += 1

    total = sum(s.size for s in want)
    print(f"getstories: {len(want)} stories in {args.output} "
          f"({fetched} fetched, {cached} cached), {total} bytes")


if __name__ == "__main__":
    main()
