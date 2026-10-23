#!/usr/bin/env python3
"""megacdrom - CDROM.DRV, CDRack and the Mixer on kern_mega, on a real 286.

    make megacdtest && python3 tests/megacdrom.py [--vm 286-mega-cdrom]
        [--cdrom FILE.DRV] [--sound FILE.DRV] [--keep]

SPEC.md 119 and 34.15 under docs/plans/KERN-MEGA-PLAN.md 14.68. Upstream
wrote the CD-ROM driver, CDRack and the Mixer for real mode, and kern_mega
is the kernel that runs on machines with a CD drive in them. 86Box BY NAME:
its 286 takes the CPU-reset path every reflection does on a real AT
(docs/86BOX.md), its ATAPI drive plays a .cue's audio (Bochs 2.7 answers
PLAY AUDIO "not implemented"), and its fault log prints every exception the
CPU raises - kern_mega COMPLETES a segment-0 access or a write through CS
rather than faulting on it (mega_null0, mega_gpw), so a defect of that kind
runs and only the log can see it. MartyPC is an 8088 and CDROM.DRV refuses
one at attach (SPEC.md 119.2).

vm/286-mega-cdrom (the owner's mr286 with an IDE CD-ROM on the secondary
channel holding build/cdaudio.cue) boots build/megacdtest.img from a fresh
CMOS, which the row sets up itself (MR BIOS's floppy page). The disk wants
SOUND.DRV and CDROM.DRV and autostarts CDRack - in REAL mode, before
mega_enter, so that copy is low and the row closes it.

  1. CDROM.DRV is loaded and found the drive (drv_tab's row, cd_base 170h).
  2. CDRack started again in PROTECTED mode from A:\\SYSTEM lands ABOVE 1MB:
     its window's W_SEG is an LDT selector of the pool, whose descriptor's
     base is past 100000h (OS88_HIGH, KERN-MEGA-PLAN 13.3 step 4).
  3. Play: CDRack's own status (cr_play, read through that base) says
     PLAYING and its seconds (cr_secs) advance - every command a PACKET with
     a tick-bounded wait, PLAY AUDIO MSF, READ SUB-CHANNEL, all in PM.
  4. The menu bar's speaker opens SOUND.DRV's quick volume (kern_mega hands
     it ES = the kernel's DATA ALIAS, 10h, which volume_popup refused), and
     its Open Mixer... starts MIXER.O88 ABOVE 1MB, which read the card's
     channels (mx_mask non-zero).
  5. The fault log charges NOTHING to a loaded image: no exception at a CS
     that is not the kernel's or the BIOS's. The 286's reflections' own
     resets (mega_svc_lo, "Vector > IDT limit") are by design.

Break on purpose: `--cdrom` with CDROM.DRV from before 14.68 is red at once -
its tick read through 0:046C traps to mega_null0 on every poll (step 5) and
its paragraph fold of ES:DI after a DRQ block loads an off-grid selector,
#GP 9CF4, `kern_mega FAULT ... reboot` (steps 1-3). `--sound` with SOUND.DRV
from before it fails step 4: the speaker opens nothing.
"""
import argparse
import os
import re
import shutil
import struct
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
import os88box                                                # noqa: E402
import os88drvsym                                             # noqa: E402

CD_ROW = 7                      # drv_tab's CDROM.DRV row (kern_big's, and
DRVR_SIZE, DRVR_SEG = 16, 2     #   so kern_mega's)
WIN_SIZE, W_SEG, W_FLAGS = 74, 22, 0
LDT_LIN = 0x120000              # kernel/mega.inc MEGA_LDT_LIN
POOL0 = 0x2800                  # MEGA_LDT_FREE0 * 8: a pool selector
CDS_PLAYING = 1
CLOSE_BOX = (19, 36)            # CDRack's, where its template opens it
SYS_CDRACK = (165, 175)         # A:\\SYSTEM's first file, the autostart's
PLAY = (130, 229)               #   A_SYS.LNK window (SYSTEM\\ lists it first)


