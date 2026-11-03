# Code review: the last two cycles (2026-11-01)

A read-only review of the work since the #313 squash, taken at
`origin/elendilon` **bee18cff**:

- **Cycle A** is `a9611731..c2e139eb`, the work squashed to main as #318.
- **Cycle B** is `f8391471..bee18cff`. It starts at the post-#318 re-cut and
  includes WIN16 (#320, merged in at e88b5c64), the heap granule, the
  real-mode fsx bracket, kern_mega size passes 1 and 2, the SOUND.DRV record
  ring and kernel size pass 15.

**How it was done.** Nine review agents ran in parallel, one per area. Each
read diffs and code only: nothing was assembled into `build/`, booted or
soaked. They covered:

1. Cycle A: the kern_mega memory refactor.
2. Cycle A: the rest of the kernel, kerndos and the drivers.
3. Cycle A: WaveRack.
4. Both cycles: the other packages.
5. Cycle B: `fsx.inc` and the Video Player.
6. Cycle B: the kern_mega size passes.
7. Cycle B: the heap granule and `OSAPI_MEM_P`.
8. Cycle B: SOUND.DRV, ETHER.DRV and the WM/desk/disk diffs.
9. Cycle B: WIN16.

**What the verdicts mean.** Line numbers are at bee18cff.

- **CONFIRMED** means the defect was re-read in the code after the agent
  reported it.
- **PLAUSIBLE** means the agent traced it but nobody re-checked it.

None of these findings has been reproduced on a machine.

## Findings, most severe first

| # | where | sev | verdict | defect | failure |
|---|---|---|---|---|---|
| 1 | `kernel/fsx.inc:1335` with `:458-462` | high | CONFIRMED by reading | In the real-mode bracket, `fsx_rcall` calls `sch_fast_off` before `.restore` calls `spk_off`. `spk_off` then reloads channel 0 with `[spk_rdsv]`, the bracket's fast divisor. `.restore`'s own `sch_fast_off` does nothing because `[sch_fast]` is already 0. | A package that leaves `OSAPI_FSX_SPK` open when it returns from a real-mode bracket (a supported exit, SPEC.md 34.11) leaves the desktop with IRQ0 at about 582 Hz, every interrupt counted as a full tick. Ticks, 40:6C and the clock then run about 32x fast. Fix: close the speaker before the rate goes off in `fsx_rcall`. |
| 2 | `apps/win16/w16load.inc:354` (`w16_layout`) | high | CONFIRMED | The total paragraph count of all segments is added up in BP with no carry check. There can be up to 64 segments of up to 0x1000 paragraphs each. | An NE file with 16 or more segments of 64KB wraps the total. The program gets a tiny `OSAPI_MEM_P` claim, and the segment copy, the relocations and the start frame then write into foreign heap where the load should have refused with "Not enough memory". |
| 3 | `apps/recorder/recorder.asm:651` (`rc_ontimer`) | med-high | CONFIRMED | The timer handler calls `rc_draw_status` and `rc_repaint` without `OSAPI_WM_CLIP_SET`. A timer arrives with no clip region armed (SPEC.md 111.3, 11.3). | While recording with the Recorder covered or minimized, its status lines paint over the covering window about 9 times a second. When the take ends, a white fill the size of its content lands on top of whatever is there. |
| 4 | `kernel/megados.inc:1539-1556` (`mega_dhcfill`) | med | CONFIRMED | The park header's descriptor 28h (the text page) is built from ES as if ES were a real segment. Since MC7, ES there is the selector `hbm_stageseg` returns, as `mega_dosgo` says three lines above the call. | That descriptor's base becomes selector x 16 instead of B8000h or B0000h. The park stub's bad-sum message (and `DOSRMARK=1`'s trace) is written to an arbitrary address below 1MB, so the user sees a silent wait for a key. Fix: use `[mega_dstg]`. |
| 5 | `apps/realdmg/rwaot.inc:1327` (`rw_hyload .free`) | med (kern_mega) | CONFIRMED | `OSAPI_XMEM_FREE` is called with AX = `[rw_hybase]` but DX left holding a selector, not `[rw_hybase+2]`. That happens on the `.pe` path and on the `rw_apart` failure path. | The free is refused, then the token is zeroed. Up to 384 KB of pool and its SUBC selectors are orphaned until the window closes. Fix: `mov dx, [rw_hybase+2]` before the call. |
| 6 | `kernel/memory.inc:4557-4559` (`mem_regrow`, KB entry) | low-med | CONFIRMED | `mov cl,6 / shl ax,cl / jc .fail`. On an 8086, CF is only the last bit shifted out, which is bit 10. | `OSAPI_MEM_REGROW` with 2049 KB wraps to 64 paragraphs and shrinks the claim to 1 KB, with CF = 0. `mem_claim_x` guards this exact case with `cmp ax, 1024` first, and the regrow door does not. This predates the granule work, but that work rewrote the routine. |
| 7 | `apps/win16/w16bmp.inc` `w16_bmfill` / `w16gdi.inc` `w16_dibrow` | med-high | PLAUSIBLE | A row is converted into `w16_gbuf` (1,920 bytes), but `w16_bmnew` accepts widths up to 32,767. | CreateDIBitmap or LoadBitmap of a 4096x8 strip overruns into the bss that follows. |
| 8 | `apps/win16/w16dlg.inc` `w16d_erepl` | med | PLAUSIBLE | `room = EC_LIMIT - length` is computed unsigned. WM_SETTEXT ignores the limit, and EM_LIMITTEXT can lower it below the current length. | Set the text past the limit, then type or paste: the 256-byte edit block overflows into the USER heap. |
| 9 | `apps/win16/w16load.inc` `w16_frame` | med | PLAUSIBLE | A non-zero `NE_SP` from the header is not checked against `[w16_dgsize]`. | The start frame and the stack can land up to 64KB past DGROUP. |
| 10 | `apps/win16/w16cens.inc` ~70-95 | med | PLAUSIBLE | The census sums its bitmap offsets in 16 bits with no carry check, and segments may overlap in the file. | The census claim is undersized and its writes land outside it. |
| 11 | `apps/win16/w16res.inc` `w16_rscan`, `w16_copyres` | med-low | PLAUSIBLE | The entry loop has no bound against `w16_rtlen`, `.skip`'s `mul` wraps, and a count-0 type can walk past the table. | Writes past the kept resource table, or FindResource/LoadString loop for ever on a crafted count. |
| 12 | `apps/video` `vp_smov` (~12331) | med | PLAUSIBLE | `vp_pseg` (the poster) and `vp_p8seg` (the VGA8 palette) are banked as real segments and stay movable while `fsx_rprepf`'s claims can compact. | On a fragmented heap the bracket reads a freed or moved palette, giving wrong colours. |
| 13 | `drivers/ramdisk/rdstore.inc:342` (`rd_at`) | med (kern_mega) | PLAUSIBLE | CF = 0 with DX = 0, the null selector `SEG_AT` returns when the LDT is full, is taken as success. | Preserve or Load with no free LDT entry raises #GP where it should have refused with `RDERR_*`. `apps/os88partsbody.inc`'s `op_at` may share the same shape. |
| 14 | `apps/recorder/recorder.asm:594` (`rc_pump`) | low | PLAUSIBLE | The overrun bound `RC_RL` is one DMA block too loose. | Up to a block of torn data is copied into a take with no OVERRUN reported. The same handler also letters the status lines over the About card (`[rc_abon]`). |
| 15 | `apps/waverack/wrxport.inc:1083` (`wt_halt .drain`) | low-med | CONFIRMED by its agent | It spins on `[wt_mixing]` with no `OSAPI_TASK_YIELD`, unlike `wt_topupn` and `wc_own`. | Stop, Pause or a seek stalls for hundreds of ms on an XT. |
| 16 | `apps/waverack/wrrec.inc:878, 904-960` | low | CONFIRMED by its agent | A take with an odd length gets no RIFF pad byte, and its RIFF size is off by one. | Strict WAV readers may reject roughly half of all takes. |
| 17 | `kernel/mega.inc` ~10840 (`mega_segf .free`) | low | PLAUSIBLE | SEG_FREE frees any row of `mega_wtab` with no owner check. | One package can free another's window, and the other then faults. |
| 18 | `kernel/mega.inc` 11615-11680 (`mega_rseg`/`mega_useg`) | low | PLAUSIBLE | A ustab row is never cleared when its claim is freed or moved, and the table has only 8 rows. | A later BIOS return of that paragraph gets a dead or moved selector. Past 8 rows, the following `pop es` raises #GP. |
| 19 | `kernel/memory.inc:5550` (`osapi_mem_p`) | low | PLAUSIBLE | `and ch, 0x7F` clears `MEMP_CONV` in the caller's CX, against the SDK's "every register not named is kept". | Nothing breaks today, since no caller reads CX afterwards. Fix either the code or the contract text. |
| 20 | `apps/thewire/thewire.asm` ~2958 | low | PLAUSIBLE | The `MEMP_REGROW` shrink stores AX into `[wr_catpara]` without testing CF. | Harmless unless a kern_mega pool shrink can be refused. |

