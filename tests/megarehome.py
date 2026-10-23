#!/usr/bin/env python3
"""Re-home HIGH (docs/plans/KERN-MEGA-PLAN.md 14.38, 14.44; SPEC.md
20.12.10.9): every RE-HOMED program that says OS88_HIGH in its own header
runs ABOVE 1MB on kern_mega, and its conventional memory stays free.

    python3 tests/megarehome.py            the row
    python3 tests/megarehome.py --keep     ...and leave the boot's output

Boots build/mega.img under Bochs (16MB) with C:'s RUN/ holding MEGARUN and the
five re-homed programs that say it - kern_mega's own DOS.O88, SCRIBE, CLEAR
SKIES, WORD and PIXELSTEIN 3D - and lets MEGARUN start each by name (OSAPI_PKG_START, the
Disk window's own path through the loader and its parts). Then it stops the
machine at its debugger prompt and reads the kernel's tables directly:

  1. each program's instance record: I_SPTR is an LDT SELECTOR (TI set, in
     the allocator's range) - its region is a pool pair, not a segment;
  2. the pool (mx_tab) holds a block owned 80h | that slot, of the keep's KB;
  3. WORD and SCRIBE (OS88_HIGHMEM too) hold claims in the pool (0A0h | slot);
  4. the conventional heap holds no claim of 32KB or more owned by any of the
     four - the 50-62KB regions that used to sit at the top of 640KB;
  5. CLEAR SKIES holds at least one LDT descriptor whose base is INSIDE its
     block other than its own pair: OSAPI_MEM_SUBSEG's, which cs_xpand asks
     for to expand its title bands and vocabulary in place;
  6. PIXELSTEIN 3D's keep, over 64KB, is ONE block (KERN-MEGA-PLAN 14.48):
     its pair's limit is its own image + bss less one, and inside the block
     sit exactly the scratch part's code/data pair and the byte textures'
     data selector, each based at its part and none running past the block.

THE FAILING CASE for 6: PXSTEIN.O88 from before 14.48 - its program says
no OS88_HIGH and its keep ran low: `FAIL: Pixelstein 3D I_SPTR 7C40 - a
conventional segment`, verified when the leg was added.

THE FAILING CASE (docs/WRITING-TESTS.md 1): drop OS88_HIGH from any of the
four headers (or the `call ld_hicx` from kernel/loader.inc's step 8b) and
(1) and (2) go red naming the program, its region a conventional segment;
build Skies without the cs_xpand SUBSEG arm and (5) does. Verified both ways
when the row was written, with WORD.O88 from the commit before 14.44 in
place of the new one: `FAIL: WORD I_SPTR 8F60 - a conventional segment`.

It reads build/ (wants=) and writes only under build/bochs-megarehome/.
"""
import contextlib
import io
import os
import re
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs  # noqa: E402

BUILD = os.path.join(ROOT, "build")
MAP = os.path.join(BUILD, "megak", "kernel.map")
# (file on C:, the name its instance record carries, holds a pool claim at
# rest). DOS and SKIES say OS88_HIGHMEM too since KERN-MEGA-PLAN 14.52, but
# neither holds a claim while it sits at its window - DOS claims at a launch,
# SKIES for a world read and a flight - so the check is not asked of them
PROGS = (("dos.o88", os.path.join(BUILD, "megak", "kdos", "DOS.O88"), "DOS",
          False),
         ("scribe.o88", os.path.join(BUILD, "scribe.o88"), "SCRIBE", True),
         ("skies.o88", os.path.join(BUILD, "skies.o88"), "SKIES", False),
         ("word.o88", os.path.join(BUILD, "word.o88"), "WORD", True),
         ("pxstein.o88", os.path.join(BUILD, "pxstein.o88"), "Pixelstein 3D",
          True))
I_RECSZ, INST_MAX = 32, 12
MX_BLKSZ, MX_NBLK = 8, 96        # kernel/mega.inc (KERN-MEGA-PLAN 14.49)
MC_SIZE, MEM_MAX = 10, 32
LOW_LIN = 0x0E60 * 16           # LOW_SEG on kern_mega (kernsize's ladder),
                                # only if SS cannot be read off the machine


