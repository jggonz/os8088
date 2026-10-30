#!/usr/bin/env python3
"""RealDMG's core against Blargg's and Mooneye's test ROMs, bare, in QEMU.

    python3 tests/gbcpu.py [-k NAME]

docs/plans/REALDMG-PLAN.md wave 1 and wave 2's `gbcpu` row. Each ROM is
booted on a bare machine through tests/rdgcore/rdghost.asm - no os8088 under
the core - and the row reads what the ROM prints on its link port, which the
harness copies to COM1. A ROM passes when it says "Passed". halt_bug
reports only on screen, so its verdict is read where Blargg's ROMs also
leave it, as text at A000 (tests/rdgcore/rdgshot.py).

The ROMs are fetched by tools/getgbtest.py at pinned SHA-256s and never
committed. QEMU counts work exactly and times nothing (docs/TESTING.md), and
this asks it for nothing else: these are CORRECTNESS gates, and the cycle
counts the timing ROMs check are the core's own.

THE NEGATIVE CONTROL, last: the same harness with DAA made a no-op - the
handler's first bytes replaced by a bare dispatch - must make 01-special FAIL.
A gate that cannot go red is a row that tests nothing (docs/WRITING-TESTS.md 1).
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests", "rdgcore"))
import rdgimg  # noqa: E402
import rdgqemu  # noqa: E402
import rdgshot  # noqa: E402

# (name, ROM as getgbtest names it, frames to allow)
ROMS = [
    ("01-special", "individual/01-", 1200),
    ("02-interrupts", "individual/02-", 600),
    ("03-op sp,hl", "individual/03-", 1200),
    ("04-op r,imm", "individual/04-", 1200),
    ("05-op rp", "individual/05-", 1200),
    ("06-ld r,r", "individual/06-", 600),
    ("07-jr,jp,call,ret,rst", "individual/07-", 600),
    ("08-misc instrs", "individual/08-", 600),
    ("09-op r,r", "individual/09-", 2400),
    ("10-bit ops", "individual/10-", 2400),
    ("11-op a,(hl)", "individual/11-", 3000),
    ("cpu_instrs (all, MBC1)", "cpu_instrs/cpu_instrs", 4200),
    ("instr_timing", "instr_timing", 600),
    ("mem_timing", "mem_timing/mem_timing", 1200),
    ("halt_bug", "halt_bug", 1200, "screen"),
]
# Mooneye's acceptance tests the core passes (REALDMG-SPEC section 12.3 says
# which of the other DMG rows it does not, and why). A Mooneye test reports
# in the registers: B,C,D,E,H,L = 3,5,8,13,21,34.
MOONEYE = [
    "bits/mem_oam", "bits/reg_f", "bits/unused_hwio-GS", "boot_div-dmgABCmgb",
    "boot_hwio-dmgABCmgb", "boot_regs-dmgABC", "div_timing", "halt_ime0_ei",
    "halt_ime0_nointr_timing", "halt_ime1_timing", "if_ie_registers",
    "instr/daa", "intr_timing", "oam_dma/basic", "oam_dma/reg_read",
    "ppu/intr_2_mode0_timing", "ppu/intr_2_mode3_timing",
    "timer/div_write", "timer/tim00", "timer/tim00_div_trigger", "timer/tim01",
    "timer/tim01_div_trigger", "timer/tim10", "timer/tim10_div_trigger",
    "timer/tim11", "timer/tim11_div_trigger",
]
DAA_NOP = "2700:8A3446FFE2"        # mov dh,[si] / inc si / jmp dx
# Our own: tools/gbprof/gbrmw.asm, the read-modify-writes of (HL) the
# recompiler does itself in RAM and hands to the interpreter elsewhere
# (REALDMG-SPEC 16.6). Its control breaks the INTERPRETER's DEC (HL) alone,
# which the translated WRAM side never runs: the two sides must part.
RMW_ROM = "gbrmw"
DECHL_NOP = "3500:8A3446FFE2"
# tools/gbprof/gbflags.py: the recompiler's dead flags (REALDMG-SPEC 16.1.2),
# whose sum must be the interpreter's. Its control does not see that an
# instruction reads a flag (ADC's carry, RLA's, DAA's) before it writes it.
FLAGS_ROM = "gbflags"
# tools/gbprof/gblazy.py: the countdown loops whose DEC r is left without F
# (REALDMG-SPEC 16.1.3), a timer interrupt landing at their JR. Its control
# takes that event with F stale.
LAZY_ROM = "gblazy"
# tools/gbprof/gbseam.asm: code run from bank 0 across 3FFF into the bank
# (REALDMG-SPEC 3.1), an instruction across it, one on at 4000 and a JR at
# 3FFF. Its control assembles the harness without the trap (RDG_NOSEAM).
SEAM_ROMS = ("gbseam", "gbseam2", "gbseam5")
# ...and gbseam3 and gbseam4, a JR across it from bank 0 into the bank and
# back: a JR chooses no DS, and only the translated machine's way through
# tr_int (or tr_ejf) puts it right, so they are held TRANSLATED, and the
# plain interpreter is their control - it must fail them (REALDMG-SPEC 3.1)
SEAM_JRS = ("gbseam3", "gbseam4")
# tools/gbprof/gbcpc.py: copies through (C), RAM to HRAM and back, taken in
# one step (cpc_ff, REALDMG-SPEC 16.6), a timer interrupt landing in them.
# Its control takes the copy the wrong way round (RT_CPCBUG).
CPC_ROM = "gbcpc"
# tools/gbprof/gbpop.asm: POPs out of a ROM bank, SP in 4000-7FFF, as
# Pokemon's VBlank copy reads tiles (pop_slow, REALDMG-SPEC 5). Its control
# takes POP BC's JB out (C104h), so BC pops from the view's rdgsys half again.
POP_ROM = "gbpop"
POPBC_FAST = "C104:9090"


def rom_path(name):
    """`tools/getgbtest.py --path NAME`, in process (a Python start a ROM)"""
    import getgbtest
    hits = [f for f in list(getgbtest.FILES) + list(getgbtest.ZIP_FILES)
            if name in f]
    if len(hits) != 1:
        sys.exit("gbcpu: %r matches %d files" % (name, len(hits)))
    return os.path.join(getgbtest.DEST, hits[0])


def run_screen(rom, frames, core, host):
    """A ROM that reports on screen and in Blargg's memory protocol (A000:
    status, A001: DE B0 61, A004: the text) rather than on the link port."""
    img = os.path.join(ROOT, "build", "realdmg", "gbcpu.img")
    rdgimg.make_image(rom, img, mode="idle", frames=frames, core=core, host=host)
    _, blk = rdgshot.capture(img, 180)
    v = rdgshot.blargg(blk)
    if not v:
        return False, "no verdict at A000"
    return v[0] == 0 and "Passed" in v[1], v[1].strip().splitlines()[-1]


def run_mooneye(rom, core, host):
    img = os.path.join(ROOT, "build", "realdmg", "gbcpu.img")
    rdgimg.make_image(rom, img, mode="idle", frames=300, core=core, host=host)
    _, blk = rdgshot.capture(img, 180)
    v = rdgshot.mooneye(blk)
    return v is True, {True: "3 5 8 13 21 34", False: "42 42 42 42 42 42",
                       None: "never finished"}[v]


def run(label, rom, frames, core, host, jit=True):
    img = os.path.join(ROOT, "build", "realdmg", "gbcpu.img")
    rdgimg.make_image(rom, img, frames=frames, core=core, host=host, jit=jit)
    text = rdgqemu.run(img, ["Passed", "Failed", "RDGHOST DONE", "RDGHOST BADOP"],
                      timeout=180, quiet=True)
    last = [l.strip() for l in text.splitlines() if l.strip()][-1:] or ["(nothing)"]
    return "Passed" in text, last[0]


def run_sum(rom, core, host, jit):
    img = os.path.join(ROOT, "build", "realdmg", "gbcpu.img")
    rdgimg.make_image(rom, img, frames=3000, core=core, host=host, jit=jit)
    text = rdgqemu.run(img, ["Passed", "RDGHOST DONE", "RDGHOST BADOP"], timeout=180, quiet=True)
    s = [l.strip() for l in text.splitlines() if l.strip().startswith("Sum ")]
    return s[0] if s and "Passed" in text else None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("-k", help="only the ROMs whose name contains this")
    a = ap.parse_args()
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools/getgbtest.py")])
    out = os.path.join(ROOT, "build", "realdmg")
    core, host = rdgimg.build_core(False, out), rdgimg.build_host(out)
    bad = 0
    for label, name, frames, *how in ROMS:
        if a.k and a.k not in label:
            continue
        if how:
            ok, last = run_screen(rom_path(name), frames, core, host)
        else:
            ok, last = run(label, rom_path(name), frames, core, host)
        print("%-4s %-24s %s" % ("ok" if ok else "FAIL", label, last))
        bad += not ok
    if not a.k or a.k in "rmw (HL)":
        import gbours
        rom = gbours.build(RMW_ROM)
        ok, last = run("rmw (HL)", rom, 2000, core, host)
        print("%-4s %-24s %s" % ("ok" if ok else "FAIL", "rmw (HL), RAM and I/O", last))
        bad += not ok
        ok, last = run("control", rom, 2000, rdgimg.patch_core(core, [DECHL_NOP]), host)
        print("%-4s %-24s %s" % ("FAIL" if ok else "ok", "control: DEC (HL) broken",
                                 "passed - the gate cannot go red" if ok else "failed, as it must"))
        bad += ok
    if not a.k or a.k in "pop from ROM":
        import gbours
        rom = gbours.build(POP_ROM)
        for jit in (False, True):
            ok, last = run("pop", rom, 1500, core, host, jit=jit)
            print("%-4s %-24s %s" % ("ok" if ok else "FAIL", "pop from ROM, %s"
                                     % ("translated" if jit else "interpreted"), last))
            bad += not ok
        ok, last = run("control", rom, 1500, rdgimg.patch_core(core, [POPBC_FAST]), host,
                       jit=False)
        print("%-4s %-24s %s" % ("FAIL" if ok else "ok", "control: POP BC from the view",
                                 "passed - the gate cannot go red" if ok else "failed, as it must"))
        bad += ok
    if not a.k or a.k in "dead flags":
        import gbours
        rom = gbours.build(FLAGS_ROM)
        want = run_sum(rom, core, host, False)
        got = run_sum(rom, core, host, True)
        ok = want is not None and got == want
        print("%-4s %-24s %s" % ("ok" if ok else "FAIL", "dead flags",
                                 "%s, interpreted %s" % (got, want)))
        bad += not ok
        broke = run_sum(rom, core, rdgimg.build_host(out, ["RT_DEADBUG"]), True)
        rdgimg.build_host(out)
        ok = broke == want
        print("%-4s %-24s %s" % ("FAIL" if ok else "ok", "control: reads unseen",
                                 "passed - the gate cannot go red" if ok
                                 else "%s, as it must not be" % broke))
        bad += ok
        broke = run_sum(rom, core, rdgimg.build_host(out, ["RT_CBBUG"]), True)
        rdgimg.build_host(out)
        ok = broke == want
        print("%-4s %-24s %s" % ("FAIL" if ok else "ok", "control: CB's C dropped",
                                 "passed - the gate cannot go red" if ok
                                 else "%s, as it must not be" % broke))
        bad += ok
    if not a.k or a.k in "lazy loops":
        import gbours
        rom = gbours.build(LAZY_ROM)
        want = run_sum(rom, core, host, False)
        got = run_sum(rom, core, host, True)
        ok = want is not None and got == want
        print("%-4s %-24s %s" % ("ok" if ok else "FAIL", "lazy loops",
                                 "%s, interpreted %s" % (got, want)))
        bad += not ok
        broke = run_sum(rom, core, rdgimg.build_host(out, ["RT_LAZYBUG"]), True)
        rdgimg.build_host(out)
        ok = broke == want
        print("%-4s %-24s %s" % ("FAIL" if ok else "ok", "control: event F stale",
                                 "passed - the gate cannot go red" if ok
                                 else "%s, as it must not be" % broke))
        bad += ok
    if not a.k or a.k in "copies through (C)":
        import gbours
        rom = gbours.build(CPC_ROM)
        want = run_sum(rom, core, host, False)
        got = run_sum(rom, core, host, True)
        ok = want is not None and got == want
        print("%-4s %-24s %s" % ("ok" if ok else "FAIL", "copies through (C)",
                                 "%s, interpreted %s" % (got, want)))
        bad += not ok
        broke = run_sum(rom, core, rdgimg.build_host(out, ["RT_CPCBUG"]), True)
        rdgimg.build_host(out)
        ok = broke == want
        print("%-4s %-24s %s" % ("FAIL" if ok else "ok", "control: copy reversed",
                                 "passed - the gate cannot go red" if ok
                                 else "%s, as it must not be" % broke))
        bad += ok
    if not a.k or a.k in "bank seam":
        import gbours
        nos = rdgimg.build_host(out, ["RDG_NOSEAM"])
        rdgimg.build_host(out)
        for name in SEAM_ROMS:
            rom = gbours.build(name)
            for jit in (False, True):
                ok, last = run(name, rom, 600, core, host, jit=jit)
                print("%-4s %-24s %s" % ("ok" if ok else "FAIL", "%s, %s" % (
                    name, "translated" if jit else "interpreted"), last))
                bad += not ok
            ok, last = run(name, rom, 600, core, nos, jit=False)
            print("%-4s %-24s %s" % ("FAIL" if ok else "ok", "control: %s no trap" % name,
                                     "passed - the gate cannot go red" if ok
                                     else "failed, as it must"))
            bad += ok
        for name in SEAM_JRS:
            rom = gbours.build(name)
            ok, last = run(name, rom, 600, core, host, jit=True)
            print("%-4s %-24s %s" % ("ok" if ok else "FAIL", "%s, translated" % name, last))
            bad += not ok
            ok, last = run(name, rom, 600, core, host, jit=False)
            print("%-4s %-24s %s" % ("FAIL" if ok else "ok", "control: %s interpreted" % name,
                                     "passed - the gate cannot go red" if ok
                                     else "failed, as it must"))
            bad += ok
    for name in MOONEYE:
        label = "mooneye " + name
        if a.k and a.k not in label:
            continue
        ok, last = run_mooneye(rom_path("acceptance/%s.gb" % name), core, host)
        print("%-4s %-38s %s" % ("ok" if ok else "FAIL", label, last))
        bad += not ok
    if not a.k:
        ok, last = run("control", rom_path("individual/01-"), 1200,
                       rdgimg.patch_core(core, [DAA_NOP]), host, jit=False)
        print("%-4s %-24s %s" % ("FAIL" if ok else "ok", "control: DAA broken",
                                 "passed - the gate cannot go red" if ok else "failed, as it must"))
        bad += ok
    print("gbcpu: %s" % ("PASS" if not bad else "%d FAILED" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
