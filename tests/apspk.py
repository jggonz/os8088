#!/usr/bin/env python3
"""AUDIO ON THE PC SPEAKER, with no card - SPEC.md 86.21, 34.11.9.

    make && python3 tests/apspk.py [--legs pcm8,box3,...] [--secs S]

On MartyPC's card-less Hercules 5150 with a fixed disk. Each leg puts a WAV
on the disk, double-clicks it, and Audio plays it through the speaker in its
bracket - on its own, with nothing pressed, because no card means the
speaker (the owner's rule). One trace a leg: the door's open, then PULSES
writes to port 42h, then the door's close. What must hold:

  1. the writes are THE MODEL'S COUNTS, in order: tools/os88spkfx.py's plan,
     resampler and Shaper over the file's samples (after at most a few dry
     pulses of silence while the ring starts);
  2. the pulses lost to IF = 0 stay under LOSS;
  3. the play keeps time: once the ladder (SPEC.md 86.21) has found the rung
     this machine can hold, the ring never runs dry but at the list's end -
     and a play that needed no rung takes the sound's own time, door open to
     door close;
  4. the kernel is left clean: channel 2 free, no sample ISR.

The legs: pcm8 (8,000 Hz, pre-emphasis, 1:1), box3 (22,050 -> 7,350, three
averaged), step (11,025 stepped to 8,000), adpcm (IMA 11,025 stepped to 5,512
- an 8088 pays the decode at the source rate), shaped (os88spkfx.py shape:
the tables alone) and counts (shape --counts: copied).

APSPK_PROF=1 is an instrument, not a check: each leg is played with no trace
and its guest sampled from outside instead (flat_ip, ~150 a second, the
guest charged nothing), bucketed by the package's labels, the kernel's and
the ROM's - where a play's time goes when it does not keep up.

--pause also reads the menu bar once the bracket is up: the progress
widget the file's read armed must be gone from it (SPEC.md 86.21.2). Broken
on purpose - apu_repaint's OSAPI_WM_CLIP_CLEAR taken out - it FAILS on the
widget's black pixels.

Broken on purpose - aps_put's copy made to skip os88spkfx_emit (raw samples
in the ring) - 1 FAILS on every shaped leg; aps_dobox's average replaced by
the first sample of the k - box3 FAILS at 1.
"""
import argparse
import math
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88build, os88vid as vid  # noqa: E402
import os88spkfx as fx                               # noqa: E402
import os88geom as geom                              # noqa: E402
from cycweb import pkg_syms                          # noqa: E402

PULSES = 800
LOSS = 0.04
DRYMAX = 40         # dry grants the list's end may take: its last span
                    # played out, then the bracket's next pass sees it
MBAR_H = 20         # kernel.asm: the menu bar's height
FPG_W = 80          # fprog.inc: the progress widget's width
MACHINE = "os8088_5150_herc_hdd_gla"
MACHINE_SB = "os8088_5150_herc_hdd_sb_gla"
TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"


def song(n, rate, seed=8088):
    """a deterministic stand-in for music at `rate`: a bass, a moving line,
    a noise burst and a gap of silence - every level and the gate"""
    out = bytearray()
    for j in range(n):
        t = j / float(rate)
        sec = int(t * 2) % 8
        if sec == 5:
            v = 0.0
        else:
            env = [0.9, 0.3, 0.08, 0.6, 1.0, 0, 0.4, 0.15][sec]
            f = 330 * (1 + 0.5 * math.sin(2 * math.pi * 0.7 * t))
            v = env * (0.5 * math.sin(2 * math.pi * 70 * t) +
                       0.35 * math.sin(2 * math.pi * f * t) +
                       0.1 * math.sin(2 * math.pi * 1900 * t))
            if sec == 3:
                seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
                v += 0.3 * ((seed >> 16) / 32768.0 - 0.5)
        out.append(max(0, min(255, int(round(128 + 127 * v)))))
    return bytes(out)


