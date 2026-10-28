#!/usr/bin/env python3
"""Nothing SOUND.DRV keeps reaches into what its attach hands back.

    python3 tests/unit/t_sndonce.py

SPEC.md 34.15.1 lays SOUND.DRV out in three runs and the attach shrinks the
claim to the end of the first or the second (DRV_H_KEEP, SPEC.md 51.1.3):

    [the tiers] snd_keep: [mixer + volume popup] snd_keepmix: [attach-only]

So after DRVV_ATTACH has returned, a byte past snd_keepmix may be somebody
else's on any machine, and a byte past snd_keep on a machine with no mixer.
Both rules are layout rules that assemble cleanly when broken: a later edit
that calls opl_probe from the tier change, or adds a mixer verb to snd_pkg
without its [mix_kind] guard, runs fine on every machine whose claim the
heap has not yet handed on - and corrupts or jumps into whatever has it on
the rest. This reads the assembler's own listing and refuses:

  1. any reference from below snd_keepmix to a label at or past it, outside
     snd_entry - the attach, which runs before the shrink and is the one
     place allowed to call the probes;
  2. any reference from below snd_keep to a label in [snd_keep,
     snd_keepmix) outside snd_pkg's verb table, snd_detach's guarded
     call and an SB Pro stereo stream's two - the doors SPEC.md 34.15.1
     names, every one behind [mix_kind].

Break it on purpose: `call opl_probe` in snd_tier fails (1), and a new `call
mix_get` anywhere in sb.inc fails (2).
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
# rule 2's doors, behind [mix_kind]: snd_pkg's verb table and snd_detach
# test it at the call; an SB Pro STEREO stream tests it at attach instead -
# sbl_pro_st runs only for a stream verb 11 took as stereo, which needs
# SND_CAP_STEREO, which snd_sb_pub grants a DSP 3.xx only with [mix_kind] = 2,
# and sbl_stop_stream's restore is behind [sbl_pst], which only sbl_pro_st
# sets (SPEC.md 34.15.1)
MIXDOORS = {"snd_ptab", "snd_detach", "sbl_pro_st", "sbl_stop_stream"}
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
    keep, keepmix = sym["snd_keep"], sym["snd_keepmix"]
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
            elif keep <= to < keepmix and at < keep \
                    and routine not in MIXDOORS | ATTACH:
                bad.append("%s (at %04X) names %s, past snd_keep - the "
                           "mixer's code is gone on a machine with none, "
                           "and only snd_pkg and snd_detach test [mix_kind]"
                           % (routine, at, name))
    if seen < 1000:
        sys.exit("t_sndonce: read only %d listing lines below snd_keepmix - "
                 "the listing format moved and this checked nothing" % seen)
    for b in bad:
        print("FAIL: " + b)
    print("t_sndonce: %d lines below snd_keepmix (%d), snd_keep %d: %s"
          % (seen, keepmix, keep, "%d FAILED" % len(bad) if bad else "clean"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
