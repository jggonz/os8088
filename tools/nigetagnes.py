#!/usr/bin/env python3
"""nigetagnes: fetch agnes, the whole-ROM ORACLE (SPEC.md 91.14.4).

    python3 tools/nigetagnes.py -o build/agnes          # fetch + verify
    python3 tools/nigetagnes.py -o build/agnes --check   # verify, never fetch

agnes is a small, complete, MIT-licensed NES emulator that runs on the host.
`apps/infones/hosttest/niagnes.c` `#include`s its .c to reach the PPU's
internals - the loopy `v` at dot 0 of every visible line, which its public API
does not offer and which a SCANLINE model cannot be checked without - runs a
real ROM, and records the per-line scroll history beside agnes's own 256x240
screen. `niuitest --replay` then drives the port's C compositor with that
state and requires the picture to come out identical.

NOTHING IS VENDORED (CONTRIBUTING.md 6, SPEC.md 91.1). Two files are fetched
into build/, which is gitignored, at a PINNED COMMIT and checked by SHA-256 -
tools/nigetroms.py's shape, one repository along. The MIT notice is reproduced
verbatim in niagnes.c, which is what the licence asks for.

IT IS NOT IN apps/infones/build.sh AND MUST NOT BE. That script runs inside
`make infones`, and a fresh clone's build must neither stall on a network nor
fail without one (apps/c64/build.sh's header says so in capitals). `make
niagnes` is the on-demand target, beside `make nicputest` and `make nisystest`.
"""
import argparse
import hashlib
import os
import sys
import urllib.request

# agnes at kgabis/agnes, commit 0e4220b084c467e39c04805d955e78c463feadd0.
# THE COMMIT IS THE PIN AND THE HASH IS THE CHECK: a tag can move and a branch
# certainly does, so the URL names the commit and the digest is compared
# before anything is written where a build can see it.
COMMIT = "0e4220b084c467e39c04805d955e78c463feadd0"
BASE = "https://raw.githubusercontent.com/kgabis/agnes/%s/" % COMMIT
FILES = {
    "agnes.c": "2a8ff8770cc4fd1dacaa17b4841e7344fc1e458aba66351ee46e8423e7af618f",
    "agnes.h": "a595b12240134ab46861562303a62f19e0e2796ae2ff0b71b5169d98425d45a1",
}


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        h.update(f.read())
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-o", "--out", default="build/agnes")
    ap.add_argument("--check", action="store_true",
                    help="verify what is there and never fetch")
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    bad = 0
    for name, want in sorted(FILES.items()):
        path = os.path.join(a.out, name)
        if os.path.exists(path) and digest(path) == want:
            print("nigetagnes: %s ok" % path)
            continue
        if a.check:
            print("nigetagnes: %s is missing or does not match its pin" % path)
            bad = 1
            continue
        url = BASE + name
        print("nigetagnes: fetching %s" % url)
        try:
            data = urllib.request.urlopen(url, timeout=60).read()
        except Exception as e:                          # noqa: BLE001
            print("nigetagnes: %s: %s" % (url, e))
            print("nigetagnes: `make niagnes` needs a network ONCE; nothing"
                  " else in this tree does, and `make infones` does not.")
            return 1
        got = hashlib.sha256(data).hexdigest()
        if got != want:
            print("nigetagnes: %s hashed %s, want %s - REFUSING to write it"
                  % (name, got, want))
            return 1
        with open(path, "wb") as f:
            f.write(data)
        print("nigetagnes: %s ok" % path)
    return bad


if __name__ == "__main__":
    sys.exit(main())
