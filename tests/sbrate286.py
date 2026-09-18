#!/usr/bin/env python3
"""SPEC.md 13.10.5.4.1: the thumb's rate is a PAIR and the MACHINE picks.

    python3 tests/sbrate286.py [machine]

A registered soak row, and the whole of it is ONE A/B on ONE boot of ONE
build: the same drag, twice, with a single byte of guest memory different
between the arms.

That byte is `cpu_tier` (SPEC.md 41.1), and poking it is what makes this
testable at all. docs/TESTING.md's closed list entry 1 says a 286 is QEMU's,
and QEMU cannot be asked what a drag LOOKS like; MartyPC can, and is an 8088
for ever. So the arm is selected the way `os88ui_sbrate` selects it - off that
byte - and nothing else in the drag path reads it. What this row therefore
proves is the GATE, exactly: that the pair is resolved on the tier and that
each side of it does what 13.10.5.4.1 says. What it cannot prove is that 3 is
the right number for a real 286, which that section states is arithmetic and
not a reading.

  A  tier 0: `os88ui_sbd_rate` is 0 after the grab, the thumb moves, and
     FS_SCRL does NOT - which is the behaviour every bar but The Wire's and
     Sheet's has had since 13.10.5 and must still have
  B  tier 1: the SAME press arms FM_SBRATE286, and the view FOLLOWS the hand
     mid-drag, with the button still down
  C  the package arm is a DIFFERENT macro - `call OSAPI_CPU_INFO` where the
     kernel has `mov al, [cpu_tier]` - so Note Pad is driven too, and its
     answer is pixels because a package's copy of the element is its own
  D  SPEC.md 13.10.5.4.2's PAUSE commit, which is a different TRIGGER and not
     another value of the rate: with the periodic rate poked to 0 the view
     must not move while the hand does, must arrive once after the hand
     STOPS, and must still be there a second and a half later

BREAK IT ON PURPOSE (docs/WRITING-TESTS.md 1): `make SBRATE286=0` must fail
B and C and pass A, and `make SBRATE=2` must fail A.
"""
import os
import re
import sys
import time

sys.path.insert(0, "tools")
import os88build as _B
import os88marty as M
import os88ui
import os88geom as geom
from os88mouse import Mouse
from os88geom import WIN_SIZE, MAX_WIN

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
MACHINE = ARGS[0] if ARGS else "os8088_5150_cga_gla"
W_FLAGS, W_X, W_Y, W_W, W_H = 0, 2, 4, 6, 8
SBCELL, SBMINH = 10, 8
CPU_8086, CPU_286 = 0, 1

fails = []


def check(name, cond, note=""):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} {note}")
    if not cond:
        fails.append(name)


def _u16(b, o):
    return b[o] | (b[o + 1] << 8)


def _defnum(path, name, env):
    """The rate THIS BUILD was assembled with - never a copy here.

    THE KNOB WINS OVER THE SOURCE, and getting that backwards is what made
    the first version of this row pass against `SBRATE286=0`: the `%define`
    in the file is only the default a `-D` on the nasm line overrides, so a
    row that read the file alone asserted the shipped numbers against a tree
    built with other ones and called it a pass. `$OS88_DEFINES` carries the
    KERNEL's (os88build's `defines`) and `$OS88_PKGDEFS` the packages'
    ($(PKGSBDEF), which the Makefile already exports for the fast tier)."""
    for d in os.environ.get(env, "").replace("-D", " ").split():
        k, _, v = d.partition("=")
        if k == name and v:
            return int(v, 0)
    m = re.search(r"^%define\s+" + name + r"\s+(\d+)", open(path).read(), re.M)
    if not m:
        raise SystemExit(f"{name} not found in {path}")
    return int(m.group(1))


FM_SBIDLE = _defnum("kernel/files.inc", "FM_SBIDLE", "OS88_DEFINES")
FM_SBRATE = _defnum("kernel/files.inc", "FM_SBRATE", "OS88_DEFINES")
FM_SBRATE286 = _defnum("kernel/files.inc", "FM_SBRATE286", "OS88_DEFINES")
SB_RATE = _defnum("apps/notepad/notepad.asm", "SB_RATE", "OS88_PKGDEFS")
SB_RATE286 = _defnum("apps/notepad/notepad.asm", "SB_RATE286", "OS88_PKGDEFS")
SB_IDLE = _defnum("apps/notepad/notepad.asm", "SB_IDLE", "OS88_PKGDEFS")
FS_SCRL_OFS = int(re.search(r"^FS_SCRL\s+equ\s+(\d+)",
                            open("kernel/files.inc").read(), re.M).group(1))


