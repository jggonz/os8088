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

# the tabs, and the GROUPS on each (98.2.8.2): an outline with a header
# round the options that go together, and the whole group greyed out when
# the file the form makes cannot use it - the CGA 4-colour group on
# anything but CGA 4 colours, the PC speaker's on any other sound. An
# option named in no group still appears, on Advanced, in "Other"
TAB_ROWS = 10    # more options than this and a tab takes two columns
TABS = ("Basic", "Picture", "Colour", "Sound", "Budget", "Loop and keys",
        "Advanced")
# what the form makes, in the words a reason uses
PF_WORDS = {"mono": "one bit, black and white",
            "cgacomp": "CGA composite colour",
            "vga8": "256 colours on a VGA", "vga4": "16 colours on a VGA",
            "cga4": "CGA 4 colours", "c160": "CGA 16 colours at 160 x 100",
            "c512": "CGA composite, 512 colours", "text": "text mode"}
AUDIO_WORDS = {"pcm8": "8-bit sound for a Sound Blaster",
               "adpcm4": "ADPCM for a Sound Blaster",
               "speaker": "the PC speaker", "none": "no sound"}


def _for(pfs, what):
    """A group's rule: it applies to the pixel formats `pfs`, and says so"""
    def when(c):
        if c["pixfmt"] in pfs:
            return None
        return ("These are for %s, and this file is %s. Choose a target "
                "that makes %s under Made for, or its pixel format on "
                "Basic." % (what, PF_WORDS[c["pixfmt"]], what))
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
# A SPEAKER WAV (SPEC.md 86.21.1) is the sound alone: of the groups only
# these apply to it, and the rest say so
WAV_KEEP = ("Made for", "The clip", "Sound", "PC speaker",
            "PC speaker: the sound shaped")
WAV_WHY = ("This file is a speaker WAV for Audio - the sound alone, shaped "
           "for the PC speaker (SPEC.md 86.21.1) - so it has no picture. "
           "Save as a .V88 to use these.")
