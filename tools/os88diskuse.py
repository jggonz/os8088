#!/usr/bin/env python3
"""os88diskuse.py - what every file costs the 360KB floppies, in CLUSTERS.

The disk-side companion to tools/os88pkgsize.py. That tool weighs a package
by image + bss, which is what it costs a MACHINE (SPEC.md 20.1); this one
weighs it by what it costs a DISK, which is a different number for three
reasons: the file is LZ4-packed (SPEC.md 20.13.5) so a byte of zero-filled
table costs almost nothing while a byte of code costs a byte, bss costs no
disk at all, and the disk is billed in whole clusters - 1,024 bytes on every
360KB volume - once per COPY, and the core packages ride several disks.

Written for the second apps size pass (docs/plans/HANDOFF-APPS-SIZE-P2.md),
whose subject is disk room on the system disk and the 360KB set.

    os88diskuse.py                       every 360KB image `all` and `small`
                                         build, one table each
    os88diskuse.py IMG...                those images
    os88diskuse.py --pkg PAINT --pkg DOS only files whose name starts so
    os88diskuse.py --save before.json    ...and keep a snapshot
    os88diskuse.py --diff before.json    what moved since the snapshot:
                                         bytes, clusters, free clusters

It reads images; it never writes one. Every number is counted.
"""
import argparse
import json
import os
import signal
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from os88fat import Fat12  # noqa: E402

DEFAULT = ["build/os8088-360.img", "build/apps360.img", "build/media360.img",
           "build/office360.img", "build/network360.img", "build/games360.img",
           "build/small360.img", "build/smallapps360.img"]


def _walk(v, ents, path, out):
    for e in ents:
        if e[0] in (0, 0xE5) or e[11] & 0x08 or e[11] == 0x0F:
            continue
        name = Fat12.pretty(e[:11])
        if name in (".", ".."):
            continue
        first = struct.unpack_from("<H", e, 26)[0]
        size = struct.unpack_from("<I", e, 28)[0]
        chain = v.chain(first) if first else []
        if e[11] & 0x10:
            out[path + name + "/"] = [0, len(chain)]
            csz = v.spc * v.bps
            sub = []
            for c in chain:
                base = v.cluster_off(c)
                sub += [bytes(v.img[base + k * 32:base + k * 32 + 32])
                        for k in range(csz // 32)]
            _walk(v, sub, path + name + "/", out)
        else:
            out[path + name] = [size, len(chain)]


def survey(img):
    v = Fat12(img)
    files = {}
    _walk(v, [e for _, _, e in v.entries()], "", files)
    return {"free": len(v.free_clusters()), "clusters": v.nclus,
            "csize": v.spc * v.bps, "files": files}


def want(path, pkgs):
    base = path.rstrip("/").split("/")[-1]
    return not pkgs or any(base.startswith(p.upper()) for p in pkgs)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("images", nargs="*")
    ap.add_argument("--pkg", action="append", default=[])
    ap.add_argument("--save")
    ap.add_argument("--diff")
    a = ap.parse_args()
    imgs = a.images or [i for i in DEFAULT if os.path.exists(i)]
    now = {os.path.basename(i): survey(i) for i in imgs}
    if a.save:
        with open(a.save, "w") as f:
            json.dump(now, f, indent=1, sort_keys=True)
    if a.diff:
        old = json.load(open(a.diff))
        tot_free = 0
        for img, s in now.items():
            o = old.get(img)
            if o is None:
                print("== %s: not in %s" % (img, a.diff))
                continue
            rows = []
            for p in sorted(set(s["files"]) | set(o["files"])):
                if p.endswith("/") or not want(p, a.pkg):
                    continue
                nb, nc = s["files"].get(p, [0, 0])
                ob, oc = o["files"].get(p, [0, 0])
                if (nb, nc) != (ob, oc):
                    rows.append("  %-26s %8d -> %8d  (%+7d B)  %3d -> %3d cl  (%+d)"
                                % (p, ob, nb, nb - ob, oc, nc, nc - oc))
            df = s["free"] - o["free"]
            tot_free += df
            print("== %s: free %d -> %d clusters (%+d)" % (img, o["free"], s["free"], df))
            print("\n".join(rows) if rows else "  (no file changed)")
        print("total: %+d free cluster(s) across %d image(s)" % (tot_free, len(now)))
        return
    for img, s in now.items():
        print("== %s: %d of %d cluster(s) free, %d-byte clusters"
              % (img, s["free"], s["clusters"], s["csize"]))
        for p, (b, c) in sorted(s["files"].items(), key=lambda kv: (-kv[1][1], kv[0])):
            if not p.endswith("/") and want(p, a.pkg):
                print("  %4d cl %8d  %s" % (c, b, p))


if __name__ == "__main__":
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)   # `| head` ends quietly
    main()
