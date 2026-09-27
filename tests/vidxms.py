#!/usr/bin/env python3
"""VIDEO.O88 holds a streamed .V88 in XMS and plays it from there - SPEC.md
98.3.18.

    make && python3 tests/vidxms.py [--arm xms|idle|nox|live|liverep|livenox]

WHY QEMU: docs/TESTING.md's closed list, entry 1. XMS is a 286-and-up store
and every MartyPC machine is an 8088, which has nothing above 1MB - there is
no "prefer MartyPC" to weigh. Nothing here is timed; every wait is on the
guest's own tick count (tests/os88qemu.py).

THE INSTRUMENT IS A DISK SWAP. Once the file is held, drive B: is changed to a
BLANK floppy under the running player, so any read the player still sends to
the disk has nothing to find. A key and a whole play that still work are
reads the hold answered - no counter the player keeps about itself is taken
on trust.

ARM xms (the default; QEMU's own 128MB):

  1. the file opens with a hold its size, and the window's timer starts
     filling it;
  2. the moment the first chunk has arrived, Play: the stream's first chunk
     comes out of the hold and the rest off the disk, each one copied up
     behind the hold as it arrives (vp_xput) - so the hold is WHOLE as the
     play returns, well before the idle loader could have got there (it
     would want a tick a chunk, and this clip is ~30 of them);
  3. the hold is byte-for-byte the file (QEMU's pmemsave of the block);
  4. B: goes blank; Right arrow loads key 1 (vp_rdat, out of the hold), and
     a play from it runs to the last frame with no error.

ARM idle is arm xms with no play at step 2: the window's timer alone loads
the file to its end, and a short last chunk is what says it has.

ARM live is 98.3.18.1, LIVE FROM THE HOLD: a 1.2 MB STREAMED Live file for
the VGA desktop, held whole, B: made blank, and Play must be a live session
- the worker decoding, the UI task filling its ring out of the hold on its
asks. At four moments the VM is stopped with the gfx lock free and the
worker's shadow compared with the reference decode, and all 450 frames must
be drawn. ARM liverep is the same file repeating from frame 10, over a lap
and a half. ARM livenox is `-m 1`: Play must NOT be Live. Broken on purpose
- `call vp_lask` out of the worker - the play stalls at frame 96.

ARM nox (`-m 1`, no memory above 1MB): the NEGATIVE CONTROL, and the
fallback. No hold is taken, the play runs off the disk to the last frame -
and after the same swap the same Right arrow FAILS. That is what says the
swap bites, so that arm xms passing means something.

VERIFIED TO FAIL: with `call vp_xput` taken out of vp_fill, arm xms reports
the hold short when the play returns; with `call vp_xrdat` taken out of
vp_rdat, step 4's key is refused.
"""
import argparse
import atexit
import signal
import time
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88qemu                                             # noqa: E402
import os88build                                            # noqa: E402
import os88geom as geom                                     # noqa: E402
import os88ui                                               # noqa: E402
import os88vid as vid                                       # noqa: E402
import xmcheck                                              # noqa: E402
from cycweb import pkg_syms                                 # noqa: E402

NF = 300
FPS = 30.0


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def u32(b, i=0):
    return struct.unpack_from("<I", b, i)[0]


def clip(tmp):
    """300 frames of 640x200 with ~2.5 KB of change each and a key every
    couple of seconds: most of a megabyte, ~30 of the loader's chunks."""
    import random
    rnd = random.Random(4088)
    wb, h = 80, 200
    cv = bytearray(wb * h)
    paths = []
    for f in range(NF):
        for _ in range(320):
            a = rnd.randrange(wb * h - 8)
            cv[a:a + 8] = bytes(rnd.getrandbits(8) for _ in range(8))
        p = os.path.join(tmp, "f%03d.pbm" % f)
        vid._write_pbm(p, wb, h, bytes(cv))
        paths.append(p)
    out = os.path.join(tmp, "CLIP.V88")
    vid.encode_frames(paths, out, FPS, None, "cga", "vidxms clip")
    vid.verify_v88(out)
    return out


