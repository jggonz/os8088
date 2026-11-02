#!/usr/bin/env python3
"""waverackdisk - SPEC.md 123.6: WaveRack PLAYS THROUGH DISK READS, a hard
disk first and a floppy second, measured against the build before it.

    make && make build/mixercfg/SYSTEM.CFG && python3 tests/waverackdisk.py
        [--leg hdd|floppy] [--before COMMIT] [--keep]

MARTYPC BY NAME, because the defect is a matter of TIME: disk.inc holds
[sch_lock] across every int 13h, so while anything reads, nothing converts
and the card plays alone from what is queued - and QEMU's floppy is
instantaneous, so it never showed. MartyPC's 5150 is a 4.77 MHz 8088 whose
cycle counter is exact. ITS HARD DISK HAS NO MECHANICAL MODEL
(docs/MARTYPC-DEBUG.md): XT-IDE, PIO, every byte copied by the CPU in the
option ROM with [sch_lock] held, ~38 KB/s - a HARSH model of the stall.
Counts are exact; milliseconds are not a field figure.

Each leg runs TWICE, on the same machine and the same files: BEFORE (the
package built from --before, Wave 4's commit by default, out of git into a
scratch tree) and AFTER (build/waverack.o88). What is measured, per play, is
the SILENCE: the guest seconds a play has been going, less the output samples
verb 9 says were played over the card's real rate (SOUND.DRV's time constant
truncates: 22,050 Hz is 22,222 on the card) - and wt_nunder, the underruns
WaveRack counted.

  hdd     os8088_5150_herc_hdd_sb_gla (XT-IDE, a Sound Blaster 2.0), a VHD
          made by tools/os88hdd.py holding tools/wrsim.py's DISK_FX:
          - BIG22.WAV (2 MB, 8-bit mono 22,050 Hz): the SCAN's guest seconds
            and its int 13h calls - FAT, directory and data - off the
            kernel's own int 13h; then 30 s of play: the silence and the
            underruns
          - BIG22 with ANOTHER WINDOW READING: a second WaveRack opened on
            ST22L, whose scan reads 1.7 MB while the first plays - what the
            ring covers, measured (and the second window closed after)
          - ST22S.WAV (16-bit stereo 22,050 Hz, 88 KB/s: its conversion alone
            is 2.1 s of an 8088's second): AFTER must HOLD it ('Loading',
            [wt_held]) and play it from memory with no underrun
          - ST22L.WAV (the same, 20 s: past the heap too): AFTER must say which
            cannot keep up - 'CPU too slow: gaps' on an 8088 - and the gaps
            are counted
  floppy  os8088_5150_herc_sb_gla_144: LONG.WAV (8-bit mono 11,025 Hz, 1.1
          MB) off a 1.44 MB floppy in B:, 30 s of play
  bigclu  the HDD machine on a 321 MB VHD of 32 KB CLUSTERS (os88hdd.py
          --spc 64, TotSec32 - what HDD.DRV formats past 1 GB): BEFORE must
          refuse it (`Cluster too large`); AFTER opens BIG22 (the I/O claim
          33 KB), plays 15 s with no underrun, and saves MID11.WAV as
          OUT32.WAV through a 32 KB output claim - read back off the VHD on
          the host and held to wrsim.write_wav byte for byte, no temp left

AFTER's legs are asserted (no underrun and no silence past the stream's own
start where the medium keeps up; the hold; the gaps said); BEFORE's are
measured and printed beside them, and asserted only to have been taken.
"""
import argparse
import os
import shutil
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

BEFORE = "4b8061193"                    # Wave 4: the grant ring, READ_AT
MACHINE_HDD = "os8088_5150_herc_hdd_sb_gla"
MACHINE_FD = "os8088_5150_herc_sb_gla_144"
TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
WT_IDLE, WT_PLAY = 0, 1
PLAY_S = 30.0                           # guest seconds a measured play runs
FAIL = []


def say(s):
    print(s, flush=True)


def check(what, ok, more=""):
    say("   %-60s %s%s" % (what, "ok" if ok else "FAIL",
                           "" if ok or not more else "  " + more))
    if not ok:
        FAIL.append(what)
    return ok


