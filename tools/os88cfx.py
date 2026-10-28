#!/usr/bin/env python3
"""os88cfx - the kernel's own list of the words that name `.cold`'s segment.

docs/plans/MOVABLE-COLD-PLAN.md 4, wave 1. A kernel that moves `.cold` by
itself (the slide over a dropped VGA rung) has to re-point every word outside
`.cold` that names its segment: in `.text`, in the boot overlay's window half
(`.ovl`) and in the blob (`.boot2`). Modules are not on the list - mod_cfix
finds theirs from the bytes alone - and `.cold` names no segment of its own
(SPEC.md 2.10.2), so a site inside it is refused here as os88romfix refuses it.

How the list is found is os88rom.py's and os88romfix.py's way: assemble the
kernel twice more with `.cold`'s segment at two other values (kernel.asm's
COLD_CS hook) and diff. Every byte that moves is half of a word that names the
segment, and the pair proves the list complete, because anything that depends
on the segment has to change when it does.

The list is STAMPED into `cfx_tab`, kernel.asm's `.cfx`: CFX_MAX bytes of
zeros in a section of its own straight after `.cold` in the file, so stage 2's
one read lands it on the RAM just past the kernel, under the blob's lift, and
it costs no resident byte. (The blob was the first home, and its 14th sector
cost 360KB and 720KB a whole int 13h call each.) Three groups - `.text`,
`.ovl`, `.boot2` - each a stream of offset STEPS from the previous site (from
0): a byte 1-255 is a step, a 0 is followed by the step as a word, and a word
of 0 ends the group (no two sites share an offset, so a step is never 0).

Usage, from the Makefile, straight after the kernel's final assembly:
    python3 tools/os88cfx.py <kernel-full.bin> <kernel.map> -- <nasm argv>
where <nasm argv> is that assembly's command less -o and --before. Writes the
table into <kernel-full.bin> in place; a kernel with no `.cfx` (kern_small,
kern_mega) is left as it is.

    python3 tools/os88cfx.py --selfcheck
"""

import os
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

FIX_A, FIX_B = 0xF601, 0xF702       # os88romfix.py's pair


def fail(msg):
    sys.exit("os88cfx: " + msg)


def sections(maptext):
    """name -> (file start, file end, vstart)"""
    out = {}
    for m in re.finditer(r"^\s*([0-9A-F]+)\s+([0-9A-F]+)\s+([0-9A-F]+)\s+"
                         r"[0-9A-F]+\s+progbits\s+(\S+)\s*$", maptext, re.M):
        out[m[4]] = (int(m[2], 16), int(m[3], 16), int(m[1], 16))
    return out


def symbol(maptext, section, name):
    t = maptext[maptext.index("-- Symbols"):]
    i = t.find("---- Section " + section + " ")
    if i < 0:
        return None
    j = t.find("---- Section", i + 10)
    for line in t[i:j if j > 0 else None].splitlines():
        p = line.split()
        if len(p) == 3 and p[2] == name:
            return int(p[1], 16)
    return None


def encode(offsets):
    out, prev = bytearray(), 0
    for o in sorted(offsets):
        step = o - prev
        if step <= 0:
            fail(f"two sites at one offset ({o:#x})")
        if step < 256:
            out.append(step)
        else:
            out += bytes((0, step & 0xFF, step >> 8))
        prev = o
    return bytes(out + b"\0\0\0")


def decode(stream, at=0):
    """The kernel's decoder, in Python: (offsets, position after the end)."""
    out, prev = [], 0
    while True:
        b = stream[at]
        at += 1
        if b:
            prev += b
        else:
            w = stream[at] | stream[at + 1] << 8
            at += 2
            if not w:
                return out, at
            prev += w
        out.append(prev)


def assemble(argv, cs, tmp):
    out = os.path.join(tmp, f"k{cs:04x}.bin")
    r = subprocess.run([*argv, f"-DCOLD_CS=0x{cs:04X}", "-o", out],
                       capture_output=True, text=True)
    if r.returncode:
        fail(f"nasm refused the kernel with COLD_CS={cs:#06x}:\n"
             + r.stderr[-2000:])
    with open(out, "rb") as f:
        return f.read()


def coldhome(maptext, secs):
    with open(os.path.join(ROOT, "kernel", "kernel.asm")) as f:
        kseg = int(re.search(r"^KERNEL_SEG\s+equ\s+(0x[0-9A-Fa-f]+)",
                             f.read(), re.M).group(1), 16)
    tst = secs[".text"][0]
    cst, _cen, vst = secs[".cold"]
    if (cst - tst) % 16:
        fail("`.cold` does not start on a paragraph past `.text`")
    return kseg + (cst - tst) // 16 - vst // 16


