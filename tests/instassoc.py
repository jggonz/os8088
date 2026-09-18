#!/usr/bin/env python3
"""The installed volume's ASSOC.DAT describes THAT volume (SPEC.md 52.10.14).

`ASSOC.DAT` (SPEC.md 54.7) is a per-VOLUME cache, and since SPEC.md 54.7.1 its
app rows carry the CLUSTER of the folder each program lives in.  An install
copied the source floppy's copy onto the hard disk, so every row on C: named a
folder on A:.

**The rows survive the copy and the locations do not, which is what makes this
worth a row of its own.**  The association still works - the mount reads the
file, `asc_merge_ext` takes its `HTM` row, and the machine knows the document
opens with BROWSER and can name it in the error.  What it cannot do is find the
program, and it cannot because of exactly one rung: SPEC.md 54.4.2's rung 4
tries the folder `assoc_dfold` names, that byte is a build-time default carried
by the kernel's own four stems alone, and a slot created by `asc_merge_ext` has
0 - which `assoc_tryfold` reads as *nothing to try*.  So `C:\\APPS`, the one
folder on the volume holding the program, is the one place the sweep is
structurally unable to look.

Reported from the field as *"install, take the system disk out, restart, go to
C:, open MEDIA, double-click BROWSER.HTM - `BROWSER.O88 - not on this disk`"*.

**A screendump cannot answer this** and neither can the installer: it says
`Done` in both the broken and the fixed case, and the difference is 664 bytes
of a hidden + system file.  So this drives a real install on MartyPC's XT-IDE
machine and then reads the partition back ON THE HOST, with `tests/instdeep.py`'s
FAT reader - which is not the one that wrote it - and asks three questions:

  1. every app row's cluster names a folder that is really on this volume
     (the broken build fails here on all eight rows at once);
  2. every package on the volume has a row, and every row a package - so the
     apps disk's seventeen are in it and the file is not the system disk's;
  3. every association in it resolves: the row it names carries a stem, and a
     package of that stem is really in the folder that row's cluster names.
     That is the user's double-click, asked as arithmetic.

    python3 tests/instassoc.py

REQUIRES A DISK: os8088_xt_hdd, and it ERASES it - `os88marty.launch` clones a
per-instance copy, so what is erased is this run's own.
"""
import os
import struct
import sys

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty as M                                      # noqa: E402
import instdeep as ID                                      # noqa: E402

# The format, a fourth reader of it (SPEC.md 54.7). kernel/assoc.inc parses it,
# drivers/hdd/iassoc.inc writes it, tools/os88disk.py writes it, and this reads
# it - deliberately spelled out rather than imported, because a test that
# borrows the writer's own constants cannot catch the writer using the wrong
# ones. tests/unit/t_mirror.py is what keeps the three PRODUCERS agreeing.
ASC_MAGIC = b"OS88AC"
ASC_HDR, ASC_ROW, ASC_ROWCLUS, ASC_ROWICO = 16, 88, 10, 16   # a VERSION 2 row
ASC_NAPP, ASC_NEXT = 32, 24

# The extensions the shipped disks' packages declare (SPEC.md 54.6), and the
# program each must reach. Named rather than counted: "5 associations" passes
# on five wrong ones, and HTM is the one the field reported.
WANT_EXT = {"HTM": "BROWSER", "TEX": "TEXPAD", "MOD": "TRACKER"}


def say(*a):
    print(*a, flush=True)


