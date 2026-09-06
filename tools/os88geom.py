#!/usr/bin/env python3
"""One place for the kernel geometry the harness mirrors - and a guard that
fails LOUDLY when the kernel moves under it.

    python3 tools/os88geom.py          # check every mirrored constant, print

A LOCAL COPY REPORTED STALE IS A BUG WAITING, not noise, and the cost of one
is not the wrong number - it is the DAY spent believing the feature under test
is broken. `scan` reported four for as long as it had no caller: `MBAR_H` was
19 in tests/dispcorner.py, dispfsx.py and dispmcfs.py and 18 in
tests/tpdraw.py, against a kernel that has said 20 since the first commit in
the repository. All four are corrected, and what they changed was looked at:

  * dispcorner.py slices its comparison at `MBAR_H*pw*3` and re-adds `MBAR_H`
    to every coordinate it reports, the SAME constant, so it cancelled
    exactly and no reported coordinate was ever off. Its `room` calculation
    at :729 is the one place it bit - one row too much of drag room, which
    `(min(40,room)//8)*8` usually rounded away but could flip a CGA
    "no vertical room - SKIPPED" branch;
  * tpdraw.py, dispfsx.py and dispmcfs.py use it as a FILTER rather than as a
    cancelling pair, so menu-bar rows 18-19 survived it. Static today, one
    repaint away from a false failure.

`scan` HAS A CALLER NOW - tests/unit/t_mirror.py, in the fast tier, which
fails on any stale copy. That is what turns this module from a thing you can
run into a thing that runs. The four above are the argument for it: they were
sitting in this docstring, correctly described, for as long as nothing read
it.

WHY THIS EXISTS. Nine scripts had their own copy of the window record's
stride, the desktop's zone pitch and the Disk window's row height, and the
kernel moved four times: WIN_SIZE went 26 -> 28 when SPEC.md 13.7 added
W_ONMOUSEUP, 28 -> 30 for 13.8.2's W_ONDRAG and 30 -> 34 for 13.9's timer
pair - this guard caught the last two on the next run, which is exactly what
it is for - and SPEC.md 26.4's square CGA icon took the zone pitch 60 -> 34
and its width 48 -> 32. Every copy that did not follow went on returning
NUMBERS - a stride that decodes window 1 as garbage and reads it as unused, a
double-click 22px below a 14-row zone that lands on bare desktop - and both
failures look exactly like the FEATURE UNDER TEST being broken:

  * `tools/sucheck.py` and `tools/callfront.py` saw one window where there
    were two, so every "the newest window" pick was the older one;
  * `tests/dispcp.py`'s own `_cp_win` walked the table at 26 while the rest
    of that file used 28, so finding the Control Panel was luck;
  * a session lost an afternoon to "the second Disk window never opens",
    which was this and not the window manager.

IT HAS NOW HAPPENED A SECOND TIME, to a constant this module was not yet
covering. Nine harness scripts each wrote down `VID_CTX_SZ = 42` - SPEC.md
39.14's per-display context stride - and the record grew twice under them:
39.18's adapter kind, then 6.1.10's `vid_tseg`, which took `VID_CTX_W` from 18
to 19 and the stride from 42 to 44. Every copy went on reading display 1's
record TWO BYTES EARLY and returning numbers: a secondary card "32858 px
wide", an origin at x=340. `dispsave`, `dispnp` and `dockmark` all failed as
though the WINDOW SYSTEM were broken, which cost a bisect across 27 commits to
disbelieve.

What let it hide is worth stating on its own: **the constant that moved was a
DERIVED one** (`VID_CTX_SZ` is `VID_CTX_W*2+5` in vidsel.inc, an expression
`_equs` deliberately refuses to evaluate), and `scan` was comparing local
copies against `_MIRROR` alone. So the guard was watching the inputs and not
the answer. It compares against `_KNOWN` now - mirrored AND derived.

AND A FOURTH TIME, one level down from any of them: **the tree builds TWO
kernels off one source**, and this module mirrored ONE of them without saying
so. `WIN_SIZE` is 34 on kern_big and 28 on kern_small (SPEC.md 13.7's
W_ONMOUSEUP pair and 13.9's timers are kern_big's), the parser below took the
FIRST `equ` it saw, and `verify` compared it against that same first `equ` -
so the guard agreed with itself and every script pointed at kern_small
decoded the window table at the wrong stride. It returns NUMBERS, which is
the founding failure exactly: a Disk window at (103, 20, 322, 155) followed
by a second "window" at (60, 552, 93, 0), read as a package that failed to
launch. It cost a session of looking at the on-demand module that had just
been built, which was fine.

So this module now knows WHICH KERNEL, off `$OS88_DEFINES` - os88sym.py's own
knob, and the one every script pointed at kern_small already sets. A mirrored
constant may carry a value per arm, and one that exists on only one arm
(vidsel.inc's extended-desktop record is kern_big's whole) is REFUSED on the
other rather than answered with the big number: a script reading `VID_CTX_SZ`
on kern_small has no such record to read, and a plausible integer is how the
last three of these hid.

The lesson is the tree's own (SPEC.md 22's `fm_hit` discipline): the drawn
thing and the tested thing must come from ONE place. So this module is that
place, and it does two things no per-file copy can.

**It reads the guest wherever the kernel does.** `desk_rows`, `desk_zstep`,
`desk_zh1` and `vid_desk_zx` are live words - the adapter decides them at
boot and SPEC.md 39.11.2's Display page can change them while the machine
runs - so nothing here writes them down. Only assembly-time constants with
no published copy are mirrored.

**And every mirrored constant is CHECKED against the kernel source at
import**, by parsing the `equ`s out of the .inc files. A constant that moves
is then an error with the old and new values in it, on the next run of any
script, rather than a wrong coordinate nobody attributes for a day. That is
the whole point: this file cannot go stale quietly, which is the property the
nine copies lacked.
"""
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class GeomError(Exception):
    pass


