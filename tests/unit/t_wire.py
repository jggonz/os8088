#!/usr/bin/env python3
"""The Wire's catalog and its archives: round trips, refusals, and the mirror.

    python3 tests/unit/t_wire.py

THREE THINGS, and the third is the one that cannot be got by reading either
file on its own. Each is done twice, once for the catalog (SPEC.md 88.2) and
once for the archive (SPEC.md 88.13) - and the archive's round trip carries a
FOURTH reading, because 88.13 pins its compression BY ITS DECODER: the tool's
encoder, the tool's reference decoder and a decoder written here from the
paragraph alone must all agree, and the 8088's resumable unpacker will be the
third of those to be written.

1. **THE ROUND TRIP.** A fixture manifest naming `build/hello.o88`,
   `build/mines.o88` and a tier-3 entry with a sidecar is packed by
   `tools/os88wire.py --pack`, verified by `--verify`, dumped by `--dump`,
   and then read back HERE by a reader written from SPEC.md 88.2 rather than
   by importing the tool's own. Two readings of one format; where they
   disagree the spec has a bug and the spec is fixed first. That is
   `tests/unit/t_wab.py`'s rule (WEAVE-SPEC 12.2), one format along.

2. **THE REFUSALS.** Every rule in SPEC.md 88.2 that the WRITER owns is fed
   an input that breaks it, and the pack must refuse. A writer whose refusals
   are untested is a writer that emits a catalog the 8088 then refuses - and
   the machine's refusal is one line in a status cell, where this one names
   the entry, the field and the limit.

3b. **AND THE GENERIC ICON IS THE KERNEL'S.** `tools/os88wire.py` gives a
   record whose package declares no `OS88_ICON16` the kernel's own
   `ico_app16` — what the Disk window already draws for that package — and it
   is baked in rather than parsed, because the website's packer runs in a
   checkout with no kernel in it. So the two copies are compared here, against
   `kernel/icons.inc`'s own `dw` rows.

3. **THE MIRROR.** Every `WC_*`, `WIRE_*`, `WF_*` and `WK_*` equ in
   `apps/thewire/wcat.inc` is compared against the same name in
   `tools/os88wire.py`. There is no linker in this tree, so a format that
   lives on both sides of a wire is a number typed out twice; this is
   `tests/unit/t_mirror.py`'s arrangement for a pair of files it does not
   cover, and it is SELF-MAINTAINING in the same way - nothing below
   enumerates which constants are mirrored, it takes every name defined in
   both and checks that they agree. A constant that becomes mirrored
   tomorrow is covered tomorrow.

Host-side and fast: it reads what `make` just built and shells out to the
tool. No emulator, no disk image.
"""
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, eq, done                       # noqa: E402

TOOL = os.path.join(ROOT, "tools", "os88wire.py")
WCAT = os.path.join(ROOT, "apps", "thewire", "wcat.inc")
WARC = os.path.join(ROOT, "apps", "thewire", "warc.inc")
ICONS = os.path.join(ROOT, "kernel", "icons.inc")
BUILD = os.path.join(ROOT, "build")

# --- SPEC.md 88.2, read out of the spec and not out of the tool -------------
S_HDR = 32
S_REC = 256
S_SC = 16
S_MAGIC = b"WIRE"
S_VER = 1
S_CATMAX = 16384
S_FILEMAX = 64512
S_DESCN, S_DESCW = 5, 28
S_WF_DISK, S_WF_PIC, S_WF_NEW, S_WF_FLOPPY, S_WF_ARC = 1, 2, 4, 8, 16
S_ARCMAX = 0x100000                 # WIRE_ARCMAX, the archive's own ceiling

# --- SPEC.md 88.13, the same way ---------------------------------------------
S_AMAGIC = b"WPAK"
S_AHDR = 32
S_AENT = 64
S_ASLOT = 12
S_ASLOTS = 4
S_ADEPTH = 3
S_ANMAX = 255
S_DISTMAX, S_LENMIN, S_LENMAX = 4096, 3, 18


def run(*args):
    return subprocess.run([sys.executable, TOOL] + list(args), cwd=ROOT,
                          capture_output=True, text=True)


