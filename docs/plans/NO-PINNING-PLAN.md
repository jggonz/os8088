# NO-PINNING-PLAN - every heap claim moves, unless it has a reason

**Status: OPEN. Wave 0 is BUILT** (the C SDK's eager overlay carve, below).
Branch `no-pinning`, cut from `elendilon`.

## 0. Why

The heap compacts (SPEC.md 66). A claim that cannot move is a WALL: free
memory either side of it cannot be joined, so the machine reports 400 KB free
and refuses a 300 KB picture. Each pin is cheap for whoever adds it, and the
cost lands on the user much later. That user cannot see a pin and cannot fix
one.

**The policy (the owner's).** A claim may be pinned for one of three reasons
and no other:

| bucket | reason | shape |
|---|---|---|
| **M** | it is an on-demand kernel MODULE (`MEM_K_MOD`, SPEC.md 2.8) | static, and temporary. Converting modules is the likely next piece of work |
| **D** | a device is armed to write into it, or is writing into it now: DMA, a bus master, an `int 13h` transfer, an ISR filling a ring | momentary: it lasts while armed |
| **C** | the program is executing in it, or using it, RIGHT NOW: a frame stands in it, a worker is mid-loop in it, the kernel holds a pointer into it across one call | momentary: it lasts while in use |

"Someone did segment arithmetic and did not make it survive a move" is not a
reason. The fix for that is a relocation proc. For almost every claim the
proc is a `ret` or one store (SPEC.md 66.3, author rule 2).

**Other candidate reasons, judged.** Only one comes close:

- *A sheddable cache with no proc.* The room comes back by SHEDDING it, so it
  is never a permanent wall. But a shed throws away contents that a move would
  keep (seconds of `int 13h` to rebuild), and a claimant with a FLOOR cannot
  shed at all (SPEC.md 50.6.6). **Verdict: not a reason.** Give each cache a
  proc (item K6). Shedding stays as the fallback.
- *The address is visible to hardware other than a DMA engine* (GDTR/IDTR on
  kern_mega). This is bucket D or nothing. A proc that reloads the
  descriptor-table registers is a few instructions. **Not a reason.**
- *The build has no compactor* (`kern_small`; `NOCOMPACT`). `NOCOMPACT` is an
  A/B knob, so it is fine. **kern_small is SETTLED (the owner, 2026-10-23):
  out of scope.** Nothing there compacts, so movable or pinned makes no
  difference. On a 128 KB machine the user is the memory manager: they can
  open one large thing or a few small ones, there are no driver interactions
  they cannot see, and what is open on the screen is what is claimed. *"You
  can run this at all"* is bought with *"you manage your own free RAM"*. Every
  kernel proc below is inside `%ifdef OS88_COMPACT`, so it costs kern_small
  nothing.

**Two more owner constraints bind every item below:**

- **RealDMG and the Video Player may not lose performance.** They still
  comply. The shape that makes that possible is a relocation proc that does
  ALL the work at MOVE time: it patches the state block, the bank table and
  the ring pointers once, when the compactor calls it. The hot loop is not
  touched, keeps its cached segments, and pays nothing per frame. The
  alternative, *"re-read your base at each use"* (SPEC.md 66.3 rule 2), is
  REFUSED for these two, because it adds a load inside the loop. Each one's
  gate is a measured frame or emulated-cycle rate before and after on
  MartyPC, and the after must not be lower.
- **Kernel bytes stay minimal.** Each kernel item is costed in 2.2, and any
  that is large may wait for a size pass.

## 1. Wave 0 - BUILT: the C SDK's eager overlay carve (what LEMMINGS pinned)

**What happened.** LEMMINGS' wave 4 (SPEC.md 116.6) found a real defect. A C
module function calling ANOTHER module function compiled to
`call far [cc_ovm_*]`. That pushes the module's own CS, and the callee's plain
`retf` pops it. A claim under that call which compacted would move the module
and leave the CS on the stack stale. `ovl_launch` -> `ovl_level_load` ->
`ovl_claim_all` claims ~329 KB three frames deep. The fix that shipped was to
PIN the eager carve on every C package (CWORD, RUNCPM, LEMMINGS), for the
program's whole life. The lazy arm and the `<NAME>.OVL` sidecar kept the
exposure and were left as an open item.

**The fix (this branch).** The pin is gone, and the call is fixed instead:

- `tools/cc8086.py` emits a module-to-module call as `mov bx, cc_ovm_<f>` /
  `call far [cc_ovvmm]`.
- `apps/cc/crt0.asm` adds `cc_ovmthunk`, which works like `cc_ovthunk`. It
  pops the 4-byte return, puts the offset in the same stash, and far-calls the
  callee from RESIDENT code. The region's CS is pinned by `mem_in_nest` while
  a callback stands, which is bucket C. The thunk then rebuilds the `retf`
  from `[cc_ovcs]`, and `cc_ovbind` re-stamps that on every move. The callee's
  frame is still four bytes above its arguments, so no compiled offset
  changes.
- `CC_OVDEPTH` goes from 16 to 32 words, because the stash now carries
  module-to-module nesting too. When the stash is full, `cc_ovmthunk` falls
  back to the direct far call, which is the old behaviour. The depth is
  therefore a safety margin, not a limit on correctness.
- `cc_ovinit` declares the eager carve movable again through `cc_ovcarve`
  when the carve holds the module alone (`OS88_PART_N` = 1): CWORD, RUNCPM,
  LEMMINGS.
- The same thunk closes the lazy arm's and the sidecar's exposure.
- `tests/ovlhigh.py` assertion 4 is flipped back: `MC_RLOC` != 0.
- `tools/ccpeep.py` treats `call far [cc_ovvmm]` as READING BX. **The first
  build broke without this**, and silently: the peephole assumes no call reads
  a register, so it deleted the `mov bx` as dead. `tests/covl` then answered
  0 where it should answer 1234. The only reason anyone saw it was a by-hand
  A/B on MartyPC, because **no suite row runs covl**. W0-T closes that gap.

**Cost.** Per module-to-module call site: 7 bytes where it was 4. Resident:
the thunk (~30 bytes) plus 32 bytes of stash. Per call: ~40 us on a
4.77 MHz 8088.

**W0-T is BUILT: `tests/ovlmove.py`.** The module moved 5340 -> 2100 under
`ovl_mv_outer -> ovl_mv_inner -> cv_claim`, and the chain answered 1008. The
row's own lesson: the first red run (the old direct call put back) PASSED,
because the claim made under the chain is never written, so the module's old
bytes were still where the stale return landed and the old copy ran
correctly. The row now poisons the claim with HLT before it is freed, and the
red run dies. *(What follows is the original note.)* **Still to gate (W0-T).** A row that really compacts the carve UNDER a live
module-to-module frame, then returns through it. `tests/covl` already makes
module-to-module calls (`ovl_mix` -> `ovl_inner`) but no row runs it, and
nothing forces a move at that moment. Shape: cmemmove.py's recipe (heapfrag
owns the floor, the test package lands above it, heapfrag exits). The test
package makes a claim from inside `ovl_outer -> ovl_inner` that needs the
pass, then asserts three things: the carve's base changed, `ovl_outer`'s
answer is right, and the window is still alive. Then break it on purpose:
make cc8086 emit the old direct call, and the row must go red.

## 2. The inventory

Taken across the whole tree on this branch, checked against code. Each row
gives a bucket and **what retires it**. ⚑ = the reason is a derived pointer or
segment arithmetic (the case the policy refuses outright).

### 2.1 Kernel rules in `mem_can_move` (kernel/memory.inc)

| rule | bucket | verdict / work |
|---|---|---|
| `MC_RLOC` = 0 | - | the default. Every row below is this |
| `MEM_K_MOD` | M | allowed. Next piece of work (modules become movable) |
| `mem_in_nest`, `[ld_base]`, `mem_in_xfer`, worker running and not parked | C/D | allowed, all momentary |
| **ANY `[drv_wcnt]` != 0 pins EVERY driver image** | D, too broad | **K1**: count workers per image. One driver's service task should not freeze the others |
| **ANY `[drv_wcnt]` != 0 pins EVERY `MC_DMA` head** | D, too broad | **K1**: a per-claim "a chip is armed here" bit, set by the arming driver |
| region with a worker and no restart point | NONE | only FROTZ is hit (2.4) |
| kern_small: no compactor at all | - | **settled, out of scope** (0) |
| *(a gap, not a pin)* a compaction run by a task other than the UI task | traced, 2026-10-23 | **K8, ANSWERED.** The allocator checks no caller. Two non-UI paths reach it. (1) **A dying package's worker**: `OSAPI_TASK_ALIVE` → `inst_task_die` (instance.inc) takes `gfx_lock` → `inst_unwin` → `wm_destroy`'s repaint → `wm_su_bank` → `wm_su_take` → `mem_claim` → `mem_compact`, on the worker's stack, far-calling the surviving windows' `W_PAINT` there too. (2) **Fractal's worker** calls `OSAPI_MEM_REGROW` under the gfx lock; regrow never compacts. **What makes both safe today is the GFX LOCK, by accident.** Callbacks run holding it, and both paths take it first, so neither can run while the UI task is suspended inside a package. `menu_drop` holds it across its `task_yield` loop, which is why no worker can claim while a menu is down. **SPEC.md's menu sentence (*"a package worker can claim while a menu is down"*) is STALE**, and rule 7 holds in practice. Proposed: (a) correct the sentence; (b) write the gfx-lock reason into SPEC.md 66.3 as the binding one, instead of rule 7's *"None of this is enforced"*; (c) have `inst_task_die` not BANK raise caches during its repaint, so the dying worker never reaches `mem_claim`. That is the last task path, and it costs ~6 bytes (a flag tested in `wm_su_bank`). Then a non-UI compaction cannot happen at all, and no momentary pin for it is needed |

### 2.2 Kernel claims - checked against the code, and COSTED

Re-read claim by claim on this branch; that corrected the first inventory in
three places. `MEM_P_ICO`, `MEM_P_DIRW`, `MEM_P_MSAVE`, `MEM_K_CLIP`,
`MEM_K_SC` and the persistent `MEM_K_ASC` cache already declare a proc, and
`MEM_K_DRV` moves under SPEC.md 66.6.3. The comment on `MEM_P_FATW` claims
`mem_can_move` refuses any purgeable cache. That stopped being true: a cache
with a declared proc moves now (`mem_can_move`'s `.notpg` note).

**The prices.** A kernel proc follows `dsk_rah_reloc`'s or `ico_reloc`'s
shape: `cmp bx, [w] / jne / mov [w], dx / ret`, 11 bytes. The call site that
declares it, `mov ax, proc / call mem_movable_x`, is 6 bytes (8-10 through
`COLD_SEG`). So **a one-word claim is ~17 bytes, all on kern_big, behind
`OS88_COMPACT`**. These are estimates in that measured shape. Each item's own
measured cost goes in when it is built.

**A FINDING THAT SAVES BYTES. Top-down claims made at BOOT and never freed
are not walls.** They ARE the ceiling: everything claimed later lands below
them, and nothing above them is ever freed to open a hole. Pinning them costs
the arena nothing. That puts `MEM_K_FONT` and `MEM_K_MTBL` (82 KB) at
**0 bytes, no work**. Each stays listed with that reason, and the ratchet (4)
must accept the reason only when the claim is (a) top-down, (b) made before
the first desktop frame, and (c) never freed.

| claim | life / door | bucket | work | kernel bytes (est.) |
|---|---|---|---|---|
| `MEM_K_MOD` | on demand | M | the module conversion, next | - |
| `MEM_K_FONT` 1 KB | boot, top-down, never freed | ceiling | none | **0** |
| `MEM_K_MTBL` 82 KB (kern_mega) | boot, top-down, never freed | ceiling | none, while it stays boot-only | **0** |
| `MEM_K_BAND` | `BAND=1` knob only | - | none on a shipped kernel | **0 shipped** |
| `MEM_K_ASC` flush, `MEM_K_SC` compose, `MEM_K_CLONE`, `MEM_K_HIB`, `MEM_K_CMPR` | one call or operation | C/D | none: the kernel is using it for that call | **0** |
| **K1a** `[drv_wcnt]` pins every `MC_DMA` head | - | D, too broad | **delete the check**. `SOUND.DRV` pins its own ring while a double-buffered stream is open (`OSAPI_MEM_MOVABLE` with AX = 0) and re-declares it when the stream closes, which is what its DIRECT stream does already (sb.inc's pool). The kernel then has nothing to ask | **-12** (kernel), +~12 in `SOUND.DRV` |
| **K1b** `[drv_wcnt]` pins every driver IMAGE | - | D, too broad | a per-row worker count in `drv_tab`. Spawn and exit bump the row of the image the task runs in, and `.drvimg` tests that row. Only `SOUND.DRV` spawns a task (`OSAPI_DRV_TASK` has one caller), so this is worth doing only when a second driver does. **Defer** | ~45 + `DRV_MAX` `.bss` |
| **K2** `MEM_K_IMG` 1 KB per mounted `.img` | mount, mid-session, top-down: **a real wall** | NONE ⚑ | re-read `IMG_DVSEG` in `dsk_xfer`'s `.img` arm after the nested transfer, plus a proc that walks the image rows for the old base | ~40 |
| **K4a** `MEM_P_RAHB` 5 KB (kern_mega) | mount | NONE | one-word proc on `[dsk_rah_bnc]`, `dsk_rah_reloc`'s shape | ~17 |
| **K4b** `MEM_K_COPY` | one Cut/Copy/Paste, including a suspended overwrite question | C, but held across user waits | one-word proc. The transfer itself is pinned by `mem_in_xfer` already | ~17 |
| ~~K6a~~ `MEM_P_WSAVE` raise caches | bottom-up, one per covered window | shed by design | **DEFERRED** (6.2): the compactor drops them, which is what serves a big request on a full machine | 0 |
| **K6b** `MEM_P_FATW` FAT windows | bottom-up per volume | NONE (shed at MED) | a proc for `dsk_fatwc` AND `[dsk_fatseg]`, the second naming word. Fix the stale comment too | ~25 |
| **K6c** `MEM_P_FLIST` multi-file clipboard, 1 KB | until the next Cut/Copy | NONE | one-word proc on `[fcp_lseg]` | ~17 |
| ~~`MEM_P_VIEW`~~, ~~`MEM_K_SAVE`~~, ~~`MEM_K_CLIP` small~~ | kern_small | - | settled, out of scope | 0 |

**(Superseded by 6.4: K4a, K6b and K6c are purgeable and go with K6a's research pass; the size pass is K4b alone.)** **Kernel total if every row is taken: ~138 bytes** (K2 + K4a/b + K6a/b/c,
less K1a's 12), plus K1b's ~45 when it is wanted. Proposed: **take K1a, K2
and K6a now**, ~50 bytes net: the two that open real walls and the one that
pays for itself. Leave K4/K6b/K6c (~76) for after the next size pass.

### 2.3 Drivers

| claim | bucket | retires it |
|---|---|---|
| `HDDTOOL.DRV`, `RAMPAGE.DRV`, `HTTPPAGE.DRV` page images (a base that is a CS, `[rd_pfar]`-style far pointers) | C | **R1 CLOSED, no code**: each image is loaded when its Control Panel page is selected and dropped at `DSV_CPCLOSE` (`rd_page_closed` -> `rd_page_drop`), so it is held only while the user is IN the panel, using it - bucket C. It is claimed top-down, so it is not a wall in the arena while held |
| HTTPFS index (hfidx.inc), 16 KB, life of the mount | - | **R2 BUILT**: `hf_reloc` (one word, `[hf_iseg]`), declared once the fetch is validated (it is the network's write target until then); `hf_list` re-reads ES after `OSAPI_FS_ENT`, the one call inside a verb that can claim. `httpmount` regression-green on QEMU. **No move proof**: QEMU cannot post a compaction from outside the way MartyPC can, and MartyPC has no NIC |
| VOLUME save-under, 2-30 KB while the popup is up | C | allowed; a `ret` proc is free (R2) |
| Sound Blaster ring / direct pool | D | allowed (narrowed by K1) |
| install/mount/MBR bounce buffers, xmem HMA, RAM-disk write buffer | C/D | allowed |

### 2.4 Package regions and workers (`tests/movable.txt`)

| line | bucket | retires it |
|---|---|---|
| csload, wdload, scload, dosload, pxstein: launch images | C (`[ld_base]`) | correct as they stand |
| **SHEET region** | - | **P1 BUILT, smaller than planned.** The binds stamp CS into vector tables in SHEET's OWN data, so a region proc that re-runs them (`sh_rgnreloc`, 6 bytes) is the whole fix, with no part conversion needed. The "far frame across file calls" reason never bound the region (callbacks pin it). `sheetmove` 3c, red-tested. **Still pinned: the two MODULE claims** (CHART.OVL 52 KB, MACRO.OVL), whose own CS is on the stack across their file calls - the C overlay's shape. That is P1b: a thunk like `cc_ovmthunk`, or making each a part |
| **FROTZ worker** | NONE | **P2, ANALYSED - a decision.** The story DATA already moves across the park (`zi_yield` banks `[zf_sdelta]` and re-bases a saved ES by range, SPEC.md 66.5.9). What pins the REGION is the worker's own stack while parked: the far return frame of `call OSAPI_TASK_ALIVE` holds Frotz's CS, and `zi_yield` is reached from every wait loop - mid-opcode, several near frames deep in `zx_run`. Two ways out: **(a)** a restart point, which needs every opcode that can wait (`read`, `read_char`, timed input, `@sound`) to be safely RE-EXECUTABLE from its start, because the restart discards the stack. That is an interpreter audit, Frotz only. **(b)** A kernel *park-frame relocation*: a worker parked at `OSAPI_TASK_ALIVE` whose package declares that NOTHING on its stack names its region except the frame ALIVE itself built (near calls push IP only) gets that one frame patched by `mem_region_reloc`. This is general (any deep worker that parks only at ALIVE), but it is resident kernel bytes, estimated 40-60 (unmeasured), and the declaration is unverifiable, so a false one is silent corruption. Recommended: (a) if the audit is clean, else (b) after a size pass. **DECIDED (the owner, 2026-10-23): NO kernel bytes. Do the audit (a); if it does not come out clean, Frotz's worker stays pinned for this round** |

### 2.5 Assembly packages - undeclared data claims

Unless marked, each fix is "relocation proc is a `ret` or one store; re-read
the base at each use". The ⚑ rows are the ones that need real work.

| package | claims | work |
|---|---|---|
| **REALDMG** ⚑ | ROM, SRAM, JIT, profile, bank table, logs | **A1, PERFORMANCE-BOUND**: one proc does all the work at move time. It patches the core state block, the bank table and any JIT code that has a segment baked in. Then it **forces a resync on return**: the emulation loop re-reads its cached segments at the next slice boundary, where it already yields, never inside an instruction. Zero instructions are added to the dispatch loop. Gate: emulated cycles per host second on MartyPC with a cartridge running, before and after, and the after must not be lower. A JIT block holding an absolute segment is the case to audit first: if one exists, the proc relocates it or flushes the JIT, and is never allowed to re-read per instruction |
| **WORD**, **SCRIBE** | doc, CHP, PAP, undo, italic, pictures | **A2 BUILT** (merged from its agent branch): every long-lived claim moves; staging and scratch stay pinned as bucket C; `wdheapmove`, `wdheapmove-sc` |
| **BROWSER** | up to 63 KB + 8 + 6 | A3 |
| **TEXPAD** | source buffer (`[tp_srcseg]`) | **A3 BUILT**: `tp_srcreloc`, pinned only across its own file read/write. `tests/pkgmove.py` is now the GENERIC gate for this table's rows: `--first` opens a package whose claims become a hole under the target's |
| **PAINT** ⚑ | GIF staging (C), LZW 16 KB (`[pt_gbase]` derived) | A3 |
| **FROTZ** | scrollback 24 KB (outside `zf_reloc`) | A3 |
| **CLEAR SKIES** ⚑, **TANK**, **PIXELSTEIN**, **ISOTRON**, **1942** | shadows/canvases 16-64 KB | **A4**: in use only inside the fsx bracket, so they are bucket C there. Outside it, either free them or declare them. CS keeps ES from `[cs_tseg]` across spans, so its proc must also be safe at the park points |
| **EXCITEBIKE** ⚑ | compiled poses are far-called IN the claim (a CS) | **A5**: a proc that re-stamps the pose vectors |
| **VIDEO** ⚑ | ring, "page-safe and never moved" | **A6, PERFORMANCE-BOUND**: the ring is bucket D/C only WHILE A PLAY RUNS (the reader, the hook and a card may all be writing it). Declare it movable when the play ENDS, and pin it again (`OSAPI_MEM_MOVABLE` with AX = 0) when the next play STARTS. Both happen outside the frame loop, so the play path gains nothing. A move between plays is one proc storing the ring's segment. Gate: the Video Player's frame rate on its demo videos, before and after. **The ring was already that shape** (SPEC.md 98.3.19.2: `vp_smov` around every bracket); **the rest of the session joined it 2026-10-24** - the resident key record, the layer's own claim, VPDIAG's profile, and a paused bank's chunk pinned for its read - at +46 bytes of image and no instruction a frame |
| **MIDIRACK**, **TRACKER**, **AUDIO**, **WIREFLEET**, **1942** PCM rings | D while armed, NONE idle | A6. **BUILT for the four** (7, *A6 audio*): each long-held claim movable while idle and pinned from a play's start to its stop. 1942's ring was already claimed and freed inside its bracket (A4) |
| **THE WIRE**, **FTPD**, **CHART**, **PIXEL**, **TELNET**, **RECORDER**, DeskClock, DotDel, StikNote | assorted | A3; **THE WIRE and PIXEL BUILT in a follow-up** (7: SPEC.md 92.5.1, 106.5.1) |
| `os88type.inc` face cache 8 KB | - | **A7 BUILT**: `ty_reloc` (faces and the pre-shifted table), every includer at once; `tymove`, red-tested |
| `os88parts.inc` carve ⚑ (op_claim) | "every part's segment is arithmetic on this base" | **A8**: declare from the library with a proc that stores `op_base`. A package that banks a part segment supplies a hook (as C2) |
| lazy `op_fetch` claims | declared only by DrMario, Excitebike, Pixel | A8 |
| **DOS** `dos_hk_claim` | undeclared | A3 |

### 2.6 C SDK and C packages

| item | work |
|---|---|
| eager carve, one row | **BUILT** (wave 0) |
| **C2**: eager carve, more rows (C64, APPLE2: ROM + overlay) | a C hook (`os88_onparts(was, now)`, or route through `os88_onmove` with the carve's base) so a program that banked `os88_part_seg()` is told. Then declare unconditionally |
| `os88_mem_claim` is born pinned; only CWORD declares (undo, 9 KB) | **C3**, per package below |
| **LEMMINGS** - mask 32, bank 96, main 60, rec 16, T 125 (VGA) / world + shadow; **held across a suspended level, so up to ~330 KB pinned on the DESKTOP** | **C4, first after wave 0**: `CC_HAS_ONMOVE` re-pointing the seven `lem_cl_*` words. First check that the asm rasters (`.inc`) load the segments from those words at each frame, not once per level |
| **C64** RAM 64 KB, **APPLE2** RAM 49 KB + 53 KB FSX, **RUNCPM** Z80 64 KB | C3: `os88_onmove` re-pointing `ramseg` / `rc_zseg`. Confirm the core reloads the segment at the top of each burst |
| **WEAVE** ⚑ bundle, VM, canvas, grid; lazy part 0 with `w_cmseg` copied | C3 + A8 |
| **LOOM** ⚑ 29 / 62 / 50 KB project buffers; lazy preview part with `lm_pvseg` copied | C3 + A8 |

## 3. Order

Ranked by the free memory each recovers on a real session, then by cost.

1. ~~W0-T~~ BUILT (`ovlmove`). ~~K8~~ BUILT. **K3** (kern_mega's tables):
   DEFERRED as niche. It binds only on a 286 with under 192 KB of extended
   memory, after a boot driver is unloaded, and the cheap fix (claim before
   `drv_boot`) means reordering kern_mega's gate. **G1** (the cost of moving big
   claims, and sound underruns while it happens) is next, and it binds C3/C4.
2. **C4** LEMMINGS - the largest pin on the desktop, and the package this
   branch exists for.
3. **K1a, K2, K9** - BUILT (K8 +14, K1a -16, K2 +58, K9 +42 resident). K6a
   deferred (6.2). The rest of the kernel rows wait for a size pass.
4. **C3/C2** C emulators and Weave/Loom - 50-64 KB walls each.
5. **P1** SHEET chart overlay as a part - 52 KB + the region.
6. **A2** WORD/SCRIBE audit, **A1** REALDMG.
7. **K4, K6b, K6c** (~76 bytes) after a size pass, and **K1b** when a second
   driver spawns a task.
8. **A3-A8** the remaining package claims, one row each.
9. **R1/R2** drivers.
10. **P2** FROTZ worker - needs a proof, not just code.
11. ~~K7~~ kern_small - settled: the user manages its RAM (0).

## 4. The ratchet (after the work)

Add a FULL-tier row, as the owner asked, in the `t_movable` shape. It is
host-side, so cheap:

- Every `OSAPI_MEM_CLAIM*` / `mem_claim*` / `os88_mem_claim` site in
  `apps/`, `drivers/` and `kernel/` must either be followed by a declaration
  (`OSAPI_MEM_MOVABLE`, `os88_mem_movable`, a kernel proc) or appear in
  `tests/pins.txt` with its bucket (M/D/C) and a reason.
- Like `tests/movable.txt`, the list turns ONE way. A line whose claim now
  declares is a failure.
- A bucket-C line has to name the call or bracket it is confined to, and a
  bucket-D line the device. A line with neither is refused.
- A runtime half on MartyPC: boot, open the heavy packages, read `mem_tab`,
  and fail on any live claim with `MC_RLOC` = 0 that is not M, not in a
  transfer, and not in the nest. This catches pins the source scan cannot
  see.

### 4.1 BUILT: `tests/pins.txt`, `t_pins` and `pinsrt` (both FULL tier)

**The registry** is `tests/pins.txt`, one line per pinned claim:
`<bucket> <file> <key>  # <reason>`. The buckets are the three of 0 - **M**,
**D**, **C** - and four more: **B** a boot ceiling claim (2.2's finding:
top-down, before the first frame, never freed), **S** shed by design (6.2,
6.4; a kernel `MEM_P_*` claim needs no line at all), **X** another reason of
60 characters or more, and **N**, *no valid reason yet*. N is the backlog
and the gate prints its count, so it can only be seen going down.

**The host half, `tests/unit/t_pins.py`**, finds every claim site -
`OSAPI_MEM_CLAIM*`/`OSAPI_MEM_HUGE`, C's `os88_mem_claim`/`os88_mem_huge`,
the parts standard's `op_load` (a carve, when a part is eager and the package
is not a re-home loader) and `op_fetch`, a C package's `CC_HAS_PARTS` carve,
and the kernel's `mem_claim` family including its macro and trampoline
spellings (`FCPX mem_claim_dma_x`, `mov bp, mem_claim_hi_x` + `wm_cbd`) - and
asks whether a declaration reaches it.

**THE KEY IS (FILE, the WORD the claim's segment is stored to)** - a kernel
claim's TAG instead, a C lvalue (`func:var` for a local), a parts row
(`op:carve`, `op:PART`), or `@routine` when nothing names it. It was chosen
after reading a few dozen sites in asm and C, because it is the identity the
relocation procs already use: `np_reloc` compares BX with `[np_dseg]`,
`wd_rltab` and `rw_mtab` are tables of those words, `os88_onmove` re-points C
globals by name. A claim is DECLARED when a declaration has DX from that word,
or is reached from the claim with DX still holding it. Getting there honestly
took more than a grep, and every step was a false PIN found in the tree:
DX is followed through the routine's own branches (`jnc .got`), register
copies and `xchg` (`hf_tseg` -> `hf_iseg`, MIDIRack's `mov bp, dx` /
`mov dx, es`), `push`/`pop` (Paint's clipboard, MACRO.OVL's load), `op_seg`'s
AX for a lazy part, and the other arm of an `%if` is skipped; package-local
claim THUNKS (`vp_mclaim`, `trk_claimsel`, `rc_oft_claim` in C) move the site
to their callers and declaration thunks (`vp_mmovz`, `pt_movable`,
`fmv_movable`) count at theirs; table walks (`wd_rltab`, `rw_mtab`,
`lem_cltab[]`) declare every word in the table; a claim stored to two words
(`[vp_ring]` and `[vp_rclm]`) is declared if either is; and a parts row's
`op_movable` is matched to the fetch by its part number. Files no package
includes - PiXEL's separately assembled parts (`pxedit.asm`, `pxgif.asm`...)
- are scanned with their whole directory as the scope, which is how four
PiXEL pins that a per-package walk could not see came in.

**The trade-off, stated.** Word identity can give a FALSE GREEN in one shape:
a package that declares a word for one claim and stores a different,
undeclared claim to the same word elsewhere. Two guards narrow it - a
declaration whose proc comes from a REGISTER (`sc_m_mem`'s CX, which may be 0)
counts as no declaration, and a function-local C variable is declared only by
an `os88_mem_movable` BEFORE its next assignment (c64kbd's `seg` is Copy's
staging in one arm and Paste's queue in the next). It gives FALSE PINS where
DX comes from somewhere it cannot follow; those fail loudly in the diff and a
plainer spelling or a line fixes them. The runtime half is the backstop for
both.

**The runtime half, `tests/pinsrt.py`**, boots the shipped 360KB pair on
MartyPC, opens Paint, `PAPER.TEX` (TeXpad) and `BROWSER.HTM` (the Browser) at
a QUIET desktop (5 #11), and reads `mem_tab`: every record is movable,
purgeable, a kernel tag `pins.txt` names with a bucket that can be LIVE there
(M, B, X, N - a C or D claim still held is a call that did not give it back,
and a B claim must be top-down), a region `t_movable` answers for, or a
package claim its directory's long-lived (N, X, B) lines excuse. 17.4s.

**The red runs, all taken**: Dot Delirium's one declaration deleted (fails
naming `apps/dotdel/ddlay.inc dd_bdseg`), `assoc.inc`'s `mem_movable_x`
deleted (`kernel/assoc.inc MEM_K_ASC`), CWORD's `os88_mem_movable(cw_u_seg,
1)` made 0, a line for a claim that declares and one for a key that no longer
claims; a C line naming no call and a kernel pin with no HEAP-CLAIMS row
failed on the seeding itself. `pinsrt --red rloc` zeroes a Paint claim's
`MC_RLOC` before the read and `--red tag` retags one `MEM_K_ICO_UNUSED`; both
fail naming the owner.

**The seed**: 279 claim sites under 237 keys; 121 keys declared, 116 pinned,
3 of them purgeable kernel caches with no line. The registry: **C 67, D 10,
N 29, X 4, M 1, B 1, S 1.** Every N line is in `docs/HEAP-CLAIMS.md` too. **The
N lines are the owner's findings** - pinned today with no reason the rule
accepts:

- kernel: `MEM_K_COPY` (held across a suspended overwrite question - K4b),
  `MEM_K_MTBL` (K3, deferred as niche), and **a new one, the Sticky Apps
  panel's second save-under** (`[stm_sseg2]`, `stikmenu.inc`), held while the
  panel is open with no proc - and tagged `MEM_K_MOD` though it is no module;
- the A6 remainder - the session-long rings nobody declares between plays:
  ~~Audio's look-ahead and speaker rings, Tracker's speaker ring, WireFleet's
  PCM ring (claimed at ENTRY), MIDIRack's song, wavetable bank and mix
  claim~~ (BUILT, 7's *A6 audio*: seven lines gone), and four Video Player
  claims `vp_smtab` did not reach (`vp_ldseg`,
  `vp_lyseg`, `vp_prseg`, the session's key-record arm) - **CLOSED
  2026-10-24** (SPEC.md 98.3.19.2): three moved into `vp_smov`/`vp_smove`,
  the key-record arm found LEAKING its claim at every resident play from a
  key, and `vp_ldseg` a C line (`vp_rload` declares the block under
  `[vp_rblk]`); per-frame cost 5,623,594 -> 5,623,600 guest cycles over
  vidplay's clip, every play routine identical at an even shift;
  `tests/vidclmove.py`;
- ~~SHEET's 32KB staging~~ **BUILT (NO-PINNING 1, SPEC.md 81.2.1)**: movable,
  pinned only across each file call by `sh_stgpin` in the two resident stubs
  every one of them goes through; +41 image bytes; `sheetmove` 3b and
  `sheetmove-rt`;
- ~~The Wire's catalog and transfer claims~~ and ~~PiXEL's~~: RETIRED by the
  follow-up in 7 (The Wire's two and PiXEL's code parts and animation claim
  MOVE; PiXEL's five job-length work claims are bucket C with the job named);
- PIXELSTEIN's art, levels and sprites, behind 6.5's fence;
- ~~DOS's own parts carve~~ **BUILT (NO-PINNING 2, SPEC.md 96.29.1.2)**: it
  exists only in the `DOSTRACE` instrument build (the shipped `DOS.O88` has
  no carve at runtime - its loader's carve IS the region). It opts into
  `OP_ONMOVE` (`dos_trmove`), moves at the prompt and is pinned from the
  program's start to its `TRACE.LOG`; shipped arms byte-identical; `dostrmove`.

## 5. The adversarial review (2026-10-23) and what it changed

An independent reviewer was asked to find what the plan would BREAK, LEMMINGS'
trap run the other way: making things movable in a way that breaks another
system silently. Each finding and what was done about it:

| # | finding | disposition |
|---|---|---|
| 1 | **Wave 0 was unsafe without K8.** An `OSAPI_WM_ONWAKE` handler runs with NO gfx lock (`ui.inc` `.wake`), so the UI task can be pre-empted inside an overlay module with the lock free. A dying worker could then compact the carve under it. The "callbacks hold the lock" argument in 2.1 and SPEC.md 66.3.1 was false | **K8 landed in the same series** (`0cd9f0d9`), so `no-pinning` has never had wave 0 without the `sch_cur` gate. SPEC.md 66.3.1 now names the GATE as the reason and says the lock never was. The menu paragraph is corrected again: a worker CAN claim while a menu is down (driver verbs, rule-7 violators), but since K8 only first fit |
| 2 | **A driver's data claim can move under a PACKAGE worker inside a driver verb.** ETHER.DRV's socket pool is declared movable (`tcp.inc`), and its verbs run on the CALLER's worker (FTPD, Telnet, Browser). `inst_seg_parked`'s driver arm counts only `TF_SERVICE` tasks. A pre-empted worker mid-`rep movsb` with ES = `[sk_seg]` comes back writing into whatever block took the pool's place. **Pre-existing, live** | **K9, new, needs a decision (6.1).** Worse than reported: `wm_pkgs`, the nest, is ONE global stack, and a worker's `OSAPI_DRV_CALL` pushes onto it from its own task, so two tasks interleave on it. An interleaved pop can leave the table naming the wrong image, which puts both the driver image and a package region at risk |
| 3 | K8 turns every non-UI claim into first fit, and `OSAPI_MEM_AVAIL` still plans both passes, so a worker that asks and then claims (Fractal) is promised room it cannot get | **Accepted as the honest answer.** The only asker-then-claimer on a worker is Fractal, and its refusal is a smaller cache. **To do (K8b):** an instrument row counting non-UI arrivals at `mem_claim_x`'s failure arm across the soak, before anything else relies on it |
| 4 | `cc_ovthunk` wrote its stash unchecked, and module-to-module calls now share that stash (Weave/Loom's FX parser takes ~5 entries a paren level) | **Fixed**: a full stash REFUSES the call (AX = 0, the call skipped: SPEC.md 47's refusal shape, `cc_ovneed`'s). `cc_ovmthunk` falls back to the direct call as before |
| 5 | **C4 is not seven words.** LEMMINGS derives `lem_anseg`, `lem_mkseg` and `lem_ptseg` and `lem_far_seg` (base + offset), `lem_m_apply` WRITES through one, and per-claim declaration would compact claims 1..N-1 while N is being claimed and derived | **C4 re-scoped**: the proc RANGE-translates every derived word (in [was, was + size) → add now - was), and is declared once, after the whole of `ovl_claim_all` |
| 6 | **The ceiling argument is false for `MEM_K_MTBL`**: `mega_enter` runs AFTER `drv_boot`, so boot driver images sit above the 82 KB tables, and unloading one leaves a hole over the wall | **K3 back in**: either claim MTBL before `drv_boot` (~0 bytes if the order allows it) or give it a proc that reloads GDTR/IDTR/LDT. `MEM_K_FONT` stays at 0 (claimed before `drv_boot`) |
| 7 | **Moving big claims costs time, with task switching paused**: ~2.86 ms/KB, so a suspended Lemmings set (~330 KB) is ~1 s, and a 64 KB emulator RAM ~180 ms. A double-buffered stream's refill task cannot run in that window, and Tracker's mixer parks | **New gate (G1), required before C3/C4 land**: Lemmings suspended + Tracker playing, then open a large package. Measure the compaction's wall time and underruns on MartyPC. If it is audible, the answer is a per-claim cost cap in the plan, not a pin |
| 8 | K1a: the deleted rule also pinned PACKAGE `MC_DMA` claims, and `sb.inc` documented relying on it for external rings | No package ring is declared movable today, so nothing broke. **Fixed the documentation**: `SND_OPENF_EXT` in `os88api.inc` and `sb.inc`'s note now say the package keeps its ring still until the stream is CLOSED (a pause keeps it armed). A6 is bound by that. The "SOUND.DRV pins its ring while a double-buffered stream is open" shape is WITHDRAWN: it was never needed (the double buffer is never declared) and would have been a wall |
| 9 | K2 traced correct; the mount's own pin before the declaration is redundant; every `clo_*` exit must clear `[mem_pinimg]` | Redundant pin **removed**. Exits audited: every refusal after the claim goes through `clo_mfree` |
| 10 | K6a turns a free DROP into a COPY (up to ~64 KB on VGA) | **K6a held for the owner (6.2)** |
| 11 | The ratchet's runtime half would fail on legitimate D and C pins during play | The runtime half reads only at a QUIET desktop (no stream, no transfer, no fsx), and names the D/C pins it may meet |

Checked and SOUND, per the reviewer: kern_mega's code selector after a move
(`cc_ovbind` recomputes `[cc_ovcs]`), no `ovl_*` function can become a
callback (`cc8086` refuses its address), `cc_ovcarve`, RunCPM's CCP, the
`sch_cur` test (task 0 is the UI task on every kernel and from before the
first claim), K2's nesting and image non-nesting, K6a's mechanics, and
RealDMG and the Video Player inside an fsx bracket (protected for free:
workers are frozen there and the posted pass runs only at `ui_task` step 0).

### 5.1 G1 - the cost of a compaction under a playing stream (MEASURED)

`tests/cpcost.py`: the trackmove scenario on a 5150 with a Sound Blaster and
the music LEFT PLAYING. HEAPFRAG's suite asked for nine compactions while
Tracker played BEVERLY.MOD from a double-buffered stream:

| pass | ms on a 4.77 MHz 8088 |
|---|---|
| 1 (moves the 116 KB module) | 491.7 |
| 2-9 | 74.1, 398.1, 58.2, 676.5, 104.7, 56.1, 84.0, 264.4 |

**One underrun across ~2.2 s of compaction**, and the stream played on
(SPEC.md 34.5's contract: an underrun is a pause the refill task resumes,
never stale audio). So TODAY a compaction under music is a short audible gap
at worst. The figure to watch as C3/C4 land is the largest single pass: a
330 KB LEMMINGS set is ~2.8x the module's 116 KB, so ~1.4 s with the refill
task held off. That is a pause, not a failure. Re-run this row with LEMMINGS
suspended once C4 lands; whether a per-pass cap is wanted is the owner's call,
made on that number.

**G1 WITH A LEVEL KEPT (C4, measured by `lemmove`):** the pass that moves
LEMMINGS' set is **848 ms** for 252 KB on a 5150 Hercules and **1,066 ms**
for 329 KB on a VGA XT, task switching paused throughout. A 64 KB emulator RAM
(C3) is ~180 ms by the same rate. These are the numbers the owner's per-pass
cap decision (6.3) is made on.

## 6. Open decisions for the owner

### 6.1 K9 - BUILT, as a count (SPEC.md 66.6.3.2)

The exact per-task form (a word per task naming the image it is in, a depth,
0FFFFh for nesting) was built: correct, gated, and **148 resident bytes**. The
owner asked for smaller. What shipped is a COUNT: non-UI calls into an image
increment `[wm_tcnt]` instead of pushing onto the UI task's stack, and while
it is non-zero every image and every driver claim is pinned. **42 bytes**
(`.cold` +32, `.text` +8 incl. a parity pad, `.bss` +2), and kern_small is
byte-identical. It is coarse, but only at the instant a compaction runs while a
worker happens to be inside an image, and the cost is a compaction that
achieves less once, never corruption. If that ever shows up in a measurement,
the exact form is in this branch's history at 148 bytes.

### 6.2 K6a - raise caches: DEFERRED (the owner, 2026-10-23)

Moving raise caches instead of dropping them would also need the PLAN changed:
`mem_avail` would have to answer "can this be served by compacting without
dropping?", and the compactor would compact more often in exchange for fewer
drops. Compaction is what lets the machine serve the 300 KB picture, or serve
anything at all on a full system. It is rare, and when it runs it is almost
always one of those two cases, so the case K6a would help is the smaller set.
**Deferred to a later research pass.** Raise caches stay purgeable and stay
DROPPED by the compactor. The ratchet (4) records them as "shed, by design".

### 6.6 P2's audit: CLEAN-WITH-WORK (2026-10-23)

The audit read every park site in `apps/frotz/`. Only the `OSAPI_TASK_ALIVE`
calls can park Frotz (it does not declare PARKSAFE, SPEC.md 66.5.9.1), and
there are eight of them: the burst loop's own, @read, @read_char, @save,
@restore, @restart, @verify's chunk loop and the [MORE] wait.

**Re-dispatching the current Z instruction is LOSSY and is not the design.**
Its operand decode pops the Z stack for variable-0 operands, five handlers
spill `[zf_pcoff]` to the store/branch byte before they park, and
re-executing repeats what is already on the glass (@read's echo, a posted
Save dialog, text printed before [MORE]).

**What works is RESUMING THE WAIT**, from state that is already in bss. One
byte, `[zx_rsm]`, names the handler (set before its spill/unlock, cleared
after the re-lock), and `zx_restart` either enters `zx_worker` (0) or jumps
to that handler's post-wait tail through a second entry into
`zi_readline`/`zi_readchar`/`zi_request`'s `.wait`. No Z instruction is
re-executed except @verify, which is pure. @restart gains the `zx_spill` the
others have (3 bytes).

**[MORE] withdraws the declaration** (`OSAPI_TASK_RESTARTABLE AX=0`) for the
length of its wait, because the text decoder's state is on the stack there
and replaying it duplicates output. The region stays pinned while the prompt
is up. That is a missed move, not a loss. A `mem_avail` plan taken mid-burst
can therefore read HIGH if Frotz reaches a page boundary before it parks. It
is the K9 shape (a compaction achieves less, once), and rare.

Estimated **200-250 bytes of FROTZ.O88, zero kernel bytes, zero per
instruction**, which the owner's rule allows. It is built after A3 merges,
since A3 also edits `zio.inc` and `zwin.inc`. Its gate is one leg per park
site (described in the audit), with red runs for a declaration pointed at
plain `zx_worker` and for the [MORE] withdrawal removed.

**6.6 BUILT (SPEC.md 66.5.9.3).** As designed, and smaller: **+37 bytes of
FROTZ.O88, zero kernel bytes, nothing per instruction, `.bss` +0** (the
estimate was 200-250; `@save` and `@restore` each share one body and one
resume row across their 0OP and EXT forms, and `zx_park`/`zx_relock` replace
the push-lock-pop pairs they wrapped). `tests/movable.txt`'s last `worker`
line is gone. `zrestart` (`tests/zrestart.py`) stops the worker in all eight
park sites, moves the region in each but `[MORE]`, and holds every leg's
dynamic memory, `[zf_sp]`, Z-stack and transcript to a boot that moves
nothing; red with the declaration pointed at plain `zx_worker` and with the
`[MORE]` withdrawal taken out. Two things the audit did not have: `@save`'s
handler branched through `zi_save`'s popped ES after a wait the story can
move across, so it reloads the PC from bss now; and `zf_reloc` shifts
`[zx_seglim]`, `zx_step`'s bound for the PC's segment. **And the gate found
`@save`/`@restore` had never worked** - three defects since Frotz shipped, +7
bytes between them, fixed here because the gate could not run without them
(SPEC.md 66.5.9.3's last paragraph).

### 6.5 PIXELSTEIN's re-homed claims need the fence widened (a kernel-byte decision)

A4 made PIXELSTEIN's shadow movable (`px_shmv`), but its levels (10 KB), art
masters (37 KB) and sprite set (60 KB) cannot be declared: the loader claims
them, OSAPI_PKG_REHOME re-owns every claim to the instance SLOT
(`mem_reown_x`), and `mem_find_own` accepts only `MC_OWN = caller's segment`
or `base = caller's segment`. So `OSAPI_MEM_MOVABLE` refuses the program's own
claims. The fix is one more arm in `mem_find_own`: accept `MC_OWN` = the
caller's slot. It needs the slot found from the caller's segment, so it is
ESTIMATED at 12-20 resident bytes, and it widens the fence for every re-homed
program (Skies, DOS) in the same way the `cmp dx, bx` arm already does for the
region. Until the owner decides, those three stay PINNED (rule: undeclared),
107 KB of a re-homed program's life.

## 7. Resume point (paused 2026-10-23 for the capacity limit)

**Built and pushed on `no-pinning`:** wave 0 (the C overlay moves; `ovlhigh`,
`ovlmove`), K8 (+14), K1a (-16), K2 (+58), K9 (+42 as a count; `tnest`), and G1
(`cpcost`, the baseline in 5.1). K6a is deferred and K3 is deferred as niche.

**Merged since:** A4/A5/A8 (`afb03c59`): 1942 and isoTron canvases movable on
the desktop and pinned across the bracket; Tank's shadow freed at the bracket's
end; PIXELSTEIN's shadow movable (6.5 is what blocks the rest); WireFleet the
first adopter of the parts standard's `OP_ONMOVE` opt-in (SPEC.md 20.12.12);
Excitebike audited, all bucket C. Rows `pkgmove-wireflet`, `pkgmove-1942`,
`pkgmove-isotron`, and `tank` and `pxsmove` extended.
C5 Weave/Loom (`1c406a9f`, WEAVE-SPEC 1.2.6): every long-lived claim of
both moves, through one `os88_onmove` each that rewrites every copy of a base;
part 0 is pinned once WEAVE's worker is hired (it parks inside it). It found
and fixed a live defect: WEAVE's region was already movable and its first
move left `wvm_ourds` stale, so natives ran against the old segment (new
opt-in `CC_ONMOVE_REGION`). On the 256KB XT the second WEAVE's region now
fits, so the refusal moved from the loader to the runtime's own window
(`weaveone` accepts either). Rows `weavemove`, `loommove`. It also found
LOOM leaking Preview's 112KB on a Pack from the pane, fixed in `056841dd`.
A1 RealDMG (`3465d15a`, REALDMG-SPEC 14.4): every long-lived claim moves
through one proc, `rw_mreloc`, which also fixes the core's copies (ROM
segment, bank table, `S_ROMX`, RAM, profile). Translated code bakes 8086
`jmp far` segments in and logs no list of them, so a move of the recompiler's
claim FLUSHES the cache (`rw_mjit`), a re-warm once per move. File calls and
the load pin everything (bucket C). **Speed unchanged**: 1414.3 against
1414.4 bank switches a guest second saturated, `gbdemo` 1.000 of real time,
all 145 hot routines instruction-identical at even shifts. +436 bytes of
image. Row `rdgmove`. The parts carve (core + view, 128KB) stays pinned until
RealDMG opts into A8's `OP_ONMOVE`, which needs a hook for `rdg_viewseg`,
`rdg_coreseg`, `S_VIEW`, `S_DISP`, `BTAB[0]` and a cache flush.
A3 (`5ffb4f99`): ten claims in seven packages move - Browser's four,
FTPD's stage, Chart's canvas, Frotz's scrollback, save staging and transcript,
Dot Delirium's board picture, the Desk Clock's face cache and Telnet's Zmodem
staging - each pinned only across its own file call. Rows `pkgmove-browser`,
`-ftpd`, `-chart`, `-dotdel`, `-deskclock`, `editmove-frotz`. Left pinned with
a reason in HEAP-CLAIMS: the bucket C transients, The Wire (needs a QEMU way to
post a compaction and pins around `OSAPI_PKG_START`/`FILE_WRITE`), PiXEL (with
its XMS work), DOS's DIR listing (an audit of `dsh_*`).
A1b (`dff195ee`): RealDMG's parts carve (core + view, 128KB) moves - the
first package beyond WireFleet on `OP_ONMOVE`, its hook `rw_partsmv` fixing
the view's banked words and the 512-entry bank table and flushing translated
code - and the close hang is fixed (`rw_bmiss` tested `[rw_closing]` before
`[rw_bankreq]`; reordered, 0 bytes). +224 bytes; 1414.4 against 1414.5 and
1403.8 against 1403.7 switches a guest second; hot routines unshifted. Rows
`rdgmove` (extended) and `rdgclose` (86Box, the bank-cache path), each red.
RealDMG now has NO long-lived pinned claim.
P1b (`758ea876`, SPEC.md 82.16.11): SHEET's two code modules, CHART.OVL
(52KB) and MACRO.OVL (28KB), move. No module CS is left on a stack across a
claim: module-to-resident calls go through an in-place thunk that stacks a
TAG instead of the CS, MACRO-to-CHART goes through `ch_ovcall`, claim-capable
kernel slots through resident stubs, and MACRO's own claim is pinned across
its text-channel I/O. +438 image, +48 bss. **Measured cost per crossing, real
work, MartyPC guest cycles**: recalc +0.47% / +0.73%, chart render +0.01%,
macro loop +0.70% - under the 2% bar, so no leaf fast path was built. Rows
`sheetovlmove`, `sheetxbench`. Found on the way, NOT this work's: a SYLK save
of 60 rows came out truncated and formula text near 60 characters cut short,
in both builds.

**The Wire and PiXEL (follow-up, zero kernel bytes):** ten N lines retired,
(N 29 -> 19 on the base it was cut from). **THE WIRE** (SPEC.md 92.5.1): the catalog and the transfer claim
move through one proc, `wr_reloc`, over the three words that name them
(`[wr_dseg]` being the worker's copy), pinned only across `OSAPI_FILE_WRITE`,
`_APPEND` and `OSAPI_PKG_START`; +80 bytes of image. **The gate is QEMU's
and it POSTS the compaction**: QEMU's monitor has no memory write, so
`tests/os88qemu.py` grew `poke`, which starts the GDB stub at runtime
(`gdbserver`), writes with the remote protocol's `M` and detaches - the same
`[mem_cpq]`/`[mem_cpq_lvl]` post MartyPC's `pkgmove` writes. `thewire`
assertions 17-18: catalog 1C00 -> 1AC0 (taken after a late Refresh, whose
regrow lands it over a hole - at the first catalog nothing below it is ever
freed), transfer claim 1AC0 -> 1980 under the archive chain with every file
read back byte-identical; red with `wr_reloc` a bare `ret`. **PIXEL**
(SPEC.md 106.5.1): the code parts move through the standard's
`OP_ONMOVE px_mreloc`, pinned exactly while a task stands in one - a mask a
task, set and cleared by `px_pin`/`px_unpin` around every far call under
`cli`, so the worker parked INSIDE a decoder (or a playing GIF's part, at
every frame delay) keeps its part still and an idle part moves; the
full-screen bracket stands in its part once, so the per-frame poll costs no
far call. The animation claim moves between passes. The five work claims
(flat file, decode, GIF pass, edit, Save As scratch) are bucket C: each lives
exactly as long as the job using it, whose worker holds derived segments
across its parks. +226 image, +4 bss, +42 in the GIF part; all 768 unchanged
routines at an even shift. Rows `pkgmove-pixel`, `-pixelgif`, `-pixelanim`,
each red-tested (`px_pin` without its pin moved the GIF part 3320 -> 2AA0
under the parked worker). `tests/pkgmove.py` grew `--scratch`, `--idle`,
`--still`, `--busy` and `--tuck` for it.

**A6 audio** (branch `nopin-audio`, off `no-pinning` 5d2792cf): the seven N
lines of Audio, Tracker, WireFleet and MIDIRack are gone, ZERO kernel bytes.
Each claim is declared movable while idle and pinned (`OSAPI_MEM_MOVABLE` AX =
0) from a play's start to its stop - never per buffer: Audio's look-ahead and
speaker ring for a play, a pause included (SPEC.md 86.5.3, +66 image);
Tracker's speaker ring from `tsp_start` to `tsp_halt` (45.25.6, +48);
WireFleet's clip ring per clip, verb 0 to verb 2 (114.11.1, +24, out of the
alignment pad, so image + bss still 65,024 and 0 bytes of pad left);
MIDIRack's bank and mix claim for a wavetable play, its song movable except
across its own read (105.10.1, +92). One `PARITY` pad byte each in Audio,
Tracker and MIDIRack keeps every hot routine instruction-identical at an even
shift (`os88rdiff --hot`: Audio 216 of 217, the odd one out the idle sleep's
immediate; Tracker 255; MIDIRack 108; WireFleet 359, all but its sound
routines). Gate: `tests/ringmove.py`, five rows
(`ringmove-audio`, `-audiospk`, `-tracker`, `-midirack`, `-wirefleet`), each
red both ways (proc a bare `ret`; the play-time pin dropped). Two defects
found on the way, both package-side and fixed: **Audio's idle worker slept 6
ticks against `INST_PARKW`'s 4**, so a compaction left its claims where they
were about one time in three (now 3, 86.5.1); and **`wf_pcm_play` never
loaded verb 0's CX**, which `SOUND.DRV` refuses as 0 (7), so a WireFleet clip
played or fell back to tones on whatever CX its caller left.

**Kernel finding from A1b, not acted on (kernel bytes):** in a pass that
moves a package's REGION, `mem_region_reloc` re-enters the restartable worker
and clears its park byte, so `inst_seg_parked` reads "running" for the rest of
that pass and the package's own claims under it wait for the NEXT compaction.
It costs a second pass, not correctness; a fix is a kernel change.

**In flight in agent worktrees (branches off `no-pinning`, NOT merged):**
- C4: LEMMINGS' claims movable (range-translating `os88_onmove`)
- C3/C2: RunCPM, C64 and APPLE2 RAM, and the multi-row eager carve
- A2: Word and Scribe data claims

All three paused cleanly, each with a WIP commit rebased onto `no-pinning`
(fc717f36) in its own worktree under `.claude/worktrees/`:

| item | branch @ commit | state | next step |
|---|---|---|---|
| A2 Word, Scribe - **MERGED** | `worktree-agent-aed038112485c85c2` @ 9989cda3 | code done. Word 6/6 and Scribe 7/7 claims MOVE (`tests/wdheapmove.py`), three red runs taken; +108 / +141 image bytes, bss down by the same | register the two rows, write the new Word and Scribe memory sections (after SPEC.md 68.3.1, and in 95), HEAP-CLAIMS rows, `make`, run the Word/Scribe rows once |
| C4 LEMMINGS - **MERGED** | `worktree-agent-a589750b58a51e736` @ 6cad7abe | 18 words translated (not 7). Claims movable while held on the desktop and PINNED around the launch/play stretch (bucket C, zero per-frame cost). One hand run on VGA: all claims moved, hashes identical; +234 image, +82 module | fix `tests/lemmove.py`'s wait on a paused screen that keeps changing, run it on Hercules and VGA, the red run, the row, SPEC.md 116.6, HEAP-CLAIMS. Then G1 again with a level suspended |
| C3/C2 emulators - **MERGED** | `worktree-agent-a0e6fd7fc8914d127` @ 96631eb3 | RunCPM, C64 and APPLE2 RAM movable; new opt-in `CC_ONMOVE_PARTS` makes the multi-row carve movable (byte-identical for packages that do not opt in); core hot paths instruction-identical at even shifts; +158..+240 bytes; host gates red-tested | run `tests/rcmove.py` for the first time, the red run, SPEC.md 73.14.2.1 / C64-SPEC 3.1.1 / APPLE2-SPEC 3.1.1 / HEAP-CLAIMS / os88.h |

One stray file for the owner: the C4 agent left a 27 KB build log at `/x.log`
and the sandbox would not let it delete it. **On resume:** read each report, review each diff against the
section 5 lessons (every holder, derived segments, declare after all claims),
merge, re-run G1 with LEMMINGS suspended (5.1), then run the deduplicated soak
union of every row these touch, once.

**Mine, next:** A6 (Video Player ring). Read so far: the ring is claimed PER
PLAY in `vp_sndprep` (`OSAPI_MEM_CLAIM_DMA_HI`, "page-safe and never moved")
and freed in `vp_sndoff`/`vp_sfree`. If `vp_sfree` runs when a play ENDS, the
ring is already bucket D for its whole life and A6 is closed with a note. If
it is held until the file closes, it is to be declared movable when a play
stops and pinned again (`OSAPI_MEM_MOVABLE`, AX = 0) when the next one starts,
outside the frame loop. Then P1 (Sheet's chart overlay as a part), A1
(RealDMG: procs only, gated on its speed), A3-A8, R1/R2 and P2.

### 6.3 A per-pass cost cap? (from G1)

Moving a kept LEMMINGS level is ~1 s with task switching paused. During that
second a playing Sound Blaster stream's refill task cannot run, so it
underruns: a pause the refill task resumes, never stale audio. The pointer
still moves, because the mouse ISR draws it, and the machine is otherwise
frozen. That is the price of the 300 KB picture opening at all, and it is paid
only when a claim could not be served otherwise. The options:

- **(a) Accept it.** Compaction is rare, and the alternative is refusing the
  open.
- **(b) Cap one pass**, e.g. at ~128 KB of copying, and let the claim fail
  only if the capped pass could not make room. This is simple, but it can
  refuse an open that an uncapped pass would have served.
- **(c) Drop `[sch_lock]` between block copies**, so the refill task runs in
  the gaps. This is the most work, and each gap needs the map re-validated.

Recommended: **(a)** for now, with `cpcost` and `lemmove` printing the figure
every run so a regression shows.

**DECIDED (the owner, 2026-10-23): (a), accepted.** A compaction is caused by
a user action (opening something big on a full machine), and partial
compaction spread over several runs is not worth its complexity or its
bytes.

### 6.4 Purgeable caches are one decision, not four rows (the owner, 2026-10-23)

K6a's deferral reaches every PURGEABLE cache, not only raise caches. A
`MEM_P_*` claim is dropped by the compactor rather than moved, and making one
movable is the same research question as K6a's: a plan that compacts without
dropping, and more compactions for fewer drops. So **K4a** (`MEM_P_RAHB`, the
read-ahead buffer), **K6b** (`MEM_P_FATW`, the FAT caches) and **K6c**
(`MEM_P_FLIST`, the multi-file clipboard list) move into that research pass
with K6a, and they are NOT size-pass items. The size pass is left with one
row: **K4b**, `MEM_K_COPY`, the clipboard copy buffer (~17 bytes), the only
non-purgeable claim of the four.

## 8. The whole soak (2026-10-24) and what it found

Run once on `no-pinning` after the merge of `elendilon`, at the owner's word.
Every failure was classified and fixed or handed off, most of them not this
branch's: `regwork`, `invariants`, `dosshrink`, `deskcaltask`, `pxshud`,
`megaovl`, `extdmod`, `vblkemu`, `vidxms`, `loommove`, `netinst`'s wants, and
WireFleet back under `APP_MAX_SIZE`. `vesa` is fixed on `kernel-size-14`
(`e1e7cebf`, which `no-pinning` also needs) and `blobruns` is left to that
size pass. A scoped re-run of the 30 non-Sheet rows was 30/30.

~62 Sheet and Chart rows failed on one harness defect: each wrote its scratch
disk to a relative `build/` path that a soak lane then looked for in the frozen
tree. They go through `os88build.at` now (`2d17a014`), and the scoped re-run
was 66/67. The one left, `sheettitle`, was a REAL DEFECT and this branch's:

- **File > New hung the machine** (`26b5c16b`). P1b's `ch_xthunk` takes any
  caller CS that is not CHART.OVL's for MACRO.OVL's, and `sh_new` - resident
  code - did `SHOUT sh_colw_clear`. Its far return was rebuilt into MACRO.OVL
  at a `.text` offset, past the module's end, where the bytes run as an
  endless push loop; task 0's stack was swept and the machine stopped on the
  death panel. It is a near call now, and `chxthunk` (soak) refuses a vector
  call from `.text` off nasm's own map - `os88ovlchk`'s package walk folds
  every section but `.modc` into `.text` and could not see it. It reached
  `elendilon` with the earlier merge.
- `sh_fdlg_counts` had New and Calculation swapped since #289: New painted a
  fourth row out of the bytes after its table, and Calculate Now could not
  be shown or picked.

Not fixed, recorded: `sch_diepanel`'s line reads as a stipple on VGA (the
text strings are not legible; only the hex is), and on the base kernel it was
not visible at all.

`test-full` after the last fix: 73/73.