def disk_win(m):
    blob = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    for i in range(MAX_WIN):
        r = blob[i * WIN_SIZE:(i + 1) * WIN_SIZE]
        if _u16(r, W_FLAGS) & 2 and _u16(r, W_H) > 60:
            return i, tuple(_u16(r, o) for o in (W_X, W_Y, W_W, W_H))
    return None, None


def ksb(m):
    b = m.read(m.sym("os88ui_ksb"), 14)
    return [_u16(b, i * 2) for i in range(7)]


def thumb(blk):
    x1, y1, x2, y2, total, fit, pos = blk
    if total <= fit:
        return None
    top = y1 + SBCELL + 1
    th = (y2 - SBCELL - 1) - top + 1
    if th < SBMINH:
        return None
    h = max((fit * th) // total, SBMINH)
    t = top + (pos * th) // total
    return min(t, top + th - h), h


def scrl(m):
    vp = int.from_bytes(m.read(m.sym("fm_vp"), 2), "little")
    return _u16(m.read((M.KERNEL_SEG << 4) + vp + FS_SCRL_OFS, 2), 0)


def rate(m):
    return m.read(m.sym("os88ui_sbd_rate"), 1)[0]


def tier(m, v=None):
    if v is not None:
        m.write(m.sym("cpu_tier"), bytes([v]))
    return m.read(m.sym("cpu_tier"), 1)[0]


def sig(m, mono, rect):
    if mono:
        w, h, rows = m.vram()
        return tuple(tuple(rows[y][rect[0]:rect[2] + 1])
                     for y in range(rect[1], rect[3] + 1))
    w, h, px = m.fbuf()
    return tuple(bytes(px[(y * w + rect[0]) * 3:(y * w + rect[2] + 1) * 3])
                 for y in range(rect[1], rect[3] + 1))


# fmthumb's disk, and for fmthumb's reason (13.10.5.3's quantisation): six
# files map the whole track onto one row of travel, which is a correct bar and
# a useless gate.
# fmthumb's disk plus one long document. The CONTENT is generated here and
# the IMAGE is os88marty.scratch_disk's, not an `if not exists` - that form
# builds once out of whatever build/ held that minute and boots it for ever
# after, which has already cost this project two wrong answers.
os.makedirs(_B.at("build/sbratesrc"), exist_ok=True)
NAMES = []
for _i in range(30):
    _n = _B.at("build/sbratesrc/FILE%02d.TXT" % _i)
    open(_n, "w").write("row %d\n" % _i)
    NAMES.append(_n)
# ...and the long one, for case C: a package's bar needs a document taller
# than its window before it has a thumb to take at all.
_n = _B.at("build/sbratesrc/LONG.TXT")
open(_n, "w").write("".join("line %03d of the long document\n" % _i
                            for _i in range(200)))
NAMES.append(_n)
DISK = M.scratch_disk("build/sbratedisk.img", *NAMES, size="360")


def one_drag(m, mo, cx, from_y, to_y, band, mono):
    """press at from_y, drag to to_y, and report what happened MID-drag -
    the button is still down when this returns."""
    was, before = scrl(m), sig(m, mono, band)
    mo.to(cx, from_y)
    mo._edge(True)
    time.sleep(0.8)
    armed = rate(m)
    mo.to(cx, to_y, l=True)
    time.sleep(1.6)
    return was, before, armed, scrl(m), sig(m, mono, band)


with os88ui.boot("build/os8088-360.img", apps=DISK, machine=MACHINE) as ui:
    m = ui.m
    mo = Mouse(marty=m)
    mono = m.video()["type"] in ("cga", "mda", "herc")
    print(f"== {MACHINE} : the rate is a PAIR (SPEC.md 13.10.5.4.1) ==")
    print(f"  build: FM_SBRATE {FM_SBRATE}/{FM_SBRATE286}, "
          f"SB_RATE {SB_RATE}/{SB_RATE286}")
    check("this machine detects as an 8086/8088", tier(m) == CPU_8086,
          f"(cpu_tier {tier(m)})")

    vw = int.from_bytes(m.read(m.sym("vid_w"), 2), "little")
    mo.dblclick(vw - 40, 80)                     # zone 1 = B:, the 30 files
    M.settle(m)
    slot, w = disk_win(m)
    if not w:
        sys.exit("no Disk window")
    blk = ksb(m)
    t = thumb(blk)
    if not t:
        sys.exit("nothing to scroll")
    top, h = t
    x1, y1, x2, y2, total, fit, _ = blk
    cx = (x1 + x2) // 2
    track_top, track_bot = y1 + SBCELL + 1, y2 - SBCELL - 1
    band = (x1 + 1, track_top, x2 - 1, track_bot)
    print(f"  thumb top {top} h {h}; track {track_top}..{track_bot}; "
          f"total {total} fit {fit}")

    # --- A: tier 0 is EXACTLY what shipped --------------------------------
    was, before, armed, now, after = one_drag(m, mo, cx, top + h // 2,
                                              track_bot, band, mono)
    check("tier 0 arms the 8086 rate", armed == FM_SBRATE,
          f"(os88ui_sbd_rate {armed}, wanted {FM_SBRATE})")
    check("...the thumb still moves with the hand", after != before)
    check("...and at rate 0 the view does NOT",
          (scrl(m) == was) if FM_SBRATE == 0 else (scrl(m) != was),
          f"(FS_SCRL {scrl(m)}, was {was})")
    mo._edge(False)
    M.settle(m)
    check("...and the release commits it", scrl(m) != was,
          f"(FS_SCRL {scrl(m)}, was {was})")

    # --- B: the same build, one byte different ----------------------------
    tier(m, CPU_286)
    check("the guest now reports a 286", tier(m) == CPU_286)
    blk = ksb(m)
    t = thumb(blk)
    top, h = t
    # drag BACK UP, so the view has somewhere to go from where A left it
    was, before, armed, mid, after = one_drag(m, mo, cx, top + h // 2,
                                              track_top, band, mono)
    check("a 286 arms the OTHER rate", armed == FM_SBRATE286,
          f"(os88ui_sbd_rate {armed}, wanted {FM_SBRATE286})")
    check("...and the VIEW FOLLOWS with the button still down", mid != was,
          f"(FS_SCRL {mid}, was {was})")
    check("...the thumb moved too", after != before)
    mo._edge(False)
    M.settle(m)

    # --- D: THE PAUSE COMMIT (13.10.5.4.2) --------------------------------
    # The periodic rate is poked to 0 so that the ONLY thing that can move the
    # view is the hand stopping - which is what makes this a test of the
    # trigger and not of the rate.
    tier(m, CPU_8086)
    t = thumb(ksb(m))
    mo.to(cx, t[0] + t[1] // 2)
    time.sleep(0.3)
    mo._edge(True)
    time.sleep(0.4)
    m.write(m.sym("os88ui_sbd_rate"), bytes([0]))
    v0 = scrl(m)
    for _ in range(10):
        m.mouse(0, 2, l=True)           # DOWN the track: case B left the view
    moving = scrl(m)                    # at the top, so up has nowhere to go
    check("the pause commit: nothing is drawn while the hand MOVES",
          moving == v0, f"(FS_SCRL {moving}, was {v0})")
    arrived, waited = None, 0
    for _ in range(70):
        time.sleep(0.06)
        waited += 1
        if scrl(m) != moving:
            arrived = scrl(m)
            break
    if FM_SBIDLE:
        check("...and it arrives when the hand STOPS", arrived is not None,
              f"(FS_SCRL {scrl(m)}, was {moving}, FM_SBIDLE {FM_SBIDLE})")
        settled = scrl(m)
        time.sleep(1.5)
        check("...ONCE, and it does not keep going", scrl(m) == settled,
              f"(FS_SCRL {scrl(m)}, was {settled})")
    else:
        check("...and with FM_SBIDLE 0 it does NOT", arrived is None,
              f"(FS_SCRL {scrl(m)}, was {moving})")
    mo._edge(False)
    M.settle(m)

    # --- C: THE PACKAGE ARM, which is a different macro -------------------
    # UI_CPUTIER is `mov al, [cpu_tier]` in the kernel and `call
    # OSAPI_CPU_INFO` in a package, so the two halves of 13.10.5.4.1 are not
    # the same code and one passing says nothing about the other. Note Pad is
    # the package driven, and the signal is PIXELS because a package's copy of
    # the element is its own - os88ui_sbd_rate is a KERNEL symbol and Note
    # Pad's byte of that name is inside its own image.
    #
    # It also proves the far call is transparent: os88ui_sbgrab takes the
    # block in BX and the press in DX and os88ui_sbrate is called between
    # loading them and using them, so a slot that clobbered either would
    # refuse the grab outright and the thumb would not move at all.
    tier(m, CPU_8086)
    np = ui.path("B:/LONG.TXT")             # .TXT -> NOTEPAD.O88 by assoc
    M.settle(m)
    w = geom.windows(m)[np] if isinstance(np, int) else np
    cx1, cy1, cx2, cy2 = w.content
    NP_MARGIN, NP_SB_ARR, NP_SB_W = 8, 11, 14
    sbx = cx2 - NP_SB_W // 2                # the bar owns the rightmost 14
    tracky = cy1 + NP_MARGIN + NP_SB_ARR    # np_ty + the up-arrow cell

    def track_bot():
        """The last row of the TRACK, read off the glass.

        [np_sbb] is NOT the content bottom - Note Pad keeps a band below the
        bar - so deriving it from the window rect put the second arm's press
        inside the DOWN-ARROW cell, where a press pages instead of grabbing
        and the case reported "nothing moved". The bar's own drawing answers
        it exactly: 13.10.5.9 frames the arrow cells, so the row between the
        track and the bottom arrow is BLANK and nothing else in the column
        is.
        """
        _w, _h, rows = m.vram()
        for y in range(tracky + SBMINH, cy2 + 1):
            if not any(rows[y][cx2 - NP_SB_W + 1:cx2 + 1]):
                return y - 1
        raise SystemExit("no track bottom under the Note Pad bar")
    # THE BAND SKIPS THE FIRST THREE ROWS AND THE BAR. The caret sits on row
    # 0 of a freshly opened document and BLINKS, so a signature that included
    # it would differ between any two reads and say "the text moved" on every
    # arm; the bar is excluded because the thumb moving is not the text
    # moving, which is the whole distinction this case is about.
    textband = (cx1 + 4, cy1 + NP_MARGIN + 24, cx2 - NP_SB_W - 4, cy2 - 4)
    print(f"  Note Pad {w!r}: bar x {sbx}, track top {tracky}")

    top_y, bot_y = tracky + 4, track_bot() - 3
    print(f"  ...track {tracky}..{track_bot()}, pressing {top_y} and {bot_y}")

    # THE SECOND ARM DRAGS THE OTHER WAY, and that is not symmetry for its own
    # sake. The first version returned the document to the top between arms
    # with a click at the track's top - which, with the thumb now at the
    # BOTTOM, is a press on the TRACK and pages up. Arm 2 then pressed the
    # track again instead of the thumb, the page-up moved the text on its own,
    # and the case read "the view followed" on a build where it cannot. The
    # release commits (13.10.5.4), so after arm 1 the thumb IS at the bottom:
    # start there and drag back up, and no repositioning click is needed at
    # all.
    for t, frm, to in ((CPU_8086, top_y, bot_y), (CPU_286, bot_y, top_y)):
        want_follow = (SB_RATE if t == CPU_8086 else SB_RATE286) > 0
        tier(m, t)
        mo.to(sbx, frm)                     # the thumb, where the last release
        time.sleep(0.4)                     # left it
        before = sig(m, mono, textband)     # ...and the band is read with the
                                            # ARROW ALREADY PARKED on the bar:
                                            # the pointer is drawn on the glass
                                            # (SPEC.md 7), so a read taken
                                            # before it moved would differ from
                                            # every later one for that reason
                                            # alone
        mo._edge(True)
        time.sleep(0.8)
        # RAW PACKETS AND NOT `mo.to`, AND THAT IS NOT A STYLE CHOICE. The
        # absolute driver confirms every packet by reading guest memory, which
        # costs ~680 GUEST ms per packet here - longer than SB_IDLE's 494, so
        # 13.10.5.4.2's pause commit fires BETWEEN two packets of what the
        # script means as one continuous drag. Driven that way this case reads
        # "the TEXT follows" on a build whose rate is 0, which is the pause
        # commit mis-attributed to the rate. A raw stream is ~17-34 ms a
        # packet, which is a real hand and is inside the deadline.
        # ...and the whole stream has to FIT the deadline: a round trip is
        # ~17 guest ms here, so 11 packets is ~190 of SB_IDLE's 494.
        step = 8 if to > frm else -8
        for _ in range(abs(to - frm) // 8):
            m.mouse(0, step, l=True)
        # NO SLEEP BEFORE THE READING, for the same reason: a host sleep is
        # magnified ~5.7x in guest time here, so even `time.sleep(0.20)` is
        # 1.1 GUEST seconds and lands the pause commit inside a window the
        # script means as "mid-drag".
        moved = sig(m, mono, textband) != before
        check(f"Note Pad, tier {t}: the TEXT "
              f"{'follows' if want_follow else 'waits'} mid-drag",
              moved == want_follow,
              f"(text changed mid-drag: {moved})")
        # ...and now the PAUSE, with the button still down
        paused = sig(m, mono, textband)
        arrived = False
        for _ in range(70):
            time.sleep(0.06)
            if sig(m, mono, textband) != paused:
                arrived = True
                break
        check(f"...and tier {t}: the pause commit lands with the button DOWN",
              arrived == (SB_IDLE > 0) or moved,
              f"(the band changed after the hand stopped: {arrived}, "
              f"SB_IDLE {SB_IDLE})")
        mo._edge(False)
        M.settle(m)
        after = sig(m, mono, textband)
        check(f"...and tier {t}'s release committed either way",
              after != before, "(the document did not scroll at all)")

    tier(m, CPU_8086)

print("FAILED: " + ", ".join(fails) if fails else "all passed")
sys.exit(1 if fails else 0)
