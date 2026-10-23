#!/usr/bin/env python3
"""SPEC.md 34.5.5: stereo and 16-bit streams through OSAPI_SND_STREAM verb 11.

    make && make sndfmttest && python3 tests/sndfmt.py           # QEMU SB16
    python3 tests/sndfmt.py --marty                              # a DSP 2.01

tests/sndfmt/sndfmt.asm runs itself off its window timer and writes every
answer it got into a RESULTS block (magic 'SFMTRES1'); this reads the block
out of the package's segment and, on QEMU, the card's own output.

QEMU (`-device sb16`, a DSP 4.05 with its 16-bit channel on DMA 5) boots
build/sndfmtsys.img, whose \\AUTOSTRT starts B:\\SNDFMT.O88, with the card's
audio captured by QEMU's `wav` audiodev at 22,050 Hz, two channels, 16-bit -
the stream's own rate, so the mixer resamples nothing. It asserts:

  1. OSAPI_SND_CAPS has SND_CAP_STEREO and SND_CAP_PCM16;
  2. the refusals: an unknown format bit 7, a format on a LINEAR open 7, a
     16-bit stereo ring at 4,500 Hz 2, a 16-bit ring at an ODD offset 7;
  3. each phase's verb 11 and verb 0 answered 0, the stream was still
     PLAYING at its end, and verb 9's bytes over the phase's ticks are the
     phase's BYTE rate (88,200 a second for 16-bit stereo) within 20% - the
     high channel's WORD count, doubled;
  4. the capture, cut into its five phases where verb 9 says each ended:
     16-bit stereo - the left channel 344.5 Hz at 8,000 and the right
     1,378 Hz at 600, neither carrying the other's tone, and the right's
     residual 45 dB or more below it, which 8 bits cannot do to a -35 dBFS
     tone (it is ~2 LSB of one); 8-bit stereo - left 689 Hz at 40 x 256,
     right 344.5 Hz at 100 x 256, separated; 16-bit mono - 1,378 Hz at
     6,000 on both sides; 16-bit stereo on an EXTERNAL ring (SPEC.md
     34.5.3), fed by memory writes alone; and 16-bit stereo left unfed until
     the ISR underruns it - verb 9 reads UNDERRUN-PAUSED, which on the
     16-bit side is D5h - then fed again, when it must RESUME (D6h) and play
     its remaining ticks' worth, tones intact across the seam.

MartyPC's Sound Blaster is a DSP 2.01, so the --marty leg is the REFUSAL
machine: no new caps bit, verb 11 answers 2 for every format and 7 for an
unknown bit, and - the negative control for the held format - the refusal
holds nothing: R2/R3/R4's opens then go on as 8-bit mono and SUCCEED, and no
phase opens at all.
"""
import argparse
import math
import os
import shutil
import struct
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
import os88build                                        # noqa: E402
import os88geom as G                                    # noqa: E402

PKG = "build/sndfmt.bin"
EVIDENCE = os.path.join(ROOT, "build", "sndfmt-evidence")
MAGIC = b"SFMTRES1"
RATE = 22050
NPH = 5
STARVE = 15                     # P4's unfed ticks (sndfmt.asm's SF_STARVE)
CAP_STEREO, CAP_PCM16 = 0x0400, 0x0800
TICK = 18.2065

# (name, byte rate, left (period, amplitude), right (period, amplitude)) -
# the amplitudes as the capture shows them: an 8-bit sample is (x-128) << 8
PHASES = (
    ("16-bit stereo", 4 * RATE, (64, 8000), (16, 600)),
    ("8-bit stereo", 2 * RATE, (32, 40 * 256), (64, 100 * 256)),
    ("16-bit mono", 2 * RATE, (16, 6000), (16, 6000)),
    ("16-bit stereo EXT", 4 * RATE, (32, 5000), (64, 12000)),
    ("16-bit stereo starved", 4 * RATE, (16, 3000), (32, 9000)),
)

bad = []


def check(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what, flush=True)
    if not cond:
        bad.append(what)


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def u32(b, i=0):
    return struct.unpack_from("<I", b, i)[0]


def res_offset():
    d = open(os88build.at(PKG), "rb").read()
    o = d.find(MAGIC)
    if o < 0:
        raise SystemExit("sndfmt: no %r in %s" % (MAGIC, PKG))
    return o


