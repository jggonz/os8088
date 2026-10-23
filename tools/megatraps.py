#!/usr/bin/env python3
"""Rank kern_mega's #GP sites from a MEGATRAP=1 boot's E9 log
(docs/plans/KERN-MEGA-PLAN.md 10.3, track 4: the trap census).

    python3 tools/megatraps.py E9.OUT [E9.OUT ...] [--top N] [--by-image]

`make megatrap` builds kern_mega with MEGA_TRAPLOG, whose mega_tlog writes one
line per #GP before mega_gpfix, mega_gpw or mega_null0 completes it:

    T eeee cccc:iiii dddd ssss NAME

error code, CS:IP, DS, ES, and the name at +16 of the image CS runs in (a
package, driver or driver overlay; '-' for anything without the 'O8' magic -
a C package's overlay part, the kernel's own .cold). Each fixup is CORRECT and
costs a fault, so a site that shows thousands of lines is a loop paying for
it, and the order this prints is the order to convert them at the source.

KIND, from what mega_fault's dispatch does with the same frame:
    xfer   error code S != 0: a far CALL/JMP/RETF through an image's DATA
           segment, finished through its code alias (mega_gpfix)
    null0  error 0, DS or ES = 0: segment 0 run as LOW0 for one instruction
           under TF (mega_null0) - TWO exceptions a time, the #GP and the #DB
    write  any other error 0: a write through a code selector or a code
           selector in DS/ES (mega_gpw) - `push cs / pop ds`, or a CS: store

A CS with bit 2 set is an LDT alias: its image is segment (CS & ~7) << 2, and
the IP resolves against that image's own listing (`nasm -l` with the image's
own defines). The NAME column is attributed by the kernel itself; for a '-'
site this also names the package MEGARUN had most recently started, which is
the sweep's own attribution (tools/os88bochs.py).

A NAMELESS image is resolved only when --lst names it by segment, and one
claim can hold different code over a boot: PiXEL loads every decoder part
into the same claim, so a `1600=` listing is right only for the lines logged
while THAT part was loaded. Split the log at the picture you opened.
"""
import argparse
import collections
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LABEL = re.compile(r"^\s*([A-Za-z_.$?][\w.$?@]*):")

TLINE = re.compile(r"^T ([0-9A-F]{4}) ([0-9A-F]{4}):([0-9A-F]{4}) "
                   r"([0-9A-F]{4}) ([0-9A-F]{4}) (.*)$")


def kind(err, ds, es):
    if err:
        return "xfer"
    if ds == 0 or es == 0:
        return "null0"
    return "write"


def image(cs):
    return (cs & ~7) << 2 if cs & 4 else None


def parse(paths):
    sites = collections.Counter()
    meta = {}
    for p in paths:
        last = "(boot)"
        with open(p, "rb") as f:
            for raw in f.read().decode("latin-1").splitlines():
                if raw.startswith("MEGARUN: ") and raw not in (
                        "MEGARUN: ok", "MEGARUN: done") \
                        and not raw.startswith("MEGARUN: no "):
                    last = raw[9:]
                    continue
                m = TLINE.match(raw)
                if not m:
                    continue
                err, cs, ip, ds, es = (int(m.group(i), 16) for i in range(1, 6))
                name = m.group(6).strip() or "-"
                k = kind(err, ds, es)
                key = (cs, ip, k, name)
                sites[key] += 1
                meta.setdefault(key, set()).add(last)
    return sites, meta


_lst = {}
_names = None


def listing(name):
    """build/<x>.lst for the image whose header names it NAME: a package's
    name is not its file's (The Wire is THEWIRE.O88), so build/*.bin's own
    +16 field decides, and the lower-cased name is the fallback."""
    global _names
    if _names is None:
        _names = {}
        b = os.path.join(ROOT, "build")
        for f in sorted(os.listdir(b)):
            if not f.endswith(".bin"):
                continue
            try:
                with open(os.path.join(b, f), "rb") as fh:
                    h = fh.read(32)
            except OSError:
                continue
            if h[:2] == b"O8" and len(h) == 32:
                n = h[16:].split(b"\0")[0].decode("latin-1").upper()
                lst = os.path.join(b, f[:-4] + ".lst")
                if os.path.exists(lst):
                    _names.setdefault(n, lst)
    return _names.get(name.upper(),
                      os.path.join(ROOT, "build", name.lower() + ".lst"))


def where(name, img, ip, lsts):
    """IP against the image's nasm listing: the source line at that offset
    and the nearest label above it. build/<name>.lst unless --lst names the
    image - by its header name, or by its segment for one with none (a
    PiXEL part: `--lst 1600=/tmp/pxjpeg.lst`)."""
    path = (lsts.get(name.upper()) or
            (lsts.get("%04X" % img) if img is not None else None))
    if not path:
        if name == "-":
            return ""
        path = listing(name)
    if path not in _lst:
        rows = []
        if os.path.exists(path):
            label = None
            with open(path, "rb") as f:
                for raw in f.read().decode("latin-1").splitlines():
                    # nasm -l: line number, an 8-digit offset (blank on a
                    # line that emits nothing - a label alone), the bytes,
                    # and the source from column 40
                    src = raw[40:]
                    lm = LABEL.match(src)
                    if lm and not lm.group(1).startswith("."):
                        label = lm.group(1)
                    addr = raw[7:15]
                    if re.fullmatch(r"[0-9A-F]{8}", addr):
                        rows.append((int(addr, 16), label, src.strip()))
        _lst[path] = rows
    best = None
    for addr, label, src in _lst[path]:
        if addr == ip:
            return "%s: %s" % (label, src[:50])
        if addr < ip:
            best = (label, ip - addr)
    return "%s+?" % best[0] if best else ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--lst", action="append", default=[],
                    help="NAME=PATH or SEG=PATH (hex image segment, for an "
                    "image with no header name): that image's nasm listing")
    ap.add_argument("--by-image", action="store_true",
                    help="totals per image name rather than per site")
    a = ap.parse_args()
    sites, meta = parse(a.logs)
    lsts = dict(x.split("=", 1) for x in a.lst)
    lsts = {k.upper(): v for k, v in lsts.items()}
    total = sum(sites.values())
    print("%d #GP lines, %d distinct sites" % (total, len(sites)))
    if a.by_image:
        per = collections.Counter()
        for (cs, ip, k, name), n in sites.items():
            per[(name, k)] += n
        for (name, k), n in per.most_common(a.top):
            print("%8d  %-5s  %s" % (n, k, name))
        return
    print("%8s  %-5s  %-9s  %-5s  %-12s  %-14s  %s" % (
        "count", "kind", "CS:IP", "image", "name", "while", "source"))
    for (cs, ip, k, name), n in sites.most_common(a.top):
        img = image(cs)
        print("%8d  %-5s  %04X:%04X  %-5s  %-12s  %-14s  %s" % (
            n, k, cs, ip, "%04X" % img if img is not None else "gdt",
            name, ",".join(sorted(meta[(cs, ip, k, name)]))[:14],
            where(name, img, ip, lsts)))


if __name__ == "__main__":
    sys.exit(main())
