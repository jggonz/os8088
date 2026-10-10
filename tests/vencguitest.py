#!/usr/bin/env python3
"""The encoder's window, without a window - VIDEO-PLAN 14.2 (E1-E4),
tools/os88vencgui.py.

    python3 tests/vencguitest.py

Host-side. Everything the window does short of drawing is ordinary data,
so it is checked here with no Tk at all:

1. EVERY OPTION IS ON A TAB, WITH A TOOLTIP: the window's table against
   os88venc.parser(), each option once, none without its help - and every
   tab the table names is one of the window's. EVERY CHOICE SAYS WHAT IT
   IS: each value of a choice option has its line in os88venc.CHOICE_HELP,
   the window's "?" beside the field, and no line names a value that is not
   one - so a preset or layout added tomorrow cannot arrive unexplained.
2. THE DEFAULTS ARE THE ENCODER'S: the form left alone makes a command line
   that parses to the parser's own defaults, option for option. The
   speaker style's high-pass, ratio and range are SHOWN filled and stay
   off the command line while they are the style's, for either style -
   and one changed by hand is on it, the sound being the speaker's (with
   any other sound the group is greyed, 14).
3. EVERY TARGET ENCODES: each "made for" choice, on a second of ffmpeg's
   testsrc2, is a file os88vid verifies, of the format and layout it says -
   and a Live one a live file naming its screen.
4. THE PREVIEW IS THE SCREEN'S SHAPE: every frame of every target rendered,
   each at its file's pixel aspect - a 4:3 screen's canvas comes out 4:3 -
   and a CGA4 frame in its own four colours.
5. MAKE A DISK makes one: the image holds the video (and VIDEO.O88 when it
   is built).
6. A TARGET SHOWS WHAT IT IS (SPEC.md 98.2.10): each "made for" fills the
   canvas, format, frame rate, detail, budget and sound it implies - and its
   command line is still the short one, parsing to exactly what the unfilled
   target's does. A --pixfmt that only echoed its own implication was left
   off, and the composite and 16-colour files came out one-bit.
7. THE 256-COLOUR PRESETS ARE 25 FPS AT 2x1: --preset modex on a 30 fps
   source makes a 25 fps file whose every pixel pair is one colour.
8. PROGRESS AND CANCEL (SPEC.md 98.2.11): an encode reports its steps in
   order and the window's bar only ever moves forward, to the end; a
   Cancel mid-read and mid-encode stops it with nothing written, an older
   file of that name byte for byte as it was, and no ffmpeg left running.
9. THE POSTER, IN PLACE: set_poster changes the poster word of every
   rendition and not one other byte, the file still verifies, the panel's
   keyframe for a frame is the one a play from it starts at, drawn exactly
   as the stream draws that frame - on a five-key encode and on the
   three-rendition logo. set_title changes the header's title field and
   not one other byte: a long title cut to 47, a shorter one after it with
   no tail of the old, a control character a '?'.
10. THE PREVIEW STARTS WHERE THE SCREEN DOES (SPEC.md 98.2.9): a VGA8
   frame 0 too big for one record is pre-rolled, and a colour play starts
   at key 0 past it - so the preview's first frame is key 0's whole
   picture, not the half-painted frame 0. The owner's Spice & Wolf encode
   played whole and previewed half-painted; with the preview from frame 0
   this FAILS. And the picture must be LIT: the source is noise over
   every pixel, one span the size of the screen, which the encoder skipped
   whole every frame until spans were cut (98.2.9.1) - so key 0 was black.
11. A DROP (SPEC.md 98.2.12): a video dropped on the window is the source,
   a .V88 (any case) goes to the preview, the first of several is taken,
   and a drop of nothing does nothing. With tkinterdnd2 installed the hook
   is registered on a real Tk (skipped where there is no display).
12. THE WINDOW'S ENCODE IS A PROCESS, AND CANCEL KILLS IT (98.2.11.1): an
   EncodeJob run to its end reports its steps in order and makes a file
   that verifies; one cancelled mid-encode is GONE within two seconds, with
   nothing of its process group left (its ffmpeg included), the older file
   byte for byte as it was, and no .part. The owner's Cancel on Windows
   left the encode running to its end, because it only asked.
13. THE PALETTE PANEL SHOWS WHAT THE ENCODER WILL USE: every target's
   Colour tab has a palette to show; a CGA4 form with the set, intensity
   and background all fixed shows ONE row, the four colours of the byte
   os88venc.cga4_pick returns for those same overrides - and with none
   fixed, one row per set and intensity, each the byte cga4_pick returns
   when just those two are fixed. Each background's "?" swatch is its
   colour, and a CGA4 file's own palette under the preview is the one in
   its header. A panel that drew mode 5 where the file will be mode 4 -
   the set bits swapped - FAILS here, naming the row.
14. A GROUP IS GREYED WHEN IT CANNOT APPLY (SPEC.md 98.2.8.2): every option
   is in one group at most, every group names options that exist, and for
   each target the group of the pixel format it makes applies and every
   other format's is greyed WITH A REASON - the speaker's two apply to
   the speaker target and to no other. What a greyed group holds stays
   off the command line: --cga-palette on the speaker target, --flip on
   13h and on 16 colours' 640 x 480 - and Flip applies to 16 colours on a
   Screen of their own. With group_state answering "applies" for everything, 14 FAILS on
   every target.
15. A FILE SAYS HOW IT WAS MADE (SPEC.md 98.1.1.4, 98.2.17): every target's
   file carries its options, and LOADING it gives a form whose command line
   resolves to the very record stored - every option, the target it was
   made for picked again, nothing noted as lost. The frozen deflate
   dictionary is pinned by SHA-256, the parser's schema by its fingerprint
   (an option added, renamed or given other choices FAILS here until
   OPTS_VERSION goes up with a MIGRATIONS entry), and the version mapper
   is driven through a made-up history - a rename, a renamed choice, an
   option added with its legacy value, one removed, a record from a newer
   encoder, a value no longer a choice. A damaged block leaves the file
   opening and playing (Reader) but refused by verify_v88; a file made
   before options were stored loads as None. With form_value answering ""
   the round trip FAILS on every target.

Broken on purpose - an option dropped from the table, or a help string
emptied - 1 FAILS naming it. Needs ffmpeg for 3 to 5 and SKIPS without it.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88venc as V                                         # noqa: E402
import os88vencgui as G                                      # noqa: E402
import os88vid as vid                                        # noqa: E402

SKIP = 77


# the group each pixel format's own options are in - spelled here rather
# than read off the window's table, so a table that greys the wrong one
# cannot agree with itself
FORMAT_GROUP = {"mono": "One bit", "cgacomp": "CGA composite",
                "cga4": "CGA, 4 colours", "c512": "CGA composite, 512 colours",
                "vga8": "VGA, 256 colours", "text": "Text mode"}
SPEAKER_GROUPS = ("PC speaker", "PC speaker: the sound shaped")


def groups_leg():
    """14: the groups against the targets"""
    bad = []
    grouped = [d for g in G.GROUPS for d in g[2]]
    opts = {f["dest"] for f in G.fields()}
    twice = sorted({d for d in grouped if grouped.count(d) > 1})
    stale = sorted(set(grouped) - opts)
    loose = sorted(opts - set(grouped))
    if twice or stale:
        bad.append("14: options in two groups %s, groups naming no option %s"
                   % (twice, stale))
    heads = {g[1] for g in G.GROUPS}
    missing = [h for h in list(FORMAT_GROUP.values()) + list(SPEAKER_GROUPS)
               if h not in heads]
    if missing:
        bad.append("14: no group %s" % missing)
    wrong = []
    for i, t in enumerate(G.TARGETS):
        v = G.form_start()
        v.update(G.target_fill(i, 30.0))
        gs, fs = G.group_state(v)
        pf = G.form_context(v)["pixfmt"]
        for f, h in FORMAT_GROUP.items():
            if (gs.get(h) is None) != (f == pf):
                wrong.append("%s: %s %s" % (t[0], h, "greyed" if gs.get(h)
                                            else "applies"))
        spk = v.get("audio") == "speaker"
        for h in SPEAKER_GROUPS:
            if (gs.get(h) is None) != spk:
                wrong.append("%s: %s %s" % (t[0], h, "greyed" if gs.get(h)
                                            else "applies"))
        noreason = [h for h, why in gs.items() if why is not None
                    and not why.strip()]
        if noreason:
            wrong.append("%s: %s greyed saying nothing" % (t[0], noreason))
    # what a greyed group holds is left off the command line
    tgt = lambda pre: [i for i, t in enumerate(G.TARGETS) if t[1] == pre][0]
    offs = []
    for pre, sets, flag, on in (
            ("herc-spk", {"cga_palette": "1"}, "--cga-palette", False),
            ("cga4", {"cga_palette": "1"}, "--cga-palette", True),
            ("vga8", {"flip": "1"}, "--flip", False),
            ("modex", {"flip": "1"}, "--flip", True),
            # 16 colours flip on a screen of their own (98.2.5.1), and
            # on 12h's own 640 x 480 do not - the owner found Flip greyed
            # with Screen at 640x400
            ("vga4", {"flip": "1"}, "--flip", False),
            ("vga4", {"flip": "1", "screen": "640x400"}, "--flip",
             True),
            ("cga4", {"spk_pulses": "2"}, "--spk-pulses", False),
            ("herc-spk", {"spk_pulses": "2"}, "--spk-pulses", True)):
        v = G.form_start()
        v.update(G.target_fill(tgt(pre), 30.0))
        v.update(sets)
        if (flag in G.argv_from("in.mp4", "o.V88", v, 30.0)) != on:
            offs.append("%s %s on %s" % (flag, "missing" if on else
                                         "left on", pre))
    # A SPEAKER WAV (86.21.1): every target's form, saved as a .WAV, has
    # only the sound's groups left and no --audio on its command line
    for i, t in enumerate(G.TARGETS):
        v = G.form_start()
        v.update(G.target_fill(i, 30.0))
        gs, fs = G.group_state(dict(v, _out="o.WAV"))
        on = sorted(h for h, why in gs.items() if why is None)
        if on != sorted(G.WAV_KEEP):
            wrong.append("%s as a WAV: %s apply" % (t[0], on))
        if "--audio" in G.argv_from("in.mp4", "o.WAV", v, 30.0):
            offs.append("--audio on a WAV from %s" % t[0])
    print("   14: %d groups, %d options in none (on Advanced), %d targets: "
          "%d wrong, %d command lines wrong"
          % (len(G.GROUPS), len(loose), len(G.TARGETS), len(wrong),
             len(offs)))
    if wrong:
        bad.append("14: %s" % "; ".join(wrong))
    if offs:
        bad.append("14: %s" % "; ".join(offs))
    return bad


ZDICT_SHA256 = ("2f1a524ffb7c1ca2ff54102a28e41201"
                "077dc25be32e295f46270508b9bbccd0")


def options_leg():
    """15, the half with no encode: the container, the schema, the mapper"""
    import hashlib
    bad = []
    got = hashlib.sha256(vid.OPTS_ZDICT[1]).hexdigest()
    if got != ZDICT_SHA256:
        bad.append("15: os88vid.OPTS_ZDICT[1] is not the frozen dictionary "
                   "(sha256 %s): every file made with it would stop reading "
                   "- a new dictionary is a new container version" % got)
    fp = V.opts_fingerprint()
    if fp != V.OPTS_FINGERPRINT.get(V.OPTS_VERSION):
        bad.append("15: the encoder's options are not version %d's any more "
                   "(fingerprint %s, pinned %s): an option was added, "
                   "renamed, removed or given other choices. Raise "
                   "os88venc.OPTS_VERSION, add MIGRATIONS[%d] saying how a "
                   "version-%d record reads now, and pin the new "
                   "fingerprint (SPEC.md 98.2.17)"
                   % (V.OPTS_VERSION, fp,
                      V.OPTS_FINGERPRINT.get(V.OPTS_VERSION),
                      V.OPTS_VERSION, V.OPTS_VERSION))
    # the MAPPER through a made-up history: version 1 called --keysecs
    # --key-secs and --dither's bayer "ordered", had no --flip (and made
    # every file as if it were on), and had an --old-thing since removed
    keep = V.OPTS_VERSION, V.MIGRATIONS
    try:
        V.OPTS_VERSION = 2
        V.MIGRATIONS = {1: [("rename", "key_secs", "keysecs"),
                            ("revalue", "dither", {"ordered": "bayer"}),
                            ("added", "flip", True),
                            ("removed", "old_thing", "folded into --stable")]}
        base = {d: V.parser().get_default(d) for d in V.opts_actions()}
        o1 = dict(base, key_secs=1.5, dither="ordered", old_thing=3)
        del o1["keysecs"], o1["flip"]
        o, notes = V.opts_migrate({"v": 1, "o": o1})
        want = dict(base, keysecs=1.5, dither="bayer", flip=True)
        if o != want or not any("old-thing" in n for n in notes):
            bad.append("15: the version mapper made %s, noting %s"
                       % ({k: o.get(k) for k in ("keysecs", "dither",
                                                  "flip", "old_thing")},
                          notes))
        o, notes = V.opts_migrate({"v": 3, "o": dict(base, novel=1,
                                                     dither="sierra")})
        if "novel" in o or "dither" in o or len(notes) < 3:
            bad.append("15: a newer encoder's record read as %s, noting %s"
                       % ({k: o.get(k) for k in ("novel", "dither")}, notes))
    finally:
        V.OPTS_VERSION, V.MIGRATIONS = keep
    # the container: a round trip, and damage refused where it is read
    doc = {"v": 1, "src": "x.mp4", "o": {"fps": 23.0}}
    if vid.unpack_options(vid.pack_options(doc)) != doc:
        bad.append("15: pack_options does not round-trip")
    old = os.path.join(ROOT, "apps", "video", "os8088.v88")
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as t:
        if G.form_from_file(vid.Reader(old)) is not None or \
                G.form_from_file(vid.HeadOptions(old)) is not None:
            bad.append("15: a file with no options loaded as if it had some")
        d = bytearray(open(old, "rb").read())
        blob = vid.pack_options(doc)
        struct.pack_into("<IH", d, vid.H_OPTS, len(d), len(blob))
        good, hurt = os.path.join(t, "G.V88"), os.path.join(t, "H.V88")
        open(good, "wb").write(bytes(d + blob))
        blob = bytearray(blob)
        blob[len(blob) // 2] ^= 0x55
        open(hurt, "wb").write(bytes(d + blob))
        try:
            vid.verify_v88(good)
            if vid.Reader(good).options() != doc or \
                    vid.HeadOptions(good).options() != doc:
                bad.append("15: a stored record reads back differently")
        except vid.V88Error as e:
            bad.append("15: a good options block is refused: %s" % e)
        try:
            vid.Reader(hurt)            # it OPENS: the player never reads it
        except vid.V88Error as e:
            bad.append("15: a damaged options block stops the file "
                       "opening: %s" % e)
        try:
            vid.verify_v88(hurt)
            bad.append("15: verify_v88 passed a damaged options block")
        except vid.V88Error:
            pass
        try:                            # (the window's header-only read)
            vid.HeadOptions(hurt).options()
            bad.append("15: the header-only read passed a damaged block")
        except vid.V88Error:
            pass
    print("   15: dictionary %s, schema %s (version %d), the mapper and the "
          "container checked" % (got[:12], fp, V.OPTS_VERSION))
    return bad


def roundtrip(i, label, src, out):
    """15: the file made for target `i` loads into a form whose command
    line resolves to the record it carries, for the same target"""
    r = vid.Reader(out)
    try:
        got = G.form_from_file(r)
    except vid.V88Error as e:
        return ["15: %s: its options do not read: %s" % (label, e)]
    if got is None:
        return ["15: %s: the file carries no options" % label]
    vals, ti, notes, name = got
    # ...and the window's read at OPEN, the header and the block alone
    # (98.2.17), must fill the same form as the whole file's
    if G.form_from_file(vid.HeadOptions(out)) != got:
        return ["15: %s: the header-only read fills another form" % label]
    stored = r.options()["o"]
    a2 = V.parser().parse_args(G.argv_from(src, out + ".2", vals))
    again = V.options_record(a2, 15.0)
    diff = ["%s %r -> %r" % (k, stored[k], again.get(k)) for k in stored
            if k not in ("title", "credits") and stored[k] != again.get(k)]
    bad = []
    if diff or ti != i or notes or name != os.path.basename(src):
        bad.append("15: %s: loaded as target %s from %r, noting %s; the "
                   "form's command line differs in %s"
                   % (label, ti, name, notes, "; ".join(diff) or "nothing"))
    print("   15: %-56s %d-byte record, loads as itself"
          % (label[:56], r.optslen))
    return bad


def main():
    bad = []
    # --- 1
    fl = G.fields()
    dests = [f["dest"] for f in fl]
    opts = [a.dest for a in V.parser()._actions
            if a.option_strings and a.dest not in G.HIDDEN]
    missing = sorted(set(opts) - set(dests))
    twice = sorted(d for d in set(dests) if dests.count(d) > 1)
    notip = [f["flag"] for f in fl if not f["tip"]]
    tabs = sorted({f["tab"] for f in fl} - set(G.TABS))
    stale = sorted(set(G.TAB_OF) - set(opts))
    nohelp = ["%s %s" % (f["flag"], c) for f in fl if f["kind"] == "choice"
              for c in f["choices"] if c and not (f["help"] or {}).get(c)]
    oldhelp = ["%s %s" % (f["flag"], c) for f in fl if f["help"]
               for c in f["help"] if c not in f["choices"]]
    oldhelp += ["--%s" % d for d in V.CHOICE_HELP if d not in dests]
    print("   %d options on %d tabs; %d missing, %d twice, %d without a "
          "tooltip, %d tab names unknown, %d placings for no option"
          % (len(fl), len({f["tab"] for f in fl}), len(missing), len(twice),
             len(notip), len(tabs), len(stale)))
    for what, lst in (("missing from the window", missing),
                      ("on the window twice", twice),
                      ("with no tooltip", notip), ("on no tab", tabs),
                      ("placed but not an option", stale),
                      ("choices with no line in CHOICE_HELP", nohelp),
                      ("CHOICE_HELP lines for no choice", oldhelp)):
        if lst:
            bad.append("options %s: %s" % (what, " ".join(lst)))
    # --- 2
    vals = G.form_start()
    a = V.parser().parse_args(G.argv_from("in.mp4", "out.V88", vals))
    d = V.parser().parse_args(["in.mp4", "out.V88"])
    diff = [k for k in vars(d) if getattr(a, k) != getattr(d, k)]
    print("   the untouched form: %d options differ from the parser's "
          "defaults" % len(diff))
    if diff:
        bad.append("the untouched form changes %s" % " ".join(diff))
    for style in sorted(vid.SPK_STYLES):
        sv = G.style_values(style)
        st = vid.SPK_STYLES[style]
        shown = dict(vals, audio="speaker", spk_style=style, **sv)
        a = V.parser().parse_args(G.argv_from("in.mp4", "o.V88", shown))
        on = [dst for dst, _ in G.STYLE_FIELDS
              if getattr(a, dst) is not None]
        wrong = [dst for dst, k in G.STYLE_FIELDS
                 if float(sv[dst]) != st[k]]
        a = V.parser().parse_args(G.argv_from(
            "in.mp4", "o.V88", dict(shown, spk_highpass="333")))
        print("   style %s shows %s" % (style, " ".join(
            "%s %s" % (dst, sv[dst]) for dst, _ in G.STYLE_FIELDS)))
        if on or wrong or a.spk_highpass != 333:
            bad.append("style %s: %s on the command line, %s shown wrong, "
                       "a changed high-pass read as %s"
                       % (style, on, wrong, a.spk_highpass))
    # --- 6: a target shows what it is, and says it briefly
    for i, t in enumerate(G.TARGETS):
        full = G.target_fill(i, 30.0)
        empty = [k for k in ("layout", "box", "pixfmt", "fps", "detail",
                             "rate", "audio") if not full.get(k)]
        a1 = V.parser().parse_args(G.argv_from("in.mp4", "o.V88", full,
                                               30.0))
        a0 = V.parser().parse_args(G.argv_from("in.mp4", "o.V88",
                                               G.target_values(i), 30.0))
        if empty or vars(a1) != vars(a0):
            bad.append("%s: shows nothing for %s, or its command line is "
                       "not the unfilled one's" % (t[0], " ".join(empty)))
    eight = [G.target_fill(i, 30.0) for i, t in enumerate(G.TARGETS)
             if t[1] in ("vga8", "modex")]
    # ...the 486's whole picture (its profile's default, 98.2.3.5)
    want8 = [("25", "1x1" if f["profile"] == "486" else "2x1") for f in eight]
    print("   %d targets filled; the 256-colour ones at %s fps, detail %s"
          % (len(G.TARGETS), "/".join(f["fps"] for f in eight),
             "/".join(f["detail"] for f in eight)))
    if len(eight) < 2 or [(f["fps"], f["detail"]) for f in eight] != want8:
        bad.append("the 256-colour targets are not 25 fps at 2x1 (1x1 on "
                   "the 486)")
    # --- 11: a drop
    cases = [(["C:/v/clip.mp4"], ("source", "C:/v/clip.mp4")),
             (["C:/v/OUT.V88", "x.mp4"], ("preview", "C:/v/OUT.V88")),
             (["/tmp/a.v88"], ("preview", "/tmp/a.v88")),
             (["", "b.avi"], ("source", "b.avi")), ([], None)]
    wrong = [(p, G.drop_target(p), w) for p, w in cases
             if G.drop_target(p) != w]
    longp = "/a/very/long/folder/name/that/goes/on/and/on/x/clip.mp4"
    if G.out_for(longp) != "/a/very/long/folder/name/that/goes/on/and/on/x/" \
            "clip.V88":
        wrong.append(("the default name for", longp, G.out_for(longp)))
    print("   11: %d drop cases, %d wrong" % (len(cases), len(wrong)))
    if wrong:
        bad.append("11: drops taken wrongly: %s" % wrong)
    leg13(bad)
    bad += groups_leg()
    bad += options_leg()
    if not shutil.which("ffmpeg") or not shutil.which("ffprobe"):
        for b in bad:
            print("   FAIL: %s" % b)
        print("   SKIP: no ffmpeg for 3 to 5")
        return 1 if bad else SKIP
    # --- 3, 4, 5
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        src = os.path.join(tmp, "src.mp4")
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi",
                        "-i", "testsrc2=duration=1:size=320x240:rate=15",
                        src], check=True)
        text, sv = G.suggest(src)
        print("   the source: %s; suggested %s" % (text, sv))
        if sv.get("audio") != "none":
            bad.append("a silent source did not suggest no sound")
        for i, (label, preset, pixfmt, profile, extra) in \
                enumerate(G.TARGETS):
            v = dict(vals)
            v.update(sv)
            v.update(G.target_fill(i, 15.0))    # what the window shows
            if pixfmt == "cgacomp":
                v["comp_quick"] = "1"       # (the gate's time, not its point)
            out = os.path.join(tmp, "T%d.V88" % i)
            argv = G.argv_from(src, out, v) + ["--quiet"]
            try:
                V.encode(V.parser().parse_args(argv))
                vid.verify_v88(out)
            except Exception as e:
                bad.append("%s: %s" % (label, e))
                continue
            bad += roundtrip(i, label, src, out)
            r, frames = G.preview_frames(out)
            w, h = frames[0][1].size
            an, ad = r.aspect
            ratio = w / float(h)
            # the canvas's own display shape: its pixels times their aspect
            px = r.g.wb * (4 if r.pixfmt == vid.PF_CGA4 else
                           vid.PIX_PER_BYTE[r.g.layout])
            if r.pixfmt in (vid.PF_C512, vid.PF_TEXT):
                px = r.g.wb // 2        # a CELL is two bytes, and the
                                        # aspect is a cell's (98.1.2)
            shape = px * an / float(ad) / (r.g.h * r.rowscale)
            print("   %-58s %s on %s, %d frames, preview %d x %d"
                  % (label, vid.PF_NAMES[r.pixfmt], r.g.name, len(frames),
                     w, h))
            if len(frames) != r.frames - vid.first_shown(r) or \
                    abs(ratio - shape) > 0.02:
                bad.append("%s: the preview is %d x %d for a %.3f:1 canvas"
                           % (label, w, h, shape))
            if extra.get("live") and not (r.live and r.target ==
                                          vid.TARGETS[extra["live"]]):
                bad.append("%s: not a live file for its screen" % label)
            if pixfmt and vid.PF_NAMES[r.pixfmt].lower() != \
                    {"mono": "mono1"}.get(pixfmt, pixfmt):
                bad.append("%s: made %s" % (label, vid.PF_NAMES[r.pixfmt]))
            if r.pixfmt == vid.PF_CGA4:
                fp = [h for h, _ in G.file_palette(r)[0][1]]
                if fp != [h for h, _ in G._c16(vid.cga4_colours(r.cgapal))]:
                    bad.append("13: the file's palette panel shows %s, not "
                               "its header's %02Xh" % (fp, r.cgapal))
                import numpy as np
                cols = {tuple(c) for c in np.asarray(
                    frames[-1][1]).reshape(-1, 3).tolist()}
                own = {tuple(G._rgb16()[c]) for c in
                       vid.cga4_colours(r.cgapal)}
                if not cols <= own:
                    bad.append("the CGA4 preview has colours its palette "
                               "has not: %s" % sorted(cols - own)[:4])
        # --- 7: the 256-colour presets' own defaults
        src30 = os.path.join(tmp, "src30.mp4")
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi",
                        "-i", "testsrc2=duration=1:size=320x240:rate=30",
                        src30], check=True)
        out = os.path.join(tmp, "MX.V88")
        V.encode(V.parser().parse_args([src30, out, "--preset", "modex",
                                        "--profile", "286-vga",
                                        "--quiet"]))
        r = vid.Reader(out)
        import numpy as np
        cv = np.frombuffer(vid.decode_at(r, r.frames - 1),
                           np.uint8).reshape(r.g.h, r.g.w)
        odd = int((cv[:, 0::2] != cv[:, 1::2]).sum())
        print("   --preset modex from 30 fps: %.3f fps, %d pixel pairs of "
              "two colours" % (r.fps, odd))
        if abs(r.fps - 25.0) > 0.01 or odd:
            bad.append("--preset modex made %.3f fps with %d split pairs, "
                       "not 25 at 2x1" % (r.fps, odd))
        leg8(tmp, bad)
        leg9(tmp, bad)
        leg10(tmp, bad)
        leg12(tmp, bad)
        legdisk(tmp, bad)
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


def leg13(bad):
    """13: the Colour tab's palette panel against the encoder's own pick"""
    empty = [t[0] for i, t in enumerate(G.TARGETS)
             if not all(h and n for h, _, n in
                        G.palette_view(G.target_fill(i, 30.0)))]
    if empty:
        bad.append("13: no palette shown for %s" % "; ".join(empty))
    cga4 = [i for i, t in enumerate(G.TARGETS) if t[2] == "cga4"][0]
    base = G.target_fill(cga4, 30.0)
    hexes = lambda sw: [x[0] if x else None for x in sw]
    swat = [c for c in range(16)
            if hexes(G.choice_swatches("cga_bg", str(c), {})) !=
            hexes(G._c16((c,)))]
    if swat:
        bad.append("13: background choices shown in the wrong colour: %s"
                   % swat)
    try:
        import numpy as np
    except ImportError:
        print("   13: %d targets show a palette; no numpy, so not checked "
              "against cga4_pick" % len(G.TARGETS))
        return
    rng = np.random.RandomState(88)
    frames = [rng.randint(0, 256, (40, 64, 3)).astype(np.uint8)]
    checked = 0
    for pal in (0, 1, 2):
        for br in (0, 1):
            for bg in (0, 1, 9, 14):
                v = dict(base, cga_palette=str(pal), cga_bright=str(br),
                         cga_bg=str(bg))
                (h, rows, n), = G.palette_view(v)
                want = G._c16(vid.cga4_colours(V.cga4_pick(frames, bg, pal,
                                                            br)))
                if len(rows) != 1 or hexes(rows[0][1]) != hexes(want):
                    bad.append("13: set %d, bright %d, background %d shows "
                               "%s, the encoder makes %s" % (
                                   pal, br, bg, rows and rows[0], want))
                checked += 1
    (h, rows, n), = G.palette_view(dict(base, cga_palette="", cga_bright="",
                                        cga_bg=""))
    want = []
    for br in (0, 1):
        for pal in (0, 1, 2):
            sel = V.cga4_pick(frames, 0, pal, br)
            want.append([None] + hexes(G._c16(vid.cga4_colours(sel)))[1:])
    got = [hexes(sw) for _, sw in rows]
    if got != want:
        bad.append("13: nothing fixed shows %s, the encoder's candidates "
                   "are %s" % (got, want))
    print("   13: %d targets show a palette; %d fixed CGA4 palettes and "
          "the %d candidates match cga4_pick" % (len(G.TARGETS), checked,
                                                 len(want)))


def root_names(img, st11, spt, heads):
    """The FAT16 root's names on a hard disk os88hdd made - read here, not
    by the tool that wrote it: past an ST11's hidden cylinder, through the
    MBR's one partition and its BPB"""
    d = open(img, "rb").read()
    o = spt * heads * 512 if st11 else 0
    lba = struct.unpack_from("<I", d, o + 446 + 8)[0]
    b = o + lba * 512
    rsvd, nfats = struct.unpack_from("<H", d, b + 14)[0], d[b + 16]
    ents, fatsz = (struct.unpack_from("<H", d, b + 17)[0],
                   struct.unpack_from("<H", d, b + 22)[0])
    r = b + (rsvd + nfats * fatsz) * 512
    out = []
    for i in range(ents):
        e = d[r + 32 * i:r + 32 * i + 32]
        if e[0] in (0, 0xE5):
            continue
        n, x = e[:8].decode().strip(), e[8:11].decode().strip()
        out.append(n + ("." + x if x else ""))
    return out


