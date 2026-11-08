#!/usr/bin/env python3
"""audhuge - the Audio Player's whole-file HOLD on kern_mega: a WAV's first
play STREAMS at once, as every build's does, and every gulp the stream reads
is also copied into ONE OSAPI_MEM_HUGE claim, a bridge of the pool and the
heap allowed; the file held whole, its next play is copies and reads no disk
(SPEC.md 86.5.4, 50.3.5, 50.3.8; KERN-MEGA-PLAN 14.173, 14.186).

    make && make mega && python3 tests/audhuge.py [--kb N] [--megs M]
         [--pkg O88 --src TREE] [--find]

86Box's at286_vga_sb with 640K + 384K above 1MB (the 14.166 machine,
$OS88_MEGA_MEGS = 1.375 unless --megs), build/mega.img in A: and a B: of its
own: kern_mega's AUDIO arm (build/megak/apps/audio.o88) and MEDIA\\HOLD.WAV,
--kb KB (576 by default, 26.7 s) of 8-bit mono at 22,050 Hz whose samples are
a pattern of their own index, so a byte the fill put in the wrong window - or
past a bridge's cut, or out of step - differs. Double-clicked, it opens the
player and plays; [ap_repeat] is set at once, so the one-track list plays it
again.

  1. NO LOAD WAIT: PLAYING within 4 s of the window, streaming (aph_ndisk
     counting, aph_on 0), the hold claimed (aph_base; aph_hw a step of 8,
     its length the file's KB or more; the shape printed);
  2. the first play's reads fill it: HELD WHOLE (aph_on 1) before the play
     ends, and every byte of the hold from the fill's first (aph_lo) to the
     data's end, read through its windows (window k = the claim + 8k,
     os88huge.inc's walk), is the file's;
  3. the second play (Repeat) is PLAYING from memory: gulps copied
     (aph_ncopy counting), aph_ndisk where the fill left it, and the floppy
     all but idle (2 READ DATA at most in 4 s: the desktop's own);
  4. Stop gives the hold back (aph_base 0), and no exception is charged to
     the player.

--gap (the audholdgap row) moves the fill's high-water mark (aph_have) on
32 KB once the play streams, so the next gulp is OUT OF STEP: the hold is
given up (aph_base 0, aph_on 0) and the play streams on, the disk read for
it, with no exception - never a gap played back. RED: aph_fill's order check
amputated (AUDHUGE_RED=noseq, its `jne .drop`s nopped): the hold kept.

--find runs the machine once per size, halving between a size claimed and
one not, and prints the LARGEST file a hold is claimed for, to 32 KB - the
measurement, not the row - and what it is in seconds at 22,050 and 11,025
Hz. --megs 4 is the 4MB machine, whose hold is past a floppy: the file then
rides a C: of its own (os88bochs's 20MB geometry, hd 0).

THE FAILING CASES (docs/WRITING-TESTS.md 1): --pkg the arm of mega-release
(14.173's: the whole file read before the play, 'Loading N%') with --src that
tree: not PLAYING for the ~25 s of the load - 1 fails. AUDHUGE_RED=nofill
(this tree, aph_fill's copy amputated, a `ret` poked at its head): the fill
never moves - 2 fails. Both seen when the row was rewritten (14.186).
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
        if os.environ.get("AUDHUGE_RED") == "nofill" and "aph_fill" in syms:
            for _ in range(40):             # the RED leg: the copy amputated
                M.guest_sleep(m, 0.25)
                if seg():
                    m.write(m.lin(seg(), syms["aph_fill"]), b"\xF8\xC3")
                    break                   # (clc, ret)
        if os.environ.get("AUDHUGE_RED") == "noseq" and "aph_fill" in syms:
            for _ in range(40):             # the RED leg: no order check
                M.guest_sleep(m, 0.25)
                if seg():
                    at = m.lin(seg(), syms["aph_fill"])
                    code = m.read(at, 48)
                    for i in range(4, len(code) - 1):  # cmp ax|dx, [aph_have]
                        if code[i] == 0x75 and code[i - 4] == 0x3B and \
                                code[i - 3] in (0x06, 0x16):    # then jne .drop
                            m.write(at + i, b"\x90\x90")
                    break
        play = None
        for i in range(16):                 # 1: playing at once, no load
            M.guest_sleep(m, 0.25)
            if rd("ap_state", 1) == AP_ST_PLAY:
                play = i * 0.25
                break
        if seg() and "ap_repeat" in syms:   # the one-track list again
            m.write(m.lin(seg(), syms["ap_repeat"]), b"\x01")
        base, on, nd0 = rd("aph_base", 2), rd("aph_on", 1), rd("aph_ndisk", 2)
        hw = None
        if "aph_hw" in syms and seg():
            hw = struct.unpack("<HHIH", m.readseg(seg(), syms["aph_hw"], 10))
        res["held"] = bool(base)
        print("   audhuge: %dK file, %sMB: base %s%s" % (
            kb, a.megs, base and "%04X" % base, " step %X len %dK cut %d" % (
                hw[1], hw[2] >> 10, hw[3]) if hw and base else ""), flush=True)
        if a.find:
            return res
        M.guest_sleep(m, 2)
        if not want(play is not None and rd("ap_state", 1) == AP_ST_PLAY and on == 0
             and rd("aph_ndisk", 2) > nd0,
             "1: PLAYING %s s after the window, no load wait, STREAMING "
             "(aph_on %s, gulps off the disk %s -> %s)" % (
                 play, on, nd0, rd("aph_ndisk", 2))):
            return res
        if not want(bool(base) and hw[1] == 8 and hw[2] >> 10 >= kb,
                    "1: the hold claimed at the open, a huge claim of its KB "
                    "or more, a step of 8 (base %s, %s)" % (base, hw,)):
            return res
        print("   --    the hold: %s" % (
            "a BRIDGE, the cut at window %d (%d KB of pool)" % (hw[3],
                                                               hw[3] * 64)
            if hw[3] else "one run"))
        if a.gap:                           # --gap: a gulp out of step
            have = rd("aph_have", 4)
            m.write(m.lin(seg(), syms["aph_have"]),
                    struct.pack("<I", have + 0x8000))
            nd = rd("aph_ndisk", 2)
            for _ in range(12):
                M.guest_sleep(m, 0.5)
                if not rd("aph_base", 2):
                    break
            M.guest_sleep(m, 2)
            new = [l for l in m.faults()[f0:] if "AUDIO" in l.upper()]
            want(not rd("aph_base", 2) and not rd("aph_on", 1) and
                 rd("ap_state", 1) == AP_ST_PLAY and rd("aph_ndisk", 2) > nd
                 and not new,
                 "GAP: the fill's high-water mark moved on 32 KB, so the next "
                 "gulp is out of step: the hold given up (aph_base %s, aph_on "
                 "%s), the play STREAMING on (state %s, gulps off the disk "
                 "%s -> %s), no exception (%s)" % (
                     rd("aph_base", 2), rd("aph_on", 1), rd("ap_state", 1),
                     nd, rd("aph_ndisk", 2), "; ".join(new[:3])))
            return res
        secs = len(data) / float(RATE)
        for _ in range(int(secs) + 10):     # 2: the first play fills it
            on = rd("aph_on", 1)
            if os.environ.get("AUDHUGE_DEBUG"):
                print("   .. on %s base %s lo %s have %s state %s ncopy %s "
                      "ndisk %s" % (on, rd("aph_base", 2), rd("aph_lo", 4),
                                    rd("aph_have", 4), rd("ap_state", 1),
                                    rd("aph_ncopy", 2), rd("aph_ndisk", 2)),
                      flush=True)
            if on or not rd("aph_base", 2):
                break
            M.guest_sleep(m, 1)
        lo, have, nd1 = rd("aph_lo", 4), rd("aph_have", 4), rd("aph_ndisk", 2)
        if not want(on == 1 and rd("aph_base", 2) == base,
                    "2: the first play's own reads filled it: HELD WHOLE "
                    "(aph_on %s, the fill %s..%s of %d bytes, base %s)" % (
                        on, lo, have, len(data), rd("aph_base", 2))):
            return res
        end = min(have, len(data))
        got = bytearray()
        k = lo >> 16
        got_lo = k << 16
        while got_lo + len(got) < end:
            n = min(0x10000, end - got_lo - len(got))
            got += m.readseg(base + 8 * k, 0, n)
            k += 1
        got = got[lo - got_lo:]
        bad = next((i for i in range(len(got)) if got[i] != data[lo + i]),
                   None)
        want(bad is None and lo + len(got) >= len(data) - 1024,
             "2: every byte of the hold from %X to %X, read window by window, "
             "is the file's%s" % (lo, end, "" if bad is None else
                                  " - byte %X differs (window %d)"
                                  % (lo + bad, (lo + bad) >> 16)))
        n0 = None
        for _ in range(40):                 # 3: the second play, copies
            M.guest_sleep(m, 0.5)
            if rd("aph_ncopy", 2) and rd("ap_state", 1) == AP_ST_PLAY:
                n0 = rd("aph_ncopy", 2)
                break
        r0 = m.disk()["reads"]
        M.guest_sleep(m, 4)
        reads = m.disk()["reads"] - r0
        want(n0 is not None and rd("ap_state", 1) == AP_ST_PLAY and
             rd("aph_ncopy", 2) > n0 and rd("aph_ndisk", 2) == nd1 and
             reads <= 2,
             "3: the second play (Repeat) PLAYING (state %s) from memory: "
             "gulps copied %s -> %d, off the disk %d -> %d (none), READ DATA "
             "while it played %d (the desktop's own, at most 2)" % (
                 rd("ap_state", 1), n0, rd("aph_ncopy", 2), nd1,
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
                    help="the largest file a hold is claimed for, to 32 KB")
    ap.add_argument("--gap", action="store_true",
                    help="audholdgap: a gulp out of step gives the hold up "
                    "and the play streams on")
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
            print("audhuge: %sMB: the largest file a hold is claimed for is %d KB (%d KB "
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