def parse(blk):
    r = {"caps": u16(blk, 8), "done": blk[10], "state": blk[11],
         "r": [u16(blk, 12 + 2 * k) for k in range(4)],
         "r4off": u16(blk, 20), "ust": u16(blk, 22), "upl": u16(blk, 24),
         "p": []}
    for k in range(NPH):
        b = 26 + 12 * k
        r["p"].append(dict(zip(("v11", "v0", "st", "played", "t0", "t1"),
                               struct.unpack_from("<6H", blk, b))))
    return r


def find_seg(read, wins_addr, off):
    """The segment of the window whose image carries the results block."""
    raw = read(wins_addr, G.MAX_WIN * G.WIN_SIZE)
    for i in range(G.MAX_WIN):
        b = i * G.WIN_SIZE
        if not u16(raw, b + G.W_FLAGS) & G.WF_USED:
            continue
        seg = u16(raw, b + G.W_SEG)
        if seg and read((seg << 4) + off, len(MAGIC)) == MAGIC:
            return seg
    return None


# --- the capture ----------------------------------------------------------------
def read_capture(path):
    """(left, right) as lists of ints. QEMU leaves the RIFF and data sizes 0
    when it is stopped by a signal, so the chunks are walked by hand and the
    data runs to the end of the file."""
    raw = open(path, "rb").read()
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise SystemExit("sndfmt: %s is not a RIFF WAVE file" % path)
    i, fmt = 12, None
    while i + 8 <= len(raw):
        cid, size = raw[i:i + 4], u32(raw, i + 4)
        if cid == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", raw, i + 8)
        elif cid == b"data":
            break
        i += 8 + size + (size & 1)
    if fmt is None or cid != b"data":
        raise SystemExit("sndfmt: %s has no fmt/data chunks" % path)
    tag, ch, rate, _, _, bits = fmt
    if (tag, ch, rate, bits) != (1, 2, RATE, 16):
        raise SystemExit("sndfmt: the capture is PCM %d, %d channel(s), %d Hz,"
                         " %d bits - not 2 x %d x 16" % (tag, ch, rate, bits,
                                                         RATE))
    data = raw[i + 8:]
    n = len(data) // 4
    s = struct.unpack("<%dh" % (2 * n), data[:4 * n])
    return list(s[0::2]), list(s[1::2])


def fit(x, period):
    """Least-squares a sin + b cos + c at an exact PERIOD (in samples) over a
    whole number of them: (amplitude, rms of what is left)."""
    n = len(x) - len(x) % period
    x = x[:n]
    w = 2 * math.pi / period
    a = 2.0 / n * sum(v * math.sin(w * k) for k, v in enumerate(x))
    b = 2.0 / n * sum(v * math.cos(w * k) for k, v in enumerate(x))
    c = sum(x) / n
    resid = math.sqrt(sum((v - a * math.sin(w * k) - b * math.cos(w * k) - c)
                          ** 2 for k, v in enumerate(x)) / n)
    return math.hypot(a, b), resid