# --- WHICH KERNEL -----------------------------------------------------------
#
# `make` sends -DKERN_BIG and `make small` sends -DKERN_SMALL, and the two
# disagree about the window record's stride - so a harness that does not know
# which one it is driving cannot decode wm_wins at all. $OS88_DEFINES is
# os88sym.py's knob for exactly this and every script pointed at kern_small
# already sets it (with $OS88_BUILD, for the map), so nothing new has to be
# threaded through and a plain run is unchanged.
_DEFINES = tuple(d for d in os.environ.get("OS88_DEFINES", "")
                 .replace(",", " ").split() if d)
ARM = "small" if "KERN_SMALL" in _DEFINES else "big"


# --- the mirrored constants -------------------------------------------------
#
# name -> (kernel file, value). The value is what this module uses; the file
# is where `verify` goes to find out whether it is still true. Anything the
# kernel computes at run time is NOT here - see the module docstring.

_MIRROR = {
    # kernel/wm.inc - the window record (SPEC.md 11)
    #
    # PER ARM, and the first constant here that is: kern_small stops the
    # record where SPEC.md 13.7 starts, so it is 28 there and 34 on kern_big.
    # A dict value means "this is not one number", and `verify` then reads the
    # kernel with the OTHER arm's %ifdef folded out rather than taking the
    # first `equ` in the file - which is what it did, and which made the guard
    # agree with itself while every kern_small script decoded garbage.
    "WIN_SIZE": ("kernel/wm.inc", {"big": 34, "small": 28}),
    "MAX_WIN": ("kernel/wm.inc", 12),
    "W_FLAGS": ("kernel/wm.inc", 0),
    "W_X": ("kernel/wm.inc", 2),
    "W_Y": ("kernel/wm.inc", 4),
    "W_W": ("kernel/wm.inc", 6),
    "W_H": ("kernel/wm.inc", 8),
    "W_TITLE": ("kernel/wm.inc", 10),
    # ...and the title bar's two boxes, which a scripted click has to land
    # INSIDE. Mirrored the day tests/xmcheck.py was found aiming at a
    # hard-coded (W_X+14, W_Y+5) for a window it assumed sat at x=180: the
    # window came up at 175, the click hit the title bar, and the gate
    # reported an extended-memory leak for a window that never closed.
    "WM_BOX_X0": ("kernel/wm.inc", 8),
    "WM_BOX_W": ("kernel/wm.inc", 10),
    "WM_BOX_Y0": ("kernel/wm.inc", 4),
    "WM_BOX_Y1": ("kernel/wm.inc", 14),
    "W_MENUS": ("kernel/wm.inc", 18),
    "W_SEG": ("kernel/wm.inc", 22),
    "WF_SIZABLE": ("kernel/wm.inc", 4),
    "WF_FULL": ("kernel/wm.inc", 8),
    "WF_NOSNAP": ("kernel/wm.inc", 16),
    "WF_SAVEU": ("kernel/wm.inc", 32),
    "WF_OWNBG": ("kernel/wm.inc", 64),
    "WF_KEEPH": ("kernel/wm.inc", 128),
    "WF_1BPP": ("kernel/wm.inc", 0x2000),
    "WF_USRSZ": ("kernel/wm.inc", 0x0200),
    # ...and the other two bits sharing the shape byte, mirrored for
    # WF_HIBITS' sake - see the derivation below.
    "WF_NOANIM": ("kernel/wm.inc", 0x4000),
    "WF_STALE": ("kernel/wm.inc", 0x8000),
    # kernel/instance.inc - the instance record (SPEC.md 29)
    "I_STATE": ("kernel/instance.inc", 0),
    "I_FLAGS": ("kernel/instance.inc", 1),
    "I_KIND": ("kernel/instance.inc", 2),
    "I_TASK": ("kernel/instance.inc", 3),
    "I_WIN": ("kernel/instance.inc", 4),
    "I_SPTR": ("kernel/instance.inc", 6),
    "I_SIZE": ("kernel/instance.inc", 8),
    "I_ICON": ("kernel/instance.inc", 10),
    "I_NAME": ("kernel/instance.inc", 12),
    "I_CYC": ("kernel/instance.inc", 28),
    "I_RECSZ": ("kernel/instance.inc", 32),
    "INST_MAX": ("kernel/instance.inc", 12),
    # kernel/dock.inc - the strip (SPEC.md 30)
    "DOCK_H": ("kernel/dock.inc", 24),
    "DOCK_X0": ("kernel/dock.inc", 8),
    "DOCK_STEP": ("kernel/dock.inc", 28),
    "DOCK_TILE_W": ("kernel/dock.inc", 24),
    "DOCK_TILE_H": ("kernel/dock.inc", 20),
    # kernel/desk.inc - the drive column (SPEC.md 26.1/26.4)
    "DESK_ZY0": ("kernel/desk.inc", 32),
    "DESK_ZW": ("kernel/desk.inc", 32),
    "DESK_COLW": ("kernel/desk.inc", 44),
    "DESK_ZOVER": ("kernel/desk.inc", 2),
    # kernel/disk.inc - the volume table (SPEC.md 18.7)
    # PER ARM (SPEC.md 51.0): kern_small can load no driver, so it can have no
    # DVK_DRV volume, so every volume it will ever have is one of the four
    # BIOS floppies SPEC.md 18.98 allows.
    "DVOL_MAX": ("kernel/disk.inc", {"big": 8, "small": 4}),
    "DV_KIND": ("kernel/disk.inc", 0),
    "DV_FLAGS": ("kernel/disk.inc", 2),
    "DV_SIZE": ("kernel/disk.inc", 16),
    "DVK_BIOS": ("kernel/disk.inc", 0),
    "DVK_DRV": ("kernel/disk.inc", 1),
    "DVK_FREE": ("kernel/disk.inc", 0xFF),
    # kernel/files.inc - a Disk window's list (SPEC.md 22)
    "FM_ROW_Y0": ("kernel/files.inc", 22),
    "FM_ROW_H": ("kernel/files.inc", 16),
    # ...and two fields of its per-window state block, which is what lets a
    # harness ask WHERE THE LIST IS SCROLLED TO instead of assuming row 0 is
    # entry 0. That assumption is the one dispcp.open_named exists to end.
    "FS_SCRL": ("kernel/files.inc", 2),
    "FS_N": ("kernel/files.inc", 6),
    "FS_VIEW": ("kernel/files.inc", 12),
    "FS_VSEG": ("kernel/files.inc", 16),
    # kernel/kernel.asm - the chrome
    "MBAR_H": ("kernel/kernel.asm", 20),
    "TITLE_H": ("kernel/kernel.asm", 18),
    "MENU_ITEM_H": ("kernel/menu.inc", 16),
    # ...and the bar cell's STRIDE, which four harness scripts wrote down by
    # hand and which moved 14 -> 12 the day MB_TX was dropped (SPEC.md 12.2).
    # Two of the four are registered rows and two are not, so without this the
    # unregistered pair would have gone on reading cell 2 as cell 1's tail.
    "MB_ENTSZ": ("kernel/menu.inc", 12),
    # kernel/mouse.inc - the pointer's CELL and the worst hot spot in it
    # (SPEC.md 7.1/7.2.2). A shape's cell starts at (pointer - hot), so a
    # harness masking the arrow's 8x12 at the published position misses the
    # part of a hot-spotted shape that sits ABOVE and LEFT of it.
    "CUR_GW": ("kernel/mouse.inc", 8),
    "CUR_GH": ("kernel/mouse.inc", 12),
    "CUR_XHX": ("kernel/mouse.inc", 3),
    "CUR_XHY": ("kernel/mouse.inc", 5),
    # kernel/fdlg.inc - the Standard File dialog's chrome, content-relative
    # (SPEC.md 38.4). Two test scripts click these, and a third copy is one
    # `equ` change away from a row that clicks empty background and reports a
    # feature broken.
    "FD_BX1": ("kernel/fdlg.inc", 224),
    "FD_BX2": ("kernel/fdlg.inc", 286),
    "FD_BY0": ("kernel/fdlg.inc", 20),
    "FD_BY1": ("kernel/fdlg.inc", 40),
    "FD_BY2": ("kernel/fdlg.inc", 60),
    "FD_BH": ("kernel/fdlg.inc", 13),
    "FD_LX1": ("kernel/fdlg.inc", 6),
    "FD_LX2": ("kernel/fdlg.inc", 213),
    "FD_ROW0": ("kernel/fdlg.inc", 22),
    "FD_ROWH": ("kernel/fdlg.inc", 16),
    # FD_TEXTX is deliberately NOT here. apps/ftpd/ftpd.asm defines its own
    # (8, the Setup page's text pen) and tests/ftpdflick.py carries a THIRD
    # value, 6 - so registering the kernel's 28 makes the gate compare two
    # constants that were never the same one. A row that wants to click a
    # listing entry should take FD_LX1..FD_LX2 and aim at the middle of the
    # row, which is what the whole row is: less coupled, and it does not need
    # this name at all.
    "KERNEL_SEG": ("kernel/kernel.asm", 0x0060),
    # kernel/memory.inc - the claim table (SPEC.md 50)
    # PER ARM (docs/KERN-SMALL-CUT-PLAN.md D1/D7): kern_small holds TWENTY claim records. The SDK
    # keeps the LARGER value, so a package over-allocates rather than
    # the kernel overflowing what it was handed (SPEC.md 51.0.0).
    "MEM_MAX": ("kernel/memory.inc", {"big": 32, "small": 20}),
    "MC_SEG": ("kernel/memory.inc", 0),
    "MC_PARA": ("kernel/memory.inc", 2),
    "MC_OWN": ("kernel/memory.inc", 4),
    "MC_DMA": ("kernel/memory.inc", 6),
    "MC_RLOC": ("kernel/memory.inc", 8),
    "MC_SIZE": ("kernel/memory.inc", 10),
    # kernel/vidsel.inc - the PER-DISPLAY CONTEXT record (SPEC.md 39.14)
    #
    # Nine harness scripts each wrote `VID_CTX_SZ = 42` down by hand, and the
    # record has now grown TWICE under them: SPEC.md 39.18's adapter kind, and
    # then 6.1.10's `vid_tseg`, which took VID_CTX_W from 18 to 19 and the
    # stride from 42 to 44. Every copy that did not follow went on reading
    # display 1's record TWO BYTES EARLY and returning numbers - a secondary
    # "32858 px wide", an origin at x=340 - and dispsave, dispnp and dockmark
    # all failed as though the WINDOW SYSTEM were broken. That is this
    # module's own founding story happening a second time, so the record joins
    # it rather than being fixed nine times.
    #
    # It has since SHRUNK, 19 -> 16, which is the same hazard the other way
    # up: [vid_strm1], [vid_rpara] and [vid_rend] were written every
    # vid_apply and read by nothing, so they left the run and took six bytes
    # of every record with them. VID_CTX_VX/VY/KIND/SZ are derived from this
    # number below, so the thirty scripts that import them followed with no
    # edit at all - which is what this module is for.
    #
    # ...and the whole record is KERN_BIG's: the extended desktop (SPEC.md
    # 39.12) is gated out of kern_small, so there is no vid_ctx there and no
    # `equ` to check against. A dict with one arm in it says so, and reading
    # one of these on the other arm RAISES - a script that wants a display
    # record on kern_small is asking about a thing that does not exist, and
    # answering it with 16 is how the three incidents above stayed hidden.
    "VID_CTX_W": ("kernel/vidsel.inc", {"big": 16}),
    "VID_CTX_CW": ("kernel/vidsel.inc", {"big": 14}),
    "VID_CTX_CH": ("kernel/vidsel.inc", {"big": 16}),
    "VID_NDISP_MAX": ("kernel/vidsel.inc", {"big": 2}),

    # --- found by the unmirrored() audit, not by anyone noticing -------------
    #
    # This record only ever held what somebody THOUGHT to add to it, and
    # `scan` only guards a name once it is here - so a constant with seven
    # local copies and no entry was invisible to both. These seven were
    # exactly that: 23 copies across 15 scripts, all agreeing, none guarded.
    #
    # `CP_RX` is the one that says why they are here now. Seven copies is the
    # shape of the VID_CTX_SZ incident above, which was nine - and that record
    # did not drift until the kernel grew a field, which is not a thing a
    # reviewer of the kernel change would think to look for.
    #
    # `APP_MAX_SIZE` is the one with teeth. It lives in `tools/os88pkg.py`,
    # which is one of the five host tools that WRITE SHIPPED BYTES, so a drift
    # there does not fail a test - it stamps a package against the wrong
    # ceiling.
    "APP_MAX_SIZE": ("kernel/kernel.asm", 61440),
    "WCR_SZ": ("kernel/kernel.asm", 8),
    "CP_RX": ("kernel/ctrl.inc", 96),
    "CP_IDRV": ("kernel/ctrl.inc", {"big": 2}),   # no Drivers page on small
    # ...and CP_ITHM is BIG-ONLY, one level further out than it looks: the
    # Theme (SPEC.md 76) is `%ifdef OS88_THEME`, which kernel.asm defines
    # under KERN_BIG - so the row does not exist on kern_small at all, and
    # ctrl.inc's `equ 4` in the no-drivers arm is the value it WOULD take.
    "CP_ITHM": ("kernel/ctrl.inc", {"big": 5}),
    "CP_ITIME": ("kernel/ctrl.inc", 1),
    "DSK_DE_SIZE": ("kernel/dskwin.inc", 32),
    # ...and the STAGED LISTING's stride, which is a different constant with a
    # different value (SPEC.md 19.1). A harness that walks `disk_dir` or a
    # window's view cache wants THIS one; DSK_DE_SIZE is the on-disk FAT
    # stride and the width of the record a DRVC_FILE driver hands over. Two
    # scripts were reading the listing at 32 and would have decoded garbage
    # from entry 1 onward the moment they diverged.
    "DSK_DE_STRIDE": ("kernel/dskwin.inc", 24),
    # kernel/sched.inc - the scheduler's slot count (SPEC.md 8). It went 8 ->
    # 14 with docs/STACK-SLOTS-PLAN.md and tests/saverate.py's copy did not,
    # so sch_cycles was read six slots short; the test reads os88sym now and
    # this entry is what makes the next copy a t_mirror failure.
    # PER ARM (docs/KERN-SMALL-CUT-PLAN.md D1/D7): kern_small has SIX worker slices and the UI task. The SDK
    # keeps the LARGER value, so a package over-allocates rather than
    # the kernel overflowing what it was handed (SPEC.md 51.0.0).
    "MAX_TASKS": ("kernel/sched.inc", {"big": 14, "small": 7}),
}

