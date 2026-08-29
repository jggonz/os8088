#!/usr/bin/env python3
"""SPEC.md 39.22: the heap floor starts UNDER .vgabuf on a machine with no VGA.

Reads [mem_base] out of a booted machine on a VGA adapter and on both mono
ones and asserts they differ by exactly VGABUF_PARA. Reading the WORD and not
a KB figure is the point: a KB total rounds, and rounding is where an
off-by-a-rung hides.

It is also the only thing that would catch the gate being written on
[vid_mono] instead of [vid_avail], which reads identically on every machine
here and parts company the moment somebody switches a VGA machine to mono
(SPEC.md 39.11) - so [vid_avail] is printed beside every reading.
"""
import os, sys
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(HERE, "tools"))
import os88marty, os88sym

S, E = os88sym.linear, os88sym.equates()
want = E["VGABUF_PARA"]
kend, low = E["KERN_END"], E["LOW_SEG"]
print(f"  VGABUF_PARA={want}  KERN_END={kend:#06x}  LOW_SEG={low:#06x}")
if not want:
    print("  vgadrop: SKIP - this kernel has no GFX_PLANE, so no rung")
    sys.exit(0)

got = {}
for mach, name in (("os8088_xt_vga", "vga"),
                   ("os8088_5150_cga_gla", "cga"),
                   ("os8088_5150_herc_gla", "herc")):
    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine=mach) as m:
        os88marty.settle(m)
        base = int.from_bytes(m.read(S("mem_base"), 2), "little")
        avail = m.read(S("vid_avail"), 1)[0]
        got[name] = base
        print(f"  {name:5} mem_base={base:#06x}  vid_avail={avail:#04x}  "
              f"heap floor {'AT' if base == kend else 'UNDER'} KERN_END",
              flush=True)

ok = True
if got["vga"] != kend:
    print(f"  FAIL: a VGA machine must reserve .vgabuf - mem_base "
          f"{got['vga']:#06x} != KERN_END {kend:#06x}")
    ok = False
for n in ("cga", "herc"):
    if got[n] != kend - want:
        print(f"  FAIL: {n} must start UNDER it - {got[n]:#06x} != "
              f"{kend - want:#06x}")
        ok = False
    elif got[n] % 32:
        # guard 2d proves this at assembly time; proving it again on the
        # BOOTED machine is what says the constant and the subtraction agree
        print(f"  FAIL: {n}'s floor {got[n]:#06x} is not 512-aligned")
        ok = False
print("  vgadrop: PASS" if ok else "  vgadrop: FAIL")
sys.exit(0 if ok else 1)
