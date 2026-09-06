#!/usr/bin/env python3
"""Where the PARTS standard's own words live, read out of the standard.

    python3 tools/os88parts.py          # print the chain, the copies, the total

`apps/os88parts.inc` publishes its bss as an equ chain off `os88_image_end`
(SPEC.md 20.12), and a package's own words start `OP_BSS` bytes after that.
Every host-side reader of a MSEG-shaped package therefore needs two things -
one field's offset, and where the package's own bss begins - and both move
whenever the standard gains a word.

**A WRITTEN-DOWN COPY IS A STALE COORDINATE WAITING**, which is exactly the
argument tools/os88geom.py makes about the kernel's geometry, and it has now
happened here too: wave 4 gave `op_xload` a separate linear cursor, `OP_BSS`
went 65 -> 69, and tests/multiseg.py's own `OP_BSS = 65` went on returning
NUMBERS - it read `ms_seg[]` two entries early, reported parts 0 and 1 as
having no segment and part 4 as GRANTED, and the verdict in the same window
said `MSEG 6/6 OK`. A gate disagreeing with the package it is testing, about
the package's own memory.

So this is the one copy. It parses the include rather than mirroring it, so
there is nothing to keep in step: a field that moves moves here on the next
run, and a field that is DELETED raises rather than answering an offset that
now means something else.

It also DECODES A PART TABLE out of a package image, for the same reason: a
gate that wants to know what the parts are should read the package's own rows
rather than write the answer down beside them. tests/mseglazy.py's central
assertion is arithmetic over those rows.
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INC = os.path.join(ROOT, "apps", "os88parts.inc")

# The chain's BASE is a %define now (OP_BSS_AT, so a C package can move it),
# so this matches "op_x equ <whatever> + N" rather than one spelling of the
# base. What is being read is the OFFSET; where the chain starts is the
# package's business and not this reader's.
_CHAIN = re.compile(r"^(op_[a-z0-9_]+)\s+equ\s+\S+\s*\+\s*(\d+)", re.M)
_PLAIN = re.compile(r"^(OP_[A-Z0-9_]+)\s+equ\s+(\d+)", re.M)
_BSS = re.compile(r"^OP_BSS\s+equ\s+(\d+)", re.M)


def _read():
    try:
        src = open(INC).read()
    except OSError as e:
        raise RuntimeError("os88parts: cannot read %s: %s" % (INC, e))
    chain = {n: int(v) for n, v in _CHAIN.findall(src)}
    plain = {n: int(v) for n, v in _PLAIN.findall(src)}
    m = _BSS.search(src)
    if not chain or not plain or not m:
        raise RuntimeError(
            "os88parts: %s has no `op_* equ os88_image_end + N` chain, no "
            "plain `OP_* equ N`, or no `OP_BSS equ N`. The standard's bss is "
            "what every host-side reader of a parts package addresses through "
            "(SPEC.md 20.12)" % INC)
    return chain, plain, int(m.group(1))


OFF, EQU, OP_BSS = _read()

# The packer's own copies, which cannot import an .inc any more than a gate
# can. tests/unit/t_mirror.py's PY_MIRROR shape: a HAND-WRITTEN mapping,
# because tools/os88pkg.py spells these OPF_*/PART_*/PK_* rather than by the
# include's names, so `scan` below cannot find them by name. A packer writing
# rows at the wrong stride produces a table the standard reads as garbage.
PKG_MIRROR = {
    "PARTS_HDR": "OP_T_ROWS",
    "PART_ROW": "OP_ROW",
    "OPF_XMS": "OP_XMS",
    "OPF_ZERO": "OP_ZERO",
    "OPF_OPT": "OP_OPT",
    "OPF_LAZY": "OP_LAZY",
    "PK_SEG": "OP_SEG",
    "PK_ASSET": "OP_ASSET",
}


def off(name):
    """A standard bss field's offset past `os88_image_end`, or a named error.

    Raising is the point: an offset for a field that no longer exists would be
    a plausible number pointing at its neighbour.
    """
    try:
        return OFF[name]
    except KeyError:
        raise KeyError(
            "os88parts: apps/os88parts.inc has no %r. It publishes: %s"
            % (name, ", ".join(sorted(OFF))))


# --- the part TABLE, decoded out of a package image --------------------------

MAGIC = b"O88PARTS"


def table_at(image):
    """Where the part table starts in `image`, or None.

    By the MAGIC and not at a derived offset, which is the format's own rule:
    the table is the package's data at a label the package placed, so there is
    nothing to compute (SPEC.md 20.12.3). Two tables is as bad as none, and
    os88pkg.py refuses that at pack time.
    """
    at = image.find(MAGIC)
    if at < 0 or image.find(MAGIC, at + 1) >= 0:
        return None
    return at


def _u16(b, i):
    return b[i] | (b[i + 1] << 8)


def rows(image):
    """[{kind, flags, off, len, zkb}] for a package image.

    `off` is in 512-byte units and 0 means "no file bytes" - a scratch part.
    `zkb` is the declared scratch KB, and on a LAZY row it is instead where a
    fetched part banks its SEGMENT, so it can be non-zero on a row that
    declared no scratch at all (SPEC.md 20.12.4).
    """
    at = table_at(image)
    if at is None:
        raise ValueError(
            "no %r in this image, or two of them - it is not a parts package"
            % MAGIC.decode())
    hdr, row = EQU["OP_T_ROWS"], EQU["OP_ROW"]
    out = []
    for i in range(image[at + hdr - 2]):
        b = at + hdr + row * i
        out.append({"kind": image[b + EQU["OP_R_KIND"]],
                    "flags": image[b + EQU["OP_R_FLAG"]],
                    "off": _u16(image, b + EQU["OP_R_OFF"]),
                    "len": _u16(image, b + EQU["OP_R_LEN"]),
                    "zkb": _u16(image, b + EQU["OP_R_ZKB"])})
    return out


def sectors(row):
    """A filed row's length in 512-byte sectors; 0 for a scratch one."""
    return 0 if not row["off"] else (row["len"] + 511) // 512