def _armval(name):
    """This arm's value for a mirrored constant, or None if it has none."""
    v = _MIRROR[name][1]
    return v.get(ARM) if isinstance(v, dict) else v


# Only the names this arm HAS. The rest are left undefined on purpose, so that
# `__getattr__` below can refuse them by name instead of handing back the other
# kernel's number.
globals().update({k: v for k in _MIRROR for v in (_armval(k),)
                  if v is not None})


def __getattr__(name):
    """A constant the OTHER arm has is a refusal, not an answer (PEP 562)."""
    if name in _MIRROR or name in _BIGONLY_DERIVED:
        raise GeomError(
            "os88geom: %s is not a constant of kern_%s - it is defined only "
            "under %s. This module is mirroring kern_%s because $OS88_DEFINES "
            "is %r; if that is wrong, set it (os88sym.py's knob). If it is "
            "right, the script asking for %s wants a kernel feature this one "
            "does not have."
            % (name, ARM,
               " and ".join("KERN_" + a.upper()
                            for a in sorted(_MIRROR.get(name, (None, {}))[1]))
               if name in _MIRROR else "KERN_BIG",
               ARM, ",".join(_DEFINES), name))
    raise AttributeError(name)

# Derived, and not mirrored: W_FLAGS bits 0 and 1 have no names in wm.inc.
WF_USED, WF_VIS = 1, 2
# WF_STALE|WF_NOANIM|WF_1BPP: the kernel's bits in the shape byte (SPEC.md
# 11.96.17), and `WF_HIBITS equ (WF_STALE | WF_NOANIM | WF_1BPP) >> 8` in
# wm.inc is an EXPRESSION, which _equs deliberately refuses to evaluate. So it
# is derived here from three values that ARE checked, rather than written down
# as a literal - which is what it was, with only WF_1BPP of the three mirrored,
# so two of its three inputs could move without a word from the guard. That is
# VID_CTX_SZ's failure exactly: watching some of the inputs and none of the
# answer. Getting this wrong reads a kernel bit as a cursor shape, which is the
# defect WF_STALE's own banner records having shipped once.
WF_HIBITS = (WF_STALE | WF_NOANIM | WF_1BPP) >> 8

