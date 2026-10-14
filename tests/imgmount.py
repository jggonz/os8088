"""imgmount - a .IMG mounted as a drive, end to end (SPEC.md 18.101).

A 1.44MB B: carries four images (tests/imgfix.py builds them, into this row's
own directory): TEST.IMG FRAGMENTED into runs stored out of order, RO.IMG
read-only on the host, STALE.IMG wearing the marker a machine that stopped
mid-mount leaves, and BAD.IMG, sixteen zero sectors with no FAT. Every step
is a double-click, the one gesture the feature has, and every assertion that
matters is read back off the DISK, not out of os8088: the writer and the
reader inside are one FAT implementation, and the defect a mount can have
that matters - a write landing outside the file - cannot be seen from in
there.

  0. Every *.IMG in B:'s listing draws the desktop's 3.5" diskette at half
     size (SPEC.md 18.101.6), pixel for pixel, and PAD.BIN does not.
  1. TEST.IMG mounts: a DRVC_IMG row appears, its toast says which drive,
     and D: lists the file inside the image, read through all four runs.
  2. ...and while it is a drive its host file is READ-ONLY with byte 12
     marked, so Delete on it is refused and it is still there.
  3. New Folder on D: writes the image. Read back on the host: the image's
     root holds GATE, the image passes os88disk --verify, and NO host sector
     outside TEST.IMG's clusters changed but the one holding its entry.
  4. The second double-click unmounts: the row is free, the toast says so,
     and the entry's attribute and byte 12 are back to what they were.
  5. RO.IMG mounts READ-ONLY (IMG_FLG), New Folder on it changes nothing,
     and its unmount leaves the user's read-only bit where it was.
  6. STALE.IMG's marker is taken as a mount the machine never finished: it
     mounts WRITABLE and its unmount clears the read-only bit the stop left.
  7. BAD.IMG says 'Not a disk image' and leaves no row and no claim.
  8. The Disk menu's half (SPEC.md 18.101.5, 22.28): Unmount Image is GREY on
     a host window; the empty-space context menu's last item opens Mount
     Image's Open box; Disk > Mount Image... mounts TEST.IMG picked in it;
     Unmount Image is LIVE in the image's own window, and picking it
     unmounts, puts the attribute back and sends the window to the host.
  9. ...and on the DESKTOP: a right-press on the image's own drive icon pops
     Unmount Image, and picking it takes the row, the zone and the claim.

    python3 tests/imgmount.py [--machine NAME]
"""
import os
import struct
import subprocess
import sys

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88build                                            # noqa: E402
import os88marty                                            # noqa: E402
import os88ui                                               # noqa: E402
import imgfix                                               # noqa: E402

MACHINE = "os8088_5150_herc_gla_144"
if "--machine" in sys.argv:
    MACHINE = sys.argv[sys.argv.index("--machine") + 1]
SYS_IMG = "build/os8088.img"
OUT = os88build.at("build/imgmount") if hasattr(os88build, "at") \
    else "build/imgmount"
DRVC_IMG = 0x41
IMG_FLG = 5
DV_SIZE = 16
FS_DRV = int(__import__("re").search(r"^FS_DRV\s+equ\s+(\d+)",
             open("kernel/files.inc").read(), __import__("re").M).group(1))
fails = []


def say(s):
    print("  " + s)


def check(ok, what, got=None):
    say("%-4s %s%s" % ("ok" if ok else "FAIL", what,
                      "" if got is None else "  [%r]" % (got,)))
    if not ok:
        fails.append(what)


def rows(ui):
    """[(volume, kind, class, sectors, label, claim)] for every row."""
    base = ui._S("dsk_vtab")
    out = []
    for v in range(8):
        r = bytes(ui.m.read(base + DV_SIZE * v, DV_SIZE))
        out.append((v, r[0], r[3], r[4] | r[5] << 8,
                    r[6:12].split(b"\0")[0].decode("ascii", "replace"),
                    r[12] | r[13] << 8))
    return out


def images(ui):
    return [r for r in rows(ui) if r[1] == 1 and r[2] == DRVC_IMG]