def wav(path, rate, data, spk=0):
    fx.write_wav(path, rate, data, spk, fx.spk_n(rate) if spk else 0)


IMA_STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34,
            37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
            157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494,
            544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552,
            1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
            4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
            12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086,
            29794, 32767]
IMA_IDX = [-1, -1, -1, -1, 2, 4, 6, 8] * 2


def ima_decode(data, balign):
    """apps/audio/apdec.inc to the byte: the IMA/DVI specification's shifts,
    (pred >> 8) + 128. NOT ffmpeg's adpcm_ima_wav, which rounds its step
    differently and disagrees by one in ~5% of samples - SPEC.md 86.4's
    "byte-identical to ffmpeg" measured false"""
    out = []
    for b in range(0, len(data) - 3, balign):
        blk = data[b:b + balign]
        pred, idx = struct.unpack_from("<h", blk, 0)[0], min(blk[2], 88)
        out.append(((pred >> 8) + 128) & 255)
        for byte in blk[4:]:
            for nib in (byte & 15, byte >> 4):
                step = IMA_STEP[idx]
                diff = step >> 3
                if nib & 4:
                    diff += step
                if nib & 2:
                    diff += step >> 1
                if nib & 1:
                    diff += step >> 2
                pred = max(-32768, min(32767, pred - diff if nib & 8
                                       else pred + diff))
                idx = max(0, min(88, idx + IMA_IDX[nib]))
                out.append(((pred >> 8) + 128) & 255)
    return bytes(out)


def adpcm(tmp, path, rate, data):
    """IMA ADPCM, encoded by ffmpeg and decoded as apdec.inc decodes it"""
    src = os.path.join(tmp, "src.wav")
    wav(src, rate, data)
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", src, "-c:a",
                    "adpcm_ima_wav", path], check=True)
    d = open(path, "rb").read()
    i, balign, body = 12, 0, b""
    while i + 8 <= len(d):
        cid, n = d[i:i + 4], struct.unpack_from("<I", d, i + 4)[0]
        if cid == b"fmt ":
            balign = struct.unpack_from("<H", d, i + 20)[0]
        elif cid == b"data":
            body = d[i + 8:i + 8 + n]
        i += 8 + n + (n & 1)
    return ima_decode(body, balign)


