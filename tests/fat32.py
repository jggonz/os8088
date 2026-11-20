#!/usr/bin/env python3
"""fat32 - a FAT32 volume read and written by kern_mega (SPEC.md 19.12), on
QEMU's 386 with SeaBIOS.

    make && make mega && python3 tests/fat32.py [--keep]

The volume is the HOST's: mtools formats a FAT32 partition (`mformat -F`, one
sector a cluster, so 190,000-odd clusters and FAT32 by count alone) behind a
hand-made MBR, type 0Ch, and puts a 40MB FILLER.BIN first - so FAR/, the files
in it and everything the guest writes land past cluster 65,535, where a
cluster's high word is not 0 and every place that kept one word is wrong.
kern_mega boots with HDD.DRV wanted by SYSTEM.CFG (hdext's), and:

  1. C: is mounted: HDD.DRV offered 0Ch and the kernel's mount read it
     (19.12.10), FSInfo made "unknown" on the way (19.12.7);
  2. C:'s root lists FILLER.BIN, FAR, HELLO.TXT and CALC.O88 - the root is a
     cluster chain at RootClus (19.12.5);
  3. FAR is entered, and CALC.O88 LAUNCHED out of it: a folder and a file
     past 65,535 - the listing's +14, FstClusHI, the loader's whole cluster
     (19.12.6), and the FAT's 4-byte window (19.12.4);
  4. in C:/FAR, HELLO.TXT is deleted (a free chain written back) and New
     Folder makes NEWDIR (a cluster allocated past 65,535, zeroed, its `.`
     and `..` written whole - 19.12.7);
  5. the HOST reads the volume back: `mdir` sees NEWDIR and no HELLO.TXT in
     FAR, `..` in NEWDIR names FAR's cluster, FILLER.BIN is its bytes, and
     FSInfo's free count is 0FFFFFFFFh. `fsck.fat -n` must be clean where the
     host has it (dosfstools);
  6. the root's CALC.O88 is copied into FAR/NEWDIR with Ctrl+C / Ctrl+V -
     the copy engine with both folders FAT32 and the destination past
     65,535 (19.12.8) - and the host reads it back past 65,535 holding the
     package's bytes, through its own reader and through `mtype`.

kern_big's half - the same partition offered and REFUSED by its BPB check,
the fence 19.12.10 relies on - is leg `--big`.

REQUIRES mtools. Writes only its own staged images under build/.
Break on purpose: dsk_bpb_check's `.f32` arm refused (stc), and 1 fails;
ld_res_name back to taking BX alone, and 3 fails; dskw_dotents writing a
word into `..`, and 5 fails.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
BIG = "--big" in sys.argv
if not BIG:                             # kern_mega's map (pciether's lines:
    os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "megak")
import os88build                                              # noqa: E402
import os88sym                                                # noqa: E402
if not BIG:                             # NOT $OS88_MEGA, which tells
    os88sym.default_defines("KERN_BIG", "KERN_MEGA")    # os88qemu "Bochs")
import os88geom as geom                                       # noqa: E402
import os88qemu                                               # noqa: E402
import os88ui                                                 # noqa: E402
import xmcheck                                                # noqa: E402
import hdext                                                  # noqa: E402
import re                                                     # noqa: E402

TI = 4                                  # a selector's table bit: the LDT


class MQH(hdext.QH):
    """hdext's QEMU, reading through kern_mega's descriptors as
    tests/pciether.py's MQemu does: a Disk window's listing segment is an
    LDT selector there, and os88qemu.seg_snap resolves it through `lin`"""

    _gdt = _ldt = None

    def _tables(self):
        tries = 0
        while self._gdt is None:
            txt = self.hmp("info registers")
            g = re.search(r"^GDT=\s*([0-9a-f]+)\s+([0-9a-f]+)", txt, re.M)
            l = re.search(r"^LDT=([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)",
                          txt, re.M)
            c = re.search(r"\bCR0=([0-9a-f]+)", txt)
            if g and l and c and int(c.group(1), 16) & 1 \
                    and int(l.group(1), 16) & 0xFFFC:
                self._gdt = (int(g.group(1), 16), int(g.group(2), 16))
                self._ldt = (int(l.group(2), 16), int(l.group(3), 16))
                break
            tries += 1
            if tries > 50:
                raise RuntimeError("kern_mega never showed an LDT")
            os88qemu.pace(self, 0.1)
        return self._gdt, self._ldt

    def sel_base(self, sel):
        (gb, gl), (lb, ll) = self._tables()
        base, limit = (lb, ll) if sel & TI else (gb, gl)
        off = sel & 0xFFF8
        if off + 7 > limit:
            return None
        d = self.read(base + off, 8)
        return d[2] | d[3] << 8 | d[4] << 16 | d[7] << 24

    def lin(self, seg, off=0):
        b = self.sel_base(seg) if seg & TI else None
        return (b if b is not None else (seg & 0xFFFF) << 4) + off

SECTOR = 512
C, H, S = 200, 16, 63                   # ~98MB, under the 504MB CHS bound
BASE = S                                # cylinder 0 head 1, the era's floor
SECS = C * H * S - BASE
FILLER_MB = 40


def chs(lba):
    c, r = divmod(lba, H * S)
    h, s = divmod(r, S)
    return bytes([h, ((c >> 2) & 0xC0) | (s + 1), c & 0xFF])


def filler():
    b = bytearray()
    for i in range(FILLER_MB * 1024 * 1024 // 4096):
        b += struct.pack("<I", i * 2654435761 & 0xFFFFFFFF) * 1024
    return bytes(b)


def stage(tmp):
    """the disk: MBR + one FAT32 partition, files placed by mtools"""
    part = os.path.join(tmp, "part.img")
    with open(part, "wb") as f:
        f.truncate(SECS * SECTOR)

    def mt(*a):
        subprocess.run(a, check=True, capture_output=True)
    mt("mformat", "-i", part, "-F", "-c", "1", "-T", str(SECS), "-h", str(H),
       "-s", str(S), "-H", str(BASE), "-M", "512", "::")
    fill = os.path.join(tmp, "FILLER.BIN")
    open(fill, "wb").write(filler())
    hello = os.path.join(tmp, "HELLO.TXT")
    open(hello, "wb").write(b"FAT32 on kern_mega\r\n")
    calc = os88build.at("build/calc.o88")
    mt("mcopy", "-i", part, fill, "::/")
    mt("mmd", "-i", part, "::/FAR")
    mt("mcopy", "-i", part, calc, "::/FAR/CALC.O88")
    mt("mcopy", "-i", part, hello, "::/FAR/HELLO.TXT")
    mt("mcopy", "-i", part, calc, "::/CALC.O88")
    mt("mcopy", "-i", part, hello, "::/")
    hd = os.path.join(tmp, "hd.img")
    mbr = bytearray(SECTOR)
    mbr[446:462] = bytes([0x80]) + chs(BASE) + bytes([0x0C]) \
        + chs(BASE + SECS - 1) + struct.pack("<II", BASE, SECS)
    mbr[510:512] = b"\x55\xAA"
    with open(hd, "wb") as f:
        f.write(mbr)
        f.write(bytes(SECTOR * (BASE - 1)))
        with open(part, "rb") as p:
            shutil.copyfileobj(p, f)
    os.remove(part)
    return hd


def ents(v, first):
    """{NAME: (attr, first cluster, size)} of one directory, through
    tests/instdeep.py's reader - the suite's host FAT reader, FAT32 included
    since this row (SPEC.md 19.12.11) - folder 0 the root chain"""
    return {n.upper(): (a, c, z) for n, a, c, z in v.entries(first)}


def body(v, ent):
    """a file's bytes off its (attr, cluster, size)"""
    return b"".join(v.sec(v.data_lba + (c - 2) * v.spc, v.spc)
                    for c in v.chain(ent[1]))[:ent[2]]


