#!/usr/bin/env python3
"""EXCITEBIKE wave 3: the rider simulation against its reference model (SPEC.md 102.3, 102.6).

    make excitebikedisk
    python3 tests/excitebike_ref.py [--quick] [--tables-only] [--no-guest]

Three parts.

1  CONSTANTS  (always; host only)  const.inc's XP_ block equals tools/exbsim.py's PHYS,
   name for name, and the mode / input / event codes agree.  Two files that state
   the same numbers are one drifting comment away from two rule sets.

2  GUEST == exbsim, STEP FOR STEP  (always; MartyPC)  The harness feeds the guest a
   scripted input byte a step (`xb_tmode`, `xb_tscript`) and reads back 16 bytes of
   state after EVERY step (`xb_ttrace`: position, speed, height, vertical velocity,
   engine temperature, mode, pitch, lane, pose).  tools/exbsim.py's `Sim` is run over
   the same script and every record must be identical.  At the end of each 256-step
   chunk the HUD string, the bike records the renderer draws and the window column
   must equal the model's too.  The scripts are: the reference rider (exbsim.bot_input)
   driving each course to the finish (both laps, the lap change, the finish and the
   barrier), seeded random hold-scripts that reach every mode and hazard (crash by
   hurdle, by wheelie and by landing, the bounce, the kicker, mud and rough, the arrows,
   overheat and its stall), a flat-out run that stalls the engine, and a wheelie held
   until the flip.  Coverage is measured, not assumed: the run prints how many steps
   each mode, event and pose saw, and FAILS when a rule was never exercised.

3  TABLE CHECK against the study material (only when EXCITEBIKE_REF names the
   reference; otherwise `SKIP: EXCITEBIKE_REF not found`, exit 0).  A stdlib reader of
   the reference's `.byte` tables (its own parser, independent of every compiler
   here) compares the rule tables with const.inc's.  Every difference must be listed by
   name in tests/excitebike_ref_deviations.txt with the reason (our tuning may differ;
   an unexplained difference may not), and a listed name that no longer differs fails
   too: the file is not allowed to go stale.  The reference is read, never written to,
   and nothing here is a build input.  The step-for-step oracle diff of plan section 15
   needs the study material assembled and run under a 6502 harness; it is NOT part of
   this wave and this test says so rather than skipping silently.
"""
import argparse
import collections
import os
import random
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import exbsim                  # noqa: E402

DEVIATIONS = os.path.join(ROOT, "tests", "excitebike_ref_deviations.txt")
CONST = os.path.join(ROOT, "apps", "excitebike", "const.inc")
CHUNK = 256
FIELDS = ("pos", "speed", "A", "vy", "temp", "mode", "pitch", "lane", "pose")
UNPACK = "<IHhhHBbBB"


# ---------------------------------------------------------------------------------
# 1  the constants
# ---------------------------------------------------------------------------------
def const_equs():
    out = {}
    for line in open(CONST, encoding="utf-8"):
        m = re.match(r"^([A-Za-z_]\w*)\s+equ\s+([-+0-9A-Fa-fx]+)\s*(?:;.*)?$", line)
        if m:
            try:
                out[m.group(1)] = int(m.group(2), 0)
            except ValueError:
                pass
    return out


def check_constants():
    eq = const_equs()
    bad = []
    for k, v in exbsim.PHYS.items():
        n = "XP_" + k
        if n not in eq:
            bad.append("%s is missing from const.inc" % n)
        elif eq[n] != v:
            bad.append("%s: const.inc has %d, exbsim.PHYS has %d" % (n, eq[n], v))
    for n, v in (("XM_RIDE", exbsim.M_RIDE), ("XM_AIR", exbsim.M_AIR),
                 ("XM_CRASH", exbsim.M_CRASH), ("XM_STALL", exbsim.M_STALL),
                 ("INP_U", exbsim.INP_U), ("INP_D", exbsim.INP_D), ("INP_L", exbsim.INP_L),
                 ("INP_R", exbsim.INP_R), ("INP_A", exbsim.INP_A), ("INP_B", exbsim.INP_B)):
        if eq.get(n) != v:
            bad.append("%s: const.inc has %r, exbsim has %d" % (n, eq.get(n), v))
    extra = [k for k in eq if k.startswith("XP_") and k[3:] not in exbsim.PHYS
             and k != "XP_RIDER_SLOT"]
    for k in extra:
        bad.append("%s is in const.inc and not in exbsim.PHYS" % k)
    assert not bad, "constants disagree:\n  " + "\n  ".join(bad)
    print("constants: %d XP_ names identical in const.inc and exbsim.PHYS" % len(exbsim.PHYS))


