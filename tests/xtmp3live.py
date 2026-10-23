#!/usr/bin/env python3
"""xtmp3live - XTMP3 with its REAL decoders, end to end (SPEC.md 121.3-121.7,
docs/xtmp3/ENGINE.md).

    make && make build/mixercfg/SYSTEM.CFG && python3 tests/xtmp3live.py
         [--leg NAME ...]

QEMU BY NAME, and for what QEMU is exact about: the BYTES. The package is the
SHIPPED build (build/xtmp3.o88: the FRONT, STANDARD and XT parts as they ship);
the card is QEMU's SB16, its output captured by the `wav` audiodev at the
STREAM's own rate (so QEMU's mixer resamples nothing and its s16/u8
conversions are exact), and every capture is held SAMPLE FOR SAMPLE to
tools/x3sim.py's PCM for the same file, divisor and format, aligned by search.
Each leg boots its own private QEMU (pidfile, its own socket, its disks under
build/x3l*) and reads XTMP3's state out of its bss by the shipped build's own
map (tests/xtmp3eng.py's Rig, which this row extends).

Legs (each one boot unless it says otherwise):
  orig44    C128.MP3 (44.1 kHz L/R stereo, LAME tag) under Auto: ORIGINAL, 16-bit
            stereo - the capture IS x3sim's trimmed PCM, every sample, the two
            channels distinct; the XT part never fetched (x3_nfetch), the
            STANDARD one resident, no underrun. With the host's half of the
            proof: the STANDARD part's map has no XT label (x3x_) and none of
            the XT part's table bytes
  orig22    M22.MP3 (22.05 kHz joint stereo) under Auto: ORIGINAL, the same
  half      C128.MP3 with Quality: HALF set (the user's override): 22,050 Hz
            16-bit stereo, x3sim's divisor 2, every sample
  dev8st    an SB16 whose 16-bit channel cannot be programmed (`dma16=1`):
            ORIGINAL in 8-bit STEREO, labelled DEVICE - the capture is x3sim's
            PCM through the documented conversion (the high byte, XOR 80h: as
            QEMU widens it back, x & FF00h)
  dev8mono  a DSP 2.01 (`version=0x0201`, MartyPC's card): M22.MP3 as 8-bit
            MONO at 22,050 Hz, labelled DEVICE - x3sim's MONO DOWNMIX through
            the same conversion. The card's rate is its time constant's
            (22,222 Hz) and the capture is taken at that
  seek      NEONDRV.MP3 live, then '.' (forward ten seconds: the ninth): the
            output after X3_PREROLL frames is x3sim decoding from the offset
            the engine aimed at (x3_rsoff) with an empty reservoir and zero
            history, those frames dropped - every sample - and the position
            shown is the target's
  xtfile    XT STANDARD set by Q, Decode First to a QEMU hard disk (an SB16
            present): the finished WAV's PCM is tools/x3simxt.py's for that
            file and profile, trimmed - every byte; it then plays through the
            card with no decoder resident, and a seek ('.') plays the WAV's
            data from that second, directly - every sample
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
import numpy as np                                            # noqa: E402
import xtmp3eng as E                                          # noqa: E402
import os88qemu                                               # noqa: E402
import os88build                                              # noqa: E402
import x3sim                                                  # noqa: E402
import x3simxt                                                # noqa: E402

check = E.check
FIX = E.FIX
X3PART_STD, X3PART_XT = 2, 3
X3SF_DEVLIM = 0x04
X3_PREROLL = 6                      # x3eng.inc's (ENGINE.md section 1)


class LRig(E.Rig):
    """xtmp3eng's rig with the SHIPPED package, the card's options and the
    capture's rate"""

    def __init__(self, leg, afreq=44100, sbopt="", mem="4", **kw):
        self.afreq = afreq
        self.sbopt = sbopt
        self.mem = mem                  # "4": an XMS pool; "1": none
        kw.setdefault("sb", True)
        super().__init__(leg, stub=False, **kw)

    def launch(self, sb):
        t = self.tmp
        os88qemu.own(self.pidf, self.sock)
        a = ["qemu-system-i386", "-machine", "pc,vmport=off", "-m", self.mem,
             "-drive", "file=%s,format=raw,if=floppy" % os.path.join(t, "sys.img"),
             "-drive", "file=%s,format=raw,if=floppy,index=1" % os.path.join(t, "b.img")]
        for i, img in enumerate(self.hds):
            a += ["-drive", "file=%s,format=raw,if=ide,index=%d,media=disk"
                  % (img, i)]
        if sb:
            a += ["-device", "sb16,audiodev=a0" + self.sbopt, "-audiodev",
                  "wav,id=a0,path=%s,out.frequency=%d,out.channels=2,"
                  "out.format=s16" % (self.wav, self.afreq)]
        a += ["-boot", "a", "-chardev", "msmouse,id=m0", "-serial",
              "chardev:m0", "-display", "none", "-qmp",
              "unix:%s,server,nowait" % self.sock, "-daemonize",
              "-pidfile", self.pidf]
        subprocess.run(a, check=True)
        self.pid = int(open(self.pidf).read().strip())
        os88qemu.acted(self, lambda: os.path.exists(self.sock), secs=30,
                       what="the QMP socket")
        E.xmcheck.wait_desktop(self.sock, "xtmp3live")

    def nfetch(self):
        return list(self.rd("x3_nfetch", 5))

    def dword(self, name):
        return struct.unpack("<I", self.rd(name, 4))[0]


