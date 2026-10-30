#!/usr/bin/env python3
"""megamod - a module and a wavetable bank over 64KB, played under kern_mega
(KERN-MEGA-MEMORY-PLAN MC7: Tracker, ModPlug and MIDIRack).

    make && make mega && make modplug && python3 tests/megamod.py [--keep]
        [--leg NAME ...] [--pkg NAME=PATH ...] [--bank PATH]

Under kern_mega's protected mode every segment a package holds is an LDT
SELECTOR: a module claim's value names a descriptor, so `blobseg + N` - the
128-byte granule sums the three players addressed a module or a bank past
its first 64KB with - names some OTHER descriptor, and a claim's selector
reaches only its first 64KB. Each player now makes OSAPI_SEG SEG_SUB windows
once at load (a real-mode kernel answers them with the very sum it replaced),
and this row plays each one on kern_mega under Bochs with a Sound Blaster 16:

  trk     the SHIPPED TRACKER.O88 on BEVERLY.MOD (116,085 bytes: its samples
          run to 113KB, so most are past the first 64KB), opened from B:'s
          listing: no `kern_mega FAULT` or `CLOSED` line, the module loaded
          and playing, the song position moving
  wide    the shipped TRACKER.O88 on a GENERATED module of 130 patterns
          whose orders play 0, 64, 128, 1, 65, 129 - so three of its five
          pattern windows are read - and whose samples sit past 128KB. Each played
          pattern p sets channel 0's volume to 2 + p % 61 on its row 0, so the
          volume the replayer holds while it says it is on pattern p is read
          back out of the guest: a window that names the wrong 32 patterns
          plays another pattern's row 0, and the pairs disagree
  trkarm  the kern_mega ARM's TRACKER.O88 (build/megak/apps/) on BEVERLY.MOD
  mpp     MODPLUG.O88 (retired: `make modplug`) on BEVERLY.MOD, through its
          own L and the chooser
  mr      the SHIPPED MIDIRACK.O88 on a wavetable bank over 64KB - a synthetic
          one PADDED so its playing samples sit past 120KB (or --bank, the
          real MIDIRACK.BNK `make midibank` builds) - with BATTLE1.MID in
          B:\\MEDIA\\MIDI, the wavetable chosen and Space pressed: the bank
          read with no fault, a window every 32KB based where it says, and
          the pre-roll's note-ons placed through a late window
  mrsb    the shipped MIDIRACK.O88 on its SYNTH output (MR_OUT_SB) - the same
          external ring, no bank: the card's half only
  mrarm   the kern_mega ARM's MIDIRACK.O88 the same way as mr

Every pattern and sample window is checked EXACTLY - its descriptor's base,
out of the LDT, against the blob's plus the offset the file says.

The Tracker and ModPlug legs also read OUT/sb.wav, what the card played:
AUDIBLE for most of the play (50 ms blocks above an RMS floor) and NOT NOISE
(the median spectral flatness of 4,096-sample frames, 100 Hz to 4 kHz: white
noise reads ~0.56 and these modules ~0.01). Bochs's SB16 drains its DMA
faster than the rate it is programmed for (`dmatimer`), so the replayer runs
ahead of the music's tempo here (~1.5x on the guest's clock; ~10x when Bochs
ran `sync=realtime`): the capture is music, but not in time. The
MIDIRack legs read the same capture, and before it that the card's stream
OPENED on MIDIRack's external ring and that the block IRQ's consumed count
in that ring moves - a song that plays out inside the listen (the fast DMA
again) is a stop with the list's end as its reason, and passes.

THE FAILING CASE (docs/WRITING-TESTS.md 1): the packages from before this
change (`--pkg TRACKER=old.o88`) - the shipped Tracker's granule sum lands
on a descriptor that is not the module's and the first row reads a #GP: the
leg fails on `kern_mega CLOSED TRACKER 0D`. The same for MIDIRack (`--src`
names the tree an old build came from, for its symbols): `kern_mega CLOSED
MIDIRack 0D` at Space. Verified when the row was written. And the KERNEL
from before the stream door asked the descriptor's base (kern_mega's
osapi_snd_stream tested DI's TI bit, which every claim has since MC7.4):
the mr legs fail on `the card's stream open ... (SOUND.DRV said 2)`.

It reads build/ (wants=) and writes only under build/bochs-megamod/.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "megak")
import os88bochs  # noqa: E402
import os88sym  # noqa: E402

BUILD = os.path.join(ROOT, "build")
# A DISPLAY OF ITS OWN when the suite has not handed it one: another session's
# Bochs on the default :99 would take every click
DISPLAY = (None if os.environ.get("OS88_BOCHS_DISPLAY")
           else os88bochs.take_display())
PLAY = 14.0                     # seconds of music a leg listens to
FAILS = []
SRC = ROOT                      # --src: the tree the --pkg builds came from

# the disk's listing, B:'s root - as Box.megarun's rows, 16 pixels apart
ROW0_Y, ROW_X, ROW_H = 123, 170, 16

# Tracker's and ModPlug's channel record: MP_VOL / MPM_VOL (the volume a Cxx
# set) and the record size; the channel 0 record is the table's first
MP_VOL, MP_CHSZ = 22, 40
MR_OUT_WT = 6
MR_OUT_SB = 3                   # MR_OUT_SB: the synth, mixed for the card
MRB_RL = 16384                  # MIDIRack's external ring (apps/midirack/
                                # mrout.inc); SND_EXT_CONS is the word at +2


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)
    return cond


# --- the generated module ----------------------------------------------------
WIDE_ORDERS = (0, 64, 128, 1, 65, 129)
WIDE_P = 130


def wide_vol(p):
    return 2 + p % 61


def wide_mod():
    """130 patterns, the orders above, four 16KB looped samples after them."""
    def cell(smp, per, eff=0, prm=0):
        return bytes(((smp & 0xF0) | (per >> 8), per & 0xFF,
                      ((smp & 0x0F) << 4) | eff, prm))
    notes = (428, 381, 339, 285, 254, 226, 214, 190)
    out = bytearray(b"os8088 MC7 windows".ljust(20, b"\0"))
    slen = 16384
    for i in range(31):
        out += ("s%d" % (i + 1)).encode().ljust(22, b"\0")
        if i < 4:
            out += struct.pack(">HBBHH", slen // 2, 0, 64, 0, slen // 2)
        else:
            out += struct.pack(">HBBHH", 0, 0, 0, 0, 1)
    out += bytes((len(WIDE_ORDERS), 0x7F))
    out += bytes(WIDE_ORDERS) + bytes(128 - len(WIDE_ORDERS))
    out += b"M.K."
    for p in range(WIDE_P):
        for row in range(64):
            for ch in range(4):
                if ch == 0 and row == 0:            # the marker: Cxx, once
                    out += cell(1 + p % 4, notes[p % 8], 0xC, wide_vol(p))
                elif ch == 3 and row == 0:          # speed 3: 60 ms a row
                    out += cell(0, 0, 0xF, 3)
                elif ch in (1, 2) and row % 8 == 0:
                    out += cell(1 + (row // 8 + ch) % 4,
                                notes[(row // 8 + p + ch) % 8])
                else:
                    out += cell(0, 0)
    for k in range(4):                      # saw, square, triangle-ish, saw/2
        per = 32 << (k & 1)
        out += bytes((((i % per) * 256 // per - 128) * (1 if k < 2 else 1)
                      & 0xFF) if k != 1 else (0x60 if i % per < per // 2
                                               else 0xA0)
                     for i in range(slen))
    return bytes(out)


# --- the padded bank ---------------------------------------------------------
def padded_bank(path):
    """os88midbank's synthetic bank with 120,000 bytes of silent samples in
    FRONT of it: every sample a program plays starts past 120KB, in its
    fourth 32KB window."""
    import os88midbank as mb
    orig = mb.write

    def padded(p, progs, drums, samples):
        pad = [{"data": bytes(40000), "lps": 0, "lpe": 0, "rate": 11025,
                "gain": 48}] * 3

        def sh(tab):
            return [None if e is None else (e[0] + 3,) + tuple(e[1:])
                    for e in tab]
        return orig(p, sh(progs), sh(drums), pad + samples)
    mb.write = padded
    try:
        mb.synth(path)
    finally:
        mb.write = orig


# --- what the card played ----------------------------------------------------
def audio(path):
    """(seconds, the share of 50 ms blocks audible, median spectral
    flatness) of the capture's second half - past the load and the first
    notes - or None if there is no capture"""
    import numpy as np
    if not os.path.exists(path) or os.path.getsize(path) < 1000:
        return None
    raw = open(path, "rb").read()
    # BY HAND, as tools/sndcheck.py does: a capture Bochs was stopped in the
    # middle of leaves the RIFF and data sizes as it began them
    if raw[0:4] != b"RIFF" or raw[8:12] != b"WAVE":
        return None
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(raw):
        cid, sz = raw[pos:pos + 4], struct.unpack("<I", raw[pos + 4:pos + 8])[0]
        if cid == b"fmt ":
            fmt = raw[pos + 8:pos + 24]
        elif cid == b"data":
            data = raw[pos + 8:]        # to the end, whatever it says
            break
        pos += 8 + ((sz + 1) & ~1)
    if fmt is None or data is None:
        return None
    _, ch, rate, _, _, bits = struct.unpack("<HHIIHH", fmt)
    sw = bits // 8
    dt = {1: np.uint8, 2: np.int16}[sw]
    x = np.frombuffer(data[:len(data) // (sw * ch) * sw * ch], dtype=dt)
    x = x.astype(np.float64)
    if sw == 1:
        x -= 128.0
    x = x.reshape(-1, ch).mean(axis=1) / (128.0 if sw == 1 else 32768.0)
    # the music: from the first audible block on
    blk = max(1, rate // 20)
    n = len(x) // blk
    if n < 4:
        return None
    rms = np.sqrt((x[:n * blk].reshape(n, blk) ** 2).mean(axis=1))
    on = np.nonzero(rms > 0.01)[0]
    if not len(on):
        return (len(x) / rate, 0.0, 1.0)
    x = x[on[0] * blk:]
    rms = rms[on[0]:]
    # AUDIBLE IS ABOVE -50 dBFS (RMS 0.003), not -40: since Bochs's clock
    # is the guest's (docs/BOCHS.md *The clock*) the replayer plays near
    # the module's own tempo, and these modules' quiet passages sit between
    # the two - a trk capture's blocks are bimodal, a tenth under 0.001
    # (silence) and the rest from 0.003 up with a median of 0.013. At 0.01
    # the quiet passages counted as SILENT and the leg read 47-56% from
    # run to run against a bar of 50% (mega-megamod's trk and trkarm). At
    # 0.003 the same captures read ~88%, and a card that is not being fed
    # is still silence
    act = float((rms > 0.003).mean())
    # FLATNESS IN THE BAND THE PLAYERS MIX INTO, 100 Hz to 4 kHz: the card's
    # capture is resampled to its own rate, so the band above the mix rate's
    # Nyquist is empty and a whole-band figure reads "tonal" for anything
    fr = 4096
    lo, hi = int(100 * fr / rate), int(4000 * fr / rate)
    flat = []
    for i in range(0, len(x) - fr, fr):
        p = np.abs(np.fft.rfft(x[i:i + fr] * np.hanning(fr)))[lo:hi] ** 2 \
            + 1e-12
        if p.mean() > 1e-7:
            flat.append(float(np.exp(np.log(p).mean()) / p.mean()))
    return (len(x) / rate, act, float(np.median(flat)) if flat else 1.0)


def check_audio(leg, d):
    a = audio(os.path.join(d, "sb.wav"))
    if a is None:
        return want(False, "%s: the card's capture is empty" % leg)
    secs, act, flat = a
    print("   %s: %.1f s of capture from the first note, %.0f%% of it "
          "audible, median spectral flatness %.3f" % (leg, secs, 100 * act,
                                                      flat))
    return want(secs > PLAY / 2 and act > 0.5 and flat < 0.25,
                "%s: music on the card - audible for most of the play and "
                "not noise" % leg)


# --- a boot ------------------------------------------------------------------
def bad_lines(box):
    return [l for l in box.lines() if l.startswith("kern_mega FAULT") or
            l.startswith("kern_mega CLOSED")]


def pkg_map(src, incs, defines=()):
    """{label: offset} of a package, re-assembled with [map all]"""
    with tempfile.TemporaryDirectory() as t:
        mp = os.path.join(t, "p.map")
        cmd = ["nasm", "-f", "bin", "-w+error"]
        for i in incs:
            cmd += ["-I", os.path.join(SRC, i)]
        cmd += ["-D" + x for x in defines]
        cmd += ["--before", "[map all %s]" % mp, "-o",
                os.path.join(t, "p.bin"), os.path.join(SRC, src)]
        subprocess.run(cmd, check=True)
        return os88sym._parse_map(mp)[0]


class Pkg(object):
    def __init__(self, box, name, syms):
        self.box, self.name, self.s = box, name, syms

    def base(self):
        b = self.box.pkg_base(self.name)
        if b is None:
            raise RuntimeError("no live %s" % self.name)
        return b

    def rd(self, label, n=1, base=None):
        return self.box.read((base or self.base()) + self.s[label], n)

    def b(self, label):
        return self.rd(label)[0]

    def put(self, label, val):
        self.box.dbg(["setpmem 0x%x 1 0x%x" % (self.base() + self.s[label],
                                                val)])


def boot(leg, out, files, megs=16):
    d = os.path.join(out, leg)
    os.makedirs(d)
    disk = os.path.join(d, "bsrc.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", disk, "--size", "1440"] + files +
                   ["--folder", "SYSTEM/APPDATA"], check=True,
                   stdout=subprocess.DEVNULL)
    box = os88bochs.Box(d, [], sb=True, cfg=1, bimg=disk, btype="1_44",
                        megs=megs, display=DISPLAY)
    box.up()
    box.capture()
    box.dbl(591, 105, "B:'s Disk window")
    return box, d


def listed(files, name):
    """NAME's row in B:'s root listing: the one folder (SYSTEM) first, then
    the files by name"""
    names = sorted(os.path.basename(f).upper() for f in files
                   if ":" not in f)
    return 1 + names.index(os.path.basename(name).upper())


def row(box, i, what, lock=True):
    box.dbl(ROW_X, ROW0_Y + ROW_H * i, what, lock=lock)


def windows(leg, box, t, base, mod, pre):
    """THE WINDOWS THEMSELVES, exact: every pattern window's and every
    playable sample's descriptor base against the blob's own base plus the
    offset the file says - read out of the LDT, so a window aimed one
    paragraph wrong fails here even where the music would not show it"""
    data = open(mod, "rb").read()
    npat = max(data[952:1080]) + 1
    u16 = lambda b, i: b[i] | b[i + 1] << 8
    blob = u16(t.rd(pre + "blobseg", 2, base), 0)
    b0 = box.sel_base(blob)
    pw = t.rd(pre + "pwin", 16, base)
    bad = []
    for w in range((npat + 31) // 32):
        got = box.sel_base(u16(pw, 2 * w)) - b0
        if got != 1024 + 32768 * w:
            bad.append("pattern window %d at +%X" % (w, got))
    tab = t.rd(pre + "smptab", 31 * 12, base)
    start, n = 1084 + 1024 * npat, 0
    for i in range(31):
        seg, off, plen = u16(tab, 12 * i), u16(tab, 12 * i + 2), \
            u16(tab, 12 * i + 4)
        if plen:
            n += 1
            got = box.sel_base(seg) - b0 + off
            if got != start or off > 15:
                bad.append("sample %d at +%X+%d, the file has it at +%X"
                           % (i + 1, got - off, off, start))
        start += 2 * (data[20 + 30 * i + 22] << 8 | data[20 + 30 * i + 23])
    return want(not bad, "%s: %d pattern window(s) and %d sample windows "
                "based where the file puts them (the last sample at +%X)%s"
                % (leg, (npat + 31) // 32, n, start, "".join(
                    " | " + x for x in bad[:4])))


def listen(box, cond, what, secs):
    """PLAY seconds of the guest's own music, failing at once on a FAULT or
    a CLOSED line; COND() read once a second must hold throughout"""
    end = time.time() + secs
    while time.time() < end:
        b = bad_lines(box)
        if b:
            return b[0]
        r = cond()
        if r is not True:
            return "%s: %s" % (what, r)
        time.sleep(1.0)
    return None


def leg_tracker(leg, out, pkg, mapsyms, mod, check_wide=False):
    box, d = boot(leg, out, [mod, pkg])
    try:
        row(box, listed([mod, pkg], mod), "the module's open", lock=False)
        t = Pkg(box, "TRACKER", mapsyms)
        box.until(lambda: bad_lines(box) or (box.pkg_base("TRACKER") and
                                            t.b("mp_playing") == 1),
                  "Tracker playing the module", 120, poll=0.5)
        b = bad_lines(box)
        if not want(not b, "%s: Tracker opened the module and plays it%s" %
                    (leg, "".join(" | " + l for l in b[:2]))):
            return
        base = t.base()
        p0 = t.b("mp_songpos")
        want(t.b("mp_loaded") == 1, "%s: [mp_loaded], [mp_npat] %d, the "
             "blob's selector %04X" % (
                 leg, int.from_bytes(t.rd("mp_npat", 2), "little"),
                 int.from_bytes(t.rd("mp_blobseg", 2), "little")))
        windows(leg, box, t, base, mod, "mp_")
        seen = {}

        trail = []

        def ok():
            sp, rw = t.rd("mp_songpos", 2, base)
            pat = t.rd("mp_pattern", 1, base)[0]
            trail.append((sp, pat, rw))
            if not t.b("mp_playing"):
                return "the replayer stopped"
            if check_wide:
                v = box.read(base + t.s["mp_chans"] + MP_VOL, 1)[0]
                r = t.rd("mp_row", 1, base)[0]
                if 2 <= r < 60:             # inside the pattern, not its turn
                    seen.setdefault(pat, []).append(v)
            return True
        bad = listen(box, ok, "%s: the play" % leg, PLAY)
        want(bad is None, "%s: %.0f s of play, no fault%s" %
             (leg, PLAY, "" if bad is None else " | " + bad))
        print("   %s: (position, pattern, row) a second apart: %s" %
              (leg, " ".join("%d/%d/%d" % x for x in trail)))
        # ...AT ANY POINT of the play, not net of it: the wide module is six
        # orders and loops, and at its own tempo (the guest's clock) 14 s is
        # one lap almost exactly - a run read 5 -> 0..5 -> 5 and failed
        moved = sorted(set(x[0] for x in trail) | {t.b("mp_songpos")})
        want(any(x != p0 for x in moved), "%s: the song position moved "
             "(%d, then %s)" % (leg, p0, moved))
        if check_wide:
            for p in WIDE_ORDERS:
                v = seen.get(p, [])
                good = sum(1 for x in v if x == wide_vol(p))
                print("   %s: pattern %3d (window %d) volume %r, want %d"
                      % (leg, p, p >> 5, sorted(set(v)), wide_vol(p)))
            hit = [p for p in WIDE_ORDERS if seen.get(p)]
            wrong = [p for p in hit
                     if sum(1 for x in seen[p] if x == wide_vol(p)) * 2
                     <= len(seen[p])]
            want(len(hit) >= 4 and not wrong and any(p >= 64 for p in hit),
                 "%s: every pattern played read its own row 0 through its "
                 "window (heard %s, wrong %s)" % (leg, hit, wrong))
        box.shot(os.path.join(d, "play.png"))
    finally:
        try:
            box.shot(os.path.join(d, "last.png"))
        finally:
            box.stop()
    check_audio(leg, d)


def leg_modplug(leg, out, pkg, mapsyms):
    files = [os.path.join(ROOT, "apps", "tracker", "beverly.mod"), pkg]
    box, d = boot(leg, out, files)
    try:
        row(box, listed(files, pkg), "ModPlug's open")
        box.until(lambda: bad_lines(box) or box.pkg_base("MODPLUG"),
                  "ModPlug's window", 60, poll=0.5)
        box.key("l")
        box.ui_done("the chooser", secs=60)
        box.shot(os.path.join(d, "chooser.png"))
        # the chooser lists B:'s root as the Disk window did, below its
        # own title (SPEC.md 38.1): a click selects the module, Enter opens
        box.click(190, 155, "the chooser's selection")
        box.key("Return")
        t = Pkg(box, "MODPLUG", mapsyms)
        box.until(lambda: bad_lines(box) or t.b("mpm_playing") == 1,
                  "ModPlug playing the module", 120, poll=0.5)
        b = bad_lines(box)
        if not want(not b, "%s: ModPlug opened the module and plays it%s" %
                    (leg, "".join(" | " + l for l in b[:2]))):
            return
        base = t.base()
        windows(leg, box, t, base, files[0], "mpm_")
        p0 = t.rd("mpm_songpos", 1, base)[0]
        bad = listen(box, lambda: True if t.b("mpm_playing") else
                     "the replayer stopped", "%s: the play" % leg, PLAY)
        want(bad is None, "%s: %.0f s of play, no fault%s" %
             (leg, PLAY, "" if bad is None else " | " + bad))
        want(t.rd("mpm_songpos", 1)[0] != p0,
             "%s: the song position moved" % leg)
        box.shot(os.path.join(d, "play.png"))
    finally:
        try:
            box.shot(os.path.join(d, "last.png"))
        finally:
            box.stop()
    check_audio(leg, d)


def wt_windows(leg, box, t, base):
    """the wavetable bank's windows, exact, and the pre-roll's note-ons
    placed through them"""
    nwin = int.from_bytes(t.rd("mwt_nwin", 2, base), "little") // 2
    kb = int.from_bytes(t.rd("mr_bankkb", 2, base), "little")
    b0 = box.sel_base(int.from_bytes(t.rd("mwt_bseg", 2, base), "little"))
    wins = t.rd("mwt_win", 2 * nwin, base)
    wsel = [wins[2 * i] | wins[2 * i + 1] << 8 for i in range(nwin)]
    at = [box.sel_base(x) - b0 for x in wsel]
    want(nwin == (kb + 31) // 32 and at == [32768 * i for i in
                                              range(nwin)],
         "%s: a window every 32KB of the %dKB bank, based where it says "
         "(%s)" % (leg, kb, " ".join("+%X" % x for x in at)))
    # the voices the pre-roll started: each one's segment is a window
    # (mwt_place), and with the padded bank a late one
    vs = t.rd("mwt_sseg", 32, base)
    used = sorted(set(wsel.index(v) for v in
                      (vs[2 * i] | vs[2 * i + 1] << 8 for i in range(16))
                      if v in wsel))
    want(used and max(used) >= (3 if kb > 120 else 0),
         "%s: the note-ons placed their samples through window(s) %s"
         % (leg, used))


def leg_midirack(leg, out, pkg, mapsyms, bank, mro=None):
    mro = MR_OUT_WT if mro is None else mro
    box, d = boot(leg, out, ["APPS:" + pkg, "APPS:" + bank,
                             "MEDIA/MIDI:" + os.path.join(
                                 ROOT, "apps", "midirack", "songs",
                                 "BATTLE1.MID")])
    try:
        row(box, 0, "B:\\APPS")
        box.shot(os.path.join(d, "apps.png"))
        row(box, 2, "MIDIRack's open", lock=False)  # .., MIDIRACK.BNK, .O88
        box.shot(os.path.join(d, "open.png"))
        t = Pkg(box, "MIDIRack", mapsyms)
        try:
            box.until(lambda: bad_lines(box) or (box.pkg_base("MIDIRack") and
                                                t.b("mr_loaded") == 1),
                      "MIDIRack's autoload", 120, poll=0.5)
        except RuntimeError:
            print("   %s: instances %r" % (leg, box.instances()))
            raise
        if not want(t.b("mr_hasbank") == 1, "%s: the bank beside it (%d KB)"
                    % (leg, int.from_bytes(t.rd("mr_bankkb", 2), "little"))):
            return
        t.put("mr_want", mro)
        box.key("space")
        # THE BANK IS READ, ITS WINDOWS MADE AND THE PRE-ROLL MIXED before
        # the card is asked for the stream (mrb_start): mwt_load, mwt_wins,
        # then mwt_span's note-ons (mwt_place) and voices (mwt_voice) reading
        # the samples through the windows - so all of it has run by the time
        # [mrb_open] or [mrb_err] says what the card answered
        box.until(lambda: bad_lines(box) or t.b("mrb_open") == 1 or
                  t.b("mrb_err"), "the card's stream", 180, poll=0.5)
        b = bad_lines(box)
        if not want(not b, "%s: the bank read and the pre-roll mixed, no "
                    "fault%s" % (leg, "".join(" | " + l for l in b[:2]))):
            return
        base = t.base()
        if mro == MR_OUT_WT:
            wt_windows(leg, box, t, base)
        # THE CARD'S HALF: the stream opened on MIDIRack's EXTERNAL ring (a
        # conventional OSAPI_MEM_CLAIM_DMA_HI claim, an LDT selector under
        # kern_mega) - which the kernel's stream door refused with 2 while
        # it tested the selector's TI bit for "above 1MB" (MC7.4) - on the
        # synth (MR_OUT_SB) as well as the wavetable
        if not want(t.b("mrb_open") == 1 and t.b("mrb_grant") == 0,
                    "%s: the card's stream open on the external ring "
                    "(SOUND.DRV said %d, grant ring %d)"
                    % (leg, t.b("mrb_err"), t.b("mrb_grant"))):
            return
        rbase = box.sel_base(int.from_bytes(t.rd("mrb_seg", 2, base),
                                            "little"))
        cons = lambda: int.from_bytes(box.read(rbase + MRB_RL + 2, 2),
                                      "little")
        c0 = cons()
        time.sleep(1.0)
        c1 = cons()
        want(c1 != c0, "%s: the card consuming the ring - the block IRQ's "
             "count at +%X moved %d -> %d" % (leg, MRB_RL + 2, c0, c1))
        # Bochs's SB16 drains its DMA ~10x fast (the module above), so the
        # song can play out inside the listen: a stop with the list's end
        # as the reason is the song ENDING, anything else is not
        endmsg = t.s["mr_s_endlist"]
        ended = []

        def playing():
            if t.b("mr_state") == 1:
                return True
            m = int.from_bytes(t.rd("mr_msg", 2, base), "little")
            if t.b("mr_state") == 0 and m == endmsg:
                ended.append(1)
                return True
            return "the song stopped (state %d, msg %04X)" % (
                t.b("mr_state"), m)
        bad = listen(box, playing, "%s: the play" % leg, PLAY)
        want(bad is None, "%s: %.0f s of the song, no fault%s%s" %
             (leg, PLAY, " (the song played to its end)" if ended else "",
              "" if bad is None else " | " + bad))
        box.shot(os.path.join(d, "play.png"))
    finally:
        try:
            box.shot(os.path.join(d, "last.png"))
        finally:
            box.stop()
    check_audio(leg, d)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--leg", action="append")
    ap.add_argument("--pkg", action="append", default=[],
                    help="TRACKER=, TRKARM=, MODPLUG=, MIDIRACK=, MRARM=")
    ap.add_argument("--src", help="the source tree the --pkg builds were "
                    "made from, for their symbols (a negative control)")
    ap.add_argument("--bank", help="a MIDIRACK.BNK (default: a padded "
                    "synthetic one)")
    a = ap.parse_args()
    global SRC
    SRC = os.path.abspath(a.src) if a.src else ROOT
    pk = {"TRACKER": "build/tracker.o88",
          "TRKARM": "build/megak/apps/tracker.o88",
          "MODPLUG": "build/modplug.o88",
          "MIDIRACK": "build/midirack.o88",
          "MRARM": "build/megak/apps/midirack.o88"}
    for p in a.pkg:
        k, v = p.split("=", 1)
        pk[k.upper()] = v
    legs = a.leg or ["trk", "wide", "trkarm", "mpp", "mr", "mrsb",
                     "mrarm"]
    for f in ("build/mega.img", "build/megarun.o88", "build/mbr.bin"):
        if not os.path.exists(os.path.join(ROOT, f)):
            sys.exit("megamod: %s is not built (make mega megarun)" % f)
    out = os.path.join(BUILD, "bochs-megamod")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    tmp = tempfile.mkdtemp(prefix="megamod-")
    t0 = time.time()
    try:
        def stage(key, name):
            p = os.path.join(tmp, key.lower(), name)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            shutil.copy(os.path.join(ROOT, pk[key]), p)
            return p
        bev = os.path.join(ROOT, "apps", "tracker", "beverly.mod")
        wide = os.path.join(tmp, "WIDE.MOD")
        with open(wide, "wb") as f:
            f.write(wide_mod())
        bank = a.bank or os.path.join(tmp, "MIDIRACK.BNK")
        if not a.bank:
            padded_bank(bank)
        else:
            shutil.copy(bank, os.path.join(tmp, "MIDIRACK.BNK"))
            bank = os.path.join(tmp, "MIDIRACK.BNK")
        trk = ("apps/tracker/tracker.asm", ("apps/", "apps/tracker/"))
        mr = ("apps/midirack/midirack.asm", ("apps/", "apps/midirack/",
                                             "drivers/sound/", "drivers/"))
        for leg in legs:
            print("megamod: %s" % leg, flush=True)
            try:
                if leg == "trk":
                    leg_tracker(leg, out, stage("TRACKER", "TRACKER.O88"),
                                pkg_map(*trk), bev)
                elif leg == "wide":
                    leg_tracker(leg, out, stage("TRACKER", "TRACKER.O88"),
                                pkg_map(*trk), wide, check_wide=True)
                elif leg == "trkarm":
                    leg_tracker(leg, out, stage("TRKARM", "TRACKER.O88"),
                                pkg_map(*trk, defines=("PKG_MEGA",)), bev)
                elif leg == "mpp":
                    leg_modplug(leg, out, stage("MODPLUG", "MODPLUG.O88"),
                                pkg_map("apps/modplug/modplug.asm",
                                        ("apps/", "apps/modplug/")))
                elif leg == "mr":
                    leg_midirack(leg, out, stage("MIDIRACK", "MIDIRACK.O88"),
                                 pkg_map(*mr), bank)
                elif leg == "mrsb":
                    leg_midirack(leg, out, stage("MIDIRACK", "MIDIRACK.O88"),
                                 pkg_map(*mr), bank, mro=MR_OUT_SB)
                elif leg == "mrarm":
                    leg_midirack(leg, out, stage("MRARM", "MIDIRACK.O88"),
                                 pkg_map(*mr, defines=("PKG_MEGA",)), bank)
                else:
                    want(False, "no leg %r" % leg)
            except Exception as e:      # noqa: BLE001 - the machine stopped
                want(False, "%s: %s" % (leg, e))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
        if not a.keep:
            for leg in legs:
                for n in ("a.img", "b.img", "c.img", "bsrc.img"):
                    try:
                        os.remove(os.path.join(out, leg, n))
                    except OSError:
                        pass
    print("megamod: %.0fs under Bochs" % (time.time() - t0))
    for f in FAILS:
        print("FAIL megamod: " + f)
    print("megamod: %s" % ("FAILED" if FAILS else "PASS"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