def expect(xs, src, at=False, adpcm_=False, spk=0, rung=0):
    """the counts Audio makes of samples xs at src Hz: SPEC.md 86.21's plan"""
    if spk == 2:
        return xs, src
    tops = [24858, 16000, 11025, 8000, 5512] if at else [8000, 5512, 4800]
    top = tops[max(rung, 1 if adpcm_ and not at else 0)]
    if src <= top:
        r, k = src, 1
    else:
        k = -(-src // top)
        if src % k == 0 and src // k >= 4679 and k <= 3:
            r = src // k
        else:
            r, k = top, 0
    ys = fx.resample(xs, src, r, k)
    return fx.Shaper(r, fx.PRE_NONE if spk == 1 else fx.PRE_DIFF).feed(ys), r


# A LEG THAT MAY LAG: 22 KB/s off MartyPC's XT-IDE, whose every byte the CPU
# copies (SPEC.md 98.2.15.5), cannot be kept up with beside 8088 pulses at any
# rung; the rule (SPEC.md 86.21) is that PCM then plays on regardless. What
# this leg asserts is that rule - to the list's end, with no refusal - and
# only prints its dry count. A DMA disk (the owner's ST11M) is another answer
LAGS = ("box3",)
LEGS = {    # name: (source rate, kind)
    "pcm8": (8000, "pcm"), "box3": (22050, "pcm"), "step": (11025, "pcm"),
    "adpcm": (11025, "adpcm"), "shaped": (8000, "shaped"),
    "counts": (8000, "counts")}


def prof(m, ui, i, name):
    """APSPK_PROF: sample the guest's flat_ip through one play"""
    import bisect
    import collections
    import time
    import os88sym
    syms, _ = pkg_syms("apps/audio/audio.asm", ("apps/", "apps/audio/"))
    pks = sorted((v, k) for k, v in syms.items())
    pkv = [v for v, k in pks]
    ksy = sorted((v, k) for k, v in os88sym.syms().items()
                 if isinstance(v, int) and v < 0x10000)
    kkv = [v for v, k in ksy]
    w = ui.path("C:/T%d.WAV" % i)
    rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE, geom.WIN_SIZE)
    base = (rec[geom.W_SEG] | rec[geom.W_SEG + 1] << 8) << 4
    seg = m.sym("spk_seg")
    t0 = time.time()
    while m.read(seg, 2) == b"\0\0" and time.time() - t0 < 60:
        time.sleep(0.05)
    hits, n = collections.Counter(), 0
    c0 = m.status()["cycles"]
    while m.read(seg, 2) != b"\0\0" and time.time() - t0 < 600:
        st = m.status()
        ip = st["flat_ip"]
        n += 1
        if base <= ip < base + 0x10000:
            nm = "pkg:" + pks[bisect.bisect_right(pkv, ip - base) - 1][1]
        elif 0x600 <= ip < 0x600 + 0x10000:
            nm = "k:" + ksy[bisect.bisect_right(kkv, ip - 0x600) - 1][1]
        elif ip >= 0xF0000:
            nm = "ROM"
        else:
            nm = "%05x" % (ip & 0xFFF00)
        hits[nm] += 1
    secs = (m.status()["cycles"] - c0) / 4772727.0
    print("   %s: %.2f guest s, %d samples" % (name, secs, n))
    for nm, c in hits.most_common(25):
        print("      %5.1f%%  %s" % (100.0 * c / n, nm))


def encoded(tmp, path, rate, xs):
    """a speaker WAV made by THE ENCODER (os88venc.py IN OUT.WAV, SPEC.md
    86.21.1), from xs at `rate`: its counts, read back out of the file"""
    src = os.path.join(tmp, "src%d.wav" % rate)
    wav(src, rate, xs)
    subprocess.run([sys.executable, "tools/os88venc.py", src, path,
                    "--rate", str(rate), "--quiet"], check=True)
    d = open(path, "rb").read()
    i = d.index(b"o8sp")
    kind, pulses, n = struct.unpack_from("<BBH", d, i + 8)
    if (kind, pulses, n) != (2, 1, fx.spk_n(rate)):
        raise SystemExit("the encoder's o8sp chunk is %r" % ((kind, pulses,
                                                               n),))
    j = d.index(b"data")
    return d[j + 8:j + 8 + struct.unpack_from("<I", d, j + 4)[0]]


