#!/usr/bin/env python3
"""The RULES test battle (SPEC.md 125.6): the demo's scene clips with a rule
set that exercises everything the engine plays - a top menu, a sub-menu and
a side menu; physical, magical and limit damage; an ally aimed at; MP, item
and limit costs; heal, revive, guard and a stat boost; the enemy's three
stances (a timed one, an automatic change below 40% HP), weighted moves at
four target rules, an area attack, a miss chance, and a counter after a
physical hit. tests/vbtrules.py plays it in the reference; tests/vbtengine.py
plays it on MartyPC against the reference.

    make_project(path=None, seed=SEED) -> a vbtproj.Project
    PLAN = {character: [action ids]} - what the party tries, in turn
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))
import os88vbt as B     # noqa: E402

SEED = 4421               # (a battle that covers every rule: tests/vbtrules.py)
PLAN = {0: ["attack", "focus", "bolt", "limit", "attack", "guard"],
        1: ["fire", "heal", "potion", "revive", "fire", "guard"]}


def dmg(power, kind="physical", hits=1):
    return {"verb": "damage", "kind": kind, "power": power, "hits": hits}


def make_project(path=None, seed=SEED):
    p = B.demo_project(None)
    p.set(("name",), "The rules battle")
    p.set(("seed",), seed)
    p.set(("actions",), {})
    p.set(("characters",), [])

    def act(aid, label, target="enemy", effects=(), cost=None, acc=100, **pools):
        p.add_action(aid, label, target=target, effects=list(effects),
                     cost=cost, accuracy=acc, pools=pools)
    act("attack", "Attack", effects=[dmg(40)], hit=["ari_attack"])
    act("bolt", "Bolt", effects=[dmg(34, "magical")], cost={"mp": 8},
        hit=["ari_bolt"])
    act("limit", "Limit", effects=[dmg(70, hits=2)], cost={"limit": True},
        hit=["ari_limit"])
    act("guard", "Guard", target="self", effects=[{"verb": "guard"}])
    act("focus", "Focus", target="self",
        effects=[{"verb": "boost", "stat": "atk", "steps": 2, "turns": 3}])
    act("fire", "Fire", effects=[dmg(32, "magical")], cost={"mp": 6},
        hit=["bram_attack"])
    act("heal", "Heal", target="ally", effects=[{"verb": "heal", "power": 40}],
        cost={"mp": 8}, hit=["bram_heal"])
    act("potion", "Potion", target="ally",
        effects=[{"verb": "heal", "amount": 150}],
        cost={"item": "Potion", "n": 1}, hit=["bram_heal"])
    act("revive", "Revive", target="ally",
        effects=[{"verb": "revive", "percent": 40}], cost={"mp": 12},
        hit=["bram_heal"])
    act("lunge", "Lunge", target="character", effects=[dmg(44)], acc=85,
        hit=["beetle_attack"])
    act("swarm", "Swarm", target="characters", effects=[dmg(26)],
        hit=["beetle_attack"])
    act("snap", "Snap", target="character", effects=[dmg(28)],
        hit=["beetle_attack"])
    act("shell", "Shell", target="none",
        effects=[{"verb": "stance", "stance": "shell", "duration": 2}])
    act("roar", "Roar", target="self",
        effects=[{"verb": "boost", "stat": "atk", "steps": 1, "turns": 3}])
    p.set(("items",), [{"name": "Potion", "count": 2}])
    ari = p.add_character("ARI", hp=420, mp=40, atk=30, mag=14,
                          **{"def": 40, "mdef": 20, "evade": 5})
    for a in ("attack", "bolt", "limit"):
        p.menu_add(ari, action=a)
    for a in ("guard", "focus"):
        p.menu_add(ari, action=a, side=True)
    bram = p.add_character("BRAM", hp=800, mp=50, atk=18, mag=30,
                           **{"def": 30, "mdef": 40, "evade": 10})
    p.menu_add(bram, action="fire")
    p.menu_add(bram, label="Help", sub=["heal", "potion", "revive"])
    p.menu_add(bram, action="guard", side=True)
    p.set(("menu_looks",), {"BRAM:Help": {"col": 2, "row": 4, "pitch": 9,
                                          "ink": 3, "cursor": "arrow"}})
    e = dict(p.d["enemy"])
    e.update(name="BEETLE", hp=1100, mp=0, atk=30, mag=10, evade=5,
             start_stance="normal")
    e["def"], e["mdef"] = 50, 30
    e["stances"] = [
        {"name": "normal",
         "moves": [{"action": "lunge", "weight": 5, "rule": "alternate"},
                   {"action": "swarm", "weight": 2},
                   {"action": "shell", "weight": 2}],
         "responses": [{"trigger": "physical", "chance": 35,
                        "action": "snap"}],
         "auto": {"kind": "hp_below", "arg": 40, "to": "enraged",
                  "via": "roar"}},
        {"name": "shell", "modifiers": {"def": 60},
         "moves": [{"action": "lunge", "weight": 1, "rule": "lowest"}]},
        {"name": "enraged", "modifiers": {"atk": 25},
         "moves": [{"action": "lunge", "weight": 3, "rule": "random"},
                   {"action": "swarm", "weight": 1}]}]
    p.set(("enemy",), e)
    if path:
        p.save(path)
    return p


def first_target(sim, R, ai):
    """The character an ally-aimed action lands on when the player takes
    the first the menu offers (vbgame.inc's vb_tnext_sel from none: the
    first standing, or the first fallen for a REVIVE); None: none"""
    import vbtrules as V
    a = R.acts[ai]
    fallen = any(e[0] == V.VERBS["revive"] for e in a["effects"])
    for i in range(R.nchar):
        if (sim.hp[i] == 0) == fallen:
            return i
    return None


def choose(sim, R, c, k):
    """Character C's K-th choice: a REVIVE when someone has fallen and it
    can pay; else the plan's, or the first of the plan it can pay for and
    aim; the action's index"""
    import vbtrules as V
    names = [a for a in R.act_ids]
    plan = PLAN[c]
    want = names.index(plan[k % len(plan)])
    revive = [names.index(x) for x in plan if any(
        e[0] == V.VERBS["revive"] for e in R.acts[names.index(x)]["effects"])]
    for ai in revive + [want] + [names.index(x) for x in plan]:
        a = R.acts[ai]
        if not sim.can_pay(ai, c):
            continue
        if a["target"] == V.TARGETS["ally"] and first_target(sim, R, ai) is None:
            continue
        return ai
    raise SystemExit("character %d can take nothing of its plan" % c)


def battle(sim, R):
    """The reference battle as a generator: yields (character, its state)
    at every character's turn, and is sent the action it takes"""
    import vbtrules as V
    while not sim.over:
        for c in range(R.nchar):
            if sim.hp[c] == 0:
                continue
            sim.start_turn(c)
            ai = yield c, state(sim, c)
            a = R.acts[ai]
            sim.take(ai, c, first_target(sim, R, ai)
                     if a["target"] == V.TARGETS["ally"] else None)
            if sim.over:
                return
        sim.enemy_turn()


def state(sim, c):
    return dict(turn=c, ehp=sim.ehp, hp=list(sim.hp), mp=list(sim.mp),
                lim=list(sim.lim), stance=sim.stance, rng=sim.rng.x)


def bundle(proj):
    """The project's bundle, and its rules with their action ids"""
    import vbtrules as V
    data = B.build_project(proj)
    R = B.Bundle(data).rules
    R.act_ids = list(proj.d["actions"])
    return data, R
