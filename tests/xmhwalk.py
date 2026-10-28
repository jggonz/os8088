#!/usr/bin/env python3
"""The Task Manager's names for the HMA - SPEC.md 28.4.13 - without a 286.

    python3 tests/xmhwalk.py

OSAPI_XMEM_WALK with BX = XMW_FIXED answers, on kern_big with the HMA tail
installed, one record for `.cold` and the tail and one per live block of the
tail's pool, IN FRONT of the extended pool's own blocks. The XMEM.DRV half is
its xm_walk; the HMA half is the TAIL's (boot/osrom.asm's hb_walk, ROMF_HWALK),
far-called through the door xm_hma banks off COLDHMA.SYS's rom_id +28 -
because the table it walks is the tail's, in the HMA, and 128 bytes of walk
there cost no 286 any heap where they cost every one of them a KB inside
XMEM.DRV (docs/plans/HMA-PLAN.md).

No emulator in this tree reaches it: MartyPC is an 8088 with nothing above
1MB, and under QEMU no row reads the walk. So this runs the SHIPPED BYTES -
build/xmem.bin at a segment of its own and build/coldhma.sys whole at
FFFF:its TAIL_AT, which is where the tail's resident half lands - under
unicorn, with a block table, driver headers and XMEM.DRV's own table written
in, and holds every record to a model of the contract: the order, each
XMW_OFF a block's segment, XMW_KB rounded UP, XMW_HMAI against XMW_HMAB by
whether a block's owner is its own segment, XMW_CLS off the owner's header
or 0 without one. Then room for fewer records than there are (none, one,
part of the pool), BX not asking, and no tail at all (the door's offset 0) -
and every register but AX, the flags' IF and DF, and the bytes either side
of the room as they were.

Broken on purpose: XMW_KB truncated instead of rounded, and the image/buffer
test inverted, each fail the model.

Needs nasm, the `unicorn` Python module, and a built tree (make).
"""
import os
import struct
import subprocess
import sys
import tempfile

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX,
                               UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI,
                               UC_X86_REG_BP, UC_X86_REG_SP, UC_X86_REG_IP,
                               UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES,
                               UC_X86_REG_SS, UC_X86_REG_EFLAGS)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD = os.environ.get("OS88_BUILD", os.path.join(ROOT, "build"))
XSEG = 0x2000                   # XMEM.DRV's image: CS = DS
RSEG = 0x4000                   # the walk's room, and the far-call stub
SSEG = 0x5000                   # a stack of its own (SS != DS, as on iron)
STUB = 0x0100                   # RSEG:STUB: call near xm_walk / hlt
ROOM = 0x1000                   # RSEG:ROOM: the records
XMW_FIXED = 0x4658
XMW_HMAK, XMW_HMAI, XMW_HMAB = 0xF5, 0xF6, 0xF7
HB_ORG = 0xC000                 # kernel.asm's DRV_HMA_ORG
FAILS = []


def check(cond, what):
    print("  %s  %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


def xmem_syms(tmp):
    """xm_walk, xm_hdoor, xm_tab and xm_tab_end out of a map of the same
    source build/xmem.bin was assembled from - refused if the bytes differ"""
    out = os.path.join(tmp, "x.bin")
    mp = os.path.join(tmp, "x.map")
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "drivers/",
                    "-I", "apps/", "--before", "[map symbols %s]" % mp,
                    "-o", out, "drivers/xmem/xmem.asm"], cwd=ROOT, check=True)
    img = open(os.path.join(BUILD, "xmem.bin"), "rb").read()
    if open(out, "rb").read() != img:
        sys.exit("xmhwalk: build/xmem.bin is not this tree's - run make")
    syms = {}
    for line in open(mp):
        p = line.split()
        if len(p) == 3 and all(c in "0123456789ABCDEF" for c in p[0]):
            syms[p[2]] = int(p[0], 16)
    return img, syms


def tail():
    d = open(os.path.join(BUILD, "coldhma.sys"), "rb").read()
    if d[:8] != b"OS88ROM\0" or d[8] != 3:
        sys.exit("xmhwalk: build/coldhma.sys is not an HMA tail")
    at, htab, ln, hbhi = struct.unpack_from("<HHHH", d, 10)
    door = struct.unpack_from("<H", d, 28)[0]
    return d, at, htab, hbhi, door


def hb_seg(base):
    return ((base >> 4) + 0xFFFF - (HB_ORG >> 4)) & 0xFFFF


# One pool, eight records (boot/osrom.asm's HB_N): base (0 = free), bytes,
# owner. Images own themselves; buffers are owned by an image's segment; a
# free record sits between live ones; sizes on, under and over a KB edge.
SND, HDD, NET = hb_seg(0x0010), hb_seg(0x2EB0), hb_seg(0x3EB0)
POOL = [
    (0x0010, 11924, SND),       # SOUND.DRV's image, 12 KB rounded up
    (0x2EB0, 4096, HDD),        # HDD.DRV's, exactly 4 KB
    (0, 0, 0),                  # a free record
    (0x3EB0, 4163, NET),        # NET.DRV's, 5 KB
    (0x4F00, 1, SND),           # a 1-byte buffer of SOUND's: 1 KB
    (0x5000, 1025, NET),        # a buffer NET holds: 2 KB
    (0, 0, 0),
    (0x5500, 2048, 0xF4FF),     # owned by a segment with no 'O8' header
]
CLS = {SND: 2, HDD: 1, NET: 0x0C}       # each driver header's +3
XTAB = [(0, 64, 3), (64, 0, 0), (100, 16, 0xFF)]   # XMEM.DRV's own: XB_OFF,
                                        # XB_KB (0 = free), XB_OWN