### Lower and cosmetic

**WIN16**

- A type-3 relocation writes 4 bytes but is range-checked as 2.
- `w16_prologs` walks the entry table without bounding it against `w16_fend`.
- GWW_HWNDPARENT accepts a cycle, which makes the parent walks spin for ever.
- `biClrUsed` is ignored, so short DIB palettes are wrong.

**Video**

- The real-mode bracket's ROM `int 08h` chain runs on the task stack.
- A refused `OSAPI_FSX_RUN` reports "Real mode" on the info card.
- A serial lease whose ring is inside the package's own region can lose bytes.
  No shipped caller holds one.

**kern_mega**

- `mega_hibnc` now relies on the BIOS keeping BX, CX, DX, SI and DI across
  `int 13h`.
- `mega_enterf`'s header says BX is kept, and it is not.
- `MX_FWIN` is never cleared.
- `mega_mpsyncf` leaves SUB descriptors past a shrunk end live.
- `mega_rseg` and `mega_oddsegf` assume any small multiple of 8 is an identity
  selector.

**WaveRack**

- `wc_name` walks the whole folder once per candidate name, which is quadratic.
- The worker is declared restartable for its whole life.

**Heap and other packages**

- `mem_sum_kb` and its kin truncate half-KB claims.
- Comments in Apple II, Word and Scribe say `MEMP_REGROW` cannot resize a pool
  block, which is out of date.
