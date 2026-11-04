#!/usr/bin/env python3
"""vbtproj - a Video Battle PROJECT (docs/plans/VBATTLE-BUILDER-PLAN.md 2).

The project is the product; the builder's window and the command line are
two faces of it. This module is the model: what a battle is made of, loaded
from and saved to a JSON file, checked, and edited only through methods, so
that a script or a test reaches everything the window does. It needs no Tk
and does no analysis (tools/vbtana.py) and no building (tools/os88vbt.py
build).

A project file (version 1):

    {"format": "vbtproj", "version": 1, "name": ...,
     "target":   {"profile", "palette" (the CGA4 byte), "fps", "rate",
                  "canvas": [x, y, w, h], "band": [cell, row, cells, rows]},
     "sources":  {id: {"kind": "video", "path"} | {"kind": "scene", "name"}},
     "sounds":   {id: {"kind": "file", "path", "level"}
                    | {"kind": "scene", "name", "level"}
                    | {"kind": "source", "source", "in", "out", "level"}},
     "music":    {"sound", "join", "loop"} - the sample it comes in at, and
                 the loop [start, end) in the sound's samples (null: all
                 of it); the join is inside the loop,
     "ui_sounds":{"move"|"open"|"close"|"take"|"deny": sound id},
     "idle":     {"source", "loop": [a, b]} - source frames a..b-1, looped,
     "clips":    {id: {"source", "in", "out", "anchors": [k...], "exit",
                       "joins", "ends", "cues": [{"frame", "sound",
                       "level"}]}},
     "openings": [{"clip", "sound"}] - in turn; the last joins the loop,
     "characters": [{"name", "hp", "mp", "atk", "mag", "def", "mdef",
                     "evade", "menu": {"top": [ITEM], "side": [action]}}],
                   ITEM = {"action": id} | {"label", "sub": [action ids]},
     "actions":  {id: ACTION} - section 11's verbs (ACTION_KEYS),
     "enemy":    {"name", stats, "stances": [STANCE], "victory", "defeat"}}

Frames are the battle's frames (fps): a clip's "in" and "out" are its
first and last, inclusive, counted in the source as the encoder decodes it.
Paths are relative to the project file.
"""

import copy
import json
import os

FORMAT = "vbtproj"
VERSION = 1

UI_SOUNDS = ("move", "open", "close", "take", "deny")
VERBS = ("damage", "heal", "restore", "revive", "guard", "charge", "boost",
         "lower", "pass", "escape", "stance")
TARGETS = ("enemy", "self", "ally", "allies", "none", "character",
           "characters")