# ...and the FOUR the kernel derives from VID_CTX_W the same way. They are
# EXPRESSIONS in vidsel.inc (`VID_CTX_W*2`, `VID_CTX_W*2+5`), which _equs
# deliberately refuses to evaluate - so they are computed here from the word
# count above, which IS checked. A change to the record moves all four on the
# next run of any script, which is the whole point.
# They follow VID_CTX_W onto kern_big alone, for its reason - there is no
# vid_ctx on kern_small to have a stride.
if ARM == "big":
    VID_CTX_VX = VID_CTX_W * 2      # the display's origin in the virtual desktop
    VID_CTX_VY = VID_CTX_W * 2 + 2
    VID_CTX_KIND = VID_CTX_W * 2 + 4  # ...WHICH ADAPTER, and not a segment
    VID_CTX_SZ = VID_CTX_W * 2 + 5  # ...the run, both origin words, and the kind
                                    # byte, which is the record's true END: the
                                    # +6 form left byte W*2+5 unused in every
                                    # row, and nothing indexes past the kind

# VID_CTX_KIND WAS THE ONE LEFT OUT, and leaving it out is what let the record
# go stale a THIRD time. `scan` compares a local copy against `_KNOWN`, so a
# name absent from `_KNOWN` is a copy the scanner walks straight past:
# tests/dispthm.py:42 said `VID_CTX_KIND = 40` in as many words, against a
# kernel that has said 42 since 6.1.10, and every run of the guard reported
# "0 stale" while looking directly at it. The kind byte is the worst field to
# read two bytes early, too, because what sits there is `vid_tseg` - so the
# reader gets 0xB000 or 0xB800, a FRAMEBUFFER SEGMENT, which is a plausible
# number rather than an obvious one and printed as an adapter kind or an
# x-coordinate for a cycle (tests/dispmode.py's "put display 1 at (45056,640)").

