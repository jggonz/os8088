#!/usr/bin/env python3
"""A LENT SOUND.DRV GONE BACK UP KEEPS ITS VECTOR (KERN-MEGA-PLAN 14.181,
SPEC.md 53.2.3.1).

    make && make mega && python3 tests/megasndup.py

The owner's pre-release pass of the VGA demo videos on 86Box's 16 MHz 286:
the first clip played with its sound, and every clip after it played SILENT
past its first block. kern_mega lends SOUND.DRV down for a real-mode bracket
(FSXF_PREP, mega_drvlow) and takes it back up when the instance ends
(mega_drvup); the forced sync at the bracket's return had written the card's
IVT cell as the lent image's conventional SEGMENT, and the trip up left it
there - a paragraph that was no longer the driver's. The next door matched
neither the pair's code half nor the new copy, banked nothing and left the
line shut; the gates the next sync built went to whatever was there.

86Box's vm/viddemo-vga (the demo videos' machine: a Micronics MR 286 at 16
MHz, 4 MB, an OTI-067, an SB16 on IRQ 5, an IDE disk), kern_mega off
build/mega.img, C: a 128/16/63 disk holding the Video Player's arm at its
root and apps/video/demo/vga/06-SNOW.V88 (15 s, 303 frames, 22 kHz PCM) in
MEDIA, as the demo disk has it. (On vm/at286-port with the clip at C:'s root
the second copy lands on the first's paragraph, the stale cell happens to
name it again, and only 2 goes red.) The clip opened, F, Space, played to its end, Esc,
the window CLOSED (the instance ends: SOUND.DRV back up) - twice:

  1. play 1 (the positive control): in a real-mode bracket, every frame
     drawn, the card open to the end ([vp_snd] 1), never dry ([vp_pause]
     0), sound queued for every frame ([vp_afr]);
  2. after the first close, SOUND.DRV is back above 1MB and every IRQ cell
     (IVT 08h-0Fh, 70h-77h) that is not the BIOS's names a pair's code
     half or the kernel - none names a conventional paragraph that is not
     an image's (the stale cell named the freed copy's);
  3. play 2, in a NEW window: 1's four facts again.

RED on mega-release (this file run against its build): 2 - IVT 0Dh names
the freed copy's paragraph (1560:0831, the image back up at 3E7C00) - and
3 - play 2 silent after 16 frames, [vp_snd] 0, [vp_pause] 1, [vp_afr] 16.
Green with mega_drvivt (14.181), whose `call` in mega_drvup is the whole of
the difference between the two trees' kernels.
"""
import os
import struct
import subprocess
import sys
import tempfile
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [os.path.join(ROOT, "tools"), HERE]
for _k in ("OS88_EMU", "OS88_86BOX_VM", "OS88_MEGA_SND"):
    os.environ.pop(_k, None)
os.environ.update(OS88_MEGA="1", OS88_MEGA_EMU="86box",
                  OS88_MEGA_VM="viddemo-vga")
os.environ.setdefault("OS88_86BOX_ROMS", os.path.join(ROOT, "build", "86box",
                                                      "roms"))
import os88build    # noqa: E402
import os88marty as om  # noqa: E402
import os88sym      # noqa: E402
import os88ui       # noqa: E402
import os88vid as vid   # noqa: E402

CLIP = os.path.join(ROOT, "apps", "video", "demo", "vga", "06-SNOW.V88")
fails = []


