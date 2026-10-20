#!/usr/bin/env python3
"""isoTron carries NOTHING from the game it remakes (SPEC.md 108.1).

    python3 tests/unit/t_isotron_clean.py
    python3 tests/unit/t_isotron_clean.py --selftest

The provenance policy (docs/plans/ISOTRON-PLAN.md section 0, the owner's
decision): isoTron is a remake from OBSERVED RULES. The reference footage, the
original executable and the scouts' working files live OUTSIDE the tree, and
no file the game owns may name them, read them or carry their words:

  1. no file the game owns names the reference folder, the original
     executable or an absolute path into either - the game's sources under
     apps/isotron/, every tools/ and tests/ file with "isotron" in its name,
     and the WHOLE Makefile (not only its isoTron block: a rule anywhere in
     it could read the folder), in any case. That is what keeps `make` and
     every test from reading anything outside the tree;
  2. apps/isotron/ - the part that SHIPS - never carries the original's name,
     its publisher's, or a copyright line (there is no copyright line
     anywhere, by the owner's decision);
  3. the tools the game's tests and build run are standard-library only;
  4. apps/isotron/ is text only: the art is procedural, so a binary blob
     there is an asset with no source of record.

--selftest is the NEGATIVE CONTROL (docs/WRITING-TESTS.md 1): it copies the
game's files and the Makefile into a temporary tree, injects a forbidden
token into a copy of the package source and into the copy of the Makefile
OUTSIDE its isoTron block, the executable's name in a casing the list does not
spell, and a copyright line into the shipped README, runs the same scan over
that tree and must see it FAIL naming every one.
It exits 0 only when the gate caught the injection.
"""
import ast
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                           # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
THIS = os.path.abspath(__file__)

# the words NO file the game owns, and nowhere in the Makefile, may contain
# (IN ANY CASE - a file system that folds case reads Novatron.EXE as well): the
# reference folder's name and the original's executable. Spelled in two
# halves so a plain grep of the tree for them finds only the gate's own text.
FORBIDDEN = [
    ("isotron" + "-ref", "the reference folder outside the tree"),
    ("novatron" + ".exe", "the original executable"),
]
# ...and the words the SHIPPED package may not carry, in any case
SHIP_FORBIDDEN = [
    ("nova" + "tron", "the original's name"),
    ("veri" + "soft", "the original's publisher"),
    ("copy" + "right", "a copyright line (there is none, by the owner's decision)"),
    ("(" + "c)", "a copyright sign"),
]
STDLIB_ONLY = ("argparse", "hashlib", "json", "os", "struct", "sys", "subprocess",
               "tempfile", "re", "shutil", "time", "zlib", "math", "random",
               "collections", "itertools", "pathlib")
TEXT_EXT = (".asm", ".inc", ".md", ".txt", ".py", ".json")
MK_START = "# isoTron (SPEC.md 108)"
MK_END = "\n# REDLINE native"


def owned_files(root):
    """The files the game owns, relative to root."""
    out = []
    app = os.path.join(root, "apps", "isotron")
    for dp, dns, fns in os.walk(app):
        dns[:] = [d for d in dns if d != "__pycache__"]
        for fn in fns:
            out.append(os.path.relpath(os.path.join(dp, fn), root).replace(os.sep, "/"))
    for base in ("tools", "tests", os.path.join("tests", "unit")):
        top = os.path.join(root, base)
        if not os.path.isdir(top):
            continue
        for fn in os.listdir(top):
            if "isotron" in fn.lower() and os.path.isfile(os.path.join(top, fn)):
                out.append(os.path.join(base, fn).replace(os.sep, "/"))
    return sorted(set(out))


def makefile_block(root):
    mk = open(os.path.join(root, "Makefile"), encoding="utf-8").read()
    i = mk.find(MK_START)
    if i < 0:
        return None
    j = mk.find(MK_END, i)
    return mk[i:j if j > 0 else len(mk)]