def boot(tmp, hd):
    sysimg = os.path.join(tmp, "sys.img")
    appsimg = os.path.join(tmp, "apps.img")
    shutil.copy(os88build.at("build/os8088.img" if BIG else "build/mega.img"),
                sysimg)
    shutil.copy(os88build.at("build/apps.img" if BIG
                             else "build/megaapps.img"), appsimg)
    cfg = os.path.join(tmp, "SYSTEM.CFG")
    open(cfg, "wb").write(b"O88CFG\0\0" + (3).to_bytes(2, "little")
                          + b"DW" + bytes([1, 2])
                          + (1 << 1).to_bytes(2, "little") + b"\0\0")
    subprocess.run([sys.executable, "tools/os88fat.py", "add", sysimg,
                    cfg, "SYSTEM.CFG"], check=True, capture_output=True)
    q = (hdext.QH if BIG else MQH)(tmp, sysimg, appsimg, hd, (C, H, S),
                                   None)
    os88qemu.acted(q, lambda: os.path.exists(q.sock), secs=30,
                   what="the QMP socket")
    xmcheck.wait_desktop(q.sock, "fat32")
    return q


def window_new(q, ui, before, what):
    box = {}

    def got():
        for o in ui.windows():
            if o.i not in before and o.visible:
                box["w"] = o
                return True
        return False
    if not os88qemu.acted(q, got, secs=30, what=what):
        raise RuntimeError("%s never opened" % what)
    q.done(1.0)
    return box["w"]


