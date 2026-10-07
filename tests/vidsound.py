#!/usr/bin/env python3
"""VIDEO.O88 with SOUND: the card is the clock - SPEC.md 98.3.1, VIDEO-PLAN
wave 4.

    make && python3 tests/vidsound.py [--secs 60] [--machine NAME]

A clip made here - SECS seconds at 30 fps, Hercules layout, 22,050 Hz PCM8
whose bytes are a pseudo-random sequence - goes on a bootable fixed disk
with VIDEO.O88 and SOUND.DRV, on the Hercules 5150 with a Sound Blaster,
and is played off that disk with the card's output captured
(MARTYPC_WAV). What must hold:

  1. every frame drawn, no stall (the reader kept up), no error;
  2. NO PAUSE: the card never ran dry (vp_pause), and the drain reached the
     last byte of sound;
  3. the picture never trailed the sound by more than two frames (vp_skmax)
     and was never more than two behind at a hook (vp_late);
  4. THE SOUND IS THE FILE'S: the capture, decoded back to the card's bytes,
     holds the clip's audio whole and in order;
  5. the play took the SOUND's time - the clip's bytes at the rate the card
     really runs (its time constant truncates 22,050 to 22,222 Hz) - within
     2%: the picture followed the card, not the file's nominal rate.

--pause holds Space for 2 guest seconds a third of the way in (SPEC.md
98.3.4): not a frame is drawn and not a byte of sound consumed while it
lasts - the card is halted (SOUND.DRV verb 10, 34.5.4) rather than left to
play out its ring - and the capture still holds the whole sound in order,
the play's time taken without the pause.

--fs goes in with F (98.3.6): full screen on the first frame, PAUSED, the
card not yet opened; Space then plays it, and the capture must still hold
the whole sound from frame 0 - the card started on the frame on the screen.

--seek N starts the play at the clip's Nth keyframe (98.3.5): the frames
from k+1 on, and the sound from frame k+1's on.

--loop L makes the clip REPEAT (98.3.9): a seam from its last frame back to
frame L, Repeat on from the file's flag. It plays two whole laps past the
first, then R turns Repeat off and the lap under way ends the play - and
every point above holds of the whole run: the capture must be the first
lap's sound and then frame L's to the end, twice, with nothing between -
the seam carrying frame L's audio - and the play takes all of it's time.

--resident makes the clip RESIDENT (98.1.7): its records one LZB block and
its sound one audio block, the whole read and expanded before the play and
the card fed from memory - every point above the same, off a file that is
never read again once the play starts.

--live makes the clip LIVE (98.3.10.1): a resident 160 x 60 canvas on
LIN80 for the Hercules desktop, its sound 11,025 Hz so the block stays
under 60 KB, played ON THE DESKTOP by the package's worker with the card
the clock - every point above the same, the drain included: the worker
plays the sound out to its last byte before the play is over. Built with
NOLIVESND=1 the play is silent and it FAILS on the card not opened.

--live --swap presses F a third of the way in - the play goes on in the
full screen, the card paused as the worker stops and resumed by the
bracket - and F again once ten frames have been drawn there, back to the
desktop Live. The capture must still hold the whole sound in order.

--audio adpcm4 plays the same clip's sound as Creative 4-bit ADPCM, which
the card decodes (DSP 7Dh); MartyPC's does it with the tables
tools/os88vid.py encodes against (tools/martypc/patches/06), so point 4 then
compares the capture with the stream DECODED. 86Box's card, and a real one,
are the independent check.
"""
import argparse
import glob
import os
import random
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88build, os88vid as vid, os88geom as geom  # noqa: E402
import sndcheck                                               # noqa: E402
import os88venc as venc                                       # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402

