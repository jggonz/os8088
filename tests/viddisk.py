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
ceiling falls as the hook takes more, the bytes READ_AT and READ_SEQ brought
back from 12 MB are STREAM.DAT's (every dword its own offset, the pattern W
writes), and the bench SAVED VIDDISK.TXT beside itself (benchlib's bl_save),
read back off the VHD on the host. --stream-name BADAPPLE.V88 names the
stream as the field image does, which is the bench's fallback and has no
pattern to check.

--floppy IS THE FIELD FLOPPY'S PATH (`make viddisk360`), for a machine
nobody can copy 12 MB onto: the fixed disk carries no stream at all and the
bench runs from build/viddisk360.img in B:. W must write a 12.5 MB STREAM.DAT
in C:'s root - read back off the VHD on the host at the end of the write and
checked dword by dword - R must find it there, time it and read the
pattern back, both reports must land on the FLOPPY, and D must delete it
again. Its write is minutes of guest time, so it is the slow arm.

THE CONTROLLER IS NOT THE OWNER'S. MartyPC's fixed disk is XT-IDE, which
moves every byte with the CPU; the owner's ST-225 sits on an ST11M, a DMA
controller. The chain walk is CPU either way and is what this measures; the
TRANSFER rate is this controller's and only this controller's
(tools/martypc/configs/os8088_machines.toml, os8088_5150_herc_hdd_sb_gla).
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
import os88marty, os88ui, os88build, os88flush, os88geom as geom  # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402
from os88fixture import need                                  # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
CHUNK = 32768                  # VK_CHUNK: one W append
STREAM = 13212000               # ~12.6 MB: BADAPPLE in the plan's format
WSTREAM = 400 * 32768           # what W writes: VK_NCHUNK x 32 KB
FLOPPY = "build/viddisk360.img"
ROWS = ("READ_AT 32K @0 MB", "READ_AT 32K @3 MB", "READ_AT 32K @6 MB",
        "READ_AT 32K @9 MB", "READ_AT 32K @12 MB", "int13 one track",
        "int13 one sector", "READ_SEQ seek 0, 1st", "READ_SEQ 32K @0 MB",
        "READ_SEQ seek 12MB 1st", "READ_SEQ 32K @12 MB",
        "READ_SEQ 16K @12 MB", "READ_SEQ 8K @12 MB")
SIZES = (32768,) * 5 + (None, 512) + (32768,) * 4 + (16384, 8192)
CEIL = ("hook 0%", "hook 25%", "hook 50%", "hook 75%", "hook 50%, ints off")
NRES = 21                       # VK_NRES (19, 20: vk_mh's timing)
HZ = 4772727.0


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def cut(m, rw, vhd, n):
    """kill the machine mid-hold and read what the disk holds"""
    os88marty.until(m, lambda mm: rw("vk_wk") >= n, "W to reach the cut",
                    poll=0.5, limit=1800.0, guest=2000.0)
    wk = rw("vk_wk")
    m.close()                           # SIGKILL: the power cut
    bad = []
    vol = os88flush.vhd_volume(vhd)
    try:
        got = vol.read("STREAM.DAT")
    except Exception as e:
        got, bad = None, ["STREAM.DAT unreadable: %s" % e]
    if got is not None:
        want = array.array("I", range(0, CHUNK, 4)).tobytes()
        if got != want:
            bad.append("STREAM.DAT is %d bytes after the cut, not the %d its "
                       "committed first chunk holds, or not those bytes"
                       % (len(got), CHUNK))
    r = subprocess.run([sys.executable, "tools/os88disk.py", "--verify-hdd",
                        vhd], capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip()
    print("\n   CUT after %d chunks (held): STREAM.DAT %s; %s"
          % (wk, "%d bytes" % len(got) if got is not None else "gone",
             out.splitlines()[-1] if out else "(no fsck output)"))
    if r.returncode:                    # chains, loops, cross-links, sizes,
        bad.append("fsck: " + out)      # FAT1 = FAT2 (os88disk.verify_hdd)
    print("viddisk: %s" % ("FAILED" if bad else "ok"))
    return 1 if bad else 0


def unclosed(m, rw, vhd):
    """'u': a held stream never CLOSED, and the bench touches no file after
    it - so the unlock that ends the callback is the only thing that can
    commit it (SPEC.md 18.4.9). Killed a second after W returns, with nothing
    else run, the disk must hold all 12.5 MB"""
    os88marty.until(m, lambda mm: rw("vk_wdone") != 0, "W to finish",
                    poll=2.0, limit=3600.0, guest=4000.0)
    t0 = m.status()["cycles"]
    os88marty.until(m, lambda mm: m.status()["cycles"] - t0 > 4_770_000,
                    "a guest second", poll=0.2, guest=5.0)
    m.close()
    bad = []
    try:
        got = os88flush.vhd_volume(vhd).read("STREAM.DAT")
    except Exception as e:
        got = None
        bad.append("STREAM.DAT unreadable: %s" % e)
    want = 400 * CHUNK
    if got is not None and (len(got) != want or got != array.array(
            "I", range(0, want, 4)).tobytes()):
        bad.append("STREAM.DAT is %d bytes on the disk once the callback "
                   "returned, not %d: the unlock did not commit it"
                   % (len(got), want))
    r = subprocess.run([sys.executable, "tools/os88disk.py", "--verify-hdd",
                        vhd], capture_output=True, text=True)
    if r.returncode:
        bad.append("fsck: " + (r.stdout + r.stderr).strip())
    print("\n   UNCLOSED (held): STREAM.DAT %s on the disk after the unlock"
          % ("%d bytes" % len(got) if got is not None else "gone"))
    for b in bad:
        print("   BAD " + b)
    print("viddisk: %s" % ("FAILED" if bad else "ok"))
    return 1 if bad else 0


def deleted(m, rw, vhd):
    """'k': the held stream is DELETED at chunk 64 and VKSIDE.TXT written,
    which a freed directory slot is exactly where it lands. The delete's gate
    must commit the hold first (SPEC.md 18.4.9) - else the close would patch
    VKSIDE.TXT's entry with the stream's size and chain. So: W stops on the
    next chunk (FERR_NOENT, the stream is gone), VKSIDE.TXT is its own 16
    bytes, STREAM.DAT is gone, and the volume checks clean"""
    os88marty.until(m, lambda mm: rw("vk_wdone") != 0, "W to stop",
                    poll=1.0, limit=1800.0, guest=2000.0)
    wk, werr = rw("vk_wk"), rw("vk_err")
    m.close()
    bad = []
    vol = os88flush.vhd_volume(vhd)
    names = vol.names()
    if "STREAM.DAT" in names:
        bad.append("STREAM.DAT survived its delete")
    try:
        got = vol.read("VKSIDE.TXT")
    except Exception as e:
        got = None
        bad.append("VKSIDE.TXT unreadable: %s" % e)
    if got is not None and len(got) != 16:
        bad.append("VKSIDE.TXT is %d bytes, not its own 16 - a commit "
                   "patched it with the deleted stream's entry" % len(got))
    if werr != 1 or wk != 64:
        bad.append("W stopped after %d chunks with %d errors; wanted 64 and "
                   "the one refusal" % (wk, werr))
    r = subprocess.run([sys.executable, "tools/os88disk.py", "--verify-hdd",
                        vhd], capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip()
    if r.returncode:
        bad.append("fsck: " + out)
    print("\n   DELETED at chunk 64 (held): W stopped at %d, %d error(s); "
          "VKSIDE.TXT %s; %s" % (wk, werr, "%d bytes" % len(got)
                                  if got is not None else "gone",
                                  out.splitlines()[-1] if out else ""))
    for b in bad:
        print("   BAD " + b)
    print("viddisk: %s" % ("FAILED" if bad else "ok"))
    return 1 if bad else 0


def failed(m, rw, vhd, inject=None):
    """a HELD stream one of whose calls FAILS: 'f' fills the volume (~4 MB
    free, no room check) and `inject` fails every data write from a chunk on.
    A failed held call loses ITSELF and nothing else (SPEC.md 18.4.9), as a
    failed APPEND does - so STREAM.DAT is every chunk W counted, byte for
    byte, and the volume checks clean. The two failures are both needed:
    a full disk has walked the whole FAT and flushed every window slide on
    the way, so the held chain is already down when the call is refused, and
    only an I/O error leaves held allocations dirty in the window - which is
    where the first rollback dropped them and a commit then linked the file
    into clusters the disk called free"""
    if inject:
        inject()
    os88marty.until(m, lambda mm: rw("vk_wdone") != 0, "W to stop",
                    poll=1.0, limit=1800.0, guest=2000.0)
    wk, werr = rw("vk_wk"), rw("vk_err")
    m.close()
    bad = []
    try:
        got = os88flush.vhd_volume(vhd).read("STREAM.DAT")
    except Exception as e:
        got = None
        bad.append("STREAM.DAT unreadable: %s" % e)
    want = array.array("I", range(0, wk * CHUNK, 4)).tobytes()
    if got is not None and got != want:
        bad.append("STREAM.DAT is %d bytes after the refused call, not the "
                   "%d of the %d chunks before it" % (len(got), len(want),
                                                        wk))
    if werr != 1 or not 1 < wk < 400:
        bad.append("W stopped after %d chunks with %d errors; wanted ONE "
                   "call refused part-way" % (wk, werr))
    r = subprocess.run([sys.executable, "tools/os88disk.py", "--verify-hdd",
                        vhd], capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip()
    if r.returncode:
        bad.append("fsck: " + out)
    print("\n   REFUSED at chunk %d (held): %d error(s); STREAM.DAT %s; %s"
          % (wk, werr, "%d bytes" % len(got) if got is not None else "gone",
             out.splitlines()[-1] if out else ""))
    for b in bad:
        print("   BAD " + b)
    print("viddisk: %s" % ("FAILED" if bad else "ok"))
    return 1 if bad else 0


def ioerr(m, rw, vhd, n):
    """fail every fixed-disk write into the DATA AREA from chunk `n` on,
    after the int 13h, as a dying patch of disk would answer - through
    every retry, the per-sector ones included, until W has seen the
    refusal. By WHERE and not by run length: dsk_xfer's last resort is a
    sector at a time, and the FAT and the directory below the data area
    must stay writable, or the rollback's own flush is what fails"""
    at = m.sym("dsk_xfer.attempt")
    i13 = at + m.read(at, 200).index(b"\xCD\x13") + 2
    op, unit = m.sym("dsk_op"), m.sym("dsk_unit")
    data = os88flush.vhd_volume(vhd).data_lba   # DI: the volume-relative LBA

    def go():
        os88marty.until(m, lambda mm: rw("vk_wk") >= n, "W to chunk %d" % n,
                        poll=0.5, limit=1800.0, guest=2000.0)
        m.bp_exec(i13)
        try:
            while not rw("vk_err"):
                if m.wait_stop(limit=0.5) != "breakpoint":
                    continue
                rg = m.regs()
                if (m.read(op, 1)[0] == 0x03 and m.read(unit, 1)[0] == 0x80
                        and rg["di"] >= data):
                    m.setreg("ax", 0x1000 | (rg["ax"] & 0xFF))
                    m.setreg("flags", rg["flags"] | 1)
                m.run()
        finally:
            m.bp_exec()
            m.run()
    return go


def free_bytes(vhd):
    """the volume's free space, counted off its FAT on the host"""
    v = os88flush.vhd_volume(vhd)
    n = sum(1 for c in range(2, v.clusters + 2)
            if os88flush.fat_get(v.fat, v.fat12, c) == 0)
    return n * v.spc * 512


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_hdd_sb_gla")
    ap.add_argument("--stream-name", default="STREAM.DAT",
                    help="what to call the stream on the disk: the bench "
                    "takes STREAM.DAT, else BADAPPLE.V88")
    ap.add_argument("--no-stream", action="store_true",
                    help="leave STREAM.DAT off the disk, as a field disk "
                    "has it: the READ_AT rows must SKIP and int 13h still run")
    ap.add_argument("--cut", type=int, default=0, metavar="N",
                    help="a POWER CUT mid-hold (SPEC.md 18.4.9): kill the "
                    "machine once W has written N chunks, then check the VHD "
                    "on the host - the stream at its committed size and "
                    "bytes, and nothing wrong with the volume but lost "
                    "clusters")
    ap.add_argument("--ioerr", type=int, default=0, metavar="N",
                    help="a DYING DISK mid-hold (SPEC.md 18.4.9): every data "
                    "write fails from chunk N on, and the stream must keep "
                    "every chunk before it")
    ap.add_argument("--wmode", choices=("append", "seq", "held", "unclosed",
                                        "inter", "deleted", "full"),
                    default="append", help="the writer: A, OSAPI_FILE_APPEND; P, "
                    "OSAPI_FILE_WRITE_SEQ plain; W (and H), HELD (SPEC.md "
                    "18.4.9) - W was APPEND until 2026-10-07")
    ap.add_argument("--floppy", action="store_true",
                    help="the field floppy's path: the bench in B: off "
                    "build/viddisk360.img, no stream on C:, then W, R and D")
    a = ap.parse_args()
    if a.floppy:
        a.no_stream, a.stream_name = True, "STREAM.DAT"
    os.chdir(ROOT)
    syms, image = pkg_syms("tests/vidbench/viddisk.asm", ("apps/", "tests/"))
    try:
        built = open(os88build.at("build/viddisk.bin"), "rb").read()
    except OSError:
        sys.exit("viddisk: no build/viddisk.bin - run `make vidbench`")
    if built != image:
        sys.exit("viddisk: build/viddisk.bin is behind the tree - run "
                 "`make vidbench`")
    if a.floppy:
        need(FLOPPY)                # `all` builds nothing under tests/
        if not os.path.exists(os88build.at(FLOPPY)):
            sys.exit("viddisk: no %s - run `make viddisk360`" % FLOPPY)
        # THE FLOPPY'S OWN COPY must be the bench the symbols come from: a
        # floppy cut before the last `make vidbench` carries an older image,
        # every word this reads is then some other word, and the row waits
        # for ever on a flag at the wrong address - which is how it first ran
        flo = os88flush.Volume(open(os88build.at(FLOPPY), "rb").read())
        if flo.read("VIDDISK.O88") != open(
                os88build.at("build/viddisk.o88"), "rb").read():
            sys.exit("viddisk: %s carries an older VIDDISK.O88 than "
                     "build/ - run `make viddisk360`" % FLOPPY)
    wbad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        stream = os.path.join(tmp, "STREAM.DAT")
        with open(stream, "wb") as f:           # every dword its own offset,
            array.array("I", range(0, STREAM, 4)).tofile(f)     # as W writes
        vhd = os.path.join(tmp, "viddisk.vhd")
        hdd = (["python3", "tools/os88hdd.py", "--template", TEMPLATE,
                "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
                "--vbr", os88build.at("build/boothd.bin"),
                "--mbr", os88build.at("build/mbr.bin"),
                "--file", "HDD.DRV=" + os88build.at("build/hdd.drv"),
                "--file", "VIDDISK.O88=" + os88build.at("build/viddisk.o88")] +
               ([] if a.no_stream else
                ["--file", a.stream_name + "=" + stream]))
        subprocess.run(hdd, check=True, capture_output=True)
        if a.wmode == "full":           # ~4 MB left: W fails a third of the
            filler = os.path.join(tmp, "FILLER.DAT")    # way through
            with open(filler, "wb") as f:
                f.truncate(free_bytes(vhd) - 4 * 1024 * 1024)
            subprocess.run(hdd + ["--file", "FILLER.DAT=" + filler],
                           check=True, capture_output=True)
        def boot():
            return os88marty.launch(None, apps=FLOPPY if a.floppy else None,
                                    machine=a.machine,
                                    extra=["--mount", "hd:0:" + vhd])

        def opened(m):
            """the bench's window opened; a reader of its words"""
            ui = os88ui.UI(m)
            ui.ready(limit=240)
            w = ui.path("B:/VIDDISK.O88" if a.floppy else "C:/VIDDISK.O88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4
            return base, lambda name: u16(m.read(base + syms[name], 2))

        wtxt = None
        m = boot()
        try:
            base, rw = opened(m)
            if a.floppy:
                m.type_text({"append": "a", "seq": "p", "held": "w",
                             "unclosed": "u", "inter": "i",
                             "deleted": "k", "full": "f"}[a.wmode])
                if a.cut:
                    return cut(m, rw, vhd, a.cut)
                if a.wmode == "deleted":
                    return deleted(m, rw, vhd)
                if a.wmode == "full":
                    return failed(m, rw, vhd)
                if a.ioerr:
                    return failed(m, rw, vhd, ioerr(m, rw, vhd, a.ioerr))
                if a.wmode == "unclosed":
                    return unclosed(m, rw, vhd)
                if os.environ.get("VD_TRACE"):
                    # docs/plans/STREAM-WRITER-PLAN.md 6: the fixed disk's
                    # transfers for the first appends, by kind - a data run,
                    # a one-sector metadata write, a read
                    at = m.sym("dsk_xfer.attempt")
                    i13 = at + m.read(at, 200).index(b"\xCD\x13")
                    m.bp_exec(i13)
                    tally = {}
                    n0 = rw("vk_wk")
                    while rw("vk_wk") < n0 + int(os.environ["VD_TRACE"]):
                        if m.wait_stop(limit=5.0) != "breakpoint":
                            continue
                        rg = m.regs()
                        if rg["dx"] & 0xFF == 0x80 and rw("vk_wk") > n0:
                            op, run = rg["ax"] >> 8, rg["ax"] & 0xFF
                            k = ("read" if op == 2 else
                                 "data write" if run > 1 else "1-sector write")
                            tally[k] = tally.get(k, 0) + 1
                        m.run()
                    m.bp_exec()
                    m.run()
                    per = rw("vk_wk") - n0 - 1
                    print("\n   TRACE, fixed disk, per 32 KB append over %d: %s"
                          % (per, ", ".join("%s %.1f" % (k, v / per)
                                            for k, v in sorted(tally.items()))))
                os88marty.until(m, lambda mm: rw("vk_wdone") != 0,
                                "W to write STREAM.DAT", poll=2.0,
                                limit=3600.0, guest=4000.0)
                wk, werr = rw("vk_wk"), rw("vk_err")
                print("\n   W: %d of 400 chunks, %d errors, %.0f guest s"
                      % (wk, werr, rw("vk_cticks") / 18.2065))
                if wk != 400 or werr:
                    wbad.append("W wrote %d of 400 chunks, %d errors"
                                % (wk, werr))
                a.no_stream = False             # R has a stream now
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
            dchk = m.read(base + syms["vk_dchk"], 1)[0]
            if a.floppy:
                # both reports went to the FLOPPY, beside the bench
                vol = os88flush.Flush(marty=m).volume(1)
                txt, txterr = None, None
                try:
                    txt = vol.read("VIDDISK.TXT").decode("latin-1")
                    wtxt = vol.read("VDWRITE.TXT").decode("latin-1")
                except Exception as e:
                    txterr = str(e)
        finally:
            m.close()
        if not a.floppy:
            try:                    # the report the bench SAVED, off the VHD
                txt = os88flush.vhd_volume(vhd).read("VIDDISK.TXT").decode(
                    "latin-1")
                txterr = None
            except Exception as e:
                txt, txterr = None, str(e)
        else:
            # W's file read by a reader that is NOT the kernel that wrote it
            vol = os88flush.vhd_volume(vhd)
            if "VIDDISK.TXT" in vol.names():
                wbad.append("the report went to C:, not beside the bench")
            try:
                sd = vol.read("STREAM.DAT")
            except Exception as e:
                sd = None
                wbad.append("no STREAM.DAT on C: after W (%s)" % e)
            if sd is not None:
                want = array.array("I", range(0, WSTREAM, 4)).tobytes()
                if sd == want:
                    print("   STREAM.DAT on C:, read on the host: %d bytes, "
                          "every dword its own offset" % len(sd))
                else:
                    n = min(len(sd), len(want))
                    at = next((i for i in range(0, n, 4)
                               if sd[i:i + 4] != want[i:i + 4]), n)
                    wbad.append("STREAM.DAT on C: is %d bytes and differs "
                                "from W's pattern at %d" % (len(sd), at))
            if wtxt is None:
                wbad.append("no VDWRITE.TXT on the floppy (%s)" % txterr)
            elif "write KB/s x 10" not in wtxt:
                wbad.append("VDWRITE.TXT has no write rate")
            else:
                print("   VDWRITE.TXT saved on the floppy: %d lines"
                      % len(wtxt.splitlines()))
            m = boot()              # ...and D, on a second boot
            try:
                base, rw = opened(m)
                m.type_text("d")
                os88marty.until(m, lambda mm: rw("vk_ddone") != 0,
                                "D to delete STREAM.DAT", poll=1.0,
                                limit=600.0, guest=600.0)
            finally:
                m.close()
            if "STREAM.DAT" in os88flush.vhd_volume(vhd).names():
                wbad.append("D left STREAM.DAT on C:")
            else:
                print("   D: STREAM.DAT is gone from C:")
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
    if not a.no_stream:
        want = 1 if a.stream_name == "STREAM.DAT" else 0
        print("   the bytes at 12 MB: %s" % ("not checked", "ok", "BAD")[
            min(dchk, 2)])
        if dchk != want:
            bad.append("the data check at 12 MB read %d, wanted %d (1 = "
                       "STREAM.DAT's pattern came back)" % (dchk, want))
    bad += wbad
    if err:
        bad.append("%d calls errored" % err)
    if full:
        bad.append("the report TRUNCATED (bl_full): the arena is too small")
    if txt is None:
        bad.append("no VIDDISK.TXT beside the bench - the save did not "
                   "happen (%s)" % txterr)
    elif "int13 one sector" not in txt or "TRUNCATED" in txt:
        bad.append("VIDDISK.TXT is not the whole report")
    else:
        print("\n   VIDDISK.TXT saved: %d lines" % len(txt.splitlines()))
        # ONE int 13h ACROSS A HEAD (the bench's vk_mh): XT-IDE's ROM hands
        # the count to a drive that walks its own geometry, so here both
        # crossings must read the truth and one call must not be the slower
        # - which is the row's mechanics checked, not the ST11M's answer
        xs = [l for l in txt.splitlines() if l.startswith((
            "one call across", "sectors timed", "N sectors", "...the ROM",
            "kernel shapes", "...WRONG", "...refused", "first wrong",
            "...head", "...from sector", "...sectors"))]
        for l in xs:
            print("   " + l.rstrip())
        for what in ("one call across a head", "one call across a cyl"):
            if not any(l.startswith(what) and "ok - the same bytes" in l
                       for l in xs):
                bad.append("%s: not the same bytes on XT-IDE" % what)
        # ...and the KERNEL'S OWN SHAPES (vk_msweep): runs from mid-track to
        # the cylinder's end, which the two rows above never issue - the
        # ST11M passed them and failed these
        def num(lab):
            for l in xs:
                if l.startswith(lab):
                    return int(l[len(lab):].split()[0])
            return None
        nsh, nbad, nref = (num("kernel shapes read"), num("...WRONG BYTES"),
                           num("...refused"))
        if not nsh or nbad != 0 or nref != 0:
            bad.append("the kernel's shapes: %s read, %s wrong, %s refused "
                       "on XT-IDE" % (nsh, nbad, nref))
        tsp = val(19) / 100.0           # hundredths of a us, as vk_bank
        ton = val(20) / 100.0           # keeps every row
        print("   one call against a call a track: %.1f ms against %.1f"
              % (ton / 1000.0, tsp / 1000.0))
        if not ton or ton > tsp * 1.1:     # (tick-timed: ~5 ms a row)
            bad.append("one call across the heads was not timed, or was "
                       "slower than a call a track")
    for b in bad:
        print("   FAIL: %s" % b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
