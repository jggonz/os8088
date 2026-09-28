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
                                     monitor model, one bit as one bit
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
import traceback

try:
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
except Exception:                                            # pragma: no cover
    tk = None
    filedialog = messagebox = ttk = None

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(_HERE)
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)
import os88venc as V                                          # noqa: E402
import os88vid as vid                                         # noqa: E402

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
    ("IBM 5150/XT, a floppy - small and slow", "cga-small", "mono",
     "floppy"),
    ("286, VGA - 16 colours in the window", "vga4", "vga4", "286-vga"),
    ("286, VGA - 256 colours, full screen", "vga8", "vga8", "286-vga"),
    ("286, VGA - Mode X, 256 colours, square pixels", "modex", None,
     "286-vga"),
    ("Live on a CGA desktop - short, black and white, read whole", None,
     None, "5150-st225", {"live": "cga"}),
    ("Live on a Hercules desktop - short, black and white", None, None,
     "5150-st225", {"live": "herc"}),
    ("Live on a VGA desktop - short, black and white", None, None,
     "286-vga", {"live": "vga"}),
]
TARGETS = [t + ({},) if len(t) == 4 else t for t in TARGETS]

# the tabs, and which options go on which - an option named nowhere here
# still appears, on Advanced
TAB_ROWS = 10    # more options than this and a tab takes two columns
TABS = ("Basic", "Picture", "Colour", "Sound", "Budget", "Loop and keys",
        "Advanced")
TAB_OF = {
    "preset": "Basic", "pixfmt": "Basic", "profile": "Basic",
    "title": "Basic", "credits": "Basic", "start": "Basic", "end": "Basic",
    "fps": "Basic",
    "layout": "Picture", "box": "Picture", "fit": "Picture",
    "dither": "Picture", "stable": "Picture", "levels": "Picture",
    "clip": "Picture", "gamma": "Picture", "contrast": "Picture",
    "brightness": "Picture", "invert": "Picture", "detail": "Picture",
    "cga_palette": "Colour", "cga_bright": "Colour", "cga_bg": "Colour",
    "vga8_dither": "Colour", "vga8_stable": "Colour",
    "vga4_stable": "Colour", "comp_dither": "Colour",
    "comp_stable": "Colour", "comp_quick": "Colour", "mix": "Colour",
    "cga_card": "Colour", "c512_dither": "Colour", "c512_stable": "Colour",
    "c512_mix": "Colour",
    "text_colour": "Colour", "text_glyphs": "Picture",
    "text_detail": "Picture", "text_sharpen": "Picture",
    "text_busy": "Picture", "text_stable": "Picture",
    "text_prefer_colour": "Colour",
    "text_ocr": "Picture", "text_ocr_large": "Picture",
    "text_ocr_conf": "Picture", "text_ocr_every": "Picture",
    "levels_mix": "Colour", "flip": "Colour",
    "audio": "Sound", "rate": "Sound", "adpcm": "Sound", "jobs": "Sound",
    "volume": "Sound", "spk_shape": "Sound", "spk_highpass": "Sound",
    "spk_drive": "Sound", "spk_lows": "Sound", "spk_pulses": "Sound",
    "spk_range": "Sound", "spk_ratio": "Sound",
    "spk_preview": "Sound",
    "disk": "Budget", "avg": "Budget", "peak": "Budget", "owe": "Budget",
    "lookahead": "Budget", "error": "Budget", "reserve": "Budget",
    "aim": "Basic", "worth": "Budget",
    "loop_from": "Loop and keys", "repeat": "Loop and keys",
    "keysecs": "Loop and keys", "poster": "Loop and keys",
    "poster_at": "Loop and keys", "resident": "Loop and keys",
    "live": "Loop and keys", "xms": "Loop and keys",
}
# the choices that IMPLY others (os88venc.implied): changing one refills them
IMPLYING = ("preset", "pixfmt", "profile", "live")
# a free-text option's COMMON values, offered in an editable list: the sound
# rate's 5,512 Hz halves the sound's bytes, which is half a Live clip's
# memory (98.1.7.2) - any other rate can still be typed. Owed time's 0 is
# OFF, the fixed per-frame ceiling (98.2.1.1)
SUGGEST = {"rate": ["", "22050", "11025", "5512"],
           "owe": ["", "0", "1.6"]}
