#!/usr/bin/env python3
"""The DOS window's TAIL, mechanically (docs/plans/DOS-HEADLESS-PLAN.md 4.4).

    doswtail.py check  HOST.bin HOST.map SHIFTED.bin   # fail on a stray ref
    doswtail.py report HOST.bin HOST.map SHIFTED.bin   # ...list them, exit 0
    doswtail.py cut    HOST.bin HOST.map PART0.bin PART1.bin

`apps/dos/dos.asm` built with DOS_WTAIL puts everything that only SHOWS the
window into a section of its own, `.wtext`, after every byte of the resident
host: `[header][core slot][resident code][resident bss][the window]`. A
headless launch never loads the last part, so in that region the tail's
addresses are FREE MEMORY - and nasm resolves a near reference across a
section without a word. A stray `call con_write` would run whatever claim
landed there, a stray `mov [con_cx], ax` would write into it.

So the resident host may name the tail only inside a WIN_ONLY bracket, whose
opening is the test itself - `call dos_hq` (`cmp byte [dos_head], 0` / `ret`,
one copy of the five-byte compare for every bracket) and a `jne` over the
window's arm - and this checker proves it. It does not disassemble anything
to find a reference, because a disassembler cannot tell code from data:

  * SHIFTED.bin is the same assembly with the tail moved up DOS_WSHIFT bytes
    (dos.asm pads the head of `.wtext`), so EVERY byte of the resident host
    whose value depends on where the tail is - a branch, a memory operand, an
    immediate, a table entry - differs between the two files, and no other
    byte does;
  * the brackets are read off the bytes too: `E8 <to dos_hq> 75 rr` is a
    short bracket over the next rr bytes, `E8 <to dos_hq> 74 03 E9 rrrr` a
    near one;
  * any differing byte outside every bracket FAILS the build, named by the
    nearest symbol of HOST.map and disassembled.

`cut` writes the two parts DOS.O88 carries (SPEC.md 96.44.19): part 0 is the
resident host up to the end of its bss (its header's image + bss), part 1 the
tail from `.wtext`'s first byte, which the parts standard lands at
CS:roundup16(part 0's length) - the offset it was assembled at.
"""
import os
import shutil
import subprocess
import sys
import tempfile


def fail(msg):
    sys.exit("doswtail: FAIL: " + msg)


def read_map(path):
    """(sections {name: (start, length)}, symbols {name: value}, sorted
    [(value, name)] of the non-local section symbols)"""
    secs, syms, mode, absn = {}, {}, None, set()
    for line in open(path):
        if line.startswith("-- Sections (summary)"):
            mode = "sum"
            continue
        if line.startswith("-- Sections (detailed)"):
            mode = None
            continue
        if line.startswith("---- "):
            mode = "sym" if "No Section" not in line else "abs"
            continue
        p = line.split()
        if mode == "sum" and len(p) == 6 and p[5].startswith("."):
            try:
                secs[p[5]] = (int(p[1], 16), int(p[3], 16))
            except ValueError:
                pass
        elif mode in ("sym", "abs") and len(p) in (2, 3):
            try:
                v = int(p[0], 16)
            except ValueError:
                continue
            if mode == "sym" or p[-1] not in syms:
                syms[p[-1]] = v
            if mode == "abs":
                absn.add(p[-1])
    for n in absn:
        syms.pop(n, None) if n != "dos_head" else None
    return secs, syms


def brackets(img, hq, end):
    """[(lo, hi)] of every WIN_ONLY bracket in img[:end]: a near call to the
    test, dos_hq, and the skip after it"""
    out = []
    i = img.find(b"\xE8", 0, end)
    while i >= 0:
        j = i + 3
        if j + 5 > len(img) or \
                (j + (img[i + 1] | (img[i + 2] << 8))) & 0xFFFF != hq:
            i = img.find(b"\xE8", i + 1, end)
            continue
        if img[j] == 0x75:                                  # jne short
            r = img[j + 1]
            if r < 0x80:
                out.append((j + 2, j + 2 + r))
        elif img[j] == 0x74 and img[j + 1] == 3 and img[j + 2] == 0xE9:
            r = img[j + 3] | (img[j + 4] << 8)               # je +3; jmp near
            if r < 0x8000:
                out.append((j + 5, j + 5 + r))
        i = img.find(b"\xE8", i + 1, end)
    return out