def legdisk(tmp, bad):
    """...AND MAKE A DISK OF IT (SPEC.md 98.2.12.1): every entry of DISKS.
    A floppy for a video whose name is not 8.3 (os88disk refused the
    40-character names the window writes), and the three hard disks, each
    through os88disk --verify-hdd and its root read back here - made in
    this tree, where they boot, and again with NO TREE (an empty build/),
    where they are formatted and do not: no kernel, the partition not
    active, os88disk's not-bootable stub in the MBR and the VBR"""
    v88 = os.path.join(tmp, "A Long Video Name.V88")
    shutil.copyfile(os.path.join(tmp, "T0.V88"), v88)
    want = G.short83(v88)
    empty = os.path.join(tmp, "notree")
    os.makedirs(empty, exist_ok=True)
    runs = [(i, None) for i in range(len(G.DISKS))] + \
        [(i, empty) for i, d in enumerate(G.DISKS) if d[1] == "hd"]
    for i, build in runs:
        label, kind, what = G.DISKS[i]
        if kind == "fd" and what not in (360, 1440):
            continue
        if build:
            label += ", no tree"
        with tempfile.TemporaryDirectory() as stage:
            cmd, img = G.disk_argv(v88, i, stage=stage, build=build)
            p = subprocess.run(cmd, capture_output=True, text=True)
        vf = "--verify" if kind == "fd" else "--verify-hdd"
        ls = subprocess.run([sys.executable,
                             os.path.join(ROOT, "tools", "os88disk.py"),
                             vf, img], capture_output=True, text=True)
        if kind == "fd":
            names = subprocess.run([sys.executable, os.path.join(
                ROOT, "tools", "os88fat.py"), "ls", img],
                capture_output=True, text=True).stdout.upper()
            miss = [n for n in (want, "VIDEO.O88") if n not in names]
        else:
            tag, cyls, heads, spt, st11 = what
            names = root_names(img, st11, spt, heads)
            if len(set(names)) != len(names):
                bad.append("%s: a name twice in the root: %s" % (label, names))
            if build:                           # formatted, NOT bootable
                miss = [n for n in (want,) if n not in names]
                d = open(img, "rb").read()
                o = spt * heads * 512 if st11 else 0
                lba = struct.unpack_from("<I", d, o + 446 + 8)[0]
                stub = b"Not a bootable disk"
                why = [w for w, ok in (
                    ("a KERNEL.SYS", "KERNEL.SYS" not in names),
                    ("the partition active", d[o + 446] == 0),
                    ("no stub in the MBR", stub in d[o:o + 446]),
                    ("no stub in the VBR",
                     stub in d[o + lba * 512:o + lba * 512 + 510]))
                    if not ok]
                if why:
                    bad.append("%s: meant not to boot, but %s"
                               % (label, ", ".join(why)))
            else:
                miss = [n for n in (want, "KERNEL.SYS", "HDD.DRV",
                                    "VIDEO.O88") if n not in names]
            size = os.path.getsize(img)
            if size != cyls * heads * spt * 512 + 512 or \
                    open(img, "rb").read()[-512:][:8] != b"conectix":
                bad.append("%s: %d bytes, not a fixed VHD of %d/%d/%d"
                           % (label, size, cyls, heads, spt))
        print("   make a disk, %s: %s%s" % (
            label, "made, verified" if not (p.returncode or ls.returncode)
            else "FAILED", ", the root without %s" % miss if miss else ""))
        if p.returncode or ls.returncode or miss:
            bad.append("make a disk, %s: %s" % (
                label, (p.stderr + ls.stderr).strip() or "missing %s" % miss))
        os.remove(img)


