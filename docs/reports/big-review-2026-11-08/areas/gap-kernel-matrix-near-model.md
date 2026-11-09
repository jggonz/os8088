# Is the kern_small / kern_big / kern_mega / kern_emu matrix and the near-model rule still the right shape, and what do they cost?

Gap review, big review 2026-11-08. All figures are at `4b9f1715` unless they
say otherwise. Line numbers are that tree's (the round-1 doc fixes moved
several since the area reports were written). "MEASURED" means read off a
map, a listing or a census script. "ESTIMATED" means arithmetic on measured
parts.

## Scope read

- `kernel/kernel.asm`:
  - 1-330: the header, the split, kern_emu, kern_mega, the KHP block.
  - 380-840: the segment-abstraction macros, KDS through KES.
  - 860-1200: the feature-symbol block and ROM_COLD/HMA_DRV/ROM_PKG.
  - 1340-1800: GFX_VGA, the budgets, KERN_CODE_MAX, DSK_FAT_SECS, the ladder.
  - 2150-2166: the section declarations.
  - 6940-6956 and 8090-8135: the `ct_` per-build trampolines.
  - 8718-8740: the VGA-window guard.
- The per-module census, by script, over `kernel/*.inc` and `kernel.asm`:
  - every `%if`/`%elif` naming `KERN_SMALL`, `KERN_BIG`, `KERN_MEGA` or `KERN_EMU`;
  - every feature symbol;
  - every segment-abstraction macro site;
  - the parity pads;
  - literal far crossings (`call COLD_SEG:` and `call KERNEL_SEG:`).
- Arm sites read by hand:
  - `driver.inc` 2140-2215 and 3925-3945 (the CLASSK predicate and the HMA placement);
  - `hiber.inc` 3395-3475 (HBM_BADV);
  - `fsx.inc` 1025-1062;
  - `sched.inc` 46-60 and 174-180, plus kern_emu's arms at 764, 1810, 2370 and 2503;
  - `driver.inc`'s kern_emu rows;
  - `mouse.inc` 3950-4180 (`osapi_mouse_feed`, `mou_apply_abs`, `mou_apply`);
  - `mod.inc` 105-140 (MOD_STAMP);
  - a sample of `desk.inc`, `memory.inc`, `vga12.inc`, `font.inc`, `wm.inc` and `vidsel.inc` arms.
- Maps: `build/smallk/kernel.map` and `build/megak/kernel.map` (section lengths); kern_big's from bytes.md's assembled listing.
- Sizes: `build/*/kernel.sys` and `build/megak/*.drv`.
- `tests/suite.py`: rows by tier and kernel, from the registry; `_ports`, 21712-21815.
- Tests: `tests/unit/t_hotpar.py` and `t_kernbudget.py`, plus which unit gates read `smallk` and `megak`.
- The Makefile: 1207, 1511-1525, 2680, 14806-14831.
- `.claude/skills/release-os8088/mkzip.py` 150-330.
- SPEC.md: §2.6 and §33 (1049-1130, 70971-70991), §9.11.3 and §9.11.7.
- Docs: `docs/KERNEL-MEMORY.md` 40-125.
- Plans:
  - MEGA-RELEASE-PLAN (all);
  - KERN-MEGA-PLAN 790-800 and its byte-identical rows;
  - LAST-DROP-PERF §7;
  - ABI-BREAK-LIST §1 and §L;
  - KERNEL-BYTE-QUEUE §1.
- Area reports:
  - bytes-3 and bytes-4, plus bytes.md M2 and M5-M7;
  - abi-2 and abi-3;
  - megacore's rethinks and prior art;
  - megados prior art;
  - video-1 and video-7;
  - boot.md mismatches 2, 42 and 43, and engsys-4;
  - all 66 bug findings in `data/*.json`, classified per arm.

## Verdict

**The matrix has the right shape. What it is missing are the mechanisms that
keep its arms honest.** The kernels should stay build-time products. The data
rules out unifying them at run time:

- **kern_small** carried at run time inside kern_big would spend **+5,440 bytes
  of tables before any code**:
  - task stacks 960 -> 2,816;
  - the FAT window 1,024 -> 4,608.

  That is against kern_small's whole **5,120** of headroom (64,000 - 58,880).
- **kern_mega** carried at run time would put **~12-14.7KB of arm code** on every
  XT, against kern_big's **4,096** of headroom.

That is the answer Windows 3.0 gave the same question: KERNEL.EXE, KRNL286
and KRNL386 were three binaries off shared source, with WIN.COM choosing
between them.

os8088 already has most of that model:

- additive build names guarded against nonsense combinations;
- a feature-symbol layer that reads like a SysV configuration file;
- segment macros that let ~400 sites serve both real and protected mode with
  no arm, and are byte-identical off kern_mega;
- packages that are one binary across kern_big and kern_small.

What it lacks is what keeps the arms honest:

1. **One-arm fixes.** Two shipped kern_big defects (drivers-3, hiber-2) were
   fixed on kern_mega and left on kern_big. The isolation discipline ("kern_big
   byte-identical") was applied to a cause that was in shared code.
2. **Refusal stubs nobody decodes.** Three of the four kern_small-only bugs
   are a refusal stub or a guard written apart from the full body (abi-2,
   dw-8, video-1). No gate decodes kern_small's table.
3. **Coverage is inverse to importance.** The 128KB floor has 28 soak rows
   and kern_mega has ~430. `test-full` never boots kern_mega. The parity gate
   covers the two kernels where parity matters least, and not the one where
   it matters most.

**Of the review's 64 code bugs, 32 live on one arm or arm-set:**

| arm or arm-set | bugs |
|---|---:|
| kern_mega only | 8 |
| kern_small only | 4 |
| kern_big's feature set | 20 |

Only **5** of the 32 are divergence: one contract implemented twice, with the
two implementations disagreeing. Those 5 are the real matrix tax. The other
27 are defects in a feature that only one arm carries, and they would exist
in any shape.

**The near model** no longer limits kernel *capacity* on kern_small or
kern_mega:

| kernel | free in the 64KB window |
|---|---:|
| kern_small | 32,565 |
| kern_mega | 14,056 |

It still binds **kern_big**, at the VGA rung's window: **6,946 bytes**, not
the 19,842 that KERN_CODE_MAX's own line implies (`kernel.asm:8729-8736`).
Everywhere else it is a **placement** rule.

`.cold`'s ~700 / ~540 bytes of crossings (bytes-4) buy:

- the HMA on a 286;
- a 5150 ROM;
- the slide over the VGA rung;
- 46KB above 1MB on kern_mega.

Keep it. Flattening kern_small saves 540 bytes and costs that kernel its ROM.
That is the owner's call, priced below.

**The recommendation to accept or refuse:**

- **Accept now (no ABI break, ~+6 to +12 resident bytes on kern_big, a few on
  kern_small):**
  - gkm-1: the two one-arm fixes, and the rule that prevents a third;
  - gkm-2: abi-3's four-kernel ABI gate, extended to the refusal stubs;
  - gkm-3: a `smallport.txt` port list;
  - gkm-4: `t_hotpar` over kern_mega;
  - gkm-7: the build-name ratchet;
  - gkm-6: kern_emu retired into a USBMOUSE-shaped VMMOUSE.DRV (+~40 kern_big).
- **Record:**
  - gkm-5: do not unify at run time;
  - gkm-8: do not flatten kern_small unless its ROM is dropped;
  - gkm-9: the one-disk 8088/286 product (WIN.COM's half), which comes after
    L3 and does not replace it. **L3 is the matrix's package half.** Kernels
    may fork; packages should not. That is Windows 3.0's line, and L3 plus the
    PROTMODE-style clean bit (megacore rethink 4) is how os8088 reaches it.

## Strengths

1. **One product name per build, with additive variants and refused
   combinations** (`kernel.asm:190-246`):
   - KERN_BIG and KERN_SMALL are both positive and mutually exclusive;
   - KERN_EMU and KERN_MEGA *imply* KERN_BIG, so no `%ifdef KERN_BIG` misses
     them;
   - small+emu, small+mega and emu+mega are `%error`s.

   This is the cheapest matrix hygiene there is, and it is complete.
2. **A configuration layer above the includes** (`kernel.asm:868-1112`):
   - the symbols are OS88_THEME, OS88_ASSOC, OS88_DRIVERS, OS88_SNDCARD,
     OS88_REDIR, OS88_IMG, OS88_RTC, OS88_COMPACT, FCP_MOD and FDLG_MOD;
   - each is resolved once, with its reason, above every `%include`. The
     comments name the one-pass-preprocessor trap that forces this.

   The comments draw distinctions such as "this one says there is no card;
   that one says no volume can be redirected", which is SysV's `master` file
   in prose. **About 700 conditionals** test these symbols rather than a
   build name.
3. **Protected-mode cleanliness enforced by the assembler, not by review**
   (`kernel.asm:389-560`). KDS, KSEGR, KDATAR, VSEG, KSEG and the CSDO family
   are byte-identical to the real-mode instruction off kern_mega. Each
   `%error`s when used in a section where its premise is false ("KDS outside
   .text: CS is not the kernel's there"). About **400 macro sites** (KSEG 54,
   CSDO 176, KDS 37, VSEG 22 and others) serve both modes with no arm. That is
   Windows 3.0's "write protected-mode-clean real-mode code" rule, made
   mechanical.
4. **The XT does not pay for the 286, and that is proved.** KERN-MEGA-PLAN's
   rows record kern_big and kern_small byte-identical across every
   kern_mega-only change (e.g. KERN-MEGA-PLAN.md:7190, 7529).
   MEGA-RELEASE-PLAN §3 closes the same way.
5. **The budgets are derived and all four are enforced at release**
   (`kernel.asm:1414-1466`; `Makefile:14830-14831` runs `kernsize --release`
   on kern_emu and kern_mega beside the two shipped ones).
6. **Packages are already one binary across kern_big and kern_small.**
   - `MAX_TASKS` is 5 against 14 (`sched.inc:48`, `:60`) and `MAX_WIN` 6
     against 12 (`wm.inc:146`).
   - The SDK copies stay at the large values, so a small kernel's snapshot
     is over-allocated rather than overrun. That is the reasoning in the
     comments.
   - Windows 3.0's real lesson — everything above KERNEL is mode-portable —
     is already learned for packages. Only kern_mega's `$(MEGAPKGS)` arms
     break it, and L3 is aimed at those.
7. **A run-time seam already exists as the alternative to arms.** kern_small
   ships FCP_MOD and FDLG_MOD as on-demand modules (`kernel.asm:1104`,
   `:1112`). kern_big's `.vga` rung is dropped at boot on a mono machine. The
   tree has both mechanisms (build-time arm and run-time shed) and has used
   each where it fits.

## Findings

### gkm-1 (hazard, medium, now): a fix made on one arm leaves the shipped kernel with the defect

**Evidence (code read at HEAD):**

- **drivers-3.** `osapi_drv_classk_x` counts loaded rows only `%ifdef
  KERN_MEGA` (`driver.inc:2167-2177`, `:2198-2209`). kern_big keeps
  `cmp ax, 1` on the KB sum. Yet kern_big's own HMA placement zeroes the field
  that sum reads: `and word [bx+DRVR_PARA], 0 ; no heap held` (`:3933`).
  kern_mega's comment names the shape exactly ("drv_hi_move ... zeroes its
  DRVR_PARA"). The fix was written for one image-off-heap path, and the other
  image-off-heap path never got it.
- **hiber-2.** `%define HBM_BADV .badv` on kern_mega and `.bad` everywhere
  else (`hiber.inc:3400-3404`). Yet `.badv` itself is assembled on kern_big:
  it sits inside `%ifdef OS88_IMG`, kern_big's own symbol (`:3405-3468`, the label at `:3459`). The
  repair was in kern_big's HIBER.DRV image, and only kern_mega routed to it.
- **The discipline.** KERN-MEGA-PLAN.md:798 says "kern_big assembles
  byte-identical with all of it", and its per-row tables re-prove it.
  MEGA-RELEASE-PLAN §3 says "kern_small and the kern_big image byte-identical
  for every kern_mega-only change". It is the right proof for kern_mega's
  MECHANISM. Applied to a defect whose cause is shared code, it becomes a
  reason to leave the shipped kernel broken. MEGA-RELEASE-PLAN §4.2 lists
  "sixteen defects kern_big ships too (seven fixed here)". These two are not
  in that list, because nobody recognised them as kern_big's.

**Recommendation:**

1. Make both arms unconditional.
2. Write the rule into KERN-MEGA-PLAN's checklist: *"a fix made under
   `%ifdef KERN_MEGA` says why its cause is not in shared code; if it is, the
   fix lands unconditionally, in the same commit, and the byte-identical claim
   is dropped for that row."*
3. Rewrite CLASSK's header (mismatch D8). Its "four bytes for eight" no
   longer holds.

**Bytes (ESTIMATED):**

- kern_big/kern_emu `.cold`: +6 (`push cx`, `xor cx,cx`, `inc cx`, then
  `cmp cx,1` and `pop cx` in place of `cmp ax,1`).
- HIBER.DRV: 0, a jump target.
- kern_small: 0 (no drivers).
- kern_mega: 0.

**Listed as:** drivers-3 and hiber-2 (this review). Neither is in
MEGA-RELEASE-PLAN 4.2.

### gkm-2 (weakness, medium, now): kern_small's arms fail at refusal stubs and guards, and no gate decodes them

**Evidence:** 3 of the 4 kern_small-only bugs are divergence at a stub or a
guard:

- **abi-2.** `OSAPI_XREFUSE` (`mov ax,1 / cmp al,2 / retf`) answers a cell
  whose contract is "Every register kept".
- **dw-8.** The WRITE_AT stub enters below its neighbours' `mov ax, FERR_NAME`.
- **video-1.** The dual-card refusal in `fsx_page` is `%ifdef KERN_BIG`
  (`fsx.inc:1034-1042`). The guard follows the build because `vid_dual_ok`
  lives in vidsel's KERN_BIG block. The hardware fact it guards (a CGA beside
  the Hercules) is just as possible on kern_small, whose `vid_avail` carries
  both.

`t_api_abi` opens `build/kernel.bin` only (abi-3). Its FIXED ledger has no
row for kern_small's XREFUSE cells, so it has never been run there.

**Recommendation:**

- **Take abi-3** (a `--kernel` mode over `smallk`, `emuk` and `megak`, with
  per-kernel FIXED rows, registered where those kernels are built).
- **Contract-shaped refusal stubs.** `OSAPI_XREFUSE` takes the cell's refusal
  shape as an argument (`KEEP`, which is `stc / retf`, or `AX=FERR_x`), so a
  stub cannot be written without saying which contract it answers.
- **Guards gate on capability, not build.** A guard must be gated on the
  capability it protects, never on a build name. That is gkm-7's ratchet.

**Bytes:**

- abi-2: 0, the cell stays 6 bytes.
- dw-8: +0 to +3 on kern_small.
- video-1: ~+6 on kern_small.
- The gate: 0.

**Listed as:** abi-2, abi-3, dw-8, video-1 (this review).

### gkm-3 (weakness, medium, now): coverage per kernel is inverse to the product's floor

**Evidence (MEASURED from `suite.rows()`: 2,034 rows; fast 76, full 14, soak 1,944):**

| kernel | soak rows | in `full` | fast gates that read its build |
|---|---:|---|---|
| kern_big | ~1,490 | 12 | all |
| kern_mega | ~430 (343 `mega-` ports from `tests/megaport.txt`) | `megahigh` (2 s, host-side); **never booted** (MEGA-RELEASE-PLAN §9) | `megagrid`, `megapkg`, `cswrite` (source), `kernbudget` |
| kern_small | **28** | `small128` | `kernbudget`, `hotpar`, `image`, `canary`, `blobruns` |
| kern_emu | **1** (`vmmouse`) | - | none |

The registry already ports rows to other kernels and emulators through
`_ports` (`suite.py:21712-21796`). `box86port.txt` already runs a row on
kern_small's own build with `@OS88_BUILD=build/smallk
@OS88_DEFINES=KERN_SMALL` (`:21804-21808`). There is no MartyPC lane for a
kern_small port, so the 128KB XT has no ported coverage on the instrument
that models it.

**Recommendation:**

- Add `tests/smallport.txt` and a `small-` prefix to `_ports`, letting a line
  keep the base row's `marty` need. Today `_ports` drops `marty` and forces
  Bochs or 86Box (`:21762-21766`).
- Seed it from the modules where kern_small has two-body arms:
  - `desk.inc` 16;
  - `diskw.inc` 12;
  - `disk.inc` 10;
  - `memory.inc` 9;
  - `files.inc` 8;
  - `wm.inc` 7;
  - `fsx`;
  - the module seam.
- Cost: 0 kernel bytes. The soak minutes go to the floor the project is named
  for.

**Listed as:** none.

### gkm-4 (perf, low-medium, now): the parity gate covers kern_big and kern_small, not kern_mega

**Evidence:**

- `t_hotpar` checks `HOT["big"]` and, when `build/smallk` is current,
  `HOT["small"]` (`t_hotpar.py:128-142`).
- Its header gives a reason for leaving kern_emu ungated (`:36-38`) and says
  nothing about kern_mega. Yet the header's own premise is "on a 16-bit bus -
  the 8086, a V20/V30, every 286 in vm/ - a word fetch at an odd address is
  two bus cycles" (`:4-6`).
- **Every kern_mega machine has that bus.** kern_small's 128KB machines are
  nearly all 8088s (5150, 5160, PCjr, Tandy 1000), where parity costs nothing.
  Even so, kern_small carries **9** hand-placed build-gated parity pads, and
  kern_mega **2**.
- The pads by kernel (census):
  - KERN_SMALL: `diskw.inc:193`, `font.inc:1010/1014/2069/2515`,
    `memory.inc:761`, `vga12.inc:4559`, `viddet.inc:1106`, `vidsel.inc:1634`;
  - KERN_BIG: `dock.inc:435`, `kernel.asm:6412`, `sched.inc:2770`, `wm.inc:7908`;
  - KERN_MEGA: `instance.inc:723`, `mouse.inc:2260`.
- Each pad is hand-tuned to one kernel. A byte cut in shared code ahead of a
  pad flips parity on the kernels it was not tuned for.

**Recommendation:**

- Pin `HOT["mega"]` in `t_hotpar` when `build/megak` is current, the same way
  kern_small is handled.
- Replace the 15 build-gated pads with one self-correcting macro, `HOTPAR
  even|odd`. It expands to `%if (($-$$) & 1) != want / db 0 / %endif` and is
  placed at the same non-fall-through points. The tree already writes this
  form relative to a local anchor (`vidext.inc:103`, `font.inc:620`).
  Section bases are even on every kernel (`.text` vstart 0, `.cold` vstart
  0x6000/0x10/0x80), so the absolute form is sound.

**Bytes:** 0 net. Each site becomes 0 or 1 byte per kernel, chosen by the
assembler instead of by hand.

**Listed as:** none.

### gkm-5 (rethink, medium, record): do not unify the kernels at run time. Priced

**Evidence (MEASURED sections; ESTIMATED deltas):**

| kernel | `.text` | `.bss` | `.cold` | `.mega` | binding figure | of |
|---|---:|---:|---:|---:|---:|---:|
| kern_big | 40,328 | 5,366 | 37,566 | - | KERN_SIZE 96,768 | 100,864 |
| kern_small | 30,035 | 2,936 | 21,031 | - | KERN_SIZE 58,880 | 64,000 |
| kern_mega | 46,076 | 5,404 | 46,509 | 23,568 | MEGA_RESIDENT 62,976 | 68,096 |

- **kern_small as kern_big that sheds at boot.** Its tables would have to be
  sized for the large machine, or become claims:
  - `MAX_TASKS` 14 vs 5: stacks 2,816 vs 960 (`sched.inc:176-177`), +1,856;
  - `DSK_FAT_SECS` 9 vs 2 (`kernel.asm:1519`, `:1565`), +3,584 of FAT window.

  That is **+5,440** against 5,120 of headroom, before a single run-time door.
  The VGA slide needs 14 registered doors (`tests/vgarefs.txt`) for one rung.
  kern_small cuts ten feature symbols. It would also give up the guarantee
  that the 64KB is proved by the assembler: guard 1 would become a boot-time
  hope.
- **kern_mega as kern_big plus a protected-mode rung (the KRNL286 model,
  folded into one binary).** kern_mega's `.text` is +5,748 and its `.cold`
  +8,943 over kern_big's. Less kern_big's `.vga` (2,030, which kern_mega
  assembles in its window) and the ten KHP knobs (~452), that is **~12-14.7KB**
  of arm and macro code. All of it would run, or be jumped over, on every XT,
  against kern_big's **4,096** of headroom. The macros alone are byte-for-byte
  more expensive in protected mode: KDS goes from 2 bytes to 5, KSEG from 2
  to ~17.
- **The prior-art reading is the decisive one.** Windows 3.0 did *not* ship
  one bimodal kernel. It shipped KERNEL.EXE (real mode), KRNL286.EXE and
  KRNL386.EXE from shared source, and WIN.COM chose at start-up by CPU and
  memory. Windows 3.1 then dropped the real-mode kernel entirely. OS/2 1.x
  never had one. Mac System software left the 128K Mac behind rather than
  fork for it. DOS is the one-binary system, and it managed that by having
  almost no features.

**Recommendation:** keep four build-time kernels. Write this table into
docs/KERNEL-MEMORY.md as the reason, so the question is not reopened as an
"obvious" simplification.

**Bytes:** 0 (keeping). Refused: +5,440 to +14,700.

**Listed as:** none. megacore and megados prior art stops at the table.

### gkm-6 (rethink, medium-low, now): retire kern_emu. VMMOUSE.DRV in USBMOUSE.DRV's shape

**Evidence:**

- **What kern_emu carries.** It exists to carry VMMOUSE.DRV's resident half:
  - a gate byte, `vmm_dsp`, `vmm_poll` (from `ui_task` and from `task_yield`)
    and `mou_apply_abs`, "385 bytes of image" (SPEC.md 9.11.7);
  - a scheduler change: the idle task does not halt again, and
    `sch_idlewake` is not posted (`sched.inc:764`, `:1810`, `:2370`,
    `:2503`, `%ifndef KERN_EMU`).
- **The precedent on kern_big.** kern_big already has a polled pointer driver
  with *no* kernel poll site. USBMOUSE.DRV spawns a worker (SPEC.md 51.7) that
  yields rather than sleeps for `UM_HOTT` ticks after a report, so a moving
  mouse is read every pass (`usbmouse.asm:12-25`, `:88-93`). It delivers
  through `OSAPI_MOUSE_FEED`, which is relative.
- **Converting the absolute report.** `mou_apply` is a plain add and clamp,
  with no acceleration (`mouse.inc:4139-4146`). An absolute report becomes a
  feed exactly: `dx = scaled_x - mouse_x`, with `mouse_x` read through
  `OSAPI_MOUSE` and passed through the same `mou_clamp`. The 386 probe moves
  into the driver behind `OSAPI_CPU_INFO`, which is where §9.11.7 says the
  probe cannot live on an XT.

**Recommendation:**

1. Re-cut VMMOUSE.DRV as a DRVC_POINT worker driver on kern_big.
2. Drop `KERN_EMU`: 21 blocks, `make emu`, `build/emuk`, `emu.img`, a
   `kernsize --release` leg, the `_SHIPPED_DEFS` entry and a buildmatrix leg.
3. The browser live image (§80.7) boots kern_big with VMMOUSE.DRV ticked.

The open risk is drag latency between worker passes. USBMOUSE's UM_HOTT
answer is already shipped and measured on real hardware.

**Bytes (ESTIMATED):**

- kern_big resident: about +40 (a `drv_tab` row, file name, label and CFG
  bit; §9.11.7 removed "27 bytes ... for a row" from kern_big when the driver
  left it).
- kern_emu-only resident: -385.
- The scheduler arms: -10 lines.

**Listed as:** none.

### gkm-7 (weakness, low-medium, now): build names, not features, are still the main configuration language of the modules

**Evidence (MEASURED census script):**

| build name | blocks | blocks with two bodies | lines in two-body blocks |
|---|---:|---:|---:|
| KERN_BIG | 636 | 147 | 1,857 |
| KERN_SMALL | 124 | 46 | 341 |
| KERN_MEGA | 406 | 152 | 832 |
| KERN_EMU | 21 | 3 | 10 |

There are about 700 feature-symbol tests beside these.

Where the build-name arms sit:

- KERN_BIG: `ctrl.inc` 95, `wm.inc` 85, `kernel.asm` 77, `vga12.inc` 53,
  `driver.inc` 35, `ui.inc` 34, `font.inc` 34, `files.inc` 34.
- KERN_MEGA, outside its own files: `memory.inc` 58, `hiber.inc` 41,
  `disk.inc` 40, `diskw.inc` 30, `compress.inc` 22, `mouse.inc` 19,
  `driver.inc` 18.

**Why it matters.** "kern_small is kern_big minus named features" holds only
where a symbol exists. A raw `%ifdef KERN_BIG` around a guard is how video-1
happened. A guard placed by build is invisible to anyone asking what the
other build does with the same hardware.

**Recommendation:** a ratchet, shaped like `tests/textsites.txt`:

- build names may appear in `kernel.asm`'s define block and in `mega*.inc`;
- every other site is registered with its reason;
- a new one fails `make`;
- sites move to named symbols as their files are touched.

There is no big conversion pass.

**Bytes:** 0.

**Listed as:** none.

### gkm-8 (rethink, low, record): flattening kern_small to one code segment. Priced and not recommended

**Evidence:**

- kern_small's `.text + .bss + .cold` is 54,002, inside 64KB with room. Its
  crossings cost ~540 bytes (bytes-4).
- Its `.cold` exists for one thing on that kernel: the ROM. It has no VGA
  rung to slide over (`kernel.asm:1152`) and no HMA. ROM_PKG puts a package in
  that ROM too, on kern_small alone (`:1186-1191`).
- The crossings are written as literals: **380 `call COLD_SEG:`** and **335
  `call KERNEL_SEG:`** in the tree, plus §2.6.1's `retf`-ending entry bodies.
  A flat arm therefore needs every one of them behind a macro first (COLDCALL
  and TEXTCALL exist, `kernel.asm:5515-5530`, and are rarely used).

**Recommendation:** refuse while kern_small's ROM is a product, because
21,031 bytes of `.cold` plus a package off a 128KB heap outweigh 540. If the
owner drops the ROM for kern_small, the trade inverts.

**Bytes:** -540 on kern_small if taken.

**Listed as:** bytes-4 (same arithmetic; this adds the ROM_PKG half and the
site census).

### gkm-9 (rethink, low, record): one disk set for the 8088 and the 286 — WIN.COM's half, after L3

**Evidence:**

- kern_mega ships on its own disks under `286-PLUS/` (`mkzip.py:312-325`), so
  a user chooses a floppy by CPU.
- Windows 3.0 made that choice in WIN.COM, from one install. That worked
  because USER, GDI and the drivers were NE modules linked by ordinal at load
  time, so they were mode-portable.
- os8088's kernel modules are bound to one kernel's layout:
  `MOD_STAMP equ (KTEXT_SIZE + KBSS_SIZE + COLD_SIZE [+ MEGA_SIZE] + KLOW_SIZE)`
  (`mod.inc:113-140`). Every kernel needs its own CTRL.DRV, DESK.DRV and the
  rest.
- A dual-kernel system disk carries kern_mega's `KERNEL.SYS` (**115,872**
  bytes) plus its **11** modules (**56,607**). That is ~172KB: feasible at
  1.2MB and 1.44MB, not at 720KB or 360KB.
- Stage 2 reads the kernel as the first file in the data area, not by name.
  A second kernel therefore needs a second fixed placement (`os88disk.py`)
  and a CPU and extended-memory test in `.boot2`. That code is transient.
- Packages still differ (`$(MEGAPKGS)`) until L3. **L3 is the precondition**:
  it makes the apps disk one disk, and only then is a shared system disk worth
  building.

**Recommendation:** record as the product step L3 enables, in this order:

1. L3, plus megacore rethink 4's clean bit;
2. one apps disk;
3. the selector.

Do not build the selector before L3.

**Bytes:** `.boot2` about +60 to +120 (transient, not resident). Disk: +172KB
on the 1.2MB and 1.44MB system disks.

**Listed as:** ABI-BREAK-LIST L3 (the precondition).

### gkm-10 (weakness, low, now): which kernel has budget slack is deciding which kernel gets a speed-up

**Evidence:**

- The block that defines the KHP knobs says kern_mega "carries the first
  eleven ... and its conventional budget had room" (`kernel.asm:255-257`).
- Rows 2-11 of LAST-DROP-PERF §7.2 are speed-ups for desktop paths that the
  XT runs slowest:
  - KHP_RELIT: a full-screen exit on CGA, 781 -> 629 ms;
  - KHP_RAISEOL: a CGA raise, -5 to -14 ms;
  - KHP_CAPTION;
  - KHP_DMGSTALE;
  - and others.
- They cost **~452** bytes on kern_big, **214** of them `.vga` (resident only
  on VGA/EGA machines).
- They ship only on the 286/386 kernel. This is the "a rung is not a design
  input" rule applied across kernels: one kernel's headroom is not a reason
  for another kernel to lack a feature.

**Recommendation:** decide each knob by the XT question the owner already
asks, "N bytes for M cycles on the target", and say per knob why kern_big
says no. The wm.md and sysmisc.md findings already name KHP_RELIT,
KHP_CAPTION and the others individually.

**Bytes:** 0 now. +452 on kern_big if all ten are taken (+238
`.text`/`.cold`, +214 `.vga`).

**Listed as:** LAST-DROP-PERF 7.2/7.3; wm.md and sysmisc.md.

### gkm-11 (weakness, medium, record): the near model still binds kern_big, at the VGA rung, by 6,946 bytes

**Evidence:**

- The VGA rung is near-called, so its last byte must be a KERNEL_SEG offset:
  `%if VGA_VSTART + VGA_SIZE > 0x10000 / %error` (`kernel.asm:8731-8736`,
  "It is the binding form of KERN_CODE_MAX now").
- On kern_big it sits above `.text`, `.bss`, the FAT window and `.lowbss`.
  kernsize reports the window at **6,946** left.
- CLAUDE.md and docs/KERNEL-MEMORY.md still define KERN_CODE_MAX as
  `.text + .bss` (mismatch D7), which reads as 19,842 left.

| kernel | binding form | left |
|---|---|---:|
| kern_big | VGA rung window | 6,946 |
| kern_mega | `.text + .bss` (no VGA rung) | 14,056 |
| kern_small | `.text + .bss` | 32,565 |

**Recommendation:** on kern_big, quote the window figure, not KERN_CODE_MAX's.
A `.lowbss` or FAT-window byte on kern_big now costs window headroom as well
as budget.

**Bytes:** 0.

**Listed as:** bytes-4 (which quotes the figure but does not name it as the
binding one).

## Rethinks

1. **Kernels fork; packages must not.** This is the systemic lesson of
   Windows 3.0, and os8088 already has two-thirds of it:
   - three kernels off one source (gkm-5);
   - one package binary across kern_big and kern_small (Strength 6).

   The remaining third is kern_mega's package arms. L3 plus a PROTMODE-style
   "clean" header bit (megacore rethink 4) retires them incrementally,
   without waiting for The Wire to serve two builds. The point of the
   framing is to stop treating `$(MEGAPKGS)` as normal: every armed package
   is matrix cost paid outside the kernel.
2. **Make arm honesty mechanical, not cultural.** gkm-1, gkm-2, gkm-4 and
   gkm-7 are one idea: each kernel's divergence surface (~350 two-body blocks
   in all) gets the gates kern_big already has. That means:
   - the ABI decoded per kernel;
   - parity pinned per kernel;
   - a MartyPC port list for the floor;
   - a ratchet on raw build names.

   None of it costs a resident byte.
3. **Placement per build, where the machine differs.** `KSW_SECT` already
   names a section that differs by kernel (`.modu` on kern_small, `.text` on
   kern_big). kern_mega could push more `.text` into `.cold` (above 1MB)
   without charging the XT crossings, if a module's crossing sites go through
   the KSWK/KSWC/KSWN-style macros. It is not needed now: MEGA_RESIDENT has
   5,120 of headroom. Record it as the lever for when kern_mega grows.
4. **Retire variants that exist for one feature** (gkm-6). kern_emu is a
   build because a polled device once needed kernel poll sites. USBMOUSE.DRV
   proved it does not.

## Prior art

| system | how it shaped the "one OS, several machines" problem | better than os8088 | os8088 better | cheap to adopt |
|---|---|---|---|---|
| **MS-DOS 5/6** | ONE kernel binary for 8088 through 486. Run-time adaptation via CONFIG.SYS: DOS=HIGH, HIMEM, EMM386, loadable drivers | one binary, so no matrix at all | DOS had nearly no features to fork. os8088's ROM/HMA adoption is DOS=HIGH done at run time, and its 128KB floor is real where DOS's feature set was simply small | the run-time adoption os8088 already does; nothing more |
| **Windows 3.0** | WIN.COM picks real (KERNEL.EXE), standard (KRNL286 + DOSX) or enhanced (KRNL386 + WIN386) at start-up. USER, GDI and drivers are shared NE modules linked by ordinal; apps are one binary with a PROTMODE flag | the selector and one install (gkm-9); mode-portable modules thanks to a dynamic linker; the clean bit | os8088's segment macros make kernel code PM-clean at assembly time. Windows relied on apps and drivers following written rules | the clean bit (megacore rethink 4); the selector after L3 |
| **Windows 3.1** | dropped real mode: two kernels, both protected mode | dropping a tier when the feature set left it behind | os8088 keeps its 8088 floor, which is the project's identity | nothing. The lesson is that dropping a tier is a legitimate move, recorded so it is a choice rather than drift |
| **OS/2 1.x** | one 286 protected-mode kernel; no 8086 at all | no matrix | os8088 runs on 128KB XTs | nothing |
| **Mac System 5-7** | one System and ROM; run-time Gestalt; 32-bit-clean as an app flag; RAM floors raised per release, so tiers were dropped rather than forked | per-app capability flags; floors stated per release | - | per-package capability flags (L3's bit) |
| **Unix System V (SVR3/SVR4)** | per-site kernels configured from a `master`/`system` file that selects object modules at link time; loadable drivers later | features are whole objects, so there is no `#ifdef` forest inside shared files | os8088 has no linker by design (`nasm -f bin`). Its feature-symbol block is the configuration file; arms are what replaces object selection | the discipline: build names only in the configuration (gkm-7) |
| **GEOS (8088)** | one real-mode kernel; geodes dynamically linked to libraries by entry table | modules portable across kernel builds | os8088's kernel modules near-call by offset, which is cheaper per call | nothing. The offset binding is right for a 128KB machine; its cost is per-kernel module sets (gkm-9) |

## Doc/code mismatches

New ones only. Already recorded elsewhere, and not repeated here:

- `kernel.asm:9` "no far code and no second code segment" (boot.md 2);
- SPEC.md §1 rule 2 (boot.md 42);
- CLAUDE.md:336 "no far code (§33)" (boot.md 43, engsys);
- SPEC.md §33 "Cold code is ordinary code, near called" (engsys-4);
- SPEC.md 2.6 "What it buys is KERN_CODE_MAX and nothing else" (bytes M2).

| # | file:line | current (exact) | the code | proposed |
|---|---|---|---|---|
| D1 | `kernel/kernel.asm:4-5` | "A Macintosh-style GUI for the 8086: 640x480x16 (VGA mode 12h), pre-emptive round-robin multitasking off the PIT, serial Microsoft mouse on COM1." | Three adapters (SPEC.md 39); `GFX_VGA` is kern_big's alone (`:1360`) and kern_small drives no VGA (`:1152`). The serial mouse contests COM1 and COM2; there is a PS/2 mouse and `OSAPI_MOUSE_FEED` drivers (`mouse.inc`) | "A Macintosh-style GUI for the 8086: one binary drives VGA/EGA (mode 12h), Hercules or CGA, picked at boot (SPEC.md 39); pre-emptive round-robin multitasking off the PIT; a serial mouse on COM1 or COM2, a PS/2 mouse, or a pointing driver (SPEC.md 9). Four kernels build from this file - kern_big, kern_small, kern_emu and kern_mega - and the split below says how." |
| D2 | `kernel/kernel.asm:148-150` | "and the KERN_BUDGET guard holds that whole span to a budget just above the BIOS data area (100KB of machine on kern_big, 64KB on kern_small)." | kern_mega's KERN_BUDGET is 68KB and binds MEGA_RESIDENT, the span less `.cold` and `.mega` (`:1455-1457`, rule 4) | "...(100KB of machine on kern_big and kern_emu, 64KB on kern_small; on kern_mega 68KB, held against MEGA_RESIDENT - the span less the rungs that go above 1MB, rule 4 below)." |
| D3 | `kernel/kernel.asm:171-172` | "KERN_BIG and KERN_SMALL are two builds off this one tree, and the knob is `make KERN_BIG=1`." | `#> [Knobs] KERN_SMALL=1 :: build kern_small instead of kern_big` (`Makefile:1207`); `ifneq ($(KERN_SMALL),) -DKERN_SMALL else -DKERN_BIG` (`:1511-1515`) | "KERN_BIG and KERN_SMALL are two builds off this one tree, and the knob is `make KERN_SMALL=1` (`make small`); with no knob the Makefile sends -DKERN_BIG." |
| D4 | `kernel/kernel.asm:1414`; `CLAUDE.md:386` and `AGENTS.md:380`; `CLAUDE.md:56` | "THE BUDGETS. Three rules, each a different quantity"; "**Memory is THREE rules and three quantities**"; "the three rules and their guards" | Four rules: rule 4, kern_mega's MEGA_RESIDENT, is listed in the same block (`kernel.asm:1430-1443`) and in the same CLAUDE.md bullet | "THE BUDGETS. Four rules, each a different quantity"; "**Memory is FOUR rules and four quantities**"; "the four rules and their guards" |
| D5 | `CLAUDE.md:386-387` (and `AGENTS.md` likewise); `docs/KERNEL-MEMORY.md:51-53` | "**kern_small fully RESIDES in 64KB** — every non-purgeable byte, no drivers, no XMS, on VGA"; "**no drivers loaded, no XMS, on VGA** (the adapter that does not drop a rung of its own...)" | kern_small drives no VGA (`kernel.asm:1152`; `GFX_VGA` only `%ifdef KERN_BIG`, `:1359-1361`), has no `.vga` rung, and its `.vgabuf` is 0 bytes (`build/smallk/kernel.map`). "On VGA" is kern_big's condition | CLAUDE.md: "...**kern_small fully RESIDES in 64KB** — every non-purgeable byte, no drivers, no XMS (on VGA for kern_big, the adapter that drops no rung; kern_small drives no VGA and has no adapter-dependent rung)". KERNEL-MEMORY.md: "**no drivers loaded, no XMS, and for kern_big on VGA** (the adapter that does not drop a rung of its own...; kern_small drives no VGA and has none to drop)" |
| D6 | `CLAUDE.md:396-397` (and `AGENTS.md:365-366`); `docs/KERNEL-MEMORY.md:62-63` | "budgeted is not RELEASED, and no release artefact carries its disks"; "a step towards a general release that is not one yet, so no release artefact carries its disks" | `mkzip.py:312-325` makes kern_mega's system, apps, XTMP3 and WIN16 disks REQUIRED items in the release zip (`1440K/286-PLUS/`, `1200K/286-PLUS/`); MEGA-RELEASE-PLAN §9, done 2026-11-07 | "budgeted is not yet RELEASED: no published release has carried its disks, though the release zip (`mkzip.py`'s `mega` group) now requires them, so the next release will" |
| D7 | `CLAUDE.md:402-403` (and `AGENTS.md:371-372`); `docs/KERNEL-MEMORY.md:120` | "`KERN_CODE_MAX` is separate and absolute: `.text` + `.bss` inside one 64KB window because offsets are 16 bits."; "the **segment** — `.text` + `.bss` in one 64KB window" | On kern_big the near-called VGA rung sits above `.text`, `.bss`, the FAT window and LOW, and its guard is "the binding form of KERN_CODE_MAX now" (`kernel.asm:8729-8736`): 6,946 bytes left, against 19,842 for `.text + .bss` | "`KERN_CODE_MAX` is separate and absolute: everything reached by a near call must sit inside KERNEL_SEG's 64KB window because offsets are 16 bits - `.text` + `.bss` on kern_small and kern_mega, and on kern_big everything up to the end of the VGA rung (`.text`, `.bss`, the FAT window, `.lowbss`, `.vga`), whose guard is the binding one." KERNEL-MEMORY.md row: "the **segment** — every near-reached byte in one 64KB window: `.text` + `.bss`, and on kern_big up to the VGA rung's end (`vga_end`)" |
| D8 | `kernel/driver.inc:2158-2161` | "AX = 0 exactly when no row of the class is loaded, because drv_load writes DRVR_SEG and DRVR_PARA together and a driver image is never under a kilobyte" | kern_big's HMA placement zeroes `DRVR_PARA` on a loaded row (`and word [bx+DRVR_PARA], 0 ; no heap held`, `:3933`), and kern_mega's `drv_hi_move` does the same, which is why kern_mega's arm counts rows (`:2198-2206`) | Append: "- EXCEPT where an image has left the heap: kern_mega's drv_hi_move and kern_big's HMA placement (`%1_hma`) both zero DRVR_PARA on a loaded row, so the KB sum is 0 for a class that is up. kern_mega counts rows for that reason (below); kern_big must too (drivers-3)." When gkm-1 lands, replace the paragraph with the row-count predicate's own reason |
| D9 | `SPEC.md:17314` (§9.11.7) | "**A third kernel off the one tree**, beside `kern_big` and `kern_small`" | Four kernels build from the tree: kern_mega is rule 4 (`kernel.asm:230-248`, `:1430`) | "**A third kernel off the one tree**, beside `kern_big` and `kern_small` (kern_mega, SPEC.md 20.18, is the fourth)" |