def scan(root, this=THIS):
    """Every problem in the tree at root, as (file, word, what) - empty is clean."""
    bad = []
    files = owned_files(root)
    for r in files:
        p = os.path.join(root, r)
        if os.path.abspath(p) == this:
            continue
        if not r.endswith(TEXT_EXT):
            bad.append((r, "<binary>", "a file with no text source of record"))
            continue
        try:
            text = open(p, encoding="utf-8").read()
        except UnicodeDecodeError:
            bad.append((r, "<binary>", "a file that is not UTF-8 text"))
            continue
        low = text.lower()
        for word, what in FORBIDDEN:
            if word in low:
                bad.append((r, word, what))
        if r.startswith("apps/isotron/"):
            low = text.lower()
            for word, what in SHIP_FORBIDDEN:
                if word in low:
                    bad.append((r, word, what))
        if r.endswith(".py"):
            try:
                tree = ast.parse(text)
            except SyntaxError as e:
                bad.append((r, "<syntax>", str(e)))
                continue
            if r.startswith("tools/"):
                for n in ast.walk(tree):
                    mods = []
                    if isinstance(n, ast.Import):
                        mods = [a.name.split(".")[0] for a in n.names]
                    elif isinstance(n, ast.ImportFrom) and n.module and not n.level:
                        mods = [n.module.split(".")[0]]
                    for m in mods:
                        if m not in STDLIB_ONLY:
                            bad.append((r, "import " + m, "a tool that needs a package "
                                        "beyond Python's standard library"))
    if makefile_block(root) is None:
        bad.append(("Makefile", MK_START, "the isoTron block is missing"))
    mk = open(os.path.join(root, "Makefile"), encoding="utf-8").read().lower()
    for word, what in FORBIDDEN:
        if word in mk:
            bad.append(("Makefile", word, what))
    return files, bad


def selftest():
    """Inject a token into a copy of the tree and require the scan to see it."""
    with tempfile.TemporaryDirectory() as td:
        files, _ = scan(ROOT)
        for r in files + ["Makefile"]:
            dst = os.path.join(td, r)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(os.path.join(ROOT, r), dst)
        token = FORBIDDEN[0][0]
        src = os.path.join(td, "apps", "isotron", "isotron.asm")
        with open(src, "a", encoding="utf-8") as f:
            f.write("\n; %%include \"../../%s/frames.inc\"\n" % token)
        mk = os.path.join(td, "Makefile")
        with open(mk, "a", encoding="utf-8") as f:   # OUTSIDE the isoTron block
            f.write("\n# reads ../%s/\n" % token)
        exe = FORBIDDEN[1][0]
        mixed = exe[:4].upper() + exe[4:-3] + exe[-3:].upper()   # e.g. NOVAtron.EXE
        tool = os.path.join(td, "tools", "isotronsim.py")
        with open(tool, "a", encoding="utf-8") as f:
            f.write("\n# %s\n" % mixed)
        shipped = os.path.join(td, "apps", "isotron", "README.md")
        with open(shipped, "a", encoding="utf-8") as f:
            f.write("\n" + SHIP_FORBIDDEN[2][0].capitalize() + " 1982\n")
        _, bad = scan(td, this=os.path.join(td, "tests", "unit", "t_isotron_clean.py"))
    hits = {(r, w) for r, w, _ in bad}
    want = {("apps/isotron/isotron.asm", token), ("Makefile", token),
            ("tools/isotronsim.py", exe),
            ("apps/isotron/README.md", SHIP_FORBIDDEN[2][0])}
    missed = want - hits
    if missed:
        print("t_isotron_clean --selftest: FAILED - the injected tokens were NOT caught: %r"
              % sorted(missed))
        return 1
    print("t_isotron_clean --selftest: the gate refused all %d injections (%s)"
          % (len(want), ", ".join("%s in %s" % (w, r) for r, w in sorted(want))))
    return 0


def main():
    if "--selftest" in sys.argv[1:]:
        sys.exit(selftest())
    files, bad = scan(ROOT)
    check(len(files) >= 8, "the provenance gate found the game's files",
          why="a walk that finds nothing passes vacuously", got=len(files), want=">= 8")
    for need in ("apps/isotron/isotron.asm", "apps/isotron/README.md", "tools/isotronsim.py"):
        check(need in files, "%s is one of the game's files" % need,
              why="the walk has to see the package and the model it is held to")
    for r, word, what in bad:
        check(False, "%s carries %s (%s)" % (r, word, what),
              why="isoTron is a remake from observed rules: the reference stays outside "
                  "the tree, and `make` and every test read nothing from it")
    done("t_isotron_clean")


if __name__ == "__main__":
    main()