def decode(raw):
    """(apps, exts) out of an ASSOC.DAT body, or a sentence saying why not."""
    if raw is None:
        return None, "ASSOC.DAT is not on the installed volume at all"
    if len(raw) < ASC_HDR or raw[0:6] != ASC_MAGIC:
        return None, ("ASSOC.DAT is %d bytes and does not start %r (it starts "
                      "%r)" % (len(raw), ASC_MAGIC, raw[0:6]))
    if raw[6] != 2:
        return None, ("ASSOC.DAT says version %d, not 2 - the installer writes "
                      "the row with the glyph column (SPEC.md 54.3.2)" % raw[6])
    napp, next_ = raw[7], raw[8]
    if napp > ASC_NAPP or next_ > ASC_NEXT:
        return None, ("ASSOC.DAT declares %d app rows and %d ext rows, against "
                      "caps of %d and %d - the kernel refuses the whole file "
                      "for either" % (napp, next_, ASC_NAPP, ASC_NEXT))
    want = ASC_HDR + napp * ASC_ROW + next_ * 4
    if len(raw) != want:
        return None, ("ASSOC.DAT is %d bytes and its own header says %d "
                      "(%d app rows, %d ext rows)"
                      % (len(raw), want, napp, next_))
    apps = []
    for i in range(napp):
        o = ASC_HDR + i * ASC_ROW
        r = raw[o:o + ASC_ROW]
        apps.append((r[0:8].decode("latin1").rstrip(),
                     struct.unpack_from("<H", r, 8)[0],
                     struct.unpack_from("<H", r, ASC_ROWCLUS)[0],
                     any(r[ASC_ROWICO:ASC_ROWICO + 64])))
    base = ASC_HDR + napp * ASC_ROW
    exts = [(raw[base + j * 4:base + j * 4 + 3].decode("latin1"),
             raw[base + j * 4 + 3]) for j in range(next_)]
    return (apps, exts), None


def volume_map(v):
    """{folder cluster: {STEM: on-disk size}} for the root and one level down.

    The root is cluster 0, which is what a row carries for a package in it -
    OSAPI_FILE_GOTO's own spelling (SPEC.md 19.2.2).
    """
    out, names = {}, {}
    def pkgs(first):
        return {n[:-4].upper(): sz for n, attr, cl, sz in v.entries(first)
                if not attr & 0x10 and n.upper().endswith(".O88")}
    out[0] = pkgs(0)
    names[0] = "the root"
    for n, attr, cl, sz in v.entries(0):
        if attr & 0x10 and n not in (".", ".."):
            out[cl] = pkgs(cl)
            names[cl] = n
    return out, names


