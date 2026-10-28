#!/usr/bin/env python3
"""megacdrom - CDROM.DRV, CDRack and the Mixer on kern_mega, on a real 286.

    make megacdtest build/megaapps.img && python3 tests/megacdrom.py [--vm 286-mega-cdrom]
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
     it ES = the kernel's DATA ALIAS, 10h, which volume_popup refused) -
     SOUND.DRV's own [volume_live], read through its selector - and Escape
     closes it. The speaker is waited for by [menu_mixcap], what the bar is
     SHOWING: the boot's "Entered protected mode" toast hides it (34.15).
     Then MIXER.O88, launched from B:\\APPS (build/megaapps.img in the
     machine's B:), starts ABOVE 1MB and reads the card's channels (mx_mask
     non-zero). The popup carried an "Open Mixer..." launcher when this row
     was written; upstream's #300 took it out (34.15: "Mixer is launched
     separately from the apps disk's APPS\\ folder"), and the row clicked
     where that item had been until it was taught the launch the SPEC names.
     Every point is the guest's: drive B:'s zone off os88geom.drive_pt, the
     windows covering it minimized by their records, the entries selected
     with [FS_SEL] read back.
  5. The fault log charges NOTHING to a loaded image: no exception at a CS
     that is not the kernel's or the BIOS's. The 286's reflections' own
     resets (mega_svc_lo, "Vector > IDT limit") are by design.

Break on purpose: `--cdrom` with CDROM.DRV from before 14.68 is red at once -
its tick read through 0:046C traps to mega_null0 on every poll (step 5) and
its paragraph fold of ES:DI after a DRQ block loads an off-grid selector,
#GP 9CF4, `kern_mega FAULT ... reboot` (steps 1-3). `--sound` with SOUND.DRV
from before it fails step 4: the speaker opens nothing (seen there by the
gfx lock the popup holds for its life, this build having no map of that
driver).
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
import os88geom as G                                          # noqa: E402
import os88ui                                                 # noqa: E402

CD_ROW = 7                      # drv_tab's CDROM.DRV row (kern_big's, and
DRVR_SIZE, DRVR_SEG = 16, 2     #   so kern_mega's)
WIN_SIZE, W_SEG, W_FLAGS = 74, 22, 0
LDT_LIN = 0x120000              # kernel/mega.inc MEGA_LDT_LIN
POOL0 = 0x2800                  # MEGA_LDT_FREE0 * 8: a pool selector
CDS_PLAYING = 1
SND_CAP_MIXER = 0x0200          # drivers/sound/sndpkg.inc (SPEC.md 34.15)
CLOSE_BOX = (19, 36)            # CDRack's, where its template opens it
SYS_CDRACK = (165, 175)         # A:\\SYSTEM's first file, the autostart's
PLAY = (130, 229)               #   A_SYS.LNK window (SYSTEM\\ lists it first)


def mr_setup(b):
    """MR BIOS 1.65 from a fresh CMOS: both floppies 3.5" 1.44MB (docs/86BOX.md)"""
    def row(r):
        return bytes.fromhex(b.cmd("text")["chars"][r]).decode("cp437")
    b.wait_text("Press Any Key", 20000)         # (86Box's own check, each ms)
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
    for f in ("build/megacdtest.img", "build/megaapps.img", "build/cdaudio.cue",
              "build/cdrack.bin", "build/mixer.bin", "build/sound.bin"):
        if not os.path.exists(os.path.join(ROOT, f)):
            sys.exit("megacdrom: %s is not built - `make %s`" % (
                f, "build/megaapps.img" if "megaapps" in f else
                "megacdtest" if f.endswith((".img", ".cue")) else "all"))
    try:
        cr = os88drvsym.symbols("cdrack.bin")
        mx = os88drvsym.symbols("mixer.bin")
        cd = os88drvsym.symbols("cdrom.bin")
        snd = None if a.sound else os88drvsym.symbols("sound.bin")
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
    # (NOT turbo=True: with 86Box's floppy turbo, kern_mega's protected-mode
    # module load for the speaker's quick volume fails - "Needs Sys Disk A:"
    # on the bar and no popup - where the real-time floppy loads it)
    b = os88box.launch(a.vm, name=name, hold=True, fresh=True, a=img, temp=True)
    try:
        steps(b, a, cr, mx, cd, snd, bad)
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
            b.quit(remove=True)
        shutil.rmtree(tmp, ignore_errors=True)
    for x in bad:
        print("FAIL megacdrom: " + x)
    if bad:
        return 1
    print("megacdrom: PASS")
    return 0


