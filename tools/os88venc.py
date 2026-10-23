#!/usr/bin/env python3
"""os88venc - any video to a .V88 (SPEC.md 98.2.1; VIDEO-PLAN wave 8).

    python3 tools/os88venc.py IN.MP4 OUT.V88 [--preset herc] [--box WxH]
        [--layout cga|herc|lin80] [--fit fit|fill|stretch]
        [--start S] [--end S] [--fps F] [--profile 5150-st225]
        [--audio pcm8|adpcm4|none] [--rate HZ] [--adpcm search|greedy]
        [--dither bayer|bluenoise|threshold] [--stable N]
        [--gamma G] [--contrast C] [--brightness B] [--invert]
        [--title T] [--credits C] [--keysecs S] [--poster K | --poster-at S]
        [--clip N]
        [--preview-png DIR] [--quiet]
    python3 tools/os88venc.py IN OUT.WAV [--rate HZ] [--spk-style S] ...
        (the SOUND alone, for Audio on the PC speaker: SPEC.md 86.21.1)

THE FRONT END, AND THE BUDGETS. ffmpeg decodes and scales the source to a
canvas whose DISPLAYED shape is the source's (the layout's pixels are not
square: CGA's are 5:12, Hercules' 29:45), grey, at a frame rate the audio
divides exactly. Each grey frame is dithered to MONO1 by an ORDERED
threshold anchored to the canvas - so a still area dithers identically
frame after frame and costs nothing - and a pixel within `--stable` grey
levels of its threshold keeps the value it had, so a source's own noise
does not flip it. That frame is the TARGET.

The stream is the SCREEN chasing the target under the machine's limits
(VIDEO-PLAN 3.2), taken from a PROFILE:
  - the DISK: a bucket of bytes refilled at the profile's rate less the
    audio's, holding at most one second - a frame may spend what quieter
    frames left, which is the burst allowance;
  - the CPU: the same for decode cycles at the profile's average share of
    the machine, with a per-frame ceiling on top (a scene cut drawn in one
    or two frames, never one frame over its own period).
A frame whose changes fit is exact. One that does not commits its changed
spans in order of pixels fixed per cycle - AGED, so an error that has sat
on the screen outranks a fresh one of the same size - until a bucket or the
ceiling says stop, and the rest is still wrong on the next frame and
competes again. Keyframes (SPEC.md 98.1.3) are the screen, not the target:
a seek shows exactly what a play would.

The cost model is os88vid's (wave 0's, CGA on MartyPC, cycles), plus the
interrupt's audio copy. ffmpeg is needed for this and for nothing else in
the tree; numpy likewise.
"""
import argparse
import math
import os
import shutil
import subprocess
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88vid as vid                                         # noqa: E402

try:
    import numpy as np
except ImportError:                                           # pragma: no cover
    np = None

# --------------------------------------------------------------------------
# profiles and presets
# --------------------------------------------------------------------------
# disk: bytes a second the stream may take, audio included; avg/peak: the
# share of a frame period decode may take over any second / in any frame on
# time; owe: the most one frame may run to, in the machine's own PERIODS,
# the two after it paying it back (98.2.1.1); speed: the machine in 8088s,
# which avg and peak are already counted in and owe is not, and which the
# SPEAKER's cost is divided by too (98.2.15); ring: the player's read-ahead
# ring on that machine, in 32 KB slots, which the disk reserve banks bursts
# in (98.2.1.3).
# rate/audio: the default sound. "predicted" profiles are arithmetic, not a
# field reading (docs/FIELD-MACHINES.md).
# A MACHINE'S OWN DECODE (98.2.3.3), microseconds on it, in cyc_table's
# terms: fitted to VIDBENCH's synthetic frames (os88vid synthxdv) as the
# owner's 86Box machines decoded them - every row of the fit reproduced to
# 0.1% (docs/reports/VIDEO-86BOX-486-2026-10-07.md). A segment's set-up is
# held at the 8088 model's ratio to a P1 (215 / 49.6), the benches not
# separating the two; P5 is between P4 and P6, which it was not timed at.
# `sub`, a sub-record's Map Mask OUT, is ESTIMATED at an ISA OUT's ~1.5 us
# - at 7 or 15 a frame it is under 1% of a 24 fps period either way
CYC_US_286 = dict(frame=28.07, seg=9.06, abs=0.392,
                  p=(2.090, 2.839, 4.120, 4.870, 5.887, 6.904),
                  slice=(2.453, 0.914), run=(3.047, 0.900), sub=1.5)
# ...and the owner's REAL 286, measured 2026-10-08 (docs/reports/VIDBENCH-
# PVGA286-2026-10-08.md): an AMD 286 at 16 MHz with a Paradise PVGA1A on
# the board. Fitted the same way, to the same synthetic frames (the frame's
# own cost held at the empty frame's), every row back within 0.6%. Its RAM has wait
# states 86Box's mr286 has not (`rep movsw` to RAM 2.64 ms an 8,000 bytes
# against 1.05), so it decodes 12-29% slower frame for frame - a slice
# byte 1.23 us against 0.91, a run byte 1.06 against 0.90
CYC_US_286PVGA = dict(frame=25.35, seg=10.274, abs=0.555,
                      p=(2.370, 3.480, 4.866, 6.150, 7.481, 8.813),
                      slice=(1.595, 1.227), run=(2.470, 1.065), sub=1.5)
# ...and the same 286 with its ISA bus at 8 wait states rather than 14 (the
# BIOS's "Tune", 2026-10-15, docs/reports/VIDPM-VICTORY286-2026-10-15.md):
# VIDBENCH run both ways on one boot, and 14 read the 10-08 table's rows to
# 0.7%. Writing the screen is what the bus setting moves - every RAM row the
# same to the microsecond - so the table is 10-08's with each term scaled by
# what a least-squares fit of the two runs' synthetic frames gives it: a P
# entry 0.946, a slice byte 0.932, a run byte 0.918, a segment and the frame
# as they were. ESTIMATED by scaling, not refitted from nothing
CYC_US_286PVGA8 = dict(frame=25.35, seg=10.274, abs=0.555,
                       p=tuple(round(x * 0.946, 3) for x in
                               CYC_US_286PVGA["p"]),
                       slice=(round(1.595 * 0.932, 3), round(1.227 * 0.932, 3)),
                       run=(round(2.470 * 0.918, 3), round(1.065 * 0.918, 3)),
                       sub=1.5)
CYC_US_486 = dict(frame=2.51, seg=2.392, abs=0.109,
                  p=(0.552, 0.957, 1.396, 1.827, 2.259, 2.692),
                  slice=(0.474, 0.407), run=(0.618, 0.390), sub=1.5)
# THE XMS COPIES A PLAYER MAKES (Encoder.set_xplay, LOADALL-PLAN 7 and L5).
# A 286 plays every file it can hold or bank IN PLACE by LOADALL
# (SPEC.md 98.3.18.11, .13): no copy down, `xbor_n` borrows a frame and one
# more each `xyield` record bytes at `xbor_us` (VIDDISK L's bare borrow,
# 0.14-0.17 ms on the Victory, docs/reports/LOADALL-VICTORY286-2026-10-14.md
# - an ESTIMATE of the player's, until a play is profiled), and the bank's
# copy UP on the disk at `xup` ms a KB. `xdown` is the copy down for a play
# the in-place paths refuse (the speaker, Repeat, Live...).
# The Victory's `xup`/`xdown`: a 32 KB copy at the gap the player places its
# ring at (SPEC.md 98.3.18.12) is 5.96 ms, either transport - 0.186 ms a KB
# (docs/reports/XMS-TRANSPORT-VICTORY286-2026-10-15.md). The other 286
# keeps its unplaced 0.345, never re-measured
XPLAY_VICTORY = dict(xplay="inplace", xup=0.186, xdown=0.186, xbor_us=160.0,
                     xbor_n=3, xyield=2048)
# THE PLAY, PROFILED on the same machine (SPEC.md 98.3.22.1, docs/reports/
# VIDPROF-VICTORY286-2026-10-15.md): three in-place plays timed frame by
# frame, each draw fitted against the table's price, the record's KB and a
# constant together - so the triple is the WHOLE in-place draw, borrows
# included, and replaces xbor_* for a layout it names. 16 colours (Cowboy
# Bebop): 0.932 x table + 12 us a KB + 1,151 us - its borrows hide in the
# table, which already ran a little high. Mode X (Bad Carrot): 1.102 x
# table + 146 us a KB + 1,096 us, with the back page at 0.952 of the table
# (cost0). GasGas's own fit is page-ahead's two records a frame moving the
# KB with the table, and fixes nothing
PLAY_VICTORY = dict(play={"modex": (1.102, 146.5, 1096.0),
                          "planar": (0.932, 12.4, 1151.0)})
XPLAY_286 = dict(xplay="inplace", xup=0.345, xdown=0.345, xbor_us=160.0,
                 xbor_n=3, xyield=2048)
PROFILES = {
    # MEASURED on the 5150 (docs/reports/VIDDISK-ST225-CYL-2026-10-07.md):
    # VIDDISK under the CYLINDER bound (SPEC.md 18.91.5, the default since
    # build 458) streamed 130.1 KB/s with the hook holding half of every
    # period, the profile's `avg` - and 134.8 idle, 131.5 at 25%, 113.9 at
    # 75%. The budget is ~90% of the 50% row: 0.9 x 130.1 KB x 1024 =
    # 119,900, rounded down as the track bound's 96,000 was. That 96,000 -
    # 0.9 of the track bound's 104.2 KB/s (docs/reports/VIDDISK-ST225-
    # 2026-09-27.md; 102.3 re-measured beside the cylinder run) - is the
    # figure for a kernel built NOHDCYL=1 or older than build 458:
    # `--disk 96000` for a file that must play there
    "5150-st225": dict(disk=119000, avg=0.50, peak=0.85, owe=1.6, speed=1,
                       ring=8,
                       # VIDDISK's rows over its 50% one (the report), and
                       # at 100% extrapolated along its last two
                       disk_at=((0.0, 134.8 / 130.1), (0.25, 131.5 / 130.1),
                                (0.5, 1.0), (0.75, 113.9 / 130.1),
                                (1.0, 97.7 / 130.1)),
                       rate=11025, audio="pcm8",
                       what="the owner's 5150: ST-225 on an ST11M, DMA, "
                            "measured at 130 KB/s under a half-machine "
                            "decode with cylinder reads (an older kernel: "
                            "--disk 96000)"),
    # MEASURED on MartyPC's XT-IDE, the CPU copying the disk, so it gets
    # what the decode leaves. Under the CYLINDER bound (SPEC.md 18.91.5):
    # 208.8 KB/s idle, 159.9 / 104.2 / 51.3 at 25 / 50 / 75%; the track
    # bound read 200.3 / 150.2 / 99.1 / 50.0 beside it - VIDEO-PLAN W3's
    # 198 / 150 / 99 / 49, reproduced - and was 91,000. 90% of the 50% row,
    # as the ST-225's: 0.9 x 104.2 KB x 1024 = 96,031. Small, because an
    # XT-IDE drive walks its own geometry with no rotation to save here; a
    # real one with a slower drive behind it saves more per call
    "5150-xtide": dict(disk=96000, avg=0.50, peak=0.85, owe=1.6, speed=1,
                       ring=8,
                       disk_at=((0.0, 208.8 / 104.2), (0.25, 159.9 / 104.2),
                                (0.5, 1.0), (0.75, 51.3 / 104.2), (1.0, 0.0)),
                       rate=11025, audio="pcm8",
                       what="a 5150 with an XT-IDE: the CPU copies the "
                            "disk, so it streams what the decode leaves "
                            "(measured on MartyPC)"),
    "5150-picomem2": dict(disk=150000, avg=0.40, peak=0.80, owe=1.6,
                          ring=8,
                          speed=1, rate=22050, audio="pcm8",
                          what="a 5150 with a PicoMEM 2: fast storage the "
                               "CPU copies, so a lower CPU share "
                               "(predicted)"),
    "floppy": dict(disk=15000, avg=0.50, peak=0.85, owe=1.6, speed=1,
                   ring=8,
                   rate=5512, audio="pcm8",
                   what="a 360 KB floppy, a cylinder a call (predicted)"),
    "286-slow": dict(disk=150000, avg=1.50, peak=2.50, owe=1.6, speed=3,
                ring=8,
                rate=22050, audio="pcm8", spk_us=(23.0, 13.0),
                what="a slow 286, 6-8 MHz: ~3x the 8088's cycles (predicted - "
                     "a stand-in no machine has measured)"),
    # MEASURED, both of them (docs/reports/VIDEO-86BOX-486-2026-10-07.md):
    # `speed` is VIDBENCH's frames decoded on the machine against this
    # model's cycles for them, taken on the frames heavy in bytes, which are
    # the ones a budget binds on. The 286 is 4.4-4.6x there (3.4-3.7 on
    # runs) and was 6, which let a frame the model put at 83% of its period
    # take ~110% - the heavy stretches the owner saw it fall behind on.
    # `lcopy_us` is the flipped player's latch copy, a byte: VIDBENCH's
    # latch row. A screen READ is slow on both cards (~1 us a byte, the
    # 486's no faster than the 286's), so the copy costs the 486 more of its
    # time than the 286. `cyc_us` is the machine's own decode (98.2.3.3):
    # with it `speed` only turns microseconds into model cycles, and runs
    # and pixels are each priced at what the machine does with them
    # The disk is 0.9 x VIDDISK's 50% row (684.7 KB/s), the 5150's and the
    # 486's rule, with its other rows as the curve. It was 400,000, a figure
    # from before the IDE was measured, and it was the one that bound: the
    # owner's lossless Last Exile, which plays on this machine with nothing
    # late, re-encoded under it cut 451 frames by the disk and none by the
    # CPU (98.2.3.4). MEASURED UNDER THE TRACK BOUND, and not re-measured
    # under the cylinder one (SPEC.md 18.91.5): with 86Box's 63 sectors a
    # track a 32 KB read is ~2 int 13h calls either way less one, and a
    # disk the CPU copies pays little a call - so this stays until a
    # VIDDISK run on build 458 or later says otherwise (the owner's own
    # VIDDISK286.TXT of 2026-10-07, track-bound, read 665.5 at 50%)
    "286-speeddemon": dict(disk=616000, avg=2.25, peak=3.75, owe=1.6, speed=4.5,
                    ring=8, xcopy=0.345, lcopy_us=1.829, cyc_us=CYC_US_286,
                    **XPLAY_286,
                    disk_at=((0.0, 1318.3 / 684.7), (0.25, 1023.9 / 684.7),
                             (0.5, 1.0), (0.75, 348.1 / 684.7), (1.0, 0.0)),
                    rate=22050, audio="pcm8", spk_us=(11.5, 6.5),
                    what="a 286 at the top of its class - RAM with no wait "
                         "states and a fast IDE disk: the VGA's bus binds, "
                         "and it decodes ~4.5x as fast as the 5150's CGA; "
                         "the disk 685 KB/s with half the period decoding "
                         "(measured on 86Box's mr286). An ST11R there: "
                         "--disk 250000"),
    # THE OWNER'S REAL 286 (2026-10-08): the decode above, its latch row
    # (17.06 ms an 8,000 bytes), and its disk - a nearly full, fragmented
    # 2.5-inch IDE drive - at 0.9 x VIDDISK's 50% row (486.3 KB/s), its
    # other rows the curve. Not CPU-bound like the mr286's: the drive reads
    # on while the CPU is busy, so the curve holds up to 25% and falls past
    # it. The shares are the mr286's - a profile with `cyc_us` prices in
    # microseconds, so `speed` is only the unit they are written in
    "286-fast": dict(disk=448000, avg=2.25, peak=3.75, owe=1.6, speed=4.5,
                     ring=8, xcopy=0.345, lcopy_us=2.133, cyc_us=CYC_US_286PVGA,
                     **XPLAY_VICTORY, **PLAY_VICTORY,
                     disk_at=((0.0, 729.5 / 486.3), (0.25, 684.7 / 486.3),
                              (0.5, 1.0), (0.75, 294.3 / 486.3), (1.0, 0.0)),
                     rate=22050, audio="pcm8", spk_us=(11.5, 6.5),
                     what="a fast 286, 16 MHz, with VGA on the board and an "
                          "IDE disk: decodes 12-29% slower than "
                          "286-speeddemon (its RAM has wait states), the "
                          "disk 486 KB/s with half the period decoding "
                          "(measured on a real one - the owner's AMD 286 "
                          "with a PVGA1A - by VIDBENCH and VIDDISK)"),
    # THE SAME 286 WITH ITS DISK ON A PICOMEM 1.x (2026-10-12, docs/reports/
    # VIDDISK-PICOMEM1-286-2026-10-12.md): the Packard Bell Victory, which is
    # 286-fast's machine, streaming off a .vhd the card serves. Its decode is
    # 286-fast's table - VIDBENCH again read the raw rows within 0.2% and
    # every frame the table was fitted to at or under its old figure (P1 to
    # P6 4-12% under, slices and runs within 1%), so the table is the
    # conservative reading - and so are the latch row and the XMS copy (X
    # read 11,301 us a 32 KB copy, the 10-08 figure to the microsecond). The
    # disk is the CPU's to copy, EXACTLY: 691.1 KB/s idle and 518.3 / 345.5 /
    # 172.7 at 25 / 50 / 75%, the hook's share taken straight off the top,
    # interrupts on or off alike.
    #
    # THE DISK IS THE MULTI-SECTOR FIRMWARE'S (docs/reports/
    # VIDDISK-PICOMEM1-286-MULTI-2026-10-12.md): the card reads a whole
    # int 13h call off the SD card in runs of 1, 2 then 4 sectors and
    # reads each next run while the CPU copies the last, where the stock
    # 1.x firmware reads a sector, waits for the copy, reads the next.
    # 755.1 idle and 575.9 / 379.8 / 189.9 at 25 / 50 / 75%, +9.9% on
    # stock and 96.6% of what the bus allows: the copy is 1.27 us a byte
    # (PMDMA K), and the card's memory reads at exactly the speed of the
    # VGA's (PMDMA I), so the bus binds and not the card. On the stock
    # firmware this profile is 9% optimistic. 0.9 x the 50% row, the
    # rule: 350,000.
    #
    # REMODELLED AT 8 WAIT STATES (2026-10-15, docs/reports/
    # VIDPM-VICTORY286-2026-10-15.md): the machine's ISA bus at the BIOS's
    # "Tune" setting, which is how it runs now. The card's disk is the bus's
    # to cross, so it moved most: 1,119.9 KB/s idle and 838.3 / 563.1 /
    # 281.5 at 25 / 50 / 75% - the same exact line, 48% higher - against
    # 755.1 / 377.5 at 14 the same day (the multi-sector firmware's 10-12
    # figures to 0.6%). 0.9 x 563.1 KB: 519,000. The decode is
    # CYC_US_286PVGA8, the latch copy 15,730 us an 8,000 bytes. The XMS copy
    # is NOT a bus figure - 14 and 8 read the same on one boot - and it has
    # read 0.214, 0.257 and 0.337 ms a KB on three boots, so xcopy stays the
    # slow boot's until the boots are understood.
    # PEAK 5.4 (120% of a period), the owner's (2026-10-16): with the play's
    # profile in the model (PLAY_VICTORY) and --lag 100 ms the CPU's ceiling
    # was the cut that looked worst - Bad Carrot at 5.4 cut 48 frames and
    # fell 83 ms behind at most against 55 and 0 at 3.75 and read smoother on
    # the Victory, and the whole frame's share has been 123-126% on every
    # profiled play with no harm (SPEC.md 98.3.22.1)
    "286-picomem1": dict(disk=519000, avg=2.25, peak=5.4, owe=1.6, speed=4.5,
                         ring=8, xcopy=0.345, lcopy_us=1.966,
                         cyc_us=CYC_US_286PVGA8, **XPLAY_VICTORY,
                         **PLAY_VICTORY,
                         disk_at=((0.0, 1119.9 / 563.1),
                                  (0.25, 838.3 / 563.1),
                                  (0.5, 1.0), (0.75, 281.5 / 563.1),
                                  (1.0, 0.0)),
                         rate=22050, audio="pcm8", spk_us=(11.5, 6.5),
                         what="286-fast's machine with its disk on a PicoMEM "
                              "1.x, the bus at 8 wait states: the CPU "
                              "copies every byte, so the disk is 1,120 KB/s "
                              "idle and 563 with half the period decoding "
                              "(measured on the owner's Packard Bell "
                              "Victory 286, on the card's multi-sector "
                              "firmware)"),
    # VIDDISK's ceiling at its 50% row, 2,192 KB/s, x 0.9 - the 5150's
    # rule - and its other rows as the curve: an IDE disk the CPU copies.
    # Track-bound, like 286-speeddemon's, and for the same reason left as it is
    # (SPEC.md 18.91.5): this machine's disk is not what binds it.
    # The "what" says 10x: that is its slices; its runs are 8x, which is
    # the table's to price, not speed's
    # avg / peak: the owner's 86Box DX2/66 played 70% / 105% flipped with
    # nothing seen wrong and 80% / 115% with late frames counted (98.2.3.6);
    # these sit under that boundary, a real machine being slower or faster.
    # XMS: a 386 or better plays IN PLACE now (SPEC.md 98.3.18.14) through a
    # base loaded by a moment of protected mode - xbor_us ESTIMATED at 6 us
    # from its instructions (four CMOS port accesses most of it; nothing has
    # timed one on iron, docs/plans/VIDEO-386-PLAN.md section M). xup and
    # xdown stay the unmeasured 0.05: the copy up is none at all where the
    # disk is HDD.DRV's rung 1 (98.3.18.15), which an encode cannot know
    "486-dx2-66": dict(disk=1950000, avg=6.50, peak=10.0, owe=1.6, speed=10, xcopy=0.05,
                xplay="inplace", xup=0.05, xdown=0.05, xbor_us=6.0,
                xbor_n=3, xyield=2048,
                ring=8, lcopy_us=1.380, cyc_us=CYC_US_486,
                disk_at=((0.0, 4151 / 2192.0), (0.25, 3157 / 2192.0),
                         (0.5, 1.0), (0.75, 1096 / 2192.0), (1.0, 0.0)),
                rate=22050, audio="pcm8", spk_us=(6.0, 3.5),
                # WHAT IT IMPLIES besides (98.2.3.5): the whole picture and
                # 48 KB frames, which the owner's 486 plays (LX48F, 239 of
                # 239 drawn) - a 256-colour preset's 2x1 and the 32 KB cap
                # are the 286's and the 5150's economies
                defaults=dict(detail="1x1", frame_cap="48"),
                what="a 486DX2/66 with an ISA VGA and IDE: the card's bus "
                     "binds, ~10x the 5150's decode (8x on runs, 11x on "
                     "pixels); the disk 2,192 KB/s with half the period "
                     "decoding (measured on 86Box)"),
    "lossless": dict(disk=None, avg=None, peak=None, owe=None, speed=1,
                     ring=None,
                     rate=22050, audio="pcm8", what="no limits: every change, exactly"),
}

# the box a canvas is fitted into, by name (VIDEO-PLAN 2.3)
PRESETS = {
    "cga": ("cga", 640, 200),
    "cga-small": ("cga", 320, 100),
    "herc": ("herc", 400, 200),
    "herc-mid": ("herc", 480, 232),
    "herc-full": ("herc", 720, 348),
    # THE PC SPEAKER on a 5150 (98.2.15.4): the owner's settings off the
    # listens and the 86Box encodes - 8 kHz, which the speaker takes ~78% of
    # the machine to play, leaves the picture this box at 23 fps
    "herc-spk": ("herc", 360, 160),
    "cga-spk": ("cga", 360, 160),
    "vga": ("lin80", 320, 240),
    "vga-mid": ("lin80", 400, 300),
    "vga-full": ("lin80", 640, 480),
    # 256 colours in mode 13h (SPEC.md 98.1.2's LIN320), full screen only
    "vga8": ("lin320", 320, 200),
    "vga8-small": ("lin320", 160, 100),
    # ...and in Mode X (98.1.3.1): square pixels, and a byte four of them
    # where a run of one colour covers an aligned group
    "modex": ("modex", 320, 240),
    # 16 colours in mode 12h (98.1.3.2): the desktop's own mode, so it plays
    # IN THE WINDOW on a VGA desktop at up to ~620 x 400, full screen above
    "vga4": ("lin80", 320, 240),
    "vga4-mid": ("lin80", 400, 300),
    "vga4-full": ("lin80", 640, 480),
    "modex-small": ("modex", 160, 120),
    # CGA in colour (98.1.3.3), full screen only: 4 colours in mode 4 (the
    # cga layout at two bits a pixel), 16 in the 160 x 100 text hack
    "cga4": ("cga", 320, 200),
    "cga4-small": ("cga", 160, 100),
    "c160": ("c160", 160, 100),
    # ...and on its COMPOSITE output (98.1.3.5): 80 x 100 CELLS, each one of
    # 512 codes, full screen on a CGA only
    "c512": ("text-80x100", 80, 100),
    # ...and TEXT (98.1.3.6): the 80 x 25 text screen every adapter has, the
    # picture made of its characters - in colour on a CGA, an EGA or a VGA,
    # or in the three attributes an MDA and a Hercules draw too
    "text": ("text-80x25", 80, 25),
    "text-mono": ("text-80x25", 80, 25),
    # Live windowed's sizes (VIDEO-PLAN 3.4): small canvases a worker blits
    "live-cga": ("cga", 320, 100),
    "live-herc": ("herc", 240, 116),
    "live-vga": ("lin80", 160, 120),
}

LIVE_BOX = {"cga": (320, 112), "herc": (360, 144), "vga": (320, 200)}
LIVE_ASPECT = {"cga": vid.ASPECT[vid.LAY_CGA],
               "herc": vid.ASPECT[vid.LAY_HERC], "vga": (1, 1)}
PRESET_PIXFMT = {"cga4": "cga4", "cga4-small": "cga4", "c160": "c160",
                 "c512": "c512", "text": "text", "text-mono": "text",
                 "vga4": "vga4", "vga4-mid": "vga4", "vga4-full": "vga4"}
# ...and what a preset sets beyond its box (98.2.10): the owner's settings
# for 256 colours, both modes, found encoding an anime opening - 25 fps
# where the format's own default is 15, and every pixel doubled along its
# row, which halves the bytes and the decode and is invisible at 320 wide
PRESET_DEFAULTS = {"vga8": dict(fps=25.0, detail="2x1"),
                   "modex": dict(fps=25.0, detail="2x1"),
                   # TEXT at 30: a whole picture is 4,000 bytes, so a
                   # full frame rate costs a text clip little (98.2.16)
                   "text": dict(fps=30.0, text_colour="colour"),
                   "text-mono": dict(fps=30.0, text_colour="mono"),
                   # the speaker's two: the SOUND is the preset's too, or
                   # a profile's card sound would take it back (98.2.15.4)
                   "herc-spk": dict(fps=23.0, audio="speaker", rate=8000),
                   "cga-spk": dict(fps=23.0, audio="speaker", rate=8000)}


# WHAT EACH CHOICE IS (98.2.8): a line per value of every option that takes
# one of a list, which the encoder's window shows behind a "?" beside the
# field. tests/vencguitest.py fails if a choice has no line here
CHOICE_HELP = {
    "preset": {
        "cga": "CGA, black and white: 640 x 200, mode 6. In the window on "
               "a CGA desktop",
        "cga-small": "CGA, black and white, a quarter of the screen: "
                     "320 x 100. Half the bytes and CPU",
        "cga4": "CGA, 4 colours: 320 x 200 in mode 4, one palette for the "
                "whole clip. Full screen, on a CGA, EGA or VGA",
        "cga4-small": "CGA, 4 colours, a quarter of the screen: 160 x 100",
        "c160": "CGA, 16 colours at 160 x 100, on CGA's text-mode trick "
                "(each character cell two pixels). Full screen, CGA or VGA",
        "c512": "CGA on a COMPOSITE monitor, ~450 colours: 80 x 100 cells "
                "of the text-mode trick. Full screen, a real CGA only",
        "text": "TEXT VIDEO in colour: the 80 x 25 text screen, the "
                "picture drawn in characters, 16 colours on 16. Full "
                "screen on a CGA, EGA or VGA",
        "text-mono": "TEXT VIDEO in black and white: the 80 x 25 text "
                     "screen in grey, white and reverse, which every "
                     "adapter draws alike - Hercules and MDA too. Full "
                     "screen",
        "herc": "Hercules, black and white: 400 x 200 of its 720 x 348. "
                "In the window on a Hercules desktop",
        "herc-mid": "Hercules, black and white, bigger: 480 x 232",
        "herc-spk": "Hercules, black and white, with the sound on the PC "
                    "SPEAKER: 360 x 160 at 23 fps, 8 kHz - what a 5150 "
                    "with no card has left once the speaker has its ~78%",
        "cga-spk": "CGA, black and white, with the sound on the PC SPEAKER: "
                   "360 x 160 at 23 fps, 8 kHz (herc-spk's, on a CGA)",
        "herc-full": "Hercules, black and white, the whole screen: "
                     "720 x 348",
        "vga": "VGA, black and white: 320 x 240 in mode 12h. In the window "
               "on a VGA desktop",
        "vga-mid": "VGA, black and white, bigger: 400 x 300",
        "vga-full": "VGA, black and white, the whole screen: 640 x 480",
        "vga4": "VGA, 16 colours: 320 x 240 in mode 12h, the desktop's own "
                "mode, so it plays in the window",
        "vga4-mid": "VGA, 16 colours, bigger: 400 x 300",
        "vga4-full": "VGA, 16 colours, the whole screen: 640 x 480",
        "vga8": "VGA, 256 colours: 320 x 200 in mode 13h. Full screen, "
                "25 fps, each pixel doubled along its row",
        "vga8-small": "VGA, 256 colours, a quarter of the screen: 160 x 100",
        "modex": "VGA, 256 colours in Mode X: 320 x 240, square pixels. "
                 "Full screen",
        "modex-small": "VGA, 256 colours in Mode X, a quarter: 160 x 120",
        "live-cga": "LIVE on a CGA desktop: a small clip loaded whole that "
                    "plays in its window beside other programs",
        "live-herc": "LIVE on a Hercules desktop: small, loaded whole, "
                     "plays in its window",
        "live-vga": "LIVE on a VGA desktop: small, loaded whole, plays in "
                    "its window",
    },
    "layout": {
        "cga": "CGA's graphics memory: 640 x 200, two banks of rows (mode "
               "6, and mode 4 for 4 colours)",
        "herc": "Hercules graphics memory: 720 x 348, four banks of rows",
        "lin80": "VGA mode 12h: 640 x 480, one row after another, 80 bytes "
                 "a row",
        "lin320": "VGA mode 13h: 320 x 200, a byte a pixel (256 colours)",
        "modex": "VGA Mode X: 320 x 240, a byte a pixel across four planes "
                 "(256 colours)",
        "c160": "CGA's text mode squeezed to 100 rows, only the "
                "attribute bytes: 160 x 100 in 16 colours",
        "text-80x100": "CGA's text mode squeezed to 100 rows, character "
                       "and attribute: 80 x 100 cells, the composite "
                       "512-colour format",
        "text-80x25": "The ordinary 80 x 25 text screen every adapter has, "
                      "character and attribute: text video",
    },
    "pixfmt": {
        "mono": "Black and white, one bit a pixel - every graphics layout",
        "cgacomp": "Composite colour: one-bit patterns a CGA's composite "
                   "output shows as 16 colours",
        "vga8": "256 colours from a palette made for the clip (lin320 or "
                "modex)",
        "vga4": "The VGA's 16 standard colours, on mode 12h (lin80)",
        "cga4": "4 colours in CGA mode 4 (cga)",
        "c160": "16 colours at 160 x 100 on the text-mode trick (c160)",
        "c512": "~450 composite colours on the text-mode trick "
                "(text-80x100)",
        "text": "Characters on the 80 x 25 text screen, colour or mono "
                "(text-80x25)",
    },
    "text_colour": {
        "colour": "16 foregrounds on 16 backgrounds: a CGA, EGA or VGA",
        "mono": "Grey, white and reverse only: plays on every adapter, "
                "Hercules and MDA too",
    },
    "text_glyphs": {
        "blocks": "Letters, the four shades and the four half blocks - the "
                  "clearest picture (the half blocks double the rows)",
        "shades": "Letters and the four shades, no half blocks",
        "ascii": "Printable ASCII only: the classic ASCII-art look, less "
                 "clear",
        "dots": "Full and half blocks for the shapes, and the dots , . ' ` "
                "for the edges and dithers a block is too coarse for - the "
                "other classic text-art style",
        "dots-plus": "The dots set with : ; \" * as well - more marks for "
                     "the middle tones, a little busier",
        "blocks-only": "No letters: the full block and the four half "
                       "blocks alone - flat colour, two dots a cell",
        "blocks-only-shade": "No letters: the four shades and the four half "
                             "blocks - the blocks-only look with tones "
                             "between",
    },
    "fit": {
        "fit": "The whole picture; the canvas shrinks to its shape, no bars",
        "fill": "The whole box; the picture's edges cropped",
        "stretch": "The whole box and the whole picture, distorted",
    },
    "profile": {
        "5150-st225": "IBM 5150/XT with a Seagate ST-225 hard disk: "
                      "119 KB/s, half the CPU on average (the default; "
                      "an older kernel's track reads want --disk 96000)",
        "5150-xtide": "IBM 5150/XT with an XT-IDE, or any disk the CPU "
                      "copies for: 96 KB/s at half the machine, and less "
                      "as the picture and the speaker take more of it - "
                      "the copy is charged",
        "5150-picomem2": "IBM 5150/XT with a PicoMEM 2: 150 KB/s, 22 kHz "
                         "sound (predicted)",
        "floppy": "Played off a floppy: 15 KB/s, 5.5 kHz sound - small and "
                  "slow",
        "286-slow": "A slow 286 with a hard disk: 150 KB/s, three 8088s of "
                    "CPU (predicted - a stand-in no machine has measured)",
        "286-speeddemon": "The fastest 286: no RAM wait states and a fast "
                          "IDE disk - 600 KB/s, four and a half 8088s of "
                          "CPU (measured on 86Box)",
        "286-fast": "A fast 16 MHz 286 with VGA on the board: 448 KB/s off "
                    "an IDE disk, the decode measured on a real one "
                    "(12-29% slower than 286-speeddemon)",
        "286-picomem1": "A fast 16 MHz 286 with its disk on a PicoMEM "
                        "1.x and the bus at 8 wait states: 519 KB/s, "
                        "falling as the decode takes more - the CPU copies "
                        "the disk (measured on a real one)",
        "486-dx2-66": "A 486DX2/66 with an ISA VGA and IDE: 1.9 MB/s, ten 8088s "
               "of CPU, 65% of it on average - Mode X, the whole picture "
               "and 48 KB frames",
        "lossless": "No budget: every change kept, whatever it costs",
    },
    "aim": {
        "asked": "Encode what was asked for, nothing more (the default)",
        "quality": "Spend budget the video leaves unused on a bigger "
                   "picture and better sound",
        "size": "Leave out changes not worth their bytes: the smallest "
                "file for the look",
    },
    "desktop_posters": {
        "auto": "A picture of each keyframe stored for the desktop's "
                "Preview where the player's own would be poor: in colour "
                "for 256 colours, for 16 in colours of their own or too "
                "big for the window, and for CGA's colours; and in black "
                "and white too for CGA's, for a CGA's desktop (the "
                "default)",
        "colour": "Each keyframe's Preview picture stored in the "
                  "desktop's 16 colours",
        "mono": "Each keyframe's Preview picture stored in black and "
                "white, as a CGA or Hercules desktop shows it",
        "both": "Each keyframe's Preview picture stored twice: in colour "
                "for a VGA or EGA desktop, black and white for the others",
        "off": "No keyframe pictures: the Preview decodes each keyframe "
               "(a custom poster is still stored)",
    },
    "desktop_size": {
        "fit": "The Preview as big as the window has room for (the "
               "default)",
        "half": "The Preview at half the video's size, however much room "
                "there is. Play then goes full screen",
        "quarter": "The Preview at a quarter of the video's size. Play then "
                   "goes full screen",
    },
    "flip_hold": {
        "ahead": "With Min fps on a flipped file, a held frame is drawn on "
                 "the hidden page toward the next one, which finishes it "
                 "(the default; needs this player)",
        "empty": "With Min fps on a flipped file, a held frame draws "
                 "nothing and the picture before is shown again (plays on "
                 "an older player)",
    },
    "error": {
        "visible": "When a frame is cut, judge what is wrong as the eye "
                   "sees it (the default)",
        "bits": "When a frame is cut, count wrong bits plainly",
    },
    "blocks": {
        "off": "A version 1 file, which the player plays",
        "on": "A version 2 file (98.1.10): a cut frame may also draw small "
              "blocks - a dithered fill or two colours on 16 colours, "
              "Video-1's three kinds on 256, a codebook tile on one bit - "
              "for a better picture at the same disk, at more decode. "
              "VIDEO.O88 plays them all (98.3.23); not with Live, "
              "resident, bands, a tear or one bit's loop (98.1.10.1)",
    },
    "vga4_order": {
        "dark": "Each scene's sixteen darkest first: the old picture shown "
                "in the new colours at a cut keeps its lightness order",
        "planes": "Each colour in the slot that makes the scene's own "
                  "changes write fewest planes - the same picture, fewer "
                  "bytes and less decode - with no cut moving more pixels "
                  "far at its palette switch than darkest first would. A "
                  "second pass of the dither",
    },
    "page_ahead": {
        "auto": "Mode X and 16 colours on a screen of their own: build each "
                "scene change ahead on the second page and flip to it - kept "
                "only if the picture comes out better and nowhere worse "
                "(the default; twice the encode time there)",
        "on": "Always build ahead, a Hercules too - which then plays only "
              "on a Hercules with no colour card beside it",
        "off": "Never: one page, every frame drawn in place",
    },
    "pages": {
        "2": "One hidden page: a scene change at a time built ahead "
             "(every player since page-ahead plays it)",
        "3": "Two hidden pages: two scene changes close together both "
             "built in the calm before them - Mode X has three pages",
        "4": "Three hidden pages - Mode X at 320 x 200 and 16 colours at "
             "320 x 200 have four",
        "5": "Four hidden pages (a screen with that many)",
        "6": "Five hidden pages (a screen with that many)",
        "7": "Six hidden pages (a screen with that many)",
        "8": "Seven hidden pages (a screen with that many)",
        "max": "Every page the screen holds (the default; two for a "
               "layered file) - the first frames of a burst "
               "built ahead too; a player before N pages refuses the file",
    },
    "page_keys": {
        "all": "Each key carries every page as it stands: no build is "
               "redone, and a seek reads a picture per page (the default)",
        "started": "Each key carries only the pages whose build has begun; "
                   "the rest are rebuilt whole after it - smaller keys and "
                   "faster seeks, for stream bytes the spare pays",
    },
    "key_pool": {
        "on": "A page several keys carry is stored once, in the file's page "
              "pool, and each key names it - far smaller keys; a seek reads "
              "a picture per page named (the default)",
        "off": "Each key carries its own copy of every page, as before the "
               "pool: one read a key part, and a player before the pool "
               "plays the file",
    },
    "page_prune": {
        "rescue": "Where auto would throw page-ahead away, try once more "
                  "without only the builds that made their own stretch "
                  "worse (the default; a third pass on those files alone)",
        "always": "Wherever a build made its own stretch worse, try once "
                  "more without it, and keep whichever file is better - a "
                  "third pass on most files that page-ahead",
        "off": "Keep or drop page-ahead whole",
    },
    "cut": {
        "rank": "When a frame is cut, keep the changes worth most, and "
                "leave out what the look-ahead says is about to change",
        "fill": "As rank, then spend the room left on the changes the "
                "look-ahead left out - fewer rows left behind on a fast "
                "pan, and a cut frame as big as it may be (the default)",
        "tear": "Keep whole rows top to bottom, carrying on "
                "next cut from where this one stopped - one tear line "
                "instead of jagged rows, like a frame drawn without vsync",
    },
    "frame_cap": {
        "32": "A frame's record is 32 KB at most: every player plays it "
              "(the default)",
        "48": "48 KB - a fast pan is cut less; needs this player or "
              "later, 32 KB more memory, and a machine that reads and draws "
              "48 KB in a frame's period (the 486 profile's default)",
        "63.5": "63.5 KB - a whole 320 x 180 Mode X frame "
                "fits; needs this player or later, 32 KB more memory, and a "
                "486-class machine",
    },
    "audio": {
        "pcm8": "8-bit sound for a Sound Blaster",
        "adpcm4": "Sound Blaster 4-bit ADPCM: half the bytes, noisier",
        "speaker": "The PC speaker, for a machine with no sound card: "
                   "about half an 8088 at the default 5.5 kHz, and about "
                   "78% of one at the 8 kHz the herc-spk and cga-spk "
                   "presets choose",
        "none": "Silent",
    },
    "buffer_sound_ahead": {
        "auto": "Ahead when this encode needs it - a burst the disk "
                "budget had to cut for the sound's sake, or a lossless "
                "encode with no disk to judge by - else in step",
        "on": "Always carry the sound ahead: the card keeps playing "
              "through a disk burst, for a little disk space at every "
              "keyframe",
        "off": "The sound in step with its picture, as files were made "
               "before: no extra space, and a burst can pause the sound",
    },
    "adpcm": {
        "search": "Search for the best ADPCM stream (~7 dB better, slower)",
        "greedy": "A nibble at a time: fast",
    },
    "spk_shape": {
        "encoder": "Shaped here, for the PC speaker: what its cone cannot "
                   "play cut, the level evened out and driven loud enough "
                   "to be heard over its whine",
        "machine": "A speaker WAV only: a plain 8-bit WAV at the rate, "
                   "shaped by Audio on the machine as it plays",
        "none": "Unshaped - the sound as it is, for comparison: most of it "
                "lands under the whine on a real 5150",
    },
    "spk_style": {
        "lifted": "Quiet passages raised, so a soft intro is heard",
        "natural": "More of the song's own rise and fall: a soft passage "
                   "stays soft (the default)",
    },
    "spk_pulses": {
        "1": "One pulse a sample: the whine is at the sound's rate",
        "2": "Two a sample: the whine at twice the rate, much quieter to "
             "the ear - ~96% of a 4.77 MHz 8088, so for a 286 or better",
    },
    "dither": {
        "bayer": "An ordered 8 x 8 pattern: steady, compresses best",
        "bluenoise": "A noise pattern: no visible grid, ~8% more data",
        "threshold": "Plain black or white at half grey: for clips that are "
                     "black and white already",
    },
    "screen": {
        "640x480": "Mode 12h, the desktop's own: one page, and it plays in "
                   "the window (the default)",
        "320x200": "Mode 0Dh, 320 x 200: full screen, two pages - or for "
                   "Mode X, 13h's 400 lines, four pages where 320 x 240 has "
                   "three",
        "320x240": "320 x 240, square pixels: full screen, two pages",
        "640x350": "640 x 350, mode 10h's lines: full screen, two pages",
        "640x400": "640 x 400: full screen, two pages",
        "320x200n": "320 x 200 NARROW: full screen, eight pages - for "
                    "--pages; no --flip (98.1.3.2.5)",
        "320x240n": "320 x 240 NARROW: full screen, six pages - for "
                    "--pages; no --flip (98.1.3.2.5)",
    },
    "palette": dict([("auto", "The format's own colours: the clip's 256 "
                      "for 256-colour VGA, the EGA's sixteen for 16 "
                      "(the default)")]
                    + [("grey%d" % n, "%d greys, black to white%s" % (
                        n, ": black and white" if n == 2 else ""))
                       for n in (2, 4, 8, 16, 32, 64)]
                    + [("clip%d" % n, "The clip's own %d colours" % n)
                       for n in (2, 4, 8, 16, 32, 64, 128)]
                    + [("colour%d" % n, "The clip's own %d, chosen for "
                        "COLOUR: a small saturated part of the picture (a "
                        "headlight, a sky) gets a colour of its own, where "
                        "the clip's %d spend them on the greys most of it "
                        "is%s" % (n, n, "" if n == 16 else
                                  ". Fewer colours change less: fewer bytes "
                                  "and less CPU, at the colours' cost"))
                       for n in (8, 12, 16)]),
    "vga4_wm3": {
        "on": "No wrong colour flashes on a VGA: the bytes that would show "
              "one are written in one store, at some cost in decode time",
        "off": "Every byte written plane by plane: cheaper to decode, and a "
               "refresh can catch a colour neither frame has",
    },
    "step_min": {
        "8": "A scene that breaks badly may go as far as its own 8",
        "12": "No scene below its own 12 colours",
    },
    "step_down": {
        "off": "Cut frames where a scene is too heavy for the machine "
               "(the default was this until 2026-10-14)",
        "on": "Give a scene that breaks its own 12 colours, or 8 where most "
              "of it broke, before cutting frames - only where that asks "
              "for less and keeps the scene's colours",
    },
    "levels": {
        "auto": "Stretch the greys the clip uses to full black-to-white",
        "none": "Keep the greys as they are",
    },
    "cga_palette": {
        "0": "Green, red and brown",
        "1": "Cyan, magenta and white",
        "2": "Cyan, red and white (mode 5's)",
    },
    "cga_bright": {"0": "The set dim", "1": "The set bright"},
    "cga_bg": {str(i): n for i, n in enumerate((
        "Black", "Blue", "Green", "Cyan", "Red", "Magenta", "Brown",
        "Light grey", "Dark grey", "Light blue", "Light green", "Light cyan",
        "Light red", "Light magenta", "Yellow", "White"))},
    "cga_card": {
        "old": "IBM's original CGA",
        "new": "IBM's 1985 CGA, whose composite colours differ",
        "both": "Each cell chosen to look right on either card (the "
                "default)",
    },
    "comp_dither": {
        "diffuse": "Error diffusion through the composite model (the "
                   "default)",
        "pattern": "An ordered pattern: steadier, less exact",
    },
    "levels_mix": {
        "4": "2 x 2 patterns: calmer and cheaper (the default)",
        "16": "4 x 4 patterns: more colours, more grain",
    },
    "live": {
        "cga": "Live on a CGA desktop",
        "herc": "Live on a Hercules desktop",
        "vga": "Live on a VGA desktop",
    },
    "layer_passes": {
        "two": "A first pass works out the bank the layer's machine keeps "
               "for the base, and the layer is made knowing it: about half "
               "again the encode's time, once (the default)",
        "one": "Learned as the encode goes: no extra time, but a layer "
               "record made before it knew may arrive too late to be drawn "
               "- and a skipped one leaves the last one's pixels standing",
    },
}
# the layer's machine is a profile like the base's: the same lines
CHOICE_HELP["layer_profile"] = CHOICE_HELP["profile"]


def implied(preset=None, pixfmt=None, profile="5150-st225", live=None,
            sfps=None):
    """WHAT A CHOICE SETS (98.2.10): the options a preset, a pixel format,
    a profile and --live imply, as the encoder will use them - dest ->
    string, "" where nothing is set. encode() takes its defaults from here,
    and the encoder's window fills its fields from it and leaves off its
    command line whatever still equals it. `sfps` is the source's frame
    rate, which caps the default one"""
    out = {}
    lay, bw, bh = PRESETS[preset] if preset else (None, None, None)
    if live:                    # one bit, or VGA4 on VGA (98.3.10.4)
        pixfmt = "vga4" if live == "vga" and pixfmt == "vga4" else "mono"
        lay = "lin80"
        bw, bh = LIVE_BOX[live]
        out["resident"] = "1"
    if not pixfmt and preset in PRESET_PIXFMT:
        pixfmt = PRESET_PIXFMT[preset]
    vga8 = lay in ("lin320", "modex")
    if not pixfmt and lay:
        pixfmt = "vga8" if vga8 else "c160" if lay == "c160" else \
            "c512" if lay == "text-80x100" else \
            "text" if lay == "text-80x25" else "mono"
    d = PRESET_DEFAULTS.get(preset, {}) if not live else {}
    fps = d.get("fps", 15.0 if pixfmt == "vga8" else 30.0)
    if sfps:
        fps = min(fps, sfps)
    prof = PROFILES[profile or "5150-st225"]
    pdef = {} if live else prof.get("defaults", {})
    # a whole number as one, never in an exponent: %g wrote the 486's
    # 1,950,000 B/s as 1.95e+06
    num = lambda v: "" if v is None else (
        "%d" % v if float(v).is_integer() else "%g" % v)
    out["text_colour"] = d.get("text_colour", "colour") \
        if pixfmt == "text" else ""
    out.update(layout=lay or "", box="%dx%d" % (bw, bh) if bw else "",
               pixfmt=pixfmt or "", fps=num(fps),
               detail=pdef["detail"] if pixfmt == "vga8" and
               "detail" in pdef else d.get("detail", "1x1"),
               frame_cap=pdef.get("frame_cap", "32"),
               disk=num(prof["disk"]),
               # AUTO IS THE DEFAULT (98.2.1.5): the field says AUTO and
               # the encode works the share out; profile_avg() is the
               # number it used to be, which the window offers beside it
               avg="" if prof["avg"] is None else "auto",
               peak=num(prof["peak"]),
               owe=num(None if live else prof["owe"]),
               reserve="" if live or prof["disk"] is None else
               num(reserve_bytes(prof) // 1024),
               memory="" if live or prof["disk"] is None else
               num(play_memory_kb(vid.ring_for(reserve_bytes(prof)),
                                  2 if FRAME_CAPS[pdef.get("frame_cap", "32")]
                                  > vid.SP_MAX else 1,
                                  d.get("audio", prof["audio"]) != "none")),
               rate=num(d.get("rate", prof["rate"])),
               audio=d.get("audio", prof["audio"]),
               # (MODE X's own screen is 320 x 240 - the window said
               # 640x480 there, which no Mode X file plays on: the owner)
               screen="320x240" if lay == "modex" else "")
    return out

# THE OPTIONS A FILE WAS MADE WITH (98.2.17): every option, as the encode
# USED it, stored in the .V88 (os88vid's options block, 98.1.1.4) so the
# encoder's window can load the file and show how it was made. EVERY
# option and not only the ones that differ from a default: a default is
# today's, and a file that stored "fps left at its default" would change
# its story the day that default moved. What a preset, a format or a
# profile implied is stored as the value it came to, and so is the
# speaker style's three numbers. It is ~160 bytes (os88vid.OPTS_ZDICT).
OPTS_VERSION = 47
# what is the encode's plumbing rather than how the file was made
OPTS_SKIP = ("src", "out", "help", "progress", "quiet", "preview_png",
             "profiles", "posters_only")
# the SCHEMA each version's records are written in - every option's name,
# kind and choices, not its default (opts_schema). tests/vencguitest.py
# fails when the parser no longer matches OPTS_VERSION's: an option was
# added, renamed, removed or took other choices, and the version must go
# up with a MIGRATIONS entry saying how an older record reads
OPTS_FINGERPRINT = {1: "06108fff43ef1307", 2: "496cc97e70197133",
                    3: "b22b9c16beb1304b", 4: "2e284988ae6e8775",
                    5: "85ea67f8ce1492a0", 6: "0d0af3901366a0c6",
                    7: "3c23376030acae01", 8: "a2cee436c2bd5178",
                    9: "566ce460b7bc0952", 10: "03033c94f4068ded",
                    11: "92246fa491f53aee", 12: "27f0d558163b22db",
                    13: "23fa4d29a8c351cd", 14: "a712ea2b2ccb6fb7",
                    15: "dfbf1586ebcab68e", 16: "e9243b5bc695a520",
                    17: "60fdabdf08a1c0e8", 18: "5fca77e01ca000f2",
                    19: "57c049e771bf1c27",
                    20: "993542ab8eacfddb", 21: "a5e999a5635b5d1b",
                    22: "22c68a5980e68e6d", 23: "97e25031b7d36403",
                    24: "46b76579ae06a076", 25: "22e038302fc21ad2",
                    26: "fd488db7645ae82c",
                    27: "1bbc64edb29f5541", 28: "f8b82d9a3bd56ab9",
                    29: "d68534558cface22",
                    30: "60532ee7474b8d76",
                    31: "09f14123f968592f", 32: "413eeac77e2d4441",
                    33: "1cb11afeb374b243", 34: "60df56fd426010fd",
                    35: "e1090fc6c20c5367", 36: "00e7310bd1a264d8",
                    37: "096abca8b264506a", 38: "eaf373ad8f1e1fdb",
                    39: "7a7d4203d99fd27d", 40: "4fffe628a862c24e",
                    41: "9c78d785df0d94a4", 42: "6f2cb773837bd222",
                    43: "ec2917558c23907f", 44: "1baaf8c2dacb6f7b",
                    45: "9f36a470662f10ba", 46: "bdf38e4e169941ba",
                    47: "5de4b2ffebe0a64f"}
LAG_MS = "50-100"       # --lag's default (98.2.1.1.3), the owner's (2026-10-16):
                        # a RANGE, searched for each burst, where it was one
                        # number tuned by hand. Its top is the 100 ms that
                        # was the default, so no file shows more lag than it
                        # could before, and the search keeps a burst that
                        # cuts nothing nearer the sound. How far behind a
                        # picture may be without the eye minding is the
                        # VIDEO's (Bad Carrot's flames read smooth at 186 ms;
                        # lip sync would not), so a file that wants more
                        # raises the top - 50-200 cut Mech2's 26 frames to 20
LAG_WIN = 4.0           # a search plays each candidate this many seconds
                        # at most...
LAG_SETTLE = 1.0        # ...or this long past its picture level again,
                        # whichever is sooner: a cut frame's bytes left wrong
                        # are redrawn after it, and that is the cut's cost too
POOL_KB = 2944          # --pool's default: a 4 MB board's ~3 MB of XMS less
                        # 128 KB (98.3.18.2.1)


# THE PROFILES' OLD NAMES (98.2.3): named for the owner's machines until
# they were named for the classes they stand in for. A command line or a
# script may still say the old one, and gets the same profile
PROFILE_RENAMED = {"286": "286-slow", "286-pvga": "286-fast",
                   "286-vga": "286-speeddemon", "486": "486-dx2-66"}


def profile_name(v):
    """--profile / --layer-profile: a profile's name, an old one taken as
    its new (PROFILE_RENAMED)"""
    return PROFILE_RENAMED.get(v, v)


# THE VERSION MAPPER: MIGRATIONS[n] is what turns a version-n record into
# version n+1, a list of steps applied in order:
#   ("rename", old, new)          an option took a new name
#   ("revalue", dest, {old: new}) a choice was renamed, or a value's
#                                 meaning moved (a dict, or a function
#                                 value -> value)
#   ("added", dest, legacy)       an option arrived: a file made before it
#                                 was made as if it were `legacy` - which
#                                 need not be the new option's default
#   ("removed", dest, why)        an option went: dropped, with the reason
# A record from a NEWER encoder than this one is read for what this one
# knows, and says so; nothing is ever guessed
MIGRATIONS = {
    # 2: --spk-shape says WHERE the sound is shaped: on is encoder, off is
    # none, and machine arrived (a speaker WAV's alone, 86.21.1)
    1: [("revalue", "spk_shape", {"on": "encoder", "off": "none"})],
    # 3: BUFFER SOUND AHEAD (98.1.8) - a file made before it carries its
    # sound in step with its picture
    2: [("added", "buffer_sound_ahead", "off"),
        ("added", "ahead_frames", 4)],
    # 4: --cut (98.2.1.2.1) - a file made before it chose by rank
    3: [("added", "cut", "rank")],
    # 5: --frame-cap (98.1.4.1) - a file made before it had 32 KB
    4: [("added", "frame_cap", "32")],
    # 6: --bands (98.2.1.2.2) - a file made before it drew plane by plane
    5: [("added", "bands", 0)],
    # 7: --profile 486 (98.2.3.2) - a new choice, so nothing an older record
    # says reads differently. Its 286-vga reads as the RE-MEASURED profile
    # (speed 4.5 where it was 6): a profile is a machine, and the machine
    # did not change, the number for it did
    6: [],
    # 8: --memory (98.2.1.3.1) - a file made before it had the profile's
    # ring, or --reserve's
    7: [("added", "memory", None)],
    # 9: --palette (98.2.3.7) - a file made before it had the clip's own
    8: [("added", "palette", "auto")],
    # 10: --screen (98.2.5.1) - a file made before it was mode 12h's
    9: [("added", "screen", None)],
    # 11: --bank and --prefill (98.2.1.3.2) - a file made before it had no
    # bank - and --profile 286-pvga, a new choice that reads nothing older
    10: [("added", "bank", None), ("added", "prefill", None)],
    # 12: THE LAYER (98.1.9) - a file made before it has none
    11: [("added", "layer_disk", None), ("added", "layer_memory", 64.0),
         ("added", "layer_prefill", None),
         ("added", "layer_seek", 10.0)],
    # 13: --layer-bank (98.1.9) - a layer made before it has none
    12: [("added", "layer_bank", None)],
    # 14: --layer-profile (98.1.9) - a layer made before it was the base's;
    # and --avg takes AUTO (98.2.1.5), which the record never holds - it
    # holds the share AUTO came to, so nothing older reads differently; and
    # --box, --detail and --screen take W*H as well (98.2.1), stored as WxH
    13: [("added", "layer_profile", None)],
    # 15: THE PROFILES ARE NAMED FOR THE MACHINES THEY STAND IN FOR (98.2.3),
    # not for the one each was measured on - a record that names an old one
    # reads as its new name, the same machine and the same numbers
    14: [("revalue", "profile", PROFILE_RENAMED),
         ("revalue", "layer_profile", PROFILE_RENAMED)],
    # 16: --bank and --layer-bank take AUTO (98.2.1.3.3), which the record
    # never holds - it holds the KB AUTO came to - and --pool, --layer-pool
    # and --layer-passes arrived: a file made before them banked what it was
    # told, and a layer made before them learned the base's need as it went
    # (when it learned it at all - 98.1.9.1's order is a day older)
    15: [("added", "pool", float(POOL_KB)), ("added", "layer_pool", None),
         ("added", "layer_passes", "one")],
    # 17: --profile 286-picomem1 (98.2.3) - a new choice, so nothing an
    # older record says reads differently
    16: [],
    # 18: --vga4-hold (VIDEO-PALETTE-PLAN 10) - a file made before it held
    # by the plan alone
    17: [("added", "vga4_hold", 0.0)],
    # 19: --vga4-hold-score (VIDEO-PALETTE-PLAN 10.2) - a file made before
    # it held unscored - and --palette colour16 (10.1), a new choice
    18: [("added", "vga4_hold_score", 0.0)],
    # 20: --scene-palettes (98.1.3.2.3) - a file made before it had one
    # palette for the whole clip
    19: [("added", "scene_palettes", False)],
    # 21: --vga4-plan-cap (VIDEO-PALETTE-PLAN 10.3) - a file made before
    # it let the pattern reach any colour
    # - and --scene-cover, a file made before it at 0.5
    20: [("added", "vga4_plan_cap", 0.0), ("added", "scene_cover", 0.5)],
    # 22: --palette colour8 and colour12 (VIDEO-PALETTE-PLAN 10.9) - new
    # choices; every older record's palette means what it did
    21: [],
    # 23: --scene-min (VIDEO-PALETTE-PLAN 10.10) - a file made before it
    # kept a palette 6 frames at least
    22: [("added", "scene_min", 6)],
    # 24: --page-ahead (98.2.1.8), and every file before it was made without
    23: [("added", "page_ahead", False)],
    # 25: --vga4-sparse (VIDEO-PALETTE-PLAN 10.11) - a file made before it
    # kept every pattern pick
    24: [("added", "vga4_sparse", 0)],
    # 26: --page-ahead auto, on or off (98.2.1.8.1): a version-25 record's
    # flag is on or off as it was
    25: [("revalue", "page_ahead", {True: "on", False: "off"})],
    # 27: --scene-hold and --vga4-order (docs/reports/VIDEO-ORDER-*) - a
    # file made before them started each scene's dither afresh, darkest
    # first
    26: [("added", "scene_hold", False), ("added", "vga4_order", "dark"),
         ("added", "coarse_cut", "off")],
    # 28: --page-prune (98.2.1.8.2) - a file made before it kept or dropped
    # page-ahead whole
    27: [("added", "page_prune", "off")],
    # 29: --step-down (98.2.1.9) - a file made before it was cut only
    28: [("added", "step_down", "off")],
    # 30: --step-min (98.2.1.9), and --step-down ON by default - a file
    # made before either was stepped as far as 8, or not at all as recorded
    29: [("added", "step_min", 8)],
    # 31: --vga4-wm3 (98.1.3.2.4) - a file made before it took write mode 3
    # on its own sixteen alone, and one in the EGA's sixteen re-encoded now
    # takes it too: a re-encode is the better file, not the same one
    30: [("added", "vga4_wm3", "on")],
    # 32: --flip-lag (98.2.1.1.2) - a file made before it kept the picture
    # less than ONE frame behind the sound
    31: [("added", "flip_lag", 1)],
    # 33: --lag MS for every play with a card's sound, in place of
    # --flip-lag FRAMES for flipped ones: the frames taken at 24 fps, the
    # rate the flag was set at on every file the owner made with it - and
    # 1 frame, the one-frame schedule, is 0: a file made before encodes the
    # same, flipped (whose floor is a frame either way) or not (whose owe
    # 0 keeps; 1 frame at 30 fps would be 1.25 and turn the lag on)
    32: [("rename", "flip_lag", "lag"),
         ("revalue", "lag", lambda n: 0.0 if float(n) <= 1 else
          round(float(n) * 1000.0 / 24, 1))],
    # 34: --pages and --page-keys (98.2.1.8.4) - a file made before them
    # built on two pages
    33: [("added", "pages", "2"), ("added", "page_keys", "all")],
    # 35: --key-pool (98.1.3.8.1) - a file made before it had none
    34: [("added", "key_pool", "off")],
    # 36: --lag takes a RANGE, LO-HI (98.2.1.1.3) - a record's number reads
    # as the fixed allowance it was (one between two of the schedule's
    # calls re-encodes as the call above it, which is the ceiling's fix)
    35: [],
    # 37: --attend and --edges (VIDEO-SEEN-PLAN 3) - a file made before
    # them spent a cut frame by the error as seen alone
    36: [("added", "attend", 0.0), ("added", "edges", 0.0)],
    # 38: --min-fps and --slow-cost (VIDEO-SEEN-PLAN 3a) - a file made
    # before them ran at one frame rate
    37: [("added", "min_fps", 0.0), ("added", "slow_cost", 2.0)],
    # 39: --flip-hold (98.1.3.9) - a flipped file made before it held with
    # nothing
    38: [("added", "flip_hold", "empty")],
    # 40: --ladder (VIDEO-SEEN-PLAN 3a) - a file made before it had none
    39: [("added", "ladder", "")],
    # 41: --lookahead-floor (98.2.1.2) - a file made before it let the
    # frames after take a pixel's worth all the way down
    40: [("added", "lookahead_floor", 0.0)],
    # 42: --flip-own (VIDEO-SEEN-PLAN 3b) - a file made before it was a
    # change against the frame before, its page brought up first
    41: [("added", "flip_own", False)],
    # 43: --desktop-posters and --poster-image (98.1.3.11) - a file made
    # before them had none
    42: [("added", "desktop_posters", "off"), ("added", "poster_image", None)],
    # 44: --desktop-size, and --desktop-posters' sets (98.1.3.11) - `on`
    # was the colour set, at the size the window shows
    43: [("revalue", "desktop_posters", {"on": "colour"}),
         ("added", "desktop_size", "fit")],
    # 45: --desktop-clip (98.1.3.11) - a file made before it dithered every
    # cell of its desktop posters
    44: [("added", "desktop_clip", 0.0)],
    # 46: --ladder-at and --scene-join (98.2.1.10, 98.1.3.2.3.3) - a file
    # made before them had one ladder for the clip and every scene's cut
    45: [("added", "ladder_at", ""), ("added", "scene_join", "")],
    # 47: --blocks (98.1.10) - a file made before it is version 1
    46: [("added", "blocks", "off")],
}


def dims(v):
    """A WxH (--box, --detail, --screen): `*` for the x as well as `x`, as
    in 640*480, and stored as WxH - so a file records one spelling"""
    p = str(v).strip().lower().replace("*", "x").split("x")
    if len(p) != 2 or not all(q.strip().isdigit() for q in p):
        raise argparse.ArgumentTypeError("%r is not WxH (or W*H)" % (v,))
    return "%dx%d" % tuple(int(q) for q in p)


def screen_arg(v):
    """--screen: a WxH (dims' spellings), with an N after it for a NARROW
    screen's 40-byte rows (98.1.3.2.5) - 320x200n"""
    t = str(v).strip().lower()
    n = t.endswith("n")
    return dims(t[:-1] if n else t) + ("n" if n else "")


def lag_arg(v):
    """--lag: MS, the allowance fixed (98.2.1.1.2) - or LO-HI, a range the
    encoder searches for each burst (98.2.1.1.3), stored as `LO-HI`"""
    if isinstance(v, (int, float)):
        return float(v)
    t = str(v).strip().lower().replace(" ", "")
    try:
        if "-" in t[1:]:
            lo, hi = (float(x) for x in t.split("-", 1))
            if lo < 0 or hi < lo:
                raise ValueError
            return lo if lo == hi else "%g-%g" % (lo, hi)
        x = float(t)
        if x < 0:
            raise ValueError
        return x
    except ValueError:
        raise argparse.ArgumentTypeError(
            "%r is not milliseconds, or LO-HI (98.2.1.1.3)" % (v,))


def lag_bounds(v):
    """--lag's (lowest, highest) milliseconds: the same twice when fixed"""
    v = lag_arg(LAG_MS if v is None else v)
    if isinstance(v, str):
        lo, hi = (float(x) for x in v.split("-", 1))
        return lo, hi
    return max(0.0, v), max(0.0, v)


def lag_best(runs):
    """THE SEARCH'S PICK (98.2.1.1.3) of its candidates' plays, each a dict
    of `L` (the allowance, frames), `cut` (frames cut over the window) and
    `left` (bytes left wrong after them): the fewest cut, then the fewest
    wrong, then the LEAST LAG - so a burst the range draws whole at every
    allowance keeps the picture nearest the sound"""
    return min(runs, key=lambda r: (r["cut"], r["left"], r["L"]))


def bank_arg(v):
    """--bank / --layer-bank: KB, or AUTO (98.2.1.3.3)"""
    if str(v).strip().lower() == "auto":
        return "auto"
    return float(v)


def avg_arg(v):
    """--avg's value: a share, or AUTO (98.2.1.5)"""
    if str(v).strip().lower() == "auto":
        return "auto"
    return float(v)


def profile_avg(profile="5150-st225", live=None):
    """The fixed average share a profile carried before AUTO was the
    default (98.2.1.5): its own, or a Live file's (LIVE_AVG) - what the
    encoder's window offers beside AUTO. "" for one with no budget"""
    prof = PROFILES[profile_name(profile)]
    if prof["avg"] is None:
        return ""
    v = LIVE_AVG if live else prof["avg"]
    return "%d" % v if float(v).is_integer() else "%g" % v


def avg_auto(prof):
    """--avg auto (98.2.1.5): on a profile with a measured disk CURVE the
    reader's CPU is already priced - every frame's disk refill is the rate
    the curve gives at that frame's share - and so are the bank's copies
    (xcopy) and the sound's (audio_cyc), so an average below the per-frame
    ceiling only reserves the same CPU a second time: AUTO is the peak.
    With no curve the average is the only thing that leaves the reader its
    time, and stays the profile's"""
    if prof.get("avg") is None or not prof.get("disk_at"):
        return prof.get("avg")
    return prof["peak"]


def opts_actions():
    """dest -> the parser's action, for every option a record holds"""
    return {x.dest: x for x in parser()._actions
            if x.option_strings and x.dest not in OPTS_SKIP}


def opts_schema():
    """[dest, kind, choices] for every option a record holds: what its KEYS
    mean at this version (98.2.17), and not what their defaults are"""
    out = []
    for d, x in sorted(opts_actions().items()):
        kind = "bool" if x.nargs == 0 else \
            getattr(x.type, "__name__", "str") if x.type else "str"
        out.append([d, kind, sorted(str(c) for c in x.choices)
                    if x.choices is not None else None])
    return out


def opts_fingerprint():
    import hashlib
    import json
    return hashlib.sha256(json.dumps(opts_schema(), sort_keys=True)
                          .encode()).hexdigest()[:16]


def _opt_typed(x, v):
    """A string the implied table gave, as the parser would have typed it"""
    if x.nargs == 0:
        return v in ("1", "True", "true", True)
    return x.type(v) if x.type else v


def options_record(a, sfps=None):
    """dest -> the value the encode USED for every option (98.2.17): the
    command line's, else what the preset, format, profile and Live choice
    imply (implied()), else the speaker style's own three numbers, else the
    parser's default - None only where None is itself the choice (the CGA4
    palette left to the clip, no end time)"""
    acts = opts_actions()
    o = {d: getattr(a, d, None) for d in acts}
    imp = implied(o.get("preset"), o.get("pixfmt"),
                  o.get("profile") or "5150-st225", o.get("live"), sfps)
    for d, v in imp.items():
        if d in o and (o[d] is None or (acts[d].nargs == 0 and not o[d])) \
                and v != "":
            o[d] = _opt_typed(acts[d], v)
    st = vid.SPK_STYLES[o.get("spk_style") or vid.SPK_STYLE]
    for d, k in (("spk_highpass", "hp"), ("spk_ratio", "ratio"),
                 ("spk_range", "rng")):
        if o.get(d) is None:
            o[d] = st[k]
    return o


# THE OPTIONS GIVEN AS AUTO (98.2.17.1): the record holds what each came
# to, and beside it which were AUTO - so a loaded form asks for AUTO again
# and not for the number, which is the same file only while nothing else
# in the form changes (and not always then: a layer for another machine
# takes that machine's own --avg auto, which a fixed share does not give it)
AUTO_OPTS = ("avg", "bank", "layer_bank")


def options_auto(a):
    """The AUTO_OPTS the encode was given as AUTO, sorted"""
    return sorted(d for d in AUTO_OPTS if getattr(a, d + "_auto", False) or
                  getattr(a, d, None) == "auto")


def options_doc(a, sfps=None, opts=None):
    """The record the file carries: its version, the source's NAME (not
    its path - a file handed on should not carry the maker's folders),
    every option, and which of them were AUTO"""
    return {"v": OPTS_VERSION,
            "src": os.path.basename(getattr(a, "src", "") or ""),
            "o": opts if opts is not None else options_record(a, sfps),
            "auto": options_auto(a)}


def opts_migrate(doc):
    """A stored record -> (dest -> value in TODAY's schema, [notes]): the
    MIGRATIONS from its version to OPTS_VERSION, then what this encoder
    does not know dropped and what the record does not hold named - each a
    note, so the window can say what it could not carry over. A value
    that is not a plain one, or not one of an option's choices, is dropped
    too: the record came off a file, and a file is hostile until read"""
    v = doc.get("v")
    o = doc.get("o")
    if not isinstance(v, int) or v < 1 or not isinstance(o, dict):
        raise vid.V88Error("an options record of version %r" % (v,))
    o, notes = dict(o), []
    if v > OPTS_VERSION:
        notes.append("made by a newer encoder (options version %d; this "
                     "one knows %d): what it does not know is left out"
                     % (v, OPTS_VERSION))
    while v < OPTS_VERSION:
        for step in MIGRATIONS.get(v, ()):
            kind = step[0]
            if kind == "rename" and step[1] in o:
                o[step[2]] = o.pop(step[1])
            elif kind == "revalue" and step[1] in o:
                m = step[2]
                o[step[1]] = m(o[step[1]]) if callable(m) else \
                    m.get(o[step[1]], o[step[1]])
            elif kind == "added" and step[1] not in o:
                o[step[1]] = step[2]
            elif kind == "removed" and step[1] in o:
                del o[step[1]]
                notes.append("--%s is gone: %s"
                             % (step[1].replace("_", "-"), step[2]))
        v += 1
    acts = opts_actions()
    for d in sorted(set(o) - set(acts)):
        del o[d]
        notes.append("--%s is not an option here: left out"
                     % d.replace("_", "-"))
    for d in sorted(o):
        x, val = acts[d], o[d]
        if val is not None and not isinstance(val, (str, int, float, bool)):
            bad = True
        elif x.choices is not None and val is not None:
            bad = str(val) not in [str(c) for c in x.choices]
        else:
            bad = False
        if bad:
            del o[d]
            notes.append("--%s %r is not a value it takes: left out"
                         % (d.replace("_", "-"), val))
    missing = sorted(set(acts) - set(o))
    if missing:
        notes.append("not in the record, so at today's default: %s"
                     % ", ".join("--" + d.replace("_", "-")
                                 for d in missing))
    return o, notes


HOOK_CYC = 3040.0        # the hook's own cycles a frame, outside the decode
                        # and the sound's copy: measured on the Hercules
                        # 5150, silent (docs/reports/VIDEO-PROFILE-2026-09-27)
LIVE_PASS_HZ = 1193182.0 / 65536   # a Live worker's passes a second: a tick
LIVE_AVG = 0.60         # a LIVE file's share of the machine (98.2.7): its
                        # decode AND its blit (98.3.10.2), the rest the
                        # desktop's around it - the owner's figure,
                        # 2026-09-27; --avg overrides it
CYC_AUDIO = 13.0        # the interrupt's copy of a PCM8 byte into the
                        # card's buffer (rep movsw, ~25 cycles a word)
# THE SPEAKER (SPEC.md 34.11, 98.2.15): no card, so every sample is an
# interrupt. os88spk_isr measured 389 cycles from its first instruction to
# the first one back in the code it interrupted on MartyPC's 5150 - its
# `iret` included, which the first figure (343, entry TO the iret) left out
# - and the 8088 takes ~60 more to acknowledge one: 449 a pulse, in 8088
# cycles (34.11.7.1). A file made for the speaker carries the COUNTS
# (98.1.1.3), so the player copies them into its ring as it would for a
# card; translating samples there was ~50 a byte. At 5,512 Hz that is ~52%
# of a 4.77 MHz machine and at 8,000 ~78% - which is the whole reason a clip
# is made FOR the speaker rather than merely played through it. It was 400,
# which left the iret out: ~4 points low at 5,512 Hz and ~8 at 8,000
CYC_SPK_PULSE = 449.0
CYC_SPK_BYTE = 15.0     # a plain copy: the file carries the counts
CYC_SPK_WHOLE2 = 522.0  # --spk-pulses 2: the whole pulse, and the half one,
CYC_SPK_HALF = 308.0    # measured the same way as CYC_SPK_PULSE (34.11.7.1)
                        # (98.1.1.3), so the table is the encoder's, not the
                        # player's. MEASURED: vp_aput 27.7 cycles a byte of
                        # wall time on MartyPC's 5150, ~15 its own once the
                        # pulses' ~46% is taken out; translating was 92.8/~50
SPK_RATE = 5512         # the speaker target's default rate
SPK_MIN = 4679          # N = 1,193,182 / rate is a lobyte count of 74..255
SPK_MAX_AT = 24858      # ...or 48..255 on a 286 profile (SPEC.md 34.11.8)
SPK_MAX_8088 = 8000     # VIDEO.O88's VP_SPKMAX: past it an 8088 is SILENT
REC_OVER = 6 + 10       # a record's header and its ten list terminators
VP_LCW = 2              # video.asm's: a latch-copied page byte, in record
                        # bytes' decode (98.3.8.1) - the player's own choice
CYC_LCOPY = 30          # ...and in cycles: a VGA read and write, ESTIMATED
                        # against CYC_SLICE's 18 for a write alone - a
                        # profile's `lcopy` is its MEASURED figure
CYC_LCOPY0 = 300        # its OUTs and setup
FRAME_CAPS = {"32": 64, "48": 96, "63.5": 127}  # --frame-cap: KB -> its
                        # super-packet's sectors (98.1.4.1); a record's
                        # room is 2 KB less, as REC_MAX is 32 KB's
REC_MAX = 30 * 1024     # a frame record rides in a super-packet of 32 KB
                        # (98.1.4): whatever the budgets say, no more.
                        # Only VGA8 can reach it - a MONO1 canvas is 16 KB
BANK_PREFILL = 10.0  # --prefill's default, seconds of the disk (98.3.18.3)
DISK_LOOKAHEAD = 96 * 1024  # what the disk bucket may bank: THREE of the
                        # player's 32 KB ring slots (98.3), which it has
                        # read before the first frame. It was one second of
                        # the rate, which is under the ring at a 5150's
                        # 60 KB/s and 2-4x over it at a 286's 400: the
                        # stream spent a surplus the player could not hold,
                        # and stalled a second in (the owner's TRK830)
VP_KMAXREC = 61440      # apps/video/video.asm's own cap on a keyframe
SND_HALF = 2048         # SOUND.DRV's block on an external ring: the card
SND_HALF_HI = 4096      # interrupts once a block, and at each it HALTS
SND_HALF_RATE = 22222   # unless the whole next block is queued (SPEC.md
                        # 34.5.2's ISR question) - 4096 above 22,222 Hz
VP_BLKDIV = 40          # apps/video/video.asm's: the player HALVES the block
                        # (up to three times, SND_OPENF_BLKSH) while it is
                        # longer than 1/40 s of sound (vp_sblk)
VP_HALFHI = 22222       # ...but PCM8 past this keeps its block


def audio_block(afmt, rate):
    """The card's block the player asks for (vp_sblk, SPEC.md 98.3.1):
    2,048 bytes (4,096 above 22,222 Hz PCM8, which keeps it), halved up to
    three times while it is longer than 25 ms of sound - 256 at 11 kHz PCM8
    and at 5,512 Hz, 512 at 22 kHz"""
    if afmt == vid.AUD_PCM8 and rate > SND_HALF_RATE:
        return SND_HALF_HI
    bps = rate // 2 if afmt == vid.AUD_ADPCM4 else rate
    if afmt == vid.AUD_PCM8 and rate > VP_HALFHI:
        return SND_HALF
    per = bps // VP_BLKDIV
    half, n = SND_HALF, 0
    while half > per and n < 3:
        half, n = half // 2, n + 1
    return half


AHEAD_SLACK = 0.10      # ...and Auto's (98.2.1.3): the share under the
                        # profile's disk an in-step stream must still fit
                        # at, or the file carries its sound ahead. T69 read
                        # 9% under 286-picomem1's where it ran its card dry
AHEAD_FRAMES = 4        # BUFFER SOUND AHEAD's default (98.1.8): frames of
                        # sound carried ahead of their picture - one 25 ms
                        # block's, plus one, plus two of margin at 24 fps


def ahead_frames(want, abytes):
    """The A a file is written with: `want`, no more than the player stages
    (os88vid.AHEAD_MAX bytes of it), 0 when not even one frame fits"""
    if not abytes:
        return 0
    return max(0, min(want, 255, vid.AHEAD_MAX // abytes))


def audio_lead(afmt, rate, abytes):
    """THE SOUND'S LEAD (98.2.1.3): the frames the reader must have loaded
    PAST the one playing, because the audio cursor queues a frame's sound
    only once its record is in the ring and the card halts at a block
    boundary whose next block is not all queued. So a block of sound, in
    frames, and one more for the frame the block ends inside: 2 at 11,025
    Hz PCM8 and 25 fps, 4 at 5,512 Hz ADPCM4 (256-byte blocks) - which was
    20 while every block was 2,048"""
    if not afmt or not abytes:
        return 0
    return -(-audio_block(afmt, rate) // abytes) + 1


def key_limit(clb):
    """The largest keyframe record the player reads in one go off a volume
    of `clb`-byte clusters - vp_parse's check (apps/video/video.asm, 98.1.3)
    worked out rather than restated: the record must be <= VP_KMAXREC, and
    its clusters at the worst offset - the record and a cluster less a byte,
    rounded up to whole clusters (vp_spankb) - must be one 64 KB claim at
    most. Past this it plays from the start only, with no poster and no
    seek."""
    return min(VP_KMAXREC, 0x10000 - clb + 1)


KEY_CLB = 2048          # the largest cluster of any disk this encoder makes:
                        # os88hdd's FAT16 is 4 sectors a cluster at 20 and
                        # 32 MB, and every floppy is 1 KB or less. kern_big's
                        # READ_AT takes whole SECTORS (SPEC.md 18.4.4.2), so
                        # its player reads a key as key_limit(512) on ANY
                        # volume - this binds kern_small, or an older kernel,
                        # on a hard disk past 256 MB (98.1.7.6)
KEY_PLAYER = key_limit(KEY_CLB)     # 61,440 - VP_KMAXREC itself, which
                        # clusters of 4 KB reach too; 32 KB ones (a volume
                        # near 2 GB) take 32,769


def need_tools():
    if np is None:
        sys.exit("os88venc: needs numpy (pip install numpy)")
    for t in ("ffmpeg", "ffprobe"):
        if not shutil.which(t):
            sys.exit("os88venc: needs %s on the PATH (apt-get install "
                     "ffmpeg)" % t)


def probe(path):
    """(width, height, display aspect, fps, duration, has audio)"""
    out = subprocess.run(
        ["ffprobe", "-v", "error", "-show_entries",
         "stream=codec_type,width,height,r_frame_rate,sample_aspect_ratio,"
         "duration:format=duration", "-of", "default=nw=1", path],
        capture_output=True, text=True, check=True).stdout
    v, audio, dur = {}, False, None
    cur = {}
    for line in out.splitlines() + ["codec_type=end"]:
        k, _, val = line.partition("=")
        if k == "codec_type":
            if cur.get("codec_type") == "video" and not v:
                v = cur
            if cur.get("codec_type") == "audio":
                audio = True
            cur = {}
        cur[k] = val
        if k == "duration" and val not in ("", "N/A"):
            dur = max(dur or 0, float(val))
    if not v:
        raise vid.V88Error("%s: no video stream" % path)
    w, h = int(v["width"]), int(v["height"])
    n, _, d = v.get("r_frame_rate", "30/1").partition("/")
    fps = float(n) / float(d or 1)
    sar = v.get("sample_aspect_ratio", "1:1")
    sn, _, sd = sar.partition(":")
    try:
        sar = float(sn) / float(sd) if float(sn) > 0 else 1.0
    except (ValueError, ZeroDivisionError):
        sar = 1.0
    return w, h, w * sar / h, fps, dur, audio


def canvas_size(layout, bw, bh, dar, fit, asp=None):
    """(width px, height, crop) of the canvas in a bw x bh box of `layout`
    for a source of display aspect `dar`. `fit` keeps the whole picture and
    shrinks the canvas to its shape (no bars: a bar would be screen the
    canvas does not need); `fill` crops the source to the box's shape;
    `stretch` fills the box and distorts. Width in whole bytes. `asp` is a
    pixel's shape where the format's is not the layout's (CGA4's)"""
    pw, ph = asp or vid.ASPECT[vid.LAYOUT_BY_NAME[layout]]
    par = pw / ph                          # a pixel's width over its height
    box = bw * par / bh                    # the box's displayed aspect
    if fit == "fit":
        if dar >= box:
            w, h = bw, round(bw * par / dar)
        else:
            w, h = round(bh * dar / par), bh
        return max(8, w // 8 * 8), max(2, min(bh, h)), None
    w, h = bw // 8 * 8, bh
    if fit == "fill":
        return w, h, box
    return w, h, None


# --------------------------------------------------------------------------
# dithering: ordered, anchored to the canvas
# --------------------------------------------------------------------------
def bayer(n):
    m = np.array([[0]])
    while m.shape[0] < n:
        m = np.block([[4 * m, 4 * m + 2], [4 * m + 3, 4 * m + 1]])
    return (m + 0.5) / m.size


def bluenoise(n=64, seed=88):
    """A void-and-cluster threshold map (Ulichney, 1993): blue noise, so the
    dither has no pattern to beat against the picture - at a little more
    data than Bayer's, whose regular pattern runs and repeats better."""
    rnd = np.random.default_rng(seed)
    g = np.exp(-(np.arange(-n // 2, n // 2) ** 2) / (2 * 1.5 ** 2))
    k = np.fft.fft2(np.fft.ifftshift(np.outer(g, g)))

    def energy(b):
        return np.real(np.fft.ifft2(np.fft.fft2(b) * k))
    b = (rnd.random((n, n)) < 0.1).astype(float)
    while True:                             # relax the initial pattern
        e = energy(b)
        hi = np.unravel_index(np.argmax(np.where(b > 0, e, -1e9)), b.shape)
        b[hi] = 0
        e = energy(b)
        lo = np.unravel_index(np.argmin(np.where(b == 0, e, 1e9)), b.shape)
        if lo == hi:
            b[hi] = 1
            break
        b[lo] = 1
    rank = np.zeros((n, n))
    ones = int(b.sum())
    b1 = b.copy()
    for r in range(ones - 1, -1, -1):
        e = energy(b1)
        i = np.unravel_index(np.argmax(np.where(b1 > 0, e, -1e9)), b.shape)
        b1[i] = 0
        rank[i] = r
    b1 = b.copy()
    for r in range(ones, n * n):
        e = energy(b1)
        i = np.unravel_index(np.argmin(np.where(b1 == 0, e, 1e9)), b.shape)
        b1[i] = 1
        rank[i] = r
    return (rank + 0.5) / (n * n)


def threshold_map(kind, w, h):
    if kind == "threshold":
        return np.full((h, w), 0.5)
    m = bayer(8) if kind == "bayer" else bluenoise()
    reps = (-(-h // m.shape[0]), -(-w // m.shape[1]))
    return np.tile(m, reps)[:h, :w]


class Ditherer:
    """THE ENDS ARE SOLID. A threshold map spread over the whole of 0..255
    puts its lowest cell at grey 2 and its highest at 253, so a black that
    a lossy source delivers as 3 lights ONE dot in every 8 x 8 tile, and a
    white as 252 darkens one - an even grid of dots over every flat area,
    which also costs bytes as the noise flickers them. So the map spans
    `clip`..255-`clip`: at or below the first a pixel is black, at or above
    the second white, and the steps between keep their spacing"""

    def __init__(self, kind, w, h, stable, invert, clip=16):
        self.t = clip + threshold_map(kind, w, h) * (255.0 - 2 * clip)
        self.stable, self.invert, self.clip = stable, invert, clip
        self.prev = None

    def __call__(self, grey):
        g = grey.astype(float)
        if self.invert:
            g = 255.0 - g
        on = g > self.t
        if self.prev is not None and self.stable:
            # (and never at or past a clip: that pixel IS black or white,
            # whatever the dead band would have held - Vga8Ditherer.settle)
            keep = (np.abs(g - self.t) < self.stable) & \
                (g > self.clip) & (g < 255.0 - self.clip)
            on = np.where(keep, self.prev, on)
        self.prev = on
        return np.packbits(on, axis=1)          # bit 7 = the leftmost


class CompDitherer:
    """COMPOSITE COLOUR (CGACOMP, SPEC.md 98.2.2): the frame, 160 cells a row
    of four hi-res pixels each, dithered to the 16 colours a CGA's nibbles
    show on a composite monitor - measured through the same model the
    emulators use (tools/os88cgacomp.py), not a table from memory.

    KNOLL'S PATTERN DITHER, the ordered dither for a FIXED palette: for
    each cell a list of N palette colours is built whose mean is its colour
    - each pick the nearest to the colour plus the error so far, times
    `mix` - sorted by luma, and the cell's Bayer threshold picks one of
    them. A grey-axis offset cannot mix two HUES and this can, which is
    what a composite palette with no blue-grey needs; and it is still
    anchored to the cell, so a still area costs nothing. Distance is in
    YCbCr with luma weighted `luma`. A cell whose last colour is within
    `stable` of this frame's pick, in that space, keeps it.
    A nibble's leftmost pixel is its high bit, so a byte is two cells, left
    in the high nibble."""

    def __init__(self, w, h, mix, stable, luma=1.5, n=4):
        import os88cgacomp
        self.pal = os88cgacomp.palette()
        self.lw = luma
        self.pyuv = self.yuv(self.pal)
        self.order = np.argsort(self.pyuv[:, 0] / luma)
        cw = w // 4
        self.N = n                  # 4: a 2 x 2 pattern, 16: 4 x 4
        m = bayer(2 if n == 4 else 4)
        reps = (-(-h // m.shape[0]), -(-cw // m.shape[1]))
        self.t = (np.tile(m, reps)[:h, :cw] * n).astype(int).clip(0, n - 1)
        self.mix, self.stable = mix, stable
        self.prev = None

    def yuv(self, rgb):
        r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
        y = 0.299 * r + 0.587 * g + 0.114 * b
        return np.stack((y * self.lw, 0.564 * (b - y), 0.713 * (r - y)),
                        axis=-1)

    def nearest(self, c):
        d = ((self.yuv(c)[..., None, :] - self.pyuv) ** 2).sum(-1)
        return d.argmin(-1)

    def raw(self, rgb):
        c = rgb.astype(float)
        h, cw = c.shape[:2]
        err = np.zeros_like(c)
        cands = np.empty((self.N, h, cw), np.int64)
        for i in range(self.N):
            k = self.nearest(c + err * self.mix)
            cands[i] = k
            err += c - self.pal[k]
        # by luma, then the threshold's rank
        lum = self.pyuv[cands, 0]
        rank = np.argsort(lum, axis=0, kind="stable")
        pick = np.take_along_axis(rank, self.t[None], 0)[0]
        return np.take_along_axis(cands, pick[None], 0)[0]

    def settle(self, rgb, best):
        if self.prev is not None and self.stable:
            y = self.yuv(rgb.astype(float))
            dp = ((y - self.pyuv[self.prev]) ** 2).sum(-1)
            db = ((y - self.pyuv[best]) ** 2).sum(-1)
            best = np.where(np.abs(dp - db) < self.stable ** 2, self.prev,
                            best)
        self.prev = best
        n = best.astype(np.uint8)
        return (n[:, 0::2] << 4) | n[:, 1::2]

    def __call__(self, rgb):
        return self.settle(rgb, self.raw(rgb))


class CompDiffuser:
    """COMPOSITE COLOUR BY ERROR DIFFUSION THROUGH THE MODEL (SPEC.md
    98.2.2), the default. The target is the frame at FULL hi-res width -
    luma on a composite monitor has it, only colour is a quarter - and each
    cell's nibble is the one whose four output pixels, rendered BESIDE ITS
    LEFT NEIGHBOUR (tools/os88cgacomp.cell_lut), come nearest the target
    plus the error carried to it, Floyd-Steinberg over cells: 7/16 right,
    3, 5 and 1/16 to the row below. A cell waits for its left neighbour and
    the three above it, so every cell with g + 2y = s is decided together
    at step s - a wavefront, vectorised. What XDC's own composite streams
    look like, rendered through the model, is what this was aimed at; the
    pattern dither before it read as flat colour with fringes.
    The dead band: a cell keeps last frame's nibble when that costs at most
    `stable` more than the best, so a still area is still."""

    W = np.array([1.5, 1.0, 1.0]) if np is not None else None

    def __init__(self, w, h, stable, look=True):
        import os88cgacomp
        self.look = look
        self.cells = w // 4
        self.LY = self.yuv(os88cgacomp.cell_lut())
        idx = np.arange(16)
        self.flat = self.LY[:, idx, idx]           # (left, b, 4, 3): c = b
        self.stable = stable
        self.prev = None

    def yuv(self, rgb):
        r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
        y = 0.299 * r + 0.587 * g + 0.114 * b
        return np.stack((y, 0.564 * (b - y), 0.713 * (r - y)), -1) * self.W

    def __call__(self, rgb):
        h, n = rgb.shape[0], self.cells
        T = self.yuv(rgb.astype(float)).reshape(h, n, 4, 3)
        nib = np.zeros((h, n), np.int64)
        E = np.zeros((h + 1, n + 2, 3))
        for s in range(n + 2 * (h - 1)):
            ys = np.arange(max(0, (s - n + 2) // 2), min(h - 1, s // 2) + 1)
            gs = s - 2 * ys
            ok = (gs >= 0) & (gs < n)
            ys, gs = ys[ok], gs[ok]
            if not len(ys):
                continue
            t = T[ys, gs] + E[ys, gs + 1][:, None, :]
            left = np.where(gs > 0, nib[ys, np.maximum(gs - 1, 0)], 0)
            k = np.arange(len(ys))
            if self.look:
                # ONE CELL OF LOOKAHEAD: a cell's pixels depend on its right
                # neighbour too, so each b is weighed with the c that suits
                # it best - its own pixels beside c, plus c's beside b
                t2 = T[ys, np.minimum(gs + 1, n - 1)]
                own = ((self.LY[left] - t[:, None, None]) ** 2).sum((3, 4))
                nxt = ((self.flat[None] - t2[:, None, None]) ** 2
                       ).sum((3, 4))                # (k, b, c)
                both = own + nxt
                e = both.min(2)                     # (k, b)
                cc = both.argmin(2)
                cb = self.LY[left[:, None], np.arange(16)[None], cc]
            else:
                cb = self.flat[left]                # (k, 16, 4, 3)
                e = ((cb - t[:, None]) ** 2).sum((2, 3))
            best = e.argmin(1)
            if self.prev is not None and self.stable:
                p = self.prev[ys, gs]
                best = np.where(e[k, p] - e[k, best] <= self.stable, p, best)
            err = (t - cb[k, best]).mean(1) * 0.9
            nib[ys, gs] = best
            E[ys, gs + 2] += err * (7 / 16)
            E[ys + 1, gs] += err * (3 / 16)
            E[ys + 1, gs + 1] += err * (5 / 16)
            E[ys + 1, gs + 2] += err * (1 / 16)
        self.prev = nib
        v = nib.astype(np.uint8)
        return (v[:, 0::2] << 4) | v[:, 1::2]


_CD = {}


def _comp_chunk(args):
    w, h, stable, look, chunk = args
    key = (w, h, stable, look)
    if key not in _CD:
        _CD.clear()
        _CD[key] = CompDiffuser(w, h, stable, look)
    d = _CD[key]
    d.prev = None               # a chunk starts its dead band afresh
    return [d(f) for f in chunk]


def comp_parallel(frames, w, h, a, jobs, per=150, tick=None):
    """CompDiffuser on every core: the frames in chunks of `per`, each
    chunk's first frame dithered without a previous one - which costs that
    one frame the dead band, about a keyframe's worth every `per` frames -
    and at most `jobs` chunks in memory at once"""
    out = []
    look = not a.comp_quick
    with worker_pool(jobs) as pool:
        batch = []

        def flush():
            for r in pool.map(_comp_chunk, batch):
                out.extend(r)
            batch.clear()
            if tick:
                tick(len(out))
        cur = []
        for f in frames:
            cur.append(f)
            if len(cur) == per:
                batch.append((w, h, a.comp_stable, look, cur))
                cur = []
                if len(batch) == jobs:
                    flush()
        if cur:
            batch.append((w, h, a.comp_stable, look, cur))
        if batch:
            flush()
    return out


class Staged:
    """A STAGED DITHERER, on every core with nothing changed. A ditherer's
    frame is two stages: `raw`, the frame's own choice, which reads no
    state - nearly all of the work - and `settle`, which weighs it against
    what the LAST frame chose (the dead band, and a text cell's glyph) and
    must go in order. So `raw` runs `jobs` frames at once in processes of
    its own and `settle` here, and the frames come out exactly as one
    process makes them - where cutting the clip into chunks (comp_parallel)
    starts each chunk's dead band afresh. `d` is the ditherer, `post`
    finishes a settled frame, and `part` picks from an item what `raw`
    reads (an item may carry more: a text frame's OCR words)"""

    def __init__(self, d, post=None, part=None, rest=None):
        self.d, self.post, self.part, self.rest = d, post, part, rest

    def raw(self, item):
        return self.d.raw(self.part(item) if self.part else item)

    def settle(self, item, r):
        if self.part:
            v = self.d.settle(self.part(item), r, *self.rest(item))
        else:
            v = self.d.settle(item, r)
        return self.post(v) if self.post else v

    def __call__(self, item):
        return self.settle(item, self.raw(item))


_DW = None


def _dw_init(d):
    global _DW
    _DW = d


def _dw_raw(f):
    return _DW.raw(f)


# the variables that size a numeric library's own thread pool: a worker
# is one core, so its BLAS is one thread (eight workers of eight threads
# each are slower than eight workers)
ONE_THREAD = ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS",
              "VECLIB_MAXIMUM_THREADS", "NUMEXPR_NUM_THREADS")


class _Done(object):
    def __init__(self, v):
        self.v = v

    def get(self, timeout=None):
        return self.v

    def wait(self, timeout=None):
        pass

    def ready(self):
        return True


class _InProcess(object):
    """worker_pool's stand-in where a pool cannot be had - inside another
    pool's worker, which may not start processes of its own: the same
    calls, made here, in order"""

    def __init__(self, init=None, args=()):
        if init:
            init(*args)

    def apply_async(self, f, args=()):
        return _Done(f(*args))

    def map(self, f, it):
        return [f(x) for x in it]

    def terminate(self):
        pass

    close = join = terminate

    def __enter__(self):
        return self

    def __exit__(self, *x):
        pass


def worker_pool(jobs, init=None, args=()):
    """`jobs` processes. FORKED on Linux, as this encoder's pools always
    were, while this process has no other thread: a fork copies a thread's
    locks mid-hold, so with one running (the OCR's readers) - and on the
    systems where a fork is not the default at all - SPAWNED, each with
    one thread of numeric library; a spawned worker imports the program
    that started it, so a script calling encode() wants the usual
    `if __name__ == "__main__"`. In a pool's worker (--aim quality's trial
    encodes) the work is done in that worker instead: the same results,
    one core each"""
    import multiprocessing
    import threading
    if multiprocessing.current_process().daemon:
        return _InProcess(init, args)
    if sys.platform.startswith("linux") and threading.active_count() == 1:
        return multiprocessing.get_context("fork").Pool(jobs, init, args)
    ctx = multiprocessing.get_context("spawn")
    saved = {k: os.environ.get(k) for k in ONE_THREAD}
    for k in ONE_THREAD:
        os.environ.setdefault(k, "1")
    try:
        return ctx.Pool(jobs, init, args)
    finally:
        for k, v in saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v


def dither_all(dith, items, jobs):
    """dith(item) for every item, in order: a Staged ditherer's `raw` on
    `jobs` processes, a few frames a process in flight, and its `settle`
    here; anything else, or one job, here alone"""
    if jobs <= 1 or not isinstance(dith, Staged):
        for it in items:
            yield dith(it)
        return
    from collections import deque
    pool = worker_pool(jobs, _dw_init, (dith.d,))
    try:
        q = deque()
        for it in items:
            q.append((it, pool.apply_async(
                _dw_raw, (dith.part(it) if dith.part else it,))))
            if len(q) >= 2 * jobs:
                it, r = q.popleft()
                yield dith.settle(it, r.get())
        while q:
            it, r = q.popleft()
            yield dith.settle(it, r.get())
    finally:
        pool.terminate()
        pool.join()


# --------------------------------------------------------------------------
# the budgeted encoder
# --------------------------------------------------------------------------
class Vga8Ditherer:
    """256 colours for mode 13h (SPEC.md 98.2.3): an ordered dither over
    the clip's own palette, anchored to the canvas so a still picture keeps
    its pattern, and STABLE - a pixel keeps the colour on the screen while
    that colour is within `stable` of what it should be, so noise in the
    source does not become bytes. The nearest colour is a 32 x 32 x 32
    table, made once"""

    def __init__(self, w, h, palette, strength, stable, clip=0.0):
        self.pal = np.frombuffer(palette, np.uint8).astype(np.float32) \
            .reshape(-1, 3) * (255 / 63)
        # THE ENDS ARE SOLID (Ditherer's rule, in colour): a pixel within
        # `clip` levels on every channel of the palette's darkest or
        # lightest colour IS that colour - the ordered dither's offset
        # otherwise lifts one cell in a tile of a near-black area to the
        # next colour up, an even grid of dots. The ends only: a 256-colour
        # palette is dense, and snapping to any colour would band it
        lum = self.pal @ np.array([0.299, 0.587, 0.114], np.float32)
        self.ends = [int(lum.argmin()), int(lum.argmax())]
        self.clip = clip
        g = (np.arange(32, dtype=np.float32) * 255 / 31)
        cube = np.stack(np.meshgrid(g, g, g, indexing="ij"), -1).reshape(-1, 3)
        lut = np.empty(len(cube), np.uint8)
        for i in range(0, len(cube), 4096):
            d = ((cube[i:i + 4096, None, :] - self.pal[None]) ** 2).sum(2)
            lut[i:i + 4096] = d.argmin(1)
        self.lut = lut
        self.t = ((threshold_map("bayer", w, h) - 0.5) * strength)[..., None]
        self.stable = stable
        self.prev = None

    def raw(self, rgb):
        """The frame's own choice, which reads no state (STAGED, below)"""
        f = rgb.astype(np.float32)
        q = np.clip(np.rint((f + self.t) * (31 / 255)), 0, 31).astype(
            np.int32)
        idx = self.lut[(q[..., 0] << 10) | (q[..., 1] << 5) | q[..., 2]]
        for e, near in self.solid(f):
            idx = np.where(near, np.uint8(e), idx)
        return idx

    def solid(self, f):
        """(end, mask) for each END a pixel of `f` is within `clip` of"""
        if not self.clip:
            return []
        return [(e, np.abs(f - self.pal[e]).max(-1) <= self.clip)
                for e in self.ends]

    def settle(self, rgb, idx):
        """...and last frame's taken into account, in order. A pixel the
        clip makes SOLID is never held: the screen's colour can be within
        `stable` of a near-black or a near-white without being black or
        white, and holding it put the dither's dots back for as long as the
        area stayed flat - a light grey speck in a white that faded up to
        it, a dark one in a glow that faded out, standing still while the
        picture moved round them (2026-10-07, the owner's Bad Carrot)"""
        if self.prev is not None and self.stable:
            f = rgb.astype(np.float32)
            err = np.sqrt(((self.pal[self.prev] - f) ** 2).sum(2))
            keep = err < self.stable
            for _, near in self.solid(f):
                keep &= ~near
            idx = np.where(keep, self.prev, idx)
        self.prev = idx
        return idx

    def __call__(self, rgb):
        return self.settle(rgb, self.raw(rgb))


# A PALETTE CHOSEN BY HAND (--palette, 98.2.3.7): a ramp of N greys, or the
# clip's own N colours, in place of palettegen's 256. The names are the
# option's choices; "auto" is each format's own (VGA8: the clip's 256)
GREY_NS = (2, 4, 8, 16, 32, 64)
CLIP_NS = (2, 4, 8, 16, 32, 64, 128)
# ...or the clip's own N chosen for COLOUR (colour_palette, VIDEO-PALETTE-
# PLAN 10.1): sixteen the count it was measured and liked at - at 8 and 4
# it lost as much colour as palettegen's over a whole clip. 12 and 8 are
# there for a palette a scene, where a scene's own few may do better
# (VIDEO-PALETTE-PLAN 10.9, the rate ladder's condense step)
COLOUR_NS = (8, 12, 16)
PALETTES = ("auto",) + tuple("grey%d" % n for n in GREY_NS) + \
    tuple("clip%d" % n for n in CLIP_NS) + \
    tuple("colour%d" % n for n in COLOUR_NS)


def palette_kind(name):
    """--palette's choice -> ("auto", 0), ("grey", N), ("clip", N) or
    ("colour", N)"""
    name = name or "auto"
    for k in ("grey", "clip", "colour"):
        if name.startswith(k):
            return k, int(name[len(k):])
    return "auto", 0


def grey_levels(n):
    """The N greys of a ramp, in the DAC's six bits, black to white"""
    return [round(i * 63 / (n - 1)) for i in range(n)]


def grey_palette(n):
    """A 256-entry palette (98.1.1's rendition palette, six bits) whose
    first N are the ramp, black at 0, and the rest the ramp's white"""
    lv = grey_levels(n)
    lv += [lv[-1]] * (256 - n)
    return bytes(v for v in lv for _ in range(3))


class GreyDitherer:
    """A RAMP OF N GREYS (--palette greyN, 98.2.3.7): the one-bit Ditherer
    made N-level. A pixel's luma is placed on the ramp - x, an integer at
    each level's own grey - and the threshold map (`kind`: bayer,
    bluenoise, or threshold for the nearest level) picks between the two
    levels round it, so every level is reachable and a flat grey ON a level
    is that level alone, where Vga8Ditherer reaches 32 greys at most and
    spreads each over three. THE ENDS ARE SOLID as the one-bit map's are:
    at or below `clip` is black, at or above 255 - `clip` white, and the
    ramp's first and last steps are stretched over what is left of them
    (for N = 2 that is exactly Ditherer's map). STABLE as the one-bit dead
    band: a pixel keeps its level while a luma within `stable` of its own
    would still pick it, and never at or past a clip. Its output is an
    index into grey_palette(n)"""

    def __init__(self, w, h, n, kind, stable, clip=16.0):
        self.n, self.clip, self.stable = n, float(clip), float(stable)
        self.b = threshold_map(kind, w, h).astype(np.float32)
        lv = [float((v * 255 + 31) // 63) for v in grey_levels(n)]
        lo, hi = self.clip, 255.0 - self.clip
        kn = [(lo, 0.0)] + [(v, float(k)) for k, v in enumerate(lv)
                            if 0 < k < n - 1 and lo < v < hi] + \
            [(hi, float(n - 1))]
        self.kx = np.array([k[0] for k in kn], np.float32)
        self.ky = np.array([k[1] for k in kn], np.float32)
        self.prev = None

    def place(self, lum):
        return np.interp(lum, self.kx, self.ky).astype(np.float32)

    def pick(self, x):
        return np.clip(np.floor(x + self.b), 0, self.n - 1)

    def raw(self, rgb):
        f = rgb.astype(np.float32)
        lum = f[..., 0] * 0.299 + f[..., 1] * 0.587 + f[..., 2] * 0.114
        return self.pick(self.place(lum)).astype(np.uint8), lum

    def settle(self, rgb, r):
        idx, lum = r
        if self.prev is not None and self.stable:
            lo = self.pick(self.place(lum - self.stable))
            hi = self.pick(self.place(lum + self.stable))
            keep = (self.prev >= lo) & (self.prev <= hi) & \
                (lum > self.clip) & (lum < 255.0 - self.clip)
            idx = np.where(keep, self.prev, idx)
        self.prev = idx
        return idx

    def __call__(self, rgb):
        return self.settle(rgb, self.raw(rgb))


class KnollDitherer(Vga8Ditherer):
    """A PATTERN dither over a sparse palette (Thomas Knoll's, 98.2.5): each
    pixel's plan is sixteen palette colours, each the nearest to the target
    plus the error of the ones before it - so their mean is the target -
    sorted by luma, and the 4 x 4 Bayer cell over the pixel picks one. The
    sixteen EGA colours are 85 to 170 steps apart, and an ordered dither
    that shifts all three channels together only ever mixes greys: the sky
    and the grass came out grey. Stable as Vga8Ditherer is"""

    N = 16

    HOLD_DECAY = 0.8                # the hold score's, a frame (settle)

    def __init__(self, w, h, palette, stable, clip=0.0, hold=0.0,
                 score=0.0, cap=0.0, sparse=0):
        super().__init__(w, h, palette, 0.0, stable)
        self.kclip = clip               # (every colour: see raw)
        self.sparse = sparse            # --vga4-sparse: see raw
        self.cap = cap                  # --vga4-plan-cap: see raw
        self.hold = hold                # --vga4-hold: see settle
        self.score_t = score            # --vga4-hold-score: see settle
        self.score = None
        b = (bayer(4) * 16).astype(np.int64)            # 0..15
        self.cell = np.tile(b, (-(-h // 4), -(-w // 4)))[:h, :w]
        pal = self.pal
        self.luma = pal[:, 0] * 0.299 + pal[:, 1] * 0.587 + pal[:, 2] * 0.114

    def raw(self, rgb):
        f = rgb.astype(np.float32)
        err = np.zeros_like(f)
        cands = []
        for _ in range(self.N):
            t = np.clip(f + err, 0, 255)
            q = np.clip(np.rint(t * (31 / 255)), 0, 31).astype(np.int32)
            c = self.lut[(q[..., 0] << 10) | (q[..., 1] << 5) | q[..., 2]]
            if self.cap:
                # THE PLAN'S REACH (--vga4-plan-cap, VIDEO-PALETTE-PLAN
                # 10.3): a candidate further from the source than `cap`, or
                # than twice the nearest colour if that is further, is the
                # nearest colour instead. The plan's error is unbounded, so
                # a dull grey a clip's sixteen cannot quite make reaches
                # across the palette for one bright colour in sixteen whose
                # error balances the rest - red and yellow dots on a grey
                # face, and they flash as the source's noise moves them.
                # The EGA's sixteen are 85 to 170 apart and NEED the reach
                # (39% of colour lost at 64), so it is a clip palette's
                # alone; a saturated target, far from every colour, keeps
                # its reach through the twice-the-nearest term
                if not cands:
                    c0 = c
                    d0 = 2 * np.sqrt(((self.pal[c0] - f) ** 2).sum(-1))
                dc = np.sqrt(((self.pal[c] - f) ** 2).sum(-1))
                c = np.where(dc > np.maximum(self.cap, d0), c0, c)
            cands.append(c)
            err += f - self.pal[c]
        cands = np.stack(cands, -1)                     # (h, w, N)
        # THE PLAN'S COLOURS, a bit each: what settle() may hold a pixel at
        plan = np.zeros(cands.shape[:2], np.uint16)
        for k in range(self.N):
            plan |= np.left_shift(np.uint16(1), cands[..., k].astype(
                np.uint16))
        order = np.argsort(self.luma[cands], axis=-1, kind="stable")
        pick = np.take_along_axis(order, self.cell[..., None], -1)[..., 0]
        idx = np.take_along_axis(cands, pick[..., None], -1)[..., 0] \
            .astype(np.uint8)
        if self.sparse:
            # NO LONE DOTS (--vga4-sparse, VIDEO-PALETTE-PLAN 10.11): a
            # colour making `sparse` or fewer of the plan's sixteen is one
            # Bayer cell of each 4 x 4 tile - an even grid of dots, which
            # is what a near-black's carried error makes - so it is the
            # colour nearest the pixel, the plan's first, instead. A
            # texture's colours make up more of the plan than that and keep
            # their pattern, which --clip, flattening everything near a
            # colour, does not
            few = (cands == idx[..., None]).sum(-1) <= self.sparse
            idx = np.where(few, cands[..., 0].astype(np.uint8), idx)
        if self.kclip:
            # SOLID COLOURS (`--clip`, Ditherer's rule for a sparse palette):
            # a pixel within `clip` levels on every channel of the colour
            # nearest it - the plan's first - IS that colour. The plan
            # otherwise carries a near-black's small error until one cell in
            # sixteen takes dark grey or blue: an even grid of dots over
            # every flat area, which also costs bytes as noise flickers them
            c0 = cands[..., 0]
            near = np.abs(f - self.pal[c0]).max(-1) <= self.kclip
            idx = np.where(near, c0.astype(np.uint8), idx)
            plan = np.where(near, np.left_shift(np.uint16(1), c0.astype(
                np.uint16)), plan)
        return idx, plan

    def settle(self, rgb, r):
        idx, plan = r
        f = rgb.astype(np.float32)
        if self.prev is not None and self.stable:
            # STABLE BY THE SOURCE: a pixel keeps its colour while what it
            # was chosen for has moved less than `stable` - a pattern's
            # choice is not near its target, so the VGA8 rule cannot apply
            # - AND while that colour is still one the pixel's plan mixes.
            # Without the second test a dot picked while a dim trail passed
            # (a red one behind a bright line) outlives it: the source sinks
            # back to black by less than `stable`, black's plan is black,
            # and the dot stays on the screen until the scene changes
            moved = np.sqrt(((f - self.at) ** 2).sum(2))
            keep = (moved < self.stable) & (np.right_shift(
                plan, self.prev.astype(np.uint16)) & 1).astype(bool)
            if self.hold:
                # THE COLOUR HOLD (--vga4-hold, VIDEO-PALETTE-PLAN 10): the
                # colour on the screen stays while it is within `hold` of
                # the source, whatever the plan says - VGA8's rule. A clip's
                # own sixteen are close together and the plan moves between
                # them on small changes; this is the rate it buys back. It
                # is the rule the plan test above was added AGAINST (a dot
                # left by a dim trail), so it is off unless asked for -
                # and, alone, it keeps a dark sky from the LAST scene and
                # stops a panning mountain, comparing the screen with the
                # source now with no memory. So it is SCORED (10.2): a
                # pixel the dither wants to change is held only while a
                # leaky sum (HOLD_DECAY a frame) of how far its colour is
                # from the source, over the frames it wanted to change,
                # stays under `score_t`. Noise comes and goes and never
                # builds; a colour that is wrong and stays wrong builds and
                # is let go, and the pixel may then hold at its new colour.
                # A score of 0 is the unscored hold, a file made before it
                err = np.sqrt(((self.pal[self.prev] - f) ** 2).sum(2))
                held = err < self.hold
                if self.score_t:
                    want = idx != self.prev
                    if self.score is None:
                        self.score = np.zeros(err.shape, np.float32)
                    self.score = self.score * self.HOLD_DECAY + \
                        np.where(want, err, 0)
                    held &= want & (self.score < self.score_t)
                    keep |= held
                    # a pixel that changes starts again
                    self.score = np.where(want & ~keep, 0, self.score)
                else:
                    keep |= held
            idx = np.where(keep, self.prev, idx)
            self.at = np.where(keep[..., None], self.at, f)
        else:
            self.at = f
        self.prev = idx
        return idx


class C512Ditherer:
    """C512 (SPEC.md 98.2.14): each 8 x 2 cell the code, of 512, nearest in
    CIELAB to the cell's colour plus an 8 x 8 Bayer offset - nearest by the
    target CARD's composite model, or for `both` by the larger of the two
    cards' errors. Two 32 x 32 x 32 tables, made once, hold the nearest code
    of each character; a cell prefers last frame's character by CHAR_BIAS
    (its attribute alone is one byte, a new character two) and keeps last
    frame's code while that is within `stable` of the best.
    With `mix` of 2 or more the cell is instead Knoll's PATTERN (98.2.5)
    over `mix` codes, the error carried at MIX_K against the cards' mean
    colour, stable by the source at `mstable`: a faint tint's nearest code
    is a grey, and `both` has few codes that are a tint on both cards, so
    the pattern trades grain for the tint. Returns the canvas: a row of
    characters and attributes, the screen's own order"""

    CHAR_BIAS = 0.75            # CIELAB units: a character change's price
    MIX_K = 0.6                 # the pattern's error carry: less grain

    def __init__(self, w, h, card, amp, stable, mix=0, mstable=24.0):
        import os88cgacomp as cc
        cards = {vid.CARD_OLD: (False,), vid.CARD_NEW: (True,),
                 vid.CARD_BOTH: (False, True)}[card]
        self.labs = [cc.srgb2lab(cc.c512_palette(n)).astype(np.float32)
                     for n in cards]
        g = (np.arange(32, dtype=np.float32) * 255 / 31)
        cube = cc.srgb2lab(np.stack(np.meshgrid(g, g, g, indexing="ij"), -1)
                           .reshape(-1, 3)).astype(np.float32)
        self.lut = np.empty((2, len(cube)), np.int16)
        for i in range(0, len(cube), 2048):
            d = self.cost(cube[i:i + 2048, None, :], slice(None))
            for ch in (0, 1):
                self.lut[ch, i:i + 2048] = \
                    d[:, ch * 256:(ch + 1) * 256].argmin(1) + ch * 256
        self.t = ((bayer(8) - 0.5) * amp).astype(np.float32)
        self.t = np.tile(self.t, (-(-h // 8), -(-w // 8)))[:h, :w, None]
        self.stable = stable
        self.prev = None
        self.mix = mix
        if mix >= 2:
            self.mstable = mstable
            self.pal = sum(cc.c512_palette(n).astype(np.float32)
                           for n in cards) / len(cards)
            self.luma = self.pal @ np.array([0.299, 0.587, 0.114],
                                            np.float32)
            self.mlut = np.empty(len(cube), np.int16)
            for i in range(0, len(cube), 2048):
                self.mlut[i:i + 2048] = self.cost(
                    cube[i:i + 2048, None, :], slice(None)).argmin(1)
            b = (bayer(4) * mix).astype(np.int64)       # 0..mix-1
            self.cell = np.tile(b, (-(-h // 4), -(-w // 4)))[:h, :w]

    def cost(self, lab, codes):
        """CIELAB error of `codes` for targets `lab` - the larger of the
        cards' when there are two"""
        return np.stack([np.sqrt(((lab - pl[codes]) ** 2).sum(-1))
                         for pl in self.labs]).max(0)

    def pattern(self, rgb):
        f = rgb.astype(np.float32)
        err = np.zeros_like(f)
        cands = []
        for _ in range(self.mix):
            t = np.clip(f + err * self.MIX_K, 0, 255)
            q = np.clip(np.rint(t * (31 / 255)), 0, 31).astype(np.int32)
            c = self.mlut[(q[..., 0] << 10) | (q[..., 1] << 5) | q[..., 2]]
            cands.append(c)
            err += f - self.pal[c]
        cands = np.stack(cands, -1)                     # (h, w, mix)
        order = np.argsort(self.luma[cands], axis=-1, kind="stable")
        pick = np.take_along_axis(order, self.cell[..., None], -1)[..., 0]
        return np.take_along_axis(cands, pick[..., None], -1)[..., 0], cands

    def pattern_settle(self, rgb, r):
        best, cands = r
        f = rgb.astype(np.float32)
        if self.prev is not None and self.mstable:
            # KnollDitherer's rule: held while the source has moved less
            # than `mstable` AND the code is still one the plan mixes
            moved = np.sqrt(((f - self.at) ** 2).sum(2))
            keep = (moved < self.mstable) & (
                cands == self.prev[..., None]).any(-1)
            best = np.where(keep, self.prev, best)
            self.at = np.where(keep[..., None], self.at, f)
        else:
            self.at = f
        self.prev = best
        return best

    def raw(self, rgb):
        if self.mix >= 2:
            return self.pattern(rgb)
        t = np.clip(rgb.astype(np.float32) + self.t, 0, 255)
        q = np.clip(np.rint(t * (31 / 255)), 0, 31).astype(np.int32)
        qi = (q[..., 0] << 10) | (q[..., 1] << 5) | q[..., 2]
        import os88cgacomp as cc
        lab = cc.srgb2lab(t).astype(np.float32)
        c0, c1 = self.lut[0][qi], self.lut[1][qi]
        e0, e1 = self.cost(lab, c0), self.cost(lab, c1)
        return lab, c0, c1, e0, e1

    def codes(self, rgb, r):
        if self.mix >= 2:
            return self.pattern_settle(rgb, r)
        lab, c0, c1, e0, e1 = r
        best = np.where(e1 < e0, c1, c0)
        eb = np.minimum(e0, e1)
        if self.prev is not None:
            same = np.where(self.prev >= 256, c1, c0)   # last frame's char
            es = np.where(self.prev >= 256, e1, e0)
            take = es <= eb + self.CHAR_BIAS
            best = np.where(take, same, best)
            eb = np.where(take, es, eb)
            if self.stable:
                ep = self.cost(lab, self.prev)
                best = np.where(ep <= eb + self.stable, self.prev, best)
        self.prev = best
        return best

    def settle(self, rgb, r):
        c = self.codes(rgb, r)
        out = np.empty((c.shape[0], c.shape[1] * 2), np.uint8)
        out[:, 0::2] = np.where(c >= 256, 0x55, 0x13)
        out[:, 1::2] = c & 255
        return out

    def __call__(self, rgb):
        return self.settle(rgb, self.raw(rgb))


PREFER_COLOUR = 1.0     # TextMatcher's --text-prefer-colour


class TextMatcher:
    """TEXT (SPEC.md 98.2.16): each 8 x 8 cell of the picture the character
    and the attribute whose cell, drawn in os88txtfont's model face, is
    nearest it - the picture is made OF the characters, and CLARITY is what
    every choice here is for:
    - The error is taken twice. DOT FOR DOT, in sRGB, says which way an
      edge runs. THROUGH THE EYE - each 2 x 2 quarter of the cell mixed in
      LINEAR light and seen as sRGB - says what TONE a shade reads as: a
      50% dot of black and white looks like 186, not 128, and pricing a
      shade at 128 turned every mid-grey dark. `detail` weights the first.
    - A LETTER EARNS ITS PLACE. A glyph that is neither a space, a shade
      nor a block pays `busy` (RMS, of 255) on top of its error: at
      80 x 25 a letter chosen because it was a hair nearer than a shade is
      noise - the eye reads the letter, not the picture.
    - The picture is SHARPENED first (`sharpen`, an unsharp mask about a
      cell wide): an edge at this size is two or three cells, and a soft
      one is lost between the glyphs.
    - COLOUR is any of sixteen on any of sixteen (blink off): per glyph the
      two colours a least-squares fit asks for, the `near` nearest of the
      sixteen to each, and every pair priced exactly. MONO is the three
      attributes every adapter draws alike, 07h, 0Fh and 70h.
    - A cell keeps last frame's code while it is within `stable` (RMS) of
      the best: a still picture stays still, and a source's noise does not
      make the letters crawl, which is the least clear thing a text
      picture can do.
    Returns the canvas: a row of characters and attributes, the screen's
    own order"""

    GAMMA = 2.2
    PLAIN = (0x20, 0xB0, 0xB1, 0xB2, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF)

    BUSY = {"ascii": 2.0,           # ...where letters are all it has,
            "dots": 2.0,            # ...and where the dots are the point
            "dots-plus": 2.0}
    BUSY_DEFAULT = 12.0

    def __init__(self, cells, rows, colour, glyphs="blocks", stable=6.0,
                 sharpen=0.6, detail=0.5, busy=None, near=2, top=16,
                 prefer=PREFER_COLOUR, ocr_exact=False, ocr_large=False):
        if busy is None:
            busy = self.BUSY.get(glyphs, self.BUSY_DEFAULT)
        self.top = top
        import os88txtfont
        self.codes = np.array(os88txtfont.GLYPH_SETS[glyphs], np.uint8)
        K = len(self.codes)
        g = os88txtfont.bitmaps(self.codes).reshape(K, 64)
        self.g, self.n = g, g.sum(1)
        q = np.zeros((16, 64), np.float32)      # 8 x 8 -> 4 x 4, averaged
        for y in range(8):
            for x in range(8):
                q[(y // 2) * 4 + x // 2, y * 8 + x] = 0.25
        self.q = q
        lv = np.rint(g @ q.T * 4).astype(np.int64)      # (K, 16): 0..4
        self.oh = (lv[:, None, :] == np.arange(5)[None, :, None]).astype(
            np.float32)                                 # (K, 5, 16)
        self.cnt = self.oh.sum(2)                       # (K, 5)
        # a perceptual weight on the channels, luma's, so a green is judged
        # as bright as the eye judges it
        cw = np.sqrt(np.array([0.299, 0.587, 0.114], np.float32) * 3)
        self.cw = np.diag(cw)
        # ...and COLOUR PREFERRED (`prefer`, 98.2.16): that weight leaves
        # blue at a third of green, and dot for dot any dither of a flat
        # area pays its whole contrast - so a pale blue, a waterfall or a
        # sky, which sixteen colours can only draw as light blue mixed into
        # grey or white, came out a plain GREY. Two things, both scaled by
        # `prefer`: the picture's saturation raised by half, so a pale hue
        # reaches for the palette's colour rather than its grey; and two
        # more channels, Cb and Cr weighted 4, in the through-the-eye term
        # and in the choice of which colours are tried, so a grey pays for
        # the hue it drops - while the dot-for-dot term, which says which
        # way an edge runs, stays brightness alone. 0 is the encoder before
        # it; MONO has no hue to prefer
        if not colour:
            prefer = 0
        self.sat = 1 + 0.5 * prefer
        chroma = 4.0 * prefer
        self.ce = self.cw
        if chroma:
            y = np.array([0.299, 0.587, 0.114], np.float32)
            cb = (np.array([0, 0, 1], np.float32) - y) * 0.564
            cr = (np.array([1, 0, 0], np.float32) - y) * 0.713
            self.ce = np.concatenate(
                [self.cw, chroma * np.stack([cb, cr], 1)], 1)   # (3, 5)
        self.back = np.diag(1 / cw) @ self.ce if chroma else None
        srgb = np.frombuffer(vid.STD16, np.uint8).astype(
            np.float32).reshape(16, 3) * (255.0 / 63)
        self.pal = srgb @ self.cw
        self.pale = srgb @ self.ce
        lin = (srgb / 255) ** self.GAMMA
        lvl = np.arange(5, dtype=np.float32) / 4
        mix = lin[None, :, None, :] + lvl[None, None, :, None] * (
            lin[:, None, None, :] - lin[None, :, None, :])   # [f, b, L]
        self.mix = (mix ** (1 / self.GAMMA)) * 255 @ self.ce  # (16,16,5,C)
        self.colour = colour
        self.cells, self.rows = cells, rows
        self.stable, self.sharpen, self.near = stable, sharpen, near
        self.wf, self.wb = detail, (1.0 - detail) * 4   # a quarter: 4 dots
        self.norm = 64 * (self.wf + self.wb / 4)
        self.pen = np.where(np.isin(self.codes, self.PLAIN), 0.0,
                            busy ** 2 * self.norm).astype(np.float32)
        self.prev = None                                # (N,) k, fg, bg
        # OCR'D WORDS (TextOCR): every code, not only the set's, since a
        # word read off the picture is drawn in its own letters
        self.ocr_exact, self.ocr_large = ocr_exact, ocr_large
        self.ocr_used = [0, 0]          # words drawn exact, drawn crisp
        if ocr_exact or ocr_large:
            self.gall = os88txtfont.bitmaps(range(256)).reshape(256, 64)
            self.pairs = np.ones((16, 16), bool)
            np.fill_diagonal(self.pairs, False)
            if not colour:
                self.pairs[:] = False
                for at in vid.TEXT_MONO_ATTRS:
                    self.pairs[at & 15, at >> 4] = True

    def _sharp(self, f):
        if not self.sharpen:
            return f
        k = np.array([1, 4, 6, 4, 1], np.float32) / 16  # 5 taps each way
        b = f
        for ax in (0, 1):
            pad = [(0, 0)] * 3
            pad[ax] = (2, 2)
            p = np.pad(b, pad, mode="edge")
            b = sum(k[i] * np.take(p, range(i, i + f.shape[ax]), axis=ax)
                    for i in range(5))
        return np.clip(f + self.sharpen * (f - b), 0, 255)

    def _err(self, st, k, fi, bi, ai=None):
        """The error of glyph index `k` in colours `fi` on `bi`, each
        (N, X) - both terms and the penalty, exactly. `ai` is each's column
        of st's per-glyph A, when that holds only the prefilter's glyphs"""
        S, SS, GT, P, PP, A = st
        n = np.arange(k.shape[0])[:, None]
        fc, bc = self.pal[fi], self.pal[bi]
        d = fc - bc
        nk = self.n[k]
        ef = (64 * (bc ** 2).sum(-1) - 2 * (bc * S[:, None]).sum(-1) +
              SS[:, None] + 2 * (d * (bc * nk[..., None] - GT[n, k])).sum(-1)
              + nk * (d ** 2).sum(-1))
        mv = self.mix[fi, bi]                           # (N, X, 5, 3)
        av = A[n, k if ai is None else ai]
        eb = ((self.cnt[k] * (mv ** 2).sum(-1)).sum(-1) -
              2 * np.einsum("nxlc,nxlc->nx", mv, av) + PP[:, None])
        return self.wf * ef + self.wb * eb + self.pen[k]

    def raw(self, rgb):
        """Everything of a frame that reads no state (STAGED, below): the
        prefilter's candidates and each priced exactly. `settle` puts last
        frame's glyph among them - repricing only the cells it was not in,
        each cell's price being its own - and chooses"""
        R, C = self.rows, self.cells
        f = self._sharp(rgb.astype(np.float32))
        if self.sat != 1:
            y = f @ np.array([0.299, 0.587, 0.114], np.float32)
            f = np.clip(y[..., None] + self.sat * (f - y[..., None]), 0, 255)
        cells = lambda a: a.reshape(R, 8, C, 8, a.shape[-1]).transpose(
            0, 2, 1, 3, 4).reshape(R * C, 64, a.shape[-1])
        t = cells(f @ self.cw)
        lin = cells((f / 255) ** self.GAMMA)
        P = (np.einsum("qp,npc->nqc", self.q, lin) ** (1 / self.GAMMA)
             * 255 @ self.ce).astype(np.float32)        # (N, 16, C)
        N, K = R * C, len(self.codes)
        S, SS = t.sum(1), (t ** 2).sum((1, 2))
        GT = np.matmul(t.transpose(0, 2, 1), self.g.T).transpose(0, 2, 1)
        # THE PREFILTER: each glyph's error at the two colours it asks for,
        # unquantised and dot for dot - closed form, (N, K) - and the best
        # `top` of them go on to be priced exactly
        on = self.n[None, :]
        e0 = SS[:, None] - (GT ** 2).sum(-1) / np.maximum(on, 1) - \
            ((S[:, None, :] - GT) ** 2).sum(-1) / np.maximum(64 - on, 1) + \
            self.pen[None, :] / max(self.wf, 1e-3)
        top = min(self.top, K)
        ks = np.argpartition(e0, top - 1, axis=1)[:, :top]     # (N, T)
        # (the least likely, which last frame's glyph would stand in for)
        worst = e0[np.arange(N)[:, None], ks].argmax(1)
        st = (S, SS, GT, P, (P ** 2).sum((1, 2)))
        e, fi, bi, A = self._price(st, ks)
        return dict(st=st, ks=ks, worst=worst, e=e, fi=fi, bi=bi, A=A,
                    t=t if self.ocr_exact or self.ocr_large else None)

    def _price(self, st, ks):
        """The glyphs `ks` (N, T) priced exactly in their colours: the
        errors (N, X) and the colours (N, X), and st's per-glyph A"""
        S, SS, GT, P, PP = st
        N, top = ks.shape
        A = np.einsum("ntlq,nqc->ntlc", self.oh[ks], P)        # (N, T, 5, 3)
        ar = np.arange(N)
        cols = np.broadcast_to(np.arange(top), (N, top))
        if self.colour:
            gt = GT[ar[:, None], ks]                            # (N, T, 3)
            on = self.n[ks][..., None]
            fs = gt / np.maximum(on, 1)
            bs = (S[:, None, :] - gt) / np.maximum(64 - on, 1)
            fs = np.where(on > 0, fs, bs)
            bs = np.where(on < 64, bs, fs)
            m, pal = self.near, self.pal
            if self.back is not None:       # the hue counts in the choice
                fs, bs, pal = fs @ self.back, bs @ self.back, self.pale
            df = ((fs[..., None, :] - pal) ** 2).sum(-1)        # (N,T,16)
            db = ((bs[..., None, :] - pal) ** 2).sum(-1)
            fi = np.argpartition(df, m - 1, axis=-1)[..., :m]
            bi = np.argpartition(db, m - 1, axis=-1)[..., :m]
            fi = np.repeat(fi[..., :, None], m, -1).reshape(N, -1)
            bi = np.repeat(bi[..., None, :], m, -2).reshape(N, -1)
            kx = np.repeat(ks, m * m, 1)
            ax = np.repeat(cols, m * m, 1)
        else:
            at = np.array(vid.TEXT_MONO_ATTRS, np.int64)
            kx = np.repeat(ks, len(at), 1)
            ax = np.repeat(cols, len(at), 1)
            fi = np.tile(at & 15, (N, top))
            bi = np.tile(at >> 4, (N, top))
        e = self._err(st + (A,), kx, fi, bi, ax)
        return e, fi, bi, A

    def settle(self, rgb, r, ocr=None):
        R, C = self.rows, self.cells
        ks, e, fi, bi, A = r["ks"], r["e"], r["fi"], r["bi"], r["A"]
        st = r["st"]
        N = ks.shape[0]
        ar = np.arange(N)
        if self.prev is not None:           # ...and last frame's, always,
            pk0 = self.prev[0]              # in place of the least likely
            new = np.nonzero(~(ks == pk0[:, None]).any(1))[0]
            if len(new):
                ks, e, fi, bi, A = (x.copy() for x in (ks, e, fi, bi, A))
                ks[new, r["worst"][new]] = pk0[new]
                e[new], fi[new], bi[new], A[new] = self._price(
                    tuple(x[new] for x in st), ks[new])
        st = st + (A,)
        n = e.shape[1] // ks.shape[1]
        best = e.argmin(1)
        eb, fg, bg = e[ar, best], fi[ar, best], bi[ar, best]
        k = ks[ar, best // n]
        if self.prev is not None and self.stable:
            pk, pf, pb = self.prev
            pa = (ks == pk[:, None]).argmax(1)
            ep = self._err(st, pk[:, None], pf[:, None], pb[:, None],
                           pa[:, None])[:, 0]
            keep = np.sqrt(np.maximum(ep, 0) / self.norm) <= \
                np.sqrt(np.maximum(eb, 0) / self.norm) + self.stable
            k = np.where(keep, pk, k)
            fg = np.where(keep, pf, fg)
            bg = np.where(keep, pb, bg)
        self.prev = (k, fg, bg)
        out = np.empty((R, C * 2), np.uint8)
        out[:, 0::2] = self.codes[k].reshape(R, C)
        out[:, 1::2] = ((bg << 4) | fg).astype(np.uint8).reshape(R, C)
        t, SS = r["t"], st[1]
        if ocr:                         # ...and the words read off it, over
            flat = out.reshape(-1)      # it. prev stays the matcher's own,
            for w in ocr:               # so a word that goes goes cleanly
                d = self._ocr_word(rgb, t, SS, *w)
                if d is not None:
                    idx, codes, attrs, crisp = d
                    flat[idx * 2], flat[idx * 2 + 1] = codes, attrs
                    self.ocr_used[1 if crisp else 0] += 1
        return out

    def __call__(self, rgb, ocr=None):
        return self.settle(rgb, self.raw(rgb), ocr)

    # a character is "about one cell" (--text-ocr) from half a cell to two
    # cells tall, and a third of a cell wide or more - wider is spread one
    # a cell, which a HUD's letters are. Taller is LARGE (--text-ocr-large):
    # half blocks give a cell two dots of height, and a letter under four
    # is a blob however crisp its edges
    OCR_ONE_H = 2.0
    CRISP = (0x20, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF)

    def _ocr_word(self, rgb, t, SS, text, x0, y0, x1, y1, layer):
        """One of TextOCR's words -> (cells, codes, attributes, crisp?), or
        None when it is not this matcher's to draw. The word OWNS the cells
        its box covers. Its INK is what the pass that read it read as ink
        (TextOCR.ink) - or, read off the grey, the side of the box's middle
        brightness with fewer pixels - and the letters are the colour
        nearest the ink's, one for the word; each cell's ground is its own,
        nearest what is not ink there, so the scene stays behind the
        letters. EXACT: the characters at the cells under them, the ground
        between. CRISP: each cell the solid block nearest the ink's shape"""
        R, C = self.rows, self.cells
        n = len(text)
        ch = (y1 - y0) / OCR_CH
        cw = (x1 - x0) / n / OCR_CW
        if ch < 0.5 or cw < 0.35:
            return None
        crisp = ch > self.OCR_ONE_H
        if not (self.ocr_large if crisp else self.ocr_exact):
            return None
        if crisp:                       # the cells whose centre it covers
            c0, c1 = round(x0 / OCR_CW), round(x1 / OCR_CW) - 1
            r0, r1 = round(y0 / OCR_CH), round(y1 / OCR_CH) - 1
        else:                           # ...or that it touches at all
            c0, c1 = int(x0 // OCR_CW), int((x1 - 1) // OCR_CW)
            r0, r1 = int(y0 // OCR_CH), int((y1 - 1) // OCR_CH)
        c0, r0, c1, r1 = max(c0, 0), max(r0, 0), min(c1, C - 1), \
            min(r1, R - 1)
        if c1 < c0 or r1 < r0:
            return None
        cells = rgb.reshape(R, 8, C, 8, 3).transpose(0, 2, 1, 3, 4)[
            r0:r1 + 1, c0:c1 + 1].reshape(-1, 64, 3).astype(np.float32)
        # the INK, inside the box itself (the cells hang over its edges)
        yy = ((np.arange(r0 * 8, (r1 + 1) * 8) + 0.5) * OCR_CH / 8)
        xx = ((np.arange(c0 * 8, (c1 + 1) * 8) + 0.5) * OCR_CW / 8)
        inbox = ((yy >= y0) & (yy < y1))[:, None] & \
            ((xx >= x0) & (xx < x1))[None, :]
        nr, nc = r1 - r0 + 1, c1 - c0 + 1
        inbox = inbox.reshape(nr, 8, nc, 8).transpose(0, 2, 1, 3).reshape(
            -1, 64)
        ink = TextOCR.ink(cells, layer)
        if ink is None:
            y = cells @ np.array([0.299, 0.587, 0.114], np.float32)
            mid = np.median(y[inbox]) if inbox.any() else np.median(y)
            ink = y > mid if (y[inbox] > mid).mean() < 0.5 else y < mid
        ink = ink & inbox
        if ink.sum() < 4 or (~ink).sum() < 4:
            return None
        near = lambda c: (((c @ self.ce) - self.pale) ** 2).sum(-1)
        dl = near(cells[ink].mean(0))
        allg = np.median(cells[~ink], 0)
        codes = np.full(len(cells), 0x20, np.uint8)
        grounds = np.empty((len(cells), 3), np.float32)
        for m in range(len(cells)):
            g = cells[m][~ink[m]]
            grounds[m] = np.median(g, 0) if len(g) >= 8 else allg
        dg = np.stack([near(g) for g in grounds])        # (M, 16)
        # the letters' colour: the one the ink is nearest, with each cell's
        # ground its best beside it
        pk = np.where(self.pairs[None], dl[None, :, None] + dg[:, None, :],
                      np.inf)                            # (M, 16f, 16b)
        f = int(pk.min(-1).sum(0).argmin())
        b = pk[:, f, :].argmin(-1)
        if crisp:                       # the block nearest the ink's shape
            bits = self.gall[list(self.CRISP)] > 0.5         # (G, 64)
            miss = (bits[None] != ink[:, None]).sum(-1)      # (M, G)
            codes = np.array(self.CRISP, np.uint8)[miss.argmin(1)]
        else:
            row = min(max(int((y0 + y1) / 2 // OCR_CH), r0), r1) - r0
            cols = [int((x0 + (i + 0.5) * (x1 - x0) / n) // OCR_CW)
                    for i in range(n)]
            if len(set(cols)) < n:      # narrower than a cell: side by side
                s0 = round((x0 + x1) / 2 / OCR_CW - n / 2)
                cols = list(range(s0, s0 + n))
            for c, t_ in zip(cols, text):
                if c0 <= c <= c1:
                    codes[row * nc + c - c0] = ord(t_)
        rs, cs = np.arange(r0, r1 + 1), np.arange(c0, c1 + 1)
        idx = (rs[:, None] * C + cs[None, :]).reshape(-1)
        return idx, codes, ((b << 4) | f).astype(np.uint8), crisp


# THE PICTURE TEXT READS OFF (--text-ocr): a second copy of the frame, a
# cell eight by twenty - 5:12, the cell's own shape - so a cell-sized
# letter is the twenty-odd pixels tesseract reads best. Twice that read
# fewer of a game's words, in four times the time
OCR_CW, OCR_CH = 8, 20


class TextOCR:
    """Words read off the picture by TESSERACT (a program, not a Python
    package: https://github.com/tesseract-ocr/tesseract), for TextMatcher.

    A frame in `every` is read, `jobs` at once, and the frames between
    carry the last reading. Each reading is FIVE passes over one frame -
    its grey, the pixels over 150 in red, in green and in blue, and those
    over 200 in brightness - because a game's text is a colour on a colour,
    and on the grey alone a yellow word on a blue ground reads as nothing.
    Where passes overlap the most confident word wins. A word is kept at
    `conf` (0..100) or better - a LARGE one at LARGE_CONF - trimmed of the punctuation round it, in
    printable ASCII, with two letters or digits at least - and only once it
    is read in the same place twice running: scenery reads as a scatter of
    confident "or", "il" and "|", which comes and goes where text stays.
    Yields, per frame, [(text, x0, y0, x1, y1, pass)] in the copy's
    pixels"""

    # a LARGE word (TextMatcher.OCR_ONE_H) is drawn from its ink, not its
    # letters, so a misreading costs it nothing: it is kept at this
    LARGE_CONF = 50.0

    def __init__(self, conf=80, every=5, jobs=None):
        self.exe = shutil.which("tesseract")
        if not self.exe:
            raise vid.V88Error("--text-ocr and --text-ocr-large read the "
                               "picture with tesseract, which is not on the "
                               "PATH (https://github.com/tesseract-ocr/"
                               "tesseract; apt install tesseract-ocr, brew "
                               "install tesseract)")
        self.conf, self.every = conf, max(1, every)
        self.jobs = max(1, jobs or os.cpu_count() or 1)

    NLAYERS = 5

    @staticmethod
    def ink(rgb, layer):
        """The pixels pass `layer` reads as INK (1..4), a bool mask - or
        None for pass 0, the grey, whose ink is either side of it"""
        rgb = rgb.astype(np.float32)
        if layer == 0:
            return None
        if layer == 4:
            return rgb @ np.array([0.299, 0.587, 0.114], np.float32) > 200
        return rgb[..., layer - 1] > 150

    @classmethod
    def layers(cls, rgb):
        y = rgb.astype(np.float32) @ np.array([0.299, 0.587, 0.114],
                                               np.float32)
        return [y.astype(np.uint8)] + [
            np.where(cls.ink(rgb, n), 0, 255).astype(np.uint8)
            for n in range(1, cls.NLAYERS)]

    def pass_(self, grey):
        h, w = grey.shape
        pgm = b"P5\n%d %d\n255\n" % (w, h) + grey.tobytes()
        env = dict(os.environ, OMP_THREAD_LIMIT="1")
        r = subprocess.run([self.exe, "stdin", "stdout", "--psm", "11",
                            "-l", "eng", "tsv"], input=pgm, env=env,
                           capture_output=True)
        out = []
        for line in r.stdout.decode("utf-8", "replace").splitlines()[1:]:
            c = line.split("\t")
            if len(c) < 12 or c[0] != "5":
                continue
            word = c[11].strip()
            try:
                conf = float(c[10])
                x, y, bw, bh = (int(v) for v in c[6:10])
            except ValueError:
                continue
            big = bh / OCR_CH > TextMatcher.OCR_ONE_H
            if conf < (min(self.conf, self.LARGE_CONF) if big
                       else self.conf) or not word:
                continue
            # trimmed of what surrounds it - "SCORE," is SCORE - and the
            # box with it, a character's share a character
            a = 0
            while a < len(word) and not word[a].isalnum():
                a += 1
            e = len(word)
            while e > a and not word[e - 1].isalnum():
                e -= 1
            cw = bw / len(word)
            core = word[a:e]
            # ...and a LARGE one three: at its lower confidence a big
            # scatter of scenery reads as "al" and "il"
            if sum(ch.isalnum() for ch in core) < (3 if big else 2) or \
                    any(not 33 <= ord(ch) <= 126 for ch in core):
                continue
            out.append((conf, core, round(x + a * cw), y,
                        round(x + e * cw), y + bh))
        return out

    @staticmethod
    def _over(p, q):
        """How much two boxes overlap, of the smaller"""
        ix = min(p[2], q[2]) - max(p[0], q[0])
        iy = min(p[3], q[3]) - max(p[1], q[1])
        if ix <= 0 or iy <= 0:
            return 0.0
        area = lambda b: (b[2] - b[0]) * (b[3] - b[1])
        return ix * iy / max(1, min(area(p), area(q)))

    def read(self, rgb):
        found = sorted((w + (n,) for n, g in enumerate(self.layers(rgb))
                        for w in self.pass_(g)),
                       key=lambda w: (-w[0], -len(w[1])))
        kept = []
        for w in found:
            hit = [i for i, k in enumerate(kept)
                   if self._over(w[2:6], k[2:6]) >= 0.5]
            if not hit:
                kept.append(w)
            elif len(hit) == 1 and kept[hit[0]][1] == w[1] and \
                    w[5] - w[3] < kept[hit[0]][5] - kept[hit[0]][3]:
                kept[hit[0]] = w        # the same word, boxed tighter: a
        return [w[1:] for w in kept]    # pass that ran it into the next
        #                                 line's ink made it look TALL

    def words(self, frames):
        from collections import deque
        from concurrent.futures import ThreadPoolExecutor
        pool = ThreadPoolExecutor(self.jobs)
        q, last, prev = deque(), [], []
        depth = self.jobs * self.every + 1

        def took(fut):
            nonlocal last, prev
            if fut is not None:
                now = fut.result()
                last = [w for w in now if any(
                    p[0] == w[0] and self._over(p[1:5], w[1:5]) >= 0.5
                    for p in prev)]
                prev = now
            return last
        try:
            for i, f in enumerate(frames):
                q.append(pool.submit(self.read, f) if i % self.every == 0
                         else None)
                while len(q) > depth:
                    yield took(q.popleft())
            while q:
                yield took(q.popleft())
        finally:
            pool.shutdown(wait=True, cancel_futures=True)


def cga4_pick(frames, bg=None, pal=None, bright=None):
    """CGA4's palette byte (98.1.3.3): the one of the 96 - a background of
    sixteen, three sets, two intensities - whose four colours are nearest
    the clip's pixels on average (a sample of them), any of the three
    given fixing its part of the choice"""
    std = np.frombuffer(vid.STD16, np.uint8).astype(np.float32).reshape(
        16, 3) * (255.0 / 63)
    step = max(1, len(frames) // 24)
    px = np.concatenate([f[::2, ::2].reshape(-1, 3) for f in
                         frames[::step]]).astype(np.float32)
    if len(px) > 200000:
        px = px[::len(px) // 200000 + 1]
    # each sample's distance to each of the sixteen, once: a palette's
    # four are then columns of it, not 200,000 distances again each
    dd = np.stack([((px - c) ** 2).sum(1) for c in std], 1)
    best = None
    for sel in range(0x80):
        if (sel & 0x60) == 0x60:
            continue
        p = 2 if sel & 0x40 else (sel >> 5) & 1
        if (bg is not None and sel & 15 != bg) or \
                (pal is not None and p != pal) or \
                (bright is not None and bool(sel & 16) != bool(bright)):
            continue
        d = dd[:, list(vid.cga4_colours(sel))].min(1).mean()
        if best is None or d < best[0]:
            best = (d, sel)
    if best is None:
        raise vid.V88Error("no CGA4 palette meets the overrides")
    return best[1]


def pack_pixels(idx, ppb):
    """Palette indexes (h, w) packed ppb to a byte, the leftmost highest"""
    h, w = idx.shape
    bits = 8 // ppb
    v = idx.reshape(h, w // ppb, ppb).astype(np.uint8)
    out = np.zeros((h, w // ppb), np.uint8)
    for i in range(ppb):
        out |= v[:, :, i] << (8 - bits * (i + 1))
    return out


def run_polled(cmd, poll=None):
    """subprocess.run(cmd, check=True), calling `poll()` every fifth of a
    second while it runs: an exception from it (a cancel) kills the
    command rather than waiting out a pass over the whole clip"""
    p = subprocess.Popen(cmd)
    try:
        while True:
            try:
                p.wait(0.2)
                break
            except subprocess.TimeoutExpired:
                if poll:
                    poll()
    finally:
        if p.poll() is None:
            p.kill()
            p.wait()
    if p.returncode:
        raise subprocess.CalledProcessError(p.returncode, cmd)


# A SCENE (98.1.3.2.3) is cut where the content rule fires - the mean
# change of hue, saturation and value between two frames past SCENE_HSV,
# PySceneDetect's rule - at every SHOT cut, the place a palette moves
# unseen (VIDEO-PALETTE-PLAN 1, the owner's ruling). `--scene-cover` adds
# a second test, that a share of the picture changed by more than
# SCENE_STEP levels, so a reverse angle on the same room keeps its colours
# - and at 0.5 it merged five dark shots of Gas Gas Gas into one 17-second
# scene whose sixteen were spent on tail lights the next shot did not have
# (VIDEO-PALETTE-PLAN 11.1): dark shots differ by small absolute amounts.
# No scene is shorter than SCENE_MIN frames, and a palette a scene's pixels
# are a SCENE_PIX sample of it
SCENE_HSV = 27.0
SCENE_STEP = 24
SCENE_MIN = 15          # (half a second at 30: 6 until 2026-10-14, when a
                        # fast camera read as a cut every 6 frames and each
                        # new palette flashed - VIDEO-PALETTE-PLAN 10.10)
SCENE_PIX = 150000


def _hsv(f):
    f = f.astype(np.float32) / 255
    mx, mn = f.max(2), f.min(2)
    c = mx - mn
    s = np.where(mx > 0, c / np.maximum(mx, 1e-6), 0)
    r, g, b = f[..., 0], f[..., 1], f[..., 2]
    cc = np.maximum(c, 1e-6)
    hh = np.where(mx == r, ((g - b) / cc) % 6,
                  np.where(mx == g, (b - r) / cc + 2, (r - g) / cc + 4)) / 6
    hh = np.where(c > 0, hh, 0)
    return np.stack([hh, s, mx], -1) * 255


def scene_pass(src, w, h, crop, fps_expr, start, end, eq, detect=True,
               cap=SCENE_PIX, cover=0.0, least=SCENE_MIN, count=None):
    """The clip's SCENES, read once and streamed: [(first frame, pixels)],
    the pixels an (N, 3) uint8 sample of the scene - every fourth row and
    column of every frame, halved while past SCENE_PIX x 8 - for its
    palette. With detect False, one scene: the whole clip's sample.
    `count(n)`, if given, is told each frame read (the progress)"""
    scenes, cur, prev, last = [], [], None, 0
    for n, f in enumerate(ffmpeg_video(src, w, h, crop, fps_expr, start, end,
                                       eq, pix="rgb24")):
        if count is not None:
            count(n + 1)
        sm = f[::4, ::4]
        if detect:
            hv = _hsv(sm)
            if prev is not None and n - last >= least:
                d = np.abs(hv - prev[0]).mean()
                cov = (np.abs(sm.astype(np.int16) - prev[1]).mean(2)
                       > SCENE_STEP).mean()
                if d > SCENE_HSV and cov >= cover:
                    scenes.append((last, np.concatenate(cur)))
                    cur, last = [], n
            prev = (hv, sm.astype(np.int16))
        cur.append(sm.reshape(-1, 3))
        if sum(len(c) for c in cur) > SCENE_PIX * 8:
            cur = [np.concatenate(cur)[::2]]
    if not cur and not scenes:
        raise vid.V88Error("no frames came out of %s" % src)
    if cur:
        scenes.append((last, np.concatenate(cur)))
    return [(s, px if len(px) <= cap else px[::len(px) // cap + 1])
            for s, px in scenes]


def colour_kmeans(px, colours=16):
    """`colours` for the pixels `px` ((N, 3) sRGB), chosen for COLOUR
    (VIDEO-PALETTE-PLAN 10.1), as the DAC's six bits, black first and the
    rest darkest first, padded to 256 entries as vga8_palette's are. A
    pattern dither reaches the hull of its palette, so a colour no entry is
    near can never be drawn: greys can be dithered into existence and
    colours cannot. palettegen places entries by how many pixels have a
    colour, so a dark scene's sixteen are a grey ramp and its red headlights
    go grey; this is k-means in CIELAB, each pixel weighted 1 + C*/8, seeded
    k-means++ by the same weight, black fixed. On the owner's Gas Gas Gas it
    kept the EGA's colour reach (7.2% of the chromatic pixels drawn at under
    half their chroma, palettegen's 55.7%) with half its chroma error - and
    asked for FEWER bytes than palettegen's sixteen, its colours sitting
    further apart. Deterministic: the seeding's generator is fixed"""
    import os88cgacomp as cc
    lab = cc.srgb2lab(np.asarray(px, np.float64))
    wt = 1 + np.hypot(lab[:, 1], lab[:, 2]) / 8
    rng = np.random.default_rng(1)
    C = [np.zeros(3)]
    near = ((lab - C[0]) ** 2).sum(1)
    for _ in range(colours - 1):
        d = near * wt
        if not d.sum():             # (a scene of one colour: black)
            C.append(C[0])
            continue
        C.append(lab[rng.choice(len(lab), p=d / d.sum())])
        near = np.minimum(near, ((lab - C[-1]) ** 2).sum(1))
    C = np.array(C)
    for _ in range(12):
        a = np.argmin(((lab[:, None] - C[None]) ** 2).sum(2), 1)
        for k in range(1, colours):
            m = a == k
            if m.any():
                C[k] = (lab[m] * wt[m, None]).sum(0) / wt[m].sum()
    # CIELAB back to sRGB (D65), then to the DAC's six bits
    fy = (C[:, 0] + 16) / 116
    fx, fz = fy + C[:, 1] / 500, fy - C[:, 2] / 200
    fi = lambda f: np.where(f ** 3 > 0.008856, f ** 3,
                            (f - 16 / 116) / 7.787)
    xyz = np.stack([fi(fx) * 0.95047, fi(fy), fi(fz) * 1.08883], -1)
    m = np.linalg.inv(np.array([[0.4124, 0.3576, 0.1805],
                                [0.2126, 0.7152, 0.0722],
                                [0.0193, 0.1192, 0.9505]]))
    c = np.clip(xyz @ m.T, 0, 1)
    c = np.where(c > 0.0031308, 1.055 * c ** (1 / 2.4) - 0.055, 12.92 * c)
    rgb = np.clip(np.rint(c * 63), 0, 63).astype(int)
    cols = sorted({tuple(int(x) for x in v) for v in rgb[1:]} - {(0, 0, 0)},
                  key=lambda c: (77 * c[0] + 150 * c[1] + 29 * c[2], c))
    cols = [(0, 0, 0)] + cols
    cols += [cols[-1]] * (256 - len(cols))
    return bytes(v for c in cols[:256] for v in c)


def colour_palette(src, w, h, crop, fps_expr, start, end, eq, colours=16,
                   count=None):
    """The clip's `colours` chosen for colour: colour_kmeans over the whole
    clip's sample (scene_pass, one scene), streamed; `count`, scene_pass's"""
    return colour_kmeans(scene_pass(src, w, h, crop, fps_expr, start, end,
                                    eq, detect=False, cap=400000,
                                    count=count)[0][1], colours)


class SceneDitherer:
    """A PALETTE A SCENE (98.1.3.2.3): Knoll's dither over each scene's own
    sixteen. An item is (frame, rgb), and the frame picks the scene; a
    scene's ditherer is fresh at its first frame - nothing held from the
    last scene's colours, so the cut is drawn in the new ones from nothing.
    The scenes' threshold cell and map are one array shared, so the pool's
    copy of this is one cell and a 32 KB table a scene"""

    def __init__(self, scenes, carry=False, codes=None):
        self.starts = [s for s, _ in scenes]
        self.ds = [d for _, d in scenes]
        for d in self.ds[1:]:
            d.cell, d.t = self.ds[0].cell, self.ds[0].t
        self.carry = carry              # --scene-hold: see settle
        self.last = None
        # (scenes of DIFFERENT counts, the rate ladder's: each scene's
        # colour k is pixel value codes[scene][k], VIDEO-PALETTE-PLAN 10.12)
        self.codes = codes

    def which(self, n):
        import bisect
        return bisect.bisect_right(self.starts, n) - 1

    def raw(self, item):
        n, f = item
        return self.ds[self.which(n)].raw(f)

    def settle(self, item, r):
        n, f = item
        k = self.which(n)
        if self.carry and self.last is not None and k != self.last and (
                self.codes is None or
                len(self.codes[k]) == len(self.codes[self.last])):
            # THE HOLD ACROSS A CUT (--scene-hold): the new scene's ditherer
            # starts from the last one's screen rather than from nothing. A
            # pixel's value is its slot, so under the new sixteen it means
            # the new colour, and --vga4-hold keeps every pixel whose slot's
            # NEW colour is already near the new source: fewer bytes at the
            # cut and fewer of its frames cut, at the price of what the
            # owner saw - the last scene's pattern standing in the new one
            # and fading out over its frames (docs/reports/VIDEO-ORDER-2026-10-13.md).
            # (Not between scenes of different counts under --step-down: a
            # slot of one means nothing in the other)
            a, b = self.ds[self.last], self.ds[k]
            if b.prev is None and a.prev is not None:
                b.prev, b.at, b.score = a.prev, a.at, None
        self.last = k
        v = self.ds[k].settle(f, r)
        return self.codes[k][v] if self.codes is not None else v


ORDER_JUMP = 25.0       # --vga4-order planes: a pixel the palette switch
                        # moves further than this (CIELAB) is a flash, and no
                        # cut may make more of them than darkest first does
ORDER_GUARD = 0.2       # ...and each dE x pixel of the switch's move costs
                        # this many plane writes, so like stays near like
ORDER_SAMPLES = 12      # frame pairs a scene keeps to choose its order on
ORDER_ROWS = 4          # ...counted on every 4th row of each


def _planes16(ix):
    return np.stack([np.packbits((ix >> p) & 1, axis=-1) for p in range(4)])


def _stores16(new, old):
    """vga4_subs' stores, label maps `new` over `old`: at a byte where a
    plane changes, the planes that want one value are one store"""
    tp, sp = _planes16(new), _planes16(old)
    ch = tp != sp
    n = 0
    for p in range(4):
        dup = np.zeros(tp.shape[1:], bool)
        for q in range(p):
            dup |= ch[q] & (tp[q] == tp[p])
        n += int((ch[p] & ~dup).sum())
    return n


def plane_orders(outs, starts, pals):
    """THE ORDER OF A SCENE'S SIXTEEN (--vga4-order planes): for each scene,
    the slot each of its colours takes, slot 0 kept black - [sig] with
    sig[colour] = slot. `outs` is the dither's (source frame, colour map)
    for the whole clip, in darkest-first colours; `starts` and `pals` the
    scenes' first frames and sixteen (the DAC's six bits).

    A pixel's value is its slot and its four bits are the planes, so the
    order decides how many planes a change writes, and darkest first puts
    the lightness neighbours a pattern dither moves between at the far
    corners of the cube (3/4, 7/8). The dither picks by colour, never by
    slot, so an order is the same picture in other slots. Chosen by a local
    search over swaps: the planes the scene's own changes write
    (popcount(slot a ^ slot b) a change), the planes its cut from the last
    scene's screen writes, and ORDER_GUARD for how far the switch moves the
    last screen's colours - under a hard rule that no cut moves more pixels
    past ORDER_JUMP than darkest first would. That rule is the flash the
    owner saw in an order guarded only on the AVERAGE move: it matches most
    slots nearly exactly and pays with a few that land far away, and those
    pixels jump. Kept only if exact stores on sampled pairs say it beats
    the order the cut alone would choose (docs/reports/VIDEO-ORDER-*)"""
    import os88cgacomp as cc
    pop = np.array([bin(i).count("1") for i in range(16)])
    labs = [cc.srgb2lab(np.frombuffer(p, np.uint8)[:48].reshape(16, 3)
                        .astype(np.float64) * (255 / 63)) for p in pals]
    ident = np.arange(16)
    sigs = []
    st = dict(k=-1, before=None)

    def search(cost, sig):
        c = cost(sig)
        while True:
            best = None
            for a in range(1, 16):
                for b in range(a + 1, 16):
                    q = sig.copy()
                    q[a], q[b] = q[b], q[a]
                    cq = cost(q)
                    if cq < c:
                        c, best = cq, q
            if best is None:
                return sig
            sig = best

    def close():
        k = st["k"]
        T, pairs, first = st["T"], st["pairs"], st["first"]
        prev = sigs[-1] if sigs else ident
        C = np.zeros((16, 16), np.int64)
        J = np.zeros((16, 16))
        n = np.zeros(16, np.int64)
        lim = 0
        last = st["before"]
        if k and last is not None:
            np.add.at(C, (last.ravel(), first.ravel()), 1)
            n = C.sum(1)
            J = np.sqrt(((labs[k][None] - labs[k - 1][:, None]) ** 2).sum(-1))
            lim = int((n * (J[ident, ident] > ORDER_JUMP)).sum())
        rows = np.arange(16)

        def jump(q):                    # (pixels past the rule, the move)
            jj = J[rows, np.argsort(q)[prev]]
            return max(0, int((n * (jj > ORDER_JUMP)).sum()) - lim), \
                ORDER_GUARD * (n * jj).sum()

        def cut(q):
            over, g = jump(q)
            return over, g + (C * pop[prev[:, None] ^ q[None, :]]).sum()

        def full(q):
            over, c = cut(q)
            return over, c + (T * pop[q[:, None] ^ q[None, :]]).sum()

        def part(q):
            return cut(q)

        # the last scene's order is where to start: each old value then
        # shows the colour darkest first would have shown it in, so no cut
        # starts past the rule, and the search never accepts a step past it
        safe = search(part, prev.copy())
        if not pairs:
            sigs.append(safe)
            return
        # THE SEARCH IS ON STORES, not on plane flips: the player stores the
        # planes that want one value as ONE write, so darkest first's
        # all-four-plane changes (7 <-> 8) often cost two stores, and an
        # order that halves the flips can cost more stores (a 3 s test:
        # -8.5% flips, +1.1% stores). Each sampled pair counted on every
        # ORDER_ROWS-th row, which is the same count's estimate for a
        # quarter of the work
        sub = [(a[::ORDER_ROWS], b[::ORDER_ROWS]) for a, b in pairs]
        scale = st["count"] / len(pairs) * ORDER_ROWS
        cf = first[::ORDER_ROWS]
        cl = last[::ORDER_ROWS] if k and last is not None else None

        def ex(q):
            over, c = jump(q)
            c += sum(_stores16(q[b], q[a]) for a, b in sub) * scale
            if cl is not None:
                c += _stores16(q[cf], prev[cl]) * ORDER_ROWS
            return over, c

        flips = search(full, safe.copy())
        sig = min((search(ex, q.copy()) for q in (safe, flips)), key=ex)
        sigs.append(sig)

    for n_, m in outs:
        m = np.asarray(m, np.uint8)
        import bisect
        k = bisect.bisect_right(starts, n_) - 1
        if k != st["k"]:
            if st["k"] >= 0:
                close()
                st["before"] = st["prev"]   # the screen the cut leaves
            while len(sigs) < k:            # (a scene no frame reached)
                sigs.append(np.arange(16))
            st.update(k=k, T=np.zeros((16, 16), np.int64), pairs=[],
                      first=m, prev=m, count=0)
            continue
        p = st["prev"]
        ch = p != m
        if ch.any():
            np.add.at(st["T"], (p[ch], m[ch]), 1)
        st["count"] += 1
        if st["count"] % 6 == 1 and len(st["pairs"]) < ORDER_SAMPLES:
            st["pairs"].append((p, m))
        st["prev"] = m
    if st["k"] >= 0:
        close()
    while len(sigs) < len(starts):          # (a scene no frame reached)
        sigs.append(ident)
    return [[int(v) for v in s] for s in sigs]


COARSE_CELL = "auto"    # --coarse-cut's default: the finest cell the
                        # budgets draw whole (COARSE_LADDER). It was 64x8,
                        # the owner's pick of 16x8 and 64x8 on Gas Gas Gas -
                        # the corner scenes that were mush have content, the
                        # transitions "block in" instead of "chop chop", and
                        # the switch flashes less


def coarse_cell(v):
    """--coarse-cut: a cell WxH (dims' spellings), auto, or off"""
    if str(v).strip().lower() in ("off", "none", "0", ""):
        return "off"
    if str(v).strip().lower() == "auto":
        return "auto"
    return dims(v)


# --coarse-cut auto (98.2.1.9, the ladder's first rung - the owner: "64x8
# when needed, or 16x4 or somesuch when we can afford that"): a scene's
# first frame tried at each of these in turn, the first that the budgets
# draw WHOLE taken, and the last when none is
COARSE_LADDER = ("full", "16x4", "32x8", "64x8")
# ...and on MODE X (the owner, 2026-10-17: "is there any reason that
# doesn't apply to other display types?"). The cells pay where the screen
# is PLANAR and a flat byte is one store for many pixels: VGA4's write mode
# 3 (two stores by planes without it) and Mode X's Map Mask 0Fh, an aligned
# group of four pixels of one colour a store (vid.modex_subs). On Mode X
# the whole gain is the group's - Last Exile's scene changes drew in 32-51%
# of their cycles at 4x1, and in exactly as many at 16x4, 32x8 and 64x8 -
# so its ladder is one cell, the finest. On a PACKED screen a flat byte is
# only a RUN, 13 cycles a byte against a slice's 18: Take On Me's Hercules
# scene changes took 87-100% of their cycles in cells, so none there
COARSE_LADDER_MX = ("full", "4x1")


def coarse_screen(g):
    """Do coarse cells pay on this screen? - a planar one's (VGA4, Mode X)"""
    return g.layout == vid.LAY_MODEX or bool(getattr(g, "bitplanes", False))


def coarse_of(e, target, cell, au=b""):
    """`target` in coarse cells of the encoder's colours: VGA4's sixteen,
    Mode X's whole palette - or, for MIX, only the tiles it must be"""
    if cell == "mix":
        m = coarse_mix(e, target, au)
        return target if m is None else m
    return coarse_target(target, e.pal, cell, len(e.pal)
                         if e.g.layout == vid.LAY_MODEX else 16)


# A MIXED SCREEN (the owner, 2026-10-17: "only coursifying the parts that
# can't be afforded until it fits, instead of the full frames"): the frame
# in tiles, and the fewest of them drawn in cells - those whose cells save
# most first - that lets the rest be drawn whole at full detail
MIX_TILE = (32, 8)                      # (W, H) pixels: Mode X's 8 groups


def coarse_mix(e, target, au):
    """`target` with just enough of its tiles in the screen's finest cell
    (Mode X's 4x1, VGA4's 16x4) that the budgets draw the frame whole: a
    binary search on copies of the encoder over the tiles ranked by what
    their cells save - the pixels to change at full detail less those in
    cells. None where the frame fits whole as it is; where even every tile
    in cells does not fit, the whole frame in what `auto` picks - the
    finest cell that does, else the coarsest (the owner: "try mix, and if
    that still cuts, try full" - so mix IS the two stacked, a frame at a
    time)"""
    if not vfr_cut(e, target, au):
        return None
    cell = COARSE_LADDER_MX[1] if e.g.layout == vid.LAY_MODEX else \
        COARSE_LADDER[1]
    c = coarse_target(target, e.pal, cell, len(e.pal)
                      if e.g.layout == vid.LAY_MODEX else 16)
    scr = e.screen.reshape(target.shape)
    tw, th = MIX_TILE
    h, w = target.shape
    ny, nx = -(-h // th), -(-w // tw)

    def tiles(a):
        p = np.pad(a, ((0, ny * th - h), (0, nx * tw - w)))
        return p.reshape(ny, th, nx, tw).sum((1, 3)).ravel()
    save = tiles(target != scr).astype(np.int64) - tiles(c != scr)
    order = [i for i in np.argsort(-save, kind="stable") if save[i] > 0]

    def mixed(k):
        m = np.zeros(ny * nx, bool)
        m[order[:k]] = True
        m = np.repeat(np.repeat(m.reshape(ny, nx), th, 0), tw, 1)[:h, :w]
        return np.where(m, c, target).astype(target.dtype)
    def whole():                        # (auto's whole-frame cell)
        k = coarse_pick(e, target, au)
        return c if k in ("full", cell) else coarse_of(e, target, k)
    if not order:
        return whole()
    lo, hi = 1, len(order)
    if vfr_cut(e, mixed(hi), au):
        return whole()
    while lo < hi:                      # (the fewest tiles that fit)
        mid = (lo + hi) // 2
        if vfr_cut(e, mixed(mid), au):
            lo = mid + 1
        else:
            hi = mid
    return mixed(lo)


def enc_clone(enc):
    """A copy of an encoder to try a frame on (PageAhead's way: numpy views
    do not survive a deep copy)"""
    import copy
    h = copy.deepcopy(enc)
    if hasattr(h, "sv"):
        h.sv = np.frombuffer(h.surf, dtype=np.uint8)
        h.tv = np.frombuffer(h.tsurf, dtype=np.uint8)
    h.metric = False
    return h


def coarse_pick(enc, target, au):
    """--coarse-cut auto: the finest of COARSE_LADDER whose frame the
    budgets draw whole - tried on a copy of the encoder, so nothing it
    spends is spent"""
    lad = COARSE_LADDER_MX if enc.g.layout == vid.LAY_MODEX else \
        COARSE_LADDER
    for cell in lad[:-1]:
        t = target if cell == "full" else coarse_of(enc, target, cell)
        e = enc_clone(enc)
        e.probe = True
        if cell != "full":
            e.all3 = getattr(e, "wm3", False)
        c0 = e.stats["cut"]
        e.frame(t, au)
        if e.stats["cut"] == c0:
            return cell
    return lad[-1]


def coarse_target(target, pal, cell, n=16):
    """A COARSE CUT (--coarse-cut WxH): a scene's first frame as flat cells,
    each W x H of the picture the palette colour nearest the cell's mean.
    The new scene lands WHOLE in one frame - flat cells are long runs, and
    a 640-wide canvas's rows are one 80-byte line after another, so a run
    goes on past a row's end - and the frames after it put the detail in as
    any cut frame's remainder is. The picture is measured against the real
    target, so what the cells cost the eye is in the report"""
    cw, ch = (int(v) for v in cell.split("x"))
    h, w = target.shape
    ph, pw = -h % ch, -w % cw
    t = np.pad(target, ((0, ph), (0, pw)), mode="edge")
    rgb = np.asarray(pal, np.float32)[:n][t]
    m = rgb.reshape(t.shape[0] // ch, ch, t.shape[1] // cw, cw, 3) \
        .mean((1, 3))
    # (the nearest by |m|^2 - 2 m.p + |p|^2, one matrix product: the
    # difference array was 0.65 s a frame on Mode X's 256 - the speed pass.
    # In float64, where it is the exact nearest on every cell: the float32
    # difference array before it picked a farther colour by rounding on
    # 0.2% of Last Exile's cells)
    p = np.asarray(pal, np.float64)[:n]
    mm = m.reshape(-1, 3).astype(np.float64)
    d = (p * p).sum(1)[None, :] - 2.0 * (mm @ p.T)
    k = d.argmin(-1).reshape(m.shape[:2]).astype(target.dtype)
    out = np.repeat(np.repeat(k, ch, 0), cw, 1)[:h, :w]
    return np.ascontiguousarray(out)


def order_palette(p, sig):
    """A palette's first sixteen put in an order: colour j to slot sig[j]"""
    out = [None] * 16
    for j in range(16):
        out[sig[j]] = p[3 * j:3 * j + 3]
    return b"".join(out) + p[48:]


def vga8_palette(src, w, h, crop, fps_expr, start, end, eq, poll=None,
                 colours=256):
    """The clip's `colours` (ffmpeg's palettegen over every frame), as
    the DAC's six bits, the DARKEST first: index 0 is what the screen and
    a keyframe start from, so the bars stay black and a keyframe writes
    only what is not"""
    import tempfile
    vf = []
    if crop:
        vf.append("crop='if(gt(dar,%f),ih*%f*sar,iw)':'if(gt(dar,%f),ih,"
                  "iw/(%f)/sar)'" % (crop, crop, crop, crop))
    vf += ["fps=%s" % fps_expr, "scale=%d:%d:flags=area" % (w, h)]
    if eq:
        vf.append("eq=%s" % eq)
    vf.append("palettegen=max_colors=%d:stats_mode=full:"
              "reserve_transparent=0" % max(colours, 4))
    with tempfile.TemporaryDirectory() as t:
        out = os.path.join(t, "pal.png")
        cmd = ["ffmpeg", "-v", "error", "-nostdin", "-y"]
        if start:
            cmd += ["-ss", "%.3f" % start]
        # (-t ON THE INPUT: palettegen writes its one frame at the end of
        # its input, so an OUTPUT -t - ffmpeg_video's, where frames flow -
        # never fired, and every clip's palette was its start to the END
        # OF THE FILE: 28 s and the closing credits' colours for a 4 s
        # clip of Last Exile - the encoder speed pass, 2026-10-17)
        if end:
            cmd += ["-t", "%.3f" % (end - (start or 0))]
        cmd += ["-i", src]
        cmd += ["-an", "-vf", ",".join(vf), "-frames:v", "1", out]
        run_polled(cmd, poll)
        from PIL import Image
        rgb = np.array(Image.open(out).convert("RGB")).reshape(-1, 3)
    cols = sorted({tuple((int(v) * 63 + 127) // 255 for v in c) for c in rgb},
                  key=lambda c: (77 * c[0] + 150 * c[1] + 29 * c[2], c))
    def merge():                    # the two nearest colours made one
        a = np.array(cols, np.int32)    # (the later goes: black, first,
        d = ((a[:, None] - a[None]) ** 2).sum(2)   # never does)
        np.fill_diagonal(d, 1 << 30)
        i, j = np.unravel_index(int(d.argmin()), d.shape)
        del cols[max(i, j)]
    if cols[0] != (0, 0, 0):        # TRUE black, whatever the clip holds:
        if len(cols) >= colours:    # it is the screen round the canvas too.
            merge()                 # Room is made by merging the two nearest
        cols.insert(0, (0, 0, 0))
    while len(cols) > colours:      # (palettegen's floor is 4 colours)
        merge()
    cols += [cols[-1]] * (256 - len(cols))
    return bytes(v for c in cols[:256] for v in c)


def scaled_aspect(asp, dh):
    """A canvas pixel's aspect when each row is shown `dh` times"""
    n, d = asp[0], asp[1] * dh
    k = math.gcd(n, d)
    return n // k, d // k


def profile_table(prof, layout):
    """(the cost table, a sub-record's OUT) an encode prices its records
    with: the PROFILE's own when it measured one (`cyc_us`, 98.2.3.3) and
    the layout's decoder is the one VIDBENCH timed - every layout but the
    text screens', which have CYC_LAYOUT tables of their own - and wave 0's
    model otherwise. A measured table is in microseconds on the machine and
    is turned into model cycles at its `speed`, so a share of the period is
    still cycles over `speed` periods of an 8088"""
    us = prof.get("cyc_us")
    if not us or layout in vid.CYC_LAYOUT:
        return vid.cyc_table(layout), vid.CYC_SUB
    k = vid.HZ / 1e6 * (prof.get("speed") or 1)
    return ((us["frame"] * k, us["seg"] * k, us["abs"] * k,
             tuple(x * k for x in us["p"]),
             (us["slice"][0] * k, us["slice"][1] * k),
             (us["run"][0] * k, us["run"][1] * k)), us["sub"] * k)


WM3_US_286 = 2.05      # a write mode 3 byte's microseconds past a plain
                        # store's on the owner's 286 (PVGA1A): an AND-store
                        # loop 3.20 us a byte against rep movsb's 1.15
                        # (docs/reports/VM3BENCH-PVGA286-2026-10-13.md)


def wm3_cost(prof):
    """A write mode 3 byte's extra model cycles on the profile's machine
    (98.1.3.2.4): measured on the 286 (VM3BENCH) and carried to any
    profile with its own table at its speed - the same card's read is the
    thing it prices - and wave 0's model otherwise"""
    if prof.get("cyc_us"):
        return WM3_US_286 * vid.HZ / 1e6 * (prof.get("speed") or 1)
    return vid.WM3_BYTE


def span_cost(bs, run, layout=None, table=None):
    """(cycles, bytes) of one span in the stream, wave 0's model: its
    list's entry and a skip byte (a segment's set-up amortised in)"""
    fr, sg, ab, cp, csl, crn = vid.cyc_table(layout) if table is None \
        else table
    n = len(bs)
    if run or (n >= 7 and bs.count(bs[:1]) == n):
        c = crn[0] + crn[1] * n
        b = 2 + (2 if n < 256 else 3)
    elif n <= 6:
        c = cp[n - 1]
        b = 1 + n
    else:
        c = csl[0] + csl[1] * n
        b = 1 + n + (1 if n < 256 else 2)
    return c + 20, b


def span_costs(pairs, layout=None, table=None):
    """[span_cost(bs, run, layout, table) for bs, run in pairs], the model's
    table fetched once rather than once a span - the same sums"""
    fr, sg, ab, cp, csl, crn = vid.cyc_table(layout) if table is None \
        else table
    r0, r1 = crn
    s0, s1 = csl
    out = []
    add = out.append
    for bs, run in pairs:
        n = len(bs)
        if run or (n >= 7 and bs.count(bs[:1]) == n):
            add((r0 + r1 * n + 20, 4 if n < 256 else 5))
        elif n <= 6:
            add((cp[n - 1] + 20, 1 + n))
        else:
            add((s0 + s1 * n + 20, 1 + n + (1 if n < 256 else 2)))
    return out


def by_mask(chosen, masks):
    """[(mask, its spans sorted)] for each of `masks`, in that order - one
    pass over the chosen, not one a mask"""
    at = {m: [] for m in masks}
    for m, a, bs, run in chosen:
        if m in at:
            at[m].append((a, bs, run))
    return [(m, sorted(at[m])) for m in masks]


def totals(costs):
    """(cycles, bytes) of a frame's candidate writes, each summed in order"""
    return sum(c for c, b in costs), sum(b for c, b in costs)


def ranked(p, look):
    """rank()'s order from the spans' priorities `p`: the highest first, a
    tie to the later span - sorted((p, i), reverse=True)'s order - and, with
    a look-ahead, none whose writing is worth nothing"""
    i = np.arange(len(p))
    o = np.lexsort((-i, -p))
    if look:
        o = o[p[o] > 0]
    return o.tolist()


def span_arrays(sp, costs, a_at=0):
    """A frame's spans as arrays: address, length, cycles, bytes"""
    n = len(sp)
    return (np.fromiter((x[a_at] for x in sp), np.int64, n),
            np.fromiter((len(x[a_at + 1]) for x in sp), np.int64, n),
            np.fromiter((c for c, b in costs), np.float64, n),
            np.fromiter((b for c, b in costs), np.float64, n))


def per_budget(c, b, er, eb):
    """Each span's share of the scarcer budget, as rank() divides by it"""
    return np.maximum(c / max(er, 1.0), b / max(eb, 1.0))


PRE_SECS = 2.0          # the longest the first picture may be held (98.2.9)
SPLIT = 1024            # a span longer than this is cut when a frame is


def split_big(cand):
    """A frame's candidate writes with every span past SPLIT bytes cut into
    SPLIT-byte pieces (98.2.9.1). A cut frame keeps the spans that fit the
    room and skips the rest WHOLE, so a picture with no gap in it - a full
    bleed, one span the size of the screen - was never written at all: the
    pre-roll held it two seconds and the screen stayed black. Pieces rank
    like any span, the worst first, and paint the picture in over frames.
    A span is (address, bytes, run), after a mask for the planar kinds"""
    out = []
    for c in cand:
        a, bs, run = c[-3:]
        if len(bs) <= SPLIT:
            out.append(c)
            continue
        for o in range(0, len(bs), SPLIT):
            out.append(c[:-3] + (a + o, bs[o:o + SPLIT], run))
    return out


UNIT = 32               # a planar cut's whole-byte unit, in bytes
WM3_DE = 25.0           # a pixel a refresh could catch in a colour this far
                        # (CIELAB) from both its old and new one is written
                        # in write mode 3 (98.1.3.2.4)


def byte_units(cand):
    """A 16-colour frame's candidates (mask, address, bytes, run) cut into
    UNITS a cut keeps or skips WHOLE (VIDEO-PALETTE-PLAN 10.4): a byte's
    planes are up to four candidates, one per group of planes that want one
    value, and a cut that kept some and skipped others left the pixel in a
    colour that is NEITHER frame's - a dark blue's planes 3 and 0 written
    and 1 and 2 not is a bright red, all but one a white - and paid for the
    stores twice. Candidates whose addresses overlap are one unit; a unit
    past UNIT bytes is cut at UNIT-byte boundaries, its candidates split
    there. Returns (candidates, the unit of each)"""
    order = sorted(range(len(cand)), key=lambda i: cand[i][1])
    groups, end = [], -1
    for i in order:
        a, n = cand[i][1], len(cand[i][2])
        if not groups or a >= end:
            groups.append([])
            end = a + n
        else:
            end = max(end, a + n)
        groups[-1].append(i)
    out, unit = [], []
    for gi, grp in enumerate(groups):
        lo = min(cand[i][1] for i in grp)
        hi = max(cand[i][1] + len(cand[i][2]) for i in grp)
        if hi - lo <= UNIT:
            for i in grp:
                out.append(cand[i])
                unit.append((gi, 0))
            continue
        for i in grp:
            m, a, bs, run = cand[i]
            o = 0
            while o < len(bs):
                k = (a + o - lo) // UNIT
                e = min(len(bs), lo + (k + 1) * UNIT - a)
                out.append((m, a + o, bs[o:e], run))
                unit.append((gi, k))
                o = e
    return out, unit


# VARIABLE FRAME RATE (--min-fps F, VIDEO-SEEN-PLAN 3a; the owner: "slowing
# down to X when it would have drawn better"): where the budgets would cut
# a frame, the next stretch is encoded on copies of the encoder at each
# rate down to F and the one whose view as seen is least broken is taken.
# A slower rate HOLDS frames: a held frame's record draws toward the next
# frame the rate shows, which finishes it, so its period's CPU and bytes go
# into that picture (an EMPTY hold buys nothing against the per-frame
# ceiling: the player draws what is due and starts nothing early). A held
# frame is judged against the picture it is DRAWING - it is the first half
# of the next one, as a slower file's frame is drawn over the same time -
# and costs VFR_HOLD besides, the price of the choppiness: so a slower rate
# is taken only where the tearing it saves is worth more. (Judged against
# its own moment, the motion it skips read as breakage and the car shot
# was never slowed)
VFR_RATES = (1.0, 5.0 / 6, 2.0 / 3, 0.5, 1.0 / 3, 0.25)  # of the frame rate
VFR_SPAN = 0.5          # the stretch a rate is chosen for, seconds
VFR_GAIN = 0.0          # ...and taken only this much less broken: the
                        # choppiness is VFR_HOLD, already in the sum
VFR_HOLD = 2.0          # a held frame's choppiness, points of the view
                        # broken (--slow-cost's default): GasGas's car
                        # shot slowed to 12 fps at it, and the owner found
                        # it "very similar" to the 20 fps file
VFR_AGAIN = 2           # frames until a cut is asked again, after the full
                        # rate won


# THE DEGRADE LADDER (--ladder, VIDEO-SEEN-PLAN 3a, the owner's "a second
# inner tab of size vs quality"): the ways a frame the budgets cannot draw
# may give - lag, a slower rate, coarse cells at a scene change, fewer
# colours - as RUNGS in the order they are tried. Where the budgets would
# cut, each prefix of the ladder is encoded on copies of the encoder over
# the next stretch, and the first that draws every frame it shows WHOLE is
# taken; none, the least broken by the view as seen. Cutting is what is
# left after the last rung. It overrides --lag, --min-fps, --coarse-cut,
# --step-down and --step-min. A CUT is a rung too (the owner, 2026-10-17:
# "some videos accept normal cuts better than others"): cut:X stops the
# climb at that rung where no frame of the stretch is more than X% broken
# as seen, and the frames are cut as the budgets cut them. Coarse cells are
# a kind of cut, so the default ladder tries them late - rung 5. And they
# are TWO rungs (the owner, 2026-10-17: "at scenes, coarse is known
# acceptable. During fast motion we have yet to test"): coarse:CELL at a
# scene's first frame, coarseany:CELL at the first frame of ANY stretch the
# budgets would cut - a moving picture drawn in cells for a frame and put
# right by the frames after
# The scene rung is COARSESCENE since 2026-10-17 (it was "coarse", which a
# ladder written before still says and degrade_parse still reads). The
# default lost its half rate the same day - the owner: it "worked great on
# gasgas, and terribly on last exile", too jerky and still cutting - and
# took coarse cells at any cut, mixed, in its place
DEGRADE_KINDS = ("lag", "slow", "coarsescene", "coarseany", "colours", "cut")
DEGRADE_DEFAULT = "lag:100,slow:5/6,colours:12,slow:2/3,coarsescene:frame," \
    "coarseany:mix"
# A coarse rung's cell: FRAME, the whole frame in the finest cell that
# fits (it was "auto", still read - the owner, 2026-10-17: so it is "clear
# to the user that they are doing a full frame at a time"), MIX, only the
# tiles it must, or a cell of the user's own
DEGRADE_COARSE = ("frame", "mix", "4x1", "16x4", "32x8", "64x8")


PEV_RESIDUE = 0.02      # a scene change named in the report: 2% of the
                        # picture or more left from the last scene
PEV_SHOW = 12           # ...at most this many, the earliest


def scene_join(scn, spec, fps, say=print):
    """--scene-join (98.1.3.2.3.3): the scene pass's [(first frame,
    pixels)] with the changes at `spec`'s times taken out - each scene
    joined to the one before it, its pixels sampled with that one's, so
    the two take one sixteen. A time is the encoder window's and its
    report's, which count the pre-roll's few frames the scene pass does
    not: the NEAREST change is taken, within half a second, and anything
    else is refused, naming the nearest, before a frame is encoded"""
    ts = [t.strip() for t in str(spec or "").replace(";", ",").split(",")
          if t.strip()]
    if not ts:
        return scn
    out = list(scn)
    for t in ts:
        try:
            k = float(t) * fps
        except ValueError:
            raise vid.V88Error("--scene-join %s: not a time in seconds" % t)
        cand = [i for i in range(1, len(out))]
        if not cand:
            raise vid.V88Error("--scene-join %s: the clip has one scene" % t)
        i = min(cand, key=lambda i: abs(out[i][0] - k))
        if abs(out[i][0] - k) > fps / 2.0:
            raise vid.V88Error(
                "--scene-join %s: no scene change within half a second - "
                "the nearest is at %.2f s" % (t, out[i][0] / fps))
        s0, px = out[i]
        p0, q = out[i - 1]
        q = np.concatenate([q, px]) if len(q) and len(px) else (
            q if len(q) else px)
        if len(q) > SCENE_PIX:          # (a sample's own size: every second
            q = q[::-(-len(q) // SCENE_PIX)]    # pixel of the two, or so)
        out[i - 1] = (p0, q)
        del out[i]
        say("   --scene-join %s: the change at %.2f s joined to the scene "
            "from %.2f s" % (t, s0 / fps, p0 / fps))
    return out


def ladder_ranges(spec):
    """--ladder-at (98.2.1.10): [(start s, end s, rungs)], in order and not
    overlapping - each range's rungs parsed as --ladder's are, colours
    refused (a scene's count is chosen before the encode, not a stretch's)"""
    out = []
    for part in str(spec or "").split(";"):
        part = part.strip()
        if not part:
            continue
        rng, eq, rungs = part.partition("=")
        a0, dash, a1 = rng.strip().partition("-")
        try:
            t0, t1 = float(a0), float(a1)
        except ValueError:
            t0 = t1 = None
        if not eq or not dash or t0 is None or not 0 <= t0 < t1:
            raise vid.V88Error("--ladder-at %s: START-END=RUNGS, seconds "
                               "into the clip, START before END" % part)
        rs = degrade_parse(rungs)
        if not rs:
            raise vid.V88Error("--ladder-at %s: no rungs" % part)
        if any(kind == "colours" for kind, v in rs):
            raise vid.V88Error("--ladder-at %s: colours is a scene's count, "
                               "chosen before the encode - give it to "
                               "--ladder" % part)
        out.append((t0, t1, rs))
    out.sort()
    for (a0, a1, _), (b0, b1, _) in zip(out, out[1:]):
        if b0 < a1:
            raise vid.V88Error("--ladder-at: %g-%g and %g-%g overlap"
                               % (a0, a1, b0, b1))
    return out


def degrade_parse(spec):
    """--ladder's rungs -> [(kind, value)]: lag ms, slow a fraction of the
    rate, coarse and coarseany a cell (or auto), colours 12 or 8, cut the
    most a frame of the stretch may be broken as seen, a percentage"""
    out = []
    for part in (spec or "").split(","):
        part = part.strip()
        if not part:
            continue
        kind, _, val = part.partition(":")
        kind, val = kind.strip().lower(), val.strip().lower()
        if kind == "coarse":            # (the scene rung's first name)
            kind = "coarsescene"
        try:
            if kind == "lag":
                v = float(val)
                ok = 0 < v <= 2000
            elif kind == "slow":
                a, _, b = val.partition("/")
                v = float(a) / float(b) if b else float(a)
                ok = 0.05 <= v < 1
            elif kind in ("coarsescene", "coarseany"):
                v, ok = val, val in DEGRADE_COARSE + ("auto",)
                if v == "frame":        # (auto, inside: coarse_pick's)
                    v = "auto"
            elif kind == "colours":
                v = int(val)
                ok = v in (12, 8)
            elif kind == "cut":
                v = float(val.rstrip("%"))
                ok = 0 < v <= 100
            else:
                ok = False
        except (ValueError, ZeroDivisionError):
            ok = False
        if not ok:
            raise vid.V88Error("--ladder: %r is not a rung (lag:MS, "
                               "slow:A/B, coarsescene:frame|mix|WxH, "
                               "coarseany:frame|mix|WxH, colours:12|8, "
                               "cut:PERCENT)"
                               % part)
        out.append((kind, v))
    return out


def degrade_rungs(a):
    """The file's ladder, parsed once - [] when there is none"""
    c = getattr(a, "_ladr", None)
    if c is None or c[0] != getattr(a, "ladder", ""):
        c = a._ladr = (getattr(a, "ladder", ""),
                       degrade_parse(getattr(a, "ladder", "")))
    return c[1]


def degrade_steps(rungs, fps, fgrid=None):
    """The prefixes the ladder tries, each (index, rate, lag frames, cell,
    cut, any cell): a prefix's slowest rate, its longest lag, its coarse
    cell at a scene's first frame, the most breakage it accepts (a
    fraction, None for none - its cut rungs' largest) and its coarse cell
    at any stretch's first - one a rung that changes nothing a stretch can
    try (fewer colours is a scene's, decided before) left out"""
    out, r, lag, cell, tol, acell = [], 1.0, None, None, None, None
    for i, (kind, v) in enumerate(rungs):
        if kind == "slow" and v < r:
            r = v
        elif kind == "lag":
            f = v * fps / 1000.0
            if fgrid:                   # (a call's grid: 98.2.1.1.3)
                f = math.ceil(max(1.0, f) / fgrid - 1e-9) * fgrid
            if lag is None or f > lag:
                lag = f
        elif kind == "coarsescene":
            cell = v
        elif kind == "coarseany":
            acell = v
        elif kind == "cut":
            tol = max(tol or 0.0, v / 100.0)
        else:
            continue
        step = (i + 1, r, lag, cell, tol, acell)
        if not out or out[-1][1:] != step[1:]:
            out.append(step)
    return out


def vfr_pattern(n, r):
    """A stretch of `n` frames at `r` of the rate: True where a frame is
    HELD - the first of a stretch held, its last always drawn"""
    out = [int((j + 1) * r + 1e-9) == int(j * r + 1e-9) for j in range(n)]
    out[-1] = False
    return out


def vfr_cut(enc, target, au):
    """Would the budgets cut `target` now? - on a copy, nothing spent"""
    e = enc_clone(enc)
    e.probe = True
    c0 = e.stats["cut"]
    e.frame(target, au)
    return e.stats["cut"] != c0


def coarse_every(cell):
    """coarseany's draw for a frame of a stretch: `cell` (or auto's pick)
    where the budgets would cut the frame, None where it fits whole"""
    def every(e, t, au):
        if cell == "mix":
            return coarse_mix(e, t, au)
        c = cell if cell != "auto" else coarse_pick(e, t, au)
        if c == "full" or (cell != "auto" and not vfr_cut(e, t, au)):
            return None
        return coarse_of(e, t, c)
    return every


def vfr_try(enc, ts, pat, au, tp, first=None, cost=VFR_HOLD, empty=False,
            shown=None, stay=False, lag=None, count=False, every=None):
    """The stretch `ts` encoded on a copy at the hold pattern `pat`: its
    mean view as seen, a frame against the picture it draws (a held one's
    the next shown, and VFR_HOLD more). `first`, what the first frame
    draws when it is not held - a scene start's coarse cells. EMPTY, a
    held frame writes nothing - a FLIPPED file's hold, the picture shown
    again whole - and is judged against `shown`, what it shows. STAY, a
    flipped file's HELD record (98.1.3.9): drawn toward the next frame on
    the page the glass is not showing, and judged as EMPTY's is, by the
    picture still shown. LAG, the lag allowance the stretch is drawn at
    (the ladder's). COUNT -> (that, the frames it shows that were cut, the
    most broken frame's view as seen, no hold's charge in it). EVERY,
    coarseany's: each frame drawn at its own time that the budgets would
    cut is drawn as every(e, frame, au) says - cells - instead"""
    e = enc_clone(enc)
    if lag is not None:
        e.flag_lag = lag
    v, prev = 0.0, tp
    cuts, worst = 0, 0.0
    spx = None                          # (the glass through a STAY's run)
    for j, t in enumerate(ts):
        k = j
        while pat[k]:
            k += 1
        if stay and k != j:
            if spx is None:
                spx = e.spix()[1].copy()
            e.staying = True
            _, rec = e.frame(ts[k], au if j == 0 else b"\x80" * len(au))
            e.charge(rec, len(au))
            T = e.tpix(shown if shown is not None else t)[1]
            q = seen_share(spx, T, prev)
            v, worst = v + q + cost / 100.0, max(worst, q)
            prev = T
            continue
        spx = None
        if empty and k != j:
            _, rec = e.frame(e.screen.copy(), au if j == 0 else
                             b"\x80" * len(au))
            e.charge(rec, len(au))
            T = e.tpix(shown if shown is not None else t)[1]
            q = seen_share(e.spix()[1], T, prev)
            v, worst = v + q + cost / 100.0, max(worst, q)
            prev = T
            continue
        if not (empty or stay) or k == j:
            shown = ts[k]
        c0 = e.stats["cut"]
        ev = every(e, ts[k], au if j == 0 else b"\x80" * len(au)) \
            if every is not None and k == j and not (
                j == 0 and first is not None and first is not ts[0]) \
            else None
        if j == 0 and k == 0 and first is not None and first is not ts[0]:
            e.all3 = e.wm3
            _, rec = e.frame(first, au)
            e.all3 = False
            cuts += 1                   # (cells: not the picture, whole)
        elif ev is not None:
            e.all3 = e.wm3
            _, rec = e.frame(ev, au if j == 0 else b"\x80" * len(au))
            e.all3 = False
            cuts += 1
        else:
            _, rec = e.frame(ts[k], au if j == 0 else b"\x80" * len(au))
        if k == j and e.stats["cut"] != c0:
            cuts += 1
        e.charge(rec, len(au))
        T = e.tpix(ts[k])[1]
        q = seen_share(e.spix()[1], T, prev)
        v, worst = v + q + (cost / 100.0 if pat[j] else 0.0), max(worst, q)
        prev = T
    return (v / len(ts), cuts, worst) if count else v / len(ts)


# THE TRIAL POOL (the encoder speed pass's second round, 2026-10-17: "I
# have 16 threads and it is using 6% of them"). The ladder's prefixes at a
# stretch are each a trial encode on a COPY of the encoder (degrade_choose),
# and no trial reads another's: they run at once, one a process, and the
# same rule picks among them - so the file is the one the trials made one
# after another, byte for byte. A trial is the encoder pickled with its
# read-only tables left out (each worker was given those once, when the pool
# was made) and the caches that are only caches emptied
TRIAL_SHARED = ("g", "ct", "csub", "rowof", "base", "idx", "wv")
TRIAL_CACHES = (("tcache", dict), ("scache", None), ("meter", None))


def _tr_init(blob):
    global _TR
    import pickle
    _TR = pickle.loads(blob)


def _tr_run(blob):
    import io
    import pickle

    class Up(pickle.Unpickler):
        def persistent_load(self, pid):
            return _TR[pid]
    fn, args = Up(io.BytesIO(blob)).load()
    return fn(*args)


METER_BATCH = 8         # frames a Meter's task measures
METER_AHEAD = 4         # ...and how many tasks a worker may have waiting


def _mt_run(cfg, prefix, calls):
    """Meter's worker: `calls` measured as measure() would - each (screen,
    target, palette) - after `prefix`, the two before them, measured only
    for the frames before that a frame's flicker and view are against"""
    m = Encoder.__new__(Encoder)
    m.dmode, m.ppb, m.blur_r = cfg
    m.look, m.meter = 0, None
    m.q_prev = m.q_tprev = (None, None)
    m.q_stp = None
    out = []
    for j, (scr, tgt, pal) in enumerate(prefix + calls):
        m.screen, m.dpal, m.tcache, m.scache = scr, pal, {}, None
        r = m.measure_of(tgt)
        if j >= len(prefix):
            out.append(r)
    return out


class Meter:
    """THE PICTURE AS PLAYED, MEASURED ELSEWHERE (the encoder speed pass's
    second round, 2026-10-17): measure() reads nothing back into the
    encode - its sums are the report's, and the pass comparisons' after it
    - so a frame's measure is a copy of its screen and target sent to a
    worker, METER_BATCH frames a task, each with the two frames before it
    that its flicker and its view are against. The results come back in
    order and are summed in order, so the report is the one measure() made
    here, to the bit. The target before (q_stp, the ladder's) is kept here.
    A copy of the encoder shares its meter, and a pickle leaves it out
    (TrialPool): neither measures"""

    def __init__(self, e, jobs):
        self.pool = worker_pool(jobs)
        self.cfg = (e.dmode, e.ppb, e.blur_r)
        self.jobs = jobs
        self.batch, self.prev = [], []
        from collections import deque
        self.seq = deque()              # ("job", result) / ("bad", shot)
        self.acc = Encoder.__new__(Encoder)     # (the sums)
        a = self.acc
        a.q_vis = a.q_wrong = a.q_flick = a.q_tflick = 0.0
        a.q_frames, a.q_bad, a.q_seen = [], [], []

    def __deepcopy__(self, memo):
        return self

    def put(self, e, target):
        e.q_stp = e.tpix(target)[1]
        self.batch.append((e.screen.copy(), np.array(target), e.dpal))
        if len(self.batch) >= METER_BATCH:
            self.send()

    def send(self):
        if not self.batch:
            return
        while sum(1 for k, _ in self.seq if k == "job") >= \
                METER_AHEAD * self.jobs:
            self.take(1)
        self.seq.append(("job", self.pool.apply_async(
            _mt_run, (self.cfg, self.prev, self.batch))))
        self.prev = (self.prev + self.batch)[-2:]
        self.batch = []

    def mark(self, shot):
        if self.batch:                  # (its frame is in this batch: the
            self.send()                 # check waits on it, in order)
        self.seq.append(("bad", shot))

    def take(self, jobs=None):
        """The results in, in order - `jobs` tasks of them, or all"""
        a = self.acc
        while self.seq and (jobs is None or jobs > 0):
            k, v = self.seq.popleft()
            if k == "bad":
                if a.q_bad and a.q_bad[-1] >= VIS_AREA:
                    v[3] += 1
                continue
            for r in v.get():
                if r is not None:
                    a.measured(r)
            if jobs is not None:
                jobs -= 1

    def finish(self, e):
        """Every frame's measure in, and the sums `e`'s, as measure() would
        have left them; the pool ended"""
        self.send()
        self.take()
        self.close()
        a = self.acc
        e.q_vis, e.q_wrong, e.q_flick, e.q_tflick = \
            a.q_vis, a.q_wrong, a.q_flick, a.q_tflick
        e.q_frames, e.q_bad, e.q_seen = a.q_frames, a.q_bad, a.q_seen
        e.meter = None

    def close(self):
        if self.pool is not None:
            self.pool.terminate()
            self.pool.join()
            self.pool = None


def meter_for(e, a):
    """A Meter for `e`'s pass, or None where measure() is best made here:
    one job, no pixels to measure, or this process a pool's worker"""
    import multiprocessing
    jobs = min(METER_JOBS, getattr(a, "jobs", None) or os.cpu_count() or 1)
    if jobs <= 1 or not e.metric or e.dmode not in ("bits", "pal", "idx") \
            or multiprocessing.current_process().daemon:
        return None
    return Meter(e, jobs)


METER_JOBS = 3          # measure is about a fifth of a frame's time here


class TrialPool:
    """`jobs` processes to try ladder prefixes on (the note above), made
    for one pass's encoder `enc`; .close() ends them. With one job, or
    inside another pool's worker, None (trial_pool) - the trials then run
    here, in order, as they did"""

    def __init__(self, enc, jobs):
        import pickle
        self.objs = [enc.__dict__[k] for k in TRIAL_SHARED
                     if enc.__dict__.get(k) is not None]
        self.ids = {id(o): i for i, o in enumerate(self.objs)}
        self.pool = worker_pool(jobs, _tr_init, (pickle.dumps(
            self.objs, pickle.HIGHEST_PROTOCOL),))

    def submit(self, fn, enc, *args):
        """fn(a copy of `enc`, *args) in a worker -> .get()"""
        import io
        import pickle
        ids = self.ids
        saved = {k: enc.__dict__[k] for k, _ in TRIAL_CACHES
                 if k in enc.__dict__}
        for k, empty in TRIAL_CACHES:   # (the caches stay here: a worker
            if k in saved:              # makes its own)
                enc.__dict__[k] = empty() if empty else None

        class Down(pickle.Pickler):
            def persistent_id(self, obj):
                return ids.get(id(obj))
        f = io.BytesIO()
        try:
            Down(f, pickle.HIGHEST_PROTOCOL).dump((fn, (enc,) + args))
        finally:
            enc.__dict__.update(saved)
        return self.pool.apply_async(_tr_run, (f.getvalue(),))

    def close(self):
        self.pool.terminate()
        self.pool.join()


def trial_pool(enc, a, most):
    """A TrialPool of up to `most` processes for `enc`, or None where the
    trials are best run here: one job (--jobs 1), or this process a pool's
    worker already (--aim quality's trials), which may start none"""
    import multiprocessing
    jobs = min(most, getattr(a, "jobs", None) or os.cpu_count() or 1)
    if jobs <= 1 or multiprocessing.current_process().daemon:
        return None
    return TrialPool(enc, jobs)


def _ladder_trial(e, ts, pat, au, tp, first, cost, empty, shown, stay,
                  lag, ac):
    """A ladder prefix's trial (degrade_choose), in a worker"""
    return vfr_try(e, ts, pat, au, tp, first, cost, empty, shown, stay,
                   lag=lag, count=True,
                   every=coarse_every(ac) if ac else None)


def degrade_choose(enc, ts, au, steps, fps, tp, first=None, cost=VFR_HOLD,
                   empty=False, shown=None, stay=False, coarse=None,
                   scene=False, pool=None):
    """THE DEGRADE LADDER at a stretch (--ladder): today's draw, then each
    of the ladder's prefixes (`steps`, degrade_steps), each encoded on a
    copy - the first that shows every frame WHOLE, or past a cut rung no
    frame more broken than it accepts; else the least broken. `coarse(cell)`
    is the stretch's first frame in a prefix's scene cell where the stretch
    is a scene's first (`scene`); a prefix's ANY cell draws each of the
    stretch's frames the budgets would cut in cells (coarse_every) ->
    (hold pattern, rate, lag frames, scene cell, rung index, any cell).
    With a `pool` (TrialPool) every prefix's trial runs at once, today's
    draw here meanwhile, and the same rule reads them in order"""
    n = len(ts)
    full = [False] * n
    # (each prefix's draw: a trial where it draws other than the one
    # before it - a cut rung alone draws the same - and the scene cells
    # made once a cell, here, before any trial: none changes the encoder)
    tries, cfs, was = [], {}, (1.0, None, False, None)
    for i, r, lag, sc, tol, ac in steps:
        cell = sc if scene else None
        if coarse is None:
            ac = None
        pat = vfr_pattern(n, r) if r < 1.0 else full
        if cell is not None and coarse is not None and not pat[0]:
            if cell not in cfs:
                cfs[cell] = coarse(cell)
            cf = cfs[cell]
        else:
            cf = first
        key = (r, lag, cf is not first and cell, ac)
        tries.append([i, r, lag, cell, tol, ac, pat, cf, key != was, None])
        was = key
    if pool is not None:
        for t in tries:
            if t[8]:
                i, r, lag, cell, tol, ac, pat, cf = t[:8]
                t[9] = pool.submit(_ladder_trial, enc, ts, pat, au, tp, cf,
                                   cost, empty, shown, stay, lag, ac)
    got = vfr_try(enc, ts, full, au, tp, first, cost, empty, shown,
                  stay, count=True)
    v, cuts, worst = got
    if os.environ.get("OS88_VENC_LADLOG"):
        with open(os.environ["OS88_VENC_LADLOG"], "a") as fl:
            fl.write("frame %d rung 0: view %.2f%% cut %d worst %.1f%%\n"
                     % (enc.stats["frames"], 100 * v, cuts, 100 * worst))
    best = (v, full, 1.0, None, None, 0, None)
    if cuts == 0:
        return best[1:]
    for i, r, lag, cell, tol, ac, pat, cf, new, job in tries:
        if new:
            got = job.get() if job is not None else vfr_try(
                enc, ts, pat, au, tp, cf, cost, empty, shown, stay,
                lag=lag, count=True,
                every=coarse_every(ac) if ac else None)
        v, cuts, worst = got            # (a cut rung alone: the same draw)
        if os.environ.get("OS88_VENC_LADLOG"):
            with open(os.environ["OS88_VENC_LADLOG"], "a") as fl:
                fl.write("frame %d rung %d: view %.2f%% cut %d worst %.1f%%"
                         " cells %s\n" % (enc.stats["frames"], i, 100 * v,
                                           cuts, 100 * worst, cf is not
                                           first and cell))
        if cuts == 0 or (tol is not None and worst <= tol + 1e-9):
            return pat, r, lag, cell if cf is not first else None, i, ac
        if v < best[0] - 1e-9:
            best = (v, pat, r, lag, cell if cf is not first else None, i, ac)
    return best[1:]


def vfr_choose(enc, ts, au, fmin, fps, tp, first=None, cost=VFR_HOLD,
               empty=False, shown=None, stay=False):
    """-> (hold pattern, rate): the stretch's best rate down to `fmin` -
    the full rate's first frame drawing `first` where that is a scene
    start's coarse cells, so a slower rate is weighed against today's"""
    n = len(ts)
    full = [False] * n
    base = vfr_try(enc, ts, full, au, tp, first, cost, empty, shown, stay)
    best = (base, full, fps)
    for r in VFR_RATES[1:]:
        if r * fps < fmin - 1e-6:
            break
        pat = vfr_pattern(n, r)
        if not any(pat):
            continue
        v = vfr_try(enc, ts, pat, au, tp, cost=cost, empty=empty,
                    shown=shown, stay=stay)
        if v < best[0] - 1e-9:
            best = (v, pat, r * fps)
    if os.environ.get("OS88_VENC_VFRLOG"):  # (each decision, to look at)
        with open(os.environ["OS88_VENC_VFRLOG"], "a") as fl:
            fl.write("frame %d: %d frames, full %.1f%%, best %.1f%% at %g "
                     "fps\n" % (enc.stats["frames"], n, 100 * base,
                                 100 * best[0], best[2]))
    if best[1] is not full and best[0] > base - VFR_GAIN:
        return full, fps
    return best[1], best[2]


def preroll(enc, target, abytes, most):
    """How many frames the budgets need to paint `target` WHOLE from the
    encoder's starting screen: 0 when the first frame is exact, else the
    frames of holding it, found on a copy of the encoder so the real one
    starts untouched. `most` bounds it: a picture the budgets never finish
    is held that long and no longer"""
    import copy
    e = copy.deepcopy(enc)
    # np.frombuffer views do not survive a deepcopy: the copy's arrays and
    # its bytearrays come apart, so a write through `tv` never reached the
    # `tsurf` the spans are read from, and the dry run encoded ZEROS - a
    # frame it called exact, and a pre-roll counted short (98.2.9.1)
    e.sv = np.frombuffer(e.surf, dtype=np.uint8)
    e.tv = np.frombuffer(e.tsurf, dtype=np.uint8)
    # ...and each frame is CHARGED as the real loop charges it (98.2.9.2):
    # without it the copy's buckets never emptied, so the frame after a cut
    # one always had a whole period's CPU and disk and came out exact, and
    # a flipped file's back-page cost was never owed - a pre-roll of 1 where
    # the budgets needed more, and key 0 a half-painted screen again
    e.future = [target] * e.look
    for n in range(max(1, most)):
        cut = e.stats["cut"]
        rec = e.frame(target, b"\x80" * abytes)[1]
        e.charge(rec, abytes)
        if e.stats["cut"] == cut:
            return n
    return most


# WHAT A PLAY CLAIMS BESIDE ITS RING (98.2.1.3.1), KB - the player's own
# sum (vp_cptry): the sound card's ring, VP_RL and a KB, and a seek's table
# entry, two clusters. A page flip or a play in the window keeps a copy of
# the canvas besides, which the summary says on its own line
PLAY_SND_KB = 17
PLAY_SEEK_KB = 4


def play_memory_kb(ring, msl, sound):
    """The free memory a play wants, KB: its ring of `ring` 32 KB slots and
    `msl` mirror slots (2 for a BIGSP stream, 98.1.4.1), the sound's ring
    and a seek's entry"""
    return (ring + msl) * 32 + (PLAY_SND_KB if sound else 0) + PLAY_SEEK_KB


def ring_for_memory(kb, msl, sound):
    """...and the ring `kb` of free memory holds"""
    return int((kb - (PLAY_SND_KB if sound else 0) - PLAY_SEEK_KB) // 32) \
        - msl


def reserve_bytes(prof):
    """The disk bucket's depth: the profile's `reserve` in KB, or what its
    ring holds read ahead - a slot for the one being decoded and one for a
    super-packet straddling into the next, so (ring - 2) x 32 KB"""
    if prof.get("reserve") is not None:
        return int(prof["reserve"] * 1024)
    return (max(2, prof.get("ring") or 4) - 2) * vid.SLOT or DISK_LOOKAHEAD


class Budget:
    """A token bucket: `per` a frame, holding at most `cap`, starting half
    full (the player fills its ring before the first frame)"""

    def __init__(self, per, cap):
        self.per, self.cap = per, cap
        self.level = cap / 2 if per is not None else None

    def tick(self):
        if self.per is not None:
            self.level = min(self.cap, self.level + self.per)

    def room(self):
        return math.inf if self.per is None else self.level

    def spend(self, n):
        if self.per is not None:
            self.level -= n


_BINOM = {}


def _sum3_same():
    """Whether this numpy's sum over a last axis of three is the plain
    left-to-right one (it is a reduction, free to pair its terms): the
    channels then add as three slices, bit for bit what sum(2) makes"""
    if np is None:
        return False
    d = np.random.default_rng(88).random((64, 67, 3), dtype=np.float32)
    return bool(np.array_equal(d.sum(2), d[..., 0] + d[..., 1] + d[..., 2]))


_SUM3 = _sum3_same()


def _binomial(r):
    """The 2r+1 taps of a binomial low-pass, float32, summing to 1"""
    k = _BINOM.get(r)
    if k is None:
        k = np.array([math.comb(2 * r, i) for i in range(2 * r + 1)],
                     np.float32)
        k /= k.sum()
        _BINOM[r] = k
    return k


# what an encoder's copy SHARES with it (Encoder.__deepcopy__): tables
# made once and only ever read - the geometry, the palette's float copy, the
# cost tables, the row maps, and the targets ahead, which a step reads
def own_price(e, base, rec):
    """`base(rec)` - or, where `base` is None, the encoder's CLASS price,
    which set_xplay's wrappers stand in front of on the instance"""
    return type(e).cost(e, rec) if base is None else base(rec)


def price_base(e):
    """What a wrapper of `e.cost` asks below it: None for the class's own
    price (a bound method of `e` - held as None, so a copy or a pickle of
    the wrapper asks ITS encoder), else the wrapper already there"""
    c = e.cost
    return None if getattr(c, "__self__", None) is e else c


class PlayCost:
    """set_xplay's THE PLAY AS PROFILED: the table's price scaled, and its
    KB and fixed costs. An object and not a closure, so an encoder PICKLES
    (the trial pool, TrialPool) and a deep copy's price is its own"""

    def __init__(self, e, base, k, kb_us, fix_us, us):
        self.e, self.base = e, base
        self.k, self.kb_us, self.fix_us, self.us = k, kb_us, fix_us, us

    def __call__(self, rec):
        return own_price(self.e, self.base, rec) * self.k + \
            (self.kb_us / 1024.0 * len(rec) + self.fix_us) * self.us


class XmsCost:
    """set_xplay's borrows and copies down, on top of the price below it -
    asked of the encoder it prices for. A closure here made a deep copy
    ask the ORIGINAL encoder's bank whether a frame was banked: the trial
    copies of a copying FIFO priced every frame by the real encode's level
    and not their own"""

    def __init__(self, e, base):
        self.e, self.base = e, base

    def __call__(self, rec):
        e = self.e
        c = own_price(e, self.base, rec)
        if e.xmode == "copy" and not e._xbanked():
            return c                    # (the disk reads into the ring)
        return c + e.xfix + e.xbyte * len(rec)


ENC_SHARED = ("g", "pal", "ct", "wv", "rowof", "base", "idx", "future")


class Encoder:
    # a frame record's room: self.rec_max, or --frame-cap's (98.1.4.1)
    rec_max = REC_MAX
    # A PROBE (vfr_cut, coarse_pick): a copy asked only whether the frame
    # would be cut. NOT stopped at the first estimate's over-run: a frame
    # over it there can still come out whole, the fall-back's split spans
    # costing less and a record under its room growing the room again -
    # stopping there made a different file, and a worse one (the speed
    # pass, VIDEO-SEEN-PLAN 3a)
    probe = False
    # SOUND AHEAD's slower-reader check (lean): a class default as well, so
    # an encoder pickled by a process that loaded an older os88venc - the
    # GUI left open across an update, its trial workers importing the new
    # file (Windows spawns) - plays as Auto did before it, not a traceback
    amargin, sh = 0.0, None

    def __deepcopy__(self, memo):
        """A copy whose surfaces are still SEEN through its own views: an
        np.frombuffer view does not survive a deep copy (preroll()'s note),
        so the copy's `sv`/`tv` are made again over the copy's bytearrays -
        every copy, the lag search's snapshots (98.2.1.1.3) among them,
        rather than a fix at each place one is taken"""
        import copy
        new = self.__class__.__new__(self.__class__)
        memo[id(self)] = new
        # (the READ-ONLY tables are SHARED, not copied: the geometry's
        # 65,536-entry row lists were most of a copy's 0.18 s, and a trial
        # copy is taken several times a frame - the encoder speed pass,
        # VIDEO-SEEN-PLAN 3a. Nothing writes into any of these in place)
        for k in ENC_SHARED:
            v = self.__dict__.get(k)
            if v is not None:
                memo[id(v)] = v
        for k, v in self.__dict__.items():
            setattr(new, k, copy.deepcopy(v, memo))
        if "sv" in new.__dict__:
            new.sv = np.frombuffer(new.surf, dtype=np.uint8)
        if "tv" in new.__dict__:
            new.tv = np.frombuffer(new.tsurf, dtype=np.uint8)
        return new

    def __init__(self, g, prof, fps, audio_cyc, audio_bps, palette=None):
        self.g = g
        # how wrong a byte is: its differing bits, or for VGA8 how far its
        # colour is from the one wanted
        self.pal = None if palette is None else \
            np.frombuffer(palette, np.uint8).astype(np.float32).reshape(
                -1, 3) * (255.0 / 63)
        period = vid.HZ / fps
        self.period = period
        # the latch copy's byte, and the table records are priced with
        self.lcopy = prof["lcopy_us"] * vid.HZ / 1e6 * prof["speed"] \
            if prof.get("lcopy_us") else CYC_LCOPY
        self.ct, self.csub = profile_table(prof, g.layout)
        self.cwm3 = wm3_cost(prof)
        self.live = False               # LIVE: the blit is charged too,
        self.blitf = min(1.0, LIVE_PASS_HZ / fps)   # a pass's share of it
        if prof["avg"] is None:
            self.cpu = Budget(None, None)
            self.peak = math.inf
            self.disk = Budget(None, None)
            self.reserve = 0
            self.dcurve = None
            self.dfloor = 0
        else:
            self.cpu = Budget(prof["avg"] * period - audio_cyc,
                              (prof["avg"] * period - audio_cyc) * fps)
            self.peak = prof["peak"] * period - audio_cyc
            vb = prof["disk"] * 0.99 - audio_bps     # super-packet padding
            if vb <= 0:
                raise vid.V88Error("the sound alone is %d bytes a second, "
                                   "and the profile's disk is %d"
                                   % (audio_bps, prof["disk"]))
            # THE DISK RESERVE (98.2.1.3): what the player's ring holds read
            # ahead, banked in calm stretches and spent in a burst
            self.reserve = reserve_bytes(prof)
            self.disk = Budget(vb / fps, self.reserve)
            # A DISK THAT SLOWS UNDER LOAD (98.2.1.3): `disk` is its rate at
            # the `avg` share, and `disk_at` its rate at others as a share
            # of that, measured. A frame refills the bucket at the rate its
            # own hook leaves - so a burst, which is when a deep reserve is
            # spent, is when it refills slowest
            self.dcurve = prof.get("disk_at")
            # ...and never spent below one READ_SEQ: the reader brings 32 KB
            # a call, ~5 periods of it, so a slot's worth can be in flight
            # and not yet in the ring. Measured: Sonic 2 at a 192 KB
            # reserve stalled 3 times in 20 s without it and 0 with it
            self.dfloor = min(float(vid.SLOT), self.reserve / 4.0)
            self.drate, self.abps, self.fps = prof["disk"], audio_bps, fps
        self.bank = self.bankpre = 0    # the XMS bank's bytes (set_bank)
        self.dslow = 1.0                # ...its copies' charge on the disk
        self.drate0 = getattr(self, "drate", None)
        self.alead, self.arefill = 0, []    # the sound's lead (disk_floor)
        self.slead, self.srefill = 0, []    # ...what it WOULD be in step
                                            # (Auto's question, 98.2.1.3)
        self.amargin, self.sh = 0.0, None   # ...and on a slower reader
        # OWED TIME (98.2.1.1): the player's schedule, simulated. On when
        # a frame may run to more than the per-frame ceiling allows
        self.audio_cyc = audio_cyc
        self.q = period * (prof.get("speed") or 1)  # a PERIOD, 8088 cycles
        self.owe = None
        if prof["avg"] is not None and prof.get("owe") and \
                prof["owe"] * self.q > prof["peak"] * period:
            self.owe = prof["owe"] * self.q - audio_cyc - HOOK_CYC
        self.ceil = self.peak
        self.s_free = self.s_t0 = 0.0   # the hook busy until; frame 0's due
        self.s_i, self.s_pair = 0, None     # the frame; a pair's deadline
        self.s_start = self.s_role = 0
        self.cpucut = self.wascut = False
        self.spk = 0.0                  # the speaker's share (98.2.15): a
                                        # decode cycle takes 1/(1-spk) of wall
        self.base = np.array(g.base, dtype=np.int64)
        self.idx = self.base[:, None] + np.arange(g.wb)
        self.screen = np.zeros((g.h, g.wb), dtype=np.uint8)
        self.age = np.zeros((g.h, g.wb), dtype=np.float32)
        self.surf = bytearray(65536)
        self.sv = np.frombuffer(self.surf, dtype=np.uint8)
        self.tsurf = bytearray(65536)
        self.tv = np.frombuffer(self.tsurf, dtype=np.uint8)
        self.wv = np.zeros(65537, dtype=np.float64)
        self.stats = dict(frames=0, exact=0, cut=0, bytes_left=0,
                          disk=0, cpu=0, peak=0, flip=0, cap=0, owed=0,
                          held=0,
                          late=0, skipped=0, unfit=0,
                          fover=0, fmaxlag=0.0, flate=0, fbehind=0)
        # THE LAG SCHEDULE (98.2.1.1.1, 98.2.1.1.2): a frame a call when
        # flipped, up to two when not, the calls a period apart or half of
        # one with a card's sound - so a frame that runs over its period
        # starts the next at the next call, and the picture falls behind
        # the sound by whole calls, catching up where the calls come faster
        # than the frames. `fgrid` is the calls' spacing, model cycles, set
        # by encode() on a budgeted file that runs it; `flag` how far behind
        # the next frame starts; `flag_lag` the frames --lag allows
        self.fgrid, self.flag = None, 0.0
        self.flag_lag = 1
        # WHAT A CUT FRAME SPENDS ON (98.2.1.2): the targets after this one
        # (`look` of them, set per frame), and error as SEEN rather than as
        # bits - both need the screen as pixels, which `dmode` says how to
        # get: "bits" a byte is 8 one-bit pixels, "pal" a byte (or a pixel)
        # indexes `dpal`, "idx" a byte packs `ppb` indexes of `dpal`, None
        # no pixel form (the composite formats: bits, and no visible error)
        self.look, self.vis, self.future = 0, False, []
        self.lfloor = 0.0                   # (value()'s look-ahead floor)
        # WHERE AN EYE LOOKS (--attend, VIDEO-SEEN-PLAN 3): value() weighted
        # by the view as seen's attention map, `attend` its strength (0
        # off); `attc` the last target's (target, pixels, map)
        self.attend, self.attc = 0.0, None
        # ...and the VARIABLE FRAME RATE's (--min-fps, VIDEO-SEEN-PLAN 3a):
        # the hold pattern committed for the frames ahead, and the stretches
        # slowed, by rate - on the encoder, so the lag search's copies of
        # it play the same holds
        from collections import deque
        self.vplan, self.vrates, self.vstretch = deque(), {}, []
        # ...and the EDGES a cut leaves (--edges, VIDEO-SEEN-PLAN 3): a
        # pixel also worth how much nearer its target's edges the screen is
        # for writing it, `edges` the strength (0 off)
        self.edges = 0.0
        # HOW A CUT FRAME CHOOSES (--cut, 98.2.1.2.1): "rank" the best
        # first, the look-ahead dropping what it scores worth nothing;
        # "fill" the same, those last instead of never; "tear" whole rows
        # in screen order from `tcur`, where the last cut frame stopped
        self.cut, self.tcur = "rank", 0
        self.blocks = False             # VERSION 2's (98.1.10)
        self.cb_due, self.cbook, self.cbt = False, None, None
        # HOW A PLANAR FRAME IS DRAWN (--bands, 98.2.1.2.2): 0, a sub-record
        # a Map Mask over the whole frame; N, the frame in N bands of rows,
        # every plane of a band before the next
        self.bands = 0
        self.thr = 0.0
        self.dmode, self.dpal, self.ppb = \
            ("bits", None, 8) if palette is None else ("pal", self.pal, 1)
        self.blur_r = 1
        self.q_vis = self.q_wrong = self.q_flick = self.q_tflick = 0.0
        self.q_prev = self.q_tprev = (None, None)
        self.q_frames, self.q_bad = [], []  # per frame: the mean, and the
                                            # share VISIBLY wrong (measure)
        self.q_seen, self.q_stp = [], None  # ...the VIEW as seen (seen_share)
                                            # and the target before
        self.tcache, self.scache = {}, None     # (tpix, spix)
        self.meter = None                   # (measure's: Meter)
        # PAGE-AHEAD (98.2.1.8): the hidden page's record this frame, its
        # (cycles, bytes) - charged with the frame's own - and what the plan
        # pass reads off a frame: which budget cut it, and its disk refill
        self.pa_spend = (0.0, 0.0)
        self.last_why = self.last_ref = None

    def set_xplay(self, mode, prof):
        """THE XMS COPIES THE PLAYER WILL MAKE (docs/plans/LOADALL-PLAN.md 7,
        L5), charged where it makes them. The player takes every file into
        XMS when the machine has a pool - held whole if it fits, banked if
        not (SPEC.md 98.3.18) - so on a profile with `xplay` every frame
        plays from there:
          - "inplace" (a 286 whose LOADALL checks out, or any 386:
            98.3.18.11, .13, .14):
            NO copy down. The decode reads each record where it lies,
            through borrows - `xbor_n` a frame (the super-packet and record
            headers, the record, the sound) and one more every `xyield`
            record bytes (vdec.inc's yields) - each `xbor_us`. The bank's
            copy UP is the reader's, on the disk (set_bank), on EVERY
            chunk: the ring is the bank's slots.
          - "copy" (98.3.18.2's FIFO: a play the in-place paths refuse,
            on any tier - before 98.3.18.14 every 386 play, which is why
            the 486's profile said so): a banked record is copied DOWN inside
            its frame's period, ahead of its decode - `xdown` ms a KB on
            the CPU, which nothing charged before and `--peak` paid for out
            of slack (LOADALL-PLAN 7.1) - and up on the disk.
        None, or no profile field: nothing, as before"""
        us = vid.HZ / 1e6 * (prof.get("speed") or 1)
        # a flipped file's back page is the last record decoded AGAIN, out
        # of the copy the player kept: the table's price alone (cost0),
        # which borrows nothing and was timed at 0.95 of it
        self.cost0 = self.cost
        self.xmode = mode
        self.xfix = self.xbyte = 0.0
        key = "modex" if self.g.layout == vid.LAY_MODEX else \
            "planar" if getattr(self.g, "bitplanes", False) else None
        play = (prof.get("play") or {}).get(key) if mode == "inplace" \
            else None
        if play:
            # THE PLAY AS PROFILED (SPEC.md 98.3.22.1): the whole in-place
            # draw measured, borrows and all - in place of xbor_*
            k, kb_us, fix_us = play
            self.cost = PlayCost(self, price_base(self), k, kb_us,
                                 fix_us, us)
            return
        if mode == "inplace":
            self.xfix = prof["xbor_us"] * prof.get("xbor_n", 3) * us
            self.xbyte = prof["xbor_us"] / float(prof.get("xyield", 2048)) * us
        elif mode == "copy" and prof.get("xdown"):
            self.xbyte = prof["xdown"] * 1000.0 / 1024 * us
        if not (self.xfix or self.xbyte):
            return
        self.cost = XmsCost(self, price_base(self))

    def _xbanked(self):
        """...a banked frame on the copying FIFO: while the bank holds
        anything (set_bank's test)"""
        return bool(self.bank) and self.disk.level > self.reserve

    def set_bank(self, bank, pre, xcopy=0.0):
        """THE XMS BANK (SPEC.md 98.3.18.2): `bank` bytes ahead of the ring,
        so the disk bucket is that much deeper, and `pre` of them filled
        before the first frame (98.3.18.3), so it starts that much fuller.
        A banked byte is read, copied UP and copied DOWN (VIDEO-XMS-PLAN
        4.2): `xcopy` ms a KB a copy - the profile's VIDDISK `X` reading -
        is time the reader spends that it does not spend reading, and the
        disk's rate is charged for it WHILE THE BANK HOLDS ANYTHING: the
        player's FIFO hands the ring its chunks through the bank only then,
        and with the bank empty the disk reads into the ring as ever. The
        bucket above the ring's reserve is the bank's share of it, so that
        is the test (_drate)"""
        if self.disk.per is None or not bank:
            return
        self.bank, self.bankpre = bank, min(pre, bank)
        self.disk.cap = self.reserve + bank
        self.disk.level = self.reserve / 2 + self.bankpre
        self.drate0 = self.drate
        if xcopy:
            # ONE copy on the disk: the copy UP, disk -> bounce -> XMS. The
            # copy DOWN is the playback's, charged a frame (set_xplay) - or
            # none at all in place. Two used to be charged here
            self.dslow = 1.0 + xcopy / 1000.0 * self.drate / 1024
            if (self.drate / self.dslow) * 0.99 <= self.abps:
                raise vid.V88Error("the bank's copies leave the disk %d bytes "
                                   "a second, and the sound is %d"
                                   % (self.drate / self.dslow, self.abps))

    def _drate(self):
        """The disk's rate this frame: the profile's, less the bank's copies
        while the bank holds anything (set_bank)"""
        if self.bank and self.dslow != 1.0 and \
                (getattr(self, "xmode", None) == "inplace" or
                 self.disk.level > self.reserve):
            return self.drate0 / self.dslow     # (in place: every chunk
        return self.drate0                      # goes up through the bank)

    def begin(self):
        """A frame starts (_begin), and where the LAG SCHEDULE runs (fgrid:
        98.2.1.1.1, 98.2.1.1.2) the frame's ceiling it leaves: as long as
        the frame may run and still keep the NEXT one less than `flag_lag`
        frames behind the sound - a call short of it. The player draws a
        frame a call when flipped, up to two when not; with a card's sound
        the calls come every half period, so a picture that fell behind
        catches up, and `flag_lag` is --lag in frames. A schedule that
        cannot catch up (calls a period apart: silent, or the speaker's
        clock) holds it at 1"""
        self._begin()
        if self.fgrid:
            q, g = self.q, self.fgrid
            # (a call short of `flag_lag` frames: 98.2.1.1.2's allowance,
            # one frame on a schedule that cannot catch up - the last CALL
            # before it, since the next frame starts at a call: an
            # allowance between two calls was `flag_lag * q - g`, and a
            # frame held to that still started the next at the call above
            # it, so in a burst every frame had that much less than a
            # period for nothing - 2.25 frames at 15 fps cut 89 frames of
            # Mech2 where 1.5 cut 26 and 3.0 cut 20, 98.2.1.1.3)
            lmax = (math.ceil(self.flag_lag * q / g - 1e-9) - 1) * g
            room = (q + lmax - self.flag) * (1.0 - self.spk) \
                - self.audio_cyc - HOOK_CYC
            self.ceil = min(self.ceil, max(0.0, room))

    def end(self, c):
        """...and ends (_end), the lag schedule moved on: the next frame
        starts at the first call after this one is drawn"""
        self._end(c)
        if self.fgrid:
            q, g = self.q, self.fgrid
            total = (c + self.audio_cyc + HOOK_CYC) / (1.0 - self.spk)
            if total > q:
                self.stats["fover"] += 1
            self.flag = max(0.0, math.ceil((self.flag + total - q) / g
                                           - 1e-9) * g)
            self.stats["fmaxlag"] = max(self.stats["fmaxlag"], self.flag / q)
            if self.flag >= q:          # behind by a frame or more, which
                self.stats["fbehind"] += 1  # --lag may allow
            if self.flag >= self.flag_lag * q:  # (only a frame that cannot
                self.stats["flate"] += 1    # be cut below its fixed cost)

    def _begin(self):
        """A frame starts: the buckets fill, and WHEN the player will
        decode it sets its ceiling (98.2.1.1). The hook runs a call a
        period and draws at most two frames a call, one due and one owed;
        a third due is late, and a silent play drops it (98.3). So a frame
        on time may run past its period - to `owe` periods, never to the
        second, when the call after it would find three due - and the two
        the next call then draws share one steady ceiling between them, so
        the call after that is on time again. The play with sound runs a
        call every half period off the card, so this schedule is the
        stricter of the two"""
        self.cpu.tick()
        if not self.dcurve:
            if self.bank:               # (the bank's copies, while it holds)
                self.disk.per = (self._drate() * 0.99 - self.abps) / self.fps
            self.disk.tick()
            self.refilled(self.disk.per)
            self.last_ref = self.disk.per
        self.wascut, self.cpucut = self.cpucut, False
        if self.owe is None:
            self.ceil = self.peak
            return
        q, i = self.q, self.s_i
        kw = 1.0 / (1.0 - self.spk)         # the schedule is WALL time
        over = self.audio_cyc + HOOK_CYC    # a frame's hook, not its decode
        if self.s_pair is not None:         # the second of a call's two:
            self.s_start, self.s_role = self.s_free, 2  # what is left
            self.ceil = (self.s_pair - self.s_free) / kw - over
            self.s_pair = None
            return
        k = max(i, math.ceil((self.s_free - self.s_t0) / q - 1e-9))
        n = k - i + 1                       # the frames due at that call
        if n > 2:                           # (never, by construction: the
            self.stats["late"] += n - 2     # model's own check)
            self.s_t0 += (n - 2) * q
            n = 2
        self.s_start = self.s_t0 + k * q
        if n == 2:                          # behind: this and the next
            self.s_role = 1                 # share one steady ceiling
            self.s_pair = self.s_start + (self.peak + self.audio_cyc) * kw
            self.ceil = (self.s_pair - self.s_start) / kw / 2 - over
        else:
            self.s_role = 0                 # on time: may run over, unless
            self.ceil = self.peak if self.wascut else self.owe  # the last
                                            # frame was already short of time

    def refilled(self, per):
        """A frame's refill of the disk bucket, kept for the sound's lead -
        and Auto's shadow bucket's, `amargin` under it (lean)"""
        if self.amargin and self.sh is not None and per is not None:
            self.sh = min(self.disk.cap, self.sh + per - self.amargin *
                          (per + self.abps / self.fps))
        if self.alead:
            self.arefill.append(per or 0.0)
            del self.arefill[:-self.alead]
        if self.slead:
            self.srefill.append(per or 0.0)
            del self.srefill[:-self.slead]

    def lean(self, spend):
        """BUFFER SOUND AHEAD on Auto (98.2.1.3): does this frame spend
        bytes the budget would have held back for the sound's lead, were
        the sound in step with the picture? Counted - and a file whose
        frames never did is written in step, its stream being one the in-
        step budget allows"""
        if self.slead and self.disk.per is not None:
            strict = self.disk.room() - (self.dfloor + sum(self.srefill))
            if spend > max(0.0, strict) + 1.0:
                self.stats["leaned"] = self.stats.get("leaned", 0) + 1
        # ...OR WOULD HAVE, ON A READER A LITTLE SLOWER: the same in-step
        # budget run beside it on a shadow bucket refilled AHEAD_SLACK under
        # the profile's disk (`refilled`). A stream that fits in step only
        # at the profile's own rate has no room for a reader that dips under
        # it, and the lead is that room. The owner's T69 (2026-10-19) leaned
        # on no frame, was written in step, and its card ran dry a tick at a
        # time where the stream ran 9% under 286-picomem1's 519,000 bytes a
        # second
        if self.amargin and self.disk.per is not None:
            if self.sh is None:
                self.sh = self.disk.room()
            sfloor = self.dfloor + sum(self.srefill) * (1.0 - self.amargin)
            near = self.sh - sfloor - spend
            if near < -1.0:
                self.stats["near"] = self.stats.get("near", 0) + 1
            lo = self.stats.get("nearmin")
            if lo is None or near < lo:
                self.stats["nearmin"] = near
            self.sh -= spend

    def disk_floor(self):
        """What the disk bucket may not be spent below: a READ_SEQ in
        flight (dfloor), and THE SOUND'S LEAD (audio_lead) - the reader
        must stay a block of sound ahead of the frame playing, and at the
        frame now being encoded that is the lead's frames later, so the
        bucket must still hold what the disk refilled over the last
        `alead` frames. Without it a burst spends the ring down to one
        slot while the card wants a block of it queued, and at 5.5 kHz
        ADPCM4 the card halted mid-burst (the owner's 5150, 98.2.1.3).
        The level CAN now be under it - the lead rises with the refill,
        and a small reserve starts below it - so a caller clamps the room
        at 0: negative, the retry's `eb *= room / len` flipped its sign
        and spent bytes the bucket did not have"""
        return self.dfloor + sum(self.arefill)

    def disk_rel(self, share):
        """The disk's rate at a hook `share` of the period, over its rate
        at `avg`: the profile's measured points, straight between them"""
        pts = self.dcurve
        share = min(max(share, pts[0][0]), pts[-1][0])
        for (s0, r0), (s1, r1) in zip(pts, pts[1:]):
            if share <= s1:
                return r0 + (r1 - r0) * (share - s0) / (s1 - s0)
        return pts[-1][1]

    def _end(self, c):
        """...and ends, having cost the model `c` cycles"""
        if self.dcurve:                 # (the speaker's pulses are the
                                        # reader's CPU too, 98.2.15)
            share = (c + self.audio_cyc + HOOK_CYC) / self.q + self.spk
            per = ((self._drate() if self.bank else self.drate) *
                   self.disk_rel(share) * 0.99 - self.abps) / self.fps
            self.disk.level = min(self.disk.cap, self.disk.level + per)
            self.refilled(per)
            self.last_ref = per
        if self.owe is None:
            return
        f = self.s_start + (c + self.audio_cyc + HOOK_CYC) / (1.0 - self.spk)
        if self.s_role == 0 and f > self.s_t0 + (self.s_i + 1) * self.q:
            self.stats["owed"] += 1     # ran over: the next is held
        elif self.s_role == 1:
            self.stats["held"] += 1     # drawn a period late
        self.s_free = f
        self.s_i += 1

    def frame(self, target, audio=b""):
        """(the frame's writes, its record): the target's changes, or as
        many of them as the budgets allow, best first"""
        g = self.g
        self.begin()
        self.stats["frames"] += 1
        diff = target != self.screen
        self.age = np.where(diff, self.age + 1, 0)
        ys, xs = np.nonzero(diff)
        cbop = self.cb_make(target)     # (one bit's codebook, 98.1.10)
        if not len(ys):
            self.stats["exact"] += 1
            return cbop, vid.record(cbop, g, audio)
        self.tv[self.idx] = target
        sp = vid.spans((self.base[ys] + xs).tolist(), self.tsurf, g)
        if self.thr:                    # --aim size (98.2.1.4)
            sp = self.worthwhile(target, sp)
            if not sp:
                self.stats["exact"] += 1
                return [], vid.record([], g, audio)
        cyc_room = min(self.cpu.room(), self.ceil)
        byte_room = max(0.0, self.disk.room() - self.disk_floor() -
                        (len(cbop[0][1]) + 3 if cbop else 0))
        costs = span_costs(((bs, run) for a, bs, run in sp),
                           table=self.ct)
        tc, tb = totals(costs)
        order = None
        er = cyc_room - self.ct[0]
        eb = min(byte_room, self.rec_max - len(audio)) - REC_OVER
        best = None
        blk, bops = None, list(cbop)
        for attempt in range(16 if self.live else 8):
            bops = list(cbop)
            if tc <= er and tb <= eb:
                chosen = sp
            else:
                if order is None:
                    self.capb = self.rec_max - len(audio) < byte_room
                    self.why(tc, tb, er, eb, cyc_room)
                    sp = split_big(sp)
                    costs = span_costs(((bs, run) for a, bs, run in sp),
                                       table=self.ct)
                    tc, tb = totals(costs)
                    order = self.rank(target, sp, costs, er, eb)
                    if self.blocks:     # VERSION 2's blocks (98.1.10)
                        blk = self.block_cands1(target, er, eb) \
                            if self.pal is None else \
                            self.block_cands8(target, er, eb)
                        if blk is not None:
                            order, sblk = self.block_merge8(
                                order, sp, blk, 2 if self.pal is None else 4)
                chosen, uc, ub = [], 0, 0
                taken, dead = set(), set()
                for i in order:
                    if i < 0:           # a BLOCK: it and the spans under it
                        j = ~i          # exclude each other
                        bid = blk[6][j]
                        if bid in taken or bid in dead:
                            continue
                        c, b = blk[3][j], blk[4][j]
                        if uc + c > er or ub + b > eb:
                            continue
                        taken.add(bid)
                        bops.append((blk[1][j], blk[2][j], blk[0][j]))
                        uc += c
                        ub += b
                        continue
                    if blk is not None and sblk.get(i, set()) & taken:
                        continue
                    c, b = costs[i]
                    if uc + c > er or ub + b > eb:
                        if self.cut == "tear":
                            break       # (one tear line: 98.2.1.2.1)
                        continue
                    chosen.append(sp[i])
                    if blk is not None:
                        dead |= sblk.get(i, set())
                    uc += c
                    ub += b
                chosen.sort()
            rec = vid.record(chosen + bops, g, audio, limit=None)
            # the model's estimate is per span; the record is measured, and
            # a frame over its ceiling tries again with the estimate scaled
            mc = self.cost(rec)
            if mc <= cyc_room and len(rec) - len(audio) <= byte_room and \
                    len(rec) <= self.rec_max:
                f = self.grow(chosen is sp, rec, audio, byte_room, mc,
                              cyc_room)
                if f is None:
                    break
                best = (chosen, rec, bops)  # (--cut fill/tear: room left)
                er, eb = er * f, eb * f
                continue
            if best is not None:
                chosen, rec, bops = best
                break
            # (never below 0: an overdrawn room made the ratio negative,
            # and a negative estimate times it came back POSITIVE - a frame
            # then spent what it did not have; VIDEO-OVERAGE-PLAN 4.2)
            er *= max(0.0, min(0.97, cyc_room / mc))
            eb *= min(0.97, min(byte_room, self.rec_max - len(audio)) /
                      max(1, len(rec) - len(audio)))
        else:
            if best is not None:
                chosen, rec, bops = best
            elif self.live and order is not None:
                chosen, rec = self.trim(sp, order, g, audio, cyc_room,
                                        byte_room)
                bops = []
        if len(rec) > self.rec_max:     # NEVER A RECORD THE FILE CANNOT
            chosen, rec = [], vid.record(cbop, g, audio)    # HOLD (unfit)
            bops = list(cbop)
            self.stats["unfit"] += 1
        if chosen is not sp:
            self.torn(chosen, 0)
        for a, bs, run in chosen:
            self.surf[a:a + len(bs)] = bs
        self.put_blocks(bops)           # (98.1.10)
        self.screen = self.sv[self.idx]
        if chosen is sp:
            self.stats["exact"] += 1
        else:                           # what is still wrong once it is
            self.stats["cut"] += 1      # on: a span bridges bytes that
            self.stats["bytes_left"] += int(    # were already right
                (target != self.screen).sum())
        # (the blocks with the spans: the Writer builds the record again from
        # what this returns, 98.1.10)
        return (chosen + bops if bops else chosen), rec

    def trim(self, sp, order, g, audio, cyc_room, byte_room):
        """LIVE: the retries did not bring the frame under its room - a
        blit's cost is its RECTANGLES, which dropping a scatter of spans
        hardly shrinks. So the most the ranking's PREFIX can keep and fit,
        found by halving; none fits, nothing (the picture converges later)"""
        lo, hi, best = 0, len(order), None
        while lo <= hi:
            k = (lo + hi) // 2
            ch = sorted(sp[i] for i in order[:k])
            rec = vid.record(ch, g, audio, limit=None)
            if self.cost(rec) <= cyc_room and \
                    len(rec) - len(audio) <= byte_room and len(rec) <= self.rec_max:
                best, lo = (ch, rec), k + 1
            else:
                hi = k - 1
        if best is None:
            best = [], vid.record([], g, audio)
        return best

    def why(self, tc, tb, er, eb, cyc_room):
        """WHICH BUDGET CUT the frame (VIDEO-PLAN 15.8: what constrains
        playback): the one the whole frame overruns by more - the disk, the
        CPU's per-second average, or the per-frame ceiling"""
        self.last_why = "cpu"           # (98.2.1.8's plan, unless below)
        if tb / max(eb, 1.0) >= tc / max(er, 1.0):
            self.last_why = "disk"
            if getattr(self, "capb", False):
                # THE RECORD's room and not the disk's (98.1.4.1): a frame
                # too big for its super-packet - the one cut a lossless
                # encode can have - kept by frame to say where
                self.last_why = "cap"
                self.stats["cap"] += 1
                if not hasattr(self, "capcuts"):
                    self.capcuts = []
                self.capcuts.append(self.stats["frames"] - 1)
            else:
                self.stats["disk"] += 1
            return
        self.cpucut = True
        # cyc_room is net of the back page's debt (a flipped file's prev_c),
        # so it is not what to ask: the BUCKET against the CEILING is. Asked
        # of cyc_room, every cut on a flipped file read as the average's,
        # when the 1-second bucket had seconds of room and the flip schedule
        # had cut the ceiling below --peak - so raising --avg or --peak
        # changed nothing a summary seemed to blame it on
        if self.cpu.room() < self.ceil:
            self.stats["cpu"] += 1
        elif self.fgrid is not None and self.ceil < self.peak:
            self.stats["flip"] += 1     # the lag schedule (98.2.1.1.1-2)
        else:
            self.stats["peak"] += 1

    def pixels(self, st):
        """A screen state (or a target) as pixels: (h, w) grey 0..1, or
        (h, w, 3) colour 0..1; None where the format has no pixel form"""
        if self.dmode == "bits":
            return np.unpackbits(st, axis=1).astype(np.float32)
        if self.dmode == "pal":
            return self.dpal[st] / 255.0
        if self.dmode == "idx":
            bits = 8 // self.ppb
            v = np.unpackbits(st[..., None], axis=2).reshape(
                st.shape[0], -1, bits) @ (1 << np.arange(bits - 1, -1, -1))
            return self.dpal[v] / 255.0
        return None

    def blur(self, im):
        """What the eye averages: a binomial low-pass 5 rows tall and as
        wide as the pixel's shape makes the same distance (blur_r a row) -
        most of the 8 x 8 Bayer tile, whose patterns it is to see past"""
        out = im.astype(np.float32)
        for axis, r in ((0, 2), (1, 2 * self.blur_r)):
            k = _binomial(r)
            pad = [(0, 0)] * out.ndim
            pad[axis] = (r, r)
            pp = np.pad(out, pad, mode="edge")
            n = out.shape[axis]
            # the taps summed in order, as sum() did - a view each rather
            # than a copy, and one sum rather than a new one a tap
            sl = [slice(None)] * out.ndim
            sl[axis] = slice(0, n)
            acc = k[0] * pp[tuple(sl)]
            acc += 0                    # (sum()'s start: -0.0 is 0.0)
            tmp = np.empty_like(acc)
            for i in range(1, 2 * r + 1):
                sl[axis] = slice(i, i + n)
                np.multiply(k[i], pp[tuple(sl)], out=tmp)
                acc += tmp
            out = acc
        return out

    def set_pal(self, pal):
        """A NEW SCENE's colours (98.1.3.2.3, 98.1.3.2.3.2): `pal` as the
        ranking and the report see it - and every pixel made through the
        last one forgotten, the targets' AND the screen's. The caches are
        keyed by the target and by the screen's bytes, which a palette
        change does not touch, so without this a scene's first frames were
        weighed in the last scene's colours. Rebound and not cleared: a
        layer's encoder may hold the same dict (LayerEnc._clone)"""
        self.pal = self.dpal = pal
        self.far = None
        self.tcache, self.scache = {}, None

    def tpix(self, T):
        """[T, pixels(T), blur of them]: a target's, made once - the
        look-ahead weighs a target on each of the frames before it, and
        the report on its own (the blur made when first asked for)"""
        c = self.tcache.get(id(T))
        if c is None or c[0] is not T:
            c = self.tcache[id(T)] = [T, self.pixels(T), None]
            while len(self.tcache) > self.look + 4:
                del self.tcache[next(iter(self.tcache))]
        return c

    def tblur(self, T):
        c = self.tpix(T)
        if c[2] is None:
            c[2] = self.blur(c[1])
        return c[2]

    def spix(self):
        """...and the screen's: the report's, after a frame, is the
        look-ahead's before the next - checked against a copy, as MODEX
        writes its screen in place"""
        c = self.scache
        if c is None or not np.array_equal(c[0], self.screen):
            c = self.scache = [self.screen.copy(), self.pixels(self.screen),
                               None]
        return c

    def sblur(self):
        c = self.spix()
        if c[2] is None:
            c[2] = self.blur(c[1])
        return c[2]

    @staticmethod
    def dist(a, b):
        d = np.abs(a - b)
        if d.ndim != 3:
            return d
        if _SUM3:                       # (the same sum, ~7x as fast)
            return (d[..., 0] + d[..., 1] + d[..., 2]) / 3.0
        return d.sum(2) / 3.0

    def value(self, target):
        """PER PIXEL, what writing the target now is worth (98.2.1.2),
        summed over this frame and, with a look-ahead, the next `look`: at
        each, how much nearer its target the pixel is for holding this
        frame's value than for keeping the screen's. A pixel about to
        change again is worth little, one about to change BACK to what the
        screen holds less than nothing.
        VISIBLE error discounts a pixel's error by the share of it an eye
        sees: the pictures low-passed as an eye averages them, against the
        plain error low-passed the same way. A dither pattern swapped for
        another of the same grey is mostly unseen and is discounted to a
        quarter; a picture wrong in its greys is not discounted at all.
        (The low-passed error's own gradient, which amplifies an edge, was
        tried first and measured WORSE: it starves a region of middling
        contrast for ever - 98.2.1.2.) None: no pixel form"""
        S = self.spix()[1]
        if S is None:
            return None
        T0 = self.tpix(target)[1]
        bS = self.sblur() if self.vis else None
        v = 0.0
        if self.edges:
            # the view as seen's edge term, priced the way `plain` is: the
            # screen's edges against each target's, less the frame's own
            # target's - with the picture kind's weights (seen_weights), so
            # on dither only the edges a cut ADDS count
            wl, _, wa, wo, _, _, _, _, re = seen_weights(T0)
            lum = (lambda x: x) if T0.ndim == 2 else \
                (lambda x: x @ _SEEN_LUM)
            eg = lambda x: _seen_grad(_seen_box(lum(x).astype(np.float32),
                                                re))
            gS, g0 = eg(S), eg(T0)
            ew = self.edges / max(wl, 1e-6)
        for k, T in enumerate([target] + self.future[:self.look]):
            Tk = T0 if k == 0 else self.tpix(T)[1]
            plain = self.dist(S, Tk) - self.dist(T0, Tk)
            if self.edges:
                gk = g0 if k == 0 else eg(Tk)
                ed = lambda g: wa * np.maximum(g - gk, 0) + \
                    wo * np.maximum(gk - g, 0)
                v = v + ew * (ed(gS) - ed(g0))
            if not self.vis:
                v = v + plain
                if k == 0:
                    vnow = v
                continue
            # the share of the pixel's local error an eye sees: the
            # blurred pictures' difference over the blurred plain one -
            # 1 where the picture is wrong, near 0 where only the dither
            # pattern is. A swap is discounted to a quarter, never more,
            # so a pattern still converges; nothing is amplified
            vis = self.dist(bS, self.tblur(T)) / \
                np.maximum(self.blur(self.dist(S, Tk)), 1e-3)
            v = v + plain * np.clip(vis, 0.25, 1.0)
            if k == 0:
                vnow = v
        # THE LOOK-AHEAD'S FLOOR (--lookahead-floor, 98.2.1.2: the owner's
        # headlights): the frames after it may take a pixel's worth
        # down to `lfloor` of what writing it is worth NOW and no further -
        # a headlight that moves on next frame still lost 8x of its worth
        # to them and went unwritten for dark road
        if self.look and self.lfloor:
            v = np.maximum(v, self.lfloor * vnow)
        if self.attend:
            v = v * self.attention(target, T0)
        return v

    def attention(self, target, T0):
        """WHERE AN EYE LOOKS (--attend, VIDEO-SEEN-PLAN 3): the view as
        seen's attention map (seen_share's: brightness, and motion against
        the camera's) for this frame's target, made once a target - its
        mean 1, so a frame's whole worth is what it was and only where it
        is spent moves - and taken to `attend`: 0 flat, 1 the map"""
        c = self.attc
        if c is not None and c[0] is target:
            return c[2]
        lT = T0 if T0.ndim == 2 else T0 @ _SEEN_LUM
        lT = lT.astype(np.float32)
        lP = c[1] if c is not None and c[1].shape == lT.shape else None
        att = seen_attention(lT, lP)
        att = att / max(float(att.mean()), 1e-6)
        if self.attend != 1.0:
            att = np.maximum(1.0 + self.attend * (att - 1.0), 0.0)
        if att.shape != T0.shape[:2]:
            att = None
        self.attc = (target, lT, 1.0 if att is None else att)
        return self.attc[2]

    def bytevalue(self, target):
        """value() summed to the target's bytes, or None"""
        v = self.value(target)
        if v is None:
            return None
        h, wb = target.shape[:2]
        return v.reshape(h, wb, -1).sum(2)

    def worthwhile(self, target, sp):
        """--aim size (98.2.1.4): the spans worth their bytes - what
        value() says writing them fixes, over the look-ahead, at least
        `thr` a byte of stream. The rest stays off the disk even when the
        budget would take it: dither shimmer, and changes about to be
        undone. Weighted by age as rank() is, so what stays wrong comes to
        be worth it and a still picture still converges"""
        bv = self.bytevalue(target)
        if bv is None:
            return sp
        self.wv[:] = 0                  # a pixel wrong for a while is worth
        self.wv[self.idx] = bv * (1.0 + self.age / 8.0)     # fixing: a
                                        # still picture converges
        cs = np.concatenate(([0.0], np.cumsum(self.wv)))
        out = [x for x in sp
               if cs[x[0] + len(x[1])] - cs[x[0]] >= self.thr * (len(x[1]) + 2)]
        self.stats["skipped"] += len(sp) - len(out)
        return out

    def measure(self, target):
        """THE PICTURE AS PLAYED, a frame (the report's `picture:` line):
        its error as seen, its pixels wrong, and its FLICKER - pixels that
        go back to what they were two frames ago, as a dither pattern
        swapping back and forth does. With a `meter` (Meter) it is measured
        in another process, and only the target before is kept here"""
        if getattr(self, "meter", None) is not None:
            self.meter.put(self, target)
            return
        r = self.measure_of(target)
        if r is not None:
            self.measured(r)

    def measure_of(self, target):
        """measure()'s frame -> (its mean error as seen, its share VISIBLY
        wrong, the view as seen, its pixels wrong, the flicker back and the
        source's, each None where there is no frame two before) - the
        frames before it the only state it keeps; None, no pixels"""
        S = self.spix()[1]
        if S is None:
            return None
        T = self.tpix(target)[1]
        ev = self.dist(self.sblur(), self.tblur(target))
        m = float(ev.mean())
        # VISIBLY WRONG (2026-10-09): the share of the frame whose picture,
        # as the eye averages it, is off by VIS_BAD or more - a band torn
        # off a scene and left behind is most of a frame wrong by a lot,
        # which the mean over a whole clip of pixels made 0.11%
        bad = float((ev > VIS_BAD).mean())
        seen = seen_share(S, T, self.q_stp)
        self.q_stp = T
        ne = S != T
        if ne.ndim == 3:
            ne = ne.any(2)
        fl = []
        for now, hist in ((S, "q_prev"), (T, "q_tprev")):
            p1, p2 = getattr(self, hist)
            if p2 is not None:
                x, y = now == p2, now != p1
                if x.ndim == 3:
                    x, y = x.all(2), y.any(2)
                fl.append(float((x & y).sum()))
            else:
                fl.append(None)
            setattr(self, hist, (now, p1))
        return m, bad, seen, float(ne.mean()), fl[0], fl[1]

    def measured(self, r):
        """measure_of()'s frame added to the report's sums"""
        m, bad, seen, wrong, fl, tfl = r
        self.q_vis += m
        self.q_frames.append(m)
        self.q_bad.append(bad)
        self.q_seen.append(seen)
        self.q_wrong += wrong
        if fl is not None:
            self.q_flick += fl
        if tfl is not None:
            self.q_tflick += tfl

    def when_bad(self, shot):
        """`shot` (shot_cut's) counted VISIBLY broken where the frame just
        measured was - when its measure is in: at once, or with a meter as
        it reads its frames back, in order"""
        if getattr(self, "meter", None) is not None:
            self.meter.mark(shot)
        elif self.q_bad and self.q_bad[-1] >= VIS_AREA:
            shot[3] += 1

    def order(self, p, a):
        """rank()'s order for spans of priority `p` at addresses `a`, by
        --cut (98.2.1.2.1): "rank" ranked()'s; "fill" ranked()'s with what
        the look-ahead scores worth nothing LAST rather than left out, so
        the room a cut leaves is spent; "tear" screen order from row
        `tcur`, wrapping, which the choosing loop takes until the first
        that does not fit - one tear line, not rows scattered over it"""
        if self.cut == "tear":
            if getattr(self, "_rowarr", None) is None:
                self._rowarr = np.asarray(self.g.rowof, np.int64)
            rows = self._rowarr[a]
            return np.lexsort((a, (rows - self.tcur) % self.g.h)).tolist()
        return ranked(p, self.look if self.cut == "rank" else 0)

    def torn(self, chosen, a_at):
        """--cut tear: a cut frame stopped after `chosen` (spans whose
        address is item `a_at`); the next cut frame starts on its last
        row, which may be half done"""
        if self.cut == "tear" and chosen:
            self.tcur = self.g.rowof[chosen[-1][a_at]]

    def subover(self, masks):
        """What a record's sub-records cost beyond their spans, bytes: a
        mask byte each, as it always was - or with --bands N up to N times
        as many sub-records, each its mask, ten list ends and a segment's
        head (98.2.1.2.2). The estimate
        left the bands out and a full 320 x 240 Mode X frame at 63.5 KB in
        12 bands was built 2.6 KB past its room - past the record's length
        word, which ended the encode; a record that still comes out over is
        measured now and cut again (vid.record's limit=None)"""
        return masks * self.bands * 14 if self.bands else masks

    def banded(self, chosen, masks):
        """by_mask()'s sub-records - or, with --bands N (98.2.1.2.2), the
        same for each of N bands of rows in turn, top to bottom. A Mode X or
        16-colour frame is drawn sub-record by sub-record, so one caught
        mid-draw (no page flip, a frame longer than a refresh) shows every
        plane of the bands above, ONE band combed, and the frame before
        below: a tear along a line, where a whole-frame sub-record per mask
        combs every fourth column and the rows of every list"""
        if not self.bands:
            return by_mask(chosen, masks)
        h, n = self.g.h, self.bands
        rowof = self.g.rowof
        groups = [[] for _ in range(n)]
        for c in chosen:
            groups[min(n - 1, rowof[c[1]] * n // h)].append(c)
        # WRITE MODE 3's sub-records after EVERY plane one (98.1.3.2.4): the
        # player sets the mode at the first and puts it back at the record's
        # end, so the bands' planes go first, top to bottom, and then their
        # colours, top to bottom (the options audit: --bands with a clip's
        # own sixteen made records every reader refused)
        pm = [m for m in masks if m < vid.SUB2_WM3]
        cm = [m for m in masks if m >= vid.SUB2_WM3]
        out = []
        for grp in groups:
            out += by_mask(grp, pm)
        if cm:
            for grp in groups:
                out += by_mask(grp, cm)
        return out

    def grow(self, whole, rec, audio, byte_room, mc, cyc_room):
        """--cut fill or tear (98.2.1.2.1): a cut record that FITS with
        room to spare - the per-span estimate counts every change at its
        length, where the record finds the runs of one colour inside them
        and stores each in four bytes, so on flat-coloured pictures it is
        about twice the record and a cut frame came out half its room.
        The factor to grow the estimate's allowances by and choose again,
        or None: full enough, the whole frame, or rank (which never grew)"""
        if self.cut == "rank" or whole or self.live:
            return None
        f = min(min(byte_room, self.rec_max - len(audio)) /
                max(1.0, len(rec) - len(audio)), cyc_room / max(1.0, mc))
        return None if f < 1.04 else min(f, 4.0)

    def rank(self, target, sp, costs, er, eb):
        """The spans' indexes, most pixels fixed per unit of the scarcer
        budget first; a pixel wrong for a while outweighs a fresh one - or,
        with a look-ahead or visible error, per value() (98.2.1.2)"""
        g = self.g
        bv = self.bytevalue(target) if self.look or self.vis else None
        if bv is not None:
            bits = bv
        elif self.pal is not None:
            bits = np.abs(self.pal[target] - self.pal[self.screen]).sum(2) \
                / 32.0
        else:
            xor = np.bitwise_xor(target, self.screen)
            bits = np.unpackbits(xor, axis=1).reshape(g.h, g.wb, 8).sum(2)
        self.wv[:] = 0
        self._bytew = bits * (1.0 + self.age / 8.0)     # (the blocks weigh
        self.wv[self.idx] = self._bytew                 # with it, 98.1.10)
        cs = np.concatenate(([0.0], np.cumsum(self.wv)))
        a, n, c, b = span_arrays(sp, costs)
        self._rankp = (cs[a + n] - cs[a]) / per_budget(c, b, er, eb)
        return self.order(self._rankp, a)

    # VERSION 2's BLOCKS on LIN320 (98.1.10): Microsoft Video-1's three, 4 x
    # 4 pixels - FILL (one colour), TWO (a mask and two) and EIGHT (a pair a
    # 2 x 2 quadrant) - each worth the share of its pixels' error it takes
    # away as seen, its 2 x 2 cells against the target's
    def near8(self):
        """RGB -> the palette's nearest entry, by a 32^3 table"""
        key = self.pal.tobytes()
        if getattr(self, "_n8k", None) != key:
            pal = self.pal.astype(np.float64)
            q = (np.arange(32) * 8 + 4).astype(np.float64)
            grid = np.stack(np.meshgrid(q, q, q, indexing="ij"), -1) \
                .reshape(-1, 3)
            t = np.empty(len(grid), np.int64)
            for o in range(0, len(grid), 1024):
                t[o:o + 1024] = ((grid[o:o + 1024, None] - pal[None]) ** 2) \
                    .sum(-1).argmin(-1)
            self._n8t, self._n8k = t, key
        t = self._n8t
        return lambda c: t[(np.clip(c, 0, 255).astype(np.int64) >> 3) @
                           np.array([1024, 32, 1])]

    def block_cands8(self, target, er, eb, wpx=None, mx=False):
        """Video-1's candidates over a picture of PIXELS - LIN320's bytes,
        or with `mx` Mode X's pixels, a block one plane byte of four rows -
        weighed by `wpx`, the ranking's per-pixel weights"""
        g = self.g
        W = target.shape[1]
        H, W4 = g.h // 4 * 4, W // 4 * 4
        if not H or not W4:
            return None
        hb, wb = H // 4, W4 // 4
        T = target[:H, :W4].reshape(hb, 4, wb, 4)
        S = self.screen[:H, :W4].reshape(hb, 4, wb, 4)
        by, bx = np.nonzero((T != S).any(axis=(1, 3)))
        if not len(by):
            return None
        pal = self.pal.astype(np.float64)
        near = self.near8()
        Tb = T[by, :, bx, :].reshape(-1, 16)
        Sb = S[by, :, bx, :].reshape(-1, 16)
        tr = pal[Tb]

        def cells(x):                   # (n, 16, 3) -> its 2 x 2 cells
            return x.reshape(-1, 2, 2, 2, 2, 3).mean(axis=(2, 4))
        tc = cells(tr)

        def miss(ix):
            return ((cells(pal[ix]) - tc) ** 2).sum(axis=(1, 2, 3))
        e0 = np.maximum(miss(Sb), 1e-6)
        Wt = (self._bytew if wpx is None else wpx)[:H, :W4].reshape(
            hb, 4, wb, 4)[by, :, bx, :].sum(axis=(1, 2))

        def split(px):                  # (n, k, 3): a mask and two colours
            lum = px @ np.array([0.299, 0.587, 0.114])
            hi = lum > lum.mean(axis=1)[:, None]
            n1 = hi.sum(1)
            n0 = px.shape[1] - n1
            m1 = (px * hi[..., None]).sum(1) / np.maximum(n1, 1)[:, None]
            m0 = (px * ~hi[..., None]).sum(1) / np.maximum(n0, 1)[:, None]
            m1 = np.where((n1 == 0)[:, None], m0, m1)
            m0 = np.where((n0 == 0)[:, None], m1, m0)
            return hi, near(m1), near(m0)
        fill = np.repeat(near(tr.mean(1))[:, None], 16, 1)
        hi, c1, c0 = split(tr)
        two = np.where(hi, c1[:, None], c0[:, None])
        # the quadrants, in v8_block's order: left-top, right-top,
        # left-bottom, right-bottom
        qi = np.array([[0, 1, 4, 5], [2, 3, 6, 7], [8, 9, 12, 13],
                       [10, 11, 14, 15]])
        eight = np.empty_like(Tb)
        ehi = np.zeros((len(Tb), 16), bool)
        qc = []
        for q in range(4):
            h, a1, a0 = split(tr[:, qi[q]])
            eight[:, qi[q]] = np.where(h, a1[:, None], a0[:, None])
            ehi[:, qi[q]] = h
            qc.append((a1, a0))
        bitw = 1 << np.arange(16)
        m2 = (hi * bitw).sum(1)
        m8 = (ehi * bitw).sum(1)
        addr = self.base[by * 4] + (bx if mx else bx * 4)
        kinds, ads, pays, cs, bs, gs, ids = [], [], [], [], [], [], []
        for kind, ix in ((("mfill", "mtwo", "meight") if mx else
                          ("fill8", "two8", "eight8"))[0], fill), \
                ((("mfill", "mtwo", "meight") if mx else
                  ("fill8", "two8", "eight8"))[1], two), \
                ((("mfill", "mtwo", "meight") if mx else
                  ("fill8", "two8", "eight8"))[2], eight):
            gain = Wt * np.clip(1.0 - miss(ix) / e0, 0.0, 1.0)
            c = vid.blk_cost(kind, self.ct, self.csub, self.cwm3) + 20 + \
                self.ct[1] / 4
            b = 1.6 + vid.BLK_KIND[kind][0]
            for i in np.nonzero(gain > 0)[0]:
                if kind in ("fill8", "mfill"):
                    pay = bytes([int(fill[i, 0])])
                elif kind in ("two8", "mtwo"):
                    pay = struct.pack("<HBB", int(m2[i]), int(c1[i]),
                                      int(c0[i]))
                else:
                    pay = struct.pack("<H", int(m8[i])) + bytes(
                        int(x) for q in range(4) for x in (qc[q][0][i],
                                                           qc[q][1][i]))
                kinds.append(kind)
                ads.append(int(addr[i]))
                pays.append(pay)
                cs.append(c)
                bs.append(b)
                gs.append(gain[i])
                ids.append(int(i))
        if not kinds:
            return None
        cs, bs, gs = (np.array(x, np.float64) for x in (cs, bs, gs))
        return (kinds, ads, pays, cs, bs, gs / per_budget(cs, bs, er, eb),
                np.array(ids), addr)

    def block_merge8(self, order, sp, blk, width=4):
        """rank()'s order with the blocks merged in by priority (a block as
        ~j) and each span's blocks under it"""
        g = self.g
        kinds, ads, pays, cs, bs, pr, ids, top = blk
        offs = [g.base[r] - g.base[0] + x for r in range(4)
                for x in range(width)]
        at = np.full(65536 + 8, -1, np.int64)
        for i, a in enumerate(top.tolist()):
            for o in offs:
                at[a + o] = i
        sblk = {}
        for i, (a, bs_, run) in enumerate(sp):
            v = at[a:a + len(bs_)]
            v = v[v >= 0]
            if len(v):
                sblk[i] = set(v.tolist())
        out = [(float(self._rankp[i]), i) for i in order] + \
            [(float(pr[j]), ~j) for j in range(len(kinds))]
        out.sort(key=lambda x: -x[0])
        return [i for p, i in out], sblk

    def put_ops(self, ops):
        """Another encoder's writes on this one's screen - the LAYER's
        (LayerEnc): the base's record, applied before its own"""
        for a, bs, run in ops:
            if not isinstance(run, str):
                self.surf[a:a + len(bs)] = bs
        self.put_blocks([o for o in ops if isinstance(o[2], str)])
        self.screen = self.sv[self.idx]

    def put_blocks(self, bops):
        """VERSION 2's blocks (98.1.10) on the surface: a codebook first,
        then LIN320's blocks or one bit's tiles"""
        g = self.g
        for a, pay, kind in bops:
            if kind == "cbook":
                if self.cbook is None:
                    self.cbook = bytearray(2048)
                self.cbook[a * 8:a * 8 + len(pay)] = pay
                self.cbt = None
        for a, pay, kind in bops:
            if kind == "cbook":
                continue
            rows = vid.blk_rows(g, a)
            if kind == "tile":
                e = self.cbook[pay[0] * 8:pay[0] * 8 + 8]
                for r, ra in enumerate(rows):
                    self.surf[ra:ra + 2] = e[2 * r:2 * r + 2]
            else:
                px = vid.v8_block(kind, pay)
                for r, ra in enumerate(rows):
                    self.surf[ra:ra + 4] = bytes(px[r * 4:r * 4 + 4])
            self.stats["blocks"] = self.stats.get("blocks", 0) + 1

    # ONE BIT's TILES (98.1.10): 16 x 4 pixels, two bytes of four rows, a
    # byte naming one of a codebook's 256 - the codebook made whole at
    # frame 0 and after every key from the target's own most frequent tiles
    def tiles_of(self, pic):
        """A one-bit picture's 16 x 4 tiles as uint64 codes: (hb, wb2)"""
        g = self.g
        H, W2 = g.h // 4 * 4, g.wb // 2 * 2
        t = pic[:H, :W2].reshape(H // 4, 4, W2 // 2, 2).transpose(0, 2, 1, 3)
        return np.ascontiguousarray(t).reshape(H // 4, W2 // 2, 8) \
            .view(np.uint64)[..., 0]

    def cb_make(self, target):
        """[the codebook op] when one is due (98.1.10), else []"""
        due, self.cb_due = self.cb_due, False
        if not (self.blocks and self.pal is None and due):
            return []
        t = self.tiles_of(target).ravel()
        u, c = np.unique(t, return_counts=True)
        cb = u[np.argsort(-c, kind="stable")][:256]
        cb = np.concatenate([cb, np.zeros(256 - len(cb), np.uint64)])
        return [(0, cb.astype("<u8").tobytes(), "cbook")]

    def block_cands1(self, target, er, eb):
        if self.cbook is None:
            return None
        g = self.g
        T, S = self.tiles_of(target), self.tiles_of(self.screen)
        by, bx = np.nonzero(T != S)
        if not len(by):
            return None
        if self.cbt is None:
            self.cbt = np.frombuffer(bytes(self.cbook), "<u8").astype(
                np.uint64)
        pop = lambda x: np.unpackbits(x.view(np.uint8).reshape(
            x.shape + (8,)), axis=-1).sum(-1)
        Tt, St = T[by, bx], S[by, bx]
        d = pop(Tt[:, None] ^ self.cbt[None])          # (n, 256)
        best = d.argmin(1)
        hd = d[np.arange(len(best)), best]
        hd0 = np.maximum(pop(Tt ^ St), 1)
        H, W2 = len(T) * 4, T.shape[1] * 2
        Wt = self._bytew[:H, :W2].reshape(len(T), 4, T.shape[1], 2)[
            by, :, bx, :].sum(axis=(1, 2))
        gain = Wt * np.clip(1.0 - hd / hd0, 0.0, 1.0)
        addr = self.base[by * 4] + bx * 2
        keep = np.nonzero(gain > 0)[0]
        if not len(keep):
            return None
        c = vid.blk_cost("tile", self.ct, self.csub, self.cwm3) + 20 + \
            self.ct[1] / 4
        cs = np.full(len(keep), c)
        bs = np.full(len(keep), 2.6)
        return (["tile"] * len(keep), [int(addr[i]) for i in keep],
                [bytes([int(best[i])]) for i in keep], cs, bs,
                gain[keep] / per_budget(cs, bs, er, eb), keep, addr)

    def clone_screen(self, b):
        """...and this encoder's screen made a COPY of `b`'s"""
        self.surf = bytearray(b.surf)
        self.sv = np.frombuffer(self.surf, dtype=np.uint8)
        self.screen = self.sv[self.idx]

    def cost(self, rec):
        """A record's cycles in the model: its decode, and on a LIVE file
        (98.3.10.2) the blit of the runs the writer will give it. A pass is
        a TICK (98.3.10) and blits the union of the frames it drew, so a
        frame is charged its runs' blit times the passes a frame - 18.2 a
        second over the frame rate, at most 1: measured, Bad Apple made
        --live herc at 30 fps modelled 31.6% of the machine in blit this
        way and gfx_blit1 took 32.1%"""
        g = self.g
        c = vid.cycles_of(rec, True, table=self.ct, sub=self.csub,
                          sub2=g.sub2, wm3=self.cwm3, bitplanes=True) \
            if g.bitplanes else vid.cycles_of(
                rec, table=self.ct, blocks=("onebit" if self.pal is None
                                            else True)
                if getattr(g, "v2", False) else False)
        if self.live:
            c += self.blitf * sum(vid.blit_cost(y1 - y0, x1 - x0, g.planes)
                                  for y0, y1, x0, x1 in vid.live_runs(rec, g))
        return c

    def charge(self, rec, abytes):
        """What the record actually costs, measured, off the buckets (the
        sound's bytes are taken off the disk's rate at the start) - and
        with page-ahead (98.2.1.8) the hidden page's record beside it"""
        c = self.cost(rec)
        pc, pb = self.pa_spend
        self.pa_spend = (0.0, 0.0)
        self.lean(len(rec) - abytes)
        self.cpu.spend(c + pc)
        self.disk.spend(len(rec) - abytes + pb)
        self.end(c + pc)
        return c + pc


class Flipped:
    """PAGE FLIPPING (98.3.8), for Mode X (EncoderX) and VGA4 on a screen of
    its own (EncoderP, 98.2.5.1): the player brings the back page up to date
    before each frame - the last record decoded again, or its rows copied
    off the glass - so a frame costs that too, and it runs the flip
    schedule. Unflipped, it changes nothing an encode makes"""

    def flip_init(self, flip):
        # FLIPPING (98.3.8): the player decodes the last record again into
        # the back page before this one, so a frame costs both
        self.flip, self.prev_c = flip, 0
        if flip:                        # a frame a call (98.3.8): owed time
            self.owe = None             # is the flip schedule's (below)

    def own_init(self):
        """EACH RECORD AGAINST ITS OWN PAGE (--flip-own, VIDEO-SEEN-PLAN 3b):
        the encoder keeps BOTH pages, and a frame is drawn on the one the
        glass is not showing - the frame two back, or after a held record
        the held one - as it stands, owing nothing. The record after a key
        is the exception: a seek puts the key on both pages, so that record
        first takes the rows where the pages differ off the glass"""
        self.own, self.opage = True, None
        self.own_hold = self.own_next = False
        self.own_up = None

    def _own_page(self):
        return (self.screen, self.surf, getattr(self, "pl", None))

    def _own_set(self, pg):
        self.screen, self.surf = pg[0], pg[1]
        if pg[2] is not None:
            self.pl = pg[2]
        self.scache = None

    def begin(self):
        if getattr(self, "own", False):
            self.own_turn()
        super().begin()

    def own_turn(self):
        """A frame starts: onto the page the glass is not showing (the same
        one after a held record), brought up first after a key"""
        shown = self._own_page()
        if self.opage is None:          # (both pages black at the start)
            self.opage = (shown[0].copy(), bytearray(shown[1]),
                          None if shown[2] is None else shown[2].copy())
        if self.own_hold:
            self.own_hold = False       # (drawn on again, as it stands)
            self.own_up, self.prev_c = None, 0.0
            return
        self._own_set(self.opage)
        self.opage = shown
        self.own_up, self.prev_c = None, 0.0
        if self.own_next:
            self.own_next = False
            rows = np.flatnonzero((self.screen != shown[0]).any(axis=1))
            if len(rows):
                y0, y1 = int(rows[0]), int(rows[-1]) + 1
                self.own_up = (y0, y1)
                self._own_set((shown[0].copy(), bytearray(shown[1]),
                               None if shown[2] is None else shown[2].copy()))
                # (the player's copy off the glass, 98.3.8.1's price)
                self.prev_c = (y1 - y0) * 80 * self.lcopy + CYC_LCOPY0

    def charge(self, rec, abytes):
        c = self.cost(rec)
        pc, pb = self.pa_spend          # (page-ahead's hidden page, 98.2.1.8)
        self.pa_spend = (0.0, 0.0)
        self.lean(len(rec) - abytes)
        self.cpu.spend(c + self.prev_c + pc)
        self.disk.spend(len(rec) - abytes + pb)
        spent = c + self.prev_c + pc
        if getattr(self, "nobring", False):
            # (FLIPPED AND BUILT AHEAD AGAINST ITS OWN PAGES: every record
            # drawn as it stands - PageAheadF's own - so nothing is owed;
            # a flip's copy is that frame's, set in its before())
            self.prev_c, self.staying = 0.0, False
            self.end(spent)
            return spent
        if getattr(self, "own", False):
            # (against its own page: nothing owed the next frame, and a
            # HELD record's page is the next one's - own_turn)
            self.own_hold = bool(getattr(self, "staying", False))
            self.staying = False
            self.end(spent)
            return spent
        if self.flip:
            # what the player brings the back page up with next frame: this
            # record again, or its rows copied off the glass through the
            # latches - the cheaper by vp_flipdec's own rule, and always the
            # copy for a record too long to keep (98.3.8.1)
            y0, y1 = struct.unpack_from("<HH", rec, 2)
            rows = max(0, y1 - y0)
            # (flipped and built ahead, the player keeps the record's own
            # lists and no more - its chain and its audio are not decoded
            # again, 98.3.8.5)
            n = len(rec) - (abytes if getattr(self, "fown", False) else 0)
            sb = getattr(self, "sband", None)
            if getattr(self, "staying", False):
                # A HELD RECORD (98.1.3.9): the next is drawn on this page
                # too, owing it nothing - and the one after that brings its
                # page up by the copy of both records' rows, as the player
                # does (vp_flipdec: the band's union, the copy forced)
                self.sband = (y0, y1) if sb is None else \
                    (min(sb[0], y0), max(sb[1], y1))
                self.prev_c, self.staying = 0.0, False
            elif sb is not None:
                self.sband = None
                rows = max(0, max(sb[1], y1) - min(sb[0], y0))
                self.prev_c = rows * 80 * self.lcopy + CYC_LCOPY0
            elif n > vid.PREV_MAX or rows * 80 * VP_LCW < n:
                self.prev_c = rows * 80 * self.lcopy + CYC_LCOPY0
            else:
                self.prev_c = getattr(self, "cost0", self.cost)(rec)
        self.end(spent)
        return spent


class EncoderX(Flipped, Encoder):
    """Encoder for MODEX (SPEC.md 98.1.3.1): the screen is PIXELS, and a
    frame's candidate writes are the five sub-records' spans - a byte under
    Map Mask 0Fh is four pixels of one colour, a plane's byte one pixel.
    The budgets, the ranking by error and age, and the measured retry are
    Encoder's"""

    def __init__(self, g, prof, fps, audio_cyc, audio_bps, palette,
                 flip=False):
        super().__init__(g, prof, fps, audio_cyc, audio_bps, palette)
        self.flip_init(flip)
        self.screen = np.zeros((g.h, g.w), dtype=np.uint8)
        self.age = np.zeros((g.h, g.w), dtype=np.float32)
        self.surf = g.surface()
        self.base = np.array(g.base, dtype=np.int64)
        self.rowof = np.array(g.rowof, dtype=np.int64)

    def frame(self, target, audio=b""):
        g = self.g
        self.begin()
        self.stats["frames"] += 1
        diff = target != self.screen
        self.age = np.where(diff, self.age + 1, 0)
        if not diff.any():
            self.stats["exact"] += 1
            return [], vid.record([], g, audio)
        subs = vid.modex_subs(target.tobytes(), diff.ravel().tolist(), g)
        cand = [(m, a, bs, run) for m, sp in subs for a, bs, run in sp]
        costs = span_costs(((bs, run) for m, a, bs, run in cand),
                           table=self.ct)
        tc, tb = totals(costs)
        cyc_room = min(self.cpu.room(), self.ceil) - self.prev_c
        byte_room = max(0.0, self.disk.room() - self.disk_floor())
        order = None
        er = cyc_room - self.ct[0] - 7 * self.csub
        eb = min(byte_room, self.rec_max - len(audio)) - REC_OVER - \
            self.subover(7)
        best = None
        blk, cblk = None, []
        for attempt in range(8):
            cblk = []
            if tc <= er and tb <= eb:
                chosen = cand
            else:
                if order is None:
                    self.capb = self.rec_max - len(audio) < byte_room
                    self.why(tc, tb, er, eb, cyc_room)
                    cand = split_big(cand)
                    costs = span_costs(
                        ((bs, run) for m, a, bs, run in cand), table=self.ct)
                    tc, tb = totals(costs)
                    order = self.rank(target, cand, costs, er, eb)
                    if self.blocks:     # VERSION 2's blocks (98.1.10)
                        blk = self.block_cands8(target, er, eb,
                                                wpx=self._pxw, mx=True)
                        if blk is not None:
                            order, sblk = self.block_mergex(order, cand,
                                                            blk)
                chosen, uc, ub = [], 0, 0
                taken, dead = set(), set()
                for i in order:
                    if i < 0:           # a BLOCK, and the spans under it
                        j = ~i          # exclude each other
                        bid = blk[6][j]
                        if bid in taken or bid in dead:
                            continue
                        c, b = blk[3][j], blk[4][j]
                        if uc + c > er or ub + b > eb:
                            continue
                        taken.add(bid)
                        cblk.append(j)
                        uc += c
                        ub += b
                        continue
                    if blk is not None and sblk.get(i, set()) & taken:
                        continue
                    c, b = costs[i]
                    if uc + c > er or ub + b > eb:
                        if self.cut == "tear":
                            break       # (one tear line: 98.2.1.2.1)
                        continue
                    chosen.append(cand[i])
                    if blk is not None:
                        dead |= sblk.get(i, set())
                    uc += c
                    ub += b
            ops = self.banded(chosen, (0x0F, 0x03, 0x0C, 1, 2, 4, 8)) + \
                self.block_opsx(blk, cblk)
            rec = vid.record(ops, g, audio, limit=None)
            # (self.cost and not the table: the price charge() will take,
            # the play's calibration and the XMS copies with it - asked of
            # the table, Mode X chose to a model the charge then disagreed
            # with, and the schedule paid the difference)
            mc = self.cost(rec)
            if mc <= cyc_room and len(rec) - len(audio) <= byte_room and \
                    len(rec) <= self.rec_max:
                f = self.grow(chosen is cand, rec, audio, byte_room, mc,
                              cyc_room)
                if f is None:
                    break
                best = (chosen, ops, rec, cblk)     # (--cut fill/tear: room
                er, eb = er * f, eb * f             # left)
                continue
            if best is not None:
                chosen, ops, rec, cblk = best
                break
            # (never below 0: an overdrawn room made the ratio negative,
            # and a negative estimate times it came back POSITIVE - a frame
            # then spent what it did not have; VIDEO-OVERAGE-PLAN 4.2)
            er *= max(0.0, min(0.97, cyc_room / mc))
            eb *= min(0.97, min(byte_room, self.rec_max - len(audio)) /
                      max(1, len(rec) - len(audio)))
        else:
            if best is not None:
                chosen, ops, rec, cblk = best
        if len(rec) > self.rec_max:     # NEVER A RECORD THE FILE CANNOT
            chosen, ops, cblk = [], [], []  # HOLD (unfit): the frame draws
            rec = vid.record([], g, audio)  # nothing, the next catch up
            self.stats["unfit"] += 1
        if chosen is cand:
            self.stats["exact"] += 1
        else:
            self.stats["cut"] += 1
            self.torn(chosen, 1)
        if chosen:                      # (every span at once - the speed
            lens = np.fromiter((len(c[2]) for c in chosen), np.int64,
                               len(chosen))    # pass: the loop a span was
            st = np.fromiter((c[1] for c in chosen), np.int64,  # a third
                             len(chosen))       # of a frame's time. Two
            ms = np.fromiter((c[0] for c in chosen), np.int64,  # spans on
                             len(chosen))       # one pixel carry the same
            k = np.cumsum(lens) - lens          # target value, so the
            ad = np.repeat(st - k, lens) + np.arange(int(lens.sum()))
            ys = self.rowof[ad]                 # order is no matter)
            xs = (ad - self.base[ys]) * 4       # (a span may run on to the
            v = np.frombuffer(b"".join(c[2] for c in chosen), np.uint8)
            mm = np.repeat(ms, lens)            # next row: at full width a
            for p in range(4):                  # plane's rows are back to
                sel = (mm >> p & 1).astype(bool)    # back)
                self.screen[ys[sel], xs[sel] + p] = v[sel]
        for j in cblk:                  # VERSION 2's blocks (98.1.10)
            self.mx_block(blk[1][j], blk[2][j], blk[0][j])
            self.stats["blocks"] = self.stats.get("blocks", 0) + 1
        if chosen is not cand:
            self.stats["bytes_left"] += int((target != self.screen).sum())
        g.put(self.surf, self.screen.tobytes())
        return ops, rec

    def mx_block(self, a, pay, kind):
        """A Mode X block on self.screen: its pixel x of a row is plane x"""
        y0 = int(self.rowof[a])
        x = (a - int(self.base[y0])) * 4
        px = vid.v8_block(kind, pay)
        for r in range(4):
            self.screen[y0 + r, x:x + 4] = px[r * 4:r * 4 + 4]

    def block_opsx(self, blk, cblk):
        if not cblk:
            return []
        out = []
        for lead, kind in ((vid.BLK_FILL8, "mfill"), (vid.BLK_TWO8, "mtwo"),
                           (vid.BLK_EIGHT8, "meight")):
            ents = sorted((blk[1][j], blk[2][j], False) for j in cblk
                          if blk[0][j] == kind)
            if ents:
                out.append((lead, ents))
        return out

    def rank(self, target, cand, costs, er, eb):
        g = self.g
        v = self.value(target) if self.look or self.vis else None
        err = (np.abs(self.pal[target] - self.pal[self.screen]).sum(2)
               / 32.0 if v is None else v) * (1.0 + self.age / 8.0)
        cs = []
        for p in range(4):
            wv = np.zeros(65537, dtype=np.float64)
            wv[(self.base[:, None] + np.arange(g.wb)).ravel()] = \
                err[:, p::4].ravel()
            cs.append(np.concatenate(([0.0], np.cumsum(wv))))
        a, n, c, b = span_arrays(cand, costs, 1)
        m = np.fromiter((x[0] for x in cand), np.int64, len(cand))
        wsum = np.zeros(len(cand))      # the planes' in order, as sum() did
        for p in range(4):
            wsum = np.where(m >> p & 1, wsum + (cs[p][a + n] - cs[p][a]),
                            wsum)
        self._pxw = err                 # (the blocks weigh with it, and
        self._rankp = wsum / per_budget(c, b, er, eb)   # merge by this)
        return self.order(self._rankp, a)

    def block_mergex(self, order, cand, blk):
        """rank()'s order with Mode X's blocks merged in by priority (a
        block as ~j) and each candidate's blocks under it: a block is one
        plane byte on each of its four rows, every plane"""
        g = self.g
        kinds, ads, pays, cs, bs, pr, ids, top = blk
        offs = [g.base[r] - g.base[0] for r in range(4)]
        at = np.full(65536 + 8, -1, np.int64)
        for i, a in enumerate(top.tolist()):
            for o in offs:
                at[a + o] = i
        sblk = {}
        for i, (m, a, bs_, run) in enumerate(cand):
            v = at[a:a + len(bs_)]
            v = v[v >= 0]
            if len(v):
                sblk[i] = set(v.tolist())
        out = [(float(self._rankp[i]), i) for i in order] + \
            [(float(pr[j]), ~j) for j in range(len(kinds))]
        out.sort(key=lambda x: -x[0])
        return [i for p, i in out], sblk

    def put_ops(self, ops):
        g = self.g
        for m, sub in ops:              # (mask, spans): a span's bytes are
            if m >= vid.BLK_LEAD:       # a pixel each on the mask's planes
                for a, pay, run in sub:     # - or VERSION 2's blocks
                    self.mx_block(a, pay, vid.MODEX_LEADS[m])
                continue
            for a, bs, run in sub:
                ad = a + np.arange(len(bs))
                ys = self.rowof[ad]
                xs = (ad - self.base[ys]) * 4
                v = np.frombuffer(bs, np.uint8)
                for p in range(4):
                    if m >> p & 1:
                        self.screen[ys, xs + p] = v
        g.put(self.surf, self.screen.tobytes())

    def clone_screen(self, b):
        self.surf = bytearray(b.surf)
        self.screen = b.screen.copy()

    def cost(self, rec):
        """A record's cycles: its sub-records' (98.1.3.1)"""
        return vid.cycles_of(rec, True, table=self.ct, sub=self.csub,
                             sub2=self.g.sub2, wm3=self.cwm3,
                             bitplanes=False)


class EncoderP(Flipped, Encoder):
    """Encoder for VGA4 on LIN80's bit-planes (SPEC.md 98.1.3.2): the
    screen is pixels of the sixteen, a byte is one plane's bit of eight of
    them, and at each byte the planes that want one value are one store
    under their combined mask - vid.vga4_subs's rule, done with numpy"""

    def __init__(self, g, prof, fps, audio_cyc, audio_bps, palette=None,
                 flip=False):
        super().__init__(g, prof, fps, audio_cyc, audio_bps,
                         palette or vid.STD16)
        self.flip_init(flip)
        self.wm3 = False                # (encode() turns it on: 98.1.3.2.4)
        self.all3 = False               # every changed byte in write mode 3:
                                        # a coarse cut's frame (--coarse-cut)
        self.far = None                 # ...and its palette's far pairs
        self.screen = np.zeros((g.h, g.w), dtype=np.uint8)
        self.age = np.zeros((g.h, g.w), dtype=np.float32)
        self.pl = np.zeros((4, g.h, g.wb), dtype=np.uint8)
        self.surf = g.surface()
        self.base = np.array(g.base, dtype=np.int64)
        self.rowof = np.array(g.rowof, dtype=np.int64)

    def put_ops(self, ops):
        g = self.g
        for m, sub in ops:
            for a, bs, run in sub:
                self.store(m, a, bs)
        self.screen = sum(np.unpackbits(self.pl[p], axis=1)[:, :g.w]
                          .astype(np.uint8) << p for p in range(4))
        for p in range(4):
            for y, b in enumerate(g.base):
                self.surf[p * vid.PLANE + b:p * vid.PLANE + b + g.wb] = \
                    self.pl[p, y].tobytes()

    def clone_screen(self, b):
        self.surf = bytearray(b.surf)
        self.pl = b.pl.copy()
        self.screen = b.screen.copy()

    def store(self, m, a, bs):
        """One write onto self.pl: a plane group's bytes under Map Mask
        `m`, or (m >= 10h) write mode 3's masks in colour m - 10h"""
        if m >= vid.BLK_LEAD:           # VERSION 2's block (98.1.10)
            y0 = self.rowof[a]
            x = a - self.base[y0]
            for r, pv in enumerate(vid.vga4_block(m, bs)):
                for p in range(4):
                    self.pl[p, y0 + r, x] = pv[p]
            return
        ad = a + np.arange(len(bs))
        ys = self.rowof[ad]
        xs = ad - self.base[ys]
        v = np.frombuffer(bs, np.uint8)
        if m >= vid.SUB2_WM3:
            c = m - vid.SUB2_WM3
            for p in range(4):
                old = self.pl[p, ys, xs]
                self.pl[p, ys, xs] = (old & ~v) | (v if c >> p & 1 else 0)
            return
        for p in range(4):
            if m >> p & 1:
                self.pl[p, ys, xs] = v

    def wm3_bytes(self, tp, ch, gm):
        """The bytes to write in WRITE MODE 3 (98.1.3.2.4): more than one
        plane group, and some state the plane order passes through - each
        group after another, lowest mask first, as by_mask() orders them -
        shows a changed pixel in a colour more than WM3_DE from both its
        old and its new one. `gm[p]` is plane p's group mask where it
        changed, else 0"""
        if self.far is None:
            import os88cgacomp as cc
            lab = cc.srgb2lab(np.asarray(self.pal[:16], np.float64))
            self.far = np.sqrt(((lab[:, None] - lab[None]) ** 2).sum(-1)) \
                > WM3_DE
        unp = lambda b: np.unpackbits(b, axis=1)
        old = sum(unp(self.pl[p]).astype(np.uint8) << p for p in range(4))
        new = sum(unp(tp[p]).astype(np.uint8) << p for p in range(4))
        chg = old != new
        ng = np.zeros(gm.shape[1:], np.int8)
        for v in range(1, 15):
            ng += (gm == v).any(0)
        bad = np.zeros(gm.shape[1:], bool)
        if not (ng > 1).any():
            return bad
        U = np.zeros(gm.shape[1:], np.uint8)
        cnt = np.zeros(gm.shape[1:], np.int8)
        for v in range(1, 15):
            pres = (gm == v).any(0)
            if not pres.any():
                continue
            U = np.where(pres, U | v, U)
            cnt = cnt + pres
            mid = pres & (cnt < ng)
            if not mid.any():
                continue
            st = sum(unp(np.where(U >> p & 1, tp[p], self.pl[p]))
                     .astype(np.uint8) << p for p in range(4))
            px = self.far[st, old] & self.far[st, new] & chg
            hit = px.reshape(px.shape[0], -1, 8).any(2)[:, :gm.shape[2]]
            bad |= mid & hit
        return bad

    def planes(self, cv):
        return np.stack([np.packbits((cv >> p) & 1, axis=1)
                         for p in range(4)])

    def subs(self, target):
        g = self.g
        tp = self.planes(target)
        ch = tp != self.pl
        m3 = None
        if self.wm3:
            gm = np.zeros((4,) + ch.shape[1:], np.uint8)
            for p in range(4):
                m = np.zeros(ch.shape[1:], np.uint8)
                for q in range(4):
                    m |= (tp[q] == tp[p]).astype(np.uint8) << q
                gm[p] = np.where(ch[p], m, 0)
            m3 = ch.any(0) if self.all3 else self.wm3_bytes(tp, ch, gm)
            if m3.any():
                ch = ch & ~m3[None]
            else:
                m3 = None
        out = {}
        for p in range(4):
            rep = ch[p].copy()
            for q in range(p):          # one store per group: its lowest
                rep &= ~(ch[q] & (tp[q] == tp[p]))     # changed plane
            if not rep.any():
                continue
            m = np.zeros(rep.shape, np.int64)
            for q in range(4):
                m |= (tp[q] == tp[p]).astype(np.int64) << q
            ys, xs = np.nonzero(rep)
            for mm in np.unique(m[ys, xs]):
                sel = m[ys, xs] == mm
                ad = (self.base[ys[sel]] + xs[sel]).tolist()
                out.setdefault(int(mm), []).append(
                    (ad, tp[p][ys[sel], xs[sel]]))
        res = []
        for mm in sorted(out, key=lambda v: (v != 15, v)):
            surf = bytearray(vid.PLANE)
            ads = []
            for ad, vals in out[mm]:
                for a, v in zip(ad, vals.tolist()):
                    surf[a] = v
                ads += ad
            res.append((mm, vid.spans(ads, surf, g, gaps=False)))
        if m3 is not None:              # ...and the write mode 3 bytes: a
            unp = lambda b: np.unpackbits(b, axis=1)    # colour's mask of
            new = sum(unp(tp[p]).astype(np.uint8) << p  # its changed pixels
                      for p in range(4))
            old = sum(unp(self.pl[p]).astype(np.uint8) << p
                      for p in range(4))
            chg = (new != old)
            for c in range(16):
                mk = np.packbits((new == c) & chg, axis=1)[:, :g.wb] & \
                    np.where(m3, 0xFF, 0).astype(np.uint8)
                ys, xs = np.nonzero(mk)
                if not len(ys):
                    continue
                surf = bytearray(vid.PLANE)
                ad = (self.base[ys] + xs).tolist()
                for a, v in zip(ad, mk[ys, xs].tolist()):
                    surf[a] = v
                res.append((vid.SUB2_WM3 + c,
                            vid.spans(ad, surf, g, gaps=False)))
        return res

    def frame(self, target, audio=b""):
        g = self.g
        self.begin()
        self.stats["frames"] += 1
        diff = target != self.screen
        self.age = np.where(diff, self.age + 1, 0)
        if not diff.any():
            self.stats["exact"] += 1
            return [], vid.record([], g, audio)
        cand = [(m, a, bs, run) for m, sp in self.subs(target)
                for a, bs, run in sp]
        costs = self.pcosts(cand)
        tc, tb = totals(costs)
        cyc_room = min(self.cpu.room(), self.ceil) - self.prev_c
        byte_room = max(0.0, self.disk.room() - self.disk_floor())
        order = None
        er = cyc_room - self.ct[0] - 15 * self.csub
        eb = min(byte_room, self.rec_max - len(audio)) - REC_OVER - \
            self.subover(15)
        masks = sorted({c[0] for c in cand}, key=lambda v: (v != 15, v))
        best = None
        blk, cblk = None, []
        for attempt in range(8):
            cblk = []
            if tc <= er and tb <= eb:
                chosen = cand
            else:
                if order is None:
                    self.capb = self.rec_max - len(audio) < byte_room
                    self.why(tc, tb, er, eb, cyc_room)
                    cand, unit = byte_units(split_big(cand))
                    costs = self.pcosts(cand)
                    tc, tb = totals(costs)
                    order = self.rank(target, cand, costs, er, eb)
                    members = {}
                    for i, u in enumerate(unit):
                        members.setdefault(u, []).append(i)
                    if self.blocks:
                        blk = self.block_cands(target, er, eb)
                    if blk is not None:
                        order, ublk = self.block_merge(order, cand, unit,
                                                       members, blk)
                chosen, uc, ub, done = [], 0, 0, set()
                taken, dead = set(), set()
                for i in order:         # A UNIT AT A TIME: a byte's planes
                    if i < 0:           # all, or none (byte_units) - or a
                        j = ~i          # BLOCK (98.1.10), which excludes
                        bid = blk[6][j]     # the units under it, and they it
                        if bid in taken or bid in dead:
                            continue
                        c, b = blk[3][j], blk[4][j]
                        if uc + c > er or ub + b > eb:
                            continue
                        taken.add(bid)
                        cblk.append(j)
                        uc += c
                        ub += b
                        continue
                    u = unit[i]
                    if u in done:
                        continue
                    if blk is not None and ublk.get(u, set()) & taken:
                        continue
                    c = sum(costs[j][0] for j in members[u])
                    b = sum(costs[j][1] for j in members[u])
                    if uc + c > er or ub + b > eb:
                        if self.cut == "tear":
                            break       # (one tear line: 98.2.1.2.1)
                        continue
                    done.add(u)
                    if blk is not None:
                        dead |= ublk.get(u, set())
                    chosen += [cand[j] for j in members[u]]
                    uc += c
                    ub += b
            ops = self.banded(chosen, masks) + self.block_ops(blk, cblk)
            rec = vid.record(ops, g, audio, limit=None)
            mc = self.cost(rec)
            if mc <= cyc_room and len(rec) - len(audio) <= byte_room and \
                    len(rec) <= self.rec_max:
                f = self.grow(chosen is cand, rec, audio, byte_room, mc,
                              cyc_room)
                if f is None:
                    break
                best = (chosen, ops, rec, cblk)     # (--cut fill/tear: room
                er, eb = er * f, eb * f             # left)
                continue
            if best is not None:
                chosen, ops, rec, cblk = best
                break
            # (never below 0: an overdrawn room made the ratio negative,
            # and a negative estimate times it came back POSITIVE - a frame
            # then spent what it did not have; VIDEO-OVERAGE-PLAN 4.2)
            er *= max(0.0, min(0.97, cyc_room / mc))
            eb *= min(0.97, min(byte_room, self.rec_max - len(audio)) /
                      max(1, len(rec) - len(audio)))
        else:
            if best is not None:
                chosen, ops, rec, cblk = best
        if len(rec) > self.rec_max:     # NEVER A RECORD THE FILE CANNOT
            chosen, ops, cblk = [], [], []  # HOLD (unfit): the frame draws
            rec = vid.record([], g, audio)  # nothing, the next catch up
            self.stats["unfit"] += 1
        self.stats["exact" if chosen is cand else "cut"] += 1
        if chosen is not cand:
            self.torn(chosen, 1)
        for m, a, bs, run in chosen:
            self.store(m, a, bs)
        for j in cblk:                  # (98.1.10: after every other kind)
            self.store(vid.BLK_PFILL if blk[0][j] == "pfill" else
                       vid.BLK_TWO, blk[1][j], blk[2][j])
            self.stats["blocks"] = self.stats.get("blocks", 0) + 1
        self.screen = sum(np.unpackbits(self.pl[p], axis=1)[:, :g.w]
                          .astype(np.uint8) << p for p in range(4))
        if chosen is not cand:
            self.stats["bytes_left"] += int((target != self.screen).sum())
        for p in range(4):
            for y, b in enumerate(g.base):
                self.surf[p * vid.PLANE + b:p * vid.PLANE + b + g.wb] = \
                    self.pl[p, y].tobytes()
        return ops, rec

    def block_merge(self, order, cand, unit, members, blk):
        """rank()'s order with the blocks merged in by priority - a block
        as ~j, j its index - and each unit's blocks under it"""
        g = self.g
        kinds, ads, pays, cs, bs, pr, ids, top = blk
        offs = [g.base[r] - g.base[0] for r in range(4)]
        at = np.full(65536 * 1, -1, np.int64)
        for i, a in enumerate(top.tolist()):
            for o in offs:
                at[a + o] = i
        ublk = {}
        for u, js in members.items():
            hit = set()
            for j in js:
                a, n = cand[j][1], len(cand[j][2])
                v = at[a:a + n]
                hit.update(v[v >= 0].tolist())
            if hit:
                ublk[u] = hit
        sp = self._rankp
        out = [(float(sp[i]), 0, i) for i in order] + \
            [(float(pr[j]), 1, ~j) for j in range(len(kinds))]
        out.sort(key=lambda x: -x[0])   # (stable: today's order kept)
        return [i for p, k, i in out], ublk

    def block_ops(self, blk, cblk):
        """The chosen blocks as sub-records (98.1.10), after every other"""
        if not cblk:
            return []
        out = []
        for lead, kind in ((vid.BLK_PFILL, "pfill"), (vid.BLK_TWO, "two")):
            ents = sorted((blk[1][j], blk[2][j], False) for j in cblk
                          if blk[0][j] == kind)
            if ents:
                out.append((lead, ents))
        return out

    def pcosts(self, cand):
        """(cycles, bytes) of a frame's candidate writes: span_costs', and a
        write mode 3 byte's extra each (98.1.3.2.4) - but a RUN of FFh,
        which is a rep stosb"""
        costs = span_costs(((bs, run) for m, a, bs, run in cand),
                           table=self.ct)
        if not self.wm3:
            return costs
        out = []
        for (m, a, bs, run), (c, b) in zip(cand, costs):
            if m >= vid.SUB2_WM3 and not (
                    (run or (len(bs) >= 7 and bs.count(bs[:1]) == len(bs)))
                    and bs[0] == 0xFF):
                c += self.cwm3 * len(bs)
            out.append((c, b))
        return out

    def errmap(self, target):
        """rank()'s weight of each pixel: what writing its target now is
        worth, aged - kept for the frame, which the blocks (98.1.10) weigh
        with too"""
        key = self.stats["frames"]
        if getattr(self, "_errk", None) != key:
            v = self.value(target) if self.look or self.vis else None
            self._errm = (np.abs(self.pal[target] - self.pal[self.screen])
                          .sum(2) / 32.0 if v is None else v) * \
                (1.0 + self.age / 8.0)
            self._errk = key
        return self._errm

    def rank(self, target, cand, costs, er, eb):
        g = self.g
        err = self.errmap(target)
        byte = err.reshape(g.h, g.wb, 8).sum(2)
        wv = np.zeros(65537, dtype=np.float64)
        wv[(self.base[:, None] + np.arange(g.wb)).ravel()] = byte.ravel()
        cs = np.concatenate(([0.0], np.cumsum(wv)))
        a, n, c, b = span_arrays(cand, costs, 1)
        pc = np.fromiter((4 if x[0] >= vid.SUB2_WM3 else
                          bin(x[0]).count("1") for x in cand), np.float64,
                         len(cand))
        wsum = (cs[a + n] - cs[a]) * pc / 4.0
        self._rankp = wsum / per_budget(c, b, er, eb)  # (the blocks merge)
        return self.order(self._rankp, a)

    # VERSION 2's BLOCKS (98.1.10): 8 x 4 pixels, one byte column of four
    # rows on all four planes, as a DITHERED FILL (two of the sixteen and a
    # mix through the 4 x 4 Bayer cell, 2 bytes) or as TWO colours and a
    # mask a row (5 bytes). A block is worth the share of its pixels' error
    # it takes away AS SEEN - its 2 x 2 cells against the target's - so a
    # pattern unlike the target's but of the same colours is worth nearly
    # all of it, and what it leaves wrong ages and is fixed later, as a cut
    # frame's remainder is
    def mixes(self):
        """(a, b, m) whose PFILL mix is nearest an RGB, by a 32^3 table, for
        the palette in force"""
        key = self.pal[:16].tobytes()
        if getattr(self, "_mixk", None) != key:
            pal = self.pal[:16].astype(np.float64)
            A, B = np.triu_indices(16)
            m = np.arange(17)
            mix = ((pal[A][:, None] * m[None, :, None] +
                    pal[B][:, None] * (16 - m)[None, :, None]) / 16) \
                .reshape(-1, 3)
            cand = np.stack([np.repeat(A, 17), np.repeat(B, 17),
                             np.tile(m, len(A))], 1)
            q = (np.arange(32) * 8 + 4).astype(np.float64)
            grid = np.stack(np.meshgrid(q, q, q, indexing="ij"), -1) \
                .reshape(-1, 3)
            t = np.empty(len(grid), np.int64)
            for o in range(0, len(grid), 2048):
                t[o:o + 2048] = ((grid[o:o + 2048, None] - mix[None]) ** 2) \
                    .sum(-1).argmin(-1)
            self._mixt, self._mixc, self._mixk = t, cand, key
        return self._mixt, self._mixc

    def block_cands(self, target, er, eb):
        """The frame's block candidates: (kinds, addresses, payloads,
        cycles, bytes, priorities, block ids) - one of each kind for every
        block whose target differs from the screen, worth anything"""
        g = self.g
        H = g.h // 4 * 4
        none = None
        if not H:
            return none
        hb, wb = H // 4, g.wb
        T = target[:H, :wb * 8].reshape(hb, 4, wb, 8)
        S = self.screen[:H, :wb * 8].reshape(hb, 4, wb, 8)
        by, bx = np.nonzero((T != S).any(axis=(1, 3)))
        if not len(by):
            return none
        pal = self.pal[:16].astype(np.float64)
        Tb, Sb = T[by, :, bx, :], S[by, :, bx, :]
        tr = pal[Tb]

        def cells(x):                   # (n, 4, 8, 3) -> its 2 x 2 cells
            return x.reshape(-1, 2, 2, 4, 2, 3).mean(axis=(2, 4))
        tc = cells(tr)

        def miss(ix):
            return ((cells(pal[ix]) - tc) ** 2).sum(axis=(1, 2, 3))
        e0 = np.maximum(miss(Sb), 1e-6)
        W = self.errmap(target)[:H, :wb * 8].reshape(hb, 4, wb, 8)[
            by, :, bx, :].sum(axis=(1, 2))
        # the dithered fill: the mix nearest the block's mean colour
        t, cand = self.mixes()
        q = np.clip(tr.mean(axis=(1, 2)), 0, 255).astype(np.int64) >> 3
        abm = cand[t[(q[:, 0] << 10) | (q[:, 1] << 5) | q[:, 2]]]
        pm = np.array(vid.PFILL_MASKS, np.uint8)[abm[:, 2]]      # (n, 4)
        bits = np.unpackbits(pm[:, :, None], axis=2).astype(bool)
        pf = np.where(bits, abm[:, 0, None, None], abm[:, 1, None, None])
        # two colours: the pixels split about the mean luminance, each
        # side's mean colour to the nearest of the sixteen
        lum = tr @ np.array([0.299, 0.587, 0.114])
        hi = lum > lum.mean(axis=(1, 2))[:, None, None]
        n1 = hi.sum(axis=(1, 2))
        n0 = 32 - n1
        m1 = (tr * hi[..., None]).sum(axis=(1, 2)) / np.maximum(n1, 1)[:, None]
        m0 = (tr * ~hi[..., None]).sum(axis=(1, 2)) / np.maximum(n0, 1)[:, None]
        m1 = np.where((n1 == 0)[:, None], m0, m1)
        m0 = np.where((n0 == 0)[:, None], m1, m0)
        near = lambda c: ((c[:, None] - pal[None]) ** 2).sum(-1).argmin(-1)
        ca, cb = near(m1), near(m0)
        tw = np.where(hi, ca[:, None, None], cb[:, None, None])
        tmask = np.packbits(hi, axis=2)[:, :, 0]                   # (n, 4)
        addr = self.base[by * 4] + bx
        kinds, ads, pays, cs, bs, gs, ids = [], [], [], [], [], [], []
        for kind, ix in (("pfill", pf), ("two", tw)):
            gain = W * np.clip(1.0 - miss(ix) / e0, 0.0, 1.0)
            # its entry and its share of a segment's head or an absolute
            # entry's address, as the writer comes out: 0.6 byte and a
            # skip-segment set-up's quarter a block, measured on Gas Gas
            # Gas at 60 KB/s (3.87 bytes a block written against 3.0
            # priced, and a frame then cut below its room)
            c = vid.blk_cost(kind, self.ct, self.csub, self.cwm3) + 20 + \
                self.ct[1] / 4
            b = 1.6 + vid.BLK_KIND[kind][0]
            for i in np.nonzero(gain > 0)[0]:
                kinds.append(kind)
                ads.append(int(addr[i]))
                if kind == "pfill":
                    pays.append(bytes([int(abm[i, 0]) << 4 | int(abm[i, 1]),
                                       int(abm[i, 2])]))
                else:
                    pays.append(bytes([int(ca[i]) << 4 | int(cb[i])]) +
                                tmask[i].tobytes())
                cs.append(c)
                bs.append(b)
                gs.append(gain[i])
                ids.append(int(i))
        if not kinds:
            return none
        cs, bs, gs = (np.array(x, np.float64) for x in (cs, bs, gs))
        return (kinds, ads, pays, cs, bs, gs / per_budget(cs, bs, er, eb),
                np.array(ids), addr)


# --------------------------------------------------------------------------
# the source
# --------------------------------------------------------------------------
AHEAD = 256 << 20       # the most frames read ahead while a pass runs


# THE VIEW AS SEEN (docs/plans/VIDEO-SEEN-PLAN.md, 2026-10-16): the share of
# where an eye is LOOKING that is broken - not the mean over every pixel,
# which the right ones dilute (GasGas's broken car was 0.3-2.6% "as seen",
# and the owner's eye put the same frames at 17-62%). Per 16-pixel patch:
# soft brightness error, colour error and edges added or lost, masked where
# the target's own texture hides them; a patch broken to 1 at SEEN_THR,
# and a broken patch counting SEEN_SPREAD of itself on its neighbours (a
# fault on an object reads as the object broken); weighted by attention -
# brightness, and motion against the camera's (the shot's shake taken out by
# phase correlation). TWO SETS OF WEIGHTS, FITTED to the owner's scores:
# COLOUR on GasGas 0:40.6-0:41.0 at 24 fps, frames 15-23 (rms 2.7 points
# over nine frames rated 17-62%, the clean frames 0; the one miss frame 18,
# 25 against 31.5); GREY - a target whose every pixel is R = G = B - on Bad
# Carrot 3:05-3:20, eight cut frames rated 5-90% (rms 7.8). One set for
# both missed by 11 points, the car frames at half the owner's: "black and
# white, so the blur ends up counting for more here, and edges matter
# more" - and colour nothing, and a broken patch spilling onto none. A
# THIRD, DITHER - a 1-bit target a quarter or more of whose pixels average
# to a mid grey (SEEN_DITHER_MIX; Bad Carrot on a Hercules is 4% at the
# median, solid black and white, and keeps GREY) - on Take On Me, an old
# 1-bit Hercules file with many cuts, nine frames rated 0-80% (rms 3.4):
# brightness averaged over a box big enough to see past the dither, the
# edges a cut ADDS counted and the ones it LOSES not ("the face shape
# survives, just blurred"). A scene change left showing the old scene
# whole reads LOW (that clip's frame G, 21 against the owner's 70 for the
# frame and "the video is not broken": it plays as a fade), and was left
# out of the fit. A report line only: it steers nothing yet
SEEN_P = 16                 # the patch, canvas pixels
#            brightness, colour, edges added, lost; broken at; spread;
#            curve; the brightness box and the edges' (radius, pixels)
SEEN_COLOUR = (0.75, 0.25, 1.0, 1.0, 0.04, 0.75, 1.0, 2, 1)
SEEN_GREY = (1.0, 0.0, 2.0, 2.0, 0.12, 0.0, 1.5, 2, 1)
SEEN_DITHER = (1.0, 0.0, 1.0, 0.0, 0.12, 0.75, 3.0, 6, 1)
SEEN_DITHER_MIX = 0.25      # ...a 1-bit target this much mid grey
SEEN_BAD = 0.10             # a frame a tenth of whose view is broken


def _seen_box(a, r):
    """A box mean (2r+1) square, the edges repeated"""
    p = np.pad(a, r, mode="edge")
    c = np.pad(p.cumsum(0).cumsum(1), ((1, 0), (1, 0)))
    n = 2 * r + 1
    return (c[n:, n:] - c[:-n, n:] - c[n:, :-n] + c[:-n, :-n]) / (n * n)


def _seen_grad(l):
    gx = np.zeros_like(l)
    gy = np.zeros_like(l)
    gx[:, 1:-1] = l[:, 2:] - l[:, :-2]
    gy[1:-1, :] = l[2:, :] - l[:-2, :]
    return np.hypot(gx, gy) / 2


def _seen_shift(a, b):
    """The (dy, dx) b is a moved by: the camera's, by phase correlation"""
    fa = np.fft.rfft2(a - a.mean())
    fb = np.fft.rfft2(b - b.mean())
    r = fb * np.conj(fa)
    r /= np.maximum(np.abs(r), 1e-9)
    c = np.fft.irfft2(r, a.shape)
    y, x = np.unravel_index(np.argmax(c), c.shape)
    return (int(y) - a.shape[0] if y > a.shape[0] // 2 else int(y),
            int(x) - a.shape[1] if x > a.shape[1] // 2 else int(x))


# ...and only when numpy is there: the module is imported by tools and rows
# that never encode (tests/vidsound.py), and its import is optional above
_SEEN_LUM = np.array([0.299, 0.587, 0.114], np.float32) if np else None


def seen_attention(lT, lP=None):
    """The view as seen's ATTENTION: per pixel of a target's brightness
    `lT`, how much an eye goes there - the bright, and what moves against
    the camera (its shake from `lP`, the target before, taken out by phase
    correlation)"""
    box = _seen_box
    if lP is not None:
        dy, dx = _seen_shift(lP, lT)
        mot = box(np.abs(lT - np.roll(np.roll(lP, dy, 0), dx, 1)), 4)
    else:
        mot = 0.0
    return 0.35 + box(lT, 6) + 3.0 * mot


def seen_weights(T):
    """The view as seen's WEIGHTS for a target T, pixels 0..1: GREY where
    every pixel is R = G = B, DITHER where it is 1-bit and a quarter of it
    averages to a mid grey (SEEN_DITHER_MIX), else COLOUR"""
    grey = T.ndim == 2 or float(np.abs(T[..., 0] - T[..., 1]).max()) < 0.01 \
        and float(np.abs(T[..., 1] - T[..., 2]).max()) < 0.01
    w = SEEN_GREY if grey else SEEN_COLOUR
    if T.ndim == 2 and T.size and float(np.abs(T - 0.5).min()) >= 0.5:
        bT = _seen_box(np.asarray(T, np.float32), 2)    # 1-bit: dithered,
        if float(((bT > 0.1) & (bT < 0.9)).mean()) >= SEEN_DITHER_MIX:
            w = SEEN_DITHER                             # or solid?
    return w


def seen_share(S, T, Tp=None):
    """THE VIEW AS SEEN: S the screen and T its target as pixels (h, w, 3)
    colour or (h, w) grey, 0..1; Tp the target before (for motion) -> the
    share of the attention-weighted view that is broken, 0..1"""
    S = np.asarray(S, np.float32)
    T = np.asarray(T, np.float32)
    box = _seen_box
    wl, wc, wa, wo, thr, spread, curve, rl, re = seen_weights(T)
    lum = np.array([0.299, 0.587, 0.114], np.float32)
    if S.ndim == 3:
        lS, lT = S @ lum, T @ lum
        opp = lambda a: (a[..., 0] - a[..., 1],
                         0.5 * (a[..., 0] + a[..., 1]) - a[..., 2])
        (s1, s2), (t1, t2) = opp(S), opp(T)
        EC = np.hypot(box(s1, 1) - box(t1, 1), box(s2, 1) - box(t2, 1))
    else:
        lS, lT, EC = S, T, 0.0
    EL = np.abs(box(lS, rl) - box(lT, rl))
    gS, gT = _seen_grad(box(lS, re)), _seen_grad(box(lT, re))
    EB = wa * np.maximum(gS - gT, 0) + wo * np.maximum(gT - gS, 0)
    act = np.sqrt(np.maximum(box(lT * lT, 3) - box(lT, 3) ** 2, 0))
    D = (wl * EL + wc * EC + EB) / (1.0 + 4.0 * act)
    lP = None
    if Tp is not None and np.shape(Tp) == np.shape(T):
        Tp = np.asarray(Tp, np.float32)
        lP = Tp @ lum if Tp.ndim == 3 else Tp
    att = seen_attention(lT, lP)
    P = SEEN_P
    h, w = lT.shape
    hh, ww = max(P, h // P * P), max(P, w // P * P)
    if h < P or w < P:
        return float(np.minimum(1.0, D.mean() / thr) ** curve)
    pool = lambda a: a[:hh, :ww].reshape(hh // P, P, ww // P, P).mean((1, 3))
    b = np.minimum(1.0, pool(D) / thr) ** curve
    if spread:
        pb = np.pad(b, 1, mode="edge")
        nb = np.max(np.stack([pb[y:y + b.shape[0], x:x + b.shape[1]]
                              for y in range(3) for x in range(3)]), 0)
        b = np.maximum(b, spread * nb)
    pa = pool(np.broadcast_to(att, lT.shape))
    return float((b * pa).sum() / pa.sum())


VIS_BAD = 0.15          # VISIBLY WRONG: the picture as seen off by 15% of
VIS_AREA = 0.02         # full scale or more, over 2% or more of a frame:
                        # on the owner's Last Exile, its two torn scenes
                        # (0:13.3 for 2.1 s, 0:32.4) and no clean second


PA_HORIZON = 900        # page-ahead's disk forecast looks this many frames
                        # ahead (30 s at 30 fps), 98.2.1.8
PA_STATE = ("surf", "sv", "pl", "screen", "age")


class PageAhead(object):
    """PAGE-AHEAD (SPEC.md 98.2.1.8): the encode's second page. A plan pass
    has run the encode as it would be made without it and logged, frame by
    frame, which budget cut it, its record's bytes, its disk refill and its
    floor - and kept the TARGETS: each cut run's first frame, the frame a
    scene change cuts. This pass draws every frame in place as before and,
    after each one's own record, spends what that frame left on the hidden
    page, toward the next target; at the target, if the hidden page is
    nearer it than the shown one, the pages swap and the frame's record is
    drawn on the new one (the player draws it on the hidden page and then
    shows it). What the hidden page may spend:
    - CYCLES: the frame's own ceiling as the schedule set it (98.2.1.1), less
      its record, and never past the per-frame peak - owed time is the
      frame's - and nothing in a frame that was itself cut;
    - BYTES: what no later frame needs, forecast over the plan's frames from
      the level this frame leaves - a withdrawal X lowers every later level
      by X until an overflow absorbs it, so X is at most any frame's slack
      plus the overflow before it, and the first frame the plan cut for the
      disk bounds it - less, on a disk that slows under load, the refill the
      hidden page's own cycles cost the reader; and the super-packet's room
      beside the frame's record.
    Measured before it was built: docs/reports/VIDEO-PAGEAHEAD-2026-10-13.md"""

    def __init__(self, enc, plan, key0=0, kcap=KEY_PLAYER, kpart=vid.KEY_PART,
                 ref=0):
        import copy
        self.e = enc
        # THE PRELOAD (98.2.1.8): a play starts at a key, and a key's hidden
        # record may hold the next flip's target WHOLE - every hidden record
        # before that flip writes only the target's own values, so a page
        # that already holds it stays it. Key 0's is built here for nothing
        # (`key0`), and later keys are upgraded at the end (upgrade_keys)
        # where the flip they lead to came out exact
        self.key0, self.kcap = key0, kcap
        # KEYS IN PARTS (98.1.3.8): a key is as many reads as it takes, so
        # with `kpart` the preload and the upgrades are WHOLE, every read of
        # them one part; None keeps the old bound, one read for the lot
        self.kpart, self.ref = kpart, ref
        self.Fat, self.full = {}, {}
        self.pre_ops = None
        self.log = plan["log"]
        self.T, self.P = plan["T"], plan["P"]
        self.targets = sorted(self.T)
        h = copy.deepcopy(enc)          # (np views do not survive a deep
        if hasattr(h, "sv"):            # copy: preroll()'s note)
            h.sv = np.frombuffer(h.surf, dtype=np.uint8)
            h.tv = np.frombuffer(h.tsurf, dtype=np.uint8)
        h.look, h.future, h.metric = 0, [], False
        h.owe, h.dcurve, h.bank, h.alead, h.slead = None, None, 0, 0, 0
        h.dfloor, h.live = 0.0, False
        # VERSION 2 (98.1.10): the hidden page draws NO blocks - every hidden
        # record before a flip writes that flip's own values (98.2.1.8, the
        # preload and the upgrades rest on it), and one bit's codebook is the
        # shown page's encoder's alone
        h.blocks, h.cb_due = False, False
        if hasattr(h, "fgrid"):
            h.fgrid = None
        self.h = h
        self.F = self._next(-1)
        self.flipped = False
        self.flips, self.lost = 0, 0
        self.cyc = self.bytes = 0.0
        self.start = {}                 # target -> the first frame that
                                        # spent toward it (page_hurt)
        self.quiet = plan.get("quiet", ())  # stretches pruned (98.2.1.8.2):
                                            # nothing spent in them

    def _next(self, j):
        for F in self.targets:
            if F > j + 1:
                return F
        return None

    def before(self, j, target):
        """Frame j is about to be encoded: at a target, swap the pages if
        the hidden one is nearer it"""
        self.flipped = False
        if self.F is not None and j >= self.F:
            if j == self.F:
                e, h = self.e, self.h
                if np.count_nonzero(h.screen != target) < \
                        np.count_nonzero(e.screen != target):
                    for k in PA_STATE:
                        if hasattr(e, k) and hasattr(h, k):
                            x, y = getattr(e, k), getattr(h, k)
                            setattr(e, k, y)
                            setattr(h, k, x)
                    e.scache = h.scache = None
                    self.flipped = True
                    self.flips += 1
                else:
                    self.lost += 1
            self.F = self._next(j)
        self.Fat[j] = self.F

    def safe_bytes(self, j, spent):
        e, L = self.e, self.log
        curve = bool(e.dcurve)
        cap = e.disk.cap
        lvl = e.disk.level - spent
        X, O = math.inf, 0.0
        if curve:                       # (this frame's refill: at its end)
            r = L[j]["ref"]
            O += max(0.0, lvl + r - cap)
            lvl = min(cap, lvl + r)
        for k in range(j + 1, min(len(L), j + 1 + PA_HORIZON)):
            r = L[k]
            if not curve:
                O += max(0.0, lvl + r["ref"] - cap)
                lvl = min(cap, lvl + r["ref"])
            X = min(X, max(0.0, lvl - r["floor"] - r["b"]) + O)
            if r["why"] == "disk" or X <= 0:
                break
            lvl -= r["b"]
            if curve:
                O += max(0.0, lvl + r["ref"] - cap)
                lvl = min(cap, lvl + r["ref"])
        if X == math.inf:
            X = max(0.0, lvl - e.disk_floor())
        return max(0.0, X)

    def preload(self):
        """Key 0's hidden page: the first target, as much of it as the key's
        read leaves room for, at no cost to any frame"""
        e, h = self.e, self.h
        T = self.T[self.F]
        if self.kpart is not None:
            room = 1 << 30              # (in parts: the whole target)
        else:
            klen = len(vid.record(vid.keyframe_ops(e.surf, e.g), e.g,
                                  limit=None))
            room = self.kcap - klen - 4096
            if room < 1024:
                return
        p = self.P.get(self.F)
        if p is not None:
            h.set_pal(p)
        h.peak = h.ceil = math.inf
        h.cpu = Budget(None, None)
        h.disk = Budget(None, None)
        h.rec_max = room
        self.pre_ops = h.frame(T, b"")[0]   # (a layer's hidden page takes
        self.preloaded = int(np.count_nonzero(h.screen == T))  # them too)

    def upgrade_keys(self, wr):
        """...and the keys after key 0: each whose hidden page leads to a flip
        that came out exact holds that flip's picture whole, where it fits
        the player's one read. Returns how many"""
        g, n = self.e.g, 0
        for i, (k, rec, cv) in enumerate(wr.keys):
            F = self.Fat.get(k)
            if k == self.key0 or F not in self.full:
                continue
            vs = g.surface()
            g.put(vs, cv)
            new = vid.key_record(vid.keyframe_ops(vs, g), g,
                                 vid.delta_ops(self.full[F], vs, g), True,
                                 self.ref, self.kpart)
            if self.kpart is not None or len(new) <= self.kcap - 4096:
                wr.keys[i] = (k, new, cv)
                n += 1
        return n

    def after(self, j, rec, audio, target=None):
        """Frame j's own record is chosen: the hidden page's writes toward
        the next target with what it left, charged with it (Encoder.charge
        takes pa_spend) - or None"""
        e, h = self.e, self.h
        if self.flipped and target is not None and \
                np.array_equal(e.screen, target):
            self.full[j] = bytes(e.surf)    # (an exact flip: upgrade_keys)
        if j == self.key0 and self.F is not None and \
                not hasattr(self, "preloaded"):
            self.preload()
        if self.flipped or self.F is None or e.last_why is not None or \
                j >= len(self.log) or \
                any(qa <= j < qb for qa, qb in self.quiet):
            return None
        room = self._room(j, rec, audio)
        if room is None:
            return None
        cpu_left, dsk_left = room
        T = self.T[self.F]
        if not np.any(h.screen != T):
            return None
        p = self.P.get(self.F)
        if p is not None and (h.pal is None or not np.array_equal(h.pal, p)):
            h.set_pal(p)                # (the target's scene's colours)
        h.peak = h.ceil = cpu_left
        h.cpu = Budget(None, None)
        h.disk = Budget(None, None)
        h.rec_max = int(dsk_left)
        ops, hrec = h.frame(T, b"")
        if not ops:
            return None
        hc, hb = float(h.cost(hrec)), float(len(hrec))
        e.pa_spend = (hc, hb)
        self.start.setdefault(self.F, j)
        self.cyc += hc
        self.bytes += hb
        return ops

    def _room(self, j, rec, audio):
        """What frame j left the hidden pages, (cycles, bytes) - or None
        when it is too little to build with"""
        e = self.e
        c = e.cost(rec) + getattr(e, "prev_c", 0.0)  # (and what a flipped
        cpu_left = min(e.cpu.room(), e.ceil, e.peak) - c    # frame owes)
        dsk_left = math.inf
        if e.disk.per is not None:
            dsk_left = min(e.disk.room() - e.disk_floor(),
                           self.safe_bytes(j, len(rec) - len(audio))) - \
                (len(rec) - len(audio))
        if e.dcurve and e.disk.per is not None:
            # A DISK THAT SLOWS UNDER LOAD: the hidden page's cycles are the
            # reader's too, out of the same safe bytes - half at most
            rate = e._drate() if e.bank else e.drate
            base = (c + e.audio_cyc + HOOK_CYC) / e.q + e.spk

            def loss(cc):
                return max(0.0, (e.disk_rel(base) - e.disk_rel(
                    base + cc / e.q)) * rate * 0.99 / e.fps)
            lo, hi = 0.0, max(0.0, cpu_left)
            if loss(hi) > dsk_left / 2:
                for _ in range(20):
                    mid = (lo + hi) / 2
                    if loss(mid) > dsk_left / 2:
                        hi = mid
                    else:
                        lo = mid
                cpu_left = lo
            dsk_left -= loss(cpu_left)
        dsk_left = min(dsk_left, e.rec_max - len(rec))
        if cpu_left < 4 * e.ct[0] or dsk_left < 256:
            return None
        return cpu_left, dsk_left


class PageAheadN(PageAhead):
    """PAGE-AHEAD ON N PAGES (SPEC.md 98.1.3.7.1, 98.2.1.8.4): PageAhead with
    N-1 hidden pages, each built toward a target of its own - the nearest
    target first, out of the same cycles and bytes one page would have
    had (`_room`), so a page is built only from what the page before it
    did not need. The pages are PHYSICAL: `vp` is the page shown, `hp[i]`
    hidden page i's, and a flip swaps the two. Measured before it was
    built: docs/reports/VIDEO-PAGES-2026-10-15.md"""

    def __init__(self, enc, plan, pages, key0=0, kcap=KEY_PLAYER,
                 kpart=vid.KEY_PART, ref=0):
        import copy
        super().__init__(enc, plan, key0, kcap, kpart, ref)
        h0 = self.h
        self.hs = [h0]
        for _ in range(pages - 2):
            h = copy.deepcopy(h0)
            if hasattr(h, "sv"):
                h.sv = np.frombuffer(h.surf, dtype=np.uint8)
                h.tv = np.frombuffer(h.tsurf, dtype=np.uint8)
            self.hs.append(h)
        self.vp, self.hp = 0, list(range(1, pages))
        self.asg = [None] * (pages - 1)     # hidden page -> its target
        self.done = set()
        self.unbuilt = 0                    # targets no page was built for
        # THE KEY RULE (98.2.1.8.4): a key carries only the pages with a
        # target, and a page it leaves out is UNKNOWN from that key on -
        # straight play holds what it held, a play from the key the key's
        # picture - until a build has written every byte of it. `keyat` is
        # the writer's key frames (set by encode())
        self.unknown = [False] * (pages - 1)
        self.keyat = lambda k: False
        self.keyrule = False                # (--page-keys started)
        self.dropped = 0                    # pages keys left out
        self._assign(-1)

    def _assign(self, j):
        """Free pages take the next targets nobody is building, nearest
        first - none the frame after j, which a build could not reach"""
        held = set(x for x in self.asg if x is not None)
        for F in self.targets:
            if None not in self.asg:
                break
            if F > j + 1 and F not in held and F not in self.done:
                i = self.asg.index(None)
                self.asg[i] = F
                held.add(F)
                if self.unknown[i]:
                    self._poison(i, self.T[F])
        live = [x for x in self.asg if x is not None]
        self.F = min(live) if live else None

    def _poison(self, i, T):
        """Hidden page i UNKNOWN: its picture in the model is the target
        with every bit flipped, so every byte of it, in every plane, is a
        change - the build writes them all, and whatever either play holds
        there it holds the target after"""
        h, g = self.hs[i], self.e.g
        X = (T ^ (15 if getattr(g, "bitplanes", False) else 0xFF)).astype(
            np.uint8)
        g.put(h.surf, X.tobytes())
        if hasattr(h, "pl"):            # (VGA4: the planes from the surface)
            for p_ in range(4):
                for y, b in enumerate(g.base):
                    h.pl[p_, y] = np.frombuffer(
                        bytes(h.surf[p_ * vid.PLANE + b:
                                     p_ * vid.PLANE + b + g.wb]), np.uint8)
        h.screen = X.copy()
        h.scache = None

    def carried(self, j):
        """The pages frame j's key carries, by number - every page whose
        build has STARTED, the rest left out and UNKNOWN from here
        (98.2.1.8.4) - or, on a frame with no key, all of them. A page with
        a target nothing has been spent toward yet holds a picture nobody
        will see, and every target is handed to a page the moment one is
        free, so that is most pages at most keys: a page with a target far
        ahead was carried by every key before its build began"""
        if not self.keyat(j) or not self.keyrule:
            return self.surfs()
        out = {self.vp: self.e.surf}
        for i, h in enumerate(self.hs):
            F = self.asg[i]
            if (F is not None and F in self.start and not self.unknown[i]) \
                    or np.array_equal(h.screen, self.e.screen):
                out[self.hp[i]] = h.surf
            elif F is not None and F in self.start:
                out[self.hp[i]] = h.surf    # (unknown, part built: carried
                                            # as the model has it, and whole
                                            # before it is shown)
            else:
                if not self.unknown[i]:
                    self.unknown[i] = True
                    self.dropped += 1
                if F is not None:           # (its build starts from the
                    self._poison(i, self.T[F])  # target, every bit flipped)
        return out

    def before(self, j, target):
        self.flipped = False
        e = self.e
        if j in self.T and j not in self.done:
            self.done.add(j)
            if j in self.asg:
                i = self.asg.index(j)
                h = self.hs[i]
                if self.unknown[i] and np.any(h.screen != target):
                    self.lost += 1      # (UNKNOWN and not yet whole: the
                                        # two plays would show it apart)
                elif np.count_nonzero(h.screen != target) < \
                        np.count_nonzero(e.screen != target):
                    for k in PA_STATE:
                        if hasattr(e, k) and hasattr(h, k):
                            x, y = getattr(e, k), getattr(h, k)
                            setattr(e, k, y)
                            setattr(h, k, x)
                    e.scache = h.scache = None
                    self.vp, self.hp[i] = self.hp[i], self.vp
                    self.unknown[i] = False     # (whole, or never unknown;
                                                # the page left is known)
                    self.flipped = True
                    self.flips += 1
                else:
                    self.lost += 1
                self.asg[i] = None
            else:
                self.unbuilt += 1
        for i, F in enumerate(self.asg):    # (a target passed unflipped)
            if F is not None and F < j:
                self.asg[i] = None
        self._assign(j)
        self.Fat[j] = self.F

    def surfs(self):
        """every page by its number: what a key carries of each"""
        out = {self.vp: self.e.surf}
        for i, h in enumerate(self.hs):
            out[self.hp[i]] = h.surf
        return out

    def preload(self):
        """Key 0's hidden pages: EVERY page's target whole, at no cost to
        any frame (the key is read in parts, 98.1.3.8)"""
        self.preloaded = self.npre = 0
        for F, i in sorted((F, i) for i, F in enumerate(self.asg)
                           if isinstance(F, int)):
            h = self.hs[i]
            T = self.T[F]
            p = self.P.get(F)
            if p is not None:
                h.set_pal(p)
            h.peak = h.ceil = math.inf
            h.cpu = Budget(None, None)
            h.disk = Budget(None, None)
            h.rec_max = 1 << 30
            h.frame(T, b"")
            self.npre += 1
            if not self.preloaded:
                self.preloaded = int(np.count_nonzero(h.screen == T))

    def upgrade_keys(self, wr):
        return 0                # (an N-page key carries every page as it is)

    def after(self, j, rec, audio, target=None):
        """...the hidden pages' writes, nearest target first: a list of
        (page, ops), or None"""
        e = self.e
        if j == self.key0 and not hasattr(self, "preloaded") and \
                any(x is not None for x in self.asg):
            if self.kpart is None:
                self.preloaded = 0  # (one read of the key: no preload)
            else:
                self.preload()
        if self.flipped or all(x is None for x in self.asg) or \
                e.last_why is not None or j >= len(self.log) or \
                any(qa <= j < qb for qa, qb in self.quiet):
            return None
        room = self._room(j, rec, audio)
        if room is None:
            return None
        cpu_left, dsk_left = room
        out, tc, tb = [], 0.0, 0.0
        for F, i in sorted((F, i) for i, F in enumerate(self.asg)
                           if F is not None):
            if cpu_left < 4 * e.ct[0] or dsk_left < 256:
                break
            h = self.hs[i]
            T = self.T[F]
            if not np.any(h.screen != T):
                continue            # (built: the next page's turn)
            p = self.P.get(F)
            if p is not None and (h.pal is None or
                                  not np.array_equal(h.pal, p)):
                h.set_pal(p)
            h.peak = h.ceil = cpu_left
            h.cpu = Budget(None, None)
            h.disk = Budget(None, None)
            h.rec_max = int(dsk_left)
            ops, hrec = h.frame(T, b"")
            if not ops:
                break
            hc, hb = float(h.cost(hrec)), float(len(hrec))
            cpu_left -= hc
            dsk_left -= hb
            tc += hc
            tb += hb
            self.start.setdefault(F, j)
            out.append((self.hp[i], ops))
            if self.unknown[i] and not np.any(h.screen != T):
                self.unknown[i] = False     # (every byte written: known)
        if not out:
            return None
        e.pa_spend = (tc, tb)
        self.cyc += tc
        self.bytes += tb
        return out


class PageAheadF(PageAheadN):
    """FLIPPED AND BUILT AHEAD (SPEC.md 98.1.3.7.2, 98.2.1.8.5): every frame
    drawn on the BACK page `bp` - the player brings it up to the frame shown
    first, which the flipped encoder already charges (`prev_c`) - and the
    pages are swapped; N-2 BUILDERS are PageAheadN's hidden pages. A flip to
    a builder is BUILT, drawn on as it stands, with nothing brought up; the
    next frame's back page must then be that page as it was built, so either
    a builder the calm COPIED it onto (four pages or more: `cp`, in bands out
    of spare cycles once the build is whole) becomes the back page, or the
    flip's own record carries a COPY of the rows that differ onto the old
    one. The copy's cycles take the bring-up's place in that frame's budget,
    and a flip whose copy does not fit is not taken. Measured before it was
    built: docs/reports/VIDEO-PAGES-2026-10-15.md §5"""

    def __init__(self, enc, plan, pages, key0=0, kcap=KEY_PLAYER,
                 kpart=vid.KEY_PART, ref=0, own=False):
        super().__init__(enc, plan, pages - 1, key0, kcap, kpart, ref)
        enc.fown = True                 # (the player keeps the own lists)
        # AGAINST ITS OWN PAGES (--flip-own, VIDEO-SEEN-PLAN 3b): every
        # frame record BUILT - drawn on its page as it stands, that page
        # the encoder's model for it - so none is brought up, and every key
        # carries the back page too, which a seek then has exactly
        self.own = own
        self.N = pages
        self.bp = pages - 1                 # the back page, drawn next
        self.bsurf = bytes(enc.surf)        # ...and what it holds
        self.bunk = False                   # ...UNKNOWN: a key left it out
        self.cp = {}                        # builder -> the builder copied
        self.cleft = {}                     #   onto, and its rows still owed
        self.built, self.copies = False, []
        self.ncopy = self.crows = 0
        self.copyrefused = 0
        self.precopied = 0
        self.bpend = None

    def _rows(self, a, b):
        """the canvas rows that differ between surfaces a and b, as a band
        (y0, y1), or None"""
        g = self.e.g
        ca, cb = g.canvas(a), g.canvas(b)
        rb = len(ca) // g.h
        rows = [y for y in range(g.h) if ca[y * rb:(y + 1) * rb] !=
                cb[y * rb:(y + 1) * rb]]
        return (rows[0], rows[-1] + 1) if rows else None

    def _ccost(self, band):
        e = self.e
        return 0.0 if band is None else \
            (band[1] - band[0]) * 80 * e.lcopy + CYC_LCOPY0

    def _setsurf(self, h, surf):
        """builder object `h` made to model the page holding `surf`"""
        g = self.e.g
        X = np.frombuffer(bytes(g.canvas(surf)), np.uint8).reshape(
            h.screen.shape)
        g.put(h.surf, X.tobytes())
        if hasattr(h, "pl"):
            for p_ in range(4):
                for y, b in enumerate(g.base):
                    h.pl[p_, y] = np.frombuffer(
                        bytes(h.surf[p_ * vid.PLANE + b:
                                     p_ * vid.PLANE + b + g.wb]), np.uint8)
        h.screen = X.copy()
        h.scache = None

    def carried(self, j):
        """every page but the back page, which after a seek holds the key's
        own picture - and is UNKNOWN from a key until it is drawn on; and
        against its own pages, the back page too"""
        out = self.surfs()
        if self.own:
            out[self.bp] = self.bsurf
        elif self.keyat(j):
            self.bunk = True
        return out

    def surfs(self):
        out = {self.vp: self.e.surf}
        for i, h in enumerate(self.hs):
            out[self.hp[i]] = h.surf
        return out

    def before(self, j, target):
        e = self.e
        self.flipped = self.built = False
        self.copies = []
        old = bytes(e.surf)                 # (the frame shown: j-1's)
        if self.bpend is not None:      # (the back page as the last flip's
            base, band = self.bpend     # copy left it: its rows of the
            self.bpend = None           # frame that flip drew)
            if band is not None:
                g = e.g
                cv = bytearray(g.canvas(base))
                rb = len(cv) // g.h
                cv[band[0] * rb:band[1] * rb] = \
                    g.canvas(old)[band[0] * rb:band[1] * rb]
                t_ = g.surface()
                g.put(t_, bytes(cv))
                base = bytes(t_)
            self.bsurf = base
        if j in self.T and j not in self.done:
            self.done.add(j)
            if j in self.asg:
                i = self.asg.index(j)
                h = self.hs[i]
                if np.count_nonzero(h.screen != target) < \
                        np.count_nonzero(e.screen != target):
                    # THE NEXT BACK PAGE: the copy the calm made, or the old
                    # back page with what differs copied onto it - the
                    # cheaper; an unknown back page takes every row
                    k = self.cp.get(i)
                    bb = (0, e.g.h) if self.bunk else \
                        self._rows(self.bsurf, h.surf)
                    kb = self._rows(self.hs[k].surf, h.surf) \
                        if k is not None else None
                    use_k = k is not None and \
                        self._ccost(kb) <= self._ccost(bb)
                    band = kb if use_k else bb
                    cc = self._ccost(band)
                    room = min(e.cpu.room(), e.ceil, e.peak)
                    if cc > max(0.0, room) * PA_COPY_OK:
                        self.lost += 1
                        self.copyrefused += 1
                    else:
                        for kk in PA_STATE:
                            if hasattr(e, kk) and hasattr(h, kk):
                                x, y = getattr(e, kk), getattr(h, kk)
                                setattr(e, kk, y)
                                setattr(h, kk, x)
                        e.scache = h.scache = None
                        self.vp, self.hp[i] = self.hp[i], self.vp
                        if use_k:       # (the copy's page is the back page,
                            kp = self.hp[k]     # the old one a builder)
                            kbase = bytes(self.hs[k].surf)
                            self.hp[k] = self.bp
                            self._setsurf(self.hs[k], self.bsurf)
                            self.bp = kp
                            self.bpend = (kbase, kb)
                            if kb is not None:
                                self.copies = [(kp, self.vp) + kb]
                            self.precopied += 1
                        else:
                            if band is not None:
                                self.copies = [(self.bp, self.vp) + band]
                            self.bpend = (self.bsurf, band)
                        if self.copies:
                            self.ncopy += 1
                            self.crows += self.copies[0][3] - \
                                self.copies[0][2]
                        self.bunk = False
                        self.cp.pop(i, None)
                        if k is not None:
                            self.asg[k] = None
                        self.built = self.flipped = True
                        self.flips += 1
                        e.prev_c = cc   # (nothing brought up: the copy's
                                        # cycles in its place)
                else:
                    self.lost += 1
                self.asg[i] = None
            else:
                self.unbuilt += 1
        for i, F in enumerate(self.asg):    # (a target passed unflipped)
            if isinstance(F, int) and F < j:
                self.asg[i] = None
        for i in list(self.cp):         # (a copy whose build is gone)
            if self.asg[i] is None:
                self.asg[self.cp.pop(i)] = None
        if not self.built:              # AN ORDINARY FRAME: on the back page,
            self.vp, self.bp = self.bp, self.vp     # which is then shown
            if self.own:                # (as it stands: the model is what
                self._setsurf(e, self.bsurf)    # that page holds)
                self.built = True
            self.bsurf = old
            self.bunk = False
        self._assign(j)
        self.Fat[j] = self.F

    def _assign(self, j):
        held = set(x for x in self.asg if isinstance(x, int))
        for F in self.targets:
            if None not in self.asg:
                break
            if F > j + 1 and F not in held and F not in self.done:
                i = self.asg.index(None)
                self.asg[i] = F
                held.add(F)
        live = [x for x in self.asg if isinstance(x, int)]
        self.F = min(live) if live else None

    def after(self, j, rec, audio, target=None):
        """...the builders' writes, nearest target first, and the calm's
        copies of a whole build onto a free builder"""
        e = self.e
        if j == self.key0 and not hasattr(self, "preloaded") and \
                any(isinstance(x, int) for x in self.asg):
            if self.kpart is None:
                self.preloaded = 0
            else:
                self.preload()
        if self.flipped or not any(isinstance(x, int) for x in self.asg) \
                or e.last_why is not None or j >= len(self.log) or \
                any(qa <= j < qb for qa, qb in self.quiet):
            return None
        room = self._room(j, rec, audio)
        if room is None:
            return None
        cpu_left, dsk_left = room
        out, tc, tb = [], 0.0, 0.0
        # A COPY THAT CANNOT BE PAID AT THE FLIP (a 286: the whole canvas is
        # most of a period) is paid in the calm, so the nearest target keeps
        # a builder for its copy - the next target's build begins once that
        # copy is whole. Where it can be paid (a 486), every builder builds
        pre = len(self.hs) > 1 and \
            e.g.h * 80 * e.lcopy + CYC_LCOPY0 > e.ceil * PA_COPY_OK
        held = False                    # (the nearest's build or copy is
        for F, i in sorted((F, i) for i, F in enumerate(self.asg)  # not
                           if isinstance(F, int)):                 # whole)
            if cpu_left < 4 * e.ct[0] or dsk_left < 256:
                break
            h = self.hs[i]
            T = self.T[F]
            if held and F not in self.start:
                break
            held = pre and (np.any(h.screen != T) or
                            self.cleft.get(i, 0) < e.g.h)
            if np.any(h.screen != T):
                p = self.P.get(F)
                if p is not None and (h.pal is None or
                                      not np.array_equal(h.pal, p)):
                    h.set_pal(p)
                h.peak = h.ceil = cpu_left
                h.cpu = Budget(None, None)
                h.disk = Budget(None, None)
                h.rec_max = int(dsk_left)
                ops, hrec = h.frame(T, b"")
                if not ops:
                    break
                hc, hb = float(h.cost(hrec)), float(len(hrec))
                cpu_left -= hc
                dsk_left -= hb
                tc += hc
                tb += hb
                self.start.setdefault(F, j)
                out.append((self.hp[i], ops))
                if np.any(h.screen != T):
                    continue
            # BUILT WHOLE: copied onto another builder in bands, so its flip
            # leaves a back page already brought up - one free, or one whose
            # target is later and not begun (taken before the next build in
            # this frame could begin it)
            if not pre:
                continue                # (paid at the flip)
            k = self.cp.get(i)
            if k is None:
                cand = [x for x in range(len(self.asg)) if x != i and (
                    self.asg[x] is None or (isinstance(self.asg[x], int) and
                                            self.asg[x] > F and
                                            self.asg[x] not in self.start))]
                if not cand:
                    continue
                k = min(cand, key=lambda x: (self.asg[x] is not None,
                                             -(self.asg[x] or 0)))
                self.asg[k] = "copy"
                self.cp[i] = k
                self.cleft[i] = 0
            y0 = self.cleft[i]
            if y0 >= e.g.h or out and out[-1][0] == self.hp[i]:
                continue                # (whole; or built THIS frame: its
                                        # rows are copied from the next)
            per = 80 * e.lcopy
            n = int(min(e.g.h - y0, (cpu_left - CYC_LCOPY0) // per))
            if n < 1:
                break
            self.copies.append((self.hp[k], self.hp[i], y0, y0 + n))
            hk = self.hs[k]
            cv = bytearray(e.g.canvas(hk.surf))
            rb = len(cv) // e.g.h
            cv[y0 * rb:(y0 + n) * rb] = \
                e.g.canvas(h.surf)[y0 * rb:(y0 + n) * rb]
            t_ = e.g.surface()
            e.g.put(t_, bytes(cv))
            self._setsurf(hk, t_)
            self.cleft[i] = y0 + n
            cc = n * per + CYC_LCOPY0
            cpu_left -= cc
            dsk_left -= vid.COPY_LEN
            tc += cc
            tb += vid.COPY_LEN
        if not out and not self.copies:
            return None
        e.pa_spend = (tc, tb)
        self.cyc += tc
        self.bytes += tb
        return out


PA_COPY_OK = 1.0        # FLIPPED AND BUILT AHEAD (98.2.1.8.5): a flip is
                        # taken only when its copy fits the frame's own room
                        # - a flipped play that overruns its period stays
                        # behind for good (98.2.1.1.1)


PA_SCENE = 0.2          # ON N PAGES (98.2.1.8.4): a cut frame is a target of
                        # its own when this share of its pixels differs from
                        # the frame before - a scene change inside a cut run,
                        # which one page never reaches anyway


def page_count(a, L, scr):
    """--pages: the pages an N-page encode uses - 2 (98.1.3.7), a number,
    or MAX, every page the screen holds in a plane's 64 KB, 8 at most"""
    v = str(getattr(a, "pages", "2") or "2")
    most = min(vid.MPAGE_MAX, 0x10000 // vid.page_bytes(L, scr)) \
        if L != vid.LAY_HERC else 2
    n = most if v == "max" else int(v)
    if not 2 <= n <= most:
        raise vid.V88Error("--pages %s: this screen holds %d pages (98.1.3.7.1)"
                           % (v, most))
    return n


def picture_report(q_frames, q_bad, fps):
    """The picture's errors as a viewer meets them (2026-10-09): the mean
    over the clip hides a scene torn apart for two seconds among forty that
    play clean, so the worst second's mean and the seconds VISIBLY broken -
    VIS_AREA of the frame off by VIS_BAD as seen - are said beside it.
    (mean, worst second's mean, its frame, broken frames)"""
    n = len(q_frames)
    if not n:
        return 0.0, 0.0, 0, 0
    w = max(1, int(round(fps)))
    best, at, run = -1.0, 0, 0.0
    for i, v in enumerate(q_frames):
        run += v
        if i >= w:
            run -= q_frames[i - w]
        if i >= w - 1 and run > best:
            best, at = run, i - w + 1
    if best < 0:
        best, at = run, 0
    broken = sum(1 for b in q_bad if b >= VIS_AREA)
    return sum(q_frames) / n, best / min(w, n), at, broken


class Report(object):
    """THE END-OF-ENCODE REPORT, GROUPED: every line names the
    part of the file it is about - FILE (what is on the disk), BASE (the
    stream every machine plays, against the profile it was made for) and
    LAYER (what only the better machine reads, against its own) - and is
    said under that heading once the encode is done, in that order, so a
    line is never left to be placed by where it happened to be printed.
    `rep(part, text)` files a line; `rep.flush()` says them"""
    PARTS = ("file", "base", "layer")

    def __init__(self, say):
        self.say = say
        self.lines = dict((p, []) for p in self.PARTS)
        self.heads = {}

    def __call__(self, part, text):
        self.lines[part].append(text)

    def head(self, part, text):
        self.heads[part] = text

    def flush(self):
        for p in self.PARTS:
            if not self.lines[p] and p not in self.heads:
                continue
            self.say("   %-5s %s" % (p.upper(), self.heads.get(p, "")))
            for t in self.lines[p]:
                self.say("         " + t)


def machine_name(name, disk):
    """'the 286-fast, disk 391 KB/s' - a profile and the disk it was given"""
    return "%s, disk %s" % (
        "the %s" % name if name else "a machine of its own",
        "%.0f KB/s at a play's share of the period" % (disk / 1024.0)
        if disk else "none modelled")


def broken_spans(q_bad, fps, most=8):
    """Where the picture is VISIBLY BROKEN: (start s, length s) of each run
    of frames past VIS_AREA, runs under a second apart joined - so a torn
    scene is one entry to go and look at"""
    out, gap = [], max(1, int(round(fps)))
    for i, b in enumerate(q_bad):
        if b < VIS_AREA:
            continue
        if out and i - out[-1][1] <= gap:
            out[-1][1] = i
        else:
            out.append([i, i])
    return [(a / fps, (b - a + 1) / fps) for a, b in out[:most]], len(out)


LAYER_NOMIX = os.environ.get("OS88_LAYER_MIX") != "1"
                        # NO MIXED BYTE (98.1.9, 2026-10-14): the layer does
                        # not change a byte the base's next write to - on
                        # the page the layer draws on, before a flip -
                        # stores in PLANES. Such a write lands on the
                        # layer's colour and makes one neither stream chose,
                        # on the glass until the layer's record after it:
                        # the owner's white flicker, 117 frames of GasGas
                        # with over 100 such pixels. OS88_LAYER_MIX=1 is the
                        # A/B (an instrument's, not an option)
LAYER_NOMIXW = int(os.environ.get("OS88_LAYER_MIXW", "100000"))
                        # ...the base's frames it looks ahead (an instrument's)
LAYER_LAG = 8.0         # seconds the layer is encoded behind the base: the
                        # CPU bucket is a second deep, and a base at ~70% of
                        # it frees a full one in ~3 s, so 8 is near exact
LAYER_SEEK_MS = 10.0    # a seek each way per 32 KB of layer read (98.1.9)


class LayerEnc(object):
    """THE LAYER (SPEC.md 98.1.9; VIDEO-OVERAGE-PLAN 7): a second encoder
    whose screen is the ENHANCED player's - each frame the base's writes go
    on it first, then it spends its own budgets on the best further writes
    toward the same target. Both budgets are what the base CANNOT use on
    the better machine, so the base plays exactly as it would alone:
      - THE DISK: the base's own bucket simulated at the better machine's
        rate (the profile's curve at the hook's share, the layer's decode
        in it); what that bucket CLIPS - its ring full and the disk idle -
        is the layer's, banked in the layer's memory, less a seek each way
        per 32 KB the layer reads;
      - THE CPU: one bucket the two share. Run LAYER_LAG behind the base,
        it knows the base's next frames' costs, and takes only what stands
        above the least the bucket must hold to pay every one of them on
        time - so no base frame runs late for it.
    THE BETTER MACHINE may be ANOTHER CPU (`mach`, --layer-profile): an
    encoder made for its profile, which prices the base's records as IT
    decodes them, holds its CPU budget and its disk's curve - so a file
    made for a 5150 carries a layer for a 286. By default it is the base's
    own, the same CPU with a faster disk.
    `drop` > 0 is a measurement: a third screen that leaves every drop-th
    layer record out, as a late player would"""

    def __init__(self, base, rate, ram_kb, pre_kb, seek_ms=LAYER_SEEK_MS,
                 drop=0, bank=0.0, xcopy=0.0, mach=None, bxcopy=0.0,
                 need=None, lpool=None, track=False, future=None,
                 paged=False):
        import copy
        if getattr(base, "flip", False) or base.live:
            raise vid.V88Error("--layer is a stream's played unflipped in "
                               "the bracket (98.1.9): not --flip, not --live")
        if base.disk.per is None:
            raise vid.V88Error("--layer is a budgeted stream's: it is what "
                               "the base cannot use")
        self.b = base
        self.m = m = mach or base           # the layer's machine
        if m.cpu.per is None:
            raise vid.V88Error("--layer-profile names a machine with a "
                               "budget, not the lossless profile")
        self.e = self._clone(copy, ram_kb, pre_kb)
        # PAGE-AHEAD (98.2.1.8.3): the base has TWO pages, so the layered
        # picture is two - the page on the glass, which the base's records
        # and the layer's are drawn on, and the hidden one, which the
        # base's hidden records build and a flip shows, carrying whatever
        # the layer drew on it while it was shown
        self.eh = self._clone(copy, ram_kb, pre_kb) if paged else None
        self.sums = []                      # each frame's layered screen,
                                            # CRC'd (a gate's, frame by frame)
        self.d = self._clone(copy, ram_kb, pre_kb) if drop else None
        self.drop = drop
        self.rate = float(rate)
        # THE BANK THAT MACHINE KEEPS FOR THE BASE (98.1.9.2): the player
        # gives the base's bank only that much there, the rest of the pool
        # the layer's - known from a first pass (`need`), or learned as the
        # encode goes (the ring-alone tracker's worst, below)
        self.T = need
        self.lpool = lpool                  # --layer-bank auto: the pool
        self.track = track                  # (a first pass: no layer made)
        self.trace = []                     # ...its base, frame by frame
        self.future = future                # ...handed to the second pass
        self.req = None
        t0 = need or 0.0
        self.bl = base.reserve / 2.0 + min(getattr(base, "bankpre", 0), t0)
        self.S = m.cpu.level                # machine; the shared CPU bucket
        # ...and the same bucket WITHOUT the base's bank, the ring alone:
        # what it falls short there is the bank this machine needs to carry
        # the base at all (98.1.9.1) - the layer cannot touch it, being read
        # only when the ring is full
        self.ul = base.reserve / 2.0
        self.udef = self.uworst = 0.0
        self.ushort = 0
        self.held = 0                       # frames the layer was held back
        self.bshort = 0                     # ...and the base short anyway,
        self.bdry = self.bdworst = 0.0      # by how much at the worst
        self.seek = seek_ms / 1000.0
        self.lag = max(2, round(LAYER_LAG * base.fps))
        self.buf = []
        self.bw = []                        # ...each one's bytes, their planes
        self.nomix = 0                      # bytes the layer left (NOMIX)
        self.f = 0
        self.bytes = self.recs = self.skipped = 0
        self.cycles = 0.0
        self.pevs, self.pev_cut = [], []    # its scenes (98.1.3.2.3.1)
        self.read = 0                       # layer bytes, for the seeks
        # A LAYER BANK (--layer-bank): its bytes are copied up and down, so
        # while it holds anything the layer's disk is the profile's xcopy
        # slower - the base bank's rule (set_bank)
        self.lbank = float(bank)
        self.lslots = ram_kb * 1024.0 - self.lbank
        if lpool is not None:               # (auto: the pool less the base's)
            self.lbank = max(0.0, lpool - t0) // vid.SLOT * vid.SLOT
            for e in (self.e, self.d):
                if e is not None:
                    e.disk.cap = self.lslots + self.lbank
                    e.disk.level = min(e.disk.level, e.disk.cap)
        self.lslow = 1.0 + 2 * xcopy / 1000.0 * self.rate / 1024 \
            if bank and xcopy else 1.0
        # ...and THE BASE's bank, if it has one, played on this machine: its
        # bytes copied too while it holds anything, at THIS machine's xcopy
        self.bslow = 1.0 + 2 * bxcopy / 1000.0 * self.rate / 1024 \
            if base.bank and bxcopy else 1.0

    def _clone(self, copy, ram_kb, pre_kb):
        b = self.b
        e = copy.copy(self.m)               # (its tables; the base's screen)
        e.clone_screen(b)
        if hasattr(b, "tsurf"):
            e.tsurf = bytearray(b.tsurf)
            e.tv = np.frombuffer(e.tsurf, dtype=np.uint8)
        e.age = b.age.copy()
        e.wv = b.wv.copy()
        e.stats = {k: 0 for k in b.stats}
        e.q_vis = e.q_wrong = e.q_flick = e.q_tflick = 0.0
        e.q_prev = e.q_tprev = (None, None)
        e.q_frames, e.q_bad = [], []
        e.q_seen, e.q_stp = [], None
        e.cpu = Budget(0.0, 1e18)
        e.blocks = False                    # (98.1.10: a layer sharpens, and
        e.cb_due = False                    # one bit's codebook is the base's)
        e.dcurve = None
        e.owe = None
        e.bank, e.dslow = 0, 1.0
        e.dfloor = 0
        e.alead, e.arefill, e.slead, e.srefill = 0, [], 0, []
        e.disk = Budget(0.0, ram_kb * 1024.0)
        e.disk.level = min(pre_kb, ram_kb) * 1024.0
        # A LAYER RECORD'S ROOM is the layer's own, 32,760 bytes whatever
        # --frame-cap gives the base (vid.L_RECMAX): the machine copied the
        # base's 47 KB, so a layer with a bank and a fast CPU planned a
        # 36 KB record that layer_frame then refused, an hour in
        e.rec_max = min(e.rec_max, vid.L_RECMAX)
        return e

    def push(self, target, ops, rec, pal=None, hops=None, flip=False,
             pre=None, key=False):
        """The base's frame, just encoded: returns the layer's ops for the
        frames that are now LAYER_LAG behind it, in order. `pal`: the
        sixteen a scene starting at this frame is in (98.1.3.2.3.1), which
        the layer takes up when IT reaches the frame, LAYER_LAG later.
        PAGE-AHEAD (98.2.1.8.3): `hops` the base's writes into its hidden
        page after this frame's record, `flip` the frame's record drawn on
        that page and shown, `pre` key 0's PRELOAD of it, `key` the frame
        a page-ahead play starts at - priced and counted on this machine
        with the base's own record, a hidden record riding in it"""
        hrec = vid.record(hops, self.b.g, limit=None) if hops else b""
        self.bw.append(self._planes(ops))
        self.buf.append((target, ops, rec + hrec, self.m.cost(rec) +
                         (self.m.cost(hrec) if hrec else 0.0), pal, hops,
                         flip, pre, key))
        out = []
        while len(self.buf) > self.lag:
            out.append(self._one(False))
        return out

    def flush(self):
        out = []
        while self.buf:
            out.append(self._one(True))
        return out

    def _planes(self, ops):
        """A base frame's writes on the page shown -> (the bytes, whether
        each takes ALL FOUR planes): a write mode 3 sub-record's colour
        does, and so do plane sub-records whose masks add up to 15 in the
        frame. None on a screen without planes (LAYER_NOMIX's only)"""
        g = self.b.g
        if not LAYER_NOMIX or not getattr(g, "bitplanes", False):
            return None
        acc = np.zeros(65536, np.uint8)
        for m, sub in ops:
            pm = 15 if m >= 16 else m
            for a, bs, run in sub:
                acc[a:a + len(bs)] |= pm
        at = np.nonzero(acc)[0]
        return at, acc[at] == 15

    def _nomix(self, target):
        """THE LAYER'S TARGET with every byte the base will next store in
        planes - before the next flip - held at what this screen shows, so
        the layer leaves it: the base's planes land on the base's own
        colour, as they do without a layer (LAYER_NOMIX)"""
        g, e = self.b.g, self.e
        if not self.bw or self.bw[0] is None:
            return target
        dec = np.zeros(65536, np.int8)      # 0 unseen, 1 whole, 2 planes
        for n_, (w, t) in enumerate(zip(self.bw, self.buf)):
            if t[6] or n_ >= LAYER_NOMIXW:  # (a flip: this page goes hidden)
                break
            at, whole = w
            new = dec[at] == 0
            dec[at[new]] = np.where(whole[new], 1, 2)
        bad = np.nonzero(dec == 2)[0]
        if not len(bad):
            return target
        if not hasattr(self, "_rows"):
            rows = np.full(65536, -1, np.int32)
            xb = np.zeros(65536, np.int32)
            for y in range(g.h):
                rows[g.base[y]:g.base[y] + g.wb] = y
                xb[g.base[y]:g.base[y] + g.wb] = np.arange(g.wb)
            self._rows, self._xb = rows, xb
        y = self._rows[bad]
        ok = y >= 0
        bm = np.zeros((g.h, g.wb), bool)
        bm[y[ok], self._xb[bad[ok]]] = True
        pm = np.repeat(bm, 8, axis=1)[:, :target.shape[1]]
        self.nomix += int(np.count_nonzero(bm & np.any(
            (target != e.screen).reshape(g.h, g.wb, -1), axis=2)))
        return np.where(pm, e.screen, target)

    def _reqs(self, bcap):
        """THE BASE'S WHOLE FUTURE ON THIS DISK (98.2.1.3.3), from the first
        pass: req[t] the least the base's bucket must hold as frame t starts
        to pay every frame from t on, with no layer decoding - the disk at
        its slowest, the bank's copies charged throughout. Backwards from
        the end; past the bucket's cap the base cannot be carried at all"""
        b, m = self.b, self.m
        rate = self.rate / self.bslow
        req = [0.0] * (len(self.future) + 1)
        for t in range(len(self.future) - 1, -1, -1):
            use, c = self.future[t]
            rel = 1.0
            if m.dcurve:
                rel = m.disk_rel((c + m.audio_cyc + HOOK_CYC) / m.q + m.spk)
            flow = (rate * rel * 0.99 - b.abps) / b.fps
            req[t] = max(0.0, req[t + 1] + use - flow)
        return req

    def _inflow(self, cl=0.0, c=None, slow=False):
        """The disk's bytes this frame on the better machine, the sound's
        taken: the profile's curve at the hook's share, the base's bank's
        copies while it holds anything - or, `slow`, as though it always
        did, which is how the need for that bank is priced (98.2.1.3.3)"""
        b, m = self.b, self.m
        rel = 1.0
        if m.dcurve:                        # (the layer's machine's curve)
            share = ((c if c is not None else 0.0) + cl + m.audio_cyc +
                     HOOK_CYC) / m.q + m.spk
            rel = m.disk_rel(share)
        rate = self.rate / (self.bslow if slow or self.bl > b.reserve
                            else 1.0)
        return (rate * rel * 0.99 - b.abps) / b.fps

    def _one(self, end):
        b, e, m = self.b, self.e, self.m
        target, ops, rec, c, pal, hops, flip, pre, key = self.buf[0]
        if pal is not None:                 # A NEW SCENE (98.1.3.2.3.1):
            for x in (e, self.d):           # the layer ranks and measures
                if x is not None:           # in its colours from this frame,
                    x.set_pal(pal)          # as the player draws it - base
                                            # and layer after vp_palev
            self.pevs.append(self.f)
        per = m.cpu.per
        need = 0.0 if end else m.cpu.cap    # (past the window: a full one)
        for t in reversed(self.buf[1:]):
            need = max(0.0, need + t[3] - per)
        # THE BASE'S NEXT FRAMES, on this disk (98.1.9.1): the least its
        # bucket must hold now to pay every one of them - so a layer record
        # the reader would fetch before refilling the base's bank never
        # leaves the base short. The base's records past this one, against
        # this frame's inflow
        flow = self._inflow(c=c)
        bneed = 0.0
        for t in reversed(self.buf[1:]):
            bneed = max(0.0, bneed + len(t[2]) - b.abps / b.fps - flow)
        self.S = min(m.cpu.cap, self.S + per) - c     # the base's, first
        room = min(max(0.0, self.S - need), max(0.0, m.peak - c))
        e.future = [t[0] for t in self.buf[1:1 + b.look]]
        self.buf.pop(0)
        self.bw.pop(0)
        if not self.track:
            eh = self.eh
            if eh is not None and flip:     # (the frame's record onto the
                for k in PA_STATE:          # hidden page, shown: the pages
                    if hasattr(e, k) and hasattr(eh, k):    # swap first)
                        x, y = getattr(e, k), getattr(eh, k)
                        setattr(e, k, y)
                        setattr(eh, k, x)
                e.scache = eh.scache = None
            e.put_ops(ops)              # the base's writes, on this screen
            if eh is not None:          # ...and its hidden page's, on the
                if pre:                 # other: key 0's preload, then the
                    eh.put_ops(pre)     # frame's
                if hops:
                    eh.put_ops(hops)
        lops = []
        cl = 0.0
        # THE BASE'S BANK ON THIS MACHINE: what it needs to carry the base
        # and no more (98.1.9.2) - known, or the worst so far - and the
        # layer's, auto, the pool's rest
        tnow = self.T if self.T is not None else \
            -(-self.uworst // vid.SLOT) * vid.SLOT
        bcap = b.reserve + min(tnow, b.bank or 0)
        if self.lpool is not None and self.T is None:
            self.lbank = max(0.0, self.lpool - tnow) // vid.SLOT * vid.SLOT
            e.disk.cap = self.lslots + self.lbank
            e.disk.level = min(e.disk.level, e.disk.cap)
        have = e.disk.level                 # A REFILL THE BASE'S BANK NEEDS
        use = len(rec) - b.abps / b.fps     # is held back: a layer byte
        hold = False                        # spent now is one the reader
        if self.track:                      # (the first pass: the base, kept)
            self.trace.append((use, c))
        if bcap > b.reserve and self.future is not None and \
                len(self.future) > self.f:
            # TWO PASSES (98.2.1.3.3): the base's whole future is known, so
            # the layer's decode - which slows the disk the bank refills
            # from, on a curve - is held to what leaves the base's bucket at
            # or above the least every frame after this one needs. Exact,
            # and the bank's slack spent whenever there is any
            if self.req is None:
                self.req = self._reqs(bcap)
            line = self.req[self.f + 1]
            fit = lambda x: min(self.bl + self._inflow(x, c), bcap) - use \
                >= line
            if not fit(0.0):
                hold = True
            elif not fit(room):
                lo, hi = 0.0, room          # (the most decode that fits)
                for _ in range(10):
                    mid = (lo + hi) / 2
                    lo, hi = (mid, hi) if fit(mid) else (lo, mid)
                room = lo
            if hold:
                self.held += 1
        elif bcap > b.reserve:              # ONE PASS: the lag's window
            allow = max(0.0, self.bl + flow - use - bneed)  # With no bank
            if allow < have:                # the layer is read only with the
                e.disk.level = allow        # ring full, and costs it nothing
                self.held += 1
        spent = False
        # (a page-ahead play STARTS at its key and never draws the key
        # frame's layer record: there is none, so the model is the play's)
        if not self.track and not hold and room > e.ct[0] * 2 and \
                e.disk.level >= 64 and not (self.eh is not None and key):
            e.cpu.per, e.cpu.level = 0.0, room
            e.peak = room
            e.disk.per = 0.0
            lops, lrec = e.frame(self._nomix(target), b"")
            if lops and (b.g.planes == 1 or any(sub for m, sub in lops)):
                cl = e.cost(lrec)
                e.disk.level = have
                spent = True
                e.disk.spend(len(lrec))
                self.S -= cl
                self.cycles += cl
                self.bytes += len(lrec)
                self.recs += 1
                k = self.read // vid.SLOT   # a 32 KB read of the layer:
                self.read += len(lrec)      # a seek there and one back
                e.disk.level -= (self.read // vid.SLOT - k) * 2 * \
                    self.seek * self.rate
        else:
            self.skipped += 1
        if not spent:
            e.disk.level = have
            self.read += len(vid.L_EMPTY)   # (its 2 bytes are in the run too:
                                            # 98.1.9.8, the seeks exact)
        if b.metric and not self.track:
            e.measure(target)
        if not self.track:
            import zlib
            self.sums.append(zlib.crc32(bytes(b.g.canvas(e.surf))))
        if pal is not None and self.f and not self.track:
            self.pev_cut.append(int(np.count_nonzero(e.screen != target)))
        if self.d is not None:
            self.d.put_ops(ops)
            if lops and (self.f + 1) % self.drop:
                self.d.put_ops(lops)
            if b.metric:
                self.d.measure(target)
        # THE DISK this frame on the better machine, in the order the
        # player reads (98.1.9.1): the base's RING first, then the LAYER's
        # slots and its bank, and only then the base's BANK - the layer's
        # decode in the hook's share
        flow = self._inflow(cl, c)
        a = min(flow, max(0.0, b.reserve - self.bl))
        self.bl += a
        left = flow - a
        if left > 0 and self.bl < bcap:
            # THE BANK BELOW WHAT THIS MACHINE NEEDS FOR THE BASE: the bank
            # first, as every file before was read, up to that much - all
            # the base's bank there is on this machine (98.1.9.2)
            fill = min(left, bcap - self.bl)
            self.bl += fill
            left -= fill
        if left > 0:
            slow = self.lslow if e.disk.level > self.lslots else 1.0
            take = min(left, (e.disk.cap - e.disk.level) * slow)
            e.disk.level += take / slow     # (through its bank: copied)
            left -= take
        self.bl = min(self.bl, bcap) - use
        if self.bl < 0:                     # (the hold above is a window's:
            self.bdry -= self.bl            # caught, and said, not hidden -
            self.bdworst = max(self.bdworst, self.bdry)  # and how far, which
            self.bl = 0.0                   # is what the next pass adds)
            self.bshort += 1
        else:
            self.bdry = 0.0
        # ...and THE RING ALONE: what it falls short is drawn from a bank,
        # and paid back only from what a full ring leaves - the most ever
        # drawn is the bank this machine needs to carry the base. Priced
        # with that bank's copies charged throughout: a bank big enough to
        # need is one that holds something nearly always, and a first pass
        # whose own bank sat empty priced the disk at its fastest and found
        # too little (the Bebop for the speeddemon: dry in 8 frames)
        self.ul += self._inflow(cl, c, slow=bool(b.bank)) \
            if b.bank and self.bslow != 1.0 else flow
        if self.ul > b.reserve:
            self.udef = max(0.0, self.udef - (self.ul - b.reserve))
            self.ul = float(b.reserve)
        self.ul -= use
        if self.ul < 0:
            self.ushort += 1
            self.udef -= self.ul
            self.uworst = max(self.uworst, self.udef)
            self.ul = 0.0
        self.f += 1
        return lops


class _Ahead(object):
    """ffmpeg_video's frames READ AHEAD: ffmpeg started now and its output
    read on a thread, up to `cap` bytes of frames, while this process does
    something else - the levels pass, the palette's - so the decode is
    done beside that pass rather than after it. The same frames, in the
    same order; closed, ffmpeg goes with it (a cancel)"""

    def __init__(self, cmd, shape, cap, src):
        import queue
        import threading
        self.shape, self.src = shape, src
        self.n = n = int(np.prod(shape))
        self.q = queue.Queue(max(2, cap // max(1, n)))
        self.stop = threading.Event()
        self.p = subprocess.Popen(cmd, stdout=subprocess.PIPE)
        self.t = threading.Thread(target=self._pump, daemon=True)
        self.t.start()
        self.done = False

    def _pump(self):
        import queue
        while not self.stop.is_set():
            b = self.p.stdout.read(self.n)
            while not self.stop.is_set():
                try:
                    self.q.put(b, timeout=0.2)
                    break
                except queue.Full:
                    pass
            if len(b) < self.n:
                return

    def __iter__(self):
        return self

    def __next__(self):
        if self.done:
            raise StopIteration
        b = self.q.get()
        if len(b) < self.n:
            self.done = True
            self.p.wait()
            rc = self.p.returncode
            self.close()
            if rc:
                raise vid.V88Error("ffmpeg failed on %s" % self.src)
            raise StopIteration
        return np.frombuffer(b, dtype=np.uint8).reshape(self.shape)

    def close(self):
        self.done = True
        self.stop.set()
        if self.p.poll() is None:
            self.p.kill()
        self.t.join()
        self.p.stdout.close()
        self.p.wait()


def ffmpeg_video(src, w, h, crop_dar, fps_expr, start, end, eq, pix="gray",
                 ahead=0):
    """The frames, (h, w) grey or (h, w, 3) rgb24. `ahead`: started NOW
    and read ahead up to that many bytes (_Ahead), for a pass to run
    beside"""
    vf = []
    if crop_dar:
        vf.append("crop='if(gt(dar,%f),ih*%f*sar,iw)':'if(gt(dar,%f),ih,"
                  "iw/(%f)/sar)'" % (crop_dar, crop_dar, crop_dar, crop_dar))
    vf += ["fps=%s" % fps_expr, "scale=%d:%d:flags=area" % (w, h)]
    if eq:
        vf.append("eq=%s" % eq)
    vf.append("format=%s" % pix)
    cmd = ["ffmpeg", "-v", "error", "-nostdin"]
    if start:
        cmd += ["-ss", "%.3f" % start]
    cmd += ["-i", src]
    if end:
        cmd += ["-t", "%.3f" % (end - (start or 0))]
    cmd += ["-an", "-vf", ",".join(vf), "-f", "rawvideo", "-pix_fmt", pix,
            "-"]
    ch = 3 if pix == "rgb24" else 1
    if ahead:
        return _Ahead(cmd, (h, w, 3) if ch == 3 else (h, w), ahead, src)
    return _ffmpeg_frames(cmd, src, w, h, ch)


def _ffmpeg_frames(cmd, src, w, h, ch):
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    n = w * h * ch
    try:
        while True:
            b = p.stdout.read(n)
            if len(b) < n:
                break
            f = np.frombuffer(b, dtype=np.uint8)
            yield f.reshape(h, w, 3) if ch == 3 else f.reshape(h, w)
        p.wait()
        if p.returncode:
            raise vid.V88Error("ffmpeg failed on %s" % src)
    finally:
        # a CANCELLED encode closes this early: ffmpeg goes with it rather
        # than decoding the rest of the clip into a pipe nobody reads
        if p.poll() is None:
            p.kill()
        p.stdout.close()
        p.wait()


def ffmpeg_audio(src, rate, start, end, volume, fmt="u8"):
    """The sound, mono at `rate`: unsigned 8-bit, or with fmt="f32le" the
    floats the speaker's shaping starts from (98.2.15.1)"""
    cmd = ["ffmpeg", "-v", "error", "-nostdin"]
    if start:
        cmd += ["-ss", "%.3f" % start]
    cmd += ["-i", src]
    if end:
        cmd += ["-t", "%.3f" % (end - (start or 0))]
    # ON THE PICTURE'S TIMELINE (98.2.1.7): a raw stream carries no
    # timestamps, so a source whose sound STARTS LATER than its picture -
    # common in a downloaded or cut MP4 - came out with the gap dropped and
    # every sample early by it, where ffmpeg_video's fps filter pads the
    # picture's own gap from 0. first_pts=0 pads the sound's with silence
    # the same way (async=1 so the resampler may insert it)
    af = ["aresample=%d:async=1:first_pts=0" % rate]
    if volume:
        af.append("volume=%s" % volume)
    cmd += ["-vn", "-ac", "1", "-af", ",".join(af), "-f", fmt, "-acodec",
            "pcm_" + fmt, "-"]
    return subprocess.run(cmd, capture_output=True, check=True).stdout


def speaker_pcm(a, rate, say):
    """The source's sound SHAPED FOR THE SPEAKER (98.2.15.1), as PCM8 at
    `rate`, by the style and the numbers `a` gives - the one body a speaker
    .V88 and a speaker WAV (86.21.1) both take theirs from. What a 5150's
    cone can play would otherwise sit 25-30 dB under the pulses' own
    carrier"""
    st = vid.SPK_STYLES[getattr(a, "spk_style", None) or vid.SPK_STYLE]
    for k, v in (("spk_highpass", "hp"), ("spk_ratio", "ratio"),
                 ("spk_range", "rng")):
        if getattr(a, k, None) is None:
            setattr(a, k, st[v])
    pcm = vid.spk_shape_f(np.frombuffer(ffmpeg_audio(
        a.src, rate, a.start, a.end, a.volume, "f32le"), dtype="<f4"),
        rate, a.spk_highpass, a.spk_drive, lows=a.spk_lows,
        rng=a.spk_range, ratio=a.spk_ratio, idle=a.spk_idle)
    say("   speaker: shaped %s - nothing under %d Hz, the level evened "
        "out %g:1 and driven to %.0f%% RMS (--spk-shape none to take the "
        "sound as it is)" % (getattr(a, "spk_style", None)
                             or vid.SPK_STYLE, a.spk_highpass,
                             a.spk_ratio, 100 * a.spk_drive))
    return pcm


# --spk-shape: WHERE the speaker's sound is shaped (98.2.15.1, 86.21.1) -
# here, on the machine (a speaker WAV's alone: a plain WAV), or nowhere
SPK_ENC, SPK_MACH, SPK_NONE = "encoder", "machine", "none"
SPK_WAV_RATE = 8000     # a speaker WAV's default rate: Audio's top rung on
                        # an 8088, which copies counts no faster (86.21)


def is_wav(path):
    return path.lower().endswith(".wav")


def encode_wav(a, tick):
    """A SPEAKER WAV FOR AUDIO (SPEC.md 86.21.1): the source's sound alone,
    shaped for the speaker exactly as a speaker .V88's is (speaker_pcm) and
    stored as the speaker's COUNTS in an 'o8sp' WAV, which Audio copies
    into the ring with no shaping, resampling or decoding of its own
    (--spk-shape encoder; none stores the unshaped wave's counts the same
    way) - or, with --spk-shape machine, a PLAIN 8-bit mono WAV at the rate,
    which Audio shapes itself as it plays. No picture: every picture,
    colour and budget option is the video's"""
    say = (lambda *x: None) if a.quiet else print
    tick("prepare", 0, 0)
    need_tools()
    if a.audio not in (None, "speaker"):
        raise vid.V88Error("a .WAV is made for the PC speaker (86.21.1): "
                           "--audio %s is a .V88's" % a.audio)
    if a.spk_pulses != 1:
        raise vid.V88Error("--spk-pulses: a .V88's (34.11.7) - Audio plays "
                           "one pulse a sample")
    prof = PROFILES[a.profile]
    fast = bool(prof.get("spk_us"))
    rate = a.rate or SPK_WAV_RATE
    top = SPK_MAX_AT if fast else SPK_MAX_8088
    if not SPK_MIN <= rate <= top:
        raise vid.V88Error(
            "--rate %d: Audio plays a speaker WAV from %d to %d Hz on this "
            "profile%s" % (rate, SPK_MIN, top, "" if fast else
                           " - a 286 profile goes to %d" % SPK_MAX_AT))
    vid.spk_table(rate, 1, fast=fast)
    tick("sound", 0, 0)
    if a.spk_shape == SPK_ENC:
        pcm = speaker_pcm(a, rate, say)
    else:
        pcm = ffmpeg_audio(a.src, rate, a.start, a.end, a.volume)
    if not pcm:
        raise vid.V88Error("no sound came out of %s" % a.src)
    tick("write", 0, 0)
    if a.spk_shape != SPK_MACH:
        # ENCODER: shaped here; NONE: the wave as it is. Either way the speaker's
        # COUNTS in an 'o8sp' WAV (kind 2), which Audio copies untouched -
        # so an OFF file is the unshaped wave, under the carrier's whine
        counts = vid.spk_counts(bytes(pcm), rate, 1)
        vid.write_spk_wav(a.out, rate, counts, vid.SPK_WAV_COUNTS)
        secs = len(counts) / float(rate)
        size = os.path.getsize(a.out)
        say("os88venc: %s: %.1f s of the speaker's counts at %d Hz, %d "
            "bytes%s - Audio copies them to the speaker, and plays them on "
            "a card as the samples they came from (SPEC.md 86.21.1)"
            % (a.out, secs, rate, size, "" if a.spk_shape == SPK_ENC else
               ", NOT shaped"))
    else:
        # MACHINE: a PLAIN 8-bit mono WAV at the rate, no 'o8sp'
        # chunk, so Audio shapes it on the machine as it would any WAV
        # (86.21) - the resample and the bit depth done here, nothing else
        vid.write_spk_wav(a.out, rate, pcm, 0)
        secs = len(pcm) / float(rate)
        size = os.path.getsize(a.out)
        say("os88venc: %s: %.1f s of 8-bit PCM at %d Hz, %d bytes, not "
            "shaped - Audio shapes it for the speaker as it plays (SPEC.md "
            "86.21)" % (a.out, secs, rate, size))
        import os88spkfx
        counts = os88spkfx.Shaper(rate, os88spkfx.PRE_DIFF).feed(bytes(pcm))
    if getattr(a, "spk_preview", None):
        s2 = vid.write_spk_preview(a.spk_preview, counts, rate, 1)
        say("   speaker preview: %s, %.1f s%s" % (
            a.spk_preview, s2, "" if a.spk_shape != SPK_MACH else
            " (Audio's own shaping, as the machine will play it)"))
    return dict(bytes=size, secs=secs, rate=rate, wav=True)


def _once(a, key, make):
    """A whole-clip pass that comes out the same every time - a palette, the
    levels, an order - made ONCE for every pass of one encode: page-ahead's
    two to four, the ladder's, the layer's. They share a._scenes (the scene
    pass's own cache), and the key names everything the pass reads (the
    options audit, 2026-10-14: palettegen ran on every pass)"""
    sc = getattr(a, "_scenes", None)
    if sc is None:
        return make()
    k = ("once",) + tuple(key)
    if k not in sc:
        sc[k] = make()
    return sc[k]


def auto_levels(src, w, h, crop, start, end, eq):
    """The grey levels the picture really spans - its 1st and 99th
    percentile over a frame a second - so a source that lives in the
    middle greys dithers to the whole of black-to-white. One bit a pixel
    has no contrast to spare."""
    hist = np.zeros(256)
    for f in ffmpeg_video(src, w, h, crop, "1", start, end, eq):
        hist += np.bincount(f.ravel(), minlength=256)
    c = np.cumsum(hist) / max(1.0, hist.sum())
    lo = int(np.searchsorted(c, 0.01))
    hi = int(np.searchsorted(c, 0.99))
    return lo, max(hi, lo + 16)


def stretch(f, lo, hi):
    return np.clip((f.astype(np.int32) - lo) * 255 // (hi - lo), 0,
                   255).astype(np.uint8)


# --------------------------------------------------------------------------
class Cancelled(Exception):
    """What a `progress` callback raises to stop an encode (98.2.11). The
    .V88 is written only at the very end, so a cancelled encode writes
    nothing, and a file it would have replaced is left as it was."""


# (ffmpeg's palettegen gives no count of what it has read: its step says
# what it is and that there is no count, and not a share of nothing)
PALGEN_NOTE = "ffmpeg's palettegen over every frame of the clip: no count"


# THE PRESETS' LADDERS (98.2.1.4): a box one step up, on the same screen
LADDERS = (("cga-small", "cga"), ("cga4-small", "cga4"),
           ("herc", "herc-mid", "herc-full"), ("vga", "vga-mid", "vga-full"),
           ("vga4", "vga4-mid", "vga4-full"), ("vga8-small", "vga8"),
           ("modex-small", "modex"))
AIM_SOUND = 22050       # --aim quality's richer sound
AIM_WINDOW = 10.0       # ...its trial windows, seconds, three of them
AIM_WHOLE = 40.0        # ...or the whole clip when it is this short
AIM_ERR = 1.25          # ...a richer choice may be this much worse as seen
AIM_ERR0 = 0.002        # ...plus this, so a clip near 0 has room
AIM_CUT = 0.05          # ...and cut this share more of its frames
AIM_SEEN0 = 0.01        # ...and the VIEW as seen (seen_share) as AIM_ERR,
                        # plus this: a point of it, the view being shares
                        # of tens where the mean is tenths (VIDEO-SEEN-PLAN)
AIM_WORTH = 1.0         # --aim size's floor: pixels fixed a byte of stream


def _trial(b):
    """One --aim quality trial window: (error as seen, share cut, the view
    as seen - None where the format has no pixels to judge it by)"""
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        b.out = os.path.join(tmp, "t.V88")
        res = _encode(b, None, lambda *x: None, [])
    return res.get("q_vis", 0.0), res.get("cutf", 0.0), res.get("q_seen")


def aim_quality(a, tick, say):
    """--aim quality (98.2.1.4): what the budget can afford, found by
    TRYING it. Trial encodes of the clip (or three windows of a long one)
    at the asked settings, then a box a step up the preset's ladder at a
    time, then 22,050 Hz sound; each is taken while its picture stays
    within AIM_ERR of the asked one's error as seen - and of its VIEW as
    seen (seen_share, plus AIM_SEEN0), the share of where an eye looks
    that is broken - and cuts no more than AIM_CUT more of its frames.
    -> the args to encode with"""
    import copy
    dur = probe(a.src)[4] or 0.0
    end = a.end if a.end is not None else dur
    span = max(0.0, end - a.start)
    if span <= AIM_WHOLE:
        wins = [(a.start, end)]
    else:
        wins = [(a.start + span * f - AIM_WINDOW / 2,
                 a.start + span * f + AIM_WINDOW / 2)
                for f in (1 / 6.0, 0.5, 5 / 6.0)]

    jobs = a.jobs or os.cpu_count() or 1

    def window(args, i):
        b = copy.copy(args)
        b.start, b.end = wins[i]
        b.quiet, b.preview_png, b.aim = True, None, "asked"
        b.adpcm = "greedy"
        return b

    def trials(argss):
        """Each args' (error as seen, share cut, view as seen), its windows
        averaged -
        EVERY window of every one at once, `jobs` encodes at a time, each
        in a process of its own (and one core: a pool's worker dithers in
        itself). The numbers are one encode's each, whichever process made
        them, so what is taken is what the trials one by one took"""
        work = [window(x, i) for x in argss for i in range(len(wins))]
        note = "%d choice%s, each in %d window%s of the clip" % (
            len(argss), "" if len(argss) == 1 else "s", len(wins),
            "" if len(wins) == 1 else "s")
        if jobs > 1 and len(work) > 1:
            pool = worker_pool(min(jobs, len(work)))
            note += ", %d at once" % min(jobs, len(work))
            try:
                rs = [pool.apply_async(_trial, (b,)) for b in work]
                for r in rs:
                    while not r.ready():    # (a cancel is heard)
                        tick("trials", sum(x.ready() for x in rs),
                             len(rs), note)
                        r.wait(0.2)
                res = [r.get() for r in rs]
            finally:
                pool.terminate()
                pool.join()
        else:
            res = []
            for b in work:
                tick("trials", len(res), len(work), note)
                res.append(_trial(b))
        out, k = [], len(wins)
        for n in range(len(argss)):
            r = res[n * k:(n + 1) * k]
            vs = [v for e, c, v in r]
            out.append((sum(e for e, c, v in r) / k,
                        sum(c for e, c, v in r) / k,
                        None if None in vs else sum(vs) / k))
        return out

    def fine(q, q0):
        return q[0] <= q0[0] * AIM_ERR + AIM_ERR0 and \
            q[1] <= q0[1] + AIM_CUT and \
            (q[2] is None or q0[2] is None or
             q[2] <= q0[2] * AIM_ERR + AIM_SEEN0)

    def figs(q):
        return "%.2f%% error as seen%s, %.0f%% of frames cut" % (
            100 * q[0], "" if q[2] is None else
            ", the view %.1f%% broken" % (100 * q[2]), 100 * q[1])

    best = a
    lad = next((l for l in LADDERS if a.preset in l), None)
    steps = []
    if lad and not a.box and not a.live:
        for p in lad[lad.index(a.preset) + 1:]:
            b = copy.copy(a)            # (a step up is the asked settings
            b.preset = p                # at the next preset: what copying
            steps.append(b)             # the last one taken made)
    # THE LADDER ON SPARE CORES: when every step's windows fit the cores at
    # once they are all tried now, beside the asked settings, and taken or
    # not in order as before - a step past one not taken is work wasted,
    # which idle cores cost nothing
    ahead = steps if jobs >= (1 + len(steps)) * len(wins) else []
    got = trials([a] + ahead)
    q0 = got[0]
    say("   aim quality: as asked, %s" % figs(q0))
    if steps:
        for n, b in enumerate(steps):
            p = b.preset
            q = got[1 + n] if ahead else trials([b])[0]
            ok = fine(q, q0)
            say("   aim quality: %s, %s - %s" % (
                p, figs(q), "taken" if ok else "not taken"))
            if not ok:
                break
            best = b
    pd = PRESET_DEFAULTS.get(None if a.live else a.preset, {})
    rate = a.rate or pd.get("rate") or PROFILES[a.profile]["rate"]
    # (not the SPEAKER's: a richer rate is more of the machine, not more
    # of the file, and 22 kHz is past what an 8088 plays at all)
    if (a.audio or pd.get("audio") or PROFILES[a.profile]["audio"]) \
            not in ("none", "speaker") and rate < AIM_SOUND and not a.live:
        b = copy.copy(best)
        b.rate = AIM_SOUND
        q = trials([b])[0]
        ok = fine(q, q0)
        say("   aim quality: %d Hz sound, %s - %s" % (
            AIM_SOUND, figs(q), "taken" if ok else "not taken"))
        if ok:
            best = b
    return best


def encode(a, keep=None, progress=None):
    """`keep`, a list, is given every frame's target: the gate's.
    `progress(step, done, total, note)` is called as the encode goes -
    `step` what it is doing (os88vencgui's STEP_TEXT names them; "pass"
    begins a pass and its note says which), `done` of `total` the real
    count of what the step works through, `total` 0 when it has none to
    give, `note` what goes on beside it - and may raise Cancelled to stop
    it (98.2.11)"""
    readers = []
    tick = progress or (lambda step, done, total, note="": None)
    if is_wav(a.out):                   # the SOUND alone (86.21.1)
        return encode_wav(a, tick)
    try:
        if getattr(a, "aim", "asked") == "quality":
            a = aim_quality(a, tick, (lambda *x: None) if a.quiet else print)
        # PAGE-AHEAD FIRST (98.2.1.8.3): its plan pass is the plain base,
        # and its real passes are the LAYER's two where there is one
        pm = page_mode(a)
        if getattr(a, "_paplan", None) is None and \
                not getattr(a, "_paok", False) and \
                (pm == "on" or (pm == "auto" and page_auto(a))):
            return page_passes(a, keep, tick, readers, pm)
        if layered_two(a):
            res = layer_passes(a, keep, tick, readers)
        elif ladder_on(a) and not getattr(a, "_ladok", False):
            res = ladder_passes(a, keep, tick, readers)
        else:
            res = _encode(a, keep, tick, readers)
        lay = a.layout or (PRESETS[a.preset][0] if a.preset else None)
        pf = a.pixfmt or PRESET_PIXFMT.get(a.preset)
        if not a.quiet and not getattr(a, "_paok", False) and \
                (lay in ("modex", "herc") or pf == "vga4"):
            # (the screens page-ahead is about: why it did not run)
            print("   page-ahead: not run - %s (98.2.1.8.1)" % (
                "--page-ahead off" if pm == "off" else page_auto_why(a)))
        return res
    finally:
        # however it leaves - finished, refused or cancelled - the ffmpeg
        # reader is closed, and a live ffmpeg with it
        for r in readers:
            r.close()


LAYER_AGAIN = 3         # --layer-passes two: at most this many more real
                        # passes, each keeping the base what the last ran
                        # short by (98.2.1.3.3)


def disk_curve_for(prof, rate):
    """The profile's DISK CURVE (`disk_at`, 98.2.1.3) for a disk of `rate`
    on its machine (98.1.9.6): the curve is the profile's own disk's, and a
    disk that fast is CPU-bound - its PIO loop is what a decode takes the
    CPU from, so it reads 1.89 times as fast idle on the 486. A SLOWER disk
    is held to its own speed and gains less: each point's distance from 1
    is scaled by `rate` over the profile's disk, at most 1, so the disk
    given is the disk the layer is made and tested for. Before it, a 486
    told its disk was 600 KB/s still drew the player's line at 1,065 KB/s
    idle - the profile's 1.89 times it - and a machine with that disk never
    read its layer"""
    pts = prof.get("disk_at")
    if not pts or not rate or not prof.get("disk") or \
            rate >= prof["disk"]:
        return pts
    k = float(rate) / prof["disk"]
    return tuple((sh, 1.0 + (r - 1.0) * k) for sh, r in pts)


def layered_two(a):
    """A layered encode in two passes (98.2.1.3.3) not yet begun"""
    return bool(getattr(a, "layer_disk", None) or
                getattr(a, "layer_profile", None)) and \
        getattr(a, "layer_passes", "two") == "two" and \
        not getattr(a, "_lypass1", False) and \
        getattr(a, "_lyneed", None) is None


def page_real(a, keep, tick, readers):
    """One of page-ahead's real passes: the LAYER's two passes over it
    where the file has a layer (98.2.1.8.3), else the encode"""
    return layer_passes(a, keep, tick, readers) if layered_two(a) else \
        _encode(a, keep, tick, readers)


def page_mode(a):
    """--page-ahead as auto, on or off: a record made before version 25
    held it as a flag, and a script may set it so too"""
    v = getattr(a, "page_ahead", "auto")
    return "on" if v is True else "off" if v in (False, None) else v


def page_auto(a):
    """--page-ahead auto (98.2.1.8.1): the screens that ALWAYS have a second
    page - Mode X, and 16 colours on a screen of their own - in a file it
    can apply to. Not a Hercules: a colour card beside one takes its page"""
    return page_auto_why(a) is None


def page_auto_why(a):
    """Why --page-ahead auto does NOT run on this file, or None if it does:
    the report says it either way"""
    lay = a.layout or (PRESETS[a.preset][0] if a.preset else None)
    pf = a.pixfmt or PRESET_PIXFMT.get(a.preset)
    for cond, why in ((a.flip, "--flip pages every frame already"),
                      (a.live, "a Live file plays on the desktop"),
                      (a.resident, "a resident file plays on the desktop"),
                      (a.loop_from is not None, "a looped file"),
                      (PROFILES[profile_name(a.profile)]["avg"] is None,
                       "the profile has no budget to plan against")):
        if cond:
            return why
    if lay == "modex" or (pf == "vga4" and
                          (a.screen or "640x480") != "640x480"):
        return None
    if pf == "vga4":
        return ("640 x 480 in 16 colours has no second page (two of its "
                "frames are 300 KB of a VGA's 256)")
    if lay == "herc":
        return "a Hercules's second page is asked for, not assumed: " \
            "--page-ahead on"
    return "this screen has no second page"


def page_line(verdict, plain, page=None, pg=None, pruned=None):
    """THE PAGE-AHEAD LINE (the owner's ask): what it did to this file,
    whatever it decided - the plain encode's frames cut and seconds broken
    beside page-ahead's, its builds flipped to and pruned, and the file
    shipped"""
    def fig(r):
        n = r.get("cuts", 0)
        return "%d frame%s cut, %.1f s visibly broken%s" % (
            n, "" if n == 1 else "s", r.get("q_broken", 0.0),
            ", the view %.1f%% broken" % (100 * r["q_seen"])
            if "q_seen" in r else "")
    out = "   page-ahead: %s - plain: %s" % (verdict, fig(plain))
    if page is not None:
        out += "; with it: %s" % fig(page)
    if pg:
        out += "; %d of %d scene changes built ahead and flipped to" % (
            pg.get("flips", 0), pg.get("targets", 0))
    if pruned:
        out += "; %d of %d builds pruned" % pruned
    return out + " (98.2.1.8)"


class _Held(object):
    """stdout while a pass runs whose report may not be the one kept: the
    encoder window's PROGRESS lines go straight through, the rest is held"""

    def __init__(self, real):
        self.real, self.part, self.lines = real, "", []

    def write(self, t):
        self.part += t
        while "\n" in self.part:
            line, self.part = self.part.split("\n", 1)
            if line.startswith(PROGRESS):
                self.real.write(line + "\n")
                self.real.flush()
            else:
                self.lines.append(line)
        return len(t)

    def flush(self):
        self.real.flush()

    def text(self):
        return "".join(x + "\n" for x in self.lines + ([self.part]
                                                        if self.part else []))


def vfr_plan(plan, kept):
    """`plan` with only the slowed stretches `kept` (indexes into its "S"):
    the holds outside them, and the targets only their holds made, left
    out (VIDEO-SEEN-PLAN 3a, wave 2)"""
    S = plan.get("S") or []
    ks = [S[i] for i in kept]
    inn = lambda F: any(s0 <= F < s1 + 1 for s0, s1, _ in ks)
    H = set(F for F in plan.get("H") or () if inn(F))
    T = dict((F, t) for F, t in plan["T"].items()
             if F not in (plan.get("HT") or ()) or inn(F))
    out = dict(plan, H=H, T=T, S=ks)
    for x in ("LG", "CC"):              # (--ladder: its lag and cells)
        if x in plan:
            out[x] = dict((F, v) for F, v in plan[x].items() if inn(F))
    return out


class _Sink(object):
    """A stdout that keeps nothing (a background pass's progress lines)"""

    def write(self, t):
        return len(t)

    def flush(self):
        pass


def _pass_bg(tx, d):
    """PassBg's process: the pass, its report held, sent back"""
    import contextlib
    readers = []
    h = _Held(_Sink())
    # (its progress on a lane of its own, straight to the window's pipe -
    # the report is held, the bar is not: 98.2.11)
    tick = progress_lines("alongside", sys.__stdout__) if \
        getattr(d, "progress", False) else \
        (lambda step, done, total, note="": None)
    try:
        with contextlib.redirect_stdout(h):
            r = page_real(d, None, pass_tick(
                tick, "page-ahead's pass without the plan's holds"), readers)
        tx.send((True, (r, h.lines + ([h.part] if h.part else []))))
    except BaseException as x:          # (said where the pass was asked)
        try:
            tx.send((False, x))
        except Exception:
            tx.send((False, RuntimeError(repr(x))))
    finally:
        for rd in readers:
            rd.close()
        tx.close()


class PassBg(object):
    """A REAL PASS IN ANOTHER PROCESS (the encoder speed pass's second
    round, 2026-10-17): page-ahead's pass without the plan's holds reads
    nothing the pass with them makes, so it runs alongside it - its own
    reader, its own pool, its file `d.out` - and .result() is what
    page_real(d) would have returned here, with its report held. FORKED
    on Linux while this process has no other thread (worker_pool's rule),
    else spawned; .close() ends it however the encode ends"""

    def __init__(self, d):
        import multiprocessing
        import threading
        self.out, self.done = d.out, False
        ctx = multiprocessing.get_context(
            "fork" if sys.platform.startswith("linux") and
            threading.active_count() == 1 else "spawn")
        self.rx, tx = ctx.Pipe(False)
        self.p = ctx.Process(target=_pass_bg, args=(tx, d))
        self.p.start()
        tx.close()

    def result(self):
        """-> (res, _Held with its report)"""
        try:
            ok, v = self.rx.recv()
        except EOFError:
            raise vid.V88Error("a page-ahead pass ended without its file "
                               "(its process exited %s)" % self.p.exitcode)
        finally:
            self.p.join()
            self.done = True
        if not ok:
            raise v
        r, lines = v
        h = _Held(sys.stdout)
        h.lines = list(lines)
        return r, h

    def close(self):
        if self.p.is_alive():
            self.p.terminate()
        self.p.join()
        if not self.done:               # (its file, never read)
            try:
                os.remove(self.out)
            except OSError:
                pass


def pass_alongside(a, keep):
    """May a real pass run in another process here? Not inside a pool's
    worker (which may start none), not at --jobs 1, and not for the gate,
    whose `keep` the passes fill one after another"""
    import multiprocessing
    jobs = getattr(a, "jobs", None) or os.cpu_count() or 1
    return keep is None and jobs > 1 and \
        not multiprocessing.current_process().daemon


def vfr_page_select(c, plan, res, held, keep, tick, readers, bg=None):
    """--min-fps WITH PAGE-AHEAD (VIDEO-SEEN-PLAN 3a, wave 2): the page-ahead
    pass with the plan's holds (`res`, its report `held`) against one
    without them, stretch by stretch - each slowed stretch kept where its
    frames' view as seen was less broken with its holds - and, when the
    verdict is mixed, a third pass with the kept ones. The best of them by
    the view as seen is the file. `bg`, the pass without them already
    running alongside (vfr_page_start). -> (res, held report, plan)"""
    import contextlib
    import copy
    import shutil
    import tempfile
    S = plan["S"]
    runs = [(res, held, plan, None)]
    tmp = []

    def run(pl, label):
        d = copy.copy(c)
        d._paplan = pl
        fd, d.out = tempfile.mkstemp(suffix=".v88")
        os.close(fd)
        tmp.append(d.out)
        if keep is not None:
            del keep[:]
        pass_tick(tick, label)
        h = _Held(sys.stdout)
        with contextlib.redirect_stdout(h):
            r = page_real(d, keep, tick, readers)
        runs.append((r, h, pl, d.out))
        return r
    try:
        if bg is not None:
            d, job = bg
            tmp.append(d.out)
            tick("wait", 0, 0, "page-ahead's pass without the holds, "
                 "running alongside, to finish")
            rb, h = job.result()
            readers.remove(job)
            runs.append((rb, h, d._paplan, d.out))
        else:
            rb = run(vfr_plan(plan, []), "page-ahead's pass without the "
                     "plan's holds")
        qa, qb = res.get("q_seenf"), rb.get("q_seenf")
        if not qa or not qb or len(qa) != len(qb):
            kept = list(range(len(S)))
        else:
            kept = [i for i, (s0, s1, _) in enumerate(S)
                    if sum(qa[s0:s1 + 1]) < sum(qb[s0:s1 + 1])]
        if 0 < len(kept) < len(S):
            run(vfr_plan(plan, kept), "page-ahead again, with only the %d "
                "hold%s that drew better" % (len(kept), "" if len(kept) == 1
                                              else "s"))
        best = min(runs, key=lambda x: x[0].get("q_seen", 0.0))
        if best[3] is not None:
            shutil.move(best[3], c.out)
        note = ("   holding frames with page-ahead: %d of %d slowed stretches drew "
                "better with their holds; the file is the pass %s - the "
                "view %.1f%% broken, against %.1f%% with every hold and "
                "%.1f%% with none (VIDEO-SEEN-PLAN 3a)" % (
                    len(kept), len(S),
                    "with every hold" if best is runs[0] else
                    "with none" if best is runs[1] else
                    "with the %d kept" % len(kept),
                    100 * best[0].get("q_seen", 0.0),
                    100 * res.get("q_seen", 0.0),
                    100 * rb.get("q_seen", 0.0)))
        best[1].lines.append(note)
        return best[0], best[1], best[2]
    finally:
        for o in tmp:
            try:
                os.remove(o)
            except OSError:
                pass


def vfr_page_start(c, plan, keep, readers):
    """vfr_page_select's pass WITHOUT the holds, started in another process
    before the pass with them (PassBg) -> (its namespace, the job), or
    None where it must wait its turn (pass_alongside)"""
    import copy
    import tempfile
    if not pass_alongside(c, keep):
        return None
    d = copy.copy(c)
    d._paplan = vfr_plan(plan, [])
    fd, d.out = tempfile.mkstemp(suffix=".v88")
    os.close(fd)
    try:
        job = PassBg(d)
    except Exception:
        os.remove(d.out)
        raise
    readers.append(job)                 # (ended however encode ends)
    return d, job


def page_passes(a, keep, tick, readers, mode="on"):
    """--page-ahead (98.2.1.8): the encode once as it would be made without
    the second page, for the PLAN - each frame's budgets and the frames a
    scene change cuts - and then the real pass, which builds those frames
    ahead on the hidden page. AUTO (98.2.1.8.1) keeps the first pass's file
    and its report beside it and ships whichever is the better picture: the
    page-ahead file only where it is better somewhere and worse nowhere -
    the mean error as seen, its worst second, the frames visibly broken and
    the frames cut. --page-prune (98.2.1.8.2) may add a THIRD pass without
    only the builds that made their own stretch worse (page_hurt): `rescue`
    where auto would ship the plain file, `always` wherever a build hurt.
    Progress: each pass says which it is as it begins (pass_tick), and its
    steps are reported as they happen"""
    import contextlib
    import copy
    import shutil
    import tempfile
    a = copy.copy(a)
    a._scenes = {}                      # (one scene pass for every pass)
    b = copy.copy(a)
    b._paok, b.preview_png, b.aim = True, None, "asked"
    # (the plan is the PLAIN BASE: a layer is drawn over the base and never
    # changes it, so the plan pass makes none - 98.2.1.8.3)
    b.layer_disk = b.layer_profile = b.layer_prefill = None
    fd, b.out = tempfile.mkstemp(suffix=".v88")
    os.close(fd)
    d = None
    h1 = _Held(sys.stdout)
    try:
        with contextlib.redirect_stdout(h1):
            res1 = _encode(b, None, pass_tick(
                tick, "the first pass: the plain encode, which page-ahead "
                "plans from"), readers)
        # THE LADDER READS THIS PASS (98.2.1.9) - and when it steps a scene
        # down, the plan is made AGAIN, stepped: a stepped scene's frames
        # are other colours at other plane codes, so the plain pass's
        # targets would build ahead toward pictures the real pass never
        # draws, and its file would be the wrong one to weigh page-ahead
        # against and to fall back on (the options audit, 2026-10-14)
        steps = ladder_decide(a, res1, tick) if ladder_on(a) else {}
        if steps:
            b._ladok, b._steps = True, steps
            h1 = _Held(sys.stdout)
            with contextlib.redirect_stdout(h1):
                res1 = _encode(b, None, pass_tick(
                    tick, "the first pass again, %d scene%s in fewer colours"
                    % (len(steps), "" if len(steps) == 1 else "s")), readers)
        plan = res1.get("paplan")
        layered = bool(getattr(a, "layer_disk", None) or
                       getattr(a, "layer_profile", None))

        def plain():
            """THE PLAIN FILE, shipped: the plan pass's own - or, with a
            LAYER, made again with it, the plan's being the bare base
            (98.2.1.8.3): only when it is shipped, so a kept page-ahead
            file costs no pass for it"""
            if not layered:
                shutil.move(b.out, a.out)
                sys.stdout.write(h1.text())
                return res1
            c0 = copy.copy(a)
            c0._paok, c0.page_ahead = True, "off"
            c0._ladok, c0._steps = True, steps
            if keep is not None:
                del keep[:]
            pass_tick(tick, "the plain file again, with its layer")
            return page_real(c0, keep, tick, readers)
        if mode == "auto" and (not plan or not plan["T"]):
            r0 = plain()                # (nothing cut: nothing to build)
            if not a.quiet:
                print(page_line("NOTHING TO BUILD - no scene change the "
                                "budgets cut", res1))
            return r0
        c = copy.copy(a)
        c._paok, c._paplan = True, plan
        c._ladok, c._steps = True, steps
        # (the pass without the holds reads nothing this one makes: it
        # runs alongside - vfr_page_start)
        bg = vfr_page_start(c, plan, keep, readers) \
            if plan and plan.get("S") else None
        pass_tick(tick, "page-ahead's pass%s" % (
            ", with the plan's holds" if bg is not None else
            ", with the plan's holds (the one without them after it)"
            if plan and plan.get("S") else ""))
        h2 = _Held(sys.stdout)
        with contextlib.redirect_stdout(h2):
            res2 = page_real(c, keep, tick, readers)
        if plan and plan.get("S"):
            # THE HOLDS WITH PAGE-AHEAD (VIDEO-SEEN-PLAN 3a, wave 2): the
            # plan pass chose them without the hidden page, so each slowed
            # stretch is kept only where it drew better WITH page-ahead
            res2, h2, plan = vfr_page_select(c, plan, res2, h2, keep, tick,
                                             readers, bg)
            c._paplan = plan
        # (a file that may HOLD frames - --min-fps, --ladder - trades cut
        # frames for held ones by design, and the passes hold differently:
        # its picture's measures judge it, and not a count of either - the
        # count alone dropped a page-ahead file better on every one of them)
        ok, worse, better, whynot = page_verdict(res2, res1)
        pg = res2.get("page") or {}
        drop = mode == "auto" and (not ok or not pg.get("flips"))
        prune = getattr(a, "page_prune", "rescue")
        note = None
        pruned = None
        if pg.get("flips") and (prune == "always" or
                                (prune == "rescue" and drop)):
            # THE PRUNE (98.2.1.8.2): once more without only the builds
            # whose own stretch came out worse than the plain encode's
            hurt = page_hurt(res1, res2)
            nb = len(pg.get("start") or {})
            if hurt and len(hurt) < nb:
                # a pruned build's stretch is spent on NOTHING, not on the
                # next build: left out, it is the plain encode's frames
                st = sorted(pg["start"].values())
                quiet = [(pg["start"][F],
                          next((x for x in st if x > pg["start"][F]),
                               len(res1["q_frames"]))) for F in hurt
                         if F in pg["start"]]
                d = copy.copy(a)
                d._paok = True
                d._ladok, d._steps = True, steps    # (the ladder's, kept)
                d._paplan = dict(plan, quiet=quiet, T=dict(
                    (F, t) for F, t in plan["T"].items() if F not in hurt))
                fd, d.out = tempfile.mkstemp(suffix=".v88")
                os.close(fd)
                if keep is not None:
                    del keep[:]
                pass_tick(tick, "page-ahead again, without the %d build%s "
                          "that made their own stretch worse" % (
                              len(hurt), "" if len(hurt) == 1 else "s"))
                h3 = _Held(sys.stdout)
                with contextlib.redirect_stdout(h3):
                    res3 = page_real(d, keep, tick, readers)
                p3 = res3.get("page") or {}
                # (a pruned build's scene change is cut again by design:
                # the frames cut cannot judge a prune, the picture can)
                ok32, w32, b32, _ = page_verdict(res3, res2)
                ok3, w3, b3, why3 = page_verdict(res3, res1)
                if p3.get("flips") and (ok32 or (drop and ok3)):
                    shutil.move(d.out, a.out)
                    res2, h2, pg = res3, h3, p3
                    worse, better, whynot = w3, b3, why3
                    drop = mode == "auto" and not ok3
                    pruned = (len(hurt), nb)
                    note = ("   page-ahead: PRUNED - %d of %d builds that "
                            "made their own stretch worse left out; better "
                            "than with them: %s (98.2.1.8.2)"
                            % (len(hurt), nb, ", ".join(b32) or "-"))
                else:
                    note = ("   page-ahead: tried once more without the %d "
                            "of %d builds that made their own stretch worse, "
                            "and it was no better - worse %s; better %s "
                            "(98.2.1.8.2)" % (len(hurt), nb,
                                              ", ".join(w32) or "nowhere",
                                              ", ".join(b32) or "nowhere"))
        k0 = page_key0(a.out, res2)
        if k0:
            # NO KEY 0, NO PAGE-AHEAD (98.1.3.7): a page-ahead file's key 0
            # carries the first flip's picture on the hidden page, and every
            # later byte the encoder writes there assumes it - a byte changed
            # in some planes is written to those planes alone. A key 0 the
            # player cannot read (past the table's 65,535, or past the
            # 61,440 it reads in one go) leaves that page black where the
            # preload was, and the plane writes over the black make colours
            # nobody chose, on that page for the rest of the play (the
            # owner's GASGAS4, 2026-10-14: the signature broken at every
            # other flip). On or Auto, the file is the plain encode (with
            # its layer, where it has one: 98.2.1.8.3)
            r0 = plain()
            if not a.quiet:
                print("   page-ahead: DROPPED%s - its key 0 (frame %d) is %d "
                      "bytes, past the %d the player reads in one go, and "
                      "the hidden page's first picture is in it. The file is "
                      "the plain encode (98.1.3.7)"
                      % (" though --page-ahead on" if mode == "on" else "",
                         k0[0], k0[1], KEY_PLAYER))
                print(page_line("DROPPED", res1, res2, pg, pruned))
            return r0
        if mode == "on":
            sys.stdout.write(h2.text())
            if not a.quiet:
                if note:
                    print(note)
                print(page_line("KEPT (--page-ahead on)", res1, res2, pg,
                                pruned))
            return res2
        if drop or not pg.get("flips"):
            r0 = plain()
            if not a.quiet:
                if note:
                    print(note)
                print("   page-ahead: DROPPED - %s; the file is the plain "
                      "encode (98.2.1.8.1)%s" % (
                          "no flip" if not pg.get("flips") else whynot,
                          "; it was better %s" % ", ".join(better)
                          if better and pg.get("flips") else ""))
                print(page_line("DROPPED", res1, res2, pg, pruned))
            return r0
        sys.stdout.write(h2.text())
        if not a.quiet:
            if note:
                print(note)
            print("   page-ahead: KEPT - the view as seen decides: better "
                  "%s; worse %s (98.2.1.8.1)" % (", ".join(better),
                                                 ", ".join(worse) or
                                                 "nowhere"))
            print(page_line("KEPT", res1, res2, pg, pruned))
        return res2
    finally:
        for o in (b.out, d.out if d is not None else None):
            try:
                if o:
                    os.remove(o)
            except OSError:
                pass


BENCH_WARN = 0.15   # the bench's machines' ratio this far from a file's
                    # own frames' is said in the report (98.1.9.13)
LY_TMIN = 100   # the least yardstick the player's bench judges a CPU by, in
                # 10 us (SPEC.md 98.1.9.12; video.asm's VP_LYTMIN): key 0
                # modelled under a millisecond is all fixed cost the model
                # does not count, and both machines read the same


def key_bench(m, rec, wr):
    """THE YARDSTICK'S WORK (98.1.9, 98.3.18.9): key 0's record as the
    player decodes it to time itself - vp_lybench's vp_decboth, which on a
    flipped or page-ahead file draws the key onto BOTH pages, and on a
    page-ahead one the hidden page's record after it. `m.cost` prices one
    record's lists once, so a page-ahead key was modelled at a third of
    the work: the owner's 486 timed 6 ms against a yardstick of 2.26, and
    the layer it was made for went unread"""
    if getattr(wr, "mpage", False):     # (98.3.8.4: vp_decn, onto EVERY
        flip, pg, own, hs = vid.mpage_parts(rec, wr.g, wr.pages,  # page,
                                            key=True)
        return wr.pages * m.cost(own) + sum(    # and its chain)
            m.cost(h) for p_, h in hs)
    if wr.page:
        flip, own, hrec = vid.page_parts(rec, wr.g, key=True)
        return 2 * m.cost(own) + (m.cost(hrec) if hrec else 0)
    return (2 if wr.flip else 1) * m.cost(rec)


# DESKTOP POSTERS (98.1.3.11): the window's room for a picture at its own
# size on a 640 x 480 desktop - wider or taller and it is shown at half
DESK_FIT = (630, 400)
_DESK_CACHE = {}


def desk_choice(pixfmt, cgapal, own, pw, ph, mode, size, custom):
    """What a file's DESKTOP POSTERS are (98.1.3.11): (scale, flags), or
    None for no table. `own`: a VGA4 file in colours of its own. The sets
    by default are what the format is seen in - colour for VGA's, black
    and white for one bit's, both for CGA's colours, which a VGA shows in
    colour and a CGA's desktop cannot - and keys by default where the
    player's own picture is poor: 256 colours (one-bit luma), CGA's (four
    shades) and 16 of their own or shown smaller (the nearest colour of a
    decimation)"""
    fit = next((s for s in (1, 2) if pw // s <= DESK_FIT[0] and
                ph // s <= DESK_FIT[1]), 4)
    ts = {"fit": fit, "half": max(fit, 2), "quarter": 4}[size]
    while ts < 4 and vid.thumb_size("c", pw // ts, -(-ph // ts)) > \
            vid.THUMB_MAX:              # (a picture the player can hold)
        ts *= 2
    if pixfmt in (vid.PF_VGA4, vid.PF_VGA8):
        kinds = TH_C
    elif pixfmt in (vid.PF_CGA4, vid.PF_C160, vid.PF_C512) or \
            (pixfmt == vid.PF_TEXT and cgapal == vid.TEXT_COLOUR):
        kinds = TH_C | TH_M
    else:
        kinds = TH_M
    keys = pixfmt in (vid.PF_VGA8, vid.PF_CGA4, vid.PF_C160,
                      vid.PF_C512) or \
        (pixfmt == vid.PF_VGA4 and (own or ts > 1))
    if mode in ("colour", "mono", "both"):
        kinds = {"colour": TH_C, "mono": TH_M, "both": TH_C | TH_M}[mode]
        keys = True
    elif mode == "off":
        keys = False
    fl = (vid.TH_KEYS if keys else 0) | (vid.TH_CUSTOM if custom else 0)
    if fl:
        fl |= kinds
    if size != "fit":
        fl |= vid.TH_SIZE
    return (ts, fl) if fl else None


TH_C, TH_M = vid.TH_COLOUR, vid.TH_MONO


def desk_picture(rgb, pw, ph, ts, kind, clip=0.0):
    """One desktop poster from a picture in six-bit RGB at the Preview's
    own size"""
    return (vid.desktop_poster_rgb if kind == "c" else
            vid.desktop_mono_rgb)(rgb, pw, ph, ts, clip)


def desk_posters(a, wr, g, palette, lw, lh, crop, rate, spf, eq, rep):
    """wr.thumbs (98.1.3.11): each key's poster from its canvas in the
    colours in effect at its frame, and a custom one from --poster-image,
    in the sets and at the size desk_choice says"""
    mode = getattr(a, "desktop_posters", "auto")
    size = getattr(a, "desktop_size", "fit")
    custom = getattr(a, "poster_image", None)
    dclip = getattr(a, "desktop_clip", 0.0) or 0.0
    pf = wr.pixfmt
    pev = list(getattr(wr, "pev", None) or [])
    base = bytes((palette or vid.STD16)[:48 if pf == vid.PF_VGA4 else 768])
    if pf == vid.PF_VGA4:           # (None: the desktop's own)
        pals = [bytes(p[:48]) for f, p in pev] or [base]
        own = any(p != vid.STD16 for p in pals)
    else:
        own = False
    rs = getattr(wr, "rowscale", 1) or 1
    pw, ph = vid.thumb_dims(pf, g, rs)
    ch = desk_choice(pf, wr.cgapal, own, pw, ph, mode, size, custom)
    if ch is None:
        return
    ts, fl = ch

    def make(cv, kind):
        f = None
        for k, r, c in wr.keys:
            if c is cv:
                f = k
        pal = base
        for fr, p in pev:
            if f is not None and fr <= f:
                pal = bytes(p[:48])
        key = (hash(cv), len(cv), pal, ts, kind, dclip)
        pk = _DESK_CACHE.get(key)
        if pk is None:
            rgb = vid.canvas_rgb(pf, cv, g, pal, wr.cgapal, rs)
            pk = _DESK_CACHE[key] = desk_picture(rgb, pw, ph, ts, kind,
                                                 dclip)
        return pk
    cpk = None
    if custom:
        if custom.startswith("@"):
            t0 = (a.start or 0.0) + float(custom[1:])
            src, t1 = a.src, t0 + 2.0 * spf / rate
        else:
            src, t0, t1 = custom, 0.0, None
        fr = next(iter(ffmpeg_video(src, lw, lh, crop, "%d/%d" % (rate, spf),
                                    t0, t1, ":".join(eq), pix="rgb24")),
                  None)
        if fr is None or fr.shape[:2] != (lh, lw):
            raise vid.V88Error("--poster-image %s: no picture of %d x %d "
                               "came out of it" % (custom, lw, lh))
        if (lw, lh) != (pw, ph):        # (the Preview's own shape: CGA's
            from PIL import Image       # colours two wide, a text cell four)
            fr = np.asarray(Image.fromarray(fr).resize(
                (pw, ph), Image.BOX if pw <= lw and ph <= lh else
                Image.NEAREST))
        rgb = fr.astype(np.float64) * 63.0 / 255.0
        cpk = {k: desk_picture(rgb, pw, ph, ts, k, dclip)
               for k in vid.thumb_kinds(fl)}
    wr.thumbs = dict(scale=ts, flags=fl, make=make, custom=cpk)
    sets = " and ".join(n for b, n in ((TH_C, "in the desktop's sixteen"),
                                       (TH_M, "in black and white"))
                        if fl & b)
    what = ", ".join(x for x in (
        "a key's each" if fl & vid.TH_KEYS and wr.keys else "",
        "a custom one" if cpk is not None else "") if x)
    rep("file", "desktop posters: %s at %s%s%s (98.1.3.11)" % (
        what or "none", {1: "its own size", 2: "half", 4: "a quarter"}[ts],
        ", " + sets if sets else "",
        ", the Preview's size" if fl & vid.TH_SIZE else ""))


def reposter(a, say=print):
    """--posters-only (98.1.3.11.1): an EXISTING file's desktop posters
    made again - every rendition's, from its own keys decoded and the
    palette in effect at each, with --desktop-posters, --desktop-size,
    --desktop-clip and --poster-image (a picture file) as an encode would
    take them - and nothing else in the stream touched. The table goes
    back where the old one was when it fits that block's sectors, and
    after everything else in the file when it does not, the old bytes
    then named by nothing (set_poster's rule); the stored options learn
    the new settings, so a re-encode from the file keeps them. `a.src` is
    the file and `a.out` where it goes (the same path rewrites it).
    -> the pictures written"""
    from PIL import Image
    mode = getattr(a, "desktop_posters", "auto")
    size = getattr(a, "desktop_size", "fit")
    dclip = getattr(a, "desktop_clip", 0.0) or 0.0
    image = getattr(a, "poster_image", None)
    if image and image.startswith("@"):
        raise vid.V88Error("--poster-image %s with --posters-only: a moment "
                           "of the source is placed by the encode's fit and "
                           "crop, which the file does not keep - give a "
                           "picture file" % image)
    d = bytearray(open(a.src, "rb").read())
    r0 = vid.Reader(a.src)
    tail = len(d) + (-len(d) % vid.SECTOR)
    total = 0
    for ri in range(r0.nrend):
        r = vid.Reader(a.src, ri)
        pf, g, rs = r.pixfmt, r.g, r.rowscale or 1
        if r.live and size != "fit":
            raise vid.V88Error("--desktop-size %s for a Live file: it plays "
                               "in its window at its own size" % size)
        pals = [bytes(p[:48]) for f, p in (r.pev or [])] or \
            [bytes((r.palette or vid.STD16)[:48])]
        own = pf == vid.PF_VGA4 and any(p != vid.STD16 for p in pals)
        pw, ph = vid.thumb_dims(pf, g, rs)
        old = r.thumb
        cpk = None
        if image:
            cpk = True                  # (made once the sets are known)
        elif old is not None and old[5] & vid.TH_CUSTOM:
            cpk = "keep"
        ch = desk_choice(pf, r.cgapal, own, pw, ph, mode, size, cpk)
        kinds = vid.thumb_kinds(ch[1]) if ch else []
        if ch and cpk == "keep":
            have = vid.thumb_kinds(old[5])
            if old[4] != ch[0] or any(k not in have for k in kinds):
                raise vid.V88Error(
                    "rendition %d's custom poster is %s at a scale of %d, and "
                    "the new posters want %s at %d: give --poster-image to "
                    "make it again" % (ri, "+".join(have), old[4],
                                       "+".join(kinds), ch[0]))
            cpk = {k: r.thumb_pic(r.thumb_at(r.nkeys, k)) for k in kinds}
        elif ch and image:
            im = Image.open(image).convert("RGB").resize((pw, ph),
                                                          Image.BOX)
            rgb = np.asarray(im).astype(np.float64) * 63.0 / 255.0
            cpk = {k: desk_picture(rgb, pw, ph, ch[0], k, dclip)
                   for k in kinds}
        keys = []
        if ch and ch[1] & vid.TH_KEYS:
            for i, e in enumerate(r.keys):
                keys.append((e[0], None, vid.decode_at(r, e[0])))

        def make(cv, kind, keys=keys, r=r, pf=pf, g=g, rs=rs, ts=ch and ch[0]):
            f = next(k for k, _, c in keys if c is cv)
            pal = vid.palette_at(r, f)      # (None: the desktop's sixteen)
            rgb = vid.canvas_rgb(pf, cv, g, (pal[:48] if pal else None)
                                 if pf == vid.PF_VGA4 else pal, r.cgapal, rs)
            return desk_picture(rgb, pw, ph, ts, kind, dclip)
        slot = r.slot + (vid.R_THUMBR if r.resident else vid.R_THUMB)
        if ch is None:
            hdr = (0, 0, 0)
            say("   rendition %d: no desktop posters" % ri)
        else:
            th = dict(scale=ch[0], flags=ch[1], make=make, custom=cpk)
            at = None
            if old is not None and old[0]:
                tat, tn = old[0], old[1]
                end = max([tat + struct.calcsize(vid.TH_ENT) * tn] +
                          [o + n for o, n, _, _ in (
                              struct.unpack_from(vid.TH_ENT, d, tat + struct.
                                                 calcsize(vid.TH_ENT) * i)
                              for i in range(tn))])
                room = end + (-end % vid.SECTOR) - tat
                blk, hdr = vid.thumb_block(th, keys, tat, pw, ph)
                if len(blk) <= room:
                    d[tat:tat + room] = blk + bytes(room - len(blk))
                    at = tat
            if at is None:
                if r.lsp0 and not r.lktab:
                    raise vid.V88Error(
                        "rendition %d: the new posters do not fit the old "
                        "ones' sectors, and a layer with no keys runs to the "
                        "file's end, so nothing may go after it" % ri)
                blk, hdr = vid.thumb_block(th, keys, tail, pw, ph)
                d[len(d):] = bytes(tail - len(d)) + blk
                tail = len(d) + (-len(d) % vid.SECTOR)
                at = hdr[0]
            n = (len(keys) + (1 if cpk else 0)) * len(kinds)
            total += n
            say("   rendition %d: %d desktop poster(s) %s at %s, %d bytes "
                "%s" % (ri, n, " and ".join(
                    {"c": "in colour", "m": "in black and white"}[k]
                    for k in kinds), {1: "its own size", 2: "half",
                                      4: "a quarter"}[ch[0]], len(blk),
                    "in place of the old" if old is not None and
                    at == old[0] else "after the file's end"))
        struct.pack_into(vid.THUMB_FMT, d, slot, *hdr)
        if not r.resident:              # (the first layout's twelve bytes:
            d[r.slot + 46:r.slot + 52] = bytes(6)   # gone, 98.1.3.11)
    vid._reshape_options(d, r0, dict(desktop_posters=mode, desktop_size=size,
                                     desktop_clip=dclip,
                                     poster_image=image or None))
    tmp = a.out + ".tmp"
    with open(tmp, "wb") as f:
        f.write(d)
    try:                                # (every poster read back, every
        for ri in range(r0.nrend):      # rendition's stream still whole)
            r = vid.Reader(tmp, ri)
            if r.thumb is not None:
                for i in range(r.thumb[1]):
                    r.thumb_pic(i)
            for _ in r.records():
                pass
    except vid.V88Error:
        os.remove(tmp)
        raise
    os.replace(tmp, a.out)
    return total


def page_key0(path, res):
    """A page-ahead file's KEY 0, as (frame, bytes), when the player cannot
    read it - left out of the table, or past KEY_PLAYER - else None"""
    if not res.get("keys"):
        kd = res.get("kdropped") or [(0, 0)]
        return kd[0]
    k = vid.Reader(path).kmax
    return (vid.Reader(path).key(0)[0], k) if k > KEY_PLAYER else None


def page_hurt(plain, page):
    """The builds that made their own stretch worse (98.2.1.8.2): a build's
    stretch runs from the first frame that spent toward its target to the
    first that spent toward the next - its flip, and whatever the bytes and
    cycles it took cost the frames after - and it hurt where the error as
    seen, summed over the stretch, is more than the plain encode's"""
    q1, q2 = plain.get("q_frames"), page.get("q_frames")
    st = (page.get("page") or {}).get("start") or {}
    if not q1 or not q2 or len(q1) != len(q2):
        return []
    s = sorted((j, F) for F, j in st.items())
    out = []
    for i, (j, F) in enumerate(s):
        e = s[i + 1][0] if i + 1 < len(s) else len(q1)
        if sum(q2[j:e]) > sum(q1[j:e]) + 1e-12:
            out.append(F)
    return out


PAGE_PICTURE = (("q_vis", "the error as seen"),
                ("q_worst", "the worst second"),
                ("q_broken", "the seconds visibly broken"),
                ("q_seen", "the view as seen"),
                ("q_seen_broken", "the seconds a tenth of the view broken"))
PAGE_MEASURES = PAGE_PICTURE + (("cutf", "the frames cut"),)


def page_compare(p, b, measures=PAGE_MEASURES):
    """([what page-ahead made worse], [what it made better]): its picture
    against the plain encode's - or, with the picture's three measures
    alone, a pruned pass against the pass it pruned (98.2.1.8.2)"""
    worse, better = [], []
    for k, name in measures:
        if k not in p or k not in b:
            continue
        if p[k] > b[k] + 1e-9:
            worse.append("%s (%.3g against %.3g)" % (name, p[k], b[k]))
        elif p[k] < b[k] - 1e-9:
            better.append("%s (%.3g against %.3g)" % (name, p[k], b[k]))
    return worse, better


# WHICH FILE PAGE-AHEAD SHIPS (98.2.1.8.1, the owner, 2026-10-17: "with
# the ladder, no matter how I configure it, it always drops page ahead"):
# THE VIEW AS SEEN decides - the measure VIDEO-SEEN-PLAN 1 built to read as
# the eye does, which the ladder and the frame rate already choose by - and
# the seconds it is broken by a tenth or more, and the seconds visibly
# broken, may be no worse by more than a frame: what the view gains by
# breaking for longer is not kept. The whole screen's means (the error as
# seen, its worst second) and the frames cut are said and do not decide:
# better somewhere and worse NOWHERE, the rule before, dropped a page-ahead
# file whose view was a third less broken because one of six came out a
# hair worse - and on a picture busy enough to cut, one nearly always does
PAGE_DECIDES = "q_seen"
PAGE_GUARDS = ("q_seen_broken", "q_broken")


def page_verdict(p, b):
    """(ship `p` rather than `b`?, [worse], [better], why not): the view as
    seen decides, the guards may be no worse by more than one frame's time,
    and the rest of page_compare's measures are reported (PAGE_DECIDES)"""
    worse, better = page_compare(p, b, PAGE_MEASURES)
    if PAGE_DECIDES not in p or PAGE_DECIDES not in b:
        return not worse and bool(better), worse, better, \
            "it bettered nothing" if not worse else "it made %s worse" % \
            ", ".join(worse)
    frame = 1.0 / (p.get("fps") or b.get("fps") or 24.0)
    names = dict(PAGE_MEASURES)
    if p[PAGE_DECIDES] >= b[PAGE_DECIDES] - 1e-9:
        return False, worse, better, "%s %s (%.3g against %.3g)" % (
            names[PAGE_DECIDES], "no better" if abs(
                p[PAGE_DECIDES] - b[PAGE_DECIDES]) <= 1e-9 else "worse",
            p[PAGE_DECIDES], b[PAGE_DECIDES])
    for k in PAGE_GUARDS:
        if k in p and k in b and p[k] > b[k] + frame + 1e-9:
            return False, worse, better, "%s worse by more than a frame " \
                "(%.3g s against %.3g)" % (names[k], p[k], b[k])
    return True, worse, better, None


def pass_tick(tick, label):
    """A PASS begins: `label` says which and why (98.2.11) - "pass", its
    note the label - and its steps are then reported as they happen, read,
    sound and encode each with its own count: until 2026-10-17 a first
    pass was folded into thousandths of one step, "first", which said a
    percent and not what the encoder was doing"""
    tick("pass", 0, 0, label)
    return tick


def layer_first_pass(a, tick, readers):
    """--layer-passes two (98.2.1.3.3): the encode run once with the layer
    TRACKED and not made - the base exactly as the real pass makes it, the
    better machine's disk under it. -> (need, trace): the bank that
    machine keeps for the base, KB in whole 32 KB slots, priced with the
    bank's copies charged throughout; and the base's whole future, each
    frame's bytes and decode, which the second pass holds the layer to
    (LayerEnc._reqs). It says so as it begins (pass_tick)"""
    import copy
    import tempfile
    b = copy.copy(a)
    b._lypass1, b.quiet, b.preview_png, b.aim = True, True, None, "asked"
    t1 = pass_tick(tick, "the layer's first pass: what the base must keep "
                   "for it")
    fd, b.out = tempfile.mkstemp(suffix=".v88")
    os.close(fd)
    try:
        res = _encode(b, None, t1, readers)
    finally:
        try:
            os.remove(b.out)
        except OSError:
            pass
    ly = res.get("layer") or {}
    return ly.get("need", 0), ly.get("trace"), res


def layer_passes(a, keep, tick, readers):
    """--layer-passes two (98.2.1.3.3): the first pass for the need and the
    base's future, the real pass made knowing both - and, should the base
    still run short under the layer, up to LAYER_AGAIN more, each keeping
    the base what it ran short by (a slot at least). Each is said; on the
    measured clips none has been needed"""
    import copy
    a = copy.copy(a)                    # (one scene pass for every pass,
    if getattr(a, "_scenes", None) is None:     # 98.1.3.2.3.1: the copies
        a._scenes = {}                  # share it - page-ahead's too)
    need, trace, res1 = layer_first_pass(a, tick, readers)
    if not getattr(a, "_ladok", False):     # (page-ahead's passes decided
        steps = ladder_decide(a, res1, tick) if ladder_on(a) else {}
        a._ladok, a._steps = True, steps    # it already: 98.2.1.8.3)
    b = copy.copy(a)
    b._lyneed, b._lytrace = need, trace
    pass_tick(tick, "the layer's second pass, %d KB kept for it" % need)
    res = _encode(b, keep, tick, readers)
    for _ in range(LAYER_AGAIN):
        ly = res.get("layer") or {}
        if not ly.get("short"):
            break
        more = need + max(32, ly.get("dry", 0))
        if not a.quiet:
            print("   ...the base's bank ran %d KB short under the layer with "
                  "%d KB kept for it: once more, keeping %d"
                  % (ly.get("dry", 0), need, more))
        need = more
        b = copy.copy(a)
        b._lyneed, b._lytrace, b._lyagain = need, trace, True
        if keep is not None:
            del keep[:]
        pass_tick(tick, "the layer's pass again, %d KB kept for it" % need)
        res = _encode(b, keep, tick, readers)
    return res


# THE RATE LADDER (--step-down, VIDEO-PALETTE-PLAN 10.12): a scene the
# first pass found VISIBLY BROKEN steps down the owner's ladder - its own 12
# colours, then, where most of it broke, its own 8 - before the real pass
# cuts what is left. A step is taken only where it asks for less (8 is
# dropped where 12 already asked less, the owner's rule) and only where the
# scene keeps its saturated colours (8 dropped the headlights' red)
LADDER_BROKEN = 3       # frames visibly broken before a scene steps at all
LADDER_SEVERE = 0.3     # ...the share of its frames past which 8 is tried
LADDER_GUARD = 15.0     # CIELAB dE: each saturated colour the scene uses
                        # keeps an entry this near after the step
LADDER_FRAMES = 90      # a scene's frames its demand is priced over


def ladder_on(a):
    """--step-down on (the default since 2026-10-14) and a palette a scene
    for it to step: a file without scenes has nothing to step and is made
    as before, so the default costs it nothing"""
    if degrade_rungs(a):                 # (--ladder: its FEWER COLOURS rung)
        return any(k == "colours" for k, v in degrade_rungs(a)) and \
            bool(getattr(a, "scene_palettes", False))
    return getattr(a, "step_down", "off") == "on" and \
        bool(getattr(a, "scene_palettes", False))


def plane_stores(vals):
    """Pixel-value frames (VGA4) -> the mean STORES a frame asks for, as
    vga4_subs makes them: at each byte that changes, one store for each
    distinct value its changed planes take - the CPU's unit as much as the
    bytes'"""
    prev, tot, n = None, 0, 0
    for v in vals:
        pl = np.stack([np.packbits((v >> p) & 1, axis=1) for p in range(4)])
        if prev is not None:
            ch = pl != prev
            seen = np.zeros(ch.shape[1:], np.int32)
            for p in range(4):
                new = ch[p].copy()
                for q in range(p):
                    new &= ~(ch[q] & (pl[q] == pl[p]))
                seen += new
            tot += int(seen.sum())
            n += 1
        prev = pl
    return tot / max(1, n)


def ladder_guard(px, big, small):
    """The scene's sample `px` keeps its saturated colours from palette
    `big` in palette `small` (six-bit, dither order): -> the worst dE"""
    import os88cgacomp as cc
    lab = cc.srgb2lab(np.asarray(px, np.float64))
    def entries(p):
        n = len(p) // 3
        return cc.srgb2lab(np.frombuffer(p, np.uint8)[:3 * n].reshape(
            n, 3).astype(np.float64) * (255.0 / 63))
    B = entries(big[:48])
    S = entries(small[:48])
    use = np.bincount(np.argmin(((lab[:, None] - B[None]) ** 2).sum(2), 1),
                      minlength=len(B)) / max(1, len(lab))
    worst = 0.0
    for k in range(len(B)):
        if use[k] >= 0.001 and np.hypot(B[k, 1], B[k, 2]) > 20:
            worst = max(worst, float(np.sqrt(((S - B[k]) ** 2).sum(1)).min()))
    return worst


def ladder_steps(a, res, tick=None):
    """-> ({scene: colours}, [report lines]): the ladder's choice from a
    first pass's scenes (res["shots"], res["scenegeom"]) and the scene
    samples the passes share (a._scenes)"""
    shots, g = res.get("shots"), res.get("scenegeom")
    sc = getattr(a, "_scenes", None) or {}
    if not shots or not g or "scn" not in sc:
        return {}, ["   step-down: no scenes to step (--scene-palettes)"]
    pn = palette_kind(a.palette)[1]
    least = int(getattr(a, "step_min", 8) or 8)     # (--step-min)
    if degrade_rungs(a):                 # (--ladder: its rung's colours)
        least = min([v for k, v in degrade_rungs(a) if k == "colours"] or
                    [least])
    rungs = [n for n in (12, 8) if least <= n < pn]
    jobs = getattr(a, "jobs", None) or os.cpu_count() or 1
    fps = g["rate"] / float(g["spf"])
    t00 = a.start or 0.0
    steps, lines = {}, []
    todo = [i for i, sh in enumerate(shots)
            if sh["broken"] >= LADDER_BROKEN and rungs]
    for i, sh in enumerate(shots):
        if sh["broken"] < LADDER_BROKEN or not rungs:
            continue
        if tick:                # (a broken scene: its sample in each count)
            tick("stepdown", todo.index(i), len(todo),
                 "%d broken scenes, each dithered in %s colours" % (
                     len(todo), " and ".join(str(n) for n in [pn] + rungs)))
        s0 = g["src0"][i]
        t0 = t00 + s0 / fps
        t1 = t0 + LADDER_FRAMES / fps
        if i + 1 < len(g["src0"]):
            t1 = min(t1, t00 + g["src0"][i + 1] / fps)
        frames = list(ffmpeg_video(a.src, g["lw"], g["lh"], g["crop"],
                                   "%d/%d" % (g["rate"], g["spf"]), t0, t1,
                                   g["eq"], pix="rgb24"))[:LADDER_FRAMES]
        px = sc["scn"][i][1]
        pals = {pn: sc["pals"][i][1]}
        dem, guard = {}, {}
        for n in [pn] + rungs:
            if n not in pals:
                pals[n] = colour_kmeans(px, n)
            codes = np.array(vid.plane_code(n), np.uint8)
            d = KnollDitherer(g["lw"], g["lh"], pals[n], a.vga4_stable,
                              g["clip"], a.vga4_hold, a.vga4_hold_score,
                              a.vga4_plan_cap, g["sparse"])
            dem[n] = plane_stores([codes[v] for v in dither_all(
                Staged(d), frames, jobs)])      # (raw on every core)
            guard[n] = 0.0 if n == pn else ladder_guard(px, pals[pn],
                                                        pals[n])
        # each rung on its own: 12 where it asks less and keeps the
        # colours; 8 where most of the scene broke, it asks less than what
        # was chosen (so 8 is dropped where 12 asked less - the owner's
        # rule) and it keeps the colours too
        n_ch, why = pn, []
        severe = sh["broken"] >= LADDER_SEVERE * sh["frames"]
        for n in rungs:
            if n == 8 and not severe:
                break
            if guard[n] > LADDER_GUARD:
                why.append("%d loses a colour (%.0f dE)" % (n, guard[n]))
                continue
            if dem[n] >= dem[n_ch]:
                why.append("%d asks no less than %d" % (n, n_ch))
                continue
            n_ch = n
        f = sh["f"]
        lines.append("   step-down: %d:%04.1f (%d frames, %d broken): %s "
                     "(stores a frame %s)%s" % (
                         f / fps // 60, f / fps % 60, sh["frames"],
                         sh["broken"], "%d -> %d" % (pn, n_ch)
                         if n_ch != pn else "stays %d" % pn,
                         ", ".join("%d: %.0f" % (n, dem[n])
                                   for n in [pn] + rungs),
                         "; " + "; ".join(why) if why else ""))
        if n_ch != pn:
            steps[i] = n_ch
    if tick and todo:
        tick("stepdown", len(todo), len(todo))
    if not lines:
        lines.append("   step-down: no scene broke - nothing to step")
    return steps, lines


def ladder_decide(a, res, tick=None):
    steps, lines = ladder_steps(a, res, tick)
    if not a.quiet:
        for x in lines:
            print(x)
    return steps


def ladder_passes(a, keep, tick, readers):
    """--step-down with no other pass of its own (98.2.1.9): the encode once
    as it would be made, whose scenes the ladder reads - and, if it steps
    any, the real pass with those steps. A first pass is SHARED: page-ahead
    and the layer run one of their own and the ladder reads theirs"""
    import contextlib
    import copy
    import shutil
    import tempfile
    a = copy.copy(a)
    a._scenes = {}                      # (one scene pass for every pass)
    b = copy.copy(a)
    b._ladok, b.aim = True, "asked"
    fd, b.out = tempfile.mkstemp(suffix=".v88")
    os.close(fd)
    h1 = _Held(sys.stdout)
    try:
        with contextlib.redirect_stdout(h1):
            res1 = _encode(b, keep, pass_tick(
                tick, "the first pass: the plain encode, whose scenes the "
                "ladder reads"), readers)
        steps = ladder_decide(a, res1, tick)
        if not steps:
            shutil.move(b.out, a.out)
            sys.stdout.write(h1.text())
            return res1
        c = copy.copy(a)
        c._ladok, c._steps = True, steps
        if keep is not None:
            del keep[:]
        pass_tick(tick, "the pass again, %d scene%s in fewer colours" % (
            len(steps), "" if len(steps) == 1 else "s"))
        return _encode(c, keep, tick, readers)
    finally:
        try:
            os.remove(b.out)
        except OSError:
            pass


def _encode(a, keep, tick, readers):
    tick("prepare", 0, 0)
    if getattr(a, "spk_shape", SPK_ENC) == SPK_MACH:
        raise vid.V88Error(
            "--spk-shape machine is a speaker WAV's (86.21.1): a plain WAV "
            "for Audio to shape. A .V88 takes encoder or none - or --audio "
            "pcm8, which the Video Player shapes for the speaker itself")
    if a.live and getattr(a, "desktop_size", "fit") != "fit":
        raise vid.V88Error("--desktop-size %s with --live: a Live file "
                           "plays in its window at its own size"
                           % a.desktop_size)
    need_tools()
    prof = dict(PROFILES[a.profile])
    # what the profile implies (98.2.3.5), where the command line said
    # nothing: a Live or RESIDENT file keeps 32 KB, the only cap it takes
    pdef = {} if a.live else prof.get("defaults", {})
    if a.frame_cap is None:
        a.frame_cap = "32" if a.live or a.resident else \
            pdef.get("frame_cap", "32")
    if a.live and prof["avg"] is not None:
        prof["avg"] = LIVE_AVG          # (the decode and the blit: 98.2.7)
    for k in ("disk", "avg", "peak", "owe", "reserve"):
        if getattr(a, k) is not None and getattr(a, k) != "auto":
            prof[k] = getattr(a, k)
    # --avg auto (98.2.1.5): after --peak, which it follows, and sticky - a
    # trial encode (--aim) is handed this namespace again - and the record
    # holds the share it came to, not the word. A Live file's share is its
    # blit's and the desktop's as well (98.2.7), so it keeps LIVE_AVG
    a.avg_auto = getattr(a, "avg_auto", False) or a.avg == "auto"
    if a.avg == "auto":
        a.avg = prof["avg"] = prof["avg"] if a.live else avg_auto(prof)
    if a.memory is not None and prof["disk"] is not None and \
            not a.resident and not a.live:
        # FREE MEMORY TO PLAY (98.2.1.3.1): the ring it holds, and the
        # reserve that ring banks
        if a.reserve is not None:
            raise vid.V88Error("--memory and --reserve both size the "
                               "player's ring: give one")
        msl = 2 if FRAME_CAPS[a.frame_cap] > vid.SP_MAX else 1
        snd = (a.audio or prof["audio"]) != "none"
        k = ring_for_memory(a.memory, msl, snd)
        if not 3 <= k <= vid.RING_MAX:
            raise vid.V88Error(
                "--memory %d KB: a ring of %d slots, and the player takes 3 "
                "to %d - %d to %d KB here"
                % (a.memory, k, vid.RING_MAX, play_memory_kb(3, msl, snd),
                   play_memory_kb(vid.RING_MAX, msl, snd)))
        prof["reserve"] = (k - 2) * vid.SLOT / 1024.0
    lay, bw, bh = PRESETS[a.preset] if a.preset else (a.layout, None, None)
    if a.live:
        # LIVE (98.2.7): one bit on LIN80, the band OSAPI_GFX_BLIT1 takes,
        # at the named screen's pixel shape and the logo's box - or on VGA
        # the sixteen colours' four planes, which GFX_BLITP takes (98.3.10.4)
        if a.pixfmt not in (None, "mono") and \
                (a.pixfmt, a.live) != ("vga4", "vga"):
            raise vid.V88Error("--live is one bit, or --pixfmt vga4 with "
                               "--live vga: no --pixfmt %s" % a.pixfmt)
        # ...RESIDENT - or, with --xms, a STREAM that plays Live once the
        # player holds it in XMS (98.3.18.1)
        a.pixfmt = a.pixfmt or "mono"
        a.resident = not getattr(a, "xms", False)
        lay = "lin80"
        bw, bh = LIVE_BOX[a.live]
    if a.pixfmt is None and a.preset in PRESET_PIXFMT:
        # a CGA or VGA colour preset IS its format - its layout alone would
        # make a one-bit file of the same size
        a.pixfmt = PRESET_PIXFMT[a.preset]
    if a.layout:
        lay = a.layout
    if a.box:
        bw, bh = (int(v) for v in a.box.lower().split("x"))
    if bw is None:
        raise vid.V88Error("--box or --preset names the canvas")
    L = vid.LAYOUT_BY_NAME[lay]
    banks, stride, rows, _ = vid.LAYOUTS[L]
    cga4 = a.pixfmt == "cga4"
    c160 = a.pixfmt == "c160"
    c512 = a.pixfmt == "c512"
    text = a.pixfmt == "text"
    if text != (L == vid.LAY_TEXT):
        raise vid.V88Error("--pixfmt text is the text-80x25 layout's, and it takes "
                           "nothing else")
    if cga4 and L != vid.LAY_CGA:
        raise vid.V88Error("--pixfmt cga4 is mode 4's: the cga layout")
    if c160 != (L == vid.LAY_C160):
        raise vid.V88Error("--pixfmt c160 is the c160 layout's, and it takes "
                           "nothing else")
    if c512 != (L == vid.LAY_TXT):
        raise vid.V88Error("--pixfmt c512 is the text-80x100 layout's, and it takes "
                           "nothing else")
    # A SCREEN OF ITS OWN (98.1.3.2.1, 98.2.5.1): VGA4 on 320 x 200, 320 x
    # 240, 640 x 350 or 640 x 400 - the box cut to the screen, the canvas
    # at the screen's pixel shape, full screen only
    # (Mode X's 320x240 is its own screen, the default: not a screen of
    # its own as VGA4's 320x240 is - the owner, 2026-10-17)
    scr = vid.SCREEN_BY_NAME[a.screen or "640x480"] \
        if not (L == vid.LAY_MODEX and a.screen == "320x240") else 0
    if scr:
        # ...or MODE X AT 320 x 200 (98.1.3.1.1): 13h's 400 lines, four
        # pages where 320 x 240 has three, at 13h's pixel shape
        mx200 = L == vid.LAY_MODEX and scr == vid.SCR_320X200
        if (a.pixfmt != "vga4" or L != vid.LAY_LIN80) and not mx200 or \
                a.live or a.resident:
            raise vid.V88Error(       # (a resident file has no screen byte,
                "--screen %s is 16-colour VGA's (--pixfmt vga4 on lin80, not "
                "Live or resident), or 320x200 Mode X's; for black and "
                "white, --pixfmt vga4 --palette grey2 (Mode X's own is "
                "320x240)" % a.screen)
        bw = min(bw, vid.SCREENS[scr][1])
        bh = min(bh, vid.SCREENS[scr][2])
    # a C512 or a TEXT box is in CELLS, two bytes each (98.1.3.5)
    ppb = 4 if cga4 else 0.5 if c512 or text else vid.PIX_PER_BYTE[L]
    if bw > stride * ppb or bh > rows:
        raise vid.V88Error("a %d x %d box does not fit %s (%d x %d)"
                           % (bw, bh, lay, stride * ppb, rows))
    sw, sh, dar, sfps, dur, has_audio = probe(a.src)
    # HOW IT WAS MADE (98.2.17), resolved now, before anything below fills
    # in a.* for itself: the file carries it for the encoder's window
    # THE SOLID-COLOURS RULE WHERE IT BELONGS (VIDEO-PALETTE-PLAN 10.11):
    # --clip 16 stops an even grid of dots where a palette's colours are
    # far apart - the EGA's sixteen, the greys of a ramp, CGA's - and on a
    # clip's own sixteen, which are near nearly everything, it flattened
    # gradients into bands. There the grid is removed where it is made
    # (--vga4-sparse) and the dither keeps its texture, for FEWER bytes
    # The record keeps what was ASKED - None is this format's default, as
    # the palette and format it also records say - so it re-encodes alike
    own16 = a.pixfmt == "vga4" and palette_kind(a.palette)[0] == "colour"
    kclip = a.clip if getattr(a, "clip", None) is not None else \
        0.0 if own16 else 16.0
    ksparse = a.vga4_sparse if getattr(a, "vga4_sparse", None) is not None \
        else 2 if own16 else 0
    optsblk = vid.pack_options(options_doc(a, sfps))
    pasp = vid.CGA4_ASPECT if cga4 else LIVE_ASPECT[a.live] if a.live \
        else vid.SCREENS[scr][3] if scr else None
    w, h, crop = canvas_size(lay, bw, bh, dar, a.fit, pasp)
    vga8 = L in vid.VGA8_LAYOUTS
    # THE DETAIL (98.2.4): the picture made at a W-th of the width and an
    # H-th of the height, each pixel repeated W times along its row and each
    # row shown H times by the VGA itself (the file's row scale) - the same
    # screen, a fraction of the bytes
    detail = a.detail or (pdef.get("detail") if vga8 else None) or \
        PRESET_DEFAULTS.get(None if a.live else a.preset, {}).get(
            "detail", "1x1")
    dw, dh = (int(v) for v in detail.lower().split("x"))
    if (dw, dh) != (1, 1) and not vga8:
        raise vid.V88Error("--detail is VGA8's (a one-bit pixel has nothing "
                           "to repeat into)")
    if dw not in (1, 2, 4) or dh not in (1, 2):
        raise vid.V88Error("--detail %s: the width 1, 2 or 4, the height "
                           "1 or 2" % detail)
    h -= h % dh
    vga4 = a.pixfmt == "vga4"
    if a.scene_palettes and (not vga4 or a.live or a.resident or
                             palette_kind(a.palette)[0] != "colour"):
        raise vid.V88Error(     # (with --palette auto or greyN it was left
            "--scene-palettes is a full-screen 16-colour "   # out unsaid)
            "file's with --palette colourN (--pixfmt vga4 --palette "
            "colour16), not Live or resident (98.1.3.2.3)")
    if a.scene_hold and not a.scene_palettes:
        raise vid.V88Error("--scene-hold holds the dither across a scene's "
                           "palette change, and takes --scene-palettes")
    if a.vga4_order != "dark" and (not vga4 or
                                   palette_kind(a.palette)[0] == "auto"):
        raise vid.V88Error("--vga4-order orders a 16-colour VGA file's own "
                           "sixteen (--pixfmt vga4 --palette colour16)")
    if palette_kind(a.palette)[0] != "auto" and not (vga8 or vga4):
        raise vid.V88Error("--palette %s is VGA colour's (--pixfmt vga8 or "
                           "vga4)" % a.palette)
    if vga4 and L != vid.LAY_LIN80:
        raise vid.V88Error("--pixfmt vga4 is mode 12h's: the lin80 layout")
    if vga4 and (dw, dh) != (1, 1):
        raise vid.V88Error("--detail is VGA8's")
    g = vid.Geom(L, int(w // ppb), h // dh, bitplanes=vga4,   # (a NARROW
                 stride=vid.screen_stride(scr) if vga4 else None)  # screen)
    g.sub2 = g.planes > 1           # SUB2 (98.1.3.2.4): every planar file
    if a.pixfmt is not None and (a.pixfmt == "vga8") != vga8:
        raise vid.V88Error("--pixfmt vga8 is the lin320 and modex layouts', "
                           "and the only format they take")

    # 256 colours are a byte a PIXEL: at 30 fps a moving camera wants ~500
    # KB/s where 15 wants ~250 (VIDEO-PLAN W11), so VGA8 defaults to 15 -
    # and its two presets to 25, at half the width (98.2.10)
    fps = a.fps or min(PRESET_DEFAULTS.get(
        None if a.live else a.preset, {}).get(
        "fps", 15.0 if vga8 else 30.0), sfps)
    pd = PRESET_DEFAULTS.get(None if a.live else a.preset, {})
    audio = a.audio or pd.get("audio") or prof["audio"]
    spk = audio == "speaker"
    if spk and (getattr(a, "bank", None) or getattr(a, "layer_disk", None)
                or getattr(a, "layer_profile", None) or a.xms):
        # NOTHING IN XMS UNDER THE SPEAKER (98.3.18.5): its clock is one
        # interrupt a sample and every XMS copy holds them off, so the player
        # reads the disk instead - a bank or a layer budgeted here would be
        # disk the play never has
        raise vid.V88Error("--audio speaker: no --bank, --layer-* or --xms - "
                           "the player makes no XMS copy under the speaker's "
                           "clock (SPEC.md 98.3.18.5), so the file would be "
                           "made for disk it never gets")
    if spk:                             # PCM8, at a rate the speaker plays
        audio = "pcm8"
        r = a.rate or pd.get("rate") or SPK_RATE
        top = SPK_MAX_AT if prof.get("spk_us") else 16124
        if not SPK_MIN <= r <= top:
            raise vid.V88Error("--audio speaker: %d Hz is not a rate the PWM "
                               "plays on this profile (%d..%d%s)" % (
                                   r, SPK_MIN, top, "" if top > 16124 else
                                   "; a 286 profile goes to %d" % SPK_MAX_AT))
        if prof["avg"] is not None and prof.get("speed") == 1 \
                and r > SPK_MAX_8088:
            raise vid.V88Error("--audio speaker: an 8088 plays at most %d Hz "
                               "through the speaker - VIDEO.O88 mutes %d "
                               "(SPEC.md 98.3.15)" % (SPK_MAX_8088, r))
        a.rate = r
        if a.spk_pulses > 1 and a.resident:
            raise vid.V88Error("--spk-pulses: a streamed file's, not a "
                               "resident one's (34.11.7)")
        vid.spk_table(r, a.spk_pulses,  # (the pulses divide N, and a
                      fast=bool(prof.get("spk_us")))  # pulse is 74..255
    if audio == "none" or not has_audio:  # counts, 48..255 on a 286)
        afmt, rate, spf, abytes = vid.AUD_NONE, round(fps * 100), 100, 0
    else:
        afmt = vid.AUD_BY_NAME[audio]
        rate = a.rate or prof["rate"]
        spf = max(1, round(rate / fps))
        if afmt == vid.AUD_ADPCM4:
            spf += spf % 2
        abytes = spf // 2 if afmt == vid.AUD_ADPCM4 else spf
    fps = rate / spf
    if a.resident:
        prof["disk"] = None if prof["avg"] is None else 10 ** 9
        prof.pop("disk_at", None)       # (no disk: nothing slows it)
    audio_bps = abytes * fps
    audio_cyc = CYC_AUDIO * abytes if afmt else 0.0
    spk_share = 0.0
    if spk and afmt and prof["avg"] is not None:
        # THE SPEAKER TAKES ITS SHARE OF EVERYTHING (98.2.15): the pulses are
        # interrupts on top of the whole machine - decode, the disk's calls,
        # the loop - so the profile's shares are of what is LEFT
        if prof.get("spk_us"):          # A 286 PROFILE: its own time a
            w_us, h_us = prof["spk_us"]     # pulse (34.11.8, predicted) -
            spk_share = rate * (w_us + (a.spk_pulses - 1) * h_us) / 1e6
        else:                           # an interrupt is not 3x an 8088's
            spk_share = rate * ((CYC_SPK_PULSE if a.spk_pulses == 1 else
                                 CYC_SPK_WHOLE2 + CYC_SPK_HALF) +
                                CYC_SPK_BYTE) / (
                vid.HZ * (prof.get("speed") or 1))
        if spk_share >= 0.8:
            raise vid.V88Error("--audio speaker: %d Hz%s takes %.0f%% of this "
                               "machine and leaves too little to draw with%s"
                               % (rate, " x %d pulses" % a.spk_pulses
                                  if a.spk_pulses > 1 else "",
                                  100 * spk_share,
                                  " - --rate 8000 raises the whine to 8 kHz "
                                  "for ~70%" if a.spk_pulses > 1 else ""))
        prof["avg"] *= 1 - spk_share
        prof["peak"] *= 1 - spk_share
        if prof.get("owe"):             # ...and of an owed frame's periods
            prof["owe"] *= 1 - spk_share    # (98.2.1.1)
        audio_cyc = 0.0                 # (charged above, as the share)

    eq = []
    if a.gamma != 1.0:
        eq.append("gamma=%g" % a.gamma)
    if a.contrast != 1.0:
        eq.append("contrast=%g" % a.contrast)
    if a.brightness:
        eq.append("brightness=%g" % a.brightness)
    comp = a.pixfmt == "cgacomp"
    if comp and lay != "cga":
        raise vid.V88Error("composite colour is a CGA's: --pixfmt cgacomp "
                           "needs the cga layout")
    pattern = comp and a.comp_dither == "pattern"
    lw, lh = w // dw, h // dh           # what is made, before the repeat
    tcol = None
    if text:
        tcol = a.text_colour or PRESET_DEFAULTS.get(a.preset, {}).get(
            "text_colour", "colour")
    # A PASS BEFORE THE FRAMES - the levels', or the palette's - reads the
    # clip through once more, and the frames are read beside it
    levels = a.levels == "auto" and not (vga8 or vga4 or cga4 or c160 or
                                        c512 or (text and tcol == "colour"))
    jobs = a.jobs or os.cpu_count() or 1
    # STREAMED (98.2.1): the frames go to the encoder as they are dithered,
    # so the dither's cores work beside the encoder's one - unless the
    # sound needs the frame count first (ADPCM4's search, its keyframes
    # counted to the end) or the dither comes in chunks (cgacomp's)
    stream = afmt != vid.AUD_ADPCM4 and not (comp and not pattern and
                                              jobs > 1)
    def source():                       # (TEXT: eight by eight a cell)
        return ffmpeg_video(
            a.src, w // 4 if pattern else lw * 8 if text else lw,
            lh * 8 if text else lh, crop, "%d/%d" % (rate, spf), a.start,
            a.end, ":".join(eq),
            "rgb24" if comp or vga8 or vga4 or cga4 or c160 or c512 or text
            else "gray", ahead=AHEAD if levels or vga8 or stream else 0)
    frames = source()
    # the frame count to expect, for the progress: the clip's length at the
    # file's rate (ffmpeg's own count can differ by a frame or two)
    est = max(1, round(((a.end or dur or 0) - (a.start or 0)) * fps)) \
        if (a.end or dur) else 0

    def counted(it):
        for n, f in enumerate(it, 1):
            if not stream:          # (streamed: the encode says it)
                tick("read", n, est)
            yield f

    def tally(it, step):
        """`it`'s frames, `step` told each one (a whole-clip pass before
        the encode: the order's, the cells' scene changes)"""
        for n, f in enumerate(it, 1):
            tick(step, n, est)
            yield f
    readers.append(frames)
    frames = counted(frames)
    say = (lambda *x: None) if a.quiet else print
    say("%s: %dx%d %.3f fps -> %s canvas %d x %d, %.3f fps (%d Hz / %d), "
        "audio %s, profile %s"
        % (a.src, sw, sh, sfps, lay, w, h, fps, rate, spf,
           {0: "none", 1: "PCM8", 2: "ADPCM4"}[afmt], a.profile))
    if spk_share:
        say("   the SPEAKER: %.0f%% of the machine at %d Hz, so decode gets "
            "%.0f%% of a period on average, %.0f%% at most"
            % (100 * spk_share, rate, 100 * prof["avg"], 100 * prof["peak"]))
    if levels:
        lo, hi = _once(a, ("levels", w, h, crop, a.start, a.end,
                           ":".join(eq)), lambda: (tick(
                               "levels", 0, 0, "a frame a second of the "
                               "clip: no count"), auto_levels(
            a.src, w, h, crop, a.start, a.end, ":".join(eq)))[1])
        say("   levels: grey %d..%d stretched to 0..255" % (lo, hi))
        frames = (stretch(f, lo, hi) for f in frames)
    palette = None
    scene_pals = None               # [(source frame, 768 bytes)], 98.1.3.2.3
    cgapal = None
    if cga4:
        frames = list(frames)
        cgapal = cga4_pick(frames, a.cga_bg, a.cga_palette, a.cga_bright)
        cols = vid.cga4_colours(cgapal)
        say("   CGA4 palette %02Xh: colours %s" % (
            cgapal, " ".join(str(c) for c in cols)))
        pal6 = b"".join(vid.STD16[3 * c:3 * c + 3] for c in cols)
        k4 = KnollDitherer(lw, lh, pal6, a.vga4_stable, kclip,
                           a.vga4_hold, a.vga4_hold_score)
        dith = Staged(k4, post=lambda i: pack_pixels(i, 4))
    elif c160:
        k16 = KnollDitherer(lw, lh, vid.STD16, a.vga4_stable, kclip,
                            a.vga4_hold, a.vga4_hold_score)
        dith = Staged(k16, post=lambda i: pack_pixels(i, 2))
    elif text:
        cgapal = vid.TEXT_COLOUR if tcol == "colour" else vid.TEXT_MONO
        tm = TextMatcher(lw, lh, cgapal, a.text_glyphs, a.text_stable,
                         a.text_sharpen, a.text_detail, a.text_busy,
                         top=16 if cgapal else 32,
                         prefer=a.text_prefer_colour, ocr_exact=a.text_ocr,
                         ocr_large=a.text_ocr_large)
        dith = Staged(tm)
        if a.text_ocr or a.text_ocr_large:  # a copy of each frame to READ
            ocr = TextOCR(a.text_ocr_conf, a.text_ocr_every, a.jobs)
            big = ffmpeg_video(a.src, lw * OCR_CW, lh * OCR_CH, crop,
                               "%d/%d" % (rate, spf), a.start, a.end,
                               ":".join(eq), "rgb24")
            readers.append(big)
            frames = zip(frames, ocr.words(big))
            dith = Staged(tm, part=lambda fw: fw[0],
                          rest=lambda fw: (fw[1],))
            say("   OCR by tesseract: %s, a frame in %d read, %d at once, "
                "confidence %g" % (
                    " and ".join(x for x, on in (
                        ("cell-sized letters exact", a.text_ocr),
                        ("larger ones crisp", a.text_ocr_large)) if on),
                    ocr.every, ocr.jobs, ocr.conf))
        say("   TEXT in %s, %s glyphs: detail %g, sharpen %g, letters pay "
            "%g, dead band %g%s" % (
                "colour - a CGA, an EGA or a VGA" if cgapal else
                "mono - any adapter", a.text_glyphs, a.text_detail,
                a.text_sharpen, TextMatcher.BUSY.get(
                    a.text_glyphs, TextMatcher.BUSY_DEFAULT)
                if a.text_busy is None else a.text_busy, a.text_stable,
                ", colour preferred %g" % a.text_prefer_colour
                if cgapal else ""))
    elif c512:
        cgapal = vid.CARD_BY_NAME[a.cga_card]
        dith = Staged(C512Ditherer(lw, lh, cgapal, a.c512_dither,
                                   a.c512_stable, a.c512_mix, a.vga4_stable))
        say("   C512 for %s CGA: %s" % (
            {"old": "the OLD", "new": "the NEW", "both": "either"}[
                a.cga_card],
            "a pattern of %d codes" % a.c512_mix if a.c512_mix >= 2 else
            "dither %g, dead band %g" % (a.c512_dither, a.c512_stable)))
    elif vga4:
        pk, pn = palette_kind(a.palette)
        if pk == "auto":
            dith = Staged(KnollDitherer(lw, lh, vid.STD16, a.vga4_stable,
                                        kclip, a.vga4_hold,
                                        a.vga4_hold_score))
        else:
            # ITS OWN SIXTEEN, or fewer (98.2.5.1): a full-screen file's,
            # each colour's bits on a group of planes (vid.PLANE_CODES) -
            # on any screen, 640 x 480 too (98.1.3.2.2), and never Live or
            # resident, which play on the desktop in the desktop's colours
            if pn > 16 or a.live or a.resident:
                raise vid.V88Error(
                    "--palette %s on 16-colour VGA: up to 16, and full "
                    "screen - not with --live or --resident, which play on "
                    "the desktop in its own colours" % a.palette)
            codes = np.array(vid.plane_code(pn), np.uint8)
            scodes = None               # (a scene's own, under --step-down)
            if a.vga4_order != "dark" and (pk == "grey" or pn != 16):
                raise vid.V88Error(
                    "--vga4-order planes orders sixteen colours of the "
                    "clip's own (--palette colour16 or clip16); fewer take "
                    "their plane codes (vid.PLANE_CODES)")
            if pk == "grey":
                cols = [(v, v, v) for v in grey_levels(pn)]
                d4 = GreyDitherer(lw, lh, pn, a.dither, a.stable, kclip)
            elif a.scene_palettes:
                # A PALETTE A SCENE (98.1.3.2.3): the scenes and their
                # colours in one pass first, and a ditherer over each
                if pk != "colour":
                    raise vid.V88Error("--scene-palettes takes --palette "
                                       "colourN")
                # (a layer's passes share one scene pass: 98.1.3.2.3.1)
                sc = getattr(a, "_scenes", None)
                scene_pals = sc.get("pals") if sc is not None else None
                if scene_pals is None:
                    tick("scenes", 0, est)
                    scn = scene_pass(a.src, lw, lh, crop, "%d/%d" % (
                        rate, spf), a.start, a.end, ":".join(eq),
                        cover=a.scene_cover, least=a.scene_min,
                        count=lambda n: tick("scenes", n, est))
                    scn = scene_join(scn, getattr(a, "scene_join", ""),
                                     rate / spf, say)
                    if len(scn) > vid.PEV_MAX:  # (the longest scenes' cuts
                        keep_s = sorted(sorted(     # kept)
                            range(1, len(scn)), key=lambda i:
                            scn[i][0] - scn[i - 1][0])[-(vid.PEV_MAX - 1):])
                        scn = [scn[0]] + [scn[i] for i in keep_s]
                    scene_pals = []
                    for k, (s0, px) in enumerate(scn):
                        tick("colours", k, len(scn),
                             "each scene's own %d" % pn)
                        scene_pals.append((s0, colour_kmeans(px, pn)))
                    tick("colours", len(scn), len(scn),
                         "each scene's own %d" % pn)
                    if sc is not None:
                        sc["pals"] = scene_pals
                        sc["scn"] = scn
                else:
                    scn = sc.get("scn")
                knoll = lambda p: KnollDitherer(
                    lw, lh, p, a.vga4_stable, kclip, a.vga4_hold,
                    a.vga4_hold_score, a.vga4_plan_cap, ksparse)
                if a.vga4_order == "planes" and not (sc or {}).get("ordered"):
                    # THE ORDER (--vga4-order planes): a pass of the dither
                    # first, in darkest-first colours, to see what each
                    # scene's changes are; then the scenes' sixteen put in
                    # their orders, and the encode dithers in those
                    tick("order", 0, est)
                    sigs = plane_orders(enumerate(dither_all(Staged(
                        SceneDitherer([(s0, knoll(p)) for s0, p in
                                       scene_pals], carry=a.scene_hold)),
                        enumerate(tally(source(), "order")), jobs)),
                        [s0 for s0, _ in scene_pals],
                        [p for _, p in scene_pals])
                    scene_pals = [(s0, order_palette(p, g)) for (s0, p), g
                                  in zip(scene_pals, sigs)]
                    if sc is not None:
                        sc["pals"], sc["ordered"] = scene_pals, True
                    say("   order: each scene's sixteen by the planes its "
                        "changes write, %d of %d scenes moved"
                        % (sum(g != list(range(16)) for g in sigs),
                           len(sigs)))
                # THE RATE LADDER's counts (--step-down, VIDEO-PALETTE-PLAN
                # 10.12): a scene the first pass found broken may take fewer
                # colours of its own - chosen from the same sample, so the
                # colours it keeps are the scene's - each its plane codes
                steps = getattr(a, "_steps", None) or {}
                ns = [steps.get(i, pn) for i in range(len(scene_pals))]
                if any(n != pn for n in ns):
                    nst, kst = sum(1 for n in ns if n != pn), [0]

                    def stepped(i):
                        tick("colours", kst[0], nst, "the stepped scenes' "
                             "fewer colours")
                        kst[0] += 1
                        return colour_kmeans(scn[i][1], ns[i])
                    scene_pals = [(s0, p if ns[i] == pn else _once(
                        a, ("kmeans", i, ns[i]), lambda i=i: stepped(i)))
                        for i, (s0, p) in enumerate(scene_pals)]
                    scodes = [np.array(vid.plane_code(n), np.uint8)
                              for n in ns]
                    codes = scodes[0]
                else:
                    scodes = None
                cp = scene_pals[0][1]
                cols = [tuple(cp[3 * k:3 * k + 3]) for k in range(ns[0])]
                d4 = SceneDitherer([(s0, knoll(p)) for s0, p in scene_pals],
                                   carry=a.scene_hold, codes=scodes)

                def coded(p, n, cd):    # the FILE's sixteen are by pixel
                    q = bytearray(768)  # value: each colour at its plane
                    for k in range(n):  # code, the rest black
                        q[3 * int(cd[k]):3 * int(cd[k]) + 3] = \
                            p[3 * k:3 * k + 3]
                    return bytes(q)
                if scodes is not None:
                    scene_pals = [(s0, coded(p, ns[i], scodes[i]))
                                  for i, (s0, p) in enumerate(scene_pals)]
                elif pn < 16:
                    scene_pals = [(s0, coded(p, pn, codes))
                                  for s0, p in scene_pals]
                say("   scenes: %d, each its own %d (98.1.3.2.3)%s"
                    % (len(scene_pals), pn, "; %d stepped down" % sum(
                        1 for n in ns if n != pn) if scodes is not None
                       else ""))
            else:
                cp = _once(a, ("pal4", pk, pn, lw, lh, crop, rate, spf,
                               a.start, a.end, ":".join(eq)), lambda:
                           colour_palette(a.src, lw, lh, crop, "%d/%d" % (
                               rate, spf), a.start, a.end, ":".join(eq), pn,
                               count=lambda n: tick(
                                   "palette", n, est, "the clip's own %d, "
                                   "chosen for colour" % pn))
                           if pk == "colour" else
                           vga8_palette(a.src, lw, lh, crop, "%d/%d" % (
                               rate, spf), a.start, a.end, ":".join(eq),
                               lambda: tick("palette", 0, 0, PALGEN_NOTE),
                               colours=pn))
                knoll = lambda p: KnollDitherer(
                    lw, lh, p, a.vga4_stable, kclip, a.vga4_hold,
                    a.vga4_hold_score, a.vga4_plan_cap, ksparse)
                if a.vga4_order == "planes":
                    def order1(cp=cp):
                        tick("order", 0, est)   # (one scene: no cut)
                        sig = plane_orders(enumerate(dither_all(Staged(
                            knoll(cp)), tally(source(), "order"), jobs)),
                            [0], [cp])[0]
                        return order_palette(cp, sig)
                    cp = _once(a, ("order1", bytes(cp), a.vga4_stable, kclip,
                                   a.vga4_hold, a.vga4_hold_score,
                                   a.vga4_plan_cap, ksparse), order1)
                    say("   order: the sixteen by the planes their changes "
                        "write")
                cols = [tuple(cp[3 * k:3 * k + 3]) for k in range(pn)]
                d4 = knoll(cp)
            pal16 = bytearray(48)
            for k, c in enumerate(cols):
                pal16[3 * int(codes[k]):3 * int(codes[k]) + 3] = bytes(c)
            palette = bytes(pal16) + bytes(vid.PAL_BYTES - 48)
            dith = Staged(d4, post=None if scodes is not None else
                          lambda i: codes[i])
            # (the pixel value each colour is stored at: said only where it
            # is not simply its index - fewer than 16 take spread plane
            # codes, vid.plane_code - since all 16 always read 0 to F)
            say("   palette: %s%s" % (
                "%d greys" % pn if pk == "grey" else
                "the clip's %d colours%s" % (
                    pn, ", chosen for colour" if pk == "colour" else ""),
                "" if [int(c) for c in codes] == list(range(len(codes)))
                else ", pixel values %s" % " ".join("%X" % c
                                                     for c in codes)))
    elif vga8:
        pk, pn = palette_kind(a.palette)
        if pk == "grey":            # A RAMP (98.2.3.7): a level a pixel,
            palette = grey_palette(pn)      # by the one-bit rule made N-level
            d8 = GreyDitherer(lw, lh, pn, a.dither, a.stable, kclip)
            say("   palette: %d greys, %s, dead band %g" % (
                pn, "nearest level" if a.dither == "threshold" else
                a.dither + " between levels", a.stable))
        else:
            palette = _once(a, ("pal8", pk, pn, lw, lh, crop, rate, spf,
                                a.start, a.end, ":".join(eq)), lambda:
                            colour_palette(a.src, lw, lh, crop, "%d/%d" % (
                                rate, spf), a.start, a.end, ":".join(eq),
                                pn, count=lambda n: tick(
                                    "palette", n, est, "the clip's own %d, "
                                    "chosen for colour" % pn))
                            if pk == "colour" else
                            vga8_palette(a.src, lw, lh, crop, "%d/%d" % (
                                rate, spf), a.start, a.end, ":".join(eq),
                                lambda: tick("palette", 0, 0, PALGEN_NOTE),
                                colours=pn or 256))
            d8 = Vga8Ditherer(lw, lh, palette, a.vga8_dither, a.vga8_stable,
                              kclip)
            if pn:
                say("   palette: the clip's %d colours" % pn)
        dith = Staged(d8, post=None if dw == 1 else
                      (lambda i: np.repeat(i, dw, axis=1)))
    elif pattern:
        dith = Staged(CompDitherer(w, h, a.mix, a.stable, n=a.levels_mix))
    elif comp:
        dith = CompDiffuser(w, h, a.comp_stable, not a.comp_quick)
    else:
        dith = Ditherer(a.dither, w, h, a.stable, a.invert, kclip)
    if a.frame_cap != "32" and (a.live or a.resident):
        raise vid.V88Error("--frame-cap %s is a stream's, played full "
                           "screen: not with --live or --resident "
                           "(98.1.4.1)" % a.frame_cap)
    if a.flip and L != vid.LAY_MODEX and not scr:
        raise vid.V88Error("--flip is Mode X's, or 16 colours' on a --screen "
                           "of its own: 13h and 12h have one page")
    if a.flip and scr in vid.NARROW:
        raise vid.V88Error("--flip is not a NARROW screen's: its pages are "
                           "for --pages, and a flip every frame would want "
                           "the whole canvas copied besides (98.1.3.2.5)")
    fown = bool(getattr(a, "flip_own", False))
    if fown and (not a.flip or a.loop_from is not None or a.live or
                 a.resident or getattr(a, "layer_disk", None) or
                 getattr(a, "layer_profile", None)):
        raise vid.V88Error("--flip-own is --flip's, a stream with no loop and "
                           "no layer (VIDEO-SEEN-PLAN 3b)")
    pmode = page_mode(a)
    pa_on = pmode == "on" or (pmode == "auto" and
                              getattr(a, "_paok", False))
    if pa_on and (L not in (vid.LAY_MODEX, vid.LAY_HERC) and not scr or
                  a.live or a.resident or a.loop_from is not None):
        raise vid.V88Error("--page-ahead is Mode X's, a Hercules's, or 16 "
                           "colours' on a --screen of its own, played full "
                           "screen: not "
                           "with --live, --resident or a loop "
                           "(98.2.1.8)")
    # ON N PAGES (98.2.1.8.4): --pages, which the plan pass takes too - its
    # targets are a scene change's every frame then, not a run's first
    npg = page_count(a, L, scr) if pa_on else 2
    if npg > 2 and str(getattr(a, "pages", "")) == "max" and (
            getattr(a, "layer_disk", None) or
            getattr(a, "layer_profile", None)):
        npg = 2                         # (MAX: every page the FILE can use,
                                        # and a layer's is two)
    # ...and FLIPPED AND BUILT AHEAD (98.2.1.8.5): --flip with it, on three
    # pages or more - two flip every frame and the rest build ahead
    if pa_on and a.flip and (npg < 3 or L == vid.LAY_HERC):
        raise vid.V88Error("--page-ahead with --flip builds ahead on the "
                           "pages the flip leaves: --pages 3 or more, Mode "
                           "X's or 16 colours' on a --screen of its own "
                           "(98.2.1.8.5)")
    if npg > 2 and (getattr(a, "layer_disk", None) or
                    getattr(a, "layer_profile", None)):
        raise vid.V88Error("--pages %d with a layer: a layer is drawn on two "
                           "pages and no more (98.1.3.7.1)" % npg)
    enc = EncoderX(g, prof, fps, audio_cyc, audio_bps, palette, a.flip) \
        if L == vid.LAY_MODEX else \
        EncoderP(g, prof, fps, audio_cyc, audio_bps, palette and
                 palette[:48], a.flip) if vga4 else \
        Encoder(g, prof, fps, audio_cyc, audio_bps, palette)
    if fown and not pa_on:
        enc.own_init()              # (two pages: byte 37 7, 98.1.3.10)
    elif fown:
        enc.nobring = True          # (on N pages: PageAheadF's, 98.2.1.8.5)
    enc.live = bool(a.live)
    # VERSION 2's BLOCKS (98.1.10): candidates beside the changes, on the
    # one page a plain stream has
    enc.blocks = getattr(a, "blocks", "off") == "on"
    if enc.blocks:
        if not (vga4 or vga8 or (L in vid.ONEBIT_LAYOUTS and not (
                cga4 or c160 or c512 or text))):
            raise vid.V88Error("--blocks: vga4, vga8, and one bit on cga or "
                               "herc (98.1.10)")
        g.v2 = True
        # WHAT BLOCKS ARE NOT MADE WITH (98.1.10.1): Live and a resident
        # file decode into RAM, which the block decoder does not write; a
        # tear's one cut line is screen order and blocks are ranked; bands
        # would want a block list a band; one bit's seam would join to a
        # codebook from the file's end
        for cond, what in ((a.live, "--live"), (a.resident, "--resident"),
                           (a.bands, "--bands"),
                           (a.loop_from is not None and not (vga4 or vga8),
                            "--loop-from on one bit"),
                           (getattr(a, "cut", "rank") == "tear",
                            "--cut tear")):
            if cond:
                raise vid.V88Error("--blocks with %s (98.1.10.1)" % what)
    # THE XMS COPIES THE PLAYER WILL MAKE (set_xplay): in place where the
    # player's in-place paths take the file, the copying FIFO where they
    # refuse it - the speaker's clock, Repeat or a loop, Live, RESIDENT and
    # the two formats decoded through the shadow (98.3.18.13). Not BIGSP:
    # the hold never refused it, and the bank's mirror is two slots for it
    # since 2026-10-19
    xmode = prof.get("xplay")
    if xmode == "inplace" and (spk or a.repeat or a.loop_from is not None
                               or a.live or a.resident
                               or a.pixfmt in ("c160", "text")
                               or enc.blocks):  # (98.3.18.11: version 2)
        xmode = "copy"
    enc.set_xplay(xmode, prof)
    # WRITE MODE 3 (98.1.3.2.4) for a byte a refresh could catch half
    # written in a colour neither frame has: every 16-colour stream, its
    # own sixteen or the EGA's (the EGA's since 2026-10-14 - a window's
    # decode holds the gfx lock, so nothing else draws between the player's
    # stores there either), and not a flipped one, which no refresh sees
    # half drawn, nor a Live or resident one, which the desktop draws
    enc.wm3 = bool(vga4 and not a.flip and not a.live and not a.resident
                   and getattr(a, "vga4_wm3", "on") == "on")
    enc.spk = spk_share
    a.bank = getattr(a, "bank", None)
    a.prefill = getattr(a, "prefill", None)
    # --bank auto (98.2.1.3.3): encoded against the whole pool, and the
    # file asks for only what the encode drew - sticky, as --avg's is
    a.bank_auto = getattr(a, "bank_auto", False) or a.bank == "auto"
    if a.bank_auto:
        a.bank = float(getattr(a, "pool", None) or POOL_KB)
    if a.bank:
        # THE XMS BANK (98.3.18.2): deeper, and fuller at the start by the
        # prefill - in seconds of the profile's disk, or the bank whole
        if a.resident or a.live or prof["disk"] is None:
            raise vid.V88Error("--bank is a stream's, played off a disk: not "
                               "with --resident, --live or a lossless "
                               "profile")
        bank = int(a.bank * 1024)
        pre = bank if a.prefill == "all" else \
            min(bank, int(float(a.prefill or BANK_PREFILL) * prof["disk"]))
        enc.set_bank(bank, pre, prof.get("xup", prof.get("xcopy")) or 0.0)
    elif a.prefill:
        raise vid.V88Error("--prefill fills a --bank, and there is none")
    # THE LAG (98.2.1.1.2): --lag ms, in frames. Only calls that come faster
    # than the frames - a card's sound, every half period - can bring a
    # picture that fell behind back to the sound; a period apart (silent,
    # the speaker's clock) it stays behind for good
    lo_ms, hi_ms = lag_bounds(getattr(a, "lag", LAG_MS))
    lad_lags = [v for kind, v in degrade_rungs(a) if kind == "lag"]
    if degrade_rungs(a):                 # (--ladder: its lag rungs, a frame
        lo_ms = hi_ms = max(lad_lags) if lad_lags else 0.0  # the floor)
    lagf = hi_ms * fps / 1000.0
    card = bool(afmt) and not spk
    lagon = False
    if a.flip and prof["avg"] is not None:
        # the flip schedule's calls (98.2.1.1.1): half a period apart off a
        # card's sound, a period silent
        enc.fgrid = enc.q / 2 if card else enc.q
        enc.flag_lag = max(1.0, lagf) if enc.fgrid < enc.q else 1
        lagon = enc.fgrid < enc.q
    elif card and lagf > 1.0 and enc.owe is not None and not a.live:
        # ...and an UNFLIPPED play with a card's sound, which draws up to
        # two frames a call and so catches up too: the lag schedule in
        # place of the owed-time one, which allowed one frame owed and no
        # more (98.2.1.1) - the owner's Victory plays a picture 4.5 frames
        # behind and caught up as smooth, where a third frame due was
        # counted fatal (docs/reports/VIDPROF-VICTORY286-2026-10-15.md)
        enc.owe = None
        enc.fgrid = enc.q / 2
        enc.flag_lag = lagf
        lagon = True
    # THE RANGE (98.2.1.1.3): the allowances in it that the schedule tells
    # apart - one a CALL, half a period apart, since an allowance between
    # two calls is the call above it (begin()) - a frame at least, the
    # schedule's own floor; the search below picks one each time the
    # picture starts to fall behind
    if degrade_rungs(a) and lagon:
        enc.flag_lag = 1                # (the ladder raises it a stretch)
    enc.lag_cands = None
    if lagon and hi_ms > lo_ms:
        gr = enc.fgrid / enc.q
        k0 = math.ceil(max(1.0, lo_ms * fps / 1000.0) / gr - 1e-9)
        k1 = math.ceil(max(1.0, hi_ms * fps / 1000.0) / gr - 1e-9)
        if k1 > k0:
            enc.lag_cands = [k * gr for k in range(k0, k1 + 1)]
    if enc.reserve and not a.resident:  # (a disk to keep ahead: 98.2.1.3;
        enc.alead = audio_lead(afmt, rate, abytes)   # resident: none)
    # BUFFER SOUND AHEAD (98.1.8, 98.2.1.3): the file carries the sound's
    # lead instead of the budget holding it back. On: always. Auto: where
    # there is no disk model to say the reader keeps a block of sound ahead
    # (lossless, or no --disk) - and on a budgeted profile only if some
    # frame spends what the in-step budget would have kept, which the
    # encode counts (Encoder.lean) and decides at the end. Off: in step
    ahead = 0
    if afmt and abytes and not a.resident and not a.live and \
            a.buffer_sound_ahead != "off":
        ahead = ahead_frames(a.ahead_frames, abytes)
    ahead_auto = ahead and a.buffer_sound_ahead == "auto" and \
        bool(enc.alead)
    if ahead:
        if ahead_auto:
            enc.slead = enc.alead       # Auto's question: in step, would
            enc.amargin = AHEAD_SLACK   # this frame have fitted - and on a
        enc.alead = max(0, enc.alead - ahead)   # reader a little slower?
    # WHAT A CUT FRAME SPENDS ON (98.2.1.2)
    enc.look, enc.vis = a.lookahead, a.error == "visible"
    enc.lfloor = float(getattr(a, "lookahead_floor", 0.0) or 0.0)
    enc.attend = float(getattr(a, "attend", 0.0) or 0.0)
    enc.edges = float(getattr(a, "edges", 1.0) or 0.0)
    enc.cut = a.cut
    if not 0 <= a.bands <= 60:
        raise vid.V88Error("--bands %d: 0 to 60" % a.bands)
    if a.bands and (a.live or not (L == vid.LAY_MODEX or vga4)):
        raise vid.V88Error("--bands is a Mode X or 16-colour stream's: the "
                           "formats drawn a plane at a time (98.2.1.2.2)")
    enc.bands = a.bands
    enc.rec_max = FRAME_CAPS[a.frame_cap] * 512 - 2048
    enc.thr = (a.worth if a.worth is not None else AIM_WORTH) \
        if a.aim == "size" else 0.0
    asp = vid.ASPECT.get(L, (1, 1))
    enc.blur_r = max(1, round(asp[1] * (dh or 1) / asp[0]))
    if cga4:
        enc.dmode, enc.ppb = "idx", 4
        enc.dpal = np.frombuffer(b"".join(
            vid.STD16[3 * c:3 * c + 3] for c in vid.cga4_colours(cgapal)),
            np.uint8).astype(np.float32).reshape(-1, 3) * (255.0 / 63)
    elif c160:
        enc.dmode, enc.ppb = "idx", 2
        enc.dpal = np.frombuffer(vid.STD16, np.uint8).astype(
            np.float32).reshape(-1, 3) * (255.0 / 63)
    elif c512 or comp or text:      # a composite colour is not its bits:
        if enc.vis:                 # the look-ahead still works on them,
            say("   --error visible: a %s format has no pixel form "
                "here - ranking by its bits"
                % ("text" if text else "composite"))
        enc.vis = False             # and the picture line is its bits'
    enc.metric = not (c512 or comp or text)
    if enc.live:                        # a pass a tick on the desktop
        enc.owe = None                  # (98.3.10), not the hook's calls
    wr = vid.Writer(g, rate, spf, vid.AUD_NONE if a.resident else afmt,
                    0 if a.resident else abytes,
                    vid.PF_VGA8 if vga8 else vid.PF_VGA4 if vga4 else
                    vid.PF_CGA4 if cga4 else vid.PF_C160 if c160 else
                    vid.PF_C512 if c512 else vid.PF_TEXT if text else
                    vid.PF_CGACOMP if comp else vid.PF_MONO1, cgapal=cgapal,
                    title=a.title or os.path.splitext(
                        os.path.basename(a.src))[0][:47],
                    credits=a.credits or "", keysecs=a.keysecs,
                    palette=palette, rowscale=dh, flip=a.flip,
                    own=fown and not pa_on,
                    aspect=scaled_aspect(pasp or vid.ASPECT[L], dh),
                    loop=None if a.loop_from is None else
                    max(0, round(a.loop_from * fps)),
                    repeat=a.repeat, spk=spk and bool(afmt),
                    spkp=a.spk_pulses if spk and afmt else 1,
                    live=vid.TARGETS[a.live] if a.live and not a.resident
                    else None, ahead=ahead, kcap=KEY_PLAYER,
                    spcap=FRAME_CAPS[a.frame_cap], screen=scr,
                    page=pa_on and getattr(a, "_paplan", None) is not None,
                    pages=npg if pa_on and getattr(a, "_paplan", None)
                    is not None else 2)
    wr.opts = optsblk                   # (98.1.1.4)
    wr.v2 = enc.blocks                  # VERSION 2 (98.1.10)
    if a.resident or a.live or (getattr(a, "_paplan", None) is not None
                                and a.repeat):
        # KEYS WHOLE (98.1.3.8): a resident file's are in its block, a Live
        # one's decode into the shadow off one read, and a page-ahead lap
        # joins at key 0 inside the hook, which cannot read a second part -
        # so those keep one record, and a key past a read is left out
        wr.kpart = None
    if enc.bank:                        # THE BANK'S ASK (98.3.18.3)
        wr.xbank = enc.bank // 1024
        wr.xpre = vid.XPRE_ALL if a.prefill == "all" else \
            max(1, -(-enc.bankpre // 1024))
    if not a.resident and enc.disk.per is not None:
        wr.ring = vid.ring_for(enc.reserve)     # (98.2.1.3)
        if wr.ring is None:
            raise vid.V88Error(
                "--reserve %d KB: the player's ring banks %d KB at most"
                % (enc.reserve // 1024,
                   (vid.RING_MAX - 2) * vid.SLOT // 1024))
    if afmt:
        tick("sound", 0, 0, "the clip's sound, read whole%s: no count" % (
            " and shaped for the speaker" if spk and
            a.spk_shape == SPK_ENC else ""))
    if spk and afmt and a.spk_shape == SPK_ENC:
        pcm = speaker_pcm(a, rate, say)
    else:
        pcm = ffmpeg_audio(a.src, rate, a.start, a.end, a.volume) \
            if afmt else b""
    cyc = []
    if isinstance(dith, CompDiffuser) and jobs > 1:
        src = iter(comp_parallel(frames, w, h, a, jobs,
                                 tick=lambda n: tick("read", n, est)))
    else:
        if scene_pals:                  # (the ditherer picks a scene by the
            frames = enumerate(frames)  # frame: 98.1.3.2.3)
        src = dither_all(dith, frames, jobs)
        readers.append(src)             # (a cancel ends its pool)
    if not stream:                      # every frame before the sound
        src = iter(list(src))
    first = next(src, None)
    if first is None:
        raise vid.V88Error("no frames came out of %s" % a.src)
    # WHAT THE PROGRESS SAYS (98.2.11): the frames dithered as they come
    # in, beside the frames encoded - streamed, both go on at once - and
    # what a frame is doing that takes longer than a frame (`doing`)
    ndith = [1]
    doing = [""]

    def dithered(it):
        for x in it:
            ndith[0] += 1
            yield x
    src = dithered(src)
    tick("prepare", 0, 0, "the pre-roll: is the first picture whole in "
         "one frame?")
    # THE PRE-ROLL (SPEC.md 98.2.9): a first picture the budgets cannot
    # paint in one frame is HELD - the source's frame 0 again, over silence -
    # until it is whole, and the keyframes start there. Key 0 was a snapshot
    # of a half-painted screen, and a colour play STARTS from key 0, so the
    # first frames of a colour file came out streaked (the owner's report: a
    # 320 x 180 VGA8 frame is 57,600 bytes against a 30 KB record)
    pre = preroll(enc, first, abytes if afmt and not a.resident else 0,
                  round(PRE_SECS * fps))
    if pre:
        if afmt:
            pcm = b"\x80" * (pre * spf) + pcm
        wr.key0 = pre
        if wr.loop is not None:
            wr.loop += pre
        say("   pre-roll: %d frame(s) hold the first picture until it is "
            "whole (%.2f s); the keyframes start there" % (pre, pre / fps))
    import itertools
    from collections import deque
    targets = itertools.chain([first] * (pre + 1), src)
    if stream:
        # THE SOUND A FRAME AT A TIME: PCM8's chunk f is its samples, 80h
        # past the end of the clip's sound - audio_chunks' own, with no
        # frame count wanted
        def chunk(f):
            c = bytes(pcm[f * spf:(f + 1) * spf])
            return c + b"\x80" * (spf - len(c))
        total = est + pre if est else 0

        def progress(f):
            """The frames ENCODED, and beside them the frames read and
            dithered so far - streamed, they go on at once, so the count
            is what the encoder has done and the note what runs ahead of
            it; until 2026-10-17 one count was spread over three made-up
            steps, a "Reading" frame number nothing had read"""
            tick("encode", f, total, "; ".join(x for x in (
                "read and dithered %d%s" % (ndith[0] + pre, (
                    " of %d" % total) if total else ""), doing[0]) if x))
        progress(0)
    else:
        rest = list(targets)
        nf = len(rest)
        targets = iter(rest)
        if afmt:
            tick("sound", 0, 0, "the sound in %s%s: no count" % (
                "ADPCM4" if afmt == vid.AUD_ADPCM4 else "PCM8",
                ", searched on %d cores" % jobs if a.adpcm == "search" and
                afmt == vid.AUD_ADPCM4 and jobs > 1 else ""))
        # a RESIDENT file's ADPCM4 ends where its lap joins (98.1.7.2): the
        # seam's frame L, or 80h at scale 0 before frame 0
        join = (wr.loop if wr.loop is not None else "start") \
            if a.resident and afmt == vid.AUD_ADPCM4 else None
        chunks = vid.audio_chunks(pcm, nf, spf, afmt, vid.key_frames(
            nf, wr.keyint, wr.key0),
            search=jobs if a.adpcm == "search" else 0, join=join) \
            if afmt else None
        chunk = chunks.__getitem__ if afmt else None
    lay = None
    if getattr(a, "layer_profile", None) and not a.layer_disk:
        a.layer_disk = PROFILES[a.layer_profile]["disk"]   # (its own disk)
    if getattr(a, "layer_disk", None):
        # THE LAYER (98.1.9): made beside the base, LAYER_LAG behind it
        if a.resident or a.live or enc.disk.per is None:
            raise vid.V88Error("--layer-disk is a budgeted stream's: not "
                               "--resident, --live or a lossless profile")
        lmem = float(a.layer_memory)
        # --layer-bank auto (98.2.1.3.3): what the layer's machine's pool
        # leaves once it keeps the base's bank it needs - that need from the
        # first pass (`_lyneed`, KB), or learned as this one goes
        a.layer_bank_auto = getattr(a, "layer_bank_auto", False) or \
            getattr(a, "layer_bank", None) == "auto"
        lneed = getattr(a, "_lyneed", None)
        lpool = float(getattr(a, "layer_pool", None) or
                      getattr(a, "pool", None) or POOL_KB) \
            if a.layer_bank_auto else None
        lbank = (max(0.0, lpool - (lneed or 0)) // 32 * 32) \
            if a.layer_bank_auto else \
            float(getattr(a, "layer_bank", None) or 0)
        ltot = lmem + lbank
        lpre = ltot if a.layer_prefill == "all" else \
            float(a.layer_prefill or 0)
        if not 0 <= lpre <= ltot or not 64 <= lmem <= 128 or \
                not 0 <= lbank <= 65000:
            raise vid.V88Error("--layer-memory %g KB, --layer-bank %g KB, "
                               "--layer-prefill %s: 64 to 128 KB of slots, a "
                               "bank to 65,000, and a prefill within the two"
                               % (lmem, lbank, a.layer_prefill))
        mach, lprof = None, prof
        if getattr(a, "layer_profile", None) and \
                a.layer_profile != a.profile:
            # THE LAYER FOR ANOTHER CPU: an encoder made for that profile,
            # with every setting the base's but its machine's
            lprof = dict(PROFILES[a.layer_profile])
            if getattr(a, "avg_auto", False) and not a.live:
                lprof["avg"] = avg_auto(lprof)  # (98.2.1.5: its machine's)
            if lprof["avg"] is None:
                raise vid.V88Error("--layer-profile lossless: a layer is what "
                                   "a budget leaves, and it has none")
            if spk_share:
                raise vid.V88Error("--layer-profile with the speaker's sound: "
                                   "its share is the base's machine's")
            lprof["disk_at"] = disk_curve_for(lprof, a.layer_disk)
            lprof["disk"] = a.layer_disk
            mach = type(enc)(*((g, lprof, fps, audio_cyc, audio_bps,
                                palette and (palette[:48] if vga4 else
                                             palette)) +
                               ((False,) if type(enc) is not Encoder
                                else ())))
            for n in ("live", "spk", "look", "vis", "cut", "bands",
                      "rec_max", "thr", "blur_r", "dmode", "ppb", "dpal",
                      "metric", "wm3"):     # (wm3: the options audit - a
                                            # layer for another profile was
                                            # priced without write mode 3)
                if hasattr(enc, n):
                    setattr(mach, n, getattr(enc, n))
        lay = LayerEnc(enc, a.layer_disk, ltot, lpre, a.layer_seek,
                       bank=lbank * 1024, xcopy=lprof.get("xcopy") or 0.0,
                       mach=mach, bxcopy=lprof.get("xcopy") or 0.0,
                       need=None if lneed is None else lneed * 1024.0,
                       lpool=None if lpool is None else lpool * 1024.0,
                       track=getattr(a, "_lypass1", False),
                       future=getattr(a, "_lytrace", None),
                       paged=getattr(a, "_paplan", None) is not None)
        lay.lmem, lay.lprefill = lmem, a.layer_prefill
        lay.speeds = (lprof.get("speed") or 1, prof.get("speed") or 1)
        wr.layer = (max(1, int(round(a.layer_disk / 1024.0))), int(lmem),
                    vid.XPRE_ALL if a.layer_prefill == "all" else int(lpre),
                    int(lbank))
    elif getattr(a, "layer_prefill", None):
        raise vid.V88Error("--layer-prefill fills a layer, and there is no "
                           "--layer-disk")
    # PAGE-AHEAD (98.2.1.8): the plan pass logs; the real pass builds
    plan = getattr(a, "_paplan", None)
    pa = (PageAheadF(enc, plan, npg, wr.key0, KEY_PLAYER, wr.kpart,
                     int(wr.audio_fmt == vid.AUD_ADPCM4), own=fown)
          if npg > 2 and a.flip else
          PageAheadN(enc, plan, npg, wr.key0, KEY_PLAYER, wr.kpart,
                     int(wr.audio_fmt == vid.AUD_ADPCM4)) if npg > 2 else
          PageAhead(enc, plan, wr.key0, KEY_PLAYER, wr.kpart,
                    int(wr.audio_fmt == vid.AUD_ADPCM4))) \
        if pa_on and plan is not None else None
    if npg > 2 and pa is not None:      # (the writer's key frames)
        pa.keyat = lambda k: k >= wr.key0 and \
            (k - wr.key0) % wr.keyint == 0
        pa.keyrule = getattr(a, "page_keys", "all") == "started"
        wr.kpool = getattr(a, "key_pool", "on") == "on"
    plog = {"log": [], "T": {}, "P": {}} if pa_on and plan is None else None
    pcut = False
    ptgt = None                         # (the frame before, for a scene's)
    ahead = deque()
    sound = []
    coarse_used = {}                    # (--coarse-cut: the cells drawn)
    cells_at = set()                    # (...and the frames they may be at)
    if scene_pals:                      # the output frame each scene starts:
        pev_at = {(s0 + pre if s0 else 0): p for s0, p in scene_pals}
        scene_pre = pre
        wr.pev, pev_cut = [], []        # the pre-roll holds source frame 0
        pev_left = []                   # (each change's frame, its share
                                        # of the last scene left standing)
        shot_cut = []                   # [first frame, frames, cut,
                                        # frames visibly broken] a scene
    bl0 = min(max(0.0, enc.disk.level - enc.reserve), enc.bank) \
        if enc.bank else 0.0
    bspan = [bl0, bl0]                  # the bank's lowest and highest

    def au_of(k):
        """Frame k's sound, as the record carries it"""
        x = chunk(k) if afmt else b""
        if spk and afmt:            # THE SPEAKER'S COUNTS, not samples
            x = vid.spk_counts(x, rate, a.spk_pulses)       # (98.1.1.3)
        return x

    def step(e, p, k, target, au, fut, vfut=()):
        """Frame k on encoder `e` and page-ahead `p`: all a frame does to
        the ENCODER - which the lag search (98.2.1.1.3) plays on copies -
        and what the writer, the layer and the report take from it.
        `vfut`, the targets after it as far as a variable frame rate looks
        (--min-fps): its holds are the encoder's state (`vplan`), so the
        search's copies play them too"""
        e.future = fut
        # ONE BIT's codebook (98.1.10): whole at frame 0 and after every key
        e.cb_due = k == 0 or (k - 1 >= wr.key0 and
                              (k - 1 - wr.key0) % wr.keyint == 0)
        npal = pal = cell = None
        if scene_pals and k in pev_at:  # A NEW SCENE (98.1.3.2.3): its
            pal = pev_at[k]             # colours from this frame, which the
            e.set_pal(np.frombuffer(                # player loads before it
                pal, np.uint8)[:768].astype(np.float32).reshape(-1, 3)
                * (255.0 / 63))                     # decodes the record
            npal = e.pal                # (...and the layer's, at ITS frame f)
        c0 = e.stats["cut"]
        e.last_why = None
        if p is not None:
            p.before(k, target)
        draw = target
        # (not onto a page just FLIPPED to: page-ahead built that frame
        # nearer than any cells, and the cells are its fallback - the
        # options audit, 2026-10-14)
        if a.coarse_cut != "off" and not lrungs and k in cells_at and \
                k and not (p is not None and p.flipped):
            # (a flat byte is ONE store in write mode 3 and up to two by
            # planes, and a flat run is one RUN: the cells fit a record)
            cell = a.coarse_cut if a.coarse_cut != "auto" else \
                coarse_pick(e, target, au)
            if cell != "full":
                draw = coarse_of(e, target, cell, au)
                e.all3 = getattr(e, "wm3", False)
        held = hdraw = None
        if lsteps is not None and vreplay is None:
            if e.vlagn:
                e.vlagn -= 1
            elif e.flag_lag != lbase and getattr(e, "flag", 0) <= 0:
                e.flag_lag = lbase      # (level again: the ladder's floor)
        if vreplay is not None and lsteps is not None:
            # (the plan's rungs, replayed: its lag and its cells)
            e.flag_lag = plan.get("LG", {}).get(k, lbase)
            c = plan.get("CC", {}).get(k)
            if c is not None and draw is target:
                cell = c
                draw = coarse_of(e, target, c, au)
                e.all3 = getattr(e, "wm3", False)
        if vreplay is not None:
            if k in vreplay and draw is target:
                held = True             # (nothing on the page shown: the
                c0 = e.stats["cut"]     # hidden page builds the next)
                ops, rec = e.frame(e.screen.copy(), au)
                e.stats["exact"] -= 1
                e.stats["holds"] = e.stats.get("holds", 0) + 1
        elif vfr and not (pre and k <= pre) and not e.vplan and vfut and (
                draw is target or k in cells_at) and not (
                (vempty or vstay) and scene_pals and k in pev_at):
            # (nor a PRE-ROLL frame: the still is painted in plainly, as
            # preroll() counted it, or key 0 is not whole - 98.2.9.2)
            # (a flipped file holds no frame its palette changes at: the
            # palette goes on with the flip, onto the picture shown)
            # (a stretch stops short of the next scene's palette)
            n = 1
            while n < vspan and n <= len(vfut) and \
                    not (scene_pals and k + n in pev_at):
                n += 1
            vcut = n >= 2 and (draw is not target or
                               vfr_cut(e, target, au))
            if os.environ.get("OS88_VENC_VFRLOG") and e is enc:
                with open(os.environ["OS88_VENC_VFRLOG"], "a") as fl:
                    fl.write("f %d: stretch %d, cut %s\n" % (k, n, vcut))
            ri = next((i for i, rg in enumerate(e.vranges)
                       if rg[0] <= k < rg[1]), None)
            ksteps = lsteps if ri is None else e.vranges[ri][2]
            if vcut and ksteps:
                busy(k, "the ladder%s at frame %d: today's draw and %d "
                     "rung%s tried on copies of the encoder%s" % (
                         "" if ri is None else " for %s s"
                         % e.vranges[ri][4], k, len(ksteps),
                         "" if len(ksteps) == 1 else "s",
                         ", at once" if trials() is not None else
                         ", one by one"))
                pat, r, lg, lc, li, ev = degrade_choose(
                    e, [target] + list(vfut)[:n - 1], au, ksteps, fps,
                    e.q_stp, draw, vcost, vempty, vshown, vstay,
                    (lambda c, t=target: coarse_of(
                        e, t, c if c != "auto" else
                        coarse_pick(e, t, au), au))
                    if coarse_on and k else None, k in cells_at,
                    pool=trials())
                doing[0] = ""
                if lg is not None:      # (the rung's lag, until level)
                    e.flag_lag, e.vlagn = lg, n
                if lc is not None:
                    cell = lc if lc != "auto" else coarse_pick(e, target, au)
                    if cell != "full":
                        draw = coarse_of(e, target, cell, au)
                        e.all3 = getattr(e, "wm3", False)
                if ri is None:
                    e.vladder[li] = e.vladder.get(li, 0) + 1
                else:
                    e.vladat[(ri, li)] = e.vladat.get((ri, li), 0) + 1
                if li:
                    e.vstretch.append((k, k + n, round(r * fps, 1)))
                    if r < 1.0:
                        e.vrates[round(r * fps, 1)] = \
                            e.vrates.get(round(r * fps, 1), 0) + 1
                    if lg is not None:
                        for f in range(k, k + n):
                            e.vlagat[f] = lg
                    if lc is not None:
                        e.vcellat[k] = cell
                e.vplan.extend(pat if any(pat) else pat[:VFR_AGAIN])
                # (coarseany: each frame the budgets would cut of what is
                # committed drawn in cells, from this one - below)
                e.vcellrun = (k + (n if any(pat) else VFR_AGAIN), ev) \
                    if ev else None
            elif vcut and lsteps is None:
                busy(k, "the frame rate at frame %d: slower rates tried on "
                     "copies of the encoder" % k)
                pat, r = vfr_choose(e, [target] + list(vfut)[:n - 1], au,
                                    vfr, fps, e.q_stp, draw, vcost, vempty,
                                    vshown, vstay)
                doing[0] = ""
                # (the full rate is committed for VFR_AGAIN frames only, so a
                # stretch that would slow better a frame or two on is asked
                # again there)
                e.vplan.extend(pat if any(pat) else pat[:VFR_AGAIN])
                if pat[0] and draw is not target:
                    draw, cell = target, None   # (slowed instead of the
                    e.all3 = False              # cells)
                if any(pat):
                    e.vrates[round(r, 1)] = e.vrates.get(round(r, 1), 0) + 1
                    e.vstretch.append((k, k + n, round(r, 1)))
        if e.vcellrun and vreplay is None and held is None and \
                draw is target and k < e.vcellrun[0] and \
                not (e.vplan and e.vplan[0]):
            d = coarse_every(e.vcellrun[1])(e, target, au)
            if d is not None:           # (COARSEANY's frame: cells, where
                draw = d                # the budgets would cut it)
                cell = e.vcellrun[1] if e.vcellrun[1] != "auto" else \
                    coarse_pick(e, target, au)
                e.all3 = getattr(e, "wm3", False)
                e.vcellat[k] = cell
        if held:
            pass
        elif e.vplan and e.vplan.popleft() and draw is target:
            held = True                 # (drawn toward the next frame the
            j = 0                       # rate shows, which finishes it -
            while e.vplan[j]:           # or, flipped, nothing at all)
                j += 1
            hdraw = None if vempty or vstay else vfut[j]
            if vstay:                   # (98.1.3.9: drawn on the hidden
                e.staying = True        # page toward the next, not shown)
            ops, rec = e.frame(vfut[j] if vstay else
                               e.screen.copy() if vempty else hdraw, au)
            if e.stats["cut"] > c0:
                e.stats["cut"] -= 1
            else:
                e.stats["exact"] -= 1
            e.stats["holds"] = e.stats.get("holds", 0) + 1
        else:
            ops, rec = e.frame(draw, au)
        e.all3 = False
        hops = p.after(k, rec, au, target) if p is not None else None
        left = int(np.count_nonzero(e.screen != target)) \
            if scene_pals and k in pev_at and k else None
        c = e.charge(rec, len(au))
        hs = p.carried(k) if p is not None and (a.flip or npg > 2) else None
        return dict(ops=ops, rec=rec, hops=hops, draw=draw, npal=npal,
                    pal=pal, cell=cell, cut=e.stats["cut"] - c0, left=left,
                    c=c, hs=hs, held=held, hdraw=hdraw,
                    stay=bool(held and vstay and vreplay is None),
                    lag=e.flag_lag if lsteps is not None else None)

    def lag_seed(objs, bufs):
        """What a lag snapshot SHARES rather than copies: the targets (and
        the target caches' own), the plan, and each encoder's tables -
        none of them written once made"""
        memo = {}
        for t in bufs:
            memo[id(t)] = t
        for o in objs:
            if o is None:
                continue
            encs = [o] if isinstance(o, Encoder) else \
                [x for x in [getattr(o, "h", None)] +
                 list(getattr(o, "hs", None) or ()) if x is not None]
            for x in ("log", "T", "P"):
                if hasattr(o, x) and not isinstance(o, Encoder):
                    memo[id(getattr(o, x))] = getattr(o, x)
            for e in encs:
                for c in getattr(e, "tcache", {}).values():
                    memo[id(c[0])] = c[0]
                for x in ("g", "idx", "base", "rowof", "ct", "csub"):
                    if x in e.__dict__:
                        memo[id(e.__dict__[x])] = e.__dict__[x]
        return memo

    def lag_restore(e, p, snap, bufs):
        """The REAL encoder and page-ahead put back to `snap`, in place:
        the layer and the closures above hold them by identity"""
        memo = lag_seed(snap, bufs)
        memo[id(snap[0])] = e
        if p is not None:
            memo[id(snap[1])] = p
        d = copy.deepcopy((snap[0].__dict__,
                           snap[1].__dict__ if p is not None else None), memo)
        e.__dict__.clear()
        e.__dict__.update(d[0])
        e.sv = np.frombuffer(e.surf, dtype=np.uint8)
        if "tv" in e.__dict__:
            e.tv = np.frombuffer(e.tsurf, dtype=np.uint8)
        if p is not None:
            p.__dict__.clear()
            p.__dict__.update(d[1])

    def lag_search(snap, k0, buf):
        """THE LAG SEARCH (98.2.1.1.3): from `snap`, frame k0 - the picture
        about to fall behind - played on a copy with every allowance in the
        range, each until LAG_SETTLE seconds past its picture level again
        or LAG_WIN seconds, all to the same frame; the allowance that cut
        the fewest frames, then left the fewest bytes wrong, then the
        least lag. `buf[j]` is frame k0+j's target"""
        nonlocal lag_simf
        settle = max(1, int(round(LAG_SETTLE * fps)))
        most = min(len(buf), max(1, int(round(LAG_WIN * fps))))
        runs = []

        def play(r, upto):
            while r["j"] < upto:
                j = r["j"]
                x = step(r["e"], r["p"], k0 + j, buf[j],
                         b"" if a.resident else au_of(k0 + j),
                         buf[j + 1:j + 1 + r["e"].look], buf[j + 1:])
                r["cut"] += x["cut"]
                r["left"] += r["e"].stats["bytes_left"] - r["bl"]
                r["bl"] = r["e"].stats["bytes_left"]
                r["j"] += 1
                if r["level"] is None and r["e"].flag <= 0:
                    r["level"] = r["j"]
                r["upto"] = min(most, (r["level"] or most) + settle)
        for L in enc.lag_cands:
            e, p = copy.deepcopy(snap, lag_seed(snap, buf))
            e.flag_lag = L
            r = dict(L=L, e=e, p=p, j=0, cut=0, left=0, level=None,
                     bl=e.stats["bytes_left"], upto=most)
            # (until level and settled: each frame may move `upto`)
            while r["j"] < r["upto"]:
                play(r, r["j"] + 1)
            runs.append(r)
        end = max(r["upto"] for r in runs)
        for r in runs:
            play(r, end)
            lag_simf += r["j"]
        best = lag_best(runs)
        lag_log.append((k0, best["L"], end,
                        [(r["L"], r["cut"]) for r in runs]))
        return best["L"]

    import copy
    lag_log, lag_simf = [], 0
    lag_since = None                    # frames since the search, in a burst
    f = -1
    # VARIABLE FRAME RATE (--min-fps, VIDEO-SEEN-PLAN 3a): the stretch's
    # hold pattern, decided where the budgets would first cut
    vfr = float(getattr(a, "min_fps", 0.0) or 0.0)
    lrungs = degrade_rungs(a)
    lsteps = None
    coarse_on = coarse_screen(enc.g)    # (the cells pay: COARSE_LADDER_MX)
    lranges = ladder_ranges(getattr(a, "ladder_at", ""))
    if lranges and not lrungs:
        raise vid.V88Error("--ladder-at needs a --ladder for the rest of the "
                           "clip (cut:100 is one that only cuts)")
    enc.vranges = []                    # (--ladder-at's: k0, k1, steps,
    if lrungs:                          # kept, the range as written)
        # THE DEGRADE LADDER (--ladder): its rungs in place of --min-fps,
        # --lag's search and --coarse-cut, each kept only where this file
        # can use it - the rest said, and left out
        kept, why = [], []
        allr = list(lrungs) + [(kind, v) for t0, t1, rs in lranges
                               for kind, v in rs]
        okr = []
        for kind, v in allr:
            no = "a sound card's sound (the lag catches up only on its " \
                "clock)" if kind == "lag" and not lagon else \
                "scene palettes (--scene-palettes)" if kind == "colours" \
                and not scene_pals else \
                "a planar screen - VGA's 16 colours or Mode X - where a flat "\
                "cell is one store for many pixels" if kind in (
                    "coarsescene", "coarseany") and not coarse_on else \
                "a file that is not Live or layered" if kind == "slow" and (
                    a.live or lay is not None) else \
                "pixels to judge a rung by" if not enc.metric else None
            if no:
                w = "%s:%s needs %s" % (kind, v if kind != "slow"
                                         else "%.3g" % v, no)
                if w not in why:
                    why.append(w)
            else:
                okr.append((kind, v))
        kept = [(kind, v) for kind, v in lrungs if (kind, v) in okr]
        for w in why:
            say("   --ladder: %s - left out" % w)
        lsteps = degrade_steps(kept, fps, enc.fgrid / enc.q
                                 if enc.fgrid else None)
        enc.vkeep = kept
        # THE LADDER BY TIME (--ladder-at, 98.2.1.10): a stretch that
        # STARTS in a range climbs that range's ladder instead - frames as
        # the file counts them, the pre-roll's included, as the report and
        # the window give times
        for t0, t1, rs in lranges:
            kr = [(kind, v) for kind, v in rs if (kind, v) in okr]
            enc.vranges.append((int(round(t0 * fps)), int(round(t1 * fps)),
                                degrade_steps(kr, fps, enc.fgrid / enc.q
                                              if enc.fgrid else None), kr,
                                "%g-%g" % (t0, t1)))
        rs = [v for kind, v in kept + [kv for r in enc.vranges
                                       for kv in r[3]] if kind == "slow"]
        vfr = fps * min(rs) if rs else fps  # (the ladder decides, or nothing
        if not lsteps and not any(r[2] for r in enc.vranges):
            lsteps, vfr = None, 0.0         # (left to decide with)
        elif not lsteps:                    # (only the ranges climb)
            lsteps = []
    lbase = enc.flag_lag
    enc.vlagn, enc.vladder, enc.vlagat, enc.vcellat = 0, {}, {}, {}
    enc.vladat = {}                     # (--ladder-at's: (range, rung))
    enc.vcellrun = None                 # (coarseany's: until, cell)
    if not hasattr(enc, "vkeep"):
        enc.vkeep = []
    # THE FRAMES COARSE CELLS MAY BE AT (98.2.1.9): each scene's first - a
    # palette a scene's own starts, or on a planar screen without them the
    # same scene pass's cuts, read for the cells alone (one more read of
    # the clip, at the canvas's size, its colours not used)
    if coarse_on and (a.coarse_cut != "off" and not lrungs or any(
            kind == "coarsescene" for kind, v in enc.vkeep + [
                kv for r in getattr(enc, "vranges", ()) for kv in r[3]])):
        if scene_pals:
            cells_at = set(pev_at)
        else:
            cuts = _once(a, ("cuts", lw, lh, crop, rate, spf, a.start, a.end,
                             ":".join(eq), a.scene_cover, a.scene_min),
                         lambda: [s0 for s0, px in scene_pass(
                             a.src, lw, lh, crop, "%d/%d" % (rate, spf),
                             a.start, a.end, ":".join(eq), cap=1,
                             cover=a.scene_cover, least=a.scene_min,
                             count=lambda n: tick("cuts", n, est))])
            cells_at = {s0 + pre for s0 in cuts if s0}
            say("   coarse cells: %d scene change(s), each drawn in cells "
                "where it would be cut" % len(cells_at))
    if vfr and (lay is not None or a.live or not enc.metric):
        say("   --min-fps: not on a %s - %s" % (
            "layered file" if lay is not None else "Live file" if a.live
            else "format with no pixels to judge",
            "the layer's frames are the base's" if lay is not None else
            "it draws a pass a tick" if a.live else
            "a rate is chosen by the view as seen"))
        vfr = 0.0
    # FLIPPED (wave 3): every record is drawn on the page the glass is not
    # showing and then shown, so a hold drawn toward the next frame would
    # be SEEN half drawn. A flipped file's hold writes nothing: the back
    # page is brought up to the frame shown, which is shown again whole -
    # its period's bytes kept, and the lag schedule's catch-up the frame
    # after it starts level with
    vempty = bool(a.flip)
    # ...and with --flip-hold ahead (98.1.3.9), a HELD RECORD: drawn on the
    # page the glass is not showing toward the next frame, which finishes
    # it there and is shown - the held period's CPU spent on that picture.
    # A flipped file built ahead keeps the empty hold (its records' bit 15
    # is REC_FLIP), and so does a loop (the seam is a flipped record too)
    vstay = vempty and getattr(a, "flip_hold", "ahead") == "ahead" and \
        not pa_on and a.loop_from is None
    if vstay:
        vempty = False
    # WITH PAGE-AHEAD (VIDEO-SEEN-PLAN 3a, wave 2): the plan pass decides
    # the holds, as a plain encode does, and logs them (`H`), each frame
    # drawn after a hold a TARGET; the real passes hold exactly those and
    # decide none, and a held frame there writes NOTHING on the page shown
    # - the picture stays whole - leaving its period to the hidden page,
    # built toward the frame after it and flipped to
    vreplay = (plan.get("H") or set()) if vfr and plan is not None \
        else None
    if vreplay is not None and lsteps is not None:
        enc.vladder = dict(plan.get("LD") or {})  # (the plan's decisions)
        enc.vladat = dict(plan.get("LDA") or {})
    if vreplay is not None:
        enc.vrates = {}
        # (the SLOWED ones: the plan's stretches are every one a rung was
        # taken at, a lag's and a coarse cell's at the full rate too - so
        # with no slow rung a page-ahead file reported its ladder's
        # stretches "slowed to" its own rate, the owner's Gas Gas Gas's 22
        # at 18 fps with 0 frames held, 2026-10-19)
        for _, _, r in plan.get("S") or ():
            if r < round(fps, 1):
                enc.vrates[r] = enc.vrates.get(r, 0) + 1
    vspan = max(2, int(round(fps * VFR_SPAN)))
    vcost = float(getattr(a, "slow_cost", VFR_HOLD))
    enc.vplan = deque()
    if vreplay is None:
        enc.vrates = {}
    vstate = None                       # (the glass's screen, a STAY's run)

    def report(f):
        """Frame f's progress: the frames encoded, and the note"""
        if stream:
            progress(f)
        else:
            tick("encode", f, nf, doing[0])

    def busy(f, note):
        """...while frame f does something longer than a frame - the
        ladder's trials, the lag search - said, and cleared after"""
        doing[0] = note
        report(f)
    tpool = []                          # (the ladder's trials: TrialPool,
                                        # made at the first stretch decided)
    enc.meter = meter_for(enc, a)       # (measure's, in other processes)
    if enc.meter is not None:
        readers.append(enc.meter)       # (closed however encode ends)

    def trials():
        if not tpool:
            tpool.append(trial_pool(enc, a, 1 + max(
                [len(lsteps)] + [len(r[2]) for r in enc.vranges])))
            if tpool[0] is not None:
                readers.append(tpool[0])    # (closed however encode ends)
        return tpool[0]
    ppal = None                         # (a scene's palette held back)
    vprev, vshown = False, None         # (the frame before held; and the
                                        # target the picture shown is of)
    while True:
        while len(ahead) < 1 + max(enc.look, vspan - 1 if vfr else 0):
            t = next(targets, None)
            if t is None:
                break
            ahead.append(t)
        if not ahead:
            break
        f += 1
        target = ahead.popleft()
        if keep is not None:        # the targets frame for frame, the
            keep.append(target)     # pre-roll's included
        report(f)
        au = au_of(f)
        if a.resident:              # the sound is ONE block (98.1.7)
            sound.append(au)
            au = b""
        fut = list(ahead)[:enc.look]
        vfut = list(ahead) if vfr else ()
        if not enc.lag_cands or (pre and f <= pre):
            # (the pre-roll at the allowance preroll() counted it with)
            x = step(enc, pa, f, target, au, fut, vfut)
        else:
            # THE LAG, SEARCHED (98.2.1.1.3): a frame that starts level
            # with the sound is drawn at the range's top first - if it ends
            # level too, every allowance in the range draws it the same -
            # and one that ends behind is a BURST, whose allowance the
            # search picks from where it began; a burst longer than the
            # search's window is searched again from where it has got to
            if lag_since is not None and enc.flag <= 0:
                lag_since = None
            x = None
            if lag_since is None or lag_since >= LAG_WIN * fps:
                bufs = [target] + list(ahead)
                snap = copy.deepcopy((enc, pa), lag_seed((enc, pa), bufs))
                burst = lag_since is not None
                if not burst:
                    enc.flag_lag = enc.lag_cands[-1]
                    x = step(enc, pa, f, target, au, fut, vfut)
                    burst = enc.flag > 0
                if burst:
                    while len(ahead) < LAG_WIN * fps + 1 + enc.look:
                        t = next(targets, None)
                        if t is None:
                            break
                        ahead.append(t)
                    busy(f, "the lag search at frame %d: each allowance "
                         "tried on a copy of the encoder" % f)
                    L = lag_search(snap, f, [target] + list(ahead))
                    busy(f, "")
                    lag_since = 0
                    # (the top's frame is drawn already: kept, as the
                    # search played it from the same state)
                    if x is None or L != enc.lag_cands[-1]:
                        if x is not None:
                            lag_restore(enc, pa, snap, bufs)
                        enc.flag_lag = L
                        x = step(enc, pa, f, target, au, fut, vfut)
            else:
                x = step(enc, pa, f, target, au, fut, vfut)
            if lag_since is not None:
                lag_since += 1
        ops, rec, hops, draw, npal = (x[k] for k in ("ops", "rec", "hops",
                                                     "draw", "npal"))
        # THE PALETTE GOES ON WITH THE PICTURE (98.1.3.2.3, the owner's
        # GasGasQ, 2026-10-18: "flipping the palette one frame too early"):
        # page-ahead's replayed HOLD writes nothing on the page shown - the
        # hidden page builds the frame after it - so a scene's first frame
        # held there showed the OLD picture in the NEW sixteen for a frame,
        # 14 of 34 scene changes in that file. The player loads a palette
        # before it decodes the record, so the scene's palette goes on the
        # first frame after it that writes or flips the page shown. (The
        # plain encode's hold draws toward the next frame ON the page
        # shown, in the new colours, so there it stays where it was)
        if x["pal"] is not None:
            ppal = x["pal"]
        if ppal is not None and not (x["held"] and vreplay is not None
                                     and pa is not None):
            wr.pev.append((f, ppal[:48]))
            ppal = None
        if x["cell"] is not None:
            coarse_used[x["cell"]] = coarse_used.get(x["cell"], 0) + 1
        if scene_pals:              # (each scene's frames and cuts, below)
            if f in pev_at:
                shot_cut.append([f, 0, 0, 0])
            shot_cut[-1][1] += 1
            shot_cut[-1][2] += x["cut"]
        if x["left"] is not None:
            pev_cut.append(x["left"])
            pev_left.append((f, x["left"] / float(target.size)))
        cyc.append(x["c"] + audio_cyc)
        if plog is not None:            # THE PLAN (98.2.1.8): a cut run's
            plog["log"].append(dict(    # first frame is a target
                why=enc.last_why, b=len(rec) - len(au),
                ref=float(enc.last_ref or 0.0),
                floor=float(enc.disk_floor()) if enc.disk.per is not None
                else 0.0))
            # (a scene's start drawn in COARSE CELLS is one too: not cut,
            # but not drawn whole either, and a build ahead can show it so)
            short = (enc.last_why is not None or draw is not target) and \
                not x["held"]
            # ...and ON N PAGES every cut frame that is a CHANGE, PA_SCENE
            # of its pixels off the frame before (98.2.1.8.4): two scene
            # changes a few frames apart are one cut run on a slow machine,
            # and its first frame alone would leave the second to be cut
            if short and (not pcut or (
                    npg > 2 and ptgt is not None and np.count_nonzero(
                        target != ptgt) > PA_SCENE * target.size)):
                plog["T"][f] = target.copy()
                plog["P"][f] = getattr(enc, "pal", None)
            if vfr:                     # (the holds, and the frame each
                if x["held"]:           # run of them leads to: a target)
                    plog.setdefault("H", set()).add(f)
                elif vprev and f not in plog["T"]:
                    plog["T"][f] = target.copy()
                    plog["P"][f] = getattr(enc, "pal", None)
                    plog.setdefault("HT", set()).add(f)
                plog["S"] = list(enc.vstretch)
                if lsteps is not None:
                    plog["LG"], plog["CC"] = dict(enc.vlagat), \
                        dict(enc.vcellat)
                    plog["LD"] = dict(enc.vladder)
                    plog["LDA"] = dict(enc.vladat)
            pcut = short
            ptgt = target.copy() if npg > 2 else None
        if enc.bank:                # HOW DEEP THE BANK IS DRAWN (98.2.1.3.3)
            bl = min(max(0.0, enc.disk.level - enc.reserve), enc.bank)
            bspan[0] = min(bspan[0], bl)
            bspan[1] = max(bspan[1], bl)
        if enc.metric:
            # (a held frame against the picture it draws, as its rate was
            # chosen: it is the first half of the next one - or, holding
            # the page shown while the hidden page builds, against the
            # picture still shown)
            if x["stay"] and vstate is not None:
                # (a HELD record's frame shows the page before it: measured
                # so, against the target that picture was drawn for)
                drawn, enc.screen = enc.screen, vstate
                enc.scache = None
                enc.measure(vshown if vshown is not None else target)
                enc.screen, enc.scache = drawn, None
            else:
                enc.measure((x["hdraw"] if x["hdraw"] is not None else
                             vshown) if x["held"] and (
                                 x["hdraw"] is not None or
                                 vshown is not None) else target)
            if not x["held"]:
                vshown = target
                if vstay:
                    vstate = enc.screen.copy()
            if scene_pals:
                enc.when_bad(shot_cut[-1] if shot_cut else None)
        vprev = bool(x["held"])
        if pa is not None and a.flip:  # (98.1.3.7.2: every frame flips)
            wr.frame(ops, enc.surf, au, hidden=hops, flip=pa.vp,
                     shown=pa.vp, hsurfs=x["hs"], built=pa.built,
                     copies=pa.copies)
        elif pa is not None and npg > 2:  # (98.1.3.7.1: pages by number)
            wr.frame(ops, enc.surf, au, hidden=hops,
                     flip=pa.vp if pa.flipped else None, shown=pa.vp,
                     hsurfs=x["hs"])
        elif pa is not None:
            wr.frame(ops, enc.surf, au, hidden=hops, flip=pa.flipped,
                     hsurf=pa.h.surf)
        else:
            wr.frame(ops, enc.surf, au, held=x["stay"],
                     up=getattr(enc, "own_up", None))
            if getattr(enc, "own", False) and wr.keys and \
                    wr.keys[-1][0] == len(wr.recs) - 1:
                enc.own_next = True     # (the next brings its page up)
        if lay is not None:
            for lops in lay.push(
                    target, ops, rec, npal, hops=hops,
                    flip=pa is not None and pa.flipped,
                    pre=pa.pre_ops if pa is not None and f == pa.key0
                    else None, key=pa is not None and f == pa.key0):
                wr.layer_frame(lops)
        if a.preview_png and f % max(1, round(fps)) == 0:
            out = os.path.join(a.preview_png, "f%05d.png" % f)
            if c512:                # what a composite monitor shows: the
                import os88cgacomp  # card's, or for both old above new
                from PIL import Image
                cv = g.canvas(enc.surf)
                news = {vid.CARD_OLD: (False,), vid.CARD_NEW: (True,),
                        vid.CARD_BOTH: (False, True)}[cgapal]
                img = np.concatenate([np.repeat(os88cgacomp.render_c512(
                    cv, g.wb, g.h, n), 2, axis=0) for n in news])
                Image.fromarray(img).save(out)
            elif text:              # the cells in the model's face,
                import os88txtfont  # each dot made square
                from PIL import Image
                img = Image.fromarray(os88txtfont.render(
                    g.canvas(enc.surf), g.wb, g.h, np.frombuffer(
                        vid.STD16, np.uint8).reshape(16, 3).astype(
                        np.uint16) * 255 // 63))
                img.resize((img.width, round(img.height * 12 / 5)),
                           Image.NEAREST).save(out)
            elif cga4 or c160:
                from PIL import Image
                cv = np.frombuffer(g.canvas(enc.surf), np.uint8).reshape(
                    g.h, g.wb)
                idx = np.unpackbits(cv[..., None], axis=2).reshape(
                    g.h, g.wb * 8)
                bits = 2 if cga4 else 4
                idx = idx.reshape(g.h, -1, bits) @ (1 << np.arange(
                    bits - 1, -1, -1))
                if cga4:
                    idx = np.array(vid.cga4_colours(cgapal))[idx]
                pl = np.frombuffer(vid.STD16, np.uint8).astype(
                    np.uint16).reshape(-1, 3) * 255 // 63
                Image.fromarray(pl[idx].astype(np.uint8)).save(out)
            elif vga8 or vga4:
                from PIL import Image
                cv = np.repeat(np.frombuffer(g.canvas(enc.surf),
                                             np.uint8).reshape(g.h, g.w),
                               dh, axis=0)
                pl = np.frombuffer(((bytes(wr.pev[-1][1]) if scene_pals
                                     and getattr(wr, "pev", None) else
                                     palette) or vid.STD16)
                                   if vga4 else palette,  # (a scene's own:
                                   # the options audit, 2026-10-14)
                                   np.uint8).astype(
                    np.uint16).reshape(-1, 3) * 255 // 63
                Image.fromarray(pl[cv].astype(np.uint8)).save(out)
            elif comp:              # what a composite monitor shows
                import os88cgacomp
                from PIL import Image
                cv = np.frombuffer(g.canvas(enc.surf), np.uint8).reshape(
                    g.h, g.wb)
                Image.fromarray(os88cgacomp.render(
                    np.unpackbits(cv, axis=1))).save(out)
            else:
                vid.write_png(out, g.wb, g.h, g.canvas(enc.surf))
        # (on stderr, which no pass holds, so every pass says it - and with
        # --progress the window's lanes say it for each pass by name, where
        # two passes side by side put two unnamed lines in its log)
        if not a.quiet and not getattr(a, "progress", False) and \
                f % 300 == 299:
            print("   frame %d of %d" % (f + 1, nf if not stream else
                                         est + pre), file=sys.stderr)
    if ppal is not None:                # (a palette held to the end: on
        wr.pev.append((f, ppal[:48]))   # the last frame, as it was)
    if tpool and tpool[0] is not None:
        readers.remove(tpool[0])
        tpool[0].close()
    if enc.meter is not None:
        readers.remove(enc.meter)
        enc.meter.finish(enc)
    nf = f + 1
    if wr.keys and not a.resident and enc.cpu.per is not None:
        # THE BASE'S YARDSTICK (98.3.18.9): key 0's decode on the machine
        # this is made for - what a player on a machine with no card times
        # itself against, to give the bank the CPU the speaker would take
        # (in parts, 98.1.3.8: the LAST part, which is what the player has
        # in hand to time once the key is decoded)
        wr.ybase = max(1, int(round(key_bench(enc, vid.key_split(
            wr.keys[0][1])[-1], wr) / (
            vid.HZ * (prof.get("speed") or 1)) * 1e5)))
    if lay is not None:
        for lops in lay.flush():
            wr.layer_frame(lops)
        # THE BANK THE LAYER'S MACHINE KEEPS FOR THE BASE (98.1.9.1), whole
        # 32 KB slots: the player reads the layer before refilling the
        # base's bank only while the bank holds this much
        wr.lyneed = int(lay.T // 1024) if lay.T is not None else \
            int(-(-lay.uworst // 32768)) * 32
        # THE PLAYER'S YARDSTICK (98.1.9): key 0's decode, modelled on the
        # layer's machine and on the base's, in 10 us - the player times its
        # own decode of the same record and reads the layer only on a CPU
        # nearer the layer's machine than the base's
        if wr.keys:
            kr = vid.key_split(wr.keys[0][1])[-1]      # (98.1.3.8: as above)
            ts = tuple(min(65535, max(1, int(round(
                key_bench(mm, kr, wr) / (vid.HZ * sp) * 1e5))))
                for mm, sp in ((lay.m, lay.speeds[0]), (enc, lay.speeds[1])))
            wr.layer = wr.layer[:4] + ts
    if not a.resident and not a.live and enc.cpu.per is not None and \
            vid.bench_ok(wr.pixfmt, wr.g.layout, False, False):
        # THE BENCH (98.1.9.13): the record the PLAYER builds and times, so
        # the CPU test asks the same question of every file - modelled on
        # the machine this is for and on the layer's. Key 0's yardsticks
        # above stay, for a player made before
        brec = vid.bench_record((1 if wr.g.layout == vid.LAY_MODEX else 2)
                                if wr.g.planes > 1 else 0, wr.g.sub2)

        def bench_us10(mm, sp):
            return min(65535, max(1, int(round(
                mm.cost(brec) / (vid.HZ * sp) * 1e5))))
        wr.bench = (bench_us10(enc, prof.get("speed") or 1),
                    bench_us10(lay.m, lay.speeds[0]) if lay is not None
                    else 0)
    if enc.bank and getattr(a, "bank_auto", False):
        # --bank auto (98.2.1.3.3): the bank the encode DREW - its level
        # never left [lo, hi], so a bank of hi - lo, prefilled lo less,
        # moves the same bytes at the same frames. The file asks for that
        bk = int(-(-(bspan[1] - bspan[0]) // vid.SLOT)) * 32
        drawn = bk
        if a.prefill == "all":
            pre = vid.XPRE_ALL
        else:
            pre = max(0, int(-(-(enc.bankpre - bspan[0]) // 1024)))
        wr.xbank, wr.xpre = (bk, pre if bk else 0)
        a.auto_bank = (drawn, enc.bank // 1024)
        a.bank = float(bk) if bk else None
        if a.prefill != "all" and bk:
            a.prefill = "%g" % (pre * 1024.0 / prof["disk"])
        elif not bk:
            a.prefill = None
    if lay is not None:
        wr.lyneed = min(wr.lyneed, wr.xbank or 0)  # (the base's bank at most)
        # THE BASE'S DISK beside the layer's (98.1.9.3): the player times
        # its own and reads the layer only past the mean of the two
        # ...WITH NOTHING DECODING (98.1.9.6): the ring's fill is what the
        # player times, so the curve's 0% point, each machine's own
        idle = lambda m, r: r * (m.disk_rel(0.0) if m.dcurve else 1.0)
        wr.lybks = max(1, int(round(idle(enc, enc.drate) / 1024.0)))
        wr.lyidl = max(1, int(round(idle(lay.m, a.layer_disk) / 1024.0)))
        if getattr(a, "layer_bank_auto", False):
            lb = int(max(0.0, lpool - wr.lyneed) // 32 * 32)
            pre = vid.XPRE_ALL if a.layer_prefill == "all" else \
                min(int(lpre), int(lay.lmem) + lb)
            wr.layer = (wr.layer[0], wr.layer[1], pre, lb) + \
                tuple(wr.layer[4:])
            a.layer_bank = float(lb)
            a.auto_lbank = (lb, lpool)
    if getattr(a, "bank_auto", False) or getattr(a, "layer_bank_auto", False):
        wr.opts = vid.pack_options(options_doc(a, sfps))   # (as resolved)
    rep = Report(say)
    if text and (a.text_ocr or a.text_ocr_large):
        rep("base", "OCR: %d words drawn exact, %d crisp (a word counts once "
            "a frame)" % tuple(tm.ocr_used))
    tick("write", 0, 0)
    poster = a.poster
    if a.poster_at is not None:
        # THE KEYFRAME NEAREST a moment, not the moment: the poster is a
        # keyframe index (SPEC.md 98.1.1) and the player opens at frame 0
        # whatever it is - it only chooses the picture in the box
        nk = len(wr.keys)
        if nk:
            poster = min(nk - 1,
                         max(0, round(a.poster_at * fps / wr.keyint)))
        else:
            # no keyframe to point at (every one past its length word, or
            # none made): the file is written with none rather than lost
            # to an index of -1 after the whole encode
            rep("file", "NOTE: --poster-at %g: the file has no keyframes, "
                "so it has no poster (and no seek) - it plays from the start"
                % a.poster_at)
            poster = None
    desk_posters(a, wr, g, palette, lw, lh, crop, rate, spf, eq, rep)
    if a.resident:                      # (98.1.3.11: the Preview's posters)
        st = vid.write_resident(
            a.out, [wr], afmt, abytes if afmt else 0, b"".join(sound),
            wr.title, wr.credits, a.repeat, vid.PK_LZB,
            posters=[poster] if poster is not None else None,
            live=[vid.TARGETS[a.live]] if a.live else None,
            spk=spk and bool(afmt), opts=optsblk)
        tick("check", 0, 0, "every frame of the file played back")
        vid.verify_v88(a.out)
        rr = vid.Reader(a.out)
        res = dict(bytes=st["bytes"], stream=st["blocks"][0][0] +
                   st["audio"][0], keys=len(wr.keys),
                   keybytes=sum(len(r) for k, r, c in wr.keys),
                   poster=rr.poster)
        rep("file", "resident: the block %d bytes, %d packed%s"
            % (st["blocks"][0] + (", LIVE on the %s desktop" % a.live
                                  if a.live else "",)))
        if a.live:                  # 98.3.10.2: what the blits will cost
            bl = [enc.blitf * sum(vid.blit_cost(y1 - y0, x1 - x0,
                                                rr.g.planes)
                      for y0, y1, x0, x1 in vid.runs_of(
                          rec, vid.lists_end(rec, rr.g), rr.g)[0])
                  for rec, _, _ in rr.records()]
            rep("base", "Live's blit alone (model, a pass a tick): mean "
                "%.1f%%, worst %.1f%% of a frame's period"
                % (100 * sum(bl) / len(bl) / enc.period,
                   100 * max(bl) / enc.period))
    else:
        if ahead_auto and not enc.stats.get("leaned") and \
                not enc.stats.get("near"):
            wr.ahead = 0                # Auto, and no frame needed it
        if pa is not None:              # (98.2.1.8: keys that preload)
            pa.upkeys = pa.upgrade_keys(wr)
        res = wr.write(a.out, poster)
        if res["kleadcost"]:
            # a key that fitted one read and does not with its lead: seeking
            # is worth more than the lead - Auto drops it, On says so
            if a.buffer_sound_ahead == "auto":
                rep("file", "sound ahead turned off: a keyframe and its "
                    "lead would pass the %d bytes the player reads in one go"
                    % KEY_PLAYER)
                wr.ahead = 0
                res = wr.write(a.out, poster)
            else:
                rep("file", "NOTE: with its sound ahead a keyframe passes "
                    "the %d bytes the player reads in one go" % KEY_PLAYER)
        kd = res["kdropped"]
        if kd and not res["keys"]:
            rep("file", "NOTE: no keyframes - the first, frame %d, is %d "
                "bytes%s, past the 65,535 a table entry holds, and a later "
                "one would start the play there: it plays from the start and "
                "does not seek" % (kd[0][0], kd[0][1], " with its sound ahead"
                                   if wr.ahead and
                                   not getattr(wr, "kleads", True) else ""))
        elif kd:
            rep("file", "NOTE: %d keyframe%s left out (frame %s): past the "
                "%d bytes the player reads in one go%s, so a seek there "
                "lands on the keyframe before" % (
                    len(kd), "" if len(kd) == 1 else "s",
                    ", ".join(str(k) for k, n in kd[:6]) +
                    (", ..." if len(kd) > 6 else ""), KEY_PLAYER,
                    " with their sound ahead" if wr.ahead and
                    not getattr(wr, "kleads", True) else ""))
        if wr.ahead:                    # every lead and the laps' tails
            tick("check", 0, 0, "every frame of the file played back "
                 "against its stream, its sound ahead with it")
            vid.verify_v88(a.out)       # held to their frames (98.1.8)
    # THE FILE'S BYTES, BY WHAT READS THEM: the base's stream (its pictures
    # and its sound, what every machine plays), the layer (only the better
    # machine's, 98.1.9) and the rest - the header, the keyframes, the
    # sound-ahead leads, the options - which a play reads once, at a seek
    secs = nf / fps
    lyb = (res.get("layer") or {}).get("bytes", 0)
    abts = audio_bps * secs
    rest = res["bytes"] - res["stream"] - lyb
    # (no rate for the whole file: the front matter is read once, one key
    # and its lead at a seek, so bytes over seconds is no play's rate)
    rep.head("file", "%d frames, %.1f s: %s bytes" % (
        nf, secs, "{:,}".format(res["bytes"])))
    rep("file", "base stream %.1f KB/s - the picture %.1f, the sound %.1f%s"
        % (res["stream"] / 1024.0 / secs,
           (res["stream"] - abts) / 1024.0 / secs, audio_bps / 1024.0,
           " (%s at %d Hz)" % ({1: "PCM8", 2: "ADPCM4"}[afmt], rate)
           if afmt else ""))
    if lyb:
        rep("file", "layer %.1f KB/s (%s bytes) - read only by a machine "
            "that plays it, past the base" % (lyb / 1024.0 / secs,
                                             "{:,}".format(lyb)))
    rep("file", "front matter %s bytes (%.1f%%) - the header, the %d "
        "keyframe%s (%s bytes; poster %s)%s" % (
            "{:,}".format(max(0, rest)), 100.0 * max(0, rest) / res["bytes"],
            res["keys"], "" if res["keys"] == 1 else "s",
            "{:,}".format(res["keybytes"]), res["poster"],
            " and the sound-ahead leads" if res.get("leadbytes") else ""))
    if not a.resident:
        nm = enc.stats.get("nearmin")
        slow = "%d%% under it" % round(100 * AHEAD_SLACK)
        close = "" if nm is None else \
            "; the closest frame there had %.1f KB to spare" % (
                nm / 1024.0) if nm >= 0 else \
            "; the worst frame there %.1f KB short" % (-nm / 1024.0)
        rep("file", "sound ahead: %s" % (
            "%d frames, %d bytes (%s)" % (
                wr.ahead, res.get("leadbytes", 0),
                ("%d frames needed it" % enc.stats["leaned"]
                 if enc.stats.get("leaned") else
                 "no frame needed it at the profile's disk, but %d would "
                 "at %s%s" % (enc.stats.get("near", 0), slow, close))
                if ahead_auto else "no disk modelled" if
                a.buffer_sound_ahead == "auto" else "asked for")
            if wr.ahead else "off" + (
                " - no frame needed it, at the profile's disk or %s%s"
                % (slow, close) if ahead_auto else
                "" if a.buffer_sound_ahead == "off" else
                " - the file streams no sound" if not ahead else "")))
    res.update(fps=fps, period=enc.period, audio_cyc=audio_cyc,
               spk_share=spk_share,
               audio_bps=audio_bps, prof=prof, w=w, h=h, layout=lay,
               palette=palette)
    # the header's own figure, which counts what write() appended - an
    # ADPCM4 key's reference byte (98.1.1.1) is not in wr.keys - and past
    # the read only for the FIRST key, write() leaving any other out
    kmax = vid.Reader(a.out).kmax
    if kmax > KEY_PLAYER:
        rep("file", "NOTE: the %s keyframe is %d bytes, past the %d the "
            "player reads in one go off a volume of %d KB clusters (a hard "
            "disk%s): there it will play from the start only, with no "
            "poster and no seek"
            % ("largest" if a.resident else "first", kmax, KEY_PLAYER,
               KEY_CLB // 1024,
               "; a floppy's 1 KB ones take %d" % key_limit(1024)
               if kmax <= key_limit(1024) else ", or a floppy"))
    if getattr(a, "spk_preview", None) and spk and afmt:
        rp = vid.Reader(a.out)          # WHAT THE 5150's SPEAKER LINE WILL
        psecs = vid.write_spk_preview(  # CARRY (98.2.15.2), carrier and all
            a.spk_preview, b"".join(rp.sound(f)
                                    for f in range(rp.frames)),
            rp.rate, rp.spkp)
        rep("file", "speaker preview: %s, %.1f s" % (a.spk_preview, psecs))
    st = enc.stats
    # THE BASE: what every machine plays, against the profile it was made
    # for. A profile that measured its machine's decode (98.2.3.3) is priced
    # by it, and says so; the percentages stay an 8088's, with the machine's
    # own share beside them
    spd = prof.get("speed") or 1
    rep.head("base", "for %s" % machine_name(a.profile, prof["disk"]))
    rep("base", "CPU (%s, audio copy in): mean %.1f%%, worst %.1f%%%s%s"
        % ("the %s's measured decode" % a.profile
           if prof.get("cyc_us") else "wave 0 model",
           100 * sum(cyc) / nf / enc.period,
           100 * max(cyc) / enc.period,
           " of an 8088 - %.0f%% / %.0f%% of this machine"
           % (100 * sum(cyc) / nf / enc.period / spd,
              100 * max(cyc) / enc.period / spd) if spd != 1 else "",
           " + the speaker's %.0f%%" % (100 * spk_share)
           if spk_share else ""))
    rep("base", "frames: %d exact, %d cut to the budget (%.1f bytes a "
        "frame left wrong)%s" % (
            st["exact"], st["cut"], st["bytes_left"] / max(1, st["cut"]),
            "; cut by the disk %d, the CPU's average %d, the per-frame "
            "ceiling %d,%s the %s KB a record holds %d" % (
                st["disk"], st["cpu"], st["peak"],
                " the lag schedule %d," % st["flip"] if st.get("flip")
                else "", a.frame_cap, st["cap"])
            if st["cut"] else ""))
    for ri, (k0, k1, _, kr, tx) in enumerate(enc.vranges):
        got = {li: c for (r_, li), c in enc.vladat.items() if r_ == ri}
        rep("base", "degrade ladder at %s s: %s (--ladder-at, 98.2.1.10)" % (
            tx, ", ".join(
                ("%d drawn as they were" % c) if i == 0 else
                ("%d at rung %d (%s)" % (c, i, ", ".join(
                    "%s:%s" % (kk, ("%.3g" % vv) if kk == "slow" else
                               ("%g" % vv) if kk == "lag" else
                               ("%g%%" % vv) if kk == "cut" else
                               "frame" if vv == "auto" else vv)
                    for kk, vv in kr[:i])))
                for i, c in sorted(got.items())) or
            "no stretch the budgets would cut starts there"))
    if lsteps is not None and enc.vladder:
        tot = sum(enc.vladder.values())
        rep("base", "degrade ladder: %d stretch(es) the budgets would cut - "
            "%s (--ladder %s, VIDEO-SEEN-PLAN 3a)" % (
                tot, ", ".join(
                    ("%d drawn as they were" % c) if i == 0 else
                    ("%d at rung %d (%s)" % (c, i, ", ".join(
                        "%s:%s" % (kk, ("%.3g" % vv) if kk == "slow" else
                                   ("%g" % vv) if kk == "lag" else
                                   ("%g%%" % vv) if kk == "cut" else
                                   "frame" if vv == "auto" else vv)
                        for kk, vv in enc.vkeep[:i])))
                    for i, c in sorted(enc.vladder.items())),
                getattr(a, "ladder", "")))
    if vfr:
        rep("base", "variable frame rate: %s; %d frames held, each %s "
            "(--min-fps %g, VIDEO-SEEN-PLAN 3a)"
            % ("%d stretches slowed - %s" % (
                sum(enc.vrates.values()), ", ".join(
                    "%d to %g fps" % (enc.vrates[r], r)
                    for r in sorted(enc.vrates, reverse=True)))
               if enc.vrates else "no stretch slowed - every cut drew "
               "better at the full rate", st.get("holds", 0),
               "showing the picture before it again" if vempty else
               "drawn on the page the glass is not showing toward the "
               "frame after it, which is shown (98.1.3.9)" if vstay else
               "the hidden page's to build the frame after it"
               if vreplay is not None else
               "drawing toward the frame after it", vfr))
    if st.get("unfit"):
        rep("base", "frames left empty: %d - the cutting did not bring them "
            "under the %d bytes a record holds, so each drew nothing and the "
            "frames after it caught up, rather than the encode ending"
            % (st["unfit"], enc.rec_max))
    if pa is not None and a.flip:
        rep("base", "page-ahead FLIPPED ON %d PAGES: %d of %d cut scene "
            "changes built ahead on %d builders and flipped to, %d of them "
            "onto a back page the calm copied, %d with a copy in the flip's "
            "record (%d rows), %d refused for a copy that did not fit; %.1f%% "
            "of the model's cycles and %d KB for them (98.2.1.8.5)" % (
                npg, pa.flips, len(pa.targets), npg - 2, pa.precopied,
                pa.ncopy, pa.crows, pa.copyrefused,
                100.0 * pa.cyc / max(1.0, sum(cyc)), pa.bytes / 1024))
    elif pa is not None and npg > 2:
        rep("base", "page-ahead ON %d PAGES: %d of %d cut scene changes built "
            "ahead on %d hidden pages and flipped to, %.1f%% of the model's "
            "cycles and %d KB for them (98.2.1.8.4); key 0 preloads %d "
            "pages' targets, %d targets had no page free in time, and keys "
            "left %d idle pages out" % (
                npg, pa.flips, len(pa.targets), npg - 1,
                100.0 * pa.cyc / max(1.0, sum(cyc)), pa.bytes / 1024,
                getattr(pa, "npre", 0),
                pa.unbuilt, pa.dropped))
    elif pa is not None:
        rep("base", "page-ahead: %d of %d cut scene changes built ahead on "
            "the hidden page and flipped to, %.1f%% of the model's cycles "
            "and %d KB for it (98.2.1.8); key 0 preloads %s, and %d later "
            "keys hold their next flip whole" % (
                pa.flips, len(pa.targets),
                100.0 * pa.cyc / max(1.0, sum(cyc)), pa.bytes / 1024,
                "%.0f%% of the first" % (100.0 * pa.preloaded /
                                         pa.T[pa.targets[0]].size)
                if hasattr(pa, "preloaded") else "nothing",
                getattr(pa, "upkeys", 0)))
    if scene_pals:
        # A PALETTE A SCENE (98.1.3.2.3): a scene's first frame is drawn in
        # its new colours over the last scene's picture, so one the budget
        # cut leaves part of the old picture standing in colours not its own
        # until a later frame fits - counted, to be looked at
        whole = sum(1 for v in pev_cut if not v)
        rep("base", "scenes: %d palettes; %d of %d scene changes drawn "
            "whole%s" % (len(wr.pev), whole, len(pev_cut),
                         ", the rest leaving %.0f pixels of the last scene "
                         "a change on average" % (
                             sum(pev_cut) / max(1, len(pev_cut) - whole))
                         if whole < len(pev_cut) else ""))
        # ...and WHERE (98.1.3.2.3.3): each change that left a share of the
        # picture, at the time and the frame the encoder window's slider
        # shows - which --scene-join takes (its own count is the scene
        # pass's, the pre-roll's few frames sooner: it takes the nearest)
        bad = [(f, sh) for f, sh in pev_left if sh >= PEV_RESIDUE]
        if bad:
            rep("base", "scene changes leaving the last scene up in the new "
                "colours: %s%s - --scene-join TIMES makes one a scene of "
                "both (98.1.3.2.3.3)" % (", ".join(
                    "%.2f s %.0f%% (frame %d)" % (f / fps, 100 * sh, f + 1)
                    for f, sh in bad[:PEV_SHOW]),
                    ", and %d more" % (len(bad) - PEV_SHOW)
                    if len(bad) > PEV_SHOW else ""))
        if lay is not None and not lay.track:
            # ...and WITH THE LAYER (98.1.3.2.3.1): what it finishes of a
            # change the base cut, on the better machine
            lc = lay.pev_cut
            lwhole = sum(1 for v in lc if not v)
            rep("base", "scenes with the layer: %d of %d drawn whole%s" % (
                lwhole, len(lc), ", the rest leaving %.0f pixels a change on "
                "average" % (sum(lc) / max(1, len(lc) - lwhole))
                if lwhole < len(lc) else ""))
    if coarse_used:
        rep("base", "coarse cuts: %s (--coarse-cut %s)" % (", ".join(
            "%d %s" % (coarse_used[c], "whole" if c == "full" else c)
            for c in COARSE_LADDER + tuple(sorted(set(coarse_used) -
                                                  set(COARSE_LADDER)))
            if c in coarse_used), a.coarse_cut))
    if scene_pals and st["cut"]:
        # WHERE THE CUTS ARE, scene by scene (VIDEO-PALETTE-PLAN 10.9): a
        # step before cutting is chosen per scene, so its cost is read there
        # ...RANKED BY WHAT IS SEEN: the frames VISIBLY BROKEN, then the
        # frames cut. A cut is not equally visible everywhere - a slow zoom
        # cut every other frame leaves pixels a frame behind and nearly
        # right, where a whip pan's cut leaves the last shot standing - and
        # the count of cuts put the zoom first on Gas Gas Gas, which the
        # owner, watching, saw nothing wrong in
        heavy = sorted((x for x in shot_cut if x[2] or x[3]),
                       key=lambda x: (-x[3], -x[2]))
        rep("base", "cut by scene (start, frames, cut, visibly broken), "
            "the most broken first: %s"
            % ", ".join("%d:%04.1f %d %d %d" % (x[0] / fps // 60,
                                                x[0] / fps % 60, x[1], x[2],
                                                x[3])
                        for x in heavy[:40]) +
            (" and %d more" % (len(heavy) - 40) if len(heavy) > 40 else ""))
    cc = getattr(enc, "capcuts", [])
    if cc:
        # WHERE THE RECORD's ROOM CUT (98.1.4.1): every such frame is
        # drawn with part of it left from the frame before - a scene change
        # or a fast pan - so they are named, by time, to be found and looked
        # at; --frame-cap 48 or 63.5 is what takes them away
        rep("base", "the record's %s KB cut %d frame%s - part of each left "
            "from the frame before, until a later frame fits: %s%s" % (
                a.frame_cap, len(cc), "" if len(cc) == 1 else "s",
                ", ".join("%d:%04.1f (%d)" % (f / fps // 60, f / fps % 60, f)
                          for f in cc[:12]),
                ", and %d more" % (len(cc) - 12) if len(cc) > 12 else ""))
    if st["skipped"]:
        rep("base", "aim size: %d changes not worth their bytes left off"
            % st["skipped"])
    if enc.owe is not None:
        rep("base", "owed time: %d frames ran past their period, %d drawn a "
            "period late while it was paid back, %d dropped"
            % (st["owed"], st["held"], st["late"]))
    if getattr(enc, "fgrid", None):
        rep("base", "lag schedule: %d frames ran past their period, the "
            "picture %.2f of a frame (%d ms) behind the sound at most, %d "
            "frames a whole one behind or more%s, %d past it (98.2.1.1.1, "
            "98.2.1.1.2)"
            % (st["fover"], st["fmaxlag"], st["fmaxlag"] * 1000.0 / fps,
               st.get("fbehind", 0),
               " - the degrade ladder's lag, raised a stretch at a time "
               "from a frame" if lsteps is not None and any(
                   kk == "lag" for kk, vv in enc.vkeep) else
               " - --lag %s searched for each burst" % a.lag
               if enc.lag_cands else
               " - --lag allows %.1f" % enc.flag_lag
               if enc.flag_lag > 1 else
               " - --lag %s ms is held to a frame: calls a period apart "
               "cannot catch up" % a.lag if hi_ms * fps / 1000.0 > 1 else "",
               st["flate"]))
    if enc.lag_cands:
        # THE SEARCH'S ANSWERS (98.2.1.1.3): each burst's allowance, and
        # what it cost the encoder to find them
        chose = {}
        for k0, L, n, tried in lag_log:
            chose[L] = chose.get(L, 0) + 1
        gr = enc.fgrid / enc.q          # (an allowance's most behind:
        most = lambda L: round((L - gr) * 1000.0 / fps)     # a call short)
        rep("base", "lag search: %d burst%s searched at --lag %s, the "
            "picture at most %s ms behind; chose %s; %d frames played to "
            "choose (%.1f x the clip) (98.2.1.1.3)" % (
                len(lag_log), "" if len(lag_log) == 1 else "s", a.lag,
                "/".join("%d" % most(L) for L in enc.lag_cands),
                ", ".join("%d ms x%d" % (most(L), n)
                          for L, n in sorted(chose.items())) or "none",
                lag_simf, lag_simf / max(1, nf)))
        dump = os.environ.get("OS88_VENC_LAGDUMP")
        if dump:                        # (each search, for the A/B)
            import json
            with open(dump, "w") as fh:
                json.dump(dict(fps=fps, cands=enc.lag_cands, log=lag_log),
                          fh)
    if getattr(wr, "ring", 0):
        msl = 2 if res["flags"] & vid.F_BIGSP else 1
        rep("base", "ring: a disk reserve of %d KB, %d slots - %d KB of free "
            "memory to play it (98.2.1.3.1)%s" % (
                enc.reserve // 1024, wr.ring,
                play_memory_kb(wr.ring, msl, bool(afmt)),
                ", and the canvas's %d KB besides for the page flip"
                % -(-(g.wb * 4 if L == vid.LAY_MODEX else
                      80 * 4 if g.bitplanes else g.wb) * g.h // 1024)
                if a.flip else ""))
    if wr.xbank or getattr(a, "auto_bank", None):
        rep("base", "XMS bank: %s%s" % (
            "%d KB ahead of the ring, %s before the first frame (98.3.18.2) "
            "- a machine with less plays it and says Low memory" % (
                wr.xbank, "all of it filled" if wr.xpre == vid.XPRE_ALL
                else "%d KB of it filled, ~%.0f s of the disk" % (
                    wr.xpre, wr.xpre * 1024.0 / prof["disk"]))
            if wr.xbank else "none - the disk carries it alone",
            "; AUTO (98.2.1.3.3), encoded against %d KB and drew %d"
            % (a.auto_bank[1], a.auto_bank[0])
            if getattr(a, "auto_bank", None) else ""))
    elif prof["disk"] and not a.resident and not a.live:
        rep("base", "XMS bank: none")
    if enc.metric:
        pm, pw, pat, pbr = picture_report(enc.q_frames, enc.q_bad, fps)
        rep("base", "picture: %.2f%% error as seen, %.2f%% of pixels "
            "wrong, %.0f pixels a frame flickering back (the source's own: "
            "%.0f)" % (100 * enc.q_vis / nf, 100 * enc.q_wrong / nf,
                       enc.q_flick / max(1, nf - 2),
                       enc.q_tflick / max(1, nf - 2)))
        rep("base", "picture at its worst second %.2f%% (from %d:%04.1f); "
            "%.1f s of %.1f VISIBLY BROKEN - %d%% of the frame or more off "
            "by %d%% as seen, the picture torn by a cut and left so" % (
                100 * pw, int(pat / fps) // 60, pat / fps % 60, pbr / fps,
                nf / fps, round(100 * VIS_AREA), round(100 * VIS_BAD)))
        sp, nsp = broken_spans(enc.q_bad, fps)
        if sp:
            rep("base", "picture broken at %s%s" % (", ".join(
                "%d:%04.1f for %.1f s" % (int(t) // 60, t % 60, ln)
                for t, ln in sp), " and %d more" % (nsp - len(sp))
                if nsp > len(sp) else ""))
        if enc.q_seen:
            qs = enc.q_seen
            wi = max(range(len(qs)), key=qs.__getitem__)
            nb = sum(1 for v in qs if v >= SEEN_BAD)
            rep("base", "the view as seen: %.1f%% of it broken on average, "
                "%.1f%% at its worst (%d:%04.1f); %.1f s of %.1f with a "
                "tenth or more of it broken (VIDEO-SEEN-PLAN)" % (
                    100 * sum(qs) / len(qs), 100 * qs[wi],
                    int(wi / fps) // 60, wi / fps % 60, nb / fps, nf / fps))
            res.update(q_seen=sum(qs) / len(qs), q_seen_worst=qs[wi],
                       q_seen_broken=nb / fps)
        res.update(q_worst=pw, q_worst_at=pat / fps, q_broken=pbr / fps)
    if lay is not None:
        # THE LAYER: what only the better machine reads, against ITS
        # profile, in the base's words - CPU, frames, bank, picture
        rep.head("layer", "for %s" % machine_name(
            getattr(a, "layer_profile", None), a.layer_disk))
        rep("layer", "stream %.1f KB/s, in %d of %d frames"
            % (lay.bytes / secs / 1024.0, lay.recs, nf))
        lunfit = sum(e.stats.get("unfit", 0) for e in (lay.e, lay.eh)
                     if e is not None)
        if lunfit:
            rep("layer", "records left empty: %d - past the %d bytes a "
                "layer record holds after the cutting, so each added nothing "
                "and the next caught up" % (lunfit, lay.e.rec_max))
        rep("layer", "CPU: %.1f%% more of that machine's, past the base's"
            % (100.0 * lay.cycles / max(1.0, lay.m.cpu.per * nf)))
        # WHAT THE BETTER MACHINE NEEDS FOR THE BASE (98.1.9.1): with no
        # bank at all its disk carries the base, or it wants this much of
        # one - and the layer read ahead of that bank, held back where the
        # base's next seconds needed it
        bkb = lay.uworst / 1024.0
        if lay.T is not None:               # (two passes: the first's need,
            bkb = lay.T / 1024.0            # its layer the real one's)
        rep("layer", "the base on that machine: %s%s" % (
            "out of its own disk, no bank needed" if not bkb else
            "carried only with %d KB of the base's bank (%d frames past its "
            "ring alone)%s" % (-(-bkb // 32) * 32, lay.ushort,
                               "" if enc.bank else ": the base has none, so "
                               "they wait there"),
            "; the layer held back in %d frames for the bank's refill"
            % lay.held if lay.held else ""))
        rep("layer", "bank: %d KB%s%s" % (
            wr.layer[1] + wr.layer[3],
            ", %s of it filled before the first frame" % (
                "all" if a.layer_prefill == "all" else
                "%d KB" % wr.layer[2]) if wr.layer[2] else "",
            "; AUTO, %d KB of that machine's %d once it keeps %d KB for "
            "the base (%s)" % (
                a.auto_lbank[0], a.auto_lbank[1], wr.lyneed,
                "from the first pass" if lay.T is not None else
                "learned in one pass")
            if getattr(a, "auto_lbank", None) else ""))
        if getattr(wr, "lyidl", 0) > getattr(wr, "lybks", 0):
            # THE PLAYER'S DISK TEST (98.1.9.6): it times the ring's fill,
            # which runs with NOTHING decoding - so the line is drawn on
            # each machine's IDLE disk, the profile's curve at 0%, and not
            # on the play-time rates the headings give. Said with both
            # conversions, or the line looks like a figure nobody asked for
            line = max((wr.lyidl + wr.lybks) // 2,
                       wr.lyidl - (wr.lyidl >> 4))
            rep("layer", "disk test (98.1.9.6): the player reads the layer "
                "only where its ring fills at %d KB/s or more - timed with "
                "NOTHING decoding, so on each machine's idle disk and not "
                "the play-time rates above: by their profiles' disk curves "
                "this machine's %.0f KB/s is %d idle%s and the base's %.0f "
                "is %d. The line is 15/16 of this machine's (or the mean of "
                "the two, if more); slower, it plays as the base's machine"
                % (line, a.layer_disk / 1024.0, wr.lyidl,
                   " (the curve scaled to the disk given, %.0f%% of the "
                   "profile's)" % (100.0 * a.layer_disk / PROFILES[
                       a.layer_profile]["disk"])
                   if getattr(a, "layer_profile", None) and
                   a.layer_disk < PROFILES[a.layer_profile]["disk"] else "",
                   prof["disk"] / 1024.0, wr.lybks))
        if wr.bench[1]:
            # THE BENCH (98.1.9.13), and how far its two machines' ratio is
            # from this file's own frames': the residual a fixed mix leaves
            fb = sum(enc.cost(r) for r in wr.recs) / lay.speeds[1]
            fl = sum(lay.m.cost(r) for r in wr.recs) / lay.speeds[0]
            br, fr = wr.bench[0] / float(wr.bench[1]), fb / fl
            rep("layer", "CPU test (98.1.9.13): the player's bench decodes "
                "in %.2f ms on that machine and %.2f on the base's by the "
                "model - %.2fx apart, against %.2fx for this file's own "
                "frames (%+.0f%%)%s" % (
                    wr.bench[1] / 100.0, wr.bench[0] / 100.0, br, fr,
                    100 * (br / fr - 1),
                    "" if abs(br / fr - 1) <= BENCH_WARN else
                    "; NOTE: past %d%%, so a machine near the line is "
                    "judged by the bench's mix more than by this file's"
                    % (100 * BENCH_WARN)))
        elif len(wr.layer) > 5 and wr.layer[4] < LY_TMIN:
            # A KEY TOO SMALL TO TELL A CPU BY (98.1.9.12): a black key 0 is
            # modelled in microseconds on both machines, and the player's
            # bench then gives no verdict - so say who decides instead
            disk = getattr(wr, "lyidl", 0) > getattr(wr, "lybks", 0)
            rep("layer", "CPU test: none - key 0 decodes in %.2f ms on that "
                "machine and %.2f on the base's by the model, under the %d ms "
                "the player's bench can tell a CPU by%s"
                % (wr.layer[4] / 100.0, wr.layer[5] / 100.0, LY_TMIN // 100,
                   ", so the disk test above decides alone" if disk else
                   "; with no disk test either it has no verdict and plays "
                   "the BASE (98.1.9.7) - a key 0 with a picture in it gives "
                   "it one"))
        if enc.metric:
            lpm, lpw, lpat, lpbr = picture_report(lay.e.q_frames,
                                                  lay.e.q_bad, fps)
            rep("layer", "picture with it: %.2f%% error as seen, at its "
                "worst second %.2f%%, %.1f s visibly broken"
                % (100 * lay.e.q_vis / nf, 100 * lpw, lpbr / fps))
        if lay.bshort and lay.T is None:
            rep("layer", "NOTE: one pass - in %d frames the layer was made "
                "on disk the base's bank turned out to need. The player "
                "knows that need from the first frame and keeps the base "
                "whole, so it is THE LAYER that is short there: those "
                "records may come too late to be drawn, leaving the last "
                "one's pixels. Two passes (--layer-passes two) leave none"
                % lay.bshort)
        elif lay.bshort:
            rep("layer", "NOTE: the base's bank ran dry on the layer's "
                "machine in %d frames: the layer was held back for too short "
                "a window" % lay.bshort)
        res.update(layer=dict(kbs=lay.bytes / secs / 1024.0,
                              q_vis=lay.e.q_vis / nf if enc.metric
                              else None, recs=lay.recs,
                              ubank=-(-bkb // 32) * 32 if lay.ushort else 0,
                              need=wr.lyneed, short=lay.bshort,
                              dry=int(-(-lay.bdworst // vid.SLOT)) * 32,
                              trace=lay.trace if lay.track else None,
                              pevs=lay.pevs, pev_cut=lay.pev_cut,
                              final=g.canvas(lay.e.surf), sums=lay.sums))
    rep.flush()
    if enc.metric and os.environ.get("OS88_VENC_QDUMP"):
        import json                     # (the per-frame figures, for a
        with open(os.environ["OS88_VENC_QDUMP"], "w") as fq:  # look at a
            json.dump(dict(fps=fps, mean=enc.q_frames, bad=enc.q_bad,  # clip's
                           layer=(lay.e.q_frames, lay.e.q_bad)  # worst)
                           if lay is not None else None,
                           seen=enc.q_seen), fq)
    res.update(q_vis=enc.q_vis / nf, q_wrong=enc.q_wrong / nf,
               q_flick=enc.q_flick / max(1, nf - 2),
               # (a HELD frame is not drawn on time either: --min-fps
               # turns cuts into holds, and counted apart a file that held
               # read as cutting less than one that drew - VIDEO-SEEN-PLAN
               # 3a, wave 2)
               cutf=(st["cut"] + st.get("holds", 0)) / nf,
               cuts=st["cut"], holds=st.get("holds", 0),
               ladder=dict(enc.vladder) if lsteps is not None else None,
               ladder_at=dict(enc.vladat), vstretch=list(enc.vstretch),
               pev_left=list(pev_left) if scene_pals else [],
               cells=dict(coarse_used))
    if enc.metric and enc.q_frames:     # (98.2.1.8.1's comparison)
        pr = picture_report(enc.q_frames, enc.q_bad, fps)
        res.update(q_worst=pr[1], q_broken=pr[3] / fps)
        if pa_on:                       # (...and page_hurt's, frame by
            res["q_frames"] = list(enc.q_frames)    # frame)
            res["q_seenf"] = list(enc.q_seen)
    if scene_pals:
        # EACH SCENE as this pass made it, and where its source is: what the
        # rate ladder chooses from (--step-down, VIDEO-PALETTE-PLAN 10.12)
        res["shots"] = [dict(f=x[0], frames=x[1], cut=x[2], broken=x[3])
                        for x in shot_cut]
        res["scenegeom"] = dict(lw=lw, lh=lh, crop=crop, rate=rate, spf=spf,
                                eq=":".join(eq), pre=scene_pre,
                                src0=[s0 for s0, _ in scene_pals],
                                clip=kclip, sparse=ksparse)
    if plog is not None:
        res["paplan"] = plog
    if pa is not None:
        res["page"] = dict(flips=pa.flips, lost=pa.lost, cyc=pa.cyc,
                           bytes=pa.bytes, targets=len(pa.targets),
                           start=dict(pa.start), pages=npg,
                           dropped=getattr(pa, "dropped", 0),
                           precopied=getattr(pa, "precopied", 0),
                           copies=getattr(pa, "ncopy", 0),
                           refused=getattr(pa, "copyrefused", 0))
    return res


def parser():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("src")
    ap.add_argument("out", help="a .V88; or a .WAV for Audio - the sound "
                    "alone, shaped for the PC speaker as --audio speaker "
                    "would and stored as its counts (SPEC.md 86.21.1)")
    ap.add_argument("--preset", choices=sorted(PRESETS),
                    help="a named box on a layout (--profiles lists them); "
                         "the cga4, cga4-small and c160 ones name their "
                         "colour format too")
    ap.add_argument("--layout", choices=sorted(vid.LAYOUT_BY_NAME),
                    type=vid.layout_name,
                    help="the screen memory the file is laid out for "
                         "(SPEC.md 98.1.2), overriding the preset's")
    ap.add_argument("--box", type=dims,
                    help="WxH (or W*H): the canvas's largest size")
    ap.add_argument("--fit", choices=("fit", "fill", "stretch"),
                    default="fit",
                    help="fit: the whole picture, the canvas shrunk to its "
                         "shape; fill: the box filled, the picture cropped; "
                         "stretch: the box filled, the picture distorted")
    ap.add_argument("--start", type=float, default=0.0,
                    help="seconds into the source to start at")
    ap.add_argument("--end", type=float,
                    help="seconds into the source to stop at (default: "
                         "its end)")
    ap.add_argument("--fps", type=float,
                    help="frames a second (default: the source's, at most "
                         "30 - 15 for 256 colours, 25 for the vga8 and modex "
                         "presets)")
    ap.add_argument("--profile", choices=sorted(PROFILES), type=profile_name,
                    default="5150-st225",
                    help="the machine's storage and CPU budget, which every "
                         "frame is fitted to (--profiles says what each is)")
    ap.add_argument("--disk", type=float, help="the profile's bytes a "
                    "second, overridden")
    ap.add_argument("--avg", type=avg_arg, default="auto",
                    help="...its average CPU share, or AUTO (the default): the per-frame ceiling's on a profile whose disk curve already prices the reader, the profile's own share on one without (98.2.1.5)")
    ap.add_argument("--peak", type=float, help="...its per-frame ceiling")
    ap.add_argument("--aim", choices=("asked", "quality", "size"),
                    default="asked",
                    help="what to do with a budget the video does not use "
                         "(98.2.1.4): nothing (asked, the default); QUALITY "
                         "- try the preset's bigger boxes and 22 kHz sound "
                         "and take what the budget still carries, found by "
                         "trial encodes; or SIZE - leave off changes not "
                         "worth their bytes even when the budget would take "
                         "them, the smallest file for the look")
    ap.add_argument("--worth", type=float, metavar="N",
                    help="--aim size's floor: pixels a change must fix, over "
                         "the look-ahead, per byte of stream (default %g; "
                         "higher is smaller and rougher)" % AIM_WORTH)
    ap.add_argument("--reserve", type=float, metavar="KB",
                    help="the disk RESERVE: how much of the stream the "
                         "player's ring banks ahead in a calm stretch, to "
                         "spend in a burst (98.2.1.3). Default: the "
                         "profile's ring less two slots - 192 KB, which "
                         "wants 288 KB of memory to play; less plays in "
                         "less memory")
    ap.add_argument("--memory", type=float, metavar="KB",
                    help="FREE MEMORY TO PLAY: what the player may take to "
                         "read ahead - its ring of 32 KB slots, the sound's "
                         "ring and a seek's - and so how big a burst the "
                         "stream may bank for (98.2.1.3.1). Default: the "
                         "profile's ring of 8, ~309 KB. More plays better on "
                         "a machine that has it, and one with less says Low "
                         "memory and may pause in a burst. Not with "
                         "--reserve, which says the same thing in the "
                         "stream's KB")
    ap.add_argument("--bank", type=bank_arg, metavar="KB|auto",
                    help="an XMS BANK on the machine that plays it: a 286 "
                         "or better with that much extended memory free "
                         "keeps the stream that far ahead of its ring, "
                         "refilled in the calm stretches, so a burst can "
                         "spend it (98.3.18.2). The disk bucket is that "
                         "much deeper. The file says so in its header and "
                         "still plays anywhere: with less, the player says "
                         "Low memory and may pause in a burst. AUTO: "
                         "encoded against the whole of --pool, and the file "
                         "asks for only what the encode drew of it "
                         "(98.2.1.3.3)")
    ap.add_argument("--pool", type=float, metavar="KB", default=POOL_KB,
                    help="with --bank auto: the XMS the machine it is made "
                         "for has free (default %d, a 4 MB board's ~3 MB "
                         "less 128 KB)" % POOL_KB)
    ap.add_argument("--prefill", metavar="S|all",
                    help="with --bank: seconds of the disk the player reads "
                         "into the bank before the first frame, saying "
                         "Buffering as it does, or 'all' for the whole bank "
                         "(98.3.18.3). The stream starts that much richer. "
                         "Default %g" % BANK_PREFILL)
    ap.add_argument("--layer-disk", type=float, metavar="BYTES",
                    help="THE LAYER (98.1.9): a second stream, encoded on "
                         "top of this one, for a machine whose disk reads "
                         "this many bytes a second - made of what the base "
                         "leaves that machine's disk and CPU. Every player "
                         "plays the base; one that reads the layer too "
                         "plays better")
    ap.add_argument("--layer-profile", choices=sorted(PROFILES),
                    type=profile_name,
                    help="THE LAYER FOR ANOTHER MACHINE: its CPU and disk, "
                         "where the base is made for --profile - a file for "
                         "a 5150 that plays better on a 286. Its disk is "
                         "--layer-disk if that is given (98.1.9)")
    ap.add_argument("--layer-memory", type=float, metavar="KB",
                    default=64.0,
                    help="with --layer-disk: the CONVENTIONAL memory the "
                         "encode assumes the layer is read ahead into, 64 "
                         "or 128 KB - the floor: a player takes more where "
                         "the heap has it (default 64)")
    ap.add_argument("--layer-bank", type=bank_arg, metavar="KB|auto",
                    help="with --layer-disk: an XMS BANK for the layer on "
                         "the machine that plays it, ahead of its slots - "
                         "the layer read further ahead in calm stretches, "
                         "and before the first frame by --layer-prefill. A "
                         "machine with less plays it and misses more of "
                         "the layer (98.1.9). AUTO: what --layer-pool leaves "
                         "once that machine keeps the base's bank it needs "
                         "(98.2.1.3.3)")
    ap.add_argument("--layer-pool", type=float, metavar="KB",
                    help="with --layer-bank auto: the XMS the layer's "
                         "machine has free (default --pool's)")
    ap.add_argument("--layer-passes", choices=("two", "one"), default="two",
                    help="with a layer: TWO passes (the default) - the first "
                         "works out the bank the layer's machine needs for "
                         "the base, and the layer is made knowing it; ONE "
                         "learns it as it goes, and a layer record made "
                         "before it knew may come too late to be drawn, "
                         "leaving the last one's pixels standing (98.2.1.3.3)")
    ap.add_argument("--layer-prefill", metavar="KB|all",
                    help="with --layer-disk: KB of the layer read before the "
                         "first frame, or 'all' of --layer-memory and "
                         "--layer-bank (default none)")
    ap.add_argument("--layer-seek", type=float, metavar="MS",
                    default=LAYER_SEEK_MS,
                    help="with --layer-disk: the seek, each way, that each "
                         "32 KB of the layer costs the disk (default %g)"
                    % LAYER_SEEK_MS)
    ap.add_argument("--lookahead", type=int, default=2, metavar="N",
                    help="WHEN A FRAME IS CUT, don't pay for pixels about "
                         "to change: a change is ranked by what it is worth "
                         "over the next N frames too, and one the picture is "
                         "about to undo is not sent (98.2.1.2). Default 2; "
                         "0 ranks by this frame alone, as before")
    ap.add_argument("--lookahead-floor", type=float, default=0.5,
                    metavar="F",
                    help="WHEN A FRAME IS CUT, how much of a change's worth "
                         "NOW the look-ahead may take away: never below F of "
                         "it, so a bright thing about to move is still "
                         "drawn and not left as dark ground (0.5, the "
                         "default; 0 lets the frames after take it all, as "
                         "before; 98.2.1.2)")
    ap.add_argument("--cut", choices=("rank", "fill", "tear"),
                    default="fill",
                    help="WHEN A FRAME IS CUT (over its budget, or a "
                         "lossless frame over the room a record holds), "
                         "which changes it keeps: rank, the best first, the "
                         "look-ahead leaving out what it scores worthless; "
                         "fill (the default), the same with those last "
                         "instead, so the record's room is spent; tear, whole rows in screen order from where "
                         "the last cut stopped - one tear line instead of "
                         "rows scattered across the picture (98.2.1.2.1)")
    ap.add_argument("--bands", type=int, default=0, metavar="N",
                    help="Mode X and 16 colours: draw each "
                         "frame in N bands of rows, every plane of a band "
                         "before the next, so a frame caught half drawn "
                         "TEARS ALONG ONE LINE instead of combing columns "
                         "and rows across the picture. Costs a few bytes "
                         "and cycles a band; 0 (the default) draws plane by "
                         "plane over the whole frame (98.2.1.2.2)")
    ap.add_argument("--frame-cap", choices=tuple(FRAME_CAPS),
                    help="the most one frame's record may take, in KB: 32 "
                         "(the default, which every player plays; 48 for the "
                         "486 profile), 48 or 63.5. Past 32 a fast pan is cut "
                         "less, or not at all, but the file plays only on a "
                         "player that knows BIG super-packets, takes 32 KB "
                         "more memory, and a big frame must be read and "
                         "drawn in its period - a 486-class machine's "
                         "(98.1.4.1). Not with --live or --resident")
    ap.add_argument("--attend", type=float, default=0.0, metavar="S",
                    help="WHEN A FRAME IS CUT, spend where an eye LOOKS: "
                         "each pixel's worth weighted by the view as seen's "
                         "attention - the bright, and what moves against the "
                         "camera - at strength S (1 the map, 0 off, the "
                         "default; VIDEO-SEEN-PLAN 3)")
    ap.add_argument("--min-fps", type=float, default=0.0, metavar="F",
                    help="VARIABLE FRAME RATE: where the budgets would cut, "
                         "slow down - to 5/6, 2/3, 1/2... of the rate, never "
                         "under F - for as long as that draws the picture "
                         "better, as the view as seen judges it; a held "
                         "frame draws toward the next one shown (0 off, the "
                         "default; not on a flipped, page-ahead, layered or "
                         "Live file; VIDEO-SEEN-PLAN 3a)")
    ap.add_argument("--ladder-at", default="", metavar="RANGES",
                    help="THE LADDER BY TIME: other rungs for the stretches "
                         "that START in a stretch of the clip - "
                         "START-END=RUNGS, seconds into the clip, several "
                         "split by ';' - as in 7.3-7.8=lag:50,coarseany:"
                         "frame,slow:2/3. Each is a whole ladder in "
                         "--ladder's words, used there instead of it, and "
                         "--ladder is still the clip's everywhere else. "
                         "Not colours, which is a scene's and chosen before "
                         "the encode (98.2.1.10)")
    ap.add_argument("--scene-join", default="", metavar="SECONDS",
                    help="with --scene-palettes: the scene changes at these "
                         "times (seconds into the clip, split by ',') NOT a "
                         "change - the scene before and the one after take "
                         "one sixteen, chosen over both - for a change the "
                         "budget cannot draw whole, which shows the last "
                         "scene's picture in the new colours until it is "
                         "drawn. The report names those, with the time to "
                         "give here (98.1.3.2.3.3)")
    ap.add_argument("--ladder", default="", metavar="RUNGS",
                    help="THE DEGRADE LADDER: where the budgets would cut, "
                         "the RUNGS in order, each on top of those before - "
                         "lag:MS, slow:A/B (of the frame rate), coarsescene:"
                         "frame|mix|WxH (cells at a scene change), coarseany:"
                         "frame|mix|WxH (cells at any frame it would cut; "
                         "frame: the whole frame in the finest cell that "
                         "fits, mix: only the tiles it must), colours:12|8 (a "
                         "scene's palette stepped down), cut:PERCENT (the "
                         "frames cut, where none is more than that broken "
                         "as seen) - the first that draws the stretch whole "
                         "taken, else the least broken; then the cut. It "
                         "sets --lag, --min-fps, "
                         "--coarse-cut, --step-down and --step-min itself. "
                         "Blank: off, the default (VIDEO-SEEN-PLAN 3a; the "
                         "window's default ladder is %s)" % DEGRADE_DEFAULT)
    ap.add_argument("--flip-hold", choices=("ahead", "empty"),
                    default="ahead",
                    help="--min-fps on a FLIPPED file: AHEAD, a held frame "
                         "is drawn on the page the glass is not showing, "
                         "toward the next frame, which finishes it there and "
                         "is shown (98.1.3.9; a player before it refuses the "
                         "file); EMPTY, it writes nothing and the picture "
                         "before is shown again")
    ap.add_argument("--slow-cost", type=float, default=VFR_HOLD,
                    metavar="PTS",
                    help="--min-fps's price of choppiness: each frame a "
                         "slower rate holds counts as PTS points of the view "
                         "broken, so a stretch slows only where the tearing it "
                         "saves is worth more. Higher holds the full rate "
                         "longer, lower slows sooner (default %g; "
                         "VIDEO-SEEN-PLAN 3a)" % VFR_HOLD)
    ap.add_argument("--edges", type=float, default=1.0, metavar="S",
                    help="WHEN A FRAME IS CUT, count the EDGES it leaves: a "
                         "change also worth how much nearer its target's "
                         "edges the screen is for it - a stale band's torn "
                         "rows, a block's outline - at strength S (1, the "
                         "default since 2026-10-16; 0 off, as before; "
                         "VIDEO-SEEN-PLAN 3)")
    ap.add_argument("--error", choices=("visible", "bits"),
                    default="visible",
                    help="WHEN A FRAME IS CUT, what counts as wrong: the "
                         "error as SEEN (the default) - a dither pattern "
                         "swapped for another of the same grey counts a "
                         "quarter, a wrong grey in full - or plain bits, as "
                         "before (98.2.1.2)")
    ap.add_argument("--owe", type=float,
                    help="OWED TIME: the most one frame may run to at a "
                         "scene cut, in PERIODS, the two after it paying it "
                         "back - the picture waits a period instead of "
                         "smearing (98.2.1.1). 0 turns it OFF, every frame "
                         "held to --peak; so does a --peak that reaches it")
    ap.add_argument("--audio", choices=("pcm8", "adpcm4", "speaker", "none"),
                    help="the sound: 8-bit PCM, Sound Blaster ADPCM (half "
                         "the bytes), SPEAKER - PCM8 at 5,512 Hz for a "
                         "machine with no card, stored as the speaker's "
                         "counts and the frames budgeted around the ~48%% of "
                         "an 8088 the speaker's interrupts take "
                         "(SPEC.md 98.2.15) - or none (default: the "
                         "profile's)")
    ap.add_argument("--rate", type=int,
                    help="the sound's samples a second (default: the "
                         "profile's). 5512 halves the sound's bytes - on a "
                         "Live or resident clip, whose sound is held in "
                         "memory, that is a longer clip (98.1.7.2)")
    ap.add_argument("--adpcm", choices=("search", "greedy"), default="search",
                    help="ADPCM4's encoder: the exact search (~7 dB better, "
                         "about real time on four cores) or greedy")
    ap.add_argument("--jobs", type=int,
                    help="cores to use (default: all): the dither, the ADPCM "
                         "search and --aim quality's trials are spread over "
                         "them")
    ap.add_argument("--volume", help="ffmpeg's volume= (e.g. 1.5, 3dB)")
    ap.add_argument("--buffer-sound-ahead", choices=("auto", "on", "off"),
                    default="auto",
                    help="carry the sound a few frames AHEAD of its picture "
                         "in the file (SPEC.md 98.1.8), so the player has "
                         "it queued before the disk reaches the picture: in "
                         "a burst the card keeps playing where it would "
                         "have paused, and the disk budget no longer holds "
                         "back the sound's lead, which goes to the picture "
                         "instead. It costs DISK SPACE - the first frames' "
                         "sound again at every keyframe, ~0.9 KB a second "
                         "at 11 kHz - and nothing while playing. auto: on "
                         "when the encode needed it, or came within the "
                         "ahead frames' refill of needing it, or when no "
                         "disk is modelled (lossless); on; off: the sound "
                         "in step with the picture, as files were before "
                         "it")
    ap.add_argument("--ahead-frames", type=int, default=AHEAD_FRAMES,
                    help="with Buffer sound ahead, how many frames ahead "
                         "(default %d): more keeps the sound going through "
                         "a longer stall and costs that much more space at "
                         "every keyframe" % AHEAD_FRAMES)
    ap.add_argument("--spk-shape", choices=(SPK_ENC, SPK_MACH, SPK_NONE),
                    default=SPK_ENC,
                    help="with --audio speaker, WHERE the sound is shaped - "
                         "what the speaker cannot play cut, the level evened "
                         "out and driven loud, so it is heard over the "
                         "pulses' whine (SPEC.md 98.2.15.1). encoder: here "
                         "(the default); machine, for a .WAV only: a plain "
                         "8-bit WAV that Audio shapes itself (86.21.1); none: "
                         "the sound as it is")
    ap.add_argument("--spk-style", choices=sorted(vid.SPK_STYLES),
                    default=vid.SPK_STYLE,
                    help="with --spk-shape: LIFTED levels harder so quiet "
                         "passages are heard; NATURAL keeps more of the "
                         "song's own rise and fall (default %(default)s). "
                         "--spk-highpass, --spk-ratio and --spk-range "
                         "override it (SPEC.md 98.2.15.1)")
    ap.add_argument("--spk-highpass", type=int, default=None,
                    metavar="HZ", help="with --spk-shape: nothing under this "
                    "(the style's; 0 keeps the bass)")
    ap.add_argument("--spk-pulses", type=int, choices=(1, 2),
                    default=1,
                    help="with --audio speaker: pulses a sample, so the "
                         "speaker's whine is the rate times this - 2 at "
                         "5512 Hz is an 11 kHz carrier, much quieter to the "
                         "ear, at the price of the machine the extra pulses "
                         "take (SPEC.md 34.11.7, 98.1.1.3.1). Older players refuse the "
                         "file")
    ap.add_argument("--spk-preview", metavar="WAV",
                    help="with --audio speaker: also write what the speaker "
                         "line will carry - the pulses, carrier and all - as "
                         "a WAV to listen to here (SPEC.md 98.2.15.2)")
    ap.add_argument("--spk-idle", type=float, default=vid.SPK_IDLE,
                    metavar="S", help="with --spk-shape: in the quiet, slide "
                    "the pulses' resting width to the short end over this "
                    "many seconds, so the whine goes where there is nothing "
                    "to cover it (default %(default)s; 0 off)")
    ap.add_argument("--spk-ratio", type=float, default=None,
                    help="with --spk-shape: the leveller's ratio - higher "
                         "lifts quiet passages more (the style's)")
    ap.add_argument("--spk-range", type=float, default=None,
                    metavar="DB", help="with --spk-shape: the most a quiet "
                    "passage is raised (the style's)")
    ap.add_argument("--spk-lows", type=float, default=vid.SPK_LOWS,
                    help="with --spk-shape: the band under %d Hz levelled "
                         "apart from the one over it and scaled by this, so "
                         "the voice keeps its drive while the lower tones - "
                         "what the clip bends first - get less (1: one band; "
                         "default %s)" % (vid.SPK_SPLIT, vid.SPK_LOWS))
    ap.add_argument("--spk-drive", type=float, default=vid.SPK_DRIVE,
                    help="with --spk-shape: the level, an RMS of full scale "
                         "(default %(default)s; higher is louder and clips "
                         "more)")
    ap.add_argument("--dither", choices=("bayer", "bluenoise", "threshold"),
                    default="bayer",
                    help="one bit: an ordered 8 x 8 pattern, a noise pattern, "
                         "or a plain threshold at half grey")
    ap.add_argument("--stable", type=float, default=6.0,
                    help="grey levels either side of a threshold that keep "
                         "a pixel's last value (0: off)")
    ap.add_argument("--levels", choices=("auto", "none"), default="auto",
                    help="stretch the grey range the source uses to the "
                         "whole of black-to-white")
    ap.add_argument("--pixfmt", choices=("mono", "cgacomp", "vga8", "vga4",
                                         "cga4", "c160", "c512", "text"),
                    help="mono (the default), cgacomp: composite colour on "
                         "a CGA (the cga layout only), vga8: 256 colours in "
                         "mode 13h (the lin320 layout, and what it implies), "
                         "vga4: 16 in mode 12h, cga4: 4 in CGA's mode 4 (the "
                         "cga layout), c160: 16 at 160 x 100 in CGA's text "
                         "hack (the c160 layout) - those two full screen "
                         "on a CGA or a VGA - and c512: ~450 at 80 x 100 "
                         "cells on the text hack's COMPOSITE output (the "
                         "text-80x100 layout), full screen on a CGA only - and text: the "
                         "80 x 25 text screen of ANY adapter, the picture "
                         "made of its characters (the text-80x25 layout), full "
                         "screen")
    ap.add_argument("--text-colour", choices=("colour", "mono"),
                    help="text: sixteen colours on sixteen (a CGA, an EGA "
                         "or a VGA), or mono - 07h, 0Fh and 70h, which an "
                         "MDA and a Hercules draw too. The preset says "
                         "which (text, text-mono); colour otherwise")
    ap.add_argument("--text-glyphs",
                    choices=("blocks", "shades", "ascii", "dots",
                             "dots-plus", "blocks-only",
                             "blocks-only-shade"),
                    default="blocks",
                    help="text: the characters the picture is made of - "
                         "printable ASCII and, with shades, the four "
                         "shades, and with blocks (the default) the half "
                         "blocks too, which every ROM draws alike and which "
                         "are the clearest - or dots: the full and half "
                         "blocks for shapes and , . ' ` for the edges and "
                         "dithers a block is too coarse for, and dots-plus "
                         "those with : ; \" * as well - or blocks-only, the "
                         "full and half blocks alone, and blocks-only-shade "
                         "those and the shades: no letters at all")
    ap.add_argument("--text-detail", type=float, default=0.5,
                    help="text: 0..1, how much a cell is judged dot for dot "
                         "(which way an edge runs) against through the eye "
                         "(what tone a shade reads as)")
    ap.add_argument("--text-sharpen", type=float, default=0.6,
                    help="text: the unsharp mask before matching, 0 none")
    ap.add_argument("--text-busy", type=float, default=None,
                    help="text: what a LETTER pays over a space, shade or "
                         "block, RMS of 255 - letters picked for a hair of "
                         "error are noise. 12 by default, 2 with ascii and "
                         "the dots sets")
    ap.add_argument("--text-prefer-colour", type=float,
                    default=PREFER_COLOUR,
                    help="text, in colour: how strongly a cell's HUE is "
                         "kept over its exact brightness, so a pale blue - "
                         "a waterfall, a sky - is drawn in blue and white "
                         "rather than crushed to a grey. %g by default, 2 "
                         "stronger, 0 off (brightness first, the encoder "
                         "before it)" % PREFER_COLOUR)
    ap.add_argument("--text-ocr", action="store_true",
                    help="text: READ the picture (tesseract, which must be "
                         "on the PATH) and draw a word whose letters are "
                         "about one character cell big in those exact "
                         "characters, in one colour - a score, a caption. "
                         "Slower: see --text-ocr-every")
    ap.add_argument("--text-ocr-large", action="store_true",
                    help="text: READ the picture (tesseract) and draw a "
                         "word whose letters are LARGER than a cell as "
                         "crisp solid blocks in one colour - no shades, "
                         "no stray letters inside them - so a title "
                         "stays readable")
    ap.add_argument("--text-ocr-conf", type=float, default=80.0,
                    help="text OCR: the confidence (0..100) a word needs to "
                         "be drawn; lower finds more and invents more")
    ap.add_argument("--text-ocr-every", type=int, default=5,
                    help="text OCR: read one frame in this many; the frames "
                         "between keep the last reading. 1 reads every "
                         "frame, the slowest and the most exact")
    ap.add_argument("--text-stable", type=float, default=6.0,
                    help="text: a cell keeps last frame's code while it is "
                         "within this (RMS of 255) of the best, so the "
                         "letters do not crawl; 0 is off")
    ap.add_argument("--cga-palette", type=int, choices=(0, 1, 2),
                    help="cga4: the set - 0 green, red, brown; 1 cyan, "
                         "magenta, white; 2 cyan, red, white (mode 5's). "
                         "Default: the nearest to the clip")
    ap.add_argument("--cga-bright", type=int, choices=(0, 1),
                    help="cga4: the set's intensity (default: the nearest)")
    ap.add_argument("--cga-bg", type=int, choices=range(16), metavar="0-15",
                    help="cga4: the background colour, any of the sixteen "
                         "(default: the nearest)")
    ap.add_argument("--cga-card", choices=("old", "new", "both"),
                    default="both",
                    help="c512: the composite output it is made for - IBM's "
                         "OLD CGA, the NEW (1985) one, or each cell chosen "
                         "for the smaller of its two errors (SPEC.md "
                         "98.1.3.5). Software cannot tell the two apart, so "
                         "this is the file's")
    ap.add_argument("--c512-dither", type=float, default=6.0,
                    help="c512: the 8 x 8 Bayer offset's amplitude, of 255 "
                         "(0: none)")
    ap.add_argument("--c512-stable", type=float, default=3.0,
                    help="c512: a cell keeps last frame's code while it is "
                         "within this of the best, in CIELAB units")
    ap.add_argument("--c512-mix", type=int, default=0,
                    help="c512: 2 or more makes each cell a PATTERN of this "
                         "many codes, whose mean is its colour (98.2.14) - "
                         "a faint tint instead of the grey nearest it, for "
                         "grain; stable by --vga4-stable. 0: nearest code")
    ap.add_argument("--flip", action="store_true",
                    help="modex: two pages, the player drawing one while "
                         "the other shows - no tearing, at twice the decode "
                         "(98.3.8)")
    ap.add_argument("--flip-own", action="store_true",
                    help="with --flip: each frame a change against the page "
                         "it is drawn on (the frame two back) and drawn on "
                         "it as it stands, so the player never brings a page "
                         "up first - less decode for more bytes. On two "
                         "pages a file only a player that knows it plays; "
                         "with --page-ahead on --pages 3 or more, built "
                         "ahead as well, and any page-ahead player's "
                         "(VIDEO-SEEN-PLAN 3b)")
    ap.add_argument("--lag", type=lag_arg, default=LAG_MS,
                    metavar="MS|LO-HI",
                    help="with a card's sound: how far the picture may fall "
                         "behind the sound in a burst and catch up after, "
                         "milliseconds. LO-HI, the default 50-100, is a "
                         "RANGE: at each burst the encoder plays every "
                         "allowance in it half a frame apart and keeps the "
                         "one that cuts the fewest frames, the least lag of "
                         "those (98.2.1.1.3). One number is that allowance "
                         "for every burst (98.2.1.1.2). A file with the "
                         "speaker's sound or none never catches up, and is "
                         "held to a frame")
    ap.add_argument("--page-ahead", choices=("auto", "on", "off"),
                    default="auto",
                    help="the second page as where a frame the budgets "
                         "would cut - a scene change - is BUILT AHEAD out "
                         "of what the frames before it leave, and shown "
                         "with one flip; every other frame is drawn in place "
                         "(98.2.1.8). auto (the default): on Mode X and 16 "
                         "colours on a --screen of its own, which always "
                         "have the page, KEPT only if the picture comes out "
                         "better and nowhere worse than without it - else "
                         "the file is the plain encode. on: also a "
                         "Hercules's (a lone card: one with a colour card "
                         "beside it refuses the file), kept whatever. Two "
                         "passes where it applies")
    ap.add_argument("--pages", choices=("2", "3", "4", "5", "6", "7", "8",
                                        "max"), default="max",
                    help="page-ahead ON N PAGES (98.2.1.8.4): the pages a "
                         "page-ahead file builds on - 2 (one hidden page: "
                         "every player since 98.1.3.7 plays it), or up to "
                         "every page the screen holds: Mode X 3, "
                         "Mode X on --screen 320x200 4, 16 colours on "
                         "320x200 4 and 320x240 3, NARROW (320x200n, "
                         "320x240n) 8 and 6. MAX, the default since "
                         "2026-10-17, takes them all - two for a layered "
                         "file, which has no more. More "
                         "pages build two or three scene changes close "
                         "together, and the first frames of a burst, in the "
                         "calm before them; a player before 98.3.8.4 refuses "
                         "such a file")
    ap.add_argument("--page-keys", choices=("all", "started"), default="all",
                    help="--pages past 2 (98.2.1.8.4): what a KEY carries "
                         "of the hidden pages - all (the default): every "
                         "page as it is, so no build is ever redone and a "
                         "seek reads a picture per page; started: only the "
                         "pages whose build has begun, the rest rebuilt "
                         "whole after the key - smaller keys and faster "
                         "seeks, for stream bytes the spare pays (on a "
                         "Mode X clip of close cuts: keys 970 KB -> 802, "
                         "the stream 655 KB -> 744, the same frames cut)")
    ap.add_argument("--key-pool", choices=("on", "off"), default="on",
                    help="--pages past 2 (98.1.3.8.1): THE PAGE POOL - a "
                         "page picture two keys or more carry is stored once "
                         "and each key names it: on (the default) - on the "
                         "owner's clips at eight pages the keys' pages fell "
                         "32-96%%; a seek reads a picture per page named. "
                         "off: every key its own copy")
    ap.add_argument("--page-prune", choices=("rescue", "always", "off"),
                    default="rescue",
                    help="page-ahead, built flip by flip (98.2.1.8.2): a "
                         "build whose own stretch - from the first frame "
                         "spent toward it to the first spent toward the "
                         "next - comes out worse than the plain encode is "
                         "left out, in a third pass. rescue (the default): "
                         "only where auto would otherwise ship the plain "
                         "file. always: wherever one did, keeping the "
                         "pruned file only if it is better. off: never")
    ap.add_argument("--detail", type=dims,
                    help="vga8: WxH, the picture made at a W-th of the "
                         "width (1, 2, 4) and an H-th of the height (1, 2) "
                         "and shown at full size - W by repeating pixels, "
                         "which Mode X stores 2 or 4 to the byte, H by the "
                         "VGA showing each row twice (98.2.4). Default "
                         "1x1, and 2x1 for the vga8 and modex presets "
                         "except on the 486 profile")
    ap.add_argument("--vga8-dither", type=float, default=24.0,
                    help="vga8: the ordered dither's reach, in 8-bit RGB "
                         "steps across the 8 x 8 map (0: none)")
    ap.add_argument("--vga4-stable", type=float, default=24.0,
                    help="vga4: keep a pixel's colour while its source has "
                         "moved less than this RGB distance since the "
                         "colour was chosen, and the colour is still one "
                         "its pattern mixes (0: off)")
    ap.add_argument("--vga4-hold", type=float, default=0.0,
                    help="vga4, cga4, c160: also keep a pixel's colour while "
                         "it is within this RGB distance of the source, "
                         "whether or not its pattern still mixes it - fewer "
                         "changes, so fewer bytes and fewer frames cut, for "
                         "a stiller dither, scored by --vga4-hold-score so "
                         "it lets go of a colour that stays wrong (0: off; "
                         "24 with a score of 60 is the measured pair)")
    ap.add_argument("--scene-palettes", action="store_true",
                    help="vga4 with --palette colourN: a palette for "
                         "each SCENE, switched at whole-scene cuts as the "
                         "cut is drawn (98.1.3.2.3) - colours change only "
                         "while the picture does")
    ap.add_argument("--scene-cover", type=float, default=0.0,
                    help="scene-palettes: also require this share of the "
                         "picture to change by more than %d levels for a "
                         "new palette, so a cut within one place keeps its "
                         "colours (0: every shot cut; 0.5 merges dark "
                         "shots)" % SCENE_STEP)
    ap.add_argument("--step-min", type=int, choices=(8, 12), default=8,
                    help="step-down: the fewest colours a scene may step "
                         "to - 12 keeps every scene at 12 or more")
    ap.add_argument("--step-down", choices=("off", "on"), default="on",
                    help="scene-palettes: the RATE LADDER (98.2.1.9) - a "
                         "scene the first pass found visibly broken takes "
                         "its own 12 colours, and where most of it broke "
                         "its own 8, before frames are cut; a step only "
                         "where it asks for less and keeps the scene's "
                         "saturated colours")
    ap.add_argument("--scene-hold", action="store_true",
                    help="scene-palettes: start each scene's dither from "
                         "the last scene's screen, so --vga4-hold keeps a "
                         "pixel whose slot's NEW colour is already near the "
                         "source - fewer bytes and frames cut at a cut, but "
                         "the last scene's pattern stands in the new one and "
                         "fades out over its first frames")
    ap.add_argument("--coarse-cut", type=coarse_cell, default=COARSE_CELL,
                    help="vga4 and modex: draw each scene's first frame "
                         "as flat cells this size (WxH), each the colour "
                         "nearest the cell's mean, so a cut BLOCKS IN where "
                         "it would chop - the detail follows in the frames "
                         "after, as a cut frame's remainder does; auto: the "
                         "finest the budgets draw whole - on vga4 of whole, "
                         "16x4 and 32x8, else 64x8; on modex whole, else 4x1, "
                         "its four pixels a store; off: the cut drawn as it "
                         "comes. The planar screens only: on a packed one a "
                         "flat cell saves little (default %s)" % COARSE_CELL)
    ap.add_argument("--blocks", choices=("off", "on"), default="off",
                    help="VERSION 2 (98.1.10): a cut frame may also draw "
                         "BLOCKS - on vga4 a dithered fill (two colours "
                         "and a mix, 2 bytes) or two colours and a mask "
                         "for 8 x 4 pixels - each priced and ranked beside "
                         "the frame's changes, the frames after sharpening "
                         "what a block stood for; on one bit a tile out "
                         "of a codebook the stream keeps. VIDEO.O88 plays "
                         "it from 98.3.23 on. Not with --live, --resident, "
                         "--bands, --cut tear or, on one bit, --loop-from "
                         "(98.1.10.1; default off)")
    ap.add_argument("--vga4-order", choices=("dark", "planes"),
                    default="dark",
                    help="vga4 with its own sixteen: which slot each colour "
                         "takes. dark: darkest first. planes: chosen per "
                         "scene for the planes its changes write - fewer "
                         "stores, the same picture - with no cut moving "
                         "more pixels far at its palette switch than "
                         "darkest first would; costs a second pass of the "
                         "dither")
    ap.add_argument("--scene-min", type=int, default=SCENE_MIN,
                    help="scene-palettes: the fewest frames a palette is "
                         "kept before the next shot cut may change it - a "
                         "fast camera reads as a cut every few frames, and "
                         "each new palette redraws the picture and flashes "
                         "its colours (default %d)" % SCENE_MIN)
    ap.add_argument("--vga4-hold-score", type=float, default=60.0,
                    help="vga4-hold: let a held pixel go once a leaky sum of "
                         "how far its colour is from the source, over the "
                         "frames it wanted another, reaches this - lower "
                         "lets go sooner (fewer stuck skies, more bytes); "
                         "0: never, which keeps a dark sky from the last "
                         "scene and stops a pan")
    ap.add_argument("--vga4-plan-cap", type=float, default=64.0,
                    help="vga4 with the clip's own colours: a colour the "
                         "pattern mixes in may be at most this RGB distance "
                         "from the source (or twice the nearest colour's) - "
                         "stops stray bright dots on dull greys; the EGA's "
                         "and CGA's fixed colours never take it (0: off)")
    ap.add_argument("--vga4-wm3", choices=("off", "on"), default="on",
                    help="vga4: write a byte some state of whose planes "
                         "would show a colour neither frame has in VGA "
                         "write mode 3, all four planes in one store - no "
                         "colour flashes, for decode time (98.1.3.2.4); "
                         "off: by planes, as before 2026-10-13")
    ap.add_argument("--vga4-sparse", type=int, default=None,
                    help="vga4 with the clip's own colours: a colour making "
                         "this many or fewer of a pixel's sixteen pattern "
                         "picks is a lone dot of an even grid, and the "
                         "pixel takes its nearest colour instead - with a "
                         "low --clip, the dither's texture without its "
                         "loose dots (0: off; the default is 2 with "
                         "--palette colourN on a VGA, else 0)")
    ap.add_argument("--vga8-stable", type=float, default=18.0,
                    help="vga8: how far, in RGB distance, the colour on "
                         "the screen may be from the source and stay")
    ap.add_argument("--screen", type=screen_arg, choices=tuple(v[0] for v in
                                              vid.SCREENS.values()),
                    help="vga4: the screen it plays on - 640x480, mode 12h "
                         "and the desktop's own (the default, and the only "
                         "one a window hosts); 320x200 (mode 0Dh), 320x240, "
                         "640x350 or 640x400, full screen and with room for "
                         "two pages, so --flip (98.2.5.1). The box is cut "
                         "to it. modex: 320x240, its own (the default), or "
                         "320x200 - 13h's 400 lines, four "
                         "pages where 320 x 240 has three (98.1.3.1.1). "
                         "320x200n and 320x240n: 16 colours NARROW, rows 40 "
                         "bytes apart for eight and six pages, for --pages "
                         "and not --flip (98.1.3.2.5)")
    ap.add_argument("--palette", choices=PALETTES, default="auto",
                    help="vga8: the colours - auto, the clip's own 256; "
                         "greyN, a ramp of N greys black to white, dithered "
                         "between levels by --dither and held by --stable "
                         "as one bit is; clipN, the clip's own N colours "
                         "(98.2.3.7). Fewer change fewer bytes: in Mode X "
                         "2 or 4 greys is what saves")
    ap.add_argument("--comp-dither", choices=("diffuse", "pattern"),
                    default="diffuse",
                    help="cgacomp: error diffusion through the model (the "
                         "default) or the ordered pattern dither")
    ap.add_argument("--comp-stable", type=float, default=50000.0,
                    help="cgacomp diffuse: how much worse, in the model's "
                         "squared error, last frame's nibble may be and "
                         "stay")
    ap.add_argument("--comp-quick", action="store_true",
                    help="cgacomp diffuse: no lookahead - ~5x faster, and a "
                         "cell before a colour change is judged as if the "
                         "colour went on")
    ap.add_argument("--mix", type=float, default=0.5,
                    help="cgacomp: how far the pattern dither mixes "
                         "colours, 0 (the nearest alone) to 1")
    ap.add_argument("--levels-mix", type=int, choices=(4, 16), default=4,
                    help="cgacomp: colours a pattern mixes - 4 (2 x 2, the "
                         "default: calmer and cheaper) or 16 (4 x 4)")
    ap.add_argument("--clip", type=float, default=None,
                    help="how close a pixel must be to a colour to be "
                         "drawn as that colour SOLID, never a dot - in "
                         "levels (0-255) on every channel. One bit: the "
                         "grey levels at each end that are black or white. "
                         "16 colours (VGA4, CGA4, C160): any of the "
                         "palette's colours, so a black level or a red "
                         "that is nearly the palette's own stops lighting "
                         "an even grid of dots. 256 colours: the darkest "
                         "and lightest only, the palette being dense. "
                         "0 dithers everything. Default 16, and 0 for 16 "
                         "colours on a VGA in the clip's own (--palette "
                         "colourN), where --vga4-sparse 2 does its work")
    ap.add_argument("--gamma", type=float, default=1.0,
                    help="ffmpeg's eq gamma: above 1 lightens the mid-tones")
    ap.add_argument("--contrast", type=float, default=1.0,
                    help="ffmpeg's eq contrast")
    ap.add_argument("--brightness", type=float, default=0.0,
                    help="ffmpeg's eq brightness, -1 to 1")
    ap.add_argument("--invert", action="store_true",
                    help="one bit: white for black")
    ap.add_argument("--loop-from", type=float, metavar="SECS",
                    help="carry a SEAM (SPEC.md 98.1.1.2): the change from the "
                         "last frame back to the frame this many seconds in "
                         "(after --start), so a repeating play loops from "
                         "there without a keyframe. 0 loops the whole clip. "
                         "Not with ADPCM4, whose decoder state cannot join")
    ap.add_argument("--repeat", action="store_true",
                    help="the player starts with Repeat on (98.1.1.2)")
    ap.add_argument("--resident", action="store_true",
                    help="RESIDENT (SPEC.md 98.2.7, 98.1.7): read whole "
                         "before it plays, and played from memory - the "
                         "records one block, LZB-packed when that fits a "
                         "single read (under 60 KB packed, 128 KB unpacked) "
                         "and STORED past it, any size the machine's memory "
                         "holds (98.1.7.1)")
    ap.add_argument("--live", choices=("cga", "herc", "vga"),
                    help="LIVE on that screen's desktop (98.2.7, 98.3.10): "
                         "resident, one bit, laid out as LIN80 at the "
                         "screen's own pixel shape - or with --pixfmt vga4 "
                         "and vga, sixteen colours (98.3.10.4)")
    ap.add_argument("--xms", action="store_true",
                    help="with --live: a STREAMED Live file, of any length, "
                         "that plays on the desktop once the player holds it "
                         "whole in XMS - a 286 or better with extended "
                         "memory - and in the window elsewhere (98.3.18.1). "
                         "One bit, or --pixfmt vga4 with --live vga")
    ap.add_argument("--title",
                    help="the name the player shows (default: the file's)")
    ap.add_argument("--credits",
                    help="a line of credits the player's Info shows")
    ap.add_argument("--keysecs", type=float, default=vid.KEY_SECS,
                    help="seconds between keyframes - the places a seek "
                         "and the Preview can start from")
    ap.add_argument("--poster", type=int, help="the poster's keyframe "
                    "index (default: the first that is not one flat value)")
    ap.add_argument("--progress", action="store_true",
                    help="print the progress as machine-readable lines, "
                    "for the encoder window (SPEC.md 98.2.11.1)")
    ap.add_argument("--poster-at", type=float, metavar="SECS",
                    help="...or the keyframe nearest this many seconds into the "
                         "clip (after --start)")
    ap.add_argument("--poster-image", metavar="FILE|@SECS",
                    help="a CUSTOM poster, shown when the file opens and "
                         "after a play - a picture FILE, or @SECS for the "
                         "clip's own frame that many seconds in (after "
                         "--start), on a key or not; the play still starts "
                         "at a key (98.1.3.11)")
    ap.add_argument("--desktop-posters",
                    choices=("auto", "colour", "mono", "both", "off"),
                    default="auto",
                    help="each key's picture for the desktop's Preview made "
                         "here - averaged and dithered to the desktop's "
                         "sixteen (colour) or to black and white (mono) - "
                         "where the player's own would be poor: auto (the "
                         "default) for 256 colours, CGA's colours and 16 in "
                         "colours of their own or too big for the window; "
                         "colour, mono or both for every file; off for "
                         "none (98.1.3.11)")
    ap.add_argument("--desktop-clip", type=float, default=16.0,
                    help="--clip for the desktop posters: how close a "
                         "cell must be to one of the desktop's sixteen "
                         "(or to black or white) to be drawn as that "
                         "colour SOLID rather than dithered - in levels "
                         "(0-255) on every channel - so a ground that is "
                         "nearly black is black, not a field of dots. 0 "
                         "dithers every cell. Default 16 (98.1.3.11)")
    ap.add_argument("--desktop-size", choices=("fit", "half", "quarter"),
                    default="fit",
                    help="the Preview's size on the desktop: fit (the "
                         "default) as big as the window has room for, or "
                         "half or a quarter of the video's whatever the "
                         "room - and Play then goes full screen "
                         "(98.1.3.11)")
    ap.add_argument("--posters-only", action="store_true",
                    help="src is an EXISTING .V88: make its desktop posters "
                         "again with --desktop-posters, --desktop-size, "
                         "--desktop-clip and --poster-image (a picture "
                         "file), from its own keys, and write it to out - "
                         "the same path rewrites it. Nothing else is "
                         "encoded: seconds, not the encode (98.1.3.11.1)")
    ap.add_argument("--preview-png", metavar="DIR",
                    help="the screen once a second, as PNGs")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--profiles", action="store_true",
                    help="list the profiles and presets")
    return ap


PROGRESS = "@@progress"


def progress_lines(lane="main", out=None):
    """--progress (98.2.11.1): each report a line on stdout - PROGRESS and
    then a JSON object: the `lane` (the encode's own, or a pass run
    alongside it in another process), the step, done and total (0 when
    the step has no count to give) and a note of what else is going on -
    for the encoder window, which runs this encoder as a process of its
    own so Cancel can kill it. A new step, a new total or the step's end
    at once; a step's count and note at most every PROGRESS_EVERY s, not
    one a frame. `out` the stream, or stdout when each line is written"""
    import json
    import time
    last = [None, 0.0, None]

    def tick(step, done, total, note=""):
        key = (step, total)
        now = time.time()
        if key == last[0] and done != total and \
                now - last[1] < PROGRESS_EVERY:
            return
        line = (step, done, total, note)
        if line == last[2]:
            return
        last[0], last[1], last[2] = key, now, line
        o = out if out is not None else sys.stdout
        o.write("%s %s\n" % (PROGRESS, json.dumps(dict(
            lane=lane, step=step, done=done, total=total, note=note))))
        o.flush()
    return tick


PROGRESS_EVERY = 0.25   # s between two of a step's reports, at most


def main():
    ap = parser()
    if "--profiles" in sys.argv:
        for k, p in PROFILES.items():
            print("%-14s %s" % (k, p["what"]))
        for k, (lay, w, h) in PRESETS.items():
            print("%-14s %s %d x %d" % (k, lay, w, h))
        return 0
    a = ap.parse_args()
    if a.posters_only:
        try:
            reposter(a, say=(lambda *x: None) if a.quiet else print)
        except (vid.V88Error, OSError) as e:
            sys.exit("os88venc: %s" % e)
        return 0
    if a.preview_png:
        os.makedirs(a.preview_png, exist_ok=True)
    try:
        encode(a, progress=progress_lines() if a.progress else None)
    except (vid.V88Error, subprocess.CalledProcessError) as e:
        sys.exit("os88venc: %s" % e)
    return 0


if __name__ == "__main__":
    # run as the MODULE, not as __main__: a worker pool pickles this file's
    # functions and classes by name, and os88venc.X is a name any process
    # can import however this one was started (a profiler, a launcher)
    import os88venc
    sys.exit(os88venc.main())
