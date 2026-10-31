#!/usr/bin/env python3
"""w16reach - how far each Windows program is from running in WIN16
(docs/plans/WIN16-ANY-PLAN.md).

    python3 tools/w16reach.py DIR...            a row per program and library
    python3 tools/w16reach.py --missing N DIR   ...and the N imports most
                                                programs call that WIN16 lacks

For each .EXE/.DLL/.SCR/.CPL under each DIR (not recursive), tools/os88ne.py's
census against apps/win16/w16api.inc's export rows: the file's size, the
Windows version it expects, the CPU tier its code reaches, its imports and
how many WIN16 does not export, its segments and how many are discardable,
and which of the plan's R rows stand in its way:

    R2   the file is over 64 KB
    R3   it is marked protected-mode only
    R4   it imports a module WIN16 does not provide (named)
    lib  it is a library rather than a program

A missing import is not a refusal (R1, taken in S5): the program runs until
it calls one, and is then ended with its name. So `miss` is how much of
USER, GDI and KERNEL a program MIGHT reach, not how much it does. The plan's
stories re-run this after each wave; the programs are the user's own and
never enter the tree.
"""
import argparse
import collections
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXTS = (".EXE", ".DLL", ".SCR", ".CPL")


def provided():
    """(module names, {(module, ordinal)}) out of w16api.inc's rows"""
    src = open(os.path.join(ROOT, "apps", "win16", "w16api.inc")).read()
    head = src.split("--- the export rows")[0]
    mods = re.findall(r"db \d+, '(\w+)'", head)
    ids = {"W16M_" + n: n for n in mods}
    rows = set()
    for m in re.finditer(r"^W16EXPORT (\w+),\s*(\d+),", src, re.M):
        if m.group(1) in ids:                   # 0xFB-0xFE: WIN16's own rows
            rows.add((ids[m.group(1)], int(m.group(2))))
    return mods, rows


def census(path):
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88ne.py"),
                        "--json", path], capture_output=True, text=True)
    try:
        x = json.loads(r.stdout)
    except ValueError:
        return None
    x = x[0] if isinstance(x, list) else x
    return x if x and "imports" in x else None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--missing", type=int, default=0, metavar="N")
    a = ap.parse_args()
    mods, rows = provided()
    print("WIN16 exports %d functions from %s" % (len(rows), ", ".join(mods)))
    print("%-13s %7s %5s %-11s %4s %4s %3s %3s  %s"
          % ("file", "bytes", "ver", "cpu", "imp", "miss", "seg", "dis", "stands in the way"))
    missing = collections.Counter()
    for d in a.dirs:
        print("-- %s" % d)
        for f in sorted(os.listdir(d)):
            if not f.upper().endswith(EXTS):
                continue
            x = census(os.path.join(d, f))
            if x is None:
                continue
            imps = {(i["module"], i["ordinal"]): i.get("name") for i in x["imports"]}
            miss = [k for k in imps if k not in rows]
            for k in miss:
                missing[(k[0], k[1], imps[k])] += 1
            ext = [m for m in x["modules"] if m not in mods]
            why = (["R2"] if x["bytes"] > 65536 else []) \
                + (["R3"] if x["pmode_only"] else []) \
                + (["R4:" + "/".join(ext)] if ext else []) \
                + (["lib"] if x["library"] else [])
            segs = x["segments"]
            print("%-13s %7d %5s %-11s %4d %4d %3d %3d  %s"
                  % (f, x["bytes"], x["expver"], x["verdict"], len(imps), len(miss),
                     len(segs), sum(1 for s in segs if s["flags"] & 0x1000),
                     " ".join(why) or "-"))
    if a.missing:
        print("\nthe imports most programs call that WIN16 lacks (%d distinct):"
              % len(missing))
        for (m, o, n), c in missing.most_common(a.missing):
            print("  %3d  %s.%d %s" % (c, m, o, n or ""))


if __name__ == "__main__":
    main()
