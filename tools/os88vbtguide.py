#!/usr/bin/env python3
"""THE BATTLE BUILDER GUIDE'S PICTURES, MADE AGAIN.

    python3 tools/os88vbtguide.py [--work DIR] [--only footage|window|os8088]

docs/battle-guide/GUIDE.HTML is the user's guide `make vbtbundle` ships
beside the Video Battle builder (tools/os88vbtbundle.py), and its pictures
in guide/ are made here rather than by hand, so that a change to the window
is one command away from a guide that shows it:

  FOOTAGE  the ORIGINAL demo battle (tools/vbtscene.py: drawn and scored for
           this project, nothing captured) written out as ordinary files -
           an .mp4 a scene, a .wav a sound, a .png title picture - and a
           project, BEETLE.vbp, that uses those FILES as its sources, so the
           window shows the names a user's own footage would have. The
           project is the demo's (os88vbt.demo_project) with its scenes
           swapped for the files and a VGA rendition asked for. Its frames
           are read once here, so the window finds them cached.
  WINDOW   the builder's window (tools/os88vbtgui.py), driven under X
           (DISPLAY, or an Xvfb this starts) one scene a process, each
           picture grabbed with Pillow and cut to 128 colours.
  OS8088   the battle on os8088, off MartyPC: the title picture's window and
           the battle with its menu on a CGA XT (build/vbattle.img), and the
           VGA demo (build/vbattlev.img) on a VGA XT. Needs `make vbattledisk`.

Then GUIDE.HTML's width and height attributes are set from the pictures (no
layout jump as they load). Needs ffmpeg, numpy, Pillow, Tk and Xvfb; the
os8088 step needs `make marty`.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
GUIDE = os.path.join(ROOT, "docs", "battle-guide")
PICS = os.path.join(GUIDE, "guide")
PROJECT = "BEETLE.vbp"
SCENES = ("new", "battle", "idle", "clips", "openings", "loops", "actions",
          "party", "enemy", "sounds", "build", "palette")


# --------------------------------------------------------------------------
# the footage: the demo's scenes as files, and a project of them
# --------------------------------------------------------------------------
def write_wav(path, x, rate):
    import numpy as np
    pcm = (np.clip(x, -1, 1) * 32000).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def write_mp4(path, frames, fps):
    """FRAMES (CGA4 indexes) as an ordinary .mp4, twice the size - what a
    capture of the scene would be. Lossless (qp 0, 4:4:4), so the builder
    reads back the scene's own pixels"""
    import numpy as np
    import vbtscene as S
    h, w = frames[0].shape
    p = subprocess.Popen(
        ["ffmpeg", "-loglevel", "error", "-y", "-f", "rawvideo",
         "-pix_fmt", "rgb24", "-s", "%dx%d" % (w * 2, h * 2),
         "-r", str(fps), "-i", "-", "-c:v", "libx264", "-qp", "0",
         "-pix_fmt", "yuv444p", path], stdin=subprocess.PIPE)
    for f in frames:
        rgb = S.to_rgb(np.asarray(f, np.uint8))
        p.stdin.write(np.repeat(np.repeat(rgb, 2, 0), 2, 1).tobytes())
    p.stdin.close()
    if p.wait() != 0:
        sys.exit("os88vbtguide: ffmpeg could not write %s" % path)