# what the window runs itself, and so does not offer
HIDDEN = {"help", "src", "out", "preview_png", "quiet", "profiles",
          "progress"}


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
            tab=TAB_OF.get(act.dest, "Advanced")))
    return out


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
    imp = implied_values(values, sfps)
    # ...but a choice that IMPLIES is compared with what the rest imply
    # without it - or a --pixfmt cgacomp would echo back as its own
    # implication and be left off, and the file come out one-bit
    imp["pixfmt"] = implied_values(dict(values, pixfmt=""), sfps)["pixfmt"]
    for f in fields():
        v = str(values.get(f["dest"], "")).strip()
        if f["kind"] == "bool":
            if v in ("1", "True", "true") and imp.get(f["dest"]) != "1":
                argv.append(f["flag"])
            continue
        if v == "" or v == f["default"] or v == imp.get(f["dest"]):
            continue
        argv += [f["flag"], v]
    return argv


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


def render(r, surf):
    """Frame `surf` of Reader `r` as an RGB PIL image, at the proportions
    the screen shows it (the file's pixel aspect, 98.1.1)"""
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
        pal = _rgb16() if pf == vid.PF_VGA4 else \
            np.frombuffer(r.palette, np.uint8).astype(np.uint16).reshape(
                -1, 3) * 255 // 63
        rgb = pal[cv].astype(np.uint8)
    if r.rowscale > 1:
        rgb = np.repeat(rgb, r.rowscale, axis=0)
    img = Image.fromarray(np.ascontiguousarray(rgb))
    an, ad = r.aspect
    w, h = img.size
    return img.resize((max(1, round(w * an / ad)), h), Image.NEAREST)


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
            out.append((f, render(r, surf)))
    return r, out


# THE PROGRESS (98.2.11): os88venc's steps, each a share of one bar, and
# then the window's own - the floppy and reading the file back to preview
STEP_TEXT = {"prepare": "Getting ready", "read": "Reading and dithering",
             "sound": "Encoding the sound", "encode": "Encoding",
             "write": "Writing the file", "disk": "Making the disk",
             "preview": "Loading the preview"}
ENCODE_SHARE = 0.9                  # the rest is the disk and the preview


def overall(step, done, total):
    """How far along the whole job is, 0.0 to 1.0"""
    if step == "disk":
        return ENCODE_SHARE
    if step == "preview":
        return ENCODE_SHARE + (1 - ENCODE_SHARE) * (
            min(done, total) / total if total else 0)
    at = 0.0
    for name, share in V.STEPS:
        if name == step:
            part = min(done, total) / total if total else 0.0
            return (at + share * part) * ENCODE_SHARE
        at += share
    return 0.0


def step_text(step, done, total):
    t = STEP_TEXT.get(step, step)
    if total:
        t += ": frame %d of %d" % (min(done, total), total)
    return t + "..."


def file_facts(r, path=None):
    """What the file IS, a line a fact: the panel under the preview"""
    g = r.g
    secs = r.frames / r.fps
    w = g.wb * (4 if r.pixfmt == vid.PF_CGA4 else vid.PIX_PER_BYTE[g.layout])
    if r.pixfmt in (vid.PF_C512, vid.PF_TEXT):
        w = g.wb // 2                   # a cell is two bytes
    out = ["'%s'" % r.title + (" - %s" % r.credits if r.credits else "")]
    out.append("%s on %s, %d x %d%s" % (
        vid.PF_NAMES[r.pixfmt], g.name, w, g.h,
        ", each row shown twice" if r.rowscale > 1 else ""))
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
    out.append("%s bytes = %.1f KB/s%s" % (
        "{:,}".format(size), size / 1024.0 / secs,
        ", resident, %d rendition%s" % (r.nrend, "s" if r.nrend > 1 else "")
        if r.resident else ""))
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
    k, rec, spo, spn, idx = r.key(i)
    surf = r.g.surface()
    r.apply(surf, rec, key=True)
    return i, k, render(r, surf)


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
                   "286": 6, "286-vga": 6, "lossless": 6}
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