# (tab, header, options, rule): a rule is None where the group applies,
# else the reason it does not - the group's tooltip while it is greyed
GROUPS = [
    ("Basic", "Made for", ("preset", "pixfmt", "profile", "aim"), ALWAYS),
    ("Basic", "The clip", ("title", "credits", "start", "end", "fps"),
     ALWAYS),
    ("Picture", "Canvas", ("layout", "box", "fit"), ALWAYS),
    ("Picture", "Tone", ("gamma", "contrast", "brightness"), ALWAYS),
    ("Picture", "Grey levels", ("levels",), _greys),
    ("Picture", "One bit", ("dither", "invert", "clip"),
     _for(("mono",), PF_WORDS["mono"])),
    ("Picture", "A pixel held", ("stable",), _pixel_kept),
    ("Picture", "Text mode", ("text_glyphs", "text_detail", "text_sharpen",
                              "text_busy", "text_stable"),
     _for(("text",), PF_WORDS["text"])),
    ("Picture", "Text mode: words read (OCR)",
     ("text_ocr", "text_ocr_large", "text_ocr_conf", "text_ocr_every"),
     _for(("text",), PF_WORDS["text"])),
    ("Colour", "Text mode colours", ("text_colour", "text_prefer_colour"),
     _for(("text",), PF_WORDS["text"])),
    ("Colour", "CGA, 4 colours", ("cga_palette", "cga_bright", "cga_bg"),
     _for(("cga4",), PF_WORDS["cga4"])),
    ("Colour", "CGA composite", ("comp_dither", "comp_stable", "comp_quick",
                                 "mix", "levels_mix"),
     _for(("cgacomp",), PF_WORDS["cgacomp"])),
    ("Colour", "CGA composite, 512 colours",
     ("cga_card", "c512_dither", "c512_stable", "c512_mix"),
     _for(("c512",), PF_WORDS["c512"])),
    ("Colour", "4 and 16 colours", ("vga4_stable",),
     _for(("cga4", "c160", "c512", "vga4"),
          "CGA 4 colours, CGA 16 colours at 160 x 100, CGA composite 512 "
          "colours or 16 colours on a VGA")),
    ("Colour", "VGA, 256 colours", ("detail", "vga8_dither", "vga8_stable",
                                    "flip"),
     _for(("vga8",), PF_WORDS["vga8"])),
    ("Sound", "Sound", ("audio", "rate", "volume"), ALWAYS),
    ("Sound", "ADPCM", ("adpcm",), _sound("adpcm4", "ADPCM")),
    ("Sound", "PC speaker", ("spk_shape", "spk_pulses", "spk_preview"),
     _sound("speaker", "the PC speaker")),
    ("Sound", "PC speaker: the sound shaped",
     ("spk_style", "spk_highpass", "spk_ratio", "spk_range", "spk_lows",
      "spk_drive", "spk_idle"), _shaping),
    ("Budget", "The machine's budget", ("disk", "avg", "peak", "owe",
                                        "reserve"), ALWAYS),
    ("Budget", "When a frame is cut", ("lookahead", "error"), ALWAYS),
    ("Budget", "Aim: size", ("worth",),
     lambda c: None if c["aim"] == "size" else
     "This is --aim size's floor, and the aim is %s. Choose size as the Aim "
     "on Basic to use it." % c["aim"]),
    ("Loop and keys", "Loop", ("loop_from", "repeat"), ALWAYS),
    ("Loop and keys", "Held in memory", ("resident", "live", "xms"),
     ALWAYS),
    ("Loop and keys", "Keyframes and the poster", ("keysecs", "poster",
                                                   "poster_at"), ALWAYS),
    ("Advanced", "This computer", ("jobs",), ALWAYS),
]
# ...and inside a group that applies, the few options narrower than it
FIELD_WHEN = {
    "text_prefer_colour": lambda c: None if c["text_colour"] == "colour"
    else "This keeps a cell's hue, and the text is mono: it has none.",
    "flip": lambda c: None if c["layout"] == "modex" else
    "Two pages are Mode X's, and this file is laid out as %s: mode 13h "
    "has one page." % (c["layout"] or "lin320"),
    "comp_stable": lambda c: None if c["comp_dither"] == "diffuse" else
    "This is the diffusion's, and Comp dither is pattern.",
    "comp_quick": lambda c: None if c["comp_dither"] == "diffuse" else
    "This is the diffusion's, and Comp dither is pattern.",
    "mix": lambda c: None if c["comp_dither"] == "pattern" else
    "This is the pattern dither's, and Comp dither is diffuse.",
    "levels_mix": lambda c: None if c["comp_dither"] == "pattern" else
    "This is the pattern dither's, and Comp dither is diffuse.",
    "xms": lambda c: None if c["live"] else
    "This makes a LIVE file streamed from XMS, and Live is not chosen.",
    "rate": lambda c: None if c["audio"] != "none" else
    "The file has no sound.",
    "volume": lambda c: None if c["audio"] != "none" else
    "The file has no sound.",
}
assert len({g[1] for g in GROUPS}) == len(GROUPS), "a header names a group"
GROUP_OF = {d: g for g in GROUPS for d in g[2]}
TAB_OF = {d: g[0] for d, g in GROUP_OF.items()}
# the fields a group's rule reads: a change to one re-judges every group
CONTEXT_FIELDS = ("preset", "pixfmt", "profile", "live", "layout", "audio",
                  "text_colour", "comp_dither", "spk_shape", "aim")
# the choices that IMPLY others (os88venc.implied): changing one refills them
IMPLYING = ("preset", "pixfmt", "profile", "live")
# a free-text option's COMMON values, offered in an editable list: the sound
# rate's 5,512 Hz halves the sound's bytes, which is half a Live clip's
# memory (98.1.7.2) - any other rate can still be typed. Owed time's 0 is
# OFF, the fixed per-frame ceiling (98.2.1.1)
SUGGEST = {"rate": ["", "22050", "11025", "8000", "5512"],
           "owe": ["", "0", "1.6"]}
# a free-text option that NAMES A FILE the encode writes: a Browse... beside
# it, a Save dialog of that type, started beside the .V88 under its name
SAVE_FILE = {"spk_preview": ("The speaker preview", ".wav",
                             [("WAV sound", "*.wav")])}