def model(hbhi, room, fixed, tailed):
    recs = []
    if fixed and tailed:
        recs.append((0xFFFF, 64 - (hbhi >> 10), XMW_HMAK))
        for base, n, own in POOL:
            if not base:
                continue
            seg = hb_seg(base)
            kb = (n + 1023) // 1024
            cls = CLS.get(own, 0)
            recs.append((seg, kb, (cls << 8) | (XMW_HMAI if own == seg
                                                 else XMW_HMAB)))
    for off, kb, own in XTAB:
        if kb:
            recs.append((off, kb, own))
    return recs[:room]


def run(img, syms, d, at, htab, door, room, bx, door_on):
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 0x200000)
    mu.mem_write(XSEG << 4, img)
    xt = b"".join(struct.pack("<HHBBH", o, k, w, 0, 0) for o, k, w in XTAB)
    xt = xt.ljust(syms["xm_tab_end"] - syms["xm_tab"], b"\0")    # the rest
                                        # free: attach zeroes the scratch it
                                        # lays over its own spent code
    mu.mem_write((XSEG << 4) + syms["xm_tab"], xt)
    hdoor = door if door_on else 0
    mu.mem_write((XSEG << 4) + syms["xm_hdoor"], struct.pack("<H", hdoor))
    hma = 0xFFFF0
    mu.mem_write(hma + at, d)                       # the tail, whole
    mu.mem_write(hma + htab, b"".join(struct.pack("<HHH", *r) for r in POOL))
    for seg, cls in CLS.items():                    # each image's header
        mu.mem_write((seg << 4) + HB_ORG, bytes([ord("O"), ord("8"), 4, cls]))
    guard = bytes(range(0x40, 0x40 + 16))
    mu.mem_write((RSEG << 4) + ROOM - 16, guard)
    mu.mem_write((RSEG << 4) + ROOM, b"\xA5" * (6 * 16))
    # a far call into XMEM.DRV's image, as the kernel's dispatch makes: CS =
    # its segment, so the stub sits IN that segment's reach - put it there
    stub_lin = (XSEG << 4) + len(img) + 0x10
    rel = (syms["xm_walk"] - (len(img) + 0x10 + 3)) & 0xFFFF
    mu.mem_write(stub_lin, struct.pack("<BH", 0xE8, rel) + b"\xF4")
    regs = {UC_X86_REG_AX: 0x1111, UC_X86_REG_BX: bx, UC_X86_REG_CX: room,
            UC_X86_REG_DX: RSEG, UC_X86_REG_SI: 0x2222,
            UC_X86_REG_DI: ROOM, UC_X86_REG_BP: 0x3333,
            UC_X86_REG_DS: XSEG, UC_X86_REG_ES: 0x6000,
            UC_X86_REG_SS: SSEG, UC_X86_REG_SP: 0xFFF0,
            UC_X86_REG_CS: XSEG}
    for r, v in regs.items():
        mu.reg_write(r, v)
    mu.reg_write(UC_X86_REG_EFLAGS, 0x0202)         # IF on, DF clear
    start = len(img) + 0x10
    mu.emu_start(stub_lin, stub_lin + 3, count=200000)
    if mu.reg_read(UC_X86_REG_IP) != start + 3:
        return None, ["did not return (IP %04X)" % mu.reg_read(UC_X86_REG_IP)]
    errs = []
    for r, v in regs.items():
        if r != UC_X86_REG_AX and mu.reg_read(r) != v:
            errs.append("register %d %04X, want %04X" % (r, mu.reg_read(r), v))
    fl = mu.reg_read(UC_X86_REG_EFLAGS)
    if fl & 1:
        errs.append("CF set")
    if fl & 0x600 != 0x200:
        errs.append("IF/DF %03X, want 200" % (fl & 0x600))
    n = mu.reg_read(UC_X86_REG_AX)
    raw = bytes(mu.mem_read((RSEG << 4) + ROOM, 6 * 16))
    if bytes(mu.mem_read((RSEG << 4) + ROOM - 16, 16)) != guard:
        errs.append("the bytes below the room were written")
    if n > 16 or raw[6 * n:] != b"\xA5" * (6 * (16 - n)):
        errs.append("bytes past record %d were written" % n)
    recs = [struct.unpack_from("<HHH", raw, 6 * i) for i in range(min(n, 16))]
    return recs, errs


def main():
    d, at, htab, hbhi, door = tail()
    if not door:
        sys.exit("xmhwalk: build/coldhma.sys has no door at rom_id +28")
    with tempfile.TemporaryDirectory() as tmp:
        img, syms = xmem_syms(tmp)
    print("xmhwalk: XMEM.DRV %d bytes, the tail at FFFF:%04X, its door "
          "%04X, `.cold` from %04X" % (len(img), at, door, hbhi))
    full = len(model(hbhi, 99, True, True))
    cases = [("BX = XMW_FIXED, room for all", 16, XMW_FIXED, True),
             ("room for none", 0, XMW_FIXED, True),
             ("room for `.cold`'s record alone", 1, XMW_FIXED, True),
             ("room for part of the pool", 4, XMW_FIXED, True),
             ("room ending in XMEM.DRV's own blocks", full - 1, XMW_FIXED,
              True),
             ("BX not asking: the extended pool alone", 16, 0, True),
             ("no tail installed (the door's offset 0)", 16, XMW_FIXED,
              False)]
    for what, room, bx, on in cases:
        want = model(hbhi, room, bx == XMW_FIXED, on)
        got, errs = run(img, syms, d, at, htab, door, room, bx, on)
        ok = got == want and not errs
        check(ok, "%s: %d records%s" % (
            what, len(want), "" if ok else " - got %r want %r; %s" % (
                got, want, "; ".join(errs))))
    if FAILS:
        print("xmhwalk: %d FAILED" % len(FAILS))
        return 1
    print("xmhwalk: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