def tool():
    """os88wire.py as a module, for the reference decoder's own refusals."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("os88wire", TOOL)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# =============================================================================
# 3. THE MIRROR
# =============================================================================
def mirror(inc, least, pins, sect):
    """Every name defined in BOTH files must say the same number."""
    asm, py = {}, {}
    pat = re.compile(r"^\s*([A-Z][A-Z0-9_]*)\s+equ\s+([^;]+?)\s*(?:;.*)?$")
    for line in open(inc):
        m = pat.match(line)
        if not m:
            continue
        name, expr = m.group(1), m.group(2).strip()
        # The only expression forms these files use are a literal and one
        # division of two literals (WIRE_REC / 16). Anything else is new and
        # should be looked at rather than guessed at.
        try:
            asm[name] = int(eval(expr, {"__builtins__": {}},
                                 dict(asm)))          # noqa: S307
        except Exception:
            continue
    src = open(TOOL).read()
    for m in re.finditer(r"^([A-Z][A-Z0-9_]*)\s*=\s*(\d+)\s*$", src, re.M):
        py[m.group(1)] = int(m.group(2))

    tag = os.path.basename(inc)
    shared = sorted(set(asm) & set(py))
    check(len(shared) >= least,
          "%s and os88wire.py share %d constants" % (tag, len(shared)),
          "the mirror is self-maintaining, so a low count means the SCAN "
          "broke (a renamed equ, a reformatted assignment) rather than that "
          "the format got smaller - and a scan that finds nothing passes",
          got=len(shared), want=">= %d" % least)
    for n in shared:
        eq(py[n], asm[n],
           "%s: apps/thewire/%s says %d, tools/os88wire.py says %d"
           % (n, tag, asm[n], py[n]),
           "the format lives on both sides of a wire and there is no linker: "
           "a half-applied change assembles and packs perfectly, and the "
           "machine then reads a record at the wrong offset (SPEC.md %s)"
           % sect)

    # ...and both must carry the whole of what the SPEC pins by number.
    for n, want in pins:
        eq(asm.get(n), want,
           "%s's %s is %s and SPEC.md %s says %d"
           % (tag, n, asm.get(n), sect, want),
           "both files could agree on a number the SPEC does not say")


# What SPEC.md 88.2 and 88.13 pin BY NUMBER, read out of the prose rather than
# out of either file - the mirror above only proves the two spellings agree.
CAT_PINS = (("WIRE_HDR", S_HDR), ("WIRE_REC", S_REC),
            ("WIRE_SC", S_SC), ("WIRE_VER", S_VER),
            ("WIRE_CATMAX", S_CATMAX), ("WIRE_FILEMAX", S_FILEMAX),
            ("WC_DESCN", S_DESCN), ("WC_DESCW", S_DESCW),
            ("WF_DISK", S_WF_DISK), ("WF_PIC", S_WF_PIC),
            ("WF_NEW", S_WF_NEW), ("WF_FLOPPY", S_WF_FLOPPY),
            ("WF_ARC", S_WF_ARC), ("WC_NEEDKB", 46),
            ("WIRE_ARCMAX", S_ARCMAX))
ARC_PINS = (("WARC_HDR", S_AHDR), ("WARC_ENT", S_AENT),
            ("WARC_SLOT", S_ASLOT), ("WARC_SLOTS", S_ASLOTS),
            ("WARC_DEPTH", S_ADEPTH), ("WARC_NMAX", S_ANMAX),
            ("WARC_VER", 1), ("WAH_PROGRAM", 1),
            ("WAM_STORED", 0), ("WAM_LZSS", 1),
            ("WARC_DISTMAX", S_DISTMAX), ("WARC_LENMIN", S_LENMIN),
            ("WARC_LENMAX", S_LENMAX), ("WA_TOTAL", 8), ("WA_NEEDKB", 12),
            ("WA_MAXENT", 14), ("WA_HOME", 16), ("WA_PATH", 16))


def icons():
    """os88wire.py's generic icon IS the kernel's `ico_app16`.

    That icon is what the Disk window draws for a package with no
    OS88_ICON16, so a Wire row showing something else would put one program
    under two pictures. The tool cannot READ kernel/icons.inc - the website's
    packer runs in a checkout with no kernel beside it - so the rows are baked
    in, and this is what stops the two copies drifting.
    """
    src = open(ICONS).read()
    i = src.find("\nico_app16:")
    body = src[i + 1:] if i >= 0 else ""
    nxt = body.find("\nico_", 1)                # the next icon in the file
    if nxt > 0:
        body = body[:nxt]
    rows = [int(m.group(1), 16)
            for m in re.finditer(r"^\s*dw\s+(0x[0-9A-Fa-f]{4})", body, re.M)]
    if not check(len(rows) == 32,
                 "ico_app16 has 32 dw rows in kernel/icons.inc",
                 "the scan found something else, so the comparison below is "
                 "against nothing - and a scan that finds nothing passes",
                 got=len(rows), want=32):
        return
    import importlib.util
    spec = importlib.util.spec_from_file_location("os88wire", TOOL)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    eq(mod.GENERIC_ICON_MASK, rows[:16],
       "os88wire.py's GENERIC_ICON_MASK is ico_app16's mask",
       "a record whose package declares no icon gets this one, and the Disk "
       "window draws ico_app16 for that same package - one program under two "
       "pictures is the drift (SPEC.md 88.2)")
    eq(mod.GENERIC_ICON_DATA, rows[16:],
       "os88wire.py's GENERIC_ICON_DATA is ico_app16's data",
       "as above, and the tool cannot read kernel/icons.inc because the "
       "website's packer runs in a checkout with no kernel in it")


# =============================================================================
# 1. THE ROUND TRIP - a reader written from the spec
# =============================================================================
def read_catalog(blob):
    """SPEC.md 88.2, by hand. Raises on anything the machine would refuse."""
    assert len(blob) >= S_HDR, "shorter than the header"
    assert len(blob) <= S_CATMAX, "over WIRE_CATMAX"
    assert blob[0:4] == S_MAGIC, "magic %r" % blob[0:4]
    assert blob[4] == S_VER, "version %d" % blob[4]
    assert blob[5] == S_REC // 16, "record size %d" % blob[5]
    n = struct.unpack_from("<H", blob, 6)[0]
    assert struct.unpack_from("<H", blob, 8)[0] == S_HDR, "header size"
    scoff = struct.unpack_from("<H", blob, 10)[0]
    scn = struct.unpack_from("<H", blob, 12)[0]
    date = blob[14:22].decode("ascii")
    assert blob[22:32] == b"\0" * 10, "header tail is not zero"
    assert 1 <= n <= 255, "record count %d" % n
    assert S_HDR + n * S_REC <= len(blob), "records run past the end"
    assert scoff + scn * S_SC <= len(blob), "sidecar table runs past the end"

    sides = []
    for j in range(scn):
        e = scoff + j * S_SC
        nm = blob[e:e + 12]
        assert b"\0" in nm, "sidecar %d has no NUL" % j
        sides.append((nm.split(b"\0")[0].decode("ascii"),
                      struct.unpack_from("<I", blob, e + 12)[0]))

    recs = []
    for i in range(n):
        o = S_HDR + i * S_REC
        r = {"stem": blob[o:o + 8].rstrip(b" ").decode("ascii"),
             "title": blob[o + 8:o + 32].split(b"\0")[0].decode("ascii"),
             "kind": blob[o + 32], "tier": blob[o + 33],
             "flags": blob[o + 34], "nside": blob[o + 35],
             "side0": struct.unpack_from("<H", blob, o + 36)[0],
             "size": struct.unpack_from("<I", blob, o + 38)[0],
             "total": struct.unpack_from("<I", blob, o + 42)[0],
             "needkb": struct.unpack_from("<H", blob, o + 46)[0],
             "icon": blob[o + 48:o + 112]}
        # +46 WAS the record's last spare word and is now WC_NEEDKB (SPEC.md
        # 88.2): an archive's RAM disk figure, and zero on everything else.
        if not r["flags"] & S_WF_ARC:
            assert r["needkb"] == 0, "%s: WC_NEEDKB is not zero on a record "\
                "that is not an archive" % r["stem"]
        assert blob[o + 252:o + 256] == b"\0" * 4, "%s: +252" % r["stem"]
        assert r["tier"] <= 3, "%s: tier %d" % (r["stem"], r["tier"])
        assert r["nside"] <= 8, "%s: %d sidecars" % (r["stem"], r["nside"])
        assert r["side0"] + r["nside"] <= scn, "%s: sidecar range" % r["stem"]
        # An ARCHIVE is the exception: WC_SIZE is the .WPK on the wire and
        # WC_TOTAL what it unpacks to, and a tree of small files carries 96
        # bytes of container per entry - so the packed file can be the larger.
        assert r["total"] >= r["size"] or r["flags"] & S_WF_ARC, \
            "%s: total < size" % r["stem"]
        d = []
        for k in range(S_DESCN):
            f = blob[o + 112 + k * S_DESCW:o + 112 + (k + 1) * S_DESCW]
            assert b"\0" in f, "%s: description line %d has no NUL" % (
                r["stem"], k)
            t = f.split(b"\0")[0].decode("ascii")
            assert all(0x20 <= c <= 0x7E for c in f.split(b"\0")[0]), \
                "%s: description line %d is not printable ASCII" % (
                    r["stem"], k)
            if t:
                d.append(t)
        r["desc"] = d
        r["sides"] = sides[r["side0"]:r["side0"] + r["nside"]]
        recs.append(r)
    return date, recs, sides


FIXTURE = {
    "date": "20260904",
    "entries": [
        {"stem": "HELLO", "title": "Hello", "kind": 0, "tier": 0,
         "flags": {"new": True}, "files": ["hello.o88"],
         "description": "The smallest os8088 package. One window, two lines "
                        "of text and a File menu."},
        {"stem": "MINES", "title": "Minesweeper", "kind": "game", "tier": 0,
         "files": ["mines.o88"],
         "description": "The 1990 game, in assembly."},
        {"stem": "BIGONE", "title": "A 486 program", "kind": 0, "tier": 3,
         "files": ["hello.o88", "mines.o88"],
         "description": "Two files, so it needs a disk."},
    ],
}


def roundtrip(tmp):
    man = os.path.join(tmp, "wire.json")
    cat = os.path.join(tmp, "catalog.bin")
    json.dump(FIXTURE, open(man, "w"))
    r = run("--pack", man, "--pkgdir", BUILD, "--out", cat)
    if r.returncode:
        check(False, "--pack refused the fixture", "the round trip cannot run",
              got=(r.stdout + r.stderr).strip())
        return
    # --pkgdir is the PUBLISHED /wire/pkg/, where a file is named <STEM>.O88
    # (that is the URL the machine composes), so it is staged rather than
    # pointed at the build tree. Staging it is also what puts the icon
    # cross-check under test.
    pub = os.path.join(tmp, "pkg")
    os.makedirs(pub, exist_ok=True)
    for stem, src in (("HELLO", "hello.o88"), ("MINES", "mines.o88"),
                      ("BIGONE", "hello.o88")):
        open(os.path.join(pub, stem + ".O88"), "wb").write(
            open(os.path.join(BUILD, src), "rb").read())
    r = run("--verify", cat, "--pkgdir", pub)
    check(r.returncode == 0, "--verify on the tool's own output",
          "a packer and a verifier that never meet is two readings of one "
          "format", got=(r.stdout + r.stderr).strip(), want="exit 0")
    r = run("--dump", cat)
    check(r.returncode == 0, "--dump ran",
          got=(r.stdout + r.stderr).strip(), want="exit 0")
    for want in ("HELLO", "Minesweeper", "486+", "mines.o88".upper()):
        check(want in r.stdout, "--dump names %s" % want,
              "a dump nobody can read against the manifest is not a check",
              got=r.stdout)

    blob = open(cat, "rb").read()
    try:
        date, recs, sides = read_catalog(blob)
    except Exception as e:
        check(False, "the spec-side reader refused the packed catalog",
              "SPEC.md 88.2 and tools/os88wire.py disagree; the SPEC is fixed "
              "first", got="%s: %s" % (type(e).__name__, e))
        return

    eq(date, "20260904", "the catalog date survives the round trip")
    eq(len(recs), 3, "three records")
    eq([r["stem"] for r in recs], ["HELLO", "MINES", "BIGONE"],
       "the records are in manifest order",
       "the filter lists in CATALOG ORDER (SPEC.md 88.6), so the order is "
       "part of the format and not an implementation detail")
    eq(recs[0]["title"], "Hello", "the title")
    eq(recs[1]["kind"], 1, "a named kind becomes its number")
    eq(recs[2]["tier"], 3, "the tier")
    check(recs[0]["flags"] & S_WF_NEW, "HELLO is WF_NEW",
          "the site marks its newest and the row draws a NEW tag from it")
    check(not recs[1]["flags"] & S_WF_NEW, "MINES is not WF_NEW")
    check(recs[2]["flags"] & S_WF_DISK,
          "a record with a sidecar is WF_DISK",
          "WF_DISK is DERIVED and not declared (SPEC.md 88.2): a sidecar "
          "without it would let Load Program run a program with its files "
          "nowhere, which is the one thing the flag exists to stop")
    check(not recs[0]["flags"] & S_WF_DISK, "one file is not WF_DISK")
    check(not any(r["flags"] & S_WF_PIC for r in recs),
          "no WF_PIC without a --picdir")

    hello = open(os.path.join(BUILD, "hello.o88"), "rb").read()
    mines = open(os.path.join(BUILD, "mines.o88"), "rb").read()
    eq(recs[0]["size"], len(hello), "WC_SIZE is the file's own length")
    eq(recs[2]["total"], len(hello) + len(mines),
       "WC_TOTAL is the .O88 plus every sidecar",
       "Add to Disk refuses on it before touching the disk (SPEC.md 88.7)")
    eq(recs[2]["sides"], [("MINES.O88", len(mines))],
       "the sidecar table names the second file and its size")
    eq(recs[0]["desc"],
       ["The smallest os8088", "package. One window, two",
        "lines of text and a File", "menu."],
       "the description is PRE-WRAPPED at 27 columns",
       "the machine wraps nothing (SPEC.md 88.2) - it is five fixed fields "
       "exactly so that a paint costs no wrapping")
    for r in recs:
        for line in r["desc"]:
            check(len(line) <= S_DESCW - 1,
                  "%s: '%s' is %d columns" % (r["stem"], line, len(line)),
                  "a line over 27 has no NUL inside its 28 and the machine "
                  "reads into the next line", got=len(line), want=S_DESCW - 1)

    # MINES declares an icon; HELLO does not, so it gets the generic one.
    eq(recs[1]["icon"], mines[32:96], "MINES's icon is the package's own",
       "the row draws it with OSAPI_ICON_DRAW, so a catalog carrying "
       "something else would show a program under another program's picture")
    check(recs[0]["icon"] != mines[32:96],
          "HELLO, which declares no icon, gets the generic one")
    check(any(recs[0]["icon"]), "the generic icon is not 64 zero bytes",
          "icon_draw_x would draw nothing at all and every row without an "
          "icon of its own would be blank")


# =============================================================================
# 2. THE REFUSALS
# =============================================================================
def refusals(tmp):
    def refuse(what, mutate, why):
        m = json.loads(json.dumps(FIXTURE))
        mutate(m)
        man = os.path.join(tmp, "bad.json")
        cat = os.path.join(tmp, "bad.bin")
        json.dump(m, open(man, "w"))
        r = run("--pack", man, "--pkgdir", BUILD, "--out", cat)
        check(r.returncode != 0, "--pack refuses %s" % what, why,
              got=(r.stdout + r.stderr).strip() or "exit 0")

    refuse("a description that will not wrap into five lines",
           lambda m: m["entries"][0].__setitem__(
               "description", " ".join(["word"] * 60)),
           "the machine wraps nothing, so a sixth line is a sentence that "
           "vanishes with no sign it was there (SPEC.md 88.2)")
    refuse("a word longer than a line",
           lambda m: m["entries"][0].__setitem__(
               "description", "supercalifragilisticexpialidocious!!"),
           "it cannot be wrapped at all, and cutting it is a description "
           "that stops mid-word")
    refuse("a non-ASCII title",
           lambda m: m["entries"][0].__setitem__("title", "Café"),
           "the machine's font is ASCII 0x20..0x7E and has no glyph for it")
    refuse("a title over 23 characters",
           lambda m: m["entries"][0].__setitem__("title", "x" * 24),
           "24 bytes with no NUL inside them is a title the reader runs past")
    refuse("a stem with a dot in it",
           lambda m: m["entries"][0].__setitem__("stem", "HEL.LO"),
           "the stem composes /wire/pkg/<STEM>.O88 and <STEM>.PIC")
    refuse("a stem over 8 characters",
           lambda m: m["entries"][0].__setitem__("stem", "TOOLONGX9"),
           "the field is 8 bytes, space-padded")
    refuse("two entries with the same stem",
           lambda m: m["entries"][1].__setitem__("stem", "HELLO"),
           "two records would fetch the same URL and Add to Disk would "
           "write one over the other")
    refuse("a tier outside 0..3",
           lambda m: m["entries"][0].__setitem__("tier", 4),
           "the filter is five radios and tier <= X; a 4 is shown by none "
           "of them, so the record would be invisible and unexplained")
    refuse("a file that is not in the package directory",
           lambda m: m["entries"][0].__setitem__("files", ["nosuch.o88"]),
           "the size and the icon both come out of the file")
    refuse("an entry with no files at all",
           lambda m: m["entries"][0].__setitem__("files", []),
           "there is nothing to fetch")

    # ...and the two that need a file rather than a manifest field.
    big = os.path.join(tmp, "big.o88")
    with open(big, "wb") as f:
        f.write(open(os.path.join(BUILD, "hello.o88"), "rb").read())
        f.write(b"\0" * (S_FILEMAX + 1))
    m = json.loads(json.dumps(FIXTURE))
    m["entries"] = [{"stem": "BIG", "title": "Big", "tier": 0,
                     "files": ["big.o88"], "description": "Too big."}]
    json.dump(m, open(os.path.join(tmp, "big.json"), "w"))
    r = run("--pack", os.path.join(tmp, "big.json"), "--pkgdir", tmp,
            "--out", os.path.join(tmp, "big.bin"))
    check(r.returncode != 0,
          "--pack refuses a file over WIRE_FILEMAX without floppy_only",
          "the Wire moves a file in ONE claim and ONE OSAPI_FILE_WRITE, so a "
          "63KB+ file has no path through the machine at all (SPEC.md 88.2)",
          got=(r.stdout + r.stderr).strip() or "exit 0")
    m["entries"][0]["flags"] = {"floppy_only": True}
    json.dump(m, open(os.path.join(tmp, "big.json"), "w"))
    r = run("--pack", os.path.join(tmp, "big.json"), "--pkgdir", tmp,
            "--out", os.path.join(tmp, "big.bin"))
    check(r.returncode == 0, "...and accepts it WITH floppy_only",
          "WF_FLOPPY is how the format says 'this one comes as a disk image', "
          "and both actions then refuse with that reason rather than the "
          "packer refusing to describe it at all",
          got=(r.stdout + r.stderr).strip(), want="exit 0")

    notpkg = os.path.join(tmp, "notpkg.o88")
    open(notpkg, "wb").write(b"MZ" + b"\0" * 200)
    m["entries"] = [{"stem": "NOTPKG", "title": "Not one", "tier": 0,
                     "files": ["notpkg.o88"], "description": "No O8 magic."}]
    json.dump(m, open(os.path.join(tmp, "np.json"), "w"))
    r = run("--pack", os.path.join(tmp, "np.json"), "--pkgdir", tmp,
            "--out", os.path.join(tmp, "np.bin"))
    check(r.returncode != 0, "--pack refuses a file that is not an .o88",
          "the icon and the header flags are read out of it, and OSAPI_PKG_RUN "
          "would answer LD_EBAD after a whole transfer had been spent",
          got=(r.stdout + r.stderr).strip() or "exit 0")


# =============================================================================
def corrupt(tmp):
    """--verify must FAIL a catalog the machine would refuse."""
    man = os.path.join(tmp, "wire.json")
    cat = os.path.join(tmp, "catalog.bin")
    json.dump(FIXTURE, open(man, "w"))
    if run("--pack", man, "--pkgdir", BUILD, "--out", cat).returncode:
        return
    good = bytearray(open(cat, "rb").read())

    for what, off, val, why in (
            ("a broken magic", 0, ord("X"),
             "wr_catck compares 'WIRE' as two words before anything else"),
            ("a format version it does not know", 4, 2,
             "a version bump is how the format changes; a reader that took "
             "one it did not know would address a record layout that moved"),
            ("a record size that is not 16 sixteens", 5, 8,
             "the reader's addressing is WIRE_HDR + 256*i and nothing else"),
            ("a record count of zero", 6, 0,
             "an empty catalog is a fetch that succeeded and says nothing"),
            ("a record count that does not fit the file", 6, 200,
             "the reader would address 51KB into a 16KB claim"),
            ("a tier of 9", S_HDR + 33, 9,
             "no filter shows it, so the record is invisible"),
    ):
        b = bytearray(good)
        b[off] = val
        p = os.path.join(tmp, "c.bin")
        open(p, "wb").write(bytes(b))
        r = run("--verify", p)
        check(r.returncode != 0, "--verify fails %s" % what, why,
              got=(r.stdout + r.stderr).strip() or "exit 0")

    # --- and the two arms of the ICON check (SPEC.md 88.2) ----------------
    # MINES declares an OS88_ICON16, so its record must carry exactly those 64
    # bytes; HELLO declares none, so its record carries a GENERIC one and the
    # verifier may not compare it against anything - two writers may pick
    # different generics and both be right. What it may still refuse is an
    # icon with no pixels, which icon_draw_x accepts and draws as nothing.
    pub = os.path.join(tmp, "pkg")
    b = bytearray(good)
    b[S_HDR + S_REC + 48] ^= 0xFF           # MINES's icon, one byte
    p = os.path.join(tmp, "c.bin")
    open(p, "wb").write(bytes(b))
    r = run("--verify", p, "--pkgdir", pub)
    check(r.returncode != 0,
          "--verify fails a record whose package DECLARES an icon and whose "
          "record carries different bytes",
          "the row is drawn from those 64 bytes alone, so it would show one "
          "program under another program's picture",
          got=(r.stdout + r.stderr).strip() or "exit 0")

    b = bytearray(good)
    b[S_HDR + 48:S_HDR + 112] = b"\0" * 64  # HELLO's generic, blanked
    open(p, "wb").write(bytes(b))
    r = run("--verify", p, "--pkgdir", pub)
    check(r.returncode != 0,
          "--verify fails a generic icon of 64 zero bytes",
          "icon_draw_x accepts it and draws nothing at all, so the row "
          "silently has no picture rather than refusing where anybody sees it",
          got=(r.stdout + r.stderr).strip() or "exit 0")

    b = bytearray(good)
    for k in range(16):                      # HELLO's generic, mask only
        b[S_HDR + 48 + 32 + k * 2] = 0
        b[S_HDR + 48 + 32 + k * 2 + 1] = 0
    open(p, "wb").write(bytes(b))
    r = run("--verify", p, "--pkgdir", pub)
    check(r.returncode != 0,
          "--verify fails a generic icon whose DATA rows are all zero",
          "the mask lays down white and nothing is drawn over it, so the row "
          "shows a blank block where a picture should be",
          got=(r.stdout + r.stderr).strip() or "exit 0")

    b = bytearray(good)                      # ...and HELLO's generic CHANGED
    for k in range(64):                      # is NOT a failure: SPEC.md 88.2
        b[S_HDR + 48 + k] ^= 0x0F            # leaves which generic to the
    open(p, "wb").write(bytes(b))            # writer
    r = run("--verify", p, "--pkgdir", pub)
    check(r.returncode == 0,
          "--verify ACCEPTS a different generic on a package with no icon",
          "SPEC.md 88.2 says 'the package's own OS88_ICON16 block or the "
          "site's generic program icon', so two independent writers may "
          "choose differently and both be right - this is the check that "
          "failed the website's real catalog on three records",
          got=(r.stdout + r.stderr).strip(), want="exit 0")

    p = os.path.join(tmp, "good.bin")
    open(p, "wb").write(bytes(good))
    r = run("--verify", p)
    check(r.returncode == 0, "...and passes the untouched one",
          "a verifier that fails everything proves nothing about the six "
          "above", got=(r.stdout + r.stderr).strip(), want="exit 0")


# =============================================================================
# THE PICTURE (SPEC.md 88.3) - a PNG in, 1,024 bytes out
#
# The PNGs below are written HERE, by hand, in the two shapes the site's
# captures come in: a 1-bit grayscale off a Hercules or a CGA, and a 4-bit
# palette off a VGA. Writing them rather than committing a fixture is what
# makes the CROP checkable - the test knows which pixel is which.
# =============================================================================
def png(w, h, depth, ctype, rows, plte=None):
    def chunk(t, b):
        return (struct.pack(">I", len(b)) + t + b
                + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF))
    out = b"\x89PNG\r\n\x1a\n"
    out += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, ctype, 0, 0, 0))
    if plte:
        out += chunk(b"PLTE", plte)
    raw = b"".join(b"\0" + r for r in rows)          # filter 0 on every row
    return out + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


def picture(tmp):
    W, H = 200, 120
    X0, Y0 = 40, 24

    def want(x, y):                     # the pattern: ink where x == y mod 16
        return ((x + y) % 16) < 4

    # --- 1bpp grayscale, 0 = black = INK
    stride = (W + 7) // 8
    rows = []
    for y in range(H):
        r = bytearray(stride)
        for x in range(W):
            if not want(x, y):          # 1 = white = paper
                r[x >> 3] |= 0x80 >> (x & 7)
        rows.append(bytes(r))
    p1 = os.path.join(tmp, "herc.png")
    open(p1, "wb").write(png(W, H, 1, 0, rows))

    # --- 4bpp palette, index 0 black and index 15 white (the VGA's own)
    plte = bytearray(16 * 3)
    plte[15 * 3:15 * 3 + 3] = b"\xff\xff\xff"
    rows = []
    for y in range(H):
        r = bytearray(W // 2)
        for x in range(W):
            v = 0 if want(x, y) else 15
            if x & 1:
                r[x >> 1] |= v
            else:
                r[x >> 1] |= v << 4
        rows.append(bytes(r))
    p4 = os.path.join(tmp, "vga.png")
    open(p4, "wb").write(png(W, H, 4, 3, rows, bytes(plte)))

    expect = bytearray(1024)
    for y in range(64):
        for x in range(128):
            if want(X0 + x, Y0 + y):
                expect[y * 16 + (x >> 3)] |= 0x80 >> (x & 7)

    for tag, src in (("1-bit grayscale", p1), ("16-colour palette", p4)):
        out = os.path.join(tmp, "out.PIC")
        r = run("--pic", src, "--crop", "%d,%d" % (X0, Y0), "--out", out)
        if r.returncode:
            check(False, "--pic on a %s PNG" % tag,
                  "the site's captures are exactly these two shapes",
                  got=(r.stdout + r.stderr).strip())
            continue
        got = open(out, "rb").read()
        eq(len(got), 1024, "--pic writes %d bytes from a %s PNG"
           % (len(got), tag),
           "the reader refuses any other Content-Length (SPEC.md 88.3)")
        eq(got, bytes(expect), "--pic cuts the right 128x64 from a %s PNG"
           % tag,
           "it is a 1:1 CROP and never a scale, and 1 is INK - a polarity "
           "error draws the picture inside out and nothing says so")

    r = run("--pic", p1, "--crop", "%d,%d" % (W - 8, 0),
            "--out", os.path.join(tmp, "no.PIC"))
    check(r.returncode != 0, "--pic refuses a crop that runs off the image",
          "a short read would pad the picture with whatever came next")


# =============================================================================
# THE ARCHIVE (SPEC.md 88.13) - a SECOND READER, written from the spec
#
# `unlzss` below is deliberately not `os88wire.lzss_decode`. The format is
# pinned by its decoder, so two decoders written from the same paragraph are
# the only way to find out that the paragraph says what everybody thinks it
# says - and the 8088's unpacker will be a third. Where they disagree the
# SPEC has a bug and the SPEC is fixed first (t_wab's rule, WEAVE-SPEC 12.2).
# =============================================================================
def unlzss(body, want):
    """SPEC.md 88.13's method 1, by hand. Raises on the four refusals."""
    out = bytearray()
    p = 0
    while len(out) < want:
        assert p < len(body), "the stream ended before the output was complete"
        flag = body[p]
        p += 1
        for bit in range(8):                    # bit 0 first, LSB
            if len(out) >= want:
                break                           # ...whatever is left of it
            if flag & (1 << bit):
                assert p < len(body), "the stream ended inside a group"
                out.append(body[p])
                p += 1
            else:
                assert p + 1 < len(body), "the stream ended inside a pair"
                dist = (((body[p + 1] >> 4) << 8) | body[p]) + 1
                ln = (body[p + 1] & 15) + S_LENMIN
                p += 2
                assert 1 <= dist <= S_DISTMAX, "distance %d" % dist
                assert S_LENMIN <= ln <= S_LENMAX, "length %d" % ln
                assert dist <= len(out), "a distance past the start"
                assert len(out) + ln <= want, "a copy past the end"
                for k in range(ln):             # ONE BYTE AT A TIME, so a
                    out.append(out[len(out) - dist])    # short distance runs
    assert p == len(body), "%d stored bytes left over" % (len(body) - p)
    return bytes(out)