# a line under a tab's fields: what no one field says
TAB_NOTES = {
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
                live=g("live"))


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


# --------------------------------------------------------------------------
# the palette: what colours the Colour tab's choices give, as swatches
# --------------------------------------------------------------------------
# the sixteen by name, the cga_bg "?" list's own words
C16_NAMES = [V.CHOICE_HELP["cga_bg"][str(i)] for i in range(16)]
# the fields whose change redraws the panel: the ones that pick a palette,
# and the ones that pick which KIND of palette there is
PAL_FIELDS = ("preset", "pixfmt", "profile", "live", "cga_palette",
              "cga_bright", "cga_bg", "text_colour", "cga_card")


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
        out.append(("256 colours", [], "Chosen from the clip itself when "
                    "it is encoded: its palette shows below once the file "
                    "is made, or a .V88 is opened."))
    else:
        out.append(("Black and white", [("one bit", _c16((0, 15)))],
                    "One bit a pixel: no palette to choose."))
    return out


def file_palette(r):
    """The palette a .V88 was ENCODED with, where it was chosen from its
    clip - CGA4's four, VGA8's 256 - as palette_view's rows: what "the
    nearest" turned out to be. [] for a file whose colours are fixed"""
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
    if n <= 256:
        return 32, 8, 8, 0
    return 32, 4, 4, 0


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
    return (i,) + key_picture(r, i)


