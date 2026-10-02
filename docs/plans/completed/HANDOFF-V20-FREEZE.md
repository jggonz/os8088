# HANDOFF-V20-FREEZE - os8088 stops dead on MartyPC's NEC V20

**Status: CLOSED (2026-09-29) - it was MartyPC's V20, not os8088.** Kept for
the diagnosis. Upstream's V20 lets `POPF` and `IRET` write FLAGS bit 15 (MD,
the mode flag) like any other bit. The chip write-protects MD in native mode:
only `BRKEM` opens it, and `RETEM` closes it again. So `cpu_detect`'s first
probe, `and ax, 0x0FFF / popf`, CLEARED MD, read `0x7000` back (bits 12-14 are
hard-wired on) and concluded the top bits could be cleared. The second probe
set them all and read `0xF000`, and `[cpu_tier]` came out **2, CPU_386**. An
8088 on the same machine reads 0. Every `cmp byte [cpu_tier], CPU_8086` gate
then took its AT arm against an XT's ports: the PS/2 aux probe
(`mou_p2_init`), `int 15h AH=88h`, the second 8259, and the CMOS instead of
the DIP switches. The tick stopped and control went wild. Section 3's "F206
in and out" was true and misleading: the FINAL `popf` restores the caller's
FLAGS, so MD is back by `.out`, but the tier was already wrong. The MD bit
the handoff found in `.bss` is a consequence, as section 2 guessed.

**The fix is `tools/martypc/patches/07-v20-mode-flag-protect.patch`**: an MD
write-enable on MartyPC's `NecVx0`. `BRKEM` sets it, `RETEM` and reset clear
it, and `pop_flags` keeps MD while it is clear. An interrupt taken in 8080
mode still returns to 8080 mode through its `RETI`, because the flag is open
for the whole session. With the patch the V20 reads `CPU_8086`, keeps MD set,
reaches the desktop and opens packages. **`tests/v20boot.py`** (soak) is the
gate: step 1 reads tier 2 without the patch, and step 3 executes an 80186
shift (`C1 E0 04`) so a profile that is silently an 8088 fails. That step was
checked red on the 8088 twin.

**No kernel change was made.** On a real V20 the probe is correct. There is a
zero-byte hardening if another V20 model turns up with MartyPC's old
behaviour: probe bits 12-14 only (`and ax, 0x8FFF` / `0x7000` masks). Those
three bits alone separate 8086, 286 and 386, and bit 15 is the only one a V20
gives meaning to. It is not taken, because nothing here needs it.

The original handoff follows unchanged.

## 1. What happens

MartyPC's V20 twin of the 5150 hard-disk machine boots the 360 KB system disk
to the desktop's **menu bar and the A: and B: drive icons, and stops**:

- the **dock strip is never drawn** (the bottom of the screen stays blank), and
  a black column sits at the right edge, x ~705-712 of 720;
- the CPU is parked at ONE address, sampled 100 times out of 100:
  **CS = 0060h, IP = CA71h, linear D071h = `ico_stage` + 23h** - which is
  `.bss`, not code (below);
- **IF = 1** there, yet **`[ticks]` has stopped at 37** after ~40 guest
  seconds: the tick is not being counted, so either IRQ0 is not arriving
  (PIC masked / no EOI) or the machine is not in a state to take it;
- FLAGS read **7A92h**. Bit 15 is clear. On a V20, FLAGS bit 15 is **MD, the
  mode flag**, and clear means **8080 emulation mode** - entered by `BRKEM`
  (`0F FF`), left only by `RETEM`/`RETI`. A V20 in 8080 mode executing x86
  code runs garbage, which fits a machine parked on a data address.

The same image on the same machine with an 8088 (`os8088_5150_herc_hdd_gla`)
boots to a desktop in every row of the suite.

## 2. What `ico_stage` is

`kernel/icons.inc:1135`: `ico_ibuf` / `ico_stage` is ONE `.bss` buffer with
two names (SPEC.md 25.7.2) - the icon renderer's indexed-kind decode target
and X-slot record stage - and it is also **`font_run_x`'s glyph POINTER
table** (`font_rn_tab equ ico_ibuf`, kernel/font.inc:2179) plus its two edge
rows on kern_big. It holds data and pointers, never code: nothing in the tree
jumps into it on purpose. So by the time the CPU is there, control flow has
already gone wild. **The MD bit is probably a CONSEQUENCE**: wild execution
through data that contains `0F FF` puts a V20 in 8080 mode, where an 8088
would run `POP CS` and something else.

The last things drawn were the menu bar and two drive icons; the dock (an
icon row, `DOCK.DRV` on kern_big, SPEC.md 2.8) was next and never appeared.
So the icon path - `icon_draw_ix` / `icon_draw_x`, `ico_pass`, and the
buffer they share with `font_run_x` - is the first place to look, but it is a
lead and not a finding.

## 3. Already checked