def img_claims(ui):
    """How many live heap claims carry MEM_K_IMG (kernel/memory.inc's
    mem_tab: MEM_MAX records of MC_SIZE = 10, segment at +0, owner at +4)."""
    raw = bytes(ui.m.read(ui._S("mem_tab"), 32 * 10))
    return sum(1 for i in range(32)
               if raw[i * 10] | raw[i * 10 + 1]
               and (raw[i * 10 + 4] | raw[i * 10 + 5] << 8) == 0xFF12)


def wait_images(ui, n, what):
    os88marty.until(ui.m, lambda _: len(images(ui)) == n, what, guest=60)
    return images(ui)


def host_now(ui, tag):
    path = os.path.abspath(os.path.join(OUT, "flush-%s.img" % tag))
    ui.m.flush(1, path)
    with open(path, "rb") as f:
        return f.read()


def entry(data, n83):
    fat = imgfix.Fat12(data)
    e = fat.entry(n83)
    return fat, e, data[e + 11], data[e + 12]


def image_of(data, n83):
    fat, e, _, _ = entry(data, n83)
    first, size = struct.unpack_from("<HI", data, e + 26)
    return b"".join(fat.clus(c) for c in fat.chain(first))[:size]


def root_names(img):
    fat = imgfix.Fat12(img)
    out = []
    for i in range(fat.rootn):
        o = fat.rootlba * 512 + i * 32
        if img[o] in (0, 0xE5) or img[o + 11] == 0x0F:
            continue
        out.append(img[o:o + 11].decode("ascii", "replace"))
    return out


def fsck(img, tag):
    p = os.path.join(OUT, "%s.img" % tag)
    with open(p, "wb") as f:
        f.write(img)
    r = subprocess.run([sys.executable, "tools/os88disk.py", "--verify", p],
                       capture_output=True, text=True)
    return r.returncode == 0, (r.stdout + r.stderr).strip().splitlines()[-1:]


def new_folder(ui, name):
    ui.m.key("KeyN")
    os88marty.settle(ui.m)
    for ch in name:
        ui.m.key("Key" + ch)
    ui.m.key("Enter")
    os88marty.settle(ui.m)


def dblclick(ui, name):
    w = ui.open_drive("B")
    ui.open(name, expect=None, win=w)   # a mount opens no window: the row
                                        # is the answer, and each caller waits
                                        # for it


os.makedirs(OUT, exist_ok=True)
host, runs = imgfix.build_set(os.path.join(OUT, "fix"))
say("TEST.IMG on the host in %d runs: %r" % (len(runs[b"TEST    IMG"]),
                                             runs[b"TEST    IMG"]))
with open(host, "rb") as f:
    orig = f.read()
orig_test = image_of(orig, b"TEST    IMG")

# SPEC.md 18.101.6's picture, derived the way dsk_img_icon derives it: the
# desktop diskette's 32 data rows (icons.inc's ico_disk32, its pool rows),
# OR-folded two pixels to one and two rows to one. '#' is ink.
_POOL = {"S": 0x3FFFFFFC, "L": 0x24000024, "T": 0x27FFFFE4, "H": 0x20000004,
         "B": 0x207FFE04, "D": 0x20400204, "U": 0x2040F204}
_ROWS = "S" + "L" * 15 + "T" + "H" * 4 + "B" + "D" + "U" * 7 + "D" + "S"


def floppy16():
    out = []
    for r in range(16):
        v = _POOL[_ROWS[2 * r]] | _POOL[_ROWS[2 * r + 1]]
        out.append("".join("#" if (v >> (30 - 2 * k)) & 3 else "."
                           for k in range(16)))
    return out


def icon_at(ui, w, name):
    """The 16x16 list-view icon on `name`'s row, '#' = ink (black)."""
    row = ui.scroll_to(ui.entry(name, w)[0], win=w)
    os88marty.settle(ui.m)              # the rows are PIXELS here
    x0 = w.x + 1 + 8                    # fm_pen8: the icon is at content x 8
    y0 = w.y + 18 + 22 + row * 16       # TITLE_H, then FM_ROW_Y0
    wv, hv, rr = ui.m.vram(None)
    return ["".join("." if rr[y0 + y][x0 + x] else "#" for x in range(16))
            for y in range(16)]


