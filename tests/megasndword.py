#!/usr/bin/env python3
"""A CLOSED STREAM LEAVES SOUND.DRV NO SEGMENT WORD (KERN-MEGA-PLAN 14.182,
SPEC.md 34.5.3, 53.2.3).

    make && make mega && python3 tests/megasndword.py

The owner's pre-release pass of the VGA demo videos on 86Box's 16 MHz 286,
each clip in its own Video Player window: 04, 08, 09 and 10 FELL BACK to a
protected-mode bracket with `FSXR_DRV` ([fsx_rwhy] 8) where a fresh boot ran
each one real. kern_mega's door asks every word of SOUND.DRV's SNDV_RMODE
list for a real-mode segment (`fsx_rsndq`) and refuses the bracket for one
with none. SOUND.DRV's ring segment ([sbl_rseg]) and an external ring's
control-word segment ([sbl_extctl+2]) outlived the stream that set them:
after the window closed they named the ring's FREED claim, and once the next
play's claims took that LDT entry for a block above 1MB the stale word
answered "no real segment" and the door refused - a stream that no longer
existed.

86Box's vm/viddemo-vga (the demo videos' machine: a Micronics MR 286 at 16
MHz, 4 MB, an OTI-067, an SB16 on IRQ 5, an IDE disk), kern_mega off
build/mega.img, C: a 128/16/63 disk holding the Video Player's arm at its
root and apps/video/demo/vga/10-WILLY.V88 and 11-TRACK.V88 in MEDIA, as
the demo disk has them. Each clip opened in its own window, F, Space, the
door's verdict read a guest second into the play, Esc, the window closed:

  1. 10's play in a real-mode bracket ([fsx_rwhy] FSXR_REAL, [fsx_rmode]
     1) - the positive control;
  2. after its close, with no stream open ([sbl_str_act] 0), SOUND.DRV's
     ring and control-word segments are 0, and every SNDV_RMODE word is 0
     or a present descriptor (the stale ones named a freed LDT entry);
  3. 11's play in a real-mode bracket too.

RED on mega-release (this file run against its build): 2 - [sbl_rseg] and
[sbl_extctl+2] both 1304h, a freed entry - and 3 - 11's door answered
FSXR_DRV (8) and the play ran protected. Green with sbl_stop_stream's two
stores (SOUND.DRV +6 bytes), the whole of the difference between the trees.
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
import os88drvsym   # noqa: E402
import os88marty as om  # noqa: E402
import os88sym      # noqa: E402
import os88ui       # noqa: E402

DEMO = os.path.join(ROOT, "apps", "video", "demo", "vga")
CLIPS = ("10-WILLY.V88", "11-TRACK.V88")
FSXR_REAL = 0
fails = []


def ok(cond, what):
    print("  %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def u16(b, o=0):
    return struct.unpack_from("<H", b, o)[0]


def main():
    os.chdir(ROOT)
    pkg = os88build.at("build/megak/apps/video.o88")
    sym = os88sym._parse_map(pkg[:-4] + ".map")[0]
    D = os88drvsym.symbols("sound.bin")
    snd = open(os88build.at("build/sound.bin"), "rb").read()
    n = (D["snd_rmlist_end"] - D["snd_rmlist"]) // 2
    rml = [u16(snd, D["snd_rmlist"] + 2 * i) for i in range(n)]
    tmp = tempfile.mkdtemp(prefix="megasndword-",
                           dir=os.path.join(ROOT, "build"))
    try:
        img, vhd = os.path.join(tmp, "c.img"), os.path.join(tmp, "c.vhd")
        tools = os.path.join(ROOT, "tools")
        subprocess.run([sys.executable, os.path.join(tools, "os88disk.py"),
                        "-o", img, "--hdd", "--geometry", "128/16/63",
                        "--mbr", os88build.at("build/mbr.bin"),
                        "--boot", os88build.at("build/megak/boothd.bin"),
                        "--kernel", os88build.at("build/megak/kernel.sys"),
                        pkg] + ["MEDIA:" + os.path.join(DEMO, c)
                                for c in CLIPS],
                       check=True, capture_output=True)
        subprocess.run([sys.executable, os.path.join(tools, "os88hdd.py"),
                        "--wrap", img, "--cyls", "128", "--heads", "16",
                        "--spt", "63", "--out", vhd], check=True,
                       capture_output=True)
        os.remove(img)
        with os88ui.boot(os88build.at("build/mega.img"),
                         machine="at286_vga_hdd", label="megasndword",
                         extra=["--mount", "hd:0:" + vhd]) as ui:
            m, S = ui.m, ui._S
            B = ["vp_loaded", "vp_ready", "vp_played"]

            def st(w):
                if m.read(S("fsx_rmode"), 1)[0]:
                    seg = u16(m.read(S("fsx_rseg"), 2))
                    r = [m.read(seg * 16 + sym[k], 1) for k in B]
                else:
                    r = om.pkg_reads(m, w, [(sym[k], 1) for k in B])
                return {k: b[0] for k, b in zip(B, r)}

            def play(i, clip):
                w = ui.path("C:/MEDIA/" + clip)
                om.until(m, lambda mm: st(w)["vp_loaded"] == 1, "the header",
                         poll=0.2, limit=600, guest=60)
                m.type_text("f")
                om.until(m, lambda mm: st(w)["vp_ready"] == 1,
                         "F into the full screen", poll=0.1, limit=600,
                         guest=60)
                m.type_text(" ")
                om.guest_sleep(m, 1)            # the play a second in
                rm = [m.read(S("fsx_rmode"), 1)[0]]
                why = m.read(S("fsx_rwhy"), 1)[0]
                print("  %s: [fsx_rwhy] %d, real-mode bracket %d" % (
                    clip, why, rm[0]), flush=True)
                ok(why == FSXR_REAL and rm[0] == 1,
                   "%d. %s plays in a real-mode bracket" % (i, clip))
                m.key("Escape")
                om.until(m, lambda mm: st(w)["vp_played"] == 1 or
                         st(w)["vp_ready"] == 0, "the play to stop",
                         poll=0.2, limit=300, guest=30)
                om.guest_sleep(m, 2)
                ui.close(w)
                om.guest_sleep(m, 2)

            play(1, CLIPS[0])
            sel = u16(m.read(S("drv_fptr") + 2, 2))     # DRVC_SOUND's data

            def word(off):
                return u16(m.read(m.lin(sel, off), 2))
            act = m.read(m.lin(sel, D["sbl_str_act"]), 1)[0]
            ws = [(off, word(off)) for off in rml]
            stale = ["%04X=%04X" % (o, v) for o, v in ws
                     if v and m.selector(v) is None]
            rseg, xseg = word(D["sbl_rseg"]), word(D["sbl_extctl"] + 2)
            print("  after the close: stream open %d, [sbl_rseg] %04X, "
                  "[sbl_extctl+2] %04X; SNDV_RMODE words %s; naming a freed "
                  "entry: %s" % (act, rseg, xseg,
                                 " ".join("%04X=%04X" % x for x in ws),
                                 stale or "none"), flush=True)
            ok(act == 0 and rseg == 0 and xseg == 0 and not stale,
               "2. no stream open, and SOUND.DRV holds no segment word of "
               "the closed one")
            play(3, CLIPS[1])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        print("megasndword: FAIL (%d)" % len(fails))
        return 1
    print("megasndword: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
