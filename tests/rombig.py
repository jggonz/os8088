#!/usr/bin/env python3
"""rombig - kern_big finds its `.cold` in ROM, and the VGA decoder moves down.

docs/plans/ROM-PLAN.md wave 3 (3.4, 3.6): the shipped kernel boots the same
off any disk with or without the 5150's U28-U32 ROM, as kern_small has since
wave 2 (tests/romsmall.py). kern_big adds two things kern_small has not got:

  * SPEC.md 5.4.1.3's `.vgabuf` rung, which sits ABOVE the cold rung - so a
    floor that dropped by the cold rung alone would land under live buffers.
    The ROM re-points the decoder's ONE segment load (vga12.inc,
    `mov ax, VGABUF_SEG`) at the bottom of the dead cold rung, and the floor
    falls to COLD_RAM + VGABUF_PARA: the whole rung back, buffers kept;
  * hibernate, whose image would carry every far reference into the ROM.
    The pointer file's pad byte says where `.cold` ran (SPEC.md 2.10.4), and
    resume compares it as part of the head it always compared, so a ROM image
    and a RAM image refuse each other. Not driven here: hibernate is its own
    rows' subject, and this byte rides the comparison they already make.

Five boots of the shipped 360KB disks:

  A. CGA 5150 (GLaBIOS), the ROM `tools/os88rom.py kernel` cuts from build/:
     [api_coldseg] = RCS - F400 less (COLD_VSTART - 0x10)/16, EE01 since
     kern_big's `.cold` starts at HMA_COLD_AT (SPEC.md 2.10.2) - [mem_base] =
     COLD_RAM (no VGA: the `.vgabuf` arm has already taken its rung, and the
     cold rung goes too), rom_mfp = F400:x (the tail's own segment), the
     Control Panel opens and CTRL.DRV's listed words say RCS.
  B. CGA, no ROM: [api_coldseg] = COLD_HOME, [mem_base] = HEAP_SEG - VGABUF_PARA.
  C. CGA, a ROM whose `.cold` is one byte off this kernel's: refused, exactly B
     - the negative control (docs/WRITING-TESTS.md 1).
  D. the VGA XT (GLaBIOS XT), with the ROM: RCS, [mem_base] = COLD_RAM +
     VGABUF_PARA, the decoder's segment word reads COLD_RAM - and the decoder
     is DRIVEN: Paint repaints a dithered picture off the byte grid
     (tests/blitplane.py's scene), a breakpoint on vga_blit_prow's
     `mov ds, ax` must see AX = COLD_RAM on every row it decodes, and the
     screen after it must be pixel-identical to E's.
  E. the VGA XT, no ROM: the same session, the reference frame, and AX =
     VGABUF_RAM at the same breakpoint.

    make && python3 tests/rombig.py
"""

import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88marty   # noqa: E402
import os88mouse   # noqa: E402
import os88ui      # noqa: E402
import dispapps    # noqa: E402
import os88rom     # noqa: E402
import os88sym     # noqa: E402
import dispcp      # noqa: E402

SYS = os.path.join(ROOT, "build", "os8088-360.img")
APPS = os.path.join(ROOT, "build", "apps360.img")
CGA = "os8088_5150_cga_gla"
VGA = "os8088_xt_vga"
F400 = os88rom.ROM_BASE // 16   # the ROM's header paragraph, where the tail
                                # and so rom_mfp run
PARK = (4, 24)                  # under the menu bar, outside both windows


EQ = os88sym.equates(())   # MOD_CTRL, MODR_SIZE: mod.inc's
# ...and the segment `.cold` runs at in the window: its first OFFSET,
# COLD_VSTART, on F4010 - EE01 for kern_big, whose `.cold` starts at the
# top of the HMA's layout (SPEC.md 2.10.2, 2.10.8)
RCS = F400 - (EQ["COLD_VSTART"] - 0x10) // 16


