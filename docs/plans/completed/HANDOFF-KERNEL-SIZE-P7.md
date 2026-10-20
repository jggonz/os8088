# Kernel size pass 6: the record, and what is left for a seventh

**PASS 6 HAS LANDED** on `kernel-size-p6`, cut from `elendilon` at
`ff1d1d2d`. The file is named `-P7` for the reason pass 5's record is named
`-P6`: each pass's record is named for the pass it hands to. Its
companions, and this file repeats none of them:

* **`docs/plans/completed/HANDOFF-KERNEL-SIZE.md`**: pass 1's handoff, still
  the authority on **method**.
* **`docs/plans/completed/HANDOFF-KERNEL-SIZE-P6.md`**: pass 5's record. Its
  §2 costed list and §3 (a near jump across sections assembles silently) still
  stand, and nothing here re-proposes one of its refusals.
* **`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-28.md`**: the per-commit
  measurement that was this pass's target list. Its *"The concepts that have
  not had a pass"* paragraph is the brief.
* **`docs/KERNEL-MEMORY.md`**: where the budgets stand. Blessed at the close
  of this pass.

---

## 0. THE BRIEF AND THE OUTCOME

The brief: the kernel commits that had not had a size pass (everything that
landed after pass 5: `5e88d453`, `dd89dfc9`, `5e1b8203`, `5c12c7a0`,
`74258a5d`, `dcace0ac`, `ddaddc63`, **851 bytes of `kern_big` and 23 of
`kern_small`**, or 20 counting `dd89dfc9`'s −3). The target was **50% of the resident bytes they added** for
anything not on a hot path. Hot paths could be optimised too, **at no more
than 2% speed penalty**. Size opportunities outside the target code were in
scope. One agent took each large change, and one agent took the small ones
together.

Resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`, measured by
`tools/kernsize.py --json`, never by rungs:

| | base `ff1d1d2d` | close | Δ |
|---|---:|---:|---:|
| kern_big resident | 100,860 | **99,998** | **−862** |
| kern_small resident | 68,591 | **68,141** | **−450** |
| kern_big `KERN_SIZE` | 107,008 | **105,984** | **−1,024** (both rungs the cycle crossed, uncrossed) |
| kern_small `KERN_SIZE` | 71,168 | **70,656** | **−512** |
| kern_big `.text`+`.bss` left of `KERN_CODE_MAX` | 12,209 | 12,591 | +382 |
| overlay (`.ovl`+`.ovlw`), big / small | 6,942 / 3,444 | 6,939 / 3,443 | −3 / −1 |

**The pass bought back more than the whole cycle spent.** The 2026-09-28
report put the branch at +803 resident over the #199 squash on kern_big. The
close stands **59 bytes under the squash** on kern_big and 745 under it on
kern_small. `KERN_SIZE` is back to the squash's 105,984, with both kernels
smaller than it inside their rungs.

## 1. WHAT THE WORK ADDED, AND WHAT CAME BACK

Four agents worked in parallel, each in a private worktree cut at `ff1d1d2d`
and each owning a set of kernel files. The coordinator merged each branch
with `--no-ff` and re-measured after every merge. The four branch deltas sum
to the merged tree's delta to within three bytes: kern_big came out 3 smaller
merged than summed, because a changed layout put jumps in short reach. After
every merge the coordinator ran the fast tier, `make small` and `os88ovlchk`.

| topic | target commits | added (big / small) | from the target's own lines (big / small) | outside it, same agent (big / small) | total (big / small) |
|---|---|---:|---:|---:|---:|
| **speaker**: `OSAPI_FSX_SPK`, `spk_off`, `sch_isr`'s sample-ISR arms, `fsx_wait`'s `hlt`, the 286 pulse floor | `5e88d453` (half), `dcace0ac` | +333 / +11 | −113 / −5 (34% / 45%) | −139 / −123 (`snd.inc`, `sched.inc`, `fsx.inc`) | **−252 / −128** |
| **blitp** (HOT): `OSAPI_GFX_BLITP` walking a region (5.4.3.6) | `5e88d453` (half) | +112 / 0 | −13 of the cold walk, and the 1bpp walk's tests replaced by a mask | −115 / −30 (`vga12.inc`, `font.inc`, `band.inc`: jcc reach, edge masks) | **−128 / −30** |
| **bigvol**: FAT16 volumes past 32MB | `dd89dfc9` | +259 / −3 | ~−72 / — (28%, a per-site count) | −226 / −189 (`disk.inc`, `diskw.inc`, `filecp.inc`) | **−298 / −189** |
| **small**: Disk window sizes (K, M, two decimals), a runtime claim marks Disk windows stale, `fm_focus_x`'s refresh debt | `5e1b8203` `5c12c7a0` `74258a5d` `ddaddc63` | +147 / +12 | −61 / −8 (41% / 67%) | −122 / −97 (every file the other three did not own) | **−183 / −105** |
| coordinator: the BPB multiply's overflow refusal (§3) | | | | +2 / +2 | **+2 / +2** |
| **total** | | **+851 / +20** | | | **−862 / −450** |

**Against the 50% target, honestly.** No topic cleared 50% on its own lines
alone. The pass as a whole took out **101% of what the targets added on
kern_big**, and more than half of that came from the files around them, which
the brief allowed.

### 1.1 Why each topic stopped where it did

* **speaker (34%)**: the door went 280 → 178 bytes. What is left is the two
  vector writes, the channel-0 and channel-2 PIT programming and the refusal
  ladder, and the agent judged all of them near their floor. Going further
  needs an ABI change. The largest candidate is dropping the unused DX:BX
  return; the refusals are in §2. The IRQ0 arms went 24 → 15. The owner has since ruled that packages are trusted, so
  the ABI route is open: LAST-DROP-BYTES §7.11 prices it at ~70-80 bytes,
  deferred for time.
* **blitp (hot)**: the walk's own cold code went 34 → 19 bytes and
  `cw_gfx_blitp` is gone, but the request block in `gfx_blitp` grew 6 bytes
  when the probe moved inline. What the pass bought instead is speed. The 1bpp
  fragment walk no longer tests anything for the planar feature: the entry
  carry becomes a rounding mask once per walk, and a piece is drawn by
  `call word [gfx_bpfn]`. The covered 1bpp blit is now faster than it was
  before the planar feature existed.
* **bigvol (28%)**: the 32-bit sector arithmetic is the feature itself. The
  cuts were structural instead: BPB rule 8 now falls out of rule 14, and
  `dsk_lbahi` sits after `dsk_rah_busy`, so one word compare gates the cache.
  A kern_big build without `OS88_BIGVOL` does not assemble, so the 28% is a
  per-site count and not a measurement.
* **small (41%)**: 28 of the target's 147 bytes cannot shrink. The 16 bytes of
  `.bss` for `FS_FREEH`/`FS_USEDH` are the only way to hold two decimals of K
  in a word-sized cache. The 4-byte `ddaddc63` fix and the 8 bytes of
  `fmv_icostale_but` are fixes that kern_big needs. Taking those 28 out, the
  recovery is 61 of 119 (51%).

### 1.2 Hot paths: measured, and all equal or faster

| path | before → after | how |
|---|---|---|
| `sch_isr`, the idle tick | 6,285 → **6,245** cycles (median of 60) | MartyPC, 5150 CGA, entry to `sch_resume`'s `iret`, base built separately |
| `sch_isr`, a rate bracket without the speaker | about −36 cycles | hand count |
| `sch_isr` under the speaker | identical sequence | hand count |
| `sch_switch` | about −19 cycles | hand count |
| Live pass, Hercules, uncovered / covered | **−0.33% / −0.47%** | MartyPC, `vp_lblit` to `.th`, minimum per blit geometry |
| Live pass, VGA 1bpp, uncovered / covered | **−0.27% / −0.52%** | same |
| `gfx_blitp` VGA4, one fragment / a region walk under a Disk window | **−0.51% / −1.14%** | same |
| gfxbench, GFX_BLITP | −1.6% (Hercules), −0.3% (VGA) | MartyPC gfxbench |
| gfxbench, GFX_BLIT1 / FONT_RUN | −0.16% / −0.1% | same |
| `dsk_xfer` cache gate, per run | about −31 cycles | hand count |
| directory walks (`dsk_find_x`, `dskw_find`, the mount scan) | about −35 to −65 per entry | hand count |
| `dsk_rah_serve` / `dskw_take1` / `dskw_flushrun` | about +15 to +35 each, under 1% of the transfer each wraps | hand count |
| Disk window size figure, K arm | about −60 cycles a file (`aam` in place of `div`) | hand count |

## 2. BEHAVIOUR CHANGES, AND THREE DEFECTS FIXED

**Defects the pass found and fixed:**

* **`[gfx_dnest]` underflowed to 255 on an extended desktop** after any
  Live-in-colour pass. The walking `gfx_blitp` call took no display hook of
  its own, but it left through `.out`, and `.out` decremented a nest nobody
  had entered. At the base the play also ran about 5x slower (46 frames in
  the sampling window against 243). It now leaves through `.pops`.
  `tests/vidlivext.py` (soak) reads 255 in all 80 samples at `ff1d1d2d`, and
  never more than 1 at the close.
* **`fcp_relink` tested the wrong half of `[dskw_fsec]`.** `dd89dfc9` moved
  the "no free slot" sentinel to the high word on kern_big. `fcp_relink`
  still tested the low word, so moving an item into a full directory could
  take a stale slot. It now tests `DSKW_FNONE`.
* **The stack-overflow death panel never reached its hang.** `sch_stkdie`
  called `sch_diepanel`, which moves SP to task 0's stack, so the closing
  `ret` popped whatever sat at `STK0_TOP`. It now falls through into the
  panel, which ends in `hlt` forever.
* **A hostile BPB could overflow `NumFATs × FATSz16`** (coordinator, +2 bytes
  on each kernel, found by the bigvol agent). A driver volume's FATSz16 has no
  cap (SPEC.md 18.2 rule 10), and the multiply's DX was ignored under a
  comment claiming DX = 0. It is carry-checked now, like every other sum in
  the derived layout.

**Deliberate changes:**

* **BPB rule 8 is folded into rule 14** (SPEC.md 18.2). The same volumes are
  refused, and one more is accepted, correctly: on kern_big, a volume with
  more than 65,535 sectors whose first data sector plus one cluster runs past
  sector 65,535. The arithmetic there is 32-bit.
* **Internal register contracts changed:**
  * `spk_off` clobbers AX.
  * `fsx_setbios` preserves AX.
  * `snd_tick` no longer banks AX/BP.
  * `gfx_bpwalk` clobbers; its one caller restores everything.
  * mkdir/rmdir take `dskw_wrap7` and now really preserve BP.
  * `.frags` takes its walk kind from the entry carry.

  Each is documented at the routine.
* **New layout adjacencies are load-bearing**, and each is pinned by an
  assembly-time `%if`/`%error`. Each assertion was planted with a violation
  once to prove it fires:
  * `snd_ch2mode`/`snd_pcm_busy`
  * `snd_town_pri`/`snd_town_inst`
  * `sch_fast`/`sch_fcnt`
  * `spl_busy`/`spl_quiet`, which lives in `viddet.inc`
  * `dsk_rah_busy`/`dsk_lbahi`
  * `gfx_lmtab`/`gfx_rmtab`
  * `fm_s_size`/`fm_s_free`
  * `gfx_blit1_x` at `.cold`'s first byte
* **The speaker's open flag is `spk_seg`**, a word, and the ISR's offset half
  is no longer stored. `tests/vidspk.py` and `tests/vidspkat.py` follow the
  rename.

## 3. WHAT IS LEFT, costed

| candidate | kern_big | kern_small | why not taken |
|---|---:|---:|---|
| **`OSAPI_FSX_SPK` as a thin door**: no DX:BX return, no block, no range/bracket checks, the vector and PIT writes in `os88spk.inc` | **~−70 to −80** | | **DEFERRED FOR TIME, not refused**: the owner trusts packages (real mode), so the checks are not a reason. `docs/plans/LAST-DROP-BYTES.md` §7.11 is the row |
| the speaker door as an on-demand module | ~−178 | | the READ_SEQ objection (P6 §2): a video on a data disk would need the system disk |
| precompute the 286 pulse floor | ~−10 | | needs `cpudet.inc` and has `cpu_tier`'s hibernate problem |
| `hiber.inc`'s fourth channel-0 programming sequence onto `sch_pit0` | a few | | found at the close, not taken |
| `fcp_x6`/`fcp_x5` as aliases of `dskw_find`'s tail | −7 | | `stkbalance` reads source and cannot evaluate `%ifdef`; it reports a false stack imbalance |
| `mov ax, FERR_PROT; stc` → a shared tail, three sites | −3 | | not done |
| `FS_FREEH`/`FS_USEDH` summed at paint time | −16 | −8 | up to 32 `fmv_dient` calls per status draw, and the cache can be gone |
| `ct_cw_snd_beep` gated on kern_small (P6 §2) | | −2 | needs an early `%define` in `kernel.asm` |
| a local epilogue ladder in `CTRL.DRV`'s `.modc` | 0 | | ~−140 of MODULE image and no resident byte; `t_asmrules` knows only the kernel's ladders |
| `gfx_blit4`'s far exits, the mono `jne sw_*` dispatches, `font_run_x`'s far jccs | −3 to −6 each | | each needs a jump-over or re-aim costing +12 to +15 cycles a primitive (hot rule) |
| a DI bit marking a planar piece | −7 | | no free bit: 15 is the probe, 14 the walk request, 13 a legal step |
| the covered-blit walk through `gfx_clip_run`, kret ladders in the renderers | | | P6 §2's refusals, re-checked and still refused |

## 4. METHOD LESSONS

* **Every worktree started at the wrong commit again.** All four started at
  `b6e1a6d6` (#201), not the branch cut. All four caught it because the brief
  named the base commit and its figures, and all four reset and reproduced
  the base to the byte. Keep both in the brief.
* **Do not edit `kernel/` while a soak runs in the same checkout.** Three of
  the four agents did it once, and each time rows failed with *"the map
  describes a DIFFERENT kernel"*. The soak's own tree is frozen, but the
  rows' symbol reader (`os88sym`) re-assembles from the live checkout. The
  rows passed on re-run. It is the harness working as designed, and it cost
  each agent a re-run.
* **Give each agent its own scratch file names.** Two agents redirected soak
  output to the same `scratchpad/soak1.log`, and one aborted a run that was
  45 rows in.
* **One agent's scanner is worth handing to the others.** The small agent's
  four scripts (a `clc`/`stc` that repeats the carry, a flag write under a
  label reached only by the matching jcc, a `jmp` onto the next label, a
  redundant NUL store) found ~35 sites in files it did not own. The
  coordinator forwarded each list to the file's owner, and the owners
  verified every site by hand. Three sites had been superseded, and one
  `stc` turned out to be required.
* **A path no row reached got a row.** Nothing in the suite played a PWM
  clip through `OSAPI_SND_PLAY`, whose only caller ships on the live media
  alone. `tests/sndplay.py` (soak) traces every OUT to ports 40h, 42h and 43h
  on the desktop and inside a bracket. It passes identically at the base and
  the close, and it went red for each of three planted breaks.

## 5. WHAT WAS RUN

The fast tier at every agent commit and every merge; `make small` at every
merge; `os88ovlchk` after every merge. Per topic, on each agent's branch:

* **speaker**: 34 + 25 rows plus the new `sndplay`.
* **blitp**: 32 rows plus the new `vidlivext`, and knob builds NOPLANE,
  NOUNAL, BAND, NOCOLFAST, NOBLITCUT, NOSPLIT, NOSEAMCUT, GFXAUDIT,
  SCROLL_ROWBASE and KERN_EMU.
* **bigvol**: 92 + 36 rows, including 49 `dos*` rows.
* **small**: 41 + 21 rows.

All green, except the stale-map failures in §4, which passed alone.

On the merged tree (with the BPB fix), **72 rows across every topic, 72/72
green** in 747 s at width 4: `bootsmoke small128 tickzero sndplay vidspk
vidspk22 vidspkcounts vidkern vidplay vidlive vidlivevga4 vidlivext
vidlivecga blitp blitplane blitcut dispblitp dispband disptitle dispdrag
dispseam dispstrad paint1bpp paint1small bigvol hdboot hddcp kdhdd
hibernate* fcpcopy fcpapi fcpsmall dirwsize assocstale assocwake assocopen
fmcommit fmrefuse fdlgstore fdlgsmall heapcheck drvmove sndmove dosfile
dosdir dosfat lzfile lzmod dockmodule curdisk wdmenusu dispclose uiblock
fsxclip dispfsx schacct stk0water trkrate vidfskeys* pathcost dskwstage
inststate`. The whole soak tier was not run: it is
the owner's to ask for.

## 6. ON `elendilon-next`: the pass squashed onto #203

Upstream squashed `elendilon-pr` as #203 (`48178e44`) after its maintainer
added six fix commits on that branch. `elendilon-next` is `main` plus ONE
commit carrying this pass and `ff1d1d2d` (the HDD page), merged three-way from
their split with `elendilon-pr` (`bba1369c`). Where #203's fixes met this
pass:

* **`fcp_relink`'s sentinel** was fixed on both sides, identically.
* **`[gfx_dnest]` on an extended desktop** was fixed on both sides,
  differently: #203 cleared `[gfx_bp_hk]` at `gfx_bpwalk`'s exit (+5), and
  this pass leaves the walking call through `.pops`, past the teardown. The
  pass's fix is kept, and `vidlivext` is its gate.
* **The speaker door's SI/DI fence** (`SPK_E_ADDR` = 5, 38 bytes in #203) is
  ported onto the rewritten door at **30 bytes**. The logic is the same: SI
  below I_SIZE, and DI + 6 at most I_SIZE. The door is 208 bytes with it.
* **`FERR_BIG`'s saturation** (+11 on each kernel) now ends on the pass's
  shared `dskw_wbody.big` tail.
* **`hbm_wake` zeroing `[dsk_lbahi]`** (HIBER.DRV) and **`ASSOC_FIND`'s miss
  leaving through `.out`** merged as they were.

| | #203 (`48178e44`) | `elendilon-next` |
|---|---:|---:|
| kern_big resident | 100,914 | **100,039** (−875) |
| kern_small resident | 68,602 | **68,152** (−450) |
| kern_big `KERN_SIZE` | 107,008 | **105,984** |
| kern_small `KERN_SIZE` | 71,168 | **70,656** |

On the merged tree: the fast tier, `make small` and `os88ovlchk` pass. Of 50
soak rows reaching the merged fixes (every `vidspk*`, `sndplay`,
`vidlivext`, the blit, hard-disk, install, copy, hibernate, LZ, Tracker and
association rows, `bigvol`, `bootsmoke`, `small128`), 48 passed and
`vidspkshape` skipped for want of ffmpeg. `sndplay` failed at launch, with
MartyPC unable to find its `hdd` resource directory, and passed when run
alone.