FPS = 30.0
RATE = 22050
TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
HZ = 4772727.0


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def clip(tmp, nf, afmt, loop=None, resident=None, live=False):
    """nf canvases 80 x 200 in the Hercules layout, and 22,050 Hz of
    pseudo-random PCM8 for them - or, LIVE, 20 x 60 on LIN80"""
    rnd = random.Random(4242)
    wb, h = (20, 60) if live else (80, 200)
    cv = bytearray(wb * h)
    paths = []
    for f in range(nf):
        if f % 60 == 0:
            cv = bytearray(wb * h)
        x, y = (f * 3) % (wb - 8), (f * 5) % (h - 24)
        for r in range(24):
            cv[(y + r) * wb + x:(y + r) * wb + x + 8] = \
                bytes([0x3C ^ (f & 0xFF)]) * 8
        if f % 30 in (10, 11):
            for _ in range(200):
                a = rnd.randrange(wb * h - 8)
                cv[a:a + 8] = bytes(rnd.getrandbits(8) for _ in range(8))
        p = os.path.join(tmp, "f%04d.pbm" % f)
        vid._write_pbm(p, wb, h, bytes(cv))
        paths.append(p)
    audio = bytes(rnd.getrandbits(8) for _ in range(int(RATE * nf / FPS)))
    wav = os.path.join(tmp, "a.wav")
    vid._write_wav(wav, RATE, audio)
    out = os.path.join(tmp, "CLIP.V88")
    vid.encode_frames(paths, out, FPS, wav, "lin80" if live else "herc",
                      "vidsound clip", audio_fmt=afmt, loop=loop,
                      repeat=loop is not None, resident=resident,
                      live="herc" if live else None)
    vid.verify_v88(out)
    return out


def snd_sym(name):
    """A symbol's offset in SOUND.DRV's image, off nasm's own map"""
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "s.asm"), os.path.join(d, "s.map")
        open(cp, "w").write(open("drivers/sound/sound.asm").read() +
                            "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I",
                        "drivers/sound/", "-I", "drivers/", "-I", "apps/",
                        "-o", os.path.join(d, "s.bin"), cp], check=True)
        for line in open(mp):
            f = line.split()
            if len(f) >= 3 and f[-1] == name:
                return int(f[1], 16)
    raise KeyError(name)


def button(m, ui, base, rw, rb, bad):
    """--button: the Mute button, clicked, on the desktop and mid-play"""
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))

    def until(cond, what, guest=20.0):
        os88marty.until(m, cond, what, poll=0.1, limit=300.0, guest=guest)

    def at():                       # (the rect is re-read: a play moves it)
        x1, y1, x2, y2 = struct.unpack(
            "<4H", m.read(base + syms["vp_brects"] + 5 * 8, 8))
        return (x1 + x2) // 2, (y1 + y2) // 2

    def click():
        ui.mo.click(*at())

    pressed = [0]

    def hold(cond, what, guest=20.0):
        """A press IN A BRACKET, held until the player has acted on it: the
        play polls the buttons between frames and a press is an edge
        between two polls, so a click shorter than a poll can fall between
        them - as it cannot for a hand, whose click is ~100 ms. [pressed]
        is the frames drawn AT the press: the pointer's trip to the button
        is guest time the play goes on through"""
        ui.mo.to(*at())
        ui.mo._sep()
        pressed[0] = rw("vp_done")
        ui.mo._edge(True)
        until(cond, what, guest)
        ui.mo._edge(False)
    click()
    until(lambda mm: rb("vp_mute") == 1, "a click to mute")
    lat = u16(m.read(base + syms["vp_bflags"] + 10, 2))
    click()
    until(lambda mm: rb("vp_mute") == 0, "a click to unmute")
    print("   on the desktop: muted, the button %s, and unmuted" %
          ("DOWN" if lat & 0x0020 else "up (flags %04x)" % lat))
    m.type_text("p")
    until(lambda mm: rb("vp_ready") == 1 and rb("vp_sopn") == 1,
          "the play, with its sound", 60.0)
    if rb("vp_winm") != 1:
        bad.append("the play is not in the window")
    until(lambda mm: rw("vp_done") >= 30, "a second of frames", 60.0)
    hold(lambda mm: rb("vp_snd") == 0 and rb("vp_sopn") == 0,
         "the click to mute the play")  # MUTED MID-PLAY: at once, and on
    d0 = rw("vp_done")
    c0 = int(m.status()["cycles"])
    until(lambda mm: rw("vp_done") > d0 + 15, "the play going on silent")
    fps = (rw("vp_done") - d0) / ((int(m.status()["cycles"]) - c0) /
                                  4772727.0)
    mid = (rb("vp_ready"), rb("vp_winm"))
    hold(lambda mm: rb("vp_mute") == 0, "the click to unmute the play")
    d1 = pressed[0]
    until(lambda mm: rb("vp_winm") == 1 and rb("vp_ready") == 1 and
          rb("vp_snd") == 1 and rb("vp_sopn") == 1,
          "the play again in the window, with its sound", 60.0)
    print("   mid-play: muted at frame %d, on silent at %.1f fps in the "
          "window %s; unmuted at %d, on again from frame %d with its sound"
          % (d0, fps, mid, d1, rw("vp_base")))
    if not 25 <= fps <= 35:
        bad.append("muted, the play ran at %.1f fps, not 30" % fps)
    if rw("vp_base") > d1:
        bad.append("unmuted, the play went on from frame %d, past %d"
                   % (rw("vp_base"), d1))
    if not lat & 0x0020:
        bad.append("the Mute button did not stand down")
    if mid != (1, 1):
        bad.append("muting stopped the play (ready %d, window %d)" % mid)
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("\n   ok")
    return 1 if bad else 0