# -----------------------------------------------------------------------------
# the capture and the reference
# -----------------------------------------------------------------------------
def capture(path):
    """QEMU's wav audiodev: s16 stereo after the 44-byte header (kept as
    build/x3l-<leg>.wav when $X3L_KEEP is set, for a look or a listen)"""
    if os.environ.get("X3L_KEEP"):
        shutil.copy(path, os.path.join(ROOT, "build", "x3l-%s.wav" % (
            os.path.basename(os.path.dirname(path)))))
    raw = open(path, "rb").read()[44:]
    return np.frombuffer(raw[:len(raw) // 4 * 4], dtype="<i2").reshape(-1, 2)


# QEMU 11's mixing engine widens an unsigned 8-bit sample u to (u - 127) x 257,
# clipped (measured: every value of a DSP 2.01 capture is in this table, and
# its inverse gives back x3sim's bytes exactly), so an 8-bit stream's capture
# is read back into the bytes the CARD was given and held to those
U8WIDE = np.clip((np.arange(256) - 127) * 257, -32768, 32767)
U8BACK = {int(U8WIDE[u]): u for u in range(256)}


def as_u8(cap):
    """an 8-bit stream's capture -> the card's bytes (as int16, -1 for a
    value no byte widens to)"""
    vals, inv = np.unique(cap, return_inverse=True)
    back = np.array([U8BACK.get(int(v), -1) for v in vals], dtype=np.int16)
    return back[inv].reshape(cap.shape)


def as_cap(ref, bits):
    """x3sim's int16 PCM (n, ch) as the CARD was given it, in both columns:
    16-bit as it is, and an 8-bit stream through the documented conversion -
    the high byte XOR 80h (x3gran.inc's U8)"""
    r = ref.astype(np.int32)
    if r.shape[1] == 1:
        r = np.repeat(r, 2, axis=1)
    if bits == 8:
        r = ((r >> 8) & 0xFF) ^ 0x80
    return r.astype(np.int16)


def align(cap, want, k=48, quiet=0):
    """the offset in CAP where WANT[0] lies: a K-frame probe from WANT's first
    loud stretch (|x - QUIET| > 2000, or 8 on bytes), found exactly. None
    when it is nowhere"""
    thr = 8 if quiet else 2000
    loud = np.nonzero(np.abs(want[:, 0].astype(np.int32) - quiet) > thr)[0]
    if not len(loud):
        return None
    j0 = int(loud[0])
    probe = want[j0:j0 + k]
    if len(probe) < k:
        return None
    hits = np.nonzero((cap[:, 0] == probe[0, 0]) & (cap[:, 1] == probe[0, 1]))[0]
    for h in hits:
        h = int(h)
        if h + k <= len(cap) and np.array_equal(cap[h:h + k], probe):
            if h - j0 >= 0:
                return h - j0
    return None


# QEMU's SB16 pulls DMA as its mixer has room and its wav audiodev writes what
# the mixer has played: when a stream closes, what the mixer still holds is
# dropped (measured: 546 frames, 49 ms, at the end of a finished WAV). The
# card has consumed it - verb 3 says so, which is what ends the stream - so a
# capture may end that much short of the stream and no more
QEMU_TAIL_S = 0.1


def held(leg, what, cap, want, partial_ok=False, bits=16, rate=44100):
    """the capture holds WANT, every sample: aligned, then compared whole. An
    8-bit stream's capture is first read back into the card's bytes"""
    if bits == 8:                       # (-1 where no byte widens to it:
        cap = as_u8(cap)                # never equal to a byte below)
    o = align(cap, want, quiet=128 if bits == 8 else 0)
    if not check(leg, what + ": found in the capture", o is not None,
                 "%d frames captured, %d wanted" % (len(cap), len(want))):
        return None
    n = min(len(want), len(cap) - o)
    diff = np.nonzero(np.any(cap[o:o + n] != want[:n], axis=1))[0]
    more = "%d of %d frames, at capture %d" % (n, len(want), o)
    if len(diff):
        i = int(diff[0])
        more += "; %d differ, the first at %d: %s vs %s" % (
            len(diff), i, list(cap[o + i]), list(want[i]))
    check(leg, what + ": every sample", not len(diff) and
          (len(want) - n <= rate * QEMU_TAIL_S or partial_ok), more)
    return o, n


def distinct(leg, cap_part):
    a = cap_part[:, 0].astype(float)
    b = cap_part[:, 1].astype(float)
    same = np.count_nonzero(cap_part[:, 0] == cap_part[:, 1])
    c = np.corrcoef(a, b)[0, 1] if a.std() and b.std() else 1.0
    check(leg, "the two channels are distinct (left != right)",
          same < len(cap_part) // 10 and c < 0.9,
          "%d of %d equal, correlation %.3f" % (same, len(cap_part), c))


def x3sim_ref(name, div=1, mono=False):
    pcm, rate, st, dec = x3sim.decode_file(os.path.join(FIX, name), div=div,
                                           mono=mono, trim=True)
    return pcm, rate, st


# -----------------------------------------------------------------------------
# the host's half of "Original never calls the XT kernels"
# -----------------------------------------------------------------------------
def std_has_no_xt(leg):
    d = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    try:
        mp, b = os.path.join(d, "s.map"), os.path.join(d, "s.bin")
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                        "-I", "apps/xtmp3/", "-I", "build/",
                        "--before", "[map symbols %s]" % mp, "-o", b,
                        "apps/xtmp3/x3std.asm"], check=True)
        same = open(b, "rb").read() == open(os88build.at("build/x3stdp.bin"),
                                             "rb").read()
        check(leg, "host: the STANDARD part assembles to the shipped bytes",
              same)
        labels = [m.group(1) for m in
                  (re.match(r"\s*[0-9A-F]+\s+[0-9A-F]+\s+(\w+)$", ln)
                   for ln in open(mp)) if m]
        xt = [x for x in labels if x.startswith("x3x_")]
        check(leg, "host: no XT label (x3x_) in the STANDARD part's map",
              labels and not xt, "%d labels, XT: %s" % (len(labels), xt[:4]))
        std = open(b, "rb").read()
        gen = open(os88build.at("build/XTGEN.BIN"), "rb").read()
        pieces = [gen[i:i + 32] for i in range(0, len(gen) - 32, 512)
                  if len(set(gen[i:i + 32])) > 8]
        found = [i for i, p in enumerate(pieces) if p in std]
        check(leg, "host: none of the XT tables' generator bytes (XTGEN.BIN) "
              "in the STANDARD part", pieces and not found,
              "%d of %d 32-byte runs found" % (len(found), len(pieces)))
    finally:
        shutil.rmtree(d, ignore_errors=True)


