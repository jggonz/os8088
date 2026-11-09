# Kernel hot paths, the baseline: where a user's time goes on the XT (2026-11-06)

A MEASUREMENT of one tree, and the map that kernel hot-paths pass 1
(docs/plans/khp1/BRIEF.md) ranks its work by. The base is `a7ee7fab`, the
commit the pass was cut from; it was measured at `kernel-hotpaths`
`7ca0f5f8`, whose kernel is the base's apart from the build number's three
bytes (SPEC.md 14.2). Every number came off `tools/os88hot.py` and can be
taken again with it:

```sh
python3 tools/os88hot.py time    <scenario> --machine cga|herc|vga -n 3
python3 tools/os88hot.py profile <scenario> --machine cga|herc|vga -n 2 --top 40
python3 tools/os88hot.py gfxbench|sysbench --machine cga|herc|vga --json f.json
python3 tools/os88hot.py time <scenario> --emu 86box --cpu 286|386 --machine ...
python3 tools/os88hot.py ab --base a7ee7fab time <scenario> --all   # the A/B
```

`HOTPATH-BASE-2026-11-06.json` beside this file holds every result
(`time-<scenario>`, `prof-<scenario>`, `gfxbench`, `sysbench`, and
`box-time-<scenario>` for 86Box), keyed by adapter the way each subcommand
writes it, so a tip can be compared against it directly:

```sh
python3 tools/os88hot.py time raise --all --json tip.json
python3 tools/os88hot.py diff docs/reports/HOTPATH-BASE-2026-11-06.json tip.json --key time-raise
```

## How to read the numbers

* **Machines**: MartyPC `os8088_5150_cga_gla`, `os8088_5150_herc_gla` and
  `os8088_xt_vga`, an 8088 at 4.772727 MHz, cycle-exact on the CPU, the bus
  and the card. The disks are `os8088-360.img` in A: and `apps360.img` in B:.
  This tree's MartyPC also models the floppy's platter
  (docs/MARTYPC-DEBUG.md, *The disk has a platter*; within one measurement
  quantum of the 5150 on the raw int 13h rows), so the disk-bound
  scenarios are close to real time. Motor spin-up is the one thing it does
  not model: it is instant here.
