#!/usr/bin/env python3
"""Is `.boot2` still only what it is FOR? (SPEC.md 2.9.14, BOOT3-PLAN wave 4)

    python3 tests/unit/t_blobbodies.py

`.boot2` is the blob stage 1 reads BEFORE THE BAR SHOWS, so every byte in it
is paid for in the one stretch of the boot where the screen says nothing
(PERFORMANCE.md prices disk work in int 13h calls, and t_blobruns counts
them). BOOT3-PLAN wave 4 took it back to its original purpose on every
kernel - the LOADER (boot/boot2.asm), the LOADING SCREEN (kernel/splash.inc),
the first tick's ADAPTER PROBE (vid_detect, bvs_apply: the splash runs them
before anything else of the kernel is expanded) and the STATUS LINE (the
captions and the driver titles, which spl_mcs reads through the blob's own
CS) - and moved every other body to `.boot3`, KERNEL.SYS's last section,
which rides the packed read after the bar is up (SPEC.md 2.9.15). Seven
sectors on all four kernels, from thirteen, fourteen and eleven.

THIS IS THE RATCHET THAT KEEPS IT THERE. Every label each built kernel puts
in `.boot2` (its kernel.map, macro locals and `.local` labels aside) must be
named in tests/blobbodies.txt, either as one of those four purposes or as a
RESIDUE row - a body that is in the blob for a reason of its own (a far door
onto the probe or the loader for a `.boot3` caller), with that reason beside
it. A new label fails `make` until it is filed; a residue row whose label a
kernel no longer has fails too, so the list cannot rot; and the number of
residue rows per kernel may not rise above RESIDUE_MAX below, which only
ever goes DOWN - so "it was quicker to leave it in the blob" is a diff to
this file somebody has to write and defend, not a drift nobody saw.

kern_big's and kern_small's trees are always there after `make`; kern_mega's
(build/megak, `make mega`) and kern_emu's (build/emuk, `make emu`) are read
when present and CURRENT - a map older than the kernel's sources is named as
stale and not read, so a tree somebody built last week cannot fail today's
`make`. Host-only, a map read per kernel: ~0.02 s.
"""
import fnmatch
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
REG = os.path.join(ROOT, "tests", "blobbodies.txt")

TREES = (("big", ""), ("small", "smallk"), ("mega", "megak"), ("emu", "emuk"))
ALWAYS = ("big", "small")             # the two `make` builds every time
KINDS = ("loader", "splash", "probe", "status", "residue")

# THE RATCHET. How many residue rows each kernel may have. IT ONLY GOES DOWN:
# lowering it is the point of a change that takes a body out of the blob, and
# raising it is a decision with a reason in the commit that does it - never a
# way to make a red row green. At BOOT3-PLAN wave 4 (2026-11-08) kern_big,
# kern_emu and kern_mega kept two doors (bvs_apply_f, the probe's VID_APPLY
# for `.boot3`'s adapter switch; ovl_fdd_cylok, the loader's b2_cylok for
# `.boot3`'s ovl_fdd_apply) and kern_small none.
# BOOT3-PLAN 16.16/16.17 (2026-11-09) changed WHICH two, not how many:
# font_init went back into the blob on kern_big, kern_emu and kern_mega
# (`.boot3` rides the packed KERNEL.SYS, which the elendilon merge had put 80
# bytes past 166 sectors), and bvs_apply_f went - bvs_apply is FAR itself now,
# so `.boot3` B2CALLs the probe directly. kern_small keeps font_init in
# `.boot3` and the blob there still holds none.
# 2026-11-10: font_init left the blob again on the three big kernels (SPEC.md
# 2.9.14.2) - the plain-MDA probe (SPEC.md 39.1) needed the room, and kernel
# size pass 19 had given the packed file back more than its 236 bytes - so
# ovl_fdd_cylok is the one residue row there.
RESIDUE_MAX = {"big": 1, "emu": 1, "mega": 1, "small": 0}