def ffmpegs():
    """ffmpeg processes whose parent is this one"""
    me, out = str(os.getpid()), []
    for pid in os.listdir("/proc"):
        try:
            st = open("/proc/%s/stat" % pid).read()
        except (OSError, ValueError):
            continue
        name = st[st.index("(") + 1:st.rindex(")")]
        if name == "ffmpeg" and st[st.rindex(")") + 2:].split()[1] == me:
            out.append(pid)
    return out


def leg8(tmp, bad):
    src = os.path.join(tmp, "src4.mp4")
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi",
                    "-i", "testsrc2=duration=4:size=320x240:rate=15", src],
                   check=True)
    out = os.path.join(tmp, "P.V88")
    argv = [src, out, "--preset", "cga", "--quiet"]
    seen = []
    V.encode(V.parser().parse_args(argv),
             progress=lambda st, d, t: seen.append((st, d, t)))
    order = [st for st, d, t in seen]
    steps = [n for n, _ in V.STEPS]
    firsts = sorted(set(order), key=order.index)
    fr = [G.overall(*x) for x in seen]
    back = sum(1 for a, b in zip(fr, fr[1:]) if b < a - 1e-9)
    enc = [d for st, d, t in seen if st == "encode"]
    print("   8: %d reports, steps %s, the bar %.3f -> %.3f, %d step(s) "
          "back; encode frames %d..%d of %d"
          % (len(seen), " ".join(firsts), fr[0], fr[-1], back,
             min(enc), max(enc), seen[-2][2] if len(seen) > 1 else 0))
    if firsts != [n for n in steps if n in firsts] or \
            set(firsts) != set(steps) or back or \
            fr[-1] < G.ENCODE_SHARE * 0.97:
        bad.append("8: the progress went %s, %d steps back, ending at %.3f"
                   % (" ".join(firsts), back, fr[-1]))
    old = b"an older file, left alone"
    for when in ("read", "encode"):
        with open(out, "wb") as f:
            f.write(old)

        def stop(st, d, t, when=when):
            if st == when and d >= 10:
                raise V.Cancelled()
        try:
            V.encode(V.parser().parse_args(argv), progress=stop)
            got = "finished"
        except V.Cancelled:
            got = "cancelled"
        left = ffmpegs()
        same = open(out, "rb").read() == old
        print("   8: cancelled in %s: %s, the old file %s, %d ffmpeg left"
              % (when, got, "untouched" if same else "CHANGED", len(left)))
        if got != "cancelled" or not same or left:
            bad.append("8: a cancel in %s: %s, the old file %s, ffmpeg %s "
                       "left" % (when, got, "kept" if same else "changed",
                                 left))
    os.remove(out)


