#!/usr/bin/env python3
"""vbtrules - a Video Battle's RULES (SPEC.md 125.6, VBATTLE-BUILDER-PLAN
11): the bytes the engine interprets, the compiler that makes them from a
project, and the REFERENCE the engine is held to.

The engine (apps/vbattle/) and this module must agree to the hit point:
the same random generator, drawn in the same order, the same integer
arithmetic with the same caps. `Sim` is that reference, run on the bytes
`compile_rules` makes (never on the project), so a test can play a battle
here and on MartyPC and compare every number (tests/vbtrules.py, and the
soak row that drives the engine).

THE RULES BLOCK sits at offset 4096 of the bundle's first region (SPEC.md
125.2), 12 KB at most, little-endian, every table at an offset its header
gives:

  header    64 bytes     counts, table offsets, the enemy, the seed
  chars     32 a char    name, HP MP ATK MAG DEF MDEF, EVADE, menus
  actions   72 each      label, target, cost, accuracy, 4 effects of 6
                         bytes, 5 pools, and 5 pools per character
  menus     32 each      label, items (an action, or a sub-menu), the look
  stances   56 each      name, modifiers, 6 moves, 2 responses, an
                         automatic change, the return
  pools     8 each       a count and up to 7 clips
  items     12 each      name, the party's count
  images    8 each       a band picture's place and its paragraph in the
                         IMAGES region

THE RANDOM GENERATOR is a 16-bit xorshift (7, 9, 8), seeded from the
header's seed when its flag says so (a test's battle is the same every
time) and from the clock otherwise. rnd(n) advances it and gives
(x * n) >> 16. A draw is taken only where DRAWS below says, in that order.
"""

import struct

RULES_AT = 4096
RULES_MAX = 12288
HDR = 64
CHAR_SZ, ACT_SZ, MENU_SZ, STANCE_SZ, POOL_SZ, ITEM_SZ, IMG_SZ = \
    32, 72, 32, 56, 8, 12, 8
MAX_CHARS, MAX_ACTS, MAX_MENUS, MAX_STANCES, MAX_POOLS, MAX_ITEMS, \
    MAX_IMGS = 4, 96, 32, 8, 255, 8, 8
MAX_ITEMS_MENU, MAX_MOVES, MAX_RESP, MAX_POOL = 8, 6, 2, 7
MAX_FOLLOW = 16                 # the enemy's FOLLOW-UPS (125.6.7)
MAX_PLACES = 64                 # menu items placed one by one (125.5.1)
# THE BAND'S LAYOUT (125.5.1): every part of the band at a column (cells,
# 0..39) and a row (pixels from the band's top), each shown or hidden - in
# this order, a byte each, in the rules block (header 56: its offset, 0
# none, every older block). The defaults are the band as it always was
LAYOUT = ("enemy_col", "enemy_row", "enemy_show", "party_row",
          "party_pitch", "name_col", "name_show", "limit_col", "limit_show",
          "hp_col", "hp_show", "hpmax_show", "mp_col", "mp_show", "msg_col",
          "msg_row", "msg_show", "lines_show", "divider_col")
LAYOUT_SZ = 20


def layout_defaults(by):
    """The band's layout as it always was, for a band from row BY"""
    return dict(enemy_col=18, enemy_row=3, enemy_show=1, party_row=12,
                party_pitch=9, name_col=18, name_show=1, limit_col=24,
                limit_show=1, hp_col=25, hp_show=1, hpmax_show=1, mp_col=36,
                mp_show=1, msg_col=1, msg_row=191 - by, msg_show=1,
                lines_show=1, divider_col=16)


def layout_of(proj):
    """The project's band layout, every field (its band_layout over the
    defaults)"""
    lay = layout_defaults(proj.d["target"]["band"][1])
    for k, v in (proj.d.get("band_layout") or {}).items():
        if k in lay:
            lay[k] = int(v)
    return lay
FOLLOW_SAME = 0xFF              # ...a follow-up's target: the same one
NONE = 0xFF

VERBS = {"damage": 1, "heal": 2, "restore": 3, "revive": 4, "guard": 5,
         "charge": 6, "boost": 7, "lower": 8, "pass": 9, "escape": 10,
         "stance": 11}
TARGETS = {"enemy": 0, "self": 1, "ally": 2, "allies": 3, "none": 4,
           "character": 5, "characters": 6}
RULES = {"alternate": 0, "random": 1, "lowest": 2, "last": 3, "named": 4}
POOLS = ("hit", "miss", "defended", "critical", "finishing")
STATS = ("atk", "mag", "def", "mdef")
KIND_PHYS, KIND_MAG, KIND_FIXED = 0, 1, 2
HEAL_POWER, HEAL_PERCENT, HEAL_FIXED = 0, 1, 2
COST_NONE, COST_MP, COST_ITEM, COST_LIMIT = 0, 1, 2, 3
TRIG = {"any": 0, "physical": 1, "magical": 2, "named": 3}
AUTO = {"none": 0, "hp_below": 1, "after_turns": 2}
AF_PHYS, AF_MAG = 1, 2          # action flags: what a response sees
MF_ARROW = 1                    # menu flags: an arrow cursor, not inverse
CAP = 9999


