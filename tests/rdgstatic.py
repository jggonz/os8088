#!/usr/bin/env python3
"""RealDMG's Static recompiler (REALDMG-SPEC 16.8), bare, in QEMU.

    python3 tests/rdgstatic.py [-k NAME]

Each ROM runs TWICE through tests/rdgcore/rdghost.asm with b_jit = 2:
- a first pass with the recompiler recording its profile;
- then the game again from its start, the profile preloaded and the block
  table fixed.

A row passes when BOTH passes report "Passed", and the DONE line says that
nothing was translated after the switch ("after 0"). Static is the dynamic
recompiler's own translator run early, so these are the same correctness
ROMs as tests/gbcpu.py, now with the blocks made before the game ran, and
the table never changed while it ran.

Then the two rows the claim "a fixed table translates nothing" rests on,
both on the first ROM with recording broken in the harness (`rt_prfadd`'s
`je` made a `jmp`), so the second pass preloads NOTHING and every hot loop
is a miss:
- the table fixed: "after" must still be 0 - Static with an empty profile
  is the interpreter;
- THE NEGATIVE CONTROL: the table never fixed as well - rt_preload's `mov
  byte [S_JITFIX],1` made 0, which disarms both of its guards, tr_new2's
  and rdg_jit's (taking out either alone changes nothing; each covers the
  other). Misses then count, the translator runs in the second pass, and
  "after" must NOT be 0. A gate that cannot go red tests nothing
  (docs/WRITING-TESTS.md 1). With a full profile it could not go red: every
  hot block is preloaded.

Then the cache's SECOND SEGMENT (REALDMG-SPEC 16.2.1), with both segments
squeezed to 1KB of code so the test ROMs, which fit one 64KB segment many
times over, fill both:
- every ROM again, Static, both passes "Passed" and "after 0";
- cpu_instrs translated as it runs: it must pass having moved into the
  second segment twice and so flushed both (the DONE line's "seg2", and
  "flushes" past rt_init's own), which is where code crosses between them;
- THE NEGATIVE CONTROL: the translator's check that a target is in the
  segment being written (rt_exit's `cmp dx,[S_JITSEG] / jne`) made two NOPs,
  so an exit is written as a near jump into the other segment's offsets.
  cpu_instrs must then NOT pass.

Then the CORE'S HOLES (REALDMG-SPEC 16.2.2), the tier between the first
segment and the second, which every leg above already passes through when
squeezed:
- cpu_instrs translated as it runs with ONE segment, squeezed, so that the
  code moves on into the core's first RT_SQHOLES half-slots and flushes
  from there: it must pass, having moved twice and flushed;
- THE NEGATIVE CONTROL: the hole table read from 8 bytes EARLIER in each
  half-slot (its bytes in the harness lowered by 8), so translated code is
  written over the last 8 bytes of op_00-op_07's handlers. cpu_instrs must
  then NOT pass - which says the code written into the core is what ran.

Every leg runs with the block table at its 64KB (REALDMG-SPEC 16.3); the
last runs cpu_instrs again with it at 16KB, the package's fallback, whose
mask rt_init writes into the core's lookups.
"""
import struct
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests", "rdgcore"))
import rdgimg  # noqa: E402
import rdgqemu  # noqa: E402

# (name, ROM as getgbtest names it, frames a pass)
ROMS = [
    ("cpu_instrs (all, MBC1)", "cpu_instrs/cpu_instrs", 4200),
    ("instr_timing", "instr_timing", 600),
    ("mem_timing", "mem_timing/mem_timing", 1200),
]
RMW_ROM = "gbrmw"                       # ours: tools/gbprof/gbrmw.asm
# rt_preload's mov byte [S_JITFIX],1 - S_JITFIX is RDG_STATE + 0F6h = 40F6h
SETFIX = bytes.fromhex("C606F64001")
# rt_prfadd's first test: cmp word [S_PRFSEG],0 / je - S_PRFSEG is 40F8h
PRFCHK = bytes.fromhex("833EF84000") + b"\x74"

def rom_path(name):
    return subprocess.check_output([sys.executable, os.path.join(ROOT, "tools/getgbtest.py"),
                                    "--path", name], text=True).strip()


