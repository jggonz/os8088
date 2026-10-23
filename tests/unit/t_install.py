#!/usr/bin/env python3
"""INSTALL is where SPEC.md 117 says it is, and its two doors agree.

    python3 tests/unit/t_install.py

SPEC.md 117 is a wizard with no install engine of its own: it reaches the
Control Panel's through OSAPI_DRV_USE (117.3) and HDD.DRV's HDV_TOOL (117.4).
Everything that makes that work is a NUMBER written in two places, and every
one of them fails silently when the two drift:

  * THE ROOT OF EVERY kern_big SYSTEM DISK, and of no kern_small one (117.6).
    A Makefile edit that drops $(SYSINST) from one geometry's recipe builds a
    disk that boots perfectly and has no installer on it.
  * HD_ABI_VER, which the resident checks at +10 of the tool image it reads.
    A tool and a resident of different vintages refuse each other; the same
    build must carry the same number in both.
  * THE STAGE AND STATE NUMBERS. HDG_* is HII_*'s numbering and HDS_* is
    HIS_*'s (117.4): the package compares the tool's answer against the SDK's
    constants, and the tool answers with inst.inc's. ipkg.inc refuses a drift
    at assembly; this reads both files so a drift also has a NAME.
  * DRVU_*, which the kernel defines for itself (it does not include the SDK)
    and the SDK defines for packages.
  * HDD.DRV's 4KB rung. The door's forwarder was paid for inside the driver
    (117.4); a fifth kilobyte would be a cost on every machine with a disk.
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, eq, done                        # noqa: E402
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88drv                                            # noqa: E402

BUILD = os.path.join(ROOT, "build")
BIG = ["os8088.img", "os8088-720.img", "os8088-120.img", "os8088-360.img"]


def equs(path, prefix):
    """{name: value} for every `NAME equ N` starting with prefix."""
    out = {}
    for line in open(os.path.join(ROOT, path)):
        m = re.match(r"\s*(%s\w*)\s+equ\s+(0x[0-9A-Fa-f]+|\d+)\b" % prefix,
                     line)
        if m:
            out[m.group(1)] = int(m.group(2), 0)
    return out


def root_names(img):
    """The 8.3 names in a FAT12 floppy's root directory."""
    b = open(img, "rb").read()
    bps, spc, rsvd, nfats, rootn = struct.unpack_from("<HBHBH", b, 11)
    fatsz = struct.unpack_from("<H", b, 22)[0]
    off = (rsvd + nfats * fatsz) * bps
    names = set()
    for i in range(rootn):
        e = b[off + i * 32:off + i * 32 + 32]
        if e[0] == 0:
            break
        if e[0] == 0xE5 or e[11] & 0x08:
            continue
        base = e[0:8].decode("ascii", "replace").rstrip()
        ext = e[8:11].decode("ascii", "replace").rstrip()
        names.add(base + ("." + ext if ext else ""))
    return names


# --- the roots ----------------------------------------------------------------
for name in BIG:
    img = os.path.join(BUILD, name)
    if not os.path.exists(img):
        check(False, "%s is built" % name, why="run `make` first")
        continue
    check("INSTALL.O88" in root_names(img),
          "INSTALL.O88 is in the root of %s" % name,
          why="SPEC.md 117.6: every kern_big system disk carries the wizard "
              "in its root - $(SYSINST) has dropped out of this geometry's "
              "recipe")
SMALL = sorted(n for n in os.listdir(BUILD)
               if n.startswith("small") and n.endswith(".img"))
# A plain `make` does NOT build build/small*.img (`make small` and test-full's
# $(BUILD)/small360.img do), so a fresh tree has none - and this row is in the
# fast tier, which `all` runs. Refusing there failed every first `make` of a
# clean checkout; the half is skipped with a note instead, t_livefull's PART B
# shape, and it runs wherever a kern_small disk has been built.
if not SMALL:
    print("t_install: no build/small*.img - the kern_small half is skipped "
          "(`make small` builds them)")
for name in SMALL:
    if True:
        check("INSTALL.O88" not in root_names(os.path.join(BUILD, name)),
              "INSTALL.O88 is NOT on kern_small's %s" % name,
              why="SPEC.md 117.6, 24.5: kern_small has no driver layer, so "
                  "there is no HDD.DRV for the wizard to switch on")

# --- HD_ABI_VER: the source, and the tool image's header ----------------------
abi = equs("drivers/hdd/hddabi.inc", "HD_ABI_VER")["HD_ABI_VER"]
tool = os.path.join(BUILD, "hddtool.bin")
if os.path.exists(tool):
    eq(open(tool, "rb").read()[10], abi,
       "HDDTOOL.DRV's header carries HD_ABI_VER",
       why="hd_tool_need refuses a tool whose +10 is not its own HD_ABI_VER: "
           "the wizard's every verb would answer CF = 1")

# --- the numbers two files share ----------------------------------------------
api = equs("apps/os88api.inc", "HD")
inst = equs("drivers/hdd/inst.inc", "HI")
for a, b in (("HDG_FAIL", "HII_IDLE"), ("HDG_APPS", "HII_SYSDONE"),
             ("HDG_DONE", "HII_DONE"), ("HDS_OK", "HIS_OK"),
             ("HDS_SMALL", "HIS_SMALL"), ("HDS_ALIEN", "HIS_ALIEN"),
             ("HDS_NONE", "HIS_NONE"), ("HDS_LIVE", "HIS_LIVE")):
    eq(api.get(a), inst.get(b), "%s (SDK) == %s (inst.inc)" % (a, b),
       why="SPEC.md 117.4: the tool answers in the Install window's own "
           "numbering and the package reads it with the SDK's")
eq(api["HDIS_TEXT"] + api["HDIS_TEXTSZ"], api["HDIS_SIZE"],
   "HDIS_TEXT ends the slot record",
   why="hd_pv_slots copies HDIS_TEXTSZ bytes at HDIS_TEXT into each record")
kdrvu = equs("kernel/driver.inc", "DRVU_")
adrvu = equs("apps/os88api.inc", "DRVU_")
eq(kdrvu, adrvu, "DRVU_* agree between the kernel and the SDK",
   why="the kernel defines OSAPI_DRV_USE's verbs for itself; a package that "
       "sends DRVU_CALL by the SDK's number must reach the CALL arm")

# --- HDD.DRV stays on its 4KB rung --------------------------------------------
drv = os.path.join(BUILD, "hdd.drv")
if os.path.exists(drv):
    image = os88drv.image_unwrap(open(drv, "rb").read())
    check(len(image) <= 4096, "HDD.DRV's image is within 4,096 bytes "
          "(%d)" % len(image),
          why="SPEC.md 117.4: HDV_TOOL was paid for inside the driver; a "
              "fifth kilobyte is claimed on every machine with a hard disk")

done("t_install")