def S(n):
    return os88sym.linear(n)


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def base(m):
    mfp = m.read(S("rom_mfp"), 4)
    return {"cs": u16(m.read(S("api_coldseg"), 2)),
            "base": u16(m.read(S("mem_base"), 2)),
            "mfp": (u16(mfp, 2), u16(mfp))}


def boot_cga(rom, sites=None):
    with os88marty.launch(SYS, apps=APPS, machine=CGA, boot=False,
                          rom=rom, label="rombig") as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        os88marty.no_saver(m)
        out = base(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle, page=None)
        out["panel"] = dispcp._cp_win(m, S) is not None
        seg = u16(m.read(S("mod_tab")
                         + EQ["MOD_CTRL"] * EQ["MODR_SIZE"], 2))
        out["ctrlseg"] = seg
        if seg and sites:
            out["ctrlsites"] = [u16(m.readseg(seg, o, 2)) for o in sites]
        return out


def boot_vga(rom, site, apps):
    """`site` is the decoder's segment word, a `.text` offset; `apps` the
    scratch disk with Paint and the colour GIF.

    THE DECODER HAS TO BE DRIVEN, and nothing on a desktop drives it: it takes
    rows at least 64 pixels wide with more than W/32 runs in them (SPEC.md
    5.4.1.3), which a Disk window and Solitaire's cards both measured at zero.
    tests/blitplane.py's scene does - Paint, a dithered picture, and the window
    nudged one pixel off the byte grid so gfx_blitp refuses and the canvas
    repaints through gfx_blit4."""
    kbase = os88sym.KERNEL_SEG << 4
    with os88ui.boot(SYS, apps=apps, machine=VGA, rom=rom,
                     label="rombig") as ui:
        m = ui.m
        out = base(m)
        out["word"] = u16(m.read(kbase + site, 2))
        pw = ui.path("B:/MEDIA/" + os.path.basename(dispapps.colour_gif()))
        os88marty.quiesce(m, lambda: m.disk().get("reads"), guest=1.0,
                          stable=3, what="the picture to load")
        os88marty.settle(m)
        seen = []

        def hit(mm, rec):
            seen.append(rec["regs"]["ax"])
            return None

        # the instruction AFTER the load: `mov ax, imm16` is three bytes and
        # `site` is its immediate, so AX here is the segment the decoder uses
        with os88marty.bp_trace(m, kbase + site + 2, regs=True, on_hit=hit):
            m.pause()
            m.write(S("wm_wins") + pw.i * dispcp.WIN_SIZE + 2,
                    (pw.x - 1).to_bytes(2, "little"))
            m.run()
            other = [w for w in ui.windows() if w.i != pw.i][0]
            ui.raise_window(other)
            ui.raise_window(pw)
            ui.mo.to(*PARK)
            os88marty.settle(m)
        out["ax"] = seen
        # PAINT'S WINDOW AND NOTHING ELSE: the decoder's pixels are in it, and
        # the menu bar's clock is not - two boots a minute apart differ there
        # by a digit (31 pixels, once, under a loaded soak)
        w, h, rgb = m.fbuf(card=0)
        x0, y0 = max(pw.x - 1, 0), pw.y
        x1, y1 = min(pw.x - 1 + pw.w, w), min(pw.y + pw.h, h)
        out["frame"] = b"".join(rgb[(y * w + x0) * 3:(y * w + x1) * 3]
                                for y in range(y0, y1))
        return out


