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
MD_IDLE, MD_CHECK, MD_LIVE, MD_FILE, MD_SPK, MD_CHOOSE, MD_CONV, MD_STEP = \
    range(8)
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


def cfg(speeds, slr=0, slaft=0, slspd=0):
    """X3STUB.CFG (apps/xtmp3/x3stub.inc): a speed x 100 by rung"""
    sp = list(speeds) + [0] * (8 - len(speeds))
    return struct.pack("<8HBBHH", *sp, slr, 0, slaft, slspd)


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
class Rig(object):
    def __init__(self, leg, sb=False, hd=(), floppy=(), xcfg=None, keep=None,
                 stub=True):
        """hd: up to two (geometry, files, folders, junk_bytes) - the first is
        C:, the second D:; floppy: B:'s files (with SYSTEM/APPDATA); xcfg:
        X3STUB.CFG's bytes, written beside XTMP3.O88 (B: when the package is
        on the floppy, else C:); keep: a previous Rig's folder to boot again
        with its disks as they are"""
        self.leg = leg
        self.stub = stub                # False: the SHIPPED package
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
        if sb:                          # SYSTEM.CFG wanting SOUND.DRV
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
        for i, (geo, files, folders, junk) in enumerate(hd):
            img = os.path.join(t, "hd%d.img" % i)
            files = list(files)
            if i == 0 and not floppy:
                files = [pkg, cfgf] + files
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
        a = ["qemu-system-i386", "-machine", "pc,vmport=off", "-m", "4",
             "-drive", "file=%s,format=raw,if=floppy" % os.path.join(t, "sys.img"),
             "-drive", "file=%s,format=raw,if=floppy,index=1" % os.path.join(t, "b.img")]
        for i, img in enumerate(self.hds):
            a += ["-drive", "file=%s,format=raw,if=ide,index=%d,media=disk" % (img, i)]
        if sb:
            a += ["-device", "sb16,audiodev=a0", "-audiodev",
                  "wav,id=a0,path=%s" % self.wav]
        a += ["-boot", "a", "-chardev", "msmouse,id=m0", "-serial",
              "chardev:m0", "-display", "none", "-qmp",
              "unix:%s,server,nowait" % self.sock, "-daemonize",
              "-pidfile", self.pidf]
        subprocess.run(a, check=True)
        self.pid = int(open(self.pidf).read().strip())
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

    def key(self, k):
        self.hmp("sendkey " + k)

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
                    md=self.b("x3_md"))

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
        t = time.time()
        while time.time() - t < secs:
            try:
                if cond():
                    return True
            except Exception:                               # noqa: BLE001
                pass
            time.sleep(poll)
        print("     (timed out waiting for %s)" % what)
        return False

    def open_file(self, drive, name, listing):
        """double-click drive's zone, then NAME's row in its Disk window:
        LISTING is the window's visible order (folders first, then names)"""
        xmcheck.dblclick(self.sock, ICON_X, DRIVE_Y[drive])
        time.sleep(2.5)
        y = ROW_Y0 + ROW_H * listing.index(name)
        for _ in range(3):
            xmcheck.dblclick(self.sock, ROW_X, y)
            if self.until(lambda: self.x3seg() is not None, "XTMP3's window",
                          20):
                self.seg = self.x3seg()
                return True
        return False

    seg = None

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


