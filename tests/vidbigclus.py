#!/usr/bin/env python3
"""A RESIDENT .V88 PLAYS OFF A VOLUME WITH 32 KB CLUSTERS - SPEC.md 98.1.7.1.

    make && python3 tests/vidbigclus.py

Reported off the owner's 5150: an install to a 2 GB partition, the media
disk's OS8088.V88 copied to C:\\MEDIA, and Play answered "This .V88 is
damaged" - where the same file plays off the floppy.

A 2 GB FAT16 has 64 sectors a cluster (SPEC.md 18.7.5, DOS's own table), and
OSAPI_FILE_READ_AT reads whole clusters in a WORD of bytes. The player read a
resident block as the block's clusters in ONE call and summed that in 16 bits,
so a 31,848-byte block 10,240 bytes into its cluster came to 74,855 - a carry,
refused as a damaged file. vp_rdat reads such a span in two calls now, and its
buffers are sized in 32 bits by vp_spankb.

The fixture is bigvol.py's 321MB XT-IDE disk, formatted on the HOST with
mtools at `-c 64`: a volume that small needs no 2GB to have 2GB's cluster. The
system floppy boots with a SYSTEM.CFG that wants HDD.DRV, so C: is the disk
and B: is the apps floppy, which carries VIDEO.O88.

1. THE CLUSTER IS 32 KB: the fixture is what it claims (spc 64), or the row
   tests nothing.
2. IT OPENS: C:/MEDIA/OS8088.V88 opens the Video Player, playable.
3. IT PLAYS: Space starts a session - [vp_sess] set, and [vp_msg] never
   the damaged-file sentence.
4. THE BLOCK IS RIGHT: the rendition's block in its claim, byte for byte as
   tools/os88vid.py reads it - the read that spans two READ_AT calls, and
   the pointer vp_ldblk moves into a segment of its own, land every byte.
5. FRAMES ARE DRAWN: twenty of them ([vp_vseq]), off that block.

Broken on purpose - vp_ldblk's 16-bit sum and vp_rdat's one call put back
(the tree before this row) - 3 FAILS with "This .V88 is damaged".

REQUIRES mtools.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty as M, os88ui, os88build, os88geom as geom  # noqa: E402
import os88vid as vid                                        # noqa: E402
import instdeep as ID                                        # noqa: E402
from bigvol import vhd_footer, chs, C, H, S, BASE, SECS, SECTOR  # noqa: E402
from cycweb import pkg_syms                                  # noqa: E402

MACHINE = "os8088_5150_herc_hdd_sb_gla"     # the owner's 5150, Hercules
HDD_CFGBIT = 1                              # kernel/driver.inc's drv_cfgbit


def stage(vhd, v88):
    size, foot = vhd_footer(C, H, S)
    mbr = bytearray(SECTOR)
    mbr[446:462] = (bytes([0x00]) + chs(BASE) + bytes([0x06])
                    + chs(BASE + SECS - 1) + struct.pack("<II", BASE, SECS))
    mbr[510:512] = b"\x55\xAA"
    with tempfile.TemporaryDirectory() as td:
        img = os.path.join(td, "part.img")
        with open(img, "wb") as f:
            f.truncate(SECS * SECTOR)
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")

        def mt(*a):
            subprocess.run(a, check=True, env=env, stdout=subprocess.DEVNULL)
        mt("mformat", "-i", img, "-T", str(SECS), "-h", str(H), "-s", str(S),
           "-H", str(BASE), "-c", "64", "-r", "32", "-M", "512", "::")
        mt("mmd", "-i", img, "::/MEDIA")
        mt("mcopy", "-i", img, v88, "::/MEDIA/OS8088.V88")
        part = open(img, "rb").read()
    os.makedirs(os.path.dirname(vhd), exist_ok=True)
    with open(vhd, "wb") as f:
        f.write(mbr)
        f.seek(BASE * SECTOR)
        f.write(part)
        f.truncate(size)
        f.seek(size)
        f.write(foot)


def main():
    os.chdir(ROOT)
    if not os.path.exists("build/martypc/run/martypc_headless"):
        sys.exit("no MartyPC - `make marty` first")
    if not shutil.which("mformat"):
        print("   SKIP: no mtools")
        return 0
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    run_dir = M.stage_run_dir("vidbigclus")
    vhd = os.path.join(run_dir, ID.VHD_REL)
    stage(vhd, "apps/video/os8088.v88")
    with open(vhd, "rb") as f:
        f.seek(BASE * SECTOR)
        bpb = f.read(SECTOR)
    spc, fst = bpb[13], bpb[54:62]
    print("   1: %s, %d sectors a cluster" % (fst.decode().strip(), spc))
    if fst != b"FAT16   " or spc != 64:
        print("   FAIL: the fixture has no 32 KB cluster - the row tests "
              "nothing")
        return 1

    bad = []
    with tempfile.TemporaryDirectory(dir=os88build.at("build")) as tmp:
        sysimg = os.path.join(tmp, "SYS.IMG")
        shutil.copyfile(os88build.at("build/os8088-360.img"), sysimg)
        cfg = os.path.join(tmp, "SYSTEM.CFG")
        with open(cfg, "wb") as f:              # (tests/vidhdmake.py's bytes)
            f.write(b"O88CFG\0\0" + (3).to_bytes(2, "little") + b"DW" +
                    bytes([1, 2]) + (1 << HDD_CFGBIT).to_bytes(2, "little") +
                    b"\0\0")
        subprocess.run([sys.executable, "tools/os88fat.py", "add", sysimg,
                        cfg, "SYSTEM.CFG"], check=True, capture_output=True)
        with M.launch(sysimg, apps=os88build.at("build/apps360.img"),
                      machine=MACHINE, run_dir=run_dir) as m:
            ui = os88ui.UI(m)
            ui.ready(limit=300)
            w = ui.path("C:/MEDIA/OS8088.V88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = struct.unpack_from("<H", rec, geom.W_SEG)[0] << 4
            rb = lambda n: m.read(base + syms[n], 1)[0]
            rw = lambda n: struct.unpack_from("<H", m.read(base + syms[n],
                                                           2))[0]
            M.until(m, lambda mm: rb("vp_ok") == 1 or rw("vp_msg") ==
                    syms["vp_s_bad"], "the header", poll=0.3, limit=300.0,
                    guest=60.0)
            if rb("vp_ok") != 1:
                bad.append("2: the file did not open playable")
            else:
                print("   2: C:/MEDIA/OS8088.V88 opened %r, playable"
                      % w.title)
                m.key("Space")
                try:
                    M.until(m, lambda mm: rb("vp_sess") == 1 or
                            rw("vp_msg") == syms["vp_s_bad"], "a session",
                            poll=0.3, limit=300.0, guest=60.0)
                except M.MartyError:
                    pass
                if rw("vp_msg") == syms["vp_s_bad"]:
                    bad.append("3: Play said \"This .V88 is damaged\"")
                elif rb("vp_sess") != 1:
                    bad.append("3: Play started no session")
                else:
                    print("   3: Play started a session off the 32 KB "
                          "clusters")
                    r = vid.Reader("apps/video/os8088.v88", rb("vp_rend"))
                    blk = b"".join(r._recs) + r._seam
                    bseg = rw("vp_rblk") << 4
                    got = bytes(m.read(bseg, len(blk))) if bseg else b""
                    d = sum(1 for x, y in zip(got, blk) if x != y) + \
                        abs(len(got) - len(blk))
                    print("   4: rendition %d's block, %d bytes: %d differ "
                          "from the host's" % (rb("vp_rend"), len(blk), d))
                    if d or not bseg:
                        bad.append("4: the block in memory differs in %d "
                                   "bytes" % d)
                    try:
                        M.until(m, lambda mm: rw("vp_vseq") >= 20,
                                "twenty frames", poll=0.3, limit=300.0,
                                guest=60.0)
                        print("   5: %d frames drawn" % rw("vp_vseq"))
                    except M.MartyError:
                        bad.append("5: %d frames drawn, not twenty"
                                   % rw("vp_vseq"))
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
