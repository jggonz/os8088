#!/usr/bin/env python3
"""xtmp3xt - XTMP3 END TO END on MartyPC's 4.77 MHz XT (SPEC.md 121.5-121.7,
121.10, 121.11; docs/xtmp3/FIELD.md).

    make && python3 tests/xtmp3xt.py [--leg std|fast|speech|c128 ...]
                                     [--json FILE] [--expect PROFILE]

THE WHOLE PLAYER, NOT A PART. The XT track timed GRANULE alone and the STD
track a bench; this is the SHIPPED package (build/xtmp3.o88 with its real
FRONT and XT parts) on os8088_xt_vga_hdd_sb - an IBM 5160, 8088 at 4.77 MHz,
640 KB, a VGA, an XT-IDE fixed disk and a Sound Blaster DSP 2.01 - booted
off a VHD that holds the system, SOUND.DRV, XTMP3.O88 and one fixture.

THE INSTRUMENT IS THE GUEST'S CYCLE COUNTER, two ways at once. The machine
is driven by `advance` in steps of a pseudo-random 20,000 to 40,000 cycles
(a fixed seed). Every step's stopping IP is a sample UNIFORM IN GUEST
CYCLES: the host's own speed, which varies by instruction (a VGA write costs
the emulator far more than an ADD), cannot weight it - a host-timed sampler
put the face's VGA writes at a quarter of the conversion. Every step also
stops at the exec breakpoints armed on the engine's own procs, whose cycle
counts are the stamps. Seconds are cycles / 4,772,727. Host load moves
neither.

Legs:
  std     double-click AFTERHRS.MP3 (11,025 Hz mono, 24 kbps, 10.0 s): Auto
          calibrates - each probe stamped (the part's fetch, its OPEN, the
          ring's priming and the timed granules) - finds no live rung and
          asks for a drive; Enter, Enter (the drive dialog, the Save dialog);
          the conversion on Auto's offline rung (XT Standard), its decode
          loop sampled for the per-stage split; finalisation; then the
          finished WAV through the card with the face's walk and needles
          stamped and the engine's granule entry armed. Asserts: the WAV
          off the VHD is tools/x3simxt.py's PCM after x3sim's trim, BIT FOR
          BIT; X3S_PEAK x 1024 <= 400,000; the playback decodes nothing
          (x3_gran1 never reached, x3_grans still, no backend resident) and
          never underruns
  fast    the same machine, Auto's dialog cancelled; the knob turned
          (= = = - - -) and stamped at READY; Q to XT FAST, D: the
          conversion, bit-exact
  speech  ...Q to XT SPEECH (divisor 2: 5,512 Hz), bit-exact
  spk     THE SPEAKER: tests/apspk.py's card-less 5150 (Hercules, XT-IDE)
          converts the 11 kHz track and plays the FINISHED WAV through the
          Audio Player's bracket; port 42h's first 800 writes must be
          tools/os88spkfx.py's counts for the WAV's own bytes, no GRANULE
          is reached while the door is open, and the kernel is left clean
  c128    C128.MP3 (44.1 kHz L/R stereo, 128 kbps, 1.5 s): the ladder's
          verdict and probes on this machine - the selection matrix's XT
          row - and the conversion it chooses: XT Standard takes its
          divisor from the source (SPEC.md 121.4, round 6), /4 here, so it
          converts at 11,025 Hz mono - which a DSP 2.01 plays - bit-exact to
          tools/x3simxt.py --profile std at that divisor (it was Quarter
          before round 6, no XT rung's 44.1 kHz output being playable)

BROKEN ON PURPOSE: `--expect fast` holds the std leg's XT Standard WAV to
XT Fast's model and FAILS at the first differing sample - which is what
proves the comparison compares.

CREDIBILITY (docs/xtmp3/FIELD.md section 1): MartyPC's 8088 is the 5150's
to within 0-4% on the CPU side (PERFORMANCE.md Part 9); its VGA's memory
wait states are not a field card's; and its XT-IDE has NO mechanical model -
a transfer is the option ROM's CPU loop and nothing else - so the disk's
share here is a floor.
"""
import argparse
import bisect
import json
import os
import random
import re
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
import os88build                                            # noqa: E402
import os88geom as geom                                     # noqa: E402
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402
import os88ui                                               # noqa: E402

HZ = 4772727.0
MACHINE = "os8088_xt_vga_hdd_sb"
TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
FIX = os.path.join(ROOT, "tests", "xtmp3")
PEAK_MAX = 400000
STEP = (20000, 40000)               # the sampler's stride, guest cycles

ST_IDLE, ST_CHECK, ST_CHOOSE, ST_DECODE, ST_READY, ST_LIVE, ST_FILE, \
    ST_PAUSED, ST_BUFFER, ST_DONE, ST_ERROR = range(11)
MD_IDLE, MD_CHECK, MD_LIVE, MD_FILE, MD_SPK, MD_CHOOSE, MD_CONV, MD_STEP = \
    range(8)
QNAME = ["AUTO", "ORIGINAL", "HALF", "QUARTER", "XT STANDARD", "XT FAST",
         "XT TURBO", "XT SPEECH"]
Q_XTSTD, Q_XTFAST, Q_XTSPEECH = 4, 5, 7
PROFILE = {Q_XTSTD: "std", Q_XTFAST: "fast", 6: "turbo", Q_XTSPEECH: "speech"}
BOUNDARY = ("x3f_walk.next", "x3f_walk.btns", "x3f_update.out")

bad = []


def check(leg, what, ok, more=""):
    print("  %s %s%s" % ("ok  " if ok else "FAIL", what,
                          (" - " + more) if more else ""))
    if not ok:
        bad.append("%s: %s%s" % (leg, what, (" (" + more + ")") if more else ""))
    return ok


def secs(c):
    return c / HZ


def r3(x):
    return None if x is None else round(x, 3)


