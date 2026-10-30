#!/usr/bin/env python3
"""THE ENCODER GUIDE'S PICTURES, MADE AGAIN (SPEC.md 98.2.13).

    python3 tools/os88vguide.py [--work DIR] [--only clip|encode|frames|window]

docs/encoder-guide/GUIDE.HTML is the user's guide `make vencbundle` ships
beside the encoder, and its pictures in guide/ are of two kinds, both made
here rather than by hand, so that a change to the window or to a format is
one command away from a guide that shows it:

  FRAMES   real .V88 frames as the screen shows them: one sample clip
           encoded for every "Made for" target and for the comparisons the
           guide makes (dither, fit, tone, budget, CGA's sets, text glyphs,
           sixteen greys), each frame drawn by the window's own `render`
           with neither dimension shrunk - the short side is stretched to
           the pixel's shape, then the picture is scaled up a whole number
           of times, nearest, so a dither stays a dither.
  WINDOW   the encoder window itself, driven under X (DISPLAY, or an Xvfb
           this starts) one scene a process, and grabbed with Pillow.

THE CLIP is twelve seconds made from three of PiXEL's sample pictures
(apps/pixel/samples/README.TXT: original, made for this project), a zoom in,
a zoom out and a pan, with a generated tune: three scenes, two cuts, nothing
from anywhere else. It and the encodes go in --work (default build/vguide/);
the window shows that folder's PATH in its Input line, so `--work ~/Videos`
makes the screenshots read like a user's.

Then GUIDE.HTML's width and height attributes are set from the pictures
(no layout jump as they load), and the window's pictures are cut to 128
colours. Needs ffmpeg, numpy, Pillow and, for WINDOW, Tk and Xvfb.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
GUIDE = os.path.join(ROOT, "docs", "encoder-guide")
PICS = os.path.join(GUIDE, "guide")
SAMPLES = os.path.join(ROOT, "apps", "pixel", "samples")
GEOM = "1300x860"               # the window, a little wider than its own
                                # 1080, so no tab's groups are cut off

# the "Made for" targets the gallery shows: TARGETS index -> picture
GALLERY = {0: "cga-mono", 1: "cga-4", 2: "cga-16", 3: "cga-composite",
           4: "cga-512", 5: "text-colour", 6: "text-mono", 7: "herc",
           8: "speaker", 9: "floppy", 10: "vga-16", 11: "vga-256",
           12: "modex", 15: "live-cga", 16: "live-herc", 17: "live-vga"}
# the comparisons: name -> the encoder's options
EXTRA = {
    "dith-bayer": ["--preset", "cga", "--dither", "bayer"],
    "dith-blue": ["--preset", "cga", "--dither", "bluenoise"],
    "dith-thr": ["--preset", "cga", "--dither", "threshold"],
    "fit-fit": ["--preset", "herc", "--fit", "fit"],
    "fit-fill": ["--preset", "herc", "--fit", "fill"],
    "fit-stretch": ["--preset", "herc", "--fit", "stretch"],
    "prof-floppy": ["--preset", "cga", "--profile", "floppy"],
    "prof-st225": ["--preset", "cga", "--profile", "5150-st225"],
    "prof-lossless": ["--preset", "cga", "--profile", "lossless"],
    "tone-dark": ["--preset", "cga", "--gamma", "0.7"],
    "tone-bright": ["--preset", "cga", "--gamma", "1.5"],
    "cga4-p0": ["--preset", "cga4", "--cga-palette", "0", "--cga-bright", "1"],
    "cga4-p1": ["--preset", "cga4", "--cga-palette", "1", "--cga-bright", "1"],
    "cgamix-all": ["--preset", "cga4", "--cga-palette", "2",
                   "--cga-bright", "1", "--cga-bg", "0"],
    "cgamix-hue": ["--preset", "cga4", "--cga-palette", "2",
                   "--cga-bright", "1", "--cga-bg", "0", "--colour-mix",
                   "hue"],
    "cgamix-solid": ["--preset", "cga4", "--cga-palette", "2",
                   "--cga-bright", "1", "--cga-bg", "0", "--colour-mix",
                   "hue", "--colour-clip", "255", "--edge-solid", "48"],
    "text-ascii": ["--preset", "text", "--text-glyphs", "ascii"],
    "text-blocks": ["--preset", "text", "--text-glyphs", "blocks"],
    "vga4-grey": ["--preset", "vga4", "--pixfmt", "vga4", "--profile",
                  "286-fast", "--palette", "grey16"],
}
# picture -> (encode, seconds into the clip)
FRAMES = dict(
    [("t-" + v, ("t%02d" % k, 10.0)) for k, v in GALLERY.items()] + [
        ("cmp-dither-bayer", ("dith-bayer", 2.0)),
        ("cmp-dither-bluenoise", ("dith-blue", 2.0)),
        ("cmp-dither-threshold", ("dith-thr", 2.0)),
        ("cmp-fit-fit", ("fit-fit", 6.0)),
        ("cmp-fit-fill", ("fit-fill", 6.0)),
        ("cmp-fit-stretch", ("fit-stretch", 6.0)),
        ("cmp-tone-dark", ("tone-dark", 2.0)),
        ("cmp-tone-normal", ("t00", 2.0)),
        ("cmp-tone-bright", ("tone-bright", 2.0)),
        ("cmp-cga4-auto", ("t01", 10.0)),
        ("cmp-cga4-p0", ("cga4-p0", 10.0)),
        ("cmp-cga4-p1", ("cga4-p1", 10.0)),
        ("cmp-cgamix-all", ("cgamix-all", 6.0)),
        ("cmp-cgamix-hue", ("cgamix-hue", 6.0)),
        ("cmp-cgamix-solid", ("cgamix-solid", 6.0)),
        ("cmp-text-blocks", ("text-blocks", 10.0)),
        ("cmp-text-ascii", ("text-ascii", 10.0)),
        ("cmp-vga4-grey16", ("vga4-grey", 10.0)),
        # a tenth of a second after the cut to the desk: the budget's
        ("cmp-budget-floppy", ("prof-floppy", 8.1)),
        ("cmp-budget-floppysmall", ("t09", 8.1)),
        ("cmp-budget-st225", ("prof-st225", 8.1)),
        ("cmp-budget-lossless", ("prof-lossless", 8.1))])


# --------------------------------------------------------------------------
# the clip, and its encodes
# --------------------------------------------------------------------------
def make_clip(work):
    """work/sample.mp4: three scenes of four seconds and a tune"""
    out = os.path.join(work, "sample.mp4")
    moves = (("VACATION", "zoom+0.0012", "iw/2-(iw/zoom/2)"),
             ("FLOWER", "if(eq(on,0),1.25,zoom-0.0012)", "iw/2-(iw/zoom/2)"),
             ("ROOM", "1.15", "(iw-iw/zoom)*on/119"))
    parts = []
    for i, (pic, z, x) in enumerate(moves):
        p = os.path.join(work, "scene%d.mp4" % i)
        subprocess.run(
            ["ffmpeg", "-loglevel", "error", "-y", "-loop", "1", "-i",
             os.path.join(SAMPLES, pic + ".JPG"), "-vf",
             "scale=1920:1440,zoompan=z='%s':x='%s':y='ih/2-(ih/zoom/2)'"
             ":d=120:s=640x480:fps=30" % (z, x), "-frames:v", "120",
             "-pix_fmt", "yuv420p", p], check=True)
        parts.append(p)
    lst = os.path.join(work, "scenes.txt")
    open(lst, "w").write("".join("file '%s'\n" % p for p in parts))
    tune = ("0.25*sin(2*PI*t*(220*pow(2,floor(mod(t*4,8))/12*2)))"
            "*exp(-mod(t*4,1)*3)+0.15*sin(2*PI*110*t)")
    subprocess.run(
        ["ffmpeg", "-loglevel", "error", "-y", "-f", "concat", "-safe", "0",
         "-i", lst, "-f", "lavfi", "-i",
         "aevalsrc='%s':s=22050:d=12" % tune, "-c:v", "libx264", "-crf",
         "18", "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest", out],
        check=True)
    return out


def encodes(src, work):
    """[(name, argv)]: every gallery target as the WINDOW builds its
    command line, and the comparisons"""
    import os88vencgui as G
    _, vals = G.suggest(src)
    out = []
    for i in GALLERY:
        v = G.target_fill(i, 30.0)
        v.update(vals, title="Sample")
        out.append(("t%02d" % i, G.argv_from(
            src, os.path.join(work, "t%02d.V88" % i), v, 30.0)))
    for k, a in EXTRA.items():
        out.append((k, [src, os.path.join(work, k + ".V88")] + a))
    return out


def encode_all(src, work, cores):
    def one(job):
        name, argv = job
        v88 = argv[1]
        if os.path.exists(v88) and \
                os.path.getmtime(v88) > os.path.getmtime(src):
            return name, "kept"
        with open(os.path.join(work, name + ".log"), "w") as log:
            r = subprocess.run([sys.executable, os.path.join(
                HERE, "os88venc.py")] + argv + ["--jobs", "2"],
                stdout=log, stderr=subprocess.STDOUT)
        return name, "ok" if r.returncode == 0 else "FAILED (%s.log)" % name
    with ThreadPoolExecutor(max(1, cores // 2)) as ex:
        for name, how in ex.map(one, encodes(src, work)):
            print("os88vguide: encode %-12s %s" % (name, how), flush=True)


# --------------------------------------------------------------------------
# frames: a .V88's frame as the screen shows it, never shrunk
# --------------------------------------------------------------------------
def screen_image(r, surf, f):
    """The window's `render` of frame F, with its aspect squeeze undone:
    the SHORT side stretched to the pixel shape, so no column is dropped"""
    import numpy as np
    from PIL import Image
    import os88vencgui as G
    import os88vid as vid
    g = r.g
    if r.pixfmt == vid.PF_C512:     # (render averages cells itself)
        import os88cgacomp
        rgb = os88cgacomp.render_c512(g.canvas(surf), g.wb, g.h,
                                      new=r.cgapal != vid.CARD_OLD)
        an, ad = r.aspect
        img = Image.fromarray(np.ascontiguousarray(rgb))
        return img.resize((round(g.wb // 2 * an / ad) * 4, g.h * 4),
                          Image.BOX).resize((640, 480), Image.LANCZOS)
    seen = {}
    resize = Image.Image.resize

    def keep(self, size, *a, **k):
        seen["img"] = self
        return resize(self, size, *a, **k)
    Image.Image.resize = keep
    try:
        G.render(r, surf, f)
    finally:
        Image.Image.resize = resize
    img = seen["img"]
    an, ad = r.aspect
    w, h = img.size
    return img.resize((w, round(h * ad / an)) if an < ad else
                      (round(w * an / ad), h), Image.NEAREST)


def one_frame(job):
    from PIL import Image
    import os88vid as vid
    name, v88, secs = job
    r = vid.Reader(v88)
    want = int(round(secs * r.fps))
    for f, surf, *_ in vid.v88_frames(r):
        if f == want:
            im = screen_image(r, surf, f).convert("RGB")
            k = max(1, -(-300 // im.size[1]))
            im = im.resize((im.size[0] * k, im.size[1] * k), Image.NEAREST)
            n = len(im.getcolors(1 << 16) or ())
            if 0 < n <= 256:
                im = im.quantize(colors=max(2, n),
                                 method=Image.Quantize.FASTOCTREE,
                                 dither=Image.Dither.NONE)
            im.save(os.path.join(PICS, name + ".png"), optimize=True)
            return name
    raise SystemExit("os88vguide: %s has no frame at %gs" % (v88, secs))


def frames_all(work, cores):
    jobs = [(n, os.path.join(work, e + ".V88"), s)
            for n, (e, s) in sorted(FRAMES.items())]
    with ProcessPoolExecutor(cores) as ex:
        for name in ex.map(one_frame, jobs):
            print("os88vguide: frame %s" % name, flush=True)


# --------------------------------------------------------------------------
# the window, a scene a process
# --------------------------------------------------------------------------
SCENES = ("empty", "loaded", "tabs", "colours", "help", "tip", "greyed",
          "ladder", "encoding")


def scene(name, work, xdisp):
    """Run in a process of its own with DISPLAY set: one App, driven a step
    at a time off Tk's own clock, each picture grabbed off the X screen"""
    import tkinter as tk
    from tkinter import ttk
    from PIL import ImageGrab
    import os88vencgui as G
    src = os.path.join(work, "sample.mp4")
    v88 = os.path.join(work, "sample.V88")
    root = G.make_root()
    app = G.App(root)
    root.geometry(GEOM + "+0+0")
    app.info.set("Choose a video, or drop one on this window.")

    def walk(w):
        yield w
        for c in w.winfo_children():
            yield from walk(c)
    nb = next(w for w in walk(root) if isinstance(w, ttk.Notebook))

    def grab(pic, w=None, size=None):
        root.event_generate("<Motion>", warp=True, x=2000, y=2000)
        root.update_idletasks()
        root.update()
        time.sleep(0.3)
        root.update()
        w = w or root
        x, y = w.winfo_rootx(), w.winfo_rooty()
        ww, hh = size or (w.winfo_width(), w.winfo_height())
        ImageGrab.grab(bbox=(x, y, x + ww, y + hh), xdisplay=xdisp).save(
            os.path.join(PICS, pic + ".png"))
        print("os88vguide: window %s" % pic, flush=True)

    def target(start):
        i = next(i for i, t in enumerate(G.TARGETS) if t[0].startswith(start))
        app.target.set(G.TARGETS[i][0])
        app.apply_target()

    def tab(t):
        nb.select(G.TABS.index(t))

    steps = [lambda: app.set_src(src)]
    if name == "empty":
        steps = [lambda: grab("window-empty")]
    elif name in ("loaded", "tabs"):
        steps += [lambda: (app.load_v88(v88), "wait")[1],
                  lambda: app.scrub.set(150)]
        if name == "loaded":
            steps.append(lambda: grab("window"))
        for t in ("Basic", "Picture", "Sound", "Budget", "Trade-offs"):
            if name == "tabs":
                steps += [lambda t=t: tab(t), lambda t=t: grab(
                    "tab-" + t.lower().replace("-", ""), nb)]
    elif name == "colours":
        steps.append(lambda: tab("Colour"))
        for start, pic in (("IBM 5150/XT, CGA - 4 colours", "colour-cga4"),
                           ("286, VGA - 16 colours in", "colour-vga4")):
            steps += [lambda s=start: target(s),
                      lambda p=pic: grab(p, nb)]
    elif name == "help":
        def helpwin():
            f = next(f for f in G.fields() if f["dest"] == "profile")
            app.choices_help(f, app.vars["profile"])

        def take():
            top = [w for w in root.winfo_children()
                   if isinstance(w, tk.Toplevel)][-1]
            grab("help-profile", top)
            top.destroy()
        steps += [helpwin, take]
    elif name == "tip":
        def hover():
            cb = next(w for w in walk(root) if isinstance(w, ttk.Combobox)
                      and w.cget("values") and str(w.cget("values")[0])
                      == G.TARGETS[0][0])
            cb.event_generate("<Enter>")

        def take():                     # (no warp: the tip follows Leave)
            root.update()
            time.sleep(0.3)
            ImageGrab.grab(bbox=(root.winfo_rootx(), root.winfo_rooty(),
                                 root.winfo_rootx() + 780,
                                 root.winfo_rooty() + 210),
                           xdisplay=xdisp).save(os.path.join(PICS,
                                                             "tip.png"))
            print("os88vguide: window tip", flush=True)
        steps += [hover, take]
    elif name == "greyed":
        def take():
            root.update()
            time.sleep(0.3)
            x, y = nb.winfo_rootx(), nb.winfo_rooty()
            ImageGrab.grab(bbox=(x, y, x + 760, y + 420),
                           xdisplay=xdisp).save(os.path.join(PICS,
                                                             "greyed.png"))
            print("os88vguide: window greyed", flush=True)
        steps += [lambda: tab("Colour"),
                  lambda: app.gheads["CGA, 4 colours"].event_generate(
                      "<Enter>"), take]
    elif name == "ladder":
        def tick():
            next(w for w in walk(root) if isinstance(w, ttk.Checkbutton)
                 and "degrade ladder" in str(w.cget("text"))).invoke()
        steps += [lambda: target("286, VGA - 16 colours in"),
                  lambda: tab("Degrade to Fit"), tick,
                  lambda: grab("tab-ladder", nb)]
    elif name == "encoding":
        steps += [lambda: target("286, VGA - 16 colours in"),
                  lambda: app.out.set(os.path.join(work, "sample-vga.V88")),
                  lambda: (app.encode(), "encode")[1],
                  lambda: grab("encoding"), lambda: app.stop()]

    def run(i=0):
        if i >= len(steps):
            root.after(500, root.destroy)
            return
        r = steps[i]()
        if r == "wait":                 # a load: until the window is idle
            def poll():
                if app.busy:
                    root.after(300, poll)
                else:
                    root.after(800, lambda: run(i + 1))
            poll()
        else:                           # an encode: let the bar move
            root.after(15000 if r == "encode" else 700, lambda: run(i + 1))
    root.after(1500, run)
    root.mainloop()


