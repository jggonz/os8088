# Kernel hot-paths pass 1: the log

The consolidated record of every change this pass made to a kernel hot
path: what it bought, what it cost in bytes, and how hot the path is. The
brief is `docs/plans/khp1/BRIEF.md` (its section 1 defines hotness H1-H4, and
section 7 defines the columns). Each agent's note in `docs/plans/khp1/` is
the authority on its own rows. This file collects them so the owner can read
the byte-costing items side by side.

**Status: IN PROGRESS.** Merged so far: files, sched.

## 1. Byte-costing items: the owner's picks

Each row is one commit whose subject starts `[+N]`, revertable on its own.
They are listed best value first: user-visible ms saved per resident byte,
weighted by how often the operation happens.

| id | agent | routine / path | hotness | change | kern_big B | kern_small B | cycles base -> tip (input) | user-visible (operation, adapter: ms base -> tip) | M/P | 286 | commit | FREE / [+N] |
|---|---|---|---|---|---:|---:|---|---|---|---|---|---|
| files-1 | files | `fm_c_up` (files.inc) | H4 | sync only a window the globals do not describe; the quiet stand's boot-sector read was always followed by `fm_go`'s own | +5 `.cold` | +5 `.cold` | int 13h 4 -> 3 per Up (motor stopped) | Up One Folder, CGA: 1,336 / 1,122 -> 1,048 / 912 ms (two folders, ~-250 ms, one isolated sector) | M | same call saved | `048c7097` | **[+5] OWNER'S PICK** |
| files-2 | files | `fm_scroll_by` middle tier, `fm_draw_rows` split (`fm_draw_rcore`) | H3 | a windowful scroll fills left of the bar and moves the THUMB through `fm_scrollpaint`'s tail (`os88ui_sbmove`, 3 calls) instead of `os88ui_sbar` (16) | +7 `.cold` | +7 `.cold` | 64 -> ~51 drawing calls (PgDn, 12-entry folder) | PgDn CGA 131.2 -> 113.6 ms, PgUp 124.2 -> 105.4 (os88hot ab -n 3). Herc/VGA list PgDn take the blit tier there: 0; their icon-view scrolls take this tier. **CHANGES TRANSIENTS** (fewer): CGA PgDn 9 -> 7 frames of visible redraw, 8 -> 6 flashing, worst 1,487 -> 927 px | M | CPU-only, same | `e02c1b7e` | **[+7] OWNER'S PICK** |
| files-3 | files | `ico_pass_bb` (icons.inc) | H1 inside H2 | at shift 0 with the icon wholly inside the stride, one word or / and-not a row word, the op picked once a pass; the byte walk kept for shifted and edge icons | +80 `.text` | +80 `.text` | one 16x16 pass ~13,000 -> ~5,000 cycles (~2.9 ms an icon, CGA) | PgDn CGA 113.6 -> 99.0 ms (131.2 at base, both items -24.6%), PgUp 105.4 -> 91.9 (-26.0%); Herc PgDn 131.9 -> 118.0 (-10.5%), PgUp 124.0 -> 109.3 (-11.8%); open A: CGA `ico_pass_bb` 24.7 -> 15.5 ms a rep. VGA/EGA 0 (planar pass) | M | PREDICTED a win (fewer instructions; a 16-bit bus takes the word op whole when DI is even, which an aligned 16px icon's is not always) | `f183b808` | **[+80] OWNER'S PICK**. Crosses kern_big's rung: KERN_SIZE 96,768 -> 97,280 |
| sched-1 | sched | `sch_isr` wake loop, `sch_switch` scan, `task_spawn`, `task_exit` | H2 | the two loops stop at `sch_top`, one past the highest slot that can be live (SPEC.md 8.1.3) | +47 | +47 | idle tick 17,558 -> 11,533 (CGA, kern_big); 12,393 -> 10,897 (kern_small); a free slot in the scan ~144 -> 0; a no-op yield ~2,630 -> ~1,100 | `OSAPI_TASK_YIELD` 644 -> 283 us (sysbench, CGA, -56%); mouse packet to UI pass, scheduler share 0.74 -> 0.38 ms; menu open 45.0 -> 42.9 ms and close 24.3 -> 22.7 ms; pointer sweep busy 236.6 -> 189.8 ms (-19.8%); typing 31 keys busy 618 -> 545 ms; idle busy 6.5% -> 4.1%; with Fractal's worker running a tick 11,811 -> 9,516 cycles (+0.9% to the render) | M (all; ms rows are sched-1 + sched-2 together, os88hot `ab`) | fewer iterations on every CPU; the parity of `sch_isr`, `sch_switch` and everything after them is kept by three never-executed pads (`t_hotpar` green) | 488ea2d5 | **[+47] OWNER'S PICK** |
| sched-4 | sched | `sch_idle_body`, `sch_isr`'s wake loop, `sch_switch`'s fallback | H2 (every interrupt while idle) | the idle task halts again after an interrupt that posted no wake; the tick's wake loop posts `[sch_idlewake]`, the "nothing ready" fallback spends it (SPEC.md 8.1.2.5). Not on kern_emu | +12 | +12 | idle tick 11,205 -> 9,982 (CGA); kern_small 10,897 -> 9,362 (with sched-2); the idle task's yields over 30 mouse packets 218 -> 29 | pointer sweep busy 191.1 -> 175.9 ms (-8.0%); typing 31 keys busy 544.6 -> 516.7 ms (-5.1%); idle busy 203.2 -> 179.7 ms / 5 s (-11.6%); ms unchanged (+0.2-0.3%, rep noise; a wake pays ~90 cycles more before its yield) | M (os88hot ab vs 89fe86d7) | none beyond the cycles; parity kept by one pad | d0ad21da | **[+12] OWNER'S PICK** |

## 2. Free items: faster and no resident byte added

| id | agent | routine / path | hotness | change | kern_big B | kern_small B | cycles base -> tip (input) | user-visible (operation, adapter: ms base -> tip) | M/P | 286 | commit | FREE / [+N] |
|---|---|---|---|---|---:|---:|---|---|---|---|---|---|
| files-4 | files | `assoc_scan` (assoc.inc) | H4 (listing, harvest) | ES = DS and `cld` once in front of the walk, not per row | 0 | 0 (no OS88_ASSOC) | median 1,959 -> 1,530 cycles a call (os88hot cycles, opendisk, CGA) | first open of B:, ~31 calls: ~-2.8 ms | M | same | `31c9775e` | **FREE** |
| sched-2 | sched | `ui_task` (the pass's first look at the event ring) | H2 (every pass) | enter the drain through `.yield`'s byte compare instead of a full `evq_pop` on an empty ring; `ui_drain` stored and decremented as a byte to pay for the near jump | 0 | 0 | ui_task + evq_pop ~2,180 -> ~1,870 a pass; idle tick 11,533 -> 11,205 on top of sched-1 | every UI pass, idle or event: ~300 cycles (0.06 ms) off the time from an event's wake to its handler | M | none | 31309aca | **FREE** |

## 3. Where the time goes: the ranked profiles

One sub-section per agent, copied from each note's WHERE THE TIME GOES.

## 4. Totals

Resident bytes and `KERN_SIZE` on all four kernels at the base (`a7ee7fab`)
and the tip, and the headline user-visible operations before and after.