def leg9(tmp, bad):
    import numpy as np
    src = os.path.join(tmp, "src4.mp4")
    out = os.path.join(tmp, "K.V88")
    V.encode(V.parser().parse_args([src, out, "--preset", "cga",
                                    "--keysecs", "0.8", "--quiet"]))
    logo = os.path.join(tmp, "LOGO.V88")
    shutil.copy(os.path.join(ROOT, "apps", "video", "os8088.v88"), logo)
    for path in (out, logo):
        r = vid.Reader(path)
        before = open(path, "rb").read()
        want = r.nkeys - 1 if r.poster != r.nkeys - 1 else 0
        f = r.keys[want][0]
        ps = vid.set_poster(path, f + 1)    # the key a play from f+1 takes
        after = open(path, "rb").read()
        slots = {vid.Reader(path, i).slot + o for i in range(r.nrend)
                 for o in (14, 15)}
        diff = [i for i in range(len(before)) if before[i] != after[i]]
        vid.verify_v88(path)
        now = [vid.Reader(path, i).poster for i in range(r.nrend)]
        print("   9: %s: %d keys, %d rendition(s): poster %d -> %s, %d "
              "byte(s) changed, all in poster words: %s"
              % (os.path.basename(path), r.nkeys, r.nrend, r.poster,
                 "/".join(map(str, now)), len(diff),
                 set(diff) <= slots))
        if not set(diff) <= slots or not diff or \
                any(p != vid.key_at(vid.Reader(path, i), f)
                    for i, p in enumerate(now)) or ps != now or \
                now[0] != want:
            bad.append("9: %s: posters %s for key %d, changed bytes %s"
                       % (path, now, want, diff[:8]))
        r = vid.Reader(path)
        G.file_facts(r, path)
        first = r.keys[0][0]
        if vid.key_at(r, max(0, first - 1)) != 0:
            bad.append("9: a frame before the first key is not key 0's")
        for i, (k, *_x) in enumerate(r.keys):
            j, kk, img = G.key_view(r, k + (1 if k + 1 < r.frames else 0))
            if (i, k) != (j, kk):
                bad.append("9: frame %d's key is %d, not %d" % (k + 1, j, i))
            for fno, sf, rec, at, n in vid.v88_frames(r):
                if fno == k:
                    fr = G.render(r, sf)
                    break
            if not np.array_equal(np.asarray(fr), np.asarray(img)):
                bad.append("9: %s key %d is not drawn as frame %d is"
                           % (os.path.basename(path), i, k))
        # ...and THE TITLE, IN PLACE: a long one cut to the field, a short
        # one after it leaving no tail, a control character a '?' - each
        # changing header bytes 32..79 and no other, the file verifying
        field = set(range(vid.TITLE_AT, vid.TITLE_AT + vid.TITLE_LEN))
        for want, title in (("L" * 47, "L" * 60), ("Short?one", "Short\tone")):
            before = open(path, "rb").read()
            got = vid.set_title(path, title)
            after = open(path, "rb").read()
            diff = {i for i in range(len(before)) if before[i] != after[i]}
            vid.verify_v88(path)
            r = vid.Reader(path)
            raw = after[vid.TITLE_AT:vid.TITLE_AT + vid.TITLE_LEN]
            ok = got == want and r.title == want and diff and \
                diff <= field and raw == want.encode() + bytes(
                    vid.TITLE_LEN - len(want))
            print("   9: %s: title '%s' -> '%s', %d byte(s) changed, all in "
                  "the title field: %s" % (
                      os.path.basename(path), title[:12], r.title[:12],
                      len(diff), diff <= field))
            if not ok:
                bad.append("9: %s: title %r set as %r (header %r), changed "
                           "bytes %s" % (os.path.basename(path), title,
                                         r.title, raw, sorted(diff)[:8]))