def out_for(src):
    """The .V88 a source is saved as by default: beside it, its own name
    cut to 40 characters - the NAME, where the whole path used to be cut,
    which put a long folder's file in its grandparent under half a name"""
    d, n = os.path.split(src)
    return os.path.join(d, os.path.splitext(n)[0][:40] + ".V88")


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


def hook_win_drop(root, got):
    """DRAG AND DROP ON WINDOWS with nothing installed: the shell's own
    WM_DROPFILES, through ctypes - the window's procedure subclassed so the
    message reaches us, every other message passed to the one Tk set.
    `got(paths)` is called ON TK'S THREAD, inside the message; the App only
    queues it. True when the hook is in"""
    import ctypes
    from ctypes import wintypes
    user32, shell32 = ctypes.windll.user32, ctypes.windll.shell32
    LRESULT = ctypes.c_ssize_t
    WNDPROC = ctypes.WINFUNCTYPE(LRESULT, wintypes.HWND, wintypes.UINT,
                                 wintypes.WPARAM, wintypes.LPARAM)
    GWLP_WNDPROC, WM_DROPFILES = -4, 0x0233
    setlong = user32.SetWindowLongPtrW if ctypes.sizeof(
        ctypes.c_void_p) == 8 else user32.SetWindowLongW
    setlong.argtypes = [wintypes.HWND, ctypes.c_int, ctypes.c_void_p]
    setlong.restype = ctypes.c_void_p
    user32.CallWindowProcW.argtypes = [ctypes.c_void_p, wintypes.HWND,
                                       wintypes.UINT, wintypes.WPARAM,
                                       wintypes.LPARAM]
    user32.CallWindowProcW.restype = LRESULT
    shell32.DragAcceptFiles.argtypes = [wintypes.HWND, wintypes.BOOL]
    shell32.DragQueryFileW.argtypes = [ctypes.c_void_p, wintypes.UINT,
                                       ctypes.c_wchar_p, wintypes.UINT]
    shell32.DragQueryFileW.restype = wintypes.UINT
    shell32.DragFinish.argtypes = [ctypes.c_void_p]
    root.update_idletasks()
    # the frame Windows draws round Tk's window: a drop anywhere in it is
    # found by walking up from the child under the pointer
    hwnd = int(root.wm_frame(), 16)
    old = []

    def proc(h, msg, wp, lp):
        if msg == WM_DROPFILES:
            try:
                n = shell32.DragQueryFileW(wp, 0xFFFFFFFF, None, 0)
                paths = []
                for i in range(n):
                    size = shell32.DragQueryFileW(wp, i, None, 0) + 1
                    buf = ctypes.create_unicode_buffer(size)
                    shell32.DragQueryFileW(wp, i, buf, size)
                    paths.append(buf.value)
                got(paths)
            except Exception:
                pass
            finally:
                shell32.DragFinish(wp)
            return 0
        return user32.CallWindowProcW(old[0], h, msg, wp, lp)
    cb = WNDPROC(proc)
    old.append(setlong(hwnd, GWLP_WNDPROC, ctypes.cast(cb, ctypes.c_void_p)))
    if not old[0]:
        return False
    shell32.DragAcceptFiles(hwnd, True)
    root._os88_drop = cb                # ctypes frees a callback nobody holds
    return True


def enable_drop(root, got):
    """Drag and drop, the best way this machine has: tkinterdnd2 where it is
    installed (any platform - `root` must then be its TkinterDnD.Tk), else
    the shell's own on Windows. Returns how, or None"""
    try:
        from tkinterdnd2 import DND_FILES
        root.drop_target_register(DND_FILES)
        root.dnd_bind("<<Drop>>", lambda e: got(
            list(root.tk.splitlist(e.data))))
        return "tkinterdnd2"
    except Exception:
        pass
    if sys.platform == "win32":
        try:
            if hook_win_drop(root, got):
                return "windows"
        except Exception:
            pass
    return None