def card_rate(rate):
    """the rate SOUND.DRV's time constant really plays (it truncates)"""
    return 1000000.0 / (1000000 // rate)


# WAVE 4 AS SPEC.md 123.6 RECORDED IT, on this row's machines - the baseline
# when --before is not in this clone's history. WaveRack reached main as ONE
# squash (#311), so Wave 4's commit exists only in the history it was
# developed in; a tree cut from main cannot build it, and the row died in
# `git ls-tree` before measuring anything. The figures are that table's,
# taken by this row against that commit: what a rebuild would re-measure.
# An underrun count the table does not give is None.
WAVE4 = {
    "hdd": {"scan_s": 93.8, "scan_i13": {"fat": 18, "dir": 2, "data": 168},
            "big": {"under": None, "silence": 17.67},
            "comp": {"under": 5, "silence": 26.91},
            "sts": {"under": 2, "silence": 2.62},
            "stl": {"under": None, "silence": 11.76}},
    "floppy": {"long": {"under": None, "silence": 18.71}},
    "bigclu": {"status": "Cluster too large", "frames": 0},
}


def reachable(commit):
    """True when COMMIT is in this clone (a squash leaves it out)"""
    return subprocess.run(["git", "cat-file", "-e", commit + "^{commit}"],
                          capture_output=True).returncode == 0


def build_before(commit, tmp):
    """the package at COMMIT, out of git into TMP: (o88 path, syms, image)"""
    src = os.path.join(tmp, "src")
    os.makedirs(src)
    incs = [p for p in subprocess.run(
        ["git", "ls-tree", "--name-only", commit, "apps/"], check=True,
        capture_output=True, text=True).stdout.split() if p.endswith(".inc")]
    tar = subprocess.run(["git", "archive", commit, "apps/waverack"] + incs,
                         check=True, capture_output=True).stdout
    subprocess.run(["tar", "-x", "-C", src], input=tar, check=True)
    incl = (os.path.join(src, "apps") + "/",
            os.path.join(src, "apps", "waverack") + "/",
            os88build.at("build") + "/")
    syms, img = pkg_syms(os.path.join(src, "apps/waverack/waverack.asm"), incl)
    b = os.path.join(tmp, "before.bin")
    open(b, "wb").write(img)
    o = os.path.join(tmp, "WAVERACK.O88")
    subprocess.run([sys.executable, "tools/os88pkg.py", "--compress-if=lz4",
                    b, "-o", o], check=True, capture_output=True)
    return o, syms, img


class Rack(object):
    """one WaveRack window on a running MartyPC, read by its own map"""

    def __init__(self, m, ui, w, syms):
        self.m, self.ui, self.w, self.s = m, ui, w, syms
        rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE, geom.WIN_SIZE)
        self.base = (rec[geom.W_SEG] | rec[geom.W_SEG + 1] << 8) << 4

    def has(self, name):
        return name in self.s

    def rd(self, name, n):
        return self.m.read(self.base + self.s[name], n)

    def b(self, name):
        return self.rd(name, 1)[0]

    def w_(self, name):
        return int.from_bytes(self.rd(name, 2), "little")

    def d(self, name):
        return int.from_bytes(self.rd(name, 4), "little")

    def text(self, name, n=24):
        return self.rd(name, n).split(b"\0")[0].decode("latin-1")


def cyc(m):
    return int(m.status()["cycles"])


def guest_wait(m, secs, what):
    c0 = cyc(m)
    os88marty.until(m, lambda mm: cyc(mm) - c0 >= secs * os88marty.GUEST_HZ,
                    what, poll=0.5, guest=secs + 30)


def open_doc(ui, rk, name, scan_s):
    """File > Open..., the chooser's own row, and the scan to its end ->
    the scan's guest seconds"""
    m = ui.m
    ui.raise_window(rk.w)
    ui.menu_pick("File", "Open...")
    ui.chooser()
    c0 = cyc(m)
    g = rk.w_("wo_gen")
    ui.chooser_open(name)
    # the open's END, not a moment inside it: wo_open empties the document
    # first (the generation moves, nothing is busy) and only then lays the
    # piece and starts the scan - a fourth run caught that gap and pressed
    # Play into 'Scanning 4%'. So: the document IS this file and its view
    # cache built ([wv_ok], which the scan's end sets), or the open refused
    os88marty.until(m, lambda mm: rk.w_("wo_gen") != g and
                    rk.b("we_busy") == 0 and
                    ((rk.b("wo_doc") and rk.text("wo_name") == name and
                      rk.b("wv_ok")) or rk.b("ww_err")),
                    "%s's scan" % name, poll=0.05,
                    guest=scan_s)
    return (cyc(m) - c0) / os88marty.GUEST_HZ


