# KERN-MEGA-MEMORY-PLAN - kern_mega's memory consolidation round

**Status: BUILT but for MC9 (2026-10-26)** - MC1, MC8, MC2, MC3, MC7, MC4
and MC5 are built (section 7); MC9, what still goes low, is deferred and is
carried in KERN-MEGA-PLAN's *Open before kern_mega goes live*. Was: branch `claude/protected-mode-memory-consolidation-k1kjao`,
cut from `elendilon` at `8eac6506`, 2026-10-25). This file is the authority for
the round: the goal, what is true of the tree today, every decision the owner
took and the assumptions under them, the waves in the order they are taken, and
what each wave came to as it is built. `docs/plans/KERN-MEGA-PLAN.md` 14.74 is
the short pointer to it and records how the round was arrived at.

A bare "14.nn" below is a section of KERN-MEGA-PLAN.md; a bare "§" is SPEC.md.

---

## 0. The goal, and the rules of the round

**The owner's words.** kern_mega *"was using memory frivolously"*: the Victory
286's Task Manager showed ~374K "Above 1MB" on a 4MB machine, most of it not
memory the kernel needs (14.70). The end goal, in the owner's words:

> *On a 4MB system that gives 3.3MB above 1MB, a single claim could take all of
> the 3.3MB the system isn't using, and then all of the 640K the system isn't
> using in a second claim - or if we can bridge the two into one claim, all the
> better.*

and *"the goal here is to shrink the overall usage."*

**The rules the owner set for doing it (2026-10-25):**

1. **The end result is what ships.** No interim wave ships on its own, so a
   wave need not work alone if a later wave completes it. Do not bend a wave
   out of shape to make it stand alone.
2. **Do not create debt for the end state.** No new pinned memory, no new
   refusal, no compatibility break that a later round would have to undo.
3. **Every piece of code ever written for the OS is in this repository**, so
   converting all of it is in scope; there is no "older style" program to
   accommodate.
4. **Old hibernate images may be refused** by the new kernel.
5. **The order is this plan's to choose**; the whole round lands before any of
   it ships, the 1MB machine included.
6. **Work through the waves**; stop only for a blocker that needs the owner,
   or for a change of plan that alters these goals.

**What "done" means:**

- every byte of extended memory the system is not using can be had by ONE
  claim (`OSAPI_MEM_HUGE`), and the same claim reaches conventional memory too,
  bridged across the 640K-1MB hole, leaving a **64K** conventional reserve;
- a program can ASK how much that is, exactly, before claiming;
- the kernel's fixed footprint above 1MB falls from ~256K to ~66K: the GDT
  is ~4K, the LDT starts at 2K and grows, the HMA is pool, and there is no
  padding;
- nothing in the pool is pinned but a module whose action is running;
- a DOS trip that has nowhere to park falls back to a hibernate, which works
  with the pool full;
- `tests/megahigh.txt` is empty and cannot grow.

---

## 1. What is true of the tree today (the knowledge base)

Read off `kernel/mega.inc`, `kernel/memory.inc`, `kernel/hiber.inc` and the
build at `8eac6506` unless a measurement says otherwise.

### 1.1 The layout above 1MB

**This table is the layout the round STARTED from** (`8eac6506`). MC2
replaced it: the tables and the code are at the top of RAM and the pool runs
from 1MB - see *MC2 as built* in 7.

| linear | what | size | source |
|---|---|---|---|
| 100000h | the HMA, kept free for DOS's real-mode half and `kern_dos` | 64K | KERN-MEGA-DOS-PLAN 7, 14.25 |
| 110000h (`MEGA_GDT_LIN`) | the GDT, 8,192 entries, IDENTITY: entry i = base i x 128 | 64K | 3.1 |
| 120000h (`MEGA_LDT_LIN`) | the LDT, 2,048 entries: 0-1279 a STATIC code alias per 512 bytes of conventional memory (index = segment / 32), 1280-2047 the allocator | 16K | 13.3, `mega_ldt_alloc` |
| 124000h (`MEGA_IDT_LIN`) | the IDT | 2K | |
| 125000h (`MEGA_COLD_LIN`) | `.cold`, then `.mega` at its rung's distance (`MEGA_HI_LIN`), then the park's scratch (`MEGA_SCR_OFF`, `MPS_SIZE`) | ~58K (`.cold` 42,207 + `.mega` 14,698 + 1,484) | `mega_coldup`, KERN-MEGA-SEGMENTS-PLAN |
| to 140000h (`MEGA_XPOOL`) | padding, there only because the pool's base wants a low word of 0 | ~50K | |
| 140000h up | the pool: `mx_tab` (96 records, `.lowbss`), 1KB granular, offsets in KB from `MEGA_XPOOL` | the rest, capped at 15,360K | `mega_xminit` |

Every linear address of the fixed area is an ASSEMBLY CONSTANT today, except
the tables' when `mega_tlowf` moved them (below). `mega_gdtr`, `mega_idtr`,
`mega_ldtlin` and the bootstrap window (`mega_bwin`) are variables in `.text`
that hold them, and the 286 reset path reloads from those variables.

**The 1MB machine.** When the BIOS reports less extended memory than the
fixed area needs, `mega_tlowf` claims **82KB of CONVENTIONAL memory**
(top-down, pinned, `MEM_K_MTBL`) and lays the GDT, LDT and IDT out there in
the same shape. Such a machine has no pool, `.cold` stays low and hibernate
greys (14.17). Verified on 86Box `vm/286-525`, an AMI 286 at 1MB with no
extended memory reported. **This is the 1MB machine's largest single cost**,
and MC7 is what cuts it.

### 1.2 The pool's rules

- `mx_alloc` places in two directions (13.4.1): the kernel's own blocks
  (`MX_KOWN` FEh) and driver images (`MX_KDRV` FDh) FIRST FIT FROM THE TOP,
  everything else from the pool's base up.
- Owners: an instance's `OSAPI_XMEM_ALLOC` block is its slot (00h-7Fh); its
  region `80h|slot`; its claims `0A0h|slot`; a driver's claims `0C0h|row`;
  the cells' kernel-owned blocks FFh; the walk's fixed-area markers F0h-F4h.
- **What moves** (`mx_canmove`): `80h`-`DFh` and `MX_KDRV`. Moving is a copy
  in pieces with interrupts on between them, and every LDT descriptor whose
  base lies in the block is rebased (`mx_cpmove`, `mx_desc`); no relocation
  proc is needed because holders name a block by SELECTOR.
- **What does not move:** `MX_KOWN` (caches, modules, the LDT-block of a lone
  selector), FFh, and `OSAPI_XMEM_ALLOC` blocks, whose token IS the linear
  base the package holds.
- `.norun` in `mx_alloc` packs movable blocks DOWN once (`mx_compactf`) and
  retries; `mx_packupf` packs them UP and is used by `OSAPI_MEM_REGROW`'s last
  try. Both exist and both work in pieces.
- Measured (14.70.3): Bochs at 16MB read **AVAIL 15.26M against MAX RUN
  14.70M**: ~570K of free pool no single claim could have.

### 1.3 Conventional memory on kern_mega

- The heap starts at **1060h** once `.cold`'s rung is returned (`mem_base`,
  read off the machine), and runs to 9FC0h: 573K on a 640K machine.
- Below 1060h: the IVT, the BDA, the kernel's `.text`/`.bss` at KERNEL_SEG
  (60h), LOW_SEG's stacks and `.lowbss` (`mem_tab`, `mx_tab`). Those are the
  segments the kernel names by constant.
- With the pool free, the kernel's own conventional claims on a desktop are
  **one**: `P_RAHB`, the 5K read-ahead landing, which is the bounce every
  reflected disk transfer uses (2, measured).
- The kernel's CACHES go above 1MB as pool blocks under a selector when the
  pool has room (`mem_hitag`'s list: the window save-unders, the menu
  save-under, the icon store, the multi-file list, the clipboard, the FAT
  windows), and to conventional memory when it does not
  (`mem_claim_x`'s kern_mega arm).

### 1.4 Purgeable caches above 1MB

A pool block has no `mem_tab` record and no tag: `mega_hiclaim` takes
`MX_KOWN` and drops BX. So a cache above 1MB is never shed, never counted as
reclaimable, and billed to System by the Task Manager. On kern_big
`mem_shed_one` sheds the cheapest purgeable record strictly below the
claimant's rank (`mem_rank_bh`, §50.6.4) and tells the holder through
`mem_pg_forget`, which knows the FAT window's and the icon store's extra
naming words (`dsk_fatw_demote`, `ico_demote`).

### 1.5 Segments and selectors

- **3.1's identity map:** GDT entry i describes base i x 128, so a
  conventional segment on the 8-paragraph grid IS its own selector. This is
  what let the whole kernel and every package run unchanged in protected
  mode, and it is why the GDT is 64K: it must reach the highest segment
  anything loads (F000 the ROM, the scratch selectors at FFD0h-FFF8h, video,
  and every heap paragraph up to 9FFFh).
- **The LDT's static code aliases:** a 512-aligned image's CS is
  `(S >> 2) | 4`, an LDT selector whose index is S / 32; `mega_cs2ds`
  converts back with `shl ax, 2`. The allocator's pairs (DATA = CODE + 8) are
  the other kind, and `mega_ldt2ds` tells them apart.
- **Measured** (14.70.1): of 8,192 GDT entries **106 were ever loaded**, 78 of
  them the Task Manager's ROM scan; of 2,048 LDT entries, **31**.
- 2.3's survey of `kernel/`: 230 loads of DS/ES/SS from a general register (61
  a constant, 37 zero, 33 a stored variable, 23 a copy, ~21 arithmetic, ~55 a
  parameter), and 44 lines of paragraph arithmetic in `memory.inc` alone.
- `mega_s2segf` converts a selector to a real-mode segment for the
  reflection; `dskw_hiseg` gives the file layer a landing for a buffer above
  1MB; `mega_oddsegf` makes a scratch selector for an off-grid paragraph.
- **The gate:** `tests/unit/t_megahigh.py` (full tier) and
  `tests/megahigh.txt`, whose remaining exemptions are SHEET, REALDMG,
  BROWSER (`high` and `highmem`), XTMP3, PIXEL and LEMMINGS (`highmem`), each
  with its reason in that file.
- **The windowed DOS is refused on kern_mega** (KERN-MEGA-DOS-PLAN 2.1): a DOS
  program runs in real mode, in the trip, where the GDT is never read. So no
  code from outside this repository ever loads a segment in protected mode.

### 1.6 The hibernate on kern_mega

- The write: the conventional picture as one write from linear 0 in 32KB
  chunks (`hbm_xwconv`, chunks from identity segments n x 800h), then an
  extended SECTION: a 512-byte head (`HIBX`, at most `HBX_HMAX` = 62 runs) and
  the runs, run 0 the GDT+LDT+IDT, then every pool block, copied through a
  **32KB conventional bounce** (`hbm_xbounce`, `MEM_K_HIB`, page-safe,
  claimed after the picture).
- The resume reads the **WHOLE FILE into one staging block above 1MB**, placed
  clear of every block the image had and every block the boot holds, and the
  park stub copies runs, then conventional memory, into place.
