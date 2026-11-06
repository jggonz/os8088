#!/usr/bin/env python3
"""os88vbtgui - the Video Battle builder's window
(docs/plans/completed/VBATTLE-BUILDER-PLAN.md 2, 4; wave 2: the shell, the Battle tab
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
NO_MUSIC = "(no music)"
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
    words = {"encode": "Reading the video", "decode": "Decoding", "cost": "Costing",
             "analyse": "Listening to", "clips": "Clips:", "check": "Checking"}
    w = words.get(st, str(st).capitalize())
    if of:
        return "%s %s - %d%%" % (w, what or "", 100 * done // max(1, of))
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
    """(name, what) for every encoder profile a battle can target: what the
    machine IS (the encoder window's own words for its "made for" choice),
    not whose it is"""
    import os88venc
    help_ = os88venc.CHOICE_HELP.get("profile", {})
    return [(k, help_.get(k, "")) for k in os88venc.PROFILES]


VIDEO_EXT = (".mp4", ".mkv", ".mov", ".avi", ".webm", ".m4v", ".mpg",
             ".mpeg", ".wmv", ".flv", ".gif", ".ts")
SOUND_EXT = (".wav", ".mp3", ".ogg", ".flac", ".m4a", ".aac", ".opus",
             ".wma", ".aif", ".aiff", ".mod")


def drop_kind(path):
    """What a dropped file is: "project", "video", "sound", "picture" or
    None"""
    e = os.path.splitext(path)[1].lower()
    if e == ".vbp":
        return "project"
    if e in VIDEO_EXT:
        return "video"
    if e in SOUND_EXT:
        return "sound"
    if e in (".png", ".bmp", ".jpg", ".jpeg", ".gif"):
        return "picture"
    return None


def unique_id(base, taken, n=16):
    """BASE cut to N, made unique against TAKEN with a number"""
    b = "".join(ch if ch.isalnum() or ch in "_-" else "_" for ch in base)[:n] or "x"
    if b not in taken:
        return b
    k = 2
    while ("%s%d" % (b[:n - len(str(k))], k)) in taken:
        k += 1
    return "%s%d" % (b[:n - len(str(k))], k)


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


def play_order(loop, whole=True, around=8, gap=8):
    """What a preview plays, round and round: the WHOLE loop, start to
    end-1, as the engine plays it - or only across its SEAM, with GAP
    black frames (None) before it goes round again, so the preview's own
    restart is never mistaken for the seam"""
    if whole:
        return list(range(loop[0], loop[1]))
    return seam_order(loop, around) + [None] * gap


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
    "limit": [("to", "Whose", "choice", "self", ("self", "target")),
              ("percent", "Fills %", "int", 25, None)],
}
# what each verb does, beside its row (SPEC.md 126.6)
VERB_HELP = {
    "damage": "HP off the target(s)",
    "heal": "HP back to the target(s)",
    "restore": "MP back to the target(s)",
    "revive": "a fallen character up again, with a share of HP",
    "guard": "the target(s) take half damage until their next turn",
    "charge": "the target(s)' NEXT damaging action deals double (not the "
              "limit gauge - that is \"limit\")",
    "boost": "a stat of the target(s) up, for some turns",
    "lower": "a stat of the target(s) down, for some turns",
    "pass": "nothing: the turn passes",
    "escape": "the battle ends, escaped",
    "stance": "the enemy into another stance",
    "limit": "a LIMIT GAUGE fuller by a percentage: the doer's (self) or "
             "the characters it hits (target)",
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
        return ["(the rules do not compile yet: %s)" % e]
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
        from os88drop import make_root, enable_drop
        self.root = make_root()         # drag and drop, where installed
        self.root.geometry("1300x760")
        self.pending = []               # videos waiting to become clips
        self.scrolls = {}               # each tab's Scroll, by its canvas
        self.clip_job = self.reading = None     # the job reading one, and
        #                                 the source it added for it
        self.proj = P.Project()
        self.saved = self._state()
        self.job = None
        self.q = queue.Queue()
        self.frames = {}            # source -> decoded screens, once loaded
        self._band = (None, None)   # (the project's state, the band's rgb)
        self.show_band = self.tk.BooleanVar(value=True)
        self.anim = None
        self._menus()
        self._body()
        if path:
            self.open_path(path)
        self.refresh()
        self._fit()
        self.root.protocol("WM_DELETE_WINDOW", self.quit)
        self.root.after(50, self._poll)
        self.drop_how = enable_drop(self.root, self._dropped)
        for ev in ("<MouseWheel>", "<Shift-MouseWheel>", "<Button-4>",
                   "<Button-5>", "<Shift-Button-4>", "<Shift-Button-5>"):
            self.root.bind_all(ev, self._wheel, add="+")
        self.root.bind_all("<FocusIn>", self._focus, add="+")

    # --- the tabs' scrolling (Scroll) -------------------------------------------
    SELF_SCROLLING = ("Listbox", "Treeview", "Text", "TCombobox", "Scale",
                      "TScale", "Spinbox", "TSpinbox")

    def scroll_of(self, w):
        """The Scroll whose content holds widget W, or None"""
        while w is not None:
            sc = self.scrolls.get(str(w))
            if sc is not None:
                return sc
            w = getattr(w, "master", None)
        return None

    def _wheel(self, e):
        """The wheel over a tab: the tab scrolls - not when the widget
        under it scrolls itself (a list, a tree, a text, a slider), which
        it is already doing"""
        w = e.widget
        if not hasattr(w, "winfo_class") or \
                w.winfo_class() in self.SELF_SCROLLING:
            return
        sc = self.scroll_of(w)
        across = bool(e.state & 1)      # Shift
        if sc is None or not sc.scrolls(across):
            return
        if e.num in (4, 5):             # X11
            n = -1 if e.num == 4 else 1
        elif abs(e.delta) >= 120:       # Windows (and Tk 8.7's X11)
            n = -int(e.delta / 120)
        else:                           # the Mac: small steps
            n = -e.delta
        if n:
            (sc.cv.xview_scroll if across else sc.cv.yview_scroll)(
                n * 3 if abs(n) == 1 else n, "units")

    def _focus(self, e):
        """A field reached by the keyboard (Tab) is scrolled into view"""
        w = e.widget
        if hasattr(w, "winfo_class"):
            sc = self.scroll_of(w)
            if sc is not None and w is not sc.inner:
                sc.see(w)

    def _cover(self):
        from tkinter import filedialog
        f = filedialog.askopenfilename(
            title="The title picture", filetypes=[
                ("Pictures", "*.png *.jpg *.jpeg *.bmp *.gif"), ("All", "*")])
        if f:
            cv = dict(self.app.proj.d.get("cover") or {})
            cv["image"] = f
            self.app.edit("set", ("cover",), cv)

    def _cover_line(self):
        cv = self.app.proj.d.get("cover")
        if not cv:
            return
        line = self.cvline.get()
        if line != (cv.get("line") or ""):
            self.app.edit("set", ("cover",), dict(cv, line=line))

    def _fit(self):
        """The window as tall as its tallest tab asks, so nothing is cut off
        below - no taller than the screen less its taskbar's room"""
        self.root.update_idletasks()
        h = min(max(760, self.root.winfo_reqheight()),
                self.root.winfo_screenheight() - 80)
        # as wide as its widest tab AND a tab's scroll bar (Scroll: a tab
        # taller than the window scrolls, and its bar is not taken out of
        # the content's width), no wider than the screen
        w = min(max(1300, self.root.winfo_reqwidth() + 18),
                self.root.winfo_screenwidth())
        self.root.geometry("%dx%d" % (w, h))

    # --- drag and drop ----------------------------------------------------------
    def _dropped(self, paths):
        """ON TK'S THREAD: the files, and the widget under the pointer when
        they landed - queued, and taken in the pump"""
        x, y = self.root.winfo_pointerxy()
        self.q.put(("drop", (paths, self.root.winfo_containing(x, y))))

    def drop(self, paths, where=None):
        """Files dropped on the window: a project anywhere is opened; a
        video always becomes a clip - or, dropped on the Idle tab, the idle
        loop's source, and on the Openings tab the next opening; a sound
        always joins the sounds, and dropped on the music it is the music,
        on a clip's effects a cue at its frame, on the Openings tab the
        chosen opening's sound; a picture joins the band pictures"""
        tab = self.nb.index(self.nb.select())
        inside = lambda w, frame: w is not None and (str(w) == str(frame) or
                                                     str(w).startswith(
                                                         str(frame) + "."))
        for p in paths:
            k = drop_kind(p)
            if k == "project":
                if self._ask_save():
                    self.open_path(p)
                return
            if k == "video":
                if inside(where, self.idle_tab.f) or (where is None and tab == 1):
                    self.idle_tab.take(p)
                elif inside(where, self.openings_tab.f):
                    self.add_clip_file(p, self._as_opening)
                else:
                    self.add_clip_file(p)
                    self.nb.select(2)
            elif k == "sound":
                sid = self.add_sound_file(p)
                if inside(where, self.battle_tab.mf):
                    self.edit("set_music", sid, 0)
                elif inside(where, self.openings_tab.f) and \
                        self.openings_tab.i() is not None:
                    self.edit("set_opening", self.openings_tab.i(), sound=sid)
                elif inside(where, self.clips_tab.f) and self.clips_tab.cid():
                    self.edit("add_cue", self.clips_tab.cid(),
                              int(self.clips_tab.scrub.get()), sid)
            elif k == "picture":
                d = self.proj.d
                name = unique_id(os.path.splitext(os.path.basename(p))[0],
                                 [im["name"] for im in d.get("images", [])], 12)
                self.edit("set", ("images",), list(d.get("images", [])) + [
                    {"name": name, "path": p, "col": 0, "row": 0, "dither": True}])
            else:
                self.say("Not a video, a sound, a picture or a project: %s"
                         % os.path.basename(p))

    def add_video(self, path):
        """VIDEO at PATH as a source (its name from the file's); its id. A
        video already a source is that source (its frames read once)"""
        for sid, src in self.proj.d["sources"].items():
            if src.get("path") and os.path.abspath(self.proj.here(
                    src["path"])) == os.path.abspath(path):
                return sid
        sid = unique_id(os.path.splitext(os.path.basename(path))[0],
                        self.proj.d["sources"])
        self.edit("add_source", sid, path=path)
        return sid

    def add_sound_file(self, path):
        for sid, snd in self.proj.d["sounds"].items():
            if snd.get("path") and os.path.abspath(self.proj.here(
                    snd["path"])) == os.path.abspath(path):
                return sid
        sid = unique_id(os.path.splitext(os.path.basename(path))[0],
                        self.proj.d["sounds"])
        self.edit("add_sound", sid, path=path)
        return sid

    def _as_opening(self, cid):
        """A clip dropped on the Openings tab, once it is one: the next
        opening, and chosen in the list so it is seen to arrive"""
        self.edit("add_opening", cid)
        lb = self.openings_tab.lb
        lb.selection_clear(0, "end")
        lb.selection_set("end")
        lb.see("end")
        self.openings_tab._pick()

    def add_clip_file(self, path, then=None):
        """VIDEO at PATH as a new CLIP, all of it to start: queued, since
        each is read by a job of its own, one after another. THEN(clip id)
        once it is one"""
        self.pending.append((path, then))
        if len(self.pending) == 1:
            self._next_clip()

    def _next_clip(self):
        if not self.pending or self.job:    # (a job's end starts it)
            return
        path, then = self.pending[0]
        known = set(self.proj.d["sources"])
        sid = self.add_video(path)
        cid = unique_id(sid, self.proj.d["clips"])
        self.reading = sid if sid not in known else None

        def got(fr):
            self.reading = None
            self.pending.pop(0)     # (first: the edit's refresh lists what
            #                          is still pending, not this one)
            self.edit("add_clip", cid, sid, (0, len(fr) - 1), anchors=())
            if then:
                then(cid)
            else:
                self.clips_tab.select(cid)
            if self.pending:
                self._next_clip()
        self.screens(sid, got)
        self.clip_job = self.job        # (None: it was read already)

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
        v = tk.Menu(mb, tearoff=0)
        v.add_checkbutton(label="The band in the previews",
                          variable=self.show_band, command=self.refresh)
        mb.add_cascade(label="View", menu=v)
        self.root.config(menu=mb)
        for k, fn in (("<Control-n>", self.new), ("<Control-o>", self.open),
                      ("<Control-s>", self.save), ("<Control-z>", self.undo),
                      ("<Control-y>", self.redo)):
            self.root.bind(k, lambda e, fn=fn: fn())

    def _body(self):
        ttk = self.ttk
        # THE STATUS BAR FIRST: the packer gives space in packing order, so a
        # bar packed after the notebook got what the tabs left - nothing,
        # whenever one asked for more than the window - and a job's progress
        # was never seen (the owner, 2026-11-04)
        bar = ttk.Frame(self.root)
        bar.pack(fill="x", side="bottom")
        ttk.Separator(self.root).pack(fill="x", side="bottom")
        self.nb = ttk.Notebook(self.root)
        self.nb.pack(fill="both", expand=True)
        self.battle_tab = BattleTab(self, self.nb)
        self.idle_tab = IdleTab(self, self.nb)
        self.clips_tab = ClipsTab(self, self.nb)
        self.actions_tab = ActionsTab(self, self.nb)
        self.party_tab = PartyTab(self, self.nb)
        self.enemy_tab = EnemyTab(self, self.nb)
        self.openings_tab = OpeningsTab(self, self.nb)
        self.loops_tab = LoopsTab(self, self.nb)
        self.build_tab = BuildTab(self, self.nb)
        self.sounds_tab = SoundsTab(self, self.nb)
        self.nb.add(self.battle_tab.f, text="Battle")
        self.nb.add(self.idle_tab.f, text="Idle")
        self.nb.add(self.clips_tab.f, text="Clips")
        self.nb.add(self.openings_tab.f, text="Openings")
        self.nb.add(self.loops_tab.f, text="Title & endings")
        self.nb.add(self.actions_tab.f, text="Actions")
        self.nb.add(self.party_tab.f, text="Characters & menus")
        self.nb.add(self.enemy_tab.f, text="Enemy")
        self.nb.add(self.sounds_tab.f, text="Sounds")
        self.nb.add(self.build_tab.f, text="Build")
        self.status = ttk.Label(bar, text="Ready")
        self.status.pack(side="left", padx=6)
        self.cancel_b = ttk.Button(bar, text="Cancel", command=self.cancel,
                                   state="disabled")
        self.cancel_b.pack(side="right", padx=4, pady=2)
        self.pbar = ttk.Progressbar(bar, length=220, mode="determinate")
        self.pbar.pack(side="right", padx=4)

    def refresh(self):
        self._title()
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
        self.openings_tab.refresh()
        self.loops_tab.refresh()
        self.build_tab.refresh()
        self.sounds_tab.refresh()

    def edit(self, fn, *a, **kw):
        """Every edit: the model's method, then the view redrawn"""
        try:
            getattr(self.proj, fn)(*a, **kw)
        except (ValueError, KeyError) as e:
            self.say("Not done: %s" % e)
        self.refresh()

    def say(self, text):
        self.status.config(text=text)
        self._title()

    def _title(self):
        """The window's title, and while a job runs ITS PROGRESS ahead of it
        - seen on the taskbar, and wherever the window is"""
        t = title_of(self.proj, self.dirty())
        if self.job is not None:
            t = "[%s] %s" % (self.status.cget("text").rstrip("."), t)
        self.root.title(t)

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

    def _drop_jobs(self):
        """Another project taken up: a job of the last one is cancelled
        (its end would land in this one - a clip read for a project no
        longer open became a clip here, its source missing) and the videos
        waiting are not taken"""
        self.pending = []
        self.cancel()

    def new(self):
        if self._ask_save():
            self._drop_jobs()
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
        except (OSError, ValueError, KeyError, TypeError,
                AttributeError) as e:      # (not a project: said, not raised)
            self.say("Could not open %s: %s" % (p, e))
            return
        self._drop_jobs()
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
        """TOOL with ARGV as a process; DONE(result) in the window's thread.
        THE PROJECT'S PATH IN ARGV IS TAKEN AFTER THE SAVE: every caller
        builds ARGV from `proj.path` before this asks for a save, and a
        project never saved has none yet - so an argument that is None (or
        the path the project had before a Save As) is the path it has now.
        It was passed as None, Popen raised inside the event pump, and the
        window answered nothing again: the first clip of a new project"""
        if self.job:
            self.say("Busy: Cancel first")
            return
        was = self.proj.path
        if not self.need_saved():
            self.say("Not saved, so nothing was done")
            self.pending = []           # (a clip waiting on this is dropped,
            return                      # not left to block the next one)
        argv = [self.proj.path if a is None or (was and a == was) else a
                for a in argv]
        try:
            self.job = Job(tool, argv).start()
        except (OSError, TypeError, ValueError) as e:
            self.job = None
            self.pending = []
            self.say("Could not start %s: %s" % (tool, e))
            return
        self.cancel_b.config(state="normal")
        self.pbar.config(mode="indeterminate", value=0, maximum=1)
        self.pbar.start(40)            # (moving at once: a job has begun)
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
        """The event pump, every 50 ms. IT MUST NEVER DIE: an exception out
        of a Tk callback is printed where nobody sees it (pythonw on
        Windows has no console) and the callback is not scheduled again,
        so every drop and every job's end after it went unanswered"""
        try:
            self._poll1()
        except Exception as e:          # noqa: BLE001
            import traceback
            traceback.print_exc()
            self.pending = []
            if self.job is None:
                self.pbar.stop()
                self.pbar.config(mode="determinate", value=0)
            self.say("Error: %s (%s) - see the console" % (
                e, type(e).__name__))
        finally:
            self.root.after(50, self._poll)

    def _poll1(self):
        try:
            while True:
                kind, ev = self.q.get_nowait()
                if kind == "drop":
                    self.drop(*ev)
                    continue
                if kind == "prog":
                    self.say(progress_text(ev))
                    if ev.get("of"):
                        if str(self.pbar.cget("mode")) != "determinate":
                            self.pbar.stop()
                            self.pbar.config(mode="determinate")
                        self.pbar.config(maximum=ev["of"], value=ev["done"])
                    elif str(self.pbar.cget("mode")) != "indeterminate":
                        self.pbar.config(mode="indeterminate")
                        self.pbar.start(40)
                elif kind == "exit":
                    job, rc, log = ev
                    self.job = None
                    self.cancel_b.config(state="disabled")
                    self.pbar.stop()
                    self.pbar.config(mode="determinate", value=0)
                    if job.killed or rc != 0:
                        why = log[-1] if log else rc
                        if str(why).startswith("os88venc: "):   # (the
                            why = str(why)[10:]     # encoder's own name)
                        mine = self.pending and job is self.clip_job
                        if mine and not job.killed and \
                                "can be read" not in str(why):  # (which
                            why = "%s is not a video that can be read " \
                                "(%s)" % (os.path.basename(   # file)
                                    self.pending[0][0]), why)
                        self.say("Cancelled" if job.killed else
                                 "Failed: %s" % why)
                        if mine:                # the videos still to come
                            self.pending = []   # are not taken either (a
                            self._forget_unread()   # video dropped while
                        #                      another job ran still is)
                    elif job.result is not None:
                        self.say("Done")
                        self._done(job.result)
                    if self.pending and not self.job:   # a clip waiting on
                        self._next_clip()               # another job
        except queue.Empty:
            pass

    def _forget_unread(self):
        """A video that could not be read (not a video, empty, cancelled)
        leaves no SOURCE behind: it was added only to become a clip, and
        a source whose file will not decode stopped every later build
        ("sources.X: ..."). One something else came to use is kept"""
        sid, self.reading = getattr(self, "reading", None), None
        d = self.proj.d
        if not sid or sid not in d["sources"]:
            return
        if any(c["source"] == sid for c in d["clips"].values()) or \
                (d.get("idle") or {}).get("source") == sid or \
                any(s.get("source") == sid for s in d["sounds"].values()):
            return
        self.edit("set", ("sources",), {k: v for k, v in
                                        d["sources"].items() if k != sid})

    def cancel(self):
        if self.job:
            self.job.cancel()

    def screens(self, sid, then, full=False):
        """Source SID's decoded screens, encoding it first if it is not in
        the cache yet (a job), then THEN(frames). FULL: over the WHOLE
        screen, as a full-screen opening is built - its own encode, kept
        apart (self.frames[(sid, "full")]) from the canvas's"""
        import vbtana as A
        k = (sid, "full") if full else sid
        if k in self.frames:
            return then(self.frames[k])
        s = self.proj.d["sources"][sid]
        if s["kind"] == "scene" or self._cached(sid, full):
            self.frames[k] = A.screens(self.proj, sid, full=full)
            return then(self.frames[k])

        def got(res):
            self.frames[k] = A.screens(self.proj, sid, full=full)
            then(self.frames[k])
        self.run("vbtana", ["screens", self.proj.path, sid] +
                 (["--full"] if full else []), got,
                 "Reading %s%s (the first look encodes it; after that it is "
                 "kept)..." % (sid, " full screen" if full else ""))

    def _cached(self, sid, full=False):
        import vbtana as A
        if not self.proj.path:
            return False
        return os.path.exists(A.screens_npy(self.proj, sid, full))

    def photo(self, px, pal=None, scale=SCALE, band=False):
        """A frame as a picture. BAND: a canvas frame with THE BAND under it,
        as the engine draws it (vbtband.render: the first character's top
        menu) - the screen the battle will show, when View has it on"""
        from PIL import Image, ImageTk
        import numpy as np
        pal = pal if pal is not None else self.proj.d["target"]["palette"]
        b = self.band_rgb() if band and self.show_band.get() else None
        if b is None or px.shape[0] + b.shape[0] != 200:
            return ImageTk.PhotoImage(frame_image(px, pal, scale))
        import vbtana as A
        im = Image.fromarray(np.vstack([A.to_rgb(px, pal), b]))
        return ImageTk.PhotoImage(im.resize((im.width * scale,
                                             im.height * scale), Image.NEAREST))

    def band_rgb(self):
        """The band's rows (BY..199) as rgb, as the engine draws them - kept
        until the project changes; None while the rules do not compile (no
        character yet, say)"""
        key = self._state()
        if self._band[0] != key:
            import numpy as np
            import vbtband
            try:
                self._band = (key, np.asarray(
                    vbtband.render(self.proj, 0).convert("RGB")))
            except Exception:           # noqa: BLE001
                self._band = (key, None)
        return self._band[1]

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
        self.scroll = Scroll(app, parent, 8)    # (the page scrolls)
        self.f, f = self.scroll.outer, self.scroll.inner
        self.loops = []
        r = 0
        ttk.Label(f, text="Name").grid(row=r, column=0, sticky="w")
        self.name = tk.StringVar()
        import os88vbt as B_
        self.tmax = B_.TITLE_MAX        # (what a window's title bar shows)
        vcmd = (f.register(lambda v: len(v) <= self.tmax), "%P")
        e = ttk.Entry(f, textvariable=self.name, width=40, validate="key",
                      validatecommand=vcmd)
        e.grid(row=r, column=1, columnspan=3, sticky="w")
        self.name_l = ttk.Label(f, text="")
        self.name_l.grid(row=r, column=4, sticky="w", padx=6)
        self.name.trace_add("write", lambda *a: self.name_l.config(
            text="%d of %d letters - the window's title (with a title "
                 "picture), all of it shown" % (len(self.name.get()),
                                                self.tmax)))
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
        # the renditions: the same battle for a VGA and a Hercules as well
        rf = ttk.LabelFrame(f, text="Also made for (each its own bundle, "
                            "BATTLEV.VBT and BATTLEH.VBT; the engine takes "
                            "its adapter's)", padding=6)
        rf.grid(row=r, column=0, columnspan=5, sticky="we", pady=6)
        self.rvga, self.rherc = tk.IntVar(), tk.IntVar()
        ttk.Checkbutton(rf, text="VGA (mode 0Dh), colours:", variable=self.rvga,
                        command=self._rends).grid(row=0, column=0, sticky="w")
        self.rcol = tk.StringVar()
        cb = ttk.Combobox(rf, textvariable=self.rcol, state="readonly",
                          values=["4", "8", "16"], width=4)
        cb.grid(row=0, column=1, sticky="w")
        cb.bind("<<ComboboxSelected>>", lambda e: self._rends())
        ttk.Checkbutton(rf, text="Hercules (720 x 348)", variable=self.rherc,
                        command=self._rends).grid(row=0, column=2, sticky="w",
                                                  padx=12)
        self.rnote = ttk.Label(rf, text="", wraplength=900)
        self.rnote.grid(row=1, column=0, columnspan=4, sticky="w")
        # VGA's WRITES IN BANDS (126.3.2): a pixel's planes are separate
        # stores, and between them it is a colour of neither frame - the
        # flashes. Bands cut how long; each costs bytes and OUTs
        vb = ttk.Frame(rf)      # (rows 2..5 are the sizes, the fit and the
        vb.grid(row=6, column=0, columnspan=4, sticky="w", pady=(4, 0))
        ttk.Label(vb, text="VGA writes:").pack(side="left")
        self.rband = tk.StringVar()
        cbb = ttk.Combobox(vb, textvariable=self.rband, state="readonly",
                           values=[l for _, l in VGA_BANDS], width=24)
        cbb.pack(side="left", padx=4)
        cbb.bind("<<ComboboxSelected>>", lambda e: self._rends())
        self.rbnote = ttk.Label(vb, text="", wraplength=640)
        self.rbnote.pack(side="left", padx=6)
        # THE BAND'S COLOURS on VGA (126.3.2): the nearest four of the
        # footage's by default; kept as CGA's own only when asked, at the
        # cost of four of the VGA's colours
        self.rbcga = tk.IntVar()
        ttk.Checkbutton(rf, text="Keep the band's CGA colours on VGA (they "
                        "take four of its colours; otherwise the band uses "
                        "the nearest four of the footage's)",
                        variable=self.rbcga, command=self._rends).grid(
                            row=7, column=0, columnspan=4, sticky="w")
        # EACH CLIP'S OWN COLOURS on VGA (126.3.2): the last of the K,
        # swapped into the DAC as a clip starts - the rest are shared
        vo = ttk.Frame(rf)      # (title picture, below: these after them)
        vo.grid(row=8, column=0, columnspan=4, sticky="w", pady=(4, 0))
        ttk.Label(vo, text="Each clip's own VGA colours:").pack(side="left")
        self.rown = tk.StringVar()
        cbo = ttk.Combobox(vo, textvariable=self.rown, state="readonly",
                           values=["auto", "0", "1", "2", "3", "4"], width=6)
        cbo.pack(side="left", padx=4)
        cbo.bind("<<ComboboxSelected>>", lambda e: self._rends())
        self.rownote = ttk.Label(vo, text="", wraplength=640)
        self.rownote.pack(side="left", padx=6)
        # THE PICTURE'S SIZE, each rendition's: a smaller picture centred in
        # black is fewer bytes a frame - the black never changes - so a
        # heavy adapter (Hercules' 640 x 232) can be set down
        sz = ttk.Frame(rf)
        sz.grid(row=2, column=0, columnspan=4, sticky="w", pady=(4, 0))
        ttk.Label(sz, text="Picture size, of the canvas:").pack(side="left")
        self.scv = {}
        import vbtana as A_
        for kind, lab in (("cga4", "CGA"), ("vga4", "VGA"),
                          ("herc", "Hercules")):
            ttk.Label(sz, text=lab).pack(side="left", padx=(10, 2))
            v = self.scv[kind] = tk.StringVar()
            cb = ttk.Combobox(sz, textvariable=v, state="readonly", width=5,
                              values=["%d%%" % x for x in A_.SCALES])
            cb.pack(side="left")
            cb.bind("<<ComboboxSelected>>", lambda e: self._scale())
        self.scnote = ttk.Label(rf, text="", wraplength=900)
        self.scnote.grid(row=3, column=0, columnspan=4, sticky="w")
        # HOW A VIDEO FITS the canvas, and how near black (or white) is
        # drawn solid: every rendition's, every source's
        ff = ttk.Frame(rf)
        ff.grid(row=4, column=0, columnspan=4, sticky="w", pady=(4, 0))
        ttk.Label(ff, text="Fit the video:").pack(side="left")
        self.fitv = tk.StringVar()
        cb = ttk.Combobox(ff, textvariable=self.fitv, state="readonly",
                          width=34, values=list(FIT_LABELS.values()))
        cb.pack(side="left", padx=(4, 16))
        cb.bind("<<ComboboxSelected>>", lambda e: self._fit())
        ttk.Label(ff, text="Clip to black and white within").pack(side="left")
        self.clipv = tk.StringVar()
        cb = ttk.Combobox(ff, textvariable=self.clipv, state="readonly",
                          width=4, values=["0", "8", "16", "24", "32", "48",
                                           "64"])
        cb.pack(side="left", padx=4)
        cb.bind("<<ComboboxSelected>>", lambda e: self._fit())
        ttk.Label(ff, text="levels (a fade to black ends black; 16 the "
                  "default)").pack(side="left")
        # THE TITLE PICTURE: the battle's window on the desktop, before the
        # play - the picture, and one line under it
        cf = ttk.Frame(rf)
        cf.grid(row=5, column=0, columnspan=4, sticky="w", pady=(4, 0))
        ttk.Label(cf, text="Title picture (the window, before the play):"
                  ).pack(side="left")
        self.cvl = ttk.Label(cf, text="", width=28)
        self.cvl.pack(side="left", padx=4)
        ttk.Button(cf, text="Choose...", command=self._cover).pack(side="left")
        ttk.Button(cf, text="None", command=lambda: self.app.edit(
            "set", ("cover",), None)).pack(side="left", padx=(2, 8))
        ttk.Label(cf, text="Its line:").pack(side="left")
        self.cvline = tk.StringVar()
        en = ttk.Entry(cf, textvariable=self.cvline, width=34)
        en.pack(side="left", padx=4)
        en.bind("<Return>", lambda e: self._cover_line())
        en.bind("<FocusOut>", lambda e: self._cover_line())
        r += 1
        # how it ends: the battle's own last clips, whoever the enemy is
        ef = ttk.LabelFrame(f, text="How it ends (each clip is played to its "
                            "end and held)", padding=6)
        ef.grid(row=r, column=0, columnspan=5, sticky="we", pady=6)
        self.endv = {}
        for i, (k, label) in enumerate((("victory", "Victory clip"),
                                        ("defeat", "Defeat clip"))):
            ttk.Label(ef, text=label).grid(row=0, column=2 * i, sticky="w",
                                           padx=(0 if i == 0 else 16, 4))
            v = self.endv[k] = tk.StringVar()
            cb = ttk.Combobox(ef, textvariable=v, state="readonly", width=18)
            cb.grid(row=0, column=2 * i + 1, sticky="w")
            cb.bind("<<ComboboxSelected>>", lambda ev, k=k: self._end(k))
            self.endv[k + "_cb"] = cb
        ttk.Label(ef, text="An escape ends it on the escape action's own "
                  "clip.").grid(row=1, column=0, columnspan=4, sticky="w")
        r += 1
        # the music
        self.mf = mf = ttk.LabelFrame(f, text="Music (it always loops)", padding=6)
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
        ttk.Button(mf, text="Remove this sound", command=self._rm_sound
                   ).grid(row=0, column=4, padx=4, sticky="w")
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
        rs = t.get("renditions", ["cga4"])
        self.rvga.set(1 if "vga4" in rs else 0)
        self.rherc.set(1 if "herc" in rs else 0)
        self.rcol.set(str(t.get("vga_colours", 8)))
        self.rnote.config(text=REND_NOTE.get(int(self.rcol.get()), ""))
        self.rbcga.set(1 if t.get("vga_band_cga") else 0)
        import vbtana as A_o
        own = t.get("vga_clip_colours")
        self.rown.set("auto" if own is None else str(own))
        self.rownote.config(text=OWN_NOTE % (A_o.vga_slots(self.app.proj),
                                             int(self.rcol.get())))
        vbn = t.get("vga_bands", VGA_BANDS_DEFAULT)
        self.rband.set(dict(VGA_BANDS).get(vbn, str(vbn)))
        self.rbnote.config(text=VGA_BAND_NOTE.get(vbn, ""))
        sc = t.get("scale") or {}
        bits = []
        for kind, (w, h) in (("cga4", (320, rows)), ("vga4", (320, rows)),
                             ("herc", (640, 348 - 2 * (200 - rows)))):
            pct = int(sc.get(kind, 100))
            self.scv[kind].set("%d%%" % pct)
            if pct < 100:
                import vbtana as A
                pw, ph, _, _ = A.boxed(w, h, pct)
                bits.append("%s %d x %d of %d x %d (about %d%% of the bytes)"
                            % ({"cga4": "CGA", "vga4": "VGA",
                                "herc": "Hercules"}[kind], pw, ph, w, h,
                               round(100.0 * pw * ph / (w * h))))
        import vbtana as A__
        self.fitv.set(FIT_LABELS[A__.fit_of(self.app.proj)])
        cv = d.get("cover") or {}
        self.cvl.config(text=os.path.basename(cv.get("image") or "") or
                        "(none)")
        import os88vbt as B__
        self.cvline.set(cv.get("line") or B__.COVER_LINE)
        self.clipv.set(str(A__.clip_of(self.app.proj)))
        self.scnote.config(text=("The picture, centred in black: " +
                                 "; ".join(bits) + ". The band is the same.")
                           if bits else "")
        for k in ("victory", "defeat"):
            self.endv[k + "_cb"].config(values=list(d["clips"]))
            self.endv[k].set(d["enemy"].get(k) or "")
        snd = [NO_MUSIC] + [s for s in d["sounds"]]
        self.mcb.config(values=snd)
        m = d.get("music") or {}
        self.msound.set(m.get("sound", "") if m else NO_MUSIC)
        rate = t["rate"]
        lp = m.get("loop")
        self.lstart.set("%.3f" % secs(lp[0], rate) if lp else "")
        self.lend.set("%.3f" % secs(lp[1], rate) if lp else "")
        self.join.set("%.3f" % secs(m.get("join", 0), rate) if m else "")

    def _rends(self):
        rs = ["cga4"] + (["vga4"] if self.rvga.get() else []) + \
            (["herc"] if self.rherc.get() else [])
        t = self.app.proj.d["target"]
        if rs != t.get("renditions", ["cga4"]):
            self.app.edit("set", ("target", "renditions"), rs)
        n = int(self.rcol.get() or 8)
        if n != t.get("vga_colours", 8):
            self.app.edit("set", ("target", "vga_colours"), n)
        if bool(self.rbcga.get()) != bool(t.get("vga_band_cga")):
            self.app.edit("set", ("target", "vga_band_cga"),
                          bool(self.rbcga.get()))
        ov = self.rown.get()
        ov = None if ov in ("", "auto") else int(ov)
        if ov != t.get("vga_clip_colours"):
            self.app.edit("set", ("target", "vga_clip_colours"), ov)
        lab = self.rband.get()
        for v, l in VGA_BANDS:
            if l == lab and v != t.get("vga_bands", VGA_BANDS_DEFAULT):
                self.app.edit("set", ("target", "vga_bands"), v)

    def _scale(self):
        t = self.app.proj.d["target"]
        sc = dict(t.get("scale") or {})
        for kind, v in self.scv.items():
            pct = int(v.get().rstrip("%") or 100)
            if pct == 100:
                sc.pop(kind, None)
            else:
                sc[kind] = pct
        if sc != (t.get("scale") or {}):
            self.app.frames.clear()     # (another size: other screens)
            self.app.edit("set", ("target", "scale"), sc or None)

    def _cover(self):
        from tkinter import filedialog
        f = filedialog.askopenfilename(
            title="The title picture", filetypes=[
                ("Pictures", "*.png *.jpg *.jpeg *.bmp *.gif"), ("All", "*")])
        if f:
            cv = dict(self.app.proj.d.get("cover") or {})
            cv["image"] = f
            self.app.edit("set", ("cover",), cv)

    def _cover_line(self):
        cv = self.app.proj.d.get("cover")
        if not cv:
            return
        line = self.cvline.get()
        if line != (cv.get("line") or ""):
            self.app.edit("set", ("cover",), dict(cv, line=line))

    def _fit(self):
        t = self.app.proj.d["target"]
        fit = [k for k, v in FIT_LABELS.items() if v == self.fitv.get()]
        fit = fit[0] if fit else "fill"
        clip = int(self.clipv.get() or 16)
        changed = False
        if fit != (t.get("fit") or "fill"):
            self.app.edit("set", ("target", "fit"),
                          None if fit == "fill" else fit)
            changed = True
        if clip != (16 if t.get("clip") is None else t["clip"]):
            self.app.edit("set", ("target", "clip"),
                          None if clip == 16 else clip)
            changed = True
        if changed:
            self.app.frames.clear()     # (other screens: encoded again)

    def _end(self, k):
        v = self.endv[k].get() or None
        if self.app.proj.d["enemy"].get(k) != v:
            self.app.edit("set", ("enemy", k), v)

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
        if self.msound.get() == NO_MUSIC:
            if self.app.proj.d.get("music"):
                self.app.edit("set", ("music",), None)
            return
        self.app.edit("set_music", self.msound.get(), 0)

    def _rm_sound(self):
        """The sound in the Sound box out of the project - the music's own
        too, which leaves the battle with none; one anything else uses is
        refused, saying what"""
        from tkinter import messagebox
        sid = self.msound.get()
        if not sid or sid == NO_MUSIC:
            return self.app.say("Choose the sound to remove in the Sound box")
        users = self.app.proj.sound_users(sid)
        if not messagebox.askyesno(TITLE, "Remove the sound %s%s?" % (
                sid, " - the battle then has no music" if "the music" in users
                else "")):
            return
        self.app.edit("remove_sound", sid)

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
        self.scroll = Scroll(app, parent, 8)    # (the page scrolls)
        self.f, f = self.scroll.outer, self.scroll.inner
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
        # (a blank image FIRST: a tk.Label with none measures width and
        # height in characters and lines, and 284 lines asked the window for
        # 5,000 px - which the notebook took, and the status bar with its
        # progress below it was left one pixel high on every tab)
        self._blank = tk.PhotoImage(width=640, height=284)
        self.pic = tk.Label(mid, bg="black", image=self._blank)
        self.pic.pack(side="left", anchor="n")
        side = ttk.Frame(mid)
        side.pack(side="left", fill="y", padx=8)
        ttk.Label(side, text="Loop: frames").grid(row=0, column=0, sticky="w")
        self.la, self.lb_ = tk.IntVar(), tk.IntVar()
        self.mloop = None           # the model's loop the boxes last showed
        for col, var in ((1, self.la), (3, self.lb_)):
            sp = ttk.Spinbox(side, from_=0, to=9999, textvariable=var, width=6,
                             command=self._pending)
            sp.grid(row=0, column=col)
            sp.bind("<Return>", lambda e: self._pending())
            sp.bind("<FocusOut>", lambda e: self._pending())
        ttk.Label(side, text="to (not incl.)").grid(row=0, column=2)
        ttk.Button(side, text="Set", command=self._set).grid(row=0, column=4)
        ttk.Button(side, text="Recommend", command=self._recommend
                   ).grid(row=1, column=0, pady=6, sticky="w")
        ttk.Button(side, text="Use", command=self._use).grid(row=1, column=1)
        self.cl = tk.Listbox(side, height=6, width=46, exportselection=False)
        self.cl.grid(row=2, column=0, columnspan=5, sticky="we")
        self.cl.bind("<<ListboxSelect>>", lambda e: self._cand())
        pb = ttk.Frame(side)
        pb.grid(row=3, column=0, columnspan=2, pady=6, sticky="w")
        self.play_b = ttk.Button(pb, text="Play the loop",
                                 command=lambda: self._play(True))
        self.play_b.pack(side="left")
        self.seam_b = ttk.Button(pb, text="Across the seam",
                                 command=lambda: self._play(False))
        self.seam_b.pack(side="left")
        self.whole = True
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
            if self.mloop != tuple(i["loop"]):  # (a choice not yet Set stays)
                self.mloop = tuple(i["loop"])
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
        p = filedialog.askopenfilename(title="The idle loop's video")
        if p:
            self.take(p)

    def take(self, path):
        """VIDEO at PATH as the idle loop's source: added, read, and its
        loop recommended - all in one, since that is what it was added for
        (a drop onto this tab comes here too)"""
        sid = self.app.add_video(path)
        self.src.set(sid)
        self._pick(recommend=True)

    def _pick(self, recommend=False):
        sid = self.src.get()
        i = self.app.proj.d.get("idle")
        new = not i or i["source"] != sid
        self._load(new, recommend or new)

    def _load(self, new=False, recommend=False):
        sid = self.src.get()
        if not sid:
            return

        def got(fr):
            self.scrub.config(to=len(fr) - 1)
            if new:                 # a new source: its whole length, until
                self.app.edit("set_idle", sid, (0, len(fr)))   # the loop is
            self._show(self.app.proj.d["idle"]["loop"][0])     # chosen
            self.refresh()
            if recommend:
                self._recommend()
        self.app.screens(sid, got)

    def _show(self, n):
        fr = self._frames()
        if fr is None or not 0 <= n < len(fr):
            return
        self._img = self.app.photo(fr[n], band=True)
        self.pic.config(image=self._img, width=self._img.width(),
                        height=self._img.height())

    def _set(self):
        self.app.edit("set_idle", self.src.get(), self.preview_loop())

    def preview_loop(self):
        """The loop the previews show: the one being CHOSEN - a candidate
        clicked, or the boxes typed in - not yet the one Set"""
        try:
            a, b = int(self.la.get()), int(self.lb_.get())
        except (ValueError, self.app.tk.TclError):
            return tuple(self.app.proj.d["idle"]["loop"])
        return (a, b) if b - a >= 2 else tuple(self.app.proj.d["idle"]["loop"])

    def _cand(self):
        sel = self.cl.curselection()
        if sel and self.cands:
            c = self.cands[sel[0]]
            self.la.set(c["start"])
            self.lb_.set(c["end"])
            self._pending()

    def _pending(self):
        """The choice changed: the previews follow it"""
        if self.app.anim:
            self._play(self.whole)  # (stopped...)
            self._play(self.whole)  # (...and played again, the new loop)
        elif self.diff.get():
            self._show_seam()
        else:
            self._show(self.preview_loop()[0])
        a, b = self.preview_loop()
        self.info.config(text="Showing frames %d to %d (%d frames, %.2f s)%s" % (
            a, b - 1, b - a, (b - a) / self.app.proj.d["target"]["fps"],
            "" if (a, b) == tuple(self.app.proj.d["idle"]["loop"])
            else " - not Set yet"))

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
            if self.cands:              # the best one highlighted IS the
                self.cl.selection_set(0)    # one previewed (not Set yet):
                self._cand()                # selection_set fires no event
        self.app.run("vbtana", ["loops", self.app.proj.path, sid], got,
                     "Looking for the loop...")

    def _use(self):
        sel = self.cl.curselection()
        if sel and self.cands:
            c = self.cands[sel[0]]
            self.app.edit("set_idle", self.src.get(), (c["start"], c["end"]))
            self._show(c["start"])

    def _play(self, whole=True):
        """Play the loop being chosen, the whole of it or across its seam;
        pressed again (either), Stop"""
        fr = self._frames()
        if fr is None:
            return self.app.say("Load the frames first")
        if self.app.anim:
            self.app.root.after_cancel(self.app.anim)
            self.app.anim = None
            self.play_b.config(text="Play the loop")
            self.seam_b.config(text="Across the seam")
            return
        import numpy as np
        self.whole = whole
        black = self.app.photo(np.zeros_like(fr[0]), pal=0)
        imgs = [black if n is None else self.app.photo(fr[n], band=True)
                for n in play_order(self.preview_loop(), whole)]
        (self.play_b if whole else self.seam_b).config(text="Stop")
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
        a, b = self.preview_loop()
        if not self.diff.get():
            return self._show(a)
        rgb = A.diff_rgb(fr[b - 1], fr[a], self.app.proj.d["target"]["palette"])
        im = Image.fromarray(rgb)
        im = im.resize((im.width * SCALE, im.height * SCALE), Image.NEAREST)
        self._img = ImageTk.PhotoImage(im)
        self.pic.config(image=self._img)
        self.app.say("The seam: frame %d onto frame %d, %.2f%% of pixels marked"
                     % (b - 1, a, A.diff(fr[b - 1], fr[a]) * 100))

    def _cost(self):
        def got(res):
            self.cost.config(text="Held in memory: %d records, %.1f KB; the "
                             "seam's record %d bytes; the largest %d bytes"
                             % (res["frames"], res["total"] / 1024.0,
                                res["seam"], res["largest"]))
        a, b = self.preview_loop()
        self.app.run("vbtana", ["cost", self.app.proj.path, "--loop", str(a),
                                str(b)], got, "Costing the loop...")


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
        self.scroll = Scroll(app, parent, 6)    # (the page scrolls)
        self.f, f = self.scroll.outer, self.scroll.inner
        self.rep = None             # the clip_report of the clip shown
        self.sel = 0                # the idle frame selected on the strip
        self.thumbs = {}
        self.drag = None
        self.view = ("clip", 0)     # what the picture shows: what was clicked
        left = ttk.Frame(f)
        left.grid(row=0, column=0, sticky="ns")
        self.lb = ttk.Treeview(left, columns=("joins", "exit"), height=12,
                               selectmode="browse")
        self.lb.heading("#0", text="Clip")
        self.lb.heading("joins", text="Joins at")
        self.lb.heading("exit", text="Exits to")
        self.lb.column("#0", width=124)
        self.lb.column("joins", width=96)    # (the tab in 1366 px, with its bar)
        self.lb.column("exit", width=60)
        self.lb.pack(fill="y")
        self.picked = None          # the clip _pick last took up
        self.lb.bind("<<TreeviewSelect>>", lambda e: self._pick(False))
        bb = ttk.Frame(left)
        bb.pack(fill="x", pady=4)
        ttk.Button(bb, text="Add videos...", command=self._add_videos).pack(side="left")
        ttk.Button(bb, text="From a source...", command=self._new).pack(side="left")
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
        for col, var in ((1, self.cin), (3, self.cout)):
            sp = ttk.Spinbox(rf, from_=0, to=99999, width=6, textvariable=var,
                             command=lambda var=var: self._look_src(var))
            sp.grid(row=0, column=col)
            sp.bind("<Return>", lambda e, var=var: self._look_src(var))
        ttk.Label(rf, text="Out").grid(row=0, column=2)
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
                              command=lambda v: self._look("clip", int(v)))
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
        ow = ttk.Frame(ef)
        ow.grid(row=2, column=0, columnspan=3, sticky="w")
        ttk.Button(ow, text="Its own soundtrack, frames", command=self._own
                   ).pack(side="left")
        self.ofrom, self.oto = tk.IntVar(), tk.IntVar()
        ttk.Spinbox(ow, from_=0, to=9999, width=4, textvariable=self.ofrom
                    ).pack(side="left")
        ttk.Label(ow, text="to").pack(side="left")
        ttk.Spinbox(ow, from_=0, to=9999, width=4, textvariable=self.oto
                    ).pack(side="left")
        self.cues = tk.Listbox(ef, height=5, width=34, exportselection=False)
        self.cues.grid(row=3, column=0, columnspan=3, sticky="we", pady=4)
        self.cues.bind("<<ListboxSelect>>", lambda e: self._cue_pick())
        cf = ttk.Frame(ef)
        cf.grid(row=4, column=0, columnspan=3, sticky="w")
        ttk.Label(cf, text="Level").pack(side="left")
        self.lvl = tk.StringVar(value="1.0")
        ttk.Entry(cf, textvariable=self.lvl, width=5).pack(side="left")
        ttk.Label(cf, text="stops at frame").pack(side="left", padx=(6, 0))
        self.until = tk.StringVar(value="")
        ttk.Entry(cf, textvariable=self.until, width=5).pack(side="left")
        ttk.Button(cf, text="Set", command=self._level).pack(side="left")
        ttk.Button(ef, text="Remove", command=self._rm_cue
                   ).grid(row=5, column=0, sticky="w")
        ttk.Button(ef, text="Listen to the clip's sound", command=self._listen
                   ).grid(row=5, column=1, columnspan=2, sticky="w")
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
        sel = self.lb.selection()
        return sel[0] if sel and sel[0] in self.app.proj.d["clips"] else None

    def select(self, cid):
        self.app.nb.select(self.f)
        if self.lb.exists(cid):
            self.lb.selection_set(cid)
            self.lb.see(cid)

    def refresh(self):
        d = self.app.proj.d
        keep = self.cid()
        self.lb.delete(*self.lb.get_children())
        for cid, c in d["clips"].items():
            opening = any(o["clip"] == cid for o in d["openings"])
            joins = "opening" if opening else "(ends)" if c.get("ends") else \
                ", ".join(map(str, c["anchors"])) or "none!"
            self.lb.insert("", "end", iid=cid, text=cid, values=(
                joins, "-" if c.get("ends") or opening else c["exit"]))
        for k, (path, _then) in enumerate(self.app.pending):
            self.lb.insert("", "end", iid="*pending %d" % k, tags=("pending",),
                           text=os.path.basename(path),
                           values=("(reading...)", ""))  # (cid(): not a clip)
        self.lb.tag_configure("pending", foreground="grey40")
        if keep in d["clips"] and self.lb.selection() != (keep,):
            self.lb.selection_set(keep)
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
        n = c["out"] - c["in"]
        if not self.oto.get() or self.oto.get() > n:
            self.ofrom.set(0)
            self.oto.set(n)
        self.cues.delete(0, "end")
        for q in c["cues"]:
            self.cues.insert("end", "frame %3d%s  %-14s level %.2f" % (
                q["frame"], " to %d" % q["until"] if q.get("until") is not None else "",
                q["sound"][:14], q.get("level", 1.0)))
        if self.rep is not None:
            self._report()

    def _pick(self, again=True):
        """A clip chosen: read its frames and the idle's, then report. A
        Treeview re-selecting the same clip (refresh rebuilds the list,
        which fires the event again) is not a new choice"""
        cid = self.cid()
        if not cid or (not again and cid == self.picked):
            return
        self.picked = cid
        if not self.app.proj.d.get("idle"):
            self.refresh()          # (its source, in and out shown: "Set"
            return                  # would write the last clip's onto it)
        c = self.app.proj.d["clips"][cid]
        idle_src = self.app.proj.d["idle"]["source"]
        self.view = ("clip", int(self.scrub.get()))

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

    def _look(self, kind, n):
        """Show what was CLICKED: a frame of the clip (the scrub), of its
        source (the In or Out field), or of the idle loop (the strip) -
        not what is set"""
        self.view = (kind, int(n))
        self._show()

    def _look_src(self, var):
        try:
            self._look("src", var.get())
        except Exception:
            pass

    def _view_px(self):
        """The picture of what was clicked, and the clip frame it is (for
        the difference); None before the frames are read"""
        fr, c = self._frames()
        idle = self._idle()
        kind, n = self.view
        if kind == "idle":
            return (None if idle is None else idle[min(n, len(idle) - 1)]), \
                "idle frame %d" % n
        if fr is None:
            return None, ""
        if kind == "src":
            n = max(0, min(n, len(fr) - 1))
            return fr[n], "source frame %d" % n
        n = max(0, min(n, c["out"] - c["in"]))
        return fr[c["in"] + n], "clip frame %d" % n

    def _show(self):
        import vbtana as A
        from PIL import Image, ImageTk
        if not self.cid():
            return
        idle = self._idle()
        px, what = self._view_px()
        if px is None:
            return
        pal = self.app.proj.d["target"]["palette"]
        if self.diff.get() and idle is not None and self.view[0] != "idle":
            im = Image.fromarray(A.diff_rgb(px, idle[self.sel], pal))
            im = im.resize((im.width * SCALE, im.height * SCALE), Image.NEAREST)
            self._img = ImageTk.PhotoImage(im)
        else:
            self._img = self.app.photo(px, band=True)
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
            if not self.diff.get():     # the difference keeps the clip's
                self.view = ("idle", k)     # frame; the picture alone
            self._show()                    # shows the frame clicked

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
            self.select(name)
        self.app.screens(sid, got)

    def _add_videos(self):
        """Each video a source and a clip of the whole of it (App.add_clip_file,
        what a video dropped on the window does)"""
        from tkinter import filedialog
        ps = filedialog.askopenfilenames(title="Videos for clips",
                                         filetypes=[("Video", " ".join(
                                             "*" + e for e in VIDEO_EXT)),
                                             ("Everything", "*")])
        for p in ps or ():
            self.app.add_clip_file(p)

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
            if own and own["kind"] == "source" and own.get("whole", True):
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
            try:
                self.app.edit("use_own_sound", self.cid(), 1.0,
                              self.ofrom.get(), self.oto.get())
            except Exception:
                self.app.say("Its own soundtrack: two frame numbers of the clip")

    def _cue_pick(self):
        i = self._cue_i()
        c = self.app.proj.d["clips"].get(self.cid() or "")
        if i is None or not c or i >= len(c["cues"]):
            return
        q = c["cues"][i]
        self.lvl.set("%g" % q.get("level", 1.0))
        self.until.set("" if q.get("until") is None else str(q["until"]))
        self.scrub.set(q["frame"])

    def _cue_i(self):
        s = self.cues.curselection()
        return s[0] if s else None

    def _level(self):
        i = self._cue_i()
        if i is None or not self.cid():
            return
        try:
            lv = float(self.lvl.get())
            u = self.until.get().strip()
            u = int(u) if u else None
        except ValueError:
            return self.app.say("A level is a number (1.0 is as recorded), "
                                "and it stops at a frame of the clip or at "
                                "nothing (it plays out)")
        q = self.app.proj.d["clips"][self.cid()]["cues"][i]
        if lv != q.get("level", 1.0):
            self.app.edit("set_cue_level", self.cid(), i, lv)
        if u != q.get("until"):
            self.app.edit("set_cue_until", self.cid(), i, u)

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