def captured_bytes(path):
    """The card's bytes back out of MartyPC's capture: a sample-and-hold
    resample of each byte to the host rate, so a byte is a RUN of equal host
    samples. Runs collapse the same way in the reference, which is what the
    comparison is made on. tools/sndcheck.py's reader, because the capture's
    size fields may be left zero"""
    rate, vals, _ = sndcheck.load(path)
    return bytes(max(0, min(255, int(round(v * 128.0)) + 128))
                 for v in vals), rate


def runs(b):
    out = bytearray()
    last = None
    for x in b:
        if x != last:
            out.append(x)
            last = x
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--secs", type=int, default=60)
    ap.add_argument("--machine", default="os8088_5150_herc_hdd_sb_gla")
    ap.add_argument("--audio", choices=("pcm8", "adpcm4"), default="pcm8",
                    help="adpcm4: the card decodes it (DSP 7Dh), MartyPC's "
                    "by tools/martypc/patches/06")
    ap.add_argument("--pause", action="store_true",
                    help="Space for 2 guest seconds a third of the way in")
    ap.add_argument("--fs", action="store_true",
                    help="go in with F (paused, 98.3.6) and play with Space")
    ap.add_argument("--seek", type=int, default=0,
                    help="play from this keyframe (0 = the start)")
    ap.add_argument("--loop", type=int, metavar="L",
                    help="repeat, a seam back to frame L: two laps more")
    ap.add_argument("--resident", action="store_true",
                    help="the clip RESIDENT (98.1.7): its records one LZB "
                    "block, the sound one audio block, played from memory")
    ap.add_argument("--swap", action="store_true",
                    help="with --live: F a third of the way in, to the full "
                    "screen playing, and F back to the desktop, Live")
    ap.add_argument("--live", action="store_true",
                    help="the clip LIVE (98.3.10.1): resident, 160 x 60 on "
                    "LIN80 for the Hercules desktop, played on the desktop "
                    "by the worker with the card the clock")
    ap.add_argument("--dsp4", action="store_true",
                    help="the card made to answer DSP 4.xx (SPEC.md 98.3.17):"
                    " SOUND.DRV's [sbl_verhi] = 4 and SND_CAP_ADPCM4Q in the "
                    "caps, before the file opens - an ADPCM4 play must then "
                    "default to MUTED, and play nothing")
    ap.add_argument("--unmute", action="store_true",
                    help="with --dsp4: M before the play - it must then play "
                    "the sound whole, the open FORCED past the driver's "
                    "DSP 4.xx refusal")
    ap.add_argument("--button", action="store_true",
                    help="THE MUTE BUTTON (SPEC.md 98.3.17): clicked on the "
                    "desktop, on and off; then in a window play - the sound "
                    "off at once and the play going on, and again, the play "
                    "started again in the window WITH its sound")
    ap.add_argument("--rate", type=int,
                    help="the sound's rate (default 22,050; 11,025 Live) - "
                    "5512 is the encoder's half-size option (98.1.7.2)")
    a = ap.parse_args()
    global RATE
    if a.live:                          # (the audio block: under 60 KB)
        RATE = 11025
        a.resident = True
    if a.rate:
        RATE = a.rate
    nf = int(a.secs * FPS)
    os.chdir(ROOT)
    syms, image = pkg_syms("apps/video/video.asm", ("apps/",))
    if open(os88build.at("build/video.bin"), "rb").read() != image:
        sys.exit("vidsound: build/video.bin is behind the tree - run `make`")
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        afmt = vid.AUD_BY_NAME[a.audio]
        v88 = clip(tmp, nf, afmt, a.loop,
                   vid.PK_LZB if a.resident else None, a.live)
        r = vid.Reader(v88)
        base0 = r.keys[a.seek][0] + 1 if a.seek else 0
        audio = b"".join(rec[-r.abytes:] for rec, _, _ in r.records())
        laps = 2 if a.loop is not None else 0
        if laps:                        # every lap after the first: frame
            audio += audio[a.loop * r.abytes:] * laps   # L's sound on
        if afmt == vid.AUD_ADPCM4:      # what the card plays: the stream
            audio = vid.adpcm4_decode(audio)    # decoded from the reference
            audio = audio[base0 * r.spf:]   # - the CONTINUOUS stream's, from
                                            # where a seek starts (98.1.1.1)
        else:
            audio = audio[base0 * r.abytes:]    # from where the play starts
        vhd = os.path.join(tmp, "vidsound.vhd")
        subprocess.run(
            ["python3", "tools/os88hdd.py", "--template", TEMPLATE,
             "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
             "--vbr", os88build.at("build/boothd.bin"),
             "--mbr", os88build.at("build/mbr.bin"),
             "--file", "HDD.DRV=" + os88build.at("build/hdd.drv"),
             "--file", "HIBER.DRV=" + os88build.at("build/hiber.drv"),
             "--file", "CTRL.DRV=" + os88build.at("build/ctrl.drv"),
             "--file", "SOUND.DRV=" + os88build.at("build/sound.drv"),
             "--file", "VIDEO.O88=" + os88build.at("build/video.o88"),
             "--file", "CLIP.V88=" + v88], check=True, capture_output=True)
        cap = os.path.join(tmp, "cap")
        os.environ["MARTYPC_WAV"] = cap
        m = os88marty.launch(None, machine=a.machine,
                             extra=["--mount", "hd:0:" + vhd])
        try:
            ui = os88ui.UI(m)
            ui.ready(limit=240)
            if a.dsp4:                  # A CARD THAT ANSWERS 4.xx: the
                dseg = u16(m.read(m.sym("drv_tab") + 2, 2)) << 4   # driver's
                m.write(dseg + snd_sym("sbl_verhi"), b"\x04")  # row 0, and
                cp_ = m.sym("drv_svc")      # the caps the kernel answers with
                m.write(cp_, struct.pack("<H", u16(m.read(cp_, 2)) | 0x40))
            w = ui.path("C:/CLIP.V88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4

            def rw(n):
                return u16(m.read(base + syms[n], 2))

            def rb(n):
                return m.read(base + syms[n], 1)[0]

            os88marty.until(m, lambda mm: rb("vp_loaded") == 1,
                            "the clip's header", poll=0.5, limit=300.0,
                            guest=30.0)
            if rb("vp_ok") != 1:
                sys.exit("vidsound: the player will not play the clip here")
            if a.dsp4:                  # DEFAULTED TO MUTED, for the reason
                print("   DSP 4.xx: muted %d, why %d (1 = ADPCM4 on a DSP "
                      "4.xx)" % (rb("vp_mute"), rb("vp_mwhy")))
                if (rb("vp_mute"), rb("vp_mwhy")) != (1, 1):
                    bad.append("a DSP 4.xx did not default the play to muted")
                if a.unmute:            # ...and the user's to undo
                    m.type_text("m")
                    os88marty.until(m, lambda mm: rb("vp_mute") == 0,
                                    "M to unmute", poll=0.2, limit=120.0,
                                    guest=10.0)
            for _ in range(a.seek):             # Right to the keyframe
                n0 = rw("vp_sel")
                m.key("ArrowRight")
                os88marty.until(m, lambda mm: rw("vp_sel") == n0 + 1,
                                "Right to pick a key", poll=0.3,
                                limit=300.0, guest=30.0)
            if a.button:
                return button(m, ui, base, rw, rb, bad)
            m.write(base + syms["vp_played"], b"\0")
            m.type_text("f" if a.fs else "p")
            os88marty.until(m, lambda mm: rb("vp_ready") == 1,
                            "the play to start", poll=0.1, limit=300.0,
                            guest=60.0)
            if a.live and (rb("vp_lsess"), rb("vp_winm")) != (1, 0):
                bad.append("the play is not LIVE (session %d, bracket in "
                           "the window %d)" % (rb("vp_lsess"),
                                               rb("vp_winm")))
            if a.fs:                            # in PAUSED: the card waits
                os88marty.pace(m, 1.0)          # for the Space
                if rb("vp_upause") != 1 or rb("vp_sopn"):
                    bad.append("F went in %s, the card %s" % (
                        "paused" if rb("vp_upause") else "PLAYING",
                        "OPEN" if rb("vp_sopn") else "closed"))
                m.type_text(" ")
            c0 = int(m.status().get("cycles", 0))
            held = None
            if a.swap:                  # LIVE -> the full screen -> LIVE,
                at = nf // 3            # the card paused and resumed at each
                os88marty.until(m, lambda mm: rw("vp_done") >= at,
                                "frame %d" % at, poll=0.1, limit=600.0,
                                guest=a.secs + 30)
                m.type_text("f")
                os88marty.until(m, lambda mm: rb("vp_lrun") == 0 and
                                rb("vp_ready") == 1 and rb("vp_upause") == 0
                                and rb("vp_winm") == 0,
                                "F to the full screen, playing", poll=0.1,
                                limit=120.0, guest=10.0)
                f0 = rw("vp_done")
                os88marty.until(m, lambda mm: rw("vp_done") > f0 + 10,
                                "the full screen's frames", poll=0.1,
                                limit=120.0, guest=10.0)
                m.type_text("f")
                os88marty.until(m, lambda mm: rb("vp_lsess") == 1 and
                                rb("vp_lrun") == 1, "F back to live",
                                poll=0.1, limit=120.0, guest=10.0)
                print("   F at frame %d to the full screen, and F back at %d"
                      % (f0, rw("vp_done")))
            if a.pause:
                at = base0 + (nf - base0) // 3
                os88marty.until(m, lambda mm: rw("vp_done") >= at,
                                "frame %d" % at, poll=0.3, limit=600.0,
                                guest=a.secs + 30)
                m.type_text(" ")
                os88marty.until(m, lambda mm: rb("vp_upause") == 1,
                                "Space to pause", poll=0.1, limit=120.0,
                                guest=10.0)
                aseg = rw("vp_aseg") << 4
                os88marty.pace(m, 0.3)          # a block already under way
                d0, c0a = rw("vp_done"), u16(m.read(aseg + 16386, 2))
                os88marty.pace(m, 2.0)
                held = (rw("vp_done") - d0, u16(m.read(aseg + 16386, 2)) -
                        c0a)
                m.type_text(" ")
                os88marty.until(m, lambda mm: rb("vp_upause") == 0,
                                "Space to resume", poll=0.1, limit=120.0,
                                guest=10.0)

            def state():
                seg = rw("vp_aseg") << 4
                ctl = m.read(seg + 16384, 4) if seg else b"\0\0\0\0"
                return (" ".join("%s=%d" % (k, rw(k)) for k in (
                    "vp_done", "vp_afr", "vp_atot", "vp_alast", "vp_pause",
                    "vp_stall", "vp_skmax", "vp_syncf", "vp_pers", "vp_lc",
                    "vp_pc", "vp_afinal", "vp_apend")) +
                    " snd=%d end=%d aend=%d err=%d ctl=%d/%d" % (
                        rb("vp_snd"), rb("vp_end"), rb("vp_aend"),
                        rb("vp_err"), u16(ctl), u16(ctl, 2)))
            if laps:                    # into the last lap, then R: that
                last = nf + (laps - 1) * (nf - a.loop) + 5  # lap ends it
                # (polled FINELY: a lap of a short clip is a guest second,
                # and at 0.5 host s a poll the guest ran on two of them -
                # R then landed a lap late, one run in two)
                os88marty.until(m, lambda mm: rw("vp_vseq") >= last,
                                "frame %d counted" % last, poll=0.02,
                                limit=1200.0, guest=a.secs * 4 + 30)
                m.type_text("r")
                os88marty.until(m, lambda mm: rb("vp_rep") == 0,
                                "R to turn Repeat off", poll=0.2,
                                limit=120.0, guest=10.0)
            try:
                os88marty.until(m, lambda mm: rb("vp_ready") == 0,
                                "the play to end", poll=1.0, limit=1200.0,
                                guest=a.secs * (2 + 2 * laps) + 30)
            except os88marty.MartyError:
                print("   STUCK: " + state())
                raise
            c1 = int(m.status().get("cycles", 0))
            if a.live:                  # (no bracket to return from)
                os88marty.until(m, lambda mm: rb("vp_lsess") == 0,
                                "the live session to end", poll=0.5,
                                limit=120.0, guest=30.0)
            else:
                os88marty.until(m, lambda mm: rb("vp_played") == 1,
                                "the bracket to return", poll=0.5,
                                limit=120.0, guest=30.0)
            st = {k: rw(k) for k in (
                "vp_done", "vp_stall", "vp_late", "vp_pause", "vp_skmax", "vp_gap",
                "vp_afr", "vp_atot", "vp_alast", "vp_afinal", "vp_dt",
                "vp_base", "vp_ptk")}
            st["vp_vseq"] = rw("vp_vseq")
            st["vp_blk"] = rw("vp_blk")
            st["vp_aseq"] = rw("vp_aseq")
            st["vp_snd"] = rb("vp_snd")
            st["vp_err"] = rb("vp_err")
            st["vp_aend"] = rb("vp_aend")
        finally:
            m.close()
            os.environ.pop("MARTYPC_WAV", None)
        allcaps = sorted(glob.glob(cap + "*.wav"))
        caps = [c for c in allcaps if "blaster" in c.lower()
                or ".sb" in c.lower()]
        if a.dsp4 and not a.unmute:     # MUTED: the picture whole, and no
            heard = False               # sound run at all
            if caps:
                got, _ = captured_bytes(caps[0])
                heard = runs(got).find(runs(audio)[:64]) >= 0
            print("   muted: drew %d of %d, the sound %s, the clip's sound "
                  "%s in the capture" % (st["vp_done"], nf,
                                         "OPEN" if st["vp_snd"] else "off",
                                         "IS" if heard else "is not"))
            if st["vp_snd"] or heard or st["vp_done"] != nf:
                bad.append("the muted play was not silent and whole")
            for b in bad:
                print("   FAIL: %s" % b)
            if not bad:
                print("\n   ok")
            return 1 if bad else 0

        secs = st["vp_dt"] * 65536 / 1193182.0     # the player's own ticks
        # (the host polls the end once a second, so a cycle count taken
        # there can overshoot by seconds of guest time)
        real = 1000000.0 / (256 - (256 - 1000000 // RATE))   # the card's rate
        bps = r.abytes / float(r.spf)                   # bytes a sample
        nplay = nf - base0 + laps * (nf - (a.loop or 0))   # every lap's
        want_s = (r.spf * nplay) / real + st["vp_blk"] / bps / real  # ...and the
                                                        # drain, to a block's end
        print("\n   %d frames, %d bytes of %s sound a frame at %d Hz (the "
              "card plays %.0f)" % (nf, r.abytes, a.audio.upper(), RATE, real))
        print("   drew %d, stalls %d, late %d, pauses %d, trailed the sound "
              "by at most %d frames; sound queued for %d frames"
              % (st["vp_done"], st["vp_stall"], st["vp_late"],
                 st["vp_pause"], st["vp_skmax"], st["vp_afr"]))
        print("   the play took %.2f s of guest time; the sound is %.2f s; "
              "the hook was held off at most %d periods"
              % (secs, want_s, st["vp_gap"]))
        if a.seek:
            print("   from keyframe %d: the play started at frame %d"
                  % (a.seek, st["vp_base"]))
            if st["vp_base"] != base0:
                bad.append("the play started at frame %d, not %d"
                           % (st["vp_base"], base0))
        if held is not None:
            print("   paused %d ticks: %d frames drawn and %d bytes of sound "
                  "consumed in 2 guest seconds of it"
                  % (st["vp_ptk"], held[0], held[1] & 0xFFFF))
            if held[0] or held[1] & 0xFFFF:
                bad.append("the pause drew %d frames and played %d bytes"
                           % (held[0], held[1] & 0xFFFF))
            if st["vp_ptk"] < 36:
                bad.append("only %d ticks counted as paused" % st["vp_ptk"])
        if not st["vp_snd"]:
            bad.append("the play was SILENT: the card was not opened")
        blk = venc.audio_block(afmt, RATE)  # THE CARD'S BLOCK (98.3.1,
        print("   the card's block: %d bytes (want %d)" % (st["vp_blk"], blk))
        if st["vp_blk"] != blk:             # 34.5.3): the rate's, which the
            bad.append("the card's block was %d bytes, not %d"  # driver
                       % (st["vp_blk"], blk))   # must have said it takes
        if st["vp_err"]:
            bad.append("the play stopped on an error")
        if st["vp_done"] != nf:
            bad.append("drew %d of %d" % (st["vp_done"], nf))
        if laps:
            print("   %d frames counted over %d laps after the first (want "
                  "%d)" % (st["vp_vseq"], laps, nplay))
            if st["vp_vseq"] != nplay:
                bad.append("%d frames counted, not %d" % (st["vp_vseq"],
                                                        nplay))
        if st["vp_stall"]:
            bad.append("%d stalls: the reader fell behind" % st["vp_stall"])
        if st["vp_pause"]:
            bad.append("the card ran dry %d times" % st["vp_pause"])
        # (LIVE draws once a tick - 55 ms, 1.65 frames at 30 fps - so up to
        # VP_LCAP = 4 due at a pass is its design, where a bracket's is 2;
        # and a UI callback holding the lock - a key, a repaint - can hold a
        # pass off a tick, which is 5 or 6 once, caught up at 4 a pass. The
        # sound is never held: the card plays on)
        if st["vp_skmax"] > (6 if a.live else 2) or \
                st["vp_late"] > (2 if a.live else 0):
            bad.append("the picture trailed the sound (max %d, late %d)"
                       % (st["vp_skmax"], st["vp_late"]))
        wrapped = laps and (st["vp_afr"] < nf or
                            st["vp_aseq"] > st["vp_vseq"])
        # the sound had queued a lap that R then took away (98.3.9): the play
        # stops at the frames drawn, and the capture below holds exactly
        # theirs. The sound's frames past the picture's say so whatever lap
        # it was in: ADPCM4 is half the bytes, so the ring holds twice the
        # frames ahead, and it had queued a WHOLE lap to the file's end
        # (vp_afr = nf) before R - which vp_afr alone read as no lap at all
        if not wrapped and (st["vp_aend"] != 1 or
                            ((st["vp_alast"] - st["vp_afinal"]) & 0x8000)):
            bad.append("the sound did not play out to its last byte "
                       "(aend %d, consumed %d, last %d)"
                       % (st["vp_aend"], st["vp_alast"], st["vp_afinal"]))
        if abs(secs - want_s) > want_s * 0.02 + 0.5:
            bad.append("the play took %.2f s where the sound takes %.2f"
                       % (secs, want_s))
        # --- the sound itself
        if not caps:
            bad.append("no Sound Blaster capture among %s" % allcaps)
        else:
            got, hrate = captured_bytes(caps[0])
            gr, wr = runs(got), runs(audio)
            at = gr.find(wr[:64])
            print("   capture: %s, %d Hz, %d host samples" %
                  (os.path.basename(caps[0]), hrate, len(got)))
            if at < 0:
                bad.append("the clip's first sound is not in the capture")
            elif gr[at:at + len(wr)] != wr:
                n = next((i for i in range(min(len(wr), len(gr) - at))
                          if gr[at + i] != wr[i]), len(gr) - at)
                bad.append("the captured sound departs from the file's "
                           "%d distinct samples in (of %d)" % (n, len(wr)))
            else:
                print("   the capture holds the clip's %d samples of sound "
                      "whole and in order" % len(audio))
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("\n   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