def measure_play(ui, rk, label, secs=PLAY_S, more=None):
    """Space, then SECS guest seconds from the moment the card has played
    anything -> dict(silence, under, played, out, status, ...)"""
    m = ui.m
    ui.raise_window(rk.w)
    stale = rk.d("wt_played")           # the LAST play's count: wt_open zeroes
    m.key("Space")                      # it, and a hold's load comes first
    t_key = cyc(m)
    loaded = {"load_s": None, "said": set()}

    def started(mm):
        loaded["said"].add(rk.text("wr_stbuf"))
        n = rk.d("wt_played")
        return (rk.b("wt_state") == WT_PLAY and n > 0 and n != stale and
                not (rk.has("wt_hload") and rk.b("wt_hload")))
    try:
        os88marty.until(m, started, label + " to sound", poll=0.05,
                        guest=240.0)
    except os88marty.MartyError:
        say("     %s never sounded: state %d, status %r, said %r, windows %r"
            % (label, rk.b("wt_state"), rk.text("wr_stbuf"),
               sorted(loaded["said"]), ui.titles()))
        raise
    c0 = cyc(m)
    loaded["load_s"] = (c0 - t_key) / os88marty.GUEST_HZ
    held0 = rk.b("wt_held") if rk.has("wt_held") else None
    if more:
        # the second window's launch takes the guest a minute and more under
        # a play (the UI task is the play's too), long enough for BIG22 to
        # END inside it - which a third run measured as a 0.1 s window. So
        # once it is up, the first play SEEKS back to its start (Home) and
        # the window begins from that new stream's first sample
        more()
        loaded["more_s"] = (cyc(m) - c0) / os88marty.GUEST_HZ
        ui.raise_window(rk.w)
        m.key("Home" if rk.b("wt_state") == WT_PLAY else "Space")
        os88marty.until(m, lambda mm: rk.b("wt_state") == WT_PLAY and
                        0 < rk.d("wt_played") < 22050, label + ", sought",
                        poll=0.05, guest=60.0)
        c0 = cyc(m)
    p0 = rk.d("wt_played")
    end = c0 + secs * os88marty.GUEST_HZ
    seen = set()

    def done(mm):
        seen.add(rk.text("wr_stbuf"))
        return cyc(mm) >= end or rk.b("wt_state") != WT_PLAY
    # (poll is HOST seconds, and MartyPC runs ~6x: 0.05 s is ~0.3 of the
    # guest's - at 0.5 the window overshot by three guest seconds, enough
    # to run a 4 s play to its end and count the silence after it)
    os88marty.until(m, done, label + " to play", poll=0.05, guest=secs + 60)
    c1, p1 = cyc(m), rk.d("wt_played")
    st = rk.b("wt_state")
    out = card_rate(rk.w_("wt_out"))
    dt = (c1 - c0) / os88marty.GUEST_HZ
    r = dict(dt=dt, played=p1 - p0, out=out, under=rk.w_("wt_nunder"),
             silence=max(0.0, dt - (p1 - p0) / out), ended=st != WT_PLAY,
             said=loaded["said"] | seen, load_s=loaded["load_s"],
             status=rk.text("wr_stbuf"), held0=held0,
             more_s=loaded.get("more_s"))
    for k in ("wt_held", "wt_slow", "wt_rdsz", "wt_ext"):
        if rk.has(k):
            r[k] = rk.w_(k) if k == "wt_rdsz" else rk.b(k)
    ui.raise_window(rk.w)               # (a second window may be in front)
    m.key("Escape")                     # Stop
    os88marty.until(m, lambda mm: rk.b("wt_state") == WT_IDLE, "Stop",
                    poll=0.25, guest=30.0)
    return r