# -----------------------------------------------------------------------------
# THE LIVE LEGS
# -----------------------------------------------------------------------------
def play_live(r, leg, name, qual=None):
    """open NAME on C:, Auto plays it; QUAL: stopped, the override set by Q
    from AUTO, played again from its start. -> the state once LIVE"""
    if not check(leg, "XTMP3 opens on %s" % name,
                 r.open_file("C", name, E.hd_listing([name]))):
        return None
    if qual is not None:
        r.until(lambda: r.st()["md"] not in (E.MD_IDLE, E.MD_CHECK),
                "Auto's decision", 60, 0.1)
        r.key("s")
        r.until(lambda: r.st()["md"] == E.MD_IDLE, "the stop", 20, 0.1)
        for _ in range(qual):
            r.key("q")
            time.sleep(0.3)
        check(leg, "the override is %s" % E.QNAME[qual],
              r.rd("x3_st", 3)[2] == qual, E.QNAME[r.rd("x3_st", 3)[2]])
        r.key("spc")
    # LIVE - or already DONE: a 1.5 s fixture can play out between two
    # polls on a loaded host (each a QMP round trip), and what is read below
    # (the rung, the output, the part) stays in x3_st after the end
    r.until(lambda: r.st()["md"] == E.MD_LIVE or
            r.st()["state"] == E.ST_DONE, "live", 60, 0.05)
    s = r.st()
    if s["state"] == E.ST_DONE and r.w("x3_grans"):
        s["md"] = E.MD_LIVE             # (it was, and it played out)
    s["nf0"] = r.nfetch()
    s["bpart"] = r.b("x3_bpart")
    s["nrseq"] = r.w("x3_nrseq")
    return s