def footage(work):
    import numpy as np
    from PIL import Image
    import vbtana as A
    import vbtscene as S
    import os88vbt as B
    src = os.path.join(work, "footage")
    os.makedirs(src, exist_ok=True)
    scenes = ["title", "reveal"] + [c[0] for c in S.CLIPS]
    for n in scenes:
        write_mp4(os.path.join(src, n + ".mp4"), A._scene(n), S.FPS)
    # the idle footage is four of the loop's turns, so there is a loop to
    # find in it, as there is in a recording of a game standing still
    write_mp4(os.path.join(src, "idle.mp4"), A._scene("idle") * 4, S.FPS)
    for n in S.SFX + S.UI_SFX:
        write_wav(os.path.join(src, n + ".wav"), S.sfx(n), S.RATE)
    write_wav(os.path.join(src, "music.wav"), S.music(16), S.RATE)
    t = A._scene("title")[30]
    Image.fromarray(np.repeat(np.repeat(S.to_rgb(np.asarray(t, np.uint8)),
                                        2, 0), 2, 1)).save(
        os.path.join(src, "cover.png"))
    p = B.demo_project(None)
    d = p.d
    d["name"] = "The Iron Beetle"
    for sid, s in list(d["sources"].items()):
        d["sources"][sid] = {"kind": "video",
                             "path": "footage/%s.mp4" % s["name"]}
    for sid, s in list(d["sounds"].items()):
        if s["kind"] == "scene":
            d["sounds"][sid] = {"kind": "file", "path": "footage/%s.wav"
                                % s["name"], "level": s.get("level", 1.0)}
    d["target"]["renditions"] = ["cga4", "vga4"]
    d["target"]["vga_colours"] = 8
    d["cover"] = {"image": "footage/cover.png",
                  "line": "Click or press Enter to begin"}
    path = os.path.join(work, PROJECT)
    p.save(path)
    print("os88vbtguide: %s and its footage" % path, flush=True)
    # every source read once, as the window's first look would
    import vbtproj as P
    proj = P.Project.load(path)
    for sid in proj.d["sources"]:
        A.screens(proj, sid)
        print("os88vbtguide: read %s" % sid, flush=True)
    return path


# --------------------------------------------------------------------------
# the window, a scene a process
# --------------------------------------------------------------------------
def scene(name, work, xdisp):
    """Run in a process of its own with DISPLAY set: one App, driven a step
    at a time off Tk's own clock, each picture grabbed off the X screen"""
    from PIL import ImageGrab
    import os88vbtgui as G
    G.recent_add = lambda path: None        # (the user's own list untouched)
    proj = os.path.join(work, PROJECT)
    app = G.App(None if name == "new" else proj)
    root = app.root
    root.geometry("+0+0")

    def grab(pic, w=None):
        root.event_generate("<Motion>", warp=True, x=3000, y=3000)
        root.update_idletasks()
        root.update()
        time.sleep(0.4)
        root.update()
        w = w or root
        x, y = w.winfo_rootx(), w.winfo_rooty()
        ww, hh = w.winfo_width(), w.winfo_height()
        ImageGrab.grab(bbox=(x, y, x + ww, y + hh), xdisplay=xdisp).save(
            os.path.join(PICS, pic + ".png"))
        print("os88vbtguide: window %s" % pic, flush=True)

    def tab(t):
        app.nb.select(t.f)

    def listpick(lb, i):
        lb.selection_clear(0, "end")
        lb.selection_set(i)
        lb.see(i)
        lb.event_generate("<<ListboxSelect>>")

    steps = []
    if name == "new":                   # a NEW project's checks: all it lacks
        bt = app.build_tab
        steps = [lambda: tab(bt), lambda: grab("build-checks", bt.chk.master)]
    elif name == "battle":
        b = app.battle_tab
        steps = [lambda: tab(b), lambda: grab("tab-battle"),
                 lambda: b.scroll.cv.yview_moveto(1.0),
                 lambda: (b._find(), "wait")[1],
                 lambda: grab("battle-music", b.mf)]
    elif name == "idle":
        it = app.idle_tab
        steps = [lambda: tab(it), lambda: (it._pick(recommend=True), "wait")[1],
                 lambda: listpick(it.cl, 0),
                 lambda: it.scrub.set(it.preview_loop()[0] + 9),
                 lambda: grab("tab-idle"),
                 lambda: (it.diff.set(1), it._show_seam()),
                 lambda: grab("idle-seam", it.pic)]
    elif name == "clips":
        ct = app.clips_tab

        def sel():
            ct.select("ari_attack")
        steps = [lambda: tab(ct), sel, "wait",
                 lambda: ct.scrub.set(9), lambda: grab("tab-clips")]
    elif name == "openings":
        ot = app.openings_tab
        steps = [lambda: tab(ot), lambda: listpick(ot.lb, 0),
                 lambda: (ot._load(), "wait")[1],
                 lambda: ot.scrub.set(12), lambda: grab("tab-openings")]
    elif name == "loops":
        lt = app.loops_tab
        title = lt.panels[0]
        steps = [lambda: tab(lt), lambda: title._play(), "pause",
                 lambda: grab("tab-loops")]
    elif name == "actions":
        at = app.actions_tab
        steps = [lambda: tab(at),
                 lambda: listpick(at.lb, list(app.proj.d["actions"])
                                  .index("bolt")),
                 lambda: grab("tab-actions")]
    elif name == "party":
        pt = app.party_tab
        steps = [lambda: tab(pt), lambda: listpick(pt.lb, 0),
                 lambda: grab("tab-party")]
    elif name == "enemy":
        steps = [lambda: tab(app.enemy_tab), lambda: grab("tab-enemy")]
    elif name == "sounds":
        steps = [lambda: tab(app.sounds_tab), lambda: grab("tab-sounds")]
    elif name in ("build", "palette"):
        bt = app.build_tab

        def pick_clip():
            kids = bt.tv.get_children()
            names = [bt.tv.item(k, "text") for k in kids]
            k = kids[names.index("bram_attack")] if "bram_attack" in names \
                else kids[0]
            bt.tv.selection_set(k)
            bt.tv.see(k)
        steps = [lambda: tab(bt), lambda: (bt._build(), "wait")[1]]
        if name == "build":
            steps += [lambda: grab("tab-build")]
        else:
            steps += [lambda: (bt.rendv.set("vga4"), bt._rend()), pick_clip,
                      lambda: grab("build-vga", bt.pal_c.master.master)]

    def run(i=0):
        if i >= len(steps):
            root.after(500, root.destroy)
            return
        s = steps[i]
        r = s if isinstance(s, str) else s()
        if r == "wait":                 # a job: until the window is idle
            def poll(n=0):
                if app.job is not None or app.pending or n < 4:
                    root.after(300, lambda: poll(n + 1))
                else:
                    root.after(800, lambda: run(i + 1))
            poll()
        else:
            root.after(1200 if r == "pause" else 700, lambda: run(i + 1))
    root.after(1500, run)
    root.mainloop()