with os88ui.boot(SYS_IMG, apps=host, machine=MACHINE) as ui:
    # --- 0. the icon --------------------------------------------------------
    w = ui.open_drive("B")
    want = floppy16()
    for n in ("TEST.IMG", "RO.IMG", "BAD.IMG"):
        got = icon_at(ui, w, n)
        check(got == want, "%s draws the halved desktop diskette" % n,
              None if got == want else "\n" + "\n".join(got))
    got = icon_at(ui, w, "PAD.BIN")
    check(got != want, "...and PAD.BIN, which is no image, does not", None)

    # --- 1. mount ---------------------------------------------------------
    dblclick(ui, "TEST.IMG")
    (v, _k, _c, secs, lbl, seg), = wait_images(ui, 1, "TEST.IMG's row")
    letter = chr(ord("A") + v)
    txt = ui.wait_toast(says="is drive")
    check(txt == "TEST.IMG is drive %s:" % letter, "the mount says which "
          "drive", txt)
    check(secs == 720 and lbl == "TEST" and seg != 0,
          "the row: 720 sectors, label TEST, a claim", (secs, lbl, seg))
    check(img_claims(ui) == 1, "...one MEM_K_IMG claim", img_claims(ui))
    ui.open_drive(letter)
    names = [r[0] for r in ui.listing()]
    check(names == ["HELLO.TXT"], "%s: lists the file inside the image, "
          "read through every run" % letter, names)

    # --- 2. the host file is protected while mounted ---------------------
    data = host_now(ui, "mounted")
    _f, _e, attr, res = entry(data, b"TEST    IMG")
    check(attr & 1 and res == imgfix.IMG_MARK, "the host file is read-only "
          "and marked while it is a drive", (attr, res))
    w = ui.open_drive("B")
    idx, _ty = ui.entry("TEST.IMG", w)
    row = ui.scroll_to(idx, win=w)
    ui.mo.click(*ui.row_xy(w, row))
    os88marty.settle(ui.m)
    ui.m.key("Delete")
    os88marty.settle(ui.m)
    ui.m.key("Delete")
    os88marty.settle(ui.m)
    check("TEST.IMG" in [r[0] for r in ui.listing(w)],
          "Delete is refused on a mounted image's file", ui.toast()[0])

    # --- 3. a write through the image ------------------------------------
    ui.open_drive(letter)
    new_folder(ui, "GATE")
    names = [r[0] for r in ui.listing()]
    check("GATE" in names, "New Folder on %s:" % letter, names)
    data = host_now(ui, "written")
    img = image_of(data, b"TEST    IMG")
    check("GATE       " in root_names(img), "...is in TEST.IMG's own root, "
          "read back on the host", root_names(img))
    ok, line = fsck(img, "inner")
    check(ok, "...and the image passes os88disk --verify", line)
    fat = imgfix.Fat12(orig)
    e = fat.entry(b"TEST    IMG")
    allowed = set()
    for first, lba, n in runs[b"TEST    IMG"]:
        allowed.update(range(lba, lba + n))
    allowed.add(e // 512)
    moved = sorted({i // 512 for i in range(len(data))
                    if data[i] != orig[i]})
    check(moved and set(moved) <= allowed, "no host sector outside the file "
          "changed but its entry's", [s for s in moved if s not in allowed])
    check(img != orig_test, "...and the image's bytes did change", None)

    # --- 4. unmount -------------------------------------------------------
    dblclick(ui, "TEST.IMG")
    wait_images(ui, 0, "TEST.IMG's row to go")
    txt = ui.wait_toast(says="unmounted")
    check(txt == "TEST.IMG unmounted", "the second double-click unmounts",
          txt)
    data = host_now(ui, "unmounted")
    _f, _e, attr, res = entry(data, b"TEST    IMG")
    _f, _e, oattr, ores = entry(orig, b"TEST    IMG")
    check((attr, res) == (oattr, ores), "the entry's attribute and byte 12 "
          "are back", ((attr, res), (oattr, ores)))
    ok, line = fsck(data, "host")
    check(ok, "the host passes os88disk --verify", line)

    # --- 5. the user's read-only image ------------------------------------
    dblclick(ui, "RO.IMG")
    (v, *_rest), = wait_images(ui, 1, "RO.IMG's row")
    seg = images(ui)[0][5]
    flg = ui.m.read(seg * 16 + IMG_FLG, 1)[0]
    check(flg & 1, "RO.IMG mounts READ-ONLY", flg)
    letter = chr(ord("A") + v)
    ui.open_drive(letter)
    new_folder(ui, "RONLY")
    check("RONLY" not in [r[0] for r in ui.listing()],
          "New Folder on a read-only image changes nothing", ui.toast()[0])
    dblclick(ui, "RO.IMG")
    wait_images(ui, 0, "RO.IMG's row to go")
    data = host_now(ui, "ro")
    _f, _e, attr, res = entry(data, b"RO      IMG")
    check(attr & 1 and res == 0, "...and its unmount leaves the user's "
          "read-only bit alone", (attr, res))
    check(image_of(data, b"RO      IMG") == image_of(orig, b"RO      IMG"),
          "...and not one byte of RO.IMG moved", None)

    # --- 6. a marker the machine never cleared ----------------------------
    dblclick(ui, "STALE.IMG")
    wait_images(ui, 1, "STALE.IMG's row")
    seg = images(ui)[0][5]
    flg = ui.m.read(seg * 16 + IMG_FLG, 1)[0]
    check(flg == 0, "a stale marker mounts WRITABLE", flg)
    dblclick(ui, "STALE.IMG")
    wait_images(ui, 0, "STALE.IMG's row to go")
    data = host_now(ui, "stale")
    _f, _e, attr, res = entry(data, b"STALE   IMG")
    check(not attr & 1 and res == 0, "...and its unmount clears what the "
          "stop left", (attr, res))

    # --- 7. not an image ----------------------------------------------------
    dblclick(ui, "BAD.IMG")
    txt = ui.wait_toast(says="disk image")
    check(txt == "Not a disk image", "BAD.IMG is refused", txt)
    check(images(ui) == [], "...and leaves no row", images(ui))
    check(img_claims(ui) == 0, "...and no MEM_K_IMG claim survives any of "
          "the four", img_claims(ui))
    data = host_now(ui, "bad")
    _f, _e, attr, res = entry(data, b"BAD     IMG")
    check((attr & 1, res) == (0, 0), "...and its entry untouched",
          (attr, res))

    # --- 8. Disk > Mount Image... / Unmount Image ---------------------------
    def disk_items(ui):
        ui.menu_pick("Nav", "Refresh")      # a press on the bar is what runs
        cells = {c[0]: c[3] for c in ui.menus()}    # fm_bar_gate
        return dict(cells.get("Disk", []))

    w = ui.open_drive("B")
    bar = [c[0] for c in ui.menus()]
    check([c for c in bar if c not in ("Apple", "Locator")]
          == ["File", "Edit", "Nav", "Disk"], "the Disk window's bar "
          "is File, Edit, Nav, Disk", bar)
    items = disk_items(ui)
    check(items.get("Unmount Image") is False, "Unmount Image is grey on a "
          "host's window", items)
    base = ui._S("fm_cxi_dir")              # the empty space's menu, in the
    raw = bytes(ui.m.read(base, 12))        # order SPEC.md 22.28 gives it
    ctx = [ui._str(os88marty.KERNEL_SEG, raw[2 * i] | raw[2 * i + 1] << 8)
           .lstrip("\x01") for i in range(6)]
    check(ctx == ["Refresh", "Up One Folder", "-" * 16, "Paste",
                  "New Folder...", "Mount Image..."], "the empty space's menu "
          "is what you SEE, a rule, then what you DO", ctx)
    n = len(ui.listing(w))
    rx, ry = ui.row_xy(w, n + 1)            # below the last row: empty space

    def aim(_mo):
        os88marty.until(ui.m, lambda mm: mm.read(ui._S("menu_btn"), 1)[0]
                        == 3, "the context menu", guest=10)
        x = ui._word("menu_x1")
        y = ui._word("menu_y1")
        return x + 8, y + 1 + 5 * 16 + 8   # item 5: Mount Image...
    ui.mo.rmenu(rx, ry, 0, 0, aim=aim)
    ch = ui.chooser(limit=60)
    check(ch.title == "Open", "the empty space's Mount Image... opens the "
          "Open box", ch.title)
    ui.chooser_cancel()

    ui.menu_pick("Disk", "Mount Image...")
    ch = ui.chooser(limit=60)
    for _ in range(4):                      # its Drive button, to B:
        if ui.m.read(ui._fsblk(ch) + FS_DRV, 1)[0] == 1:
            break
        ui.chooser_button(ui.CH_DRIVE, ch)
        os88marty.settle(ui.m)
    ui.chooser_select("TEST.IMG", ch)
    ui.m.key("Enter")
    (v, *_rest), = wait_images(ui, 1, "TEST.IMG's row, by the dialog")
    letter = chr(ord("A") + v)
    txt = ui.wait_toast(says="is drive")
    check(txt == "TEST.IMG is drive %s:" % letter, "Disk > Mount Image... "
          "mounts what the Open box answered", txt)
    data = host_now(ui, "dlg")
    _f, _e, attr, res = entry(data, b"TEST    IMG")
    check(attr & 1 and res == imgfix.IMG_MARK, "...marked on the host, found "
          "by the DIALOG's folder", (attr, res))

    iw = ui.open_drive(letter)
    items = disk_items(ui)
    check(items.get("Unmount Image") is True, "Unmount Image is live in the "
          "image's own window", items)
    ui.menu_pick("Disk", "Unmount Image")
    wait_images(ui, 0, "TEST.IMG's row to go, by the menu")
    txt = ui.wait_toast(says="unmounted")
    check(txt == "TEST.IMG unmounted", "Disk > Unmount Image unmounts it",
          txt)
    drv = ui.m.read(ui._fsblk(iw) + FS_DRV, 1)[0]
    check(drv == 1, "...and the window goes to the HOST", drv)
    data = host_now(ui, "dlgun")
    _f, _e, attr, res = entry(data, b"TEST    IMG")
    check((attr, res) == (oattr, ores), "...and puts the attribute back, "
          "the file found by the name the claim kept", (attr, res))
    check(img_claims(ui) == 0, "...and frees its claim", img_claims(ui))

    # --- 9. the desktop's right-click ---------------------------------------
    ui.close(iw)                            # it lies over B:'s own, which
    dblclick(ui, "TEST.IMG")                # a raise would click through
    (v, *_rest), = wait_images(ui, 1, "TEST.IMG's row, for the desktop")
    letter = chr(ord("A") + v)
    for w in list(ui.windows()):            # the icons are under the windows
        ui.close(w)
    zx, zy = os88ui.geom.drive_pt(ui.m, letter, ui.sym)

    def aim0(_mo):
        os88marty.until(ui.m, lambda mm: mm.read(ui._S("menu_btn"), 1)[0]
                        == 3, "the drive icon's menu", guest=10)
        return ui._word("menu_x1") + 8, ui._word("menu_y1") + 1 + 8
    ui.mo.rmenu(zx, zy, 0, 0, aim=aim0)
    wait_images(ui, 0, "TEST.IMG's row to go, by the desktop")
    txt = ui.wait_toast(says="unmounted")
    check(txt == "TEST.IMG unmounted", "a right-press on %s:'s icon unmounts "
          "it" % letter, txt)
    check(img_claims(ui) == 0, "...and frees its claim", img_claims(ui))
    data = host_now(ui, "desk")
    _f, _e, attr, res = entry(data, b"TEST    IMG")
    check((attr, res) == (oattr, ores), "...and puts the attribute back",
          (attr, res))

for f in fails:
    print("FAIL " + f)
print("imgmount: %s (%d failed)" % ("FAIL" if fails else "ok", len(fails)))
sys.exit(1 if fails else 0)