def mr_setup(b):
    """MR BIOS 1.65 from a fresh CMOS: both floppies 3.5" 1.44MB (docs/86BOX.md)"""
    def row(r):
        return bytes.fromhex(b.cmd("text")["chars"][r]).decode("cp437")
    for _ in range(40):
        b.cmd("step 500")
        try:
            if "Press Any Key" in "".join(row(r) for r in range(25)):
                break
        except SystemExit:
            pass
    b.tap("return"); b.cmd("step 1500")
    for _ in range(4):
        b.tap("right"); b.cmd("step 300")
    b.tap("down"); b.cmd("step 300")
    for r in (10, 11):
        n = 0
        while "1.4" not in row(r) and n < 8:
            b.tap("space"); b.cmd("step 300"); n += 1
        b.tap("down"); b.cmd("step 300")
    b.tap("esc"); b.cmd("step 500")
    b.tap("f10")


def windows(b):
    lin = b.ksym("wm_wins")
    out = []
    for i in range(12):
        rec = b.memr(lin + WIN_SIZE * i, WIN_SIZE)
        if struct.unpack_from("<H", rec, W_FLAGS)[0] & 1:
            out.append(struct.unpack_from("<H", rec, W_SEG)[0])
    return out


def base_of(b, sel):
    """A pool selector's descriptor base, out of kern_mega's LDT"""
    d = b.memr(LDT_LIN + (sel & ~7), 8)
    return d[2] | d[3] << 8 | d[4] << 16


def high(b, have):
    """The windows whose segment is a pool selector above 1MB and new"""
    return [s for s in windows(b) if s & 4 and s >= POOL0 and s not in have
            and base_of(b, s) >= 0x100000]


def image_faults(run):
    """Exceptions charged to anything but the kernel (0060, 0028, FFD8) or
    the BIOS (F000 and up)"""
    bad = []
    for line in open(os.path.join(run, "stderr.log"), errors="replace"):
        m = os88box.FAULT.search(line)
        if m and int(m.group(2), 16) < 0xF000 and \
                int(m.group(2), 16) not in (0x0060, 0x0028, 0xFFD8):
            bad.append(line.strip())
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--vm", default="286-mega-cdrom")
    ap.add_argument("--cdrom", help="a CDROM.DRV to put on the disk instead")
    ap.add_argument("--sound", help="a SOUND.DRV to put on the disk instead")
    ap.add_argument("--keep", action="store_true",
                    help="leave the machine running")
    a = ap.parse_args()
    for f in ("build/megacdtest.img", "build/cdaudio.cue", "build/cdrack.bin",
              "build/mixer.bin"):
        if not os.path.exists(os.path.join(ROOT, f)):
            sys.exit("megacdrom: %s is not built - `make megacdtest`" % f)
    try:
        cr = os88drvsym.symbols("cdrack.bin")
        mx = os88drvsym.symbols("mixer.bin")
        cd = os88drvsym.symbols("cdrom.bin")
    except os88drvsym.Mismatch as e:
        sys.exit("megacdrom: %s" % e)
    tmp = tempfile.mkdtemp(prefix="megacd")
    img = os.path.join(tmp, "megacdtest.img")   # (named mega*: os88box then
    shutil.copy(os.path.join(ROOT, "build", "megacdtest.img"), img)  # reads
    for drv, name in ((a.cdrom, "CDROM.DRV"), (a.sound, "SOUND.DRV")):  # megak/)
        if drv:
            fat = [sys.executable, os.path.join(ROOT, "tools", "os88fat.py")]
            os88box.subprocess.run(fat + ["del", img, name], check=True,
                                   stdout=os88box.subprocess.DEVNULL)
            os88box.subprocess.run(fat + ["add", img, drv, name], check=True,
                                   stdout=os88box.subprocess.DEVNULL)
    bad = []
    name = "megacd%d" % os.getpid()
    b = os88box.launch(a.vm, name=name, hold=True, fresh=True, a=img)
    try:
        steps(b, a, cr, mx, cd, bad)
    except SystemExit as e:                     # os88box: the guest stopped
        bad.append("the machine stopped answering: %s" % e)  # answering
    finally:
        f = image_faults(b.run)
        print("   5. exceptions charged to a loaded image: %d" % len(f))
        for line in f[:6]:
            print("      " + line)
        if f:
            bad.append("5. %d exception(s) in a driver's or a package's code"
                       % len(f))
        if not a.keep:
            b.cmd("quit")
            shutil.rmtree(b.run, ignore_errors=True)
        shutil.rmtree(tmp, ignore_errors=True)
    for x in bad:
        print("FAIL megacdrom: " + x)
    if bad:
        return 1
    print("megacdrom: PASS")
    return 0


