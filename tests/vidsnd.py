#!/usr/bin/env python3
"""One interrupt per video frame off a Sound Blaster 2.0, hardware ADPCM, and
how much of each frame a streaming disk leaves - VIDEO-PLAN wave 0 (c)(e).

    make vidbench && python3 tests/vidsnd.py
                                [--machine os8088_5150_herc_hdd_sb_gla]

tests/vidbench/vidsnd.asm suspends the drivers, finds the card's line with
DSP F2h and programs DMA channel 1 and the DSP itself, as VIDEO-PLAN 4.4's
frame stream will: auto-init over a double buffer, DSP block = one frame's
audio. It counts the interrupts for 5 seconds at XDC's two shapes (22,050 Hz
with a 735-byte block = 30 fps; 8,040 Hz with 134 = 60 fps) and with 4-bit
ADPCM (DSP 7Dh), then reads whole tracks off the fixed disk for 5 seconds
while the interrupt burns 0, 25, 50 and 75% of each frame with interrupts
off, and 50% with them on (acknowledged and EOI'd first, as the player's
hook does). It saves VIDSND.TXT beside itself, read back off the VHD.

ASSERTED: the card was found and answered, a line was found, a fixed disk
answered, and both PCM rows interrupt at the DSP's rate / the block, within
2% - the property the player's clock is. REPORTED, never gated: the ADPCM
row (whether MartyPC's DSP does 7Dh at all is part of the question) and
the ceiling, which on this emulator is an XT-IDE's (CPU-copied) and not the
owner's DMA ST11M (tools/martypc/configs/os8088_machines.toml).
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88build, os88flush, os88geom as geom  # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
SECS = 91 * 65536 / 1193182.0   # VS_TICKS
FLAGS = {1: "no card answered at the driver's port or 220h",
         2: "the DSP did not reset", 4: "no fixed disk answered int 13h",
         8: "no IRQ line answered DSP F2h", 16: "OSAPI_DRV_SUSPEND refused",
         32: "no claim"}


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_hdd_sb_gla")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, image = pkg_syms("tests/vidbench/vidsnd.asm", ("apps/", "tests/"))
    try:
        built = open(os88build.at("build/vidsnd.bin"), "rb").read()
    except OSError:
        sys.exit("vidsnd: no build/vidsnd.bin - run `make vidbench`")
    if built != image:
        sys.exit("vidsnd: build/vidsnd.bin is behind the tree - run "
                 "`make vidbench`")
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        vhd = os.path.join(tmp, "vidsnd.vhd")
        subprocess.run(
            ["python3", "tools/os88hdd.py", "--template", TEMPLATE,
             "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
             "--vbr", os88build.at("build/boothd.bin"),
             "--mbr", os88build.at("build/mbr.bin"),
             "--file", "HDD.DRV=" + os88build.at("build/hdd.drv"),
             # OSAPI_DRV_SUSPEND runs through HIBER.DRV, read off the boot
             # volume (SPEC.md 51.11): without it the suspend refuses
             "--file", "HIBER.DRV=" + os88build.at("build/hiber.drv"),
             "--file", "CTRL.DRV=" + os88build.at("build/ctrl.drv"),
             "--file", "SOUND.DRV=" + os88build.at("build/sound.drv"),
             "--file", "VIDSND.O88=" + os88build.at("build/vidsnd.o88")],
            check=True, capture_output=True)
        m = os88marty.launch(None, machine=a.machine,
                             extra=["--mount", "hd:0:" + vhd])
        try:
            ui = os88ui.UI(m)
            ui.ready(limit=240)
            w = ui.path("C:/VIDSND.O88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4

            def rw(name):
                return u16(m.read(base + syms[name], 2))

            m.type_text("r")
            os88marty.until(m, lambda mm: rw("vs_done") != 0,
                            "the sound bench to finish", poll=1.0,
                            limit=1500.0, guest=300.0)
            r = [u16(m.read(base + syms["vs_res"], 32), 2 * i)
                 for i in range(16)]
            full = m.read(base + syms["bl_full"], 1)[0]
        finally:
            m.close()
        try:                        # the report the bench SAVED, off the VHD
            txt = os88flush.vhd_volume(vhd).read("VIDSND.TXT").decode(
                "latin-1")
            txterr = None
        except Exception as e:
            txt, txterr = None, str(e)
    flags = r[14]
    bad = [FLAGS[b] for b in FLAGS if flags & b]
    print("\n   machine %s: DSP at %03xh, IRQ %d, version %d.%02d"
          % (a.machine, r[11], r[12], r[13] >> 8, r[13] & 0xFF))

    def rate(tc, block, irqs, label):
        hz = 1e6 / (256 - tc)
        want = hz / block * SECS
        print("   %-28s %5d interrupts in %.2f s = %6.2f/s   (the DSP's "
              "%.0f Hz / %d = %.2f/s)" % (label, irqs, SECS, irqs / SECS, hz,
                                          block, hz / block))
        return want

    w22 = rate(211, 735, r[0], "PCM8 22,050 Hz / 735 (30 fps)")
    w8 = rate(132, 134, r[2], "PCM8  8,040 Hz / 134 (60 fps)")
    rate(211, 735, r[4], "ADPCM4 22,050 Hz / 368 bytes")
    for got, want, lab in ((r[0], w22, "30 fps"), (r[2], w8, "60 fps")):
        if not flags and abs(got - want) > want * 0.02:
            bad.append("the %s row interrupted %d times, not %.0f" %
                       (lab, got, want))
    if not flags & 4:
        trk = r[10] * 512
        print("\n   fixed disk, whole %d-sector tracks for %.2f s, the "
              "interrupt burning:" % (r[10], SECS))
        base0 = r[6] or 1
        for pct, n, how in ((0, r[6], ""), (25, r[7], ""), (50, r[8], ""),
                            (75, r[9], ""), (50, r[15], ", ints on")):
            print("     %2d%%  %4d tracks  %6.1f KB/s  (%3.0f%% of the "
                  "unburnt rate)%s" % (pct, n, n * trk / 1024.0 / SECS,
                                       100.0 * n / base0, how))
    if full:
        bad.append("the report TRUNCATED (bl_full): the arena is too small")
    if txt is None:
        bad.append("no VIDSND.TXT on C: - the save did not happen (%s)"
                   % txterr)
    elif "flags" not in txt or "TRUNCATED" in txt:
        bad.append("VIDSND.TXT is not the whole report")
    else:
        print("\n   VIDSND.TXT saved: %d lines" % len(txt.splitlines()))
    for b in bad:
        print("   FAIL: %s" % b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