def ok(cond, what):
    print("  %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def u16(b, o=0):
    return struct.unpack_from("<H", b, o)[0]


def main():
    pkg = os88build.at("build/megak/apps/video.o88")
    sym = os88sym._parse_map(pkg[:-4] + ".map")[0]
    nf = vid.Reader(CLIP).frames
    tmp = tempfile.mkdtemp(prefix="megasndup-", dir=os.path.join(ROOT,
                                                                 "build"))
    try:
        img, vhd = os.path.join(tmp, "c.img"), os.path.join(tmp, "c.vhd")
        tools = os.path.join(ROOT, "tools")
        subprocess.run([sys.executable, os.path.join(tools, "os88disk.py"),
                        "-o", img, "--hdd", "--geometry", "128/16/63",
                        "--mbr", os88build.at("build/mbr.bin"),
                        "--boot", os88build.at("build/megak/boothd.bin"),
                        "--kernel", os88build.at("build/megak/kernel.sys"),
                        pkg,
                        "MEDIA:" + CLIP], check=True, capture_output=True)
        subprocess.run([sys.executable, os.path.join(tools, "os88hdd.py"),
                        "--wrap", img, "--cyls", "128", "--heads", "16",
                        "--spt", "63", "--out", vhd], check=True,
                       capture_output=True)
        os.remove(img)
        with os88ui.boot(os88build.at("build/mega.img"),
                         machine="at286_vga_hdd", label="megasndup",
                         extra=["--mount", "hd:0:" + vhd]) as ui:
            m, S = ui.m, ui._S
            W = ["vp_done", "vp_pause", "vp_afr"]
            B = ["vp_loaded", "vp_ok", "vp_ready", "vp_played", "vp_snd"]

            def st(w):
                names = [(n, 2) for n in W] + [(n, 1) for n in B]
                if m.read(S("fsx_rmode"), 1)[0]:
                    seg = u16(m.read(S("fsx_rseg"), 2))
                    r = [m.read(seg * 16 + sym[n], ln) for n, ln in names]
                else:
                    r = om.pkg_reads(m, w, [(sym[n], ln) for n, ln in names])
                d = {n: (b[0] if ln == 1 else u16(b))
                     for (n, ln), b in zip(names, r)}
                d["rmode"] = m.read(S("fsx_rmode"), 1)[0]
                return d

            def play(n):
                w = ui.path("C:/MEDIA/06-SNOW.V88")
                om.until(m, lambda mm: st(w)["vp_loaded"] == 1, "the header",
                         poll=0.2, limit=600, guest=60)
                m.type_text("f")
                om.until(m, lambda mm: st(w)["vp_ready"] == 1,
                         "F into the full screen", poll=0.1, limit=600,
                         guest=60)
                m.type_text(" ")
                rm = [0]

                def done(mm):
                    s = st(w)
                    rm[0] |= s["rmode"]
                    return s["vp_ready"] == 0
                om.until(m, done, "the play to end", poll=0.5, limit=900,
                         guest=nf / 20.0 + 60)
                om.until(m, lambda mm: st(w)["vp_played"] == 1,
                         "the bracket to return", poll=0.2, limit=300,
                         guest=30)
                s = st(w)
                print("  play %d: drawn %d of %d, card %d, dry %d, sound "
                      "for %d frames, real-mode bracket %d" % (
                          n, s["vp_done"], nf, s["vp_snd"], s["vp_pause"],
                          s["vp_afr"], rm[0]), flush=True)
                ok(rm[0] == 1 and s["vp_done"] == nf and s["vp_snd"] == 1
                   and s["vp_pause"] == 0 and s["vp_afr"] == nf,
                   "%d. play %d: a real-mode bracket, every frame drawn, the "
                   "card open to the end and never dry, sound for every "
                   "frame" % (1 if n == 1 else 3, n))
                m.key("Escape")
                om.guest_sleep(m, 2)
                ui.close(w)
                om.guest_sleep(m, 2)
            play(1)
            # 2: SOUND.DRV back up, and no IRQ cell naming a paragraph that
            # is not somebody's image (the stale one named the freed copy's)
            ivt = m.read(0, 0x78 * 4)
            sel = u16(m.read(S("drv_fptr") + 2, 2))
            up = m.lin(sel, 0) >= 0x100000
            bad = []
            for v in list(range(8, 16)) + list(range(0x70, 0x78)):
                seg, off = u16(ivt, v * 4 + 2), u16(ivt, v * 4)
                if seg >= 0xA000 or seg == 0x0060 or seg & 4:
                    continue            # the ROM's, the kernel's, a pair's
                bad.append("%02X=%04X:%04X" % (v, seg, off))
            print("  after the close: SOUND.DRV at %06X (selector %04X); "
                  "IRQ cells naming a conventional paragraph: %s" % (
                      m.lin(sel, 0), sel, bad or "none"), flush=True)
            ok(up and not bad,
               "2. SOUND.DRV back above 1MB and no IRQ cell naming a "
               "conventional paragraph")
            play(2)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        print("megasndup: FAIL (%d)" % len(fails))
        return 1
    print("megasndup: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
