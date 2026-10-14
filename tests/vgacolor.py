#!/usr/bin/env python3
"""Does Force Color undo a VGA BIOS's "monochrome display" verdict? (SPEC.md 39.28)

    make && python3 tests/vgacolor.py

A VGA BIOS senses the monitor once, at POST, and RECORDS the answer: 40:89h
bit 2 (mono display) and bit 1 (grey-scale summing), and with summing on
every palette it loads is grey - os8088's whole desktop with it, because the
kernel never programs the DAC. The owner's two PVGA1A machines reach that
verdict with a COLOUR monitor that was already on. No emulator here models
the sense circuit, so no BIOS reaches it by itself: this row puts the record
where the PVGA1A leaves it - stopped at `drvf_drv_mounted`, drv_boot_x's
first call and so ahead of the settings read and `vgcf_boot` on every boot,
with the splash already on the glass in mode 12h, it
sets 40:89h bits 1 and 2 and sums the live DAC to grey through the
emulator's own port doors - and then lets the machine boot.

Four legs, on MartyPC's VGA and on a two-card machine with none:

  1. the box CLEAR (a stock disk, no 'VC' record): the desktop must still be
     grey and the record still mono. The default must not force anything -
     on a real 8503 the BIOS was right;
  2. the panel, same boot: Display page, a click on Force Color. drv_cfg's
     byte goes to 1 and the DAC is the EGA table NOW, exactly; the close
     writes 'VC' ver 1 len 1 = 1 to SYSTEM.CFG;
  3. the WRITTEN disk booted with the same grey verdict planted: the desktop
     comes up on the EGA table and 40:89h's two bits are clear;
  4. a CGA + Hercules machine: the Display page is shown (two adapters) and
     the box is there GREYED - a click on it must change nothing, neither
     the byte nor [cp_wdirty] (SPEC.md 31.10.4, 47 rule 1).

Take `call COLD_SEG:vgcf_boot` out of drv_boot_x and leg 3 fails grey; take
the `call cp_vgc_force` out of cp_vid_click and leg 2 fails on the DAC; take
cp_vid_fcok's `jc .done` out of the click and leg 4 fails on the byte.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88build                                            # noqa: E402
import os88flush                                            # noqa: E402
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402

S = os88sym.linear
TITLE_H = 18
CP_RX = 96
BDA_VGAF = 0x489                # 40:89h - bit 1 grey summing, bit 2 mono


def ega_table():
    """The 64 entries a COLOUR BIOS loads for modes 10h/12h: bits 2/1/0 are
    R/G/B at 2Ah, bits 5/4/3 are r/g/b at 15h. Written from the rgbRGB
    definition here, not from the kernel's loop, so the two are independent."""
    t = []
    for i in range(64):
        for hi, lo in ((2, 5), (1, 4), (0, 3)):
            t.append((0x2A if i >> hi & 1 else 0) + (0x15 if i >> lo & 1 else 0))
    return t


EGA = ega_table()


def dac(m):
    m.outb(0x3C7, 0)
    return [m.inb(0x3C9) for _ in range(192)]


def plant_grey(m):
    """The PVGA1A's verdict, at drvf_drv_mounted - drv_boot_x's first call,
    before SYSTEM.CFG is read and so before vgcf_boot on EVERY boot (a disk
    with no settings file never reaches vgcf_boot at all): summing and mono
    display in the
    record, and the palette already on the glass summed (0.30R+0.59G+0.11B,
    the BIOS's own weights). Answers the grey palette it wrote."""
    m.bp_exec("drvf_drv_mounted")
    m.run()
    if not m.wait_stop(60.0):
        raise SystemExit("vgacolor: the boot never reached drvf_drv_mounted")
    m.bp_exec()
    cur = dac(m)
    if cur != EGA:
        raise SystemExit("vgacolor: MartyPC's BIOS did not load the EGA table "
                         "for mode 12h, so the comparison below means nothing")
    m.write(BDA_VGAF, bytes([m.read(BDA_VGAF, 1)[0] | 0x06]))
    m.outb(0x3C8, 0)
    grey = []
    for i in range(64):
        r, g, b = cur[3 * i:3 * i + 3]
        y = (30 * r + 59 * g + 11 * b + 50) // 100
        for _ in range(3):
            m.outb(0x3C9, y)
        grey += [y, y, y]
    m.run()
    return grey


