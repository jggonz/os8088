#!/usr/bin/env python3
"""The Wire's catalog: the tool's round trip, its refusals, and the mirror.

    python3 tests/unit/t_wire.py

THREE THINGS, and the third is the one that cannot be got by reading either
file on its own.

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
S_WF_DISK, S_WF_PIC, S_WF_NEW, S_WF_FLOPPY = 1, 2, 4, 8


def run(*args):
    return subprocess.run([sys.executable, TOOL] + list(args), cwd=ROOT,
                          capture_output=True, text=True)


# =============================================================================
# 3. THE MIRROR
# =============================================================================
def mirror():
    """Every name defined in BOTH files must say the same number."""
    asm, py = {}, {}
    pat = re.compile(r"^\s*([A-Z][A-Z0-9_]*)\s+equ\s+([^;]+?)\s*(?:;.*)?$")
    for line in open(WCAT):
        m = pat.match(line)
        if not m:
            continue
        name, expr = m.group(1), m.group(2).strip()
        # The only expression forms this file uses are a literal and one
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

    shared = sorted(set(asm) & set(py))
    check(len(shared) >= 25,
          "wcat.inc and os88wire.py share %d constants" % len(shared),
          "the mirror is self-maintaining, so a low count means the SCAN "
          "broke (a renamed equ, a reformatted assignment) rather than that "
          "the format got smaller - and a scan that finds nothing passes",
          got=len(shared), want=">= 25")
    for n in shared:
        eq(py[n], asm[n],
           "%s: apps/thewire/wcat.inc says %d, tools/os88wire.py says %d"
           % (n, asm[n], py[n]),
           "the catalog format lives on both sides of a wire and there is no "
           "linker: a half-applied change assembles and packs perfectly, and "
           "the machine then reads a record at the wrong offset (SPEC.md 88.2)")

    # ...and both must carry the whole of what SPEC.md 88.2 pins by number.
    for n, want in (("WIRE_HDR", S_HDR), ("WIRE_REC", S_REC),
                    ("WIRE_SC", S_SC), ("WIRE_VER", S_VER),
                    ("WIRE_CATMAX", S_CATMAX), ("WIRE_FILEMAX", S_FILEMAX),
                    ("WC_DESCN", S_DESCN), ("WC_DESCW", S_DESCW),
                    ("WF_DISK", S_WF_DISK), ("WF_PIC", S_WF_PIC),
                    ("WF_NEW", S_WF_NEW), ("WF_FLOPPY", S_WF_FLOPPY)):
        eq(asm.get(n), want,
           "wcat.inc's %s is %s and SPEC.md 88.2 says %d"
           % (n, asm.get(n), want),
           "both files could agree on a number the SPEC does not say")


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
             "icon": blob[o + 48:o + 112]}
        assert blob[o + 46:o + 48] == b"\0\0", "%s: +46 is not zero" % r["stem"]
        assert blob[o + 252:o + 256] == b"\0" * 4, "%s: +252" % r["stem"]
        assert r["tier"] <= 3, "%s: tier %d" % (r["stem"], r["tier"])
        assert r["nside"] <= 8, "%s: %d sidecars" % (r["stem"], r["nside"])
        assert r["side0"] + r["nside"] <= scn, "%s: sidecar range" % r["stem"]
        assert r["total"] >= r["size"], "%s: total < size" % r["stem"]
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


def main():
    for f in ("hello.o88", "mines.o88"):
        if not os.path.exists(os.path.join(BUILD, f)):
            print("t_wire: no build/%s - run `make` first" % f)
            sys.exit(1)
    mirror()
    icons()
    with tempfile.TemporaryDirectory() as tmp:
        roundtrip(tmp)
        refusals(tmp)
        corrupt(tmp)
        picture(tmp)
    done("t_wire")


if __name__ == "__main__":
    main()
