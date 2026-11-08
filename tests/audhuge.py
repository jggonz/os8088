#!/usr/bin/env python3
"""audhuge - the Audio Player's whole-file HOLD on kern_mega: a WAV the
machine can hold is read whole into ONE OSAPI_MEM_HUGE claim, a bridge of the
pool and the heap allowed, and played from memory (SPEC.md 86.5.4, 50.3.5,
50.3.8; docs/plans/MEGA-HUGE-PLAN.md 3).

    make && make mega && python3 tests/audhuge.py [--kb N] [--megs M]
         [--pkg O88 --src TREE] [--find]

86Box's at286_vga_sb with 640K + 384K above 1MB (the 14.166 machine,
$OS88_MEGA_MEGS = 1.375 unless --megs), build/mega.img in A: and a B: of its
own: kern_mega's AUDIO arm (build/megak/apps/audio.o88) and MEDIA\\HOLD.WAV,
--kb KB (576 by default) of 8-bit mono at 22,050 Hz whose samples are a
pattern of their own index, so a byte the load put in the wrong window - or
past a bridge's cut - differs. Double-clicked, it opens the player and plays.

  1. the file HELD WHOLE (aph_on, aph_load 0) in a huge claim (aph_hw: a
     step of 8, its length the file's KB rounded to 32); the shape (bridged
     at HW_CUT, or one run) printed;
  2. every byte of the hold, read through its windows (window k = the claim
     + 8k, os88huge.inc's walk), is the file's;
  3. the play is PLAYING and its gulps are COPIES out of the hold
     (aph_ncopy counting, aph_ndisk 0: no gulp of it read the disk), and the
     floppy all but idle while it plays (2 READ DATA at most in 4 s: a run
     once saw the desktop's own one);
  4. Stop gives the hold back (aph_base 0), and no exception is charged to
     the player.

--find runs the machine once per size, halving between a size held and one
not, and prints the LARGEST file held whole to 32 KB - the measurement, not
the row - and what it is in seconds at 22,050 and 11,025 Hz. --megs 4 is the
4MB machine, whose hold is past a floppy: the file then rides a C: of its
own (os88bochs's 20MB geometry, hd 0).

THE FAILING CASE (docs/WRITING-TESTS.md 1): --pkg the arm of mega-release
(before the hold: no aphold.inc) with --src that tree: the file is not held
- there is no hold - and every gulp is a floppy read: 1 fails. Seen when the
row was written.
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

NAME = "HOLD.WAV"
RATE = 22050
AP_ST_STOP, AP_ST_PLAY = 0, 1
HW_SIZE = 14


def make_wav(path, kb):
    n = kb * 1024 - 44
    data = bytes(((i ^ (i >> 8) ^ ((i >> 16) * 37)) & 0xFF) for i in range(n))
    out = b"RIFF" + struct.pack("<I", 36 + n) + b"WAVEfmt " + \
        struct.pack("<IHHIIHH", 16, 1, 1, RATE, RATE, 1, 8) + \
        b"data" + struct.pack("<I", n) + data
    open(path, "wb").write(out)
    return out


def run(a, kb, work):
    import os88marty as M
    import os88ui
    import dispapps
    from cycweb import pkg_syms
    os.environ["OS88_MEGA_MEGS"] = a.megs
    src = a.src or ROOT
    syms, img = pkg_syms(os.path.join(src, "apps/audio/audio.asm"),
                         (os.path.join(src, "apps") + "/",
                          os.path.join(src, "apps/audio") + "/"),
                         ("PKG_MEGA",))
    want_bin = os.path.join(ROOT, "build", "megak", "apps", "audio.bin")
    if not a.pkg and img != open(want_bin, "rb").read():
        sys.exit("audhuge: %s is not this source's - `make mega`" % want_bin)
    pkg = a.pkg or os.path.join(ROOT, "build", "megak", "apps", "audio.o88")
    wav = os.path.join(work, NAME)
    data = make_wav(wav, kb)
    onc = len(data) > 1300 * 1024
    o88 = os.path.join(work, "AUDIO.O88")
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
             "--geometry", "615/4/17", "MEDIA:" + wav],
            stdout=subprocess.DEVNULL)
        extra = ["--mount", "hd:0:" + vhd]
    res = dict(kb=kb, held=False, checks=[])

    def want(ok, what):
        res["checks"].append((ok, what))
        print("   %s  %s" % ("ok  " if ok else "FAIL", what), flush=True)
        return ok
    with os88ui.boot("build/mega.img", apps=disk, machine="at286_vga_sb",
                     label="audhuge", extra=extra) as ui:
        m = ui.m
        f0 = len(m.faults())
        ui.path(("C:" if onc else "B:") + "/MEDIA/" + NAME, limit=120)

        def seg():
            got = dispapps.pkg_seg(m, 0)
            return got and int(got[1])

        def rd(name, n, off=0):
            s = seg()
            if not s or name not in syms:
                return None
            return int.from_bytes(m.readseg(s, syms[name] + off, n), "little")
        on = load = None
        for _ in range(40):                 # the launch's open: the load
            M.guest_sleep(m, 0.5)           # begun (or nothing to hold)
            on, load = rd("aph_on", 1), rd("aph_load", 1)
            if on or load or rd("ap_state", 1) == AP_ST_PLAY:
                break
        for _ in range(240):                # the load, then the play
            on, load = rd("aph_on", 1), rd("aph_load", 1)
            if os.environ.get("AUDHUGE_DEBUG"):
                print("   .. on %s load %s have %s pct %s base %s state %s "
                      "ncopy %s ndisk %s" % (
                          on, load, rd("aph_have", 4), rd("aph_pct", 1),
                          rd("aph_base", 2), rd("ap_state", 1),
                          rd("aph_ncopy", 2), rd("aph_ndisk", 2)), flush=True)
            if not load:
                break
            M.guest_sleep(m, 1)
        hw = None
        if "aph_hw" in syms and seg():
            hw = struct.unpack("<HHIH", m.readseg(seg(), syms["aph_hw"], 10))
        res["held"] = on == 1
        print("   audhuge: %dK file, %sMB: on %s load %s%s" % (
            kb, a.megs, on, load, " step %X len %dK cut %d" % (
                hw[1], hw[2] >> 10, hw[3]) if hw and on else ""), flush=True)
        if a.find:
            return res
        if not want(on == 1 and load == 0, "1: the %d-byte file HELD WHOLE "
                    "(aph_on %s, aph_load %s)" % (len(data), on, load)):
            return res
        want(hw[1] == 8 and hw[2] >> 10 >= (kb + 31) // 32 * 32,
             "1: a huge claim of its KB or more (claimed past the heap's "
             "largest run, forced to bridge), a step of 8 (%s)" % (hw,))
        print("   --    the hold: %s" % (
            "a BRIDGE, the cut at window %d (%d KB of pool)" % (hw[3],
                                                               hw[3] * 64)
            if hw[3] else "one run"))
        base = rd("aph_base", 2)
        got = bytearray()
        k = 0
        while len(got) < len(data):
            n = min(0x10000, len(data) - len(got))
            got += m.readseg(base + 8 * k, 0, n)
            k += 1
        bad = next((i for i in range(len(data)) if got[i] != data[i]), None)
        want(bad is None, "2: every byte of the hold, read window by window, "
             "is the file's%s" % ("" if bad is None else
                                  " - byte %X differs (window %d)"
                                  % (bad, bad >> 16)))
        r0 = m.disk()["reads"]
        n0 = rd("aph_ncopy", 2)
        M.guest_sleep(m, 4)
        reads = m.disk()["reads"] - r0
        want(rd("ap_state", 1) == AP_ST_PLAY and rd("aph_ncopy", 2) > n0 and
             rd("aph_ndisk", 2) == 0 and reads <= 2,
             "3: playing (state %s) from memory: gulps copied %d -> %d, read "
             "off the disk %s, READ DATA while it played %d (the desktop's "
             "own, at most 2: none of them a gulp)" % (
                 rd("ap_state", 1), n0, rd("aph_ncopy", 2),
                 rd("aph_ndisk", 2), reads))
        m.key("KeyS")
        for _ in range(20):
            M.guest_sleep(m, 0.5)
            if rd("ap_state", 1) == AP_ST_STOP:
                break
        new = [l for l in m.faults()[f0:] if "AUDIO" in l.upper()]
        want(rd("ap_state", 1) == AP_ST_STOP and rd("aph_base", 2) == 0 and
             not new, "4: Stop gives the hold back (aph_base %s), no "
             "exception charged to the player (%s)"
             % (rd("aph_base", 2), "; ".join(new[:3])))
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--kb", type=int, default=576)
    ap.add_argument("--megs", default=os.environ.get("OS88_MEGA_MEGS",
                                                     "1.375"))
    ap.add_argument("--pkg")
    ap.add_argument("--src")
    ap.add_argument("--find", action="store_true",
                    help="the largest file held whole, to 32 KB")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    os.chdir(ROOT)
    for f in ("build/mega.img", "build/megak/apps/audio.o88",
              "build/mbr.bin"):
        if not os.path.exists(f):
            sys.exit("audhuge: %s is not built - `make && make mega`" % f)
    work = tempfile.mkdtemp(prefix="audhuge-")
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
            print("audhuge: %sMB: the largest file held whole is %d KB (%d KB "
                  "is not) - %.0f s at 22,050 Hz, %.0f s at 11,025 Hz 8-bit "
                  "mono" % (a.megs, lo, hi, lo * 1024 / 22050.0,
                            lo * 1024 / 11025.0))
            return 0
        r = run(a, a.kb, work)
        fails = [w for ok, w in r["checks"] if not ok] or \
            ([] if r["checks"] else ["no run"])
        print("audhuge: %s" % ("ok" if not fails else
                               "FAILED: " + "; ".join(fails)))
        return 1 if fails else 0
    finally:
        if a.keep:
            print("audhuge: kept %s" % work)
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
