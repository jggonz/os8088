#!/usr/bin/env python3
"""x3huge - XTMP3's file hold as ONE OSAPI_MEM_HUGE claim on kern_mega, a
bridge of the pool and the heap allowed (SPEC.md 121.6.1, 50.3.5;
docs/plans/MEGA-HUGE-PLAN.md 3).

    make && make mega && python3 tests/x3huge.py [--kb N] [--megs M]
         [--pkg O88 --map MAP] [--find]

86Box's at286_vga_sb (an SB16) with 640K + 384K above 1MB (the 14.166
machine, $OS88_MEGA_MEGS = 1.375 unless --megs), build/mega.img in A: and a
B: of its own: kern_mega's XTMP3 arm (build/megak/apps/xtmp3.o88) and
MEDIA\\BIGHOLD.MP3, an MPEG-1 stream of --kb KB (480 by default) built here
from NEONDRV.MP3's frames repeated (its ID3 tag once, in front; whole frames,
so the stream is valid end to end - a reservoir glitch at each seam is all a
decoder can find). 480 KB is past both holds kern_mega had: the XMS block is
the pool's best run, a fraction of 384K once the package's own claims are in
it, and the conventional hold stops at X3H_CAPKB = 192 KB.

  1. the file HELD WHOLE (x3h_on) in a huge claim (x3h_kind = X3HK_HUGE), a
     step of 8; the shape (bridged at HW_CUT, or one run) printed;
  2. every byte of the hold, read through its windows (window k = the claim
     + 8k, as os88huge.inc walks it), is the file's - the load's READ_SEQ
     chunks landed where hw_fread aimed them, across the cut when it bridged;
  3. Auto's probe reads it OUT of the hold: x3h_ncopy counting (x3_nrseq,
     the disk's gulps, is every other file's - printed, not asserted);
  4. no exception charged to XTMP3.

--find runs the machine once per size, halving between a size held (any
kind of hold, so the arm of before is measured the same way) and one not,
and prints the LARGEST held whole to 32 KB - the measurement, not the row. --megs 4 is the 4MB machine,
whose pool takes more than a floppy holds: the file then rides a C: of its
own (a 20MB disk, os88bochs's geometry, mounted as hd 0).

THE FAILING CASE (docs/WRITING-TESTS.md 1): --pkg and --map naming the arm
of mega-release before the adoption (no x3h_huge): the 480 KB song is NOT
held (x3h_kind 0: XMEM_CAPS short, past X3H_CAPKB) and streams - 1 fails.
Seen when the row was written.
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

MD_IDLE, MD_LOAD = 0, 8
X3HK_HUGE = 3
HW_SIZE = 14
NAME = "BIGHOLD.MP3"
MP3_L3_KBPS = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256,
               320]
MP3_SR = [44100, 48000, 32000]


def make_mp3(path, size):
    """NEONDRV.MP3's frames repeated to `size` bytes or the whole frame
    under it: its ID3v2 tag once, then MPEG-1 layer III frames only"""
    src = open(os.path.join(HERE, "xtmp3", "NEONDRV.MP3"), "rb").read()
    tag = b""
    if src[:3] == b"ID3":
        n = 10 + (src[6] << 21 | src[7] << 14 | src[8] << 7 | src[9])
        tag, src = src[:n], src[n:]
    frames, i = [], 0
    while i + 4 <= len(src):
        h = src[i:i + 4]
        if h[0] != 0xFF or h[1] & 0xE0 != 0xE0:
            break
        assert (h[1] >> 3) & 3 == 3 and (h[1] >> 1) & 3 == 1, "MPEG-1 L3"
        n = 144000 * MP3_L3_KBPS[h[2] >> 4] // MP3_SR[(h[2] >> 2) & 3] + \
            ((h[2] >> 1) & 1)
        if i + n > len(src):
            break
        frames.append(src[i:i + n])
        i += n
    out = bytearray(tag)
    k = 0
    while len(out) + len(frames[k % len(frames)]) <= size:
        out += frames[k % len(frames)]
        k += 1
    open(path, "wb").write(out)
    return bytes(out)


def run(a, kb, work):
    import os88marty as om
    import os88sym
    import os88ui
    megs = a.megs
    os.environ["OS88_MEGA_MEGS"] = megs
    sym = os88sym._parse_map(a.map)[0]
    mp3 = os.path.join(work, NAME)
    data = make_mp3(mp3, kb * 1024)
    disk = os.path.join(work, "b.img")
    extra = []
    fl = float(megs)
    onc = len(data) > 1300 * 1024
    args = [a.pkg] + ([] if onc else ["MEDIA:" + mp3]) + \
        ["--folder", "SYSTEM/APPDATA"]
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools",
                                                        "os88disk.py"),
                           "-o", disk, "--size", "1440"] + args,
                          stdout=subprocess.DEVNULL)
    if onc:
        vhd = os.path.join(work, "c.img")
        subprocess.check_call(
            [sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
             "-o", vhd, "--hdd", "--mbr", os.path.join(ROOT, "build",
                                                        "mbr.bin"),
             "--boot", os.path.join(ROOT, "build", "megak", "boothd.bin"),
             "--kernel", os.path.join(ROOT, "build", "megak", "kernel.sys"),
             "--geometry", "615/4/17", "MEDIA:" + mp3],
            stdout=subprocess.DEVNULL)
        extra = ["--mount", "hd:0:" + vhd]
    res = {}
    with os88ui.boot("build/mega.img", apps=disk, machine="at286_vga_sb",
                     label="x3huge", extra=extra) as ui:
        m = ui.m
        f0 = len(m.faults())
        w = ui.path(("C:" if onc else "B:") + "/MEDIA/" + NAME)
        hw = sym.get("x3h_hw")

        def st():
            spans = [(sym["x3_md"], 1), (sym["x3h_kind"], 1),
                     (sym["x3h_on"], 1), (sym["x3h_base"], 2),
                     (sym["x3h_ncopy"], 2), (sym["x3_nrseq"], 2),
                     (sym["x3h_have"], 4)]
            if hw:
                spans.append((hw, HW_SIZE))
            r = om.pkg_reads(m, w, spans)
            out = dict(md=r[0][0], kind=r[1][0], on=r[2][0],
                       base=struct.unpack("<H", r[3])[0],
                       ncopy=struct.unpack("<H", r[4])[0],
                       nrseq=struct.unpack("<H", r[5])[0],
                       have=struct.unpack("<I", r[6])[0])
            if hw:
                seg, step, ln, cut = struct.unpack_from("<HHIH", r[7])
                out.update(seg=seg, step=step, len=ln, cut=cut)
            return out
        s = st()
        for _ in range(120):            # the open, then the load: MD_LOAD
            om.guest_sleep(m, 1)        # until it is in (MD_IDLE before)
            s = st()
            if s["md"] not in (MD_IDLE, MD_LOAD):
                break
        print("   x3huge: %dK song, %sMB: kind %d on %d have %d%s"
              % (kb, megs, s["kind"], s["on"], s["have"],
                 " step %X len %dK cut %d" % (s["step"], s["len"] >> 10,
                                              s["cut"]) if hw else ""),
              flush=True)
        res = dict(s, held=s["on"] == 1, size=len(data))
        if a.find:
            return res
        res["checks"] = []

        def want(ok, what):
            res["checks"].append((ok, what))
            print("   %s  %s" % ("ok  " if ok else "FAIL", what), flush=True)
            return ok
        if not want(res["held"] and s["kind"] == X3HK_HUGE,
                    "1: the %d-byte song HELD WHOLE in a huge "
                    "claim (x3h_kind %d, x3h_on %d)"
                    % (len(data), s["kind"], s["on"])):
            return res
        want(s["step"] == 8, "1: a run of selectors, a step of 8 (%X)"
             % s["step"])
        print("   --    the hold: %d KB, %s" % (s["len"] >> 10, (
            "a BRIDGE, the cut at window %d (%d KB of pool)" % (
                s["cut"], s["cut"] * 64)) if s["cut"] else "one run"))
        got = bytearray()
        k = 0
        while len(got) < len(data):
            n = min(0x10000, len(data) - len(got))
            got += m.readseg(s["base"] + s["step"] * k, 0, n)
            k += 1
        bad = next((i for i in range(len(data)) if got[i] != data[i]), None)
        want(bad is None, "2: every byte of the hold, read window by window, "
             "is the file's%s" % ("" if bad is None else
                                  " - byte %X differs (window %d)"
                                  % (bad, bad >> 16)))
        n0, r0 = s["ncopy"], s["nrseq"]
        for _ in range(20):             # Auto's probe and the play
            om.guest_sleep(m, 1)
            s = st()
            if s["ncopy"] > n0 + 4:
                break
        want(s["ncopy"] > n0, "3: gulps copied out of the hold %d -> %d "
             "(md %d; the disk's gulps, every OTHER file's - a cache's "
             "header, a WAV - %d -> %d)" % (n0, s["ncopy"], s["md"], r0,
                                            s["nrseq"]))
        new = [l for l in m.faults()[f0:] if "XTMP3" in l]
        want(not new, "4: no exception charged to XTMP3 (%s)"
             % "; ".join(new[:3]))
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--kb", type=int, default=480)
    ap.add_argument("--megs", default=os.environ.get("OS88_MEGA_MEGS",
                                                     "1.375"))
    ap.add_argument("--pkg", default="build/megak/apps/xtmp3.o88")
    ap.add_argument("--map", default="build/megak/apps/xtmp3.map")
    ap.add_argument("--find", action="store_true",
                    help="the largest song held whole, to 32 KB")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    os.chdir(ROOT)
    for f in ("build/mega.img", a.pkg, a.map, "build/mbr.bin"):
        if not os.path.exists(f):
            sys.exit("x3huge: %s is not built - `make && make mega`" % f)
    work = tempfile.mkdtemp(prefix="x3huge-")
    try:
        if a.find:
            lo, hi = 32, None           # held / not held, KB
            kb = a.kb
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
            print("x3huge: %sMB: the largest song held whole is %d KB "
                  "(%d KB is not)" % (a.megs, lo, hi))
            return 0
        r = run(a, a.kb, work)
        fails = [w for ok, w in r.get("checks", [(False, "no run")]) if not ok]
        print("x3huge: %s" % ("ok" if not fails else
                              "FAILED: " + "; ".join(fails)))
        return 1 if fails else 0
    finally:
        if a.keep:
            print("x3huge: kept %s" % work)
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
