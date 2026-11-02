# msp1 agent "megabudget" - kern_mega stops being a KERN_KNOB build

kern_mega size pass 1's follow-up step (`docs/plans/completed/msp1/BRIEF.md`),
branch `msp1-megabudget`, cut from `mega-size-1` at `759da0e6` after the
pass's four agents merged. KERN-MEGA-PLAN 14.126 is the log entry.

**The owner's decision, verbatim**: *"Yes, it should be a normal build with
its own budget. We're getting close to being stable enough for a general
release - and this is a step on that path. Note that this still doesn't move
it to "released" yet - so we're not building disks in the release zip/skill
yet."*

## WHAT KERN_KNOB DID FOR kern_mega, AND WHAT EACH USE BECAME

`make mega` passed `-DKERN_KNOB` because the Makefile's exemption list named
`KERN_SMALL` and `KERN_EMU` and not `KERN_MEGA`. Every use of the define, and
what kern_mega gets now:

| use | what it did for kern_mega | now |
|---|---|---|
| Makefile, `-DKERN_KNOB` filter | made it a knob build | `KERN_MEGA=%` is in the filter-out, beside `KERN_SMALL=%`/`KERN_EMU=%`. A knob ON kern_mega (`MEGATRAP`, `MEGADIAG`, `MEGAFDMASK`, `PICLOSE`, `BOOTMARK`, ...) is still in `$(KNOBS)` and still passes it |
| Makefile `BOOT2_XSEC` / kernel.asm `BOOT2_KNOBSEC` | the knob's extra blob sector, 14 not 13 | KEPT BY NAME: an `ifneq ($(KERN_MEGA),)` beside `KERN_EMU`'s and an `%elifdef KERN_MEGA`. Only a 286+ boots it, `KERN_EMU`'s argument; dropping it would have changed `BOOT2_PAD`, `KSIG_OFF` and the kernel's bytes for no reason, and whether 13 sectors would even hold its blob was not asked |
| kernel.asm guard 1 (`KERN_BUDGET`) | SKIPPED - no budget at all | **rule 4**: `KERN_MEGA_RESIDENT_KB` = 68, `KERN_BUDGET` = 68,096 on kern_mega, held against `MEGA_RESIDENT` (below). The guard reads `%ifndef KERN_KNOB` / `%ifdef KERN_MEGA` / `%elif KERN_SIZE > ...` |
| kernel.asm guard 5 (128KB boot) | SKIPPED; the mega arm (640KB) stood under it | the 128KB line now skips `KERN_MEGA` by name; the 640KB arm is unchanged in effect, its figure named `MEGA_BOOT_KB` beside `MIN_RAM_KB` (with `MEGA_MIN_RAM_KB` moved there), and it binds the knob arms too, as before |
| kernel.asm `HMA_COLD_AT` knob arm | defined 0x4000 - and reached nothing | unchanged: kern_mega's `COLD_VSTART` is 0x80 (14.45), so neither line of `HMA_COLD_AT` is used on it |
| kernel.asm `MOU_IN_BLOB` | nothing - kern_small's rule | unchanged |
| `dskwin.inc` `DSK_OVLPAD` | would have been 1,024 bytes of `.lowbss` | **already fixed by msp1 `shared` item 1**: kern_mega's own define-based arm comes FIRST. It does NOT simplify now, because the knob arms that need the pad (`BOOT_MARK`, `MOU_DIAG`, `SND_SNIFF_SB`, `FDD_FORCE_ABSENT`, `BAKED_FONT`) and the ones that do not (`MEGATRAP`, `MEGADIAG`, ...) are all still `KERN_KNOB` builds - dropping the arm would give every mega knob build, `MEGADIAG`'s field disk among them, 1,024 bytes of dead conventional memory. Only its comment changed |
| kernel.asm `KS_MINRAM`/`KS_BOOTMX` | kernsize printed nothing about boot for mega | on kern_mega they are `MEGA_BOOT_KB` and its ceiling, so the boot line is mega's own |
| `tools/kernsize.py` `MEGA_DEFS`, `knob_args` | `-DKERN_KNOB` was part of the variant; `--bless` refused kern_mega outright | `MEGA_DEFS` is `-DKERN_BIG -DKERN_MEGA`; `knob_args` lost its mega special case (so a mega knob tree is a knob again); the mega report takes a baseline and prints resident against `KERN_BUDGET`, the boot line and a budget-move line; `--release` judges `mres` for mega (and now refuses a knob build outright); `--bless` works on it |
| `tools/os88sym.py` `_SHIPPED_DEFS` | `KERN_MEGA` absent, so every re-assembly of kern_mega added `KERN_KNOB` | `KERN_MEGA` added. Verified both ways on the built trees: `syms(("KERN_MEGA",))` on `build/megak` and `syms(("KERN_MEGA","MEGA_TRAPLOG"))` on `build/megatrap/megak` both pass the byte-identity check |
| `tests/unit/t_kernbudget.py` | read two arms, checked big and small | reads three (`%ifdef KERN_MEGA` / `%elifdef KERN_BIG` / `%else`) and checks the mega baseline's budget too; it prints `KERN_BUDGET big 100864, small 64000, mega 68096` |
| `tests/knobhd.py`, `mseglazy.py`, `msegnomem.py`, `t_registry.py`, `atblit.py`, `vmmouse.py` | comments about derived define sets; none is a kern_mega row | unchanged |