def live_case(leg, name, want_q, rate, ch, bits, devlim, afreq, sbopt="",
              qual=None, div=1, mono=False, std_proof=False, hold=1, mem="4"):
    print(" %s: %s" % (leg, name))
    if std_proof:
        std_has_no_xt(leg)
    r = LRig(leg, afreq=afreq, sbopt=sbopt, mem=mem,
             hd=[(E.HD0_GEO, [E.fixture(name)], ["SYSTEM/APPDATA"], 0)])
    try:
        s = play_live(r, leg, name, qual)
        if s is None:
            return
        check(leg, "LIVE on %s" % E.QNAME[want_q],
              s["md"] == E.MD_LIVE and s["qual"] == want_q,
              "md %d state %d qual %s '%s'" % (s["md"], s["state"],
                                              E.QNAME[s["qual"]], r.errtext()))
        check(leg, "the output: %d Hz, %d channel%s, %d-bit" % (
            rate, ch, "s" if ch > 1 else "", bits),
            (s["rate"], s["ch"], s["bits"]) == (rate, ch, bits),
            "%d Hz %d ch %d-bit" % (s["rate"], s["ch"], s["bits"]))
        check(leg, "labelled DEVICE" if devlim else "not labelled DEVICE",
              bool(s["flags"] & X3SF_DEVLIM) == devlim, "flags %02x" % s["flags"])
        check(leg, "the STANDARD part is the one resident",
              s["bpart"] == X3PART_STD, "x3_bpart %d" % s["bpart"])
        r.until(lambda: r.st()["state"] == E.ST_DONE, "the end", 60, 0.25)
        s2 = r.st()
        nf = r.nfetch()
        check(leg, "played to the end, no underrun",
              s2["state"] == E.ST_DONE and s2["under"] == 0,
              "state %d under %d" % (s2["state"], s2["under"]))
        check(leg, "the XT part was never fetched (x3_nfetch)",
              nf[X3PART_XT] == 0 and nf[X3PART_STD] >= 1, str(nf))
        # THE HOLD (SPEC.md 121.6.1): the whole MP3 read before the ladder -
        # XMS on -m 4, a conventional claim on -m 1 - and from LIVE to the
        # end not one gulp off the disk; the capture below is still x3sim's
        kind, on = r.b("x3h_kind"), r.b("x3h_on")
        n1, nc = r.w("x3_nrseq"), r.w("x3h_ncopy")
        check(leg, "held whole (%s), and NO READ_SEQ from LIVE to the end: "
              "%d gulps copied out of the hold" % (
                  {1: "XMS", 2: "a conventional claim"}.get(hold, hold), nc),
              kind == hold and on == 1 and n1 == s["nrseq"] and nc > 0,
              "kind %d on %d, disk gulps %d -> %d, copies %d" % (
                  kind, on, s["nrseq"], n1, nc))
    finally:
        r.close()
    try:
        cap = capture(r.wav)
        ref, rrate, _ = x3sim_ref(name, div=div, mono=mono)
        check(leg, "x3sim's rate is the stream's", rrate == rate,
              "%d" % rrate)
        want = as_cap(ref, bits)
        got = held(leg, "x3sim's %s PCM%s" % (
            "mono downmix" if mono else "trimmed",
            " (as 8 bits)" if bits == 8 else ""), cap, want, bits=bits,
            rate=afreq)
        if got and ch == 2:
            distinct(leg, cap[got[0]:got[0] + got[1]])
    finally:
        r.done()