# Everything `scan` compares a local copy against: the mirrored constants AND
# the derived ones. It used to be _MIRROR alone, and that is exactly how nine
# copies of `VID_CTX_SZ = 42` went stale unseen - the constant that MOVED was
# the derived one, and the scanner was not looking at it.

# The purgeable ranks (SPEC.md 50.6.4), by the high byte of the owner word.
RANK = {0xFB: "trivial", 0xFC: "low", 0xFD: "medium", 0xFE: "high"}


_BIGONLY_DERIVED = ("VID_CTX_VX", "VID_CTX_VY", "VID_CTX_KIND", "VID_CTX_SZ")

_KNOWN = dict({k: v for k in _MIRROR for v in (_armval(k),) if v is not None},
              WF_USED=WF_USED, WF_VIS=WF_VIS, WF_HIBITS=WF_HIBITS)
if ARM == "big":
    _KNOWN.update(VID_CTX_VX=VID_CTX_VX, VID_CTX_VY=VID_CTX_VY,
                  VID_CTX_KIND=VID_CTX_KIND, VID_CTX_SZ=VID_CTX_SZ)


# --- the guard --------------------------------------------------------------

_EQU = re.compile(r"^\s*([A-Z_][A-Z0-9_]*)\s+equ\s+"
                  r"(0[xX][0-9a-fA-F]+|[0-9]+)\s*(?:;.*)?$")


_COND = re.compile(r"^\s*%(\w+)\s*(\S*)")


def _karm(root=None):
    """{symbol: arm} for every conditional this parser can resolve.

    The two kernels are chosen by `KERN_BIG` / `KERN_SMALL`, but no kernel
    source tests those directly for a FEATURE - kernel.asm turns each one into
    a named symbol first (`OS88_DRIVERS`, `OS88_ASSOC`, `FCP_MOD`, ...) so that
    a call site cannot disagree with the body it guards. A constant gated on
    one of those names is per-arm just as surely as one gated on the arm
    itself, and `DVOL_MAX` is the first: it is 4 under `KERN_SMALL` and 8
    otherwise, and mirroring the wrong one puts a harness back to reading a
    table at the wrong stride.

    So the map is READ OUT OF kernel.asm rather than written down - every
    `%define NAME` that sits inside a bare `%ifdef KERN_BIG` or
    `%ifdef KERN_SMALL`. A feature symbol added tomorrow is covered on the
    next run of any script, which is the property this whole module exists
    to have.
    """
    out = {"KERN_BIG": "big", "KERN_SMALL": "small"}
    path = os.path.join(root or ROOT, "kernel", "kernel.asm")
    if not os.path.exists(path):
        return out
    arm, depth = None, 0
    with open(path, "r", errors="replace") as fh:
        for line in fh:
            mo = _COND.match(line)
            if mo:
                d, a = mo.group(1).lower(), mo.group(2)
                if d == "ifdef" and a in ("KERN_BIG", "KERN_SMALL") and not depth:
                    arm, depth = out[a], 1
                elif d.startswith("if"):
                    depth += 1 if depth else 0
                elif d == "endif" and depth:
                    depth -= 1
                    if not depth:
                        arm = None
                elif d in ("else", "elif") and depth == 1:
                    arm = None      # the other side of an arm test: unresolved
                elif d == "define" and arm and depth == 1 and a:
                    # HERE and not below: `%define` matches _COND too (it is a
                    # directive), so a second pass over the same line never ran
                    # and this map came back with only the two arms in it.
                    out[a] = arm
                continue
    return out


_KARM = _karm()


def _equs(path, arm=None):
    """{NAME: int} for every `NAME equ <plain integer>` in a kernel source.

    Deliberately only PLAIN integers: several constants here are expressions
    over others (desk_zh1 is 32 + DESK_ZGAP + DESK_LBLH - 1), and a half
    evaluator that silently got one wrong would be worse than not checking it
    - so an expression is reported as unverifiable rather than guessed at.

    And deliberately only TWO conditionals: `%ifdef KERN_BIG` and
    `%ifdef KERN_SMALL`, whose inactive arm is folded out when `arm` is given.
    Every other `%if` is OPAQUE and both of its arms are scanned, which is
    what this did to all of them - so a knob is still first-wins and only the
    kernel this module claims to describe is resolved. Half an evaluator is
    exactly what the docstring above refuses; this is not one, because it
    evaluates only the two symbols it is told the answer to.
    """
    out = {}
    stack = []                  # one [kind, live] per open %if, kind 'k'|'?'
    with open(path, "r", errors="replace") as fh:
        for line in fh:
            mo = _COND.match(line)
            if mo:
                d, arg = mo.group(1).lower(), mo.group(2)
                if d in ("ifdef", "ifndef") and arm and arg in _KARM:
                    live = (_KARM[arg] == arm) == (d == "ifdef")
                    stack.append(["k", live])
                elif d.startswith("if"):
                    stack.append(["?", True])
                elif d.startswith("eli") and stack:
                    stack[-1] = ["?", True]     # not understood -> scan it
                elif d == "else" and stack:
                    if stack[-1][0] == "k":
                        stack[-1][1] = not stack[-1][1]
                elif d == "endif" and stack:
                    stack.pop()
                continue
            if not all(e[1] for e in stack):
                continue
            mo = _EQU.match(line)
            if mo:
                out.setdefault(mo.group(1), int(mo.group(2), 0))
    return out