# ---------------------------------------------------------------------------------
# the scripts
# ---------------------------------------------------------------------------------
def bot_script(art, course):
    """The reference rider's inputs, closed loop against the model (the guest is fed the
    same bytes open loop)."""
    sim = exbsim.Sim(art, course)
    out = []
    for _ in range(9000):
        inp = exbsim.bot_input(sim)
        out.append(inp)
        sim.step(inp)
        if sim.fin and sim.speed == 0:
            out += [0] * 40                      # the finish, and the barrier
            break
    return out


def random_script(seed, steps):
    rnd = random.Random(seed)
    out = []
    a = exbsim.INP_A
    while len(out) < steps:
        n = rnd.randrange(1, 46)
        base = rnd.choice((a, a, a | exbsim.INP_B, a | exbsim.INP_B, 0, a | exbsim.INP_B))
        extra = 0
        for bit in (exbsim.INP_U, exbsim.INP_D, exbsim.INP_L, exbsim.INP_R):
            if rnd.random() < 0.22:
                extra |= bit
        out += [base | extra] * n
    return out[:steps]


def scripts(art, course, quick):
    q = 1 if quick else 0
    S = collections.OrderedDict()
    S["reference rider"] = bot_script(art, course)
    if not q:
        S["random A"] = random_script(0x3001 + course, 5200)
        S["random B"] = random_script(0x3101 + course, 5200)
        if course == 0:
            # flat out with the turbo: the engine overheats (32 units), stalls for 258
            # steps and comes back; then the same again while steering the lanes
            s = [exbsim.INP_A | exbsim.INP_B] * 900
            s += [exbsim.INP_A | exbsim.INP_B | (exbsim.INP_U if (i // 25) % 2 else exbsim.INP_D)
                  for i in range(700)]
            S["turbo until it stalls"] = s
            # a wheelie held: the flip (13 steps at the top of the pitch)
            S["wheelie flip"] = ([exbsim.INP_A | exbsim.INP_B] * 200 +
                                 [exbsim.INP_A | exbsim.INP_B | exbsim.INP_L] * 120 +
                                 [exbsim.INP_A] * 200)
    else:
        S["random A"] = random_script(0x3001 + course, 1500)
    return S


# ---------------------------------------------------------------------------------
# 2  the guest
# ---------------------------------------------------------------------------------
def unpack_recs(raw):
    return [struct.unpack_from(UNPACK, raw, 16 * i) for i in range(len(raw) // 16)]


def bikes_expect(sim):
    return sim.bikes()


def guest_bikes(raw):
    out = []
    for i in range(3):
        act, pose, x, y, rider, _ = struct.unpack_from("<BBHHBB", raw, i * 8)
        out.append({"pose": pose, "x": x, "y": y} if act else None)
    return out


class Coverage:
    def __init__(self):
        self.modes = collections.Counter()
        self.events = collections.Counter()
        self.poses = collections.Counter()
        self.classes = collections.Counter()
        self.steps = 0
        self.other = collections.Counter()

    def note(self, sim, prev_lane_ldir=None):
        self.steps += 1
        self.modes[sim.mode] += 1
        self.poses[sim.pose] += 1
        self.classes[sim.cls] += 1
        for e in sim.events:
            self.events[e] += 1
        sim.events.clear()
        if sim.fin:
            self.other["finished"] += 1
        if sim.lap == 2:
            self.other["lap 2"] += 1
        if sim.sqt:
            self.other["squash"] += 1
        if sim.ldir:
            self.other["lane change"] += 1
        if sim.ldir == 0 and sim.lane in exbsim.LANE_CENTRES:
            self.other["lane settled"] += 1
        if sim.wcount:
            self.other["wheelie counting"] += 1
        if sim.scr is not None:
            self.other["script running"] += 1
        if sim.sdefer:
            self.other["defer used"] += 1


def compare_chunk(sim, script, chunk0, got, cov, label):
    """Step the model through this chunk and compare every record; -> nothing or raises."""
    for i, inp in enumerate(script):
        sim.step(inp)
        want = sim.rec()
        have = got[i]
        wv = struct.unpack(UNPACK, want)
        if tuple(have) != wv:
            j = next(k for k in range(len(wv)) if wv[k] != have[k])
            lines = ["%s: step %d (input %#04x) differs in %s: guest %r, model %r" % (
                label, chunk0 + i, inp, FIELDS[j], have[j], wv[j]),
                "  model %s" % dict(zip(FIELDS, wv)),
                "  guest %s" % dict(zip(FIELDS, have))]
            if i:
                lines.append("  guest before %s" % dict(zip(FIELDS, got[i - 1])))
            lines.append("  model: col %d lane %d ldir %d sc %d th %#x gh %#x vg %#x pitch %d "
                         "ptimer %d mtimer %d cls %d" % (sim.col, sim.lane, sim.ldir, sim.sc,
                                                         sim.th, sim.gh, sim.vg, sim.pitch,
                                                         sim.ptimer, sim.mtimer, sim.cls))
            raise AssertionError("\n".join(lines))
        cov.note(sim)


def run_guest(quick, courses):
    import os88marty as M          # noqa: E402
    import os88ui                  # noqa: E402
    import excitebike_video as V   # noqa: E402
    art = exbsim.X.Art()
    ref = exbsim.Ref(art)
    sym = V.symbols()
    cov = [Coverage(), Coverage()]
    total = 0
    for course in courses:
        sc = scripts(art, course, quick)
        with os88ui.boot(V.at("build/os8088-360.img"), apps=V.at("build/excitebike360.img"),
                         machine=V.MACHINE["vga"]) as ui:
            ui.open_drive("B")
            ui.settle()
            ui.open("8BITBIKE.O88")
            ui.settle()
            g = V.Game(ui, "vga", sym, ref)
            g.put("xb_track", course)
            M.until(ui.m, lambda _: g.w("xb_reveal") >= g.w("xb_frontheight") > 0,
                    "the splash reveal finishes", guest=30)
            ui.settle()
            g.enter(sim=True)
            m = g.m
            for name, script in sc.items():
                # a fresh rider, the harness feeding the steps
                g.put("xb_tmode", 1)
                g.put("xb_tsn", 0, 2)
                g.put("xb_tsi", 0, 2)
                g.put("xb_treset", 1)
                g.put("xb_tpause", 0)
                m.run()
                M.until(m, lambda _: g.b("xb_tpause") == 1 and g.b("xb_treset") == 0,
                        "a fresh rider", guest=30)
                m.pause()
                sim = exbsim.Sim(art, course)
                for c0 in range(0, len(script), CHUNK):
                    chunk = script[c0:c0 + CHUNK]
                    buf = bytes(chunk) + bytes(CHUNK - len(chunk))
                    g.put("xb_tscript", buf)
                    g.put("xb_tsn", len(chunk), 2)
                    g.put("xb_tsi", 0, 2)
                    g.put("xb_tpause", 0)
                    m.run()
                    M.until(m, lambda _: g.b("xb_tpause") == 1, "%d steps" % len(chunk),
                            guest=120)
                    m.pause()
                    raw = m.read(g.a("xb_ttrace"), 16 * len(chunk))
                    compare_chunk(sim, chunk, c0, unpack_recs(raw), cov[course],
                                  "course %d, %s" % (course + 1, name))
                    # the HUD, the bike records and the window
                    hud = bytes(g.rd("xb_hudstr", 20)).decode("latin1")
                    assert hud == sim.hud(), "HUD after step %d: guest %r, model %r" % (
                        c0 + len(chunk), hud, sim.hud())
                    have = guest_bikes(g.rd("xb_bikes", 32))
                    want = sim.bikes()
                    assert have == want, "bike records after step %d: guest %r, model %r" % (
                        c0 + len(chunk), have, want)
                    smax = g.w("xb_Smax")
                    S = min(sim.pos >> 11, smax)
                    assert g.w("xb_S") == S, "window column after step %d: guest %d, model %d" % (
                        c0 + len(chunk), g.w("xb_S"), S)
                    assert (g.w("xb_cs"), g.b("xm_lap"), g.b("xm_fin")) == (
                        sim.cs, sim.lap, sim.fin), "clock/lap/finish differ after step %d" % (
                        c0 + len(chunk))
                total += len(script)
                print("course %d, %-22s %5d steps: every record equal (final: mode %d, lap %d, "
                      "col %d, %s)" % (course + 1, name, len(script), sim.mode, sim.lap,
                                       sim.col, sim.hud()), flush=True)
            if course == 0:
                key_test(g, m)
    return cov, total


def key_test(g, m):
    """The keyboard, end to end (input.inc): real key events through MartyPC's 8255 into
    OSAPI_KEY_DOWN and the frame loop, no harness feeding.  Z is the throttle, X the
    turbo, the arrows steer; each must do what SPEC.md 102.3 says within a second or two
    of guest time."""
    import os88marty as M          # noqa: E402

    def fresh():
        g.put("xb_tmode", 0)
        g.put("xb_treset", 1)
        g.put("xb_tpause", 0)
        m.run()
        M.until(m, lambda _: g.b("xb_treset") == 0, "a fresh rider", guest=20)
        M.guest_sleep(m, .3)

    def state():
        return (g.w("xm_speed"), g.w("xm_temp"), g.b("xm_lane"), g.b("xm_pitch") - (256 if g.b("xm_pitch") > 127 else 0),
                g.b("xm_mode"))

    def held(keys, cond, limit=3.0):
        """Hold the keys, looking every 0.1 s of guest time until cond(state) is true (or
        limit seconds pass): a wheelie held long enough flips the bike, so a fixed hold
        would race the rule it is testing."""
        for k in keys:
            m.key(k, up=False)
        got = None
        t = 0.0
        while t < limit:
            M.guest_sleep(m, .1)
            t += .1
            m.pause()
            got = state()
            if cond(got):
                break
            m.run()
        m.pause()
        for k in keys:
            m.key(k, down=False)
        m.run()
        return got
    fresh()
    speed, temp, lane, pitch, mode = held(["KeyZ"], lambda s: s[0] >= 0x180)
    assert speed >= 0x180, "holding Z did not accelerate the bike (speed %#x)" % speed
    assert temp <= exbsim.PHYS["TEMP_A"] + 0x100, "the throttle alone heated the engine to %#x" % temp
    fresh()
    speed, temp, lane, pitch, mode = held(["KeyZ", "KeyX"], lambda s: s[1] > exbsim.PHYS["TEMP_MIN"] + 0x300)
    assert temp > exbsim.PHYS["TEMP_MIN"] + 0x300, "holding X did not heat the engine (%#x)" % temp
    fresh()
    speed, temp, lane, pitch, mode = held(["ArrowUp"], lambda s: s[2] < 20)
    assert lane < 26 and speed == 0, "Up did not move the bike to the far lane (lane %d)" % lane
    fresh()
    speed, temp, lane, pitch, mode = held(["ArrowDown"], lambda s: s[2] > 32)
    assert lane > 26, "Down did not move the bike to the near lane (lane %d)" % lane
    fresh()
    speed, temp, lane, pitch, mode = held(["KeyZ", "ArrowLeft"], lambda s: s[3] >= 2)
    assert pitch >= 2 and mode == exbsim.M_RIDE, "Z with Left did not lift the nose (pitch %d, mode %d)" % (
        pitch, mode)
    fresh()
    M.guest_sleep(m, .5)
    m.pause()
    speed, temp, lane, pitch, mode = state()
    m.run()
    assert speed == 0 and lane == 26 and pitch == 0, "an idle bike moved: %r" % ((speed, lane, pitch),)
    print("keys: Z accelerates, X heats the engine, Up/Down change lane, Left lifts the nose, "
          "and nothing moves untouched")


def coverage_report(cov):
    events = collections.Counter()
    modes = collections.Counter()
    poses = collections.Counter()
    classes = collections.Counter()
    other = collections.Counter()
    for c in cov:
        events.update(c.events)
        modes.update(c.modes)
        poses.update(c.poses)
        classes.update(c.classes)
        other.update(c.other)
    print("coverage: modes", dict(sorted(modes.items())), "events", dict(sorted(events.items())))
    print("coverage: poses", dict(sorted(poses.items())))
    print("coverage: classes", dict(sorted(classes.items())), "other", dict(other))
    return events, modes, poses, classes, other


def check_coverage(events, modes, poses, classes, other, full):
    need_events = ["crash", "land", "takeoff", "lap", "finish"]
    if full:
        need_events += ["overheat", "bump", "bounce"]
    miss = [e for e in need_events if not events.get(e)]
    for m, n in ((exbsim.M_RIDE, "ride"), (exbsim.M_AIR, "air"), (exbsim.M_CRASH, "crash")):
        if not modes.get(m):
            miss.append("mode " + n)
    if full and not modes.get(exbsim.M_STALL):
        miss.append("mode stall")
    for c, n in ((exbsim.C_MUD, "mud"), (exbsim.C_ROUGH, "rough"), (exbsim.C_HLO, "low hurdle"),
                 (exbsim.C_HHI, "high hurdle"), (exbsim.C_ARROW, "arrow"),
                 (exbsim.C_KICKER, "kicker"), (exbsim.C_RAMP, "ramp"), (exbsim.C_HILL, "hill")):
        if not classes.get(c):
            miss.append("class " + n)
    for k in ("lane change", "lane settled", "squash", "wheelie counting", "defer used",
              "finished"):
        if not other.get(k):
            miss.append(k)
    used = [p for p in range(24) if poses.get(p)]
    if full and len(used) < 22:
        miss.append("only %d of the 24 poses were on the screen" % len(used))
    assert not miss, ("the scripts never exercised: %s (a rule that no script reaches is a rule "
                      "the equality proves nothing about)" % ", ".join(miss))
    print("coverage: every rule exercised (%d of the 24 poses seen)" % len(used))


# ---------------------------------------------------------------------------------
# 3  the table check
# ---------------------------------------------------------------------------------
def read_tables(path):
    """The reference's `.byte` tables by label: label -> [bytes].  A table is a label
    and the `.byte` lines that follow it (aliases and comments between them are skipped)."""
    tables = {}
    cur = []
    for raw in open(path, encoding="utf-8", errors="replace"):
        line = raw.rstrip("\n")
        m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*):", line)
        if m:
            cur = [m.group(1)] if not cur or cur_has_data(tables, cur) else cur + [m.group(1)]
            for n in cur:
                tables.setdefault(n, [])
            continue
        m = re.search(r"\.byte\s+((?:\$[0-9A-Fa-f]{2}\s*,?\s*)+)", line)
        if m and cur:
            vals = [int(v, 16) for v in re.findall(r"\$([0-9A-Fa-f]{2})", m.group(1))]
            for n in cur:
                tables[n] += vals
        elif line.strip() and not line.lstrip().startswith(";") and not m:
            if not re.match(r"^\s*(-|\d)", line) or ".byte" not in line:
                if re.search(r"\b(LDA|STA|JSR|JMP|RTS|LDX|LDY|CMP|BNE|BEQ|CLC|SEC|ADC|SBC|INC|DEC)\b", line):
                    cur = []
    return tables


def cur_has_data(tables, cur):
    return any(tables.get(n) for n in cur)


def table_check(ref):
    path = os.path.join(ref, "bank_FF.asm")
    if not os.path.exists(path):
        print("SKIP: EXCITEBIKE_REF not found (no bank_FF.asm under %s)" % ref)
        return
    T = read_tables(path)
    P = exbsim.PHYS
    rows = []            # (name, ours, theirs)

    def add(name, ours, theirs):
        rows.append((name, ours, theirs))

    def tb(label, n=None):
        v = T.get(label)
        assert v, "the reference has no table %s (the reader could not find it)" % label
        return v if n is None else v[:n]

    acc = tb("tbl_C0BC")
    add("ACC_A", P["ACC_A"], acc[0])
    add("ACC_B", P["ACC_B"], acc[1])
    lo, hi = tb("tbl_C0CE"), tb("tbl_C0D1")
    add("SPD_A", P["SPD_A"], lo[0] | hi[0] << 8)
    add("SPD_B", P["SPD_B"], lo[1] | hi[1] << 8)
    dec = tb("tbl_C0C1")
    add("DEC", P["DEC"], dec[0])                        # both are applied every 4th step
    add("DEC_AIR", P["DEC_AIR"], dec[4])
    add("DEC_AIR_L", P["DEC_AIR_L"], dec[3])
    add("MUD", P["DEC_MUD"], dec[5])
    grav = tb("tbl_D868")
    add("GRAV", P["GRAV"], grav[0])
    add("GRAV_L", P["GRAV_L"], grav[2])
    heat = tb("tbl_D8F7")
    add("HEAT_B", P["HEAT_B"], heat[1])
    add("HEAT_A", P["HEAT_A"], heat[2])
    tgt = tb("tbl_D8FB")
    add("TEMP_MIN", P["TEMP_MIN"] >> 8, tgt[0])
    add("TEMP_MAX", P["TEMP_MAX"] >> 8, tgt[1])
    add("TEMP_A", P["TEMP_A"] >> 8, tgt[2])
    cad = tb("tbl_C0D4")
    add("PT_GROUND", P["PT_GROUND"] + 1, cad[0])
    add("PT_AIR", P["PT_AIR"] + 1, cad[1])
    win_lo, win_hi, perfect = tb("tbl_D86C"), tb("tbl_D87C"), tb("tbl_D88B")
    # our landing rule is |pitch - slope pitch| <= 1 clean, 2 bounces, 3 crashes; theirs is a
    # window per slope class: the widths are what can be compared
    add("LAND_WINDOW", P.get("LAND_CLEAN", 1) + 1, max(hi_ - lo_ for lo_, hi_ in zip(win_lo, win_hi)) if win_lo else 0)
    add("TAKEOFF", P["TAKEOFF_NUM"], 0)                 # ours 3/2 x the sloped rise; theirs a per-element handler
    add("PITCH_SCALE", P["PITCH_MAX"] - P["PITCH_MIN"], max(tb("tbl_C0CA")))
    add("LANE_CENTRES", list(exbsim.LANE_CENTRES), None)
    # lanes: the four centres are a table in the reference too
    for name, vals in T.items():
        if vals[:4] == [0x0E, 0x1A, 0x26, 0x32]:
            rows[-1] = ("LANE_CENTRES", list(exbsim.LANE_CENTRES), vals[:4])
            break
    listed = {}
    if os.path.exists(DEVIATIONS):
        for line in open(DEVIATIONS, encoding="utf-8"):
            line = line.split("#", 1)[0].strip()
            if line:
                k, _, why = line.partition(":")
                assert why.strip(), "a deviation with no reason: %s" % k
                listed[k.strip()] = why.strip()
    pinned = {"MUD": 64, "LAND_WINDOW": 2, "TAKEOFF": 3, "PITCH_SCALE": 7}   # ours, as the deviations file describes
    for n, o, t in rows:
        if n in pinned:
            assert o == pinned[n], "%s is now %r; %s describes %r - update both" % (n, o, DEVIATIONS, pinned[n])
    diff = [(n, o, t) for n, o, t in rows if o != t]
    same = [n for n, o, t in rows if o == t]
    unexplained = [(n, o, t) for n, o, t in diff if n not in listed]
    stale = [n for n in listed if n not in {d[0] for d in diff}]
    print("table check: %d rules compared, %d identical (%s)" % (len(rows), len(same), ", ".join(same)))
    for n, o, t in diff:
        print("  deviation %-13s ours %-22r theirs %-22r %s" % (
            n, o, t, listed.get(n, "UNEXPLAINED")))
    assert not unexplained, "unexplained differences from the study tables: %s (list them in %s " \
        "with the reason, or fix the constant)" % ([d[0] for d in unexplained], DEVIATIONS)
    assert not stale, "tests/excitebike_ref_deviations.txt lists %s which no longer differ" % stale
    print("table check: every difference is named in tests/excitebike_ref_deviations.txt")


# ---------------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true", help="the reference rider and one short random script")
    ap.add_argument("--tables-only", action="store_true")
    ap.add_argument("--no-guest", action="store_true")
    ap.add_argument("--course", type=int, choices=(1, 2), help="one course only")
    o = ap.parse_args()
    check_constants()
    ref = os.environ.get("EXCITEBIKE_REF")
    if ref and os.path.isdir(ref):
        table_check(ref)
    else:
        print("SKIP: EXCITEBIKE_REF not found (the table check needs the study disassembly)")
    print("NOTE: the step-for-step oracle diff of plan section 15 item 2 is not implemented in "
          "this wave (tests here compare the guest with exbsim, and the tables with the study "
          "tables)")
    if o.tables_only or o.no_guest:
        return 0
    courses = [o.course - 1] if o.course else [0, 1]
    cov, total = run_guest(o.quick, courses)
    ev = coverage_report(cov)
    check_coverage(*ev, full=not o.quick and not o.course)
    print("excitebike_ref: PASS - %d steps, guest == exbsim on every record" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
