#!/usr/bin/env python3
"""A MISSING CLIP IS WARNED OF ONLY WHERE THE BATTLE CAN GO (SPEC.md 125.6,
the builder's checks), host side.

    python3 tests/vbtreach.py

The owner: "I'm getting warnings for actions that a character doesn't have
access to, that there is no clip for that character attached to the
action ... Similar thing for actions only targeted against a particular
character; it doesn't matter that a path unreachable has no clip attached
to it." The demo battle, each action's HIT clips given per character
(by_target) and none of its own:

  1. ARI's Attack, ARI's clip alone: no warning (BRAM has no Attack);
     Attack put in BRAM's menu too: warned, naming BRAM;
  2. the enemy's Lunge, ARI's clip alone, the move aimed by "alternate":
     warned, naming BRAM; the move "named" ARI: no warning; a follow-up
     "same" after it aims where it did, and one "named" BRAM is warned.

  3. the party line's fields as wide as the party's own values: HP 300
     with its "/300" at column 26 and MP at 35 do not overlap (the widest
     HP field, 11 cells, would); MP at 32 does, and is said.

Broken on purpose (vbtproj.clip_chars answering every character, as the
check did before) 1's first and 2's second go red; the widths back at
their widest, 3 goes red.
"""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88vbt as B         # noqa: E402
import vbtproj as P         # noqa: E402

fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def hit_warn(p, aid):
    return [x.text for x in p.check()
            if x.where == "actions." + aid and "no HIT clip" in x.text]


def main():
    with tempfile.TemporaryDirectory() as t:
        B.demo_project(os.path.join(t, "s.vbp"))
        p = P.Project.load(os.path.join(t, "s.vbp"))
    d = p.d
    a = d["actions"]["attack"]
    a["by_target"] = {"ARI": {"hit": a["pools"].pop("hit")}}
    w = hit_warn(p, "attack")
    ok(not w, "1. ARI's Attack with ARI's clip alone: no warning (%s)" % w)
    d["characters"][1]["menu"]["top"].append({"action": "attack"})
    w = hit_warn(p, "attack")
    ok(len(w) == 1 and "BRAM" in w[0] and "ARI" not in w[0],
       "1. ...in BRAM's menu too: warned of BRAM's (%s)" % w)
    lg = d["actions"]["lunge"]
    lg["by_target"] = {"ARI": {"hit": lg["pools"].pop("hit")}}
    mv = d["enemy"]["stances"][0]["moves"][0]
    w = hit_warn(p, "lunge")
    ok(len(w) == 1 and "BRAM" in w[0],
       "2. the enemy's Lunge at anybody, ARI's clip alone: BRAM's warned "
       "(%s)" % w)
    mv.update(rule="named", target="ARI")
    w = hit_warn(p, "lunge")
    ok(not w, "2. ...the move named ARI: no warning (%s)" % w)
    d["enemy"]["follow_ups"] = [dict(after="lunge", then="lunge",
                                     target="same")]
    w1 = hit_warn(p, "lunge")
    d["enemy"]["follow_ups"][0].update(target="named", named="BRAM")
    w2 = hit_warn(p, "lunge")
    ok(not w1 and len(w2) == 1 and "BRAM" in w2[0],
       "2. a follow-up \"same\" aims where the move did (%s); one named "
       "BRAM is warned (%s)" % (w1, w2))
    # 3. THE PARTY'S FIELDS as wide as the party's values (the owner: "they
    # overlap because of the max possible values ... but they don't
    # actually"): HP with its "/max" at column 26, MP at 35
    for c in d["characters"]:
        c["hp"], c["mp"] = 300, 40
    d["band_layout"] = dict(hp_col=26, hpmax_show=1, mp_col=35)
    lw = [x.text for x in p.check() if x.where == "band_layout"
          and "overlap" in x.text]
    d["band_layout"]["mp_col"] = 32
    lw2 = [x.text for x in p.check() if x.where == "band_layout"
           and "overlap" in x.text]
    ok(not lw and len(lw2) == 1 and "HP" in lw2[0] and "MP" in lw2[0],
       "3. HP 300 '/300' at 26 and MP at 35: no overlap (%s); MP at 32, "
       "inside '/300': said (%s)" % (lw, lw2))
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
