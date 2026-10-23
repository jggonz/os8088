#!/usr/bin/env python3
"""REALDMG.O88's four channels on the FM chip.

    python3 tests/gbfm.py

docs/REALDMG-SPEC.md section 17.1. MartyPC's VGA XT with a Sound Blaster
(SOUND.DRV loads for it), REALDMG.O88 and GBFM.GB (tools/gbprof/gbfm.asm,
ours) on B:. GBFM starts square 1 at 440 Hz, square 2 at 660 Hz, the wave at
220 Hz and the noise, a channel at a time, and holds them. Once all four
sound:

  * the package holds the chip (rw_fmon) and its shadow of each channel's
    A0h and B0h is the F-number, block and key the Game Boy's registers
    say - worked out here from the Game Boy's own formulas, not copied from
    the package's;
  * SOUND.DRV's own record of each channel's B0h (opl_b0, which every
    SNDV_OPLW write passes through) agrees, so the writes reached the chip;
  * the capture of the card's output has sound in it, and with square 1
    alone its strongest peak is 440 Hz and its octave well under that - a
    50% square's harmonics are the odd ones, which the modulator at twice
    the carrier makes (a 1:1 patch gave 880 Hz louder than 440).

Without rw_fmopen's claim the speaker plays instead and the first check
fails; a wrong F-number fails the second.
"""
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88marty as M  # noqa: E402
import os88ui  # noqa: E402
import sndcheck  # noqa: E402
import gbwindow  # noqa: E402

MACHINE = "os8088_xt_vga_sb"


def opl(d):
    """2048 - x of a square's 131,072 / (2048 - x) Hz -> (B0h, A0h) keyed"""
    if d < 22:
        return None
    if d < 43:
        fnum, block = 21597 // d, 7
    else:
        fnum, block = 2764482 // d, 0
        while fnum >= 1024:
            fnum >>= 1
            block += 1
    return 0x20 | block << 2 | fnum >> 8, fnum & 0xFF


def main():
    rom = subprocess.check_output([sys.executable, os.path.join(ROOT, "tools", "gbours.py"), "gbfm"],
                                  text=True).strip().splitlines()[-1]
    tmp = tempfile.mkdtemp(prefix="gbfm-")
    gb = os.path.join(tmp, "GBFM.GB")
    with open(rom, "rb") as i, open(gb, "wb") as o:
        o.write(i.read())
    disk = os.path.join(tmp, "apps.img")
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"), "-o", disk,
                           "--size", "360", os.path.join(ROOT, "build", "realdmg.o88"), gb],
                          stdout=subprocess.DEVNULL)
    viewoff = gbwindow.label_off(os.path.join(ROOT, "build", "realdmg.lst"), "rdg_viewseg")
    dbgoff = gbwindow.label_off(os.path.join(ROOT, "build", "realdmg.lst"), "rw_fmdbg")
    import os88drvsym
    oplb0 = os88drvsym.symbols("sound.bin")["opl_b0"]
    want = [opl(2048 - 1750), opl(2048 - 1849), opl(2 * (2048 - 1750)), opl(22)]
    fails = []
    cap = os.path.join(tmp, "audio")
    os.environ["MARTYPC_WAV"] = cap
    try:
        with os88ui.boot(os.path.join(ROOT, "build", "os8088-360.img"), apps=disk,
                         machine=MACHINE) as ui:
            os.environ.pop("MARTYPC_WAV", None)
            m = ui.m
            ui.path("B:/GBFM.GB")
            seg = view = None
            for _ in range(60):
                M.guest_sleep(m, 2)
                seg = seg or gbwindow.find_seg(m, b"REALDMG", viewoff)
                if seg:
                    view = int.from_bytes(m.read(seg * 16 + viewoff, 2), "little")
                if view and m.read(view * 16 + 0xC000, 1)[0] >= 150:
                    break
            else:
                fails.append("GBFM.GB never reached frame 150")
            if seg:
                shp, onp = struct.unpack("<HH", m.read(seg * 16 + dbgoff, 4))
                on = m.read(seg * 16 + onp, 1)[0]
                sh = m.read(seg * 16 + shp, 16)
                fseg = struct.unpack("<H", m.read(m.sym("drv_fseg"), 2))[0]
                b0 = m.read(fseg * 16 + oplb0, 4)
                print("gbfm: chip ours %d; channels (B0h, A0h): %s; driver's B0h: %s; want %s"
                      % (on, [(sh[c * 4], sh[c * 4 + 1]) for c in range(4)], list(b0), want))
                if not on:
                    fails.append("the package does not hold the FM chip")
                for c in range(4):
                    got = (sh[c * 4], sh[c * 4 + 1])
                    if got != want[c]:
                        fails.append("channel %d: B0h/A0h %02X/%02X, want %02X/%02X"
                                     % (c, got[0], got[1], want[c][0], want[c][1]))
                    if b0[c] != want[c][0]:
                        fails.append("channel %d: the driver wrote B0h %02X, want %02X"
                                     % (c, b0[c], want[c][0]))
            M.guest_sleep(m, 2)
    finally:
        os.environ.pop("MARTYPC_WAV", None)
    wavs = [f for f in sorted(os.listdir(tmp)) if f.startswith("audio") and "pc_speaker" not in f]
    print("gbfm: captures %s" % wavs)
    loud = 0.0
    for f in wavs:
        try:
            rate, mono, _ = sndcheck.load(os.path.join(tmp, f))
        except BaseException as e:              # noqa: BLE001
            print("gbfm: %s: %s" % (f, e))
            continue
        tail = mono[-rate:]
        loud = max(loud, (sum(v * v for v in tail) / max(1, len(tail))) ** 0.5)
    print("gbfm: the card's last second, RMS %.4f of full scale" % loud)
    if loud < 0.005:
        fails.append("the card's capture is silent (RMS %.4f)" % loud)
    opl3 = [f for f in wavs if "adlib" in f]
    if opl3:                                # square 1 alone, its first 0.4 s:
        import numpy as np                  #   440 Hz the strongest, and its
        rate, mono, _ = sndcheck.load(os.path.join(tmp, opl3[0]))   # octave,
        x = np.array(mono)                  #   an even harmonic a 50% square
        on = int(np.argmax(np.abs(x) > 0.02))   # has not, well under it
        seg = x[on + rate // 20:on + rate // 20 + int(0.4 * rate)]
        sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
        fr = np.fft.rfftfreq(len(seg), 1.0 / rate)
        top = fr[int(np.argmax(sp))]
        at = lambda hz: float(sp[(fr > hz - 8) & (fr < hz + 8)].max())
        print("gbfm: square 1 alone: strongest %.0f Hz; 880 Hz at %.2f of 440's"
              % (top, at(880) / at(440)))
        if abs(top - 440) > 8:
            fails.append("square 1 alone peaks at %.0f Hz, not 440" % top)
        if at(880) > 0.25 * at(440):
            fails.append("square 1 has an octave (880 Hz) at %.2f of its 440: not a square's "
                         "odd harmonics" % (at(880) / at(440)))
    for f in fails:
        print("FAIL gbfm: " + f)
    print("gbfm: %s" % ("PASS" if not fails else "FAIL"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