STAT_MAX = 999       # ATK, MAG, DEF, MDEF: the arithmetic's 32/16
                     # divides stay in range (SPEC.md 125.6.2)


def stat(v):
    return max(0, min(STAT_MAX, int(v)))


def name_bytes(s, n, upper=True):
    s = s.upper() if upper else s
    return s.encode("ascii", "replace")[:n].ljust(n, b" ")


# --- the compiler ---------------------------------------------------------------

class RulesError(ValueError):
    pass


def compile_rules(proj, clip_index, images=(), preview=False):
    """The RULES block for project PROJ (a vbtproj.Project), its clips
    numbered by CLIP_INDEX {clip id: bundle index}. IMAGES: [(col, row,
    cells, rows, paragraph)] of the band pictures, in the order the
    project's "images" list names them. PREVIEW (the band's, vbtband): the
    victory and defeat clips may be unchosen yet - the band never plays
    them. (The list goes on:)
    project's "images" list names them. Returns the bytes"""
    d = proj.d
    chars = d["characters"]
    acts = list(d["actions"])
    if not 1 <= len(chars) <= MAX_CHARS:
        raise RulesError("%d characters" % len(chars))
    if len(acts) > MAX_ACTS:
        raise RulesError("%d actions, at most %d" % (len(acts), MAX_ACTS))
    aidx = {a: i for i, a in enumerate(acts)}

    def need(tab, key, what, kind):
        """TAB[KEY], or a RulesError that says WHAT names nothing - a field
        left unchosen in the window was a KeyError(None), which the window
        showed as "None" (the owner, 2026-11-04)"""
        if key in tab:
            return tab[key]
        if key in (None, ""):
            raise RulesError("%s: no %s chosen" % (what, kind))
        raise RulesError("%s: no %s called %s" % (what, kind, key))
    cname = [c["name"] for c in chars]
    stances = d["enemy"]["stances"]
    sidx = {s["name"]: i for i, s in enumerate(stances)}
    items = d.get("items", [])
    iidx = {it["name"]: i for i, it in enumerate(items)}
    imgs = [im["name"] for im in d.get("images", [])]
    pools, pool_of = [], {}

    def pool(clips):
        clips = [need(clip_index, c, "an action's clips", "clip")
                 for c in clips]
        if not clips:
            return NONE
        if len(clips) > MAX_POOL:
            raise RulesError("a pool of %d clips, at most %d" %
                             (len(clips), MAX_POOL))
        k = tuple(clips)
        if k not in pool_of:
            pool_of[k] = len(pools)
            pools.append(k)
            if len(pools) > MAX_POOLS:
                raise RulesError("more than %d pools" % MAX_POOLS)
        return pool_of[k]

    # menus: every character's top (and its sub-menus) and side menu
    menus = []

    def menu(label, items_, look, kind_items):
        m = dict(label=label, items=kind_items, look=look or {})
        menus.append(m)
        if len(menus) > MAX_MENUS:
            raise RulesError("more than %d menus" % MAX_MENUS)
        return len(menus) - 1

    looks = d.get("menu_looks", {})
    cmenus = []
    for c in chars:
        top_items = []
        top = menu(c["name"], None, looks.get(c["name"] + ":top"), top_items)
        for it in c["menu"]["top"]:
            if "action" in it:
                top_items.append((0, need(aidx, it["action"], "%s's menu"
                                          % c["name"], "action")))
            else:
                sub = menu(it["label"], None,
                           looks.get(c["name"] + ":" + it["label"]),
                           [(0, need(aidx, a, "%s's %s menu" % (
                               c["name"], it["label"]), "action"))
                            for a in it["sub"]])
                top_items.append((1, sub))
        side = NONE
        if c["menu"]["side"]:
            side = menu("side", None, looks.get(c["name"] + ":side"),
                        [(0, need(aidx, a, "%s's side menu" % c["name"],
                                  "action")) for a in c["menu"]["side"]])
        cmenus.append((top, side))
    for m in menus:
        if len(m["items"]) > MAX_ITEMS_MENU:
            raise RulesError("menu %s: %d items, at most %d" %
                             (m["label"], len(m["items"]), MAX_ITEMS_MENU))

    out = bytearray(HDR)
    tabs = {}

    def table(key, blob):
        tabs[key] = len(out)
        out.extend(blob)

    # characters
    b = bytearray()
    for c, (top, side) in zip(chars, cmenus):
        r = bytearray(CHAR_SZ)
        r[0:8] = name_bytes(c["name"], 8)
        struct.pack_into("<6H", r, 8, max(1, min(0xFFFF, c["hp"])),
                         min(0xFFFF, c["mp"]), *[stat(v) for v in (
                             c.get("atk", 16), c.get("mag", 16),
                             c.get("def", 0), c.get("mdef", 0))])
        r[20] = min(100, c.get("evade", 0))
        r[21], r[22] = top, side
        b += r
    table("char", b)
    # actions
    b = bytearray()
    for aid in acts:
        a = d["actions"][aid]
        r = bytearray(ACT_SZ)
        r[0:8] = name_bytes(a["label"], 8, False)
        r[8] = TARGETS[a["target"]]
        cost = a.get("cost") or {}
        if cost.get("mp"):
            r[9], n = COST_MP, cost["mp"]
        elif cost.get("item"):
            r[9], n = COST_ITEM, cost.get("n", 1)
            r[12] = need(iidx, cost["item"], "action %s's cost" % aid,
                         "item")
        elif cost.get("limit"):
            r[9], n = COST_LIMIT, 0
        else:
            r[9], n = COST_NONE, 0
        struct.pack_into("<H", r, 10, n)
        r[13] = max(0, min(100, a.get("accuracy", 100)))
        fl = 0
        effs = a["effects"]
        if len(effs) > 4:
            raise RulesError("action %s: %d effects, at most 4" % (aid, len(effs)))
        for i, e in enumerate(effs):
            v = VERBS[e["verb"]]
            at = 16 + 6 * i
            aa, p, bb, cc = 0, 0, 0, 0
            if e["verb"] == "damage":
                if "amount" in e:
                    aa, p = KIND_FIXED, e["amount"]
                else:
                    aa = KIND_MAG if e.get("kind") == "magical" else KIND_PHYS
                    p = e.get("power", 16)
                bb = max(1, min(4, e.get("hits", 1)))
                fl |= AF_MAG if aa == KIND_MAG else AF_PHYS
            elif e["verb"] == "heal":
                if "amount" in e:
                    aa, p = HEAL_FIXED, e["amount"]
                elif "percent" in e:
                    aa, p = HEAL_PERCENT, min(100, e["percent"])
                else:
                    aa, p = HEAL_POWER, e.get("power", 16)
            elif e["verb"] == "restore":
                p = e.get("amount", e.get("power", 0))
            elif e["verb"] == "revive":
                p = e.get("percent", 25)
            elif e["verb"] in ("boost", "lower"):
                aa = STATS.index(e.get("stat", "atk"))
                bb = max(1, min(4, e.get("steps", 1)))
                cc = e.get("turns", 3)
            elif e["verb"] == "stance":
                aa = need(sidx, e["stance"], "action %s's stance effect"
                          % aid, "stance")
                cc = e.get("duration", 0)
            r[at] = v
            r[at + 1] = aa
            struct.pack_into("<H", r, at + 2, min(0xFFFF, p))
            r[at + 4], r[at + 5] = bb, cc
        r[14] = fl
        for k, pk in enumerate(POOLS):
            r[40 + k] = pool(a["pools"].get(pk, []))
        for ci, nm in enumerate(cname):
            per = a.get("by_target", {}).get(nm, {})
            for k, pk in enumerate(POOLS):
                r[48 + 5 * ci + k] = pool(per.get(pk, []))
        for ci in range(len(cname), MAX_CHARS):
            for k in range(5):
                r[48 + 5 * ci + k] = NONE
        b += r
    table("act", b)
    # menus
    b = bytearray()
    for m in menus:
        r = bytearray(MENU_SZ)
        lk = m["look"]
        r[0:8] = name_bytes(m["label"], 8, False)
        r[8] = len(m["items"])
        r[9] = imgs.index(lk["image"]) if lk.get("image") in imgs else NONE
        r[10] = lk.get("col", 1)
        r[11] = lk.get("row", 4)
        r[12] = lk.get("pitch", 9)
        r[13] = lk.get("ink", 3)
        r[14] = MF_ARROW if lk.get("cursor") == "arrow" else 0
        for i, (kind, idx) in enumerate(m["items"]):
            struct.pack_into("<BB", r, 16 + 2 * i, idx, kind)
        b += r
    table("menu", b)
    # stances
    b = bytearray()
    if len(stances) > MAX_STANCES:
        raise RulesError("%d stances" % len(stances))
    for s in stances:
        r = bytearray(STANCE_SZ)
        r[0:8] = name_bytes(s["name"], 8)
        mods = s.get("modifiers", {})
        for i, st in enumerate(STATS):
            struct.pack_into("<b", r, 8 + i, max(-100, min(127, mods.get(st, 0))))
        moves, resp = s["moves"], s.get("responses", [])
        if not moves:
            raise RulesError("stance %s has no move" % s["name"])
        if len(moves) > MAX_MOVES or len(resp) > MAX_RESP:
            raise RulesError("stance %s: %d moves, %d responses" %
                             (s["name"], len(moves), len(resp)))
        r[12], r[13] = len(moves), len(resp)
        w = "the enemy's stance %s" % s["name"]
        r[14] = need(aidx, s["return"], w + " (its return)", "action") \
            if s.get("return") else NONE
        for i, m in enumerate(moves):
            if m.get("rule") == "named" and m.get("target") not in cname:
                raise RulesError("%s: move %d aims at no character (%s)"
                                 % (w, i + 1, m.get("target")))
            arg = cname.index(m["target"]) if m.get("rule") == "named" else 0
            struct.pack_into("<4B", r, 16 + 4 * i, need(
                aidx, m.get("action"), "%s, move %d" % (w, i + 1), "action"),
                             max(1, min(255, m.get("weight", 1))),
                             RULES[m.get("rule", "alternate")], arg)
        for i, x in enumerate(resp):
            trig = x.get("trigger", "any")
            arg = need(aidx, x.get("named"), "%s, response %d" % (w, i + 1),
                       "action it answers") if trig == "named" else 0
            struct.pack_into("<4B", r, 40 + 4 * i, TRIG[trig], arg,
                             max(0, min(100, x.get("chance", 100))),
                             need(aidx, x.get("action"), "%s, response %d"
                                  % (w, i + 1), "action"))
        au = s.get("auto") or {}
        if au:
            struct.pack_into("<4B", r, 48, AUTO[au["kind"]], au["arg"],
                             need(sidx, au.get("to"), w + " (its change)",
                                  "stance"),
                             need(aidx, au["via"], w + " (its change)",
                                  "action") if au.get("via") else NONE)
        b += r
    table("stance", b)
    # pools, items, images
    b = bytearray()
    for p in pools:
        b += bytes([len(p)] + list(p) + [0] * (MAX_POOL - len(p)))
    table("pool", b)
    b = bytearray()
    for it in items[:MAX_ITEMS]:
        r = bytearray(ITEM_SZ)
        r[0:8] = name_bytes(it["name"], 8, False)
        struct.pack_into("<H", r, 8, it.get("count", 1))
        b += r
    table("item", b)
    b = bytearray()
    for col, row, cells, rows, para in images:
        b += struct.pack("<4BH2x", col, row, cells, rows, para)
    table("img", b)
    # THE ENEMY'S FOLLOW-UPS (125.6.7): after its turn played AFTER, its
    # next turn plays THEN - at the same target (FOLLOW_SAME), or by a
    # move's rule. Four bytes each, named from the header's 52..55, which
    # every rules block before them left zero: none
    fol = d["enemy"].get("follow_ups", [])
    if len(fol) > MAX_FOLLOW:
        raise RulesError("%d follow-ups, at most %d" % (len(fol), MAX_FOLLOW))
    b = bytearray()
    for i, x in enumerate(fol):
        w = "the enemy's follow-up %d" % (i + 1)
        tg = x.get("target", "same")
        arg = 0
        if tg == "same":
            rule = FOLLOW_SAME
        else:
            rule = RULES[tg]
            if tg == "named":
                if x.get("named") not in cname:
                    raise RulesError("%s aims at no character (%s)"
                                     % (w, x.get("named")))
                arg = cname.index(x["named"])
        b += bytes([need(aidx, x.get("after"), w, "action it follows"),
                    need(aidx, x.get("then"), w, "action"), rule, arg])
    table("fol", b)
    # THE BAND'S LAYOUT (125.5.1), and the menu items placed one by one: a
    # look's "places" [[col, row] or null per item]
    lay = layout_of(proj)
    for k in LAYOUT:
        hi = 39 if k.endswith("_col") else 255
        if not 0 <= lay[k] <= hi:
            raise RulesError("the band's %s is %d, 0 to %d" % (k, lay[k], hi))
    table("lay", bytes([lay[k] for k in LAYOUT]) + bytes(LAYOUT_SZ -
                                                         len(LAYOUT)))
    b = bytearray()
    for mi, m in enumerate(menus):
        for ii, pl in enumerate(m["look"].get("places") or []):
            if pl and ii < len(m["items"]):
                b += bytes([mi, ii, max(0, min(39, int(pl[0]))),
                            max(0, min(255, int(pl[1])))])
    if len(b) // 4 > MAX_PLACES:
        raise RulesError("%d menu items placed, at most %d"
                         % (len(b) // 4, MAX_PLACES))
    nplace = len(b) // 4
    table("place", b)
    if len(out) > RULES_MAX:
        raise RulesError("the rules are %d bytes, over %d" % (len(out), RULES_MAX))

    e = d["enemy"]

    def end_clip(k):
        if preview and e.get(k) is None:
            return NONE
        return need(clip_index, e.get(k), "the enemy's %s (the Enemy tab)"
                    % k, "clip")
    struct.pack_into("<8B7H", out, 0, 1, len(chars), len(acts), len(menus),
                     len(stances), len(pools), len(items), len(images),
                     tabs["char"], tabs["act"], tabs["menu"], tabs["stance"],
                     tabs["pool"], tabs["item"], tabs["img"])
    band = d.get("band_image")
    out[22] = imgs.index(band) if band in imgs else NONE
    out[23] = sidx.get(e.get("start_stance", stances[0]["name"]), 0)
    out[24:34] = name_bytes(e["name"], 10)
    struct.pack_into("<6HBBBBH", out, 34, *([max(1, min(0xFFFF, e["hp"])),
                     min(0xFFFF, e.get("mp", 0))] + [stat(v) for v in (
                         e.get("atk", 16), e.get("mag", 16), e.get("def", 0),
                         e.get("mdef", 0))] +
                     [min(100, e.get("evade", 0)),
                     end_clip("victory"), end_clip("defeat"),
                     1 if d.get("seed") else 0, d.get("seed") or 0]))
    struct.pack_into("<BxH", out, 52, len(fol), tabs["fol"])
    struct.pack_into("<HBxH", out, 56, tabs["lay"], nplace, tabs["place"])
    return bytes(out)


# --- reading it back: what the engine sees ----------------------------------------

class Rules:
    def __init__(self, blob):
        b = self.b = bytes(blob)
        (ver, self.nchar, self.nact, self.nmenu, self.nstance, self.npool,
         self.nitem, self.nimg, oc, oa, om, os_, op, oi, og) = \
            struct.unpack_from("<8B7H", b, 0)
        self.band_img, self.start_stance = b[22], b[23]
        self.ename = b[24:34].decode().rstrip()
        (self.ehp, self.emp, self.eatk, self.emag, self.edef, self.emdef,
         self.eevade, self.victory, self.defeat, flags, self.seed) = \
            struct.unpack_from("<6HBBBBH", b, 34)
        self.fixed_seed = bool(flags & 1)
        self.chars = []
        for i in range(self.nchar):
            r = b[oc + i * CHAR_SZ:]
            hp, mp, atk, mag, df, mdf = struct.unpack_from("<6H", r, 8)
            self.chars.append(dict(name=r[0:8].decode().rstrip(), hp=hp, mp=mp,
                                   atk=atk, mag=mag, df=df, mdf=mdf,
                                   evade=r[20], top=r[21], side=r[22]))
        self.acts = []
        for i in range(self.nact):
            r = b[oa + i * ACT_SZ:oa + (i + 1) * ACT_SZ]
            effs = []
            for k in range(4):
                v, aa, p, bb, cc = struct.unpack_from("<BBHBB", r, 16 + 6 * k)
                if v:
                    effs.append((v, aa, p, bb, cc))
            self.acts.append(dict(
                label=r[0:8].decode().rstrip(), target=r[8], cost=r[9],
                cost_n=struct.unpack_from("<H", r, 10)[0], cost_item=r[12],
                acc=r[13], flags=r[14], effects=effs, pools=list(r[40:45]),
                by=[list(r[48 + 5 * c:53 + 5 * c]) for c in range(MAX_CHARS)]))
        self.menus = []
        for i in range(self.nmenu):
            r = b[om + i * MENU_SZ:]
            items = [(r[16 + 2 * k + 1], r[16 + 2 * k]) for k in range(r[8])]
            self.menus.append(dict(label=r[0:8].decode().rstrip(), items=items,
                                   img=r[9], col=r[10], row=r[11],
                                   pitch=r[12], ink=r[13], flags=r[14]))
        self.stances = []
        for i in range(self.nstance):
            r = b[os_ + i * STANCE_SZ:]
            mods = struct.unpack_from("<4b", r, 8)
            moves = [tuple(r[16 + 4 * k:20 + 4 * k]) for k in range(r[12])]
            resp = [tuple(r[40 + 4 * k:44 + 4 * k]) for k in range(r[13])]
            self.stances.append(dict(name=r[0:8].decode().rstrip(), mods=mods,
                                     moves=moves, resp=resp, ret=r[14],
                                     auto=tuple(r[48:52])))
        self.pools = []
        for i in range(self.npool):
            r = b[op + i * POOL_SZ:]
            self.pools.append(list(r[1:1 + r[0]]))
        self.items = []
        for i in range(self.nitem):
            r = b[oi + i * ITEM_SZ:]
            self.items.append(dict(name=r[0:8].decode().rstrip(),
                                   count=struct.unpack_from("<H", r, 8)[0]))
        nf, of = struct.unpack_from("<BxH", b, 52)
        self.follow = [tuple(b[of + 4 * k:of + 4 * k + 4]) for k in range(nf)]
        ol, npl, opl = struct.unpack_from("<HBxH", b, 56)
        self.layout = dict(zip(LAYOUT, b[ol:ol + len(LAYOUT)])) if ol \
            else None               # (None: the defaults, a block before it)
        self.places = {(b[opl + 4 * k], b[opl + 4 * k + 1]):
                       (b[opl + 4 * k + 2], b[opl + 4 * k + 3])
                       for k in range(npl)}


# --- the reference: the battle as the engine plays it ---------------------------------

class Rng:
    """The 16-bit xorshift (7, 9, 8)"""

    def __init__(self, seed):
        self.x = seed & 0xFFFF or 1

    def step(self):
        x = self.x
        x ^= (x << 7) & 0xFFFF
        x ^= x >> 9
        x ^= (x << 8) & 0xFFFF
        self.x = x
        return x

    def peek(self):
        s = self.x
        v = self.step()
        self.x = s
        return v

    def rnd(self, n):
        return (self.step() * n) >> 16


ENEMY = 0x80


class Sim:
    """A battle played by the rules in the bytes. Party turns are chosen by
    the caller (take(action, target)); everything else happens as the
    engine makes it happen. Every number the engine holds is here under
    the same name: hp, mp, lim, guard, charge, mods, ehp, emp, stance..."""

    def __init__(self, rules, seed=None):
        R = self.R = rules
        self.rng = Rng(seed if seed is not None else R.seed)
        n = R.nchar
        self.hp = [c["hp"] for c in R.chars]
        self.mp = [c["mp"] for c in R.chars]
        self.lim = [0] * n
        self.guard = [0] * n
        self.charge = [0] * n
        self.mods = [[0] * 4 for _ in range(n)]
        self.modt = [[0] * 4 for _ in range(n)]
        self.ehp, self.emp = R.ehp, R.emp
        self.eguard = self.echarge = 0
        self.emods, self.emodt = [0] * 4, [0] * 4
        self.stance = R.start_stance
        self.sleft = 0
        self.sret = NONE
        self.sturns = 0
        self.tnext = 0
        self.last = 0
        self.efol, self.eftgt = NONE, NONE  # a follow-up owed (its entry
                                            # in R.follow), its target
        self.items = [it["count"] for it in R.items]
        self.log = []
        self.over = None            # "victory", "defeat", "escape"

    # --- stats -------------------------------------------------------------
    def stat(self, who, k):
        if who == ENEMY:
            base = (self.R.eatk, self.R.emag, self.R.edef, self.R.emdef)[k]
            v = base * (8 + self.emods[k]) // 8
            return v * (100 + self.R.stances[self.stance]["mods"][k]) // 100
        c = self.R.chars[who]
        base = (c["atk"], c["mag"], c["df"], c["mdf"])[k]
        return base * (8 + self.mods[who][k]) // 8

    def evade(self, who):
        return self.R.eevade if who == ENEMY else self.R.chars[who]["evade"]

    def standing(self):
        return [i for i in range(self.R.nchar) if self.hp[i] > 0]

    # --- the arithmetic (SPEC.md 125.6.2) ----------------------------------
    def damage(self, kind, power, att, dfn, var):
        if kind == KIND_FIXED:
            return min(CAP, power)
        a = self.stat(att, 0 if kind == KIND_PHYS else 1)
        d = min(255, self.stat(dfn, 2 if kind == KIND_PHYS else 3))
        x = min(0xFFFF, (power * a) >> 4)
        x = min(CAP, (x * (256 - d)) >> 8)
        return min(CAP, x * var // 100)

    def heal_amount(self, kind, power, who, tgt, var):
        if kind == HEAL_FIXED:
            return min(CAP, power)
        if kind == HEAL_PERCENT:
            mx = self.R.chars[tgt]["hp"] if tgt != ENEMY else self.R.ehp
            return mx * power // 100
        x = min(CAP, (power * self.stat(who, 1)) >> 4)
        return min(CAP, x * var // 100)

    # --- an action: its targets, its rolls, its pool, its clip -----------------
    def targets(self, a, who, chosen=None):
        t = a["target"]
        revive = any(e[0] == VERBS["revive"] for e in a["effects"])
        if who == ENEMY:
            if t in (TARGETS["self"], TARGETS["ally"], TARGETS["allies"]):
                return [ENEMY]
            if t == TARGETS["none"]:
                return []
            if t == TARGETS["characters"]:
                return self.standing()
            return [chosen]
        if t == TARGETS["self"]:
            return [who]
        if t == TARGETS["ally"]:
            return [chosen]
        if t == TARGETS["allies"]:
            return list(range(self.R.nchar)) if revive else self.standing()
        if t == TARGETS["none"]:
            return []
        return [ENEMY]

    def resolve(self, ai, who, chosen=None):
        """THE DRAWS, in order (SPEC.md 125.6.3): the POOL pick first; then
        for each target in turn, its HIT roll (an opponent, accuracy under
        100 or an evade over 0), and for each DAMAGE effect's each hit its
        CRITICAL roll (physical only) and its VARIANCE; a HEAL by power its
        variance. Returns the plan: the clip and every target's results"""
        R, rng = self.R, self.rng
        a = R.acts[ai]
        pick = rng.step()
        tg = self.targets(a, who, chosen)
        charged = self.charge[who] if who != ENEMY else self.echarge
        res = []
        for t in tg:
            opp = (who == ENEMY) != (t == ENEMY)
            hit = True
            if opp and (a["acc"] < 100 or self.evade(t) > 0):
                hit = rng.rnd(100) < a["acc"] - self.evade(t)
            r = dict(t=t, hit=hit, dmg=0, crit=False, heal=0)
            first_dmg = True
            for (v, aa, p, bb, cc) in a["effects"]:
                if v == VERBS["damage"] and hit:
                    tot = 0
                    for h in range(bb):
                        crit = aa == KIND_PHYS and rng.rnd(16) == 0
                        var = 90 + rng.rnd(21) if aa != KIND_FIXED else 100
                        x = self.damage(aa, p, who, t, var)
                        if crit:
                            x = min(CAP, x * 2)
                            r["crit"] = True
                        tot = min(CAP, tot + x)
                    if charged and first_dmg:
                        tot = min(CAP, tot * 2)
                    first_dmg = False
                    if self.is_guarding(t):
                        tot >>= 1
                    r["dmg"] = min(CAP, r["dmg"] + tot)
                elif v == VERBS["heal"]:
                    var = 90 + rng.rnd(21) if aa == HEAL_POWER else 100
                    r["heal"] += self.heal_amount(aa, p, who, t, var)
            res.append(r)
        # the outcome, and the pool it plays (SPEC.md 125.6.4): over the
        # OPPONENTS' results only
        O = [r for r in res if (who == ENEMY) != (r["t"] == ENEMY)]
        H = [r for r in O if r["hit"]]
        if who != ENEMY and any(r["dmg"] for r in O) and \
                self.ehp <= sum(r["dmg"] for r in O):
            outc = 4                        # finishing
        elif any(r["crit"] for r in O):
            outc = 3                        # critical
        elif H and all(self.is_guarding(r["t"]) for r in H):
            outc = 2                        # defended
        elif O and not H:
            outc = 1                        # missed
        else:
            outc = 0                        # hit
        single = tg[0] if len(tg) == 1 and tg[0] != ENEMY else None
        clip = self.clip_of(a, outc, single, pick)
        return dict(action=ai, who=who, targets=tg, res=res, outcome=outc,
                    clip=clip, charged=charged)

    def is_guarding(self, t):
        return self.eguard if t == ENEMY else self.guard[t]

    def pool_clips(self, a, k, single):
        if single is not None:
            p = a["by"][single][k]
            if p != NONE:
                return self.R.pools[p]
        p = a["pools"][k]
        return self.R.pools[p] if p != NONE else []

    def clip_of(self, a, outc, single, pick):
        """The pool for outcome OUTC, falling back to HIT; the target's own
        pool before the general one"""
        for k in ((outc, 0) if outc else (0,)):
            c = self.pool_clips(a, k, single)
            if c:
                return c[(pick * len(c)) >> 16]
        return NONE

    def peek_clip(self, ai, who, chosen=None):
        """The clip the read-ahead fetches while the cursor is on an action:
        its HIT pool's pick by the next draw, not taken"""
        a = self.R.acts[ai]
        tg = self.targets(a, who, chosen) if a["target"] != TARGETS["ally"] \
            or chosen is not None else []
        single = tg[0] if len(tg) == 1 and tg[0] != ENEMY else None
        return self.clip_of(a, 0, single, self.rng.peek())

    # --- costs and effects -----------------------------------------------------
    def can_pay(self, ai, who):
        a = self.R.acts[ai]
        if a["cost"] == COST_MP:
            return self.mp[who] >= a["cost_n"]
        if a["cost"] == COST_ITEM:
            return self.items[a["cost_item"]] >= max(1, a["cost_n"])
        if a["cost"] == COST_LIMIT:
            return self.lim[who] >= 100
        return True

    def pay(self, ai, who):
        a = self.R.acts[ai]
        if a["cost"] == COST_MP:
            self.mp[who] -= a["cost_n"]
        elif a["cost"] == COST_ITEM:
            self.items[a["cost_item"]] -= max(1, a["cost_n"])
        elif a["cost"] == COST_LIMIT:
            self.lim[who] = 0

    def apply(self, plan):
        """The effects, once the clip is over (SPEC.md 125.6.5)"""
        R = self.R
        a = R.acts[plan["action"]]
        who = plan["who"]
        if any(e[0] == VERBS["damage"] for e in a["effects"]):
            if who == ENEMY:
                self.echarge = 0
            else:
                self.charge[who] = 0
        for r in plan["res"]:
            t = r["t"]
            for (v, aa, p, bb, cc) in a["effects"]:
                if v == VERBS["damage"] and r["dmg"]:
                    if t == ENEMY:
                        self.ehp = max(0, self.ehp - r["dmg"])
                        self.last = who
                    else:
                        self.hp[t] = max(0, self.hp[t] - r["dmg"])
                        self.lim[t] = min(100, self.lim[t] +
                                          r["dmg"] * 100 // R.chars[t]["hp"])
                    r["dmg_done"] = True
                    break
            healed = False
            for (v, aa, p, bb, cc) in a["effects"]:
                if v == VERBS["heal"]:
                    if healed:              # (the total, once: as damage)
                        continue
                    healed = True
                    if t == ENEMY:
                        self.ehp = min(R.ehp, self.ehp + r["heal"])
                    elif self.hp[t] > 0:
                        self.hp[t] = min(R.chars[t]["hp"], self.hp[t] + r["heal"])
                elif v == VERBS["restore"]:
                    if t == ENEMY:
                        self.emp = min(R.emp, self.emp + p)
                    else:
                        self.mp[t] = min(R.chars[t]["mp"], self.mp[t] + p)
                elif v == VERBS["revive"] and t != ENEMY and self.hp[t] == 0:
                    self.hp[t] = max(1, R.chars[t]["hp"] * p // 100)
                elif v == VERBS["guard"]:
                    if t == ENEMY:
                        self.eguard = 1
                    else:
                        self.guard[t] = 1
                elif v == VERBS["charge"]:
                    if t == ENEMY:
                        self.echarge = 1
                    else:
                        self.charge[t] = 1
                elif v in (VERBS["boost"], VERBS["lower"]) and r["hit"]:
                    sgn = 1 if v == VERBS["boost"] else -1
                    m, mt = (self.emods, self.emodt) if t == ENEMY else \
                        (self.mods[t], self.modt[t])
                    m[aa] = max(-4, min(4, m[aa] + sgn * bb))
                    mt[aa] = cc
        for (v, aa, p, bb, cc) in a["effects"]:     # not per target
            if v == VERBS["stance"] and who == ENEMY:
                self.enter_stance(aa, cc)
            elif v == VERBS["escape"]:
                self.over = "escape"
        if self.ehp == 0 and not self.over:
            self.over = "victory"
        elif not self.standing() and not self.over:
            self.over = "defeat"

    def enter_stance(self, s, dur):
        if dur:
            self.sret = self.stance
            self.sleft = dur
        else:
            self.sret, self.sleft = NONE, 0
        self.stance = s
        self.sturns = 0

    def tick(self, m, mt):
        for k in range(4):
            if mt[k]:
                mt[k] -= 1
                if not mt[k]:
                    m[k] = 0

    # --- the turns ---------------------------------------------------------------
    def start_turn(self, c):
        """Character C's turn begins: its guard ends, its stat changes age"""
        self.guard[c] = 0
        self.tick(self.mods[c], self.modt[c])

    def take(self, ai, who, chosen=None):
        """Character WHO takes action AI (paid for: can_pay was asked) at
        CHOSEN when its target is an ally. Returns the plans in order: the
        action's, then the enemy's RESPONSE if one comes"""
        self.pay(ai, who)
        plan = self.resolve(ai, who, chosen)
        self.apply(plan)
        self.log.append(plan)
        out = [plan]
        if not self.over:
            rp = self.respond(ai, who)
            if rp:
                out.append(rp)
        return out

    def respond(self, ai, who):
        """SPEC.md 125.6.6: the stance's responses in order, each whose
        trigger the action meets drawing its chance; the first that comes up
        is played, at the actor"""
        a = self.R.acts[ai]
        for trig, arg, chance, act in self.R.stances[self.stance]["resp"]:
            if trig == 0 or (trig == 1 and a["flags"] & AF_PHYS) or \
                    (trig == 2 and a["flags"] & AF_MAG) or \
                    (trig == 3 and arg == ai):
                if self.rng.rnd(100) < chance:
                    tgt = who if self.hp[who] > 0 else self.pick_target(0, 0)
                    plan = self.resolve(act, ENEMY, tgt)
                    plan["response"] = True
                    self.apply(plan)
                    self.log.append(plan)
                    return plan
        return None

    def fol_target(self, x, ftgt):
        """Follow-up X's target: the one its first action was played at
        while that one stands (FOLLOW_SAME), else by its rule - the same
        falling back to the alternate"""
        rule, arg = x[2], x[3]
        if rule == FOLLOW_SAME:
            if ftgt != NONE and ftgt is not None and self.hp[ftgt] > 0:
                return ftgt
            rule = 0
        return self.pick_target(rule, arg)

    def pick_target(self, rule, arg):
        st = self.standing()
        if not st:
            return None
        if rule == RULES["random"]:
            return st[self.rng.rnd(len(st))]
        if rule == RULES["lowest"]:
            return min(st, key=lambda i: (self.hp[i], i))
        if rule == RULES["last"] and self.last in st:
            return self.last
        if rule == RULES["named"] and arg in st:
            return arg
        # alternate: the next standing from tnext, round the party
        for k in range(self.R.nchar):
            i = (self.tnext + k) % self.R.nchar
            if self.hp[i] > 0:
                self.tnext = (i + 1) % self.R.nchar
                return i
        return None

    def enemy_turn(self):
        """SPEC.md 125.6.7: the guard ends and the stat changes age; a stance
        whose turns are up returns (its return action, if any, is the
        turn); else the stance's automatic change (its action, if any, is
        the turn); else a move drawn by weight, at a target by its rule"""
        R = self.R
        self.eguard = 0
        self.tick(self.emods, self.emodt)
        st = R.stances[self.stance]
        ai = NONE
        rule, arg = 0, 0
        fol, self.efol = self.efol, NONE    # A FOLLOW-UP OWED (125.6.7): it
        ftgt = self.eftgt                   # is this turn, whatever else
        if self.sleft:
            self.sleft -= 1
            if not self.sleft:
                ai = st["ret"]
                back = self.sret
                self.sret = NONE
                self.stance = back
                self.sturns = 0
                if ai == NONE:
                    st = R.stances[self.stance]
        if fol != NONE:                 # (the stance's timer has counted;
            ai = R.follow[fol][1]           # its return action, if this was
                                            # its turn, gives way)
        if ai == NONE and not self.sleft:
            kind, aarg, to, via = st["auto"]
            fire = (kind == 1 and self.ehp * 100 < R.ehp * aarg) or \
                   (kind == 2 and self.sturns >= aarg)
            if kind and fire and to != self.stance:
                self.enter_stance(to, 0)
                ai = via
                st = R.stances[self.stance]
        if ai == NONE:
            moves = st["moves"]
            tot = sum(m[1] for m in moves)
            r = self.rng.rnd(tot)
            for m in moves:
                if r < m[1]:
                    ai, rule, arg = m[0], m[2], m[3]
                    break
                r -= m[1]
        a = R.acts[ai]
        tgt = None
        if a["target"] in (TARGETS["enemy"], TARGETS["character"]):
            if fol != NONE:
                tgt = self.fol_target(R.follow[fol], ftgt)
            else:
                tgt = self.pick_target(rule, arg)
        for k, x in enumerate(R.follow):    # this turn's action OWES one:
            if x[0] == ai:                  # the first follow-up naming it
                self.efol, self.eftgt = k, tgt
                break
        self.sturns += 1
        plan = self.resolve(ai, ENEMY, tgt)
        self.apply(plan)
        self.log.append(plan)
        return plan
