#!/usr/bin/env python3
"""The video encoder with a window on it (VIDEO-PLAN 14.2, E1-E4).

    python3 tools/os88vencgui.py

Same engine, different face - `tools/os88proxygui.py`'s rule one tool along:
**it imports `os88venc` and reimplements none of it.** The window's settings
become the argv `os88venc.parser()` reads, the encode is `os88venc.encode`,
and what it prints is the log. So the form is DERIVED from that parser:
every option the encoder has is on a tab here, with its own help text as
the tooltip, and an option added to the encoder tomorrow appears here with
nothing to do - `tests/vencguitest.py` fails if one has no tooltip.

What the window adds is ASSISTANCE, in the order a person needs it:

    1. What am I making it FOR?   - a target machine, which sets the
                                     preset, the pixel format and the
                                     storage profile together
    2. What is my video?          - ffprobe's answer, and the defaults it
                                     implies (its rate, its length, whether
                                     it has sound)
    3. What will it look like?    - a scrubber over the ENCODED frames as
                                     the adapter shows them: CGA's colours,
                                     a VGA's palette, composite through the
                                     monitor model, one bit as one bit -
                                     and, on the Colour tab, the palette
                                     the choices give, as swatches
                                     (SPEC.md 98.2.8.1)
    4. How do I get it onto the machine? - Save writes the .V88; "...and
                                     make a disk of it" puts it and
                                     VIDEO.O88 on a floppy image of any of
                                     the four sizes, or on a BOOTABLE hard
                                     disk: 20 MB on an ST11M, 32 MB on an
                                     ST11R, 32 MB IDE (SPEC.md 98.2.12.1) -
                                     formatted but not bootable where
                                     there is no built os8088 to put on it

tkinter because it is in the standard library: one file, no pip. Tk is
imported SOFTLY for the proxy GUI's reason - everything above `App` loads
on a machine with no Tk, so the test can check the table, the argv and the
preview renderer without a display.
"""
import base64
import io
import os
import queue
import subprocess
import sys
import threading
import time
import traceback

try:
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    from tkinter import font as tkfont
except Exception:                                            # pragma: no cover
    tk = None
    filedialog = messagebox = ttk = tkfont = None

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(_HERE)
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)
import os88venc as V                                          # noqa: E402
import os88vid as vid                                         # noqa: E402
from os88drop import enable_drop, make_root                   # noqa: E402,F401

APPNAME = "os8088 video encoder"

# WHAT IT IS FOR: a machine, which is three settings at once. The profile
# is the storage and CPU budget (os88venc's PROFILES say what each is); a
# fifth element is anything else the choice sets.
#   label                                        preset   pixfmt     profile
TARGETS = [
    ("IBM 5150/XT, CGA - black and white", "cga", "mono", "5150-st225"),
    ("IBM 5150/XT, CGA - 4 colours, full screen", "cga4", "cga4",
     "5150-st225"),
    ("IBM 5150/XT, CGA - 16 colours at 160 x 100, full screen", "c160",
     "c160", "5150-st225"),
    ("IBM 5150/XT, CGA on a composite monitor - colour", "cga", "cgacomp",
     "5150-st225"),
    ("IBM 5150/XT, CGA on a composite monitor - 512 colours, full screen",
     "c512", "c512", "5150-st225"),
    ("Any PC - text mode, 16 colours, full screen", "text", "text",
     "5150-st225"),
    ("Any PC, Hercules and MDA too - text mode, black and white, full "
     "screen", "text-mono", "text", "5150-st225"),
    ("IBM 5150/XT, Hercules - black and white", "herc", "mono",
     "5150-st225"),
    # no sound card: the preset carries the speaker's 8 kHz and 23 fps, and
    # a CGA is its twin, cga-spk, a preset away rather than a longer list
    ("IBM 5150/XT, PC speaker - Hercules (preset cga-spk: CGA)",
     "herc-spk", "mono", "5150-st225"),
    ("IBM 5150/XT, a floppy - small and slow", "cga-small", "mono",
     "floppy"),
    ("286, VGA - 16 colours in the window", "vga4", "vga4", "286-fast"),
    ("286, VGA - 256 colours, full screen", "vga8", "vga8", "286-fast"),
    ("286, VGA - Mode X, 256 colours, square pixels", "modex", None,
     "286-fast"),
    ("486, VGA - 16 colours in the window", "vga4-mid", "vga4", "486-dx2-66"),
    ("486, VGA - Mode X, 256 colours, square pixels", "modex", None,
     "486-dx2-66"),
    ("Live on a CGA desktop - short, black and white, read whole", None,
     None, "5150-st225", {"live": "cga"}),
    ("Live on a Hercules desktop - short, black and white", None, None,
     "5150-st225", {"live": "herc"}),
    ("Live on a VGA desktop - short, black and white", None, None,
     "286-fast", {"live": "vga"}),
]
TARGETS = [t + ({},) if len(t) == 4 else t for t in TARGETS]

# the tabs, and the GROUPS on each (98.2.8.2): an outline with a header
# round the options that go together, and the whole group greyed out when
# the file the form makes cannot use it - the CGA 4-colour group on
# anything but CGA 4 colours, the PC speaker's on any other sound. An
# option named in no group still appears, on Basic, in "Other"
TAB_ROWS = 10    # more options than this and a tab takes two columns
PAL_W = 600      # the Palette panel's width: the Colour tab's (620 ran
                 # its outline 7 pixels past the tab's edge)
FPAL_W = 390     # ...and the open file's, in its box
TABS = ("Basic", "Picture", "Colour", "Sound", "Budget", "Trade-offs",
        "Degrade to Fit")
# what the form makes, in the words a reason uses
PF_WORDS = {"mono": "one bit, black and white",
            "cgacomp": "CGA composite colour",
            "vga8": "256 colours on a VGA", "vga4": "16 colours on a VGA",
            "cga4": "CGA 4 colours", "c160": "CGA 16 colours at 160 x 100",
            "c512": "CGA composite, 512 colours", "text": "text mode"}
AUDIO_WORDS = {"pcm8": "8-bit sound for a Sound Blaster",
               "adpcm4": "ADPCM for a Sound Blaster",
               "speaker": "the PC speaker", "none": "no sound"}


def _for(pfs, what, these="These are"):
    """A group's rule: it applies to the pixel formats `pfs`, and says so -
    or one option's, `these` then "This is" (FIELD_WHEN)"""
    def when(c):
        if c["pixfmt"] in pfs:
            return None
        return ("%s for %s, and this file is %s. Choose a target "
                "that makes %s under Made for, or its pixel format on "
                "Basic." % (these, what, PF_WORDS[c["pixfmt"]], what))
    return when


def _sound(kind, what):
    def when(c):
        if c["audio"] == kind:
            return None
        return ("These are for %s, and this file's sound is %s. Choose %s "
                "as the Audio above, or a target that uses it under Made "
                "for." % (what, AUDIO_WORDS.get(c["audio"], c["audio"]),
                          kind))
    return when


def _shaping(c):
    return _sound("speaker", "the PC speaker")(c) or (
        None if c["spk_shape"] == V.SPK_ENC else
        "These shape the sound for the speaker in the encoder, and Spk "
        "shape is machine: the file is a plain 8-bit WAV at the rate, and "
        "Audio shapes it itself as it plays. Set Spk shape to encoder to "
        "use them."
        if c["spk_shape"] == V.SPK_MACH else
        "These shape the sound for the speaker, and Spk shape is none: the "
        "sound goes on the speaker as it is. Set Spk shape to encoder to "
        "use them.")


def _pixel_kept(c):
    if c["pixfmt"] == "mono" or (c["pixfmt"] == "cgacomp"
                                 and c["comp_dither"] == "pattern"):
        return None
    return ("This holds a pixel for the one-bit dither and the composite "
            "PATTERN dither, and this file is %s%s."
            % (PF_WORDS[c["pixfmt"]], " made by diffusion (Comp dither)"
               if c["pixfmt"] == "cgacomp" else ""))


def _greys(c):
    if c["pixfmt"] in ("mono", "cgacomp") or (
            c["pixfmt"] == "text" and c["text_colour"] == "mono"):
        return None
    return ("The stretch is for a picture made of greys, and this file is "
            "%s, whose colours are chosen from the clip as they are."
            % (PF_WORDS[c["pixfmt"]] + (" in colour" if c["pixfmt"] ==
                                        "text" else "")))


ALWAYS = lambda c: None


def _ahead(c):
    """BUFFER SOUND AHEAD (98.1.8): a streamed file's, with sound"""
    if c["audio"] == "none":
        return "The file has no sound to carry ahead."
    if c["live"] or c["resident"]:
        return ("A %s file's sound is held in memory and never waits on "
                "the disk, so there is nothing to carry ahead."
                % ("Live" if c["live"] else "resident"))
    return None
# A SPEAKER WAV (SPEC.md 86.21.1) is the sound alone: of the groups only
# these apply to it, and the rest say so
WAV_KEEP = ("Made for", "The clip", "Sound", "PC speaker",
            "PC speaker: the sound shaped")
WAV_WHY = ("This file is a speaker WAV for Audio - the sound alone, shaped "
           "for the PC speaker (SPEC.md 86.21.1) - so it has no picture. "
           "Save as a .V88 to use these.")
# (tab, header, options, rule): a rule is None where the group applies,
# else the reason it does not - the group's tooltip while it is greyed
def _bank(c):
    """The bank is a STREAM's, read off a disk: not a resident or Live
    file's, and not a lossless profile's, which has no disk to budget"""
    if c["resident"] or c["live"]:
        return ("A bank reads a stream ahead off the disk, and this file is "
                "%s." % ("Live" if c["live"] else "held whole in memory"))
    if c["profile"] == "lossless":
        return "The lossless profile has no disk budget for a bank to deepen."
    return _nospk(c)


def _nospk(c):
    """NOTHING IN XMS UNDER THE SPEAKER (98.3.18.5): the bank, the layer and
    Live from XMS all ride XMS copies, and the player makes none while the
    speaker's interrupts are its clock"""
    if c["audio"] == "speaker":
        return ("The sound is the PC speaker, whose clock is one interrupt a "
                "sample - and an XMS copy holds interrupts off, so the "
                "player reads the disk instead and makes none.")
    return None


def _tearing(c):
    """BANDS AND A SECOND PAGE: Mode X and 16 colours (bands, 98.2.1.2.2,
    and a page on a screen of their own, 98.3.8), and a Hercules (page-ahead
    only, 98.3.8.3). Which of them applies is each option's own rule"""
    if c["layout"] in ("modex", "herc") or c["pixfmt"] == "vga4":
        return None
    return ("Bands and a second page are Mode X's, 16 colours' and (page-"
            "ahead) a Hercules's, and this file is %s."
            % ("mode 13h" if c["pixfmt"] == "vga8" else
               PF_WORDS[c["pixfmt"]]))


def _desk(c):
    """DESKTOP POSTERS (98.1.3.11): the Preview's size is any file's but a
    Live one's, which plays in its window at its own size"""
    if c["live"]:
        return ("A Live file plays in its window at its own size, so its "
                "Preview is that size.")
    return None


def _held(c, what):
    """None unless the file is held WHOLE - Live or resident - which then
    `what`: the read-ahead's options have nothing to act on"""
    if c["live"] or c["resident"]:
        return "A %s file is held whole in memory, and %s." % (
            "Live" if c["live"] else "resident", what)
    return None


def _scenes(c):
    """None when a palette a scene APPLIES and is chosen - what the options
    that act on its scenes need. On the checkbox alone the window wrote
    --step-down and --scene-hold under a scene choice the encoder could not
    take, and the encoder refused them (the options audit, 2026-10-14)"""
    if FIELD_WHEN["scene_palettes"](c) is None and c["scene_palettes"]:
        return None
    return "Only a palette a scene has scenes."


def _coarse(c):
    """None where COARSE CELLS pay (98.2.1.9): a planar screen, VGA's 16
    colours or Mode X, where a flat cell is one store for many pixels. On
    a packed one a flat byte is only a run - Take On Me's Hercules scene
    changes took 87-100% of their cycles in cells, Last Exile's Mode X ones
    32-51% (the owner's question, 2026-10-17)"""
    if c["pixfmt"] == "vga4" or c["layout"] == "modex":
        return None
    return ("Coarse cells pay on a planar screen - VGA's 16 colours or Mode "
            "X - where a flat cell is one store for many pixels. On this "
            "file's they would save little.")


def _scene_cuts(c):
    """None where scenes are FOUND: a palette a scene's, or coarse cells'
    on a planar screen - the same scene pass either way"""
    if _scenes(c) is None:
        return None
    if _coarse(c) is None and (c.get("coarse_cut", "auto") != "off" or
                               "coarsescene" in c.get("ladder", "") or
                               "coarse:" in c.get("ladder", "")):
        return None
    return ("Scenes are found for a palette a scene, or for coarse cells on "
            "VGA's 16 colours or Mode X.")


def _knoll_hold(c):
    """The pattern dither's HOLD: CGA's four, C160 and 16 colours - not
    composite's 512, and not a 16-colour ramp of greys"""
    if c["pixfmt"] == "c512":
        return "Composite's 512 colours are dithered their own way."
    if c["pixfmt"] == "vga4" and c["palette"].startswith("grey"):
        return "A ramp of greys is dithered level by level: no pattern."
    return None


def _knoll_own(c):
    """The pattern's cap and the sparse rule: a 16-colour file's OWN
    colours (Palette colourN or clipN)"""
    if c["pixfmt"] == "vga4" and (c["palette"].startswith("colour") or
                                  c["palette"].startswith("clip")):
        return None
    return ("These shape the pattern over a clip's own colours (Palette "
            "colourN or clipN on 16 colours).")


_FOUR16_WHAT = ("CGA 4 colours, CGA 16 colours at 160 x 100, CGA composite "
                "512 colours or 16 colours on a VGA")
_FOUR16 = _for(("cga4", "c160", "c512", "vga4"), _FOUR16_WHAT)
_FOUR16_ONE = _for(("cga4", "c160", "c512", "vga4"), _FOUR16_WHAT, "This is")


