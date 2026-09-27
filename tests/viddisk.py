#!/usr/bin/env python3
"""What streaming a large file off the fixed disk costs - VIDEO-PLAN wave 0
(b), and the rows waves 2 and 3 added.

    make vidbench && python3 tests/viddisk.py
                                [--machine os8088_5150_herc_hdd_sb_gla]

AN INSTRUMENT. tests/vidbench/viddisk.asm runs on a machine booted off a
fixed disk that carries a 12.6 MB STREAM.DAT - about the size BADAPPLE
comes to in the plan's format - and times OSAPI_FILE_READ_AT reading 32 KB
at 0, 3, 6, 9 and 12 MB into it, then the ROM's own int 13h reading whole
tracks and single sectors. SPEC.md 18.4.4 says READ_AT re-walks the
cluster chain from the front on every call, so the per-call time should
GROW with the offset; the slope is what OSAPI_FILE_READ_SEQ (VIDEO-PLAN 4.2)
exists to remove, and int 13h's track rate is the ceiling it can approach.

Then OSAPI_FILE_READ_SEQ (SPEC.md 18.4.8): a seek's first call, 32 KB
calls at 0 and at 12 MB (which must not differ the way READ_AT's do), 16
and 8 KB calls, and how many int 13h calls one 32 KB call costs and how many
of them land under cylinder 16 (the FAT). And the SILENT PLAYER'S CEILING
(SPEC.md 98.3): READ_SEQ streaming for 5 s inside an FSXF_RATE bracket whose
30 Hz hook holds 0/25/50/75% of every period - interrupts on, as the
player's decode does - and 50% with them off.

What it asserts: every row produced a number, every READ_AT delivered its
32 KB, no call errored, READ_SEQ at 12 MB is no dearer than at 0 MB, the
ceiling falls as the hook takes more, and the bench SAVED VIDDISK.TXT beside
itself (benchlib's bl_save), read back off the VHD on the host.
--stream-name BADAPPLE.V88 names the stream as the field image does, which
is the bench's fallback.

THE CONTROLLER IS NOT THE OWNER'S. MartyPC's fixed disk is XT-IDE, which
moves every byte with the CPU; the owner's ST-225 sits on an ST11M, a DMA
controller. The chain walk is CPU either way and is what this measures; the
TRANSFER rate is this controller's and only this controller's
(tools/martypc/configs/os8088_machines.toml, os8088_5150_herc_hdd_sb_gla).
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88build, os88flush, os88geom as geom  # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
STREAM = 13212000               # ~12.6 MB: BADAPPLE in the plan's format
ROWS = ("READ_AT 32K @0 MB", "READ_AT 32K @3 MB", "READ_AT 32K @6 MB",
        "READ_AT 32K @9 MB", "READ_AT 32K @12 MB", "int13 one track",
        "int13 one sector", "READ_SEQ seek 0, 1st", "READ_SEQ 32K @0 MB",
        "READ_SEQ seek 12MB 1st", "READ_SEQ 32K @12 MB",
        "READ_SEQ 16K @12 MB", "READ_SEQ 8K @12 MB")
SIZES = (32768,) * 5 + (None, 512) + (32768,) * 4 + (16384, 8192)
CEIL = ("hook 0%", "hook 25%", "hook 50%", "hook 75%", "hook 50%, ints off")
NRES = 20                       # VK_NRES
HZ = 4772727.0


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_hdd_sb_gla")
    ap.add_argument("--stream-name", default="STREAM.DAT",
                    help="what to call the stream on the disk: the bench "
                    "takes STREAM.DAT, else BADAPPLE.V88")
    ap.add_argument("--no-stream", action="store_true",
                    help="leave STREAM.DAT off the disk, as a field disk "
                    "has it: the READ_AT rows must SKIP and int 13h still run")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, image = pkg_syms("tests/vidbench/viddisk.asm", ("apps/", "tests/"))
    try:
        built = open(os88build.at("build/viddisk.bin"), "rb").read()
    except OSError:
        sys.exit("viddisk: no build/viddisk.bin - run `make vidbench`")
    if built != image:
        sys.exit("viddisk: build/viddisk.bin is behind the tree - run "
                 "`make vidbench`")
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        stream = os.path.join(tmp, "STREAM.DAT")
        with open(stream, "wb") as f:           # the content is not the point
            blk = bytes(range(256)) * 256
            left = STREAM
            while left:
                n = min(left, len(blk))
                f.write(blk[:n])
                left -= n
        vhd = os.path.join(tmp, "viddisk.vhd")
        subprocess.run(
            ["python3", "tools/os88hdd.py", "--template", TEMPLATE,
             "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
             "--vbr", os88build.at("build/boothd.bin"),
             "--mbr", os88build.at("build/mbr.bin"),
             "--file", "HDD.DRV=" + os88build.at("build/hdd.drv"),
             "--file", "VIDDISK.O88=" + os88build.at("build/viddisk.o88")] +
            ([] if a.no_stream else ["--file", a.stream_name + "=" + stream]),
            check=True, capture_output=True)
        m = os88marty.launch(None, machine=a.machine,
                             extra=["--mount", "hd:0:" + vhd])
        try:
            ui = os88ui.UI(m)
            ui.ready(limit=240)
            w = ui.path("C:/VIDDISK.O88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4

            def rw(name):
                return u16(m.read(base + syms[name], 2))

            m.type_text("r")
            os88marty.until(m, lambda mm: rw("vk_done") != 0,
                            "the disk bench to finish", poll=1.0,
                            limit=1500.0, guest=900.0)
            done = rw("vk_done")
            res = m.read(base + syms["vk_res"], NRES * 4)
            full = m.read(base + syms["bl_full"], 1)[0]
            err, got = rw("vk_err"), rw("vk_got")
            spt = m.read(base + syms["vk_spt"], 1)[0]
            heads = m.read(base + syms["vk_heads"], 1)[0]
        finally:
            m.close()
        try:                        # the report the bench SAVED, off the VHD
            txt = os88flush.vhd_volume(vhd).read("VIDDISK.TXT").decode(
                "latin-1")
            txterr = None
        except Exception as e:
            txt, txterr = None, str(e)
    if done == 0xFFFF:
        sys.exit("viddisk: the bench could not claim, or no fixed disk "
                 "answered int 13h")
    def val(i):
        return int.from_bytes(res[i * 4:i * 4 + 4], "little")
    us = [val(i) / 100.0 for i in range(13)]
    print("\n   machine %s: fixed disk %d sectors a track, %d heads"
          % (a.machine, spt, heads))
    print("   %-22s %12s %12s" % ("row", "ms / call", "KB/s"))
    bad = []
    for i, lab in enumerate(ROWS):
        n = SIZES[i] or spt * 512
        ms = us[i] / 1000.0
        if a.no_stream and i not in (5, 6):
            if us[i]:
                bad.append("%s ran with no stream - the skip failed" % lab)
            continue
        if us[i] <= 0:
            bad.append("%s produced no number" % lab)
        print("   %-22s %12.1f %12.1f" % (lab, ms,
                                          n / 1024.0 / (ms / 1000.0)
                                          if ms else 0))
    if not a.no_stream:
        slope = (us[4] - us[0]) / 12.0 / 1000.0
        print("\n   READ_AT grows %.1f ms per MB of offset (the chain walk)"
              % slope)
        print("   READ_SEQ 32K: %.1f ms at 0 MB, %.1f at 12 MB; the seek to "
              "12 MB walks once, %.1f ms" % (us[8] / 1e3, us[10] / 1e3,
                                             us[9] / 1e3))
        if us[10] > us[8] * 1.25 + 5000:
            bad.append("READ_SEQ at 12 MB (%.1f ms) is dearer than at 0 MB "
                       "(%.1f) - it is walking" % (us[10] / 1e3, us[8] / 1e3))
        n13, lo13 = val(13) & 0xFFFF, val(13) >> 16
        print("   int 13h calls for 8 x 32 KB READ_SEQ at 12 MB: %d, %d of "
              "them under cylinder 16 (the FAT and the root)" % (n13, lo13))
        if not n13:
            bad.append("the int 13h counter saw no call")
        print("\n   the silent player's ceiling, 32 KB READ_SEQ for 5 s under "
              "a 30 Hz hook:")
        ceil = [val(14 + k) / 10.0 for k in range(5)]
        for k, lab in enumerate(CEIL):
            print("   %-22s %9.1f KB/s" % (lab, ceil[k]))
            if ceil[k] <= 0:
                bad.append("ceiling %s produced no number" % lab)
        if not ceil[0] > ceil[1] > ceil[2] > ceil[3]:
            bad.append("the ceiling does not fall as the hook holds more")
    if not a.no_stream and got != 32768 and got not in (16384, 8192):
        bad.append("the last read delivered %d bytes" % got)
    if err:
        bad.append("%d calls errored" % err)
    if full:
        bad.append("the report TRUNCATED (bl_full): the arena is too small")
    if txt is None:
        bad.append("no VIDDISK.TXT on C: - the save did not happen (%s)"
                   % txterr)
    elif "int13 one sector" not in txt or "TRUNCATED" in txt:
        bad.append("VIDDISK.TXT is not the whole report")
    else:
        print("\n   VIDDISK.TXT saved: %d lines" % len(txt.splitlines()))
    for b in bad:
        print("   FAIL: %s" % b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
