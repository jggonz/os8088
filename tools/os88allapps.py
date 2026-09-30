#!/usr/bin/env python3
"""os88allapps: the everything payload on as many floppies as it needs (SPEC.md 19.10).

`make allapps` used to write ONE floppy per geometry, and the payload outgrew
it: 1942's folder alone is 709 of the 1.44MB disk's 2,847 clusters, and the
build failed with "packages need 3123 clusters; disk holds 2847". A set of
disks that grows by itself is the shape that does not run out, so this packs
the payload onto <prefix>-1.img, <prefix>-2.img, ... and adds a disk when the
payload needs one. Nobody edits a disk list when a program is added.

THE UNIT IS A TOP-LEVEL FOLDER, and a folder is never split between disks
while it fits on one. That is a correctness rule and not tidiness: WORD\\,
CWORD\\, RUNCPM\\, C64\\, APPLE2\\, WEAVE\\, LOOM\\, PACCMAN\\ and 1942\\ each
hold a program, its overlay or data files, and its documents, which all have
to sit in the folder the program is launched from (SPEC.md 19.10). A
COLLECTION folder (--collection: APPS, GAMES, MEDIA) is a set of independent
programs, and only when one outgrows a whole floppy is it split, between
same-stem groups so that GAMES\\DRMARCO.O88 keeps DRMARCO.VGA beside it. Any
other folder that outgrows a floppy is refused by name: it needs a decision.

PLACEMENT IS FIRST FIT IN PAYLOAD ORDER, so the set is a function of the
payload and nothing else. It is NOT stable against growth: a folder early in
the order that grows can push a later one onto another disk (DrMarco joining
GAMES\ moves RUNCPM\ off the first 1.44MB disk). CONTENTS.TXT on every disk
is the map a reader needs, and it is rewritten with the set.

RUNCPM'S DRIVE A IS PRICED WHOLE. On the single floppy it absorbed whatever
was left over, which is how it shrank to one file (SPEC.md 19.10.1). Here the
RUNCPM\\ unit costs its package PLUS the A\\0 fill that an otherwise empty disk
of the geometry would hold (tools/getruncpm.py select(), the same ranking and
the same save room). The fill on the disk it lands on is then priced against
that disk's real contents, and this refuses if it came out smaller than the
empty-disk fill.

Every disk carries the --folder folders (DOCS, SYSTEM\\APPDATA: SPEC.md 19.9)
and a CONTENTS.TXT that says which disk of the set holds what. The costs are
os88disk.py's own arithmetic: a file is its clusters, a folder's directory is
max(2 + entries, --dir-slots) slots rounded up to clusters, and ASSOC.DAT is
os88disk.build_assoc on that disk's packages. os88disk.py still builds and
verifies each disk, so a pricing mistake here fails the build rather than
shipping.

--list names the images one per line. It is what make, the release zip and
the 86Box machines read, and it is how a disk the set no longer needs gets
deleted rather than left behind looking current.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88disk                                                  # noqa: E402

TOOLS = os.path.dirname(os.path.abspath(__file__))
CONTENTS = "CONTENTS.TXT"
CONTENTS_RESERVE = 2              # clusters held for it while packing


def die(msg):
    print(f"os88allapps: {msg}", file=sys.stderr)
    sys.exit(1)


def cdiv(a, b):
    return (a + b - 1) // b


class Payload:
    """File sizes and package bodies, read once."""

    def __init__(self):
        self.size, self.body = {}, {}

    def get(self, path):
        if path not in self.size:
            try:
                with open(path, "rb") as f:
                    data = f.read()
            except OSError as e:
                die(f"cannot read {path}: {e}")
            self.size[path] = len(data)
            if path.lower().endswith(".o88"):
                self.body[path] = data
        return self.size[path]


def parents(key):
    out = []
    while key:
        out.append(key)
        key = key.rpartition("/")[0]
    return out


def price(entries, folders, slots, cb, pay):
    """Clusters os88disk.py spends on these (folder, path) entries plus
    the always-present `folders`: files, directories, ASSOC.DAT."""
    keys = set()
    for f in folders:
        keys.update(parents(f))
    for f, _ in entries:
        keys.update(parents(f))
    files = {k: [] for k in keys}
    for f, p in entries:
        files[f].append(p)
    kids = {k: sum(1 for c in keys if c.rpartition("/")[0] == k) for k in keys}
    need = sum(cdiv(pay.get(p), cb) or 1 for _, p in entries)
    for k in keys:
        need += max(1, cdiv(max(2 + kids[k] + len(files[k]), slots.get(k, 0)) * 32, cb))
    groups = {}
    for f, p in entries:
        if p.lower().endswith(".o88"):
            seen = set()
            groups.setdefault(f, []).append(
                (os88disk.name83(p, seen), pay.body[p], cdiv(pay.size[p], cb)))
    asc, _ = os88disk.build_assoc(groups) if groups else (b"", None)
    if asc:
        need += max(1, cdiv(len(asc), cb))
    return need


def units(args_):
    """[(name, [(folder, path)])], top-level folder order of first appearance."""
    order, by = [], {}
    for a in args_:
        folder, sep, path = a.partition(":")
        if not sep or not folder or not path:
            die(f"'{a}': every entry needs a FOLDER: prefix")
        key = os88disk.folder_key(folder)
        top = key.split("/")[0]
        if top not in by:
            by[top] = []
            order.append(top)
        by[top].append((key, path))
    return [(t, by[t]) for t in order]


def stem_groups(entries):
    """A collection's entries, grouped by 8.3 stem in first-appearance order."""
    order, by = [], {}
    for f, p in entries:
        stem = os.path.basename(p).upper().partition(".")[0]
        if stem not in by:
            by[stem] = []
            order.append(stem)
        by[stem].append((f, p))
    return [by[s] for s in order]