def boot2_labels(path):
    """Every non-local label the map files under `.boot2`."""
    lines = open(path, errors="replace").read().split("\n")
    try:
        at = next(i for i, l in enumerate(lines) if l.startswith("-- Symbols"))
        at = next(i for i in range(at, len(lines))
                  if lines[i].startswith("---- Section .boot2 "))
    except StopIteration:
        raise SystemExit("t_blobbodies: %s has no `.boot2` symbols" % path)
    out = []
    for l in lines[at + 1:]:
        if l.startswith("---- Section"):
            break
        p = l.split()
        if len(p) == 3 and re.match(r"^[0-9A-F]+$", p[0]):
            name = p[2]
            if name.startswith("..@") or "." in name:
                continue                  # a macro local, or a proc's own
            out.append(name)
    return out


def registry():
    rows = []
    for n, line in enumerate(open(REG), 1):
        line = line.split("#", 1)[0].rstrip()
        if not line.strip():
            continue
        p = line.split(None, 3)
        if len(p) < 4:
            raise SystemExit("t_blobbodies: %s:%d wants <kernels> <label> "
                             "<kind> <why>" % (REG, n))
        kern, label, kind, why = p
        kerns = set(TREES_ALL) if kern == "*" else set(kern.split(","))
        if not kerns <= set(TREES_ALL) or kind not in KINDS:
            raise SystemExit("t_blobbodies: %s:%d: kernels are %s or *, kinds "
                             "%s" % (REG, n, ",".join(TREES_ALL),
                                     "/".join(KINDS)))
        if kind == "residue" and any(c in label for c in "*?["):
            raise SystemExit("t_blobbodies: %s:%d: a residue row names ONE "
                             "label - a glob would let the list grow without "
                             "a row" % (REG, n))
        rows.append((n, kerns, label, kind, why.strip()))
    return rows


TREES_ALL = tuple(k for k, _ in TREES)


def newest_source():
    files = glob.glob(os.path.join(ROOT, "kernel", "*.inc"))
    files += [os.path.join(ROOT, "kernel", "kernel.asm"),
              os.path.join(ROOT, "boot", "boot2.asm")]
    return max(os.path.getmtime(f) for f in files)


def main():
    rows = registry()
    src = newest_source()
    fail, seen, stale = [], [], []
    for kern, sub in TREES:
        path = os.path.join(ROOT, "build", sub, "kernel.map")
        if not os.path.exists(path):
            if kern in ALWAYS:
                fail.append("%s: no %s - run make first" % (kern, path))
            continue
        if kern not in ALWAYS and os.path.getmtime(path) < src:
            stale.append(kern)
            continue
        labels = boot2_labels(path)
        mine = [r for r in rows if kern in r[1]]
        res = [r for r in mine if r[3] == "residue"]
        for lab in labels:
            if not any(fnmatch.fnmatchcase(lab, r[2]) for r in mine):
                fail.append(
                    "%s: `%s` is in `.boot2` and tests/blobbodies.txt does not "
                    "say why. The blob is what stage 1 reads before the bar "
                    "shows, and since BOOT3-PLAN wave 4 it holds the loader, "
                    "the splash, the first tick's probe and the status line "
                    "only: put the body in `.boot3` (B3SECT, SPEC.md 2.9.15), "
                    "or file it there - one of those four kinds, or a residue "
                    "row with its reason, within RESIDUE_MAX" % (kern, lab))
        for n, _k, lab, _kind, _why in res:
            if lab not in labels:
                fail.append("%s: tests/blobbodies.txt:%d names residue `%s`, "
                            "which this kernel's `.boot2` no longer has - "
                            "delete the row (and lower RESIDUE_MAX: that is "
                            "the ratchet turning)" % (kern, n, lab))
        if len(res) > RESIDUE_MAX[kern]:
            fail.append("%s: %d residue rows against RESIDUE_MAX %d - the "
                        "list only shrinks (this file's docstring)"
                        % (kern, len(res), RESIDUE_MAX[kern]))
        seen.append("%s %d labels, %d residue" % (kern, len(labels), len(res)))
    for f in fail:
        print("FAIL: %s" % f)
    print("blobbodies: %s%s" % (
        "FAILED" if fail else "; ".join(seen),
        "" if not stale else "; not read, older than the kernel's sources: "
        + ", ".join(stale)))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
