#!/usr/bin/env python3
"""COPY ONTO A FAT16 FIXED DISK, OF A FILE PAST ONE KILOBYTE (SPEC.md 96.30.7.3).

    make kdostest && make marty && python3 tests/doscopyhd.py

`COPY B:\\BIG.TXT C:\\` answered `File creation error` for any file past
about a kilobyte, while 13 and 616 bytes copied, the desktop's own copy was
fine and a copy to A: was fine. The shell's buffer is a whole number of
CLUSTERS, because every chunk but the last is read with READ_SEQ and APPENDED
with WRITE_SEQ, and both take only whole clusters (SPEC.md 18.4.8, 18.4.9) -
but it was sized off the volume the PROMPT stood on, with the KB it asked for
left holding a directory cluster number (dsh_bank's [dos_curdir]): at A:\\
that is one 512-byte-cluster KB. The first chunk CREATES the file and any
length is legal there, so a file under 1KB survived; the second chunk is the
first APPEND, 1KB onto a volume of 2KB clusters, and is refused.

THE MACHINE IS tests/dosseq.py's: an XT with an XT-IDE fixed disk, booted
off it, `tools/os88hdd.py` formatting C: at its smallest FAT16 cluster -
2KB, four sectors, which is every FAT16 C: os8088 makes. B: is a floppy
carrying three files: 616 bytes (one chunk, the control: it copied before
the fix), 1,380 and 5,170 bytes (the two sizes the field report named). Each
is copied from the B:\\> prompt with `COPY B:\\NAME C:\\`, and afterwards the
VHD is read ON THE HOST by an
independent FAT16 reader and each file compared byte for byte with what was
put on the floppy.

VERIFIED RED with dsh_copy_go's sizing taken out (the goto and the
`mov ax, DSH_CPKB`): 1,380 and 5,170 are not on C: (COPY's undo deletes a
destination it could not finish), the 616-byte control copied.
"""
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
MACHINE = "os8088_xt_hdd"
VHD = os.path.abspath("build/doscopyhd-%d.vhd" % os.getpid())
FLOP = os.path.abspath("build/doscopyhd-%d.img" % os.getpid())
FILES = {"SMALL.TXT": 616, "MID.TXT": 1380, "BIG.TXT": 5170}


def fail(msg):
    print("doscopyhd: FAIL: %s" % msg)
    sys.exit(1)


def body(name, n):
    """Text that differs at every offset, so a chunk out of place shows."""
    out = b""
    i = 0
    while len(out) < n:
        out += ("%s %05d\r\n" % (name[:3], i)).encode("ascii")
        i += 1
    return out[:n]


def fixture(tmp):
    for p in (TEMPLATE, "build/kernel.sys", "build/dos.o88", "build/hdd.drv"):
        if not os.path.exists(p):
            fail("%s is missing - `make` and `make marty`" % p)
    subprocess.check_call(
        ["python3", "tools/os88hdd.py", "--template", TEMPLATE, "--out", VHD,
         "--kernel", "build/kernel.sys", "--vbr", "build/boothd.bin",
         "--mbr", "build/mbr.bin",
         "--file", "HIBER.DRV=build/hiber.drv",
         "--file", "CTRL.DRV=build/ctrl.drv",
         "--file", "HDD.DRV=build/hdd.drv",
         "--file", "DOS.O88=build/dos.o88"], cwd=ROOT,
        stdout=subprocess.DEVNULL)
    paths = []
    for name, n in FILES.items():
        p = os.path.join(tmp, name)
        open(p, "wb").write(body(name, n))
        paths.append(p)
    subprocess.check_call(["python3", "tools/os88disk.py", "-o", FLOP,
                           "--size", "360"] + paths, cwd=ROOT,
                          stdout=subprocess.DEVNULL)


