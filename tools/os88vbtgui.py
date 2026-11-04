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


def thumb_image(px, pal, w=80):
    """A small picture of a frame, for the filmstrip"""
    from PIL import Image
    import vbtana as A
    im = Image.fromarray(A.to_rgb(px, pal))
    return im.resize((w, max(1, im.height * w // im.width)), Image.BOX)


def cue_x(frame, nframes, width):
    """A cue's place on a timeline WIDTH pixels wide"""
    return int(round((frame + 0.5) * width / max(1, nframes)))


def x_cue(x, nframes, width):
    """...and the frame a place on it is"""
    return max(0, min(nframes - 1, int(x * nframes / max(1, width))))


def clip_flags(c):
    if c.get("ends"):
        return "ending"
    return "joins at %s, exits to %d" % (
        ", ".join(map(str, c["anchors"])) or "nothing", c["exit"])


# --- the rules tabs' arithmetic (wave 4) ------------------------------------------

# the fields each verb reads (vbtrules.compile_rules): (key, label, kind,
# default, choices). "amount" is FIXED: no formula, no variance
EFFECT_FIELDS = {
    "damage": [("kind", "Kind", "choice", "physical",
                ("physical", "magical", "fixed")),
               ("power", "Power (or the amount, fixed)", "int", 32, None),
               ("hits", "Hits", "int", 1, None)],
    "heal": [("kind", "By", "choice", "power", ("power", "percent", "fixed")),
             ("power", "Power, % or amount", "int", 32, None)],
    "restore": [("amount", "MP", "int", 10, None)],
    "revive": [("percent", "HP %", "int", 25, None)],
    "guard": [], "charge": [], "pass": [], "escape": [],
    "boost": [("stat", "Stat", "choice", "atk", ("atk", "mag", "def", "mdef")),
              ("steps", "Steps (1-4)", "int", 1, None),
              ("turns", "Turns", "int", 3, None)],
    "lower": [("stat", "Stat", "choice", "def", ("atk", "mag", "def", "mdef")),
              ("steps", "Steps (1-4)", "int", 1, None),
              ("turns", "Turns", "int", 3, None)],
    "stance": [("stance", "Stance", "stance", "", None),
               ("duration", "Turns (0: until changed)", "int", 0, None)],
}


def effect_form(e):
    """An effect as the form shows it: {field: value}, the "kind" of a
    damage or heal spelled out (compile_rules reads amount, percent and
    power by which is present)"""
    v = e.get("verb")
    f = {}
    if v == "damage":
        if "amount" in e:
            f.update(kind="fixed", power=e["amount"])
        else:
            f.update(kind=e.get("kind", "physical"), power=e.get("power", 32))
        f["hits"] = e.get("hits", 1)
    elif v == "heal":
        if "amount" in e:
            f.update(kind="fixed", power=e["amount"])
        elif "percent" in e:
            f.update(kind="percent", power=e["percent"])
        else:
            f.update(kind="power", power=e.get("power", 32))
    else:
        for key, _, _, dflt, _ in EFFECT_FIELDS.get(v, []):
            f[key] = e.get(key, dflt)
    return f


def effect_from(verb, f):
    """...and back: the effect the model stores"""
    e = {"verb": verb}
    if verb == "damage":
        if f["kind"] == "fixed":
            e["amount"] = int(f["power"])
        else:
            e.update(kind=f["kind"], power=int(f["power"]))
        e["hits"] = max(1, min(4, int(f.get("hits", 1))))
    elif verb == "heal":
        k = {"fixed": "amount", "percent": "percent"}.get(f["kind"], "power")
        e[k] = int(f["power"])
    else:
        for key, _, kind, _, _ in EFFECT_FIELDS.get(verb, []):
            e[key] = int(f[key]) if kind == "int" else f[key]
    return e


def effect_text(e):
    v = e.get("verb", "?")
    f = effect_form(e)
    if v == "damage":
        return "DAMAGE %s %s%s" % (f["kind"], f["power"],
                                   " x%d" % f["hits"] if f["hits"] > 1 else "")
    if v == "heal":
        return "HEAL %s%s" % (f["power"], "%" if f["kind"] == "percent" else
                              " fixed" if f["kind"] == "fixed" else "")
    if v in ("boost", "lower"):
        return "%s %s %d for %d turns" % (v.upper(), f["stat"], f["steps"],
                                          f["turns"])
    if v == "stance":
        return "STANCE %s%s" % (f["stance"], " for %d turns" % f["duration"]
                                if f["duration"] else "")
    if v == "restore":
        return "RESTORE %d MP" % f["amount"]
    if v == "revive":
        return "REVIVE to %d%%" % f["percent"]
    return v.upper()


def users_of(proj, aid):
    """Who takes action AID: the characters whose menus hold it, and the
    enemy when a move, response or change does"""
    d = proj.d
    out = []
    for i, ch in enumerate(d["characters"]):
        acts = []
        for it in ch["menu"]["top"]:
            acts += [it["action"]] if "action" in it else it["sub"]
        acts += ch["menu"]["side"]
        if aid in acts:
            out.append(i)
    for st in d["enemy"]["stances"]:
        names = [m["action"] for m in st["moves"]] + \
            [r["action"] for r in st.get("responses", [])] + \
            [x for x in (st.get("return"), (st.get("auto") or {}).get("via")) if x]
        if aid in names:
            out.append("enemy")
            break
    return out


def damage_preview(proj, aid):
    """Lines saying what action AID does, from the reference's own
    arithmetic (vbtrules.Sim.damage and heal_amount) at the battle's start:
    each user against each opponent, the variance's ends, the critical"""
    import vbtrules as V
    d = proj.d
    try:
        idx = {c: i for i, c in enumerate(d["clips"])}
        R = V.Rules(V.compile_rules(proj, idx, [(0, 0, 1, 1, 0)] *
                                    len(d.get("images", []))))
    except Exception as e:
        return ["(not until the rules compile: %s)" % e]
    sim = V.Sim(R)
    a = d["actions"][aid]
    ai = list(d["actions"]).index(aid)
    eff = R.acts[ai]["effects"]
    out = []
    for u in users_of(proj, aid):
        who = V.ENEMY if u == "enemy" else u
        wname = R.ename if u == "enemy" else R.chars[u]["name"]
        opps = list(range(R.nchar)) if u == "enemy" else [V.ENEMY]
        for (v, aa, pw, bb, cc) in eff:
            if v == V.VERBS["damage"]:
                for o in opps:
                    oname = R.ename if o == V.ENEMY else R.chars[o]["name"]
                    lo = sim.damage(aa, pw, who, o, 90) * bb
                    hi = sim.damage(aa, pw, who, o, 110) * bb
                    out.append("%s on %s: %d to %d%s" % (
                        wname, oname, lo, hi,
                        ", critical %d" % min(V.CAP, hi * 2)
                        if aa == V.KIND_PHYS else ""))
            elif v == V.VERBS["heal"]:
                lo = sim.heal_amount(aa, pw, who, 0 if who != V.ENEMY else who, 90)
                hi = sim.heal_amount(aa, pw, who, 0 if who != V.ENEMY else who, 110)
                out.append("%s heals %d to %d" % (wname, lo, hi))
    acc = a.get("accuracy", 100)
    if acc < 100:
        out.append("Hits %d%% of the time, less the target's evade" % acc)
    if not out:
        out.append("(no one takes it yet: put it on a menu or a stance)"
                   if not users_of(proj, aid) else "(no damage or healing)")
    return out


def menu_rows(proj, ci):
    """Character CI's menus as rows (where, index, sub index, text) for
    the tree: the top menu's items (a sub-menu's under it), then the side
    menu's"""
    ch = proj.d["characters"][ci]
    rows = []
    for i, it in enumerate(ch["menu"]["top"]):
        if "action" in it:
            rows.append(("top", i, None, it["action"]))
        else:
            rows.append(("top", i, None, "%s >" % it["label"]))
            for j, a in enumerate(it["sub"]):
                rows.append(("sub", j, i, a))
    for i, a in enumerate(ch["menu"]["side"]):
        rows.append(("side", i, None, a))
    return rows


def look_key(proj, ci, where, sub):
    """The menu_looks key of a menu: CHAR:top, CHAR:side, CHAR:LABEL"""
    name = proj.d["characters"][ci]["name"]
    if where == "side":
        return name + ":side"
    if sub is not None:
        return name + ":" + proj.d["characters"][ci]["menu"]["top"][sub]["label"]
    return name + ":top"


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
        self.root.geometry("1300x760")
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
        self.clips_tab = ClipsTab(self, self.nb)
        self.actions_tab = ActionsTab(self, self.nb)
        self.party_tab = PartyTab(self, self.nb)
        self.enemy_tab = EnemyTab(self, self.nb)
        self.nb.add(self.battle_tab.f, text="Battle")
        self.nb.add(self.idle_tab.f, text="Idle")
        self.nb.add(self.clips_tab.f, text="Clips")
        self.nb.add(self.actions_tab.f, text="Actions")
        self.nb.add(self.party_tab.f, text="Characters & menus")
        self.nb.add(self.enemy_tab.f, text="Enemy")
        for name, wave in (("Build", 5),):
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
        self.clips_tab.refresh()
        self.actions_tab.refresh()
        self.party_tab.refresh()
        self.enemy_tab.refresh()

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


# --- the Clips tab (4.3, 10.2) --------------------------------------------------------

class ClipsTab(object):
    """A clip: its source and range, whether the battle ends on it, its
    ANCHORS and EXIT against the idle loop's filmstrip (each frame's score
    and its entry's bytes), the DIFFERENCE between the clip's frame and an
    idle frame, the longest wait, and its EFFECTS placed on its timeline"""
    TW = 80             # a filmstrip thumbnail's width

    def __init__(self, app, parent):
        tk, ttk = app.tk, app.ttk
        self.app = app
        self.f = f = ttk.Frame(parent, padding=6)
        self.rep = None             # the clip_report of the clip shown
        self.sel = 0                # the idle frame selected on the strip
        self.thumbs = {}
        self.drag = None
        left = ttk.Frame(f)
        left.grid(row=0, column=0, sticky="ns")
        self.lb = tk.Listbox(left, height=14, width=26, exportselection=False)
        self.lb.pack(fill="y")
        self.lb.bind("<<ListboxSelect>>", lambda e: self._pick())
        bb = ttk.Frame(left)
        bb.pack(fill="x", pady=4)
        ttk.Button(bb, text="New clip...", command=self._new).pack(side="left")
        ttk.Button(bb, text="Delete", command=self._delete).pack(side="left")
        ttk.Label(left, text="Source").pack(anchor="w")
        self.src = tk.StringVar()
        self.scb = ttk.Combobox(left, textvariable=self.src, state="readonly",
                                width=24)
        self.scb.pack(anchor="w")
        self.scb.bind("<<ComboboxSelected>>", lambda e: self._source())
        rf = ttk.Frame(left)
        rf.pack(anchor="w", pady=4)
        ttk.Label(rf, text="In").grid(row=0, column=0)
        self.cin, self.cout = tk.IntVar(), tk.IntVar()
        ttk.Spinbox(rf, from_=0, to=99999, width=6, textvariable=self.cin
                    ).grid(row=0, column=1)
        ttk.Label(rf, text="Out").grid(row=0, column=2)
        ttk.Spinbox(rf, from_=0, to=99999, width=6, textvariable=self.cout
                    ).grid(row=0, column=3)
        ttk.Button(rf, text="Set", command=self._range).grid(row=0, column=4)
        self.ends = tk.IntVar()
        ttk.Checkbutton(left, text="The battle ends on it (held, no exit)",
                        variable=self.ends, command=self._ends
                        ).pack(anchor="w")
        ttk.Button(left, text="Recommend", command=self._show_rec
                   ).pack(anchor="w", pady=(8, 0))
        self.rec_l = ttk.Label(left, text="", wraplength=200)
        self.rec_l.pack(anchor="w")
        ttk.Button(left, text="Use the recommendation", command=self._use
                   ).pack(anchor="w")
        # the picture and the clip's own timeline
        mid = ttk.Frame(f)
        mid.grid(row=0, column=1, sticky="n", padx=6)
        self.pic = tk.Label(mid, bg="black")
        self.pic.pack()
        self.scrub = tk.Scale(mid, from_=0, to=0, orient="horizontal",
                              length=640, label="The clip's frame",
                              command=lambda v: self._show())
        self.scrub.pack()
        self.tl = tk.Canvas(mid, width=640, height=44, bg="#223",
                            highlightthickness=0)
        self.tl.pack()
        self.tl.bind("<Button-1>", self._tl_down)
        self.tl.bind("<B1-Motion>", self._tl_move)
        self.tl.bind("<ButtonRelease-1>", self._tl_up)
        # the right: what the selection costs, the difference, the effects
        right = ttk.Frame(f)
        right.grid(row=0, column=2, sticky="n")
        self.diff = tk.IntVar()
        ttk.Checkbutton(right, text="Difference from the selected idle frame",
                        variable=self.diff, command=self._show
                        ).pack(anchor="w")
        self.sel_l = ttk.Label(right, text="", wraplength=300)
        self.sel_l.pack(anchor="w", pady=4)
        sb = ttk.Frame(right)
        sb.pack(anchor="w")
        ttk.Button(sb, text="Anchor on/off", command=self._toggle
                   ).pack(side="left")
        ttk.Button(sb, text="Exit onto it", command=self._exit
                   ).pack(side="left")
        self.wait_l = ttk.Label(right, text="", wraplength=300)
        self.wait_l.pack(anchor="w", pady=4)
        ef = ttk.LabelFrame(right, text="Effects on its timeline", padding=4)
        ef.pack(fill="x", pady=6)
        self.esnd = tk.StringVar()
        self.ecb = ttk.Combobox(ef, textvariable=self.esnd, state="readonly",
                                width=18)
        self.ecb.grid(row=0, column=0, columnspan=2, sticky="w")
        ttk.Button(ef, text="Add at this frame", command=self._add_cue
                   ).grid(row=0, column=2)
        ttk.Button(ef, text="A sound file...", command=self._add_file
                   ).grid(row=1, column=0, sticky="w")
        ttk.Button(ef, text="Its own soundtrack", command=self._own
                   ).grid(row=1, column=1, columnspan=2, sticky="w")
        self.cues = tk.Listbox(ef, height=5, width=36, exportselection=False)
        self.cues.grid(row=2, column=0, columnspan=3, sticky="we", pady=4)
        ttk.Label(ef, text="Level").grid(row=3, column=0, sticky="e")
        self.lvl = tk.StringVar(value="1.0")
        ttk.Entry(ef, textvariable=self.lvl, width=6).grid(row=3, column=1,
                                                           sticky="w")
        ttk.Button(ef, text="Set", command=self._level).grid(row=3, column=2,
                                                             sticky="w")
        ttk.Button(ef, text="Remove", command=self._rm_cue
                   ).grid(row=4, column=0, sticky="w")
        ttk.Button(ef, text="Listen to the clip's sound", command=self._listen
                   ).grid(row=4, column=1, columnspan=2, sticky="w")
        # the idle loop's filmstrip
        sf = ttk.LabelFrame(f, text="The idle loop: click a frame to select "
                            "it; double-click toggles an anchor; right-click "
                            "makes it the exit", padding=2)
        sf.grid(row=1, column=0, columnspan=3, sticky="we", pady=4)
        f.columnconfigure(2, weight=1)
        self.strip = tk.Canvas(sf, height=118, bg="#111",
                               highlightthickness=0)
        hs = ttk.Scrollbar(sf, orient="horizontal", command=self.strip.xview)
        self.strip.config(xscrollcommand=hs.set)
        self.strip.pack(fill="x")
        hs.pack(fill="x")
        self.strip.bind("<Button-1>", lambda e: self._strip(e, "sel"))
        self.strip.bind("<Double-Button-1>", lambda e: self._strip(e, "anchor"))
        self.strip.bind("<Button-3>", lambda e: self._strip(e, "exit"))

    # --- what is shown --------------------------------------------------------
    def cid(self):
        sel = self.lb.curselection()
        names = list(self.app.proj.d["clips"])
        return names[sel[0]] if sel and sel[0] < len(names) else None

    def refresh(self):
        d = self.app.proj.d
        keep = self.cid()
        self.lb.delete(0, "end")
        for cid, c in d["clips"].items():
            self.lb.insert("end", "%s  (%s)" % (cid, clip_flags(c)))
        names = list(d["clips"])
        if keep in names:
            self.lb.selection_set(names.index(keep))
        self.scb.config(values=list(d["sources"]))
        self.ecb.config(values=list(d["sounds"]))
        cid = self.cid()
        if not cid:
            return
        c = d["clips"][cid]
        self.src.set(c["source"])
        self.cin.set(c["in"])
        self.cout.set(c["out"])
        self.ends.set(1 if c.get("ends") else 0)
        self.scrub.config(to=c["out"] - c["in"])
        self.cues.delete(0, "end")
        for q in c["cues"]:
            self.cues.insert("end", "frame %3d  %-16s level %.2f" % (
                q["frame"], q["sound"], q.get("level", 1.0)))
        if self.rep is not None:
            self._report()

    def _pick(self):
        cid = self.cid()
        if not cid or not self.app.proj.d.get("idle"):
            return
        c = self.app.proj.d["clips"][cid]
        idle_src = self.app.proj.d["idle"]["source"]

        def go(_):
            self.app.screens(c["source"], lambda __: self._report())
        self.app.screens(idle_src, go)
        self.refresh()

    def _report(self):
        import vbtana as A
        cid = self.cid()
        if not cid:
            return
        c = self.app.proj.d["clips"][cid]
        if c["source"] not in self.app.frames or \
                self.app.proj.d["idle"]["source"] not in self.app.frames:
            self.rep = None         # (encoding is a job's: _pick starts it)
            return
        self.rep = A.clip_report(self.app.proj, cid)
        self._strip_draw()
        self._show()
        n = self.app.proj.idle_frames()
        w = self.rep["wait"]
        self.wait_l.config(text="The longest wait for an anchor: %s" % (
            "none set - it can never start" if w is None else
            "%d frames, %.2f s" % (w, w / self.app.proj.d["target"]["fps"])))
        r = self.rep["recommend"]
        self.rec_l.config(text="Recommended: in at %d, out at %d, anchors %s, "
                          "%s" % (r["first"], r["last"], r["anchors"],
                                  "no exit (an ending)" if r["exit"] is None
                                  else "exit onto %d" % r["exit"]))
        self._sel_text()

    def _frames(self):
        c = self.app.proj.d["clips"][self.cid()]
        return self.app.frames.get(c["source"]), c

    def _idle(self):
        i = self.app.proj.d["idle"]
        fr = self.app.frames.get(i["source"])
        return None if fr is None else fr[i["loop"][0]:i["loop"][1]]

    def _show(self):
        import vbtana as A
        from PIL import Image, ImageTk
        if not self.cid():
            return
        fr, c = self._frames()
        idle = self._idle()
        if fr is None:
            return
        n = int(self.scrub.get())
        px = fr[c["in"] + n]
        pal = self.app.proj.d["target"]["palette"]
        if self.diff.get() and idle is not None:
            im = Image.fromarray(A.diff_rgb(px, idle[self.sel], pal))
            im = im.resize((im.width * SCALE, im.height * SCALE), Image.NEAREST)
            self._img = ImageTk.PhotoImage(im)
        else:
            self._img = self.app.photo(px)
        self.pic.config(image=self._img)
        self._tl_draw()

    def _strip_draw(self):
        from PIL import ImageTk
        cv = self.strip
        cv.delete("all")
        idle = self._idle()
        if idle is None or self.rep is None:
            return
        c = self.app.proj.d["clips"][self.cid()]
        pal = self.app.proj.d["target"]["palette"]
        key = (self.app.proj.d["idle"]["source"], pal, len(idle))
        if self.thumbs.get("key") != key:
            self.thumbs = {"key": key, "img": [ImageTk.PhotoImage(
                thumb_image(f, pal, self.TW)) for f in idle]}
        rec = set(self.rep["recommend"]["anchors"])
        best = min(self.rep["entry_scores"])
        for k, im in enumerate(self.thumbs["img"]):
            x = k * (self.TW + 4) + 2
            cv.create_image(x, 2, image=im, anchor="nw")
            h = im.height()
            if k in c["anchors"]:
                cv.create_rectangle(x - 2, 0, x + self.TW + 1, h + 3,
                                    outline="#ff0", width=2)
            if not c.get("ends") and k == c["exit"]:
                cv.create_rectangle(x - 1, 1, x + self.TW, h + 2,
                                    outline="#0cf", width=2, dash=(3, 2))
            if k == self.sel:
                cv.create_rectangle(x, h + 4, x + self.TW - 1, h + 6,
                                    fill="#fff", outline="")
            sc = self.rep["entry_scores"][k]
            ln = int(min(1.0, sc / max(best * 3, 1e-6)) * (self.TW - 2))
            cv.create_rectangle(x, h + 9, x + self.TW - 2 - ln, h + 15,
                                fill="#3c3" if k in rec else "#686",
                                outline="")
            cv.create_text(x, h + 18, anchor="nw", fill="#ddd",
                           font=("TkDefaultFont", 7),
                           text="%d: %.1f%% %dB" % (
                               k, sc * 100, self.rep["entry_bytes"][k]))
            cv.create_text(x, h + 31, anchor="nw", fill="#8cf",
                           font=("TkDefaultFont", 7),
                           text="out %.1f%%" % (self.rep["exit_scores"][k] * 100))
        cv.config(scrollregion=(0, 0, len(idle) * (self.TW + 4), 118))

    def _sel_text(self):
        if self.rep is None:
            return
        k = self.sel
        c = self.app.proj.d["clips"][self.cid()]
        self.sel_l.config(text="Idle frame %d: the clip's in frame differs "
                          "%.2f%%, an entry from it would be %d bytes%s. "
                          "Its out frame differs %.2f%%%s." % (
                              k, self.rep["entry_scores"][k] * 100,
                              self.rep["entry_bytes"][k],
                              " (an anchor)" if k in c["anchors"] else "",
                              self.rep["exit_scores"][k] * 100,
                              " (the exit)" if k == c["exit"] and
                              not c.get("ends") else ""))

    def _strip(self, e, what):
        if self.rep is None:
            return
        x = self.strip.canvasx(e.x)
        k = int(x // (self.TW + 4))
        if not 0 <= k < len(self.rep["entry_scores"]):
            return
        self.sel = k
        cid = self.cid()
        if what == "anchor":
            self.app.edit("toggle_anchor", cid, k)
        elif what == "exit":
            self.app.edit("set_exit", cid, k)
        else:
            self._strip_draw()
            self._sel_text()
            self._show()

    # --- the clip's timeline: its effects, dragged -----------------------------
    def _tl_draw(self):
        cv = self.tl
        cv.delete("all")
        if not self.cid():
            return
        c = self.app.proj.d["clips"][self.cid()]
        n = c["out"] - c["in"] + 1
        w = int(cv["width"])
        for fr in range(0, n, 15):
            x = cue_x(fr, n, w)
            cv.create_line(x, 34, x, 44, fill="#556")
            cv.create_text(x + 2, 36, anchor="nw", fill="#778",
                           font=("TkDefaultFont", 7), text="%.0fs" % (fr / 15.0))
        x = cue_x(int(self.scrub.get()), n, w)
        cv.create_line(x, 0, x, 44, fill="#fff")
        for i, q in enumerate(c["cues"]):
            x = cue_x(q["frame"], n, w)
            cv.create_polygon(x - 6, 2, x + 6, 2, x, 14, fill="#fc3",
                              tags=("cue", "c%d" % i))
            cv.create_text(x + 8, 4, anchor="nw", fill="#fc3",
                           font=("TkDefaultFont", 7), text=q["sound"])

    def _tl_down(self, e):
        c = self.app.proj.d["clips"].get(self.cid())
        if not c:
            return
        n = c["out"] - c["in"] + 1
        w = int(self.tl["width"])
        for i, q in enumerate(c["cues"]):
            if abs(cue_x(q["frame"], n, w) - e.x) <= 7 and e.y <= 18:
                self.drag = i
                self.cues.selection_clear(0, "end")
                self.cues.selection_set(i)
                return
        self.drag = None
        self.scrub.set(x_cue(e.x, n, w))

    def _tl_move(self, e):
        if self.drag is None:
            c = self.app.proj.d["clips"].get(self.cid())
            if c:
                self.scrub.set(x_cue(e.x, c["out"] - c["in"] + 1,
                                     int(self.tl["width"])))
            return
        self.tl.delete("ghost")
        self.tl.create_line(e.x, 0, e.x, 16, fill="#fc3", tags="ghost")

    def _tl_up(self, e):
        if self.drag is None:
            return
        c = self.app.proj.d["clips"][self.cid()]
        fr = x_cue(e.x, c["out"] - c["in"] + 1, int(self.tl["width"]))
        i, self.drag = self.drag, None
        if fr != c["cues"][i]["frame"]:
            self.app.edit("move_cue", self.cid(), i, fr)

    # --- the edits ---------------------------------------------------------------
    def _new(self):
        from tkinter import simpledialog
        d = self.app.proj.d
        if not d["sources"]:
            return self.app.say("Add a video first (the Idle tab's Add a "
                                "video adds a source)")
        sid = self.src.get() or next(iter(d["sources"]))
        name = simpledialog.askstring(TITLE, "The new clip's name (from "
                                      "source %s):" % sid, parent=self.f)
        if not name:
            return
        name = name.strip().replace(" ", "_")[:16]

        def got(fr):
            self.app.edit("add_clip", name, sid, (0, len(fr) - 1), anchors=())
            names = list(self.app.proj.d["clips"])
            self.lb.selection_clear(0, "end")
            self.lb.selection_set(names.index(name))
            self._pick()
        self.app.screens(sid, got)

    def _delete(self):
        if self.cid():
            self.app.edit("remove_clip", self.cid())

    def _source(self):
        cid = self.cid()
        if cid and self.src.get() != self.app.proj.d["clips"][cid]["source"]:
            self.app.edit("set", ("clips", cid, "source"), self.src.get())
            self._pick()

    def _range(self):
        cid = self.cid()
        if cid:
            self.app.edit("set_clip_range", cid, self.cin.get(),
                          self.cout.get())

    def _ends(self):
        if self.cid():
            self.app.edit("set_ends", self.cid(), self.ends.get())

    def _show_rec(self):
        self._pick()

    def _use(self):
        cid = self.cid()
        if not cid or self.rep is None:
            return
        r = self.rep["recommend"]
        p = self.app.proj

        def f(d):
            c = d["clips"][cid]
            shift = r["first"] - c["in"]
            c["in"], c["out"] = r["first"], r["last"]
            n = c["out"] - c["in"] + 1
            c["cues"] = [dict(q, frame=q["frame"] - shift) for q in c["cues"]
                         if 0 <= q["frame"] - shift < n]
            c["anchors"] = sorted(r["anchors"])
            if r["exit"] is not None:
                c["exit"] = r["exit"]
            own = d["sounds"].get(P.own_sound(cid))
            if own and own["kind"] == "source":
                own["in"], own["out"] = c["in"], c["out"]
        p._edit(f)
        self.app.refresh()

    def _toggle(self):
        if self.cid():
            self.app.edit("toggle_anchor", self.cid(), self.sel)

    def _exit(self):
        if self.cid():
            self.app.edit("set_exit", self.cid(), self.sel)

    def _add_cue(self):
        if self.cid() and self.esnd.get():
            self.app.edit("add_cue", self.cid(), int(self.scrub.get()),
                          self.esnd.get())

    def _add_file(self):
        from tkinter import filedialog
        p = filedialog.askopenfilename(title="A sound effect")
        if not p:
            return
        sid = os.path.splitext(os.path.basename(p))[0][:16]
        self.app.edit("add_sound", sid, path=p)
        self.esnd.set(sid)

    def _own(self):
        if self.cid():
            self.app.edit("use_own_sound", self.cid())

    def _cue_i(self):
        s = self.cues.curselection()
        return s[0] if s else None

    def _level(self):
        i = self._cue_i()
        if i is None or not self.cid():
            return
        try:
            self.app.edit("set_cue_level", self.cid(), i, float(self.lvl.get()))
        except ValueError:
            self.app.say("A level is a number: 1.0 is as recorded")

    def _rm_cue(self):
        i = self._cue_i()
        if i is not None and self.cid():
            self.app.edit("remove_cue", self.cid(), i)

    def _listen(self):
        cid = self.cid()
        if not cid:
            return
        out = os.path.join(tempfile.gettempdir(), "vbtgui-clip.wav")

        def got(res):
            if res.get("silent"):
                self.app.say("The clip is silent: no effect on it yet")
            elif not play_wav(res["out"]):
                self.app.say("No player here: the WAV is %s" % res["out"])
        self.app.run("vbtana", ["clipwav", self.app.proj.path, cid, out], got,
                     "Mixing the clip's sound...")


# --- small form pieces ------------------------------------------------------------------

class Rows(object):
    """A frame of labelled fields in a grid, each committing on Return or
    on leaving it"""

    def __init__(self, app, parent):
        self.app, self.f = app, app.ttk.Frame(parent)
        self.r = 0

    def entry(self, label, value, commit, width=10):
        tk, ttk = self.app.tk, self.app.ttk
        ttk.Label(self.f, text=label).grid(row=self.r, column=0, sticky="w")
        v = tk.StringVar(value=str(value))
        e = ttk.Entry(self.f, textvariable=v, width=width)
        e.grid(row=self.r, column=1, sticky="w", pady=1)
        def go(ev=None, v=v, last=[str(value)]):
            if v.get() != last[0]:
                last[0] = v.get()
                commit(v.get())
        e.bind("<Return>", go)
        e.bind("<FocusOut>", go)
        self.r += 1
        return v

    def choice(self, label, value, choices, commit, width=16):
        tk, ttk = self.app.tk, self.app.ttk
        ttk.Label(self.f, text=label).grid(row=self.r, column=0, sticky="w")
        v = tk.StringVar(value=value)
        cb = ttk.Combobox(self.f, textvariable=v, values=list(choices),
                          state="readonly", width=width)
        cb.grid(row=self.r, column=1, sticky="w", pady=1)
        cb.bind("<<ComboboxSelected>>", lambda ev: commit(v.get()))
        self.r += 1
        return v


def as_int(text):
    return int(str(text).strip() or 0)


def clear(frame):
    for w in frame.winfo_children():
        w.destroy()


# --- the Actions tab (11.2, 11.2.2) -------------------------------------------------------

class ActionsTab(object):
    """An action: its label, target, cost, accuracy, up to four effects, its
    clip pools (general, or one character's), and what the reference's
    arithmetic says it does"""

    def __init__(self, app, parent):
        tk, ttk = app.tk, app.ttk
        self.app = app
        self.f = f = ttk.Frame(parent, padding=6)
        left = ttk.Frame(f)
        left.pack(side="left", fill="y")
        self.lb = tk.Listbox(left, height=24, width=24, exportselection=False)
        self.lb.pack(fill="y")
        self.lb.bind("<<ListboxSelect>>", lambda e: self.show())
        bb = ttk.Frame(left)
        bb.pack(fill="x")
        ttk.Button(bb, text="New...", command=self._new).pack(side="left")
        ttk.Button(bb, text="Delete", command=self._delete).pack(side="left")
        self.body = ttk.Frame(f)
        self.body.pack(side="left", fill="both", expand=True, padx=8)
        self.who = "(general)"

    def aid(self):
        s = self.lb.curselection()
        names = list(self.app.proj.d["actions"])
        return names[s[0]] if s and s[0] < len(names) else None

    def refresh(self):
        keep = self.aid()
        self.lb.delete(0, "end")
        names = list(self.app.proj.d["actions"])
        for a in names:
            self.lb.insert("end", "%s  (%s)" % (a, self.app.proj.d["actions"][a]["label"]))
        if keep in names:
            self.lb.selection_set(names.index(keep))
        self.show()

    def _new(self):
        from tkinter import simpledialog
        name = simpledialog.askstring(TITLE, "The new action's name:", parent=self.f)
        if not name:
            return
        aid = name.strip().replace(" ", "_")[:16]
        self.app.edit("add_action", aid, name.strip()[:8],
                      effects=[{"verb": "damage", "kind": "physical", "power": 32}])
        names = list(self.app.proj.d["actions"])
        self.lb.selection_clear(0, "end")
        self.lb.selection_set(names.index(aid))
        self.show()

    def _delete(self):
        if self.aid():
            self.app.edit("remove_action", self.aid())

    def show(self):
        tk, ttk = self.app.tk, self.app.ttk
        clear(self.body)
        aid = self.aid()
        if not aid:
            ttk.Label(self.body, text="Pick an action, or make one").pack(anchor="w")
            return
        P_ = self.app.proj
        a = P_.d["actions"][aid]
        top = ttk.Frame(self.body)
        top.pack(fill="x")
        form = Rows(self.app, top)
        form.f.pack(side="left", anchor="n")
        form.entry("Label (8)", a["label"],
                   lambda v: self.app.edit("set", ("actions", aid, "label"), v[:8]))
        form.choice("Target", a["target"], P.TARGETS,
                    lambda v: self.app.edit("set", ("actions", aid, "target"), v))
        cost = a.get("cost") or {}
        kind = "MP" if cost.get("mp") else "item" if cost.get("item") else \
            "limit" if cost.get("limit") else "none"
        form.choice("Cost", kind, ("none", "MP", "item", "limit"),
                    lambda v: self._cost(aid, v))
        if kind == "MP":
            form.entry("  MP", cost["mp"], lambda v: self.app.edit(
                "set", ("actions", aid, "cost"), {"mp": as_int(v)}))
        elif kind == "item":
            items = [it["name"] for it in P_.d.get("items", [])]
            form.choice("  Item", cost.get("item", ""), items, lambda v: self.app.edit(
                "set", ("actions", aid, "cost"), {"item": v, "n": cost.get("n", 1)}))
        form.entry("Accuracy %", a.get("accuracy", 100), lambda v: self.app.edit(
            "set", ("actions", aid, "accuracy"), max(0, min(100, as_int(v)))))
        pv = ttk.LabelFrame(top, text="What it does (the reference's "
                            "arithmetic, at the battle's start)", padding=4)
        pv.pack(side="left", fill="both", expand=True, padx=8, anchor="n")
        for line in damage_preview(P_, aid):
            ttk.Label(pv, text=line).pack(anchor="w")
        # effects
        ef = ttk.LabelFrame(self.body, text="Effects, in order (up to 4)", padding=4)
        ef.pack(fill="x", pady=4)
        for i, e in enumerate(a["effects"]):
            self._effect_row(ef, aid, i, e)
        if len(a["effects"]) < 4:
            ttk.Button(ef, text="Add an effect", command=lambda: self._add_effect(aid)
                       ).pack(anchor="w")
        # pools
        pf = ttk.LabelFrame(self.body, text="Clips, by outcome (one chosen at "
                            "random; empty falls back to HIT)", padding=4)
        pf.pack(fill="x", pady=4)
        names = ["(general)"] + [c["name"] for c in P_.d["characters"]]
        wv = tk.StringVar(value=self.who if self.who in names else "(general)")
        row = ttk.Frame(pf)
        row.pack(fill="x")
        ttk.Label(row, text="For").pack(side="left")
        cb = ttk.Combobox(row, textvariable=wv, values=names, state="readonly", width=14)
        cb.pack(side="left", padx=4)
        cb.bind("<<ComboboxSelected>>", lambda ev: (setattr(self, "who", wv.get()),
                                                    self.show()))
        ttk.Label(row, text="(a character's own: when the action is aimed at "
                  "them alone)").pack(side="left")
        grid = ttk.Frame(pf)
        grid.pack(fill="x")
        pools = a["pools"] if self.who == "(general)" else \
            a.get("by_target", {}).get(self.who, {})
        clips = list(P_.d["clips"])
        for k, pk in enumerate(P.POOLS):
            col = ttk.Frame(grid)
            col.grid(row=0, column=k, padx=4, sticky="n")
            ttk.Label(col, text=pk.upper()).pack(anchor="w")
            lb = tk.Listbox(col, height=4, width=14, exportselection=False)
            lb.pack()
            for c in pools.get(pk, []):
                lb.insert("end", c)
            add = tk.StringVar()
            cb2 = ttk.Combobox(col, textvariable=add, values=clips, state="readonly",
                               width=12)
            cb2.pack()
            cb2.bind("<<ComboboxSelected>>",
                     lambda ev, pk=pk, add=add: self._pool(aid, pk, add.get(), True))
            ttk.Button(col, text="Remove", command=lambda pk=pk, lb=lb: self._pool(
                aid, pk, lb.get(lb.curselection()[0]) if lb.curselection() else None,
                False)).pack()

    def _cost(self, aid, kind):
        v = {"none": {}, "MP": {"mp": 5}, "limit": {"limit": True}}.get(kind)
        if kind == "item":
            items = self.app.proj.d.get("items", [])
            if not items:
                return self.app.say("No items yet: add one on Characters & menus")
            v = {"item": items[0]["name"], "n": 1}
        self.app.edit("set", ("actions", self.aid(), "cost"), v)

    def _effect_row(self, parent, aid, i, e):
        tk, ttk = self.app.tk, self.app.ttk
        row = ttk.Frame(parent)
        row.pack(fill="x", pady=1)
        verb = tk.StringVar(value=e["verb"])
        cb = ttk.Combobox(row, textvariable=verb, values=list(EFFECT_FIELDS),
                          state="readonly", width=9)
        cb.pack(side="left")
        cb.bind("<<ComboboxSelected>>", lambda ev: self._set_effect(
            aid, i, effect_from(verb.get(), {k: d for k, _, _, d, _ in
                                             EFFECT_FIELDS[verb.get()]})))
        f = effect_form(e)
        vars_ = {}
        stances = [st["name"] for st in self.app.proj.d["enemy"]["stances"]]
        for key, label, kind, dflt, choices in EFFECT_FIELDS.get(e["verb"], []):
            ttk.Label(row, text=label).pack(side="left", padx=(8, 2))
            v = tk.StringVar(value=str(f.get(key, dflt)))
            vars_[key] = v
            if kind in ("choice", "stance"):
                w = ttk.Combobox(row, textvariable=v, state="readonly", width=9,
                                 values=list(choices) if choices else stances)
                w.bind("<<ComboboxSelected>>", lambda ev: commit())
            else:
                w = ttk.Entry(row, textvariable=v, width=6)
                w.bind("<Return>", lambda ev: commit())
                w.bind("<FocusOut>", lambda ev: commit())
            w.pack(side="left")

        def commit():
            try:
                ne = effect_from(e["verb"], {k: v.get() for k, v in vars_.items()})
            except ValueError:
                return self.app.say("A number, please")
            if ne != e:
                self._set_effect(aid, i, ne)
        ttk.Button(row, text="Remove", command=lambda: self._rm_effect(aid, i)
                   ).pack(side="right")

    def _set_effect(self, aid, i, e):
        effs = list(self.app.proj.d["actions"][aid]["effects"])
        effs[i] = e
        self.app.edit("set", ("actions", aid, "effects"), effs)

    def _add_effect(self, aid):
        effs = list(self.app.proj.d["actions"][aid]["effects"]) + [{"verb": "guard"}]
        self.app.edit("set", ("actions", aid, "effects"), effs)

    def _rm_effect(self, aid, i):
        effs = list(self.app.proj.d["actions"][aid]["effects"])
        del effs[i]
        self.app.edit("set", ("actions", aid, "effects"), effs)

    def _pool(self, aid, pk, clip, add):
        if not clip:
            return
        a = self.app.proj.d["actions"][aid]
        if self.who == "(general)":
            path, pools = ("actions", aid, "pools"), dict(a["pools"])
        else:
            by = dict(a.get("by_target", {}))
            pools = dict(by.get(self.who, {}))
            path = ("actions", aid, "by_target")
        lst = list(pools.get(pk, []))
        if add:
            lst.append(clip)
        elif clip in lst:
            lst.remove(clip)
        pools[pk] = lst
        if self.who == "(general)":
            self.app.edit("set", path, pools)
        else:
            by[self.who] = pools
            self.app.edit("set", path, by)


# --- the Characters & menus tab (11.1, 4.4) ---------------------------------------------

class PartyTab(object):
    """The characters (1 to 4): their stats and their menus - the top menu,
    its sub-menus, the side menu - each menu's look; the band's pictures and
    the party's items; and the band as the engine will draw it"""

    def __init__(self, app, parent):
        tk, ttk = app.tk, app.ttk
        self.app = app
        self.f = f = ttk.Frame(parent, padding=6)
        left = ttk.Frame(f)
        left.pack(side="left", fill="y")
        self.lb = tk.Listbox(left, height=5, width=20, exportselection=False)
        self.lb.pack()
        self.lb.bind("<<ListboxSelect>>", lambda e: self.show())
        bb = ttk.Frame(left)
        bb.pack(fill="x")
        ttk.Button(bb, text="Add", command=self._add).pack(side="left")
        ttk.Button(bb, text="Remove", command=self._remove).pack(side="left")
        self.side = ttk.Frame(left)
        self.side.pack(fill="x", pady=8)
        self.body = ttk.Frame(f)
        self.body.pack(side="left", fill="both", expand=True, padx=8)
        self.mrow = None

    def ci(self):
        s = self.lb.curselection()
        return s[0] if s else (0 if self.app.proj.d["characters"] else None)

    def refresh(self):
        keep = self.lb.curselection()
        self.lb.delete(0, "end")
        for c in self.app.proj.d["characters"]:
            self.lb.insert("end", c["name"])
        if keep and keep[0] < self.lb.size():
            self.lb.selection_set(keep[0])
        self.show()

    def _add(self):
        from tkinter import simpledialog
        name = simpledialog.askstring(TITLE, "The character's name:", parent=self.f)
        if name:
            self.app.edit("add_character", name.strip().upper()[:8])

    def _remove(self):
        if self.ci() is not None:
            self.app.edit("remove_character", self.ci())

    def show(self):
        tk, ttk = self.app.tk, self.app.ttk
        clear(self.body)
        clear(self.side)
        d = self.app.proj.d
        self._extras()
        ci = self.ci()
        if ci is None or ci >= len(d["characters"]):
            ttk.Label(self.body, text="Add a character").pack(anchor="w")
            return
        c = d["characters"][ci]
        top = ttk.Frame(self.body)
        top.pack(fill="x")
        form = Rows(self.app, top)
        form.f.pack(side="left", anchor="n")
        form.entry("Name", c["name"], lambda v: self.app.edit(
            "set", ("characters", ci, "name"), v.upper()[:8]))
        for k in ("hp", "mp", "atk", "mag", "def", "mdef", "evade"):
            form.entry(k.upper(), c.get(k, 0), lambda v, k=k: self.app.edit(
                "set", ("characters", ci, k), as_int(v)))
        mf = ttk.LabelFrame(top, text="Menus: the top one, its sub-menus (>), "
                            "the side one (Left/Right)", padding=4)
        mf.pack(side="left", fill="both", expand=True, padx=8)
        self.tree = tk.Listbox(mf, height=10, width=30, exportselection=False)
        self.tree.grid(row=0, column=0, rowspan=8, sticky="n")
        self.rows = menu_rows(self.app.proj, ci)
        for where, i, sub, text in self.rows:
            ind = "    " if where == "sub" else ""
            tag = "side: " if where == "side" else ""
            self.tree.insert("end", ind + tag + text)
        self.tree.bind("<<ListboxSelect>>", lambda e: self._pick_row())
        acts = list(d["actions"])
        self.add = tk.StringVar()
        ttk.Combobox(mf, textvariable=self.add, values=acts, state="readonly",
                     width=14).grid(row=0, column=1, sticky="w")
        ttk.Button(mf, text="Add to the top", command=lambda: self._menu_add("top")
                   ).grid(row=1, column=1, sticky="w")
        ttk.Button(mf, text="Add to the side", command=lambda: self._menu_add("side")
                   ).grid(row=2, column=1, sticky="w")
        ttk.Button(mf, text="Add to the sub-menu", command=lambda: self._menu_add("sub")
                   ).grid(row=3, column=1, sticky="w")
        ttk.Button(mf, text="New sub-menu...", command=self._new_sub
                   ).grid(row=4, column=1, sticky="w")
        ttk.Button(mf, text="Up", command=lambda: self._move(-1)).grid(row=5, column=1, sticky="w")
        ttk.Button(mf, text="Down", command=lambda: self._move(1)).grid(row=6, column=1, sticky="w")
        ttk.Button(mf, text="Remove", command=self._menu_rm).grid(row=7, column=1, sticky="w")
        # the look of the menu the selected row is in
        where, sub = self._menu_of_row()
        key = look_key(self.app.proj, ci, where, sub)
        lk = d.get("menu_looks", {}).get(key, {})
        lf = ttk.LabelFrame(self.body, text="The look of %s" % key, padding=4)
        lf.pack(fill="x", pady=4)
        lform = Rows(self.app, lf)
        lform.f.pack(side="left")
        imgs = ["(none)"] + [im["name"] for im in d.get("images", [])]
        lform.choice("Picture", lk.get("image") or "(none)", imgs,
                     lambda v: self._look(key, "image", None if v == "(none)" else v))
        lform.entry("Text column (cell)", lk.get("col", 1), lambda v: self._look(key, "col", as_int(v)))
        lform.entry("Text row (in the band)", lk.get("row", 4), lambda v: self._look(key, "row", as_int(v)))
        lform.entry("Row pitch", lk.get("pitch", 9), lambda v: self._look(key, "pitch", as_int(v)))
        lform.choice("Ink", str(lk.get("ink", 3)), ("1", "2", "3"),
                     lambda v: self._look(key, "ink", int(v)))
        lform.choice("Cursor", lk.get("cursor", "inverse"), ("inverse", "arrow"),
                     lambda v: self._look(key, "cursor", v))
        # the band as the engine draws it
        pv = ttk.Frame(lf)
        pv.pack(side="left", padx=8)
        ttk.Label(pv, text="The band, as the engine draws it (the letters' "
                  "shapes are the tree's 8x8 face; the machine's are its "
                  "ROM's):").pack(anchor="w")
        self.band = tk.Label(pv, bg="black")
        self.band.pack(anchor="w")
        self._preview(ci, where, sub)

    def _menu_of_row(self):
        if self.mrow is None or self.mrow >= len(self.rows):
            return "top", None
        where, i, sub, _ = self.rows[self.mrow]
        if where == "sub":
            return "top", sub
        if where == "top" and self.rows[self.mrow][3].endswith(">"):
            return "top", i
        return where, None

    def _pick_row(self):
        s = self.tree.curselection()
        self.mrow = s[0] if s else None
        self.show()
        if self.mrow is not None:
            self.tree.selection_set(self.mrow)

    def _preview(self, ci, where, sub):
        from PIL import ImageTk
        import vbtband
        menu = "side" if where == "side" else None
        if sub is not None:
            menu = self.app.proj.d["characters"][ci]["menu"]["top"][sub]["label"]
        try:
            im = vbtband.render(self.app.proj, ci, menu)
        except Exception as e:
            self.band.config(text="(not until the rules compile: %s)" % e,
                             fg="white", image="")
            return
        im = im.resize((im.width * 2, im.height * 2))
        self._bimg = ImageTk.PhotoImage(im)
        self.band.config(image=self._bimg)

    def _look(self, key, field, value):
        looks = dict(self.app.proj.d.get("menu_looks", {}))
        lk = dict(looks.get(key, {}))
        if value is None:
            lk.pop(field, None)
        else:
            lk[field] = value
        looks[key] = lk
        self.app.edit("set", ("menu_looks",), looks)

    def _menu_add(self, where):
        aid = self.add.get()
        ci = self.ci()
        if not aid or ci is None:
            return self.app.say("Pick an action to add")
        if where == "sub":
            w, sub = self._menu_of_row()
            if sub is None:
                return self.app.say("Pick a sub-menu (or a row in one) first")
            def f(d):
                d["characters"][ci]["menu"]["top"][sub]["sub"].append(aid)
            self.app.proj._edit(f)
            return self.app.refresh()
        self.app.edit("menu_add", ci, action=aid, side=where == "side")

    def _new_sub(self):
        from tkinter import simpledialog
        label = simpledialog.askstring(TITLE, "The sub-menu's label:", parent=self.f)
        if label and self.ci() is not None:
            self.app.edit("menu_add", self.ci(), label=label.strip()[:8], sub=())

    def _move(self, by):
        if self.mrow is None:
            return
        where, i, sub, _ = self.rows[self.mrow]
        self.app.edit("menu_move", self.ci(), "top" if where == "sub" else where,
                      i, by, sub=sub if where == "sub" else None)

    def _menu_rm(self):
        if self.mrow is None:
            return
        where, i, sub, _ = self.rows[self.mrow]
        self.app.edit("menu_remove", self.ci(), "top" if where == "sub" else where,
                      i, sub=sub if where == "sub" else None)
        self.mrow = None

    def _extras(self):
        """The band's pictures and the party's items, under the list"""
        tk, ttk = self.app.tk, self.app.ttk
        d = self.app.proj.d
        pf = ttk.LabelFrame(self.side, text="Band pictures", padding=4)
        pf.pack(fill="x")
        for i, im in enumerate(d.get("images", [])):
            ttk.Label(pf, text="%s at %d,%d" % (im["name"], im.get("col", 0),
                                                im.get("row", 0))).pack(anchor="w")
        ttk.Button(pf, text="Add a picture...", command=self._add_image).pack(anchor="w")
        imgs = ["(none)"] + [im["name"] for im in d.get("images", [])]
        bv = tk.StringVar(value=d.get("band_image") or "(none)")
        ttk.Label(pf, text="Under the whole band:").pack(anchor="w")
        cb = ttk.Combobox(pf, textvariable=bv, values=imgs, state="readonly", width=14)
        cb.pack(anchor="w")
        cb.bind("<<ComboboxSelected>>", lambda e: self.app.edit(
            "set", ("band_image",), None if bv.get() == "(none)" else bv.get()))
        it = ttk.LabelFrame(self.side, text="The party's items", padding=4)
        it.pack(fill="x", pady=4)
        for i, x in enumerate(d.get("items", [])):
            r = Rows(self.app, it)
            r.f.pack(anchor="w")
            r.entry(x["name"], x.get("count", 1), lambda v, i=i: self.app.edit(
                "set", ("items", i, "count"), as_int(v)), width=4)
        ttk.Button(it, text="Add an item...", command=self._add_item).pack(anchor="w")

    def _add_image(self):
        from tkinter import filedialog, simpledialog
        p = filedialog.askopenfilename(title="A band picture")
        if not p:
            return
        name = simpledialog.askstring(TITLE, "Its name:", parent=self.f) or \
            os.path.splitext(os.path.basename(p))[0][:12]
        col = simpledialog.askinteger(TITLE, "Its column (cells, 0-39):", parent=self.f,
                                      initialvalue=0) or 0
        row = simpledialog.askinteger(TITLE, "Its row in the band:", parent=self.f,
                                      initialvalue=0) or 0
        imgs = list(self.app.proj.d.get("images", [])) + [
            {"name": name, "path": p, "col": col, "row": row, "dither": True}]
        self.app.edit("set", ("images",), imgs)

    def _add_item(self):
        from tkinter import simpledialog
        name = simpledialog.askstring(TITLE, "The item's name:", parent=self.f)
        if name:
            items = list(self.app.proj.d.get("items", [])) + [{"name": name[:8], "count": 1}]
            self.app.edit("set", ("items",), items)


# --- the Enemy tab (11.1, 11.2.1, 11.2.2) -------------------------------------------------

class EnemyTab(object):
    """The enemy: its stats, its victory and defeat clips, and its STANCES -
    each with its modifiers, its weighted moves and their target rules, its
    responses, its automatic change and its return"""

    def __init__(self, app, parent):
        tk, ttk = app.tk, app.ttk
        self.app = app
        self.f = f = ttk.Frame(parent, padding=6)
        self.left = ttk.Frame(f)
        self.left.pack(side="left", fill="y")
        self.body = ttk.Frame(f)
        self.body.pack(side="left", fill="both", expand=True, padx=8)
        self.si = 0

    def refresh(self):
        tk, ttk = self.app.tk, self.app.ttk
        clear(self.left)
        clear(self.body)
        P_ = self.app.proj
        e = P_.d["enemy"]
        form = Rows(self.app, self.left)
        form.f.pack(anchor="w")
        form.entry("Name", e["name"], lambda v: self.app.edit(
            "set", ("enemy", "name"), v.upper()[:10]))
        for k in ("hp", "mp", "atk", "mag", "def", "mdef", "evade"):
            form.entry(k.upper(), e.get(k, 0), lambda v, k=k: self.app.edit(
                "set", ("enemy", k), as_int(v)))
        clips = list(P_.d["clips"])
        form.choice("Victory clip", e.get("victory") or "", clips,
                    lambda v: self.app.edit("set", ("enemy", "victory"), v))
        form.choice("Defeat clip", e.get("defeat") or "", clips,
                    lambda v: self.app.edit("set", ("enemy", "defeat"), v))
        names = [s["name"] for s in e["stances"]]
        form.choice("Starts in", e.get("start_stance", names[0]), names,
                    lambda v: self.app.edit("set", ("enemy", "start_stance"), v))
        ttk.Label(self.left, text="Stances").pack(anchor="w", pady=(8, 0))
        self.lb = tk.Listbox(self.left, height=6, width=20, exportselection=False)
        self.lb.pack(anchor="w")
        for s_ in names:
            self.lb.insert("end", s_)
        self.si = min(self.si, len(names) - 1)
        self.lb.selection_set(self.si)
        self.lb.bind("<<ListboxSelect>>", lambda ev: self._pick())
        bb = ttk.Frame(self.left)
        bb.pack(anchor="w")
        ttk.Button(bb, text="New...", command=self._new).pack(side="left")
        ttk.Button(bb, text="Delete", command=lambda: self.app.edit(
            "remove_stance", self.si)).pack(side="left")
        self._stance()

    def _pick(self):
        s = self.lb.curselection()
        if s:
            self.si = s[0]
            clear(self.body)
            self._stance()

    def _new(self):
        from tkinter import simpledialog
        name = simpledialog.askstring(TITLE, "The stance's name:", parent=self.f)
        if name:
            self.app.edit("add_stance", name.strip().lower()[:8])

    def _stance(self):
        tk, ttk = self.app.tk, self.app.ttk
        P_ = self.app.proj
        st = P_.d["enemy"]["stances"][self.si]
        base = ("enemy", "stances", self.si)
        acts = list(P_.d["actions"])
        chars = [c["name"] for c in P_.d["characters"]]
        names = [s["name"] for s in P_.d["enemy"]["stances"]]
        top = ttk.Frame(self.body)
        top.pack(fill="x")
        mf = ttk.LabelFrame(top, text="Modifiers, %", padding=4)
        mf.pack(side="left", anchor="n")
        mods = st.get("modifiers", {})
        r = Rows(self.app, mf)
        r.f.pack()
        for k in ("atk", "mag", "def", "mdef"):
            r.entry(k.upper(), mods.get(k, 0), lambda v, k=k: self.app.edit(
                "set", base + ("modifiers",), dict(mods, **{k: as_int(v)})), width=5)
        af = ttk.LabelFrame(top, text="Its own change, and its return", padding=4)
        af.pack(side="left", anchor="n", padx=8)
        au = st.get("auto") or {}
        r2 = Rows(self.app, af)
        r2.f.pack()
        r2.choice("Changes when", au.get("kind", "none"), ("none", "hp_below", "after_turns"),
                  lambda v: self._auto(base, au, kind=v))
        r2.entry("...below % / after turns", au.get("arg", 0),
                 lambda v: self._auto(base, au, arg=as_int(v)), width=5)
        r2.choice("...to", au.get("to", names[0]), names, lambda v: self._auto(base, au, to=v))
        r2.choice("...by (an action)", au.get("via") or "(none)", ["(none)"] + acts,
                  lambda v: self._auto(base, au, via=None if v == "(none)" else v))
        r2.choice("Returns by", st.get("return") or "(none)", ["(none)"] + acts,
                  lambda v: self.app.edit("set", base + ("return",),
                                          None if v == "(none)" else v))
        mv = ttk.LabelFrame(self.body, text="Moves (up to 6): drawn by weight", padding=4)
        mv.pack(fill="x", pady=4)
        for i, m in enumerate(st["moves"]):
            row = ttk.Frame(mv)
            row.pack(fill="x")
            self._combo(row, m["action"], acts, lambda v, i=i: self._move(base, i, action=v))
            ttk.Label(row, text="weight").pack(side="left", padx=(6, 2))
            w = tk.StringVar(value=str(m.get("weight", 1)))
            en = ttk.Entry(row, textvariable=w, width=4)
            en.pack(side="left")
            en.bind("<Return>", lambda ev, i=i, w=w: self._move(base, i, weight=as_int(w.get())))
            en.bind("<FocusOut>", lambda ev, i=i, w=w: self._move(base, i, weight=as_int(w.get())))
            ttk.Label(row, text="at").pack(side="left", padx=(6, 2))
            self._combo(row, m.get("rule", "alternate"), P.RULES,
                        lambda v, i=i: self._move(base, i, rule=v))
            if m.get("rule") == "named":
                self._combo(row, m.get("target", chars[0] if chars else ""), chars,
                            lambda v, i=i: self._move(base, i, target=v))
            ttk.Button(row, text="Remove", command=lambda i=i: self._rm(base, "moves", i)
                       ).pack(side="right")
        if len(st["moves"]) < 6 and acts:
            ttk.Button(mv, text="Add a move", command=lambda: self.app.edit(
                "set", base + ("moves",), st["moves"] + [
                    {"action": acts[0], "weight": 1, "rule": "alternate"}])).pack(anchor="w")
        rf = ttk.LabelFrame(self.body, text="Responses (up to 2): after a "
                            "character's action, outside its own turn", padding=4)
        rf.pack(fill="x", pady=4)
        for i, x in enumerate(st.get("responses", [])):
            row = ttk.Frame(rf)
            row.pack(fill="x")
            ttk.Label(row, text="after").pack(side="left")
            self._combo(row, x.get("trigger", "any"), ("any", "physical", "magical", "named"),
                        lambda v, i=i: self._resp(base, i, trigger=v))
            if x.get("trigger") == "named":
                self._combo(row, x.get("named", acts[0]), acts,
                            lambda v, i=i: self._resp(base, i, named=v))
            ttk.Label(row, text="chance %").pack(side="left", padx=(6, 2))
            c = tk.StringVar(value=str(x.get("chance", 100)))
            en = ttk.Entry(row, textvariable=c, width=4)
            en.pack(side="left")
            en.bind("<Return>", lambda ev, i=i, c=c: self._resp(base, i, chance=as_int(c.get())))
            en.bind("<FocusOut>", lambda ev, i=i, c=c: self._resp(base, i, chance=as_int(c.get())))
            ttk.Label(row, text="it does").pack(side="left", padx=(6, 2))
            self._combo(row, x["action"], acts, lambda v, i=i: self._resp(base, i, action=v))
            ttk.Button(row, text="Remove", command=lambda i=i: self._rm(base, "responses", i)
                       ).pack(side="right")
        if len(st.get("responses", [])) < 2 and acts:
            ttk.Button(rf, text="Add a response", command=lambda: self.app.edit(
                "set", base + ("responses",), st.get("responses", []) + [
                    {"trigger": "any", "chance": 30, "action": acts[0]}])).pack(anchor="w")

    def _combo(self, parent, value, values, commit):
        tk, ttk = self.app.tk, self.app.ttk
        v = tk.StringVar(value=value)
        cb = ttk.Combobox(parent, textvariable=v, values=list(values),
                          state="readonly", width=12)
        cb.pack(side="left")
        cb.bind("<<ComboboxSelected>>", lambda ev: commit(v.get()))

    def _auto(self, base, au, **kw):
        n = dict(au)
        n.update(kw)
        if n.get("kind", "none") == "none":
            n = {}
        else:
            n.setdefault("arg", 50)
            n.setdefault("to", self.app.proj.d["enemy"]["stances"][0]["name"])
        self.app.edit("set", base + ("auto",), n or None)

    def _move(self, base, i, **kw):
        moves = [dict(m) for m in self.app.proj.get(*base)["moves"]]
        moves[i].update(kw)
        self.app.edit("set", base + ("moves",), moves)

    def _resp(self, base, i, **kw):
        rs = [dict(r) for r in self.app.proj.get(*base).get("responses", [])]
        rs[i].update(kw)
        self.app.edit("set", base + ("responses",), rs)

    def _rm(self, base, key, i):
        lst = list(self.app.proj.get(*base).get(key, []))
        del lst[i]
        self.app.edit("set", base + (key,), lst)


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser(description=TITLE)
    ap.add_argument("project", nargs="?")
    a = ap.parse_args(argv)
    App(a.project).root.mainloop()


if __name__ == "__main__":
    main()