def steps(b, a, cr, mx, cd, snd, bad):
    if True:
        mr_setup(b)
        # the boot, the autostart: was a fixed 70 emulated s. Now the
        # desktop's own state (os88box.kernel_up), protected mode entered
        # ([mega_on]) and the autostarted CDRack's window up, and the UI done
        # with it - each bounded by the old 70 s, so a boot that never gets
        # there fails at the same reads as before
        t = b.kernel_up(guest_ms=70000, idle=False)
        b.until(lambda: b.byte("mega_on") and [s for s in windows(b) if s],
                "kern_mega and the autostart", max(1000, 70000 - t), 250,
                need=False)
        b.ui_done(cap_ms=10000)
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
        b.to(*CLOSE_BOX); b.click(); b.ui_done(cap_ms=3000)
        have = windows(b)
        b.to(*SYS_CDRACK); b.click(2)           # (bounded by the old 15 s)
        b.until(lambda: high(b, have), "CDRack above 1MB", 15000, 250, need=False)
        b.ui_done(cap_ms=5000)
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
            b.to(*PLAY); b.click()              # PLAYING, then the 5 s
            b.until(lambda: b.memr(crb + 1, 1)[0] == CDS_PLAYING, "PLAYING",
                    4000, 100, need=False)
            s0 = struct.unpack("<H", b.memr(crb + 12, 2))[0]
            b.cmd("step 5000")
            st = b.memr(crb + 1, 1)[0]
            s1 = struct.unpack("<H", b.memr(crb + 12, 2))[0]
            print("   3. cr_play %d, cr_secs %d -> %d over 5 s" % (st, s0, s1))
            if st != CDS_PLAYING or s1 <= s0:
                bad.append("3. the play did not run: cr_play %d, cr_secs %d "
                           "-> %d" % (st, s0, s1))
        mixer_steps(b, a, mx, snd, bad)


class Guest(object):
    """os88geom's reader over the 86Box socket: `read` and `sym` are all its
    drive_pt and close_xy ask of a machine"""
    def __init__(self, b):
        self.b = b

    def read(self, addr, n):
        return self.b.memr(addr, n)

    def sym(self, name):
        return self.b.ksym(name)


def seg_lin(b, seg):
    """A segment word's linear base: an LDT selector of the pool through its
    descriptor (kern_mega's drivers and packages in protected mode), a
    paragraph otherwise"""
    return base_of(b, seg) if seg & 4 and seg >= POOL0 else seg << 4


def win_recs(b):
    """{slot: (x, y, w, h, W_FLAGS, W_SEG)} of every used window"""
    raw = b.memr(b.ksym("wm_wins"), 12 * WIN_SIZE)
    out = {}
    for i in range(12):
        if struct.unpack_from("<H", raw, i * WIN_SIZE + W_FLAGS)[0] & 1:
            out[i] = struct.unpack_from("<HHHH", raw, i * WIN_SIZE + G.W_X) \
                + struct.unpack_from("<H", raw, i * WIN_SIZE + W_FLAGS) \
                + struct.unpack_from("<H", raw, i * WIN_SIZE + W_SEG)
    return out


def fs_block(b, slot):
    """A Disk window's own KD_POOL state block (os88ui._fsblk's route:
    wm_owner -> inst_tab -> I_SPTR), linear, or None"""
    inst = b.memr(b.ksym("wm_owner"), 12)[slot]
    if inst == 0xFF:
        return None
    blk = struct.unpack("<H", b.memr(b.ksym("inst_tab") + inst * G.I_RECSZ
                                     + G.I_SPTR, 2))[0]
    return (G.KERNEL_SEG << 4) + blk if blk else None


