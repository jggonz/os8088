#!/usr/bin/env python3
"""os88vmzip.py - pack vm/ profiles into a zip of ready-to-run 86Box machines.

    os88vmzip.py -o build/os8088-videos.zip --top os8088-videos \
        --from build --readme apps/video/demo/86BOX.TXT \
        vm/viddemo-vga vm/viddemo-cga vm/viddemo-herc

Each profile becomes one folder in the zip, named for its vm/ directory, and
holding exactly what 86Box wants in a machine folder: its 86box.cfg, nvr/
(copied from the profile's tracked nvr-seed/, because vm/*/nvr/ is the
running machine's and is gitignored) and every disk image the config names.
A user drops the folders into their 86Box machines directory and starts one.

The config is the contract: every image key (`*_fn`, `cdrom_*_image_path`)
must be a BARE file name - 86Box resolves one against the machine's own
folder - and the file must be in --from. Anything else is refused, because
a path that works in this tree is a path to nowhere on someone else's disk.
So is a remembered window (`window_remember`, `window_coordinates`): it is a
place on the screen of whoever saved the config, and without one 86Box puts
the window's title bar off the top of the screen.
The zip is deterministic (fixed timestamps, sorted entries), like every
other artefact `make` writes.
"""
import argparse
import os
import sys
import zipfile

STAMP = (1980, 1, 1, 0, 0, 0)


def images(cfg):
    """The image file names a config names, refusing any that are paths."""
    out = []
    for line in open(cfg, encoding="utf-8"):
        k, _, v = line.partition("=")
        k, v = k.strip(), v.strip()
        # A remembered window is a position on the screen of whoever saved
        # the config, and with no coordinates 86Box puts the CLIENT area at
        # 0,0 - the title bar off the top of the screen, and the window
        # unmovable. A shipped machine lets the window manager place it.
        if k in ("window_remember", "window_coordinates") and v not in ("", "0"):
            raise SystemExit("os88vmzip: %s: %s = %s; a shipped machine does not "
                             "remember a window position" % (cfg, k, v))
        if not v or "image_history" in k:
            continue
        if not (k.endswith("_fn") or (k.startswith("cdrom_") and k.endswith("_image_path"))):
            continue
        if v.startswith("wp://"):
            v = v[5:]
        if "/" in v or "\\" in v:
            raise SystemExit("os88vmzip: %s: %s = %s is a path; a shipped machine names "
                             "its disk by bare file name, beside the config" % (cfg, k, v))
        out.append(v)
    return out


def add(z, arc, path):
    zi = zipfile.ZipInfo(arc, STAMP)
    zi.compress_type = zipfile.ZIP_DEFLATED
    zi.external_attr = 0o644 << 16
    with open(path, "rb") as f, z.open(zi, "w", force_zip64=True) as w:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            w.write(b)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--top", required=True, help="the zip's one top-level folder")
    ap.add_argument("--from", dest="src", required=True, help="where the disk images are")
    ap.add_argument("--readme", help="a text file put at the top as README.TXT")
    ap.add_argument("profiles", nargs="+")
    a = ap.parse_args()

    plan = []
    for prof in a.profiles:
        name = os.path.basename(os.path.normpath(prof))
        cfg = os.path.join(prof, "86box.cfg")
        plan.append((name + "/86box.cfg", cfg))
        seed = os.path.join(prof, "nvr-seed")
        if os.path.isdir(seed):
            for n in sorted(os.listdir(seed)):
                plan.append((name + "/nvr/" + n, os.path.join(seed, n)))
        for img in images(cfg):
            p = os.path.join(a.src, img)
            if not os.path.isfile(p):
                raise SystemExit("os88vmzip: %s names %s, which is not in %s" % (cfg, img, a.src))
            plan.append((name + "/" + img, p))
    if a.readme:
        plan.insert(0, ("README.TXT", a.readme))

    tmp = a.out + ".tmp"
    with zipfile.ZipFile(tmp, "w", allowZip64=True) as z:
        for arc, path in plan:
            add(z, a.top + "/" + arc, path)
    os.replace(tmp, a.out)
    for arc, path in plan:
        print("  %-48s %12d" % (a.top + "/" + arc, os.path.getsize(path)))
    print("os88vmzip: %s, %d bytes" % (a.out, os.path.getsize(a.out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