def report(tag, label, r):
    extra = " ".join("%s=%s" % (k[3:], r[k]) for k in
                     ("wt_held", "wt_slow", "wt_rdsz", "wt_ext") if k in r)
    say("     %-6s %-22s %5.1f s: silent %5.2f s, %3d underruns, %7d "
        "samples%s%s" % (tag, label, r["dt"], r["silence"], r["under"],
                          r["played"], ", ended" if r["ended"] else "",
                          (", " + extra) if extra else ""))


class I13(object):
    """the kernel's int 13h, counted by what it reads (czseq's classifier)"""

    def __init__(self, m, ui, vt):
        S = ui._S
        at = S("dsk_xfer.attempt")
        self.i13 = at + m.read(at, 200).index(b"\xCD\x13")  # the int itself
        self.root, self.data = vt.root_lba, vt.data_lba
        self.n = {"fat": 0, "dir": 0, "data": 0}

    def hit(self, mm, rec):
        lba = mm.regs()["di"]
        k = "fat" if lba < self.root else "dir" if lba < self.data else "data"
        self.n[k] += 1


def leg_hdd(tag, o88, syms, tmp, fx):
    say("waverackdisk: hdd %s - %s on XT-IDE + SB 2.0" % (tag, o88))
    vhd = os.path.join(tmp, "c-%s.vhd" % tag)
    args = [sys.executable, "tools/os88hdd.py", "--template", TEMPLATE,
            "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
            "--vbr", os88build.at("build/boothd.bin"),
            "--mbr", os88build.at("build/mbr.bin"),
            "--file", "SOUND.DRV=" + os88build.at("build/sound.drv"),
            "--file", "WAVERACK.O88=" + o88]
    for n in wrsim.DISK_FX:
        args += ["--file", "%s=%s" % (n, os.path.join(fx, n))]
    subprocess.run(args, check=True, capture_output=True)
    vt = os88flush.vhd_volume(vhd)
    res = {}
    m = os88marty.launch(None, machine=MACHINE_HDD,
                         extra=["--mount", "hd:0:" + os.path.abspath(vhd)])
    try:
        ui = os88ui.UI(m, verbose=False)
        ui.ready(limit=240)
        w = ui.path("C:/WAVERACK.O88")
        rk = Rack(m, ui, w, syms)
        # --- the scan of a 2 MB file: its time and its int 13h calls -------
        i13 = I13(m, ui, vt)
        with os88marty.bp_trace(m, i13.i13, on_hit=i13.hit):
            res["scan_s"] = open_doc(ui, rk, "BIG22.WAV", 900.0)
        res["scan_i13"] = dict(i13.n)
        say("     %-6s BIG22 scan: %.1f s guest, int 13h %s" % (
            tag, res["scan_s"], res["scan_i13"]))
        res["big"] = measure_play(ui, rk, "BIG22")
        report(tag, "BIG22 (2 MB, 22 kHz)", res["big"])
        # --- another window reading the disk during the play --------------
        other = {}

        def second():
            # the Disk window of C:'s root by what it shows - not
            # disk_window(): [fm_vinst] still names the chooser the first
            # window's File > Open closed
            dw = [x for x in ui.windows()
                  if x.visible and ui.fs_of(x) == (2, 0, 1)]
            if dw:
                ui.open("WAVERACK.O88", win=dw[0])
            else:
                ui.path("C:/WAVERACK.O88")
            # the NEW WaveRack by what it is - a window titled WaveRack that
            # is not the first - and not by what open() answered, which a
            # third run saw be the Disk window (its File menu, not ours)
            box = {}

            def second_up(mm):
                for x in ui.windows():
                    if x.visible and x.i != rk.w.i and \
                            x.title.startswith("WaveRack"):
                        box["w"] = x
                        return True
                return False
            os88marty.until(m, second_up, "the second WaveRack", poll=0.25,
                            guest=120.0)
            w2 = box["w"]
            rk2 = Rack(m, ui, w2, syms)
            other["rk"] = rk2
            ui.raise_window(w2)
            ui.menu_pick("File", "Open...")
            ui.chooser()
            ui.chooser_open("ST22L.WAV")
        res["comp"] = measure_play(ui, rk, "BIG22 + a scan", secs=40.0,
                                   more=second)
        rk2 = other.get("rk")
        res["comp_scanning"] = rk2 is not None and rk2.b("we_busy") == 1
        report(tag, "BIG22 + another's scan", res["comp"])
        say("     %-6s (the second WaveRack's launch and Open took %.1f s "
            "under the play)" % (tag, res["comp"]["more_s"] or 0))
        if rk2 is not None:
            ui.close(rk2.w)             # (its scan would disturb what follows)
        # --- 16-bit stereo: past an 8088's conversion ----------------------
        open_doc(ui, rk, "ST22S.WAV", 300.0)
        res["sts"] = measure_play(ui, rk, "ST22S", secs=3.0)
        report(tag, "ST22S (4 s, 88 KB/s)", res["sts"])
        open_doc(ui, rk, "ST22L.WAV", 600.0)
        res["stl"] = measure_play(ui, rk, "ST22L", secs=15.0)
        report(tag, "ST22L (20 s, 88 KB/s)", res["stl"])
    finally:
        m.close()
    return res


