#!/usr/bin/env python3
"""A STREAMED .V88 KEEPS ITS SEEK ON A VOLUME WITH 32 KB CLUSTERS - SPEC.md
18.4.4.2, 98.1.7.6.

    make && python3 tests/vidkeyclus.py

Reported off the owner's encode for a 286: one keyframe left out past the
61,440 bytes the player reads in one go, and the play answered every Left
and Right with "No seeking". The player checks the LARGEST key against one
64 KB read, and that read was the record's CLUSTERS at the worst offset -
OSAPI_FILE_READ_AT took whole clusters - so a 60,845-byte key needed 72 KB
on a 512 MB partition's 8 KB clusters, and 96 KB on a 2 GB one's 32 KB: the
whole table was dropped, the poster with it. kern_big's READ_AT takes whole
SECTORS now, the player asks for that at open ([vp_rgb] 512), and a key's
read is its record and less than a sector either side on any volume.

The fixture is bigvol.py's 321MB XT-IDE disk formatted on the HOST with
mtools at `-c 64`, as tests/vidbigclus.py's - and a VGA8 clip made here
whose keys are 39 KB: a span the old read refused at 32 KB clusters (96 KB)
and passed at 16 KB ones, so the row tests exactly the cluster.

1. THE FIXTURE IS WHAT IT CLAIMS: spc 64, and the clip's largest key past
   what a cluster-granular read takes there (os88venc.key_limit) - or the
   row tests nothing.
2. IT OPENS WITH ITS KEYS: [vp_rgb] is a sector, [vp_nkeys] the file's, and
   the poster is decoded ([vp_dkey]).
3. THE KEY'S BYTES ARE THE FILE'S: a Right in the full screen reads a key
   record at a byte offset inside a 32 KB cluster - stopped at vp_spos.kok,
   the record in memory is the file's, byte for byte. That is the kernel's
   READ_AT skipping the cluster's head (dsk_read_chain's first run), and the
   player's read from the sector under it.
4. THE SEEK LANDS: the play goes on from that key's frame.

Broken on purpose - the probe at vp_open taken out, [vp_rgb] left the
cluster - 2 FAILS with no keys, and a Right's toast says "Seek: keys too
big" (the reason is asserted there, the one place it is reachable); the first run's `add al, [dsk_csk]` taken
out of dsk_read_chain - 3 FAILS, the record being the cluster's head.

REQUIRES mtools.
"""
import os
import random
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
import os88venc as venc                                      # noqa: E402
import instdeep as ID                                        # noqa: E402
from bigvol import BASE, SECTOR                              # noqa: E402
from cycweb import pkg_syms                                  # noqa: E402
from vidbigclus import stage                                 # noqa: E402

MACHINE = "os8088_xt_vga_hdd"
HDD_CFGBIT = 1                              # kernel/driver.inc's drv_cfgbit
W, H, NF, FPS, KEYSECS = 320, 200, 150, 15, 2
KE_OFF, KE_LEN = 4, 8                       # apps/video/video.asm's entry


def clip(path):
    """VGA8, silent, streamed: noise that grows 16 rows a frame to 120 (so
    no frame's record passes a super-packet) under a band that slides - a
    key is the whole picture, ~39 KB"""
    rnd = random.Random(7)
    noise = bytes(rnd.randrange(256) for _ in range(W * H))
    cvs = []
    for f in range(NF):
        rows = min(120, (f + 1) * 16)
        cv = bytearray(noise[:rows * W]) + bytearray([3]) * ((H - rows) * W)
        for y in range(120, H):
            for x in range((f * 4) % W, min(W, (f * 4) % W + 24)):
                cv[y * W + x] = (f * 9 + y) & 255
        cvs.append(bytes(cv))
    pal = bytes((i * 3) % 64 for i in range(768))
    vid.encode_canvases(cvs, vid.Geom(vid.LAY_LIN320, W, H), path, FPS,
                        vid.PF_VGA8, pal, "clus", keysecs=KEYSECS)
    vid.verify_v88(path)