def disasm(img, start, at):
    """the instruction of img that covers byte `at`, disassembling from
    `start` - a label, so the decode is in step with the code there"""
    nd = shutil.which("ndisasm")
    if not nd or not 0 <= start <= at:
        return "?"
    fd, tmp = tempfile.mkstemp(prefix="doswtail", suffix=".bin")
    try:
        os.write(fd, img[start:at + 8])
        os.close(fd)
        out = subprocess.run([nd, "-b", "16", "-o", hex(start), tmp],
                             capture_output=True, text=True).stdout
    finally:
        os.unlink(tmp)
    for line in out.splitlines():
        p = line.split(None, 2)
        if len(p) == 3:
            o, n = int(p[0], 16), len(p[1]) // 2
            if o <= at < o + n:
                return p[2]
    return "?"


def layout(mapf):
    secs, syms = read_map(mapf)
    if ".text" not in secs or ".wtext" not in secs:
        fail("%s has no .wtext section: build it with -DDOS_WTAIL" % mapf)
    if "dos_head" not in syms or "dos_hq" not in syms:
        fail("%s has no dos_head/dos_hq: the bracket's test" % mapf)
    tlen = secs[".text"][1]
    wstart, wlen = secs[".wtext"]
    named = sorted((v, n) for n, v in syms.items()
                   if "." not in n and not n.startswith("..")
                   and v < wstart + wlen and n not in ("dos_head",))
    sync = sorted(set(v for n, v in syms.items() if v < tlen))
    return tlen, wstart, wlen, syms, named, sync


def owner(named, at):
    best = None
    for v, n in named:
        if v <= at:
            best = (v, n)
        else:
            break
    return "%s+0x%X" % (best[1], at - best[0]) if best else "?"


def check(argv, strict):
    host, mapf, shifted = argv
    a = open(host, "rb").read()
    b = open(shifted, "rb").read()
    tlen, wstart, wlen, syms, named, sync = layout(mapf)
    if len(a) < wstart:
        fail("%s is shorter than its own map's .wtext start" % host)
    diff = [i for i in range(tlen) if i >= len(b) or a[i] != b[i]]
    if len(b) - len(a) <= 0:
        fail("%s is not longer than %s: was it built with DOS_WSHIFT?"
             % (shifted, host))
    br = brackets(a, syms["dos_hq"], tlen)
    inside = lambda i: any(lo <= i < hi for lo, hi in br)
    stray = [i for i in diff if not inside(i)]
    # one report per instruction: differing bytes within 4 of each other
    sites = []
    for i in stray:
        if sites and i - sites[-1][-1] <= 3:
            sites[-1].append(i)
        else:
            sites.append([i])
    for s in sites:
        st = max([v for v in sync if v <= s[0]] or [0])
        print("doswtail: %04X  %-32s  %s" % (s[0], owner(named, s[0]),
              disasm(a, st, s[0])))
    nref = len(diff) - len(stray)
    print("doswtail: resident %d bytes, tail %d at 0x%X; %d bracket(s), "
          "%d tail-dependent byte(s) inside them, %d site(s) outside"
          % (tlen, wlen, wstart, len(br), nref, len(sites)))
    if strict and sites:
        fail("%d reference(s) from the resident host into the window's tail "
             "outside a WIN_ONLY bracket (apps/dos/dos.asm's DOS_WTAIL note): "
             "in a headless region each one names FREE memory" % len(sites))


def cut(argv):
    host, mapf, p0, p1 = argv
    a = open(host, "rb").read()
    tlen, wstart, wlen, syms, named, sync = layout(mapf)
    img = a[8] | (a[9] << 8)
    bss = a[10] | (a[11] << 8)
    if img + bss != tlen:
        fail("part 0's header says image %d + bss %d = %d, and .text is %d: "
             "the resident bss must end the section" % (img, bss, img + bss, tlen))
    if wstart != (tlen + 15) & ~15:
        fail(".wtext starts at 0x%X, not at the paragraph after part 0's end "
             "(0x%X) - which is where the parts standard lands part 1"
             % (wstart, (tlen + 15) & ~15))
    if any(a[tlen:wstart]):
        fail("the paragraph pad between the parts is not zeros")
    if len(a) != wstart + wlen:
        fail("%s is %d bytes and its map says it ends at %d"
             % (host, len(a), wstart + wlen))
    open(p0, "wb").write(a[:tlen])
    open(p1, "wb").write(a[wstart:])
    print("doswtail: part 0 %d bytes (the resident host), part 1 %d (the "
          "window) at CS:0x%X" % (tlen, wlen, wstart))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    v, a = sys.argv[1], sys.argv[2:]
    if v in ("check", "report") and len(a) == 3:
        check(a, v == "check")
    elif v == "cut" and len(a) == 4:
        cut(a)
    else:
        sys.exit(__doc__)


main()