def read_archive(blob):
    """SPEC.md 88.13, by hand. (header, entries); raises on any refusal."""
    assert len(blob) >= S_AHDR, "shorter than the header"
    assert blob[0:4] == S_AMAGIC, "magic %r" % blob[0:4]
    assert blob[4] == 1, "format version %d" % blob[4]
    hflags = blob[5]
    assert not hflags & ~1, "header flags 0x%02X" % hflags
    n = struct.unpack_from("<H", blob, 6)[0]
    assert 1 <= n <= S_ANMAX, "entry count %d" % n
    total = struct.unpack_from("<I", blob, 8)[0]
    needkb = struct.unpack_from("<H", blob, 12)[0]
    maxent = struct.unpack_from("<H", blob, 14)[0]
    assert maxent <= S_FILEMAX, "largest entry %d" % maxent
    # A SLOT IS AT MOST TWELVE BYTES STOPPING AT A NUL (SPEC.md 88.13): no NUL
    # at all is a twelve-character 8.3 name filling it, which is what
    # CONSOLE7.COM and LEFT-OFF.TXT are. A byte AFTER the NUL is still a fault.
    def slot(s, what):
        t = s.split(b"\0")[0]
        assert s[len(t):] == b"\0" * (S_ASLOT - len(t)), \
            "%s has bytes after its NUL" % what
        return t.decode("ascii")

    home = slot(blob[16:28], "the home slot")
    assert blob[28:32] == b"\0" * 4, "the header's +28 is not zero"

    ents, p = [], S_AHDR
    for i in range(n):
        assert p + S_AENT <= len(blob), "entry %d's header runs past the end" % i
        e = blob[p:p + S_AENT]
        method, depth = e[0], e[1]
        assert method in (0, 1), "entry %d method %d" % (i, method)
        assert depth <= S_ADEPTH, "entry %d depth %d" % (i, depth)
        assert e[2:4] == b"\0\0" and e[12:16] == b"\0" * 4, \
            "entry %d has a reserved field set" % i
        stored = struct.unpack_from("<I", e, 4)[0]
        size = struct.unpack_from("<I", e, 8)[0]
        assert size <= S_FILEMAX, "entry %d is %d bytes" % (i, size)
        assert size <= maxent, "entry %d is over the header's largest" % i
        slots = []
        for k in range(S_ASLOTS):
            s = e[16 + k * S_ASLOT:16 + (k + 1) * S_ASLOT]
            t = slot(s, "entry %d slot %d" % (i, k))
            if k <= depth:
                assert t, "entry %d slot %d is empty" % (i, k)
                assert t == t.upper(), "entry %d slot %d is not uppercase" % (
                    i, k)
            else:
                assert s == b"\0" * S_ASLOT, \
                    "entry %d slot %d is past the name and not all-NUL" % (i, k)
            slots.append(t)
        p += S_AENT
        assert p + stored <= len(blob), "entry %d's body runs past the end" % i
        body = blob[p:p + stored]
        p += stored
        if method == 0:
            assert stored == size, "entry %d is stored and %d != %d" % (
                i, stored, size)
            data = bytes(body)
        else:
            data = unlzss(body, size)
        assert len(data) == size, "entry %d unpacked to %d" % (i, len(data))
        ents.append({"method": method, "depth": depth, "stored": stored,
                     "size": size, "path": "/".join(slots[:depth + 1]),
                     "data": data})
    assert p == len(blob), "%d bytes after the last body" % (len(blob) - p)

    sizes = [e["size"] for e in ents]
    assert total == sum(sizes), "the header's total is %d and the entries add "\
        "to %d" % (total, sum(sizes))
    assert needkb == sum((s + 1023) // 1024 for s in sizes), \
        "the header's need-KB is %d" % needkb
    assert maxent == max(sizes), "the header's largest is %d" % maxent
    groups = []
    for e in ents:
        f = e["path"].rsplit("/", 1)[0] if e["depth"] else ""
        if not groups or groups[-1] != f:
            assert f not in groups, "the folder %r appears in two runs" % f
            groups.append(f)
    if hflags & 1:
        assert ents[-1]["depth"] == 0 and ents[-1]["path"].endswith(".O88"), \
            "WAH_PROGRAM and the last entry is %s" % ents[-1]["path"]
    return {"flags": hflags, "n": n, "total": total, "needkb": needkb,
            "maxent": maxent, "home": home}, ents


# =============================================================================
# THE ENCODER against the reference decoder, at the format's own limits
# =============================================================================
def far_match(extra):
    """A WARC_LENMAX token repeated at a distance of WARC_DISTMAX + `extra`.

    The filler is 0..127 and the token 200..217, so the token occurs exactly
    twice and NO BYTE of either is 0xFF. That is what makes the assertion
    below exact rather than statistical: the pair `FF FF` decodes to distance
    ((0xF << 8) | 0xFF) + 1 = 4096 and length 18, and it is the only way two
    0xFF bytes can appear in the stream at all - a flag byte of 0xFF is eight
    literals, and none of this input's literals is 0xFF.
    """
    token = bytes(range(200, 200 + S_LENMAX))
    filler = bytes((i * 7 + 3) % 128
                   for i in range(S_DISTMAX + extra - S_LENMAX))
    return token + filler + token


def lzss(mod):
    hello = open(os.path.join(BUILD, "hello.o88"), "rb").read()
    import random
    rnd = random.Random(88)                 # SEEDED: a fast-tier row that
    cases = [                               # fails once a month is no row
        ("empty", b""),
        ("one byte", b"A"),
        ("two bytes - under the minimum match", b"AB"),
        ("exactly the minimum match", b"ABC"),
        ("a run longer than WARC_LENMAX", b"Z" * 200),
        ("a run of exactly WARC_LENMAX + 2", b"ABC" + b"Q" * (S_LENMAX + 2)),
        ("a period the encoder must find", b"os8088. " * 300),
        ("a match at exactly WARC_DISTMAX", far_match(0)),
        ("one byte past WARC_DISTMAX", far_match(1)),
        ("random bytes, which the writer stores instead",
         bytes(rnd.randrange(256) for _ in range(1500))),
        ("a real package image", hello),
        ("an entry at WIRE_FILEMAX", (b"os8088" * 11000)[:S_FILEMAX]),
    ]
    for what, src in cases:
        enc = mod.lzss_encode(src)
        eq(mod.lzss_decode(enc, len(src)), src,
           "LZSS round trip: %s (%d -> %d bytes)" % (what, len(src), len(enc)),
           "the encoder and the reference decoder are one file's two halves; "
           "a stream only this encoder can read is a stream the 8088 refuses "
           "(SPEC.md 88.13)")
        try:
            got = unlzss(enc, len(src))
        except AssertionError as e:
            check(False, "the SPEC-side decoder reads %s" % what,
                  "SPEC.md 88.13 pins method 1 BY ITS DECODER, so two readings "
                  "of that paragraph disagreeing is a bug in the paragraph",
                  got=str(e))
            continue
        eq(got, src, "the SPEC-side decoder reads %s" % what,
           "the 8088's unpacker will be a third reader of the same paragraph")

    # ...and the format's two ends, checked on the STREAM and not on its
    # length. A distance is ((B1 >> 4) << 8 | B0) + 1, so 4096 is the last
    # one representable and a length of 18 is (B1 & 15) + 3 at its top: the
    # pair FF FF is exactly that match and nothing else in these two inputs
    # can spell it.
    check(b"\xff\xff" in mod.lzss_encode(far_match(0)),
          "the encoder reaches a match at WARC_DISTMAX with WARC_LENMAX",
          "the window is the whole entry and the far end of it is where a "
          "greedy search stops looking first - an encoder that quietly "
          "capped the distance short would still decode and would simply be "
          "worse, which is the failure nothing else here would see",
          got="no FF FF pair")
    check(b"\xff\xff" not in mod.lzss_encode(far_match(1)),
          "...and one byte past it is written out as literals",
          "4097 does not fit the 12-bit field, and an encoder that emitted "
          "it anyway would write a pair the decoder resolves to somewhere "
          "else entirely", got="an FF FF pair")


def decoder_refusals(mod):
    """SPEC.md 88.13's four refusals, on streams made by hand."""
    def refuse(what, body, want, why):
        try:
            mod.lzss_decode(body, want)
        except mod.Refused:
            check(True, "the decoder refuses %s" % what)
            return
        check(False, "the decoder refuses %s" % what, why, got="it decoded")

    # A DISTANCE PAST THE START: one literal, then a pair reaching 2 back.
    refuse("a distance past the start of the entry",
           bytes([0x01, ord("A"), 0x01, 0x00]), 10,
           "the output is a whole file in one claim and a back-reference is a "
           "copy WITHIN it, so a distance past its start reads the heap")
    # A COPY PAST THE END: three literals, then an 18-byte copy into a want
    # of 5. The claim is the header's largest-entry figure and nothing else.
    refuse("a copy that would pass the end of the entry",
           bytes([0x07, ord("A"), ord("B"), ord("C"), 0x00, 0x0F]), 5,
           "the claim is made from the header's largest-entry figure, so a "
           "copy past the entry's own length writes past the claim")
    # A SHORT STREAM: a flag byte promising a literal that is not there.
    refuse("a stream that ends before the output is complete",
           bytes([0x01]), 4,
           "a truncated body is a transfer that failed, and a decoder that "
           "padded it would write half a program and say it succeeded")
    refuse("a stream that is empty and an output that is not", b"", 1,
           "as above, and this is the shape a zero-length body takes")
    # TRAILING STORED BYTES: the output completes and a byte is left over.
    refuse("stored bytes left over when the output is complete",
           bytes([0x03, ord("A"), ord("B"), 0x00]), 2,
           "the entry's stored size and its stream must end together; a "
           "byte left over is the next entry's header being read as data")
    # ...and the stream that is exactly right still decodes.
    eq(mod.lzss_decode(bytes([0x03, ord("A"), ord("B")]), 2), b"AB",
       "...and a stream that ends exactly with the output decodes",
       "a decoder that refuses everything proves nothing about the five above")


# =============================================================================
# THE ARCHIVE ROUND TRIP - pack, verify, dump, and read back from the SPEC
# =============================================================================
def arc_fixture(tmp):
    """A tree with everything SPEC.md 88.13 has a rule about in it."""
    src = os.path.join(tmp, "tree")
    for d in ("", "DOCS", "DATA", os.path.join("DATA", "SUB"),
              os.path.join("DATA", "SUB", "DEEP")):
        os.makedirs(os.path.join(src, d), exist_ok=True)
    import random
    rnd = random.Random(13)
    files = {
        # A COMPRESSIBLE entry, an EMPTY one, a STORED one (random bytes do
        # not compress, so the writer must fall back to method 0), a
        # depth-WARC_DEPTH one, and the program LAST.
        "README.TXT": b"The Wire moves a folder tree in one stream.\r\n" * 40,
        # ...and the two shapes 88.13 was AMENDED for: a twelve-character 8.3
        # name FILLS its slot and carries no NUL, at the root and inside a
        # folder. Five of the RunCPM master disk's 77 files are these, and the
        # first draft of the format could not carry any of them.
        "CONSOLE7.COM": b"\xc3\x00\x01" * 300,
        os.path.join("DOCS", "LEFT-OFF.TXT"): b"where the last session "
                                              b"left off\r\n" * 12,
        os.path.join("DOCS", "GUIDE.TXT"): b"os8088 " * 500,
        os.path.join("DOCS", "EMPTY.TXT"): b"",
        os.path.join("DATA", "RANDOM.BIN"):
            bytes(rnd.randrange(256) for _ in range(900)),
        os.path.join("DATA", "SUB", "DEEP.TXT"): b"two folders down\r\n" * 20,
        os.path.join("DATA", "SUB", "DEEP", "DEEPEST.TXT"):
            b"three folders down, which is WARC_DEPTH\r\n" * 20,
        "HELLO.O88": open(os.path.join(BUILD, "hello.o88"), "rb").read(),
    }
    for name, body in files.items():
        open(os.path.join(src, name), "wb").write(body)
    return src, files


def archive_roundtrip(tmp):
    src, files = arc_fixture(tmp)
    wpk = os.path.join(tmp, "WPKTEST.WPK")
    r = run("--archive", wpk, "--srcdir", src, "--home", "WPKTEST",
            "--program", "HELLO.O88")
    if r.returncode:
        check(False, "--archive packed the fixture tree",
              "the archive round trip cannot run",
              got=(r.stdout + r.stderr).strip())
        return
    r = run("--verify", wpk)
    check(r.returncode == 0, "--verify on the writer's own archive",
          "a writer and a reader that never meet is two readings of one "
          "format (SPEC.md 88.13)",
          got=(r.stdout + r.stderr).strip(), want="exit 0")
    r = run("--dump", wpk)
    check(r.returncode == 0, "--dump on a .WPK",
          got=(r.stdout + r.stderr).strip(), want="exit 0")
    for want in ("WPAK v1", "WPKTEST", "WAH_PROGRAM",
                 "DATA/SUB/DEEP/DEEPEST.TXT", "stored"):
        check(want in r.stdout, "--dump names %s" % want,
              "a dump nobody can read against the tree is not a check",
              got=r.stdout)

    blob = open(wpk, "rb").read()
    try:
        hdr, ents = read_archive(blob)
    except AssertionError as e:
        check(False, "the SPEC-side reader refused the packed archive",
              "SPEC.md 88.13 and tools/os88wire.py disagree; the SPEC is "
              "fixed first", got=str(e))
        return

    eq(hdr["home"], "WPKTEST", "the home folder survives the round trip",
       "the whole tree lands under it, which is what lets a later archive of "
       "a CP/M game name the same home and paths under A/1/ (SPEC.md 88.13)")
    eq(hdr["n"], len(files), "every file in the tree is an entry")
    check(hdr["flags"] & 1, "WAH_PROGRAM is set",
          "Load Program runs the last entry when the tree has landed")
    eq(hdr["total"], sum(len(b) for b in files.values()),
       "the header's unpacked total is the tree's bytes",
       "it is copied into WC_TOTAL and Add to Disk's free-space check is "
       "decided on it (SPEC.md 88.2)")
    eq(hdr["maxent"], max(len(b) for b in files.values()),
       "the header's largest entry is the largest file",
       "it is the ONE claim the reader makes for the whole transfer")
    eq(hdr["needkb"], sum((len(b) + 1023) // 1024 for b in files.values()),
       "the header's need-KB is the sum of ceil(size / 1024)",
       "PER ENTRY and not over the total: a RAM disk store <= 2MB has a 1KB "
       "extent (SPEC.md 62.9.10), so every file rounds up on its own")

    got = {e["path"]: e["data"] for e in ents}
    eq(sorted(got), sorted(p.replace(os.sep, "/") for p in files),
       "every path in the tree is in the archive")
    for name, body in files.items():
        eq(got.get(name.replace(os.sep, "/")), body,
           "%s comes back byte for byte" % name.replace(os.sep, "/"),
           "the tree is written to a disk or a RAM store from these bytes")

    # THE TWELVE-CHARACTER NAMES, on the bytes: the slot is full and there is
    # no NUL in it, which is what SPEC.md 88.13 amended the format to say.
    # THE SLOT IS FULL AND THERE IS NO NUL IN IT, which is the whole of the
    # amendment: the reader above copies at most twelve bytes and stops at a
    # NUL, so a name coming back at twelve characters is the proof - a
    # NUL-terminated slot could carry eleven, and a writer that truncated
    # would have handed back CONSOLE7.CO.
    for path in ("CONSOLE7.COM", "DOCS/LEFT-OFF.TXT"):
        name = path.rsplit("/", 1)[-1]
        eq(len(name), S_ASLOT,
           "%s is a twelve-character 8.3 name" % name,
           "the case is only a case if the fixture really is at the limit")
        check(path in got,
              "a twelve-character 8.3 name round-trips: %s" % path,
              "five of the RunCPM master disk's 77 files are these, so a "
              "format that could not carry them could not carry its own "
              "motivating example (SPEC.md 88.13)", got=sorted(got))

    eq(ents[-1]["path"], "HELLO.O88", "the program entry is LAST",
       "the claim still holds it when the transfer ends, and OSAPI_PKG_RUN "
       "takes it from there with no second read (SPEC.md 88.14)")
    eq(ents[-1]["depth"], 0, "...and it is at depth 0")
    rand = [e for e in ents if e["path"] == "DATA/RANDOM.BIN"][0]
    eq(rand["method"], 0, "random bytes are STORED and not LZSS",
       "a body that did not get smaller costs the reader a decode for "
       "nothing and the wire more than it saved")
    guide = [e for e in ents if e["path"] == "DOCS/GUIDE.TXT"][0]
    eq(guide["method"], 1, "a compressible entry is LZSS")
    check(guide["stored"] < guide["size"], "...and its body is smaller",
          "method 1 is only chosen when it wins", got=(guide["stored"],
                                                       guide["size"]))
    empty = [e for e in ents if e["path"] == "DOCS/EMPTY.TXT"][0]
    eq((empty["method"], empty["stored"], empty["size"]), (0, 0, 0),
       "an empty file is a stored entry of no bytes at all",
       "0 is a legal unpacked size (SPEC.md 88.13) and a folder tree has "
       "them; a writer that skipped it would lose a file with no sign")

    groups = []
    for e in ents:
        f = e["path"].rsplit("/", 1)[0] if e["depth"] else ""
        if not groups or groups[-1] != f:
            groups.append(f)
    eq(len(groups), len(set(groups)),
       "the entries are GROUPED BY FOLDER, %d folders in %d runs"
       % (len(set(groups)), len(groups)),
       "the reader banks the folder it stands in, so a tree in this order "
       "costs one folder change per FOLDER and not one per file - and a "
       "folder change on a floppy is FIND walks and a GOTO, each priced in "
       "int 13h calls (SPEC.md 88.13)")
    eq(groups[-1], "",
       "...and the root is the LAST run, because the program is in it",
       "sorting alone puts the root first, and lifting the program out of it "
       "to the end would leave the root appearing in two runs - both of "
       "88.13's ordering rules hold only if the whole root group goes last")
    return wpk


def archive_corrupt(mod, wpk):
    """--verify must FAIL a .WPK the machine would refuse.

    In process, for archive_record's reason. The first entry of the fixture
    tree is DATA/RANDOM.BIN - depth 1, so slot 0 is DATA, slot 1 the file
    name and slots 2 and 3 all-NUL.
    """
    good = bytearray(open(wpk, "rb").read())
    E = S_AHDR                                  # the first entry header

    def poke(off, val):
        return lambda b: b.__setitem__(off, val)

    def word(off, val):
        return lambda b: struct.pack_into("<H", b, off, val)

    for what, mutate, why in (
            ("a broken magic", poke(0, ord("X")),
             "the reader compares 'WPAK' before anything else"),
            ("a format version it does not know", poke(4, 2),
             "a version bump is how the format changes; a reader that took "
             "one it did not know would address an entry layout that moved"),
            ("a header flag it does not know", poke(5, 0x80),
             "the only flag is WAH_PROGRAM, and an unknown one means the "
             "writer was saying something this reader will not do"),
            ("an entry count of zero", word(6, 0),
             "an archive of nothing is a transfer that succeeded and did "
             "nothing"),
            ("an entry count that does not fit the file", word(6, 200),
             "the reader walks the chain against that count and would read "
             "past the last body"),
            ("an unpacked total that is not the entries'",
             lambda b: struct.pack_into("<I", b, 8, 1),
             "it is copied into WC_TOTAL and Add to Disk's free-space check "
             "is decided on it"),
            ("a need-KB that is not the entries'", word(12, 1),
             "Load Program sizes the RAM disk store on it, and a short store "
             "fills before the tree has landed"),
            ("a largest-entry figure under a real entry", word(14, 1),
             "it is the ONE claim the reader makes for the whole transfer, "
             "so an entry over it writes past the claim"),
            ("an entry deeper than WARC_DEPTH", poke(E + 1, 4),
             "the path is four slots and the last is the file name"),
            ("a lowercase name in a path slot", poke(E + 16, ord("d")),
             "a FAT12 directory entry is uppercase, and the machine cannot "
             "see two names fold onto one"),
            ("a path slot past the file name that is not NUL",
             poke(E + 16 + 2 * S_ASLOT, ord("X")),
             "the slots after the name are where a reader stops, so a byte "
             "there is a folder nobody asked for"),
            ("a reserved field that is not zero", poke(E + 2, 1),
             "the zeros are what a version 2 will use, and a version 1 "
             "writer that filled them is not writing version 1"),
    ):
        b = bytearray(good)
        mutate(b)
        check(bool(mod.arc_verify(bytes(b), "bad.WPK")),
              "--verify fails %s" % what, why, got="no complaint")

    check(bool(mod.arc_verify(bytes(good[:-1]), "bad.WPK")),
          "--verify fails a truncated archive",
          "a body that stops short is a transfer that failed, and the machine "
          "must say so rather than write half a file", got="no complaint")
    check(bool(mod.arc_verify(bytes(good) + b"\0", "bad.WPK")),
          "--verify fails a byte after the last body",
          "the entry chain and the file end together; a byte after it is a "
          "header the reader never looked at", got="no complaint")
    check(not mod.arc_verify(bytes(good), "good.WPK"),
          "...and passes the untouched one",
          "a verifier that fails everything proves nothing about the fourteen "
          "above", got=mod.arc_verify(bytes(good), "good.WPK"))


# =============================================================================
# THE CATALOG RECORD FOR AN ARCHIVE (SPEC.md 88.2's WF_ARC)
# =============================================================================
def archive_record(tmp, wpk, mod):
    man = os.path.join(tmp, "arc.json")
    cat = os.path.join(tmp, "arc.bin")
    json.dump({"date": "20260904", "entries": [
        {"stem": "WPKTEST", "title": "An archive", "kind": 0, "tier": 0,
         "archive": {"file": "WPKTEST.WPK"},
         "description": "A folder tree in one stream."}]},
        open(man, "w"))
    r = run("--pack", man, "--pkgdir", tmp, "--out", cat)
    if r.returncode:
        check(False, "--pack accepts an archive entry",
              "the manifest's archive record is how a .WPK reaches the list",
              got=(r.stdout + r.stderr).strip())
        return
    r = run("--verify", cat, "--pkgdir", tmp)
    check(r.returncode == 0, "--verify cross-checks the record against the "
          ".WPK", "the record's three figures are copies of the archive "
          "header's, and a copy is a place for them to disagree",
          got=(r.stdout + r.stderr).strip(), want="exit 0")
    r = run("--dump", cat)
    check(" FA " in r.stdout, "--dump shows F and A on the row",
          "the two flags travel together and a dump is how a person checks "
          "that they did", got=r.stdout)

    blob = open(cat, "rb").read()
    _, recs, _ = read_catalog(blob)
    rec = recs[0]
    hdr, _ = read_archive(open(wpk, "rb").read())
    check(rec["flags"] & S_WF_ARC, "the record is WF_ARC")
    check(rec["flags"] & S_WF_FLOPPY,
          "...and the writer sets WF_FLOPPY WITH it",
          "wr_catck never refused a flag it did not know, so a Wire from "
          "before 88.13 reads the same catalog, greys both buttons with the "
          "floppy reason, and never fetches a .O88 that is not there "
          "(SPEC.md 88.2)")
    check(not rec["flags"] & S_WF_DISK,
          "...and WF_DISK is CLEAR on it",
          "the tree it unpacks IS its files on a disk, so Load Program is "
          "the point of the record rather than the thing it refuses")
    eq(rec["nside"], 0, "an archive record has no sidecars",
       "its extra files are entries in the one stream, which is the format")
    eq(rec["size"], os.path.getsize(wpk),
       "WC_SIZE is the .WPK's own byte count",
       "wr_hdrdone checks Content-Length against it before the body")
    eq(rec["total"], hdr["total"], "WC_TOTAL is the archive's UNPACKED total")
    eq(rec["needkb"], hdr["needkb"],
       "WC_NEEDKB is the archive header's own figure",
       "Load Program sizes the RAM disk store on the record and then fills "
       "it from the stream (SPEC.md 88.14)")
    check(any(rec["icon"]),
          "the record carries an icon with pixels in it",
          "build/hello.o88 declares no OS88_ICON16, so this record gets the "
          "generic - and icon_draw_x accepts 64 zero bytes and draws nothing")

    # ...and an archive whose program DOES declare one must carry exactly it.
    # build/mines.o88 is the package in this tree that has an icon, so the
    # rule is checked on a second archive rather than on the fixture above.
    mines = open(os.path.join(BUILD, "mines.o88"), "rb").read()
    mtree = os.path.join(tmp, "mtree")
    os.makedirs(mtree, exist_ok=True)
    open(os.path.join(mtree, "MINES.O88"), "wb").write(mines)
    open(os.path.join(tmp, "MINEARC.WPK"), "wb").write(
        mod.archive(mtree, "MINES", "MINES.O88"))
    mcat = mod.pack({"date": "20260904", "entries": [
        {"stem": "MINEARC", "title": "An archive with an icon", "tier": 0,
         "archive": {"file": "MINEARC.WPK"}, "description": "One entry."}]},
        tmp, None)
    eq(mcat[S_HDR + 48:S_HDR + 112], mines[32:96],
       "the record's icon is the archive's PROGRAM entry's own",
       "the Disk window draws that icon for the .O88 the tree lands as, so a "
       "row showing anything else puts one program under two pictures - the "
       "rule the .O88 records have had since 88.2, reached through a decode")
    check(not mod.verify(mcat, tmp), "...and it verifies against the .WPK",
          got=mod.verify(mcat, tmp))
    b = bytearray(mcat)
    b[S_HDR + 48] ^= 0xFF
    check(bool(mod.verify(bytes(b), tmp)),
          "--verify fails an archive record carrying a different icon",
          "the picture is decoded out of the stream, so the website's second "
          "writer has to decode it too - which is why `--verify` run over "
          "what that writer published is the arrangement (SPEC.md 88.13)",
          got="no complaint")

    # ...and the four ways the record can be wrong, each refused. These call
    # the verifier IN PROCESS rather than over the CLI: the round trip above
    # has already proved the two agree, and this row is in the fast tier,
    # where an interpreter start per assertion is most of the budget.
    good = bytearray(blob)
    for what, mutate, why in (
            ("WF_ARC without WF_FLOPPY",
             lambda b: b.__setitem__(S_HDR + 34,
                                     b[S_HDR + 34] & ~S_WF_FLOPPY),
             "a Wire from before 88.13 would fetch a <STEM>.O88 that is not "
             "published"),
            ("WF_ARC with WF_DISK",
             lambda b: b.__setitem__(S_HDR + 34, b[S_HDR + 34] | S_WF_DISK),
             "88.2 clears bit 0 on an archive, and Load Program is greyed on "
             "bit 0"),
            ("a WC_NEEDKB that is not the archive's",
             lambda b: struct.pack_into("<H", b, S_HDR + 46, 1),
             "the RAM disk would be sized short and fill before the tree "
             "landed"),
            ("a WC_TOTAL that is not the archive's",
             lambda b: struct.pack_into("<I", b, S_HDR + 42, 99),
             "Add to Disk's free-space check is decided on it"),
    ):
        b = bytearray(good)
        mutate(b)
        check(bool(mod.verify(bytes(b), tmp)), "--verify fails %s" % what, why,
              got="no complaint")

    # --- WIRE_ARCMAX, and the bound that must NOT apply ---------------------
    # An archive's WC_SIZE is a dword and an archive is the first body over
    # 64KB by design, so WIRE_FILEMAX is the wrong bound - and it is the one
    # that refused os8088.com's real RUNCPM record. 231,463 is that record.
    b = bytearray(good)
    struct.pack_into("<I", b, S_HDR + 38, 231463)
    check(not mod.verify(bytes(b)),
          "--verify accepts an archive record whose WC_SIZE is 231,463",
          "bounding an archive by WIRE_FILEMAX is what refused the site's own "
          "RunCPM record, and a verifier that refuses what the site publishes "
          "is a gate nobody can leave switched on",
          got=mod.verify(bytes(b)))
    # WIRE_ARCMAX IS THE FIRST SIZE REFUSED and not the last accepted, because
    # the machine compares the high word alone (wcat.inc). So the boundary is
    # checked on both sides of itself: one byte under passes, exactly the
    # bound fails.
    b = bytearray(good)
    struct.pack_into("<I", b, S_HDR + 38, S_ARCMAX - 1)
    check(not mod.verify(bytes(b)),
          "--verify accepts an archive record of WIRE_ARCMAX - 1 bytes",
          "an off-by-one here is a record the host publishes and the machine "
          "refuses, or the reverse - and neither end says which",
          got=mod.verify(bytes(b)))
    b = bytearray(good)
    struct.pack_into("<I", b, S_HDR + 38, S_ARCMAX)
    check(bool(mod.verify(bytes(b))),
          "--verify fails an archive record of exactly WIRE_ARCMAX bytes",
          "wcat.inc says an archive's WC_SIZE must be BELOW the bound, which "
          "is one compare on the high word rather than three on the pair - so "
          "the bound itself is refused",
          got="no complaint")

    # A WC_NEEDKB on a record that is NOT an archive is a fault too: 88.2
    # says the word is zero on every other record, and a reader that trusts
    # it would size a RAM disk for a program that never asked for one.
    b = bytearray(good)
    b[S_HDR + 34] &= ~(S_WF_ARC | S_WF_FLOPPY)
    check(bool(mod.verify(bytes(b))),
          "--verify fails a WC_NEEDKB with no WF_ARC",
          "the word is zero on every record that is not an archive",
          got="no complaint")


# =============================================================================
# THE ARCHIVE WRITER'S REFUSALS (SPEC.md 88.13)
# =============================================================================
def archive_refusals(tmp, mod):
    root = os.path.join(tmp, "ref")

    def tree(name, files, dirs=()):
        d = os.path.join(root, name)
        for sub in dirs:
            os.makedirs(os.path.join(d, sub), exist_ok=True)
        os.makedirs(d, exist_ok=True)
        for f, body in files.items():
            os.makedirs(os.path.dirname(os.path.join(d, f)) or d,
                        exist_ok=True)
            open(os.path.join(d, f), "wb").write(body)
        return d

    # IN PROCESS, for archive_record's reason: this row is in the fast tier
    # and an interpreter start per assertion is most of the budget. The CLI's
    # own refusal path is checked once, at the bottom.
    def refuse(what, srcdir, why, program=None, order=None):
        try:
            mod.archive(srcdir, None, program, order)
        except mod.Refused:
            check(True, "--archive refuses %s" % what)
            return
        check(False, "--archive refuses %s" % what, why, got="it packed")

    hello = open(os.path.join(BUILD, "hello.o88"), "rb").read()

    refuse("a path four folders deep",
           tree("deep", {os.path.join("A", "B", "C", "D", "F.TXT"): b"x"}),
           "the path is four 12-byte slots and the last of them is the FILE "
           "name, so three folders is all there is room for (WARC_DEPTH)")
    refuse("a name that is not 8.3",
           tree("bad83", {"TOOLONGNAME.TXT": b"x"}),
           "it lands in a FAT12 directory entry, which is 8 and 3")
    # THE OTHER SIDE OF THE TWELVE-CHARACTER RULE. CONSOLE7.COM fills the slot
    # and is packed; a thirteen-character name cannot, and neither can a
    # twelve-character one whose stem is nine - the slot's width is not the
    # rule, 8.3 is, and the two happen to meet at twelve.
    refuse("a thirteen-character name",
           tree("thirteen", {"CONSOLE77.COM": b"x"}),
           "8.3 tops out at twelve characters, so thirteen could not be "
           "written whole - and a writer that truncated would land a file "
           "the launched program cannot find")
    refuse("a twelve-character name with a nine-character stem",
           tree("stem9", {"LONGSTEM9.TX": b"x"}),
           "it FITS the slot and is still not an 8.3 name: the rule is 8.3 "
           "and not the slot's width, and the two only happen to meet at "
           "twelve. FAT12 would split it 8 and 3 and show LONGSTEM.9TX")
    refuse("a lowercase name",
           tree("lower", {"readme.txt": b"x"}),
           "a silent upper-case would put readme and README on one FAT12 "
           "entry, and the machine cannot see that happen")
    refuse("a folder name with an extension",
           tree("dotdir", {os.path.join("MY.DIR", "F.TXT"): b"x"}),
           "a directory entry takes no extension - tools/os88disk.py's "
           "folder83 rule, at every level")
    refuse("an entry over WIRE_FILEMAX",
           tree("big", {"BIG.BIN": b"\0" * (S_FILEMAX + 1)}),
           "the reader makes ONE claim for the largest entry and writes each "
           "file with ONE OSAPI_FILE_WRITE, so a 63KB+ file has no path "
           "through the machine at all")
    refuse("a --program that is not at depth 0",
           tree("progdeep", {os.path.join("SUB", "HELLO.O88"): hello,
                             "F.TXT": b"x"}),
           "OSAPI_PKG_RUN runs it with the instance's directory on the tree, "
           "which is where its overlay and sidecars have to be",
           program="SUB/HELLO.O88")
    refuse("a --program that names nothing in the tree",
           tree("prognone", {"F.TXT": b"x"}),
           "a WAH_PROGRAM with no program is an archive that lands and never "
           "launches", program="HELLO.O88")
    refuse("an empty source directory", tree("empty", {}),
           "an archive of nothing is a transfer that succeeded and did "
           "nothing")

    # --- the two that need a CURATED order, because the writer's own sort
    #     cannot produce them: a program that is not last, and two runs of
    #     one folder.
    curated = tree("cur", {"HELLO.O88": hello, "F.TXT": b"x",
                           os.path.join("DOCS", "A.TXT"): b"a",
                           os.path.join("DOCS", "B.TXT"): b"b"})
    order = os.path.join(tmp, "order.txt")
    open(order, "w").write("# the program is not last\nHELLO.O88\nF.TXT\n"
                           "DOCS/A.TXT\nDOCS/B.TXT\n")
    refuse("a program entry that is not last", curated,
           "the claim that holds the last entry is what OSAPI_PKG_RUN "
           "launches; anything else is a second read (SPEC.md 88.14)",
           program="HELLO.O88", order=order)
    open(order, "w").write("DOCS/A.TXT\nF.TXT\nDOCS/B.TXT\nHELLO.O88\n")
    refuse("entries that are not grouped by folder", curated,
           "the reader banks the folder it stands in, so DOCS twice is two "
           "GOTOs and a FIND walk that a grouped order does not pay",
           order=order)

    # MORE THAN 255 ENTRIES. The count is 1..255 in a word field and the
    # reader's chain is one entry at a time, so 256 is where it stops.
    many = os.path.join(root, "many")
    os.makedirs(many, exist_ok=True)
    for i in range(S_ANMAX + 1):
        open(os.path.join(many, "F%05d.TXT" % i), "wb").write(b"x")
    refuse("more than %d entries" % S_ANMAX, many,
           "the header's entry count is 1..255 and the status cell counts "
           "the chain against it")

    # --- WIRE_ARCMAX, at both ends ------------------------------------------
    # The VERIFIER's arm uses the real constant: a megabyte of zeros costs an
    # allocation and the length check is made before the structure is read.
    check(bool(mod.arc_verify(bytes(S_ARCMAX), "big.WPK")),
          "--verify fails an archive of exactly WIRE_ARCMAX bytes",
          "the whole file is what the record's WC_SIZE claims and what "
          "Content-Length is checked against, so an archive at the bound "
          "cannot be published whatever is inside it - and the bound is the "
          "FIRST size refused", got="no complaint")

    # The WRITER's arm POKES THE BOUND rather than building a megabyte of
    # incompressible tree: seventeen 63KB entries is 0.9 s of encoding in a
    # row that costs 1.6, and what is under test here is the writer's path
    # and its sentence, the value having just been checked above.
    # tests/fishedge.py pokes [sv_hlim] for the same reason.
    was = mod.WIRE_ARCMAX
    try:
        mod.WIRE_ARCMAX = 64
        refuse("an archive over WIRE_ARCMAX", curated,
               "the record's WC_SIZE is checked against Content-Length before "
               "the body arrives, so an archive over the bound has no path "
               "through the machine at all")
    finally:
        mod.WIRE_ARCMAX = was
    check(mod.WIRE_ARCMAX == S_ARCMAX,
          "WIRE_ARCMAX is 0x%X" % S_ARCMAX,
          "the poke above is restored, and the bound the tool actually ships "
          "with is the one SPEC.md 88.2 gives", got=mod.WIRE_ARCMAX,
          want=S_ARCMAX)

    # ...and the CLI reports a refusal as an exit code, which is what the
    # Makefile and the website's build read. Once is enough: everything above
    # is the same `Refused` reaching the same handler.
    r = run("--archive", os.path.join(tmp, "bad.WPK"),
            "--srcdir", os.path.join(root, "bad83"))
    check(r.returncode != 0 and "refused" in r.stderr,
          "--archive says why it refused and exits non-zero",
          "a packer that refuses on stdout with exit 0 is a build that goes "
          "green with no archive in it",
          got=(r.stdout + r.stderr).strip() or "exit 0")

    # ...and a curated order that obeys both rules is ACCEPTED, over the CLI,
    # because a writer that refuses everything proves nothing about the two
    # refusals above.
    open(order, "w").write("DOCS/A.TXT\nDOCS/B.TXT\nF.TXT\nHELLO.O88\n")
    r = run("--archive", os.path.join(tmp, "cur.WPK"), "--srcdir", curated,
            "--program", "HELLO.O88", "--order", order)
    check(r.returncode == 0, "...and accepts a curated order that obeys both",
          got=(r.stdout + r.stderr).strip(), want="exit 0")
    r = run("--verify", os.path.join(tmp, "cur.WPK"))
    check(r.returncode == 0, "...and the archive it wrote verifies",
          got=(r.stdout + r.stderr).strip(), want="exit 0")


def main():
    for f in ("hello.o88", "mines.o88"):
        if not os.path.exists(os.path.join(BUILD, f)):
            print("t_wire: no build/%s - run `make` first" % f)
            sys.exit(1)
    mirror(WCAT, 25, CAT_PINS, "88.2")
    mirror(WARC, 18, ARC_PINS, "88.13")
    icons()
    mod = tool()
    lzss(mod)
    decoder_refusals(mod)
    with tempfile.TemporaryDirectory() as tmp:
        roundtrip(tmp)
        refusals(tmp)
        corrupt(tmp)
        picture(tmp)
        wpk = archive_roundtrip(tmp)
        if wpk:
            archive_corrupt(mod, wpk)
            archive_record(tmp, wpk, mod)
        archive_refusals(tmp, mod)
    done("t_wire")


if __name__ == "__main__":
    main()