def names(ui, w):
    return [n.upper() for n, _ in ui.listing(w)]


def dbl(q, S, ui, w, name):
    i, _ = ui.entry(name, w)
    os88qemu.mouse_dblclick(q, S, q.hmp, *ui.row_xy(w, i - ui.scroll(w)))


def click(q, S, ui, w, name):
    i, _ = ui.entry(name, w)
    x, y = ui.row_xy(w, i - ui.scroll(w))
    q.hmp_burst(os88qemu.click_lines(q, S, x, y)) \
        if hasattr(os88qemu, "click_lines") else xmcheck.click(q.sock, x, y)
    q.done(1.0)


def keys(q, *ks):
    for k in ks:
        q.hmp("sendkey %s" % k)
        q.done(0.5)


def guest(q, bad):
    S = xmcheck.sym
    ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)
    # 1 + 2: C: is there, and its root lists
    try:
        pt = geom.drive_pt(q, "C", S)
    except Exception as e:                      # noqa: BLE001
        bad.append("1. no C: on the desktop - the mount refused (%s)" % e)
        return
    print("   1. C: is mounted: True")
    before = set(o.i for o in ui.windows())
    os88qemu.mouse_dblclick(q, S, q.hmp, *pt)
    w = window_new(q, ui, before, "C:'s window")
    root = names(ui, w)
    want = {"FILLER.BIN", "FAR", "HELLO.TXT", "CALC.O88"}
    print("   2. C:'s root lists %s: %s" % (sorted(want), want <= set(root)))
    if not want <= set(root):
        bad.append("2. C:'s root is %r" % root)
        return
    # 3: into FAR, and CALC.O88 launched out of it
    dbl(q, S, ui, w, "FAR")
    if not os88qemu.acted(q, lambda: "CALC.O88" in names(ui, w)
                          and "HELLO.TXT" in names(ui, w), secs=30,
                          what="FAR's listing"):
        bad.append("3. FAR did not list: %r" % names(ui, w))
        return
    before = set(o.i for o in ui.windows())
    dbl(q, S, ui, w, "CALC.O88")
    try:
        window_new(q, ui, before, "the Calculator")
        print("   3. CALC.O88 launched out of C:/FAR: True")
    except RuntimeError as e:
        bad.append("3. %s" % e)
    # 4: delete HELLO.TXT, make NEWDIR - in the Disk window, fronted by the
    # click that selects the row
    click(q, S, ui, w, "HELLO.TXT")     # the first click RAISES the
    os88qemu.pace(q, 1.5)               # window over the Calculator, the
    click(q, S, ui, w, "HELLO.TXT")     # second selects - apart, or the
    keys(q, "delete")                   # pair is a double-click (SPEC.md 22)
    os88qemu.pace(q, 1.0)               # ...and the prompt armed before the
    keys(q, "delete")                   # Delete that answers it
    gone = os88qemu.acted(q, lambda: "HELLO.TXT" not in names(ui, w),
                          secs=30, what="HELLO.TXT's delete")
    keys(q, "n")                        # New Folder, its editor armed
    os88qemu.pace(q, 1.0)               # before the name is typed
    keys(q, "n", "e", "w", "d", "i", "r", "ret")
    made = os88qemu.acted(q, lambda: "NEWDIR" in names(ui, w), secs=30,
                          what="NEWDIR")
    print("   4. HELLO.TXT deleted: %s, NEWDIR made: %s" % (gone, made))
    if not (gone and made):             # the glass, for whoever reads why
        png = os.path.join(os.path.dirname(q.sock), "step4.png")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "shot.py"),
                        q.sock, png], capture_output=True)
        print("   4. (the glass: %s)" % png)
    if not gone:
        bad.append("4. HELLO.TXT is still listed in C:/FAR")
    if not made:
        bad.append("4. New Folder made no NEWDIR in C:/FAR: %r"
                   % names(ui, w))
        return
    # 6: a COPY across the far folders - the root's CALC.O88 into
    # FAR/NEWDIR with Ctrl+C / Ctrl+V (SPEC.md 22.3): the copy engine with a
    # destination folder past 65,535, and a new chain allocated there.
    # New Folder's remount re-counts the free space (fm_measure, ~2,200
    # sector reads on this disk) and with the mkdir in DISKOPS.DRV (SPEC.md
    # 18.5.3) NEWDIR is listed BEFORE that count ends, so the UI is still
    # busy: wait for it, or the '..' and the click below land inside the
    # count and the click selects nothing
    q.done(15.0)
    dbl(q, S, ui, w, "..")
    if not os88qemu.acted(q, lambda: "FILLER.BIN" in names(ui, w), secs=30,
                          what="the root again"):
        bad.append("6. '..' did not lead back to the root: %r" % names(ui, w))
        return
    click(q, S, ui, w, "CALC.O88")
    keys(q, "ctrl-c")
    dbl(q, S, ui, w, "FAR")
    os88qemu.acted(q, lambda: "NEWDIR" in names(ui, w), secs=30, what="FAR")
    dbl(q, S, ui, w, "NEWDIR")
    os88qemu.acted(q, lambda: names(ui, w) == [".."], secs=30, what="NEWDIR")
    keys(q, "ctrl-v")
    pasted = os88qemu.acted(q, lambda: "CALC.O88" in names(ui, w), secs=60,
                            what="the paste")
    print("   6. CALC.O88 pasted into C:/FAR/NEWDIR: %s" % pasted)
    if not pasted:
        bad.append("6. Ctrl+V put nothing in C:/FAR/NEWDIR: %r"
                   % names(ui, w))