def cinv(n):
    """apps/audio/apengine.inc's ap_cinv: a count back to its sample"""
    return bytes(min(255, (max(c - 1, 0) * 255 + (n - 2) // 2) // (n - 2))
                 for c in range(256))


def cardcounts(a):
    """--cardcounts: an encoder-made counts WAV on a CARD (86.21.1) - the
    first half staged to the card is the file's counts turned back into
    samples, byte for byte"""
    os.chdir(ROOT)
    bad = []
    syms, _ = pkg_syms("apps/audio/audio.asm", ("apps/", "apps/audio/"))
    rate = 8000
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        path = os.path.join(tmp, "T0.WAV")
        c = encoded(tmp, path, rate, song(int(rate * a.secs), rate))
        want = c[:2048].translate(cinv(fx.spk_n(rate)))
        vhd = os.path.join(tmp, "apq.vhd")
        subprocess.run(
            [sys.executable, "tools/os88hdd.py", "--template", TEMPLATE,
             "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
             "--vbr", os88build.at("build/boothd.bin"),
             "--mbr", os88build.at("build/mbr.bin"),
             "--file", "SOUND.DRV=" + os88build.at("build/sound.drv"),
             "--file", "AUDIO.O88=" + os88build.at("build/audio.o88"),
             "--file", "T0.WAV=" + path], check=True, capture_output=True)
        m = os88marty.launch(None, machine=MACHINE_SB, extra=[
            "--mount", "hd:0:" + os.path.abspath(vhd)])
        try:
            ui = os88ui.UI(m)
            ui.ready(limit=240)
            k_st, k_spk = m.sym("osapi_snd_stream"), m.sym("osapi_fsx_spk")
            ev = {"half": None, "spk": 0, "base": None}

            def hit(mm, rec):
                r_ = mm.regs()
                if rec.get("addr") == k_spk:
                    ev["spk"] += 1
                elif r_["ax"] & 0xFF == 6 and ev["half"] is None:
                    # verb 6, the first stage: the half is apd_out, in the
                    # CALLER's segment (the X stub's ES)
                    ev["half"] = mm.read((r_["es"] << 4) + syms["apd_out"],
                                         2048)
            with os88marty.bp_trace(m, k_st, k_spk, on_hit=hit) as tr:
                ui.path("C:/T0.WAV")
                tr.until(lambda: ev["half"] is not None, "the first half",
                         limit=300.0)
            got = ev["half"]
            diff = [i for i in range(2048) if got[i] != want[i]]
            print("   cardcounts: the first half staged %s (%d of 2048 "
                  "differ%s); the speaker's door %d" % (
                      "EXACT" if not diff else "WRONG", len(diff),
                      ", first at %d: %d for %d" % (diff[0], got[diff[0]],
                                                    want[diff[0]])
                      if diff else "", ev["spk"]))
            if diff or ev["spk"]:
                bad.append("the counts did not reach the card as samples")
        finally:
            m.close()
    for b in bad:
        print("   FAIL: " + b)
    print("apspk: %s" % ("FAIL" if bad else "ok"))
    return 1 if bad else 0


def card(a):
    """--card: the same open path (ap_prep_track, SPEC.md 86.21) with a card"""
    os.chdir(ROOT)
    bad = []
    syms, _ = pkg_syms("apps/audio/audio.asm", ("apps/", "apps/audio/"))
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        path = os.path.join(tmp, "T0.WAV")
        wav(path, 11025, song(int(11025 * a.secs), 11025))
        vhd = os.path.join(tmp, "apc.vhd")
        subprocess.run(
            [sys.executable, "tools/os88hdd.py", "--template", TEMPLATE,
             "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
             "--vbr", os88build.at("build/boothd.bin"),
             "--mbr", os88build.at("build/mbr.bin"),
             "--file", "SOUND.DRV=" + os88build.at("build/sound.drv"),
             "--file", "AUDIO.O88=" + os88build.at("build/audio.o88"),
             "--file", "T0.WAV=" + path], check=True, capture_output=True)
        m = os88marty.launch(None, machine=MACHINE_SB, extra=["--mount", "hd:0:" + os.path.abspath(vhd)])
        try:
            ui = os88ui.UI(m)
            ui.ready(limit=240)
            ev = {"stream": 0, "spk": 0}
            k_st, k_spk = m.sym("osapi_snd_stream"), m.sym("osapi_fsx_spk")

            def hit(mm, rec):
                r_ = mm.regs()
                if rec.get("addr") == k_st and r_["ax"] & 0xFF == 0:
                    ev["stream"] += 1
                elif rec.get("addr") == k_spk:
                    ev["spk"] += 1
            with os88marty.bp_trace(m, k_st, k_spk, on_hit=hit) as tr:
                w = ui.path("C:/T0.WAV")
                rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                             geom.WIN_SIZE)
                base = (rec[geom.W_SEG] | rec[geom.W_SEG + 1] << 8) << 4
                rw = lambda n_: (lambda b: b[0] | b[1] << 8)(
                    m.read(base + syms[n_], 2))
                tr.until(lambda: rw("ap_msg") == syms["ap_s_endlist"],
                         "the list's end", limit=600.0)
            print("   card: the stream opened %d time%s, the speaker's door "
                  "%d; it ended on the list's end" % (
                      ev["stream"], "" if ev["stream"] == 1 else "s",
                      ev["spk"]))
            if ev["stream"] != 1 or ev["spk"]:
                bad.append("the play was not the card's")
        finally:
            m.close()
    for b in bad:
        print("   FAIL: " + b)
    print("apspk: %s" % ("FAIL" if bad else "ok"))
    return 1 if bad else 0


def pause(a):
    """--pause: the imposter window's own keys, SPEC.md 86.21"""
    os.chdir(ROOT)
    bad = []
    syms, _ = pkg_syms("apps/audio/audio.asm", ("apps/", "apps/audio/"))
    secs = max(a.secs, 8.0)
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        xs = song(int(8000 * secs), 8000)
        path = os.path.join(tmp, "T0.WAV")
        wav(path, 8000, xs)
        counts, r = expect(xs, 8000)
        vhd = os.path.join(tmp, "app.vhd")
        subprocess.run(
            [sys.executable, "tools/os88hdd.py", "--template", TEMPLATE,
             "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
             "--vbr", os88build.at("build/boothd.bin"),
             "--mbr", os88build.at("build/mbr.bin"),
             "--file", "AUDIO.O88=" + os88build.at("build/audio.o88"),
             "--file", "T0.WAV=" + path], check=True, capture_output=True)
        m = os88marty.launch(None, machine=MACHINE,
                             extra=["--mount", "hd:0:" + os.path.abspath(vhd)])
        try:
            ui = os88ui.UI(m)
            ui.ready(limit=240)
            k_open, k_off = m.sym("osapi_fsx_spk"), m.sym("spk_off")
            ev = {"open": [], "close": [], "w": [], "cap": 0}

            def hit(mm, rec):
                r_ = mm.regs()
                if rec.get("addr") == k_open and r_["ax"] & 0xFF == 0:
                    ev["open"].append(rec["cycles"])
                    ev["cap"] = 200
                    mm.breakpoints([{"type": "exec", "addr": k_open},
                                    {"type": "exec", "addr": k_off},
                                    {"type": "io", "addr": 0x42}])
                elif rec.get("addr") == k_off:
                    if ev["open"]:          # (os88spk_go closes first: a
                        ev["close"].append(rec["cycles"])   # no-op, before
                elif ev["cap"]:                             # any open)
                    ev["w"].append((len(ev["open"]), r_["ax"] & 0xFF))
                    ev["cap"] -= 1
                    if not ev["cap"]:
                        mm.breakpoints([{"type": "exec", "addr": k_open},
                                        {"type": "exec", "addr": k_off}])
            with os88marty.bp_trace(m, k_open, k_off, on_hit=hit) as tr:
                w = ui.path("C:/T0.WAV")
                rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                             geom.WIN_SIZE)
                base = (rec[geom.W_SEG] | rec[geom.W_SEG + 1] << 8) << 4
                rb = lambda n_: m.read(base + syms[n_], 1)[0]
                rw = lambda n_: (lambda b: b[0] | b[1] << 8)(
                    m.read(base + syms[n_], 2))
                tr.until(lambda: len(ev["open"]) == 1 and not ev["cap"],
                         "the play", limit=300.0)
                os88marty.pace(m, 0.3)
                # THE BAR IS CLEAN (86.21.2): the file's read put the
                # progress widget up, the bracket's door took it down, and
                # nothing of it is left in its span of the menu bar
                x0 = u16r(m, m.sym("fpg_x0"), 0)
                wd, _, fb = m.fbuf()
                dark = lambda x, y: fb[3 * (y * wd + x):3 * (y * wd + x) + 3] \
                    == b"\0\0\0"
                rule = [all(dark(x, y) for x in range(x0, x0 + FPG_W))
                        for y in range(MBAR_H + 8)]
                top = rule.index(False)          # the bar's INTERIOR: below
                bot = rule.index(True, top)      # its top rule, above its
                blk = sum(1 for y in range(top, bot)     # bottom one
                          for x in range(x0, x0 + FPG_W) if dark(x, y))
                print("   the bar: the widget's span at x %d, rows %d-%d, %d "
                      "black pixels left in it" % (x0, top, bot - 1, blk))
                if not x0 or blk:
                    bad.append("the bar: the progress widget was left on "
                               "the menu bar (%d px)" % blk)
                m.type_text(" ")                    # PAUSE: the desktop
                spk = m.sym("spk_seg")
                tr.until(lambda: rb("ap_state") == 2 and
                         m.read(spk, 2) == b"\0\0", "Space to pause",
                         limit=120.0)
                cons = u16r(m, rw("aps_rseg") << 4, 16386)
                drew = rw("ap_last_s")
                print("   pause: the door shut, state %d (2 = paused), the "
                      "session %d, CONS %d, the clock drawn at %d s" % (
                          rb("ap_state"), rb("aps_on"), cons, drew))
                if rb("aps_on") != 1:
                    bad.append("pause: the session did not survive it")
                if drew < 1:
                    bad.append("pause: the window's clock was never drawn "
                               "in the bracket")
                os88marty.pace(m, 0.3)
                m.type_text(" ")                    # RESUME, where it was
                tr.until(lambda: len(ev["open"]) == 2 and not ev["cap"],
                         "Space to resume", limit=120.0)
                again = bytes(c for n_, c in ev["w"] if n_ == 2)
                at = -1
                for lead in range(4):
                    at = counts.find(again[lead:], max(0, cons - 64))
                    if at >= 0:
                        break
                print("   resume: its first pulses are the model's from %d "
                      "(CONS at the pause %d)" % (at, cons))
                if at < 0 or abs(at - cons) > 64:
                    bad.append("resume: not on from where it paused (%d "
                               "against %d)" % (at, cons))
                os88marty.pace(m, 0.1)
                m.key("Escape")                     # STOP
                tr.until(lambda: rb("ap_state") == 0 and rb("aps_on") == 0,
                         "Esc to stop", limit=120.0)
                msg = rw("ap_msg")
                print("   stop: state %d, the session %d, the message %s" % (
                    rb("ap_state"), rb("aps_on"), "Stopped"
                    if msg == syms["ap_s_stopped"] else hex(msg)))
                if rb("aps_on") or msg != syms["ap_s_stopped"]:
                    bad.append("stop: not stopped")
            ch2 = m.read(m.sym("snd_ch2mode"), 1)[0]
            if ch2 or m.read(m.sym("spk_seg"), 2) != b"\0\0":
                bad.append("the speaker was left taken")
        finally:
            m.close()
    for b in bad:
        print("   FAIL: " + b)
    print("apspk: %s" % ("FAIL" if bad else "ok"))
    return 1 if bad else 0


