#!/usr/bin/env python3
"""The RAM disk's Preserve and Load, round-tripped and read from the HOST
(SPEC.md 62.9.12, 62.9.12.1).

    python3 tests/rdpreserve.py [--machine M] [--kb N] [--sys IMG --src ROOT]

Preserve writes the whole volume to one `.RAM` file - the metadata block with
OSAPI_FILE_WRITE, then the chain table and every arena chunk as ONE HELD
OSAPI_FILE_WRITE_SEQ stream, closed once (SPEC.md 18.4.9) - and Load reads it
back into a fresh store. Nothing else in the suite drives either button, so
this is the gate for both, and for the stream conversion in particular.

THE SESSION, on a GLaBIOS 5150 with a SCRATCH B: this row writes on the host:

  1  the Ram Disk page, the size box TYPED to `--kb` (136 by default: four
     32,768-byte chunks and an 8KB TAIL, so both of rd_preserve's chunk
     lengths run), and Mount
  2  B:SRC/DATA.BIN - 40,000 bytes of a fixed pseudo-random stream, so it
     spans 40 extents and no chunk of it can match by being zero - copied
     onto D: by the file manager
  3  Preserve As, the Save dialog stepped to B: with its Drive button, and the
     default name taken: B:RAMDISK.RAM
  4  Unmount, which DISCARDS the store (SPEC.md 62.9.11), then Load of that
     file back through the Open dialog
  5  DATA.BIN copied from D: back to B:'s root

THE CHECKS, every one of them on bytes the writer did not produce:

  S  the image's size is RDI_META + RDI_CTAB + the store, to the byte
  H  its header, read by a parser written from SPEC.md 62.9.12 and sharing no
     code with the driver: 'O8RD', version 2, the arena length, the store's KB
  A  the arena in the file IS the store in guest memory at the moment of the
     preserve, byte for byte, and the chain table IS the chain claim - so no
     chunk was dropped, repeated, shifted or written from a stale bounce
  F  DATA.BIN, reassembled from the FILE ALONE (its directory record, its
     chain through the table, its extents) equals the host's source
  Q  ...and it went down as ONE stream: the controller's read count across
     the preserve, from outside. A token that never reaches the kernel makes
     every WRITE_SEQ call cold - a lookup each, APPEND's cost - and writes a
     byte-identical image, so S, A and F cannot see it
  V  B: fscks clean (`tools/os88disk.py --verify`, via os88flush)
  L  after Load, the store in guest memory is the file's arena again and the
     chain claim its table
  R  and DATA.BIN copied back off the LOADED volume is the source byte for
     byte - the round trip through the user's own path, reading the volume
     Load mounted through the kernel's file layer

THE EXTENDED-MEMORY STORE IS NOT EXERCISED, for SPEC.md 62.9.14's reason:
every MartyPC machine is an 8088 and `OSAPI_XMEM_CAPS` answers 0, so the
Store radio is greyed and this is the conventional 32KB-chunk path. The 4KB
bounce chunk has no gate anywhere in this container.

RED CONTROL (docs/WRITING-TESTS.md 1), each a RAMDISK.DRV built from a
private copy of drivers/ and swapped onto a copy of the system disk with
`tools/os88fat.py del/add` - `--sys` boots that disk and `--src` maps that
driver's labels. The same route with the source UNCHANGED is green, so the
route is not what goes red:

  * the second 32KB chunk's WRITE_SEQ skipped: S (81,920 bytes - the patch's
    low-word compare also hit 98,304), A (four chunks differ), F, and L -
    Load REFUSES the short file, SPEC.md 62.9.12's count check doing its job.
  * `mov [rd_itok], di` dropped, so every call is cold: Q only (6 reads, as
    the OSAPI_FILE_APPEND writer this replaced; 62 writes against 47).
  * THE CLOSE REMOVED (`xor cx, cx` / `call OSAPI_FILE_WRITE_SEQ`) STAYS
    GREEN, and must: SPEC.md 18.4.9 commits a held stream at `gfx_unlock`,
    and Preserve runs inside the page's callback, so the commit lands before
    anything can read the disk - with the same 47 writes, the unlock's commit
    being the close's. No row can see that line; it is there for the day
    Preserve is not called under the lock.
"""
import argparse
import os
import random
import struct
import subprocess
import sys
import tempfile

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_ROOT, "tools"))
import os88build                                        # noqa: E402
import os88flush                                        # noqa: E402
import os88marty                                        # noqa: E402
import os88ui                                           # noqa: E402