def steps(b, a, cr, mx, cd, bad):
    if True:
        mr_setup(b)
        b.cmd("step 70000")                     # the boot, the autostart
        row = b.memr(b.ksym("drv_tab") + CD_ROW * DRVR_SIZE, DRVR_SIZE)
        seg = struct.unpack_from("<H", row, DRVR_SEG)[0]
        port = struct.unpack("<H", b.memr(seg * 16 + cd["cd_base"], 2))[0] \
            if seg and not seg & 4 and not a.cdrom else 0
        print("   1. CDROM.DRV at %04Xh, task file %s" % (
            seg, "%03Xh" % port if not a.cdrom else "(not this build's map)"))
        if not seg or (port != 0x170 and not a.cdrom):
            bad.append("1. CDROM.DRV is not loaded, or did not find the "
                       "secondary master")
        low = windows(b)
        b.to(*CLOSE_BOX); b.click(); b.cmd("step 3000")
        have = windows(b)
        b.to(*SYS_CDRACK); b.click(2); b.cmd("step 15000")
        h = high(b, have)
        crb = base_of(b, h[0]) + cr["os88_image_end"] if h else None
        print("   2. windows %s before, %s after CDRack from SYSTEM; above "
              "1MB: %s" % (" ".join("%04X" % s for s in low),
                           " ".join("%04X" % s for s in windows(b)),
                           "%04X at %06Xh" % (h[0], base_of(b, h[0]))
                           if h else "none"))
        if len(h) != 1:
            bad.append("2. CDRack did not start above 1MB")
        else:
            b.to(*PLAY); b.click(); b.cmd("step 4000")
            s0 = struct.unpack("<H", b.memr(crb + 12, 2))[0]
            b.cmd("step 5000")
            st = b.memr(crb + 1, 1)[0]
            s1 = struct.unpack("<H", b.memr(crb + 12, 2))[0]
            print("   3. cr_play %d, cr_secs %d -> %d over 5 s" % (st, s0, s1))
            if st != CDS_PLAYING or s1 <= s0:
                bad.append("3. the play did not run: cr_play %d, cr_secs %d "
                           "-> %d" % (st, s0, s1))
        have = windows(b)
        mxx = struct.unpack("<H", b.memr(b.ksym("menu_mixx"), 2))[0]
        b.to(mxx + 12, 9); b.click(); b.cmd("step 3000")
        b.to(mxx - 106, 101); b.click(); b.cmd("step 15000")
        h = high(b, have)
        mask = struct.unpack("<H", b.memr(base_of(b, h[0]) + mx["mx_mask"], 2)
                             )[0] if h else 0
        print("   4. speaker at x %d; the Mixer above 1MB: %s, mx_mask %04Xh"
              % (mxx, "%04X at %06Xh" % (h[0], base_of(b, h[0])) if h
                 else "none", mask))
        if len(h) != 1 or not mask:
            bad.append("4. the quick volume's Open Mixer... started no Mixer "
                       "above 1MB that read the card")


if __name__ == "__main__":
    sys.exit(main())