- The `r` key in `tests/sbtest` still opens a record grant, which the driver now
  refuses.

## Checked and found sound

These areas came back clean. They are listed so the next review does not
re-derive them.

- **Kernel and drivers outside kern_mega's own files (cycle A):**
  - VESA's `vx_int10` stack hop.
  - HDD's `SEG_LIN` rewrites and bounce buffer.
  - kerndos's park, snapshot and HMA slot.
  - The `diskw` high-carry helpers.
- **kern_mega size passes 1 and 2:** every shared tail, folded table and stub
  stride, plus the debug-port gates.
- **The heap granule (all 79 `OSAPI_MEM_P` call sites):** units and
  overflow, 512-byte alignment of every base, the ABI lock (`DRV_VER` 12), and
  the Task Manager's half-KB cells.
- **SOUND.DRV's external record ring:** 64KB page tests, ISR discipline,
  teardown order.
- **Kernel size pass 15's frame pairings:** none of them adds a repaint.
- **WIN16:** the coroutine and gate layer, and about 130 `retf N` argument
  counts checked against Wine's `.spec` files.
- **The fsx real-mode bracket's prep and teardown:** frames, refusal paths, the
  disk twins, and PIC/IRQ handling.
- **WaveRack's RIFF walk:** every 32-bit sum is carry-checked, and no worker
  touches a file.