# --- the Openings tab (plan 10.3) ---------------------------------------------------

def opening_lines(proj):
    """The Openings list's lines: each opening in turn, its sound, and the
    last one's join onto the idle loop"""
    d = proj.d
    out = []
    for i, o in enumerate(d["openings"]):
        c = d["clips"].get(o["clip"], {})
        n = c.get("out", 0) - c.get("in", 0) + 1
        out.append("%d. %-14s %5.1f s  %-11s %s%s" % (
            i + 1, o["clip"], n / float(d["target"]["fps"]),
            "FULL SCREEN" if o.get("full") else "with menu",
            o.get("sound") or "(silent)",
            "  - the scene: joins idle frame %d" % c.get("exit", 0)
            if i == len(d["openings"]) - 1 else ""))
    return out


class OpeningsTab(object):
    """The openings, in turn from black: each a clip played whole over the
    whole screen with its own sound; the last, the SCENE opening, joins the
    idle loop at its exit, and when its sound ends the MUSIC comes in at
    the join - which is suggested here (vbtana.music_joins), listened to,
    and used"""

    def __init__(self, app, parent):
        tk, ttk = app.tk, app.ttk
        self.app = app
        self.scroll = Scroll(app, parent, 6)    # (the page scrolls)
        self.f, f = self.scroll.outer, self.scroll.inner
        self.joins = []
        self.frames = None          # (pictures, the openings' first frames)
        left = ttk.Frame(f)
        left.grid(row=0, column=0, sticky="nw")
        ttk.Label(left, text="In turn, from a black screen; the last joins "
                  "the idle loop", wraplength=360).pack(anchor="w")
        self.lb = tk.Listbox(left, height=9, width=58, exportselection=False,
                             font=("TkFixedFont", 9))
        self.lb.pack(anchor="w")
        self.lb.bind("<<ListboxSelect>>", lambda e: self._pick())
        bb = ttk.Frame(left)
        bb.pack(anchor="w", pady=4)
        self.addv = tk.StringVar()
        self.addcb = ttk.Combobox(bb, textvariable=self.addv, state="readonly",
                                  width=16)
        self.addcb.pack(side="left")
        ttk.Button(bb, text="Add", command=self._add).pack(side="left")
        ttk.Button(bb, text="Remove", command=self._remove).pack(side="left")
        ttk.Button(bb, text="Earlier", command=lambda: self._move(-1)
                   ).pack(side="left")
        ttk.Button(bb, text="Later", command=lambda: self._move(1)
                   ).pack(side="left")
        of = ttk.LabelFrame(left, text="The opening chosen", padding=6)
        of.pack(anchor="w", fill="x", pady=4)
        ttk.Label(of, text="Clip").grid(row=0, column=0, sticky="w")
        self.clipv = tk.StringVar()
        self.clipcb = ttk.Combobox(of, textvariable=self.clipv,
                                   state="readonly", width=18)
        self.clipcb.grid(row=0, column=1, sticky="w")
        self.clipcb.bind("<<ComboboxSelected>>", lambda e: self._set_clip())
        ttk.Label(of, text="Its sound").grid(row=1, column=0, sticky="w")
        self.sndv = tk.StringVar()
        self.sndcb = ttk.Combobox(of, textvariable=self.sndv, state="readonly",
                                  width=18)
        self.sndcb.grid(row=1, column=1, sticky="w")
        self.sndcb.bind("<<ComboboxSelected>>", lambda e: self._set_sound())
        ttk.Button(of, text="Its own soundtrack", command=self._own
                   ).grid(row=1, column=2, padx=4)
        ttk.Label(of, text="Joins idle frame").grid(row=2, column=0, sticky="w")
        self.exitv = tk.IntVar()
        self.exitsp = ttk.Spinbox(of, from_=0, to=9999, width=6,
                                  textvariable=self.exitv)
        self.exitsp.grid(row=2, column=1, sticky="w")
        ttk.Button(of, text="Set", command=self._set_exit
                   ).grid(row=2, column=2, sticky="w", padx=4)
        ttk.Button(of, text="Recommend", command=self._rec_exit
                   ).grid(row=2, column=3, sticky="w")
        # FULL SCREEN, or the canvas with the menu below (126.3): the full
        # ones first, the last always with the menu
        ttk.Label(of, text="Screen").grid(row=3, column=0, sticky="w")
        self.fullv = tk.IntVar()
        sb = ttk.Frame(of)
        sb.grid(row=3, column=1, columnspan=3, sticky="w")
        self.full_rb = ttk.Radiobutton(sb, text="Full screen",
                                       variable=self.fullv, value=1,
                                       command=self._set_full)
        self.full_rb.pack(side="left")
        ttk.Radiobutton(sb, text="With the menu (video at the top)",
                        variable=self.fullv, value=0,
                        command=self._set_full).pack(side="left", padx=6)
        self.exit_l = ttk.Label(of, text="", wraplength=380)
        self.exit_l.grid(row=4, column=0, columnspan=4, sticky="w")
        # the music coming in
        mf = ttk.LabelFrame(left, text="The music comes in", padding=6)
        mf.pack(anchor="w", fill="x", pady=4)
        self.join_l = ttk.Label(mf, text="", wraplength=380)
        self.join_l.pack(anchor="w")
        jb = ttk.Frame(mf)
        jb.pack(anchor="w", pady=2)
        ttk.Button(jb, text="Suggest the join", command=self._suggest
                   ).pack(side="left")
        ttk.Button(jb, text="Listen to the join", command=self._listen_join
                   ).pack(side="left")
        ttk.Button(jb, text="Use", command=self._use_join).pack(side="left")
        self.jl = tk.Listbox(mf, height=5, width=52, exportselection=False)
        self.jl.pack(anchor="w")
        self.jl.bind("<<ListboxSelect>>", lambda e: self._join_pick())
        # the picture: the openings in turn
        right = ttk.Frame(f)
        right.grid(row=0, column=1, sticky="n", padx=8)
        self.pic = tk.Label(right, bg="black")
        self.pic.pack()
        self.scrub = tk.Scale(right, from_=0, to=0, orient="horizontal",
                              length=640, label="The openings' frame",
                              command=lambda v: self._show(int(v)))
        self.scrub.pack()
        pb = ttk.Frame(right)
        pb.pack(anchor="w")
        self.play_b = ttk.Button(pb, text="Play the start, with its sound",
                                 command=self._play)
        self.play_b.pack(side="left")
        ttk.Button(pb, text="Load the pictures", command=self._load
                   ).pack(side="left", padx=4)
        self.info = ttk.Label(right, text="", wraplength=640)
        self.info.pack(anchor="w", pady=4)
        self.pending_join = None        # a candidate clicked, not yet Used

    # --- the list -------------------------------------------------------------
    def i(self):
        s = self.lb.curselection()
        return s[0] if s and s[0] < len(self.app.proj.d["openings"]) else None

    def refresh(self):
        d = self.app.proj.d
        keep = self.i()
        self.lb.delete(0, "end")
        for line in opening_lines(self.app.proj):
            self.lb.insert("end", line)
        for path, then in self.app.pending:     # A VIDEO ON ITS WAY: seen at
            if then == self.app._as_opening:    # once where it was dropped
                self.lb.insert("end", "   %s  (reading...)"
                               % os.path.basename(path))
                self.lb.itemconfig("end", foreground="grey40")
        if keep is not None and keep < len(d["openings"]):
            self.lb.selection_set(keep)
        clips = list(d["clips"])
        self.addcb.config(values=clips)
        self.clipcb.config(values=clips)
        self.sndcb.config(values=["(silent)"] + list(d["sounds"]))
        i = self.i()
        if i is not None:
            o = d["openings"][i]
            self.clipv.set(o["clip"])
            self.sndv.set(o.get("sound") or "(silent)")
            last = i == len(d["openings"]) - 1
            c = d["clips"].get(o["clip"])
            self.exitv.set(c["exit"] if c else 0)
            self.fullv.set(1 if o.get("full") else 0)
            self.full_rb.config(state="disabled" if last else "normal")
            st = "normal" if last else "disabled"
            self.exitsp.config(state=st)
            if not last:
                self.exit_l.config(text="Only the last opening joins the "
                                   "idle loop: this one runs into the next.")
            else:
                self.exit_l.config(text="The last opening joins the idle "
                                   "loop, so it plays with the menu.")
        self._join_text()
        self.frames = None if self.frames and self.frames[2] != self._key() \
            else self.frames

    def _key(self):
        d = self.app.proj.d
        return json.dumps([d["openings"], [d["clips"].get(o["clip"]) for o in
                                           d["openings"]]], sort_keys=True)

    def _join_text(self):
        d = self.app.proj.d
        t = d["target"]
        fps, rate = t["fps"], t["rate"]
        n = sum(d["clips"][o["clip"]]["out"] - d["clips"][o["clip"]]["in"] + 1
                for o in d["openings"] if o["clip"] in d["clips"])
        m = d.get("music") or {}
        j = self.pending_join if self.pending_join is not None else m.get("join")
        if not m:
            return self.join_l.config(text="No music yet (the Battle tab)")
        self.join_l.config(text="After %.2f s of openings, the music comes in "
                           "at %.3f s into %s%s." % (
                               n / float(fps), secs(j or 0, rate), m["sound"],
                               " - not Used yet" if self.pending_join is not None
                               and self.pending_join != m.get("join") else ""))

    def _set_full(self):
        """Full screen, or with the menu. The full ones must come first and
        the last is with the menu: an order that breaks that is said in the
        status bar at once (and the Build tab's checks refuse it)"""
        i = self.i()
        if i is None:
            return
        self.app.edit("set_opening", i, full=bool(self.fullv.get()))
        if getattr(self, "loaded", False):  # the preview as it now is (a
            self._load()                    # first full look encodes)
        bad = [p for p in self.app.proj.check()
               if p.where.startswith("openings.") and "full screen" in p.text]
        if bad:
            self.app.say("Note: opening %d: %s" % (
                int(bad[0].where.split(".")[1]) + 1, bad[0].text))

    def _pick(self):
        self.refresh()
        i = self.i()
        if i is not None and self.frames:
            self.scrub.set(self.frames[1][i])

    def _add(self):
        if self.addv.get():
            self.app.edit("add_opening", self.addv.get())
            self.lb.selection_clear(0, "end")
            self.lb.selection_set(len(self.app.proj.d["openings"]) - 1)
            self.refresh()

    def _remove(self):
        if self.i() is not None:
            self.app.edit("remove_opening", self.i())

    def _move(self, by):
        i = self.i()
        if i is not None:
            self.app.edit("move_opening", i, by)
            j = max(0, min(len(self.app.proj.d["openings"]) - 1, i + by))
            self.lb.selection_clear(0, "end")
            self.lb.selection_set(j)
            self.refresh()

    def _set_clip(self):
        if self.i() is not None:
            self.app.edit("set_opening", self.i(), clip=self.clipv.get())

    def _set_sound(self):
        if self.i() is not None:
            v = self.sndv.get()
            self.app.edit("set_opening", self.i(),
                          sound=None if v == "(silent)" else v)

    def _own(self):
        i = self.i()
        if i is None:
            return
        cid = self.app.proj.d["openings"][i]["clip"]
        src = self.app.proj.d["sources"][self.app.proj.d["clips"][cid]["source"]]
        if src["kind"] == "scene":
            return self.app.say("A made-up video has no soundtrack of its own")
        sid = self.app.proj.own_sound_of(cid)
        self.app.edit("set_opening", i, sound=sid)

    def _set_exit(self):
        i = self.i()
        if i is not None:
            cid = self.app.proj.d["openings"][i]["clip"]
            self.app.edit("set_exit", cid, self.exitv.get())

    def _rec_exit(self):
        """The idle frame the scene opening's last frame is most like: its
        seam's fewest changed pixels"""
        import vbtana as A
        i = self.i()
        d = self.app.proj.d
        if i is None or not d.get("idle"):
            return
        cid = d["openings"][i]["clip"]
        c = d["clips"][cid]

        def got(_):
            fr = self.app.frames[c["source"]]
            idle = self.app.frames[d["idle"]["source"]]
            a, b = d["idle"]["loop"]
            sc = [A.diff(fr[c["out"]], idle[k]) for k in range(a, b)]
            k = min(range(len(sc)), key=sc.__getitem__)
            self.exitv.set(k)
            self.exit_l.config(text="Recommended: idle frame %d, its seam "
                               "%.2f%% of the pixels (the exit now: %d, "
                               "%.2f%%). Set takes it." % (
                                   k, sc[k] * 100, c["exit"],
                                   sc[min(c["exit"], len(sc) - 1)] * 100))
        self.app.screens(d["idle"]["source"],
                         lambda _: self.app.screens(c["source"], got))

    # --- the join ------------------------------------------------------------
    def _suggest(self):
        rate = self.app.proj.d["target"]["rate"]

        def got(res):
            self.joins = res["joins"]
            self.jl.delete(0, "end")
            if not self.joins:
                self.app.say("Nothing to match: the openings end in silence "
                             "(or there are none) - the join is yours to pick")
            for j in self.joins:
                self.jl.insert("end", "at %.3f s: the music before it is "
                               "%.0f%% like the openings' end" % (
                                   secs(j["join"], rate),
                                   max(0, j["score"]) * 100))
            if self.joins:
                self.jl.selection_set(0)
                self._join_pick()
        self.app.run("vbtana", ["joins", self.app.proj.path], got,
                     "Matching the openings' end to the music...")

    def _join_pick(self):
        s = self.jl.curselection()
        if s and s[0] < len(self.joins):
            self.pending_join = self.joins[s[0]]["join"]
            self._join_text()

    def _use_join(self):
        if self.pending_join is not None:
            self.app.edit("set", ("music", "join"), self.pending_join)
            self.pending_join = None
            self._join_text()

    def _listen_join(self):
        out = os.path.join(tempfile.gettempdir(), "vbtgui-join.wav")
        argv = ["joinwav", self.app.proj.path, out]
        if self.pending_join is not None:
            argv += ["--join", str(self.pending_join)]

        def got(res):
            if not play_wav(res["out"]):
                self.app.say("No player here: the WAV is %s" % res["out"])
        self.app.run("vbtana", argv, got, "Mixing the join...")

    # --- the picture -----------------------------------------------------------
    def _load(self, then=None):
        """Every opening's frames, read (a job each the first time), then
        the openings in turn as whole screens: the canvas over black"""
        d = self.app.proj.d
        # a FULL-SCREEN opening's own encode, over the whole screen, as the
        # build makes it - the canvas's for one with the menu
        srcs = [(d["clips"][o["clip"]]["source"], bool(o.get("full")))
                for o in d["openings"] if o["clip"] in d["clips"]]
        if d.get("idle"):
            srcs.append((d["idle"]["source"], False))

        def step(k):
            if k < len(srcs):
                sid, full = srcs[k]
                return self.app.screens(sid, lambda _: step(k + 1), full=full)
            pics, starts = [], []
            for o in d["openings"]:
                c = d["clips"][o["clip"]]
                fk = (c["source"], "full") if o.get("full") else c["source"]
                starts.append(len(pics))
                pics += list(self.app.frames[fk][c["in"]:c["out"] + 1])
            self.loaded = True
            self.frames = (pics, starts, self._key())
            self.scrub.config(to=max(0, len(pics) - 1))
            self._show(0)
            if then:
                then()
        step(0)

    def _band_from(self):
        """The preview's frame number the band comes in at, as the engine
        brings it in: the first frame of the first opening with the menu
        (the last's, if every one were full screen)"""
        d, starts = self.app.proj.d, self.frames[1]
        for k, o in enumerate(d["openings"]):
            if not o.get("full") and k < len(starts):
                return starts[k]
        return starts[-1] if starts else 0

    def _pic(self, px, n):
        """Preview frame N: the canvas with the band under it from where
        the engine lets it in, black before; a full-screen frame whole"""
        if px.shape[0] < 200 and n >= self._band_from() and \
                self.app.band_rgb() is not None and self.app.show_band.get():
            return self.app.photo(px, band=True)
        return self.app.photo(self._whole(px))

    def _whole(self, px):
        import numpy as np
        h = 200 - px.shape[0]
        return np.vstack([px, np.zeros((h, px.shape[1]), px.dtype)]) if h > 0 \
            else px

    def _show(self, n):
        if not self.frames or not self.frames[0]:
            return
        pics, starts, _ = self.frames
        n = max(0, min(n, len(pics) - 1))
        k = max(i for i, s0 in enumerate(starts) if s0 <= n)
        self._img = self._pic(pics[n], n)
        self.pic.config(image=self._img)
        self.info.config(text="Opening %d (%s), its frame %d" % (
            k + 1, self.app.proj.d["openings"][k]["clip"], n - starts[k]))

    def _play(self):
        """The battle's START: the openings in turn, then the idle loop from
        the scene's join, for 4 s - with the sound the engine makes (the
        openings' own, then the music at the join)"""
        if self.app.anim:
            self.app.root.after_cancel(self.app.anim)
            self.app.anim = None
            self.play_b.config(text="Play the start, with its sound")
            return
        d = self.app.proj.d
        if not d["openings"] or not d.get("idle"):
            return self.app.say("Needs an opening and the idle loop")
        if not self.frames or self.frames[2] != self._key():
            return self._load(self._play)
        pics = list(self.frames[0])
        idle = self.app.frames[d["idle"]["source"]]
        a, b = d["idle"]["loop"]
        e = d["clips"][d["openings"][-1]["clip"]]["exit"]
        n = b - a
        pics += [idle[a + (e + k) % n] for k in range(1, 4 * d["target"]["fps"])]
        imgs = [self._pic(p, i) for i, p in enumerate(pics)]
        op = sum(len(range(d["clips"][o["clip"]]["in"],
                           d["clips"][o["clip"]]["out"] + 1))
                 for o in d["openings"])
        out = os.path.join(tempfile.gettempdir(), "vbtgui-start.wav")
        step = [0]

        def tick():
            if step[0] >= len(imgs):
                self.app.anim = None
                self.play_b.config(text="Play the start, with its sound")
                return
            self._img = imgs[step[0]]
            self.pic.config(image=self._img)
            step[0] += 1
            self.app.anim = self.app.root.after(1000 // d["target"]["fps"], tick)

        def got(res):
            play_wav(res["out"])
            self.play_b.config(text="Stop")
            tick()
        before = op / float(d["target"]["fps"]) + 0.01
        self.app.run("vbtana", ["joinwav", self.app.proj.path, out,
                                "--before", "%.3f" % before], got,
                     "Mixing the start...")


# --- the Title & endings tab (SPEC.md 126.3.1) ----------------------------------------

# what a VGA rendition's colours cost, measured (VBATTLE-MEMORY-PLAN 2: the
# owner's Attack1, unbudgeted, against CGA4's 82 KB/s)
REND_NOTE = {4: "Four: black and three chosen from the footage, none a "
                "clip's own - about what CGA4 costs (x1.2). \"Keep the "
                "band's CGA colours\" makes them CGA's own four.",
             8: "Eight: black, white and six chosen from the idle loop "
                "and every clip for colour - about twice CGA4's data "
                "(x2.1). Usually the right choice.",
             16: "Sixteen: black, white and fourteen chosen from the idle "
                 "loop and every clip - about three times CGA4's data "
                 "(x3.1): a fast disk, or memory to hold it."}
# each clip's own VGA colours (target.vga_clip_colours; SPEC.md 126.3.2)
OWN_NOTE = ("%d of the %d are each clip's own, loaded as it starts (auto: "
            "none - every colour shared, chosen over the idle loop and every "
            "clip, so a join keeps one dither; a clip's own may suit footage "
            "whose clips have colours of their own)")
# VGA's write bands (target.vga_bands, rows; 0 the whole frame): measured on
# 86Box's VGA XT with real footage - the flash pixels a sampled frame and
# the records' bytes against the whole frame (SPEC.md 126.3.2)
VGA_BANDS = ((0, "Whole frame (flashes)"), (32, "Bands of 32 rows"),
             (16, "Bands of 16 rows"), (8, "Bands of 8 rows"))
VGA_BANDS_DEFAULT = 16
VGA_BAND_NOTE = {
    0: "Smallest and fastest; a changing pixel shows a wrong colour while "
       "the frame is written (~300 flash pixels a frame on real footage).",
    32: "~4x fewer flashes for ~6% more data.",
    16: "~12x fewer flashes for ~9% more data. The default.",
    8: "~14x fewer flashes for ~24% more data: a fast disk or a hold."}
BAYER4 = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))
HOW_LABELS = (("hold", "Hold its last frame"),
              ("fade", "Fade to its end clip"),
              ("join", "Join its end clip"),
              ("again", "Fade, and play again"))