def low_lin(out, e9):
    """LOW_SEG's linear base, read off the running machine: SS is LOW_SEG
    everywhere the kernel runs (SPEC.md 2.1), and its descriptor says where.
    A constant here went stale once (0x0EC0 against the ladder's 0x0E60), so
    mem_tab was read from the wrong place without a word."""
    n = os.path.getsize(e9)
    with contextlib.redirect_stdout(io.StringIO()):
        os88bochs.dbg(out, ["sreg"])
    with open(e9, "rb") as f:
        f.seek(n)
        text = f.read().decode("latin-1")
    m = re.search(r"ss:0x[0-9a-f]+, dh=(0x[0-9a-f]+), dl=(0x[0-9a-f]+)", text)
    if not m:
        return LOW_LIN
    dh, dl = int(m.group(1), 16), int(m.group(2), 16)
    return (dl >> 16) | ((dh & 0xFF) << 16) | (dh & 0xFF000000)


def sym(name):
    with open(MAP) as f:
        for line in f:
            p = line.split()
            if len(p) == 3 and p[2] == name:
                return int(p[1], 16)
    raise SystemExit("megarehome: no %s in %s" % (name, MAP))


def words(text, addr):
    """The words `xp /Nhx addr` printed, out of the debugger's transcript."""
    out, on = [], False
    for line in text.splitlines():
        m = re.match(r"<bochs:\d+> xp /\d+hx (0x[0-9a-f]+)", line)
        if m:
            on = int(m.group(1), 16) == addr
            continue
        m = re.match(r"0x[0-9a-f]+ <bogus\+\s*\d+>:\s+(.*)", line)
        if m and on:
            out += [int(w, 16) for w in m.group(1).split()]
    return b"".join(w.to_bytes(2, "little") for w in out)


def w16(b, o):
    return int.from_bytes(b[o:o + 2], "little")


