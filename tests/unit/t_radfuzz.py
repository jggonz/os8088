#!/usr/bin/env python3
"""MUTATED files through the shipped validator and both engines, on the host
(SPEC.md 96.4.4, 96.4.5, 96.8).

    python3 tests/unit/t_radfuzz.py       (needs nasm and Python's unicorn)
    python3 tests/unit/t_radfuzz.py --cases 3000 --seed 7

What the committed gates compare is `tests/unit/radrows.py`'s hand-built
hostile-file table (116 rows) and five fixtures, on the machine. A defect in
`rv_valid` or in the engines that neither the table nor a fixture happens to
land on passes every one of them. This row is the breadth those cannot have:
the SAME rig `t_radrace` uses - RADPLAY.DRV assembled here and run in
Unicorn's 16-bit x86, driven the way SOUND.DRV drives it - fed mutants of the
five fixtures from a SEEDED PRNG, so every case is reproducible by its seed.

Two claims, and the second is the one worth the runtime:

  1. VALIDATION. For each mutant the driver's `RADV_LOAD` answer - CF, AL, AH
     and CX - must be `tools/radsim.py`'s `check` to the byte, including the
     RADC_* reason and the file OFFSET of the failure. radsim models steps
     2-5 as well, so those are applied here first and only a mutant that
     passes them reaches the overlay, which is exactly the contract
     `rv_valid` documents ("steps 1-5 passed by the resident").
  2. REPLAY. Every mutant BOTH accept then plays, in a -DRADLOG build, for
     `--frames` frames from a fresh start and a stop, and its register log
     must be radsim's sent stream for the same file byte for byte. An
     accepted mutant is a valid RAD file nobody wrote: it reaches note,
     effect and riff states the five fixtures do not, and it is the engines
     rather than the validator that it exercises.

The negative control is a mutated VALIDATOR, not a mutated file: one of
`rv_valid`'s bounds checks is taken out and the row must go red - so a text
drift that stops the mutation applying is itself a failure, the shape
`t_radrace`'s control already uses.

No emulator: this is a unit row and runs in the soak tier beside
`t_radrace`, which it shares its rig with.
"""
import argparse
import os
import random
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, done                          # noqa: E402
import radsim                                            # noqa: E402

try:
    import unicorn                                       # noqa: F401
except ImportError:
    print("t_radfuzz: SKIP - Python's unicorn module is not installed "
          "(pip install unicorn)")
    sys.exit(0)

import t_radrace                                         # noqa: E402
from unicorn.x86_const import UC_X86_REG_AX, UC_X86_REG_CX   # noqa: E402

CLAIM = t_radrace.CLAIM
HALTSEG = t_radrace.HALTSEG
RAD_TUNE = t_radrace.RAD_TUNE
RADV_LOAD, RADV_START, RADV_STOP = 1, 2, 5
LOGSEG = 0x8000                 # 32KB of RAD_LOGKB, above the rig's segments
RAD_MAXLEN = 49152
TIMEOUT_US = 20 * 1000 * 1000   # a verb that never returns is a FINDING, not
                                # a hang: Unicorn stops and `call` raises
SIG = radsim.SIG


class Rig(t_radrace.Rig):
    """t_radrace's rig with a wall clock on each verb and the RADLOG buffer
    wired up, so a mutant that loops cannot hang the row."""

    def run(self, count=None):
        if self.done():
            return
        self.steps = 0
        self.limit = count
        try:
            self.mu.emu_start(self.here(), HALTSEG * 16, TIMEOUT_US)
        finally:
            self.limit = None

    def load_tune(self, tune, opl3=True):
        """RADV_LOAD on `tune` with the log armed. -> (CF, AL, AH, CX)."""
        self.mu.mem_write(LOGSEG * 16, b"\0\0")
        self.mu.mem_write(CLAIM * 16, self.img)
        self.mu.mem_write(CLAIM * 16 + RAD_TUNE, tune)
        self.w(34, 0, 1)                       # tick class
        self.w(35, 1 if opl3 else 0, 1)        # RO_IS3: which chip answered
        self.w(36, t_radrace.RSEG, 2)
        self.w(38, 0x100, 2)
        self.w(40, 0, 2)                       # the stack top: 64KB
        self.w(42, LOGSEG, 2)                  # the RADLOG buffer
        self.w(44, len(tune), 2)
        cf = self.call(RADV_LOAD)
        ax = self.mu.reg_read(UC_X86_REG_AX)
        return cf, ax & 0xFF, ax >> 8, self.mu.reg_read(UC_X86_REG_CX)

    def log(self):
        """(records, filled) out of the RADLOG buffer."""
        n = int.from_bytes(bytes(self.mu.mem_read(LOGSEG * 16, 2)), "little")
        full = bool(n & 0x8000)
        n &= 0x7FFF
        return radsim.decode_rlg(bytes(self.mu.mem_read(LOGSEG * 16 + 2, n))), full


