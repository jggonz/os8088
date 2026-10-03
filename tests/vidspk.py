#!/usr/bin/env python3
"""VIDEO.O88's SOUND THROUGH THE PC SPEAKER - SPEC.md 34.11, 98.3.15.

    make && python3 tests/vidspk.py [--rate 5512] [--keep DIR]

A clip made here - a Hercules canvas, and PCM8 sound that is a sine sweep -
goes on a bootable fixed disk with VIDEO.O88 and NO sound driver, on the
Hercules 5150 with no card, and plays in the full screen through the
speaker (OSAPI_FSX_SPK and apps/os88spk.inc) with MartyPC's speaker output
captured (MARTYPC_WAV). What must hold:

  1. every frame drawn, no stall, no error - and the speaker, not silence,
     was the clock: [vp_snd] = 2 as the play ran;
  2. THE SOUND IS THE FILE'S: 800 writes to port 42h mid-play are the clip's
     samples through the speaker shaper (SPEC.md 34.11.9, tools/os88spkfx.py
     deciding each frame's audio as vp_aput does), IN ORDER - or, for a clip
     of counts, the file's bytes as they are - and the capture moves;
  3. the play took the SOUND's time: the clip's samples at the rate the
     speaker really runs (1,193,182 / N) within 2%;
  4. the kernel is left as it was: channel 2 back to tone-idle and nobody's
     (snd_ch2mode 0), no sample ISR (spk_seg 0).

--silent plays the same clip with S pressed first, in the window: the play
must be SILENT ([vp_snd] = 0) and its capture flat. --counts makes the clip
FOR the speaker (98.1.1.3): its PCM8 is stored as the counts, the player
copies them, and the pulses must be the file's bytes as they are.
--route-spk and --cp-spk put a Sound Blaster in the machine with SOUND.DRV
loaded and choose PC Speaker - in SYSTEM.CFG before the boot, or in the
Control Panel's Sound page at run time - and the play must still be the
speaker's: the route is the user's, and the card's presence does not
outrank it (SPEC.md 34.8).
--covox plays the clip on MartyPC's Covox machine (a Covox on LPT2, no card,
`make covoxtest`'s disk, the clip and VIDEO.O88 on a scratch floppy in B:):
the play goes to the DAC (SPEC.md 98.3.15.1) - every frame drawn, the
sample writes on port 378h FOLLOWING THE CLIP'S SWEEP (the leveller's gain
moves a frame at a time, so the oracle is the best alignment's correlation
and not byte equality), the DAC's capture sounding and the speaker's flat,
and the kernel left clean. --counts with it: a speaker-counts clip, copied
to the DAC raw (98.1.1.3's card rule): the writes are the file's bytes.
VIDSPK_APUT=1 is an instrument, not a check: vp_aput's cycles a byte, entry
to exit over 30 calls (27.7 copying counts, 92.8 translating samples), and
VIDSPK_ISR=1 the ISR's fast path, entry to iret over 60 pulses, and
VIDSPK_ATTR=1 the lost pulses over ~3,000, each named by the code the late
one interrupted (SPEC.md 34.11.3 and 34.11.6 came off it).

Broken on purpose - os88spk_isr's `out 0x42, al` taken out - 2 FAILS (the
capture is flat); vp_aput's shaper taken out (raw samples in the
ring) - 2 FAILS (the pulses no longer follow the samples' order).
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
import os88marty, os88ui, os88build, os88vid as vid, os88geom as geom  # noqa: E402
import sndcheck                                               # noqa: E402
import os88spkfx as fx                                        # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402

FPS = 15.0
PULSES = 800        # port writes held to the clip's samples, mid-play...
LOSSN = 3000        # ...and traced for the lost share: the first 800 alone
                    # swung with where a disk read fell in them - the SAME
                    # machine lost 3.6% and 4.9% of them with one package
                    # shifted a few hundred bytes, and exactly 121 of ~3,140
                    # periods (3.85%) both times
LOSS = 0.04         # the pulses a play may lose to IF = 0 (SPEC.md 34.11)
LOSS2 = 0.05        # ...at TWO pulses a sample, whose periods are half as
                    # long, so the same stretches at IF = 0 cost a larger
                    # share: one pulse measures 2.3-3.0% and two 3.7-4.2%
                    # across kernel layouts (2026-09-29), the stretches being
                    # the kernel's (74 of 117 lost right after dskw_nbody's
                    # read, 20 after the keyboard's) and not the player's
MACHINE = "os8088_5150_herc_hdd_gla"
MACHINE_SB = "os8088_5150_herc_hdd_sb_gla"     # the owner's 5150's shape
TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def clip(tmp, secs, rate, spk=False, spkp=1):
    """a 40 x 100 Hercules canvas moving a block, and a sine sweep"""
    nf = int(secs * FPS)
    wb, h = 40, 100
    paths = []
    for f in range(nf):
        cv = bytearray(wb * h)
        x, y = (f * 2) % (wb - 8), (f * 3) % (h - 24)
        for r in range(24):
            cv[(y + r) * wb + x:(y + r) * wb + x + 8] = b"\xFF" * 8
        p = os.path.join(tmp, "f%04d.pbm" % f)
        vid._write_pbm(p, wb, h, bytes(cv))
        paths.append(p)
    n = int(rate * nf / FPS)
    audio = bytearray()
    ph = 0.0
    for i in range(n):
        hz = 150.0 + 900.0 * i / n              # 150 Hz up to 1,050 Hz
        ph += 2 * math.pi * hz / rate
        audio.append(128 + int(100 * math.sin(ph)))
    wav = os.path.join(tmp, "a.wav")
    vid._write_wav(wav, rate, bytes(audio))
    out = os.path.join(tmp, "CLIP.V88")
    vid.encode_frames(paths, out, FPS, wav, "herc", "vidspk clip",
                      audio_fmt=vid.AUD_PCM8, spk=spk, spkp=spkp)
    vid.verify_v88(out)
    return out


CP_I0Y, CP_IROWH, CP_RX, CP_PGX, CP_PR0Y = 6, 14, 96, 4, 26  # kernel/ctrl.inc
CP_SOUND = 4                    # cp_items' record: sched, time, drivers, display, SOUND


def corr(xs, ys):
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    sxx = sum((x - mx) ** 2 for x in xs)
    syy = sum((y - my) ** 2 for y in ys)
    return sxy / ((sxx * syy) ** .5 or 1.0)


def covox(a):
    """the clip through a Covox on LPT2 (98.3.15.1)"""
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = clip(tmp, a.secs, a.rate, a.counts)
        r = vid.Reader(v88)
        audio = b"".join(rec[-r.abytes:] for rec, _, _ in r.records())
        img = os.path.join(tmp, "cvx.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", img,
                        "--size", "720", os88build.at("build/video.o88"),
                        v88], check=True, capture_output=True)
        cap = os.path.join(tmp, "cap")
        os.environ["MARTYPC_WAV"] = cap
        try:
            m = os88marty.launch("build/covoxsys720.img", apps=img,
                                 machine="os8088_5150_herc_covox_720_gla")
        finally:
            os.environ.pop("MARTYPC_WAV", None)
        try:
            ui = os88ui.UI(m)
            ui.ready(limit=240)
            w = ui.path("B:/CLIP.V88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4
            rw = lambda n_: u16(m.read(base + syms[n_], 2))
            rb = lambda n_: m.read(base + syms[n_], 1)[0]
            os88marty.until(m, lambda mm: rb("vp_loaded") == 1, "the header",
                            poll=0.3, limit=300.0, guest=30.0)
            if rb("vp_mute"):
                bad.append("0: opened MUTED (why %d) - a Covox play at %d Hz "
                           "is not too fast for an 8088" % (rb("vp_mwhy"),
                                                            a.rate))
            m.write(base + syms["vp_played"], b"\0")
            ev = {"open": None, "w": []}
            k_open = m.sym("osapi_fsx_spk")

            def hit(mm, rec_):
                r_ = mm.regs()
                if rec_.get("addr") == k_open:
                    if r_["ax"] & 0xFF == 0 and ev["open"] is None:
                        ev["open"] = rec_["cycles"]
                        mm.breakpoints([{"type": "io", "addr": 0x378}])
                elif len(ev["w"]) < PULSES:
                    ev["w"].append(r_["ax"] & 0xFF)
                    if len(ev["w"]) == PULSES:
                        mm.breakpoints([])
            with os88marty.bp_trace(m, k_open, on_hit=hit) as tr:
                m.type_text("p")
                tr.until(lambda: rb("vp_played") == 1, "the play's end",
                         limit=900.0)
            st = {k: rw(k) for k in ("vp_done", "vp_stall")}
            ch2 = m.read(m.sym("snd_ch2mode"), 1)[0]
            isr = u16(m.read(m.sym("spk_seg"), 2))
        finally:
            m.close()
        print("   1: drew %d of %d, stalls %d; %d writes to 378h"
              % (st["vp_done"], r.frames, st["vp_stall"], len(ev["w"])))
        if st["vp_done"] != r.frames or st["vp_stall"]:
            bad.append("1: the play (drew %d, stalls %d)" % (st["vp_done"],
                                                           st["vp_stall"]))
        got = ev["w"]
        if len(got) < PULSES:
            bad.append("2: only %d writes reached the Covox's port" % len(got))
        else:
            body = got[100:]            # past the ring's first silence
            if r.spk:                   # counts: the file's bytes, raw
                ok = bytes(body) in audio
                print("   2: the writes %s the clip's stored counts"
                      % ("ARE" if ok else "are NOT"))
                if not ok:
                    bad.append("2: a counts clip's bytes did not reach the "
                               "DAC as they are")
            else:
                best = max((corr(body, audio[o:o + len(body)]), o)
                           for o in range(0, min(len(audio) - len(body),
                                                 4000)))
                print("   2: the writes follow the sweep at offset %d, "
                      "correlation %.3f" % (best[1], best[0]))
                if best[0] < 0.9:
                    bad.append("2: the DAC's writes do not follow the clip "
                               "(best correlation %.3f)" % best[0])
        for nm, want_loud in (("covox", True), ("pc_speaker", False)):
            rate_, s, _ = sndcheck.load(cap + "." + nm + ".wav")
            seg = s[-int(3 * rate_):]
            mm_ = sum(seg) / max(len(seg), 1)
            rms = (sum((x - mm_) ** 2 for x in seg) / max(len(seg), 1)) ** .5
            print("   3: the %s capture's RMS over the play's end: %.4f"
                  % (nm, rms))
            if want_loud and rms < 0.02:
                bad.append("3: the DAC's capture is silent")
            if not want_loud and rms > 0.01:
                bad.append("3: the PC speaker sounded during a Covox play")
        print("   4: channel 2 %d, sample ISR %04x" % (ch2, isr))
        if ch2 or isr:
            bad.append("4: the kernel was not left clean")
    for b_ in bad:
        print("FAIL " + b_)
    print("vidspk: %s" % ("FAIL" if bad else "ok"))
    return 1 if bad else 0


def cp_speaker(m, ui, bad):
    """Chip menu -> Control Panel -> Sound -> PC Speaker, and close it: what
    the owner did. Every step confirmed off the kernel's own bytes."""
    ui.menu_pick("Apple", "Control Panel")
    w = ui.wait_window("Control Panel")
    cx, cy = w.content[0], w.content[1]
    for ordinal in range(8):            # the Sound row, FOUND: [cp_hide] can
        ui.mo.click(cx + 40, cy + CP_I0Y + ordinal * CP_IROWH + 7, settle=0)
        os88marty.until(m, lambda mm: True, "a beat", poll=0.2, limit=30.0,
                        guest=0.5)
        if m.read(m.sym("cp_sel"), 1)[0] == CP_SOUND:
            break
    else:
        raise RuntimeError("no row of the list is the Sound page")
    rt0 = m.read(m.sym("snd_route"), 1)[0]
    ui.mo.click(cx + CP_RX + CP_PGX + 6, cy + CP_PR0Y + 6, settle=0)
    os88marty.until(m, lambda mm: mm.read(mm.sym("snd_route"), 1)[0] == 1,
                    "the route to become PC Speaker", poll=0.3, limit=120.0,
                    guest=10.0)
    caps = u16(m.read(m.sym("drv_svc"), 2))
    print("   0: the panel moved the route %d -> 1 (PC Speaker); the driver's "
          "caps %04x (PCM_BG %s)" % (rt0, caps, "SET" if caps & 4 else
                                     "clear"))
    if caps & 4:
        bad.append("0: PC Speaker chosen and the card still publishes PCM_BG")
    ui.close(w)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rate", type=int, default=5512)
    ap.add_argument("--secs", type=float, default=4.0)
    ap.add_argument("--silent", action="store_true")
    ap.add_argument("--counts", action="store_true",
                    help="a clip MADE for the speaker: its PCM8 stored as "
                         "the counts (98.1.1.3), which the player copies")
    ap.add_argument("--cp-spk", action="store_true",
                    help="a Sound Blaster, the route chosen in the Control "
                         "Panel at run time, as the owner did")
    ap.add_argument("--route-spk", action="store_true",
                    help="a Sound Blaster installed and SOUND.DRV loaded, the "
                         "Control Panel's route PC Speaker (SYSTEM.CFG 'SR' = "
                         "1): the play must be the speaker's, not the card's")
    ap.add_argument("--full", action="store_true",
                    help="play in the full screen: F, then Space")
    ap.add_argument("--fs-off", action="store_true",
                    help="--full, and S part way through: silence, now")
    ap.add_argument("--unmute", action="store_true",
                    help="M before the play: past VP_SPKMAX, where an 8088 "
                    "defaults to MUTED (98.3.17), the speaker plays anyway")
    ap.add_argument("--fs-on", action="store_true",
                    help="M before the play, F, Space, then M part way "
                    "through: the speaker on from the key at or before it")
    ap.add_argument("--pulses", type=int, default=1,
                    help="a clip made for this many pulses a sample (SPEC.md "
                         "34.11.7): muted by default on an 8088, so M; its "
                         "writes to 42h are each count twice")
    ap.add_argument("--keep", help="copy the capture here")
    ap.add_argument("--covox", action="store_true",
                    help="MartyPC's Covox machine: the clip on the DAC "
                         "(98.3.15.1)")
    a = ap.parse_args()
    if a.covox:
        return covox(a)
    a.full = a.full or a.fs_off or a.fs_on
    if a.pulses > 1:                    # (an 8088 mutes it by default, and
        a.counts, a.unmute = True, True     # only a speaker file has it)
    c_s = 0
    os.chdir(ROOT)
    # ...and a pulse shorter than 74 counts is a 286's (SPEC.md 34.11.8):
    # on this 8088 even M finds the door shut, and the play is SILENT
    shut = (1193182 // a.rate) // a.pulses < 74
    spk = not a.silent and not shut and (a.rate <= 8000 or a.unmute)
    # past VP_SPKMAX an 8088 defaults to MUTED (SPEC.md 34.11.4, 98.3.17),
    # and M is how the user says play it anyway
    fast = a.rate > 8000
    many = a.pulses > 1                 # ...and so does two pulses a sample
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = clip(tmp, a.secs, a.rate, a.counts, a.pulses)
        r = vid.Reader(v88)
        audio = b"".join(rec[-r.abytes:] for rec, _, _ in r.records())
        n = 1193182 // a.rate
        srate = 1193182.0 / n
        vhd = os.path.join(tmp, "spk.vhd")
        extra = []
        if a.cp_spk:                    # A CARD, THE PANEL CLICKED AT RUN
            extra = ["--file", "SOUND.DRV=" + os88build.at("build/sound.drv")]
        if a.route_spk:                 # A CARD, AND THE PANEL SAYING SPEAKER:
            cfg = os.path.join(tmp, "SYSTEM.CFG")   # SOUND.DRV wanted as by
            with open(cfg, "wb") as f:              # default, and one record,
                f.write(b"O88CFG\0\0" + (3).to_bytes(2, "little") +
                        b"SR" + bytes([1, 1, 1]) + b"\0\0")  # 'SR' = 1
            extra = ["--file", "SOUND.DRV=" + os88build.at("build/sound.drv"),
                     "--file", "SYSTEM.CFG=" + cfg]
        subprocess.run(
            [sys.executable, "tools/os88hdd.py", "--template", TEMPLATE,
             "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
             "--vbr", os88build.at("build/boothd.bin"),
             "--mbr", os88build.at("build/mbr.bin"),
             "--file", "CTRL.DRV=" + os88build.at("build/ctrl.drv"),
             "--file", "VIDEO.O88=" + os88build.at("build/video.o88"),
             "--file", "CLIP.V88=" + v88] + extra, check=True,
            capture_output=True)
        cap = os.path.join(tmp, "cap")
        os.environ["MARTYPC_WAV"] = cap
        m = os88marty.launch(None, machine=MACHINE_SB if a.route_spk
                             or a.cp_spk else MACHINE,
                             extra=["--mount", "hd:0:" + os.path.abspath(vhd)])
        os.environ.pop("MARTYPC_WAV", None)
        try:
            ui = os88ui.UI(m)
            ui.ready(limit=240)
            if a.cp_spk:                # THE OWNER'S GESTURE: Control Panel,
                cp_speaker(m, ui, bad)  # Sound, PC Speaker - then the play
            w = ui.path("C:/CLIP.V88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4
            rw = lambda n_: u16(m.read(base + syms[n_], 2))
            rb = lambda n_: m.read(base + syms[n_], 1)[0]
            os88marty.until(m, lambda mm: rb("vp_loaded") == 1, "the header",
                            poll=0.3, limit=300.0, guest=30.0)
            if a.route_spk:
                rt = m.read(m.sym("snd_route"), 1)[0]
                caps = u16(m.read(m.sym("drv_svc"), 2))
                print("   0: the route %d (1 = PC Speaker), the driver's caps "
                      "%04x (PCM_BG %s)" % (rt, caps,
                                            "SET" if caps & 4 else "clear"))
                if rt != 1 or caps & 4:
                    bad.append("0: the card is not off: route %d, caps %04x"
                               % (rt, caps))
            mw = (rb("vp_mute"), rb("vp_mwhy"))
            print("   0: opened muted %d, why %d (2 = too fast for this "
                  "speaker)" % mw)
            if mw != ((1, 2) if fast or many else (0, 0)):
                bad.append("0: muted %d why %d at the open" % mw)
            if a.silent or a.fs_on:
                m.type_text("s")
                os88marty.until(m, lambda mm: rb("vp_mute") == 1,
                                "S to choose silence", poll=0.3,
                                limit=120.0, guest=10.0)
            if a.unmute:
                m.type_text("m")
                os88marty.until(m, lambda mm: rb("vp_mute") == 0,
                                "M to unmute", poll=0.3, limit=120.0,
                                guest=10.0)
            m.write(base + syms["vp_played"], b"\0")
            # ONE TRACE, three phases the callback walks: the door's open
            # (its cycle), then PULSES port writes to 42h (each count and its
            # cycle), then the door's close
            ev = {"open": None, "close": None, "w": []}
            k_open = m.sym("osapi_fsx_spk")
            k_off = m.sym("spk_off")

            def hit(mm, rec):
                r_ = mm.regs()
                if rec.get("addr") == k_open and r_["ax"] & 0xFF == 0:
                    if ev["open"] is None:
                        ev["open"] = rec["cycles"]
                        mm.breakpoints([{"type": "io", "addr": 0x42}])
                elif rec.get("addr") == k_off:
                    ev["close"] = rec["cycles"]
                    mm.breakpoints([])
                elif ev["open"] is not None and ev["close"] is None:
                    ev["w"].append((rec["cycles"], r_["ax"] & 0xFF))
                    if len(ev["w"]) >= (PULSES if a.fs_off else LOSSN):
                        mm.bp_exec(k_off)   # (S at PULSES wants the close)
            arm = [k_open] if spk else []
            if os.environ.get("VIDSPK_ATTR"):     # what the late pulses hit
                import time as _t, collections, bisect as _b
                import os88sym
                m.type_text("p")
                os88marty.until(m, lambda mm: rb("vp_ready") == 1, "play",
                                poll=0.1, limit=300.0, guest=60.0)
                ksy = sorted((v, k) for k, v in os88sym.syms().items()
                             if isinstance(v, int) and v < 0x10000)
                kkv = [v for v, k in ksy]
                pks = sorted((v, k) for k, v in syms.items())
                pkv = [v for v, k in pks]
                isr_ = base + syms["os88spk_isrm" if a.pulses > 1   # (two
                                   else "os88spk_isr"]        # pulses: the toggle)

                def hit(mm, rec):
                    r_ = mm.regs()
                    f = mm.read((r_["ss"] << 4) + r_["sp"], 4)
                    return (u16(f, 2), u16(f, 0))
                with os88marty.bp_trace(m, isr_, on_hit=hit) as tr:
                    t_ = _t.time()
                    while tr.n < 3000 and _t.time() - t_ < 300:
                        _t.sleep(0.5)
                cy = [h["cycles"] for h in tr.hits]
                late = collections.Counter()
                for g_, h in zip([b - a for a, b in zip(cy, cy[1:])],
                                 tr.hits[1:]):
                    lost = int(round(g_ / (4.0 * n / a.pulses))) - 1
                    if lost <= 0:
                        continue
                    cs, ip = h["hit"]
                    if cs << 4 == base:
                        nm = "pkg:" + pks[_b.bisect_right(pkv, ip) - 1][1]
                    elif cs == 0x60:
                        nm = "k:" + ksy[_b.bisect_right(kkv, ip) - 1][1]
                    else:
                        nm = "%04x:%04x" % (cs, ip)
                    late[nm] += lost
                span = cy[-1] - cy[0]
                per = 4 * n // a.pulses
                print("   attr: %d lost of %d periods; after: %s" % (
                    span // per - len(cy) + 1, span // per,
                    late.most_common(12)))
                return 0
            if os.environ.get("VIDSPK_ISR"):      # the fast path, cycles
                m.type_text("p")
                os88marty.until(m, lambda mm: rb("vp_ready") == 1, "play",
                                poll=0.1, limit=300.0, guest=60.0)
                ent = base + syms["os88spk_isr"]
                ext = base + syms["os88spk_isr.ev"] - 1     # its iret
                t = []
                for _ in range(60):
                    m.bp_exec(ent)
                    m.run()
                    m.wait_stop(20.0)
                    c1 = m.status()["cycles"]
                    m.bp_exec(ext)
                    m.run()
                    m.wait_stop(20.0)
                    d = m.status()["cycles"] - c1
                    if d < 1500:                # (an event path: not this)
                        t.append(d)
                m.bp_exec()
                m.run()
                t.sort()
                print("   isr: fast path entry to iret, %d pulses: min %d, "
                      "median %d cycles" % (len(t), t[0], t[len(t) // 2]))
                return 0
            if os.environ.get("VIDSPK_APUT"):     # vp_aput, entry to exit
                m.type_text("p")
                os88marty.until(m, lambda mm: rb("vp_ready") == 1, "play",
                                poll=0.1, limit=300.0, guest=60.0)
                pseg = base
                ent, ext = pseg + syms["vp_aput"], pseg + syms["vp_aput.cp"] - 1
                t = []
                for _ in range(30):
                    m.bp_exec(ent)
                    m.run()
                    m.wait_stop(20.0)
                    r_ = m.regs()
                    c1, n_ = m.status()["cycles"], r_["cx"]
                    m.bp_exec(ext)
                    m.run()
                    m.wait_stop(20.0)
                    t.append((m.status()["cycles"] - c1, n_))
                m.bp_exec()
                m.run()
                cyc = sum(c for c, b in t)
                nb = sum(b for c, b in t)
                print("   aput: %d calls, %d bytes, %.1f cycles a byte "
                      "(interrupts included)" % (len(t), nb, cyc / max(1, nb)))
                return 0
            c_p = m.status()["cycles"]
            with os88marty.bp_trace(m, *arm, on_hit=hit) as tr:
                if a.full:                  # F goes in PAUSED (98.3.6), and
                    m.type_text("f")        # Space is the resume that opens
                    tr.until(lambda: rb("vp_ready") == 1, "the full screen",
                             limit=300.0)   # the door
                    m.type_text(" ")
                    if a.fs_on:             # M a second in: the door opens
                        tr.until(lambda: rw("vp_done") >= FPS,  # at the key
                                 "a second of frames", limit=300.0)  # it
                        c_s = m.status()["cycles"]              # seeks to
                        m.type_text("m")
                    if a.fs_off:            # S once the trace has its pulses:
                        tr.until(lambda: len(ev["w"]) >= PULSES,
                                 "the pulses", limit=300.0)
                        c_s = m.status()["cycles"]
                        m.type_text("s")    # the door closes and the play
                else:                       # goes on silent (98.3.15)
                    m.type_text("p")
                tr.until(lambda: rb("vp_played") == 1, "the play's end",
                         limit=900.0)
            c_e = m.status()["cycles"]
            rsnd = rb("vp_snd")
            st = {k: rw(k) for k in ("vp_done", "vp_stall", "vp_late")}
            snd = 2 if ev["open"] else 0
            ch2 = m.read(m.sym("snd_ch2mode"), 1)[0]
            isr = u16(m.read(m.sym("spk_seg"), 2))
        finally:
            m.close()
        want = 2 if spk else 0
        print("   1: drew %d of %d, stalls %d, late %d; the sound went to %s"
              % (st["vp_done"], r.frames, st["vp_stall"], st["vp_late"],
                 {0: "nothing", 2: "THE SPEAKER"}.get(snd, snd)))
        if st["vp_done"] != r.frames or st["vp_stall"] or snd != want:
            bad.append("1: the play (drew %d, snd %d)" % (st["vp_done"], snd))
        caps = [os.path.join(tmp, f) for f in os.listdir(tmp)
                if f.startswith("cap.") and "speaker" in f]
        if a.keep and caps:
            import shutil
            shutil.copy(caps[0], a.keep)
        span = 0.0
        if caps:                        # THE PLAY'S OWN STRETCH of it: the
            hr, vals, _ = sndcheck.load(caps[0])    # capture runs from
            k = hr / 4772727.0                      # power-on, POST beep
            seg = vals[int(c_p * k):int(c_e * k)]   # and all
            span = max(seg) - min(seg) if seg else 0.0
            if ev["w"]:
                x = int(ev["w"][0][0] * k)
                print("   (the first pulse at capture sample %d; the capture "
                      "is live from %d)" % (x, next(
                          (i for i in range(max(0, x - 200), len(vals))
                           if vals[i]), -1)))
        if not spk:
            print("   2: the capture's level varies by %.3f (silent: flat)"
                  % span)
            if span > 0.05 or ev["w"]:
                bad.append("2: the silent play made a sound")
        else:
            np_ = n // a.pulses             # a PULSE's counts (34.11.7)
            tab = [1 + s_ * (np_ - 2) // 255 for s_ in range(256)]
            if r.spk:                       # counts: copied as they are
                want_c = audio
            else:                           # a card's samples: SHAPED, the
                sh = fx.Shaper(a.rate)      # shaper deciding each frame's
                want_c = b""                # audio before emitting it
                for j in range(0, len(audio), r.abytes):   # (SPEC.md 34.11.9,
                    sh.level(audio[j:j + r.abytes])         # 98.3.15)
                    want_c += sh.emit(audio[j:j + r.abytes])
            if a.counts and not r.spk:
                bad.append("the clip was not made as speaker counts")
            got = bytes(c for _, c in ev["w"][:PULSES])
            if many:                        # each count twice: whole, half
                want_c = bytes(x for x in want_c for _ in range(a.pulses))
            at, lead = -1, 0
            if len(got) == PULSES:          # (the first pulse is a dry one,
                for lead in range(9):       # the table's middle: os88spk_go
                    at = want_c.find(got[lead:])    # starts on silence)
                    if at >= 0:
                        break
            print("   2: %d pulses held; after %d silent, as counts they are"
                  " the clip's samples %s; the capture's level spans %.2f"
                  % (len(got), lead, ("%d.., as the file stores them" if r.spk
                                      else "%d.. through the shaper (34.11.9)") % at
                     if at >= 0
                     else "NOWHERE", span))
            if at < 0:
                bad.append("2: the pulses are not the clip's samples")
            if span < 0.5:
                bad.append("2: the speaker's capture barely moves (%.2f)"
                           % span)
            if len(ev["w"]) > 1:
                cy = ev["w"][-1][0] - ev["w"][0][0]
                edges = cy / (4.0 * (n // a.pulses))
                lost = edges - (len(ev["w"]) - 1)
                print("   3: in them, %.0f of %.0f PIT periods had no pulse "
                      "(%.1f%%)" % (lost, edges, 100.0 * lost / edges))
                # (not after M in the full screen: its 800 pulses are the
                # first after a seek, the ring being read again from the
                # disk while they play - 15.4% measured, SPEC.md 98.3.17)
                if lost > (LOSS2 if a.pulses > 1 else LOSS) * edges \
                        and not fast and not a.fs_on:
                    bad.append("3: %.1f%% of the pulses were lost"
                               % (100.0 * lost / edges))
            if a.fs_on:                     # M: OPENED THERE, in step
                ok_ = ev["open"] and ev["open"] > c_s and (
                    ev["open"] - c_s) / 4772727.0 < 2.0
                print("   3: M opened the speaker %.2f guest s after it was "
                      "pressed" % (((ev["open"] or c_s) - c_s) / 4772727.0))
                if not ok_:
                    bad.append("3: M did not turn the speaker on")
            elif fast and len(ev["w"]) > 1:     # UNMUTED past what an 8088
                print("   3: past VP_SPKMAX: its pulses and its time are "   # keeps
                      "the machine's to lose, and not checked")           # up with
            elif a.fs_off:                  # S: CLOSED THERE, not played out
                ok_ = ev["close"] and ev["close"] > c_s and (
                    ev["close"] - c_s) / 4772727.0 < 1.0 and rsnd == 0
                print("   3: S closed the speaker %.2f guest s after it was "
                      "pressed; the play went on %s" % (
                          ((ev["close"] or c_s) - c_s) / 4772727.0,
                          "silent" if rsnd == 0 else "with vp_snd %d" % rsnd))
                if not ok_:
                    bad.append("3: S did not turn the speaker off")
            elif ev["open"] and ev["close"]:
                secs = (ev["close"] - ev["open"]) / 4772727.0
                want_s = len(audio) / srate
                print("   3: the speaker played %.2f guest s; the sound is "
                      "%.2f s at %.0f Hz (N = %d)" % (secs, want_s, srate, n))
                if abs(secs - want_s) > 0.05 * want_s + 0.1:
                    bad.append("3: the play's time %.2f s against %.2f"
                               % (secs, want_s))
            else:
                bad.append("3: the door was %s" % (
                    "never opened" if not ev["open"] else "never closed"))
        print("   4: after it, channel 2's owner mode %d, the sample ISR's "
              "segment %04x" % (ch2, isr))
        if ch2 or isr:
            bad.append("4: the kernel was left with the speaker taken")
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
