#!/usr/bin/env python3
"""SPEC.md 100 - DrMarco's COMMITTED art must be what its compiler writes.

apps/drmario/art/native/ is build output kept in the tree: composing the two
PNGs needs Pillow and `make` is stdlib-only, so `make drmarco-art` runs that
half by hand and its result is committed (1942's and the logo video's
arrangement). Nothing on `make`'s path can notice a PNG edited without the
re-run, or a compiler change whose output was never committed - the package
would go on shipping the OLD art and every row would pass.

So this re-runs the Pillow half into a scratch directory and requires the
committed directory to hold exactly the same files, byte for byte, and no
others. A failure names the file; the cure is `make drmarco-art` and a commit.
"""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                                    # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
NATIVE = os.path.join(ROOT, "apps", "drmario", "art", "native")


def main():
    committed = sorted(os.listdir(NATIVE)) if os.path.isdir(NATIVE) else []
    check(len(committed) >= 10,
          "apps/drmario/art/native/ holds %d files - the committed art is "
          "missing, and every comparison below would be vacuous" % len(committed),
          why="`make drmarco-art` writes it (SPEC.md 100)")
    with tempfile.TemporaryDirectory() as td:
        fresh = os.path.join(td, "native")
        r = subprocess.run(
            [sys.executable, "tools/drmario_assets.py", "reference/drmario",
             os.path.join(td, "out"), "--art", fresh],
            cwd=ROOT, capture_output=True, text=True)
        check(r.returncode == 0, "tools/drmario_assets.py --art failed",
              got=(r.stderr or r.stdout).strip()[-400:])
        if r.returncode:
            return done("t_drmarcoart")
        made = sorted(os.listdir(fresh))
        check(made == committed,
              "the compiler writes a different SET of files than is committed",
              got=", ".join(committed), want=", ".join(made),
              why="run `make drmarco-art` and commit apps/drmario/art/native/")
        for name in made:
            want = open(os.path.join(fresh, name), "rb").read()
            path = os.path.join(NATIVE, name)
            got = open(path, "rb").read() if os.path.exists(path) else None
            check(got == want,
                  "apps/drmario/art/native/%s is not what the compiler writes" % name,
                  got=None if got is None else "%d bytes" % len(got),
                  want="%d bytes" % len(want),
                  why="a PNG or tools/drmario_assets.py changed without "
                      "`make drmarco-art`; re-run it and commit the result")
    done("t_drmarcoart")


if __name__ == "__main__":
    main()