* **ms** is the operation's latency: the first injected input to the end of
  the last drawing it caused (the last `gfx_unlock` before the UI went idle,
  or the scenario's own end label). It includes the mouse packets' flight
  at 1200 baud, about 25 ms a packet, and any disk time.
* **busy** is the part of that window when the CPU was NOT halted in the idle
  task. It is the work, and it is what a speedup reduces. busy close to
  ms means the CPU was the bottleneck, and a gap means waiting: for the
  serial line, for the keyboard (`type` sends a key every 90 ms), or for
  the disk.
* **int 13h** is the FDC's count of read commands in the window. On the 5150
  each costs about 400 ms (PERFORMANCE.md Part 2), and fewer calls beat
  fewer cycles.
* A **profile** samples CS:IP once per MartyPC batch, every ~1,193 cycles
  (0.25 ms) of guest time. The sampling is deterministic and costs the
  guest nothing. Samples inside a logged halt are left out, so **every
  share below is a share of BUSY time**. Samples are resolved by LINEAR
  address against every code section where it runs, with the mono `.cold`
  slide applied, and a local label rolls up into its routine. `BIOS` is
  ROM code: on these scenarios it is almost all the floppy BIOS waiting on
  the drive. `pkg <title>` is a package's own code.
* Every window starts at IRQ0's entry, so reps agree to about 1%. Medians of
  3 reps are used for `time`, and 2 reps are summed for `profile`. The
  first raise after the other drive's window acted re-reads that disk, and
  that is a warm-up rep that is not counted. `launch` reports the cold load
  (rep 1, read off the floppy) apart from the cached relaunch (no int 13h).

## 1. The headline: each scenario on each adapter (MartyPC, MEASURED)

| scenario / phase | CGA ms | CGA busy | Herc ms | Herc busy | VGA ms | VGA busy | int 13h calls (C/H/V) |
|---|---:|---:|---:|---:|---:|---:|---|
| desktop: boot | 14559.1 | 14488.2 | 14662.0 | 14569.5 | 15976.8 | 15951.8 | 18/18/18 |
| opendisk: open A: | 1701.1 | 1686.9 | 1698.7 | 1682.9 | 1639.8 | 1627.6 | 4/4/4 |
| raise: raise Disk | 179.8 | 170.3 | 230.1 | 221.9 | 366.1 | 357.8 | 0/0/0 |
| drag: drag | 512.4 | 502.5 | 538.2 | 528.6 | 611.0 | 599.7 | 0/0/0 |
| menu: open File | 47.4 | 36.5 | 44.1 | 35.5 | 50.6 | 39.8 | 0/0/0 |
| menu: close File | 22.4 | 22.4 | 24.2 | 24.2 | 28.7 | 28.7 | 0/0/0 |
| pointer: sweep | 632.0 | 235.4 | 631.1 | 235.0 | 632.2 | 245.2 | 0/0/0 |
| type: type 31 keys | 2942.0 | 617.6 | 2945.9 | 560.2 | 2941.5 | 626.6 | 0/0/0 |
| launch: launch Calc cold | 1414.2 | 1403.2 | 1471.1 | 1462.2 | 1419.3 | 1407.3 | 1/1/1 |
| launch: launch Calc cached | 969.8 | 961.1 | 977.7 | 967.6 | 977.6 | 969.2 | 0/0/0 |
| scroll: PgDn | 131.2 | 131.2 | 131.9 | 131.9 | 104.1 | 104.1 | 0/0/0 |
| scroll: PgUp | 124.2 | 124.2 | 124.0 | 124.0 | 96.8 | 96.8 | 0/0/0 |
| idle: idle 5s | 5009.0 | 326.0 | 5009.0 | 326.0 | 5009.0 | 326.0 | 0/0/0 |

The scenarios (`python3 tools/os88hot.py scenarios`):

| scenario | what it does |
|---|---|
| desktop | power-on to the first idle desktop, stepped from cycle 0 (busy here is unhalted time, not counted against a halt) |
| opendisk | double-click drive A:'s zone to its Disk window, desktop clear |
| raise | click the exposed title bar of a background Disk window (A: and B: open, one over the other) |
| drag | press on the front Disk window's title, 8 packets of (+8,+3), release: the move's commit included |
| menu | press the desktop's File title (open, ends at `menu_drop.poll`, the pull-down drawn), release on the bar (close) |
| pointer | 24 packets of (+20,+5) across a clear desktop: the cost of motion itself |
| type | 31 keys into Note Pad (A:\APPS\NOTEPAD.O88), one every 90 ms |
| launch | double-click B:\APPS\CALC.O88 in its Disk window to Calc's first frame |
| scroll | PgDn, then PgUp, in a Disk window on B:\APPS (12 entries) |
| idle | 5 guest seconds of the desktop with a Disk window open |

**What the table says, in user terms.** On a 4.77 MHz XT a window raise is
0.18 s on CGA, 0.23 s on Hercules and 0.37 s on VGA. A drag of the
smallest window is half a second. Opening a Disk window is 1.7 s, and
launching Calculator is 1.4 s cold and 0.97 s cached. A keystroke into Note
Pad costs about 20 ms of CPU, of which about 6 ms is the idle machine's
own tick work, so the keystroke itself is about 14 ms. At rest the machine
is 6.5% busy, the same on all three adapters. In every scenario except
`pointer`, `type` and `idle` the CPU is the bottleneck: busy is 75-100% of
ms, and the gap in `menu open` is the press packet's flight. `raise`, `drag` and `menu`
are slowest on VGA, which pays for a save-under, while `scroll` and
`opendisk` are slowest on the 1bpp cards. `raise` alternates between its
two windows, A: raised over B: and B: over A:, and on VGA those are 335
and 366 ms. Its median is over that pair, so compare it at the same `-n`.

## 2. Where the time goes, over the six interactive operations

The busy time of `raise`, `drag`, `menu` (both phases), `pointer`, `type` and
`scroll`, summed per routine, ms per rep of each scenario. The disk-bound
scenarios are left out because their `BIOS` share is drive time, and the
kernel's share there is listed in section 3.

| # | CGA | ms | % | Herc | ms | % | VGA | ms | % |
|---:|---|---:|---:|---|---:|---:|---|---:|---:|
| 1 | `sch_switch` | 313.9 | 17.5 | `sch_switch` | 310.3 | 17.3 | `vga_restore_vram_v` | 348.1 | 16.7 |
| 2 | `sw_col` | 216.9 | 12.1 | `sw_col` | 220.4 | 12.3 | `vga_save_vram_v` | 318.0 | 15.2 |
| 3 | `sw_xfer` | 129.1 | 7.2 | `sw_xfer` | 167.2 | 9.3 | `sch_switch` | 313.2 | 15.0 |
| 4 | `font_run_x` | 92.2 | 5.1 | `font_run_x` | 97.2 | 5.4 | `vga_solid_rect_v` | 165.8 | 7.9 |
| 5 | `pkg Note Pad` | 90.0 | 5.0 | `sw_plane_op` | 81.9 | 4.6 | `font_run_x` | 87.7 | 4.2 |
| 6 | `sw_plane_op` | 85.1 | 4.7 | `cur_move_mono` | 65.6 | 3.7 | `pkg Note Pad` | 58.5 | 2.8 |
| 7 | `cur_move_mono` | 64.1 | 3.6 | `pkg Note Pad` | 59.5 | 3.3 | `cur_draw` | 56.3 | 2.7 |
| 8 | `sch_account` | 48.0 | 2.7 | `ui_task` | 46.7 | 2.6 | `sch_account` | 54.2 | 2.6 |
| 9 | `ui_task` | 43.9 | 2.5 | `sch_account` | 45.0 | 2.5 | `gfx_rect_setup` | 52.5 | 2.5 |
| 10 | `cur_put_mono` | 37.7 | 2.1 | `cur_put_mono` | 37.5 | 2.1 | `ui_task` | 44.4 | 2.1 |
| 11 | `ico_pass_bb` | 36.3 | 2.0 | `sch_isr` | 34.8 | 1.9 | `ico_pass` | 38.3 | 1.8 |
| 12 | `task_yield` | 34.9 | 1.9 | `task_yield` | 32.6 | 1.8 | `sch_isr` | 36.5 | 1.7 |
| 13 | `sch_isr` | 33.3 | 1.9 | `BIOS` | 28.5 | 1.6 | `BIOS` | 36.2 | 1.7 |
| 14 | `BIOS` | 32.1 | 1.8 | `cur_get_mono` | 27.5 | 1.5 | `task_yield` | 31.6 | 1.5 |
| 15 | `gfx_rect_setup` | 32.0 | 1.8 | `ico_pass_bb` | 26.4 | 1.5 | `mou_eoi` | 20.1 | 1.0 |
| 16 | `cur_get_mono` | 27.7 | 1.5 | `vgas_bankcopy` | 24.7 | 1.4 | `gfx_rowbase` | 19.0 | 0.9 |
| 17 | `sw_rect` | 26.7 | 1.5 | `sch_resume` | 24.4 | 1.4 | `sch_resume` | 19.0 | 0.9 |
| 18 | `sch_resume` | 25.4 | 1.4 | `gfx_rect_setup` | 24.2 | 1.4 | `gfx_fill_gray_raw_v` | 18.6 | 0.9 |
| 19 | `mou_eoi` | 19.7 | 1.1 | `mou_eoi` | 19.9 | 1.1 | `gfx_scroll_v` | 17.7 | 0.8 |
| 20 | `mou_isr` | 16.0 | 0.9 | `sw_rect` | 17.5 | 1.0 | `mou_isr` | 16.1 | 0.8 |
| 21 | `cur_geom` | 15.3 | 0.9 | `ico_bbop_byte` | 17.5 | 1.0 | `vga_gc_reset` | 14.4 | 0.7 |
| 22 | `gfx_rowbase` | 13.5 | 0.8 | `mou_isr` | 16.0 | 0.9 | `cur_geom` | 12.6 | 0.6 |
| 23 | `cur_mvrow` | 12.4 | 0.7 | `cur_geom` | 15.2 | 0.9 | `clk_tick` | 11.4 | 0.5 |
| 24 | `ico_bbop_byte` | 11.9 | 0.7 | `clk_tick` | 13.6 | 0.8 | `cur_vis` | 11.3 | 0.5 |
| 25 | `ui_bill` | 11.6 | 0.6 | `cur_mvrow` | 12.4 | 0.7 | `mou_apply` | 9.8 | 0.5 |

**The ranking, and who owns it** (BRIEF.md section 2):

1. **`sch_switch`, sched: 17% of interactive busy time on every adapter,
   and 45% of an idle machine's work.** `profile drag --local` places it in
   the task-table scan (`.nowrap`, `.scan`, `.skip`: 21% of a CGA drag
   between them). The scan runs once for every switch, and the switches
   come from two places. The first is the idle task's yield after each
   interrupt it takes (SPEC.md 8.1.2.2): a mouse packet is three of them,
   and a key is two. The second is the yield loops in `ui_track` (a drag)
   and `menu_drop.poll` (a menu held down). **In those two loops the
   switches are the wait for the next mouse packet, not drawing**: on the
   86Box 286, where drawing is fast, `sch_switch` is 71% of a drag's
   samples. So a faster scan makes pointer motion, typing and the idle
   machine cheaper, which leaves more of the CPU free between packets. It
   does not shorten a drag, whose length is set by the packets
   (`drag` ms barely moves between CGA, Hercules and VGA). That is H2:
   per interrupt.
2. **The 1bpp backbuffer, mono: `sw_col`, `sw_xfer`, `sw_plane_op` and
   `sw_rect`.** These are 25% of CGA's and 28% of Hercules's interactive
   busy time. `sw_xfer` alone is 35% of a raise and `sw_col` 26% of a drag,
   and the row loops are the hot spots (`.xrow`, `.row`, `.xnw`, `.srow`,
   `.rrow`). That is H1, and the largest drawing block a 1983 machine
   spends.
3. **VGA save-under and restore, vga: `vga_save_vram_v` and
   `vga_restore_vram_v`.** These are 32% of VGA's interactive busy time,
   70% of a VGA raise (120 + 118 ms of 341) and 43% of a drag. They are why
   VGA is the slowest adapter on `raise` (366 ms against 180 on CGA).
   `vga_solid_rect_v` is the next VGA routine, at 8%.
4. **Text, text: `font_run_x` (with `fnt_rn_edge`).** These are 5% of
   interactive time, but 22% of opening a menu, 20-26% of a Disk window
   PgDn, and the largest single routine in both on every adapter. That is
   H2 per cell, and the cost of menus and lists.
5. **The pointer, pointer: `cur_move_mono`, `cur_put_mono`,
   `cur_get_mono` and `cur_mvrow`; on VGA, `cur_draw` with the save and
   restore.** These are 25% of the busy time of moving the pointer on
   CGA. A 24-packet sweep costs about 235 ms of CPU on every adapter, and
   37% of the window is busy. Take off the idle machine's 6.5% and a packet
   costs about 8 ms.
6. **Package load, disk: `lz_lzb`, `lz_match` and `lz_copy`.** These are
   18-23% of a cached Calc launch and 14-16% of a cold one, and the
   decoder is the biggest kernel routine on that path. A cold launch is 1
   int 13h call. A Disk window open is 4 calls, and `BIOS` (the drive) is
   64% of it.
7. **The Disk window, files: `ico_pass_bb`, `ico_bbop_byte`,
   `fm_draw_icon16` and `fm_stage_name`.** `ico_pass_bb` with
   `ico_bbop_byte` is 18% of a 1bpp PgDn, and `ico_pass` is 18% of a VGA
   PgDn.
8. **The fixed part of every drawing call, gfxcall: `gfx_rect_setup`,
   `gfx_rowbase` and `gfx_nextrow`.** `gfx_rect_setup` is 2-4% of every
   drawing scenario. gfxbench prices a call's fixed part at 539 us
   (`GFX_HLINE 8px`, CGA) against PERFORMANCE.md Part 2's 756, and the
   lock pair at 461 us.
9. **sched and clock, on the idle machine: `sch_account`, `ui_task`,
   `sch_isr`, `sch_resume` and `clk_tick`.** With `sch_switch`, these are
   the whole idle cost: 6.5% of the CPU, about 3.6 ms a tick, the same on
   all three adapters.

## 3. Each scenario's top 15 kernel routines, per adapter (MartyPC, MEASURED)

`%` is the share of the scenario's busy time. A name with no prefix is a
kernel routine. The halted share closes each table.

#### desktop: boot

| # | CGA | % | Herc | % | VGA | % |
|---:|---|---:|---|---:|---|---:|
| 1 | `BIOS` | 81.6 | `BIOS` | 80.9 | `BIOS` | 74.4 |
| 2 | `kz_expand` | 4.2 | `kz_expand` | 4.1 | `kz_expand` | 3.8 |
| 3 | `mouse_init` | 3.4 | `mouse_init` | 3.4 | `mouse_init` | 3.2 |
| 4 | `spl_prow` | 1.2 | `spl_prow` | 1.1 | `other C7B00` | 3.0 |
| 5 | `other 17E00` | 0.7 | `sw_plane_op` | 0.8 | `other C7A00` | 1.9 |
| 6 | `spl_fill` | 0.7 | `other 17E00` | 0.7 | `spl_prow` | 0.8 |
| 7 | `mem_bpara` | 0.6 | `spl_fill` | 0.7 | `other C4300` | 0.8 |
| 8 | `spl_nextrow` | 0.6 | `spl_nextrow` | 0.7 | `other C4200` | 0.7 |
| 9 | `spl_xform` | 0.5 | `vid_wipe` | 0.6 | `gfx_fill_gray_raw_v` | 0.7 |
| 10 | `spl_rowmk` | 0.5 | `mem_bpara` | 0.6 | `mem_bpara` | 0.6 |
| 11 | `sw_plane_op` | 0.5 | `spl_xform` | 0.6 | `spl_nextrow` | 0.5 |
| 12 | `spl_text` | 0.4 | `spl_rowmk` | 0.5 | `other C5E00` | 0.5 |
| 13 | `spl_span` | 0.4 | `spl_span` | 0.3 | `other C3C00` | 0.5 |
| 14 | `vid_wipe` | 0.3 | `sw_col` | 0.3 | `spl_xform` | 0.5 |
| 15 | `font_run_x` | 0.3 | `font_run_x` | 0.3 | `spl_fill` | 0.5 |

Shares are of BUSY time; halted share of the window, C/H/V: 0% / 0% / 0%.

#### opendisk: open A:

| # | CGA | % | Herc | % | VGA | % |
|---:|---|---:|---|---:|---|---:|
| 1 | `BIOS` | 64.1 | `BIOS` | 63.6 | `BIOS` | 65.9 |
| 2 | `sw_col` | 6.0 | `sw_col` | 6.5 | `vga_solid_rect_v` | 5.5 |
| 3 | `sch_switch` | 5.0 | `sch_switch` | 4.4 | `sch_switch` | 5.0 |
| 4 | `sw_plane_op` | 2.8 | `sw_plane_op` | 3.0 | `font_run_x` | 2.5 |
| 5 | `font_run_x` | 2.0 | `font_run_x` | 2.5 | `ico_pass` | 1.9 |
| 6 | `ico_pass_bb` | 1.5 | `ico_pass_bb` | 1.4 | `gfx_rect_setup` | 1.0 |
| 7 | `gfx_rect_setup` | 1.0 | `gfx_rect_setup` | 0.9 | `gfx_fill_gray_raw_v` | 0.9 |
| 8 | `dsk_copy_seg_x` | 0.8 | `dsk_copy_seg_x` | 0.8 | `dsk_copy_seg_x` | 0.8 |
| 9 | `dsk_free_clus_x` | 0.8 | `dsk_free_clus_x` | 0.8 | `dsk_free_clus_x` | 0.8 |
| 10 | `sch_isr` | 0.7 | `icon_draw_ix` | 0.7 | `icon_draw_ix` | 0.7 |
| 11 | `task_yield` | 0.7 | `sch_isr` | 0.7 | `gfx_blit1_x` | 0.7 |
| 12 | `sw_rect` | 0.6 | `ico_bbop_byte` | 0.7 | `sch_isr` | 0.7 |
| 13 | `gfx_blit1_x` | 0.6 | `gfx_blit1_x` | 0.6 | `task_yield` | 0.6 |
| 14 | `sch_account` | 0.5 | `sw_rect` | 0.6 | `sch_account` | 0.5 |
| 15 | `desk_cbit` | 0.5 | `task_yield` | 0.6 | `desk_cbit` | 0.5 |

Shares are of BUSY time; halted share of the window, C/H/V: 1% / 1% / 1%.

#### raise: raise Disk

| # | CGA | % | Herc | % | VGA | % |
|---:|---|---:|---|---:|---|---:|
| 1 | `sw_xfer` | 35.4 | `sw_xfer` | 37.9 | `vga_save_vram_v` | 35.1 |
| 2 | `sw_col` | 16.7 | `sw_col` | 20.2 | `vga_restore_vram_v` | 34.7 |
| 3 | `sw_plane_op` | 7.0 | `sw_plane_op` | 6.1 | `vga_solid_rect_v` | 9.0 |
| 4 | `gfx_rect_setup` | 4.3 | `gfx_rect_setup` | 4.0 | `gfx_rect_setup` | 2.4 |
| 5 | `sch_switch` | 2.5 | `sch_switch` | 2.3 | `sch_switch` | 1.6 |
| 6 | `font_char_bb` | 2.1 | `sw_rect` | 1.9 | `font_char` | 1.4 |
| 7 | `sw_rect` | 1.7 | `font_char_bb` | 1.8 | `vga_gc_reset` | 1.0 |
| 8 | `menu_bput` | 1.3 | `gfx_rowbase` | 1.2 | `sch_isr` | 0.8 |
| 9 | `gfx_rowbase` | 1.2 | `menu_bput` | 1.0 | `menu_bput` | 0.6 |
| 10 | `mem_claim_1` | 1.0 | `sch_isr` | 0.9 | `gfx_rowbase` | 0.6 |
| 11 | `sw_ink` | 1.0 | `mem_claim_1` | 0.9 | `sch_account` | 0.6 |
| 12 | `sch_isr` | 1.0 | `cur_put_mono` | 0.8 | `mem_claim_1` | 0.6 |
| 13 | `mem_next` | 0.9 | `sch_account` | 0.7 | `cur_draw` | 0.4 |
| 14 | `gfx_clip_query` | 0.8 | `gfx_clip_query` | 0.6 | `mou_eoi` | 0.4 |
| 15 | `font_char` | 0.8 | `mou_eoi` | 0.6 | `ui_task` | 0.3 |

Shares are of BUSY time; halted share of the window, C/H/V: 6% / 5% / 2%.

#### drag: drag

| # | CGA | % | Herc | % | VGA | % |
|---:|---|---:|---|---:|---|---:|
| 1 | `sw_col` | 26.2 | `sw_col` | 28.0 | `vga_restore_vram_v` | 22.1 |
| 2 | `sch_switch` | 23.8 | `sch_switch` | 21.3 | `vga_save_vram_v` | 20.6 |
| 3 | `sw_xfer` | 13.6 | `sw_xfer` | 17.1 | `sch_switch` | 17.9 |
| 4 | `sw_plane_op` | 8.4 | `sw_plane_op` | 7.6 | `vga_solid_rect_v` | 16.4 |
| 5 | `task_yield` | 3.9 | `task_yield` | 3.5 | `task_yield` | 2.8 |
| 6 | `gfx_rect_setup` | 3.2 | `gfx_rect_setup` | 2.3 | `gfx_rect_setup` | 2.6 |
| 7 | `cur_put_mono` | 1.4 | `sw_rect` | 1.2 | `gfx_fill_gray_raw_v` | 2.6 |
| 8 | `mou_eoi` | 1.2 | `cur_put_mono` | 1.2 | `cur_draw` | 0.9 |
| 9 | `sw_rect` | 1.1 | `mou_eoi` | 1.1 | `mou_eoi` | 0.9 |
| 10 | `sch_resume` | 1.0 | `sch_resume` | 0.8 | `sch_isr` | 0.8 |
| 11 | `cur_get_mono` | 0.9 | `mou_isr` | 0.8 | `sch_resume` | 0.8 |
| 12 | `sch_isr` | 0.8 | `sch_isr` | 0.7 | `mou_isr` | 0.6 |
| 13 | `mou_isr` | 0.7 | `cur_get_mono` | 0.7 | `sch_account` | 0.5 |
| 14 | `mou_apply` | 0.6 | `gfx_rowbase` | 0.6 | `vga_gc_reset` | 0.5 |
| 15 | `sch_account` | 0.6 | `mou_apply` | 0.5 | `gfx_rowbase` | 0.5 |

Shares are of BUSY time; halted share of the window, C/H/V: 2% / 2% / 2%.

#### menu: open File

| # | CGA | % | Herc | % | VGA | % |
|---:|---|---:|---|---:|---|---:|
| 1 | `font_run_x` | 21.9 | `font_run_x` | 21.3 | `vga_save_vram_v` | 26.4 |
| 2 | `sw_plane_op` | 13.5 | `sw_col` | 12.8 | `font_run_x` | 18.0 |
| 3 | `sw_col` | 12.0 | `sw_plane_op` | 11.4 | `vga_solid_rect_v` | 14.8 |
| 4 | `sw_xfer` | 11.3 | `sw_xfer` | 10.6 | `sch_switch` | 6.4 |
| 5 | `sch_switch` | 7.7 | `sch_switch` | 7.8 | `vga_restore_vram_v` | 3.9 |
| 6 | `gfx_rect_setup` | 3.5 | `gfx_rect_setup` | 2.9 | `gfx_rect_setup` | 3.9 |
| 7 | `mem_next` | 2.8 | `mem_claim_1` | 2.8 | `ui_task` | 2.0 |
| 8 | `mem_claim_1` | 2.1 | `ui_task` | 2.1 | `mem_next` | 1.9 |
| 9 | `ui_task` | 1.8 | `mou_eoi` | 1.8 | `mem_claim_1` | 1.9 |
| 10 | `gfx_clip_query` | 1.4 | `cur_get_mono` | 1.4 | `mou_eoi` | 1.9 |
| 11 | `mou_eoi` | 1.4 | `sch_account` | 1.4 | `font_width_x` | 1.3 |
| 12 | `cur_get_mono` | 1.4 | `kret_cx` | 1.4 | `dsk_vol_fixed_x` | 1.3 |
| 13 | `sch_account` | 1.4 | `mem_next` | 1.4 | `mou_isr` | 1.2 |
| 14 | `menu_widest` | 1.4 | `mou_isr` | 1.2 | `sch_account` | 1.0 |
| 15 | `mou_isr` | 1.1 | `sw_rect` | 1.1 | `sch_isr` | 0.7 |

Shares are of BUSY time; halted share of the window, C/H/V: 21% / 23% / 20%.

#### menu: close File

| # | CGA | % | Herc | % | VGA | % |
|---:|---|---:|---|---:|---|---:|
| 1 | `sch_switch` | 36.8 | `sch_switch` | 35.9 | `vga_restore_vram_v` | 39.4 |
| 2 | `sw_xfer` | 16.3 | `sw_xfer` | 18.1 | `sch_switch` | 27.3 |
| 3 | `task_yield` | 6.5 | `task_yield` | 5.6 | `vga_solid_rect_v` | 5.2 |
| 4 | `sw_plane_op` | 5.5 | `sw_plane_op` | 5.6 | `task_yield` | 3.9 |
| 5 | `sw_col` | 4.3 | `sw_col` | 3.9 | `menu_drop` | 3.4 |
| 6 | `BIOS` | 4.3 | `menu_drop` | 3.9 | `BIOS` | 3.0 |
| 7 | `menu_drop` | 3.8 | `menu_hover` | 3.9 | `kbm_pollm` | 1.7 |
| 8 | `kbm_p5spend` | 2.7 | `BIOS` | 3.4 | `kbm_p5spend` | 1.7 |
| 9 | `menu_hover` | 2.7 | `kbm_p5spend` | 2.3 | `mou_eoi` | 1.7 |
| 10 | `mou_eoi` | 2.2 | `kbd_ovspend` | 2.3 | `menu_hover` | 1.7 |
| 11 | `gfx_rect_setup` | 2.2 | `mou_eoi` | 2.2 | `mou_isr` | 1.6 |
| 12 | `mou_isr` | 2.1 | `sch_resume` | 1.7 | `kbd_ovspend` | 1.3 |
| 13 | `sch_isr` | 1.2 | `gfx_rect_setup` | 1.7 | `sch_isr` | 0.9 |
| 14 | `cw_kretf` | 1.1 | `mou_isr` | 1.5 | `cw_kretf` | 0.9 |
| 15 | `kbd_ovspend` | 1.1 | `sch_isr` | 1.2 | `sch_account` | 0.9 |

Shares are of BUSY time; halted share of the window, C/H/V: 0% / 0% / 0%.

#### pointer: sweep

| # | CGA | % | Herc | % | VGA | % |
|---:|---|---:|---|---:|---|---:|
| 1 | `sch_switch` | 26.1 | `sch_switch` | 27.6 | `sch_switch` | 24.4 |
| 2 | `cur_move_mono` | 25.5 | `cur_move_mono` | 26.4 | `vga_restore_vram_v` | 13.2 |
| 3 | `cur_mvrow` | 4.9 | `ui_task` | 6.0 | `vga_save_vram_v` | 10.8 |
| 4 | `sch_account` | 4.9 | `cur_mvrow` | 5.0 | `cur_draw` | 7.8 |
| 5 | `mou_eoi` | 4.9 | `mou_eoi` | 5.0 | `ui_task` | 5.5 |
| 6 | `ui_task` | 4.6 | `mou_isr` | 4.4 | `sch_account` | 5.4 |
| 7 | `mou_isr` | 4.5 | `sch_account` | 3.9 | `mou_eoi` | 4.8 |
| 8 | `cur_mvbg` | 4.4 | `cur_mvbg` | 2.7 | `mou_isr` | 4.3 |
| 9 | `sch_resume` | 2.7 | `cur_geom` | 2.5 | `gfx_rect_setup` | 3.5 |
| 10 | `cur_geom` | 2.5 | `mou_apply` | 2.5 | `mou_apply` | 2.5 |
| 11 | `task_yield` | 2.5 | `sch_resume` | 2.2 | `task_yield` | 2.3 |
| 12 | `mou_apply` | 2.0 | `sch_isr` | 1.9 | `gfx_rowbase` | 2.3 |
| 13 | `sch_isr` | 1.9 | `cur_move_1` | 1.8 | `cur_move_1` | 2.1 |
| 14 | `cur_move_1` | 1.8 | `task_yield` | 1.8 | `sch_isr` | 2.1 |
| 15 | `clk_tick` | 1.4 | `clk_tick` | 1.3 | `sch_resume` | 1.9 |

Shares are of BUSY time; halted share of the window, C/H/V: 64% / 63% / 62%.

#### type: type 31 keys

| # | CGA | % | Herc | % | VGA | % |
|---:|---|---:|---|---:|---|---:|
| 1 | `sch_switch` | 20.3 | `sch_switch` | 22.4 | `sch_switch` | 20.1 |
| 2 | `pkg Note Pad` | 14.7 | `pkg Note Pad` | 10.8 | `pkg Note Pad` | 9.4 |
| 3 | `font_run_x` | 5.8 | `font_run_x` | 6.8 | `vga_restore_vram_v` | 7.6 |
| 4 | `cur_put_mono` | 5.0 | `cur_put_mono` | 5.5 | `vga_save_vram_v` | 5.9 |
| 5 | `ui_task` | 4.9 | `ui_task` | 5.4 | `sch_account` | 5.4 |
| 6 | `sch_account` | 4.9 | `sch_account` | 5.3 | `BIOS` | 5.3 |
| 7 | `BIOS` | 4.5 | `BIOS` | 4.7 | `font_run_x` | 5.0 |
| 8 | `sw_col` | 3.9 | `sch_isr` | 4.0 | `cur_draw` | 4.7 |
| 9 | `cur_get_mono` | 3.5 | `cur_get_mono` | 3.9 | `ui_task` | 4.4 |
| 10 | `sch_isr` | 3.4 | `sch_resume` | 2.5 | `sch_isr` | 3.4 |
| 11 | `sw_rect` | 2.3 | `task_yield` | 1.9 | `gfx_rect_setup` | 2.4 |
| 12 | `sch_resume` | 2.2 | `clk_tick` | 1.8 | `sch_resume` | 1.3 |
| 13 | `ui_bill` | 1.9 | `sw_col` | 1.5 | `cur_vis` | 1.3 |
| 14 | `task_yield` | 1.6 | `cur_vis` | 1.4 | `clk_tick` | 1.3 |
| 15 | `task_sleep` | 1.4 | `wm_idx2ptr` | 1.4 | `task_yield` | 1.3 |

Shares are of BUSY time; halted share of the window, C/H/V: 79% / 81% / 78%.

#### launch: launch Calc cold

| # | CGA | % | Herc | % | VGA | % |
|---:|---|---:|---|---:|---|---:|
| 1 | `BIOS` | 24.2 | `BIOS` | 33.3 | `BIOS` | 28.2 |
| 2 | `lz_lzb` | 13.8 | `lz_lzb` | 12.2 | `lz_lzb` | 13.3 |
| 3 | `sch_switch` | 10.8 | `sch_switch` | 9.5 | `sch_switch` | 10.4 |
| 4 | `sw_col` | 9.1 | `sw_col` | 7.6 | `vga_save_vram_v` | 8.9 |
| 5 | `sw_plane_op` | 4.9 | `sw_plane_op` | 4.1 | `vga_solid_rect_v` | 8.4 |
| 6 | `font_run_x` | 3.4 | `lz_match` | 2.9 | `font_run_x` | 3.0 |
| 7 | `lz_match` | 3.2 | `font_run_x` | 2.8 | `lz_match` | 2.9 |
| 8 | `sw_xfer` | 2.5 | `sw_xfer` | 2.8 | `lz_copy` | 2.1 |
| 9 | `gfx_rect_setup` | 2.4 | `lz_copy` | 1.9 | `gfx_rect_setup` | 1.7 |
| 10 | `lz_copy` | 2.2 | `gfx_rect_setup` | 1.9 | `lz_take` | 1.3 |
| 11 | `lz_take` | 1.8 | `lz_take` | 1.2 | `lz_decomp_big` | 1.2 |
| 12 | `lz_decomp_big` | 1.3 | `task_yield` | 1.2 | `task_yield` | 1.2 |
| 13 | `task_yield` | 1.2 | `lz_decomp_big` | 1.2 | `dsk_copy_seg_x` | 1.0 |
| 14 | `sw_rect` | 1.2 | `sw_rect` | 1.1 | `pkg Calculator` | 0.9 |
| 15 | `dsk_copy_seg_x` | 1.0 | `dsk_copy_seg_x` | 0.9 | `sch_isr` | 0.8 |

Shares are of BUSY time; halted share of the window, C/H/V: 1% / 1% / 1%.

#### launch: launch Calc cached

| # | CGA | % | Herc | % | VGA | % |
|---:|---|---:|---|---:|---|---:|
| 1 | `lz_lzb` | 18.7 | `lz_lzb` | 18.3 | `lz_lzb` | 18.1 |
| 2 | `sch_switch` | 13.8 | `sch_switch` | 13.6 | `sch_switch` | 14.0 |
| 3 | `sw_col` | 11.9 | `sw_col` | 11.5 | `vga_save_vram_v` | 12.5 |
| 4 | `sw_plane_op` | 7.1 | `sw_plane_op` | 7.2 | `vga_solid_rect_v` | 12.4 |
| 5 | `font_run_x` | 4.4 | `sw_xfer` | 4.3 | `lz_match` | 4.4 |
| 6 | `lz_match` | 4.2 | `lz_match` | 4.2 | `font_run_x` | 4.2 |
| 7 | `sw_xfer` | 3.2 | `font_run_x` | 4.1 | `lz_copy` | 2.9 |
| 8 | `lz_copy` | 3.0 | `gfx_rect_setup` | 3.0 | `gfx_rect_setup` | 2.5 |
| 9 | `gfx_rect_setup` | 2.8 | `lz_copy` | 2.8 | `lz_take` | 2.3 |
| 10 | `lz_take` | 1.9 | `lz_take` | 2.4 | `lz_decomp_big` | 1.7 |
| 11 | `sw_rect` | 1.8 | `lz_decomp_big` | 1.7 | `task_yield` | 1.5 |
| 12 | `lz_decomp_big` | 1.8 | `task_yield` | 1.7 | `dsk_copy_seg_x` | 1.3 |
| 13 | `task_yield` | 1.5 | `sw_rect` | 1.6 | `pkg Calculator` | 1.3 |
| 14 | `pkg Calculator` | 1.4 | `pkg Calculator` | 1.4 | `sch_resume` | 1.0 |
| 15 | `dsk_copy_seg_x` | 1.4 | `dsk_copy_seg_x` | 1.4 | `gfx_rowbase` | 0.9 |

Shares are of BUSY time; halted share of the window, C/H/V: 1% / 1% / 1%.

#### scroll: PgDn

| # | CGA | % | Herc | % | VGA | % |
|---:|---|---:|---|---:|---|---:|
| 1 | `font_run_x` | 20.4 | `font_run_x` | 21.7 | `font_run_x` | 25.9 |
| 2 | `sw_col` | 16.0 | `sw_col` | 12.4 | `ico_pass` | 18.5 |
| 3 | `ico_pass_bb` | 14.1 | `ico_pass_bb` | 9.7 | `vga_solid_rect_v` | 13.4 |
| 4 | `sw_plane_op` | 10.4 | `vgas_bankcopy` | 9.4 | `gfx_scroll_v` | 8.5 |
| 5 | `ico_bbop_byte` | 4.2 | `sw_plane_op` | 8.3 | `fnt_rn_edge` | 2.7 |
| 6 | `fnt_rn_edge` | 2.9 | `ico_bbop_byte` | 6.9 | `fm_draw_icon16` | 1.7 |
| 7 | `gfx_nextrow` | 2.8 | `gfx_nextrow` | 4.4 | `dsk_copy_seg_x` | 1.7 |
| 8 | `gfx_rect_setup` | 2.4 | `fnt_rn_edge` | 2.9 | `gfx_rect_setup` | 1.6 |
| 9 | `dsk_copy_seg_x` | 1.9 | `fm_draw_icon16` | 1.6 | `vga_restore_vram_v` | 1.5 |
| 10 | `gfx_clip_query` | 1.5 | `dsk_copy_seg_x` | 1.3 | `fm_stage_name` | 1.4 |
| 11 | `fm_draw_icon16` | 1.4 | `sch_switch` | 1.1 | `gfx_fill_gray_raw_v` | 1.4 |
| 12 | `fm_ultoa_x` | 1.2 | `gfx_rect_setup` | 1.0 | `fm_draw_lrow` | 1.2 |
| 13 | `font_ink` | 1.2 | `sch_isr` | 0.8 | `fm_ultoa_x` | 1.0 |
| 14 | `sw_rect` | 1.2 | `fm_ultoa_x` | 0.8 | `gfx_rowbase` | 1.0 |
| 15 | `fm_stage_name` | 1.2 | `fm_stage_name` | 0.7 | `sch_switch` | 1.0 |

Shares are of BUSY time; halted share of the window, C/H/V: 0% / 0% / 0%.

#### scroll: PgUp

| # | CGA | % | Herc | % | VGA | % |
|---:|---|---:|---|---:|---|---:|
| 1 | `font_run_x` | 18.1 | `font_run_x` | 18.7 | `font_run_x` | 23.2 |
| 2 | `sw_col` | 16.5 | `sw_col` | 13.0 | `ico_pass` | 19.7 |
| 3 | `ico_pass_bb` | 14.4 | `ico_pass_bb` | 11.0 | `vga_solid_rect_v` | 14.4 |
| 4 | `sw_plane_op` | 11.4 | `vgas_bankcopy` | 10.0 | `gfx_scroll_v` | 9.2 |
| 5 | `ico_bbop_byte` | 5.1 | `sw_plane_op` | 8.7 | `dsk_copy_seg_x` | 2.1 |
| 6 | `gfx_nextrow` | 3.1 | `ico_bbop_byte` | 6.7 | `fnt_rn_edge` | 2.1 |
| 7 | `fnt_rn_edge` | 2.3 | `gfx_nextrow` | 4.4 | `fm_draw_icon16` | 1.8 |
| 8 | `sw_rect` | 2.2 | `fnt_rn_edge` | 2.4 | `vga_restore_vram_v` | 1.6 |
| 9 | `gfx_rect_setup` | 2.2 | `dsk_copy_seg_x` | 2.0 | `gfx_fill_gray_raw_v` | 1.6 |
| 10 | `dsk_copy_seg_x` | 2.0 | `fm_draw_icon16` | 1.8 | `fm_stage_name` | 1.3 |
| 11 | `fm_draw_icon16` | 1.6 | `sch_switch` | 1.2 | `gfx_rect_setup` | 1.2 |
| 12 | `sch_switch` | 1.2 | `ico_core` | 1.1 | `fm_draw_lrow` | 1.1 |
| 13 | `ico_core` | 1.0 | `fm_ultoa_x` | 1.0 | `ico_core` | 1.1 |
| 14 | `fm_stage_name` | 1.0 | `gfx_rect_setup` | 1.0 | `sch_switch` | 1.0 |
| 15 | `font_ink` | 1.0 | `fm_stage_name` | 1.0 | `dsk_ico_stage` | 1.0 |

Shares are of BUSY time; halted share of the window, C/H/V: 0% / 0% / 0%.

#### idle: idle 5s

| # | CGA | % | Herc | % | VGA | % |
|---:|---|---:|---|---:|---|---:|
| 1 | `sch_switch` | 44.8 | `sch_switch` | 44.8 | `sch_switch` | 44.7 |
| 2 | `sch_account` | 14.6 | `sch_account` | 14.6 | `sch_account` | 14.5 |
| 3 | `ui_task` | 12.5 | `ui_task` | 12.6 | `ui_task` | 12.7 |
| 4 | `sch_isr` | 10.0 | `sch_isr` | 10.0 | `sch_isr` | 10.0 |
| 5 | `sch_resume` | 7.8 | `sch_resume` | 7.8 | `sch_resume` | 7.9 |
| 6 | `clk_tick` | 7.5 | `clk_tick` | 7.5 | `clk_tick` | 7.5 |
| 7 | `evq_pop` | 2.5 | `evq_pop` | 2.4 | `evq_pop` | 2.4 |
| 8 | `sch_currec` | 0.4 | `sch_currec` | 0.4 | `sch_currec` | 0.5 |
| 9 |  |  |  |  |  |  |
| 10 |  |  |  |  |  |  |
| 11 |  |  |  |  |  |  |
| 12 |  |  |  |  |  |  |
| 13 |  |  |  |  |  |  |
| 14 |  |  |  |  |  |  |
| 15 |  |  |  |  |  |  |

Shares are of BUSY time; halted share of the window, C/H/V: 94% / 94% / 94%.

## 4. 286 and 386 on 86Box: RELATIVE ONLY

**These are not timings.** 86Box is not cycle-accurate (docs/86BOX.md, *What
it is NOT*). It steps in whole emulated milliseconds, and it has no logging
breakpoints, so a run's end is the first millisecond its DONE state is seen
at, and its busy figure is a share of 1 ms samples. Its devices also run on
host threads, so reps vary more than MartyPC's do. The machines are
`tools/os88box86api.py`'s: `at286_*` is vm/at286-port, the owner's mr286
(MR BIOS, a 16 MHz 286), and `at386_vga` is vm/386-ps2's 386SX with a serial
mouse. Use these numbers for one thing: a base-against-tip A/B on the same
86Box machine, to check that a change made on the 8088 does not lose on a
286. The last column, 286 against the MartyPC XT, gives the scale and is no
more than that.

| scenario / phase | 286 CGA ms | 286 Herc ms | 286 VGA ms | 386 VGA ms | XT (MartyPC) VGA ms | 286 VGA / XT VGA |
|---|---:|---:|---:|---:|---:|---:|
| desktop: boot | 10608.0 | 10400.0 | 11440.0 | 19136.0 | 15976.8 | 0.72 |
| opendisk: open A: | 1980.0 | 1009.0 | 1982.0 | 1100.0 | 1639.8 | 1.21 |
| raise: raise Disk | 81.0 | 113.0 | 115.0 | 126.0 | 366.1 | 0.31 |
| drag: drag | 467.0 | 473.0 | 524.0 | 531.0 | 611.0 | 0.86 |
| menu: open File | 40.0 | 41.0 | 40.0 | 45.0 | 50.6 | 0.79 |
| menu: close File | 57.0 | 37.0 | 36.0 | 40.0 | 28.7 | 1.26 |
| pointer: sweep | 958.0 | 955.0 | 956.0 | 961.0 | 632.2 | 1.51 |
| type: type 31 keys | 2792.0 | 2792.0 | 2792.0 | 2792.0 | 2941.5 | 0.95 |
| launch: launch Calc cold | 1740.0 | 1750.0 | 1767.0 | 1771.0 | 1419.3 | 1.25 |
| launch: launch Calc cached | 404.5 | 405.0 | 426.5 | 430.5 | 977.6 | 0.44 |
| scroll: PgDn | 20.0 | 22.0 | 22.0 | 24.0 | 104.1 | 0.21 |
| scroll: PgUp | 20.0 | 20.0 | 22.0 | 24.0 | 96.8 | 0.23 |
| idle: idle 5s | 5000.0 | 5000.0 | 5000.0 | 5000.0 | 5009.0 | 1.00 |

How to read it, given what 86Box is:

* The scenarios that draw are **3-5x faster on the 286**: raise 0.31x, PgDn
  0.21x and a cached launch 0.44x of the XT's VGA. The 386SX lands at or
  just below the 286. Its BIOS boots more slowly, and a 16 MHz 286 with a
  fast 286 board is not slower than a 386SX-16 in any way this measures.
* **Input-paced scenarios** barely move: `drag`, `menu`, `pointer` and
  `type` are set by the serial line and by the 90 ms key gap. `pointer`
  and `menu close` read longer than on MartyPC because 86Box's serial
  mouse paces packets on its own clock.
* **Disk scenarios** are 86Box's FDC model and an AT BIOS. Reps vary by up
  to 2x (`opendisk` on the 286 CGA read 1,043 and 1,980 ms in one run), so
  they are no use for an A/B at 3 reps.
* The 286 profiles of drag, raise and type (2 reps, 1 ms samples, CGA) are
  in the JSON (`box-prof-*`). The drag's `sch_switch` is 71% there, which
  is the evidence in section 2's item 1 that a drag's switches are waiting
  rather than working.

## 5. gfxbench and sysbench, per adapter (MartyPC, MEASURED)

Run with `os88hot.py gfxbench|sysbench --machine M` from the Bench menu,
and the report read back off the floppy. `us/op` is per iteration, net of
the loop overhead. A `!` marks a row that came near the PIT wrap and `t` a
row that is tick-timed. The full parsed reports are in the JSON
(`gfxbench`, `sysbench`).

### gfxbench: primitives, text, composites, API cells (us/op)

| section | row | CGA | Herc | VGA |
|---|---|---:|---:|---:|
| primitives | GFX_PIXEL | 538.80 | 538.81 | 483.69 |
| primitives | GFX_HLINE 8px | 539.54 | 539.53 | 488.67 |
| primitives | GFX_HLINE 256px | 764.54 | 764.87 | 619.43 |
| primitives | GFX_VLINE 8px | 757.52 | 750.02 | 569.19 |
| primitives | GFX_VLINE 128px | 4468.95 | 4371.08 | 2078.29 |
| primitives | GFX_FILL 8x8 | 666.48 | 658.78 | 554.74 |
| primitives | GFX_FILL 64x64 | 6338.86 | 6150.36 | 3555.67 |
| primitives | GFX_FILL 64x64 clipped | 7510.17 | 7327.57 | 4731.00 |
| primitives | mask line 127x32 | 3148.00 | 3148.00 | 3148.00 |
| primitives | xordiff 128x128 | 27699.83 | 27699.55 | 27699.58 |
| primitives | GFX_BLIT1 128x128 | 11612.32 | 11452.88 | 9048.75 |
| primitives | GFX_BLIT1 128x128 pen | 11671.48 | 11507.53 | 9212.56 |
| primitives | clear mask | 3113.01 | 3113.05 | 3113.05 |
| primitives | GFX_POINTS 8 pts | 1285.89 | 1285.88 | 3856.91 |
| primitives | GFX_POINTS 24 pts | 2947.89 | 2947.89 | 11217.42 |
| primitives | GFX_FILL 256x128 | 20391.67 | 20064.67 | 13367.97 |
| primitives | GFX_FILL 256x1 | 742.51 | 742.36 | 599.67 |
| primitives | GFX_FRAME 64x64 | 6142.19 | 6042.49 | 3509.19 |
| primitives | GFX_FILL_GRAY 64x64 | 6345.11 | 6150.99 | 4083.53 |
| primitives | GFX_FILL_PAT 64x64 | 13436.62 | 13079.21 | 5585.48 |
| primitives | GFX_XOR_FILL 64x64 | 9003.21 | 8913.89 | 6753.33 |
| primitives | GFX_XOR_RECT 64x64 | 4763.86 | 4673.90 | 3777.31 |
| primitives | GFX_XOR_RECT 256x128 | 8023.41 | 7859.14 | 5926.76 |
| primitives | GFX_XOR_RECT 256x1 | 1148.02 | 1148.02 | 1087.32 |
| primitives | GFX_BLIT4 solid | 44931.72! | 44931.51! | 39376.03! |
| primitives | GFX_BLIT4 4px runs | 45718.91! | 45718.91! | 183085.73w |
| primitives | GFX_BLITP 64x64 | 95.26 | 95.26 | 12376.57 |
| primitives | GFX_BLITP 256x16 | 95.12 | 95.26 | 9479.74 |
| primitives | GFX_SCROLL 256x128 | 30394.95 | 30093.60 | 19525.89 |
| text | FONT_CHAR one cell | 854.84 | 847.54 | 700.71 |
| text | FONT_STR 10 aligned | 8053.09 | 7959.71 | 6626.85 |
| text | PAIR 10 aligned | 9453.55 | 9337.55 | 7584.24 |
| text | FONT_RUN 10 aligned | 3268.38 | 3255.04 | 2982.02 |
| text | FONT_RUN 10 disabled | 3408.69 | 3398.42 | 3104.95 |
| text | FONT_RUN 10 coloured | 3267.19 | 3254.83 | 7975.21 |
| text | PAIR 10 coloured | 9287.61 | 9169.86 | 7584.24 |
| text | FONT_RUN 19 col/blk | 5261.17 | 5253.42 | 4823.33 |
| text | FONT_RUN 20 text | 5486.13 | 5472.93 | 5018.40 |
| text | FONT_RUN 20 padded | 3765.02 | 3749.58 | 3462.67 |
| text | PAIR 10 skewed | 10016.97 | 9907.18 | 8261.64 |
| text | FONT_RUN 10 skewed | 5308.66 | 5290.50 | 4930.54 |
| text | FONT_RUN 19 col/blk +5 | 8391.68 | 8366.19 | 7916.62 |
| text | FONT_WIDTH | 226.10 | 226.11 | 226.08 |
| API cells that draw nothing | GET_TICKS | 24.91 | 24.90 | 24.91 |
| API cells that draw nothing | SET_COLOR | 25.78 | 25.59 | 25.78 |
| API cells that draw nothing | GFX_PEN | 55.08 | 55.10 | 55.09 |
| API cells that draw nothing | WM_BAND set+retire | 337.85 | 337.83 | 337.84 |
| API cells that draw nothing | WM_CONTENT | 242.31 | 242.31 | 242.31 |
| API cells that draw nothing | WM_GEOM | 457.61 | 457.62 | 457.62 |
| API cells that draw nothing | WM_OBSCURED | 214.33 | 214.30 | 214.32 |
| API cells that draw nothing | WM_CLIP_TEST | 100.63 | 100.84 | 100.82 |
| API cells that draw nothing | WM_CLIP_SET+CLEAR | 961.45 | 961.44 | 961.44 |
| API cells that draw nothing | MOUSE | 75.23 | 75.23 | 75.23 |
| composite | GFX_UNLOCK+LOCK pair | 461.38 | 473.27 | 482.17 |
| composite | WM_TITLE strip | 33305.88! | 32621.99 | 25370.75 |
| composite | one full-width row | 18387.35 | 18382.46 | 16980.18 |
| composite | whole page of rows | 247165.74t | 521794.35t | 741497.24t |

### sysbench: interrupts, the API floor, the floppy (us/op unless named)

| section | row | CGA | Herc | VGA |
|---|---|---:|---:|---:|
| what the kernel own interrupts cost | work, interrupts off | 3594.44 | 3594.44 | 3594.44 |
| what the kernel own interrupts cost | work, interrupts on | 3707.48t | 3707.48t | 3707.48t |
| what the kernel own interrupts cost | interrupt load pct | 3 | 3 | 3 |
| the API far-call floor and the scheduler | near call + ret | 10.71 | 10.70 | 10.81 |
| the API far-call floor and the scheduler | API far call cell | 24.91 | 24.91 | 24.91 |
| the API far-call floor and the scheduler | TASK_YIELD | 644.22 | 644.21 | 644.20 |
| the API far-call floor and the scheduler | RAND | 86.35 | 86.34 | 86.36 |
| the API far-call floor and the scheduler | FILE_HERE | 156.52 | 156.51 | 156.51 |
| the API far-call floor and the scheduler | FILE_DFREE | 7612.47 | 7612.46 | 7612.42 |
| the floppy | read 16K, cold motor | 1318217.31t | 1208365.87t | 1373143.04t |
| the floppy | read error code | 0 | 0 | 0 |
| the floppy | bytes read | 16384 | 16384 | 16384 |
| the floppy | read 16K, warm | 988662.98t | 1043588.71t | 988662.98t |
| the floppy | read 1 sector file | 82388.58t | 82388.58t | 82388.58t |
| the floppy | floppy bytes/sec | 16582 | 15708 | 16582 |

## 6. Limits of this map

* **Single-window scenes.** The scenes are small on purpose, so that they
  are reproducible: two Disk windows at most. `tests/deskbench.py`'s
  four-window scene is the busy-desktop complement, and a wm or files item
  should be checked against both.
* **One scene per operation.** A raise of a Disk window is not a raise of
  Paint. The routine ranking is stable across scenes in kind, and its
  shares are not.
* **Disk.** The platter model reproduces the 5150's int 13h rows, but motor
  spin-up is instant here, and a disk number still goes to the 5150
  (docs/FIELD-MACHINES.md) before PERFORMANCE.md Part 9 quotes it.
* **`desktop`** is one rep, and at boot most samples are `BIOS`: the floppy
  being read. Its routine shares are those of a boot, not of a session.
* **`sch_switch`'s share** is real time spent in the scheduler: the idle
  task yields on every interrupt it takes (SPEC.md 8.1.2.2), and every
  mouse byte and key is one. That makes it the largest single kernel
  routine in `pointer`, `type`, `drag` and `idle`. See section 2 for
  `--local`.