## Open questions

1. **kern_small's ROM.** Is it a product the owner wants to keep? It is the
   only thing kern_small's `.cold` buys, and it decides gkm-8 (-540 bytes on
   every 128KB machine).
2. **kern_emu's per-yield pump.** Has the drag latency it exists to avoid
   been measured against USBMOUSE's worker shape? If not, gkm-6 can be tried
   with one prototype driver and the `vmmouse` row, before any kernel arm is
   removed.
3. **A one-disk 8088/286 product (gkm-9).** Is it a goal, or are separate
   `286-PLUS/` disks the intended shape for good? The answer decides whether
   L3 is a size and cleanliness row or a product row.
4. **Should `test-full` boot kern_mega once** (a Bochs desktop boot, if one
   fits the tier's budget)? Today a major merge can break kern_mega's boot
   with no tier noticing. MEGA-RELEASE-PLAN §9 leaves this to "the scoped
   `mega-*` union" of a release.

## Verification

Adversarial pass, 2026-11-08, against the current tree. Every finding was
re-read at its cited code; section sizes were re-read from
`build/smallk/kernel.map`, `build/megak/kernel.map` and `tools/kernsize.py`,
and the row counts re-derived from `suite.rows()`. None of the findings is
already listed in `docs/plans/ABI-BREAK-LIST.md`, `KERNEL-BYTE-QUEUE.md` or
MEGA-RELEASE-PLAN 4.2.

### Findings

- **gkm-1 - CORRECTED.** drivers-3 holds: the row count is kern_mega-only
  (`driver.inc` 2170-2209), and `%1_hma` (3933) zeroes `DRVR_PARA` under
  `HMA_DRV`, which `kernel.asm` 1176-1183 defines on kern_big and kern_emu
  but not on kern_mega. hiber-2 holds too, but the evidence is wrong:
  `.badv` is **not** assembled on kern_big. The `%ifdef OS88_IMG` at
  `hiber.inc` 3404 wraps only `.bimg`. `.b9` through `.nox`, `.badv`
  included, sit inside the `%ifdef KERN_MEGA` that opens at `.b6`. So the
  unconditional fix has to assemble `.badv` on kern_big as well as re-point
  `HBM_BADV`: about 20 module bytes in HIBER.DRV, 0 resident. The row count
  costs +5 (push/xor/inc/pop of CX, and `cmp cx,1` for `cmp ax,1`), not +6.
- **gkm-2 - CONFIRMED.** The guard is at `fsx.inc` 1034-1042 as cited.
  `OSAPI_XREFUSE` takes no argument and serves four cells. `t_api_abi.py`
  457 opens `build/kernel.bin` only. The video-1 cost should read +13 (from
  video.json's correction), not ~+6.
- **gkm-3 - CORRECTED.** The counts hold within attribution noise. Nothing
  boots kern_mega in test-full: its 14 rows include `megahigh`, which is a
  host check. But no `box86port.txt` line runs kern_small today. The
  `@OS88_BUILD=build/smallk` form appears only in the file's header comment
  (lines 6-7), and none of the 45 lines uses it. A smallport lane also needs
  `_ports` to stop dropping `marty` (`suite.py` 21775-21777), not only a new
  list.
- **gkm-4 - CONFIRMED.** The self-aligning precedent assembles today
  (`vidext.inc` 103, `font.inc` 620). Some pad line numbers have drifted by
  a few lines.
- **gkm-5 - CONFIRMED.** Every section length, the +5,440 against 5,120,
  `sched.inc` 177, `DSK_FAT_SECS` 2/9 and the 14 `vgarefs.txt` doors all
  check.
- **gkm-6 - CONFIRMED** as a priced option, on a spot-check. Not proved: a
  worker rotation pumping a drag's spin loop as promptly as `vmm_poll`
  inside `task_yield` does (SPEC.md 9.11.3's freeze argument), and the
  +~40 estimate, since `DRV_MAX` goes from 6 to 7. The prototype-first step
  covers both.
- **gkm-7 - CONFIRMED.** Re-counted at 638, 126, 406 and 21 conditional
  lines.
- **gkm-8 - CONFIRMED.** Re-counted at 380 `call COLD_SEG:` and 335
  `call KERNEL_SEG:` sites; 54,002 checks.
- **gkm-9 - CONFIRMED** (spot-check). The 115,872 and 56,607-byte figures
  check, and `MOD_STAMP` is a function of the section sizes.
- **gkm-10 - CORRECTED.** The bytes check (452, 214 of them in `.vga`). But
  LAST-DROP-PERF 7.1 already prices every knob for kern_big and gives
  "why it is not on yet / what would flip it" per row. What is left is the
  owner's decision, not a new record.
- **gkm-11 - CONFIRMED.** kernsize prints "19,842 left" for the segment and
  "6,946 of the 64KB window left". KERNEL-MEMORY.md 1121-1124 and SPEC.md
  4028-4029 already say so; only the CLAUDE.md/AGENTS.md short form did not.

### Doc/code mismatches

- **D1, D3 - CONFIRMED.** The texts were refined and spread over the
  comment lines.
- **D2 - CONFIRMED.** The fix adds kern_emu beside kern_big, and kern_mega's
  68KB against `MEGA_RESIDENT`.
- **D4 - CORRECTED.** docs/KERNEL-MEMORY.md frames the budgets on purpose as
  "THREE sentences, THREE quantities (and kern_mega's own)". So only the
  `kernel.asm` header changes, to "Three rules ... and kern_mega's own
  (rule 4, below)". CLAUDE.md/AGENTS.md's "THREE rules", read with their
  "kern_mega is rule 4" a line later, are left alone.
- **D5 - REFUTED.** "on VGA" names the costliest configuration a rule is
  stated against. For kern_small it is vacuous, not false, and the owner's
  instruction file is not edited for that.
- **D6 - CONFIRMED.** The owner answered "Released" on 2026-11-07, and
  `mkzip.py`'s `mega` group requires the disks. Fixed in CLAUDE.md,
  AGENTS.md and docs/KERNEL-MEMORY.md. The same stale sentence in
  `kernel.asm`'s rule 4 block ("a release that is not one yet") was found
  and fixed alongside.
- **D7 - CORRECTED.** `KERN_CODE_MAX`'s own guard really is `.text` +
  `.bss`, so the definition stays. CLAUDE.md/AGENTS.md gain a clause saying
  that on kern_big the VGA rung's guard binds first. KERNEL-MEMORY.md 120 is
  the guard's definition and 1121-1124 already gives the window, so it is
  left alone.
- **D8 - CONFIRMED.** The header gains a note that the predicate fails
  where an image has left the heap, on kern_big's `%1_hma` as on kern_mega.
  The code fix itself is drivers-3's.
- **D9 - REFUTED.** kern_emu was the third kernel, and that is a true
  historical statement in its own section.