def verify(root=None):
    """Every mirrored constant against the kernel. Returns a list of strings,
    empty when they all still agree."""
    root = root or ROOT
    problems, cache = [], {}
    for name, (rel, _) in sorted(_MIRROR.items()):
        mine = _armval(name)
        if mine is None:
            continue            # the other arm's constant - nothing to check
        path = os.path.join(root, rel)
        if rel not in cache:
            if not os.path.exists(path):
                problems.append("%s: %s is missing" % (name, rel))
                cache[rel] = {}
                continue
            cache[rel] = _equs(path, ARM)
        theirs = cache[rel].get(name)
        if theirs is None:
            problems.append(
                "%s: no plain `%s equ <int>` in %s under kern_%s - it was "
                "renamed, removed, turned into an expression, or gated onto "
                "the other kernel. Check it by hand and update "
                "tools/os88geom.py." % (name, name, rel, ARM))
        elif theirs != mine:
            problems.append(
                "%s: this module says %d for kern_%s and %s says %d - the "
                "kernel moved. Fix the value here; every script reads it from "
                "this one place." % (name, mine, ARM, rel, theirs))
    return problems


def _check_at_import():
    # A source tree is not always there (a wheel, a copied script), and a
    # missing kernel is not a mismatch - only an ANSWERED disagreement is.
    if not os.path.isdir(os.path.join(ROOT, "kernel")):
        return
    problems = verify()
    if problems:
        raise GeomError(
            "tools/os88geom.py has gone stale against kern_%s:\n  " % ARM
            + "\n  ".join(problems)
            + "\n\nEvery scripted coordinate in the harness comes from here, "
              "so this is stopping now rather than aiming clicks at the wrong "
              "pixels and reporting the feature under test as broken.")


_check_at_import()


# --- reading the guest ------------------------------------------------------
#
# `sym` is the symbol resolver. It defaults to the Marty's own `m.sym`; the
# display tests pass their cached `S` because they resolve against a kernel
# built with -D knobs (os88sym.py --define).

def _sym(m, sym):
    return sym if sym is not None else m.sym


def word(m, name, sym=None):
    """One live kernel word, by symbol."""
    b = m.read(_sym(m, sym)(name), 2)
    return b[0] | (b[1] << 8)


class Win(object):
    """One window record, decoded at the CURRENT stride."""

    def __init__(self, m, i, raw, sym=None):
        b = i * WIN_SIZE
        self.i = i
        self.flags = struct.unpack_from("<H", raw, b + W_FLAGS)[0]
        self.x, self.y, self.w, self.h = struct.unpack_from("<HHHH", raw,
                                                            b + W_X)
        tp = struct.unpack_from("<H", raw, b + W_TITLE)[0]
        seg = struct.unpack_from("<H", raw, b + W_SEG)[0]
        self.title = bytes(m.readseg(seg or KERNEL_SEG, tp, 24)) \
            .split(b"\0")[0].decode("latin-1")

    @property
    def used(self):
        return bool(self.flags & WF_USED)

    @property
    def visible(self):
        return bool(self.flags & WF_VIS)

    @property
    def promises(self):
        return bool(self.flags & WF_SAVEU)

    @property
    def mono(self):
        """SPEC.md 11.96.17: it claims its content is colour 0 or 15 only."""
        return bool(self.flags & WF_1BPP)

    @property
    def shape(self):
        """SPEC.md 7.2.1's cursor shape - the high byte MINUS the kernel's.

        Three flags share that byte and the mask is WF_HIBITS in the kernel;
        getting it wrong here reads a kernel bit as a shape, which is the
        exact defect WF_STALE's banner records having shipped once.
        """
        return (self.flags >> 8) & ~(WF_HIBITS) & 0xFF

    @property
    def content(self):
        """The content rect - what wm_su_rect answers and the cache holds."""
        return (self.x + 1, self.y + TITLE_H,
                self.x + self.w - 2, self.y + self.h - 2)

    def covers(self, x, y):
        return (self.x <= x < self.x + self.w
                and self.y <= y < self.y + self.h)

    def __repr__(self):
        return "%s@%d(%d,%d,%d,%d)%s%s" % (
            self.title, self.i, self.x, self.y, self.w, self.h,
            "" if self.visible else " HIDDEN",
            "+saveu" if self.promises else "")


def snapw(w, flush=False, x=None, screen=None):
    """A template frame width as SPEC.md 11.94.5's size snap leaves it.

    wm_snap_w rounds a frame so its CONTENT is a whole number of framebuffer
    bytes - W_W - 2 with a left border, W_W - 1 without one - so a window does
    not come up the width its template asked for. Mirrored here rather than
    retyped at each call site because three tests identify a window BY its
    size, and a literal is what turned all three into failures the day the
    snap landed: dispcorner's hl_tpl 240 is 242, dispclose's os88ui_ask 288 is
    290, fdlggrey's dialog 300 is 306.

    UP WHERE IT FITS, DOWN WHERE IT DOES NOT, and this function had it
    unconditionally down for a cycle because the kernel did (SPEC.md
    11.94.5.1): rounding down hands a package less content than its template
    asked for, and a layout drawn from `template_w - 2` then runs over the
    window's own right border.

    `x` and `screen` are what decides the direction, and they are optional
    because every caller here is a window narrow enough that UP fits. Pass
    them for a window near the right edge - TeXPad at x = 7 on a 640 screen
    is the one in the tree that takes the DOWN branch.

    It does NOT model the other two refusals - a frame spanning the screen,
    and a DOWN result under the window's declared minimum - because no test
    subject is either. Read the record rather than this function if that
    changes.
    """
    c = 1 if flush else 2
    up = ((w - c + 7) & ~7) + c
    if x is None or screen is None or x + up <= screen:
        return up
    down = ((w - c) & ~7) + c
    return down if down > c else w


