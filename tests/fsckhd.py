#!/usr/bin/env python3
"""Defragmenting an INSTALLED os8088 on a hard disk (SPEC.md 118.14.5) -
python3 tests/fsckhd.py

The case the defragmenter is for: a hard-disk install whose own files are in
pieces. A FAT16 VHD is built the way an install leaves one (tools/os88hdd.py:
the MBR, BOOTHD's VBR, KERNEL.SYS), with SYSCHECK.O88, FSCHECK.DRV and
HDD.DRV beside it at the root - util360's layout - and then, on the host:

  * KERNEL.SYS is MOVED off cluster 2 to a run high on the disk and the VBR's
    BOOTHD_KOFS patched to match, so the machine still boots and the kernel
    has somewhere lower to go;
  * HDD.DRV (hidden + system), SYSCHECK.O88 and FSCHECK.DRV are cut into
    pieces, interleaved with each other;
  * a user file in a folder is cut into pieces too;
  * \\AUTOSTRT\\FSCK.LNK opens SYSCHECK.O88 with `C: DEFRAG REPORT` (SPEC.md
    118.14.9), so the defragmenter runs at boot with nobody at the machine.

BOOT 1, MartyPC's 4.77MHz XT on XT-IDE: the run ends with the toast that
names SYSCHECK.TXT. The VHD is then held to an INDEPENDENT FAT reader: nothing
wrong, no movable file in pieces - the system's own files included - every
file's bytes as they were, KERNEL.SYS lower than it was and the VBR's
BOOTHD_KOFS naming exactly where it now starts.

BOOT 2: every FREE cluster is zeroed on the host first, so a VBR still
pointing at the kernel's old run would load zeros - and the machine must
reach the desktop.

    python3 tests/fsckhd.py           the row
    python3 tests/fsckhd.py --keep D  leave the VHD in D
"""
import argparse
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import fsckdisk as F                                        # noqa: E402

MACHINE = "os8088_xt_hdd"
FOOTER = 512
LINKARG = "C: DEFRAG REPORT"     # the link the defragmenter runs from
KOFS, KSECS = 504, 508                  # boot/boothd.asm's two patch words


def b(name):
    """build/NAME - or, under $OS88_MEGA (tests/megaport.txt), kern_mega's
    own copy for what a kernel builds for itself: C: boots with A: empty, so
    a kern_big KERNEL.SYS here would be the kernel Bochs ran, and kern_mega
    has its own FSCHECK.DRV (SPEC.md 118.14.8)"""
    if os.environ.get("OS88_MEGA") and name in ("kernel.sys", "boothd.bin",
                                                "fscheck.drv"):
        import os88build
        return os.path.join(os88build.mega_build(), "megak", name)
    return F.b(name)


class Disk(object):
    """A VHD and its active partition as an fsckdisk.Vol, written back whole."""

    def __init__(self, path):
        self.path = path
        self.blob = bytearray(open(path, "rb").read())
        mbr = self.blob[:512]
        for i in range(4):
            e = mbr[446 + 16 * i:446 + 16 * (i + 1)]
            if e[0] == 0x80:
                self.lba, self.secs = struct.unpack_from("<II", e, 8)
                break
        else:
            raise SystemExit("fsckhd: the VHD has no active partition")
        a = self.lba * 512
        self.v = F.Vol(self.blob[a:a + self.secs * 512])

    def save(self):
        a = self.lba * 512
        self.blob[a:a + len(self.v.img)] = self.v.img
        open(self.path, "wb").write(bytes(self.blob))


def kofs_of(v, first):
    return (first - 2) * v.spc


