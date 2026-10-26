#!/usr/bin/env python3
"""waverackplay - SPEC.md 122.6, 122.8, 122.9: WaveRack PLAYS what the
reference says it must, and gives everything back.

    make && make waverackfix build/mixercfg/SYSTEM.CFG &&
        python3 tests/waverackplay.py [--keep]
         [--leg rates|d2]

QEMU BY NAME, and for what QEMU is exact about: the BYTES. The card is
QEMU's SB16, its output captured by the `wav` audiodev at the rate the card
really plays (SOUND.DRV's time constant, which QEMU rounds back - 22,050 Hz is
22,222 on the glass and in the capture alike), so QEMU's mixer resamples
nothing and an 8-bit stream's capture is read back into the card's own bytes
(tests/xtmp3live.py's measured widening). Each play is a SLICE of the capture,
marked by the file's size before and after, and held to tools/wrsim.py's
convert() of the same frames - sample for sample where the rate is the
capture's, by its dominant frequency and its length where QEMU resamples:

  rates  one boot, an SB16 (SND_CAP_PCM_HI: d = 1 to 44,100 Hz):
         STEREO16.WAV (22,050 Hz 16-bit stereo: the high byte, the average,
           VOLUME 75%) played to its end - every sample; the status bar's
           output line, the LEVEL PLAY caption, the meter's lit cells for
           its tone (wrart_db of the half's peak) and PEAK at the spike (a
           16-bit rail); no underrun; the claims given back
         a SELECTION dragged with the mouse in the waveform, against the drag
           arithmetic; Loop on and played: paused after two laps and more -
           the position from verb 9's count held to wrsim.play_pos, the
           capture the selection's bytes REPEATED, every sample; resumed;
           Stop - the playhead back where the play began, the cursor and
           the selection where they were, every claim and the grant back
         TONE8.WAV (11,025 Hz) and TONE8K.WAV (8,000 Hz, 16-bit): their
           dominant frequency and length in the capture
         the window CLOSED mid-play: no claim of its region left, and a
           relaunch opens the card again (the stream was closed)
  d2     one boot, a DSP 2.01 (no PCM_HI: the ceiling 22,222 Hz): TONE44.WAV
         (44,100 Hz 8-bit stereo) at d = 2 - 22,050 Hz, the box filter of
         two frames - with VOLUME at 50% from the keys: every sample, and
         'Out 22,050 Hz mono /2'

Break on purpose: drop the `rcr al, 1` in wt_cv16s and STEREO16 fails at its
first sample; take the loop's wrap out of wt_refill and the loop leg's laps
are not there; count wt_played without d and the d2 leg's length and the
loop's position both fail.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
import numpy as np                                            # noqa: E402
import os88build                                              # noqa: E402
import os88geom as geom                                       # noqa: E402
import os88qemu                                               # noqa: E402
import os88ui                                                 # noqa: E402
import os88wrart                                              # noqa: E402
import waverack as WRT                                        # noqa: E402
import wrsim                                                  # noqa: E402
import xmcheck                                                # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402
from vidxms import Q                                          # noqa: E402

check, say, until, u32 = WRT.check, WRT.say, WRT.until, WRT.u32
FAIL = WRT.FAIL
WT_IDLE, WT_PLAY, WT_PPAUSE = 0, 1, 2
DB = os88wrart.db_table()

# QEMU 11's mixer widens an unsigned 8-bit sample u to (u - 127) x 257,
# clipped - tests/xtmp3live.py measured it - so a capture reads back into
# the card's bytes exactly
U8WIDE = np.clip((np.arange(256) - 127) * 257, -32768, 32767)
U8BACK = {int(U8WIDE[u]): u for u in range(256)}


def tc_rate(rate):
    """the rate QEMU's SB16 plays SOUND.DRV's time constant at: the driver
    truncates 1e6 / rate, QEMU rounds 1e6 / (256 - TC)"""
    t = 1000000 // rate
    return (1000000 + t // 2) // t


class PR(WRT.WR):
    """WaveRack on a private QEMU with an SB16 whose output is captured"""

    def __init__(self, tmp, syms, img, sbopt="", afreq=22222):
        self.syms, self.img = syms, img
        sysimg = os.path.join(tmp, "sys.img")
        bimg = os.path.join(tmp, "b.img")
        self.wav = os.path.join(tmp, "out.wav")
        shutil.copy(os88build.at("build/os8088.img"), sysimg)
        subprocess.run([sys.executable, "tools/os88fat.py", "add", sysimg,
                        os88build.at("build/mixercfg/SYSTEM.CFG")],
                       check=True, capture_output=True)  # SOUND.DRV wanted
        shutil.copy(os88build.at("build/wrfix.img"), bimg)
        self.q = Q(tmp, sysimg, bimg, 4, extra=[
            "-device", "sb16,audiodev=a0" + sbopt, "-audiodev",
            "wav,id=a0,path=%s,out.frequency=%d,out.channels=2,"
            "out.format=s16" % (self.wav, afreq)])
        self.sock = self.q.sock
        os88qemu.acted(self.q, lambda: os.path.exists(self.sock), secs=30,
                       what="the QMP socket")
        xmcheck.wait_desktop(self.sock, "waverackplay")
        self.ui = os88ui.UI(self.q, mouse=WRT.Mo(self.sock), sym=xmcheck.sym,
                            verbose=False)
        self.seg = None

    def mark(self):
        """frames the capture holds so far"""
        try:
            return max(0, (os.path.getsize(self.wav) - 44) // 4)
        except OSError:
            return 0

    def capture(self, f0, f1=None):
        """channel 0 of the capture's frames [f0, f1), as the card's bytes
        (-1 where no byte widens to the value)"""
        raw = open(self.wav, "rb").read()[44:]
        cap = np.frombuffer(raw[:len(raw) // 4 * 4], dtype="<i2")
        cap = cap.reshape(-1, 2)[f0:f1, 0]
        vals, inv = np.unique(cap, return_inverse=True)
        back = np.array([U8BACK.get(int(v), -1) for v in vals], dtype=np.int16)
        return back[inv], cap

    def state(self):
        return self.b("wt_state")

    def dsel(self):
        return self.d("wo_s"), self.d("wo_e")

    def play_to_end(self, what, secs=60):
        """Space, the play seen, then its end - sampling the meter and the
        status line on the way. -> (capture slice start, what was seen)"""
        f0 = self.mark()
        seen = dict(lit=set(), peak=0, mwho=set(), stat=set(), heap=None)
        self.key("spc")
        if not until(lambda: self.state() == WT_PLAY, what + " playing", 15,
                     0.05):
            return f0, seen
        t = time.time()
        while time.time() - t < secs and self.state() == WT_PLAY:
            seen["lit"].add(self.b("wr_lit"))
            seen["peak"] |= self.b("wr_peak")
            seen["mwho"].add(self.b("wr_mwho"))
            seen["stat"].add(self.text("wr_stbuf"))
            if seen["heap"] is None:
                seen["heap"] = self.heap()
            time.sleep(0.03)
        time.sleep(1.0)                 # (the capture's own buffering)
        return f0, seen

    def wpoint(self, col):
        """the screen point of waveform column COL, mid-lane"""
        x = self.w("wd_ox") + self.w("we_bx") + col
        y = (self.w("wd_oy") + self.w("we_by") + self.w("we_rulh") +
             self.w("we_lh") // 2)
        return x, y


def arr(b):
    """bytes (wrsim.convert's) as an int16 array, the capture's type"""
    return np.frombuffer(bytes(b), dtype=np.uint8).astype(np.int16)


def found(cap, want, lo=0):
    """the offset in CAP (the card's bytes) where WANT begins, searched by a
    48-byte probe from its first loud stretch. None when it is nowhere"""
    w = arr(want)
    loud = np.nonzero(np.abs(w.astype(np.int32) - 128) > 8)[0]
    if not len(loud):
        return None
    j0 = int(loud[0])
    probe = w[j0:j0 + 48]
    for h in np.nonzero(cap == probe[0])[0]:
        h = int(h)
        if h - j0 >= lo and np.array_equal(cap[h:h + len(probe)], probe):
            return h - j0
    return None


def held(what, cap, want, tail=0.1, rate=22222):
    """the capture holds WANT, every sample (QEMU drops what its mixer still
    holds when a stream closes: at most TAIL s short at the end)"""
    o = found(cap, want)
    if not check(what + ": found in the capture", o is not None,
                 "%d captured, %d wanted" % (len(cap), len(want))):
        return None
    n = min(len(want), len(cap) - o)
    w = arr(want[:n])
    diff = np.nonzero(cap[o:o + n] != w)[0]
    more = "%d of %d samples at capture %d" % (n, len(want), o)
    if len(diff):
        i = int(diff[0])
        more += "; %d differ, the first at %d: %d vs %d" % (
            len(diff), i, cap[o + i], w[i])
    check(what + ": every sample", not len(diff) and
          len(want) - n <= rate * tail, more)
    return o


def dominant(x, rate):
    """the dominant frequency of the loud stretch of int16 samples X, and
    that stretch's length in seconds"""
    x = x.astype(float)
    loud = np.nonzero(np.abs(x) > 2000)[0]
    if len(loud) < 64:
        return None, 0.0
    seg = x[loud[0]:loud[-1] + 1]
    seg = seg - seg.mean()
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    k = int(np.argmax(spec[1:])) + 1
    return k * rate / len(seg), len(seg) / rate


FX = {}


def reference(name):
    if not FX:
        FX.update(wrsim.fixtures())
    data = FX[name][0]
    r = wrsim.parse(data)
    return data, r, wrsim.mono(data, r)


# =============================================================================
# THE LEGS
# =============================================================================
def leg_rates(syms, img, tmp):
    say("waverackplay: rates - an SB16 (PCM_HI), captured at 22,222 Hz")
    pr = PR(tmp, syms, img, afreq=22222)
    try:
        if not check("WaveRack launches off B:", pr.launch()):
            return
        time.sleep(1)
        idle0 = pr.heap()
        say("     heap, idle, no document: %s KB" % idle0)
        # --- STEREO16, to its end, every sample ---------------------------------
        if not check("STEREO16.WAV opens", pr.open("MEDIA/GOOD/STEREO16.WAV")):
            return
        pr.settled()
        idle = pr.heap()
        say("     heap, idle with it open: %s KB = %d KB" % (idle, sum(idle)))
        data, r, m = reference("STEREO16.WAV")
        f0, seen = pr.play_to_end("STEREO16")
        f1 = pr.mark()
        check("STEREO16: d = 1 at 22,050 Hz (SND_CAP_PCM_HI)",
              (pr.b("wt_d"), pr.w("wt_out")) == (1, 22050),
              "d %d out %d" % (pr.b("wt_d"), pr.w("wt_out")))
        check("STEREO16: the status said 'Out 22,050 Hz mono'",
              "Out 22,050 Hz mono" in seen["stat"], repr(seen["stat"]))
        check("STEREO16: the meter's caption was LEVEL  PLAY (wr_mwho 2)",
              2 in seen["mwho"], repr(seen["mwho"]))
        want_lit = DB[24000 >> 8]       # L's tone: |s| >> 8 of 24,000
        check("STEREO16: the meter lit %d cells for its tone (wrart_db[%d])"
              % (want_lit, 24000 >> 8), want_lit in seen["lit"],
              "saw %s" % sorted(seen["lit"]))
        check("STEREO16: PEAK lit at R's spike (32767, a rail)",
              seen["peak"] == 1, "never")
        say("     heap, playing: %s KB = %d KB" % (seen["heap"],
                                                  sum(seen["heap"] or [])))
        check("STEREO16: ended IDLE, no underrun, 'End of the sound'",
              pr.state() == WT_IDLE and pr.w("wt_nunder") == 0 and
              pr.text("wr_stbuf") == "End of the sound",
              "state %d under %d '%s'" % (pr.state(), pr.w("wt_nunder"),
                                          pr.text("wr_stbuf")))
        check("STEREO16: every claim given back (heap %s KB)" % pr.heap(),
              pr.heap() == idle and pr.b("wt_ghave") == 0 and
              pr.b("wt_sopen") == 0, "want %s" % idle)
        cap, _ = pr.capture(f0, f1)
        held("STEREO16 at 75%", cap, wrsim.convert(m, 1, 75))
        leg_loop(pr, m, idle)
        # --- TONE8 and TONE8K: their frequency and length ------------------------
        for name, rate, hz, secs in (("TONE8.WAV", 11025, 1000, 1.0),
                                     ("TONE8K.WAV", 8000, 500, 1.0)):
            if not check("%s opens" % name, pr.open("MEDIA/GOOD/" + name)):
                continue
            pr.settled()
            f0, seen = pr.play_to_end(name)
            f1 = pr.mark()
            tc = tc_rate(rate)
            _, raw = pr.capture(f0, f1)
            f, ln = dominant(raw, 22222)
            want_f = hz * tc / rate     # QEMU plays the time constant's rate
            check("%s: %d Hz on the card (d = 1), status '%s'" % (
                name, pr.w("wt_out"), pr.text("wr_stbuf")),
                pr.w("wt_out") == rate and pr.b("wt_d") == 1)
            check("%s: the capture's tone %.1f Hz, want %.1f (+-2%%)" % (
                name, f or 0, want_f), f and abs(f - want_f) < want_f * 0.02)
            check("%s: %.3f s long, want %.3f (+-0.06)" % (
                name, ln, secs * rate / tc), abs(ln - secs * rate / tc) < 0.06)
        # --- closed mid-play: nothing left, and the card free --------------------
        leg_close(pr)
    finally:
        pr.q.close()


def leg_loop(pr, m, idle):
    """a selection dragged, looped, paused, resumed, stopped"""
    say(" loop: a selection dragged in the waveform, looped")
    ww, z, first = pr.w("we_ww"), pr.b("wv_z"), pr.d("wv_first")
    c0, c1 = ww // 5, ww // 5 + ww // 4
    xa, y = pr.wpoint(c0)
    xb, _ = pr.wpoint(c1)
    WRT.mouse(pr.sock, "to", xa, y)
    time.sleep(0.3)
    WRT.mouse(pr.sock, "down", xa, y)
    time.sleep(0.3)
    for x in range(xa, xb + 1, max(1, (xb - xa) // 6)):
        WRT.mouse(pr.sock, "to", x, y)
        time.sleep(0.1)
    WRT.mouse(pr.sock, "to", xb, y)
    time.sleep(0.3)
    WRT.mouse(pr.sock, "up")
    time.sleep(0.5)
    fpp = 1 << z
    want = (first + c0 * fpp, min(first + c1 * fpp + fpp, pr.d("wo_frames")))
    s, e = pr.dsel()
    check("the drag selects [%d, %d): columns %d..%d at fpp %d" % (
        s, e, c0, c1, fpp), (s, e) == want, "want %r" % (want,))
    check("the cursor stands at s", pr.d("wo_cur") == s,
          "cursor %d" % pr.d("wo_cur"))
    pr.key("l")
    until(lambda: pr.b("wr_loop") == 1, "Loop on", 5)
    lap = wrsim.convert(m[s:e], 1, 75)
    f0 = pr.mark()
    pr.key("spc")
    until(lambda: pr.state() == WT_PLAY, "the loop playing", 15, 0.05)
    t = (e - s) / 22050.0
    time.sleep(max(2.0, 2.6 * t + 0.6))
    pr.key("spc")
    until(lambda: pr.state() == WT_PPAUSE, "paused", 10, 0.05)
    time.sleep(1.0)
    f1 = pr.mark()
    n, d = pr.d("wt_played"), pr.b("wt_d")
    sp, e1 = pr.d("wt_sp"), pr.d("wt_e1")
    ph = pr.d("wt_ph")
    want_ph = wrsim.play_pos(n * d, s, e, s, e, True)
    check("paused after %.1f laps: %d samples played (verb 9)" % (
        n * d / float(e - s), n), n * d > 2 * (e - s), "fewer than two laps")
    check("the region and the lap: sp %d e1 %d = [s, e)" % (sp, e1),
          (sp, e1, pr.d("wt_r0"), pr.d("wt_r1")) == (s, e, s, e))
    check("the paused position %d = wrsim.play_pos(%d x %d) = %d" % (
        ph, n, d, want_ph), ph == want_ph and pr.d("wt_ppos") == ph)
    cap, _ = pr.capture(f0, f1)
    o = found(cap, lap)
    k = 0
    if o is not None:
        while o + (k + 1) * len(lap) <= len(cap) and np.array_equal(
                cap[o + k * len(lap):o + (k + 1) * len(lap)], arr(lap)):
            k += 1
    check("the capture holds the selection REPEATED, every sample: %d whole "
          "laps of %d" % (k, len(lap)), k >= 2,
          "found at %r" % o)
    pr.key("spc")
    ok = until(lambda: pr.state() == WT_PLAY, "resumed", 10, 0.05)
    check("Space resumes it", ok)
    time.sleep(0.6)
    pr.key("s")
    until(lambda: pr.state() == WT_IDLE, "stopped", 10, 0.05)
    time.sleep(0.5)
    check("Stop: the playhead back where the play began (%d)" % s,
          pr.d("wt_ph") == s and pr.d("wt_p0") == s,
          "ph %d p0 %d" % (pr.d("wt_ph"), pr.d("wt_p0")))
    check("Stop: the cursor and the selection where they were",
          pr.d("wo_cur") == s and pr.dsel() == (s, e),
          "cursor %d sel %r" % (pr.d("wo_cur"), pr.dsel()))
    check("Stop: every claim and the grant back (heap %s KB)" % pr.heap(),
          pr.heap() == idle and pr.b("wt_ghave") == 0 and
          pr.b("wt_sopen") == 0 and pr.w("wt_laseg") == 0)
    pr.key("l")                         # Loop off again
    until(lambda: pr.b("wr_loop") == 0, "Loop off", 5)


def leg_close(pr):
    say(" close: the window closed mid-play")
    if not check("STEREO16.WAV opens", pr.open("MEDIA/GOOD/STEREO16.WAV")):
        return
    pr.settled()
    pr.key("spc")
    until(lambda: pr.state() == WT_PLAY, "playing", 15, 0.05)
    seg = pr.seg
    w = pr.window()
    cx, cy = geom.close_xy(w.x, w.y)
    xmcheck.click(pr.sock, cx, cy)
    check("the window closes", until(lambda: pr.wrseg()[0] is None,
                                      "the close", 15))
    time.sleep(1)
    left = pr.heap()                    # (claims still owned by its segment)
    check("no claim of the closed region left (%s)" % left, left == [])
    pr.seg = None
    if not check("WaveRack launches again", pr.launch()):
        return
    time.sleep(1)
    if not check("STEREO16.WAV opens again", pr.open("MEDIA/GOOD/STEREO16.WAV")):
        return
    pr.settled()
    pr.key("spc")
    ok = until(lambda: pr.state() == WT_PLAY, "playing again", 15, 0.05)
    check("the card opens again - the old stream was closed (%s)" %
          pr.text("wr_stbuf"), ok)
    pr.key("s")
    until(lambda: pr.state() == WT_IDLE, "stopped", 10, 0.05)
    del seg


def leg_d2(syms, img, tmp):
    say("waverackplay: d2 - a DSP 2.01 (no PCM_HI), captured at %d Hz"
        % tc_rate(22050))
    pr = PR(tmp, syms, img, sbopt=",version=0x0201", afreq=tc_rate(22050))
    try:
        if not check("WaveRack launches off B:", pr.launch()):
            return
        time.sleep(1)
        if not check("TONE44.WAV opens", pr.open("MEDIA/GOOD/TONE44.WAV")):
            return
        pr.settled()
        for _ in range(5):
            pr.key("bracket_left")
            time.sleep(0.15)
        check("VOLUME 50%% from the keys (wr_vol %d)" % pr.b("wr_vol"),
              until(lambda: pr.b("wr_vol") == 50, "volume 50", 5))
        data, r, m = reference("TONE44.WAV")
        f0, seen = pr.play_to_end("TONE44")
        f1 = pr.mark()
        check("TONE44: d = 2 at 22,050 Hz (the ceiling 22,222)",
              (pr.b("wt_d"), pr.w("wt_out")) == (2, 22050),
              "d %d out %d" % (pr.b("wt_d"), pr.w("wt_out")))
        check("TONE44: the status said 'Out 22,050 Hz mono /2'",
              "Out 22,050 Hz mono /2" in seen["stat"], repr(seen["stat"]))
        check("TONE44: ended, no underrun", pr.state() == WT_IDLE and
              pr.w("wt_nunder") == 0, "under %d" % pr.w("wt_nunder"))
        n = pr.d("wt_played")
        check("TONE44: %d samples played (verb 9) - the frames / 2, padded "
              "to a half" % n, n >= len(m) // 2 and n - len(m) // 2 < 2048,
              "want %d" % (len(m) // 2))
        cap, _ = pr.capture(f0, f1)
        held("TONE44 at d = 2, 50%", cap, wrsim.convert(m, 2, 50))
    finally:
        pr.q.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true",
                    help="leave the scratch folders (the captures) behind")
    ap.add_argument("--leg", action="append", choices=("rates", "d2"))
    a = ap.parse_args()
    for p in ("build/os8088.img", "build/wrfix.img", "build/waverack.bin",
              "build/mixercfg/SYSTEM.CFG"):
        if not os.path.exists(os88build.at(p)):
            sys.exit("waverackplay: no %s - make && make waverackfix "
                     "build/mixercfg/SYSTEM.CFG" % p)
    syms, img = pkg_syms("apps/waverack/waverack.asm",
                         ("apps/", "apps/waverack/", os88build.at("build") + "/"))
    if img != open(os88build.at("build/waverack.bin"), "rb").read():
        sys.exit("waverackplay: build/waverack.bin is not this source's - make")
    for name, leg in (("rates", leg_rates), ("d2", leg_d2)):
        if a.leg and name not in a.leg:
            continue
        tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"), prefix="wrp")
        try:
            leg(syms, img, tmp)
        finally:
            if a.keep:
                say("waverackplay: kept %s" % tmp)
            else:
                shutil.rmtree(tmp, ignore_errors=True)
    for f in FAIL:
        say("  FAIL: " + f)
    say("waverackplay: %s" % ("FAILED (%d)" % len(FAIL) if FAIL else "ok"))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
