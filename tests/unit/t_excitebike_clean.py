#!/usr/bin/env python3
"""EXCITEBIKE carries NOTHING from a NES ROM or its disassembly (SPEC.md 102.2).

    python3 tests/unit/t_excitebike_clean.py

The art and audio policy (docs/plans/EXCITEBIKE-PLAN.md section 0): every
sprite, tile, font, splash and sound is original work committed under
apps/excitebike/, and a plain `make excitebikedisk` on a machine that has never
seen the reference directory must build the whole game.  That is a statement
about PROVENANCE, and provenance is exactly the thing that erodes one
convenient import at a time - a CHR file read "just for the placeholder", an
absolute path into ../NES-Games-Disassembly left in a tool.  This gate holds it:

  1. no file the game owns names the ROM, its CHR data, its disassembly's
     files, the replay format, or an absolute path into the reference tree.
     The ONLY exceptions are tests/excitebike_ref.py and tools/exboracle/
     (the optional oracle, which reads the reference at TEST time, only through
     the EXCITEBIKE_REF environment variable, and skips when it is absent);
  2. the build tools use the standard library only - a Pillow import would
     make `make` depend on a package the policy says it must not need;
  3. the Makefile's Excitebike block does not mention the reference either;
  4. apps/excitebike/ holds text only: no ROM, no CHR, no binary blob.

Broken on purpose - `open("CHR_ROM.chr")` added to tools/excitebike_assets.py -
check 1 fails naming the file and the word.
"""
import ast
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                           # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
THIS = os.path.abspath(__file__)

# the words a file of ours may not contain (case-sensitive where the source is)
FORBIDDEN = [
    ("CHR_ROM", "the ROM's character data"),
    ("CHR-ROM", "the ROM's character data"),
    ("bank_FF", "the disassembly's code bank"),
    ("bank_ram", "the disassembly's RAM map"),
    ("bank_val", "the disassembly's value tables"),
    (".fm2", "a replay movie of the reference game"),
    ("NES-Games-Disassembly", "a path into the reference tree"),
    ("EXCITEBIKE_SOURCE", "the retired import knob"),
    ("EXCITEBIKE_REF", "the test-time oracle's variable"),
]
# ...which only these may mention (the optional oracle)
ORACLE_OK = ("tests/excitebike_ref.py", "tools/exboracle/", "tests/excitebike_ref_deviations.txt")
STDLIB_ONLY = ("argparse", "hashlib", "json", "math", "os", "re", "shutil", "struct",
               "subprocess", "sys", "tempfile", "zlib", "random", "excitebike_audio",
               "excitebike_art", "excitebike_assets", "excitebike_tracks", "exbsim", "importlib",
               "fractions")
TEXT_EXT = (".asm", ".inc", ".txt", ".json", ".md", ".trk", ".mml", ".py")


def owned_files():
    out = []
    for base in ("apps/excitebike", "tools", "tests"):
        top = os.path.join(ROOT, base)
        for dp, dns, fns in os.walk(top):
            rel = os.path.relpath(dp, ROOT)
            # Python's own bytecode cache is a by-product of running the
            # game's tools (excitebikeaudiohost imports excitebike_audio),
            # not an asset - without this, every second `make` fails here.
            dns[:] = [d for d in dns if d != "__pycache__"]
            if base != "apps/excitebike" and "excitebike" not in rel and dp != top:
                dns[:] = [d for d in dns if "excitebike" in d or "exb" in d]
            for fn in fns:
                r = os.path.join(rel, fn).replace(os.sep, "/")
                if base == "apps/excitebike" or "excitebike" in fn or fn.startswith("exb"):
                    out.append(r)
    return sorted(set(out))


def main():
    files = owned_files()
    check(len(files) >= 8, "the provenance gate found the game's files",
          why="a walk that finds nothing passes vacuously", got=len(files), want=">= 8")
    bins = []
    for r in files:
        if os.path.abspath(os.path.join(ROOT, r)) == THIS:
            continue
        p = os.path.join(ROOT, r)
        if not r.endswith(TEXT_EXT):
            bins.append(r)
            continue
        try:
            text = open(p, encoding="utf-8").read()
        except UnicodeDecodeError:
            bins.append(r)
            continue
        oracle = any(r == o or r.startswith(o) for o in ORACLE_OK)
        for word, what in FORBIDDEN:
            if word in text and not (oracle and word == "EXCITEBIKE_REF"):
                if oracle:
                    continue
                check(False, "%s mentions %s (%s)" % (r, word, what),
                      why="EXCITEBIKE's art and audio are original and committed; the "
                          "reference tree may be read only by the optional oracle test")
        if r.startswith("tools/excitebike_") and r.endswith(".py"):
            tree = ast.parse(text)
            for n in ast.walk(tree):
                mods = []
                if isinstance(n, ast.Import):
                    mods = [a.name.split(".")[0] for a in n.names]
                elif isinstance(n, ast.ImportFrom) and n.module:
                    mods = [n.module.split(".")[0]]
                for m in mods:
                    check(m in STDLIB_ONLY, "%s imports %s" % (r, m),
                          why="the compilers are standard-library only: `make` must not "
                              "need Pillow or any other package for this game")
    check(not bins, "apps/excitebike and the game's tools/tests hold text only",
          why="a binary blob here is an asset with no source of record", got=bins, want=[])
    # the art sources, the tracks and the audio are all present as text
    for need in ("art/palette.json", "art/tiles.txt", "art/pieces.txt", "art/top.txt",
                 "art/poses.txt", "art/font.txt", "art/splash.json", "art/README.md",
                 "tracks/t1.trk", "tracks/t2.trk", "audio/sfx.txt"):
        check(os.path.exists(os.path.join(ROOT, "apps/excitebike", need)),
              "apps/excitebike/%s is committed" % need,
              why="the source of record for the art must be in the tree, not fetched")
    # the Makefile block
    mk = open(os.path.join(ROOT, "Makefile"), encoding="utf-8").read()
    i = mk.find("# Native Excitebike")
    check(i >= 0, "the Makefile has an Excitebike block")
    block = mk[i:] if i >= 0 else ""
    for word, what in FORBIDDEN:
        check(word not in block, "the Makefile's Excitebike block mentions %s" % word,
              why="a plain `make excitebikedisk` reads no reference directory")
    done("t_excitebike_clean")


if __name__ == "__main__":
    main()
