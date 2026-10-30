# REALDMG-HIGHCACHE-PLAN - the recompiler's cache segments above 1MB on kern_mega

**Status: R1 to R4 BUILT (REALDMG-SPEC 16.2.6); R5 waits on the kernel**
(written 2026-10-25 on `mega-dmg`, built the same day). K1 was supplied by
the memory round as `SEG_SUBC`; R2 took one `OSAPI_XMEM_ALLOC` block rather
than an `OSAPI_MEM_HUGE` claim, so it needed neither MC5 nor a claim
record; the setting is the arm's Machine > Cache: All. R5, freeing the
cache before a hibernate, needs a package-visible hibernate notice
(KERN-MEGA-MEMORY-PLAN 8.4) and is the one step left. The rest of this
file is the plan as written. It
follows REALDMG-SPEC 16.2.5, which is built: kern_mega's arm of RealDMG
runs its round robin over a TABLE of cache tiers and fills that table from
conventional memory. This plan fills it from extended memory instead. It
waits on the kernel's memory consolidation round
(docs/plans/completed/KERN-MEGA-MEMORY-PLAN.md on
`claude/protected-mode-memory-consolidation-k1kjao`), because that round
changes what a claim and a code selector ARE, and so does this.

**Since then (2026-10-25):** that round's MC7 has landed and been merged into
`mega-dmg`, and RealDMG's prerequisite conversion is BUILT - REALDMG-SPEC
10.3.4: the arm asks `OSAPI_SEG` for every "claim + N" (the extra tiers
already as `SEG_SUBC` pairs inside cache B's claim, which is K1's twin
supplied by the kernel), and the shipped build refuses under `CPU_F_PM`.
What is left of this plan is R2 onward: the tiers out of the POOL, and the
`highmem` line in tests/megahigh.txt that the pool's no-regrow-past-64KB
rule now holds in place. Sections 1 and 2 below are as written, before MC7.

The owner's words: *"write up plans for being able to put the recompiler
segments in protected/>1mb ram; we may still do it for completeness sake,
and for being able to play even longer when ram there is abundant."*

---

## 1. What is true today

- **The tiers.** Cache A (64KB, conventional, with the recompiler's part
  after it), the core's holes, cache B (64KB, conventional), and since 16.2.5
  up to `RT_XMAX` = 5 extra 64KB conventional segments. On a 640KB machine
  the arm gets **two** extras: 277,204 bytes of code space where it had
  162,036 (docs/reports/REALDMG-MEGA-TIERS-2026-10-25.md).
