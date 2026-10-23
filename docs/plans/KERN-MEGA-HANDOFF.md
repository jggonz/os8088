# kern_mega: tracks to run side by side

Written 2026-10-18 for parallel sessions, cut from `protected-mode` at the
commit that adds this file. Each track is one session. **Read Part A first,
then only your track.** The design they all serve is
docs/plans/KERN-MEGA-PLAN.md; section 13 is the memory direction the owner
set today, and section 10.3 is the log of what is built and what the field
machine said.

---

## Part A - what every session needs

### A.1 What kern_mega is, in one paragraph

A 286+ build of os8088 that runs the kernel and every package in 16-bit
protected mode, ring 0. GDT selector N is segment N whenever N is a multiple
of 8 (the identity map), so ordinary real-mode code keeps working; the LDT
holds a CODE alias for every 512-aligned heap image, `(S >> 2) | 4`, with its
data segment `(CS & ~7) << 2` (`mega_cs2ds`). BIOS calls are REFLECTED to real
mode (`mega_svc`): a 386 clears CR0.PE, a 286 is RESET through the BIOS's
shutdown-code path (CMOS 0Fh = 0Ah, back at `mega_r286`). The floppy is native
(`mega_fd13`), and so are the tick, the keyboard, the XMEM pool above 1MB
(`mx_*`) and `.cold`, which runs from 0x125000. Code the kernel does not own
that writes through CS, or loads a code selector into DS, is completed by #GP
fixups (`mega_gpw`, `mega_gpfix`, `mega_null0`) - correct, and slow if it is in
a loop. All of it is `kernel/mega.inc`, assembled only with `-DKERN_MEGA`.

### A.2 Build and the one rule that is not negotiable

```
make deps            # first, on a fresh box
make                 # kern_big + the fast tier (must pass)
make mega            # kern_mega: build/megak/, build/mega.img (1.44MB)
```

**kern_big must stay BYTE-IDENTICAL.** Everything you add goes inside
`%ifdef KERN_MEGA`, or is a macro that expands to today's bytes on every
other build (PUSH_KCODE, KDATAR, MEGA_SEG0 are the pattern). Prove it at ONE
build number, because the About box's number is the commit count and moves
three bytes of `.text` per commit:

```
W=$(mktemp -d); git worktree add -q $W HEAD~0   # or your base commit
(cd $W && OS88_BUILDNUM=1600 make build/kernel.bin)
OS88_BUILDNUM=1600 make build/kernel.bin
cmp $W/build/kernel.bin build/kernel.bin && echo IDENTICAL
git worktree remove --force $W
```

For an apps/ or drivers/ change the same holds for that package's `.o88` /
`.drv`: it ships on kern_big too, so either it is byte-identical there or the
change is one that is right on both kernels and you say so.