def leg_holdconv():
    """the hold with no XMS pool (QEMU -m 1): a conventional claim, the
    same song, the same sample-exact capture and no disk read while live"""
    live_case("holdconv", "C128.MP3", E.Q_ORIG, 44100, 2, 16, False, 44100,
              hold=2, mem="1")


def leg_orig44():
    live_case("orig44", "C128.MP3", E.Q_ORIG, 44100, 2, 16, False, 44100,
              std_proof=True)


def leg_orig22():
    live_case("orig22", "M22.MP3", E.Q_ORIG, 22050, 2, 16, False, 22050)


def leg_half():
    live_case("half", "C128.MP3", E.Q_HALF, 22050, 2, 16, False, 22050,
              qual=E.Q_HALF, div=2)


def leg_dev8st():
    live_case("dev8st", "C128.MP3", E.Q_ORIG, 44100, 2, 8, True, 44100,
              sbopt=",dma16=1")


def qemu_tc_rate(rate):
    """the rate QEMU's SB16 plays a legacy time constant at: SOUND.DRV
    truncates 1e6 / rate (sb.inc), QEMU rounds 1e6 / (256 - TC)"""
    tmp = 1000000 // rate
    return (1000000 + tmp // 2) // tmp


def leg_dev8mono():
    live_case("dev8mono", "M22.MP3", E.Q_ORIG, 22050, 1, 8, True,
              qemu_tc_rate(22050), sbopt=",version=0x0201", mono=True)


# -----------------------------------------------------------------------------
# SEEK
# -----------------------------------------------------------------------------
def seek_ref(name, off, tframe, div=1):
    """x3sim from file offset OFF as x3_resync leaves the machine: the scan
    for the next agreeing pair under the stream's own lock (x3p_reset), the
    reservoir empty, every history zero (RESET), the pre-roll's frames
    decoded and dropped; then the tail trim when the count is exact"""
    data = open(os.path.join(FIX, name), "rb").read()
    st = x3sim.Stream(data)
    s = x3sim.find_sync(st.b, off, st.end, st.h0)
    st.first = s[0]
    st.tagframe = None
    dec = x3sim.Decoder(st, "int", div, False)
    chunks = []
    for h, side, md, verdict in st.frames():
        if verdict == x3sim.F_TRUNC:
            break
        chunks.append(dec.decode_frame(h, side, md, verdict))
    pcm = np.concatenate(chunks, axis=0)
    spf = 576 * st.h0.granules // div
    pre = tframe - max(tframe - X3_PREROLL, 0)
    pcm = pcm[pre * spf:]
    if st.info.get("exact"):
        total = st.info["frames"] * spf
        _, tail = x3sim.trim_counts(st.info)
        keep = total - tframe * spf - tail // div
        pcm = pcm[:keep]
    return pcm, s[0]


def frame_at(name, off):
    """the index of the frame that starts at byte OFF (the walk x3sim's
    frames() makes), or None"""
    data = open(os.path.join(FIX, name), "rb").read()
    st = x3sim.Stream(data)
    o, k = st.first + (st.h0.size if st.tagframe else 0), 0
    while o + 4 <= st.end and o < off:
        h = x3sim.parse_header(st.b, o)
        if h is None:
            return None
        o, k = o + h.size, k + 1
    return k if o == off else None


def cont_ref(name, frame, div=1):
    """the same stretch from an uninterrupted decode, for the record"""
    pcm, rate, st, dec = x3sim.decode_file(os.path.join(FIX, name), div=div)
    spf = 576 * st.h0.granules // div
    return pcm[frame * spf:]


def leg_seek():
    leg = "seek"
    name = "NEONDRV.MP3"
    print(" %s: %s" % (leg, name))
    r = LRig(leg, afreq=44100,
             hd=[(E.HD0_GEO, [E.fixture(name)], ["SYSTEM/APPDATA"], 0)])
    try:
        s = play_live(r, leg, name)
        if s is None:
            return
        check(leg, "LIVE on ORIGINAL", s["qual"] == E.Q_ORIG,
              E.QNAME[s["qual"]])
        r.key("dot")                    # forward ten seconds: to the ninth
        r.until(lambda: r.st()["elap"] >= 9, "the seek", 10, 0.05)
        time.sleep(0.5)                 # (the clock re-opens at the target
        e = r.st()["elap"]              # frame's first sample, 8.986 s: 0:08
        tf, off = r.w("x3_tframe"), r.dword("x3_rsoff")     # for 14 ms)
        check(leg, "the seek went to 0:09 (ten seconds on, the clamp)",
              e == 9, "elapsed %d" % e)
        r.until(lambda: r.st()["state"] == E.ST_DONE, "the end", 60, 0.25)
        check(leg, "played on to the end, no underrun",
              r.st()["under"] == 0, str(r.st()["under"]))
        print("     target frame %d, the pre-roll's scan from byte %d"
              % (tf, off))
    finally:
        r.close()
    try:
        cap = capture(r.wav)
        ref, at = seek_ref(name, off, tf)
        check(leg, "the target frame is the ninth second's",
              tf == 9 * 44100 // 1152, "frame %d" % tf)
        got = held(leg, "x3sim from byte %d (frame at %d), %d frames of "
                   "pre-roll dropped" % (off, at, X3_PREROLL), cap, ref)
        k = frame_at(name, at)
        print("     the scan found frame %s; the audio after the pre-roll is "
              "frame %s's, the position shown frame %d's (the CBR estimate of "
              "a stream with no Xing count)" % (k, None if k is None else
                                                 k + X3_PREROLL, tf))
        cont = cont_ref(name, (k or 0) + X3_PREROLL)
        if got:
            n = min(len(cont), got[1])
            d = cap[got[0]:got[0] + n].astype(float) - cont[:n].astype(float)
            diff = np.nonzero(np.any(d != 0, axis=1))[0]
            print("     against the uninterrupted decode: %s" % (
                "identical from the first sample" if not len(diff) else
                "identical from sample %d on (%d differ before it)" % (
                    diff[-1] + 1, len(diff))))
    finally:
        r.done()


# -----------------------------------------------------------------------------
# THE XT PATH END TO END: a conversion, held to x3simxt
# -----------------------------------------------------------------------------
def leg_xtfile():
    leg = "xtfile"
    name = "AFTERHRS.MP3"
    print(" %s: %s" % (leg, name))
    r = LRig(leg, afreq=qemu_tc_rate(11025),    # (8-bit mono under 22,222
                                                # Hz is a time constant's)
             hd=[(E.HD0_GEO, [E.fixture(name)], ["SYSTEM/APPDATA"], 0)])
    try:
        if not check(leg, "XTMP3 opens", r.open_file(
                "C", name, E.hd_listing([name]))):
            return
        r.until(lambda: r.st()["md"] not in (E.MD_IDLE, E.MD_CHECK),
                "Auto's decision", 60, 0.1)
        r.key("s")
        r.until(lambda: r.st()["md"] == E.MD_IDLE, "the stop", 20, 0.1)
        for _ in range(E.Q_XTSTD):
            r.key("q")
            time.sleep(0.3)
        check(leg, "the override is XT STANDARD",
              r.rd("x3_st", 3)[2] == E.Q_XTSTD)
        nf0 = r.nfetch()
        r.key("d")
        E.conv_flow(r, leg)
        s = r.st()
        check(leg, "the conversion finished and the WAV plays on the card",
              s["state"] == E.ST_FILE and s["md"] == E.MD_FILE,
              "state %d md %d %s" % (s["state"], s["md"], r.errtext()))
        check(leg, "no decoder resident while it plays",
              r.b("x3_bpart") == 0xFF)
        g0 = r.w("x3_grans")
        check(leg, "the working set stayed at or under 400,000 bytes",
              0 < s["peak"] * 1024 <= 400000, "%d KB" % s["peak"])
        print("     peak %d KB, fetches %s -> %s" % (s["peak"], nf0,
                                                      r.nfetch()))
        time.sleep(1.0)
        r.key("dot")                    # a FINISHED file seeks directly
        r.until(lambda: r.st()["elap"] >= 9, "the seek", 10, 0.05)
        e = r.st()["elap"]
        r.until(lambda: r.st()["state"] == E.ST_DONE, "its end", 60, 0.25)
        check(leg, "no granule decoded while it played (the seek either)",
              r.w("x3_grans") == g0, "%d -> %d" % (g0, r.w("x3_grans")))
        check(leg, "no underrun", r.st()["under"] == 0)
    finally:
        r.close()
    try:
        data = r.mcat(r.hds[0], "::AFTERHRS.WAV")
        if not check(leg, "C: holds AFTERHRS.WAV", data is not None):
            return
        clu = 2048
        dlen = struct.unpack_from("<I", data, clu - 4)[0]
        pcm = np.frombuffer(data[clu:clu + dlen], dtype=np.uint8)
        ref, rate, dec = x3simxt.decode(os.path.join(FIX, name), "std")
        st = x3sim.Stream(open(os.path.join(FIX, name), "rb").read())
        ref = x3sim.trim_pcm(ref.reshape(-1, 1), st.info, 1).reshape(-1)
        check(leg, "the WAV: 11,025 Hz 8-bit mono, x3simxt's length",
              struct.unpack_from("<HHI", data, 20)[1:] == (1, 11025) and
              len(pcm) == len(ref), "%d vs %d" % (len(pcm), len(ref)))
        n = min(len(pcm), len(ref))
        bad = np.nonzero(pcm[:n] != ref[:n])[0]
        check(leg, "the WAV's PCM is x3simxt's XT Standard, trimmed: every "
              "byte", n and not len(bad),
              "%d of %d differ%s" % (len(bad), n, (", the first at %d"
                                                    % bad[0]) if len(bad)
                                     else ""))
        cap = capture(r.wav)
        frm = 9 * 11025
        want = as_cap(pcm[frm:].astype(np.int16).reshape(-1, 1), 16)
        held(leg, "the WAV's data from 0:09 (the seek, direct)", cap, want,
             bits=8, rate=qemu_tc_rate(11025))
        check(leg, "the seek showed 0:09", e == 9, str(e))
    finally:
        r.done()


LEGS = {"holdconv": leg_holdconv, "orig44": leg_orig44, "orig22": leg_orig22, "half": leg_half,
        "dev8st": leg_dev8st, "dev8mono": leg_dev8mono, "seek": leg_seek,
        "xtfile": leg_xtfile}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--leg", action="append", choices=sorted(LEGS))
    a = ap.parse_args()
    for f in ("build/xtmp3.o88", "build/xtmp3.bin", "build/os8088.img",
              "build/mixercfg/SYSTEM.CFG", "build/mbr.bin",
              "build/boothd.bin", "build/kernel.sys", "build/x3stdp.bin",
              "build/XTGEN.BIN"):
        if not os.path.exists(os88build.at(f)):
            sys.exit("xtmp3live: no %s - make && make "
                     "build/mixercfg/SYSTEM.CFG" % f)
    for name in a.leg or list(LEGS):
        print("xtmp3live: %s" % name)
        LEGS[name]()
    if E.bad:
        print("xtmp3live: %d FAILED" % len(E.bad))
        for b in E.bad:
            print("  " + b)
        return 1
    print("xtmp3live: every leg passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
