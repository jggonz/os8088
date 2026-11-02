#!/usr/bin/env python3
"""Nothing SOUND.DRV keeps reaches into what its attach hands back.

    python3 tests/unit/t_sndonce.py

SPEC.md 34.15.1 lays SOUND.DRV out in five runs and the attach shrinks the
claim to the end of the last of the middle three it found a user for
(DRV_H_KEEP, SPEC.md 51.1.3):

    [the tiers] snd_keep: [PicoMEM] snd_keeppm: [MPU-401] snd_keepmpu:
    [mixer + volume popup] snd_keepmix: [attach-only]

So after DRVV_ATTACH has returned, a byte past snd_keepmix may be somebody
else's on any machine, a byte past snd_keepmpu on a machine with no mixer,
and so on down. Every rule is a layout rule that assembles cleanly when
broken: a later edit that calls opl_probe from the tier change, or adds a
mixer verb to snd_pkg without its [mix_kind] guard, runs fine on every
machine whose claim the heap has not yet handed on - and corrupts or jumps
into whatever has it on the rest. This reads the assembler's own listing and
refuses:

  1. any reference from below snd_keepmix to a label at or past it, outside
     snd_entry - the attach, which runs before the shrink and is the one
     place allowed to call the probes;
  2. any reference from a run to a label in a LATER one of the middle three
     (a run is kept whenever a later one is, so the other way is free)
     outside that run's doors - the routines SPEC.md 34.15.1 names, every
     one behind its run's own test.

Break it on purpose: `call opl_probe` in snd_tier fails (1), a new `call
mix_get` anywhere in sb.inc fails (2), and so does a `call mpu_put` in
sbl_tick or a `call pm_runaway` in snd_tier.
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(
    __file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88drvsym                                       # noqa: E402

ATTACH = {"snd_entry"}                  # rule 1's one door
# rule 2's doors, run by run. The PicoMEM's: sbl_tick asks [pm_up] before
# pm_runaway, and pm_tickpub publishes pm_tick only with [pm_base]. The
# MPU-401's: snd_pkg's verb table (snd_pkg refuses verbs 2..5 without
# [mpu_base]) and mpu_detach, which reaches mpu_hush only with [mpu_own].
# The mixer's: snd_pkg's verb table and snd_detach test [mix_kind] at the
# call; sbl_hw_start calls sbl_pro_st (sb.inc's SBL_PRO) only for a stream
# verb 11 took as stereo, which needs SND_CAP_STEREO, which snd_sb_pub grants
# a DSP 3.xx only with [mix_kind] = 2, and sbl_stop_stream's sbl_pro_end is
# behind [sbl_pst], which only sbl_pro_st sets (SPEC.md 34.15.1)
RUNS = [("snd_keep", "snd_keeppm", "the PicoMEM's", {"sbl_tick", "pm_tickpub"}),
        ("snd_keeppm", "snd_keepmpu", "the MPU-401's", {"snd_ptab", "mpu_detach"}),
        ("snd_keepmpu", "snd_keepmix", "the mixer's",
         {"snd_ptab", "snd_detach", "sbl_hw_start", "sbl_stop_stream"})]
LST = re.compile(r"^\s*\d+\s+([0-9A-F]{8})\s+\S")
LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")
IDENT = re.compile(r"(?<![.\w])([A-Za-z_][A-Za-z0-9_]*)")


def listing():
    line, real = os88drvsym.recipe("sound.bin")
    with tempfile.TemporaryDirectory() as d:
        b, lst = os.path.join(d, "x.bin"), os.path.join(d, "x.lst")
        line = line.replace(" -o %s " % real, " -o %s -l %s " % (b, lst), 1)
        r = subprocess.run(["sh", "-c", line], cwd=ROOT, capture_output=True,
                           text=True)
        if r.returncode:
            sys.exit("t_sndonce: %s\n%s" % (line, r.stderr))
        return open(lst, errors="replace").read().splitlines()


def main():
    sym = os88drvsym.symbols("sound.bin")
    keepmix = sym["snd_keepmix"]
    runs = [(sym[a], sym[b], what, doors) for a, b, what, doors in RUNS]

    def run(at):                        # 0 = the tiers, 1..3 the middle runs
        for i, (lo, hi, _, _) in enumerate(runs):
            if lo <= at < hi:
                return i + 1
        return 0
    bad, routine, seen = [], None, 0
    for ln in listing():
        m = LST.match(ln)
        # the source text follows the hex columns (and a <n> macro depth)
        src = re.sub(r"^\s*\d+\s+(?:[0-9A-F]{8}\s+\S+\s*)?(?:<\d+>)?", "",
                     ln).split(";", 1)[0]
        lab = LABEL.match(src.strip())
        if lab and lab.group(1) in sym:
            routine = lab.group(1)
        if not m:
            continue
        at = int(m.group(1), 16)
        if at >= keepmix:
            continue
        seen += 1
        for name in IDENT.findall(src):
            if name not in sym or name == routine:
                continue
            to = sym[name]
            if to >= keepmix and routine not in ATTACH:
                bad.append("%s (at %04X) names %s, past snd_keepmix - "
                           "attach-only code is gone once the attach "
                           "returns" % (routine, at, name))
                continue
            rt = run(to)
            if rt and rt > run(at) and routine not in runs[rt - 1][3] \
                    and routine not in ATTACH:
                bad.append("%s (at %04X) names %s, in %s run - gone on a "
                           "machine without it, and only %s test for it"
                           % (routine, at, name, runs[rt - 1][2],
                              ", ".join(sorted(runs[rt - 1][3]))))
    if seen < 1000:
        sys.exit("t_sndonce: read only %d listing lines below snd_keepmix - "
                 "the listing format moved and this checked nothing" % seen)
    for b in bad:
        print("FAIL: " + b)
    print("t_sndonce: %d lines below snd_keepmix (%d); keeps: tiers %d, "
          "+PicoMEM %d, +MPU-401 %d, +mixer %d: %s"
          % (seen, keepmix, sym["snd_keep"], sym["snd_keeppm"],
             sym["snd_keepmpu"], keepmix,
             "%d FAILED" % len(bad) if bad else "clean"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