LNF, LWB, LH = 450, 20, 120


def live_clip(tmp, loop=None):
    """A STREAMED Live file (98.3.18.1) for the VGA desktop: 160 x 120, one
    bit, every byte new every frame - ~1.1 MB, four times the biggest ring,
    so the play cannot be read whole at its start and the worker's asks are
    what carry it to the end"""
    import random
    rnd = random.Random(1188)
    paths = []
    for f in range(LNF):
        cv = bytes(rnd.getrandbits(8) for _ in range(LWB * LH))
        p = os.path.join(tmp, "l%03d.pbm" % f)
        vid._write_pbm(p, LWB, LH, cv)
        paths.append(p)
    out = os.path.join(tmp, "CLIP.V88")
    vid.encode_frames(paths, out, FPS, None, "lin80", "vidxms live",
                      live="vga", loop=loop, repeat=loop is not None)
    vid.verify_v88(out)
    return out


class Q(object):
    """One private QEMU: its own socket, pidfile and copies of both disks."""

    def __init__(self, tmp, sysimg, apps, mem):
        self.sock = os.path.join(tmp, "qmp.sock")
        self.pid = os.path.join(tmp, "qemu.pid")
        self.pidn = None
        cmd = ["qemu-system-i386", "-machine", "pc,vmport=off",
               "-m", str(mem),
               "-drive", "file=%s,format=raw,if=floppy" % sysimg,
               "-boot", "a", "-chardev", "msmouse,id=m0",
               "-serial", "chardev:m0",
               "-drive", "file=%s,format=raw,if=floppy,index=1" % apps,
               "-display", "none",
               "-qmp", "unix:%s,server,nowait" % self.sock,
               "-daemonize", "-pidfile", self.pid]
        # KILLED BY THE PID IT WROTE, not by os88qemu.own(pidfile): the
        # pidfile is in `tmp`, which a failing run deletes on its way out
        # before atexit runs - and a teardown that cannot find the pidfile
        # leaves the emulator running (three were, the first day)
        atexit.register(self.close)
        signal.signal(signal.SIGTERM, lambda n, f: sys.exit(128 + n))
        subprocess.run(cmd, check=True)
        self.pidn = int(open(self.pid).read().strip())

    def close(self):
        if self.pidn:
            try:
                os.kill(self.pidn, signal.SIGTERM)
                os88qemu.gone(self.pidn)
            except OSError:
                pass
            self.pidn = None

    def hmp(self, *cmds):
        return xmcheck.qmp(self.sock, *cmds)

    def read(self, linear, n):
        return bytes(xmcheck.read_bytes(self.sock, linear, n))

    def readseg(self, seg, off, n):
        return self.read((seg << 4) + off, n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", choices=("xms", "idle", "nox", "live",
                                      "livenox", "liverep"), default="xms")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    sysimg0 = os88build.at("build/os8088.img")
    for p in (pkg, sysimg0):
        if not os.path.exists(p):
            sys.exit("vidxms: no %s - run `make`" % p)
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        islive = a.arm in ("live", "livenox", "liverep")
        v88 = live_clip(tmp, 10 if a.arm == "liverep" else None) \
            if islive else clip(tmp)
        data = open(v88, "rb").read()
        size = len(data)
        rd88 = vid.Reader(v88)
        nkeys = len(rd88.keys)
        print("   clip: %d frames, %d bytes, %d keys"
              % (rd88.frames, size, nkeys))
        disk = os.path.join(tmp, "vidxms.img")
        blank = os.path.join(tmp, "blank.img")
        sysimg = os.path.join(tmp, "sys.img")
        shutil.copy(sysimg0, sysimg)
        for out, files in ((disk, [v88, pkg]), (blank, [])):
            subprocess.run([sys.executable, "tools/os88disk.py", "-o", out,
                            "--size", "1440"] + files, check=True,
                           capture_output=True)
        q = Q(tmp, sysimg, disk, 1 if a.arm in ("nox", "livenox") else 128)
        for _ in range(150):
            if os.path.exists(q.sock):
                break
            time.sleep(0.2)
        xmcheck.wait_desktop(q.sock, "vidxms")
        S = xmcheck.sym
        ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)

        def wait(cond, what, secs):
            if not os88qemu.acted(q, cond, secs=secs, what=what, poll=0.05):
                raise SystemExit("vidxms: timed out waiting for %s" % what)

        # --- open B: and double-click the clip
        x, y = geom.drive_pt(q, "B", S)
        xmcheck.dblclick(q.sock, x, y)
        box = {}

        def diskwin():
            for w in ui.windows():
                if w.visible and ui.fs_of(w) == (1, 0, 1):
                    box["w"] = w
                    return True
            return False
        wait(diskwin, "drive B's window", 30)
        os88qemu.pace(q, 1)
        w = box["w"]
        i, _ = ui.entry("CLIP.V88", w)
        before = set(o.i for o in ui.windows())
        xmcheck.dblclick(q.sock, *ui.row_xy(w, i - ui.scroll(w)))

        def player():
            for o in ui.windows():
                if o.i not in before and o.visible:
                    box["p"] = o
                    return True
            return False
        wait(player, "the Video Player's window", 30)
        rec = q.read(S("wm_wins") + box["p"].i * geom.WIN_SIZE, geom.WIN_SIZE)
        base = u16(rec, geom.W_SEG) << 4

        def rb(n):
            return q.read(base + syms[n], 1)[0]

        def rw(n):
            return u16(q.read(base + syms[n], 2))

        def rd(n):
            return u32(q.read(base + syms[n], 4))

        def state():
            return ("done=%d err=%d ready=%d played=%d sel=%d kload=%d "
                    "xon=%d xfull=%d xhave=%d"
                    % (rw("vp_done"), rb("vp_err"), rb("vp_ready"),
                       rb("vp_played"), rw("vp_sel"), rw("vp_kload"),
                       rb("vp_xon"), rb("vp_xfull"), rd("vp_xhave")))

        if not os88qemu.acted(q, lambda: rb("vp_loaded") == 1, secs=30,
                              what="the clip's header", poll=0.05):
            subprocess.run([sys.executable, "tools/shot.py", q.sock,
                            os.path.join(ROOT, "build", "vidxms-fail.png")],
                           capture_output=True)
            msg = q.read(base + rw("vp_msg"), 40).split(b"\0")[0]
            sys.exit("vidxms: the clip did not open: %r (ok=%d)"
                     % (msg.decode("ascii", "replace"), rb("vp_ok")))
        if rb("vp_ok") != 1:
            sys.exit("vidxms: the player will not play the clip here")

        def play(what):
            q.hmp("sendkey p")
            wait(lambda: rb("vp_ready") == 1, what + " to start", 30)
            wait(lambda: rb("vp_ready") == 0 and rb("vp_played") == 1,
                 what + " to end", 60)

        def key1():
            """Right arrow: the next key, out of whatever still answers. A
            key that cannot be read leaves [vp_sel] where it was and says
            so (vp_seekto's .fail)"""
            s0 = rw("vp_sel")
            q.hmp("sendkey right")
            ok = os88qemu.acted(q, lambda: rw("vp_sel") != s0, secs=10,
                                what="the next key", poll=0.05)
            return ok and rw("vp_kload") == rw("vp_sel")

        if a.arm == "livenox":
            # --- no pool: the streamed Live file plays in the window
            q.hmp("sendkey p")
            wait(lambda: rb("vp_ready") == 1, "the play to start", 30)
            print("   no XMS, Play: lsess=%d sess=%d xon=%d"
                  % (rb("vp_lsess"), rb("vp_sess"), rb("vp_xon")))
            if rb("vp_lsess"):
                bad.append("a streamed Live file played LIVE with no hold - "
                           "the worker would have read the disk")
            q.hmp("sendkey esc")
        elif a.arm in ("live", "liverep"):
            # --- the hold whole, B: blank, and Play is LIVE
            if rb("vp_xon") != 1:
                sys.exit("vidxms: no hold was taken: %s" % state())
            wait(lambda: rb("vp_xfull") == 1, "the loader to finish", 120)
            q.hmp("change floppy1 %s raw" % blank)
            os88qemu.pace(q, 1)
            q.hmp("sendkey p")
            wait(lambda: rb("vp_ready") == 1, "the play to start", 30)
            if rb("vp_lsess") != 1:
                sys.exit("vidxms: Play was not LIVE (%s lsess=%d)"
                         % (state(), rb("vp_lsess")))
            lock = S("gfx_lock_flag")
            nf = rd88.frames
            checked = 0
            laps = a.arm == "liverep"
            for n in range(7 if laps else 4):
                os88qemu.pace(q, 3)
                for _ in range(40):         # a moment with no frame half
                    q.hmp("stop")           # decoded: the lock free
                    if q.read(lock, 1)[0] == 0:
                        break
                    q.hmp("cont")
                    time.sleep(0.01)
                done = rw("vp_done")
                shseg = rw("vp_shseg")
                if rb("vp_lsess") == 1 and done:
                    sh = q.read(shseg << 4, LH * 80)
                    got = b"".join(sh[y * 80:y * 80 + LWB] for y in range(LH))
                    want = vid.decode_at(rd88, done - 1)
                    diff = sum(1 for j in range(len(got))
                               if got[j] != want[j])
                    print("   live, frame %d: the shadow against the decode, "
                          "%d bytes of %d differ; ring lc=%d pc=%d k=%d "
                          "stalls %d" % (done, diff, len(got), rw("vp_lc"),
                                         rw("vp_pc"), rw("vp_k"),
                                         rw("vp_stall")))
                    checked += 1
                    if diff:
                        bad.append("the Live shadow after frame %d differs "
                                   "in %d bytes" % (done, diff))
                q.hmp("cont")
            if laps:
                # REPEAT (98.3.9): the file's end asks for the seam and the
                # next lap's start, and the play goes round - it never ends
                seq = rw("vp_vseq")
                print("   repeating: %d frames drawn, %d to a lap; stalls %d"
                      % (seq, nf, rw("vp_stall")))
                if seq <= nf + 60 or rb("vp_err") or not rb("vp_lsess"):
                    bad.append("the repeating Live play drew %d frames with a "
                               "lap of %d (error %d, live %d)"
                               % (seq, nf, rb("vp_err"), rb("vp_lsess")))
                q.hmp("sendkey esc")
                wait(lambda: rb("vp_lsess") == 0, "Esc to stop it", 20)
                q.close()
                return report(bad, a.arm)
            wait(lambda: rb("vp_lsess") == 0 or rw("vp_done") >= nf,
                 "the Live play to end", 90)
            print("   live play: done=%d of %d err=%d stalls %d lend=%d"
                  % (rw("vp_done"), nf, rb("vp_err"), rw("vp_stall"),
                     rb("vp_lend")))
            if rw("vp_done") != nf or rb("vp_err"):
                bad.append("the Live play off the hold drew %d of %d "
                           "(error %d)" % (rw("vp_done"), nf, rb("vp_err")))
            if checked < 2:
                bad.append("only %d samples of the shadow were taken" % checked)
        elif a.arm == "nox":
            # --- the fallback, then the control
            if rb("vp_xon"):
                bad.append("a machine with no XMS took a hold")
            play("the play off the disk")
            print("   no XMS: %s" % state())
            if rw("vp_done") != NF or rb("vp_err"):
                bad.append("the play off the disk drew %d of %d (error %d)"
                           % (rw("vp_done"), NF, rb("vp_err")))
            q.hmp("change floppy1 %s raw" % blank)
            os88qemu.pace(q, 1)
            if key1():
                bad.append("with B: blank and no hold, the next key STILL loaded - "
                           "the swap does not bite, so arm xms proves "
                           "nothing")
            else:
                print("   B: blank, no hold: the next key refused, as it must be")
        else:
            # --- 1: the hold, and its first chunk
            cap = rd("vp_xcap")
            print("   the hold: on=%d, %d bytes for a file of %d"
                  % (rb("vp_xon"), cap, size))
            if rb("vp_xon") != 1:
                sys.exit("vidxms: no hold was taken: %s" % state())
            if not (size < cap <= size + 1024 + 1):
                bad.append("the hold is %d bytes for a file of %d" % (cap,
                                                                       size))
            wait(lambda: rd("vp_xhave") >= 32768, "the loader's first chunk",
                 30)
            if a.arm == "idle":
                # --- 2: the loader alone, to the file's end
                q.hmp("sendkey i")          # the card out: its line 6
                os88qemu.pace(q, 1)         # counts the hold, from the timer
                shot = os.path.join(ROOT, "build", "vidxms-loading.png")
                subprocess.run([sys.executable, "tools/shot.py", q.sock,
                                shot], capture_output=True)
                print("   loading: %s  (the card: %s)" % (state(), shot))
                wait(lambda: rb("vp_xfull") == 1, "the loader to finish", 120)
                print("   loaded: %s" % state())
                if rd("vp_xhave") != size:
                    bad.append("the loader ended at %d of %d"
                               % (rd("vp_xhave"), size))
            else:
                # --- 2: play at once; the stream fills the rest behind it
                h0 = rd("vp_xhave")
                play("the first play")
                h1, f1 = rd("vp_xhave"), rb("vp_xfull")
                print("   first play: %s  (the hold had %d at Play)"
                      % (state(), h0))
                if rw("vp_done") != NF or rb("vp_err"):
                    bad.append("the first play drew %d of %d (error %d)"
                               % (rw("vp_done"), NF, rb("vp_err")))
                if not f1 or h1 != size:
                    bad.append("the play returned with the hold at %d of %d "
                               "(full %d): the stream did not fill it behind "
                               "itself" % (h1, size, f1))
                wait(lambda: rb("vp_xfull") == 1, "the hold to be whole", 60)
            # --- 3: the hold is the file
            dump = os.path.join(tmp, "hold.bin")
            q.hmp('pmemsave %d %d "%s"' % (rd("vp_xbase"), size, dump))
            got = open(dump, "rb").read()
            diff = sum(1 for j in range(size) if got[j] != data[j])
            print("   the hold against the file: %d bytes of %d differ"
                  % (diff, size))
            if diff:
                bad.append("the hold differs from the file in %d bytes" % diff)
            # --- 4: B: blank; a key and a play, out of the hold
            q.hmp("change floppy1 %s raw" % blank)
            os88qemu.pace(q, 1)
            if not key1():
                bad.append("B: blank, the hold whole: the next key was refused "
                           "(%s)" % state())
            else:
                print("   B: blank: key %d loaded out of the hold" % rw("vp_sel"))
                play("the play from that key")
                print("   second play: %s" % state())
                if rw("vp_done") != NF or rb("vp_err"):
                    bad.append("B: blank, the play from the key drew %d of %d "
                               "(error %d)" % (rw("vp_done"), NF,
                                               rb("vp_err")))
            if not rb("vp_xon"):
                bad.append("the hold was dropped")
        q.close()
    return report(bad, a.arm)


def report(bad, arm):
    if bad:
        print("\nvidxms (%s): FAIL" % arm)
        for b in bad:
            print("  - " + b)
        return 1
    print("\nvidxms (%s): ok" % arm)
    return 0


if __name__ == "__main__":
    sys.exit(main())