- **Already above 1MB:** the cartridge (10.3.1, in place behind
  `OSAPI_XMEM_RUN`'s data selectors) and the 64KB block table (16.2.5,
  `rw_jtx`, the same cell's first selector). Both are DATA: nothing jumps
  into them.
- **The two names of a tier** (16.2.4): a DATA segment, which every write
  goes through (the translator, the tails' copies, the link patches through
  `tr_lpes`/`tr_lpx`), and a CODE selector, which every jump loads (the
  block table's +6, `S_JITOUT+2`, a `jmp far` in a chain, the link log).
  `rt_d2c`/`rt_c2d` and the core's `tr_lpx` convert between them by table,
  and the extra tiers' pairs are that table (`S_JITXT`).
- **Why the cache cannot simply go above 1MB:** a pool block has no code
  view. `OSAPI_MEM_CODESEL` (`mega_codesel`) answers an identity segment on
  the 512-byte grid (`(S >> 2) | 4`, a static LDT alias), the code half of an
  allocator PAIR (itself) and the data half of a pair (its code twin, -8),
  and REFUSES a lone data selector - which is what every package-visible
  claim above 1MB is (`OSAPI_XMEM_RUN`'s, `OSAPI_MEM_HUGE`'s windows). Only
  the kernel makes pairs (`mega_hiclaim2`), for a module image or an
  `OS88_HIGH` region.
- **What else stays conventional** and why, so it is not mistaken for this
  plan's job: the view and the core (the parts carve, 128KB: the core runs
  with SS = the view, and the view is reached by paragraph arithmetic -
  `tests/megahigh.txt`'s `highmem` line), cache A with the recompiler's part
  (a CS), the frame buffers, cartridge RAM (`S_RAMSEG + bank x 200h`). Those
  are the memory round's MC7 census, which names RealDMG among the six
  `megahigh` exemptions it converts.

## 2. What the memory round changes under this

Read off KERN-MEGA-MEMORY-PLAN at `42288dfe`:

- **MC7 drops the static aliases.** Identity selectors survive only below
  the heap floor; every claim above it gets LDT selectors "allocated with it
  and freed with it". So `(S >> 2) | 4` - what `OSAPI_MEM_CODESEL` answers
  for RealDMG's conventional cache segments TODAY - goes away, and the cell
  must answer some other way for every package that far-calls into a claim
  (RealDMG, 1942, Excitebike, the Sticky Apps, the C SDK's overlays, PiXEL's
  parts, LOOM's preview). **That is the kernel half of this plan, and the
  round needs it whether or not this plan is taken.**
- **MC3 makes pool blocks move** by copy and LDT rebase: holders name a
  block by selector and need no relocation proc. Both selectors of a pair
  are rebased with it (`mx_desc` rebases every descriptor whose base lies in
  the block).
- **MC5 makes one claim reach all free memory**, bridging conventional and
  extended under one run of 64KB windows walked by CX, with a 64K
  conventional reserve and an exact ask-first answer.
- **MC1 makes kernel caches above 1MB purgeable.** A package's claim is not
  a cache and is not shed.

## 3. The kernel half: a code twin for any claim (K1)

**Proposal: `OSAPI_MEM_CODESEL` makes the twin when there is none.** On a
lone LDT data selector that names (part of) a claim the caller owns, it
allocates a code descriptor over the same base and limit, records it with
the block, and answers it; the same DX answers the same twin every time
after. The twin is freed with the block and rebased with it by MC3's walk,
which already rebases every descriptor in a block's range.

- **Why on demand and not a pair for every claim**: an LDT entry is 8 bytes
  of a table MC7 sizes to what is used, and almost no claim is ever entered
  as code. A pair for every claim doubles MC7's LDT for nothing.
- **Why this cell and not a new one**: every package that enters a claim as
  code already asks it (the census above), and on a real-mode kernel it
  answers DX itself, so ONE binary stays right on every kernel - 16.2.4's
  property, kept. The contract widens from "answers" to "answers, and may
  allocate", which is additive (SDK `abi.lock.json`, no `PKG_FMT` bump,
  SPEC.md 20.2.0.1).
- **The record**: the twin's selector rides in the block's `mx_tab` record
  (MC1 grew it to 10 bytes; `MX_AUX` holds a cache's tag or MC3's token, so
  a twin wants a word of its own or a small side table of (data, code)
  pairs per instance - to decide when built; a side table keeps `mx_tab`'s
  size and is freed with the instance).
- **A HUGE claim's windows**: each 64KB window of an `OSAPI_MEM_HUGE` run is
  its own data selector, so each gets its own twin on demand. A window step
  of 8 is the run's DATA step only - the twins are wherever the LDT had room
  and are asked for one window at a time.
- **Refused**: a selector the caller does not own, a window of a 32KB-step
  (`BX = 8000h`) run - its windows overlap, so code in one is data in
  another's bounds - and real mode, where the cell keeps its old answer.
- **Cost**: ~80-120 bytes of `.mega` (ESTIMATE, priced against
  `mega_codesel`'s pair arm and `mega_ldt_alloc`), nothing in the shared
  window but what the cell already has, kern_big and kern_small untouched.
- **Gate**: a row claiming a huge block, asking a twin per window, copying
  a `retf` stub into each through the data selector and far-calling it
  through the twin - then moving the block (MC3's pack) and calling again,
  and freeing it and asserting the twins' entries are free. Red with the
  twin not rebased.

This is the only kernel work in the plan. It is best taken INSIDE the
memory round, as part of MC7's "every claim gets LDT selectors", because
MC7 has to answer the same question for the conventional claims RealDMG
uses today.

## 4. The package half (RealDMG's arm, `PKG_MEGA`)

**R1 - the tier table grows.** `RT_XMAX` 5 is set by `[S_JITUSED]`, a byte
with a bit a tier, and by `S_JITXT`'s 32 bytes of the view. With the pool
behind it the count is the machine's RAM: 3.3MB is ~50 tiers.
- `[S_JITUSED]` goes, replaced by the GENERATIONS the tiers already carry:
  a tier is used since the last flush when its word at `FFF0h` is above
  `[S_JITGF]`, the generation `rt_flush` recorded, and `rt_flush` zeroes
  every tier's word. That scales to any count, costs a word of the view,
  and removes the shift.
- `S_JITXT` moves to a free run of the view big enough for `RT_XMAX` x 4
  (the run past `S_JITLN`, 4849h-48BEh, holds 29 entries; a larger run is
  taken if the count wants it - the realdmg-fullstatic branch's `S_AOT*`
  words end at 4848h). `tr_lpx`'s and `rt_xlat`'s scans stay linear: links
  are made once, and a recycle converts each log entry once.
- **The link log** (124 entries in cache A's `FE00h`-`FFEFh`) is the next
  limit: with fifty tiers more links go into an OLDER tier, which is what is
  logged, and a full log refuses the link - correct, but each refused link
  is a lookup per execution. It moves to a 4-16KB window of the huge claim
  (`S_JITLSEG`; `tr_lpatch` loads ES from it instead of `S_JITSEGA`), sized
  by measurement (`[S_JITLN]`'s high water on a long play, logged).
- **`rt_forget` walks the whole 64KB table per recycle** (8,192 entries).
  With many tiers recycles are rarer, so this stays, measured first.

**R2 - the claim.** One claim, whatever the count: a package holds
`MEM_OWNER_MAX` = 8 claim records, and 16.2.5's first build spent one a
tier and refused the replay's claim with 91KB free (it grows cache B's
claim now). `rw_jitxget` takes ONE `OSAPI_MEM_HUGE` claim (the
package is `OS88_HIGHMEM` by then - MC7's conversion of RealDMG is the
prerequisite) of N x 64KB, asks a twin per window (K1), and hands each
(window, twin) to `rt_more`. Conventional extras stop: the conventional
memory goes back to the machine.
- **How many - a setting**, because "abundant" is the user's to say: the
  Machine menu's Cache item grows from 64K/128K to **64K / 128K / All**, All
  meaning MC5's exact ask-first answer less a reserve (the memory round's
  64K conventional reserve is the kernel's; RealDMG keeps `RW_XRESKB`'s
  extended-memory equivalent for a second program). The setting is
  `REALDMG.CFG`'s, beside `RW_CFGBIG`.
- **Real mode** (the arm on a kern_big machine, an 8088): unchanged, the
  16.2.5 path, as the arm already plans by `CPU_F_PM`.

**R3 - moves and frees.** MC3 moves the huge claim by rebase: the
selectors and twins stay valid, so the cache needs NO flush on a move -
better than today, where a conventional cache segment's move re-runs
`rt_init`. `rw_mreloc` stops tracking the extras (they are selectors). A
free is one `OSAPI_MEM_FREE`, the twins going with it (K1).

**R4 - Static's preload stops stopping.** A preload or a fixed table stops
at a full cache (16.8); with the pool's tiers a profile's whole list fits,
and `RW_JIT2N`'s "second segment if the profile names more" becomes "enough
tiers for the profile", from `rw_prfblocks`.

**R5 - the hibernate.** The hibernate writes every pool block (memory
round 1.6), so a 2MB cache is 2MB of image and of write. The cache is
regenerable, so the package frees its huge claim when a hibernate is
taken and claims it again after the resume (the tiers come back empty and
re-warm). How a package is told of a hibernate is the open question in 6.

## 5. Order, and what each step needs

| step | needs | gate |
|---|---|---|
| K1 code twin on demand | MC3 (rebase) for the move half; best inside MC7 | the twin row in 3 |
| R1 generations for `S_JITUSED`, the table moved and grown, the log in its own window | nothing - can be taken now, in conventional memory, against 16.2.5's squeezed leg | `rdgmega`'s squeezed leg at RT_XMAX 29, recycling every tier; `rdgstatic`'s squeezed rows (the harness runs the generation change too) |
| R2 the huge claim and the setting | K1, MC7's conversion of RealDMG (`OS88_HIGHMEM`), MC5 for "All" | `rdgmega`'s arm leg asserting the tiers are LDT selectors above 1MB; the squeezed leg over them |
| R3 moves | MC3 | a row packing the pool under a running game, no flush counted, cpu_instrs passing |
| R4 Static | R2 | `rdgstatic` under `make mega` |
| R5 hibernate | the answer to 6.1 | `megahiber` with a game open |

R1 is the only step that can be taken before the memory round lands, and
it is worth taking only if the conventional extras are kept - otherwise it
lands with R2.

## 6. Open questions

1. **How a package learns of a hibernate.** Drivers have `hbm_detach` and
   `hbm_reload`; packages have nothing. A package-visible "about to
   hibernate / resumed" notice (an `OSAPI_WM_ONWAKE` reason?) is an ABI
   addition the memory round may want for MC4's DOS-trip-offers-hibernate
   anyway.
2. **Does an 8-bit DMA ring stay low** (memory round MC9) - irrelevant here
   unless the sound path's ring is what holds the conventional reserve on
   a machine with a Sound Blaster.
3. **Whether K1's record is in `mx_tab` or beside it** - the memory round's
   call, since MC3 reshapes the record.
4. **A cap on tiers per game**: past the size of a game's code, more tiers
   buy nothing (Kirby's Dream Land's first level and miniboss are ~137KB,
   the report's 3.1). A profile (`NAME.RDP`) knows the size; "All" could be
   "what the profile names, plus half".

## 7. What the owner's report said, for the record

On the owner's mr286 (86Box) with the arm of 16.2.5 on kern_mega the game
ran *"much more smooth than big's build"*, the rate mostly 100% with bursts
between ~80% and ~115%; the shipped build on KERN_BIG with Cache: 128K
slowed to ~30% for ~2 seconds twice in the same replay, some of it bank
thrashing (kern_big has the bank cache and fetches through XMS; the arm
reads no bank). Nothing in the replay recycled a tier on kern_mega; this
plan is for the plays that are longer than the ones measured so far.
