"""imgswap - an image on a FLOPPY is swapped with that floppy (SPEC.md 18.101.7).

A .IMG mounted off B: is a drive whose sectors are B:'s, so the floppy in B:
can be taken out from under it. As first built the image row was a DVK_DRV
row and so a FIXED one: its BPB was banked for good, no mount of it read its
boot sector again, and its writes went to the image's old host LBAs on
whatever disk was now in the drive, every call answering CF = 0. This row is
that sequence, end to end, with the verdict read off the DISK:

  1. TEST.IMG mounts off B: (tests/imgfix.py's 1.44MB set) and D: lists it.
  2. The image's BPB bank is not PERMANENT (dsk_bpbok != 1): a floppy-hosted
     image banks for a batch only, like the floppy itself.
  3. B:'s motor stops, B: is swapped (MartyPC's `mount`) for ANOTHER disk -
     one big file whose bytes cover every host LBA TEST.IMG occupied, so the
     image's boot sector there is not a boot sector - and New Folder is
     asked of D:.
  4. The other disk, flushed back to the host, is UNCHANGED to the byte.

    python3 tests/imgswap.py [--machine NAME]
"""
import os
import re
import shutil
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
OUT = os88build.at("build/imgswap") if hasattr(os88build, "at") \
    else "build/imgswap"
DRVC_IMG = int(re.search(r"^DRVC_IMG\s+equ\s+(\w+)",
                          open("kernel/disk.inc").read(), re.M).group(1), 0)
DV_SIZE = 16
fails = []


def say(s):
    print("  " + s)


def check(ok, what, got=None):
    say("%-4s %s%s" % ("ok" if ok else "FAIL", what,
                      "" if got is None else "  [%r]" % (got,)))
    if not ok:
        fails.append(what)


def images(ui):
    """[volume] of every DRVC_IMG row."""
    base = ui._S("dsk_vtab")
    out = []
    for v in range(8):
        r = bytes(ui.m.read(base + DV_SIZE * v, DV_SIZE))
        if r[0] == 1 and r[3] == DRVC_IMG:
            out.append(v)
    return out


def motor_off(ui):
    """a swap is noticed once the motor has stopped (SPEC.md 18.9.1)"""
    os88marty.until(ui.m, lambda mm: mm.read(0x43F, 1)[0] & 0x0F == 0,
                    "the floppy motors to stop", poll=0.2, guest=10.0)


os.makedirs(OUT, exist_ok=True)
host, runs = imgfix.build_set(os.path.join(OUT, "fix"))
img_lbas = sorted(l for _f, lba, n in runs[b"TEST    IMG"]
                  for l in range(lba, lba + n))
first_lba = [lba for f, lba, _n in runs[b"TEST    IMG"] if f == 0][0]

# THE OTHER DISK: one file as big as a 1.44MB volume holds, in a pattern a
# sector written anywhere in it cannot reproduce by accident
fill = os.path.join(OUT, "OTHER.BIN")
with open(fill, "wb") as f:
    f.write(bytes((i * 37 + (i >> 9)) & 0xFF for i in range(1_400_000)))
other = os.path.join(OUT, "other.img")
subprocess.run([sys.executable, "tools/os88disk.py", "--size", "1440", "-o",
                other, fill], check=True, stdout=subprocess.DEVNULL)
with open(other, "rb") as f:
    other_orig = f.read()
o_fat = imgfix.Fat12(other_orig)
check(img_lbas[-1] < o_fat.datalba + 1_400_000 // 512
      and img_lbas[0] >= o_fat.datalba,
      "precondition: OTHER.BIN covers every host LBA of TEST.IMG",
      (img_lbas[0], img_lbas[-1], o_fat.datalba))
sec0 = other_orig[first_lba * 512:first_lba * 512 + 512]
check(sec0[510:512] != b"\x55\xAA", "precondition: the image's boot sector's "
      "host LBA holds no boot sector on the other disk", first_lba)
other_run = os.path.join(OUT, "other-run.img")
shutil.copyfile(other, other_run)       # MartyPC uses `mount`'s file in place

with os88ui.boot(SYS_IMG, apps=host, machine=MACHINE) as ui:
    # --- 1. mount ---------------------------------------------------------
    w = ui.open_drive("B")
    ui.open("TEST.IMG", expect=None, win=w)
    os88marty.until(ui.m, lambda _: len(images(ui)) == 1, "TEST.IMG's row",
                    guest=60)
    v = images(ui)[0]
    letter = chr(ord("A") + v)
    txt = ui.wait_toast(says="is drive")
    check(txt == "TEST.IMG is drive %s:" % letter, "TEST.IMG mounts off B:",
          txt)
    ui.open_drive(letter)
    names = [r[0] for r in ui.listing()]
    check(names == ["HELLO.TXT"], "%s: lists the file inside the image"
          % letter, names)

    # --- 2. the bank is a floppy's -----------------------------------------
    ok = ui.m.read(ui._S("dsk_bpbok") + v, 1)[0]
    check(ok != 1, "the image's BPB is NOT banked for good on a floppy host "
          "(dsk_bpbok: 0 none, 1 permanent, 2/3 this batch only)", ok)

    # --- 3. swap B: and write to the image's drive -------------------------
    motor_off(ui)
    ui.m.mount(1, other_run)
    motor_off(ui)
    ui.m.key("KeyN")                    # New Folder, in D:'s window
    os88marty.settle(ui.m)
    for ch in "SWAP":
        ui.m.key("Key" + ch)
    ui.m.key("Enter")
    os88marty.settle(ui.m)
    say("toast after the write: %r" % (ui.toast()[0],))

    # --- 4. the other disk, read back on the host ---------------------------
    motor_off(ui)
    path = os.path.abspath(os.path.join(OUT, "flush-other.img"))
    ui.m.flush(1, path)
    with open(path, "rb") as f:
        now = f.read()
    moved = sorted({i // 512 for i in range(min(len(now), len(other_orig)))
                    if now[i] != other_orig[i]})
    check(len(now) == len(other_orig) and not moved, "the disk swapped into "
          "B: is unchanged to the byte - D:'s write went nowhere",
          moved[:16])

for f in fails:
    print("FAIL " + f)
print("imgswap: %s (%d failed)" % ("FAIL" if fails else "ok", len(fails)))
sys.exit(1 if fails else 0)
