#!/usr/bin/env python3
"""os88vbtgui - the Video Battle builder's window
(docs/plans/VBATTLE-BUILDER-PLAN.md 2, 4; wave 2: the shell, the Battle tab
and the Idle tab).

    python3 tools/os88vbtgui.py [PROJECT.vbp]

A VIEW of a project (tools/vbtproj.py). Every edit is one of the model's
methods, so Undo is the model's; every long piece of work - encoding a
source, finding loops, costing the idle loop, building - is a PROCESS
(tools/vbtana.py, tools/os88vbt.py, with JSON lines on stdout) that Cancel
kills, group and all, the encoder window's lesson (SPEC.md 98.2.11.1).

Everything above `class App` needs no Tk, so tests/vbtguitest.py drives it
as the window does.
"""

import json
import os
import queue
import subprocess
import sys
import tempfile
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import vbtproj as P     # noqa: E402

TITLE = "Video Battle builder"
SCALE = 2               # a canvas pixel on the screen
BG_NAMES = ("Black", "Blue", "Green", "Cyan", "Red", "Magenta", "Brown",
            "Light grey", "Dark grey", "Light blue", "Light green",
            "Light cyan", "Light red", "Light magenta", "Yellow", "White")
SET_NAMES = ("Green, red and brown", "Cyan, magenta and white",
             "Cyan, red and white (mode 5's)")
CONFIG = os.path.join(os.path.expanduser("~"), ".config", "os8088",
                      "vbtgui.json")


# --- the jobs: a process, its JSON lines, a Cancel that kills it ---------------

class Job(object):
    """A tool run as a process of its own, its stdout JSON lines read as
    events. TOOL is "vbtana" or "os88vbt"; ARGV its arguments"""

    def __init__(self, tool, argv):
        self.cmd = [sys.executable, "-u", os.path.join(HERE, tool + ".py")] + \
            list(argv)
        self.p = None
        self.killed = False
        self.result = None

    def start(self):
        kw = dict(stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                  stderr=subprocess.STDOUT, encoding="utf-8",
                  errors="replace", bufsize=1,
                  env=dict(os.environ, PYTHONIOENCODING="utf-8"))
        if sys.platform == "win32":
            kw["creationflags"] = 0x00000200 | 0x08000000
        else:
            kw["start_new_session"] = True
        self.p = subprocess.Popen(self.cmd, **kw)
        return self

    def events(self):
        """("prog", {stage, done, of, what}), ("log", line), then
        ("done", the result) if it gave one, and ("exit", returncode)"""
        for line in self.p.stdout:
            try:
                ev = json.loads(line)
            except ValueError:
                yield "log", line.rstrip()
                continue
            if not isinstance(ev, dict):
                yield "log", line.rstrip()
            elif ev.get("stage") == "done":
                self.result = ev
                yield "done", ev
            else:
                yield "prog", ev
        yield "exit", self.p.wait()

    def run(self):
        """Start, wait, and return the result (no window: a test's way)"""
        self.start()
        log = []
        for kind, ev in self.events():
            if kind == "log":
                log.append(ev)
            elif kind == "exit" and ev != 0 and not self.killed:
                raise RuntimeError("%s failed:\n%s" % (" ".join(self.cmd[2:4]),
                                                       "\n".join(log[-20:])))
        return self.result

    def cancel(self):
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


def progress_text(ev):
    st, done, of, what = (ev.get(k) for k in ("stage", "done", "of", "what"))
    words = {"encode": "Encoding", "decode": "Decoding", "cost": "Costing",
             "analyse": "Listening to", "clips": "Clips:", "check": "Checking"}
    w = words.get(st, str(st).capitalize())
    if of:
        return "%s %s - %d of %d" % (w, what or "", done, of)
    return "%s %s" % (w, what or "")


# --- listening, through the platform's own player (plan 10.2) ----------------------

def player_argv(wav):
    """The command that plays WAV here, or None (Windows: winsound, in
    process). The window builds the WAV; it plays nothing itself"""
    if sys.platform == "darwin":
        return ["afplay", wav]
    if sys.platform == "win32":
        return None
    import shutil
    for exe in ("paplay", "aplay", "ffplay"):
        if shutil.which(exe):
            return [exe, wav] if exe != "ffplay" else \
                [exe, "-nodisp", "-autoexit", "-loglevel", "quiet", wav]
    return []


