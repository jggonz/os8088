#!/usr/bin/env python3
"""The RAM disk's EXTENDED-MEMORY store, end to end (SPEC.md 62.9.10, 62.9.14).

    python3 tests/rdxms.py [--kb N] [--sys IMG --src ROOT]

RAMDISK.DRV can keep its store above 1MB: `rd_loc` = RDL_XMS, the store an
`OSAPI_XMEM_ALLOC` block at `rd_xbase`, and every extent reached through ONE
conventional bounce that `rd_ext_seg` stages it into and `rd_ext_flush` writes
it back out of (drivers/ramdisk/rdstore.inc). tests/rdpreserve.py is the gate
for the conventional store and says in its own docstring that it cannot reach
this one - every MartyPC machine is an 8088, `OSAPI_XMEM_CAPS` answers 0 there
and the Store radio is greyed.

WHY QEMU: docs/TESTING.md's closed list, entry 1. QEMU's `pc` machine with
`-m 128` is a 386 with 63MB above 1MB - `xm_kb` 64,448 and `cpu_tier` 2 - and
no MartyPC machine has a byte up there. It boots the 1.44MB system disk, as
tests/vidxms.py and tests/msegxms.py do, and borrows their launcher and
tests/xmcheck.py's reader of XMEM.DRV's block table.

THE SESSION, with a scratch 1.44MB B: holding SRC/DATA.BIN (rdpreserve's
40,000 seeded bytes) and an OUT/ folder:

  1  the Ram Disk page: the Store radio set to Xms, the size box TYPED to
     `--kb` (136), and Mount
  2  B:SRC/DATA.BIN copied onto the RAM volume by the file manager
  3  ...and copied back to B:'s root, through the kernel
  4  Preserve As B:RAMDISK.RAM, then Unmount
  5  Load of that file, and DATA.BIN copied off the LOADED volume to B:OUT
  6  the final Unmount

THE CHECKS:

  X  the radio really selected the extended store ([rd_loc] = RDL_XMS)
  M  the mount is EXTENDED: rd_xbase above 1MB, NO conventional arena, a
     bounce, and XMEM.DRV's table holding a live block at least the store's
     size - so this is not the conventional path wearing the radio's label
  W  THE STORE, READ PHYSICALLY OUT OF EXTENDED MEMORY and walked by the
     live chain table from the extent holding the file's first bytes, IS the
     file. This is what the row adds over every kernel-side check: a copy
     back through the driver reads through the same bounce the write went
     into, so a store that was never written back can still read correctly
     for as long as the right extent is the one staged
  R1 DATA.BIN copied back to B:\\ is the source, read on the HOST
  S/H/A/F/V  rdpreserve's own checks on B:RAMDISK.RAM, with its parser,
     which shares no code with the writer: the size to the byte, the header
     (O8RD v2), the arena == the extended store as read in W's way, the chain
     table == the chain claim, the file out of the image alone, B: fscks
  U  Unmount discards the store and FREES the extended block - no block of
     the store's size left live in XMEM.DRV's table
  L  Load puts the store back in XMS and its bytes ARE the file's arena, its
     chain claim the file's table
  R2 DATA.BIN off the LOADED volume, on B:OUT, is the source
  U2 the final Unmount frees the block again

RED CONTROL (docs/WRITING-TESTS.md 1), rdpreserve's route: a RAMDISK.DRV
built from a private copy of the tree and swapped onto a copy of the system
disk with `tools/os88fat.py del`/`add`; `--sys` boots that disk and `--src`
maps that driver's labels. Wrap the copy's `build/ramdisk.bin` with a plain
`tools/os88drv.py` (no `--compress`): `os88fat.py add` writes no directory
hint, and a hint-less 'CZ' driver did not load in 30 guest seconds. The same
route with the source UNCHANGED is green. Two breaks, measured:

  * `rd_ext_flush`'s `jne .out` made `jmp short .out`, so a write through the
    bounce reaches extended memory only when some OTHER extent is staged over
    it: W ALONE goes red - the walk finds the file's last extent (offset
    39,936 of 40,000) not written - and R1 STAYS GREEN, because the copy back
    reads that very extent out of the bounce it is still sitting in, staging
    the others in and so writing it out on the way. Everything after is green
    too. That is the whole case for W: a round trip through the driver cannot
    see a store that was never written back.
  * `rd_stage_out` made a `ret`, so nothing reaches extended memory at all:
    W (no extent holds the file's first bytes), R1, F and R2 go red. A and L
    stay green, and are right to: the image and the store agree with each
    other, both being the zeros that are really up there.
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
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88build                                            # noqa: E402
import os88flush                                            # noqa: E402
import os88geom as geom                                     # noqa: E402
import os88qemu                                             # noqa: E402
import os88ui                                               # noqa: E402
import rdpreserve as rp                                     # noqa: E402
import xmcheck                                              # noqa: E402
from vidxms import Q                                        # noqa: E402

S = xmcheck.sym
RD_DIRB = 96 * 24               # rdabi.inc: the chain table starts after the
                                # directory in [rd_dtab]'s claim (62.9.13.1)
RDL_XMS = 1                     # rdabi.inc; page.inc refuses to assemble if
                                # it moves (`rp_radio and the paint take
                                # RDL_CONV as zero and RDL_XMS as one`)
# page.inc's Xms radio, pane-relative and GLYPH AND LABEL (47 rule 2):
# RP_RX1 = 130, RP_RW1 = 40, on the Store row at RP_LY = 34
R_XMS = (130, 31, 170, 43)
XM_OWN_KERN = xmcheck.XM_OWN_KERN   # a RAM disk's block is the kernel's:
                                    # the driver claims it outside any
                                    # instance's stamp (rdstore.inc)

fails = []


def say(*a):
    print(*a, flush=True)


def check(tag, ok, what, why=""):
    say("  %-3s %-4s %s%s" % (tag, "ok" if ok else "BAD", what,
                              "" if ok else "  -- " + why))
    if not ok:
        fails.append("%s: %s%s" % (tag, what, (" -- " + why) if why else ""))
    return ok


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def first_diff(a, b):
    return next((k for k in range(min(len(a), len(b))) if a[k] != b[k]),
                min(len(a), len(b)))


class Machine(Q):
    """tests/vidxms.py's private QEMU, plus the input this row needs.

    Every pause is the GUEST's (tests/os88qemu.py): a click is followed by
    `ui_done` - ui_task asleep with nothing queued - capped where the old
    fixed pause was, and every wait below is on the state the gesture is FOR.
    """

    def read(self, linear, n):
        out = bytearray()
        while n > 0:                    # xmcheck's reader is one `xp` per
            k = min(n, 4096)            # call; a 136KB store is many
            out += xmcheck.read_bytes(self.sock, linear, k)
            linear += k
            n -= k
        return bytes(out)

    def mouse(self, *args):
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "mouse.py"), self.sock]
                       + [str(a) for a in args], check=True,
                       capture_output=True, cwd=ROOT)

    def done(self, cap=2.0):
        os88qemu.ui_done(self, S, cap=cap)

    def click(self, x, y, cap=2.0):
        self.mouse("click", x, y)
        self.done(cap)

    def dblclick(self, x, y, cap=2.0):
        xmcheck.dblclick(self.sock, x, y)
        self.done(cap)

    def key(self, k):
        self.hmp("sendkey %s" % k)

    def wait(self, cond, what, secs=30):
        """True when `cond` held within `secs` GUEST seconds."""
        return os88qemu.acted(self, cond, secs=secs, what=what, poll=0.1)

    def need(self, cond, what, secs=30):
        if not self.wait(cond, what, secs):
            raise SystemExit("rdxms: timed out (%d guest s) waiting for %s"
                             % (secs, what))


def menu_pick(q, ui, menu, item):
    """os88ui's menu gesture on QEMU: press on the title, CONFIRM the
    pull-down (`menu_y1`, `menu_dropd`), move to the item and CONFIRM it is
    the highlighted one (`menu_sel`) before the release - so a release can
    neither land in a menu that has not been drawn nor pick its neighbour.
    Both positions come off the guest's own tables (`ui.menus()`)."""
    cells = ui.menus()
    hit = [i for i, c in enumerate(cells)
           if c[0].upper().startswith(menu.upper())]
    if len(hit) != 1:
        raise SystemExit("rdxms: menu %r is not on the bar: %r"
                         % (menu, [c[0] for c in cells]))
    cell = hit[0]
    _, x0, x1, _ = cells[cell]
    q.mouse("down", (x0 + x1) // 2, geom.MBAR_H // 2)
    q.need(lambda: ui._word("menu_y1") and ui._byte("menu_dropd"),
           "the %s menu to drop" % menu, 10)
    items = ui.menus()[cell][3]
    idx = [i for i, (t, _) in enumerate(items)
           if t.upper().startswith(item.upper())]
    if len(idx) != 1 or not items[idx[0]][1]:
        q.mouse("up")
        raise SystemExit("rdxms: %s > %s is not a live item: %r"
                         % (menu, item, items))
    k = idx[0]
    q.mouse("to", ui._word("menu_x1") + 8,
            ui._word("menu_y1") + 1 + k * geom.MENU_ITEM_H
            + geom.MENU_ITEM_H // 2)
    q.need(lambda: ui._word("menu_sel") == k, "%s highlighted" % item, 10)
    q.mouse("up")
    q.done()


def scratch_b(work, data):
    """1.44MB, so the store's image (147,456 bytes) and two copies fit with
    room: SRC/DATA.BIN to copy on, and OUT/ to copy the loaded file into."""
    open(os.path.join(work, rp.DOC), "wb").write(data)
    keep = os.path.join(work, "KEEP.TXT")
    open(keep, "wb").write(b"keep\r\n")
    img = os.path.join(work, "b.img")
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "os88disk.py"),
                        "-o", img, "--size", "1440",
                        rp.SRC + ":" + os.path.join(work, rp.DOC),
                        "OUT:" + keep], capture_output=True, text=True)
    if r.returncode:
        sys.exit("rdxms: os88disk: " + r.stderr)
    return img


