#!/usr/bin/env python3
"""The Video Battle builder's WINDOW, driven: the first clip of a NEW project
(tools/os88vbtgui.py's event pump, which tests/vbtguitest.py never opens).

    python3 tests/vbtguiwin.py

The owner's report, 2026-11-04: a video dragged onto the Openings tab of a
new project asked for the .vbp to be saved, and then nothing - no clip, no
progress bar, and every later drop or "Add Videos..." did nothing either.
Every job built its arguments from `proj.path` BEFORE run() asked for the
save, so the first job of a never-saved project was started with None for
its file; Popen raised inside the Tk callback that pumps the window's
events, Tk printed it where nobody looks, and the pump was never scheduled
again. This row drives that window under Xvfb:

  A. a new project; a video dropped on the Openings tab; the save dialog
     answered with a path - the clip is read and becomes opening 1; a second
     dropped there is opening 2; a third added as "Add Videos..." adds one
     is a clip; nothing raised in a Tk callback, the status not an error;
  B. a new project; a video dropped; the save dialog CANCELLED - the status
     says nothing was done, and the next drop (saved this time) still
     becomes a clip: a cancelled save leaves nothing in the queue.

Broken on purpose (run() not taking the path after the save) A and B fail:
no clip is ever made (the pump survives it now, and says so).

Needs a Python whose tkinter imports, xvfb-run, and ffmpeg
(os88build.tk_python(); the suite's `tk` capability).
"""
import os
import subprocess
import sys
import tempfile
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def inner(tmp):
    import os88vbtgui as G
    from tkinter import filedialog
    vids = []
    for n in range(4):
        v = os.path.join(tmp, "clip%d.mp4" % n)
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi",
                        "-i", "testsrc=size=320x240:rate=15", "-f", "lavfi",
                        "-i", "sine=f=%d" % (300 + 100 * n), "-t", "2",
                        "-shortest", v], check=True)
        vids.append(v)
    answer = {"p": None}
    filedialog.asksaveasfilename = lambda **k: answer["p"] or ""

    def window():
        app = G.App()
        errs = []
        app.root.report_callback_exception = lambda *e: errs.append(
            "".join(traceback.format_exception(*e)))
        names = [app.nb.tab(t, "text") for t in app.nb.tabs()]
        app.nb.select(app.nb.tabs()[[i for i, n in enumerate(names)
                                     if "pening" in n][0]])
        app.root.update()
        return app, errs

    seen = {"bar": 0, "title": 0, "moved": 0, "what": "", "ph": 0}

    def pump(app, cond, secs=90):
        t = time.time()
        while time.time() - t < secs:
            app.root.update()
            if app.pending and app.job is not None:     # the list it went to
                seen["ph"] += any("reading" in app.openings_tab.lb.get(k)
                                  for k in range(app.openings_tab.lb.size()))
            if app.job is not None:     # WHILE A JOB RUNS: what the eye has
                seen["bar"] = max(seen["bar"],
                                  app.pbar.winfo_height()
                                  if app.pbar.winfo_ismapped() else 0)
                seen["title"] += app.root.title().startswith("[")
                seen["moved"] += str(app.pbar.cget("mode")) == \
                    "indeterminate" or float(app.pbar.cget("value")) > 0
                seen["what"] = seen["what"] or app.status.cget("text")
            time.sleep(0.02)
            if cond():
                return True
        return False

    # the status bar keeps its line however tall a tab asks to be (the Idle
    # tab's empty picture asked for 5,000 px, and the bar was 1 px high)
    answer["p"] = os.path.join(tmp, "a.vbp")
    app, errs = window()
    hs = []
    for geo in ("1300x760", "1000x500"):
        app.root.geometry(geo)
        want_h = int(geo.split("x")[1])
        for _ in range(100):            # (until the resize has landed)
            app.root.update()
            if app.root.winfo_height() == want_h:
                break
            time.sleep(0.02)
        app.root.update()
        hs.append(app.status.master.winfo_height())
    ok(min(hs) >= 20 and app.nb.winfo_reqheight() < 1200,
       "the status bar and its progress keep their line: %s px high at "
       "1300x760 and 1000x500 (the tabs ask %d)"
       % (hs, app.nb.winfo_reqheight()))
    app.root.geometry("1300x760")

    # A: the owner's sequence
    app.q.put(("drop", ([vids[0]], app.openings_tab.f)))
    got = pump(app, lambda: app.proj.d["clips"] and not app.pending
               and not app.job)
    ok(got and app.proj.path == answer["p"] and
       [o["clip"] for o in app.proj.d.get("openings", [])] == ["clip0"],
       "A. the first video of a NEW project, dropped on the Openings tab: "
       "saved, read and opening 1 (clips %s)" % list(app.proj.d["clips"]))
    ok(seen["bar"] >= 10 and seen["moved"] > 0 and seen["title"] > 0,
       "A. ...and while it was read, SOMETHING WAS SEEN to happen: the bar "
       "%d px high and moving, %r, the title showing it"
       % (seen["bar"], seen["what"]))
    ok(seen["ph"] > 0, "A. ...shown in the Openings list as it was read "
       "('(reading...)', greyed)")
    ok(app.openings_tab.i() == 0, "A. ...the new opening chosen in the list")
    app.q.put(("drop", ([vids[1]], app.openings_tab.f)))
    app.add_clip_file(vids[2])          # (what "Add Videos..." does)
    got = pump(app, lambda: len(app.proj.d["clips"]) >= 3 and not app.pending
               and not app.job)
    ok(got and [o["clip"] for o in app.proj.d["openings"]]
       == ["clip0", "clip1"] and "clip2" in app.proj.d["clips"],
       "A. ...a second dropped there is opening 2, a third added is a clip "
       "(openings %s)" % [o["clip"] for o in app.proj.d.get("openings", [])])
    # FULL SCREEN, or with the menu (125.3): opening 1 set full through its
    # radio button; the last's "Full screen" greyed (it joins the idle loop)
    ot = app.openings_tab
    app.nb.select(ot.f)
    ot.lb.selection_clear(0, "end")
    ot.lb.selection_set(0)
    ot._pick()
    ot.fullv.set(1)
    ot._set_full()
    line0 = ot.lb.get(0)
    ot.lb.selection_clear(0, "end")
    ot.lb.selection_set(1)
    ot._pick()
    ok(app.proj.d["openings"][0].get("full") is True
       and "FULL SCREEN" in line0 and "full" not in app.proj.d["openings"][1]
       and str(ot.full_rb.cget("state")) == "disabled",
       "A. opening 1 set FULL SCREEN by its button (%r); the last's button "
       "greyed" % line0.strip())
    # THE PREVIEW of a full-screen opening is its own encode, over the
    # whole screen (the owner: "selectable, but the preview is still
    # showing it not fullscreen"); set back, the preview follows by itself
    ot._load()
    pump(app, lambda: ot.frames is not None and not app.job, 120)
    sh = [ot.frames[0][ot.frames[1][k]].shape[0] for k in (0, 1)] \
        if ot.frames else None
    ot.lb.selection_clear(0, "end")
    ot.lb.selection_set(0)
    ot._pick()
    ot.fullv.set(0)
    ot._set_full()
    pump(app, lambda: ot.frames is not None and not app.job, 60)
    sh2 = ot.frames[0][0].shape[0] if ot.frames else None
    ok(sh == [200, 142] and sh2 == 142,
       "A. the preview: the full-screen opening 200 rows, the menu one 142 "
       "(%s); set back to the menu, reloaded at %s" % (sh, sh2))
    ot.fullv.set(1)
    ot._set_full()
    pump(app, lambda: ot.frames is not None and not app.job, 60)

    # THE PICTURE'S SIZE (125.3.2): Hercules set down to half on the
    # Battle tab, then back
    bt = app.battle_tab
    bt.scv["herc"].set("50%")
    bt._scale()
    sc1, note = app.proj.d["target"].get("scale"), bt.scnote.cget("text")
    bt.scv["herc"].set("100%")
    bt._scale()
    ok(sc1 == {"herc": 50} and "320 x 116" in note
       and not app.proj.d["target"].get("scale"),
       "A. Hercules' picture set to 50%% (%r: %r), and back"
       % (sc1, note[:60]))
    st = app.status.cget("text")
    ok(not errs and not st.startswith(("Error", "Could not", "Failed")),
       "A. nothing raised in a Tk callback (%d), the status %r"
       % (len(errs), st))
    for e in errs[:2]:
        print(e)
    app.root.destroy()

    # C: THE BAND - in the previews as the engine draws it, and its
    # pictures edited after they are added (the owner's ask)
    import numpy as np
    import os88vbt as B
    from PIL import Image
    cv = os.path.join(tmp, "c.vbp")
    B.demo_project(cv, bars=2)
    pic = os.path.join(tmp, "crest.png")
    Image.fromarray((np.indices((24, 48)).sum(0) % 7 * 40).astype(np.uint8)
                    ).convert("RGB").save(pic)
    app, errs = window()
    app.open_path(cv)
    it = app.idle_tab
    app.nb.select(it.f)
    it._load()
    pump(app, lambda: it._frames() is not None and not app.job, 60)
    it._show(0)
    h_on = it._img.height()
    rgb = np.array(app.band_rgb()) if app.band_rgb() is not None else None
    app.show_band.set(False)
    it._show(0)
    h_off = it._img.height()
    app.show_band.set(True)
    ok(rgb is not None and rgb.shape[:2] == (58, 320) and rgb.any()
       and h_on == 200 * 2 and h_off == 142 * 2,
       "C. the Idle preview with THE BAND under it (%d px high, %d without: "
       "View's switch)" % (h_on, h_off))
    ot = app.openings_tab
    ot._load()
    pump(app, lambda: ot.frames is not None and not app.job, 60)
    ok(ot.frames is not None and
       ot._pic(ot.frames[0][0], 0).height() == 400,
       "C. an opening WITH THE MENU previews with the band")
    d = app.proj.d
    app.proj.set_opening(0, full=True) if len(d["openings"]) > 1 else None
    pt = app.party_tab
    app.nb.select(pt.f)
    app.edit("set", ("images",), [{"name": "crest", "path": pic, "col": 2,
                                   "row": 4, "dither": True}])
    app.edit("set", ("band_image",), "crest")
    pt.imlb.selection_set(0)
    pt._pick_image()
    b0 = np.array(app.band_rgb())
    pt._im_set(0, "col", 20)
    b1 = np.array(app.band_rgb())
    pt._rename_image(0, "shield")
    ok(pt.imsel == 0 and not np.array_equal(b0, b1)
       and app.proj.d["images"][0]["col"] == 20
       and app.proj.d["images"][0]["name"] == "shield"
       and app.proj.d.get("band_image") == "shield",
       "C. a band picture EDITED after it was added: moved (the band "
       "preview changes with it), renamed (the band's own use with it)")
    # THE MUSIC: none chosen, then its sound removed; a sound the menu
    # uses refuses (the owner: "no way to remove it, and it won't build
    # without it")
    from tkinter import messagebox
    messagebox.askyesno = lambda *a, **k: True
    bt = app.battle_tab
    app.nb.select(bt.f)
    msid = app.proj.d["music"]["sound"]
    bt.msound.set("(no music)")
    bt._music_sound()
    none1 = app.proj.d["music"] is None
    bt.msound.set(msid)
    bt._music_sound()
    bt._rm_sound()
    gone = msid not in app.proj.d["sounds"] and app.proj.d["music"] is None
    used = next(iter(app.proj.d["ui_sounds"].values()))
    bt.msound.set(used)
    bt._rm_sound()
    ok(none1 and gone and used in app.proj.d["sounds"]
       and "used by" in app.status.cget("text")
       and not [x for x in app.proj.check() if x.where == "music"
                and x.level == "ERROR"],
       "C. the music set to none, its sound removed; a menu sound refuses "
       "(%r)" % app.status.cget("text")[:70])
    # LEVELS (the owner: "a hard time matching the music volume and the clip
    # volumes"): the Sounds tab measures, a level nudged, the suggestions
    # applied
    app.open_path(cv)
    st = app.sounds_tab
    app.nb.select(st.f)
    app.save()
    st._measure()
    pump(app, lambda: st.res is not None and not app.job, 120)
    rows = st.tv.get_children()
    st.tv.selection_set("tick")
    st._pick()
    st._nudge(3)
    lv = app.proj.d["sounds"]["tick"]["level"]
    st._measure()
    pump(app, lambda: st.res is not None and not app.job, 120)
    riser0 = app.proj.d["sounds"]["riser"].get("level", 1.0)
    st._apply()
    riser1 = app.proj.d["sounds"]["riser"]["level"]
    import math
    ok(len(rows) >= 10 and abs(20 * math.log10(lv) - 3) < 0.05
       and riser1 < riser0 * 0.7 and st.res is None,
       "C. the Sounds tab: %d sounds measured, the tick +3 dB, the "
       "suggestions applied (the opening's riser %.2f -> %.2f)"
       % (len(rows), riser0, riser1))
    # THE BAND'S LAYOUT (125.5.1): MP hidden, the message moved, an item
    # placed - on the Characters & menus tab, its preview following
    pt = app.party_tab
    app.nb.select(pt.f)
    b0 = np.array(app.band_rgb())
    pt._lay("mp_show", 0)
    pt._lay("msg_row", "51")            # (clear of the menu: 125.5)
    pt.mrow = 1
    pt._place(app.proj.d["characters"][0]["name"] + ":top", 1, 0, "1")
    b1 = np.array(app.band_rgb())
    lk = app.proj.d["menu_looks"][app.proj.d["characters"][0]["name"] + ":top"]
    said = [x.text for x in app.proj.check() if x.where == "band_layout"]
    ok(app.proj.d["band_layout"] == {"mp_show": 0, "msg_row": 51}
       and lk["places"][1][0] == 1 and not np.array_equal(b0, b1)
       and not said,
       "C. the band's LAYOUT set: MP hidden, the message moved, an item "
       "placed (%s), the preview with it %s" % (lk["places"], said))
    pt._lay("mp_show", "")
    pt._lay("msg_row", "")
    ok(not app.proj.d.get("band_layout"), "C. ...and back to as it was")

    # THE MENU'S SOUNDS: chosen on the Sounds tab, one from a file
    st = app.sounds_tab
    app.nb.select(st.f)
    st.uiv["open"].set("(none)")
    st._ui_set("open")
    from tkinter import filedialog
    beep = os.path.join(tmp, "beep.wav")
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                    "sine=f=990:d=0.1", beep], check=True)
    filedialog.askopenfilename = lambda **k: beep
    st._ui_file("take")
    ui = app.proj.d["ui_sounds"]
    ok("open" not in ui and ui.get("take") == "beep" and "beep" in
       app.proj.d["sounds"] and st.uiv["take"].get() == "beep"
       and "the menu's take sound" in app.proj.sound_users("beep"),
       "C. MENU SOUNDS on the Sounds tab: one taken off, one added from a "
       "file (%s)" % ui)

    # A SOUND'S FILE RENAMED (the owner, 2026-11-05: "no way to remove a
    # sound clip; so if I rename one, it just breaks the project"): the
    # Sounds tab lists it FILE MISSING, Replace file points it at the new
    # name with its users kept, and Remove takes it out of them too
    gone = os.path.join(tmp, "beep2.wav")
    os.rename(beep, gone)
    st.refresh()
    said = st.tv.set("beep", "plays")
    filedialog.askopenfilename = lambda **k: gone
    st.tv.selection_set("beep")
    st._refile()
    st.refresh()
    re_ok = (not app.proj.sound_missing("beep") and
             app.proj.d["ui_sounds"].get("take") == "beep")
    st.tv.selection_set("beep")
    st._remove()
    ok(said == "FILE MISSING" and re_ok and "beep" not in
       app.proj.d["sounds"] and "take" not in app.proj.d["ui_sounds"]
       and "beep" not in st.tv.get_children(),
       "C. a sound whose file was RENAMED: listed '%s', its file replaced "
       "(users kept: %s), then REMOVED from the Sounds tab and from the "
       "menu sound that used it" % (said, re_ok))
    st.uiv["take"].set("(none)")

    # THE END CLIP (125.3.1): full screen, played once, then the title
    lt = app.loops_tab
    app.nb.select(lt.f)
    vp = [x for x in lt.panels if x.kind == "victory"][0]
    vp.refresh()
    vp.fullv.set(1)
    vp.plays.set("once")
    vp.then.set("join the title")
    vp._commit()
    en = app.proj.d["endings"]["victory"]
    ok(en.get("full") is True and en.get("plays") == "once"
       and en.get("then") == "join" and "joins the title's first" in
       vp.note.cget("text"),
       "C. victory's END CLIP set full screen, played once, then joining "
       "the title (%s)" % {k: en.get(k) for k in ("how", "full", "plays",
                                                   "then")})

    # FOLLOW-UPS (125.6.7): the Enemy tab's box, one added and set
    et = app.enemy_tab
    app.nb.select(et.f)
    acts = list(app.proj.d["actions"])
    app.edit("set", ("enemy", "follow_ups"), [
        {"after": acts[0], "then": acts[0], "target": "same"}])
    et._fol(0, after="lunge", then="swarm")
    et._fol(0, target="lowest")
    fu = app.proj.d["enemy"].get("follow_ups")
    ok(fu == [{"after": "lunge", "then": "swarm", "target": "lowest"}],
       "C. a FOLLOW-UP set on the Enemy tab: %s" % fu)
    ok(not errs, "C. nothing raised in a Tk callback (%d)" % len(errs))
    for e in errs[:2]:
        print(e)
    app.root.destroy()

    # B: the save cancelled, then not
    answer["p"] = None
    app, errs = window()
    app.q.put(("drop", ([vids[3]], app.openings_tab.f)))
    pump(app, lambda: app.status.cget("text").startswith("Not saved"), 20)
    ok(app.status.cget("text").startswith("Not saved") and not app.pending
       and not app.job,
       "B. the save CANCELLED: %r, nothing left queued"
       % app.status.cget("text"))
    answer["p"] = os.path.join(tmp, "b.vbp")
    app.q.put(("drop", ([vids[3]], app.openings_tab.f)))
    got = pump(app, lambda: app.proj.d["openings"] and not app.pending
               and not app.job)
    ok(got and not errs, "B. ...and the next drop, saved, is a clip and an "
       "opening (%s)" % list(app.proj.d["clips"]))
    app.root.destroy()


def main():
    if os.environ.get("VBTGUIWIN_INNER"):
        with tempfile.TemporaryDirectory() as tmp:
            inner(tmp)
    else:
        import os88build
        py = os88build.tk_python()
        if not py:
            sys.exit("vbtguiwin: no python with tkinter and xvfb-run here")
        return subprocess.run(
            ["xvfb-run", "-a", py, os.path.abspath(__file__)],
            env=dict(os.environ, VBTGUIWIN_INNER="1"), cwd=ROOT).returncode
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