def main():
    eq = os88sym.equates(())
    cold_ram, heap = eq["COLD_RAM"], eq["HEAP_SEG"]
    vgap = eq["VGABUF_PARA"]
    vgab_ram = eq["VGABUF_RAM"]
    fails = []

    def want(cond, what):
        print("   %s  %s" % ("ok  " if cond else "FAIL", what))
        if not cond:
            fails.append(what)

    with tempfile.TemporaryDirectory(prefix="rombig-") as tmp:
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                        "os88rom.py"), "kernel", "--build",
                        os.path.join(ROOT, "build"), "--out", tmp], check=True)
        good = os.path.join(tmp, "osrom-big.bin")
        _img, t = os88rom.build_kernel(os.path.join(ROOT, "build"), "KERN_BIG")
        sites = t["lists"]["mods"].get(EQ["MOD_CTRL"], [])
        vg = t["lists"]["vgab"]
        want(len(vg) == 1, "the ROM carries the decoder's one segment word "
             "(%d listed)" % len(vg))

        img = bytearray(open(good, "rb").read())
        img[os88rom.HDR_SIZE + 0x1000] ^= 0x01      # one byte of `.cold`
        img = os88rom.balance(img)
        bad = os.path.join(tmp, "osrom-bad.bin")
        with open(bad, "wb") as f:
            f.write(img)

        print("A: the ROM, CGA")
        a = boot_cga(good, sites=sites)
        want(a["cs"] == RCS, "[api_coldseg] = %04X, `.cold` runs in ROM (%04X)"
             % (a["cs"], RCS))
        want(a["base"] == cold_ram, "[mem_base] = %04X: past BOTH rungs, onto "
             "COLD_RAM (%04X)" % (a["base"], cold_ram))
        want(a["mfp"][0] == F400, "rom_mfp = %04X:%04X" % a["mfp"])
        want(a.get("panel"), "the Control Panel opened")
        cs = a.get("ctrlsites", [])
        want(cs and all(w == RCS for w in cs),
             "CTRL.DRV at %04X: all %d of its far references say %04X"
             % (a.get("ctrlseg", 0), len(cs), RCS))

        print("B: no ROM, CGA")
        b = boot_cga(False)     # False: $OS88_ROM must not reach it
        want(b["cs"] == eq["COLD_HOME"], "[api_coldseg] = %04X (COLD_HOME)" % b["cs"])
        want(b["base"] == heap - vgap, "[mem_base] = %04X (HEAP_SEG less the "
             "`.vgabuf` rung)" % b["base"])
        want(b.get("panel"), "the Control Panel opened")

        print("C: a ROM one byte away from this kernel's, CGA")
        c = boot_cga(bad)
        want(c["cs"] == eq["COLD_HOME"], "[api_coldseg] = %04X - refused" % c["cs"])
        want(c["base"] == heap - vgap, "[mem_base] = %04X" % c["base"])
        want(c.get("panel"), "the Control Panel opened")

        if vg:
            apps = os.path.join(tmp, "rombig-apps.img")
            os88marty.scratch_disk(apps, "APPS:build/paint.o88",
                                   "MEDIA:" + dispapps.colour_gif())
            print("D: the ROM, VGA")
            d = boot_vga(good, vg[0], apps)
            want(d["cs"] == RCS, "[api_coldseg] = %04X" % d["cs"])
            want(d["base"] == cold_ram + vgap, "[mem_base] = %04X: the cold "
                 "rung back, the buffers kept under it (%04X)"
                 % (d["base"], cold_ram + vgap))
            want(d["word"] == cold_ram, "the decoder's segment word = %04X "
                 "(COLD_RAM)" % d["word"])
            want(d["ax"] and set(d["ax"]) == {cold_ram},
                 "vga_blit_prow ran %d time(s), every one on %s"
                 % (len(d["ax"]), sorted("%04X" % x for x in set(d["ax"]))))

            print("E: no ROM, VGA")
            e = boot_vga(False, vg[0], apps)
            want(e["base"] == heap, "[mem_base] = %04X (HEAP_SEG)" % e["base"])
            want(e["ax"] and set(e["ax"]) == {vgab_ram},
                 "vga_blit_prow ran %d time(s), every one on %s (VGABUF_RAM)"
                 % (len(e["ax"]), sorted("%04X" % x for x in set(e["ax"]))))
            want(d["frame"] == e["frame"], "Paint's window is identical with "
                 "and without the ROM (%d differing bytes)"
                 % sum(x != y for x, y in zip(d["frame"], e["frame"])))

    if fails:
        sys.exit("rombig: %d failed" % len(fails))
    print("rombig: ok")


if __name__ == "__main__":
    main()