def analyse(path, played):
    """QEMU's wav backend writes only while a voice is open, so the three
    phases arrive BACK TO BACK, with no silence between them. They are cut
    where verb 9 says each one ended - its bytes played, as frames - and
    only each one's middle second is judged, which a cut half a second out
    still leaves inside its own phase."""
    left, right = read_capture(path)
    frames = [n // (w // RATE) for n, (_, w, _, _) in zip(played, PHASES)]
    print("capture: %.2f s; verb 9 says %s" % (
        len(left) / RATE, " + ".join("%.2f" % (f / RATE) for f in frames)))
    check(abs(len(left) - sum(frames)) <= 0.05 * sum(frames),
          "the capture is as long as verb 9's three phases (%d frames "
          "against %d)" % (len(left), sum(frames)))
    edge = 0
    for (name, _, (lp, la), (rp, ra)), f in zip(PHASES, frames):
        a, b = edge, edge + f
        edge = b
        mid = (a + b) // 2
        lo, hi = mid - RATE // 2, mid + RATE // 2       # its middle second
        L, R = left[lo:hi], right[lo:hi]
        for side, x, per, amp, other in (("left", L, lp, la, rp),
                                         ("right", R, rp, ra, lp)):
            got, resid = fit(x, per)
            print("  %s %s: %.1f Hz at %.0f (want %.0f), residual %.1f"
                  % (name, side, RATE / per, got, amp, resid))
            check(0.75 * amp <= got <= 1.25 * amp,
                  "%s: the %s channel carries %.1f Hz at %.0f (want %.0f)"
                  % (name, side, RATE / per, got, amp))
            if other != per:
                leak, _ = fit(x, other)
                check(leak < 0.02 * amp + 8,
                      "%s: the %s channel carries none of the other side's "
                      "%.1f Hz (%.1f)" % (name, side, RATE / other, leak))
        if name == "16-bit stereo":
            got, resid = fit(R, rp)
            snr = 20 * math.log10(got / max(resid, 0.01) / math.sqrt(2))
            check(snr >= 45, "16-bit stereo: the -35 dBFS right channel is "
                  "%.1f dB above its residual - more than 8 bits can carry"
                  % snr)


# --- the two machines ------------------------------------------------------------
def run_qemu(off):
    import os88qemu
    import os88sym
    import vidxms
    import xmcheck
    with tempfile.TemporaryDirectory(prefix="sndfmt-", dir=os.path.join(
            ROOT, "build")) as t:
        sy, ap = os.path.join(t, "sys.img"), os.path.join(t, "apps.img")
        shutil.copy(os88build.at("build/sndfmtsys.img"), sy)
        shutil.copy(os88build.at("build/sndfmt.img"), ap)
        os.makedirs(EVIDENCE, exist_ok=True)   # kept: the capture is the
        wav = os.path.join(EVIDENCE, "sb16.wav")    # evidence, listenable
        if os.path.exists(wav):
            os.unlink(wav)
        q = vidxms.Q(t, sy, ap, 16, (
            "-audiodev", "wav,id=snd,path=%s,out.frequency=%d,out.channels=2,"
            "out.format=s16" % (wav, RATE),
            "-device", "sb16,audiodev=snd"))
        os88qemu.own(q.pid, q.sock)
        try:
            xmcheck.wait_desktop(q.sock, "sndfmt")
            box = {}

            def found():
                box["seg"] = find_seg(q.read, os88sym.linear("wm_wins"), off)
                return box["seg"] is not None
            if not os88qemu.acted(q, found, secs=30,
                                  what="SNDFMT's window (\\AUTOSTRT)"):
                raise SystemExit("sndfmt: the autostart never opened SNDFMT")
            seg = box["seg"]
            print("SNDFMT in segment %04x" % seg)

            def done():
                return q.readseg(seg, off + 10, 1)[0] == 1
            if not os88qemu.acted(q, done, secs=40, what="the run's end"):
                raise SystemExit("sndfmt: the run never finished (state %d)"
                                 % q.readseg(seg, off + 11, 1)[0])
            blk = q.readseg(seg, off, 26 + 12 * NPH)
        finally:
            q.close()
        r = parse(blk)
        report(r, card="sb16")
        if not os.path.exists(wav):
            check(False, "QEMU wrote the capture")
        else:
            analyse(wav, played(r))


def run_marty(off):
    import os88marty
    import os88ui
    with os88ui.boot("build/mixer360.img", apps="build/sndfmt360.img",
                     machine=os88marty.machine("os8088_5150_sb_gla"),
                     verbose=False) as ui:
        m = ui.m
        w = ui.path("B:/SNDFMT.O88")
        raw = m.read(m.sym("wm_wins"), G.MAX_WIN * G.WIN_SIZE)
        seg = u16(raw, w.i * G.WIN_SIZE + G.W_SEG)
        print("SNDFMT in segment %04x" % seg)
        if m.readseg(seg, off, len(MAGIC)) != MAGIC:
            raise SystemExit("sndfmt: no results block in that segment")
        os88marty.until(m, lambda _: m.readseg(seg, off + 10, 1)[0] == 1,
                        "the run's end", limit=60)
        report(parse(m.readseg(seg, off, 26 + 12 * NPH)), card="dsp2")


def played(r):
    """Each phase's bytes played, unwrapped: verb 9 is free-running mod
    65536, and what a phase's ticks imply says how many wraps to add. The
    starved phase played its ring once and then its resumed ticks."""
    out = []
    for (name, rate, _, _), p in zip(PHASES, r["p"]):
        live = (p["t1"] - p["t0"]) & 0xFFFF
        if "starved" in name:
            live -= STARVE
        want = rate * live / TICK + ("starved" in name) * 16384
        out.append(p["played"] + 65536 * round((want - p["played"]) / 65536))
    return out


def report(r, card):
    print("caps %04x  R1..R4 %s  R4 offset %04x  starved %04x at %d" % (
        r["caps"], " ".join("%04x" % v for v in r["r"]), r["r4off"],
        r["ust"], r["upl"]))
    for (name, rate, _, _), p in zip(PHASES, r["p"]):
        print("  %-14s v11 %04x v0 %04x state %04x played %5d in %d ticks"
              % (name, p["v11"], p["v0"], p["st"], p["played"],
                 (p["t1"] - p["t0"]) & 0xFFFF))
    if card == "sb16":
        check(r["caps"] & (CAP_STEREO | CAP_PCM16) == CAP_STEREO | CAP_PCM16,
              "OSAPI_SND_CAPS has SND_CAP_STEREO and SND_CAP_PCM16 (DSP 4.05,"
              " DMA 5)")
        check(r["r"][0] == 7, "verb 11 with an unknown bit answers 7")
        check(r["r"][1] == 7, "a format on a LINEAR open answers 7")
        check(r["r"][2] == 2, "16-bit stereo at 4,500 Hz answers 2")
        check(r["r4off"] & 1 == 1 and r["r"][3] == 7,
              "a 16-bit ring at an odd offset (%04x) answers 7" % r["r4off"])
        for (name, rate, _, _), p in zip(PHASES, r["p"]):
            check(p["v11"] == 0, "%s: verb 11 answered 0" % name)
            check(p["v0"] & 0xFF == 0, "%s: verb 0 opened it" % name)
            check(p["st"] == 0, "%s: still PLAYING at its end" % name)
            ticks = (p["t1"] - p["t0"]) & 0xFFFF
            # verb 9 is free-running mod 65536: the phase plays ~2.2 s, so
            # add the wraps its ticks imply before comparing
            want = rate * ticks / TICK
            got = p["played"] + 65536 * round((want - p["played"]) / 65536)
            if "starved" in name:
                # fed for its last PLAY - STARVE ticks only; before that the
                # ring's 16KB played once and the ISR halted it (D5h)
                resumed = (p["played"] - r["upl"]) & 0xFFFF
                live = rate * (ticks - STARVE) / TICK
                resumed += 65536 * round((live - resumed) / 65536)
                check(r["ust"] == 1, "%s: verb 9 read UNDERRUN-PAUSED (%d) "
                      "while it was unfed" % (name, r["ust"]))
                check(abs(resumed - live) <= 0.25 * live,
                      "%s: fed again it RESUMED - %d bytes in the %d ticks "
                      "after (want ~%.0f)" % (name, resumed, ticks - STARVE,
                                              live))
                continue
            check(ticks and abs(got - want) <= 0.2 * want,
                  "%s: %d bytes played in %d ticks - %.0f a second (want %d)"
                  % (name, got, ticks, got * TICK / max(ticks, 1), rate))
    else:
        check(r["caps"] & (CAP_STEREO | CAP_PCM16) == 0,
              "a DSP 2.01 publishes neither SND_CAP_STEREO nor SND_CAP_PCM16")
        check(r["r"][0] == 7, "verb 11 with an unknown bit answers 7")
        check(all(v & 0xFF == 0 for v in r["r"][1:]),
              "R2-R4 opened as 8-bit mono: a refused verb 11 holds nothing")
        for (name, _, _, _), p in zip(PHASES, r["p"]):
            check(p["v11"] == 2, "%s: verb 11 answers 2 on a DSP 2.01" % name)
            check(p["v0"] == 0, "%s: and nothing was opened" % name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--marty", action="store_true",
                    help="the DSP 2.01 refusal leg on MartyPC")
    a = ap.parse_args()
    off = res_offset()
    if a.marty:
        run_marty(off)
    else:
        run_qemu(off)
    if bad:
        print("sndfmt: %d FAILED" % len(bad))
        sys.exit(1)
    print("sndfmt: all passed")


if __name__ == "__main__":
    main()
