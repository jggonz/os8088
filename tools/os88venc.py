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
PROFILES = {
    # MEASURED (docs/reports/VIDDISK-ST225-2026-09-27.md): VIDDISK off
    # `make viddisk360` streamed 104.2 KB/s with the hook holding half of
    # every period, the profile's `avg` - and 110.3 idle, 86.7 at 75%. The
    # budget is ~90% of the 50% row; it was 60,000, a margin nobody had
    # measured (VIDEO-PLAN 15.8)
    "5150-st225": dict(disk=96000, avg=0.50, peak=0.85, owe=1.6, speed=1,
                       ring=8,
                       # VIDDISK's rows over its 50% one (the report), and
                       # at 100% extrapolated along its last two
                       disk_at=((0.0, 110.3 / 104.2), (0.25, 105.3 / 104.2),
                                (0.5, 1.0), (0.75, 86.7 / 104.2),
                                (1.0, 69.2 / 104.2)),
                       rate=11025, audio="pcm8",
                       what="the owner's 5150: ST-225 on an ST11M, DMA, "
                            "measured at 104 KB/s under a half-machine "
                            "decode"),
    # MEASURED on MartyPC (VIDEO-PLAN W3): the XT-IDE the CPU copies, so
    # the disk gets what the decode leaves - 198 KB/s idle, 150 / 99 / 49
    # at 25 / 50 / 75%. 90% of the 50% row, as the ST-225's
    "5150-xtide": dict(disk=91000, avg=0.50, peak=0.85, owe=1.6, speed=1,
                       ring=8,
                       disk_at=((0.0, 2.0), (0.25, 150 / 99.0), (0.5, 1.0),
                                (0.75, 49 / 99.0), (1.0, 0.0)),
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
    "286": dict(disk=150000, avg=1.50, peak=2.50, owe=1.6, speed=3,
                ring=8,
                rate=22050, audio="pcm8",
                what="a 6 MHz 286: ~3x the 8088's cycles (predicted)"),
    "286-vga": dict(disk=400000, avg=3.00, peak=5.00, owe=1.6, speed=6,
                    ring=8,
                    rate=22050, audio="pcm8",
                    what="a 12-16 MHz 286 with a VGA, for VGA8: the VGA's "
                         "bus binds, and it stores ~4.5x as fast as the "
                         "5150's CGA; its IDE disk 627 KB/s with half the "
                         "period decoding (86Box's mr286, the owner's "
                         "bench). An ST11R there: --disk 250000"),
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
                   "text-mono": dict(fps=30.0, text_colour="mono")}


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
    },
    "fit": {
        "fit": "The whole picture; the canvas shrinks to its shape, no bars",
        "fill": "The whole box; the picture's edges cropped",
        "stretch": "The whole box and the whole picture, distorted",
    },
    "profile": {
        "5150-st225": "IBM 5150/XT with a Seagate ST-225 hard disk: "
                      "96 KB/s, half the CPU on average (the default)",
        "5150-xtide": "IBM 5150/XT with an XT-IDE: 91 KB/s",
        "5150-picomem2": "IBM 5150/XT with a PicoMEM 2: 150 KB/s, 22 kHz "
                         "sound (predicted)",
        "floppy": "Played off a floppy: 15 KB/s, 5.5 kHz sound - small and "
                  "slow",
        "286": "A 286 with a hard disk: 150 KB/s, three 8088s of CPU "
               "(predicted)",
        "286-vga": "A 286 with VGA: 400 KB/s, six 8088s of CPU",
        "lossless": "No budget: every change kept, whatever it costs",
    },
    "aim": {
        "asked": "Encode what was asked for, nothing more (the default)",
        "quality": "Spend budget the video leaves unused on a bigger "
                   "picture and better sound",
        "size": "Leave out changes not worth their bytes: the smallest "
                "file for the look",
    },
    "error": {
        "visible": "When a frame is cut, judge what is wrong as the eye "
                   "sees it (the default)",
        "bits": "When a frame is cut, count wrong bits plainly",
    },
    "audio": {
        "pcm8": "8-bit sound for a Sound Blaster",
        "adpcm4": "Sound Blaster 4-bit ADPCM: half the bytes, noisier",
        "speaker": "The PC speaker, for a machine with no sound card "
                   "(5.5 kHz; takes about half an 8088)",
        "none": "Silent",
    },
    "adpcm": {
        "search": "Search for the best ADPCM stream (~7 dB better, slower)",
        "greedy": "A nibble at a time: fast",
    },
    "dither": {
        "bayer": "An ordered 8 x 8 pattern: steady, compresses best",
        "bluenoise": "A noise pattern: no visible grid, ~8% more data",
        "threshold": "Plain black or white at half grey: for clips that are "
                     "black and white already",
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
}


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
    num = lambda v: "" if v is None else ("%g" % v)
    out["text_colour"] = d.get("text_colour", "colour") \
        if pixfmt == "text" else ""
    out.update(layout=lay or "", box="%dx%d" % (bw, bh) if bw else "",
               pixfmt=pixfmt or "", fps=num(fps),
               detail=d.get("detail", "1x1"),
               disk=num(prof["disk"]),
               avg=num(LIVE_AVG if live and prof["avg"] is not None
                       else prof["avg"]),
               peak=num(prof["peak"]),
               owe=num(None if live else prof["owe"]),
               reserve="" if live or prof["disk"] is None else
               num(reserve_bytes(prof) // 1024),
               rate=num(prof["rate"]),
               audio=prof["audio"])
    return out

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
# interrupt. os88spk_isr measured 343 cycles entry to iret on MartyPC's 5150,
# and the 8088 takes ~60 more to acknowledge one: ~400 a pulse, in 8088
# cycles. A file made for the speaker carries the COUNTS (98.1.1.3), so the
# player copies them into its ring as it would for a card; translating
# samples there was ~50 a byte. At 5,512 Hz that is ~48% of a 4.77 MHz
# machine - which is the whole reason a clip is made FOR the speaker rather
# than merely played through it
CYC_SPK_PULSE = 400.0
CYC_SPK_BYTE = 15.0     # a plain copy: the file carries the counts
                        # (98.1.1.3), so the table is the encoder's, not the
                        # player's. MEASURED: vp_aput 27.7 cycles a byte of
                        # wall time on MartyPC's 5150, ~15 its own once the
                        # pulses' ~46% is taken out; translating was 92.8/~50
SPK_RATE = 5512         # the speaker target's default rate
SPK_MIN = 4679          # N = 1,193,182 / rate is a lobyte count of 74..255
SPK_MAX_8088 = 8000     # VIDEO.O88's VP_SPKMAX: past it an 8088 is SILENT
REC_OVER = 6 + 10       # a record's header and its ten list terminators
REC_MAX = 30 * 1024     # a frame record rides in a super-packet of 32 KB
                        # (98.1.4): whatever the budgets say, no more.
                        # Only VGA8 can reach it - a MONO1 canvas is 16 KB
DISK_LOOKAHEAD = 96 * 1024  # what the disk bucket may bank: THREE of the
                        # player's 32 KB ring slots (98.3), which it has
                        # read before the first frame. It was one second of
                        # the rate, which is under the ring at a 5150's
                        # 60 KB/s and 2-4x over it at a 286's 400: the
                        # stream spent a surplus the player could not hold,
                        # and stalled a second in (the owner's TRK830)
VP_KMAXREC = 61440      # apps/video/video.asm's own cap on a keyframe


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
                        # 32 MB, and every floppy is 1 KB or less
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
        self.stable, self.invert = stable, invert
        self.prev = None

    def __call__(self, grey):
        g = grey.astype(float)
        if self.invert:
            g = 255.0 - g
        on = g > self.t
        if self.prev is not None and self.stable:
            keep = np.abs(g - self.t) < self.stable
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

    def __call__(self, rgb):
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
        best = np.take_along_axis(cands, pick[None], 0)[0]
        if self.prev is not None and self.stable:
            y = self.yuv(c)
            dp = ((y - self.pyuv[self.prev]) ** 2).sum(-1)
            db = ((y - self.pyuv[best]) ** 2).sum(-1)
            best = np.where(np.abs(dp - db) < self.stable ** 2, self.prev,
                            best)
        self.prev = best
        n = best.astype(np.uint8)
        return (n[:, 0::2] << 4) | n[:, 1::2]


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
    import multiprocessing
    out = []
    look = not a.comp_quick
    with multiprocessing.Pool(jobs) as pool:
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

    def __init__(self, w, h, palette, strength, stable):
        self.pal = np.frombuffer(palette, np.uint8).astype(np.float32) \
            .reshape(-1, 3) * (255 / 63)
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

    def __call__(self, rgb):
        f = rgb.astype(np.float32)
        q = np.clip(np.rint((f + self.t) * (31 / 255)), 0, 31).astype(
            np.int32)
        idx = self.lut[(q[..., 0] << 10) | (q[..., 1] << 5) | q[..., 2]]
        if self.prev is not None and self.stable:
            err = np.sqrt(((self.pal[self.prev] - f) ** 2).sum(2))
            idx = np.where(err < self.stable, self.prev, idx)
        self.prev = idx
        return idx


class KnollDitherer(Vga8Ditherer):
    """A PATTERN dither over a sparse palette (Thomas Knoll's, 98.2.5): each
    pixel's plan is sixteen palette colours, each the nearest to the target
    plus the error of the ones before it - so their mean is the target -
    sorted by luma, and the 4 x 4 Bayer cell over the pixel picks one. The
    sixteen EGA colours are 85 to 170 steps apart, and an ordered dither
    that shifts all three channels together only ever mixes greys: the sky
    and the grass came out grey. Stable as Vga8Ditherer is"""

    N = 16

    def __init__(self, w, h, palette, stable):
        super().__init__(w, h, palette, 0.0, stable)
        b = (bayer(4) * 16).astype(np.int64)            # 0..15
        self.cell = np.tile(b, (-(-h // 4), -(-w // 4)))[:h, :w]
        pal = self.pal
        self.luma = pal[:, 0] * 0.299 + pal[:, 1] * 0.587 + pal[:, 2] * 0.114

    def __call__(self, rgb):
        f = rgb.astype(np.float32)
        err = np.zeros_like(f)
        cands = []
        for _ in range(self.N):
            t = np.clip(f + err, 0, 255)
            q = np.clip(np.rint(t * (31 / 255)), 0, 31).astype(np.int32)
            c = self.lut[(q[..., 0] << 10) | (q[..., 1] << 5) | q[..., 2]]
            cands.append(c)
            err += f - self.pal[c]
        cands = np.stack(cands, -1)                     # (h, w, N)
        order = np.argsort(self.luma[cands], axis=-1, kind="stable")
        pick = np.take_along_axis(order, self.cell[..., None], -1)[..., 0]
        idx = np.take_along_axis(cands, pick[..., None], -1)[..., 0] \
            .astype(np.uint8)
        if self.prev is not None and self.stable:
            # STABLE BY THE SOURCE: a pixel keeps its colour while what it
            # was chosen for has moved less than `stable` - a pattern's
            # choice is not near its target, so the VGA8 rule cannot apply
            moved = np.sqrt(((f - self.at) ** 2).sum(2))
            keep = moved < self.stable
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
        self.cc = cc
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
        best = np.take_along_axis(cands, pick[..., None], -1)[..., 0]
        if self.prev is not None and self.mstable:
            moved = np.sqrt(((f - self.at) ** 2).sum(2))
            keep = moved < self.mstable
            best = np.where(keep, self.prev, best)
            self.at = np.where(keep[..., None], self.at, f)
        else:
            self.at = f
        self.prev = best
        return best

    def codes(self, rgb):
        if self.mix >= 2:
            return self.pattern(rgb)
        t = np.clip(rgb.astype(np.float32) + self.t, 0, 255)
        q = np.clip(np.rint(t * (31 / 255)), 0, 31).astype(np.int32)
        qi = (q[..., 0] << 10) | (q[..., 1] << 5) | q[..., 2]
        lab = self.cc.srgb2lab(t).astype(np.float32)
        c0, c1 = self.lut[0][qi], self.lut[1][qi]
        e0, e1 = self.cost(lab, c0), self.cost(lab, c1)
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

    def __call__(self, rgb):
        c = self.codes(rgb)
        out = np.empty((c.shape[0], c.shape[1] * 2), np.uint8)
        out[:, 0::2] = np.where(c >= 256, 0x55, 0x13)
        out[:, 1::2] = c & 255
        return out


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

    def __call__(self, rgb, ocr=None):
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
        if self.prev is not None:           # ...and last frame's, always,
            ar = np.arange(N)               # in place of the least likely
            worst = e0[ar[:, None], ks].argmax(1)
            ks[ar, worst] = np.where((ks == self.prev[0][:, None]).any(1),
                                     ks[ar, worst], self.prev[0])
        A = np.einsum("ntlq,nqc->ntlc", self.oh[ks], P)        # (N, T, 5, 3)
        st = (S, SS, GT, P, (P ** 2).sum((1, 2)), A)
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
        e = self._err(st, kx, fi, bi, ax)
        best = e.argmin(1)
        eb, k, fg, bg = e[ar, best], kx[ar, best], fi[ar, best], bi[ar, best]
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
        if ocr:                         # ...and the words read off it, over
            flat = out.reshape(-1)      # it. prev stays the matcher's own,
            for w in ocr:               # so a word that goes goes cleanly
                d = self._ocr_word(rgb, t, SS, *w)
                if d is not None:
                    idx, codes, attrs, crisp = d
                    flat[idx * 2], flat[idx * 2 + 1] = codes, attrs
                    self.ocr_used[1 if crisp else 0] += 1
        return out

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
    best = None
    for sel in range(0x80):
        if (sel & 0x60) == 0x60:
            continue
        p = 2 if sel & 0x40 else (sel >> 5) & 1
        if (bg is not None and sel & 15 != bg) or \
                (pal is not None and p != pal) or \
                (bright is not None and bool(sel & 16) != bool(bright)):
            continue
        cols = std[vid.cga4_colours(sel)]
        d = ((px[:, None, :] - cols[None, :, :]) ** 2).sum(2).min(1).mean()
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


def vga8_palette(src, w, h, crop, fps_expr, start, end, eq, poll=None):
    """The clip's 256 colours (ffmpeg's palettegen over every frame), as
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
    vf.append("palettegen=max_colors=256:stats_mode=full:reserve_transparent=0")
    with tempfile.TemporaryDirectory() as t:
        out = os.path.join(t, "pal.png")
        cmd = ["ffmpeg", "-v", "error", "-nostdin", "-y"]
        if start:
            cmd += ["-ss", "%.3f" % start]
        cmd += ["-i", src]
        if end:
            cmd += ["-t", "%.3f" % (end - (start or 0))]
        cmd += ["-an", "-vf", ",".join(vf), "-frames:v", "1", out]
        run_polled(cmd, poll)
        from PIL import Image
        rgb = np.array(Image.open(out).convert("RGB")).reshape(-1, 3)
    cols = sorted({tuple((int(v) * 63 + 127) // 255 for v in c) for c in rgb},
                  key=lambda c: (77 * c[0] + 150 * c[1] + 29 * c[2], c))
    if cols[0] != (0, 0, 0):        # TRUE black, whatever the clip holds:
        if len(cols) >= 256:        # it is the screen round the canvas too.
            a = np.array(cols, np.int32)    # Room is made by merging the
            d = ((a[:, None] - a[None]) ** 2).sum(2)   # two nearest colours
            np.fill_diagonal(d, 1 << 30)
            i, j = np.unravel_index(int(d.argmin()), d.shape)
            del cols[max(i, j)]
        cols.insert(0, (0, 0, 0))
    cols += [cols[-1]] * (256 - len(cols))
    return bytes(v for c in cols[:256] for v in c)


def scaled_aspect(asp, dh):
    """A canvas pixel's aspect when each row is shown `dh` times"""
    n, d = asp[0], asp[1] * dh
    k = math.gcd(n, d)
    return n // k, d // k


def span_cost(bs, run, layout=None):
    """(cycles, bytes) of one span in the stream, wave 0's model: its
    list's entry and a skip byte (a segment's set-up amortised in)"""
    fr, sg, ab, cp, csl, crn = vid.cyc_table(layout)
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
    for n in range(max(1, most)):
        cut = e.stats["cut"]
        e.frame(target, b"\x80" * abytes)
        if e.stats["cut"] == cut:
            return n
    return most


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


class Encoder:
    def __init__(self, g, prof, fps, audio_cyc, audio_bps, palette=None):
        self.g = g
        # how wrong a byte is: its differing bits, or for VGA8 how far its
        # colour is from the one wanted
        self.pal = None if palette is None else \
            np.frombuffer(palette, np.uint8).astype(np.float32).reshape(
                -1, 3) * (255.0 / 63)
        period = vid.HZ / fps
        self.period = period
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
                          disk=0, cpu=0, peak=0, owed=0, held=0,
                          late=0, skipped=0)
        # WHAT A CUT FRAME SPENDS ON (98.2.1.2): the targets after this one
        # (`look` of them, set per frame), and error as SEEN rather than as
        # bits - both need the screen as pixels, which `dmode` says how to
        # get: "bits" a byte is 8 one-bit pixels, "pal" a byte (or a pixel)
        # indexes `dpal`, "idx" a byte packs `ppb` indexes of `dpal`, None
        # no pixel form (the composite formats: bits, and no visible error)
        self.look, self.vis, self.future = 0, False, []
        self.thr = 0.0
        self.dmode, self.dpal, self.ppb = \
            ("bits", None, 8) if palette is None else ("pal", self.pal, 1)
        self.blur_r = 1
        self.q_vis = self.q_wrong = self.q_flick = self.q_tflick = 0.0
        self.q_prev = self.q_tprev = (None, None)

    def begin(self):
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
            self.disk.tick()
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

    def disk_rel(self, share):
        """The disk's rate at a hook `share` of the period, over its rate
        at `avg`: the profile's measured points, straight between them"""
        pts = self.dcurve
        share = min(max(share, pts[0][0]), pts[-1][0])
        for (s0, r0), (s1, r1) in zip(pts, pts[1:]):
            if share <= s1:
                return r0 + (r1 - r0) * (share - s0) / (s1 - s0)
        return pts[-1][1]

    def end(self, c):
        """...and ends, having cost the model `c` cycles"""
        if self.dcurve:                 # (the speaker's pulses are the
                                        # reader's CPU too, 98.2.15)
            share = (c + self.audio_cyc + HOOK_CYC) / self.q + self.spk
            per = (self.drate * self.disk_rel(share) * 0.99 - self.abps) \
                / self.fps
            self.disk.level = min(self.disk.cap, self.disk.level + per)
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
        if not len(ys):
            self.stats["exact"] += 1
            return [], vid.record([], g, audio)
        self.tv[self.idx] = target
        sp = vid.spans((self.base[ys] + xs).tolist(), self.tsurf, g)
        if self.thr:                    # --aim size (98.2.1.4)
            sp = self.worthwhile(target, sp)
            if not sp:
                self.stats["exact"] += 1
                return [], vid.record([], g, audio)
        cyc_room = min(self.cpu.room(), self.ceil)
        byte_room = self.disk.room() - self.dfloor
        costs = [span_cost(bs, run, g.layout) for a, bs, run in sp]
        order = None
        er = cyc_room - vid.cyc_table(g.layout)[0]
        eb = min(byte_room, REC_MAX - len(audio)) - REC_OVER
        for attempt in range(16 if self.live else 8):
            tc = sum(c for c, b in costs)
            tb = sum(b for c, b in costs)
            if tc <= er and tb <= eb:
                chosen = sp
            else:
                if order is None:
                    self.why(tc, tb, er, eb, cyc_room)
                    sp = split_big(sp)
                    costs = [span_cost(bs, run, g.layout)
                             for a, bs, run in sp]
                    order = self.rank(target, sp, costs, er, eb)
                chosen, uc, ub = [], 0, 0
                for i in order:
                    c, b = costs[i]
                    if uc + c > er or ub + b > eb:
                        continue
                    chosen.append(sp[i])
                    uc += c
                    ub += b
                chosen.sort()
            rec = vid.record(chosen, g, audio, limit=65535)
            # the model's estimate is per span; the record is measured, and
            # a frame over its ceiling tries again with the estimate scaled
            mc = self.cost(rec)
            if mc <= cyc_room and len(rec) - len(audio) <= byte_room and \
                    len(rec) <= REC_MAX:
                break
            er *= min(0.97, cyc_room / mc)
            eb *= min(0.97, min(byte_room, REC_MAX - len(audio)) /
                      max(1, len(rec) - len(audio)))
        else:
            if self.live and order is not None:
                chosen, rec = self.trim(sp, order, g, audio, cyc_room,
                                        byte_room)
        for a, bs, run in chosen:
            self.surf[a:a + len(bs)] = bs
        self.screen = self.sv[self.idx]
        if chosen is sp:
            self.stats["exact"] += 1
        else:                           # what is still wrong once it is
            self.stats["cut"] += 1      # on: a span bridges bytes that
            self.stats["bytes_left"] += int(    # were already right
                (target != self.screen).sum())
        return chosen, rec

    def trim(self, sp, order, g, audio, cyc_room, byte_room):
        """LIVE: the retries did not bring the frame under its room - a
        blit's cost is its RECTANGLES, which dropping a scatter of spans
        hardly shrinks. So the most the ranking's PREFIX can keep and fit,
        found by halving; none fits, nothing (the picture converges later)"""
        lo, hi, best = 0, len(order), None
        while lo <= hi:
            k = (lo + hi) // 2
            ch = sorted(sp[i] for i in order[:k])
            rec = vid.record(ch, g, audio, limit=65535)
            if self.cost(rec) <= cyc_room and \
                    len(rec) - len(audio) <= byte_room and len(rec) <= REC_MAX:
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
        if tb / max(eb, 1.0) >= tc / max(er, 1.0):
            self.stats["disk"] += 1
            return
        self.cpucut = True
        if cyc_room >= self.ceil:
            self.stats["peak"] += 1
        else:
            self.stats["cpu"] += 1

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
            k = np.array([math.comb(2 * r, i) for i in range(2 * r + 1)],
                         np.float32)
            k /= k.sum()
            pad = [(0, 0)] * out.ndim
            pad[axis] = (r, r)
            pp = np.pad(out, pad, mode="edge")
            n = out.shape[axis]
            out = sum(k[i] * np.take(pp, range(i, i + n), axis=axis)
                      for i in range(2 * r + 1))
        return out

    @staticmethod
    def dist(a, b):
        d = np.abs(a - b)
        return d.sum(2) / 3.0 if d.ndim == 3 else d

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
        S = self.pixels(self.screen)
        if S is None:
            return None
        T0 = self.pixels(target)
        bS = self.blur(S) if self.vis else None
        v = 0.0
        for k, T in enumerate([target] + self.future[:self.look]):
            Tk = T0 if k == 0 else self.pixels(T)
            plain = self.dist(S, Tk) - self.dist(T0, Tk)
            if not self.vis:
                v = v + plain
                continue
            # the share of the pixel's local error an eye sees: the
            # blurred pictures' difference over the blurred plain one -
            # 1 where the picture is wrong, near 0 where only the dither
            # pattern is. A swap is discounted to a quarter, never more,
            # so a pattern still converges; nothing is amplified
            vis = self.dist(bS, self.blur(Tk)) / \
                np.maximum(self.blur(self.dist(S, Tk)), 1e-3)
            v = v + plain * np.clip(vis, 0.25, 1.0)
        return v

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
        swapping back and forth does"""
        S = self.pixels(self.screen)
        if S is None:
            return
        T = self.pixels(target)
        self.q_vis += float(self.dist(self.blur(S), self.blur(T)).mean())
        ne = S != T
        if ne.ndim == 3:
            ne = ne.any(2)
        self.q_wrong += float(ne.mean())
        for now, hist, k in ((S, "q_prev", "q_flick"),
                             (T, "q_tprev", "q_tflick")):
            p1, p2 = getattr(self, hist)
            if p2 is not None:
                a, b = now == p2, now != p1
                if a.ndim == 3:
                    a, b = a.all(2), b.any(2)
                setattr(self, k, getattr(self, k) + float((a & b).sum()))
            setattr(self, hist, (now, p1))

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
        self.wv[self.idx] = bits * (1.0 + self.age / 8.0)
        cs = np.concatenate(([0.0], np.cumsum(self.wv)))
        pri = []
        for i, (a, bs, run) in enumerate(sp):
            c, b = costs[i]
            pri.append(((cs[a + len(bs)] - cs[a]) /
                        max(c / max(er, 1.0), b / max(eb, 1.0)), i))
        pri.sort(reverse=True)
        return [i for p, i in pri if p > 0 or not self.look]

    def cost(self, rec):
        """A record's cycles in the model: its decode, and on a LIVE file
        (98.3.10.2) the blit of the runs the writer will give it. A pass is
        a TICK (98.3.10) and blits the union of the frames it drew, so a
        frame is charged its runs' blit times the passes a frame - 18.2 a
        second over the frame rate, at most 1: measured, Bad Apple made
        --live herc at 30 fps modelled 31.6% of the machine in blit this
        way and gfx_blit1 took 32.1%"""
        g = self.g
        c = vid.cycles_of(rec, True) if g.bitplanes else \
            vid.cycles_of(rec, layout=g.layout)
        if self.live:
            c += self.blitf * sum(vid.blit_cost(y1 - y0, x1 - x0, g.planes)
                                  for y0, y1, x0, x1 in vid.live_runs(rec, g))
        return c

    def charge(self, rec, abytes):
        """What the record actually costs, measured, off the buckets (the
        sound's bytes are taken off the disk's rate at the start)"""
        c = self.cost(rec)
        self.cpu.spend(c)
        self.disk.spend(len(rec) - abytes)
        self.end(c)
        return c


class EncoderX(Encoder):
    """Encoder for MODEX (SPEC.md 98.1.3.1): the screen is PIXELS, and a
    frame's candidate writes are the five sub-records' spans - a byte under
    Map Mask 0Fh is four pixels of one colour, a plane's byte one pixel.
    The budgets, the ranking by error and age, and the measured retry are
    Encoder's"""

    def __init__(self, g, prof, fps, audio_cyc, audio_bps, palette,
                 flip=False):
        super().__init__(g, prof, fps, audio_cyc, audio_bps, palette)
        # FLIPPING (98.3.8): the player decodes the last record again into
        # the back page before this one, so a frame costs both
        self.flip, self.prev_c = flip, 0
        if flip:                        # a frame a call (98.3.8): one that
            self.owe = None             # runs over drops the next
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
        costs = [span_cost(bs, run) for m, a, bs, run in cand]
        cyc_room = min(self.cpu.room(), self.ceil) - self.prev_c
        byte_room = self.disk.room() - self.dfloor
        order = None
        er = cyc_room - vid.CYC_FRAME - 7 * vid.CYC_SUB
        eb = min(byte_room, REC_MAX - len(audio)) - REC_OVER - 7
        for attempt in range(8):
            tc = sum(c for c, b in costs)
            tb = sum(b for c, b in costs)
            if tc <= er and tb <= eb:
                chosen = cand
            else:
                if order is None:
                    self.why(tc, tb, er, eb, cyc_room)
                    cand = split_big(cand)
                    costs = [span_cost(bs, run) for m, a, bs, run in cand]
                    order = self.rank(target, cand, costs, er, eb)
                chosen, uc, ub = [], 0, 0
                for i in order:
                    c, b = costs[i]
                    if uc + c > er or ub + b > eb:
                        continue
                    chosen.append(cand[i])
                    uc += c
                    ub += b
            ops = [(m, sorted((a, bs, run) for mm, a, bs, run in chosen
                              if mm == m))
                   for m in (0x0F, 0x03, 0x0C, 1, 2, 4, 8)]
            rec = vid.record(ops, g, audio, limit=65535)
            mc = vid.cycles_of(rec, True)
            if mc <= cyc_room and len(rec) - len(audio) <= byte_room and \
                    len(rec) <= REC_MAX:
                break
            er *= min(0.97, cyc_room / mc)
            eb *= min(0.97, min(byte_room, REC_MAX - len(audio)) /
                      max(1, len(rec) - len(audio)))
        if chosen is cand:
            self.stats["exact"] += 1
        else:
            self.stats["cut"] += 1
        for m, a, bs, run in chosen:     # a span may run on to the next
            ad = a + np.arange(len(bs))  # row: at full width a plane's
            ys = self.rowof[ad]          # rows are back to back
            xs = (ad - self.base[ys]) * 4
            v = np.frombuffer(bs, np.uint8)
            for p in range(4):
                if m >> p & 1:
                    self.screen[ys, xs + p] = v
        if chosen is not cand:
            self.stats["bytes_left"] += int((target != self.screen).sum())
        g.put(self.surf, self.screen.tobytes())
        return ops, rec

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
        pri = []
        for i, (m, a, bs, run) in enumerate(cand):
            c, b = costs[i]
            wsum = sum(cs[p][a + len(bs)] - cs[p][a] for p in range(4)
                       if m >> p & 1)
            pri.append((wsum / max(c / max(er, 1.0), b / max(eb, 1.0)), i))
        pri.sort(reverse=True)
        return [i for p, i in pri if p > 0 or not self.look]

    def charge(self, rec, abytes):
        c = vid.cycles_of(rec, True)
        self.cpu.spend(c + self.prev_c)
        self.disk.spend(len(rec) - abytes)
        spent = c + self.prev_c
        if self.flip:
            self.prev_c = c
        self.end(spent)
        return spent


class EncoderP(Encoder):
    """Encoder for VGA4 on LIN80's bit-planes (SPEC.md 98.1.3.2): the
    screen is pixels of the sixteen, a byte is one plane's bit of eight of
    them, and at each byte the planes that want one value are one store
    under their combined mask - vid.vga4_subs's rule, done with numpy"""

    def __init__(self, g, prof, fps, audio_cyc, audio_bps):
        super().__init__(g, prof, fps, audio_cyc, audio_bps, vid.STD16)
        self.screen = np.zeros((g.h, g.w), dtype=np.uint8)
        self.age = np.zeros((g.h, g.w), dtype=np.float32)
        self.pl = np.zeros((4, g.h, g.wb), dtype=np.uint8)
        self.surf = g.surface()
        self.base = np.array(g.base, dtype=np.int64)
        self.rowof = np.array(g.rowof, dtype=np.int64)

    def planes(self, cv):
        return np.stack([np.packbits((cv >> p) & 1, axis=1)
                         for p in range(4)])

    def subs(self, target):
        g = self.g
        tp = self.planes(target)
        ch = tp != self.pl
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
        costs = [span_cost(bs, run) for m, a, bs, run in cand]
        cyc_room = min(self.cpu.room(), self.ceil)
        byte_room = self.disk.room() - self.dfloor
        order = None
        er = cyc_room - vid.CYC_FRAME - 15 * vid.CYC_SUB
        eb = min(byte_room, REC_MAX - len(audio)) - REC_OVER - 15
        masks = sorted({c[0] for c in cand}, key=lambda v: (v != 15, v))
        for attempt in range(8):
            tc = sum(c for c, b in costs)
            tb = sum(b for c, b in costs)
            if tc <= er and tb <= eb:
                chosen = cand
            else:
                if order is None:
                    self.why(tc, tb, er, eb, cyc_room)
                    cand = split_big(cand)
                    costs = [span_cost(bs, run) for m, a, bs, run in cand]
                    order = self.rank(target, cand, costs, er, eb)
                chosen, uc, ub = [], 0, 0
                for i in order:
                    c, b = costs[i]
                    if uc + c > er or ub + b > eb:
                        continue
                    chosen.append(cand[i])
                    uc += c
                    ub += b
            ops = [(m, sorted((a, bs, run) for mm, a, bs, run in chosen
                              if mm == m)) for m in masks]
            rec = vid.record(ops, g, audio, limit=65535)
            mc = self.cost(rec)
            if mc <= cyc_room and len(rec) - len(audio) <= byte_room and \
                    len(rec) <= REC_MAX:
                break
            er *= min(0.97, cyc_room / mc)
            eb *= min(0.97, min(byte_room, REC_MAX - len(audio)) /
                      max(1, len(rec) - len(audio)))
        self.stats["exact" if chosen is cand else "cut"] += 1
        for m, a, bs, run in chosen:
            ad = a + np.arange(len(bs))
            ys = self.rowof[ad]
            xs = ad - self.base[ys]
            v = np.frombuffer(bs, np.uint8)
            for p in range(4):
                if m >> p & 1:
                    self.pl[p, ys, xs] = v
        self.screen = sum(np.unpackbits(self.pl[p], axis=1)[:, :g.w]
                          .astype(np.uint8) << p for p in range(4))
        if chosen is not cand:
            self.stats["bytes_left"] += int((target != self.screen).sum())
        for p in range(4):
            for y, b in enumerate(g.base):
                self.surf[p * vid.PLANE + b:p * vid.PLANE + b + g.wb] = \
                    self.pl[p, y].tobytes()
        return ops, rec

    def rank(self, target, cand, costs, er, eb):
        g = self.g
        v = self.value(target) if self.look or self.vis else None
        err = (np.abs(self.pal[target] - self.pal[self.screen]).sum(2)
               / 32.0 if v is None else v) * (1.0 + self.age / 8.0)
        byte = err.reshape(g.h, g.wb, 8).sum(2)
        wv = np.zeros(65537, dtype=np.float64)
        wv[(self.base[:, None] + np.arange(g.wb)).ravel()] = byte.ravel()
        cs = np.concatenate(([0.0], np.cumsum(wv)))
        pri = []
        for i, (m, a, bs, run) in enumerate(cand):
            c, b = costs[i]
            wsum = (cs[a + len(bs)] - cs[a]) * bin(m).count("1") / 4.0
            pri.append((wsum / max(c / max(er, 1.0), b / max(eb, 1.0)), i))
        pri.sort(reverse=True)
        return [i for p, i in pri if p > 0 or not self.look]

    def charge(self, rec, abytes):
        c = self.cost(rec)
        self.cpu.spend(c)
        self.disk.spend(len(rec) - abytes)
        self.end(c)
        return c


# --------------------------------------------------------------------------
# the source
# --------------------------------------------------------------------------
def ffmpeg_video(src, w, h, crop_dar, fps_expr, start, end, eq, pix="gray"):
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
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    ch = 3 if pix == "rgb24" else 1
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


def ffmpeg_audio(src, rate, start, end, volume):
    cmd = ["ffmpeg", "-v", "error", "-nostdin"]
    if start:
        cmd += ["-ss", "%.3f" % start]
    cmd += ["-i", src]
    if end:
        cmd += ["-t", "%.3f" % (end - (start or 0))]
    af = ["aresample=%d" % rate]
    if volume:
        af.append("volume=%s" % volume)
    cmd += ["-vn", "-ac", "1", "-af", ",".join(af), "-f", "u8", "-acodec",
            "pcm_u8", "-"]
    return subprocess.run(cmd, capture_output=True, check=True).stdout


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


# the steps an encode reports to `progress`, and the share of the whole
# each is shown as: reading and dithering, and encoding, are nearly all of it
STEPS = (("prepare", 0.03), ("read", 0.45), ("sound", 0.04),
         ("encode", 0.46), ("write", 0.02))


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
AIM_WORTH = 1.0         # --aim size's floor: pixels fixed a byte of stream


def aim_quality(a, tick, say):
    """--aim quality (98.2.1.4): what the budget can afford, found by
    TRYING it. Trial encodes of the clip (or three windows of a long one)
    at the asked settings, then a box a step up the preset's ladder at a
    time, then 22,050 Hz sound; each is taken while its picture stays
    within AIM_ERR of the asked one's error as seen and cuts no more than
    AIM_CUT more of its frames. -> the args to encode with"""
    import copy
    import tempfile
    dur = probe(a.src)[4] or 0.0
    end = a.end if a.end is not None else dur
    span = max(0.0, end - a.start)
    if span <= AIM_WHOLE:
        wins = [(a.start, end)]
    else:
        wins = [(a.start + span * f - AIM_WINDOW / 2,
                 a.start + span * f + AIM_WINDOW / 2)
                for f in (1 / 6.0, 0.5, 5 / 6.0)]

    def trial(args):
        errs, cuts = [], []
        with tempfile.TemporaryDirectory() as tmp:
            for i, (t0, t1) in enumerate(wins):
                b = copy.copy(args)
                b.start, b.end = t0, t1
                b.out = os.path.join(tmp, "t%d.V88" % i)
                b.quiet, b.preview_png, b.aim = True, None, "asked"
                b.adpcm = "greedy"
                tick("prepare", 0, 0)  # (a trial: the window waits)
                res = _encode(b, None, lambda *x: None, [])
                errs.append(res.get("q_vis", 0.0))
                cuts.append(res.get("cutf", 0.0))
        return sum(errs) / len(errs), sum(cuts) / len(cuts)

    def fine(q, q0):
        return q[0] <= q0[0] * AIM_ERR + AIM_ERR0 and q[1] <= q0[1] + AIM_CUT

    q0 = trial(a)
    say("   aim quality: as asked, %.2f%% error as seen, %.0f%% of frames "
        "cut" % (100 * q0[0], 100 * q0[1]))
    best = a
    lad = next((l for l in LADDERS if a.preset in l), None)
    if lad and not a.box and not a.live:
        for p in lad[lad.index(a.preset) + 1:]:
            b = copy.copy(best)
            b.preset = p
            q = trial(b)
            ok = fine(q, q0)
            say("   aim quality: %s, %.2f%% and %.0f%% - %s" % (
                p, 100 * q[0], 100 * q[1], "taken" if ok else "not taken"))
            if not ok:
                break
            best = b
    rate = a.rate or PROFILES[a.profile]["rate"]
    if a.audio != "none" and rate < AIM_SOUND and not a.live:
        b = copy.copy(best)
        b.rate = AIM_SOUND
        q = trial(b)
        ok = fine(q, q0)
        say("   aim quality: %d Hz sound, %.2f%% and %.0f%% - %s" % (
            AIM_SOUND, 100 * q[0], 100 * q[1],
            "taken" if ok else "not taken"))
        if ok:
            best = b
    return best


def encode(a, keep=None, progress=None):
    """`keep`, a list, is given every frame's target: the gate's.
    `progress(step, done, total)` is called as the encode goes - `step` one
    of STEPS, `total` 0 when the step has no count - and may raise
    Cancelled to stop it (98.2.11)"""
    readers = []
    tick = progress or (lambda step, done, total: None)
    try:
        if getattr(a, "aim", "asked") == "quality":
            a = aim_quality(a, tick, (lambda *x: None) if a.quiet else print)
        return _encode(a, keep, tick, readers)
    finally:
        # however it leaves - finished, refused or cancelled - the ffmpeg
        # reader is closed, and a live ffmpeg with it
        for r in readers:
            r.close()


def _encode(a, keep, tick, readers):
    tick("prepare", 0, 0)
    need_tools()
    prof = dict(PROFILES[a.profile])
    if a.live and prof["avg"] is not None:
        prof["avg"] = LIVE_AVG          # (the decode and the blit: 98.2.7)
    for k in ("disk", "avg", "peak", "owe", "reserve"):
        if getattr(a, k) is not None:
            prof[k] = getattr(a, k)
    lay, bw, bh = PRESETS[a.preset] if a.preset else (a.layout, None, None)
    if a.live:
        # LIVE (98.2.7): one bit on LIN80, the band OSAPI_GFX_BLIT1 takes,
        # at the named screen's pixel shape and the logo's box - or on VGA
        # the sixteen colours' four planes, which GFX_BLITP takes (98.3.10.4)
        if a.pixfmt not in (None, "mono") and \
                (a.pixfmt, a.live) != ("vga4", "vga"):
            raise vid.V88Error("--live is one bit, or --pixfmt vga4 with "
                               "--live vga: no --pixfmt %s" % a.pixfmt)
        a.pixfmt, a.resident = a.pixfmt or "mono", True
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
    # a C512 or a TEXT box is in CELLS, two bytes each (98.1.3.5)
    ppb = 4 if cga4 else 0.5 if c512 or text else vid.PIX_PER_BYTE[L]
    if bw > stride * ppb or bh > rows:
        raise vid.V88Error("a %d x %d box does not fit %s (%d x %d)"
                           % (bw, bh, lay, stride * ppb, rows))
    sw, sh, dar, sfps, dur, has_audio = probe(a.src)
    pasp = vid.CGA4_ASPECT if cga4 else LIVE_ASPECT[a.live] if a.live \
        else None
    w, h, crop = canvas_size(lay, bw, bh, dar, a.fit, pasp)
    vga8 = L in vid.VGA8_LAYOUTS
    # THE DETAIL (98.2.4): the picture made at a W-th of the width and an
    # H-th of the height, each pixel repeated W times along its row and each
    # row shown H times by the VGA itself (the file's row scale) - the same
    # screen, a fraction of the bytes
    detail = a.detail or PRESET_DEFAULTS.get(
        None if a.live else a.preset, {}).get("detail", "1x1")
    dw, dh = (int(v) for v in detail.lower().split("x"))
    if (dw, dh) != (1, 1) and not vga8:
        raise vid.V88Error("--detail is VGA8's (a one-bit pixel has nothing "
                           "to repeat into)")
    if dw not in (1, 2, 4) or dh not in (1, 2):
        raise vid.V88Error("--detail %s: the width 1, 2 or 4, the height "
                           "1 or 2" % detail)
    h -= h % dh
    vga4 = a.pixfmt == "vga4"
    if vga4 and L != vid.LAY_LIN80:
        raise vid.V88Error("--pixfmt vga4 is mode 12h's: the lin80 layout")
    if vga4 and (dw, dh) != (1, 1):
        raise vid.V88Error("--detail is VGA8's")
    g = vid.Geom(L, int(w // ppb), h // dh, bitplanes=vga4)
    if a.pixfmt is not None and (a.pixfmt == "vga8") != vga8:
        raise vid.V88Error("--pixfmt vga8 is the lin320 and modex layouts', "
                           "and the only format they take")

    # 256 colours are a byte a PIXEL: at 30 fps a moving camera wants ~500
    # KB/s where 15 wants ~250 (VIDEO-PLAN W11), so VGA8 defaults to 15 -
    # and its two presets to 25, at half the width (98.2.10)
    fps = a.fps or min(PRESET_DEFAULTS.get(
        None if a.live else a.preset, {}).get(
        "fps", 15.0 if vga8 else 30.0), sfps)
    audio = a.audio or prof["audio"]
    spk = audio == "speaker"
    if spk:                             # PCM8, at a rate the speaker plays
        audio = "pcm8"
        r = a.rate or SPK_RATE
        if not SPK_MIN <= r <= 16124:
            raise vid.V88Error("--audio speaker: %d Hz is not a rate the PWM "
                               "plays (%d..16124)" % (r, SPK_MIN))
        if prof["avg"] is not None and prof.get("speed") == 1 \
                and r > SPK_MAX_8088:
            raise vid.V88Error("--audio speaker: an 8088 plays at most %d Hz "
                               "through the speaker - VIDEO.O88 mutes %d "
                               "(SPEC.md 98.3.15)" % (SPK_MAX_8088, r))
        a.rate = r
    if audio == "none" or not has_audio:
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
        spk_share = rate * (CYC_SPK_PULSE + CYC_SPK_BYTE) / (
            vid.HZ * (prof.get("speed") or 1))
        if spk_share >= 0.8:
            raise vid.V88Error("--audio speaker: %d Hz takes %.0f%% of this "
                               "machine and leaves too little to draw with"
                               % (rate, 100 * spk_share))
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
    frames = ffmpeg_video(              # (TEXT: eight by eight a cell)
        a.src, w // 4 if pattern else lw * 8 if text else lw,
        lh * 8 if text else lh, crop,
                          "%d/%d" % (rate, spf), a.start, a.end,
                          ":".join(eq),
                          "rgb24" if comp or vga8 or vga4 or cga4 or c160
                          or c512 or text else "gray")
    # the frame count to expect, for the progress: the clip's length at the
    # file's rate (ffmpeg's own count can differ by a frame or two)
    est = max(1, round(((a.end or dur or 0) - (a.start or 0)) * fps)) \
        if (a.end or dur) else 0

    def counted(it):
        for n, f in enumerate(it, 1):
            tick("read", n, est)
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
    tcol = None
    if text:
        tcol = a.text_colour or PRESET_DEFAULTS.get(a.preset, {}).get(
            "text_colour", "colour")
    if a.levels == "auto" and not (vga8 or vga4 or cga4 or c160 or c512 or
                                   (text and tcol == "colour")):
        lo, hi = auto_levels(a.src, w, h, crop, a.start, a.end, ":".join(eq))
        say("   levels: grey %d..%d stretched to 0..255" % (lo, hi))
        frames = (stretch(f, lo, hi) for f in frames)
    palette = None
    cgapal = None
    if cga4:
        frames = list(frames)
        cgapal = cga4_pick(frames, a.cga_bg, a.cga_palette, a.cga_bright)
        cols = vid.cga4_colours(cgapal)
        say("   CGA4 palette %02Xh: colours %s" % (
            cgapal, " ".join(str(c) for c in cols)))
        pal6 = b"".join(vid.STD16[3 * c:3 * c + 3] for c in cols)
        k4 = KnollDitherer(lw, lh, pal6, a.vga4_stable)
        dith = lambda f, k4=k4: pack_pixels(k4(f), 4)
    elif c160:
        k16 = KnollDitherer(lw, lh, vid.STD16, a.vga4_stable)
        dith = lambda f, k16=k16: pack_pixels(k16(f), 2)
    elif text:
        cgapal = vid.TEXT_COLOUR if tcol == "colour" else vid.TEXT_MONO
        tm = TextMatcher(lw, lh, cgapal, a.text_glyphs, a.text_stable,
                         a.text_sharpen, a.text_detail, a.text_busy,
                         top=16 if cgapal else 32,
                         prefer=a.text_prefer_colour, ocr_exact=a.text_ocr,
                         ocr_large=a.text_ocr_large)
        dith = tm
        if a.text_ocr or a.text_ocr_large:  # a copy of each frame to READ
            ocr = TextOCR(a.text_ocr_conf, a.text_ocr_every, a.jobs)
            big = ffmpeg_video(a.src, lw * OCR_CW, lh * OCR_CH, crop,
                               "%d/%d" % (rate, spf), a.start, a.end,
                               ":".join(eq), "rgb24")
            readers.append(big)
            frames = zip(frames, ocr.words(big))
            dith = lambda fw, tm=tm: tm(fw[0], fw[1])
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
        dith = C512Ditherer(lw, lh, cgapal, a.c512_dither, a.c512_stable,
                            a.c512_mix, a.vga4_stable)
        say("   C512 for %s CGA: %s" % (
            {"old": "the OLD", "new": "the NEW", "both": "either"}[
                a.cga_card],
            "a pattern of %d codes" % a.c512_mix if a.c512_mix >= 2 else
            "dither %g, dead band %g" % (a.c512_dither, a.c512_stable)))
    elif vga4:
        dith = KnollDitherer(lw, lh, vid.STD16, a.vga4_stable)
    elif vga8:
        palette = vga8_palette(a.src, lw, lh, crop, "%d/%d" % (rate, spf),
                               a.start, a.end, ":".join(eq),
                               lambda: tick("prepare", 0, 0))
        d8 = Vga8Ditherer(lw, lh, palette, a.vga8_dither, a.vga8_stable)
        dith = d8 if dw == 1 else \
            (lambda f, d8=d8: np.repeat(d8(f), dw, axis=1))
    elif pattern:
        dith = CompDitherer(w, h, a.mix, a.stable, n=a.levels_mix)
    elif comp:
        dith = CompDiffuser(w, h, a.comp_stable, not a.comp_quick)
    else:
        dith = Ditherer(a.dither, w, h, a.stable, a.invert, a.clip)
    if a.flip and L != vid.LAY_MODEX:
        raise vid.V88Error("--flip is Mode X's: 13h has one page")
    enc = EncoderX(g, prof, fps, audio_cyc, audio_bps, palette, a.flip) \
        if L == vid.LAY_MODEX else \
        EncoderP(g, prof, fps, audio_cyc, audio_bps) if vga4 else \
        Encoder(g, prof, fps, audio_cyc, audio_bps, palette)
    enc.live = bool(a.live)
    enc.spk = spk_share
    # WHAT A CUT FRAME SPENDS ON (98.2.1.2)
    enc.look, enc.vis = a.lookahead, a.error == "visible"
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
                    aspect=scaled_aspect(pasp or vid.ASPECT[L], dh),
                    loop=None if a.loop_from is None else
                    max(0, round(a.loop_from * fps)),
                    repeat=a.repeat, spk=spk and bool(afmt))
    if not a.resident and enc.disk.per is not None:
        wr.ring = vid.ring_for(enc.reserve)     # (98.2.1.3)
        if wr.ring is None:
            raise vid.V88Error(
                "--reserve %d KB: the player's ring banks %d KB at most"
                % (enc.reserve // 1024,
                   (vid.RING_SLOTS[-1] - 1) * vid.SLOT // 1024))
    pcm = ffmpeg_audio(a.src, rate, a.start, a.end, a.volume) if afmt else b""
    cyc, recs, n = [], [], 0
    pend = []
    if isinstance(dith, CompDiffuser) and (a.jobs or os.cpu_count() or 1) > 1:
        pend = comp_parallel(frames, w, h, a, a.jobs or os.cpu_count(),
                             tick=lambda n: tick("read", n, est))
    else:
        for grey in frames:
            pend.append(dith(grey))
    if text and (a.text_ocr or a.text_ocr_large):
        say("   OCR: %d words drawn exact, %d crisp (a word counts once a "
            "frame)" % tuple(tm.ocr_used))
    nf = len(pend)
    if not nf:
        raise vid.V88Error("no frames came out of %s" % a.src)
    # THE PRE-ROLL (SPEC.md 98.2.9): a first picture the budgets cannot
    # paint in one frame is HELD - the source's frame 0 again, over silence -
    # until it is whole, and the keyframes start there. Key 0 was a snapshot
    # of a half-painted screen, and a colour play STARTS from key 0, so the
    # first frames of a colour file came out streaked (the owner's report: a
    # 320 x 180 VGA8 frame is 57,600 bytes against a 30 KB record)
    pre = preroll(enc, pend[0], abytes if afmt and not a.resident else 0,
                  round(PRE_SECS * fps))
    if pre:
        pend = [pend[0]] * pre + pend
        nf += pre
        if afmt:
            pcm = b"\x80" * (pre * spf) + pcm
        wr.key0 = pre
        if wr.loop is not None:
            wr.loop += pre
        say("   pre-roll: %d frame(s) hold the first picture until it is "
            "whole (%.2f s); the keyframes start there" % (pre, pre / fps))
    if keep is not None:            # the targets frame for frame, the
        keep.extend(pend)           # pre-roll's included
    jobs = a.jobs or os.cpu_count() or 1
    tick("sound", 0, 0)
    # a RESIDENT file's ADPCM4 ends where its lap joins (98.1.7.2): the
    # seam's frame L, or 80h at scale 0 before frame 0
    join = (wr.loop if wr.loop is not None else "start") \
        if a.resident and afmt == vid.AUD_ADPCM4 else None
    chunks = vid.audio_chunks(pcm, nf, spf, afmt, vid.key_frames(
        nf, wr.keyint, wr.key0), search=jobs if a.adpcm == "search" else 0,
        join=join) if afmt else None
    if spk and chunks:                  # THE SPEAKER'S COUNTS, not samples
        chunks = [vid.spk_counts(c, rate) for c in chunks]  # (98.1.1.3)
    sound = []
    for f, target in enumerate(pend):
        tick("encode", f, nf)
        au = chunks[f] if afmt else b""
        if a.resident:              # the sound is ONE block (98.1.7)
            sound.append(au)
            au = b""
        enc.future = pend[f + 1:f + 1 + enc.look]
        ops, rec = enc.frame(target, au)
        cyc.append(enc.charge(rec, len(au)) + audio_cyc)
        if enc.metric:
            enc.measure(target)
        wr.frame(ops, enc.surf, au)
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
                pl = np.frombuffer(vid.STD16 if vga4 else palette,
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
        if not a.quiet and f % 300 == 299:
            print("   frame %d of %d" % (f + 1, nf), file=sys.stderr)
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
            say("   NOTE: --poster-at %g: the file has no keyframes, so it "
                "has no poster (and no seek) - it plays from the start"
                % a.poster_at)
            poster = None
    if a.resident:
        st = vid.write_resident(
            a.out, [wr], afmt, abytes if afmt else 0, b"".join(sound),
            wr.title, wr.credits, a.repeat, vid.PK_LZB,
            posters=[poster] if poster is not None else None,
            live=[vid.TARGETS[a.live]] if a.live else None,
            spk=spk and bool(afmt))
        vid.verify_v88(a.out)
        rr = vid.Reader(a.out)
        res = dict(bytes=st["bytes"], stream=st["blocks"][0][0] +
                   st["audio"][0], keys=len(wr.keys),
                   keybytes=sum(len(r) for k, r, c in wr.keys),
                   poster=rr.poster)
        say("   RESIDENT: the block %d bytes, %d packed%s"
            % (st["blocks"][0] + (", LIVE on the %s desktop" % a.live
                                  if a.live else "",)))
        if a.live:                  # 98.3.10.2: what the blits will cost
            bl = [enc.blitf * sum(vid.blit_cost(y1 - y0, x1 - x0,
                                                rr.g.planes)
                      for y0, y1, x0, x1 in vid.runs_of(
                          rec, vid.lists_end(rec, rr.g), rr.g)[0])
                  for rec, _, _ in rr.records()]
            say("   Live's blit alone (model, a pass a tick): mean "
                "%.1f%%, worst %.1f%% of a frame's period"
                % (100 * sum(bl) / len(bl) / enc.period,
                   100 * max(bl) / enc.period))
    else:
        res = wr.write(a.out, poster)
    res.update(fps=fps, period=enc.period, audio_cyc=audio_cyc,
               spk_share=spk_share,
               audio_bps=audio_bps, prof=prof, w=w, h=h, layout=lay,
               palette=palette)
    # the header's own figure, which counts what write() appended - an
    # ADPCM4 key's reference byte (98.1.1.1) is not in wr.keys
    kmax = vid.Reader(a.out).kmax
    if kmax > KEY_PLAYER:
        say("   NOTE: the largest keyframe is %d bytes, past the %d the "
            "player reads in one go off a volume of %d KB clusters (a hard "
            "disk%s): there it will play from the start only, with no "
            "poster and no seek"
            % (kmax, KEY_PLAYER, KEY_CLB // 1024,
               "; a floppy's 1 KB ones take %d" % key_limit(1024)
               if kmax <= key_limit(1024) else ", or a floppy"))
    secs = nf / fps
    st = enc.stats
    say("   %d frames, %.1f s: %d bytes = %.1f KB/s (%.1f video, %.1f "
        "audio)" % (nf, secs, res["bytes"], res["bytes"] / 1024.0 / secs,
                    (res["stream"] - audio_bps * secs) / 1024.0 / secs,
                    audio_bps / 1024.0))
    say("   CPU (wave 0 model, audio copy in): mean %.1f%%, worst %.1f%%%s; "
        "%d frames exact, %d cut to the budget (%.1f bytes a frame left "
        "wrong)" % (100 * sum(cyc) / nf / enc.period,
                    100 * max(cyc) / enc.period,
                    " + the speaker's %.0f%%" % (100 * spk_share)
                    if spk_share else "", st["exact"], st["cut"],
                    st["bytes_left"] / max(1, st["cut"])))
    if st["cut"]:
        say("   cut by: the disk %d, the CPU's average %d, the per-frame "
            "ceiling %d" % (st["disk"], st["cpu"], st["peak"]))
    if getattr(wr, "ring", 0):
        say("   disk reserve %d KB: the player's ring of %d slots, %d KB of "
            "memory to play it (98.2.1.3)" % (
                enc.reserve // 1024, wr.ring, (wr.ring + 1) * 32))
    if enc.metric:
        say("   picture: %.2f%% error as seen, %.2f%% of pixels wrong, "
            "%.0f pixels a frame flickering back (the source's own: %.0f)"
            % (100 * enc.q_vis / nf, 100 * enc.q_wrong / nf,
               enc.q_flick / max(1, nf - 2), enc.q_tflick / max(1, nf - 2)))
    res.update(q_vis=enc.q_vis / nf, q_wrong=enc.q_wrong / nf,
               q_flick=enc.q_flick / max(1, nf - 2), cutf=st["cut"] / nf)
    if st["skipped"]:
        say("   aim size: %d changes not worth their bytes left off"
            % st["skipped"])
    if enc.owe is not None:
        say("   owed time: %d frames ran past their period, %d drawn a "
            "period late while it was paid back, %d dropped"
            % (st["owed"], st["held"], st["late"]))
    say("   %d keyframes = %d bytes (%.1f%% of the file), poster %d"
        % (res["keys"], res["keybytes"],
           100.0 * res["keybytes"] / res["bytes"], res["poster"]))
    return res


def parser():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("src")
    ap.add_argument("out")
    ap.add_argument("--preset", choices=sorted(PRESETS),
                    help="a named box on a layout (--profiles lists them); "
                         "the cga4, cga4-small and c160 ones name their "
                         "colour format too")
    ap.add_argument("--layout", choices=sorted(vid.LAYOUT_BY_NAME),
                    type=vid.layout_name,
                    help="the screen memory the file is laid out for "
                         "(SPEC.md 98.1.2), overriding the preset's")
    ap.add_argument("--box", help="WxH: the canvas's largest size")
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
    ap.add_argument("--profile", choices=sorted(PROFILES),
                    default="5150-st225",
                    help="the machine's storage and CPU budget, which every "
                         "frame is fitted to (--profiles says what each is)")
    ap.add_argument("--disk", type=float, help="the profile's bytes a "
                    "second, overridden")
    ap.add_argument("--avg", type=float, help="...its average CPU share")
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
    ap.add_argument("--lookahead", type=int, default=2, metavar="N",
                    help="WHEN A FRAME IS CUT, don't pay for pixels about "
                         "to change: a change is ranked by what it is worth "
                         "over the next N frames too, and one the picture is "
                         "about to undo is not sent (98.2.1.2). Default 2; "
                         "0 ranks by this frame alone, as before")
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
    ap.add_argument("--jobs", type=int, help="cores for the search "
                    "(default: all)")
    ap.add_argument("--volume", help="ffmpeg's volume= (e.g. 1.5, 3dB)")
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
                             "dots-plus"),
                    default="blocks",
                    help="text: the characters the picture is made of - "
                         "printable ASCII and, with shades, the four "
                         "shades, and with blocks (the default) the half "
                         "blocks too, which every ROM draws alike and which "
                         "are the clearest - or dots: the full and half "
                         "blocks for shapes and , . ' ` for the edges and "
                         "dithers a block is too coarse for, and dots-plus "
                         "those with : ; \" * as well")
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
    ap.add_argument("--detail",
                    help="vga8: WxH, the picture made at a W-th of the "
                         "width (1, 2, 4) and an H-th of the height (1, 2) "
                         "and shown at full size - W by repeating pixels, "
                         "which Mode X stores 2 or 4 to the byte, H by the "
                         "VGA showing each row twice (98.2.4). Default "
                         "1x1, and 2x1 for the vga8 and modex presets")
    ap.add_argument("--vga8-dither", type=float, default=24.0,
                    help="vga8: the ordered dither's reach, in 8-bit RGB "
                         "steps across the 8 x 8 map (0: none)")
    ap.add_argument("--vga4-stable", type=float, default=24.0,
                    help="vga4: keep a pixel's colour while its source has "
                         "moved less than this RGB distance since the "
                         "colour was chosen (0: off)")
    ap.add_argument("--vga8-stable", type=float, default=18.0,
                    help="vga8: how far, in RGB distance, the colour on "
                         "the screen may be from the source and stay")
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
    ap.add_argument("--clip", type=float, default=16.0,
                    help="grey levels at each end that are solid black "
                         "or white, never a dot")
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
    ap.add_argument("--preview-png", metavar="DIR",
                    help="the screen once a second, as PNGs")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--profiles", action="store_true",
                    help="list the profiles and presets")
    return ap


PROGRESS = "@@progress"


def progress_lines():
    """--progress (98.2.11.1): each step's progress as a line on stdout -
    PROGRESS, the step, done, total - for the encoder window, which runs
    this encoder as a process of its own so Cancel can kill it. A line a
    step, and then one a percent, not one a frame"""
    last = [None]

    def tick(step, done, total):
        key = (step, 100 * done // total if total else 0)
        if key != last[0]:
            last[0] = key
            print("%s %s %d %d" % (PROGRESS, step, done, total), flush=True)
    return tick


def main():
    ap = parser()
    if "--profiles" in sys.argv:
        for k, p in PROFILES.items():
            print("%-14s %s" % (k, p["what"]))
        for k, (lay, w, h) in PRESETS.items():
            print("%-14s %s %d x %d" % (k, lay, w, h))
        return 0
    a = ap.parse_args()
    if a.preview_png:
        os.makedirs(a.preview_png, exist_ok=True)
    try:
        encode(a, progress=progress_lines() if a.progress else None)
    except (vid.V88Error, subprocess.CalledProcessError) as e:
        sys.exit("os88venc: %s" % e)
    return 0


if __name__ == "__main__":
    sys.exit(main())
