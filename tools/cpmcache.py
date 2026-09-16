#!/usr/bin/env python3
"""cpmcache: the COMMITTED copy of every CP/M file the RUNCPM disks and the
live media carry (SPEC.md 74.5, 74.6, 80.6).

    python3 tools/cpmcache.py --check        # every pin present and matching
    python3 tools/cpmcache.py --pack         # rebuild the zip from build/'s
                                             # verified caches (after a pin moves)

`tools/getruncpm.py` and `tools/getcpmsw.py` pin their inputs by SHA-256 and
used to download every one of them: RunCPM's CCP, licence, read-me and master
disk off GitHub at a pinned commit, and some eighty files of the RunCPM
software collection off Google Drive ONE REQUEST (and one virus-scan form) AT
A TIME. The Drive half is what made a clean `make live` slow, so both scripts
now read `apps/runcpm/cache/cpmcache.zip` before they touch the network. The
pins are unchanged and still checked on every byte that comes out of the zip,
so the zip is a transport, not a second source of truth: a member that does
not match its pin is refused exactly as a bad download is.

**COMMITTING THESE IS A USER-DECIDED DEPARTURE from CONTRIBUTING.md 6**, in
the shape `apps/c64/rom/` already took; `apps/runcpm/cache/README.md` records
it and says whose the files are.

The network is still the fallback for a file the zip does not hold - which is
what a moved pin looks like - and `--pack` is how the zip catches up: fetch
once with the two scripts, then pack. The zip is DETERMINISTIC (sorted
members, a fixed timestamp, fixed attributes), so repacking unchanged inputs
changes nothing git can see.

Member names:  runcpm/<path in RunCPM's repository>    (getruncpm.PINNED)
               cpmsw/<DRIVE>/<USER>/<NAME>             (getcpmsw.PINNED)
"""
import argparse
import hashlib
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ZIP = os.path.join(ROOT, "apps", "runcpm", "cache", "cpmcache.zip")
STAMP = (1980, 1, 1, 0, 0, 0)

_zf = None


def member(name):
    """The bytes of one member, or None when there is no zip or no such
    member. The caller checks the pin - this answers bytes, not trust."""
    global _zf
    if _zf is None:
        if not os.path.exists(ZIP):
            return None
        _zf = zipfile.ZipFile(ZIP)
    try:
        return _zf.read(name)
    except KeyError:
        return None


def pins():
    """{member name: (sha256, size)} over both scripts' PINNED tables."""
    sys.path.insert(0, HERE)
    import getcpmsw                                             # noqa: E402
    import getruncpm                                            # noqa: E402
    want = {}
    for path, (sha, size) in getruncpm.PINNED.items():
        want["runcpm/" + path] = (sha, size)
    for area, files in getcpmsw.PINNED.items():
        for name, (_, sha, size) in files.items():
            want[f"cpmsw/{area}/{name}"] = (sha, size)
    return want


def fail(msg):
    print(f"cpmcache: error: {msg}", file=sys.stderr)
    sys.exit(1)


def pack(runcpm_dir, cpmsw_dir):
    """Every pinned file out of the two scripts' own caches, into the zip."""
    want = pins()
    data = {}
    for name, (sha, size) in sorted(want.items()):
        if name.startswith("runcpm/"):
            p = os.path.join(runcpm_dir, ".artifacts", os.path.basename(name))
        else:
            p = os.path.join(cpmsw_dir, *name.split("/")[1:])
        if not os.path.exists(p):
            fail(f"no {p}: run tools/getruncpm.py and tools/getcpmsw.py first")
        with open(p, "rb") as fh:
            b = fh.read()
        if len(b) != size or hashlib.sha256(b).hexdigest() != sha:
            fail(f"{p} does not match its pin")
        data[name] = b
    os.makedirs(os.path.dirname(ZIP), exist_ok=True)
    tmp = ZIP + ".tmp"
    with zipfile.ZipFile(tmp, "w") as zf:
        for name in sorted(data):
            info = zipfile.ZipInfo(name, STAMP)
            info.external_attr = 0o644 << 16
            info.create_system = 3
            # A0.zip is already deflated: storing it saves the time and costs
            # nothing
            info.compress_type = (zipfile.ZIP_STORED if name.endswith(".zip")
                                  else zipfile.ZIP_DEFLATED)
            zf.writestr(info, data[name], compresslevel=9)
    os.replace(tmp, ZIP)
    total = sum(len(b) for b in data.values())
    print(f"cpmcache: {len(data)} files ({total} bytes) -> {ZIP} "
          f"({os.path.getsize(ZIP)} bytes)")


def check():
    """Answers a list of problems: a pin the zip lacks or contradicts, or a
    member no pin names (dead weight that nothing would ever verify)."""
    if not os.path.exists(ZIP):
        return [f"no {ZIP}"]
    want = pins()
    bad = []
    with zipfile.ZipFile(ZIP) as zf:
        have = set(zf.namelist())
        for name, (sha, size) in sorted(want.items()):
            if name not in have:
                bad.append(f"{name}: pinned, not in the zip")
                continue
            b = zf.read(name)
            if len(b) != size or hashlib.sha256(b).hexdigest() != sha:
                bad.append(f"{name}: does not match its pin")
        for name in sorted(have - set(want)):
            bad.append(f"{name}: in the zip, pinned by nothing")
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true",
                    help="verify the zip against both scripts' pins")
    ap.add_argument("--pack", action="store_true",
                    help="rebuild the zip from the scripts' caches in build/")
    ap.add_argument("--runcpm", default="build/runcpm-disk", metavar="DIR",
                    help="getruncpm.py's output directory (default build/runcpm-disk)")
    ap.add_argument("--cpmsw", default="build/cpmsw", metavar="DIR",
                    help="getcpmsw.py's output directory (default build/cpmsw)")
    args = ap.parse_args()
    if args.pack:
        pack(args.runcpm, args.cpmsw)
    if args.check or not args.pack:
        bad = check()
        for line in bad:
            print(f"cpmcache: {line}", file=sys.stderr)
        if bad:
            sys.exit(1)
        print(f"cpmcache: {len(pins())} files, every one matching its pin")


if __name__ == "__main__":
    main()