def leg10(tmp, bad):
    import numpy as np
    src = os.path.join(tmp, "noise.mp4")   # every pixel differs from its
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi",
                    "-i", "testsrc2=duration=2:size=320x240:rate=15,"
                    "noise=alls=60:allf=t", src], check=True)  # neighbour
    out = os.path.join(tmp, "PRE.V88")
    V.encode(V.parser().parse_args([src, out,
                                    "--preset", "vga8", "--detail", "1x1",
                                    "--profile", "286-vga",
                                    "--audio", "none", "--quiet"]))
    r, frames = G.preview_frames(out)
    k0 = r.keys[0][0]
    first = frames[0][0]
    same = np.array_equal(np.asarray(frames[0][1]),
                          np.asarray(G.key_view(r, k0)[2]))
    print("  10: VGA8 pre-rolled %d frame(s); the preview starts at frame %d, "
          "%s key 0's picture" % (k0, first, "which is" if same else
                                  "NOT"))
    im = np.asarray(frames[0][1])
    lit = (im != 0).any(2).mean()
    print("      %.1f%% of it lit: a full-bleed picture is painted in, not "
          "skipped whole (98.2.9.1)" % (100 * lit))
    if not k0 or first != k0 or not same or lit < 0.9:
        bad.append("10: the preview starts at frame %d, key 0 is at %d%s"
                   % (first, k0, "" if same else ", a different picture"))


