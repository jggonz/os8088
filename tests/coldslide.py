#!/usr/bin/env python3
"""coldslide - a machine with no VGA and no EGA gives the VGA rung back.

docs/plans/MOVABLE-COLD-PLAN.md 2, SPEC.md 2.10.9. kern_big's code that only a
VGA or an EGA runs is the `.vga` rung, between LOW and `.cold`. On a machine
whose [vid_avail] has neither card, kmain_o slides `.cold` down over the rung
at boot and the heap floor falls by it. Two boots of the shipped 360KB disks:

  A. a Hercules 5150 (GLaBIOS): `.cold` SLID -
     * [api_coldseg] = COLD_HOME - VGA_PARA;
     * the bytes at the new home are kernel.bin's `.cold`, every one - the
       copy is the whole of the move, so a short or misaligned one is a
       kernel that runs garbage;
     * every `.text` word the kernel's own list (`.cfx`) holds names the new
       segment - read out of the guest, against the two-assembly diff
       tools/os88rom.py takes, not against the list itself;
     * [mem_base] = MONO_FLOOR, and the Task Manager's kernel row
       ([vga_skkern]) has both rungs off it;
     * the Control Panel opens, and CTRL.DRV - loaded AFTER the slide - has
       every far reference re-pointed by mod_cfix (read, not inferred).
  B. the VGA XT (GLaBIOS XT): NOTHING moves - [api_coldseg] = COLD_HOME and
     [mem_base] = HEAP_SEG. The negative control (docs/WRITING-TESTS.md 1): a
     slide that fired on a VGA machine would run the planar renderer out of
     `.cold`'s bytes.

    make && python3 tests/coldslide.py
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88layout  # noqa: E402
import os88rom     # noqa: E402
import os88sym     # noqa: E402
import os88ui      # noqa: E402
import dispcp      # noqa: E402

SYS = os.path.join(ROOT, "build", "os8088-360.img")
APPS = os.path.join(ROOT, "build", "apps360.img")
HERC = "os8088_5150_herc_gla"
VGA = "os8088_xt_vga"
EQ = os88sym.equates(())


def S(n):
    return os88sym.linear(n)


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def coldimage():
    """`.cold` as the build laid it out - os88layout's span, the one place
    that knows the file's shape (tests/dispcold.py's reason)."""
    _seg, off, length = os88layout.cold_span(ROOT)
    with open(os.path.join(ROOT, "build", "kernel.bin"), "rb") as f:
        return f.read()[off:off + length]


def main():
    fails = []

    def want(cond, what):
        print("   %s  %s" % ("ok  " if cond else "FAIL", what))
        if not cond:
            fails.append(what)

    if "VGA_PARA" not in EQ:
        sys.exit("coldslide: this kernel has no VGA rung (COLD_SLIDE) - "
                 "nothing to test")
    home, vpara = EQ["COLD_HOME"], EQ["VGA_PARA"]
    slid = home - vpara
    vst = EQ["COLD_VSTART"]
    t = os88rom.kernel_tables(os.path.join(ROOT, "build"), "KERN_BIG")
    tsites = t["lists"]["text"]
    csites = t["lists"]["mods"].get(EQ["MOD_CTRL"], [])
    cold = coldimage()
    kbase = EQ["KERNEL_SEG"] << 4
    want(vpara > 0 and tsites and csites,
         "the rung is %d paragraphs; %d `.text` words and %d in CTRL.DRV name "
         "`.cold`" % (vpara, len(tsites), len(csites)))

    print("A: Hercules, no VGA and no EGA")
    with os88ui.boot(SYS, apps=APPS, machine=HERC, label="coldslide") as ui:
        ui.ready()
        m = ui.m
        cs = u16(m.read(S("api_coldseg"), 2))
        want(cs == slid, "[api_coldseg] = %04X: slid by VGA_PARA (%04X)"
             % (cs, slid))
        got = m.read((slid << 4) + vst, len(cold))
        bad = sum(1 for x, y in zip(got, cold) if x != y)
        want(bad == 0, "the %d bytes at %04X:%04X are kernel.bin's `.cold` "
             "(%d differ)" % (len(cold), slid, vst, bad))
        words = [u16(m.read(kbase + o, 2)) for o in tsites]
        want(all(w == slid for w in words),
             "all %d listed `.text` words say %04X (%d do not)"
             % (len(words), slid, sum(1 for w in words if w != slid)))
        base = u16(m.read(S("mem_base"), 2))
        want(base == EQ["MONO_FLOOR"], "[mem_base] = %04X (MONO_FLOOR %04X, "
             "HEAP_SEG %04X)" % (base, EQ["MONO_FLOOR"], EQ["HEAP_SEG"]))
        sk = m.read(S("vga_skkern"), 1)[0]
        want(sk == EQ["KERN_KB"] - EQ["SK_MONO_KB"],
             "the kernel row reports %d KB (KERN_KB %d less both rungs, %d)"
             % (sk, EQ["KERN_KB"], EQ["SK_MONO_KB"]))
        dispcp.open_panel(m, ui.mo, ui.sym, ui.settle, page=None)
        seg = u16(m.read(S("mod_tab") + EQ["MOD_CTRL"] * EQ["MODR_SIZE"], 2))
        want(seg != 0, "the Control Panel opened, CTRL.DRV at %04X" % seg)
        if seg:
            cw = [u16(m.readseg(seg, o, 2)) for o in csites]
            want(all(w == slid for w in cw),
                 "CTRL.DRV: all %d of its far references to `.cold` say %04X "
                 "(%d do not) - mod_cfix" % (len(cw), slid,
                                             sum(1 for w in cw if w != slid)))

    print("B: VGA - nothing moves")
    with os88ui.boot(SYS, apps=APPS, machine=VGA, label="coldslide") as ui:
        ui.ready()
        m = ui.m
        cs = u16(m.read(S("api_coldseg"), 2))
        want(cs == home, "[api_coldseg] = %04X (COLD_HOME)" % cs)
        base = u16(m.read(S("mem_base"), 2))
        want(base == EQ["HEAP_SEG"], "[mem_base] = %04X (HEAP_SEG)" % base)

    if fails:
        sys.exit("coldslide: %d failed" % len(fails))
    print("coldslide: all pass")


if __name__ == "__main__":
    main()
