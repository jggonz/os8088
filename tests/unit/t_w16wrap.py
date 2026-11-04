#!/usr/bin/env python3
"""t_w16wrap - no near call or jmp in WIN16 that QEMU takes to the wrong place
(SPEC.md 124.31.3).

A near `call`/`jmp` carries a 16-bit displacement and the CPU adds it to IP
modulo 64 KB, so in a section longer than 32 KB a call from its first bytes
to its last is a "negative" displacement whose IP wraps - legal 8086, which
every real CPU runs. QEMU (11.0) does not, on one path: when the jump stays
on the 4 KB page it starts on, it chains the two blocks without the 16-bit
wrap, and that page is shared only when the segment's base is not 4 KB
aligned and the source and the wrapped target are within 4 KB of the 64 KB
seam. The library part (`.w16dlg`) is longer than 60 KB, and on kern_mega
its first function called its arm's thunk at the part's end: the CPU went
to the part's base minus 0C91h, ran zeros, pushed ES until the UI task's
stack had gone round LOW_SEG, and the canary called it a STACK OVERFLOW -
only when the part's claim landed at such a base, which is why a 1 KB task
stack, moving the heap, hid it.

So: in both builds (real mode, and the kern_mega arm, -DPKG_MEGA), in the
image and every part, no E8/E9 whose IP wraps with a displacement under
4 KB. A wrap of 4 KB or more never shares a page, and QEMU masks it.

    python3 tests/unit/t_w16wrap.py
"""
import glob
import hashlib
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILDS = (("real", [], "build/win16"), ("mega", ["-DDOS_MEGA", "-DPKG_MEGA"], "build/megak/apps/win16"))
SECT = re.compile(r"---- Section (\S+) -+\s+class:\s+\S+\s+length:\s+([0-9A-F]+)\s+"
                  r"start:\s+([0-9A-F]+)")
PAGE = 0x1000


def sources():
    return sorted(glob.glob(os.path.join(ROOT, "apps", "win16", "*.*"))
                  + glob.glob(os.path.join(ROOT, "apps", "dos", "*.*"))
                  + glob.glob(os.path.join(ROOT, "apps", "*.inc")))


def assemble(tag, defs, built):
    """WIN16's flat binary and map for one build: the Makefile's own (it
    writes the map beside win16.bin) when it is newer than every source, else
    assembled here and cached under a hash of every source file"""
    b, m = os.path.join(ROOT, built + ".bin"), os.path.join(ROOT, built + ".map")
    if os.path.exists(b) and os.path.exists(m):
        newest = max(os.path.getmtime(f) for f in sources())
        if min(os.path.getmtime(b), os.path.getmtime(m)) >= newest:
            return b, m
    h = hashlib.sha256()
    for f in sources():
        h.update(f.encode() + b"\0" + open(f, "rb").read())
    cache = os.path.join(ROOT, "build", "win16", "wrap-%s-%s" % (tag, h.hexdigest()[:16]))
    if not all(os.path.exists(cache + x) for x in (".map", ".bin")):
        os.makedirs(os.path.dirname(cache), exist_ok=True)
        for old in glob.glob(os.path.join(os.path.dirname(cache), "wrap-%s-*" % tag)):
            os.remove(old)
        with tempfile.TemporaryDirectory() as d:
            w, m, o = (os.path.join(d, n) for n in ("w.asm", "w.map", "w.bin"))
            with open(w, "w") as f:
                f.write('%%include "win16/win16.asm"\n[map all %s]\n' % m)
            subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/", "-I", "apps/win16/",
                            "-I", "apps/dos/"] + defs + ["-o", o, w], check=True, cwd=ROOT)
            open(cache + ".map", "w").write(open(m).read())
            open(cache + ".bin", "wb").write(open(o, "rb").read())
    return cache + ".bin", cache + ".map"


def labels(text):
    """section -> sorted (offset, name), for naming a site"""
    out, sec = {}, None
    for line in text.splitlines():
        m = re.match(r"---- Section (\S+)", line)
        if m:
            sec = m.group(1)
            continue
        f = line.split()
        if sec and len(f) == 3 and re.fullmatch(r"[0-9A-F]+", f[0]) and re.fullmatch(r"[0-9A-F]+", f[1]):
            out.setdefault(sec, []).append((int(f[1], 16), f[2]))
    for v in out.values():
        v.sort()
    return out


# data tables inside a part: ndisasm reads their bytes as instructions, and an
# E8/E9 in one is a "call" that does not exist. Each is a label and its _end.
DATA = ("w16_pnames",)


def data_ranges(tab):
    """[(lo, hi)) of the DATA tables in one section's label list"""
    d = dict((n, o) for o, n in tab)
    return [(d[n], d[n + "_end"]) for n in DATA if n in d and n + "_end" in d]


def name(tab, at):
    best = None
    for off, n in tab:
        if off > at:
            break
        best = (off, n)
    return "%s+%X" % (best[1], at - best[0]) if best else "%04X" % at


def scan(binf, mapf):
    """(section, length, wraps, [(site, target)]) for each section"""
    img = open(binf, "rb").read()
    text = open(mapf).read()
    tabs = labels(text)
    out = []
    for m in SECT.finditer(text):
        sec, length, start = m.group(1), int(m.group(2), 16), int(m.group(3), 16)
        with tempfile.NamedTemporaryFile(suffix=".bin") as t:
            t.write(img[start:start + length])
            t.flush()
            dis = subprocess.run(["ndisasm", "-b", "16", t.name], capture_output=True,
                                 text=True, check=True).stdout
        wraps, bad = 0, []
        skip = data_ranges(tabs.get(sec, []))
        for line in dis.splitlines():
            f = line.split(None, 2)
            if len(f) < 3 or f[1][:2] not in ("E8", "E9") or len(f[1]) != 6:
                continue
            at = int(f[0], 16)
            if any(lo <= at < hi for lo, hi in skip):
                continue
            d = int(f[1][4:6] + f[1][2:4], 16)
            d = d - 0x10000 if d & 0x8000 else d
            to = at + 3 + d
            if 0 <= to <= 0xFFFF:
                continue
            wraps += 1
            if abs(d) < PAGE:
                bad.append((name(tabs.get(sec, []), at), name(tabs.get(sec, []), to & 0xFFFF)))
        out.append((sec, length, wraps, bad))
    return out


def main():
    fails = []
    for tag, defs, built in BUILDS:
        for sec, length, wraps, bad in scan(*assemble(tag, defs, built)):
            print("t_w16wrap: %-4s %-8s %5d bytes, %3d wrapping near transfers, %d within 4 KB"
                  % (tag, sec, length, wraps, len(bad)))
            fails += ["%s %s: %s -> %s" % (tag, sec, a, b) for a, b in bad]
    if fails:
        print("t_w16wrap: %d FAILED (SPEC.md 124.31.3) - a near call or jmp whose IP wraps "
              "64 KB within 4 KB of its target; QEMU runs it to the base minus the distance. "
              "Move one end, or make it indirect (W16V_CALL's shape):" % len(fails))
        for f in fails:
            print("  FAIL: " + f)
        return 1
    print("t_w16wrap: passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