def boot_grey(image, apps, machine):
    m = os88marty.launch(image, apps=apps, machine=machine, boot=0)
    grey = plant_grey(m)
    os88marty.settle(m)
    return m, grey


def click_box(m, mo):
    eq = os88sym.equates()
    wx, wy = dispcp._cp_win(m, S)
    mo.click(wx + 1 + CP_RX + eq["CPV_FCX"] + 6,
             wy + TITLE_H + 1 + eq["CPV_FCY"] + 6, settle=0)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--grey-machine", default="os8088_5150_both_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--shot", default="build/vgacolor.png")
    a = ap.parse_args(argv)
    a.image = os.path.abspath(os88build.at(a.image))
    a.apps = os.path.abspath(os88build.at(a.apps))
    eq = os88sym.equates()
    VGC = S("drv_cfg") + eq["CFG_VGC"]
    page = eq["CP_IVID"]
    settle = os88marty.settle
    written = os.path.abspath(os.path.join("build", "vgacolor-written.img"))
    fail = []

    def byte(m, addr):
        return m.read(addr, 1)[0]

    # --- 1 and 2: a stock disk, the verdict planted --------------------------
    m, grey = boot_grey(a.image, a.apps, a.machine)
    with m:
        if byte(m, VGC):
            sys.exit("vgacolor: the disk already carries a 'VC' record")
        if dac(m) != grey or not byte(m, BDA_VGAF) & 6:
            fail.append("leg 1: the box is CLEAR and the desktop is not grey "
                        "- the default forced colour on a mono verdict")
        else:
            print("leg 1: box clear, desktop grey, 40:89 = %02X     ok"
                  % byte(m, BDA_VGAF))
        f = os88flush.Flush(marty=m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, settle, page=page)
        settle(m)
        click_box(m, mo)
        try:
            os88marty.until(m, lambda mm: byte(mm, VGC) == 1,
                            "Force Color's byte to tick", poll=0.05,
                            guest=10.0)
        except os88marty.MartyError as e:
            fail.append("leg 2: the click did not tick the box (%s)" % e)
        settle(m)
        w, h, rgb = m.fbuf()                # VGA: the rendered frame
        os88marty.write_png_rgb(a.shot, w, h, rgb)
        got = dac(m)
        if got != EGA:
            fail.append("leg 2: ticked, and the DAC is not the EGA table "
                        "(entries %s differ)" % [i for i in range(64)
                                                 if got[3*i:3*i+3]
                                                 != EGA[3*i:3*i+3]][:8])
        elif byte(m, BDA_VGAF) & 6:
            fail.append("leg 2: ticked, and 40:89 still says mono/summing")
        else:
            print("leg 2: ticked, the EGA table now, 40:89 = %02X    ok"
                  % byte(m, BDA_VGAF))
        dispcp.close_panel(m, mo, S, settle)
        try:
            cfg = f.volume(0).read("SYSTEM.CFG")
        except os88flush.FlushError:
            cfg = b""
        if b"VC\x01\x01\x01" not in cfg:
            fail.append("leg 2: SYSTEM.CFG carries no 'VC' ver 1 len 1 = 1")
        f.save(0, written)

    # --- 3: the written disk, the same verdict -------------------------------
    m, _ = boot_grey(written, a.apps, a.machine)
    with m:
        got = dac(m)
        if byte(m, VGC) != 1:
            fail.append("leg 3: the 'VC' record did not load")
        elif got != EGA or byte(m, BDA_VGAF) & 6:
            fail.append("leg 3: the box is ticked and the desktop came up "
                        "grey (40:89 = %02X)" % byte(m, BDA_VGAF))
        else:
            print("leg 3: boot with the box ticked: EGA table,        ok")

    # --- 4: no VGA, so the box is greyed and refuses --------------------------
    with os88marty.launch(a.image, apps=a.apps,
                          machine=os88marty.machine(a.grey_machine)) as m:
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, settle, page=page)
        settle(m)
        click_box(m, mo)
        settle(m)
        if byte(m, VGC) or byte(m, S("cp_wdirty")):
            fail.append("leg 4: no VGA, and the greyed box took the click")
        else:
            print("leg 4: no VGA, greyed box refuses               ok")
        w, h, rows = m.vram(None)
        os88marty.write_png(a.shot.replace(".png", "-grey.png"), w, h, rows)

    if fail:
        print("vgacolor: FAIL")
        for x in fail:
            print("  " + x)
        return 1
    print("vgacolor: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
