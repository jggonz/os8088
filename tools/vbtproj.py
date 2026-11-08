#!/usr/bin/env python3
"""vbtproj - a Video Battle PROJECT (docs/plans/completed/VBATTLE-BUILDER-PLAN.md 2).

The project is the product; the builder's window and the command line are
two faces of it. This module is the model: what a battle is made of, loaded
from and saved to a JSON file, checked, and edited only through methods, so
that a script or a test reaches everything the window does. It needs no Tk
and does no analysis (tools/vbtana.py) and no building (tools/os88vbt.py
build).

A project file (version 1):

    {"format": "vbtproj", "version": 1, "name": ...,
     "target":   {"profile", "palette" (the CGA4 byte), "fps", "rate",
                  "canvas": [x, y, w, h], "band": [cell, row, cells, rows],
                  "burst_cap": false to build a streamed clip's bursts
                  as they are (SPEC.md 126.4.3; true when absent),
                  "vga_bands": VGA4's writes in bands of this many rows,
                  0 the whole frame (SPEC.md 126.3.2; 16 when absent),
                  "vga_band_cga": true to keep the band's four CGA colours
                  among VGA4's (false when absent: the nearest four of the
                  footage's),
                  "vga_clip_colours": how many of VGA4's colours are each
                  clip's own, swapped in as it starts (SPEC.md 126.3.2;
                  absent: 4 of 16, 1 of 8, none of 4),
                  "xms_kb": the XMS the battle may count on (SPEC.md
                  126.4.6: the clip cache; 0 or absent none),
                  "cadence": false to read a CAPTURE (a source at a whole
                  multiple of the target's rate) by time, not by the
                  frames its footage had (true when absent: SPEC.md
                  126.2.5)},
     "sources":  {id: {"kind": "video", "path"} | {"kind": "scene", "name"}},
     "sounds":   {id: {"kind": "file", "path", "level"}
                    | {"kind": "scene", "name", "level"}
                    | {"kind": "source", "source", "in", "out", "level",
                       "whole"}} - "whole": a clip's own soundtrack over all
                       of it, which follows its in and out (else a part,
                       which stays on its footage),
     "music":    null (none: silence, a second of it played round) |
                 {"sound", "join", "loop"} - the sample it comes in at, and
                 the loop [start, end) in the sound's samples (null: all
                 of it); the join is inside the loop,
     "ui_sounds":{"move"|"open"|"close"|"take"|"deny": sound id},
     "idle":     {"source", "loop": [a, b]} - source frames a..b-1, looped,
     "clips":    {id: {"source", "in", "out", "anchors": [k...], "exit",
                       "joins", "ends", "cues": [{"frame", "sound",
                       "level", "until"?}], "palette"?: "auto" (absent) |
                       "own" | "shared" - on VGA every colour its own,
                       loaded over black (SPEC.md 126.3.3)}} - a cue with
                       "until" stops at that frame of the clip,
     "openings": [{"clip", "sound", "full"?}] - in turn; the last joins the
                 loop; "full" (absent: false) plays it over the WHOLE screen,
                 the menu not yet there - the full ones first, the last with
                 the menu (it joins the idle loop),
                 "seam"?: how its own sound joins the one before it -
                 "auto" (absent), "trim" (silent black head frames dropped),
                 "shift" (a sub-frame silence closed, the sound earlier),
                 "none" (os88vbt.seam_join),
     "title":    null | LOOP + {"leave": null (a fade) | a clip id, "anchors":
                 [k...] (the title's frames the leaving clip is entered at)}
                 - SPEC.md 126.3.1: before the openings, round until a key,
     "endings":  {"victory"|"defeat": {"how": "hold" | "fade" (to its loop)
                 | "join" (its clip seams onto its loop's frame 0) |
                 "again" (fade, and play again), "loop": LOOP | null,
                 "full"?: true (the end clip over the whole screen),
                 "plays"?: "once" (not round), "then"?: "fade" | "join"
                 (after one that plays once: to the title, the play
                 again; absent: hold), "line"?: the band's line at the
                 end, 38 characters at most (absent: os88vbt.ending_line's
                 default - SPEC.md 126.3.1)}},
     "fade_steps": 1..16, a fade's steps, one a frame,
     "any_key":  true: any key (Esc aside) leaves the title and plays an
                 end loop again, not Enter or Space alone (header 348),
     "cover":    {"image": a picture's path, "line": the line under it} -
                 the TITLE PICTURE, in the battle's window on the desktop,
     "characters": [{"name", "hp", "mp", "atk", "mag", "def", "mdef",
                     "evade", "menu": {"top": [ITEM], "side": [action]}}],
                   ITEM = {"action": id} | {"label", "sub": [action ids]},
     "actions":  {id: ACTION} - section 11's verbs (ACTION_KEYS),
     "enemy":    {"name", stats, "stances": [STANCE], "victory", "defeat",
                  "start_stance"},
     "items":    [{"name", "count"}] - the party's, for an action's cost,
     "band_layout": {vbtrules.LAYOUT key: value} - the band's parts placed
                 and shown (126.5.1; absent: as it always was); a menu
                 look's "places": [[col, row] | null per item],
     "images":   [{"name", "path", "col", "row", "dither"}] - band pictures,
     "band_image": an image's name, under the whole band, or null,
     "menu_looks": {"CHAR:top" | "CHAR:LABEL" | "CHAR:side": {"image",
                  "col", "row", "pitch", "ink", "cursor"}},
     "seed":     null (the clock's) or a number: the battle the same every
                 time (a test's)}

    ACTION = {"label", "target" (TARGETS), "cost": {"mp": n} | {"item":
              name, "n": n} | {"limit": true} | {}, "accuracy" (%),
              "effects": [{"verb", ...}] (vbtrules.compile_rules says which
              fields each verb reads), "pools": {POOL: [clips]},
              "by_target": {character name: {POOL: [clips]}}}
    LOOP = {"clip", "loop": [a, b] (the clip's frames a..b-1, round), "sound":
            a sound id or null}
    STANCE = {"name", "modifiers": {"atk"|"mag"|"def"|"mdef": %},
              "moves": [{"action", "weight", "rule", "target"}],
              (the enemy's "follow_ups": [{"after", "then", "target":
              "same" | a move's rule, "named"?}] - after its turn played
              AFTER, the next plays THEN, before any move or change),
              "responses": [{"trigger", "named", "chance", "action"}],
              "auto": {"kind", "arg", "to", "via"}, "return": action}

Frames are the battle's frames (fps): a clip's "in" and "out" are its
first and last, inclusive, counted in the source as the encoder decodes it.
Paths are relative to the project file.
"""

