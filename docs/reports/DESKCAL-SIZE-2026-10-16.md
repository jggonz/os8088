# DESKCAL.O88: where the 23 KB goes

**A measurement, not a description.** Taken 2026-10-16 on `elendilon-next`
at `2c6edaf`, in a build container, off the assembler's own map and listing
of `apps/deskcal/deskcal.asm` (SPEC.md 110.3). True of that tree only; a
second measurement is a new file. It is the breakdown asked for before any
size work on the calendar, and it proposes nothing.

## The totals

| | bytes |
|---|---:|
| image (`deskcal.bin`, what is loaded) | **18,976** |
| bss (zeroed at load, not on disk) | **3,709** |
| **resident while it runs** | **22,685** |
| file on disk, lz4 (`DESKCAL.O88`) | **16,171** - 85.2% of the image, 16 clusters at 360 KB |

The file compresses badly because the image is **90% code**: 1,805 bytes
are `db`/`dw` (1,118 of strings, 687 of tables, bitmaps and the package
header) and **17,171 are instructions**. So the disk figure follows the
code almost one for one, and every byte of code taken out is roughly
0.85 of a byte off the floppy.

## Who the image belongs to

Every byte attributed to the global label it falls under, and the label
to the file that defines it:

| source | bytes | share |
|---|---:|---:|
| `deskcal.asm` - entry, keys, menus, card presses, the edits, the tick, all menu strings | 4,657 | 24.5% |
| `dcdraw.inc` - the three layouts, the composers, hit tests, partial redraws | 4,507 | 23.8% |
| `os88stik.inc` - the Sticky App include | 3,764 | 19.8% |
| `os88ui.inc` - the alert and its button kit, the About card | 1,421 | 7.5% |
| `os88gfx.inc` - `GFXE_TEXT` + `GFXE_BITS`, the band composer | 1,228 | 6.5% |
| `dcrem.inc` - reminders | 1,046 | 5.5% |
| `os88line.inc` - the one-line editor | 986 | 5.2% |
| `dcdata.inc` - the payload: check, sorted insert, deletes, parser | 745 | 3.9% |
| `dcdate.inc` - date maths | 508 | 2.7% |
| header, icon, menu-list words | 114 | 0.6% |

**`os88stik.inc` is shared in name only**: DESKCAL is the only package
that includes it, so all 3,764 bytes are this program's. The four shared
includes come to **7,399 bytes, 39% of the image**. The calendar's own
source is the other **11,577, 61%**.

## By feature

The same bytes grouped by what they are for. Each routine is counted once,
under the feature it mostly serves. The shared includes are split where
their `%ifdef`s split them:

| feature | bytes | the biggest pieces |
|---|---:|---|
| **Widget treatment** (`os88stik.inc` under `STIK_WIDGET`: chromeless cards on the desk, the name tag that turns into a handle, hover and dwell, focus, slabs, re-fit) | **2,404** | `stik_tagdraw` 309, `stik_pbody` 263, `stik_slabs` 150, `stik_mup` 124, `stik_wsize` 120, `stik_hit` 112, `stik_click` 112, `stik_hvtick` 106 |
| **Editing** (the entry field, the time parser, sorted insert, delete and its question, its strings) | **2,139** | `dc_cappt` 298, `dc_parse` 158, `dc_ctask` 154, `dc_delans` 146, `dc_ains` 116, `dc_fopen` 112, `dc_edit` 88, `dc_askdel` 82 |
| **Reminders** (`dcrem.inc`, card 3, the Remind menu, snooze and dismiss, missed-at-boot, bell bitmaps x3) | **1,906** | `dc_crem` 298, `dc_c3sync` 151, `dc_c3calc` 135, `dc_remind` 123, `dc_missed` 104, `dc_hrem` 99 |
| **Month card** (grid, nav, today's big date, day select, month step, month names) | **1,728** | `dc_cmon` 325, `dc_hmon` 303, `dc_cday1` 265, `dc_rbox` 84, `dc_navlbl` 82 |
| **Drawing core** (geometry tables, compose/ctext/put* helpers, partial-rect recompose, the press-hit dispatcher, button press-lighting) | **1,585** | `dc_rrect` 228, `dc_oncard` 176, `dc_geos` 138, `dc_oncardup` 126, `dc_litinv` 101, `dc_ctext` 88, `dc_hit` 78 |
| **Sticky App core** (`os88stik.inc` v1: load, save, register, close, the beat, the walk to `SYSTEM\APPDATA`, the damaged-file question) | **1,360** | `stik_load` 210, `stik_save` 174, `stik_made` 148, `stik_beat` 107, `stik_close` 102, `stik_register` 92 |
| **Alert + button kit** (`OS88UI_ALERT`) | **1,238** | `os88ui_btn` 248, `os88ui_ask` 140, `os88ui_arect` 79, `os88ui_btninit` 72, `os88ui_abtn1` 69 |
| **Band composer** (`os88gfx.inc`) | **1,228** | `gfxe_glyph` 442 (scaled and bold text, plus four straight-path copies for speed), `gfxe_bfill` 162, `gfxe_expand` 97 |
| **App shell** (entry, key dispatch, menu greying, the tick, About strings, header) | **1,218** | `dc_onkey` 233, `dc_entry` 185, `dc_tick` 142, `dc_menulive` 135 |
| **Agenda** (card 2's appointment list, headings, paging, `+N more`) | **1,108** | `dc_hday` 123, `dc_caline` 119, `dc_casec` 106, `dc_chead` 101, `dc_hagenda` 92 |
| **Line editor** (`os88line.inc`) | **986** | `os88line_draw` 204, `os88line_edit` 160, `os88line_caroff` 83 |
| **Tasks** (list, tick boxes x3 sizes, toggle, Clear Done) | **978** | `dc_htask` 131, `dc_ctline` 130, `dc_ctsec` 89, `dc_rtbox` 73, `dc_tclear` 71 |
| **Date maths** (`dcdate.inc`) | **508** | `dc_build` 169, `dc_today` 118, `dc_dow_of` 73 |
| **Selection + COMPACT's pages** | **407** | `dc_selset` 123, `dc_selrows` 113, `dc_pswap` 39 |
| **About card** (`OS88UI_ABOUT`) | **183** | `os88ui_about_d` 175 |
| | **18,976** | |

## Cross-cutting

These cut across the feature rows above. They are not added to them:

- **Hit testing: 999 bytes.** That is `dc_hit` 78, `dc_hbtn` 61,
  `dc_hmon` 303, `dc_hday` 123, `dc_hagenda` 92, `dc_htask` 131,
  `dc_hrem` 99 and `stik_hit` 112. Each card's layout is worked out three
  times by hand: once to draw it, once to map a press back to an element,
  and once more for the button rect that lights. `dc_hmon` alone is nearly
  as big as `dc_cmon`, which draws the card.
- **Partial redraws: ~750 bytes.** That is `dc_rrect`, `dc_rbox`,
  `dc_rtbox`, `dc_selrows`, `dc_rday`/`dc_rmon`/`dc_rhdr`/`dc_regrid`,
  `dc_c3sync` and `dc_vis`. This is SPEC.md 110.6's review fixes for the
  select and paint costs, and PERFORMANCE.md rule 1, so it buys a
  documented number.
- **Button press-lighting: ~300 bytes.** That is `dc_litinv` 101,
  `dc_lpdone` 58 and the lighting halves of `dc_oncardup` and `stik_mup`.
- **The two yes/no questions cost 1,238 bytes.** The alert and its button
  kit are included for exactly two `os88ui_ask` calls: *Delete this
  appointment/task?* (`dc_askdel`) and *Replace damaged calendar?*
  (`stik_dans`).
- **Three layouts.** TALL (VGA), MID (Hercules/EGA) and COMPACT (CGA) are
  table-driven. Their tables are only **174 bytes** (`dc_lays` 36,
  `dc_geos` 138). The cost is elsewhere: in COMPACT-only branches inside
  the draw and hit routines (`cmp byte [stik_li], 2` appears throughout),
  in the page switcher, and in three sizes of every bitmap (bell 16/12/8,
  tick box 11/9/8: 202 bytes).
- **Menus: ~800 bytes.** Two menus, 24 strings and pointer tables
  (595 bytes of data), plus `dc_menulive` 135 and `dc_oncmd` 72. Every
  greyable item carries a second string saying *why* it is grey
  (SPEC.md 47).

## The bss, 3,709 bytes

| | bytes |
|---|---:|
| **the payload held in memory** - 48 appointments x 32 + 32 tasks x 28 + 8 (`DCP_LEN`), behind the state file's 16-byte header | **2,456** |
| the composer's band, 176 x 32 at 1bpp (`dc_band`) | 704 |
| the calendar's own state and scratch (entry buffer 32, insert record 32, toast line 42, string builder 32, field block 20, ~150 of bytes and words) | 320 |
| `os88stik.inc`'s state: the desktop row 128 (`OSAPI_DI_SIZE`), the FIND buffer 24, the 8.3 name 13, the widget's hover/focus/paint words ~64 | 229 |

**Two thirds of the bss is the capacity of the data file**, which is sized
by `DCP_MAXAPPT` = 48 and `DCP_MAXTASK` = 32 and is a format constant
(SPEC.md 110.3, `tools/os88stik.py` mirrors it). It is not code.

## How it grew

SPEC.md 110.6 already carries the history; it is collected here.

| | image | on disk |
|---|---:|---:|
| v1 wave 2 | 6,080 | 5,343 |
| v1 wave 3 (`os88line.inc`, the alert, the lists, the field, the parser) | 12,368 | 10,544 |
| v1 polish | 13,168 | 11,211 |
| v2 wave 4, the widget | 18,336 | 15,630 |
| **today** | **18,976** | **16,171** |

So the widget (v2) added **~5.8 KB**: the `STIK_WIDGET` half of the
include (2,404), the band composer (1,228), the drawing core that composes
a card into the band instead of calling `font_run`, and three layouts.
SPEC.md 110.6 records that v2 overran its design (~14.0-14.5 KB planned)
by about 4 KB. It also records that no owner decision was asked for it.

## Re-deriving it

```sh
nasm -f bin -w+error -I apps/ -I apps/deskcal/ \
     --before "[map all dc.map]" -l dc.lst -o dc.bin apps/deskcal/deskcal.asm
cmp dc.bin build/deskcal.bin           # must be the shipped image
```

The `.text` symbol table in `dc.map` gives every label's address. A global
label's size is the distance to the next global label, so local `.x`
labels stay inside their routine. The listing's `db`/`dw`/`times` lines
give the data bytes. The bss figures are the `equ` offsets in
`deskcal.asm` (`DC_BSS` = 1,024) and `os88stik.inc` (`STIK_BSS_END`).
