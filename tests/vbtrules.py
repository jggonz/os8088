#!/usr/bin/env python3
"""The Video Battle RULES' reference, host-side (tools/vbtrules.py, SPEC.md
126.6): the bytes, the arithmetic, the outcome and its clip, the turns -
everything the engine is held to by tests/vbtengine.py - and the builder's
rules tabs' arithmetic (tools/os88vbtgui.py, tools/vbtband.py).

    python3 tests/vbtrules.py

  1. THE GENERATOR is the 16-bit xorshift (7, 9, 8): every non-zero state
     once in 65,535 steps, and rnd(n) in 0..n-1.
  2. THE ARITHMETIC by hand: a physical hit, a magical one, the stance's
     and the steps' share of a stat, the caps, a fixed amount, a heal by
     power and by percent.
  3. THE OUTCOME AND ITS CLIP: finishing over critical over defended over
     missed over hit; an empty pool falls back to HIT; a character's own
     pool before the general one.
  4. THE COMPILER refuses what the engine cannot hold: a fifth effect, a
     ninth menu item, a stance with no move, an unknown stance.
  5. THE RULES BATTLE (tests/vbtrulesdemo.py) played in the reference
     covers every rule it was made to, ends, and is the same twice.
  6. THE BUILDER'S ARITHMETIC: every verb's form round-trips; the damage
     estimate is the reference's own; the band preview puts the arrow
     where the engine does.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))
import vbtrules as V        # noqa: E402
import vbtrulesdemo as D    # noqa: E402

fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


# 1
r = V.Rng(1)
seen = set()
for _ in range(65535):
    seen.add(r.step())
ok(len(seen) == 65535 and 0 not in seen and r.x == 1,
   "the xorshift visits every non-zero state once in 65,535 steps")
r = V.Rng(77)
ok(all(0 <= r.rnd(21) < 21 for _ in range(5000)), "rnd(21) is in 0..20")
x = V.Rng(9)
p = x.peek()
ok(x.step() == p and x.peek() != p, "a peek is the next step, not taken")

# 2
proj = D.make_project()
data, R = D.bundle(proj)
s = V.Sim(R, seed=1)
ari, bram, E = 0, 1, V.ENEMY
# ARI ATK 30, the beetle DEF 50: (40*30)>>4 = 75; (75*206)>>8 = 60
ok(s.damage(V.KIND_PHYS, 40, ari, E, 100) == 60, "Attack: 40*30>>4, *206>>8 = 60")
ok(s.damage(V.KIND_PHYS, 40, ari, E, 110) == 66, "...at variance 110: 66")
# BRAM MAG 30, the beetle MDEF 30: (32*30)>>4 = 60; (60*226)>>8 = 52
ok(s.damage(V.KIND_MAG, 32, bram, E, 100) == 52, "Fire: 32*30>>4, *226>>8 = 52")
s.stance = 1                        # shell: DEF +60%
ok(s.stat(E, 2) == 80, "the shell stance: DEF 50 * 160 / 100 = 80")
s.mods[ari][0] = 2
ok(s.stat(ari, 0) == 37, "two steps up: ATK 30 * 10 / 8 = 37")
s.mods[ari][0] = -4
ok(s.stat(ari, 0) == 15, "four down: ATK 30 * 4 / 8 = 15")
s.mods[ari][0] = 0
ok(s.damage(V.KIND_FIXED, 12345, ari, E, 100) == V.CAP, "a fixed amount is capped at 9,999")
ok(s.damage(V.KIND_PHYS, 0xFFFF, ari, E, 110) == V.CAP, "...and a huge one")
ok(s.heal_amount(V.HEAL_POWER, 40, bram, bram, 100) == 75,
   "Heal: 40*30>>4 = 75")
ok(s.heal_amount(V.HEAL_PERCENT, 25, bram, ari, 100) == 105,
   "a 25% heal of 420 HP: 105")

# 3
s = V.Sim(R, seed=5)
a = dict(R.acts[0])
names = R.act_ids
lunge = names.index("lunge")
la = R.acts[lunge]
ok(s.clip_of(la, 1, None, 0) == R.pools[la["pools"][0]][0],
   "a MISS with no miss pool plays the HIT clip")
fake = dict(la, pools=[la["pools"][0], la["pools"][0], V.NONE, V.NONE, V.NONE],
            by=[[V.NONE] * 5 for _ in range(4)])
R.pools.append([7, 8])
fake["by"][1][0] = len(R.pools) - 1
ok(s.clip_of(fake, 0, 1, 0) == 7 and s.clip_of(fake, 0, 1, 0xFFFF) == 8,
   "a character's own HIT pool before the general one, picked by the draw")
ok(s.clip_of(fake, 0, 0, 0) == R.pools[la["pools"][0]][0],
   "...and the general one for another character")
R.pools.pop()
# precedence: an attack that finishes, with a critical, is FINISHING
s.ehp = 10
s.rng = V.Rng(3)
plan = s.resolve(names.index("attack"), 0)
ok(plan["outcome"] == 4, "an attack that leaves the enemy at 0 is FINISHING")
s = V.Sim(R, seed=3)
s.guard = [1, 1]
outs = set()
for seed in range(1, 200):
    s.rng = V.Rng(seed)
    outs.add(s.resolve(lunge, E, 0)["outcome"])
ok(outs == {1, 2, 3} or outs >= {1, 2},
   "a lunge at a guard is DEFENDED or MISSED (or CRITICAL): %s" % sorted(outs))

# 4
import copy                         # noqa: E402
import vbtproj as P                 # noqa: E402


def refused(fn, what):
    q = P.Project(copy.deepcopy(proj.d))
    fn(q)
    idx = {c: i for i, c in enumerate(q.d["clips"])}
    try:
        V.compile_rules(q, idx, [])
        ok(False, what)
    except (V.RulesError, KeyError, ValueError):
        ok(True, what)
refused(lambda q: q.d["actions"]["attack"]["effects"].extend([{"verb": "guard"}] * 4),
        "a fifth effect is refused")
refused(lambda q: q.d["characters"][0]["menu"]["top"].extend([{"action": "attack"}] * 8),
        "a ninth menu item is refused")
refused(lambda q: q.d["enemy"]["stances"][1].__setitem__("moves", []),
        "a stance with no move is refused")
refused(lambda q: q.d["actions"]["shell"]["effects"][0].__setitem__("stance", "nope"),
        "a stance that does not exist is refused")

# 5


def play(seed):
    sim = V.Sim(R, seed=seed)
    g = D.battle(sim, R)
    per = {0: 0, 1: 0}
    states = []
    try:
        c, st = next(g)
        while True:
            states.append(st)
            ai = D.choose(sim, R, c, per[c])
            per[c] += 1
            c, st = g.send(ai)
    except StopIteration:
        pass
    return sim, states
sim, states = play(D.SEED)
got = set()
for pl in sim.log:
    got.add(names[pl["action"]])
    if pl.get("response"):
        got.add("response")
    got.add(("hit", "miss", "defended", "critical", "finishing")[pl["outcome"]])
need = {"shell", "roar", "response", "swarm", "miss", "critical", "heal",
        "potion", "revive", "limit", "guard", "focus", "bolt", "fire", "mark"}
# THE FOLLOW-UP: every Mark of the enemy's own turn followed, at its next,
# by a Snap at the same character while that one stands
own = [pl for pl in sim.log if pl["who"] == V.ENEMY and not pl.get("response")]
folw = [(own[i], own[i + 1]) for i in range(len(own) - 1)
        if names[own[i]["action"]] == "mark"]
ok(folw and all(names[b["action"]] == "snap" and
                (b["targets"] == a["targets"] or not any(
                    sim.hp[t] for t in a["targets"]))
                for a, b in folw),
   "every Mark is FOLLOWED by a Snap at its target (%d times)" % len(folw))
ok(need <= got, "the rules battle covers every rule (missing: %s)" %
   sorted(need - got))
ok(sim.over in ("victory", "defeat"), "...and ends (%s, %d turns)"
   % (sim.over, len(states)))
sim2, states2 = play(D.SEED)
ok(states == states2, "...and is the same played twice")
ok(any(st["stance"] == 1 for st in states) and any(st["stance"] == 2 for st in states),
   "it goes into the shell and comes back, and is enraged below 40%")

# THE LIMIT EFFECT (126.6): ARI's Attack fills ARI's own gauge 10 points
# (to: self), BRAM's Heal the healed one's 20 (to: target)
s = V.Sim(R, seed=5)
l0 = s.lim[0]
s.apply(s.resolve(names.index("attack"), 0))
l1 = s.lim[0]
s.hp[0] = 100
s.apply(s.resolve(names.index("heal"), 1, 0))
ok(l1 == min(100, l0 + 10) and s.lim[0] == min(100, l1 + 20)
   and s.lim[1] == 0,
   "LIMIT: Attack fills its doer's gauge %d -> %d, Heal the healed one's "
   "-> %d, the healer's untouched" % (l0, l1, s.lim[0]))

# 6
import os88vbtgui as G          # noqa: E402
for verb, fields in G.EFFECT_FIELDS.items():
    e = G.effect_from(verb, {k: d for k, _, _, d, _ in fields})
    ok(G.effect_from(verb, G.effect_form(e)) == e and e["verb"] == verb,
       "the %s form round-trips" % verb)
lines = G.damage_preview(proj, "attack")
want = "ARI on BEETLE: %d to %d" % (V.Sim(R).damage(V.KIND_PHYS, 40, 0, E, 90),
                                    V.Sim(R).damage(V.KIND_PHYS, 40, 0, E, 110))
ok(lines and lines[0].startswith(want), "the estimate is the reference's: %s" % lines[0])
import vbtband                  # noqa: E402
im = vbtband.render(proj, 1, "Help", cursor=1)
by = proj.d["target"]["band"][1]
lk = proj.d["menu_looks"]["BRAM:Help"]
px = im.load()
row = lk["row"] + 1 * lk["pitch"] + 3       # the second line's glyph rows
ink = [px[x, row] for x in range(lk["col"] * 8, lk["col"] * 8 + 8)]
blank = [px[x, lk["row"] + 3] for x in range(lk["col"] * 8, lk["col"] * 8 + 8)]
ok(im.size == (320, 200 - by), "the preview is the band: 320 x %d" % (200 - by))
ok(len(set(ink)) > 1 and len(set(blank)) == 1,
   "the arrow is on the cursor's line, at the menu's column, and not on the first")

if fails:
    print("%d FAILED" % len(fails))
    sys.exit(1)
print("all passed")