import copy
import json
import ntpath
import os
import re

FORMAT = "vbtproj"
NO_ENDING = {"how": "hold", "loop": None}
ENDING_HOWS = ("hold", "fade", "join", "again")
VERSION = 1

UI_SOUNDS = ("move", "open", "close", "take", "deny")
VERBS = ("damage", "heal", "restore", "revive", "guard", "charge", "boost",
         "lower", "pass", "escape", "stance", "limit")
TARGETS = ("enemy", "self", "ally", "allies", "none", "character",
           "characters")
POOLS = ("hit", "miss", "defended", "critical", "finishing")
RULES = ("random", "lowest", "last", "alternate", "named")
STATS = ("hp", "mp", "atk", "mag", "def", "mdef", "evade")
MAX_CHARS = 4
MAX_OPENINGS = 8          # the engine's VB_MAXOPEN
ACTION_KEYS = ("label", "target", "cost", "effects", "accuracy", "pools",
               "by_target")


def blank():
    """A new project: the target and nothing else"""
    return {
        "format": FORMAT, "version": VERSION, "name": "Untitled",
        "target": {"profile": "5150-st225", "palette": 0x50, "fps": 15,
                   "rate": 11025, "canvas": [0, 0, 320, 142],
                   "band": [0, 142, 40, 58]},
        "sources": {}, "sounds": {}, "music": None,
        "ui_sounds": {}, "idle": None, "clips": {}, "openings": [],
        "characters": [], "actions": {}, "items": [], "images": [],
        "menu_looks": {}, "band_image": None, "seed": None,
        "title": None, "endings": {"victory": dict(NO_ENDING),
                                   "defeat": dict(NO_ENDING)},
        "fade_steps": 16,
        "enemy": {"name": "ENEMY", "hp": 100, "mp": 0, "atk": 16, "mag": 16,
                  "def": 0, "mdef": 0, "evade": 0,
                  "stances": [{"name": "normal", "moves": [],
                               "responses": []}],
                  "victory": None, "defeat": None},
    }


def upgrade(d):
    """A project dict of any version, brought to VERSION. Version 1 is the
    first, so this only refuses what it cannot read"""
    if d.get("format") != FORMAT:
        raise ValueError("not a Video Battle project")
    v = d.get("version")
    if not isinstance(v, int) or v < 1 or v > VERSION:
        raise ValueError("a project of version %r; this reads 1..%d"
                         % (v, VERSION))
    d.setdefault("title", None)         # (the loops came after version 1's
    d.setdefault("endings", {})         # first files: absent, there are none)
    for k in ("victory", "defeat"):
        d["endings"].setdefault(k, dict(NO_ENDING))
    d.setdefault("fade_steps", 16)
    return d


def own_sound(cid):
    """The id of clip CID's own soundtrack, as a sound"""
    return cid + "_own"


class Problem:
    """One finding of check(): ERROR stops a build, WARN is said, INFO is
    what the engine of today does not play yet"""

    def __init__(self, level, where, text):
        self.level, self.where, self.text = level, where, text

    def __repr__(self):
        return "%s %s: %s" % (self.level, self.where, self.text)


