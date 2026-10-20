#!/usr/bin/env python3
"""CLEAR SKIES' frame, broken down IN FLIGHT (SPEC.md 88.12.1).

    python3 tests/skiesprof.py [--profile cruise] [--frames 40] [--tier 2]
                               [--hzfull 1]

AN INSTRUMENT, NOT A GATE - registered as such in tests/unit/t_registry.py,
and it asserts nothing.

**It is the third Clear Skies instrument and it exists because the other two
measure a STILL scene.** `tests/skiesperf.py` and `tests/skiescount.py` both
pin the aeroplane and pause the world, which is what makes their A/Bs exact -
and it means neither has ever measured a frame that had to update the panel,
step the flight model, cross an LOD threshold or refill a rolled horizon. A
parked frame is not the frame anybody flies.

So this one FLIES. A profile pokes a starting state, sets the throttle and
lets go; the world runs, the simulation steps on its own ticks, and the panel
redraws what changed. Nothing is pinned after the first frame except a held
bank, where the profile says so.

**HOW IT IS EXACT WITHOUT AN A/B.** Every stage is bracketed at its CALL SITE
- a breakpoint on the `call` and another on the instruction after it - so a
stage's cost is one subtraction of the emulator's cycle counter and no arm has
to be compared with another. A stopped guest burns no cycles, so the
breakpoints are free to the measurement; and because the guest's own clock is
its cycle count, a flight under the debugger takes the same ticks and steps as
one without, which is what makes two passes over the same profile comparable.
`os88marty.bp_trace` is the pump.

The brackets nest, so the walk keeps a stack: a stage's INCLUSIVE cost is its
own bracket and its EXCLUSIVE cost is that less the brackets inside it. What
is left at the top is the loop's own arithmetic.

**TIERS, because a breakpoint on a hot symbol runs the guest at a fraction of
its speed** (os88marty's BpTrace comment). Tier 1 is the frame's own stages,
~40 stops a frame. Tier 2 adds the per-object ones, ~300. Tier 3 adds the
per-primitive ones, ~700. Tier 5 is tier 1 plus the rolled horizon's band
(SPEC.md 88.3.1.1) and NOT the object tiers - a breakpoint inside a loop that
runs once a view row is dear enough alone. Run the tier that answers the
question.

`--hzfull 1` pokes `[cs_hzfull]`, which puts every split row back on the
whole-view span: 88.3.1.1's A/B, on one binary and one flight.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import os88build
import dispapps                                             # noqa: E402
import skies as skiestest                                   # noqa: E402


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# WHERE `cswidx.inc` IS. Clear Skies' resident world index is GENERATED
# (SPEC.md 88.10.5.3), so it is not in apps/skies/ and nasm reaches it only
# through the build tree - which the Makefile passes as `-I $(BUILD)/` and
# every script that re-assembles for a LISTING has to pass too, or the tree
# "does not assemble" and the message points at the package. os88build.at
# honours $OS88_BUILD, so a frozen soak tree resolves to its own copy.
CSWIDX = os.path.join(ROOT, os88build.at("build")) + os.sep

CPS = 4772727                           # the 4.77 MHz clock: cycles a second

# --- the stages, by tier: (scope label, the call's text, short name) --------
# The scope is the enclosing GLOBAL label, which is how skiesperf.py finds a
# site too: never a remembered offset.
TIER1 = [
    ("cs_fsx_main", r"call cs_input$",       "input"),
    ("cs_fsx_main", r"call cs_steps$",       "steps"),
    ("cs_fsx_main", r"call cs_toaststep$",   "toast"),
    ("cs_fsx_main", r"call cs_stick$",       "stick"),
    ("cs_fsx_main", r"call cs_step$",        "step"),
    ("cs_fsx_main", r"call cs_sound_step$",  "sound"),
    ("cs_fsx_main", r"call cs_render$",      "RENDER"),
    ("cs_render",   r"call cs_r_begin$",     "r_begin"),
    ("cs_render",   r"call cs_matrix$",      "matrix"),
    ("cs_render",   r"call cs_skyground$",   "skyground"),
    ("cs_render",   r"call cs_scene$",       "scene"),
    ("cs_render",   r"call cs_panel$",       "panel"),
    ("cs_render",   r"call cs_r_end$",       "r_end"),
    ("cs_r_end",    r"call cs_blit$",        "blit"),
]
TIER2 = [
    ("cs_scene",    r"call cs_consider$",    "cull"),        # two sites
    ("cs_scene",    r"call cs_occlude$",     "occlude"),
    ("cs_scene",    r"call cs_drawpass$",    "drawpass"),
    ("cs_drawpass", r"call cs_drawobj$",     "drawobj"),
    ("cs_drawobj",  r"call cs_sizepx$",      "sizepx"),
    ("cs_drawobj",  r"call cs_scale$",       "scale"),
    ("cs_drawobj",  r"call cs_nearat$",      "nearat"),
    ("cs_drawobj",  r"call cs_wireclr$",     "wireclr"),
    ("cs_drawobj",  r"call cs_stackverts$",  "stackverts"),
    ("cs_drawobj",  r"call cs_flatverts$",   "flatverts"),
    ("cs_drawobj",  r"call cs_projall$",     "project"),
    ("cs_drawobj",  r"call cs_boxlod$",      "boxlod"),
    ("cs_drawobj",  r"call cs_faces$",       "faces"),
    ("cs_drawobj",  r"call cs_edges$",       "edges"),
    ("cs_drawobj",  r"call cs_markrows$",    "markrows"),
]
TIER4 = [                               # the panel's own internals
    ("cs_pitem",    r"call cs_pkey$",        "pkey"),
    ("cs_d_adi",    r"call cs_adwin$",       "adi_erase"),
    ("cs_d_adi",    r"call cs_seg$",         "adi_seg"),
    ("cs_d_adi",    r"call cs_sin$",         "adi_sin"),
    ("cs_d_adi",    r"call cs_isqrt$",       "adi_sqrt"),
    ("cs_pdisc",    r"call cs_prect$",       "disc_row"),
    ("cs_pdisc",    r"call cs_elhw$",        "disc_hw"),
    ("cs_elhw",     r"call cs_isqrt$",       "hw_sqrt"),
]
TIER5 = [                               # the rolled horizon's own band
    ("cs_skyground", r"call cs_hzrows$",     "hzrows"),      # above, below
    # (cs_hzrow_sh's two runs were bracketed here until they were INLINED -
    #  SPEC.md 88.3.1.2 carries what they measured: 664 cycles a row for 49
    #  bytes, of which ~350 were the stores - and `call [cs_hzproc]` was
    #  bracketed here until 88.3.1.3 FUSED the band's row into cs_skyground
    #  and deleted the vector, so the band is exclusive time here now)
    # ...and the SPAN PASS (88.3.1.1) is a walk of its own with no call in
    # it, so what it costs is cs_skyground's own EXCLUSIVE time here
]
TIER6 = [                               # the flight model's own calls (88.7)
    ("cs_step",     r"call cs_msgage$",      "msgage"),
    ("cs_step",     r"call \[di \+ CSP_ATT\]$", "attproc"),
    ("cs_step",     r"call cs_lift$",        "lift"),
    ("cs_step",     r"call cs_sin$",         "sin"),
    ("cs_step",     r"call cs_cos$",         "cos"),
    ("cs_step",     r"call cs_move$",        "move"),
    ("cs_step",     r"call cs_fence$",       "fence"),
    ("cs_step",     r"call cs_touch$",       "touch"),
    ("cs_step",     r"call cs_collide$",     "collide"),
]
TIER7 = [                               # THE VERTEX PIPELINE's own calls
    # 41.7 ms at 12 degrees and 39.2 of a 140 ms LEVEL frame - the largest
    # block in level flight, and the only one measured so far that does NOT
    # move with the bank. What this tier answers is whether its cost is the
    # MULTIPLIES (cs_rot's nine, cs_colscale's three a column, the
    # projection's two a vertex) or the per-vertex loop around them, because
    # SPEC.md 88.5.6's own note prices a vertex at ~1,200 cycles and two
    # `imul` is a quarter of that.
    ("cs_scale",      r"call cs_rot$",       "rot"),
    ("cs_stackverts", r"call cs_colscale$",  "colscale"),
    ("cs_projall",    r"call \[cs_projp\]$", "projp"),
]
TIER3 = [
    ("cs_faces",    r"call cs_axcull$",      "axcull"),
    ("cs_faces",    r"call cs_fclip$",       "fclip"),
    ("cs_faces",    r"call cs_poly$",        "poly"),
    ("cs_faces",    r"call cs_wire$",        "wire"),
    ("cs_poly",     r"call cs_edge$",        "edge"),
    ("cs_edge1",    r"call cs_seg$",         "seg"),
]

# --- the flight profiles ----------------------------------------------------
# Every one of them MOVES. `pos` is where it starts, `thr` the throttle it is
# given, `roll` the bank it is rolled to, `hold` whether that bank is put back
# every frame (a sustained turn) or left to decay (a released one).
PROFILES = {
    "cruise": dict(
        pos=(150, 300, -900), hdg=30, pitch=0, roll=0, thr=70,
        what="straight and level over the Champ de Mars heading north-east: "
             "the tower and the Trocadero drawing faces, La Defense and "
             "Montmartre as box impostors, the Seine and the axis road under "
             "them"),
    "bank": dict(
        pos=(150, 300, -900), hdg=30, pitch=0, roll=45, thr=70,
        what="the same view rolled 45 degrees and RELEASED, so the bank "
             "decays back towards level under the flight model's own easing "
             "(88.7.5) - the horizon sweeps, the ADI moves every frame, and "
             "the object set changes as the nose comes round"),
    "rollsweep": dict(
        pos=(150, 300, -900), hdg=30, pitch=0, roll=0, thr=70, sweep=2,
        what="the same view with the bank driven 2 degrees a frame, so the "
             "ADI's key (88.9.2) changes EVERY frame and the instrument "
             "redraws every frame - the profile the ADI's own modes are "
             "measured on, where `bank` only passes through that state"),
    "turnhold": dict(
        pos=(150, 300, -900), hdg=30, pitch=0, roll=45, thr=70, hold=True,
        what="...and the same bank HELD, so every frame refills a rolled "
             "horizon whole (88.3.1) for the whole run. The decaying one "
             "passes through this; this one lives in it"),
    "sparse": dict(
        pos=(-6000, 300, -6000), hdg=225, pitch=0, roll=45, thr=70, hold=True,
        what="turnhold's held 45 degree bank over an EMPTY quarter of the map "
             "- ONE object in the view, near the horizon, and nothing else at "
             "all. The other five profiles are busy on purpose; this is the "
             "one where a band row can be object-free, which is what SPEC.md "
             "88.3.1.3.2's horizon cache needs. THE AERODROME WAS TRIED FIRST "
             "and is the wrong scene: three short buildings and two of the "
             "Seine's ribbons reads as sparse and is not, because cs_markrows "
             "marks an object's BOX (88.3.2) and a flat ground model "
             "kilometres across has an enormous one - it measured 0 of 112 "
             "rows object-free, same as turnhold, with WIDER spans"),
    "roadpass": dict(
        pos=(-168, 30, 1613), hdg=115, pitch=0, roll=0, thr=100, spd=40,
        what="LOW ALONG THE AXIS ROAD at 30 m, 150 m abeam of it, flying PAST "
             "its middle vertex - so that vertex passes beside the eye with "
             "|cx| over NINE cz and the projection CLAMPS it. This is the "
             "profile the side clip (88.5.7) was taken for: the one in the "
             "set where the line actually WANDERS without it, where the other "
             "six are byte-identical"),
    "slightbank": dict(
        pos=(150, 33, -2000), hdg=30, pitch=0, roll=12, thr=100, spd=60,
        hold=True,
        what="LOW and BARELY BANKED - 33 m (the panel's 108 FEET) at 117 "
             "knots over Paris, the tower a spire ON THE HORIZON and the sky "
             "above it empty, banked 12 degrees and HELD. THE ANGLE IS THE "
             "FIELD'S OWN, derived from its screenshot rather than guessed: "
             "the horizon's screen slope is tan(roll) x scly/sclx (88.4.1), "
             "and 54 rows of drop over 398 px is 11.9 degrees. That is what "
             "a barely-perceptible tilt is - HALF THE VIEW'S ROWS split "
             "(88.3.1.3.5). Use --roll to walk it; 0 is the control"),
    "climb": dict(
        pos=None, hdg=None, pitch=8, roll=0, thr=100, spd=40,
        what="full throttle from where cs_reset puts it on the Issy runway: "
             "the runway polygon under the wheels at CS_NEARG (88.5.5) - "
             "forty full-width rows - plus the dashed centreline (88.6.2), "
             "the ground transition and a panel whose every field is moving"),
    "descend": dict(
        pos=(2600, 600, -2600), hdg=300, pitch=-12, roll=0, thr=40,
        what="nose down towards the city from 600 m: objects GROW through "
             "the LOD thresholds, so impostors become polygon models and "
             "outlines appear (88.5.4) - the cost cliff no still scene can "
             "show"),
}


def sites(defines=()):
    """Each stage's call bracket: (call address, the address after it), found
    by INSTRUCTION TEXT in nasm's own listing, never by remembered offset."""
    fd, lst = tempfile.mkstemp(prefix="skiesprof_", suffix=".lst")
    os.close(fd)
    r = subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                        "-I", "apps/skies/", "-I", CSWIDX] + list(defines) +
                       ["-o", lst + ".bin", "-l", lst, "apps/skies/skies.asm"],
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit("skiesprof: the tree does not assemble:\n" + r.stderr[:400])
    lines = open(lst).read().split("\n")
    os.unlink(lst)
    if os.path.exists(lst + ".bin"):
        os.unlink(lst + ".bin")
    rx = re.compile(r"\s*\d+\s+([0-9A-F]{8})\s+([0-9A-F\[\]]+)\s+"
                    r"(?:<\d+>\s*)?(.*)$")
    lab = re.compile(r"\s*\d+\s+(?:[0-9A-F]{8}\s+(?:[0-9A-F()\-\[\]]+\s+)?)?"
                     r"(?:<\d+>\s*)?([A-Za-z_][A-Za-z0-9_]*):")

    def find(scope, pat):
        hits, inside = [], False
        for L in lines:
            m = lab.match(L)
            if m:
                inside = (m.group(1) == scope)
            if not inside:
                continue
            m = rx.match(L)
            if m and re.search(pat, m.group(3).split(";")[0].rstrip()):
                b = bytes.fromhex(m.group(2).replace("[", "").replace("]", ""))
                a = int(m.group(1), 16)
                hits.append((a, a + len(b)))
        return hits
    return find


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--profile", default="cruise", choices=sorted(PROFILES))
    ap.add_argument("--frames", type=int, default=30)
    ap.add_argument("--tier", type=int, default=2,
                    choices=(1, 2, 3, 4, 5, 6, 7))
    ap.add_argument("--warm", type=int, default=6,
                    help="frames flown before the trace arms, so the first "
                         "frame after a poke - which redraws the whole panel "
                         "- is not one of the ones reported")
    ap.add_argument("--hzfull", type=int, default=None, choices=(0, 1),
                    help="poke cs_hzfull: 1 puts the rolled horizon back on "
                         "the whole-row refill (SPEC.md 88.3.1.1's A/B)")
    ap.add_argument("--roll", type=float, default=None,
                    help="override the profile's bank, everything else the "
                         "same. THE A/B FOR WHAT A BANK COSTS: a profile is "
                         "one scene from one place, so the only honest "
                         "control for a rolled frame is the SAME scene "
                         "level (SPEC.md 88.3.1.3.5)")
    ap.add_argument("--noshort", type=int, default=None, choices=(0, 1),
                    help="poke [cs_slnoshort]: 1 puts a SHORT sliced run back "
                         "on the general row body, which is what shipped "
                         "before SPEC.md 88.4.6.2. The A/B, on one binary")
    ap.add_argument("--noside", type=int, default=None, choices=(0, 1),
                    help="poke [cs_noside]: 1 turns SPEC.md 88.5.7's side "
                         "clip off wholly, so a clamped point is drawn to and "
                         "the line through it wanders as the 1983 original's "
                         "did. The A/B for it, in flight")
    ap.add_argument("--nostep", type=int, default=None, choices=(0, 1),
                    help="poke cs_mknostep: 1 puts a thin diagonal's mark "
                         "back on its BOX (SPEC.md 88.3.2.2's A/B)")
    ap.add_argument("--csv", help="write the per-frame table here")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    P = dict(PROFILES[a.profile])
    if a.roll is not None:
        P["roll"] = a.roll
    MP = dispapps._map("skies")

    def off(n):
        return MP[n] - MP["os88_image_end"]

    find = sites()
    stages, raw = [], []            # (listing offset, kind, name)
    # tier 5 is TIER1 plus the band's internals, and tier 6 TIER1 plus the
    # FLIGHT MODEL's - never the object tiers with either, a breakpoint
    # inside a 112-iteration loop being expensive enough alone
    want = ({5: [1, 5], 6: [1, 6], 7: [1, 2, 7]}.get(a.tier)
            or list(range(1, a.tier + 1)))
    for tier, rows in ((1, TIER1), (2, TIER2), (3, TIER3), (4, TIER4),
                       (5, TIER5), (6, TIER6), (7, TIER7)):
        if tier not in want:
            continue
        for scope, pat, name in rows:
            hits = find(scope, pat)
            if not hits:
                sys.exit("skiesprof: no site for %s in %s" % (pat, scope))
            for i, (c, rt) in enumerate(hits):
                nm = name if len(hits) == 1 else "%s#%d" % (name, i)
                stages.append(nm)
                raw.append((c, "<", nm))
                raw.append((rt, ">", nm))

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        slot, seg, base = skiestest.open_game(m)
        lin = seg << 4
        # THE LISTING'S ADDRESSES ARE OFFSETS IN THE PACKAGE (org 0) and a
        # breakpoint takes a FLAT one. Without the base the whole set arms
        # near 0, nothing ever hits, and the wait reports a guest that flew
        # 3,000 of its own seconds without a frame - which points at the
        # aeroplane rather than at the arming.
        # ...and ONE ADDRESS CAN CARRY TWO EVENTS. Two `call`s in a row put
        # the first one's RETURN on the second one's CALL - `call cs_stick`
        # is followed immediately by `call cs_step` - so a dict of one event
        # an address silently drops half the brackets, and the stages that
        # vanish are exactly the ones followed by another call. Returns are
        # applied before calls at a shared address, which is the order they
        # happen in.
        addrs = {}
        for o, k, nm in raw:
            addrs.setdefault(lin + o, []).append((k, nm))
        for v in addrs.values():
            v.sort(key=lambda e: 0 if e[0] == ">" else 1)

        def w(n, sz=2):
            return int.from_bytes(m.readseg(seg, base + off(n), sz), "little")

        def sw(n):
            v = w(n)
            return v - 0x10000 if v >= 0x8000 else v

        def poke(n, d):
            m.write(lin + base + off(n), d)

        m.type_text("f")
        m.advance(frames=30)
        m.run()
        print("  backend %d, view %dx%d"
              % (w("cs_back") & 0xFF, w("cs_ww"), w("cs_wh")))
        print("  profile %s: %s%s" % (a.profile, P["what"],
              "" if a.hzfull is None else "  [cs_hzfull=%d]" % a.hzfull)
              + ("" if a.noside is None else "  [cs_noside=%d]" % a.noside))

        # --- put the aeroplane where the profile wants it, ONCE -------------
        m.pause()
        if P["pos"]:
            x, y, z = P["pos"]
            for n, v in (("cs_px", x), ("cs_py", y), ("cs_pz", z)):
                poke(n, ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_hdg", ((P["hdg"] * 65536 // 360) & 0xFFFF)
                 .to_bytes(2, "little"))
            poke("cs_pitch", ((P["pitch"] * 65536 // 360) & 0xFFFF)
                 .to_bytes(2, "little"))
            poke("cs_state", b"\x01")
            poke("cs_spd", (P.get("spd", 40) * 128).to_bytes(2, "little"))
        poke("cs_roll", ((int(P["roll"] * 65536 / 360)) & 0xFFFF)
             .to_bytes(2, "little"))
        poke("cs_thr", P["thr"].to_bytes(2, "little"))
        if a.hzfull is not None:
            poke("cs_hzfull", bytes([a.hzfull]))
        if a.nostep is not None:
            poke("cs_mknostep", bytes([a.nostep]))
        if a.noshort is not None:
            poke("cs_slnoshort", bytes([a.noshort]))
        if a.noside is not None:
            poke("cs_noside", bytes([a.noside]))
        if P["pos"] is None:            # the runway start: it is ON the strip
            poke("cs_pitch", ((P["pitch"] * 65536 // 360) & 0xFFFF)
                 .to_bytes(2, "little"))
            poke("cs_spd", (P.get("spd", 40) * 128).to_bytes(2, "little"))
            poke("cs_state", b"\x01")
        # a poke is a teleport, so the cull's skip counters go (88.5.2)
        ap_ = w("cs_airport")
        objs = int.from_bytes(m.read(lin + ap_ + 18, 2), "little")
        nobj = int.from_bytes(m.read(lin + ap_ + 20, 2), "little")
        for o in range(objs, objs + nobj * 20, 20):
            m.write(lin + o + 18, b"\x00\x00")
        m.run()
        m.advance(frames=a.warm * 8)        # fly a little before arming

        rollv = ((int(P["roll"] * 65536 / 360)) & 0xFFFF).to_bytes(2, "little")
        hold = P.get("hold", False)
        sweep = P.get("sweep", 0)
        swept = [P["roll"]]
        state = []

        def on_hit(mm, rec):
            """At the frame's own boundary, read what the aeroplane is doing
            - so the report can show that the profile did what it claims."""
            ev = addrs.get(rec["addr"], ())
            # --- A HELD BANK IS PINNED AT THE MATRIX, NOT AT THE INPUT ------
            # Pinning it at the frame's start and letting the model run means
            # the roll cs_matrix actually sees is 45 degrees LESS whatever
            # 88.7.5's easing rolled out over the ticks that frame - and the
            # tick count per frame alternates, so the horizon alternated
            # between exactly two positions three rows apart and read as a
            # wobbling aeroplane. It was the pin wobbling.
            if hold and ("<", "matrix") in ev:
                poke("cs_roll", rollv)
            if ("<", "input") not in ev:
                return None
            if hold:
                poke("cs_roll", rollv)
            if sweep:               # drive the bank so the ADI's key moves
                swept[0] = (swept[0] + sweep) % 360
                poke("cs_roll", ((int(swept[0] * 65536 / 360)) & 0xFFFF)
                     .to_bytes(2, "little"))
            state.append(dict(
                roll=sw("cs_roll") * 360.0 / 65536,
                pitch=sw("cs_pitch") * 360.0 / 65536,
                hdg=(w("cs_hdg") * 360.0 / 65536) % 360,
                alt=sw("cs_py" ) if False else
                    int.from_bytes(m.readseg(seg, base + off("cs_py"), 4),
                                   "little") / 256.0,
                spd=w("cs_spd") / 128.0,
                nvis=w("cs_nvisn")))
            return True

        want = a.frames
        with os88marty.bp_trace(m, *sorted(addrs), poll=0.0008, cap=400000,
                                on_hit=on_hit) as tr:
            os88marty.until(m, lambda _: len(state) > want,
                            "%d flown frames" % want, poll=0.5, limit=900.0,
                            guest=3000.0)
        report(tr, addrs, stages, state, a, P)


def report(tr, addrs, stages, state, a, P):
    """Walk the stops, keeping a stack: a bracket's INCLUSIVE cost is one
    subtraction, and its EXCLUSIVE cost is that less the brackets inside."""
    frames = []                     # per frame: {stage: [incl, excl]}, total
    cur, stack, t0 = None, [], None
    for h in tr.hits:
        c = h["cycles"]
        for kind, nm in addrs.get(h["addr"], ()):
          if True:
            if kind == "<":
                if nm == "input":   # the loop's own boundary
                    if cur is not None and t0 is not None:
                        cur["_total"] = c - t0
                        frames.append(cur)
                    cur, stack, t0 = {}, [], c
                stack.append((nm, c, 0))
            else:
                if not stack or stack[-1][0] != nm or cur is None:
                    continue        # a bracket whose partner was lost
                name, c0, kids = stack.pop()
                incl = c - c0
                e = cur.setdefault(name, [0, 0, 0])
                e[0] += incl
                e[1] += incl - kids
                e[2] += 1
                if stack:
                    pr = stack[-1]
                    stack[-1] = (pr[0], pr[1], pr[2] + incl)
    if len(frames) > a.frames:
        frames = frames[:a.frames]
    n = len(frames)
    if not n:
        sys.exit("skiesprof: no complete frames were traced")
    tot = [f["_total"] for f in frames]
    ms = lambda c: c / CPS * 1000.0
    print("  %d frames traced, %d stops (%d kept)%s"
          % (n, tr.n, len(tr.hits), "  OVERFLOWED" if tr.overflowed else ""))
    print()
    print("  FRAME: mean %.1f ms (%.2f fps), min %.1f, max %.1f, spread %.0f%%"
          % (ms(sum(tot) / n), 1000 / ms(sum(tot) / n), ms(min(tot)),
             ms(max(tot)), 100.0 * (max(tot) - min(tot)) / (sum(tot) / n)))
    if state:
        s0, s1 = state[0], state[min(len(state) - 1, n)]
        print("  FLIGHT: roll %+.1f -> %+.1f deg, pitch %+.1f -> %+.1f, "
              "hdg %.0f -> %.0f, alt %.0f -> %.0f m, spd %.0f -> %.0f m/s, "
              "objects %d -> %d"
              % (s0["roll"], s1["roll"], s0["pitch"], s1["pitch"], s0["hdg"],
                 s1["hdg"], s0["alt"], s1["alt"], s0["spd"], s1["spd"],
                 s0["nvis"], s1["nvis"]))
    print()
    print("  %-12s %>9s %>7s %>9s %>7s %>7s %>7s"
          .replace(">", "") % ("stage", "incl ms", "%", "excl ms", "%",
                               "calls", "max ms"))
    print("  " + "-" * 62)
    seen = set()
    order = [s for s in stages if s not in seen and not seen.add(s)]
    mean = sum(tot) / n
    for s in order:
        rows = [f[s] for f in frames if s in f]
        if not rows:
            continue
        i = sum(r[0] for r in rows) / n
        e = sum(r[1] for r in rows) / n
        k = sum(r[2] for r in rows) / n
        mx = max(r[0] for r in rows)
        print("  %-12s %9.2f %6.1f%% %9.2f %6.1f%% %7.1f %7.2f"
              % (s, ms(i), 100 * i / mean, ms(e), 100 * e / mean, k, ms(mx)))
    acc = sum(sum(f[s][1] for s in f if s != "_total") for f in frames) / n
    print("  " + "-" * 62)
    print("  %-12s %9.2f %6.1f%%  <- the loop's own arithmetic, and every "
          "call it makes that is not bracketed above"
          % ("unaccounted", ms(mean - acc), 100 * (mean - acc) / mean))
    if a.csv:
        with open(a.csv, "w") as fh:
            fh.write("frame,total_ms," + ",".join(order) + "\n")
            for j, f in enumerate(frames):
                fh.write("%d,%.3f," % (j, ms(f["_total"])))
                fh.write(",".join("%.3f" % ms(f[s][0]) if s in f else "0"
                                  for s in order) + "\n")
        print("  wrote %s" % a.csv)


if __name__ == "__main__":
    main(sys.argv[1:])