# --------------------------------------------------------------------------
# the window
# --------------------------------------------------------------------------
class Tip(object):
    """A tooltip: the option's help, shown while the pointer rests on it"""

    def __init__(self, widget, text):
        self.w, self.text, self.top = widget, text, None
        widget.bind("<Enter>", self.show)
        widget.bind("<Leave>", self.hide)

    def show(self, _e=None):
        if not self.text or self.top:
            return
        x = self.w.winfo_rootx() + 16
        y = self.w.winfo_rooty() + self.w.winfo_height() + 4
        self.top = tk.Toplevel(self.w)
        self.top.wm_overrideredirect(True)
        self.top.wm_geometry("+%d+%d" % (x, y))
        tk.Label(self.top, text=self.text, justify="left", wraplength=420,
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
        """("prog", (step, done, total)) and ("log", line) as the encode
        goes, then ("exit", returncode)"""
        for line in self.p.stdout:
            if line.startswith(V.PROGRESS + " "):
                try:
                    st, d, t = line.split()[1:4]
                    yield "prog", (st, int(d), int(t))
                    continue
                except ValueError:
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
                  foreground="#555").grid(row=0, column=0, columnspan=2,
                                          sticky="w", pady=(0, 8))
        bold = ("TkDefaultFont", 10, "bold")

        def take(c):
            v.set(c)
            if f["dest"] in IMPLYING:
                self.apply_implied()
            elif f["dest"] == "audio":
                self.apply_audio()
            top.destroy()
        for i, (c, what, on) in enumerate(choice_lines(f, v.get()), 1):
            b = ttk.Button(fr, text=c, width=14,
                           command=lambda c=c: take(c))
            b.grid(row=i, column=0, sticky="nw", pady=1)
            lab = ttk.Label(fr, text=what, wraplength=420, justify="left")
            if on:
                lab.configure(font=bold)
            lab.grid(row=i, column=1, sticky="w", padx=8, pady=1)
        ttk.Button(fr, text="Close", command=top.destroy).grid(
            row=i + 1, column=1, sticky="e", pady=(8, 0))
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
        self.job = 0                    # the job the window is showing: a
        self.ejob = None                # message from any other is stale
        root.title(APPNAME)
        root.geometry("1080x760")
        self.src = tk.StringVar()
        self.out = tk.StringVar()
        self.target = tk.StringVar(value=TARGETS[0][0])
        self.info = tk.StringVar(value="Choose a video.")
        self.mkdisk = tk.BooleanVar(value=False)
        self.disksize = tk.StringVar(value=DISK_LABELS[0])
        self.diskpicked = False         # ...a person's choice, kept
        self._build()
        self.apply_target()
        root.after(100, self._pump)

    def _build(self):
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
                ("Video", self.src, self.browse_src),
                ("Save as", self.out, self.browse_out))):
            ttk.Label(ess, text=label).grid(row=row, column=0, sticky="w")
            ttk.Entry(ess, textvariable=var, width=60).grid(
                row=row, column=1, sticky="we", padx=4)
            ttk.Button(ess, text="Browse...", command=cmd).grid(
                row=row, column=2)
        ttk.Label(ess, text="Made for").grid(row=2, column=0, sticky="w")
        tg = ttk.Combobox(ess, textvariable=self.target, state="readonly",
                          values=[t[0] for t in TARGETS], width=58)
        tg.grid(row=2, column=1, columnspan=2, sticky="we", padx=4)
        tg.bind("<<ComboboxSelected>>", lambda e: self.apply_target())
        Tip(tg, "The machine and screen it will play on. This sets the "
                "preset, the pixel format and the storage profile below, "
                "and fills in every option they imply - the canvas, the "
                "frame rate, the detail, the budget and the sound - as the "
                "encoder will use them. Change any of them afterwards on "
                "their tabs.")
        ttk.Label(ess, textvariable=self.info, foreground="#555").grid(
            row=3, column=1, columnspan=2, sticky="w", padx=4)
        ess.columnconfigure(1, weight=1)
        # --- make a disk, go, the progress and the log: packed from the
        # BOTTOM and before the tabs, so it is the tabs that give way in a
        # short window and not the log (which the default size cut off
        # entirely - and the Encode button with it, off the row's end)
        self.log = tk.Text(left, height=9, wrap="word")
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
        rows = {t: 0 for t in TABS}
        # a tab of more than TAB_ROWS options is laid out in TWO columns,
        # down the first and then down the second: the Picture tab's 21
        # in one were taller than the window
        per = {t: 0 for t in TABS}
        for f in fields():
            per[f["tab"]] += 1
        half = {t: n if n <= TAB_ROWS else -(-n // 2)
                for t, n in per.items()}
        for f in fields():
            p, i = pages[f["tab"]], rows[f["tab"]]
            rows[f["tab"]] += 1
            r, c0 = i % half[f["tab"]], 4 * (i // half[f["tab"]])
            if c0:
                p.columnconfigure(c0 - 1, minsize=16)
            lab = ttk.Label(p, text=f["label"])
            lab.grid(row=r, column=c0, sticky="w", pady=1)
            if f["kind"] == "bool":
                v = tk.StringVar(value="")
                w = ttk.Checkbutton(p, variable=v, onvalue="1", offvalue="")
            elif f["kind"] == "choice" or f["choices"]:
                v = tk.StringVar(value=f["default"])
                w = ttk.Combobox(p, textvariable=v, values=f["choices"],
                                 width=22)
            else:
                v = tk.StringVar(value=f["default"])
                w = ttk.Entry(p, textvariable=v, width=24)
            w.grid(row=r, column=c0 + 1, sticky="w", padx=4, pady=1)
            if f["dest"] in IMPLYING:
                w.bind("<<ComboboxSelected>>",
                       lambda e: self.apply_implied())
            elif f["dest"] == "audio":
                w.bind("<<ComboboxSelected>>",
                       lambda e: self.apply_audio())
            Tip(lab, f["tip"])
            Tip(w, f["tip"])
            if f["help"]:               # WHAT EACH CHOICE IS, a click away
                hb = ttk.Button(p, text="?", width=2,
                                command=lambda f=f, v=v:
                                self.choices_help(f, v))
                hb.grid(row=r, column=c0 + 2, sticky="w", pady=1)
                Tip(hb, "What each choice of %s is - click one to take it"
                    % f["label"])
            self.vars[f["dest"]] = v
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
        ttk.Label(pr, textvariable=self.status, foreground="#555").pack(
            anchor="w")
        # --- the preview
        hd = ttk.Frame(right)
        hd.pack(fill="x")
        ttk.Label(hd, text="What the screen will show").pack(side="left")
        ob = ttk.Button(hd, text="Open a .V88...", command=self.browse_v88)
        ob.pack(side="right")
        Tip(ob, "Look at a .V88 made before - to scrub it, or to change its "
                "poster or its title.")
        self.canvas = tk.Canvas(right, width=400, height=300,
                                background="black", highlightthickness=0)
        self.canvas.pack()
        self.scrub = tk.Scale(right, from_=0, to=0, orient="horizontal",
                              length=400, command=self.show_frame,
                              showvalue=False)
        self.scrub.pack(fill="x")
        self.fr = tk.StringVar(value="")
        ttk.Label(right, textvariable=self.fr).pack(anchor="w")
        # --- the file, and the keyframe a play from here starts at
        fi = ttk.LabelFrame(right, text="The file", padding=6)
        fi.pack(fill="x", pady=(6, 0))
        self.facts = tk.StringVar(value="Encode a video, or open a .V88.")
        fl = ttk.Label(fi, textvariable=self.facts, justify="left",
                       wraplength=370)
        fl.pack(anchor="w", fill="x")
        fi.bind("<Configure>", lambda e: fl.config(
            wraplength=max(120, e.width - 16)))
        tf = ttk.Frame(fi)              # the title, changed in place
        tf.pack(fill="x", pady=(6, 0))
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
        kf = ttk.Frame(fi)
        kf.pack(fill="x", pady=(6, 0))
        self.kcanvas = tk.Canvas(kf, width=160, height=120,
                                 background="black", highlightthickness=0)
        self.kcanvas.pack(side="left")
        kr = ttk.Frame(kf, padding=(8, 0, 0, 0))
        kr.pack(side="left", fill="both", expand=True)
        self.ktext = tk.StringVar(value="")
        ttk.Label(kr, textvariable=self.ktext, justify="left",
                  wraplength=220).pack(anchor="w")
        self.posterbtn = ttk.Button(kr, text="Set as poster",
                                    command=self.set_poster,
                                    state="disabled")
        self.posterbtn.pack(anchor="w", pady=(6, 0))
        Tip(self.posterbtn, "Make this keyframe the picture the player "
                            "shows before a play. Only the poster changes: "
                            "the file is not encoded again.")

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
            self.autoout = out_for(p)
            self.out.set(self.autoout)
        try:
            text, vals = suggest(p)
        except Exception as e:
            self.info.set("ffprobe could not read it: %s" % e)
            return
        self.info.set(text)
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

    def browse_out(self):
        p = filedialog.asksaveasfilename(defaultextension=".V88",
                                         filetypes=[("os8088 video",
                                                     "*.V88")])
        if p:
            self.out.set(p)

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

    def apply_implied(self):
        """A preset, format, profile or Live choice changed by hand: every
        option it implies is shown as it will be used (98.2.10)"""
        vals = {k: v.get() for k, v in self.vars.items()}
        vals["pixfmt"] = fitting_pixfmt(vals)
        for k, v in implied_values(vals, self.sfps).items():
            self.vars[k].set(v)
        self.imply_disk()

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
        except Exception as e:
            messagebox.showerror(APPNAME, "The title was not changed: %s"
                                 % e)
            return
        self.titlev.set(t)
        self.write("The title is now '%s', in %s.\n" % (t, self.cur))
        self.show_frame(self.scrub.get())

    def title_state(self):
        """The title field follows the file in the preview: filled from its
        header when one is loaded, and closed while a job runs"""
        st = "normal" if self.cur and not self.busy else "disabled"
        self.titlee.config(state=st)
        self.titlebtn.config(state=st)

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
                        self.prog = val
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

    def write(self, text):
        self.log.insert("end", text)
        self.log.see("end")

    def finish(self, text, frac):
        self.busy = False
        self.prog = None
        self.ejob = None
        self.gobtn.config(state="normal")
        self.stopbtn.config(state="disabled")
        self.bar.config(value=1000 * frac)
        self.status.set(text)
        self.title_state()

    def _pump(self):
        prog = self.prog
        if self.busy and prog:
            self.bar.config(value=1000 * overall(*prog))
            self.status.set(step_text(*prog))
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
                    self.kshown = None
                    self.scrub.config(to=max(0, len(self.frames) - 1))
                    self.scrub.set(0)
                    self.finish("Done.", 1)
                    self.show_frame(0)
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
            return
        k = r.keys[i][0]
        if (self.cur, i) != getattr(self, "kshown", None):
            # a keyframe is decoded once as the scrubber crosses into it,
            # not on every step of the drag
            img = key_view(r, f)[2]
            w, h = img.size
            z = min(160.0 / w, 120.0 / h)
            img = img.resize((max(1, int(w * z)), max(1, int(h * z))))
            buf = io.BytesIO()
            img.save(buf, "PNG")
            self.kphoto = tk.PhotoImage(data=base64.b64encode(
                buf.getvalue()))
            self.kcanvas.delete("all")
            self.kcanvas.create_image(80, 60, image=self.kphoto)
            self.kshown = (self.cur, i)
        self.key = i
        n = len(r.key(i)[1])
        self.ktext.set(
            "Keyframe %d (of 0 to %d)\nframe %d (%.2f s)%s%s" % (
                i, r.nkeys - 1, k, k / r.fps,
                "\nthe poster" if i == r.poster else "",
                "\n%d bytes: past the %d the player reads, so it would "
                "show no poster" % (n, V.KEY_PLAYER)
                if n > V.KEY_PLAYER else ""))
        self.posterbtn.config(state="disabled" if self.busy or
                              i == r.poster else "normal")


def main():
    if tk is None:
        sys.exit("os88vencgui: this Python has no tkinter")
    try:                                # drag and drop, where installed
        from tkinterdnd2 import TkinterDnD
        root = TkinterDnD.Tk()
    except Exception:
        root = tk.Tk()
    app = App(root)
    # the drop only QUEUES: the pump takes it, as it takes the encode's log
    how = enable_drop(root, lambda paths: app.q.put(("drop", paths, None)))
    app.info.set("Choose a video%s." % (
        ", or drop one on this window" if how else ""))
    root.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