def resident_steps(b, opl3=True):
    """Steps 2-5 of verb 4, which the RESIDENT makes and the overlay is
    entitled to assume (radval.inc). -> a radsim triple, or None.

    Step 5 is modelled too, because a `head` mutation can turn a 1.0 base's
    version byte into 21h and the OPL2 arm below would then disagree with
    radsim about RADE_NEEDOPL3 rather than about anything in the engine."""
    if len(b) < 17 or b[:16] != SIG:
        return (radsim.RADE["NOTRAD"], 0, 0)
    if b[0x10] not in (0x10, 0x21):
        return (radsim.RADE["VERSION"], 0, 0)
    if len(b) > RAD_MAXLEN:
        return (radsim.RADE["BIG"], 0, 0)
    if b[0x10] == 0x21 and not opl3:
        return (radsim.RADE["NEEDOPL3"], 0, 0)
    return None


def mutate(rng, base):
    """One mutant of `base`, and the name of what was done to it."""
    b = bytearray(base)
    kind = rng.choice(("flip", "flip", "byte", "byte", "burst", "trunc",
                       "grow", "head"))
    if kind == "flip":                         # one bit, anywhere past the sig
        i = rng.randrange(0x10, len(b))
        b[i] ^= 1 << rng.randrange(8)
    elif kind == "byte":                       # one byte, any value
        i = rng.randrange(0x10, len(b))
        b[i] = rng.randrange(256)
    elif kind == "burst":                      # a run of them
        i = rng.randrange(0x10, len(b))
        for j in range(i, min(len(b), i + rng.randrange(2, 9))):
            b[j] = rng.randrange(256)
    elif kind == "trunc":                      # every structure runs off the end
        b = b[:rng.randrange(17, len(b))]
    elif kind == "grow":                       # trailing junk: legal, ignored
        b += bytes(rng.randrange(256) for _ in range(rng.randrange(1, 40)))
    else:                                      # the header itself
        i = rng.randrange(0x10, 0x13)
        b[i] = rng.randrange(256)
    return bytes(b), kind


def one(rig, data, frames, opl3=True):
    """(problem or None, accepted, frames played) for one mutant."""
    want = radsim.check(data, opl3=opl3)
    try:
        cf, al, ah, cx = rig.load_tune(data, opl3=opl3)
    except RuntimeError as e:
        return "RADV_LOAD did not return (%s)" % e, False, 0
    if want is None:
        if cf:
            return ("driver refuses AL %d AH %d CX %d, radsim accepts"
                    % (al, ah, cx)), False, 0
    else:
        got = (al, ah if al == radsim.RADE["CORRUPT"] else 0,
               cx if al == radsim.RADE["CORRUPT"] else 0)
        if not cf:
            return "driver accepts, radsim refuses %r" % (want,), False, 0
        if got != want:
            return "driver %r, radsim %r" % (got, want), False, 0
        return None, False, 0
    # accepted by both: play it and compare the register stream. The tick
    # count is CAPPED - a mutant may HALT (a legal end, SPEC.md 34.12.2) and
    # then no tick advances a frame again, so the loop cannot be "until
    # `frames` frames" alone. Whatever it played is what radsim is asked for.
    try:
        rig.call(RADV_START)
        for _ in range(frames * 4 + 200):
            log, full = rig.log()
            if full or sum(1 for r, _ in log if r == radsim.LOG_FRAME_END) >= frames:
                break
            rig.tick()
        rig.call(RADV_STOP)
    except RuntimeError as e:
        return "a verb did not return while playing (%s)" % e, True, 0
    log, full = rig.log()
    if full:
        return "the RADLOG buffer filled in %d frames" % frames, True, 0
    n = sum(1 for r, _ in log if r == radsim.LOG_FRAME_END)
    if not n:
        return "not one frame played", True, 0
    ref = radsim.sent_stream(radsim.reference_stream(data, n, opl3=opl3))
    if log != ref:
        for i, (a, b) in enumerate(zip(log, ref)):
            if a != b:
                fr = sum(1 for r, _ in log[:i] if r == radsim.LOG_FRAME_END)
                return ("record %d (frame %d): driver %03x=%02x, radsim %03x=%02x"
                        % (i, fr, a[0], a[1], b[0], b[1])), True, n
        return ("lengths differ: driver %d records, radsim %d"
                % (len(log), len(ref))), True, n
    return None, True, n