def main():
    if not os.path.exists("build/martypc/run/martypc_headless"):
        sys.exit("no MartyPC - `make marty` first")
    for img in ("build/os8088-360.img", "build/apps360.img"):
        if not os.path.exists(img):
            sys.exit("no %s - `make` first" % img)

    with M.launch("build/os8088-360.img", apps="build/apps360.img",
                  machine=ID.MACHINE) as m:
        M.settle(m)
        ID.install(m)
        v = ID.partition(ID.vhd(m))     # inside the `with`: the media go with it

    fails = []
    byclus, names = volume_map(v)
    onvol = {stem: cl for cl, d in byclus.items() for stem in d}
    say("  the volume carries %d packages in %d folders: %s"
        % (len(onvol), len([c for c in byclus if byclus[c]]),
           ", ".join("%s=%d" % (names[c], len(byclus[c]))
                     for c in byclus if byclus[c])))

    got, why = decode(v.read("ASSOC.DAT"))
    if got is None:
        say("  FAIL: " + why)
        say("instassoc: FAILED")
        return 1
    apps, exts = got
    say("  ASSOC.DAT    %d app rows, %d ext rows" % (len(apps), len(exts)))

    # --- 1. every row's cluster is a folder THIS volume has ------------------
    # The broken build fails here and only here on eight rows at once, which is
    # the signature worth recognising: the file is the SOURCE's, verbatim.
    stale = [(s, c) for s, _, c, _ in apps if c not in byclus]
    if stale:
        fails.append(
            "%d of %d app rows name a cluster that is no folder on this "
            "volume: %s. This volume's folders are %s. A row's cluster is the "
            "DIRECTORY the program lives in (SPEC.md 54.7.1), so these rows "
            "were written for a different disk - which is what an install "
            "copying the source's ASSOC.DAT produces (SPEC.md 52.10.14)"
            % (len(stale), len(apps),
               ", ".join("%s->%d" % (s, c) for s, c in stale[:6]),
               ", ".join("%s=%d" % (names[c], c) for c in sorted(byclus))))

    # --- 2. ...and it is really where the row says ---------------------------
    wrong = [(s, c) for s, _, c, _ in apps
             if c in byclus and s not in byclus[c]]
    if wrong:
        fails.append(
            "%d app row(s) name a folder that does not hold that program: %s"
            % (len(wrong), ", ".join("%s is not in %s" % (s, names[c])
                                     for s, c in wrong[:6])))

    badsz = [(s, sz, byclus[c][s]) for s, sz, c, _ in apps
             if c in byclus and s in byclus[c] and sz != byclus[c][s] & 0xFFFF]
    if badsz:
        fails.append(
            "%d row(s) carry the wrong SIZE: %s. asc_lookup keys on (stem, "
            "size) against the raw directory entry (SPEC.md 19.1's +20), so a "
            "row with the wrong number misses on every mount for ever - which "
            "is what OSAPI_FILE_FIND rather than OSAPI_FILE_FIND_RAW gives a "
            "compressed file (SPEC.md 20.14.3)"
            % (len(badsz), ", ".join("%s says %d, the directory says %d"
                                     % t for t in badsz[:6])))

    # --- 3. every package on the volume is covered ---------------------------
    # It used to be the SYSTEM disk's cache, so the apps disk's packages were
    # simply absent. Bounded by the cap, which a big install really does reach.
    rows = {s for s, _, _, _ in apps}
    missing = sorted(set(onvol) - rows)
    if missing and len(apps) < ASC_NAPP:
        fails.append(
            "%d package(s) on the volume have no row while the file holds only "
            "%d of its %d: %s. The cache is built from the volume's own "
            "packages (SPEC.md 52.10.14), so anything short of the cap is a "
            "walk that missed a folder"
            % (len(missing), len(apps), ASC_NAPP, ", ".join(missing[:8])))
    elif missing:
        say("  %d package(s) past the %d-row cap: %s"
            % (len(missing), ASC_NAPP, ", ".join(missing[:8])))
    ghosts = sorted(rows - set(onvol))
    if ghosts:
        fails.append(
            "%d row(s) name a program that is not on this volume at all: %s"
            % (len(ghosts), ", ".join(ghosts)))

    # --- 4. and every association RESOLVES, which is the user's double-click --
    seen = {}
    for e, ix in exts:
        if ix >= len(apps):
            fails.append("the %r row points at app row %d and the file has %d"
                         % (e, ix, len(apps)))
            continue
        seen[e.strip()] = apps[ix][0]
    for e, want in WANT_EXT.items():
        if e not in seen:
            fails.append(
                "%r has no association in ASSOC.DAT. %s.O88 declares it "
                "(SPEC.md 54.6) and it is on this volume, so a mount of C: "
                "leaves the machine unable to open a .%s at all" % (e, want, e))
        elif seen[e] != want:
            fails.append("%r resolves to %s and should be %s"
                         % (e, seen[e], want))
        else:
            cl = next(c for s, _, c, _ in apps if s == want)
            if want in byclus.get(cl, {}):
                say("    .%-4s -> %-8s in %-8s (cluster %d)  reachable"
                    % (e, want, names[cl], cl))
            else:
                # NOT folded into check 1, which would report this as one more
                # stale row among eight. This is the field's own sentence:
                # the association resolves, the program is on the volume, and
                # the machine still cannot open the document.
                fails.append(
                    ".%s resolves to %s, %s.O88 IS on this volume in %s, and "
                    "the row sends the open to cluster %d which is %s - so "
                    "SPEC.md 54.4.2's rungs answer `%s.O88 - not on this "
                    "disk` for a program that is right there"
                    % (e, want, want, names[onvol[want]], cl,
                       names.get(cl, "no folder on this volume"), want))

    for f in fails:
        say("  FAIL: " + f)
    say("instassoc: %s" % ("FAILED" if fails else
                           "the installed volume's ASSOC.DAT describes that "
                           "volume - %d rows, %d associations, every cluster "
                           "its own" % (len(apps), len(exts))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