- **So the hibernate REFUSES when the pool is full** (`hbm_xplan`: *"whether a
  resume could place all of it in one block beside it"*). Measured, 2.
- The image is as big as the memory in use: the pool's blocks are all
  written.

### 1.7 Where kern_mega's code can go (os88megaseg at `8eac6506`)

| where | used | room | binds? |
|---|---|---|---|
| `.mega`, its own segment | 14,698 | ~50K of 64K | no |
| the slot above 1MB to `MEGA_XPOOL` | 58,678 (with `.cold`, scratch) | 51,914 | until MC2 |
| `.cold` under `%ifdef KERN_MEGA` | 42,207 | ~23K of 64K | no |
| the SHARED 64K window, `.text`+`.bss` | 52,188 | 13,348 physically | **the `tests/megaseg.txt` allowance: 3,714 of 4,029 used, 315 left** |

The allowance protects kern_big: every byte kern_mega adds to the shared
window is a byte kern_big can never grow by. So the round's logic goes in
`.mega` (or `.cold` under `%ifdef`), and the shared window gets only HOOKS
(a far call, ~5 bytes). Where hooks exceed 315 bytes, room is made first by
moving the part of `mega.inc`'s 2,456 `.text` bytes that never runs in real
mode into `.mega` (14.45 moved 1,378 that way); a raise is asked of the owner
only if that falls short, with the measured figure.

**Code is usage too.** `.mega` and `.cold` are resident above 1MB from boot,
`.text` in conventional memory. The round's code is a few KB (ESTIMATE)
against ~200K returned.

---

## 2. The measurement behind the reserve

Taken once, because it decides a design (CLAUDE.md's Testing exception),
under Bochs 2.7 at `543827bd`: kern_mega, SOUND.DRV + HDD.DRV, an SB16, booted
off C: with the system files so HIBER.DRV can write there,
`build/megaapps.img` in B:. `tools/megaconvres.py` re-takes it (`MEGS=4` for
the owner's 4MB example; `--nofill` leaves the pool free). It is not a suite
row; MC5's row keeps the reserve honest.

**Method.** After the desktop came up every free KB of the pool was covered by
pinned `mx_tab` records written through the debugger: a program holding all
of extended memory. Then ordinary actions, sampling `mem_tab` for every claim
not owned by an instance. 16MB (pool 15,104K) and 4MB (2,816K) read the same.

| action, pool full | kernel claims in conventional memory | total |
|---|---|---|
| idle desktop | `P_RAHB` 5K | 5K |
| open B:, then a folder | + `P_FATW` 5K, `P_ICO` 5K | 15K |
| open the Control Panel | + `K_MOD` 15K (CTRL.DRV), `P_WSAVE` 30K | 60K |
| System > Hibernate (dialog) | + `K_MOD` 11K (HIBER.DRV), a second `P_WSAVE` 22K | 93K |
| Hibernate | **REFUSED** (pool full); ~20 s unfilled | - |
| a file copy (`fcp_claim`, by reading) | `K_COPY`: free up to 64K, at least 3K or a cluster | 3K+ |
| the hibernate's write (`hbm_xbounce`, by reading) | `K_HIB` 32K, page-safe | 32K |

**Reading it.** The raise caches and the icon store are TRIV and are shed by
any program claim; they are not reserved. What must keep working with
everything held is the disk (landing + FAT window, 10K), the Control Panel
(CTRL.DRV 15K) and the hibernate (HIBER.DRV 11K + the 32K bounce). The worst
concurrent case is the hibernate: **5 + 5 + 11 + 32 = 53K**.

**The reserve is 64K, one run** (owner: accepted). Every conventional claim is
page-safe, so the 32K bounce must sit inside one 64KB DMA page. A 64K run
straddles at most one page boundary, so one side of it is at least 32K, and
the other 32K holds the 21K; a 53K run with the boundary near its middle
cannot hold the bounce at all. Not covered, on purpose: launching another
program (it has no pool to run in), mounting a driver mid-session (it refuses
with its reason), the holding program's own DMA ring (it claims that first).
32K was the alternative (Control Panel only); taking the hibernate under 32K
means a smaller `HBX_CHUNK`, which is the image's file chunk too.

---

## 3. Decisions, and the assumptions under them

| # | decision | by | rests on |
|---|---|---|---|
| D1 | The fixed area goes to the TOP of RAM and every claim is taken top-down, so free memory is one run from 1MB up and the HMA is reached last, just before conventional memory | owner (2026-10-25) | MC3's no-pinning pass, without which a late pinned block walls the run |
| D2 | The pool gets kern_big's no-pinning treatment: everything moves but a module in use | owner | `XMEM_ALLOC` tokens are never read in place under protected mode (A3) |
| D3 | Caches above 1MB are purgeable exactly as on kern_big | 14.70.1 | |
| D4 | The HMA is pool; a DOS trip parks what stands there | owner (14.70.1) | |
| D5 | A DOS trip with nowhere to park offers a HIBERNATE instead | owner | the streaming resume (MC4) |
| D6 | One claim can take all free memory; a program can ask the exact figure first | owner | REGION-SELF-COMPACT-PLAN 5.1.1: a refused claim is destructive |
| D7 | Huge claims BRIDGE conventional and extended memory BY DEFAULT | owner | a huge claim is a run of 64KB windows, walked by CX (A4) |
| D8 | The conventional reserve is 64K, one run | owner, on 2's measurement | |
| D9 | Every program, driver and kernel path is converted so that above the heap floor nothing needs a segment to be its own selector; the GDT is a fixed ~4K, the LDT all allocator | owner | A1, A2 |
| D10 | Old hibernate images are refused | owner | |
| D11 | The order is MC1, MC8, MC2, MC3, MC7, MC4, MC5; MC9 is a later round | this plan, owner's delegation | 4 |
| D12 | The round's logic lives in `.mega`; the shared window takes hooks only | 1.7 | |

**Assumptions** (each is checked by the wave that relies on it, and a failed
one is a stop-and-ask):

- **A1** No code outside this repository loads a segment in protected mode:
  the windowed DOS is refused on kern_mega, DOS programs run in the trip in
  real mode. Checked by MC7's census.
- **A2** Real-mode code never reads the GDT or LDT, so the reflection, the
  trip, the 286 reset landing and a ROM calling USBMOUSE.DRV's hook are
  unaffected by what the tables hold, as long as the segments handed to them
  are REAL segments (`mega_s2segf`).
- **A3** On kern_mega no package reads an `OSAPI_XMEM_ALLOC` token in place:
  `os88la.inc` (LOADALL) and `os88ub.inc` (a moment of protected mode) are
  real-mode techniques, and the Video Player takes `OSAPI_XMEM_RUN`'s
  selectors under `[vp_pm]`. Every use of a token goes through a kernel cell.
  MC3 censuses token ARITHMETIC (a token plus an offset passed back to a cell
  is fine inside the block's own range).
- **A4** A package walks a huge claim only by the CX step `OSAPI_MEM_HUGE`
  answers (the SDK says so), never by assuming windows are physically
  adjacent. MC5 censuses the callers: `apps/video/video.asm` (32KB-step, never
  bridged) and the C SDK's `os88_mem_huge`.
- **A5** The resuming boot on the same machine computes the same top-of-RAM
  layout as the hibernating one (same `int 15h AH=88h` answer). MC4 checks
  the image's recorded layout and refuses on a mismatch.
- **A6** A raw DOS allocator during a trip is caught by the sum whichever end
  it takes memory from (KERN-MEGA-DOS-PLAN 5.2's placement paragraph turns
  over with D1; updated in MC2).

---

## 4. The order (D11), and why

1. **MC1 purgeable** first: small, independent, and every later figure counts
   on caches being sheddable.
2. **MC8 14.69's three** next: small, and two of them (RAMDISK.DRV's and
   NET.DRV's paragraph folds) are MC7's first census items anyway.
3. **MC2 the fixed area to the top**, before anything shrinks it: while the
   fixed area sits below the pool, a table that shrinks leaves a hole nothing
   can claim.
4. **MC3 the no-pinning pass and every claim top-down**: free memory becomes
   one run from 1MB up.
5. **MC7 selectors everywhere**, before the hibernate and the whole-memory
   claim: it changes what a claim IS (a selector, movable by rebase), and the
   tables the hibernate saves, so the hibernate's format is reworked once, in
   MC4, on the final layout.
6. **MC4 the HMA and the streaming resume**, on the final layout.
7. **MC5 one claim for all of it**, last: it needs every wave before it.
8. **MC9 what still goes low** is a later round unless the owner says
   otherwise.

---

## 5. The waves

Each wave verifies on the host (`make`, `make mega`, `make KERN_SMALL=1`,
`tools/kernsize.py`, `tools/os88megaseg.py`) and by LOOKING under Bochs, and
NAMES its rows; the deduplicated union of the named rows runs ONCE after the
last wave, with `test-full` (CLAUDE.md, *Testing*). kern_big and kern_small
must stay byte-identical except where a wave says why not.

### MC1 - purgeable pool blocks (D3)

- `mega_hiclaim` keeps the cache's TAG. An `mx_tab` record is 8 bytes and
  full; `MX_SEL` is `OSAPI_XMEM_SEL`'s window and never used by a cache, so
  the tag rides there behind a `MX_FL` bit, `MX_FPURGE`. (MC3 widens the
  record anyway; if the tag moves then, so be it.)
- `mx_alloc`'s refusal path sheds before it gives up: the cheapest purgeable
  block strictly below the claimant's rank (`mem_rank_bh`), the holder told
  first through `mem_pg_forget`, then the block and its selector freed. A
  cache never sheds a more valuable cache, never the FAT window being written.
- `mem_claim_x`'s conventional refusal also sheds pool caches when the pool
  path is what it would retry into (to decide while building).
- `OSAPI_XMEM_WALK` reports the bit; the Task Manager files those blocks
  under `Purgeable` (14.70.3's last box).
- Rows: `megaown`, `tmowner`, `megahigh`, and a new row: fill the pool with
  raise caches, ask for a claim that fits only with them gone, assert it
  succeeds and that they were shed; red without the shed.

### MC8 - 14.69's three

- RAMDISK.DRV's `rd_fold` and NET.DRV's `net_norm` stop folding paragraphs
  into a kern_mega selector; `t_megagrid` takes `drivers/` too.
- `CDROM.DRV` declares `OS88_DRV_HIGHOK`; `megacdrom` asserts its base above
  1MB.
- `\AUTOSTRT` is walked after `mega_enter` on kern_mega, so an `OS88_HIGH`
  package it opens is placed high.
- Rows: `megagrid`, `megacdrom`, `autostart` under `make mega`.

### MC2 - the fixed area at the top of RAM; the pool from 1MB (D1)

- The top is 1MB + `[mega_extkb]`, capped at 16MB. `mega_enter` lays the
  fixed area out DOWNWARD from it, KB-granular: IDT, GDT, LDT, `.cold`,
  `.mega`, the park's scratch, with what MC7 shrinks (LDT, GDT) at the zone's
  BOTTOM edge, next to the pool.
- The `MEGA_*_LIN` constants become variables; most readers already read one
  (`mega_gdtr`, `mega_idtr`, `mega_ldtlin`, the descriptors written at run
  time). The rest is a census: `kernel.asm`'s fit checks, `mega_xmfix`, the
  park's addresses (`megados.inc`, `MEGA_SEL_PSCR`), the hibernate.
- `MEGA_XPOOL` becomes 100000h (64KB-aligned, so `add dx, MEGA_XPOOL >> 16`
  stands). `mega_xminit` records the fixed zone and the HMA (until MC4) as
  pinned marker records; `XMW_FIXED` reads them, and `Padding` disappears.
- The hibernate's section names the pool base and the tables' places: an old
  image is refused (D10).
- KERN-MEGA-DOS-PLAN 5.2's placement paragraph is updated (A6).
- `mega_tlowf`'s low-table path stays for machines with too little extended
  memory; MC7 is what shrinks it.
- Buys ~50K.
- Rows: `megahigh`, `megahiber`, `megap2hiber`, `megadosp`, `megakbtrip`,
  `megarehome`, `megapiclose`, `tmowner`.

### MC3 - the no-pinning pass, and every claim top-down (D1, D2)

- `OSAPI_XMEM_ALLOC` blocks: the token becomes a range in a TOKEN SPACE, not
  the physical base; the block's physical base moves under it and every cell
  (COPY, SEL, RUN, FREE, the walk) translates. `mx_tab` gains the token base:
  8 -> 10 bytes, +192 bytes of LOW_SEG. Census: no package does arithmetic on
  a token beyond its own block (A3).
- The kernel's cache blocks move by copy and rebase (13.1), or are shed if
  that is cheaper (MC1).
- Modules move when no frame is inside them; pinned only while their action
  runs (kern_big's rule).
- `mx_alloc` places every owner top-down; compaction packs UP against the
  fixed zone (`mx_packupf` becomes `.norun`'s pass; `mx_compactf` goes).
- Rows: `megaown`, `megavidbig`, `megavidv2`, `megaovl`, `megarehome`,
  `megahigh`, `megahiber`, `tmowner` (MAX RUN = AVAIL after a pack), and a new
  row: an `XMEM_ALLOC` block moved by a pack and read back through the same
  token.

### MC7 - selectors everywhere above the floor; a ~4K GDT, an LDT that grows (D9)

- **The end state.** Identity selectors survive only from segment 0 to the
  heap floor (1060h): the IVT window, the BDA, the kernel's image and
  LOW_SEG. The GDT is that, ~525 entries, **~4.2K, fixed**. Every claim,
  region, carve, overlay, module and driver image above the floor gets LDT
  selectors allocated with it and freed with it, in conventional or extended
  memory alike. The LDT is all allocator, starts at 2K and grows (a bigger
  pinned block, a copy, GDT entry 18h rewritten, `lldt`). The 1MB machine's
  82KB low tables (1.1) become ~8K.
- **Side effect worth having:** a conventional claim named by a selector
  moves by copy and rebase with no relocation proc - MC3's pass, extended to
  conventional memory.
- **Step 1, the census:** every site that loads a paragraph ABOVE the floor
  into a segment register in protected mode, or hands one to something that
  will:
  - **Packages** - the six `tests/megahigh.txt` exempts (1.5), converted as
    14.40-14.52 converted the rest (offsets inside 64KB selectors,
    `OSAPI_MEM_SUBSEG`, `OSAPI_MEM_HUGE`'s step, `OSAPI_MEM_CODESEL`). After
    it the file is EMPTY and `t_megahigh` refuses any line.
  - **Claims through other doors** - the DMA rings, the C64/Apple II ROM
    carve, the CWORD/RunCPM overlay carve, `os88type.inc`'s face cache, the
    re-homers' parts carves. Each keeps living where it must but is named by
    an LDT selector; a holder that programs the 8237 gets the LINEAR base
    from the DMA door beside the selector (an ABI addition, §20.2.0.1).
  - **Drivers** - the seven without `OS88_DRV_HIGHOK` (CDROM, HDDTOOL, HTTPFS,
    HTTPPAGE, RAMPAGE, USBMOUSE, VESA; VMMOUSE is kern_emu's, XMEM.DRV's cells
    are the kernel's own on kern_mega, OS88NET is a DOS program) and every
    driver's own claims.
  - **The kernel** - the loads of HEAP paragraphs: the compactor
    (`memory.inc`), `dskw_norm`'s kern_mega arm (128-byte granules ARE the
    identity map; a selector takes `dskw_hiseg`'s path), the loader's region
    and carve, `mod_check`'s `(S >> 2) | 4`, `hbm_xwconv`'s identity chunks,
    `mem_claim`'s own answer (a selector for every claimant on kern_mega).
  - **A000h and up** - video behind `[vid_*]`, the ROM font, the Task
    Manager's ROM scan, EMS page frames, any package writing A000h/B800h
    itself in an fsx bracket: allocated selectors; a hot path converted, not
    fixed up.
  - **The scratch and code selectors at FFD0h-FFF8h** - into the LDT.
  - **Claims made in real mode before the switch** (MC8's finding 3): every
    boot-time claim holds a real segment in a kernel word, and is given its
    selector AT the switch, holder by holder (`mem_hicaches_f`,
    `drv_hiboot_f` are the pattern).
  - **File drivers get the selector itself** (MC8's finding 1):
    `drv_fs_hi`'s 512-byte bounce is retired and the kernel splits a
    transfer at the selector's 64KB limit. RAMDISK.DRV, NET.DRV and
    CDROM.DRV already walk an LDT selector's offset (MC8); HTTPFS is
    checked in the census.
- **Step 2** packages and drivers, **step 3** the kernel, **step 4** cut the
  GDT's limit to the floor and drop the static aliases; a missed site then
  shows as a `#GP` past the limit, reported at the instruction (14.42).
- **Buys** ~60K of GDT and ~14K of LDT on every machine; on a 1MB machine with
  no extended memory, ~74K of its conventional memory.
- **Repercussions** (owner: accepted): a wild segment load faults instead of
  reading whatever is there; old hibernate images refused; the census is the
  cost.
- Rows: `megagrid`, `megapkg`, `megahigh` (empty list), `megaflt`, `megaovl`,
  `megarehome`, `megavidbig`, `megavidv2`, `megadosp`, `megahiber`,
  `megap2hiber`, `megatrap0`, `tmowner`, the converted packages' own rows
  under `make mega`, the adapters' boots; a new row that opens programs until
  the LDT grows and asserts the GDT's limit is the floor's.

#### MC7.1 The census (2026-10-25)

Five read-only passes over the tree at `331b47f7` (after MC3): the kernel in
three slices, the packages with the C SDK, and the drivers. Each site was
classed OPAQUE (only held and loaded: works once the value is a selector),
ARITH (a segment made by arithmetic, compared by order, or turned into a
linear address), REAL (handed to real-mode code or to hardware), CONST (an
immediate segment at or above the floor) or IDENTITY (a segment used as a
key). The findings that decide the design:

- **The kernel's claim choke point is one routine.** `mem_claim_1`'s publish
  (memory.inc) is where every conventional claim - about 45 kernel call
  sites and every package and driver claim through `mmf_osapi_mem_claim*` -
  gets its value. A selector allocated there, with the record keeping its
  physical paragraph (MC_SEG/MC_PARA bookkeeping, ~95 lines, stays
  physical), reaches every claimant at once. Four lookup helpers
  (`mem_find_own`, `mem_owner_of_x`, `mem_is_region`, `mem_in_xfer`) carry
  most of the identity compares.
- **The TI bit is used as "above 1MB" at about twenty sites** (memory.inc
  ×7, loader ×3, mod ×2, driver ×3, disk ×3, lz ×2, plus `dsk_runcap`).
  Once every claim is an LDT selector they all misroute silently. One
  predicate - "is this a pool block?", by base or by owner - replaces them.
- **The code aliases already have a pair arm everywhere.** `MEGA_CSELTOP`,
  `mega_frame_cs`, `mem_reloc_call`, `mega_cs2ds`/`dsimg`/`csdata` and
  `mega_codesel` all handle a code/data pair (code = data - 8). Once every
  CS-able claim is a pair, the `(S>>2)|4` arms are dead code. The 232
  module `CSDO`/`PUSH_CSDATA` sites need nothing.
- **The compactor's copy engine** (`mem_bcopy`) loads arbitrary paragraphs;
  it becomes a linear copy plus a descriptor rebase (MC3's pool rule), and
  the relocation protocol's kernel half (`mem_rr_tab`, the IVT rows, the
  worker restart) becomes a no-op for selector holders.
- **Video is one table.** `vid_tab`'s segment column feeds `vid_seg`,
  `vid_rseg`, `vid_tseg` and the two `vid_ctx` records; twelve planar hot
  paths load `VGA_SEG` as an immediate (`mov es, [vid_seg]` is a byte
  shorter); `fsx_mtab` hands nine framebuffer segments to packages, and no
  package does arithmetic on FSI_SEG (pages are reached by offset). The ROM
  font is one word, `font_seg`, with four origins decided in real mode.
- **The file layer**: `dsk_runcap` is the one DMA-page authority and must
  work from a LINEAR base; `dskw_hiseg`'s low arm (a real segment loaded in
  protected mode) retires, and the high arm needs a 64KB-limit split it
  does not have today; `drv_fs_hi`'s 512-byte bounce retires once file
  drivers take the selector (NET, RAMDISK, CDROM are ready; HTTPFS's
  `hf_bnorm` is not). The DISKOPS cloner and compressor and the LZ decoder
  step segments through claims over 64KB.
- **Values captured in real mode and used after the switch**: the live FAT
  window, `sc_seg`, `ico_seg` (over 63KB), `font_seg`, `vid_*`, `vid_ctx`,
  `bnd_seg`, driver rows and their IVT halves, module tables, a boot
  RAMDISK's claims, `em_frame`. `drv_hiboot_f`/`mem_hicaches_f` are the
  pattern.
- **Drivers** (one image ships to every kernel, so a kern_mega test costs
  every disk): SOUND programs the 8237 from `seg*16` (its own ring and a
  package's external ring), HDD caps a BIOS transfer by `seg*16`, RAMDISK's
  conventional arena is over 64KB, HTTPFS folds unconditionally, EMS walks
  C000h-F000h and hands out its frame, USBMOUSE must write a REAL segment
  into IVT 33h. About 150-200 bytes, plus three kernel doors: the linear
  base of a selector (`seg*16` elsewhere), a selector for a physical
  paragraph (the paragraph elsewhere), and selector-to-real-segment.
- **Packages: about 270 sites in about 30 packages and 4 shared layers.**
  - The six registry exempts (Sheet ~10, RealDMG ~25 plus two whole
    classes, Browser ~14, XT-MP3 7, PiXEL ~30, Lemmings 16).
  - Packages that DECLARE both bits but keep claims over 64KB conventional
    "by contract" and walk them in 128-byte granules, in HOT paths: Paint's
    canvas, Frotz's story, Tracker's and ModPlug's modules, MIDIRack's
    banks, the Video Player (all 31 of its claims through the DMA door on
    purpose).
  - The parts library (`op_seg`, `op_read`, `op_unpack`, `op_fetch`, the
    REHOME macro) - 18 users - and the C SDK's `cc_ovbind`, both segment
    arithmetic on a carve that may pass 64KB.
  - ROM biases (Apple II, C64: a segment BELOW the ROM part, hot), static
    `OS88_CODESEL` inside relocation procs that may not call the kernel,
    framebuffer immediates (Video, Excitebike, Gorillas), ROM/EBDA scans
    (Task Manager, Redline), `os88type`'s 512-byte alignment assert.
  - Five packages hand an external DMA ring to SOUND.DRV as a segment.
  - Lemmings cannot be run here (its data is fetched, not committed).

#### MC7.2 The decision this needs (asked of the owner, 2026-10-25)

The package half is the bulk, and most of it is ONE pattern: segment
arithmetic in 128-byte granules INSIDE one claim, in hot paths shared by
every kernel. Two ways to meet D9:

- **(A) Convert each site** to OSAPI_MEM_HUGE windows, offsets or SUBSEG
  calls. Zero run-time table cost. Thousands of lines across ~30 packages,
  each a change on the 8088 too unless it is a `PKG_MEGA` arm (two code
  paths to keep), and hot loops (Paint, Frotz, the mixers) change shape on
  the machine the project is measured on.
- **(B) A GRANULAR claim**: the kernel maps a claim with one LDT entry per
  128 bytes, consecutive, so `selector + 8k` names granule k exactly as
  `segment + 8k` does today - inside that claim and nowhere else. Every
  granule-arithmetic site works unchanged on kern_mega and no package byte
  changes on any other kernel. It costs 8 bytes of LDT per 128 bytes of
  such a claim (6.25%) while the claim is held: Paint's 150KB VGA canvas
  ~9.6KB, a 508KB Frotz story ~32KB, nothing at all on a desktop that holds
  no big claim. The GDT is still ~4K and the LDT still starts at 2K.

What (B) cannot express is converted either way: the ROM biases (a biased
SUBSEG), CS writes (a writable alias), framebuffer immediates, DMA rings
(the linear-base door), ROM scans, CODESEL inside relocation procs (the
pair rule), the parts library's off-grid slack, and the exempts' remaining
sites.

**Recommendation: (B) for in-claim granule arithmetic, (A) for the rest.**
Work common to both proceeds while the owner decides: the growable LDT,
the scratch selectors into the LDT, the doors, the video/font/ROM selectors
at the switch, the kernel's own conversion, and the drivers.

**The owner's answer (2026-10-25): (A).** (B) nets a saving everywhere -
nothing on a desktop holding no big claim, ~64K with Paint's 150KB canvas,
~42K with a 508KB story - but it gives memory back exactly when a big
document is open, and the goal is to shrink usage. (A) costs work, not RAM.
**How (A) is taken:** every site is converted. Where a conversion reshapes
a hot 8088 path, it is measured (MartyPC, guest cycles): no slower and it
is one shared path; slower and it is a `PKG_MEGA` arm built onto kern_mega's
own disks (SPEC.md 20.18), the shipped build keeping its loop.

#### MC7.3 The order of work

Packages and drivers convert BEFORE the kernel stops handing out identity
segments, so each conversion is checked on today's kernel and every kernel
keeps working at each step:

1. **The doors**, on every kernel (new cells at the table's end, no
   `PKG_FMT` bump): the LINEAR base of a segment or selector (`seg*16` on a
   real-mode kernel); a selector over a PHYSICAL paragraph range (the
   paragraph itself on a real-mode kernel); a selector's REAL segment; and
   `OSAPI_MEM_SUBSEG` answering for a CLAIM and for a conventional region,
   a BIASED base allowed (the ROM banks). Each with its real-mode answer, so
   a converted package needs no `CPU_F_PM` test for it.
2. **The shared layers**: the parts library, the REHOME macro, `crt0`'s
   overlay binder, `os88type`, `os88xgap`, `os88con`.
3. **The drivers** (SOUND, HDD, RAMDISK, HTTPFS, EMS, USBMOUSE; the three
   overlays' far pointers).
4. **The packages**: the six exempts, the big-claim arms (Paint, Frotz,
   Tracker, ModPlug, MIDIRack, the Video Player), the ROM biases, the
   framebuffer immediates, the scans. `tests/megahigh.txt` empties.
5. **The kernel flips**: every claim a selector from `mem_claim_1`, pairs
   for everything CS-able, the TI predicate, the compactor's linear copy and
   rebase, the file layer's linear DMA authority and 64KB split, the
   DISKOPS cloner and compressor, the LZ decoder, the video/font/ROM
   selectors and the boot captures at the switch.
6. **The tables**: the scratch selectors into reserved LDT entries, the
   static aliases gone, the LDT all allocator from 2K and growing, the GDT
   cut at `.cold`'s selector. A missed site is a `#GP` past the limit,
   reported at its instruction (KERN-MEGA-PLAN 14.42).

#### MC7.4 Step 5's design: the kernel stops handing out identity segments

**Taken before the packages** (a change of 7.3's order, for a reason): a
package converted against today's kernel is only checked against the
identity map, which hides exactly the mistakes a conversion can make. With
the kernel flipped first, every converted package is booted against the end
state as it lands. Packages not yet converted break on the branch meanwhile;
nothing ships between waves (0, rule 1).

- **A pair for every conventional claim.** `mem_claim_1`'s publish asks
  `.mega` for a code/data pair over the new block (16 bytes of LDT); the
  DATA selector is the claim's value, the CODE half is data - 8 for EVERY
  claim, so `OSAPI_MEM_CODESEL` and every relocation proc that converts
  without calling the kernel (`cc_ovbind`) can rely on the rule. The
  selector lives in a PARALLEL table, `mem_sel[MEM_MAX]` in `.lowbss`,
  indexed like `mem_tab`: `MC_SIZE` is in the claim snapshot's ABI and does
  not change. `MC_SEG`/`MC_PARA` stay physical; the allocator's bookkeeping
  (~95 lines) does not change.
- **Moves are rebases.** In protected mode the compactor copies a claim
  linearly (`mega_lincopy`, 32KB pieces, interrupts on between) and rebases
  every LDT descriptor based in the old block (the pool's `mx_desc` rule) -
  the pair, and any `SEG_SUB` selector inside. No relocation proc is called
  and none is needed: every holder holds a selector. So EVERY claim may move
  but a DMA claim with a head (a card may be reading it), one in an `int
  13h` transfer (`mem_in_xfer`), and one whose holder is mid-copy under
  `[sch_lock]`.
- **The switch translates.** At `mega_enter`, every live record gets its
  pair, and then the relocation protocol already in the tree runs ONCE per
  record with BX = its physical segment and DX = its new selector:
  `mem_rr_tab`'s rows (window records, instance pointers, driver rows, the
  disk layer's words, menu bars, ...) and every kernel holder's proc
  (`dsk_rah_reloc`, `ico_reloc`, `sc_reloc`, `asc_reloc`, `fm_reloc`, ...)
  rewrite their words exactly as they do for a move. What that protocol
  does not cover is listed by the census (`font_seg`, `bnd_seg`, the video
  words) and translated beside it. The IVT rows stay REAL: they are what a
  real-mode BIOS and the DOS trip read.
- **Lookups by value** (`mem_find_own`, `mem_owner_of_x`, `mem_is_region`,
  `mem_in_xfer`, `mem_fatw_dirty`) compare `mem_sel` in protected mode.
- **The TI tests** (about twenty) become an explicit question - "is this a
  pool block's?" by its descriptor's base - at each site.
- **Images**: a package region, a driver image and a module are pairs from
  the claim itself, so `MEGA_CSELTOP`'s pair arm, `mega_frame_cs`,
  `mem_reloc_call`'s alias and `mega_codesel` already do the right thing,
  and every `(S >> 2) | 4` arm is dead (step 6 deletes them). Moving one
  high (`ld_hi`, `drv_hi`, `mod_hi`) copies from a selector.
- **Sub-steps, each booting to a desktop**: (5.1) the pairs made, recorded,
  freed and rebased, while callers still receive the physical segment;
  (5.2) callers receive the selector, the lookups and TI tests converted,
  the switch's translation; (5.3) the images; (5.4) video, font and ROM;
  (5.5) the file layer's linear DMA authority, 64KB split and file drivers
  handed selectors; (5.6) DISKOPS's cloner and compressor, the LZ decoder,
  `cpc_fdtest`; (5.7) `OSAPI_MEM_HUGE`'s conventional arm as a selector run,
  with a 16KB step beside 32 and 64.

### MC4 - the HMA is RAM; the hibernate is the way out (D4, D5)

- The HMA's record is unpinned; after MC3 it is the bottom 64K of the free run
  and fills nearly last.
- A DOS trip saves the blocks in [1MB, 1MB+64K) with the park and restores
  them at the same linear addresses.
- **When the park has no room**, the trip offers a hibernate. That needs a
  **streaming resume**: the extended runs read straight to their final linear
  addresses a chunk at a time through the 32K bounce, conventional memory last
  through a stub in the text framebuffer and an extent list (§87.5's shape).
  The resuming boot takes nothing from the pool before the resume; its fixed
  zone lands where the image's did (A5).
- Costs said out loud: the image is the size of the memory in use (~3.9MB on
  the 4MB example; the volume's room is checked, and a short volume refuses
  with that reason), and the write and the resume move it all.
- Rows: `megadosp`, `megakbtrip`, `megahiber`, `megap2hiber`, `megatrap0`;
  new: a block in the HMA across a DOS trip, compared after; a DOS trip with
  the pool held full, which offers the hibernate, takes it and resumes.

#### MC4.1 The design, as decided (2026-10-25)

Read off the tree after MC7's kernel steps. Two halves, built in this order.

**(a) The HMA is pool.** `mega_xminit` stops writing the pinned `XMW_HMA`
record, so the pool's first 64KB is ordinary memory that MC3's top-down
placement reaches last. What still needs the HMA is the DOS trip alone
(`kern_dos` runs there since KERN-MEGA-DOS-PLAN W4), so:

- `mx_alloc` takes a FLOOR, `[mega_mxflr]` KB from the pool's base (0
  outside a trip): no placement under it, and its pack (`mx_packupf`) starts
  there, so nothing under it moves and nothing new lands in the HMA.
- `mega_dosclaimf` raises the floor to 64 for the park's and the arena's
  claims and drops it after; if any block stands in the HMA at that moment,
  the park is claimed 64KB larger.
- `mega_dosparkf` copies [1MB, 1MB+64K) into that tail at the snapshot, sums
  it with the park's image, and lists ONE copy back for the stub, which makes
  its copies before it restores conventional memory (megapark.inc's `.xcopy`,
  the W5 list). A pool block across the HMA's end is summed only above it:
  what is below is `kern_dos`'s during the trip and the copy's after.
- Nothing else names the HMA: the hibernate's run list and `mx_canmove` drop
  their `XMW_HMA` tests, the Task Manager's (`tm_himax`) are dead and harmless.

**(b) The resume streams.** Today's resume (1.6) reads the whole file into
one staging block above 1MB clear of every run the image had and every block
the fresh boot holds, and refuses when there is none - which is exactly the
case D5 exists for (the pool held full). No staging place can exist then: the
image's runs cover the pool and its conventional picture is ~600K that can
only land in conventional memory, last. So the resume leaves protected mode:

- The fresh kernel reads the section's head (as today), checks the runs
  against this machine's layout (`hbm_xruns`, A5), walks the file's EXTENTS
  (SPEC.md 87.5's `hbm_extents`, bounded by `HS_XMAX`; more is a refusal with
  the fresh session intact), stages them, the run list and the image's GDTR,
  IDTR and wake address in the text page as DATA, and leaves protected mode
  by the DOS handover's own route (`mega_dosgo`'s tail: the 8259 back at 08h,
  CR0 on a 386, the shutdown-0Ah reset on a 286).
- A REAL-MODE stub then does all of it with the BIOS: the section first,
  sector runs through a conventional bounce and `int 15h AH=87h` to each
  run's linear address (the tables, run 0, included - nothing reads them in
  real mode, A2), summing as it goes; then the conventional picture by the
  extents, kern_big's `hbstub.inc` loop; the IVT last.
- **Bochs will not fetch an instruction from the text framebuffer**
  (megados.inc's `mega_hbgof` note), so this stub does NOT run there, as
  kern_big's does: it runs from a few KB of CONVENTIONAL memory, whose
  image sectors it lands in the text page as data and puts back last, from a
  256-byte tail at 0000:0500 - the half of file sector 2 that no kernel owns
  and no resume restores (SPEC.md 87.5). That tail opens A20, loads the
  image's GDTR and IDTR, sets PE and far-jumps to `mega_hbwake`, which now
  puts the 8259 back at 20h (`mega_pic_pmf`, as `mega_dwake` does).
- **A sum that disagrees can no longer be a refusal**: the fresh session is
  gone by the time the last sector is read. It is the park stub's answer
  instead - a sentence, a key and a restart. A read error is the same
  (kern_big's stub already says so). Every refusal BEFORE the drop (no
  image, a layout that differs, too many extents, a volume the BIOS cannot
  reach) still leaves the fresh session running.
- The same stub serves D5's route: a DOS trip with no room for a park
  writes the hibernate instead (kern_big's route), and `kern_dos` -DKD_MEGA
  takes `kd_resume`'s path with this stub at the program's exit.

Costs said out loud: the image is the size of the memory in use and the
resume moves it all through the BIOS; on a 286 each `int 15h AH=87h` is a CPU
reset in the ROM, 64 sectors a call at most. The stub's tail rests on
0000:0500-05FF being nobody's on every kern_mega machine, which is SPEC.md
87.5's own assumption for kern_big.

#### MC4 (a) as built (2026-10-25) - DONE

- As 4.1 (a) says, with two findings on the way.
- **Two MC7 step-6 regressions on the DOS and resume paths**, found by the
  new row and fixed with it: `hbm_stageseg` loaded B800h/B000h - no
  selector since the identity map stopped at the kernel - so every DOS
  trip faulted the kernel after the park (`[k_vsc]`/`[k_vsb]` now, and
  `mega_dosgo` banks the REAL segment for `mega_dropr`); and `FONT_PICK`
  read the planar ROM as F000h in `hbm_wake`, which runs after every trip
  and every resume (`FONT_F000`, `[k_vsf]` on kern_mega, and the heap copy
  told from a ROM by `mega_s2segf`). `megakbtrip`, `megadosp` and
  `megahiber` all cross both and had not run since step 6.
- **And three more on the hibernate's own path**, found by running
  `megahiber` before building (b) on it: `hbm_psum` loaded each claim's
  `MC_SEG` - the PHYSICAL paragraph, which MC7.4 keeps physical - into ES
  (`hbm_pses_x` aims `MEGA_SEL_XA` there now); `hbm_xwconv` appended its
  chunks from identity segments n x 800h (a `KSEG` scratch, K4); and
  `hbm_xbounce` took the bounce's linear base as DX x 16 where DX is a
  selector, which would have copied the extended section from the wrong
  place without a fault (`mega_selbasef`). `megahiber`, `megap2hiber`,
  `megakbtrip` (10/10) and `megadosp` all pass after.
- Cost: kern_mega `.text`/`.bss` +4 (`[mega_mxflr]`, `[mega_dhkb]`,
  `[mega_dhfree]` less the HMA record's code), `.mega` +~170; kern_big and
  kern_small byte-identical (`kernel.sys` and `HIBER.DRV` compared at one
  commit).
- Row: `megahma` (Bochs, 14s): a 16KB pinned block with a pattern at the
  pool's base across one KBTRIP trip. Red with `[mega_dhkb]` held at 0:
  64 of 64 dwords overwritten by `kern_dos`.

#### MC4 (b) as built (2026-10-25) - DONE

- As 4.1 (b) says. `kernel/megares.inc` is the stub (~700 bytes in
  HIBER.DRV's `.modh`, nothing resident) and its 256-byte tail; `hbm_xres`
  stages it, `mega_hbgor` leaves protected mode by `mega_dosgo_lo`, and
  `mega_dropr` needed NOTHING new: its runs-park placement already puts the
  16KB stage at `[mem_top]` - 400h and enters it. The stub's run list is the
  section head's, its geometry `hbm_geom`'s, its extents `hbm_extat`'s over
  the WHOLE file (the picture's chunks and the section), bounded by
  `HS_XMAX` - a file in more runs than that is refused, `Hibernate:
  fragmented`, with the fresh session intact.
- **The text page is data only**, Bochs's rule: the stage's own 16KB are
  the last 32 sectors of the picture and land in the text page, and the
  tail swaps the stage's cells (0x400-0x4FF: HS_CLK and the rest
  `hbm_wake` reads there) back into it as it copies those bytes home.
- `mega_hbwake` puts the 8259 back at 20h (`mega_pic_pmf`) and ES on a
  selector. Deleted: `hbm_xfit`, `hbm_xclaim`, `hbm_xunclm`, `hbm_xneed`,
  `mega_hbgof`, and `hbm_xplan`'s room test - so the hibernate no longer
  refuses a full pool, and the System menu greys it only on a machine with
  no pool at all (it greyed a 1MB AT before).
- kern_big and kern_small byte-identical (`kernel.sys`, `HIBER.DRV` and
  kern_small's `kernel.bin` at one commit).
- Rows: `megahiber` passes on the new route (`kern_mega: resumed`,
  MEGAHIB's 64KB back); `megahibfull` (new) is the case itself - 847K of
  pinned filler covering every free KB of a 950K pool, hibernated and
  resumed, filler and MEGAHIB back - and on the tree before the hibernate
  never happens. Both under Bochs, a 386: a 286's reset drop and its `int
  15h AH=87h` are the DOS trip's own and have run there, but this route
  has not, and wants 86Box's `286-mega`.
- **D5 is built** (the same day): a DOS trip whose park claim fails - under
  `MPK_MIN_KB` of pool to be had - takes kern_big's route when there is a
  fixed disk: `hbm_dosrun` writes HIBERNAT.IMG (HBP_DOS) through the W5
  writer, `mega_dosgo` hands over with NO park (the stage under
  `[mem_top]`, the program the whole machine, no park header or W7 anchor
  written - those would have gone to linear 0), and at the program's exit
  `kern_dos -DKD_MEGA`'s `kd_resume` declines - a kern_mega image is
  `megares.inc`'s, not `hbstub`'s - so W6's reboot runs and the boot
  resumes the image with no question, through the streaming resume. A
  machine with no fixed disk still refuses with nothing torn down. Row
  `megadosroom` (Bochs, 2MB, every free KB of the pool held): no park line,
  KBTRIP's key, `kern_mega: resumed`, the DOS window showing the exit code
  posted into the restored session; red on the tree before (the trip
  refuses, KBTRIP never starts). A live resume from `kern_dos` (no reboot)
  would need `megares.inc` staged from there - not built; the reboot is
  W6's route and it works.

### MC5 - one claim for all of it (D6, D7, D8)

- **The exact answer:** pool free + purgeable below the caller's rank (one run
  after MC3's pack) + conventional free less the 64K reserve, less the seam's
  waste. A new answer of `OSAPI_MEM_HUGE` (AX = 0 asks) or a cell of its own,
  per §20.2.0.1.
- **The bridge, by default:** when the pool alone is short, a plain huge claim
  is a pool piece and a conventional piece under one run of selectors. The
  FIRST piece is cut to whole 64KB windows (the kernel puts first the piece
  with the smaller remainder; waste under 64K). Each piece is one record. The
  32KB-step form (`MX_FH32`) overlaps its windows and is never bridged.
- **The reserve:** 64K of conventional memory left free, one run.
- Rows: a new one on a 4MB machine (Bochs, and `vm/286-mega`): ask, claim the
  answer, a pattern through every window including the seam, then with the
  claim held open the Control Panel and hibernate; free, ask again, equal to
  the Task Manager's free figure. Plus `megavidbig`, `megavidv2`.

### MC9 - what still goes low (14.53, 14.60) - a later round

- First, 14.60's lead: an AT's 8237 page register may make an 8-bit DMA ring
  above 1MB legal on every kern_mega machine. If so, the rings, `SOUND.DRV`'s
  pool and the 32KB bounce all move.
- BIGSP in place stays with the Video Player round; `mx_tab` stays low by the
  owner's deferral (14.51).

---

## 6. The whole round, estimated

About **200K** of the fixed area's 256K comes back to programs (50 padding +
64 HMA + ~60 GDT + ~14 LDT), the caches standing come back when needed, and
every free byte but the 64K reserve can be had by one claim. On the 1MB
machine with no extended memory reported, the tables' 82K of conventional
memory becomes ~8K.

---

## 7. As built

Each wave records here what it came to: the bytes (`.mega`, `.cold`, `.text`,
the megaseg overhead), what it measured and on what, what it found, and its
rows. Nothing is recorded as built that was not.

### The round as built (2026-10-26) - DONE but for MC9 and one line

**What the round came to:** MC1, MC8, MC2, MC3, MC7, MC4 and MC5 are built, in
that order (D11). MC9 is deferred, as section 5 planned it.

- **Every claim is a selector** under protected mode, below 1MB as well as
  above it.
- **The GDT stops at `.cold`'s segment**, and the LDT sits on it and grows
  with the machine.
- **The HMA is ordinary pool.**
- **A hibernate needs no room to resume**, because the resume streams. So a
  DOS trip with a full pool hibernates instead of refusing.
- **One claim can take all free memory**, bridging the pool and conventional
  memory and leaving the 64KB reserve.
- **Packages:** every package and driver the MC7 census named is converted,
  on one shared path unless the XT measured slower.
- **`tests/megahigh.txt` is EMPTY**: its last line, RealDMG's `high`, went
  on `mega-dmg` (2026-10-26) when the arm and `gbaot.py --mega`'s package
  declared `OS88_HIGH`, run above 1MB by `tests/rdgmega.py` and
  `tests/rdgaotmega.py` (REALDMG-SPEC 16.2.7). RealDMG's conversion itself
  (item 11 of the package wave) is DONE, both builds and the precompiled
  package, merged at `3bc7f0be`.

**kern_mega's own limits, as the owner set them (2026-10-26):** it is a knob
build that is MEANT TO GROW.
- **No budget and no 128KB boot bind it.** Its limits are 64KB a segment,
  and `.mega` is where new protected-mode code goes.
- **`KERN_KB` may run to 255.** `osapi_sys_kb` widens `vga_skkern`
  unsigned there; kern_big and kern_small keep `cbw` and 127.
- **Its boot floor is a 1MB machine.** Its own guard 5 checks that the image
  fits 640KB of conventional memory, where stage 1 reads it.
- Merging `elendilon`'s sector-cache change took `KERN_KB` to 128, which the
  old guard refused. That is how the limit was found.

**The final pass:**
- **The soak union** was 128 rows chosen by reach: every kern_mega row, plus
  each converted package's rows on kern_big. 113 passed and 3 skipped for
  host tools this box lacks (`x3compare`, `hmahiber`, `p2hiber`).
- **None of the 12 failures was a kernel or package defect.**
  - **Mine:** six were my own `make` racing the pass, or a `wants=` gap. They
    pass on rerun.
  - **Harnesses behind MC7:** three.
    - `megarehome` counted a GDT scratch, because the LDT now shares the
      GDT's base, so Bochs lists those entries too.
    - `x3xtunit` and `x3xtexact` run the XT part with no kernel under it, so
      nothing answered `OSAPI_SEG`.
  - **Browser rows:** `brarena` and `brdownload` fail 2 of 2 on the round's
    base as well. All four of them (with `brmem` and `brfetch`) pass on
    `elendilon`'s soak fixes.
- **`test-full`:** 76 rows green after two ledgers were paid. `tests/pins.txt`
  re-keys the two claim sites MC7's part fetches moved; the user guide's
  Hibernate page describes the streaming resume.

**Left, and whose:**
- **MC9** (section 5): what still goes low.
- ~~RealDMG's `high` line~~: DONE on `mega-dmg` (REALDMG-SPEC 16.2.7).
- **The speed pass's completed #GPs**, writes through CS that work and cost
  a fault each:
  - Sheet's macro module: 164 a macro run.
  - The shipped PiXEL's PXSW stores: about 290 sites.
  - Word's and Scribe's undo.
  - RealDMG's.
- **`drv_fs_call`'s 512-byte bounce** for every claim (RAMDISK.DRV, NET.DRV).
- **`dskw_hisub`'s windows**, made per 64KB crossing and freed only with
  their claim: bounded, not leaked, and never measured for accumulation.
- **8.6 (a 64KB file into a 64KB pool claim)** was still open at the
  round's close, missed from this list. It was fixed the next day; 8.6 has
  the cause.

### MC1 as built (2026-10-25) - DONE

- **The record grew a word, shared with MC3.** `MX_BLKSZ` 8 -> 10, the new
  `MX_AUX` (offset 8) holding a cache's tag; MC3's token will use the same
  word for an `OSAPI_XMEM_ALLOC` block, the two never applying to one block.
  `MX_FPURGE` (4) is the `MX_FL` bit. `.lowbss` +192 bytes for 96 records.
- **`mega_hiclaim` takes the tag in BX** and gets the block's selector
  through `mega_xmsel`, so it lives in `MX_SEL` and `mx_free` frees both. A
  purgeable tag marks the entry; any other is pinned. Callers: `mem_claim_x`'s
  kern_mega arm (BX was already the tag), `mem_hicaches_f` (`MEM_P_ICO`) and
  `dsk_rah_wanthi`, which passed the read-ahead's WIDTH in BX and now passes
  `MEM_P_DIRW` - so the 32K store at the pool's top is now a HIGH cache, as
  its conventional twin always was. `mega_hiclaim2` (module pairs) passes
  BH = 0, the top rank.
- **The shed** is `mx_shedf` in `memory.inc` (`.cold`, `%ifdef KERN_MEGA`),
  beside `mem_shed_one`, using `mem_rank_bh` and `mem_pg_forget` unchanged.
  `mx_alloc` calls it (through `mx_shed`, which sets the rank: BH for
  `MX_KOWN`, the top for everyone else) at `.norun` after the compaction it
  already tried, and at `.full`, and on success runs the whole walk again
  with the compaction re-armed.
- **The walk** writes the level at `XMW_CLS` for such a block (owner FEh) -
  additive, `abi.lock.json` refreshed with no bump. The Task Manager files it
  under `Purgeable` as TYPE `Cache` with that TIER (+36 bytes of image).
  Seen under Bochs: `Purgeable 77K` = the conventional read-ahead landing
  (5K, HIGH), the read-ahead store (32K, HIGH), B:'s FAT window (5K, MED),
  the icon store (5K, TRIV) and the About box's raise cache (30K, TRIV); the
  `Kernel` group, which used to hold the 32K, is gone from the list.
- **Bytes:** kern_mega `.mega` 14,698 -> 14,837 (+139), `.cold` 42,207 ->
  42,332 (+125), `.text`/`.bss` +0 (megaseg overhead 3,714, unchanged);
  kern_big and kern_small byte-identical at one commit.
- **Row: `megashed`** (soak, Bochs, 14 s): caches standing, every free KB
  covered, the Control Panel opened; CTRL.DRV must go above 1MB, shedding
  the icon store and the raise cache (both TRIV) and leaving the FAT window
  and the read-ahead store. Green; and red with `call mx_shed` taken out
  (CTRL.DRV stays low, nothing shed) - verified.
- SPEC.md 50.6.8 is the contract; 28.4.11's cell table names the byte.

### MC8 as built (2026-10-25) - DONE

- **The folds, in the units a segment's value allows.** RAMDISK.DRV's
  `rd_fold`, NET.DRV's `net_norm` and CDROM.DRV's `cd_bnorm` now share one
  rule:
  - real mode folds paragraphs, exactly as before;
  - under `CPU_F_PM` an IDENTITY (GDT) selector folds whole 128-byte
    granules, the kernel's own `dskw_norm` rule;
  - an LDT selector does not fold at all - its offset walks.

  The LDT arm is not reachable today, because the kernel bounces any selector
  buffer through `drv_fs_hi` 512 bytes a call. It is in place for MC7, which
  hands drivers selectors directly. NET.DRV banks `OSAPI_CPU_INFO` at attach
  (`net_cpuf`), as RAMDISK.DRV already did, and so does CDROM.DRV
  (`cd_cpuf`).
- **CDROM.DRV says `OS88_DRV_HIGHOK`** (no arithmetic on its own CS or DS, PIO
  only). Seen under Bochs on `make megacdtest`'s disk: `kern_mega: above 1MB:
  CD-ROM`.
- **`t_megagrid` scans every driver**, not only the packages that declare
  `OS88_HIGH`. It flagged two A20 probes, both real-mode-only, registered with
  their reasons. NET.DRV's `CPU_F_PM` test is registered in
  `tests/megapkg.txt` (a correctness test every build needs, ~25 bytes).
- **AUTOSTART is walked after `mega_enter`.**
  - The walk is ONE macro body (`AS_WALK`, `kernel/desksc.inc`) assembled as
    `as_boot` in the blob on every other kernel and as `as_bootc` in `.cold`
    on kern_mega. `kmain` calls the latter last, after the drivers and caches
    go up.
  - The kern_mega arm holds no data and no CS idiom (`.cold`'s rules 1 and
    2): the folder's name and the skip toast are immediate stores, and the
    find's answer is a 24-byte STACK frame popped before anything opens.
  - Seen under Bochs: the autostarted CDRack's segment is `284Ch`, a pool
    selector, where it was `9640h` in conventional memory.
  - A finding on the way: `mega_tasks_ok` REFUSED protected mode for the
    whole session when an autostarted package had a worker running. A walk
    after the switch cannot cause that.
- **Bytes:**
  - kern_mega `.cold` +221 (the walk; `.mega` unchanged), shared window +4
    (the call; megaseg overhead 3,718 of 4,029);
  - kern_big and kern_small byte-identical with a clean build of the same
    commit;
  - RAMDISK.DRV, NET.DRV and CDROM.DRV each a few bytes larger.
- **A method note.** A stash-and-rebuild comparison was WRONG here: make
  keeps generated files (the packed kernel's defines) from the dirtier
  build, so the stashed "baseline" mixed two trees and showed a 1-byte
  difference in stage 2 that a clean build does not have. Byte-identity is
  checked against a clean worktree of the same commit from now on.
- **For MC7, found here:**
  1. Once every buffer is an LDT selector, keeping `drv_fs_hi`'s 512-byte
     bounce would make every file-driver transfer 512 bytes a call - over the
     LapLink cable a large slowdown. MC7 hands file drivers the selector
     directly and SPLITS a transfer at the selector's 64KB limit. The drivers
     are ready for that now.
  2. `mega_tasks_ok`'s premise (real-mode frames from before the switch)
     remains for anything else that runs before `mega_enter`.
  3. **Every claim made in real mode before the switch** (the FAT windows,
     the read-ahead landing, the icon store, the shortcuts table, the
     ASSOC.DAT cache, drivers' claims, the settings buffer) holds a real
     SEGMENT in a kernel word. Once the GDT stops covering the heap they
     must be given selectors AT the switch, holder by holder:
     `mem_hicaches_f` and `drv_hiboot_f` are the pattern, and MC7's census
     has to list every one.
- Rows named: `megagrid`, `megapkg`, `cswrite` (fast); `megacdrom` (86Box,
  now asserting both the driver and the autostarted CDRack above 1MB);
  `rdmove`, `rdup`, `rdpreserve`, `rdxms`, `rdcz`, `rdicon`, `rdmount`,
  `netcoexist`, `netpromise`, `netinst`, `cdrom`, `cdromsim`, `cdromempty`,
  `autostart` (the drivers' and the walk's real-mode paths).

### MC2 as built (2026-10-25) - DONE

- **The layout is a pure function of `mega_extkb`.** `mega_layoutf` (`.cold`,
  far, no state) answers it, so the switch and the park's probe
  (`mega_wrprobe`, which must find the anchor before anything else is known)
  compute the same places:
  - the top is 1MB + `[mega_extkb]`, capped at 16MB;
  - the CODE area, `MEGA_CODE_KB` (`.cold`, `.mega` at its rung's distance,
    the park's scratch), is the top KBs - 59K today;
  - the TABLE block, `MEGA_TBL_SIZE` = 82KB (GDT 64K, LDT 16K at
    `MEGA_LDT_OFF`, IDT 2K at `MEGA_IDT_OFF`), is under it, its GDT end
    next to the pool, where MC7's shrinking leaves the room;
  - the pool runs from `MEGA_XPOOL` = 100000h to the table block:
    `[mega_fixkb]` KB, which `mega_xminit` takes as `xm_kb`.

  Measured under Bochs: 16MB puts the tables at FDCC00h and the code at
  FF1400h with a 15,219K pool; 2MB puts them at 1DCC00h/1F1400h with 883K.
  The pool was 14,976K on 16MB before (the old base was 140000h, and 64K of
  padding came with it).
- **Every table address is a variable.** `mega_tset` (DX:AX = the block's
  base) writes `mega_gdtr`, `mega_idtr`, `mega_ldtlin`, `mega_tbllin`, the
  bootstrap window and the LDT/IDT descriptors in one place; the switch calls
  it with `mega_layoutf`'s answer and `mega_tlowf` with its conventional
  block's. `mega_coldup` reads `[mega_coldlin]` (`mega_hilin`, a `.text`
  helper, adds `.mega`'s rung distance) and gives `.cold`'s descriptor base
  coldlin - `COLD_VSTART`. `[mega_codehi]` says whether the code went up.
- **The HMA is the pool's first block**, 64K pinned, owner `XMW_HMA` (F0h).
  MC4 is what unpins it. The hibernate's run list and the DOS park's
  "everything the pool gave out" both skip it: it holds nothing of the
  session.
- **`XMW_FIXED` names three areas above the pool** (GDT, LDT+IDT, kernel
  code) at POSITIVE offsets from the base - KB past `xm_kb` - and no
  padding; the HMA is listed as the block it is. `mega_xmfix`'s table is
  gone; `XMW_PAD` stays in the SDK unused.
- **The DOS park**: `mega_dsave`'s entries are offsets from the table
  block's base and the dreg list adds `[mega_tbllin]` (`.tbl` for an
  offset under 64K, `.tblp` for the IDT's, given in paragraphs); `.cold` and
  `.mega` are saved from `[mega_coldlin]`. The parked stub's own copy of the
  GDTR/IDTR (`mpk_tabs`) is patched at staging by `mega_dstubup`, 12 bytes
  from `mega_gdtr`/`mega_idtr` (an assembly check holds the two adjacent).
- **The hibernate** writes run 0 from `[mega_tbllin]`, `MEGA_TBL_SIZE` long,
  and a restore REFUSES an image whose run 0 is not exactly that: an image
  from before MC2, or from a machine with different RAM (D10).
- **The Task Manager** reads the fixed areas as KB OUTSIDE the pool (above
  its top now, where kern_big's are under its base), so RAM stays the whole
  machine: `Ram 0.31/3.62M` on a 4MB Bochs machine, `System>1MB 205K` = HMA
  64 + GDT 64 + LDT/IDT 18 + code 59. `tm_himax` treats the HMA's record as
  the pinned block it is.
- **Bytes:** kern_mega `.text` +32, `.cold` +117, `.mega` +110 (shared
  window overhead 3,718 -> 3,750 of 4,029); `.boot2` +12 (the blob,
  discarded); the Task Manager +48 of image; kern_big's `kernel.sys` and
  kern_small's `kernel.bin` byte-identical with a clean build of the same
  commit.
- **A method note, second of its kind.** `make build/kernel.bin` alone is
  PASS 1 of the packed kernel (SPEC.md 2.9.13): the blob carries placeholder
  `KZ_*` defines, so it differs by a byte from a tree whose last build was a
  full `make`. Compare `kernel.sys` after a full `make` in both trees.
- **`tools/os88megaseg.py`** prints the code area against its 255K cap (a
  KB byte in the walk's record), the second wall the old slot was.
- Rows named: `megahigh` (full); `megashed`, `megahiber`, `megap2hiber`,
  `megadosp`, `megakbtrip`, `megarehome`, `megapiclose`, `megacdrom`,
  `tmowner` (soak).


### MC3 as built (2026-10-25) - DONE

- **`mx_canmove`: every block moves but the HMA and an `MX_FPIN` one.**
  - `MX_FPIN` (an `MX_FL` bit) marks a block whose LINEAR address something
    holds: the DOS trip's park and arena (now owner `MX_KOWN` instead of FFh,
    pinned by `mega_dosclaimf`'s `.alloc`) and a hibernate resume's staging
    block. Both live only for the moment they are used.
  - **A deviation from 5's MC3, in the direction of the goal.** The plan
    pinned a module while its action runs, kern_big's rule. On kern_mega
    that rule is not needed: a module above 1MB is named only by SELECTORS
    (its pair, `dkv`/`thv`'s far pointers, `mega_dklibf`'s lone selector),
    the compactor rebases every LDT descriptor in a block it moves, and a far
    return or a pop reloads the base. That is the same argument a package's
    own code region moving while it is the caller has stood on since
    KERN-MEGA-PLAN 14.46 (`megaown`). So nothing is pinned for being in use;
    MC5's "nothing pinned but a module in use" is met with room to spare.
- **The token** (`MX_AUX` for owners 00h-7Fh and FFh, `mx_istok`):
  - chosen at the take where the block is (`mx_tokfit`), so the two agree
    until the first move; never overlapping a live token; never 0;
  - every cell finds the block by it and goes to where it is now: `mx_free`
    and `mx_chk` through `mx_key`, `mx_copy` and `mega_xmsel` through
    `mx_tophys`, `mega_xmrunf` (`.cold`) inline;
  - the census (a subagent read every caller) found nothing that reads a
    token in place under protected mode, so A3 holds. Its list of what the
    kernel must keep true - never 0:0, bounds on the KB-rounded size, FFh
    blocks from driver context, XMEM_RUN blocks moved with care - is met;
  - the Video Player's DRAM-interleave gap (`os88xgap.inc`) reads a token's
    low bits as physical: now a guess, speed only, never correctness.
- **A defect found by the new row, kept as its red case.** `mx_tophys`
  subtracts a SIGNED distance, and a block that moved UP leaves a borrow in
  CF; `mx_chk` handed that back as "not your block" (`MEGAXM: no 03 0002`).
  It ends in `clc` now.
- **An `OSAPI_XMEM_RUN` block** moves like an `MX_FH32` claim: cut every
  64KB with the package's rate hook held (`mx_cpflags`).
- **Top-down for every owner**: `mx_alloc`'s bottom-up arm is gone; a miss
  packs UP (`mx_packupf` from KB 0) and retries, for every owner, before the
  shed. `mx_compactf` stays for `OSAPI_MEM_REGROW`'s `.hpack`.
- **The Task Manager**: `tm_himax` replays the pack with only the HMA
  pinned.
- **Tests:**
  - `tests/megaxmove.py`, a new soak row: MEGAXM (tests/megaxm) gained a
    MOVE step - a 64KB filler above its block, freed, then a claim of every
    free KB - and reads every chunk back through the first token. Seen:
    `MEGAXM: move 0AA7 0AE7` and `MEGAXM: ok` at 4MB, 16 s under Bochs; red
    with token blocks pinned (`MEGAXM: no 42`). The row writes the packed
    `ZBIG.TXT` MEGAXM's file leg reads, which KERN-MEGA-PLAN 14.70 left to a
    hand-passed `--put`.
  - `megaown` asserts its region moves UP now (MEGAHOLE's hole is above it).
  - `megashed` and `tools/megaconvres.py` cover the pool with `MX_KOWN` +
    `MX_FPIN` blocks, so the pack leaves their cover where it is.
- **Bytes:** kern_mega `.cold` +15, `.mega` +112, `.text` +0 (shared window
  3,750 unchanged); kern_big and kern_small byte-identical with a clean
  build of the same commit; the Task Manager -12 bytes of image.
- SPEC.md 50.6.10 is the contract.
- Rows named: `megaxmove` (new), `megaown`, `megashed`, `megavidbig`,
  `megavidv2`, `megaovl`, `megarehome`, `megahiber`, `megap2hiber`,
  `megadosp`, `tmowner` (soak); `megahigh` (full).

### MC7 as built, step by step

- **Step 1, the doors (2026-10-25) - DONE.** One cell, `OSAPI_SEG` (`0x04E3`,
  SPEC.md 50.6.11), five verbs in BL rather than four cells, because a cell
  is six bytes of every kernel's `.text`: `SEG_LIN`, `SEG_REAL`, `SEG_WIN`,
  `SEG_SUB`, `SEG_FREE`. Every real-mode kernel answers the caller's own
  arithmetic. On kern_mega a window is recorded per owner in `mega_wtab`
  and freed by `mx_relall`; a SUB inside a pool block is based in it, so it
  moves with it and `mx_dfree` takes it with a claim's free. Checked by
  MEGAXM under Bochs (the `megaxmove` row): a SUB read through, no real
  segment above 1MB, a ROM window reading the BIOS date, its linear base,
  the free. Cost: `.text` +6 everywhere, `.cold` +40/+39/+68, `.mega` +900,
  `.lowbss` +128. **Open for later steps:** the 1MB machine has no pool, so
  no `mx_relinst` runs there and its windows would not be freed at an
  instance's end - the teardown hook has to move when step 5 gives
  conventional claims selectors. `OSAPI_MEM_HUGE` gets a 16KB step (the
  Video Player's and RealDMG's rolling windows) in step 5. MEGAXM's
  `OSAPI_MEM_CODESEL` check asserts the static alias `(DS >> 2) | 4`, which
  step 6 retires.
- **Step 5.1, a pair for every conventional claim (2026-10-25) - DONE.**
  `mem_sel` (`.lowbss`, one word per `mem_tab` record) holds each claim's
  DATA selector, the CODE half at - 8. `mega_mpnewf` makes it at
  `mem_claim_1`'s publish (no pair, no claim) and `mega_mpall` at the switch
  for every claim real mode made; `mega_mpfreef` frees it with the record
  (`mem_free_x`, `mem_free_owner_x`, the shed, the compactor's drop) and
  every allocator data descriptor based in the block with it;
  `mega_mpmovef` rebases every descriptor based in a block the compactor or
  a regrow moved; `mega_mpsyncf` re-aims the pair when a regrow or a
  re-home's trim changes the block. Callers still receive the physical
  segment (5.2 changes that). Checked under Bochs at 4MB and 1MB (where
  every claim is conventional): every live record's pair matches its block,
  through a Disk window, the Task Manager and the Control Panel.

- **Step 5.2, callers receive the selector (2026-10-25) - DONE.** In
  protected mode `mem_claim_1` answers a claim's DATA selector, and every
  lookup by value (`mem_find_own`, `mem_owner_of_x`, `mem_is_region`,
  `mem_can_move`, `mem_fatw_dirty`, `mem_in_xfer`) compares `mem_val` -
  `mem_sel` in protected mode, `MC_SEG` otherwise; `mem_in_xfer`'s pinned
  transfer is a selector too and is tested by its descriptor's base.
  `mem_regrow` keeps the caller's value and does its arithmetic on the base.
  A compactor move in protected mode is a REBASE and calls no relocation
  proc, every holder holding a selector. **The switch translates**
  (`mem_xlatf`, `.cold`, straight after `mega_enter`): every live record
  whose base and selector differ runs the relocation protocol once, BX = the
  base and DX = the selector - the holder's proc where one is declared and
  the kernel's half (`mem_region_reloc`'s rows, the IRQ vectors and the IDT
  through them) for every record. The TI-bit tests that meant "above 1MB"
  became `MEGA_ISHI` (`mega_ishif`: an allocator selector whose base is at
  or above 1MB) in `memory.inc`, `loader.inc`, `mod.inc` and `driver.inc`;
  the ones that mean "any LDT selector" stay (the disk layer's bounce and
  `.drvhi`, `drv_fs_call`'s redirector bounce, the LZ decoder's 64KB
  refusal) until 5.5 and 5.6 hand those paths selectors properly.
  `dsk_runcap` does its DMA-page arithmetic on a selector's BASE, so a
  conventional claim is a direct transfer and only memory above 1MB
  bounces - which is also what keeps the read-ahead landing, itself a
  selector now, from bouncing into itself. **Modules (5.3's first piece,
  taken here because the boot needs it):** `mega_mods_fix` runs after
  `mega_mpall` and names each boot-loaded module by its claim's pair -
  `MODR_SEG` the data selector, its slots and the `dkv`/`thv` tables the
  code half, found by `mega_p2d` - and `mod_check`, `mod_hi`, `mod_hiboot`
  arm and re-arm with data - 8; DESK.DRV's library selector is freed with a
  conventional image too. The loader's compressed-package path converts the
  region's selector to its real segment before adding the prefix
  (`mega_s2segf`). Checked under Bochs at 4MB and 1MB: the desktop, a Disk
  window, the Task Manager (running from a selector region on the 1MB
  machine) and the Control Panel, every pair matching its block, and a scan
  of the kernel's `.bss` and `.lowbss` finding no word still holding a
  claim's physical base. Cost: kern_mega `.cold` +400, `.mega` +51, shared
  window +4; kern_big and kern_small byte-identical with a clean build of
  the same commit. **Known slow until 5.5:** a hard-disk volume served by a
  driver (`.drvhi`) and a redirector (`drv_fs_hi`) still move a selector
  buffer a sector at a time. Rows named for the final pass: `megaown`,
  `megaxmove`, `megashed`, `megaxm`, `megahigh`, `megaovl`, `megarehome`,
  `megahiber`, `megadosp`, `drvmove` on kern_mega.
- **Step 5.3, the images (2026-10-25) - DONE.** A package region, a driver
  image and a module are pairs from the claim itself, so the pair arms of
  `MEGA_CSELTOP`, `mega_frame_cs`, `mem_reloc_call`, `mega_codesel`,
  `mega_cs2ds`, `mega_dsimg` and the fault handler's `.pairq` already did
  the right thing; the modules' arming was 5.2's. What was left is
  `OSAPI_MEM_SUBSEG` (SPEC.md 50.3.6): it answered only a region placed
  above 1MB, and a conventional region is a selector now too, so the
  caller's fallback sum named some other descriptor. `mega_rgnconv` finds
  the region's claim by its selector and the rest of the body is shared;
  `mega_cdfree` frees a claim's code twins as well as its data
  descriptors, which is what a SUBSEG code window inside a conventional
  region needs at the free. Cost: `.mega` +48. **Found on the way, and a
  package's to fix rather than the kernel's:** HEAPFRAG (tests/heapfrag)
  walks each block it claimed by adding 40h paragraphs to the SEGMENT a
  kilobyte at a time - under Bochs on the 1MB machine it faults on the
  second kilobyte, `mov es, dx` with DX = the next pair's selector + 40h.
  The kernel handed it the right selectors (a trace of every pair made,
  freed and moved confirmed it). It joins the package waves' list.
- **Step 5.4, video, font and ROM (2026-10-25) - DONE.** The identity GDT is
  what made `mov ax, 0A000h / mov es, ax` work in protected mode, and A000h is
  GDT entry 1400h - 40KB into a table step 6 cuts at ~4K. So at the switch
  `mega_vfixall` (after `mega_mpall`) makes one data selector per real
  segment at or above A000h that the kernel holds (`mega_useg`, cached in
  `mega_ustab`: A000h, B000h, B800h, F000h, and an option ROM's if the font
  is there) and translates every such word: `vid_tab`'s column, the live
  block's `vid_seg`/`vid_rseg`/`vid_tseg`, each display context's three,
  the full-screen modes' rows (`fsx_mtab`) and `font_seg` - and a claim's
  base among them to its pair (`mega_p2d`: `bnd_seg` under `BANDCOMP`, a
  copied font). **The fourteen immediates** in the renderers, the font and
  icon painters, `vid_setmode`, `vid_mono_text` and the full-screen clear
  became `VSEG reg, seg`: the constant itself on every other kernel, and on
  kern_mega a word read through CS (`k_vsa`..`k_vsf`) the switch sets to
  the selector - one byte a site. **The trap, found by bisecting a boot
  that triple-faulted before the switch:** the words first lived in
  `.lowbss` and were read through SS, and the loader draws its splash
  through `vid_setmode` on ITS stack (SS = 97E0h) before kmain - and from
  IRQ0 inside the ROM's `int 13h` on the ROM's; then in `mega.inc`'s
  `.text`, which is not in memory yet while the image is still arriving.
  They sit beside `vid_tab` now, which that early code already reads, and
  `VSEG` refuses to assemble outside `.text` and `.vga`. The five sites in
  `.boot2` stay immediates: the blob runs in real mode only and is freed
  before the switch. The Hercules text row's B800h -> B000h edit is a WORD
  store on kern_mega (a selector's high byte is not the segment's).
  Checked under Bochs at 4MB and 1MB: the desktop, a Disk window, the Task
  Manager and the Control Panel drawn through `vid_seg` = a selector, the
  font through F000h's. Cost: kern_mega shared window +27, `.mega` +246;
  kern_big and kern_small byte-identical with a clean build of the same
  commit. **Not covered, and named for their waves:** the hibernate's
  `font_seg` test (`hiber.inc`, `cmp ax, 0xA000` to tell a ROM font from a
  heap copy - MC4), a return to real mode that draws (none today: the
  service reflection does not, and the DOS trip and the hibernate come
  back into protected mode), the `STKDIAG` knob's F000h, and the drivers'
  and packages' own frame-buffer and ROM immediates (their waves).
  **CORRECTED (KERN-MEGA-PLAN 14.75): the service reflection DOES draw** -
  14.1 answer 18 runs the mouse ISR there, and it loaded `vid_seg`/`k_vsa`
  as paragraphs, drawing every arrow it moved into kernel memory and
  leaving the one on the glass. `mega_vrm` makes the two words segments
  for each reflection that opens the mouse's line.

- **Step 5.5's first half, the file doors and the compactor's copy
  (2026-10-25) - DONE.** Both still turned a claim into a REAL segment and
  loaded it, which is the identity GDT step 6 cuts:
  - **`dskw_hiseg` sends every allocator selector down the selector arm**,
    below 1MB as well as above it: the body moves the OFFSET and never does
    arithmetic on the selector, and `dsk_runcap` (5.2) splits a run at a DMA
    page from the selector's base, so a conventional buffer is still a
    direct transfer. Only the kernel's own segments, which stay on the
    identity grid, take the real-segment arm. A PACKED file read into a
    selector at a paragraph offset gets a SUB based there (`dskw_subsrc`,
    tied to the claim) where the old arm moved the segment; offset 0 and
    every refusal are as they were.
  - **`mem_bpara` copies linearly in protected mode** (`mega_bparaf`: the
    chunk through `mx_lcopy`/`mx_lcopyb` with interrupts off, which is
    `mega_lincopy`'s rule for its scratch pair), forwards or from the top
    as the overlap needs.
  - **HEAPFRAG converted** (tests/heapfrag): a block is walked by its offset
    under one segment (64KB at most, `hf_s` capped there), and "did it move"
    compares the block's PARAGRAPH through `OSAPI_SEG`'s `SEG_LIN`
    (`hf_para`) rather than the value, which under protected mode is a
    selector that a move rebases and does not change; check 11 expects the
    holder to be told nothing under `CPU_F_PM`. **17 of its 18 checks pass**
    under Bochs on the 1MB kern_mega, where every claim is conventional and
    selector-named - 8 (every surviving word read back after the moves), 10
    (blocks moved) and 13 (the ceiling packs up and its contents survive)
    among them. **Check 14 fails there, and it is OPEN**: its "one KB more
    than the largest run" claim is granted out of a 52KB run at 7C40h
    without the page-constrained block moving, so on this layout the
    check's premise does not hold - a placement question to settle in the
    final pass, not a selector one (a trace of every pair made and moved
    showed no stale selector).
  - Cost: kern_mega `.cold` +81, `.mega` +59; kern_big and kern_small
    byte-identical with a clean build of the same commit.
  - Still to come in 5.5: the redirectors (`drv_fs_hi`) and a driver-served
    disk (`.drvhi`) still move a selector buffer a sector at a time - correct
    after the cut, slow until the drivers take selectors (their wave).

- **Step 5.7, `OSAPI_MEM_HUGE` below 1MB (2026-10-25) - DONE**, taken
  before 5.6 because 5.2 had broken it: a conventional huge claim answered
  its selector and CX = 1000h, and a selector + 1000h names some other
  descriptor. `mega_hugec` remakes the claim's pair as the entry before a
  RUN of data selectors over it - the run's first IS the claim's value, its
  code half at - 8 as for every claim - and answers CX = 8; every window is
  based in the block, so 5.1's rebase and free cover them with nothing
  added. **The 16KB step** (BX = 4000h) is in both arms - the pool's as
  `MX_FH16` with `MX_FH32`'s rules (not regrown, moved with the hook held) -
  for the Video Player's and RealDMG's rolling windows. MEGAXM checks it
  (steps 5D-5F: 96KB at a 16KB step below 1MB, a byte written through
  window 5 read back through window 4 at + 4000h, the free) and goes red
  with the windows filled at 64KB (`MEGAXM: bad 5E`). Cost: kern_mega
  `.cold` +244, `.mega` +15; kern_big and kern_small byte-identical. Not
  regrown past its first 64KB below 1MB - a conventional huge claim that
  grows is a package-wave question if one turns up.

- **Step 5.6's first half, the LZ decoder and a packed file past one
  window (2026-10-25) - DONE.** After 5.5 a packed file larger than 64KB read
  into a CONVENTIONAL claim had nowhere to go: the selector arm holds one
  window and refuses a stream that crosses it, and the decoder walks its
  input and output by PARAGRAPH. Now:
  - **`dskw_tolo` puts such a read back on the segment arm** - the buffer's
    real paragraph and an offset of 0..15 - when the base is under 1MB (above
    it the refusal stands, nothing names it by paragraph), and a plain file
    that would wrap the window from its offset takes the same road. Every
    segment register that arm loads goes through a scratch selector:
    `DSKW_WSEG` for the transfer and both staging loops, `mega_oddsegf` for
    the output, `mega_sldsegf` (MEGA_SEL_SLD) for the decoder's input.
  - **The decoder slides by descriptor, never by sum, in protected mode**:
    `lz_at` and `lz_copy` through `mega_slidef`, which now asks real mode
    first and otherwise slides EVERY selector by its base (the scratch ones
    included); a match reaching back over a 64KB boundary of the output
    through a third scratch, MEGA_SEL_BACK (`mega_backf`).
  - **The defect the row found**: a match's source that SLIDES re-aimed
    MEGA_SEL_SLD - the very selector the decoder's input was held in - so
    the `pop ds` after the match reloaded the wrong base and the raw tail
    came out 16 bytes off (`MEGAXM: bad 65`). A match source that may slide,
    or that sits on the ODD scratch the output's own bump re-aims, is now
    read through MEGA_SEL_BACK (`mega_bkaimf`), and `mega_slidef` slides
    BACK onto itself. The cost is one compare per match on kern_mega only.
  - MEGAXM checks it (steps 61-65: a 90,000-byte lz4 file read into a
    conventional `OSAPI_MEM_HUGE` run and compared byte for byte from 64KB
    to the end through window 4) and goes red with the fallback refused
    (`MEGAXM: no 61`). Cost: kern_mega shared window +12, `.cold` +297;
    kern_big and kern_small byte-identical with a clean build of the same
    commit.
  - The second half is the next entry.

- **Step 5.6's second half, the modules that do paragraph arithmetic
  (2026-10-25) - DONE, as "MC7.6".** DISKOPS.DRV's compressor, its join and
  the cloner, and CTRL.DRV's `cpc_fdtest`, all walk their claims as REAL
  paragraphs - 16KB slides, a 512-byte rounding of a DMA buffer, a buffer
  placed on a 64KB page by hand, `ES - 1:8` for a header in front of a
  window - and since 5.2 a claim's value is an LDT selector, on which that
  arithmetic names other descriptors. They had never run under protected
  mode since (the row below found the compressor's first fault on its first
  call). Converting each site to windows and offsets would have reshaped
  ~100 loads in 7,000 lines of a parse that runs at ~1,600 cycles a byte;
  what was done instead keeps every line of arithmetic:
  - **The claims are held as paragraphs** (`clo_kpara` after the claim,
    `clo_kfree` on the way back, `mega_p2df` turning a paragraph back into
    its selector for `mem_free`). They are PINNED - none of the image's
    claims declares a proc - so a paragraph stays true for the claim's life,
    which is also the reason the conversion is safe.
  - **Every segment register is loaded through a scratch of its own
    STREAM**: `KSEG sreg, paragraph, MEGA_SEL_Kn`, six of them (`MEGA_SEL_K0`
    to `K5`, GDT entries 1FF3h-1FF8h until step 6 moves every scratch into
    the LDT). The compressor's source is K0, its output K1, head[]/prev[] K2
    (aimed ONCE per pack and loaded by selector at every probe and fill, the
    one hot load), temporary views K3, the claim's base K4 and the second
    claim K5; the join's window K0 and output K1; the cloner's header K4 and
    staging buffer K5; `cpc_fdtest`'s cylinder K0 and track K1. One stream
    per scratch is the whole safety argument: an aim never moves a selector
    another live register or stack copy is holding.
  - **A slide re-aims the stream's own scratch** (`mega_slidef`, which now
    slides a K scratch onto itself, signed - a header's view steps BACK a
    granule) and `KPARA` reads a scratch's paragraph back for the arithmetic
    that starts from a live register. A scratch handed to the file doors
    becomes its paragraph there (`dskw_hiseg`'s GDT arm), so a 160KB write
    walks the segment arm exactly as on kern_big, and a scratch is written
    with interrupts off (`mega_gdtaim`), so an ISR's reload never reads half
    a descriptor.
  - **One defect found on the way that predates the round**: `cmz_trk0/1`
    (the pointer-tracking flag around the parse) stored to `[cur_inxfer]`
    through `KERNEL_SEG`, which in protected mode is the kernel's CODE
    selector - `mega_gpw` completed every one of those writes and logged it,
    2,074 times in one Compress. It takes the data selector (`KDATAR_M`)
    now.
  - **`tests/megacz.py` is the row** (86Box's 286, `vm/286-mega`, three
    boots): Compress BIG1.TXT (packs under 64KB), BIG2.TXT (past 64KB, both
    sides slide) and BIG3.TXT (STREAMED), each byte for byte what
    `os88lz.lzb_compress_machine` writes; Uncompress all three back; the
    Control Panel's Reads -> Cylinder saved, which runs `cpc_fdtest`; Clone
    Disk A: -> B:, every sector bar the serial; and no exception charged to
    a loaded image in any boot. Red with KSEG a plain `mov` (every packed
    file wrong from 32KB on, Uncompress says `Disk error`) and red with
    `cpc_fdtest` unconverted (an exception, and the save never finishes).
  - Not reached by the row: the cloner's same-drive path through XMS
    staging (`clo_xms`, its own `CLO_XCOPY` door), Write Img, and the join
    of a split set; all three are the same loads under the same rule.
  - Cost: kern_mega only; kern_big and kern_small byte-identical with a
    clean build of the same commit.

- **Step 6, the tables (2026-10-25) - DONE, with one change of mechanism:
  the LDT does not GROW, it is SIZED per machine.** The 82KB block (a 64KB
  identity GDT, a 16KB LDT with 1,280 static code aliases, the IDT) is now
  ~11KB on a 1MB machine and ~12KB at 4MB:
  - **The GDT is the identity map up to `.cold`'s segment and no further**
    (`MEGA_GDT_ID` = COLD_SEG/8 + 1, 520 entries on today's kern_mega), then
    padding and the thirteen scratches (K0-K5, BACK, PSCR, CODE, SLD, LDTD,
    XA, XB) placed so their last 64-byte line is K5..XB - the line the DOS
    park banks apart, as it banked 1FF8h-1FFDh. A segment loaded past the
    limit is a #GP at the instruction, which is what found the sites below.
  - **The LDT sits ON the GDT**: its base is the GDT's, so its entries below
    `MEGA_LDT_FREE0` are the GDT's own and the allocator starts at the first
    entry past them - no entry is wasted, and every `MEGA_LDT_FREE0 * 8` test
    still tells an identity segment from an allocator selector. The 1,280
    static aliases `(S >> 2) | 4` are GONE, and every arm that made or read
    one with them: `mega_codesel`, `mega_cs2ds`/`cs2dsw` (whose claim-map
    walk existed only for aliases), `mega_selbase`, `mega_s2seg`,
    `MEGA_CSELTOP`, `mega_frame_cs`, `mega_dsimg`/`csdata`, the IDT builder
    (a heap image's IVT segment is its pair's code half, made once
    `mega_mpall` has run), `mega_ivt_sync`'s row fix (a pair a driver
    stamped is written back as its image's real segment), the far-transfer
    fixer, `osapi_vol_fence`, `osapi_mem_movable_x`, `mem_reloc_call`,
    DESK.DRV's library selector, and the SDK's `OS88_CODESEL`, which is
    `value - 8` under CPU_F_PM now (the ABI lock moved without a bump: a
    no-op on every real-mode kernel).
  - **Sized, not grown.** The plan asked for an LDT that starts at 2K and
    grows. What shipped is sized once, at the switch, from `[mega_extkb]`:
    `MEGA_LDT_FREE0 + 512 + one entry per 16KB of extended memory` (to 15MB),
    in whole lines. Two reasons, and neither changes an end goal:
    `mega_layoutf` must stay a pure function of the machine (a park's anchor
    is where the next boot looks, and a resume finds the tables where its
    image had them), and the bound is computable - the finest
    `OSAPI_MEM_HUGE` step over all of extended memory, beside 512 entries
    for pairs, SUBs, windows and video selectors, which is what 768 static
    allocator entries had held. 1MB: 1,056 entries, 11KB; 4MB: 1,248, 12KB;
    16MB: 2,016, 18KB.
  - `mega_oddsegf` answers ODD for any paragraph past the identity map;
    scratches are written with interrupts off; the reflection's way back
    turns a real segment the BIOS handed back in ES or DS (a parameter
    table at F000h) into a selector (`mega_rseg`: a claim's pair, else
    `mega_useg`'s). Hibernate's table run and the DOS park's sums take the
    machine's sizes; an image of other tables is refused.
  - **The file doors' segment arm gets a scratch of its own,
    `MEGA_SEL_FD`** (`DSKW_LD`, which `DSKW_WSEG` now is), in the padding
    before the scratch block: `dskw_czstamp` and WRITE_AT's inside arm
    loaded `[dskw_seg]` as a segment, which the identity map had answered
    (megacz's Compress found the first), and MEGA_SEL_ODD is the decoder's
    and the loader's, re-aimed inside a transfer.
  - **Found by the cut, outside the kernel**: SOUND.DRV's ISR and HDD.DRV's
    own-buffer bounce both tested "above 1MB" as `selector >= 2800h`, which
    has been every conventional claim since 5.2 - SOUND.DRV takes the pair's
    data half (+ 8) for any selector now and HDD.DRV bounces any selector;
    the Task Manager's EBDA read and option-ROM scan loaded raw paragraphs
    and go through `OSAPI_SEG`'s `SEG_WIN` (the paragraph itself on every
    real-mode kernel). Its memory views still read claim headers by the
    snapshot's physical paragraph - the Task Manager's own conversion, in
    the package wave.
  - A defect of this step found by its own row: `mega_layoutf` is also
    called from the boot blob's `mega_wrprobe`, on the loader's stack, so
    the allocator's scan bound is written in `mega_tset` and not there.
  - **`tests/megatbl.py` is the row** (Bochs at 1, 4 and 16MB): the GDTR's
    limit, the LDT on the GDT with this machine's entries, the IDT after it,
    the block's KB printed, and B:'s Disk window and the Task Manager with
    no fault. `megaxmove` and `megacz` pass on the cut.
  - Cost: kern_big and kern_small byte-identical; kern_mega smaller in code
    (the alias arms) and ~70KB smaller in RAM on every machine.

- **Step 6's follow-ups, found by a sweep of every package (2026-10-25).**
  `tools/os88bochs.py`'s sweep over every shipped package, the kern_mega
  arm where one exists, at 16MB under Bochs: 37 opened clean, twelve were
  closed for a #GP and two FAULTED THE KERNEL. The two are the cut's:
  - **The planar decoder's buffer is not on the identity map.** The switch
    moves it to `COLD_RAM` and then `MEGA_RAM` - rungs PAST `.cold`'s
    segment - and `vga_blit_prow` loaded `[rom_vgabw]`, which on kern_mega
    was its own immediate. Widening the map to `MEGA_RAM` would have been
    ~2.7KB of GDT; instead a scratch of its own, `MEGA_SEL_VB`, in the
    padding beside `MEGA_SEL_FD`, aimed by `mega_vbaimf` wherever the
    switch puts the buffer, and `vga_blit_prow` loads `[mega_vbsel]` on
    kern_mega (`rom_vgabw` a word of its own there, the floor's alone).
    Solitaire's and WireBall's first blit; Paint's canvas the third check.
  - **The parts standard does segment arithmetic on the carve**, which has
    been a selector since 5.2, so every package with parts closed at its
    first load (Scribe, Word, Skies, Pixelstein - its loaders - and
    Sheet). Converted in `apps/os88partsbody.inc`, on every kernel and
    with no `PKG_MEGA` arm: `op_seg` keeps its arithmetic in PARAGRAPHS
    INTO THE CARVE and asks `OSAPI_SEG`'s `SEG_SUB` for the answer (the
    sum on every real-mode kernel, a SUB selector tied to the carve on
    kern_mega), `op_cseg` asks `SEG_SUBC` for a pair, and an
    `OP_SUBV_RAW` mode answers the paragraphs themselves (plus one, so 0
    stays "not there") for layout arithmetic - `op_scrub`, the loaders'
    checks and `OS88_REHOME`, which now takes the program part's INDEX.
    The load-time walks (`op_read`, `op_unpack`, `op_scrub`, `op_fetch`)
    hold paragraph offsets and load through `op_at`: a new verb,
    `SEG_AT`, re-aims one of two windows at a claim's base plus
    paragraphs on kern_mega, and every real-mode kernel REFUSES it with
    every register kept, so `op_at`'s fallback is the sum and kern_big
    and kern_small carry no byte for it. Why a verb and not `SEG_LIN` +
    `SEG_WIN` (48 bytes) or bound windows (164): WireFleet sat at
    APP_MAX_SIZE with 46 bytes of slack, so the library's whole cost to
    it had to fit there - it is 21 bytes for `op_at` and 3 of bss, the
    unnamed word at `OP_BSS_AT + 76` holding `op_wa`. The kernel's half of
    a re-home: `mem_reown_x` takes the SUB selector as its paragraph and
    the loader runs the program in the trimmed carve's own selector
    (`[mega_rhval]`). `crt0`'s overlay binder asks for its module as a
    pair. Sheet's literal bss is +3.
  - **Sheet** carved its note and table lists as the border claim plus
    paragraphs: `SEG_SUB` now, which moves with the claim; its two modules'
    far vectors take `op_cseg`'s code half rather than a data selector
    (a #GP per call into a module otherwise).
  - **A defect older than this round, found by Fractal**: an OS88_HIGHMEM
    pool claim regrown into a NEW block (`mega_claimregrowf`'s `.copy`)
    swapped the caller's selector onto the new block's entry but re-aimed it
    only after freeing the old block - and freeing a block frees every
    descriptor still based in it (`mx_dfree`), the caller's included. The
    re-aim comes first now. Fractal's run cache doubling was the first
    caller to reach it.
  - Re-swept: all twelve run; kern_big and kern_small byte-identical with a
    clean build of the same commit.

- **The package wave's census, retaken (2026-10-25)** - MC7.1's census was
  never written down site by site, and step 5 has since made every claim a
  selector, so it was retaken against this tree: four read-only passes, and
  `tests/megasweep.py` (every shipped package OPENS, 49 of them) as the
  floor under it. What OPENS is not what WORKS: the sites below break when
  a package loads a document, a module, a story or a song. **The work list,
  ranked:**
  1. **Frotz** - a story over 64KB: `zm_map`/`zm_addr`/`zm_norm`/
     `zm_pcaddr`, the `zx_seglim` order compares and the checksum walk test
     the TI bit for "≤64KB" and step granules. A table of `SEG_SUB`
     selectors per 32KB at load; the PC's page an index.
  2. **Paint** - a canvas over 64KB: `pt_layout`'s row table, the undo's
     `undelta`, `pt_mstep`/`pt_cvmove`/`pt_orowset`/`pt_srowset`. The row
     table by 64KB windows (HUGE step), a `SEG_SUB` for a row on a seam.
  3. **Lemmings** (C) - its 96KB bank and 125KB T-claim: `ovl_far`,
     `lem_far_add`, the plane loads in `lemblit`/`lemsprite` (a per-byte
     loop) - four plane selectors made once - and its reloc. Needs C
     wrappers for `OSAPI_SEG`, which the SDK does not have.
  4. **The Video Player's kern_mega arm** - written for the identity map:
     ring, layer, plane and EMS addressing by granule, TI-bit tests, and
     the windowed play's framebuffer constants. Slot and plane selectors
     cached once, `SEG_WIN` for the frame. (The shipped build refuses.)
     **This branch's own agent has it** (an earlier line here said the
     `mega-video` session had taken it - withdrawn, 2026-10-25): `mega-video`
     waits for this conversion to commit and builds BIGSP in place (9.1) on
     top of it. A first fault at `f3e558c0`, for whoever is converting:
     `vp_thpic`'s poster unpack adds 128-byte granules to `[vp_rdseg]`
     (`Video Pl 0D at 1184:1454` on opening any `.V88`). And the conventional
     ring has the same shape throughout: `[vp_ring]` + 2,048 paragraphs a
     slot, where the ring above 1MB (`[vp_rhi]`) is `[vp_ring]` + 8 a slot.
  5. **XT-MP3** - the hold's load and refill, the conversion stage,
     `x3xt`'s generator segment; and `x3xt`'s CS writes if that backend can
     run on a 286.
  6. **Tracker** (both builds; the shipped one has no selector arm at all),
     **ModPlug** (modules over 62KB), **MIDIRack** (bank over 64KB, per
     note-on) - per-sample `SEG_SUB`s and 64-pattern windows; the "re-claim
     at 65KB, conventional by contract" premise goes.
  7. **C64 and Apple II** - the ROM bias (`romseg - N`, a base BELOW a
     block, hot) has no door; the launch compares `romseg < N` and Apple
     II's regrow size are wrong too. Screenshot-confirmed: C64's CPU JAMs.
  8. **Drivers**: SOUND's 8237 address from `seg * 16` (own ring and a
     package's external one) -> `SEG_LIN`; HDD's run cap the same;
     RAMDISK's arena over 64KB -> a bound window; HTTPFS's `hf_bnorm`
     without a guard; EMS's frame walk and the frame it hands out; VESA's
     A000h and its VBE pointers; USBMOUSE's real-mode int 33h (a design
     question: refuse it under protected mode, or a callback stub).
  9. Small: Gorillas' and Excitebike's framebuffer constants (FSI_SEG is
     already in hand), Excitebike's own part fetch, Pixelstein's `px_hrel`
     sum, WireFleet's pin of `cs` (the code half: `ds`), Skies' SUBSEG
     fallback, Audio's and `os88pcm.inc`'s writes through CS (per sample:
     a #GP the kernel completes, each), artful's.
  10. Docs: `os88.h`'s three passages and `OS88_HUGE_INCR`'s comment still
      say only an OS88_HIGHMEM claim is a selector, and `os88drv.inc`'s
      HIGHOK/HICLAIM text.
  11. From the fourth pass: **PiXEL** (both builds: every master row and the
      decoders' work carves by granule - the flat arm, a rewrite of its
      addressing), **RealDMG's shipped build** (the bank table, RAM banks,
      the slot cache, the frame carve and the JIT part's layout; the
      `PKG_MEGA` arm covers only the bank store and the table), the
      **Browser** (its document, line and source arenas over 64KB by
      `>> 4` sums, its EMS frame, and four far vectors through `op_seg`),
      **Redline** (F000h, C000h-EFFFh and the frame paragraphs, and a
      PicoMEM paragraph), the **Task Manager**'s two reads of a driver's
      header by its physical paragraph, Sheet's row-height table, and
      `os88xgap.inc`'s/`os88con.inc`'s one line each. Writes through CS - a
      #GP the kernel completes per store - in Sheet's two modules, Word's
      and Scribe's undo, RealDMG's and the PiXEL parts' (`PXSW`) are a
      COST and not a defect, listed for the speed pass. DOS needs nothing:
      kern_mega's arm runs its program in `kern_dos`, in real mode.
      **RealDMG, both builds: DONE by the `mega-dmg` session (2026-10-26),
      merged here at `3bc7f0be`.** The first fault at `f3e558c0`, for the
      record: `rw_jpart` loaded ES with cache A's selector + 1000h
      (`CLOSED REALDMG 0D at 11AC:0778 err 2224`). What landed, by build:
      - **REALDMG.O88, shipped**: refuses under `CPU_F_PM`
        (`OS88_MEGA_REFUSE`); kern_mega runs the arm (REALDMG-SPEC 10.3.4).
      - **REALDMG.O88, the `PKG_MEGA` arm**: every "claim + N" asked of
        `OSAPI_SEG` (the JIT part and the extra tiers `SEG_SUBC`, the
        frames, table, bank 0 and a one-claim cartridge's banks `SEG_SUB`,
        cartridge RAM an offset into its claim; no bank cache under
        `CPU_F_PM`). The extra tiers are one `OSAPI_XMEM_ALLOC` block, up to
        55, and the arm declares `OS88_HIGHMEM` (16.2.6). `tests/rdgmega.py`,
        four legs.
      - **The precompiled host (`rwaot.inc`)**: `gbaot.py --mega` packages
        it as RDG_AOT + `PKG_MEGA`: the ROM in place, every code image in
        one pool block as `SEG_SUBC` pairs, code selectors in the
        relocations and tokens, the images as `S_JITXT` pairs (20.15);
        the plain package still refuses (20.14). `tests/rdgaotmega.py`.
      Its `high` line went too (REALDMG-SPEC 16.2.7), and with 8.6's file
      read fixed (`ca60319e`) RealDMG's workaround for it is gone: nothing
      of item 11 is left.
  Each converts on every kernel where the XT loop is no slower, or as a
  `PKG_MEGA` arm where it measures slower (MC7.2's answer).
  **Taken (2026-10-25, batch 1):** C64 and Apple II (the three bank fetch
  segments and the one made at `c64_run`'s/`a2_run`'s door from the ROM
  part's linear base, a window under kern_mega and the sum elsewhere - so
  `ROMBIAS` is one load and the XT's fetch rebias got CHEAPER; Apple II's
  regrow through a new SDK call, `os88_seg_para`; both boot to their
  prompts under Bochs), SOUND's two 8237 producers and HDD's run cap
  (`SEG_LIN`), HTTPFS's fold (none under protected mode), Gorillas' and
  Excitebike's frame constants (`FSI_SEG`), Excitebike's own part fetch
  (which `op_read`'s new unit had broken on EVERY kernel - the one direct
  caller of `op_read` outside the standard), Sheet's `ch_xthunk` (it
  compared the caller's CS with the module's DATA selector and so tagged
  every chart-module callback as the macro module's), `os88type.inc`'s
  alignment check (every face refused under kern_mega - Word's, Scribe's
  and FontView's type), WireFleet's pin.
  **Taken (2026-10-25, batch 2):** the Task Manager's two driver-header
  reads (a window over the paragraph, kept), Redline's F000h, option-ROM
  and frame paragraphs (`cs_win`/`cs_f000`, in `csrows.inc` so RLAPPLY
  shares them), EMS's frame walk and the frame it hands out (the paragraph
  kept beside a window), RAMDISK's arena over 64KB (`rd_at`, `SEG_AT`),
  `os88con.inc`'s F000h, XT-MP3's hold refill (`x3h_at`), its conversion
  stage (`SEG_SUB`) and its XT generator, Sheet's row-height table, and
  Audio's and `os88pcm.inc`'s stores through CS (now through DS, so the
  per-sample #GP is gone). **USBMOUSE** refuses its PicoMEM path under
  protected mode (`um_pmattach`): the card's int 33h callback is a
  real-mode far call into the image, and kern_mega has no real-to-protected
  callback, so the CH375 path is the one it keeps - a design question this
  round leaves as a refusal. **VESA needs nothing**: kern_mega builds
  without `VID_EXT` (SPEC.md 120.3), so it has no VESA row and never loads
  `VESA.DRV`; item 8's "A000h and its VBE pointers" was an entry for a
  driver that cannot meet the kernel.
  **Skies' and PIXELSTEIN's SUBSEG fallbacks** (item 9's "edge"): since
  MC7.4 `OSAPI_MEM_SUBSEG` answers a conventional region's pair too, so
  under protected mode CF = 1 means only a FULL LDT - and both programs'
  CF = 1 arm adds paragraphs to their own DS, which there names ANOTHER
  descriptor, somebody else's memory. A write through it is corruption
  rather than a fault. Both refuse instead: Skies' art band is reported
  missing (`cs_xpand` CF = 1, which both callers already handle) and
  PIXELSTEIN's part answers 0, which degrades exactly as a refused part
  does (the bodies run from the image, no byte textures). ~10 bytes each,
  registered in `tests/megapkg.txt` as fail-safes.
  **THE VIDEO PLAYER (2026-10-25) - DONE (item 4), the kern_mega arm
  only; the shipped build is byte-identical.** The arm's 8-paragraph grid
  (KERN-MEGA-PLAN 14.16) named a claim's parts by sums, which step 6 made
  invalid: opening any `.V88` closed the player at `vp_kpic`/`vp_thpic`.
  Structures inside a claim get a `SEG_SUB` each, made with the claim: the
  ring's slots (`vp_rsmk` -> `[vp_rsel]`, `vp_addr` patched to `vp_paddr`
  for every protected-mode ring), the layer's slots, mirror and bounce
  (`vp_lymk`), a planar image's planes (`vp_plmk`/`vp_plset`) - so the rate
  hook reads tables and calls nothing. Walks in the UI task or a worker
  re-aim `SEG_AT` windows (`vp_at`); in the hook, a record past its slot's
  64KB window (BIGSP's, a seam key) and a resident play's records go
  through bound windows aimed by `SEG_AIM` (`vp_pmbig`, `vp_rres`,
  `vp_raud`), a resident block's positions being paragraph offsets into it.
  The desktop frame buffer is a `SEG_WIN` window and the gap's placement
  asks `SEG_LIN`. One new defect TYPE: freeing a claim with ES inside it is
  a #GP at the X cell's own `pop es`, so `vp_mfree` moves ES off the claim
  first. Cost: the arm +866 bytes, 615 left under `APP_MAX_SIZE` - BIGSP in
  place will be tight; no kernel byte. Rows: `megavidbig`, `megavidv2` (0
  fixups on every step) and the new `megavidring` (1MB, every claim
  conventional: lin80 with speaker PCM, vga4 full screen, a layered clip, a
  resident Live clip, each to its last frame; red with the arm before).
  Still open and the `mega-video` session's: BIGSP in place (`vp_pmbigq`,
  buildable on `SEG_BWIN`/`SEG_AIM` now), EMS under protected mode, 86Box.
  Found in passing, not MC7's: a Mode X clip with a page pool and keys in
  parts says *"That keyframe could not be read"* (`vp_knrd` against
  `R_KMAX`) on kern_big too.
  **THE BROWSER (2026-10-25) - DONE, one shared path.** Its document,
  source and EMS-frame sums are gone on kern_mega: `br_docmap` reads the
  document through one bound window (`SEG_BWIN` + `SEG_AIM`) aimed at the
  16KiB page holding the byte and re-aimed only when the page changes - the
  EMS arm's own contract, so no caller changed - and `[br_docfar]`
  (`[br_docems]` OR CPU_F_PM) keeps the 8088's single test where it was.
  The parser's window (`br_srcat`) and the worker's receive (`br_take`)
  each have a bound window of their own; a line record is an OFFSET in the
  line table's one segment (62KiB cap), so no sum on any kernel; the EMS
  spill copies address the frame by offset and the conventional side by
  `SEG_AT`, and restore DS without `push cs`; windows go back before their
  claim is regrown or freed (`SEG_FREE` does not ask whose an entry is);
  the Download Manager part's vectors take `op_cseg`'s code selector; and
  the Browser declares `OS88_HIGH | OS88_HIGHMEM`, retiring
  `tests/megahigh.txt`'s two Browser lines. MEASURED on MartyPC's XT:
  `br_layout` -0.19%, a painted line -37 cycles, `br_parse` within a
  tick's noise. Image +366. Row `brmega` (Bochs): a 78KB page off B: and
  over HTTP and a 3,000-line page with every claim above 1MB, red with
  either sum restored and on the Browser before. Not run: the EMS paths
  (no EMS board in Bochs), the Download Manager on kern_mega. Found in
  passing and NOT this change: `brdownload*` hang or refuse depending only
  on code layout (the Browser before, plus 280 bytes of NOPs, hangs in
  kernel `.bss` the same way) - a latent defect for its own round.
  **THE STREAM DOOR (2026-10-25) - DONE, a kernel defect and not
  SOUND.DRV's.** The 2 "SOUND.DRV answers MIDIRack's stream open with" came
  from `osapi_snd_stream`'s `KERN_MEGA` block (kernel/snd.inc), which
  refused an external ring (verb 0 or 4 with `SND_OPENF_EXT`) whose DI had
  TI set at or past `MEGA_LDT_FREE0` - "a pool selector, above 1MB", one of
  MC7.4's TI tests missed in its sweep. Since 5.2 every claim is such a
  selector, so MIDIRack's conventional `OSAPI_MEM_CLAIM_DMA_HI` ring was
  refused for the synth and the wavetable alike, while Tracker and
  ModPlug, whose rings are grants from the driver's own pool, never reached
  it. The door now asks the descriptor's base (`mega_selbasef`) and
  refuses only at or above 1MB, in protected mode only. +13 bytes of
  kern_mega; kern_big and kern_small byte-identical. `megamod`'s mr and
  mrarm legs assert the open on the external ring, the block IRQ's count
  moving and the capture being music, a new mrsb leg the synth output; all
  red with the old door. Its sibling in `drv_fs_call` (RAMDISK.DRV,
  NET.DRV) is the same test and is RIGHT to stay: it sends every claim's
  buffer through `drv_fs_hi`'s 512-byte bounce, because those redirectors'
  arithmetic reaches one 64KB window - a speed item for later, not a fault.
  **TRACKER, MODPLUG and MIDIRACK (2026-10-25) - DONE (item 6), one shared
  path, no `PKG_MEGA` arm.** A module or bank past 64KB is reached through
  `SEG_SUB` windows made once at load (a real-mode kernel answers the sum
  they replace). Tracker and ModPlug: eight pattern windows of 32 patterns
  each (`mp_pwin`; pattern p is window p >> 5 at 60 + 1024 x (p & 31), since
  64 a window wrapped the 64th pattern's last rows onto the order table) and
  one SUB per PLAYABLE sample at start >> 4 - at most 39 LDT entries an
  instance, none for an unplayable sample, freed with the claim; a refused
  SUB refuses the load as "Out of memory". MIDIRack: a window every 32KB of
  the bank, 64KB long, a note playing from window A >> 15 at A & 7FFFh
  within it, and a guard freeing a voice whose sample ends at or before it
  starts (a lying bank). The "re-claim at 65KB, conventional by contract"
  arms (`trk_claimsel`, `mpp_claimsel`, the bit-2 tests, `mwt_lim`) are
  deleted. `mp_patseg` returns the cell offset in a register now: with a
  per-pattern offset, the word the UI task and the worker shared would have
  handed one of them the other's pattern. MEASURED on MartyPC (guest
  cycles, medians): Tracker's row read 5,859 -> 5,859, ModPlug's 5,929 ->
  5,876, MIDIRack's note-on 9,429.5 -> 9,284.5; the mixer loops are byte
  for byte unchanged. Images +88 / +5 / +132. Rows: `megamod` (Bochs, an
  SB16): every window's base against the file, three pattern windows read
  back through the replayer, the card's capture music and not noise, red
  with the packages before (`CLOSED TRACKER 0D`, `CLOSED MIDIRack 0D`);
  `trackmove`'s new 5b, red with the window loop out of `trk_reloc`.
  **Was OPEN here, and is CLOSED by THE STREAM DOOR above:** SOUND.DRV
  answered MIDIRack's stream open with 2 under kern_mega on Bochs, the synth
  as well as the wavetable, and with the packages from before this change
  too. It was the kernel's door and not the driver. `megamod`'s mrsb leg
  now hears the synth.
  **LEMMINGS (2026-10-25) - DONE (item 3), one shared path.** Its 96KB
  style bank and 125KB T claim are reached through windows made once at
  the claim by a new C SDK call, `os88_seg_sub` (OSAPI_SEG's SEG_SUB;
  `seg + para` on every real-mode kernel): three over the bank (0, 32KB,
  64KB) - `ovl_far` picks one by the offset's 32KB and answers a 16-bit
  offset, and `lem_far_add` no longer renormalises - and four over T's
  planes, which the raster reads from `_lem_tpl` by index in place of `T +
  k*LEM_TPLANE`. The bank's second part and the special picture's pieces
  go through the same windows, and `os88_onmove`'s table names them for a
  real-mode move. MEASURED on MartyPC's XT: the quiet frame rate unchanged
  at 3.75 fps, the compose and the preview picture slightly faster,
  `lembench` equal or one tick faster. +94 image, +166 overlay, +14 bss;
  the 360KB disk keeps its 53 levels with 48 bytes to spare (a hostile-
  bank range check came out to make that, so a bad bank reads past its
  claim in real mode as before, and is closed by the selector's limit on
  kern_mega). Row `lemmega` (Bochs): Fun 1, a nuke and Fun 22's special
  picture with no CLOSED or FAULT, red with either old sum put back. Not
  run: CGA and Hercules under kern_mega, 86Box, the 720KB/360KB sets.
  **And it now DECLARES (2026-10-25):** `CC_STACK_CLASS OS88_HIGH |
  OS88_HIGHMEM`, and its two `tests/megahigh.txt` lines are gone. The first
  line's whole reason was that it had never been run on kern_mega. The
  second line's reason was the four sums the conversion removed. `lemmega`
  now asserts the region is above 1MB (FD3C00h on its 16MB machine) and
  plays all three stages there. The bank and T stay conventional, being over
  64KB, so their windows read the same as before. The class byte is the only
  byte that changed, and only the on-demand Lemmings disks carry it.
  **XT-MP3 declares OS88_HIGHMEM (2026-10-25).** Batch 2 converted its two
  sums (the stage a `SEG_SUB`, the conventional hold's reads `x3h_at`'s
  `SEG_AT`), and the audit of the rest found nothing more to convert: the
  backend's work segments, the queue probe's scratch, the look-ahead and the
  speaker ring are loaded whole (the ring and its PCM are offsets in the
  look-ahead claim's first 64KB, the stage its SUB), the parts and the
  external ring come
  through the DMA door, conventional by contract, and `x3_rmove`'s `add
  [x3c_sseg]` never runs here, a move being a rebase that calls no proc
  (SPEC.md 50.6.12). Its only BIOS call is `int 16h`, with no buffer. So the
  class byte says `OS88_HIGH | OS88_HIGHMEM` in both builds, and its
  `tests/megahigh.txt` line is gone. Byte 15 (80h -> C0h) is the only byte
  that changed in either image. Row `x3mega` (Bochs, 16MB, SB16, the arm) has
  two legs. `play`: the region and the look-ahead claim above 1MB, and Auto
  plays NEONDRV live on the card out of an XMS hold. `conv`: the kernel's
  `OSAPI_XMEM_CAPS` cell is patched to answer 0 while the track opens, so
  the hold is a 30 KB conventional claim above 1MB with `x3h_at`'s window
  based inside it. Then a forced XT Standard runs Decode First onto C:. The
  stage is an LDT selector exactly `x3_lasz` + 2,560 bytes into the
  look-ahead claim, and the WAV is x3simxt's to the byte. No CLOSED or
  FAULT, ~50 s. Each old sum put back is red: `CLOSED XTMP3 0D` at the first
  stage write and at the hold's first read past +0. Not exercised: the
  speaker and Covox (the card is present), the XT backend's speed (Bochs is
  a 386), the shipped build's own image under kern_mega (its claim code is
  the arm's byte for byte), a 32 KB-cluster volume (whose claim passes
  64KB and is conventional), and a pool compaction under a running play.
  **PiXEL (2026-10-25) - DONE (item 11), one shared path, and it now
  declares OS88_HIGHMEM.** The whole item is done, master rows and decoder
  carves alike, so no flat arm was needed.
  - **How a master is reached:** through a table of `SEG_SUB` windows 32KB
    apart (`apps/pixel/pxrec.inc`: `px_mwc`, `px_mwp`, `px_mwd`). Row y is
    window `O >> 15` at `O & 7FFFh`.
  - **When the tables are made and checked:** once per claim, then checked at
    every worker job, every UI part call and every composer pass. A
    real-mode move re-bases them through `px_mreloc`.
  - **The other carves:** the histogram tail, the work claim's sub-buffers,
    the ring's second slot, the in-memory file's windows, the PNG
    scanlines, the GIF animation row and the JPEG bounce are each a
    `SEG_SUB`.
  - **The DMA claim's alignment test:** `px_dmaclaim`'s 512-byte test reads
    `SEG_LIN` now. It used to read the selector's bits, so on the parent
    kernel PiXEL opened nothing.
  - **Patched under protected mode, 8088 loops unchanged:** `pe_rot90`'s
    64KB carry, and the progressive JPEG store's 1/4 and 1/8 arms (the 1/8
    one through the `je`'s displacement) walk windows instead.
  - **A master is held to 1MB.**
  - **The parked worker drops ES.** That removes one #GP per PNG or GIF
    open, which came from a window being freed under it.
  - **Measured on MartyPC's 5150 (minimum cycles per call):**
    - `px_mrow`: 395 -> 321
    - `px_crow`: -62 and -68 a row
    - `px_cset`: +75 a pass
    - each table: about 4K per claim
    - pxbench's zoom, pan and Fit: unchanged on CGA and VGA
  - **Bytes:** image +474, bss +220, PIXEL.O88 +2 sectors. Parity pads keep
    every unchanged routine at an even shift.
  - **The declaration:** its `tests/megahigh.txt` line is retired, and
    `OS88_HIGH | OS88_HIGHMEM` is the header.
  - **Rows `megapixel` and `megapixelship` (Bochs)** check, byte for byte:
    the DEEP.BMP master, its tail, both turns and Save As; and FLOWER, HOUSE
    and ROOM at 1/4 and 1/8 against pixelsim. They also check that the view
    claim (17-31KB) is above 1MB, at FBD000h. They go red:
    - on the parent;
    - with `px_mrow`'s old sum put back;
    - with the carry left unpatched;
    - without the HIGHMEM bit (the view claim at 058000h).
  - **Left as a cost:** the shipped build's PXSW stores through CS, for the
    speed pass. The arm stores them through `PXP_SELF`.
  - **Not run:** non-VGA adapters under kern_mega, the XMS undo, and full
    screen on kern_mega.
  **SHEET declares OS88_HIGH and OS88_HIGHMEM (2026-10-25)**, and its two
  `tests/megahigh.txt` lines are gone. Both reasons had been answered
  already: the far vectors into the chart and macro modules are built from
  `op_cseg` (the CODE half), the CS `ch_ovbind`/`sh_ovbind` stamp is the
  region's own code half, `ch_xthunk` compares the code half (batch 1),
  and the note/table carves and the row-height table are `SEG_SUB`
  windows (batch 2). What the declaration still needed was a run and one
  fix: the macro module's text channels (`FOPEN`...`FCLOSE`, 81.91) made
  ES its own CS with `push cs / pop es` and handed that to
  `OSAPI_FILE_READ`/`WRITE`, so the kernel's file read and the module's
  `[es:]` stores went through a read-only code selector - completed by
  `mega_gpw` one #GP each, a cost and not a defect. They take the part's
  DATA half now, `[sh_m2dseg]` (set by `sh_m2need` from `op_seg`, kept by
  `sh_partmv` on a real-mode move). The modules' claims are DMA-door claims
  and stay conventional; the region and the five data claims go above
  1MB. COST: image +12 (54,718 -> 54,730), bss +2 (9,490 -> 9,492), the
  macro part +8 inside its fixed 28,672 bytes, `SHEET.O88` +4 on disk;
  one shared path, no `PKG_MEGA` arm - the four `mov es, [sh_m2dseg]` are
  outside any loop. Row `megasheet` (Bochs, MEGATRAP=1's census): the
  region and claims above 1MB, a SYLK opened by double-click, Data > Chart
  Column... drawn by the chart module, both vectors code halves, a macro
  summing a FOR/NEXT and reading and writing a file through a channel,
  no CLOSED or FAULT, and no #GP that is a far transfer or a store through
  the macro module's code selector in DS/ES. Red with `op_seg` in
  `ch_ovneed` or `ch_xthunk` comparing `ch_ovseg` (FAULT 0D at the
  document's read), with `push cs / pop es` back (census only), and on the
  parent's `SHEET.O88` (region at 090000). STILL PAID: 164 #GPs in that one
  macro run, every one a `mov [cs:x], ...` in the macro module (none in
  the chart module) - item 11's speed-pass entry, unchanged. Not
  exercised under kern_mega: notes, row heights and Insert/Delete Row (the
  `SEG_SUB` carves), Undo, the BIFF/DIF writers, Export Chart as BMP, the
  dialog engines past Run, and any adapter but Bochs's VGA.
  **Paint (2026-10-25) - DONE, one shared path.** A canvas over 64KB under
  protected mode is laid out in `SEG_SUB` windows at fixed 32KB offsets
  (`pt_layout`'s `.win`): a row at file offset O is window O >> 15 at O &
  7FFFh, and fixed offsets make a re-layout (a resize's two passes, a load)
  ask for the same windows and get the same selectors, so the LDT does not
  grow per resize. The undo image has windows of its own over the same
  offsets (`pt_uwins`; `pt_ucopy`, `pt_uswap_row` and `pt_urowset` map
  through `pt_uwinof` behind `[pt_pmw]`), `pt_orowset`/`pt_srowset` read
  through them, `pt_cvmove` looks each row up (`.pmrow`), and the band cap
  is a layout fact (`[pt_brmax4]`), which takes a `div` out of every
  `pt_blit_1`. Save Bmp is one `OSAPI_FILE_WRITE` again: the agent's
  first build wrote 32KB pieces around the kernel defect below, and the
  fix took that out of Paint. MEASURED on MartyPC `os8088_xt_vga`, per call:
  `pt_ucopy` -218 to -332 cycles a row, `pt_uswap_row` 0, `pt_blit_1` -143
  to -504, `pt_layout` -15%, `pt_resize` -2.7%. Image +464, bss +39 (the
  small arm +67) - shared on the owner's choice A, no slower anywhere on
  the XT. Row `megapaint` (Bochs): a 580x380 BMP opened, maximized to
  590x390 across 4 windows, strokes, undo/redo, Save Bmp read back to the
  byte; red on the base build (`kern_mega CLOSED PAINT`), on undo through
  the canvas's window, and on a one-call save. Not run: the planar format
  under kern_mega (Bochs gave a packed canvas).
  **FIXED (kernel): a file transfer of 64KB or more through an LDT
  selector wrapped at the selector's limit** - found by Paint's save (a
  115KB Save Bmp wrote its DIB header again at 65,536). The selector arm
  moved only the OFFSET; now `dskw_hicarry` steps the transfer's selector
  ([dskw_wseg], the advancing copy, not the caller's banked [dskw_seg]) a
  whole 64KB on by `SEG_SUB` when a run's offset carries out, and
  `dskw_hifix` steps it to the offset's paragraph before a sector or the
  tail would cross the end, where `dskw_one`'s copy would have faulted. A
  SUB is tied to the caller's block, so it moves and frees with it.
  kern_mega `.text` +~70; kern_big and kern_small untouched (every line
  under `%ifdef KERN_MEGA`). `megapaint` 7 is the gate: one-call save,
  identical; DIFFERENT from byte 65,536 on the kernel before.
  **Frotz (2026-10-25) - DONE, one shared path, no `PKG_MEGA` arm.** The
  story claim is walked by 32KB PAGE: `zm_pages` builds `[zf_stab]` at
  load, one `SEG_SUB` per page (a 64KB data selector on kern_mega, the
  claim's segment + 800h x k on every real-mode kernel; pages past the
  claim hold the claim). The PC is a page INDEX `[zf_pcpg]` plus SI, so
  `zm_map`/`zm_seek` index the table, `zm_norm` and `zx_jrel`'s backward
  wrap step the index, `zm_pcaddr` is index x 32KB + SI, `zx_step`'s guard
  is ES equal to its page's entry (no order compare; `zx_seglim` is gone),
  `@verify` walks by page, and `zf_reloc` sums the table on a real-mode
  move. `zp_place` (the `.PIX` archive, pinned) takes `SEG_AT` with the
  sum as the real-mode answer. MEASURED on MartyPC, key to next prompt: a
  148KB story's `go` 141.84M -> 140.70M cycles (-0.8%), `text` 382.95M ->
  380.68M (-0.6%); a 34KB story -0.3 to -0.6%. +88 bytes of image, +36 of
  bss. Under Bochs a 282KB v8 story plays to the generator's gold
  (`megafrotz`); the build before printed VERIFY bad and halted on "bad
  routine header". `tests/frotz/zbiggen.py` writes the fixture and its
  gold, so `zbig` and `megafrotz` need neither the IF Archive nor dfrotz.
  Not run: the shipped stories (the IF Archive is blocked from this box),
  and a `.PIX` archive over 64KB on kern_mega.

- **The two asks of sections 8 and 9 (2026-10-25) - DONE, as three
  `OSAPI_SEG` verbs** (SPEC.md 50.6.11), taken now so neither round has to
  reopen the end state:
  - **8.1, RealDMG's code name past a claim's first 64KB: `SEG_SUBC`** - a
    SUB that is a code/data pair, the code half at DX - 8, based in a pool
    block or a conventional claim, rebased and freed with it. The recompiler's
    part and each extra tier are one call each; `MEM_OWNER_MAX` is untouched.
  - **9.1, the Video Player's hook-legal window: `SEG_BWIN` + `SEG_AIM`** - a
    fresh window bound to the caller's block, and a re-aim to a 32-bit byte
    offset that takes no lock, never sets IF and costs ~40 bytes of stack
    with the cell's frame (SPEC.md 53.2.2 names the exception). One window
    serves a whole play, so the LDT holds one entry for it. A block holding
    one moves with the rate hook held off (`MX_FWIN`), as 9.1 asked, and an
    aim after a move reads the block's current base.
  - On the way: a SUB in a CONVENTIONAL claim is now tied to the block as a
    pool one is (5.1's rebase and free cover it), instead of a window; and
    freeing an LDT entry clears any `mega_wtab` row naming it, so a window
    that went with its block is not freed again at its owner's end.
  - Cost: kern_big and kern_small +0 (the real-mode body grew by exactly the
    parity pad it no longer needs; `SEG_AIM` refuses in real mode, its only
    callers being kern_mega arms whose real-mode path already does the sum);
    kern_mega `.cold` +30, `.mega` +485. MEGAXM checks all three (steps 57
    to 5C, and 13 and 14 in real mode) and goes red with the re-aim's offset
    ignored (`MEGAXM: bad 5A`); its step 0D now expects the region's code
    half at DS - 8.

- **The final pass's two red rows (2026-10-26) - neither was a kernel
  defect, and no kernel byte moved.**
  - **`megarehome`'s PIXELSTEIN leg** read a fourth selector, `113C`, at
    its block + 128KB with a 64KB limit "running past its block". `113C` is
    `MEGA_SEL_XB`, a linear copy's DESTINATION scratch (`MEGA_GDT_SCR` +
    12). Step 6 put the LDT on the GDT's base, so `info ldt` lists the GDT's
    entries as well, and XB is left on the last 32KB chunk `mega_imghuge`
    copied. Nothing made it for the block, and nothing reads it before
    re-aiming it. A trace on `mega_ldt_alloc`, `mega_ldt_alloc2` and
    `mega_ldt_free` for that entry fired not once in a whole run. The row
    now reads `MEGA_LDT_FREE0` off the map and counts allocator selectors
    only. It passes (PIXELSTEIN 3 inside, SKIES still 4) and still goes red
    when a stray `mega_ldt_alloc` data selector is put at the same place on
    purpose (`selector 1264 runs past its block`). The work-in-progress
    fixes for a re-home trim were reverted, because neither touched this:
    `mega_mpcutf` (10 of `.cold`'s 12 spare bytes) and `OS88_REHOME` freeing
    `op_wa`/`op_wb`. Those two windows are `mega_wtab` rows under the
    instance SLOT, which the loader and the program share, so the
    teardown's `mx_relall` frees them. A HIGH program's carve also takes
    them with it when it goes. They are not orphaned.
  - **`x3xtunit` and `x3xtexact`** run the XT part with no kernel under it,
    and the package wave made its OPEN call `OSAPI_SEG`. The two harnesses
    now answer that cell with `tests/x3xtbench/x3rmseg.inc`, which is
    `osapi_seg`'s real-mode arm instruction for instruction. The bench also
    moves its case directory off `0060:0`, where it covered the cell.
    `x3xtunit`'s eight refusal failures predate this round: they were
    already there at the merge base. Two were stale expectations, since the
    speech tables run at 2 or 4 on any version (docs/xtmp3/XT.md 8.1). The
    other six were the first wrong acceptance clobbering a shared part. Both
    rows are ok.
- **The installer's first Next faulted the machine (2026-10-26) - an MC7
  miss in HDD.DRV, FIXED.** The owner, on 86Box's mr286 with a 503MB IDE
  disk: `kern_mega FAULT 0D err 0000 at 1154:07BE` on the wizard's Step 1.
  `1154` is an allocator selector (index 22Ah, `MEGA_LDT_FREE0` + 2), the
  CODE half of HDD.DRV's image pair, and `07BE` is the `rep movsw` in
  `hd_raw`'s bounce (`drivers/hdd/mount.inc`). Bochs reproduced it to the
  offset (`1194:07BE` there, the pair a few entries on; ES `11F4` the tool,
  SI `3400`, CX `0100`). Step 6 widened that bounce from "an image above
  1MB" to ANY selector (the "found by the cut" bullet above), but the copy
  still read `DS:BX` - "the buffer is OURS (ES = DS)", true only while HDD.DRV
  was the one caller. HDDTOOL.DRV reaches it through `HSV_RAW` with a buffer
  in ITS image (`hd_mbr` at 3400h), so the copy read HDD.DRV's own data
  segment at the tool's offset and ran off its limit. A second defect sat
  behind it: the bounce moved ONE sector, and `hd_kfat_get` (every install's
  `hd_kplan`) reads three into `[hd_ibufseg]` - 512 bytes past a 1KB claim
  and two sectors never copied back. The bounce now copies from and to the
  caller's `ES:BX` and carries CX sectors in a claim of CX/2 KB. One shared
  path: +7 bytes of HDD.DRV (`hd_raw` to `hd_chs_regs` 0x2B4 -> 0x2BB), no
  kernel byte on any build; the real-mode kernels reach the bounce only for
  an image in the HMA, where ES = DS and the behaviour is the old one, and an
  XT never reaches it. Control Panel's Hard Drive page never faulted because
  its `hd_part_load` is the resident's own, ES = DS. **`megainst` is the
  row** (Bochs, 16MB): the wizard walked to Done through Erase, C:'s
  KERNEL.SYS then the floppy's byte for byte; red on the parent commit at the
  partition page with IP 07BE. It does NOT gate the sector count (a fresh
  volume's FAT reads as zeros either way). `hmadrv` (the HMA bounce on QEMU)
  is green beside it.


### MC5 as built (2026-10-25) - DONE

- **The ask is `OSAPI_MEM_HUGE` with AX = 0**, so no new cell: a 0KB claim
  was already a refusal on every kernel, so a package asking can tell an old
  kernel from a new one by CF alone. kern_mega answers CF = 0 and AX = the
  most one claim of the cell can be: the conventional heap's largest claim
  (`mem_avail_lvl_x` at the top rank), the pool's (`mx_maxrun`), or the
  bridge's, whichever is largest (`mega_hugeqf`). In real mode it is the heap's
  alone. kern_big and kern_small answer CF = 1, as before, and the SDK says to
  ask `OSAPI_MEM_AVAIL` there. `PKG_FMT` and `DRV_VER` are unchanged: the
  contract was clarified, and nothing built before stops running.
- **`mx_maxrun` is exact for the pack `mx_alloc` makes.** That pack moves every
  movable block up against the pinned block above it and sheds every cache a
  program outranks. So each stretch between two pinned blocks, above
  `[mega_mxflr]`, offers its length less the movable non-cache KB that start
  in it. The answer is the best stretch.
- **The bridge (D7) is tried only when neither piece alone can take the
  claim**, after `mega_hugef` and the conventional claim have both refused.
  The plan said the kernel would put first the piece with the smaller
  remainder. As built, the **pool piece always comes first, cut to whole 64KB
  windows**, and the heap piece takes the rest. The heap piece's last window
  ends where the claim ends, so nothing is lost at the seam.
  - The heap piece is claimed first. A compaction may take time over it, and
    nothing is held yet.
  - The pool block and the run of selectors are then made in one step under
    `sch_lock`.
  - A 32KB-step or 16KB-step ask (`BX` = 8000h or 4000h) is never bridged,
    because their windows overlap.
- **Each piece is the record it would be alone, so each moves and is freed
  alone.**
  - The pool block's windows are its own `MX_SEL` run. Its KB is a whole
    number of windows, so `mx_unsel` frees exactly those.
  - The heap piece's windows follow the pool's in the same run but are based
    inside the heap piece. So `mega_mpmovef` rebases them when it moves and
    `mega_mpfreef`'s `mega_cdfree` frees them with it. That is the rule
    MC7.4 already gave a conventional claim's SUBs, and no new code was
    needed for it.
  - The pool record carries `MX_FBRIDGE` (40h) and names the heap piece's
    selector in `MX_AUX`. `OSAPI_MEM_FREE` of the run's first selector frees
    the pool block and then the heap piece (`[mega_bfpart]`, set by
    `mega_claimfreef` on every free). An instance's teardown frees each piece
    as its own record.
  - A bridge does not regrow: `mega_claimregrowf` refuses `MX_FBRIDGE`.
- **The reserve (D8):** a bridge leaves the heap `MEGA_RESKB` = 64KB, one
  run. Its heap piece is taken only when the heap's largest claim, less 64KB,
  still holds it. The query answers by the same rule.
- **One defect found on the way:** `mega_mxat` matches a block by its base
  AND its owner. The bridge called it with BL overwritten by the windows'
  KB, so the lookup missed, and `MX_SEL` and the flags were written one record
  past `mx_tab`. The fix is one `mov bl, [mega_bfown]`. Without it no record
  carries the run's selector, so `OSAPI_MEM_FREE` of it would refuse and the
  bridge would stand until teardown. That is read from the code: the run
  that found the defect had faulted earlier, on a bug in the instrument
  itself, which stored CX after its own `mov cl, 3`.
- **Cost, kern_mega against the tree before:** `.text` +12, `.cold` +458,
  `.mega` +173, its scratch +3: 646 bytes in all. The cold rung has 12 bytes
  left (470 before), and the megaseg overhead is 3,779 of the 4,029 allowed.
  kern_big and kern_small: the code is all `%ifdef KERN_MEGA`, and kern_big's
  sections read the same before and after.
- **Row: `megahuge`.** Bochs, 4MB, tests/megahuge's MEGAHUGE started by
  MEGARUN.
  - It asks (3,450K, against a pool of 2,997K, so the answer is a bridge),
    claims exactly that (step 8), and checks that the first window is at
    10C000h and the last at 81000h, under 1MB.
  - It writes a pattern through all 54 windows, the seam included, reads it
    back, frees, and asks again for the same 3,450K.
  - 16 seconds.
  - It is red on the tree before: `MEGAHUGE: no 01`, the 0KB claim refused.
    Verified.
- **Not done, of what MC5's row list asked:**
  - holding the claim open across the Control Panel and a hibernate;
  - `vm/286-mega`;
  - comparing the answer with the Task Manager's free figure.
  `megavidbig` and `megavidv2` are the Video Player's rows, which the
  `mega-video` session now owns. They go in the final pass only if that
  session has not changed them.

---

## 8. Addendum from `mega-dmg`: what RealDMG needs from this round (2026-10-25)

**The conversion itself is taken by `mega-dmg`** (7, the package wave's
item 11): leave RealDMG to that session.

A note from the RealDMG session, for MC7's step 4 (the six exempts) and
step 5. It describes RealDMG as it stands on `elendilon` at `1f013feb`,
which this branch has not merged yet: `mega-dmg` added code selectors
(REALDMG-SPEC 16.2.4), the cartridge in place above 1MB (10.3.1), more
cache tiers (16.2.5) and kern_mega's own build of the package
(`$(MEGAPKGS)`, `PKG_MEGA`). The longer design is
`docs/plans/REALDMG-HIGHCACHE-PLAN.md` on `elendilon`. Nothing here asks
the round to change course; 7.4's "a pair for every conventional claim,
code = data - 8" already answers most of it.

### 8.1 Code RealDMG enters, and where it is

RealDMG far-jumps into, and only writes code through, a DATA name, and asks
`OSAPI_MEM_CODESEL` for the code name of each (16.2.4). The code is in four
places:

| what | where | needs |
|---|---|---|
| the core | a part of the parts carve (`op_seg`) | the carve's conversion (7.3 step 2); its code name through `OSAPI_MEM_CODESEL` |
| cache A, 64KB | the first 64KB of its claim | the claim's own pair |
| **the recompiler's part**, ~21KB | **inside cache A's claim, past 64KB** (`rw_jpseg` = the claim + 1000h + the table's paragraphs; + 1000h exactly on kern_mega, where the table is in the pool) | **a code name for a range PAST a claim's first 64KB** |
| cache B and **the extra tiers** | cache B's claim, **grown with `OSAPI_MEM_REGROW` to 1 + k x 64KB** (k up to 5), the tiers at B + 1000h, + 2000h... (`rw_jitxset`) | **a code name for each 64KB window past the first** |

**The ask (the only kernel one):** a code twin for a range inside a claim -
`SEG_SUB` with a code form (a pair, code = data - 8, the 7.4 rule), or
`OSAPI_MEM_CODESEL` answering a `SEG_SUB` selector by making its twin. The
pair over a claim stops at 64KB (`mega_mpbase`'s limit), and a lone data
selector has no code view (`mega_codesel` refuses it), so without one of
these the part and the extra tiers have no CS on the end-state kernel.
`OSAPI_MEM_HUGE`'s windows want the same, for the plan's tiers above 1MB.
With it RealDMG converts its own way: SUB the part and each tier, ask each
for its code name, and keep everything else as it is.

**Not the alternative:** splitting the part and the tiers into claims of
their own would take RealDMG past **`MEM_OWNER_MAX` = 8**. A game on kern_mega
holds five claims after its load (the frames 17K, 15K, cache A, cache B and
the carve; read off `mem_tab` on 86Box), and Machine > Log, a replay (F7)
and Machine > Profile each take one more. 16.2.5's first build took a claim
per extra tier, seven after the load, and its replay was refused - "Replay:
no room" with 91KB free. Growing B's claim is how it stays at five. If the round raises the cap on
kern_mega, say so and that route opens too.

### 8.2 RealDMG's segment arithmetic, for the census

Beyond the code above, every site where RealDMG makes a segment by
arithmetic. All of them are inside one claim, and each is a `SEG_SUB` or an
offset after the flip:

- **Cartridge RAM**: `S_RAMSEG` + bank x 200h (`rdg_rambank`, rdgsys.inc),
  at most 128KB in one claim.
- **The bank cache's slots**: `rw_slotseg` + slot x 400h (rwbank.inc). On
  kern_mega's build this is only taken when the pool is too short for the
  cartridge in place.
- **A cartridge in one claim** (one that fits the heap, the shipped build on
  every kernel): the bank table names, for bank n, the segment whose 4000h
  is the bank's first byte - a BIASED base, the claim + (n - 1) x 400h.
  This is the ROM-bias case 7.2 lists (Apple II, C64).
- **The block table**: cache A's claim + 1000h in conventional memory. On
  kern_mega's build it is an extended block behind `OSAPI_XMEM_RUN`'s first
  selector (`rw_jtx`), so that arm has no arithmetic there.
- **The frames**: `rw_fseg` = `rw_pseg` + a paragraph offset (one 17KB claim).
- **The view and the core**: through the parts library (`op_seg`), 7.3 step 2.

Already selector-safe on kern_mega, needing nothing:

- the cartridge in place (`OSAPI_XMEM_RUN`'s selectors in the bank table);
- the link patches (they write through the data name, `tr_lpes`/`tr_lpx`);
- `rw_partsmv`, which skips a word with bit 2 set;
- `rw_reloc`, which re-stamps by code name.

`XMEM_ALLOC` tokens (the cartridge's, the table's) only go back to
`XMEM_RUN` and `XMEM_FREE`, so A3 holds.

### 8.3 What changes for RealDMG under 7.4

- **Moves become rebases, and that is better for RealDMG.** A cache move
  re-ran `rt_init` (a flush) through `rw_mreloc`; with no proc called, the
  translated code, its `jmp far` chains and the block table all stay valid.
  Its relocation procs go quiet on kern_mega and stay for the other kernels.
- **The recompiler converts data to code by a TABLE** (`rt_d2c`, the view's
  `S_JITCSA`/`S_JITCSB`/`S_JITXT`, filled from `OSAPI_MEM_CODESEL`), never by
  "- 8". So it works on either rule and needs no change for the pair layout.
- **Writes through CS that remain**: `rdg_init`'s two words and the lossy
  picture's `rdg_lpatch`, about 90 completed #GPs a session on the host's
  own stack (16.2.4). They are harmless, but the end state would count them.

### 8.4 An open question the round may want anyway

**A package-visible hibernate notice.** RealDMG's cache is regenerable. A
large cache in the pool should be FREED before a hibernate and claimed again
after, rather than written into the image. MC4's "the trip offers a
hibernate" makes the image the size of what is in use. Drivers have
`hbm_detach`/`hbm_reload`; packages have nothing.

### 8.5 How to check RealDMG after the flip

- **`tests/rdgmega.py`** (Bochs, `MEGATRAP=1`, about 4 minutes), three legs,
  each running blargg's `cpu_instrs` under the recompiler:
  - the shipped build;
  - kern_mega's build;
  - kern_mega's build squeezed to ~0.5KB a tier, so it recycles through
    every tier, the extra ones included.
  
  It fails on a trap charged to the core or a cache's code name.
- **`tests/rdgcore/rdgwin.py --vm 286-mega`** (86Box), for speed.
- **`tools/rdglog.py`**, which compares a field log with an emulator log of
  the same replay.

### 8.6 A defect RealDMG found: a whole 64KB file read into a 64KB pool claim (FIXED)

**FIXED (2026-10-26), and not where the reading below put it.** The read
never reached the data. `dskw_rbody`'s check that a plain file fits the
window from its offset was `src + len` must not carry, then `lenhi` must be
0. A 65,536-byte file has `len` = 0 and `lenhi` = 1, so a file ending
exactly at the window's end was judged a misfit. It went to the conventional
fallback, `dskw_tolo`, which refuses a block above 1MB, and the read answered
FERR_IO. `dskw_hicarry` was never involved: E9 markers at every failure exit
of the data stage fired none. The test now compares the file's END in 32
bits with 65,536. kern_mega `.cold` +7, inside `%ifdef KERN_MEGA`.
`megahuge` has a leg for it: a 64KB claim in the pool, a whole 64KB file
read into it, and bytes checked at both ends and the middle. It was red
before the fix (`read64 refused, AX 0002`). RealDMG's 65KB workaround can
come out.

The original report follows.


**Not fixed as of `dc591db4`** (`kernel/diskw.inc` has not changed since
`dcbffc9e`, and MC5 does not touch the file layer). Found on `mega-dmg` at
`5a620b2f` (this branch merged at `f068399f`), REALDMG-SPEC 16.2.6:

- **What fails**: `OSAPI_FILE_READ` of a 65,536-byte file, ES = a 64KB
  `OS88_HIGHMEM` claim that went to the POOL (a lone data selector, limit
  FFFFh), BX = 0, DX:CX = 1:0000 (the capacity, exactly the file). The call
  returns CF = 1 with an error that is not `FERR_BIG` - RealDMG's
  "read failed" - and the machine carries on, idle; nothing hangs or faults.
- **What does not**: the same read with the claim a KB larger (65KB, so
  conventional) succeeds; a 32,768-byte file into a 32KB pool claim
  succeeds; `OSAPI_FILE_READ_SEQ` in 16KB pieces into a 16KB pool claim
  succeeds. So it is the transfer that ENDS exactly at a pool selector's
  64KB limit - the offset that carries to 0 after the last sector - and
  `dcbffc9e`'s `dskw_hicarry`/`dskw_hifix` are where to look: stepping
  `[dskw_wseg]` a whole 64KB on by `SEG_SUB` when the run's offset carries
  asks for a window AT the block's end, which `mega_bclip` refuses (no
  room past it), so a transfer that has nothing left to move fails anyway.
  That is a reading of the code, not a trace.
- **Repro**: `tests/rdgmega.py --leg oneclaim` with RealDMG's workaround
  taken out (`apps/realdmg/realdmg.asm`, `rw_load0`'s `.claim`: the `cmp
  ax, 64 / jne .c65 / inc ax` under `PKG_MEGA`) - blargg's `cpu_instrs` is
  a 64KB ROM, and with `RW_NOXPLAN` its one claim is exactly 64KB. Red:
  "the view was never found"; screenshots every 0.3 s after the
  double-click show the toast.
- **RealDMG works round it** by claiming a 64KB one-claim cartridge as 65KB
  under `CPU_F_PM`, which keeps it conventional; that line comes out when
  the kernel is fixed. Any other package reading a whole 64KB file into a
  64KB `OS88_HIGHMEM` claim meets the same refusal.

---

## 9. Addendum from `mega-video`: what the Video Player needs from this round (2026-10-25)

A note from the Video Player session. It describes the player as it stands
on `elendilon` at `08a25018`, which this branch has not merged yet:
KERN-MEGA-PLAN 14.73 there (*the Video Player on a BIOS-served disk*) fixed
four causes of pauses and over-long plays on kern_mega. The owner has asked
that the next step - **BIGSP super-packets read in place above 1MB** - wait
for this round. 5's MC9 already says "BIGSP in place stays with the Video
Player round". This section is what that round will need from this one, so
the end state does not have to be reopened for it.

### 9.1 The ask: a window the rate hook can re-aim, anywhere in a pool block

**Where things stand.** On kern_mega the player decodes in place from an
`OSAPI_XMEM_ALLOC` block - the file held whole, or the bank - through
`OSAPI_XMEM_RUN`'s run: a window every 32KB, each 64KB long. `vp_xmk`
picks window `offset >> 15` at `offset & 7FFFh`. That covers any record of
32KB or less. A BIGSP super-packet is up to **127 sectors (63.5KB)**, and
most of them, 38-63KB, run off their window. So `vp_pmbigq` refuses in
place, and a BIGSP file under protected mode copies its records through
the conventional ring instead. That is LASTX's ~5% (14.73), and it needs
room the Victory 286 runs short of.

**Why a run cannot fix it.** A run of step S holds every record up to
64KB - S. Holding 63.5KB needs a 512-byte step: on a 3MB hold that is 6,144
LDT entries, 48KB of LDT. That is exactly the usage this round exists to
remove.

**Why `OSAPI_SEG` as specified cannot fix it either.**

- The decode runs in the player's `FSXF_RATE` hook (`vp_hook`, at IRQ0), and
  §53.2.2 forbids kernel calls there. `vp_xmk` is reached from that decode.
- `SEG_SUB` takes a 16-bit paragraph offset (1MB of reach), and a held file
  can be larger.
- `SEG_SUB` has no re-aim. Asked for a new base each record, it would hold
  one LDT entry per distinct record until the block is freed - thousands in
  a play.
- `SEG_WIN` re-aims, but it takes a physical PARAGRAPH, so it cannot name
  memory above 1MB.

**What would do it.** A small number of windows (2-4) over the caller's own
pool block. The caller re-aims one to a **32-bit offset into the block**,
64KB of reach, **legal inside the rate hook**: IF = 0, no lock, nothing
that yields, inside §53.2.2's 48 bytes of stack, the cell's own frame
included. It is one descriptor's base rewritten (three bytes), checked
against the owner and the block's end. The shape is yours to choose:

- a re-aim verb on `OSAPI_SEG` that takes an existing SUB;
- a hook-safe form of `OSAPI_XMEM_SEL`;
- a rolling form of `OSAPI_XMEM_RUN`.

The contract the player needs is:

- an offset in, a selector out (or the same selector re-aimed);
- callable from the hook;
- no LDT entry per call.

One re-aim per BIGSP record is about one per frame at 25 fps.

**Moves.** D2 makes these blocks movable. The windows must be rebased with
the block (`mx_desc`, as a SUB is today), and an offset re-aim must use the
block's CURRENT base. A piece must also never be copied while the hook can
read it: either the hook is held off for each piece, the rule
`OSAPI_MEM_HUGE`'s 32KB form already has (§50.3.5), or a block with a live
window does not move. Either works for the player.

### 9.2 What must stay true, for the paths 14.73 fixed

- **`OSAPI_XMEM_RUN`'s selectors stay CONSECUTIVE** (selector + 8k). The hook
  computes them without calling the kernel (`vp_xmk`: window x 8 +
  `[vp_xrun]`). T69 and MECH2 play in place through this (the XMS bank in
  place, `[vp_einp]` 2), and step 6's LDT-as-allocator must still hand out a
  run.
- **DMA-door claims stay page-safe for their whole life.** The player claims
  through `OSAPI_MEM_CLAIM_DMA`:
  - the ring's bounce (`vp_rhtry`, CX = 32);
  - the ring's 64KB or 32KB+ head (`vp_rclaim`, `vp_rclk1`);
  - the ring's slots.

  14.73's second cause was a bounce that straddled a 64KB page. Every chunk
  then cost extra `int 13h` staging calls through `dsk_rah`. 7.4 lets every
  claim move "but a DMA claim with a head". The player needs these claims to
  be ones that stay put - or that only ever move to page-safe spots - and to
  stay direct transfers for `dsk_runcap` (5.2's base arithmetic already does
  that for conventional selectors).
- **Two kern_mega kernel fixes arrive with the merge, and step 6 must keep
  them working**:
  - `mega_svc_back` runs `sch_rhook` (through `mega_tcall`) when a
    reflection returns with `[sch_rpend]` owed (§53.2.2's kern_mega
    paragraph).
  - `mega_rmaskf` leaves an option ROM's IRQ unmasked during a service
    reflection. It recognises one by its IDT GATE: selector `MEGA_SEL_CODE`,
    offset within 14 x 6 of `mega_irv`. It does not read the IVT, because an
    IVT cell can hold a selector. If the GDT cut moves `MEGA_SEL_CODE` or the
    reflection stubs, that test must follow them.
- **SOUND.DRV's direct ring** (SPEC 34.5.2.1 on `elendilon`) now counts blocks
  from the 8237's position (`sbl_dleft`). It does offset arithmetic inside
  the ring and no segment arithmetic, so it adds nothing to the census. The
  external ring the player hands over is still a segment today, and is in
  7.1's "five packages" item.
- **Claim count.** The player's ring is a head plus slots, alongside the
  bounce and its own region. Nothing in this round should lower
  `MEM_OWNER_MAX`.

### 9.3 How to check the player after the flip

14.73's method, on 86Box `286-victory` with an XT-IDE ROM disk (BIOS-served,
the case that paused) and an SB16:

- Build `make mega`, put the VGA demo videos on a VHD, and play each one
  while counting frames drawn by 64 s.
- 14.73's table is the bar. At `08a25018`:

  | video | frames by 64 s | full play |
  |---|---|---|
  | T69 | 1,505 | all 1,838 in 72 s, 0 pauses |
  | MECH2 | 892 | |
  | LASTX | 1,273 | |
  | BCRRT | 1,407 | |
  | WILLY | 844 | |

- The rows are `megavidbig` and `megavidv2`, which 5 already names.

Once 9.1 exists, the Video Player round can delete `vp_pmbigq`'s refusal
and the copying bank's PM arm (`vp_rclk1`), and LASTX goes in place.

**Used (2026-10-25, `mega-video`):** BIGSP in place is built on `SEG_BWIN` +
`SEG_AIM` - one bound window over the hold or the bank, aimed per record by
the rate hook (`vp_pmbig`'s in-place arm). LASTX plays in place on 86Box's
`286-victory` at kern_big's rate; KERN-MEGA-PLAN 14.73.1 is the record.
`vp_rclk1` stays: the copying bank is still what a session with no bound
window, or an XMS bank in real mode on kern_mega's fallback, plays through.