# --- and the same question asked of the harness ------------------------------

_PYCONST = re.compile(r"^([A-Z][A-Z0-9_]*)\s*=\s*(\d+)\s*(?:#.*)?$", re.M)


def known():
    """Every constant a host script might copy: the plain `OP_*` equs, the bss
    chain under its uppercase spelling, and `OP_BSS`."""
    out = dict(EQU)
    out["OP_BSS"] = OP_BSS
    for n, v in OFF.items():
        out[n.upper()] = v
    return out


def scan(root=None):
    """Every LOCAL copy of one of those, as (path, line, name, mine, ours).

    tools/os88geom.py's `scan`, one namespace along, and here for the reason
    its docstring gives: a correct copy is the seed of the next stale one.
    tests/unit/t_mirror.py is the caller; without one this file would be a
    thing you can run rather than a thing that runs.
    """
    root = root or ROOT
    want = known()
    out = []
    for d in ("tools", "tests"):
        for dirpath, _, names in os.walk(os.path.join(root, d)):
            for fn in sorted(names):
                if not fn.endswith(".py") or fn == "os88parts.py":
                    continue
                path = os.path.join(dirpath, fn)
                try:
                    src = open(path, errors="replace").read()
                except OSError:
                    continue
                for i, line in enumerate(src.splitlines(), 1):
                    m = _PYCONST.match(line)
                    if m and m.group(1) in want:
                        out.append((os.path.relpath(path, root), i, m.group(1),
                                    int(m.group(2)), want[m.group(1)]))
    out.sort(key=lambda c: (c[3] == c[4], c[0], c[1]))
    return out


def pkg_copies(root=None):
    """The packer's hand-mapped copies, as (path, line, name, mine, ours)."""
    root = root or ROOT
    path = os.path.join(root, "tools", "os88pkg.py")
    out, seen = [], set()
    for i, line in enumerate(open(path, errors="replace").read().splitlines(), 1):
        line = line.split("#", 1)[0].rstrip()   # ...comments and all
        # `A, B = 1, 2` as well as `A = 1`, because os88pkg.py writes both.
        m = re.match(r"^([A-Z][A-Za-z0-9_, ]*?)\s*=\s*([0-9, ]+)$", line)
        if not m:
            continue
        names = [n.strip() for n in m.group(1).split(",")]
        vals = [v.strip() for v in m.group(2).split(",")]
        if len(names) != len(vals):
            continue
        for n, v in zip(names, vals):
            if n in PKG_MIRROR:
                seen.add(n)
                out.append(("tools/os88pkg.py", i, n, int(v),
                            EQU[PKG_MIRROR[n]]))
    for n in sorted(set(PKG_MIRROR) - seen):
        # RENAMED OR GONE, which is the way a hand-written mapping stops
        # watching without saying so. -1 can never equal the include's value,
        # so it reads as stale rather than as absent.
        out.append(("tools/os88pkg.py", 0, n, -1, EQU[PKG_MIRROR[n]]))
    return out


def main():
    for n in sorted(OFF, key=lambda k: OFF[k]):
        print("  %-12s os88_image_end + %d" % (n, OFF[n]))
    print("  %-12s %d" % ("OP_BSS", OP_BSS))
    if OP_BSS <= max(OFF.values()):
        print("os88parts: OP_BSS (%d) is not past the last field (%d) - the "
              "chain and its total disagree" % (OP_BSS, max(OFF.values())))
        return 1
    bad = 0
    for path, line, name, mine, ours in scan() + pkg_copies():
        stale = mine != ours
        bad += stale
        print("  %-28s %s = %d%s" % ("%s:%d" % (path, line), name, mine,
                                     "" if not stale
                                     else "   STALE - the include says %d" % ours))
    print("os88parts: %d local copies, %d stale"
          % (len(scan()) + len(pkg_copies()), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
