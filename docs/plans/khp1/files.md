# Kernel hot-paths pass 1: files (Disk window, icons, desk, assoc, file dialog)

The **files** agent's record (BRIEF.md section 6). Owned files:
`kernel/files.inc`, `kernel/icons.inc`, `kernel/desk.inc`, `kernel/desksc.inc`,
`kernel/assoc.inc`, `kernel/dskwin.inc`, `kernel/fdlg.inc`. One shared include
outside every row was touched by none of the items: `apps/os88ui.inc` (its
scroll bar is priced in CROSS-FILE).

Instruments: MartyPC guest cycles (`tools/os88hot.py` time / profile / cycles
/ ab, base `6d6c23ae`), and four scratch tools written for this note - a
deterministic stepping sampler (advance 3,000 cycles, read CS:IP, resolve
against the kernel map with the `.cold` slide), a logged-breakpoint call
counter (`bp_trace(log=True)`, no stop a hit), an `int 13h` tracer that walks
the stack for the caller chain, and a pixel A/B (the raw framebuffer after
each step of a script, base tree against tip tree). Disk is priced in
`int 13h` CALLS (BRIEF section 8: this MartyPC models the platter, so its
disk ms are near the 5150's but move with rotational phase).

## 1. BASE AND TIP

Assembled by `make`'s `kernsize` lines. Resident = `.text` + `.bss` +
`.cold` + `.vga` + `.lowbss` + `.vgabuf`.

| kernel | base resident | tip resident | delta | base KERN_SIZE | tip KERN_SIZE |
|---|---:|---:|---:|---:|---:|
| kern_big | 91,530 | 91,624 | +94 (+92 mine, +2 the coordinator's `mod_cfix` merge) | 96,768 | **97,280** (one rung, at the [+80]) |
| kern_small | 57,258 | 57,350 | +92 | 59,392 | 59,392 |
| kern_emu | 91,784 | 91,878 | +94 | 97,280 | 97,280 |
| kern_mega | `.text` 45,853, `.cold` 46,395 | 45,933, 46,407 | +80 `.text`, +12 `.cold` (above 1MB) | conventional resident 62,976 | 62,976 |

Resident growth BEFORE `.cold` (BRIEF section 8's COLD_HOME limit): **80
bytes** of `.text` (the [+80]); the other 12 are in `.cold` itself.

## 2. WHERE THE TIME GOES

All on MartyPC, kern_big, `os8088-360.img` + `apps360.img`, a 320x200 Disk
window. Stepping sampler (exact guest time, 3,000-cycle resolution), base.

### Open B: from its desktop zone (double-click to listing), 5150/CGA: 1,495 ms

| share | ms | what |
|---|---:|---|
| BIOS / floppy | 855-907 | **4 int 13h**: boot sector (1), FAT window S2..S9 (8), the root's first four sectors AGAIN (4), ASSOC.DAT (6) |
| zoom outline (`wm_anim`) | ~200 | wm's (SPEC.md 11.99) |
| Disk-window content paint | 110 | `fm_paint_x`: header + buttons 26.5, four rows + bar 62, status 21 |
| frame + title | 72 | wm's |
| desktop cell, select then deselect | 2 x 31 | `desk_zones_paint` (desk.inc): each cell is 4 `thm_desk` ground fills (~45k cycles), the column `gfx_blit1`, the caption blits |
| ASSOC.DAT harvest (CPU) | 84 | `asc_use` / `asc_seed` / `asc_merge_ext`: once per volume SWITCH |
| scheduler | ~150 | `sch_switch` while waiting on the FDC |

The non-disk third is the CPU budget (BRIEF 8: 1,701/1,699/1,640 ms on
CGA/Herc/VGA, BIOS 64%).

### Navigation, CGA

| operation | ms | int 13h | notes |
|---|---:|---:|---|
| open a folder (`APPS`, 14 entries) | 1,120 | 3 | boot sector, FAT window, the folder (5 sectors). A remount per navigation (SPEC.md 19.2) |
| Up One Folder (Backspace), motor stopped | 1,336 / 1,122 | **4** | `fmv_sync_x`'s quiet stand reads the boot sector, then `fm_go`'s mount reads it again |
| Refresh | 850 | 3 | |
| launch a document (`GUIDE.TEX`) | | 7 | assoc's two quiet mounts (APPS for TeXPad, MEDIA back) each re-read the boot sector once the motor had stopped (2.5 s of package start between them): SPEC.md 18.9.1's motor test, working as designed |

### Repaints, CGA (logged-breakpoint call counts)

| operation | ms | drawing calls | the parts |
|---|---:|---:|---|
| PgDn (a windowful: `fm_scroll_by`'s middle tier) | 131 | 64 | 5 rows x ~75k cycles (icon ~28k = 37%, name run ~14k, size run, staging ~5k); the WHOLE scroll bar again (16 calls, 134k cycles = 28 ms) |
| view toggle (list <-> icons) | ~205 | 94 | `fm_cfill` (one fill, ~33 ms), header, buttons, 6 grid cells, the whole bar (27 ms), status, then `wm_grow_paint` (14 fills, 49k cycles = 10 ms) |
| scroll one row (blit tier) | 58 | 16 | already SPEC.md 22.11's |
| select a row | ~10 | 2 | XOR bands, already SPEC.md 22.2's |

**The 16x16 icon on a 1bpp card was the single largest piece of a row**:
`ico_pass_bb` ~13,000 cycles a pass, two passes an icon, every byte reached
through `call ico_bbop_byte`, which re-tested the op byte each time. The
baseline report (BRIEF 8) puts `ico_pass_bb`/`ico_pass` at 14-18% of a PgDn
on all three adapters. A session's icon draws on CGA were at shift 0 in 33 of
34 cases (Disk-window rows and grid, the dock).

## 3. TAKEN

| id | agent | routine / path | hotness | change | kern_big B | kern_small B | cycles base -> tip (input) | user-visible (operation, adapter: ms base -> tip) | M/P | 286 | commit | FREE / [+N] |
|---|---|---|---|---|---:|---:|---|---|---|---|---|---|
| files-1 | files | `fm_c_up` (files.inc) | H4 | sync only a window the globals do not describe; the quiet stand's boot-sector read was always followed by `fm_go`'s own | +5 `.cold` | +5 `.cold` | int 13h 4 -> 3 per Up (motor stopped) | Up One Folder, CGA: 1,336 / 1,122 -> 1,048 / 912 ms (two folders, ~-250 ms, one isolated sector) | M | same call saved | `048c7097` | **[+5] OWNER'S PICK** |
| files-2 | files | `fm_scroll_by` middle tier, `fm_draw_rows` split (`fm_draw_rcore`) | H3 | a windowful scroll fills left of the bar and moves the THUMB through `fm_scrollpaint`'s tail (`os88ui_sbmove`, 3 calls) instead of `os88ui_sbar` (16) | +7 `.cold` | +7 `.cold` | 64 -> ~51 drawing calls (PgDn, 12-entry folder) | PgDn CGA 131.2 -> 113.6 ms, PgUp 124.2 -> 105.4 (os88hot ab -n 3). Herc/VGA list PgDn take the blit tier there: 0; their icon-view scrolls take this tier. **CHANGES TRANSIENTS** (fewer): CGA PgDn 9 -> 7 frames of visible redraw, 8 -> 6 flashing, worst 1,487 -> 927 px | M | CPU-only, same | `e02c1b7e` | **[+7] OWNER'S PICK** |
| files-3 | files | `ico_pass_bb` (icons.inc) | H1 inside H2 | at shift 0 with the icon wholly inside the stride, one word or / and-not a row word, the op picked once a pass; the byte walk kept for shifted and edge icons | +80 `.text` | +80 `.text` | one 16x16 pass ~13,000 -> ~5,000 cycles (~2.9 ms an icon, CGA) | PgDn CGA 113.6 -> 99.0 ms (131.2 at base, both items -24.6%), PgUp 105.4 -> 91.9 (-26.0%); Herc PgDn 131.9 -> 118.0 (-10.5%), PgUp 124.0 -> 109.3 (-11.8%); open A: CGA `ico_pass_bb` 24.7 -> 15.5 ms a rep. VGA/EGA 0 (planar pass) | M | PREDICTED a win (fewer instructions; a 16-bit bus takes the word op whole when DI is even, which an aligned 16px icon's is not always) | `f183b808` | **[+80] OWNER'S PICK**. Crosses kern_big's rung: KERN_SIZE 96,768 -> 97,280 |
| files-4 | files | `assoc_scan` (assoc.inc) | H4 (listing, harvest) | ES = DS and `cld` once in front of the walk, not per row | 0 | 0 (no OS88_ASSOC) | median 1,959 -> 1,530 cycles a call (os88hot cycles, opendisk, CGA) | first open of B:, ~31 calls: ~-2.8 ms | M | same | `31c9775e` | **FREE** |

Every item's framebuffer was compared with the base's after the same script
(boot, open B:, APPS, PgDn, PgDn, PgUp, select, PgDn, PgUp, icon view, PgDn,
PgUp, a Calc launch): **identical on CGA, Hercules and VGA** (raw B8000 /
B0000 bytes, VGA's rendered `fbuf`).

**opendisk (A:) moved by rotational phase, not by work.** os88hot ab -n 3,
same 4 calls and 20 sectors: CGA 1,640 -> 1,748 ms median (min 1,630 ->
1,684), Herc 1,782 -> 1,810, VGA 1,686 -> 1,670. The profile A/B says where:
BIOS 1,045 -> 1,106 ms a rep (+61, the platter), everything else -12 ms
(`ico_pass_bb` -9.2). The next merge by anyone moves that phase again; price
the disk in calls.

## 4. TRIED AND REFUSED

- **`assoc_stage` as one `rep movsb` with DS/ES swapped** (+1 byte `.cold`,
  ~180 cycles a call, ~400 calls a volume's harvest: ~15 ms a volume switch
  on any adapter). Refused: the +1 in `.cold` moves `dsk_xfer`,
  `dsk_runcap` and `dsk_next_clus_x` off their pinned parity (hotpar), and
  `t_asmrules` reads the swap idiom as "restores in the wrong order". +2 with
  a pad, for 15 ms on an operation that happens when the user changes drive.
- **Skipping `wm_grow_paint` after `fm_repaint`** (10 ms a repaint on the
  front window, CGA: 14 fill calls, 49k cycles). The content fill would have
  to stop short of the 13x13 grow rect (one more fill) and the paint would
  have to prove nothing of its own lands in that rect - and the status line
  CAN reach it in a narrow window (it is truncated at (cw-12)/8 cells from
  x 8, the box starts at cw-13), a two-line prompt changes the rows, and
  WIN95.DRV's grip (`THV_GRIP`) has its own rect. Not taken: the predicate is
  three facts in three modules for 5% of a view toggle.
- **The arrow glyphs of `os88ui_sbar` as one icon each** instead of five
  `UI_HLINE`s (would save ~8 calls, ~6 ms a whole-bar draw on CGA). Refused:
  `icon_draw` refuses a whole shape against an armed clip region where an
  hline clips per pixel, so a partly covered bar would draw differently; and
  the file is the SDK every package assembles.
- **Moving the paint routines from `.cold` to `.text`** (the `ct_cw_*` hop
  is ~120 cycles a primitive call). A list row makes 3 primitive calls of
  ~60k cycles: 0.6%. Not worth the near-window room.
- **A desktop drive click** (2 cell repaints, 31 ms each on CGA) was looked
  at: the select and the deselect-on-open are both needed for the settled
  picture. The cell's four ground fills (~9 ms of each) could only go by
  widening SPEC.md 26.9.9's band to the whole cell, which is more `.bss`
  and a rebuild of a section its owner rebuilt for bytes. Left.

## 5. WANTS AN ABI BREAK

None found. No directory, `ASSOC.DAT` or `SYSTEM.CFG` format forces an extra
read on a path measured here; every extra read found is a policy inside the
kernel (section 7).

## 6. WANTS THE OWNER

- **A per-volume ASSOC.DAT stamp.** `asc_vol` names ONE volume, so every
  volume switch (an A: window and a B: window used alternately) re-reads the
  new volume's ASSOC.DAT (**1 int 13h**, 6 sectors on apps360) and re-runs
  the harvest's CPU (**~84 ms on any adapter**). The icon store already
  "only grows" and is machine-wide, so a stamp BITMASK (one byte for
  DVOL_MAX <= 8, gated on `[ico_n]` != 0 as the single stamp is) would make a
  return to a volume free. It CHANGES BEHAVIOUR: `asc_seed` re-points
  `assoc_clus` / `assoc_drv` (where a program lives) at the volume it reads,
  so with a program on both disks the base re-seeds to the disk you just
  came back to and the bitmask would keep the other one's hint - a document
  would open its program from the other drive. About +10 bytes `.cold`.

## 7. CROSS-FILE

- **disk.inc (disk agent): a root mount reads the root's first four
  sectors twice.** Open B: and Up-to-root both show `int 13h` S2 x 8 from
  `dsk_fat_window` (FAT 1, FAT 2 AND root sectors 1-4, through
  `dsk_rah_fill`) and then S6 x 4 from `disk_mount_x.rootsec` ->
  `dsk_dirw_get_x` - the same four sectors, one call (~200-380 ms measured
  between them on CGA). Every open of a root, Refresh at a root, Up to a
  root: **one int 13h**.
- **disk.inc: every navigation re-reads the FAT window** (8 sectors, 1 call)
  after the boot sector, even when the boot sector's signature matches the
  mounted volume. Folder open = 3 calls where 2 would do when the volume is
  unchanged (SPEC.md 19.2's remount is the reason; the FAT window is valid
  when `dsk_sigcur` matches). ~380 ms a navigation on a 360KB disk.
- **disk.inc `disk_mount` reads the boot sector even with the motor still
  running** (`dsk_media_ok` says it cannot have changed). Measured: Backspace
  inside 2 s of the previous navigation still cost 3 calls.
- **disk.inc `dsk_ico_stage`: the built-in folder body is decoded from its
  runs for every folder row** (~3,560 cycles = 0.75 ms a folder icon, every
  adapter); then `fm_draw_icon16` ORs all 32 words to test for "no icon"
  (~1,600 cycles). A ZF out of `dsk_ico_stage` (it knows ICO_R_NONE and a
  failed `ico_body`) would retire the OR loop.
- **apps/os88ui.inc `os88ui_sbar`**: ten `gfx_hline`s for the two arrow
  glyphs, each a whole `gfx_fill` underneath on 1bpp (~3k cycles each, ~6 ms
  a bar on CGA), on every full Disk-window repaint, every file dialog and
  every package bar. A one-call glyph needs a per-pixel-clipping primitive.
- **wm.inc `wm_grow_paint`**: 2 fills + 3 `gfx_frame`s = 14 fill calls,
  ~49k cycles (10 ms CGA) for a 13x13 box, after every self-initiated repaint
  of a front sizable window. Composed into one `gfx_blit1` it would be one.
- **softgfx.inc / gfxcall**: `gfx_frame` is four `gfx_fill`s (~13k cycles on
  CGA for an 11x11 box); `gfx_hline` is a `gfx_fill` of one row. `fm_cfill`
  (one content fill, ~180 rows) costs ~33 ms on CGA almost entirely in the
  per-row setup (PERFORMANCE.md Part 2: 177 us a row).
- **wm.inc `wm_anim`**: the zoom outline is ~200 ms of a 1.5 s open on CGA.

## 8. ROWS RUN

Once, after the last code commit, `python3 tools/os88test.py soak -k ...
--marty-jobs 1`: **12 passed, 0 failed** - `assocopen`, `assocfind`,
`assocvol`, `icoclip`, `deskfdd`, `fmarrows`, `msel`, `fdlgchoose`,
`fmthumb`, `fmbtn`, `sbar`, `sbardlg` (318.5 s). `make -j2` (fast tier, with
`hotpar`), `make -j2 small` (in `all`), `make emu` and `make mega` pass at
the tip.

Rows covering the items: files-1 `msel` (Backspace / Up), `fmarrows`;
files-2 `fmthumb`, `fmarrows`, `sbar`, `sbardlg`, `fdlgchoose` (the chooser
scrolls through the same tier); files-3 `icoclip`, `deskfdd`, and every
1bpp boot; files-4 `assocopen`, `assocfind`, `assocvol`.

## 9. DEFECTS FOUND

None in the shipped behaviour. One instrument note: `os88hot cycles
icon_draw16` reports "no call" because `icon_draw16` tail-jumps into
`icon_draw.hdr` and leaves through `kret_es`; logged breakpoints on its
entry and the next routine's are the way to price it.