def key_picture(r, i):
    """(its frame, its picture): keyframe `i` drawn from itself alone"""
    k, rec, spo, spn, idx = r.key(i)
    surf = r.g.surface()
    r.apply(surf, rec, key=True)
    return k, render(r, surf)


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
    width = width or max(c.winfo_width(), 620)
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
                  foreground="#555").grid(row=0, column=0, columnspan=3,
                                          sticky="w", pady=(0, 8))
        vals = {k: x.get() for k, x in self.vars.items()}
        bold = ("TkDefaultFont", 10, "bold")

        def take(c):
            v.set(c)
            if f["dest"] in IMPLYING:
                self.apply_implied()
            elif f["dest"] == "audio":
                self.apply_audio()
            elif f["dest"] == "spk_style":
                self.apply_style()
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
        self.job = 0                    # the job the window is showing: a
        self.ejob = None                # message from any other is stale
        root.title(APPNAME)
        root.geometry("1080x760")
        self.src = tk.StringVar()
        self.out = tk.StringVar()
        self.outtype = tk.StringVar(value=OUT_TYPES[0][0])
        self.target = tk.StringVar(value=TARGETS[0][0])
        self.info = tk.StringVar(value="Choose a video.")
        self.mkdisk = tk.BooleanVar(value=False)
        self.disksize = tk.StringVar(value=DISK_LABELS[0])
        self.diskpicked = False         # ...a person's choice, kept
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
        # --- the GROUPS (98.2.8.2): an outline with a header round the
        # options that go together, down one column - or, on a tab of more
        # than TAB_ROWS options, two, split in the table's order where the
        # taller is shortest (the Picture tab's 21 in one were taller than
        # the window)
        fl = {f["dest"]: f for f in fields()}
        tabgroups = {t: [] for t in TABS}
        for tab, name, dests, _ in GROUPS:
            tabgroups[tab].append((name, [fl[d] for d in dests if d in fl]))
        other = [f for d, f in fl.items() if d not in GROUP_OF]
        if other:                       # an option no group names yet
            tabgroups["Advanced"].append(("Other", other))
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
        for t in TABS:
            gs = [g for g in tabgroups[t] if g[1]]
            cols = [gs]
            if sum(len(g[1]) for g in gs) > TAB_ROWS and len(gs) > 1:
                hs = [len(g[1]) + 1.5 for g in gs]
                k = min(range(1, len(gs)), key=lambda k: max(
                    sum(hs[:k]), sum(hs[k:])))
                cols = [gs[:k], gs[k:]]
            # a TWO-COLUMN tab's fields are narrower, or the right-hand
            # column's "?" and Browse... fall off the pane's edge
            fw = 22 if len(cols) == 1 else 12
            for ci, col in enumerate(cols):
                cf = ttk.Frame(pages[t])
                cf.grid(row=0, column=ci, sticky="nwe",
                        padx=(0 if ci == 0 else 10, 0))
                # every group in a column lines its fields up, by the
                # labels' own widths rather than a count of characters
                lw = max(norm.measure(f["label"]) for g in col
                         for f in g[1]) + 6
                for name, fs in col:
                    self.group_box(cf, name, fs, fw, lw, start)
        for t, text in TAB_NOTES.items():   # a word the fields cannot say
            ttk.Label(pages[t], text=text, foreground="#555",
                      wraplength=600, justify="left").grid(
                row=1, column=0, columnspan=2, sticky="w", pady=(10, 0))
        for d in CONTEXT_FIELDS:        # a change re-judges every group
            if d in self.vars:
                self.vars[d].trace_add("write",
                                       lambda *a: self.groups_dirty())
        self.out.trace_add("write", lambda *a: self.groups_dirty())
        self.out.trace_add("write", lambda *a: self.sync_outtype())
        # --- the palette the Colour tab's choices give, redrawn as they
        # change: under the tab's fields, whatever the tab's note
        self.palframe = ttk.LabelFrame(pages["Colour"], text="Palette",
                                       padding=(6, 2, 6, 4))
        self.palframe.grid(row=2, column=0, columnspan=2, sticky="we",
                           pady=(8, 0))
        self.palcanvas = tk.Canvas(self.palframe, width=620, height=60,
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
        ttk.Label(pr, textvariable=self.status, foreground="#555").pack(
            anchor="w")
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
        # the palette the file was encoded with, where its clip chose it:
        # packed here by draw_palette when there is one
        self.fpal = tk.Canvas(fi, width=360, height=24,
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
                    self.paltips)
        rows = file_palette(self.reader)
        if rows:
            swatch_draw(self.fpal, [(None, [("Palette: " + ln, sw)
                                            for ln, sw in rows], None)],
                        self.paltips, width=360)
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
                v = tk.StringVar(value=start[f["dest"]])
                w = ttk.Combobox(p, textvariable=v, values=f["choices"],
                                 width=fw)
            else:
                v = tk.StringVar(value=start[f["dest"]])
                w = ttk.Entry(p, textvariable=v, width=fw + 2)
            w.grid(row=r, column=1, sticky="w", padx=4, pady=(0, 1))
            if f["dest"] in IMPLYING:
                w.bind("<<ComboboxSelected>>",
                       lambda e: self.apply_implied())
            elif f["dest"] == "audio":
                w.bind("<<ComboboxSelected>>",
                       lambda e: self.apply_audio())
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
                bb = ttk.Button(p, text="Browse...", style="Tool.TButton",
                                command=lambda f=f, v=v:
                                self.browse_save(f, v))
                bb.grid(row=r, column=2, sticky="w")
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
        self.formload = p       # ...and the form set to how it was made
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
        self.formload = None            # (load_v88 sets it after this)
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
                elif kind == "wavdone":
                    self.finish("Done.", 1)
                    self.write("\n%s: the sound for Audio on the PC "
                               "speaker (SPEC.md 86.21.1). Copy it to the "
                               "machine and open it; on a Sound Blaster it "
                               "plays too.\n" % os.path.basename(val))
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
                    if getattr(self, "formload", None) == self.cur:
                        self.apply_file_options()
                    self.formload = None
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
        n = len(r.key(i)[1])
        self.ktext.set(
            "Keyframe %d (of 0 to %d)\nframe %d (%.2f s)%s%s" % (
                i, r.nkeys - 1, k, k / r.fps,
                "\n= the poster" if i == r.poster else "",
                "\n%d bytes: past the %d the player reads, so it would "
                "show no poster" % (n, V.KEY_PLAYER)
                if n > V.KEY_PLAYER else ""))
        self.posterbtn.config(state="disabled" if self.busy or
                              i == r.poster else "normal")

    def apply_file_options(self):
        """A .V88 LOADED (98.2.17): every field set to the option it was
        made with, the target it was made for, and what could not be
        carried over said in the log - or, for a file made before the
        options were stored, the form left as it was, and that said"""
        name = os.path.basename(self.cur)
        try:
            got = form_from_file(self.reader)
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
