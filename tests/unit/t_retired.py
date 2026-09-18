#!/usr/bin/env python3
"""Every package under apps/ ships, or apps/RETIRED.txt says why it does not.

    python3 tests/unit/t_retired.py

CLAUDE.md's Layout section says "apps/ - loadable packages; everything here
ships". That is an invariant nothing enforced, and the shape it misses is not
a package somebody decided to withhold - it is a package that ships nowhere
because a list was edited and nobody noticed. Those two look identical from
every angle except intent, which is exactly the argument t_movable.py makes
about an undeclared region: the interesting case is the package in NEITHER
the disks nor the registry, because that is the one no diff shows.

It has happened twice in this tree already, in both directions. PACMAN.O88
came off the floppies under a Makefile comment saying "while Dot Delirium is
developed", which is a sentence with no expiry and nothing watching it; and
the same edit went wrong once before - the comment records that taking the
package off the disk lists took it out of every BUILD too, because APPS_GAMES
was the only thing that had ever named it.

TWO KINDS, AND `retired` IS CHECKED HARDER THAN `instrument` (SPEC.md 20.16):

  * `retired`    a failure. It may not be on any shipped image, may not be in
                 the live payload, and `all` may not even build it. Its source
                 and its SPEC.md section stay - that is the record - so this
                 also checks the directory is still there.
  * `instrument` a bench or a gate that happens to be a package. `all` MAY
                 build it, because keeping it assembling is usually the point,
                 but no shipped image may carry it.

THE LIVE PAYLOAD IS READ FROM build/livepayload.txt, which `all` emits from
$(LIVEARGS) itself - the same file tests/unit/t_livefull.py reads, and for the
same reason: a payload list that is derived cannot disagree with the Makefile,
where a list retyped here would.

THE IMAGES ARE WALKED RECURSIVELY, with t_image.py's own Vol, and the name is
matched WHOLE. Both halves of that were wrong first and each gave a wrong
answer in a different direction. `tools/os88fat.py ls` lists the ROOT only,
and every shipped package lives in APPS/ or GAMES/, so every one of them read
as not shipping. And a substring test for "WIRE.O88" matches "THEWIRE.O88",
which reported WIREFRAME - an instrument that correctly ships nowhere - as
being on the network disk. A package's 8.3 name is not its directory name
either (solitaire ships as SOLITAIR.O88), so the comparison is against the
truncation the packer actually writes.

IT TURNS ONE WAY. A `retired` package that reappears on a disk FAILS here
rather than being quietly accepted, so the registry cannot become a list of
things that used to be true.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                           # noqa: E402
from t_image import Vol, read                             # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    "..", ".."))
REGISTRY = os.path.join(ROOT, "apps", "RETIRED.txt")
BUILD = os.path.join(ROOT, "build")
MAKEFILE = os.path.join(ROOT, "Makefile")
LIVEPAYLOAD = os.path.join(BUILD, "livepayload.txt")

KINDS = ("retired", "instrument")

# The SHIPPED images, and nothing else: an on-demand disk (zdisk, worddisk,
# wiredisk, regapp360) is not a product, so an instrument riding one is
# correct rather than a finding. This is t_image.py's list plus the four
# 360KB-only ones, held here for the reason that file's own comment gives -
# each walker keeps its own list, so a new image is not covered until it is
# added to all of them.
SHIPPED = ("os8088.img", "os8088-720.img", "os8088-360.img", "os8088-120.img",
           "apps.img", "apps720.img", "apps360.img", "apps120.img",
           "media360.img", "office360.img", "network360.img", "games360.img")

# A package is a directory under apps/ that emits a package header. apps/cc is
# the C SDK and emits none of its own.
HEADER = re.compile(r"^\s*OS88_HEADER\b", re.M)
CRT0INC = re.compile(r'^\s*%include\s+"cc/crt0\.asm"', re.M)


def registry():
    """[(kind, package, reason)] from apps/RETIRED.txt."""
    rows, txt = [], ""
    if os.path.exists(REGISTRY):
        txt = open(REGISTRY, encoding="utf-8").read()
    # A reason may continue onto following comment-only lines, which is how the
    # file is actually written, so the entry is the KIND line and the reason is
    # whatever trails it.
    for line in txt.splitlines():
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        parts = s.split("#", 1)
        f = parts[0].split()
        if len(f) != 2:
            check(False, "apps/RETIRED.txt line is not `<kind> <package>  # reason`",
                  got=s)
            continue
        rows.append((f[0], f[1], parts[1].strip() if len(parts) > 1 else ""))
    return rows


def packages():
    """Every apps/<name> that emits a package header."""
    out = []
    apps = os.path.join(ROOT, "apps")
    for d in sorted(os.listdir(apps)):
        p = os.path.join(apps, d)
        if not os.path.isdir(p) or d == "cc":
            continue
        hit = False
        for root, _, files in os.walk(p):
            for fn in files:
                if not fn.endswith((".asm", ".c")):
                    continue
                if "hosttest" in root:
                    continue
                try:
                    t = open(os.path.join(root, fn), encoding="utf-8",
                             errors="replace").read()
                except OSError:
                    continue
                if HEADER.search(t) or CRT0INC.search(t):
                    hit = True
                    break
            if hit:
                break
        if hit:
            out.append(d)
    return out


def image_names(img):
    """The set of 8.3 file names on one image, whole, or None if unreadable."""
    try:
        v = Vol(read(img), os.path.basename(img))
        out = set()
        for _path, name11, attr, _clus, _size in v.walk():
            if attr & 0x10:                    # A_DIR
                continue
            nm = name11.decode("ascii", "replace")
            out.add((nm[:8].strip() + "." + nm[8:].strip()).upper())
        return out
    except Exception:                          # noqa: BLE001 - unreadable is
        return None                            # a SKIP, not a finding


def main():
    rows = registry()
    known = {p: k for k, p, _ in rows}

    for kind, pkg, reason in rows:
        check(kind in KINDS, "apps/RETIRED.txt names an unknown kind %r" % kind,
              why="the kinds are %s" % ", ".join(KINDS))
        check(bool(reason), "%s %s has no reason" % (kind, pkg),
              why="a line without a reason is a list of names, which is what "
                  "the Makefile comment already was")
        check(os.path.isdir(os.path.join(ROOT, "apps", pkg)),
              "apps/RETIRED.txt names %s and apps/%s does not exist" % (pkg, pkg),
              why="a retired package keeps its source as the record; a line "
                  "naming nothing is rot")

    # --- THE CASE THAT MATTERS: in neither the disks nor the list ------------
    live = ""
    if os.path.exists(LIVEPAYLOAD):
        live = open(LIVEPAYLOAD, encoding="utf-8").read()
    else:
        check(False, "build/livepayload.txt is missing",
              why="`all` emits it; run make before this row (t_livefull.py "
                  "reads the same file)")

    imgs = {}
    for name in SHIPPED:
        p = os.path.join(BUILD, name)
        if os.path.exists(p):
            imgs[name] = image_names(p)

    for pkg in packages():
        o88 = "/%s.o88" % pkg
        in_live = o88 in live
        # The WHOLE name, and the 8.3 truncation the packer writes: `wire`
        # is WIRE.O88 and must not match THEWIRE.O88, `solitaire` is
        # SOLITAIR.O88 and would match nothing if it were not truncated here.
        want = pkg.upper()[:8] + ".O88"
        on_img = [n for n, names in imgs.items() if names and want in names]
        ships = in_live or bool(on_img)
        kind = known.get(pkg)

        if kind is None:
            check(ships,
                  "apps/%s ships on no image and has no apps/RETIRED.txt line"
                  % pkg,
                  why="CLAUDE.md says everything in apps/ ships, so a package "
                      "that does not needs a `retired` or `instrument` line "
                      "saying so (SPEC.md 20.16). If it is meant to ship, a "
                      "disk list has lost it")
        else:
            check(not in_live,
                  "%s is %s and is in the live payload" % (pkg, kind),
                  why="build/livepayload.txt names it; take it out of "
                      "LIVEPKGARGS/LIVEARGS in the Makefile")
            check(not on_img,
                  "%s is %s and is on a shipped image" % (pkg, kind),
                  got=", ".join(on_img),
                  why="the registry turns one way: putting it back on a disk "
                      "means deleting its line, not adding a disk")

    # --- `all` MAY NOT BUILD A RETIRED PACKAGE ------------------------------
    mk = open(MAKEFILE, encoding="utf-8").read()
    m = re.search(r"^all:((?:[^\n]*\\\n)*[^\n]*)", mk, re.M)
    check(m is not None, "cannot find the `all:` rule in the Makefile")
    allrule = m.group(1) if m else ""
    for kind, pkg, _ in rows:
        if kind != "retired":
            continue
        check("/%s.o88" % pkg not in allrule,
              "`all` builds %s, which is retired" % pkg,
              why="a retired package is built by `make %s` alone - keeping it "
                  "in `all` charges every build for a program that ships "
                  "nowhere" % pkg)

    done("t_retired")


if __name__ == "__main__":
    main()