def play_wav(wav):
    argv = player_argv(wav)
    if argv is None:
        import winsound
        winsound.PlaySound(wav, winsound.SND_FILENAME | winsound.SND_ASYNC)
        return True
    if not argv:
        return False
    subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL)
    return True


# --- the Battle tab's arithmetic ----------------------------------------------------

def palette_parts(b):
    """CGA4 palette byte B (98.1.3.3) as (set 0..2, bright 0/1, background)"""
    st = 2 if b & 0x40 else (b >> 5) & 1
    return st, (b >> 4) & 1, b & 15


def palette_byte(st, bright, bg):
    return (0x40 if st == 2 else st << 5) | (bright << 4) | bg


def profiles():
    """(name, what) for every encoder profile a battle can target"""
    import os88venc
    return [(k, v.get("what", "")) for k, v in os88venc.PROFILES.items()]


def sizes_for(rows):
    """A canvas ROWS high over the band in the rest of 200: the canvas and
    band rectangles of the project's target (10.4)"""
    rows = max(8, min(192, int(rows) // 2 * 2))
    return [0, 0, 320, rows], [0, rows, 40, 200 - rows]


def secs(samples, rate):
    return samples / float(rate)


def samples_of(text, rate):
    return int(round(float(text) * rate))


# --- the Idle tab's arithmetic -------------------------------------------------------

def seam_order(loop, around=8):
    """Source frame indexes that play the loop ACROSS its seam: the last
    AROUND frames, then the first AROUND, so the join is seen in motion"""
    a, b = loop
    k = min(around, (b - a) // 2)
    return list(range(b - k, b)) + list(range(a, a + k))


def frame_image(px, pal, scale=SCALE):
    """A PIL image of pixel indexes PX in palette byte PAL"""
    from PIL import Image
    import vbtana as A
    im = Image.fromarray(A.to_rgb(px, pal))
    return im.resize((im.width * scale, im.height * scale * 2 // 2),
                     Image.NEAREST)


def title_of(proj, dirty):
    nm = proj.d.get("name") or "Untitled"
    f = os.path.basename(proj.path) if proj.path else "not saved"
    return "%s%s - %s - %s" % ("*" if dirty else "", nm, f, TITLE)


def recent_load():
    try:
        with open(CONFIG) as f:
            return [p for p in json.load(f).get("recent", []) if os.path.exists(p)]
    except (OSError, ValueError):
        return []


def recent_add(path):
    r = [path] + [p for p in recent_load() if p != path]
    try:
        os.makedirs(os.path.dirname(CONFIG), exist_ok=True)
        with open(CONFIG, "w") as f:
            json.dump({"recent": r[:8]}, f)
    except OSError:
        pass


# --- the window ----------------------------------------------------------------------

class App(object):
    def __init__(self, path=None):
        import tkinter as tk
        from tkinter import ttk
        self.tk, self.ttk = tk, ttk
        self.root = tk.Tk()
        self.root.geometry("1220x720")
        self.proj = P.Project()
        self.saved = self._state()
        self.job = None
        self.q = queue.Queue()
        self.frames = {}            # source -> decoded screens, once loaded
        self.anim = None
        self._menus()
        self._body()
        if path:
            self.open_path(path)
        self.refresh()
        self.root.protocol("WM_DELETE_WINDOW", self.quit)
        self.root.after(50, self._poll)

    # --- the shell -----------------------------------------------------------
    def _state(self):
        return json.dumps(self.proj.d, sort_keys=True)

    def dirty(self):
        return self._state() != self.saved

    def _menus(self):
        tk = self.tk
        mb = tk.Menu(self.root)
        f = tk.Menu(mb, tearoff=0)
        f.add_command(label="New", command=self.new, accelerator="Ctrl+N")
        f.add_command(label="Open...", command=self.open, accelerator="Ctrl+O")
        self.recent_menu = tk.Menu(f, tearoff=0)
        f.add_cascade(label="Open recent", menu=self.recent_menu)
        f.add_command(label="Save", command=self.save, accelerator="Ctrl+S")
        f.add_command(label="Save as...", command=self.save_as)
        f.add_separator()
        f.add_command(label="Quit", command=self.quit)
        mb.add_cascade(label="File", menu=f)
        e = tk.Menu(mb, tearoff=0)
        e.add_command(label="Undo", command=self.undo, accelerator="Ctrl+Z")
        e.add_command(label="Redo", command=self.redo, accelerator="Ctrl+Y")
        mb.add_cascade(label="Edit", menu=e)
        self.root.config(menu=mb)
        for k, fn in (("<Control-n>", self.new), ("<Control-o>", self.open),
                      ("<Control-s>", self.save), ("<Control-z>", self.undo),
                      ("<Control-y>", self.redo)):
            self.root.bind(k, lambda e, fn=fn: fn())

    def _body(self):
        ttk = self.ttk
        self.nb = ttk.Notebook(self.root)
        self.nb.pack(fill="both", expand=True)
        self.battle_tab = BattleTab(self, self.nb)
        self.idle_tab = IdleTab(self, self.nb)
        self.nb.add(self.battle_tab.f, text="Battle")
        self.nb.add(self.idle_tab.f, text="Idle")
        for name, wave in (("Clips", 3), ("Menus", 4), ("Build", 5)):
            fr = ttk.Frame(self.nb)
            ttk.Label(fr, text="The %s tab comes in the plan's wave %d. Until "
                      "then, tools/os88vbt.py build PROJECT builds what the "
                      "project says." % (name, wave), wraplength=600
                      ).pack(padx=20, pady=20, anchor="w")
            self.nb.add(fr, text=name)
        bar = ttk.Frame(self.root)
        bar.pack(fill="x", side="bottom")
        self.status = ttk.Label(bar, text="Ready")
        self.status.pack(side="left", padx=6)
        self.cancel_b = ttk.Button(bar, text="Cancel", command=self.cancel,
                                   state="disabled")
        self.cancel_b.pack(side="right", padx=4, pady=2)
        self.pbar = ttk.Progressbar(bar, length=220, mode="determinate")
        self.pbar.pack(side="right", padx=4)

    def refresh(self):
        self.root.title(title_of(self.proj, self.dirty()))
        self.recent_menu.delete(0, "end")
        for p in recent_load():
            self.recent_menu.add_command(label=p,
                                         command=lambda p=p: self.open_path(p))
        self.battle_tab.refresh()
        self.idle_tab.refresh()

    def edit(self, fn, *a, **kw):
        """Every edit: the model's method, then the view redrawn"""
        try:
            getattr(self.proj, fn)(*a, **kw)
        except (ValueError, KeyError) as e:
            self.say("Not done: %s" % e)
        self.refresh()

    def say(self, text):
        self.status.config(text=text)

    def _ask_save(self):
        from tkinter import messagebox
        if not self.dirty():
            return True
        r = messagebox.askyesnocancel(TITLE, "Save the changes first?")
        if r is None:
            return False
        if r:
            return self.save()
        return True

    def new(self):
        if self._ask_save():
            self.proj = P.Project()
            self.saved = self._state()
            self.frames.clear()
            self.refresh()

    def open(self):
        from tkinter import filedialog
        if not self._ask_save():
            return
        p = filedialog.askopenfilename(filetypes=[("Video Battle projects",
                                                   "*.vbp"), ("All", "*")])
        if p:
            self.open_path(p)

    def open_path(self, p):
        try:
            self.proj = P.Project.load(p)
        except (OSError, ValueError) as e:
            self.say("Could not open %s: %s" % (p, e))
            return
        self.saved = self._state()
        self.frames.clear()
        recent_add(self.proj.path)
        self.refresh()
        self.say("Opened %s" % p)

    def save(self):
        if not self.proj.path:
            return self.save_as()
        self.proj.save()
        self.saved = self._state()
        recent_add(self.proj.path)
        self.refresh()
        self.say("Saved")
        return True

    def save_as(self):
        from tkinter import filedialog
        p = filedialog.asksaveasfilename(defaultextension=".vbp",
                                         filetypes=[("Video Battle projects",
                                                     "*.vbp")])
        if not p:
            return False
        self.proj.save(p)
        return self.save()

    def undo(self):
        if not self.proj.undo():
            self.say("Nothing to undo")
        self.refresh()

    def redo(self):
        if not self.proj.redo():
            self.say("Nothing to redo")
        self.refresh()

    def quit(self):
        if self._ask_save():
            self.cancel()
            self.root.destroy()

    # --- jobs ------------------------------------------------------------------
    def need_saved(self):
        """A job reads the project from its file, so it must be on disk"""
        if self.proj.path is None or self.dirty():
            return self.save()
        return True

    def run(self, tool, argv, done, what=""):
        """TOOL with ARGV as a process; DONE(result) in the window's thread"""
        if self.job:
            self.say("Busy: Cancel first")
            return
        if not self.need_saved():
            return
        self.job = Job(tool, argv).start()
        self.cancel_b.config(state="normal")
        self.pbar.config(value=0, maximum=1)
        self.say(what or "Working...")
        job, q = self.job, self.q

        def pump():
            log = []
            for kind, ev in job.events():
                if kind == "log":
                    log.append(ev)
                elif kind == "exit":
                    q.put(("exit", (job, ev, log)))
                else:
                    q.put((kind, ev))
        threading.Thread(target=pump, daemon=True).start()
        self._done = done

    def _poll(self):
        try:
            while True:
                kind, ev = self.q.get_nowait()
                if kind == "prog":
                    self.say(progress_text(ev))
                    if ev.get("of"):
                        self.pbar.config(maximum=ev["of"], value=ev["done"])
                elif kind == "exit":
                    job, rc, log = ev
                    self.job = None
                    self.cancel_b.config(state="disabled")
                    self.pbar.config(value=0)
                    if job.killed:
                        self.say("Cancelled")
                    elif rc != 0:
                        self.say("Failed: %s" % (log[-1] if log else rc))
                    elif job.result is not None:
                        self.say("Done")
                        self._done(job.result)
        except queue.Empty:
            pass
        self.root.after(50, self._poll)

    def cancel(self):
        if self.job:
            self.job.cancel()

    def screens(self, sid, then):
        """Source SID's decoded screens, encoding it first if it is not in
        the cache yet (a job), then THEN(frames)"""
        import vbtana as A
        if sid in self.frames:
            return then(self.frames[sid])
        s = self.proj.d["sources"][sid]
        if s["kind"] == "scene" or self._cached(sid):
            self.frames[sid] = A.screens(self.proj, sid)
            return then(self.frames[sid])

        def got(res):
            self.frames[sid] = A.screens(self.proj, sid)
            then(self.frames[sid])
        self.run("vbtana", ["screens", self.proj.path, sid], got,
                 "Encoding %s..." % sid)

    def _cached(self, sid):
        import hashlib
        import vbtana as A
        s, t = self.proj.d["sources"][sid], self.proj.d["target"]
        if not self.proj.path:
            return False
        key = hashlib.sha1(json.dumps(
            [A.ENC_KEY, A.file_hash(self.proj.here(s["path"])),
             t["canvas"][3], t["fps"], t["profile"], t["palette"]]).encode()
                           ).hexdigest()[:20]
        return os.path.exists(os.path.join(A.cache_dir(self.proj), key + ".npy"))

    def photo(self, px, pal=None, scale=SCALE):
        from PIL import ImageTk
        return ImageTk.PhotoImage(frame_image(px, pal if pal is not None else
                                              self.proj.d["target"]["palette"],
                                              scale))

    def listen(self, sound, loop=None):
        out = os.path.join(tempfile.gettempdir(), "vbtgui-listen.wav")
        argv = ["wav", self.proj.path, sound, out]
        if loop:
            argv += ["--loop", str(loop[0]), str(loop[1])]

        def got(res):
            if not play_wav(res["out"]):
                self.say("No player here: the WAV is %s" % res["out"])
        self.run("vbtana", argv, got, "Making the sound to listen to...")


# --- the Battle tab (4.1, 10.2, 10.4) ---------------------------------------------------

class BattleTab(object):
    def __init__(self, app, parent):
        tk, ttk = app.tk, app.ttk
        self.app = app
        self.f = f = ttk.Frame(parent, padding=8)
        self.loops = []
        r = 0
        ttk.Label(f, text="Name").grid(row=r, column=0, sticky="w")
        self.name = tk.StringVar()
        e = ttk.Entry(f, textvariable=self.name, width=40)
        e.grid(row=r, column=1, columnspan=3, sticky="w")
        e.bind("<FocusOut>", lambda ev: self._set("name", self.name.get()))
        e.bind("<Return>", lambda ev: self._set("name", self.name.get()))
        r += 1
        ttk.Label(f, text="Made for").grid(row=r, column=0, sticky="w")
        self.profs = profiles()
        self.prof = tk.StringVar()
        cb = ttk.Combobox(f, textvariable=self.prof, state="readonly",
                          values=[p for p, _ in self.profs], width=20)
        cb.grid(row=r, column=1, sticky="w")
        cb.bind("<<ComboboxSelected>>",
                lambda ev: self._set_t("profile", self.prof.get()))
        self.prof_what = ttk.Label(f, text="", wraplength=560)
        self.prof_what.grid(row=r, column=2, columnspan=3, sticky="w")
        r += 1
        # the palette
        pf = ttk.LabelFrame(f, text="Palette (CGA4: the whole battle and its "
                                    "band share it)", padding=6)
        pf.grid(row=r, column=0, columnspan=5, sticky="we", pady=6)
        self.pset = tk.IntVar()
        for i, n in enumerate(SET_NAMES):
            ttk.Radiobutton(pf, text=n, variable=self.pset, value=i,
                            command=self._pal).grid(row=0, column=i, sticky="w")
        self.pbright = tk.IntVar()
        ttk.Checkbutton(pf, text="Bright", variable=self.pbright,
                        command=self._pal).grid(row=1, column=0, sticky="w")
        ttk.Label(pf, text="Background").grid(row=1, column=1, sticky="e")
        self.pbg = tk.StringVar()
        bg = ttk.Combobox(pf, textvariable=self.pbg, values=BG_NAMES,
                          state="readonly", width=14)
        bg.grid(row=1, column=2, sticky="w")
        bg.bind("<<ComboboxSelected>>", lambda ev: self._pal())
        self.swatch = tk.Canvas(pf, width=4 * 44, height=28,
                                highlightthickness=0)
        self.swatch.grid(row=0, column=3, rowspan=2, padx=10)
        r += 1
        # the sizes
        sf = ttk.LabelFrame(f, text="Sizes (the video above, the band below)",
                            padding=6)
        sf.grid(row=r, column=0, columnspan=5, sticky="we", pady=6)
        ttk.Label(sf, text="Video rows").grid(row=0, column=0)
        self.rows = tk.IntVar()
        sp = ttk.Spinbox(sf, from_=8, to=192, increment=2, width=6,
                         textvariable=self.rows, command=self._rows)
        sp.grid(row=0, column=1)
        sp.bind("<Return>", lambda ev: self._rows())
        self.size_text = ttk.Label(sf, text="")
        self.size_text.grid(row=0, column=2, padx=8)
        self.layout = tk.Canvas(sf, width=160, height=100, highlightthickness=0)
        self.layout.grid(row=0, column=3, padx=10)
        ttk.Label(sf, text="15 frames a second, sound at 11,025 Hz (the "
                           "engine's)").grid(row=1, column=0, columnspan=3,
                                             sticky="w")
        r += 1
        # the music
        mf = ttk.LabelFrame(f, text="Music (it always loops)", padding=6)
        mf.grid(row=r, column=0, columnspan=5, sticky="nswe", pady=6)
        f.rowconfigure(r, weight=1)
        f.columnconfigure(4, weight=1)
        ttk.Label(mf, text="Sound").grid(row=0, column=0, sticky="w")
        self.msound = tk.StringVar()
        self.mcb = ttk.Combobox(mf, textvariable=self.msound, state="readonly",
                                width=24)
        self.mcb.grid(row=0, column=1, sticky="w")
        self.mcb.bind("<<ComboboxSelected>>", lambda ev: self._music_sound())
        ttk.Button(mf, text="Add a file...", command=self._add_music
                   ).grid(row=0, column=2, padx=4)
        ttk.Button(mf, text="Listen", command=lambda: self._listen(False)
                   ).grid(row=0, column=3, padx=4)
        ttk.Label(mf, text="Loop (seconds)").grid(row=1, column=0, sticky="w")
        self.lstart, self.lend = tk.StringVar(), tk.StringVar()
        lf = ttk.Frame(mf)
        lf.grid(row=1, column=1, columnspan=2, sticky="w")
        ttk.Entry(lf, textvariable=self.lstart, width=10).pack(side="left")
        ttk.Label(lf, text=" to ").pack(side="left")
        ttk.Entry(lf, textvariable=self.lend, width=10).pack(side="left")
        ttk.Button(mf, text="Set", command=self._set_loop).grid(row=1, column=3)
        ttk.Label(mf, text="Comes in at (s)").grid(row=2, column=0, sticky="w")
        self.join = tk.StringVar()
        je = ttk.Entry(mf, textvariable=self.join, width=10)
        je.grid(row=2, column=1, sticky="w")
        ttk.Button(mf, text="Set", command=self._set_join).grid(row=2, column=2,
                                                                sticky="w")
        ttk.Button(mf, text="Find loops", command=self._find
                   ).grid(row=3, column=0, pady=4, sticky="w")
        ttk.Button(mf, text="Use the chosen loop", command=self._use
                   ).grid(row=3, column=1, sticky="w")
        ttk.Button(mf, text="Listen to its jump", command=lambda: self._listen(True)
                   ).grid(row=3, column=2, sticky="w")
        self.lb = tk.Listbox(mf, height=6, width=70)
        self.lb.grid(row=4, column=0, columnspan=4, sticky="we")

    def refresh(self):
        d = self.app.proj.d
        t = d["target"]
        self.name.set(d.get("name", ""))
        self.prof.set(t["profile"])
        self.prof_what.config(text=dict(self.profs).get(t["profile"], ""))
        st, br, bg = palette_parts(t["palette"])
        self.pset.set(st)
        self.pbright.set(br)
        self.pbg.set(BG_NAMES[bg])
        self._swatches(t["palette"])
        rows = t["canvas"][3]
        self.rows.set(rows)
        self.size_text.config(text="video 320 x %d, band %d rows (%d cells "
                                   "of text)" % (rows, 200 - rows,
                                                 (200 - rows) // 8))
        c = self.layout
        c.delete("all")
        k = 160 / 320.0
        c.create_rectangle(0, 0, 159, rows * k, fill="#446", outline="")
        c.create_rectangle(0, rows * k, 159, 100, fill="#664", outline="")
        c.create_text(80, rows * k / 2, text="video", fill="white")
        c.create_text(80, (rows * k + 100) / 2, text="band", fill="white")
        snd = [s for s in d["sounds"]]
        self.mcb.config(values=snd)
        m = d.get("music") or {}
        self.msound.set(m.get("sound", ""))
        rate = t["rate"]
        lp = m.get("loop")
        self.lstart.set("%.3f" % secs(lp[0], rate) if lp else "")
        self.lend.set("%.3f" % secs(lp[1], rate) if lp else "")
        self.join.set("%.3f" % secs(m.get("join", 0), rate) if m else "")

    def _swatches(self, b):
        import vbtana as A
        self.swatch.delete("all")
        for i, rgb in enumerate(A.palette_rgb(b)):
            self.swatch.create_rectangle(i * 44, 0, i * 44 + 40, 28,
                                         fill="#%02x%02x%02x" % rgb,
                                         outline="#888")

    def _set(self, key, v):
        if self.app.proj.d.get(key) != v:
            self.app.edit("set", (key,), v)

    def _set_t(self, key, v):
        if self.app.proj.d["target"].get(key) != v:
            self.app.frames.clear()     # another target: other screens
            self.app.edit("set", ("target", key), v)

    def _pal(self):
        b = palette_byte(self.pset.get(), self.pbright.get(),
                         BG_NAMES.index(self.pbg.get()))
        self._set_t("palette", b)

    def _rows(self):
        cv, band = sizes_for(self.rows.get())
        if self.app.proj.d["target"]["canvas"] != cv:
            self.app.frames.clear()
            p = self.app.proj

            def f(d):
                d["target"]["canvas"], d["target"]["band"] = cv, band
            p._edit(f)
            self.app.refresh()

    def _add_music(self):
        from tkinter import filedialog
        p = filedialog.askopenfilename(title="The music")
        if not p:
            return
        sid = os.path.splitext(os.path.basename(p))[0][:16] or "music"
        self.app.edit("add_sound", sid, path=p)
        self.app.edit("set_music", sid, 0)

    def _music_sound(self):
        self.app.edit("set_music", self.msound.get(), 0)

    def _rate(self):
        return self.app.proj.d["target"]["rate"]

    def _set_loop(self):
        try:
            a = samples_of(self.lstart.get(), self._rate())
            b = samples_of(self.lend.get(), self._rate())
        except ValueError:
            return self.app.say("A loop is two times in seconds")
        self.app.edit("set_music_loop", a, b)

    def _set_join(self):
        m = self.app.proj.d.get("music")
        if not m:
            return
        try:
            j = samples_of(self.join.get(), self._rate())
        except ValueError:
            return self.app.say("A time in seconds")
        self.app.edit("set", ("music", "join"), j)

    def _find(self):
        m = self.app.proj.d.get("music")
        if not m:
            return self.app.say("No music yet")

        def got(res):
            self.loops = res["loops"]
            self.lb.delete(0, "end")
            r = res["rate"]
            for c in self.loops:
                self.lb.insert("end", "%7.3f s to %7.3f s (%.2f s long): "
                               "alike %.0f%%" % (c["start"] / r, c["end"] / r,
                                                 (c["end"] - c["start"]) / r,
                                                 c["score"] * 100))
            if self.loops:
                self.lb.selection_set(0)
        self.app.run("vbtana", ["musicloops", self.app.proj.path, m["sound"]],
                     got, "Listening for the music's loops...")

    def _chosen(self):
        sel = self.lb.curselection()
        return self.loops[sel[0]] if sel and self.loops else None

    def _use(self):
        c = self._chosen()
        if c:
            self.app.edit("set_music_loop", c["start"], c["end"])

    def _listen(self, jump):
        m = self.app.proj.d.get("music")
        if not m:
            return self.app.say("No music yet")
        lp = None
        if jump:
            c = self._chosen()
            lp = (c["start"], c["end"]) if c else m.get("loop")
            if not lp:
                return self.app.say("No loop chosen")
        self.app.listen(m["sound"], lp)


# --- the Idle tab (4.2) ---------------------------------------------------------------

class IdleTab(object):
    def __init__(self, app, parent):
        tk, ttk = app.tk, app.ttk
        self.app = app
        self.f = f = ttk.Frame(parent, padding=8)
        self.cands = []
        top = ttk.Frame(f)
        top.pack(fill="x")
        ttk.Label(top, text="Source").pack(side="left")
        self.src = tk.StringVar()
        self.scb = ttk.Combobox(top, textvariable=self.src, state="readonly",
                                width=24)
        self.scb.pack(side="left", padx=4)
        self.scb.bind("<<ComboboxSelected>>", lambda ev: self._pick())
        ttk.Button(top, text="Add a video...", command=self._add).pack(
            side="left", padx=4)
        ttk.Button(top, text="Load its frames", command=self._load).pack(
            side="left", padx=4)
        mid = ttk.Frame(f)
        mid.pack(fill="both", expand=True, pady=6)
        self.pic = tk.Label(mid, bg="black", width=640, height=284)
        self.pic.pack(side="left", anchor="n")
        side = ttk.Frame(mid)
        side.pack(side="left", fill="y", padx=8)
        ttk.Label(side, text="Loop: frames").grid(row=0, column=0, sticky="w")
        self.la, self.lb_ = tk.IntVar(), tk.IntVar()
        ttk.Spinbox(side, from_=0, to=9999, textvariable=self.la, width=6
                    ).grid(row=0, column=1)
        ttk.Label(side, text="to (not incl.)").grid(row=0, column=2)
        ttk.Spinbox(side, from_=0, to=9999, textvariable=self.lb_, width=6
                    ).grid(row=0, column=3)
        ttk.Button(side, text="Set", command=self._set).grid(row=0, column=4)
        ttk.Button(side, text="Recommend", command=self._recommend
                   ).grid(row=1, column=0, pady=6, sticky="w")
        ttk.Button(side, text="Use", command=self._use).grid(row=1, column=1)
        self.cl = tk.Listbox(side, height=6, width=46)
        self.cl.grid(row=2, column=0, columnspan=5, sticky="we")
        self.play_b = ttk.Button(side, text="Play across the seam",
                                 command=self._play)
        self.play_b.grid(row=3, column=0, columnspan=2, pady=6, sticky="w")
        self.diff = tk.IntVar()
        ttk.Checkbutton(side, text="Difference at the seam", variable=self.diff,
                        command=self._show_seam).grid(row=3, column=2,
                                                      columnspan=3, sticky="w")
        ttk.Button(side, text="Cost it", command=self._cost
                   ).grid(row=4, column=0, sticky="w")
        self.cost = ttk.Label(side, text="", wraplength=330)
        self.cost.grid(row=5, column=0, columnspan=5, sticky="w")
        self.info = ttk.Label(side, text="", wraplength=330)
        self.info.grid(row=6, column=0, columnspan=5, sticky="w", pady=6)
        self.scrub = tk.Scale(f, from_=0, to=0, orient="horizontal",
                              command=lambda v: self._show(int(v)),
                              label="Frame")
        self.scrub.pack(fill="x")

    def refresh(self):
        d = self.app.proj.d
        vids = list(d["sources"])
        self.scb.config(values=vids)
        i = d.get("idle")
        if i:
            self.src.set(i["source"])
            self.la.set(i["loop"][0])
            self.lb_.set(i["loop"][1])
            fr = self.app.frames.get(i["source"])
            self.info.config(text="The loop: %d frames, %.2f s%s" % (
                i["loop"][1] - i["loop"][0],
                (i["loop"][1] - i["loop"][0]) / d["target"]["fps"],
                "" if fr is None else "; the source has %d" % len(fr)))
        else:
            self.info.config(text="No idle loop yet: pick a source")

    def _frames(self):
        return self.app.frames.get(self.src.get())

    def _add(self):
        from tkinter import filedialog
        p = filedialog.askopenfilename(title="A video")
        if not p:
            return
        sid = os.path.splitext(os.path.basename(p))[0][:16]
        self.app.edit("add_source", sid, path=p)
        self.src.set(sid)
        self._pick()

    def _pick(self):
        sid = self.src.get()
        i = self.app.proj.d.get("idle")
        if not i or i["source"] != sid:
            self.app.edit("set_idle", sid, (0, 2))
        self._load()

    def _load(self):
        sid = self.src.get()
        if not sid:
            return

        def got(fr):
            self.scrub.config(to=len(fr) - 1)
            self._show(self.app.proj.d["idle"]["loop"][0])
            self.refresh()
        self.app.screens(sid, got)

    def _show(self, n):
        fr = self._frames()
        if fr is None or not 0 <= n < len(fr):
            return
        self._img = self.app.photo(fr[n])
        self.pic.config(image=self._img, width=self._img.width(),
                        height=self._img.height())

    def _set(self):
        self.app.edit("set_idle", self.src.get(), (self.la.get(), self.lb_.get()))

    def _recommend(self):
        sid = self.src.get()
        if not sid:
            return

        def got(res):
            self.cands = res["loops"]
            self.cl.delete(0, "end")
            for c in self.cands:
                self.cl.insert("end", "frames %d to %d (%d frames): the seam "
                               "differs %.2f%%" % (c["start"], c["end"] - 1,
                                                   c["end"] - c["start"],
                                                   c["score"] * 100))
            if self.cands:
                self.cl.selection_set(0)
        self.app.run("vbtana", ["loops", self.app.proj.path, sid], got,
                     "Looking for the loop...")

    def _use(self):
        sel = self.cl.curselection()
        if sel and self.cands:
            c = self.cands[sel[0]]
            self.app.edit("set_idle", self.src.get(), (c["start"], c["end"]))
            self._show(c["start"])

    def _play(self):
        fr = self._frames()
        if fr is None:
            return self.app.say("Load the frames first")
        if self.app.anim:
            self.app.root.after_cancel(self.app.anim)
            self.app.anim = None
            self.play_b.config(text="Play across the seam")
            return
        order = seam_order(self.app.proj.d["idle"]["loop"])
        imgs = [self.app.photo(fr[n]) for n in order]
        self.play_b.config(text="Stop")
        step = [0]

        def tick():
            self._img = imgs[step[0] % len(imgs)]
            self.pic.config(image=self._img)
            step[0] += 1
            self.app.anim = self.app.root.after(
                1000 // self.app.proj.d["target"]["fps"], tick)
        tick()

    def _show_seam(self):
        import vbtana as A
        from PIL import Image, ImageTk
        fr = self._frames()
        if fr is None:
            return
        a, b = self.app.proj.d["idle"]["loop"]
        if not self.diff.get():
            return self._show(a)
        rgb = A.diff_rgb(fr[b - 1], fr[a], self.app.proj.d["target"]["palette"])
        im = Image.fromarray(rgb)
        im = im.resize((im.width * SCALE, im.height * SCALE), Image.NEAREST)
        self._img = ImageTk.PhotoImage(im)
        self.pic.config(image=self._img)
        self.app.say("The seam: frame %d onto frame %d, %.2f%% of pixels in red"
                     % (b - 1, a, A.diff(fr[b - 1], fr[a]) * 100))

    def _cost(self):
        def got(res):
            self.cost.config(text="Held in memory: %d records, %.1f KB; the "
                             "seam's record %d bytes; the largest %d bytes"
                             % (res["frames"], res["total"] / 1024.0,
                                res["seam"], res["largest"]))
        self.app.run("vbtana", ["cost", self.app.proj.path], got,
                     "Costing the loop...")


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser(description=TITLE)
    ap.add_argument("project", nargs="?")
    a = ap.parse_args(argv)
    App(a.project).root.mainloop()


if __name__ == "__main__":
    main()
