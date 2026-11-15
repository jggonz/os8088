# soak-speed-2 — the office rows (sheet*, chart*)

One stream of the second soak-speed pass. The technique numbers are
docs/plans/completed/SOAK-SPEED.md's (cited in the rows as `SOAK-SPEED N`).
Rows: the 67 `sheet*` rows, the five `chart*` rows and their `mega-*` ports
on Bochs. The redline rows were left as the first pass left them.

## Where a sheet row's time went (read, before anything was run)

Every sheet row has the same shape: build a 360KB B: with `SHEET.O88` and
its fixtures, boot the CGA 5150 (`os8088_5150_cga_gla`), open B:, open the
document (which launches SHEET by association), drive menus and dialogs
with `os88mouse`, read the glass (`m.vram`) or SHEET's bss, Save As, and
poll the floppy through `os88flush` until the file has changed. 737
`M.settle` calls in 59 files, almost all straight after a mouse verb that
already waited `ui_done` (os88mouse's default), and the boot was the
default `launch` settle followed by a second settle.

## Changes

| what | rows | technique |
|---|---|---|
| `launch(boot=False)` + `m.run()` + `kernel_up` in place of the boot settle and the settle after it; `no_saver` kept (added to sheetfin/sheetfmt, which had relied on launch's own) | the 59 CGA/VGA sheet rows that used `M.launch` | 12 |
| the settle after `dispcp.open_drive` -> `ui_done`: what follows reads `wm_wins` or is an os88ui verb | same 59 | 4 |
| Save As: the three settles between File > Save As, the format radio, OK and the chooser's Save -> `ui_done`. Each click is aimed off the kernel's window record (`SF.fdlg_xy` reads `geom.top`), never off pixels | the 33 rows with the exact idiom | 4 |
| one key at a time into a polling field: `type_text(ch)` + `guest_sleep(0.15 / 0.3)` -> `text_taken(ch, ui=False, cap=)` - the key is TAKEN when the ring's head catches up, the old pace is the cap | sheetmctl, sheetmcmd3, sheetmevt, sheetmfile, sheetmmenu, sheetmdbox, sheetnote, sheetrefcyc, sheetdlgclose | 9 |
| sheetmctl NOESC: `guest_sleep(40)` for the loop to finish -> `until([sh_macro_running] == 0)`, bounded at 60 guest s and not raised (a run parked on a dialog by a broken CANCEL.KEY is the checks' to name) | sheetmctl | 5 |
| chartgal: the canvas-change poll and the screen-hash poll, both `time.sleep(0.25)` HOST-paced, and the settle after them -> one `ui_done`. Chart has no worker and arms no timer, so the Gallery pick's render and blit ARE the ui_task pass `ui_done` waits out; the canvas is then read on a stopped guest as before | chartgal, mega-chartgal | 4, goal 2 |
| chart rows: `os88ui.boot(..., quick=True)` and every `ui.settle()` -> `ui.still(0)`: every read after them is Chart's bss or the canvas | chart2ser, chartexp, chartgal, chartname, chartread (+ mega-) | 12, 4 |
| `SF.menu(m, mo, ...)` for every `mo.menu` on SHEET's own bar (88 sites + sheetfmt's own): os88mouse's drag waits "the press to be taken" with `ui_done` capped at PRESS_GUEST, 3 guest s - and SHEET's menu tracking YIELDS with the button down, so ui_task never sleeps and EVERY press paid the whole cap, unlogged (a capped `ui_done` writes nothing to the waitlog). Now the press is confirmed on `[sh_mopen]` leaving SH_M_NONE (bounded at the old 3 s) and the drag goes through os88mouse's `aim`. A press that is not on a front Sheet window is the plain `mo.menu` | every sheet row that opens a menu | 4, hidden cap |
| `SF.held(m)`: the settle while a button HOLDS a pull-down open - ui_task never idles there either, so `settle(m)` paid its full long window, ~13.5 guest s a hold (sheetmtail: 27 of 64 waited guest s in two). `settle(quiet=0.25, stable=2)`: two ~1.1 guest s still windows, which a one-shot pull-down draw is well inside | sheetkeys, sheetmmenu, sheetmsep, sheetmtail, sheetopts, sheetrefcyc, sheetdbcmd | 4 |
| `tests/paintmove.pkg_syms` caches the map per SOURCE CLOSURE (`os88fixture.cached`): keyed on the bytes of the source and of every file it `%include`s/`%incbin`s (resolved as nasm resolves them here), the -I list and nasm's version. SHEET's assemble is ~5 CPU s, 11-12 wall under load, and ~25 sheet rows paid it (sheetmtail and sheetmove twice); a hit is 0.08 s. Maps checked equal to a fresh assemble for sheet, chart and paint; an edit to an included file is a miss (checked) | every `pkg_syms` caller, including the paint rows | 16 |

## Measured

**Guest seconds per row** (load-independent; each MartyPC instance's cycle
count at close, `.scratch-agent`'s wrapper round `Marty.close`). Before is
`git show` of the starting commit's script, run with `OS88_FIXCACHE=0`;
both on the same loaded box (8 agents, 4 cores), all green both ways:

| row | before guest s | after guest s | |
|---|---|---|---|
| sheetrowh | 132.7 | 105.7 | -20% (eight menu presses' hidden 3 s caps, the boot settle) |
| sheetmctl | 172.4 | 114.6 | -34% (the fixed 40 s, the caps, the per-key paces) |
| sheetdec | 194.6 | 178.1 | -8% (four opens and four saves: the floppy IS the row) |
| chartgal | 140.6 | 96.0 | -32%; host wall beside it 230 -> 138 s: the host-paced canvas/screen polls (unlogged, `fbuf` + md5 every 0.25 s) were ~120 of the old row's host seconds |

sheetmtail's two held-menu settles were 27.1 of its 64 waited guest s
before (`OS88_WAITLOG`); `SF.held` is ~2-3 guest s a hold.

**The scoped soak** (`os88soak.py start -k 'sheet*' -k 'chart*' -k
'mega-sheet*' -k 'mega-chart*' -x sheetmove -x sheetmove-rt -x
sheetovlmove -x sheetxbench --marty-jobs 1`, both batches): **83 / 83 ok**,
2,419 s wall for the run. The rows had been DECLARED at 28,145 s (7.8 h)
between them; they ran in 2,419 s of row wall (0.67 h) on a box that had
quietened by then. The four excluded rows ran green in the batch-1 soak
(sheetmove 62 s, sheetmove-rt 57, sheetovlmove 59, sheetxbench 430) and use
none of batch 2's helpers.

**`secs=` re-declared** for all 82 emulator rows of that run (suite.py and
megaport.txt): 1.5x the measured wall rounded up to 5 s, floor 20 -
the set now declares 4,280 s where it declared 28,145. The timeout
(`4 * secs + 30`, CPU-charged) stays well above the loaded runs measured
here (sheetrowh 74-119 s wall standalone under load against a 210 s
timeout). Most of the old figure was never the rows: the sheet rows were
declared at 300-900 s and none ran past 60 s.

## Not done / next

1. **One boot for many sheet rows.** Boot (~14 guest s with kernel_up) and
   SHEET's launch off the floppy (~11 guest s, `dispcp.py:746`) are now the
   largest fixed costs left in every sheet row - about a quarter of a
   typical one. 60 rows boot the same system disk on the same machine;
   `os88marty.Forks` after the desktop, then `Marty.mount` of each row's own
   B: in the restored machine, would pay one boot instead of 60. Needs each
   row as a callable leg and a check that a swapped B: is read afresh
   (SPEC.md 18.9.1's motor-off rule).
2. **`settle(m)` after a mouse verb -> `ui_done`** in the rows that read
   only `m.vram` (VRAM is the framebuffer, which `ui_done` covers): 141
   sites in 30 non-macro rows counted, ~0.3-0.5 guest s each - ~1 min of
   the family's run. Not done: low value against touching every row again.
3. **os88mouse's PRESS_GUEST cap** is paid by EVERY `mo.menu` on any bar
   whose tracking yields (the kernel's own menu_track polls a level too),
   unlogged. SF.menu fixes it for SHEET; the general fix belongs in
   os88mouse (a `pressed=` predicate, or `menu_dropd`/`menu_y1` for the
   kernel's bar, as os88ui.menu_pick already does) and would reach every
   row that uses `mo.menu` directly. A capped `ui_done` writing nothing to
   OS88_WAITLOG is why it was invisible: logging the cap would show it.
4. sheetdec's own Save As spells the menu over two lines and kept its three
   intermediate settles (~1.3 guest s a save).