def group(pgid):
    """processes in a process group, other than zombies"""
    out = []
    for pid in os.listdir("/proc"):
        try:
            st = open("/proc/%s/stat" % pid).read()
        except (OSError, ValueError):
            continue
        f = st[st.rindex(")") + 2:].split()
        if f[0] != "Z" and f[2] == str(pgid):
            out.append(pid)
    return out


def leg12(tmp, bad):
    import time
    src = os.path.join(tmp, "src4.mp4")
    out = os.path.join(tmp, "J.V88")
    ev = list(G.EncodeJob([src, out, "--preset", "cga"]).start().events())
    steps = [v[0] for k, v in ev if k == "prog"]
    firsts = sorted(set(steps), key=steps.index)
    rc = ev[-1][1]
    ok = rc == 0 and os.path.exists(out)
    if ok:
        vid.verify_v88(out)
    print("  12: a job run whole: exit %d, %d progress lines, steps %s, "
          "%d log lines" % (rc, len(steps), " ".join(firsts),
                            sum(1 for k, v in ev if k == "log")))
    if not ok or firsts != [n for n, _ in V.STEPS]:
        bad.append("12: a whole job exited %d with steps %s" % (rc, firsts))
    long_src = os.path.join(tmp, "long.mp4")
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi",
                    "-i", "mandelbrot=size=320x240:rate=30", "-t", "20",
                    long_src], check=True)
    old = b"an older file, left alone"
    with open(out, "wb") as f:
        f.write(old)
    j = G.EncodeJob([long_src, out, "--preset", "modex", "--profile",
                     "286-vga"]).start()
    seen = None
    for kind, val in j.events():
        if kind == "prog" and val[0] in ("read", "encode") and val[1] > 5:
            seen = val
            break
    t0 = time.time()
    j.cancel()
    j.leftover(out)
    dt = time.time() - t0
    left = group(j.p.pid)
    same = open(out, "rb").read() == old
    part = os.path.exists(out + ".part")
    print("  12: cancelled at %s: gone in %.2f s, %d process(es) of its group "
          "left, the old file %s, a .part %s" % (
              seen, dt, len(left), "untouched" if same else "CHANGED",
              "LEFT" if part else "none"))
    if seen is None or j.p.poll() is None or dt > 2.0 or left or not same \
            or part:
        bad.append("12: a cancel at %s took %.2f s and left %s, the old "
                   "file %s" % (seen, dt, left, "kept" if same else
                                "changed"))


if __name__ == "__main__":
    sys.exit(main())