def leg_floppy(tag, o88, syms, tmp):
    say("waverackdisk: floppy %s - LONG.WAV off a 1.44 MB floppy" % tag)
    sysimg = os.path.join(tmp, "sys-%s.img" % tag)
    shutil.copy(os88build.at("build/os8088.img"), sysimg)
    subprocess.run([sys.executable, "tools/os88fat.py", "add", sysimg,
                    os88build.at("build/mixercfg/SYSTEM.CFG")],
                   check=True, capture_output=True)    # SOUND.DRV wanted
    long_ = os.path.join(tmp, "LONG.WAV")
    if not os.path.exists(long_):
        open(long_, "wb").write(wrsim.long_wav())
    bimg = os.path.join(tmp, "b-%s.img" % tag)
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", bimg,
                    "--size", "1440", o88, long_], check=True,
                   capture_output=True)
    res = {}
    m = os88marty.launch(sysimg, apps=bimg, machine=MACHINE_FD)
    try:
        ui = os88ui.UI(m, verbose=False)
        ui.ready(limit=240)
        w = ui.path("B:/WAVERACK.O88")
        rk = Rack(m, ui, w, syms)
        res["scan_s"] = open_doc(ui, rk, "LONG.WAV", 900.0)
        say("     %-6s LONG scan: %.1f s guest" % (tag, res["scan_s"]))
        res["long"] = measure_play(ui, rk, "LONG")
        report(tag, "LONG (1.1 MB, 11 kHz)", res["long"])
    finally:
        m.close()
    return res