def close_xy(wx, wy):
    """The close box's centre for a window whose frame starts at (wx, wy).

    wm_hit accepts columns W_X+WM_BOX_X0 .. +WM_BOX_X0+WM_BOX_W and rows
    W_Y+WM_BOX_Y0 .. +WM_BOX_Y1, so the centre is the safe aim - a click one
    column past the box is a click on the TITLE BAR, which starts a drag and
    closes nothing. The minimize box is the same span mirrored at
    W_X+W_W-1-WM_BOX_X0, and is not written out here because nothing asks for
    it yet; the four constants above are what an asker would need.
    """
    return (wx + WM_BOX_X0 + WM_BOX_W // 2,
            wy + (WM_BOX_Y0 + WM_BOX_Y1) // 2)


def windows(m, sym=None):
    """Every USED window record."""
    raw = m.read(_sym(m, sym)("wm_wins"), MAX_WIN * WIN_SIZE)
    return [Win(m, i, raw, sym) for i in range(MAX_WIN)
            if struct.unpack_from("<H", raw, i * WIN_SIZE)[0] & WF_USED]


def winptr(m, win, sym=None):
    """The kernel address of a window record - what wm_top answers with."""
    i = win.i if hasattr(win, "i") else win
    return _sym(m, sym)("wm_wins") + i * WIN_SIZE


def win_rect(m, slot, sym=None):
    """(x, y, w, h) of one window slot, by index."""
    r = m.read(_sym(m, sym)("wm_wins") + slot * WIN_SIZE, WIN_SIZE)
    return struct.unpack_from("<HHHH", r, W_X)


def win_list(m, sym=None, check=True):
    """The slots that are used - and visible too, unless check is False."""
    want = (WF_USED | WF_VIS) if check else WF_USED
    raw = m.read(_sym(m, sym)("wm_wins"), MAX_WIN * WIN_SIZE)
    return [i for i in range(MAX_WIN)
            if struct.unpack_from("<H", raw, i * WIN_SIZE)[0] & want == want]


def top(m, sym=None):
    """wm_top's answer - the address of the frontmost VISIBLE window, or 0.

    Taken the way wm_top takes it, off the back of wm_zord, so a z-order bug
    cannot hide behind a re-derivation from the window table.
    """
    S = _sym(m, sym)
    n = m.read(S("wm_zn"), 1)[0]
    if not n:
        return 0
    zord = m.read(S("wm_zord"), n)
    raw = m.read(S("wm_wins"), MAX_WIN * WIN_SIZE)
    for k in range(n - 1, -1, -1):
        i = zord[k]
        if struct.unpack_from("<H", raw, i * WIN_SIZE)[0] & WF_VIS:
            return S("wm_wins") + i * WIN_SIZE
    return 0


def instances(m, sym=None):
    """{slot: {state, flags, win, minimized}} for every LIVE instance."""
    raw = m.read(_sym(m, sym)("inst_tab"), INST_MAX * I_RECSZ)
    out = {}
    for i in range(INST_MAX):
        b = i * I_RECSZ
        if raw[b + I_STATE] != 1:
            continue
        out[i] = {"state": raw[b + I_STATE], "flags": raw[b + I_FLAGS],
                  "win": struct.unpack_from("<H", raw, b + I_WIN)[0],
                  "minimized": bool(raw[b + I_FLAGS] & 1)}
    return out


def tile_xy(m, win, sym=None):
    """The centre of the dock tile of the instance owning a window.

    Through wm_owner (window slot -> instance slot) rather than counted off by
    eye: tile position i belongs to instance-table record i, holes included,
    so the Nth tile on screen is not the Nth instance (SPEC.md 30).
    """
    S = _sym(m, sym)
    i = win.i if hasattr(win, "i") else win
    inst = m.read(S("wm_owner"), MAX_WIN)[i]
    if inst == 0xFF:
        raise GeomError("window slot %d has no instance, so no dock tile" % i)
    return (DOCK_X0 + inst * DOCK_STEP + DOCK_TILE_W // 2,
            word(m, "vid_dock_y0", sym) + DOCK_TILE_H // 2)


def drive_ordinal(m, letter="B", sym=None):
    """Which desktop ZONE does drive `letter` own? None if it has none.

    NOT the drive number. A zone exists per volume with DV_FLAGS bit 0 set and
    the ordinal is that volume's POSITION among the shown ones - so a machine
    whose B: was retired by SPEC.md 18.97's probe, or which mounts a hard
    disk, numbers them differently. Walking dsk_vtab is the only way to be
    right, and it turns "no window opened" into "B: has no zone", which is the
    difference between a test that fails and a test that says why.
    """
    want = ord(letter.upper()) - ord("A") if isinstance(letter, str) else letter
    t = m.read(_sym(m, sym)("dsk_vtab"), DVOL_MAX * DV_SIZE)
    n = 0
    for v in range(DVOL_MAX):
        r = t[v * DV_SIZE:(v + 1) * DV_SIZE]
        if r[DV_KIND] == DVK_FREE or not (r[DV_FLAGS] & 1):
            continue
        if v == want:
            return n
        n += 1
    return None


def drive_xy(m, ordinal, sym=None):
    """The centre of zone `ordinal`, in virtual screen coordinates.

    desk_ord_xy's arithmetic (SPEC.md 26.1): zones fill a column downwards and
    wrap to a NEW COLUMN ON THE LEFT, and how many fit is [desk_rows] - 2 on a
    CGA with the tall icon, 4 with SPEC.md 26.4's square one, 4 on Hercules,
    7 on VGA. The pitch and the zone's height are live words for that reason.
    """
    rows = word(m, "desk_rows", sym)
    step = word(m, "desk_zstep", sym)
    zh1 = word(m, "desk_zh1", sym)
    zx = word(m, "vid_desk_zx", sym)
    col, row = divmod(ordinal, rows)
    return (zx - col * DESK_COLW + DESK_ZW // 2,
            DESK_ZY0 + row * step + zh1 // 2)


def drive_pt(m, letter="B", sym=None):
    """drive_xy for a LETTER, refusing a drive with no zone by name."""
    ordinal = drive_ordinal(m, letter, sym)
    if ordinal is None:
        raise GeomError("drive %s: has no desktop zone on this machine "
                        "(dsk_vtab says it is free or hidden)" % letter)
    return drive_xy(m, ordinal, sym)


def row_xy(win, row=0, x=60):
    """fm_layout's list geometry (SPEC.md 22): row `row`'s middle, in the
    content of a Disk window. Takes a Win, or an (x, y) frame origin."""
    wx, wy = (win.x, win.y) if hasattr(win, "x") else win
    return (wx + x, wy + TITLE_H + FM_ROW_Y0 + row * FM_ROW_H + FM_ROW_H // 2)


# --- and the same question asked of the harness itself ----------------------

# A name a host script defines that the kernel also defines, where the two
# mean DIFFERENT things. `unmirrored` would otherwise nominate them for ever.
#
# BAND_KB is the worked example and the reason this list is by NAME and not by
# value: kernel/band.inc's is the 1bpp title-bar composer's buffer (2 KB, and
# no shipped build even compiles that file), while tests/paintsu.py's is a
# threshold about PAINT's band cache - "the band cache measures 3 KB, the whole
# content 9". Same name, unrelated things, and the values differ, so a check
# that trusted the match would report a drift that is not one.
_COLLISIONS = {
    "BAND_KB": "kernel/band.inc's title-bar composer buffer vs Paint's own "
               "band-cache threshold in tests/paintsu.py - unrelated",
}


def unmirrored(root=None, least=2):
    """Kernel constants host scripts hard-code that this record does NOT hold.

    `scan` is the wrong way round to catch the next VID_CTX_SZ: it only looks
    at names ALREADY in `_MIRROR`, so a constant with seven local copies and no
    entry is invisible to it, and the record only ever held what somebody
    thought to add. This is the other direction - every `NAME equ <int>` the
    kernel defines, against every `NAME = <int>` a host script defines.

    Returns [(name, kernel_file, kernel_value, [(script, line, value), ...])]
    for names with at least `least` copies, worst first. `least` defaults to 2
    because ONE copy is a script naming a thing, and two is the beginning of
    the drift that took nine scripts down.
    """
    import ast

    root = root or ROOT
    kern = {}
    equ = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s+equ\s+(.+?)\s*$", re.I)
    kdir = os.path.join(root, "kernel")
    if os.path.isdir(kdir):
        for fn in sorted(os.listdir(kdir)):
            if not (fn.endswith(".inc") or fn.endswith(".asm")):
                continue
            rel = os.path.join("kernel", fn)
            for line in open(os.path.join(kdir, fn), "r", errors="replace"):
                m = equ.match(line.split(";")[0])
                if not m:
                    continue
                try:
                    kern.setdefault(m.group(1), (rel, int(m.group(2).strip(), 0)))
                except ValueError:
                    pass

    hits = {}
    for sub in ("tools", "tests", os.path.join("tests", "unit")):
        d = os.path.join(root, sub)
        if not os.path.isdir(d):
            continue
        for fn in sorted(os.listdir(d)):
            if not fn.endswith(".py") or fn == "os88geom.py":
                continue
            try:
                tree = ast.parse(open(os.path.join(d, fn), "r",
                                      errors="replace").read())
            except SyntaxError:
                continue
            for node in ast.walk(tree):
                if not isinstance(node, ast.Assign):
                    continue
                for tgt in node.targets:
                    if (isinstance(tgt, ast.Name)
                            and isinstance(node.value, ast.Constant)
                            and isinstance(node.value.value, int)):
                        hits.setdefault(tgt.id, []).append(
                            (os.path.join(sub, fn), node.lineno,
                             node.value.value))

    out = []
    for name, places in hits.items():
        if name in _MIRROR or name in _COLLISIONS or name not in kern:
            continue
        # A short or mixed-case name matching a kernel equ is a coincidence far
        # more often than it is a copy.
        if len(name) < 4 or not name.isupper() or len(places) < least:
            continue
        rel, val = kern[name]
        out.append((name, rel, val, sorted(places)))
    return sorted(out, key=lambda r: (-len(r[3]), r[0]))


def scan(root=None):
    """Every LOCAL copy of a mirrored constant in tools/ and tests/.

    Returns [(path, line, name, value, kernel_value)], stale ones first. This
    module fixes the copies that had already gone wrong; `scan` is what stops
    the next one going wrong quietly, because a copy that is correct today is
    only the seed of the next occurrence - and several are left deliberately,
    where converting a working test would be more churn than the copy is
    worth. One command names them all and says which have drifted.
    """
    import ast

    root = root or ROOT
    out = []
    for sub in ("tools", "tests"):
        d = os.path.join(root, sub)
        if not os.path.isdir(d):
            continue
        for fn in sorted(os.listdir(d)):
            if not fn.endswith(".py") or fn == "os88geom.py":
                continue
            path = os.path.join(d, fn)
            try:
                tree = ast.parse(open(path, "r", errors="replace").read())
            except SyntaxError:
                continue
            for node in ast.walk(tree):
                if not isinstance(node, ast.Assign):
                    continue
                for tgt in node.targets:
                    # NAME = 18, and NAME, OTHER = 18, 20 - the two spellings
                    # the harness actually uses.
                    pairs = []
                    if isinstance(tgt, ast.Name):
                        pairs = [(tgt, node.value)]
                    elif (isinstance(tgt, ast.Tuple)
                          and isinstance(node.value, ast.Tuple)
                          and len(tgt.elts) == len(node.value.elts)):
                        pairs = list(zip(tgt.elts, node.value.elts))
                    for nm, val in pairs:
                        if not isinstance(nm, ast.Name) or nm.id not in _KNOWN:
                            continue
                        if not isinstance(val, ast.Constant) \
                                or not isinstance(val.value, int):
                            continue
                        out.append((os.path.join(sub, fn), node.lineno, nm.id,
                                    val.value, _KNOWN[nm.id]))
    return sorted(out, key=lambda r: (r[3] == r[4], r[0], r[1]))


def _main():
    problems = verify()
    for p in problems:
        print("os88geom: %s" % p)

    copies = scan()
    stale = [c for c in copies if c[3] != c[4]]
    for path, line, name, mine, theirs in copies:
        print("os88geom: %-22s %s:%d = %d%s"
              % (name, path, line, mine,
                 "" if mine == theirs else "  ** STALE, kernel says %d **"
                 % theirs))
    print("os88geom: kern_%s: %d constants (%d on this arm), %d problem(s); "
          "%d local copies, %d stale"
          % (ARM, len(_MIRROR),
             sum(1 for k in _MIRROR if _armval(k) is not None),
             len(problems), len(copies), len(stale)))
    return 1 if problems or stale else 0


if __name__ == "__main__":
    sys.exit(_main())