def ladder_case(name, speeds, want, live=True, bound=None, unprobed=()):
    leg = "ladder/" + name
    print(" %s: speeds %s" % (leg, speeds))
    r = Rig(leg, sb=True, xcfg=cfg(speeds),
            hd=[(HD0_GEO, [fixture(NEON)], ["SYSTEM/APPDATA"], 0)])
    try:
        if not check(leg, "XTMP3 opens on the MP3",
                     r.open_file("C", NEON, hd_listing([NEON]))):
            return
        t0 = r.ticks()
        r.until(lambda: r.st()["md"] not in (MD_IDLE, MD_CHECK),
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
    boundary is the step's other trigger (SPEC.md 121.5). Either way ONE
    step, Original to Half, never back up, and the position never goes back"""
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


def conv_flow(r, leg, expect_dialog=True):
    """the drive dialog's Enter, the Save dialog's Enter, the conversion"""
    r.until(lambda: r.st()["md"] == MD_CHOOSE, "the drive dialog", 60, 0.2)
    check(leg, "the drive dialog is up", r.st()["state"] == ST_CHOOSE)
    r.key("ret")
    time.sleep(2.5)
    r.key("ret")
    r.until(lambda: r.st()["md"] not in (MD_CHOOSE, MD_CONV), "the end of "
            "the conversion", 240, 0.3)


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
        r.until(lambda: r.st()["md"] == MD_CHOOSE, "the drive dialog", 90)
        r.key("ret")
        time.sleep(2.5)
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
                r2.until(lambda: r2.st()["md"] not in (MD_IDLE, MD_CHECK),
                         "the decision", 60, 0.1)
                s = r2.st()
                check(leg, "an %s file is not reused: the dialog is asked"
                      % case, s["md"] == MD_CHOOSE, "md %d" % s["md"])
                r2.key("esc")
                time.sleep(1)
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
        r.until(lambda: r.st()["md"] == MD_CHOOSE, "the drive dialog", 90)
        check(leg, "no volume listed", r.b("x3c_nv") == 0)
        r.key("ret")
        time.sleep(2)
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
        r.until(lambda: r.st()["md"] == MD_CHOOSE, "the drive dialog", 90)
        check(leg, "two hard drives listed, C: and D:", r.b("x3c_nv") == 2 and
              list(r.rd("x3c_vv", 2)) == [2, 3], str(list(r.rd("x3c_vv", 2))))
        r.key("down")
        time.sleep(1)
        r.key("ret")
        time.sleep(2.5)
        r.key("ret")
        r.until(lambda: r.st()["md"] not in (MD_CHOOSE, MD_CONV), "the end",
                240, 0.3)
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
        r.until(lambda: r.st()["md"] == MD_CHOOSE, "its dialog", 90)
        vf = list(r.rd("x3c_vf", 2))
        check(leg, "D: is 'too little room' for it, C: is not",
              bool(vf[1] & 2) and not vf[0] & 2, str(vf))
        r.key("down")                   # D: chosen...
        time.sleep(1)
        r.key("ret")                    # ...and Decode and Play refused
        time.sleep(2)
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
        r.until(lambda: r.st()["md"] == MD_CHOOSE, "the decode-first dialog",
                90, 0.2)
        r.key("esc")
        r.until(lambda: r.st()["md"] == MD_IDLE, "the dialog's cancel", 20)
        for _ in range(Q_XTSTD):        # Q: AUTO, ORIGINAL ... XT STANDARD
            r.key("q")
            time.sleep(0.4)
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
    finally:
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
        time.sleep(2)
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


LEGS = {"ladder": leg_ladder, "slowdown": leg_slowdown, "conv": leg_conv,
        "cancel": leg_cancel, "full": leg_full, "stale": leg_stale,
        "novol": leg_novol, "twovol": leg_twovol, "playwav": leg_playwav,
        "pause": leg_pause, "xtpeak": leg_xtpeak}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--leg", action="append", choices=sorted(LEGS))
    a = ap.parse_args()
    for f in ("build/xtmp3s.o88", "build/xtmp3s.bin", "build/xtmp3.o88",
              "build/xtmp3.bin", "build/os8088.img",
              "build/mixercfg/SYSTEM.CFG", "build/mbr.bin", "build/boothd.bin",
              "build/kernel.sys"):
        if not os.path.exists(os88build.at(f)):
            sys.exit("xtmp3eng: no %s - make && make xtmp3stub "
                     "build/mixercfg/SYSTEM.CFG" % f)
    for name in a.leg or list(LEGS):
        print("xtmp3eng: %s" % name)
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
