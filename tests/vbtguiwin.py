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