def main():
    keep = "--keep" in sys.argv
    out = os.path.join(BUILD, "bochs-megarehome")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    stage = os.path.join(out, "progs")
    os.makedirs(stage)
    pkgs = []
    for fn, src, _, _ in PROGS:
        dst = os.path.join(stage, fn)
        shutil.copy(src, dst)
        pkgs.append(dst)
    t0 = time.time()
    fails = []
    box = os88bochs.Box(out, pkgs)
    try:
        if not box.wait_for("kern_mega:", 240):
            raise SystemExit("megarehome: no kern_mega line in 240s")
        time.sleep(15)
        box.capture()
        os88bochs.dbl(591, 160)         # C:
        time.sleep(10)
        os88bochs.dbl(150, 123)         # RUN
        time.sleep(8)
        os88bochs.dbl(160, 139)         # 0RUN.O88: MEGARUN starts the rest
        end = time.time() + 180
        while time.time() < end:
            ls = box.lines()
            if "MEGARUN: done" in ls or any(
                    l.startswith("kern_mega FAULT") for l in ls):
                break
            time.sleep(2)
        time.sleep(10)                  # the last one's window
        lines = box.lines()
        for l in lines:
            if l.startswith("kern_mega FAULT") or l.startswith("MEGARUN: no "):
                fails.append(l)
        if "MEGARUN: done" not in lines:
            fails.append("MEGARUN never said done")
        os88bochs.shot(os.path.join(out, "desk.png"))
        inst = 0x600 + sym("inst_tab")
        e9 = box.e9
        low = low_lin(out, e9)          # SS's base: LOW_SEG, off the machine
        mx = low + sym("mx_tab")        # .lowbss since KERN-MEGA-PLAN 14.49
        mem = low + sym("mem_tab")
        n = os.path.getsize(e9)
        cmds = ["xp /%dhx 0x%x" % (I_RECSZ * INST_MAX // 2, inst),
                "xp /%dhx 0x%x" % (MX_BLKSZ * MX_NBLK // 2, mx),
                "xp /%dhx 0x%x" % (MC_SIZE * MEM_MAX // 2, mem),
                "info ldt"]
        with contextlib.redirect_stdout(io.StringIO()):
            os88bochs.dbg(out, cmds)    # (its transcript is read from e9)
        with open(e9, "rb") as f:
            f.seek(n)
            text = f.read().decode("latin-1")
    finally:
        box.stop()

    I = words(text, inst)
    X = words(text, mx)
    M = words(text, mem)
    if len(I) < I_RECSZ * INST_MAX or len(X) < MX_BLKSZ * MX_NBLK:
        fails.append("the debugger read came back short (%d, %d bytes)"
                     % (len(I), len(X)))
        I = X = M = b""
    ldt = []
    for line in text.splitlines():
        m = re.match(r"LDT\[(0x[0-9a-f]+)\]=(Code|Data) segment, base="
                     r"(0x[0-9a-f]+), limit=(0x[0-9a-f]+)", line)
        if m:
            ldt.append((int(m.group(1), 16), m.group(2), int(m.group(3), 16),
                        int(m.group(4), 16)))
    blocks = []
    for k in range(MX_NBLK if X else 0):
        e = X[k * MX_BLKSZ:(k + 1) * MX_BLKSZ]
        if w16(e, 2):
            blocks.append((w16(e, 0), w16(e, 2), e[4]))
    conv = []
    for k in range(MEM_MAX if M else 0):
        r = M[k * MC_SIZE:(k + 1) * MC_SIZE]
        if w16(r, 0):
            conv.append((w16(r, 0), w16(r, 2), w16(r, 4)))

    for fn, _, name, highmem in PROGS:
        slot = None
        for k in range(INST_MAX if I else 0):
            rec = I[k * I_RECSZ:(k + 1) * I_RECSZ]
            if rec[0] and rec[2] & 0x80 and \
                    rec[12:28].split(b"\0")[0].decode("latin-1") == name:
                slot, sptr, size = k, w16(rec, 6), w16(rec, 8)
        if slot is None:
            fails.append("%s: no instance - it did not start" % name)
            continue
        if not sptr & 4:
            fails.append("%s I_SPTR %04X - a conventional segment: its region "
                         "did not go above 1MB" % (name, sptr))
            continue
        mine = [b for b in blocks if b[2] == 0x80 | slot]
        if len(mine) != 1:
            fails.append("%s: %d pool blocks owned %02X, not one"
                         % (name, len(mine), 0x80 | slot))
            continue
        off, kb, _ = mine[0]
        claims = [b for b in blocks if b[2] == 0xA0 | slot]
        if highmem and not claims:
            fails.append("%s says OS88_HIGHMEM and holds no claim in the pool"
                         % name)
        big = [c for c in conv if c[2] == slot and c[1] >= 32 * 64]
        if big:
            fails.append("%s still holds %.1fKB of conventional memory at %04X"
                         % (name, big[0][1] / 64, big[0][0]))
        # the pair and anything else inside the block, by base
        pairs = [d for d in ldt if d[0] * 8 + 4 == sptr]
        base = pairs[0][2] if pairs else None
        inside = [d for d in ldt if base is not None and
                  base <= d[2] < base + kb * 1024 and
                  d[0] * 8 + 4 not in (sptr, sptr - 8)]
        print("megarehome: %-6s slot %d I_SPTR %04X I_SIZE %5d  block %3dK at "
              "%dK  claims in the pool %d  other selectors inside it %d"
              % (name, slot, sptr, size, kb, off, len(claims), len(inside)))
        if name == "SKIES" and not inside:
            fails.append("SKIES: no OSAPI_MEM_SUBSEG selector inside its "
                         "block - cs_xpand did the sum, or did not ask")
        if name == "Pixelstein 3D":
            # 14.48: ONE block of the whole keep (over 64KB), the pair over
            # the program's own image + bss alone, and a selector per part
            # based at the part's first byte and as long as the part: the
            # scratch's code/data pair and the byte textures' data
            if kb <= 64:
                fails.append("PXSTEIN: a %dKB block - its keep is over 64KB, "
                             "so the scratch and the textures are not in it"
                             % kb)
            if pairs and pairs[0][3] != size - 1:
                fails.append("PXSTEIN: the pair's limit is %04X, not its "
                             "image + bss less one (%04X)"
                             % (pairs[0][3], size - 1))
            for d in inside:
                print("megarehome:   PXSTEIN part selector %04X %s base +%d "
                      "limit %04X" % (d[0] * 8 + 4, d[1], d[2] - base, d[3]))
            codes = [d for d in inside if d[1] == "Code"]
            datas = [d for d in inside if d[1] == "Data"]
            if len(codes) != 1 or len(datas) != 2:
                fails.append("PXSTEIN: %d code and %d data selectors inside "
                             "its block, not the scratch's pair and the byte "
                             "textures' (1 and 2)" % (len(codes), len(datas)))
            for d in inside:
                if d[2] - base + d[3] + 1 > kb * 1024:
                    fails.append("PXSTEIN: selector %04X runs past its block"
                                 % (d[0] * 8 + 4))
    print("megarehome: %.0fs under Bochs" % (time.time() - t0))
    if not keep and not fails:
        shutil.rmtree(out, ignore_errors=True)
    for f in fails:
        print("FAIL: " + f)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