Two ratchets will fire on new code and both are right to: `tests/cswrites.txt`
(a write through CS, or a `push cs / pop` pair) and `tests/seg0.txt` (a load
of segment 0). A site that runs only in REAL mode (the reflection's window)
is registered with that reason; anything else is converted.

### A.3 The instrument: Bochs (docs/BOCHS.md is the full manual)

QEMU does not check segment limits or CS writes, so it runs code a real 286
faults on. Bochs checks every access the way the CPU does and reproduced the
field machine's faults to the CS:IP. Install once:

```
apt-get install -y bochs bochsbios vgabios bochs-x xvfb xdotool x11-apps imagemagick
```

`tools/os88bochs.py` runs it (its docstring is the manual):

```
python3 tools/os88bochs.py boot  [--out DIR] [--sb] [--ne2k] [--cfg BITS]
                                 [--c run|blank|keep] [--bootc] [--noa]
                                 [--b IMG --btype 1_2|1_44]
python3 tools/os88bochs.py sweep [--batch N] PKG.o88 ...   # start each, log faults
python3 tools/os88bochs.py dbg [--out DIR] "xp /4bx 0xADDR" ...   # break in, read, continue
python3 tools/os88bochs.py stop
```

`--cfg BITS` writes a SYSTEM.CFG wanting drivers (bit 0 SOUND.DRV, bit 1
HDD.DRV). `--c run` (default) is a C: holding RUN/ with your packages and
MEGARUN; `blank` is zeros for the installer; `keep` reuses OUT/c.img. E9 output
(the kernel's `mega_say`/`mega_puts`) lands in `OUT/e9.out`, and a
`kern_mega FAULT ...` line there is a fault with its stack.

`tools/os88bochsui.py` is the hands: `capture` once, then `goto X Y`, `dbl`,
`down`/`move DX DY`/`up` for a menu, `shot FILE.png`. Guest coordinates on the
640x480 desktop: A: icon (591,45), B: (591,105), C: (591,160), the System menu
(10,5) with Control Panel at +35 below it. A boot takes ~80 s before the
desktop is up; poll `e9.out` for `kern_mega: protected mode` rather than
sleeping blind.

**Four things Bochs cannot tell you**, each of which has already cost a
wrong conclusion:
1. **ONE Bochs per container.** Clicks go to whichever window is on top, and
   two machines read exactly like a guest that froze. `boot` refuses while
   one runs. Parallel work is parallel SESSIONS, not parallel agents in one.
2. **Its CPU is a 386**, so reflection takes the CR0 path. The 286 RESET path
   (`mega_r286`) does not run in Bochs at all, and forcing it does not help:
   Bochs's triple fault resets the whole machine, devices included, where an
   AT resets only the CPU. The 286 path is the field machine's to test.
3. **HDD.DRV takes rung 1 (native IDE)** on a 286+, so a Bochs C: never
   reaches the BIOS. On the field machine C: is a PicoMEM, BIOS-only, and
   every C: call is a reflection.
4. **There is no PicoMEM.** Its Sound Blaster is an option ROM's multiplexer
   on one IRQ line (plan 10.3 items 18-24); a Bochs SB16 is a plain card.

### A.4 The field machine

The owner's Packard Bell Victory 286 (PicoMEM 1.x on the owner's firmware,
`firmware/picomem1/`: emulated SB 2.0, BIOS-only C:; PS/2 keyboard and mouse;
1.44MB A:, a B: drive). The owner tests images you produce; say exactly what
to try and what a photograph should show. The menu-bar diagnostics already
in the kernel (`NMI`, `FD BIOS`, `RST`, `FDC RESET`, `RFL`, `Q`) are read off
photographs; `mega_tell` is how a new one gets on the bar safely. **Since
KERN-MEGA-PLAN 14.65 they are `MEGADIAG=1`'s** (`make megadiag`): a field run
that needs one is given `build/megadiag/mega.img`, and a new one goes inside
`%ifdef MEGA_DIAG`.

### A.5 How to land the work

* Work on the branch your session was given, cut from `protected-mode`.
  Commit with the repo's usual footer; no model names in commits.
* Record what you built and measured as new numbered items in
  KERN-MEGA-PLAN.md 10.3 - the doc gate (`tools/checkdocs.py`) runs in `make`.
* Before you stop: merge `protected-mode` into your branch, `make` and
  `make mega` clean, the byte-identity check, your own Bochs checks, push,
  and tell the owner what is ready and what to test on the Victory. The owner
  merges into `protected-mode`.
* Do not run the whole soak tier (CLAUDE.md). `make` runs the fast tier.

### A.6 Who touches what

| track | files it owns | must not edit |
|---|---|---|
| 1 memory core | `kernel/memory.inc`, `kernel/mega.inc` (allocator, caches), `kernel/disk*.inc` as needed | apps/ |
| 2 strip | `kernel/kernel.asm`, the HMA/ROM/`.ovl` users, `%ifndef KERN_MEGA` guards | `memory.inc`'s claim/compaction code (track 1) |
| 3 SDK | `apps/cc/*`, `apps/os88partsbody.inc`, `apps/os88api.inc` (additions only), the C packages | kernel/ |
| 4 traps | `apps/*/`, `drivers/*/`, a trap-census knob in `mega.inc`'s `mega_fault` only | `memory.inc` |
| 5 DOS plan | `docs/plans/` only | code |

Tracks 1 and 2 both touch `mem_unblob`'s neighbourhood: track 2 merges first.

---

## Track 1 - the memory core (M3, plan section 13)

**Goal.** The LDT allocator and the first high-first consumers, so kernel-owned
memory leaves conventional RAM.

**State today.** The heap is conventional only, identity selectors,
`mem_claim` with its lo/hi split (`kernel/memory.inc`, `kernel/loader.inc:810`
`mem_claim_hi_x`). `.cold` is copied to 0x125000 and its GDT descriptor
rebased (`mega_coldup`), which bought +36KB of heap. The XMEM pool runs from
0x130000 to the top of extended memory (`mega_xminit`, `mx_tab`, `mx_*`).
The LDT (`MEGA_LDT_LIN` 0x120000, 2048 entries) is a fixed alias table:
entry i is base i x 512, the code alias of heap segment i x 32. Entries
1280-2047 alias segments A000-FFE0, where no image lives, so they are free.

**Do, in order, stopping cleanly wherever the time runs out:**
1. An allocator over LDT entries 1280-2047: data selector or code/data pair
   with any 24-bit base and limit, freed by selector. Say so in os88api.inc
   only if a package needs it now (track 3 does not yet).
2. Carve the extended-memory region track 1 owns out of the `mx_*` pool, or
   give the pool and the heap one table - decide, write down why in plan 13.
3. First consumer: the `dirw` directory cache (`MEM_P_DIRW`, docs/KERNEL-MEMORY.md)
   above 1MB. Every transfer the BIOS performs goes through a low bounce
   buffer (plan 13.2: the Victory's C: is BIOS-only, and a reflected
   `int 13h` gets a real-mode ES:BX). The native floppy can DMA to any
   address below 16MB in one 64KB page.
4. Measure: the Task Manager's heap figure before and after, under Bochs.
5. If time remains: the modules (`mod_need`) above 1MB as code/data pairs.

**Check.** kern_big byte-identical; `make mega`; Bochs boots to the desktop;
a C: to B: copy is byte-identical (Part A.3 tools); `tools/os88bochs.py sweep`
over the shipped packages still starts all of them; the Task Manager reads
more heap.

---

## Track 2 - strip the size machinery from kern_mega (plan 4 item 2)

**Goal.** Take out of kern_mega what exists only because conventional memory
is scarce, and measure what it returns.

**The list** (plan 4 item 2): the HMA `.cold` and its driver pool (SPEC.md
2.10.7, `COLDHMA.SYS`), the ROM `.cold`, the `rp_walk` fixups, `mem_unblob`,
the `.ovl`/`.ovlw` FAT-window loading. For each: is it reachable on kern_mega
at all (`.cold` already moves above 1MB in `mega_coldup`, so the HMA and ROM
homes are dead there), what does removing it cost in risk, and what does it
return in BYTES (CLAUDE.md's banner: the byte, never the rung).

**Mind the fallback.** kern_mega boots in real mode first and enters
protected mode at the end of `kmain` (`mega_enter`). If A20 will not open it
REFUSES and runs on in real mode (`real mode: ...` toast). Anything the boot
needs before `mega_enter`, or the real-mode fallback needs after, stays.

**Do.** One `%ifndef KERN_MEGA` (or a `KERN_MEGA` arm) per item, each its own
commit with its measured bytes (`tools/kernsize.py`, and the Task Manager's
heap under Bochs). Skip an item whose removal you cannot prove safe, and say
why in plan 10.3.

**Check.** kern_big byte-identical; `make mega`; Bochs boots, opens A:, B:,
C:, starts a few packages, plays a WAV with `--sb --cfg 1`.

---

## Track 3 - the SDK's code aliases (M4)

**Goal.** Code that lives in a claim is reached through a CODE selector, not
through its data segment and a #GP fixup per far call.

**State today.** Overlays and parts are far-called through the claim's data
segment: `cc_ovbind` stamps `[cc_ovseg]` and `cc_ovthunk` does `retf` to it
(`apps/cc/crt0.asm`, around 1270 and 1325), and `apps/os88partsbody.inc` does
the same for code parts. On kern_mega every such transfer faults and
`mega_gpfix` completes it - so the packages START (all 44 shipped ones do,
plan 10.1 item 16) but pay a fault per call. The C packages that are not on
mega.img (cword, loom, weave, runcpm, apple2, c64, paccman) have never been
swept at all.

**The rule.** `OSAPI_CPU_INFO` AH bit 4, `CPU_F_PM` (os88api.inc), says
segments are selectors. A heap claim is 512-aligned, so its code alias is
`(seg >> 2) | 4`. One SDK macro or helper that yields "the selector to far
call this segment's code through" - the segment itself without the bit, the
alias with it - makes one binary right on both kernels.

**Do.**
1. The helper, in the SDK (`apps/os88api.inc` additions, `apps/cc/os88.h` if C
   needs it), and use it in `cc_ovbind` and `os88partsbody.inc`.
2. `OS88_HUGE_INCR` (plan 3.2): the selector step between consecutive 64KB of
   one claim - 0x1000 in conventional memory, where the identity map gives it
   for nothing. Add the constant and use it where the C runtime walks huge
   data; the LDT side of it is track 1's later.
3. The C toolchain (`tools/setup-cc.sh`, docs/C-TOOLCHAIN.md), rebuild the C
   packages, and sweep them under Bochs with `tools/os88bochs.py sweep`
   (they need their data files beside them - read each one's `make <name>disk`
   rule for what it carries).
4. The shared speaker and PCM includes (`apps/os88spk.inc` 59 CS writes,
   `os88spkfx.inc` 29, `os88pcm.inc` 4): convert what is in a loop.

**Check.** Every touched package byte-identical on kern_big where the helper
expands to today's code, or reasoned in the commit where it does not; the C
packages start under Bochs; a cword document opens and edits.

---

## Track 4 - the trap census and the hot sites (M5)

**Goal.** Find the #GP fixups that run in loops, and convert them at the
source, worst first.

**How it was done once by hand** (plan 10.3, the Tracker investigation): a
throwaway build that prints every #GP's error, CS and IP to E9 in
`mega_fault`, right after DS is loaded:

```
    cmp byte [ss:bp+20], 0x0D       ; frame: [bp+22] error, [bp+24] IP, [bp+26] CS
    jne .nolog
    ...mega_hex4 the three words, a newline...
.nolog:
```

then `grep '^T' OUT/e9.out | sort | uniq -c | sort -rn`. One site,
`2774:08EE`, fired once per SB block: SOUND.DRV's `sbl_isr`, whose
`push cs / pop ds` hands DS the read-only code alias so the first write
traps. A CS value with bit 2 set is an LDT alias; its image is segment
`(CS & ~7) << 2`, and its offset resolves against that image's listing
(`nasm -l` with the image's own defines - SOUND.DRV is `-DPICOMEM`).

**Do.**
1. Make that logger a knob that a normal build never carries (an
   `%ifdef MEGA_TRAPLOG` and a Makefile define, stamp-tracked like the
   others), or a counting table dumped by `mega_tell` - your call, but it
   must not cost the shipped kernel a byte.
2. Census: `tools/os88bochs.py sweep` every shipped package, and EXERCISE the
   ones that loop (play a module, a WAV, a video; scroll a document; run a
   game for a minute). Rank sites by count.
3. Fix at the source, starting with `sbl_isr`. A DRIVER is one binary on
   both kernels, so it cannot use the kernel's macros: the pattern that is
   right on both is to bank DS once at attach into a CS-resident word and
   load it with `mov ds, [cs:x]` (a read through CS is legal in protected
   mode), never `push cs / pop ds`. Packages: the worst were pixel (593 CS
   writes), weave (153), excitebike (104), video (84), but the census decides
   the order, not the static count.
4. Per fix: the trap count before and after.

**Check.** Each touched `.o88`/`.drv` either byte-identical on kern_big or the
change argued correct on both; the package still runs on kern_big under QEMU
or MartyPC (CLAUDE.md's instruments) and on kern_mega under Bochs.

---

## Track 5 - the plan for M6: DOS beside protected mode, and hibernate

**Goal.** A plan document, no code: `docs/plans/KERN-MEGA-DOS-PLAN.md`.

**Inputs.** KERN-MEGA-PLAN.md 2.6 and 2.7 (the feasibility verdict: drop to
real mode for the bracket) and section 13 (high-first, so conventional memory
is free for the DOS program); docs/plans/DOS-EXEC-PLAN.md and
docs/plans/KERN-DOS-PLAN.md; SPEC.md 96 (the DOS box) and 87 (hibernate); and
the reflection lessons in 10.3: a 286 leaves protected mode only by a reset
through the BIOS's shutdown path, the BIOS may re-initialise the 8259 there
(`mega_r286`), the IVT and IDT are kept in step by `mega_ivt_sync`, and an
option ROM's IRQ (the PicoMEM's) must run in real mode with kernel lines
captured and replayed (items 18-24).

**Answer.** How the box enters and leaves real mode for the length of its
bracket on a 286 and on a 386; what of the kernel must stay below 1MB for it
(plan 13.2's low region, sized); how the high claims survive the bracket
untouched; what the box's XMS door becomes (plan 12.2); how hibernate writes
and restores an image that has state above 1MB (plan 2.6); and what each
wave would cost and how it would be tested, given Part A.3's limits. End
with the open questions only the owner can settle.

**Check.** `python3 tools/checkdocs.py` clean; if it asks for an index entry,
regenerate with `tools/os88index.py`.

---

## Track 6 - M6, the DOS box under kern_mega: waves W0 to W3

Added 2026-10-18, after Track 5's plan landed. **Branch `protected-mode-7`**,
cut from `protected-mode`. Part A applies in full.

**Goal.** A DOS program runs from kern_mega's desktop and the desktop comes
back, on a 386 under Bochs and on the Victory 286, by the route
docs/plans/KERN-MEGA-DOS-PLAN.md chose: the WHOLE-MACHINE handoff ("arm 3"),
with conventional memory PARKED in a block above 1MB in place of a hibernation
file. **Read that plan first, all of sections 1-10 and 12.1** - 12.1 is the
owner's answers to its open questions and it binds you:

* the park and nothing else on kern_mega; the Memory page shows one choice
  there or is not drawn;
* a sum mismatch on the way back RESTARTS with a sentence; the sum is sparse
  but DISTRIBUTED over every region it covers;
* TWO `DOS.O88`s: `mega.img` carries a kern_mega build, kern_big's stays
  byte-identical, and a copy carried to the other kernel REFUSES;
* a 1MB AT must work: park what fits, then only the runs in use, and if even
  that does not fit, leave the rest in place and give the DOS program less;
* the XMS arena is everything left in the pool.

**Not blocked.** W0-W3 use the XMEM pool only through `mx_alloc`/`mx_free`
(`kernel/mega.inc`), which track 1 may restructure but will keep. W5
(hibernate) waits for track 1, and W4 (the HMA) and W6/W7 come after - do not
start them.

**Do, in the plan's order:**
1. **W0 - break it first, then refuse.** Before any change, run a
   `tests/dostrap` probe through the DOS box under Bochs and confirm or
   correct the plan's section 1 predictions (an `int 21h` landing on IRQ1's
   gate; `hbm_handover`'s `retf` into B800h faulting). Record what really
   happens in the plan's section 1. Then the refusal: arms 1-2 greyed on
   `CPU_F_PM`, arm 3 refused until W1, and the Memory page reduced on
   kern_mega.
2. **The two `DOS.O88`s** (12.1 item 4): the build arm, the mega.img recipe
   change, and the cross-kernel refusal (a kern_mega build can tell from
   `CPU_F_PM`; decide how a kern_big build on kern_mega and the reverse each
   say so, in a sentence the user can act on).
3. **W1 - the park on the 386 path** (plan 2.3 steps 3-11): `mega_dropr`,
   `mega_dwake`, the park stub, `kd_leave`'s fetch, the distributed sum, the
   restart-with-a-sentence on a mismatch, and 12.1 item 5's partial park for
   a small machine (Bochs is `megs: 16`; test the 1MB AT case by capping the
   pool, and say how). W1's Bochs checks are in plan section 10, including
   the break-on-purpose ones (`os88bochs.py dbg "setpmem ..."` into the park,
   the GDT and `.cold`), ten round trips with no growth in `mx_tab`, and a WAV
   before and after.
4. **W2 - the 286 drop** (plan 3.1, 3.4): the flag and the branch in
   `mega_r286`, the masks-first landing, and a `mega_tell` line for the trip
   (`DOS park 640K out ooo back bbb ms`). Bochs cannot run it (Part A.3 point
   2), so it ships to the owner with exactly the Victory test plan section 10
   gives, and `DOSRMARK=1` working for the park stub as it does for the
   hibernate stub.
5. **W3 - the XMS door** (plan section 6), with its probe and its negative
   control.

**Keeping out of the other tracks' way.** Put the new kernel code in a new
file, `kernel/megados.inc`, `%include`d from `kernel/mega.inc` inside its
`KERN_MEGA` scope, so tracks 1 and 4 (also in `mega.inc`) merge cleanly. The
one edit inside existing code that cannot move is `mega_r286`'s branch: keep
it to a few lines. HIBER.DRV (`kernel/hiber.inc`, built per kernel) and
`kerndos/`, `apps/dos/` and the Control Panel's Memory page (`kernel/ctrl.inc`)
are yours.

**Measure, do not estimate, where you can.** Every byte figure in the plan is
an estimate; replace each with the built one in the plan as the wave lands.
`tools/kernsize.py --build build/megak` reported kern_big's sizes, not
kern_mega's (found by Track 5); since KERN-MEGA-PLAN 14.45 it measures a
kern_mega tree as kern_mega (`kernsize[mega]`: the sections with `.mega` and
its scratch, the rungs, the segment, and what stays resident after the
switch), and refuses to bless one. `kerndos/kdlayout.inc`'s "133 bytes left" may be stale: the
assertion at the foot of `kerndos/kdos.asm` is the truth.

**Check, per wave.** kern_big's `kernel.bin` and `hiber.drv` byte-identical at
one build number (Part A.2), and kern_big's `DOS.O88` too; `make` and
`make mega` clean; the wave's Bochs checks from plan section 10. The MartyPC
rows that cover `kern_dos` on kern_big (`kdhand`, `kdreturn`, `kdreturnm`,
`kdpart`, `dosmem`, `dosxms` in `tests/suite.py`) run ONCE, after your last
wave, scoped with `python3 tools/os88test.py soak -k ...` (CLAUDE.md's
Testing section) - and only if a kern_big byte moved.