def window_all(work):
    xvfb = None
    disp = os.environ.get("DISPLAY")
    if not disp:
        if not shutil.which("Xvfb"):
            sys.exit("os88vbtguide: no DISPLAY and no Xvfb to make one")
        disp = ":%d" % (80 + os.getpid() % 9)
        xvfb = subprocess.Popen(["Xvfb", disp, "-screen", "0",
                                 "1400x1000x24"], stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        time.sleep(2)
    import os88build
    py = os88build.tk_python()      # (a container's newest python3 often
    if not py:                      # has no Tk where python3.12 does)
        sys.exit("os88vbtguide: no python3 here imports tkinter, numpy and "
                 "Pillow's ImageTk")
    try:
        for s in SCENES:
            subprocess.run([py, os.path.abspath(__file__),
                            "--scene", s, "--work", work, "--xdisplay", disp],
                           check=True, env=dict(os.environ, DISPLAY=disp),
                           timeout=900)
    finally:
        if xvfb:
            xvfb.terminate()
    from PIL import Image
    for n in os.listdir(PICS):
        if n.startswith("os-"):
            continue
        p = os.path.join(PICS, n)
        Image.open(p).convert("RGB").quantize(
            colors=128, method=Image.Quantize.MEDIANCUT,
            dither=Image.Dither.NONE).save(p, optimize=True)


# --------------------------------------------------------------------------
# the battle on os8088 (MartyPC)
# --------------------------------------------------------------------------
def os8088_all(work):
    """The guide project's own bundles on os8088, off a hard disk as the
    walkthrough makes one: the title picture's window, the title loop, the
    battle's menu, and an action playing - on a CGA XT and a VGA XT, each
    with a Sound Blaster"""
    import os88build
    import os88marty
    import os88ui
    from PIL import Image
    tmpl = "build/martypc/run/media/hdds/default_xtide.vhd"
    if not os.path.exists(os.path.join(ROOT, tmpl)):
        sys.exit("os88vbtguide: no %s - `make marty`" % tmpl)
    arms = (("os8088_xt_hdd_sb", "BATTLE.VBT", "os-cga",
             ("window", "title", "menu", "action")),
            ("os8088_xt_vga_hdd_sb", "BATTLEV.VBT", "os-vga",
             ("action",)))
    for machine, bundle, pre, want in arms:
        src = os.path.join(work, bundle)
        if not os.path.exists(src):
            sys.exit("os88vbtguide: no %s - the window step builds it" % src)
        vhd = os.path.join(work, pre + ".vhd")
        subprocess.run(
            [sys.executable, os.path.join(HERE, "os88hdd.py"), "--template",
             tmpl, "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
             "--vbr", os88build.at("build/boothd.bin"),
             "--mbr", os88build.at("build/mbr.bin"),
             "--file", "HDD.DRV=" + os88build.at("build/hdd.drv"),
             "--file", "CTRL.DRV=" + os88build.at("build/ctrl.drv"),
             "--file", "SOUND.DRV=" + os88build.at("build/sound.drv"),
             "--file", "VBATTLE.O88=" + os88build.at("build/vbattle.o88"),
             "--file", "%s=%s" % (bundle, src)], check=True, cwd=ROOT,
            capture_output=True)
        m = os88marty.launch(None, machine=machine,
                             extra=["--mount", "hd:0:" + vhd], boot=False)
        try:
            m.run()
            ui = os88ui.UI(m)
            ui.up(limit=300)
            os88marty.no_saver(m)
            os88marty.ui_done(m)

            def secs(n):
                m.advance(frames=int(n * 60))

            def key(k):
                m.key(k, up=False)
                m.advance(frames=4)
                m.key(k, down=False)

            def shot(tag):
                if tag not in want:
                    return
                m.pause()
                w, h, data = m.fbuf(0)
                out = os.path.join(PICS, "%s-%s.png" % (pre, tag))
                os88marty.write_png_rgb(out, w, h, data)
                im = Image.open(out).convert("RGB")
                if h <= 200:            # (CGA's 640 x 200: its rows twice,
                    im = im.resize((w, h * 2), Image.NEAREST)   # as a 4:3
                im.save(out, optimize=True)                     # screen)
                print("os88vbtguide: %s" % os.path.basename(out), flush=True)
            ui.path("C:/VBATTLE.O88")
            secs(3)
            shot("window")
            key("Enter")            # the title picture's window: the play
            secs(4)
            shot("title")
            key("Enter")            # the title loop: the fade, the opening
            secs(12)
            shot("menu")
            key("Enter")            # Ari's Attack, at the enemy
            secs(0.6)
            key("Enter")
            secs(1.4)
            shot("action")
        finally:
            m.quit()


def sizes():
    """GUIDE.HTML's width= and height= from the pictures themselves"""
    import re
    from PIL import Image
    page = os.path.join(GUIDE, "GUIDE.HTML")
    if not os.path.exists(page):
        return
    text = open(page, encoding="utf-8").read()

    def fix(m):
        tag = re.sub(r'\s(?:width|height)="\d+"', "", m.group(0))
        src = re.search(r'src="([^"]+)"', tag).group(1)
        p = os.path.join(GUIDE, src)
        if not os.path.exists(p):
            return tag
        w, h = Image.open(p).size
        return tag.replace("<img ", '<img width="%d" height="%d" ' % (w, h),
                           1)
    new = re.sub(r"<img [^>]*>", fix, text)
    if new != text:
        open(page, "w", encoding="utf-8").write(new)
        print("os88vbtguide: GUIDE.HTML's picture sizes set")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--work", default=os.path.join(ROOT, "build", "vbtguide"),
                    help="where the footage and the project go (default "
                    "build/vbtguide); its path shows in the window's pictures")
    ap.add_argument("--only", choices=("footage", "window", "os8088"),
                    help="one step (window needs footage to have run once)")
    ap.add_argument("--scene", choices=SCENES, help=argparse.SUPPRESS)
    ap.add_argument("--xdisplay", help=argparse.SUPPRESS)
    a = ap.parse_args()
    work = os.path.abspath(os.path.expanduser(a.work))
    if a.scene:
        return scene(a.scene, work, a.xdisplay)
    os.makedirs(work, exist_ok=True)
    os.makedirs(PICS, exist_ok=True)
    if a.only in (None, "footage") or \
            not os.path.exists(os.path.join(work, PROJECT)):
        footage(work)
    if a.only in (None, "window"):
        window_all(work)
    if a.only in (None, "os8088"):
        os8088_all(work)
    sizes()
    return 0


if __name__ == "__main__":
    sys.exit(main())