def u16r(m, addr, off):
    b = m.read(addr + off, 2)
    return b[0] | b[1] << 8


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--legs", default=",".join(LEGS))
    ap.add_argument("--secs", type=float, default=5.0)
    ap.add_argument("--cardcounts", action="store_true",
                    help="an encoder-made counts WAV on a card (86.21.1)")
    ap.add_argument("--card", action="store_true",
                    help="a Sound Blaster and SOUND.DRV: the play must be the "
                         "CARD's - the stream opened, the list played to its "
                         "end, and the speaker's door never opened")
    ap.add_argument("--pause", action="store_true",
                    help="Space a second in (back to the desktop, paused), "
                         "Space again (the play resumes AT the sample it "
                         "stopped on), then Esc (stopped, the door shut)")
    a = ap.parse_args()
    if a.cardcounts:
        return cardcounts(a)
    if a.card:
        return card(a)
    if a.pause:
        return pause(a)
    os.chdir(ROOT)
    bad = []
    legs = a.legs.split(",")
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        files, want = [], {}
        for i, name in enumerate(legs):
            rate, kind = LEGS[name]
            xs = song(int(rate * a.secs), rate)
            fn = "T%d.WAV" % i
            path = os.path.join(tmp, fn)
            if kind == "adpcm":
                dec = adpcm(tmp, path, rate, xs)
                want[name] = expect(dec, rate, adpcm_=True)
            elif kind == "shaped":
                sh = bytes(vid.spk_shape(xs, rate))
                wav(path, rate, sh, fx.SPK_SHAPED)
                want[name] = expect(sh, rate, spk=1)
            elif kind == "counts":        # made as a user makes one: by
                c = encoded(tmp, path, rate, xs)  # the encoder (86.21.1)
                want[name] = expect(c, rate, spk=2)
            else:
                wav(path, rate, xs)
                want[name] = expect(xs, rate)
            files += ["--file", fn + "=" + path]
        vhd = os.path.join(tmp, "aps.vhd")
        subprocess.run(
            [sys.executable, "tools/os88hdd.py", "--template", TEMPLATE,
             "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
             "--vbr", os88build.at("build/boothd.bin"),
             "--mbr", os88build.at("build/mbr.bin"),
             "--file", "AUDIO.O88=" + os88build.at("build/audio.o88")]
            + files, check=True, capture_output=True)
        m = os88marty.launch(None, machine=MACHINE,
                             extra=["--mount", "hd:0:" + os.path.abspath(vhd)])
        try:
            ui = os88ui.UI(m)
            ui.ready(limit=240)
            k_open, k_off = m.sym("osapi_fsx_spk"), m.sym("spk_off")
            syms, _ = pkg_syms("apps/audio/audio.asm", ("apps/", "apps/audio/"))
            ex_o = {"type": "exec", "addr": k_open}
            ex_c = {"type": "exec", "addr": k_off}
            io42 = {"type": "io", "addr": 0x42}
            ex_d = {"type": "exec", "addr": -1}
            for i, name in enumerate(legs):
                ev = {"open": [], "close": [], "w": [], "dry": 0}

                def hit(mm, rec):
                    r_ = mm.regs()
                    if rec.get("addr") == k_open and r_["ax"] & 0xFF == 0:
                        ev["open"].append(rec["cycles"])
                        ev["dry"] = 0           # counted from the LAST open
                        if len(ev["open"]) == 1:
                            f = mm.read((r_["ss"] << 4) + r_["sp"], 4)
                            ex_d["addr"] = ((f[2] | f[3] << 8) << 4) + \
                                syms["os88spk_grant.dry"]
                            mm.breakpoints([ex_o, ex_c, ex_d, io42])
                    elif rec.get("addr") == ex_d["addr"]:
                        ev["dry"] += 1
                    elif rec.get("addr") == k_off:
                        if ev["open"]:
                            ev["close"].append(rec["cycles"])
                    elif ev["open"] and len(ev["w"]) < PULSES:
                        ev["w"].append((rec["cycles"], r_["ax"] & 0xFF))
                        if len(ev["w"]) >= PULSES:
                            mm.breakpoints([ex_o, ex_c, ex_d])
                if os.environ.get("APSPK_PROF"):
                    prof(m, ui, i, name)
                    ui.menu_pick("File", "Exit")
                    continue
                with os88marty.bp_trace(m, k_open, k_off, on_hit=hit) as tr:
                    w = ui.path("C:/T%d.WAV" % i)
                    rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                                 geom.WIN_SIZE)
                    base = (rec[geom.W_SEG] | rec[geom.W_SEG + 1] << 8) << 4
                    rb = lambda n_: m.read(base + syms[n_], 1)[0]
                    # THE PLAY'S END is the bracket's: the list done, the
                    # session closed - across any rung the ladder came down
                    tr.until(lambda: ev["close"] and rb("aps_on") == 0
                             and rb("ap_state") == 0,
                             "leg %s's play" % name, limit=900.0)
                rung = rb("aps_rung")
                counts, r = want[name]
                n = fx.spk_n(r)
                got = bytes(c for _, c in ev["w"])
                at, lead = -1, 0
                for lead in range(12):
                    at = counts.find(got[lead:])
                    if at >= 0:
                        break
                cy = ev["w"][-1][0] - ev["w"][0][0] if ev["w"] else 0
                edges = cy / (4.0 * n)
                lost = edges - (len(ev["w"]) - 1)
                secs = (ev["close"][-1] - ev["open"][0]) / 4772727.0
                want_s = len(counts) / (1193182.0 / n)
                falls = len(ev["open"]) - 1
                print("   %-7s %5d Hz -> %5d Hz: %d pulses, after %d silent "
                      "%s; lost %.1f%%; played %.2f s of %.2f%s" % (
                          name, LEGS[name][0], r, len(got), lead,
                          "THE MODEL'S from %d" % at if at >= 0 else
                          "NOT the model's", 100.0 * lost / max(1, edges),
                          secs, want_s, "; came down %d rung%s to rung %d"
                          % (falls, "" if falls == 1 else "s", rung)
                          if falls else ""))
                if at < 0 and os.environ.get("APSPK_DIFF"):
                    for ld in range(3):
                        j = next((q for q in range(len(got) - ld)
                                  if got[ld + q] != counts[q]), None)
                        print("      lead %d: first difference at %s: got %s "
                              "model %s" % (ld, j, list(got[ld + (j or 0):][:16]),
                                            list(counts[(j or 0):][:16])))
                if at < 0:
                    bad.append("1: %s: not the model's counts (first %s; "
                               "model starts %s)" % (
                                   name, list(got[:12]), list(counts[:12])))
                if edges and lost > LOSS * edges:
                    bad.append("2: %s lost %.1f%% of its pulses"
                               % (name, 100.0 * lost / edges))
                # 3: KEEPING TIME is the ring never running dry once the
                # ladder has found the rung - a producer behind the pulses
                # runs it dry over and over; the list's end drains it once
                print("      the ring ran dry %d time%s after the last door "
                      "opened" % (ev["dry"], "" if ev["dry"] == 1 else "s"))
                msg = m.read(base + syms["ap_msg"], 2)
                msg = [k_ for k_, v in syms.items() if v == (msg[0] | msg[1] << 8)
                       and k_.startswith("ap_s_")]
                print("      it ended on %s" % (msg or "?"))
                if msg != ["ap_s_endlist"]:
                    bad.append("3: %s ended on %s, not the list's end"
                               % (name, msg))
                if ev["dry"] > DRYMAX and name not in LAGS:
                    bad.append("3: %s: the ring ran dry %d times" % (
                        name, ev["dry"]))
                if not falls and abs(secs - want_s) > 0.05 * want_s + 0.3:
                    bad.append("3: %s played %.2f s for %.2f" % (name, secs,
                                                                 want_s))
                ui.menu_pick("File", "Exit")
            ch2 = m.read(m.sym("snd_ch2mode"), 1)[0]
            seg = m.read(m.sym("spk_seg"), 2)
            print("   4: channel 2's mode %d, the sample ISR's segment %02x%02x"
                  % (ch2, seg[1], seg[0]))
            if ch2 or seg != b"\0\0":
                bad.append("4: the speaker was left taken")
        finally:
            m.close()
    for b in bad:
        print("   FAIL: " + b)
    print("apspk: %s" % ("FAIL" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