# ctrl.inc's geometry and page.inc's, mirrored for tests/rdup.py's reason: a
# gate that derives its coordinates from the thing under test tests it
# against itself
CP_I0Y, CP_IROWH, CP_RX = 6, 14, 96
CP_DBY1, CP_DROWH, CP_IDRV = 20, 26, 2
DRVR_SZ, DRVR_SEG, RD_ROW = 16, 2, 3
TITLE_H = 18
R_SBOX = (42, 15, 101, 26)          # page.inc's rects, pane-relative
R_MOUNT = (2, 52, 65, 67)
R_LOAD = (152, 52, 215, 67)
R_PRESA = (2, 72, 97, 87)
# fdlg.inc's button column and list, content-relative (SPEC.md 38.3)
FD_BX1, FD_BX2 = 224, 286
FD_BY = (20, 40, 60)                # Open/Save, Cancel, Drive
FD_BH = 13
FD_LY0, FD_ROWH, FD_NROWS = 22, 16, 6

# rdabi.inc, the image (SPEC.md 62.9.12) - restated here deliberately: this
# is the INDEPENDENT reader, and a reader that imported the writer's numbers
# would agree with it about a wrong one
RDI_META, RDI_CTAB = 4096, 4096
RDI_ARENA = RDI_META + RDI_CTAB
RD_MAXEXT, RD_CEND = 2048, 0xFFFF
RDCF_BOOT = 1

SRC, DOC, IMG = "SRC", "DATA.BIN", "RAMDISK.RAM"
NDATA = 40000

fails = []


def say(*a):
    print(*a, flush=True)


def check(tag, ok, what, why=""):
    say("  %s %-4s %s%s" % (tag, "ok" if ok else "BAD", what,
                            "" if ok else "  -- " + why))
    if not ok:
        fails.append("%s: %s%s" % (tag, what, (" -- " + why) if why else ""))
    return ok


def u16(b, o=0):
    return b[o] | (b[o + 1] << 8)


def payload():
    """40,000 bytes nobody could produce by accident: a seeded stream, so a
    chunk written from the wrong offset cannot match by being zero."""
    return random.Random(62912).randbytes(NDATA)


def drv_syms(src):
    """ramdisk.asm's labels, by re-assembling it (tests/rdmove.py's trick,
    with the source root a parameter so a red-control tree maps ITS driver)."""
    rd = os.path.join(src, "drivers", "ramdisk")
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "r.asm"), os.path.join(d, "r.map")
        open(cp, "w").write(open(os.path.join(rd, "ramdisk.asm")).read()
                            + "\n[map symbols %s]\n" % mp)
        kb = (os.path.getsize(os88build.at("build/rampage.bin")) + 1023) // 1024
        subprocess.run(["nasm", "-f", "bin", "-w+error",
                        "-I", rd + "/", "-I", os.path.join(src, "drivers/"),
                        "-I", os.path.join(src, "apps/"),
                        "-I", os88build.at("build") + "/",
                        "-DRAMPAGE_KB=%d" % kb,
                        "-o", os.path.join(d, "r.bin"), cp], check=True)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[0], 16)
        return out


