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
| `tests/paintmove.pkg_syms` caches the map per SOURCE CLOSURE (`os88fixture.cached`): keyed on the bytes of the source and of every file it `%include`s/`%incbin`s (resolved as nasm resolves them here), the -I list and nasm's version. SHEET's assemble is ~5 CPU s, 11-12 wall under load, and ~25 sheet rows paid it (sheetmtail and sheetmove twice); a hit is 0.08 s. Maps checked equal to a fresh assemble for sheet, chart and paint; an edit to an included file is a miss (checked) | every `pkg_syms` caller, including the paint rows | 16 |

## Measured

(filled in after the 09:05 verification pass)