def stat(v):
    if not v:
        return None
    v = sorted(v)
    ms = lambda c: round(1000.0 * secs(c), 2)                   # noqa: E731
    return {"n": len(v), "min_ms": ms(v[0]), "median_ms": ms(v[len(v) // 2]),
            "max_ms": ms(v[-1])}


# -----------------------------------------------------------------------------
# THE MAPS: the package's (every label, locals too), the two parts' and the
# kernel's - the primary's for the stamps, all four for the sampler
# -----------------------------------------------------------------------------
def nasm_map(src, built, incs=(), post=None):
    """{label: offset} out of `src` assembled with a map, checked to be the
    bytes of `built` (a map of a different image reads plausible rubbish)"""
    d = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    try:
        mp, b = os.path.join(d, "x.map"), os.path.join(d, "x.bin")
        subprocess.run(["nasm", "-f", "bin", "-w+error"]
                       + sum([["-I", i] for i in incs], [])
                       + ["--before", "[map symbols %s]" % mp, "-o", b, src],
                       check=True)
        if post:
            subprocess.run(post + [b], check=True, capture_output=True)
        if open(b, "rb").read() != open(os88build.at(built), "rb").read():
            sys.exit("xtmp3xt: %s is not this source's - run make" % built)
        out = {}
        for line in open(mp):
            m = re.match(r"\s*([0-9A-F]+)\s+([0-9A-F]+)\s+([\w.]+)$", line)
            if m:
                out[m.group(3)] = int(m.group(2), 16)
        return out
    finally:
        shutil.rmtree(d, ignore_errors=True)


P = PXT = PFR = PSTD = None


def maps():
    global P, PXT, PFR, PSTD
    P = nasm_map("apps/xtmp3/xtmp3.asm", "build/xtmp3.bin",
                 incs=("apps/", "apps/xtmp3/", "apps/audio/",
                       "drivers/sound/", "build/"))
    PXT = nasm_map("apps/xtmp3/x3xt.asm", "build/x3xt.bin",
                   incs=("apps/xtmp3/", "build/"),
                   post=[sys.executable, "tools/x3xtgen.py", "--seal"])
    PFR = nasm_map("apps/xtmp3/x3front.asm", "build/x3front.bin",
                   incs=("apps/", "apps/xtmp3/", "build/"))
    PSTD = nasm_map("apps/xtmp3/x3std.asm", "build/x3stdp.bin",
                    incs=("apps/", "apps/xtmp3/", "build/"))


class Labels(object):
    """the nearest preceding non-local label"""

    def __init__(self, table, base):
        rows = sorted((off, n) for n, off in table.items() if "." not in n)
        self.offs = [o for o, _ in rows]
        self.names = [n for _, n in rows]
        self.base = base

    def name(self, flat):
        i = bisect.bisect_right(self.offs, flat - self.base) - 1
        return self.names[i] if i >= 0 else "?"


class KLabels(object):
    """the kernel's code labels by FLAT address, over .text and .cold (the
    cold section is kernel code too - gfx_blit1_x lives there)"""

    def __init__(self):
        s, sec = os88sym.syms(), os88sym.sections()
        rows, self.ranges = [], []
        for want in (".text", ".cold"):
            lin = []
            for n, sc in sec.items():
                if sc == want and "." not in n:
                    try:
                        lin.append((os88sym.linear(n), n))
                    except Exception:                       # noqa: BLE001
                        pass
            if lin:
                lin.sort()
                end = lin[-1][0] + 2048
                if want == ".text":
                    end = os88sym.linear("kernel_text_end")
                self.ranges.append((lin[0][0], end))
                rows += lin
        rows.sort()
        self.offs = [a for a, _ in rows]
        self.names = [n for _, n in rows]

    def has(self, ip):
        return any(a <= ip < b for a, b in self.ranges)

    def name(self, ip):
        i = bisect.bisect_right(self.offs, ip) - 1
        return self.names[i] if i >= 0 else "?"


def kernel_labels():
    return KLabels()


# -----------------------------------------------------------------------------
# THE MACHINE
# -----------------------------------------------------------------------------
class Rig(object):
    def __init__(self, leg, fixture, machine=MACHINE, sound=True):
        self.leg = leg
        self.fix = fixture
        self.tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"),
                                    prefix="x3xt%d" % os.getpid())
        self.vhd = os.path.abspath(os.path.join(self.tmp, "c.vhd"))
        subprocess.run(
            [sys.executable, "tools/os88hdd.py", "--template", TEMPLATE,
             "--out", self.vhd, "--kernel", os88build.at("build/kernel.sys"),
             "--vbr", os88build.at("build/boothd.bin"),
             "--mbr", os88build.at("build/mbr.bin"),
             "--file", "XTMP3.O88=" + os88build.at("build/xtmp3.o88"),
             "--file", "%s=%s" % (fixture, os.path.join(FIX, fixture))]
            + (["--file", "SOUND.DRV=" + os88build.at("build/sound.drv")]
               if sound else []),
            check=True, capture_output=True)
        self.m = os88marty.launch(None, machine=machine,
                                  label="xtmp3xt-" + leg,
                                  extra=["--mount", "hd:0:" + self.vhd])
        self.ui = os88ui.UI(self.m, verbose=False)
        self.ui.ready(limit=300)
        self.seg = None
        self.rnd = random.Random(8088)

    def close(self):
        if self.m is not None:
            self.m.close()
            self.m = None

    def done(self):
        self.close()
        shutil.rmtree(self.tmp, ignore_errors=True)

    # --- XTMP3's own state ----------------------------------------------------
    def find(self):
        wins = self.m.read(geom._sym(self.m, None)("wm_wins"),
                           geom.MAX_WIN * geom.WIN_SIZE)
        for i in range(geom.MAX_WIN):
            o = i * geom.WIN_SIZE
            fl = struct.unpack_from("<H", wins, o + geom.W_FLAGS)[0]
            seg = struct.unpack_from("<H", wins, o + geom.W_SEG)[0]
            if fl & 1 and seg and \
                    bytes(self.m.read((seg << 4) + 16, 5)) == b"XTMP3":
                return seg
        return None

    def a(self, name):
        return (self.seg << 4) + P[name]

    def rd(self, name, n=1):
        return bytes(self.m.read(self.a(name), n))

    def b(self, name):
        return self.rd(name)[0]

    def w(self, name):
        return struct.unpack("<H", self.rd(name, 2))[0]

    def st(self):
        s = self.rd("x3_st", 25)
        return dict(state=s[0], qual=s[1], qwant=s[2], flags=s[3],
                    rate=struct.unpack_from("<H", s, 8)[0], ch=s[10],
                    bits=s[11], pct=s[15],
                    speed=struct.unpack_from("<H", s, 16)[0],
                    under=struct.unpack_from("<H", s, 18)[0],
                    peak=struct.unpack_from("<H", s, 20)[0],
                    err=struct.unpack_from("<H", s, 22)[0], vol=s[24],
                    md=self.b("x3_md"))

    def errtext(self):
        p = self.st()["err"]
        if not p:
            return ""
        return bytes(self.m.read((self.seg << 4) + p, 40)).split(b"\0")[0] \
            .decode("latin-1")

    def meas(self):
        return list(struct.unpack("<8H", self.rd("x3l_meas", 16)))

    def until(self, cond, what, guest):
        os88marty.until(self.m, lambda _: cond(), what, poll=0.05,
                        guest=guest)

    def open_track(self):
        """double-click the fixture, then find XTMP3's segment and PAUSE: a
        document joins the queue at the engine's first tick, a pass after
        the window exists, so the ladder has not started (the caller checks)
        and every stamp is armed before it does"""
        self.ui.path("C:/" + self.fix)
        seg = self.find()
        self.m.pause()
        self.seg = seg or self.find()
        return self.seg

    # --- THE DRIVER -----------------------------------------------------------
    def drive(self, names, stop, guest=600.0, sample=False, regs=(),
              keys=(), mid=None):
        """Run the PAUSED machine in `advance` steps, exec breakpoints armed
        on `names` (the primary's labels), until stop(stamps) is true; leave
        it paused. Returns (stamps, samples): stamps [(name, cycles, regs)],
        regs read only at the names in `regs`; samples [(cycles, flat_ip)],
        one a step, when `sample`. `keys` are queued after arming. `mid` is
        (n, fn): fn() once, after n steps (a screenshot mid-way)."""
        m = self.m
        bps, by = [], {}
        for n in names:
            by[self.a(n)] = n
            bps.append({"type": "exec", "addr": self.a(n)})
        m.breakpoints(bps)
        for k in keys:
            m.key(k)
        stamps, samples = [], []
        c0 = int(m.status()["cycles"])
        steps = 0
        try:
            while True:
                steps += 1
                if mid and steps == mid[0]:
                    mid[1]()
                n = self.rnd.randint(*STEP) if sample else 1000000
                st = m.advance(cycles=n)
                c, ip = int(st["cycles"]), st["flat_ip"]
                if st["state"] == "breakpoint" and ip in by:
                    rg = m.regs() if by[ip] in regs else None
                    stamps.append((by[ip], c, rg))
                    if stop(stamps):
                        return stamps, samples
                elif sample:
                    samples.append((c, ip))
                if c - c0 > guest * HZ:
                    raise SystemExit(
                        "xtmp3xt: %s: %.0f guest seconds and the awaited "
                        "stamp never came (the last: %s)"
                        % (self.leg, guest, [s[:2] for s in stamps[-4:]]))
        finally:
            m.breakpoints([])


SHOTS = [None]
CALONLY = [False]


def shot(r, name):
    """the VGA's rendered screen as build/../<SHOTS>/<leg>-<name>.png"""
    if not SHOTS[0]:
        return
    os.makedirs(SHOTS[0], exist_ok=True)
    w, h, data = r.m.fbuf()
    os88marty.write_png_rgb(os.path.join(SHOTS[0], "marty-%s-%s.png"
                                         % (r.leg, name)), w, h, data)


def first(stamps, name, after=0):
    return next((c for n, c, _ in stamps if n == name and c > after), None)


def last(stamps, name, before=None):
    v = [c for n, c, _ in stamps
         if n == name and (before is None or c < before)]
    return v[-1] if v else None


# -----------------------------------------------------------------------------
# THE SPLIT: the decode loop's guest-uniform samples, by where the 8088 was
# -----------------------------------------------------------------------------
def bases(r, rung):
    """where everything sat during the decode loop: each region's flat base
    and its length, so an IP past one image's end is not charged to that
    image's last label"""
    xtseg = struct.unpack("<HH", r.rd("x3_bgran", 4))[1]
    sym = geom._sym(r.m, None)
    drv = bytes(r.m.read(sym("drv_tab"), 16 * 12))
    drivers = {}
    for row, name in ((0, "SOUND.DRV"), (1, "HDD.DRV")):
        seg = struct.unpack_from("<H", drv, row * 16 + 2)[0]
        if seg:
            drivers[name] = seg << 4
    std = rung < Q_XTSTD
    part = "build/x3stdp.bin" if std else "build/x3xt.bin"
    return {"backend": "STD" if std else "XT",
            "xt": [xtseg << 4, os.path.getsize(os88build.at(part))],
            "front": [r.w("x3p_fcs") << 4,
                      os.path.getsize(os88build.at("build/x3front.bin"))],
            "primary": [r.seg << 4, max(P.values()) + 16],
            "drivers": drivers}


def split(rows, bs, kl):
    """the decode loop's guest-uniform samples, by where the 8088 was"""
    std = bs.get("backend") == "STD"
    regions = [("xt", Labels(PSTD if std else PXT, bs["xt"][0])),
               ("front", Labels(PFR, bs["front"][0])),
               ("primary", Labels(P, bs["primary"][0]))]
    coarse, fine = {}, {}
    for _, ip in rows:
        g = f = None
        for key, lab in regions:
            base, n = bs[key]
            if base <= ip < base + n:
                nm = lab.name(ip)
                if key == "xt":
                    g = "%s part: reconstruction" % ("STANDARD" if std else
                                                     "XT")
                    f = ("std:" if std else "xt:") + nm
                elif key == "front":
                    g, f = "FRONT part: side info, scalefactors, Huffman", \
                        "front:" + nm
                else:
                    if nm.startswith("x3p_"):
                        g = "primary: the front end's frame level (x3pars)"
                    elif nm.startswith(("x3f_", "x3d_", "os88ui")):
                        g = "primary: the face"
                    elif nm.startswith("x3c_"):
                        g = "primary: conversion (the stage, the progress " \
                            "window)"
                    else:
                        g = "primary: engine (look-ahead, trim, meters)"
                    f = "pkg:" + nm
                break
        if g is None and kl.has(ip):
            nm = kl.name(ip)
            if nm.startswith(("gfx_", "font_", "vga_", "cur_", "wm_", "menu_",
                              "fpg_", "band_", "soft", "clk_", "dock_")):
                g = "kernel: drawing"
            elif nm.startswith(("fat_", "dsk_", "file", "fs_", "dir_", "fm_",
                                "wseq", "rseq", "mem_", "osapi_file")):
                g = "kernel: file system, heap"
            elif nm.startswith(("sch_", "task_", "idle", "evq_", "kbm_",
                                "mou_", "snd_", "ui_", "tmr_")):
                g = "kernel: scheduler, ISRs, idle, UI task"
            else:
                g = "kernel: other"
            f = "k:" + nm
        if g is None:
            for dn, dbase in bs["drivers"].items():
                if dbase <= ip < dbase + 0x4000:
                    g = f = "driver: " + dn
                    break
        if g is None:
            if ip >= 0xF0000:
                g, f = "system BIOS", "rom:%05X" % (ip & ~0xFF)
            elif ip >= 0xC0000:
                g, f = "option ROMs (XT-IDE int 13h, VGA BIOS)", \
                    "rom:%05X" % (ip & ~0xFF)
            else:
                g, f = "elsewhere", "low:%05X" % (ip & ~0xFFF)
        coarse[g] = coarse.get(g, 0) + 1
        fine[f] = fine.get(f, 0) + 1
    n = max(1, len(rows))

    def pc(d, k):
        return {a: round(100.0 * b / n, 1)
                for a, b in sorted(d.items(), key=lambda x: -x[1])[:k]}
    return {"samples": len(rows), "coarse": pc(coarse, 99),
            "fine": pc(fine, 40)}


# -----------------------------------------------------------------------------
# THE WAV off the VHD, and the model's PCM
# -----------------------------------------------------------------------------
def _part(vhd):
    lba = struct.unpack_from("<I", open(vhd, "rb").read(512), 446 + 8)[0]
    return "%s@@%d" % (vhd, lba * 512)


def vhd_file(vhd, name):
    r = subprocess.run(["mcopy", "-n", "-i", _part(vhd), "::" + name, "-"],
                       capture_output=True)
    return r.stdout if r.returncode == 0 else None


def vhd_dir(vhd):
    r = subprocess.run(["mdir", "-a", "-b", "-i", _part(vhd), "::"],
                       capture_output=True, text=True)
    return [ln.strip() for ln in r.stdout.splitlines()]


def model_pcm(fixture, rung, och):
    """the bytes the WAV must hold: tools/x3simxt.py's PCM for an XT rung,
    tools/x3sim.py's STANDARD model for Original/Half/Quarter (OCH channels:
    1 on a stereo source is the downmix) converted to U8 as the engine asks
    the part to (the int16's high byte XOR 80h) - both after x3sim's trim"""
    import x3sim as S
    data = open(os.path.join(FIX, fixture), "rb").read()
    if rung >= Q_XTSTD:
        import x3simxt as X
        prof = PROFILE[rung]
        if prof == "std" and X.stdd_div(S.Stream(data).h0.rate):
            prof = "stdd"                   # XT Standard divided (round 6)
        pcm, rate, dec = X.decode(data, prof)
        d, p = S.trim_counts(S.Stream(data).info)
        d //= dec.div
        p //= dec.div
        return bytes(pcm[d:len(pcm) - p if p else len(pcm)].tobytes()), rate
    div = {1: 1, 2: 2, 3: 4}[rung]
    pcm, rate, st, dec = S.decode_file(data, "int", div, mono=(och == 1),
                                       trim=True)
    u8 = ((pcm.astype("int32") >> 8) & 0xFF) ^ 0x80
    return bytes(u8.astype("uint8").reshape(-1).tobytes()), rate


def wav_chunks(raw):
    fmt = struct.unpack_from("<HHIIHH", raw, 20)
    o, data, idn = 12, None, None
    while o + 8 <= len(raw):
        cid, n = raw[o:o + 4], struct.unpack_from("<I", raw, o + 4)[0]
        if cid == b"data":
            data = raw[o + 8:o + 8 + n]
        elif cid == b"xtm3":
            idn = raw[o + 8:o + 8 + n]
        o += 8 + n + (n & 1)
    return fmt, data, idn


# -----------------------------------------------------------------------------
# THE PHASES
# -----------------------------------------------------------------------------
CAL = ("x3l_start", "x3l_launch", "x3l_probed", "x3l_done", "x3_be_get",
       "x3_wsclaim", "x3_shrink", "x3_la_claim", "x3c_dopen")


def calibrate(r, leg, res):
    """the ladder, stamped: per probe the part's fetch, its OPEN, then the
    ring's priming and the timed granules; to the drive dialog's open"""
    stamps, _ = r.drive(CAL, lambda s: s[-1][0] == "x3c_dopen", guest=180,
                        regs=("x3l_launch",))
    r.m.run()
    r.until(lambda: r.b("x3_md") == MD_CHOOSE, "the decode-first dialog", 30)
    os88marty.guest_sleep(r.m, 1.5)
    r.m.pause()
    shot(r, "dialog")
    res["cal_peak_kb"] = r.st()["peak"]
    res["free0_kb"] = r.w("x3_free0")
    m = r.meas()
    t0, t1 = first(stamps, "x3l_start"), first(stamps, "x3l_done")
    probes, cur = [], None
    for n, c, rg in stamps:
        if c > t1:
            break
        if n == "x3l_launch":
            cur = {"rung": QNAME[rg["ax"] & 0xFF], "t0": c}
            probes.append(cur)
        elif cur is not None and n not in cur:
            cur[n] = c
    out = {"wall_s": r3(secs(t1 - t0)),
           "to_dialog_s": r3(secs(first(stamps, "x3c_dopen") - t0)),
           "probes": []}
    for p in probes:
        q = QNAME.index(p["rung"])
        row = {"rung": p["rung"],
               "measured_x100": None if m[q] == 0xFFFF else m[q],
               "total_s": r3(secs(p.get("x3l_probed", p["t0"]) - p["t0"]))}
        if "x3_wsclaim" in p:
            row["fetch_s"] = r3(secs(p["x3_wsclaim"] - p["x3_be_get"]))
        if "x3_shrink" in p:
            row["open_s"] = r3(secs(p["x3_shrink"] - p["x3_wsclaim"]))
        if "x3_la_claim" in p and "x3l_probed" in p:
            row["prime_probe_s"] = r3(secs(p["x3l_probed"] - p["x3_la_claim"]))
        out["probes"].append(row)
    res["calibration"] = out
    print("     calibration: %.3f s of guest time to the decision, %.3f s to "
          "the dialog; X3S_PEAK %d KB through it, the heap %d KB free at "
          "launch" % (out["wall_s"], out["to_dialog_s"], res["cal_peak_kb"],
                      res["free0_kb"]))
    check(leg, "X3S_PEAK through the calibration x 1024 <= 400,000 (SPEC.md "
          "121.10 is the XT path's WHOLE working set: the probes too)",
          0 < res["cal_peak_kb"] * 1024 <= PEAK_MAX, "%d KB = %d bytes" % (
              res["cal_peak_kb"], res["cal_peak_kb"] * 1024))
    for p in out["probes"]:
        print("       %-12s %5s x100  %.3f s  (fetch %s, OPEN %s, prime and "
              "probe %s)" % (p["rung"], p["measured_x100"], p["total_s"],
                             p.get("fetch_s"), p.get("open_s"),
                             p.get("prime_probe_s")))
    return out


CONV = ("x3c_start", "x3c_dopen", "x3_be_get", "x3_wsclaim", "x3_shrink",
        "x3c_final", "x3c_ended", "x3_file_play")


def convert(r, leg, rung, res, expect):
    """from the decode-first dialog: Enter, the Save dialog's Enter; the
    conversion and finalisation stamped and sampled; the playback; the WAV
    off the VHD against the model"""
    m = r.m
    kl = kernel_labels()
    m.run()
    r.until(lambda: r.st()["md"] == MD_CHOOSE, "the drive dialog", 120)
    m.key("Enter")
    r.ui.chooser()
    m.pause()
    g0 = r.w("x3_grans")
    stamps, rows = r.drive(CONV, lambda s: s[-1][0] == "x3_file_play",
                           guest=1500, sample=True, keys=("Enter",),
                           mid=(1500, lambda: shot(r, "converting")))
    g1 = r.w("x3_grans")
    m.run()
    r.until(lambda: r.st()["state"] in (ST_FILE, ST_DONE, ST_ERROR),
            "the finished file to play", 60)
    s = r.st()
    if not check(leg, "the conversion finished and the WAV plays",
                 s["state"] in (ST_FILE, ST_DONE),
                 "state %d md %d '%s'" % (s["state"], s["md"], r.errtext())):
        return None
    c_start = first(stamps, "x3c_start")
    c_loop = first(stamps, "x3c_dopen", c_start)
    c_play = first(stamps, "x3_file_play", c_start)
    c_fin = last(stamps, "x3c_final", c_play)
    beg, ws, shr = (first(stamps, n, c_start)
                    for n in ("x3_be_get", "x3_wsclaim", "x3_shrink"))
    peak = s["peak"]
    res["peak_kb"] = peak
    res["fmt"] = [s["rate"], s["ch"], s["bits"]]
    res["x3s_speed_x100"] = s["speed"]
    check(leg, "X3S_PEAK x 1024 <= 400,000 (SPEC.md 121.10)",
          0 < peak * 1024 <= PEAK_MAX, "%d KB = %d bytes" % (peak, peak * 1024))
    m.pause()
    loop = [x for x in rows if c_loop <= x[0] <= c_fin]
    bs = bases(r, rung)
    res["split"] = split(loop, bs, kl)
    SAMPLES[leg] = {"bases": bs, "rows": loop}
    playback(r, leg, res)
    r.close()
    wav = r.fix.split(".")[0] + ".WAV"
    raw = vhd_file(r.vhd, wav)
    names = vhd_dir(r.vhd)
    check(leg, "%s is on C: and no .TMP is left" % wav, raw is not None
          and not any(".TMP" in n for n in names), str(names))
    if raw is None:
        return None
    fmt, data, idn = wav_chunks(raw)
    want, mrate = model_pcm(r.fix, expect, fmt[1])
    diff = next((i for i in range(min(len(data), len(want)))
                 if data[i] != want[i]), None)
    check(leg, "fmt: %d Hz mono 8-bit" % mrate,
          (fmt[1], fmt[2], fmt[5]) == (1, mrate, 8), str(fmt))
    check(leg, "the WAV off the VHD is %s after x3sim's trim, BIT FOR BIT"
          % ("tools/x3simxt.py --profile " + PROFILE[expect]
             if expect >= Q_XTSTD else "tools/x3sim.py's STANDARD model at %s"
             % QNAME[expect]),
          len(data) == len(want) and diff is None,
          "%d bytes against %d%s" % (len(data), len(want), "" if diff is None
                                     else ", the first difference at %d" % diff))
    check(leg, "its xtm3 identity names %s" % QNAME[rung],
          idn is not None and idn[21] == rung,
          str(None if idn is None else QNAME[idn[21]]))
    audio = len(data) / float(fmt[2])
    conv = {"rung": QNAME[rung], "audio_s": round(audio, 4),
            "frames": len(data), "rate": fmt[2], "granules": g1 - g0,
            "job_s": r3(secs(c_play - c_start)),
            "setup_s": r3(secs(c_loop - c_start)),
            "fetch_s": r3(secs(ws - beg)) if beg and ws else None,
            "open_s": r3(secs(shr - ws)) if ws and shr else None,
            "loop_s": r3(secs(c_fin - c_loop)),
            "final_s": r3(secs(c_play - c_fin))}
    conv["x_job"] = round(audio / conv["job_s"], 4)
    conv["x_loop"] = round(audio / conv["loop_s"], 4)
    conv["cyc_per_granule"] = int((c_fin - c_loop) / max(1, g1 - g0))
    res["conversion"] = conv
    print("     conversion %s: %.2f s of audio; the job %.2f s = %.4fx (setup "
          "%.2f s: the part's fetch %s, OPEN %s; the loop %.2f s = %.4fx, "
          "%d granules at %d cycles; finalisation %.2f s); XTMP3 showed "
          "%.2fx" % (conv["rung"], audio, conv["job_s"], conv["x_job"],
                     conv["setup_s"], conv["fetch_s"], conv["open_s"],
                     conv["loop_s"], conv["x_loop"], conv["granules"],
                     conv["cyc_per_granule"], conv["final_s"],
                     s["speed"] / 100.0))
    sp = res["split"]
    print("     the decode loop, %d guest-uniform samples:" % sp["samples"])
    for k, v in sp["coarse"].items():
        print("       %5.1f%%  %s" % (v, k))
    for k, v in list(sp["fine"].items())[:20]:
        print("              %5.1f%%  %s" % (v, k))
    return conv


def element_spans(stamps, entry, drew):
    """(drawn, unchanged): cycles from `entry` to the walk's next element,
    split by whether `drew` came between"""
    draws, cached, cur = [], [], None
    for n, c, _ in stamps:
        if n in BOUNDARY and cur:
            (draws if cur[1] else cached).append(c - cur[0])
            cur = None
        if n == entry:
            cur = [c, False]
        elif n == drew and cur:
            cur[1] = True
    return draws, cached


def playback(r, leg, res):
    """the finished WAV on the card, to its end: the engine's granule entry
    armed (it must never stop there), the face's walk, its dial elements and
    the needles' redraws stamped"""
    m = r.m
    g0 = r.w("x3_grans")
    s0 = r.st()
    bpart = r.b("x3_bpart")
    stamps, _ = r.drive(("x3_gran1", "x3f_update", "x3f_update.out",
                         "x3f_walk.next", "x3f_walk.btns", "x3f_e_dial",
                         "x3f_e_dial.draw", "x3_advance"),
                        lambda s: s[-1][0] == "x3_advance", guest=60,
                        mid=(4, lambda: shot(r, "playing")))
    m.run()
    r.until(lambda: r.st()["state"] == ST_DONE, "Finished", 20)
    s = r.st()
    g1 = r.w("x3_grans")
    hits = sum(1 for n, _, _ in stamps if n == "x3_gran1")
    check(leg, "the finished WAV plays with NO decoder resident",
          bpart == 0xFF, "x3_bpart %02X" % bpart)
    check(leg, "...and NO GRANULE: x3_gran1 never reached, x3_grans still",
          hits == 0 and g1 == g0, "%d stops, %d -> %d" % (hits, g0, g1))
    check(leg, "...and no underrun", s["under"] == s0["under"] == 0,
          "%d -> %d" % (s0["under"], s["under"]))
    t0 = first(stamps, "x3f_update")
    span = stamps[-1][1] - t0 if t0 else 0
    upd, ou = [], None
    for n, c, _ in stamps:
        if n == "x3f_update":
            ou = c
        elif n == "x3f_update.out" and ou is not None:
            upd.append(c - ou)
            ou = None
    draw, cached = element_spans(stamps, "x3f_e_dial", "x3f_e_dial.draw")
    face = {"span_s": r3(secs(span)), "updates": stat(upd),
            "needle_redraw": stat(draw), "dial_unchanged": stat(cached),
            "share_pct": round(100.0 * sum(upd) / span, 1) if span else None}
    res["playback"] = {"granule_calls": hits, "underruns": s["under"],
                       "face": face}
    print("     the face over %.1f s of the WAV's play: x3f_update %s; a "
          "needle's redraw %s; a dial with nothing to draw %s; the face's "
          "share of the machine %s%%" % (secs(span), face["updates"],
                                         face["needle_redraw"],
                                         face["dial_unchanged"],
                                         face["share_pct"]))


def knob(r, leg, res):
    """at READY: = = = - - -, each knob element stamped from x3f_e_knob to the
    walk's next element; the volume back where it started"""
    m = r.m
    m.pause()
    v0 = r.st()["vol"]
    names = BOUNDARY + ("x3f_e_knob", "x3f_ekdone")
    every = []
    for k in ("Equal", "Equal", "Equal", "Minus", "Minus", "Minus"):
        def drawn(s):
            d, _ = element_spans(s, "x3f_e_knob", "x3f_ekdone")
            return bool(d)
        stamps, _ = r.drive(names, drawn, guest=30, keys=(k,))
        every += stamps
    m.run()
    draws, cached = element_spans(every, "x3f_e_knob", "x3f_ekdone")
    v1 = r.st()["vol"]
    check(leg, "the knob turned six detents and came back to %d" % v0,
          v1 == v0 and len(draws) == 6, "%d, %d redraws" % (v1, len(draws)))
    res["knob"] = {"redraw": stat(draws), "unchanged": stat(cached)}
    print("     the knob at READY: a redraw %s; unchanged %s"
          % (res["knob"]["redraw"], res["knob"]["unchanged"]))


# -----------------------------------------------------------------------------
# THE LEGS
# -----------------------------------------------------------------------------
def opened(r, leg):
    seg = r.open_track()
    if not check(leg, "XTMP3 opens on %s" % r.fix, seg is not None):
        return False
    return check(leg, "...found before its ladder started",
                 r.b("x3_md") == MD_IDLE, "md %d" % r.b("x3_md"))


def leg_auto(leg, fixture, out, convert_too=True):
    res = out.setdefault(leg, {"fixture": fixture})
    r = Rig(leg, fixture)
    try:
        if not opened(r, leg):
            return
        cal = calibrate(r, leg, res)
        s = r.st()
        res["decision"] = "Decode first" if s["md"] == MD_CHOOSE else \
            "live %s" % QNAME[s["qual"]]
        res["offline_rung"] = QNAME[r.b("x3c_rung")]
        res["meas_x100"] = r.meas()
        res["device"] = "card %d, caps %04X" % (r.b("x3_card"), r.w("x3_caps"))
        print("     decision: %s; the conversion's rung %s; %s"
              % (res["decision"], res["offline_rung"], res["device"]))
        check(leg, "an XT goes to Decode first: no live rung passes 1.25x",
              s["md"] == MD_CHOOSE and s["state"] == ST_CHOOSE,
              "md %d state %d" % (s["md"], s["state"]))
        probed = [p["rung"] for p in cal["probes"]]
        check(leg, "Half was never probed (a cheaper rung failed first)",
              "HALF" not in probed, str(probed))
        if not convert_too or CALONLY[0]:
            return
        rung = r.b("x3c_rung")
        check(leg, "Auto's offline rung is %s" % QNAME[OFFLINE[leg]],
              rung == OFFLINE[leg], QNAME[rung])
        convert(r, leg, rung, res, EXPECT.get(leg, rung))
    finally:
        r.done()


def leg_override(leg, rung, out, knob_too=False):
    res = out.setdefault(leg, {"fixture": "AFTERHRS.MP3"})
    r = Rig(leg, "AFTERHRS.MP3")
    try:
        if not opened(r, leg):
            return
        m = r.m
        m.run()
        r.until(lambda: r.st()["md"] == MD_CHOOSE, "Auto's dialog", 120)
        m.key("Escape")
        r.until(lambda: r.st()["md"] == MD_IDLE, "the dialog's cancel", 30)
        if knob_too:
            knob(r, leg, res)
        while r.st()["qwant"] != rung:
            q = r.st()["qwant"]
            m.key("KeyQ")
            r.until(lambda: r.st()["qwant"] != q, "Q to move", 20)
        check(leg, "the override is %s" % QNAME[rung], r.st()["qwant"] == rung)
        m.key("KeyD")
        convert(r, leg, rung, res, EXPECT.get(leg, rung))
    finally:
        r.done()


SPK_MACHINE = "os8088_5150_herc_hdd_gla"
SPK_PULSES = 800        # tests/apspk.py's: the first 800 writes to port 42h
SPK_LOSS = 0.04


def leg_spk(out):
    """THE SPEAKER (SPEC.md 121.6, 86.21): a card-less 640 KB 5150 with a
    Hercules and an XT-IDE disk - tests/apspk.py's machine - converts the
    11 kHz track (Auto, decode first, Enter, Enter) and the FINISHED file
    plays through the Audio Player's bracket. Port 42h is traced from the
    door's open: its first 800 writes must be tools/os88spkfx.py's plan,
    resampler and shaper over the WAV's own bytes (apspk.expect, the rung
    the bracket ran), after at most a few silent pulses; the lost share
    under 4%; x3_gran1 never reached while it plays; the play to the end
    and the kernel left clean (channel 2 free, no sample ISR)"""
    import apspk
    import os88spkfx as fx
    leg = "spk"
    res = out.setdefault(leg, {"fixture": "AFTERHRS.MP3",
                               "machine": SPK_MACHINE})
    r = Rig(leg, "AFTERHRS.MP3", machine=SPK_MACHINE, sound=False)
    try:
        if not opened(r, leg):
            return
        m = r.m
        m.run()
        r.until(lambda: r.st()["md"] == MD_CHOOSE, "the decode-first dialog",
                180)
        rung = r.b("x3c_rung")
        res["offline_rung"] = QNAME[rung]
        res["card"] = r.b("x3_card")
        m.key("Enter")
        r.ui.chooser()
        k_open, k_off = m.sym("osapi_fsx_spk"), m.sym("spk_off")
        ex_o = {"type": "exec", "addr": k_open}
        ex_c = {"type": "exec", "addr": k_off}
        ex_d = {"type": "exec", "addr": r.a("os88spk_grant.dry")}
        ex_g = {"type": "exec", "addr": r.a("x3_gran1")}
        io42 = {"type": "io", "addr": 0x42}
        ev = {"open": [], "close": [], "w": [], "dry": [], "gran": 0,
              "rungs": []}
        a_rung = r.a("aps_rung")

        def hit(mm, rec):
            a = rec.get("addr")
            if a == k_open:
                if mm.regs()["ax"] & 0xFF == 0:
                    ev["open"].append(rec["cycles"])
                    ev["rungs"].append(mm.read(a_rung, 1)[0])
                    if len(ev["open"]) == 1:
                        mm.breakpoints([ex_o, ex_c, ex_d, ex_g, io42])
            elif a == k_off:
                if ev["open"]:
                    ev["close"].append(rec["cycles"])
            elif a == ex_d["addr"]:
                ev["dry"].append(rec["cycles"])
            elif a == ex_g["addr"]:
                ev["gran"] += 1
            elif ev["open"] and len(ev["w"]) < SPK_PULSES:
                ev["w"].append((rec["cycles"], mm.regs()["ax"] & 0xFF))
                if len(ev["w"]) >= SPK_PULSES:
                    mm.breakpoints([ex_o, ex_c, ex_d, ex_g])
        with os88marty.bp_trace(m, ex_o, ex_c, on_hit=hit, cap=100000) as tr:
            m.key("Enter")
            tr.until(lambda: bool(ev["close"]) and
                     r.st()["state"] == ST_DONE and r.b("aps_on") == 0,
                     "the conversion, then the speaker's play to its end",
                     limit=2400.0)
        s = r.st()
        aps_rung = r.b("aps_rung")
        ch2 = m.read(m.sym("snd_ch2mode"), 1)[0]
        sseg = bytes(m.read(m.sym("spk_seg"), 2))
        check(leg, "no card: the finished file played through the speaker "
              "(MD_SPK) to the end", res["card"] == 0 and len(ev["open"]) >= 1
              and s["state"] == ST_DONE, "card %d, %d opens, state %d"
              % (res["card"], len(ev["open"]), s["state"]))
        check(leg, "...with NO GRANULE: x3_gran1 never reached while the door "
              "was open", ev["gran"] == 0, "%d stops" % ev["gran"])
        check(leg, "the kernel left clean: channel 2 free, no sample ISR",
              ch2 == 0 and sseg == b"\0\0", "mode %d, seg %s" % (ch2,
                                                                 sseg.hex()))
        r.close()
        raw = vhd_file(r.vhd, "AFTERHRS.WAV")
        if not check(leg, "AFTERHRS.WAV is on C:", raw is not None):
            return
        fmt, data, idn = wav_chunks(raw)
        want, mrate = model_pcm(r.fix, rung, fmt[1])
        check(leg, "the WAV is %s's model, BIT FOR BIT" % QNAME[rung],
              data == want, "%d bytes against %d" % (len(data), len(want)))
        # THE TRACE IS THE FIRST DOOR'S: its 800 writes are made at the rung
        # the bracket opened on, before any fall (a later door reopens
        # mid-stream at the sample it reached, on a slower rung)
        rung0 = ev["rungs"][0] if ev["rungs"] else aps_rung
        counts, rr = apspk.expect(data, fmt[2], rung=rung0)
        n = fx.spk_n(rr)
        got = bytes(c for _, c in ev["w"])
        at, lead = -1, 0
        for lead in range(12):
            at = counts.find(got[lead:])
            if at >= 0:
                break
        cy = ev["w"][-1][0] - ev["w"][0][0] if ev["w"] else 0
        edges = cy / (4.0 * n)
        lost = edges - (len(ev["w"]) - 1)
        played = (ev["close"][-1] - ev["open"][0]) / HZ
        want_s = len(counts) / (1193182.0 / n)
        res["speaker"] = {"source_hz": fmt[2], "pulse_hz": rr,
                          "bracket_rungs": ev["rungs"],
                          "bracket_rung_at_end": aps_rung, "pulses_traced": len(got),
                          "silent_lead": lead, "model_offset": at,
                          "lost_pct": round(100.0 * lost / max(1, edges), 2),
                          "played_s": round(played, 3),
                          "model_s": round(want_s, 3),
                          "door_opens": len(ev["open"]),
                          "dry_grants": len(ev["dry"]),
                          "granule_calls": ev["gran"]}
        print("     the speaker: %d Hz -> %d Hz (the first door's rung %d; "
              "rungs %s): %d pulses, "
              "after %d silent %s; lost %.1f%%; played %.2f s of %.2f; the "
              "ring dry %d times; %d opens" % (
                  fmt[2], rr, rung0, ev["rungs"], len(got), lead,
                  "THE MODEL'S from %d" % at if at >= 0 else "NOT the model's",
                  100.0 * lost / max(1, edges), played, want_s,
                  len(ev["dry"]), len(ev["open"])))
        check(leg, "port 42h's first %d writes are tools/os88spkfx.py's "
              "counts for the WAV's own bytes" % SPK_PULSES,
              at >= 0 and len(got) == SPK_PULSES,
              "first %s; the model starts %s" % (list(got[:10]),
                                                  list(counts[:10])))
        check(leg, "under %d%% of the pulses lost" % int(SPK_LOSS * 100),
              edges and lost <= SPK_LOSS * edges,
              "%.1f%%" % (100.0 * lost / max(1, edges)))
    finally:
        r.done()


EXPECT = {}
SAMPLES = {}
# Auto's conversion rung on this machine: XT Standard for the 11 kHz track;
# for the 44.1 kHz one the DSP 2.01 plays nothing above 22,222 Hz, so no XT
# rung (whose output is the source's rate) is even tried, and the cheapest
# STANDARD rung, Quarter at 11,025 Hz, is what converts
OFFLINE = {"std": Q_XTSTD, "c128": Q_XTSTD}     # c128: XT Standard at /4 (round 6)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--leg", action="append",
                    choices=("std", "fast", "speech", "c128", "spk"))
    ap.add_argument("--json", help="write every figure here")
    ap.add_argument("--shots", help="save the VGA's screen at the dialog, "
                    "mid-conversion and mid-play into this folder")
    ap.add_argument("--calonly", action="store_true",
                    help="std and c128: stop at the decode-first dialog")
    ap.add_argument("--expect", choices=("std", "fast", "speech"),
                    help="BROKEN ON PURPOSE: hold the std leg's WAV to this "
                    "profile's model")
    a = ap.parse_args()
    if a.expect:
        EXPECT["std"] = {"std": Q_XTSTD, "fast": Q_XTFAST,
                         "speech": Q_XTSPEECH}[a.expect]
    SHOTS[0] = a.shots
    CALONLY[0] = a.calonly
    maps()
    out = {"machine": MACHINE, "guest_hz": HZ}
    for leg in a.leg or ["std", "fast", "speech", "c128", "spk"]:
        print("xtmp3xt: %s" % leg)
        t = time.time()
        if leg == "std":
            leg_auto("std", "AFTERHRS.MP3", out)
        elif leg == "c128":
            leg_auto("c128", "C128.MP3", out)
        elif leg == "fast":
            leg_override("fast", Q_XTFAST, out, knob_too=True)
        elif leg == "speech":
            leg_override("speech", Q_XTSPEECH, out)
        elif leg == "spk":
            leg_spk(out)
        print("     (%.0f s of host time)" % (time.time() - t))
    if a.json:
        with open(a.json, "w") as f:
            json.dump(out, f, indent=1, sort_keys=True)
        with open(a.json + ".samples", "w") as f:
            json.dump(SAMPLES, f)
    for b in bad:
        print("FAIL: " + b)
    print("xtmp3xt: %s" % ("FAIL" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