def main():
    os.chdir(ROOT)
    if not os.path.exists("build/martypc/run/martypc_headless"):
        sys.exit("no MartyPC - `make marty` first")
    if not shutil.which("mformat"):
        print("   SKIP: no mtools")
        return 0
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    bad = []
    with tempfile.TemporaryDirectory(dir=os88build.at("build")) as tmp:
        v88 = os.path.join(tmp, "CLUS.V88")
        clip(v88)
        r = vid.Reader(v88)
        fdat = open(v88, "rb").read()
        ents = [fdat[r.ktab + 16 * i:r.ktab + 16 * i + 16]
                for i in range(r.nkeys)]
        keyf = [struct.unpack_from("<I", e, 0)[0] for e in ents]
        run_dir = M.stage_run_dir("vidkeyclus")
        vhd = os.path.join(run_dir, ID.VHD_REL)
        stage(vhd, v88)
        with open(vhd, "rb") as f:
            f.seek(BASE * SECTOR)
            bpb = f.read(SECTOR)
        spc = bpb[13]
        lim = venc.key_limit(spc * SECTOR)
        print("   1: %d sectors a cluster; %d keys, the largest %d bytes "
              "against a cluster read's %d" % (spc, r.nkeys, r.kmax, lim))
        if spc != 64 or r.kmax <= lim or r.nkeys < 3:
            print("   FAIL: the fixture does not need the sector read - the "
                  "row tests nothing")
            return 1
        sysimg = os.path.join(tmp, "SYS.IMG")
        shutil.copyfile(os88build.at("build/os8088-360.img"), sysimg)
        cfg = os.path.join(tmp, "SYSTEM.CFG")      # (tests/vidhdmake.py's)
        with open(cfg, "wb") as f:
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
            pseg = struct.unpack_from("<H", rec, geom.W_SEG)[0]
            base = pseg << 4
            rb = lambda n: m.read(base + syms[n], 1)[0]
            rw = lambda n: struct.unpack_from("<H", m.read(base + syms[n],
                                                           2))[0]
            ww = lambda n, v: m.write(base + syms[n], struct.pack("<H", v))
            wait = lambda c, what, g=60.0: M.until(m, c, what, poll=0.3,
                                                   limit=600.0, guest=g)
            try:
                wait(lambda mm: rb("vp_loaded") == 1, "the header")
            except M.MartyError:
                print("   FAIL: 2: the file never opened - its reads came "
                      "back wrong")
                return 1
            try:                        # (the poster follows the header)
                wait(lambda mm: rw("vp_dkey") != 0xFFFF, "the poster", 20.0)
            except M.MartyError:
                pass
            print("   2: granule %d, %d keys of %d, poster key %d (%04X)"
                  % (rw("vp_rgb"), rw("vp_nkeys"), r.nkeys, rw("vp_dkey"),
                     rw("vp_pseg")))
            if rw("vp_rgb") != SECTOR:
                bad.append("2: the read's granule is %d, not a sector"
                           % rw("vp_rgb"))
            if rw("vp_nkeys") != r.nkeys:
                bad.append("2: %d keys, not the file's %d - no seek"
                           % (rw("vp_nkeys"), r.nkeys))
                # ...and a Right says WHY (98.3.14): the file has keys, past
                # one read here - not "no keyframes"
                m.write(base + syms["vp_nowin"], b"\1")
                m.type_text("p")
                wait(lambda mm: rb("vp_ready") == 1, "the play", 180.0)
                m.key("ArrowRight")
                try:
                    wait(lambda mm: rb("vo_kind") in (5, 11) and rb("vo_n"),
                         "the toast", 30.0)
                    n = rb("vo_n")
                    t = bytes(m.read(base + syms["vo_text"], n))
                    print("   2: a Right says %r" % t.decode("latin-1"))
                    if t != b"Seek: keys too big":
                        bad.append("2: the toast says %r, not why" % t)
                except M.MartyError:
                    bad.append("2: a Right showed no toast")
            elif rw("vp_dkey") != r.poster or not rw("vp_pseg"):
                bad.append("2: no poster decoded")
            if not bad:
                m.write(base + syms["vp_nowin"], b"\1")
                m.type_text("p")
                wait(lambda mm: rb("vp_ready") == 1 and rw("vp_done") >= 20,
                     "the play past frame 20", 180.0)
                m.bp_exec((pseg << 4) + syms["vp_spos.kok"])
                m.key("ArrowRight")
                st = m.wait_stop(limit=300.0, guest=60.0)
                if st is None:
                    bad.append("3: the seek read no key")
                else:
                    regs = m.regs()
                    kl = rw("vp_kload")
                    ent = ents[kl] if kl < len(ents) else None
                    rseg = rw("vp_rdseg")
                    if ent is None:
                        bad.append("3: [vp_kload] %d is not a key" % kl)
                    else:
                        off = struct.unpack_from("<I", ent, KE_OFF)[0]
                        n = struct.unpack_from("<H", ent, KE_LEN)[0]
                        got = bytes(m.read((rseg << 4) + regs["si"], n))
                        d = sum(1 for x, y in zip(got, fdat[off:off + n])
                                if x != y)
                        print("   3: key %d, %d bytes at file offset %d (%d "
                              "into its 32 KB cluster): %d differ"
                              % (kl, n, off, off % (spc * SECTOR), d))
                        if d:
                            bad.append("3: key %d's record differs from "
                                       "the file in %d bytes" % (kl, d))
                    m.bp_exec()
                    m.run()
                    try:
                        wait(lambda mm: rw("vp_base") == keyf[kl] + 1 and
                             rb("vp_ready") == 1, "the play from the key")
                        print("   4: the play went on from frame %d, key "
                              "%d's" % (rw("vp_base") - 1, kl))
                    except M.MartyError:
                        bad.append("4: the seek did not land on key %d "
                                   "(vp_base %d)" % (kl, rw("vp_base")))
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
