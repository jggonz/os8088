#!/usr/bin/env python3
"""EXCITEBIKE course authoring (an OFFLINE tool, not a build dependency).

    python3 tools/excitebike_tracks.py --write      # (re)generate tracks/t3.trk .. t5.trk
    python3 tools/excitebike_tracks.py --pars       # run the reference rider, write each par/par2

The committed `apps/excitebike/tracks/*.trk` files are the SOURCE; this script is
the record of how t3, t4 and t5 were first laid out - a seeded random walk over the
piece catalogue under the compiler's own authoring rules (a lap of at most 797
columns, at least 70% plain, an obstacle at least 8 columns after the last, opening
with 8 plain columns, ending in the finish piece) with a difficulty ramp: each
course has more obstacles, and heavier ones, than the last.  After generation the
files are edited by hand as needed; `--pars` then makes each par what the rules
demand, the reference rider's turbo time plus 8% (tools/exbsim.py --run-track), for
both passes (the second pass is the harder `swap` set on both laps).  Standard
library only; nothing is read from outside apps/excitebike and tools/.
"""
import argparse
import os
import random
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
TRK = os.path.join(ROOT, "apps", "excitebike", "tracks")

# (title, theme, seed, obstacles, weights by kind, gap range in columns)
COURSES = {
    "t3": ("Dusk Canyon", 1, 3031, 34,
           {"rough": 5, "mud": 3, "hlo": 4, "hhi": 1, "arrow": 3, "kicker": 2, "ramp": 6, "hill": 3},
           (8, 15)),
    "t4": ("Frost Ridge", 3, 4041, 42,
           {"rough": 4, "mud": 4, "hlo": 4, "hhi": 3, "arrow": 3, "kicker": 3, "ramp": 6, "hill": 6},
           (8, 13)),
    "t5": ("Night Circuit", 4, 5051, 50,
           {"rough": 3, "mud": 5, "hlo": 4, "hhi": 5, "arrow": 3, "kicker": 3, "ramp": 6, "hill": 8},
           (8, 12)),
}
KINDS = {
    "rough": ["rough_a", "rough_b", "rough_c"],
    "mud": ["mud_a", "mud_b", "mud_c"],
    "hlo": ["hurdle_lo_a", "hurdle_lo_b", "hurdle_lo_c"],
    "hhi": ["hurdle_hi_a", "hurdle_hi_b", "hurdle_hi_c"],
    "arrow": ["arrow_a", "arrow_b", "arrow_c"],
    "kicker": ["kicker_a", "kicker_b"],
    "ramp": ["ramp_a", "ramp_b", "ramp_c", "ramp_d", "ramp_e", "ramp_f", "ramp_g", "ramp_h"],
    "hill": ["hill_a", "hill_b", "hill_c", "hill_d", "hill_e", "hill_f"],
}
# the second pass: what each course's harder set swaps (every occurrence, both laps)
SWAPS = {
    "t3": [("hurdle_lo_a", "hurdle_hi_a"), ("hurdle_lo_b", "hurdle_hi_b"), ("rough_a", "mud_a"),
           ("rough_c", "mud_c"), ("ramp_a", "ramp_c"), ("kicker_a", "kicker_b"),
           ("hill_a", "hill_c")],
    "t4": [("hurdle_lo_c", "hurdle_hi_c"), ("hurdle_lo_a", "hurdle_hi_a"), ("mud_c", "mud_b"),
           ("rough_b", "mud_b"), ("ramp_d", "ramp_g"), ("hill_b", "hill_e"), ("hill_a", "hill_c"),
           ("kicker_b", "kicker_a")],
    "t5": [("hurdle_lo_b", "hurdle_hi_b"), ("hurdle_lo_c", "hurdle_hi_c"), ("mud_a", "mud_b"),
           ("rough_a", "mud_a"), ("ramp_b", "ramp_e"), ("ramp_f", "ramp_g"), ("hill_d", "hill_f"),
           ("hill_a", "hill_e"), ("kicker_a", "kicker_b")],
}


def plain_run(rng, n):
    """`n` plain columns as `plain_x xM` lines, the three plain ids mixed."""
    out = []
    while n > 0:
        k = min(n, rng.randint(1, 7))
        out.append("%s x%d" % (rng.choice(["plain_a", "plain_b", "plain_c"]), k))
        n -= k
    return out


def make(name):
    title, theme, seed, n_obst, weights, (glo, ghi) = COURSES[name]
    rng = random.Random(seed)
    kinds = [k for k, w in weights.items() for _ in range(w)]
    # the lap: 8 plain columns, then (obstacle, gap) n times, then the finish piece
    lines = plain_run(rng, rng.randint(8, 12))
    last = None
    for _ in range(n_obst):
        k = rng.choice(kinds)
        while k == last and k in ("hhi", "mud"):      # no two heavy pieces in a row
            k = rng.choice(kinds)
        last = k
        lines.append(rng.choice(KINDS[k]))
        lines += plain_run(rng, rng.randint(glo, ghi))
    lines.append("finish")
    txt = ["track %s" % title, "theme %d" % theme, "laps 2",
           "par 9:59.99 placeholder: run tools/excitebike_tracks.py --pars",
           "par2 9:59.99 placeholder: run tools/excitebike_tracks.py --pars", "",
           "# pass 1: one piece (or `piece xN`) a line", ""] + lines + ["", "pass2",
           "# every occurrence of the left piece becomes the right one in BOTH laps of the second pass round"]
    txt += ["swap %s %s" % s for s in SWAPS[name]]
    return "\n".join(txt) + "\n"


def set_pars():
    import exbsim
    art = exbsim.X.Art()
    for c, t in enumerate(art.tracks):
        name = t["file"][:-4]
        path = os.path.join(TRK, t["file"])
        text = open(path).read()
        for flag, key in ((0, "par"), (1, "par2")):
            ok, steps, cs, crashes = exbsim.run_track(c, art=art, flag=flag)
            if not ok:
                sys.exit("%s pass %d: the reference rider did not finish" % (name, flag + 1))
            par = exbsim.par_of(cs)
            note = "the turbo time %s + 8%% (tools/exbsim.py --run-track %s)" % (exbsim.fmt_cs(cs), name)
            text = re.sub(r"^%s .*$" % key, "%s %s %s" % (key, exbsim.fmt_cs(par), note), text,
                          count=1, flags=re.M)
            print("%s %s: turbo %s in %d steps, %d crashes -> %s %s" % (
                name, key, exbsim.fmt_cs(cs), steps, crashes, key, exbsim.fmt_cs(par)))
        open(path, "w").write(text)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--pars", action="store_true")
    o = ap.parse_args()
    if o.write:
        # the compiler wants every course to parse, so write into place one at a time
        for name in COURSES:
            with open(os.path.join(TRK, name + ".trk"), "w") as f:
                f.write(make(name))
            print("wrote", name)
    if o.pars:
        set_pars()
    if not (o.write or o.pars):
        ap.print_help()


if __name__ == "__main__":
    main()