def runcpm_fill(rdir, geometry, others, reserve_clusters, slots):
    import getruncpm
    paths, used, budget = getruncpm.select(rdir, geometry, others, slots=slots,
                                           folders=0,
                                           reserve_clusters=reserve_clusters)
    return paths, used


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--size", type=int, required=True, choices=(1440, 1200))
    ap.add_argument("--prefix", required=True,
                    help="images are PREFIX-1.img, PREFIX-2.img, ...")
    ap.add_argument("--list", required=True, help="write the image list here")
    ap.add_argument("--folder", action="append", default=[],
                    help="a folder every disk carries (repeatable)")
    ap.add_argument("--dir-slots", action="append", default=[],
                    help="FOLDER=N, passed to the disk that has FOLDER")
    ap.add_argument("--collection", action="append", default=[],
                    help="a folder of independent programs that may be split "
                         "between disks when it outgrows one (repeatable)")
    ap.add_argument("--runcpm", metavar="DIR",
                    help="tools/getruncpm.py's cache: fill RUNCPM\\A\\0 from it")
    ap.add_argument("--runcpm-slots", type=int, default=0)
    ap.add_argument("--title", default="os8088 everything disks")
    ap.add_argument("entries", nargs="+", metavar="FOLDER:path")
    a = ap.parse_args()

    spt, heads, tot, spc, fatsz, root_ent, media = os88disk.GEOMETRY[a.size]
    lay = os88disk.Layout(spc, 1, 2, root_ent, tot, fatsz)
    cb, total = lay.cluster_bytes, lay.nclus
    cap = total - CONTENTS_RESERVE
    pay = Payload()
    every = [os88disk.folder_key(f) for f in a.folder]
    slots = {}
    for s in a.dir_slots:
        k, sep, n = s.rpartition("=")
        if not sep or not n.isdigit():
            die(f"--dir-slots wants FOLDER=N, not '{s}'")
        slots[os88disk.folder_key(k)] = int(n)
    collections = {c.upper() for c in a.collection}

    # RUNCPM\A\0 at its empty-disk size: what the fill is owed wherever it lands
    a0_full = a0_cost = 0
    if a.runcpm:
        full_paths, used = runcpm_fill(a.runcpm, a.size, [], 0, a.runcpm_slots)
        a0_full = len(full_paths)
        import getruncpm
        a0_cost = used + cdiv(getruncpm.SAVE_ROOM_KB[a.size] * 1024, cb)

    def disk_need(entries, has_cpm):
        folders = every + (["RUNCPM/A"] if has_cpm else [])
        return price(entries, folders, slots, cb, pay) + (a0_cost if has_cpm else 0)

    disks = []                                   # [{"entries": [...], "cpm": bool}]

    def place(entries, is_cpm):
        for d in disks:
            cpm = d["cpm"] or is_cpm
            if disk_need(d["entries"] + entries, cpm) <= cap:
                d["entries"] += entries
                d["cpm"] = cpm
                return True
        if disk_need(entries, is_cpm) <= cap:
            disks.append({"entries": list(entries), "cpm": is_cpm})
            return True
        return False

    for name, entries in units(a.entries):
        is_cpm = bool(a.runcpm) and name == "RUNCPM"
        if place(entries, is_cpm):
            continue
        alone = disk_need(entries, is_cpm)
        if name not in collections:
            die(f"folder {name}\\ alone needs {alone} clusters and a {a.size}KB "
                f"floppy holds {cap} after its CONTENTS.TXT: a folder that is ONE "
                f"program cannot be split across disks (SPEC.md 19.10) - it "
                f"needs a decision, not a bigger set")
        for group in stem_groups(entries):
            if not place(group, False):
                die(f"{group[0][1]} and its same-stem files need "
                    f"{disk_need(group, False)} clusters, more than a whole "
                    f"{a.size}KB floppy ({cap})")

    n = len(disks)
    images = [f"{a.prefix}-{i + 1}.img" for i in range(n)]

    def tops(d):
        seen = []
        for f, _ in d["entries"]:
            t = f.split("/")[0]
            if t not in seen:
                seen.append(t)
        return seen

    # CONTENTS.TXT: the same map on every disk, with "this one" marked
    label = {1440: "1.44MB", 1200: "1.2MB"}[a.size]
    lines = [f"{a.title}, {label}: {n} disk{'s' if n != 1 else ''}", ""]
    for i, d in enumerate(disks):
        lines.append(f"  {os.path.basename(images[i])}")
        lines.append("      " + ", ".join(t + "\\" for t in tops(d)))
    lines += ["", "A program and the files it opens are always on the same",
              "disk. Put the disk that holds a program in drive B: and",
              "open its folder.", ""]

    old = []
    if os.path.exists(a.list):
        with open(a.list) as f:
            old = [l.strip() for l in f if l.strip()]

    with tempfile.TemporaryDirectory() as td:
        for i, d in enumerate(disks):
            entries = list(d["entries"])
            extra = []
            if d["cpm"]:
                # select() prices the files it is handed and their ASSOC.DAT
                # itself, and A\0's own directory; what it is told on top is
                # every OTHER directory on this disk, and CONTENTS.TXT
                others = [p for _, p in entries]
                dirs = dirs_only(entries, every + ["RUNCPM/A"], slots, cb)
                paths, used = runcpm_fill(a.runcpm, a.size, others,
                                          dirs + CONTENTS_RESERVE,
                                          a.runcpm_slots)
                if len(paths) < a0_full:
                    die(f"RUNCPM\\A\\0 on {os.path.basename(images[i])} took "
                        f"{len(paths)} files where an empty {a.size}KB disk "
                        f"takes {a0_full}: the packing under-priced it")
                extra = [f"RUNCPM/A/0:{p}" for p in paths]
            text = "\r\n".join(lines).replace(
                "  " + os.path.basename(images[i]),
                "> " + os.path.basename(images[i]) + "   (this disk)")
            cpath = os.path.join(td, str(i + 1), CONTENTS)
            os.makedirs(os.path.dirname(cpath))
            with open(cpath, "wb") as f:
                f.write(text.encode("ascii"))
            if cdiv(len(text), cb) > CONTENTS_RESERVE:
                die("CONTENTS.TXT outgrew its reserve; raise CONTENTS_RESERVE")
            present = set()
            for f, _ in entries + [(e.split(":")[0], None) for e in extra]:
                present.update(parents(f))
            present.update(k for f in every for k in parents(f))
            cmd = [sys.executable, os.path.join(TOOLS, "os88disk.py"),
                   "-o", images[i], "--size", str(a.size), "--deep-folders"]
            for k, v in slots.items():
                if k in present:
                    cmd += ["--dir-slots", f"{k}={v}"]
            if d["cpm"] and a.runcpm_slots:
                cmd += ["--dir-slots", f"RUNCPM/A/0={a.runcpm_slots}"]
            for f in a.folder:
                cmd += ["--folder", f]
            cmd += [f"{f}:{p}" for f, p in entries] + extra + [cpath]
            r = subprocess.run(cmd)
            if r.returncode:
                die(f"os88disk.py refused {images[i]} (the packing is wrong "
                    f"if it says clusters: that is this tool's arithmetic)")
            r = subprocess.run([sys.executable, os.path.join(TOOLS, "os88disk.py"),
                                "--verify", images[i]], capture_output=True, text=True)
            if r.returncode:
                die(f"{images[i]} does not verify:\n{r.stdout}{r.stderr}")
            m = re.search(r"(\d+) cluster\(s\) in use", r.stdout)
            print(f"os88allapps: {os.path.basename(images[i])}: "
                  f"{', '.join(t + chr(92) for t in tops(d))}"
                  + (f" + {len(extra)} A\\0 files" if extra else "")
                  + f" | {m.group(1) if m else '?'} of {total} clusters")

    for stale in old:
        if stale not in images and os.path.exists(stale):
            os.remove(stale)
            print(f"os88allapps: removed {stale} - the set no longer needs it")
    with open(a.list, "w") as f:
        f.write("".join(p + "\n" for p in images))
    print(f"os88allapps: {n} disk{'s' if n != 1 else ''} at {a.size}KB -> {a.list}")


def dirs_only(entries, folders, slots, cb):
    """Directory clusters alone: price() minus files minus ASSOC.DAT."""
    keys = set()
    for f in folders:
        keys.update(parents(f))
    for f, _ in entries:
        keys.update(parents(f))
    nfiles = {k: 0 for k in keys}
    for f, _ in entries:
        nfiles[f] += 1
    kids = {k: sum(1 for c in keys if c.rpartition("/")[0] == k) for k in keys}
    return sum(max(1, cdiv(max(2 + kids[k] + nfiles[k], slots.get(k, 0)) * 32, cb))
               for k in keys)


if __name__ == "__main__":
    main()