# THE OPTIONS THE DEGRADE LADDER REPLACES, whole: on with no ladder, and
# with one only what its rungs name (VIDEO-SEEN-PLAN 3a)
LADDER_SETS = ("lag", "min_fps", "coarse_cut", "step_down", "step_min")
GROUPS = [
    ("Basic", "Made for", ("preset", "pixfmt", "profile", "aim"), ALWAYS),
    ("Basic", "The clip", ("title", "credits", "start", "end", "fps"),
     ALWAYS),
    ("Picture", "Canvas", ("layout", "box", "fit"), ALWAYS),
    ("Picture", "Tone", ("gamma", "contrast", "brightness"), ALWAYS),
    ("Picture", "Grey levels", ("levels",), _greys),
    ("Picture", "One bit", ("dither", "invert"),
     _for(("mono",), PF_WORDS["mono"])),
    ("Picture", "Solid colours", ("clip",),
     _for(("mono", "vga4", "cga4", "c160", "vga8"),
          "a dithered picture - one bit, 16 colours or 256")),
    ("Picture", "Text mode", ("text_glyphs", "text_detail", "text_sharpen",
                              "text_busy"),
     _for(("text",), PF_WORDS["text"])),
    ("Picture", "Text mode: words read (OCR)",
     ("text_ocr", "text_ocr_large", "text_ocr_conf", "text_ocr_every"),
     _for(("text",), PF_WORDS["text"])),
    ("Colour", "Text mode colours", ("text_colour", "text_prefer_colour"),
     _for(("text",), PF_WORDS["text"])),
    ("Colour", "CGA, 4 colours", ("cga_palette", "cga_bright", "cga_bg"),
     _for(("cga4",), PF_WORDS["cga4"])),
    ("Colour", "4 and 16 colours", ("vga4_plan_cap", "vga4_sparse"),
     _FOUR16),
    ("Colour", "CGA composite, 512 colours",
     ("cga_card", "c512_dither", "c512_mix"),
     _for(("c512",), PF_WORDS["c512"])),
    # (in this order the tab's two columns are near one height, with the
    # tallest Palette panel - CGA4's six sets and its note - under them)
    ("Colour", "CGA composite", ("comp_dither", "comp_quick", "mix",
                                 "levels_mix"),
     _for(("cgacomp",), PF_WORDS["cgacomp"])),
    ("Colour", "VGA, 256 colours", ("vga8_dither",),
     _for(("vga8",), PF_WORDS["vga8"])),
    ("Colour", "VGA: palette and screen", ("palette", "scene_palettes",
                                           "scene_cover", "scene_min",
                                           "scene_join", "screen"),
     _for(("vga8", "vga4"), "256 or 16 colours on a VGA")),
    ("Sound", "Sound", ("audio", "rate", "volume"), ALWAYS),
    ("Sound", "Buffer sound ahead", ("buffer_sound_ahead", "ahead_frames"),
     _ahead),
    ("Sound", "ADPCM", ("adpcm",), _sound("adpcm4", "ADPCM")),
    ("Sound", "PC speaker", ("spk_shape", "spk_pulses", "spk_preview"),
     _sound("speaker", "the PC speaker")),
    ("Sound", "PC speaker: the sound shaped",
     ("spk_style", "spk_highpass", "spk_ratio", "spk_range", "spk_lows",
      "spk_drive", "spk_idle"), _shaping),
    ("Budget", "The machine's budget", ("disk", "avg", "peak", "owe",
                                        "reserve", "memory"), ALWAYS),
    ("Budget", "XMS bank (a 286 or better)", ("bank", "prefill", "pool"),
     _bank),
    # (a layer IS made beside a bank: 98.1.9's banked-and-layered file, and
    # the pool split for whichever machine plays it, 98.1.9.2)
    ("Budget", "Layer: for a faster disk too",
     ("layer_profile", "layer_disk", "layer_memory", "layer_bank",
      "layer_pool", "layer_prefill", "layer_seek", "layer_passes"),
     lambda c: _bank(c) or (
         "A layer is played unflipped." if c.get("flip") else None)),
    # SIZE VS QUALITY: what gives up some of the picture for a smaller
    # file or a lower rate, gathered off the tabs of the formats they act
    # on - which overran the Colour tab's height twice (the owner). Each
    # option keeps its format's rule, as its own (FIELD_WHEN)
    ("Trade-offs", "Hold the picture still", ("stable", "text_stable",
                                                   "comp_stable",
                                                   "vga4_stable", "vga4_hold",
                                                   "vga4_hold_score",
                                                   "c512_stable",
                                                   "vga8_stable"), ALWAYS),
    ("Trade-offs", "VGA, 256 colours: fewer pixels", ("detail",),
     _for(("vga8",), PF_WORDS["vga8"])),
    ("Trade-offs", "16 colours: the player's time to draw",
     ("vga4_order", "vga4_wm3"), _for(("vga4",), PF_WORDS["vga4"])),
    ("Trade-offs", "Scenes: fewer bytes at a cut",
     ("scene_hold",), _for(("vga4",), PF_WORDS["vga4"])),
    # (a Hercules has a second page too, so page-ahead is not VGA's alone:
    # under the VGA group it was greyed on the one screen that needs it
    # asked for by name - the options audit, 2026-10-14. Bands are the
    # same question - what a frame caught half drawn looks like, for a few
    # bytes or twice the decode - and each keeps its own rule)
    ("Trade-offs", "Tearing: bands and a second page",
     ("bands", "flip", "flip_own", "page_ahead", "pages", "page_keys",
      "key_pool", "page_prune"),
     _tearing),
    # (the DEGRADE LADDER's tab is a CHOICE, and laid out as one
    # (the owner, 2026-10-18: "ladder OR these other settings"): the
    # ladder, OR the whole-clip settings it replaces - each of which it
    # leaves OFF where no rung names it, which is how a ladder says so -
    # and then what prices and draws a held frame under either)
    ("Degrade to Fit", "Degrade ladder", ("ladder", "ladder_at"),
     ALWAYS),
    ("Degrade to Fit", "Or, without the ladder: for the whole clip",
     LADDER_SETS, ALWAYS),
    ("Degrade to Fit", "Held frames, either way", ("slow_cost",
                                                    "flip_hold"), ALWAYS),
    ("Trade-offs", "When a frame is cut", ("lookahead", "lookahead_floor",
                                                "error", "cut",
                                                "frame_cap", "edges",
                                                "attend"), ALWAYS),
    ("Trade-offs", "Aim: size", ("worth",),
     lambda c: None if c["aim"] == "size" else
     "This is --aim size's floor, and the aim is %s. Choose size as the Aim "
     "on Basic to use it." % c["aim"]),
    ("Basic", "Loop", ("loop_from", "repeat"), ALWAYS),
    ("Basic", "Held in memory", ("resident", "live", "xms"),
     ALWAYS),
    ("Basic", "Keyframes and the poster", ("keysecs", "poster",
                                                   "poster_at",
                                                   "poster_image",
                                                   "desktop_posters",
                                                   "desktop_clip",
                                                   "desktop_size"),
     ALWAYS),
    ("Basic", "This computer", ("jobs",), ALWAYS),
]
# ...and inside a group that applies, the few options narrower than it
FIELD_WHEN = {
    # (the boxes audit, 2026-10-14: these stood EMPTY on a Live target,
    # where the encoder ignores them - a file held whole has no ring to
    # size, and a Live play is a pass a tick, not the hook's owed time)
    "memory": lambda c: _held(c, "has no ring to size"),
    "reserve": lambda c: _held(c, "has no ring to bank ahead in"),
    "owe": lambda c: None if not c["live"] else "A Live file draws a pass "
    "a tick on the desktop, and owes no time at a cut.",
    "text_prefer_colour": lambda c: None if c["text_colour"] == "colour"
    else "This keeps a cell's hue, and the text is mono: it has none.",
    "bands": lambda c: ("Bands are a stream's drawn full screen, and a Live "
                        "file draws a pass a tick on the desktop."
                        if c["live"] else None) if c["layout"] == "modex" or
    c["pixfmt"] == "vga4" else "Bands are for a picture drawn a plane at a "
    "time - Mode X, or 16 colours - and %s is one plane." % (
        "mode 13h" if c["pixfmt"] == "vga8" else "a Hercules"),
    "flip": lambda c: ("A Hercules's second page is page-ahead's: flipping "
                       "every frame is a VGA's (its latches copy the page)."
                       if c["layout"] == "herc" else
                       "A NARROW screen's pages are page-ahead's (--pages): "
                       "flipping every frame is a screen with 80-byte rows'."
                       if str(c.get("screen", "")).endswith("n") else
                       "Flipping is a stream's played full screen: not Live "
                       "or resident." if c["live"] or c["resident"] else
                       None) if c["layout"] in ("modex", "herc") or (
        c["pixfmt"] == "vga4" and c["screen"] not in ("", "640x480")) else
    "Two pages are Mode X's, or 16 colours' on a Screen of their own "
    "(320x200, 320x240, 640x350, 640x400) - and %s has one page."
    % ("mode 12h's 640 x 480" if c["pixfmt"] == "vga4" else
       "mode 13h" if c["pixfmt"] == "vga8" else
       "this %s file" % c["pixfmt"]),
    # (98.1.3.11: the desktop's posters are a sixteen-colour stream's)
    "desktop_size": lambda c: _desk(c),
    # (VIDEO-SEEN-PLAN 3b: each record against its own page - the flip's,
    # in a stream with no loop or layer; with page-ahead on 3 pages or more
    # too, built ahead as well)
    "flip_own": lambda c: FIELD_WHEN["flip"](c) or (
        "Each frame against its own page is Flip's: turn Flip on."
        if not c.get("flip") else
        "A looped file or a layer is drawn against the frame before."
        if c.get("loop_from") or c.get("layer_disk") or
        c.get("layer_profile") else None),
    "lag": lambda c: "A Live file is drawn on the desktop: it keeps time "
    "frame by frame." if c.get("live") else None
    if c["audio"] not in ("none", "speaker") else
    "The lag is a sound card's: it plays from a ring while the picture "
    "catches up, and %s keeps to the frame." % (
        "the PC speaker" if c["audio"] == "speaker" else "a silent file"),
    "page_ahead": lambda c: (
        "Page-ahead is a full-screen stream's: not Live, resident or "
        "looped." if c.get("live") or c.get("resident") or
        c.get("loop_from") else
        "With Flip, page-ahead builds on the pages the flip leaves: "
        "Pages 3 or more (flipped and built ahead)." if c.get("flip") and
        str(c.get("pages", "2")) == "2" else None)
    if c["layout"] in ("modex", "herc") or (
        c["pixfmt"] == "vga4" and c["screen"] not in ("", "640x480")) else
    "Two pages are Mode X's, a Hercules's, or 16 colours' on a Screen of "
    "their own (320x200, 320x240, 640x350, 640x400) - and %s has one page."
    % ("mode 12h's 640 x 480" if c["pixfmt"] == "vga4" else
       "mode 13h" if c["pixfmt"] == "vga8" else
       "this %s file" % c["pixfmt"]),
    "pages": lambda c: (FIELD_WHEN["page_ahead"](c) if not c.get("flip")
                        else None) or (
        "Pages are page-ahead's: Page ahead is off." if
        c.get("page_ahead") == "off" else
        "A Hercules has two pages." if c["layout"] == "herc" else None),
    "page_keys": lambda c: FIELD_WHEN["pages"](c) or (
        "Keys carry the pages past the second: Pages is 2." if
        str(c.get("pages", "2")) == "2" else None),
    "key_pool": lambda c: FIELD_WHEN["pages"](c) or (
        "The pool shares pages past the second between keys: Pages is 2." if
        str(c.get("pages", "2")) == "2" else None),
    "page_prune": lambda c: FIELD_WHEN["page_ahead"](c) or (
        "Pruning is page-ahead's: Page ahead is off." if
        c.get("page_ahead") == "off" else
        "A Hercules's second page is lost to a colour card in the same "
        "machine, so auto leaves page-ahead off here: choose on for a lone "
        "Hercules." if c["layout"] == "herc" and
        c.get("page_ahead") == "auto" else None),
    "scene_palettes": lambda c: None if c["pixfmt"] == "vga4" and
    c["palette"].startswith("colour") and not (c["live"] or
                                               c["resident"]) else
    "A palette a scene is a full-screen 16-colour file's, with a Palette "
    "colour8, colour12 or colour16.",
    "scene_cover": _scene_cuts,
    "scene_join": _scenes,
    "ladder_at": lambda c: None if c.get("ladder") else
    "The ladder by time is the ladder's: turn on Use the degrade ladder.",
    "scene_min": _scene_cuts,
    "step_down": _scenes,
    "step_min": lambda c: _scenes(c) or (
        None if c["step_down"] == "on" else
        "The fewest colours a scene steps down to: it needs Step down on."),
    "coarse_cut": _coarse,
    "scene_hold": _scenes,
    # (the options audit, 2026-10-14: what each of the group's knobs
    # reaches - composite's dither holds nothing, a ramp of greys is
    # GreyDitherer's, and the pattern's cap and the sparse rule are the
    # clip's own colours')
    "vga4_hold": lambda c: _FOUR16_ONE(c) or _knoll_hold(c),
    "vga4_hold_score": lambda c: _FOUR16_ONE(c) or _knoll_hold(c),
    # (the holds left their formats' groups for Trade-offs, and took
    # each format's rule with them)
    "stable": _pixel_kept,
    "text_stable": _for(("text",), PF_WORDS["text"], "This is"),
    "vga4_stable": _FOUR16_ONE,
    "c512_stable": _for(("c512",), PF_WORDS["c512"], "This is"),
    "vga8_stable": _for(("vga8",), PF_WORDS["vga8"], "This is"),
    "vga4_plan_cap": lambda c: _knoll_own(c),
    "vga4_sparse": lambda c: _knoll_own(c),
    "vga4_order": lambda c: None if c["pixfmt"] == "vga4" and
    c["palette"] in ("colour16", "clip16") and not (c["live"] or
                                                   c["resident"]) else
    "Only a 16-colour file's own sixteen (Palette colour16 or clip16) "
    "take an order.",
    "palette": lambda c: None if c["pixfmt"] != "vga4" or not (
        c["live"] or c["resident"]) else "A 16-colour file's own palette "
    "plays full screen, and a Live or resident file plays on the desktop, "
    "in the desktop's sixteen.",
    "screen": lambda c: ("A screen of its own is played full screen, and a "
                         "Live or resident file plays on the desktop."
                         if c["live"] or c["resident"] else None)
    if c["pixfmt"] == "vga4" else
    # (MODE X: 320x200 is 13h's own 400 lines on the unchained card, four
    # pages where 320 x 240 has three, 98.1.3.1.1 - the encoder refuses any
    # other screen there and says so)
    None if c["layout"] == "modex" else
    "A screen of its own is 16 colours' (Pixfmt vga4), or 320x200 for "
    "Mode X: 13h is a screen already.",
    "comp_stable": lambda c: _for(("cgacomp",), PF_WORDS["cgacomp"],
                                  "This is")(c) or (
        None if c["comp_dither"] == "diffuse" else
        "This is the diffusion's, and Comp dither is pattern."),
    "comp_quick": lambda c: None if c["comp_dither"] == "diffuse" else
    "This is the diffusion's, and Comp dither is pattern.",
    "mix": lambda c: None if c["comp_dither"] == "pattern" else
    "This is the pattern dither's, and Comp dither is diffuse.",
    "levels_mix": lambda c: None if c["comp_dither"] == "pattern" else
    "This is the pattern dither's, and Comp dither is diffuse.",
    "xms": lambda c: _nospk(c) if c["live"] else
    "This makes a LIVE file streamed from XMS, and Live is not chosen.",
    "rate": lambda c: None if c["audio"] != "none" else
    "The file has no sound.",
    "volume": lambda c: None if c["audio"] != "none" else
    "The file has no sound.",
    "ahead_frames": lambda c: None if c["ahead"] != "off" else
    "Buffer sound ahead is off.",
}
# THE DEGRADE LADDER (its own tab, Degrade to Fit): the options
# a ladder sets itself, greyed while one is on - and the rungs this file
# can use, each (label, --ladder's token)
LADDER_WHY = ("The degrade ladder is on, and replaces this: only what its "
              "rungs name is done, so with no rung for it, it is off "
              "(Degrade to Fit).")
LADDER_RUNGS = 8


def _ladder_wrap(d):
    was = FIELD_WHEN.get(d)
    FIELD_WHEN[d] = lambda c: LADDER_WHY if c.get("ladder") else (
        was(c) if was else None)


for _d in LADDER_SETS:
    _ladder_wrap(_d)


def ladder_options(c):
    """The rungs the form's file can use, in the order the list shows them:
    (label, token) - lag a card's sound's and not Live's, a slower rate not
    Live's or a layer's, a cut any file's, cells a planar screen's (Mode
    X's one, VGA4's three) and fewer colours a palette a scene's"""
    out = []
    if c["audio"] not in ("none", "speaker") and not c.get("live"):
        out += [("Lag up to %d ms" % ms, "lag:%d" % ms)
                for ms in (50, 100, 150, 200, 300)]
    if not c.get("live") and not c.get("layer_disk") and \
            not c.get("layer_profile"):
        try:
            fps = float(c.get("fps") or 0)
        except ValueError:
            fps = 0.0
        for a, b in ((5, 6), (2, 3), (1, 2), (1, 3), (1, 4)):
            out.append(("Slow to %d/%d of the rate%s" % (
                a, b, " (%.3g fps)" % (fps * a / b) if fps else ""),
                "slow:%d/%d" % (a, b)))
    # (a CUT: the climb stops where no frame is more broken than this, and
    # the frames are cut - any file's, a picture to judge it by being all)
    out += [("Cut, no frame over %d%% broken" % x, "cut:%d" % x)
            for x in (1, 2, 5, 10, 20)]
    if _coarse(c) is None:              # (Mode X's one cell, VGA4's three)
        cells = [cell for cell in V.DEGRADE_COARSE
                 if (cell in ("frame", "4x1")) == (c["layout"] == "modex") or
                 cell in ("frame", "mix")]
        out += [("Coarse cells at a scene change (%s)" % CELL_WORDS.get(
            cell, cell), "coarsescene:%s" % cell) for cell in cells]
        # (...and at ANY cut: a moving picture in cells for a frame)
        out += [("Coarse cells at any cut (%s)" % CELL_WORDS.get(cell, cell),
                 "coarseany:%s" % cell) for cell in cells]
    if _scenes(c) is None:
        out += [("Fewer colours: %d" % n, "colours:%d" % n) for n in (12, 8)]
    return out


# WHAT A LIST OFFERS, BY WHAT THE FILE IS (the owner, 2026-10-17: "change
# the options in the dropdown based on the layout, to only what it actually
# supports... and not force the user to guess it"): dest -> the choices the
# file the form makes can take, in the list's own words (a blank as its
# word), or None to offer them all. Each is the encoder's own refusal read
# the other way, so a choice the list leaves out is one the encode refuses
PIX_BY_LAYOUT = {"cga": ("mono", "cgacomp", "cga4"), "herc": ("mono",),
                 "lin80": ("mono", "vga4"), "lin320": ("vga8",),
                 "modex": ("vga8",), "c160": ("c160",),
                 "text-80x100": ("c512",), "text-80x25": ("text",)}
VGA4_SCREENS = ("640x480", "320x200", "320x240", "640x350", "640x400",
                "320x200n", "320x240n")