def fixture(work):
    vhd = os.path.join(work, "c.vhd")
    tpl = os.path.join(ROOT, "build", "martypc", "run", "media", "hdds",
                       "default_xtide.vhd")
    filler = os.path.join(work, "FILLER.BIN")
    open(filler, "wb").write(F.blob(40000, 7))
    subprocess.check_call(
        ["python3", "tools/os88hdd.py", "--template", tpl, "--out", vhd,
         "--kernel", b("kernel.sys"), "--vbr", b("boothd.bin"),
         "--mbr", b("mbr.bin"),
         "--file", "HDD.DRV=" + b("hdd.drv"),
         "--file", "SYSCHECK.O88=" + b("syscheck.o88"),
         "--file", "FSCHECK.DRV=" + b("fscheck.drv"),
         "--file", "FILLER.BIN=" + filler], cwd=ROOT,
        stdout=subprocess.DEVNULL)
    d = Disk(vhd)
    v = d.v
    if not v.fat16:
        raise SystemExit("fsckhd: the volume is not FAT16 - this row is about "
                         "the FAT16 arms")
    # --- KERNEL.SYS: high on the disk, the VBR told -------------------------
    o, e = v.find("\\KERNEL.SYS")
    first = struct.unpack_from("<H", e, 26)[0]
    vbr = v.img[:512]
    assert struct.unpack_from("<I", vbr, KOFS)[0] == kofs_of(v, first), \
        "os88hdd's VBR does not name KERNEL.SYS where it is"
    n = len(v.chain(first))
    free = v.free()
    run = free[len(free) // 2:len(free) // 2 + n]      # contiguous: free is
    assert run[-1] - run[0] == n - 1                    # one run up there
    v.relocate("\\KERNEL.SYS", run)
    struct.pack_into("<I", v.img, KOFS, kofs_of(v, run[0]))
    # --- the system's own files and a user's, in pieces ---------------------
    names = ["\\HDD.DRV", "\\SYSCHECK.O88", "\\FSCHECK.DRV"]
    sizes = [len(v.chain(struct.unpack_from("<H", v.find(p)[1], 26)[0]))
             for p in names]
    pool = v.free()
    pool = pool[:3 * max(sizes) + 8]                    # low, interleaved
    datas = [v.read(p) for p in names]
    for k, p in enumerate(names):
        o, e = v.find(p)
        for c in v.chain(struct.unpack_from("<H", e, 26)[0]):
            v.put(c, 0)
    for k, p in enumerate(names):
        use = pool[k::3][:sizes[k]]
        v.write_chain(use, datas[k])
        struct.pack_into("<H", v.img, v.find(p)[0] + 26, use[0])
    hdd_o, hdd_e = v.find("\\HDD.DRV")
    v.img[hdd_o + 11] |= 0x06                           # hidden + system
    docs = v.mkdir("DOCS")
    data = F.blob(9 * v.cb + 300, 21)
    use = v.free()[1:20:2][:10]                         # every other one
    v.add("NOTES.TXT", data, clusters=use, parent=docs)
    F.folder_in_pieces(v)                               # \\FOLD, in two
    # --- the link that runs it at boot ----------------------------------------
    import os88lnk
    auto = v.mkdir("AUTOSTRT")
    v.add("FSCK.LNK", os88lnk.make("SYSCHECK.O88", arg=LINKARG),
          parent=auto)
    d.save()
    r = F.check(v)
    for k in ("lost", "xlink", "long", "short", "broken", "dot"):
        assert not r[k], (k, r[k])
    frag = set(r["frag"])
    assert {"\\HDD.DRV", "\\SYSCHECK.O88", "\\FSCHECK.DRV",
            "\\DOCS\\NOTES.TXT"} <= frag, frag
    assert r["dfrag"] == ["\\FOLD"], r["dfrag"]
    return vhd, run[0]


def sums(v):
    return {p: hashlib.sha1(v.read(p)).hexdigest()
            for p, _, e, _ in v.walk() if not e[11] & 0x10}


def boot(vhd, wait, fails):
    import os88marty as M
    import os88ui
    m = M.launch(None, machine=MACHINE, extra=["--mount", "hd:0:" + vhd],
                 boot=False)
    try:
        m.run()
        ui = os88ui.UI(m)
        ui.up(limit=300)
        if wait:
            M.no_saver(m)
            try:
                said = ui.wait_toast(says="SYSCHECK.TXT", limit=1200)
                if "Saved" not in said:
                    fails.append("the report was not saved: %r" % said)
            except Exception as ex:             # noqa: BLE001
                fails.append("the defragmenter never saved its report: %s"
                             % ex)
        return True
    except Exception as ex:                     # noqa: BLE001
        fails.append("boot: %s" % ex)
        return False
    finally:
        m.quit()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", metavar="DIR")
    a = ap.parse_args()
    work = tempfile.mkdtemp(prefix="fsckhd-")
    fails = []
    try:
        vhd, kfirst = fixture(work)
        before = sums(Disk(vhd).v)
        # --- boot 1: the defragmenter runs from the link -------------------------
        boot(vhd, True, fails)
        d = Disk(vhd)
        v = d.v
        try:
            text = v.read("\\SYSCHECK.TXT").decode("latin1")
            print(text)
        except Exception:                       # noqa: BLE001
            text = ""
            fails.append("no SYSCHECK.TXT on C:")
        if "Task: Defragment the disk" not in text:
            fails.append("the report is not the defragmenter's")
        if "0 left in pieces" not in text:
            fails.append("the report says files were left in pieces")
        if "folders moved" not in text:
            fails.append("the report moved no folder")
        r = F.check(v)
        for k in ("lost", "xlink", "long", "short", "broken", "dot",
                  "dfrag"):
            if r[k]:
                fails.append("after defragmenting the reader finds %s: %r"
                             % (k, r[k]))
        if r["fatdiff"]:
            fails.append("after defragmenting the FAT copies differ")
        left = [p for p in r["frag"] if not p.endswith(".IMG") and
                not p.endswith("SYSCHECK.TXT")]     # written AFTER the run
        if left:
            fails.append("still in pieces: %r" % left)
        after = sums(v)
        for p, s in before.items():
            if after.get(p) != s:
                fails.append("%s's bytes changed in the move" % p)
        o, e = v.find("\\KERNEL.SYS")
        kf = struct.unpack_from("<H", e, 26)[0]
        if kf >= kfirst:
            fails.append("KERNEL.SYS did not move down (cluster %d, was %d)"
                         % (kf, kfirst))
        if struct.unpack_from("<I", v.img, KOFS)[0] != kofs_of(v, kf):
            fails.append("the VBR's BOOTHD_KOFS does not name KERNEL.SYS's "
                         "new run")
        # --- boot 2: free space zeroed, the kernel must load from its new run -
        for c in v.free():
            v.img[v.coff(c):v.coff(c) + v.cb] = bytes(v.cb)
        o, e = v.find("\\AUTOSTRT\\FSCK.LNK")
        if o is not None:
            v.img[o] = 0xE5                     # boot 2 only boots
            for c in v.chain(struct.unpack_from("<H", e, 26)[0]):
                v.put(c, 0)
        d.save()
        if not boot(vhd, False, fails):
            fails.append("the disk does not boot after its kernel moved")
        if a.keep:
            os.makedirs(a.keep, exist_ok=True)
            shutil.copy(vhd, a.keep)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if fails:
        for f in fails:
            print("FAIL: " + f)
        return 1
    print("fsckhd: an installed system in pieces defragmented on FAT16 - its "
          "kernel moved, the VBR told, and the disk boots")
    return 0


if __name__ == "__main__":
    sys.exit(main())
