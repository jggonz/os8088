# Kernel size pass 15: the record, and what is left for a sixteenth

**PASS 15 HAS LANDED** on `kernel-size-15`, cut from `elendilon-next` at
`e4896481`. The file is named `-P16` for the reason the records before it are
named one ahead: each pass's record is named for the pass it hands to. Pass 14
wrote no record of this kind (its BRIEF and its agent notes in
`docs/plans/completed/ksp14/` are its record), so the pass before this one is
read there and in `HANDOFF-KERNEL-SIZE-P14.md` (pass 13's).

The brief is `docs/plans/completed/ksp15/BRIEF.md`; each agent's note is
beside it (`snd.md`, `recent.md`, `blob.md`, `gui.md`, `fsys.md`) and is the
authority on its own items. This file is the summary, the owner's decisions
and what is left.

## 0. THE BRIEF AND THE OUTCOME

The owner's words: *"This size pass will work on the recently committed and
not yet size passed kernel work, along with a general pass in an attempt to
recover half a kilobyte. Additionally, set one agent to try to get the
resident, non-mixer variant of sound.drv under 6kb, and as a bonus but not
required, the mixer version to 9kb."* Five agents, all at once.

**All three goals are met.**

Kernel resident (`.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`, plus
`.mega` on kern_mega), assembled, `e4896481` against the merged tip:

| kernel | base | tip | delta |
|---|---:|---:|---:|
| **kern_big** | 90,008 | **89,471** | **-537** |
| kern_small | 58,220 | 57,834 | -386 |
| kern_emu | 90,264 | 89,727 | -537 |
| kern_mega | 124,192 | 123,218 | -974 |

No `KERN_SIZE` rung moved on kern_big (97,280) or kern_emu; the bytes are
real and the next rung is that much further off.

SOUND.DRV, bytes kept after the attach (claim in brackets):

| machine | base | tip |
|---|---:|---:|
| no mixer, neither PicoMEM nor MPU-401 | 6,392 (7KB) | **5,797 (6KB)** |
| no mixer, a PicoMEM | 6,392 (7KB) | 5,913 (6KB) |
| no mixer, an MPU-401 | 6,392 (7KB) | 6,098 (6KB) |
| a mixer (SB Pro, SB16) | 9,937 (10KB) | **8,622 (9KB)** |
| the image (load claim) | 11,737 (12KB) | 10,460 (11KB) |

## 1. WHO TOOK WHAT (kern_big resident)

| agent | concept | kern_big | the shape of it |
|---|---|---:|---|
| **fsys** | the file side | -146 | single-caller helpers dropping frames their callers never read (mount, BPB, FAT window, sort, driver loader, Disk window); `dskw_mounted` answering in ZF; `drv_check` leaving ES on the image |
| **gui** | the window/drawing side | -146 | ~15 routines onto `kent_di`/`kret_di`; `wm_bank4` for four-word copies; ~25 skip bytes; reloads and carries that were already known |
| **blob** | where code lives | -132 | **`.cfx` found as a home for boot-only code** (it lives exactly as long as the blob, costs no blob sector, is reached far); `desk_rowcalc` and `vid_ctx_capture` out of resident as macros expanded in CTRL.DRV, EXTD.DRV, `.cfx` and the overlay; `drvf_drv_call` gone |
| **recent** | the unpassed work since #313, then kern_mega | -36 (kern_mega -481) | lone relocation words as one `mem_rr_lone` row; `asc_use_x`, `ico_grow`, `ico_need`; kern_mega's `.mega` using the 286's shift counts and `push imm` |
| **gui (close-out)** | the CROSS-FILE leftovers of all five | -77 | `SKIP*` macros for skip bytes and known carries across the file side, `fdlg_rqsp`/`drv_blkseg` into `mem_rr_lone`, `hbk_drv_blk_call` and `os88ui_btn_f` deleted, one parity-pad pair folded |

The unpassed work itself was small on the 8086 kernels (+138 on kern_big
since #313): most of #318 and `f8391471` is kern_mega's, which had never had a
pass, and the recent agent's -481 there is its first.

## 2. HOT PATHS

Nothing on the hot-path list grew. The trades taken, each priced in its note:

- `kent_di` on ~15 per-operation routines (`wm_fit`, `wm_zoom`, `toast_place`,
  ...): ~90 clocks a call, none of them per pixel or per repaint.
- SOUND.DRV verbs 5 and 6 as one routine: ~20-65 cycles a 512-byte chunk.
- The volume popup's band through ES: ~4,000 cycles (~0.85 ms) a slider
  redraw, while dragging the popup only.
- `sbl_tick` now tests `[pm_up]` before the call: IRQ0 is CHEAPER without a
  PicoMEM.

New parity pads this pass: `clip.inc` (recent, replacing `ico_reloc`'s),
after `menu_kbnav` (gui), after `vid_ctx_act` (blob); the viddet/vidsel pair
was folded (gui close-out, -2 on kern_big). `hotpar` is green on both gated
kernels.

## 3. WHAT THE OWNER DECIDED

**APPROVED**

1. SOUND.DRV's third and fourth keep points:
   `[tiers] [PicoMEM] [MPU-401] [mixer] [attach]` (SPEC.md 34.15.1).
2. The volume popup's slider band in the popup's own save-under claim, through
   the opt-in `GFXE_BAND_SEG` in `apps/os88gfx.inc` (every other includer
   byte-identical). The popup's claim grows a KB while it is up on every
   adapter.

**DECLINED - binding on the next pass**

1. **Reordering SOUND.DRV's runs so the mixer precedes the MPU-401 and/or the
   PicoMEM.** An SB Pro would save ~215 bytes and keep the same 9KB claim; a
   standalone MPU-401 with no mixer (the XT MIDI machine) or a PicoMEM would
   keep the whole mixer run.
2. **Dropping LZB from the kernel** (`COMPRESS=lz4` default, -100 resident on
   both kernels). Twenty package builds SHIP LZB (SPEC.md 20.13.5); CLAUDE.md
   and the Makefile said otherwise and are corrected.
3. **`kent_di` on the damage-repaint path** (`wm_draw_win`, `wm_chrome_clip`,
   `wm_covered`, `wm_obscured`, `wm_ttl_seen`, `wm_clip_split`: -16 on both
   kernels, ~90 clocks a call, ~0.46 ms on a close over four windows). The
   owner's words: *"we can keep the speed - even though 0.46ms is not user
   visible, it can add up if we have a ton of windows."* Code on the repaint
   path is priced per window, not per call.

## 4. WANTS AN ABI BREAK

None. No frozen interface moved. `apps/os88gfx.inc`'s `GFXE_BAND_SEG` and the
`SKIP*` macros are additions.

## 5. DEFECTS FOUND

1. **`megamod` fails on kern_mega with a #GP at `1154:089A`** on every
   sound-playing leg, identically with `kernel/` at the pass's base: it
   predates the pass. Not chased (recent.md).
2. **`recorder` is intermittent and predates the pass**: `os88bisect.py
   sample` read **4/6 failed at `e4896481`** and 5/6 at the merged tip, both on
   leg 5 ("Stop left state 1"). The two commits before the base were fixes to
   this row; it is still flaky there.
3. CLAUDE.md's O88-COMPRESSION-PLAN row and the Makefile's `PKGZ` comment
   said LZB was not shipped; SPEC.md 20.13.5 says twenty builds ship it.
   Corrected here.
4. `sbl_v_open_in`'s header comment says `sbl_total` holds the capture
   capacity; nothing writes it (snd.md). Left.

## 6. WHAT IS LEFT

- **Room is gone in three places**, so the next "where code lives" move must
  make room first: the kern_big blob is 6,655 of 6,656, the boot overlay
  5,119 of 5,120 on kern_big and kern_emu, and CTRL.DRV kern_big 15,256 of its
  15,360. `.cfx` (blob.md) is where a boot-only body goes now.
- kern_mega: the five largest `.mega` routines (`mega_k09`, `mega_gpfix`,
  `mega_gpw`, `mega_fkill`, `mega_fd13`, 434-610 bytes) have not been read for
  structure; `pusha` in `.mega` (~10 a routine) was refused until kern_mega's
  stacks are measured.
- `osapi_seg`'s -1 and the `clip.inc` pad each pay with one more odd change in
  the right span (recent.md).
- `disk.inc`'s `align 2` after `dsk_img16` swallows any odd `.text` delta
  above it - an agent will measure one byte less than its routines add to.
- SOUND.DRV: nothing proposed. The mixer run is ~2,500 bytes; a 8KB mixer
  claim needs the reorder the owner declined.

## 7. WHAT WAS RUN

Each agent ran its own rows once after its last commit (the notes list them;
all green but `recorder` and `megamod`, section 5). The coordinator ran the
deduplicated union of every agent's rows - 149 rows - once on the merged tip
with `tools/os88soak.py`, and `test-full` once; the results are in section 7.1.

### 7.1 The final pass

- **The union, 149 rows, once, on the merged tip** (`tools/os88soak.py`,
  run `20261030-213536`): **149 ok, 0 FAIL, 0 SKIP** in 22 minutes, with
  `tools/picomem/build.sh` and `tools/setup-cc.sh` run first so `pmlib`,
  `pmmarty` and `megaovl` did not skip. `recorder` passed in it - it is
  intermittent at the base too (section 5).
- **`test-full`, once: 77 ok, 2 FAIL.** `docindex` was the coordinator's
  merge leaving `docs/INDEX.md` stale (regenerated). `guidelock-full` named
  three guide pages: SOUND_DRV was this pass's (the page said ~7K / 10K;
  now 6K / 9K, restamped), and **TOOL_VENC and VIDEO_O88 were already stale
  at the base** - their sources moved past their stamps before `e4896481` -
  and are left to whoever owns the Video Player's tools. Re-run, `docindex`
  is ok and `guidelock-full` fails on those two alone.
- `make`, `make small`, `make emu` and `make mega` build at the tip; the fast
  tier is 66/66.

## 8. METHOD LESSONS

- **Five at once on four cores worked** at `make -j1` each: load peaked ~14,
  no row was lost to it, and one row (`fatwpin`) ran 5x its declared seconds
  and passed.
- **Two merges failed only on `docs/INDEX.md`**: each agent's note regenerates
  it, so every merge after the first conflicts or goes stale. Regenerate after
  every merge, before `make`.
- **A size pass that reads a CLAUDE.md claim as fact can propose the wrong
  thing**: the LZB proposal rested on "LZB is NOT shipped", which SPEC.md had
  already overtaken. Check the claim against SPEC.md before costing on it.
- **A keep point is a PREFIX**: a later run's user keeps every earlier run, so
  the order of the runs is a choice between machine classes, and that choice
  is the owner's.
