#!/usr/bin/env python3
"""viddiskcpu - VIDDISK's C mode (docs/plans/DISK-CPU-PLAN.md 7): what a read
costs the CPU, measured by the bench reading sectors through the task file
itself.

    make vidbench && python3 tests/viddiskcpu.py

AN INSTRUMENT'S GATE, not a measurement: QEMU counts work and cannot time it,
so every number here is the host's. What it asserts is that C is fit to send
to a real 286 - every row produced a number, no command ended in ERR, no row
errored, and the report was saved - and, the half a timed row cannot show,
that the bench's own commands read THE SECTORS IT ASKED FOR. C runs its
direct stream last, so the buffer ends holding the 64 sectors before
[vk_cplba]; they are compared here with the same sectors of the image, which
is STREAM.DAT's pattern - every dword its own offset - inside the 16 MB
stream on a 16-head, 63-sector disk, so that any other sector differs.

QEMU by name: `rep insw` and the IDE task file both need a 286-class machine
(docs/TESTING.md's list, entries 1 and 2). Break on purpose: get vk_ide_cmd's
CHS split wrong (heads and sectors swapped) and the buffer is some other
sectors; drop `rep insw` and every transfer row reads near zero against the
PIT read's own row while the buffer check fails.
"""
import array
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
import os88build                                              # noqa: E402
import os88flush                                              # noqa: E402
import os88geom as geom                                       # noqa: E402
import os88qemu                                               # noqa: E402
import os88ui                                                 # noqa: E402
import xmcheck                                                # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402
from hdtake import QH                                         # noqa: E402

STREAM = 16 * 1024 * 1024    # past LBA 29,376, where the direct reads end
                              # (and >= 12 MB + 32 KB, vk_find's test)
ROWS = ("a PIT read, itself", "rep insw, 256 words", "in/stosw/loop, 256 w",
        "...a command, 1st DRQ", "READ_SEQ 32K", "direct 64-sector cmd")


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def main():
    syms, image = pkg_syms("tests/vidbench/viddisk.asm", ("apps/", "tests/"))
    try:
        built = open(os88build.at("build/viddisk.bin"), "rb").read()
    except OSError:
        sys.exit("viddiskcpu: no build/viddisk.bin - run `make vidbench`")
    if built != image:
        sys.exit("viddiskcpu: build/viddisk.bin is behind the tree - run "
                 "`make vidbench`")
    bad = []
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    q = None
    try:
        stream = os.path.join(tmp, "STREAM.DAT")
        with open(stream, "wb") as f:
            array.array("I", range(0, STREAM, 4)).tofile(f)
        hdimg = os.path.join(tmp, "hd.img")
        subprocess.run([sys.executable, "tools/os88hdd.py", "--raw",
                        "--noboot", "--out", hdimg, "--heads", "16",
                        "--spt", "63", "--cyls", "130", "--file",
                        "VIDDISK.O88=" + os88build.at("build/viddisk.o88"),
                        "--file", "STREAM.DAT=" + stream],
                       check=True, capture_output=True)
        sysimg = os.path.join(tmp, "sys.img")
        shutil.copy(os88build.at("build/os8088.img"), sysimg)
        cfg = os.path.join(tmp, "SYSTEM.CFG")      # HDD.DRV wanted (bit 1)
        open(cfg, "wb").write(b"O88CFG\0\0" + (3).to_bytes(2, "little")
                              + b"DW" + bytes([1, 2])
                              + (1 << 1).to_bytes(2, "little") + b"\0\0")
        subprocess.run([sys.executable, "tools/os88fat.py", "add", sysimg,
                        cfg, "SYSTEM.CFG"], check=True, capture_output=True)
        appsimg = os.path.join(tmp, "apps.img")
        shutil.copy(os88build.at("build/apps.img"), appsimg)
        q = QH(tmp, sysimg, appsimg, hdimg)
        os88qemu.acted(q, lambda: os.path.exists(q.sock), secs=30,
                       what="the QMP socket")
        xmcheck.wait_desktop(q.sock, "viddiskcpu")
        S = xmcheck.sym
        ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)
        box = {}

        def opened(key, before):
            for o in ui.windows():
                if o.i not in before and o.visible:
                    box[key] = o
                    return True
            return False
        before = set(o.i for o in ui.windows())
        xmcheck.dblclick(q.sock, *geom.drive_pt(q, "C", S))
        if not os88qemu.acted(q, lambda: opened("c", before), secs=30,
                              what="C:'s window"):
            bad.append("C:'s window never opened")
            return report(bad)
        os88qemu.pace(q, 1)
        w = box["c"]
        i, _ = ui.entry("VIDDISK.O88", w)
        before = set(o.i for o in ui.windows())
        xmcheck.dblclick(q.sock, *ui.row_xy(w, i - ui.scroll(w)))
        if not os88qemu.acted(q, lambda: opened("p", before), secs=30,
                              what="VIDDISK"):
            bad.append("VIDDISK did not open off C:")
            return report(bad)
        rec = q.read(S("wm_wins") + box["p"].i * geom.WIN_SIZE,
                      geom.WIN_SIZE)
        base = u16(rec, geom.W_SEG) << 4

        def rw(n):
            return u16(q.read(base + syms[n], 2))
        os88qemu.pace(q, 1)
        q.hmp("sendkey c")
        if not os88qemu.acted(q, lambda: rw("vk_cpdone") != 0, secs=600,
                              what="C to finish"):
            bad.append("C never finished")
            return report(bad)
        lba = rw("vk_cplba")
        buf = q.read(rw("vk_buf") << 4, 32768)
        q.close()
        q = None
        disk = open(hdimg, "rb").read()
        want = disk[(lba - 64) * 512:lba * 512]
        same = buf == want
        if not want.strip(b"\0"):
            bad.append("the sectors the direct reads ended on are empty disk "
                       "- the check below would compare zeros with zeros")
        print("   the last direct command: LBA %d..%d, %s the image's"
              % (lba - 64, lba - 1, "the same bytes as" if same
                 else "NOT"))
        if not same:
            bad.append("the bench's own commands read other sectors than "
                       "the ones it asked for (LBA %d..%d)" % (lba - 64,
                                                             lba - 1))
        if lba != 20 * 16 * 63 + (8 * 2 + 128) * 64:
            bad.append("the direct reads did not all run: [vk_cplba] is %d"
                       % lba)
        try:
            txt = os88flush.vhd_volume(hdimg).read_latest("VDCPU.TXT").decode(
                "latin-1")
        except Exception as e:
            bad.append("no VDCPU.TXT beside the bench: %s" % e)
            return report(bad)
        vals = {}
        for line in txt.splitlines():
            for r in ROWS + ("...commands ending ERR", "errors (any row)"):
                if line.startswith(r):
                    f = line[len(r):].split()
                    if f:
                        try:
                            vals[r] = int(f[0])
                        except ValueError:
                            pass
        print("   VDCPU.TXT: %d lines; %s" % (len(txt.splitlines()), vals))
        for r in ROWS:
            if not vals.get(r):
                bad.append("row %r has no number" % r)
        if vals.get("...commands ending ERR", 1) != 0:
            bad.append("commands ended in ERR")
        if vals.get("errors (any row)", 1) != 0:
            bad.append("the bench counted errors")
    finally:
        if q is not None:
            q.close()
        shutil.rmtree(tmp, ignore_errors=True)
    return report(bad)


def report(bad):
    for b in bad:
        print("   FAIL: %s" % b)
    print("viddiskcpu: %s" % ("FAIL" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
