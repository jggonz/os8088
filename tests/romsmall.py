#!/usr/bin/env python3
"""romsmall - kern_small finds its `.cold` in ROM, uses it, and lets it go.

docs/plans/ROM-PLAN.md wave 2 (3.4): a ROM_COLD kernel boots the same off any
disk with or without the 5150's U28-U32 ROM. With the ROM it is ADOPTED - the
ROM's own adapter checks the expanded kernel is the one it was cut from and
re-points every far reference to `.cold` at F400 - and the heap floor drops by
the cold rung. Without it, or with a ROM from a different kernel, nothing
changes. Four boots of `make small`'s disks under MartyPC's GLaBIOS 5150 (the
BIOS 5150 #2, the ROM testbed, runs - docs/FIELD-MACHINES.md):

  A. 640KB, the ROM `tools/os88rom.py kernel` cuts from build/smallk:
     [api_coldseg] = F400, [mem_base] = COLD_RAM (the floor fell by the whole
     rung), rom_mfp = F400:x, and the Control Panel opens - CTRL.DRV is a
     module, loaded off the disk AFTER boot, so it far-calls `.cold` at
     COLD_RAM until the ROM's rom_modfix re-points it. Opening is not proof on
     its own (the dead RAM copy of `.cold` is still there to be called by
     mistake), so every one of the module's listed words is READ and must say
     F400.
  B. 640KB, no ROM: [api_coldseg] = COLD_HOME, [mem_base] = HEAP_SEG, the
     panel opens - the kernel a machine without the ROM always ran.
  C. 640KB, a ROM whose `.cold` differs from the kernel's by ONE byte (sums
     re-balanced so the BIOS still accepts it): refused, exactly B. The
     negative control - a ROM that is not this kernel's must be no ROM
     (docs/WRITING-TESTS.md 1).
  D. the 128KB floor machine with the ROM: a desktop, and [mem_base] =
     COLD_RAM - the cold rung's worth more heap than tests/small128.py's
     machine has (23.0KB at build 387; the test prints it).

    make small && python3 tests/romsmall.py
"""

import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "smallk")
import os88marty   # noqa: E402
import os88mouse   # noqa: E402
import os88rom     # noqa: E402
import os88sym     # noqa: E402
import dispcp      # noqa: E402

DEFS = ("KERN_SMALL",)
os88sym.default_defines(*DEFS)
SYS = os.path.join(ROOT, "build", "small360.img")
APPS = os.path.join(ROOT, "build", "apps360.img")
M640 = "os8088_5150_cga_gla"
M128 = "os8088_5150_cga_128k"


EQ = os88sym.equates(DEFS)   # MOD_CTRL, MODR_SIZE: mod.inc's


def S(n):
    return os88sym.linear(n, DEFS)


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def boot(rom, machine=M640, panel=True, sites=None):
    out = {}
    with os88marty.launch(SYS, apps=APPS, machine=machine, boot=False,
                          rom=rom, label="romsmall") as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        os88marty.no_saver(m)
        out["cs"] = u16(m.read(S("api_coldseg"), 2))
        out["base"] = u16(m.read(S("mem_base"), 2))
        mfp = m.read(S("rom_mfp"), 4)
        out["mfp"] = (u16(mfp, 2), u16(mfp))
        if panel:
            mo = os88mouse.Mouse(marty=m)
            dispcp.open_panel(m, mo, S, os88marty.settle, page=None)
            out["panel"] = dispcp._cp_win(m, S) is not None
            seg = u16(m.read(S("mod_tab")
                         + EQ["MOD_CTRL"] * EQ["MODR_SIZE"], 2))
            out["ctrlseg"] = seg
            if seg and sites:
                out["ctrlsites"] = [u16(m.readseg(seg, o, 2)) for o in sites]
    return out


def main():
    eq = os88sym.equates(DEFS)
    cold_ram, heap = eq["COLD_RAM"], eq["HEAP_SEG"]
    fails = []

    def want(cond, what):
        print("   %s  %s" % ("ok  " if cond else "FAIL", what))
        if not cond:
            fails.append(what)

    with tempfile.TemporaryDirectory(prefix="romsmall-") as tmp:
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                        "os88rom.py"), "kernel", "--build",
                        os.path.join(ROOT, "build", "smallk"), "--small",
                        "--pkg", os.path.join(ROOT, "build", "smallapp",
                                              "taskmgr.o88"),
                        "--out", tmp], check=True)
        good = os.path.join(tmp, "osrom-small.bin")
        _img, t = os88rom.build_kernel(os.path.join(ROOT, "build", "smallk"),
                                       "KERN_SMALL")
        sites = t["lists"]["mods"].get(EQ["MOD_CTRL"], [])

        img = bytearray(open(good, "rb").read())
        img[os88rom.HDR_SIZE + 0x1000] ^= 0x01      # one byte of `.cold`
        img = os88rom.balance(img)
        bad = os.path.join(tmp, "osrom-bad.bin")
        with open(bad, "wb") as f:
            f.write(img)

        print("A: the ROM, 640KB")
        a = boot(good, sites=sites)
        want(a["cs"] == os88rom.ROM_BASE // 16,
             "[api_coldseg] = %04X, `.cold` runs in ROM" % a["cs"])
        want(a["base"] == cold_ram,
             "[mem_base] = %04X: the floor fell onto the cold rung (%04X)"
             % (a["base"], cold_ram))
        want(a["mfp"][0] == os88rom.ROM_BASE // 16,
             "rom_mfp = %04X:%04X, the ROM's module door" % a["mfp"])
        want(a.get("panel"), "the Control Panel opened")
        cs = a.get("ctrlsites", [])
        want(cs and all(w == os88rom.ROM_BASE // 16 for w in cs),
             "CTRL.DRV at %04X: all %d of its far references to `.cold` say "
             "F400" % (a.get("ctrlseg", 0), len(cs)))

        print("B: no ROM, 640KB")
        b = boot(False)         # False: $OS88_ROM must not reach it
        want(b["cs"] == eq["COLD_HOME"], "[api_coldseg] = %04X (COLD_HOME)" % b["cs"])
        want(b["base"] == heap, "[mem_base] = %04X (HEAP_SEG)" % b["base"])
        want(b.get("panel"), "the Control Panel opened")

        print("C: a ROM one byte away from this kernel's, 640KB")
        c = boot(bad)
        want(c["cs"] == eq["COLD_HOME"], "[api_coldseg] = %04X - refused, as if no "
             "ROM" % c["cs"])
        want(c["base"] == heap, "[mem_base] = %04X (HEAP_SEG)" % c["base"])
        want(c.get("panel"), "the Control Panel opened")

        print("D: the ROM on the 128KB floor machine")
        d = boot(good, machine=M128, panel=False)
        want(d["cs"] == os88rom.ROM_BASE // 16 and d["base"] == cold_ram,
             "a desktop with `.cold` in ROM and the floor at %04X: %.1f KB of "
             "heap against %.1f without" % (d["base"],
                                           (128 * 1024 - d["base"] * 16) / 1024,
                                           (128 * 1024 - heap * 16) / 1024))

    if fails:
        sys.exit("romsmall: %d failed" % len(fails))
    print("romsmall: ok")


if __name__ == "__main__":
    main()