class Project:
    def __init__(self, data=None, path=None):
        self.d = upgrade(copy.deepcopy(data)) if data else blank()
        self.path = path
        self.undo_stack, self.redo_stack = [], []

    # --- files --------------------------------------------------------------
    @classmethod
    def load(cls, path):
        with open(path) as f:
            return cls(json.load(f), os.path.abspath(path))

    def file_refs(self):
        """Every file the project names: (the dict holding it, its key)"""
        d = self.d
        out = [(v, "path") for v in d["sources"].values() if v.get("path")]
        out += [(v, "path") for v in d["sounds"].values()
                if v.get("kind") == "file" and v.get("path")]
        out += [(v, "path") for v in d.get("images") or [] if v.get("path")]
        if (d.get("cover") or {}).get("image"):
            out.append((d["cover"], "image"))
        return out

    def save(self, path=None):
        """...every file it names written RELATIVE to the project when it
        is in the project's folder or below it (the owner's "so I can copy
        projects around without needing to relink"), absolute otherwise -
        each resolved against where the project WAS first, so a Save As to
        another folder keeps finding them"""
        path = os.path.abspath(path or self.path)
        here = os.path.dirname(path)
        for v, k in self.file_refs():
            a = self.here(v[k])
            if not os.path.exists(a):   # (not found: left as it was named)
                continue
            a = os.path.abspath(a)
            try:
                r = os.path.relpath(a, here)
            except ValueError:          # (Windows: another drive)
                r = ".."
            v[k] = r.replace(os.sep, "/") if r != ".." and not \
                r.startswith(".." + os.sep) and not r.startswith("../") \
                and not os.path.isabs(r) else a
        tmp = path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(self.d, f, indent=1)   # (order is meaning: clip order)
            f.write("\n")
        os.replace(tmp, path)
        self.path = path

    def here(self, p):
        """A project-relative path, absolute. An ABSOLUTE one that is not
        there (a project copied from another machine, or saved before paths
        were relative) is looked for beside the project by its name - a
        Windows one too, on any host"""
        if not self.path or not p:
            return p
        base = os.path.dirname(self.path)
        if os.path.isabs(p) or re.match(r"^[A-Za-z]:[\\/]|^\\\\", p):
            if os.path.exists(p):
                return p
            q = os.path.join(base, ntpath.basename(p))
            return q if os.path.exists(q) else p
        return os.path.normpath(os.path.join(base, p.replace("\\", "/")))

    # --- every edit goes through one door, for undo -------------------------
    def _edit(self, fn):
        before = copy.deepcopy(self.d)
        try:
            out = fn(self.d)
        except Exception:
            self.d = before
            raise
        self.undo_stack.append(before)
        self.redo_stack.clear()
        return out

    def undo(self):
        if not self.undo_stack:
            return False
        self.redo_stack.append(self.d)
        self.d = self.undo_stack.pop()
        return True

    def redo(self):
        if not self.redo_stack:
            return False
        self.undo_stack.append(self.d)
        self.d = self.redo_stack.pop()
        return True

    def set(self, keys, value):
        """The general edit: the value at a path of keys and indexes"""
        def f(d):
            for k in keys[:-1]:
                d = d[k]
            d[keys[-1]] = copy.deepcopy(value)
        self._edit(f)

    def get(self, *keys):
        d = self.d
        for k in keys:
            d = d[k]
        return d

    # --- the named edits ----------------------------------------------------
    def add_source(self, sid, path=None, scene=None):
        if (path is None) == (scene is None):
            raise ValueError("a source is a video path or a scene name")
        v = {"kind": "video", "path": path} if path else \
            {"kind": "scene", "name": scene}
        self.set(("sources", sid), v)

    def add_sound(self, sid, path=None, scene=None, source=None, rng=None,
                  level=1.0):
        if path:
            v = {"kind": "file", "path": path}
        elif scene:
            v = {"kind": "scene", "name": scene}
        elif source:
            v = {"kind": "source", "source": source, "in": rng[0],
                 "out": rng[1]}
        else:
            raise ValueError("a sound is a file, a scene's or a source's")
        v["level"] = level
        self.set(("sounds", sid), v)

    def sound_users(self, sid):
        """What uses sound SID, as words: the music, the menu's sounds,
        an opening's, a clip's cues, the title's and the end loops'"""
        d, out = self.d, []
        if (d.get("music") or {}).get("sound") == sid:
            out.append("the music")
        out += ["the menu's %s sound" % k for k, v in
                d.get("ui_sounds", {}).items() if v == sid]
        out += ["opening %d" % (i + 1) for i, o in enumerate(d["openings"])
                if o.get("sound") == sid]
        out += ["a cue of %s" % cid for cid, c in d["clips"].items()
                if any(q["sound"] == sid for q in c.get("cues", []))]
        if ((d.get("title") or {}).get("sound")) == sid:
            out.append("the title loop")
        for k, en in d.get("endings", {}).items():
            if ((en.get("loop") or {}).get("sound")) == sid:
                out.append("the %s end loop" % k)
        return out

    def sounds_used(self):
        """Every sound something uses - the ones a build reads"""
        return {sid for sid in self.d["sounds"] if self.sound_users(sid)}

    def remove_sound(self, sid, force=False):
        """Sound SID out of the project. The MUSIC may be it - removing the
        music's sound leaves the battle with none; anything else that uses
        it refuses, naming what - unless FORCE, when it is taken out of
        each of them too (a menu sound silent, an opening's, the title's
        or an end clip's sound the music, a cue gone)"""
        users = [u for u in self.sound_users(sid) if u != "the music"]
        if users and not force:
            raise ValueError("%s is used by %s: take it out of those first"
                             % (sid, ", ".join(users)))

        def f(d):
            del d["sounds"][sid]
            if (d.get("music") or {}).get("sound") == sid:
                d["music"] = None
            ui = d.get("ui_sounds", {})
            for k in [k for k, v in ui.items() if v == sid]:
                del ui[k]
            for o in d["openings"]:
                if o.get("sound") == sid:
                    o["sound"] = None
            for c in d["clips"].values():
                if any(q["sound"] == sid for q in c.get("cues", [])):
                    c["cues"] = [q for q in c["cues"] if q["sound"] != sid]
            if (d.get("title") or {}).get("sound") == sid:
                d["title"]["sound"] = None
            for en in d.get("endings", {}).values():
                if (en.get("loop") or {}).get("sound") == sid:
                    en["loop"]["sound"] = None
        self._edit(f)

    def set_sound_file(self, sid, path):
        """Sound SID read from the file at PATH from now on - its file moved
        or renamed, or another wanted - everything that uses it kept"""
        if self.d["sounds"][sid].get("kind") != "file":
            raise ValueError("%s is not a file's sound" % sid)
        self.set(("sounds", sid, "path"), path)

    def sound_missing(self, sid):
        """True when sound SID is a file's and the file is not there"""
        s = self.d["sounds"][sid]
        return s.get("kind") == "file" and not os.path.exists(
            self.here(s["path"]))

    def set_idle(self, source, loop):
        self.set(("idle",), {"source": source, "loop": list(loop)})

    def add_clip(self, cid, source, rng, anchors=(0,), exit=0, joins=True,
                 ends=False, cues=()):
        self.set(("clips", cid), {
            "source": source, "in": rng[0], "out": rng[1],
            "anchors": list(anchors), "exit": exit, "joins": joins,
            "ends": ends, "cues": [dict(c) for c in cues]})

    def set_anchors(self, cid, anchors):
        self.set(("clips", cid, "anchors"), sorted(set(anchors)))

    def add_cue(self, cid, frame, sound, level=1.0):
        def f(d):
            d["clips"][cid]["cues"].append(
                {"frame": frame, "sound": sound, "level": level})
        self._edit(f)

    def remove_clip(self, cid):
        """Refused while anything uses it: an action's pool, an opening, an
        ending"""
        if cid in self.clips_used():
            raise ValueError("%s is in use: take it out of what uses it "
                             "first" % cid)
        def f(d):
            del d["clips"][cid]
        self._edit(f)

    def set_clip_range(self, cid, first, last):
        """The clip's in and out, in its source's frames. Its cues move with
        its in point (a cue is placed on the clip's own frames) and any that
        fall off its end are dropped"""
        def f(d):
            c = d["clips"][cid]
            if last < first:
                raise ValueError("out before in")
            shift = first - c["in"]
            c["in"], c["out"] = first, last
            n = last - first + 1
            c["cues"] = [dict(q, frame=q["frame"] - shift) for q in c["cues"]
                         if 0 <= q["frame"] - shift < n]
            own = d["sounds"].get(own_sound(cid))
            if own and own["kind"] == "source":     # its own soundtrack:
                if own.get("whole", True):          # the whole clip's
                    own["in"], own["out"] = first, last     # follows it;
                else:                               # a part of it stays
                    own["in"] = max(own["in"], first)   # on its footage
                    own["out"] = min(own["out"], last)
                    if own["out"] < own["in"]:
                        own["in"], own["out"] = first, last
                    for q in c["cues"]:
                        if q["sound"] == own_sound(cid):
                            q["frame"] = own["in"] - first
        self._edit(f)

    def use_own_sound(self, cid, level=1.0, first=0, last=None):
        """The clip's own soundtrack - its source's audio under the clip's
        frames FIRST..LAST (the whole clip by default) - as a cue at frame
        FIRST. The whole clip's follows its in and out from here on; a part
        stays on the footage it was taken from"""
        def f(d):
            c = d["clips"][cid]
            n = c["out"] - c["in"]
            lo = max(0, min(first, n))
            hi = n if last is None else max(lo, min(last, n))
            sid = own_sound(cid)
            d["sounds"][sid] = {"kind": "source", "source": c["source"],
                                "in": c["in"] + lo, "out": c["in"] + hi,
                                "level": level, "whole": lo == 0 and hi == n}
            c["cues"] = [q for q in c["cues"] if q["sound"] != sid]
            c["cues"].append({"frame": lo, "sound": sid, "level": 1.0})
        self._edit(f)

    def set_cue_until(self, cid, i, until):
        """Cue I stops at the clip's frame UNTIL (None: plays out)"""
        def f(d):
            q = d["clips"][cid]["cues"][i]
            if until is None:
                q.pop("until", None)
            elif until <= q["frame"]:
                raise ValueError("a cue stops after the frame it starts at")
            else:
                q["until"] = int(until)
        self._edit(f)

    def toggle_anchor(self, cid, k):
        def f(d):
            a = set(d["clips"][cid]["anchors"])
            a ^= {k}
            d["clips"][cid]["anchors"] = sorted(a)
        self._edit(f)

    def set_exit(self, cid, k):
        self.set(("clips", cid, "exit"), k)

    def set_ends(self, cid, ends):
        """An ENDING (the battle ends on it, its last frame held) does not
        join the loop again; anything else does"""
        def f(d):
            d["clips"][cid]["ends"] = bool(ends)
            d["clips"][cid]["joins"] = not ends
        self._edit(f)

    def move_cue(self, cid, i, frame):
        def f(d):
            c = d["clips"][cid]
            if not 0 <= frame <= c["out"] - c["in"]:
                raise ValueError("frame %d is off the clip" % frame)
            c["cues"][i]["frame"] = frame
        self._edit(f)

    def set_cue_level(self, cid, i, level):
        self.set(("clips", cid, "cues", i, "level"), float(level))

    def remove_cue(self, cid, i):
        def f(d):
            del d["clips"][cid]["cues"][i]
        self._edit(f)

    def add_opening(self, clip, sound=None):
        def f(d):
            if len(d["openings"]) >= MAX_OPENINGS:
                raise ValueError("at most %d openings" % MAX_OPENINGS)
            d["openings"].append({"clip": clip, "sound": sound})
        self._edit(f)

    def remove_opening(self, i):
        def f(d):
            del d["openings"][i]
        self._edit(f)

    def move_opening(self, i, by):
        """Opening I moved BY places in the turn (the last is the scene's)"""
        def f(d):
            o = d["openings"]
            j = max(0, min(len(o) - 1, i + by))
            o.insert(j, o.pop(i))
        self._edit(f)

    def set_opening(self, i, clip=None, sound=False, full=None, seam=None):
        """Opening I's clip, its sound (None: silent; False: as it is),
        whether it is FULL SCREEN (True) or plays with the menu (False), and
        its SEAM with the opening before it (os88vbt.SEAMS; "auto" removes
        the key); None leaves it as it is"""
        def f(d):
            o = d["openings"][i]
            if seam is not None:
                if seam == "auto":
                    o.pop("seam", None)
                else:
                    o["seam"] = seam
            if clip is not None:
                o["clip"] = clip
            if sound is not False:
                o["sound"] = sound
            if full is not None:
                if full:
                    o["full"] = True
                else:
                    o.pop("full", None)
        self._edit(f)

    def own_sound_of(self, cid, level=1.0):
        """Clip CID's own soundtrack, all of it, as a sound (an opening's
        sound, which plays instead of the music); its id"""
        sid = own_sound(cid)

        def f(d):
            c = d["clips"][cid]
            d["sounds"][sid] = {"kind": "source", "source": c["source"],
                                "in": c["in"], "out": c["out"],
                                "level": level, "whole": True}
        self._edit(f)
        return sid

    def set_music(self, sound, join=0, loop=None):
        self.set(("music",), {"sound": sound, "join": join,
                              "loop": list(loop) if loop else None})

    def set_music_loop(self, start, end):
        def f(d):
            m = d["music"]
            m["loop"] = [start, end]
            if not start <= m["join"] < end:
                m["join"] = start
        self._edit(f)

    def add_character(self, name, **stats):
        def f(d):
            if len(d["characters"]) >= MAX_CHARS:
                raise ValueError("at most %d characters" % MAX_CHARS)
            c = {"name": name, "hp": 100, "mp": 0, "atk": 16, "mag": 16,
                 "def": 0, "mdef": 0, "evade": 0,
                 "menu": {"top": [], "side": []}}
            c.update(stats)
            d["characters"].append(c)
            return len(d["characters"]) - 1
        return self._edit(f)

    def move_character(self, i, by):
        """Character I moved BY places in the party's order (the turns',
        the band's lines'). Everything else names a character by its NAME
        (an action's own clips, an enemy move, a menu's look), so nothing
        else changes"""
        def f(d):
            c = d["characters"]
            j = max(0, min(len(c) - 1, i + by))
            c.insert(j, c.pop(i))
            return j
        return self._edit(f)

    def remove_character(self, i):
        def f(d):
            if len(d["characters"]) <= 1:
                raise ValueError("a battle has at least one character")
            del d["characters"][i]
        self._edit(f)

    def add_action(self, aid, label, target="enemy", effects=(), cost=None,
                   accuracy=100, pools=None):
        self.set(("actions", aid), {
            "label": label, "target": target, "cost": cost or {},
            "effects": [dict(e) for e in effects], "accuracy": accuracy,
            "pools": {k: list(v) for k, v in (pools or {}).items()},
            "by_target": {}})

    def menu_add(self, char, action=None, label=None, sub=(), side=False):
        """An item on character CHAR's top menu (an action, or a sub-menu of
        actions under a label), or an action on the side menu"""
        def f(d):
            m = d["characters"][char]["menu"]
            if side:
                m["side"].append(action)
            elif action:
                m["top"].append({"action": action})
            else:
                m["top"].append({"label": label, "sub": list(sub)})
        self._edit(f)

    def remove_action(self, aid):
        """Refused while a menu, a move or a response names it"""
        if aid in self.actions_used():
            raise ValueError("%s is in use: take it off what uses it first"
                             % aid)
        def f(d):
            del d["actions"][aid]
        self._edit(f)

    def add_stance(self, name):
        def f(d):
            if any(s["name"] == name for s in d["enemy"]["stances"]):
                raise ValueError("a stance %s already" % name)
            d["enemy"]["stances"].append({"name": name, "moves": [],
                                          "responses": []})
        self._edit(f)

    def remove_stance(self, i):
        def f(d):
            st = d["enemy"]["stances"]
            if len(st) <= 1:
                raise ValueError("the enemy has at least one stance")
            name = st[i]["name"]
            for a in d["actions"].values():
                for e in a["effects"]:
                    if e.get("verb") == "stance" and e.get("stance") == name:
                        raise ValueError("an action changes to %s" % name)
            for s2 in st:
                if (s2.get("auto") or {}).get("to") == name:
                    raise ValueError("%s's change goes to %s" % (s2["name"], name))
            del st[i]
        self._edit(f)

    def menu_remove(self, char, where, i, sub=None):
        """Item I of character CHAR's "top" or "side" menu, or of the top
        menu's sub-menu at index SUB"""
        def f(d):
            m = d["characters"][char]["menu"]
            if sub is not None:
                del m["top"][sub]["sub"][i]
            else:
                del m[where][i]
        self._edit(f)

    def menu_move(self, char, where, i, by, sub=None):
        def f(d):
            m = d["characters"][char]["menu"]
            lst = m["top"][sub]["sub"] if sub is not None else m[where]
            j = i + by
            if not 0 <= j < len(lst):
                raise ValueError("it is at the end already")
            lst[i], lst[j] = lst[j], lst[i]
        self._edit(f)

    def enemy_move(self, action, weight=1, stance=0, rule="alternate",
                   duration=0):
        def f(d):
            d["enemy"]["stances"][stance]["moves"].append(
                {"action": action, "weight": weight, "rule": rule,
                 "duration": duration})
        self._edit(f)

    # --- what is derived, never stored ---------------------------------------
    def clip_frames(self, cid):
        c = self.d["clips"][cid]
        return c["out"] - c["in"] + 1

    def idle_frames(self):
        a, b = self.d["idle"]["loop"]
        return b - a

    def actions_used(self):
        """Every action a menu or the enemy names"""
        used = []
        for ch in self.d["characters"]:
            for it in ch["menu"]["top"]:
                used += [it["action"]] if "action" in it else it["sub"]
            used += ch["menu"]["side"]
        for st in self.d["enemy"]["stances"]:
            used += [m["action"] for m in st["moves"]]
            used += [r["action"] for r in st.get("responses", [])]
            used += [x for x in (st.get("return"),
                                 (st.get("auto") or {}).get("via")) if x]
        return used

    def clip_chars(self, aid):
        """The characters whose OWN clips (action AID's "by_target") the
        battle can look for, as vbtrules.clip_char picks them: the
        character doing it when it is aimed at the enemy or at oneself;
        any character when one is chosen (a menu's ally, an enemy move
        that is not "named"); the named one when the enemy names it. Aimed
        at more than one, or the enemy's at itself, nobody's: the action's
        own pools alone (vbtrules.targets)"""
        d = self.d
        a = d["actions"].get(aid)
        if a is None:
            return set()
        t = a.get("target")
        names = [ch["name"] for ch in d["characters"]]
        out = set()
        for ch in d["characters"]:              # the party's menus
            m = ch["menu"]
            has = aid in m["side"] or any(
                it.get("action") == aid or aid in it.get("sub", ())
                for it in m["top"])
            if not has:
                continue
            if t == "ally":                     # (vbtrules.targets: a
                out.update(names)               # character's "character"
            elif t in ("enemy", "self", "character"):   # is the enemy)
                out.add(ch["name"])
        if t in ("enemy", "character"):         # the enemy's, at whom it
            out |= self._enemy_aims(aid, names, set())  # chose
        return out

    def _enemy_aims(self, aid, names, seen):
        """The characters the enemy can aim action AID at: a "named" move's
        one, anybody for any other rule; a follow-up "same" as the move it
        follows"""
        if aid in seen:
            return set()
        seen.add(aid)
        e, out = self.d["enemy"], set()
        for st in e["stances"]:
            for m in st["moves"]:
                if m["action"] == aid:          # (named, none chosen: the
                    tg = m.get("target") or (names[:1] or [None])[0]  # first)
                    out |= {tg} if m.get("rule") == "named" and \
                        tg in names else set(names)
            if any(r.get("action") == aid for r in st.get("responses", [])) \
                    or aid in (st.get("return"),
                               (st.get("auto") or {}).get("via")):
                out |= set(names)
        for f in e.get("follow_ups", []):
            if f.get("then") != aid:
                continue
            tg = f.get("target", "same")
            if tg == "same":
                out |= self._enemy_aims(f.get("after"), names, seen)
            elif tg == "named" and (f.get("named") or
                                    (names[:1] or [None])[0]) in names:
                out.add(f.get("named") or names[0])
            else:
                out |= set(names)
        return out

    def clips_used(self):
        used = [o["clip"] for o in self.d["openings"]]
        for a in self.d["actions"].values():
            for pool in a["pools"].values():
                used += pool
            for per in a.get("by_target", {}).values():
                for pool in per.values():
                    used += pool
        e = self.d["enemy"]
        used += [x for x in (e.get("victory"), e.get("defeat")) if x]
        t = self.d.get("title")
        if t:
            used += [t["clip"]] + ([t["leave"]] if t.get("leave") else [])
        for en in self.d["endings"].values():
            if en.get("loop"):
                used.append(en["loop"]["clip"])
        return used

    def sources_used(self):
        """The sources a build READS: the idle loop's, those of the clips
        something plays, and those a used sound is cut from"""
        d = self.d
        out = {(d.get("idle") or {}).get("source")} | {
            d["clips"][c]["source"] for c in self.clips_used()
            if c in d["clips"]} | {
            d["sounds"][k].get("source") for k in self.sounds_used()
            if k in d["sounds"]}
        return out - {None}

    # --- the title loop and the endings (SPEC.md 126.3.1) ---------------------
    def set_title(self, clip, loop=None, sound=None, leave=None, anchors=()):
        """The TITLE loop: clip CLIP's frames LOOP (all of it by default),
        round until a key, then a fade (LEAVE None) or clip LEAVE, entered
        at the title's frames ANCHORS. CLIP None: no title"""
        def f(d):
            if clip is None:
                d["title"] = None
                return
            c = d["clips"][clip]
            lp = list(loop) if loop else [0, c["out"] - c["in"] + 1]
            d["title"] = {"clip": clip, "loop": lp, "sound": sound,
                          "leave": leave, "anchors": sorted(set(anchors))}
        self._edit(f)

    def set_ending(self, outcome, how, clip=None, loop=None, sound=None,
                   full=None, plays=None, then=None, music_after=None):
        """How OUTCOME ("victory"/"defeat") ends: HOW, and its END clip -
        clip CLIP's frames LOOP - for "fade" and "join": FULL screen or the
        canvas, it PLAYS "loop" (round until a key) or "once", and THEN -
        after one that plays once - "hold", "fade" (to the title, the play
        again) or "join" (the title); MUSIC_AFTER, for one that plays once
        with its own sound, the battle's music back once that sound is over
        (False, the default: silence to its end). None keeps what is
        there"""
        if how not in ENDING_HOWS:
            raise ValueError("an ending is %s" % ", ".join(ENDING_HOWS))
        if plays not in (None, "loop", "once"):
            raise ValueError("an end clip plays loop or once")
        if then not in (None, "hold", "fade", "join"):
            raise ValueError("after a clip that plays once: hold, fade or "
                             "join")

        def f(d):
            old = d["endings"].get(outcome) or {}
            lp = None
            if clip:
                c = d["clips"][clip]
                lp = {"clip": clip, "sound": sound, "loop": list(loop) if loop
                      else [0, c["out"] - c["in"] + 1]}
            en = {"how": how, "loop": lp}
            for k, v in (("full", full), ("plays", plays), ("then", then),
                         ("music_after", music_after)):
                v = old.get(k) if v is None else v
                if v not in (None, False, "loop", "hold"):
                    en[k] = v
            if old.get("line"):     # (its line, the author's: kept)
                en["line"] = old["line"]
            d["endings"][outcome] = en
        self._edit(f)

    def loop_sound(self, clip, loop):
        """Clip CLIP's own soundtrack under its frames LOOP, as a sound for a
        title or end loop; its id"""
        sid = "%s_lp" % clip[:12]

        def f(d):
            c = d["clips"][clip]
            d["sounds"][sid] = {"kind": "source", "source": c["source"],
                                "in": c["in"] + loop[0],
                                "out": c["in"] + loop[1] - 1, "level": 1.0,
                                "whole": False}
        self._edit(f)
        return sid

    # --- the checks -----------------------------------------------------------
    def check(self):
        """Everything wrong with the project, as Problems. An ERROR stops a
        build; INFO is what the engine of today does not yet play"""
        d, out = self.d, []

        def err(w, t):
            out.append(Problem("ERROR", w, t))

        def warn(w, t):
            out.append(Problem("WARN", w, t))

        def info(w, t):
            out.append(Problem("INFO", w, t))

        t = d["target"]
        x, y, w, h = t["canvas"]
        if not (0 <= x and x + w <= 320 and 0 <= y and y + h <= 200):
            err("target", "the canvas %s is off a 320x200 screen" % t["canvas"])
        if w != 320 or x != 0 or y != 0:
            info("target", "the engine plays a canvas at 0,0, 320 wide")
        for k, v in (t.get("scale") or {}).items():
            if k not in ("cga4", "vga4", "herc") or v not in (100, 90, 75,
                                                               67, 50):
                err("target.scale", "%s at %s%%: a picture is 100, 90, 75, 67 "
                    "or 50%% of its canvas" % (k, v))
        if t.get("fit") not in (None, "fill", "stretch"):
            err("target.fit", "fit %s: fill or stretch" % t["fit"])
        if t.get("clip") is not None and not (
                isinstance(t["clip"], int) and 0 <= t["clip"] <= 96):
            err("target.clip", "clip %s: 0 to 96 levels" % t["clip"])
        for k, en in d.get("endings", {}).items():
            ln = en.get("line") or ""
            if len(ln) > 38 or any(not (" " <= ch <= "~") for ch in ln):
                err("endings.%s.line" % k, "%r: 38 plain characters at most "
                    "(A-Z, digits, punctuation)" % ln)
        if t.get("vga_band_cga") not in (None, True, False):
            err("target.vga_band_cga", "vga_band_cga: true or false")
        vo = t.get("vga_clip_colours")
        if vo is not None and not (isinstance(vo, int) and not
                                   isinstance(vo, bool) and 0 <= vo <= 4):
            err("target.vga_clip_colours", "vga_clip_colours %r: 0 to 4"
                % (vo,))
        if t.get("cadence") not in (None, True, False):
            err("target.cadence", "cadence: true or false")
        xk = t.get("xms_kb")
        if xk is not None and not (isinstance(xk, int) and not
                                   isinstance(xk, bool) and 0 <= xk <= 65535):
            err("target.xms_kb", "xms_kb %r: 0 to 65535 KB" % (xk,))
        if t.get("vga_bands") is not None and not (
                isinstance(t["vga_bands"], int) and 0 <= t["vga_bands"] <= 200):
            err("target.vga_bands", "vga_bands %s: rows a band, 0 the whole "
                "frame" % t["vga_bands"])
        bc, br, bn, bh = t["band"]
        if br < y + h and br + bh > y:
            err("target", "the band overlaps the canvas")
        used = self.sounds_used()
        # a source is READ only for what is built (sources_used) - one
        # nothing uses missing is said, and stops nothing (as a sound)
        srcs = self.sources_used()
        for sid, s in d["sources"].items():
            if s["kind"] == "video" and self.path and \
                    not os.path.exists(self.here(s["path"])):
                (err if sid in srcs else warn)(
                    "sources." + sid, "%s is missing%s" % (
                        s["path"], "" if sid in srcs else
                        " (nothing built uses it)"))
        for sid, s in d["sounds"].items():
            if s["kind"] == "file" and self.path and sid in used and \
                    not os.path.exists(self.here(s["path"])):
                err("sounds." + sid, "%s is missing" % s["path"])
            if s["kind"] == "source" and s["source"] not in d["sources"]:
                err("sounds." + sid, "no source %s" % s["source"])
        if not d["idle"]:
            err("idle", "no idle loop")
        else:
            if d["idle"]["source"] not in d["sources"]:
                err("idle", "no source %s" % d["idle"]["source"])
            a, b = d["idle"]["loop"]
            if b - a < 2:
                err("idle", "a loop of %d frames" % (b - a))
        n = self.idle_frames() if d["idle"] else 0
        tt = d.get("title") or {}         # (a title's or an end loop's clip
        looped = {tt.get("clip"), tt.get("leave")} | {   # needs no anchor)
            en["loop"]["clip"] for en in d["endings"].values() if en.get("loop")}
        used_clips = set(self.clips_used())
        for cid, c in d["clips"].items():
            w = "clips." + cid
            if c["source"] not in d["sources"]:
                err(w, "no source %s" % c["source"])
            if c["out"] < c["in"]:
                err(w, "out before in")
            if c.get("palette") not in (None, "auto", "own", "shared"):
                err(w, "palette %r: auto, own or shared" % (c["palette"],))
            opening = any(o["clip"] == cid for o in d["openings"]) or \
                cid in looped
            # (an anchor is where an ACTION's clip joins from the idle loop:
            # a clip nothing plays needs none, and is said once, below)
            if not opening and not c["anchors"] and cid in used_clips:
                err(w, "no anchor: it can never start")
            for k in c["anchors"]:
                if not 0 <= k < n:
                    err(w, "anchor %d is not an idle frame (0..%d)" % (k, n - 1))
            if len(c["anchors"]) > 16:
                err(w, "%d anchors, at most 16" % len(c["anchors"]))
            if not 0 <= c["exit"] < max(n, 1):
                err(w, "exit %d is not an idle frame" % c["exit"])
            for cue in c["cues"]:
                if cue["sound"] not in d["sounds"]:
                    err(w, "cue: no sound %s" % cue["sound"])
                if not 0 <= cue["frame"] <= c["out"] - c["in"]:
                    err(w, "cue at frame %d, past the clip" % cue["frame"])
        if len(d["openings"]) > MAX_OPENINGS:
            err("openings", "%d openings, at most %d" % (len(d["openings"]),
                                                         MAX_OPENINGS))
        if not d["openings"]:
            info("openings", "no opening: the battle starts on the idle "
                 "loop's first frame, from black")
        for i, o in enumerate(d["openings"]):
            if o["clip"] not in d["clips"]:
                err("openings.%d" % i, "no clip %s" % o["clip"])
            if o.get("sound") and o["sound"] not in d["sounds"]:
                err("openings.%d" % i, "no sound %s" % o["sound"])
            if o.get("full") and i == len(d["openings"]) - 1:
                err("openings.%d" % i, "the last opening joins the idle "
                    "loop, so it plays with the menu, not full screen")
            if o.get("full") and any(not p.get("full")
                                     for p in d["openings"][:i]):
                err("openings.%d" % i, "full screen after an opening with "
                    "the menu: the full-screen ones come first (the menu, "
                    "once it is there, stays)")
        if not d["music"]:
            info("music", "no music: after the openings the battle is "
                 "silent but for its clips' and the menu's sounds")
        elif d["music"]["sound"] not in d["sounds"]:
            err("music", "no sound %s" % d["music"]["sound"])
        elif d["music"].get("loop"):
            a, b = d["music"]["loop"]
            if not 0 <= a < b:
                err("music", "a loop of %d..%d" % (a, b))
            elif not a <= d["music"]["join"] < b:
                err("music", "the join at %d is outside the loop %d..%d"
                    % (d["music"]["join"], a, b))
        for k, v in d["ui_sounds"].items():
            if k not in UI_SOUNDS:
                err("ui_sounds", "no menu sound called %s" % k)
            elif v not in d["sounds"]:
                err("ui_sounds." + k, "no sound %s" % v)
        if not 1 <= len(d["characters"]) <= MAX_CHARS:
            err("characters", "%d characters, 1 to %d" % (len(d["characters"]),
                                                         MAX_CHARS))
        for aid, a in d["actions"].items():
            w = "actions." + aid
            if a["target"] not in TARGETS:
                err(w, "no target %s" % a["target"])
            for e in a["effects"]:
                if e.get("verb") not in VERBS:
                    err(w, "no verb %s" % e.get("verb"))
            for pk, pool in a["pools"].items():
                if pk not in POOLS:
                    err(w, "no pool %s" % pk)
                for cl in pool:
                    if cl not in d["clips"]:
                        err(w, "pool %s: no clip %s" % (pk, cl))
            if not a["pools"].get("hit") and a.get("accuracy", 100) != 0:
                # (0%: it never hits - a miss made a miss, the owner's)
                # (a HIT clip per TARGET counts: an enemy's action is often
                # made a clip per character it can hit) - and only where the
                # battle can GO: a character's own clip for an action only
                # another has, or one the enemy aims at somebody else, is a
                # path nobody takes (the owner, 2026-11-05)
                by = a.get("by_target", {})
                names_ = [ch["name"] for ch in d["characters"]]
                have = [n_ for n_ in names_ if by.get(n_, {}).get("hit")]
                reach = self.clip_chars(aid)
                if not have:
                    warn(w, "no HIT clip: it happens at once, with no picture")
                elif reach - set(have):
                    warn(w, "no HIT clip at %s: there it happens at once, "
                         "with no picture" % ", ".join(
                             n_ for n_ in names_ if n_ in reach and
                             n_ not in have))
        for aid in self.actions_used():
            if aid not in d["actions"]:
                err("menus", "no action %s" % aid)
        for ci, ch in enumerate(d["characters"]):
            if not ch["menu"]["top"]:
                err("characters.%d" % ci, "%s has an empty menu" % ch["name"])
        e = d["enemy"]
        for k in ("victory", "defeat"):
            if not e.get(k):
                err("enemy." + k, "no %s clip" % k)
            elif e[k] not in d["clips"]:
                err("enemy." + k, "no clip %s" % e[k])
        if not any(st["moves"] for st in e["stances"]):
            err("enemy", "the enemy has no move")
        for si, st in enumerate(e["stances"]):
            for m in st["moves"]:
                if m.get("rule", "alternate") not in RULES:
                    err("enemy.stances.%d" % si, "no rule %s" % m["rule"])
        # THE BAND'S LAYOUT (126.5.1)
        import vbtrules as V_
        own = d.get("band_layout") or {}
        for k in own:
            if k not in V_.LAYOUT:
                err("band_layout", "no part of the band called %s" % k)
        lay = V_.layout_of(self)
        # ...each field as wide as the party's own values make it, not
        # the widest it could be (the owner, 2026-11-05): a name's letters,
        # HP's four places (or the number's own, past 9999), "/max" its
        # digits - its padding past them is drawn over by a field to its
        # right (vbtband.party_line: a later one wins)
        chs = d["characters"] or [{"name": "", "hp": 0, "mp": 0}]
        nw = max(len(str(c.get("name", ""))[:6]) for c in chs)
        hw = max(max(4, len(str(int(c.get("hp", 0))))) for c in chs)
        mw = max(max(4, len(str(int(c.get("mp", 0))))) for c in chs)
        xw = max(1 + len(str(int(c.get("hp", 0)))) for c in chs)
        f = []
        if lay["name_show"] and nw:
            f.append(("the name", lay["name_col"], nw))
        if lay["limit_show"]:
            f.append(("the limit star", lay["limit_col"], 1))
        if lay["hp_show"]:
            f.append(("HP", lay["hp_col"], hw if not lay["hpmax_show"]
                      else max(hw, 4 + xw)))
        if lay["mp_show"]:
            f.append(("MP", lay["mp_col"], mw))
        for i, (n1, c1, w1) in enumerate(f):
            for n2, c2, w2 in f[i + 1:]:
                if c1 < c2 + w2 and c2 < c1 + w1:
                    warn("band_layout", "%s (columns %d-%d) and %s (%d-%d) "
                         "overlap" % (n1, c1, c1 + w1 - 1, n2, c2,
                                      c2 + w2 - 1))
        import vbtband as VB_       # parts that share cells: a partial
        for a, b in VB_.overlaps(self):  # redraw lays them otherwise
            warn("band_layout", "%s and %s share cells - whichever changed "
                 "last is drawn over the other" % (a, b))
        # THE FOLLOW-UPS (126.6.7): after X, the next turn Y
        fol = e.get("follow_ups", [])
        nxt = {}
        for i, x in enumerate(fol):
            w = "enemy.follow_ups.%d" % i
            for k in ("after", "then"):
                if x.get(k) not in d["actions"]:
                    err(w, "no action chosen for %s" % ("what it follows"
                        if k == "after" else "what follows"))
            tg = x.get("target", "same")
            if tg != "same" and tg not in RULES:
                err(w, "no target rule %s" % tg)
            chn = [c["name"] for c in d["characters"]]
            if tg == "named" and (x.get("named") or
                                  (chn[0] if chn else None)) not in chn:
                # (none chosen: the first, as the Enemy tab shows it)
                err(w, "aims at no character (%s)" % x.get("named"))
            if x.get("after") in nxt:
                warn(w, "%s already has a follow-up: the first is played"
                     % x.get("after"))
            nxt.setdefault(x.get("after"), x.get("then"))
        for a in list(nxt):             # a chain that comes back round is
            seen, k = set(), a          # every turn from then on
            while k in nxt and k not in seen:
                seen.add(k)
                k = nxt[k]
            if k == a:
                warn("enemy.follow_ups", "%s leads back to itself: once "
                     "played, the enemy plays that round for good" % a)
                break
        idle_src = (d.get("idle") or {}).get("source")
        for cid, c in d["clips"].items():
            if cid in used_clips:
                continue
            if c["source"] != idle_src:     # (one of the idle loop's own
                warn("clips." + cid, "no action, opening or ending uses it: "
                     "it is left out of the build")
        for it in d.get("items", []):
            if not it.get("name"):
                err("items", "an item with no name")
        names = [im["name"] for im in d.get("images", [])]
        for im in d.get("images", []):
            if self.path and not os.path.exists(self.here(im["path"])):
                err("images." + im["name"], "%s is missing" % im["path"])
        if d.get("band_image") and d["band_image"] not in names:
            err("band_image", "no image %s" % d["band_image"])
        for k, lk in d.get("menu_looks", {}).items():
            if lk.get("image") and lk["image"] not in names:
                err("menu_looks." + k, "no image %s" % lk["image"])
        self._loops_check(err, warn, info)
        if not any(p.level == "ERROR" for p in out):
            out += self.rules_check()
        out += self.engine_gaps()
        return out

    def _loops_check(self, err, warn, info):
        d = self.d

        def loop(w, lp, need=2):
            if not lp:
                return
            c = d["clips"].get(lp["clip"])
            if c is None:
                return err(w, "no clip %s" % lp["clip"])
            a, b = lp["loop"]
            if not 0 <= a < b <= c["out"] - c["in"] + 1 or b - a < need:
                err(w, "a loop of frames %d..%d of %s (%d frames)" % (
                    a, b - 1, lp["clip"], c["out"] - c["in"] + 1))
            if lp.get("sound") and lp["sound"] not in d["sounds"]:
                err(w, "no sound %s" % lp["sound"])
        t = d.get("title")
        if t:
            loop("title", t)
            if t.get("leave"):
                if t["leave"] not in d["clips"]:
                    err("title", "no clip %s to leave by" % t["leave"])
                n = t["loop"][1] - t["loop"][0]
                if not t.get("anchors"):
                    err("title", "it is left by %s and has no frame to enter "
                        "it at (an anchor)" % t["leave"])
                elif any(not 0 <= k < n for k in t["anchors"]) or \
                        len(t["anchors"]) > 16:
                    err("title", "anchors %s: 1 to 16 of its %d frames"
                        % (t["anchors"], n))
        if not 1 <= d.get("fade_steps", 16) <= 16:
            err("fade_steps", "a fade is 1 to 16 steps")
        e = d["enemy"]
        for k, en in d["endings"].items():
            w = "endings." + k
            if en["how"] not in ENDING_HOWS:
                err(w, "an ending is %s" % ", ".join(ENDING_HOWS))
            elif en["how"] in ("fade", "join"):
                if not en.get("loop"):
                    err(w, "%s to an end loop, and there is none" % en["how"])
                loop(w, en.get("loop"))
                if en["how"] == "join" and not e.get(k):
                    err(w, "joins its end loop, and there is no %s clip" % k)
            ec = e.get(k)
            if ec in d["clips"]:
                c = d["clips"][ec]
                pools = [aid for aid, a in d["actions"].items()
                         if any(ec in pl for pl in a["pools"].values()) or
                         any(ec in pl for per in a.get("by_target", {}).values()
                             for pl in per.values())]
                if pools:
                    warn("clips." + ec, "is the %s clip and %s's too: played "
                         "there it would end the battle - give the action a "
                         "clip of its own" % (k, ", ".join(pools)))
                if en.get("plays") == "once" and en.get("then") == "join" \
                        and not d.get("title"):
                    warn(w, "joins the title, and there is none: the play "
                         "again comes by a fade")
            elif en["how"] == "again" and en.get("loop"):
                warn(w, "plays again: its end loop is not played")

    def errors(self):
        return [p for p in self.check() if p.level == "ERROR"]

    # --- what today's engine does not play yet (SPEC.md 126.7) ------------------
    def engine_gaps(self):
        """INFO for each thing the project says that the engine of today
        does not play yet"""
        out = []
        for st in self.d["enemy"]["stances"]:
            if st.get("idle"):
                out.append(Problem("INFO", "enemy.stances." + st["name"],
                                   "a stance's own idle loop is not played "
                                   "yet: it shares the battle's"))
        return out

    def rules_check(self):
        """What compile_rules would refuse, as ERRORs (it is the authority:
        this asks it)"""
        import vbtrules
        idx = {c: i for i, c in enumerate(self.d["clips"])}
        try:
            places = [(im.get("col", 0), im.get("row", 0), 1, 1, 0)
                      for im in self.d.get("images", [])]
            vbtrules.compile_rules(self, idx, places)
        except (vbtrules.RulesError, KeyError, ValueError, IndexError) as e:
            return [Problem("ERROR", "rules", "%s: %s" % (type(e).__name__, e))]
        return []


def main(argv=None):
    import argparse
    p = argparse.ArgumentParser(description="check a Video Battle project")
    p.add_argument("project")
    a = p.parse_args(argv)
    pr = Project.load(a.project)
    probs = pr.check()
    for x in probs:
        print(x)
    print("%d errors, %d warnings, %d not played yet" % (
        sum(x.level == "ERROR" for x in probs),
        sum(x.level == "WARN" for x in probs),
        sum(x.level == "INFO" for x in probs)))
    return 1 if any(x.level == "ERROR" for x in probs) else 0


if __name__ == "__main__":
    raise SystemExit(main())