def listing(b, blk):
    """[(name, type)] the Disk window is showing, out of its own cache"""
    n, = struct.unpack("<H", b.memr(blk + G.FS_N, 2))
    vseg, = struct.unpack("<H", b.memr(blk + G.FS_VSEG, 2))
    if not n or not vseg:
        return []
    raw = b.memr(seg_lin(b, vseg), n * G.DSK_DE_STRIDE)
    return [(raw[i * G.DSK_DE_STRIDE:i * G.DSK_DE_STRIDE + 16]
             .split(b"\0")[0].decode("latin-1"),
             struct.unpack_from("<H", raw, i * G.DSK_DE_STRIDE
                                + G.LD_DE_TYPE)[0]) for i in range(n)]


def pick(b, slot, blk, name, what):
    """Select `name` in the Disk window and press Enter (SPEC.md 22.11: Enter
    opens the selection like a double-click). With NO selection the arrows
    only scroll the view (22.26), so the first one is a click on the view's
    top row, at os88ui.row_xy's list geometry; then the arrows walk it, with
    [FS_SEL] read back after each key, so the walk is the window's answer"""
    rows = listing(b, blk)
    names = [r[0].upper() for r in rows]
    if name not in names:
        return "%s: %s is not in the folder, which holds %s" % (
            what, name, " ".join(names) or "nothing")
    want = names.index(name)
    sel = lambda: struct.unpack("<H", b.memr(blk + G.FS_SEL, 2))[0]
    if sel() >= len(rows):
        if b.memr(blk + G.FS_VIEW, 1)[0]:
            return "%s: the window is not in the list view" % what
        x, y = win_recs(b)[slot][:2]
        b.to(x + 1 + os88ui.FM_ROW_X,
             y + G.TITLE_H + 1 + G.FM_ROW_Y0 + G.FM_ROW_H // 2)
        b.click()
        b.until(lambda: sel() < len(rows), "a selection", 3000, 20,
                need=False)
        b.ui_done(cap_ms=1000)
    for _ in range(len(rows) + 4):
        s = sel()
        if s == want or s >= len(rows):
            break
        b.tap("down" if s < want else "up")
        b.until(lambda: sel() != s, "the selection to move", 2000, 20,
                need=False)
    if sel() != want:
        return "%s: the selection stopped at %d, not %s's %d" % (
            what, sel(), name, want)
    b.ui_done(cap_ms=1000)
    b.tap("return")
    return None


def mixer_steps(b, a, mx, snd, bad):
    """4. The speaker opens the quick volume, and the Mixer - launched from
    the apps disk's APPS\\ folder, where SPEC.md 34.15 puts it since the
    popup stopped carrying a launcher - starts above 1MB and reads the card.

    THE SPEAKER IS ONLY ON THE BAR WHILE NO TOAST COVERS IT (SPEC.md 34.15:
    a toast that missed the gap hides it, and its cell is then the Date/Time
    cell's), so [menu_mixcap] - what is SHOWING - is waited for first and the
    x is read after it. The popup's own state is SOUND.DRV's [volume_live],
    read through the driver's selector: a click that missed opens nothing
    and that byte says so, where a remembered coordinate inside a popup that
    never opened would report the Mixer as the failure."""
    b.until(lambda: b.word("menu_mixcap") == SND_CAP_MIXER,
            "the speaker on the bar", 10000, 100, need=False)
    cap = b.word("menu_mixcap")
    mxx = b.word("menu_mixx")
    dseg = b.word("drv_fseg")
    # (--sound puts a driver this build has no map of on the disk: its popup
    # is then seen by the gfx lock it holds for the whole of its life - still
    # held half a second on, where the click's own handling has long let go)
    live = (lambda: b.memr(seg_lin(b, dseg) + snd["volume_live"], 1)[0]
            if dseg else 0) if snd else (lambda: b.byte("gfx_lock_flag"))
    if cap == SND_CAP_MIXER:
        b.to(mxx + 12, 9); b.click()
        if snd:
            b.until(live, "the quick volume", 3000, 50, need=False)
        else:
            b.cmd("step 500")
    up = live()
    vx = struct.unpack("<H", b.memr(seg_lin(b, dseg) + snd["volume_x"],
                                    2))[0] if dseg and snd else 0
    if up:
        b.tap("esc")
        b.until(lambda: not live(), "the quick volume to close", 3000, 50,
                need=False)
    print("   4. speaker at x %d (menu_mixcap %04Xh); SOUND.DRV at %04X: the "
          "quick volume %s%s, then %s" % (
              mxx, cap, dseg, "opened" if up else "did NOT open",
              " at x %d" % vx if snd else " (by the gfx lock)",
              "closed" if up and not live() else "-"))
    if not up:
        bad.append("4. the menu bar's speaker opened no quick volume")
        return
    if live():
        bad.append("4. Escape did not close the quick volume")
        return
    # the Mixer from B:\\APPS, through drive B:'s desktop zone - which is
    # BEHIND every window, and CDRack's 610-wide one is over it. So whatever
    # covers it is MINIMIZED (the play goes on: a minimized instance runs),
    # by the box its record puts at W_X + W_W - 1 - WM_BOX_X0, mirrored from
    # the close box
    try:
        bx, by = G.drive_pt(Guest(b), "B")
    except G.GeomError as e:
        bad.append("4. %s" % e)
        return
    over = lambda: [s for s, (x, y, w, h, f, seg) in win_recs(b).items()
                    if f & G.WF_VIS and x <= bx < x + w and y <= by < y + h]
    for s in over():
        x, y, w, h, f, seg = win_recs(b)[s]
        b.to(x + w - 1 - G.WM_BOX_X0 - G.WM_BOX_W // 2,
             y + (G.WM_BOX_Y0 + G.WM_BOX_Y1) // 2)
        b.click()
        b.until(lambda: s not in over(), "window %d minimized" % s, 3000, 50,
                need=False)
        b.ui_done(cap_ms=3000)
    over = over()
    if over:
        bad.append("4. drive B:'s zone at (%d,%d) is covered by window(s) %s"
                   % (bx, by, over))
        return
    before = set(win_recs(b))
    b.to(bx, by); b.click(2)
    disk = lambda: [s for s in win_recs(b) if s not in before
                    and fs_block(b, s) and b.memr(fs_block(b, s) + G.FS_MOK,
                                                  1)[0]]
    b.until(disk, "a Disk window on B:", 15000, 100, need=False)
    b.ui_done(cap_ms=3000)
    d = disk()
    if not d:
        bad.append("4. double-clicking drive B:'s zone at (%d,%d) opened no "
                   "Disk window" % (bx, by))
        return
    blk = fs_block(b, d[0])
    cwd = lambda: struct.unpack("<H", b.memr(blk + G.FS_CWD, 2))[0]
    root = cwd()
    why = pick(b, d[0], blk, "APPS", "B:\\")
    if not why:
        b.until(lambda: cwd() != root and b.memr(blk + G.FS_MOK, 1)[0],
                "B:\\APPS", 15000, 100, need=False)
        b.ui_done(cap_ms=3000)
        why = pick(b, d[0], blk, "MIXER.O88", "B:\\APPS")
    if why:
        bad.append("4. " + why)
        return
    have = windows(b)
    mask = lambda h: struct.unpack("<H", b.memr(
        base_of(b, h[0]) + mx["mx_mask"], 2))[0] if h else 0
    b.until(lambda: mask(high(b, have)), "the Mixer reading the card",
            15000, 250, need=False)
    h = high(b, have)
    mask = mask(h)
    print("      B:\\APPS\\MIXER.O88 -> the Mixer above 1MB: %s, mx_mask %04Xh"
          % ("%04X at %06Xh" % (h[0], base_of(b, h[0])) if h else "none",
             mask))
    if len(h) != 1 or not mask:
        bad.append("4. MIXER.O88 from B:\\APPS started no Mixer above 1MB "
                   "that read the card")

if __name__ == "__main__":
    sys.exit(main())
