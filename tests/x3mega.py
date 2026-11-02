#!/usr/bin/env python3
"""x3mega - XT-MP3 under kern_mega with OS88_HIGHMEM declared: its claims
of 64KB or less above 1MB, played and converted (docs/plans/
KERN-MEGA-MEMORY-PLAN.md MC7, SPEC.md 121).

    make && make mega && python3 tests/x3mega.py [--keep] [--leg NAME ...]

Under kern_mega every claim is an LDT selector, and one declared
OS88_HIGHMEM of 64KB or less is a selector over a pool block ABOVE 1MB,
over exactly the KB asked: adding paragraphs to it names some other
descriptor. XT-MP3 did that twice and both were converted in MC7's package
wave: a Decode First's stage, cut out of the look-ahead claim as
ap_la_seg + (x3_lasz + X3_PCMRES) / 16, is an OSAPI_SEG SEG_SUB now
(x3conv.inc, [x3c_sseg]), and a conventional HOLD (x3hold.inc) is read and
copied through x3h_at's SEG_AT window where it was hold + (offset >> 7 <<
3). This row is the kern_mega evidence that retired the package's
`highmem` line in tests/megahigh.txt. Each leg boots kern_mega under Bochs
at 16MB with a Sound Blaster 16 and SOUND.DRV wanted, the kern_mega ARM's
XTMP3.O88 (build/megak/apps/, the build kern_mega's apps disk carries) and
AFTERHRS.MP3 on a B: floppy, and opens the MP3 from B:'s listing:

  play  Auto, the hold where it falls by itself - one XMS block, the pool
        having room. The region, the look-ahead claim (the ring and the PCM)
        are above 1MB; the track plays on the card (MD_LIVE, the stream
        open, granules decoding and frames counting up) with its gulps
        COPIED out of the hold; Stop.
  conv  OSAPI_XMEM_CAPS made to answer 0 for the open (the kernel's cell,
        patched in guest memory and put back once the hold is taken) so the
        hold is a CONVENTIONAL claim - 30 KB, OS88_HIGHMEM, so a pool block
        above 1MB, read and served through x3h_at's window, which must be
        based inside it. Then Stop, XT STANDARD by Q, Decode First (D) onto
        C:: while it converts, the stage [x3c_sseg] is an LDT selector based
        exactly x3_lasz + 2,560 bytes into the look-ahead claim, above 1MB;
        the WAV it writes is tools/x3simxt.py's XT Standard for the file,
        every byte - which is the stage's bytes, since every cluster of the
        file is written out of it - and it then plays on the card.

Both: no `kern_mega CLOSED` (a package's #GP, completed by closing it) or
`kern_mega FAULT` line.

NOT EXERCISED: the XT backend's own speed (Bochs is a 386; nothing here is
timed), a hold over 64KB (conventional on every kernel, so its window is
the same call), the speaker and Covox paths (the card is present), and a
compaction of the pool under a running play.

THE FAILING CASE (docs/WRITING-TESTS.md 1): with the stage's old sum put
back in x3conv.inc (`mov dx, [ap_la_seg] / add dx, ax` for the SEG_SUB),
`conv` fails - the first write out of the stage is a #GP and XT-MP3 is
CLOSED (`kern_mega CLOSED XTMP3 0D`); with x3h_at's SEG_AT dropped for the
sum, `conv` fails the same way at the hold's first read. Both seen when the
row was written.

It reads build/ and writes only to a temporary folder (--keep keeps it).
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [os.path.join(ROOT, "tools"), HERE]
os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "megak")
import os88bochs  # noqa: E402
import os88sym  # noqa: E402

BUILD = os.path.join(ROOT, "build")
PKG = os.path.join(BUILD, "megak", "apps", "xtmp3.o88")
PMAP = os.path.join(BUILD, "megak", "apps", "xtmp3.map")
FIX = os.path.join(ROOT, "tests", "xtmp3")
MP3 = "AFTERHRS.MP3"                    # 30 KB: a hold of 64KB or less
PLAYMP3 = "NEONDRV.MP3"                 # 70 KB: long enough to listen to
DISPLAY = (None if os.environ.get("OS88_BOCHS_DISPLAY")
           else os88bochs.take_display())

# x3eng.inc / x3hold.inc / x3face.inc (tests/xtmp3eng.py's names)
ST_FILE, ST_DONE = 6, 9
MD_IDLE, MD_CHECK, MD_LIVE, MD_FILE, MD_SPK, MD_CHOOSE, MD_CONV, MD_STEP, \
    MD_LOAD = range(9)
Q_XTSTD = 4
X3HK_XMS, X3HK_CONV = 1, 2
X3_PCMRES = 2560
# OSAPI_XMEM_CAPS's cell, KERNEL_SEG:015Ah - at its physical place, the
# kernel's code being at 0060:0 in protected mode as in real mode - and the
# answer "no pool" (AX = 0: the three cells after it all refuse)
CAPS_CELL = 0x600 + 0x15A
CAPS_NONE = bytes((0x31, 0xC0, 0xCB))          # xor ax, ax / retf
ROW0_Y, ROW_X, ROW_H = 123, 170, 16            # a Disk window's rows
# THE PLAY LEG'S CPU. Auto's ladder (x3eng.inc x3l_run) MEASURES each rung's
# decode in audio seconds a guest second and plays live only on one that
# keeps 1.25x - and Bochs's default guest, 4M instructions a guest second
# (docs/BOCHS.md, *The clock*), is slower than the 386DX/25 that already
# takes Decode First on a 128 kbps song (docs/xtmp3/FIELD.md), so there it
# correctly offers the dialog (md 5) and this leg's subject - a LIVE play
# out of an XMS hold - never happens. It passed under `sync=realtime` only
# because that guest ran as fast as the host ran Bochs. 12M already plays
# Quarter live and 25M Half; the measure is guest time either way, so a
# loaded host makes the leg slower and not different
PLAY_IPS = 25000000

FAILS = []


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)
    return cond


def bad_lines(box):
    return box.faults()


class X3(object):
    """XT-MP3's bss, read through the debugger by the arm's own map"""

    def __init__(self, box):
        self.box = box
        self.s = os88sym._parse_map(PMAP)[0]

    def base(self):
        b = self.box.pkg_base("XTMP3")
        if b is None:
            raise RuntimeError("no live XTMP3")
        return b

    def rd(self, *names):
        base = self.base()
        spans = [(base + self.s[n], k) for n, k in names]
        return self.box.reads(spans)

    def w(self, name):
        return struct.unpack("<H", self.rd((name, 2))[0])[0]

    def b(self, name):
        return self.rd((name, 1))[0][0]

    def st(self):
        b, md = self.rd(("x3_st", 26), ("x3_md", 1))
        return dict(state=b[0], qual=b[1], flags=b[3],
                    elap=struct.unpack_from("<H", b, 4)[0],
                    under=struct.unpack_from("<H", b, 18)[0],
                    err=struct.unpack_from("<H", b, 22)[0], md=md[0])


def boot(leg, out, mp3, ips=None):
    d = os.path.join(out, leg)
    os.makedirs(d)
    disk = os.path.join(d, "bsrc.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", disk, "--size", "1440", os.path.join(FIX, mp3), PKG,
                    "--folder", "SYSTEM/APPDATA"], check=True,
                   stdout=subprocess.DEVNULL)
    box = os88bochs.Box(d, [], sb=True, cfg=1, bimg=disk, btype="1_44",
                        megs=16, display=DISPLAY, ips=ips)
    box.up()
    box.capture()
    box.dbl(591, 105, "B:'s Disk window")
    return box, d


def open_mp3(box):
    """B:'s listing is SYSTEM, the MP3, XTMP3.O88: the MP3's row, and
    XT-MP3 is found by the association its header declares"""
    box.dbl(ROW_X, ROW0_Y + ROW_H, "the MP3's open", lock=False)
    box.until(lambda: bad_lines(box) or box.pkg_base("XTMP3"),
              "XTMP3 started", 120, poll=0.5)


def above(box, sel):
    return sel & 4 and box.sel_base(sel) >= 0x100000


def common(leg, box, x):
    rb = x.base()
    want(rb >= 0x100000, "%s: the region above 1MB (%06X)" % (leg, rb))
    la = x.w("ap_la_seg")
    want(la and above(box, la), "%s: the look-ahead claim (ring, PCM, "
         "stage) an LDT selector above 1MB (%04X at %06X)"
         % (leg, la, box.sel_base(la) if la else 0))
    return la


def leg_play(out):
    leg = "play"
    box, d = boot(leg, out, PLAYMP3, ips=PLAY_IPS)
    try:
        open_mp3(box)
        x = X3(box)
        box.until(lambda: bad_lines(box) or
                  x.st()["md"] not in (MD_IDLE, MD_CHECK, MD_LOAD),
                  "Auto's decision", 240, poll=0.5)
        s = x.st()
        print("   %s: Auto decided: state %d md %d quality %d"
              % (leg, s["state"], s["md"], s["qual"]))
        common(leg, box, x)
        want(x.b("x3h_kind") == X3HK_XMS and x.b("x3h_on") == 1,
             "%s: the file held whole in one XMS block" % leg)
        if not want(s["md"] == MD_LIVE, "%s: Auto plays it live (md %d)"
                    % (leg, s["md"])):
            return
        g0, f0 = x.w("x3_grans"), x.w("x3_frames")
        # ...decoding ON: granules and frames counting up, read off the
        # guest (a host sleep is a fraction of a guest second on a loaded box)
        box.until(lambda: bad_lines(box) or
                  (x.w("x3_grans") > g0 + 10 and x.w("x3_frames") > f0 + 5),
                  "granules and frames counting", 120, 0.5)
        s = x.st()
        g1, f1 = x.w("x3_grans"), x.w("x3_frames")
        want(x.b("x3_sopen") == 1 and s["md"] in (MD_LIVE, MD_STEP),
             "%s: the card's stream open, still live (md %d)"
             % (leg, s["md"]))
        want(g1 > g0 and f1 > f0, "%s: granules %d -> %d, frames %d -> %d"
             % (leg, g0, g1, f0, f1))
        want(x.w("x3h_ncopy") > 0, "%s: gulps copied out of the hold: %d, "
             "disk reads %d" % (leg, x.w("x3h_ncopy"), x.w("x3_nrseq")))
        box.shot(os.path.join(d, "play.png"))
        box.key("s")
        box.until(lambda: x.st()["md"] == MD_IDLE, "the stop", 60, 0.5)
    except Exception as e:      # noqa: BLE001 - the machine stopped
        want(False, "%s: the run stopped: %s" % (leg, e))
    finally:
        b = bad_lines(box)
        want(not b, "%s: no kern_mega FAULT or CLOSED%s"
             % (leg, "".join(" | " + l for l in b[:3])))
        box.stop()


def part(img):
    lba = struct.unpack_from("<I", open(img, "rb").read(512), 446 + 8)[0]
    return lba * 512


def wav_check(leg, cimg):
    import numpy as np
    import x3sim
    import x3simxt
    off = part(cimg)
    r = subprocess.run(["mcopy", "-n", "-i", "%s@@%d" % (cimg, off),
                        "::AFTERHRS.WAV", "-"], capture_output=True)
    if not want(r.returncode == 0 and r.stdout, "%s: C: holds AFTERHRS.WAV"
                % leg):
        return
    data = r.stdout
    bpb = open(cimg, "rb").read()[off:off + 64]
    clu = struct.unpack_from("<H", bpb, 11)[0] * bpb[13]
    dlen = struct.unpack_from("<I", data, clu - 4)[0]
    pcm = np.frombuffer(data[clu:clu + dlen], dtype=np.uint8)
    src = os.path.join(FIX, MP3)
    ref, _, _ = x3simxt.decode(src, "std")
    st = x3sim.Stream(open(src, "rb").read())
    ref = x3sim.trim_pcm(ref.reshape(-1, 1), st.info, 1).reshape(-1)
    want(struct.unpack_from("<HHI", data, 20)[1:] == (1, 11025) and
         len(pcm) == len(ref), "%s: the WAV is 11,025 Hz mono, x3simxt's "
         "length (%d vs %d; cluster %d)" % (leg, len(pcm), len(ref), clu))
    n = min(len(pcm), len(ref))
    bad = np.nonzero(pcm[:n] != ref[:n])[0]
    want(n and not len(bad), "%s: its PCM is x3simxt's XT Standard, every "
         "byte (%d of %d differ%s)" % (leg, len(bad), n,
                                       (", the first at %d" % bad[0])
                                       if len(bad) else ""))


def leg_conv(out):
    leg = "conv"
    box, d = boot(leg, out, MP3)
    cell = None
    try:
        cell = box.read(CAPS_CELL, len(CAPS_NONE))
        want(cell[:1] == b"\x55", "%s: OSAPI_XMEM_CAPS's cell where the API "
             "table puts it (%s)" % (leg, cell.hex()))
        box.dbg(["setpmem 0x%x 1 0x%x" % (CAPS_CELL + i, v)
                 for i, v in enumerate(CAPS_NONE)])
        open_mp3(box)
        x = X3(box)
        box.until(lambda: bad_lines(box) or x.b("x3h_kind") != 0,
                  "the hold", 120, poll=0.3)
        box.dbg(["setpmem 0x%x 1 0x%x" % (CAPS_CELL + i, v)
                 for i, v in enumerate(cell)])
        cell = None
        box.until(lambda: bad_lines(box) or
                  x.st()["md"] not in (MD_IDLE, MD_CHECK, MD_LOAD),
                  "Auto's decision", 240, poll=0.5)
        hb, hw, hk = x.w("x3h_base"), x.w("x3h_win"), x.w("x3h_ckb")
        want(x.b("x3h_kind") == X3HK_CONV and x.b("x3h_on") == 1 and
             hb and above(box, hb), "%s: the file held whole in a "
             "CONVENTIONAL claim of %d KB, an LDT selector above 1MB "
             "(%04X at %06X)" % (leg, hk, hb, box.sel_base(hb) if hb else 0))
        hbase = box.sel_base(hb) if hb else 0
        wb = box.sel_base(hw) if hw & 4 else 0
        want(hw and hw != hb and hbase <= wb < hbase + hk * 1024,
             "%s: x3h_at's window %04X based inside the hold (+%X)"
             % (leg, hw, wb - hbase))
        common(leg, box, x)
        n0 = x.w("x3h_ncopy")
        want(n0 > 0, "%s: gulps copied out of the hold through it: %d"
             % (leg, n0))
        s = x.st()
        print("   %s: Auto decided: state %d md %d" % (leg, s["state"],
                                                       s["md"]))
        if s["md"] != MD_IDLE:
            box.key("s")
            box.until(lambda: x.st()["md"] == MD_IDLE, "the stop", 60, 0.5)
        for _ in range(Q_XTSTD):
            box.key("q")
            box.guest_sleep(0.4)
        want(x.rd(("x3_st", 3))[0][2] == Q_XTSTD,
             "%s: the override is XT STANDARD" % leg)
        box.key("d")
        box.until(lambda: x.st()["md"] == MD_CHOOSE, "the drive dialog",
                  120, 0.5)
        box.key("Return")                       # C:, the only hard drive
        box.guest_sleep(3)                      # GUEST seconds
        box.key("Return")                       # the Save dialog, on C:
        box.until(lambda: bad_lines(box) or x.st()["md"] == MD_CONV,
                  "the conversion", 120, 0.3)
        la = x.w("ap_la_seg")
        lasz = x.w("x3_lasz")
        stage = []

        def conv():
            if bad_lines(box):
                return True
            ss = x.w("x3c_sseg")
            if ss and not stage:
                stage.append(ss)
            return x.st()["md"] != MD_CONV
        box.until(conv, "the end of the conversion", 600, 0.5)
        if want(stage, "%s: a stage while it converted" % leg):
            ss = stage[0]
            want(ss & 4 and ss != la and box.sel_base(ss) ==
                 box.sel_base(la) + lasz + X3_PCMRES and above(box, ss),
                 "%s: the stage %04X an LDT selector based %d + %d bytes "
                 "into the look-ahead claim %04X (%06X / %06X), above 1MB"
                 % (leg, ss, lasz, X3_PCMRES, la, box.sel_base(ss),
                    box.sel_base(la)))
        n1 = x.w("x3h_ncopy")
        want(n1 > n0, "%s: the conversion read the MP3 out of the hold, "
             "through the window: copies %d -> %d" % (leg, n0, n1))
        s = x.st()
        # (Bochs's SB16 drains its DMA ~10x fast, so a 30 KB song may have
        # played out by the time this reads it: FILE, or DONE after it)
        want(s["state"] in (ST_FILE, ST_DONE) and
             s["md"] in (MD_FILE, MD_IDLE) and not s["err"],
             "%s: the conversion finished and the WAV plays on the card "
             "(state %d md %d)" % (leg, s["state"], s["md"]))
        box.shot(os.path.join(d, "conv.png"))
        box.until(lambda: x.st()["state"] == ST_DONE or bad_lines(box),
                  "its end", 120, 0.5)
    except Exception as e:      # noqa: BLE001 - the machine stopped
        want(False, "%s: the run stopped: %s" % (leg, e))
    finally:
        if cell is not None:
            try:
                box.dbg(["setpmem 0x%x 1 0x%x" % (CAPS_CELL + i, v)
                         for i, v in enumerate(cell)])
            except Exception:   # noqa: BLE001
                pass
        b = bad_lines(box)
        want(not b, "%s: no kern_mega FAULT or CLOSED%s"
             % (leg, "".join(" | " + l for l in b[:3])))
        box.stop()
    wav_check(leg, os.path.join(d, "c.img"))


LEGS = {"play": leg_play, "conv": leg_conv}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--leg", action="append", choices=sorted(LEGS))
    a = ap.parse_args()
    for p in (PKG, PMAP, os.path.join(BUILD, "mega.img"),
              os.path.join(BUILD, "megarun.o88")):
        if not os.path.exists(p):
            sys.exit("x3mega: no %s - make && make mega" % p)
    if shutil.which("mcopy") is None:
        sys.exit("x3mega: needs mtools (mcopy)")
    out = tempfile.mkdtemp(prefix="x3mega-")
    t0 = time.time()
    for name in a.leg or ("play", "conv"):
        print("x3mega: %s" % name, flush=True)
        LEGS[name](out)
    print("x3mega: %.0fs under Bochs" % (time.time() - t0))
    if a.keep or FAILS:
        print("x3mega: the boots are in " + out)
    else:
        shutil.rmtree(out, ignore_errors=True)
    for f in FAILS:
        print("FAIL x3mega: " + f)
    print("x3mega: %s" % ("FAILED" if FAILS else "PASS"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