PLAYS_LABELS = (("loop", "round, until a key"), ("once", "once"))
THEN_LABELS = (("hold", "hold its last frame"),
               ("fade", "fade to the title"),
               ("join", "join the title"))


def fade_masks():
    """The fade's byte masks, by threshold 1..16 and the row's y & 3 - the
    engine's VB_FMASK table (apps/vbattle/vbloop.inc), which the test holds
    this to"""
    out = []
    for t in range(1, 17):
        row = []
        for y in range(4):
            m = 0
            for x in range(4):
                if BAYER4[y][x] >= t:
                    m |= 3 << (6 - 2 * x)
            row.append(m)
        out.append(row)
    return out


def fade_frame(px, step, steps):
    """Pixel frame PX at fade step STEP of STEPS, as the engine draws it: the
    pixels whose 4 x 4 rank is under the threshold put to colour 0"""
    import numpy as np
    t = step * 16 // steps
    h, w = px.shape
    rank = np.array(BAYER4)[np.arange(h)[:, None] % 4, np.arange(w)[None, :] % 4]
    out = px.copy()
    out[rank < t] = 0
    return out


def loop_lines(cands, fps, against=None):
    """The candidates' lines: frames, seconds, the seam's difference - and,
    for an ending that JOINS, how its last frame differs from each start"""
    out = []
    for c in cands:
        out.append("frames %d to %d (%.1f s): the seam %.2f%%%s" % (
            c["start"], c["end"] - 1, (c["end"] - c["start"]) / float(fps),
            c["score"] * 100, "" if against is None else
            ", joined %.2f%%" % (against[c["start"]] * 100)))
    return out