def pages_most(c):
    """The pages the form's screen holds (os88venc.page_count's rule), or
    None where the file has no second page"""
    lay = c["layout"]
    if lay == "herc":
        return 2
    if lay == "modex":
        scr = vid.SCR_320X200 if c.get("screen") == "320x200" else 0
    elif c["pixfmt"] == "vga4" and c.get("screen") not in ("", "640x480"):
        scr = vid.SCREEN_BY_NAME[c["screen"]]
    else:
        return None
    L = vid.LAYOUT_BY_NAME[lay or "lin80"]
    return min(vid.MPAGE_MAX, 0x10000 // vid.page_bytes(L, scr))


def _list_screen(c):
    if c["layout"] == "modex":
        return ["320x240", "320x200"]
    if c["pixfmt"] == "vga4":
        return ["640x480"] if c["live"] or c["resident"] else \
            list(VGA4_SCREENS)
    return None


def _list_palette(c):
    if c["pixfmt"] == "vga8":
        return None
    if c["pixfmt"] == "vga4":
        if c["live"] or c["resident"]:
            return ["auto"]
        return [p for p in V.PALETTES if V.palette_kind(p)[1] <= 16]
    return None


def _list_pages(c):
    most = pages_most(c)
    return None if most is None else \
        [str(n) for n in range(2, most + 1)] + ["max"]


def _list_coarse(c):
    if c["layout"] == "modex":
        return ["auto", "off", "4x1"]
    if c["pixfmt"] == "vga4":
        return ["auto", "off", "16x4", "16x8", "32x8", "64x8"]
    return None


FIELD_CHOICES = {
    "pixfmt": lambda c: ["auto"] + list(PIX_BY_LAYOUT[c["layout"]])
    if c["layout"] in PIX_BY_LAYOUT else None,
    "screen": _list_screen,
    "palette": _list_palette,
    "pages": _list_pages,
    "coarse_cut": _list_coarse,
    # (a Live or resident file's record is a stream's 32 KB, 98.1.4.1)
    "frame_cap": lambda c: ["auto", "32"] if c["live"] or c["resident"]
    else None,
    # (a layer is for a machine with a budget, never the lossless one)
    "layer_profile": lambda c: [p for p in ["none"] + sorted(V.PROFILES)
                                if p != "lossless"],
}


# a coarse rung's cell, in words (frame: the whole frame at a time)
CELL_WORDS = {"frame": "whole frame", "auto": "whole frame",
              "mix": "mixed: only the tiles it must"}


def ladder_label(tok):
    """A --ladder token's words, for one the form's file cannot use too"""
    kind, _, v = tok.partition(":")
    if kind.startswith("coarse"):
        v = CELL_WORDS.get(v, v)
    return {"lag": "Lag up to %s ms", "slow": "Slow to %s of the rate",
            "coarsescene": "Coarse cells at a scene change (%s)",
            "coarse": "Coarse cells at a scene change (%s)",
            "coarseany": "Coarse cells at any cut (%s)",
            # ("mix": only the tiles the budget cannot draw, VIDEO-SEEN 3a)
            "colours": "Fewer colours: %s",
            "cut": "Cut, no frame over %s%% broken"}.get(kind, "%s") % v


assert len({g[1] for g in GROUPS}) == len(GROUPS), "a header names a group"
GROUP_OF = {d: g for g in GROUPS for d in g[2]}
# the groups on Degrade to Fit, laid out by ladder_page
LADDER_PAGE = ("Degrade ladder", "Or, without the ladder: for the whole clip",
               "Held frames, either way")
TAB_OF = {d: g[0] for d, g in GROUP_OF.items()}
# the fields a group's rule reads: a change to one re-judges every group
CONTEXT_FIELDS = ("preset", "pixfmt", "profile", "live", "layout", "audio",
                  "text_colour", "comp_dither", "spk_shape", "aim",
                  "resident", "buffer_sound_ahead", "screen", "palette",
                  "scene_palettes", "flip", "loop_from", "repeat",
                  "layer_disk", "layer_profile", "page_ahead", "step_down",
                  "ladder", "fps", "coarse_cut")
# the choices that IMPLY others (os88venc.implied): changing one refills them
IMPLYING = ("preset", "pixfmt", "profile", "live")
# a free-text option's COMMON values, offered in an editable list: the sound
# rate's 5,512 Hz halves the sound's bytes, which is half a Live clip's
# memory (98.1.7.2) - any other rate can still be typed. Owed time's 0 is
# OFF, the fixed per-frame ceiling (98.2.1.1)
SUGGEST = {"rate": ["", "22050", "11025", "8000", "5512"],
           # AUTO and the profile's own share: avg_list() fills the second
           # in as the profile changes
           "avg": ["auto"],
           "owe": ["", "0", "1.6"],
           # THE LAG (98.2.1.1.2, 98.2.1.1.3): the default range, a
           # tighter and a looser one, and one number fixes it
           "lag": ["50-100", "50-150", "50-200", "100-300", "100"],
           # FREE MEMORY TO PLAY (98.2.1.3.1): rings of 8, 10 and 12 slots
           "memory": ["", "309", "373", "437"],
           # THE XMS BANK (98.3.18.2): 128 KB under a pool of 1, 3, 7 and
           # 15 MB - the player takes the WHOLE pool (98.3.18.2.1), and a
           # 4 MB board is ~3 MB of XMS once 640 KB of base memory, the
           # HMA and a shadowed BIOS are off it, so 1 and 3 MB are a 286's
           # common pools; the prefill in seconds of the disk, or the whole
           # bank
           "bank": ["", "auto", "896", "2944", "7040", "15232"],
           # ...the layer's the same - AUTO the pool its machine has left
           # once it keeps the base's bank it needs (98.2.1.3.3) - and the
           # pools those are 128 KB under
           "layer_bank": ["", "auto", "896", "2944", "7040", "15232"],
           "pool": ["896", "2944", "7040", "15232"],
           "layer_pool": ["", "896", "2944", "7040", "15232"],
           "prefill": ["", "5", "10", "30", "all"],
           # A COARSE CUT (98.2.5): AUTO, the default, OFF - the cut drawn
           # as it comes, as before the cells - and the cells AUTO picks
           # from, plus 16x8; any other WxH is typed. It was a bare text
           # box, and EMPTYING it meant "the default", which is AUTO: a cut
           # could not be turned off by clearing it, only by typing the word
           "coarse_cut": ["auto", "off", "4x1", "16x4", "16x8", "32x8",
                          "64x8"],
           # ...and the other boxes that took a WORD typed blind (the
           # boxes audit, 2026-10-14): each word is its BLANK_WORD below
           "jobs": ["", "1", "2", "4", "8"],
           "end": [""],
           "poster": [""],
           "loop_from": ["", "0"],
           "volume": ["", "0.5", "1.5", "2", "3dB", "6dB"],
           "clip": ["", "0", "16"],
           "desktop_clip": ["0", "16", "32"],
           "text_busy": ["", "12", "2"],
           "layer_disk": [""],
           "layer_prefill": ["", "16", "32", "64", "all"],
           "vga4_sparse": ["", "0", "1", "2", "3"],
           "worth": ["", "0.5", "2", "4"]}
# WHAT A BLANK MEANS, SAID (the boxes audit, 2026-10-14): an option whose
# default is "none", "all", "the nearest" or "what the rest imply" showed
# an EMPTY box, which reads as unset rather than as a choice already made.
# The window shows this word instead - in the field and in its dropdown -
# and reads the word back as the blank it stands for, so the command line,
# the groups' rules and every other reader of the form are unchanged
# (WordVar; typing the word is the same as leaving the field alone)
BLANK_WORD = {"preset": "none", "layout": "auto", "pixfmt": "auto",
              "audio": "auto", "live": "off", "layer_profile": "none",
              "text_colour": "auto", "frame_cap": "auto",
              "screen": "640x480",
              "cga_palette": "nearest", "cga_bright": "nearest",
              "cga_bg": "nearest",
              "jobs": "all", "end": "end", "poster": "auto",
              "loop_from": "off", "volume": "unchanged", "clip": "auto",
              "text_busy": "auto", "bank": "none", "layer_bank": "none",
              "layer_pool": "as Pool", "layer_disk": "none",
              "layer_prefill": "none", "prefill": "%g" % V.BANK_PREFILL,
              "vga4_sparse": "auto", "worth": "%g" % V.AIM_WORTH}


def blank_word(dest):
    """What field DEST shows for a blank, or None: it shows the blank"""
    return BLANK_WORD.get(dest)


def from_shown(dest, raw):
    """What the field's text means to the form: its BLANK_WORD is the
    blank it stands for, and anything else is itself"""
    w = blank_word(dest)
    if w is not None and str(raw).strip().lower() == w.lower():
        return ""
    return raw


def to_shown(dest, value):
    """...and back: a blank is shown as the field's word, when it has one"""
    w = blank_word(dest)
    return w if w is not None and not str(value).strip() else value


def dropdown(f):
    """A field's dropdown, as the window offers it: a blank is its word,
    and an option with a real default offers no blank at all - picking the
    default says the same thing, and an empty entry only emptied the box"""
    out = []
    for c in f["choices"] or []:
        if c == "":
            c = blank_word(f["dest"])
            if c is None and f["default"]:
                continue
        if c is not None and c not in out:
            out.append(c)
    return out


def blank_fill(dest, values, sfps=None):
    """What a field EMPTIED by hand is put back to as the pointer leaves
    it: what the encode will use for the blank - the preset's, format's
    and profile's implication, the speaker style's number, or the parser's
    default - so a cleared box never stands empty for a value it has"""
    imp = implied_values(values, sfps)
    imp.update(style_values(values.get("spk_style")))
    v = imp.get(dest)
    if v not in (None, ""):
        return str(v)
    return [f["default"] for f in fields() if f["dest"] == dest][0]
# a free-text option that NAMES A FILE the encode writes: a Browse... beside
# it, a Save dialog of that type, started beside the .V88 under its name
SAVE_FILE = {"spk_preview": ("The speaker preview", ".wav",
                             [("WAV sound", "*.wav")])}
# ...under the right-hand column rather than across the tab, where the
# left-hand one already reaches the window's bottom
NOTE_IN_COLUMN = ("Trade-offs",)
# a line under a tab's fields: what no one field says
TAB_NOTES = {
    "Trade-offs": "Each trades picture for bytes or the player's "
                       "time; greyed where the file cannot use it.",
    "Budget": "XT-IDE and other disk controllers the CPU copies for "
              "cost CPU the picture would have had: every byte read is "
              "the 8088's work, where an ST11M or ST11R's DMA is not. The "
              "profile says which (Basic): 5150-st225 is DMA, 5150-xtide "
              "charges the copy - its disk slows as the decode and the "
              "speaker take the machine, so fewer bytes a frame are "
              "planned."}
# the speaker style's three numbers (os88vid.SPK_STYLES): the parser's
# default is None, "the style's", so the window shows the style's own
STYLE_FIELDS = (("spk_highpass", "hp"), ("spk_ratio", "ratio"),
                ("spk_range", "rng"))
# what the window runs itself, and so does not offer
HIDDEN = {"help", "src", "out", "preview_png", "quiet", "profiles",
          "progress", "posters_only"}   # (Redo posters is a button)


def fields():
    """Every option the encoder takes, as the window shows it: a dict per
    option - dest, flag, label, kind (choice / bool / text), default,
    choices, tooltip, tab. Built from os88venc.parser() every time, so the
    window can never be missing one"""
    out = []
    for act in V.parser()._actions:
        if act.dest in HIDDEN or not act.option_strings:
            continue
        flag = act.option_strings[-1]
        kind = "bool" if act.nargs == 0 else \
            "choice" if act.choices is not None else "text"
        dflt = act.default
        out.append(dict(
            dest=act.dest, flag=flag,
            label=flag.lstrip("-").replace("-", " ").capitalize(),
            kind=kind,
            default="" if dflt is None or kind == "bool" else str(dflt),
            choices=[""] + [str(c) for c in act.choices]
            if kind == "choice" else SUGGEST.get(act.dest),
            tip=(act.help or "").strip(),
            help=V.CHOICE_HELP.get(act.dest) if kind == "choice" else None,
            tab=TAB_OF.get(act.dest, "Basic")))
    return out


def style_values(style=None):
    """What a speaker STYLE sets (98.2.15.1): its high-pass, ratio and
    range as the window shows them - dest -> string"""
    st = vid.SPK_STYLES.get(str(style or "").strip() or vid.SPK_STYLE,
                            vid.SPK_STYLES[vid.SPK_STYLE])
    return {d: "%g" % st[k] for d, k in STYLE_FIELDS}


def form_start():
    """The form as the window opens it: every field's default, and the
    speaker style's numbers filled in - dest -> string"""
    vals = {f["dest"]: f["default"] for f in fields()}
    vals.update(style_values(vals.get("spk_style")))
    return vals


def avg_choices(share):
    """--avg's list for a profile whose old fixed share was SHARE ("" for
    one with no budget): AUTO first, the default, then that share"""
    return ["auto"] + ([share] if share else [])


def layer_disk_of(lprof):
    """--layer-disk as a chosen --layer-profile fills it: that machine's own
    disk, the bytes a second the encode budgets the layer for when the field
    is left empty (98.1.9) - so the field shows the number that will be used
    and can be changed from it. "" for no profile, or one with no disk"""
    lprof = (lprof or "").strip()
    if not lprof:
        return ""
    d = V.PROFILES.get(V.profile_name(lprof), {}).get("disk")
    if d is None:
        return ""
    return "%d" % d if float(d).is_integer() else "%g" % d


def choice_lines(f, current=""):
    """(value, what it is, is it the one chosen) for a choice field, in
    the field's own order: what its "?" shows (os88venc.CHOICE_HELP)"""
    return [(c, f["help"].get(c, ""), c == current)
            for c in f["choices"] if c]


def implied_values(values, sfps=None):
    """What the form's preset, format, profile and Live choice imply
    (os88venc.implied, SPEC.md 98.2.10) - every option they set, as the
    encoder will use it"""
    g = lambda k: str(values.get(k, "")).strip() or None
    return V.implied(g("preset"), g("pixfmt"), g("profile") or "5150-st225",
                     g("live"), sfps)


def argv_from(src, out, values, sfps=None):
    """The form's VALUES (dest -> string, "1"/"" for a switch) -> the
    encoder's command line. A value left at the parser's default is left
    off, and so is one that still equals what the preset, format and
    profile imply - the window SHOWS those (98.2.10), and the command line
    stays the short one that says the same thing"""
    argv = [src, out]
    values = dict(values, _out=out)     # (a .WAV greys the picture's groups)
    imp = implied_values(values, sfps)
    # ...but a choice that IMPLIES is compared with what the rest imply
    # without it - or a --pixfmt cgacomp would echo back as its own
    # implication and be left off, and the file come out one-bit
    imp["pixfmt"] = implied_values(dict(values, pixfmt=""), sfps)["pixfmt"]
    # ...and the speaker style's numbers, which the window shows filled
    imp.update(style_values(values.get("spk_style")))
    # an option that CANNOT APPLY to the file is left off: its group is
    # greyed (98.2.8.2), and a --detail or a --flip left over from another
    # format would only be refused
    off = group_state(values)[1]
    wav = out.lower().endswith(".wav")
    for f in fields():
        if off.get(f["dest"]) or (wav and f["dest"] == "audio"):
            continue                    # (a WAV's sound IS the speaker's)
        v = str(values.get(f["dest"], "")).strip()
        if f["kind"] == "bool":
            if v in ("1", "True", "true") and imp.get(f["dest"]) != "1":
                argv.append(f["flag"])
            continue
        if v == "" or v == f["default"] or v == imp.get(f["dest"]):
            continue
        argv += [f["flag"], v]
    return argv


def form_context(values):
    """What the form MAKES, as the groups' rules ask it (98.2.8.2): the
    pixel format and layout the preset, format, layout and Live choice come
    to, and the sound and the few choices a group depends on"""
    g = lambda k: str(values.get(k, "") or "").strip()
    try:
        imp = implied_values(values)
    except Exception:
        imp = {}
    lay = g("layout") or imp.get("layout", "")
    pf = g("pixfmt")
    if g("live"):
        pf = "vga4" if g("live") == "vga" and pf == "vga4" else "mono"
    elif not pf and g("layout"):        # a layout typed by hand IS a format
        pf = "vga8" if lay in ("lin320", "modex") else \
            {"c160": "c160", "text-80x100": "c512",
             "text-80x25": "text"}.get(lay, "mono")
    pf = pf or imp.get("pixfmt") or "mono"
    wav = g("_out").lower().endswith(".wav")
    return dict(pixfmt=pf if pf in PF_WORDS else "mono", layout=lay,
                wav=wav, audio="speaker" if wav else
                g("audio") or imp.get("audio") or "pcm8",
                text_colour=g("text_colour") or imp.get("text_colour") or
                "colour",
                comp_dither=g("comp_dither") or "diffuse",
                spk_shape=g("spk_shape") or V.SPK_ENC, aim=g("aim") or "asked",
                live=g("live"), resident=g("resident") not in ("", "0",
                                                              "False"),
                ahead=g("buffer_sound_ahead") or "auto",
                screen=g("screen"), profile=g("profile"),
                palette=g("palette") or "auto",
                scene_palettes=g("scene_palettes") not in ("", "0", "False"),
                # (what the narrower rules - flip's, page-ahead's, the
                # layer's - read: never here, so they never greyed a thing)
                flip=g("flip") not in ("", "0", "False"),
                repeat=g("repeat") not in ("", "0", "False"),
                loop_from=g("loop_from"), layer_disk=g("layer_disk"),
                layer_profile=g("layer_profile"),
                page_ahead=g("page_ahead") or "auto",
                step_down=g("step_down") or "on",
                ladder=g("ladder"), fps=g("fps") or imp.get("fps", ""),
                coarse_cut=g("coarse_cut") or "auto")


def group_state(values):
    """({group header: why it cannot apply, or None}, {option: why it
    cannot apply, or None}) for the form's values - an option's is its
    group's, else its own (FIELD_WHEN). No Tk: the gate reads it"""
    c = form_context(values)
    groups = {g[1]: g[3](c) for g in GROUPS}
    if c["wav"]:                        # the sound alone (86.21.1)
        for g in GROUPS:
            if g[1] not in WAV_KEEP:
                groups[g[1]] = WAV_WHY
    opts = {}
    for f in fields():
        g = GROUP_OF.get(f["dest"])
        why = groups.get(g[1]) if g else None
        if why is None and f["dest"] in FIELD_WHEN:
            why = FIELD_WHEN[f["dest"]](c)
        opts[f["dest"]] = why
    return groups, opts


def target_values(i):
    """The values a TARGET sets - every one of them, so choosing another
    target takes back what the last one set"""
    label, preset, pixfmt, profile, extra = TARGETS[i]
    vals = {"preset": preset or "", "pixfmt": pixfmt or "",
            "profile": profile, "live": ""}
    vals.update(extra)
    return vals


# the layouts each pixel format is drawn on (os88venc refuses the rest)
PIXFMT_LAYOUTS = {"vga8": ("lin320", "modex"), "vga4": ("lin80",),
                  "cga4": ("cga",), "c160": ("c160",), "c512": ("text-80x100",),
                  "text": ("text-80x25",),
                  "cgacomp": ("cga",),
                  "mono": ("cga", "herc", "lin80")}


def form_value(f, v, imp=None):
    """A stored option's value (98.2.17) as the form's field holds it: the
    parser's own spelling of its default, or the implied table's, where it
    is that number - so a loaded form leaves the same short command line
    a form filled by hand would"""
    if v is None:
        return ""
    if isinstance(v, bool) or f["kind"] == "bool":
        return "1" if v else ""
    for ref in (f["default"], (imp or {}).get(f["dest"], "")):
        if ref == "":
            continue
        try:
            if isinstance(v, (int, float)) and float(ref) == float(v):
                return ref
        except ValueError:
            pass
        if str(v) == ref:
            return ref
    if isinstance(v, float):
        return "%d" % v if v.is_integer() else repr(v)
    return str(v)


def target_for(values):
    """The "made for" target a form is, or None: the one whose preset,
    profile and Live choice it has, making the same pixel format"""
    g = lambda k: str(values.get(k, "") or "").strip()
    pf = form_context(values)["pixfmt"]
    for i in range(len(TARGETS)):
        tv, tf = target_values(i), target_fill(i)
        if (g("preset"), g("profile"), g("live")) == (
                tv["preset"], tv["profile"], tv["live"]) and \
                form_context(tf)["pixfmt"] == pf:
            return i
    return None


def form_from_file(r):
    """A .V88 made with its options stored (98.2.17) -> (the form's
    values, the target it was made for or None, notes, the source's
    name); None for a file made before they were stored. The TITLE and
    CREDITS are the header's, which can be changed in place after the
    encode (98.2.11) and are what a new encode should keep. No Tk"""
    doc = r.options()
    if doc is None:
        return None
    o, notes = V.opts_migrate(doc)
    vals = form_start()
    g = lambda k: o.get(k) if o.get(k) is not None else None
    imp = implied_values({k: "" if g(k) is None else str(g(k))
                          for k in ("preset", "pixfmt", "profile", "live")})
    for f in fields():
        if f["dest"] in o:
            vals[f["dest"]] = form_value(f, o[f["dest"]], imp)
    # ...and an option given as AUTO asks for AUTO again (98.2.17.1), not
    # for the number it came to. A record from before this holds no list
    # and loads its numbers; a list off a file is hostile, so only the
    # names this encoder knows take it
    au = doc.get("auto")
    for d in au if isinstance(au, list) else ():
        if isinstance(d, str) and d in V.AUTO_OPTS and d in vals:
            vals[d] = "auto"
    vals["title"], vals["credits"] = r.title, r.credits
    return vals, target_for(vals), notes, str(doc.get("src") or "")


def fitting_pixfmt(values):
    """The form's pixel format, if it fits the canvas the rest of the form
    implies - else "", so the preset's own is taken: a preset changed from
    modex to cga must not keep modex's vga8, which the encoder refuses"""
    pf = str(values.get("pixfmt", "")).strip()
    lay = implied_values(dict(values, pixfmt=""))["layout"]
    return pf if pf and lay in PIXFMT_LAYOUTS.get(pf, ()) else ""


def target_fill(i, sfps=None):
    """A TARGET as the window shows it: what it sets, and every option that
    implies (98.2.10) - so a preset is seen for what it is"""
    vals = target_values(i)
    vals.update(implied_values(vals, sfps))
    return vals


def keep_title(cur, auto_was):
    """Whether a source chosen keeps the Title field as it is: yes, unless
    it is empty or still the file name the window itself filled in from the
    source before (AUTO_WAS) - a .V88's title, or one typed, is kept"""
    cur = str(cur or "").strip()
    return bool(cur) and cur != str(auto_was or "").strip()


def suggest(src):
    """(a line describing the source, the values its facts imply): its
    frame rate and length, and no sound when it has none"""
    sw, sh, dar, sfps, dur, has_audio = V.probe(src)
    text = ("%d x %d, %.2f fps, %.1f s, %s, %.3f:1"
            % (sw, sh, sfps, dur, "with sound" if has_audio else "SILENT",
               dar))
    vals = {"title": os.path.splitext(os.path.basename(src))[0][:47]}
    if not has_audio:
        vals["audio"] = "none"
    return text, vals


# --------------------------------------------------------------------------
# the preview: a file's frames as the adapter shows them
# --------------------------------------------------------------------------
def _rgb16():
    import numpy as np
    return np.frombuffer(vid.STD16, np.uint8).astype(np.uint16).reshape(
        16, 3) * 255 // 63


def render(r, surf, f=None):
    """Frame `surf` of Reader `r` as an RGB PIL image, at the proportions
    the screen shows it (the file's pixel aspect, 98.1.1). `f`, the frame
    it is the screen after: a file with a palette a scene (98.1.3.2.3) is
    shown in that scene's sixteen, as the player shows it"""
    import numpy as np
    from PIL import Image
    g = r.g
    pf = r.pixfmt
    if pf in (vid.PF_MONO1, vid.PF_CGACOMP, vid.PF_CGA4, vid.PF_C160):
        cv = np.frombuffer(g.canvas(surf), np.uint8).reshape(g.h, g.wb)
        bits = np.unpackbits(cv, axis=1)
        if pf == vid.PF_MONO1:
            rgb = np.repeat((bits * 255).astype(np.uint8)[..., None], 3, 2)
        elif pf == vid.PF_CGACOMP:
            import os88cgacomp
            rgb = os88cgacomp.render(bits)
        else:
            n = 2 if pf == vid.PF_CGA4 else 4
            idx = bits.reshape(g.h, -1, n) @ (1 << np.arange(n - 1, -1, -1))
            if pf == vid.PF_CGA4:
                idx = np.array(vid.cga4_colours(r.cgapal))[idx]
            rgb = _rgb16()[idx].astype(np.uint8)
    elif pf == vid.PF_TEXT:             # the text screen (98.1.3.6), in
        import os88txtfont                # the model's face, 8 x 8 a cell
        rgb = os88txtfont.render(g.canvas(surf), g.wb, g.h, _rgb16())
    elif pf == vid.PF_C512:             # the text hack on a composite
        import os88cgacomp                # monitor (98.1.3.5): 8 dots a cell
        rgb = os88cgacomp.render_c512(g.canvas(surf), g.wb, g.h,
                                      new=r.cgapal != vid.CARD_OLD)
        an, ad = r.aspect               # ...averaged back to a cell's width
        img = Image.fromarray(np.ascontiguousarray(rgb))
        return img.resize((max(1, round(g.wb // 2 * an / ad)), g.h),
                          Image.BOX)
    else:                               # a pixel a byte: VGA8, VGA4, Mode X
        cv = np.frombuffer(g.canvas(surf), np.uint8).reshape(g.h, g.w)
        p = vid.palette_at(r, f) if f is not None and r.pev else r.palette
        pal = _rgb16() if pf == vid.PF_VGA4 and not p else \
            np.frombuffer(p, np.uint8).astype(np.uint16).reshape(
                -1, 3) * 255 // 63
        rgb = pal[cv].astype(np.uint8)
    if r.rowscale > 1:
        rgb = np.repeat(rgb, r.rowscale, axis=0)
    img = Image.fromarray(np.ascontiguousarray(rgb))
    an, ad = r.aspect
    w, h = img.size
    return img.resize((max(1, round(w * an / ad)), h), Image.NEAREST)


# --------------------------------------------------------------------------
# the palette: what colours the Colour tab's choices give, as swatches
# --------------------------------------------------------------------------
# the sixteen by name, the cga_bg "?" list's own words
C16_NAMES = [V.CHOICE_HELP["cga_bg"][str(i)] for i in range(16)]
# the fields whose change redraws the panel: the ones that pick a palette,
# and the ones that pick which KIND of palette there is
PAL_FIELDS = ("preset", "pixfmt", "profile", "live", "palette",
              "screen", "cga_palette", "cga_bright", "cga_bg", "text_colour",
              "cga_card")


def _hex(rgb):
    return "#%02x%02x%02x" % tuple(int(round(float(c))) for c in rgb)


def _c16(idxs):
    """[(#rrggbb, what it is)] for indexes of the sixteen"""
    std = [tuple(vid.STD16[i * 3:i * 3 + 3]) for i in range(16)]
    return [(_hex(c * 255 // 63 for c in std[i]),
             "%d: %s" % (i, C16_NAMES[i])) for i in idxs]


def _int_or_none(v):
    v = str(v if v is not None else "").strip()
    try:
        return int(v)
    except ValueError:
        return None


def cga4_sel(pal, bright, bg):
    """CGA4's palette byte (98.1.3.3) for a set, an intensity and a
    background - the byte os88venc.cga4_pick returns for those overrides"""
    return (bg & 15) | (16 if bright else 0) | \
        (0x40 if pal == 2 else 0x20 if pal == 1 else 0)


def cga4_row(sel):
    """(a line naming palette byte `sel`, its four swatches)"""
    p = 2 if sel & 0x40 else (sel >> 5) & 1
    return ("set %d %s on %s" % (p, "bright" if sel & 16 else "dim",
                                 C16_NAMES[sel & 15].lower()),
            _c16(vid.cga4_colours(sel)))


_PALCACHE = {}


def _cached(key, make):
    if key not in _PALCACHE:
        _PALCACHE[key] = make()
    return _PALCACHE[key]


def _comp16():
    import os88cgacomp
    return [(_hex(c), "nibble %X" % n)
            for n, c in enumerate(os88cgacomp.palette())]


def _c512(new):
    """The 512 codes' flat colours, darkest first (what order they are in
    means nothing to the eye), each named by its code"""
    import os88cgacomp
    rgb = os88cgacomp.c512_palette(new=new)
    order = sorted(range(512), key=lambda k: float(
        rgb[k] @ (0.299, 0.587, 0.114)))
    return [(_hex(rgb[k]), "code %d" % k) for k in order]


def palette_view(values):
    """WHAT THE COLOUR TAB'S CHOICES GIVE, as swatches: [(heading, [(line,
    [(#rrggbb, what it is) or None for chosen-from-the-clip])], note)] -
    for the pixel format the form makes, the colours it can have. No Tk:
    the gate reads it as data"""
    vals = dict(values)
    try:
        vals["pixfmt"] = str(vals.get("pixfmt", "")).strip() or \
            implied_values(vals).get("pixfmt") or "mono"
        imp = implied_values(vals)
    except Exception:
        imp = {}
    pf = vals["pixfmt"]
    g = lambda k: str(vals.get(k, "") or imp.get(k, "") or "").strip()
    out = []
    if pf == "cga4":
        pal, bright = _int_or_none(g("cga_palette")), \
            _int_or_none(g("cga_bright"))
        bg = _int_or_none(g("cga_bg"))
        rows = []
        for b in ((bright,) if bright in (0, 1) else (0, 1)):
            for p in ((pal,) if pal in (0, 1, 2) else (0, 1, 2)):
                sel = cga4_sel(p, b, bg or 0)
                line, sw = cga4_row(sel)
                if pal is None or bright is None:
                    line = line.rsplit(" on ", 1)[0]    # (the swatch says)
                sw = [None if bg is None or not 0 <= bg < 16 else
                      (sw[0][0], "the background, " + sw[0][1])] + sw[1:]
                rows.append((line, sw))
        auto = [n for n, v in (("the set", pal), ("its intensity", bright),
                               ("the background", bg)) if v is None]
        auto = [", ".join(auto[:-1]), auto[-1]] if len(auto) > 2 else auto
        note = ("%s: chosen from the clip when it is encoded, nearest "
                "its colours - one of these. The first swatch is the "
                "background." % " and ".join(auto).capitalize()
                if auto else "All three fixed: this is the file's palette, "
                "the background first.")
        out.append(("CGA, 4 colours (mode 4)", rows, note))
    elif pf == "vga4" and V.palette_kind(g("palette"))[0] != "auto":
        pk, pn = V.palette_kind(g("palette"))
        codes = vid.plane_code(min(pn, 16))
        if pk == "grey":
            lv = V.grey_levels(min(pn, 16))
            sw = [(_hex(v * 255 // 63 for _ in range(3)), "value %X: %d of 63"
                   % (codes[i], v)) for i, v in enumerate(lv)]
        else:
            sw = []
        out.append(("%d %s, its own" % (min(pn, 16), "greys" if pk == "grey"
                                         else "colours"),
                    [("loaded into the VGA's DAC", sw)] if sw else [],
                    "A full-screen file's own palette, on any Screen - the "
                    "file then plays full screen, never in a window: each "
                    "colour's bits on a group of planes, "
                    "so %d colours store %d bit(s) a pixel.%s" % (
                        min(pn, 16), max(1, (min(pn, 16) - 1).bit_length()),
                        "" if sw else " Chosen from the clip when it is "
                        "encoded.")))
    elif pf in ("vga4", "c160"):
        out.append(("The sixteen", [("every pixel one of", _c16(range(16)))],
                    "Fixed: %s. No choice to make here." % (
                        "mode 12h's own, which no theme changes"
                        if pf == "vga4" else "CGA's, two a text cell")))
    elif pf == "text":
        if g("text_colour") == "mono":
            out.append(("Text, black and white", [
                ("07h, 0Fh, 70h", _c16((0, 7, 15)))],
                "Grey on black, white on black, black on grey: what an "
                "MDA and a Hercules draw as a colour card does."))
        else:
            out.append(("Text, 16 on 16", [("fore- and background",
                                            _c16(range(16)))],
                        "Any of the sixteen on any of the sixteen, blink "
                        "off: a CGA, an EGA or a VGA."))
    elif pf == "cgacomp":
        try:
            sw = _cached("comp", _comp16)
        except Exception:
            sw = []
        out.append(("Composite colour on a CGA", [("a nibble a colour", sw)],
                    "What each of the sixteen nibbles shows on a composite "
                    "monitor (the model the preview uses); patterns mix "
                    "them further."))
    elif pf == "c512":
        card = g("cga_card") or "both"
        rows = []
        for name, new in (("old", False), ("new", True)):
            if card in (name, "both"):
                try:
                    sw = _cached(("c512", new), lambda: _c512(new))
                except Exception:
                    sw = []
                rows.append(("the %s CGA" % name.upper(), sw))
        out.append(("512 codes on the composite output", rows,
                    "Every code's flat colour, darkest first; about 450 "
                    "differ. Hover for the code."))
    elif pf in ("vga8", "modex"):
        pk, pn = V.palette_kind(g("palette"))
        if pk == "grey":
            lv = V.grey_levels(pn)
            out.append(("%d greys" % pn, [("black to white", [
                (_hex(v * 255 // 63 for _ in range(3)), "%d: %d of 63"
                 % (i, v)) for i, v in enumerate(lv)])],
                "A ramp, fixed: each pixel one level, dithered between "
                "the two round it by the one-bit Dither and held by its "
                "Stable."))
        else:
            out.append(("%d colours" % (pn or 256), [], "Chosen from the "
                        "clip itself when it is encoded: its palette shows "
                        "under The file once it is made, or a .V88 is "
                        "opened."))
    else:
        out.append(("Black and white", [("one bit", _c16((0, 15)))],
                    "One bit a pixel: no palette to choose."))
    return out


def file_palette(r, f=None):
    """The palette a .V88 was ENCODED with, where it was chosen from its
    clip - CGA4's four, VGA8's 256, a VGA4 file's own 16 - as
    palette_view's rows: what "the nearest" turned out to be. A file with
    a palette a scene (98.1.3.2.3) shows the scene frame `f` is in, as the
    preview's picture is drawn in. [] for a file whose colours are fixed"""
    if r is None:
        return []
    if r.pixfmt == vid.PF_CGA4:
        line, sw = cga4_row(r.cgapal)
        return [(line, [(sw[0][0], "the background, " + sw[0][1])] +
                 sw[1:])]
    if r.pixfmt == vid.PF_VGA8 and r.palette:
        p = r.palette
        return [("its 256", [(_hex(c * 255 // 63 for c in p[i * 3:i * 3 + 3]),
                              "%d" % i) for i in range(len(p) // 3)])]
    if r.pixfmt == vid.PF_VGA4 and r.palette:   # a screen's own, 98.1.3.2.1
        p, scene = r.palette, ""    # ...the values its colours take: the
        if f is not None and r.pev:     # (the scene's, 98.1.3.2.3)
            p = vid.palette_at(r, f)
            k = sum(1 for fr, _ in r.pev if fr <= f)
            scene = "Palette: scene %d of %d, at frame %d" % (
                max(k, 1), len(r.pev), f + 1)
        lit = {i for i in range(16) if any(p[3 * i:3 * i + 3])} | {0}
        n = min(k for k in sorted(vid.PLANE_CODES)  # fewest PLANE_CODES
                if lit <= set(vid.PLANE_CODES[k]))  # that hold them
        return [("its %d" % n,
                 [(_hex(c * 255 // 63 for c in p[i * 3:i * 3 + 3]),
                   "value %X" % i) for i in vid.PLANE_CODES[n]], scene)]
    return []


def choice_swatches(dest, choice, values):
    """The colours one choice of a palette field gives, for its "?" list:
    the set at the intensity chosen (dim when blank), the intensity on the
    set chosen (1 when blank), the background by itself. [] for any other"""
    c = _int_or_none(choice)
    if c is None:
        return []
    pal = _int_or_none(values.get("cga_palette"))
    bright = _int_or_none(values.get("cga_bright"))
    if dest == "cga_bg" and 0 <= c < 16:
        return _c16((c,))
    if dest == "cga_palette" and c in (0, 1, 2):
        return cga4_row(cga4_sel(c, bright or 0, 0))[1][1:]
    if dest == "cga_bright" and c in (0, 1):
        return cga4_row(cga4_sel(pal if pal in (0, 1, 2) else 1, c,
                                 0))[1][1:]
    return []


def grid_of(n):
    """(columns, swatch width, height, gap) for a row of n swatches"""
    if n <= 16:
        return n, 22, 16, 2
    if n <= 256:                    # (four rows of 64: a 256-colour
        return 64, 6, 6, 0          # palette is 24 pixels tall)
    return 64, 4, 4, 0              # (512: eight rows of 64, 32 pixels)


def preview_frames(path, most=2000, tick=None):
    """(reader, [frame canvases' images]) - every frame the screen shows,
    or every n-th of a long file so the scrubber stays usable. A colour
    file's start at key 0 (os88vid.first_shown): the pre-roll before it is
    never on the screen, so it is not in the preview either - the owner's
    Spice & Wolf encode played whole and previewed half-painted.
    `tick(done, total)` as it goes"""
    r = vid.Reader(path)
    f0 = vid.first_shown(r)
    step = max(1, -(-(r.frames - f0) // most))
    out = []
    for f, surf, rec, at, i in vid.v88_frames(r):
        if tick:
            tick(f, r.frames)
        if f >= f0 and (f - f0) % step == 0:
            out.append((f, render(r, surf, f)))
    return r, out


# THE PROGRESS (98.2.11): what os88venc SAYS it is doing, as it does it -
# the pass it is in and why, the step, a count of what the step works
# through, and a note of what goes on beside it - and then the window's
# own steps, the floppy and the preview. The bar is the step's own count,
# and goes INDETERMINATE where a step has none to give (ffmpeg's palette,
# the sound): until 2026-10-17 one bar spread made-up shares of the whole
# over the steps, and a streamed encode, which reads, dithers and encodes
# at once, showed a "Reading" frame number nothing had read
STEP_TEXT = {"prepare": ("Getting ready", None),
             "palette": ("Choosing the clip's colours", "frame"),
             "levels": ("Measuring the picture's levels", None),
             "scenes": ("Finding the scenes", "frame"),
             "colours": ("Choosing each scene's colours", "scene"),
             "order": ("Ordering the colours by the planes they write",
                       "frame"),
             "cuts": ("Finding the scene changes, for the coarse cells",
                      "frame"),
             "stepdown": ("Trying fewer colours where scenes broke",
                          "scene"),
             "trials": ("Trying the quality choices", "trial"),
             "read": ("Reading and dithering", "frame"),
             "sound": ("Encoding the sound", None),
             "encode": ("Encoding", "frame"),
             "wait": ("Waiting", None),
             "write": ("Writing the file", None),
             "check": ("Checking the file", None),
             "disk": ("Making the disk", None),
             "preview": ("Loading the preview", "frame")}


def bar_of(step, done, total):
    """The bar for a step: its own count's share, 0.0 to 1.0 - or None,
    a step with no count, shown as working rather than as a share"""
    if not total or step == "pass":
        return None
    return min(done, total) / float(total)


def clock(secs):
    secs = int(secs)
    return "%d:%02d" % (secs // 60, secs % 60) if secs < 3600 else \
        "%d:%02d:%02d" % (secs // 3600, secs // 60 % 60, secs % 60)


def step_text(step, done, total, note="", secs=None):
    """A step as it is said under the bar: what it is, its count, how long
    it has run and - with a count and long enough to tell - about how long
    it has left at its own rate so far, then what goes on beside it"""
    t, unit = STEP_TEXT.get(step, (step, None))
    if total:
        t += ": %s %d of %d" % (unit or "", min(done, total), total) \
            if unit else ": %d of %d" % (min(done, total), total)
    out = t
    if secs is not None and secs >= 1:
        out += " - %s" % clock(secs)
        if total and 0 < done < total and secs >= 5:
            out += ", about %s left" % clock(secs * (total - done) / done)
    if note:
        out += "\n" + note[0].upper() + note[1:]
    return out


def lanes_text(lanes, now):
    """Every lane's state as the lines under the bar: the encode's own pass
    and step, and then any pass running ALONGSIDE it in another process"""
    out = []
    for name in sorted(lanes, key=lambda n: (n != "main", n)):
        st = lanes[name]
        head = ("Pass %d: %s" % (st["n"], st["label"])) if st.get("label") \
            else ""
        if name != "main":
            head = "Alongside" + (" - " + head if head else "")
        body = step_text(st["step"], st["done"], st["total"], st["note"],
                         now - st["since"]) if st.get("step") else ""
        out.append("\n".join(x for x in (head, body) if x))
    return "\n".join(x for x in out if x)


def lane_put(lanes, ev, now):
    """One progress report into `lanes`: a "pass" names the lane's next
    pass (and ends any lane alongside the encode's own, which finished
    before the encode went on); a new step starts its clock"""
    name = ev.get("lane", "main")
    st = lanes.setdefault(name, {"n": 0, "label": "", "step": "",
                                 "done": 0, "total": 0, "note": "",
                                 "since": now})
    if ev["step"] == "pass":
        st.update(n=st["n"] + 1, label=ev.get("note", ""), step="")
        if name == "main":
            for k in [k for k in lanes if k != "main"]:
                del lanes[k]
        return
    if ev["step"] != st["step"]:
        st["since"] = now
    st.update(step=ev["step"], done=ev["done"], total=ev["total"],
              note=ev.get("note", ""))


def file_facts(r, path=None):
    """What the file IS, a line a fact: the panel under the preview"""
    g = r.g
    secs = r.frames / r.fps
    w = g.wb * (4 if r.pixfmt == vid.PF_CGA4 else vid.PIX_PER_BYTE[g.layout])
    if r.pixfmt in (vid.PF_C512, vid.PF_TEXT):
        w = g.wb // 2                   # a cell is two bytes
    out = ["'%s'" % r.title + (" - %s" % r.credits if r.credits else "")]
    out.append("%s on %s, %d x %d%s%s%s" % (
        vid.PF_NAMES[r.pixfmt], g.name, w, g.h,
        ", each row shown twice" if r.rowscale > 1 else "",
        " on %s" % vid.SCREENS[r.screen][0] if getattr(r, "screen", 0)
        else "", ", flipped, each frame against its own page"
        if getattr(r, "own", False) else
        ", flipped" if getattr(r, "flip", False) else ""))
    out.append("%d frames at %.2f fps = %.1f s; sound %s" % (
        r.frames, r.fps, secs,
        "none" if not r.audio else "%s at %d Hz" % (
            {1: "PCM8", 2: "ADPCM4"}[r.audio], r.rate)))
    if r.nkeys:
        p = "none" if r.poster == 0xFFFF else "keyframe %d (frame %d)" % (
            r.poster, r.keys[r.poster][0])
        out.append("%d keyframes; the poster is %s" % (r.nkeys, p))
    else:
        out.append("no keyframes: no poster, and a play starts at the top")
    size = os.path.getsize(path) if path else len(r.d)
    out.append("%s bytes%s" % (                 # (no rate: the front
        "{:,}".format(size),                    # matter is read once)
        ", resident, %d rendition%s" % (r.nrend, "s" if r.nrend > 1 else "")
        if r.resident else ""))
    if not r.resident:                  # BY WHAT READS IT: the base's
        lyb = len(r.d) - r.lsp0 if r.lsp0 else 0    # stream every machine
        out.append("the base stream %.1f KB/s%s" % (    # plays, the layer
            r.slen / 1024.0 / secs,                     # only the better
            "; the layer %.1f KB/s, read only by a machine that plays it"
            % (lyb / 1024.0 / secs) if lyb else ""))    # one (98.1.9)
    if r.loop:
        out.append("repeats from frame %d" % r.loop[0])
    return out


def key_view(r, f):
    """(key index, its frame, its picture) - the keyframe a play from frame
    `f` starts at (os88vid.key_at), drawn from that keyframe alone; None
    for a file with none"""
    i = vid.key_at(r, f)
    if i is None:
        return None
    return (i,) + key_picture(r, i)


def key_picture(r, i):
    """(its frame, its picture): keyframe `i` drawn from itself alone"""
    k, rec, spo, spn, idx = r.key(i)
    if r.page:      # (98.1.3.7: the shown page's, the hidden record after
        ps, vis, _ = r.key_view(rec)    # its lists - which a plain decode
        surf = ps[vis]                  # refuses, and the picture was black;
    else:                               # on N pages the page it names)
        surf = r.g.surface()
        r.apply(surf, rec, key=True)
    return k, render(r, surf, k)


def poster_view(r):
    """(key index, its frame, its picture) of the file's POSTER - what the
    player shows before a play - or None when it has none"""
    if not r.nkeys or r.poster == 0xFFFF:
        return None
    return (r.poster,) + key_picture(r, r.poster)


def thumb(img, w=160, h=120):
    """A PIL picture as a Tk one, fitted into w x h"""
    z = min(float(w) / img.size[0], float(h) / img.size[1])
    img = img.resize((max(1, int(img.size[0] * z)),
                      max(1, int(img.size[1] * z))))
    buf = io.BytesIO()
    img.save(buf, "PNG")
    return tk.PhotoImage(data=base64.b64encode(buf.getvalue()))


# ...AND MAKE A DISK OF IT (SPEC.md 98.2.12.1): the four floppies os88disk
# builds, for B: beside a system disk, and three BOOTABLE hard disks
# os88hdd builds - the geometries of the machines the videos are made for
# (the ones `make videnchd` cut by hand for the owner's 5150 and 286). A
# hard disk is (tag, cylinders, heads, sectors, an ST11's layout)
#   label                                                 kind  what
DISKS = [
    ("a 360 KB floppy", "fd", 360),
    ("a 720 KB floppy", "fd", 720),
    ("a 1.2 MB floppy", "fd", 1200),
    ("a 1.44 MB floppy", "fd", 1440),
    ("a 20 MB hard disk: ST-225 on an ST11M (5150/XT)", "hd",
     ("ST11M", 615, 4, 17, True)),
    ("a 32 MB hard disk: ST-238R on an ST11R (XT)", "hd",
     ("ST11R", 615, 4, 26, True)),
    ("a 32 MB hard disk: IDE (286 and up)", "hd",
     ("IDE", 250, 15, 17, False)),
]
DISK_LABELS = [d[0] for d in DISKS]
# the disk a storage profile implies, until the person picks another
DISK_OF_PROFILE = {"5150-st225": 4, "5150-picomem2": 4, "floppy": 0,
                   "286-slow": 6, "286-fast": 6, "286-picomem1": 6,
                   "286-speeddemon": 6,
                   "486-dx2-66": 6, "lossless": 6}
# what a hard disk needs out of build/ to BOOT - the kernel, its boot records
# and HDD.DRV - and what a booting one takes besides when build/ has it.
# Without HD_BOOT (the encoder shipped to people with no os8088 tree) the disk
# is made anyway, formatted and NOT bootable: the machine boots its system
# floppy and HDD.DRV mounts the disk as C: (SPEC.md 98.2.12.1)
HD_BOOT = (("", "kernel.sys"), ("", "boothd.bin"),       # (os88hdd lays the
                                                       # kernel down itself)
           ("", "mbr.bin"), ("HDD.DRV", "hdd.drv"))
HD_WANT = (("VIDEO.O88", "video.o88"), ("CTRL.DRV", "ctrl.drv"),
           ("SOUND.DRV", "sound.drv"), ("HIBER.DRV", "hiber.drv"))
HD_NOBOOT_NOTE = ("It does not boot (no built os8088 here to put on it): boot "
                  "os8088 from its system floppy, tick the hard-disk driver "
                  "in the Control Panel, and it comes up as C:. With no "
                  "VIDEO.O88 on it, the player is found on the apps floppy.")


def short83(path):
    """The name a video goes onto a disk as: its own, cut to an 8.3 name
    (a 40-character one was refused by os88disk outright)"""
    stem = os.path.splitext(os.path.basename(path))[0].upper()
    # ASCII only: str.isalnum() passes "É" or "日", which no FAT directory
    # entry here can carry (os88hdd's latin1 encode raised on the second)
    stem = "".join(c for c in stem
                   if c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")[:8]
    return (stem or "VIDEO") + ".V88"


def player_path(build=None):
    """VIDEO.O88 for a disk: build/'s, or one shipped BESIDE this tool - the
    encoder handed to people with no os8088 tree can carry the player, and
    every disk it makes then carries it too. None when there is neither"""
    b = build or os.path.join(_ROOT, "build")
    for p in (os.path.join(b, "video.o88"), os.path.join(_HERE, "VIDEO.O88"),
              os.path.join(_HERE, "video.o88")):
        if os.path.exists(p):
            return p
    return None


def hd_missing(build=None):
    """What the boot files' folder (boot_dir) lacks for a hard disk that
    BOOTS: [] when it can make one. Anything missing makes the disk
    unbootable, not refused"""
    b = boot_dir(build)
    return [f for _, f in HD_BOOT if not os.path.exists(os.path.join(b, f))]


def boot_dir(build=None):
    """Where a hard disk's boot files come from: build/, or the bundle's
    boot/ BESIDE this tool (tools/os88vbundle.py --boot, SPEC.md 98.2.13) -
    so the encoder handed to people with no os8088 tree makes a hard disk
    that boots, as player_path lets its floppies carry the player"""
    b = build or os.path.join(_ROOT, "build")
    for d in (b, os.path.join(_HERE, "boot")):
        if all(os.path.exists(os.path.join(d, f)) for _, f in HD_BOOT):
            return d
    return b


def disk_argv(v88, disk, out=None, build=None, stage=None):
    """(the command line, the image) for DISKS[disk] holding the video and
    VIDEO.O88. A floppy takes the player when build/ has one, and the video
    under its 8.3 name (a copy in STAGE, a directory, when its own is
    longer); a hard disk BOOTS when build/ has HD_BOOT, and takes HD_WANT
    too - without HD_BOOT it is formatted, not bootable, and takes the
    player alone"""
    label, kind, what = DISKS[disk]
    b = boot_dir(build)
    base = os.path.splitext(v88)[0]
    name = short83(v88)
    if kind == "fd":
        img = out or base + "-%d.img" % what
        argv = [sys.executable, os.path.join(_HERE, "os88disk.py"), "-o",
                img, "--size", str(what)]
        player = player_path(build)
        if player:
            argv.append(player)
        src = v88
        if os.path.basename(v88).upper() != name:
            import shutil
            src = os.path.join(stage, name)
            shutil.copyfile(v88, src)
        return argv + [src], img
    tag, cyls, heads, spt, st11 = what
    img = out or base + "-%s.VHD" % tag
    argv = [sys.executable, os.path.join(_HERE, "os88hdd.py"), "--out", img,
            "--cyls", str(cyls), "--heads", str(heads), "--spt", str(spt)]
    if st11:
        argv.append("--st11")
    if hd_missing(b):
        argv.append("--noboot")
        files = ()                              # (the player, below)
    else:
        argv += ["--kernel", os.path.join(b, "kernel.sys"),
                 "--vbr", os.path.join(b, "boothd.bin"),
                 "--mbr", os.path.join(b, "mbr.bin")]
        files = HD_BOOT + HD_WANT[1:]
    for nm, f in files:
        if nm and os.path.exists(os.path.join(b, f)):
            argv += ["--file", "%s=%s" % (nm, os.path.join(b, f))]
    player = player_path(build)
    if player:
        argv += ["--file", "VIDEO.O88=%s" % player]
    return argv + ["--file", "%s=%s" % (name, v88)], img


def out_for(src, ext=".V88"):
    """The file a source is saved as by default: beside it, its own name
    cut to 40 characters - the NAME, where the whole path used to be cut,
    which put a long folder's file in its grandparent under half a name -
    as the Save as type (`ext`, a .V88 unless a speaker WAV was chosen)"""
    d, n = os.path.split(src)
    return os.path.join(d, os.path.splitext(n)[0][:40] + ext)


# the Save as line's types: what Browse... offers first, and the extension
# a default name takes. A .WAV is a speaker WAV for Audio (86.21.1)
OUT_TYPES = [(".V88", "os8088 video", "*.V88"),
             (".WAV", "Speaker sound for Audio", "*.WAV")]


def drop_target(paths):
    """What a drop onto the window does (98.2.12): ("preview", path) for a
    .V88 - it is looked at, not encoded - else ("source", path), the first
    file dropped being the one taken. None for a drop of nothing"""
    paths = [p for p in paths if p]
    if not paths:
        return None
    p = paths[0]
    if p.lower().endswith(".v88"):
        return "preview", p
    return "source", p


# --------------------------------------------------------------------------
# the window
# --------------------------------------------------------------------------
def swatch_draw(c, secs, tips, width=None):
    """Sections of palette_view's shape drawn on canvas `c`, which is then
    as tall as they are: a heading (None: none), each row its caption and
    its swatches - beside it when they are one short row, in columns, and
    under it when a grid - flowed across with as many to a line as fit and
    the lines made even (six CGA sets are two lines of three, dim over
    bright), and the note (None: none). tips[(c, item)] = what a swatch is"""
    import tkinter.font as tkfont
    c.delete("all")
    for k in [k for k in tips if k[0] is c]:
        del tips[k]
    fg = ttk.Style().lookup("TLabel", "foreground") or "black"
    font = tkfont.nametofont("TkDefaultFont")
    th = font.metrics("linespace")
    width = width or max(c.winfo_width(), PAL_W)
    y = 2
    for head, rows, note in secs:
        if head:
            c.create_text(4, y, text=head, anchor="nw", fill=fg,
                          font=("TkDefaultFont", 9, "bold"))
            y += th + 2
        lay = []                        # (caption w, beside?, row's width)
        for line, sw in rows:
            cols, w, h, gap = grid_of(len(sw))
            gw = cols * (w + gap) if sw else 0
            tw = font.measure(line) + 6
            side = len(sw) <= cols
            lay.append((tw, side, tw + gw if side else max(tw, gw)))
        if lay and all(side for _, side, _ in lay):     # in columns
            tw = max(L[0] for L in lay)
            cw = max(L[2] - L[0] for L in lay) + tw
            lay = [(tw, True, cw)] * len(lay)
        fit = max(1, (width - 4) // (max([L[2] for L in lay] + [1]) + 16))
        per = -(-len(rows) // -(-len(rows) // fit)) if rows else 1
        x, lineh = 4, 0
        for n, ((line, sw), (tw, side, cw)) in enumerate(zip(rows, lay)):
            cols, w, h, gap = grid_of(len(sw))
            if n and n % per == 0:
                x, y, lineh = 4, y + lineh, 0
            c.create_text(x, y + (max(h - th, 0) // 2 if side else 0),
                          text=line, anchor="nw", fill=fg)
            sx, sy = (x + tw, y) if side else (x, y + th + 2)
            for i, sv in enumerate(sw):
                x0 = sx + (i % cols) * (w + gap)
                y0 = sy + (i // cols) * (h + gap)
                if sv is None:          # chosen from the clip
                    it = c.create_rectangle(x0, y0, x0 + w - 1, y0 + h - 1,
                                            outline="#888", dash=(2, 2))
                    c.create_text(x0 + w // 2, y0 + h // 2, text="?",
                                  fill="#888")
                    tips[(c, it)] = "the background, chosen from the clip"
                    continue
                it = c.create_rectangle(x0, y0, x0 + w - 1, y0 + h - 1,
                                        fill=sv[0], outline="#666" if gap
                                        else sv[0])
                tips[(c, it)] = "%s  %s" % (sv[1], sv[0])
            rowsn = -(-len(sw) // cols) if sw else 0
            lineh = max(lineh, (sy - y) + rowsn * (h + gap) + 6, th + 6)
            x += cw + 16
        y += lineh
        if note:
            it = c.create_text(4, y, text=note, anchor="nw", fill="#555",
                               width=width - 8)
            y = c.bbox(it)[3] + 6
    c.config(height=y)


class WordVar(tk.StringVar if tk is not None else object):
    """A field's variable that SHOWS its BLANK_WORD for a blank and READS
    the word back as the blank (from_shown/to_shown), so every reader of
    the form keeps its blank and the person sees what the blank means"""

    def __init__(self, dest, value=""):
        self.dest = dest
        tk.StringVar.__init__(self, value=to_shown(dest, value))

    def raw(self):
        return tk.StringVar.get(self)

    def get(self):
        return from_shown(self.dest, tk.StringVar.get(self))

    def set(self, value):
        tk.StringVar.set(self, to_shown(self.dest, value))


class Tip(object):
    """A tooltip: the option's help, shown while the pointer rests on it.
    `text` may be a function, asked each time - a greyed group's says why,
    and says nothing once the group applies"""

    def __init__(self, widget, text, on="<Enter>"):
        self.w, self.text, self.top = widget, text, None
        widget.bind(on, self.show)
        widget.bind("<Leave>", self.hide)

    def show(self, _e=None):
        text = self.text() if callable(self.text) else self.text
        if not text or self.top:
            return
        x = self.w.winfo_rootx() + 16
        y = self.w.winfo_rooty() + self.w.winfo_height() + 4
        self.top = tk.Toplevel(self.w)
        self.top.wm_overrideredirect(True)
        self.top.wm_geometry("+%d+%d" % (x, y))
        tk.Label(self.top, text=text, justify="left", wraplength=420,
                 background="#ffffe0", relief="solid", borderwidth=1,
                 padx=6, pady=4).pack()

    def hide(self, _e=None):
        if self.top:
            self.top.destroy()
            self.top = None


class EncodeJob(object):
    """AN ENCODE IN A PROCESS OF ITS OWN (98.2.11.1): os88venc run the way
    the command line runs it, with --progress, so that Cancel KILLS it -
    and its ffmpeg and any worker it started - rather than asking it to
    stop at its next report. In the window's thread the encode shared one
    interpreter lock with the window and stopped only when it next checked:
    on the owner's machine that was the end of the encode. No Tk here, so
    the gate drives it as the window does"""

    def __init__(self, argv):
        self.argv = list(argv)
        self.p = None
        self.killed = False

    def start(self):
        cmd = [sys.executable, "-u", os.path.join(_HERE, "os88venc.py")] + \
            self.argv + ["--progress"]
        env = dict(os.environ, PYTHONIOENCODING="utf-8")
        kw = dict(stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                  stderr=subprocess.STDOUT, env=env, encoding="utf-8",
                  errors="replace", bufsize=1)
        if sys.platform == "win32":     # its own group, and no console
            kw["creationflags"] = 0x00000200 | 0x08000000
        else:                           # its own group, killed as one
            kw["start_new_session"] = True
        self.p = subprocess.Popen(cmd, **kw)
        return self

    def events(self):
        """("prog", {lane, step, done, total, note}) and ("log", line) as
        the encode goes, then ("exit", returncode)"""
        import json
        for line in self.p.stdout:
            if line.startswith(V.PROGRESS + " "):
                try:
                    ev = json.loads(line[len(V.PROGRESS) + 1:])
                    ev["done"], ev["total"] = int(ev["done"]), \
                        int(ev["total"])
                    yield "prog", ev
                    continue
                except (ValueError, KeyError, TypeError):
                    pass
            yield "log", line
        yield "exit", self.p.wait()

    def cancel(self):
        """Kill it, and everything it started. A half-written .V88 cannot be
        left: the encoder writes a .part and renames it (98.2.11.1), and
        the .part is removed here"""
        self.killed = True
        p = self.p
        if p is None or p.poll() is not None:
            return
        try:
            if sys.platform == "win32":
                subprocess.run(["taskkill", "/F", "/T", "/PID", str(p.pid)],
                               capture_output=True, creationflags=0x08000000)
            else:
                import signal
                os.killpg(p.pid, signal.SIGKILL)
        except Exception:
            p.kill()
        try:
            p.wait(5)
        except Exception:
            pass

    def leftover(self, out):
        """A .part the kill interrupted, removed"""
        try:
            os.remove(out + ".part")
        except OSError:
            pass


class App(object):
    def choices_help(self, f, v):
        """A window listing every choice of field `f` and what it is; the
        one chosen now in bold, and a click on any takes it"""
        top = tk.Toplevel(self.root)
        top.title("%s - %s" % (f["label"], APPNAME))
        top.transient(self.root)
        fr = ttk.Frame(top, padding=10)
        fr.pack(fill="both", expand=True)
        ttk.Label(fr, text=f["tip"], wraplength=520, justify="left",
                  foreground="#555").grid(row=0, column=0, columnspan=3,
                                          sticky="w", pady=(0, 8))
        vals = {k: x.get() for k, x in self.vars.items()}
        bold = ("TkDefaultFont", 10, "bold")

        def take(c):
            v.set(c)
            if f["dest"] in IMPLYING:
                self.apply_implied(f["dest"])
            elif f["dest"] == "audio":
                self.apply_audio()
            elif f["dest"] == "spk_style":
                self.apply_style()
            elif f["dest"] == "layer_profile":
                self.apply_layer()
            top.destroy()
        for i, (c, what, on) in enumerate(choice_lines(f, v.get()), 1):
            b = ttk.Button(fr, text=c, width=14,
                           command=lambda c=c: take(c))
            b.grid(row=i, column=0, sticky="nw", pady=1)
            sw = choice_swatches(f["dest"], c, vals)
            if sw:                      # A PALETTE'S CHOICE: its colours
                cv = tk.Canvas(fr, width=24 * len(sw), height=18,
                               highlightthickness=0)
                for j, (rgb, _n) in enumerate(sw):
                    cv.create_rectangle(24 * j + 1, 1, 24 * j + 22, 17,
                                        fill=rgb, outline="#666")
                cv.grid(row=i, column=1, sticky="w", padx=(8, 0), pady=1)
            lab = ttk.Label(fr, text=what, wraplength=420, justify="left")
            if on:
                lab.configure(font=bold)
            lab.grid(row=i, column=2, sticky="w", padx=8, pady=1)
        ttk.Button(fr, text="Close", command=top.destroy).grid(
            row=i + 1, column=2, sticky="e", pady=(8, 0))
        top.bind("<Escape>", lambda e: top.destroy())

    def __init__(self, root):
        self.root = root
        self.q = queue.Queue()
        self.vars = {}
        self.sfps = None                # the source's frame rate, once read
        self.frames = []
        self.reader = None
        self.cur = None                 # the .V88 in the preview
        self.key = None                 # the keyframe shown under it
        self.busy = False
        self.cancel = threading.Event()
        self.prog = None                # (step, done, total), from the thread
        self.lanes = {}                 # (the encoder's lanes: lane_put)
        self.job = 0                    # the job the window is showing: a
        self.ejob = None                # message from any other is stale
        root.title(APPNAME)
        root.geometry("1080x840")      # (the Palette panel: 98.2.8.1)
        self.src = tk.StringVar()
        self.out = tk.StringVar()
        self.outtype = tk.StringVar(value=OUT_TYPES[0][0])
        self.target = tk.StringVar(value=TARGETS[0][0])
        self.info = tk.StringVar(value="Choose a video.")
        self.mkdisk = tk.BooleanVar(value=False)
        self.disksize = tk.StringVar(value=DISK_LABELS[0])
        self.diskpicked = False         # ...a person's choice, kept
        self.layerfill = ""             # what apply_layer last filled in
        self._build()
        self.apply_target()
        self.update_groups()
        root.after(100, self._pump)

    def _build(self):
        start = form_start()            # the form as it opens
        top = ttk.Frame(self.root, padding=8)
        top.pack(fill="both", expand=True)
        # the preview's column is packed FIRST, so a narrow window squeezes
        # the form rather than cutting the picture and its panel off
        right = ttk.Frame(top, padding=(8, 0, 0, 0))
        right.pack(side="right", fill="both")
        left = ttk.Frame(top)
        left.pack(side="left", fill="both", expand=True)
        # --- the essentials, always on screen
        ess = ttk.Frame(left)
        ess.pack(fill="x")
        for row, (label, var, cmd) in enumerate((
                ("Input", self.src, self.browse_src),
                ("Save as", self.out, self.browse_out))):
            ttk.Label(ess, text=label).grid(row=row, column=0, sticky="w")
            ttk.Entry(ess, textvariable=var, width=52).grid(
                row=row, column=1, sticky="we", padx=4)
            ttk.Button(ess, text="Browse...", command=cmd).grid(
                row=row, column=2)
        # WHAT is saved: a .V88, or the sound alone as a speaker WAV for
        # Audio - Browse...'s type first, and a default name's extension
        ot = ttk.Combobox(ess, textvariable=self.outtype, state="readonly",
                          values=[t[0] for t in OUT_TYPES], width=6)
        ot.grid(row=1, column=3, sticky="we", padx=(4, 0))
        ot.bind("<<ComboboxSelected>>", lambda e: self.pick_outtype())
        Tip(ot, "What to save: a .V88 video, or a .WAV - the sound alone, "
                "shaped for the PC speaker, for Audio to play (SPEC.md "
                "86.21.1). Browse... offers this type first, and the name "
                "above takes its extension.")
        ttk.Label(ess, text="Made for").grid(row=2, column=0, sticky="w")
        tg = ttk.Combobox(ess, textvariable=self.target, state="readonly",
                          values=[t[0] for t in TARGETS], width=58)
        tg.grid(row=2, column=1, columnspan=2, sticky="we", padx=4)
        # A .V88 MADE BEFORE: previewed, and the form set to the options it
        # was made with when it carries them (98.2.17) - beside the Input's
        # Browse..., being the other way to fill that line's role
        ob = ttk.Button(ess, text="Load .V88", command=self.browse_v88)
        ob.grid(row=0, column=3, sticky="we", padx=(4, 0))
        Tip(ob, "Open a .V88 made before: scrub it, change its poster or "
                "its title - and, when it carries the options it was made "
                "with, set every field below to them, so it can be made "
                "again or changed.")
        tg.bind("<<ComboboxSelected>>", lambda e: self.apply_target())
        Tip(tg, "The machine and screen it will play on. This sets the "
                "preset, the pixel format and the storage profile below, "
                "and fills in every option they imply - the canvas, the "
                "frame rate, the detail, the budget and the sound - as the "
                "encoder will use them. Change any of them afterwards on "
                "their tabs.")
        ttk.Label(ess, textvariable=self.info, foreground="#555").grid(
            row=3, column=1, columnspan=3, sticky="w", padx=4)
        ess.columnconfigure(1, weight=1)
        # --- make a disk, go, the progress and the log: packed from the
        # BOTTOM and before the tabs, so it is the tabs that give way in a
        # short window and not the log (which the default size cut off
        # entirely - and the Encode button with it, off the row's end)
        self.log = tk.Text(left, height=6, wrap="word")
        self.log.pack(side="bottom", fill="x", pady=(6, 0))
        pr = ttk.Frame(left)
        pr.pack(side="bottom", fill="x", pady=(6, 0))
        go = ttk.Frame(left)
        go.pack(side="bottom", fill="x")
        # --- every option, on tabs
        nb = ttk.Notebook(left)
        nb.pack(fill="both", expand=True, pady=6)
        pages = {}
        for t in TABS:
            pages[t] = ttk.Frame(nb, padding=6)
            nb.add(pages[t], text=t)
        # DEGRADE TO FIT, the ladder's own tab (VIDEO-SEEN-PLAN 3a): laid
        # out by ladder_page below rather than as a column of groups
        self.ladpage = pages["Degrade to Fit"]
        # --- the GROUPS (98.2.8.2): an outline with a header round the
        # options that go together, down one column - or, on a tab of more
        # than TAB_ROWS options, two, split in the table's order where the
        # taller is shortest (the Picture tab's 21 in one were taller than
        # the window)
        fl = {f["dest"]: f for f in fields()}
        tabgroups = {t: [] for t in TABS}
        for tab, name, dests, _ in GROUPS:
            if name in LADDER_PAGE:         # (its own tab, below)
                continue
            tabgroups[tab].append((name, [fl[d] for d in dests if d in fl]))
        other = [f for d, f in fl.items() if d not in GROUP_OF]
        if other:                       # an option no group names yet
            tabgroups["Basic"].append(("Other", other))
        st = ttk.Style(self.root)
        bold = tkfont.nametofont("TkDefaultFont").copy()
        bold.configure(weight="bold")
        st.configure("Group.TLabel", font=bold)
        self.groupfont = bold
        # a "?" or a Browse... as tall as the field beside it, not a row
        # and a half: a tab of groups has no height to spare
        st.configure("Tool.TButton", padding=(4, 0))
        norm = tkfont.nametofont("TkDefaultFont")
        self.gheads = {}                # header -> its label
        self.fwidgets = {}              # option -> its label and widgets
        self.gwhy, self.why = {}, {}    # why a group, an option, is off
        self.grppend = False
        lastcol = {}                    # (the tab's shortest column: its note)
        for t in TABS:
            gs = [g for g in tabgroups[t] if g[1]]
            if not gs:                  # (Degrade to Fit: ladder_page's)
                continue
            cols = [gs]
            if sum(len(g[1]) for g in gs) > TAB_ROWS and len(gs) > 1:
                hs = [len(g[1]) + 1.5 for g in gs]
                k = min(range(1, len(gs)), key=lambda k: max(
                    sum(hs[:k]), sum(hs[k:])))
                cols = [gs[:k], gs[k:]]
            # a TWO-COLUMN tab's fields are narrower, or the right-hand
            # column's "?" and Browse... fall off the pane's edge
            fw = 22 if len(cols) == 1 else 11
            tall = None
            for ci, col in enumerate(cols):
                cf = ttk.Frame(pages[t])
                h = sum(len(g[1]) + 1.5 for g in col)
                if tall is None or h < tall:
                    lastcol[t], tall = cf, h
                cf.grid(row=0, column=ci, sticky="nwe",
                        padx=(0 if ci == 0 else 10, 0))
                # every group in a column lines its fields up, by the
                # labels' own widths rather than a count of characters
                lw = max(norm.measure(f["label"]) for g in col
                         for f in g[1]) + 6
                for name, fs in col:
                    self.group_box(cf, name, fs, fw, lw, start)
        st.configure("GroupCheck.TCheckbutton", font=bold)
        self.ladder_page(self.ladpage, start)
        lf = ttk.Frame(self.ladpage)
        lf.grid(row=2, column=0, columnspan=2, sticky="nwe", pady=(6, 0))
        ci = 0                          # (B and what serves both, side by
        for tab, name, dests, _ in GROUPS:      # side)
            if name in LADDER_PAGE and name != "Degrade ladder":
                fs = [fl[d] for d in dests if d in fl]
                cf = ttk.Frame(lf)
                cf.grid(row=0, column=ci, sticky="nw",
                        padx=(0 if ci == 0 else 10, 0))
                self.group_box(cf, name, fs, 11, max(
                    norm.measure(f["label"]) for f in fs) + 6, start)
                ci += 1
        for t, text in TAB_NOTES.items():   # a word the fields cannot say
            if t in NOTE_IN_COLUMN:
                ttk.Label(lastcol[t], text=text, foreground="#555",
                          wraplength=310, justify="left").pack(
                    anchor="w", pady=(4, 0))
                continue
            ttk.Label(pages[t], text=text, foreground="#555",
                      wraplength=600, justify="left").grid(
                row=1, column=0, columnspan=2, sticky="w", pady=(10, 0))
        for d in CONTEXT_FIELDS:        # a change re-judges every group
            if d in self.vars:
                self.vars[d].trace_add("write",
                                       lambda *a: self.groups_dirty())
        self.out.trace_add("write", lambda *a: self.groups_dirty())
        self.out.trace_add("write", lambda *a: self.sync_outtype())
        # --avg's list follows the machine: AUTO, and the share the profile
        # (or a Live file) used to fix (98.2.1.5)
        for d in ("profile", "live"):
            if d in self.vars:
                self.vars[d].trace_add("write", lambda *a: self.avg_list())
        self.avg_list()
        # THE PROFILE BEFORE THIS ONE, however it was set - a pick, a
        # target, a file loaded - so a pick by hand can tell the fields it
        # filled from the ones a person changed since (apply_implied)
        self.profwas = self.profnow = self.vars["profile"].get()

        def profwrite(*a):
            self.profwas, self.profnow = self.profnow, \
                self.vars["profile"].get()
        self.vars["profile"].trace_add("write", profwrite)
        # --- the palette the Colour tab's choices give, redrawn as they
        # change: under the tab's fields - what the file WILL be, where the
        # preview's column is the file that IS (the owner). The VGA groups
        # pushed it off the window once: the window is taller now, and the
        # tab's groups are ordered so its two columns are near one height
        self.palframe = ttk.LabelFrame(pages["Colour"], text="Palette",
                                       padding=(6, 0, 6, 2))
        self.palframe.grid(row=2, column=0, columnspan=2, sticky="we",
                           pady=(6, 0))
        self.palcanvas = tk.Canvas(self.palframe, width=PAL_W, height=60,
                                   highlightthickness=0)
        self.palcanvas.pack(anchor="w", fill="x")
        self.paltips = {}
        self.paltip = None
        self.palpend = False
        for d in PAL_FIELDS:
            if d in self.vars:
                self.vars[d].trace_add("write", lambda *a: self.pal_dirty())
        # --- make a disk, and go: the buttons packed FIRST, so a narrow
        # row squeezes the disk list rather than cutting Encode off
        self.stopbtn = ttk.Button(go, text="Cancel", command=self.stop,
                                  state="disabled")
        self.stopbtn.pack(side="right")
        Tip(self.stopbtn, "Stop the encode. The .V88 is written only at the "
                          "end, so nothing is written, and a file it would "
                          "have replaced is left as it was.")
        self.gobtn = ttk.Button(go, text="Encode", command=self.encode)
        self.gobtn.pack(side="right", padx=4)
        cb = ttk.Checkbutton(go, text="...and make a disk of it:",
                             variable=self.mkdisk)
        cb.pack(side="left")
        tip = ("An image beside the .V88 with the video on it, under an "
               "8.3 name. A FLOPPY takes VIDEO.O88 too when build/ has one: "
               "put it in B: and double-click the file. A HARD DISK is a "
               "fixed VHD that BOOTS - the kernel, HDD.DRV, the drivers, "
               "VIDEO.O88 and the video - for 86Box or the machine itself: "
               "an ST-225 on an ST11M is the IBM 5150's 20 MB, an ST-238R "
               "on an ST11R an XT's 32 MB, and IDE a 286's. Where there is "
               "no built os8088 to put on it, the hard disk is formatted "
               "but does not boot: " + HD_NOBOOT_NOTE)
        Tip(cb, tip)
        dc = ttk.Combobox(go, textvariable=self.disksize, state="readonly",
                          values=DISK_LABELS, width=30)
        dc.pack(side="left", padx=4, fill="x", expand=True)
        dc.bind("<<ComboboxSelected>>",
                lambda e: setattr(self, "diskpicked", True))
        Tip(dc, tip)
        self.bar = ttk.Progressbar(pr, maximum=1000, mode="determinate")
        self.bar.pack(fill="x")
        self.status = tk.StringVar(value="")
        ttk.Label(pr, textvariable=self.status, foreground="#555",
                  justify="left", wraplength=560).pack(anchor="w")
        # --- the preview
        hd = ttk.Frame(right)
        hd.pack(fill="x")
        ttk.Label(hd, text="What the screen will show").pack(side="left")
        self.canvas = tk.Canvas(right, width=400, height=300,
                                background="black", highlightthickness=0)
        self.canvas.pack()
        self.scrub = tk.Scale(right, from_=0, to=0, orient="horizontal",
                              length=400, command=self.show_frame,
                              showvalue=False)
        self.scrub.pack(fill="x")
        # ONE FRAME BACK OR ON (the owner, 2026-10-18): a button each side
        # of the frame's line, and , and . on the preview and the slider -
        # the player's keys for it (98.3.14)
        sr = ttk.Frame(right)
        sr.pack(fill="x")
        ttk.Button(sr, text="<", width=3,
                   command=lambda: self.step_frame(-1)).pack(side="left")
        self.fr = tk.StringVar(value="")
        ttk.Label(sr, textvariable=self.fr).pack(side="left", padx=6)
        ttk.Button(sr, text=">", width=3,
                   command=lambda: self.step_frame(1)).pack(side="left")
        for w in (self.canvas, self.scrub):
            w.bind("<comma>", lambda e: self.step_frame(-1))
            w.bind("<period>", lambda e: self.step_frame(1))
        self.canvas.bind("<Button-1>", lambda e: self.canvas.focus_set(),
                         add="+")
        # --- the file, and the keyframe a play from here starts at
        fi = ttk.LabelFrame(right, text="The file", padding=6)
        fi.pack(fill="x", pady=(6, 0))
        self.facts = tk.StringVar(value="Encode a video, or open a .V88.")
        fl = ttk.Label(fi, textvariable=self.facts, justify="left",
                       wraplength=370)
        fl.pack(anchor="w", fill="x")
        fi.bind("<Configure>", lambda e: fl.config(
            wraplength=max(120, e.width - 16)))
        # the palette the file was encoded with, where its clip chose it,
        # or its own: packed here by draw_palette when there is one. Kept
        # short (a 256 is 24 pixels, a VGA4 file's the values it uses) so
        # the box is never taller than the column
        self.fpal = tk.Canvas(fi, width=FPAL_W, height=24,
                              highlightthickness=0)
        tf = ttk.Frame(fi)              # the title, changed in place
        tf.pack(fill="x", pady=(6, 0))
        self.titlef = tf
        for c in (self.palcanvas, self.fpal):
            c.bind("<Motion>", self.pal_motion)
            c.bind("<Leave>", self.pal_leave)
        ttk.Label(tf, text="Title").pack(side="left")
        self.titlev = tk.StringVar(value="")
        self.titlee = ttk.Entry(tf, textvariable=self.titlev, width=30,
                                state="disabled")
        self.titlee.pack(side="left", padx=4, fill="x", expand=True)
        self.titlee.bind("<Return>", lambda e: self.set_title())
        self.titlebtn = ttk.Button(tf, text="Set title",
                                   command=self.set_title, state="disabled")
        self.titlebtn.pack(side="left")
        for w in (self.titlee, self.titlebtn):
            Tip(w, "Change the title the player shows. Only the title "
                   "changes: the file is not encoded again. Up to %d "
                   "characters; the player's panel shows the first 35."
                % (vid.TITLE_LEN - 1))
        self.thumbbtn = ttk.Button(tf, text="Redo posters",
                                   command=self.redo_posters,
                                   state="disabled")
        self.thumbbtn.pack(side="left", padx=(4, 0))
        Tip(self.thumbbtn, "Make this file's desktop posters again with the "
                           "Desktop posters, size, clip and poster picture "
                           "set now - from its own keyframes, so only the "
                           "posters change: the file is not encoded again. "
                           "A poster picture must be a picture file here, "
                           "not a moment of the video.")
        # THE POSTER, and the keyframe a play from the scrubbed frame starts
        # at, side by side: what the player shows now, and what Set as
        # poster would make it
        kf = ttk.Frame(fi)
        kf.pack(fill="x", pady=(6, 0))
        pl = ttk.Frame(kf)
        pl.pack(side="left", anchor="n")
        ttk.Label(pl, text="The poster", style="Group.TLabel").pack(
            anchor="w", pady=(2, 1))
        self.pcanvas = tk.Canvas(pl, width=160, height=120,
                                 background="black", highlightthickness=0)
        self.pcanvas.pack(anchor="w")
        self.ptext = tk.StringVar(value="")
        ttk.Label(pl, textvariable=self.ptext, justify="left",
                  wraplength=160).pack(anchor="w")
        Tip(self.pcanvas, "The picture the player shows before a play: "
                          "the file's poster keyframe.")
        kr = ttk.Frame(kf, padding=(10, 0, 0, 0))
        kr.pack(side="left", anchor="n", fill="both", expand=True)
        # its header carries Set as poster, a row a file with a palette
        # line has no height for under the picture
        kh = ttk.Frame(kr)
        kh.pack(fill="x")
        ttk.Label(kh, text="Here", style="Group.TLabel").pack(side="left")
        self.kcanvas = tk.Canvas(kr, width=160, height=120,
                                 background="black", highlightthickness=0)
        self.kcanvas.pack(anchor="w")
        Tip(self.kcanvas, "The keyframe a play from the scrubbed frame "
                          "starts at - the one Set as poster makes the "
                          "poster.")
        self.ktext = tk.StringVar(value="")
        ttk.Label(kr, textvariable=self.ktext, justify="left",
                  wraplength=200).pack(anchor="w")
        self.posterbtn = ttk.Button(kh, text="Set as poster",
                                    command=self.set_poster,
                                    state="disabled", style="Tool.TButton")
        self.posterbtn.pack(side="right")
        Tip(self.posterbtn, "Make this keyframe the picture the player "
                            "shows before a play. Only the poster changes: "
                            "the file is not encoded again.")

    # --- the palette panel
    def pal_dirty(self):
        """A palette field changed: redraw once, when Tk is idle - a target
        sets a dozen fields at a time"""
        if not self.palpend:
            self.palpend = True
            self.root.after_idle(self.draw_palette)

    def draw_palette(self):
        """The Colour tab's Palette panel, and the open file's under its
        facts. The tab's first heading is its frame's label"""
        self.palpend = False
        vals = {k: v.get() for k, v in self.vars.items()}
        secs = palette_view(vals)
        self.palframe.config(text="Palette: " + secs[0][0])
        swatch_draw(self.palcanvas, [(None,) + secs[0][1:]] + secs[1:],
                    self.paltips,
                    width=max(self.palcanvas.winfo_width(), PAL_W))
        self.fpalnow = None
        self.draw_fpal(getattr(self, "ffrm", None))

    def draw_fpal(self, f):
        """The open file's palette under its facts - at frame `f`, the one
        the scrubber shows, for a file with a palette a scene - redrawn only
        when it changes: a drag through a scene is one draw"""
        self.ffrm = f
        rows = file_palette(self.reader, f)
        if rows == getattr(self, "fpalnow", None):
            return
        self.fpalnow = rows
        if rows:                    # (a palette a scene's: its scene as
            head = rows[0][2] if len(rows[0]) > 2 else None   # a heading)
            swatch_draw(self.fpal, [(head or None, [
                ("Palette: " + ln if not head else ln, sw)
                for ln, sw, *_ in rows], None)], self.paltips, width=FPAL_W)
            if not self.fpal.winfo_ismapped():
                self.fpal.pack(anchor="w", fill="x", pady=(6, 0),
                               before=self.titlef)
        else:
            self.fpal.pack_forget()

    def pal_motion(self, e):
        """What the swatch under the pointer is, in a tip beside it"""
        c = e.widget
        hit = c.find_overlapping(e.x, e.y, e.x, e.y)
        tips = [self.paltips[(c, i)] for i in hit if (c, i) in self.paltips]
        if not tips:
            return self.pal_leave()
        if self.paltip is None:
            self.paltip = tk.Toplevel(c)
            self.paltip.wm_overrideredirect(True)
            self.paltipv = tk.StringVar()
            tk.Label(self.paltip, textvariable=self.paltipv,
                     background="#ffffe0", relief="solid", borderwidth=1,
                     padx=4, pady=1).pack()
        self.paltipv.set(tips[-1])
        self.paltip.wm_geometry("+%d+%d" % (e.x_root + 14, e.y_root + 12))

    def pal_leave(self, _e=None):
        if self.paltip is not None:
            self.paltip.destroy()
            self.paltip = None

    # --- the essentials
    def browse_src(self):
        p = filedialog.askopenfilename(title="A video")
        if p:
            self.set_src(p)

    def dropped(self, paths):
        """A drop, on Tk's thread (98.2.12): a .V88 is looked at, anything
        else is the video to encode"""
        what = drop_target(paths)
        if what is None or self.busy:
            return
        kind, p = what
        if kind == "preview":
            self.load_v88(p)
        else:
            self.set_src(p)

    def set_src(self, p):
        self.src.set(p)
        # the .V88 beside it, unless a name was chosen by hand: a second
        # video dropped takes a name of its own
        if not self.out.get() or self.out.get() == getattr(self, "autoout",
                                                           None):
            self.autoout = out_for(p, self.outtype.get())
            self.out.set(self.autoout)
        try:
            text, vals = suggest(p)
        except Exception as e:
            self.info.set("ffprobe could not read it: %s" % e)
            return
        self.info.set(text)
        # A TITLE ALREADY THERE STAYS: one a .V88 loaded or a person typed
        # is the title, and the source's file name only fills an empty
        # field - or one still holding the name it filled last time, so a
        # second video dropped is renamed as the first was
        auto = vals.pop("title", "")
        if keep_title(self.vars["title"].get(),
                      getattr(self, "autotitle", None)):
            self.autotitle = None
        else:
            self.vars["title"].set(auto)
            self.autotitle = auto
        for k, v in vals.items():
            self.vars[k].set(v)
        cur = {k: v.get() for k, v in self.vars.items()}
        was = implied_values(cur, self.sfps)["fps"]
        try:
            self.sfps = V.probe(p)[3]   # it caps the frame rate shown...
        except Exception:
            self.sfps = None
        if cur.get("fps", "") in ("", was):     # ...unless it was edited
            self.vars["fps"].set(implied_values(cur, self.sfps)["fps"])

    def browse_save(self, f, v):
        """A SAVE_FILE option's Browse...: its type, beside the .V88 and
        under the .V88's name unless it already names a file of its own"""
        what, ext, types = SAVE_FILE[f["dest"]]
        cur = v.get().strip() or os.path.splitext(self.out.get().strip())[0]
        kw = {}
        if cur:
            d, n = os.path.split(cur)
            kw = dict(initialdir=d or None,
                      initialfile=os.path.splitext(n)[0] + ext)
        p = filedialog.asksaveasfilename(title=what, defaultextension=ext,
                                         filetypes=types, **kw)
        if p:
            v.set(p)

    def browse_out(self):
        ext = self.outtype.get()
        types = sorted(OUT_TYPES, key=lambda t: t[0] != ext)
        p = filedialog.asksaveasfilename(
            defaultextension=ext, filetypes=[(w, g) for _, w, g in types])
        if p:
            self.out.set(p)

    def pick_outtype(self):
        """The Save as type chosen: a name already there takes its
        extension, so what the line says is what will be written"""
        cur = self.out.get().strip()
        base, e = os.path.splitext(cur)
        ext = self.outtype.get()
        if cur and e.upper() != ext:
            auto = cur == getattr(self, "autoout", None)
            self.out.set((base if e.upper() in [t[0] for t in OUT_TYPES]
                          else cur) + ext)
            if auto:
                self.autoout = self.out.get()

    def sync_outtype(self):
        """A name typed or browsed to: the type box follows its extension"""
        e = os.path.splitext(self.out.get().strip())[1].upper()
        if e in [t[0] for t in OUT_TYPES] and e != self.outtype.get():
            self.outtype.set(e)

    def apply_target(self):
        i = [t[0] for t in TARGETS].index(self.target.get())
        for k, v in target_fill(i, self.sfps).items():
            self.vars[k].set(v)
        self.imply_disk()

    def imply_disk(self):
        """The disk the profile implies, unless one was picked by hand"""
        d = DISK_OF_PROFILE.get(self.vars["profile"].get())
        if d is not None and not self.diskpicked:
            self.disksize.set(DISK_LABELS[d])

    def apply_implied(self, dest=None):
        """A preset, format, profile or Live choice changed by hand: every
        option it implies is shown as it will be used (98.2.10).

        A PROFILE is the machine and nothing else, so picking one moves
        only what the machine sets - and of that, only what the new machine
        sets DIFFERENTLY, and only a field still holding what the old one
        set: the box, the frame rate and a peak or disk typed by hand all
        stay (the owner, 2026-10-15: every pick of a profile dropped the
        box and the peak, and each encode was a hunt for what changed). A
        preset, a format or Live is a fresh start and sets the lot"""
        vals = {k: v.get() for k, v in self.vars.items()}
        vals["pixfmt"] = fitting_pixfmt(vals)
        new = implied_values(vals, self.sfps)
        if dest == "profile":
            was = dict(vals, profile=self.profwas)
            was["pixfmt"] = fitting_pixfmt(was)
            old = implied_values(was, self.sfps)
            # (to or from LOSSLESS every limit follows it: a peak typed for
            # a machine would quietly cap a file meant to have none)
            nolim = "lossless" in (self.profwas, self.profnow)
            for k, v in new.items():
                cur = str(self.vars[k].get()).strip()
                if v != old.get(k) and (nolim or cur in ("", old.get(k))):
                    self.vars[k].set(v)
        else:
            for k, v in new.items():
                self.vars[k].set(v)
        self.imply_disk()

    def apply_layer(self):
        """The layer's profile changed by hand: its disk is filled into
        Layer disk (layer_disk_of), which is what the encode would use for an
        empty one anyway. A number typed there by hand is replaced, as the
        base's Disk is when the profile changes; clearing the profile clears
        the field only while it still holds what a profile filled, since a
        Layer disk alone is a layer on THIS machine with a faster disk"""
        ld = self.vars.get("layer_disk")
        if ld is None:
            return
        new = layer_disk_of(self.vars["layer_profile"].get())
        if new:
            ld.set(new)
        elif ld.get().strip() == self.layerfill:
            ld.set("")
        self.layerfill = new

    def apply_audio(self):
        """The sound changed by hand: the SPEAKER plays at 5,512 Hz
        (os88venc.SPK_RATE), so choosing it sets the rate to that, and
        leaving it puts back what the profile implies"""
        rate = self.vars["rate"]
        if self.vars["audio"].get() == "speaker":
            rate.set(str(V.SPK_RATE))
        elif rate.get() == str(V.SPK_RATE):
            vals = {k: v.get() for k, v in self.vars.items()}
            rate.set(implied_values(vals, self.sfps)["rate"])

    def refill(self, dest):
        """A field left EMPTY: put back what the encode will use for it
        (blank_fill), or its word for a blank (WordVar)"""
        v = self.vars.get(dest)
        if v is None or str(v.raw()).strip():
            return
        vals = {k: x.get() for k, x in self.vars.items()}
        v.set(blank_fill(dest, vals, self.sfps))

    # --- the groups
    def group_box(self, parent, name, fs, fw, lw, start):
        """One GROUP (98.2.8.2): its outline and header, and its options a
        row each - label, field, and a "?" or a Browse... beside it"""
        fr = ttk.LabelFrame(parent, padding=(6, 0, 6, 3))
        hdr = ttk.Label(fr, text=name, style="Group.TLabel")
        fr.configure(labelwidget=hdr)
        fr.pack(fill="x", anchor="n", pady=(0, 3))
        fr.columnconfigure(0, minsize=lw)
        fr.columnconfigure(3, weight=1)
        self.gheads[name] = hdr
        # while greyed, the outline and its header say why - the outline on
        # a MOTION, which Tk gives the deepest window under the pointer and
        # so only the outline's own bare parts: an Enter reaches it from a
        # field too, and its tip then stood beside the field's
        gtip = lambda n=name: self.gwhy.get(n) and \
            "Not used for this file. " + self.gwhy[n]
        Tip(fr, gtip, on="<Motion>")
        Tip(hdr, gtip)
        for r, f in enumerate(fs):
            p = fr
            lab = ttk.Label(p, text=f["label"])
            lab.grid(row=r, column=0, sticky="w")
            if f["kind"] == "bool":
                v = tk.StringVar(value="")
                w = ttk.Checkbutton(p, variable=v, onvalue="1", offvalue="")
            elif f["kind"] == "choice" or f["choices"]:
                v = WordVar(f["dest"], start[f["dest"]])
                w = ttk.Combobox(p, textvariable=v, values=dropdown(f),
                                 width=fw)
            elif f["dest"] in SAVE_FILE:
                # A FILE IT WRITES: the field and its Browse... share the
                # field's and the "?"'s cells, as wide as the rows above -
                # in a column of their own a Browse..., being wider than a
                # "?", ran the Sound tab's right-hand column off the pane
                v = WordVar(f["dest"], start[f["dest"]])
                cell = ttk.Frame(p)
                cell.grid(row=r, column=1, columnspan=2, sticky="w",
                          padx=(4, 0), pady=(0, 1))
                w = ttk.Entry(cell, textvariable=v, width=fw - 2)
                w.pack(side="left")
            else:
                v = WordVar(f["dest"], start[f["dest"]])
                w = ttk.Entry(p, textvariable=v, width=fw + 2)
            if f["kind"] != "bool":     # EMPTIED, it shows what it means
                w.bind("<FocusOut>", lambda e, d=f["dest"]: self.refill(d),
                       add="+")
            if f["dest"] not in SAVE_FILE:
                w.grid(row=r, column=1, sticky="w", padx=4, pady=(0, 1))
            if f["dest"] in IMPLYING:
                w.bind("<<ComboboxSelected>>",
                       lambda e, d=f["dest"]: self.apply_implied(d))
            elif f["dest"] == "audio":
                w.bind("<<ComboboxSelected>>",
                       lambda e: self.apply_audio())
            elif f["dest"] == "layer_profile":
                w.bind("<<ComboboxSelected>>",
                       lambda e: self.apply_layer())
            elif f["dest"] == "spk_style":
                w.bind("<<ComboboxSelected>>",
                       lambda e: self.apply_style())
            # a field says why it is greyed only when it is greyed ALONE,
            # in a group that applies (FIELD_WHEN): a greyed group says it
            # once, on its header and outline, and its fields keep their
            # own help
            ftip = lambda d=f["dest"], t=f["tip"], n=name: \
                "Not used for this file. %s\n\n%s" % (self.why[d], t) \
                if self.why.get(d) and not self.gwhy.get(n) else t
            Tip(lab, ftip)
            Tip(w, ftip)
            ws = [lab, w]
            if f["dest"] in SAVE_FILE:  # A FILE IT WRITES: chosen, not typed
                bb = ttk.Button(w.master, text="Browse...", width=0,
                                style="Tool.TButton", command=lambda f=f, v=v:
                                self.browse_save(f, v))
                bb.pack(side="left", padx=(4, 0))
                Tip(bb, "Choose where %s is written"
                    % SAVE_FILE[f["dest"]][0].lower())
                ws.append(bb)
            if f["help"]:               # WHAT EACH CHOICE IS, a click away
                hb = ttk.Button(p, text="?", width=2, style="Tool.TButton",
                                command=lambda f=f, v=v:
                                self.choices_help(f, v))
                hb.grid(row=r, column=2, sticky="w")
                Tip(hb, "What each choice of %s is - click one to take it"
                    % f["label"])
                ws.append(hb)
            self.vars[f["dest"]] = v
            self.fwidgets[f["dest"]] = ws

    def ladder_page(self, page, start):
        """THE DEGRADE LADDER's tab: a checkbox that turns it on, and
        a rung a row - each a list of the rungs this file can use - which
        compose --ladder. Its variable is the option's, so a file loaded
        sets the rungs, and the rungs set it"""
        v = WordVar("ladder", start.get("ladder", ""))
        self.vars["ladder"] = v
        self.ladon = tk.StringVar(value="1" if v.get() else "")
        self.ladsync = False
        # (A, in a box of its own whose title is the switch - the
        # settings it replaces are B, beside it below: the owner's "ladder
        # OR these other settings")
        top = ttk.Checkbutton(page, text="Use the degrade ladder",
                              variable=self.ladon, onvalue="1", offvalue="",
                              command=self.ladder_toggle,
                              style="GroupCheck.TCheckbutton")
        box = ttk.LabelFrame(page, labelwidget=top, padding=(6, 0, 6, 4))
        box.grid(row=0, column=0, columnspan=2, sticky="we")
        Tip(top, "Where the budgets would cut a stretch of frames, try "
                 "these in order, each on top of the ones before - the "
                 "first that draws the stretch whole is taken, else the "
                 "least broken - and only then cut. A Cut rung stops there "
                 "instead, where no frame is more broken than it says: for "
                 "a video that takes a cut better than a slower rate. It "
                 "REPLACES Lag, Min fps, Coarse cut, Step down and Step min: "
                 "only what a rung names is done.")
        self.ladrows = []
        for i in range(LADDER_RUNGS):
            lab = ttk.Label(box, text="Rung %d" % (i + 1))
            lab.grid(row=i, column=0, sticky="w", pady=(2, 0))
            cb = ttk.Combobox(box, state="readonly", width=56)
            cb.grid(row=i, column=1, sticky="w", padx=4, pady=(2, 0))
            cb.bind("<<ComboboxSelected>>", lambda e: self.ladder_compose())
            self.ladrows.append((lab, cb))
        # THE LADDER BY TIME (--ladder-at, 98.2.1.10): other rungs for a
        # stretch of the clip, typed in --ladder's words - the window's
        # slider gives the times
        i = LADDER_RUNGS
        lab = ttk.Label(box, text="By time")
        lab.grid(row=i, column=0, sticky="w", pady=(6, 0))
        av = WordVar("ladder_at", start.get("ladder_at", ""))
        ent = ttk.Entry(box, textvariable=av, width=58)
        ent.grid(row=i, column=1, sticky="w", padx=4, pady=(6, 0))
        tip = [f["tip"] for f in fields() if f["dest"] == "ladder_at"]
        for w in (lab, ent):
            Tip(w, (tip[0] if tip else "") + " The times are the slider's, "
                "as the preview shows them.")
        self.vars["ladder_at"] = av
        self.fwidgets["ladder_at"] = [lab, ent]
        ttk.Label(page, text="The ladder OR the settings for the whole clip, "
                  "not both: with the ladder on, only what a rung names is "
                  "done, and a setting with no rung for it is off - which "
                  "is how a ladder leaves one out. After the last rung the "
                  "frames are cut (When a frame is cut, on Trade-offs).",
                  foreground="#555",
                  wraplength=560, justify="left").grid(
            row=1, column=0, columnspan=2, sticky="w", pady=(4, 0))
        v.trace_add("write", lambda *a: self.ladder_show())
        self.ladder_show()

    def ladder_toggle(self):
        if self.ladon.get():
            self.vars["ladder"].set(self.vars["ladder"].get() or
                                    V.DEGRADE_DEFAULT)
        else:
            self.vars["ladder"].set("")

    def ladder_compose(self):
        """The rungs as chosen -> --ladder (a "(none)" left out)"""
        if self.ladsync:
            return
        toks = [self.ladtok.get(cb.get()) for _, cb in self.ladrows]
        self.vars["ladder"].set(",".join(t for t in toks if t) if
                                self.ladon.get() else "")

    def ladder_show(self):
        """--ladder -> the checkbox and the rungs, each list the rungs this
        file can use - and a rung it cannot, kept and marked, so a change
        of format leaves what was chosen where it can be seen"""
        if self.ladsync:
            return
        self.ladsync = True
        try:
            vals = {k: x.get() for k, x in self.vars.items()}
            vals["_out"] = self.out.get() if hasattr(self, "out") else ""
            opts = ladder_options(form_context(vals))
            spec = self.vars["ladder"].get()
            toks = [t.strip() for t in spec.split(",") if t.strip()]
            toks = ["coarsescene:" + t[7:] if t.startswith("coarse:") else t
                    for t in toks]      # (the scene rung's first name)
            toks = [t[:-5] + ":frame" if t.startswith("coarse") and
                    t.endswith(":auto") else t for t in toks]   # (and auto)
            self.ladon.set("1" if toks else "")
            self.ladtok = {"(none)": ""}
            for lab, tok in opts:
                self.ladtok[lab] = tok
            have = {tok: lab for lab, tok in opts}
            for i, (lab, cb) in enumerate(self.ladrows):
                tok = toks[i] if i < len(toks) else ""
                names = ["(none)"] + [lb for lb, _ in opts]
                cur = have.get(tok, "(none)")
                if tok and tok not in have:
                    cur = ladder_label(tok) + " - not for this file"
                    self.ladtok[cur] = tok
                    names.append(cur)
                cb.configure(values=names)
                cb.set(cur)
                st = "!disabled" if toks else "disabled"
                cb.state([st])
                lab.state([st])
        finally:
            self.ladsync = False

    def avg_list(self):
        """--avg's dropdown: AUTO, the default, and the fixed share the
        chosen profile carried before it was (os88venc.profile_avg) - so the
        old behaviour is one pick away, under the machine it belongs to"""
        ws = self.fwidgets.get("avg")
        if not ws:
            return
        prof = self.vars["profile"].get().strip() or "5150-st225"
        live = self.vars["live"].get().strip() if "live" in self.vars \
            else ""
        try:
            n = V.profile_avg(prof, live or None)
        except KeyError:
            n = ""
        ws[1].configure(values=avg_choices(n))

    def groups_dirty(self):
        """A field a group's rule reads changed: judge them again once,
        when Tk is idle - a target sets a dozen fields at a time"""
        if not self.grppend:
            self.grppend = True
            self.root.after_idle(self.update_groups)

    def update_groups(self):
        """Every group greyed that cannot apply to the file the form makes,
        and every option in it, and the rest put back (98.2.8.2)"""
        self.grppend = False
        vals = {k: v.get() for k, v in self.vars.items()}
        vals["_out"] = self.out.get()   # (a .WAV: 86.21.1)
        self.gwhy, self.why = group_state(vals)
        for name, hdr in self.gheads.items():
            hdr.state(["disabled" if self.gwhy.get(name) else "!disabled"])
        for d, ws in self.fwidgets.items():
            for w in ws:
                w.state(["disabled" if self.why.get(d) else "!disabled"])
        if hasattr(self, "ladrows"):    # (the rungs this file can use)
            self.ladder_show()
        c = form_context(vals)          # (each list, what this file takes)
        for d, rule in FIELD_CHOICES.items():
            cb = [w for w in self.fwidgets.get(d, ())
                  if isinstance(w, ttk.Combobox)]
            if not cb:
                continue
            want = rule(c)
            full = dropdown([f for f in fields() if f["dest"] == d][0])
            cb[0].configure(values=full if want is None else
                            [x for x in want if x in full or x in want])

    def apply_style(self):
        """The speaker's style changed by hand: its high-pass, ratio and
        range are shown as the encode will use them (98.2.15.1)"""
        for k, v in style_values(self.vars["spk_style"].get()).items():
            self.vars[k].set(v)

    def browse_v88(self):
        if self.busy:
            return
        p = filedialog.askopenfilename(title="A .V88",
                                       filetypes=[("os8088 video", "*.V88"),
                                                  ("all files", "*")])
        if p:
            self.load_v88(p)

    def load_v88(self, p):
        job = self.begin()
        self.write("Loading %s...\n" % p)
        # ...and the form set to how it was made AT ONCE, off the header and
        # the options block alone (98.2.17): a long file's preview is
        # minutes of decoding, and a person who wants only its settings
        # should not wait on it. Set now, the form is the person's while
        # the preview loads - the preview's end does not set it again
        self.autotitle = None           # (its title is the file's: kept)
        try:
            self.apply_file_options(vid.HeadOptions(p), p)
        except (OSError, vid.V88Error) as e:
            self.write("Its options do not read (%s): the form is as it "
                       "was.\n" % e)
        threading.Thread(target=self._load, args=(p, job, self.cancel),
                         daemon=True).start()

    def _load(self, path, job, cancel):
        try:
            self.put(job, "done", (path,) + self._preview(path, job, cancel))
        except V.Cancelled:
            pass
        except Exception as e:
            self.put(job, "fail", str(e))

    def stop(self):
        """CANCEL, at once (98.2.11.1): the window is ready again NOW, the
        job's process killed and anything it says after ignored"""
        if not self.busy:
            return
        self.cancel.set()
        step = self.prog[0] if self.prog else ""
        if self.ejob is not None:
            self.ejob.cancel()
            self.ejob.leftover(self.out.get())
            self.ejob = None
        self.job += 1
        text = "Cancelled; nothing was written." if step not in (
            "disk", "preview") else "Cancelled: the file was written, its " \
            "preview was not loaded."
        self.write("\n%s\n" % text)
        self.finish(text, 0)
        self.show_frame(self.scrub.get())

    def set_poster(self):
        if self.busy or not self.cur or self.key is None:
            return
        try:
            vid.set_poster(self.cur, self.reader.keys[self.key][0])
            self.reader = vid.Reader(self.cur)
            self.ffrm = None
        except Exception as e:
            messagebox.showerror(APPNAME, "The poster was not changed: %s"
                                 % e)
            return
        self.write("The poster is now keyframe %d (frame %d), in %s.\n"
                   % (self.reader.poster,
                      self.reader.keys[self.reader.poster][0], self.cur))
        self.show_frame(self.scrub.get())

    def set_title(self):
        if self.busy or not self.cur:
            return
        try:
            t = vid.set_title(self.cur, self.titlev.get())
            self.reader = vid.Reader(self.cur)
            self.ffrm = None
        except Exception as e:
            messagebox.showerror(APPNAME, "The title was not changed: %s"
                                 % e)
            return
        self.titlev.set(t)
        self.write("The title is now '%s', in %s.\n" % (t, self.cur))
        self.show_frame(self.scrub.get())

    def title_state(self):
        """The title field follows the file in the preview: filled from its
        header when one is loaded, and closed while a job runs - and Redo
        posters with it"""
        st = "normal" if self.cur and not self.busy else "disabled"
        self.titlee.config(state=st)
        self.titlebtn.config(state=st)
        self.thumbbtn.config(state=st)

    def redo_posters(self):
        """--posters-only on the file in the preview (98.1.3.11.1), on a
        thread: the posters the form's four fields say, nothing encoded"""
        if self.busy or not self.cur:
            return
        import argparse
        g = {k: self.vars[k].get() if k in self.vars else None
             for k in ("desktop_posters", "desktop_size", "desktop_clip",
                       "poster_image")}
        try:
            clip = float(g["desktop_clip"] or 0)
        except ValueError:
            messagebox.showerror(APPNAME, "The desktop clip is not a number: "
                                 "%s" % g["desktop_clip"])
            return
        a = argparse.Namespace(src=self.cur, out=self.cur,
                               desktop_posters=g["desktop_posters"] or "auto",
                               desktop_size=g["desktop_size"] or "fit",
                               desktop_clip=clip,
                               poster_image=(g["poster_image"] or "").strip()
                               or None)
        self.write("\nos88venc --posters-only %s %s --desktop-posters %s "
                   "--desktop-size %s --desktop-clip %g%s\n" % (
                       self.cur, self.cur, a.desktop_posters, a.desktop_size,
                       clip, " --poster-image %s" % a.poster_image
                       if a.poster_image else ""))
        job = self.begin()

        def run():
            try:
                n = V.reposter(a, say=lambda t: self.put(job, "log",
                                                         t + "\n"))
                self.put(job, "posters", n)
            except Exception as e:
                self.put(job, "fail", "The desktop posters were not changed: "
                         "%s" % e)
        threading.Thread(target=run, daemon=True).start()

    def argv(self):
        return argv_from(self.src.get(), self.out.get(),
                         {k: v.get() for k, v in self.vars.items()},
                         self.sfps)

    # --- the encode, on a thread; its prints are the log
    def encode(self):
        if self.busy:
            return
        if not self.src.get() or not self.out.get():
            messagebox.showinfo(APPNAME, "Choose a video and where to save "
                                         "it first.")
            return
        argv = self.argv()
        self.log.delete("1.0", "end")
        self.write("os88venc " + " ".join(argv[2:]) + "\n")
        job = self.begin()
        # (Tk's variables are read HERE: the thread may not touch them -
        # "main thread is not in main loop", found by driving the window)
        disk = DISK_LABELS.index(self.disksize.get()) \
            if self.mkdisk.get() else None
        try:
            self.ejob = EncodeJob(argv).start()
        except Exception as e:
            self.write("\nFAILED: the encoder did not start: %s\n" % e)
            self.finish("Failed.", 0)
            return
        threading.Thread(target=self._run, args=(
            self.ejob, self.out.get(), disk, job, self.cancel),
            daemon=True).start()

    def begin(self):
        """A job starts: its number, and a cancel of its own - a job
        cancelled and still winding down keeps ITS flag set"""
        self.job += 1
        self.busy = True
        self.cancel = threading.Event()
        self.prog = ("prepare", 0, 0)
        self.lanes = {}
        self.gobtn.config(state="disabled")
        self.posterbtn.config(state="disabled")
        self.title_state()
        self.stopbtn.config(state="normal")
        return self.job

    def put(self, job, kind, val):
        self.q.put((kind, val, job))

    def _preview(self, path, job, cancel):
        def tick(d, t):
            if job == self.job:
                self.prog = ("preview", d, t)
            if cancel.is_set():
                raise V.Cancelled()
        return preview_frames(path, tick=tick)

    def _run(self, ej, out, disk, job, cancel):
        """The encode's process read to its end, on a thread: its lines
        are the log and its progress the bar. A killed one says nothing"""
        tail = []
        try:
            rc = None
            for kind, val in ej.events():
                if kind == "prog":
                    if job == self.job:
                        lane_put(self.lanes, val, time.time())
                        if val.get("lane", "main") == "main" and \
                                val["step"] != "pass":
                            self.prog = (val["step"], val["done"],
                                         val["total"])
                elif kind == "log":
                    self.put(job, "log", val)
                    tail = (tail + [val])[-6:]
                else:
                    rc = val
            if ej.killed or cancel.is_set():
                return
            if rc:
                self.put(job, "fail", "".join(tail).strip() or
                         "the encoder stopped with %d" % rc)
                return
            if out.lower().endswith(".wav"):  # the sound alone (86.21.1):
                self.put(job, "wavdone", out)   # no frames to preview and
                return                          # no video for a disk
            if disk is not None:
                if job == self.job:
                    self.prog = ("disk", 0, 0)
                import tempfile
                with tempfile.TemporaryDirectory() as stage:
                    cmd, img = disk_argv(out, disk, stage=stage)
                    p = subprocess.run(cmd, capture_output=True, text=True)
                self.put(job, "log", (p.stdout + p.stderr) or
                         "the disk: %s\n" % img)
                if DISKS[disk][1] == "hd" and "--noboot" in cmd:
                    self.put(job, "log", HD_NOBOOT_NOTE + "\n")
                if p.returncode:
                    self.put(job, "fail", "the disk was not made: %s" %
                             (p.stderr.strip() or p.returncode))
                    return
            self.put(job, "done", (out,) + self._preview(out, job, cancel))
        except V.Cancelled:
            pass
        except Exception as e:
            self.put(job, "fail", "%s\n%s" % (e, traceback.format_exc()))

    def step_frame(self, d):
        """The preview a frame back (d -1) or on (+1): the slider moved, which
        shows it - a preview of a long file holds every n-th frame
        (preview_frames), so a step is one of those"""
        if not getattr(self, "frames", None):
            return
        n = max(0, min(len(self.frames) - 1, int(self.scrub.get()) + d))
        self.scrub.set(n)               # (its command shows it)

    def write(self, text):
        self.log.insert("end", text)
        self.log.see("end")

    def finish(self, text, frac):
        self.busy = False
        self.prog = None
        self.ejob = None
        self.gobtn.config(state="normal")
        self.stopbtn.config(state="disabled")
        self.bar.stop()
        self.bar.config(mode="determinate", value=1000 * frac)
        self.status.set(text)
        self.title_state()

    def _pump(self):
        prog = self.prog
        if self.busy and prog:
            if prog[0] in ("disk", "preview"):  # (the window's own steps)
                lane_put(self.lanes, dict(lane="main", step=prog[0],
                                          done=prog[1], total=prog[2]),
                         time.time())
            frac = bar_of(*prog)
            if frac is None:            # (a step with no count: working)
                if str(self.bar.cget("mode")) != "indeterminate":
                    self.bar.config(mode="indeterminate")
                    self.bar.start(40)
            else:
                if str(self.bar.cget("mode")) != "determinate":
                    self.bar.stop()
                    self.bar.config(mode="determinate")
                self.bar.config(value=1000 * frac)
            self.status.set(lanes_text(self.lanes, time.time()))
        try:
            while True:
                kind, val, job = self.q.get_nowait()
                if kind != "drop" and job != self.job:
                    continue            # a cancelled job's, winding down
                if kind == "log":
                    self.write(val)
                elif kind == "drop":
                    self.dropped(val)
                    continue
                elif kind == "wavdone":
                    self.finish("Done.", 1)
                    self.write("\n%s: the sound for Audio on the PC "
                               "speaker (SPEC.md 86.21.1). Copy it to the "
                               "machine and open it; on a Sound Blaster it "
                               "plays too.\n" % os.path.basename(val))
                elif kind == "posters":
                    self.reader = vid.Reader(self.cur)
                    self.ffrm = None
                    self.kshown = self.pshown = None
                    self.finish("Done.", 1)
                    self.write("%d desktop poster(s) written into %s.\n"
                               % (val, os.path.basename(self.cur)))
                    self.show_frame(self.scrub.get())
                elif kind == "fail":
                    cancelled = val.startswith("Cancelled")
                    self.write("\n%s\n" % (val if cancelled else
                                            "FAILED: %s" % val))
                    self.finish(val.split("\n")[0] if cancelled else
                                "Failed.", 0)
                    self.show_frame(self.scrub.get())
                else:
                    self.cur, self.reader, self.frames = val
                    self.titlev.set(self.reader.title)
                    self.kshown = self.pshown = None
                    self.scrub.config(to=max(0, len(self.frames) - 1))
                    self.scrub.set(0)
                    self.finish("Done.", 1)
                    self.show_frame(0)
                    self.pal_dirty()    # ...and its palette, if chosen
                    self.write("\n%s. Drag the slider to see every frame as "
                               "the screen will.\n" % os.path.basename(
                                   self.cur))
        except queue.Empty:
            pass
        self.root.after(100, self._pump)

    def show_frame(self, i):
        if not self.frames:
            return
        f, img = self.frames[int(i)]
        w, h = img.size
        k = min(400.0 / w, 300.0 / h)
        img = img.resize((max(1, int(w * k)), max(1, int(h * k))))
        buf = io.BytesIO()
        img.save(buf, "PNG")
        self.photo = tk.PhotoImage(data=base64.b64encode(buf.getvalue()))
        self.canvas.delete("all")
        self.canvas.create_image(200, 150, image=self.photo)
        self.fr.set("frame %d of %d (%.2f s)" % (
            f + 1, self.reader.frames, f / self.reader.fps))
        self.show_key(f)
        self.draw_fpal(f)           # (a palette a scene: this frame's)

    def show_key(self, f):
        """The panel: the file's facts, and the keyframe a play from frame
        `f` starts at - the one Set as poster would make the poster"""
        r = self.reader
        self.facts.set("\n".join(file_facts(r, self.cur)))
        i = vid.key_at(r, f)
        if i is None:
            self.key = None
            self.kcanvas.delete("all")
            self.ktext.set("No keyframes.")
            self.posterbtn.config(state="disabled")
            self.show_poster()
            return
        k = r.keys[i][0]
        if (self.cur, i) != getattr(self, "kshown", None):
            # a keyframe is decoded once as the scrubber crosses into it,
            # not on every step of the drag
            self.kphoto = thumb(key_view(r, f)[2])
            self.kcanvas.delete("all")
            self.kcanvas.create_image(80, 60, image=self.kphoto)
            self.kshown = (self.cur, i)
        self.key = i
        self.show_poster()
        # (a key in PARTS, 98.1.3.8, is a read a part: only a part past
        # the player's one read leaves it no poster - the whole key's
        # bytes said so of every big key in parts, which shows fine)
        n = max(vid.key_reads(r.key(i)[1]))
        self.ktext.set(
            "Keyframe %d (of 0 to %d)\nframe %d (%.2f s)%s%s" % (
                i, r.nkeys - 1, k, k / r.fps,
                "\n= the poster" if i == r.poster else "",
                "\n%d bytes in one read: past the %d the player reads, so "
                "it would show no poster" % (n, V.KEY_PLAYER)
                if n > V.KEY_PLAYER else ""))
        self.posterbtn.config(state="disabled" if self.busy or
                              i == r.poster else "normal")

    def apply_file_options(self, r, path):
        """A .V88 LOADED (98.2.17): every field set to the option it was
        made with, the target it was made for, and what could not be
        carried over said in the log - or, for a file made before the
        options were stored, the form left as it was, and that said. `r`
        is a Reader or os88vid's HeadOptions: the header alone will do"""
        name = os.path.basename(path)
        try:
            got = form_from_file(r)
        except vid.V88Error as e:
            self.write("\nIts options block does not read (%s): the form is "
                       "as it was.\n" % e)
            return
        if got is None:
            self.write("\n%s was made before the encoder stored its "
                       "options in the file: the form is as it was.\n"
                       % name)
            return
        vals, ti, notes, src = got
        for k, v in vals.items():
            if k in self.vars:
                self.vars[k].set(v)
        self.target.set(TARGETS[ti][0] if ti is not None else "")
        self.update_groups()
        self.info.set("How %s was made%s%s" % (
            name, ", from %s" % src if src else "",
            "" if ti is not None else " - none of the Made for targets"))
        self.write("\nThe form is set to the options %s was made with%s. "
                   "Choose %s as the Video to make it again.\n"
                   % (name, " (from %s)" % src if src else "",
                      src or "its source"))
        for n in notes:
            self.write("   %s\n" % n)
        doc = r.options() or {}
        came = ["--%s auto (it came to %s)" % (
            d.replace("_", "-"), "%g" % o if isinstance(o, (int, float))
            else o) for d in V.AUTO_OPTS if vals.get(d) == "auto"
            for o in [(doc.get("o") or {}).get(d)] if o is not None]
        if came:
            self.write("   %s\n" % "; ".join(came))

    def show_poster(self):
        """The poster panel: the file's poster keyframe, drawn once per
        file and poster - a scrub does not redraw it"""
        r = self.reader
        mark = (self.cur, r.poster)
        if mark == getattr(self, "pshown", None):
            return
        self.pshown = mark
        self.pcanvas.delete("all")
        pv = poster_view(r)
        if pv is None:
            self.ptext.set("None: the player shows its panel alone.")
            return
        i, k, img = pv
        self.pphoto = thumb(img)
        self.pcanvas.create_image(80, 60, image=self.pphoto)
        self.ptext.set("Keyframe %d\nframe %d (%.2f s)" % (i, k, k / r.fps))


def main():
    if tk is None:
        sys.exit("os88vencgui: this Python has no tkinter")
    root = make_root()                  # drag and drop, where installed
    app = App(root)
    # the drop only QUEUES: the pump takes it, as it takes the encode's log
    how = enable_drop(root, lambda paths: app.q.put(("drop", paths, None)))
    app.info.set("Choose a video%s." % (
        ", or drop one on this window" if how else ""))
    root.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
