#!/usr/bin/env python3
"""Two source rules NASM cannot refuse and a reader does not see.

    python3 tests/unit/t_asmrules.py

1. UNREACHABLE CODE AFTER AN UNCONDITIONAL TRANSFER.

CLAUDE.md records what this costs, in the tracker's sequential-boundary work
(SPEC.md 45.13.5): *"It shipped once as DEAD CODE - two `jmp short`s in a row,
the old `jmp short .out` above the new `jmp short .carry` - which assembles
under `-w+error`, boots, and puts the field's original complaint back in the
field's original words."*  That is the whole failure mode.  An instruction
after an unconditional `jmp`/`ret`/`iret` with no label between them can never
execute, so the assembler is content, the machine boots, and the feature you
just wrote is simply not there.  It is the residue of an edit that inserted a
new path above an old one, which is exactly what a merge does.

Its first run on this tree found one, in `drivers/hdd/tool.inc` - a duplicated
`jmp short .out`, benign because both went to the same place, and two bytes of
a driver that could never run.

`jmp short $+2` IS NOT A TRANSFER AWAY and must not be reported: it jumps to
the very next instruction and is the 8086 I/O settling idiom (it flushes the
prefetch queue), used nine times in `clock.inc`, `mouse.inc` and
`lplink.inc`.  Anything whose target is `$`-relative is skipped.

WHAT IT CANNOT SEE, by construction and worth stating so nobody trusts it too
far: a jump table, a computed `jmp bx`, and a label that is referenced only
through a macro.  It is a lint, not a proof - so it only ever reports a line
that is unreachable by the plainest possible reading.

2. A PROLOGUE RESTORED IN THE WRONG ORDER.

SPEC.md 1's register discipline is an INTERFACE: a public routine gives back
every register but its documented outputs, and a caller is entitled to keep a
pointer in one across the call.  `push a / push b ... pop a / pop b` honours
the stack DEPTH and breaks that interface - the routine returns with the pair
swapped.  Nothing catches it: NASM is content, the stack is balanced, nothing
faults, and the routine returns cleanly into a caller whose registers now mean
something else.

Its first run on this tree found FOUR, all found by hand first and each one
its own shape of bad:

  ark_blit_pu     SPEC.md 44.10.6.1 - a falling Arkanoid capsule froze in
                  mid-air, because the routine used SI after restoring it
  mem_shed_one    SPEC.md 50.6.4.1 - the kernel's cache eviction handed the
                  shed-and-retry a DMA constraint nobody asked for, on the one
                  path that only runs when memory is already short
  fd_cfg_getstr   SPEC.md 77.12.3 - the FTP server's config walk left the
                  buffer at the first string key, so every setting after it in
                  the file was silently ignored
  cy_walk_one     latent: no caller reads either register across the call
                  today, so this one cost nothing and was waiting

WHAT THIS CANNOT SEE either: a routine with more than one prologue, a `pop`
run that is not the last thing before `ret`, and a deliberate swap (there are
none here, and one would need a comment saying so).  It compares only a
maximal leading run of `push <reg>` against a maximal `pop <reg>` run ending
at a `ret` OF THE SAME LENGTH, which is why the value-push idiom - a `push ax`
inside the prologue paired with a `pop ax` in the body, eleven of them here -
falls out untested rather than reported.  A lint, again, not a proof.

It does allow a FLAG-ONLY instruction between the last `pop` and the `ret`
(`GAPOK` below).  `clc` / `stc` before `ret` is this tree's return convention,
and abandoning the run on it threw away 719 ret sites, `cy_walk_one` among
them.  Measured over these 184 sources: 3,721 of 4,892 sites with a push
prologue were evaluated before (76%), 4,440 of 4,895 are now (91%) - and the
check reports the same number of defects on this tree either way: zero.  THE SAME-LENGTH RULE ABOVE IS NOT
LOOSENED, and should not be: relaxing that axis crudely produces 277 reports
and carefully exactly five, and all five are false positives (`tm_hsnap`,
`dsk_put_ico_body`, `mx_frame`, `hd_bios_geom`, `hd_fmt_boot`).

3. THE 8086 CONSTRAINT IS REACHABLE FROM EVERY ROOT.

SPEC.md 1's first hard rule is that this is an 8086 target, and the mechanism
is one `cpu 8086` directive - after which NASM refuses `pusha`, `movzx`, a
32-bit register and `shl reg, imm`.  Without it those assemble silently and
fault on the machine the whole project is calibrated against.  The kernel
declares it; a package gets it from `OS88_HEADER` in `apps/os88api.inc` and a
driver from `drivers/os88drv.inc`.

`boot/boot.asm` had NO 8086 constraint at all until this check was written -
512 bytes that run on a 5150 before anything has probed anything.  Adding it
changed `build/boot.bin` by **0 bytes**, which is what says the sector was
already clean and that what was missing was the refusal, not a fix.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402

# The 8086 mnemonic set. A whitelist rather than "anything that looks like an
# instruction", because the noise this has to reject is `equ`, `db`, `section`
# and the tree's UPPERCASE macro invocations - all of which are perfectly
# legal after a `ret`.
MNEMONIC = set("""
aaa aad aam aas adc add and call cbw clc cld cli cmc cmp cmps cmpsb cmpsw cwd
daa das dec div hlt idiv imul in inc int into iret ja jae jb jbe jc jcxz je jg
jge jl jle jmp jna jnae jnb jnbe jnc jne jng jnge jnl jnle jno jnp jns jnz jo
jp jpe jpo js jz lahf lds lea les lock lods lodsb lodsw loop loope loopne
loopnz loopz mov movs movsb movsw mul neg nop not or out pop popf push pushf
rcl rcr rep repe repne repnz repz ret retf retn rol ror sahf sal sar sbb scas
scasb scasw shl shr stc std sti stos stosb stosw sub test wait xchg xlat xlatb
xor
""".split())

UNCOND = re.compile(r"^\s*(jmp|ret|retf|iret)\b(.*)$", re.I)
LABEL = re.compile(r"^\s*([.A-Za-z_][\w.$@]*:|%)")
SELFREL = re.compile(r"\$")            # `jmp short $+2` - the I/O settle idiom

SDK_WITH_CPU = ("os88api.inc", "os88drv.inc")


def sources():
    for sub in ("kernel", "boot", "apps", "drivers"):
        for dirpath, _, files in os.walk(os.path.join(ROOT, sub)):
            for f in sorted(files):
                if f.endswith((".asm", ".inc")):
                    yield os.path.join(dirpath, f)


def dead_code(path):
    """[(line, after, dead)] - instructions that cannot be reached."""
    out = []
    with open(path, errors="replace") as f:
        lines = f.read().split("\n")
    prev = None
    for i, raw in enumerate(lines):
        s = raw.split(";")[0].rstrip()
        if not s.strip():
            continue
        if LABEL.match(s):
            prev = None                    # a label makes the next line reachable
            continue
        word = s.strip().split()[0].lower()
        # A dead `nop` is PADDING and can never be a lost feature: every FAT
        # boot sector in this tree opens `jmp short entry` / `nop` so that the
        # BPB starts at offset +3 (SPEC.md 18.2 rule 2 tests the first byte),
        # and that `nop` is unreachable by design and mandatory.
        if prev is not None and word in MNEMONIC and word != "nop":
            out.append((i + 1, lines[prev].strip(), s.strip()))
        m = UNCOND.match(s)
        prev = i if (m and not SELFREL.search(m.group(2))) else None
    return out


PUSH = re.compile(r"^\s+push\s+([a-z]{2})\s*(;.*)?$", re.I)
POP = re.compile(r"^\s+pop\s+([a-z]{2})\s*(;.*)?$", re.I)
TOPLBL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")
RET = re.compile(r"^\s+ret(f)?\s*(;.*)?$", re.I)

# ...and the instructions a `pop` run may be INTERRUPTED by without the run
# being abandoned.  `clc` / `stc` before `ret` is this tree's return
# convention, so a line clearing `poprun` on any instruction at all threw away
# 719 of the 4,892 ret sites that have a push prologue - 15% of them, and one
# of the four defects in the header (`cy_walk_one`, apps/cyclone/cyclone.asm)
# sits behind exactly such a `clc`.  Every mnemonic here writes only FLAGS or a
# direction: none touches a general-purpose or a segment register, so none can
# reorder a restore and none can manufacture a report.  Anything else still
# ends the run, because it could have moved a register the pops are about to
# overwrite.  Coverage with this list: 91%, against 76% without it.
GAPOK = re.compile(r"^\s+(clc|stc|cmc|cld|std|sti|cli|nop)\s*(;.*)?$", re.I)


def crossed_pops(path):
    """[(line, routine, prologue, epilogue)] - a restore in the wrong order."""
    out = []
    with open(path, errors="replace") as f:
        lines = f.read().split("\n")
    routine, pro, inpro = None, [], False
    poprun, start = [], 0
    for n, raw in enumerate(lines, 1):
        if TOPLBL.match(raw):
            routine, pro, inpro, poprun = TOPLBL.match(raw).group(1), [], True, []
            continue
        m = PUSH.match(raw)
        if m:
            if inpro:
                pro.append(m.group(1).lower())
            poprun = []
            continue
        m = POP.match(raw)
        if m:
            inpro = False
            if not poprun:
                start = n
            poprun.append(m.group(1).lower())
            continue
        if RET.match(raw):
            # SAME LENGTH ONLY - see the header. A shorter pop run means the
            # prologue ends in a value push, which is a different idiom.
            if (len(poprun) == len(pro) and pro
                    and sorted(poprun) == sorted(pro) and poprun != pro[::-1]):
                out.append((start, routine, list(pro), list(poprun)))
            poprun = []
            continue
        if GAPOK.match(raw):
            continue                       # a flag-only instruction: see GAPOK
        if raw.strip() and not raw.strip().startswith(";"):
            inpro, poprun = False, []
    return out


def main():
    files = list(sources())
    for path in files:
        rel = os.path.relpath(path, ROOT)
        for line, after, dead in dead_code(path):
            check(False, "%s:%d is unreachable" % (rel, line),
                  "nothing can transfer here - the line above is an unconditional "
                  "jump and there is no label between them. An edit that inserted a "
                  "new path above an old one leaves exactly this (SPEC.md 45.13.5)",
                  got=dead, want="a label, or the line deleted")
        for line, routine, pro, popped in crossed_pops(path):
            check(False, "%s:%d - %s restores in the wrong order"
                  % (rel, line, routine),
                  "SPEC.md 1: the stack depth is right, so nothing faults and "
                  "nothing asserts - the routine simply returns with a pair of "
                  "registers swapped, into a caller entitled to keep a pointer "
                  "in one of them (SPEC.md 44.10.6.1, 50.6.4.1, 77.12.3)",
                  got="push %s / pop %s" % (", ".join(pro), ", ".join(popped)),
                  want="pop %s" % ", ".join(reversed(pro)))

    # Every root NASM is pointed at must be able to see a `cpu 8086`.
    # A "root" is an .asm that nothing %includes. Reading the Makefile for
    # the name instead was tried and is UNSOUND: `runcpm.asm` and
    # `ccsmoke.asm` are built through pattern rules that never spell the file
    # out, so both dropped silently out of the list - and a root that is not
    # detected is a root that is not checked, which is the one failure a gate
    # may not have.
    included = set()
    for q in files:
        with open(q, errors="replace") as f:
            for i in re.findall(r'^\s*%include\s+"([^"]+)"', f.read(), re.M):
                included.add(os.path.basename(i))
    roots = [p for p in files
             if p.endswith(".asm") and os.path.basename(p) not in included]
    checked, gated = 0, 0
    for path in sorted(set(roots)):
        rel = os.path.relpath(path, ROOT)
        with open(path, errors="replace") as f:
            src = f.read()
        # A C package's body is `<name>.gen.asm`, written by SmallerC, which
        # targets an 80386 even in 16-bit mode. `tools/cc8086.py` is that
        # output's 8086 gate - it LOWERS the seven 386-isms SmallerC emits and
        # refuses the rest - so the constraint lives there and a bare
        # `cpu 8086` here would refuse the compiler's own output instead.
        if re.search(r'^\s*%include\s+"[\w.]+\.gen\.asm"', src, re.M):
            gated += 1
            continue
        if re.search(r"^\s*cpu\s+8086\b", src, re.M):
            checked += 1
            continue
        inc = re.findall(r'^\s*%include\s+"([^"]+)"', src, re.M)
        via = [i for i in inc if os.path.basename(i) in SDK_WITH_CPU]
        if check(bool(via), "%s can reach a `cpu 8086`" % rel,
                 "SPEC.md 1: without the directive NASM accepts pusha, movzx, a "
                 "32-bit register and `shl reg, imm` - all of which fault on the "
                 "4.77MHz 8088 this project targets, having assembled in silence",
                 got="no `cpu 8086` and no SDK include",
                 want="cpu 8086, or %include an SDK"):
            checked += 1

    print("t_asmrules: %d sources scanned for dead code and crossed pops, "
          "%d roots 8086-constrained (%d C roots gated by tools/cc8086.py "
          "instead)" % (len(files), checked, gated))
    done("t_asmrules")


if __name__ == "__main__":
    main()
