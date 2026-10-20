#!/usr/bin/env python3
"""Video Player wave 2's three kernel changes, on the 5150-shaped machine -
FSXF_RATE (SPEC.md 53.2.2), the progress-box fence (12.8.5.2) and
OSAPI_FILE_READ_SEQ (18.4.8).

    make vidbench && python3 tests/vidkern.py
                                [--machine os8088_5150_herc_hdd_sb_gla]

tests/vidkern/vidkern.asm runs on a machine booted off a fixed disk carrying
STREAM.DAT (12.6 MB, every dword its own offset) and FENCE.DAT (8 KB). Three
keys, three verdicts:

R  the three calls that must refuse DID (FASTTICK with RATE, a divisor under
   FSX_RATE_MIN, a hook outside the image); the bracket at 30.0 Hz ran; the
   periods the hook was handed against [ticks] is 65536/39773 within 3 - the
   accumulator - even though every 16th call stis and runs two periods long,
   so the kernel must SKIP calls and hand the periods on (the most handed
   to one call must be >= 2, or that path never ran); the BIOS's own 40:6C
   moved with [ticks]; and [sch_fast] is 0 after it.
F  the read before the bracket ARMED the widget (the control: without it
   the row proves nothing); a same-mode bracket's door took it down; a read
   inside the bracket did not arm it.
S  every byte READ_SEQ delivered is the one at its offset, across a seek, a
   write and a delete in the middle of a run (each bumps the mount
   generation, so the cursor re-seeds), the end of the file (the tail whole,
   then 0) and a capacity that is not a cluster multiple (FERR_NAME). And it
   is FLAT: 32 KB at 12 MB costs within 25% of 32 KB at 0 MB, where READ_AT
   at 12 MB is reported beside it.

Broken on purpose - the accumulator's `jc .full` made unconditional (every
entry a tick), fpg_arm's test put back to [fsx_cur], READ_SEQ's `.walk`
skip removed so it walks from the cursor's cluster - each verdict goes red.
"""
import argparse
import array
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88build, os88flush, os88geom as geom        # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
SIZE = 13212000                 # VK_SIZE
DIV = 39773                     # VK_DIV
TICK_MS = 65536 * 1000.0 / 1193182


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_hdd_sb_gla")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, image = pkg_syms("tests/vidkern/vidkern.asm", ("apps/", "tests/"))
    try:
        built = open(os88build.at("build/vidkern.bin"), "rb").read()
    except OSError:
        sys.exit("vidkern: no build/vidkern.bin - run `make vidbench`")
    if built != image:
        sys.exit("vidkern: build/vidkern.bin is behind the tree - run "
                 "`make vidbench`")
    ferr = [ln for ln in open("apps/os88api.inc") if ln.startswith("FERR_NAME")]
    ferr_name = int(ferr[0].split()[2], 0)
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        stream = os.path.join(tmp, "STREAM.DAT")
        with open(stream, "wb") as f:
            array.array("I", range(0, SIZE, 4)).tofile(f)
        fence = os.path.join(tmp, "FENCE.DAT")
        with open(fence, "wb") as f:
            f.write(bytes(range(256)) * 32)
        vhd = os.path.join(tmp, "vidkern.vhd")
        subprocess.run(
            ["python3", "tools/os88hdd.py", "--template", TEMPLATE,
             "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
             "--vbr", os88build.at("build/boothd.bin"),
             "--mbr", os88build.at("build/mbr.bin"),
             "--file", "HDD.DRV=" + os88build.at("build/hdd.drv"),
             "--file", "VIDKERN.O88=" + os88build.at("build/vidkern.o88"),
             "--file", "FENCE.DAT=" + fence,
             "--file", "STREAM.DAT=" + stream], check=True,
            capture_output=True)
        m = os88marty.launch(None, machine=a.machine,
                             extra=["--mount", "hd:0:" + vhd])
        try:
            ui = os88ui.UI(m)
            ui.ready(limit=240)
            w = ui.path("C:/VIDKERN.O88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4

            def rw(name, off=0):
                return u16(m.read(base + syms[name] + off, 2))

            def rb(name):
                return m.read(base + syms[name], 1)[0]

            m.type_text("r")
            os88marty.until(m, lambda mm: rw("vk_rdone") != 0,
                            "the rate bracket to end", poll=1.0,
                            limit=600.0, guest=60.0)
            sch_fast = ui._byte("sch_fast")

            m.type_text("f")
            fpg = {}
            for ph in (1, 2, 3):
                os88marty.until(m, lambda mm: rb("vk_phase") == ph,
                                "the fence's phase %d" % ph, poll=0.5,
                                limit=300.0, guest=30.0)
                fpg[ph] = ui._byte("fpg_on")
                m.write(base + syms["vk_go"], bytes([ph]))
            os88marty.until(m, lambda mm: rw("vk_fdone") != 0,
                            "the fence row to end", poll=0.5, limit=300.0,
                            guest=30.0)

            m.type_text("s")
            os88marty.until(m, lambda mm: rw("vk_sdone") != 0,
                            "the READ_SEQ rows to end", poll=1.0,
                            limit=1500.0, guest=300.0)
            r = [rw("vk_res", 2 * i) for i in range(26)]

            # A: the same three for a person - timed parks, no harness to
            # say go - reported and SAVED as VIDKERN.TXT beside the package
            m.type_text("a")
            os88marty.until(m, lambda mm: rb("bl_saved") != 0,
                            "the run-all to save its report", poll=1.0,
                            limit=1500.0, guest=300.0)
            full = rb("bl_full")
            auto = [rw("vk_res", 2 * i) for i in range(26)]
        finally:
            m.close()
        try:
            txt = os88flush.vhd_volume(vhd).read("VIDKERN.TXT").decode(
                "latin-1")
            txterr = None
        except Exception as e:
            txt, txterr = None, str(e)

    bad = []
    print("\n   machine %s" % a.machine)
    # --- R ---
    periods = r[3] | r[4] << 16
    want = r[6] * 65536.0 / DIV
    print("\n   FSXF_RATE at 30.0 Hz (divisor %d)" % DIV)
    print("     refusals 0x%x (want 0x7)   bracket CF %d" % (r[0], r[1] & 1))
    print("     hook calls %d, periods handed %d, most in one call %d"
          % (r[2], periods, r[5]))
    print("     [ticks] moved %d, BIOS 40:6C moved %d; periods / ticks = %.4f "
          "(want %.4f)" % (r[6], r[7], periods / max(r[6], 1), 65536.0 / DIV))
    if r[0] != 7:
        bad.append("a call that must refuse did not (bits 0x%x)" % r[0])
    if r[1]:
        bad.append("the bracket was refused")
    if abs(periods - want) > 3:
        bad.append("%d periods in %d ticks, not %.1f: the accumulator"
                   % (periods, r[6], want))
    if r[5] < 2:
        bad.append("no call was handed 2+ periods: the skip path never ran")
    if r[2] >= periods:
        bad.append("as many calls as periods: nothing was skipped")
    if abs(r[7] - r[6]) > 1:
        bad.append("the BIOS clock moved %d where [ticks] moved %d"
                   % (r[7], r[6]))
    if sch_fast:
        bad.append("[sch_fast] = %d after the bracket" % sch_fast)
    # --- F ---
    print("\n   the progress-box fence: [fpg_on] after the arming read %d, "
          "at the bracket's door %d, after a read inside it %d"
          % (fpg[1], fpg[2], fpg[3]))
    if not fpg[1]:
        bad.append("CONTROL: the read before the bracket did not arm the "
                   "widget, so the fence rows prove nothing")
    if fpg[2]:
        bad.append("the widget survived into a same-mode bracket")
    if fpg[3]:
        bad.append("a read inside a same-mode bracket armed the widget")
    if r[8]:
        bad.append("the same-mode bracket was refused")
    # --- S ---
    per = lambda t, n: t * TICK_MS / n                          # noqa: E731
    print("\n   OSAPI_FILE_READ_SEQ, 32 KB a call, %d-byte clusters "
          "(the read alone)" % r[18])
    print("     at  0 MB  %7.1f ms a call" % per(r[9], 8))
    print("     at 12 MB  %7.1f ms a call" % per(r[10], 8))
    print("     int 13h calls on 80h (of them under cylinder 16): at 0 MB "
          "%d (%d), at 12 MB %d (%d)" % (r[21], r[22], r[23], r[24]))
    print("     the seek to 12 MB (its first call walks once) %7.1f ms"
          % per(r[25], 1))
    print("     READ_AT at 12 MB  %7.1f ms a call" % per(r[11], 2))
    print("     end: tail %d bytes, then %d   refusal AX %d (want %d)   "
          "chunks wrong %d   calls failed %d"
          % (r[13], r[14], r[15], ferr_name, r[12], r[17]))
    tail = SIZE - (SIZE & ~(r[18] - 1)) if r[18] else -1
    if r[12]:
        bad.append("%d chunks held bytes from the wrong offset" % r[12])
    if r[17]:
        bad.append("%d calls failed" % r[17])
    if not r[16]:
        bad.append("the write-in-the-middle row did not run")
    if r[13] != tail or r[14] != 0:
        bad.append("the end: %d then %d, not %d then 0" % (r[13], r[14], tail))
    if r[15] != ferr_name:
        bad.append("a capacity of 1000 answered %d, not FERR_NAME" % r[15])
    if r[24]:
        bad.append("%d int 13h calls at 12 MB went under cylinder 16: the "
                   "chain is being re-read" % r[24])
    if not r[9] or r[10] > r[9] * 1.25 + 2:
        bad.append("READ_SEQ is not flat: %d ticks at 0 MB, %d at 12 MB"
                   % (r[9], r[10]))
    # --- A ---
    if auto[0] != 7 or auto[1] or auto[8] or auto[12] or auto[17]:
        bad.append("the run-all's rows differ from the keyed ones: refusals "
                   "0x%x, CFs %d/%d, bad chunks %d, failed calls %d"
                   % (auto[0], auto[1], auto[8], auto[12], auto[17]))
    if full:
        bad.append("the report TRUNCATED (bl_full)")
    if txt is None:
        bad.append("no VIDKERN.TXT on C: - the save did not happen (%s)"
                   % txterr)
    elif "periods/tick" not in txt or "8 x 32K @12 MB" not in txt:
        bad.append("VIDKERN.TXT is not the whole report")
    else:
        print("\n   VIDKERN.TXT saved: %d lines" % len(txt.splitlines()))
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("\n   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