# rt_exit's same-segment check: cmp dx,[S_JITSEG] / jne - S_JITSEG is 40CCh
SEGCHK = bytes.fromhex("3B16CC4075")


def run(rom, frames, core, host, jit="static", seg2=False, small=False, squeeze=None):
    img = os.path.join(ROOT, "build", "realdmg", "rdgstatic.img")
    rdgimg.make_image(rom, img, frames=frames, core=core, host=host, jit=jit,
                     seg2=seg2, squeeze=seg2 if squeeze is None else squeeze, small=small)
    text = rdgqemu.run(img, ["RDGHOST DONE", "RDGHOST BADOP"], timeout=400, quiet=True)
    if jit != "static":
        m = re.search(r"flushes (\d+) seg2 (\d+)", text)
        r = re.search(r"recycled (\d+)", text)
        return text.count("Passed"), (int(m.group(1)), int(m.group(2)),
                                      int(r.group(1)) if r else 0) if m else None
    m = re.search(r"static (\d+) after (\d+)", text)
    return text.count("Passed"), (int(m.group(1)), int(m.group(2))) if m else None


def norec(host):
    """The harness with rt_prfadd recording nothing: its je made a jmp."""
    i = host.find(PRFCHK)
    if i < 0 or host.find(PRFCHK, i + 1) >= 0:
        sys.exit("rdgstatic: rt_prfadd's test is not in the harness exactly once")
    host = bytearray(host)
    host[i + len(PRFCHK) - 1] = 0xEB
    return bytes(host)


def crossseg(host):
    """The harness with rt_exit chaining into the other cache segment."""
    i = host.find(SEGCHK)
    if i < 0 or host.find(SEGCHK, i + 1) >= 0:
        sys.exit("rdgstatic: rt_exit's segment check is not in the harness exactly once")
    host = bytearray(host)
    host[i + len(SEGCHK) - 1:i + len(SEGCHK) + 1] = b"\x90\x90"
    return bytes(host)


def holeshift(host, out):
    """The harness with the core's hole table (rdgtmpl.py's 512 bytes, at the
    templates' RDGT_CFREE) reading 8 bytes into each half-slot's code."""
    tmpl = open(os.path.join(rdgimg.private(out), "rdgtmpl.bin"), "rb").read()
    sys.path.insert(0, os.path.join(ROOT, "apps", "realdmg"))
    import rdgtmpl
    off, n = struct.unpack_from("<HH", tmpl, rdgtmpl.HEAD - 16)
    tab = tmpl[off:off + n]
    i = host.find(tmpl[:64])
    if n != 512 or i < 0 or host[i + off:i + off + n] != tab:
        sys.exit("rdgstatic: the core's hole table is not where rdgtmpl.py put it")
    host = bytearray(host)
    for k in range(n):
        if tab[k] != 0xFF:
            host[i + off + k] = max(tab[k] - 8, 0)
    return bytes(host)


# tr_lpatch's age test (REALDMG-SPEC 16.2.3): cmp dx,[cs:0FFF0h] / jae .ok
AGECHK = bytes.fromhex("2E3B16F0FF73")


def nolog(core):
    """The core with tr_lpatch chaining into an older tier WITHOUT logging
    it - a link the recycling of that tier then leaves pointing into its
    new code."""
    i = core.find(AGECHK)
    if i < 0 or core.find(AGECHK, i + 1) >= 0:
        sys.exit("rdgstatic: tr_lpatch's age test is not in the core exactly once")
    core = bytearray(core)
    core[i + len(AGECHK) - 1] = 0xEB
    return bytes(core)