def scratch_b(work, data):
    """A blank 360KB volume holding SRC/DATA.BIN and nothing else, so the
    image and the copy-back both have room and the dialog lists two rows."""
    f = os.path.join(work, DOC)
    open(f, "wb").write(data)
    img = os.path.join(work, "b.img")
    r = subprocess.run([sys.executable, os.path.join(_ROOT, "tools",
                                                     "os88disk.py"),
                        "-o", img, "--size", "360", SRC + ":" + f],
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit("rdpreserve: os88disk: " + r.stderr)
    return img


def parse_image(b):
    """SPEC.md 62.9.12's three blocks, read from the text of the section."""
    if len(b) < RDI_ARENA:
        return None
    h = {"magic": bytes(b[0:4]), "ver": u16(b, 4), "extb": u16(b, 6),
         "nent": u16(b, 8), "bytes": struct.unpack_from("<I", b, 10)[0],
         "kb": u16(b, 14), "rows": {}}
    for i in range(h["nent"]):
        o = 32 + 24 * i
        name = bytes(b[o + 10:o + 24]).split(b"\0")[0].decode("latin1")
        h["rows"][name] = {"idx": b[o], "par": b[o + 1], "type": b[o + 2],
                           "ext": u16(b, o + 4),
                           "size": struct.unpack_from("<I", b, o + 6)[0]}
    return h


def file_from_image(b, h, name):
    """A file's bytes out of the image alone: its record, its chain walked
    through the table block, its extents out of the arena block."""
    row = h["rows"].get(name)
    if row is None:
        return None, "no directory record named %s (rows: %s)" % (
            name, sorted(h["rows"]))
    out, ext, seen = bytearray(), row["ext"], 0
    need = -(-row["size"] // h["extb"])
    while len(out) < row["size"]:
        if ext >= RD_MAXEXT or seen > need:
            return None, "the chain runs off at extent %d after %d" % (ext,
                                                                        seen)
        a = RDI_ARENA + ext * h["extb"]
        out += b[a:a + h["extb"]]
        seen += 1
        nxt = u16(b, RDI_META + 2 * ext)
        if nxt == RD_CEND:
            break
        ext = nxt - 1
    return bytes(out[:row["size"]]), "%d extents for %d needed" % (seen, need)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--kb", type=int, default=136,
                    help="the store, typed into the page (a multiple of 8)")
    ap.add_argument("--src", default=_ROOT,
                    help="the tree whose drivers/ramdisk the images carry")
    ap.add_argument("--sys", default="build/os8088-360.img",
                    help="the system disk to boot (a red control's, whose "
                         "RAMDISK.DRV was swapped)")
    ap.add_argument("--no-load", action="store_true",
                    help="stop after the preserve's checks")
    a = ap.parse_args()
    R = drv_syms(a.src)
    data = payload()
    with tempfile.TemporaryDirectory() as work:
        bimg = scratch_b(work, data)
        with os88ui.boot(os88build.at(a.sys), apps=bimg,
                         machine=a.machine) as ui:
            session(ui, a, R, data)
    for f in fails:
        say("  FAIL: " + f)
    say("rdpreserve: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


def session(ui, a, R, data):
    m, mo = ui.m, ui.mo

    def rd_seg():
        return u16(m.read(ui.sym("drv_tab") + RD_ROW * DRVR_SZ + DRVR_SEG, 2))

    def dw(name):                   # a driver word, at the driver's segment
        return u16(m.read(rd_seg() * 16 + R[name], 2))    # AS IT IS NOW

    def db(name):
        return m.read(rd_seg() * 16 + R[name], 1)[0]

    def wait(cond, what, guest=60.0):
        try:
            os88marty.until(m, lambda _: cond(), what, poll=0.2, guest=guest)
            return True
        except os88marty.MartyError:
            return False

    def fdlg_up():
        return u16(m.read(ui.sym("fdlg_win"), 2)) != 0

    # --- the panel, its Drivers page, the row, the Ram Disk page -----------
    def open_page(first):
        mo.menu(8, 8, 8, 40)
        cp = ui.wait_window("Control Panel")
        x0, y0 = cp.x + 1, cp.y + TITLE_H
        if first:
            mo.click(x0 + 40, y0 + CP_I0Y + CP_IDRV * CP_IROWH + 7)
            ui.settle(limit=15.0)
            mo.click(x0 + CP_RX + 40,
                     y0 + CP_DBY1 + RD_ROW * CP_DROWH + CP_DROWH // 2)
            if not wait(rd_seg, "the RAM disk driver to load", 30.0):
                sys.exit("rdpreserve: the RAM disk driver did not load")
            ui.settle(limit=25.0)
        nst = m.read(ui.sym("cp_nst"), 1)[0]
        mo.click(x0 + 40, y0 + CP_I0Y + nst * CP_IROWH + 7)
        ui.settle(limit=25.0)           # the first paint LOADS RAMPAGE.DRV
        return cp, x0 + CP_RX, y0

    def press(px, py, r):
        mo.click(px + (r[0] + r[2]) // 2, py + (r[1] + r[3]) // 2)

    def dlg_rect():
        """The dialog is modal and in front while [fdlg_win] is set."""
        return ui.front() if fdlg_up() else None

    def dlg_names():
        """What the DIALOG lists - its own store (SPEC.md 38.5), not a Disk
        window's cache, which is what `ui.listing` would read."""
        n = u16(m.read(ui.sym("disk_nfiles"), 2))
        vseg = u16(m.read(ui.sym("fdlg_vseg"), 2))
        return [x for x, _ in os88ui._decode(
            m.read(vseg << 4, n * os88ui.geom.DSK_DE_STRIDE), n)]

    def dlg_to_b():
        """Step the dialog's Drive button until it stands on B: (SPEC.md
        38.11 cycles every volume), confirmed off [disk_drive]."""
        d = dlg_rect()
        if d is None:
            return False
        for _ in range(6):
            if m.read(ui.sym("disk_drive"), 1)[0] == 1:
                return True
            mo.click(d.x + 1 + (FD_BX1 + FD_BX2) // 2,
                     d.y + TITLE_H + FD_BY[2] + FD_BH // 2)
            ui.settle(limit=20.0)
        return m.read(ui.sym("disk_drive"), 1)[0] == 1

    say("rdpreserve: %s, a %dKB conventional store, %d bytes of %s"
        % (a.machine, a.kb, len(data), DOC))

    # --- 1: size and mount -------------------------------------------------
    cp, px, py = open_page(True)
    press(px, py, R_SBOX)
    ui.settle(limit=10.0)
    for c in str(a.kb):
        m.key("Digit" + c)
    m.key("Enter")
    ui.settle(limit=10.0)
    check("K", dw("rd_kb") == a.kb, "the size box took %dKB" % a.kb,
          "[rd_kb] reads %d" % dw("rd_kb"))
    press(px, py, R_MOUNT)
    if not check("M", wait(lambda: db("rd_vol") != 0xFF and dw("rd_arena"),
                           "the volume to mount"),
                 "Mount put the volume up", "[rd_vol] is still FF"):
        return
    ui.settle(limit=15.0)
    ui.close(cp)
    letter = chr(ord("A") + db("rd_vol"))

    # --- 2: DATA.BIN onto it -----------------------------------------------
    bw = ui.open_drive("B")
    ui.open(SRC, expect="nav", win=bw)
    i, _ = ui.entry(DOC, bw)
    mo.click(*ui.row_xy(bw, ui.scroll_to(i, win=bw)))
    ui.settle(limit=10.0)
    ui.menu_pick("Edit", "Copy")
    dwin = ui.open_drive(letter)
    ui.menu_pick("Edit", "Paste")
    ok = wait(lambda: DOC in [n for n, _ in ui.listing(dwin)],
              "the paste to land on %s:" % letter, 90.0)
    ui.settle(limit=20.0)
    if not check("C", ok, "%s copied onto %s:" % (DOC, letter)):
        return

    # --- 3: Preserve As, onto B: -------------------------------------------
    cp, px, py = open_page(False)
    press(px, py, R_PRESA)
    if not check("D", wait(fdlg_up, "the Save dialog", 20.0),
                 "Preserve As put up the Save dialog"):
        return
    ui.settle(limit=15.0)
    if not check("D", dlg_to_b(), "the dialog stepped to B:",
                 "[disk_drive] is %d" % m.read(ui.sym("disk_drive"), 1)[0]):
        return
    m.disk(reset=True)
    m.key("Enter")                  # the default name, RAMDISK.RAM
    done = wait(lambda: not fdlg_up() and db("rd_flags") & RDCF_BOOT,
                "the preserve to finish", 120.0)
    # the held stream is committed at the close, or at the gfx unlock at the
    # latest: let the drive go quiet before the store is read
    os88marty.quiesce(m, lambda: m.disk().get("writes"), guest=1.0,
                      what="the floppy writes to stop")
    ui.settle(limit=20.0)
    io = m.disk()
    say("  (preserve: %s)" % io)
    check("P", done, "Preserve As completed ([rd_flags] RDCF_BOOT)",
          "the page caption is the verdict - SPEC.md 62.9.12's 'Disk error "
          "on the image' is RDERR_FILE")
    kb, next_ = dw("rd_kb"), dw("rd_next")
    # Q: ONE STREAM. A HOT WRITE_SEQ call starts at the record's last cluster
    # instead of looking the name up (SPEC.md 18.4.9), so the image's
    # STREAM_CALLS writes read the disk about once between them; a token that
    # is lost or never carried makes every call COLD - an APPEND's lookup
    # each - and the image comes out byte-identical, so nothing above can see
    # it. Counted by the controller from outside, exact at any load. Measured
    # on this machine: 1 read held, 6 with the token dropped and 6 on the
    # OSAPI_FILE_APPEND writer it replaced; the bound sits between.
    calls = 1 + -(-kb * 1024 // 32768)
    check("Q", io.get("reads", 99) <= calls // 2,
          "the image went down as ONE stream (%d disk reads for %d stream "
          "calls)" % (io.get("reads", -1), calls),
          "a lookup per call - every WRITE_SEQ call was COLD, so the token "
          "is not reaching the kernel (rd_itok)")
    store = m.read(dw("rd_arena") * 16, kb * 1024)
    ctab = m.read(dw("rd_ctab") * 16, next_ * 2)
    ui.close(cp)

    fl = os88flush.Flush(marty=m)
    vol = fl.volume(1)
    try:
        img = vol.read(IMG)
    except Exception as e:          # noqa: BLE001 - the verdict says what
        img = None
        say("  (%s)" % e)
    want = RDI_ARENA + kb * 1024
    check("S", img is not None and len(img) == want,
          "B:%s is %d bytes" % (IMG, want),
          "it is %s" % ("absent" if img is None else "%d" % len(img)))
    h = parse_image(img) if img else None
    check("H", bool(h) and h["magic"] == b"O8RD" and h["ver"] == 2
          and h["bytes"] == kb * 1024 and h["kb"] == kb,
          "header: O8RD v2, %d arena bytes, %dKB" % (kb * 1024, kb),
          "read %r" % ({k: v for k, v in h.items() if k != "rows"}
                       if h else None))
    if img and len(img) >= RDI_ARENA:
        arena = img[RDI_ARENA:]
        bad = [c for c in range(0, max(len(store), len(arena)), 32768)
               if arena[c:c + 32768] != store[c:c + 32768]]
        check("A", not bad and len(arena) == len(store),
              "the file's arena IS the store (%d chunks)" % (
                  -(-len(store) // 32768)),
              "chunks differing at arena offsets %s" % bad)
        tab = img[RDI_META:RDI_ARENA]
        check("A", tab[:len(ctab)] == ctab and not any(tab[len(ctab):]),
              "the file's chain table IS the chain claim (%d extents)"
              % next_)
    if h:
        got, how = file_from_image(img, h, DOC)
        check("F", got == data, "%s out of the image alone (%s)" % (DOC, how),
              "%s" % ("missing" if got is None else
                      "%d bytes, first difference at %d" % (
                          len(got), next((i for i, (x, y) in
                                          enumerate(zip(got, data))
                                          if x != y), min(len(got),
                                                          len(data))))))
    rc, why = fl.verify(1)
    check("V", rc == 0, "B: fscks clean", why)
    if a.no_load or not img:
        return

    # --- 4: Unmount, which discards; then Load ------------------------------
    cp, px, py = open_page(False)
    press(px, py, R_MOUNT)          # it reads Unmount while the volume is up
    if not check("U", wait(lambda: db("rd_vol") == 0xFF, "the unmount", 20.0),
                 "Unmount took the volume down"):
        return
    ui.settle(limit=15.0)
    press(px, py, R_LOAD)
    if not check("D", wait(fdlg_up, "the Open dialog", 20.0),
                 "Load put up the Open dialog"):
        return
    ui.settle(limit=15.0)
    if not check("D", dlg_to_b(), "the Open dialog stepped to B:"):
        return
    names = dlg_names()
    say("  (the dialog lists %s)" % names)
    if IMG not in names or names.index(IMG) >= FD_NROWS:
        check("D", False, "%s is on the dialog's first page" % IMG,
              "it lists %s" % names)
        return
    d = dlg_rect()
    mo.click(d.x + 1 + 40,
             d.y + TITLE_H + FD_LY0 + names.index(IMG) * FD_ROWH + FD_ROWH // 2)
    ui.settle(limit=10.0)
    m.key("Enter")
    ok = wait(lambda: not fdlg_up() and db("rd_vol") != 0xFF
              and dw("rd_arena"), "the load to mount", 120.0)
    ui.settle(limit=20.0)
    if not check("L", ok, "Load mounted the image"):
        return
    kb2 = dw("rd_kb")
    store2 = m.read(dw("rd_arena") * 16, kb2 * 1024)
    ctab2 = m.read(dw("rd_ctab") * 16, dw("rd_next") * 2)
    check("L", kb2 == kb and store2 == img[RDI_ARENA:],
          "the loaded store IS the file's arena (%dKB)" % kb2)
    check("L", ctab2 == img[RDI_META:RDI_META + len(ctab2)],
          "the loaded chain claim IS the file's table")
    ui.close(cp)

    # --- 5: and the file comes back off the LOADED volume -------------------
    letter = chr(ord("A") + db("rd_vol"))
    dwin = ui.open_drive(letter)
    i, _ = ui.entry(DOC, dwin)
    mo.click(*ui.row_xy(dwin, ui.scroll_to(i, win=dwin)))
    ui.settle(limit=10.0)
    ui.menu_pick("Edit", "Copy")
    bw = ui.open_drive("B")         # a NEW window, on B:'s root - where
    if SRC not in [n for n, _ in ui.listing(bw)]:   # DATA.BIN is not yet
        check("R", False, "a B: window on the root to paste into",
              "it lists %s" % [n for n, _ in ui.listing(bw)])
        return
    ui.menu_pick("Edit", "Paste")
    wait(lambda: DOC in [n for n, _ in ui.listing(bw)],
         "the copy back to land on B:", 90.0)
    os88marty.quiesce(m, lambda: m.disk().get("writes"), guest=1.0,
                      what="the floppy writes to stop")
    try:
        back = fl.volume(1).read(DOC)
    except Exception as e:          # noqa: BLE001
        back = None
        say("  (%s)" % e)
    check("R", back == data,
          "%s copied back off the loaded volume is the source" % DOC,
          "read %s" % ("nothing" if back is None else "%d bytes" % len(back)))
    rc, why = fl.verify(1)
    check("V", rc == 0, "B: still fscks clean", why)


if __name__ == "__main__":
    sys.exit(main())
