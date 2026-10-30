#!/usr/bin/env python3
"""xtmp3eng - XTMP3's ENGINE over the stub backend (SPEC.md 121.5-121.7,
docs/xtmp3/ENGINE.md).

    make && make xtmp3stub && python3 tests/xtmp3eng.py [--leg NAME ...]

QEMU BY NAME: behaviour, not speed. The package is the X3STUB=1 build
(build/xtmp3s.o88, named XTMP3.O88 on its disk): the real front end
(x3pars.inc and the FRONT part) over real MP3 frames, and the STUB backend
(the parts table's row 4), which makes a 440.8 Hz square a granule
and costs, per rung, the speed X3STUB.CFG gives it - spun on the 8253, so a
rung configured at 0.80x calibrates under 1.25 on any host. Each leg boots
its own private QEMU (pidfile, its own socket) with its own disks, reads
XTMP3's state out of its bss by its own map (checked to be the bytes of
build/xtmp3s.bin), and checks the disks on the host afterwards (mtools).

xtpeak runs the SHIPPED package (build/xtmp3.o88, the real parts) instead.

Legs:
  ladder    THE LADDER'S TRANSITIONS, on QEMU's SB16 (the deterministic
            capability fixtures of the brief, section 14): a capable machine
            stays on Original; a middling one takes Half, a slower one
            Quarter - each before any XT rung, Original and Half never
            probed where Quarter fails; an XT goes to Decode first, its
            calibration bounded in guest time; a hopeless machine is told in
            under a second a rung
  slowdown  live Original whose rung turns slow mid-track: at 1.05x two slow
            windows step it down ONCE, to Half, with no underrun; at 0.60x
            one step with at most one underrun; the position kept, no step
            back up
  conv      no card: the double-clicked MP3 is probed, the drive dialog lists
            C: and D:, the Save dialog stands on C:, the conversion writes a
            WAV whose every field is checked on the host (RIFF, fmt, the xtm3
            identity against zlib's CRC-32s, JUNK, data on the cluster, the
            frame count = frames x 576 - 529), no .TMP is left, XTMP3.DAT is
            written, the finished file plays (the speaker bracket), and a
            second Play reuses it - no probe, no granule, no decoder resident
  cancel    a slow conversion, Esc: answered in under a second of guest
            time, the temporary file gone, nothing written, READY
  full      the disk fills mid-write (an estimated duration that is short):
            ERROR 'The disk is full', the temporary file deleted, nothing
            plays
  stale     a finished file made incomplete (its RIFF length) and another
            whose identity changed, on the host between boots: neither is
            reused - the dialog is asked again - and their records go
  novol     no hard drive: the dialog's sentence says so, Decode and Play
            is greyed (Enter does nothing), Esc leaves the track READY
  twovol    D: chosen: the Save dialog stands on D: and the WAV lands there;
            a track too long for D: greys it ('too little room')
  playwav   D on the SB16: the conversion, then the finished WAV through
            the card - its output the stub's tone - with no MP3 work (the
            granule count still, the backend part not resident)
  pause     live on the SB16: Pause closes the stream where it is and keeps
            the position, Resume goes on from it, Rewind seeks, no underrun
  xtpeak    SPEC.md 121.10 on the shipped package and the REAL XT part, no
            card: XT Standard by Q, Decode First, the conversion finishes and
            X3S_PEAK, the measured working set, is at most 400,000 bytes
"""
import argparse
import atexit
import os
import re
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
import os88geom as geom                                       # noqa: E402
import os88qemu                                               # noqa: E402
import xmcheck                                                # noqa: E402
import os88build                                               # noqa: E402

FIX = os.path.join(ROOT, "tests", "xtmp3")
S = xmcheck.sym

# x3face.inc / x3eng.inc / x3conv.inc
ST_IDLE, ST_CHECK, ST_CHOOSE, ST_DECODE, ST_READY, ST_LIVE, ST_FILE, \
    ST_PAUSED, ST_BUFFER, ST_DONE, ST_ERROR = range(11)
Q_AUTO, Q_ORIG, Q_HALF, Q_QUART, Q_XTSTD, Q_XTFAST = range(6)
MD_IDLE, MD_CHECK, MD_LIVE, MD_FILE, MD_SPK, MD_CHOOSE, MD_CONV, MD_STEP, \
    MD_LOAD = range(9)                  # (MD_LOAD: the hold, x3hold.inc)
QNAME = ["AUTO", "ORIGINAL", "HALF", "QUARTER", "XT STANDARD", "XT FAST",
         "XT TURBO", "XT SPEECH"]
DRIVE_Y = {"A": 44, "B": 104, "C": 164, "D": 224}   # the desktop zones (640x480)
ICON_X = 592
ROW_X, ROW_Y0, ROW_H = 170, 127, 16                 # a Disk window's rows
HD0_GEO, HD1_GEO = "40/16/63", "250/4/17"           # C: 19 MB, D: 8 MB

bad = []


def check(leg, what, ok, more=""):
    print("  %s %s%s" % ("ok  " if ok else "FAIL", what,
                          (" - " + more) if more else ""))
    if not ok:
        bad.append("%s: %s%s" % (leg, what, (" (" + more + ")") if more else ""))
    return ok


def cfg(speeds, slr=0, slaft=0, slspd=0, has=0, failr=0, undat=0, undn=0,
        qfail=0):
    """X3STUB.CFG (apps/xtmp3/x3stub.inc): a speed x 100 by rung, the
    mid-track slowdown, the KB the machine HAS (0: its own), a rung whose
    live start fails, and UNDN underruns counted from live granule UNDAT,
    four granules apart, and the queue's probe refused while live (qfail)"""
    sp = list(speeds) + [0] * (8 - len(speeds))
    return struct.pack("<8HBBHHHBBHBB", *sp, slr, 0, slaft, slspd, has,
                       failr, 0, undat, undn, qfail)


# -----------------------------------------------------------------------------
# the package's own symbols, by its map - and only for the bytes that run
# -----------------------------------------------------------------------------
_SYM = {True: {}, False: {}}


def pkgsym(name, stub=True):
    """STUB: the X3STUB build's map (build/xtmp3s.bin), else the shipped
    build's (build/xtmp3.bin) - the one with the real decoder parts"""
    syms = _SYM[stub]
    if not syms:
        d = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
        mp, b = os.path.join(d, "x3.map"), os.path.join(d, "x3.bin")
        subprocess.run(["nasm", "-f", "bin", "-w+error"]
                       + (["-DX3STUB"] if stub else []) +
                       ["-I", "apps/", "-I", "apps/xtmp3/", "-I", "apps/audio/",
                        "-I", "drivers/sound/", "-I", "build/",
                        "--before", "[map symbols %s]" % mp, "-o", b,
                        "apps/xtmp3/xtmp3.asm"], check=True)
        built = "build/xtmp3s.bin" if stub else "build/xtmp3.bin"
        same = open(b, "rb").read() == open(os88build.at(built), "rb").read()
        for line in open(mp):
            m = re.match(r"\s*([0-9A-F]+)\s+([0-9A-F]+)\s+(\w+)$", line)
            if m:
                syms[m.group(3)] = int(m.group(2), 16)
        shutil.rmtree(d, ignore_errors=True)
        if not same:
            sys.exit("xtmp3eng: %s is not this source's - make && make "
                     "xtmp3stub" % built)
    return syms[name]


# -----------------------------------------------------------------------------
# THE RIG: a private QEMU, its disks in a scratch folder under build/
# -----------------------------------------------------------------------------

# THE RIG RUNNING NOW, for `gpace`: every pause in this file was a host
# `time.sleep`, which hands a starved guest less of the machine; they are
# GUEST seconds now (os88qemu.pace), on whichever rig is up
_RIG = [None]

# THE RATE LEGS run on a FIXED-SPEED guest (os88qemu.icount, docs/plans/
# SOAK-PARALLEL.md 17.8). ladder, slowdown and rungfail calibrate the
# stub's rung on the 8253, and the four that assert NO UNDERRUN race the
# SB16's DMA - without -icount both clocks are the HOST's, so a starved
# guest was a slower machine (1.05x has five per cent of headroom; it
# stepped down twice at eight emulators on four cores). Under -icount the
# PIT, the card and the guest's work all count instructions, so the answer
# is the same at any load and the row shares the lane. The other legs are
# behaviour and keep QEMU's default clock. The CARD only counts
# instructions on `qemuvt` (os88build.qemuvt()), and mainstall is the leg
# that says so.
RATE_LEGS = ("ladder", "slowdown", "rungfail", "playwav", "pause",
             "wavdirs", "xtpeak", "mainstall")
_ICOUNT = [False]


def gpace(secs):
    r = _RIG[0]
    try:
        if r is not None and getattr(r, "pid", None):
            os88qemu.pace(r, secs)
            return
    except Exception:                                       # noqa: BLE001
        pass
    time.sleep(secs)