def lists(full, a, b, secs, home, tab):
    """The three groups, and a count of the module sites left to mod_cfix."""
    if not (len(a) == len(b) == len(full)):
        fail("moving `.cold`'s segment changed the kernel's length")
    out = {".text": [], ".ovl": [], ".boot2": []}
    mods = 0
    i = 0
    while i < len(a):
        if a[i] == b[i]:
            i += 1
            continue
        w1, w2 = a[i] | a[i + 1] << 8, b[i] | b[i + 1] << 8
        if w1 != FIX_A or w2 != FIX_B:
            fail(f"file offset {i:#x} moves with `.cold`'s segment but is not "
                 f"a plain word naming it")
        if full[i] | full[i + 1] << 8 != home:
            fail(f"file offset {i:#x} is a site but the kernel does not hold "
                 f"COLD_SEG ({home:#06x}) there")
        where = next((n for n, (s, e, _v) in secs.items() if s <= i < e),
                     None)
        if where in out:
            out[where].append(i - secs[where][0])
        elif where == ".cfx":
            fail("a site inside cfx_tab itself")
        elif where == ".cold":
            fail(f"`.cold` names its own segment at +{i - secs['.cold'][0]:#x}"
                 f" - SPEC.md 2.10.2, tools/os88romfix.py --check")
        elif where and where.startswith(".mod"):
            mods += 1                   # mod_cfix's (tools/os88rom.py proves)
        else:
            fail(f"a site at file offset {i:#x} is in {where or 'no section'}"
                 f" - nothing re-points it there")
        i += 2
    return out, mods


def stamp(fullp, mapp, argv):
    with open(mapp) as f:
        maptext = f.read()
    secs = sections(maptext)
    for need in (".text", ".ovl", ".boot2", ".cold"):
        if need not in secs:
            fail(f"the kernel has no {need} section")
    t0 = symbol(maptext, ".cfx", "cfx_tab")
    t1 = symbol(maptext, ".cfx", "cfx_tab_end")
    if t0 is None or t1 is None:
        return                          # kern_small, kern_mega: no slide
    b0 = secs[".cfx"]
    tab = (b0[0] + t0 - b0[2], b0[0] + t1 - b0[2])
    with open(fullp, "rb") as f:
        full = bytearray(f.read())
    if any(full[tab[0]:tab[1]]):
        fail(f"cfx_tab in {fullp} is not zero - stamped already, or not the "
             f"assembly the map describes")
    with tempfile.TemporaryDirectory(prefix="os88cfx-") as tmp:
        with ThreadPoolExecutor(2) as ex:
            a, b = ex.map(lambda cs: assemble(argv, cs, tmp), (FIX_A, FIX_B))
    home = coldhome(maptext, secs)
    groups, mods = lists(full, a, b, secs, home, tab)
    blob = b"".join(encode(groups[g]) for g in (".text", ".ovl", ".boot2"))
    if len(blob) > tab[1] - tab[0]:
        fail(f"the list is {len(blob)} bytes and cfx_tab holds "
             f"{tab[1] - tab[0]} - raise CFX_MAX in kernel.asm")
    # ...and it decodes to what was meant, by the kernel's own rule
    at = 0
    for g in (".text", ".ovl", ".boot2"):
        got, at = decode(blob, at)
        if got != sorted(groups[g]):
            fail(f"the {g} group does not decode to itself")
    full[tab[0]:tab[0] + len(blob)] = blob
    with open(fullp, "wb") as f:
        f.write(full)
    print(f"os88cfx: {len(groups['.text'])}+{len(groups['.ovl'])}+"
          f"{len(groups['.boot2'])} words name `.cold` ({home:#06x}), "
          f"{len(blob)} of cfx_tab's {tab[1] - tab[0]} bytes; {mods} module "
          f"words are mod_cfix's")


def blank(image, maptext):
    """`image` with cfx_tab zeroed, for a reader that compares a fresh
    assembly with the stamped kernel (tools/os88sym.py)."""
    secs = sections(maptext)
    t0 = symbol(maptext, ".cfx", "cfx_tab")
    t1 = symbol(maptext, ".cfx", "cfx_tab_end")
    if t0 is None or t1 is None or ".cfx" not in secs:
        return image
    b0 = secs[".cfx"]
    s, e = b0[0] + t0 - b0[2], b0[0] + t1 - b0[2]
    out = bytearray(image)
    out[s:e] = bytes(e - s)
    return bytes(out)


def selfcheck():
    for offs in ([], [3], [1, 2, 255, 256, 511, 767, 60000], list(range(5, 4000, 300))):
        s = encode(offs)
        got, end = decode(s + b"\x07")
        assert got == sorted(offs) and end == len(s), (offs, got)
    print("os88cfx: selfcheck ok")


def main():
    if sys.argv[1:] == ["--selfcheck"]:
        selfcheck()
        return
    if len(sys.argv) < 5 or sys.argv[3] != "--":
        fail("usage: os88cfx.py <kernel-full.bin> <kernel.map> -- <nasm argv>")
    stamp(sys.argv[1], sys.argv[2], sys.argv[4:])


if __name__ == "__main__":
    main()