def leg_bigclu(tag, o88, syms, tmp, fx):
    """a FAT16 volume of 32 KB CLUSTERS - what HDD.DRV formats past 1 GB
    (SPEC.md 18.7.5): os88hdd.py --spc 64 on a 654/16/63 VHD (321 MB, a
    geometry MartyPC's XT-IDE takes, tests/bigvol.py's). Wave 4 refused it
    (`Cluster too large`); this one opens, plays and saves on it"""
    say("waverackdisk: bigclu %s - a FAT16 volume of 32 KB clusters" % tag)
    vhd = os.path.join(tmp, "c32-%s.vhd" % tag)
    args = [sys.executable, "tools/os88hdd.py", "--out", vhd,
            "--cyls", "654", "--heads", "16", "--spt", "63", "--spc", "64",
            "--kernel", os88build.at("build/kernel.sys"),
            "--vbr", os88build.at("build/boothd.bin"),
            "--mbr", os88build.at("build/mbr.bin"),
            "--file", "SOUND.DRV=" + os88build.at("build/sound.drv"),
            "--file", "WAVERACK.O88=" + o88]
    for n in ("BIG22.WAV", "MID11.WAV"):
        args += ["--file", "%s=%s" % (n, os.path.join(fx, n))]
    subprocess.run(args, check=True, capture_output=True)
    res = {}
    m = os88marty.launch(None, machine=MACHINE_HDD,
                         extra=["--mount", "hd:0:" + os.path.abspath(vhd)])
    try:
        ui = os88ui.UI(m, verbose=False)
        ui.ready(limit=240)
        w = ui.path("C:/WAVERACK.O88")
        rk = Rack(m, ui, w, syms)
        res["scan_s"] = open_doc(ui, rk, "BIG22.WAV", 900.0)
        res["status"] = rk.text("wr_stbuf")
        res["frames"] = rk.d("wo_frames")
        if tag == "before":
            say("     before open BIG22: %r, %d frames" % (res["status"],
                                                          res["frames"]))
            return res
        src = rk.rd("wo_src", 64)
        res["clu"] = int.from_bytes(src[30:32], "little")
        res["iokb"] = rk.w_("wo_iokb")
        say("     after  open BIG22: %.1f s, cluster %d, I/O claim %d KB, "
            "heap %s" % (res["scan_s"], res["clu"], res["iokb"],
                         "(see the claim map)"))
        res["play"] = measure_play(ui, rk, "BIG22 on 32 KB clusters",
                                   secs=15.0)
        report(tag, "BIG22 (32 KB clusters)", res["play"])
        open_doc(ui, rk, "MID11.WAV", 300.0)
        ui.raise_window(rk.w)
        ui.menu_pick("File", "Save As...")
        ui.chooser()
        ui.chooser_save("OUT32.WAV")
        os88marty.until(m, lambda mm: rk.b("ws_busy") == 0 and
                        rk.text("wr_stbuf").startswith("Saved"),
                        "the save", poll=0.5, guest=300.0)
        res["osz"] = rk.w_("ws_osz")
        res["saved"] = rk.text("wr_stbuf")
    finally:
        m.close()
    vol = os88flush.vhd_volume(vhd)
    res["names"] = [n for n in vol.names()]
    try:
        res["out"] = vol.read("OUT32.WAV")
    except Exception as e:                              # noqa: BLE001
        res["out"] = None
        res["outerr"] = str(e)
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--leg", action="append",
                    choices=("hdd", "floppy", "bigclu"))
    ap.add_argument("--before", default=BEFORE)
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    for p in ("build/kernel.sys", "build/boothd.bin", "build/mbr.bin",
              "build/sound.drv", "build/waverack.o88", "build/waverack.bin",
              "build/os8088.img", "build/mixercfg/SYSTEM.CFG", TEMPLATE):
        if not os.path.exists(os88build.at(p)):
            sys.exit("waverackdisk: no %s - make && make "
                     "build/mixercfg/SYSTEM.CFG (and make marty)" % p)
    syms, img = pkg_syms("apps/waverack/waverack.asm",
                         ("apps/", "apps/waverack/",
                          os88build.at("build") + "/"))
    if img != open(os88build.at("build/waverack.bin"), "rb").read():
        sys.exit("waverackdisk: build/waverack.bin is not this source's - "
                 "make")
    tmp = tempfile.mkdtemp(dir=os88build.at("build"), prefix="wrdisk")
    try:
        rec = not reachable(a.before)
        if rec:
            say("waverackdisk: %s is not in this clone's history (WaveRack "
                "came as one squash, #311): BEFORE is Wave 4 as SPEC.md 123.6 "
                "recorded it" % a.before)
            bo88 = bsyms = None
        else:
            bo88, bsyms, _ = build_before(a.before, tmp)
        fx = os.path.join(tmp, "fx")
        os.makedirs(fx)
        for n in wrsim.DISK_FX:
            open(os.path.join(fx, n), "wb").write(wrsim.disk_fixture(n))
        after = os88build.at("build/waverack.o88")
        legs = a.leg or ["hdd", "floppy", "bigclu"]
        if "hdd" in legs:
            hb = WAVE4["hdd"] if rec else leg_hdd("before", bo88, bsyms, tmp, fx)
            ha = leg_hdd("after", after, syms, tmp, fx)
            say("waverackdisk: hdd, before -> after")
            check("BIG22 scan: guest seconds %.1f -> %.1f, no slower"
                  % (hb["scan_s"], ha["scan_s"]),
                  ha["scan_s"] <= hb["scan_s"] * 1.05)
            check("BIG22 scan: FAT reads %d -> %d" % (
                hb["scan_i13"]["fat"], ha["scan_i13"]["fat"]),
                ha["scan_i13"]["fat"] <= hb["scan_i13"]["fat"])
            big = ha["big"]
            check("BIG22 after: streamed (held %s), the external ring (%s)"
                  % (big.get("wt_held"), big.get("wt_ext")),
                  big.get("wt_held") == 0 and big.get("wt_ext") == 1)
            check("BIG22 after: no underrun in %.0f s (before: %s, %.2f s "
                  "silent)" % (big["dt"], hb["big"]["under"],
                               hb["big"]["silence"]),
                  big["under"] == 0 and big["silence"] < 0.25,
                  "%d underruns, %.2f s silent" % (big["under"],
                                                    big["silence"]))
            sts = ha["sts"]
            check("ST22S after: HELD - 'Loading' said, read from memory",
                  sts.get("held0") == 1 and
                  any(s.startswith("Loading") for s in sts["said"]),
                  repr(sorted(sts["said"])))
            check("ST22S after: no underrun (before: %s, %.2f s silent)"
                  % (hb["sts"]["under"], hb["sts"]["silence"]),
                  sts["under"] == 0 and sts["silence"] < 0.25,
                  "%d underruns, %.2f s silent" % (sts["under"],
                                                    sts["silence"]))
            stl = ha["stl"]
            why = {1: "Disk too slow: gaps", 2: "CPU too slow: gaps"}.get(
                stl.get("wt_slow"))
            check("ST22L after: %r said, the gaps counted (%d underruns, "
                  "%.2f s silent)" % (why, stl["under"], stl["silence"]),
                  why is not None and why in stl["said"],
                  repr(sorted(stl["said"])))
            say("     another window reading: before %s underruns / %.2f s "
                "silent, after %d / %.2f s (its scan still running at the "
                "end: %s)" % (hb["comp"]["under"], hb["comp"]["silence"],
                              ha["comp"]["under"], ha["comp"]["silence"],
                              ha["comp_scanning"]))
        if "floppy" in legs:
            fb = WAVE4["floppy"] if rec else leg_floppy("before", bo88, bsyms, tmp)
            fa = leg_floppy("after", after, syms, tmp)
            lg = fa["long"]
            check("LONG after: no underrun in %.0f s off the floppy (before: "
                  "%s, %.2f s silent)" % (lg["dt"], fb["long"]["under"],
                                          fb["long"]["silence"]),
                  lg["under"] == 0 and lg["silence"] < 0.25,
                  "%d underruns, %.2f s silent" % (lg["under"],
                                                    lg["silence"]))
        if "bigclu" in legs:
            cb = WAVE4["bigclu"] if rec else leg_bigclu("before", bo88, bsyms, tmp, fx)
            ca = leg_bigclu("after", after, syms, tmp, fx)
            check("32 KB clusters before: refused (%r)" % cb["status"],
                  cb["status"] == "Cluster too large" and cb["frames"] == 0)
            check("32 KB clusters after: BIG22 opens (%d frames), cluster "
                  "%d, I/O claim %d KB" % (ca["frames"], ca.get("clu", 0),
                                          ca.get("iokb", 0)),
                  ca["frames"] == 22050 * 95 and ca.get("clu") == 32768 and
                  ca.get("iokb") == 33)
            pl = ca.get("play")
            check("32 KB clusters after: 15 s played, no underrun (%s)"
                  % (pl and "%.2f s silent" % pl["silence"]),
                  pl is not None and pl["under"] == 0 and
                  pl["silence"] < 0.25)
            mid = wrsim.disk_fixture("MID11.WAV")
            r = wrsim.parse(mid)
            want = wrsim.write_wav([(0, 0, r["frames"])],
                                   {0: mid[r["dataoff"]:]}, (11025, 8, 1))
            check("32 KB clusters after: OUT32.WAV is wrsim.write_wav of "
                  "MID11, byte for byte (%d), %r, a %d-byte output claim"
                  % (len(want), ca.get("saved"), ca.get("osz", 0)),
                  ca.get("out") == want and ca.get("osz") == 32768,
                  ca.get("outerr", "%r bytes" % (len(ca.get("out") or b""),)))
            check("32 KB clusters after: no temp file left (%s)"
                  % ca.get("names"), not any("WRSAV" in n.upper()
                                             for n in ca.get("names", [])))
    finally:
        if a.keep:
            say("waverackdisk: kept %s" % tmp)
        else:
            shutil.rmtree(tmp, ignore_errors=True)
    for f in FAIL:
        say("  FAIL: " + f)
    say("waverackdisk: %s" % ("FAILED (%d)" % len(FAIL) if FAIL else "ok"))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
