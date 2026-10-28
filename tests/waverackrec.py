#!/usr/bin/env python3
"""waverackrec - SPEC.md 123.7: WaveRack RECORDS - a take onto a hard disk
first and a floppy second, read back off the disk on the host.

    make && make build/mixercfg/SYSTEM.CFG && python3 tests/waverackrec.py
        [--leg hdd|bigclu|floppy|noinput] [--keep] [--shots DIR]

MARTYPC BY NAME for every leg but one, because a take is a matter of TIME:
the card writes the ring by DMA while disk.inc holds [sch_lock] across every
int 13h, and the ring only has to outlast the longest write plus the UI's
gap. MartyPC's Sound Blaster is a DSP 2.01, auto-init, whose ADC records a
FLAT 80h (tests/sndcap.py), so every take's content is known before it is
made. ITS HARD DISK HAS NO MECHANICAL MODEL (docs/MARTYPC-DEBUG.md): XT-IDE,
PIO, every byte copied by the CPU with [sch_lock] held - a harsh model of a
write's stall. Counts are exact; milliseconds are not a field figure.

Every take is read back off the disk on the host and held to
tools/wrsim.py's take_header byte for byte - RIFF, WAVE, fmt, JUNK to one
cluster, data - with its data all 80h and its frames equal to what WaveRack
wrote, which must equal the card's own count (SND_EXT_CONS, folded into
[wc_rcap]; the sum of the streams across a pause). Overruns must be 0
([wc_why] names none), and [wc_lagmax] - the ring's fullest - is printed
against the ring.

  hdd      os8088_5150_herc_hdd_sb_gla (XT-IDE, a Sound Blaster 2.0), a VHD of
           SMALL clusters (tools/os88hdd.py off MartyPC's template) holding
           BIG22.WAV:
           - 60 s at 11,025 Hz, the tick's own cost priced on the way by
             breakpoints (wc_copy a byte, wc_tick, the worker's wc_feed,
             wc_write, wc_draw), and
             INPUT GAIN's table read out of the ring claim at +4 dB against
             tools/wrsim.py's gain_table
           - 60 s at 15,000 Hz, verb 4's top rate on a DSP 2.00+
           - a take while a SECOND WaveRack opens BIG22.WAV (2 MB scanned)
           - Pause and Record again: the frames are the two streams' counts
             summed, and nothing moves while paused
           - RECOVER: a take killed with the machine; the VHD read on the
             host (RIFF 0, data 0, whole writes), the machine booted again,
             File > Recover Take live, the take opened dirty
             ('Recovered; save to fix') with the file's frames, and
             WAVERACK.TAK marked done
  bigclu   os8088_xt_vga_hdd_sb (a 4.77 MHz 5160, VGA, XT-IDE, SB 2.01) on a
           321 MB VHD of 32 KB CLUSTERS (os88hdd.py --spc 64): 60 s at
           11,025 Hz and 60 s at 15,000 Hz - the header one 32 KB cluster,
           every write 32 KB out of the output claim. The colour face is
           photographed mid-take
  floppy   os8088_5150_herc_sb_gla_144: a 1.44 MB floppy in B: filled so a
           30 s take at 8,000 Hz fits and a second runs out of room (11,025
           Hz refused there first - 'Floppy: use 8,000 Hz': a floppy's 32 KB
           write blocks the copy longer than the ring holds at 11 kHz): the
           first read back whole, the second DISK FULL - 'Disk full: take
           kept', the take kept with whole writes
  calls    (US-11, written in wave 6a) a -DWR_CALLS build on the HDD machine:
           drawing primitives a tick idle, recording, playing the take, and
           one cut; wr_redraw's cycles a tick while recording; a full
           repaint (a layout switch each way) in primitives and milliseconds
  noinput  QEMU (an SB16 that captures nothing, SPEC.md 35): 11,025 Hz onto
           the B: floppy is refused ('Floppy: use 8,000 Hz'); at 8,000 Hz
           Record answers 'No input from the card', and no VOICE file is left

What it does NOT cover, and cannot: a real microphone's content, the gain
heard, and a real disk's write speed.
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
sys.path[:0] = [HERE, os.path.join(ROOT, "tools")]
os.chdir(ROOT)
import os88build                                              # noqa: E402
import os88flush                                              # noqa: E402
import os88geom as geom                                       # noqa: E402
import os88marty                                              # noqa: E402
import os88ui                                                 # noqa: E402
import wrsim                                                  # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402
from waverackdisk import (Rack, cyc, guest_wait, open_doc,     # noqa: E402
                          TEMPLATE)

MACHINE_HDD = "os8088_5150_herc_hdd_sb_gla"
MACHINE_VGA = "os8088_xt_vga_hdd_sb"
MACHINE_FD = "os8088_5150_herc_sb_gla_144"
WT_IDLE, WT_REC, WT_RPAUSE = 0, 3, 4
TAKE_S = 60.0                           # guest seconds a measured take runs
HZ = os88marty.GUEST_HZ
FAIL = []
SHOTS = None


def say(s):
    print(s, flush=True)


def check(what, ok, more=""):
    say("   %-62s %s%s" % (what, "ok" if ok else "FAIL",
                           "" if ok or not more else "  " + more))
    if not ok:
        FAIL.append(what)
    return ok


def card_rate(rate):
    """the rate SOUND.DRV's time constant really records at (it truncates)"""
    return 1000000.0 / (1000000 // rate)


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def shot(m, name):
    """the machine's screen into SHOTS/NAME.png (VGA rendered, 1bpp VRAM)"""
    if not SHOTS:
        return
    path = os.path.join(SHOTS, name + ".png")
    try:
        if m.video().get("type") == "vga":
            w, h, data = m.fbuf()
            os88marty.write_png_rgb(path, w, h, data)
        else:
            w, h, rows = m.vram()
            os88marty.write_png(path, w, h, rows)
        say("     (picture: %s)" % path)
    except Exception as e:                              # noqa: BLE001
        say("     (no picture %s: %s)" % (name, e))


def heap(m, ui, rk):
    """WaveRack's claims on the kernel's map, KB each (waverack.py's)"""
    n = 32
    raw = m.read(ui._S("mem_tab"), n * geom.MC_SIZE)
    seg = rk.base >> 4
    recs = [tuple(u16(raw, i * geom.MC_SIZE + k) for k in (0, 2, 4))
            for i in range(n)]
    return sorted((r[1] * 16 + 1023) // 1024 for r in recs
                  if r[0] and r[2] == seg)


def why(rk):
    p = rk.w_("wc_why")
    if not p:
        return ""
    return bytes(rk.m.read(rk.base + p, 24)).split(b"\0")[0].decode("latin-1")


def launch(ui, path, syms):
    w = ui.path(path)
    return Rack(ui.m, ui, w, syms)


def set_rate(ui, rk, k):
    """Sound > Rate: cycles 8,000 -> 11,025 -> 15,000 - each pick waited
    out before the next (the command runs on the UI task after the release:
    a first run picked again before the first had landed, twice in all)"""
    for _ in range(3):
        if rk.b("wr_rate") == k:
            return True
        ui.raise_window(rk.w)
        r0 = rk.b("wr_rate")
        ui.menu_pick("Sound", "Rate:")
        os88marty.until(ui.m, lambda mm: rk.b("wr_rate") != r0,
                        "the rate to move", poll=0.05, guest=30.0)
    return rk.b("wr_rate") == k


def take(ui, rk, label, secs, during=None, pause=None, stop=True):
    """R, SECS guest seconds of take (DURING called once it runs), Stop and
    the take opened -> what WaveRack and the card said"""
    m = ui.m
    ui.raise_window(rk.w)
    m.key("KeyR")
    os88marty.until(m, lambda mm: rk.b("wt_state") == WT_REC,
                    label + " recording", poll=0.05, guest=60.0)
    c0 = cyc(m)
    r = dict(name=rk.text("wc_name0", 14), room=rk.text("wr_stbuf"),
             rate=rk.w_("wc_rate"), clu=rk.w_("wc_clu"), osz=rk.w_("wc_osz"),
             rl=rk.w_("wc_rl"), nu=rk.b("wc_nu"), heap=heap(m, ui, rk))
    if during:
        r["during"] = during()
    if pause:
        r.update(pause() or {})
    end = c0 + secs * HZ
    os88marty.until(m, lambda mm: cyc(mm) >= end or
                    rk.b("wt_state") != WT_REC, label + " to run",
                    poll=0.05, guest=secs + 180.0)
    r["dt"] = (cyc(m) - c0) / HZ
    r["early"] = rk.b("wt_state") != WT_REC
    r["status_rec"] = rk.text("wr_stbuf")
    if not stop:
        return r
    if not r["early"]:
        ui.raise_window(rk.w)
        m.key("Escape")
        # THE TAKE ENDS WHEN THE STREAM CLOSES, which is when the UI task
        # handles the key - after a floppy write it is inside, or another
        # window's work - and the card records until then: the take's time
        # is to the close ([wc_sopen] = 0), not to the key (a third run
        # counted 3.9 s of frames past the key on the floppy, every one real)
        os88marty.until(m, lambda mm: rk.b("wc_sopen") == 0, label +
                        " closed", poll=0.02, guest=60.0)
        r["dt"] = (cyc(m) - c0) / HZ
    os88marty.until(m, lambda mm: rk.b("wt_state") == WT_IDLE and
                    rk.b("we_busy") == 0 and
                    ((rk.b("wo_doc") and rk.text("wo_name") == r["name"] and
                      rk.b("wv_ok")) or rk.d("wc_wrote") == 0 or
                     rk.b("ww_err")),
                    label + " stopped and opened", poll=0.1, guest=900.0)
    r.update(rcap=rk.d("wc_rcap"), frames=rk.d("wc_frames"),
             wrote=rk.d("wc_wrote"), lagmax=rk.w_("wc_lagmax"),
             why=why(rk), status=rk.text("wr_stbuf"),
             doc=rk.text("wo_name"), docframes=rk.d("wo_frames"),
             dirty=rk.b("wo_dirty"))
    return r


def report(tag, label, r):
    say("     %-6s %-24s %5.1f s: %s, %d frames (card %d), ring high %d of "
        "%d, writes of %d KB%s" % (
            tag, label, r["dt"], r["name"], r.get("wrote", 0),
            r.get("rcap", 0), r.get("lagmax", 0), r["rl"], r["osz"] // 1024,
            (", ended: " + r["why"]) if r.get("why") else ""))


def check_take(tag, label, r, data, cards=None):
    """the take ON THE DISK against what WaveRack said and the card counted"""
    clu, rate = r["clu"], r["rate"]
    n = r["wrote"]
    if not check("%s %s: on the disk (%s)" % (tag, label, r["name"]),
                 data is not None):
        return
    check("%s %s: header = wrsim.take_header(%d, %d, %d)" % (
        tag, label, rate, clu, n),
        data[:clu] == wrsim.take_header(rate, clu, n),
        "got %r" % data[:48])
    check("%s %s: the file is the header cluster + the frames" % (tag, label),
          len(data) == clu + n, "%d bytes, %d + %d" % (len(data), clu, n))
    card = sum(cards) if cards else r["rcap"]
    check("%s %s: frames written = the card's count (%d)" % (
        tag, label, card), n == card == r["frames"],
        "wrote %d, staged %d, card %d" % (n, r["frames"], card))
    body = data[clu:]
    check("%s %s: every frame 80h (MartyPC's input)" % (tag, label),
          len(body) > 0 and body.count(b"\x80") == len(body),
          "%d of %d" % (body.count(b"\x80"), len(body)))
    check("%s %s: no overrun, no reason" % (tag, label), not r["why"],
          repr(r["why"]))
    want = card_rate(rate) * r["dt"]
    say("     %-6s %s: %d frames over %.1f guest s = %.0f Hz (the card's "
        "time constant: %.0f Hz)" % (tag, label, n, r["dt"], n / r["dt"],
                                     card_rate(rate)))
    check("%s %s: the frames are the take's time at the card's rate, "
          "within 1 s" % (tag, label), abs(n - want) < card_rate(rate),
          "%d against %.0f" % (n, want))
    check("%s %s: opened as the document, not dirty" % (tag, label),
          r["doc"] == r["name"] and r["docframes"] == n and
          not r.get("dirty"), "%r %d dirty %r" % (r["doc"], r["docframes"],
                                                 r.get("dirty")))


class Cost(object):
    """the tick's work priced in guest cycles: entry -> exit pairs in the
    package, by address"""

    PAIRS = (("wc_copy", "wc_copy.g"), ("wc_tick", "wc_tick.shown"),
             ("wc_feed", "wc_feed.out"), ("wc_write", "wc_write.ok"),
             ("wc_draw", "wc_draw.out"))

    def __init__(self, rk, syms):
        self.at = {}
        for a, b in self.PAIRS:
            self.at[rk.base + syms[a]] = (a, 0)
            self.at[rk.base + syms[b]] = (a, 1)
        self.open = {}
        self.got = {a: [] for a, _ in self.PAIRS}

    def targets(self):
        return list(self.at)

    def hit(self, m, rec):
        name, end = self.at.get(rec["addr"], (None, 0))
        if name is None:
            return None
        if not end:
            self.open[name] = (rec["cycles"], rec.get("regs", {}).get("cx", 0))
        elif name in self.open:
            c0, cx = self.open.pop(name)
            self.got[name].append((rec["cycles"] - c0, cx))
        return None

    def say(self, rate):
        cp = self.got["wc_copy"]
        if cp:
            byt = sum(cx for _, cx in cp)
            cyc_ = sum(c for c, _ in cp)
            per = cyc_ / max(1, byt)
            say("     cost   wc_copy: %d calls, %d bytes, %.1f cycles a byte "
                "-> %.1f%% of an 8088 at %d Hz" % (
                    len(cp), byt, per, 100.0 * per * rate / HZ, rate))
        for name in ("wc_tick", "wc_feed", "wc_write", "wc_draw"):
            v = sorted(c for c, _ in self.got[name])
            if v:
                say("     cost   %s: %d calls, median %.2f ms, max %.1f ms" % (
                    name, len(v), 1000.0 * v[len(v) // 2] / HZ,
                    1000.0 * v[-1] / HZ))
        return self.got


def hdd_vhd(tmp, o88, name, fx=None, big=False):
    vhd = os.path.join(tmp, name)
    args = [sys.executable, "tools/os88hdd.py", "--out", vhd,
            "--kernel", os88build.at("build/kernel.sys"),
            "--vbr", os88build.at("build/boothd.bin"),
            "--mbr", os88build.at("build/mbr.bin"),
            "--file", "SOUND.DRV=" + os88build.at("build/sound.drv"),
            "--file", "WAVERACK.O88=" + o88]
    if big:
        args += ["--cyls", "654", "--heads", "16", "--spt", "63",
                 "--spc", "64"]
    else:
        args += ["--template", TEMPLATE]
    if fx:
        args += ["--file", "BIG22.WAV=" + os.path.join(fx, "BIG22.WAV")]
    subprocess.run(args, check=True, capture_output=True)
    # SYSTEM\APPDATA, as an installed disk has it (the system disk's tree;
    # tests/syscheck.py's `--folder SYSTEM/APPDATA`): WAVERACK.TAK lives there
    # and os88data.inc creates no folder. os88hdd.py's volume is root files
    lba = struct.unpack_from("<I", open(vhd, "rb").read(512), 446 + 8)[0]
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    subprocess.run(["mmd", "-i", "%s@@%d" % (vhd, lba * 512), "::/SYSTEM",
                    "::/SYSTEM/APPDATA"], check=True, capture_output=True,
                   env=env)
    return vhd


def vhd_read(vhd, name):
    try:
        return os88flush.vhd_volume(vhd).read(name)
    except os88flush.FlushError:
        return None


def leg_hdd(o88, syms, tmp, fx):
    say("waverackrec: hdd - takes onto an XT-IDE VHD of small clusters")
    vhd = hdd_vhd(tmp, o88, "c.vhd", fx)
    takes = []
    m = os88marty.launch(None, machine=MACHINE_HDD,
                         extra=["--mount", "hd:0:" + os.path.abspath(vhd)])
    try:
        ui = os88ui.UI(m, verbose=False)
        ui.ready(limit=240)
        rk = launch(ui, "C:/WAVERACK.O88", syms)
        idle = heap(m, ui, rk)
        say("     heap, idle: %s KB" % idle)
        # --- 60 s at 11,025 Hz, the tick priced, the gain table read -------
        check("hdd: the next take is 11,025 Hz", set_rate(ui, rk, 1))
        cost = Cost(rk, syms)
        box = {}

        def priced():
            m.key("Period")             # INPUT GAIN +2 dB, twice: step 8
            m.key("Period")
            with os88marty.bp_trace(m, *cost.targets(), regs=True,
                                    on_hit=cost.hit):
                c0 = cyc(m)
                os88marty.until(m, lambda mm: cyc(mm) - c0 >= 8 * HZ,
                                "8 s of the take priced", poll=0.1,
                                guest=60.0)
            seg = rk.w_("wc_rseg")
            tab = bytes(m.read((seg << 4) + rk.w_("wc_rl") + 16, 256))
            box["gain"] = (rk.b("wr_gain"), tab)
        r = take(ui, rk, "take 1", TAKE_S, during=priced)
        report("hdd", "11,025 Hz, 60 s", r)
        check("hdd: a fixed disk of 2 KB clusters writes 16 KB units, four",
              r["osz"] == 16384 and r["nu"] == 4, "%d x %d" % (r["nu"],
                                                         r["osz"]))
        say("     heap, recording: %s KB (the ring %d KB + 1, %d units of %d "
            "KB)" % (r["heap"], r["rl"] // 1024, r["nu"], r["osz"] // 1024))
        g, tab = box["gain"]
        check("hdd: INPUT GAIN step %d's table = wrsim.gain_table" % g,
              g == 8 and tab == wrsim.gain_table(8))
        r["cost"] = cost.say(r["rate"])
        takes.append(("11k", r))
        m.key("Comma")
        m.key("Comma")                  # (unity again)
        # --- 60 s at 15,000 Hz ----------------------------------------------
        check("hdd: the next take is 15,000 Hz", set_rate(ui, rk, 2))
        r = take(ui, rk, "take 2", TAKE_S)
        report("hdd", "15,000 Hz, 60 s", r)
        takes.append(("15k", r))
        say("     heap, the take open: %s KB" % heap(m, ui, rk))
        # --- a second WaveRack reads the disk during a take -----------------
        check("hdd: the next take is 11,025 Hz", set_rate(ui, rk, 1))

        def second():
            dw = [x for x in ui.windows()
                  if x.visible and ui.fs_of(x) == (2, 0, 1)]
            if dw:
                ui.open("WAVERACK.O88", win=dw[0])
            else:
                ui.path("C:/WAVERACK.O88")
            got = {}

            def up(mm):
                for x in ui.windows():
                    if x.visible and x.i != rk.w.i and \
                            x.title.startswith("WaveRack"):
                        got["w"] = x
                        return True
                return False
            os88marty.until(m, up, "the second WaveRack", poll=0.25,
                            guest=120.0)
            rk2 = Rack(m, ui, got["w"], syms)
            box["rec2"] = rk2.b("wc_rec")   # (the take is live: no offer)
            c0 = cyc(m)
            # A TAKE DRAWS EVERY TICK (the strip, the meter, the time), so the
            # screen never settles and os88ui's chooser() - which settles for
            # the paint - would wait for ever: the paint is waited out in
            # guest seconds instead, for as long as the take runs
            real = ui.settle
            ui.settle = lambda **kw: guest_wait(m, 4.0, "a paint")
            try:
                open_doc(ui, rk2, "BIG22.WAV", 600.0)
            finally:
                ui.settle = real
            box["rk2"] = rk2
            return (cyc(m) - c0) / HZ
        r = take(ui, rk, "take 3", TAKE_S, during=second)
        say("     hdd    (the second WaveRack's Open and 2 MB scan: %.1f s "
            "guest, inside the take)" % r["during"])
        report("hdd", "11,025 Hz + a 2 MB scan", r)
        check("hdd: a second WaveRack does not offer the LIVE take",
              box.get("rec2") == 0, repr(box.get("rec2")))
        takes.append(("scan", r))
        if box.get("rk2"):
            ui.close(box["rk2"].w)
        # --- Pause, then Record again -----------------------------------------

        def paused():
            c0 = cyc(m)
            os88marty.until(m, lambda mm: cyc(mm) - c0 >= 5 * HZ, "5 s",
                            poll=0.1, guest=30.0)
            ui.raise_window(rk.w)
            m.key("KeyR")               # (Record during a take: Pause)
            os88marty.until(m, lambda mm: rk.b("wt_state") == WT_RPAUSE,
                            "paused", poll=0.05, guest=30.0)
            n1, f1 = rk.d("wc_rcap"), rk.d("wc_frames")
            shot(m, "hdd-paused")
            c1 = cyc(m)
            os88marty.until(m, lambda mm: cyc(mm) - c1 >= 4 * HZ,
                            "4 s paused", poll=0.1, guest=30.0)
            f2 = rk.d("wc_frames")
            m.key("KeyR")               # resume
            os88marty.until(m, lambda mm: rk.b("wt_state") == WT_REC,
                            "recording again", poll=0.05, guest=30.0)
            return dict(n1=n1, f1=f1, f2=f2, pst=(cyc(m) - c1) / HZ)
        r = take(ui, rk, "take 4", 16.0, pause=paused)
        r["dt"] -= r["pst"]             # (the frames are the take's, not the
        report("hdd", "pause and resume", r)   # pause's)
        check("hdd pause: nothing captured while paused",
              r["f1"] == r["f2"] == r["n1"], "%d %d %d" % (
                  r["n1"], r["f1"], r["f2"]))
        say("     hdd    pause: stream 1 = %d, stream 2 = %d, file %d" % (
            r["n1"], r["rcap"], r["wrote"]))
        takes.append(("pause", r))
        r["cards"] = [r["n1"], r["rcap"]]
        # --- killed mid-take: the recovery path ------------------------------
        rk_name = None
        r = take(ui, rk, "take 5", 12.0, stop=False)
        rk_name = r["name"]
        shot(m, "hdd-recording")
        m.pause()                       # (still: what it has written is read)
        killed = dict(r, wrote=rk.d("wc_wrote"), clu=r["clu"])
        say("     hdd    killed mid-take: %s, %d bytes written" % (
            rk_name, killed["wrote"]))
    finally:
        m.close()
    # --- every take off the VHD ---------------------------------------------
    for tag, r in takes:
        check_take("hdd", tag, r, vhd_read(vhd, r["name"]), r.get("cards"))
    raw = vhd_read(vhd, rk_name)
    clu = killed["clu"]
    # (a write the pause caught inside its int 13h may or may not have
    # committed: the file is whole writes, at least what WaveRack counted)
    kept = len(raw) - clu if raw else -1
    killed["kept"] = kept
    check("hdd recover: the killed take on the disk, unfinished, whole "
          "writes", raw is not None and raw[:clu] == wrsim.take_header(
              killed["rate"], clu, 0) and kept >= killed["wrote"] > 0 and
          kept % killed["osz"] == 0 and
          raw[clu:].count(b"\x80") == kept,
          "len %s, wrote %d" % (len(raw) if raw else None, killed["wrote"]))
    tak = vhd_read(vhd, "SYSTEM/APPDATA/WAVERACK.TAK")
    check("hdd recover: WAVERACK.TAK names the take, running",
          tak is not None and tak[:5] == b"WRT2\x01" and
          tak[10:22] == rk_name.encode(), repr(tak))
    # --- the machine again: File > Recover Take -----------------------------
    m = os88marty.launch(None, machine=MACHINE_HDD,
                         extra=["--mount", "hd:0:" + os.path.abspath(vhd)])
    try:
        ui = os88ui.UI(m, verbose=False)
        ui.ready(limit=240)
        rk = launch(ui, "C:/WAVERACK.O88", syms)
        check("hdd recover: the marker read at launch ([wc_rec])",
              rk.b("wc_rec") == 1)
        ui.raise_window(rk.w)
        # the offer, photographed with the menu down (os88ui's own press)
        cells = ui.menus()
        fx0, fx1 = [(c[1], c[2]) for c in cells if c[0] == "File"][0]
        ui.mo.to((fx0 + fx1) // 2, geom.MBAR_H // 2)
        ui.mo._edge(True)
        os88marty.until(m, lambda mm: ui._word("menu_y1") and
                        ui._byte("menu_dropd"), "the File menu", poll=0.1,
                        guest=30.0)
        live = dict(ui.menus()[[c[0] for c in cells].index("File")][3])
        shot(m, "hdd-recover-offer")
        ui.mo.to((fx0 + fx1) // 2, geom.MBAR_H // 2)
        ui.mo._edge(False)
        check("hdd recover: File > Recover Take is live",
              live.get("Recover Take") is True, repr(live))
        ui.menu_pick("File", "Recover Take")
        os88marty.until(m, lambda mm: rk.b("we_busy") == 0 and
                        rk.b("wo_doc") and rk.text("wo_name") == rk_name and
                        rk.b("wv_ok"), "the recovered take", poll=0.1,
                        guest=600.0)
        shot(m, "hdd-recovered")
        st = rk.text("wr_stbuf")
        check("hdd recover: opened with the file's frames, dirty, "
              "'Recovered; save to fix'",
              rk.d("wo_frames") == killed["kept"] and
              st == "Recovered; save to fix",
              "%d frames, %r" % (rk.d("wo_frames"), st))
        check("hdd recover: Recover Take greyed again", rk.b("wc_rec") == 0)
    finally:
        m.close()
    tak = vhd_read(vhd, "SYSTEM/APPDATA/WAVERACK.TAK")
    check("hdd recover: WAVERACK.TAK's entry cleared (after the open)",
          tak is not None and tak[:4] == b"WRT2" and
          all(tak[4 + 20 * k] == 0 for k in range(4)), repr(tak))


def leg_bigclu(o88, syms, tmp):
    say("waverackrec: bigclu - takes onto a VHD of 32 KB clusters (VGA XT)")
    vhd = hdd_vhd(tmp, o88, "c32.vhd", big=True)
    takes = []
    m = os88marty.launch(None, machine=MACHINE_VGA,
                         extra=["--mount", "hd:0:" + os.path.abspath(vhd)])
    try:
        ui = os88ui.UI(m, verbose=False)
        ui.ready(limit=240)
        rk = launch(ui, "C:/WAVERACK.O88", syms)
        for k, tag in ((1, "11k"), (2, "15k")):
            check("bigclu: the next take is %s" % tag, set_rate(ui, rk, k))

            def look():
                c0 = cyc(m)
                os88marty.until(m, lambda mm: cyc(mm) - c0 >= 20 * HZ,
                                "20 s", poll=0.1, guest=60.0)
                shot(m, "vga-recording-%s" % tag)
            r = take(ui, rk, "take " + tag, TAKE_S, during=look)
            report("bigclu", "%s, 60 s, 32 KB clusters" % tag, r)
            check("bigclu %s: the cluster is 32 KB, the writes 32 KB" % tag,
                  r["clu"] == 32768 and r["osz"] == 32768,
                  "%d %d" % (r["clu"], r["osz"]))
            say("     heap, recording: %s KB" % r["heap"])
            takes.append((tag, r))
        shot(m, "vga-take-open")
    finally:
        m.close()
    for tag, r in takes:
        check_take("bigclu", tag, r, vhd_read(vhd, r["name"]))


def free_clusters(img):
    out = subprocess.run([sys.executable, "tools/os88fat.py", "ls", img],
                         check=True, capture_output=True, text=True).stdout
    line = [x for x in out.splitlines() if "cluster(s) free" in x][-1]
    return int(line.split()[0])


def leg_floppy(o88, syms, tmp):
    say("waverackrec: floppy - a take onto 1.44 MB, then the disk fills")
    sysimg = os.path.join(tmp, "sys.img")
    shutil.copy(os88build.at("build/os8088.img"), sysimg)
    subprocess.run([sys.executable, "tools/os88fat.py", "add", sysimg,
                    os88build.at("build/mixercfg/SYSTEM.CFG")],
                   check=True, capture_output=True)    # SOUND.DRV wanted
    bimg = os.path.join(tmp, "b.img")
    fill = os.path.join(tmp, "FILL.BIN")
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", bimg,
                    "--size", "1440", o88], check=True,
                   capture_output=True)    # (os88disk refuses an empty file)
    # take 1: 30 s at 8,000 Hz (and its stop) and a header; take 2 gets
    # 80 KB: two 32 KB writes and a header, then the third refused
    want = (int(34 * card_rate(8000)) + 511) // 512 + 1 + 160
    free = free_clusters(bimg)
    with open(fill, "wb") as f:
        f.write(bytes((free - want) * 512))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", bimg,
                    "--size", "1440", o88, fill], check=True,
                   capture_output=True)
    say("     B: %d clusters free of %d" % (free_clusters(bimg), free))
    m = os88marty.launch(sysimg, apps=bimg, machine=MACHINE_FD)
    try:
        ui = os88ui.UI(m, verbose=False)
        ui.ready(limit=240)
        rk = launch(ui, "B:/WAVERACK.O88", syms)
        ui.raise_window(rk.w)
        m.key("KeyR")                   # 11,025 Hz: refused on a floppy
        os88marty.until(m, lambda mm: rk.text("wr_stbuf") ==
                        "Floppy: use 8,000 Hz", "the floppy's refusal",
                        poll=0.05, guest=30.0)
        check("floppy: 11,025 Hz refused, 'Floppy: use 8,000 Hz', no take",
              rk.b("wt_state") == WT_IDLE and rk.b("wc_made") == 0)
        check("floppy: the next take is 8,000 Hz", set_rate(ui, rk, 0))
        r1 = take(ui, rk, "floppy take", 30.0)
        report("floppy", "8,000 Hz, 30 s", r1)
        say("     floppy room said: %r" % r1["room"])
        r2 = take(ui, rk, "disk full", 120.0)
        report("floppy", "until the disk is full", r2)
        shot(m, "floppy-diskfull")
        vol = os88flush.Flush(marty=m).volume(1)
        d1 = vol.read(r1["name"])
        try:
            d2 = vol.read(r2["name"])
        except os88flush.FlushError:
            d2 = None
    finally:
        m.close()
    check_take("floppy", "30 s", r1, d1)
    check("floppy: the units are 32 KB (a floppy's write), two",
          r1["osz"] == 32768 and r1["nu"] == 2, "%d x %d" % (r1["nu"],
                                                             r1["osz"]))
    check("floppy full: the take ended early on 'Disk full: take kept'",
          r2["early"] and r2["why"] == "Disk full: take kept" and
          r2["status"] == "Disk full: take kept",
          "early %r why %r status %r" % (r2["early"], r2["why"],
                                         r2["status"]))
    n = r2["wrote"]
    check("floppy full: the take kept - whole writes, a finished header",
          d2 is not None and n > 0 and n % r2["osz"] == 0 and
          d2 == wrsim.take_header(r2["rate"], r2["clu"], n) +
          b"\x80" * n, "wrote %d, file %s" % (n, len(d2) if d2 else None))


def leg_calls(tmp, fx):
    """US-11's redraw cost (docs/plans/WAVERACK-PLAN.md): a -DWR_CALLS build -
    every drawing primitive WaveRack calls counted into [wr_ncalls] - on the
    5150 + Hercules + XT-IDE + SB 2.0, read per tick IDLE, RECORDING (11,025
    Hz, the strip and the meter), PLAYING the take (the ring pipeline wave 4b
    made, at Fit), and for one CUT at Fit; and the guest cycles of wr_redraw a
    tick while recording. Waves 3 and 4 counted these by hand on QEMU's VGA
    (idle 0, playing 1.70, an edit 34-37, a full repaint 120-126); this is the
    row that takes them on the target, and the recording figure for the first
    time. WRITTEN IN WAVE 6a, first run in the final pass"""
    say("waverackrec: calls - drawing primitives a tick (-DWR_CALLS)")
    syms, img = pkg_syms("apps/waverack/waverack.asm",
                         ("apps/", "apps/waverack/",
                          os88build.at("build") + "/"), defines=("WR_CALLS",))
    b = os.path.join(tmp, "wrcalls.bin")
    open(b, "wb").write(img)
    o88 = os.path.join(tmp, "WAVERACK.O88")
    subprocess.run([sys.executable, "tools/os88pkg.py", "--compress-if=lz4",
                    b, "-o", o88], check=True, capture_output=True)
    vhd = hdd_vhd(tmp, o88, "calls.vhd")
    m = os88marty.launch(None, machine=MACHINE_HDD,
                         extra=["--mount", "hd:0:" + os.path.abspath(vhd)])
    try:
        ui = os88ui.UI(m, verbose=False)
        ui.ready(limit=240)
        rk = launch(ui, "C:/WAVERACK.O88", syms)
        ui.raise_window(rk.w)

        def per_tick(label, secs):
            n0, c0 = rk.w_("wr_ncalls"), cyc(m)
            os88marty.until(m, lambda mm: cyc(mm) - c0 >= secs * HZ, label,
                            poll=0.1, guest=secs + 60.0)
            n = (rk.w_("wr_ncalls") - n0) & 0xFFFF
            t = (cyc(m) - c0) / HZ * 18.2065
            say("     calls  %-28s %6.2f a tick (%d in %.0f ticks)" % (
                label, n / t, n, t))
            return n / t
        idle = per_tick("idle, no document", 10.0)
        check("calls: idle draws nothing", idle == 0.0, "%.2f" % idle)
        check("calls: the next take is 11,025 Hz", set_rate(ui, rk, 1))
        m.key("KeyR")
        os88marty.until(m, lambda mm: rk.b("wt_state") == WT_REC,
                        "recording", poll=0.05, guest=60.0)
        guest_wait(m, 2.0, "the take under way")
        cost = []
        pair = (rk.base + syms["wr_redraw"], rk.base + syms["wr_redraw.out"])

        def hit(mm, rec):
            if rec["addr"] == pair[0]:
                cost.append([rec["cycles"], None])
            elif cost and cost[-1][1] is None:
                cost[-1][1] = rec["cycles"]
        with os88marty.bp_trace(m, *pair, on_hit=hit):
            rec = per_tick("recording, 11,025 Hz", 10.0)
        v = sorted(b - a for a, b in cost if b is not None)
        if v:
            say("     calls  wr_redraw while recording: %d ticks, median %.2f "
                "ms, max %.1f ms" % (len(v), 1000.0 * v[len(v) // 2] / HZ,
                                     1000.0 * v[-1] / HZ))
        check("calls: a recording tick draws at most 4 primitives (the time, "
              "a strip column, the meter's runs)", 0 < rec <= 4.0,
              "%.2f" % rec)
        m.key("Escape")
        os88marty.until(m, lambda mm: rk.b("wt_state") == 0 and
                        rk.b("we_busy") == 0 and rk.b("wv_ok"),
                        "the take opened", poll=0.1, guest=300.0)
        guest_wait(m, 2.0, "quiet")
        m.key("Space")
        os88marty.until(m, lambda mm: rk.b("wt_state") == 1, "playing",
                        poll=0.05, guest=60.0)
        guest_wait(m, 1.0, "the play under way")
        play = per_tick("playing the take, Fit", 8.0)
        check("calls: a playing tick at Fit draws under 3 primitives "
              "(wave 3: 1.70 on QEMU)", 0 < play < 3.0, "%.2f" % play)
        m.key("Escape")
        os88marty.until(m, lambda mm: rk.b("wt_state") == 0, "stopped",
                        poll=0.05, guest=30.0)
        guest_wait(m, 2.0, "quiet")
        n0 = rk.w_("wr_ncalls")         # one CUT at Fit: select all, X
        m.key("KeyA")
        guest_wait(m, 2.0, "selected")
        n1 = rk.w_("wr_ncalls")
        m.key("KeyX")
        os88marty.until(m, lambda mm: rk.b("wo_dirty"), "the cut",
                        poll=0.05, guest=30.0)
        guest_wait(m, 3.0, "drawn")
        n2 = rk.w_("wr_ncalls")
        say("     calls  select all %d, one cut %d (wave 4 on QEMU: 34-37)" % (
            (n1 - n0) & 0xFFFF, (n2 - n1) & 0xFFFF))
        check("calls: a cut at Fit is under 60 primitives, never a full "
              "repaint (126)", 0 < ((n2 - n1) & 0xFFFF) < 60,
              "%d" % ((n2 - n1) & 0xFFFF))
        # A FULL REPAINT, priced: View > Compact Layout and back each paint
        # the whole window (wr_paint, entry to its ret), with the document
        # open - PERFORMANCE.md's measure of a redraw, on the target
        span = []
        pp = (rk.base + syms["wr_paint"], rk.base + syms["wr_paint.out"])

        def phit(mm, rec):
            if rec["addr"] == pp[0]:
                span.append([rec["cycles"], None, rk.w_("wr_ncalls")])
            elif span and span[-1][1] is None:
                span[-1][1] = rec["cycles"]
                span[-1].append(rk.w_("wr_ncalls"))
        for item in ("Compact Layout", "Full Layout"):
            with os88marty.bp_trace(m, *pp, on_hit=phit):
                ui.menu_pick("View", item)
                guest_wait(m, 6.0, "the repaint")
            done = [x for x in span if x[1] is not None]
            if done:
                c0, c1, k0, k1 = done[-1]
                say("     calls  a full repaint after %-16s %4d primitives, "
                    "%.0f ms" % (item, (k1 - k0) & 0xFFFF,
                                 1000.0 * (c1 - c0) / HZ))
            check("calls: %s repainted the window once" % item, bool(done))
    finally:
        m.close()


def leg_noinput(syms, img, tmp):
    say("waverackrec: noinput - QEMU's SB16 captures nothing")
    import waverackplay as P                                  # (QEMU's)
    pr = P.PR(tmp, syms, img)
    try:
        if not check("noinput: WaveRack launches off B:", pr.launch()):
            return
        time.sleep(1)
        h0 = pr.heap()
        pr.key("r")                     # 11,025 Hz onto a floppy: refused
        ok = P.until(lambda: pr.text("wr_stbuf") == "Floppy: use 8,000 Hz",
                     "'Floppy: use 8,000 Hz'", 15, 0.05)
        check("noinput: 11,025 Hz onto a floppy says 'Floppy: use 8,000 Hz'",
              ok and pr.b("wt_state") == WT_IDLE, repr(pr.text("wr_stbuf")))
        for _ in range(2):              # 11,025 -> 15,000 -> 8,000
            r0 = pr.b("wr_rate")
            pr.menu_pick("Sound", "Rate:")
            P.until(lambda: pr.b("wr_rate") != r0, "the rate", 10, 0.05)
        check("noinput: the next take is 8,000 Hz", pr.b("wr_rate") == 0)
        pr.key("r")
        ok = P.until(lambda: pr.b("wt_state") == WT_IDLE and
                     pr.text("wr_stbuf") == "No input from the card",
                     "'No input from the card'", 30, 0.05)
        check("noinput: Record answers 'No input from the card'", ok,
              repr(pr.text("wr_stbuf")))
        check("noinput: the heap is what it was", pr.heap() == h0,
              "%s against %s" % (pr.heap(), h0))
        check("noinput: the LCD says NO FILE again", pr.b("wo_doc") == 0)
    finally:
        pr.q.close()
    out = subprocess.run([sys.executable, "tools/os88fat.py", "ls",
                          os.path.join(tmp, "b.img")], check=True,
                         capture_output=True, text=True).stdout
    check("noinput: no VOICE file left on B:", "VOICE" not in out)


def main():
    global SHOTS
    ap = argparse.ArgumentParser()
    ap.add_argument("--leg", action="append",
                    choices=("hdd", "bigclu", "floppy", "noinput", "calls"))
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--shots", default=os88build.at("build/waverackrec"))
    a = ap.parse_args()
    for p in ("build/os8088.img", "build/waverack.o88", "build/waverack.bin",
              "build/kernel.sys", "build/boothd.bin", "build/mbr.bin",
              "build/sound.drv", "build/mixercfg/SYSTEM.CFG",
              "build/wrfix.img"):
        if not os.path.exists(os88build.at(p)):
            sys.exit("waverackrec: no %s - make && make waverackfix "
                     "build/mixercfg/SYSTEM.CFG" % p)
    syms, img = pkg_syms("apps/waverack/waverack.asm",
                         ("apps/", "apps/waverack/",
                          os88build.at("build") + "/"))
    if img != open(os88build.at("build/waverack.bin"), "rb").read():
        sys.exit("waverackrec: build/waverack.bin is not this source's - make")
    SHOTS = a.shots
    os.makedirs(SHOTS, exist_ok=True)
    o88 = os88build.at("build/waverack.o88")
    tmp = tempfile.mkdtemp(dir=os88build.at("build"), prefix="wrrec")
    try:
        fx = os.path.join(tmp, "fx")
        os.makedirs(fx)
        open(os.path.join(fx, "BIG22.WAV"), "wb").write(
            wrsim.disk_fixture("BIG22.WAV"))
        legs = a.leg or ("hdd", "bigclu", "floppy", "noinput", "calls")
        for leg in legs:
            t0 = time.time()
            try:
                if leg == "hdd":
                    leg_hdd(o88, syms, tmp, fx)
                elif leg == "bigclu":
                    leg_bigclu(o88, syms, tmp)
                elif leg == "floppy":
                    leg_floppy(o88, syms, tmp)
                elif leg == "calls":
                    leg_calls(tmp, fx)
                else:
                    q = os.path.join(tmp, "q")
                    os.makedirs(q)
                    leg_noinput(syms, img, q)
            except Exception as e:                      # noqa: BLE001
                check("%s: ran to its end" % leg, False,
                      "%s: %s" % (type(e).__name__, e))
            say("     (%s: %.0f s host)" % (leg, time.time() - t0))
    finally:
        if a.keep:
            say("waverackrec: kept %s" % tmp)
        else:
            shutil.rmtree(tmp, ignore_errors=True)
    for f in FAIL:
        say("  FAIL: " + f)
    say("waverackrec: %s" % ("FAILED (%d)" % len(FAIL) if FAIL else "ok"))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