def spread_anchors(scores, best, most=4):
    """Of the frames within reach of the BEST (vbtana.attach's anchors and
    any as good), the lowest-scoring at least a quarter of the loop apart:
    four anchors side by side wait as long as one"""
    n = len(scores)
    lo = min(scores)
    pool = sorted(set(best) | {k for k in range(n)
                               if scores[k] <= lo * 1.5 + 0.002},
                  key=lambda k: (scores[k], k))
    gap = max(1, n // most)
    out = []
    for k in pool:
        if all(min(abs(k - j), n - abs(k - j)) >= gap for j in out):
            out.append(k)
        if len(out) == most:
            break
    return sorted(out)


class LoopPanel(object):
    """One loop - the title, or an ending's - with its clip, its frames,
    the recommended loops of that clip (vbtana.loop_candidates, the idle
    loop's own), its sound, and Play; and for an ending, how it is reached"""

    def __init__(self, tab, parent, kind):
        tk, ttk = tab.app.tk, tab.app.ttk
        self.tab, self.app, self.kind = tab, tab.app, kind
        self.cands, self.pending = [], None
        title = {"title": "The title: before the openings, round until "
                 "Enter or Space", "victory": "Victory",
                 "defeat": "Defeat"}[kind]
        self.f = f = ttk.LabelFrame(parent, text=title, padding=4)
        r = 0
        if kind != "title":
            ttk.Label(f, text="Then").grid(row=r, column=0, sticky="w")
            self.how = tk.StringVar()
            cb = ttk.Combobox(f, textvariable=self.how, state="readonly",
                              values=[l for _, l in HOW_LABELS], width=22)
            cb.grid(row=r, column=1, columnspan=3, sticky="w")
            cb.bind("<<ComboboxSelected>>", lambda e: self._commit())
            r += 1
            # THE END CLIP (126.3.1): full screen or the canvas, round or
            # once, and after one that plays once - the title, or a hold
            ef = ttk.Frame(f)
            ef.grid(row=r, column=0, columnspan=4, sticky="w")
            self.fullv = tk.IntVar()
            ttk.Checkbutton(ef, text="Full screen", variable=self.fullv,
                            command=self._commit).pack(side="left")
            ttk.Label(ef, text="  It plays").pack(side="left")
            self.plays = tk.StringVar()
            pc = ttk.Combobox(ef, textvariable=self.plays, state="readonly",
                              width=16, values=[l for _, l in PLAYS_LABELS])
            pc.pack(side="left", padx=2)
            pc.bind("<<ComboboxSelected>>", lambda e: self._commit())
            self.musv = tk.IntVar()     # (once, its own sound: the music
            self.muscb = ttk.Checkbutton(   # back after it, or silence)
                ef, text="Battle music after its sound",
                variable=self.musv, command=self._commit)
            self.muscb.pack(side="left", padx=6)
            r += 1
            tf = ttk.Frame(f)
            tf.grid(row=r, column=0, columnspan=4, sticky="w")
            ttk.Label(tf, text="After it (when once)").pack(side="left")
            self.then = tk.StringVar()
            self.thencb = ttk.Combobox(tf, textvariable=self.then,
                                       state="readonly", width=18,
                                       values=[l for _, l in THEN_LABELS])
            self.thencb.pack(side="left", padx=2)
            self.thencb.bind("<<ComboboxSelected>>", lambda e: self._commit())
            r += 1
            # ITS LINE in the band (126.3.1): the author's, or the builder's
            # default - which names a key only where it works from the start
            lf = ttk.Frame(f)
            lf.grid(row=r, column=0, columnspan=4, sticky="w")
            ttk.Label(lf, text="Its line").pack(side="left")
            self.line = tk.StringVar()
            le = ttk.Entry(lf, textvariable=self.line, width=40)
            le.pack(side="left", padx=2)
            le.bind("<Return>", lambda e: self._line())
            le.bind("<FocusOut>", lambda e: self._line())
            self.lnote = ttk.Label(lf, text="")
            self.lnote.pack(side="left", padx=4)
            r += 1
        ttk.Label(f, text="Loop clip" if kind == "title" else "End clip"
                  ).grid(row=r, column=0, sticky="w")
        self.clip = tk.StringVar()
        self.clipcb = ttk.Combobox(f, textvariable=self.clip, state="readonly",
                                   width=16)
        self.clipcb.grid(row=r, column=1, columnspan=2, sticky="w")
        self.clipcb.bind("<<ComboboxSelected>>", lambda e: self._clip())
        r += 1
        ttk.Label(f, text="Frames").grid(row=r, column=0, sticky="w")
        self.a, self.b = tk.IntVar(), tk.IntVar()
        ff = ttk.Frame(f)
        ff.grid(row=r, column=1, columnspan=3, sticky="w")
        ttk.Spinbox(ff, from_=0, to=9999, width=5, textvariable=self.a,
                    command=self._look).pack(side="left")
        ttk.Label(ff, text="to (not incl.)").pack(side="left")
        ttk.Spinbox(ff, from_=1, to=9999, width=5, textvariable=self.b,
                    command=self._look).pack(side="left")
        ttk.Button(ff, text="Set", command=self._commit).pack(side="left")
        r += 1
        bf = ttk.Frame(f)
        bf.grid(row=r, column=0, columnspan=4, sticky="w")
        ttk.Button(bf, text="Recommend", command=self._recommend
                   ).pack(side="left")
        ttk.Button(bf, text="Play the loop", command=self._play
                   ).pack(side="left")
        r += 1
        self.cl = tk.Listbox(f, height=3, width=54, exportselection=False)
        self.cl.grid(row=r, column=0, columnspan=4, sticky="we")
        self.cl.bind("<<ListboxSelect>>", lambda e: self._cand())
        r += 1
        ttk.Label(f, text="Its sound").grid(row=r, column=0, sticky="w")
        self.snd = tk.StringVar()
        self.sndcb = ttk.Combobox(f, textvariable=self.snd, state="readonly",
                                  width=22)
        self.sndcb.grid(row=r, column=1, columnspan=3, sticky="w")
        self.sndcb.bind("<<ComboboxSelected>>", lambda e: self._commit())
        r += 1
        if kind == "title":
            ttk.Label(f, text="Left by").grid(row=r, column=0, sticky="w")
            self.leave = tk.StringVar()
            self.leavecb = ttk.Combobox(f, textvariable=self.leave,
                                        state="readonly", width=16)
            self.leavecb.grid(row=r, column=1, columnspan=2, sticky="w")
            self.leavecb.bind("<<ComboboxSelected>>", lambda e: self._commit())
            r += 1
            ttk.Label(f, text="Entered at").grid(row=r, column=0, sticky="w")
            self.anch = tk.StringVar()
            af = ttk.Frame(f)
            af.grid(row=r, column=1, columnspan=3, sticky="w")
            e = ttk.Entry(af, textvariable=self.anch, width=14)
            e.pack(side="left")
            e.bind("<Return>", lambda ev: self._commit())
            ttk.Button(af, text="Recommend", command=self._rec_anchors
                       ).pack(side="left")
            r += 1
        self.note = ttk.Label(f, text="", wraplength=400)
        self.note.grid(row=r, column=0, columnspan=4, sticky="w")

    # --- the model's view ------------------------------------------------------
    def spec(self):
        """The loop as the project holds it, or None"""
        d = self.app.proj.d
        if self.kind == "title":
            return d.get("title")
        return d["endings"][self.kind].get("loop")

    def refresh(self):
        d = self.app.proj.d
        clips = ["(none)"] + list(d["clips"])
        self.clipcb.config(values=clips)
        self.sndcb.config(values=["(none)", "its own soundtrack"] +
                          list(d["sounds"]))
        lp = self.spec()
        self.clip.set(lp["clip"] if lp else "(none)")
        if lp:
            self.a.set(lp["loop"][0])
            self.b.set(lp["loop"][1])
            self.snd.set(lp.get("sound") or "(none)")
        if self.kind == "title":
            self.leavecb.config(values=["a fade"] + list(d["clips"]))
            t = d.get("title") or {}
            self.leave.set(t.get("leave") or "a fade")
            self.anch.set(" ".join(map(str, t.get("anchors", []))))
            self.note.config(text="" if not lp else
                             "Enter or Space: %s." % (
                                 "the screen fades, and the openings play"
                                 if not t.get("leave") else
                                 "%s is played from the title's next anchor, "
                                 "and the openings after it" % t["leave"]))
        else:
            en = d["endings"][self.kind]
            self.how.set(dict(HOW_LABELS)[en["how"]])
            self.fullv.set(1 if en.get("full") else 0)
            once = en.get("plays") == "once"
            self.plays.set(dict(PLAYS_LABELS)["once" if once else "loop"])
            self.then.set(dict(THEN_LABELS)[en.get("then") or "hold"])
            self.thencb.config(state="readonly" if once else "disabled")
            self.musv.set(1 if en.get("music_after") else 0)
            import os88vbt as B_
            self.line.set(en.get("line") or "")
            self.lnote.config(text="(empty: \"%s\")" % B_.ending_line(
                self.kind, en["how"], en.get("loop") if en["how"] in
                ("fade", "join") else None, en.get("then")))
            self.muscb.config(state="normal" if once and (en.get("loop") or {})
                              .get("sound") else "disabled")
            ex = d["enemy"].get(self.kind)
            where = "over the whole screen (the band down)" if \
                en.get("full") else "in the canvas, under the band"
            after = {"hold": "its last frame stays; Enter plays again",
                     "fade": "the screen fades and the play starts again "
                             "at the title",
                     "join": "its last frame joins the title's first and "
                             "the play starts again there"}[
                en.get("then") or "hold"]
            clip_ = ("it plays once %s, then %s." % (where, after)) if once \
                else ("it goes round %s; Enter plays again." % where)
            self.note.config(text={
                "hold": "Its clip's last frame stays up; Esc leaves.",
                "fade": "The %s fades to the end clip: %s" % (
                    "screen" if en.get("full") else "canvas", clip_),
                "join": "%s's last frame joins the end clip's first, "
                        "exactly: %s" % (ex or "(no %s clip)" % self.kind,
                                         clip_),
                "again": "The screen fades, and the play starts over."}[
                            en["how"]])

    def _commit(self):
        """What the panel says, into the project"""
        cid = self.clip.get()
        cid = None if cid in ("", "(none)") else cid
        try:
            a, b = self.a.get(), self.b.get()
        except Exception:
            return self.app.say("A loop's frames are two numbers")
        loop = (a, b) if cid else None
        snd = self.snd.get()
        if cid and snd == "%s_lp" % cid[:12]:   # its own: under the new frames
            snd = "its own soundtrack"
        if snd == "its own soundtrack" and cid:
            snd = self.app.proj.loop_sound(cid, loop)
        elif snd in ("", "(none)", "its own soundtrack"):
            snd = None
        if self.kind == "title":
            lv = self.leave.get()
            anchors = [int(x) for x in self.anch.get().replace(",", " ").split()
                       if x.isdigit()]
            self.app.edit("set_title", cid, loop, snd,
                          None if lv in ("", "a fade") else lv, anchors)
        else:
            how = dict((l, k) for k, l in HOW_LABELS).get(self.how.get(), "hold")
            plays = dict((l, k) for k, l in PLAYS_LABELS).get(
                self.plays.get(), "loop")
            then = dict((l, k) for k, l in THEN_LABELS).get(
                self.then.get(), "hold")
            self.app.edit("set_ending", self.kind, how, cid, loop, snd,
                          full=bool(self.fullv.get()), plays=plays, then=then,
                          music_after=bool(self.musv.get()))

    def _line(self):
        """Its line, into the project: 38 plain characters at most, empty
        for the builder's default"""
        t = "".join(ch for ch in self.line.get() if " " <= ch <= "~")[:38]
        t = t.strip()
        en = self.app.proj.d["endings"][self.kind]
        if t != (en.get("line") or ""):
            self.app.edit("set", ("endings", self.kind, "line"), t or None)

    def _clip(self):
        cid = self.clip.get()
        if cid in self.app.proj.d["clips"]:
            c = self.app.proj.d["clips"][cid]
            self.a.set(0)
            self.b.set(c["out"] - c["in"] + 1)
        self.cands = []
        self.cl.delete(0, "end")
        self._commit()

    # --- the clip's frames, and the recommendation ------------------------------
    def frames(self, then):
        d = self.app.proj.d
        cid = self.clip.get()
        if cid not in d["clips"]:
            return self.app.say("Choose the loop's clip first")
        c = d["clips"][cid]
        self.app.screens(c["source"], lambda fr: then(fr[c["in"]:c["out"] + 1]))

    def _recommend(self):
        import vbtana as A
        d = self.app.proj.d

        def got(fr):
            self.cands = [dict(score=sc, start=a, end=b) for sc, a, b in
                          A.loop_candidates(fr, max(4, min(20, len(fr) // 3)))]
            against = None
            ex = d["enemy"].get(self.kind) if self.kind != "title" else None
            if self.kind != "title" and d["endings"][self.kind]["how"] == "join" \
                    and ex in d["clips"]:
                ec = d["clips"][ex]
                last = self.app.frames.get(ec["source"])
                if last is not None:
                    against = [A.diff(last[ec["out"]], f) for f in fr]
            self.cl.delete(0, "end")
            for line in loop_lines(self.cands, d["target"]["fps"], against):
                self.cl.insert("end", line)
            if self.cands:
                self.cl.selection_set(0)
                self._cand()
        ex = d["enemy"].get(self.kind) if self.kind != "title" else None
        if ex in d["clips"]:
            self.app.screens(d["clips"][ex]["source"],
                             lambda _: self.frames(got))
        else:
            self.frames(got)

    def _cand(self):
        s = self.cl.curselection()
        if s and s[0] < len(self.cands):
            c = self.cands[s[0]]
            self.a.set(c["start"])
            self.b.set(c["end"])
            self.note.config(text="Frames %d to %d previewed - Set takes them."
                             % (c["start"], c["end"] - 1))
            self._look()

    def _look(self):
        self.frames(lambda fr: self.tab.show(fr[min(self.a.get(),
                                                    len(fr) - 1)],
                                             self.kind != "title"))

    def _play(self):
        def got(fr):
            a, b = self.a.get(), self.b.get()
            self.tab.play(fr[a:b], self.kind != "title")
        self.frames(got)

    def _rec_anchors(self):
        """The title's frames its way out is most like at its start
        (vbtana.attach, the clips' own rule)"""
        import vbtana as A
        d = self.app.proj.d
        lv = self.leave.get()
        if lv not in d["clips"]:
            return self.app.say("Choose the clip the title is left by first")
        lc = d["clips"][lv]

        def got(fr):
            loop = fr[self.a.get():self.b.get()]

            def got2(lfr):
                import os88vbt as B     # (both as whole screens: a title's
                r = A.attach([B.full_of(lfr[lc["in"]])],    # frames may be
                             [B.full_of(x) for x in loop], search=1)  # either)
                ks = spread_anchors(r["entry_scores"], r["anchors"])
                self.anch.set(" ".join(map(str, ks)))
                self.note.config(text="Recommended: entered at title frames "
                                 "%s (%.2f%% at the best). Return in the box "
                                 "takes them." % (ks,
                                                  min(r["entry_scores"]) * 100))
            self.app.screens(lc["source"], got2)
        self.frames(got)


class LoopsTab(object):
    """The title loop, and how victory and defeat end (SPEC.md 126.3.1): a
    panel each, one picture for all three, and the fade"""

    def __init__(self, app, parent):
        tk, ttk = app.tk, app.ttk
        self.app = app
        self.scroll = Scroll(app, parent, 6)    # (the page scrolls)
        self.f, f = self.scroll.outer, self.scroll.inner
        left = ttk.Frame(f)
        left.grid(row=0, column=0, sticky="nw")
        self.panels = [LoopPanel(self, left, k)
                       for k in ("title", "victory", "defeat")]
        for p in self.panels:
            p.f.pack(fill="x", pady=3)
        right = ttk.Frame(f)
        right.grid(row=0, column=1, sticky="n", padx=8)
        self.pic = tk.Label(right, bg="black")
        self.pic.pack()
        ff = ttk.LabelFrame(right, text="The fade", padding=4)
        ff.pack(fill="x", pady=4)
        ttk.Label(ff, text="Steps, one a frame").pack(side="left")
        self.steps = tk.IntVar()
        sp = ttk.Spinbox(ff, from_=1, to=16, width=4, textvariable=self.steps,
                         command=self._steps)
        sp.pack(side="left")
        sp.bind("<Return>", lambda e: self._steps())
        ttk.Button(ff, text="Show it", command=self._fade
                   ).pack(side="left", padx=4)
        self.anyk = tk.IntVar()
        ttk.Checkbutton(right, text="Any key continues - leaves the title, "
                        "plays an end loop again (not Enter alone; Esc "
                        "always leaves)", variable=self.anyk,
                        command=lambda: self.app.edit(
                            "set", ("any_key",),
                            True if self.anyk.get() else None)
                        ).pack(anchor="w")
        self.info = ttk.Label(right, text="", wraplength=640)
        self.info.pack(anchor="w")
        self.px = None

    def refresh(self):
        for p in self.panels:
            p.refresh()
        self.steps.set(self.app.proj.d.get("fade_steps", 16))
        self.anyk.set(1 if self.app.proj.d.get("any_key") else 0)

    def _steps(self):
        try:
            n = max(1, min(16, int(self.steps.get())))
        except Exception:
            return
        if n != self.app.proj.d.get("fade_steps"):
            self.app.edit("set", ("fade_steps",), n)

    def whole(self, px, canvas):
        """An end loop's frame over the band's black, a title's as it is"""
        import numpy as np
        h = 200 - px.shape[0]
        return np.vstack([px, np.zeros((h, px.shape[1]), px.dtype)]) \
            if h > 0 else px

    def show(self, px, canvas=False):
        self.px = px
        self._img = self.app.photo(self.whole(px, canvas))
        self.pic.config(image=self._img)

    def play(self, frames, canvas=False):
        if self.app.anim:
            self.app.root.after_cancel(self.app.anim)
            self.app.anim = None
            return
        imgs = [self.app.photo(self.whole(f, canvas)) for f in frames]
        fps = self.app.proj.d["target"]["fps"]
        step = [0]

        def tick():
            self._img = imgs[step[0] % len(imgs)]
            self.pic.config(image=self._img)
            step[0] += 1
            self.app.anim = self.app.root.after(1000 // fps, tick)
        tick()
        self.info.config(text="%d frames round; Play again stops it"
                         % len(frames))

    def _fade(self):
        """The fade on the frame shown, step by step, as the engine draws it"""
        if self.px is None:
            return self.app.say("Show a frame first (a loop's Play or a "
                                "candidate)")
        n = self.app.proj.d.get("fade_steps", 16)
        imgs = [self.app.photo(self.whole(fade_frame(self.px, k, n), False))
                for k in range(n + 1)]
        fps = self.app.proj.d["target"]["fps"]
        step = [0]

        def tick():
            if step[0] > n:
                self.app.anim = None
                return
            self._img = imgs[step[0]]
            self.pic.config(image=self._img)
            step[0] += 1
            self.app.anim = self.app.root.after(1000 // fps, tick)
        if self.app.anim:
            self.app.root.after_cancel(self.app.anim)
        tick()
        self.info.config(text="A fade of %d steps: %.2f s" % (n, n / float(fps)))


# --- the Build tab (plan 4.5) ------------------------------------------------------

# where a check's WHERE sends the window: its tab (and a clip, selected)
CHECK_TABS = (("clips.", "clips"), ("openings", "openings"),
              ("title", "loops"), ("endings", "loops"), ("fade", "loops"),
              ("actions", "actions"), ("characters", "party"),
              ("menu", "party"), ("items", "party"), ("images", "party"),
              ("enemy", "enemy"), ("idle", "idle"), ("music", "battle"),
              ("ui_sounds", "battle"), ("target", "battle"), ("sounds", "clips"))


_VGAC = {}


def vga_colours(path, k=None, clip=None):
    """The Build tab's palette viewer out of a built VGA4 bundle (SPEC.md
    126.3.2): its K colours in index order, each {rgb, value, band, own} -
    the 8-bit colour, the pixel value it is drawn in, which of the band's
    four (the menu's) it serves or None, and whether it is a clip's OWN (one
    of the last SLOTS, swapped as each clip starts) - the idle loop's own,
    or with CLIP (an index of the bundle's clips, the budget's order) that
    clip's, and "usage": each colour's share of the clip's canvas pixels
    over all its frames, decoded as the engine decodes them. K: the
    header's (an older caller's K is ignored)"""
    import numpy as np
    import os88vbt as B
    st = os.stat(path)
    key = (path, st.st_mtime, st.st_size, clip)
    if key in _VGAC:
        return _VGAC[key]
    data = open(path, "rb").read()
    bd = B.Bundle(data)
    k, slots = bd.vga or (k or 8, 0)
    codes = bd.codes or B.vga_codes(k)     # (the bundle's own values)
    bmap = list(data[352:356])
    pal = [tuple(data[464 + 3 * c:467 + 3 * c]) for c in codes]
    c = bd.clips[clip] if clip is not None and 0 <= clip < len(bd.clips) \
        else None
    has = bool(c is not None and c["own"])
    if has:
        pal[k - slots:] = c["own"][:slots]
    out = dict(cols=[dict(rgb=tuple((v << 2) | (v >> 4) for v in pal[i]),
                          value=codes[i],
                          band=bmap.index(codes[i]) if codes[i] in bmap and
                          i < k - slots else None, own=i >= k - slots,
                          free=i >= k - slots and not has)
                     for i in range(k)], usage=None, clip=None,
               slots=slots, has_own=has)
    if c is not None:
        rows = B.REND.rows
        surf = B.REND.blank()
        hist = np.zeros(16, np.int64)
        B.REND.dac_check = False        # (from a blank screen, not the one
        try:                            # the engine plays it over)
            for r in bd.stream(c)[:-1]:
                B.decode(surf, r)
                hist += np.bincount(B.REND.pixels(surf, rows).ravel(),
                                    minlength=16)
        finally:
            B.REND.dac_check = True
        tot = max(1, int(hist.sum()))
        out["usage"] = [float(hist[x["value"]]) / tot for x in out["cols"]]
        out["clip"] = c["name"]
    _VGAC[key] = out
    return out


def check_tab(where):
    for pre, tab in CHECK_TABS:
        if where.startswith(pre):
            return tab
    return "battle"


def budget_rows(bud):
    """The Build tab's per-clip lines out of vbtana.budget: (values, tag),
    tagged "over" when the clip and the music want more than the disk
    gives, "near" past 85% of it"""
    rows = []
    for c in bud["clips"]:
        w = c["with_music"]
        tag = "over" if w > 1.0 else "near" if w > 0.85 else ""

        rows.append(((c.get("id", c["name"]), "%d" % (c["bytes"] // 1024),
                      "%.1f" % c["secs"], "%.1f" % (c["rate"] / 1024),
                      "%.0f%%" % (w * 100), "%.2f" % c["head_secs"],
                      "yes" if c["fits_ring"] else
                      "read round" if c.get("loop") else "streams",
                      " ".join(map(str, c["anchors"])) or "-",
                      "-" if c["wait"] is None or not c["anchors"]
                      else "%d" % c["wait"]), tag))
    return rows


FIT_LABELS = {"fill": "Fill (crop the edges to the shape)",
              "stretch": "Stretch (the whole picture, squeezed)"}


def memory_lines(mem):
    kb = lambda n: (n + 1023) // 1024
    out = ["The resident claim, held while the window is open: %d KB" %
           kb(mem["resident"])]
    out += ["    %-24s %4d KB" % (n, kb(b)) for n, b in mem["parts"]]
    out.append("The card's ring: %d KB" % kb(mem["card"]))
    out.append("Two clip rings, a play's, out of what is left: %s" % (
        "%d KB each" % kb(mem["ring"]) if mem["ring"] else
        "NOT ENOUGH - Play would say Not enough memory"))
    if mem["ring"] and mem["ring"] < 96 * 1024:     # (VBATTLE-RAM-PLAN R4:
        out.append("    SMALL: a ring is what rides out a clip faster "
                   "than the disk, and the build holds clips to it - "
                   "a smaller resident claim (the idle loop, the music, "
                   "the band's pictures) is a larger ring")   # told so)
    return out


class BuildTab(object):
    """What is wrong (every check, double-click to go there), BUILD (the
    bundle, verified), its BUDGET - each clip against the profile's disk,
    the music streaming beside it, and the engine's memory - and a DISK of
    it (os88mkdisk, the encoder's own list)"""
    COLS = (("clip", "Clip", 150), ("kb", "KB", 55), ("secs", "Seconds", 60),
            ("rate", "KB/s", 55), ("disk", "Of the disk", 80),
            ("head", "Head (s)", 65), ("ring", "In a ring", 70),
            ("anch", "Anchors", 110), ("wait", "Wait", 45))

    def __init__(self, app, parent):
        tk, ttk = app.tk, app.ttk
        import os88mkdisk as M
        self.app, self.M = app, M
        self.scroll = Scroll(app, parent, 6)    # (the page scrolls)
        self.f, f = self.scroll.outer, self.scroll.inner
        self.bud = None
        top = ttk.Frame(f)
        top.pack(fill="x")
        ttk.Button(top, text="Build", command=self._build).pack(side="left")
        self.capv = app.tk.IntVar(value=1)      # (126.4.1: on, unless asked)
        ttk.Checkbutton(top, text="Hold clips to the disk", variable=self.capv,
                        command=self._cap).pack(side="left", padx=6)
        self.out_l = ttk.Label(top, text="")
        self.out_l.pack(side="left", padx=8)
        cf = ttk.LabelFrame(f, text="What is wrong (ERROR stops a build; "
                            "double-click to go there)", padding=4)
        cf.pack(fill="x", pady=4)
        self.chk = ttk.Treeview(cf, columns=("where", "text"), height=5,
                                selectmode="browse")
        self.chk.heading("#0", text="")
        self.chk.heading("where", text="Where")
        self.chk.heading("text", text="What")
        self.chk.column("#0", width=76, stretch=False)
        self.chk.column("where", width=170, stretch=False)
        self.chk.column("text", width=860)
        self.chk.tag_configure("ERROR", foreground="#b00")
        self.chk.tag_configure("WARN", foreground="#a60")
        self.chk.tag_configure("INFO", foreground="#557")
        self.chk.pack(fill="x")
        self.chk.bind("<Double-Button-1>", lambda e: self._go())
        bf = ttk.LabelFrame(f, text="The budget (after a build)", padding=4)
        bf.pack(fill="x", pady=4)
        bh = ttk.Frame(bf)
        bh.pack(anchor="w", fill="x")
        ttk.Label(bh, text="Rendition").pack(side="left")
        self.rendv = tk.StringVar(value="cga4")
        self.rendcb = ttk.Combobox(bh, textvariable=self.rendv,
                                   state="readonly", values=["cga4"], width=6)
        self.rendcb.pack(side="left", padx=4)
        self.rendcb.bind("<<ComboboxSelected>>", lambda e: self._rend())
        self.bud_l = ttk.Label(bf, text="Build to see it.", wraplength=1100)
        self.bud_l.pack(anchor="w")
        self.res = None
        self.tv = ttk.Treeview(bf, columns=[c for c, _, _ in self.COLS[1:]],
                               height=9)
        for i, (c, h, w) in enumerate(self.COLS):
            k = "#0" if i == 0 else c
            self.tv.heading(k, text=h)
            self.tv.column(k, width=w, stretch=False)
        self.tv.tag_configure("over", background="#f3c0c0")
        self.tv.tag_configure("near", background="#f6e2b0")
        self.tv.pack(fill="x")
        mf = ttk.Frame(bf)
        mf.pack(fill="x")
        self.mem_l = ttk.Label(mf, text="", font=("TkFixedFont", 9),
                               justify="left")
        self.mem_l.pack(side="left", anchor="n", pady=4)
        self.tv.bind("<<TreeviewSelect>>", lambda e: self._pal())
        pf = ttk.LabelFrame(mf, text="VGA's colours", padding=4)
        pf.pack(side="left", anchor="n", padx=12, pady=4)
        self.pal_l = ttk.Label(pf, text="Build a VGA rendition to see its "
                               "colours.", wraplength=16 * 33)
        self.pal_l.pack(anchor="w")
        self.pal_c = tk.Canvas(pf, height=96, width=16 * 36 + 4,
                               highlightthickness=0)
        self.pal_c.pack(anchor="w")
        df = ttk.LabelFrame(f, text="...and make a disk of it, with "
                            "VBATTLE.O88 beside it", padding=4)
        # (the disk and the game PACKED BEFORE the budget, at the bottom:
        # the packer gives space in packing order, and on a laptop's screen
        # the budget's lines pushed both buttons out of the window)
        df.pack(fill="x", pady=4, side="bottom", before=bf)
        self.diskv = tk.StringVar()
        ttk.Combobox(df, textvariable=self.diskv, values=M.DISK_LABELS,
                     state="readonly", width=46).pack(side="left")
        ttk.Button(df, text="Make the disk", command=self._disk
                   ).pack(side="left", padx=4)
        self.disk_l = ttk.Label(df, text="", wraplength=640)
        self.disk_l.pack(side="left", padx=8)
        self.disk_picked = False
        gf = ttk.LabelFrame(f, text="...or make it a GAME: one .O88 for each "
                            "adapter, the battle inside it - copy the file "
                            "and double-click it", padding=4)
        gf.pack(fill="x", pady=4, side="bottom", before=df)
        ttk.Button(gf, text="Make the games", command=self._game
                   ).pack(side="left")
        self.game_l = ttk.Label(gf, text="", wraplength=700)
        self.game_l.pack(side="left", padx=8)

    def _cap(self):
        """THE READER'S MODEL (126.4.1) on or off: a streamed clip's burst
        drawn in part where the disk would not have it in time"""
        on = bool(self.capv.get())
        if on != self.app.proj.d["target"].get("burst_cap", True):
            self.app.edit("set", ("target", "burst_cap"), on)

    def refresh(self):
        P_ = self.app.proj
        self.capv.set(1 if P_.d["target"].get("burst_cap", True) else 0)
        self.chk.delete(*self.chk.get_children())
        for k, x in enumerate(P_.check()):
            self.chk.insert("", "end", iid=str(k), text=x.level,
                            values=(x.where, x.text), tags=(x.level,))
        if not self.disk_picked:
            self.diskv.set(self.M.DISK_LABELS[self.M.DISK_OF_PROFILE.get(
                P_.d["target"]["profile"], 6)])

    def _go(self):
        s = self.chk.selection()
        if not s:
            return
        where = self.chk.item(s[0], "values")[0]
        tab = getattr(self.app, check_tab(where) + "_tab")
        self.app.nb.select(tab.f)
        if where.startswith("clips."):
            self.app.clips_tab.select(where.split(".")[1])

    def _build(self):
        def got(res):
            self.show(res)
            self.app.say("Built %s: %d bytes, verified" % (
                os.path.basename(res["out"]), res["bytes"]))
        self.app.run("os88vbt", ["build", self.app.proj.path, "--json"], got,
                     "Building...")

    def _rend(self):
        if self.res:
            self.show(self.res)

    def _pal(self):
        """THE PALETTE VIEWER (126.3.2): the battle's K colours - black, the
        base; the band's four, the menu's; the rest shared by every clip -
        and, with a clip picked in the budget, each one's share of that
        clip's pixels"""
        c = self.pal_c
        c.delete("all")
        rs = [r for r in (self.res or {}).get("renditions") or []
              if r["kind"] == "vga4"]
        if not rs or not os.path.exists(rs[0]["out"]):
            self.pal_l.config(text="Build a VGA rendition to see its "
                              "colours.")
            return
        k = self.app.proj.d["target"].get("vga_colours", 8)
        sel = self.tv.selection()
        clip = self.tv.index(sel[0]) if sel else None
        name = self.tv.item(sel[0], "text") if sel else None
        try:
            v = vga_colours(rs[0]["out"], k, clip)
        except (OSError, ValueError) as e:
            self.pal_l.config(text="Cannot read %s: %s" % (rs[0]["out"], e))
            return
        sl = v.get("slots", 0)
        self.pal_l.config(text=(
            "The battle's %d: %d shared by every clip (\"menu\" marks the "
            "band's four)%s. " % (len(v["cols"]), len(v["cols"]) - sl,
                                  ", and %d each clip's OWN, loaded as it "
                                  "starts%s" % (sl, " - %s's" % name if
                                                v.get("has_own") else
                                                " (the idle loop and the "
                                                "scenes use the shared "
                                                "alone)")
                                  if sl else "")) + (
            "Bars: each one's share of %s's pixels, all its frames." %
            (name or v["clip"]) if v["usage"] else
            "Click a clip above to see what it draws with."))
        top = max(v["usage"] or [0]) or 1.0
        for i, x in enumerate(v["cols"]):
            x0 = 2 + i * 36
            c.create_rectangle(x0, 2, x0 + 30, 24, outline="#000",
                               fill="#%02x%02x%02x" % x["rgb"])
            role = [r for r, on in (("base", i == 0 and x["rgb"] == (0, 0, 0)),
                                    ("menu %d" % (x["band"] or 0),
                                     x["band"] is not None),
                                    ("free" if x.get("free") else "own",
                                     x.get("own"))) if on]
            for j, r in enumerate(role):
                c.create_text(x0 + 15, 31 + 10 * j, text=r,
                              font=("TkDefaultFont", 7))
            if v["usage"]:
                u = v["usage"][i]
                h = max(1, int(round(u / top * 34)))
                c.create_rectangle(x0 + 6, 94 - h, x0 + 24, 94,
                                   outline="", fill="#557")
                c.create_text(x0 + 15, 94 - h - 6, text="%.0f%%" % (u * 100),
                              font=("TkDefaultFont", 7))

    def show(self, res):
        self.res = res
        rs = res.get("renditions") or [dict(kind="cga4", out=res["out"],
                                            bytes=res["bytes"],
                                            budget=res["budget"])]
        self.rendcb.config(values=[r["kind"] for r in rs])
        pick = [r for r in rs if r["kind"] == self.rendv.get()] or rs[:1]
        self.rendv.set(pick[0]["kind"])
        res = dict(res, out=pick[0]["out"], bytes=pick[0]["bytes"],
                   budget=pick[0]["budget"], capped=pick[0].get("capped") or {})
        bud = self.bud = res["budget"]
        self.out_l.config(text="%s - %d KB" % (res["out"], res["bytes"] // 1024))
        self.tv.delete(*self.tv.get_children())
        for vals, tag in budget_rows(bud):
            self.tv.insert("", "end", text=vals[0], values=vals[1:],
                           tags=(tag,) if tag else ())
        self.bud_l.config(text="Against %s's disk, %d KB/s. The music %s. "
                          "\"Of the disk\" is the clip and the music together; "
                          "red is more than the disk gives, so it will stall. "
                          "The head is what is read before a clip may start." % (
                              self.app.proj.d["target"]["profile"],
                              bud["disk"] // 1024,
                              "is resident" if bud["memory"]["music_resident"]
                              else "streams beside it, %.1f KB/s" %
                              (bud["music_rate"] / 1024)) + (
            "\nHeld to the disk where they ran faster than it (a frame drawn "
            "in part, the rest on the frames after; the sound whole): %s." %
            ", ".join("%s %d frames" % kv for kv in sorted(
                res["capped"].items())) if res["capped"] else ""))
        hl = []
        for pool, hh in sorted(bud.get("hold", {}).items(), key=lambda x: int(x[0])):
            hl.append("With %d MB of XMS: %s" % (int(pool) // 1024,
                      "the whole bundle held - every read a copy" if hh["whole"]
                      else "%d KB held, the hold order's first (the music, the "
                      "loops, every action's start...) of its %d" % (
                          hh["kb"], hh["order_kb"])))
        self.mem_l.config(text="\n".join(memory_lines(bud["memory"]) + hl + [
            "(on a 640 KB machine: %d KB free to the engine, measured on "
            "MartyPC's XT; 86Box's 5150 left about 20 KB more)" %
            (bud["memory"]["free"] // 1024)]))
        self._pal()

    def _disk(self):
        self.disk_picked = True
        n = self.M.DISK_LABELS.index(self.diskv.get())

        def got(res):
            self.disk_l.config(text="%s%s" % (res["out"], "\n" + res["note"]
                                              if res.get("note") else ""))
            self.app.say("Made %s" % os.path.basename(res["out"]))
        argv = ["disk", self.app.proj.path, "--disk", str(n), "--json"]
        if self.res:                    # the rendition chosen above
            argv += ["--rendition", self.rendv.get()]
        self.app.run("os88vbt", argv, got, "Making the disk...")


    def _game(self):
        """Each rendition built beside the project, inside its own copy of
        the engine (os88vbt.py game): NAME.O88 for CGA, NAMEV.O88 for VGA,
        NAMEH.O88 for Hercules - built first if they are not"""
        def got(res):
            outs = res.get("out") or []
            self.game_l.config(text="\n".join(
                "%s (%d KB)" % (o, os.path.getsize(o) // 1024) for o in outs))
            self.app.say("Made %s" % ", ".join(os.path.basename(o)
                                               for o in outs))
        self.app.run("os88vbt", ["game", self.app.proj.path, "--json"], got,
                     "Making the games...")


# --- small form pieces ------------------------------------------------------------------

class Scroll(object):
    """A TAB THAT SCROLLS: OUTER is the notebook's page (the tab's `f`, so
    nb.select(tab.f) and a drop's "inside this tab" test hold), and the
    tab's content goes in INNER, a frame on a Canvas inside it. INNER is
    never smaller than the view - a tab that fits fills it, as before -
    and the scroll bars come only when it does not. The canvas ASKS for
    the content's size, so the window still opens as tall as its tallest
    tab where the screen allows (App._fit). The Battle tab is 900 px and
    Title & endings 850: on a 1366x768 screen, without this, their lower
    halves could not be reached at all.

    The wheel scrolls the tab under the pointer (App._wheel: X11's
    buttons 4 and 5, Windows' and the Mac's <MouseWheel>, Shift for
    across) unless a list, a tree or a text under it scrolls itself; a
    field reached by the keyboard is scrolled into view (App._focus)"""

    def __init__(self, app, parent, padding=0):
        tk, ttk = app.tk, app.ttk
        self.outer = ttk.Frame(parent)
        self.cv = tk.Canvas(self.outer, highlightthickness=0, bd=0)
        self.vs = ttk.Scrollbar(self.outer, orient="vertical",
                                command=self.cv.yview)
        self.hs = ttk.Scrollbar(self.outer, orient="horizontal",
                                command=self.cv.xview)
        self.cv.configure(yscrollcommand=self.vs.set,
                          xscrollcommand=self.hs.set)
        self.cv.grid(row=0, column=0, sticky="nsew")
        self.outer.rowconfigure(0, weight=1)
        self.outer.columnconfigure(0, weight=1)
        self.inner = ttk.Frame(self.cv, padding=padding)
        self.win = self.cv.create_window(0, 0, window=self.inner,
                                         anchor="nw")
        self.inner.bind("<Configure>", lambda e: self._ask())
        self.cv.bind("<Configure>", lambda e: self._lay())
        app.scrolls[str(self.cv)] = self

    def _ask(self):
        """The content's size changed: the canvas asks for it"""
        w, h = self.inner.winfo_reqwidth(), self.inner.winfo_reqheight()
        if (int(self.cv.cget("width")), int(self.cv.cget("height"))) != \
                (w, h):
            self.cv.configure(width=w, height=h)
        self._lay()

    def _lay(self):
        """The view's size or the content's: the content at least the
        view's size, the region all of it, a bar where it is wanted"""
        vw, vh = self.cv.winfo_width(), self.cv.winfo_height()
        if vw <= 1 or vh <= 1:          # (not mapped yet)
            return
        rw, rh = self.inner.winfo_reqwidth(), self.inner.winfo_reqheight()
        self.cv.itemconfigure(self.win, width=max(vw, rw),
                              height=max(vh, rh))
        self.cv.configure(scrollregion=(0, 0, max(vw, rw), max(vh, rh)))
        for bar, need, grid in ((self.vs, rh > vh, dict(row=0, column=1)),
                                (self.hs, rw > vw, dict(row=1, column=0))):
            if need and not bar.winfo_ismapped():
                bar.grid(sticky="ns" if bar is self.vs else "ew", **grid)
            elif not need and bar.winfo_ismapped():
                bar.grid_remove()
        if rh <= vh:
            self.cv.yview_moveto(0)
        if rw <= vw:
            self.cv.xview_moveto(0)

    def scrolls(self, across=False):
        """Is there anything to scroll, down (or ACROSS)?"""
        lo, hi = (self.cv.xview() if across else self.cv.yview())
        return lo > 0.0 or hi < 1.0

    def see(self, w):
        """Widget W, somewhere in INNER, scrolled into the view"""
        try:
            y = w.winfo_rooty() - self.inner.winfo_rooty()
            x = w.winfo_rootx() - self.inner.winfo_rootx()
            spans = ((y, w.winfo_height(), self.cv.winfo_height(),
                      self.inner.winfo_height(), self.cv.yview,
                      self.cv.yview_moveto),
                     (x, w.winfo_width(), self.cv.winfo_width(),
                      self.inner.winfo_width(), self.cv.xview,
                      self.cv.xview_moveto))
        except Exception:               # noqa: BLE001 (a TclError)
            return                      # (a widget gone meanwhile)
        for pos, size, view, total, where, move in spans:
            if total <= view or total <= 0 or size >= view:
                continue                # (one taller than the view: left)
            top = where()[0] * total
            if pos < top:
                move(max(0, pos - 8) / float(total))
            elif pos + size > top + view:
                move(min(total - view, pos + size + 8 - view) / float(total))


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
        self.scroll = Scroll(app, parent, 6)    # (the page scrolls)
        self.f, f = self.scroll.outer, self.scroll.inner
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
                  "them alone - or, aimed at the enemy, when they are the one "
                  "doing it; else the general)").pack(side="left")
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
        ttk.Label(parent, text="   " + VERB_HELP.get(e["verb"], ""),
                  foreground="grey35").pack(anchor="w")
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

UI_SOUND_LABELS = (
    ("move", "The cursor moves (a menu's item, or the target)"),
    ("open", "A sub-menu opens, or the target is to be chosen"),
    ("close", "Back out of a sub-menu or a target (Esc)"),
    ("take", "An action is taken"),
    ("deny", "Refused: it cannot be paid for, or has no one to aim at"))


def db(level):
    """A linear level in dB, for the window"""
    import math
    return 20 * math.log10(level) if level > 0 else -99.0


class SoundsTab(object):
    """EVERY SOUND'S LEVEL (SPEC.md 126.3): each sound in use, what plays it
    and how loud it is AS HEARD - the music and an effect at half scale, an
    opening's or a loop's own sound at full - its level in dB to set, and
    the AUTO-LEVEL suggestion (vbtana.levels): each as loud as the music,
    never past its peak"""

    COLS = (("plays", "Plays as", 70), ("uses", "Used by", 260),
            ("loud", "Loudness", 80), ("peak", "Peak", 72),
            ("level", "Level", 70), ("suggest", "Suggested", 80))

    def __init__(self, app, parent):
        tk, ttk = app.tk, app.ttk
        self.app = app
        self.scroll = Scroll(app, parent, 8)    # (the page scrolls)
        self.f, f = self.scroll.outer, self.scroll.inner
        self.res = None             # the last measurement (vbtana levels)
        ttk.Label(f, text="Every sound the battle plays, as loud as it will "
                  "be heard. The music and the effects are kept at half "
                  "scale so that they add without clipping; an opening's or "
                  "a loop's own sound plays instead of the music at full "
                  "scale - the same file 6 dB louder there.",
                  wraplength=900).pack(anchor="w")
        self.tv = ttk.Treeview(f, columns=[c[0] for c in self.COLS], height=14)
        self.tv.heading("#0", text="Sound")
        self.tv.column("#0", width=140)
        self.tv.column("#0", stretch=False)
        for k, t, w in self.COLS:
            self.tv.heading(k, text=t)
            self.tv.column(k, width=w, anchor="w", stretch=False)
        self.tv.pack(anchor="w", pady=6)
        self.tv.bind("<<TreeviewSelect>>", lambda e: self._pick())
        row = ttk.Frame(f)
        row.pack(anchor="w")
        ttk.Label(row, text="Level of the sound chosen (dB):").pack(side="left")
        self.lv = tk.StringVar()
        e = ttk.Entry(row, textvariable=self.lv, width=7)
        e.pack(side="left", padx=4)
        e.bind("<Return>", lambda ev: self._set())
        ttk.Button(row, text="Set", command=self._set).pack(side="left")
        ttk.Button(row, text="-3 dB", command=lambda: self._nudge(-3)
                   ).pack(side="left", padx=(8, 0))
        ttk.Button(row, text="+3 dB", command=lambda: self._nudge(3)
                   ).pack(side="left")
        ttk.Button(row, text="Listen", command=self._listen
                   ).pack(side="left", padx=8)
        ttk.Button(row, text="Replace file...", command=self._refile
                   ).pack(side="left")
        ttk.Button(row, text="Remove", command=self._remove
                   ).pack(side="left", padx=4)
        row2 = ttk.Frame(f)
        row2.pack(anchor="w", pady=6)
        ttk.Button(row2, text="Measure and suggest levels",
                   command=self._measure).pack(side="left")
        ttk.Button(row2, text="Apply the suggestions",
                   command=self._apply).pack(side="left", padx=4)
        ttk.Button(row2, text="Apply to the chosen sound",
                   command=lambda: self._apply(one=True)).pack(side="left")
        self.note = ttk.Label(f, text="", wraplength=900)
        self.note.pack(anchor="w")
        # THE MENU'S SOUNDS (126.5): the engine plays each at its moment
        mf = ttk.LabelFrame(f, text="Menu sounds (each an effect over the "
                            "music; blank: silent)", padding=6)
        mf.pack(anchor="w", fill="x", pady=8)
        self.uiv = {}
        for r, (k, label) in enumerate(UI_SOUND_LABELS):
            ttk.Label(mf, text=label).grid(row=r, column=0, sticky="w")
            v = self.uiv[k] = tk.StringVar()
            cb = ttk.Combobox(mf, textvariable=v, state="readonly", width=20)
            cb.grid(row=r, column=1, sticky="w", padx=4, pady=1)
            cb.bind("<<ComboboxSelected>>", lambda e, k=k: self._ui_set(k))
            ttk.Button(mf, text="Add a file...", command=lambda k=k:
                       self._ui_file(k)).grid(row=r, column=2, padx=2)
            ttk.Button(mf, text="Listen", command=lambda k=k:
                       self._ui_listen(k)).grid(row=r, column=3, padx=2)
            self.uiv[k + "_cb"] = cb

    def _ui_set(self, k):
        ui = dict(self.app.proj.d.get("ui_sounds", {}))
        v = self.uiv[k].get()
        if v in ("", "(none)"):
            ui.pop(k, None)
        else:
            ui[k] = v
        if ui != self.app.proj.d.get("ui_sounds", {}):
            self.app.edit("set", ("ui_sounds",), ui)

    def _ui_file(self, k):
        from tkinter import filedialog
        p = filedialog.askopenfilename(title="A menu sound", filetypes=[
            ("Sound", "*.wav *.mp3 *.ogg *.flac *.aif *.aiff"),
            ("Everything", "*")])
        if p:
            sid = self.app.add_sound_file(p)
            self.uiv[k].set(sid)
            self._ui_set(k)

    def _ui_listen(self, k):
        sid = self.app.proj.d.get("ui_sounds", {}).get(k)
        if sid:
            self.app.listen(sid)
        else:
            self.app.say("No sound chosen for it")

    def sid(self):
        s = self.tv.selection()
        return s[0] if s and s[0] in self.app.proj.d["sounds"] else None

    def refresh(self):
        d = self.app.proj.d
        for k, _ in UI_SOUND_LABELS:
            self.uiv[k + "_cb"].config(values=["(none)"] + list(d["sounds"]))
            self.uiv[k].set(d.get("ui_sounds", {}).get(k) or "(none)")
        keep = self.sid()
        self.tv.delete(*self.tv.get_children())
        meas = {r["sound"]: r for r in (self.res or {}).get("sounds", [])}
        import vbtana as A
        used = self.app.proj.sounds_used()
        for sid in sorted(d["sounds"], key=lambda k: (k not in used, k)):
            lvl = d["sounds"][sid].get("level", 1.0)
            r = meas.get(sid)
            fresh = r is not None and abs(r["level"] - lvl) < 1e-9
            if r is not None and not fresh:     # a level changed since the
                shift = db(lvl) - db(r["level"])    # measurement: its
                r = dict(r, loud=None if r["loud"] is None else  # figures
                         r["loud"] + shift, peak=None if r["peak"] is None
                         else r["peak"] + shift, level=lvl)   # move with it
            f1 = lambda v: "-" if v is None else "%.1f dB" % v
            gone = self.app.proj.sound_missing(sid)
            self.tv.insert("", "end", iid=sid, text=sid, values=(
                "FILE MISSING" if gone else
                A.plays_as(self.app.proj, sid) or "",
                ", ".join(self.app.proj.sound_users(sid))[:60] or
                "nothing (not built)",
                f1(r["loud"]) if r else "", f1(r["peak"]) if r else "",
                "%+.1f dB" % db(lvl),
                "%+.1f dB" % db(r["suggest"]) if r and fresh else ""))
        if keep in d["sounds"]:
            self.tv.selection_set(keep)
        if self.res and self.res.get("target") is not None:
            self.note.config(text="Matched to %.1f dB, %s. A suggestion never "
                             "takes a sound past its peak; an effect laid over "
                             "loud action may still want a little more." % (
                                 self.res["target"], "the music's loudness"
                                 if d.get("music") else "the sounds' median"))

    def _remove(self):
        """The chosen sound out of the project - and, when something uses
        it, out of each of those once that is confirmed (a menu sound goes
        silent, an opening's or a loop's plays the music)"""
        from tkinter import messagebox
        sid = self.sid()
        if not sid:
            return self.app.say("Choose the sound to remove")
        users = self.app.proj.sound_users(sid)
        if not messagebox.askyesno(TITLE, "Remove the sound %s?%s" % (
                sid, "\n\nIt is used by %s, and is taken out of those too."
                % ", ".join(users) if users else "")):
            return
        self.app.edit("remove_sound", sid, force=True)
        self.app.say("%s removed" % sid)

    def _refile(self):
        """The chosen sound read from another file - its own moved or
        renamed - everything that uses it kept"""
        from tkinter import filedialog
        sid = self.sid()
        if not sid:
            return self.app.say("Choose the sound whose file to replace")
        p = filedialog.askopenfilename(title="The file for %s" % sid,
                                       filetypes=[("Sound", "*.wav *.mp3 "
                                                   "*.ogg *.flac *.aif *.aiff"),
                                                  ("Everything", "*")])
        if p:
            self.app.edit("set_sound_file", sid, p)

    def _pick(self):
        sid = self.sid()
        if sid:
            self.lv.set("%.1f" % db(self.app.proj.d["sounds"][sid]
                                    .get("level", 1.0)))

    def _level(self, sid, lin):
        lin = max(0.0, min(8.0, lin))
        self.app.edit("set", ("sounds", sid, "level"), round(lin, 4))

    def _set(self):
        sid = self.sid()
        if not sid:
            return self.app.say("Choose a sound")
        try:
            v = float(self.lv.get())
        except ValueError:
            return self.app.say("A level in dB, as -6 or 3.5")
        self._level(sid, 10 ** (v / 20.0))

    def _nudge(self, by):
        sid = self.sid()
        if sid:
            self._level(sid, self.app.proj.d["sounds"][sid].get("level", 1.0)
                        * 10 ** (by / 20.0))
            self._pick()

    def _listen(self):
        sid = self.sid()
        if sid:
            self.app.listen(sid)

    def _measure(self):
        def got(res):
            self.res = res
            self.refresh()
        self.app.run("vbtana", ["levels", self.app.proj.path], got,
                     "Measuring every sound...")

    def _apply(self, one=False):
        if not self.res:
            return self.app.say("Measure first")
        d = self.app.proj.d
        want = {r["sound"]: r["suggest"] for r in self.res["sounds"]
                if r["sound"] in d["sounds"]
                and abs(r["level"] - d["sounds"][r["sound"]].get("level", 1.0))
                < 1e-9}
        if one:
            sid = self.sid()
            want = {sid: want[sid]} if sid in want else {}
        if not want:
            return self.app.say("Measure again: the levels have changed")

        def f(d):
            for sid, v in want.items():
                d["sounds"][sid]["level"] = v
        self.app.proj._edit(f)
        self.res = None             # (measured at the old levels)
        self.app.refresh()
        self.app.say("Levels set for %d sound(s)" % len(want))


class PartyTab(object):
    """The characters (1 to 4): their stats and their menus - the top menu,
    its sub-menus, the side menu - each menu's look; the band's pictures and
    the party's items; and the band as the engine will draw it"""

    def __init__(self, app, parent):
        tk, ttk = app.tk, app.ttk
        self.app = app
        self.scroll = Scroll(app, parent, 6)    # (the page scrolls)
        self.f, f = self.scroll.outer, self.scroll.inner
        left = ttk.Frame(f)
        left.pack(side="left", fill="y")
        self.lb = tk.Listbox(left, height=5, width=20, exportselection=False)
        self.lb.pack()
        self.lb.bind("<<ListboxSelect>>", lambda e: self.show())
        bb = ttk.Frame(left)
        bb.pack(fill="x")
        ttk.Button(bb, text="Add", command=self._add).pack(side="left")
        ttk.Button(bb, text="Remove", command=self._remove).pack(side="left")
        bm = ttk.Frame(left)
        bm.pack(fill="x")
        ttk.Button(bm, text="Up", command=lambda: self._move(-1)
                   ).pack(side="left")
        ttk.Button(bm, text="Down", command=lambda: self._move(1)
                   ).pack(side="left")
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

    def _move(self, by):
        """The selected character a place up or down the party - the turns'
        order and the band's lines - and still selected after"""
        i = self.ci()
        if i is None:
            return
        j = max(0, min(len(self.app.proj.d["characters"]) - 1, i + by))
        if j == i:
            return
        self.app.edit("move_character", i, by)
        self.lb.selection_clear(0, "end")
        self.lb.selection_set(j)
        self.show()

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
        # THE ITEM CHOSEN, placed of its own (126.5.1): blank, the menu's
        # rows from its look
        it = self._item_of_row()
        if it is not None:
            pl = (lk.get("places") or [])
            pl = pl[it] if it < len(pl) else None
            lform.entry("This item's column", "" if not pl else pl[0],
                        lambda v, it=it: self._place(key, it, 0, v), width=5)
            lform.entry("This item's row", "" if not pl else pl[1],
                        lambda v, it=it: self._place(key, it, 1, v), width=5)
        # the band as the engine draws it
        pv = ttk.Frame(lf)
        pv.pack(side="left", padx=8)
        ttk.Label(pv, text="The band, as the engine draws it (the letters' "
                  "shapes are the tree's 8x8 face; the machine's are its "
                  "ROM's):").pack(anchor="w")
        self.band = tk.Label(pv, bg="black")
        self.band.pack(anchor="w")
        self._preview(ci, where, sub)
        self._layout()

    # THE BAND'S LAYOUT (126.5.1): every part placed, and shown or not
    LAYOUT_ROWS = (("The enemy's name", "enemy_col", "enemy_row", "enemy_show"),
                   ("The party: first row, pitch", None, "party_row", None),
                   ("A character's name", "name_col", None, "name_show"),
                   ("The limit star", "limit_col", None, "limit_show"),
                   ("HP", "hp_col", None, "hp_show"),
                   ("MP (and the menus' costs)", "mp_col", None, "mp_show"),
                   ("The message line", "msg_col", "msg_row", "msg_show"),
                   ("The plain look's lines; divider", "divider_col", None,
                    "lines_show"),
                   ("An item's count beside an action it costs", None, None,
                    "item_count_show"))

    def _layout(self):
        tk, ttk = self.app.tk, self.app.ttk
        import vbtrules as V
        lay = V.layout_of(self.app.proj)
        own = self.app.proj.d.get("band_layout") or {}
        lf = ttk.LabelFrame(self.body, text="The band's layout (every battle's "
                            "band: columns in cells 0-39, rows in pixels from "
                            "the band's top; blank: as it always was)",
                            padding=4)
        lf.pack(fill="x", pady=4)
        g = ttk.Frame(lf)
        g.pack(anchor="w")
        for c, t in enumerate(("", "Column", "Row", "Shown")):
            ttk.Label(g, text=t).grid(row=0, column=c, sticky="w", padx=4)
        for r, (label, ck, rk, sk) in enumerate(self.LAYOUT_ROWS, 1):
            ttk.Label(g, text=label).grid(row=r, column=0, sticky="w")
            for c, k in ((1, ck), (2, rk)):
                if label.startswith("The party"):
                    k = ("party_row", "party_pitch")[c - 1]
                if not k:
                    continue
                v = tk.StringVar(value=str(own[k]) if k in own else "")
                e = ttk.Entry(g, textvariable=v, width=5)
                e.grid(row=r, column=c, sticky="w", padx=4)
                e.bind("<Return>", lambda ev, k=k, v=v: self._lay(k, v.get()))
                e.bind("<FocusOut>", lambda ev, k=k, v=v: self._lay(k, v.get()))
                ttk.Label(g, text="(%d)" % lay[k], foreground="grey40"
                          ).grid(row=r, column=c, sticky="e")
            if sk:
                b = tk.IntVar(value=lay[sk])
                ttk.Checkbutton(g, variable=b, command=lambda sk=sk, b=b:
                                self._lay(sk, b.get())).grid(row=r, column=3)
            if ck == "hp_col":
                b2 = tk.IntVar(value=lay["hpmax_show"])
                ttk.Checkbutton(g, text="with its most (/max)", variable=b2,
                                command=lambda b2=b2: self._lay(
                                    "hpmax_show", b2.get())).grid(
                                        row=r, column=4, sticky="w")

    def _lay(self, k, v):
        own = dict(self.app.proj.d.get("band_layout") or {})
        v = str(v).strip()
        if v == "":
            own.pop(k, None)
        else:
            try:
                own[k] = int(v)
            except ValueError:
                return self.app.say("A number, or blank for the default")
        if own != (self.app.proj.d.get("band_layout") or {}):
            self.app.edit("set", ("band_layout",), own or None)

    def _item_of_row(self):
        """The selected row's place in its own menu, or None"""
        if self.mrow is None or self.mrow >= len(self.rows):
            return None
        where, i, sub, text = self.rows[self.mrow]
        if where == "top" and text.endswith(">"):
            return None             # (a sub-menu's own row: its look's)
        return i

    def _place(self, key, it, k, v):
        looks = dict(self.app.proj.d.get("menu_looks", {}))
        lk = dict(looks.get(key, {}))
        pl = list(lk.get("places") or [])
        while len(pl) <= it:
            pl.append(None)
        cur = list(pl[it]) if pl[it] else [None, None]
        v = str(v).strip()
        try:
            cur[k] = int(v) if v else None
        except ValueError:
            return self.app.say("A number, or blank")
        if cur[0] is None and cur[1] is None:
            pl[it] = None
        else:
            if cur[0] is None:
                cur[0] = lk.get("col", 1)
            if cur[1] is None:
                cur[1] = lk.get("row", 4) + it * lk.get("pitch", 9)
            pl[it] = cur
        while pl and pl[-1] is None:
            pl.pop()
        if pl:
            lk["places"] = pl
        else:
            lk.pop("places", None)
        looks[key] = lk
        self.app.edit("set", ("menu_looks",), looks)

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
            self.band.config(text="The band cannot be drawn yet: %s" % e,
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
        ims = d.get("images", [])
        self.imlb = tk.Listbox(pf, height=4, width=24, exportselection=False)
        self.imlb.pack(anchor="w")
        for im in ims:
            self.imlb.insert("end", "%s at %d,%d" % (
                im["name"], im.get("col", 0), im.get("row", 0)))
        sel = getattr(self, "imsel", None)
        if sel is not None and sel < len(ims):
            self.imlb.selection_set(sel)
        self.imlb.bind("<<ListboxSelect>>", lambda e: self._pick_image())
        ib = ttk.Frame(pf)
        ib.pack(anchor="w")
        ttk.Button(ib, text="Add a picture...", command=self._add_image
                   ).pack(side="left")
        if sel is not None and sel < len(ims):
            # THE PICTURE CHOSEN, edited in place: where it sits, how much
            # of it, how it is made - the band preview follows each change
            im = ims[sel]
            ttk.Button(ib, text="Remove", command=self._rm_image
                       ).pack(side="left")
            ef = Rows(self.app, pf)
            ef.f.pack(anchor="w", pady=2)
            ef.entry("Name", im["name"], lambda v: self._rename_image(sel, v),
                     width=12)
            ef.entry("Column (cells, 0-39)", im.get("col", 0),
                     lambda v: self._im_set(sel, "col", as_int(v)), width=5)
            ef.entry("Row in the band", im.get("row", 0),
                     lambda v: self._im_set(sel, "row", as_int(v)), width=5)
            ef.entry("Width (cells; blank: its own)", im.get("cells") or "",
                     lambda v: self._im_set(sel, "cells",
                                            as_int(v) or None), width=5)
            ef.entry("Height (rows; blank: its own)", im.get("rows") or "",
                     lambda v: self._im_set(sel, "rows",
                                            as_int(v) or None), width=5)
            ef.choice("Dithered", "yes" if im.get("dither", True) else "no",
                      ("yes", "no"),
                      lambda v: self._im_set(sel, "dither", v == "yes"),
                      width=5)
            ttk.Label(pf, text=os.path.basename(im.get("path", "")),
                      wraplength=220).pack(anchor="w")
            ttk.Button(pf, text="Replace its file...",
                       command=lambda: self._replace_image(sel)
                       ).pack(anchor="w")
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

    def _pick_image(self):
        s = self.imlb.curselection()
        self.imsel = s[0] if s else None
        self.show()

    def _im_set(self, i, field, value):
        imgs = [dict(x) for x in self.app.proj.d.get("images", [])]
        if value is None:
            imgs[i].pop(field, None)
        else:
            imgs[i][field] = value
        self.app.edit("set", ("images",), imgs)

    def _rename_image(self, i, name):
        """A picture renamed, and every place that names it with it: the
        band's own and each menu's look"""
        d = self.app.proj.d
        name = "".join(ch for ch in name.strip() if ch.isalnum() or
                       ch in "_-")[:12]
        old = d["images"][i]["name"]
        if not name or name == old:
            return
        if any(x["name"] == name for x in d["images"]):
            return self.app.say("There is a picture called %s already" % name)

        def f(d):
            d["images"][i]["name"] = name
            if d.get("band_image") == old:
                d["band_image"] = name
            for lk in d.get("menu_looks", {}).values():
                if lk.get("image") == old:
                    lk["image"] = name
        self.app.proj._edit(f)
        self.app.refresh()

    def _rm_image(self):
        i = self.imsel
        d = self.app.proj.d
        if i is None or i >= len(d.get("images", [])):
            return
        old = d["images"][i]["name"]

        def f(d):
            del d["images"][i]
            if d.get("band_image") == old:
                d["band_image"] = None
            for lk in d.get("menu_looks", {}).values():
                if lk.get("image") == old:
                    lk.pop("image", None)
        self.imsel = None
        self.app.proj._edit(f)
        self.app.refresh()

    def _replace_image(self, i):
        from tkinter import filedialog
        p = filedialog.askopenfilename(title="The picture's new file")
        if p:
            self._im_set(i, "path", p)

    def _add_item(self):
        from tkinter import simpledialog
        name = simpledialog.askstring(TITLE, "The item's name:", parent=self.f)
        if name:
            items = list(self.app.proj.d.get("items", [])) + [{"name": name[:8], "count": 1}]
            self.app.edit("set", ("items",), items)


# --- the Enemy tab (11.1, 11.2.1, 11.2.2) -------------------------------------------------

class EnemyTab(object):
    """The enemy: its stats and its STANCES (the battle's endings are the
    Battle tab's) -
    each with its modifiers, its weighted moves and their target rules, its
    responses, its automatic change and its return"""

    def __init__(self, app, parent):
        tk, ttk = app.tk, app.ttk
        self.app = app
        self.scroll = Scroll(app, parent, 6)    # (the page scrolls)
        self.f, f = self.scroll.outer, self.scroll.inner
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
        # FOLLOW-UPS (126.6.7): the enemy's, in every stance - "if it did X
        # last turn, it must do Y this turn, at the same target"
        ff = ttk.LabelFrame(self.body, text="Follow-ups (in every stance): "
                            "after the enemy's turn plays one action, its "
                            "next turn plays another - before any move, "
                            "change or return", padding=4)
        ff.pack(fill="x", pady=4)
        fol = P_.d["enemy"].get("follow_ups", [])
        for i, x in enumerate(fol):
            row = ttk.Frame(ff)
            row.pack(fill="x")
            ttk.Label(row, text="after").pack(side="left")
            self._combo(row, x.get("after") or "", acts,
                        lambda v, i=i: self._fol(i, after=v))
            ttk.Label(row, text="next turn").pack(side="left", padx=(6, 2))
            self._combo(row, x.get("then") or "", acts,
                        lambda v, i=i: self._fol(i, then=v))
            ttk.Label(row, text="at").pack(side="left", padx=(6, 2))
            self._combo(row, x.get("target", "same"),
                        ("same",) + tuple(P.RULES),
                        lambda v, i=i: self._fol(i, target=v))
            if x.get("target") == "named":
                self._combo(row, x.get("named", chars[0] if chars else ""),
                            chars, lambda v, i=i: self._fol(i, named=v))
            ttk.Button(row, text="Remove", command=lambda i=i: self.app.edit(
                "set", ("enemy", "follow_ups"), fol[:i] + fol[i + 1:])
                       ).pack(side="right")
        if acts:
            ttk.Button(ff, text="Add a follow-up", command=lambda: self.app.edit(
                "set", ("enemy", "follow_ups"), fol + [
                    {"after": acts[0], "then": acts[0], "target": "same"}])
                       ).pack(anchor="w")
        ttk.Label(ff, text="\"same\": the character the first action was "
                  "aimed at, while they stand (else the next in turn).",
                  wraplength=700).pack(anchor="w")

    def _fol(self, i, **kw):
        fol = [dict(x) for x in self.app.proj.d["enemy"].get("follow_ups", [])]
        fol[i].update(kw)
        if fol[i].get("target") == "named" and not fol[i].get("named"):
            fol[i]["named"] = self._named_default()
        self.app.edit("set", ("enemy", "follow_ups"), fol)

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

    def _named_default(self):
        ch = [c["name"] for c in self.app.proj.d["characters"]]
        return ch[0] if ch else None

    def _move(self, base, i, **kw):
        moves = [dict(m) for m in self.app.proj.get(*base)["moves"]]
        moves[i].update(kw)
        if moves[i].get("rule") == "named" and moves[i].get("target") is None:
            moves[i]["target"] = self._named_default()  # (what the box shows,
                                                        # stored: it was not)
        self.app.edit("set", base + ("moves",), moves)

    def _resp(self, base, i, **kw):
        rs = [dict(r) for r in self.app.proj.get(*base).get("responses", [])]
        rs[i].update(kw)
        if rs[i].get("trigger") == "named" and not rs[i].get("named"):
            acts = list(self.app.proj.d["actions"])
            rs[i]["named"] = acts[0] if acts else None
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
