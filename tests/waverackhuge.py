#!/usr/bin/env python3
"""waverackhuge - WaveRack's play HOLD as ONE OSAPI_MEM_HUGE claim on
kern_mega, a bridge of the pool and the heap allowed (SPEC.md 123.6, 50.3.5,
50.3.8; docs/plans/MEGA-HUGE-PLAN.md 3).

    make && make mega && python3 tests/waverackhuge.py [--kb N] [--megs M]
         [--pkg O88 --src TREE [--plain]] [--find] [--force]

86Box's at286_vga_sb with 640K + 384K above 1MB (the 14.166 machine,
$OS88_MEGA_MEGS = 1.375 unless --megs), build/mega.img in A: and a B: of its
own: kern_mega's WaveRack arm (build/megak/apps/waverack.o88) and
MEDIA\\HUGE.WAV, --kb KB (640 by default) of 8-bit mono at 44,100 Hz whose
samples are a pattern of their own index - so a byte the load put in the
wrong window, or past a bridge's cut, is a byte that differs. A floppy
cannot sustain 44,100 bytes a second with SPEC.md 123.6's quarter to spare,
so Play HOLDS the region: one byte a frame, 640 KB - past the heap's
largest run, which is all the shipped build's hold can ever be on kern_mega
(the conventional claim beside the ring, the look-ahead and 16 KB).

  1. Play takes the hold (wt_held) and it is a huge claim walked by its step:
     wt_hw's step 8, its length the region's KB; the shape (bridged at
     HW_CUT, or one run) printed;
  2. the load runs to the region's end (wt_hpos = wt_hhi, wt_hload 0) and
     the play is PLAYING from it, wt_slow 0 ('Disk too slow' not said);
  3. every byte of the hold, read window by window (window k = wt_hseg +
     8k, os88huge.inc's walk), is tools/wrsim.py's hold_bytes() for the
     region - across the cut when it bridged;
  4. Stop gives it back (wt_hseg 0), and no exception is charged to WaveRack.

--find runs the machine once per size, halving between a size held and one
not, and prints the LARGEST region held to 32 KB - the measurement, not the
row: at 44,100 Hz, and at 11,025 Hz as seconds (one byte a frame either way).
--megs 4 is the 4MB machine, whose hold is past a floppy: the file then rides
a C: of its own (os88bochs's 20MB geometry, hd 0), and --force makes the
medium look slow (the source record's timed reads, WS_RDB/WS_RDT, poked to 1
byte in 1,000 ticks before Play) because a fixed disk streams 44 KB a second
- the claim, not the medium, is what is measured.

THE FAILING CASE (docs/WRITING-TESTS.md 1): --pkg the SHIPPED WAVERACK.O88
(build/waverack.o88, the only build mega-release had) with --plain, its
symbols read by assembling --src without PKG_MEGA: the 640 KB region is NOT
held - the heap's largest run is short of it - and plays streamed with gaps
('Disk too slow: gaps', wt_slow 1): 1 fails. Seen when the row was written.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
os.environ["OS88_MEGA"] = "1"
os.environ["OS88_MEGA_EMU"] = "86box"
sys.path[:0] = [os.path.join(ROOT, "tools"), HERE]

NAME = "HUGE.WAV"
RATE = 44100
WT_IDLE, WT_PLAY = 0, 1
WS_USED, WS_RDB, WS_RDT = 27, 56, 60    # wrdoc.inc's source record
WO_SRC_SZ = 64
HW_SIZE = 14


def make_wav(path, kb):
    n = kb * 1024
    data = bytes(((i ^ (i >> 8) ^ ((i >> 16) * 37)) & 0xFF) for i in range(n))
    hdr = b"RIFF" + struct.pack("<I", 36 + n) + b"WAVEfmt " + \
        struct.pack("<IHHIIHH", 16, 1, 1, RATE, RATE, 1, 8) + \
        b"data" + struct.pack("<I", n)
    open(path, "wb").write(hdr + data)
    return hdr + data


def run(a, kb, work):
    import os88marty as M
    import os88ui
    import dispapps
    import wrsim
    from cycweb import pkg_syms
    os.environ["OS88_MEGA_MEGS"] = a.megs
    src = a.src or ROOT
    syms, img = pkg_syms(os.path.join(src, "apps/waverack/waverack.asm"),
                         (os.path.join(src, "apps") + "/",
                          os.path.join(src, "apps/waverack") + "/",
                          os.path.join(src, "build") + "/"),
                         () if a.plain else ("PKG_MEGA",))
    want_bin = os.path.join(ROOT, "build", "megak", "apps", "waverack.bin")
    if not a.pkg and img != open(want_bin, "rb").read():
        sys.exit("waverackhuge: %s is not this source's - `make mega`"
                 % want_bin)
    pkg = a.pkg or os.path.join(ROOT, "build", "megak", "apps",
                                "waverack.o88")
    wav = os.path.join(work, NAME)
    data = make_wav(wav, kb)
    r = wrsim.parse(data)
    onc = len(data) > 1300 * 1024
    o88 = os.path.join(work, "WAVERACK.O88")
    shutil.copyfile(pkg, o88)
    disk = os.path.join(work, "b.img")
    subprocess.check_call(
        [sys.executable, os.path.join(ROOT, "tools", "os88disk.py"), "-o",
         disk, "--size", "1440", o88] + ([] if onc else ["MEDIA:" + wav]) +
        ["--folder", "SYSTEM/APPDATA"], stdout=subprocess.DEVNULL)
    extra = []
    if onc:
        vhd = os.path.join(work, "c.img")
        subprocess.check_call(
            [sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
             "-o", vhd, "--hdd", "--mbr", os.path.join(ROOT, "build",
                                                        "mbr.bin"),
             "--boot", os.path.join(ROOT, "build", "megak", "boothd.bin"),
             "--kernel", os.path.join(ROOT, "build", "megak", "kernel.sys"),
             "--geometry", "615/4/17", o88, "MEDIA:" + wav],
            stdout=subprocess.DEVNULL)
        extra = ["--mount", "hd:0:" + vhd]
    res = dict(kb=kb, held=False, checks=[])

    def want(ok, what):
        res["checks"].append((ok, what))
        print("   %s  %s" % ("ok  " if ok else "FAIL", what), flush=True)
        return ok
    with os88ui.boot("build/mega.img", apps=disk, machine="at286_vga_sb",
                     label="wrhuge", extra=extra) as ui:
        m = ui.m
        f0 = len(m.faults())
        w = ui.path(("C:" if onc else "B:") + "/WAVERACK.O88", limit=120)

        def seg():
            got = dispapps.pkg_seg(m, 0)
            return got and int(got[1])

        def rd(name, n, off=0):
            s = seg()
            if not s:
                return None
            return int.from_bytes(m.readseg(s, syms[name] + off, n), "little")
        M.guest_sleep(m, 2)
        ui.raise_window(w)
        ui.menu_pick("File", "Open...")
        names = [row[0] for row in ui.listing(ui.chooser())]
        ui.chooser_open(NAME if NAME in names else "MEDIA/" + NAME)
        for _ in range(240):                # the scan: the whole file read
            M.guest_sleep(m, 1)
            if rd("we_busy", 1) == 0 and rd("wo_rate", 2):
                break
        if a.force:                         # the medium made to look slow
            for i in range(4):
                if rd("wo_src", 1, i * WO_SRC_SZ + WS_USED):
                    m.write(m.lin(seg(), syms["wo_src"] + i * WO_SRC_SZ +
                                  WS_RDB), struct.pack("<IH", 1, 1000))
        m.key("Space")
        held = 0
        for _ in range(8):                  # the plan is at Play
            M.guest_sleep(m, 0.5)
            held = held or rd("wt_held", 1) or 0
            if held or rd("wt_slow", 1):
                break
        hw = None
        if "wt_hw" in syms:
            hw = struct.unpack("<HHIH", m.readseg(seg(), syms["wt_hw"], 10))
        slow = rd("wt_slow", 1)
        res["held"] = bool(held)
        print("   waverackhuge: %d KB region, %sMB: held %d slow %d%s"
              % (kb, a.megs, held, slow or 0,
                 (" step %X len %dK cut %d" % (hw[1], hw[2] >> 10, hw[3]))
                 if hw and held else ""), flush=True)
        if a.find:
            return res
        if not want(held == 1, "1: Play HOLDS the %d KB region (wt_held %d, "
                    "wt_slow %d)" % (kb, held, slow or 0)):
            return res
        want(hw is not None and hw[1] == 8 and hw[2] >> 10 >= kb,
             "1: a huge claim of its KB, a step of 8 (%s)" % (hw,))
        print("   --    the hold: %s" % (
            "a BRIDGE, the cut at window %d (%d KB of pool)" % (hw[3],
                                                               hw[3] * 64)
            if hw and hw[3] else "one run"))
        for _ in range(240):                # the load
            M.guest_sleep(m, 1)
            if rd("wt_hload", 1) == 0:
                break
        lo, hi, pos = rd("wt_hlo", 4), rd("wt_hhi", 4), rd("wt_hpos", 4)
        st = rd("wt_state", 1)
        want(pos == hi and hi - lo == kb * 1024 and rd("wt_hload", 1) == 0
             and st == WT_PLAY and rd("wt_slow", 1) == 0,
             "2: loaded whole, frames %d..%d (wt_hpos %d), playing (state %s)"
             " from it" % (lo, hi, pos, st))
        hseg = rd("wt_hseg", 2)
        got = bytearray()
        k = 0
        n = hi - lo
        while len(got) < n:
            c = min(0x10000, n - len(got))
            got += m.readseg(hseg + 8 * k, 0, c)
            k += 1
        ref = wrsim.hold_bytes(data, r, lo, hi)
        bad = next((i for i in range(min(len(got), len(ref)))
                    if got[i] != ref[i]), None)
        want(bytes(got) == ref, "3: every byte of the hold, window by "
             "window, is wrsim.hold_bytes()%s" % (
                 "" if bad is None else " - frame %d differs (window %d)"
                 % (bad, bad >> 16)))
        M.guest_sleep(m, 2)
        m.key("KeyS")
        for _ in range(20):
            M.guest_sleep(m, 0.5)
            if rd("wt_state", 1) == WT_IDLE:
                break
        new = [l for l in m.faults()[f0:] if "WaveRack" in l]
        want(rd("wt_state", 1) == WT_IDLE and rd("wt_hseg", 2) == 0 and
             not new, "4: Stop gives the hold back (wt_hseg %s), no "
             "exception charged to WaveRack (%s)"
             % (rd("wt_hseg", 2), "; ".join(new[:3])))
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--kb", type=int, default=640)
    ap.add_argument("--megs", default=os.environ.get("OS88_MEGA_MEGS",
                                                     "1.375"))
    ap.add_argument("--pkg")
    ap.add_argument("--src")
    ap.add_argument("--plain", action="store_true",
                    help="--pkg is a shipped build (symbols without PKG_MEGA)")
    ap.add_argument("--find", action="store_true",
                    help="the largest region held, to 32 KB")
    ap.add_argument("--force", action="store_true",
                    help="poke the medium slow (a fixed disk's file)")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    os.chdir(ROOT)
    for f in ("build/mega.img", "build/megak/apps/waverack.o88",
              "build/mbr.bin"):
        if not os.path.exists(f):
            sys.exit("waverackhuge: %s is not built - `make && make mega`" % f)
    work = tempfile.mkdtemp(prefix="wrhuge-")
    try:
        if a.find:
            lo, hi, kb = 32, None, a.kb
            while True:
                r = run(a, kb, work)
                if r["held"]:
                    lo = kb
                    kb = kb * 2 if hi is None else (lo + hi) // 2
                else:
                    hi = kb
                    kb = (lo + hi) // 2
                kb = kb // 32 * 32
                if hi is not None and hi - lo <= 32:
                    break
            print("waverackhuge: %sMB: the largest region held is %d KB "
                  "(%d KB is not) - %.0f s at 44,100 Hz, %.0f s at 11,025 Hz"
                  % (a.megs, lo, hi, lo * 1024 / 44100.0,
                     lo * 1024 / 11025.0))
            return 0
        r = run(a, a.kb, work)
        fails = [w for ok, w in r["checks"] if not ok] or \
            ([] if r["checks"] else ["no run"])
        print("waverackhuge: %s" % ("ok" if not fails else
                                    "FAILED: " + "; ".join(fails)))
        return 1 if fails else 0
    finally:
        if a.keep:
            print("waverackhuge: kept %s" % work)
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