- **`cpu_detect` (kernel/cpudet.inc) is clean on the V20.** Its FLAGS test
  writes bits 12-15 through `POPF`, which on a V20 is bit 15 = MD. FLAGS
  read **F206h at its entry and at `cpu_detect.out`** (a breakpoint at
  each): MD set, the V20 still in native mode. So the probe does not flip
  it. (`[cpu_tier]` was not read; a V20 should come out `CPU_8086`.)
- **`AAM`** - the V20 ignores AAM/AAD's immediate and always uses 10. Every
  `aam` in the kernel is the base-10 form (clock.inc, clockw.inc, files.inc,
  vidsel.inc), so that difference cannot bite. `AAD` does not appear.
- Resolving the IP: a kernel address must be resolved by LINEAR address
  through `os88sym.linear()` - resolved against `.text` alone, a `.cold`
  address comes back with a plausible wrong name (docs/plans/DISK-CPU-PLAN.md
  section 1). `ico_stage+23` is the linear answer.

## 4. How to reproduce

```sh
make deps && make && make marty     # the V20 profile is baked into
                                    # build/martypc/run by `make marty`
```

The profile is `os8088_5150_herc_hdd_v20_gla` in
`tools/martypc/configs/os8088_machines.toml`: `os8088_5150_herc_hdd_gla` with

```toml
    [machine.cpu]
    upgrade_type = "NecV20"
```

MartyPC accepts `NecV20` as an upgrade of `Intel8088` only
(`COMPATIBLE_CPUS`, marty_core/src/machine_config.rs). `os88marty.launch()`
with the default `boot=True` gives up at `settle`'s desktop gate after 360
guest seconds ("the os8088 desktop never appeared"), so launch it unsettled:

```python
import sys, os; sys.path.insert(0, "tools")
import os88marty, os88sym
m = os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                     machine="os8088_5150_herc_hdd_v20_gla", boot=False)
m.run(); os88marty.pace(m, 30.0)
r = m.regs(); print(r)                       # CS 0060, IP CA71, FLAGS 7A92
w, h, rgb = m.fbuf(None)                     # the menu bar and A:/B: only
os88marty.write_png_rgb("v20.png", w, h, rgb)
m.close()
```

## 5. Where to go next, cheapest first

1. **Is it MartyPC's V20 or os8088?** Boot the same image on a V20 somewhere
   else. 86Box offers the NEC V20 on its XT boards: one run, one photograph
   (86Box has no automation socket here - docs/TESTING.md). A desktop there
   says MartyPC; the same stop says os8088. MartyPC carries a V20 CPU test
   suite of its own (the `RUN_V20_CPU_TESTS` run configurations in
   `build/martypc/src/.idea/`), which is the other half of that question.
   The owner's second machine, a Toshiba T1100 Plus, is an 80C86 and not a
   V20, so it cannot settle this.
2. **Find the first wild transfer.** Breakpoints, not samples: a MartyPC
   `exec` breakpoint on `ico_stage` (linear D04Eh) and a few bytes past it
   catches the moment of arrival, and `m.regs()` then plus the stack (`SS:SP`
   words) says who came from where - a `ret` popping a wrong word, an
   indirect `call`/`jmp` through a table, or an `iret`. `BOOTMARK=1` (the
   Makefile knob) says which `kmain` call last returned, if it dies in boot.
3. **Differences between a V20 and an 8088 that os8088 could touch**, to
   check against what the first wild transfer turns out to be:
   - opcodes `60h`-`6Fh` alias the `Jcc` row on an 8088 and are 80186
     instructions (`PUSHA`, `BOUND`, `IMUL imm`, ...) on a V20;
   - `0Fh` is `POP CS` on an 8088 and a two-byte-opcode PREFIX on a V20
     (`0F FF` = `BRKEM`);
   - shift and rotate counts in CL: an 8088 uses all eight bits, an 80186
     masks to five - check MartyPC's V20 for which it does, then any
     `sh?/ro?/rc? reg, cl` whose CL can reach 32;
   - `SALC` (`D6h`) and other undocumented 8088 opcodes;
   - `DIV`/`IDIV` corner cases, and the flags `MUL` leaves;
   - the PREFETCH QUEUE: os8088 patches its own code in places (SPEC.md
     8.1.1's `NOSMC=1` discussion is one; `mp_stepi_set` in Tracker patches
     its mixer, but that is a package, not the boot). A store into bytes
     already in the queue behaves differently on a CPU that fetches at a
     different rate, and the V20 is faster.
4. **Then fix it on whichever side it is**, with a MartyPC V20 row once the
   profile boots: nothing in `tests/` runs a V20 today.

## 6. Why it matters

A V20 is the classic XT upgrade and the Video Player's heavier speaker paths
were aimed at one (SPEC.md 34.11.9). If os8088 itself does not run on a V20,
that is a real bug for anyone who swapped the chip. If it is MartyPC's V20,
the profile needs noting as unusable until MartyPC is fixed, and V20 numbers
must come from elsewhere.