**The kernel is byte-identical.** All four kernels were built at `759da0e6`
and again with the change, at the same commit: `cmp` of `kernel.bin` and
`kernel.sys` for kern_mega, and `md5sum -c` of 177 files across
`build/`, `build/smallk/`, `build/emuk/` and `build/megak/` (every `.bin` and
`.sys`), all OK.

## THE BUDGET, AND WHY THAT QUANTITY

kern_mega has two sizes. `KERN_SIZE` at boot (130,560) is everything stage 1
reads into conventional memory; at the switch `.cold` and `.mega` are copied
above 1MB and their two rungs, 66,048 bytes, go to the heap. What stays is
**`MEGA_RESIDENT` = `KERN_SIZE - (COLD_PARA + MEGA_PARA) * 16` = 64,512** -
the same figure `kernsize`'s mega footprint line already printed as
`resident`, now emitted by the assembler as `mres` on the `ksm:` line.

That is the budgeted one, because it is what a user's programs lose for as
long as the machine is up, and it is the same KIND of number as kern_big's and
kern_small's budgets: a rung-rounded span from `KERNEL_SEG`, with the FAT
window and the task stacks in it. (The section sum
`.text`+`.bss`+`.lowbss`+`.vgabuf` the BRIEF quoted is 58,724; the gap to
64,512 is the FAT window's 4,608 and the rungs' rounding.) `KERN_SIZE` is not
budgeted: it is a boot-time transient, guard 5's to bound, and kern_mega's
growth is MEANT to go into `.mega` above 1MB (the owner's word, 14.45) - a
budget on `KERN_SIZE` would bill exactly the growth the design asks for. The
64KB segment (`KERN_CODE_MAX`, `tools/os88megaseg.py`, `tests/megaseg.txt`)
stays what it was, guard 2 and the segment-overhead ratchet.

**68KB = 68,096 bytes from `KERNEL_SEG`, 3,584 spare - seven steps.** That is
kern_big's own headroom to the byte (97,280 of 100,864). It is the number
picked to the brief's "modest headroom in the style kern_big's has"; the
owner may move it, and it is one `equ` (`KERN_MEGA_RESIDENT_KB`) plus a
`--bless`.

`KERN_GRACE` (2,048) applies as on the other kernels: `make` refuses past
70,144, `make budget-check` past 68,096.

## THE MINIMUM-RAM STATEMENT

**kern_mega boots on a 1MB machine, and what binds is its 640KB of
conventional memory.** Guard 5's mega arm, which predates this step, asserts
`KERNEL_SEG*16 + KERN_SIZE + BOOT2_PAD <= MEGA_BOOT_KB*1024 - BOOT_SECT -
BOOT_STACK`; kernsize's boot line now prints it: **KERN_SIZE 130,560 of
644,096, 513,536 before it cannot boot.** The 128KB rule does not apply and
never did. What kern_mega REALLY needs to boot is about **139KB** of
conventional memory (1,536 + 130,560 + 7,168 + 2,560 = 141,824 bytes) - and
it does not strictly need extended memory either: `mega_gatef` places the
tables in 82KB of conventional memory when extended memory cannot hold them
(14.17), and `.cold` then stays low. Neither of those smaller machines is
tested anywhere, so the guard stays at the 1MB machine's 640KB and the
tighter figure is an owner question below.

## GATES

- `make -j2` (with the fast tier, 68/68), `make -j2 small`, `make -j2 emu`,
  `make -j2 mega` - all green at the tip.
- `make budget-check`: four lines, all spare -
  `kern_big KERN_SIZE 97,280 of 100,864 - 3,584`,
  `kern_small 59,904 of 64,000 - 4,096`, `kern_emu 97,792 of 100,864 - 3,072`,
  `kern_mega resident 64,512 of 68,096 - 3,584`.
- `python3 tools/os88test.py fast`: 68 passed. `python3 tools/checkdocs.py`:
  0 problems.
- Knobs on kern_mega still build, still as knob builds: `make megadiag`
  (`-DKERN_BIG -DKERN_MEGA -DMEGA_DIAG -DKERN_KNOB`) and `make megatrap`
  (`... -DMEGA_TRAPLOG -DKERN_KNOB`), both green, both bannered `BUILT WITH A
  KNOB`. **`MEGADIAG=1` measures 68,096 resident - exactly the budget** (`.text`
  +2,510, `.bss` +1,078: its menu-bar readings are conventional). Unbound, as
  a knob; worth knowing before it is ever asked to fit.
- **The red test.** `KERN_MEGA_RESIDENT_KB` 64 (budget 64,000): `make` still
  assembles (512 over is inside the grace) and `tools/kernsize.py --release
  --build build/megak` fails, *"kern_mega resident 64,512 is 512 OVER
  KERN_BUDGET 64,000"*, rc 1. 61 (60,928 + 2,048 = 62,976): `make mega` fails
  at kernel.asm guard 1, *"kern_mega too big: what it keeps in conventional
  memory after the switch is past KERN_BUDGET's branch grace"*. Restored to 68
  and rebuilt; md5s re-checked.

## ROWS RUN

Once, after the last code commit (`fc8c0b08`; the tip after it is docs only):

| row | tier | why | result |
|---|---|---|---|
| `buildmatrix` | soak | the Makefile's `-DKERN_KNOB` filter and `BOOT2_XSEC` moved; every knob arm, the six mega ones among them, must still assemble | ok, 381.1s |
| `megatree` | soak | kern_mega's private-tree plumbing, cheap | ok, 0.1s |
| `megahigh` | full | kern_mega's above-1MB ratchet, which reads its build | ok, 1.8s |
| `kernbudget` (and the rest of `fast`) | fast | t_kernbudget changed | 68 passed |

No emulator row: the kernels are byte-identical, so a boot row would run
yesterday's kernel and answer yesterday's question.

## WANTS THE OWNER

1. **The number.** 68KB (3,584 spare, kern_big's headroom). Tighter (66KB,
   1,536 spare) or looser is one `equ` and a bless.
2. **The quantity.** Resident-after-switch, rung-rounded, FAT window and task
   stacks included (64,512) - not the section sum (58,724) and not
   `KERN_SIZE` (130,560). Say if a boot-time ceiling on `KERN_SIZE` is wanted
   as well; today only guard 5's 640KB bounds it.
3. **The boot floor.** Guard 5's mega arm asserts the 1MB machine's 640KB.
   kern_mega fits ~139KB; a 256KB IBM 5170 base model would be a real, tighter
   floor, but nothing has booted kern_mega on less than 640KB conventional.
4. **`make budget-check` now builds `make mega`** (its prerequisite list gained
   `mega`, as it has `small` and `emu`), so the release skill, which runs
   budget-check, builds kern_mega's disks into `build/megak/`. It ships none of
   them - the release zip, the skill and the website are untouched - but it
   is a longer budget-check. Say if kern_mega should be measured only from a
   kernel-only target instead.
5. **`MEGADIAG=1` sits at exactly the budget.** As a knob it is not bound; if
   the field-reading disk is ever to be held to the shipped figure, it is 0
   spare today.

## DEFERRED

- The KERNEL-MEMORY.md "Which guard binds" table's kern_big and kern_small
  rows are stale prose from an earlier bless (97,792 / 62,976 against today's
  97,280 / 59,904); the kern_mega row added beside them is this step's. Not
  re-taken here.
- KERN-MEGA-PLAN's head still calls kern_mega "a developer preview", which
  stays true - budgeted is not released.
- Whether 13 blob sectors would hold kern_mega's blob (it keeps 14 by name).