class Rig(object):
    def __init__(self, leg, sb=False, hd=(), floppy=(), xcfg=None, keep=None,
                 stub=True, extra=(), mach="pc,vmport=off", syscfg=None,
                 pkgdir=None, hdimg=None, boot="a"):
        """hd: up to two (geometry, files, folders, junk_bytes) - the first is
        C:, the second D:; floppy: B:'s files (with SYSTEM/APPDATA); xcfg:
        X3STUB.CFG's bytes, written beside XTMP3.O88 (B: when the package is
        on the floppy, else C:); keep: a previous Rig's folder to boot again
        with its disks as they are"""
        self.leg = leg
        self.stub = stub                # False: the SHIPPED package
        self.extra = list(extra)        # QEMU arguments of the leg's own
        self.mach = mach
        self.syscfg = syscfg            # SYSTEM.CFG's bytes (else the SB's)
        self.pkgdir = pkgdir            # C:'s folder for the package, else root
        self.hdimg = hdimg              # a prepared C: image, copied as it is
        self.boot = boot                # "c": C: is the boot volume (TESTHD)
        self.tmp = keep or tempfile.mkdtemp(dir=os.path.join(ROOT, "build"),
                                            prefix="x3e")
        self.sock = os.path.join(self.tmp, "q.sock")
        self.pidf = os.path.join(self.tmp, "q.pid")
        self.wav = os.path.join(self.tmp, "sb.wav")
        self.pid = None
        self.hds = []
        atexit.register(self.close)
        if keep is None:
            self.build(sb, hd, floppy, xcfg)
        else:
            self.hds = [os.path.join(self.tmp, n) for n in ("hd0.img", "hd1.img")
                        if os.path.exists(os.path.join(self.tmp, n))]
        self.launch(sb)

    def build(self, sb, hd, floppy, xcfg):
        t = self.tmp
        shutil.copy(os88build.at("build/os8088.img"), os.path.join(t, "sys.img"))
        if self.syscfg is not None:     # SYSTEM.CFG of the leg's own
            d = os.path.join(t, "cfg")
            os.makedirs(d)
            open(os.path.join(d, "SYSTEM.CFG"), "wb").write(self.syscfg)
            subprocess.run([sys.executable, "tools/os88fat.py", "add",
                            os.path.join(t, "sys.img"),
                            os.path.join(d, "SYSTEM.CFG")], check=True,
                           capture_output=True)
        elif sb:                        # SYSTEM.CFG wanting SOUND.DRV
            subprocess.run([sys.executable, "tools/os88fat.py", "add",
                            os.path.join(t, "sys.img"),
                            os88build.at("build/mixercfg/SYSTEM.CFG")], check=True,
                           capture_output=True)
        pkg = os.path.join(t, "XTMP3.O88")
        shutil.copy(os88build.at("build/xtmp3s.o88" if self.stub
                                 else "build/xtmp3.o88"), pkg)
        cfgf = os.path.join(t, "X3STUB.CFG")
        open(cfgf, "wb").write(xcfg or cfg([]))
        bfiles = list(floppy)
        if floppy:
            bfiles = [pkg, cfgf] + bfiles
        subprocess.run([sys.executable, "tools/os88disk.py", "-o",
                        os.path.join(t, "b.img"), "--size", "1440",
                        "--folder", "SYSTEM/APPDATA"] + bfiles, check=True,
                       capture_output=True)
        if self.hdimg:                  # (a disk someone handed us)
            img = os.path.join(t, "hd0.img")
            shutil.copy(self.hdimg, img)
            self.hds.append(img)
        for i, (geo, files, folders, junk) in enumerate(hd):
            img = os.path.join(t, "hd%d.img" % i)
            if isinstance(geo, dict):       # a FAT16 volume of a forced
                bigfat(img, **geo)          # cluster (a data disk: D:, ...)
                for f in files:
                    subprocess.run(["mcopy", "-i", part(img), f, "::"],
                                   check=True)
                self.hds.append(img)
                continue
            files = list(files)
            if i == 0 and not floppy:
                files = ([pkg, cfgf] if not self.pkgdir else
                         ["%s:%s" % (self.pkgdir, pkg),
                          "%s:%s" % (self.pkgdir, cfgf)]) + files
            if junk:
                jf = os.path.join(t, "JUNK.BIN")
                with open(jf, "wb") as f:
                    f.truncate(junk)
                files.append(jf)
            if files or folders:
                args = []
                for fo in folders:
                    args += ["--folder", fo]
                subprocess.run([sys.executable, "tools/os88disk.py", "--hdd",
                                "--geometry", geo, "--mbr", os88build.at("build/mbr.bin"),
                                "--boot", os88build.at("build/boothd.bin"), "--kernel",
                                os88build.at("build/kernel.sys"), "-o", img] + args + files,
                               check=True, capture_output=True)
            else:
                c, h, s = geo.split("/")
                subprocess.run([sys.executable, "tools/os88hdd.py", "--noboot",
                                "--raw", "--spt", s, "--heads", h, "--cyls", c,
                                "--out", img], check=True, capture_output=True)
            self.hds.append(img)

    def launch(self, sb):
        t = self.tmp
        os88qemu.own(self.pidf, self.sock)
        a = ["qemu-system-i386", "-machine", self.mach, "-m", "4",
             "-drive", "file=%s,format=raw,if=floppy" % os.path.join(t, "sys.img"),
             "-drive", "file=%s,format=raw,if=floppy,index=1" % os.path.join(t, "b.img")]
        for i, img in enumerate(self.hds):
            a += ["-drive", "file=%s,format=raw,if=ide,index=%d,media=disk" % (img, i)]
        if sb:
            a += ["-device", "sb16,audiodev=a0", "-audiodev",
                  "wav,id=a0,path=%s" % self.wav]
        a += self.extra
        if _ICOUNT[0]:
            a += os88qemu.icount()
        a += ["-boot", self.boot, "-chardev", "msmouse,id=m0", "-serial",
              "chardev:m0", "-display", "none", "-qmp",
              "unix:%s,server,nowait" % self.sock, "-daemonize",
              "-pidfile", self.pidf]
        os88qemu.start(a, check=True)
        self.pid = int(open(self.pidf).read().strip())
        _RIG[0] = self                  # gpace paces on the rig that is up
        os88qemu.acted(self, lambda: os.path.exists(self.sock), secs=30,
                       what="the QMP socket")
        xmcheck.wait_desktop(self.sock, "xtmp3eng")

    def close(self):
        if self.pid:
            try:
                xmcheck.qmp(self.sock, "quit")
            except BaseException:                           # noqa: BLE001
                pass
            try:
                os.kill(self.pid, signal.SIGTERM)
            except OSError:
                pass
            os88qemu.gone(self.pid)
            self.pid = None

    def done(self):
        self.close()
        shutil.rmtree(self.tmp, ignore_errors=True)

    # --- the guest ------------------------------------------------------------
    def read(self, linear, n):
        return bytes(xmcheck.read_bytes(self.sock, linear, n))

    def hmp(self, *c):
        return xmcheck.qmp(self.sock, *c)

    def override_to(self, want, limit=16):
        """Q until the quality override READS `want` (x3_st's third byte),
        each press waited for: a count of presses, slept between, left the
        override wherever a press the app took late put it, and the Decode
        First that followed asked for another rung or nothing at all
        ('state 3 md 0' in the soak)"""
        for _ in range(limit):
            was = self.rd("x3_st", 3)[2]
            if was == want:
                return True
            self.key("q")
            self.until(lambda: self.rd("x3_st", 3)[2] != was,
                       "the override to step", 5, 0.05)
        return self.rd("x3_st", 3)[2] == want

    def key(self, k):
        os88qemu.key(self, k)       # the press, then the release once the ROM
                                    # stored it: sendkey's pair can arrive
                                    # together and lose the press (os88qemu.key)

    def ticks(self):
        return struct.unpack("<H", self.read(0x46C, 2))[0]

    def x3seg(self):
        n = S("wm_wins")
        for i in range(geom.MAX_WIN):
            rec = self.read(n + i * geom.WIN_SIZE, geom.WIN_SIZE)
            seg = struct.unpack_from("<H", rec, geom.W_SEG)[0]
            if seg and struct.unpack_from("<H", rec, geom.W_FLAGS)[0] & 2:
                try:
                    hdr = self.read(seg << 4, 32)
                except Exception:                           # noqa: BLE001
                    continue
                if hdr[16:21] == b"XTMP3":
                    return seg
        return None

    def rd(self, name, n=1):
        seg = self.seg or self.x3seg()
        return self.read((seg << 4) + pkgsym(name, self.stub), n)

    def w(self, name):
        return struct.unpack("<H", self.rd(name, 2))[0]

    def b(self, name):
        return self.rd(name, 1)[0]

    def st(self):
        b = self.rd("x3_st", 68)
        return dict(state=b[0], qual=b[1], flags=b[3],
                    elap=struct.unpack_from("<H", b, 4)[0],
                    total=struct.unpack_from("<H", b, 6)[0],
                    rate=struct.unpack_from("<H", b, 8)[0], ch=b[10],
                    bits=b[11], pct=b[15],
                    under=struct.unpack_from("<H", b, 18)[0],
                    peak=struct.unpack_from("<H", b, 20)[0],
                    err=struct.unpack_from("<H", b, 22)[0],
                    cur=b[25], md=self.b("x3_md"))

    def errtext(self):
        p = self.st()["err"]
        if not p:
            return ""
        return self.rd_at(p, 40).split(b"\0")[0].decode("latin-1")

    def rd_at(self, off, n):
        return self.read((self.seg << 4) + off, n)

    def meas(self):
        return list(struct.unpack("<8H", self.rd("x3l_meas", 16)))

    def until(self, cond, what, secs=60.0, poll=0.25):
        """`cond()` within `secs` of the GUEST's clock (os88qemu's ticks).

        It was `time.time()`, so a loaded box gave the guest less of every
        budget, and at eight emulators on four cores `XTMP3's window` stopped
        arriving inside its 20 host seconds (docs/plans/SOAK-PARALLEL.md
        17.5). A guest whose clock stops still ends the wait, by the host."""
        c = os88qemu.Clock(self)
        lim = os88qemu.to_ticks(secs)
        while True:
            try:
                if cond():
                    return True
            except Exception:                               # noqa: BLE001
                pass
            try:
                if c.step() >= lim or c.stalled():
                    break
            except Exception:                               # noqa: BLE001
                pass
            time.sleep(poll)
        # WHICH of the two ended it, and what the player was doing: a guest
        # whose budget was SPENT and one whose clock STOPPED want different
        # fixes, and "under 0" alone said neither (xtmp3rate's pause leg in
        # a soak: "the end" never came, and nothing could say why)
        try:
            why = ("its clock stopped for %ds of host time" % os88qemu.STALL
                   if c.stalled() else "%.1f guest s spent" % (c.n / os88qemu.TICK_HZ))
        except Exception:                                   # noqa: BLE001
            why = "?"
        try:
            s = self.st()
            now = "state %d md %d elap %d of %d under %d '%s'" % (
                s["state"], s["md"], s["elap"], s["total"], s["under"],
                self.errtext())
        except Exception as e:                              # noqa: BLE001
            now = "unreadable (%s)" % e
        print("     (timed out waiting for %s: %s; %s)" % (what, why, now))
        return False

    def open_file(self, drive, name, listing):
        """double-click drive's zone, then NAME's row in its Disk window:
        LISTING is the window's visible order (folders first, then names)"""
        os88qemu.mouse_dblclick(self, S, self.hmp, ICON_X, DRIVE_Y[drive])
        # the Disk window OPENED, not 2.5 s passed: a starved guest was still
        # reading the directory when the row's double-click arrived, and it
        # landed on the desktop (docs/plans/SOAK-PARALLEL.md 17.5). The open
        # runs in ui_task's handler, so the UI going idle is the window there
        os88qemu.ui_done(self, S, cap=30.0, what="the drive's window")
        gpace(0.5)
        y = ROW_Y0 + ROW_H * listing.index(name)
        for _ in range(3):
            os88qemu.mouse_dblclick(self, S, self.hmp, ROW_X, y)
            if self.until(lambda: self.x3seg() is not None, "XTMP3's window",
                          20):
                self.seg = self.x3seg()
                return True
        return False

    seg = None

    def menu_pick(self, title, item):
        """XTMP3's menu TITLE, item ITEM (an index), by the kernel's own bar:
        press on the title, wait for the pull-down, move to the item, wait
        until it is the one highlighted, release (tools/os88ui.py's gesture,
        over QMP)"""
        def w(name):
            return struct.unpack("<H", self.read(S(name), 2))[0]

        def find():
            n = w("menu_nbar")
            raw = self.read(S("menu_bar"), n * geom.MB_ENTSZ)
            for i in range(n):
                b = i * geom.MB_ENTSZ
                tp = struct.unpack_from("<H", raw, b + geom.MB_TITLE)[0]
                sg = struct.unpack_from("<H", raw, b + geom.MB_SEG)[0]
                if tp and sg:
                    t = self.read((sg << 4) + tp, 12).split(b"\0")[0]
                    if t.decode("latin-1") == title:
                        return (struct.unpack_from("<H", raw, b + geom.MB_XL)[0],
                                struct.unpack_from("<H", raw, b + geom.MB_XR)[0])
            return None
        # THE TITLE IS WAITED FOR, not read once: open_file returns when
        # XTMP3's WINDOW exists, and its menus reach the bar when that window
        # comes to the front - a moment later, and on a loaded box late
        # enough that a pick made at once found no "File" (dfdel, offdir and
        # wavdirs in the soak, a different set each run)
        box = []
        if not self.until(lambda: box.append(find()) or box[-1], "the %s "
                          "menu on the bar" % title, 10, 0.1):
            return False
        cell = box[-1]
        x = (cell[0] + cell[1]) // 2
        # CLOSED LOOP (os88qemu.mouse_to/mouse_button): every move and both
        # edges confirmed against the kernel's mouse_x/mouse_y/mouse_btn.
        # tools/mouse.py dead-reckons QEMU's serial mouse, and a packet lost
        # or merged on a loaded box put the press beside the title - "the
        # pull-down" never came - or the release on the wrong item
        hmp = self.hmp
        try:
            os88qemu.mouse_to(self, S, hmp, x, 6)
            os88qemu.mouse_button(self, S, hmp, 1)
            if not self.until(lambda: w("menu_y1") and
                              self.read(S("menu_dropd"), 1)[0],
                              "the pull-down", 10, 0.1):
                os88qemu.mouse_button(self, S, hmp, 0)
                return False
            iy = (w("menu_y1") + 1 + item * geom.MENU_ITEM_H
                  + geom.MENU_ITEM_H // 2)
            os88qemu.mouse_to(self, S, hmp, x, iy, btn=1)
            ok = self.until(lambda: w("menu_sel") == item, "item %d" % item,
                            10, 0.1)
            os88qemu.mouse_button(self, S, hmp, 0)
        except os88qemu.MouseError as e:
            print("     (%s)" % e)
            return False
        return ok

    def alert_button(self, k):
        """os88ui's alert (OS88UI_AW x OS88UI_AH, apps/os88ui.inc): button K
        of its two clicked, at os88ui_arect's own formula"""
        aw, ah, bw, bg, by, bh = 288, 92, 72, 12, 46, 13
        n = S("wm_wins")

        def up():                       # WAITED FOR: the press that asks
            for i in range(geom.MAX_WIN):   # opens it a pass later, and on
                rec = self.read(n + i * geom.WIN_SIZE, geom.WIN_SIZE)  # a
                fl, _x, _y, ww, hh = struct.unpack_from("<HHHHH", rec, 0)
                if fl & 2 and aw <= ww <= aw + 2 and hh == ah:  # loaded box
                    return True             # a fixed sleep missed it
            return False
        self.until(up, "the alert", 15, 0.1)
        for i in range(geom.MAX_WIN):
            rec = self.read(n + i * geom.WIN_SIZE, geom.WIN_SIZE)
            fl, x, y, ww, hh = struct.unpack_from("<HHHHH", rec, 0)
            if fl & 2 and aw <= ww <= aw + 2 and hh == ah:
                left = x + (ww - (2 * (bw + bg) - bg)) // 2
                bx = left + k * (bw + bg) + bw // 2
                byy = y + geom.TITLE_H + by + bh // 2
                try:                    # closed loop, as menu_pick
                    os88qemu.mouse_click(self, S, self.hmp, bx, byy)
                except os88qemu.MouseError as e:
                    print("     (%s)" % e)
                    return False
                return True
        wins = []
        for i in range(geom.MAX_WIN):
            rec = self.read(n + i * geom.WIN_SIZE, geom.WIN_SIZE)
            wins.append(struct.unpack_from("<HHHHH", rec, 0))
        print("     (no alert: windows %s)" % [w for w in wins if w[0]])
        return False

    def mdir(self, img, path="::"):
        r = subprocess.run(["mdir", "-a", "-b", "-i", part(img), path],
                           capture_output=True, text=True)
        return [ln.strip() for ln in r.stdout.splitlines()]

    def mcat(self, img, path):
        r = subprocess.run(["mcopy", "-n", "-i", part(img), path, "-"],
                           capture_output=True)
        return r.stdout if r.returncode == 0 else None


def part(img):
    """IMG@@offset for mtools: the partition's first LBA, out of the MBR"""
    lba = struct.unpack_from("<I", open(img, "rb").read(512), 446 + 8)[0]
    return "%s@@%d" % (img, lba * 512)


def bigfat(path, mb, spc):
    """a SPARSE hard-disk image with one FAT16 partition whose clusters are
    SPC sectors - 16, 32 and 64 are the 8, 16 and 32 KB a DOS FORMAT gives a
    256 MB-2 GB volume, here on the least FAT16 that has them (4,085
    clusters at least, so the type is FAT16 by the count, as it must be).
    The MBR's end CHS is 1023/254/63, so QEMU guesses 255 heads and 63
    sectors, the geometry SeaBIOS's translation answers int 13h AH=08 with"""
    tot = mb * 2048
    start = 63
    ptot = tot - start
    root = 32                               # 512 entries
    fatsz = 1
    for _ in range(64):
        nclus = (ptot - 1 - 2 * fatsz - root) // spc
        need = ((nclus + 2) * 2 + 511) // 512
        if need == fatsz:
            break
        fatsz = need
    assert 4085 <= nclus < 65525, (mb, spc, nclus)
    mbr = bytearray(512)
    e = bytearray(16)
    e[1:4] = b"\x01\x01\x00"
    e[4] = 0x06
    e[5:8] = b"\xFE\xFF\xFF"
    struct.pack_into("<II", e, 8, start, ptot)
    mbr[446:462] = e
    mbr[510:512] = b"\x55\xAA"
    bs = bytearray(512)
    bs[0:3] = b"\xEB\x3C\x90"
    bs[3:11] = b"MSDOS5.0"
    struct.pack_into("<HBHBHHBHHHII", bs, 11, 512, spc, 1, 2, 512,
                     ptot if ptot < 65536 else 0, 0xF8, fatsz, 63, 255,
                     start, ptot if ptot >= 65536 else 0)
    bs[36] = 0x80
    bs[38] = 0x29
    struct.pack_into("<I", bs, 39, 0x38083808)
    bs[43:54] = b"BIGCLU%-5d" % (spc // 2)
    bs[54:62] = b"FAT16   "
    bs[510:512] = b"\x55\xAA"
    with open(path, "wb") as f:
        f.truncate(tot * 512)
        f.seek(0)
        f.write(mbr)
        f.seek(start * 512)
        f.write(bs)
        for k in range(2):
            f.seek((start + 1 + k * fatsz) * 512)
            f.write(b"\xF8\xFF\xFF\xFF")


def floppy_listing(names):
    return ["SYSTEM"] + sorted(names + ["X3STUB.CFG", "XTMP3.O88"])


def hd_listing(names, with_pkg=True):
    base = names + (["X3STUB.CFG", "XTMP3.O88"] if with_pkg else [])
    return ["SYSTEM"] + sorted(base)


def fixture(name):
    return os.path.join(FIX, name)


# -----------------------------------------------------------------------------
# THE WAV, read on the host (x3conv.inc's layout)
# -----------------------------------------------------------------------------
def wavcheck(leg, data, src, clu, rung, rate, ch, bits, frames=None):
    ok = check(leg, "the WAV is a RIFF/WAVE of its own length",
               data[:4] == b"RIFF" and data[8:12] == b"WAVE" and
               struct.unpack_from("<I", data, 4)[0] + 8 == len(data))
    fmt = struct.unpack_from("<HHIIHH", data, 20)
    check(leg, "fmt: PCM %d Hz %d ch %d-bit" % (rate, ch, bits),
          fmt == (1, ch, rate, rate * ch * bits // 8, ch * bits // 8, bits),
          str(fmt))
    check(leg, "the xtm3 chunk", data[36:40] == b"xtm3" and
          struct.unpack_from("<I", data, 40)[0] == 44)
    idn = data[44:88]
    fp = struct.unpack_from("<III", idn, 2)
    want = (len(src), zlib.crc32(src[:16384]), zlib.crc32(src[-16384:]))
    check(leg, "the identity's fingerprint is the source's (size, CRC-32 of "
          "its first and last 16 KB)", fp == want,
          "%s vs %s" % ([hex(x) for x in fp], [hex(x) for x in want]))
    check(leg, "the identity: versions, rung %s, the format" % QNAME[rung],
          struct.unpack_from("<HH", idn, 14) == (1, 1) and idn[21] == rung and
          struct.unpack_from("<H", idn, 22)[0] == rate and idn[24] == ch and
          idn[25] == bits)
    check(leg, "JUNK pads the data to the cluster", data[88:92] == b"JUNK" and
          data[clu - 8:clu - 4] == b"data")
    dlen = struct.unpack_from("<I", data, clu - 4)[0]
    check(leg, "the data runs to the file's end", clu + dlen == len(data))
    nfr = struct.unpack_from("<I", idn, 26)[0]
    check(leg, "the identity's frame count is the data's",
          nfr * ch * bits // 8 == dlen, "%d frames, %d bytes" % (nfr, dlen))
    if frames is not None:
        check(leg, "frames = the stream's x 576 / divisor - 529 (x3sim's "
              "trim)", nfr == frames, "%d, want %d" % (nfr, frames))
    pcm = data[clu:clu + 4000]
    if bits == 8:
        vals = set(pcm[::ch])
        check(leg, "the data is the stub's square (80h +- 30h)",
              vals <= {0x50, 0xB0} and len(vals) == 2, str(sorted(vals)[:4]))
    return ok


def frames_of(src):
    """Layer III frames after the first, the stub front's own count: sync,
    the header's size, the next - x3stubp.inc x3p_next's walk"""
    kb1 = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320]
    kb2 = [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160]
    hz = {3: [44100, 48000, 32000], 2: [22050, 24000, 16000],
          0: [11025, 12000, 8000]}
    o = 0
    if src[:3] == b"ID3":
        n = 0
        for i in range(6, 10):
            n = (n << 7) | (src[i] & 0x7F)
        o = n + 10
    n, ver = 0, None
    while o + 4 <= len(src):
        h = src[o:o + 4]
        if h[0] != 0xFF or (h[1] & 0xE0) != 0xE0:
            o += 1
            continue
        v = (h[1] >> 3) & 3
        br, sr, pad = h[2] >> 4, (h[2] >> 2) & 3, (h[2] >> 1) & 1
        if v == 1 or ((h[1] >> 1) & 3) != 1 or br in (0, 15) or sr == 3:
            o += 1
            continue
        if ver is None:
            ver = (v, sr)
        elif (v, sr) != ver:
            o += 1
            continue
        kb = (kb1 if v == 3 else kb2)[br]
        size = (144 if v == 3 else 72) * kb * 1000 // hz[v][sr] + pad
        if o + size > len(src):
            break
        n += 1
        o += size
    return n, ver


# -----------------------------------------------------------------------------
# THE LEGS
# -----------------------------------------------------------------------------
NEON = "NEONDRV.MP3"


def ladder_case(name, speeds, want, live=True, bound=None, unprobed=(),
                div=None):
    leg = "ladder/" + name
    print(" %s: speeds %s" % (leg, speeds))
    r = Rig(leg, sb=True, xcfg=cfg(speeds),
            hd=[(HD0_GEO, [fixture(NEON)], ["SYSTEM/APPDATA"], 0)])
    try:
        if not check(leg, "XTMP3 opens on the MP3",
                     r.open_file("C", NEON, hd_listing([NEON]))):
            return
        t0 = r.ticks()
        r.until(lambda: r.st()["md"] not in (MD_IDLE, MD_CHECK, MD_LOAD),
                "the ladder's decision", 60, 0.1)
        dt = (r.ticks() - t0) & 0xFFFF
        s = r.st()
        m = r.meas()
        print("     measured (x100): %s, %.1f s of guest time" % (
            ", ".join("%s %s" % (QNAME[i], "-" if v == 0xFFFF else v)
                      for i, v in enumerate(m) if i), dt / 18.2))
        if live:
            check(leg, "plays LIVE on %s" % QNAME[want],
                  s["md"] == MD_LIVE and s["qual"] == want,
                  "md %d qual %s" % (s["md"], QNAME[s["qual"]]))
            if div:
                check(leg, "...divided by %d, %d Hz from the source's 44,100 "
                      "(SPEC.md 121.4: XT Standard's divisor is its source's)"
                      % (div, 44100 // div), r.b("x3_odiv") == div and
                      s["rate"] == 44100 // div, "div %d, %d Hz" % (
                          r.b("x3_odiv"), s["rate"]))
        else:
            check(leg, "asks to decode first (no live rung)",
                  s["md"] == MD_CHOOSE and s["state"] == ST_CHOOSE,
                  "md %d" % s["md"])
        for q in unprobed:
            check(leg, "%s was never probed (a cheaper rung failed)"
                  % QNAME[q], m[q] == 0xFFFF, str(m[q]))
        if bound:
            check(leg, "calibration took under %.1f s of guest time" % bound,
                  dt / 18.2 < bound, "%.1f s" % (dt / 18.2))
    finally:
        r.done()


def leg_ladder():
    ladder_case("capable", [0, 400, 400, 400, 400, 400], Q_ORIG)
    ladder_case("middling", [0, 100, 200, 400, 500, 500], Q_HALF)
    ladder_case("slower", [0, 60, 100, 200, 500, 500], Q_QUART,
                unprobed=())
    # A MIDDLING MACHINE WITH A 44.1 kHz SONG (SPEC.md 121.5's rule: Decode
    # first only when no live rung passes): every STANDARD rung short, XT
    # Standard - divided by 4 for a 44.1 kHz source - passes, and it plays
    # live; XT Fast (no divided kernels) is never asked
    ladder_case("xt44", [0, 30, 50, 80, 200, 400], Q_XTSTD, div=4,
                unprobed=(Q_XTFAST,))
    ladder_case("xt", [0, 30, 40, 50, 80, 100], None, live=False,
                unprobed=(Q_ORIG, Q_HALF), bound=6.0)
    ladder_case("hopeless", [0, 5, 5, 5, 5, 5], None, live=False,
                unprobed=(Q_ORIG, Q_HALF), bound=4.0)


def dlog(r):
    """the X3STUB build's debug ring (X3LOG), oldest first"""
    d, n = r.rd("x3_dlog", 48), r.b("x3_dlogn")
    order = list(range(n, 48, 3)) + list(range(0, n, 3))
    return " ".join("%s%d" % (chr(d[i]), struct.unpack_from("<H", d, i + 1)[0])
                    for i in order if d[i])


def leg_slowdown():
    """two mid-track slowdowns on Original. 1.05x: the headroom is gone but
    the card is still fed, so only the WINDOWS can see it - two under 1.10
    step down before anything is heard. 0.60x: genuinely too slow, so the
    ring runs dry before two windows can close - one underrun at a granule
    boundary, with the window under 1.10x, is the step's other trigger
    (SPEC.md 121.5). Either way ONE step, Original to Half, never back up,
    and the position never goes back. Then the underrun RULE at 4x: one is
    kept, two in a window step"""
    leg = "slowdown"
    for spd, under_ok in ((105, False), (60, True)):
        r = Rig(leg, sb=True, xcfg=cfg([0, 400, 400, 400, 400, 400],
                                       slr=Q_ORIG, slaft=150, slspd=spd),
                hd=[(HD0_GEO, [fixture(NEON)], ["SYSTEM/APPDATA"], 0)])
        tag = "%d.%02dx: " % (spd // 100, spd % 100)
        try:
            if not check(leg, tag + "XTMP3 opens",
                         r.open_file("C", NEON, hd_listing([NEON]))):
                return
            r.until(lambda: r.st()["md"] == MD_LIVE, "live", 60, 0.1)
            check(leg, tag + "starts LIVE on Original",
                  r.st()["qual"] == Q_ORIG)
            seen, el = [], []
            r.until(lambda: (seen.append(r.st()["qual"])
                             or el.append(r.st()["elap"])
                             or r.st()["state"] == ST_DONE), "the end", 60, 0.3)
            quals = [q for i, q in enumerate(seen)
                     if i == 0 or q != seen[i - 1]]
            s = r.st()
            check(leg, tag + "stepped down ONCE, Original to Half, and stayed",
                  quals == [Q_ORIG, Q_HALF], "%s, %d underruns, %s" % (
                      [QNAME[q] for q in quals], s["under"],
                      dlog(r)))
            if under_ok:
                check(leg, tag + "at most one underrun (the step's own)",
                      s["under"] <= 1, str(s["under"]))
            else:
                check(leg, tag + "no underrun (the windows stepped first)",
                      s["under"] == 0, str(s["under"]))
            back = [b - a for a, b in zip(el, el[1:]) if b < a]
            check(leg, tag + "the position never went back", not back,
                  str(el))
        finally:
            r.done()
    # AN UNDERRUN IS A REASON TO RE-EVALUATE, NOT A STEP (SPEC.md 121.5): at
    # 4x headroom one underrun (the test hook counts it as the card's own, at
    # live granule 40) is the disk's - the rung is kept and the refill asked
    # for earlier; two inside one 32-granule window (40 and 44) step anyway
    for n, quals_want in ((1, [Q_ORIG]), (2, [Q_ORIG, Q_HALF])):
        r = Rig(leg, sb=True, xcfg=cfg([0, 400, 400, 400, 400, 400],
                                       undat=40, undn=n),
                hd=[(HD0_GEO, [fixture(NEON)], ["SYSTEM/APPDATA"], 0)])
        tag = "%d underrun%s at 4x: " % (n, "" if n == 1 else "s")
        try:
            if not check(leg, tag + "XTMP3 opens",
                         r.open_file("C", NEON, hd_listing([NEON]))):
                return
            r.until(lambda: r.st()["md"] == MD_LIVE, "live", 60, 0.1)
            seen = []
            r.until(lambda: seen.append(r.st()["qual"]) or
                    r.st()["state"] == ST_DONE, "the end", 60, 0.3)
            quals = [q for i, q in enumerate(seen)
                     if i == 0 or q != seen[i - 1]]
            s = r.st()
            check(leg, tag + ("kept Original: the disk's, not the decode's"
                              if n == 1 else "stepped to Half: the safety "
                              "valve"), quals == quals_want and
                  s["under"] == n, "%s, %d underruns" % (
                      [QNAME[q] for q in quals], s["under"]))
            if n == 1:
                check(leg, tag + "...and the refill comes earlier: the "
                      "look-ahead's low water at 3/4 of its ring",
                      r.w("x3_lalw") == r.w("x3_lasz") * 3 // 4,
                      "%d of %d" % (r.w("x3_lalw"), r.w("x3_lasz")))
        finally:
            r.done()


def drive_dialog(r, what="the drive dialog", secs=90):
    """the decode-first dialog UP: its WINDOW, not its mode. x3c_begin sets
    MD_CHOOSE first and only then scans the fixed volumes (x3c_vscan: each
    one's root and its free space, which on a 140 MB FAT16 disk is long) and
    opens the window - so a wait on the mode read x3c_nv 0 and x3c_vf
    unfilled (xtclu/32k in the soak: 'lists C: and D: - 0', '[1, 0]'), and
    an Enter sent then reached no dialog"""
    return r.until(lambda: r.st()["md"] == MD_CHOOSE and
                   r.b("x3c_dmode") == 1 and r.w("x3c_dwin"), what, secs, 0.2)


def save_dialog(r):
    """the drive dialog's Enter has asked for the SAVE dialog: wait until it
    is up (fdlg_win). A fixed 2.5 s let the next Enter land before it was,
    on a loaded box, and the conversion never started (xtmp3eng in the
    soak: 'state 3 md 0')"""
    r.until(lambda: struct.unpack("<H", r.read(S("fdlg_win"), 2))[0],
            "the Save dialog", 30, 0.1)


def conv_flow(r, leg, expect_dialog=True):
    """the drive dialog's Enter, the Save dialog's Enter, the conversion"""
    drive_dialog(r, secs=60)
    check(leg, "the drive dialog is up", r.st()["state"] == ST_CHOOSE)
    r.key("ret")
    save_dialog(r)
    r.key("ret")
    conv_end(r, "the end of the conversion")


def conv_end(r, what, secs=240):
    """the conversion over AND what it hands to begun: x3c_ended puts x3_md
    back to IDLE before x3_file_play's disk reads, with the state still
    DECODE, and only the play makes it FILE (or ERROR). Waiting on md alone
    returned in that window and read 'state 3 md 0' (covox, in the soak)"""
    r.until(lambda: (r.st()["md"] not in (MD_CHOOSE, MD_CONV) and
                     r.st()["state"] != ST_DECODE), what, secs, 0.3)


def leg_conv():
    leg = "conv"
    name = "AFTERHRS.MP3"
    r = Rig(leg, floppy=[fixture(name)],
            hd=[(HD0_GEO, [], [], 0), (HD1_GEO, [], [], 0)])
    try:
        if not check(leg, "XTMP3 opens on the MP3",
                     r.open_file("B", name, floppy_listing([name]))):
            return
        conv_flow(r, leg)
        s = r.st()
        check(leg, "the finished file plays (the speaker's bracket)",
              s["state"] == ST_FILE and s["md"] == MD_SPK,
              "state %d md %d" % (s["state"], s["md"]))
        grans = r.w("x3_grans")
        r.key("esc")
        r.until(lambda: r.st()["md"] == MD_IDLE, "the stop", 60)
        check(leg, "Esc stops it: READY", r.st()["state"] == ST_READY)
        r.key("spc")                    # PLAY AGAIN: the cache
        r.until(lambda: r.st()["md"] == MD_SPK, "the second play", 60, 0.1)
        s = r.st()
        check(leg, "a second Play plays the finished file at once",
              s["md"] == MD_SPK and s["state"] == ST_FILE)
        check(leg, "...with no granule decoded", r.w("x3_grans") == grans,
              "%d -> %d" % (grans, r.w("x3_grans")))
        check(leg, "...and no decoder resident", r.b("x3_bpart") == 0xFF)
        r.key("esc")
        r.until(lambda: r.st()["md"] == MD_IDLE, "the stop", 60)
        r.close()
        hd0, b = r.hds[0], os.path.join(r.tmp, "b.img")
        names = r.mdir(hd0)
        check(leg, "C: holds AFTERHRS.WAV and no .TMP",
              any("AFTERHRS.WAV" in n for n in names) and
              not any(".TMP" in n for n in names), str(names))
        data = r.mcat(hd0, "::AFTERHRS.WAV")
        src = open(fixture(name), "rb").read()
        nfr, _ = frames_of(src)
        if check(leg, "the WAV can be read back", data is not None):
            wavcheck(leg, data, src, 2048, Q_ORIG, 11025, 1, 8,
                     frames=nfr * 576 - 529)
        dat = subprocess.run(["mcopy", "-n", "-i", b,
                              "::SYSTEM/APPDATA/XTMP3.DAT", "-"],
                             capture_output=True).stdout
        check(leg, "XTMP3.DAT records it", dat[:4] == b"X3DT" and
              dat[8 + 46] == 1 and dat[8 + 24:8 + 36].rstrip(b"\0") ==
              b"AFTERHRS.WAV", dat[:64].hex())
    finally:
        r.done()


def leg_cancel():
    leg = "cancel"
    name = "NEONDRV.MP3"
    r = Rig(leg, floppy=[fixture(name)], xcfg=cfg([0, 20, 30, 40, 50, 60]),
            hd=[(HD0_GEO, [], [], 0)])
    try:
        if not check(leg, "XTMP3 opens", r.open_file("B", name,
                                                      floppy_listing([name]))):
            return
        drive_dialog(r)
        r.key("ret")
        save_dialog(r)
        r.key("ret")
        r.until(lambda: r.st()["md"] == MD_CONV and r.st()["pct"] > 3,
                "the conversion under way", 60)
        check(leg, "it converts (DECODE, a percentage)",
              r.st()["state"] == ST_DECODE, "pct %d" % r.st()["pct"])
        t0 = r.ticks()
        r.key("esc")
        r.until(lambda: r.st()["md"] != MD_CONV, "the cancel", 30, 0.05)
        dt = ((r.ticks() - t0) & 0xFFFF) / 18.2
        s = r.st()
        check(leg, "Esc cancels: READY, nothing playing",
              s["state"] == ST_READY and s["md"] == MD_IDLE, str(s))
        check(leg, "answered within a second of guest time", dt < 1.0,
              "%.2f s" % dt)
        r.close()
        names = r.mdir(r.hds[0])
        check(leg, "no .TMP and no .WAV is left on C:",
              not any(".TMP" in n or ".WAV" in n for n in names), str(names))
    finally:
        r.done()


def synth_long(path, frames=2600):
    """an MPEG-2.5 11,025 Hz mono stream whose FIRST frame is 160 kbps and the
    rest 8 kbps, with no Xing tag: its estimated duration (bytes x 8 / the
    first frame's kbps) is about a twentieth of what it plays"""
    out = bytearray()

    def frame(bri):
        kb = [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160][bri]
        size = 72 * kb * 1000 // 11025
        h = bytes([0xFF, 0xE3, (bri << 4) | (0 << 2), 0xC0])
        return h + bytes(size - 4)
    out += frame(14)
    for _ in range(frames):
        out += frame(1)
    open(path, "wb").write(out)
    return frames + 1


def leg_full():
    leg = "full"
    t = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    long_ = os.path.join(t, "LONG.MP3")
    nfr = synth_long(long_)
    # C: is the 8 MB layout filled to ~250 KB free; the stream says ~7 s
    # (~75 KB of 8-bit mono) and plays ~136 s (~1.5 MB)
    r = Rig(leg, floppy=[long_], hd=[(HD1_GEO, [], [], 8060 * 1024)])
    try:
        if not check(leg, "XTMP3 opens", r.open_file("B", "LONG.MP3",
                                                      floppy_listing(["LONG.MP3"]))):
            return
        conv_flow(r, leg)
        s = r.st()
        check(leg, "stops with ERROR 'The disk is full'",
              s["state"] == ST_ERROR and r.errtext() == "The disk is full",
              "state %d '%s'" % (s["state"], r.errtext()))
        check(leg, "nothing plays", s["md"] == MD_IDLE)
        r.close()
        names = r.mdir(r.hds[0])
        check(leg, "the temporary file is deleted, no WAV",
              not any(".TMP" in n or ".WAV" in n for n in names), str(names))
    finally:
        r.done()
        shutil.rmtree(t, ignore_errors=True)
    print("     (the stream: %d frames)" % nfr)


def leg_stale():
    leg = "stale"
    name = "AFTERHRS.MP3"
    r = Rig(leg, floppy=[fixture(name)], hd=[(HD0_GEO, [], [], 0)])
    keep = r.tmp
    try:
        if not check(leg, "XTMP3 opens (boot 1)",
                     r.open_file("B", name, floppy_listing([name]))):
            return
        conv_flow(r, leg)
        r.key("esc")
        r.until(lambda: r.st()["md"] == MD_IDLE, "the stop", 60)
        r.close()
        for case in ("incomplete", "identity"):
            hd0 = r.hds[0]
            data = bytearray(r.mcat(hd0, "::AFTERHRS.WAV"))
            if case == "incomplete":
                struct.pack_into("<I", data, 4, len(data) - 8 + 512)
            else:
                data[44 + 6] ^= 0xFF        # the head CRC
            p = os.path.join(keep, "AFTERHRS.WAV")
            open(p, "wb").write(data)
            subprocess.run(["mcopy", "-o", "-n", "-i", part(hd0), p,
                            "::AFTERHRS.WAV"], check=True)
            r2 = Rig(leg, keep=keep)
            try:
                if not check(leg, "XTMP3 opens (%s)" % case,
                             r2.open_file("B", name, floppy_listing([name]))):
                    return
                r2.until(lambda: r2.st()["md"] not in (MD_IDLE, MD_CHECK, MD_LOAD),
                         "the decision", 60, 0.1)
                s = r2.st()
                check(leg, "an %s file is not reused: the dialog is asked"
                      % case, s["md"] == MD_CHOOSE, "md %d" % s["md"])
                r2.key("esc")
                gpace(1)
            finally:
                r2.close()
            dat = subprocess.run(["mcopy", "-n", "-i",
                                  os.path.join(keep, "b.img"),
                                  "::SYSTEM/APPDATA/XTMP3.DAT", "-"],
                                 capture_output=True).stdout
            used = [dat[8 + i * 48 + 46] for i in range(16)] if dat else []
            check(leg, "its record is dropped (%s)" % case, not any(used),
                  str(used))
            if case == "incomplete":    # the identity case wants a record:
                subprocess.run(["mdel", "-i", part(hd0), "::AFTERHRS.WAV"],
                               check=True)  # (the name free again) and a
                r3 = Rig(leg, keep=keep)    # conversion made for it
                try:
                    r3.open_file("B", name, floppy_listing([name]))
                    conv_flow(r3, leg)
                    r3.key("esc")
                    r3.until(lambda: r3.st()["md"] == MD_IDLE, "the stop", 60)
                finally:
                    r3.close()
                dat = subprocess.run(["mcopy", "-n", "-i",
                                      os.path.join(keep, "b.img"),
                                      "::SYSTEM/APPDATA/XTMP3.DAT", "-"],
                                     capture_output=True).stdout
                check(leg, "a fresh conversion is recorded (for the identity "
                      "case)", any(dat[8 + i * 48 + 46] for i in range(16)))
    finally:
        shutil.rmtree(keep, ignore_errors=True)


def leg_novol():
    leg = "novol"
    name = "AFTERHRS.MP3"
    r = Rig(leg, floppy=[fixture(name)])
    try:
        if not check(leg, "XTMP3 opens", r.open_file("B", name,
                                                      floppy_listing([name]))):
            return
        drive_dialog(r)
        check(leg, "no volume listed", r.b("x3c_nv") == 0)
        r.key("ret")
        gpace(2)
        check(leg, "Enter does nothing (Decode and Play greyed)",
              r.st()["md"] == MD_CHOOSE and not r.b("x3c_savepend"))
        r.key("esc")
        r.until(lambda: r.st()["md"] == MD_IDLE, "the cancel", 20)
        check(leg, "Esc leaves the track READY", r.st()["state"] == ST_READY)
    finally:
        r.done()


def leg_twovol():
    leg = "twovol"
    name = "AFTERHRS.MP3"
    t = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    long_ = os.path.join(t, "HUGE.MP3")     # a frame count no 8 MB disk holds
    open(long_, "wb").write(huge_xing())
    lst = floppy_listing([name, "HUGE.MP3"])
    r = Rig(leg, floppy=[fixture(name), long_],
            hd=[(HD0_GEO, [], [], 0), (HD1_GEO, [], [], 0)])
    try:
        if not check(leg, "XTMP3 opens", r.open_file("B", name, lst)):
            return
        drive_dialog(r)
        check(leg, "two hard drives listed, C: and D:", r.b("x3c_nv") == 2 and
              list(r.rd("x3c_vv", 2)) == [2, 3], str(list(r.rd("x3c_vv", 2))))
        r.key("down")
        gpace(1)
        r.key("ret")
        save_dialog(r)
        r.key("ret")
        conv_end(r, "the end")
        check(leg, "converted and playing", r.st()["state"] == ST_FILE)
        check(leg, "the destination was D:", r.b("x3c_dvol") == 3)
        r.key("esc")
        r.until(lambda: r.st()["md"] == MD_IDLE, "the stop", 60)
        r.close()
        names = r.mdir(r.hds[1])
        check(leg, "AFTERHRS.WAV is on D:", any("AFTERHRS.WAV" in n
                                                for n in names), str(names))
    finally:
        r.done()
    # A TRACK D: CANNOT HOLD (and C: can): its Info count
    r = Rig(leg, floppy=[fixture(name), long_],
            hd=[(HD0_GEO, [], [], 0), (HD1_GEO, [], [], 0)])
    try:
        if not check(leg, "XTMP3 opens on the long one",
                     r.open_file("B", "HUGE.MP3", lst)):
            return
        drive_dialog(r, "its dialog")
        vf = list(r.rd("x3c_vf", 2))
        check(leg, "D: is 'too little room' for it, C: is not",
              bool(vf[1] & 2) and not vf[0] & 2, str(vf))
        r.key("down")                   # D: chosen...
        gpace(1)
        r.key("ret")                    # ...and Decode and Play refused
        gpace(2)
        check(leg, "Decode and Play is greyed on D:",
              r.st()["md"] == MD_CHOOSE and not r.b("x3c_savepend"))
    finally:
        r.done()
        shutil.rmtree(t, ignore_errors=True)


def huge_xing():
    """MPEG-1 44.1 kHz mono 128 kbps: an Info frame claiming 10,000 frames
    (261 s: 11.5 MB of 8-bit mono - more than D:'s 8 MB, less than C:'s 19),
    then a few real ones"""
    size = 144 * 128 * 1000 // 44100
    hdr = bytes([0xFF, 0xFB, 0x90, 0xC0])
    side = bytes(17)
    tag = b"Info" + struct.pack(">I", 1) + struct.pack(">I", 10000)
    first = hdr + side + tag
    first += bytes(size - len(first))
    out = first
    for _ in range(40):
        out += hdr + bytes(size - 4)
    return out


def leg_playwav():
    leg = "playwav"
    r = Rig(leg, sb=True, xcfg=cfg([0, 400, 400, 400, 400, 400]),
            hd=[(HD0_GEO, [fixture(NEON)], ["SYSTEM/APPDATA"], 0)])
    try:
        if not check(leg, "XTMP3 opens", r.open_file("C", NEON,
                                                      hd_listing([NEON]))):
            return
        r.until(lambda: r.st()["md"] == MD_LIVE, "live", 60)
        r.key("s")                      # Stop, then Decode First (D)
        r.until(lambda: r.st()["md"] == MD_IDLE, "the stop", 20)
        r.key("d")
        conv_flow(r, leg)
        s = r.st()
        check(leg, "the finished WAV plays through the card",
              s["md"] == MD_FILE and s["state"] == ST_FILE,
              "md %d state %d" % (s["md"], s["state"]))
        g0 = r.w("x3_grans")
        check(leg, "no decoder resident while it plays", r.b("x3_bpart") == 0xFF)
        r.until(lambda: r.st()["state"] == ST_DONE, "its end", 60)
        check(leg, "no granule decoded while it played",
              r.w("x3_grans") == g0, "%d -> %d" % (g0, r.w("x3_grans")))
        check(leg, "no underrun", r.st()["under"] == 0, str(r.st()["under"]))
        r.close()
        tone = captured_tone(r.wav)
        check(leg, "the card played the stub's square (its step is 655/65536 "
              "of 44,100: 440.8 Hz)", 440 <= tone[0] <= 441, "%s Hz" % tone[0])
    finally:
        r.done()


def leg_xtpeak():
    """SPEC.md 121.10 on the SHIPPED package: an XT Standard conversion with
    the REAL XT part (no stub), the user's override and Decode First, on a
    machine with no card. The engine's claim order (the part fetched, its
    work claimed and OPEN run, the part shrunk to its resident half, and only
    then the ring and the stage) must keep the working set X3S_PEAK measures
    - the region, the eager parts and every claim since launch - at or under
    400,000 bytes"""
    leg = "xtpeak"
    r = Rig(leg, stub=False,
            hd=[(HD0_GEO, [fixture(NEON)], ["SYSTEM/APPDATA"], 0)])
    try:
        if not check(leg, "XTMP3 opens", r.open_file("C", NEON,
                                                      hd_listing([NEON]))):
            return
        drive_dialog(r, "the decode-first dialog")
        r.key("esc")
        r.until(lambda: r.st()["md"] == MD_IDLE, "the dialog's cancel", 20)
        r.override_to(Q_XTSTD)        # Q: AUTO, ORIGINAL ... XT STANDARD
        check(leg, "the override is XT STANDARD",
              r.rd("x3_st", 3)[2] == Q_XTSTD, QNAME[r.rd("x3_st", 3)[2]])
        r.key("d")
        conv_flow(r, leg)
        s = r.st()
        check(leg, "the conversion finished and plays",
              s["state"] == ST_FILE, "state %d md %d, %s" % (
                  s["state"], s["md"], r.errtext()))
        print("     peak %d KB = %d bytes" % (s["peak"], s["peak"] * 1024))
        check(leg, "the working set stayed at or under 400,000 bytes",
              0 < s["peak"] * 1024 <= 400000, "%d KB" % s["peak"])
        nf = list(r.rd("x3_nfetch", 5))
        check(leg, "the XT part was fetched ONCE: the conversion is the "
              "probe's rung, so its OPEN stands and a RESET does (x3_reuse)",
              nf[3] == 1, "fetches %s" % nf)
    finally:
        r.done()
    # LIVE XT STANDARD WITH A CARD (SPEC.md 121.10 counts SOUND.DRV's grant):
    # x3_plan prefers the 16 KB grant with a smaller look-ahead (a floppy's
    # gulp shorter), the 8 KB grant only where that cannot fit; never the
    # external ring - NEONDRV is 44.1 kHz, 44,100 bytes a second of 8-bit mono
    r = Rig(leg, sb=True, stub=False,
            hd=[(HD0_GEO, [fixture(NEON)], ["SYSTEM/APPDATA"], 0)])
    try:
        if not check(leg, "XTMP3 opens (an SB16)", r.open_file(
                "C", NEON, hd_listing([NEON]))):
            return
        r.until(lambda: r.st()["md"] not in (MD_IDLE, MD_CHECK, MD_LOAD),
                "Auto's decision", 90, 0.2)
        r.key("s")
        r.until(lambda: r.st()["md"] == MD_IDLE, "the stop", 20)
        r.override_to(Q_XTSTD)
        r.key("spc")
        r.until(lambda: r.st()["md"] == MD_LIVE, "XT STANDARD live", 60, 0.2)
        s = r.st()
        check(leg, "live on XT STANDARD with the card",
              s["md"] == MD_LIVE and s["qual"] == Q_XTSTD,
              "md %d qual %s %s" % (s["md"], QNAME[s["qual"]], r.errtext()))
        check(leg, "the card's ring is the 16 KB grant (never the external "
              "ring), XT Standard divided by 4 (NEONDRV is 44.1 kHz: the "
              "speech tables, so the 16 KB look-ahead fits beside it)",
              r.w("x3_rsz") == 16384 and r.b("x3_ext") == 0 and
              r.b("x3_odiv") == 4 and r.w("x3_lasz") in (8192, 16384),
              "%d bytes, ext %d, div %d, look-ahead %d" % (
                  r.w("x3_rsz"), r.b("x3_ext"), r.b("x3_odiv"),
                  r.w("x3_lasz")))
        check(leg, "...and a gulp is X3_RDXT, 4 KB: one read the card's ring "
              "plays through (a read holds sch_lock)", r.w("x3_rdmax") == 4096,
              "%d" % r.w("x3_rdmax"))
        gpace(3)
        s = r.st()
        print("     live peak %d KB = %d bytes" % (s["peak"], s["peak"] * 1024))
        check(leg, "...no underrun", s["under"] == 0, "%d" % s["under"])
        check(leg, "live XT with a card: the working set at or under 400,000 "
              "bytes", 0 < s["peak"] * 1024 <= 400000, "%d KB" % s["peak"])
    finally:
        r.done()


# -----------------------------------------------------------------------------
# THE CEILING ON LARGE CLUSTERS (SPEC.md 121.10, x3_plan): the destination's
# cluster sizes the stage, and the plan keeps the XT path inside 400,000 bytes
# whatever the cluster is - or refuses by name where nothing fits
# -----------------------------------------------------------------------------
def xt_wav_check(leg, r, img, wav, src, profile, clu):
    data = r.mcat(img, "::" + wav)
    if not check(leg, "%s is on D:" % wav, data is not None):
        return
    import numpy as np
    import x3simxt
    import x3sim
    ref, rate, dec = x3simxt.decode(fixture(src), profile)
    st = x3sim.Stream(open(fixture(src), "rb").read())
    ref = x3sim.trim_pcm(ref.reshape(-1, 1), st.info, 1).reshape(-1)
    check(leg, "its header is one %d KB cluster ('data' at %d)" % (
        clu // 1024, clu - 8), data[clu - 8:clu - 4] == b"data")
    dlen = struct.unpack_from("<I", data, clu - 4)[0]
    pcm = np.frombuffer(data[clu:clu + dlen], dtype=np.uint8)
    n = min(len(pcm), len(ref))
    bad = np.count_nonzero(pcm[:n] != ref[:n]) if n else -1
    check(leg, "its PCM is x3simxt's %s, trimmed: every byte" % profile,
          len(pcm) == len(ref) and bad == 0,
          "%d vs %d bytes, %d differ" % (len(pcm), len(ref), bad))


def clu_convert(r, leg):
    """the drive dialog: D: chosen, Decode and Play, the Save dialog's name"""
    drive_dialog(r)
    check(leg, "the drive dialog lists C: and D:", r.b("x3c_nv") == 2,
          "%d" % r.b("x3c_nv"))
    r.key("down")
    gpace(1)
    r.key("ret")
    save_dialog(r)
    r.key("ret")
    conv_end(r, "the end of the conversion")


def leg_xtclu():
    for spc, mb, src in ((16, 40, "X11K24.MP3"), (32, 72, "X12K32.MP3"),
                         (64, 140, "X8K16.MP3")):
        clu = spc * 512
        leg = "xtclu/%dk" % (clu // 1024)
        print(" %s: %s to a %d KB-cluster D:" % (leg, src, clu // 1024))
        r = Rig(leg, stub=False,
                hd=[(HD0_GEO, [fixture(src)], ["SYSTEM/APPDATA"], 0),
                    ({"mb": mb, "spc": spc}, [], [], 0)])
        try:
            if not check(leg, "XTMP3 opens", r.open_file(
                    "C", src, hd_listing([src]))):
                continue
            drive_dialog(r, "Auto's dialog")
            r.key("esc")
            r.until(lambda: r.st()["md"] == MD_IDLE, "the cancel", 20)
            r.override_to(Q_XTSTD)
            r.key("d")
            if clu == 32768:                # GREYED IN THE DIALOG for XT
                drive_dialog(r)             # Standard, which cannot hold a
                vf = list(r.rd("x3c_vf", 2))    # 32 KB cluster's stage
                check(leg, "XT STANDARD: D: greyed '32 KB clusters' in the "
                      "drive dialog, C: not", bool(vf[1] & 4) and
                      not vf[0] & 4, str(vf))
                r.key("down")
                gpace(1)
                r.key("ret")
                gpace(2)
                check(leg, "...Decode and Play refuses it (Enter does "
                      "nothing)", r.st()["md"] == MD_CHOOSE and
                      not r.b("x3c_savepend"))
                check(leg, "...the peak so far under 400,000 bytes",
                      r.st()["peak"] * 1024 <= 400000,
                      "%d KB" % r.st()["peak"])
                r.key("esc")
                r.until(lambda: r.st()["md"] == MD_IDLE, "the cancel", 20)
            else:
                clu_convert(r, leg)
            s = r.st()
            if clu < 32768:
                check(leg, "XT STANDARD converted to D: and plays",
                      s["state"] == ST_FILE, "state %d '%s'" % (
                          s["state"], r.errtext()))
                one, lasz = r.b("x3c_one"), r.w("x3_lasz")
                print("     peak %d KB; stage %s of %d KB, look-ahead %d KB"
                      % (s["peak"], "one half" if one else "two halves",
                         r.w("x3c_half") // 1024, lasz // 1024))
                check(leg, "the working set at or under 400,000 bytes",
                      0 < s["peak"] * 1024 <= 400000, "%d KB" % s["peak"])
                check(leg, "the stage is ONE cluster (the plan: two do not "
                      "fit)", one == 1, "x3c_one %d" % one)
                wav = src.replace(".MP3", ".WAV")
                r.key("esc")
                r.until(lambda: r.st()["md"] == MD_IDLE, "the stop", 30)
                r.close()
                xt_wav_check(leg, r, r.hds[1], wav, src, "std", clu)
            else:
                check(leg, "the override is XT FAST",   # (by state, as
                      r.override_to(Q_XTFAST))          # override_to says)
                r.key("d")
                clu_convert(r, leg)
                s = r.st()
                check(leg, "XT FAST converted to the 32 KB-cluster D:",
                      s["state"] == ST_FILE, "state %d '%s'" % (
                          s["state"], r.errtext()))
                print("     peak %d KB; stage %s of %d KB" % (
                    s["peak"], "one half" if r.b("x3c_one") else "two halves",
                    r.w("x3c_half") // 1024))
                check(leg, "the working set at or under 400,000 bytes",
                      0 < s["peak"] * 1024 <= 400000, "%d KB" % s["peak"])
                r.key("esc")
                r.until(lambda: r.st()["md"] == MD_IDLE, "the stop", 30)
                r.close()
                names = r.mdir(r.hds[1])
                check(leg, "no .TMP left on D:",
                      not any(".TMP" in nm for nm in names), str(names))
                xt_wav_check(leg, r, r.hds[1], src.replace(".MP3", ".WAV"),
                             src, "fast", clu)
        finally:
            r.done()


# -----------------------------------------------------------------------------
# THE PATHS NO LEG HAD TAKEN (docs/xtmp3/ENGINE.md section 12)
# -----------------------------------------------------------------------------
def patch_file_in_image(img, src, at, xor=0xFF):
    """flip one byte AT bytes into the file whose first 512 bytes are SRC's,
    in the image on the host - under a running guest, which reads it next
    time through int 13h (QEMU caches nothing of a raw image)"""
    with open(img, "r+b") as f:
        data = f.read()
        o = data.find(src[:512])
        if o < 0:
            return False
        f.seek(o + at)
        b = data[o + at]
        f.write(bytes([b ^ xor]))
    return True


def leg_chgsrc():
    """the source CHANGED DURING a conversion: finalisation takes its
    fingerprint again (SPEC.md 121.7) and the job fails with the reason, the
    temporary file goes and nothing plays"""
    leg = "chgsrc"
    name = "AFTERHRS.MP3"
    src = open(fixture(name), "rb").read()
    r = Rig(leg, floppy=[fixture(name)], xcfg=cfg([0, 20, 30, 40, 50, 60]),
            hd=[(HD0_GEO, [], [], 0)])
    try:
        if not check(leg, "XTMP3 opens", r.open_file("B", name,
                                                      floppy_listing([name]))):
            return
        drive_dialog(r)
        r.key("ret")
        save_dialog(r)
        r.key("ret")
        r.until(lambda: r.st()["md"] == MD_CONV and r.st()["pct"] > 10,
                "the conversion under way", 90)
        check(leg, "it converts", r.st()["state"] == ST_DECODE)
        check(leg, "the source changed under it (a byte of its first 16 KB, "
              "on the host)", patch_file_in_image(
                  os.path.join(r.tmp, "b.img"), src, 600))
        r.until(lambda: r.st()["md"] != MD_CONV, "the end of the job", 300,
                0.5)
        s = r.st()
        check(leg, "the job fails: 'The MP3 changed meanwhile'",
              s["state"] == ST_ERROR and
              r.errtext() == "The MP3 changed meanwhile",
              "state %d '%s'" % (s["state"], r.errtext()))
        check(leg, "nothing plays", s["md"] == MD_IDLE)
        r.close()
        names = r.mdir(r.hds[0])
        check(leg, "no .TMP and no .WAV on C:",
              not any(".TMP" in n or ".WAV" in n for n in names), str(names))
    finally:
        r.done()


def ix_records(dat):
    return [dat[8 + i * 48:8 + i * 48 + 48] for i in range(16)]


def leg_dfdel():
    """Decoded Files deletes only what XTMP3 made (SPEC.md 121.7): a record
    whose file is XTMP3's goes with its file; a record that names someone
    else's WAV (its chunk is not xtm3) goes, and the file STAYS; a file no
    record names is never touched"""
    leg = "dfdel"
    name = "AFTERHRS.MP3"
    r = Rig(leg, floppy=[fixture(name)], hd=[(HD0_GEO, [], [], 0)])
    keep = r.tmp
    try:
        if not check(leg, "XTMP3 opens (boot 1)", r.open_file(
                "B", name, floppy_listing([name]))):
            return
        conv_flow(r, leg)
        r.key("esc")
        r.until(lambda: r.st()["md"] == MD_IDLE, "the stop", 60)
        r.close()
        hd0, bimg = r.hds[0], os.path.join(keep, "b.img")
        wav = bytearray(r.mcat(hd0, "::AFTERHRS.WAV"))
        other = bytearray(wav)
        other[36:40] = b"xtmX"              # someone else's WAV: not XTMP3's
        po, pk = os.path.join(keep, "OTHER.WAV"), os.path.join(keep, "KEEP.TXT")
        open(po, "wb").write(other)
        open(pk, "wb").write(b"not XTMP3's\r\n")
        for f in (po, pk):
            subprocess.run(["mcopy", "-n", "-i", part(hd0), f, "::"],
                           check=True)
        dat = bytearray(subprocess.run(["mcopy", "-n", "-i", bimg,
                                        "::SYSTEM/APPDATA/XTMP3.DAT", "-"],
                                       capture_output=True).stdout)
        rec = bytearray(ix_records(dat)[0])
        rec[24:37] = b"OTHER.WAV".ljust(13, b"\0")
        dat[8 + 48:8 + 96] = rec            # a second record, naming it
        pd = os.path.join(keep, "XTMP3.DAT")
        open(pd, "wb").write(dat)
        subprocess.run(["mcopy", "-o", "-n", "-i", bimg, pd,
                        "::SYSTEM/APPDATA/XTMP3.DAT"], check=True)
        r2 = Rig(leg, keep=keep)
        try:
            if not check(leg, "XTMP3 opens (boot 2)", r2.open_file(
                    "B", name, floppy_listing([name]))):
                return
            # THE PLAY ITSELF, waited for: md IDLE and state IDLE are also
            # how the window opens, before the cache is even looked at, so
            # this returned at once, the Esc met nothing, the bracket began
            # under the Decoded Files window and took its Enter as 'back to
            # the desktop, paused' - no alert ever asked (dfdel, 'timed out
            # waiting for the alert')
            r2.until(lambda: r2.st()["md"] == MD_SPK, "the cache's play", 60)
            r2.key("esc")
            r2.until(lambda: r2.st()["md"] == MD_IDLE, "the stop", 30)
            for k, what in ((0, "AFTERHRS.WAV, XTMP3's"),
                            (1, "OTHER.WAV, not XTMP3's")):
                if not check(leg, "File > Decoded Files", r2.menu_pick(
                        "File", 4)):
                    return
                r2.until(lambda: r2.b("x3c_dmode") == 3, "the window", 10)
                sel = r2.b("x3c_fsel")
                check(leg, "the list's first record is selected (%s)" % what,
                      sel == k, "x3c_fsel %d" % sel)
                r2.key("ret")               # Enter: Delete (button 1)...
                gpace(1.5)
                if not check(leg, "...asks first; Proceed", r2.alert_button(1)):
                    return
                gpace(2.5)
                r2.key("esc")               # (the window: Close)
                gpace(1)
        finally:
            r2.close()
        names = r.mdir(hd0)
        check(leg, "XTMP3's own WAV is deleted",
              not any("AFTERHRS.WAV" in n for n in names), str(names))
        check(leg, "someone else's WAV, though a record named it, STAYS",
              any("OTHER.WAV" in n for n in names), str(names))
        check(leg, "a file no record names is untouched",
              any("KEEP.TXT" in n for n in names), str(names))
        dat = subprocess.run(["mcopy", "-n", "-i", bimg,
                              "::SYSTEM/APPDATA/XTMP3.DAT", "-"],
                             capture_output=True).stdout
        used = [x[46] for x in ix_records(dat)] if dat else []
        check(leg, "both records are gone from XTMP3.DAT", not any(used),
              str(used))
    finally:
        shutil.rmtree(keep, ignore_errors=True)


def leg_batch():
    """an APPROVED BATCH (SPEC.md 121.7): the first track's drive dialog with
    'Queue too: Yes', and the second converts into the same folder with no
    dialog at all"""
    leg = "batch"
    a, b = "AFTERHRS.MP3", "X11K24.MP3"
    lst = floppy_listing([a, b])
    r = Rig(leg, floppy=[fixture(a), fixture(b)], hd=[(HD0_GEO, [], [], 0)])
    try:
        if not check(leg, "XTMP3 opens on the first", r.open_file("B", a, lst)):
            return
        drive_dialog(r, "its dialog")
        r.key("esc")
        r.until(lambda: r.st()["md"] == MD_IDLE, "the cancel", 20)
        if not check(leg, "File > Add to Queue", r.menu_pick("File", 1)):
            return
        r.until(lambda: struct.unpack("<H", r.read(S("fdlg_win"), 2))[0],
                "the Open dialog", 20)
        wp = struct.unpack("<H", r.read(S("fdlg_win"), 2))[0]
        wx, wy = struct.unpack("<HH", r.read(geom.KERNEL_SEG * 16 + wp + 2, 4))
        gpace(1.5)                     # its listing: B:, as the Disk
        os88qemu.mouse_dblclick(r, S, r.hmp, wx + 70,   # window lists it, the second MP3
                         wy + 47 + 16 * lst.index(b))
        r.until(lambda: r.b("x3_qn") == 2, "the queue of two", 20)
        check(leg, "two tracks queued", r.b("x3_qn") == 2, str(r.b("x3_qn")))
        r.key("spc")                        # the first, played: its dialog
        drive_dialog(r)
        r.key("q")                          # 'Queue too: Yes'
        gpace(0.8)
        check(leg, "'Queue too: Yes'", r.b("x3c_bqueue") == 1)
        r.key("ret")
        save_dialog(r)
        r.key("ret")
        conv_end(r, "the first conversion")
        check(leg, "the first plays", r.st()["state"] == ST_FILE)
        asked = []

        def second():                   # (twice, 0.6 s apart: one QMP read
            s = r.st()                  # can catch x3_cue1 between its CUR
            return s["state"] == ST_DONE or (   # and its STATE - the first
                s["state"] == ST_FILE and s["cur"] == 1)   # track's FILE
        r.until(lambda: (asked.append(r.st()["md"]) or
                         (second() and (gpace(0.6) or second()))),
                "the second", 400, 0.3)
        s = r.st()
        check(leg, "the second converted and plays with NO dialog",
              MD_CHOOSE not in asked and s["cur"] == 1 and
              s["state"] in (ST_FILE, ST_DONE),
              "state %d cur %d, modes %s" % (s["state"], s["cur"],
                                             sorted(set(asked))))
        r.key("esc")
        r.until(lambda: r.st()["md"] == MD_IDLE, "the stop", 30)
        r.close()
        names = r.mdir(r.hds[0])
        check(leg, "both WAVs are on C:, no .TMP",
              any("AFTERHRS.WAV" in n for n in names) and
              any("X11K24.WAV" in n for n in names) and
              not any(".TMP" in n for n in names), str(names))
    finally:
        r.done()


def leg_rovol():
    """a READ-ONLY fixed volume: QEMU's IDE has no write-protect for a hard
    disk, so the one this machine HAS - a CD-ROM through CDROM.DRV (SPEC.md
    119), VK_FIXED with no write-at - is in the drive dialog, greyed
    'read-only', and Decode and Play refuses it"""
    leg = "rovol"
    name = "AFTERHRS.MP3"
    iso = os.path.join(ROOT, "build", "x3ro%d.iso" % os.getpid())
    subprocess.run([sys.executable, "tools/os88cdiso.py", "iso", "-o", iso,
                    "--volid", "X3RO", "--file",
                    "README.TXT=tests/cdfix/README.TXT"], check=True,
                   capture_output=True)
    cfgb = (b"O88CFG\0\0" + (3).to_bytes(2, "little") + b"DW" +
            bytes([1, 2]) + (1 << 8).to_bytes(2, "little") + b"\0\0")
    r = Rig(leg, floppy=[fixture(name)], hd=[(HD0_GEO, [], [], 0)],
            syscfg=cfgb, extra=["-drive", "file=%s,format=raw,if=ide,"
                                "index=2,media=cdrom" % iso])
    try:
        if not check(leg, "XTMP3 opens", r.open_file("B", name,
                                                      floppy_listing([name]))):
            return
        drive_dialog(r)
        # ...and its LIST, which the dialog fills after the mode flips
        r.until(lambda: r.b("x3c_nv") >= 2, "the drive dialog's volumes",
                15, 0.1)
        nv = r.b("x3c_nv")
        vv, vf = list(r.rd("x3c_vv", nv)), list(r.rd("x3c_vf", nv))
        print("     volumes %s flags %s" % (["%c:" % (65 + v) for v in vv], vf))
        ro = [i for i in range(nv) if vf[i] & 1]
        check(leg, "a read-only fixed volume (the CD) is listed, flagged so",
              nv == 2 and len(ro) == 1 and not vf[1 - ro[0]] & 1, str(vf))
        if ro:
            while r.b("x3c_vsel") != ro[0]:
                r.key("down" if r.b("x3c_vsel") < ro[0] else "up")
                gpace(0.5)
            r.key("ret")
            gpace(2)
            check(leg, "Decode and Play is greyed on it (Enter does nothing)",
                  r.st()["md"] == MD_CHOOSE and not r.b("x3c_savepend"))
    finally:
        r.done()
        if os.path.exists(iso):
            os.unlink(iso)


def straddle_mp3(path):
    """AFTERHRS.MP3's frames behind an ID3v2 tag that ends 200 bytes short of
    32 KB - so on a 32 KB-cluster volume the probe's sync, which needs the
    first header and the one after it, asks across the cluster's end. TIT2
    'STRADDLE' lies in the tag's first bytes, where the title is read"""
    audio = open(fixture("AFTERHRS.MP3"), "rb").read()
    st = x3sim_mod().Stream(audio)
    audio = audio[st.start:]
    size = 32768 - 200 - 10
    tit = b"\x03STRADDLE"
    frame = b"TIT2" + struct.pack(">I", len(tit)) + b"\0\0" + tit
    body = frame + bytes(size - len(frame))
    ss = bytes([(size >> 21) & 127, (size >> 14) & 127, (size >> 7) & 127,
                size & 127])
    data = b"ID3\x03\x00\x00" + ss + body + audio
    open(path, "wb").write(data)
    return x3sim_mod().Stream(data)


def x3sim_mod():
    import x3sim
    return x3sim


def leg_probe32():
    """the queue's probe on a 32 KB-cluster volume (STD.md, the integration
    track): a request that runs past one cluster is answered (the tail moved
    down in front of the next): the title and the duration are read. Play
    then refuses the MP3 by name - its look-ahead would need two clusters"""
    leg = "probe32"
    t = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    mp = os.path.join(t, "STRADDLE.MP3")
    st = straddle_mp3(mp)
    want = int(st.info["duration"] + 0.5)
    r = Rig(leg, stub=False, floppy=[os.path.join(FIX, "SILENT.MP3")],
            hd=[({"mb": 140, "spc": 64}, [mp], [], 0)])
    try:
        if not check(leg, "XTMP3 opens on C:'s STRADDLE.MP3 (32 KB clusters)",
                     r.open_file("C", "STRADDLE.MP3", ["STRADDLE.MP3"])):
            return
        r.until(lambda: r.st()["state"] in (ST_ERROR, ST_READY, ST_CHECK),
                "the probe and the play", 30)
        q = r.rd("x3_q", 60)
        title = q[19:60].split(b"\0")[0].decode("latin-1")
        dur = struct.unpack_from("<H", q, 16)[0]
        check(leg, "the probe read its title across the cluster's end",
              title == "STRADDLE", repr(title))
        check(leg, "...and its (estimated) duration, x3sim's", dur == want,
              "%d s, x3sim %d" % (dur, want))
        r.until(lambda: r.st()["state"] == ST_ERROR, "the refusal", 20)
        check(leg, "Play refuses it by name: 'Its disk's clusters are too "
              "large'", r.errtext() == "Its disk's clusters are too large",
              repr(r.errtext()))
    finally:
        r.done()
        shutil.rmtree(t, ignore_errors=True)


def leg_shortmem():
    """the STATED MINIMUM (SPEC.md 121.10, 24.5): an XT-class machine whose
    memory cannot hold the conversion's rung is told so in figures before a
    drive is asked for - not a bare 'Out of memory' after it. The machine's
    memory is the test hook's (X3STUB.CFG: 300 KB)"""
    leg = "shortmem"
    r = Rig(leg, sb=True, xcfg=cfg([0, 5, 5, 10, 80, 100], has=300),
            hd=[(HD0_GEO, [fixture(NEON)], ["SYSTEM/APPDATA"], 0)])
    try:
        if not check(leg, "XTMP3 opens", r.open_file("C", NEON,
                                                      hd_listing([NEON]))):
            return
        seen = []
        r.until(lambda: seen.append(r.st()["md"]) or
                r.st()["state"] == ST_ERROR, "the refusal", 60, 0.1)
        check(leg, "ERROR with the figures: 'Needs 390 KB, has 300 KB'",
              r.st()["state"] == ST_ERROR and
              r.errtext() == "Needs 390 KB, has 300 KB",
              "state %d '%s'" % (r.st()["state"], r.errtext()))
        check(leg, "...and no drive was asked for", MD_CHOOSE not in seen,
              str(sorted(set(seen))))
    finally:
        r.done()


def leg_rungfail():
    """A RUNG THAT CANNOT START IS A RUNG THAT FAILED: Original calibrates
    (the stub at 4x) but its live start is refused as out of memory (the
    test hook) - the ladder goes on, to Half, and plays; never an error"""
    leg = "rungfail"
    r = Rig(leg, sb=True, xcfg=cfg([0, 400, 400, 400, 400, 400],
                                   failr=Q_ORIG),
            hd=[(HD0_GEO, [fixture(NEON)], ["SYSTEM/APPDATA"], 0)])
    try:
        if not check(leg, "XTMP3 opens", r.open_file("C", NEON,
                                                      hd_listing([NEON]))):
            return
        states = []
        r.until(lambda: states.append(r.st()["state"]) or
                r.st()["md"] == MD_LIVE, "live", 60, 0.1)
        s = r.st()
        check(leg, "plays LIVE on HALF", s["md"] == MD_LIVE and
              s["qual"] == Q_HALF, "md %d qual %s '%s'" % (
                  s["md"], QNAME[s["qual"]], r.errtext()))
        check(leg, "...Original stored as failed (0) for the session",
              r.meas()[Q_ORIG] == 0, str(r.meas()))
        check(leg, "...and no ERROR on the way", ST_ERROR not in states,
              str(sorted(set(states))))
    finally:
        r.done()


def leg_packed():
    """an MP3 STORED PACKED (an lz4 'CZ' wrapper: READ_SEQ hands over the
    wrapper, SPEC.md 20.14.3) is refused by name, never decoded as noise"""
    leg = "packed"
    t = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    pk = os.path.join(t, "PACKED.MP3")
    subprocess.run([sys.executable, "tools/os88lz.py", "--wrap", pk,
                    fixture("AFTERHRS.MP3")], check=True, capture_output=True)
    r = Rig(leg, stub=False, floppy=[pk], hd=[(HD0_GEO, [], [], 0)])
    try:
        if not check(leg, "XTMP3 opens on it", r.open_file(
                "B", "PACKED.MP3", floppy_listing(["PACKED.MP3"]))):
            return
        r.until(lambda: r.st()["state"] == ST_ERROR, "the refusal", 30)
        check(leg, "ERROR: 'The MP3 is stored packed'",
              r.st()["state"] == ST_ERROR and
              r.errtext() == "The MP3 is stored packed",
              "state %d '%s'" % (r.st()["state"], r.errtext()))
        check(leg, "...and no decoder was fetched for it",
              list(r.rd("x3_nfetch", 5))[2:4] == [0, 0])
    finally:
        r.done()
        shutil.rmtree(t, ignore_errors=True)


def leg_covox():
    """the finished file's ACTUAL SOUND with no card. QEMU's pcspk renders
    only a PIT mode-3 square (its capture of the speaker's PWM bracket is
    EMPTY, measured), so the speaker cannot be heard there; the Covox can,
    exactly: the same bracket (apspk.inc, SPEC.md 86.21.3) copies the WAV's
    samples to the LPT data port one for one, and QEMU traces every byte
    written there (parallel_ioport_write). The shipped package, the real
    STANDARD decoder: the WAV it finished must be, sample for sample, what
    the Covox was given"""
    leg = "covox"
    name = "AFTERHRS.MP3"
    tr = os.path.join(ROOT, "build", "x3cvx%d.log" % os.getpid())
    cfgb = (b"O88CFG\0\0" + (3).to_bytes(2, "little") + b"DW" +
            bytes([1, 2]) + (1).to_bytes(2, "little") + b"SR" +
            bytes([1, 1, 4]) + b"\0\0")      # SOUND.DRV, the Covox on LPT1
    r = Rig(leg, stub=False, floppy=[fixture(name)],
            hd=[(HD0_GEO, [], [], 0)], syscfg=cfgb,
            extra=["-parallel", "null", "-trace",
                   "enable=parallel_ioport_write,file=%s" % tr])
    try:
        if not check(leg, "XTMP3 opens (no card: a Covox)", r.open_file(
                "B", name, floppy_listing([name]))):
            return
        conv_flow(r, leg)
        check(leg, "the finished file plays through the bracket",
              r.st()["md"] == MD_SPK and r.st()["state"] == ST_FILE,
              "md %d state %d %s" % (r.st()["md"], r.st()["state"],
                                     r.errtext()))
        g0 = r.w("x3_grans")                # QEMU runs the bracket's 11 kHz
        time.sleep(45)                      # IRQ0 slowly (~700 samples a
        check(leg, "no granule decoded while it plays",   # wall second):
              r.w("x3_grans") == g0)        # what it played in 45 s is held
        r.key("esc")
        r.until(lambda: r.st()["md"] == MD_IDLE, "the stop", 60)
    finally:
        r.close()
    try:
        data = r.mcat(r.hds[0], "::AFTERHRS.WAV")
        if not check(leg, "the WAV is on C:", data is not None):
            return
        dlen = struct.unpack_from("<I", data, 2048 - 4)[0]
        pcm = data[2048:2048 + dlen]
        out = bytearray()
        for ln in open(tr, errors="replace"):
            m = re.search(r"parallel_ioport_write.*addr 0x0+ val 0x([0-9a-f]+)",
                          ln)
            if m:
                out.append(int(m.group(1), 16))
        k = bytes(out).find(pcm[:256])
        same = 0
        if k >= 0:                          # the longest run from the head
            got = bytes(out[k:k + len(pcm)])
            same = next((i for i in range(len(got)) if got[i] != pcm[i]),
                        len(got))
        check(leg, "the Covox was given the WAV's samples, in order, one for "
              "one, from its first (at least two seconds of them)",
              k >= 0 and same >= 2 * 11025,
              "%d bytes written to 378h; the WAV's head %s, %d of its %d "
              "bytes identical from there" % (
                  len(out), "at %d" % k if k >= 0 else "nowhere", same,
                  len(pcm)))
    finally:
        if os.path.exists(tr):
            os.unlink(tr)
        r.done()


def captured_tone(path):
    """QEMU's wav audiodev, which may not have finished its header: 44,100 Hz
    16-bit stereo after the 44 bytes. The dominant frequency of a second"""
    import numpy as np
    raw = open(path, "rb").read()[44:]
    d = np.frombuffer(raw[:len(raw) // 4 * 4], dtype="<i2").reshape(-1, 2)
    nz = np.nonzero(np.abs(d[:, 0]) > 1000)[0]
    if len(nz) < 44100:
        return (0, 0)
    seg = d[nz[0]:nz[0] + 44100, 0].astype(float)
    sp = np.abs(np.fft.rfft(seg))
    return (int(np.argmax(sp[1:]) + 1), (nz[-1] - nz[0]) / 44100.0)


def leg_pause():
    leg = "pause"
    r = Rig(leg, sb=True, xcfg=cfg([0, 400, 400, 400, 400, 400]),
            hd=[(HD0_GEO, [fixture(NEON)], ["SYSTEM/APPDATA"], 0)])
    try:
        if not check(leg, "XTMP3 opens", r.open_file("C", NEON,
                                                      hd_listing([NEON]))):
            return
        r.until(lambda: r.st()["md"] == MD_LIVE and r.st()["elap"] >= 3,
                "three seconds live", 60)
        r.key("spc")
        r.until(lambda: r.st()["state"] == ST_PAUSED, "the pause", 10, 0.1)
        e1 = r.st()["elap"]
        check(leg, "Pause: PAUSED, the stream closed",
              r.st()["state"] == ST_PAUSED and not r.b("x3_sopen"))
        gpace(2)
        check(leg, "the clock stands", r.st()["elap"] == e1)
        r.key("spc")
        r.until(lambda: r.st()["md"] == MD_LIVE, "the resume", 10, 0.1)
        e2 = r.st()["elap"]
        check(leg, "Resume: LIVE from where it paused", e1 <= e2 <= e1 + 1,
              "%d -> %d" % (e1, e2))
        r.key("comma")                  # back ten seconds: the start
        r.until(lambda: r.st()["elap"] <= 1, "the rewind", 10, 0.1)
        check(leg, "Rewind seeks to the start", r.st()["elap"] <= 1,
              str(r.st()["elap"]))
        r.until(lambda: r.st()["state"] == ST_DONE, "the end", 60)
        check(leg, "plays to the end, no underrun",
              r.st()["state"] == ST_DONE and r.st()["under"] == 0,
              "under %d" % r.st()["under"])
    finally:
        r.done()


def _stall_thread(tid, secs):
    """Stop ONE THREAD of another process for `secs` of host time (ptrace
    SEIZE + INTERRUPT on its tid; the process's other threads run on).
    False where that cannot be done (no Linux ptrace, or not allowed)."""
    import ctypes
    try:
        libc = ctypes.CDLL(None, use_errno=True)
        libc.ptrace.argtypes = [ctypes.c_long, ctypes.c_long,
                                ctypes.c_void_p, ctypes.c_void_p]
    except (OSError, AttributeError):
        return False
    if not sys.platform.startswith("linux") or \
            libc.ptrace(0x4206, tid, None, None) != 0:      # PTRACE_SEIZE
        return False
    try:
        libc.ptrace(0x4207, tid, None, None)                # PTRACE_INTERRUPT
        st = ctypes.c_int()
        libc.waitpid(tid, ctypes.byref(st), 0x40000000)     # __WALL
        time.sleep(secs)
    finally:
        libc.ptrace(17, tid, None, None)                    # PTRACE_DETACH
    return True


def leg_mainstall():
    """THE CARD ON THE GUEST'S CLOCK: QEMU's MAIN-LOOP thread - and only it
    - stopped for 300 ms of host time, three times, during a live play on
    the SB16. Stock QEMU moves the 8237's transfers on that loop in HOST
    time while the vCPU spends guest time without it, so ONE such stall is
    SOUND.DRV's watchdog and 'The sound card stopped' (what a loaded soak
    did to the pause leg); on `qemuvt` (tools/qemu9/i8257-icount.patch) the
    transfers are a virtual-clock timer and the play ends clean
    (docs/plans/SOAK-PARALLEL.md 17.8.1). OS88_QEMUVT=0 is the A/B"""
    leg = "mainstall"
    r = Rig(leg, sb=True, xcfg=cfg([0, 400, 400, 400, 400, 400]),
            hd=[(HD0_GEO, [fixture(NEON)], ["SYSTEM/APPDATA"], 0)])
    try:
        if not check(leg, "XTMP3 opens", r.open_file("C", NEON,
                                                      hd_listing([NEON]))):
            return
        r.until(lambda: r.st()["md"] == MD_LIVE and r.st()["elap"] >= 2,
                "two seconds live", 60)
        states = []
        for _ in range(3):
            if not _stall_thread(r.pid, 0.3):
                print("  NOTE  %s: cannot stop one thread of QEMU here (no "
                      "Linux ptrace) - not checked" % leg)
                return
            states.append(r.st()["state"])
            gpace(1)
        r.until(lambda: states.append(r.st()["state"]) or
                r.st()["state"] in (ST_DONE, ST_ERROR), "the end", 60, 0.2)
        s = r.st()
        check(leg, "three 300 ms main-loop stalls: plays to the end, no "
              "ERROR, no underrun", s["state"] == ST_DONE and
              ST_ERROR not in states and s["under"] == 0,
              "state %d under %d '%s' %s" % (s["state"], s["under"],
                                             r.errtext(),
                                             sorted(set(states))))
    finally:
        r.done()


def leg_offvol():
    """THE PACKAGE ON B:, THE SONG ON C: (the docs track's report): a lazy
    part is read from XTMP3.O88 by NAME, and a name resolves where the
    instance stands - the track's folder once a track is open - so the fetch
    must stand in the folder XTMP3 was launched from (x3_fetchhome). Before
    it, Auto's probe could not fetch STANDARD, went to Decode First and said
    'Needs 360 KB, has 571 KB', a memory figure for a disk refusal. The
    SHIPPED package with a card: it plays live and the part was fetched;
    then a forced XT Standard converts onto C:"""
    leg = "offvol"
    t = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    note = os.path.join(t, "NOTE.TXT")
    open(note, "wb").write(b"the package's disk\r\n")
    r = Rig(leg, sb=True, stub=False, floppy=[note],
            hd=[(HD0_GEO, [fixture(NEON)], ["SYSTEM/APPDATA"], 0)])
    try:
        if not check(leg, "XTMP3 (on B:) opens C:'s song", r.open_file(
                "C", NEON, hd_listing([NEON]))):
            return
        states = []
        r.until(lambda: states.append(r.st()["state"]) or
                r.st()["md"] in (MD_LIVE, MD_CHOOSE) or
                r.st()["state"] == ST_ERROR, "Auto's decision", 90, 0.2)
        s = r.st()
        check(leg, "plays LIVE (no Decode First, no ERROR)",
              s["md"] == MD_LIVE and ST_ERROR not in states,
              "md %d qual %s '%s'" % (s["md"], QNAME[s["qual"]],
                                       r.errtext()))
        nf = list(r.rd("x3_nfetch", 5))
        check(leg, "...STANDARD's part was fetched from B:", nf[2] >= 1,
              "fetches %s" % nf)
        r.key("s")                          # then XT STANDARD, forced, and
        r.until(lambda: r.st()["md"] == MD_IDLE, "the stop", 20)
        r.override_to(Q_XTSTD)          # Decode First onto C: - the XT
                                        # part fetched from B: as well
        r.key("d")
        conv_flow(r, leg)
        s = r.st()
        nf = list(r.rd("x3_nfetch", 5))
        check(leg, "a forced XT STANDARD converts and plays, the XT part "
              "fetched from B:", s["state"] == ST_FILE and nf[3] >= 1,
              "state %d md %d '%s', fetches %s" % (
                  s["state"], s["md"], r.errtext(), nf))
    finally:
        r.done()
        shutil.rmtree(t, ignore_errors=True)


def leg_qagain():
    """A QUEUED TRACK WHOSE PROBE WAS NOT HAD (the docs track's Hercules
    report): on QEMU's relocated Hercules the heap beside a live XT stream
    had no room for the probe's 8 KB, and the row kept its file's stem for
    good. Here the test hook refuses the probe while live: the row shows the
    stem, is flagged QEF_AGAIN, and the title arrives once the player is
    idle (x3_qagain)"""
    leg = "qagain"
    a, b = NEON, "NIGHTSIG.MP3"
    lst = floppy_listing([a, b])
    r = Rig(leg, sb=True, xcfg=cfg([0, 400, 400, 400, 400, 400], qfail=1),
            floppy=[fixture(a), fixture(b)], hd=[(HD0_GEO, [], [], 0)])
    try:
        if not check(leg, "XTMP3 opens on the first", r.open_file("B", a, lst)):
            return
        r.until(lambda: r.st()["md"] == MD_LIVE, "live", 60, 0.1)
        if not check(leg, "File > Add to Queue", r.menu_pick("File", 1)):
            return
        r.until(lambda: struct.unpack("<H", r.read(S("fdlg_win"), 2))[0],
                "the Open dialog", 20)
        wp = struct.unpack("<H", r.read(S("fdlg_win"), 2))[0]
        wx, wy = struct.unpack("<HH", r.read(geom.KERNEL_SEG * 16 + wp + 2, 4))
        gpace(1.5)
        os88qemu.mouse_dblclick(r, S, r.hmp, wx + 70, wy + 47 + 16 * lst.index(b))
        r.until(lambda: r.b("x3_qn") == 2, "the queue of two", 20)
        def row():                          # entry 1 (xtmp3.asm's QE_*: 60
            raw = r.rd("x3_q", 2 * 60)[60:]  # bytes, flags 18, title 19)
            t = raw[19:60].split(b"\0")[0].decode("ascii", "replace")
            return t, raw[18]
        t, fl = row()
        check(leg, "queued while live: the stem, flagged to ask again",
              r.st()["md"] == MD_LIVE and t == "NIGHTSIG" and fl & 0x40,
              "md %d '%s' flags %02x" % (r.st()["md"], t, fl))
        r.key("s")
        r.until(lambda: r.st()["md"] == MD_IDLE, "the stop", 30)
        r.until(lambda: row()[0] != "NIGHTSIG", "the title, idle", 60, 0.2)
        t, fl = row()
        check(leg, "idle: asked again, the tag's title", t == "NIGHT SIGNALS"
              and not fl & 0x40, "'%s' flags %02x" % (t, fl))
    finally:
        r.done()


def leg_offdir():
    """THE SAME DRIVE, ANOTHER FOLDER (the owner's 86Box XT: XTMP3 in
    C:\\XTMP3, the song in C:\\MEDIA, 'Needs 330K has 520K'): the package at
    C:'s root, the song in C:\\MEDIA, queued through File > Add to Queue,
    whose dialog opens in MEDIA (SPEC.md 38.10). Played, Auto fetches
    STANDARD and plays live - a fetch that follows the carve's read rides
    os88_rseq's cursor and never looks the name up - and a forced XT
    Standard, whose part is read by NAME, converts"""
    leg = "offdir"
    r = Rig(leg, sb=True, stub=False,
            hd=[(HD0_GEO, ["MEDIA:" + fixture(NEON)],
                 ["SYSTEM/APPDATA", "MEDIA"], 0)])
    try:
        lst = ["MEDIA", "SYSTEM", "X3STUB.CFG", "XTMP3.O88"]
        if not check(leg, "XTMP3 opens from C:'s root",
                     r.open_file("C", "XTMP3.O88", lst)):
            return
        if not check(leg, "File > Add to Queue", r.menu_pick("File", 1)):
            return
        r.until(lambda: struct.unpack("<H", r.read(S("fdlg_win"), 2))[0],
                "the Open dialog", 20)
        wp = struct.unpack("<H", r.read(S("fdlg_win"), 2))[0]
        wx, wy = struct.unpack("<HH", r.read(geom.KERNEL_SEG * 16 + wp + 2, 4))
        gpace(1.5)
        os88qemu.mouse_dblclick(r, S, r.hmp, wx + 70, wy + 47 + 16)  # '..', then the song
        r.until(lambda: r.b("x3_qn") == 1, "the queue of one", 20)
        if not check(leg, "C:\\MEDIA\\%s queued" % NEON, r.b("x3_qn") == 1):
            return
        r.key("spc")
        states = []
        r.until(lambda: states.append(r.st()["state"]) or
                r.st()["md"] in (MD_LIVE, MD_CHOOSE) or
                r.st()["state"] == ST_ERROR, "Auto's decision", 90, 0.2)
        s = r.st()
        nf = list(r.rd("x3_nfetch", 5))
        check(leg, "plays LIVE, STANDARD fetched from C:\\ (no Decode First, "
              "no ERROR)", s["md"] == MD_LIVE and ST_ERROR not in states and
              nf[2] >= 1, "md %d qual %s '%s', fetches %s" % (
                  s["md"], QNAME[s["qual"]], r.errtext(), nf))
        r.key("s")                          # the XT part: its fetch does not
        r.until(lambda: r.st()["md"] == MD_IDLE, "the stop", 20)
        r.override_to(Q_XTSTD)          # follow the carve's read, so the
                                        # file is found by NAME (the
                                        # owner's case: 'Needs 330 KB')
        r.key("d")
        conv_flow(r, leg)
        s = r.st()
        nf = list(r.rd("x3_nfetch", 5))
        check(leg, "a forced XT STANDARD converts and plays, the XT part "
              "fetched from C:\\", s["state"] == ST_FILE and nf[3] >= 1,
              "state %d md %d '%s', fetches %s" % (
                  s["state"], s["md"], r.errtext(), nf))
    finally:
        r.done()


def leg_wavdirs():
    """A FINISHED WAV PLAYED TO ITS END from the owner's layout: XTMP3 in
    C:\\XTMP3, the song in C:\\MEDIA, the WAV saved to C:\\ - a third
    folder. An XT Standard conversion, its play, then the song again
    through the cache: every byte of the WAV's data reaches the card, no
    underrun, and Finished at the total. (The owner's 86Box XT played one
    ring of it: its SB 2.0 shared IRQ 5 with the fixed disk, and the card
    stopped - which XTMP3 now says, 'The sound card stopped'; QEMU's SB16
    has no such conflict, so this leg is the layout's half)"""
    leg = "wavdirs"
    r = Rig(leg, sb=True, stub=False, pkgdir="XTMP3",
            hd=[(HD0_GEO, ["MEDIA:" + fixture("AFTERHRS.MP3")],
                 ["SYSTEM/APPDATA", "MEDIA", "XTMP3"], 0)])

    def played():
        return struct.unpack("<I", r.rd("x3_played", 4))[0]

    def dlen():
        e, o = (struct.unpack("<I", r.rd(n, 4))[0]
                for n in ("ap_data_end", "ap_data_off"))
        return e - o

    def to_end(what):
        r.until(lambda: r.st()["state"] == ST_DONE, what + " to its end",
                60, 0.3)
        s = r.st()
        check(leg, what + ": every byte to the card, no underrun, Finished",
              s["state"] == ST_DONE and played() >= dlen() > 0 and
              s["under"] == 0 and s["elap"] == s["total"],
              "state %d, %d of %d bytes, %d underruns, %d of %d s '%s'" % (
                  s["state"], played(), dlen(), s["under"], s["elap"],
                  s["total"], r.errtext()))
    try:
        os88qemu.mouse_dblclick(r, S, r.hmp, ICON_X, DRIVE_Y["C"])
        gpace(2.5)
        os88qemu.mouse_dblclick(r, S, r.hmp, ROW_X, ROW_Y0 + ROW_H * 2)    # XTMP3
        gpace(2.5)
        for _ in range(3):                  # '..', X3STUB.CFG, XTMP3.O88
            os88qemu.mouse_dblclick(r, S, r.hmp, ROW_X, ROW_Y0 + ROW_H * 2)
            if r.until(lambda: r.x3seg() is not None, "XTMP3's window", 20):
                r.seg = r.x3seg()
                break
        if not check(leg, "XTMP3 opens from C:\\XTMP3", r.seg is not None):
            return
        if not check(leg, "File > Add to Queue", r.menu_pick("File", 1)):
            return
        r.until(lambda: struct.unpack("<H", r.read(S("fdlg_win"), 2))[0],
                "the Open dialog", 20)
        wp = struct.unpack("<H", r.read(S("fdlg_win"), 2))[0]
        wx, wy = struct.unpack("<HH", r.read(geom.KERNEL_SEG * 16 + wp + 2, 4))
        gpace(1.5)                     # C:\MEDIA: '..', the song
        os88qemu.mouse_dblclick(r, S, r.hmp, wx + 70, wy + 47 + 16)
        r.until(lambda: r.b("x3_qn") == 1, "the queue of one", 20)
        r.key("spc")
        r.until(lambda: r.st()["md"] == MD_LIVE, "live", 60, 0.2)
        r.key("s")
        r.until(lambda: r.st()["md"] == MD_IDLE, "the stop", 20)
        r.override_to(Q_XTSTD)
        r.key("d")
        conv_flow(r, leg)                   # the Save dialog stands in C:\
        s = r.st()
        if not check(leg, "converted into C:\\ and playing",
                     s["state"] == ST_FILE and
                     r.mcat(r.hds[0], "::AFTERHRS.WAV") is not None,
                     "state %d '%s'" % (s["state"], r.errtext())):
            return
        to_end("the finished WAV")
        r.key("q")                          # Auto again: the cache
        gpace(0.4)
        for _ in range(7):
            if r.rd("x3_st", 3)[2] == 0:
                break
            r.key("q")
            gpace(0.4)
        r.key("spc")
        r.until(lambda: r.st()["md"] in (MD_FILE, MD_LIVE, MD_CHOOSE),
                "the second play", 30, 0.2)
        check(leg, "the second play is the cache's WAV", r.st()["md"] ==
              MD_FILE, "md %d" % r.st()["md"])
        to_end("the cached WAV")
    finally:
        r.done()


LEGS = {"ladder": leg_ladder, "slowdown": leg_slowdown, "conv": leg_conv,
        "cancel": leg_cancel, "full": leg_full, "stale": leg_stale,
        "novol": leg_novol, "twovol": leg_twovol, "playwav": leg_playwav,
        "pause": leg_pause, "mainstall": leg_mainstall, "xtpeak": leg_xtpeak, "xtclu": leg_xtclu,
        "chgsrc": leg_chgsrc, "dfdel": leg_dfdel, "batch": leg_batch,
        "rovol": leg_rovol, "packed": leg_packed, "covox": leg_covox,
        "probe32": leg_probe32, "shortmem": leg_shortmem,
        "rungfail": leg_rungfail, "offvol": leg_offvol,
        "qagain": leg_qagain, "offdir": leg_offdir, "wavdirs": leg_wavdirs}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--leg", action="append", choices=sorted(LEGS))
    # ...and the complement: the soak runs the RATE legs (RATE_LEGS) as a
    # row of their own, xtmp3rate, on a fixed-speed guest, and every other
    # leg as xtmp3eng (docs/plans/SOAK-PARALLEL.md 17.8)
    ap.add_argument("--skip", action="append", choices=sorted(LEGS),
                    default=[])
    a = ap.parse_args()
    for f in ("build/xtmp3s.o88", "build/xtmp3s.bin", "build/xtmp3.o88",
              "build/xtmp3.bin", "build/os8088.img",
              "build/mixercfg/SYSTEM.CFG", "build/mbr.bin", "build/boothd.bin",
              "build/kernel.sys"):
        if not os.path.exists(os88build.at(f)):
            sys.exit("xtmp3eng: no %s - make && make xtmp3stub "
                     "build/mixercfg/SYSTEM.CFG" % f)
    legs = [n for n in (a.leg or list(LEGS)) if n not in a.skip]
    if os88qemu.icount() and set(legs) & set(RATE_LEGS):
        # THE CARD'S CLOCK (os88build.qemuvt()): stock QEMU moves the SB16's
        # DMA on its main loop in HOST time, so under -icount a starved main
        # loop stops the card. The runner puts the patched QEMU first on
        # PATH for xtmp3rate (needs `qemuvt`); a hand run does it here
        vt = (os88build.qemuvt() if os.environ.get("OS88_QEMUVT") != "0"
              else None)
        if vt:
            os.environ["PATH"] = (os.path.dirname(vt) + os.pathsep +
                                  os.environ.get("PATH", ""))
        else:
            print("xtmp3eng: NOTE no qemuvt (tools/setup-qemu9.sh) - the "
                  "rate legs' card runs in host time")
    for name in legs:
        _ICOUNT[0] = name in RATE_LEGS
        print("xtmp3eng: %s%s" % (name, " (-icount)" if _ICOUNT[0] and
                                  os88qemu.icount() else ""))
        LEGS[name]()
    if bad:
        print("xtmp3eng: %d FAILED" % len(bad))
        for b in bad:
            print("  " + b)
        return 1
    print("xtmp3eng: every leg passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