def unfix(host):
    """The harness with rt_preload never fixing the table."""
    i = host.find(SETFIX)
    if i < 0 or host.find(SETFIX, i + 1) >= 0:
        sys.exit("rdgstatic: rt_preload's store is not in the harness exactly once")
    host = bytearray(host)
    host[i + len(SETFIX) - 1] = 0
    return bytes(host)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("-k", help="only the ROMs whose name contains this")
    a = ap.parse_args()
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools/getgbtest.py")])
    import gbours
    out = os.path.join(ROOT, "build", "realdmg")
    core, host = rdgimg.build_core(False, out), rdgimg.build_host(out)
    rows = [(label, rom_path(name), frames) for label, name, frames in ROMS]
    rows.append(("rmw (HL), RAM and I/O", gbours.build(RMW_ROM), 2000))
    bad = 0
    for label, rom, frames in rows:
        if a.k and a.k not in label:
            continue
        n, st = run(rom, frames, core, host)
        ok = n == 2 and st is not None and st[1] == 0
        print("%-4s %-24s passed %d of 2, %s" % (
            "ok" if ok else "FAIL", label, n,
            "%d blocks preloaded, %d translated after" % st if st else "no DONE line"))
        bad += not ok
    if not a.k:
        rom, frames = rows[0][1], rows[0][2]
        nohost = norec(host)
        n, st = run(rom, frames, core, nohost)
        ok = n == 2 and st is not None and st[0] == 0 and st[1] == 0
        print("%-4s %-24s passed %d of 2, %s" % (
            "ok" if ok else "FAIL", "empty profile, fixed", n,
            "%d blocks preloaded, %d translated after" % st if st else "no DONE line"))
        bad += not ok
        n, st = run(rom, frames, core, unfix(nohost))
        red = st is None or st[1] > 0
        print("%-4s %-24s %s" % ("ok" if red else "FAIL", "control: table not fixed",
                                 "%d translated after the switch, as it must" % st[1]
                                 if st and red else "nothing translated - the gate cannot go red"))
        bad += not red
    for label, rom, frames in rows:                 # two segments, squeezed
        if a.k and a.k not in label:
            continue
        n, st = run(rom, frames, core, host, seg2=True)
        ok = n == 2 and st is not None and st[1] == 0
        print("%-4s %-24s passed %d of 2, %s" % (
            "ok" if ok else "FAIL", "seg2: " + label[:18], n,
            "%d blocks preloaded, %d translated after" % st if st else "no DONE line"))
        bad += not ok
    if not a.k:
        rom, frames = rows[0][1], rows[0][2]
        n, st = run(rom, frames, core, host, jit=True, seg2=True)
        ok = n == 1 and st is not None and st[0] >= 2 and st[1] >= 2
        print("%-4s %-24s passed %d of 1, %s" % (
            "ok" if ok else "FAIL", "seg2: translated as run", n,
            "%d flushes, %d moves into the second" % st[:2] if st else "no DONE line"))
        bad += not ok
        n, st = run(rom, frames, core, crossseg(host), jit=True, seg2=True)
        red = n == 0
        print("%-4s %-24s %s" % ("ok" if red else "FAIL", "control: cross-segment",
                                 "fails, as it must" if red else
                                 "passed - the gate cannot see a cross-segment jump"))
        bad += not red
        n, st = run(rom, frames, core, host, jit=True, squeeze=True)   # the core's holes
        ok = n == 1 and st is not None and st[0] >= 2 and st[1] >= 2 and st[2] >= 2
        print("%-4s %-24s passed %d of 1, %s" % (
            "ok" if ok else "FAIL", "core tier: as run", n,
            "%d flushes (%d of them one tier recycled), %d moves into the core"
            % (st[0], st[2], st[1]) if st else "no DONE line"))
        bad += not ok
        n, st = run(rom, frames, nolog(core), host, jit=True, squeeze=True)
        red = n == 0
        print("%-4s %-24s %s" % ("ok" if red else "FAIL", "control: links not logged",
                                 "fails, as it must" if red else
                                 "passed - the gate cannot see a stale chain"))
        bad += not red
        n, st = run(rom, frames, core, holeshift(host, out), jit=True, squeeze=True)
        red = n == 0
        print("%-4s %-24s %s" % ("ok" if red else "FAIL", "control: holes over code",
                                 "fails, as it must" if red else
                                 "passed - the gate cannot see the core's holes"))
        bad += not red
        n, st = run(rom, frames, core, host, small=True)   # the 16KB table
        ok = n == 2 and st is not None and st[1] == 0
        print("%-4s %-24s passed %d of 2, %s" % (
            "ok" if ok else "FAIL", "16KB block table", n,
            "%d blocks preloaded, %d translated after" % st if st else "no DONE line"))
        bad += not ok
    print("rdgstatic: %s" % ("PASS" if not bad else "%d FAILED" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