def sweep(img, syms, cases, seed, frames, bases):
    """(problems, accepted, refused, kinds, frames played) over `cases`."""
    rng = random.Random(seed)
    rig = Rig(img, syms, trace=False)
    problems, accepted, played, kinds = [], 0, 0, {}
    opl2 = 0
    for k in range(cases):
        name, base = bases[rng.randrange(len(bases))]
        # WHICH CHIP. A 2.1 base stays on the OPL3, because verb 4 step 5
        # refuses it outright on an OPL2 (D1) and every such mutant would be
        # a RADE_NEEDOPL3 row rather than a load. A 1.0 base draws its chip,
        # so the half of the engine an OPL2 machine actually runs - rd_set's
        # 1xxh filter, the 1.0 register choices, the OPL2 stop sequence - is
        # compared against radsim too, and not only by RV1.RAD in radopl2.
        opl3 = True if base[0x10] == 0x21 else bool(rng.randrange(2))
        opl2 += 0 if opl3 else 1
        data, kind = mutate(rng, base)
        early = resident_steps(data, opl3=opl3)
        if early is not None:                  # the resident's own refusal:
            want = radsim.check(data, opl3=opl3)   # radsim must agree, and the
            if want != early:                  # overlay never sees the file
                problems.append((k, name, kind,
                                 "steps 2-5 say %r, radsim %r" % (early, want)))
            continue
        bad, acc, n = one(rig, data, frames, opl3=opl3)
        kinds[kind] = kinds.get(kind, 0) + 1
        accepted += 1 if acc else 0
        played += n
        if bad:
            problems.append((k, name, kind, bad))
    return problems, accepted, cases - accepted, kinds, played, opl2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cases", type=int, default=2500)
    ap.add_argument("--seed", type=int, default=20260917)
    ap.add_argument("--frames", type=int, default=90)
    a = ap.parse_args()
    if not shutil.which("nasm"):
        print("t_radfuzz: SKIP - no nasm")
        sys.exit(0)
    bases = [(n, fn()) for n, fn in radsim.FIXTURES]
    tmp = tempfile.mkdtemp(prefix="radfuzz-")
    try:
        img, syms = t_radrace.build(tmp, defines=("RADLOG",))
        probs, acc, ref, kinds, played, opl2 = sweep(img, syms, a.cases,
                                                     a.seed, a.frames, bases)
        print("  %d mutants of %d fixtures, seed %d: %d accepted and played "
              "(%d frames compared, %d asked of each), %d refused, %d offered "
              "to an OPL2"
              % (a.cases, len(bases), a.seed, acc, played, a.frames, ref,
                 opl2))
        print("  by mutation: %s"
              % ", ".join("%s %d" % kv for kv in sorted(kinds.items())))
        check(acc >= a.cases // 20,
              "the sweep reached the ENGINES: at least a twentieth of the "
              "mutants were accepted by both and played",
              got=acc, want=">= %d" % (a.cases // 20))
        check(ref >= a.cases // 20,
              "...and the VALIDATOR: at least a twentieth were refused",
              got=ref, want=">= %d" % (a.cases // 20))
        check(played >= acc * a.frames * 3 // 4,
              "...and the accepted mutants PLAYED rather than halting at once: "
              "three quarters of the frames asked for were compared",
              got=played, want=">= %d" % (acc * a.frames * 3 // 4))
        check(not probs, "every mutant's verb 4 answer and register stream is "
              "radsim's", got=probs[:3], want=[])
        check(opl2 >= a.cases // 20,
              "...and a twentieth of them went to an OPL2, so the 1.0 "
              "engine's own register choices are compared as well",
              got=opl2, want=">= %d" % (a.cases // 20))
        # the negative control: the validator's end-of-file bound made toothless
        # - `rv_need` returns instead of refusing - so every truncated mutant is
        # accepted by a driver radsim refuses RADC_TRUNC
        mtmp = os.path.join(tmp, "mut")
        os.mkdir(mtmp)
        applied = []

        def blunt(d):
            src = os.path.join(d, "radval.inc")
            s = open(src).read()
            hit = s.replace("    pop ax\n    ja .trunc\n    ret\n.trunc:",
                            "    pop ax\n    ret\n.trunc:", 1)
            applied.append(hit != s)
            open(src, "w").write(hit)

        mimg, msyms = t_radrace.build(mtmp, defines=("RADLOG",), patch=blunt)
        check(applied and applied[0],
              "negative control: rv_need's bound found to take out")
        if applied and applied[0]:
            mprobs = sweep(mimg, msyms, min(a.cases, 400), a.seed, a.frames,
                           bases)[0]
            check(mprobs, "negative control: a validator missing V1's bound "
                  "fails the sweep", got=len(mprobs), want=">0")
            print("  control: %d problems, e.g. %r" % (len(mprobs), mprobs[:1]))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    done("t_radfuzz")


if __name__ == "__main__":
    main()
