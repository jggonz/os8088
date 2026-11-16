#!/usr/bin/env python3
"""Every driver REFUSES a verb it does not implement (SPEC.md 51.2.2).

    python3 tests/unit/t_drvverb.py

THE RULE. A driver's entry proc answers a `DRVV_*` it does not implement with
CF = 1 and does no work. CF = 0 means "I did it", and the kernel acts on it:
`DRVV_TIER`'s CF = 0 re-publishes SI as the service table, `DRVV_HWINFO`'s
CF = 0 writes AX/BX/CX into `OSAPI_DRV_SUSPEND`'s record as the hardware's
address and counts it. ETHER.DRV, PCIETHER.DRV, NET.DRV, RAMDISK.DRV and
USBMOUSE.DRV answered every unknown verb CF = 0 ("a verb we do not implement
is not an error") until this row, so DOS's suspend through them was handed a
DRVC_NET record with three words of whatever the registers held - and the
next opt-in verb (a wake, say) would have read "yes" off every one of them.
CD-ROM, EMS and HTTP answered TIER CF = 0 by a `cmp al, DRVV_HWINFO / cmc`
that split the verb space at 4 rather than naming the verbs.

The two verbs EVERY driver gets are not this rule's: DRVV_DETACH cannot fail,
and DRVV_READY is optional in the other direction - nothing to do is CF = 0,
because a READY refusal is banked as the row's DRVE_* (SPEC.md 51.2.2).

HOW IT READS IT. Off the BINARY `make` just built, not the source: each
kern_big driver image (class below 0x40 - an overlay's AL is its owner's verb
space, not DRVV_*), entered at its header's +6 with AH = 0 and AL = each verb
the driver's class does not implement, and stepped over the dispatch subset -
`cmp al/ax, imm`, the conditional jumps, `jmp`, `stc`/`clc`/`cmc`,
`test al, imm` (CLC_OR_STC's A8), `mov al, imm`, `ret`. A verb must reach
`ret` with CF = 1 inside that subset. Reaching any other instruction is
WORK on a verb nobody implements - the shape snd_entry had when READY ran a
second whole attach (SPEC.md 51.2.2) - and fails as that.

AND THE OPT-IN PAIR (SPEC.md 51.2.5). A driver whose header +10 has bit 15
set (OS88_DRV_WAKES) is put to sleep and kept by a hibernate rather than
unloaded, so it must NOT refuse DRVV_SLEEP (5) or DRVV_WAKE (6) at its
dispatch - a flagged driver that answers both CF = 1 there is a declaration
nothing backs, which costs the user nothing but the feature. Every driver
without the bit still refuses both, by the rule above. WAKES is the list
that declares it, so a lost %define fails here rather than in a soak row.

WHAT IT DOES NOT SEE: a driver that does not dispatch by compares (a table
indexed by AL would stop the walk at its first instruction and fail loudly,
which is the right direction), and the verbs a class DOES implement.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88drv                                            # noqa: E402

BUILD = os.path.join(ROOT, "build")

DRVV_TIER, DRVV_HWINFO = 2, 4
DRVV_SLEEP, DRVV_WAKE = 5, 6
WAKEBIT = 0x8000                # header +10, OS88_DRV_WAKES (SPEC.md 51.2.5)
DRVC_SOUND = 1
DRVC_OVL = 0x40

# The kern_big drv_tab drivers. Listed rather than globbed: build/ also holds
# overlays, tool halves and whatever a gate target left there
DRIVERS = ["sound.drv", "hdd.drv", "ether.drv", "pciether.drv", "net.drv",
           "ramdisk.drv", "usbmouse.drv", "ems.drv", "cdrom.drv",
           "httpfs.drv"]

# the drivers that declare OS88_DRV_WAKES (SPEC.md 51.2.5, B4). SOUND does
# not, by decision (docs/plans/B4-WAKE-PLAN.md)
WAKES = {"ether.drv", "pciether.drv", "usbmouse.drv", "httpfs.drv"}

# what each class implements beyond ATTACH/DETACH/READY, which every driver
# gets and this row does not ask about
IMPLEMENTS = {DRVC_SOUND: {DRVV_TIER, DRVV_HWINFO}}

# every verb up to here is asked; the kernel's are 0..4 today, and the rest
# stand for the verbs a later kernel adds
VERB_TOP = 0x3F


def walk(img, entry, ax):
    """Step the dispatch. Returns ('ret', cf) or ('work', offset)."""
    ip, cf, zf = entry, 0, 0
    for _ in range(200):
        op = img[ip]
        if op == 0x3C:                          # cmp al, imm8
            a, b = ax & 0xFF, img[ip + 1]
            cf, zf, ip = int(a < b), int(a == b), ip + 2
        elif op == 0x3D:                        # cmp ax, imm16
            b = struct.unpack_from("<H", img, ip + 1)[0]
            cf, zf, ip = int(ax < b), int(ax == b), ip + 3
        elif op == 0xA8:                        # test al, imm8
            cf, zf, ip = 0, int((ax & img[ip + 1] & 0xFF) == 0), ip + 2
        elif op == 0xB0:                        # mov al, imm8
            ax, ip = (ax & 0xFF00) | img[ip + 1], ip + 2
        elif op == 0xF8:
            cf, ip = 0, ip + 1
        elif op == 0xF9:
            cf, ip = 1, ip + 1
        elif op == 0xF5:
            cf, ip = cf ^ 1, ip + 1
        elif op == 0xC3:
            return ("ret", cf)
        elif op == 0xEB:
            ip = (ip + 2 + struct.unpack_from("<b", img, ip + 1)[0]) & 0xFFFF
        elif op == 0xE9:
            ip = (ip + 3 + struct.unpack_from("<h", img, ip + 1)[0]) & 0xFFFF
        elif op in (0x72, 0x73, 0x74, 0x75, 0x76, 0x77):
            take = {0x72: cf, 0x73: not cf, 0x74: zf, 0x75: not zf,
                    0x76: cf or zf, 0x77: not (cf or zf)}[op]
            nxt = ip + 2
            ip = (nxt + struct.unpack_from("<b", img, ip + 1)[0]) if take else nxt
        else:
            return ("work", ip)
    return ("work", ip)


def main():
    seen = 0
    for name in DRIVERS:
        path = os.path.join(BUILD, name)
        if not check(os.path.isfile(path), "%s is built" % name,
                     "run make first: this row reads the images it made"):
            continue
        with open(path, "rb") as f:
            img = os88drv.image_unwrap(f.read())
        cls, entry = img[3], struct.unpack_from("<H", img, 6)[0]
        if not check(cls < DRVC_OVL, "%s is a drv_tab driver" % name,
                     "an overlay's AL is its owner's verb space"):
            continue
        seen += 1
        has = set(IMPLEMENTS.get(cls, set()))
        wakes = bool(struct.unpack_from("<H", img, 10)[0] & WAKEBIT)
        check(wakes == (name in WAKES),
              "%s %s OS88_DRV_WAKES" % (name, "declares" if wakes else "does not declare"),
              "the WAKES list here and the driver's %define disagree - a lost "
              "declaration is a driver unloaded and re-read at every wake "
              "(SPEC.md 51.2.5)")
        if wakes:
            has |= {DRVV_SLEEP, DRVV_WAKE}
            for verb in (DRVV_SLEEP, DRVV_WAKE):
                check(walk(img, entry, verb) != ("ret", 1),
                      "%s: declared, so verb %d is not refused at dispatch"
                      % (name, verb),
                      "OS88_DRV_WAKES says SLEEP and WAKE are answered "
                      "(SPEC.md 51.2.5)")
        for verb in [DRVV_TIER, DRVV_HWINFO] + list(range(5, VERB_TOP + 1)):
            if verb in has:
                continue
            what, val = walk(img, entry, verb)
            if what == "work":
                check(False, "%s: verb %d reaches work at +%04X" % (name, verb, val),
                      "an unknown verb must refuse and do nothing - snd_entry's "
                      "fallthrough ran a second whole attach on READY (51.2.2)")
                break
            if not check(val == 1, "%s: verb %d answers CF = 1" % (name, verb),
                         "CF = 0 is 'I did it': TIER's re-publishes SI and "
                         "HWINFO's is written into OSAPI_DRV_SUSPEND's "
                         "record as the hardware's address (SPEC.md 51.2.2)"):
                break
    check(seen == len(DRIVERS), "every listed driver was walked (%d of %d)"
          % (seen, len(DRIVERS)))
    done("drvverb")


if __name__ == "__main__":
    main()