def host_read(img, name):
    """A file off the B: image on the HOST (os88flush's FAT12 reader), which
    is not the implementation that wrote it."""
    try:
        return os88flush.Volume(open(img, "rb").read()).read(name)
    except Exception as e:          # noqa: BLE001 - the verdict says what
        say("   (host read of %s: %s)" % (name, e))
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kb", type=int, default=136,
                    help="the store, typed into the page")
    ap.add_argument("--mem", type=int, default=128,
                    help="QEMU's -m: the machine's RAM in MB")
    ap.add_argument("--src", default=ROOT,
                    help="the tree whose drivers/ramdisk the disk carries")
    ap.add_argument("--sys", default="build/os8088.img",
                    help="the system disk to boot (a red control's, whose "
                         "RAMDISK.DRV was swapped)")
    a = ap.parse_args()
    os.chdir(ROOT)
    for p in (a.sys, "build/xmem.bin", "build/rampage.bin"):
        if not os.path.exists(os88build.at(p)):
            sys.exit("rdxms: no %s - run `make`" % p)
    R = rp.drv_syms(a.src)
    data = rp.payload()
    work = tempfile.mkdtemp(prefix="rdxms-")    # short: a QMP socket path
    try:                                        # has a ~107-byte limit
        bimg = scratch_b(work, data)
        sysimg = os.path.join(work, "sys.img")
        shutil.copy(os88build.at(a.sys), sysimg)
        q = Machine(work, sysimg, bimg, a.mem)
        for _ in range(150):
            if os.path.exists(q.sock):
                break
            time.sleep(0.2)             # the socket appearing is a HOST event
        xmcheck.wait_desktop(q.sock, "rdxms")
        session(q, a, R, data, bimg)
        q.close()
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for f in fails:
        say("  FAIL: " + f)
    say("rdxms: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


def session(q, a, R, data, bimg):
    ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)
    xm_kb, tier = u16(q.read(S("xm_kb"), 2)), q.read(S("cpu_tier"), 1)[0]
    say("rdxms: QEMU -m %d, xm_kb %d, cpu_tier %d, a %dKB store, %d bytes of "
        "%s" % (a.mem, xm_kb, tier, a.kb, len(data), rp.DOC))
    if xm_kb < a.kb:
        raise SystemExit("rdxms: the machine reports %dKB above 1MB - there "
                         "is no extended store to test (needs -m >= 2)"
                         % xm_kb)
    xtab = xmcheck.table_base(q.sock)

    def rd_seg():
        return u16(q.read(S("drv_tab") + rp.RD_ROW * rp.DRVR_SZ
                          + rp.DRVR_SEG, 2))

    def dw(n):                      # a driver word, at the driver's segment
        return u16(q.read(rd_seg() * 16 + R[n], 2))     # AS IT IS NOW

    def dd(n):
        return struct.unpack("<I", q.read(rd_seg() * 16 + R[n], 4))[0]

    def db(n):
        return q.read(rd_seg() * 16 + R[n], 1)[0]

    def fdlg_up():
        return u16(q.read(S("fdlg_win"), 2)) not in (0, 0xFFFF)

    def ch_drv():                   # the chooser's own FS_DRV (rdpreserve)
        blk = u16(q.read(S("fdlg_blk"), 2))
        return q.read((geom.KERNEL_SEG << 4) + blk + geom.FS_DRV, 1)[0]

    def chooser_win():
        ptr = u16(q.read(S("fdlg_win"), 2))
        i = (ptr + (geom.KERNEL_SEG << 4) - S("wm_wins")) // geom.WIN_SIZE
        return next((o for o in ui.windows() if o.i == i), None)

    def store_blocks():
        """XMEM.DRV's live blocks the size of this store or bigger."""
        return [b for b in xmcheck.blocks(q.sock, xtab) if b[2] >= a.kb]

    def raise_win(w):
        top = ui.front()
        if top is None or top.i != w.i:
            q.click(w.x + w.w // 2, w.y + 8)
            q.need(lambda: ui.front() is not None and ui.front().i == w.i,
                   "window %d to raise" % w.i, 15)

    def close(w):
        w = next(o for o in ui.windows() if o.i == w.i)
        raise_win(w)
        q.click(*geom.close_xy(w.x, w.y))
        q.need(lambda: all(o.i != w.i or not o.visible
                           for o in ui.windows()),
               "window %r to close" % w.title, 15)

    def open_page(first):
        menu_pick(q, ui, "Apple", "Control")
        box = {}

        def got():
            try:
                box["w"] = ui.window("Control Panel")
                return box["w"].visible
            except os88ui.UIError:
                return False
        q.need(got, "the Control Panel", 20)
        q.done(4.0)
        cp = box["w"]
        x0, y0 = cp.x + 1, cp.y + rp.TITLE_H
        if first:                   # the Drivers page, and tick the RAM row
            q.click(x0 + 40, y0 + rp.CP_I0Y + rp.CP_IDRV * rp.CP_IROWH + 7)
            q.click(x0 + rp.CP_RX + 40,
                    y0 + rp.CP_DBY1 + rp.RD_ROW * rp.CP_DROWH
                    + rp.CP_DROWH // 2)
            q.need(rd_seg, "the RAM disk driver to load", 30)
            q.done(4.0)
        nst = q.read(S("cp_nst"), 1)[0]     # the Ram Disk page's row
        q.click(x0 + 40, y0 + rp.CP_I0Y + nst * rp.CP_IROWH + 7, cap=6.0)
        return cp, x0 + rp.CP_RX, y0        # the first paint LOADS RAMPAGE

    def press(px, py, r, cap=2.0):
        q.click(px + (r[0] + r[2]) // 2, py + (r[1] + r[3]) // 2, cap)

    def dlg_to_b():
        """Step the Drive button until the chooser stands on B:, confirmed
        off the chooser's own block (rdpreserve's dlg_to_b)."""
        if not q.wait(fdlg_up, "a chooser", 20):
            return None
        q.done(4.0)
        w = chooser_win()
        for _ in range(6):
            if ch_drv() == 1:
                return w
            q.click(*ui.chooser_button_xy(ui.CH_DRIVE, w), cap=4.0)
        return w if ch_drv() == 1 else None

    def disk_window(letter):
        before = set(o.i for o in ui.windows())
        q.dblclick(*geom.drive_pt(q, letter, S))
        box = {}

        def got():
            for o in ui.windows():
                if o.i not in before and o.visible:
                    box["w"] = o
                    return True
            return False
        q.need(got, "drive %s's window" % letter, 30)
        q.done(4.0)
        return box["w"]

    def names(w):
        return [n for n, _ in ui.listing(w)]

    def select(w, name):
        w = next(o for o in ui.windows() if o.i == w.i)
        raise_win(w)
        i, _ = ui.entry(name, w)
        q.click(*ui.row_xy(w, i - ui.scroll(w)))

    def enter(w, folder, shows):
        select(w, folder)
        i, _ = ui.entry(folder, w)
        q.dblclick(*ui.row_xy(w, i - ui.scroll(w)))
        q.need(lambda: shows in names(w), "B:%s to list" % folder, 20)
        q.done(4.0)

    def paste_into(w, name, what):
        """Edit > Paste, and wait on the LISTING and then on the drive
        going quiet: the name appears at the directory write, before the
        last data sector is down (docs/reports, fcpcopy)."""
        menu_pick(q, ui, "Edit", "Paste")
        ok = q.wait(lambda: name in names(w), what, 90)
        q.done(15.0)
        return ok

    def store():
        """The store AS IT IS IN EXTENDED MEMORY, read physically."""
        return q.read(dd("rd_xbase"), dw("rd_kb") * 1024)

    def ctab():
        return q.read(dw("rd_dtab") * 16 + RD_DIRB, dw("rd_next") * 2)

    # --- 1: Xms, the size, Mount ---------------------------------------------
    cp, px, py = open_page(True)
    press(px, py, R_XMS)
    q.wait(lambda: db("rd_loc") == RDL_XMS, "[rd_loc] to read Xms", 10)
    check("X", db("rd_loc") == RDL_XMS, "the Store radio selected Xms",
          "[rd_loc] = %d - the radio is greyed, or the click missed"
          % db("rd_loc"))
    press(px, py, rp.R_SBOX)
    for c in str(a.kb):
        q.key(c)
    q.key("ret")
    q.done(4.0)
    check("K", dw("rd_kb") == a.kb, "the size box took %dKB" % a.kb,
          "[rd_kb] = %d" % dw("rd_kb"))
    press(px, py, rp.R_MOUNT)
    if not check("M", q.wait(lambda: db("rd_vol") != 0xFF and db("rd_have"),
                             "the mount", 30),
                 "Mount put the volume up",
                 "rd_vol %#x, rd_err %d" % (db("rd_vol"), db("rd_err"))):
        return
    q.done(4.0)
    xb, kb = dd("rd_xbase"), dw("rd_kb")
    check("M", xb >= 0x100000 and dw("rd_arena") == 0 and dw("rd_bounce"),
          "the store is EXTENDED: xbase %#x, no arena, bounce %#x, %d-byte "
          "extents, %d of them" % (xb, dw("rd_bounce"), dw("rd_extb"),
                                   dw("rd_next")),
          "arena %#x" % dw("rd_arena"))
    held = store_blocks()
    say("   (XMEM.DRV's blocks of >= %dKB: %r)" % (a.kb, held))
    check("M", any(b[3] == XM_OWN_KERN for b in held),
          "XMEM.DRV holds a live block of >= %dKB for it" % a.kb, repr(held))
    close(cp)
    letter = chr(ord("A") + db("rd_vol"))

    # --- 2: DATA.BIN onto it ---------------------------------------------------
    bw = disk_window("B")
    enter(bw, rp.SRC, rp.DOC)
    select(bw, rp.DOC)
    menu_pick(q, ui, "Edit", "Copy")
    dwin = disk_window(letter)
    if not check("C", paste_into(dwin, rp.DOC, "the paste onto %s:" % letter),
                 "%s copied onto %s:" % (rp.DOC, letter),
                 "rd_err %d, xerr %d, the listing is %r"
                 % (db("rd_err"), db("rd_xerr"), names(dwin))):
        return

    # W: the store in EXTENDED memory, walked by the live chain table from
    # the extent holding the file's first bytes. The payload is seeded noise,
    # so that extent cannot be found by accident; the chain says the rest.
    st, ct, extb = store(), ctab(), dw("rd_extb")
    first = [k for k in range(len(st) // extb)
             if st[k * extb:(k + 1) * extb] == data[:extb]]
    walk, n = bytearray(), 0
    if first:
        e = first[0]
        while len(walk) < len(data) and n <= len(ct) // 2:
            walk += st[e * extb:(e + 1) * extb]
            n += 1
            nxt = u16(ct, 2 * e)
            if nxt == rp.RD_CEND:
                break
            e = nxt - 1
    walk = bytes(walk[:len(data)])
    check("W", walk == data,
          "the EXTENDED store, walked by its chain from extent %s, IS %s "
          "(%d extents)" % (first[:1] or "?", rp.DOC, n),
          "no extent holds its first %d bytes" % extb if not first else
          "%d bytes walked, first difference at %d (extent %d of the walk)"
          % (len(walk), first_diff(walk, data),
             first_diff(walk, data) // extb))

    # --- 3: back through the kernel, to B:\ ------------------------------------
    close(bw)
    select(dwin, rp.DOC)
    menu_pick(q, ui, "Edit", "Copy")
    bw = disk_window("B")
    paste_into(bw, rp.DOC, "the copy back to B:")
    back = host_read(bimg, rp.DOC)
    check("R1", back == data, "%s copied back off the store to B:\\ is the "
          "source (%d bytes)" % (rp.DOC, len(data)),
          "read %s" % (None if back is None else "%d bytes" % len(back)))
    close(bw)
    close(dwin)

    # --- 4: Preserve As B:RAMDISK.RAM ------------------------------------------
    cp, px, py = open_page(False)
    press(px, py, rp.R_PRESA)
    if not check("D", dlg_to_b() is not None,
                 "Preserve As put up the Save chooser on B:"):
        return
    nxt = dw("rd_next")
    pre_st, pre_ct = store(), ctab()
    q.key("ret")                    # the default name, RAMDISK.RAM
    done = q.wait(lambda: not fdlg_up() and db("rd_flags") & rp.RDCF_BOOT,
                  "the preserve", 120)
    q.done(15.0)
    check("P", done, "Preserve As completed ([rd_flags] RDCF_BOOT)",
          "rd_err %d, rd_flags %#x" % (db("rd_err"), db("rd_flags")))
    img = host_read(bimg, rp.IMG)
    want = rp.RDI_ARENA + kb * 1024
    check("S", img is not None and len(img) == want,
          "B:%s is %d bytes" % (rp.IMG, want),
          "it is %s" % ("absent" if img is None else "%d" % len(img)))
    h = rp.parse_image(img) if img else None
    check("H", bool(h) and h["magic"] == b"O8RD" and h["ver"] == 2
          and h["bytes"] == kb * 1024 and h["kb"] == kb,
          "header: O8RD v2, %d arena bytes, %dKB" % (kb * 1024, kb),
          "read %r" % ({k: v for k, v in h.items() if k != "rows"}
                       if h else None))
    if img and len(img) >= rp.RDI_ARENA:
        arena = img[rp.RDI_ARENA:]
        bad = [c for c in range(0, max(len(arena), len(pre_st)), 4096)
               if arena[c:c + 4096] != pre_st[c:c + 4096]]
        check("A", not bad and len(arena) == len(pre_st),
              "the file's arena IS the extended store (%d 4KB chunks)"
              % (len(pre_st) // 4096),
              "4KB chunks differing at arena offsets %s" % bad[:8])
        tab = img[rp.RDI_META:rp.RDI_ARENA]
        check("A", tab[:len(pre_ct)] == pre_ct and not any(tab[len(pre_ct):]),
              "the file's chain table IS the chain claim (%d extents)" % nxt)
    if h:
        got, how = rp.file_from_image(img, h, rp.DOC)
        check("F", got == data, "%s out of the image alone (%s)"
              % (rp.DOC, how),
              "missing" if got is None else "%d bytes, first difference at "
              "%d" % (len(got), first_diff(got, data)))
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "os88disk.py"),
                        "--verify", bimg], capture_output=True, text=True)
    check("V", r.returncode == 0, "B: fscks clean",
          (r.stdout + r.stderr)[-300:])

    # --- 5: Unmount, Load, and the file off the LOADED volume ----------------
    press(px, py, rp.R_MOUNT)       # it reads Unmount while the volume is up
    if not check("U", q.wait(lambda: db("rd_vol") == 0xFF, "the unmount", 20),
                 "Unmount took the volume down"):
        return
    q.done(4.0)
    left = store_blocks()
    check("U", not db("rd_have") and not left,
          "...discarded the store and FREED the extended block",
          "rd_have %d, blocks still live: %r" % (db("rd_have"), left))
    press(px, py, rp.R_LOAD)
    w = dlg_to_b()
    if not check("D", w is not None, "Load put up the Open chooser on B:"):
        return
    ls = names(w)
    if not check("D", rp.IMG in ls, "%s is listed" % rp.IMG, repr(ls)):
        return
    q.click(*ui.row_xy(w, ls.index(rp.IMG) - ui.scroll(w)))
    q.key("ret")                    # a FILE in the Open form answers (38.4)
    ok = q.wait(lambda: not fdlg_up() and db("rd_vol") != 0xFF
                and db("rd_have"), "the load", 120)
    q.done(15.0)
    if not check("L", ok, "Load mounted the image", "rd_err %d"
                 % db("rd_err")):
        return
    check("L", db("rd_loc") == RDL_XMS and dd("rd_xbase") >= 0x100000,
          "the loaded store is EXTENDED (xbase %#x)" % dd("rd_xbase"))
    if img:
        check("L", dw("rd_kb") == kb and store() == img[rp.RDI_ARENA:],
              "the loaded extended store IS the file's arena (%dKB)"
              % dw("rd_kb"))
        check("L", ctab() == img[rp.RDI_META:rp.RDI_META
                                 + 2 * dw("rd_next")],
              "the loaded chain claim IS the file's table")
    close(cp)

    letter = chr(ord("A") + db("rd_vol"))
    dwin = disk_window(letter)
    select(dwin, rp.DOC)
    menu_pick(q, ui, "Edit", "Copy")
    bw = disk_window("B")
    enter(bw, "OUT", "KEEP.TXT")
    paste_into(bw, rp.DOC, "the copy into B:OUT")
    back = host_read(bimg, "OUT/" + rp.DOC)
    check("R2", back == data, "%s off the LOADED volume, on B:OUT, is the "
          "source" % rp.DOC,
          "read %s" % (None if back is None else "%d bytes" % len(back)))
    close(bw)
    close(dwin)

    # --- 6: the final Unmount --------------------------------------------------
    cp, px, py = open_page(False)
    press(px, py, rp.R_MOUNT)
    q.wait(lambda: db("rd_vol") == 0xFF, "the final unmount", 20)
    q.done(4.0)
    left = store_blocks()
    check("U2", db("rd_vol") == 0xFF and not db("rd_have") and not left,
          "the final Unmount discarded the store and freed its block",
          "rd_vol %#x, rd_have %d, blocks %r"
          % (db("rd_vol"), db("rd_have"), left))


if __name__ == "__main__":
    sys.exit(main())