POOLS = ("hit", "miss", "defended", "critical", "finishing")
RULES = ("random", "lowest", "last", "alternate", "named")
STATS = ("hp", "mp", "atk", "mag", "def", "mdef", "evade")
MAX_CHARS = 4
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
        "characters": [], "actions": {},
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
    return d


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

    def save(self, path=None):
        path = os.path.abspath(path or self.path)
        tmp = path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(self.d, f, indent=1)   # (order is meaning: clip order)
            f.write("\n")
        os.replace(tmp, path)
        self.path = path

    def here(self, p):
        """A project-relative path, absolute"""
        if os.path.isabs(p) or not self.path:
            return p
        return os.path.normpath(os.path.join(os.path.dirname(self.path), p))

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

    def add_opening(self, clip, sound=None):
        def f(d):
            d["openings"].append({"clip": clip, "sound": sound})
        self._edit(f)

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
        return used

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
        return used

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
        bc, br, bn, bh = t["band"]
        if br < y + h and br + bh > y:
            err("target", "the band overlaps the canvas")
        for sid, s in d["sources"].items():
            if s["kind"] == "video" and self.path and \
                    not os.path.exists(self.here(s["path"])):
                err("sources." + sid, "%s is missing" % s["path"])
        for sid, s in d["sounds"].items():
            if s["kind"] == "file" and self.path and \
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
        for cid, c in d["clips"].items():
            w = "clips." + cid
            if c["source"] not in d["sources"]:
                err(w, "no source %s" % c["source"])
            if c["out"] < c["in"]:
                err(w, "out before in")
            opening = any(o["clip"] == cid for o in d["openings"])
            if not opening and not c["anchors"]:
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
        for i, o in enumerate(d["openings"]):
            if o["clip"] not in d["clips"]:
                err("openings.%d" % i, "no clip %s" % o["clip"])
            if o.get("sound") and o["sound"] not in d["sounds"]:
                err("openings.%d" % i, "no sound %s" % o["sound"])
        if not d["music"]:
            err("music", "no music")
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
            if not a["pools"].get("hit"):
                err(w, "no HIT clip")
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
        for cid in d["clips"]:
            if cid not in self.clips_used():
                warn("clips." + cid, "no action, opening or ending uses it")
        out += self.engine_gaps()
        return out

    def errors(self):
        return [p for p in self.check() if p.level == "ERROR"]

    # --- what today's engine plays (SPEC.md 125.6) -----------------------------
    def engine_gaps(self):
        """INFO for each thing the project says that the engine of today does
        not play yet: it plays the subset engine_script() makes"""
        d, out = self.d, []
        if len(d["characters"]) > 2:
            out.append(Problem("INFO", "characters",
                               "the engine plays the first 2 characters"))
        for ci, ch in enumerate(d["characters"][:2]):
            top = ch["menu"]["top"]
            if any("sub" in it for it in top) or ch["menu"]["side"]:
                out.append(Problem("INFO", "characters.%d" % ci,
                                   "sub-menus and the side menu are not "
                                   "played yet: their actions are listed "
                                   "flat, up to 4"))
        for aid, a in d["actions"].items():
            verbs = {e["verb"] for e in a["effects"]}
            if verbs - {"damage", "heal"}:
                out.append(Problem("INFO", "actions." + aid,
                                   "only DAMAGE and HEAL are played yet"))
            if any(len(p) > 1 for p in a["pools"].values()) or \
                    set(a["pools"]) - {"hit"}:
                out.append(Problem("INFO", "actions." + aid,
                                   "only the first HIT clip is played yet"))
        e = d["enemy"]
        if len(e["stances"]) > 1 or \
                sum(len(st["moves"]) for st in e["stances"]) > 1:
            out.append(Problem("INFO", "enemy",
                               "the enemy plays its first move only"))
        return out

    def engine_script(self, power_of=None):
        """The battle script the engine of today plays (os88vbt
        script_bytes): up to 2 members of up to 4 commands, each a clip and
        DAMAGE (K_HIT) or HEAL (K_HEAL) of an amount; the enemy's first
        move. POWER_OF(action) -> amount; by default an effect's "amount",
        else its "power" """
        d = self.d

        def amount(a):
            if power_of:
                return power_of(a)
            for e in a["effects"]:
                if e["verb"] in ("damage", "heal"):
                    return e.get("amount", e.get("power", 0))
            return 0

        def kind(a):
            vs = [e["verb"] for e in a["effects"]]
            return 1 if "heal" in vs and "damage" not in vs else 0

        party = []
        for ch in d["characters"][:2]:
            acts = []
            for it in ch["menu"]["top"]:
                acts += [it["action"]] if "action" in it else it["sub"]
            acts += ch["menu"]["side"]
            cmds = []
            for aid in acts[:4]:
                a = d["actions"][aid]
                cmds.append(dict(label=a["label"], clip=a["pools"]["hit"][0],
                                 kind=kind(a), amount=amount(a),
                                 mp=a["cost"].get("mp", 0)))
            party.append(dict(name=ch["name"], hp=ch["hp"], mp=ch["mp"],
                              commands=cmds))
        e = d["enemy"]
        mv = next(m for st in e["stances"] for m in st["moves"])
        ea = d["actions"][mv["action"]]
        return dict(enemy=dict(name=e["name"], hp=e["hp"],
                               clip=ea["pools"]["hit"][0], damage=amount(ea),
                               victory=e["victory"], defeat=e["defeat"]),
                    party=party, victory=e["victory"], defeat=e["defeat"])


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