def host(hd, bad):
    import instdeep                     # (lazily, as hdext does: it brings
    with open(hd, "rb") as f:           # os88marty with it)
        f.seek(BASE * SECTOR)
        v = instdeep.Vol(f.read())
    if v.bits != 32:
        bad.append("5. the host's reader sees FAT%d, not FAT32" % v.bits)
        return
    root = ents(v, 0)
    far = root.get("FAR")
    if not far or far[1] <= 0xFFFF:
        bad.append("5. FAR is %r - not past cluster 65,535, so this tested "
                   "nothing" % (far,))
        return
    fe = ents(v, far[1])
    if "HELLO.TXT" in fe:
        bad.append("5. the host still sees FAR/HELLO.TXT")
    nd = fe.get("NEWDIR")
    if not nd or not nd[0] & 0x10:
        bad.append("5. the host sees no folder FAR/NEWDIR: %r" % sorted(fe))
    else:
        if nd[1] <= 0xFFFF:
            bad.append("5. NEWDIR's cluster %d is not past 65,535" % nd[1])
        ne = ents(v, nd[1])
        if ne.get(".", (0, -1))[1] != nd[1]:
            bad.append("5. NEWDIR's . is %r, want %d" % (ne.get("."), nd[1]))
        if ne.get("..", (0, -1))[1] != far[1]:
            bad.append("5. NEWDIR's .. is %r, want FAR's %d"
                       % (ne.get(".."), far[1]))
        cp = ne.get("CALC.O88")
        want = open(os88build.at("build/calc.o88"), "rb").read()
        if not cp:
            bad.append("6. the host sees no FAR/NEWDIR/CALC.O88: %r"
                       % sorted(ne))
        elif cp[1] <= 0xFFFF or body(v, cp) != want:
            bad.append("6. FAR/NEWDIR/CALC.O88 is cluster %d, %d bytes - "
                       "not a chain past 65,535 holding the package"
                       % (cp[1], cp[2]))
        # ...and mtools, a reader this row did not write (19.12.11)
        img = "%s@@%d" % (hd, BASE * SECTOR)
        r = subprocess.run(["mtype", "-i", img, "::/FAR/NEWDIR/CALC.O88"],
                           capture_output=True)
        if r.returncode or r.stdout != want:
            bad.append("6. mtype reads FAR/NEWDIR/CALC.O88 as %d bytes "
                       "(exit %d), not the package" % (len(r.stdout),
                                                       r.returncode))
        r = subprocess.run(["mdir", "-b", "-i", img, "::/FAR"],
                           capture_output=True, text=True)
        got = {os.path.basename(x.rstrip("/")).upper()
               for x in r.stdout.split()}
        if r.returncode or "NEWDIR" not in got or "HELLO.TXT" in got:
            bad.append("5. mdir sees FAR as %r (exit %d)"
                       % (sorted(got), r.returncode))
    fl = root.get("FILLER.BIN")
    if not fl or body(v, fl) != filler():
        bad.append("5. FILLER.BIN is not its bytes")
    fsi = v.sec(v.fsinfo)
    if fsi[488:492] != b"\xff\xff\xff\xff":
        bad.append("5. FSInfo's free count is %s, not 0FFFFFFFFh (19.12.7)"
                   % fsi[488:492].hex())
    print("   5. the host reads the volume back: %s"
          % (not any(b.startswith("5.") for b in bad)))
    fsck = shutil.which("fsck.fat") or shutil.which("fsck.vfat")
    if fsck:
        part = hd + ".part"
        with open(hd, "rb") as f, open(part, "wb") as o:
            f.seek(BASE * SECTOR)
            shutil.copyfileobj(f, o)
        r = subprocess.run([fsck, "-n", part], capture_output=True, text=True)
        os.remove(part)
        print("   5. fsck.fat -n: exit %d" % r.returncode)
        if r.returncode:
            bad.append("5. fsck.fat -n: %s" % (r.stdout + r.stderr)[-400:])
    else:
        print("   5. (no fsck.fat on this host - dosfstools)")