def window_all(work):
    xvfb = None
    disp = os.environ.get("DISPLAY")
    if not disp:
        if not shutil.which("Xvfb"):
            sys.exit("os88vguide: no DISPLAY and no Xvfb to make one")
        disp = ":%d" % (90 + os.getpid() % 9)
        xvfb = subprocess.Popen(["Xvfb", disp, "-screen", "0",
                                 "1400x1400x24"], stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        time.sleep(2)
    # the window's file is the CGA target's, under the name a user's has
    shutil.copy(os.path.join(work, "t00.V88"),
                os.path.join(work, "sample.V88"))
    try:
        for s in SCENES:
            subprocess.run([sys.executable, os.path.abspath(__file__),
                            "--scene", s, "--work", work, "--xdisplay", disp],
                           check=True, env=dict(os.environ, DISPLAY=disp),
                           timeout=600)
    finally:
        if xvfb:
            xvfb.terminate()
    from PIL import Image
    for n in os.listdir(PICS):
        if n.startswith(("t-", "cmp-")):
            continue
        p = os.path.join(PICS, n)
        Image.open(p).convert("RGB").quantize(
            colors=128, method=Image.Quantize.MEDIANCUT,
            dither=Image.Dither.NONE).save(p, optimize=True)


def sizes():
    """GUIDE.HTML's width= and height= from the pictures themselves"""
    from PIL import Image
    page = os.path.join(GUIDE, "GUIDE.HTML")
    text = open(page, encoding="utf-8").read()

    def fix(m):
        tag = re.sub(r'\s(?:width|height)="\d+"', "", m.group(0))
        src = re.search(r'src="([^"]+)"', tag).group(1)
        w, h = Image.open(os.path.join(GUIDE, src)).size
        return tag.replace("<img ", '<img width="%d" height="%d" ' % (w, h),
                           1)
    new = re.sub(r"<img [^>]*>", fix, text)
    if new != text:
        open(page, "w", encoding="utf-8").write(new)
        print("os88vguide: GUIDE.HTML's picture sizes set")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--work", default=os.path.join(ROOT, "build", "vguide"),
                    help="where the clip and its encodes go (default "
                    "build/vguide); its path shows in the window's pictures")
    ap.add_argument("--only", choices=("clip", "encode", "frames", "window"),
                    help="one step (each needs the ones before it to have "
                    "run once)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    ap.add_argument("--scene", choices=SCENES, help=argparse.SUPPRESS)
    ap.add_argument("--xdisplay", help=argparse.SUPPRESS)
    a = ap.parse_args()
    work = os.path.abspath(os.path.expanduser(a.work))
    if a.scene:
        return scene(a.scene, work, a.xdisplay)
    os.makedirs(work, exist_ok=True)
    os.makedirs(PICS, exist_ok=True)
    src = os.path.join(work, "sample.mp4")
    if a.only in (None, "clip") or not os.path.exists(src):
        make_clip(work)
    if a.only in (None, "encode"):
        encode_all(src, work, a.jobs)
    if a.only in (None, "frames"):
        frames_all(work, a.jobs)
    if a.only in (None, "window"):
        window_all(work)
    sizes()
    return 0


if __name__ == "__main__":
    sys.exit(main())