class Fat16(object):
    """C:, read on the host: the first partition's root, nothing else."""

    def __init__(self, path):
        self.img = open(path, "rb").read()
        lba = struct.unpack_from("<I", self.img, 446 + 8)[0]
        base = lba * 512
        bps, self.spc, rsv, nf, self.nroot = struct.unpack_from(
            "<HBHBH", self.img, base + 11)
        fsz = struct.unpack_from("<H", self.img, base + 22)[0]
        self.bps = bps
        self.fat = base + rsv * bps
        self.root = self.fat + nf * fsz * bps
        self.data = self.root + self.nroot * 32

    def files(self):
        out = {}
        for o in range(self.nroot):
            e = self.img[self.root + o * 32:self.root + o * 32 + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5 or e[11] & 0x18:
                continue
            n = e[:8].rstrip().decode("latin-1")
            if e[8:11].strip():
                n += "." + e[8:11].rstrip().decode("latin-1")
            c = struct.unpack_from("<H", e, 26)[0]
            size = struct.unpack_from("<I", e, 28)[0]
            out[n] = self.body(c, size)
        return out

    def body(self, c, size):
        csz = self.spc * self.bps
        parts = []
        while 2 <= c < 0xFFF8 and len(parts) * csz < size:
            o = self.data + (c - 2) * csz
            parts.append(self.img[o:o + csz])
            c = struct.unpack_from("<H", self.img, self.fat + 2 * c)[0]
        return b"".join(parts)[:size]


def main():
    os.chdir(ROOT)
    import tempfile
    tmp = tempfile.mkdtemp(prefix="doscopyhd-")
    fixture(tmp)
    spc = Fat16(VHD).spc
    print("doscopyhd: C: is FAT16 at %d sectors a cluster (%d bytes)"
          % (spc, spc * 512))
    if spc * 512 <= 1024:
        fail("C:'s cluster is %d bytes, so a 1KB chunk is a whole cluster "
             "there and this row could not see the defect" % (spc * 512))
    bad = []
    try:
        with os88marty.launch(None, apps=FLOP, machine=MACHINE,
                              extra=["--mount", "hd:0:" + VHD],
                              boot=False) as m:
            m.run()
            ui = os88ui.UI(m)
            ui.up(limit=240)
            os88marty.no_saver(m)
            if not ui.path("C:/DOS.O88"):
                fail("could not launch C:/DOS.O88")
            os88marty.settle(m)
            # THE VERDICT IS THE HOST'S: the files on C:, read back after the
            # machine is gone. Each COPY is waited out on the guest's own
            # disk counters - the drives quiet for two guest seconds - since
            # a COPY that failed writes nothing and one that worked stops
            # FROM THE B:\\> PROMPT, which is the field's shape (it was A>):
            # the buffer was sized on the prompt's volume, a floppy's 1KB
            # clusters, so from C:\\> - where the destination IS the prompt's
            # volume - the defect cannot show at all
            m.type_text("B:\n")
            os88marty.settle(m)
            for name in FILES:
                m.type_text("COPY B:\\%s C:\\\n" % name)
                os88marty.quiesce(m, lambda: m.disk(), guest=2.0, stable=3,
                                  what="COPY %s to finish" % name)
                print("doscopyhd: COPY B:\\%s C:\\  (%d bytes) typed"
                      % (name, FILES[name]))
            m.pause()
            m.quit()
        got = Fat16(VHD).files()
        for name, n in FILES.items():
            want = body(name, n)
            if name not in got:
                bad.append("%s is not on C: after the COPY" % name)
            elif got[name] != want:
                bad.append("%s on C: is %d bytes and differs from the floppy's "
                           "%d" % (name, len(got[name]), n))
            else:
                print("doscopyhd: C:\\%s is byte-identical (%d bytes)"
                      % (name, n))
    finally:
        for p in (VHD, FLOP):
            try:
                os.remove(p)
            except OSError:
                pass
    if bad:
        fail("; ".join(bad))
    print("doscopyhd: ok - three sizes copied onto a 2KB-cluster FAT16 C:, "
          "byte for byte")
    return 0


if __name__ == "__main__":
    sys.exit(main())