def main():
    if not shutil.which("mformat"):
        print("fat32: SKIP: no mtools")
        return 0
    need = ["build/calc.o88"] + (["build/os8088.img", "build/apps.img"] if BIG
                                 else ["build/mega.img", "build/megaapps.img",
                                       "build/megak/kernel.map"])
    for p in need:
        if not os.path.exists(os88build.at(p)):
            sys.exit("fat32: no %s - `make%s` first" % (p, "" if BIG
                                                         else " mega"))
    bad = []
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    q = None
    try:
        hd = stage(tmp)
        q = boot(tmp, hd)
        if BIG:
            try:
                geom.drive_pt(q, "C", xmcheck.sym)
                bad.append("kern_big mounted a FAT32 partition as C: - its "
                           "BPB check is the fence 19.12.10 relies on")
            except Exception:                   # noqa: BLE001
                print("   kern_big: the FAT32 partition is refused: True")
        else:
            guest(q, bad)
            q.close()
            q = None
            host(hd, bad)
    finally:
        if q is not None:
            q.close()
        if "--keep" in sys.argv:
            print("fat32: kept %s" % tmp)
        else:
            shutil.rmtree(tmp, ignore_errors=True)
    for b in bad:
        print("   FAIL: %s" % b)
    print("fat32: %s" % ("FAIL" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
