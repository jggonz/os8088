#!/usr/bin/env python3
"""Which 8x8 table does the kernel read, and does the copy draw the same?

    python3 tests/fontpick.py [--machine NAME] [--no-build]

SPEC.md 6.0.1's two verdicts, off the guest's own memory and the glass:

  A. THE SHIPPED KERNEL. Every BIOS this tree boots carries the IBM set at
     F000:FA6E, and every VGA BIOS in reach answers int 10h AX=1130h with the
     same glyphs - so the kernel must read the PLANAR table in place:
     [font_seg]:[font_base] = F000:FB6E, and no MEM_K_FONT claim anywhere in
     mem_tab. A kernel that takes the BIOS's answer without the planar test
     reads F000:FB6E here too on a CGA (the answer IS the planar table there),
     which is why the default machine is the VGA XT: there the answer is the
     option ROM at C000 and only the test brings it to F000.
     And the kernel's OWN measurement, which font_init leaves at 0040:00F8
     behind 'FP' (chosen table, RAM, the BIOS's answer, in PIT counts):
     the chosen/RAM ratio must be within the quarter that keeps the table in
     place - MartyPC prices every ROM read as a RAM read, so it reads ~1000.
  B. `make FONTSLOW=1`, which forces the copy verdict on any machine. The
     kernel must have ONE 1KB MEM_K_FONT claim, pinned at the arena's CEILING
     (its last paragraph is [mem_top] - 1, the top-down placement that makes
     it no barrier), the pointer must be that claim at offset 0, its 760 bytes
     must equal the ROM's, and the desktop with a Disk window open must be
     the SAME PICTURE as arm A's - pixel for pixel, the menu bar's clock
     masked, since the two boots are not the same instant.

Broken on purpose and watched go red (both at once): FONT_PICK's `jne` made
an unconditional `jmp` (the BIOS's answer always kept) put arm A on a
9FC0:0008 heap copy with three failures, and the copy's `xor bp, bp` made
`mov bp, 8` failed arm B's pointer and glyph checks. The pixel compare is
the RENDERER half - a copy that holds the right bytes and draws them
differently - and is only as independent as arm A is green.
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build                                          # noqa: E402
import os88marty                                          # noqa: E402
import os88sym                                            # noqa: E402
import os88ui                                             # noqa: E402

MEM_K_FONT = 0xFF10
MC_SIZE, MC_SEG, MC_PARA, MC_OWN = 10, 0, 2, 4
PLANAR = (0xF000, 0xFA6E + 32 * 8)
FONT_BYTES = 95 * 8
CLOCK_H = 16                    # the menu bar, whose clock differs by boot


def u16(b, o=0):
    return struct.unpack_from("<H", b, o)[0]


def leg(tree, machine, say):
    tree.apply()
    defs = tree.defines
    S = lambda n: os88sym.linear(n, defines=defs)          # noqa: E731
    with os88ui.boot(tree.img("os8088-360.img"),
                     apps=tree.img("apps360.img"),
                     machine=machine, verbose=False) as ui:
        m = ui.m
        seg = u16(m.read(S("font_seg"), 2))
        off = u16(m.read(S("font_base"), 2))
        top = u16(m.read(S("mem_top"), 2))
        mmax = 16 if "KERN_SMALL" in defs else 32
        tab = m.read(S("mem_tab"), mmax * MC_SIZE)
        claims = []
        for i in range(mmax):
            r = tab[i * MC_SIZE:(i + 1) * MC_SIZE]
            if u16(r, MC_SEG) and u16(r, MC_OWN) == MEM_K_FONT:
                claims.append((u16(r, MC_SEG), u16(r, MC_PARA)))
        glyphs = bytes(m.read(seg * 16 + off, FONT_BYTES))
        ica = bytes(m.read(0x4F8, 8))
        rom = bytes(m.read(PLANAR[0] * 16 + PLANAR[1], FONT_BYTES))
        ui.open_drive("A")
        ui.settle()
        v = m.video()
        if v["type"] == "vga":
            w, h, data = m.fbuf(None)
            rows = [bytes(data[y * w * 3:(y + 1) * w * 3]) for y in range(h)]
        else:
            w, h, rows = m.vram(None)
            rows = [bytes(r) for r in rows]
    say("font %04X:%04X, mem_top %04X, MEM_K_FONT claims %s"
        % (seg, off, top, ["%04X+%d" % c for c in claims] or "none"))
    if ica[:2] == b"FP":
        best, ram, bios = struct.unpack_from("<HHH", ica, 2)
        ratio = 1000.0 * best / ram if ram else 0.0
        say("the kernel's own clock (0040:00F8): chosen %d, RAM %d, BIOS "
            "answer %d PIT counts - chosen/RAM x1000 = %.0f"
            % (best, ram, bios, ratio))
    else:
        ratio = None
        say("no 'FP' record at 0040:00F8")
    return seg, off, top, claims, glyphs, rom, rows, ratio


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--no-build", action="store_true",
                    help="arm A only (no knob tree)")
    a = ap.parse_args(argv)
    fail = []

    def say(s):
        print("fontpick: " + s)

    print("=== A: the shipped kernel on %s ===" % a.machine)
    seg, off, top, claims, glyphs, rom, pa, ratio = leg(
        os88build.plain(), a.machine, say)
    if ratio is None:
        fail.append("A: font_init left no timing record at 0040:00F8")
    elif not 900 <= ratio < 1250:
        fail.append("A: the kernel measured the chosen table at %.0f x1000 "
                    "of RAM - MartyPC prices a ROM read as a RAM read, so "
                    "this is 1000 or the clock is wrong" % ratio)
    if (seg, off) != PLANAR:
        fail.append("A: the kernel reads %04X:%04X, not the planar table "
                    "F000:FB6E whose glyphs every BIOS here carries "
                    "(SPEC.md 6.0.1 test 1)" % (seg, off))
    if claims:
        fail.append("A: a MEM_K_FONT claim on a machine with the planar set "
                    "- 1KB of heap for nothing")
    if glyphs != rom:
        fail.append("A: the table the kernel reads is not the planar ROM's")

    if not a.no_build:
        print("\n=== B: FONTSLOW=1, the copy verdict forced ===")
        knob = os88build.tree("FONTSLOW=1")
        seg, off, top, claims, glyphs, rom, pb, ratio = leg(knob, a.machine,
                                                            say)
        if ratio is None:
            fail.append("B: no timing record at 0040:00F8")
        if len(claims) != 1:
            fail.append("B: %d MEM_K_FONT claims, want exactly one"
                        % len(claims))
        else:
            cseg, cpara = claims[0]
            if cpara != 64:
                fail.append("B: the claim is %d paragraphs, want 64 (1KB)"
                            % cpara)
            if cseg + cpara != top:
                fail.append("B: the claim ends at %04X and the arena at %04X "
                            "- not at the CEILING, so it can stand in the "
                            "middle of the heap" % (cseg + cpara, top))
            if (seg, off) != (cseg, 0):
                fail.append("B: the pointer is %04X:%04X, not the copy "
                            "%04X:0000" % (seg, off, cseg))
        if glyphs != rom:
            fail.append("B: the copy's 760 bytes differ from the ROM's")
        diff = sum(1 for y, (ra, rb) in enumerate(zip(pa, pb))
                   if y >= CLOCK_H for x, z in zip(ra, rb) if x != z)
        if len(pa) != len(pb) or diff:
            fail.append("B: the desktop differs from arm A's by %d byte(s) "
                        "below the menu bar - the copy draws a different "
                        "face" % diff)
        else:
            say("B draws the same desktop as A (0 differing bytes below the "
                "menu bar)")
        os88build.plain().apply()

    for f in fail:
        print("fontpick: FAIL: " + f)
    if fail:
        return 1
    print("fontpick: the planar table in place on the shipped kernel, a "
          "ceiling claim that draws the same under FONTSLOW=1 - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
